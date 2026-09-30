#pragma once
#include <stddef.h>
#include <stdint.h>

#define MDNS_MAX_HOSTS 16
#define MDNS_MAX_ADDRS 32
#define MDNS_SERVICE "_nvstream._tcp.local"

typedef struct {
    char instance[256];
    char target[256];
    char address[16];
    unsigned short port;
} mdns_host_t;

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
int mdns_discover(mdns_host_t *hosts, int capacity, int timeout_ms);
int mdns_valid_ipv4(const char *address);
