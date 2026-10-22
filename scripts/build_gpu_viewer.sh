#!/usr/bin/env bash
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$REPO_DIR/scripts/env.sh"
BUILD_DIR="${BUILD_DIR:-$REPO_DIR/build-ps4}"
cmake --build "$BUILD_DIR" --target moonlight-gpu-viewer.elf
OUT="$BUILD_DIR/gpu-viewer"
mkdir -p "$OUT"
"$OO_PS4_TOOLCHAIN/bin/linux/create-fself" \
    -in="$BUILD_DIR/moonlight-gpu-viewer.elf" -out="$OUT/gpu-viewer.oelf" \
    --eboot "$OUT/eboot.bin" --paid 0x3800000000000011
ML_PKG_TITLE="GPU Snapshot Viewer" ML_PKG_TITLE_ID="MLGV00001" ML_PKG_CATEGORY=gd \
    ML_PKG_CONTENT_ID="IV0000-MLGV00001_00-MOONLIGHTGPUVIEW" \
    "$REPO_DIR/scripts/make_pkg.sh" "$OUT/eboot.bin" "$OUT"
