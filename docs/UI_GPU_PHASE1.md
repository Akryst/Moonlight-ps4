# UI modernization — Phase 1 only

Status: revision r10 passes GPU capture and sequential copied presentation within
one application on the development PS4. The user confirms a blue background and
red rectangle in GPU Probe; logs confirm EGL teardown/module unload followed by
300 native VideoOut submissions. Reducing system shared memory from 250 to 64 MiB
resolves the observed native allocation failure. Direct Piglet scanout remains
unproven. Phase 2 is still gated on repeated lifecycle/stream-reentry checks and
a suitable continuous presentation route; no UI GPU backend is enabled in Moonlight.

Host validation: the Guide filter and existing host suites pass; both the normal
01.36 client PKG and the standalone probe ELF/PKG compile and package with the
installed OpenOrbis toolchain. Inspecting the executables' dynamic imports confirms
that only the standalone probe depends on Piglet. These are build results, not
successful GPU execution or PS4 presentation results by themselves.

Console inspection (2026-10-20): the installed main package and latest boot log
confirm 01.36. GoldHEN FTP reports the console's Piglet runtime module present in
`/system/common/lib` (782,616 bytes). The separate probe package was not yet found
installed and no `gpu-probe.log` was available. Its PKG has been staged in
`/data/pkg/Moonlight-PS4-GPU-Probe-01.36.pkg`; a successful transfer does not count
as installation, runtime execution or visual validation.

The first probe installation failed with CE-32957-6. Its original title ID had
five letters and four digits; the corrected package uses `MLGP00001` and a matching
content ID. `make_pkg.sh` now rejects malformed or mismatched IDs before staging,
covered by `tests/test_pkg_metadata.sh`. The corrected package was transferred
again and its remote SHA-256 verified.

Hardware result for the first runnable revision: installation and module loading
succeed, but the screen stays black. Its log records `eglGetDisplay` returning
`EGL_NO_DISPLAY`; `eglGetError` reports `0x3000` (no additional EGL error). No EGL
context, frame presentation or teardown cycle has passed yet.

Revision r2 aligns the isolated probe's `CATEGORY=gde` and `FORMAT=obs` with the
SDK Piglet sample; the main package still defaults to `gd`. Its console log now
confirms EGL 1.4 and an OpenGL ES 2.0 Piglet context. The user still sees a black
screen and no completed frame appears in the log. Context creation is a partial
pass; presentation and teardown remain unvalidated.

Revision r3 loads the system's `libScePrecompiledShaders.sprx`, as the SDK sample
does. It presents before hiding the splash, moves pixel readback to the second
frame and logs clear/swap/readback boundaries. This revision has been compiled,
staged and checksum-verified. On-console logs stop at the first `eglSwapBuffers`.

Revision r4 changes swap interval from 1 to 0. Its hardware log records three
successful context lifecycles, 360 successful swap calls, three red RGBA readbacks
(`255,0,0,255`) and successful module unloads. The user reports that the TV remains
black throughout. These API results do not establish visible presentation.

Revision r5 explicitly loads internal System Service before hiding the splash,
following the SDK sample's setup. Its first cycle is extended to 600 frames to
allow time for the shell transition, followed by two 120-frame cycles. The log
confirms all 840 swaps, three red-pixel readbacks, three context teardown passes
and both successful module unloads. The user confirms that no rectangle appears
at any time: the screen remains black. Extra startup time and System Service
initialization have not resolved visible output.

The next investigation must establish how this firmware's Piglet/compositor
surface reaches the physical display, using a known visible reference and its
actual output ownership. A successful swap or pixel readback is insufficient.
Do not bridge arbitrary EGL pointers into the current VideoOut buffers or enable
automatic GPU selection on the basis of these API checks. Keep the CPU UI and
the existing streaming pipeline until that presentation route is proven.

