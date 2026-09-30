#include "ui_menu.h"
#include "ui_draw.h"
#include "ui_theme.h"
#include "ui_art.h"
#include "../log.h"
#include "../input/input_pad.h"
#include "../video/video.h"
#include "../gamestream/gs_errors.h"
#include "../gamestream/discovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifdef __ORBIS__
#include <orbis/SystemService.h>
#endif

#define UI_W 1920
#define UI_H 1080
#define UI_MAX_APPS 256

/* Colors 0xAARRGGBB (BGRA LE). */
#define COL_BG      (ui_theme_current()->bg)
#define COL_PANEL   (ui_theme_current()->panel)
#define COL_SEL     (ui_theme_current()->selected)
#define COL_ACCENT  (ui_theme_current()->accent)
#define COL_TEXT    (ui_theme_current()->text)
#define COL_DIM     (ui_theme_current()->dim)
#define COL_WARN    (ui_theme_current()->warn)

static int s_splash_hidden;
static char s_notice[160];
void ui_menu_set_notice(const char *message) {
    snprintf(s_notice, sizeof(s_notice), "%s", message ? message : "");
}

static void ui_hide_splash_once(void) {
#ifdef __ORBIS__
    if (s_splash_hidden)
        return;
    (void)sceSystemServiceHideSplashScreen();
    s_splash_hidden = 1;
#endif
}

void ui_show_splash(int theme) {
    ui_theme_set(theme);
    if (video_ui_begin(UI_W, UI_H) != 0) return;
    uint8_t *frame = malloc((size_t)UI_W * UI_H * 4);
    if (!frame) return;
    ui_surface_t surface = {frame, UI_W * 4, UI_W, UI_H};
    ui_theme_splash(&surface);
    if (video_ui_present(frame, UI_W * 4) == 0) ui_hide_splash_once();
    free(frame);
}

void ui_show_status(const char *title, const char *line1, const char *line2) {
    if (video_ui_begin(UI_W, UI_H) != 0) {
        LOGE("ui_show_status: video_ui_begin failed");
        ui_hide_splash_once(); /* better than eternal splash */
        return;
    }

    size_t pitch = (size_t)UI_W * 4u;
    size_t bytes = pitch * (size_t)UI_H;
    uint8_t *staging = (uint8_t *)malloc(bytes);
    if (!staging) {
        ui_hide_splash_once();
        return;
    }

    ui_surface_t s = { staging, (int)pitch, UI_W, UI_H };
    ui_theme_background(&s);
    ui_text(&s, 80, 80, 5, COL_TEXT, title ? title : "MOONLIGHT PS4");
    if (line1 && line1[0])
        ui_text(&s, 80, 220, 4, COL_ACCENT, line1);
    if (line2 && line2[0])
        ui_text(&s, 80, 320, 3, COL_DIM, line2);
    ui_text(&s, 80, UI_H - 60, 2, COL_DIM, "Please wait...");

    for (int i = 0; i < 4; i++) {
        (void)video_ui_present(staging, (int)pitch);
        usleep(16000);
    }
    ui_hide_splash_once();
    free(staging);
    LOGI("ui_show_status: '%s' | '%s' | '%s'",
         title ? title : "", line1 ? line1 : "", line2 ? line2 : "");
}

typedef enum {
    TAB_PCS = 0,
    TAB_APPS = 1,
    TAB_SETTINGS = 2,
} ui_tab_t;

enum {
    SET_HOST = 0,
    SET_DEBUG_HOST,
    SET_RES,
    SET_FPS,
    SET_BITRATE,
    SET_SOPS,
    SET_LOCAL_AUDIO,
    SET_PREFER_HW,
    SET_PREFER_YCBCR,
    SET_FILE_LOG,
    SET_SHOW_STATS,
    SET_DECODE_MODE,
    SET_THEME,
    SET_COUNT,
};

static const char *k_set_names[SET_COUNT] = {
    "PC IP address",
    "Debug host",
    "Resolution",
    "FPS",
    "Bitrate (kbps)",
    "SOPS",
    "Local audio",
    "Hardware decoding",
    "YCbCr (experimental)",
    "File logging",
    "Performance overlay",
    "Decode mode",
    "Theme",
};

