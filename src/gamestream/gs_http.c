#include "gs_http.h"
#include "gs_errors.h"
#include "../log.h"

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/error.h>

#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* OpenOrbis's <sys/ioctl.h> ships only LINUX_FIONBIO (its musl port is
 * Linux-header based); the Orbis kernel is FreeBSD-derived and BSD's
 * socket FIONBIO id is different. Neither id is field-proven for Orbis
 * TCP sockets (hardware logs show the sweep falling through
 * both), so the helpers below try FIONBIO (BSD id on Orbis), then
 * LINUX_FIONBIO, then fcntl — a wrong ioctl id fails cleanly (ENOTTY)
 * and the caller falls back. */
#ifndef FIONBIO
#ifdef __ORBIS__
#define FIONBIO 0x8004667e /* FreeBSD _IOW('f', 126, int) */
#elif defined(LINUX_FIONBIO)
#define FIONBIO LINUX_FIONBIO
#endif
#endif

#define HTTP_TIMEOUT_DEFAULT_MS 7000
#define MAX_RESPONSE (4 * 1024 * 1024)
#define POOL_MAX_IDLE_MS 2000
#define POOL_STRIKE_LIMIT 3

static client_identity_t *s_id;
static int s_verbose;

static pthread_key_t timeout_key;
static pthread_once_t timeout_once=PTHREAD_ONCE_INIT;
static void timeout_init(void){pthread_key_create(&timeout_key,NULL);}
void http_set_timeout_ms(unsigned ms){pthread_once(&timeout_once,timeout_init);pthread_setspecific(timeout_key,(void *)(uintptr_t)ms);}
static unsigned default_timeout(void){pthread_once(&timeout_once,timeout_init);unsigned ms=(unsigned)(uintptr_t)pthread_getspecific(timeout_key);return ms?ms:HTTP_TIMEOUT_DEFAULT_MS;}

void http_init(client_identity_t *id, int verbose) {
    s_id = id;
    s_verbose = verbose;
}

void http_resp_free(http_resp_t *resp) {
    free(resp->body);
    resp->body = NULL;
    resp->len = 0;
}

/* One in-flight connection. Heap-allocated, owned by exactly one request
 * at a time (in flight, or parked in the pool slot). The mbedtls objects
 * contain self-referential pointers (set_bio binds &c->net), so this
 * struct must NEVER be copied or moved — ownership passes by pointer. */
typedef struct connection {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    int use_tls;
    int tls_started;                 /* ssl/conf were initialized */
    unsigned timeout_ms;             /* budget of the owning request */
    unsigned long long deadline;
    atomic_int cancelled;
    pthread_t owner;
    struct connection *next;
    char host[128];
    unsigned short port;
    unsigned long long last_used_ms; /* idle age while pooled */
} conn_t;

static pthread_mutex_t active_mutex=PTHREAD_MUTEX_INITIALIZER;
static conn_t *active;
static void active_add(conn_t *c){
    c->owner=pthread_self();atomic_store(&c->cancelled,0);
    pthread_mutex_lock(&active_mutex);c->next=active;active=c;pthread_mutex_unlock(&active_mutex);
}
static void active_remove(conn_t *c){
    pthread_mutex_lock(&active_mutex);conn_t **p=&active;while(*p && *p!=c)p=&(*p)->next;
    if(*p)*p=c->next;pthread_mutex_unlock(&active_mutex);
}
void http_cancel_thread(pthread_t thread){
    pthread_mutex_lock(&active_mutex);
    for(conn_t *c=active;c;c=c->next)if(pthread_equal(c->owner,thread)){
        atomic_store(&c->cancelled,1);if(c->net.fd>=0)shutdown(c->net.fd,SHUT_RDWR);
    }
    pthread_mutex_unlock(&active_mutex);
}
void http_cancel_pending(void){
    pthread_mutex_lock(&active_mutex);
    for(conn_t *c=active;c;c=c->next){atomic_store(&c->cancelled,1);if(c->net.fd>=0)shutdown(c->net.fd,SHUT_RDWR);}
    pthread_mutex_unlock(&active_mutex);
}
static unsigned long long now_ms(void);
static unsigned remaining(conn_t *c){
    unsigned long long now=now_ms();
    return atomic_load(&c->cancelled)||now>=c->deadline?0:(unsigned)(c->deadline-now);
}

