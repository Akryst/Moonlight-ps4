// mbedTLS port of the libgamestream logic (moonlight-embedded, GPLv3).

#define MBEDTLS_ALLOW_PRIVATE_ACCESS // access to cert.sig (certificate signature)

#include "client.h"
#include "gs_http.h"
#include "gs_errors.h"
#include "discovery.h" /* wol_mac_parse / wol_mac_format (mac normalizing) */
#include "../log.h"

#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/pk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define SIGNATURE_LEN 256
#define MAX_PLAINCERT 8192

// ---------------------------------------------------------------------------
// Cryptographic utilities
// ---------------------------------------------------------------------------

static void hash_data(int use_sha256, const unsigned char *in, size_t len,
                      unsigned char *out /*32 bytes*/) {
    memset(out, 0, 32);
    if (use_sha256)
        mbedtls_sha256(in, len, out, 0);
    else
        mbedtls_sha1(in, len, out);
}

// AES-128-ECB without padding over multiples of 16 bytes.
static void aes_ecb(int encrypt, const unsigned char *key16,
                    const unsigned char *in, size_t len, unsigned char *out) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (encrypt)
        mbedtls_aes_setkey_enc(&aes, key16, 128);
    else
        mbedtls_aes_setkey_dec(&aes, key16, 128);
    for (size_t off = 0; off + 16 <= len; off += 16)
        mbedtls_aes_crypt_ecb(&aes, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                              in + off, out + off);
    mbedtls_aes_free(&aes);
}

// RSA PKCS#1 v1.5 SHA-256 signature of the message.
static int sign_message(client_identity_t *id, const unsigned char *msg, size_t mlen,
                        unsigned char *sig, size_t sig_size, size_t *slen) {
    unsigned char hash[32];
    mbedtls_sha256(msg, mlen, hash, 0);
    pthread_mutex_lock(&identity_crypto_mutex);
    int ret = mbedtls_pk_sign(&id->key, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                              sig, sig_size, slen, mbedtls_ctr_drbg_random, &id->rng);
    pthread_mutex_unlock(&identity_crypto_mutex);
    return ret == 0 ? GS_OK : GS_FAILED;
}

// Verify a SHA256-RSA signature with the server's PEM certificate.
static bool verify_signature(const unsigned char *data, size_t datalen,
                             const unsigned char *sig, size_t siglen,
                             const char *cert_pem) {
    mbedtls_x509_crt crt;
    unsigned char hash[32];
    bool ok = false;

    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse(&crt, (const unsigned char *)cert_pem,
                               strlen(cert_pem) + 1) != 0)
        goto out;

    mbedtls_sha256(data, datalen, hash, 0);
    ok = mbedtls_pk_verify(&crt.pk, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                           sig, siglen) == 0;
out:
    mbedtls_x509_crt_free(&crt);
    return ok;
}

// ---------------------------------------------------------------------------
// API requests
// ---------------------------------------------------------------------------

