/* Touchpad gesture state machine (pad 0, "touchpad mouse" mode).
 *
 * Pure logic, no Orbis calls: input_pad.c feeds it raw touch reports and
 * physical-click state each poll; it decides what the samples mean. This
 * file builds for host unit tests too (mock/test touch cases), so the
 * gesture rules stay verifiable without a DualShock.
 *
 * Gestures (DS4 touchpad, 1920x950 touch units, up to 2 touch points):
 *   - 1 finger moving      -> host mouse pointer (relative)
 *   - 1 finger + click     -> left mouse button
 *   - 2 fingers + click    -> right mouse button (only if the pair has not
 *                             turned into a scroll: keep right-click exact)
 *   - 2 fingers moving     -> mouse wheel (vertical dominant; horizontal
 *                             supported). Firing the scroll needs a small
 *                             movement threshold so that a "2-finger
 *                             click" (fingers land, pad is pressed
 *                             physically, no drag) never scrolls.
 *
 * Movement units: DS4 touch deltas are summed per poll; the pointer
 * accumulator is the same float+flush scheme as the pointer path. Scroll
 * quantizes accumulated travel into Windows wheel ticks (120 = one notch)
 * so it works with every host (Sunshine maps it to REL_WHEEL /
 * REL_WHEEL_HI_RES; old GFE ignores the fractional part gracefully).
 */
#pragma once

#define TG_FINGERS 2

typedef enum {
    TG_NONE = 0,
    TG_POINTER,   /* 1 finger active -> pointer deltas */
    TG_PENDING,   /* 2 fingers active, under the scroll threshold */
    TG_SCROLL,    /* 2 fingers locked into scrolling */
} tg_mode_t;

typedef enum {
    TG_ACT_NONE = 0,
    TG_ACT_MOVE,     /* dx/dy = pointer delta (touch units) */
    TG_ACT_SCROLL,   /* dy/dx = high-res wheel delta (multiples of 120) */
    TG_ACT_L_DOWN,   /* left button press */
    TG_ACT_L_UP,     /* left button release */
    TG_ACT_R_DOWN,   /* right button press */
    TG_ACT_R_UP,     /* right button release */
} tg_action_t;

typedef struct {
    tg_mode_t mode;

    int id[TG_FINGERS];   /* per-slot finger id; 0 = slot empty */
    int x[TG_FINGERS], y[TG_FINGERS];

    /* Pointer accumulation (1 finger). */
    float acc_x, acc_y;

    /* Scroll accumulation (2 fingers): average of the tracked pair. */
    float acc_sx, acc_sy;

    /* Pending-mode travel: |movement| since the second finger landed. */
    int travel;

    /* Button state so clicks can be released on gesture transitions. */
    int left, right;
} touch_gesture_t;

/* One touch report: fingers = number of valid entries in id/x/y arrays
 * (0..2). click = the physical touchpad button is pressed. */
typedef struct {
    int fingers;
    int id[TG_FINGERS];
    int x[TG_FINGERS];
    int y[TG_FINGERS];
    int click;
} touch_report_t;

void tg_reset(touch_gesture_t *g);

/* Feed one report; returns 0..3 actions (out array). Each action carries
 * the relevant value in dx/dy. */
int tg_feed(touch_gesture_t *g, const touch_report_t *r, tg_action_t *out,
            int max_out, int *dx, int *dy);

/* Sensitivity knobs (kept here so tests can mirror production values). */
#define TG_SCROLL_LOCK_PX   40   /* pair travel before locking into scroll */
#define TG_SCROLL_PX_PER_TICK 128 /* touch px per one wheel notch */
#define TG_WHEEL_UNIT       120   /* Windows wheel delta per notch */
#define TG_POINTER_CLAMP    3000
