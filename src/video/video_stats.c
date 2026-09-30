#include "video.h"

#include <pthread.h>
#include <string.h>

/* sceKernelGetProcessTime is in us per scene docs (see input_pad.c). */
static uint64_t now_us(void) {
    extern uint64_t sceKernelGetProcessTime(void);
    return sceKernelGetProcessTime();
}

static video_stats_t s_stats;
static pthread_mutex_t s_stats_lock = PTHREAD_MUTEX_INITIALIZER;

void video_get_stats(video_stats_t *out) {
    pthread_mutex_lock(&s_stats_lock);
    *out = s_stats;
    pthread_mutex_unlock(&s_stats_lock);
}

void video_reset_stats(void) {
    pthread_mutex_lock(&s_stats_lock);
    memset(&s_stats, 0, sizeof(s_stats));
    pthread_mutex_unlock(&s_stats_lock);
}

/*
 * Rolling per-frame history for the on-screen perf overlay. Independent of
 * s_stats above (which stream.c zeroes every ~1s for the log): this ring
 * always holds the last VIDEO_STATS_HISTORY presented frames.
 *
 * Decode time/AU bytes are recorded per-decode (video_stats_add_decode /
 * video_stats_add_au) and latched here as "pending"; video_stats_add() (once
 * per presented frame) consumes them together with its own convert/present
 * times into one ring sample. With dec_pipeline_depth=1 (default) this is an
 * exact 1:1 match; with deeper pipelines it can be off by one frame, which is
 * acceptable for a live overlay.
 */
typedef struct {
    uint64_t ts_us;
    unsigned decode_us;
    unsigned convert_us;
    unsigned present_us;
    unsigned bytes;
} live_sample_t;

static live_sample_t s_ring[VIDEO_STATS_HISTORY];
static int s_ring_head; /* next slot to write */
static int s_ring_count;
static unsigned long long s_pending_decode_us;
static unsigned long long s_pending_bytes;
static unsigned long long s_pending_bounce_us;
static pthread_mutex_t s_live_lock = PTHREAD_MUTEX_INITIALIZER;

void video_stats_add(unsigned long long decode_us, unsigned long long convert_us,
                     unsigned long long present_us, unsigned dropped) {
    pthread_mutex_lock(&s_stats_lock);
    s_stats.decode_us_total += decode_us;
    s_stats.convert_us_total += convert_us;
    s_stats.bgra_us_total += convert_us;
    s_stats.present_us_total += present_us;
    if (!dropped)
        s_stats.frames++;
    else
        s_stats.dropped += dropped;
    pthread_mutex_unlock(&s_stats_lock);

    if (dropped)
        return;

    pthread_mutex_lock(&s_live_lock);
    live_sample_t *s = &s_ring[s_ring_head];
    s->ts_us = now_us();
    s->decode_us = (unsigned)(decode_us + s_pending_decode_us);
    s->convert_us = (unsigned)(convert_us + s_pending_bounce_us);
    s_pending_bounce_us = 0;
    s->present_us = (unsigned)present_us;
    s->bytes = (unsigned)s_pending_bytes;
    s_pending_decode_us = 0;
    s_pending_bytes = 0;
    s_ring_head = (s_ring_head + 1) % VIDEO_STATS_HISTORY;
    if (s_ring_count < VIDEO_STATS_HISTORY)
        s_ring_count++;
    pthread_mutex_unlock(&s_live_lock);
}

void video_stats_add_decode(unsigned long long decode_us) {
    pthread_mutex_lock(&s_stats_lock);
    s_stats.decode_us_total += decode_us;
    s_stats.decodes++;
    pthread_mutex_unlock(&s_stats_lock);

    pthread_mutex_lock(&s_live_lock);
    s_pending_decode_us = decode_us;
    pthread_mutex_unlock(&s_live_lock);
}

void video_stats_add_bounce(unsigned long long bounce_us) {
    pthread_mutex_lock(&s_stats_lock);
    s_stats.convert_us_total += bounce_us;
    s_stats.bounce_us_total += bounce_us;
    pthread_mutex_unlock(&s_stats_lock);
    pthread_mutex_lock(&s_live_lock);
    s_pending_bounce_us = bounce_us;
    pthread_mutex_unlock(&s_live_lock);
}

void video_stats_add_au(unsigned long long au_us, unsigned long long au_bytes) {
    pthread_mutex_lock(&s_stats_lock);
    s_stats.au_us_total += au_us;
    s_stats.au_bytes_total += au_bytes;
    pthread_mutex_unlock(&s_stats_lock);

    pthread_mutex_lock(&s_live_lock);
    s_pending_bytes = au_bytes;
    pthread_mutex_unlock(&s_live_lock);
}

void video_stats_get_live(video_live_stats_t *out) {
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&s_live_lock);
    int n = s_ring_count;
    if (n == 0) {
        pthread_mutex_unlock(&s_live_lock);
        return;
    }
    int oldest = (s_ring_head - n + VIDEO_STATS_HISTORY) % VIDEO_STATS_HISTORY;
    uint64_t current_ts = now_us();
    uint64_t window_us = (uint64_t)VIDEO_STATS_WINDOW_MS * 1000ull;
    uint64_t window_start = (current_ts > window_us) ? current_ts - window_us : 0;

    unsigned long long sum_decode = 0, sum_convert = 0, sum_present = 0, sum_bytes = 0;
    int win_count = 0;
    uint64_t first_ts_in_window = current_ts;
    int out_n = 0;

    for (int i = 0; i < n; i++) {
        int idx = (oldest + i) % VIDEO_STATS_HISTORY;
        const live_sample_t *s = &s_ring[idx];
        if (out_n < VIDEO_STATS_HISTORY) {
            int prev = (idx - 1 + VIDEO_STATS_HISTORY) % VIDEO_STATS_HISTORY;
            /* Delivery interval, not sum of overlapping pipeline work. */
            out->frame_ms[out_n++] = i ? (float)(s->ts_us - s_ring[prev].ts_us) / 1000.0f : 0;
        }
        if (s->ts_us >= window_start) {
            if (win_count == 0)
                first_ts_in_window = s->ts_us;
            sum_decode += s->decode_us;
            sum_convert += s->convert_us;
            sum_present += s->present_us;
            sum_bytes += s->bytes;
            win_count++;
        }
    }
    out->frame_count = out_n;

    if (win_count > 0) {
        double win_s = (double)(current_ts - first_ts_in_window) / 1e6;
        if (win_s < 0.2)
            win_s = 0.2; /* avoid absurd fps right after startup */
        out->fps = (float)((double)(win_count - 1) / win_s);
        out->decode_ms = (float)((double)sum_decode / win_count / 1000.0);
        out->convert_ms = (float)((double)sum_convert / win_count / 1000.0);
        out->present_ms = (float)((double)sum_present / win_count / 1000.0);
        out->kb_per_frame = (float)((double)sum_bytes / win_count / 1024.0);
    }
    pthread_mutex_unlock(&s_live_lock);
}

/* Called before starting a new session, with the decoder stopped. */
void video_stats_begin_session(void) {
    video_reset_stats();
    pthread_mutex_lock(&s_live_lock);
    memset(s_ring, 0, sizeof(s_ring));
    s_ring_head = s_ring_count = 0;
    s_pending_decode_us = s_pending_bytes = s_pending_bounce_us = 0;
    pthread_mutex_unlock(&s_live_lock);
}

void video_take_stats(video_stats_t *out) {
    pthread_mutex_lock(&s_stats_lock);
    *out = s_stats;
    memset(&s_stats, 0, sizeof(s_stats));
    pthread_mutex_unlock(&s_stats_lock);
}
