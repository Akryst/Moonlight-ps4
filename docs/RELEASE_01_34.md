# Moonlight PS4 01.34 — Experimental

An unofficial Moonlight client for PS4, tested with an original console on
firmware 9.00 and GoldHEN. Stream games from Sunshine using a DualShock 4
connected to the console. The target is H.264 1080p60; locked 60 FPS is not guaranteed.

## Changes included

- English PCs / Games / Settings UI, local IPv4 mDNS discovery, manual IP entry
  and PIN pairing.
- Sunshine game listing, launch/resume, Desktop streaming and return to menu.
- Five selectable themes: Midnight, Mono, Blue Wave, Daylight and Cinema, with
  saved preferences, matching startup splashes and the Moonlight logo.
- Proportional typography, game covers and panoramas, menu-only artwork caching,
  and an actual PS4 screenshot in the README.
- Optional Windows companion to publish installed Steam games and local artwork
  to Sunshine; preserves manual entries and defers reload while streaming.
- DualShock 4 input, L1 + R1 + Options for the host Guide button, and
  Options + Touchpad held for about one second to return to the client menu.
- H.264 hardware decoding, stereo Opus, Balanced / Low latency modes, performance
  overlay, corrected FPS/drop counters and file logging.
- Rec.709 limited-to-full color conversion with matching scalar/SSE2 paths.
- **New in 01.34:** event-driven VideoOut waits with polling fallback, buffer
  selection from one status snapshot, post-decode freshness checks and wait
  limits for aged frames, plus pending-flip and observed presentation diagnostics.
- Consolidated repository layout, release-oriented documentation, root ignore
  rules and an updated WSL/OpenOrbis build/export workflow. Test files stay local.

## Assets and installation

- `Moonlight-PS4-01.34-test.pkg`: install through GoldHEN Package Installer.
- `Moonlight-PS4-Steam-Companion-01.34.zip`: optional Windows scripts and setup guide;
  requires Python with Pillow and an administrator PowerShell for installation.
- `SHA256SUMS`: checksums for both files.

Title ID: **MLNT00001**. Existing settings are retained in `/data/moonlight`.
Discover the host with Triangle in PCs, or enter its IP in Settings. Connect,
enter the displayed PIN in Sunshine and select a game. For TruckersMP, use Desktop.

Starting settings: **1080p / 60 FPS / 20,000 kbps / Hardware decoding / Balanced**.
Ethernet on both ends is the tested configuration. Configure Sunshine's gamepad
emulation on Windows; the test host uses ViGEmBus.

## Validation and known limitations

Host checks and OpenOrbis compilation/packaging pass, including buffer-selection
and deadline/freshness-boundary checks. Test files are excluded from the public
source tree. These checks do not establish runtime PS4 event delivery or latency.

01.33 ran on the user's console. A Stellar Blade sample recorded 59.020 client
FPS over 217.07 seconds, with 33 no-free-buffer drops. The new 01.34 policy is
experimental and **still requires console comparison**, including repeated return
to menu/relaunch, controller response and audio. No performance gain is claimed yet.

Firmware/model compatibility beyond the original PS4 on 9.00 is unverified.
Discovery is local IPv4. Keyboard/mouse and native YCbCr presentation remain
unfinished; experimental plugins are not part of the standard installation.
The reported sharpening appearance has not been conclusively diagnosed.

## Attribution

Based on JaimeJimenezG/Moonlight-ps4, moonlight-common-c and OpenOrbis, with Sunshine,
FFmpeg, Opus, mbedTLS and other pinned dependencies. Existing dependency licenses
and notices apply; font notices are included in the package. This project is not
affiliated with, endorsed by or maintained by the official Moonlight project.