/* Numeric IPv4 keyboard. */
#define OSK_COLS 3
#define OSK_ROWS 4
static const char k_osk_chars[OSK_ROWS][OSK_COLS + 1] = {
    "123", "456", "789", ".0."
};

/* Only settings needed for personal play are exposed. */
static const int k_visible_settings[] = {
    SET_HOST, SET_THEME, SET_RES, SET_FPS, SET_BITRATE, SET_PREFER_HW, SET_SHOW_STATS, SET_FILE_LOG, SET_DECODE_MODE
};
#define VISIBLE_SETTINGS 9


typedef struct {
    char name[CONFIG_MAX_APP];
    int id;
} ui_app_t;

typedef struct {
    app_config_t *cfg;
    gs_server_t *server;
    const char *config_dir;

    ui_app_t apps[UI_MAX_APPS];
    int napps;
    mdns_host_t hosts[MDNS_MAX_HOSTS];
    int nhosts, sel_host;
    char notice[160];
    int apps_err; /* 0 ok, 1 applist failed, 2 no connection */

    ui_tab_t tab;
    int sel_app;
    int app_first; /* scroll */
    int sel_set;

    int osk_active;
    int osk_target; /* SET_HOST / SET_DEBUG_HOST */
    int osk_row, osk_col;
    char osk_buf[CONFIG_MAX_HOST];

    int host_dirty; /* host changed: reconnect required */
    int result;     /* -1000 = stay in loop */
} ui_state_t;

static void fetch_apps(ui_state_t *st) {
    ui_art_clear();
    st->napps = 0;
    if (!st->server) {
        st->apps_err = 2;
        return;
    }
    (void)gs_refresh_status(st->server);
    app_entry_t *list = NULL;
    if (gs_applist(st->server, &list) != GS_OK) {
        LOGW("ui: gs_applist failed: %s", gs_error ? gs_error : "?");
        st->apps_err = 1;
        return;
    }
    for (app_entry_t *a = list; a && st->napps < UI_MAX_APPS; a = a->next) {
        snprintf(st->apps[st->napps].name, sizeof(st->apps[st->napps].name),
                 "%s", a->name ? a->name : "?");
        char *trademark = strstr(st->apps[st->napps].name, "\xE2\x84\xA2");
        if (trademark) memmove(trademark, trademark + 3, strlen(trademark + 3) + 1);
        st->apps[st->napps].id = a->id;
        st->napps++;
    }
    xml_applist_free(list);
    st->apps_err = 0;
    LOGI("ui: applist %d apps currentGame=%d", st->napps,
         st->server->currentGame);
}

static const char *active_app_name(const ui_state_t *st) {
    if (!st->server || st->server->currentGame == 0)
        return NULL;
    for (int i = 0; i < st->napps; i++) {
        if (st->apps[i].id == st->server->currentGame)
            return st->apps[i].name;
    }
    return NULL;
}

static void quit_active_stream(ui_state_t *st) {
    if (!st->server || st->server->currentGame == 0)
        return;
    int id = st->server->currentGame;
    const char *name = active_app_name(st);
    LOGI("ui: closing active stream id=%d name='%s'", id, name ? name : "?");
    if (gs_quit_app(st->server) != GS_OK) {
        LOGW("ui: gs_quit_app failed: %s", gs_error ? gs_error : "?");
        (void)gs_refresh_status(st->server);
    } else {
        LOGI("ui: stream closed");
        LOGN("Stream closed");
    }
}

static void preselect_app(ui_state_t *st) {
    st->sel_app = 0;
    for (int i = 0; i < st->napps; i++) {
        if (!strcasecmp(st->apps[i].name, st->cfg->app_name) ||
            atoi(st->cfg->app_name) == st->apps[i].id) {
            st->sel_app = i;
            break;
        }
    }
}

/* --- setting values as text --- */

