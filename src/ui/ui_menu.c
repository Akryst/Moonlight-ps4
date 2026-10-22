#include "ui_menu.h"
#include "ui_draw.h"
#include "ui_theme.h"
#include "ui_art.h"
#include "ui_motion.h"
#include "../log.h"
#include "../stream.h"
#include "../input/input_pad.h"
#include "../video/video.h"
#include "../gamestream/gs_errors.h"
#include "../gamestream/discovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

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
static int s_intro_pending;
static uint64_t ui_now_us(void) {
    struct timeval now;gettimeofday(&now,NULL);
    return (uint64_t)now.tv_sec*1000000u+(uint64_t)now.tv_usec;
}
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

void ui_show_splash(int theme,int reduced_motion) {
    ui_theme_set(theme);
    if (video_ui_begin(UI_W, UI_H) != 0) return;
    uint8_t *frame = malloc((size_t)UI_W * UI_H * 4);
    if (!frame) return;
    ui_surface_t surface = {frame, UI_W * 4, UI_W, UI_H};
    ui_theme_splash(&surface);
    uint8_t *display=malloc((size_t)UI_W*UI_H*4);
    if(display && !reduced_motion) {
        ui_surface_t output={display,UI_W*4,UI_W,UI_H};ui_clear(&output,0xff000000);
        uint64_t start=ui_now_us();unsigned previous=0;
        for(;;) {
            unsigned current=ui_motion_progress(ui_now_us()-start,380000);
            ui_crossfade(&output,&output,&surface,ui_motion_step(previous,current));previous=current;
            if(video_ui_present(display,UI_W*4)==0)ui_hide_splash_once();
            if(current==255)break;
            usleep(16000);
        }
    } else if (video_ui_present(frame, UI_W * 4) == 0) ui_hide_splash_once();
    free(display);
    s_intro_pending=1;
    usleep(180000);
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
    ui_theme_splash(&s);
    ui_label_fit(&s, 180, 754, 26, 0, COL_DIM, title ? title : "MOONLIGHT PS4", 1560);
    if (line1 && line1[0])
        ui_label_fit(&s, 180, 812, 34, 0, COL_TEXT, line1, 1560);
    if (line2 && line2[0])
        ui_label_fit(&s, 180, 882, 25, 0, COL_DIM, line2, 1560);

    for (int i = 0; i < 4; i++) {
        (void)video_ui_present(staging, (int)pitch);
        usleep(16000);
    }
    ui_hide_splash_once();
    free(staging);
    LOGI("ui_show_status: '%s' | '%s' | '%s'",
         title ? title : "", line1 ? line1 : "", line2 ? line2 : "");
}

static char pending_game[CONFIG_MAX_APP];
void ui_menu_set_pending_game(const char *name){snprintf(pending_game,sizeof(pending_game),"%s",name?name:"");}
const char *ui_menu_take_pending_game(void){static char result[CONFIG_MAX_APP];snprintf(result,sizeof(result),"%s",pending_game);pending_game[0]=0;return result;}

static uint32_t launch_mix(uint32_t under, uint32_t over, int alpha) {
    unsigned inv = 255 - (unsigned)alpha;
    return 0xff000000u |
        (((((under >> 16) & 255) * inv + ((over >> 16) & 255) * (unsigned)alpha) / 255) << 16) |
        (((((under >> 8) & 255) * inv + ((over >> 8) & 255) * (unsigned)alpha) / 255) << 8) |
        (((under & 255) * inv + (over & 255) * (unsigned)alpha) / 255);
}

void ui_launch_screen_run(const char *game, const char *verb,
                           launch_progress_t *prog) {
    if (video_ui_begin(UI_W, UI_H) != 0) {
        LOGE("launch screen: video_ui_begin failed");
        return;
    }
    size_t pitch = (size_t)UI_W * 4u;
    size_t bytes = pitch * (size_t)UI_H;
    uint8_t *staging = (uint8_t *)malloc(bytes);
    if (!staging) {
        LOGE("launch screen: staging malloc failed");
        return;
    }

    const char *name = (game && game[0]) ? game : "your game";
    const char *head = (verb && verb[0]) ? verb : "STARTING";
    unsigned long long start = (ui_now_us()/1000u);
    int frame = 0;
    int cancel_requested = 0;

    LOGI("launch screen: '%s' (%s)", name, head);
    while (!prog->done) {
        unsigned pressed = 0, held = 0;
        if (input_menu_poll(&pressed, &held) == 0 && (pressed & MENU_BTN_CIRCLE)) {
            if (!cancel_requested) {
                cancel_requested = 1;
                prog->cancelled = 1;
                LOGI("launch screen: cancel requested by user");
                break;
            }
        }

        ui_surface_t s = { staging, (int)pitch, UI_W, UI_H };
        ui_theme_background(&s);

        ui_text(&s, 80, 250, 3, COL_DIM, head);
        ui_label_fit(&s, 80, 310, 60, 1, COL_TEXT, name, 1760);
        char phase[128];launch_progress_read(prog,phase,sizeof(phase));
        if(phase[0])ui_label_fit(&s,80,450,30,0,COL_ACCENT,phase,1760);

        /* PS4-style orbiting spinner: eight dots at fixed 45-degree spots,
         * the fading trail rotates through them (no libm, no cos/sin). */
        {
            static const int spin_dx[8] = {1024, 724, 0, -724, -1024, -724, 0, 724};
            static const int spin_dy[8] = {0, 724, 1024, 724, 0, -724, -1024, -724};
            int cx = UI_W / 2, cy = 690, radius = 40;
            int slot = (frame / 2) % 8; /* full turn every 16 frames ~0.5 s */
            for (int k = 0; k < 8; k++) {
                int pos = (slot + k) & 7;
                int alpha = 255 - k * 28;
                ui_circle(&s, cx + radius * spin_dx[pos] / 1024,
                          cy + radius * spin_dy[pos] / 1024, 7,
                          launch_mix(COL_BG, COL_ACCENT, alpha));
            }
        }

        unsigned long long elapsed_s = ((ui_now_us()/1000u) - start) / 1000u;
        char line[96];
        if (cancel_requested)
            snprintf(line, sizeof(line), "Cancelling...  %llu s", elapsed_s);
        else
            snprintf(line, sizeof(line), "%llu s", elapsed_s);
        ui_text(&s, 80, UI_H - 130, 2, COL_DIM, line);

        /* Hint bar: CIRCLE cancels. */
        ui_control_image(&s,UI_CIRCLE,UI_W-300,UI_H-118,38);
        ui_label(&s, UI_W - 244, UI_H - 122, 26, 0, cancel_requested ? COL_DIM : COL_TEXT,
                 "Cancel");

        (void)video_ui_present(staging, (int)pitch);
        frame++;
        usleep(33000); /* ~30 fps */
    }

    free(staging);
    input_menu_absorb();
    LOGI("launch screen: done rc=%d after %llu ms", prog->rc,
         (ui_now_us()/1000u) - start);
}

