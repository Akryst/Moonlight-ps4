// On-screen menu: Sunshine app list + basic ini settings.
#pragma once
void ui_show_splash(int theme);

#include "../config.h"
#include "../gamestream/client.h"

#define UI_MENU_LAUNCH    0 /* launch stream with cfg->app_name */
#define UI_MENU_RECONNECT 1 /* host changed / retry connection */

/* Status screen (pairing, connecting…). Leaves VideoOut BGRA open. */
void ui_menu_set_notice(const char *message);
void ui_show_status(const char *title, const char *line1, const char *line2);

/* server may be NULL (no connection): Settings only + retry.
 * Returns UI_MENU_LAUNCH, UI_MENU_RECONNECT, or <0 (video error). */
int ui_menu_run(app_config_t *cfg, gs_server_t *server, const char *config_dir);