static void set_value_str(const ui_state_t *st, int row, char *out, size_t cap) {
    const app_config_t *c = st->cfg;
    switch (row) {
    case SET_THEME: snprintf(out, cap, "%s", ui_theme_name(c->ui_theme)); break;
    case SET_DECODE_MODE: snprintf(out, cap, "%s", c->dec_pipeline_depth > 1 ? "Balanced" : "Low latency"); break;
    case SET_HOST:        snprintf(out, cap, "%s", c->host); break;
    case SET_DEBUG_HOST:  snprintf(out, cap, "%s", c->debug_host); break;
    case SET_RES:         snprintf(out, cap, "%dx%d", c->stream.width, c->stream.height); break;
    case SET_FPS:         snprintf(out, cap, "%d", c->stream.fps); break;
    case SET_BITRATE:     snprintf(out, cap, "%d", c->stream.bitrate); break;
    case SET_SOPS:        snprintf(out, cap, "%s", c->sops ? "yes" : "no"); break;
    case SET_LOCAL_AUDIO: snprintf(out, cap, "%s", c->local_audio ? "yes" : "no"); break;
    case SET_PREFER_HW:   snprintf(out, cap, "%s", c->prefer_hw ? "yes" : "no"); break;
    case SET_PREFER_YCBCR:snprintf(out, cap, "%s", c->prefer_ycbcr ? "yes" : "no"); break;
    case SET_FILE_LOG:    snprintf(out, cap, "%s", c->enable_file_log ? "yes" : "no"); break;
    case SET_SHOW_STATS:  snprintf(out, cap, "%s", c->show_stats ? "yes" : "no"); break;
    default: out[0] = '\0'; break;
    }
}

static void cycle_res(app_config_t *c, int dir) {
    static const int presets[][2] = { {1280, 720}, {1920, 1080} };
    const int n = 2;
    int cur = 1;
    for (int i = 0; i < n; i++) {
        if (c->stream.width == presets[i][0] && c->stream.height == presets[i][1]) {
            cur = i;
            break;
        }
    }
    cur = (cur + dir + n) % n;
    c->stream.width = presets[cur][0];
    c->stream.height = presets[cur][1];
}

static void save_cfg(ui_state_t *st) {
    if (config_save(st->cfg, st->config_dir) != 0)
        LOGW("ui: config_save failed");
}

/* --- OSK --- */

static void osk_open(ui_state_t *st, int target) {
    st->osk_active = 1;
    st->osk_target = target;
    st->osk_row = 0;
    st->osk_col = 0;
    const char *cur = (target == SET_HOST) ? st->cfg->host : st->cfg->debug_host;
    snprintf(st->osk_buf, sizeof(st->osk_buf), "%s", cur);
}

static void osk_accept(ui_state_t *st) {
    if (!mdns_valid_ipv4(st->osk_buf)) {
        snprintf(st->notice, sizeof(st->notice), "Enter a valid PC IPv4 address (for example 192.168.1.100).");
        return;
    }
    st->notice[0] = 0;
    if (st->osk_target == SET_HOST) {
        if (strcmp(st->cfg->host, st->osk_buf) != 0) {
            snprintf(st->cfg->host, sizeof(st->cfg->host), "%s", st->osk_buf);
            st->cfg->http_port = 47989;
            st->host_dirty = 1;
        }
        save_cfg(st);
        st->osk_active = 0;
        /* After writing host: reconnect when leaving the keyboard. */
        if (st->cfg->host[0] && st->host_dirty)
            st->result = UI_MENU_RECONNECT;
        return;
    }
    snprintf(st->cfg->debug_host, sizeof(st->cfg->debug_host), "%s", st->osk_buf);
    save_cfg(st);
    st->osk_active = 0;
}

static void osk_input(ui_state_t *st, unsigned pr) {
    if (pr & MENU_BTN_UP)    st->osk_row = (st->osk_row + OSK_ROWS - 1) % OSK_ROWS;
    if (pr & MENU_BTN_DOWN)  st->osk_row = (st->osk_row + 1) % OSK_ROWS;
    if (pr & MENU_BTN_LEFT)  st->osk_col = (st->osk_col + OSK_COLS - 1) % OSK_COLS;
    if (pr & MENU_BTN_RIGHT) st->osk_col = (st->osk_col + 1) % OSK_COLS;
    if (pr & MENU_BTN_CROSS) {
        size_t len = strlen(st->osk_buf);
        if (len + 1 < sizeof(st->osk_buf)) {
            st->osk_buf[len] = k_osk_chars[st->osk_row][st->osk_col];
            st->osk_buf[len + 1] = '\0';
        }
    }
    if (pr & MENU_BTN_SQUARE) {
        size_t len = strlen(st->osk_buf);
        if (len)
            st->osk_buf[len - 1] = '\0';
    }
    if (pr & MENU_BTN_CIRCLE)
        st->osk_active = 0;
    if (pr & MENU_BTN_OPTIONS)
        osk_accept(st);
}

