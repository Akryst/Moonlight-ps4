#pragma once
/* Launch ladder with progress reporting and cancellation.
 *
 * This used to live in stream.c. It moved here so the host build (and the
 * mock-server tests) can exercise the exact ladder the PS4 runs, without
 * linking the video/input stack: launch_flow.c depends only on the
 * gamestream client and pthreads.
 *
 * Field reports against a Sunshine fork showed the
 * client-side half of "click a game -> the app looks frozen": the ladder
 * below can legitimately spend tens of seconds in bounded HTTP attempts
 * (fresh /launch + status re-check + retry + /cancel + final retry) with
 * NOTHING drawn on screen between the menu closing and the first video
 * frame. The progress text + cancellation here feed the animated launch
 * screen (ui_launch_screen_run) so that wait is visible and cancellable
 * instead of reading as a hang. */

#include "client.h"

#include <stdatomic.h>
#include <pthread.h>
typedef struct {
    atomic_int cancelled,done,rc;
    pthread_mutex_t mutex;
    char phase[128];
} launch_progress_t;
void launch_progress_read(launch_progress_t *p,char *out,size_t cap);

/* Full launch ladder for app_id. Same recovery logic as the
 * stream.c:launch_app(), plus per-step progress text and cancellation
 * checks between the (bounded) HTTP attempts. display_name is optional
 * (shown in the phase text); pass NULL for a generic wording.
 * Returns 0 on success, -1 on failure (gs_error has the reason),
 * -2 when progress->cancelled was set. */
int launch_app_flow(gs_server_t *server, STREAM_CONFIGURATION *stream,
                    int app_id, bool sops, bool local_audio,
                    const char *display_name, launch_progress_t *progress);

/* Recovery after LiStartConnection failed against a LIVE session (the
 * launch or resume succeeded server-side, but the stream handshake did
 * not): /cancel the stuck session, then a clean /launch. This is the
 * "first click fails, second works" pattern collapsed into ONE click.
 * Same return codes as launch_app_flow(). */
int launch_recover_flow(gs_server_t *server, STREAM_CONFIGURATION *stream,
                        int app_id, bool sops, bool local_audio,
                        const char *display_name, launch_progress_t *progress);

int launch_refresh_flow(gs_server_t *server,int *app_id,const char *name,launch_progress_t *progress);
int launch_resume_flow(gs_server_t *server,STREAM_CONFIGURATION *stream,int app_id,bool sops,bool local_audio,const char *name,launch_progress_t *progress);
