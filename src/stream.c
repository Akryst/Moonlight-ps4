// Orchestration: identity -> connect -> menu UI -> launch -> stream -> menu.

#include "stream.h"
#include "log.h"
#include "config.h"
#include "gamestream/client.h"
#include "gamestream/gs_http.h"
#include "gamestream/gs_errors.h"
#include "audio/audio_orbis.h"
#include "video/video.h"
#include "input/input_pad.h"
#include "ui/ui_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/time.h>

static gs_server_t s_server;
static client_identity_t s_id;
static volatile int s_connected;
static char s_stream_title[CONFIG_MAX_APP];
static int s_catalog_requested;

static void cl_stage_starting(int stage) {
    const char *name = LiGetStageName(stage);
    LOGI("connection: stage START %s (%d)", name, stage);
}
static void cl_stage_failed(int stage, int errorCode) {
    const char *name = LiGetStageName(stage);
    LOGE("connection: stage FAIL %s (%d) err=%d", name, stage, errorCode);
}
static void cl_connection_started(void) {
    s_connected = 1;
    LOGI("connection: streaming active");
    if (s_stream_title[0])
        LOGN("%s", s_stream_title);
}
static void cl_connection_terminated(int errorCode) {
    LOGE("connection: terminated err=%d", errorCode);
    s_connected = 0;
    input_request_quit();
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

/* serverinfo + pairing with the current host from cfg. */
static int stream_connect(app_config_t *cfg, const char *config_dir) {
    char pin[8] = {0};
    char line[160];

    snprintf(line, sizeof(line), "Host: %s", cfg->host);
    ui_show_status("MOONLIGHT PS4", "Connecting...", line);

    LOGI("gs_init(%s:%u)...", cfg->host, (unsigned)cfg->http_port);
    if (gs_init(&s_server, &s_id, cfg->host, cfg->http_port, config_dir) != GS_OK) {
        LOGE("gs_init FAIL: %s", gs_error ? gs_error : "?");
        ui_menu_set_notice(gs_error ? gs_error : "Could not connect to Sunshine. Retry or change your PC IP.");
        ui_show_status("ERROR", "Could not connect",
                       gs_error ? gs_error : "gs_init FAIL");
        return -1;
    }
    LOGI("server %s ver=%s paired=%d https=%u codecs=0x%x currentGame=%d",
         cfg->host, s_server.appVersion, s_server.paired, s_server.httpsPort,
         s_server.serverCodecModeSupport, s_server.currentGame);

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

        http_set_timeout_ms(120000);
        int pair_rc = gs_pair(&s_server, pin);
        http_set_timeout_ms(0);
        LOGI("gs_pair => %d (%s)", pair_rc, gs_error ? gs_error : "ok");
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
        ui_show_status("MOONLIGHT PS4", "Paired OK", "Opening menu...");
    } else {
        LOGI("already paired; skip pair");
    }
    /* Exactly once per app run, before the first app list is fetched. The
     * optional Windows companion authenticates our existing paired cert. */
    if (!s_catalog_requested) {
        s_catalog_requested = 1;
        http_resp_t response = {0};
        const char *saved_error = gs_error;
        ui_show_status("MOONLIGHT PS4", "Loading games...", "");
        http_set_timeout_ms(60000);
        int sync_rc = http_get(cfg->host, 47991, 1,
                               "/moonlight-ps4/open", &response);
        http_set_timeout_ms(0);
        http_resp_free(&response);
        gs_error = saved_error;
        LOGI("startup Steam sync => %d (companion is optional)", sync_rc);
        /* The companion may have reloaded Sunshine after a catalog change. */
        if (sync_rc == GS_OK)
            gs_refresh_status(&s_server);
    }
    return 0;
}