int ui_launch_fail_screen_run(const char *game, const char *reason,
                              int allow_refresh) {
    if (video_ui_begin(UI_W, UI_H) != 0) {
        LOGE("launch fail screen: video_ui_begin failed");
        return UI_LAUNCH_FAIL_BACK;
    }
    size_t pitch = (size_t)UI_W * 4u;
    size_t bytes = pitch * (size_t)UI_H;
    uint8_t *staging = (uint8_t *)malloc(bytes);
    if (!staging) {
        LOGE("launch fail screen: staging malloc failed");
        return UI_LAUNCH_FAIL_BACK;
    }
    const char *name = (game && game[0]) ? game : "your game";
    LOGI("launch fail screen: '%s'%s", name, allow_refresh ? "" : " (no refresh)");

    input_menu_absorb();
    for (;;) {
        unsigned pressed = 0, held = 0;
        if (input_menu_poll(&pressed, &held) == 0) {
            if (pressed & MENU_BTN_CROSS) {
                free(staging);
                input_menu_absorb();
                return UI_LAUNCH_FAIL_RETRY;
            }
            if (allow_refresh && (pressed & MENU_BTN_TRIANGLE)) {
                free(staging);
                input_menu_absorb();
                return UI_LAUNCH_FAIL_REFRESH;
            }
            if (pressed & MENU_BTN_CIRCLE) {
                free(staging);
                input_menu_absorb();
                return UI_LAUNCH_FAIL_BACK;
            }
        }

        ui_surface_t s = { staging, (int)pitch, UI_W, UI_H };
        ui_theme_background(&s);

        ui_text(&s, 80, 250, 3, COL_WARN, "COULD NOT START");
        ui_label_fit(&s, 80, 310, 60, 1, COL_TEXT, name, 1760);
        if (reason && reason[0])
            ui_label_fit(&s, 80, 450, 30, 0, COL_ACCENT, reason, 1760);

        ui_label_fit(&s, 80, 560, 30, 1, COL_TEXT,
                     allow_refresh
                         ? "Could not start the stream. Press X to try again, TRIANGLE to refresh."
                         : "Could not start the stream. Press X to try again.",
                     1760);

        /* Keycap hints, mirroring the launch screen's cancel hint. */
        int hx = 80, hy = UI_H - 122;
        ui_control_image(&s,UI_CROSS,hx,hy,38);
        ui_label(&s, hx + 52, hy + 4, 26, 0, COL_TEXT, "Retry");
        hx += 250;
        ui_control_image(&s,UI_TRIANGLE,hx,hy,38);
        ui_label(&s, hx + 52, hy + 4, 26, 0, allow_refresh ? COL_TEXT : COL_DIM,
                 "Refresh");
        if (allow_refresh) {
            hx += 250;
            ui_control_image(&s,UI_CIRCLE,hx,hy,38);
            ui_label(&s, hx + 52, hy + 4, 26, 0, COL_TEXT, "Back");
        }

        (void)video_ui_present(staging, (int)pitch);
        usleep(33000); /* ~30 fps */
    }
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
    SET_REDUCED,
    SET_DEADZONE,
    SET_SWAP_FACE,
    SET_SURROUND,SET_TOUCHPAD_MOUSE,SET_GYRO,SET_TITLE_MOTION,SET_EXIT,
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
    "Reduced motion",
    "Stick deadzone",
    "Swap Cross / Circle",
    "5.1 surround (HDMI)","Touchpad mouse","Motion input","Motion for selected game","Exit Moonlight",
};

/* Numeric IPv4 keyboard. */
#define OSK_COLS 3
#define OSK_ROWS 4
static const char k_osk_chars[OSK_ROWS][OSK_COLS + 1] = {
    "123", "456", "789", ".0."
};