// GET to the API; path is built with printf format.
__attribute__((format(printf, 4, 5)))
static int api_get(gs_server_t *server, int https, http_resp_t *resp,
                   const char *fmt, ...) {
    char path[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(path, sizeof(path), fmt, ap);
    va_end(ap);
    if (n <= 0 || n >= (int)sizeof(path)) {
        gs_error = "Request too long";
        return GS_INVALID;
    }
    return http_get(server->address, https ? server->httpsPort : server->httpPort,
                    https, path, resp);
}

/* Same, with an explicit per-request timeout (the timeout is a
 * request parameter now — global state could be clobbered by concurrent
 * threads mid-request, one request resetting
 * another's budget while it queued was observed on hardware). */
__attribute__((format(printf, 5, 6)))
static int api_get_t(gs_server_t *server, int https, http_resp_t *resp,
                     unsigned timeout_ms, const char *fmt, ...) {
    char path[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(path, sizeof(path), fmt, ap);
    va_end(ap);
    if (n <= 0 || n >= (int)sizeof(path)) {
        gs_error = "Request too long";
        return GS_INVALID;
    }
    return http_get_timeout(server->address,
                            https ? server->httpsPort : server->httpPort,
                            https, path, resp, HTTP_FLAG_NONE, timeout_ms);
}

/* Same, but on a dedicated (non-pooled) connection. For state-changing
 * endpoints (launch/resume/cancel): never auto-retried by the HTTP layer,
 * and never written onto a possibly half-closed pooled socket where the
 * request could be silently delivered while the client treats it as
 * failed ("streaming failed although the game started on the host"). */
__attribute__((format(printf, 5, 6)))
static int api_get_fresh(gs_server_t *server, int https, http_resp_t *resp,
                         unsigned timeout_ms, const char *fmt, ...) {
    char path[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(path, sizeof(path), fmt, ap);
    va_end(ap);
    if (n <= 0 || n >= (int)sizeof(path)) {
        gs_error = "Request too long";
        return GS_INVALID;
    }
    /* Fresh connection + per-request timeout: Sunshine (and forks) can
     * block on /launch while the app process starts, and this request
     * must never ride a pooled socket that the server may have closed. */
    return http_get_timeout(server->address,
                            https ? server->httpsPort : server->httpPort,
                            https, path, resp, HTTP_FLAG_FRESH, timeout_ms);
}

// Check status_code and extract a field from the XML.
static int xml_get_field(http_resp_t *resp, const char *field, char **out) {
    *out = NULL;
    if (xml_status(resp->body, resp->len) != 200) {
        gs_error = "Server returned an error status";
        return GS_ERROR;
    }
    return xml_search(resp->body, resp->len, field, out);
}

/* normalize + sanity-check a <mac> serverinfo field. Accepts the
 * ':'/'-'/space-separated (or bare 12-hex) forms; rejects the all-zero
 * placeholder Sunshine sends to unpaired/HTTP callers (and that would
 * wake nothing anyway). On success out is "AA:BB:CC:DD:EE:FF" (needs
 * >= 18 bytes). Exported for the host unit tests. */
int gs_mac_from_field(const char *raw, char *out, size_t cap) {
    if (!raw || !out || cap < 18)
        return -1;
    out[0] = '\0';
    uint8_t mac[6];
    /* wol_mac_parse (discovery.c) accepts every separator style and
     * rejects short/garbage input. */
    if (wol_mac_parse(raw, mac) != 0)
        return -1;
    if (!mac[0] && !mac[1] && !mac[2] && !mac[3] && !mac[4] && !mac[5])
        return -1;
    wol_mac_format(mac, out, cap);
    return 0;
}

static int load_serverinfo(gs_server_t *server, bool https) {
    char uuid[37];
    http_resp_t resp;
    int ret = GS_INVALID;
    char *paired = NULL, *currentGame = NULL, *state = NULL, *httpsPort = NULL;
    char *appversion = NULL, *gfeversion = NULL, *codecMode = NULL;
    char *mac = NULL;

    uuid_v4(server->id, uuid);
    ret = api_get(server, https, &resp,
                  "/serverinfo?uniqueid=%s&uuid=%s", GS_UNIQUEID, uuid);
    if (ret != GS_OK)
        return ret;

    ret = GS_INVALID;
    if (xml_status(resp.body, resp.len) != 200) {
        gs_error = "serverinfo returned an error status";
        goto out;
    }

    if (xml_search(resp.body, resp.len, "currentgame", &currentGame) != GS_OK ||
        xml_search(resp.body, resp.len, "PairStatus", &paired) != GS_OK ||
        xml_search(resp.body, resp.len, "appversion", &appversion) != GS_OK ||
        xml_search(resp.body, resp.len, "state", &state) != GS_OK)
        goto out;

    if (!strlen(currentGame) || !strlen(paired) || !strlen(appversion) || !strlen(state))
        goto out;

    xml_search(resp.body, resp.len, "HttpsPort", &httpsPort);
    xml_search(resp.body, resp.len, "GfeVersion", &gfeversion);
    xml_search(resp.body, resp.len, "ServerCodecModeSupport", &codecMode);
    /* Sunshine tells PAIRED clients the streaming PC's MAC over
     * HTTPS (nvhttp.cpp root.mac; plain-HTTP callers get the
     * 00:00:00:00:00:00 placeholder, filtered by gs_mac_from_field) so
     * the Wake-on-LAN setting can be filled without typing 12 hex
     * digits. */
    if (xml_search(resp.body, resp.len, "mac", &mac) == GS_OK) {
        char norm[24];
        if (gs_mac_from_field(mac, norm, sizeof(norm)) == 0)
            snprintf(server->host_mac, sizeof(server->host_mac), "%s", norm);
    }

    server->paired = !strcmp(paired, "1");
    server->currentGame = atoi(currentGame);
    server->serverMajorVersion = atoi(appversion);
    server->serverCodecModeSupport = (codecMode && codecMode[0]) ? atoi(codecMode) : SCM_H264;
    server->isNvidiaSoftware = strstr(state, "MJOLNIR") != NULL;
    snprintf(server->appVersion, sizeof(server->appVersion), "%s", appversion);
    if (gfeversion)
        snprintf(server->gfeVersion, sizeof(server->gfeVersion), "%s", gfeversion);

    server->httpsPort = httpsPort ? (unsigned short)atoi(httpsPort) : 0;
    if (!server->httpsPort)
        server->httpsPort = 47984;

    if (strstr(state, "_SERVER_BUSY") == NULL) {
        // After GFE 2.8 currentgame stays set even with no active session.
        server->currentGame = 0;
    }
    ret = GS_OK;

out:
    free(paired);
    free(currentGame);
    free(state);
    free(httpsPort);
    free(appversion);
    free(gfeversion);
    free(codecMode);
    free(mac);
    http_resp_free(&resp);
    return ret;
}

int gs_init(gs_server_t *server, client_identity_t *id, const char *address,
            unsigned short httpPort, const char *keydir) {
    (void)keydir;
    memset(server, 0, sizeof(*server));
    server->id = id;
    snprintf(server->address, sizeof(server->address), "%s", address);
    server->httpPort = httpPort ? httpPort : 47989;
    server->httpsPort = 0;

    LOGI("gs_init: HTTP serverinfo %s:%u ...", server->address, server->httpPort);
    int ret = load_serverinfo(server, false);
    if (ret != GS_OK) {
        LOGE("gs_init: HTTP serverinfo FAIL ret=%d err=%s", ret,
             gs_error ? gs_error : "?");
        return ret;
    }
    LOGI("gs_init: HTTP OK paired=%d httpsPort=%u ver=%s",
         server->paired, server->httpsPort, server->appVersion);

    LOGI("gs_init: HTTPS serverinfo port %u ...", server->httpsPort);
    if (load_serverinfo(server, true) != GS_OK) {
        LOGW("gs_init: HTTPS serverinfo failed (normal if not paired); paired=0");
        server->paired = false;
    } else {
        LOGI("gs_init: HTTPS OK paired=%d", server->paired);
    }

    LiInitializeServerInformation(&server->serverInfo);
    server->serverInfo.address = server->address;
    server->serverInfo.serverInfoAppVersion = server->appVersion;
    server->serverInfo.serverInfoGfeVersion = server->gfeVersion;
    server->serverInfo.serverCodecModeSupport = server->serverCodecModeSupport;
    return GS_OK;
}

// ---------------------------------------------------------------------------
// Four-phase pairing
// ---------------------------------------------------------------------------

// Check paired=1 in the response; frees resp.
static int check_paired(http_resp_t *resp) {
    char *result = NULL;
    int ret = xml_get_field(resp, "paired", &result);
    if (ret == GS_OK && (!result || strcmp(result, "1") != 0)) {
        gs_error = "Server rejected pairing";
        ret = GS_FAILED;
    }
    free(result);
    http_resp_free(resp);
    return ret;
}

int gs_pair_ex(gs_server_t *server, const char *pin, unsigned timeout_ms) {
    int ret;
    char uuid[37];
    http_resp_t resp = {0};
    char *result = NULL;
    client_identity_t *id = server->id;

    if (server->paired) {
        gs_error = "Already paired";
        return GS_WRONG_STATE;
    }
    if (server->currentGame != 0) {
        gs_error = "A game is running on the server; close it before pairing";
        return GS_WRONG_STATE;
    }

    LOGI("pairing: phase 1, getservercert (PIN=%s)", pin);
    LOGI("pair f1...");

    // --- Phase 1: salt + client certificate -> server certificate
    unsigned char salt[16];
    char salt_hex[33];
    rng_fill(id, salt, sizeof(salt));
    bytes_to_hex(salt, salt_hex, sizeof(salt));

    uuid_v4(id, uuid);
    ret = api_get_t(server, 0, &resp, timeout_ms,
                  "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                  "&phrase=getservercert&salt=%s&clientcert=%s",
                  GS_UNIQUEID, uuid, salt_hex, id->cert_hex);
    if (ret != GS_OK) {
        LOGE("pairing: phase 1 http FAIL ret=%d %s", ret, gs_error ? gs_error : "");
        return ret;
    }

    if ((ret = xml_get_field(&resp, "paired", &result)) != GS_OK)
        goto fail_resp;
    if (strcmp(result, "1") != 0) {
        gs_error = "Pairing rejected (phase 1)";
        ret = GS_FAILED;
        goto fail_resp;
    }
    free(result);
    result = NULL;

    if ((ret = xml_search(resp.body, resp.len, "plaincert", &result)) != GS_OK)
        goto fail_resp;
    if (!result[0]) {
        // Sunshine returns empty plaincert if another pairing is in progress.
        gs_error = "Server did not return its certificate (another pairing in progress?)";
        ret = GS_FAILED;
        goto fail_resp;
    }

    size_t plaincert_len = 0;
    if (strlen(result) / 2 >= sizeof(server->serverCertPem)) {
        gs_error = "Server certificate too large";
        ret = GS_FAILED;
        goto fail_resp;
    }
    hex_to_bytes(result, (unsigned char *)server->serverCertPem, &plaincert_len);
    server->serverCertPem[plaincert_len] = '\0';
    free(result);
    result = NULL;
    http_resp_free(&resp);

    // AES key derived from salt+PIN.
    int use_sha256 = server->serverMajorVersion >= 7;
    int hash_len = use_sha256 ? 32 : 20;
    unsigned char salt_pin[16 + 4];
    unsigned char aes_key[32];
    memcpy(salt_pin, salt, 16);
    memcpy(salt_pin + 16, pin, 4);
    hash_data(use_sha256, salt_pin, sizeof(salt_pin), aes_key);

    LOGI("pairing: phase 2, clientchallenge");
    LOGI("pair f2...");

    // --- Phase 2: encrypted client challenge
    unsigned char challenge[16];
    unsigned char challenge_enc[16];
    char challenge_hex[33];
    rng_fill(id, challenge, sizeof(challenge));
    aes_ecb(1, aes_key, challenge, sizeof(challenge), challenge_enc);
    bytes_to_hex(challenge_enc, challenge_hex, sizeof(challenge_enc));

    uuid_v4(id, uuid);
    ret = api_get_t(server, 0, &resp, timeout_ms,
                  "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                  "&clientchallenge=%s",
                  GS_UNIQUEID, uuid, challenge_hex);
    if (ret != GS_OK)
        goto fail;

    if ((ret = xml_get_field(&resp, "paired", &result)) != GS_OK)
        goto fail_resp;
    if (strcmp(result, "1") != 0) {
        gs_error = "Pairing rejected (phase 2)";
        ret = GS_FAILED;
        goto fail_resp;
    }
    free(result);
    result = NULL;

    if ((ret = xml_search(resp.body, resp.len, "challengeresponse", &result)) != GS_OK)
        goto fail_resp;

    unsigned char challresp_enc[64];
    unsigned char challresp[64];
    size_t challresp_len = 0;
    if (strlen(result) / 2 > sizeof(challresp_enc)) {
        gs_error = "challengeresponse too large";
        ret = GS_FAILED;
        goto fail_resp;
    }
    hex_to_bytes(result, challresp_enc, &challresp_len);
    /* Validate before decrypting/reading: Sunshine sends 64 bytes
     * (hash || server challenge), but a broken or hostile reply must not
     * read past the buffer. */
    if (challresp_len < (size_t)hash_len + 16 || (challresp_len & 15u) != 0) {
        gs_error = "Server sent a malformed challenge response";
        ret = GS_FAILED;
        goto fail_resp;
    }
    aes_ecb(0, aes_key, challresp_enc, challresp_len, challresp);
    free(result);
    result = NULL;
    http_resp_free(&resp);

    LOGI("pairing: phase 3, serverchallengeresp");
    LOGI("pair f3...");

    // --- Phase 3: response to server challenge
    // challresp = hash(servchal..) + serverChallenge(16), starting at hash_len.
    unsigned char client_secret[16];
    rng_fill(id, client_secret, sizeof(client_secret));

    const unsigned char *cert_sig = id->cert.sig.p;
    size_t cert_sig_len = id->cert.sig.len;
    if (cert_sig_len != SIGNATURE_LEN)
        LOGW("pairing: certificate signature is %zu bytes (expected %d)",
             cert_sig_len, SIGNATURE_LEN);

    unsigned char chall_response[16 + SIGNATURE_LEN + 16];
    size_t cr_len = 0;
    memcpy(chall_response, challresp + hash_len, 16);
    cr_len += 16;
    memcpy(chall_response + cr_len, cert_sig, cert_sig_len);
    cr_len += cert_sig_len;
    memcpy(chall_response + cr_len, client_secret, sizeof(client_secret));
    cr_len += sizeof(client_secret);

    unsigned char cr_hash[32];
    unsigned char cr_hash_enc[32];
    char cr_hex[65];
    hash_data(use_sha256, chall_response, cr_len, cr_hash);
    aes_ecb(1, aes_key, cr_hash, 32, cr_hash_enc);
    bytes_to_hex(cr_hash_enc, cr_hex, 32);

    uuid_v4(id, uuid);
    ret = api_get_t(server, 0, &resp, timeout_ms,
                  "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                  "&serverchallengeresp=%s",
                  GS_UNIQUEID, uuid, cr_hex);
    if (ret != GS_OK)
        goto fail;

    if ((ret = xml_get_field(&resp, "paired", &result)) != GS_OK)
        goto fail_resp;
    if (strcmp(result, "1") != 0) {
        gs_error = "Pairing rejected (phase 3): wrong PIN?";
        ret = GS_FAILED;
        goto fail_resp;
    }
    free(result);
    result = NULL;

    if ((ret = xml_search(resp.body, resp.len, "pairingsecret", &result)) != GS_OK)
        goto fail_resp;

    unsigned char pairing_secret[16 + SIGNATURE_LEN];
    size_t ps_len = 0;
    if (strlen(result) / 2 > sizeof(pairing_secret)) {
        gs_error = "pairingsecret too large";
        ret = GS_FAILED;
        goto fail_resp;
    }
    hex_to_bytes(result, pairing_secret, &ps_len);
    free(result);
    result = NULL;
    http_resp_free(&resp);

    // Anti-MITM check: secret signature must be from the certificate
    // the server presented in phase 1.
    if (!verify_signature(pairing_secret, 16, pairing_secret + 16,
                          ps_len - 16, server->serverCertPem)) {
        gs_error = "Invalid server signature (possible MITM)";
        ret = GS_FAILED;
        goto fail;
    }

    LOGI("pairing: phase 4, clientpairingsecret");
    LOGI("pair f4...");

    // --- Phase 4: signed client secret
    unsigned char signature[SIGNATURE_LEN];
    size_t sig_len = 0;
    if (sign_message(id, client_secret, sizeof(client_secret),
                     signature, sizeof(signature), &sig_len) != GS_OK) {
        gs_error = "Could not sign client secret";
        ret = GS_FAILED;
        goto fail;
    }

    unsigned char client_pairing_secret[16 + SIGNATURE_LEN];
    char cps_hex[(16 + SIGNATURE_LEN) * 2 + 1];
    memcpy(client_pairing_secret, client_secret, 16);
    memcpy(client_pairing_secret + 16, signature, sig_len);
    bytes_to_hex(client_pairing_secret, cps_hex, 16 + sig_len);

    uuid_v4(id, uuid);
    ret = api_get_t(server, 0, &resp, timeout_ms,
                  "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                  "&clientpairingsecret=%s",
                  GS_UNIQUEID, uuid, cps_hex);
    if (ret != GS_OK)
        goto fail;
    if ((ret = check_paired(&resp)) != GS_OK)
        goto fail;

    LOGI("pairing: HTTPS confirmation, pairchallenge");
    LOGI("pair f5 https...");

    // --- HTTPS confirmation with client certificate
    uuid_v4(id, uuid);
    ret = api_get_t(server, 1, &resp, timeout_ms,
                  "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                  "&phrase=pairchallenge",
                  GS_UNIQUEID, uuid);
    if (ret != GS_OK)
        goto fail;
    if ((ret = check_paired(&resp)) != GS_OK)
        goto fail;

    server->paired = true;
    /* Pairing succeeded: clear the error string so a later status print
     * does not show a stale failure from an earlier phase (a stale
     * "No response from server" would otherwise print right after a
     * successful handshake). */
    gs_error = NULL;
    LOGI("pairing: completed");
    LOGI("pair DONE");
    return GS_OK;

fail_resp:
    free(result);
    http_resp_free(&resp);
fail:
    /* Preserve the pairing failure reason: gs_unpair overwrites gs_error
     * with its own result (usually a follow-up failure), which used to
     * mask the real cause reported to the user. */
    {
        const char *pair_error = gs_error;
        (void)gs_unpair(server);
        gs_error = pair_error;
    }
    return ret;
}

int gs_pair(gs_server_t *server, const char *pin) {
    /* Legacy budget: the PIN screen waits for the user to type the PIN
     * into Sunshine, so pairing phases get a long leash (120 s). */
    return gs_pair_ex(server, pin, 120000);
}

int gs_unpair(gs_server_t *server) {
    char uuid[37];
    http_resp_t resp;

    uuid_v4(server->id, uuid);
    int ret = api_get(server, 0, &resp, "/unpair?uniqueid=%s&uuid=%s",
                      GS_UNIQUEID, uuid);
    if (ret == GS_OK) {
        http_resp_free(&resp);
        server->paired = false;
    }
    return ret;
}

// ---------------------------------------------------------------------------
// Applist and launch
// ---------------------------------------------------------------------------

int gs_appasset(gs_server_t *server, int app_id, unsigned char **bytes, size_t *length) {
    *bytes = NULL;
    *length = 0;
    http_resp_t resp = {0};
    int ret = api_get(server, 1, &resp,
        "/appasset?uniqueid=%s&appid=%d&AssetType=2&AssetIdx=0", GS_UNIQUEID, app_id);
    if (ret == GS_OK) {
        *bytes = (unsigned char *)resp.body;
        *length = resp.len;
    } else http_resp_free(&resp);
    return ret;
}

int gs_applist(gs_server_t *server, app_entry_t **list) {
    return gs_applist_ex(server->id, server->address, server->httpsPort, list);
}

/* Race-free variant for background threads: uses explicit host/port and the
 * identity captured at spawn time instead of dereferencing gs_server_t,
 * which the main thread may re-initialize (gs_init) while a menu is closed. */
int gs_applist_ex(client_identity_t *id, const char *address,
                  unsigned short https_port, app_entry_t **list) {
    char uuid[37];
    char path[256];
    http_resp_t resp;

    uuid_v4(id, uuid);
    snprintf(path, sizeof(path), "/applist?uniqueid=%s&uuid=%s",
             GS_UNIQUEID, uuid);
    int ret = http_get(address, https_port, 1, path, &resp);
    if (ret != GS_OK)
        return ret;

    if (xml_status(resp.body, resp.len) != 200) {
        gs_error = "applist returned an error status";
        ret = GS_ERROR;
    } else {
        ret = xml_applist(resp.body, resp.len, list);
    }
    http_resp_free(&resp);
    return ret;
}

/* put the PC's own answer in gs_error, whitespace-
 * flattened and bounded - shown as "Could not start the game. (the PC
 * replied: ...)" on the launch fail screen. */
static void compose_reply_error(const char *text) {
    char *reply=gs_error_buffer();
    const char *src = text ? text : "";
    char clean[144];
    size_t o = 0;
    for (size_t i = 0; src[i] && o < sizeof(clean) - 1; i++) {
        char ch = src[i];
        if (ch == '\r' || ch == '\n' || ch == '\t')
            ch = ' ';
        clean[o++] = ch;
    }
    clean[o] = '\0';
    snprintf(reply, 512, "the PC replied: %s", clean);
    gs_error = reply;
}

int gs_start_app(gs_server_t *server, STREAM_CONFIGURATION *config, int appId,
                 bool sops, bool localaudio, int gamepad_mask) {
    char uuid[37];
    http_resp_t resp;
    char *result = NULL;
    int ret;

    rng_fill(server->id, (unsigned char *)config->remoteInputAesKey,
             sizeof(config->remoteInputAesKey));
    memset(config->remoteInputAesIv, 0, sizeof(config->remoteInputAesIv));

    uint32_t rikeyid = 0;
    rng_fill(server->id, (unsigned char *)config->remoteInputAesIv, 4);
    memcpy(&rikeyid, config->remoteInputAesIv, 4);
    rikeyid = htonl(rikeyid);

    char rikey_hex[sizeof(config->remoteInputAesKey) * 2 + 1];
    bytes_to_hex((unsigned char *)config->remoteInputAesKey, rikey_hex,
                 sizeof(config->remoteInputAesKey));

    // FPS > 60 makes GFE SOPS force 720p60; Sunshine ignores it.
    int fps = (server->isNvidiaSoftware && config->fps > 60) ? 0 : config->fps;
    int surround_info = SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(config->audioConfiguration);

    uuid_v4(server->id, uuid);
    /* Fresh connection + generous timeout: Sunshine (and forks) can block
     * on /launch while the app process starts, and this request must never
     * ride a pooled socket that the server may have closed. */
    ret = api_get_fresh(server, 1, &resp, 15000,
                  "/%s?uniqueid=%s&uuid=%s&appid=%d&mode=%dx%dx%d"
                  "&additionalStates=1&sops=%d&rikey=%s&rikeyid=%u"
                  "&localAudioPlayMode=%d&surroundAudioInfo=%d"
                  "&remoteControllersBitmap=%d&gcmap=%d%s",
                  server->currentGame ? "resume" : "launch",
                  GS_UNIQUEID, uuid, appId,
                  config->width, config->height, fps,
                  sops ? 1 : 0, rikey_hex, (unsigned)rikeyid,
                  localaudio ? 1 : 0, surround_info,
                  gamepad_mask, gamepad_mask,
                  LiGetLaunchUrlQueryParameters());
    if (ret != GS_OK) {
        /* Non-200 (or transport error). A body means the host EXPLAINED
         * itself (Sunshine: "app is locked", "not available", a PIN
         * prompt on some forks); log it and surface the PC's words instead
         * of the HTTP layer's generic "Server returned an HTTP error".
         * Transport failures carry no body and keep their specific reason. */
        if (resp.body && resp.len) {
            LOGW("launch error body: %.120s", resp.body);
            char *msg = NULL;
            xml_search(resp.body, resp.len, "root", &msg);
            compose_reply_error(msg && msg[0] ? msg : resp.body);
            free(msg);
        }
        http_resp_free(&resp);
        return ret;
    }

    if (xml_status(resp.body, resp.len) != 200) {
        char *msg = NULL;
        xml_search(resp.body, resp.len, "root", &msg);
        /* HTTP 200 but an XML error code: same treatment - the answer is
         * the message. */
        LOGW("launch error body: %.120s", resp.body ? resp.body : "");
        compose_reply_error(msg && msg[0] ? msg : (resp.body ? resp.body : ""));
        free(msg);
        ret = GS_ERROR;
        goto out;
    }

    if (xml_search(resp.body, resp.len, "gamesession", &result) != GS_OK &&
        xml_search(resp.body, resp.len, "resume", &result) != GS_OK) {
        ret = GS_INVALID;
        goto out;
    }
    if (!strcmp(result, "0")) {
        gs_error = "Server could not start the session";
        ret = GS_FAILED;
        goto out;
    }
    free(result);
    result = NULL;

    if (xml_search(resp.body, resp.len, "sessionUrl0", &result) == GS_OK) {
        snprintf(server->rtspSessionUrl, sizeof(server->rtspSessionUrl), "%s", result);
        server->serverInfo.rtspSessionUrl = server->rtspSessionUrl;
    }

    server->currentGame = appId;
    ret = GS_OK;

out:
    free(result);
    http_resp_free(&resp);
    return ret;
}

int gs_quit_app(gs_server_t *server) {
    char uuid[37];
    http_resp_t resp;
    char *result = NULL;

    uuid_v4(server->id, uuid);
    /* Fresh connection: /cancel stops the running app; a ghost duplicate
     * from a retried stale socket would kill a session we meant to keep. */
    int ret = api_get_fresh(server, 1, &resp, 10000,
                            "/cancel?uniqueid=%s&uuid=%s",
                            GS_UNIQUEID, uuid);
    if (ret != GS_OK)
        return ret;

    if ((ret = xml_get_field(&resp, "cancel", &result)) != GS_OK)
        goto out;
    if (!strcmp(result, "0")) {
        gs_error = "Server could not cancel the session";
        ret = GS_FAILED;
        goto out;
    }
    server->currentGame = 0;
    ret = GS_OK;

out:
    free(result);
    http_resp_free(&resp);
    return ret;
}

int gs_refresh_status(gs_server_t *server) {
    if (!server)
        return GS_INVALID;
    bool https = server->paired && server->httpsPort != 0;
    int ret = load_serverinfo(server, https);
    if (ret != GS_OK && https)
        ret = load_serverinfo(server, false);
    return ret;
}

/* Lightweight serverinfo query for background threads (same race-free
 * capture rules as gs_applist_ex). Reports paired state and the running
 * app. Prefers the paired HTTPS view: Sunshine's plain-HTTP serverinfo
 * always answers PairStatus=0 (it cannot authenticate the client there),
 * which used to overwrite the live server state with paired=false and
 * lose currentGame for the menu/launch logic. Falls back to HTTP when
 * HTTPS is unavailable (e.g. pre-pairing first run).
 * host_mac_out (may be NULL) receives the PC's MAC when the
 * paired HTTPS view reports one ("" otherwise) - the menu refresh can
 * then adopt it for Wake-on-LAN without waiting for a connect. */
int gs_serverinfo_status(client_identity_t *id, const char *address,
                         unsigned short https_port, unsigned short http_port,
                         bool *paired, int *current_game,
                         char *host_mac_out, size_t host_mac_cap) {
    char uuid[37];
    char path[512];
    http_resp_t resp;
    int ret = GS_INVALID;
    char *paired_str = NULL, *current = NULL, *state = NULL, *mac = NULL;

    *paired = false;
    *current_game = 0;
    if (host_mac_out && host_mac_cap)
        host_mac_out[0] = '\0';
    uuid_v4(id, uuid);
    snprintf(path, sizeof(path), "/serverinfo?uniqueid=%s&uuid=%s",
             GS_UNIQUEID, uuid);

    ret = GS_INVALID;
    if (https_port) {
        ret = http_get(address, https_port, 1, path, &resp);
        if (ret == GS_OK)
            goto parse;
        /* Unpaired or HTTPS filtered: try the plain view. */
        LOGW("status: HTTPS serverinfo failed (%s); trying HTTP",
             gs_error ? gs_error : "?");
    }
    ret = http_get(address, http_port, 0, path, &resp);
    if (ret != GS_OK)
        return ret;

parse:
    ret = GS_INVALID;
    if (xml_status(resp.body, resp.len) != 200) {
        gs_error = "serverinfo returned an error status";
        goto out;
    }
    if (xml_search(resp.body, resp.len, "PairStatus", &paired_str) != GS_OK ||
        xml_search(resp.body, resp.len, "currentgame", &current) != GS_OK)
        goto out;
    xml_search(resp.body, resp.len, "state", &state);

    /* Same placeholder filter as load_serverinfo: unpaired/HTTP callers
     * get 00:00:00:00:00:00, which never adopts. */
    if (host_mac_out && host_mac_cap >= 18 &&
        xml_search(resp.body, resp.len, "mac", &mac) == GS_OK) {
        if (gs_mac_from_field(mac, host_mac_out, (size_t)host_mac_cap) != 0)
            host_mac_out[0] = '\0';
    }

    *paired = paired_str && !strcmp(paired_str, "1");
    *current_game = atoi(current);
    if (!state || !strstr(state, "_SERVER_BUSY"))
        *current_game = 0; /* after GFE 2.8 currentgame stays set while idle */
    ret = GS_OK;

out:
    free(paired_str);
    free(current);
    free(state);
    free(mac);
    http_resp_free(&resp);
    return ret;
}
