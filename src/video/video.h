// Video interface: decoders (FFmpeg / Videodec2) + YCbCr/NV12 presentation.
#pragma once

#include <Limelight.h>
#include <stdint.h>
#include <stddef.h>

#ifndef ML_ENABLE_VIDEODEC2
#define ML_ENABLE_VIDEODEC2 0
#endif

extern DECODER_RENDERER_CALLBACKS video_callbacks_ffmpeg;

#ifdef __ORBIS__
extern DECODER_RENDERER_CALLBACKS video_callbacks_orbis;
int video_orbis_probe(int width, int height);
int videodec2_spike_run(void);
/* Videodec2 tuning; must be called before LiStartConnection (dr_setup latches it). */
void video_orbis_set_tuning(int pipeline_depth, int thread_prio,
                            int au_onion, int fb_garlic);
#endif

typedef enum {
    VIDEO_FRAME_NV12,
    VIDEO_FRAME_YUV420P,
} video_frame_format_t;

typedef struct {
    unsigned long long decode_us_total;
    unsigned long long convert_us_total; /* bounce + blit/bgra */
    unsigned long long bounce_us_total;  /* memcpy WC→cacheable (decoder_orbis) */
    unsigned long long bgra_us_total;    /* kernel convert/blit only in present */
    unsigned long long present_us_total;
    unsigned long long au_us_total;    /* AU copy into decoder-visible memory */
    unsigned long long au_bytes_total; /* AU payload; grows with bitrate */
    unsigned frames;   /* frames presented (flip OK) */
    unsigned decodes;  /* Decode calls counted (may be > frames) */
    unsigned dropped;
} video_stats_t;

void video_get_stats(video_stats_t *out);
void video_reset_stats(void);
void video_take_stats(video_stats_t *out);
void video_stats_begin_session(void);
void video_stats_add(unsigned long long decode_us, unsigned long long convert_us,
                     unsigned long long present_us, unsigned dropped);
/* Accumulate Decode time and increment decodes (not frames). */
void video_stats_add_decode(unsigned long long decode_us);
/* Accumulate WC→bounce memcpy in convert/bounce (without incrementing frames). */
void video_stats_add_bounce(unsigned long long bounce_us);
/* Accumulate the AU staging copy and its size (not counted in decode). */
void video_stats_add_au(unsigned long long au_us, unsigned long long au_bytes);

/* Rolling per-frame history for the on-screen perf overlay (see ui_menu
 * "Perf overlay"). Independent of video_reset_stats()/the periodic log in
 * stream.c: always reflects the last VIDEO_STATS_HISTORY presented frames. */
#define VIDEO_STATS_HISTORY 120
#define VIDEO_STATS_WINDOW_MS 2000 /* sliding window for fps/avg computations */

typedef struct {
    float fps;          /* presented frames/sec over the last ~2s */
    float decode_ms;    /* avg decode time over the window */
    float convert_ms;   /* avg convert (bounce+blit/bgra) time over the window */
    float present_ms;   /* avg SubmitFlip+wait time over the window */
    float kb_per_frame; /* avg AU (encoded) size over the window */
    float frame_ms[VIDEO_STATS_HISTORY]; /* delivery interval per frame, oldest..newest */
    int frame_count;                     /* valid entries in frame_ms */
} video_live_stats_t;

void video_stats_get_live(video_live_stats_t *out);

#ifdef __ORBIS__
/* workers <= 0 keeps the default. nt_pref: -1 auto (from the framebuffer
 * mapping), 0 forces cached stores, 1 forces streaming stores. */
void video_present_set_bgra_tuning(int workers, int nt_pref);
/* Reset presentation diagnostics and select the bounded slot wait per session. */
void video_present_begin_session(int pipeline_depth);
int video_present_init(int w, int h, int prefer_ycbcr);
void video_present_shutdown(void);
int video_present_should_drop(void);
/* Flip queue truly backed up (ignores the pipelined convert in flight). */
int video_present_flip_backlogged(void);
int video_present_plugin_loaded(void);
int video_present_is_bgra(void);
/* Perf overlay (FPS/decode/convert/present/KB per frame + frame-time timeline)
 * drawn top-left on the streamed frame. Only supported in BGRA present mode
 * (prefer_ycbcr=false); ignored otherwise. */
void video_set_show_stats(int enable);
/* BGRA pipeline: kick convert async; finish = join + flip. Decode always separate. */
int video_present_bgra_pipe_kick(const uint8_t *y, const uint8_t *uv,
                                 int pitch_y, int pitch_uv, int w, int h,
                                 uint32_t frame_wait_limit_us);
int video_present_bgra_pipe_finish(void);
int video_present_frame(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                        int pitch_y, int pitch_uv, int w, int h,
                        video_frame_format_t fmt);
int video_present_frame_nv12_copy(const void *src, size_t src_size,
                                  int pitch_y, int pitch_uv, int w, int h);

/* Menu UI: reuses BGRA present (CPU draw + flip). Exclusive with
 * the stream: call video_ui_end() before LiStartConnection. */
int video_ui_begin(int w, int h);
int video_ui_get_fb(uint8_t **bgra, int *pitch_bytes, int *idx);
int video_ui_flip(int idx);
/* Present a full BGRA frame from staging (avoids drawing on-screen). */
int video_ui_present(const uint8_t *src, int src_pitch);
void video_ui_end(void);
#endif
