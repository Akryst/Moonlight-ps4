# Controller, recovery, network and surround integration

Source: [raferrazz/Moonlight-ps4](https://github.com/raferrazz/Moonlight-ps4),
implementation commit `5cc1217`, inspected at `d2422196c0ac662116cfb6015944c37e7f70b388`.
Adapted under the repository's GPLv3 license. Thanks to raferrazz for the
controller, launch-flow, HTTP, audio and presentation work.

These changes ship in **1.4.0**, PS4 package **01.40**, following the local
01.36 COOP1 test build.
The existing portrait UI, five themes, splash transitions, designed PNG prompts,
Guide-chord fix, deadzone/swap preferences and Steam/Playnite companion remain.
The companion still runs **once per app opening**, never on a timer, menu refresh
or return from a stream. Its optional network request now runs in the background.

## Included

- Four logged-in DS4 users, separate P1-P4 slots and player lightbar colors;
  rumble and Sunshine/Steam RGB callbacks reach the corresponding physical pad.
- Touchpad mouse on P1: drag, physical left click, two-finger right click and
  high-resolution scrolling. Settings can disable mouse mode.
- Host-requested gyro/accelerometer reports, capped at the 4 ms input cadence.
  Settings -> Controller has global motion and a persistent override for the
  game selected in Library. Per-title overrides use Sunshine's numeric app IDs.
- Automatic stream-resume attempts with a cancel/retry screen; resume does not
  cancel a running game. Initial launch can clean up a stuck host session and
  retry in the same action. Launch errors retain the PC's own answer.
- Animated launch screen: actual game label, live HTTP step, elapsed time,
  Circle cancel; X retries and Triangle refreshes the real application list.
- Settings -> Exit -> Exit Moonlight uses `sceSystemServiceLoadExec("exit")`.
  Seven fault handlers leave a signal marker and terminate; they do not fix a
  fault's root cause or guarantee clean behavior for an external system kill.
- Persistent app-list cache plus the existing bounded disk artwork cache.
  Network refresh and artwork decoding stay on workers. The menu flips only
  for changed content, transitions or a failed presentation retry.
- Native TCP uses the working blocking mbedTLS path: Orbis rejects nonblocking
  socket flags. Only the request's worker can wait for the kernel SYN timeout;
  cancelled workers own captured data and cannot write into a departed menu.
- Request-specific HTTP budgets, per-thread errors, separate pool/crypto locks,
  keep-alive for read-only requests and full TLS handshakes. Launch/resume/cancel
  always use fresh sockets and never receive the HTTP layer's automatic retry.
- Write-combined menu presentation alias, with the original mapping fallback.
- Optional 5.1 (Settings -> Audio) through an 8-channel HDMI port; failure to open
  that port automatically uses a clipped stereo downmix. Stereo remains default.
- The imported stream quick menu opens with L3+R3 held for 2.5 seconds (BGRA).
  It exposes touchpad, stats, bitrate and return/quit actions. Held inputs are
  released while it is open and must return to neutral before passthrough.

## Integration fixes beyond the fork

- Heap-owned cancellable connection/launch jobs replace an abandoned stack job.
- Server-information pointers are rebound after copying a worker result,
  including the RTSP URL; no pointer may reference the freed worker.
- Reconnect resumes instead of using the fork's cancel/relaunch recovery path.
- Launch announces all four controller slots and negotiates surround before
  sending `/launch`, including the first launch after changing audio settings.
- 16-bit host rumble strength is scaled into the DS4's 8-bit motor range.
- Cross-thread status, quick-menu snapshots, input callbacks and error strings
  are synchronized; the initial motion request is not reset after connecting.
- The existing five-theme selection and older saved preferences are preserved.

## Validation

`bash preview/run_native_checks.sh` runs ASan/UBSan checks for drawing, artwork
worker/cancellation/cache limits, menu state, preferences, HTTP deadlines and
truncation, thread-specific errors, Playnite, four-controller input, Guide pulse,
motion report cadence, touch mouse, 5.1 expansion/stereo fallback/clipping,
launch/resume cancellation/error handling and stream-menu actions.
Controller/audio checks use mocked device APIs with the installed OpenOrbis
headers. They do not replace a physical-console test.

Still required on PS4: pairing/connect/launch, a mid-stream network drop and
resume, four logged-in physical controllers, motor intensity and host RGB,
touch gestures, IMU units/orientation in a motion-aware game, 5.1 channel routing
on an AVR, idle-menu presentation and both title-side/system close paths.
No new hardware result or performance improvement is claimed yet.
