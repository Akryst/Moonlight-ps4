#include "input_pad.h"
#include "../log.h"

#include <Limelight.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static int32_t s_pad = -1;
static atomic_bool s_quit;
static uint64_t s_combo_start_us;
static int s_announced;

/* Menu-mode state (edges + D-pad auto-repeat). */
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

int input_init(void) {
    atomic_store(&s_quit, false);
    s_combo_start_us = 0;
    s_announced = 0;

    int rc = scePadInit();
    if (rc < 0) {
        LOGE("input: scePadInit failed: 0x%08x", rc);
        return -1;
    }

    int32_t userId = 0;
    sceUserServiceGetInitialUser(&userId);
    s_pad = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (s_pad < 0) {
        LOGE("input: scePadOpen failed: 0x%08x", s_pad);
        return -1;
    }

    LOGI("input: pad open (handle=%d, user=%d)", s_pad, userId);
    return 0;
}

void input_shutdown(void) {
    if (s_pad >= 0) {
        scePadClose(s_pad);
        s_pad = -1;
    }
}

void input_request_quit(void) {
    atomic_store(&s_quit, true);
}

void input_reset(void) {
    atomic_store(&s_quit, false);
    s_combo_start_us = 0;
    s_announced = 0;
    s_menu_prev = 0;
    s_menu_rpt_start_us = 0;
    s_menu_rpt_last_us = 0;
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

void input_menu_absorb(void) {
    if (s_pad < 0)
        return;
    OrbisPadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad, &pad) < 0)
        return;
    s_menu_prev = pad_to_menu_mask(&pad);
    /* Touchpad is not in menu mask but OPTIONS is: already covered. */
    s_menu_rpt_start_us = 0;
    s_menu_rpt_last_us = 0;
}

void input_menu_wait_release(unsigned mask, int timeout_ms) {
    if (s_pad < 0 || !mask)
        return;
    for (int i = 0; i < timeout_ms; i++) {
        OrbisPadData pad;
        memset(&pad, 0, sizeof(pad));
        if (scePadReadState(s_pad, &pad) == 0) {
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
    if (s_pad < 0)
        return -1;

    OrbisPadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad, &pad) < 0)
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

bool input_poll(void) {
    if (s_pad < 0)
        return input_should_quit();

    OrbisPadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad, &pad) < 0)
        return input_should_quit();

    if (!s_announced) {
        // DualShock 4: PS type, rumble + touchpad + gyro.
        LiSendControllerArrivalEvent(
            0, 0x1, LI_CTYPE_PS,
            0xFFFFFFFF, // generic supported buttons
            LI_CCAP_RUMBLE | LI_CCAP_GYRO | LI_CCAP_TOUCHPAD | LI_CCAP_ANALOG_TRIGGERS);
        LiSendControllerBatteryEvent(0, LI_BATTERY_STATE_FULL, 100);
        s_announced = 1;
    }

    int buttons = 0;
    if (pad.buttons & ORBIS_PAD_BUTTON_UP)        buttons |= UP_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_DOWN)      buttons |= DOWN_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_LEFT)      buttons |= LEFT_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_RIGHT)     buttons |= RIGHT_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_CROSS)     buttons |= A_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_CIRCLE)    buttons |= B_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_SQUARE)    buttons |= X_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_TRIANGLE)  buttons |= Y_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_L1)        buttons |= LB_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_R1)        buttons |= RB_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_L3)        buttons |= LS_CLK_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_R3)        buttons |= RS_CLK_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_OPTIONS)   buttons |= PLAY_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) buttons |= TOUCHPAD_FLAG;

    /* The physical PS button belongs to the console shell. Send the host's
     * Guide button with a chord instead; keep its component buttons out of
     * the same host event. Releasing the chord releases Guide normally. */
    const unsigned guide_combo = ORBIS_PAD_BUTTON_L1 | ORBIS_PAD_BUTTON_R1 |
                                 ORBIS_PAD_BUTTON_OPTIONS;
    if ((pad.buttons & guide_combo) == guide_combo &&
        !(pad.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD)) {
        buttons &= ~(LB_FLAG | RB_FLAG | PLAY_FLAG);
        buttons |= SPECIAL_FLAG;
    }

    // Y axis inverted: PS4 grows downward, Moonlight upward.
    short lx = stick_to_short(pad.leftStick.x);
    short ly = stick_to_short_inverted(pad.leftStick.y);
    short rx = stick_to_short(pad.rightStick.x);
    short ry = stick_to_short_inverted(pad.rightStick.y);

    LiSendMultiControllerEvent(0, 0x1, buttons,
                               pad.analogButtons.l2, pad.analogButtons.r2,
                               lx, ly, rx, ry);

    // Quit combo: OPTIONS + TOUCHPAD for ~1 s.
    const int combo = ORBIS_PAD_BUTTON_OPTIONS | ORBIS_PAD_BUTTON_TOUCH_PAD;
    if ((pad.buttons & combo) == combo) {
        uint64_t now = now_us();
        if (!s_combo_start_us)
            s_combo_start_us = now;
        else if (now - s_combo_start_us > 1000000ull) {
            LOGI("input: quit combo detected");
            input_request_quit();
        }
    } else {
        s_combo_start_us = 0;
    }

    return input_should_quit();
}