/* Pool: a single idle connection slot. Guarded by s_lock, which is held
 * ONLY for pointer hand-off and counter updates — never across connect,
 * handshake or I/O (see the threading model in gs_http.h for the
 * failure this prevents). */
static conn_t *s_pool;
static int s_pool_strikes; /* consecutive failed pooled reuses (s_lock) */
static int s_pool_banned;  /* keep-alive disabled for the process (s_lock) */

/* The TLS session cache is GONE. Hardware forensics
 * proved the resume attempt is a liability against Sunshine: every
 * "(resume)" ClientHello on the /launch path hung for the request's full
 * budget (15 s x 2 attempts, both TLS handshake read timeouts) while
 * every FULL handshake around it completed in ~300 ms — including a
 * resumed handshake 0.1 s after pairing and a fresh one 0.3 s after a
 * failed resume. The server simply never answers those resumed
 * ClientHellos; the ~250 ms a successful resume saves is not worth a
 * launch flow that can never complete. Full handshakes only, always. */

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#define s_tls_lock identity_crypto_mutex
static pthread_mutex_t s_err_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ull + tv.tv_usec / 1000ull;
}

/* Composed connect error (reason included). gs_error points here after a
 * connect failure; the string is also what the UI error screen shows, so
 * it must stay short and human-readable. */
#define s_conn_err (gs_error_buffer())

static const char *errno_reason(int e) {
    switch (e) {
    case ECONNREFUSED: return "connection refused";
    case EHOSTUNREACH:
    case ENETUNREACH:
    case ENETDOWN:
    case EADDRNOTAVAIL: return "host unreachable";
    case ETIMEDOUT: return "connect timeout";
    default: return NULL;
    }
}

/* Put a TCP socket into non-blocking mode.
 *
 * Field facts (PS4 hardware):
 *  - fcntl(F_SETFL, O_NONBLOCK) FAILS with EACCES for TCP sockets on
 *    Orbis (errno=13 on every connect).
 *  - ioctl(FIONBIO) (BSD + Linux ids) also fails there; the sweep
 *    fell through both into the blocking worker pool.
 *  - socket(SOCK_NONBLOCK) creation is rejected as well (the socket()
 *    call itself fails - it is not merely ignored).
 * Nothing depends on any of these working: with 0 the caller runs a
 * blocking connect (bounded on sane stacks by SO_SNDTIMEO; on Orbis it
 * is bounded only by the kernel — see the header).
 * Returns: 2 = ioctl(FIONBIO) set, 1 = fcntl set (flags in *fl_out),
 *          0 = still blocking (caller MUST tolerate a long connect). */
static int sock_set_nonblocking(int fd, long *fl_out) {
    int one = 1;
    if (ioctl(fd, FIONBIO, &one) == 0)
        return 2;
#ifdef LINUX_FIONBIO
    if (ioctl(fd, LINUX_FIONBIO, &one) == 0)
        return 2;
#endif
    long fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0) {
        *fl_out = fl;
        return 1;
    }
    *fl_out = -1;
    return 0;
}

static void sock_restore_blocking(int fd, int mode, long fl) {
    if (mode == 1)
        (void)fcntl(fd, F_SETFL, fl);
    else if (mode == 2) {
        int zero = 0;
        (void)ioctl(fd, FIONBIO, &zero);
#ifdef LINUX_FIONBIO
        (void)ioctl(fd, LINUX_FIONBIO, &zero);
#endif
    }
}

static int s_warned_blocking;

/* Bounded TCP connect. Bounded TCP connect (poll+SO_ERROR
 * tolerances, SO_SNDTIMEO backstop, recognized hard errors only), with
 * the timeout now a parameter instead of global state and the "blocking
 * unavailable" warning printed once per process instead of on every
 * connect
 *
 * IMPORTANT: on Orbis this runs in blocking mode and SO_SNDTIMEO
 * does NOT bound connect() there — a dead/firewalled target blocks for
 * the kernel's SYN budget (~75 s). That is tolerable ONLY because the
 * caller no longer holds any lock while this runs. */
