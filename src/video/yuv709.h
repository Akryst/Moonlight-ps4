/* SDR Rec.709 limited-range YCbCr -> full-range BGRA.
 * Stream configuration explicitly requests COLORSPACE_REC_709 / LIMITED.
 * Fixed-point coefficients closely approximate ITU-R BT.709: Y 16..235,
 * neutral chroma 128. Every product uses a signed 32-bit intermediate, or
 * SSE2 mulhi with bounded operands; never truncate a large product to int16.
 */
#pragma once
#include <stdint.h>

static inline uint8_t ml_color_clamp(int value) {
    return value<0?0:value>255?255:(uint8_t)value;
}
static inline void ml_yuv709_pixel(uint8_t *dst,int y,int u,int v) {
    int luma=((y-16)*2385)>>11;
    int cb=u-128,cr=v-128;
    dst[0]=ml_color_clamp(luma+((cb*1082)>>9));
    dst[1]=ml_color_clamp(luma-((cb*109)>>9)-((cr*273)>>9));
    dst[2]=ml_color_clamp(luma+((cr*918)>>9));
    dst[3]=255;
}

#ifdef __SSE2__
#include <emmintrin.h>
static inline __m128i ml_yuv709_luma8(__m128i y) {
    __m128i centered=_mm_sub_epi16(y,_mm_set1_epi16(16));
    return _mm_mulhi_epi16(_mm_slli_epi16(centered,5),_mm_set1_epi16(2385));
}
static inline void ml_yuv709_chroma8(__m128i u,__m128i v,
                                    __m128i *rv,__m128i *guv,__m128i *bu) {
    __m128i us=_mm_slli_epi16(u,7),vs=_mm_slli_epi16(v,7);
    *rv=_mm_mulhi_epi16(vs,_mm_set1_epi16(918));
    *guv=_mm_add_epi16(_mm_mulhi_epi16(us,_mm_set1_epi16(109)),
                       _mm_mulhi_epi16(vs,_mm_set1_epi16(273)));
    *bu=_mm_mulhi_epi16(us,_mm_set1_epi16(1082));
}
#endif
