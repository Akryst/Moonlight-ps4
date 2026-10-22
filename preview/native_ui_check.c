#include "../src/ui/ui_draw.h"
#include "../src/ui/ui_motion.h"
#include <assert.h>
#include <string.h>

int main(void) {
    uint32_t a[]={0xff000000,0xff000000,0xdeadbeef,0xff000000,0xff000000,0xdeadbeef};
    uint32_t b[]={0xffffffff,0xffffffff,0xdeadbeef,0xffffffff,0xffffffff,0xdeadbeef};
    ui_surface_t from={(uint8_t *)a,12,2,2},to={(uint8_t *)b,12,2,2};
    ui_crossfade(&from,&from,&to,0);assert(a[0]==0xff000000);
    ui_crossfade(&from,&from,&to,128);assert(a[0]==0xff808080 && a[3]==a[0]);
    ui_crossfade(&from,&from,&to,255);assert(a[0]==0xffffffff && a[4]==a[0]);
    assert(a[2]==0xdeadbeef && a[5]==0xdeadbeef);
    const uint8_t image[]={255,0,0,0,0,0,0,128};
    ui_image_alpha(&from,0,0,2,1,image,2,1);
    assert(a[0]==0xffffffff && a[1]==0xff7f7f7f);
    ui_image_alpha(&from,-1,-1,2,2,image,2,1);
    assert(a[2]==0xdeadbeef && a[5]==0xdeadbeef);
    unsigned previous=0;
    for(unsigned t=0;t<=240000;t+=1000) {
        unsigned current=ui_motion_progress(t,240000);
        assert(current>=previous && current<=255);
        assert(ui_motion_step(previous,current)<=255);
        previous=current;
    }
    assert(previous==255 && ui_motion_step(100,100)==0);
    assert(ui_motion_step(100,255)==255 && ui_motion_progress(1,0)==255);
    assert(ui_motion_progress(UINT64_MAX,240000)==255);
    return 0;
}
