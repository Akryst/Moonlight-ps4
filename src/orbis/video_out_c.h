// C-safe wrappers over sceVideoOut. Official headers use
// `enum X : int32_t` (C++ syntax), so we avoid including them from C.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <orbis/_types/kernel.h>

#define ML_VIDEO_USER_MAIN              0xFF
#define ML_VIDEO_OUT_BUS_MAIN           0
#define ML_VIDEO_OUT_FLIP_VSYNC         1
#define ML_VIDEO_OUT_FLIP_60HZ          0
#define ML_VIDEO_OUT_TILING_TILE        0
#define ML_VIDEO_OUT_TILING_LINEAR      1
#define ML_VIDEO_OUT_ASPECT_16_9        0
#define ML_VIDEO_OUT_PIXEL_B8G8R8A8     0x80000000u
#define ML_VIDEO_OUT_PIXEL_YCBCR420_BT709 0x08322200u

// Direct memory (libkernel): Videodec2 requires Onion for compute/cpuGpu; GPU fb = Garlic.
#define ML_DMEM_ALIGN       0x4000u
#define ML_DMEM_TYPE_ONION  0   // ORBIS_KERNEL_WB_ONION
#define ML_DMEM_TYPE_GARLIC 3   // ORBIS_KERNEL_WC_GARLIC
#define ML_DMEM_TYPE_WB_GARLIC 10 // ORBIS_KERNEL_WB_GARLIC (CPU-cacheable Garlic)
#define ML_DMEM_PROT_RW     0x33u

#define ML_YCBCR_PLUGIN_MARKER "/data/moonlight/ycbcr_unlock.loaded"

typedef struct {
    int32_t format;
    int32_t tmode;
    int32_t aspect;
    uint32_t width;
    uint32_t height;
    uint32_t pixelPitch;
    uint64_t reserved[2];
} MlVideoOutBufferAttribute;

typedef struct {
    uint64_t num;
    uint64_t ptime;
    uint64_t stime;
    int64_t flipArg;
    uint64_t reserved[2];
    int32_t numGpuFlipPending;
    int32_t numFlipPending;
    int32_t currentBuffer;
    uint32_t reserved1;
} MlVideoOutFlipStatus;

int32_t sceVideoOutOpen(int32_t userId, int32_t busType, int32_t index, const void *param);
int32_t sceVideoOutClose(int32_t handle);
int32_t sceVideoOutRegisterBuffers(int32_t handle, int32_t startIndex, void *const *addrs,
                                   int32_t count, const MlVideoOutBufferAttribute *attr);
int32_t sceVideoOutUnregisterBuffers(int32_t handle, int32_t attributeIndex);
int32_t sceVideoOutSubmitFlip(int32_t handle, int32_t bufferIndex, uint32_t flipMode, int64_t flipArg);
void sceVideoOutSetBufferAttribute(void *attr, uint32_t pixelFormat, uint32_t tilingMode,
                                   uint32_t aspectRatio, uint32_t width, uint32_t height,
                                   uint32_t pitchInPixel);
int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t flipRate);
int32_t sceVideoOutGetFlipStatus(int32_t handle, MlVideoOutFlipStatus *status);
int32_t sceVideoOutAddFlipEvent(OrbisKernelEqueue eq,int32_t handle,void *data);
int32_t sceKernelCreateEqueue(OrbisKernelEqueue *eq,const char *name);
int32_t sceKernelDeleteEqueue(OrbisKernelEqueue eq);
int32_t sceKernelWaitEqueue(OrbisKernelEqueue eq,OrbisKernelEvent *events,
                           int32_t capacity,int32_t *count,OrbisKernelUseconds *timeout);
/* Do NOT declare AddBufferYccPrivilege / SysUpdatePrivilege here: the
 * OpenOrbis stub is `jmp .` (infinite hang). Resolve via Dlsym from the real SPRX. */

int32_t sceUserServiceGetInitialUser(int32_t *userId);
int32_t sceUserServiceInitialize(void *param);

int32_t sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t len,
                                      size_t align, int32_t memoryType, off_t *physAddrOut);
int32_t sceKernelMapDirectMemory(void **addr, size_t len, int32_t prot, int32_t flags,
                                 off_t directMemoryStart, size_t align);
/* type = virtual memory (ONION/GARLIC); same phys → CPU-cacheable alias. */
int32_t sceKernelMapDirectMemory2(void **addr, size_t len, int32_t type, int32_t prot,
                                  int32_t flags, off_t directMemoryStart, size_t align);
int32_t sceKernelReleaseDirectMemory(off_t start, size_t len);
int32_t sceKernelMunmap(void *addr, size_t len);
int32_t sceKernelUsleep(uint32_t microseconds);
size_t sceKernelGetDirectMemorySize(void);
uint64_t sceKernelGetProcessTime(void);

/* After CPU writes to WC_GARLIC: make data visible to DCE/GPU. */
void sceGnmFlushGarlic(void);
