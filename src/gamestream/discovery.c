#include "discovery.h"
#include "../log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

/* OpenOrbis's <sys/ioctl.h> ships only LINUX_FIONBIO; the Orbis kernel
 * is FreeBSD-derived and BSD's socket FIONBIO id differs. Neither id is
 * proven for Orbis TCP sockets (hardware logs show the
 * old sweep falling through both), so probe_host's helper tries FIONBIO
 * (BSD id on Orbis), then LINUX_FIONBIO, then fcntl — a wrong ioctl id
 * fails cleanly (ENOTTY) and the caller falls back. */

#ifndef FIONBIO
#ifdef __ORBIS__
#define FIONBIO 0x8004667e /* FreeBSD _IOW('f', 126, int) */
#elif defined(LINUX_FIONBIO)
#define FIONBIO LINUX_FIONBIO
#endif
#endif

int mdns_valid_ipv4(const char *s) {
    if (!s || !*s) return 0;
    for (int i = 0; i < 4; i++) {
        unsigned v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (unsigned)(*s++ - '0');
            if (++digits > 3 || v > 255) return 0;
        }
        if (!digits || (i == 0 && (!v || v >= 224))) return 0;
        if (i < 3) { if (*s++ != '.') return 0; }
        else if (*s) return 0;
    }
    return 1;
}

/* --- Wake-on-LAN --------------------------------------------- */

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int wol_mac_parse(const char *s, uint8_t out[6]) {
    if (!s)
        return -1;
    uint8_t bytes[6];
    int n = 0;
    int hi = -1; /* half-assembled nibble */
    for (const char *p = s; *p; p++) {
        if (*p == ':' || *p == '-' || *p == '.' || *p == ' ')
            continue;
        int d = hex_digit(*p);
        if (d < 0 || n >= 6)
            return -1;
        if (hi < 0)
            hi = d;
        else {
            bytes[n++] = (uint8_t)((hi << 4) | d);
            hi = -1;
        }
    }
    if (hi >= 0 || n != 6)
        return -1;
    /* An all-zero MAC is never valid. */
    if (!bytes[0] && !bytes[1] && !bytes[2] &&
        !bytes[3] && !bytes[4] && !bytes[5])
        return -1;
    memcpy(out, bytes, 6);
    return 0;
}

void wol_mac_format(const uint8_t mac[6], char *out, size_t cap) {
    snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Build the 102-byte magic packet: 6x 0xFF followed by the target MAC
 * repeated 16 times. Exposed for the host unit test. */
void wol_build_packet(const uint8_t mac[6], uint8_t out[102]) {
    memset(out, 0xFF, 6);
    for (int i = 0; i < 16; i++)
        memcpy(out + 6 + i * 6, mac, 6);
}

int wol_send(const char *mac_str, const char *target_ip) {
    uint8_t mac[6];
    if (wol_mac_parse(mac_str, mac) != 0) {
        LOGW("wol: invalid MAC '%s'", mac_str ? mac_str : "(null)");
        return 0;
    }

    uint8_t pkt[102];
    wol_build_packet(mac, pkt);

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        LOGE("wol: socket failed errno=%d", errno);
        return 0;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

    /* Destinations: global broadcast :9 and :7; the /24 broadcast of the
     * saved host (some routers drop global broadcasts); a direct unicast
     * (works when the router still holds the ARP entry). */
    struct sockaddr_in dst[4];
    char sub[16] = {0};
    int ndst = 0;
    memset(dst, 0, sizeof(dst));
    const char *addrs[4] = { "255.255.255.255", "255.255.255.255", NULL, NULL };
    const unsigned short ports[4] = { 9, 7, 9, 9 };
    if (target_ip && mdns_valid_ipv4(target_ip)) {
        snprintf(sub, sizeof(sub), "%s", target_ip);
        char *dot = strrchr(sub, '.');
        if (dot)
            snprintf(dot + 1, sizeof(sub) - (size_t)(dot + 1 - sub), "255");
        addrs[2] = sub;
        addrs[3] = target_ip;
        ndst = 4;
    } else {
        ndst = 2;
    }
    for (int i = 0; i < ndst; i++) {
#ifdef __ORBIS__
        dst[i].sin_len = sizeof(dst[i]);
#endif
        dst[i].sin_family = AF_INET;
        dst[i].sin_port = htons(ports[i]);
        inet_pton(AF_INET, addrs[i], &dst[i].sin_addr);
    }

    int sent = 0;
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < ndst; i++) {
            if (sendto(fd, pkt, sizeof(pkt), 0,
                       (struct sockaddr *)&dst[i], sizeof(dst[i])) > 0)
                sent++;
        }
        if (round == 0)
            usleep(100000); /* small gap so flaky NICs see both rounds */
    }
    close(fd);

    char mac_txt[18];
    wol_mac_format(mac, mac_txt, sizeof(mac_txt));
    LOGI("wol: %d magic packets for %s (target %s)", sent, mac_txt,
         target_ip ? target_ip : "broadcast");
    return sent;
}

