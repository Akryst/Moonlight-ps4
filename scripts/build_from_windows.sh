#!/usr/bin/env bash
# Invoke with wsl.exe -d Ubuntu -- bash scripts/build_from_windows.sh from the repository root.
set -euo pipefail
SOURCE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${PS4_CLIENT_WORK:-$HOME/ps4dev/moonlight-client}"
if [[ "$SOURCE" == "$WORK" ]]; then
    echo "Use this script from the Windows source checkout, not the Linux build mirror." >&2
    exit 1
fi
mkdir -p "$WORK/third_party"
# Keep dependencies and build outputs on the Linux filesystem. DrvFS owner /
# chmod semantics can otherwise break git clones and executable scripts.
tar -C "$SOURCE" --exclude=.git --exclude=third_party --exclude=build --exclude='build-*' --exclude=dist -cf - . |
    tar -C "$WORK" -xf -
cp "$SOURCE/third_party/DEPS" "$WORK/third_party/DEPS"
cd "$WORK"
chmod +x scripts/*.sh
if [[ ! -f third_party/mbedtls/framework/CMakeLists.txt ]]; then
    scripts/setup_deps.sh
fi
if [[ ! -f "$HOME/ps4dev/ffmpeg-ps4/lib/libavcodec.a" ]]; then
    scripts/build_ffmpeg_ps4.sh
fi
if [[ -f tests/run_host_tests.sh ]]; then
    bash tests/run_host_tests.sh
else
    echo "Host tests not included in this checkout; building the client only."
fi
scripts/build_pkg.sh
mkdir -p "$SOURCE/dist"
VERSION="$(sed -n 's/^VERSION="\([^"]*\)"/\1/p' scripts/make_pkg.sh)"
cp "build-ps4/Moonlight-${VERSION}.pkg" "$SOURCE/dist/Moonlight-PS4-${VERSION}-test.pkg"
cd "$SOURCE/dist"
sha256sum "Moonlight-PS4-${VERSION}-test.pkg" > SHA256SUMS
echo "PKG: $SOURCE/dist/Moonlight-PS4-${VERSION}-test.pkg"
