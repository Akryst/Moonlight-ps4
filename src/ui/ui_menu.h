// On-screen menu: Sunshine app list + basic ini settings.
#pragma once
void ui_show_splash(int theme,int reduced_motion);

#include "../config.h"
#include "../gamestream/client.h"
#include "../gamestream/launch_flow.h"

#define UI_MENU_LAUNCH    0 /* launch stream with cfg->app_name */
#define UI_MENU_RECONNECT 1 /* host changed / retry connection */
#define UI_MENU_RECONNECT_LAUNCH 2 /* reconnect, then launch cfg->app_name
                                      * right away (one click instead of two
                                      * after the connection dropped mid-menu) */
#define UI_MENU_EXIT 3 /* user picked Settings -> Exit: die the sanctioned way */

/* Status screen (pairing, connecting…). Leaves VideoOut BGRA open. */
void ui_menu_set_notice(const char *message);
void ui_show_status(const char *title, const char *line1, const char *line2);

/* Selected game display name: launch_selected() stores the tile's label so
 * the launch screen can say "Starting 'Desktop'..." instead of a numeric
 * app id. stream.c takes (and clears) it when the launch begins. */
void ui_menu_set_pending_game(const char *name);
const char *ui_menu_take_pending_game(void);

/* Animated, cancellable launch screen. Draws the themed
 * background, the game name, the worker's current phase text, a spinner
 * and an elapsed-seconds counter at ~30 fps while the launch ladder runs
 * on another thread. CIRCLE sets prog->cancelled; the loop returns once
 * prog->done goes up (the worker checks cancelled between its bounded
 * steps, so the wait to return is bounded by the current step).
 * verb: header above the game name ("STARTING" when NULL; callers pass
 * "RECONNECTING" for mid-stream drop recovery). */
void ui_launch_screen_run(const char *game, const char *verb,
                           launch_progress_t *prog);

/* when the ladder (or the RTSP/ENet handshake)
 * fails, stay on the launch screen and let the user act in place instead
 * of falling back to the menu with a notice. X runs it again, CIRCLE
 * returns; allow_refresh=1 also offers TRIANGLE to re-read the host's
 * app list first (stale app ids are the classic cause of a failed
 * /launch). Returns UI_LAUNCH_FAIL_RETRY / ..._REFRESH / ..._BACK. */
enum {
    UI_LAUNCH_FAIL_BACK = -1,
    UI_LAUNCH_FAIL_RETRY = 0,
    UI_LAUNCH_FAIL_REFRESH = 1,
};
int ui_launch_fail_screen_run(const char *game, const char *reason,
                              int allow_refresh);

/* server may be NULL (no connection): Settings only + retry.
 * Returns UI_MENU_LAUNCH, UI_MENU_RECONNECT, UI_MENU_RECONNECT_LAUNCH,
 * or <0 (video error). */
int ui_menu_run(app_config_t *cfg, gs_server_t *server, const char *config_dir);