/* --- shared scan state ----------------------------------------------
 *
 * The worker probes the saved host and publishes the result through
 * scan_add(), which locks, dedupes and publishes. The menu's worker
 * runs the probe; the UI thread only ever calls scan_job_poll(), which
 * takes the same mutex and copies a snapshot out. */
typedef struct {
    mdns_host_t hosts[MDNS_MAX_HOSTS];
    int total;
    char phase[48];
} scan_results_t;

static void scan_set_phase(scan_results_t *r, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

struct scan_job {
    pthread_mutex_t mtx;
    int done;
    int abandoned;
    scan_results_t res;
    char hint_host[64];
};

static void scan_add(scan_results_t *r, const char *ip, unsigned short port,
                     const char *name, int online, int *slots) {
    if (r->total >= MDNS_MAX_HOSTS)
        return;
    for (int i = 0; i < r->total; i++)
        if (!strcmp(r->hosts[i].address, ip)) {
            /* Refresh an already-listed host in place (name/online). */
            if (name && name[0])
                snprintf(r->hosts[i].instance, sizeof(r->hosts[i].instance),
                         "%s", name);
            r->hosts[i].online = online;
            return;
        }
    mdns_host_t *h = &r->hosts[r->total];
    memset(h, 0, sizeof(*h));
    snprintf(h->instance, sizeof(h->instance), "%s",
             (name && name[0]) ? name : "Gaming PC");
    snprintf(h->address, sizeof(h->address), "%s", ip);
    h->port = port;
    h->online = online;
    r->total++;
    LOGI("scan: found %s:%u (%s)", h->address, (unsigned)h->port,
         r->phase);
    if (slots)
        (*slots)++;
}

static void scan_set_phase(scan_results_t *r, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->phase, sizeof(r->phase), fmt, ap);
    va_end(ap);
}

/* --- TCP confirm probe ----------------------------------------------
 *
 * Confirm the saved PC: /serverinfo over plain HTTP on 47989 must answer
 * HTTP 200 with an appversion field (every GameStream/Sunshine host
 * sends it), and the reply's <hostname> (Sunshine includes it) becomes
 * the display name. Connects to a LIVE host return in milliseconds; only
 * a dead or filtered target blocks (kernel SYN budget on Orbis), which
 * is why this runs on the scan worker, never on the UI thread. */
#define SWEEP_CONNECT_MS 350
#define SWEEP_PROBE_READ_MS 900

static int sweep_set_nonblocking(int fd, long *fl_out) {
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

static unsigned long long sweep_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ull + tv.tv_usec / 1000ull;
}

static int probe_host(const char *ip, char *name_out, size_t name_cap) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return 0;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
#ifdef __ORBIS__
    dst.sin_len = sizeof(dst);
