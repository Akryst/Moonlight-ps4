/* Phase 1: repeated EGL lifecycle, then live readback/copy to native VideoOut. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "gpu_readback.h"
#include "gpu_memory_probe.h"
#include "../video/video.h"
#include "../log.h"
#include <orbis/Pigletv2VSH.h>
#include <orbis/libkernel.h>
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>

#define PROBE_W 1280
#define PROBE_H 720
static FILE *report;
static uint8_t *snapshot;

#define EGL_CHECK(call) do { \
    if (!(call)) { \
        fprintf(report, "%s failed: EGL 0x%x\n", #call, eglGetError()); \
        goto cleanup; \
    } \
} while (0)

static int probe_cycle(int cycle) {
    int live = cycle == 3;
    uint8_t *bgra = NULL;
    int native_started = 0, submitted = 0, dropped = 0;
    int completed = 0;
    uint64_t readback_us = 0, convert_us = 0, present_us = 0;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config;
    EGLint count = 0, major = 0, minor = 0;
    OrbisPglWindow window = {0, 1280, 720};
    const EGLint config_attributes[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_SAMPLE_BUFFERS, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE
    };
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    const EGLint surface_attributes[] = {EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE};
    int initialized = 0, result = -1;
    uint64_t start = sceKernelGetProcessTime();

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGL_CHECK(display != EGL_NO_DISPLAY);
    EGL_CHECK(eglInitialize(display, &major, &minor));
    initialized = 1;
    if (gpu_memory_probe(report, "EGL initialized") != 0)
        goto cleanup;
    EGL_CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    EGL_CHECK(eglChooseConfig(display, config_attributes, &config, 1, &count));
    EGL_CHECK(count > 0);
    surface = eglCreateWindowSurface(display, config, &window, surface_attributes);
    EGL_CHECK(surface != EGL_NO_SURFACE);
    if (gpu_memory_probe(report, "window surface created") != 0)
        goto cleanup;
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    EGL_CHECK(context != EGL_NO_CONTEXT);
    EGL_CHECK(eglMakeCurrent(display, surface, surface, context));
    EGL_CHECK(eglSwapInterval(display, 0));
    fprintf(report, "cycle=%d EGL=%d.%d GL=%s renderer=%s\n",
            cycle, major, minor, glGetString(GL_VERSION), glGetString(GL_RENDERER));
    fflush(report);
    if (gpu_memory_probe(report, "EGL current") != 0)
        goto cleanup;
    if (live) {
        bgra = malloc((size_t)PROBE_W * PROBE_H * 4);
        if (!bgra) {
            fprintf(report, "live BGRA allocation failed\n");
            goto cleanup;
        }
        int user_rc = sceUserServiceInitialize(NULL);
        fprintf(report, "live UserService init=0x%x; VideoOut begin with EGL current\n", (unsigned)user_rc);
        fflush(report);
        int native_rc = video_ui_begin(PROBE_W, PROBE_H);
        fprintf(report, "live VideoOut begin returned=%d\n", native_rc);
        fflush(report);
        if (native_rc != 0)
            goto cleanup;
        native_started = 1;
    }

    int service_rc = sceSystemServiceHideSplashScreen();
    fprintf(report, "cycle=%d hide splash before drawing=0x%x\n", cycle, service_rc);
    fflush(report);

    int frame_count = live ? 240 : 2;
    uint64_t animation_start = sceKernelGetProcessTime();
    for (int frame = 0; frame < frame_count; ++frame) {
        if (live && sceKernelGetProcessTime() - animation_start >= 20000000)
            break;
        if (frame < 2) {
            fprintf(report, "cycle=%d frame=%d clear begin\n", cycle, frame);
            fflush(report);
        }
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.05f, 0.10f, 0.20f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        int rect_x = 100 + (frame % 120) * 4;
        glScissor(rect_x, 300, 160, 120);
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        uint64_t read_start = sceKernelGetProcessTime();
        if (frame == 1 || live) {
            unsigned char pixel[4] = {0};
            if (!live || frame < 2) {
                fprintf(report, "cycle=%d readback begin\n", cycle);
                fflush(report);
            }
            glReadPixels(rect_x + 20, 320, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (!live || frame % 120 == 0)
                fprintf(report, "cycle=%d frame=%d readback=%u,%u,%u,%u\n",
                        cycle, frame, pixel[0], pixel[1], pixel[2], pixel[3]);
            if (pixel[0] < 247 || pixel[1] > 8 || pixel[2] > 8 || pixel[3] < 247) {
                fprintf(report, "pixel check failed: %u,%u,%u,%u\n",
                        pixel[0], pixel[1], pixel[2], pixel[3]);
                goto cleanup;
            }
            if (!live || frame == 0)
                fprintf(report, "full-frame readback begin\n");
            fflush(report);
            glReadPixels(0, 0, PROBE_W, PROBE_H, GL_RGBA, GL_UNSIGNED_BYTE, snapshot);
            if (!live || frame == 0)
                fprintf(report, "full-frame readback returned\n");
            fflush(report);
        }
        readback_us += sceKernelGetProcessTime() - read_start;
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            fprintf(report, "frame=%d GL error=0x%x\n", frame, error);
            goto cleanup;
        }
        if (live) {
            uint64_t convert_start = sceKernelGetProcessTime();
            gpu_readback_bgra(bgra, snapshot, PROBE_W, PROBE_H);
            convert_us += sceKernelGetProcessTime() - convert_start;
            uint64_t present_start = sceKernelGetProcessTime();
            if (video_ui_present(bgra, PROBE_W * 4) == 0) {
                if (submitted++ == 0)
                    fprintf(report, "first live native flip submitted\n");
            } else {
                ++dropped;
            }
            present_us += sceKernelGetProcessTime() - present_start;
            if (frame < 2 || frame % 30 == 0) {
                fprintf(report, "live frame=%d submitted=%d dropped=%d readback_us=%llu convert_us=%llu present_us=%llu\n",
                        frame, submitted, dropped, (unsigned long long)readback_us,
                        (unsigned long long)convert_us, (unsigned long long)present_us);
                fflush(report);
            }
        }
        if (!live && frame < 2) {
            fprintf(report, "cycle=%d frame=%d swap begin\n", cycle, frame);
            fflush(report);
        }
        /* Live output uses VideoOut; do not also submit to Piglet's compositor. */
        if (!live)
            EGL_CHECK(eglSwapBuffers(display, surface));
        if (!live && frame == 0) {
            fprintf(report, "cycle=%d first swap complete\n", cycle);
            fflush(report);
        }
        ++completed;
        /* Keep the manual visual check visible without relying on swap VSync. */
        sceKernelUsleep(16000);
    }
    fprintf(report, "cycle=%d frames=%d elapsed_us=%llu (includes swaps/readback)\n",
            cycle, completed, (unsigned long long)(sceKernelGetProcessTime() - start));
    if (live)
        fprintf(report, "live animation_elapsed_us=%llu completed=%d readback_us=%llu convert_us=%llu present_us=%llu; EGL swaps skipped\n",
                (unsigned long long)(sceKernelGetProcessTime() - animation_start), completed,
                (unsigned long long)readback_us, (unsigned long long)convert_us,
                (unsigned long long)present_us);
    result = !live || submitted > 0 ? 0 : -1;

