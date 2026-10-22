#include "audio_orbis.h"
#include "../log.h"

#include <opus_multistream.h>

#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// sceAudioOut works with fixed sample grains; 256 is the minimum
// (5.33 ms at 48 kHz), closest to Moonlight's Opus frame (240, 5 ms).
#define AUDIO_GRAIN 256
#define MAX_DECODE_CHANNELS 8 /* stereo, 5.1 (6); 7.1 headroom */
// Cap on queued PCM before drop: ~32 ms. If output stalls
// we prefer dropping audio over accumulating latency.
#define RING_SAMPLES (AUDIO_GRAIN * 6)

// OpenOrbis: 0x8026000E = ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT
#define AUDIO_OUT_ALREADY_INIT 0x8026000Eu

/* PS4 SDK: 8-channel S16 (7.1 layout FL FR FC LFE BL BR SL SR). The
 * OpenOrbis header stops at stereo, so spell the known value out. */
#ifndef ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_8CH
#define ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_8CH 2
#endif

static OpusMSDecoder *s_decoder;
static int s_decode_channels; /* channels the Opus stream decodes to */
static int s_out_channels;    /* channels sceAudioOut was opened with */
static int s_samples_per_frame;
static int32_t s_handle = -1;

// Interleaved PCM ring buffer (output layout).
static short s_ring[RING_SAMPLES * MAX_DECODE_CHANNELS];
static int s_ring_head; // read sample index
static int s_ring_tail; // write sample index
static int s_ring_count;

static short s_outbuf[AUDIO_GRAIN * MAX_DECODE_CHANNELS];
static short s_decbuf[512 * MAX_DECODE_CHANNELS];

static void ar_cleanup(void);

static int ar_init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
                   void *context, int arFlags) {
    (void)audioConfiguration; (void)context; (void)arFlags;
    int rc;
    uint32_t rate;
    ar_cleanup();
    if(!opusConfig || opusConfig->samplesPerFrame<1 || opusConfig->samplesPerFrame>512)return -1;

    s_decode_channels = opusConfig->channelCount;
    if (s_decode_channels!=1 && s_decode_channels!=2 && s_decode_channels!=6) {
        LOGE("audio: %d channels unsupported (max %d)",
             s_decode_channels, MAX_DECODE_CHANNELS);
        return -1;
    }
    s_samples_per_frame = opusConfig->samplesPerFrame;

    // Orbis only accepts 48000 in practice for MAIN; Moonlight Opus is 48 kHz.
    rate = opusConfig->sampleRate;
    if (rate != 48000u) {
        LOGW("audio: sampleRate=%u unsupported, forcing 48000", rate);
        rate = 48000u;
    }

    int err;
    s_decoder = opus_multistream_decoder_create(
        opusConfig->sampleRate, opusConfig->channelCount, opusConfig->streams,
        opusConfig->coupledStreams, opusConfig->mapping, &err);
    if (!s_decoder) {
        LOGE("audio: opus_multistream_decoder_create failed: %d", err);
        return -1;
    }

    // Same as Net: the link stub is not enough; must load the internal PRX.
    rc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
    LOGI("audio: LoadModuleInternal(AUDIOOUT) => 0x%08x", (unsigned)rc);

    rc = sceAudioOutInit();
    if (rc != 0 && (uint32_t)rc != AUDIO_OUT_ALREADY_INIT) {
        LOGE("audio: sceAudioOutInit failed: 0x%08x", (unsigned)rc);
        ar_cleanup();return -1;
    }
    LOGI("audio: sceAudioOutInit => 0x%08x", (unsigned)rc);

    int32_t user = ORBIS_USER_SERVICE_USER_ID_SYSTEM;
    int32_t initial = -1;
    if (sceUserServiceGetInitialUser(&initial) == 0 && initial > 0)
        user = initial;

    /* 5.1 support. Moonlight decodes 5.1 to FL FR FC LFE BL BR -
     * exactly the first six slots of the PS4 8-channel S16 layout - so
     * the two surround pairs line up with a plain channel expand (last
     * two slots silent). If the 8ch port open fails (TV stereo-only
     * setups, odd HDMI negotiations), fall back to a classic 5.1->2.0
     * downmix so audio keeps playing instead of going silent. */
    s_out_channels = s_decode_channels;
    if (s_decode_channels == 6) {
        int32_t h8 = sceAudioOutOpen(user, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                                     AUDIO_GRAIN, rate,
                                     ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_8CH);
        if (h8 <= 0 && user != ORBIS_USER_SERVICE_USER_ID_SYSTEM) {
            h8 = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,
                                 ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                                 AUDIO_GRAIN, rate,
                                 ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_8CH);
        }
        if (h8 > 0) {
            s_handle = h8;
            s_out_channels = 8;
            LOGI("audio: 5.1 stream -> 8ch port (handle=%d)", (int)h8);
        } else {
            LOGW("audio: 8ch open failed 0x%08x; 5.1 downmix to stereo",
                 (unsigned)h8);
            s_out_channels = 2;
        }
    }

    if (s_handle <= 0) {
        uint32_t fmt = (s_out_channels == 1) ? ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_MONO
                                             : ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO;
        s_handle = sceAudioOutOpen(user, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                                   AUDIO_GRAIN, rate, fmt);
        if (s_handle <= 0) {
            // Typical homebrew fallback: user SYSTEM (0xFF).
            LOGW("audio: Open user=%d failed 0x%08x; retry SYSTEM",
                 (int)user, (unsigned)s_handle);
            s_handle = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,
                                       ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0,
                                       AUDIO_GRAIN, rate, fmt);
        }
        if (s_handle <= 0) {
            LOGE("audio: sceAudioOutOpen failed: 0x%08x",
                 (unsigned)s_handle);
            ar_cleanup();return -1;
        }
    }

    s_ring_head = s_ring_tail = s_ring_count = 0;
    LOGI("audio: handle=%d %u Hz, decode %d ch -> out %d ch, grain %d",
         (int)s_handle, rate, s_decode_channels, s_out_channels, AUDIO_GRAIN);
    return 0;
}

