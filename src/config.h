// Persistent client configuration (simple INI).
#pragma once

#include <stdbool.h>
#include <Limelight.h>

#define CONFIG_DIR_PS4 "/data/moonlight"
#define CONFIG_MAX_HOST 128
#define CONFIG_MAX_APP  128

typedef struct {
    STREAM_CONFIGURATION stream; // embedded: width/height/fps/bitrate/...

    char host[CONFIG_MAX_HOST];
    unsigned short http_port; // discovered SRV port; manual IP defaults to 47989
    char app_name[CONFIG_MAX_APP]; // name or numeric id
    char last_app[CONFIG_MAX_APP]; // library selection, separate from the launched app
    char debug_host[64];

    bool sops;
    bool local_audio;
    bool prefer_hw;
    bool videodec2_spike;
    bool prefer_ycbcr;
    bool enable_file_log; // writes /data/moonlight/debug.log
    bool show_stats; // on-screen perf overlay (FPS/decode/convert/present/KB per frame)
    bool paired_ok; // runtime only
    bool autostart; // boot straight into app_name (set with SQUARE on a game)
    int ui_theme; // Midnight, Mono, Blue Wave, Daylight, Cinema (0..4)
    bool reduced_motion;
    int controller_deadzone; // percent, 0..25
    bool swap_face_buttons; // swap Cross/Circle on the host, menu remains unchanged

    /* Feature additions. */
    char wol_mac[24];      // "AA:BB:CC:DD:EE:FF"; empty = Wake-on-LAN off
    bool wol_auto;         // wol_mac came from the host's serverinfo
                           // (shows "(auto)" in Settings; manual edit clears)
    bool surround51;       // request 5.1 audio from the host (opt-in; falls
                           // back to a stereo downmix if 8ch output fails)
    bool touchpad_mouse;   // DS4 touchpad acts as a mouse (click=L, 2-finger=R)
    char motion_disabled_apps[1024]; // comma-separated numeric app ids
    bool gyro_motion;      // forward DS4 gyro/accel to the host when a
                           // game asks (toggle; off = no motion caps announced)

    /* Videodec2 tuning, A/B-able on console without a rebuild. Rev-2 defaults
     * (depth=2 + AU ONION) are the fast path; files with cfg_rev < 2 get them
     * re-applied on load. See docs/CONSOLE_VALIDATE.md. */
    int dec_pipeline_depth; // frames in flight inside Videodec2 (1..VIDEODEC2_MAX_FB)
    int dec_thread_prio;    // Orbis priority of the Vdec CPU worker (lower = higher)
    int slices_per_frame;   // CAPABILITY_SLICES_PER_FRAME asked of the host
    bool dec_au_onion;      // AU bitstream in cacheable ONION (false = WC_GARLIC)
    bool dec_fb_garlic;     // force decoder framebuffer to WC_GARLIC (skip ONION alias)
    int bgra_workers;       // NV12->BGRA convert threads (1..6)
    int bgra_nt;            // -1 auto, 0 cached stores, 1 streaming stores
} app_config_t;

void config_set_defaults(app_config_t *cfg);
void config_ensure_dir(const char *dir);
int config_load(app_config_t *cfg, const char *dir);
int config_save(const app_config_t *cfg, const char *dir);

bool config_title_motion_enabled(const app_config_t *cfg,int app_id);
int config_toggle_title_motion(app_config_t *cfg,int app_id);
