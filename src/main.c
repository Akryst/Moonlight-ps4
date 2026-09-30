// Moonlight client for PS4: load config, start logging, and launch the stream.
// On Orbis main() must NOT return: the CRT calls exit()/_exit() and the kernel
// responds with SIGSYS. After finishing, hang forever until the user closes
// the app with the PS button.

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "log.h"
#include "config.h"
#include "ui/ui_menu.h"
#include "stream.h"
#include "video/video.h"
#include "orbis/net_orbis.h"

#ifdef __ORBIS__
#include "orbis/video_out_c.h"
#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#define CONFIG_DIR CONFIG_DIR_PS4
#else
#define CONFIG_DIR "."
#endif

#define VERSION_STR "1.3.3-dev"

static void read_line_file(const char *path, char *out, size_t outlen) {
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) {
        LOGI("read_line: %s does not exist", path);
        return;
    }
    if (fgets(out, (int)outlen, f))
        out[strcspn(out, " \r\n")] = '\0';
    fclose(f);
    LOGI("read_line: %s => '%s'", path, out);
}

#ifdef __ORBIS__
static void hang_forever(const char *reason) {
    LOGE("HANG: %s", reason ? reason : "(no reason)");
    for (;;)
        sceKernelUsleep(1000 * 1000);
}
#else
static void hang_forever(const char *reason) {
    LOGE("HANG: %s", reason ? reason : "(no reason)");
}
#endif

int main(void) {
#ifdef __ORBIS__
    config_ensure_dir(CONFIG_DIR);
    int us = sceUserServiceInitialize(NULL);
    LOGI("sceUserServiceInitialize => 0x%08x", (unsigned)us);
    /* Splash is hidden on the first UI frame (pairing/menu), not here:
     * hiding earlier leaves a black screen during gs_init/pair. */
#endif

    app_config_t cfg;
    LOGI("config_load(%s)...", CONFIG_DIR);
    int clr = config_load(&cfg, CONFIG_DIR);
    LOGI("config_load data => %d host='%s' debug='%s' app='%s'",
         clr, cfg.host, cfg.debug_host, cfg.app_name);

#ifdef __ORBIS__
    if (!cfg.host[0]) {
        LOGI("no host in /data; trying /app0/assets/misc");
        clr = config_load(&cfg, "/app0/assets/misc");
        LOGI("config_load app0 => %d host='%s' debug='%s'",
             clr, cfg.host, cfg.debug_host);
    }
#endif

    if (!cfg.debug_host[0])
        read_line_file(CONFIG_DIR "/debug_host.txt", cfg.debug_host, sizeof(cfg.debug_host));
    if (!cfg.debug_host[0])
        read_line_file("/app0/assets/misc/debug_host.txt", cfg.debug_host, sizeof(cfg.debug_host));
    if (!cfg.debug_host[0] && cfg.host[0]) {
        snprintf(cfg.debug_host, sizeof(cfg.debug_host), "%s", cfg.host);
        LOGI("debug_host=host (%s)", cfg.debug_host);
    }

    /* Log file only if the user enables it in settings / ini. */
    if (cfg.enable_file_log)
        log_set_file_enabled(1, CONFIG_DIR "/debug.log");

    log_init(cfg.debug_host, 9999);
    LOGI("=== moonlight-ps4 %s boot ===", VERSION_STR);
    LOGN("Moonlight PS4 %s", VERSION_STR);
    LOGI("moonlight-ps4 %s host=%s debug=%s %dx%d@%d %dkbps",
         VERSION_STR, cfg.host, cfg.debug_host,
         cfg.stream.width, cfg.stream.height, cfg.stream.fps, cfg.stream.bitrate);

    if (cfg.prefer_ycbcr)
        LOGN("YCbCr active");

    /* No host: don't hang. The menu lets the user type it in Settings. */

#ifdef __ORBIS__
    if (cfg.prefer_ycbcr && !video_present_plugin_loaded()) {
        LOGW("ycbcr_unlock.loaded MISSING â€” YCbCr will fail without GoldHEN plugin");
    } else if (cfg.prefer_ycbcr) {
        LOGI("ycbcr_unlock plugin: marker OK (%s)", ML_YCBCR_PLUGIN_MARKER);
    }

    /* Before any present init: the menu already brings up the BGRA path. */
    video_present_set_bgra_tuning(cfg.bgra_workers, cfg.bgra_nt);

    if (cfg.videodec2_spike) {
        int spike_rc = videodec2_spike_run();
        hang_forever(spike_rc == 0 ? "VD2 spike PASS" : "VD2 spike FAIL");
    }
#endif

    ui_show_splash(cfg.ui_theme);
    if (net_orbis_init() != 0) {
        hang_forever("FAIL sceNet/UDP init");
    }
    LOGI("net_orbis_init OK");

    LOGI("config_save(%s)...", CONFIG_DIR);
    if (config_save(&cfg, CONFIG_DIR) != 0)
        LOGW("config_save failed (errno=%d)", errno);
    else
        LOGI("config_save ok");

    LOGI(">>> stream_run begin");
    int rc = stream_run(&cfg, CONFIG_DIR);
    LOGI("<<< stream_run => %d", rc);

    hang_forever(rc == 0 ? "OK Stream closed" : "FAIL");
    return 0;
}
