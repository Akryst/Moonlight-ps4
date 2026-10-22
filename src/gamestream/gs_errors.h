// Gamestream layer error codes, compatible with those from
// moonlight-embedded for easier comparisons.
#pragma once

#define GS_OK                  0
#define GS_FAILED             -1
#define GS_OUT_OF_MEMORY      -2
#define GS_INVALID            -3
#define GS_WRONG_STATE        -4
#define GS_IO_ERROR           -5
#define GS_NOT_SUPPORTED_4K   -6
#define GS_UNSUPPORTED_VERSION -7
#define GS_NOT_SUPPORTED_MODE -8
#define GS_ERROR              -9

const char **gs_error_slot(void);
char *gs_error_buffer(void);
#define gs_error (*gs_error_slot())
