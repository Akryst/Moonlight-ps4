#include "ui_stream.h"
#include "ui_art.h"
#include <math.h>
#include <stdio.h>

#define PANEL 0xff111214u
#define TEXT 0xfff3f1ecu
#define DIM 0xffa5a7aau
#define RULE 0xff343638u
static int scale(const ui_surface_t *s,int value){return value*s->h/1080;}
static void right_label(ui_surface_t *s,int x,int y,int width,int size,uint32_t color,const char *text){
    int measured=ui_label_w(size,0,text);
    if(measured<=width)ui_label(s,x+width-measured,y,size,0,color,text);
    else ui_label_fit(s,x,y,size,0,color,text,width);
}
static void control_hint(ui_surface_t *s,int button,int x,int y,const char *text){
    int key=scale(s,38);
    ui_control_image(s,button,x,y,key);
    ui_label(s,x+key+scale(s,10),y+scale(s,5),scale(s,22),0,TEXT,text);
}

void ui_stream_menu(ui_surface_t *s,const qm_view_t *m){
    if(!m || !m->open || s->h<360)return;
    int margin=scale(s,42),w=scale(s,550),h=scale(s,780);
    int x=s->w-margin-w,y=(s->h-h)/2,pad=scale(s,32);
    ui_round_rect_alpha(s,x,y,w,h,scale(s,20),PANEL,248);
    ui_moonlight_logo(s,x+pad+scale(s,12),y+scale(s,38),scale(s,12),TEXT);
    ui_label(s,x+pad+scale(s,36),y+scale(s,20),scale(s,24),0,TEXT,"moonlight");
    ui_label(s,x+pad,y+scale(s,78),scale(s,36),0,TEXT,"Stream controls");
    ui_label(s,x+pad,y+scale(s,134),scale(s,21),0,DIM,"Game input is paused");
    ui_rect(s,x+pad,y+scale(s,187),w-pad*2,scale(s,1),RULE);
    static const char *names[QM_ROW_COUNT]={"Touchpad mouse","Statistics","Bitrate","Return to library","Close game on PC"};
    for(int i=0;i<QM_ROW_COUNT;i++){
        int row_y=y+scale(s,211+i*78+(i>=3?28:0));
        int selected=m->sel==i;
        if(selected)ui_round_rect(s,x+scale(s,18),row_y,w-scale(s,36),scale(s,66),scale(s,10),TEXT);
        if(i==3)ui_rect(s,x+pad,row_y-scale(s,18),w-pad*2,scale(s,1),RULE);
        uint32_t fg=selected?PANEL:TEXT;
        int has_value=i<=QM_ROW_BITRATE;
        int value_w=scale(s,112),text_w=w-pad*2-(has_value?value_w+scale(s,16):0);
        ui_label_fit(s,x+pad,row_y+scale(s,16),scale(s,26),0,fg,names[i],text_w);
        char value[24];
        if(i==QM_ROW_TOUCHPAD || i==QM_ROW_OVERLAY){
            int on=i==QM_ROW_TOUCHPAD?m->touchpad:m->overlay;
            snprintf(value,sizeof(value),"%s",on?"On":"Off");
        }else if(i==QM_ROW_BITRATE)snprintf(value,sizeof(value),"%d Mbps",m->bitrate/1000);
        else continue;
        right_label(s,x+w-pad-value_w,row_y+scale(s,17),value_w,scale(s,24),fg,value);
    }
    int info_y=y+scale(s,657);
    if(m->sel==QM_ROW_BITRATE){
        ui_label(s,x+pad,info_y,scale(s,20),0,DIM,"Left / right adjusts the bitrate.");
        ui_label(s,x+pad,info_y+scale(s,28),scale(s,20),0,DIM,"Reconnects when you resume.");
    }else if(m->sel==QM_ROW_QUIT_APP){
        ui_label(s,x+pad,info_y,scale(s,20),0,DIM,"Closes the running game on your PC.");
    }else if(m->sel==QM_ROW_QUIT){
        ui_label(s,x+pad,info_y,scale(s,20),0,DIM,"Your game keeps running on the PC.");
    }
    control_hint(s,UI_CROSS,x+pad,y+h-scale(s,63),"Select");
    control_hint(s,UI_CIRCLE,x+scale(s,278),y+h-scale(s,63),"Resume");
}

