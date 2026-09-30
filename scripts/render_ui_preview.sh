#!/usr/bin/env bash
# Run in the WSL build mirror after build_from_windows.sh has synchronized it.
# Optional: set UI_COVERS_DIR to Sunshine's moonlight-steam-covers directory.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-tests
if [[ -n "${UI_COVERS_DIR:-}" ]]; then
    for app in 553850 227300; do
        if [[ -f "$UI_COVERS_DIR/steam-$app.png" ]]; then
            cp "$UI_COVERS_DIR/steam-$app.png" "build-tests/art-$app.png"
        fi
    done
fi
clang -D_DEFAULT_SOURCE -std=c11 -O1 -g -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -Ithird_party/moonlight-common-c/src \
    -Ithird_party/mbedtls/include tests/render_ui.c src/ui/ui_draw.c \
    src/ui/ui_theme.c src/ui/ui_art.c -Wl,--gc-sections -lm -o build-tests/render_ui
build-tests/render_ui
