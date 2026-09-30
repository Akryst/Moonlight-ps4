# Build Moonlight PS4

Run commands from the repository root. The current package version is 01.34.

## Windows with Ubuntu WSL

Prepare the environment once:

```powershell
wsl -d Ubuntu -- bash scripts/bootstrap_ubuntu.sh
```

Build and export the package (local host tests run if present):

```powershell
wsl -d Ubuntu -- bash scripts/build_from_windows.sh
```

Output: `dist/Moonlight-PS4-01.34-test.pkg` and `dist/SHA256SUMS`.
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

Output: `build-ps4/Moonlight-01.34.pkg`.
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