cleanup:
    if (native_started) {
        video_ui_end();
        fprintf(report, "live native submissions=%d dropped=%d; TV movement requires confirmation\n", submitted, dropped);
    }
    free(bgra);
    if (initialized) {
        if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
            result = -1;
        if (context != EGL_NO_CONTEXT && !eglDestroyContext(display, context))
            result = -1;
        if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface))
            result = -1;
        if (!eglTerminate(display))
            result = -1;
    }
    fprintf(report, "cycle=%d teardown=%s\n", cycle, result == 0 ? "PASS" : "FAIL");
    fflush(report);
    return result;
}

static int present_snapshot(const uint8_t *bgra) {
    int rc = sceUserServiceInitialize(NULL);
    fprintf(report, "UserService init=0x%x; native VideoOut begin\n", (unsigned)rc);
    fflush(report);
    rc = video_ui_begin(PROBE_W, PROBE_H);
    fprintf(report, "native VideoOut begin returned=%d\n", rc);
    fflush(report);
    if (rc != 0)
        return -1;
    int submitted = 0;
    for (int frame = 0; frame < 300; ++frame) {
        if (video_ui_present(bgra, PROBE_W * 4) == 0 && submitted++ == 0) {
            fprintf(report, "first native flip submitted\n");
            fflush(report);
            (void)sceSystemServiceHideSplashScreen();
        }
        sceKernelUsleep(16000);
    }
    video_ui_end();
    fprintf(report, "native flips submitted=%d; verify visibility on TV\n", submitted);
    fflush(report);
    return submitted == 300 ? 0 : -1;
}

static int save_and_present_snapshot(void) {
    size_t bytes = (size_t)PROBE_W * PROBE_H * 4;
    uint8_t *bgra = malloc(bytes);
    if (!bgra) {
        fprintf(report, "BGRA allocation failed\n");
        return -1;
    }
    gpu_readback_bgra(bgra, snapshot, PROBE_W, PROBE_H);
    int result = -1;
    FILE *capture = fopen("/data/moonlight/gpu-probe-readback.bgra", "wb");
    if (capture) {
        size_t saved = fwrite(bgra, 1, bytes, capture);
        int close_rc = fclose(capture);
        fprintf(report, "saved BGRA capture=%zu/%zu bytes close=%d\n", saved, bytes, close_rc);
        result = saved == bytes && close_rc == 0 ? 0 : -1;
    } else {
        fprintf(report, "could not save BGRA capture\n");
    }
    fflush(report);
    int presentation = present_snapshot(bgra);
    fprintf(report, "native snapshot presentation result=%d\n", presentation);
    if (presentation != 0)
        result = -1;
    free(bgra);
    return result;
}

