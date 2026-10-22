#include "../src/gamestream/launch_flow.c"
#include "../src/config.h"
#include <assert.h>
#include <stdarg.h>
static int starts,quits,fail,mask;
void log_printf(const char *fmt,...){(void)fmt;}
int gs_quit_app(gs_server_t *s){quits++;s->currentGame=0;return GS_OK;}
int gs_refresh_status(gs_server_t *s){s->currentGame=23;return GS_OK;}
int gs_start_app(gs_server_t *s,STREAM_CONFIGURATION *c,int id,bool ops,bool audio,int bitmap){(void)c;(void)ops;(void)audio;starts++;mask=bitmap;if(fail){gs_error="PC says display unavailable";return GS_ERROR;}s->currentGame=id;return GS_OK;}
int main(void){
    gs_server_t s={0};STREAM_CONFIGURATION stream={0};
    launch_progress_t p={.mutex=PTHREAD_MUTEX_INITIALIZER};
    assert(!launch_app_flow(&s,&stream,23,1,0,"Game",&p));assert(starts==1 && quits==0 && mask==15);
    s.currentGame=23;assert(!launch_app_flow(&s,&stream,23,1,0,"Game",&p));assert(quits==0);
    int saved_quits=quits;assert(!launch_resume_flow(&s,&stream,23,1,0,"Game",&p));assert(quits==saved_quits);
    p.cancelled=1;int before=starts;assert(launch_app_flow(&s,&stream,23,1,0,"Game",&p)==-2 && starts==before);
    p.cancelled=0;s.currentGame=0;fail=1;assert(launch_app_flow(&s,&stream,23,1,0,"Game",&p)==-1);
    char phase[128];launch_progress_read(&p,phase,sizeof(phase));assert(strstr(phase,"display unavailable"));
    app_config_t cfg={0};
    assert(config_title_motion_enabled(&cfg,23));assert(!config_toggle_title_motion(&cfg,23));
    assert(!config_title_motion_enabled(&cfg,23) && config_title_motion_enabled(&cfg,2));
    assert(!config_toggle_title_motion(&cfg,2));assert(!config_toggle_title_motion(&cfg,23));
    assert(config_title_motion_enabled(&cfg,23) && !config_title_motion_enabled(&cfg,2));
    pthread_mutex_destroy(&p.mutex);puts("Launch: resume preserves game, four-pad bitmap, cancel, PC error, per-title motion passed");
}