static int conn_connect(mbedtls_net_context *ctx, const char *host,
                        const char *port, unsigned timeout_ms) {
#ifdef __ORBIS__
    int rc=mbedtls_net_connect(ctx,host,port,MBEDTLS_NET_PROTO_TCP);
    if(rc){const char *reason=errno_reason(errno);snprintf(gs_error_buffer(),512,"Could not connect to host (%s)",reason?reason:"connect failed");gs_error=gs_error_buffer();return GS_IO_ERROR;}
    struct timeval tv={timeout_ms/1000u,(timeout_ms%1000u)*1000u};
    setsockopt(ctx->fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    return GS_OK;
#else
    struct addrinfo hints, *list = NULL, *rp;
    int fd = -1;
    const char *reason = "connect failed";
    int last_errno = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &list) != 0 || !list) {
        gs_error = "Could not resolve host";
        return GS_IO_ERROR;
    }

    for (rp = list; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, IPPROTO_TCP);
        if (fd < 0) {
            reason = "socket failed";
            last_errno = errno;
            continue;
        }

        /* Backstop for every mode: bounds later sends, and bounds
         * connect() on stacks that honor SO_SNDTIMEO for it (Linux does;
         * Orbis does not — see above). */
        {
            struct timeval tv;
            tv.tv_sec = (time_t)(timeout_ms / 1000u);
            tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
            (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }

        long nb_fl = -1;
        int nb = sock_set_nonblocking(fd, &nb_fl);
        if (nb == 0 && !s_warned_blocking) {
            s_warned_blocking = 1;
            LOGW("http: non-blocking unavailable (fcntl errno=%d); "
                 "connects are blocking on this system (Orbis: up to the "
                 "kernel SYN budget for filtered/dead hosts)", errno);
        }

        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0)
            goto connected;

        int e = errno;
        last_errno = e;
        if (e == EISCONN)
            goto connected;

        if (errno_reason(e) && e != ETIMEDOUT) {
            /* Recognized hard failure for this address: skip it now. */
            reason = errno_reason(e);
            close(fd);
            fd = -1;
            continue;
        }

        if (nb == 0) {
            /* Blocking stack: connect() has already waited and returned.
             * EAGAIN/EWOULDBLOCK (FreeBSD) or EINPROGRESS/EALREADY
             * (Linux) is the SO_SNDTIMEO expiry — a timeout, not a
             * socket error. On Orbis a blocking connect that returns
             * ETIMEDOUT is the kernel's SYN budget running out. */
            if (e == EAGAIN || e == EWOULDBLOCK || e == EINPROGRESS ||
                e == EALREADY) {
                reason = "connect timeout";
                last_errno = ETIMEDOUT;
            } else {
                reason = "connect failed";
            }
            close(fd);
            fd = -1;
            continue;
        }

        /* EINPROGRESS / EALREADY / anything we do not positively
         * recognize (including untranslated Orbis codes): wait for the
         * real outcome. */
        {
            unsigned long long deadline = now_ms() + timeout_ms;
            int connected = 0;
            reason = "connect timeout";
            while (!connected) {
                long long left = (long long)(deadline - now_ms());
                if (left <= 0)
                    break;
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                int pr = poll(&pfd, 1, (int)(left > 60000 ? 60000 : left));
                if (pr < 0) {
                    if (errno == EINTR)
                        continue;
                    reason = "poll failed";
                    last_errno = errno;
                    break;
                }
                if (pr == 0)
                    break; /* reason already "connect timeout" */
                if (pfd.revents & POLLNVAL) {
                    reason = "poll invalid fd";
                    break;
                }
                int soerr = 0;
                socklen_t sol = sizeof(soerr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sol) != 0) {
                    /* SO_ERROR unavailable: assume connected and let the
                     * request write decide (it has its own error paths). */
                    connected = 1;
                    break;
                }
                if (soerr == 0 || soerr == EISCONN) {
                    connected = 1;
                    break;
                }
                if (soerr == EINPROGRESS || soerr == EALREADY ||
                    soerr == EAGAIN || soerr == EWOULDBLOCK) {
                    continue; /* signalled before establishment */
                }
                last_errno = soerr;
                reason = errno_reason(soerr) ? errno_reason(soerr)
                                             : "connect failed";
                break;
            }
            if (!connected) {
                close(fd);
                fd = -1;
                continue;
            }
        }

