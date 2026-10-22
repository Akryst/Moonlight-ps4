# Build Moonlight PS4

Run commands from the repository root. Release 1.4.1 uses PS4 package version 01.41.

## Windows with Ubuntu WSL

Prepare the environment once:

```powershell
wsl -d Ubuntu -- bash scripts/bootstrap_ubuntu.sh
```

Build and export the package (local host tests run if present):

```powershell
wsl -d Ubuntu -- bash scripts/build_from_windows.sh
```

Output: `dist/Moonlight-PS4-01.41-test.pkg` and `dist/SHA256SUMS`.
The script mirrors source to `~/ps4dev/moonlight-client` to keep dependencies
and build outputs on the Linux filesystem. At the repository root, `build.bat`
invokes the same build.

## Ubuntu / Linux

```bash
bash scripts/bootstrap_ubuntu.sh
bash scripts/setup_deps.sh
bash scripts/build_ffmpeg_ps4.sh
if [ -f tests/run_host_tests.sh ]; then bash tests/run_host_tests.sh; fi
bash scripts/build_pkg.sh
```

Output: `build-ps4/Moonlight-01.41.pkg`.
The scripts prepare OpenOrbis 0.5.4, host LLVM/Clang tools and PS4 FFmpeg.
Dependencies are pinned in `third_party/DEPS`; local PS4 changes are reapplied
from `patches/`. Do not commit patched dependency working trees into submodules.

## Checks and previews

Host tests cover color conversion, UI drawing/artwork, discovery, configuration,
statistics and Steam synchronization. C checks use address and undefined-behavior
sanitizers. Python with Pillow is needed for artwork generation/conversion.

Testing sources and fixtures are local and excluded by `.gitignore`. If your
workspace contains them, run `bash tests/run_host_tests.sh` after dependencies
are prepared. The client can also be built from a checkout without this suite.
Native UI previews can be generated in the Linux build mirror with
`bash scripts/render_ui_preview.sh` and converted using
`scripts/collect_ui_preview.py`. See `design/ASSETS_01_31.md` for asset provenance.

Analyze a console log:

```bash
python3 scripts/analyze_stream_log.py debug.log --output report.json
```

Compilation and host tests do not establish console performance. A fresh-clone
build on a clean environment, real PS4 screenshots, license/redistribution review
and console validation of 01.34 remain necessary before a public release.

The supplied reference was based on upstream commit
`61427a214d4e632ee246816a98ee4f2374844a73`.

## Desktop menu preview

The isolated libcross2d desktop menu opens with `preview.bat` on Windows/WSLg.
See [preview/README.md](../preview/README.md) for dependencies, controls and
renderer snapshots. This target does not build or replace the PS4 client.

## Native UI test build (1.3.6-dev-ui2)

`dist/Moonlight-PS4-01.36-UI-test.pkg` contains the console redesign: a portrait
cover rail, selected-game backdrop, contextual Settings, packaged monochrome PS4
button images and a splash with a fade into the menu. All themes keep the same
layout and retain their colour palettes. Navigation/focus changes crossfade
together over 240 ms; splash fade-in takes 380 ms. These times follow elapsed
time rather than a fixed number of frames.

The new design runs in the existing native BGRA menu renderer, not the isolated
libcross2d prototype or the Piglet probe. It needs one additional 1920 × 1080
staging buffer (about 8 MiB) while the menu is open. Allocation failure disables
transitions and keeps navigation available. The buffer is freed before streaming.
No artwork requests or UI animation run during streaming.

Actual native drawing code is exercised by `scripts/render_ui_preview.sh` in the
Linux build mirror. Its host captures can be reviewed as `dist/PS4-Library.png`,
`dist/PS4-Settings.png` and `dist/PS4-Splash.png`; they are not console screenshots.
`preview/native_ui_check.c` checks clipped alpha compositing, in-place crossfade,
row padding and transition bounds. Real PS4 timing, startup and stream return
must still be checked on the console before release.

## Library update (1.3.6-dev-ui3)

The next console test package is `Moonlight-PS4-01.36-UI3-test.pkg`.

- Artwork loads on a menu-scoped worker. The main drawing path uses placeholders
  or decoded memory slots and never waits for an artwork HTTP request. Work is
  cancelled and joined before streaming, refreshing the catalog or closing a
  host application. HTTP transfers have total deadlines; IPv4 connects are
  bounded as well. Configured DNS names retain the existing system resolver.
