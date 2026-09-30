// GameStream/Sunshine API client: serverinfo, four-phase pairing,
// applist and launch/resume/cancel. mbedTLS port of libgamestream from
// moonlight-embedded (GPLv3).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <Limelight.h>

#include "certgen.h"
#include "mini_xml.h"

#define GS_UNIQUEID "0123456789ABCDEF"

typedef struct {
    SERVER_INFORMATION serverInfo;

    char address[128];
    unsigned short httpPort;
    unsigned short httpsPort;

    bool paired;
    int currentGame;
    int serverMajorVersion;
    int serverCodecModeSupport;
    bool isNvidiaSoftware;

    char appVersion[32];
    char gfeVersion[32];
    char serverCertPem[8192]; // server certificate saved during pairing
    char rtspSessionUrl[256];

    client_identity_t *id;
} gs_server_t;

// Initialize identity (generating certificate if needed), query
// serverinfo and fill server state.
int gs_init(gs_server_t *server, client_identity_t *id, const char *address,
            unsigned short httpPort, const char *keydir);

// Four-phase pairing with 4-digit PIN.
int gs_pair(gs_server_t *server, const char *pin);
int gs_unpair(gs_server_t *server);

int gs_applist(gs_server_t *server, app_entry_t **list);
int gs_appasset(gs_server_t *server, int app_id, unsigned char **bytes, size_t *length);

// Launch or resume an app and leave the server ready for LiStartConnection.
// Fills config->remoteInputAesKey/Iv and server->rtspSessionUrl.
int gs_start_app(gs_server_t *server, STREAM_CONFIGURATION *config, int appId,
                 bool sops, bool localaudio, int gamepad_mask);

int gs_quit_app(gs_server_t *server);

/* Refresh serverinfo (currentGame, paired, etc.) without re-init. */
int gs_refresh_status(gs_server_t *server);
