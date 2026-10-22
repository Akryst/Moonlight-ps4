#include "log.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#ifdef __ORBIS__
#include <orbis/libkernel.h>
#endif

static int s_sock = -1;
static pthread_mutex_t log_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct sockaddr_in s_dst;
static FILE *s_file;
static int s_udp_ok;
static char s_log_path[256];
static unsigned long long s_log_t0_ms;

static unsigned long long log_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ull + (unsigned long long)tv.tv_usec / 1000ull;
}

static void log_init_file_locked(const char *path) {
    if (!path || !path[0])
        return;
    snprintf(s_log_path, sizeof(s_log_path), "%s", path);
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    /* Rotation: streaming sessions emit several log lines per
     * second, so an always-on file log grows without bound. Keep the file
     * keeps itself useful by starting fresh once it passes 1 MB. */
    FILE *probe = fopen(path, "r");
    if (probe) {
        int too_big = 0;
        if (fseek(probe, 0, SEEK_END) == 0)
            too_big = ftell(probe) > 1024L * 1024L;
        fclose(probe);
        if (too_big) {
            FILE *fresh = fopen(path, "w");
            if (fresh)
                fclose(fresh);
        }
    }
    s_file = fopen(path, "a");
    if (s_file) {
        setvbuf(s_file, NULL, _IONBF, 0);
        fprintf(s_file, "\n----- log start -----\n");
    }
}

static void log_close_file_locked(void) {
    if (!s_file)
        return;
    fprintf(s_file, "----- log end -----\n");
    fclose(s_file);
    s_file = NULL;
}

void log_set_file_enabled(int enable, const char *path) {
    if (enable) {
        if (!s_file)
            log_init_file(path && path[0] ? path : "/data/moonlight/debug.log");
    } else {
        log_close_file();
    }
}

void log_init(const char *udp_host, unsigned short udp_port) {
    s_udp_ok = 0;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }

    if (!udp_host || !udp_host[0]) {
        LOGW("log: no debug_host; no UDP");
        return;
    }

    memset(&s_dst, 0, sizeof(s_dst));
    s_dst.sin_family = AF_INET;
    s_dst.sin_port = htons(udp_port);
    if (inet_pton(AF_INET, udp_host, &s_dst.sin_addr) != 1) {
        LOGE("log: invalid debug_host: %s", udp_host);
        return;
    }

    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0) {
        LOGE("log: socket UDP errno=%d", errno);
        return;
    }
    s_udp_ok = 1;
    LOGI("log: UDP %s:%u ok; file=%s", udp_host, udp_port,
         s_file ? "si" : "no");
}

void log_shutdown(void) {
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    log_close_file();
    s_udp_ok = 0;
}

/* Async-signal-safe marker for teardown paths: raw open/write/
 * close only, usable from a signal handler while other threads are
 * mid-stdio. Writes to the log file last-known path; harmless no-op when
 * file logging is off (the handler exits immediately afterwards anyway). */
void log_signal_marker(const char *msg) {
    if (!s_log_path[0])
        return;
    int fd = open(s_log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0)
        return;
    static const char prefix[]="EXIT-MARKER: ";
    size_t length=0;while(msg[length] && length<96)length++;
    (void)write(fd,prefix,sizeof(prefix)-1);(void)write(fd,msg,length);(void)write(fd,"\n",1);
    close(fd);
}

static void log_vprintf_locked(const char *fmt, va_list ap) {
    char buf[1536];
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap2);
    if (len < 0)
        return;
    if (len >= (int)sizeof(buf))
        len = (int)sizeof(buf) - 1;

    /* Elapsed-time stamp: a long stall leaves no way to tell WHERE
     * the time went when lines carry no timestamps. Every line carries
     * seconds.milliseconds since the first log line, which makes the
     * log directly correlatable with user-visible symptoms. */
    if (!s_log_t0_ms)
        s_log_t0_ms = log_time_ms();
    unsigned long long ms = log_time_ms() - s_log_t0_ms;
    char stamp[32];
    int slen = snprintf(stamp, sizeof(stamp), "[t=%llu.%03llu] ",
                        ms / 1000ull, ms % 1000ull);
    if (slen < 0)
        slen = 0;

    if (s_file) {
        fwrite(stamp, 1, (size_t)slen, s_file);
        fwrite(buf, 1, (size_t)len, s_file);
        if (len == 0 || buf[len - 1] != '\n')
            fputc('\n', s_file);
    }

    if (s_sock >= 0) {
        if (slen > 0)
            sendto(s_sock, stamp, (size_t)slen, 0,
                   (struct sockaddr *)&s_dst, sizeof(s_dst));
        sendto(s_sock, buf, (size_t)len, 0,
               (struct sockaddr *)&s_dst, sizeof(s_dst));
    }
#ifndef __ORBIS__
    else if (!s_file) {
        /* Host builds with no UDP host and no log file: keep logs visible
         * on stderr instead of silently dropping them. The PS4 build keeps
         * the original behavior (splash/console has no stderr worth using). */
        fwrite(stamp, 1, (size_t)slen, stderr);
        fwrite(buf, 1, (size_t)len, stderr);
        if (len == 0 || buf[len - 1] != '\n')
            fputc('\n', stderr);
    }
#endif
}

void log_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vprintf(fmt, ap);
    va_end(ap);
}

void log_notify(const char *fmt, ...) {
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

#ifdef __ORBIS__
    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, text, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
#endif
    log_printf("[N] %s\n", text);
}

void log_init_file(const char *path){pthread_mutex_lock(&log_mutex);log_init_file_locked(path);pthread_mutex_unlock(&log_mutex);}

void log_close_file(void){pthread_mutex_lock(&log_mutex);log_close_file_locked();pthread_mutex_unlock(&log_mutex);}

void log_vprintf(const char *fmt,va_list ap){pthread_mutex_lock(&log_mutex);log_vprintf_locked(fmt,ap);pthread_mutex_unlock(&log_mutex);}
