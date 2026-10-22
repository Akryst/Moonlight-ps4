#pragma once
#include <stdint.h>

/* Linear progress with a smooth start/end; driven by elapsed time, not FPS. */
static inline unsigned ui_motion_progress(uint64_t elapsed_us,unsigned duration_us) {
    if(!duration_us || elapsed_us>=duration_us)return 255;
    uint64_t t=elapsed_us*255/duration_us;
    return (unsigned)(t*t*(765-2*t)/(255*255));
}
/* Blend a previously displayed intermediate frame towards its final target. */
static inline unsigned ui_motion_step(unsigned previous,unsigned current) {
    if(current>=255)return 255;
    if(current<=previous || previous>=255)return 0;
    return (current-previous)*255/(255-previous);
}
