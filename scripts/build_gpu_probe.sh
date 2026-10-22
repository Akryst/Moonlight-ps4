#!/usr/bin/env bash
# Run from the configured Linux build mirror, after scripts/build_pkg.sh.
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$REPO_DIR/scripts/env.sh"
BUILD_DIR="${BUILD_DIR:-$REPO_DIR/build-ps4}"
cmake --build "$BUILD_DIR" --target moonlight-gpu-probe.elf
OUT="$BUILD_DIR/gpu-probe"
mkdir -p "$OUT"
# Match the SDK Piglet sample's execution attributes, not the streaming app's.
AUTHINFO="000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
"$OO_PS4_TOOLCHAIN/bin/linux/create-fself" \
    -in="$BUILD_DIR/moonlight-gpu-probe.elf" -out="$OUT/gpu-probe.oelf" \
    --eboot "$OUT/eboot.bin" --paid 0x3800000000000035 --authinfo "$AUTHINFO"
ML_PKG_TITLE="Moonlight GPU Probe" ML_PKG_TITLE_ID="MLGP00001" ML_PKG_CATEGORY=gde \
    ML_PKG_CONTENT_ID="IV0000-MLGP00001_00-MOONLIGHTGPUPROB" \
    "$REPO_DIR/scripts/make_pkg.sh" "$OUT/eboot.bin" "$OUT"