static float metric(float value){return isfinite(value)&&value>0?fminf(value,9999.0f):0;}
void ui_stream_stats(ui_surface_t *s,const video_live_stats_t *v){
    if(!v || s->h<360)return;
    int x=scale(s,32),y=scale(s,32),w=scale(s,350),h=scale(s,374),pad=scale(s,24);
    ui_round_rect_alpha(s,x,y,w,h,scale(s,14),PANEL,237);
    char text[48];snprintf(text,sizeof(text),"%.1f",(double)fminf(metric(v->fps),999.9f));
    ui_label(s,x+pad,y+scale(s,17),scale(s,44),0,TEXT,text);
    int fps_width=ui_label_w(scale(s,44),0,text);
    ui_label(s,x+pad+fps_width+scale(s,8),y+scale(s,42),scale(s,18),0,DIM,"FPS");
    snprintf(text,sizeof(text),"%dp",s->h);
    right_label(s,x+w-pad-scale(s,90),y+scale(s,27),scale(s,90),scale(s,20),DIM,text);
    ui_rect(s,x+pad,y+scale(s,88),w-pad*2,scale(s,1),RULE);
    static const char *labels[]={"Decode","Convert","Present","Receive","Host","Video bitrate"};
    const float values[]={v->decode_ms,v->convert_ms,v->present_ms,v->net_recv_ms,v->host_ms,v->rx_mbps};
    for(int i=0;i<6;i++){
        int row_y=y+scale(s,105+i*31);
        ui_label(s,x+pad,row_y,scale(s,20),0,DIM,labels[i]);
        if(i==4 && metric(values[i])==0)snprintf(text,sizeof(text),"--");
        else snprintf(text,sizeof(text),"%.1f %s",(double)metric(values[i]),i==5?"Mbps":"ms");
        right_label(s,x+w-pad-scale(s,128),row_y,scale(s,128),scale(s,20),TEXT,text);
    }
    int gx=x+pad,gy=y+scale(s,310),gw=w-pad*2,gh=scale(s,35);
    ui_rect(s,gx,gy,gw,gh,0xff1c1e20);
    int count=v->frame_count;if(count<0)count=0;if(count>VIDEO_STATS_HISTORY)count=VIDEO_STATS_HISTORY;
    int bar=scale(s,2);if(bar<1)bar=1;
    int capacity=gw/(bar+1),shown=count<capacity?count:capacity;
    float maximum=33.4f;
    for(int i=count-shown;i<count;i++)maximum=fmaxf(maximum,metric(v->frame_ms[i]));
    for(int i=0;i<shown;i++){
        float ms=metric(v->frame_ms[count-shown+i]);
        int bh=(int)(ms/maximum*(gh-2));if(bh<1)bh=1;
        uint32_t color=ms>33.4f?0xffc98686:ms>17.5f?0xffc7ba91:0xffb9c3bc;
        ui_rect(s,gx+gw-shown*(bar+1)+i*(bar+1),gy+gh-bh,bar,bh,color);
    }
}

void ui_stream_hint(ui_surface_t *s,unsigned opacity){
    if(s->h<360)return;
    int w=scale(s,654),h=scale(s,60),x=(s->w-w)/2,y=s->h-h-scale(s,30);
    ui_round_rect_alpha(s,x,y,w,h,scale(s,12),PANEL,opacity);
    int cy=y+scale(s,15);
    ui_label(s,x+scale(s,22),cy,scale(s,21),0,TEXT,"Hold L3 + R3");
    ui_label(s,x+scale(s,180),cy,scale(s,21),0,DIM,"Stream menu");
    ui_rect(s,x+scale(s,325),y+scale(s,17),scale(s,1),scale(s,27),RULE);
    ui_control_image(s,UI_L1,x+scale(s,344),y+scale(s,12),scale(s,36));
    ui_control_image(s,UI_R1,x+scale(s,385),y+scale(s,12),scale(s,36));
    ui_control_image(s,UI_OPTIONS,x+scale(s,426),y+scale(s,12),scale(s,36));
    ui_label(s,x+scale(s,478),cy,scale(s,21),0,DIM,"Guide (PS)");
}
void ui_stream_hold(ui_surface_t *s,int progress){
    if(progress<=0 || s->h<360)return;
    if(progress>1000)progress=1000;
    int w=scale(s,270),h=scale(s,64),x=(s->w-w)/2,y=s->h-scale(s,160);
    ui_round_rect_alpha(s,x,y,w,h,scale(s,12),PANEL,240);
    ui_label(s,x+scale(s,20),y+scale(s,10),scale(s,20),0,TEXT,"Opening stream menu");
    ui_rect(s,x+scale(s,20),y+scale(s,48),w-scale(s,40),scale(s,3),RULE);
    ui_rect(s,x+scale(s,20),y+scale(s,48),(w-scale(s,40))*progress/1000,scale(s,3),TEXT);
}
