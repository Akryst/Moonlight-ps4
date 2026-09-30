# Presentation experiment — 01.34

## Stellar Blade baseline: 01.33

An incremental live-log sample excluded earlier games and stream startup:
217.07 seconds, 59.020 client-reported FPS, 33 local drops and 33
`present_queue no_slot` drops. Decoder backlog/stale-queue drops and flip-submit
failures were zero. Three source frame-number gaps were reported; these counters
do not establish packet loss or the game's own FPS.

Decode averaged 5.605 ms, conversion 1.967 ms. There were 3,628 slot waits among
12,840 attempts, averaging 3.581 ms when waiting, with a 9.2 ms maximum.
These are rounded log summaries; the interval sets have different boundaries.

## Changes

- Create a flip-event queue lazily for streaming. Wait for a VideoOut event using
  the remaining slot deadline instead of polling every 500 microseconds.
- On queue setup/wait errors, fall back to polling. Normal event timeouts do not
  disable the event path. Destroy the queue at session reset/shutdown or port reopen.
- Keep the existing Balanced 8 ms / Low latency 4 ms maximum wait; OS scheduling
  may exceed a requested timeout. Registration overhead is outside the slot timer.
- Select a buffer from one status snapshot. Never select the current display,
  converting buffer or known pending buffer. If multiple flips obscure queued
  indices, wait rather than guessing. Failed status queries do not permit a write.
- Decode every H.264 unit to preserve references. Recheck the existing 33 ms
  decoder-queue freshness threshold after Decode and immediately before BGRA
  conversion. An aged decoded image can be skipped without skipping reference
  decoding. Slot waiting is capped by the remaining freshness budget.
- Keep three BGRA buffers. Do not cancel or overwrite queued flips.

The API declarations follow the installed OpenOrbis 0.5.4 headers:
[`VideoOut.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/VideoOut.h)
and [`libkernel.h`](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h).
Host checks cover occupied/unknown slots and wait-deadline/freshness boundaries;
they do not exercise PS4 event delivery or measure console latency.

## Compare on the console

Install `dist/Moonlight-PS4-01.34-test.pkg` after the current session. Repeat a
comparable Stellar Blade location for at least three minutes with 1080p60,
20 Mbps, Hardware decoding, Balanced, Performance overlay and File logging.

Look for `present: flip_events=enabled rc=0x00000000`. The analyzer supports new
`present_timing` fields: observed show delay, peak pending flips, events,
timeouts, event errors and status errors. Compare FPS, local drops, waits and
stale-queue discards with 01.33; also check controller response and audio.

`observed_show_*` measures submission until the application first observes that
buffer as displayed. It is an upper-bound observation affected by status-query
timing, not exact scanout timing, input-to-photon latency or decoder queue age.
Some displayed frames may not be observed individually between status queries.

Host tests and packaging pass. Runtime event behavior, return to menu/relaunch,
repeated session cleanup and any fluency/latency improvement require PS4 testing.
