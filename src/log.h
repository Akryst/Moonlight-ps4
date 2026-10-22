#pragma once

#include <stdarg.h>

// Logging: UDP (nc -u -l -p 9999) and optional file.
// LOGN/log_notify: Orbis toast — use ONLY for user-facing events
// (version, ycbcr, title, pause, close).

void log_init(const char *udp_host, unsigned short udp_port);
void log_init_file(const char *path);
void log_close_file(void);
void log_set_file_enabled(int enable, const char *path);
void log_shutdown(void);

void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_vprintf(const char *fmt, va_list ap);

// Async-signal-safe one-line marker for teardown/exit paths.
// Raw open/write/close of the log file; safe from signal handlers.
void log_signal_marker(const char *msg);

void log_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOGI(fmt, ...) log_printf("[I] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) log_printf("[W] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) log_printf("[E] " fmt "\n", ##__VA_ARGS__)
#define LOGN(fmt, ...) log_notify(fmt, ##__VA_ARGS__)
