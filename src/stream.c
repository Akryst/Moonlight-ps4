// Orchestration: identity -> connect -> menu UI -> launch -> stream -> menu.

#include "stream.h"
#include "log.h"
#include "config.h"
#include "gamestream/client.h"
#include "gamestream/gs_http.h"
#include "gamestream/gs_errors.h"
#include "gamestream/launch_flow.h"
#include "gamestream/discovery.h"
#include "audio/audio_orbis.h"
#include "video/video.h"
#include "input/input_pad.h"
#include "ui/ui_menu.h"
#include "ui/ui_art.h"
#include "ui/ui_draw.h"
#include "ui/ui_theme.h"
#include "ui/quick_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>

static gs_server_t s_server;
static client_identity_t s_id;
static volatile int s_connected;
static volatile int s_terminated_by_host; /* connectionTerminated fired */
static char s_stream_title[CONFIG_MAX_APP];
static char handshake_error[160];

/* "Quit app on PC" from the quick menu. Set on the way out of the
 * input loop; consumed after LiStopConnection so the app on the host is
 * closed over a fresh HTTPS /cancel (the same call the app list uses on
 * OPTIONS over a running game) and the client lands back in the menu.
 * The Moonlight app itself keeps running. */
static int s_quit_app_after_stream;

/* connection/decoder/audio callbacks at file scope - reconnect
 * attempts (stream_reconnect, after a mid-stream drop) call
 * LiStartConnection again and need the same structs. Filled in
 * stream_play(). */
static CONNECTION_LISTENER_CALLBACKS s_cl;
static DECODER_RENDERER_CALLBACKS s_dr;
static AUDIO_RENDERER_CALLBACKS s_ar;
static int s_prefer_ycbcr_flag;

static unsigned long long stream_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ull + tv.tv_usec / 1000ull;
}

static int s_catalog_requested;
static atomic_int launch_busy,connect_busy;
static atomic_int catalog_done;
typedef struct {char host[CONFIG_MAX_HOST];} catalog_job_t;
static void *catalog_worker(void *arg){
    catalog_job_t *job=arg;http_resp_t response={0};
    int rc=http_get_timeout(job->host,47991,1,"/moonlight-ps4/open",&response,HTTP_FLAG_FRESH,60000);
    http_resp_free(&response);free(job);atomic_store(&catalog_done,rc==GS_OK?1:2);
    LOGI("startup Steam/Playnite sync => %d (optional companion)",rc);return NULL;
}
int stream_take_catalog_update(void){return atomic_exchange(&catalog_done,0)==1;}
static void catalog_start(const char *host){
    if(s_catalog_requested)return;
    catalog_job_t *job=calloc(1,sizeof(*job));if(!job)return;
    snprintf(job->host,sizeof(job->host),"%s",host);pthread_t thread;
    if(pthread_create(&thread,NULL,catalog_worker,job)){free(job);return;}
    pthread_detach(thread);s_catalog_requested=1;
}

static void cl_stage_starting(int stage) {
    const char *name = LiGetStageName(stage);
    LOGI("connection: stage START %s (%d)", name, stage);
}
static void cl_stage_failed(int stage, int errorCode) {
    snprintf(handshake_error,sizeof(handshake_error),"%s failed (error %d)",LiGetStageName(stage),errorCode);
    const char *name = LiGetStageName(stage);
    LOGE("connection: stage FAIL %s (%d) err=%d", name, stage, errorCode);
}
static void cl_connection_started(void) {
    s_connected = 1;
    s_terminated_by_host = 0;
    LOGI("connection: streaming active");
    if (s_stream_title[0])
        LOGN("%s", s_stream_title);
}
static void cl_connection_terminated(int errorCode) {
    LOGE("connection: terminated err=%d", errorCode);
    s_connected = 0;
    s_terminated_by_host = 1;
    input_request_quit();
}
/* host rumble -> DS4 motors (large/slow = lowFreq, small/fast =
 * highFreq). Advertised since the beginning (LI_CCAP_RUMBLE) but never
 * wired, so in-game rumble silently did nothing. */
static void cl_rumble(unsigned short controllerNumber,
                      unsigned short lowFreqMotor, unsigned short highFreqMotor) {
    input_rumble(controllerNumber,lowFreqMotor>>8,highFreqMotor>>8);
}
static void cl_log(const char *format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    // Limelight sometimes omits \n
    size_t n = strlen(buf);
    if (n && buf[n - 1] == '\n')
        log_printf("[Li] %s", buf);
    else
        log_printf("[Li] %s\n", buf);
}
static void cl_status(int status) {
    LOGI("connection: statusUpdate %d", status);
}
/* the host asked this controller to start (rate_hz > 0) or stop
 * (rate_hz == 0) gyro/accelerometer reports — Sunshine does this when a
 * game on the host requests motion input. input_pad samples the DS4 IMU
 * at the requested rate and ships LiSendControllerMotionEvent packets. */
static void cl_motion_state(unsigned short controllerNumber, unsigned char motionType,
                            unsigned short reportRateHz) {
    LOGI("connection: motion request pad=%d type=%d rate=%d Hz",
         (int)controllerNumber, (int)motionType, (int)reportRateHz);
    input_motion_set_state((int)controllerNumber, motionType, reportRateHz);
}
/* the host set the virtual controller's lightbar color (Steam's
 * controller LED option on the Sunshine pad, or a game driving it;
 * control event 0x5502). Mirror it on the physical DualShock 4: the
 * Steam color is primary while set, and the per-slot player colors
 * (input_pad) apply whenever nothing is set - restored on stream end. */
static void cl_set_led(unsigned short controllerNumber, unsigned char r,
                       unsigned char g, unsigned char b) {
    input_set_led((int)controllerNumber, r, g, b);
}