connected:
        /* Connected: restore blocking mode — the send/recv paths carry
         * their own timeouts, and SO_SNDTIMEO keeps a wedged send from
         * hanging forever. */
        sock_restore_blocking(fd, nb, nb_fl);
        break;
    }
    freeaddrinfo(list);

    if (fd < 0) {
        pthread_mutex_lock(&s_err_lock);
        snprintf(s_conn_err, 512,
                 "Could not connect to host (%s)", reason);
        gs_error = s_conn_err;
        pthread_mutex_unlock(&s_err_lock);
        LOGE("http: connect FAIL %s:%s (%s errno=%d/0x%x)", host, port,
             reason, last_errno, (unsigned)last_errno);
        return GS_IO_ERROR;
    }
    ctx->fd = fd;
    return GS_OK;
#endif
}

/* --- connection lifecycle ------------------------------------------- */

static void conn_close_free(conn_t *c) {
    if (!c)
        return;
    if (c->tls_started) {
        (void)mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
    }
    active_remove(c);
    mbedtls_net_free(&c->net);
    free(c);
}

/* Opens a fresh connection to host:port with the request's timeout.
 * Connect runs WITHOUT any lock held (on Orbis it can block for the
 * kernel SYN budget against a filtered port — that must only ever stall
 * this request's own thread). The TLS handshake runs under s_tls_lock:
 * the identity's DRBG/RSA key are shared (full handshakes only —
 * the session cache is gone). A handshake only ever runs
 * against a host that just
 * ACCEPTED the TCP connection, so the lock hold is the handshake itself.
 * On failure the caller owns cleanup (conn_close_free). */
static int conn_open(conn_t *c, const char *host, unsigned short port,
                     int use_tls, unsigned timeout_ms) {
    char port_str[8];
    int ret;

    memset(c, 0, sizeof(*c));
    c->use_tls = use_tls;
    c->timeout_ms = timeout_ms;
    mbedtls_net_init(&c->net);
    c->deadline=now_ms()+timeout_ms;
    snprintf(c->host, sizeof(c->host), "%s", host);
    c->port = port;

    snprintf(port_str, sizeof(port_str), "%u", port);

    LOGI("http: connect %s:%s tls=%d timeout=%ums", host, port_str,
         use_tls, timeout_ms);
    ret = conn_connect(&c->net, host, port_str, timeout_ms);
    if (ret != GS_OK)
        return ret;

    active_add(c);
    if(!remaining(c)){gs_error="Request timed out";return GS_IO_ERROR;}
    if (!use_tls)
        return GS_OK;

    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    c->tls_started = 1;

    ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
        goto tls_fail;

    // Server certificate is self-signed; real authenticity comes from
    // pairing (signature verification), not PKI.
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &s_id->rng);
    mbedtls_ssl_conf_read_timeout(&c->conf, timeout_ms);

    ret = mbedtls_ssl_conf_own_cert(&c->conf, &s_id->cert, &s_id->key);
    if (ret != 0)
        goto tls_fail;

    ret = mbedtls_ssl_setup(&c->ssl, &c->conf);
    if (ret != 0)
        goto tls_fail;

    mbedtls_ssl_set_bio(&c->ssl, &c->net, mbedtls_net_send, NULL,
                        mbedtls_net_recv_timeout);

    pthread_mutex_lock(&s_tls_lock);
    {
        /* Wall-clock guard around the handshake loop: WANT_READ is
         * bounded by conf read_timeout and WANT_WRITE by SO_SNDTIMEO,
         * but a hard cap keeps a pathological server from pinning
         * s_tls_lock forever. */
        unsigned long long deadline = now_ms() + (unsigned long long)timeout_ms * 2ull + 3000ull;
        while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE)
                break;
            if (now_ms() > deadline) {
                ret = MBEDTLS_ERR_SSL_TIMEOUT;
                break;
            }
        }
    }
    if (ret != 0) {
        pthread_mutex_unlock(&s_tls_lock);
        goto tls_fail;
    }
    pthread_mutex_unlock(&s_tls_lock);
    return GS_OK;

