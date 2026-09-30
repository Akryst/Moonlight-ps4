#include "discovery.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifndef MDNS_PARSER_ONLY
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#endif

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] * 256u + p[1]; }
static unsigned u32(const uint8_t *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | p[3];
}

/* DNS compression may reference anywhere in the packet. Limit jumps and the
 * expanded name size to reject loops, malformed pointers and long names. */
static int name_read(const uint8_t *p, size_t n, size_t *cursor,
                     char *out, size_t cap) {
    size_t pos = *cursor, end = 0, used = 0;
    int jumped = 0;
    for (int steps = 0; steps < 128; steps++) {
        if (pos >= n) return -1;
        unsigned len = p[pos++];
        if ((len & 0xc0) == 0xc0) {
            if (pos >= n) return -1;
            size_t next = ((len & 0x3f) << 8) | p[pos++];
            if (next >= n) return -1;
            if (!jumped) end = pos;
            jumped = 1;
            pos = next;
            continue;
        }
        if (len & 0xc0) return -1;
        if (!len) {
            if (used >= cap) return -1;
            out[used] = 0;
            *cursor = jumped ? end : pos;
            return 0;
        }
        if (pos + len > n || used + len + (used ? 1u : 0u) >= cap) return -1;
        if (used) out[used++] = '.';
        for (unsigned i = 0; i < len; i++) {
            /* Embedded NUL would make comparisons ambiguous. */
            if (!p[pos + i]) return -1;
            out[used++] = (char)p[pos + i];
        }
        pos += len;
    }
    return -1;
}

static int is_instance(const char *name) {
    size_t a = strlen(name), b = strlen(MDNS_SERVICE);
    return a > b + 1 && name[a - b - 1] == '.' &&
           !strcasecmp(name + a - b, MDNS_SERVICE);
}

static mdns_host_t *host_get(mdns_cache_t *c, const char *name) {
    for (int i = 0; i < c->count; i++)
        if (!strcasecmp(c->hosts[i].instance, name)) return &c->hosts[i];
    if (c->count >= MDNS_MAX_HOSTS) return NULL;
    mdns_host_t *h = &c->hosts[c->count++];
    memset(h, 0, sizeof(*h));
    snprintf(h->instance, sizeof(h->instance), "%s", name);
    return h;
}

int mdns_parse(mdns_cache_t *c, const uint8_t *p, size_t n) {
    if (!c || !p || n < 12 || !(p[2] & 0x80) || (p[3] & 15)) return -1;
    unsigned qd = u16(p + 4), rr = u16(p + 6) + u16(p + 8) + u16(p + 10);
    if (qd > 64 || rr > 256) return -1;
    size_t pos = 12;
    char name[256], value[256];
    for (unsigned i = 0; i < qd; i++) {
        if (name_read(p, n, &pos, name, sizeof(name)) || pos + 4 > n) return -1;
        pos += 4;
    }
    for (unsigned i = 0; i < rr; i++) {
        if (name_read(p, n, &pos, name, sizeof(name)) || pos + 10 > n) return -1;
        unsigned type = u16(p + pos), cls = u16(p + pos + 2) & 0x7fff;
        unsigned ttl = u32(p + pos + 4), len = u16(p + pos + 8);
        pos += 10;
        size_t end = pos + len, rpos = pos;
        if (end > n) return -1;
        if (cls != 1 || !ttl) { pos = end; continue; }
        if (type == 12 && !strcasecmp(name, MDNS_SERVICE)) {
            if (name_read(p, n, &rpos, value, sizeof(value)) || rpos != end) return -1;
            if (is_instance(value)) (void)host_get(c, value);
        } else if (type == 33 && is_instance(name)) {
            if (len < 7) return -1;
            unsigned port = u16(p + pos + 4);
            rpos += 6;
            if (name_read(p, n, &rpos, value, sizeof(value)) || rpos != end) return -1;
            mdns_host_t *h = host_get(c, name);
            if (h && port) {
                snprintf(h->target, sizeof(h->target), "%s", value);
                h->port = (unsigned short)port;
            }
        } else if (type == 1 && len == 4) {
            int slot = -1;
            for (int j = 0; j < c->address_count; j++)
                if (!strcasecmp(c->addresses[j].name, name)) { slot = j; break; }
            if (slot < 0 && c->address_count < MDNS_MAX_ADDRS) slot = c->address_count++;
            if (slot >= 0 && p[pos] && p[pos] < 224) {
                mdns_address_t *a = &c->addresses[slot];
                snprintf(a->name, sizeof(a->name), "%s", name);
                snprintf(a->address, sizeof(a->address), "%u.%u.%u.%u",
                         p[pos], p[pos+1], p[pos+2], p[pos+3]);
            }
        }
        pos = end;
    }
    for (int i = 0; i < c->count; i++) {
        c->hosts[i].address[0] = 0;
        for (int j = 0; j < c->address_count; j++)
            if (!strcasecmp(c->hosts[i].target, c->addresses[j].name)) {
                snprintf(c->hosts[i].address, sizeof(c->hosts[i].address),
                         "%s", c->addresses[j].address);
                break;
            }
    }
    return 0;
}

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

