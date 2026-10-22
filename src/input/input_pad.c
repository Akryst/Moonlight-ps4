#include "input_pad.h"
#include "touch_gesture.h"
#include "guide_chord.h"
#include "stick_deadzone.h"
#include <pthread.h>
#include "../log.h"

#include <Limelight.h>
#include <math.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static int s_deadzone=8, s_swap_face;
static pthread_mutex_t pad_mutex=PTHREAD_MUTEX_INITIALIZER;
void input_set_preferences(int deadzone,bool swap) {
    s_deadzone=deadzone<0?0:deadzone>25?25:deadzone; s_swap_face=!!swap;
}

/* multi-controller support. Each DualShock 4 logged in on the
 * console belongs to one Orbis user (extra pads pressing the PS button
 * create guest users), so "how many pads do we have?" is really "which
 * users are logged in right now?". The login list is re-checked every
 * couple of seconds so pads that join mid-stream are picked up. */
#define MAX_PADS 4
#define USER_RECHECK_US 2000000ull

/* PS4 player lightbar colors (blue/red/green/pink). */
static const OrbisPadColor k_player_colors[MAX_PADS] = {
    {0, 0, 255, 0}, {255, 0, 0, 0}, {0, 255, 0, 0}, {255, 0, 255, 0},
};

typedef struct {
    guide_chord_t guide;
    int32_t handle;         /* scePad handle; <0 = slot unused */
    int32_t user_id;        /* Orbis user this slot was opened for */
    int announced;          /* arrival+battery sent for this session */
    char connected;         /* last read connected flag */
    char sent_neutral;      /* released all inputs to the host after drop */
    unsigned last_buttons;  /* host-visible flags (for neutral release) */
} pad_slot_t;

/* hold duration of the in-stream L3+R3 combo. The stats
 * hold used to be 0.5 s - easy to hit accidentally mid-game (L3+R3
 * is a common sprint+crouch chord). 2.5 s keeps it deliberate; a progress
 * ring draws around the hold so the wait is visible (renderer_videoout.c).
 * the hold now OPENS the quick menu (touchpad mouse, perf overlay,
 * bitrate, quit stream, quit and exit app). The old OPTIONS+TOUCHPAD quit
 * combo is gone - the menu's quit rows replace it. */
#define STATS_HOLD_US 2500000ull

static pad_slot_t s_pads[MAX_PADS];
static int s_initial_user = 0;
static atomic_bool s_quit;
static uint64_t s_user_recheck_us;

/* quick-menu state. s_qm_request is set by input_poll when the
 * L3+R3 hold completes (same thread as the stream loop, so no locking);
 * the stream loop takes it and calls input_quick_menu_engage(). While
 * s_quick_menu is set, input_poll forwards nothing to the host (the
 * menu reads pad 0 through input_menu_poll) and no combos fire. On
 * disengage each pad waits until it reads neutral before passthrough
 * resumes, so nothing held during menu browsing leaks into the game. */
static int s_qm_request;
static int s_quick_menu;
static int s_resume_neutral[MAX_PADS];

/* Touchpad-as-mouse state (pad 0 only). the gesture state machine
 * (touch_gesture.c) also handles two-finger scroll; this block keeps the
 * host button state + the shared gesture struct. */
static int s_touch_mouse;
static touch_gesture_t s_tg;
static atomic_ullong s_stats_combo_start_us;
static atomic_int s_stats_combo_done;
static int s_mouse_left, s_mouse_right;

/* motion sensing. The host (Sunshine) asks for gyro/accel reports
 * at a given rate via cl.setMotionEventState when a game wants motion;
 * input_poll() then samples the DS4 IMU (OrbisPadData.vel = angular
 * velocity, .acell = acceleration) and ships LiSendControllerMotionEvent
 * packets. Report rate is capped by the 250 Hz poll cadence.
 * s_motion_enabled is the Settings toggle ("Gyro motion input").
 * Off = do not announce LI_CCAP_GYRO/ACCEL (games never ask) AND skip
 * sampling if a request slips through anyway. */
static uint16_t s_motion_rate[MAX_PADS][2]; /* [slot][LI_MOTION_TYPE_* - 1] */
static uint64_t s_motion_last_us[MAX_PADS][2];
static int s_motion_enabled = 1;

/* host lightbar color (Steam's per-controller lightbar setting on
 * the Sunshine virtual pad, forwarded as control event 0x5502). Stored
 * per slot so a pad that reconnects mid-session re-applies it; a full
 * reset (input_reset / session end) drops back to the player colors. */
