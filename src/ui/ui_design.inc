/* Console library layout; included by ui_menu.c to share the real input state. */
static int design_sidebar(void) { return 0; }
static void design_art(ui_state_t *st,int index,ui_surface_t *s,int x,int y,int w,int h,int radius) {
    if(!strcasecmp(st->apps[index].name,"Desktop") || !strcasecmp(st->apps[index].name,"Playnite")) {
        ui_round_rect(s,x,y,w,h,radius,COL_PANEL);
        /* Quiet, original cover for Desktop; no oversized monitor or 3D ornament. */
        ui_moonlight_logo(s,x+w/2,y+h/2-28,w/6,COL_TEXT);
        ui_label(s,x+(w-ui_label_w(24,0,st->apps[index].name))/2,y+h*3/4,24,0,COL_TEXT,st->apps[index].name);
    } else ui_art_draw_round(st->server,st->apps[index].id,s,x,y,w,h,radius);
}
static void design_play(ui_surface_t *s,int x,int y,int resume) {
    ui_round_rect(s,x,y,240,72,10,COL_TEXT);
    /* Image sits on a dark circular key so it stays legible in every theme. */
    ui_control_image(s,UI_CROSS,x+17,y+13,46);
    ui_label(s,x+84,y+12,34,0,COL_BG,resume?"Resume":"Play");
}
static void design_footer(ui_state_t *st,ui_surface_t *s) {
    ui_overlay(s,0,974,UI_W,106,COL_BG,240);
    ui_rect(s,96,974,1728,1,COL_PANEL);
    ui_control_image(s,UI_DPAD,96,1000,44);ui_label(s,155,1006,25,0,COL_DIM,"Navigate");
    ui_control_image(s,UI_CROSS,382,1000,44);ui_label(s,441,1006,25,0,COL_DIM,st->tab==TAB_APPS?(st->apps_err?"Select":"Play / resume"):st->tab==TAB_PCS?"Connect":st->set_list?"Change":"Open");
    ui_control_image(s,UI_CIRCLE,650,1000,44);ui_label(s,709,1006,25,0,COL_DIM,"Back");
    if(st->tab!=TAB_SETTINGS) {
        ui_control_image(s,UI_TRIANGLE,890,1000,44);ui_label(s,949,1006,25,0,COL_DIM,"Refresh");
    }
    if(st->tab==TAB_APPS && st->server && st->server->currentGame) {
        ui_control_image(s,UI_SQUARE,1180,1000,44);ui_label(s,1239,1006,25,0,COL_DIM,"Resume");
        ui_control_image(s,UI_OPTIONS,1460,1000,44);ui_label(s,1519,1006,25,0,COL_DIM,"Close session");
    }
}
static void design_shell(ui_state_t *st,ui_surface_t *s) {
    ui_theme_background(s);
    if(st->tab==TAB_APPS && st->napps && strcasecmp(st->apps[st->sel_app].name,"Desktop") &&
       strcasecmp(st->apps[st->sel_app].name,"Steam Big Picture") && strcasecmp(st->apps[st->sel_app].name,"Playnite")) {
        ui_art_draw(st->server,st->apps[st->sel_app].id,s,0,0,UI_W,974);
        for(int x=0;x<UI_W;x+=16)ui_overlay(s,x,0,16,974,COL_BG,242-x*170/UI_W);
        for(int y=400;y<974;y+=8)ui_overlay(s,0,y,UI_W,8,COL_BG,(y-400)*245/574);
    }
    ui_overlay(s,0,0,UI_W,144,COL_BG,150);
    ui_moonlight_logo(s,118,84,23,COL_TEXT);
    ui_label(s,165,57,36,0,COL_TEXT,"moonlight");
    const char *tabs[]={"Computers","Library","Settings"};
    ui_control_image(s,UI_L1,698,61,46);
    for(int i=0;i<3;i++) {
        int x=784+i*288,selected=(int)st->tab==i;
        ui_label(s,x,63,30,0,selected?COL_TEXT:COL_DIM,tabs[i]);
        if(selected)ui_rect(s,x,116,ui_label_w(30,0,tabs[i]),3,COL_TEXT);
    }
    ui_control_image(s,UI_R1,1648,61,46);
    ui_label(s,1774,67,24,0,COL_DIM,"PS4");
}
static int design_first(int selected,int count) {
    int first=selected>0?selected-1:0;
    int last=count>5?count-5:0;
    return first<last?first:last;
}
static void design_apps(ui_state_t *st,ui_surface_t *s) {
    if(st->apps_err || !st->napps) {
        ui_label(s,96,270,58,0,COL_TEXT,st->apps_err?"Let's reconnect.":"Your library starts here.");
        ui_label(s,100,365,28,0,COL_DIM,"Refresh your library or choose a computer to get started.");
        ui_round_rect(s,96,470,290,72,10,st->error_action?COL_PANEL:COL_TEXT);
        ui_label(s,140,484,30,0,st->error_action?COL_TEXT:COL_BG,"Retry");
        ui_round_rect(s,420,470,430,72,10,st->error_action?COL_TEXT:COL_PANEL);
        ui_label(s,460,484,30,0,st->error_action?COL_BG:COL_TEXT,"Choose computer");
        return;
    }
    ui_label(s,98,188,23,0,COL_DIM,st->server && st->server->currentGame==st->apps[st->sel_app].id?"CONTINUE WHERE YOU LEFT OFF":"READY WHEN YOU ARE");
    ui_label_fit(s,92,241,72,0,COL_TEXT,st->apps[st->sel_app].name,1680);
    design_play(s,96,365,st->server && st->server->currentGame==st->apps[st->sel_app].id);
    if(!st->motion_ready) {
        st->rail_position=(float)design_first(st->sel_app,st->napps);
        st->focus_position=(float)st->sel_app;st->motion_ready=1;
    }
    int first=(int)st->rail_position-1;if(first<0)first=0;
    for(int index=first;index<first+7 && index<st->napps;index++) {
        float distance=(float)index-st->focus_position;if(distance<0)distance=-distance;
        float emphasis=distance<1?1-distance:0;
        int x=96+(int)(((float)index-st->rail_position)*352);
        if(x>1824 || x+294<64)continue;
        int y=532-(int)(32*emphasis),w=266+(int)(28*emphasis),h=399+(int)(42*emphasis);
        ui_round_rect(s,x+4,y+13,w,h,10,0xff05080e);
        design_art(st,index,s,x,y,w,h,8);
        if(index==st->sel_app)ui_focus(s,x,y,w,h,8,COL_TEXT,0);
        else ui_overlay(s,x,y,w,h,COL_BG,(int)(72*(1-emphasis)));
    }
}
static int design_animate(ui_state_t *st,uint64_t elapsed) {
    if(st->tab!=TAB_APPS || !st->napps)return 0;
    float rail=(float)design_first(st->sel_app,st->napps),focus=(float)st->sel_app;
    if(!st->motion_ready){st->rail_position=rail;st->focus_position=focus;st->motion_ready=1;return 1;}
    float blend=st->cfg->reduced_motion?1:(float)elapsed*16/1000000;
    if(blend>1)blend=1;
    float dr=rail-st->rail_position,df=focus-st->focus_position;
    int moving=dr>0.002f || dr< -0.002f || df>0.002f || df< -0.002f;
    if(moving){st->rail_position+=dr*blend;st->focus_position+=df*blend;}
    else {st->rail_position=rail;st->focus_position=focus;}
    return moving;
}