static int resolve_app_id(gs_server_t *server, const char *app_name) {
    LOGI("resolve_app_id('%s')...", app_name);
    int app_id = atoi(app_name);
    if (app_id != 0) {
        LOGI("numeric app_id=%d", app_id);
        return app_id;
    }

    app_entry_t *list = NULL;
    int rc = gs_applist(server, &list);
    if (rc != GS_OK) {
        LOGE("gs_applist failed: %d %s", rc, gs_error ? gs_error : "");
        return 0;
    }
    int count = 0;
    for (app_entry_t *a = list; a; a = a->next) {
        LOGI("  app[%d]=%d '%s'", count, a->id, a->name);
        count++;
        if (!strcasecmp(a->name, app_name)) {
            app_id = a->id;
            LOGI("name match => %d", app_id);
        }
    }
    if (!app_id && list) {
        app_id = list->id;
        LOGW("no match; using first app id=%d '%s'", app_id, list->name);
    }
    xml_applist_free(list);
    LOGI("resolve_app_id => %d (%d apps)", app_id, count);
    return app_id;
}

/* Resolve the human display name for a numeric app id. Auto-start boots
 * straight into stream_play() without a menu click, so nobody ever called
 * ui_menu_set_pending_game() and the launch screens would fall back to
 * the "your game"/"the game" placeholders. One applist request against
 * the host we are already connected to (bounded by gs_http's timeouts)
 * gives the screens the real game name. Returns 0 when found. */
static int resolve_app_display(gs_server_t *server, int app_id,
                               char *out, size_t cap) {
    app_entry_t *list = NULL;
    if (gs_applist(server, &list) != GS_OK || !list) {
        if (list)
            xml_applist_free(list);
        return -1;
    }
    int found = 0;
    for (app_entry_t *a = list; a; a = a->next) {
        if (a->id == app_id && a->name[0]) {
            snprintf(out, cap, "%s", a->name);
            found = 1;
            break;
        }
    }
    xml_applist_free(list);
    LOGI("resolve_app_display(%d) => '%s'", app_id, found ? out : "?");
    return found ? 0 : -1;
}

/* --- serverinfo + pairing with the current host from cfg ----------------
 *
 * --- bounded connect screen ----------------------------------
 *
 * stream_connect() used to run gs_init() inline: two serverinfo requests
 * with a 7 s timeout each. On a healthy host that is well under a second,
 * but on Orbis a blocking connect to a powered-off / firewalled PC blocks
 * for the kernel's full SYN budget (~75 s, SO_SNDTIMEO does not bound
 * connect there — the hardware logs), which froze the "Connecting..."
 * status screen with the pad dead for over a minute. The gs_init phase
 * now runs on a worker thread while an animated screen draws, with
 * CIRCLE-to-cancel and a hard wall-clock budget. If the worker is still
 * stuck when the budget runs out, it is abandoned: it finishes (or times
 * out in the kernel), discards its result and frees itself — the global
 * s_server is only ever written by the worker, and only when the UI is
 * still waiting for it. */
#define CONNECT_BUDGET_MS 12000

/* when a connect fails on a host that has a Wake-on-LAN MAC saved,
 * send a wake burst so the PC can boot while the user reads the error -
 * the classic "PC asleep" case then needs one retry, not a walk to the
 * desk. UDP sendto() only: cannot hit Orbis's TCP connect stall. */
static int connect_failed_wol(app_config_t *cfg, int cancelled) {
    if (cancelled || !cfg->wol_mac[0] || !cfg->host[0])
        return 0;
    int n = wol_send(cfg->wol_mac, cfg->host);
    return n > 0;
}

/* Sunshine reports the streaming PC's MAC address to paired
 * clients in /serverinfo over HTTPS (root.mac; see client.c). When it
 * differs from the saved Wake-on-LAN MAC (or none is saved yet), adopt
 * it: the user never types 12 hex digits, and a NIC change on the PC is
 * picked up automatically. Runs after every successful connect and
 * after pairing. The adoption is PERSISTED as provenance
 * (cfg->wol_auto), so Settings can show "(auto)" even when the current
 * menu session has no serverinfo at hand (fresh boot, HTTP-only
 * refresh, manual MAC entered first). */
static void stream_sync_wol_mac(app_config_t *cfg, const char *config_dir) {
    if (!s_server.host_mac[0] || !cfg->host[0])
        return;
    if (strcmp(s_server.host_mac, cfg->wol_mac) != 0 || !cfg->wol_auto) {
        int changed = strcmp(s_server.host_mac, cfg->wol_mac) != 0;
        snprintf(cfg->wol_mac, sizeof(cfg->wol_mac), "%s", s_server.host_mac);
        cfg->wol_auto = true;
        if (config_dir && config_save(cfg, config_dir) == 0)
            LOGI("stream: Wake-on-LAN MAC %s: %s (saved, auto)",
                 changed ? "auto-detected" : "source updated", cfg->wol_mac);
        else
            LOGW("stream: Wake-on-LAN MAC auto-detected: %s (save FAILED)",
                 cfg->wol_mac);
    }
}

typedef struct {
    pthread_mutex_t mtx;
    atomic_int done;
    int abandoned;
    int rc;
    char errbuf[128];
    /* gs_init() target (captured; never dereferences globals that the
     * main thread may touch while this runs). */
    char host[CONFIG_MAX_HOST];
    unsigned short http_port;
} connect_job_t;

static void *connect_job_thread(void *arg) {
    connect_job_t *j = arg;
    gs_server_t local;
    int rc = gs_init(&local, &s_id, j->host, j->http_port, NULL);
    const char *err = gs_error;
    pthread_mutex_lock(&j->mtx);
    if (!j->abandoned && rc == GS_OK) {
        /* Publish: SERVER_INFORMATION holds pointers INTO the struct
         * (address/appVersion/gfeVersion), so move the struct into the
         * global and rebind the three pointers. Only this thread writes
         * s_server here, and the UI reads it only after done=1. */
        s_server = local;
        s_server.serverInfo.address = s_server.address;
        s_server.serverInfo.serverInfoAppVersion = s_server.appVersion;
        s_server.serverInfo.serverInfoGfeVersion = s_server.gfeVersion;
    } else if (rc == GS_OK) {
        rc = GS_FAILED; /* abandoned: result discarded by agreement */
        snprintf(j->errbuf, sizeof(j->errbuf), "abandoned");
    } else {
        snprintf(j->errbuf, sizeof(j->errbuf), "%s",
                 err ? err : "Could not connect to Sunshine");
    }
    j->rc = rc;
    j->done = 1;
    int free_it = j->abandoned;
    pthread_mutex_unlock(&j->mtx);
    atomic_store(&connect_busy,0);
    if (free_it) {
        pthread_mutex_destroy(&j->mtx);
        free(j);
    }
    return NULL;
}

