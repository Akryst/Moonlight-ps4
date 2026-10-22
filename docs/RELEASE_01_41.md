# Moonlight PS4 1.4.1

the extra overlays were clanked. this patch fixes the layout and a real bug in
the statistics panel.

The stream menu now sits to the side, with a clear white selection, shorter
labels and the same PNG controller buttons as the library. Bitrate controls and
quit actions have their own space. The statistics panel is smaller, with fixed
label/value columns so the numbers don't run into the text.

**The stats panel also wrote five rows into an array sized for four.** That's
fixed. The new renderer bounds its history reads and handles invalid timing
values. Controller images are loaded before streaming instead of being read or
decoded while drawing a video frame.

The hold indicator and shortcut hint got the same visual cleanup. The overlays
use a fixed dark palette for now; they don't follow the selected menu theme.
Everything else from 1.4.0 stays included, and the startup version toast stays gone.

## install

Install `Moonlight-PS4-01.41.pkg` through GoldHEN's Package Installer. Runtime
version is **1.4.1**, PS4 package version is **01.41**, and title ID is still
`MLNT00001`. Existing settings stay in `/data/moonlight`.

The Windows companion hasn't changed. Keep your 1.4.0 installation, or download
it from the [1.4.0 release](https://github.com/Akryst/Moonlight-ps4/releases/tag/v1.4.0)
if you need Steam/Playnite synchronization. `SHA256SUMS` covers this release's PKG.

## checked

Native PS4 compilation and automated checks pass, including ASan/UBSan drawing
checks at 1080p, 720p and 480p. Those checks exercise every menu row, padded
framebuffer boundaries, oversized history counts and non-finite timing values.
The PKG passes hash/signature validation.

The previews use simulated numbers on a PC. Console appearance and the overlay's
frame cost still need a physical PS4 check. This remains experimental homebrew.

Maintained and released by [Akryst](https://github.com/Akryst).
