// Minimal HTTP/HTTPS client over mbedTLS for the GameStream/Sunshine API.
// GET only.
//
// THREADING MODEL:
//
// The original design ran every request under ONE global mutex held
// across connect + TLS handshake + I/O, with the request timeout in a
// global variable. On real Orbis hardware that combination
// froze the app for 75 seconds behind a single optional request:
//
//   - fcntl(O_NONBLOCK) fails EACCES for every TCP socket,
//     ioctl(FIONBIO)/socket(SOCK_NONBLOCK) are unavailable there too
//     (the sweep fell to the blocking pool), so connects on Orbis
//     run in BLOCKING mode;
//   - SO_SNDTIMEO does NOT bound connect() on Orbis: a connect to a
//     firewall-dropped port blocks for the kernel's full SYN budget
//     (~75 s, errno 60) — observed on hardware against a filtered port;
//   - while that connect held the global mutex, the box-art GET and the
//     /launch GET queued behind it for 44 s, and a concurrent thread's
//     timeout reset clobbered the /launch request's 15 s budget to 7 s in flight
//     (http_set_timeout_ms is global state).
//
// The fix removes the shared choke points instead of trying to bound the
// unboundable:
//
//   1. s_lock guards ONLY pool bookkeeping (checkout/checkin/strikes).
//      It is never held across connect, handshake or I/O.
//   2. s_tls_lock serializes TLS handshakes only (the identity's CTR_DRBG
//      and RSA key are shared and not concurrency-safe). Handshakes only
//      ever run against a host that has ACCEPTED the TCP connection, so
//      the hold time is the (short) handshake itself, bounded by the
//      request's read timeout in the pathological case.
//   3. The timeout is a per-request PARAMETER (http_get_timeout()).
//      http_get()/http_get_ex() use the process default. No global
//      mutation remains; concurrent requests can no longer clobber each
//      other's budgets (one request must never eat another's budget).
//   4. Connections are heap objects owned by exactly one in-flight
//      request. The pool is a single slot holding a pointer; checkout
//      moves the pointer out, checkin moves it back in. mbedtls contexts
//      contain self-referential pointers (set_bio), so the structs are
//      never copied or moved in memory — only the pointer changes hands.
//   5. TLS session resumption is REMOVED: hardware logs
//      proved Sunshine never answers the resumed ClientHellos on
//      the /launch path (15 s handshake timeouts, attempt after attempt)
//      while full handshakes complete in ~300 ms every time. Every
//      connection now pays one full handshake (~300 ms on LAN), which
//      is a price the launch flow is more than happy to pay.
//   6. A request to one host:port can still block ~75 s in connect on
//      Orbis (kernel limitation, host off / firewall drop) — but it now
//      blocks ONLY its own thread. Callers that must stay responsive run
//      such requests on worker threads (subnet scan,
//      art worker) and none of them can delay another request anymore.
//
// Keep-alive pooling, stale-pool retry-once, pre-flight detection of
// server-closed connections and the 3-strike keep-alive ban are carried
// over from the earlier single-lock design. TLS session resumption is
// gone (see point 5).
#pragma once

#include <stddef.h>
#include "certgen.h"

typedef struct {
    char *body;
    size_t len;
} http_resp_t;

// Identity is used as the client certificate for HTTPS.
void http_init(client_identity_t *id, int verbose);

enum {
    HTTP_FLAG_NONE = 0,
    // Bypass the pool for this request: open a dedicated connection and
    // never auto-retry. Use for state-changing endpoints (launch/resume/
    // cancel): a write to a stale pooled socket can be delivered to the
    // server after the client has already given up, and a blind retry
    // would then hit the server twice (duplicate launch).
    HTTP_FLAG_FRESH = 1u << 0,
};

int http_get(const char *host, unsigned short port, int use_tls,
             const char *path, http_resp_t *resp);

// Same as http_get() with flag control (see HTTP_FLAG_*).
int http_get_ex(const char *host, unsigned short port, int use_tls,
                const char *path, http_resp_t *resp, unsigned flags);

// Same as http_get_ex(), with a per-request timeout in ms. This is the
// preferred entry point for anything running on a worker thread next to
// other traffic: the budget belongs to the request, not the process.
// Pass 0 for the process default. Pairing passes ~120000.
int http_get_timeout(const char *host, unsigned short port, int use_tls,
                     const char *path, http_resp_t *resp, unsigned flags,
                     unsigned timeout_ms);

void http_resp_free(http_resp_t *resp);

#include <pthread.h>
void http_set_timeout_ms(unsigned ms); /* legacy, thread-specific default */
void http_cancel_thread(pthread_t thread);
void http_cancel_pending(void);