/* --- tab input --- */

static void launch_selected(ui_state_t *st) {
    if (st->napps > 0) {
        if (st->server && st->server->currentGame != 0 &&
            st->server->currentGame != st->apps[st->sel_app].id) {
            quit_active_stream(st);
            if (st->server->currentGame != 0) {
                LOGN("Close the active session before starting another game");
                return;
            }
        }
        snprintf(st->cfg->app_name, sizeof(st->cfg->app_name), "%d",
                 st->apps[st->sel_app].id);
    }
    save_cfg(st);
    LOGI("ui: launch app='%s' (sel=%d host_dirty=%d)",
         st->cfg->app_name, st->sel_app, st->host_dirty);
    st->result = st->host_dirty ? UI_MENU_RECONNECT : UI_MENU_LAUNCH;
}

static void apps_input(ui_state_t *st, unsigned pr) {
    if ((pr & MENU_BTN_UP) && st->napps > 0)
        st->sel_app = ui_theme_move_vertical(st->sel_app,st->napps,-1);
    if ((pr & MENU_BTN_DOWN) && st->napps > 0)
        st->sel_app = ui_theme_move_vertical(st->sel_app,st->napps,1);
    if ((pr & MENU_BTN_LEFT) && st->napps > 0)
        st->sel_app = (st->sel_app + st->napps - 1) % st->napps;
    if ((pr & MENU_BTN_RIGHT) && st->napps > 0)
        st->sel_app = (st->sel_app + 1) % st->napps;

    if (pr & MENU_BTN_TRIANGLE) {
        if (st->host_dirty || !st->server) {
            save_cfg(st);
            st->result = UI_MENU_RECONNECT;
        } else {
            fetch_apps(st);
            preselect_app(st);
        }
    }
    /* X starts; O returns to the PC list. */
    if (pr & MENU_BTN_CIRCLE) st->tab = TAB_PCS;
    if (pr & MENU_BTN_OPTIONS) {
        if (st->server && st->server->currentGame != 0)
            quit_active_stream(st);
    }
    if (pr & MENU_BTN_CROSS) {
        if (st->napps > 0)
            launch_selected(st);
        else if (st->apps_err)
            st->result = UI_MENU_RECONNECT;
    }
}

static void settings_input(ui_state_t *st, unsigned pr) {
    app_config_t *c = st->cfg;
    if (pr & MENU_BTN_UP)
        st->sel_set = (st->sel_set + VISIBLE_SETTINGS - 1) % VISIBLE_SETTINGS;
    if (pr & MENU_BTN_DOWN)
        st->sel_set = (st->sel_set + 1) % VISIBLE_SETTINGS;
    if (pr & MENU_BTN_CIRCLE) {
        st->tab = TAB_APPS;
        return;
    }

    int dir = 0;
    if (pr & MENU_BTN_LEFT) dir = -1;
    if (pr & MENU_BTN_RIGHT) dir = 1;
    int activate = (pr & MENU_BTN_CROSS) ? 1 : 0;
    if (!dir && !activate)
        return;

    int changed = 1;
    int setting = k_visible_settings[st->sel_set];
    switch (setting) {
    case SET_THEME:
        c->ui_theme = (c->ui_theme + (dir ? dir : 1) + UI_THEME_COUNT) % UI_THEME_COUNT;
        ui_theme_set(c->ui_theme);
        break;
    case SET_HOST:
    case SET_DEBUG_HOST:
        if (activate)
            osk_open(st, setting);
        changed = 0;
        break;
    case SET_RES:
        cycle_res(c, dir ? dir : 1);
        break;
    case SET_FPS:
        c->stream.fps = (c->stream.fps == 60) ? 30 : 60;
        break;
    case SET_BITRATE: {
        int b = c->stream.bitrate + (dir ? dir : 1) * 5000;
        if (b < 5000) b = 5000;
        if (b > 50000) b = 50000;
        c->stream.bitrate = b;
        break;
    }
    case SET_DECODE_MODE: c->dec_pipeline_depth = c->dec_pipeline_depth > 1 ? 1 : 2; break;
    case SET_SOPS:        c->sops = !c->sops; break;
    case SET_LOCAL_AUDIO: c->local_audio = !c->local_audio; break;
    case SET_PREFER_HW:   c->prefer_hw = !c->prefer_hw; break;
    case SET_PREFER_YCBCR:c->prefer_ycbcr = !c->prefer_ycbcr; break;
    case SET_FILE_LOG:
        c->enable_file_log = !c->enable_file_log;
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/debug.log", st->config_dir);
            log_set_file_enabled(c->enable_file_log ? 1 : 0, path);
        }
        break;
    case SET_SHOW_STATS:  c->show_stats = !c->show_stats; break;
    default: changed = 0; break;
    }
    if (changed)
        save_cfg(st);
}

