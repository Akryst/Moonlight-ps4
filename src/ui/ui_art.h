#pragma once
#include "../gamestream/client.h"
#include "ui_draw.h"
void ui_art_clear(void);
void ui_art_start(gs_server_t *server,const char *config_dir);
void ui_art_stop(void);
unsigned ui_art_revision(void);
void ui_art_draw(gs_server_t *server, int app_id, ui_surface_t *surface,
                 int x, int y, int width, int height);
void ui_art_draw_round(gs_server_t *server, int app_id, ui_surface_t *surface,
                      int x, int y, int width, int height, int radius);
void ui_landscape(ui_surface_t *surface, int x, int y, int w, int h, int radius);
enum { UI_CROSS, UI_CIRCLE, UI_SQUARE, UI_TRIANGLE, UI_OPTIONS, UI_DPAD, UI_L1, UI_R1, UI_CONTROLS };
void ui_control_image(ui_surface_t *surface,int button,int x,int y,int size);
