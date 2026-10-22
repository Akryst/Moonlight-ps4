# Moonlight library preview

The current PS4 redesign is implemented in `src/ui/ui_design.inc` and
`src/ui/ui_menu.c`. This isolated libcross2d prototype retains the earlier
desktop layout; it is not the UI shipped by the new console test package.
Review the native renderer captures under `dist/PS4-*.png` for that package.

Desktop-only libcross2d menu. The PS4 executable, Guide shortcut, themes, decoder,
audio and VideoOut renderer are not modified. On this Windows machine the launcher
reads `C:\Program Files\Sunshine\config\apps.json` and displays its application
names and local PNG artwork. It reads no credentials, executes no app commands,
and writes no Sunshine configuration. This is a local catalog snapshot, not a
paired network connection or a streaming client.

Without a local Sunshine catalog it uses six fictional games with original
geometric artwork. Force that mode with `bash scripts/build_ui_preview.sh --demo`.

Open **preview.bat** from Windows (Ubuntu WSL with WSLg), or run:

```sh
bash scripts/build_ui_preview.sh
```

Requirements inside Ubuntu:

```sh
sudo apt-get install --no-install-recommends cmake ninja-build g++ git pkg-config python3 \
  libsdl2-dev libfreetype-dev libpng-dev libconfig-dev libglm-dev libgl1-mesa-dev
```

The first configure fetches libcross2d at
`9dfe2fcb4ccb2dd7be04718ee7ddb450abd85de1`; later builds reuse it under
`~/ps4dev/moonlight-preview`. Upstream is GPL-3.0; its source, license and embedded
font notices remain in the dependency checkout. No dependency enters the PS4 build.

Controls: arrows / D-pad move focus, Enter / Cross opens the selected game's
information, Escape / Circle returns (and exits from the library). Tab / Options
opens configuration; Q / L1 returns to the library, E / R1 opens configuration.
Space / Square opens details in the library or restores draft defaults in
configuration. F / Triangle opens the profile. Mouse clicks also select cards,
open either section, change values and activate save/restore buttons.

The carousel shows four applications per page. Left/right crosses page boundaries;
up/down moves a page while preserving the slot. Only the four visible cover
textures and selected hero artwork are kept in memory.
PNG covers are inspected before decoding (16 MiB file / 4096 x 4096 dimension
limits). Missing, corrupt or unsupported covers use the geometric fallback.
Existing embedded `mlBg` panoramas from the Steam companion are extracted to the
build cache and used without altering their source images. PNG artwork is
center-cropped to fill its display area without stretching.

The English interface has one top navigation bar (Library / Settings), large
artwork, a restrained full-width hero and TV-friendly margins. Desktop uses an
original monitor illustration with a small animated 3D mesh. Selection has an
interpolated lift, scale and shadow rather than a large foreground decoration.
The mesh is rotated/projected from 3D coordinates, depth-sorted and drawn through
libcross2d's existing triangle batch; it introduces no new graphics runtime.
The PS4 controls are original transparent PNG images, including face buttons,
D-pad, Options, L1 and R1. CMake copies the images and bundled DejaVu Sans font
into the build's assets directory. See `assets/README.md` for regeneration and
font licensing. The UI has no Sunshine/local-catalog status badge.

Configuration supports 720p/1080p, 30/60 FPS, 10/20/30/50 Mbps and reduced motion.
Arrows change a selected value; Cross changes values or activates the selected
restore/save action. Saving writes an atomic desktop profile under
`~/.config/moonlight-ps4-preview/settings.cfg`. Invalid profiles show a notice;
they are not overwritten simply by opening the app. Reduced motion immediately
stops the monitor mesh rotation and selection interpolation. Settings are grouped
under Video and Interface with contextual help. Other settings describe a
future streaming session: they do not change Sunshine or the PS4 client.

The connection prompt is a preview state; launching a real game is not connected.
SDL handles game controllers present when the application starts.

Generate an actual renderer capture without keeping a window open:

```sh
mkdir -p dist
bash scripts/build_ui_preview.sh --snapshot "$PWD/dist/Moonlight-Console-Library.png"
bash scripts/build_ui_preview.sh --settings-snapshot "$PWD/dist/Moonlight-Console-Settings.png"
```

The capture is read from the rendered GL backbuffer. It is not an HTML mockup or
an image of a PS4 run. CTest checks navigation boundaries, incomplete rows, empty
catalog handling, cross-page navigation and focus preservation while viewing
details. Additional checks cover malformed catalog input, string escaping,
read-only export, invalid PNG chunks and preservation of a previous good export.
Profile checks cover defaults, option ranges, round-trip persistence and rejection
of invalid values; projection checks verify finite geometry and rotation length.
An asset check decodes every packaged controller image and verifies its size,
transparency and nonempty content. Portrait artwork is contained, rather than
cropped, to keep launcher icons legible; landscape artwork fills its frame.

`scripts/export_preview_catalog.py` uses Python's standard library and exports
only names/cover paths to `catalog.cfg` in the build directory. The C++ reader
uses libconfig, already required by libcross2d. To preview another local Sunshine
configuration, export it with `--source`/`--output` and pass `--catalog catalog.cfg`
to the launcher. Invalid explicit imports fail instead of silently showing demos.

The first WSL run used Mesa llvmpipe (CPU OpenGL), not the PS4 GPU. Upstream also
attempts several optional CRT filters whose shaders fail on this desktop profile;
the menu uses the working color/texture programs and none of those filters. No
performance comparison is inferred from this preview. PS4 integration and physical
controller validation remain pending.
