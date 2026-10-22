#include <stdint.h>
int video_ui_begin(int w,int h);
int video_ui_present(const uint8_t *frame,int pitch);
void video_ui_end(void);
#include "../src/ui/ui_menu.c"
#include <assert.h>


static int saves,quits;
void log_printf(const char *fmt,...){(void)fmt;}
void log_notify(const char *fmt,...){(void)fmt;}
void log_set_file_enabled(int enabled,const char *path){(void)enabled;(void)path;}
int config_save(const app_config_t *cfg,const char *directory){(void)cfg;(void)directory;saves++;return 0;}
void ui_art_start(gs_server_t *server,const char *directory){(void)server;(void)directory;}
void ui_art_stop(void){}
void ui_art_clear(void){}
int gs_refresh_status(gs_server_t *server){(void)server;return GS_OK;}
int gs_applist(gs_server_t *server,app_entry_t **apps){(void)server;*apps=NULL;return GS_OK;}
void xml_applist_free(app_entry_t *apps){(void)apps;}
int gs_quit_app(gs_server_t *server){quits++;server->currentGame=0;return GS_OK;}

int main(void) {
    app_config_t cfg={0};gs_server_t server={0};
    ui_state_t st={0};st.cfg=&cfg;st.server=&server;st.result=-1000;
    st.napps=10;
    for(int i=0;i<10;i++){st.apps[i].id=i+1;snprintf(st.apps[i].name,CONFIG_MAX_APP,"Game %d",i+1);}
    apps_input(&st,MENU_BTN_RIGHT);assert(st.sel_app==1 && !strcmp(cfg.last_app,"2"));
    assert(st.selection_save_at>ui_now_us());st.sel_app=0;preselect_app(&st);assert(st.sel_app==1);
    server.currentGame=7;apps_input(&st,MENU_BTN_SQUARE);assert(st.sel_app==6 && st.result==-1000);
    apps_input(&st,MENU_BTN_OPTIONS);assert(st.close_confirm && quits==0);
    apps_input(&st,MENU_BTN_CIRCLE);assert(!st.close_confirm && quits==0);
    apps_input(&st,MENU_BTN_OPTIONS);apps_input(&st,MENU_BTN_CROSS);assert(quits==1 && !st.close_confirm);
    st.apps_err=2;apps_input(&st,MENU_BTN_RIGHT);apps_input(&st,MENU_BTN_CROSS);assert(st.tab==TAB_PCS);
    st.error_action=0;apps_input(&st,MENU_BTN_CROSS);assert(st.result==UI_MENU_RECONNECT);
    st.tab=TAB_SETTINGS;st.apps_err=0;st.sel_set=0;st.set_group=0;
    settings_input(&st,MENU_BTN_DOWN);assert(st.set_group==1 && !st.set_list);
    settings_input(&st,MENU_BTN_CROSS);assert(st.set_list && st.sel_set==0);
    settings_input(&st,MENU_BTN_CIRCLE);assert(!st.set_list);
    st.set_group=3;st.set_list=1;st.sel_set=0;cfg.controller_deadzone=25;
    settings_input(&st,MENU_BTN_RIGHT);assert(cfg.controller_deadzone==25);
    settings_input(&st,MENU_BTN_LEFT);assert(cfg.controller_deadzone==24);
    st.sel_set=1;settings_input(&st,MENU_BTN_CROSS);assert(cfg.swap_face_buttons);
    st.set_group=4;st.sel_set=1;settings_input(&st,MENU_BTN_CROSS);assert(cfg.reduced_motion);
    assert(saves>0);
    for(int count=1;count<=256;count++)for(int selected=0;selected<count;selected++) {
        int first=design_first(selected,count);assert(first>=0 && first<=selected && selected<first+5);
    }
    st.tab=TAB_APPS;st.motion_ready=0;st.sel_app=0;design_animate(&st,8000);
    st.sel_app=7;design_animate(&st,8000);assert(st.focus_position==7 && st.rail_position==5);
    cfg.reduced_motion=false;st.sel_app=6;design_animate(&st,8000);
    assert(st.focus_position>6 && st.focus_position<7);
    puts("Native menu: categories, preferences, selection, resume, close confirmation, recovery and rail bounds passed");
}

int config_toggle_title_motion(app_config_t *cfg,int id){(void)id;cfg->gyro_motion=!cfg->gyro_motion;return 0;}
bool config_title_motion_enabled(const app_config_t *cfg,int id){(void)id;return cfg->gyro_motion;}

int gs_applist_ex(client_identity_t *id,const char *host,unsigned short port,app_entry_t **apps){(void)id;(void)host;(void)port;*apps=NULL;return GS_OK;}
int gs_serverinfo_status(client_identity_t *id,const char *h,unsigned short a,unsigned short b,bool *paired,int *game,char *mac,size_t cap){(void)id;(void)h;(void)a;(void)b;(void)cap;*paired=true;*game=0;if(mac)mac[0]=0;return GS_OK;}