static OrbisPadColor s_host_led[MAX_PADS];
static int s_host_led_active[MAX_PADS];

/* Menu-mode state (edges + D-pad auto-repeat), pad 0 only. */
static unsigned s_menu_prev;
static uint64_t s_menu_rpt_start_us;
static uint64_t s_menu_rpt_last_us;

static uint64_t now_us(void) {
    // sceKernelGetProcessTime is in us per scene docs.
    extern uint64_t sceKernelGetProcessTime(void);
    return sceKernelGetProcessTime();
}

// PS4 stick: 0..255 with center ~128. Moonlight expects -32768..32767.
static short stick_to_short(uint8_t v) {
    int centered = ((int)v - 128) * 256;
    if (centered < -32768) centered = -32768;
    if (centered > 32767) centered = 32767;
    return (short)centered;
}

static short stick_to_short_inverted(uint8_t v) {
    int inverted = -(int)stick_to_short(v);
    if (inverted > 32767) inverted = 32767;
    return (short)inverted;
}

/* --- logged-in users -> pad handles ----------------------------------- */

static int slot_for_user(int32_t user_id) {
    for (int i = 0; i < MAX_PADS; i++)
        if (s_pads[i].handle >= 0 && s_pads[i].user_id == user_id)
            return i;
    return -1;
}

/* Open a handle for every logged-in user not yet served; close handles
 * whose user logged out. Called from input_poll every USER_RECHECK_US and
 * once at input_init(). Slot 0 is forced to the initial (foreground) user
 * so the menu keeps its pad through user shuffles. */
static void pads_sync_users(void) {
    OrbisUserServiceLoginUserIdList list;
    memset(&list, 0, sizeof(list));
    int rc = sceUserServiceGetLoginUserIdList(&list);
    if (rc < 0) {
        LOGW("input: GetLoginUserIdList failed 0x%08x", (unsigned)rc);
        return;
    }

    /* Close slots whose user disappeared. */
    for (int i = 0; i < MAX_PADS; i++) {
        pad_slot_t *p = &s_pads[i];
        if (p->handle < 0)
            continue;
        int seen = 0;
        for (int k = 0; k < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; k++) {
            if (list.userId[k] > 0 && list.userId[k] == p->user_id) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            LOGI("input: user %d logged out; closing slot %d",
                 (int)p->user_id, i);
            unsigned mask=0;
            for(int other=0;other<MAX_PADS;other++)if(other!=i && s_pads[other].handle>=0 && s_pads[other].connected)mask|=1u<<other;
            if(p->announced)LiSendMultiControllerEvent((short)i,(short)mask,0,0,0,0,0,0,0);
            scePadClose(p->handle);
            p->handle = -1;
            p->announced = 0;
            p->connected = 0;
        }
    }

    /* Open slots for newly logged-in users. The initial user takes slot 0
     * (menu pad); everyone else takes the first free slot. */
    for (int k = 0; k < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; k++) {
        int32_t user = list.userId[k];
        if (user <= 0 || slot_for_user(user) >= 0)
            continue;
        int slot = (user == s_initial_user) ? 0 : -1;
        if (slot < 0) {
            for (int i = 1; i < MAX_PADS; i++) {
                if (s_pads[i].handle < 0) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot < 0 || s_pads[slot].handle >= 0)
            continue; /* no room */
        int32_t h = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
        if (h < 0) {
            LOGW("input: scePadOpen(user=%d slot=%d) failed 0x%08x",
                 (int)user, slot, (unsigned)h);
            continue;
        }
        s_pads[slot].handle = h;
        s_pads[slot].user_id = user;
        s_pads[slot].announced = 0;
        s_pads[slot].connected = 0;
        s_pads[slot].sent_neutral = 0;
        (void)scePadSetLightBar(h, (OrbisPadColor *)&k_player_colors[slot]);
        LOGI("input: pad slot %d open (user=%d handle=%d)", slot,
             (int)user, (int)h);
    }
}

int input_init(void) {
    atomic_store(&s_quit, false);
    s_qm_request = 0;
    s_quick_menu = 0;
    memset(s_resume_neutral, 0, sizeof(s_resume_neutral));
    s_stats_combo_start_us = 0;
    s_stats_combo_done = 0;
    s_touch_mouse = 0;
    s_mouse_left = s_mouse_right = 0;
    memset(&s_tg, 0, sizeof(s_tg));
    memset(s_motion_rate, 0, sizeof(s_motion_rate));
    memset(s_motion_last_us, 0, sizeof(s_motion_last_us));
    memset(s_host_led_active, 0, sizeof(s_host_led_active));
    memset(s_pads, 0, sizeof(s_pads));
    for (int i = 0; i < MAX_PADS; i++) {
        s_pads[i].handle = -1;
        s_pads[i].user_id = -1;
    }
    s_user_recheck_us = now_us() + USER_RECHECK_US;

    int rc = scePadInit();
    if (rc < 0) {
        LOGE("input: scePadInit failed: 0x%08x", rc);
        return -1;
    }

    s_initial_user = 0;
    sceUserServiceGetInitialUser(&s_initial_user);
    pads_sync_users();
    /* Pads log in asynchronously; make sure the initial user's pad is open
     * even if the login list races (fall back to a direct open). */
    if (s_pads[0].handle < 0 && s_initial_user > 0) {
        int32_t h = scePadOpen(s_initial_user, ORBIS_PAD_PORT_TYPE_STANDARD,
                               0, NULL);
        if (h >= 0) {
            s_pads[0].handle = h;
            s_pads[0].user_id = s_initial_user;
            (void)scePadSetLightBar(h, (OrbisPadColor *)&k_player_colors[0]);
            LOGI("input: pad slot 0 open via initial user (handle=%d)",
                 (int)h);
        }
    }

    if (s_pads[0].handle < 0) {
        LOGE("input: no pad for the initial user");
        return -1;
    }
    LOGI("input: ready (slot0 handle=%d user=%d)", (int)s_pads[0].handle,
         s_initial_user);
    return 0;
}

void input_shutdown(void) {
    for (int i = 0; i < MAX_PADS; i++) {
        if (s_pads[i].handle >= 0) {
            scePadClose(s_pads[i].handle);
            s_pads[i].handle = -1;
        }
    }
}

static void input_rumble_locked(int slot, unsigned low, unsigned high) {
    if (slot < 0 || slot >= MAX_PADS || s_pads[slot].handle < 0)
        return;
    OrbisPadVibeParam vibe;
    vibe.lgMotor = (uint8_t)(low > 255 ? 255 : low);   /* large, slow motor */
    vibe.smMotor = (uint8_t)(high > 255 ? 255 : high);  /* small, fast motor */
    int rc = scePadSetVibration(s_pads[slot].handle, &vibe);
    if (rc < 0)
        LOGW("input: rumble slot %d failed 0x%08x", slot, (unsigned)rc);
}

void input_set_touchpad_mouse(int enable) {
    s_touch_mouse = enable ? 1 : 0;
    /* Mode switch mid-press: force-release mouse buttons + gestures. */
    s_mouse_left = 0;
    s_mouse_right = 0;
    memset(&s_tg, 0, sizeof(s_tg));
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,BUTTON_LEFT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,BUTTON_RIGHT);
}

