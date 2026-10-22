/* Launch ladder implementation - see launch_flow.h for the contract. */

#include "launch_flow.h"
#include "gs_errors.h"
#include "../log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static void progress_set(launch_progress_t *p, const char *fmt, ...) {
    if (!p)
        return;
    va_list ap;
    pthread_mutex_lock(&p->mutex);
    va_start(ap, fmt);
    vsnprintf(p->phase, sizeof(p->phase), fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&p->mutex);
}

void launch_progress_read(launch_progress_t *p,char *out,size_t cap){
    pthread_mutex_lock(&p->mutex);snprintf(out,cap,"%s",p->phase);pthread_mutex_unlock(&p->mutex);
}

/* Cancellation checkpoint: call between the ladder's steps. */
static int flow_stopped(launch_progress_t *p) {
    return p && p->cancelled;
}

static const char *game_or(const char *display_name) {
    return display_name && display_name[0] ? display_name : "the game";
}

int launch_app_flow(gs_server_t *server, STREAM_CONFIGURATION *stream,
                    int app_id, bool sops, bool local_audio,
                    const char *display_name, launch_progress_t *progress) {
    progress_set(progress, "Preparing...");

    /* Sunshine cannot run two apps at once: cancel a different running
     * app before launching (same-app is the resume path, keep it). */
    if (server->currentGame != 0 && server->currentGame != app_id) {
        if (flow_stopped(progress))
            goto cancelled;
        progress_set(progress, "Closing the current session on your PC...");
        LOGI("launch flow: currentGame=%d != %d -> cancel previous",
             server->currentGame, app_id);
        (void)gs_quit_app(server);
    }

    if (flow_stopped(progress))
        goto cancelled;
    progress_set(progress, "Asking your PC to start '%s'...",
                 game_or(display_name));

    int rc = gs_start_app(server, stream, app_id, sops, local_audio, 0xf);
    if (rc == GS_OK)
        goto ok;

    LOGW("launch flow: attempt 1 FAIL: %s", gs_error ? gs_error : "?");
    if (flow_stopped(progress))
        goto cancelled;

    if (rc == GS_IO_ERROR) {
        /* Transport-level failure: the request may have reached the host.
         * Ask the server what actually happened before deciding. */
        progress_set(progress, "No answer - checking your PC...");
        if (gs_refresh_status(server) == GS_OK && server->currentGame == app_id) {
            LOGI("launch flow: launch response lost, but app %d is live "
                 "server-side; continuing", app_id);
            progress_set(progress, "'%s' is running - continuing...",
                         game_or(display_name));
            goto ok; /* LiStartConnection decides from here */
        }
        if (flow_stopped(progress))
            goto cancelled;
        progress_set(progress, "No answer - retrying...");
        LOGI("launch flow: launch retry (fresh connection)");
        if (gs_start_app(server, stream, app_id, sops, local_audio, 0xf) == GS_OK)
            goto ok;
        LOGW("launch flow: attempt 2 FAIL: %s", gs_error ? gs_error : "?");
    }

    /* Still failing: a stale session (e.g. a previous run died mid-stream
     * and Sunshine still holds the app busy) is the most common cause.
     * Cancel whatever runs, let the host settle for a moment, then make
     * one final attempt (01.46: the settle measurably helped forks that
     * take a moment to release the previous session). */
    if (server->currentGame != 0) {
        if (flow_stopped(progress))
            goto cancelled;
        progress_set(progress, "Session stuck - cleaning it up...");
        LOGI("launch flow: session held by app %d -> cancel and relaunch",
             server->currentGame);
        (void)gs_quit_app(server);
        if (flow_stopped(progress))
            goto cancelled;
        LOGI("launch flow: settle 600 ms, final attempt");
        usleep(600 * 1000);
        if (flow_stopped(progress))
            goto cancelled;
        progress_set(progress, "One more try...");
        if (gs_start_app(server, stream, app_id, sops, local_audio, 0xf) == GS_OK)
            goto ok;
        LOGW("launch flow: final attempt FAIL: %s", gs_error ? gs_error : "?");
    }
    progress_set(progress, "Could not start the game. (%s)",
                 gs_error ? gs_error : "no answer");
    return -1;

ok:
    /* Cancellation wins even over success: if the user pressed CIRCLE
     * while the (bounded) request was in flight, honor it - returning to
     * the menu is what "Cancelling..." promised. The host-side session
     * (if one started) is cleaned by the menu's session chip (OPTIONS)
     * or the next launch's cancel-previous step. */
    if (flow_stopped(progress))
        goto cancelled;
    progress_set(progress, "'%s' is starting on your PC...",
                 game_or(display_name));
    return 0;

cancelled:
    progress_set(progress, "Cancelled.");
    return -2;
}

int launch_recover_flow(gs_server_t *server, STREAM_CONFIGURATION *stream,
                        int app_id, bool sops, bool local_audio,
                        const char *display_name, launch_progress_t *progress) {
    progress_set(progress, "Recovering the session...");

    if (server->currentGame != 0) {
        if (flow_stopped(progress))
            goto cancelled;
        progress_set(progress, "Closing the stuck session...");
        LOGI("recover flow: cancelling app %d", server->currentGame);
        (void)gs_quit_app(server);
    }
    if (flow_stopped(progress))
        goto cancelled;
    progress_set(progress, "Starting '%s' cleanly...",
                 game_or(display_name));
    if (gs_start_app(server, stream, app_id, sops, local_audio, 0xf) == GS_OK) {
        if (flow_stopped(progress))
            goto cancelled;
        progress_set(progress, "'%s' is starting on your PC...",
                     game_or(display_name));
        return 0;
    }
    LOGW("recover flow: relaunch FAIL: %s", gs_error ? gs_error : "?");
    progress_set(progress, "Could not recover the session.");
    return -1;

cancelled:
    progress_set(progress, "Cancelled.");
    return -2;
}

int launch_refresh_flow(gs_server_t *server,int *app_id,const char *name,launch_progress_t *p){
    progress_set(p,"Refreshing games on your PC...");
    if(flow_stopped(p))return -2;
    (void)gs_refresh_status(server);
    if(flow_stopped(p))return -2;
    app_entry_t *apps=NULL;
    if(gs_applist(server,&apps)!=GS_OK){progress_set(p,"Could not refresh games: %s",gs_error?gs_error:"no answer");return -1;}
    int found=0;
    for(app_entry_t *a=apps;a;a=a->next)if(name && name[0] && !strcasecmp(a->name,name)){*app_id=a->id;found=1;break;}
    xml_applist_free(apps);
    if(flow_stopped(p))return -2;
    if(!found){progress_set(p,"This game is no longer in the PC's application list.");return -1;}
    return 0;
}
int launch_resume_flow(gs_server_t *server,STREAM_CONFIGURATION *stream,int app_id,bool sops,bool local_audio,const char *name,launch_progress_t *p){
    progress_set(p,"Reconnecting to your PC...");
    if(flow_stopped(p))return -2;
    if(gs_refresh_status(server)!=GS_OK){progress_set(p,"PC unavailable: %s",gs_error?gs_error:"no answer");return -1;}
    if(flow_stopped(p))return -2;
    if(server->currentGame && server->currentGame!=app_id){progress_set(p,"A different game is running on your PC.");return -1;}
    progress_set(p,"Resuming '%s'...",game_or(name));
    int rc=gs_start_app(server,stream,app_id,sops,local_audio,0xf);
    if(flow_stopped(p))return -2;
    if(rc!=GS_OK){progress_set(p,"Could not resume: %s",gs_error?gs_error:"no answer");return -1;}
    return 0;
}
