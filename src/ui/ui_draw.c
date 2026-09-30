#include "ui_draw.h"
#include "ui_font.h"

#include <string.h>
#include "ui_font_sans_data.h"

static uint32_t mix_pixel(uint32_t under, uint32_t over, unsigned a) {
    unsigned inv = 255 - a;
    return 0xff000000u |
        (((((under >> 16) & 255) * inv + ((over >> 16) & 255) * a) / 255) << 16) |
        (((((under >> 8) & 255) * inv + ((over >> 8) & 255) * a) / 255) << 8) |
        (((under & 255) * inv + (over & 255) * a) / 255);
}
static void pixel(ui_surface_t *s, int x, int y, uint32_t color, unsigned alpha) {
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h) return;
    uint32_t *p = (uint32_t *)(void *)(s->fb + (size_t)y * s->pitch) + x;
    *p = mix_pixel(*p, color, alpha);
}
static unsigned next_char(const char **text) {
    unsigned c = (unsigned char)*(*text)++;
    if (c >= 128) { while (((unsigned char)**text & 0xc0) == 0x80) (*text)++; c = '?'; }
    return c >= 32 && c <= 126 ? c : '?';
}
int ui_label_w(int size, int bold, const char *text) {
    int width = 0;
    if (!text || size < 1) return 0;
    while (*text) {
        unsigned c = next_char(&text);
        width += (sans_glyphs[(bold ? 95 : 0) + c - 32].advance * size + 24) / 48;
    }
    return width;
}
void ui_label(ui_surface_t *s, int x, int y, int size, int bold, uint32_t color, const char *text) {
    if (!text || size < 1 || size > 128) return;
    while (*text) {
        unsigned index = (bold ? 95 : 0) + next_char(&text) - 32;
        int gw = sans_glyphs[index].width;
        const unsigned char *mask = sans_pixels + sans_glyphs[index].offset;
        int width = (gw * size + 47) / 48, height = (64 * size + 47) / 48;
        for (int row = 0; row < height; row++) for (int col = 0; col < width; col++) {
            int fx = col * 48 * 256 / size, fy = row * 48 * 256 / size;
            int sx = fx / 256, sy = fy / 256, dx = fx % 256, dy = fy % 256;
            if (sx >= gw || sy >= 64) continue;
            int sx1 = sx + 1 < gw ? sx + 1 : sx, sy1 = sy + 1 < 64 ? sy + 1 : sy;
            unsigned a = ((mask[sy * gw + sx] * (256-dx) + mask[sy * gw + sx1] * dx) * (256-dy) +
                          (mask[sy1 * gw + sx] * (256-dx) + mask[sy1 * gw + sx1] * dx) * dy) / 65536;
            if (a) pixel(s, x + col, y + row, color, a);
        }
        x += (sans_glyphs[index].advance * size + 24) / 48;
    }
}
void ui_label_fit(ui_surface_t *s, int x, int y, int size, int bold, uint32_t color, const char *text, int width) {
    if (!text || width <= 0) return;
    if (ui_label_w(size, bold, text) <= width) { ui_label(s,x,y,size,bold,color,text); return; }
    char shortened[256]; size_t n = strlen(text);
    if (n > sizeof(shortened)-4) n = sizeof(shortened)-4;
    memcpy(shortened,text,n); shortened[n] = 0;
    while (n && ui_label_w(size,bold,shortened) + ui_label_w(size,bold,"...") > width) {
        do { n--; } while (n && ((unsigned char)shortened[n] & 0xc0) == 0x80);
        shortened[n] = 0;
    }
    memcpy(shortened+n,"...",4); ui_label(s,x,y,size,bold,color,shortened);
}
void ui_overlay(ui_surface_t *s, int x, int y, int w, int h, uint32_t color, int opacity) {
    if (opacity <= 0) return;
    if (opacity > 255) opacity = 255;
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w<s->w?x+w:s->w, y1=y+h<s->h?y+h:s->h;
    for(int row=y0;row<y1;row++) for(int col=x0;col<x1;col++) pixel(s,col,row,color,(unsigned)opacity);
}
void ui_round_rect(ui_surface_t *s, int x, int y, int w, int h, int radius, uint32_t color) {
    if(w<=0||h<=0) return;
    if(radius>w/2)radius=w/2; if(radius>h/2)radius=h/2;
    if(radius<1) { ui_rect(s,x,y,w,h,color); return; }
    for(int row=0;row<h;row++) {
        int inset=0, dy=row<radius?radius-row-1:row>=h-radius?row-(h-radius):0;
        if(dy) { int extent=radius; while(extent*extent+dy*dy>radius*radius)extent--; inset=radius-extent; }
        ui_rect(s,x+inset,y+row,w-2*inset,1,color);
    }
}
void ui_focus(ui_surface_t *s, int x, int y, int w, int h, int radius, uint32_t color, int glow) {
    /* Paint outside the card only, preserving the image and the background. */
    for(int row=-glow-4;row<h+glow+4;row++) for(int col=-glow-4;col<w+glow+4;col++) {
        int cx=col<radius?radius:col>=w-radius?w-radius-1:col;
        int cy=row<radius?radius:row>=h-radius?h-radius-1:row;
        int dx=col-cx,dy=row-cy,d2=dx*dx+dy*dy;
        if(d2 < radius*radius)continue;
        int distance=radius; while(distance*distance<d2 && distance<radius+glow+5)distance++;
        int out=distance-radius;
        if(out<=3)pixel(s,x+col,y+row,color,255);
        else if(glow && out<glow+4)pixel(s,x+col,y+row,color,(unsigned)((glow+4-out)*65/glow));
    }
}
static void line(ui_surface_t *s,int x0,int y0,int x1,int y1,int thickness,uint32_t color) {
    int dx=x1-x0,dy=y1-y0, steps=(dx<0?-dx:dx)>(dy<0?-dy:dy)?(dx<0?-dx:dx):(dy<0?-dy:dy);
    if(!steps)steps=1;
    for(int i=0;i<=steps;i++)ui_circle(s,x0+dx*i/steps,y0+dy*i/steps,thickness/2,color);
}
void ui_icon(ui_surface_t *s,int x,int y,int size,int type,uint32_t color) {
    int unit=size/12;if(unit<1)unit=1;
    if(type==0) { /* monitor */
        ui_rect(s,x,y,size,unit,color);ui_rect(s,x,y+size*2/3-unit,size,unit,color);
        ui_rect(s,x,y,unit,size*2/3,color);ui_rect(s,x+size-unit,y,unit,size*2/3,color);
        ui_rect(s,x+size/2-unit,y+size*2/3,unit*2,unit*2,color);
        ui_round_rect(s,x+size/4,y+size*5/6,size/2,unit,unit/2,color);
    } else if(type==1) {
        ui_round_rect(s,x,y+size/4,size,size/2,size/5,color);
        ui_circle(s,x+size/6,y+size*2/3,size/7,color);ui_circle(s,x+size*5/6,y+size*2/3,size/7,color);
        uint32_t dark=0xff102332;
        ui_rect(s,x+size/5,y+size/3,unit,size/4,dark);ui_rect(s,x+size/8,y+size*5/12,size/4,unit,dark);
        ui_circle(s,x+size*3/4,y+size*5/12,unit,dark);ui_circle(s,x+size*5/6,y+size/2,unit,dark);
    } else if(type==2) {
        int outer=size/3,inner=size/6;
        for(int yy=-outer;yy<=outer;yy++)for(int xx=-outer;xx<=outer;xx++) {
            int d=xx*xx+yy*yy;if(d<=outer*outer&&d>=inner*inner)pixel(s,x+size/2+xx,y+size/2+yy,color,255);
        }
        ui_rect(s,x+size/2-unit,y+unit,unit*2,unit*2,color);ui_rect(s,x+size/2-unit,y+size-unit*3,unit*2,unit*2,color);
        ui_rect(s,x+unit,y+size/2-unit,unit*2,unit*2,color);ui_rect(s,x+size-unit*3,y+size/2-unit,unit*2,unit*2,color);
        ui_rect(s,x+unit*2,y+unit*2,unit*2,unit*2,color);ui_rect(s,x+size-unit*4,y+unit*2,unit*2,unit*2,color);
        ui_rect(s,x+unit*2,y+size-unit*4,unit*2,unit*2,color);ui_rect(s,x+size-unit*4,y+size-unit*4,unit*2,unit*2,color);
    } else if(type==3) {
        for(int row=0;row<size;row++){int dy=row-size/2;if(dy<0)dy=-dy;ui_rect(s,x,y+row,size-dy*2,1,color);}
    } else if(type==4) {
        for(int r=0;r<3;r++)for(int c=0;c<3;c++)ui_round_rect(s,x+c*size/3,y+r*size/3,size/5,size/5,1,color);
    } else if(type==6 || type==7 || type==8) {
        int radius=size/2;
        for(int yy=-radius;yy<=radius;yy++)for(int xx=-radius;xx<=radius;xx++) {
            int d=xx*xx+yy*yy;if(d<=radius*radius&&d>=(radius-unit)*(radius-unit))pixel(s,x+radius+xx,y+radius+yy,color,255);
        }
        if(type==6) {int r=size/5;for(int yy=-r;yy<=r;yy++)for(int xx=-r;xx<=r;xx++){int d=xx*xx+yy*yy;if(d<=r*r&&d>=(r-unit)*(r-unit))pixel(s,x+radius+xx,y+radius+yy,color,255);}}
        if(type==7) {line(s,x+size/3,y+size/3,x+size*2/3,y+size*2/3,unit,color);line(s,x+size*2/3,y+size/3,x+size/3,y+size*2/3,unit,color);}
        if(type==8) {line(s,x+size/2,y+size/4,x+size/4,y+size*3/4,unit,color);line(s,x+size/4,y+size*3/4,x+size*3/4,y+size*3/4,unit,color);line(s,x+size*3/4,y+size*3/4,x+size/2,y+size/4,unit,color);}
    }
}
void ui_moon(ui_surface_t *s,int cx,int cy,int radius,uint32_t color,uint32_t waves) {
    int inner=radius*9/10, ox=radius*2/5, oy=-radius/5;
    for(int y=-radius;y<=radius;y++)for(int x=-radius;x<=radius;x++) {
        int d=x*x+y*y, di=(x-ox)*(x-ox)+(y-oy)*(y-oy);
        if(d<=radius*radius&&di>=inner*inner)pixel(s,cx+x,cy+y,color,255);
    }
    for(int j=0;j<3;j++) {
        int px=cx-radius/2, py=cy+radius*3/4+j*12;
        for(int t=1;t<=100;t++) {
            int x=cx-radius/2+radius*2*t/100;
            int u=t-50;
            int y=cy+radius*3/4+j*12-radius*t/120 + u*u*u*radius/600000;
            line(s,px,py,x,y,4-j,waves);px=x;py=y;
        }
    }
}

