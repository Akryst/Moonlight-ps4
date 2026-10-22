#pragma once
#include <libconfig.h>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>

struct PreviewSettings {
    int resolution = 1, fps = 1, bitrate = 1;
    bool reduced_motion = false;
    static constexpr std::array<int, 4> bitrates = {10, 20, 30, 50};
    bool valid() const {
        return resolution >= 0 && resolution < 2 && fps >= 0 && fps < 2 && bitrate >= 0 && bitrate < 4;
    }
    void adjust(int row, int direction) {
        if (row == 0) resolution = (resolution + 2 + direction) % 2;
        if (row == 1) fps = (fps + 2 + direction) % 2;
        if (row == 2) bitrate = (bitrate + 4 + direction) % 4;
        if (row == 3) reduced_motion = !reduced_motion;
    }
    std::string value(int row) const {
        if (row == 0) return resolution ? "1920 x 1080" : "1280 x 720";
        if (row == 1) return fps ? "60 FPS" : "30 FPS";
        if (row == 2) return std::to_string(bitrates[bitrate]) + " Mbps";
        return reduced_motion ? "Reduced" : "Full";
    }
};

inline PreviewSettings load_settings(const std::filesystem::path &path) {
    PreviewSettings result;
    if (!std::filesystem::exists(path)) return result;
    if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("Profile exceeds 4 KiB");
    config_t config;
    config_init(&config);
    int reduced = 0;
    bool valid = config_read_file(&config, path.c_str()) &&
        config_lookup_int(&config, "resolution", &result.resolution) &&
        config_lookup_int(&config, "fps", &result.fps) &&
        config_lookup_int(&config, "bitrate", &result.bitrate) &&
        config_lookup_bool(&config, "reduced_motion", &reduced) && result.valid();
    config_destroy(&config);
    if (!valid) throw std::runtime_error("Invalid preview profile");
    result.reduced_motion = reduced != 0;
    return result;
}

inline bool save_settings(const PreviewSettings &settings, const std::filesystem::path &path, std::string &error) {
    if (!settings.valid()) { error = "Invalid profile"; return false; }
    try {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += ".tmp";
        config_t config;
        config_init(&config);
        auto *root = config_root_setting(&config);
        config_setting_set_int(config_setting_add(root, "resolution", CONFIG_TYPE_INT), settings.resolution);
        config_setting_set_int(config_setting_add(root, "fps", CONFIG_TYPE_INT), settings.fps);
        config_setting_set_int(config_setting_add(root, "bitrate", CONFIG_TYPE_INT), settings.bitrate);
        config_setting_set_bool(config_setting_add(root, "reduced_motion", CONFIG_TYPE_BOOL), settings.reduced_motion);
        bool written = config_write_file(&config, temporary.c_str());
        config_destroy(&config);
        if (!written) { error = "Could not save profile"; return false; }
        std::filesystem::rename(temporary, path);
        return true;
    } catch (const std::exception &) { error = "Could not save profile"; return false; }
}
