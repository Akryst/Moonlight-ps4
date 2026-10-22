# Moonlight PS4 1.4.0

The library finally feels at home on a console. Portrait covers, game backgrounds,
proper controller images, smoother transitions and settings that don't cram
everything into one list. The new startup splash stays. The dev-version toast goes.

This release also brings in controller, network, recovery and audio work adapted
from [raferrazz/Moonlight-ps4](https://github.com/raferrazz/Moonlight-ps4), while
keeping the current interface and the Steam/Playnite companion.

## included

- Up to four logged-in DS4s, separate player slots and lightbar colors, host RGB
  changes and rumble. A 4 ms input interval targets 250 Hz.
- Touchpad mouse with drag, click, two-finger right click and scrolling.
  Host-requested gyro/accelerometer reports, with global and per-game settings.
- The short L1 + R1 + Options Guide pulse, plus deadzone and face-button preferences.
- Animated game launches with real names, live steps, elapsed time and Circle
  cancel. X retries an error; Triangle refreshes the application list.
- Automatic stream-resume attempts and recovery for a stuck initial host session.
- Disk-cached game lists and covers, background refresh/decoding, request-specific
  HTTP budgets, keep-alive and fresh connections for state-changing requests.
- Menu presentation through a write-combined alias, with flips only on changes.
- Optional 5.1 HDMI audio and an automatic stereo downmix if the 8-channel port
  can't be opened. Stereo remains the default.
- Settings -> Exit -> Exit Moonlight, plus fault-exit markers for diagnostics.
- Installed Playnite detection through the optional Windows companion. Steam and
  Playnite synchronization runs once per app opening, without a repeating timer.

Hold L3 + R3 for 2.5 seconds to open the stream menu in BGRA mode. It has touchpad,
stats, bitrate and quit controls. The physical PS button stays with the console.

## install

Install `Moonlight-PS4-01.40.pkg` through GoldHEN's Package Installer. The GitHub
version is **1.4.0** and the PS4 package version is **01.40**. Title ID `MLNT00001`
and saved settings under `/data/moonlight` are unchanged.

The optional `Moonlight-PS4-Steam-Companion-01.40.zip` includes the Windows
installer, updater, synchronization scripts and setup guide. Update existing
companion scripts for Playnite detection. It needs Python and Pillow.
`SHA256SUMS` covers both downloads.

The README now shows the two latest actual PS4 captures, Library and Settings.
They were taken on the preceding 01.36 test build, before the version and
startup-notification change.

## validation

Native PS4 compilation, automated input/audio/HTTP/UI/preferences checks,
regression tests and PKG hash/signature validation passed. The screenshots are
real console captures. Four physical controllers together, motion calibration,
5.1 channel routing, drop recovery and exit behavior still need broader console
testing. Fault handlers record and terminate; they don't repair a fault's cause
or guarantee behavior when the system forcibly closes the app.

The tested platform is an original PS4 on firmware 9.00 with GoldHEN. This remains
experimental homebrew; locked 60 FPS and other firmware/model combinations aren't
verified. Detailed changes and limitations: [integration notes](FORK_INTEGRATION.md).

Maintained and released by [Akryst](https://github.com/Akryst).