/* the host asked for (or stopped asking for) motion reports on a
 * controller (LI_MOTION_TYPE_ACCEL / LI_MOTION_TYPE_GYRO, 0 Hz = stop).
 * Rate 0 clears the slot; the poller below does the actual sampling. */
static void input_motion_set_state_locked(int slot, int motion_type, int rate_hz) {
    if (slot < 0 || slot >= MAX_PADS)
        return;
    if (motion_type != LI_MOTION_TYPE_ACCEL && motion_type != LI_MOTION_TYPE_GYRO)
        return;
    int idx = motion_type - 1;
    int was = s_motion_rate[slot][idx] != 0;
    s_motion_rate[slot][idx] = rate_hz > 0 ? (uint16_t)rate_hz : 0;
    s_motion_last_us[slot][idx] = 0;
    int now_on = (s_motion_rate[slot][0] != 0) || (s_motion_rate[slot][1] != 0);
    if (was != now_on)
        LOGI("input: slot %d motion sensing %s (accel=%d Hz gyro=%d Hz)",
             slot, now_on ? "ON" : "off",
             (int)s_motion_rate[slot][0], (int)s_motion_rate[slot][1]);
}

/* Settings toggle. Off also un-advertises the motion caps at the
 * next arrival event, so hosts stop asking for reports entirely. */
void input_set_motion_enabled(int enable) {
    s_motion_enabled = enable ? 1 : 0;
    if (!s_motion_enabled)
        memset(s_motion_rate, 0, sizeof(s_motion_rate));
    LOGI("input: gyro/accel motion input %s", s_motion_enabled ? "ON" : "off");
}

