#include "ui_art.h"
#include "ui_theme.h"
#include "../gamestream/gs_errors.h"
#include <stdlib.h>
#include <limits.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../vendor/stb_image.h"
#pragma GCC diagnostic pop
#include "ui_lake_data.h"
#include <string.h>
static unsigned char *landscape;
static int landscape_w, landscape_h;
void ui_landscape(ui_surface_t *surface,int x,int y,int w,int h,int radius) {
    if(!landscape) {
        int channels;
        landscape=stbi_load_from_memory(lake_png,sizeof(lake_png),&landscape_w,&landscape_h,&channels,4);
    }
    if(landscape)ui_image_round(surface,x,y,w,h,landscape,landscape_w,landscape_h,radius);
    else ui_round_rect(surface,x,y,w,h,radius,ui_theme_current()->panel);
}

/* Menu-scoped, bounded cache. No cover requests/decoding during streaming. */
#define ART_SLOTS 6
static struct { int id, w, h, hw, hh; unsigned stamp; unsigned char *rgba, *hero; } slots[ART_SLOTS];
static unsigned s_stamp;
static unsigned read_be32(const unsigned char *p) {
    return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3];
}
/* Optional embedded panorama in standard PNG; truncated/oversized chunks fail closed. */
static unsigned char *decode_hero(const unsigned char *png,size_t size,int *width,int *height) {
    if(size<8 || memcmp(png,"\x89PNG\r\n\x1a\n",8))return NULL;
    size_t pos=8;
    while(size-pos>=12) {
        unsigned len=read_be32(png+pos);
        if(len>size-pos-12)return NULL;
        if(!memcmp(png+pos+4,"mlBg",4)) {
            int w,h,c;
            if(len>INT_MAX||!stbi_info_from_memory(png+pos+8,(int)len,&w,&h,&c))return NULL;
            if(w<=0||h<=0||w>1920||h>1080||(size_t)w*h>2200000)return NULL;
            return stbi_load_from_memory(png+pos+8,(int)len,width,height,&c,4);
        }
        if(!memcmp(png+pos+4,"IEND",4))break;
        pos+=(size_t)len+12;
    }
    return NULL;
}
void ui_art_clear(void) {
    stbi_image_free(landscape);landscape=NULL;
    for (int i = 0; i < ART_SLOTS; i++) {
        stbi_image_free(slots[i].rgba);
        stbi_image_free(slots[i].hero);slots[i].hero=NULL;
        slots[i].rgba = NULL;
        slots[i].id = slots[i].w = slots[i].h = 0;
        slots[i].stamp = 0;
    }
    s_stamp = 0;
}
void ui_art_draw(gs_server_t *server, int app_id, ui_surface_t *surface,
                 int x, int y, int width, int height) {
    ui_art_draw_round(server,app_id,surface,x,y,width,height,0);
}
void ui_art_draw_round(gs_server_t *server, int app_id, ui_surface_t *surface,
                      int x, int y, int width, int height, int radius) {
    int slot = -1;
    for (int i = 0; i < ART_SLOTS; i++) if (slots[i].stamp && slots[i].id == app_id) slot = i;
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < ART_SLOTS; i++) if (slots[i].stamp < slots[slot].stamp) slot = i;
        stbi_image_free(slots[slot].rgba);
        stbi_image_free(slots[slot].hero);slots[slot].hero=NULL;
        slots[slot].rgba = NULL;
        slots[slot].id = app_id;
        slots[slot].w = slots[slot].h = 0;
        unsigned char *encoded = NULL;
        size_t length = 0;
        if (server && gs_appasset(server, app_id, &encoded, &length) == GS_OK && length <= INT_MAX) {
            int w, h, channels;
            if (stbi_info_from_memory(encoded, (int)length, &w, &h, &channels) &&
                w > 0 && h > 0 && w <= 1200 && h <= 1800 && (size_t)w * h <= 2200000) {
                slots[slot].rgba = stbi_load_from_memory(encoded, (int)length, &slots[slot].w, &slots[slot].h, &channels, 4);
                slots[slot].hero=decode_hero(encoded,length,&slots[slot].hw,&slots[slot].hh);
            }
        }
        free(encoded);
    }
    slots[slot].stamp = ++s_stamp;
    if(slots[slot].hero && width*10>height*11)
        ui_image_round(surface,x,y,width,height,slots[slot].hero,slots[slot].hw,slots[slot].hh,radius);
    else if (slots[slot].rgba)
        ui_image_round(surface, x, y, width, height, slots[slot].rgba, slots[slot].w, slots[slot].h,radius);
    else {
        const ui_theme_t *t = ui_theme_current();
        ui_landscape(surface,x,y,width,height,radius);
        ui_icon(surface,x+width/2-40,y+height/2-35,80,0,t->text);
    }
}