/* --- drawing --- */

static void draw_osk(ui_state_t *st, ui_surface_t *s) {
    const int px = 480, py = 260, pw = 960, ph = 560;
    ui_rect(s, px - 4, py - 4, pw + 8, ph + 8, COL_ACCENT);
    ui_rect(s, px, py, pw, ph, COL_PANEL);

    const char *title = "PC IP ADDRESS";
    ui_text(s, px + 32, py + 28, 3, COL_ACCENT, title);

    /* Value with cursor */
    char line[CONFIG_MAX_HOST + 2];
    snprintf(line, sizeof(line), "%s_", st->osk_buf);
    ui_rect(s, px + 32, py + 88, pw - 64, 44, COL_BG);
    ui_text(s, px + 40, py + 98, 3, COL_TEXT, line);

    /* Grid */
    const int cell = 80, gx0 = px + pw / 2 - OSK_COLS * cell / 2, gy0 = py + 170;
    for (int r = 0; r < OSK_ROWS; r++) {
        for (int cidx = 0; cidx < OSK_COLS; cidx++) {
            int x = gx0 + cidx * cell;
            int y = gy0 + r * cell;
            int sel = (r == st->osk_row && cidx == st->osk_col);
            ui_rect(s, x, y, cell - 8, cell - 8, sel ? COL_SEL : COL_BG);
            if (sel)
                ui_rect(s, x, y + cell - 12, cell - 8, 4, COL_ACCENT);
            char ch[2] = { k_osk_chars[r][cidx], 0 };
            ui_text(s, x + (cell - 8) / 2 - 12, y + (cell - 8) / 2 - 12, 3,
                    sel ? COL_TEXT : COL_DIM, ch);
        }
    }

    ui_text(s, px + 32, py + ph - 40, 2, COL_DIM,
            "X add   [] backspace   O cancel   OPTIONS accept");
}

#include "ui_design.inc"

static void draw_settings(ui_state_t *st, ui_surface_t *s) {
    const int y0 = 230, row_h = 76;
    int x=design_sidebar()?440:100;
    for (int i = 0; i < VISIBLE_SETTINGS; i++) {
        int y = y0 + i * row_h;
        int sel = (i == st->sel_set);
        if (sel)
            ui_round_rect(s, x, y - 8, UI_W - x - 64, row_h - 6, 8, COL_SEL);
        ui_label(s, x+24, y, 28, sel, sel ? COL_TEXT : COL_DIM, k_set_names[k_visible_settings[i]]);
        char val[160];
        set_value_str(st, k_visible_settings[i], val, sizeof(val));
        ui_label_fit(s, x+640, y, 28, 0, sel ? COL_ACCENT : COL_DIM, val, UI_W-x-720);
    }
    if (st->host_dirty)
        ui_label_fit(s, x+24, 936, 22, 0, COL_WARN,
                "Host changed: open Games and press TRIANGLE to reconnect.",UI_W-x-64);
}

static void scan_hosts(ui_state_t *st) {
    ui_show_status("FIND YOUR PC", "Searching the local network...", "Sunshine must be running on the same LAN.");
    st->nhosts = mdns_discover(st->hosts, MDNS_MAX_HOSTS, 1800);
    st->sel_host = 0;
    if (st->nhosts < 0) {
        st->nhosts = 0;
        snprintf(st->notice, sizeof(st->notice), "Network scan failed. Use Settings to enter your PC IP.");
    } else if (!st->nhosts) {
        snprintf(st->notice, sizeof(st->notice), "No PCs found. Press TRIANGLE to scan again or enter an IP in Settings.");
    } else st->notice[0] = 0;
    input_menu_absorb();
}

