#pragma once
#include <stdint.h>

/* With two pending flips the status exposes only the last queued index.
 * Do not guess the other one: wait until there is at most one pending. */
static inline int ml_present_pick_slot(int count,int current,int pending,
                                      int last_queued,int converting,int last_used) {
    if(count<2||count>3||current< -1||current>=count||pending<0||pending>=count-1)return -1;
    if(pending && (last_queued<0||last_queued>=count))return -1;
    for(int i=0;i<count;i++) {
        int candidate=(last_used+1+i)%count;
        if(candidate<0)candidate+=count;
        if(candidate==current||candidate==converting||
           (pending&&candidate==last_queued))continue;
        return candidate;
    }
    return -1;
}
static inline uint32_t ml_present_wait_budget(uint64_t age_us,uint32_t budget_us) {
    if(age_us>=33000)return 0;
    uint32_t fresh_us=(uint32_t)(33000-age_us);
    return fresh_us<budget_us?fresh_us:budget_us;
}
static inline uint32_t ml_present_remaining_us(uint64_t now,uint64_t deadline) {
    if(now>=deadline)return 0;
    uint64_t remaining=deadline-now;
    return remaining>UINT32_MAX?UINT32_MAX:(uint32_t)remaining;
}
