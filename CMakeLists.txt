cmake_minimum_required(VERSION 3.16)
project(moonlight-ps4 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---------------------------------------------------------------------------
# Shared dependencies (host and PS4): mbedTLS, moonlight-common-c, opus
# ---------------------------------------------------------------------------
if(PS4)
    add_compile_definitions(MBEDTLS_USER_CONFIG_FILE="${CMAKE_SOURCE_DIR}/src/ports/ps4_mbedtls_user_config.h")
endif()

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/mbedtls EXCLUDE_FROM_ALL)

set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/moonlight-common-c EXCLUDE_FROM_ALL)

set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
if(PS4)
    set(OPUS_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(third_party/opus EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(COMMON_SOURCES
    src/log.c
    src/config.c
)

set(GAMESTREAM_SOURCES
    src/gamestream/discovery.c
    src/gamestream/certgen.c
    src/gamestream/gs_http.c
    src/gamestream/mini_xml.c
    src/gamestream/client.c
    src/gamestream/sps.c
)

# h264bitstream (SPS fixup). Skip its CMakeLists (FILE_SET / cmake 3.23).
add_library(h264bitstream STATIC
    third_party/h264bitstream/h264_nal.c
    third_party/h264bitstream/h264_sei.c
    third_party/h264bitstream/h264_stream.c
)
target_include_directories(h264bitstream PUBLIC third_party/h264bitstream)
if(PS4)
    target_compile_options(h264bitstream PRIVATE -Wno-unused-parameter -Wno-sign-compare)
endif()

if(PS4)
    # Cross-built FFmpeg (see scripts/build_ffmpeg_ps4.sh)
    set(FFMPEG_PS4_PREFIX "$ENV{HOME}/ps4dev/ffmpeg-ps4")
    if(NOT EXISTS "${FFMPEG_PS4_PREFIX}/lib/libavcodec.a")
        message(FATAL_ERROR
            "PS4 FFmpeg not found at ${FFMPEG_PS4_PREFIX}. "
            "Run: scripts/build_ffmpeg_ps4.sh")
    endif()

    add_executable(moonlight-ps4.elf
        src/main.c
        src/stream.c
        src/ports/ps4_entropy.c
        src/orbis/net_orbis.c
        src/orbis/videodec2_loader.c
        src/audio/audio_orbis.c
        src/video/video_stats.c
        src/video/renderer_videoout.c
        src/video/nv12_blit.c
        src/video/decoder_ffmpeg.c
        src/video/decoder_orbis.c
        src/input/input_pad.c
        src/ui/ui_draw.c
        src/ui/ui_theme.c
        src/ui/ui_art.c
        src/ui/ui_menu.c
        ${COMMON_SOURCES}
        ${GAMESTREAM_SOURCES}
    )
    target_include_directories(moonlight-ps4.elf PRIVATE
        src
        ${FFMPEG_PS4_PREFIX}/include
        third_party/h264bitstream
    )
    target_link_libraries(moonlight-ps4.elf PRIVATE
        moonlight-common-c
        enet
        mbedtls mbedx509 mbedcrypto
        opus
        h264bitstream
        ${FFMPEG_PS4_PREFIX}/lib/libavcodec.a
        ${FFMPEG_PS4_PREFIX}/lib/libavutil.a
        -lSceNet
        -lSceAudioOut
        -lSceVideoOut
        -lSceGnmDriver
        -lScePad
        -lSceUserService
        -lSceSystemService
        -lSceSysmodule
    )
    # decoder_orbis.c always linked; prefer_hw in INI selects at runtime.
    target_compile_definitions(moonlight-ps4.elf PRIVATE ML_ENABLE_VIDEODEC2=1)

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/eboot.bin
        COMMAND ${CMAKE_COMMAND} -E env OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}
                ${OO_HOST_TOOLS}/create-fself
                -in=$<TARGET_FILE:moonlight-ps4.elf>
                -out=${CMAKE_BINARY_DIR}/moonlight-ps4.oelf
                --eboot ${CMAKE_BINARY_DIR}/eboot.bin
                --paid 0x3800000000000011
        DEPENDS moonlight-ps4.elf
        COMMENT "Generating eboot.bin (fself)"
        VERBATIM
    )

    add_custom_target(pkg ALL
        COMMAND ${CMAKE_COMMAND} -E env OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}
                ${CMAKE_SOURCE_DIR}/scripts/make_pkg.sh
                ${CMAKE_BINARY_DIR}/eboot.bin ${CMAKE_BINARY_DIR}
        DEPENDS ${CMAKE_BINARY_DIR}/eboot.bin
        COMMENT "Packaging .pkg"
        VERBATIM
    )
else()
    # Linux development CLI (pairing / video dump)
    find_package(PkgConfig)
    pkg_check_modules(HOST_FFMPEG IMPORTED_TARGET libavcodec libavutil)

    add_executable(moonlight-cli
        src/host/main_cli.c
        ${COMMON_SOURCES}
        ${GAMESTREAM_SOURCES}
    )
    target_include_directories(moonlight-cli PRIVATE src third_party/h264bitstream)
    target_link_libraries(moonlight-cli PRIVATE
        moonlight-common-c
        enet
        mbedtls mbedx509 mbedcrypto
        opus
        h264bitstream
        pthread
        m
    )
endif()
