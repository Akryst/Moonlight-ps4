#pragma once

#include <stdbool.h>

// Initialize scePad and open handles for every logged-in user
// (up to four DualShock 4s for local co-op; pad 0 belongs to the initial
// user and also drives the menu UI).
int input_init(void);
void input_shutdown(void);

// Read all connected pads and send their state to Moonlight (controller
// number = pad slot). Returns true when the user requests quit (the
// quick menu's quit rows, or the external flag). The OPTIONS+
// TOUCHPAD quit combo is gone - hold L3+R3 to open the quick menu.
bool input_poll(void);

// Request quit from another thread (connectionTerminated, etc.).
void input_request_quit(void);
bool input_should_quit(void);

// Reset quit/combo/controller announcement (before entering menu or stream).
void input_reset(void);
void input_set_preferences(int deadzone_percent,bool swap_face_buttons);

/* host rumble event for controller `slot` (0..3). low/high are
 * 0..255 for the large/slow and small/fast DS4 motors respectively. */
void input_rumble(int slot, unsigned low, unsigned high);

/* when enabled, DS4 touchpad touch moves the host mouse and the
 * physical click is left click (two fingers = right click). The click is
 * then no longer forwarded as the gamepad touchpad button. A
 * two-finger drag also scrolls (see touch_gesture.h for the rules). */
void input_set_touchpad_mouse(int enable);

/* hold progress (0..1000) of the in-stream quick-menu combo
 * (L3+R3, 2.5 s); the renderer turns it into an on-screen progress ring.
 * 0 = idle. (The old OPTIONS+TOUCHPAD quit combo is gone.) */
int input_stats_hold_progress(void);

/* in-stream quick menu. input_poll sets an open request when the
 * L3+R3 hold completes; the stream loop takes it with take_open() and
 * calls engage(): one all-zero controller event per announced pad (the
 * game stops eating held buttons), pad 0 switches to input_menu_poll()
 * and nothing is forwarded to the host until disengage(). Disengage gates
 * each pad's passthrough on its next neutral read, so nothing held
 * during menu browsing leaks into the game. Same-thread as input_poll. */
int  input_quick_menu_take_open(void);
void input_quick_menu_engage(void);
void input_quick_menu_disengage(void);

/* Motion sensing: host request (via cl.setMotionEventState) to
 * report gyro/accelerometer data for `slot` at rate_hz (0 Hz = stop).
 * motion_type is LI_MOTION_TYPE_ACCEL / LI_MOTION_TYPE_GYRO.
 * input_set_motion_enabled(0) mutes the whole path - the arrival
 * event then omits the gyro/accel caps (so games never ask) and any
 * stray request stops the sampler. */
void input_motion_set_state(int slot, int motion_type, int rate_hz);
void input_set_motion_enabled(int enable);

/* host (Steam via Sunshine, control event 0x5502) set the virtual
 * controller's lightbar color; mirror it on the physical DualShock 4 for
 * `slot`. Remembered per slot until the next session (input_reset restores
 * the per-slot player colors when the stream ends). r/g/b are 0..255. */
void input_set_led(int slot, unsigned r, unsigned g, unsigned b);

/* Restore the per-slot player lightbar colors and forget any host LED
 * state (session end / return to menu). */
void input_leds_restore(void);

/* Mark current pad state as "already seen" (no edges). Call when entering
 * a menu-driven screen to avoid phantom presses on the first poll. */
void input_menu_absorb(void);

/* Wait until launch buttons are released (OPTIONS/X/O). timeout_ms. */
void input_menu_wait_release(unsigned mask, int timeout_ms);

// --- Menu mode (UI): read pad 0 without sending anything to Moonlight ---
#define MENU_BTN_UP       0x0001u
#define MENU_BTN_DOWN     0x0002u
#define MENU_BTN_LEFT     0x0004u
#define MENU_BTN_RIGHT    0x0008u
#define MENU_BTN_CROSS    0x0010u
#define MENU_BTN_CIRCLE   0x0020u
#define MENU_BTN_SQUARE   0x0040u
#define MENU_BTN_TRIANGLE 0x0080u
#define MENU_BTN_L1       0x0100u
#define MENU_BTN_R1       0x0200u
#define MENU_BTN_OPTIONS  0x0400u

// pressed: edges this frame (with D-pad auto-repeat); held: state.
// Returns 0 if pad was read, <0 if no controller.
int input_menu_poll(unsigned *pressed, unsigned *held);