static uint32_t connect_mix(uint32_t under, uint32_t over, int alpha) {
    unsigned inv = 255 - (unsigned)alpha;
    return 0xff000000u |
        (((((under >> 16) & 255) * inv + ((over >> 16) & 255) * (unsigned)alpha) / 255) << 16) |
        (((((under >> 8) & 255) * inv + ((over >> 8) & 255) * (unsigned)alpha) / 255) << 8) |
        (((under & 255) * inv + (over & 255) * (unsigned)alpha) / 255);
}

/* Animated "Connecting" screen. Returns when the job is done, the user
 * pressed CIRCLE, or the budget ran out. */
static void connect_screen_run(const char *host, connect_job_t *j,
                               int *cancelled) {
    const int W = 1920, H = 1080;
    *cancelled = 0;
    if (video_ui_begin(W, H) != 0) {
        LOGE("connect screen: video_ui_begin failed");
        /* No display: wait for the worker inline (bounded by gs_init). */
        while (!j->done)
            usleep(20000);
        return;
    }
    size_t pitch = (size_t)W * 4u;
    uint8_t *staging = malloc(pitch * (size_t)H);
    if (!staging) {
        video_ui_end();
        while (!j->done)
            usleep(20000);
        return;
    }
    unsigned long long start = stream_now_ms();
    int frame = 0;
    while (!j->done) {
        unsigned pressed = 0, held = 0;
        if (input_menu_poll(&pressed, &held) == 0 &&
            (pressed & MENU_BTN_CIRCLE)) {
            *cancelled = 1;
            LOGI("connect screen: cancel requested by user");
            break;
        }
        if (stream_now_ms() - start >= CONNECT_BUDGET_MS) {
            LOGW("connect screen: budget reached (%d ms); abandoning",
                 CONNECT_BUDGET_MS);
            break;
        }

        ui_surface_t s = { staging, (int)pitch, W, H };
        ui_theme_background(&s);
        ui_text(&s, 80, 250, 3, ui_theme_current()->dim, "MOONLIGHT");
        ui_label_fit(&s, 80, 310, 60, 1, ui_theme_current()->text, host, 1760);
        ui_label_fit(&s, 80, 450, 30, 0, ui_theme_current()->accent,
                     "Connecting to Sunshine...", 1760);
        {
            static const int spin_dx[8] = {1024, 724, 0, -724, -1024, -724, 0, 724};
            static const int spin_dy[8] = {0, 724, 1024, 724, 0, -724, -1024, -724};
            int cx = W / 2, cy = 690, radius = 40;
            int slot = (frame / 2) % 8;
            for (int k = 0; k < 8; k++) {
                int pos = (slot + k) & 7;
                int alpha = 255 - k * 28;
                ui_circle(&s, cx + radius * spin_dx[pos] / 1024,
                          cy + radius * spin_dy[pos] / 1024, 7,
                          connect_mix(ui_theme_current()->bg,
                                      ui_theme_current()->accent, alpha));
            }
        }
        unsigned long long elapsed_s = (stream_now_ms() - start) / 1000u;
        char line[96];
        snprintf(line, sizeof(line), "%llu s", elapsed_s);
        ui_text(&s, 80, H - 130, 2, ui_theme_current()->dim, line);
        ui_icon(&s, W - 300, H - 118, 38, 6, 0xffff727d);
        ui_label(&s, W - 244, H - 122, 26, 0, ui_theme_current()->text,
                 "Cancel");

        (void)video_ui_present(staging, (int)pitch);
        frame++;
        usleep(33000);
    }
    free(staging);
    input_menu_absorb();
    video_ui_end();
}

