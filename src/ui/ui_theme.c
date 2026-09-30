#include "ui_theme.h"
#include "ui_art.h"

static int s_index;
static const ui_theme_t themes[UI_THEME_COUNT] = {
    {"Midnight", 0xFF071322, 0xFF10243A, 0xFF16445B, 0xFF42DDF2, 0xFFF0F6FA, 0xFFA2B8CB, 0xFFFFC46A, 0xFF102B47},
    {"Mono", 0xFF101010, 0xFF1C1C1C, 0xFF363636, 0xFFF1F1EE, 0xFFF1F1EE, 0xFF8B8B89, 0xFFF1F1EE, 0xFF171717},
    {"Blue Wave", 0xFF061B50, 0xFF102E68, 0xFF1C509C, 0xFF9AE9FF, 0xFFF5FAFF, 0xFFBDD0EB, 0xFFFFD07C, 0xFF0E3984},
    {"Daylight", 0xFFF3F0EA, 0xFFE6E3DD, 0xFFD8E5F3, 0xFF326CAE, 0xFF202B39, 0xFF526170, 0xFF92530A, 0xFFFAF8F4},
    {"Cinema", 0xFF101B20, 0xFF1B2D32, 0xFF234950, 0xFF4BDBD7, 0xFFEFF6F6, 0xFFA8BEC3, 0xFFFFC878, 0xFF254650},
};
void ui_theme_set(int index) { s_index = index >= 0 && index < UI_THEME_COUNT ? index : 0; }
int ui_theme_index(void) { return s_index; }
const ui_theme_t *ui_theme_current(void) { return &themes[s_index]; }
const char *ui_theme_name(int index) { return themes[index >= 0 && index < UI_THEME_COUNT ? index : 0].name; }
int ui_theme_move_vertical(int selected,int count,int direction) {
    if(count<=0)return 0;
    if(selected<0||selected>=count)selected=0;
    if(s_index==3) {
        /* Three cards per page: top-left, top-right, bottom-left. The bottom
         * right is the details panel, so vertical movement must skip it. */
        int position=selected%3, candidate=selected;
        if(direction<0) {
            if(position==2)candidate=selected-2;
            else if(selected>=3)candidate=selected-3+(position==0?2:0);
        } else {
            if(position==0)candidate=selected+2;
            else candidate=selected+(position==1?3:1);
        }
        return candidate>=0&&candidate<count?candidate:selected;
    }
    int step=s_index==2?3:1;
    return (selected+count+(direction<0?-step%count:step))%count;
}

static uint32_t blend(uint32_t a, uint32_t b, int amount) {
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        int av = (a >> shift) & 255, bv = (b >> shift) & 255;
        out |= (uint32_t)(av + (bv - av) * amount / 255) << shift;
    }
    return out;
}
void ui_theme_background(ui_surface_t *s) {
    const ui_theme_t *t = ui_theme_current();
    for (int y = 0; y < s->h; y++)
        ui_rect(s, 0, y, s->w, 1, blend(t->bg, t->horizon, y * 255 / s->h));
    if (s_index == 2) {
        /* Layered flowing ribbons, rather than a straight decorative rule. */
        for (int x = 0; x < s->w; x++) {
            int u=x*1000/s->w;
            int y=70+u*3/4-u*u/1000+u*u/1000*u/5000;
            for(int layer=38;layer>=0;layer--)
                ui_rect(s,x,y+layer,1,1,blend(t->bg,0xff3196ef,12+(38-layer)*2));
            ui_rect(s,x,y,1,2,0xff247fea);
            int lower=s->h-150+u/5-u*u/2200;
            for(int layer=20;layer>=0;layer--)
                ui_rect(s,x,lower+layer,1,1,blend(t->horizon,0xff1b6ab8,25-layer));
        }
    }
}
void ui_theme_splash(ui_surface_t *s) {
    const ui_theme_t *t = ui_theme_current();
    ui_theme_background(s);
    if(s_index==0||s_index==4) {
        ui_landscape(s,0,0,s->w,s->h,0);
        ui_overlay(s,0,0,s->w,s->h,t->bg,s_index==0?70:125);
    }
    int cx=s->w/2,cy=350;
    ui_moon(s,cx-25,cy,112,t->text,s_index==1?t->text:t->accent);
    int size=s_index==1?60:54;
    const char *title="M O O N L I G H T";
    ui_label(s,cx-ui_label_w(size,0,title)/2,530,size,0,t->text,title);
    ui_label(s,cx-ui_label_w(32,0,"PS4")/2,615,32,0,s_index==1?t->dim:t->accent,"PS4");
    if(s_index==3)ui_rect(s,cx-25,705,50,4,t->accent);
    ui_label(s,cx-ui_label_w(28,0,"Starting...")/2,850,28,0,t->dim,"Starting...");
}
