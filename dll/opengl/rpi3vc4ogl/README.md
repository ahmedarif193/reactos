<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
-->

# Raspberry Pi 3 packaged VC4 rollback

The source build now lives in [../mesa_gallium](../mesa_gallium/README.md).
It packages one `mesa_gallium.dll` containing VC4, V3D and softpipe.
This directory retains the previous VC4 archive and external-ICD override.

## Selecting the packaged rollback

`rpi3vc4ogl.dll.tar.xz` is retained unchanged as the previous packaged ICD.
Select it with `-DMESA_GALLIUM_FROM_SOURCE=OFF`. An explicit
`RPI3VC4_MESA_VC4_ICD` path takes precedence over both source and archive
selection. The archive details and historical measurements below describe
that rollback binary, not the newly built Mesa DLL.

The rollback package was rebuilt on September 9 in Release mode (`-O3`, `NDEBUG`, no debug information), including the linked KMT and zlib libraries from `output-Clang-arm64-release`. Its DLL SHA256 is `68b6584ab2f03c39cff2c3c0ad9a0fffeb3fbd9f26215805adb554bc1418cebd`. Build provenance and the previous archive are saved in `validation/graphics-errors-20260909/`. This package also fixes WGL framebuffer lookup when a cached window DC is reassigned to a different window.

Earlier candidate039 added partial-primary GPU copies from037 plus VC4 GPU copying for WGL shared-window publication. Its unchanged benchmark suites completed, and run040 passed 73 final-primary pixel samples covering GDI and WGL movement/repaint/resize. Effects were off.
See `validation/rpi3-gpu-performance-20260908/GPU_WINDOW039.md` and the saved
`MESA037_SWAP_HINT.patch` / `MESA039_GPU_WINDOW_COPY.patch` increments.
The copy still waits for its completion fence; zero-copy, full effects and
Windows scheduler parity remain open. Older results below are historical.

## Rollback provenance

The previous artifact was vendored before the source build was integrated.
Its matching source and cross-build configuration were kept in
`/home/ahmed/WorkDir/TTE/mesa-reactos-rpi3`, based
on Mesa commit `e7f6c8ab7ed2ff61185caa3082c693284b0cbcaa`. The archive is
unpacked by `CMakeLists.txt` when the packaged rollback is selected.

## Packaged rollback contents

| | |
|---|---|
| Mesa version | 26.1.0-devel (base `e7f6c8ab7ed2ff61185caa3082c693284b0cbcaa`, ReactOS VC4 worktree) |
| Gallium driver | `vc4` |
| Target | ReactOS ARM64 (PE32+, machine `0xaa64`) |
| Unpacked MD5 | `059f7fa33f3ef4245ab05285431533b5` |
| Unpacked SHA-256 | `68b6584ab2f03c39cff2c3c0ad9a0fffeb3fbd9f26215805adb554bc1418cebd` |
| Reports | `GL_RENDERER = VC4 V3D 2.1`, `GL_VERSION = 2.1 Mesa 26.1.0-devel` |

Includes the full `vc4` compiler stack — `vc4_program.c` (NIR to QIR),
`vc4_qpu_emit.c`, `vc4_qpu_schedule.c`, `vc4_register_allocate.c`, the QIR
optimiser set — plus Mesa's `stw_*` WGL state tracker and all 20 `Drv*` ICD
entry points.

## How it reaches the GPU

    opengl32.dll         OpenGLDriverName=rpi3vc4ogl.dll, from the rpi3vc4 service key
      -> rpi3vc4ogl.dll  Mesa vc4 gallium + stw WGL state tracker
      -> rpi3vc4kmt_*    statically linked D3DKMT winsys (sdk/lib/rpi3vc4kmt)
      -> gdi32 D3DKMT*   Escape / CreateAllocation / Lock / Present / fences
      -> rpi3vc4.sys     RPI3VC4_ESCAPE_INFO, CL submit and validation
      -> VC4 V3D         bin/render pipelines

For DWM's authenticated full-screen output, the front-buffer callback imports
the WDDM primary and uses a VC4 tile-buffer copy (or render blit when needed)
before the ordered fence wait,
cache invalidation, and direct flip. This avoids Mesa's generic linear
`resource_copy_region` CPU copy. Other WGL windows retain their regular
front-buffer path.

The WGL presentation fix in [wgl-present-fix.patch](wgl-present-fix.patch)
is applied to this artifact. Callback update rectangles are window-relative,
matching OpenGL32's conversion to the client-sized DWM allocation. Passing a
zero-origin client rectangle instead made decorated windows publish negative
coordinates and fail with `STATUS_INVALID_PARAMETER`, leaving their content
black. The legacy `DrvPresentBuffers` structure also retains its 32-byte ARM64
ABI; the driver no longer reads nonexistent trailing version/event fields.
It uses the version-2 callback and completes its existing synchronous fence
wait before returning.