static int stream_connect(app_config_t *cfg, const char *config_dir) {
    char pin[8] = {0};
    char line[160];

    LOGI("gs_init(%s:%u)...", cfg->host, (unsigned)cfg->http_port);

    if(atomic_exchange(&connect_busy,1)){ui_menu_set_notice("The previous connection request is finishing. Retry shortly.");return -1;}
    connect_job_t *j=calloc(1,sizeof(*j));
    if(!j){atomic_store(&connect_busy,0);ui_menu_set_notice("Not enough memory to connect.");return -1;}
    pthread_t th;
    int threaded = 0;
    memset(j, 0, sizeof(*j));
    pthread_mutex_init(&j->mtx, NULL);
    snprintf(j->host, sizeof(j->host), "%s", cfg->host);
    j->http_port = cfg->http_port;
    if (pthread_create(&th, NULL, connect_job_thread, j) == 0) {
        threaded = 1;
    } else {
        /* No threads (never seen on PS4): run inline as before. */
        LOGW("connect: worker unavailable; inline gs_init");
        connect_job_thread(j);
    }

    if (threaded) {
        int cancelled = 0;
        connect_screen_run(cfg->host, j, &cancelled);
        pthread_mutex_lock(&j->mtx);
        int done = j->done;
        if (!done)
            j->abandoned = 1;
        pthread_mutex_unlock(&j->mtx);
        if (!done) {
            /* Worker still stuck in a (kernel-bounded) connect: let it
             * drain in the background and free itself. */
            pthread_detach(th);
            LOGW("connect: abandoned; worker draining in background");
            int woke = connect_failed_wol(cfg, cancelled);
            ui_menu_set_notice(cancelled
                ? "Connect cancelled."
                : woke
                    ? "No response - a Wake-on-LAN packet was sent. Try again in ~15 s."
                    : "No response from your PC. Is it on and is Sunshine running?");
            ui_show_status("ERROR", "Could not connect",
                           cancelled ? "Cancelled." :
                               woke ? "Wake packet sent - try again in ~15 s." :
                               "No response from your PC. Is it running Sunshine?");
            return -1;
        }
        pthread_join(th, NULL);
    }

    int rc = j->rc;
    if (rc != GS_OK) {
        LOGE("gs_init FAIL: %s", j->errbuf[0] ? j->errbuf : "?");
        int woke = connect_failed_wol(cfg, 0);
        ui_menu_set_notice(woke
            ? "Could not connect - a Wake-on-LAN packet was sent. Try again in ~15 s."
            : "Could not connect. Retry, or change your PC IP in Settings.");
        ui_show_status("ERROR", "Could not connect",
                       j->errbuf[0] ? j->errbuf : "gs_init FAIL");
        pthread_mutex_destroy(&j->mtx);free(j);
        return -1;
    }
    pthread_mutex_destroy(&j->mtx);free(j);
    LOGI("server %s ver=%s paired=%d https=%u codecs=0x%x currentGame=%d",
         cfg->host, s_server.appVersion, s_server.paired, s_server.httpsPort,
         s_server.serverCodecModeSupport, s_server.currentGame);

    /* adopt the PC-reported MAC for Wake-on-LAN (paired HTTPS
     * serverinfo just filled s_server.host_mac). */
    stream_sync_wol_mac(cfg, config_dir);

    if (!s_server.paired) {
        char path[512];
        config_ensure_dir(config_dir);
        snprintf(path, sizeof(path), "%s/pin.txt", config_dir);
        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(pin, sizeof(pin), f))
                pin[strcspn(pin, " \r\n")] = '\0';
            fclose(f);
            LOGI("pin.txt read: '%s'", pin);
        } else {
            LOGI("pin.txt does not exist yet");
        }
        if (strlen(pin) != 4) {
            unsigned char rnd[2];
            rng_fill(&s_id, rnd, sizeof(rnd));
            snprintf(pin, sizeof(pin), "%04u", (rnd[0] << 8 | rnd[1]) % 10000u);
            f = fopen(path, "w");
            if (f) {
                fputs(pin, f);
                fclose(f);
                LOGI("PIN written to %s = %s", path, pin);
            } else {
                LOGE("fwrite pin FAIL path=%s errno=%d", path, errno);
            }
        }

        char pin_line[64];
        snprintf(pin_line, sizeof(pin_line), "PIN: %s", pin);
        snprintf(line, sizeof(line), "Enter the PIN in Sunshine (%s)", cfg->host);
        ui_show_status("PAIR", pin_line, line);
        LOGI(">>> PAIRING PIN=%s  -> https://%s:47990/pin  (timeout 120s)",
             pin, cfg->host);

        /* gs_pair carries the 120 s budget per request now; the
         * host answered serverinfo moments ago, so the PIN phase cannot
         * hit an unbounded connect. */
        int pair_rc = gs_pair(&s_server, pin);
        LOGI("gs_pair => %d (%s)", pair_rc,
             pair_rc == GS_OK ? "ok" : (gs_error ? gs_error : "?"));
        if (pair_rc != GS_OK) {
            ui_menu_set_notice("Pairing failed. Connect again and enter the PIN in Sunshine.");
            ui_show_status("ERROR", "Pairing failed",
                           gs_error ? gs_error : "gs_pair FAIL");
            return -1;
        }
        if (remove(path) != 0)
            LOGW("could not delete %s errno=%d", path, errno);
        else
            LOGI("pin.txt deleted after pairing OK");
        /* now that Sunshine trusts this client its serverinfo
         * carries the PC's real MAC (the pre-pairing view carried the
         * 00:00:00:00:00:00 placeholder). One paired HTTPS refresh adopts
         * it immediately, so Wake-on-LAN is configured from the very
         * first session instead of after a reconnect. */
        if (gs_refresh_status(&s_server) != GS_OK)
            LOGW("connect: post-pairing refresh failed (WOL MAC deferred)");
        stream_sync_wol_mac(cfg, config_dir);
        ui_show_status("MOONLIGHT", "Paired OK", "Opening menu...");
    } else {
        LOGI("already paired; skip pair");
    }
    catalog_start(cfg->host);
    return 0;
}

/* Launch an app with transport-level recovery - the ladder itself now
 * lives in gamestream/launch_flow.c (see launch_flow.h for why: it runs
 * on a worker thread while ui_launch_screen_run() draws the animated
 * "Starting <game>..." screen, so the wait is visible and cancellable
 * instead of the old "last menu frame frozen for up to a minute"
 * that users reported as a hang). */
typedef struct {
    launch_progress_t prog;
    gs_server_t srv;
    STREAM_CONFIGURATION stream;
    int app_id,fn,abandoned;
    bool sops,local_audio;
    char display[128];
} launch_thread_t;
static void server_rebind(gs_server_t *s){
    s->serverInfo.address=s->address;s->serverInfo.serverInfoAppVersion=s->appVersion;
    s->serverInfo.serverInfoGfeVersion=s->gfeVersion;
    s->serverInfo.rtspSessionUrl=s->rtspSessionUrl[0]?s->rtspSessionUrl:NULL;
}
static void launch_job_free(launch_thread_t *a){pthread_mutex_destroy(&a->prog.mutex);free(a);}
static void *launch_thread(void *arg){
    launch_thread_t *a=arg;
    int rc;
    if(a->fn==2)rc=launch_refresh_flow(&a->srv,&a->app_id,a->display,&a->prog);
    else if(a->fn==3)rc=launch_resume_flow(&a->srv,&a->stream,a->app_id,a->sops,a->local_audio,a->display,&a->prog);
    else rc=a->fn==0?launch_app_flow(&a->srv,&a->stream,a->app_id,a->sops,a->local_audio,a->display,&a->prog)
        :launch_recover_flow(&a->srv,&a->stream,a->app_id,a->sops,a->local_audio,a->display,&a->prog);
    pthread_mutex_lock(&a->prog.mutex);
    a->prog.rc=rc;a->prog.done=1;int abandoned=a->abandoned;
    pthread_mutex_unlock(&a->prog.mutex);
    atomic_store(&launch_busy,0);
    if(abandoned)launch_job_free(a);
    return NULL;
}
static int run_launch_threaded(app_config_t *cfg,int app_id,int fn,const char *display,char *reason,size_t cap){
    if(atomic_exchange(&launch_busy,1)){if(reason && cap)snprintf(reason,cap,"The previous request is still finishing. Retry shortly.");return -1;}
    launch_thread_t *a=calloc(1,sizeof(*a));
    if(!a){atomic_store(&launch_busy,0);if(reason && cap)snprintf(reason,cap,"Not enough memory to start a game.");return -1;}
    pthread_mutex_init(&a->prog.mutex,NULL);
    a->srv=s_server;server_rebind(&a->srv);a->stream=cfg->stream;
    a->app_id=app_id;a->sops=cfg->sops;a->local_audio=cfg->local_audio;a->fn=fn;
    snprintf(a->display,sizeof(a->display),"%s",display?display:"");
    pthread_t th;
    if(pthread_create(&th,NULL,launch_thread,a)){
        atomic_store(&launch_busy,0);launch_job_free(a);if(reason && cap)snprintf(reason,cap,"Could not create the launch worker.");return -1;
    }
    ui_launch_screen_run(display,fn==2?"REFRESHING":fn?"RECONNECTING":NULL,&a->prog);
    pthread_mutex_lock(&a->prog.mutex);
    int done=a->prog.done;
    if(!done){a->prog.cancelled=1;a->abandoned=1;}
    pthread_mutex_unlock(&a->prog.mutex);
    if(!done){http_cancel_thread(th);pthread_detach(th);return -2;}
    pthread_join(th,NULL);
    int rc=a->prog.rc;
    if(rc==0){s_server=a->srv;server_rebind(&s_server);cfg->stream=a->stream;
        if(fn==2)snprintf(cfg->app_name,sizeof(cfg->app_name),"%d",a->app_id);
    }
    if(reason && cap)snprintf(reason,cap,"%s",rc?a->prog.phase:"");
    launch_job_free(a);return rc;
}

