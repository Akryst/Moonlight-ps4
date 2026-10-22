# Video comparison: released 01.35 and working 01.36

Compared against tag `v1.3.5`, commit
`cb3276d1107e58581ba827ec065075faf2bba050`, on 2026-10-20.
The user confirms that only **Moonlight GPU Probe** has a black screen.

## Verified differences

All 24 tracked files under `src/video`, `src/ui`, `src/audio`, plus `src/stream.c`,
match the release byte-for-byte after normalizing line endings. There is no video,
decoder, audio, UI or stream-orchestration source change in the normal client.

The normal client changes controller shortcut handling and its displayed version.
CMake adds separate `EXCLUDE_FROM_ALL` GPU probe and snapshot viewer targets. Packaging supports
separate metadata; the main client retains `MLNT00001` / `gd` and the probe uses
`MLGP00001` / `gde` / `obs`.

| Application | Drawing | Presentation |
| --- | --- | --- |
| Released 01.35 | Existing CPU BGRA UI | Existing VideoOut buffers and explicit flips |
| Normal working 01.36 | Same UI and video sources | Same VideoOut path |
| GPU Probe r5 | Piglet ES2 surface, scissored clears | EGL compositor/swap path; physical output not demonstrated |
| GPU Probe r7 + separate viewer | GPU capture saved as BGRA; independent CPU viewer | Existing VideoOut path; static image visible on TV, confirmed by user |
| GPU Probe r10 | GPU capture, EGL teardown/unload, explicit BGRA copy in the same process | Existing VideoOut path; 300 submissions and static image visible on TV, confirmed by user |

The current main executable imports `libSceVideoOut` and `libSceGnmDriver`, and
does not import Piglet. At comparison time, probe r5 imported Piglet and did not call/link the
project's VideoOut renderer. EGL may manage its own native output internally;
absence of a direct VideoOut import alone is not proof of the failure's cause.

## What the black screen establishes

The probe's on-console log confirms EGL 1.4 / OpenGL ES 2.0 Piglet, successful
swap calls, red RGBA pixel readbacks, three context lifecycles and module unloads.
The user nevertheless reports no visible rectangle during the extended run.
This localizes the unresolved failure to visible presentation of the probe's EGL
surface. It does not establish a decoding failure or a regression from 01.35.
The precise internal compositor/window/scanout cause has not been identified.

The 840 successful swap calls are **not 840 verified displayed frames**. Readback
proves color in the GL framebuffer, not that the physical display scanned it out.

There is also a separate, deterministic behavior in the probe: after its finite
test cycles it destroys the surface/context, terminates EGL and waits forever.
A black screen after completion is therefore expected. This cannot explain the
user's report that no rectangle appears during the active drawing period.

## Copied-path diagnostic result

Revision r6 captured the complete GPU image and saved it after converting
RGBA/bottom-up to BGRA/top-down. After EGL teardown and module unload, the unchanged
native presenter failed at `AllocateDirectMemory` with `0x80020023`. The cause of
that same-process allocation failure remains unresolved.

Revision r7 keeps capture and viewing in separate applications. The independent
`gd` viewer (`MLGV00001`) does not import Piglet. After the producer was fully
closed, its log recorded successful native initialization and 300 submitted flips;
the user confirmed the blue background and static red rectangle on the TV.

This proves a GPU-generated image can reach the known VideoOut path through a
saved capture and a separate process. It does not fix direct Piglet scanout or
demonstrate real-time rendering, safe same-process ownership or a performance gain.
Revision r8 narrowed the native allocation failure to EGL initialization: 12 MiB
allocations passed before EGL but failed while current and after unload. The
producer reported a 768 MiB direct-memory limit with 250/512 MiB configured
system/video reservations. Revision r9's 2/36 MiB reservations could not create
the context. Revision r10 keeps 512 MiB video and reduces only system memory
against r8, to 64 MiB. All allocation/release checkpoints now pass; EGL capture,
teardown and unload succeed, followed by native initialization and 300 submitted
flips. The user confirms the blue background and red rectangle in the same Probe.

This resolves the allocation blocker for one sequential same-process copied
snapshot. Direct Piglet scanout, simultaneous ownership, continuous rendering and
repeat/stream-reentry stability remain unproven. The normal client's renderer
remains unchanged; GPU integration stays gated on those checks.

Revision r11 is a pending hardware diagnostic: three short EGL lifecycle cycles,
then 600 GPU draw/readback/CPU-copy/native-submit iterations with EGL current,
followed by the static post-unload presentation. It tests coexistence explicitly
without importing EGL pointers into native buffers. Build and focused host checks
pass; visible animation and streaming reentry are not yet established. This does
not change the released or normal working client's rendering implementation.