typedef struct { const char *name;int count;int rows[8]; } settings_group_t;
static const settings_group_t setting_groups[]={
    {"Connection",1,{SET_HOST}},
    {"Video",3,{SET_RES,SET_FPS,SET_BITRATE}},
    {"Audio",2,{SET_LOCAL_AUDIO,SET_SURROUND}},
    {"Controller",5,{SET_DEADZONE,SET_SWAP_FACE,SET_TOUCHPAD_MOUSE,SET_GYRO,SET_TITLE_MOTION}},
    {"Advanced",8,{SET_THEME,SET_REDUCED,SET_PREFER_HW,SET_SHOW_STATS,SET_FILE_LOG,SET_DECODE_MODE,SET_SOPS,SET_PREFER_YCBCR}},
    {"Exit",1,{SET_EXIT}}
};
#define SETTING_GROUPS 6


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
    int refresh_pending,refresh_retries,refresh_retry_wait;
    struct apps_job *job;
    scan_job_t *scan;char scan_phase[48];

    ui_tab_t tab;
    int sel_app;
    int app_first; /* scroll */
    int sel_set;
    int set_group,set_list;
    uint64_t selection_save_at;
    float rail_position,focus_position;
    int motion_ready;
    int close_confirm;
    int error_action;

    int osk_active;
    int osk_target; /* SET_HOST / SET_DEBUG_HOST */
    int osk_row, osk_col;
    char osk_buf[CONFIG_MAX_HOST];

    int host_dirty; /* host changed: reconnect required */
    int result;     /* -1000 = stay in loop */
} ui_state_t;

#define APPS_CACHE_FILE "applist.cache"

typedef struct apps_job {
    pthread_mutex_t mtx;
    int done;      /* results are ready */
    int abandoned; /* menu exited; the thread frees the job */
    int ok;        /* 0 = refresh succeeded */
    int apps_err;  /* 1 applist failed, 2 no connection */
    int napps;
    ui_app_t apps[UI_MAX_APPS];
    int status_ok;   /* serverinfo succeeded */
    int current_game;
    int paired;
    char error[192];
    char host_mac[24]; /* PC MAC from the paired HTTPS serverinfo */
    /* Captured at spawn time (main thread): the worker never touches
     * gs_server_t, which the main thread may re-initialize later. */
    client_identity_t *id;
    char host[CONFIG_MAX_HOST];
    unsigned short http_port;
    unsigned short https_port;
} apps_job_t;

static void apps_cache_path(const ui_state_t *st, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", st->config_dir, APPS_CACHE_FILE);
}

static void apps_sanitize_name(char *name) {
    for (char *p = name; *p; p++)
        if (*p == '\n' || *p == '\r' || *p == '\t')
            *p = ' ';
}