/* the host set the virtual controller's lightbar color (Steam's
 * controller LED option on the Sunshine pad, e.g. via Steam Input). If
 * the physical DS4 is present it takes effect immediately; otherwise the
 * color is remembered and applied when the pad (re)connects. */
static void input_set_led_locked(int slot, unsigned r, unsigned g, unsigned b) {
    if (slot < 0 || slot >= MAX_PADS)
        return;
    OrbisPadColor col = { (uint8_t)r, (uint8_t)g, (uint8_t)b, 0 };
    s_host_led[slot] = col;
    s_host_led_active[slot] = 1;
    int applied = 0;
    if (s_pads[slot].handle >= 0 && s_pads[slot].connected)
        applied = scePadSetLightBar(s_pads[slot].handle, &col) >= 0;
    LOGI("input: host lightbar slot %d -> RGB %03u,%03u,%03u (%s)", slot,
         r, g, b, applied ? "applied" : "stored");
}

/* drop host lightbar colors and repaint the per-slot player
 * colors (blue/red/green/pink) on every open pad. Called when a stream
 * ends and at session start, so the console look returns with the menu
 * and each new session starts from the defaults the PS4 users know. */
void input_leds_restore(void) {
    memset(s_host_led_active, 0, sizeof(s_host_led_active));
    for (int i = 0; i < MAX_PADS; i++) {
        if (s_pads[i].handle >= 0)
            (void)scePadSetLightBar(s_pads[i].handle,
                                    (OrbisPadColor *)&k_player_colors[i]);
    }
}

/* --- quick-menu open/close ------------------------------------- */

/* Defined below (touchpad-mouse section / menu-mode section). */
static void mouse_send_button(int which, int down);
void input_menu_absorb(void);

int input_quick_menu_take_open(void) {
    if (!s_qm_request)
        return 0;
    s_qm_request = 0;
    return 1;
}

/* Pause passthrough and hand pad 0 to the menu. Every pad the host knows
 * about gets one all-zero controller event so the game does not keep
 * eating held buttons while the menu is up (the ring combo itself leaves
 * L3+R3 held). Idempotent. */
void input_quick_menu_engage(void) {
    if (s_quick_menu)
        return;
    s_quick_menu = 1;

    /* Touchpad-mouse state: force the shared gesture machine quiet. */
    mouse_send_button(BUTTON_LEFT, 0);
    mouse_send_button(BUTTON_RIGHT, 0);
    memset(&s_tg, 0, sizeof(s_tg));

    unsigned mask = 0;
    for (int i = 0; i < MAX_PADS; i++) {
        pad_slot_t *p = &s_pads[i];
        if (p->handle >= 0 && p->connected && p->announced) {
            mask |= 1u << i;
            p->last_buttons = 0;
            LiSendMultiControllerEvent((short)i, (short)mask, 0, 0, 0,
                                       0, 0, 0, 0);
        }
    }
    input_menu_absorb();
    LOGI("input: quick menu open (passthrough paused)");
}

/* Resume passthrough. Each open pad is gated on its next neutral read
 * (no buttons, triggers released, sticks centered, no touch) so the first
 * forwarded frame after the menu is calm; the game sees no snap. */
void input_quick_menu_disengage(void) {
    if (!s_quick_menu)
        return;
    s_quick_menu = 0;
    for (int i = 0; i < MAX_PADS; i++) {
        if (s_pads[i].handle >= 0)
            s_resume_neutral[i] = 1;
    }
    input_menu_absorb();
    LOGI("input: quick menu closed (passthrough resumes on neutral)");
}

static int pad_is_neutral(const OrbisPadData *pad) {
    if (pad->buttons)
        return 0;
    if (pad->analogButtons.l2 > 10 || pad->analogButtons.r2 > 10)
        return 0;
    int lx = (int)pad->leftStick.x - 128;
    int ly = (int)pad->leftStick.y - 128;
    int rx = (int)pad->rightStick.x - 128;
    int ry = (int)pad->rightStick.y - 128;
    if (lx > 24 || lx < -24 || ly > 24 || ly < -24)
        return 0;
    if (rx > 24 || rx < -24 || ry > 24 || ry < -24)
        return 0;
    if (pad->touch.fingers)
        return 0;
    return 1;
}

int input_stats_hold_progress(void) {
    uint64_t start=atomic_load(&s_stats_combo_start_us);
    if (!start || s_stats_combo_done)
        return 0;
    uint64_t held = now_us() - start;
    if (held >= STATS_HOLD_US)
        return 1000;
    return (int)(held * 1000 / STATS_HOLD_US);
}