/* reconnect after a mid-stream drop. The game is still running on
 * the host, so the RESUME ladder (launch_recover_flow) re-attaches to the
 * live session instead of relaunching it. Three automatic attempts, then
 * the user decides (X retry / CIRCLE back to the menu) - the prompt waits
 * indefinitely so a PC that takes a minute to come back is still one X
 * press away. Returns 0 when a fresh LiStartConnection is streaming
 * again, -1 when the user chose the menu. */
static int stream_reconnect(app_config_t *cfg, int app_id,
                            const char *display) {
    char reason[160];
    for (int attempt = 1; ; attempt++) {
        int rrc = run_launch_threaded(cfg, app_id, 3, display,
                                      reason, sizeof(reason));
        if (rrc == -2) {
            LOGI("reconnect: cancelled on the launch screen");
            return -1;
        }
        if (rrc == 0) {
            input_reset();
            video_present_begin_session(cfg->dec_pipeline_depth);
            video_stats_begin_session();
            LOGI("reconnect: LiStartConnection begin (attempt %d)...",
                 attempt);
            int ret = LiStartConnection(&s_server.serverInfo, &cfg->stream,
                                        &s_cl, &s_dr, &s_ar,
                                        &s_prefer_ycbcr_flag,
                                        sizeof(s_prefer_ycbcr_flag), NULL, 0);
            LOGI("reconnect: LiStartConnection => %d", ret);
            if (ret == 0) {
                s_terminated_by_host = 0;
                LOGN("Connection restored");
                return 0;
            }
            snprintf(reason, sizeof(reason), "Reconnect failed. (%s)",
                     handshake_error[0]?handshake_error:"connection error");
        }
        if (attempt >= 3) {
            int choice = ui_launch_fail_screen_run(display, reason, 0);
            if (choice == UI_LAUNCH_FAIL_BACK)
                return -1; /* X retries the loop; CIRCLE ends it */
        }
    }
}

/* One stream session: launch + LiStartConnection + loop until exit.
 * failures stay on the launch screen with an
 * in-place X-to-retry (and TRIANGLE-to-refresh for ladder failures) -
 * instead of dropping back to the menu with a notice to find the game
 * and click it again. */
