#pragma once
#include <libconfig.h>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

struct CatalogEntry { std::string name, cover; };

inline std::vector<CatalogEntry> read_catalog(const char *path) {
    if (std::filesystem::file_size(path) > 2 * 1024 * 1024)
        throw std::runtime_error("Catalog exceeds 2 MiB");
    config_t config;
    config_init(&config);
    std::vector<CatalogEntry> result;
    try {
        if (!config_read_file(&config, path))
            throw std::runtime_error("Cannot read preview catalog");
        auto *apps = config_lookup(&config, "apps");
        if (!apps || config_setting_type(apps) != CONFIG_TYPE_LIST || config_setting_length(apps) > 4096)
            throw std::runtime_error("Invalid catalog application list");
        for (int i = 0; i < config_setting_length(apps); ++i) {
            auto *app = config_setting_get_elem(apps, i);
            const char *name = nullptr, *cover = nullptr;
            if (!config_setting_lookup_string(app, "name", &name) || !name[0] || std::strlen(name) > 512 ||
                !config_setting_lookup_string(app, "cover", &cover) || std::strlen(cover) > 4096)
                throw std::runtime_error("Invalid catalog entry");
            for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
                if (*p < 32 || *p == 127) throw std::runtime_error("Invalid catalog name");
            result.push_back({name, cover});
        }
    } catch (...) {
        config_destroy(&config);
        throw;
    }
    config_destroy(&config);
    return result;
}