#endif
    dst.sin_family = AF_INET;
    dst.sin_port = htons(47989);
    inet_pton(AF_INET, ip, &dst.sin_addr);

    /* Bounding attempt (honored on Linux; ignored by Orbis for connect,
     * see discovery.h — the caller accepts the blocking consequence). */
    {
        struct timeval tv;
        tv.tv_sec = (time_t)(SWEEP_CONNECT_MS / 1000u);
        tv.tv_usec = (suseconds_t)((SWEEP_CONNECT_MS % 1000u) * 1000u);
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    long nb_fl = -1;
    int nb = sweep_set_nonblocking(fd, &nb_fl);
    if (nb != 0) {
        int rc = connect(fd, (struct sockaddr *)&dst, sizeof(dst));
        if (rc != 0 && errno != EISCONN) {
            int e = errno;
            int hard = e == ECONNREFUSED || e == EHOSTUNREACH ||
                       e == ENETUNREACH || e == ENETDOWN ||
                       e == EADDRNOTAVAIL || e == EAFNOSUPPORT;
            /* Unrecognized errno values (Orbis may not translate
             * in-progress codes the way the headers predict): do NOT
             * give up here - let poll + SO_ERROR decide. */
            if (hard) {
                close(fd);
                return 0;
            }
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            if (poll(&pfd, 1, SWEEP_CONNECT_MS) <= 0) {
                close(fd);
                return 0;
            }
            if (!(pfd.revents & (POLLOUT | POLLERR | POLLHUP))) {
                close(fd);
                return 0;
            }
            int soerr = 0;
            socklen_t sol = sizeof(soerr);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sol) == 0 &&
                soerr != 0 && soerr != EISCONN) {
                close(fd);
                return 0;
            }
            /* getsockopt failing: assume connected, the write decides. */
        }
        if (nb == 1)
            (void)fcntl(fd, F_SETFL, nb_fl); /* blocking from here */
        else {
            int zero = 0;
            (void)ioctl(fd, FIONBIO, &zero);
#ifdef LINUX_FIONBIO
            (void)ioctl(fd, LINUX_FIONBIO, &zero);
#endif
        }
    } else {
        /* No non-blocking available (an earlier revision dropped the candidate here,
         * which on PS4 hardware killed every probe): blocking connect.
         * Fast for live hosts; kernel-bounded otherwise. */
        if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
            close(fd);
            return 0;
        }
    }

    char req[128];
    int len = snprintf(req, sizeof(req),
                       "GET /serverinfo?uniqueid=0123456789ABCDEF HTTP/1.0\r\n"
                       "Host: %s\r\n\r\n", ip);
    if (send(fd, req, (size_t)len, 0) != len) {
        close(fd);
        return 0;
    }

    char buf[2048];
    size_t got = 0;
    unsigned long long deadline = sweep_now_ms() + SWEEP_PROBE_READ_MS;
    for (;;) {
        unsigned long long now = sweep_now_ms();
        if (now >= deadline)
            break;
        int left = (int)(deadline - now);
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, left) <= 0)
            break;
        if (!(pfd.revents & (POLLIN | POLLERR | POLLHUP)))
            break;
        int n = (int)recv(fd, buf + got, sizeof(buf) - got - 1, 0);
        if (n <= 0)
            break;
        got += (size_t)n;
        buf[got] = '\0';
        if (got >= sizeof(buf) - 1)
            break;
    }
    close(fd);
    if (got < 32)
        return 0;
    if (!(strstr(buf, "appversion") && strstr(buf, " 200")))
        return 0;
    /* Best-effort display name from the reply (Sunshine includes
     * <hostname> in /serverinfo; GFE does too). Callers pass NULL to
     * skip. Any XML-unsafe character is simply kept — the UI only ever
     * prints the string through its label fitter. */
    if (name_out && name_cap) {
        name_out[0] = '\0';
        const char *hs = strstr(buf, "<hostname>");
        if (hs) {
            hs += strlen("<hostname>");
            while (*hs == ' ' || *hs == '\t')
                hs++;
            const char *he = strchr(hs, '<');
            if (he && he > hs) {
                size_t n = (size_t)(he - hs);
                if (n >= name_cap)
                    n = name_cap - 1;
                memcpy(name_out, hs, n);
                name_out[n] = '\0';
            }
        }
    }
    return 1;
}

/* --- the scan thread ----------------------------------------------
 *
 * hint-only. The earlier candidate machinery (unicast mDNS sweep
 * of the /24 + reduced deep TCP tail) never produced a single host
 * across several hardware rounds — Sunshine's mDNS does not answer unicast queries
 * on the user's LAN, and a blocking TCP sweep cannot work on a console
 * where every connect against an absent address costs the kernel SYN
 * budget (measured: 8 workers, 75.013 s, total=0). What DOES
 * work, every time, is probing the SAVED host: the connect to a live
 * PC returns in milliseconds. The menu seeds the PCs tab from the
 * config instantly; this worker only refines the entry (online status
 * + the PC's hostname). Manual IP entry (Settings) covers the very
 * first run. */
