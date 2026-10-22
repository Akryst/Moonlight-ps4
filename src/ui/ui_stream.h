#pragma once
#include "ui_draw.h"
#include "quick_menu.h"
#include "../video/video.h"
void ui_stream_menu(ui_surface_t *s,const qm_view_t *menu);
void ui_stream_stats(ui_surface_t *s,const video_live_stats_t *stats);
void ui_stream_hint(ui_surface_t *s,unsigned opacity);
void ui_stream_hold(ui_surface_t *s,int progress);
