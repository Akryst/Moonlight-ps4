#pragma once
#include <stdint.h>
#include <stdio.h>

static inline int gpu_capture_read(FILE *file, uint8_t *dst, size_t bytes) {
    if (fread(dst, 1, bytes, file) != bytes)
        return -1;
    return fgetc(file) == EOF && !ferror(file) ? 0 : -1;
}

/* GL reads bottom-up RGBA; the existing presenter takes top-down BGRA. */
static inline void gpu_readback_bgra(uint8_t *dst, const uint8_t *rgba, int w, int h) {
    for (int y = 0; y < h; ++y) {
        const uint8_t *src = rgba + (h - 1 - y) * w * 4;
        for (int x = 0; x < w; ++x) {
            *dst++ = src[2];
            *dst++ = src[1];
            *dst++ = src[0];
            *dst++ = src[3];
            src += 4;
        }
    }
}
