#include "../src/ui/quick_menu.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    qm_open(1,0,20000);assert(qm_is_open());
    assert(qm_input(MENU_BTN_CROSS)==QM_ACT_TOGGLE_TOUCHPAD);
    qm_view_t view;qm_view(&view);assert(!view.touchpad);
    qm_input(MENU_BTN_DOWN);assert(qm_input(MENU_BTN_CROSS)==QM_ACT_TOGGLE_OVERLAY);
    qm_input(MENU_BTN_DOWN);qm_input(MENU_BTN_RIGHT);int br,changed;
    assert(qm_input(MENU_BTN_CIRCLE)==QM_ACT_CLOSE);qm_close(&br,&changed);assert(br==25000 && changed);
    assert(!qm_is_open());puts("Stream menu: toggles, bitrate and close passed");
}
