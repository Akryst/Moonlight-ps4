/* Separate process: display the saved GPU pixels without loading Piglet. */
#include <stdlib.h>
#include "gpu_readback.h"
#include "gpu_memory_probe.h"
#include "../video/video.h"
#include "../log.h"
#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/SystemService.h>

int main(void) {
    FILE *report = fopen("/data/moonlight/gpu-viewer.log", "w");
    if (!report)
        report = stdout;
    fprintf(report, "GPU Snapshot Viewer r2; Piglet is not loaded in this process.\n");
    size_t bytes = (size_t)1280 * 720 * 4;
    uint8_t *bgra = NULL;
    if (gpu_memory_probe(report, "viewer before VideoOut") != 0)
        goto done;
    bgra = malloc(bytes);
    if (!bgra) {
        fprintf(report, "snapshot allocation failed\n");
        goto done;
    }
    FILE *capture = fopen("/data/moonlight/gpu-probe-readback.bgra", "rb");
    if (!capture) {
        fprintf(report, "GPU snapshot not found; run GPU Probe first\n");
        goto done;
    }
    int rc = gpu_capture_read(capture, bgra, bytes);
    fclose(capture);
    if (rc != 0) {
        fprintf(report, "GPU snapshot has an invalid size/read error; expected %zu bytes\n", bytes);
        goto done;
    }
    log_init_file("/data/moonlight/gpu-viewer-videoout.log");
    rc = sceUserServiceInitialize(NULL);
    fprintf(report, "UserService init=0x%x; VideoOut begin\n", rc);
    fflush(report);
    rc = video_ui_begin(1280, 720);
    fprintf(report, "VideoOut begin returned=%d\n", rc);
    fflush(report);
    if (rc != 0)
        goto done;
    int submitted = 0;
    for (int frame = 0; frame < 300; ++frame) {
        if (video_ui_present(bgra, 1280 * 4) == 0 && submitted++ == 0) {
            fprintf(report, "first native flip submitted\n");
            fflush(report);
            (void)sceSystemServiceHideSplashScreen();
        }
        sceKernelUsleep(16000);
    }
    fprintf(report, "native flips submitted=%d; visibility requires user confirmation\n", submitted);
    video_ui_end();
done:
    free(bgra);
    fprintf(report, "Viewer finished; close with PS.\n");
    fflush(report);
    for (;;)
        sceKernelUsleep(1000000);
}