/* One stream session: launch + LiStartConnection + loop until exit. */
static int stream_play(app_config_t *cfg) {
    s_connected = 0;
    snprintf(s_stream_title, sizeof(s_stream_title), "%s", cfg->app_name);

    int app_id = resolve_app_id(&s_server, cfg->app_name);
    if (!app_id) {
        LOGE("app not found: '%s'", cfg->app_name);
        return -1;
    }
    LOGI("launch app_id=%d name='%s' %dx%d@%d bitrate=%d sops=%d localAudio=%d currentGame=%d",
         app_id, cfg->app_name, cfg->stream.width, cfg->stream.height,
         cfg->stream.fps, cfg->stream.bitrate, cfg->sops, cfg->local_audio,
         s_server.currentGame);

    /* If Sunshine already has another app active, cancel before launching. */
    if (s_server.currentGame != 0 && s_server.currentGame != app_id) {
        LOGI("stream: currentGame=%d != %d â†’ cancel previous",
             s_server.currentGame, app_id);
        (void)gs_quit_app(&s_server);
    }

    if (gs_start_app(&s_server, &cfg->stream, app_id, cfg->sops, cfg->local_audio, 0x1) != GS_OK) {
        LOGE("gs_start_app FAIL: %s", gs_error ? gs_error : "?");
        /* Retry: force clean launch after cancel. */
        if (s_server.currentGame != 0) {
            LOGI("stream: retry after cancel");
            (void)gs_quit_app(&s_server);
            if (gs_start_app(&s_server, &cfg->stream, app_id, cfg->sops, cfg->local_audio, 0x1) == GS_OK) {
                LOGI("gs_start_app OK (retry)");
                goto start_ok;
            }
            LOGE("gs_start_app retry FAIL: %s", gs_error ? gs_error : "?");
        }
        return -1;
    }
start_ok:
    LOGI("gs_start_app OK");

    input_reset();

    CONNECTION_LISTENER_CALLBACKS cl;
    LiInitializeConnectionCallbacks(&cl);
    cl.stageStarting = cl_stage_starting;
    cl.stageFailed = cl_stage_failed;
    cl.connectionStarted = cl_connection_started;
    cl.connectionTerminated = cl_connection_terminated;
    cl.logMessage = cl_log;
    cl.connectionStatusUpdate = cl_status;

    DECODER_RENDERER_CALLBACKS dr = video_callbacks_ffmpeg;
    int prefer_ycbcr = cfg->prefer_ycbcr ? 1 : 0;
    const char *decoder_reason = "default";
#if ML_ENABLE_VIDEODEC2
    if (!cfg->prefer_hw) {
        decoder_reason = "prefer_hw=false";
    } else {
        int probe = video_orbis_probe(cfg->stream.width, cfg->stream.height);
        if (probe == 0) {
            video_orbis_set_tuning(cfg->dec_pipeline_depth, cfg->dec_thread_prio,
                                   cfg->dec_au_onion, cfg->dec_fb_garlic);
            dr = video_callbacks_orbis;
            if (cfg->slices_per_frame > 0)
                dr.capabilities = CAPABILITY_SLICES_PER_FRAME(cfg->slices_per_frame);
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
         dr.submitDecodeUnit == video_callbacks_ffmpeg.submitDecodeUnit ? "SW" : "HW",
         cfg->prefer_hw ? "true" : "false", decoder_reason);
    AUDIO_RENDERER_CALLBACKS ar = audio_callbacks_orbis;

    video_set_show_stats(cfg->show_stats);
    if (cfg->show_stats && prefer_ycbcr)
        LOGW("stream: Perf overlay requires BGRA; disable YCbCr to see it");

    video_present_begin_session(cfg->dec_pipeline_depth);
    video_stats_begin_session();
    LOGI("LiStartConnection begin...");
    int ret = LiStartConnection(&s_server.serverInfo, &cfg->stream, &cl, &dr, &ar,
                                &prefer_ycbcr, sizeof(prefer_ycbcr), NULL, 0);
    LOGI("LiStartConnection => %d", ret);
    if (ret != 0) {
        gs_quit_app(&s_server);
        return -1;
    }

    LOGI("main loop; OPTIONS+TOUCHPAD 1s to quit");
    LOGI("input: L1+R1+OPTIONS sends host Guide (PS) button");

    struct timeval perf_start;
    gettimeofday(&perf_start, NULL);
    int ticks = 0;
    while (!input_poll()) {
        usleep(8000);
        if (++ticks % 125 == 0) {
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
        if (!s_connected && ticks > 250) {
            LOGE("stream: connection lost after %d ticks", ticks);
            break;
        }
    }

    LOGI("leaving loop (quit or disconnect)");
    LiStopConnection();
    /* No gs_quit_app: leave the game on Sunshine for resume â†’ "paused". */
    LOGN("Stream paused");
    LOGI("stream_play done OK â†’ return to menu");
    return 0;
}

int stream_run(app_config_t *cfg, const char *config_dir) {
    s_connected = 0;
    s_catalog_requested = 0;

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
        ui_show_status("MOONLIGHT PS4", "Set the Host",
                       "SETTINGS â†’ Host â†’ on-screen keyboard");
    }

    /* Menu <-> stream loop until the user closes with the PS button. */
    for (;;) {
        input_reset();
        int m = ui_menu_run(cfg, connected ? &s_server : NULL, config_dir);
        LOGI("ui_menu_run => %d", m);
        if (m < 0) {
            input_shutdown();
            return -1;
        }
        if (m == UI_MENU_RECONNECT) {
            if (!cfg->host[0])
                continue;
            connected = (stream_connect(cfg, config_dir) == 0);
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

        int rc = stream_play(cfg);
        LOGI("stream_play => %d", rc);
        if (rc < 0) {
            ui_menu_set_notice("Streaming failed. Check Sunshine and press TRIANGLE to reconnect.");
            connected = 0;
        }
    }
}