void input_request_quit(void) {
    atomic_store(&s_quit, true);
}

void input_reset(void) {
    atomic_store(&s_quit, false);
    s_qm_request = 0;
    s_quick_menu = 0;
    memset(s_resume_neutral, 0, sizeof(s_resume_neutral));
    s_stats_combo_start_us = 0;
    s_stats_combo_done = 0;
    s_mouse_left = s_mouse_right = 0;
    memset(&s_tg, 0, sizeof(s_tg));
    /* Motion requests are per-game on the host: a new session must not
     * keep streaming the previous game's requests. */
    memset(s_motion_rate, 0, sizeof(s_motion_rate));
    memset(s_motion_last_us, 0, sizeof(s_motion_last_us));
    /* New streaming session: re-announce every pad to the host, and start
     * from the per-slot player lightbar colors (any host/Steam lightbar
     * color arrives fresh with the new session). */
    input_leds_restore();
    for (int i = 0; i < MAX_PADS; i++) {
        s_pads[i].announced = 0;
        s_pads[i].guide=(guide_chord_t){0};
        s_pads[i].sent_neutral = 0;
    }
}

static unsigned pad_to_menu_mask(const OrbisPadData *pad) {
    unsigned h = 0;
    if (pad->buttons & ORBIS_PAD_BUTTON_UP)        h |= MENU_BTN_UP;
    if (pad->buttons & ORBIS_PAD_BUTTON_DOWN)      h |= MENU_BTN_DOWN;
    if (pad->buttons & ORBIS_PAD_BUTTON_LEFT)      h |= MENU_BTN_LEFT;
    if (pad->buttons & ORBIS_PAD_BUTTON_RIGHT)     h |= MENU_BTN_RIGHT;
    if (pad->buttons & ORBIS_PAD_BUTTON_CROSS)     h |= MENU_BTN_CROSS;
    if (pad->buttons & ORBIS_PAD_BUTTON_CIRCLE)    h |= MENU_BTN_CIRCLE;
    if (pad->buttons & ORBIS_PAD_BUTTON_SQUARE)    h |= MENU_BTN_SQUARE;
    if (pad->buttons & ORBIS_PAD_BUTTON_TRIANGLE)  h |= MENU_BTN_TRIANGLE;
    if (pad->buttons & ORBIS_PAD_BUTTON_L1)        h |= MENU_BTN_L1;
    if (pad->buttons & ORBIS_PAD_BUTTON_R1)        h |= MENU_BTN_R1;
    if (pad->buttons & ORBIS_PAD_BUTTON_OPTIONS)   h |= MENU_BTN_OPTIONS;
    return h;
}

/* --- menu-mode polling: pad 0 only, nothing sent to the host --------- */

static int menu_read(OrbisPadData *pad) {
    if (s_pads[0].handle < 0)
        return -1;
    memset(pad, 0, sizeof(*pad));
    if (scePadReadState(s_pads[0].handle, pad) < 0)
        return -1;
    return 0;
}

void input_menu_absorb(void) {
    OrbisPadData pad;
    if (menu_read(&pad) != 0)
        return;
    s_menu_prev = pad_to_menu_mask(&pad);
    s_menu_rpt_start_us = 0;
    s_menu_rpt_last_us = 0;
}

void input_menu_wait_release(unsigned mask, int timeout_ms) {
    if (s_pads[0].handle < 0 || !mask)
        return;
    for (int i = 0; i < timeout_ms; i++) {
        OrbisPadData pad;
        if (menu_read(&pad) == 0) {
            unsigned h = pad_to_menu_mask(&pad);
            if (pad.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD)
                h |= MENU_BTN_OPTIONS; /* treat touchpad as "held" */
            if ((h & mask) == 0) {
                s_menu_prev = h;
                return;
            }
            s_menu_prev = h; /* keep absorbing while held */
        }
        sceKernelUsleep(1000);
    }
    input_menu_absorb();
}

int input_menu_poll(unsigned *pressed, unsigned *held) {
    *pressed = 0;
    *held = 0;
    OrbisPadData pad;
    if (menu_read(&pad) != 0)
        return -1;

    unsigned h = pad_to_menu_mask(&pad);
    unsigned edges = h & ~s_menu_prev;

    /* Auto-repeat D-pad only: 350 ms delay, then every 90 ms. */
    const unsigned dpad = MENU_BTN_UP | MENU_BTN_DOWN | MENU_BTN_LEFT | MENU_BTN_RIGHT;
    uint64_t now = now_us();
    if (h & dpad) {
        if (edges & dpad) {
            s_menu_rpt_start_us = now;
            s_menu_rpt_last_us = now;
        } else if (now - s_menu_rpt_start_us > 350000ull &&
                   now - s_menu_rpt_last_us > 90000ull) {
            edges |= h & dpad;
            s_menu_rpt_last_us = now;
        }
    } else {
        s_menu_rpt_start_us = 0;
    }

    s_menu_prev = h;
    *pressed = edges;
    *held = h;
    return 0;
}

