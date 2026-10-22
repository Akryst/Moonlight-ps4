#ifndef GPU_MEMORY_PROBE_H
#define GPU_MEMORY_PROBE_H
#include <stdio.h>
#include <orbis/libkernel.h>

/* Reserve/release the presenter's 3 x 4 MiB budget without mapping or owning output. */
static inline int gpu_memory_probe(FILE *report, const char *stage) {
    const size_t bytes = 12u * 1024 * 1024;
    size_t total = sceKernelGetDirectMemorySize();
    off_t offset = 0;
    int rc = sceKernelAllocateDirectMemory(0, total, bytes, 0x200000, 3, &offset);
    fprintf(report, "memory stage=%s total=0x%zx request=0x%zx allocate=0x%x\n",
            stage, total, bytes, (unsigned)rc);
    int release = 0;
    if (rc == 0) {
        release = sceKernelReleaseDirectMemory(offset, bytes);
        fprintf(report, "memory stage=%s release=0x%x\n", stage, (unsigned)release);
    }
    fflush(report);
    return release;
}
#endif