void ui_circle(ui_surface_t *s, int cx, int cy, int radius, uint32_t argb) {
    if (radius <= 0 || radius > 2048) return;
    for (int dy = -radius; dy <= radius; dy++) {
        int extent = 0;
        while (extent < radius && (extent + 1) * (extent + 1) + dy * dy <= radius * radius)
            extent++;
        ui_rect(s, cx - extent, cy + dy, extent * 2 + 1, 1, argb);
    }
}

void ui_image(ui_surface_t *s, int x, int y, int w, int h,
              const uint8_t *rgba, int iw, int ih) {
    ui_image_round(s,x,y,w,h,rgba,iw,ih,0);
}
void ui_image_round(ui_surface_t *s, int x, int y, int w, int h,
                    const uint8_t *rgba, int iw, int ih, int radius) {
    if (!rgba || w <= 0 || h <= 0 || iw <= 0 || ih <= 0) return;
    int source_w = iw, source_h = ih;
    if ((long long)iw * h > (long long)ih * w)
        source_w = (int)((long long)ih * w / h);
    else source_h = (int)((long long)iw * h / w);
    if (source_w < 1) source_w = 1;
    if (source_h < 1) source_h = 1;
    int source_x = (iw - source_w) / 2, source_y = (ih - source_h) / 2;
    for (int row = 0; row < h; row++) {
        if (y + row < 0 || y + row >= s->h) continue;
        uint32_t *dst = (uint32_t *)(void *)(s->fb + (size_t)(y + row) * s->pitch);
        for (int col = 0; col < w; col++) {
            if (x + col < 0 || x + col >= s->w) continue;
            if (radius > 0) {
                int dx=col<radius?radius-col-1:col>=w-radius?col-(w-radius):0;
                int dy=row<radius?radius-row-1:row>=h-radius?row-(h-radius):0;
                if(dx*dx+dy*dy>radius*radius)continue;
            }
            const uint8_t *p = rgba + ((size_t)(source_y + row * source_h / h) * iw + source_x + col * source_w / w) * 4;
            dst[x + col] = 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
}

void ui_clear(ui_surface_t *s, uint32_t argb) {
    for (int y = 0; y < s->h; y++) {
        uint32_t *row = (uint32_t *)(void *)(s->fb + (size_t)y * (size_t)s->pitch);
        for (int x = 0; x < s->w; x++)
            row[x] = argb;
    }
}

void ui_rect(ui_surface_t *s, int x, int y, int w, int h, uint32_t argb) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0)
        return;
    for (int r = 0; r < h; r++) {
        uint32_t *row = (uint32_t *)(void *)(s->fb + (size_t)(y + r) * (size_t)s->pitch);
        for (int c = 0; c < w; c++)
            row[x + c] = argb;
    }
}

