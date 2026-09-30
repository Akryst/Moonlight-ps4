// Software drawing onto a BGRA framebuffer (uint32 0xAARRGGBB little-endian).
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t *fb;
    int pitch; /* bytes per row */
    int w, h;  /* pixels */
} ui_surface_t;

void ui_clear(ui_surface_t *s, uint32_t argb);
void ui_rect(ui_surface_t *s, int x, int y, int w, int h, uint32_t argb);
/* Menu text using proportional sans-serif; scale maps to pixel size. */
void ui_text(ui_surface_t *s, int x, int y, int scale, uint32_t argb, const char *str);
/* Original lightweight font for per-frame streaming telemetry. */
void ui_text_bitmap(ui_surface_t *s, int x, int y, int scale, uint32_t argb, const char *str);
void ui_text_fit(ui_surface_t *s, int x, int y, int scale, uint32_t argb,
                 const char *str, int max_width);
int ui_text_w(int scale, const char *str);
/* Antialiased proportional sans-serif labels; size is the font pixel size. */
void ui_label(ui_surface_t *s, int x, int y, int size, int bold, uint32_t color, const char *text);
int ui_label_w(int size, int bold, const char *text);
void ui_label_fit(ui_surface_t *s, int x, int y, int size, int bold, uint32_t color, const char *text, int width);
void ui_round_rect(ui_surface_t *s, int x, int y, int w, int h, int radius, uint32_t color);
void ui_overlay(ui_surface_t *s, int x, int y, int w, int h, uint32_t color, int opacity);
void ui_focus(ui_surface_t *s, int x, int y, int w, int h, int radius, uint32_t color, int glow);
void ui_icon(ui_surface_t *s, int x, int y, int size, int type, uint32_t color);
void ui_moon(ui_surface_t *s, int cx, int cy, int radius, uint32_t color, uint32_t waves);
void ui_circle(ui_surface_t *s, int cx, int cy, int radius, uint32_t argb);
void ui_image(ui_surface_t *s, int x, int y, int w, int h,
              const uint8_t *rgba, int iw, int ih);
void ui_image_round(ui_surface_t *s, int x, int y, int w, int h,
                    const uint8_t *rgba, int iw, int ih, int radius);