#ifndef MDNS_PARSER_ONLY
static uint64_t time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (unsigned)tv.tv_usec / 1000;
}

static int query(int fd, const struct sockaddr_in *dest, const char *name, unsigned type) {
    uint8_t p[512] = {0x4d,0x4c,0,0,0,1,0,0,0,0,0,0};
    size_t pos = 12;
    while (*name) {
        const char *dot = strchr(name, '.');
        size_t len = dot ? (size_t)(dot - name) : strlen(name);
        if (!len || len > 63 || pos + len + 6 >= sizeof(p)) return -1;
        p[pos++] = (uint8_t)len;
        memcpy(p + pos, name, len);
        pos += len;
        if (!dot) break;
        name = dot + 1;
    }
    p[pos++] = 0;
    p[pos++] = (uint8_t)(type >> 8); p[pos++] = (uint8_t)type;
    p[pos++] = 0; p[pos++] = 1;
    return sendto(fd, p, pos, 0, (const struct sockaddr *)dest, sizeof(*dest)) < 0 ? -1 : 0;
}

int mdns_discover(mdns_host_t *hosts, int capacity, int timeout_ms) {
    if (!hosts || capacity <= 0 || timeout_ms <= 0 || timeout_ms > 10000) return -1;
    /* RFC 6762 legacy queries from an ephemeral source port receive unicast
     * replies. This avoids competing for port 5353 or needing a multicast
     * membership on Orbis. It discovers IPv4 hosts on the same LAN. */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (fd >= FD_SETSIZE) { close(fd); return -1; }
    struct sockaddr_in local, dest;
    memset(&local, 0, sizeof(local)); memset(&dest, 0, sizeof(dest));
#ifdef __ORBIS__
    local.sin_len = sizeof(local); dest.sin_len = sizeof(dest);
#endif
    local.sin_family = dest.sin_family = AF_INET;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) { close(fd); return -1; }
    dest.sin_port = htons(5353);
    inet_pton(AF_INET, "224.0.0.251", &dest.sin_addr);
    int ttl = 255;
    (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    mdns_cache_t cache;
    memset(&cache, 0, sizeof(cache));
    uint64_t start = time_ms(), next_query = start;
    uint8_t packet[4096];
    int sent = 0;
    while (time_ms() - start < (unsigned)timeout_ms) {
        if (time_ms() >= next_query) {
            if (!query(fd, &dest, MDNS_SERVICE, 12)) sent = 1;
            for (int i = 0; i < cache.count; i++) {
                if (!cache.hosts[i].target[0]) query(fd, &dest, cache.hosts[i].instance, 33);
                else if (!cache.hosts[i].address[0]) query(fd, &dest, cache.hosts[i].target, 1);
            }
            next_query = time_ms() + 500;
        }
        fd_set read_set; FD_ZERO(&read_set); FD_SET(fd, &read_set);
        struct timeval wait = {0, 100000};
        int ready = select(fd + 1, &read_set, NULL, NULL, &wait);
        if (ready < 0) { if (errno == EINTR) continue; close(fd); return -1; }
        if (ready > 0) {
            int n = (int)recvfrom(fd, packet, sizeof(packet), 0, NULL, NULL);
            if (n > 0) (void)mdns_parse(&cache, packet, (size_t)n);
        }
    }
    close(fd);
    if (!sent) return -1;
    int count = 0;
    for (int i = 0; i < cache.count && count < capacity; i++) {
        mdns_host_t *h = &cache.hosts[i];
        if (!h->port || !mdns_valid_ipv4(h->address)) continue;
        int duplicate = 0;
        for (int j = 0; j < count; j++)
            if (hosts[j].port == h->port && !strcmp(hosts[j].address, h->address)) duplicate = 1;
        if (!duplicate) hosts[count++] = *h;
    }
    return count;
}
#endif
