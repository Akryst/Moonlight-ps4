#pragma once
#include <stddef.h>
#include <stdint.h>

/* Host list shared by the scan and the menu. The mdns_ prefix is kept
 * only to avoid churning every call site. `online` is the probe verdict:
 * 1 answering, 0 unreachable, -1 unknown/not checked yet. */
#define MDNS_MAX_HOSTS 16

typedef struct {
    char instance[256];
    char target[256];
    char address[16];
    unsigned short port;
    int online;
} mdns_host_t;

int mdns_valid_ipv4(const char *address);

/* ============================================================
 * Wake-on-LAN.
 *
 * A PS4 cannot reach a powered-off PC over TCP (no probe answers),
 * but a UDP "magic packet" broadcast still reaches its NIC, which
 * can power the machine on (BIOS/Windows "Wake on Magic Packet"
 * must be enabled). The MAC has to be known in advance — it is
 * entered once in Settings (the PCs tab then offers SQUARE = wake,
 * and a failed connect auto-sends a burst).
 * ============================================================ */

/* Parse "AA:BB:CC:DD:EE:FF" / "AA-BB-CC-DD-EE-FF" / "AABBCCDDEEFF"
 * (case-insensitive, separators optional, exactly 12 hex digits).
 * Returns 0 and fills out[6] on success. */
int wol_mac_parse(const char *s, uint8_t out[6]);

/* Format 6 bytes back to "AA:BB:CC:DD:EE:FF" (out must hold >= 18). */
void wol_mac_format(const uint8_t mac[6], char *out, size_t cap);

/* Build the 102-byte magic packet into out (6x 0xFF + 16x MAC). Host
 * unit tests call this directly. */
void wol_build_packet(const uint8_t mac[6], uint8_t out[102]);

/* Send the 102-byte magic packet (6x 0xFF + 16x MAC) as a short burst:
 * broadcast 255.255.255.255:9 and :7, the target subnet's broadcast
 * (derived from target_ip, assumed /24) :9, and a unicast to the host
 * itself :9 — repeated twice, 100 ms apart. Pure UDP sendto() calls,
 * so this never blocks on a console whose TCP connects can stall.
 * mac_str is any form wol_mac_parse accepts; target_ip may be NULL
 * (then only the global broadcasts go out). Returns packets sent. */
int wol_send(const char *mac_str, const char *target_ip);

/* ============================================================
 * Scan architecture — honest about what the PS4 can do.
 *
 * Every subnet-wide discovery primitive does not work on
 * this console (verified across three designs on real hardware):
 *  - mDNS multicast: never received on this console;
 *  - unicast mDNS queries to every /24 address: ZERO responders in
 *    every hardware run (Sunshine's responder does not answer unicast
 *    queries on the user's LAN / Windows firewall eats them);
 *  - TCP sweeps of any shape: fcntl/ioctl/SOCK_NONBLOCK all fail on
 *    Orbis, so every connect to an ABSENT address blocks for the
 *    kernel SYN budget (~75 s). A /24 is structurally ~40 minutes.
 *
 * What works every time: connecting to a KNOWN address. The PCs list
 * is seeded instantly from the saved config (cfg->host) and the scan
 * worker just refines it: one plain-HTTP /serverinfo probe that
 * returns in milliseconds when the PC is on (and extracts the PC's
 * hostname for the list entry). The very first run uses manual IP
 * entry — after that the PC is remembered and always listed.
 * ============================================================ */

/* One-shot synchronous scan (hint host only). Used by the CLI/host
 * tests; the PS4 menu uses the incremental scan_job API below so the
 * UI stays interactive while the probe runs. Returns the total found,
 * or -1 when there is no hint to probe. */
int subnet_discover(mdns_host_t *hosts, int count, int capacity);

/* Incremental scan for the menu (runs on a worker thread).
 *
 *   scan_job_begin(hint_host)  spawn the worker (hint = last host, may
 *                              be NULL/empty)
 *   scan_job_poll(...)         snapshot the current results + status
 *   scan_job_done(...)         1 once the scan has finished
 *   scan_job_finish(&job)      consume or abandon; the worker frees a
 *                              still-running job by itself
 */
typedef struct scan_job scan_job_t;

scan_job_t *scan_job_begin(const char *hint_host);
int scan_job_poll(scan_job_t *j, mdns_host_t *out_hosts, int cap,
                  char *phase_text, size_t phase_cap);
int scan_job_done(scan_job_t *j);
void scan_job_finish(scan_job_t **j);

int mdns_discover(mdns_host_t *hosts,int capacity,int timeout_ms);

#define MDNS_MAX_ADDRS 32
#define MDNS_SERVICE "_nvstream._tcp.local"
typedef struct {
    char name[256];
    char address[16];
} mdns_address_t;

typedef struct {
    mdns_host_t hosts[MDNS_MAX_HOSTS];
    mdns_address_t addresses[MDNS_MAX_ADDRS];
    int count, address_count;
} mdns_cache_t;

/* Bounded parser: keeps records across packets; never connects to a host. */
int mdns_parse(mdns_cache_t *cache, const uint8_t *packet, size_t length);