ARM64 Debug hardware verification captured visible colored gears (run64) and
the glmark2 jellyfish scene (run66). Run64 completed both apps with no shared
surface publication errors or GPU validator failures. Artifacts and the actual
callback/ABI regression check are in
`validation/dwm-gpu-present-20260907/`. Power interruptions during later
verification are separate from this display fix.

The packaged DLL subsequently removes the temporary `RPI3VC4_SUBMIT_PERF`
and `RPI3VC4_FRONTBUFFER_PERF` output and their timing/counter overhead.
Rendering, synchronization, error diagnostics and opt-in Mesa performance
queries are unchanged. This logging cleanup was rebuilt and checked in the
packaged binary; it does not add a new hardware performance measurement.

## Shared DWM surfaces (September 8 bring-up)

The packaged ICD now includes `wglBindSharedTextureROS` and
`wglUpdateSharedTextureROS`, matching `sdk/include/reactos/dwmgpuinterop.h`.
DWM imports shared window allocations instead of uploading CPU snapshots.
Publication IDs avoid repeated linear-to-tiled GPU conversions for unchanged
content. This still permits GPU conversions on content changes; it is not a
claim that every producer/presentation path is zero-copy.

The matching external source changes are preserved in
`validation/dwm-gpu-present-20260907/mesa-shared-import-current.patch`.
Run139 completed the loaded sequence at 720x480; run141's three primary-buffer
pixel samples matched. Full effects/visual parity and 60 FPS remain unverified.
The September 8 Debug image had retained the previous ICD and fell back to
software on run149. Run150 tests the restored pair at 1920x1080. Detailed GPU presentation success diagnostics are opt-in with `VC4_DEBUG=perf`;
formatting/output occurs after the device mutex is released. That September 8 ICD
linked ARM64 DEBUG KMT/zlib libraries while Mesa used an optimized build.
Run174 verified that earlier package: all ten direct-primary blur phases passed,
Taskmgr11 normal/maximized/drag tests completed, and repeated synthetic drag
measured about 9.4–9.7FPS. GPU shadows and finished-blur reuse were retained.
This does not establish 60FPS or fix ordinary GDI screen-shadow readback; see
`RPI3_DWM_DRAG_LATENCY_REPORT_2026-09-08.md` for measurements and remaining
copy/wait costs.

## ABI warning

The DLL statically links `sdk/lib/rpi3vc4kmt`, so it is pinned to that shim's
escape ABI (`ESCAPE_PACKET_MAGIC_V2` `0x32454756`, `RESOURCE_LIST_MAGIC_V3`
`0x3352474c`, `RPI3VC4_ESCAPE_INFO` 96 bytes, `RPI3VC4KMT_SUBMIT_CL` 160
bytes). That ABI was introduced by commit `0b59cb64953`.

**Changing `sdk/include/reactos/rpi3vc4kmt.h` or
`sdk/lib/rpi3vc4kmt/rpi3vc4kmt.c` requires rebuilding this binary.**

## Source rebuild / overriding

The current ARM64 source build is the shared native CMake Mesa project described
in `dll/opengl/mesa_gallium/README.md`. It builds VC4 and V3D together and creates
its matching Release KMT/zlib support tree automatically, even for a Debug
ReactOS image. To test a separately built result without replacing the archive:

    cmake -DRPI3VC4_MESA_VC4_ICD=/path/to/libgallium_wgl.dll .

To replace the vendored copy, pack the new DLL under its installed name:

    tar --sort=name --owner=0 --group=0 --numeric-owner \
        -cJf rpi3vc4ogl.dll.tar.xz rpi3vc4ogl.dll

The archive must contain exactly one member named `rpi3vc4ogl.dll`:
`reactos.cab` records the source basename and ignores `NAME_ON_CD`, so the
staged file has to already carry the installed name.

## On-demand presentation counters

The packaged ICD also implements `wglControlPresentationTraceROS`, using the
versioned private `dwmpresenttrace.h` / `dwmpresenttracecore.h` contract. Counters
are off by default. `dwmprof.dll` and `dwmprof.exe` capture DWM, this ICD, and
dxgkrnl together; no hot-path printing or GL context on the reader is needed.
Run179 verified all three domains and byte-identical repeated SDK snapshots;
run177 passed all ten direct-primary blur phases. See
`sdk/lib/dwmprof/README.md` and `validation/dwm-present-capture-20260908/`.

## September 8 partial-primary presentation (037)

The packaged ICD now implements `GL_WIN_swap_hint` and passes DWM repair
rectangles through a synchronous private callback snapshot to VC4 tile-rounded
primary GPU copies. Full initialization/recovery and completion waits remain.
SHA256: `855f911846431a9537cd725f573ed3bd56e45ca76ff87c80957d05309d4d45d5`.
Run037 completed unchanged benchmark suites;038 passed37 direct-primary pixel
checks for movement, repaint, resize and hide, with effects disabled.
See `validation/rpi3-gpu-performance-20260908/SWAP_HINT037.md` and the incremental
`MESA037_SWAP_HINT.patch` for the paired frontend/backend changes and limits.
This is not zero-copy or full effects/Windows parity certification.
