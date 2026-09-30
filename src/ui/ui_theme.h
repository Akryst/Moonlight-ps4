#pragma once
#include "ui_draw.h"

#define UI_THEME_COUNT 5
typedef struct {
    const char *name;
    uint32_t bg, panel, selected, accent, text, dim, warn, horizon;
} ui_theme_t;
void ui_theme_set(int index);
int ui_theme_index(void);
const ui_theme_t *ui_theme_current(void);
const char *ui_theme_name(int index);
int ui_theme_move_vertical(int selected, int count, int direction);
void ui_theme_background(ui_surface_t *s);
void ui_theme_splash(ui_surface_t *s);
