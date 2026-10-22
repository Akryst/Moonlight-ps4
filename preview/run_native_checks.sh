#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-tests
common=(-D_DEFAULT_SOURCE -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
    -ffunction-sections -fdata-sections -Ithird_party/moonlight-common-c/src -Ithird_party/mbedtls/include)
clang "${common[@]}" preview/native_ui_check.c src/ui/ui_draw.c -lm -o build-tests/native-ui-check
build-tests/native-ui-check
clang "${common[@]}" preview/async_art_check.c src/ui/ui_draw.c src/ui/ui_theme.c src/gamestream/gs_errors.c -lm -pthread -Wl,--gc-sections -o build-tests/async-art-check
build-tests/async-art-check
clang "${common[@]}" preview/native_menu_check.c src/ui/ui_theme.c src/gamestream/gs_errors.c -lm -pthread -Wl,--gc-sections -o build-tests/native-menu-check
build-tests/native-menu-check
clang "${common[@]}" preview/config_preferences_check.c src/config.c -o build-tests/config-preferences-check
build-tests/config-preferences-check
http_libraries=build-host/third_party/mbedtls/library
if [[ ! -f "$http_libraries/libmbedtls.a" ]]; then
    cmake -S third_party/mbedtls -B build-tests/http-mbedtls -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build-tests/http-mbedtls --target mbedtls mbedx509 mbedcrypto -j 4
    http_libraries=build-tests/http-mbedtls/library
fi
clang "${common[@]}" preview/http_check.c src/gamestream/gs_http.c src/gamestream/gs_errors.c \
    "$http_libraries/libmbedtls.a" "$http_libraries/libmbedx509.a" "$http_libraries/libmbedcrypto.a" \
    -pthread -o build-tests/http-check
build-tests/http-check
python3 preview/playnite_check.py

sdk_headers=/home/akryst/ps4dev/OpenOrbis/PS4Toolchain/include
if [[ -d "$sdk_headers" ]]; then
    clang "${common[@]}" -idirafter "$sdk_headers" preview/controllers_check.c src/input/touch_gesture.c -pthread -o build-tests/controllers-check
    build-tests/controllers-check
    clang "${common[@]}" -idirafter "$sdk_headers" -Ithird_party/opus/include preview/audio_surround_check.c -o build-tests/audio-surround-check
    build-tests/audio-surround-check
fi
clang "${common[@]}" preview/fork_flow_check.c src/config.c src/gamestream/gs_errors.c -Wl,--gc-sections -pthread -o build-tests/fork-flow-check
build-tests/fork-flow-check

clang "${common[@]}" preview/quick_menu_check.c src/ui/quick_menu.c -pthread -o build-tests/quick-menu-check
build-tests/quick-menu-check

clang "${common[@]}" preview/stream_overlay_check.c src/ui/ui_stream.c src/ui/ui_draw.c src/ui/ui_art.c src/ui/ui_theme.c -Wl,--gc-sections -pthread -lm -o build-tests/stream-overlay-check
build-tests/stream-overlay-check
