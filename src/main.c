// Moonlight PS4 startup and title-side exit.
// Adapted from raferrazz/Moonlight-ps4 5cc1217. Fault handlers leave a marker;
// they do not repair the underlying fault or intercept an external system kill.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#ifdef __ORBIS__
#include <orbis/SystemService.h>
#endif

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

#define VERSION_STR "1.4.1"

#ifdef __ORBIS__
/* Terminate the process with SYS_exit directly. PS4's kernel is FreeBSD 9
 * derived: SYS_exit == 1, argument (exit status) in RDI. Never returns.
 * Deliberately free of locks, allocations, logging or stdio: it can be
 * invoked from any thread while others are mid-operation. */
static _Noreturn void orbis_sys_exit(int status) {
    __asm__ volatile(
        "movl $1, %%eax\n\t"          /* SYS_exit */
        "movl %0, %%edi\n\t"          /* status */
        "syscall\n"
        "1: pause\n\t"
        "jmp 1b\n\t"
        :
        : "r"(status)
        : "eax", "rdi", "rcx", "r11", "memory");
    __builtin_unreachable();
}

/* Pre-empt the CRT's broken exit path (see file header). The executable's
 * own definitions win over the dynamic libkernel exports, so every
 * exit()/_exit() call from CRT or library code lands here. */
_Noreturn void exit(int status) {
    orbis_sys_exit(status);
}

_Noreturn void _exit(int status) {
    orbis_sys_exit(status);
}

/* The SYS_exit override alone did not stop the crash dialog when
 * closing from the PS4 menu — which proves the system's close path does
 * not go through exit()/_exit() at all. Two plausible mechanisms remain:
 * a POSIX signal delivered to the process, or an external kill. If it is
 * a catchable signal, the handlers below turn the close into a clean
 * SYS_exit and leave an EXIT-MARKER line in debug.log. */
static void close_signal_handler(int sig) {
    /* Async-signal-safe: raw open/write/close inside, then kernel exit. */
    log_signal_marker("signal caught; clean exit");
    orbis_sys_exit(0);
}

/* convert fatal faults into a clean death. The
 * system's crash dialog (CE-34878-0) is shown for unhandled SIGSEGV et al.;
 * a handler that immediately SYS_exits reports nothing and leaves the
 * EXIT-MARKER for the log. The PS button > Close path bypasses every
 * catchable signal, so
 * these only cover faults the app raises in itself. */
static void fault_signal_handler(int sig) {
    char tag[48];
    /* Async-signal-safe number formatting (no snprintf in a handler). */
    char digits[12];
    int n = 0, v = sig;
    if (v < 0) v = -v;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    int o = 0;
    tag[o++] = 'f'; tag[o++] = 'a'; tag[o++] = 'u'; tag[o++] = 'l';
    tag[o++] = 't'; tag[o++] = ' ';
    while (n > 0 && o < (int)sizeof(tag) - 1)
        tag[o++] = digits[--n];
    tag[o] = '\0';
    log_signal_marker(tag);
    orbis_sys_exit(0);
}

static void install_close_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = close_signal_handler;
    sigemptyset(&sa.sa_mask);
    int installed = 0;
    const int sigs[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT};
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        char name[16];
        snprintf(name, sizeof(name), "sig %d", sigs[i]);
        if (sigaction(sigs[i], &sa, NULL) == 0)
            installed++;
        else
            LOGW("close: sigaction failed for %s errno=%d", name, errno);
    }
    LOGI("close: %d teardown signal handlers installed", installed);

    struct sigaction fa;
    memset(&fa, 0, sizeof(fa));
    fa.sa_handler = fault_signal_handler;
    sigemptyset(&fa.sa_mask);
    int faults = 0;
    const int fsigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSYS,
                         SIGTRAP};
    for (unsigned i = 0; i < sizeof(fsigs) / sizeof(fsigs[0]); i++) {
        char name[16];
        snprintf(name, sizeof(name), "sig %d", fsigs[i]);
        if (sigaction(fsigs[i], &fa, NULL) == 0)
            faults++;
        else
            LOGW("close: sigaction failed for %s errno=%d", name, errno);
    }
    LOGI("close: %d fault handlers installed (convert fatal to clean exit)",
         faults);
}

/* Title-side exit avoids relying on the system Close Application kill path. */
static _Noreturn void app_exit_clean(void) {
    LOGI("exit: sceSystemServiceLoadExec(\"exit\")");
    /* File logging is unbuffered, so the line above is already on disk. */
    int rc = sceSystemServiceLoadExec("exit", NULL);
    LOGW("exit: LoadExec => 0x%08x; SYS_exit fallback", (unsigned)rc);
    orbis_sys_exit(0);
}
#endif

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
#ifdef __ORBIS__
    install_close_handlers();
#endif
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

    ui_show_splash(cfg.ui_theme,cfg.reduced_motion);
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

#ifdef __ORBIS__
    if (rc == 0) {
        /* The user asked to leave (Settings > Exit): die the sanctioned
         * way instead of hanging into a system kill. */
        app_exit_clean();
    }
    hang_forever(rc < 0 ? "FAIL" : "unexpected stream_run return");
#else
    hang_forever(rc == 0 ? "OK Stream closed" : "FAIL");
#endif
    return 0;
}