Revision r6 is a sequential handover diagnostic. It captures two GPU frames,
reads the complete RGBA image, tears down EGL and unloads the modules before
calling the existing, unchanged `video_ui_begin` / `video_ui_present` functions.
An explicit CPU copy changes bottom-up RGBA into top-down BGRA. A focused host
test checks orientation, channels, alpha and destination bounds.

The full capture is saved as `/data/moonlight/gpu-probe-readback.bgra` (1280x720,
4 bytes/pixel); native presentation diagnostics go to `gpu-probe-videoout.log`.
On hardware, the full capture succeeded, but native VideoOut initialization in
that same process failed at `AllocateDirectMemory` with `0x80020023`. Successful
EGL teardown and module unload did not suffice for this handover. The allocation
failure does not establish whether the cause is pool availability, permissions
or retained ownership.

Revision r7 separates the GPU capture producer from a native `gd` viewer with no
Piglet imports. After closing the producer completely, **GPU Snapshot Viewer**
reads the saved image and uses the unchanged VideoOut presenter. Its console log
records successful initialization and 300 submitted flips. The user confirms the
blue background and static red rectangle on the TV. The WB_GARLIC mapping attempts
fail, but the existing ONION fallback maps and registers BGRA buffers successfully.

This validates GPU framebuffer → file → independent CPU viewer → TV. It does not
validate direct Piglet scanout, same-process handover, real-time rendering,
zero-copy sharing or improved performance. The full-frame conversion and strict
capture-size checks have focused host tests. A PNG of the recovered framebuffer
is saved locally as `dist/GPU-Readback-01.36.png`; it is not a photograph of the TV.

Revision r8 adds direct-memory checkpoints without changing Piglet configuration
or taking VideoOut ownership. Each checkpoint attempts the presenter's 12 MiB
allocation with 2 MiB alignment and memory type 3, then releases it immediately.
The stages are before module loading, after loading, after configuration, with
EGL current, after EGL termination and after module unload. Viewer r2 runs the
same check before native initialization as a `gd` control. Allocation failure is
recorded and execution continues; release failure aborts the experiment to avoid
accumulating diagnostic reservations. A focused host test covers successful
release, failed allocation and failed release. Both packages build and their
transfers are checksum-verified. These checks
are intended to distinguish a pre-existing process constraint from a change
during Piglet initialization, not to assert a memory leak from one failure.

Hardware r8/r2 result: the `gde` producer reports a direct-memory limit of
`0x30000000` (768 MiB), while the `gd` viewer reports `0x120000000` (4608 MiB).
The producer can allocate/release 12 MiB before module loading, after loading and
after configuration. Once EGL is current, allocation returns `0x80020023`; it
continues to fail after EGL termination and both module unloads. The viewer's
allocation/release passes and it submits 300 native flips. The user's "listo"
confirms running this test, not a new explicit observation of its displayed image.
This narrows the allocation failure to changes during EGL initialization; it does
not prove a leak or explain direct compositor scanout.

