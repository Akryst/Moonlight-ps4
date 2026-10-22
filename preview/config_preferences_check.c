#include "../src/config.h"
#include "../src/input/stick_deadzone.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void log_printf(const char *fmt,...){(void)fmt;}
void LiInitializeStreamConfiguration(PSTREAM_CONFIGURATION cfg){memset(cfg,0,sizeof(*cfg));}
int main(void) {
    char directory[]="/tmp/moonlight-preferences-XXXXXX";assert(mkdtemp(directory));
    app_config_t cfg,loaded;config_set_defaults(&cfg);assert(cfg.controller_deadzone==8);
    strcpy(cfg.host,"192.0.2.1");strcpy(cfg.last_app,"1729");
    assert(!config_toggle_title_motion(&cfg,1729));cfg.surround51=true;cfg.touchpad_mouse=false;
    cfg.reduced_motion=true;cfg.swap_face_buttons=true;cfg.controller_deadzone=15;
    assert(config_save(&cfg,directory)==0 && config_load(&loaded,directory)==0);
    assert(!config_title_motion_enabled(&loaded,1729) && loaded.surround51 && !loaded.touchpad_mouse);
    assert(!strcmp(loaded.last_app,"1729") && loaded.reduced_motion && loaded.swap_face_buttons && loaded.controller_deadzone==15);
    char path[256];snprintf(path,sizeof(path),"%s/moonlight.ini",directory);
    FILE *file=fopen(path,"w");assert(file);fputs("cfg_rev=2\nhost=192.0.2.1\ncontroller_deadzone=999\n",file);fclose(file);
    assert(config_load(&loaded,directory)==0 && loaded.controller_deadzone==8 && !loaded.reduced_motion && !loaded.last_app[0]);
    for(int zone=0;zone<=25;zone++) {
        int previous=-32768;
        for(int value=-32768;value<=32767;value++) {
            int adjusted=stick_deadzone((short)value,zone);assert(adjusted>=previous);previous=adjusted;
            if(value==0)assert(adjusted==0);
        }
    }
    assert(stick_deadzone(32767,0)==32767 && stick_deadzone(-32768,0)==-32768);
    assert(stick_deadzone(1000,8)==0 && stick_deadzone(-1000,8)==0);
    assert(!unlink(path) && !rmdir(directory));
    puts("Preferences: migration, persistence, invalid ranges and exhaustive stick deadzone monotonicity passed");
}
