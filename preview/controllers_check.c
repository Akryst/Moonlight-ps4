#include "../src/input/input_pad.c"
#include <assert.h>
#include <stdio.h>
static OrbisPadData data[4];
static int32_t users[4]={11,22,33,44};
static uint64_t clock_us=1;
static int buttons[4],masks[4],arrivals[4],motion[4],moves,scrolls,mouse_down;
static OrbisPadColor leds[4];static OrbisPadVibeParam vibes[4];
void log_printf(const char *fmt,...){(void)fmt;}
uint64_t sceKernelGetProcessTime(void){return clock_us;}
int sceKernelUsleep(unsigned us){clock_us+=us;return 0;}
int32_t scePadInit(void){return 0;}
int32_t sceUserServiceGetInitialUser(int32_t *user){*user=users[0];return 0;}
int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList *list){memcpy(list->userId,users,sizeof(users));return 0;}
int32_t scePadOpen(int32_t user,int32_t type,int32_t index,void *param){(void)type;(void)index;(void)param;for(int i=0;i<4;i++)if(users[i]==user)return i+1;return -1;}
int32_t scePadClose(int32_t h){(void)h;return 0;}
int32_t scePadReadState(int32_t h,OrbisPadData *out){*out=data[h-1];return 0;}
int scePadReadStateExt(int h,OrbisPadData *out){*out=data[h-1];return 0;}
int32_t scePadSetLightBar(int32_t h,OrbisPadColor *color){leds[h-1]=*color;return 0;}
int32_t scePadSetVibration(int32_t h,const OrbisPadVibeParam *v){vibes[h-1]=*v;return 0;}
int LiSendMultiControllerEvent(short slot,short mask,int flags,unsigned char lt,unsigned char rt,short lx,short ly,short rx,short ry){(void)lt;(void)rt;(void)lx;(void)ly;(void)rx;(void)ry;buttons[slot]=flags;masks[slot]=mask;return 0;}
int LiSendControllerArrivalEvent(uint8_t slot,uint16_t mask,uint8_t type,uint32_t flags,uint16_t caps){(void)mask;(void)type;(void)flags;(void)caps;arrivals[slot]++;return 0;}
int LiSendControllerBatteryEvent(uint8_t s,uint8_t state,uint8_t percentage){(void)s;(void)state;(void)percentage;return 0;}
int LiSendControllerMotionEvent(uint8_t slot,uint8_t type,float x,float y,float z){(void)type;(void)x;(void)y;(void)z;motion[slot]++;return 0;}
int LiSendMouseMoveEvent(short x,short y){(void)x;(void)y;moves++;return 0;}
int LiSendMouseButtonEvent(char action,int button){(void)button;mouse_down=action==BUTTON_ACTION_PRESS;return 0;}
int LiSendHighResScrollEvent(short amount){scrolls+=amount;return 0;}
int LiSendHighResHScrollEvent(short amount){(void)amount;return 0;}
int main(void){
    for(int i=0;i<4;i++){data[i].connected=1;data[i].leftStick.x=data[i].leftStick.y=data[i].rightStick.x=data[i].rightStick.y=128;data[i].buttons=ORBIS_PAD_BUTTON_CROSS;}
    assert(!input_init());input_reset();assert(!input_poll());
    for(int i=0;i<4;i++)assert(arrivals[i]==1 && buttons[i]==A_FLAG && masks[i]==15);
    input_rumble(2,128,64);assert(vibes[2].lgMotor==128 && vibes[2].smMotor==64);
    input_set_led(1,12,34,56);assert(leds[1].r==12 && leds[1].g==34 && leds[1].b==56);
    input_leds_restore();assert(leds[1].r==255 && leds[1].g==0);
    data[3].connected=0;input_poll();assert(masks[3]==7 && buttons[3]==0);
    data[0].buttons=ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_R1|ORBIS_PAD_BUTTON_OPTIONS;
    input_poll();assert(buttons[0]==0);clock_us+=4000;input_poll();assert(buttons[0]==SPECIAL_FLAG);
    for(int i=0;i<200;i++){clock_us+=4000;input_poll();}assert(buttons[0]==0);
    data[0].buttons=0;input_poll();input_motion_set_state(0,LI_MOTION_TYPE_GYRO,250);
    input_poll();int before=motion[0];clock_us+=3999;input_poll();assert(motion[0]==before);
    clock_us++;input_poll();assert(motion[0]==before+1);input_set_motion_enabled(0);
    clock_us+=4000;input_poll();assert(motion[0]==before+1);
    input_set_touchpad_mouse(1);data[0].touch.fingers=1;data[0].touch.touch[0].finger=1;
    data[0].touch.touch[0].x=100;data[0].touch.touch[0].y=100;input_poll();
    data[0].touch.touch[0].x=150;input_poll();assert(moves>0);
    data[0].buttons=ORBIS_PAD_BUTTON_TOUCH_PAD;input_poll();assert(mouse_down);
    input_set_touchpad_mouse(0);assert(!mouse_down);
    input_shutdown();puts("Controllers: four pads, disconnect, rumble, RGB, Guide pulse, motion cadence and touch mouse passed");
}