static int apps_load_cache(ui_state_t *st) {
    st->napps = 0;
    if (!st->cfg->host[0])
        return -1;
    char path[512], line[CONFIG_MAX_APP + 32];
    apps_cache_path(st, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    /* Header: "host <host>" — a cache from another host is ignored.
     * fgets() keeps the trailing '\n', so strip it before comparing:
     * an earlier revision compared raw ("192.168.2.99\n") and never matched,
     * which silently disabled the whole disk cache (the list then
     * depended entirely on the network refresh landing). */
    if (!fgets(line, sizeof(line), f) || strncmp(line, "host ", 5) != 0) {
        fclose(f);
        LOGW("ui: applist cache unreadable (bad header)");
        return -1;
    }
    {
        char *nl = strpbrk(line + 5, "\r\n");
        if (nl)
            *nl = '\0';
        if (strcmp(st->cfg->host, line + 5) != 0) {
            fclose(f);
            LOGW("ui: applist cache is for host '%s' (cfg '%s')",
                 line + 5, st->cfg->host);
            return -1;
        }
    }
    while (st->napps < UI_MAX_APPS && fgets(line, sizeof(line), f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl)
            *nl = '\0';
        char *tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = '\0';
        int id = atoi(line);
        if (id <= 0)
            continue;
        snprintf(st->apps[st->napps].name, sizeof(st->apps[st->napps].name),
                 "%s", tab + 1);
        st->apps[st->napps].id = id;
        st->napps++;
    }
    fclose(f);
    LOGI("ui: applist cache %d apps", st->napps);
    return st->napps ? 0 : -1;
}

static void apps_save_cache(const ui_state_t *st) {
    if (!st->napps || !st->cfg->host[0])
        return;
    char path[512], tmp[540];
    apps_cache_path(st, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    int good=fprintf(f, "host %s\n", st->cfg->host)>=0;
    for (int i = 0; i < st->napps; i++) {
        char name[CONFIG_MAX_APP];
        snprintf(name, sizeof(name), "%s", st->apps[i].name);
        apps_sanitize_name(name);
        if(fprintf(f, "%d\t%s\n", st->apps[i].id, name)<0)good=0;
    }
    if(fclose(f))good=0;
    if (!good || rename(tmp, path) != 0)
        remove(tmp);
}

static void apps_job_free(apps_job_t *j) {
    pthread_mutex_destroy(&j->mtx);
    free(j);
}

/* Defined with the settings helpers below; used by the refresh poller so
 * the menu-side WOL MAC adoption can save immediately. */
static void save_cfg(ui_state_t *st);
static void preselect_app(ui_state_t *st);

static void *apps_job_thread(void *arg) {
    apps_job_t *j = arg;
    app_entry_t *list = NULL;

    /* Best-effort status refresh, mirroring the old fetch_apps(). */
    bool paired = false;
    int current_game = 0;
    j->status_ok = gs_serverinfo_status(j->id, j->host, j->https_port,
                                        j->http_port, &paired,
                                        &current_game,
                                        j->host_mac, sizeof(j->host_mac)) == GS_OK;
    j->paired = paired;
    j->current_game = current_game;

    int rc = gs_applist_ex(j->id, j->host, j->https_port, &list);
    int n = 0;
    if (rc == GS_OK) {
        for (app_entry_t *a = list; a && n < UI_MAX_APPS; a = a->next) {
            snprintf(j->apps[n].name, sizeof(j->apps[n].name), "%s",
                     a->name ? a->name : "?");
            char *trademark = strstr(j->apps[n].name, "\xE2\x84\xA2");
            if (trademark)
                memmove(trademark, trademark + 3, strlen(trademark + 3) + 1);
            j->apps[n].id = a->id;
            n++;
        }
        xml_applist_free(list);
    }
    snprintf(j->error,sizeof(j->error),"%s",gs_error?gs_error:"network error");
    j->napps = n;
    j->ok = rc == GS_OK;
    j->apps_err = rc == GS_OK ? 0 : 1;

    pthread_mutex_lock(&j->mtx);
    j->done = 1;
    int free_it = j->abandoned;
    pthread_mutex_unlock(&j->mtx);
    if (free_it)
        apps_job_free(j); /* menu left before consuming the results */
    return NULL;
}

/* Consume finished refresh results. Returns 1 if the UI must redraw. */
static int apps_job_poll(ui_state_t *st) {
    apps_job_t *j = st->job;
    if (!j)
        return 0;
    pthread_mutex_lock(&j->mtx);
    if (!j->done) {
        pthread_mutex_unlock(&j->mtx);
        return 0;
    }
    st->refresh_pending = 0;
    if (j->ok) {
        st->napps = j->napps;
        memcpy(st->apps, j->apps, sizeof(ui_app_t) * (size_t)j->napps);
        st->apps_err = 0;
        st->refresh_retry_wait = 0; /* list landed; stop any retry timer */
        preselect_app(st);
        apps_save_cache(st);
        if (!strcmp(st->notice, "Refreshing games..."))
            st->notice[0] = 0;
        LOGI("ui: applist refreshed %d apps currentGame=%d", st->napps,
             j->current_game);
    } else if (st->napps == 0) {
        /* No cached content to fall back on. */
        st->apps_err = 1;
        snprintf(st->notice, sizeof(st->notice),
                 "Could not refresh games: %s", j->error);
        /* Auto-retry: a transient blip at boot (companion probe holding
         * the HTTP mutex, host briefly busy) should not leave the grid
         * empty until the user discovers TRIANGLE. Two retries, ~1.5 s
         * apart at the menu cadence. */
        if (st->refresh_retries < 2) {
            st->refresh_retries++;
            st->refresh_retry_wait = 200;
            LOGI("ui: games refresh failed; auto-retry %d scheduled",
                 st->refresh_retries);
        }
    } else {
        /* Keep the cached list; surface the failure without blocking. */
        snprintf(st->notice, sizeof(st->notice),
                 "Could not refresh games: %s", j->error);
        LOGW("ui: applist refresh failed: %s", j->error);
    }
    if (j->status_ok && st->server) {
        st->server->currentGame = j->current_game;
        st->server->paired = j->paired != 0;
        /* keep the live server's MAC current and adopt it for
         * Wake-on-LAN right in the menu (syncing only at connect
         * time left a user who paired but had not streamed yet - or
         * whose value sat un-flagged - without the "(auto)" tag). */
        if (j->host_mac[0]) {
            snprintf(st->server->host_mac, sizeof(st->server->host_mac),
                     "%s", j->host_mac);
            if (strcmp(j->host_mac, st->cfg->wol_mac) != 0 ||
                !st->cfg->wol_auto) {
                snprintf(st->cfg->wol_mac, sizeof(st->cfg->wol_mac),
                         "%s", j->host_mac);
                st->cfg->wol_auto = true;
                save_cfg(st);
                LOGI("ui: Wake-on-LAN MAC auto-detected: %s (saved)",
                     st->cfg->wol_mac);
            }
        }
    }
    pthread_mutex_unlock(&j->mtx);
    apps_job_free(j);
    st->job = NULL;
    return 1;
}

/* Launch the background refresh (no-op if one is already running). */
static void fetch_apps_async(ui_state_t *st) {
    if (st->job)
        return;
    if (!st->server) {
        st->apps_err = 2;
        return;
    }
    apps_job_t *j = calloc(1, sizeof(*j));
    if (!j)
        return; /* keep whatever the cache gave us */
    pthread_mutex_init(&j->mtx, NULL);
    j->id = st->server->id;
    snprintf(j->host, sizeof(j->host), "%s", st->server->address);
    j->http_port = st->server->httpPort;
    j->https_port = st->server->httpsPort;
    st->job = j;
    st->refresh_pending = 1;
    pthread_t t;
    if (pthread_create(&t, NULL, apps_job_thread, j) == 0) {
        pthread_detach(t);
    } else {
        st->job=NULL;st->refresh_pending=0;apps_job_free(j);
        snprintf(st->notice,sizeof(st->notice),"Could not create the refresh worker. Press Triangle to retry.");
    }
}

static void fetch_apps(ui_state_t *st){fetch_apps_async(st);}

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
    const char *preferred=st->cfg->last_app[0]?st->cfg->last_app:st->cfg->app_name;
    for (int i = 0; i < st->napps; i++) {
        if (!strcasecmp(st->apps[i].name, preferred) ||
            atoi(preferred) == st->apps[i].id) {
            st->sel_app = i;
            break;
        }
    }
}

/* --- setting values as text --- */

static void set_value_str(const ui_state_t *st, int row, char *out, size_t cap) {
    const app_config_t *c = st->cfg;
    switch (row) {
    case SET_SURROUND:snprintf(out,cap,"%s",c->surround51?"5.1":"Stereo");break;
    case SET_TOUCHPAD_MOUSE:snprintf(out,cap,"%s",c->touchpad_mouse?"On":"Off");break;
    case SET_GYRO:snprintf(out,cap,"%s",c->gyro_motion?"On":"Off");break;
    case SET_TITLE_MOTION:snprintf(out,cap,"%s",st->napps?(config_title_motion_enabled(c,st->apps[st->sel_app].id)?"On":"Off"):"Select a game first");break;
    case SET_EXIT:snprintf(out,cap,"Return to PS4 home");break;
    case SET_REDUCED: snprintf(out,cap,"%s",c->reduced_motion?"On":"Off");break;
    case SET_DEADZONE: snprintf(out,cap,"%d%%",c->controller_deadzone);break;
    case SET_SWAP_FACE: snprintf(out,cap,"%s",c->swap_face_buttons?"On":"Off");break;
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
    if (config_save(st->cfg, st->config_dir) != 0) {
        LOGW("ui: config_save failed");
        snprintf(st->notice,sizeof(st->notice),"Could not save settings. Check free space on the console.");
    }
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
            st->cfg->last_app[0]=0;
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
        ui_menu_set_pending_game(st->apps[st->sel_app].name);
        snprintf(st->cfg->app_name, sizeof(st->cfg->app_name), "%d",
                 st->apps[st->sel_app].id);
        snprintf(st->cfg->last_app,sizeof(st->cfg->last_app),"%d",st->apps[st->sel_app].id);
    }
    save_cfg(st);
    LOGI("ui: launch app='%s' (sel=%d host_dirty=%d)",
         st->cfg->app_name, st->sel_app, st->host_dirty);
    st->result = st->host_dirty || !st->server ? UI_MENU_RECONNECT_LAUNCH : UI_MENU_LAUNCH;
}

static void apps_input(ui_state_t *st, unsigned pr) {
    if(st->apps_err || !st->napps) {
        if(pr&(MENU_BTN_LEFT|MENU_BTN_RIGHT|MENU_BTN_UP|MENU_BTN_DOWN))st->error_action=!st->error_action;
        if(pr&MENU_BTN_CIRCLE)st->tab=TAB_PCS;
        if((pr&MENU_BTN_TRIANGLE) || ((pr&MENU_BTN_CROSS) && !st->error_action)) {
            ui_art_stop();save_cfg(st);st->result=UI_MENU_RECONNECT;
        } else if(pr&MENU_BTN_CROSS)st->tab=TAB_PCS;
        return;
    }
    if(st->close_confirm) {
        if(pr&MENU_BTN_CIRCLE)st->close_confirm=0;
        if(pr&MENU_BTN_CROSS) {
            ui_art_stop();quit_active_stream(st);st->close_confirm=0;
            ui_art_start(st->server,st->config_dir);
        }
        return;
    }
    if((pr&MENU_BTN_SQUARE) && st->server && st->server->currentGame) {
        for(int i=0;i<st->napps;i++)if(st->apps[i].id==st->server->currentGame) {
            st->sel_app=i;
            snprintf(st->cfg->last_app,sizeof(st->cfg->last_app),"%d",st->apps[i].id);
            st->selection_save_at=ui_now_us()+1000000;
            return;
        }
    }
    int previous=st->sel_app;
    if(pr&(MENU_BTN_UP|MENU_BTN_LEFT))st->sel_app=(st->sel_app+st->napps-1)%st->napps;
    else if(pr&(MENU_BTN_DOWN|MENU_BTN_RIGHT))st->sel_app=(st->sel_app+1)%st->napps;
    if(st->sel_app!=previous) {
        snprintf(st->cfg->last_app,sizeof(st->cfg->last_app),"%d",st->apps[st->sel_app].id);
        st->selection_save_at=ui_now_us()+1000000;
    }
    if(pr&MENU_BTN_TRIANGLE) {
        ui_art_stop();
        if(st->host_dirty || !st->server){save_cfg(st);st->result=UI_MENU_RECONNECT;}
        else {fetch_apps(st);preselect_app(st);ui_art_start(st->server,st->config_dir);}
    }
    if(pr&MENU_BTN_CIRCLE)st->tab=TAB_PCS;
    if((pr&MENU_BTN_OPTIONS) && st->server && st->server->currentGame)st->close_confirm=1;
    if(pr&MENU_BTN_CROSS) {
        ui_art_stop();
        if(st->server && st->server->currentGame && st->server->currentGame!=st->apps[st->sel_app].id) {
            /* Switching apps also closes the active host app; make it explicit first. */
            st->close_confirm=1;ui_art_start(st->server,st->config_dir);
        } else launch_selected(st);
    }
}

static void settings_input(ui_state_t *st, unsigned pr) {
    app_config_t *c = st->cfg;
    const settings_group_t *group=&setting_groups[st->set_group];
    if(!st->set_list) {
        if(pr&MENU_BTN_UP)st->set_group=(st->set_group+SETTING_GROUPS-1)%SETTING_GROUPS;
        if(pr&MENU_BTN_DOWN)st->set_group=(st->set_group+1)%SETTING_GROUPS;
        st->sel_set=0;
        if(pr&(MENU_BTN_CROSS|MENU_BTN_RIGHT))st->set_list=1;
        if(pr&MENU_BTN_CIRCLE)st->tab=TAB_APPS;
        return;
    }
    if(pr&MENU_BTN_UP)st->sel_set=(st->sel_set+group->count-1)%group->count;
    if(pr&MENU_BTN_DOWN)st->sel_set=(st->sel_set+1)%group->count;
    if(pr&MENU_BTN_CIRCLE){st->set_list=0;return;}

    int dir = 0;
    if (pr & MENU_BTN_LEFT) dir = -1;
    if (pr & MENU_BTN_RIGHT) dir = 1;
    int activate = (pr & MENU_BTN_CROSS) ? 1 : 0;
    if (!dir && !activate)
        return;

    int changed = 1;
    int setting = group->rows[st->sel_set];
    switch (setting) {
    case SET_SURROUND:c->surround51=!c->surround51;break;
    case SET_TOUCHPAD_MOUSE:c->touchpad_mouse=!c->touchpad_mouse;break;
    case SET_GYRO:c->gyro_motion=!c->gyro_motion;break;
    case SET_TITLE_MOTION:
        if(st->napps && config_toggle_title_motion(c,st->apps[st->sel_app].id))
            snprintf(st->notice,sizeof(st->notice),"Motion override list is full.");
        break;
    case SET_EXIT:if(activate)st->result=UI_MENU_EXIT;changed=0;break;
    case SET_REDUCED: c->reduced_motion=!c->reduced_motion;break;
    case SET_DEADZONE:
        c->controller_deadzone+=dir?dir:1;
        if(c->controller_deadzone<0)c->controller_deadzone=0;
        if(c->controller_deadzone>25)c->controller_deadzone=25;
        break;
    case SET_SWAP_FACE: c->swap_face_buttons=!c->swap_face_buttons;break;
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
        ui_art_stop(); /* The artwork worker logs too; do not close its file while it writes. */
        c->enable_file_log = !c->enable_file_log;
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/debug.log", st->config_dir);
            log_set_file_enabled(c->enable_file_log ? 1 : 0, path);
        }
        ui_art_start(st->server,st->config_dir);
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
    ui_label(s,96,190,52,0,COL_TEXT,"Settings");
    for(int i=0;i<SETTING_GROUPS;i++) {
        int y=300+i*102,selected=i==st->set_group;
        if(selected)ui_rect(s,96,y-8,3,66,COL_TEXT);
        ui_label(s,124,y+4,30,0,selected?COL_TEXT:COL_DIM,setting_groups[i].name);
    }
    ui_rect(s,444,282,1,630,COL_PANEL);
    const settings_group_t *group=&setting_groups[st->set_group];
    for(int i=0;i<group->count;i++) {
        int y=300+i*72,sel=i==st->sel_set && st->set_list;
        if(sel)ui_overlay(s,490,y-8,740,64,COL_TEXT,20);
        ui_label(s,514,y+6,26,0,sel?COL_TEXT:COL_DIM,k_set_names[group->rows[i]]);
        char value[160];set_value_str(st,group->rows[i],value,sizeof(value));
        ui_label_fit(s,950,y+6,26,0,sel?COL_TEXT:COL_DIM,value,260);
        ui_rect(s,514,y+62,690,1,COL_PANEL);
    }
    int setting=group->rows[st->sel_set];
    const char *help[SET_COUNT][2]={
        [SET_HOST]={"Enter your gaming PC's IP address.","Use Computers to scan the network."},
        [SET_RES]={"1080p shows more detail.","720p uses less bandwidth."},
        [SET_FPS]={"60 FPS makes movement fluid.","30 FPS reduces host and network load."},
        [SET_BITRATE]={"Higher bitrate keeps more detail.","Lower it if your connection struggles."},
        [SET_LOCAL_AUDIO]={"Also play stream audio on the PC.","Console audio stays enabled."},
        [SET_DEADZONE]={"Ignore small stick movements near centre.","Raise this if the camera drifts."},
        [SET_SWAP_FACE]={"Swap Cross and Circle on the host.","The console menu keeps its controls."},
        [SET_THEME]={"Choose a colour palette.","Your library keeps the same layout."},
        [SET_REDUCED]={"Disable menu motion and crossfades.","Focus changes immediately."},
        [SET_PREFER_HW]={"Prefer the console hardware decoder.","Software remains the fallback."},
        [SET_SHOW_STATS]={"Show performance statistics in game.","Disable for a cleaner picture."},
        [SET_FILE_LOG]={"Save diagnostics on the console.","Leave off during normal play."},
        [SET_DECODE_MODE]={"Balanced allows decoding overlap.","Low latency queues less work."},
        [SET_SOPS]={"Ask the host to optimise game settings.","Support depends on the streaming host."},
        [SET_PREFER_YCBCR]={"Experimental video output.","Requires the YCbCr GoldHEN plugin."}
    };
    ui_label_fit(s,1290,300,34,0,COL_TEXT,k_set_names[setting],530);
    if(help[setting][0])ui_label_fit(s,1290,382,24,0,COL_DIM,help[setting][0],530);
    if(help[setting][1])ui_label_fit(s,1290,430,24,0,COL_DIM,help[setting][1],530);
    if(st->set_group==3) {
        ui_label(s,1290,572,24,0,COL_DIM,"L1 + R1 + Options: host Guide");
        ui_label(s,1290,620,24,0,COL_DIM,"Options + touchpad: return to menu");
    }
    ui_label(s,1290,810,23,0,COL_DIM,"Changes save automatically.");
    if(st->host_dirty)ui_label_fit(s,1290,862,23,0,COL_WARN,"Refresh Library to reconnect.",530);
}

static void pcs_seed_saved(ui_state_t *st) {
    if (!st->cfg->host[0] || !mdns_valid_ipv4(st->cfg->host))
        return;
    if (st->nhosts >= MDNS_MAX_HOSTS)
        return;
    for (int i = 0; i < st->nhosts; i++)
        if (!strcmp(st->hosts[i].address, st->cfg->host))
            return; /* already listed */
    mdns_host_t *h = &st->hosts[st->nhosts++];
    memset(h, 0, sizeof(*h));
    snprintf(h->instance, sizeof(h->instance), "Gaming PC");
    snprintf(h->address, sizeof(h->address), "%s", st->cfg->host);
    h->port = st->cfg->http_port;
    h->online = -1; /* unknown until the probe lands */
}

static void scan_start(ui_state_t *st) {
    if (st->scan)
        return;
    st->scan = scan_job_begin(st->cfg->host[0] ? st->cfg->host : NULL);
    if (st->scan) {
        LOGI("ui: network scan started (hint='%s')",
             st->cfg->host[0] ? st->cfg->host : "-");
        snprintf(st->notice, sizeof(st->notice), "Checking for your PC...");
    }
}

/* Poll the scan worker: merge the probe's results into the seeded list
 * (update in place by address; append unknown addresses if any).
 * Returns 1 when the UI must redraw. */
static int scan_poll(ui_state_t *st) {
    if (!st->scan)
        return 0;
    mdns_host_t found[MDNS_MAX_HOSTS];
    found[0].address[0] = 0; /* scan_job_poll only writes n entries */
    int n = scan_job_poll(st->scan, found, MDNS_MAX_HOSTS,
                          st->scan_phase, sizeof(st->scan_phase));
    int redraw = 0;
    for (int i = 0; i < n; i++) {
        int hit = -1;
        for (int k = 0; k < st->nhosts; k++)
            if (!strcmp(st->hosts[k].address, found[i].address)) {
                hit = k;
                break;
            }
        if (hit < 0 && st->nhosts < MDNS_MAX_HOSTS) {
            hit = st->nhosts++;
            memset(&st->hosts[hit], 0, sizeof(st->hosts[hit]));
            snprintf(st->hosts[hit].address,
                     sizeof(st->hosts[hit].address), "%s", found[i].address);
            st->hosts[hit].port = found[i].port;
            redraw = 1;
        }
        if (hit >= 0 && found[i].instance[0] &&
            strcmp(st->hosts[hit].instance, found[i].instance) != 0) {
            snprintf(st->hosts[hit].instance,
                     sizeof(st->hosts[hit].instance), "%s", found[i].instance);
            redraw = 1;
        }
        if (hit >= 0 && st->hosts[hit].online != found[i].online) {
            st->hosts[hit].online = found[i].online;
            redraw = 1;
        }
    }
    if (scan_job_done(st->scan)) {
        scan_job_finish(&st->scan);
        LOGI("ui: network scan finished (%d hosts)", st->nhosts);
        redraw = 1;
    }
    return redraw;
}

static void pcs_input(ui_state_t *st, unsigned pr) {
    if (pr & MENU_BTN_TRIANGLE) scan_start(st);
    if (st->nhosts && (pr & MENU_BTN_UP))
        st->sel_host = (st->sel_host + st->nhosts - 1) % st->nhosts;
    if (st->nhosts && (pr & MENU_BTN_DOWN))
        st->sel_host = (st->sel_host + 1) % st->nhosts;
    if (pr & MENU_BTN_CROSS) {
        if (st->nhosts) {
            mdns_host_t *h = &st->hosts[st->sel_host];
            if(strcmp(st->cfg->host,h->address))st->cfg->last_app[0]=0;
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
    ui_label(s, x+24, 310, 26, 0, COL_DIM, "Choose a computer to connect and pair.");
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
    if(st->close_confirm) {
        ui_overlay(s,0,0,UI_W,UI_H,COL_BG,215);
        ui_round_rect(s,420,330,1080,370,18,COL_PANEL);
        ui_label(s,478,380,42,0,COL_TEXT,"Close the current session?");
        ui_label(s,480,460,28,0,COL_DIM,"The host application will be closed. Save your game first.");
        ui_control_image(s,UI_CROSS,480,590,44);ui_label(s,540,596,28,0,COL_TEXT,"Close session");
        ui_control_image(s,UI_CIRCLE,1000,590,44);ui_label(s,1060,596,28,0,COL_TEXT,"Keep playing");
    }
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
    st.tab = TAB_APPS;
    snprintf(st.notice, sizeof(st.notice), "%s", s_notice);
    s_notice[0] = 0;
    /* No host / no server: open Settings to configure. */
    if (!cfg->host[0] || !server) {
        st.tab = cfg->host[0] && st.notice[0]?TAB_APPS:TAB_PCS;
        st.sel_set = SET_HOST;
    }

    if (video_ui_begin(UI_W, UI_H) != 0) {
        LOGE("ui: video_ui_begin failed");
        return -1;
    }
    LOGI("ui: menu open (server=%s)", server ? "OK" : "NULL");

    apps_load_cache(&st);preselect_app(&st);pcs_seed_saved(&st);
    /* Offscreen staging: drawing NEVER touches the on-screen buffer. */
    size_t stage_pitch = (size_t)UI_W * 4u;
    size_t stage_bytes = stage_pitch * (size_t)UI_H;
    uint8_t *staging = (uint8_t *)malloc(stage_bytes);
    if (!staging) {
        LOGE("ui: staging malloc failed");
        video_ui_end();
        return -1;
    }
    uint8_t *display=malloc(stage_bytes);
    ui_surface_t target={staging,(int)stage_pitch,UI_W,UI_H};
    ui_surface_t output={display,(int)stage_pitch,UI_W,UI_H};
    uint64_t transition_start=0;
    unsigned transition_previous=255;
    int animating=0;
    if(!display)LOGW("ui: transition buffer unavailable; displaying without animation");

    /* First frame NOW: don't hide splash until we have an image (avoids black). */
    {
        ui_surface_t s = { staging, (int)stage_pitch, UI_W, UI_H };
        if(s_intro_pending)ui_theme_splash(&s);
        else draw_all(&st, &s);
        if(display)memcpy(display,staging,stage_bytes);
        for (int i = 0; i < 3; i++) {
            if (video_ui_present(staging, (int)stage_pitch) == 0)
                break;
            usleep(8000);
        }
        ui_hide_splash_once();
    }
    s_intro_pending=0;

    fetch_apps_async(&st);
    ui_art_start(st.server,st.config_dir);
    if (!server) scan_start(&st);

    /*
     * After OPTIONS+TOUCHPAD from stream, OPTIONS is still held. If s_menu_prev=0,
     * the OPTIONS/X edge would relaunch instantly. Wait for release + absorb.
     */
    input_menu_wait_release(
        MENU_BTN_OPTIONS | MENU_BTN_CROSS | MENU_BTN_CIRCLE | MENU_BTN_TRIANGLE,
        2000);
    input_menu_absorb();

    int dirty = 1;
    unsigned artwork_revision=ui_art_revision();

    int idle_frames = 0;
    uint64_t motion_last=ui_now_us();
    int grace = 45; /* ~0.7 s without accepting launch */

    while (st.result == -1000) {
        unsigned pr = 0, held = 0;
        if(apps_job_poll(&st))dirty=1;
        if(stream_take_catalog_update()){if(st.job)st.refresh_retry_wait=1;else fetch_apps_async(&st);}
        if(st.refresh_retry_wait==1 && !st.job){st.refresh_retry_wait=0;fetch_apps_async(&st);}
        if(scan_poll(&st))dirty=1;
        if(st.refresh_retry_wait>0 && !st.job && !st.napps && --st.refresh_retry_wait==0)fetch_apps_async(&st);
        unsigned revision=ui_art_revision();
        if(revision!=artwork_revision){artwork_revision=revision;dirty=1;}
        if(st.selection_save_at && ui_now_us()>=st.selection_save_at) {
            save_cfg(&st);st.selection_save_at=0;
        }
        if (input_menu_poll(&pr, &held) != 0) {
            if(strcmp(st.notice,"Connect a DualShock 4 to continue.")) {
                snprintf(st.notice, sizeof(st.notice), "Connect a DualShock 4 to continue.");
                dirty = 1;
            }
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

        uint64_t motion_now=ui_now_us();
        int motion=design_animate(&st,motion_now-motion_last);motion_last=motion_now;
        int redraw=dirty || motion;
        if(redraw) {
            draw_all(&st,&target);
            if(dirty && display && !st.cfg->reduced_motion) {
                transition_start=ui_now_us();transition_previous=0;animating=1;
            } else if(!animating || st.cfg->reduced_motion) {animating=0;if(display)memcpy(display,staging,stage_bytes);}
            dirty=0;
        }
        int transition_frame=animating;
        if(animating) {
            unsigned progress=ui_motion_progress(ui_now_us()-transition_start,240000);
            ui_crossfade(&output,&output,&target,ui_motion_step(transition_previous,progress));
            transition_previous=progress;
            if(progress==255)animating=0;
        }
        if (redraw || transition_frame || idle_frames>=3) {
            idle_frames = 0;
            if (video_ui_present(display?display:staging, (int)stage_pitch) != 0)
                idle_frames=3; /* Retry the displayed frame without restarting its transition. */
            else {
                ui_hide_splash_once();
            }
        }
        usleep(8000);
    }

    if(st.scan)scan_job_finish(&st.scan);
    if(st.job){
        apps_job_t *j=st.job;pthread_mutex_lock(&j->mtx);
        int done=j->done;if(!done)j->abandoned=1;
        pthread_mutex_unlock(&j->mtx);if(done)apps_job_free(j);
    }
    LOGI("ui: freeing staging...");
    ui_art_stop();
    if(st.selection_save_at)save_cfg(&st);
    free(staging);
    free(display);
    ui_art_clear();
    LOGI("ui: closing present...");
    video_ui_end();
    LOGI("ui: menu closed => %d (app='%s' host='%s')",
         st.result, cfg->app_name, cfg->host);
    return st.result;
}