static void ar_cleanup(void) {
    if (s_handle >= 0) {
        sceAudioOutOutput(s_handle, NULL); // wait for last block
        sceAudioOutClose(s_handle);
        s_handle = -1;
    }
    if (s_decoder) {
        opus_multistream_decoder_destroy(s_decoder);
        s_decoder = NULL;
    }
}

static void ring_push(const short *pcm, int samples) {
    if (s_ring_count + samples > RING_SAMPLES) {
        // Overflow: drop oldest samples to avoid accumulating latency.
        int drop = s_ring_count + samples - RING_SAMPLES;
        s_ring_head = (s_ring_head + drop) % RING_SAMPLES;
        s_ring_count -= drop;
    }
    for (int i = 0; i < samples; i++) {
        memcpy(&s_ring[s_ring_tail * s_out_channels], &pcm[i * s_out_channels],
               (size_t)s_out_channels * sizeof(short));
        s_ring_tail = (s_ring_tail + 1) % RING_SAMPLES;
    }
    s_ring_count += samples;
}

static void ring_pop(short *out, int samples) {
    for (int i = 0; i < samples; i++) {
        memcpy(&out[i * s_out_channels], &s_ring[s_ring_head * s_out_channels],
               (size_t)s_out_channels * sizeof(short));
        s_ring_head = (s_ring_head + 1) % RING_SAMPLES;
    }
    s_ring_count -= samples;
}

/* Convert one decoded frame (s_decode_channels interleaved) into the
 * output layout and queue it in the ring (s_out_channels interleaved). */
static void push_decoded(const short *pcm, int samples) {
    if (s_out_channels == s_decode_channels) {
        ring_push(pcm, samples);
        return;
    }
    if (s_out_channels == 8 && s_decode_channels == 6) {
        /* FL FR FC LFE BL BR -> FL FR FC LFE BL BR (sil) (sil). */
        if (s_ring_count + samples > RING_SAMPLES) {
            int drop = s_ring_count + samples - RING_SAMPLES;
            s_ring_head = (s_ring_head + drop) % RING_SAMPLES;
            s_ring_count -= drop;
        }
        for (int i = 0; i < samples; i++) {
            short *dst = &s_ring[s_ring_tail * 8];
            memcpy(dst, &pcm[i * 6], 6 * sizeof(short));
            dst[6] = 0;
            dst[7] = 0;
            s_ring_tail = (s_ring_tail + 1) % RING_SAMPLES;
        }
        s_ring_count += samples;
        return;
    }
    if (s_out_channels == 2 && s_decode_channels == 6) {
        /* Classic 5.1 -> stereo downmix:
         * L = FL + 0.707*FC + 0.707*BL, R = FR + 0.707*FC + 0.707*BR,
         * LFE dropped. Fixed-point (181/256 ~ 0.707), clamped. */
        if (s_ring_count + samples > RING_SAMPLES) {
            int drop = s_ring_count + samples - RING_SAMPLES;
            s_ring_head = (s_ring_head + drop) % RING_SAMPLES;
            s_ring_count -= drop;
        }
        for (int i = 0; i < samples; i++) {
            const short *s6 = &pcm[i * 6];
            int l = s6[0] + ((s6[2] * 181) >> 8) + ((s6[4] * 181) >> 8);
            int r = s6[1] + ((s6[2] * 181) >> 8) + ((s6[5] * 181) >> 8);
            short *dst = &s_ring[s_ring_tail * 2];
            dst[0] = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            dst[1] = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            s_ring_tail = (s_ring_tail + 1) % RING_SAMPLES;
        }
        s_ring_count += samples;
        return;
    }
    LOGW("audio: unhandled layout %d -> %d", s_decode_channels,
         s_out_channels);
}

// Called from the moonlight-common-c audio thread (without DIRECT_SUBMIT).
// sceAudioOutOutput blocks until the previous block finishes, which
// sets the playback pace. sampleData==NULL => libopus PLC.
static void ar_decode_and_play(char *sampleData, int sampleLength) {
    int decoded = opus_multistream_decode(
        s_decoder,
        sampleData ? (unsigned char *)sampleData : NULL,
        sampleData ? sampleLength : 0,
        s_decbuf, s_samples_per_frame, 0);
    if (decoded <= 0) {
        if (sampleData)
            LOGW("audio: opus decode error: %d", decoded);
        return;
    }

    push_decoded(s_decbuf, decoded);

    while (s_ring_count >= AUDIO_GRAIN) {
        ring_pop(s_outbuf, AUDIO_GRAIN);
        int rc = sceAudioOutOutput(s_handle, s_outbuf);
        if (rc < 0) {
            LOGW("audio: sceAudioOutOutput failed: 0x%08x", rc);
            break;
        }
    }
}

AUDIO_RENDERER_CALLBACKS audio_callbacks_orbis = {
    .init = ar_init,
    .cleanup = ar_cleanup,
    .decodeAndPlaySample = ar_decode_and_play,
    .capabilities = CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION,
};