Revision r9 reduces the configured system/video shared reservations from 250/512
MiB to 2/36 MiB, as in [flatz's original probe](https://github.com/flatz/ps4_gl_test/blob/master/main.c).
Flexible-memory and command-buffer settings stay as in r8. It keeps all memory
checkpoints and restores the sequential native presenter test after EGL teardown
and module unload, using an explicit converted snapshot. The standalone package
builds and passes transfer checksum verification. On hardware r9 fails earlier:
`eglCreateContext` returns `EGL_NO_CONTEXT`, while `eglGetError` gives `0x3000`.
The user sees black. Native presentation is never reached; the existing native
log is from an earlier run and cannot establish an r9 VideoOut result.

Revision r10 restores the known-working 512 MiB video reservation and changes
only the system reservation from r8's 250 MiB to 64 MiB. It adds checkpoints after
EGL initialization and window creation, logs Piglet's reported configuration,
and adds a native log session delimiter at process startup. The logger appends;
only the last `----- log start -----` section belongs to the current launch.
All other EGL and memory settings match r8. The package compiles and its remote
SHA-256 is verified.

Hardware r10 result: Piglet reports the requested 64/512 MiB reservations and
170 MiB flexible setting. Every 12 MiB allocation/release checkpoint passes,
including while EGL is current and after teardown/unload. RGBA readback is red
(`255,0,0,255`), the full capture writes 3,686,400 bytes, teardown and both module
unloads pass. Native initialization succeeds and submits 300 flips. The user
confirms the blue background and static red rectangle in GPU Probe itself.
The native mapping attempts using Map2 fail with `0x8002002d`, then the existing
WC_GARLIC fallback succeeds, registers BGRA buffers and presents the snapshot.
This demonstrates one sequential capture/teardown/native-presentation cycle in
the same process. It does not establish simultaneous ownership, repeated-cycle
stability, direct compositor visibility, continuous animation or production speed.

Revision r11 (2026-10-22) keeps r10's configuration and runs three short EGL
create/draw/readback/destroy cycles before a fourth context attempts live copied
output. With that fourth context current, it initializes the unchanged native
presenter, then draws, checks a red pixel at the moving rectangle's current
position, reads the complete image, converts it to BGRA and submits it to VideoOut
for each of 600 iterations. No EGL/native buffer pointers are shared. Submission
successes and drops are counted separately; zero successful submissions fails
the cycle. Each GL/EGL/readback failure aborts the diagnostic. The CPU staging
buffer is freed and `video_ui_end` runs before EGL teardown; the existing renderer
keeps VideoOut open, so this is not a test of native Close/Unregister safety.

After teardown and module unload it saves and presents the last snapshot as in
r10, leaving a static image after the motion test. The native log session is
identified by `gpu-probe r11`. Both focused host suites (conversion and diagnostic
allocation/release) pass with address/undefined-behavior sanitizers; the package
compiles and its transfer SHA-256 is verified. Physical motion, coexistence,
repeated launch stability and streaming reentry remain pending. This synchronous
readback/copy experiment makes no speed or zero-copy claim and changes no normal
Moonlight rendering source.

## Existing flow and reuse

- `main.c` loads configuration and initializes the current CPU splash before the
  network/stream loop. Five themes already live in `ui_theme.c`.
- `stream.c` connects/pairs, enters `ui_menu_run`, and starts streaming after the
  menu exits. Input already has menu edges/repeat and a separate streaming poll.
- `ui_menu.c` combines state, navigation, drawing and artwork loading. It draws a
  1920x1080 BGRA staging surface with `ui_draw.c` and existing artwork caching in
  `ui_art.c`. It redraws dirty content; idle presentation is throttled.
- `video_ui_present` copies staging into an available display buffer and submits
  a flip through `renderer_videoout.c`. It already checks queue occupancy.
- `video_ui_end` deliberately leaves VideoOut open for streaming reuse. Existing
  comments warn that closing/unregistering with pending flips can hang the app.
- The H.264 decoders, audio and current presentation policy remain untouched.

The existing drawing/theme/artwork modules are the CPU fallback. Replacing them
or splitting all of `ui_menu.c` now would exceed Phase 1.

## API/dependency evidence

The installed OpenOrbis 0.5.4 SDK contains `orbis/Pigletv2VSH.h`, EGL/GLES2 headers,
`lib/libScePigletv2VSH.so` and `samples/piglet`. The header supplies the Piglet
configuration and native window ABI; the sample demonstrates an ES2 context.
This establishes a compilation path, not runtime compatibility on firmware 9.00.

Primary references:

- [OpenOrbis Piglet sample](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/samples/piglet/piglet/PigletApplication.cpp)
- [Piglet sample packaging/attributes](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/samples/piglet/Makefile)
- [Piglet ABI header](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Pigletv2VSH.h)

The SDK `.so` is an import stub. It does not supply the console's Piglet runtime
module. The prototype attempts the sample's sandbox `common/lib` path and records
load failures. No Sony runtime modules are downloaded or added to the package.
Their availability and the execution attributes must be verified on the console.
If module imports fail before `main`, the probe may not produce a log at all.

The upstream textured sample uses a precompiled-shader module. This first probe
uses scissored clears, not GLSL compilation or textures: those API paths and shader
compiler availability are NOT established by a successful clear/present test.

There is no demonstrated EGL import path for Moonlight's current CPU/GARLIC display
buffers. Piglet's compositor and the existing VideoOut handle must not be treated
as interchangeable. No shared-buffer or simultaneous-owner assumption is made.

## Minimal prototype and build

`src/experiments/gpu_probe.c` is an opt-in, standalone C11 executable excluded from
the default build. Current r11 creates three short 1280x720 EGL/ES2 contexts with
two-frame captures, followed by a fourth context with 600 live draw/readback/copy
iterations. After EGL teardown and module unload, it saves top-down BGRA for the
independent viewer and attempts 300 static native presentations. Earlier r5
exercised three context cycles (600, 120 and 120 frames).
Elapsed time includes swaps/readback; it is neither GPU timing nor a menu/streaming
performance result.

It tests the existing VideoOut renderer with EGL current in the fourth cycle,
then again after teardown/unload. This opt-in coexistence test is not enabled in
the main application. It never starts networking/decoding/audio. Package attributes follow the SDK
sample; current reservations retain the working video budget and reduce system
memory for hardware calibration. These are not an established production memory budget. Each
allocation/setup failure is a failed probe, not GPU fallback.

Build the normal project first using `build.bat`. Then, in the Linux build mirror:

```sh
cd ~/ps4dev/moonlight-client
bash scripts/build_gpu_probe.sh
bash scripts/build_gpu_viewer.sh
```

The probe is `build-ps4/gpu-probe/Moonlight-01.36.pkg`, titled **Moonlight GPU Probe**
with separate title ID **MLGP00001**. It does not replace **MLNT00001**. The streaming
build remains CPU UI. Install/use the standalone probe only with streaming closed.
Ensure `/data/moonlight` exists so `/data/moonlight/gpu-probe.log` can be written.
Close the probe with PS after completion; returning from `main` is unsafe on Orbis.

The viewer is `build-ps4/gpu-viewer/Moonlight-01.36.pkg`, titled **GPU Snapshot
Viewer**, with title ID **MLGV00001** and category `gd`. Close the producer fully
with Options → Close Application before opening the viewer. The viewer requires
the saved capture, rejects missing/truncated/oversized input, submits 300 flips
and retains the last display buffer until closure. Its logs are
`/data/moonlight/gpu-viewer.log` and `gpu-viewer-videoout.log`.

## Required physical validation and phase gate

1. Record firmware/model, SDK revision and availability of Piglet. Capture the
   first load/config/context error if initialization fails.
2. Direct Piglet presentation failed through r5 and is not isolated by r10's
   two-frame capture. Static copied output has passed both the separate viewer
   and r10's sequential same-process test. Continuous visible animation remains
   unvalidated. A readback assertion alone cannot prove TV output. Collect
   `gpu-probe.log` and only the current native log session.
3. Require three successful context/create/present/destroy cycles with no GL/EGL
   errors, followed by a successful module unload. Repeat separate app launches
   and watch for crashes, stalls and memory growth. Lifecycle logs alone do not
   prove leak freedom.
4. Close the probe, launch normal Moonlight, inspect all five themes, then launch,
   pause, resume and return to the CPU menu repeatedly with hardware decoding and
   audio. Check that normal VideoOut presentation still works after the probe.
5. Collect a comparable CPU-menu baseline (frame time, allocations and memory)
   before any future UI migration. No speed, latency or 60 FPS improvement is
   claimed by this change.

Even if these pass, simultaneous EGL/VideoOut ownership remains unverified. One
sequential handover passes in r10; repeated handover and streaming reentry remain
unchecked. Before Phase 2 integration, establish safe ownership/teardown or a documented buffer
import route without disrupting pending flips. If unavailable, keep the CPU
renderer. Next phases (optional backend, scene graph, animation, GPU texture/font
cache and library features) remain deferred until that gate is resolved.