int main(void) {
    report = fopen("/data/moonlight/gpu-probe.log", "w");
    if (!report)
        report = stdout;
    fprintf(report, "Standalone Piglet probe r12 (live native output without EGL swaps); streaming is not started.\n");
    log_init_file("/data/moonlight/gpu-probe-videoout.log");
    LOGI("gpu-probe r12: current native log session");
    if (gpu_memory_probe(report, "before module load") != 0)
        goto done;
    snapshot = malloc((size_t)PROBE_W * PROBE_H * 4);
    if (!snapshot) {
        fprintf(report, "RGBA allocation failed\n");
        goto done;
    }
    int service_rc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    fprintf(report, "SystemService load=0x%x\n", service_rc);
    fflush(report);
    if (service_rc < 0)
        goto done;
    const char *sandbox = sceKernelGetFsSandboxRandomWord();
    char module_path[256];
    int module = -1, module_result = 0, shaders = -1;
    if (!sandbox)
        goto done;
    int length = snprintf(module_path, sizeof(module_path), "/%s/common/lib/libScePigletv2VSH.sprx", sandbox);
    if (length < 0 || length >= (int)sizeof(module_path))
        goto done;
    module = sceKernelLoadStartModule(module_path, 0, NULL, 0, NULL, &module_result);
    fprintf(report, "Piglet module=%d start=0x%x\n", module, module_result);
    fflush(report);
    if (module < 0)
        goto done;

    length = snprintf(module_path, sizeof(module_path), "/%s/common/lib/libScePrecompiledShaders.sprx", sandbox);
    if (length < 0 || length >= (int)sizeof(module_path))
        goto done;
    shaders = sceKernelLoadStartModule(module_path, 0, NULL, 0, NULL, &module_result);
    fprintf(report, "PrecompiledShaders module=%d start=0x%x\n", shaders, module_result);
    fflush(report);
    if (shaders < 0)
        goto done;
    if (gpu_memory_probe(report, "modules loaded") != 0)
        goto done;

    /* Keep the known-working video budget; reduce only system memory versus r8. */
    OrbisPglConfig config = {0};
    config.size = sizeof(config);
    config.flags = ORBIS_PGL_FLAGS_USE_COMPOSITE_EXT | ORBIS_PGL_FLAGS_USE_FLEXIBLE_MEMORY | 0x60;
    config.processOrder = 1;
    config.systemSharedMemorySize = 64ull * 1024 * 1024;
    config.videoSharedMemorySize = 512ull * 1024 * 1024;
    config.maxMappedFlexibleMemory = 170ull * 1024 * 1024;
    config.drawCommandBufferSize = 1024 * 1024;
    config.lcueResourceBufferSize = 1024 * 1024;
    config.dbgPosCmd_0x40 = 1280;
    config.dbgPosCmd_0x44 = 720;
    config.unk_0x5C = 2;
    if (!scePigletSetConfigurationVSH(&config)) {
        fprintf(report, "Piglet configuration failed\n");
        goto done;
    }
    OrbisPglConfig actual = {0};
    actual.size = sizeof(actual);
    if (scePigletGetConfigurationVSH(&actual)) {
        fprintf(report, "Piglet reported config: system=%llu video=%llu flexible=%llu flags=0x%x\n",
                (unsigned long long)actual.systemSharedMemorySize,
                (unsigned long long)actual.videoSharedMemorySize,
                (unsigned long long)actual.maxMappedFlexibleMemory, actual.flags);
    } else {
        fprintf(report, "Piglet configuration readback failed\n");
    }
    fflush(report);
    if (gpu_memory_probe(report, "Piglet configured") != 0)
        goto done;
    for (int cycle = 0; cycle < 4; ++cycle) {
        if (probe_cycle(cycle) != 0)
            goto done;
        if (gpu_memory_probe(report, "after context cycle") != 0)
            goto done;
    }
    if (gpu_memory_probe(report, "EGL terminated") != 0)
        goto done;
    fprintf(report, "EGL closed; unloading Piglet before saving capture\n");
    module_result = 0;
    int unload_shaders = sceKernelStopUnloadModule(shaders, 0, NULL, 0, NULL, &module_result);
    int unload_piglet = sceKernelStopUnloadModule(module, 0, NULL, 0, NULL, &module_result);
    fprintf(report, "PrecompiledShaders unload=%d; Piglet unload=%d\n", unload_shaders, unload_piglet);
    fflush(report);
    if (unload_shaders < 0 || unload_piglet < 0)
        goto done;
    if (gpu_memory_probe(report, "modules unloaded") != 0)
        goto done;
    int capture_rc = save_and_present_snapshot();
    fprintf(report, "Snapshot save/presentation result=%d\n", capture_rc);

done:
    free(snapshot);
    fprintf(report, "Probe finished. Close with PS; inspect gpu-probe.log.\n");
    fflush(report);
    for (;;)
        sceKernelUsleep(1000000);
}