static void pcs_input(ui_state_t *st, unsigned pr) {
    if (pr & MENU_BTN_TRIANGLE) scan_hosts(st);
    if (st->nhosts && (pr & MENU_BTN_UP))
        st->sel_host = (st->sel_host + st->nhosts - 1) % st->nhosts;
    if (st->nhosts && (pr & MENU_BTN_DOWN))
        st->sel_host = (st->sel_host + 1) % st->nhosts;
    if (pr & MENU_BTN_CROSS) {
        if (st->nhosts) {
            mdns_host_t *h = &st->hosts[st->sel_host];
            snprintf(st->cfg->host, sizeof(st->cfg->host), "%s", h->address);
            st->cfg->http_port = h->port;
            save_cfg(st);
            st->result = UI_MENU_RECONNECT;
        } else if (st->cfg->host[0]) st->result = UI_MENU_RECONNECT;
        else { st->tab = TAB_SETTINGS; st->sel_set = 0; osk_open(st, SET_HOST); }
    }
}

static void draw_pcs(ui_state_t *st, ui_surface_t *s) {
    int x=design_sidebar()?440:100;
    ui_label(s, x+24, 250, 36, 1, COL_TEXT, "Your computers");
    ui_label(s, x+24, 310, 26, 0, COL_DIM, "Choose a PC to connect and pair with Sunshine.");
    if (!st->nhosts) {
        ui_round_rect(s, x, 380, UI_W-x-64, 200, 14, COL_PANEL);
        ui_label(s, x+36, 415, 32, 1, COL_TEXT, st->cfg->host[0] ? st->cfg->host : "Add your gaming PC");
        ui_label(s, x+36, 475, 26, 0, COL_DIM, st->cfg->host[0] ? "X connect saved PC   TRIANGLE scan network" : "X enter IP   TRIANGLE scan network");
    }
    int first = st->sel_host >= 7 ? st->sel_host - 6 : 0;
    for (int i = first; i < st->nhosts && i < first + 7; i++) {
        int y = 390 + (i-first) * 78;
        char name[64], line[80];
        const char *end = strstr(st->hosts[i].instance, "._nvstream");
        size_t len = end ? (size_t)(end - st->hosts[i].instance) : strlen(st->hosts[i].instance);
        if (len >= sizeof(name)) len = sizeof(name)-1;
        memcpy(name, st->hosts[i].instance, len); name[len] = 0;
        ui_round_rect(s, x, y-12, UI_W-x-64, 70, 10, i == st->sel_host ? COL_SEL : COL_PANEL);
        ui_icon(s,x+24,y+4,36,0,COL_TEXT);
        ui_label_fit(s, x+86, y, 30, 1, COL_TEXT, name, 650);
        snprintf(line, sizeof(line), "%s:%u", st->hosts[i].address, (unsigned)st->hosts[i].port);
        ui_label(s, UI_W-420, y+6, 24, 0, COL_DIM, line);
    }
}