- Valid PNGs are saved atomically under `/data/moonlight/artwork`, separated by
  host address, port and certificate fingerprint. The disk cache is limited to
  128 files / 64 MiB; stale covers are refreshed after seven days. Invalid PNGs
  are rejected. Eight decoded memory slots bound the active artwork set.
- The selected app is remembered independently of the launched app. Selection
  writes are debounced by one second and configuration replacement is atomic.
  Cross resumes the selected active app; Square focuses the active app. Options
  asks before closing it. Starting another app also requires closing the current
  session explicitly first.
- The portrait rail slides and the focus grows using elapsed time. Reduced
  motion disables rail interpolation, crossfades and splash animation.
- Settings groups are Connection, Video, Audio, Controller and Advanced. Audio
  exposes the existing PC audio option. Controller provides a saved stick
  deadzone (0–25%) and host Cross/Circle swap; console menu controls stay fixed.
- Connection/start failures show Retry and Choose computer. An interrupted
  stream retains the last app selection for the next successful connection.

Run `bash preview/run_native_checks.sh` in the prepared Linux checkout. It checks
worker cancellation/persistent cache/eviction, menu actions and motion, new
preference migration and persistence, all stick input values, HTTP deadlines
and cancellation, and Playnite detection/merge behavior. C checks use ASan/UBSan.
The Windows build launcher runs these before the existing host suite.

Playnite requires the updated Windows companion; see [STEAM.md](STEAM.md).
Real PS4 thread lifecycle, animation smoothness, input calibration, reconnect
and launcher behavior remain console validation requirements.

`Moonlight-PS4-01.36-UI4-test.pkg` fixes UI3's IPv4 connect regression on Orbis:
the nonblocking socket path now initializes FreeBSD's `sockaddr_in.sin_len`,
matching the existing discovery code, and explicitly selects TCP. Socket mode,
connect and wait failures now record their stage and native error. Linux HTTP
checks cannot establish compatibility with the console's socket ABI.

UI4's console log additionally shows `F_SETFL(O_NONBLOCK)` returning EACCES (13).
`Moonlight-PS4-01.36-UI5-test.pkg` restores the previously working mbedTLS TCP
connect path on Orbis. Desktop retains its bounded nonblocking connect. On PS4,
the initial TCP connect uses the OS timeout; HTTP deadlines and cancellation
apply after that connection is established. UI3's library features are retained.

`Moonlight-PS4-01.36-UI6-test.pkg` updates the eight controller PNGs with bevels,
soft reflections and embossed symbols. Assets remain 128 × 128 RGBA under
32 KiB each; shading is pre-rendered and the existing alpha-image renderer is
reused. The native transport remains the UI5 implementation.

### 01.36 COOP1

See [fork integration](FORK_INTEGRATION.md) for the imported features,
compatibility fixes, controls and remaining physical-console checks.
Build artifact: `Moonlight-PS4-01.36-COOP1-test.pkg`.

## Release 1.4.0

The release asset is `Moonlight-PS4-01.40.pkg`. The local Windows build helper
retains its `-test.pkg` suffix. Version logging remains enabled; the startup
version notification has been removed. See [release notes](RELEASE_01_40.md).

## Stream overlay test build

`Moonlight-PS4-01.40-OVERLAY1-test.pkg` replaces the central quick menu with a
right-side panel matching the portrait UI, PNG action prompts and shorter labels.
Statistics use fixed label/value columns and bounded history sampling. The old
statistics code wrote a fifth row into a four-row array; the new renderer removes
that overflow. Drawing lives in `src/ui/ui_stream.c` so the exact runtime output
can be checked with ASan/UBSan on the host at 1080p, 720p and 480p. Test values in
rendered previews are simulated, not console measurements.

Controller PNGs are preloaded before streaming, avoiding file reads/decodes on
the video presentation thread. The startup version notification remains removed.
The internal log version is `1.4.1-dev-overlay1`; the PS4 test package remains
01.40. Physical-console appearance and overlay frame cost still need validation.

## Release 1.4.1

The overlay changes above ship in `Moonlight-PS4-01.41.pkg`, with runtime version
1.4.1. The startup version toast remains removed. The release includes the PS4
package and `SHA256SUMS`; the Windows companion is unchanged from 1.4.0.
See [release notes](RELEASE_01_41.md).
