/* Native layouts derived from design/ui-concepts; included by ui_menu.c. */
static int design_sidebar(void) {
    int theme=ui_theme_index();return theme==1||theme==3||theme==4;
}
static void design_art(ui_state_t *st,int index,ui_surface_t *s,int x,int y,int w,int h,int radius) {
    if(!strcasecmp(st->apps[index].name,"Desktop")) {
        ui_landscape(s,x,y,w,h,radius);
        if(h>300)ui_icon(s,x+w/2-60,y+h/2-40,120,0,0xfff4f9ff);
    } else ui_art_draw_round(st->server,st->apps[index].id,s,x,y,w,h,radius);
}
static void design_profile(ui_state_t *st,char *text,size_t size) {
    snprintf(text,size,"%dp / %d FPS target",st->cfg->stream.height,st->cfg->stream.fps);
}
static void design_play(ui_surface_t *s,int x,int y,int width,int theme) {
    uint32_t fill=theme==1?COL_TEXT:theme==3?COL_ACCENT:theme==2?0xff96e5ff:0xff106676;
    uint32_t ink=theme==1?COL_BG:theme==2?0xff092443:0xfff5fbff;
    ui_round_rect(s,x,y,width,72,theme==1?0:14,fill);
    if(theme==0||theme==4)ui_focus(s,x,y,width,72,14,COL_ACCENT,8);
    int center=x+width/2;ui_icon(s,center-74,y+22,30,3,ink);
    ui_label(s,center-25,y+10,36,1,ink,"Play");
}
static void design_footer(ui_state_t *st,ui_surface_t *s) {
    int mono=ui_theme_index()==1;
    ui_overlay(s,0,980,UI_W,100,COL_BG,ui_theme_index()==3?25:185);
    ui_rect(s,64,980,1792,1,COL_DIM);
    uint32_t cross=mono?COL_TEXT:0xff65bcff,back=mono?COL_TEXT:0xffff727d,refresh=mono?COL_TEXT:0xff69e4bd;
    ui_icon(s,72,1014,38,7,cross);ui_label(s,124,1013,26,0,COL_TEXT,st->tab==TAB_APPS?"Play":st->tab==TAB_PCS?"Connect":"Edit");
    ui_icon(s,275,1014,38,6,back);ui_label(s,327,1013,26,0,COL_TEXT,"Back");
    ui_icon(s,464,1014,38,8,refresh);ui_label(s,516,1013,26,0,COL_TEXT,st->tab==TAB_SETTINGS?"Change":"Refresh");
    ui_round_rect(s,736,1014,57,38,5,COL_DIM);ui_round_rect(s,738,1016,53,34,4,COL_BG);
    ui_label(s,745,1014,23,0,COL_TEXT,"L1");ui_label(s,808,1014,23,0,COL_DIM,"/");
    ui_round_rect(s,835,1014,57,38,5,COL_DIM);ui_round_rect(s,837,1016,53,34,4,COL_BG);
    ui_label(s,844,1014,23,0,COL_TEXT,"R1");ui_label(s,911,1013,26,0,COL_TEXT,"Tabs");
    char profile[80];design_profile(st,profile,sizeof(profile));
    ui_label(s,UI_W-64-ui_label_w(24,0,profile),1015,24,0,COL_DIM,profile);
}
static void design_shell(ui_state_t *st,ui_surface_t *s) {
    int theme=ui_theme_index(),side=design_sidebar();
    ui_theme_background(s);
    if(theme==4&&st->tab==TAB_APPS&&st->napps) {
        design_art(st,st->sel_app,s,0,0,UI_W,980,0);
        /* Fade the hero into readable navigation/title and thumbnail regions. */
        for(int x=0;x<UI_W;x+=8){int alpha=245-x*170/UI_W;ui_overlay(s,x,0,8,980,0xff030d15,alpha);}
        for(int y=580;y<980;y+=4)ui_overlay(s,0,y,UI_W,4,COL_BG,(y-580)*170/400);
    }
    int logo_size=theme==3?34:theme==1?48:42;
    ui_label(s,64,48,logo_size,theme!=1&&theme!=3,COL_TEXT,"MOONLIGHT");
    if(theme==3)ui_label(s,66,96,24,0,COL_ACCENT,"PS4");
    else ui_label(s,64+ui_label_w(logo_size,theme!=1&&theme!=3,"MOONLIGHT")+18,62,28,0,COL_DIM,"PS4");
    const char *tabs[]={"PCs","Games","Settings"};int icons[]={0,1,2};
    if(side) {
        int order[]={0,1,2};if(theme==1){order[0]=1;order[1]=0;}
        int rail=theme==3?390:320;
        if(theme!=4)ui_rect(s,rail,theme==1?200:24,1,theme==1?740:930,COL_DIM);
        for(int i=0;i<3;i++) {
            int tab=order[i],y=250+i*118,selected=st->tab==tab;
            uint32_t ink=selected&&theme==1?COL_BG:selected&&theme==3?0xfff8fbff:selected?COL_ACCENT:COL_DIM;
            if(selected) {
                ui_round_rect(s,48,y-8,rail-72,80,theme==1?0:12,theme==1?COL_TEXT:theme==3?COL_ACCENT:COL_PANEL);
                if(theme==4)ui_round_rect(s,48,y-8,4,80,2,COL_ACCENT);
            }
            ui_icon(s,76,y+9,40,icons[tab],ink);ui_label(s,140,y+3,30,selected,ink,tabs[tab]);
        }
    } else {
        int y=theme==2?168:60,x0=theme==2?64:720;
        for(int i=0;i<3;i++) {
            int x=x0+i*245,selected=st->tab==i;
            if(theme==2)ui_round_rect(s,x,y-8,245,72,8,selected?COL_SEL:COL_PANEL);
            ui_label(s,x+40,y,32,selected,selected?COL_TEXT:COL_DIM,tabs[i]);
            if(selected){int width=theme==2?212:ui_label_w(32,1,tabs[i]);ui_rect(s,theme==2?x+16:x+40,y+53,width,3,COL_ACCENT);}
        }
    }
    int connected=st->server&&st->server->paired;
    if(theme==3) {
        ui_rect(s,64,820,290,1,COL_DIM);ui_icon(s,70,857,44,0,COL_DIM);
        ui_label(s,132,840,26,1,COL_TEXT,"Gaming PC");ui_circle(s,138,896,6,connected?0xff42cc94:COL_WARN);
        ui_label(s,154,880,24,0,COL_DIM,connected?"Connected":"Disconnected");
    } else if(theme!=4) {
        int hx=theme==1?1160:1500;
        if(theme!=1)ui_round_rect(s,hx-25,36,365,94,24,COL_PANEL);
        ui_icon(s,hx,58,42,0,COL_TEXT);ui_label(s,hx+66,42,26,0,COL_TEXT,"Gaming PC");
        ui_circle(s,hx+73,101,6,connected?0xff42dfaa:COL_WARN);
        ui_label(s,hx+90,85,22,0,connected?COL_ACCENT:COL_WARN,connected?"Connected":"Disconnected");
    }
    if(theme==1)ui_rect(s,64,174,1792,1,COL_DIM);
    if(theme==3) {
        ui_label(s,440,48,48,1,COL_TEXT,tabs[st->tab]);
        char subtitle[180];snprintf(subtitle,sizeof(subtitle),st->tab==TAB_APPS?"%d games available on Gaming PC":"Configure your streaming experience",st->napps);
        if(st->tab==TAB_PCS)snprintf(subtitle,sizeof(subtitle),"Connect to Sunshine on your local network");
        ui_label(s,440,115,27,0,COL_DIM,subtitle);
    }
}
static void design_apps(ui_state_t *st,ui_surface_t *s) {
    int theme=ui_theme_index(),start_x=design_sidebar()?440:100;
    if(st->apps_err||!st->napps) {
        ui_label(s,start_x,280,36,1,COL_TEXT,st->apps_err?"Unable to load games":"No games available");
        ui_label(s,start_x,342,26,0,COL_DIM,"Press TRIANGLE to refresh, or check your PC connection.");return;
    }
    int count=theme==1?5:theme==2?6:3;
    int first=st->sel_app/count*count;
    if(theme==1) {
        if(st->sel_app<st->app_first)st->app_first=st->sel_app;
        if(st->sel_app>=st->app_first+5)st->app_first=st->sel_app-4;
        first=st->app_first;
        ui_rect(s,1350,218,1,700,COL_DIM);
        for(int i=0;i<5&&first+i<st->napps;i++) {
            int idx=first+i,y=240+i*125,sel=idx==st->sel_app;
            ui_rect(s,354,y,950,105,COL_DIM);ui_rect(s,355,y+1,948,103,sel?COL_TEXT:COL_BG);
            uint32_t ink=sel?COL_BG:COL_TEXT;ui_icon(s,392,y+32,38,4,ink);
            ui_label_fit(s,478,y+22,36,sel,ink,st->apps[idx].name,720);
            if(sel)ui_icon(s,1240,y+34,36,3,ink);
        }
        ui_label_fit(s,1404,232,36,1,COL_TEXT,st->apps[st->sel_app].name,450);
        ui_rect(s,1404,320,450,1,COL_DIM);ui_icon(s,1410,364,44,0,COL_TEXT);
        ui_label(s,1480,352,28,0,COL_TEXT,"Gaming PC");ui_label(s,1480,400,24,0,COL_DIM,"Connected");
        ui_rect(s,1404,482,450,1,COL_DIM);design_play(s,1404,516,450,theme);
    } else if(theme==4) {
        ui_icon(s,380,252,44,0,COL_TEXT);ui_label(s,450,234,28,0,COL_TEXT,"Gaming PC");
        ui_circle(s,457,301,6,0xff49ecb8);ui_label(s,477,283,25,0,COL_ACCENT,"Connected");
        ui_label_fit(s,380,356,64,1,COL_TEXT,st->apps[st->sel_app].name,1390);
        design_play(s,380,483,360,theme);
        char profile[80];design_profile(st,profile,sizeof(profile));ui_icon(s,382,590,30,2,COL_DIM);ui_label(s,430,584,26,0,COL_DIM,profile);
        for(int i=0;i<3&&first+i<st->napps;i++) {
            int idx=first+i,x=64+i*610,y=708;
            design_art(st,idx,s,x,y,580,232,12);
            ui_overlay(s,x,y+154,580,78,COL_BG,195);
            ui_label_fit(s,x+22,y+172,28,1,COL_TEXT,st->apps[idx].name,540);
            if(idx==st->sel_app)ui_focus(s,x,y,580,232,12,COL_ACCENT,12);
        }
    } else if(theme==0) {
        for(int i=0;i<3&&first+i<st->napps;i++) {
            int idx=first+i,sel=idx==st->sel_app,x=64+i*608,y=198,width=570,height=594;
            design_art(st,idx,s,x,y,width,height,16);
            if(sel)ui_focus(s,x,y,width,height,16,COL_ACCENT,15);
            int title_width=ui_label_w(32,sel,st->apps[idx].name);
            int tx=x+(width-title_width)/2;if(tx<x+8)tx=x+8;
            ui_label_fit(s,tx,810,32,sel,sel?COL_TEXT:COL_DIM,st->apps[idx].name,width-16);
            if(sel)design_play(s,x+110,875,350,theme);
        }
    } else if(theme==2) {
        for(int i=0;i<6;i++) {
            int x=64+(i%3)*608,y=i<3?282:742,width=570,height=i<3?422:188;
            int idx=first+i,sel=idx==st->sel_app;
            if(idx>=st->napps){ui_round_rect(s,x,y,width,height,12,COL_PANEL);continue;}
            design_art(st,idx,s,x,y,width,height,12);
            if(sel)ui_focus(s,x,y,width,height,12,0xffdcfaff,16);
            if(i<3&&sel)design_play(s,x+14,y+height-82,width-28,theme);
            else {
                ui_overlay(s,x,y+height-67,width,67,COL_BG,180);
                ui_label_fit(s,x+24,y+height-54,30,1,COL_TEXT,st->apps[idx].name,width-48);
            }
        }
    } else { /* Daylight: two columns, persistent navigation and soft cards. */
        for(int i=0;i<3&&first+i<st->napps;i++) {
            int idx=first+i,sel=idx==st->sel_app,x=440+(i%2)*706,y=214+(i/2)*346,width=666,height=256;
            ui_round_rect(s,x+3,y+8,width,312,14,0xffe1ded8);
            ui_round_rect(s,x,y,width,310,12,COL_PANEL);
            design_art(st,idx,s,x,y,width,height,12);
            ui_rect(s,x,y+height-12,width,12,COL_PANEL);
            ui_label_fit(s,x+22,y+height+5,28,sel,COL_TEXT,st->apps[idx].name,width-44);
            if(sel)ui_focus(s,x,y,width,310,12,COL_ACCENT,0);
        }
        design_play(s,1166,572,626,theme);
        ui_rect(s,1166,676,626,1,COL_DIM);
        ui_label_fit(s,1166,700,26,1,COL_TEXT,st->apps[st->sel_app].name,626);
        ui_label(s,1166,748,24,0,COL_DIM,"Launch on your gaming PC and start streaming.");
        ui_rect(s,1166,809,626,1,COL_DIM);
        ui_icon(s,1170,840,32,0,COL_DIM);ui_label(s,1220,831,24,0,COL_DIM,"Gaming PC");
        char profile[80];design_profile(st,profile,sizeof(profile));
        ui_label(s,1166,884,24,0,COL_ACCENT,profile);
    }
    if(st->napps>count) {
        char position[40];snprintf(position,sizeof(position),"%d / %d",st->sel_app+1,st->napps);
        ui_label(s,design_sidebar()?440:64,942,22,0,COL_DIM,position);
    }
}