tls_fail:
    if (s_verbose) {
        char err[128];
        mbedtls_strerror(ret, err, sizeof(err));
        LOGE("http: TLS failure: -0x%04x %s", (unsigned)-ret, err);
    }
    gs_error = "TLS handshake failed";
    return GS_IO_ERROR;
}

/* --- pool (s_lock guards pointer hand-off only) ---------------------- */

static void pool_strike(void) {
    pthread_mutex_lock(&s_lock);
    if (++s_pool_strikes >= POOL_STRIKE_LIMIT && !s_pool_banned) {
        s_pool_banned = 1;
        LOGW("http: server closes keep-alive connections; pooling disabled");
    }
    pthread_mutex_unlock(&s_lock);
}

/* Take the pooled connection if it can serve this request. Mismatched or
 * stale entries are closed OUTSIDE the lock (close_notify is a write). */
static conn_t *pool_checkout(const char *host, unsigned short port,
                             int use_tls) {
    pthread_mutex_lock(&s_lock);
    conn_t *c = s_pool;
    s_pool = NULL;
    pthread_mutex_unlock(&s_lock);
    if (!c)
        return NULL;
    if (s_pool_banned ||
        c->use_tls != use_tls || c->port != port || strcmp(c->host, host) != 0 ||
        (c->last_used_ms && now_ms() - c->last_used_ms > POOL_MAX_IDLE_MS)) {
        conn_close_free(c);
        return NULL;
    }
    return c;
}

/* Pre-flight: catch a server-closed connection BEFORE the doomed
 * write. The close-happy Sunshine fork sends FIN right after responding;
 * that shows up as a zero-timeout poll event, and recv(MSG_PEEK) then
 * yields EOF (0), an error (-1) or stale bytes (>0) — all three mean
 * this connection cannot serve the request. One syscall instead of the
 * write -> RST -> failed-read -> reconnect round trip. */
static int pool_preflight_dead(conn_t *c) {
    struct pollfd pf;
    pf.fd = c->net.fd;
    pf.events = POLLIN;
    pf.revents = 0;
    if (poll(&pf, 1, 0) != 0) {
        LOGI("http: pooled conn closed by server; fresh connect");
        return 1;
    }
    return 0;
}

/* drain a parked connection to EOF (or a short
 * quiet window) before discarding it. The first poll is non-blocking, so
 * an idle connection costs nothing; once bytes or a FIN arrive they are
 * consumed (with a small grace window) so the server-side teardown of the
 * old socket completes before the fresh request can race it. */
static void conn_drain(conn_t *c) {
    unsigned char sink[4096];
    for (int round = 0; round < 32; round++) {
        struct pollfd pf;
        pf.fd = c->net.fd;
        pf.events = POLLIN;
        pf.revents = 0;
        int pr = poll(&pf, 1, round ? 30 : 0);
        if (pr <= 0) /* quiet or poll error: drained */
            return;
        if (recv(c->net.fd, sink, sizeof(sink), 0) <= 0)
            return; /* EOF or error: drained */
    }
}

static void pool_checkin(conn_t *c, int from_pool) {
    if (!c)
        return;
    if (from_pool) {
        pthread_mutex_lock(&s_lock);
        s_pool_strikes = 0; /* a live pooled reuse proves the server
                             * keeps connections open */
        pthread_mutex_unlock(&s_lock);
    }
    pthread_mutex_lock(&s_lock);
    if (s_pool || s_pool_banned) {
        pthread_mutex_unlock(&s_lock);
        conn_close_free(c);
        return;
    }
    active_remove(c);
    c->last_used_ms = now_ms();
    s_pool = c;
    pthread_mutex_unlock(&s_lock);
}