static void draw_all(ui_state_t *st, ui_surface_t *s) {
    design_shell(st,s);
    if(st->tab==TAB_APPS)design_apps(st,s);
    else if(st->tab==TAB_PCS)draw_pcs(st,s);
    else draw_settings(st,s);
    design_footer(st,s);
    if(st->notice[0])ui_label_fit(s,design_sidebar()?440:64,948,20,0,COL_WARN,st->notice,1300);
    if(st->osk_active)draw_osk(st,s);
}
int ui_menu_run(app_config_t *cfg, gs_server_t *server, const char *config_dir) {
    ui_theme_set(cfg->ui_theme);
    ui_state_t st;
    memset(&st, 0, sizeof(st));
    st.cfg = cfg;
    st.server = server;
    st.config_dir = config_dir;
    st.result = -1000;
    snprintf(st.notice, sizeof(st.notice), "%s", s_notice);
    s_notice[0] = 0;
    /* No host / no server: open Settings to configure. */
    if (!cfg->host[0] || !server) {
        st.tab = TAB_PCS;
        st.sel_set = SET_HOST;
    }

    if (video_ui_begin(UI_W, UI_H) != 0) {
        LOGE("ui: video_ui_begin failed");
        return -1;
    }
    LOGI("ui: menu open (server=%s)", server ? "OK" : "NULL");

    /* Offscreen staging: drawing NEVER touches the on-screen buffer. */
    size_t stage_pitch = (size_t)UI_W * 4u;
    size_t stage_bytes = stage_pitch * (size_t)UI_H;
    uint8_t *staging = (uint8_t *)malloc(stage_bytes);
    if (!staging) {
        LOGE("ui: staging malloc failed");
        video_ui_end();
        return -1;
    }

    /* First frame NOW: don't hide splash until we have an image (avoids black). */
    {
        ui_surface_t s = { staging, (int)stage_pitch, UI_W, UI_H };
        draw_all(&st, &s);
        for (int i = 0; i < 3; i++) {
            if (video_ui_present(staging, (int)stage_pitch) == 0)
                break;
            usleep(8000);
        }
        ui_hide_splash_once();
    }

    fetch_apps(&st);
    preselect_app(&st);
    if (!cfg->host[0]) scan_hosts(&st);

    /*
     * After OPTIONS+TOUCHPAD from stream, OPTIONS is still held. If s_menu_prev=0,
     * the OPTIONS/X edge would relaunch instantly. Wait for release + absorb.
     */
    input_menu_wait_release(
        MENU_BTN_OPTIONS | MENU_BTN_CROSS | MENU_BTN_CIRCLE | MENU_BTN_TRIANGLE,
        2000);
    input_menu_absorb();

    int dirty = 1;

    int idle_frames = 0;
    int grace = 45; /* ~0.7 s without accepting launch */

    while (st.result == -1000) {
        unsigned pr = 0, held = 0;
        if (input_menu_poll(&pr, &held) != 0) {
            snprintf(st.notice, sizeof(st.notice), "Connect a DualShock 4 to continue.");
            dirty = 1;
        } else if (!strcmp(st.notice, "Connect a DualShock 4 to continue.")) {
            st.notice[0] = 0; dirty = 1;
        }

        if (grace > 0) {
            grace--;
            /* During grace: navigate OK, but don't launch / change host. */
            pr &= ~(MENU_BTN_CROSS | MENU_BTN_CIRCLE | MENU_BTN_OPTIONS |
                    MENU_BTN_TRIANGLE);
            if (held & (MENU_BTN_OPTIONS | MENU_BTN_CROSS | MENU_BTN_CIRCLE))
                input_menu_absorb();
        }

        if (pr) {
            if (st.osk_active)
                osk_input(&st, pr);
            else if (pr & (MENU_BTN_L1 | MENU_BTN_R1)) {
                st.tab = (ui_tab_t)((st.tab + ((pr & MENU_BTN_R1) ? 1 : 2)) % 3);
            }
            else if (st.tab == TAB_PCS)
                pcs_input(&st, pr);
            else if (st.tab == TAB_APPS)
                apps_input(&st, pr);
            else
                settings_input(&st, pr);
            dirty = 1;
        }

        /* Exit NOW: don't redraw or flip (SubmitFlip VSYNC can hang
         * with high pending / cur=-1 â†’ eternal black screen). */
        if (st.result != -1000) {
            LOGI("ui: leaving menu (result=%d)", st.result);
            break;
        }

        if (dirty || ++idle_frames >= 3) {
            idle_frames = 0;
            ui_surface_t s = { staging, (int)stage_pitch, UI_W, UI_H };
            if (dirty)
                draw_all(&st, &s);
            if (video_ui_present(staging, (int)stage_pitch) != 0)
                dirty = 1;
            else {
                dirty = 0;
                ui_hide_splash_once();
            }
        }
        usleep(8000);
    }

    LOGI("ui: freeing staging...");
    free(staging);
    ui_art_clear();
    LOGI("ui: closing present...");
    video_ui_end();
    LOGI("ui: menu closed => %d (app='%s' host='%s')",
         st.result, cfg->app_name, cfg->host);
    return st.result;
}
