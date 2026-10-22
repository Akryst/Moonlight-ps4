#include "library_view.h"
#include <cstdio>
#include <cstdlib>

static bool snapshot(const char *path) {
    std::vector<unsigned char> pixels(1280 * 720 * 4);
    glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) return false;
    std::vector<unsigned char> top(pixels.size());
    for (int y = 0; y < 720; ++y)
        std::memcpy(top.data() + y * 1280 * 4, pixels.data() + (719 - y) * 1280 * 4, 1280 * 4);
    png_image output{};
    output.version = PNG_IMAGE_VERSION; output.width = 1280; output.height = 720; output.format = PNG_FORMAT_RGBA;
    const bool ok = png_image_write_to_file(&output, path, 0, top.data(), 0, nullptr) != 0;
    if (!ok) std::fprintf(stderr, "PNG: %s\n", output.message);
    png_image_free(&output);
    return ok;
}

int main(int argc, char **argv) {
    const char *shot = nullptr;
    const char *catalog = nullptr;
    bool demo = false;
    bool settings_shot = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--snapshot" && i + 1 < argc) shot = argv[++i];
        else if (arg == "--settings-snapshot" && i + 1 < argc) { shot = argv[++i]; settings_shot = true; }
        else if (arg == "--catalog" && i + 1 < argc) catalog = argv[++i];
        else if (arg == "--demo") demo = true;
        else { std::fprintf(stderr, "Usage: %s [--demo | --catalog catalog.cfg] [--snapshot output.png | --settings-snapshot output.png]\n", argv[0]); return 2; }
    }
    if (demo && catalog) { std::fprintf(stderr, "Choose --demo or --catalog\n"); return 2; }
    std::vector<Game> games = demo_games;
    if (catalog) {
        try {
            games.clear();
            for (const auto &entry : read_catalog(catalog))
                games.push_back({entry.name, "", entry.name == "Desktop" ? "Your PC, on the big screen." : "Your next session starts here.",
                    demo_games[games.size() % demo_games.size()].color, entry.cover});
        } catch (const std::exception &error) {
            std::fprintf(stderr, "Catalog: %s\n", error.what()); return 1;
        }
    }
    const char *home = std::getenv("HOME");
    auto profile = std::filesystem::path(home ? home : ".") / ".config/moonlight-ps4-preview/settings.cfg";
    Library app(std::move(games), profile);
    if (!app.available) return 1;
    // Keyboard equivalents of the PS4 face buttons, plus existing directional mappings.
    app.getInput()->setKeyboardMapping({
        {Input::Up, SDL_SCANCODE_UP}, {Input::Down, SDL_SCANCODE_DOWN},
        {Input::Left, SDL_SCANCODE_LEFT}, {Input::Right, SDL_SCANCODE_RIGHT},
        {Input::A, SDL_SCANCODE_RETURN}, {Input::B, SDL_SCANCODE_ESCAPE},
        {Input::Start, SDL_SCANCODE_TAB}, {Input::LB, SDL_SCANCODE_Q}, {Input::RB, SDL_SCANCODE_E},
        {Input::X, SDL_SCANCODE_SPACE}, {Input::Y, SDL_SCANCODE_F}});
    if (shot) {
        if (settings_shot) app.open_settings();
        app.settle();
        app.GLRenderer::flip(true, false);
        return snapshot(shot) ? 0 : 1;
    }
    while (app.running) app.flip();
    return 0;
}
