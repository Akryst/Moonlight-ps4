#pragma once
#include "../gamestream/client.h"
#include "ui_draw.h"
void ui_art_clear(void);
void ui_art_draw(gs_server_t *server, int app_id, ui_surface_t *surface,
                 int x, int y, int width, int height);
void ui_art_draw_round(gs_server_t *server, int app_id, ui_surface_t *surface,
                      int x, int y, int width, int height, int radius);
void ui_landscape(ui_surface_t *surface, int x, int y, int w, int h, int radius);