/* --- request I/O ------------------------------------------------------ */

static int conn_read(conn_t *c, unsigned char *buf, size_t len) {
    int ret;
    do {
        unsigned budget=remaining(c);if(!budget)return -1;
        if(c->use_tls)mbedtls_ssl_conf_read_timeout(&c->conf,budget);
        if (c->use_tls)
            ret = mbedtls_ssl_read(&c->ssl, buf, len);
        else
            ret = mbedtls_net_recv_timeout(&c->net, buf, len, budget);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;
    return ret;
}

static int conn_write_all(conn_t *c, const unsigned char *buf, size_t len) {
    size_t off = 0;
    /* Cap the whole write: mbedTLS surfaces a stuck socket as
     * WANT_WRITE (EAGAIN under SO_SNDTIMEO), which must not spin here
     * without a wall-clock budget. */
    unsigned long long deadline =
        now_ms() + (unsigned long long)c->timeout_ms + 3000ull;
    while (off < len) {
        if(!remaining(c))return -1;
        int ret;
        if (c->use_tls)
            ret = mbedtls_ssl_write(&c->ssl, buf + off, len - off);
        else
            ret = mbedtls_net_send(&c->net, buf + off, len - off);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (now_ms() > deadline) {
                LOGW("http: write deadline exceeded (wrote %zu/%zu)", off, len);
                return -1;
            }
            continue;
        }
        if (ret <= 0)
            return -1;
        off += (size_t)ret;
    }
    return 0;
}