static void draw_glyph(ui_surface_t *s, int x, int y, int scale, uint32_t argb,
                       const unsigned char *glyph) {
    for (int gy = 0; gy < UI_GLYPH_H; gy++) {
        unsigned bits = glyph[gy];
        if (!bits)
            continue;
        for (int gx = 0; gx < UI_GLYPH_W; gx++) {
            if (!((bits >> gx) & 1u))
                continue;
            ui_rect(s, x + gx * scale, y + gy * scale, scale, scale, argb);
        }
    }
}

void ui_text(ui_surface_t *s, int x, int y, int scale, uint32_t argb, const char *str) {
    ui_label(s,x,y,scale < 2 ? 18 : scale * 10,scale >= 4,argb,str);
}
void ui_text_bitmap(ui_surface_t *s,int x,int y,int scale,uint32_t argb,const char *str) {
    if(scale<1)scale=1;
    for(const unsigned char *p=(const unsigned char *)str;*p;p++,x+=UI_GLYPH_W*scale) {
        unsigned c=*p;if(c<32||c>126)c='?';
        draw_glyph(s,x,y,scale,argb,ui_font8x8[c-32]);
    }
}

int ui_text_w(int scale, const char *str) {
    return ui_label_w(scale < 2 ? 18 : scale * 10,scale >= 4,str);
}

void ui_text_fit(ui_surface_t *s, int x, int y, int scale, uint32_t argb,
                 const char *str, int max_width) {
    ui_label_fit(s,x,y,scale < 2 ? 18 : scale * 10,scale >= 4,argb,str,max_width);
}
