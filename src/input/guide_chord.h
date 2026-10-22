#pragma once

#include <stdint.h>
#include <Limelight.h>

enum { GUIDE_IDLE, GUIDE_NEUTRAL, GUIDE_PULSE, GUIDE_DRAIN };

typedef struct {
    int phase;
    uint64_t release_us;
} guide_chord_t;

static inline int guide_chord_filter(guide_chord_t *state, int buttons, uint64_t now) {
    const int chord = LB_FLAG | RB_FLAG | PLAY_FLAG;
    switch (state->phase) {
    case GUIDE_IDLE:
        if ((buttons & chord) != chord || (buttons & TOUCHPAD_FLAG))
            return buttons;
        /* Release previously forwarded buttons before pressing Guide. */
        state->phase = GUIDE_NEUTRAL;
        return 0;
    case GUIDE_NEUTRAL:
        state->phase = GUIDE_PULSE;
        state->release_us = now + 80000;
        if (!(buttons & TOUCHPAD_FLAG))
            return SPECIAL_FLAG;
        /* Touchpad belongs to the existing local exit shortcut. */
        state->phase = GUIDE_DRAIN;
        return 0;
    case GUIDE_PULSE:
        if (now < state->release_us && !(buttons & TOUCHPAD_FLAG))
            return SPECIAL_FLAG;
        state->phase = GUIDE_DRAIN;
        return 0;
    default:
        if (!(buttons & chord))
            state->phase = GUIDE_IDLE;
        return buttons & ~chord;
    }
}

static inline int guide_chord_neutral(const guide_chord_t *state) {
    return state->phase == GUIDE_NEUTRAL || state->phase == GUIDE_PULSE;
}