// Read until exactly `len` bytes are complete.
static int read_exact(conn_t *c, unsigned char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        int n = conn_read(c, buf + off, len - off);
        if (n <= 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

// Read a line ending in \n (included) into buf.
static int read_line(conn_t *c, char *buf, size_t buflen) {
    size_t o = 0;
    while (o + 1 < buflen) {
        unsigned char ch;
        int n = conn_read(c, &ch, 1);
        if (n <= 0)
            return -1;
        buf[o++] = (char)ch;
        if (ch == '\n')
            break;
    }
    buf[o] = '\0';
    return o && buf[o-1]=='\n'?(int)o:-1;
}

/* One attempt: uses the pool when possible, otherwise opens a connection
 * (connect OUTSIDE any lock). Never retries internally — the caller
 * retries once on a stale pool. The connection is returned to the pool
 * only when a reuse is safe. */
static int http_get_attempt(const char *host, unsigned short port, int use_tls,
                            const char *path, http_resp_t *resp,
                            unsigned timeout_ms, unsigned flags,
                            int *used_pool) {
    conn_t *c = NULL;
    char line[1024];
    int ret = GS_FAILED;
    long content_length = -1;
    int chunked = 0;
    int keep_alive = 0; /* resolved after the status line / headers */
    int http10 = 0;
    int poolable_after = 1;
    int from_pool = 0;

    resp->body = NULL;
    resp->len = 0;

    if (!(flags & HTTP_FLAG_FRESH)) {
        c = pool_checkout(host, port, use_tls);
        if (c && pool_preflight_dead(c)) {
            conn_close_free(c);
            c = NULL;
        }
    } else {
        /* FRESH never rides a pooled socket, but a PARKED connection to
         * the same endpoint is drained and discarded first (01.46): the
         * host's teardown of the parked socket then finishes before the
         * fresh connection's request can arrive. */
        conn_t *parked = pool_checkout(host, port, use_tls);
        if (parked) {
            LOGI("http: draining parked connection before fresh open");
            conn_drain(parked);
            conn_close_free(parked);
        }
    }
    if (c) {
        from_pool = 1;
        *used_pool = 1;
        c->timeout_ms = timeout_ms; /* budget of THIS request */
        c->deadline=now_ms()+timeout_ms;active_add(c);
        if (c->use_tls)
            mbedtls_ssl_conf_read_timeout(&c->conf, timeout_ms);
        if (s_verbose)
            LOGI("http: reusing pooled %s://%s:%u", use_tls ? "https" : "http",
                 host, port);
    } else {
        *used_pool = 0;
        c = malloc(sizeof(*c));
        if (!c) {
            gs_error = "Out of memory";
            return GS_OUT_OF_MEMORY;
        }
        ret = conn_open(c, host, port, use_tls, timeout_ms);
        if (ret != GS_OK) {
            LOGE("http: conn_open FAIL ret=%d err=%s", ret,
                 gs_error ? gs_error : "");
            conn_close_free(c);
            return ret;
        }
    }

    char req[6144];
    int reqlen = snprintf(req, sizeof(req),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s:%u\r\n"
                          "User-Agent: moonlight-ps4/0.1\r\n"
                          "Accept: */*\r\n"
                          "Connection: keep-alive\r\n"
                          "\r\n",
                          path, host, port);
    if (reqlen <= 0 || reqlen >= (int)sizeof(req)) {
        gs_error = "URL too long";
        ret = GS_INVALID;
        goto out;
    }

    if (conn_write_all(c, (unsigned char *)req, (size_t)reqlen) != 0) {
        gs_error = "Failed to send request";
        ret = GS_IO_ERROR;
        goto out;
    }

    // Status line
    if (read_line(c, line, sizeof(line)) <= 0) {
        gs_error = "No response from server";
        ret = GS_IO_ERROR;
        goto out;
    }
    int http_status = 0;
    {
        char version[16] = {0};
        if (sscanf(line, "%15s %d", version, &http_status) < 2 ||
            strncmp(version, "HTTP/", 5) != 0) {
            gs_error = "Invalid HTTP response";
            ret = GS_IO_ERROR;
            goto out;
        }
        if (!strcmp(version, "HTTP/1.0"))
            http10 = 1;
    }
    /* HTTP/1.1 defaults to keep-alive, HTTP/1.0 to close. The Connection
     * header parsed below overrides either way. */
    keep_alive = http10 ? 0 : 1;

    // Headers
    for (;;) {
        if (read_line(c, line, sizeof(line)) <= 0) {
            gs_error = "Truncated HTTP headers";
            ret = GS_IO_ERROR;
            goto out;
        }
        if (!strcmp(line, "\r\n") || !strcmp(line, "\n"))
            break;
        if (!strncasecmp(line, "Content-Length:", 15))
            content_length = strtol(line + 15, NULL, 10);
        else if (!strncasecmp(line, "Transfer-Encoding:", 18) && strstr(line, "chunked"))
            chunked = 1;
        else if (!strncasecmp(line, "Connection:", 11)) {
            if (strstr(line, "close"))
                keep_alive = 0;
            else if (strstr(line, "keep-alive"))
                keep_alive = 1;
        }
    }

    // Body
    if (chunked) {
        size_t cap = 8192;
        resp->body = malloc(cap);
        if (!resp->body) {
            ret = GS_OUT_OF_MEMORY;
            goto out;
        }
        for (;;) {
            if (read_line(c, line, sizeof(line)) <= 0) {
                ret = GS_IO_ERROR;
                goto out;
            }
            char *end;errno=0;long chunk = strtol(line, &end, 16);
            if (errno || end==line || (*end!=';' && *end!='\r' && *end!='\n') || chunk < 0 || (size_t)chunk > MAX_RESPONSE) {
                ret = GS_IO_ERROR;
                goto out;
            }
            if (chunk == 0) {
                poolable_after=0; /* trailers are not parsed; never reuse this connection */
                break;
            }
            if (resp->len + (size_t)chunk > MAX_RESPONSE) {
                gs_error = "Response too large";
                ret = GS_IO_ERROR;
                goto out;
            }
            if (resp->len + (size_t)chunk + 1 > cap) {
                while (cap < resp->len + (size_t)chunk + 1)
                    cap *= 2;
                if (cap > MAX_RESPONSE + 1)
                    cap = MAX_RESPONSE + 1;
                char *nb = realloc(resp->body, cap);
                if (!nb) {
                    ret = GS_OUT_OF_MEMORY;
                    goto out;
                }
                resp->body = nb;
            }
            if (read_exact(c, (unsigned char *)resp->body + resp->len, (size_t)chunk) != 0) {
                ret = GS_IO_ERROR;
                goto out;
            }
            resp->len += (size_t)chunk;
            if(read_line(c,line,sizeof(line))!=2 || strcmp(line,"\r\n")){ret=GS_IO_ERROR;goto out;}
        }
    } else if (content_length >= 0) {
        if (content_length > MAX_RESPONSE) {
            gs_error = "Response too large";
            ret = GS_IO_ERROR;
            goto out;
        }
        resp->body = malloc((size_t)content_length + 1);
        if (!resp->body) {
            ret = GS_OUT_OF_MEMORY;
            goto out;
        }
        if (read_exact(c, (unsigned char *)resp->body, (size_t)content_length) != 0) {
            gs_error = "Truncated HTTP body";
            ret = GS_IO_ERROR;
            goto out;
        }
        resp->len = (size_t)content_length;
    } else {
        // No length: read until close.
        size_t cap = 8192;
        poolable_after = 0; /* the connection is consumed */
        resp->body = malloc(cap);
        if (!resp->body) {
            ret = GS_OUT_OF_MEMORY;
            goto out;
        }
        for (;;) {
            if (resp->len + 2048 + 1 > cap) {
                cap *= 2;
                if (cap > MAX_RESPONSE) {
                    ret = GS_IO_ERROR;
                    goto out;
                }
                char *nb = realloc(resp->body, cap);
                if (!nb) {
                    ret = GS_OUT_OF_MEMORY;
                    goto out;
                }
                resp->body = nb;
            }
            int n = conn_read(c, (unsigned char *)resp->body + resp->len, 2048);
            if (n < 0) {
                ret = GS_IO_ERROR;
                goto out;
            }
            if (n <= 0)
                break;
            resp->len += (size_t)n;
        }
    }

    if (resp->body)
        resp->body[resp->len] = '\0';

    if (http_status != 200) {
        gs_error = "Server returned an HTTP error";
        ret = GS_FAILED;
        goto out;
    }

    if (s_verbose && resp->body)
        LOGI("http: response %d, %zu bytes", http_status, resp->len);

    ret = GS_OK;
    if (!(flags & HTTP_FLAG_FRESH) && keep_alive && poolable_after) {
        pool_checkin(c, from_pool);
        c = NULL; /* ownership passed to the pool */
    }

out:
    if (c)
        conn_close_free(c); /* error path / consumed / FRESH */
    return ret;
}

int http_get(const char *host, unsigned short port, int use_tls,
             const char *path, http_resp_t *resp) {
    return http_get_timeout(host, port, use_tls, path, resp, HTTP_FLAG_NONE,
                            default_timeout());
}

int http_get_ex(const char *host, unsigned short port, int use_tls,
                const char *path, http_resp_t *resp, unsigned flags) {
    return http_get_timeout(host, port, use_tls, path, resp, flags,
                            default_timeout());
}

int http_get_timeout(const char *host, unsigned short port, int use_tls,
                     const char *path, http_resp_t *resp, unsigned flags,
                     unsigned timeout_ms) {
    if (!timeout_ms)
        timeout_ms = HTTP_TIMEOUT_DEFAULT_MS;

    int used_pool = 0;
    int ret;

    if (s_verbose)
        LOGI("http: %s://%s:%u%.120s%s", use_tls ? "https" : "http", host, port,
             path, (flags & HTTP_FLAG_FRESH) ? " (fresh)" : "");

    ret = http_get_attempt(host, port, use_tls, path, resp, timeout_ms, flags,
                           &used_pool);
    if (ret != GS_OK && used_pool && !(flags & HTTP_FLAG_FRESH)) {
        /* The pooled connection went stale between requests: one retry on
         * a fresh connection. GETs are idempotent, so this is safe. */
        pool_strike();
        LOGI("http: pooled connection failed; retrying fresh");
        ret = http_get_attempt(host, port, use_tls, path, resp, timeout_ms,
                               flags, &used_pool);
    }
    return ret;
}
