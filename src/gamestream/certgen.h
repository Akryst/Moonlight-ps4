// Client identity: RSA-2048 key and self-signed certificate that
// GameStream/Sunshine use to recognize the paired client.
#pragma once

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

typedef struct {
    mbedtls_pk_context key;
    mbedtls_x509_crt cert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;

    char cert_pem[4096];  // certificate PEM (for TLS and for hex)
    char key_pem[4096];   // key PEM (for TLS)
    char cert_hex[8192];  // PEM as hex (pairing clientcert parameter)
} client_identity_t;

// Load identity from keydir (cert.pem / key.pem) or generate and save it.
int identity_init(client_identity_t *id, const char *keydir);
void identity_free(client_identity_t *id);

// Shared utilities.
void bytes_to_hex(const unsigned char *in, char *out, size_t len);
void hex_to_bytes(const char *in, unsigned char *out, size_t *outlen);
void uuid_v4(client_identity_t *id, char *out37);
int rng_fill(client_identity_t *id, unsigned char *buf, size_t len);

#include <pthread.h>
extern pthread_mutex_t identity_crypto_mutex;