bool input_should_quit(void) {
    return atomic_load(&s_quit);
}

/* --- touchpad-as-mouse (pad 0) ---------------------------------------- */

static void mouse_send_move(int dx, int dy) {
    if (dx == 0 && dy == 0)
        return;
    LiSendMouseMoveEvent((short)dx, (short)dy);
}

static void mouse_send_button(int which, int down) {
    int *cur = which == BUTTON_LEFT ? &s_mouse_left : &s_mouse_right;
    if (*cur == down)
        return;
    *cur = down;
    LiSendMouseButtonEvent(down ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE,
                            which);
}

/* Map DS4 touch data to host mouse events via the gesture machine
 * (touch_gesture.c): 1 finger = pointer, 1-finger click = left button,
 * 2-finger click = right button, 2-finger drag = wheel scroll. */
static void touchpad_mouse_process(const OrbisPadData *pad) {
    touch_report_t rep;
    memset(&rep, 0, sizeof(rep));
    rep.click = (pad->buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) != 0;
    int fingers = pad->touch.fingers;
    if (fingers > ORBIS_PAD_MAX_TOUCH_NUM)
        fingers = ORBIS_PAD_MAX_TOUCH_NUM;
    rep.fingers = fingers > TG_FINGERS ? TG_FINGERS : fingers;
    for (int i = 0; i < rep.fingers; i++) {
        rep.id[i] = pad->touch.touch[i].finger;
        rep.x[i] = pad->touch.touch[i].x;
        rep.y[i] = pad->touch.touch[i].y;
    }

    tg_action_t acts[8];
    int dx[8], dy[8];
    int n = tg_feed(&s_tg, &rep, acts, 8, dx, dy);
    for (int i = 0; i < n; i++) {
        switch (acts[i]) {
        case TG_ACT_MOVE:
            mouse_send_move(dx[i], dy[i]);
            break;
        case TG_ACT_SCROLL:
            /* Vertical: negative wheel = content scrolls down (fingers
             * move up the pad = dy negative in PS4 coordinates, where Y
             * grows downward). LiSendHighResScrollEvent takes Windows
             * wheel deltas (120 per notch, positive = up). Horizontal
             * goes through the matching H-scroll API. */
            if (dy[i] != 0)
                LiSendHighResScrollEvent((short)-dy[i]);
            if (dx[i] != 0)
                LiSendHighResHScrollEvent((short)dx[i]);
            break;
        case TG_ACT_L_DOWN: mouse_send_button(BUTTON_LEFT, 1); break;
        case TG_ACT_L_UP:   mouse_send_button(BUTTON_LEFT, 0); break;
        case TG_ACT_R_DOWN: mouse_send_button(BUTTON_RIGHT, 1); break;
        case TG_ACT_R_UP:   mouse_send_button(BUTTON_RIGHT, 0); break;
        default: break;
        }
    }
}

/* --- streaming-mode polling: all pads --------------------------------- */

static unsigned pad_to_host_buttons(const OrbisPadData *pad) {
    unsigned buttons = 0;
    if (pad->buttons & ORBIS_PAD_BUTTON_UP)        buttons |= UP_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_DOWN)      buttons |= DOWN_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_LEFT)      buttons |= LEFT_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_RIGHT)     buttons |= RIGHT_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_CROSS)     buttons |= s_swap_face?B_FLAG:A_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_CIRCLE)    buttons |= s_swap_face?A_FLAG:B_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_SQUARE)    buttons |= X_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_TRIANGLE)  buttons |= Y_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_L1)        buttons |= LB_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_R1)        buttons |= RB_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_L3)        buttons |= LS_CLK_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_R3)        buttons |= RS_CLK_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_OPTIONS)   buttons |= PLAY_FLAG;
    if (pad->buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) buttons |= TOUCHPAD_FLAG;
    return buttons;
}