static int stream_play(app_config_t *cfg, const char *config_dir) {
    ui_controls_preload();
    input_set_preferences(cfg->controller_deadzone,cfg->swap_face_buttons);
    cfg->stream.audioConfiguration=cfg->surround51?AUDIO_CONFIGURATION_51_SURROUND:AUDIO_CONFIGURATION_STEREO;
    s_connected = 0;
    s_quit_app_after_stream = 0; /* defensive: no stale quit-app request */
    snprintf(s_stream_title, sizeof(s_stream_title), "%s", cfg->app_name);

    char display_buf[128]; /* real game name when auto-start set none */
    const char *display = ui_menu_take_pending_game();
    char reason[160];
    int app_id = 0;

restart:
    app_id = resolve_app_id(&s_server, cfg->app_name);
    if (!app_id) {
        LOGE("app not found: '%s'", cfg->app_name);
        ui_menu_set_notice("App not found - press TRIANGLE to refresh the list.");
        return -1;
    }
    /* Auto-start path: no menu click ever set a display name, so ask the
     * host for it once - the launch screen, its phase texts and the fail
     * screens then read like "Asking your PC to start 'Cyberpunk 2077'..."
     * instead of the old "your game"/"the game" placeholders. */
    if (!display[0] &&
        resolve_app_display(&s_server, app_id, display_buf,
                            sizeof(display_buf)) == 0) {
        display = display_buf;
        snprintf(s_stream_title, sizeof(s_stream_title), "%s", display);
    }
    LOGI("launch app_id=%d name='%s' display='%s' %dx%d@%d bitrate=%d "
         "sops=%d localAudio=%d currentGame=%d",
         app_id, cfg->app_name, display && display[0] ? display : "-",
         cfg->stream.width, cfg->stream.height,
         cfg->stream.fps, cfg->stream.bitrate, cfg->sops, cfg->local_audio,
         s_server.currentGame);

    /* the ladder runs on a worker thread while an animated status
     * screen (with CIRCLE-to-cancel) draws here. Previously this whole
     * sequence ran inline with the LAST MENU FRAME frozen on screen -
     * bounded attempts of 15s+14s+15s+10s+15s stacked to over a minute,
     * which every field user read as "the app froze when I clicked a
     * game" (and force-closing mid-ladder left a half-launched host
     * session, making the NEXT click even slower). */
    int lrc = run_launch_threaded(cfg, app_id, 0, display, reason, sizeof(reason));
    while (lrc == -1) {
        int choice = ui_launch_fail_screen_run(display, reason, 1);
        if (choice == UI_LAUNCH_FAIL_BACK) {
            ui_menu_set_notice("Could not start the game.");
            return -1;
        }
        if (choice == UI_LAUNCH_FAIL_REFRESH) {
            int refreshed=run_launch_threaded(cfg,app_id,2,display,reason,sizeof(reason));
            if(refreshed==-2)return -2;
            if(refreshed!=0)continue;
            app_id=atoi(cfg->app_name);config_save(cfg,config_dir);
        }
        lrc = run_launch_threaded(cfg, app_id, 0, display, reason, sizeof(reason));
    }
    if (lrc == -2) {
        ui_menu_set_notice("Launch cancelled.");
        return -2; /* -2: user cancel; the connection itself is fine */
    }
    if (lrc != 0)
        return -1;
    LOGI("gs_start_app OK");

    input_reset();

    LiInitializeConnectionCallbacks(&s_cl);
    s_cl.stageStarting = cl_stage_starting;
    s_cl.stageFailed = cl_stage_failed;
    s_cl.connectionStarted = cl_connection_started;
    s_cl.connectionTerminated = cl_connection_terminated;
    s_cl.rumble = cl_rumble; /* in-game rumble reaches the DS4 motors */
    s_cl.logMessage = cl_log;
    s_cl.connectionStatusUpdate = cl_status;
    s_cl.setMotionEventState = cl_motion_state; /* gyro/accel reports */
    s_cl.setControllerLED = cl_set_led;        /* Steam lightbar -> DS4 */

    s_dr = video_callbacks_ffmpeg;
    s_prefer_ycbcr_flag = cfg->prefer_ycbcr ? 1 : 0;
    const char *decoder_reason = "default";
#if ML_ENABLE_VIDEODEC2
    if (!cfg->prefer_hw) {
        decoder_reason = "prefer_hw=false";
    } else {
        int probe = video_orbis_probe(cfg->stream.width, cfg->stream.height);
        if (probe == 0) {
            video_orbis_set_tuning(cfg->dec_pipeline_depth, cfg->dec_thread_prio,
                                   cfg->dec_au_onion, cfg->dec_fb_garlic);
            s_dr = video_callbacks_orbis;
            if (cfg->slices_per_frame > 0)
                s_dr.capabilities = CAPABILITY_SLICES_PER_FRAME(cfg->slices_per_frame);
            decoder_reason = "probe OK";
        } else {
            LOGW("stream: HW probe failed rc=%d; fallback SW", probe);
            decoder_reason = "probe FAIL";
        }
    }
#else
    decoder_reason = "ML_ENABLE_VIDEODEC2=0";
#endif
    LOGI("stream: decoder=%s (prefer_hw=%s, %s)",
         s_dr.submitDecodeUnit == video_callbacks_ffmpeg.submitDecodeUnit ? "SW" : "HW",
         cfg->prefer_hw ? "true" : "false", decoder_reason);
    s_ar = audio_callbacks_orbis;

    /* stereo by default; 5.1 when opted in (Settings). The audio
     * renderer downmixes to stereo if 8ch output is unavailable, so this
     * can never end in silence. */
    cfg->stream.audioConfiguration = cfg->surround51
        ? AUDIO_CONFIGURATION_51_SURROUND : AUDIO_CONFIGURATION_STEREO;
    /* DS4 touchpad as a mouse (Settings toggle, default on). */
    input_set_touchpad_mouse(cfg->touchpad_mouse);
    /* gyro/accelerometer motion toggle (Settings, default on). */
    input_set_motion_enabled(cfg->gyro_motion && config_title_motion_enabled(cfg,app_id));

    video_set_show_stats(cfg->show_stats);
    if (cfg->show_stats && s_prefer_ycbcr_flag)
        LOGW("stream: Perf overlay requires BGRA; disable YCbCr to see it");

    video_present_begin_session(cfg->dec_pipeline_depth);
    video_stats_begin_session();
    handshake_error[0]=0;
    LOGI("LiStartConnection begin...");
    int ret = LiStartConnection(&s_server.serverInfo, &cfg->stream, &s_cl,
                                &s_dr, &s_ar, &s_prefer_ycbcr_flag,
                                sizeof(s_prefer_ycbcr_flag), NULL, 0);
    LOGI("LiStartConnection => %d", ret);

    /* a handshake failure against a session that IS live host-side
     * (launch or resume succeeded, but RTSP/ENet did not) used to end this
     * click with "Streaming failed" - the infamous "first click fails,
     * second works" pattern: the second click went through the ladder's
     * cancel-and-relaunch branch. Collapse that into the SAME click:
     * cancel the stuck session, relaunch cleanly (both visible on the
     * animated screen), then try the handshake once more. */
    if (ret != 0 && s_server.currentGame == app_id) {
        LOGW("stream: handshake failed on a live session (resume path); "
             "recovering in-click");
        int rrc = run_launch_threaded(cfg, app_id, 1, display, reason,
                                      sizeof(reason));
        if (rrc == -2) {
            ui_menu_set_notice("Launch cancelled.");
            return -2;
        }
        if (rrc == 0) {
            input_reset();
            video_present_begin_session(cfg->dec_pipeline_depth);
            video_stats_begin_session();
            LOGI("LiStartConnection retry begin...");
            ret = LiStartConnection(&s_server.serverInfo, &cfg->stream, &s_cl,
                                    &s_dr, &s_ar, &s_prefer_ycbcr_flag,
                                    sizeof(s_prefer_ycbcr_flag), NULL, 0);
            LOGI("LiStartConnection retry => %d", ret);
        }
    }

    if (ret != 0) {
        /* Handshake failure: stay on the screen and offer X-to-retry (no
         * refresh prompt - the app list is not the suspect here). */
        gs_quit_app(&s_server);
        snprintf(reason, sizeof(reason), "Streaming failed. (%s)",
                 handshake_error[0]?handshake_error:"connection error");
        int choice = ui_launch_fail_screen_run(display, reason, 0);
        if (choice == UI_LAUNCH_FAIL_RETRY)
            goto restart;
        return -1;
    }

    s_terminated_by_host = 0;
    for (;;) {
        LOGI("main loop; quick menu: hold L3+R3 2.5 s (toggles, bitrate, quit)");
        LOGI("input: L1+R1+OPTIONS sends host Guide (PS) button");
        video_show_hint(6); /* transient on-screen reminder of the combos */

        struct timeval perf_start;
        gettimeofday(&perf_start, NULL);
        int ticks = 0;
        int qm_restart = 0;   /* quick menu changed the bitrate */
        int qm_pad_lost = 0;  /* input_menu_poll fails (pad gone): auto-close */
        while (!input_poll()) {
            /* 4 ms: input is sampled and sent to the host at 250 Hz (was 125).
             * Average button-to-host latency drops ~4 ms; negligible CPU cost. */
            usleep(4000);

            /* L3+R3 hold complete -> open the in-stream quick menu.
             * BGRA present mode only: the YCbCr path cannot draw overlays,
             * so there the hold stays a no-op (logged once). */
            if (input_quick_menu_take_open()) {
                if (!video_present_is_bgra()) {
                    LOGW("quick menu: skipped (YCbCr present mode has no "
                         "overlay plane)");
                } else {
                    input_quick_menu_engage();
                    qm_open(cfg->touchpad_mouse, video_get_show_stats(),
                            cfg->stream.bitrate);
                    qm_pad_lost = 0;
                }
            }

            /* Quick menu input (pad 0 via input_menu_poll; passthrough is
             * paused while it is open). Actions apply to the live session:
             * toggles take effect immediately, the bitrate is applied on
             * close through a seamless reconnect (the game keeps running
             * on the host, same path as a mid-stream drop). */
            if (qm_is_open()) {
                unsigned pressed, held;
                if (input_menu_poll(&pressed, &held) != 0) {
                    /* Pad 0 vanished mid-menu (USB unplug, user shuffle):
                     * auto-close instead of a stuck invisible overlay. */
                    if (++qm_pad_lost > 250) { /* ~1 s at the 4 ms cadence */
                        qm_close(NULL, NULL);
                        input_quick_menu_disengage();
                        LOGW("quick menu: pad lost; closed");
                    }
                } else {
                    qm_pad_lost = 0;
                    qm_action_t a = qm_input(pressed);
                    int br = 0, br_changed = 0;
                    switch (a) {
                    case QM_ACT_TOGGLE_TOUCHPAD:
                        cfg->touchpad_mouse = !cfg->touchpad_mouse;
                        input_set_touchpad_mouse(cfg->touchpad_mouse);
                        if (config_dir)
                            config_save(cfg, config_dir);
                        LOGI("quick menu: touchpad mouse %s",
                             cfg->touchpad_mouse ? "ON" : "off");
                        break;
                    case QM_ACT_TOGGLE_OVERLAY:
                        LOGI("quick menu: perf overlay %s",
                             video_toggle_stats() ? "ON" : "off");
                        break;
                    case QM_ACT_CLOSE:
                        qm_close(&br, &br_changed);
                        input_quick_menu_disengage();
                        if (br_changed && br != cfg->stream.bitrate) {
                            cfg->stream.bitrate = br;
                            if (config_dir)
                                config_save(cfg, config_dir);
                            LOGI("quick menu: bitrate -> %d kbps; "
                                 "restarting stream", br);
                            qm_restart = 1;
                        }
                        break;
                    case QM_ACT_QUIT:
                        qm_close(NULL, NULL);
                        input_quick_menu_disengage();
                        LOGI("quick menu: quit stream");
                        input_request_quit();
                        break;
                    case QM_ACT_QUIT_APP:
                        qm_close(NULL, NULL);
                        input_quick_menu_disengage();
                        LOGI("quick menu: quit app on PC");
                        s_quit_app_after_stream = 1;
                        input_request_quit();
                        break;
                    default:
                        break;
                    }
                    if (qm_restart)
                        break;
                }
            }

            if (++ticks % 250 == 0) {
                video_stats_t st;
                video_take_stats(&st);
                struct timeval perf_end;
                gettimeofday(&perf_end, NULL);
                double elapsed = (double)(perf_end.tv_sec - perf_start.tv_sec) +
                                 (double)(perf_end.tv_usec - perf_start.tv_usec) / 1000000.0;
                if (elapsed > 0) LOGI("perf bitrate=%d fps=%.1f decodes/s=%.1f drops=%u interval=%.2fs",
                    cfg->stream.bitrate, st.frames / elapsed, st.decodes / elapsed, st.dropped, elapsed);
                perf_start = perf_end;
                LOGI("stats t=%d connected=%d frames=%u decodes=%u drop=%u",
                     ticks, s_connected, st.frames, st.decodes, st.dropped);
                if (st.frames || st.decodes) {
                    double nd = st.decodes ? (double)st.decodes : 1.0;
                    double nf = st.frames ? (double)st.frames : 1.0;
                    LOGI("  decode=%.1fms convert=%.1fms (bounce=%.1f bgra=%.1f) present=%.1fms",
                         (st.decode_us_total / nd) / 1000.0,
                         st.frames ? (st.convert_us_total / nf) / 1000.0 : 0.0,
                         st.frames ? (st.bounce_us_total / nf) / 1000.0 : 0.0,
                         st.frames ? (st.bgra_us_total / nf) / 1000.0 : 0.0,
                         st.frames ? (st.present_us_total / nf) / 1000.0 : 0.0);
                    LOGI("  au=%.2fms %.0fKB/frame", (st.au_us_total / nd) / 1000.0,
                         (st.au_bytes_total / nd) / 1024.0);
                }
            }
            if (!s_connected && ticks > 500) { /* ~2 s at the 4 ms cadence */
                LOGE("stream: connection lost after %d ticks", ticks);
                break;
            }
        }


        LOGI("leaving loop (quit or disconnect)");
        /* the connection can drop while the quick menu is open
         * (host reboot mid-menu). Close it here so the reconnect ladder
         * and any following session start from a clean slate. */
        if (qm_is_open()) {
            qm_close(NULL, NULL);
            input_quick_menu_disengage();
            LOGW("quick menu: connection dropped; menu closed");
        }
        LiStopConnection();

        /* the host lightbar color (Steam) belongs to the stream;
        * back in the menu the per-slot player colors return. */
        input_leds_restore();

        /* the perf overlay can be flipped live with L3+R3; persist
         * the preference so the next session starts with the same state. */
        if (config_dir) {
            int stats_now = video_get_show_stats();
            if (!!stats_now != !!cfg->show_stats) {
                cfg->show_stats = stats_now ? true : false;
                if (config_save(cfg, config_dir) == 0)
                    LOGI("stream: perf overlay preference saved (%d)",
                         stats_now);
            }
        }

        /* "Quit app on PC" - after the session is down, tell the
         * host to close the streamed app (fresh-connection /cancel, so it
         * cannot race the streaming sockets or their threads - the same
         * call the app list makes on OPTIONS over a running game). Then
         * back to the client menu: the Moonlight app itself keeps running.
         * Failure is non-fatal: the user still lands in the menu, and the
         * app-list OPTIONS quit remains available there. */
        if (s_quit_app_after_stream) {
            s_quit_app_after_stream = 0;
            LOGI("quick menu: closing the app on the PC");
            if (gs_quit_app(&s_server) == GS_OK) {
                LOGN("App closed on the PC");
                ui_menu_set_notice("App closed on your PC.");
            } else {
                LOGW("quick menu: gs_quit_app failed: %s",
                     gs_error ? gs_error : "?");
                ui_menu_set_notice(
                    "Stream ended. The app is still running on your PC.");
            }
            return 0;
        }

        /* quick-menu bitrate change - relaunch the session with the
         * new value through the reconnect ladder (animated screens, three
         * attempts, X-to-retry). The game keeps running on the host, so
         * this is a ~2-4 s blip, not a relaunch. */
        if (qm_restart) {
            if (stream_reconnect(cfg, app_id, display) != 0) {
                ui_menu_set_notice("Connection lost.");
                return -1;
            }
            /* stream_reconnect() re-started the session; loop back to input. */
            continue;
        }

        if (!s_terminated_by_host) {
            /* No gs_quit_app: leave the game on Sunshine for resume -> "paused". */
            LOGN("Stream paused");
            LOGI("stream_play done OK -> return to menu");
            return 0;
        }

        /* a mid-stream drop (network blip, Sunshine restart, host
         * reboot) used to dump the user into the menu - and getting back
         * into a game that was STILL RUNNING host-side took a full
         * connect + relaunch cycle. Now: three automatic resume attempts
         * on the animated "RECONNECTING" screen, then an X-to-retry /
         * CIRCLE-for-menu prompt that waits for the user. */
        LOGW("stream: connection dropped by the host; reconnecting");
        if (stream_reconnect(cfg, app_id, display) != 0) {
            ui_menu_set_notice("Connection lost.");
            return -1;
        }
        /* stream_reconnect() re-started the session; loop back to input. */
    }
}

