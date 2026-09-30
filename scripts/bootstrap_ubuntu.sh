#!/usr/bin/env bash
# Run inside Ubuntu/WSL. Installs host tools and a local OpenOrbis SDK.
set -euo pipefail
sudo apt-get update
sudo apt-get install -y clang lld llvm cmake ninja-build git curl build-essential pkg-config python3
DEV="${PS4_DEV_ROOT:-$HOME/ps4dev}"
mkdir -p "$DEV/downloads" "$DEV/hostlibs"
SDK="$DEV/downloads/openorbis-0.5.4.tar.gz"
if [[ ! -f "$DEV/OpenOrbis/PS4Toolchain/include/orbis/Net.h" ]]; then
    curl -fL --retry 3 -o "$SDK" \
        https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz
    echo "3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526  $SDK" | sha256sum -c -
    tar -xzf "$SDK" -C "$DEV"
fi
# PkgTool's old .NET runtime needs OpenSSL 1.1. Extract it privately;
# do not replace the operating system's OpenSSL libraries.
DEB="$DEV/downloads/libssl1.1.deb"
if [[ ! -f "$DEV/hostlibs/usr/lib/x86_64-linux-gnu/libssl.so.1.1" ]]; then
    curl -fL --retry 3 -o "$DEB" \
        https://archive.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb
    echo "7cf39d70a639017d1dd7c8d36daa2258063608688e449fddf40ffdd46f992a78  $DEB" | sha256sum -c -
    dpkg-deb -x "$DEB" "$DEV/hostlibs"
fi
ln -sf x86_64-linux-gnu/libssl.so.1.1 "$DEV/hostlibs/usr/lib/libssl.so.1.1"
ln -sf x86_64-linux-gnu/libcrypto.so.1.1 "$DEV/hostlibs/usr/lib/libcrypto.so.1.1"
echo "OpenOrbis ready: $DEV/OpenOrbis/PS4Toolchain"