static void slot_forward(int slot, const OrbisPadData *pad, unsigned mask,
                         int touch_mouse_mode) {
    pad_slot_t *p = &s_pads[slot];

    /* passthrough stays held back until this pad reads neutral
     * after the quick menu closes (a disconnected pad clears the gate
     * and takes the normal disconnect path below). */
    if (s_resume_neutral[slot]) {
        if (!pad->connected)
            s_resume_neutral[slot] = 0;
        else if (!pad_is_neutral(pad))
            return;
        else
            s_resume_neutral[slot] = 0;
    }

    if (!pad->connected) {
        /* Release everything the host thinks is held, once. */
        if (p->connected) {
            LOGI("input: slot %d disconnected", slot);
            p->connected = 0;
            p->announced = 0;
        }
        if (!p->sent_neutral) {
            p->sent_neutral = 1;
            p->last_buttons = 0;
            LiSendMultiControllerEvent((short)slot, (short)mask, 0, 0, 0,
                                       0, 0, 0, 0);
        }
        return;
    }

    if (!p->connected) {
        p->connected = 1;
        p->guide=(guide_chord_t){0};
        p->sent_neutral = 0;
        /* Lightbar: the host's color (Steam via Sunshine) when it set one
         * this session, else the slot's player color. */
        (void)scePadSetLightBar(p->handle,
                                s_host_led_active[slot]
                                    ? &s_host_led[slot]
                                    : (OrbisPadColor *)&k_player_colors[slot]);
        LOGI("input: slot %d connected (user=%d)", slot, (int)p->user_id);
    }

    if (!p->announced) {
        /* DualShock 4: PS type, rumble + touchpad + gyro + accel + RGB
         * lightbar. Gyro/accel caps are announced only when the Settings toggle
         * is on (games then never request motion); the lightbar cap is
         * always announced so Steam's color reaches the DS4. */
        unsigned caps = LI_CCAP_RUMBLE | LI_CCAP_TOUCHPAD |
                        LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RGB_LED |
                        (s_motion_enabled ? (LI_CCAP_GYRO | LI_CCAP_ACCEL) : 0);
        LiSendControllerArrivalEvent(
            (uint8_t)slot, (uint16_t)mask, LI_CTYPE_PS,
            0xFFFFFFFF,
            caps);
        LiSendControllerBatteryEvent((uint8_t)slot, LI_BATTERY_STATE_FULL,
                                     100);
        p->announced = 1;
        LOGI("input: announced slot %d to host (mask=0x%x caps=0x%02x)",
             slot, mask, caps);
    }

    int buttons = (int)pad_to_host_buttons(pad);

    int neutral=guide_chord_neutral(&p->guide);
    buttons=guide_chord_filter(&p->guide,buttons,now_us());
    neutral|=guide_chord_neutral(&p->guide);

    /* Touchpad mouse mode (slot 0): the touch surface drives the host
     * cursor; the physical click becomes left/right mouse. The gamepad
     * "touchpad button" is then not forwarded. */
    if (touch_mouse_mode && slot == 0 && !neutral) {
        buttons &= ~(unsigned)TOUCHPAD_FLAG;
        touchpad_mouse_process(pad);
    }

    // Y axis inverted: PS4 grows downward, Moonlight upward.
    short lx = stick_deadzone(stick_to_short(pad->leftStick.x),s_deadzone);
    short ly = stick_deadzone(stick_to_short_inverted(pad->leftStick.y),s_deadzone);
    short rx = stick_deadzone(stick_to_short(pad->rightStick.x),s_deadzone);
    short ry = stick_deadzone(stick_to_short_inverted(pad->rightStick.y),s_deadzone);

    if(neutral)lx=ly=rx=ry=0;
    p->last_buttons = (unsigned)buttons;
    LiSendMultiControllerEvent((short)slot, (short)mask, buttons,
                               neutral?0:pad->analogButtons.l2, neutral?0:pad->analogButtons.r2,
                               lx, ly, rx, ry);
}

