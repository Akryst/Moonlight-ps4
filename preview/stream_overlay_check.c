#include "../src/ui/ui_stream.h"
#include "../src/ui/ui_art.h"
#include "../src/gamestream/gs_errors.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void save_ppm(const char *path,const ui_surface_t *s){
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",s->w,s->h);
    for(int y=0;y<s->h;y++)for(int x=0;x<s->w;x++){
        const unsigned char *p=s->fb+(size_t)y*s->pitch+x*4;
        unsigned char rgb[]={p[2],p[1],p[0]};assert(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);
}
int main(void){
    ui_controls_preload();
    const int sizes[][2]={{1920,1080},{1280,720},{854,480},{32,24}};
    for(unsigned r=0;r<sizeof(sizes)/sizeof(sizes[0]);r++){
        int w=sizes[r][0],h=sizes[r][1],pitch=w*4+32;size_t bytes=(size_t)pitch*h;
        unsigned char *allocation=malloc(bytes+64);assert(allocation);memset(allocation,0xa7,bytes+64);
        ui_surface_t s={allocation+32,pitch,w,h};
        video_live_stats_t v={0};v.fps=59.94f;v.decode_ms=6.2f;v.convert_ms=3.4f;v.present_ms=1.1f;
        v.net_recv_ms=0.8f;v.host_ms=2.3f;v.rx_mbps=18.7f;v.frame_count=VIDEO_STATS_HISTORY;
        for(int i=0;i<VIDEO_STATS_HISTORY;i++)v.frame_ms[i]=i%17?16.7f:25.0f;
        qm_view_t m={.open=1,.touchpad=1,.overlay=1,.bitrate=20000};
        for(int selected=0;selected<QM_ROW_COUNT;selected++){
            ui_clear(&s,0xff22262a);m.sel=selected;qm_view_t before=m;
            ui_stream_stats(&s,&v);ui_stream_menu(&s,&m);assert(!memcmp(&m,&before,sizeof(m)));
            if(r==0 && selected==2)save_ppm("build-tests/stream-overlays-1080.ppm",&s);
            if(r==1 && selected==4)save_ppm("build-tests/stream-overlays-720.ppm",&s);
        }
        const int counts[]={-1,0,1,VIDEO_STATS_HISTORY,VIDEO_STATS_HISTORY+1000};
        for(unsigned i=0;i<sizeof(counts)/sizeof(counts[0]);i++){
            v.frame_count=counts[i];v.fps=NAN;v.rx_mbps=INFINITY;v.frame_ms[0]=NAN;
            ui_stream_stats(&s,&v);ui_stream_hold(&s,2000);ui_stream_hint(&s,220);
        }
        for(int i=0;i<32;i++)assert(allocation[i]==0xa7 && allocation[bytes+32+i]==0xa7);
        for(int y=0;y<h;y++)for(int x=w*4;x<pitch;x++)assert(s.fb[(size_t)y*pitch+x]==0xa7);
        free(allocation);
    }
    puts("Stream overlays: all rows, viewport sizes, PNG prompts, history bounds, non-finite values and padded frame guards passed");
}