/* Auto-start ("default app"): once per process run, boot straight into the
 * configured game. the old 3-second "press CIRCLE for the menu"
 * window is gone - boot goes directly into the animated launch screen,
 * where CIRCLE still cancels into the menu at any point of the ladder. */
int stream_run(app_config_t *cfg, const char *config_dir) {
    s_connected = 0;
    int autostart_done = 0;

    LOGI("identity_init(%s)...", config_dir);
    if (identity_init(&s_id, config_dir) != GS_OK) {
        LOGE("identity_init FAIL: %s", gs_error ? gs_error : "?");
        return -1;
    }
    LOGI("identity_init OK");
    http_init(&s_id, 1);
    LOGI("http_init OK (verbose)");

    LOGI("input_init...");
    if (input_init() != 0) {
        LOGW("input_init failed; menu without pad (auto-launch)");
    } else {
        LOGI("input_init OK");
    }

    int connected = 0;
    if (cfg->host[0]) {
        connected = (stream_connect(cfg, config_dir) == 0);
    } else {
        LOGI("stream_run: empty host â†’ settings menu");
        ui_show_status("MOONLIGHT", "Set the Host",
                       "SETTINGS â†’ Host â†’ on-screen keyboard");
    }

    /* Boot auto-launch: straight into the default game when configured -
     * no grace window, the launch screen's CIRCLE-to-cancel covers bailing
     * out to the menu (and it can interrupt the ladder at any step). */
    if (connected && cfg->autostart && cfg->app_name[0] && !autostart_done) {
        autostart_done = 1;
        LOGI("autostart: booting straight into default app '%s'",
             cfg->app_name);
        input_reset();
        int rc = stream_play(cfg, config_dir);
        LOGI("stream_play => %d (autostart)", rc);
        if (rc == -2) {
            ui_menu_set_notice("Auto-start cancelled.");
        } else if (rc < 0) {
            ui_menu_set_notice("Auto-start failed. Pick a game below, or press "
                               "SQUARE on a game to change the default.");
            connected = 0;
        }
    }

    /* Menu <-> stream loop until the user closes with the PS button. */
    for (;;) {
        input_reset();
        int m = ui_menu_run(cfg, connected ? &s_server : NULL, config_dir);
        LOGI("ui_menu_run => %d", m);
        if (m == UI_MENU_EXIT) {
            LOGI("menu: exit requested; leaving cleanly");
            input_shutdown();
            return 0;
        }
        if (m < 0) {
            input_shutdown();
            return -1;
        }
        if (m == UI_MENU_RECONNECT || m == UI_MENU_RECONNECT_LAUNCH) {
            if (!cfg->host[0])
                continue;
            connected = (stream_connect(cfg, config_dir) == 0);
            /* One click = reconnect + launch: the user picked a game from
             * the cached list while disconnected; don't make them click it
             * again after the reconnect. */
            if (connected && m == UI_MENU_RECONNECT_LAUNCH && cfg->app_name[0]) {
                LOGI("reconnect-launch: '%s' after reconnect", cfg->app_name);
                int rc = stream_play(cfg, config_dir);
                LOGI("stream_play => %d (reconnect-launch)", rc);
                if (rc == -2) {
                    ui_menu_set_notice("Launch cancelled.");
                } else if (rc < 0) {
                    ui_menu_set_notice("Could not start the stream.");
                    connected = 0;
                }
            }
            continue;
        }
        if (!cfg->host[0]) {
            /* Launch without host: ask for host again. */
            continue;
        }
        if (!connected) {
            connected = (stream_connect(cfg, config_dir) == 0);
            continue;
        }

        int rc = stream_play(cfg, config_dir);
        LOGI("stream_play => %d", rc);
        if (rc == -2) {
            ui_menu_set_notice("Launch cancelled.");
        } else if (rc < 0) {
            ui_menu_set_notice("Could not start the stream.");
            connected = 0;
        }
    }
}