static bool input_poll_locked(void) {
    uint64_t now = now_us();
    if ((int64_t)(now - s_user_recheck_us) >= 0) {
        pads_sync_users();
        s_user_recheck_us = now + USER_RECHECK_US;
    }

    unsigned mask = 0;
    OrbisPadData reads[MAX_PADS];
    int have[MAX_PADS];
    for (int i = 0; i < MAX_PADS; i++) {
        have[i] = 0;
        if (s_pads[i].handle >= 0) {
            memset(&reads[i], 0, sizeof(reads[i]));
            have[i]=1;
            if(scePadReadState(s_pads[i].handle,&reads[i])!=0)memset(&reads[i],0,sizeof(reads[i]));
            if(reads[i].connected)mask|=1u<<i;
        }
    }

    /* while the quick menu is up, nothing is forwarded to the host
     * and no combos fire - pad 0 is being read by input_menu_poll() on
     * the same thread, and the game already got its neutral frames when
     * the menu opened. Reads above keep pads_sync_users() cadence and
     * per-slot bookkeeping intact. */
    if (s_quick_menu)
        return input_should_quit();

    for (int i = 0; i < MAX_PADS; i++)
        if (have[i])
            slot_forward(i, &reads[i], mask, s_touch_mouse);

    /* Quick-menu combo: L3 + R3 held ~2.5 s (was 0.5 s; the hold
     * shows a progress ring, see input_stats_hold_progress). The
     * hold requests the in-stream quick menu instead of toggling the
     * perf overlay directly (the overlay lives on the menu now). Fires
     * once per hold; both must be released before it can fire again. */
    if (have[0]) {
        unsigned both = ORBIS_PAD_BUTTON_L3 | ORBIS_PAD_BUTTON_R3;
        if ((reads[0].buttons & both) == both) {
            if (!s_stats_combo_done) {
                if (!s_stats_combo_start_us)
                    s_stats_combo_start_us = now;
                else if (now - s_stats_combo_start_us > STATS_HOLD_US) {
                    s_stats_combo_done = 1;
                    s_qm_request = 1;
                    LOGI("input: L3+R3 held 2.5 s - quick menu requested");
                }
            }
        } else {
            s_stats_combo_start_us = 0;
            s_stats_combo_done = 0;
        }
    }

    /* Motion sensing: for every slot with an active host request,
     * sample the DS4 IMU at the requested rate (bounded by our 250 Hz
     * poll) and ship motion events. OrbisPadData carries vel (angular
     * velocity, rad/s) and acell (linear acceleration, m/s^2, gravity
     * included); common-c wants deg/s and m/s^2. The Settings
     * toggle gates the whole block. */
    if (s_motion_enabled)
    for (int slot = 0; slot < MAX_PADS; slot++) {
        if (s_pads[slot].handle < 0 || !have[slot] || !reads[slot].connected)
            continue;
        for (int t = 0; t < 2; t++) {
            if (!s_motion_rate[slot][t])
                continue;
            uint64_t period = 1000000ull / s_motion_rate[slot][t];
            if (period < 4000ull)
                period = 4000ull; /* poll cadence cap */
            uint64_t last = s_motion_last_us[slot][t];
            if (last && now - last < period)
                continue;
            s_motion_last_us[slot][t] = now;

            /* scePadReadStateExt reports the extended pad data (motion
             * sensors) where the plain read may leave them zero. It is a
             * separate read so the game input path above is untouched; on
             * failure fall back to the main read's copy. */
            OrbisPadData ext;
            memset(&ext, 0, sizeof(ext));
            const OrbisPadData *src = &reads[slot];
            if (scePadReadStateExt(s_pads[slot].handle, &ext) == 0 &&
                ext.connected)
                src = &ext;

            if (t + 1 == LI_MOTION_TYPE_GYRO) {
                /* rad/s -> deg/s. */
                LiSendControllerMotionEvent(
                    (uint8_t)slot, LI_MOTION_TYPE_GYRO,
                    src->vel.x * 57.29578f,
                    src->vel.y * 57.29578f,
                    src->vel.z * 57.29578f);
            } else {
                LiSendControllerMotionEvent(
                    (uint8_t)slot, LI_MOTION_TYPE_ACCEL,
                    src->acell.x, src->acell.y, src->acell.z);
            }
        }
    }

    return input_should_quit();
}

bool input_poll(void) {pthread_mutex_lock(&pad_mutex);bool result=input_poll_locked();pthread_mutex_unlock(&pad_mutex);return result;}

void input_rumble(int slot,unsigned low,unsigned high) {pthread_mutex_lock(&pad_mutex);input_rumble_locked(slot,low,high);pthread_mutex_unlock(&pad_mutex);}

void input_set_led(int slot,unsigned r,unsigned g,unsigned b) {pthread_mutex_lock(&pad_mutex);input_set_led_locked(slot,r,g,b);pthread_mutex_unlock(&pad_mutex);}

void input_motion_set_state(int slot,int motion_type,int rate_hz) {pthread_mutex_lock(&pad_mutex);input_motion_set_state_locked(slot,motion_type,rate_hz);pthread_mutex_unlock(&pad_mutex);}