static void scan_run_phases(scan_job_t *j) {
    scan_results_t *r = &j->res;

    if (!j->hint_host[0] || !mdns_valid_ipv4(j->hint_host)) {
        scan_set_phase(r,"Searching the local network...");
        mdns_host_t found[MDNS_MAX_HOSTS];int count=mdns_discover(found,MDNS_MAX_HOSTS,1800);
        pthread_mutex_lock(&j->mtx);
        for(int i=0;i<count;i++)scan_add(r,found[i].address,found[i].port,found[i].instance,1,NULL);
        pthread_mutex_unlock(&j->mtx);return;
    }

    scan_set_phase(r, "Checking %s", j->hint_host);
    char name[64] = {0};
    if (probe_host(j->hint_host, name, sizeof(name))) {
        pthread_mutex_lock(&j->mtx);
        scan_add(r, j->hint_host, 47989, name, 1, NULL);
        pthread_mutex_unlock(&j->mtx);
    } else {
        /* Keep the entry (the UI seeded it) but mark it unreachable. */
        pthread_mutex_lock(&j->mtx);
        scan_add(r, j->hint_host, 47989, NULL, 0, NULL);
        pthread_mutex_unlock(&j->mtx);
        LOGI("scan: %s not answering", j->hint_host);
    }
}

static void *scan_job_thread(void *arg) {
    scan_job_t *j = arg;
    scan_run_phases(j);
    pthread_mutex_lock(&j->mtx);
    j->done = 1;
    scan_set_phase(&j->res, "Done (%d found)", j->res.total);
    int free_it = j->abandoned;
    pthread_mutex_unlock(&j->mtx);
    if (free_it) {
        pthread_mutex_destroy(&j->mtx);
        free(j);
    }
    return NULL;
}

scan_job_t *scan_job_begin(const char *hint_host) {
    scan_job_t *j = calloc(1, sizeof(*j));
    if (!j)
        return NULL;
    pthread_mutex_init(&j->mtx, NULL);
    if (hint_host && hint_host[0] && mdns_valid_ipv4(hint_host))
        snprintf(j->hint_host, sizeof(j->hint_host), "%s", hint_host);
    snprintf(j->res.phase, sizeof(j->res.phase), "Starting scan");
    pthread_t th;
    if (pthread_create(&th, NULL, scan_job_thread, j) == 0) {
        pthread_detach(th);
        return j;
    }
    pthread_mutex_destroy(&j->mtx);
    free(j);
    return NULL;
}

int scan_job_poll(scan_job_t *j, mdns_host_t *out_hosts, int cap,
                  char *phase_text, size_t phase_cap) {
    if (!j)
        return 0;
    pthread_mutex_lock(&j->mtx);
    int n = j->res.total;
    if (n > cap)
        n = cap;
    for (int i = 0; i < n; i++)
        out_hosts[i] = j->res.hosts[i];
    if (phase_text && phase_cap)
        snprintf(phase_text, phase_cap, "%s", j->res.phase);
    pthread_mutex_unlock(&j->mtx);
    return n;
}

int scan_job_done(scan_job_t *j) {
    if (!j)
        return 1;
    pthread_mutex_lock(&j->mtx);
    int d = j->done;
    pthread_mutex_unlock(&j->mtx);
    return d;
}

void scan_job_finish(scan_job_t **jp) {
    if (!jp || !*jp)
        return;
    scan_job_t *j = *jp;
    *jp = NULL;
    pthread_mutex_lock(&j->mtx);
    if (j->done) {
        pthread_mutex_unlock(&j->mtx);
        pthread_mutex_destroy(&j->mtx);
        free(j);
        return;
    }
    j->abandoned = 1; /* worker frees when it lands */
    pthread_mutex_unlock(&j->mtx);
}

/* --- one-shot synchronous scan (CLI / host tests) ---------------------- */

int subnet_discover(mdns_host_t *hosts, int count, int capacity) {
    scan_job_t *j = calloc(1, sizeof(*j));
    if (!j)
        return count;
    pthread_mutex_init(&j->mtx, NULL);
    scan_run_phases(j);
    int total = j->res.total;
    int room = capacity - count;
    int n = total > room ? room : total;
    if (n > 0)
        memcpy(hosts + count, j->res.hosts, sizeof(mdns_host_t) * (size_t)n);
    pthread_mutex_destroy(&j->mtx);
    free(j);
    return count + (n > 0 ? n : 0);
}
