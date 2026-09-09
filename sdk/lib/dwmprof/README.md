<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
-->

# DWM presentation profiling SDK

`dwmprof.dll` is a userspace shared SDK library. It reads and controls versioned
counters in DWM, the VC4 ICD loaded by DWM, and dxgkrnl. The console consumer is
`dwmprof.exe`. Neither component requires a GL context, debugger attachment, or
HDMI capture. The profiler is disabled at boot.

Build from `output-Clang-arm64-debug`:

```
ninja dwmprof dwmprof_tool dwm dwmcore dxgkrnl cdd
```

The Mesa ICD must also be built against the matching private headers for its
counter bank to be available. A missing ICD is reported in the snapshot; the
profiler does not activate GPU rendering or change fallback policy.

## Commands

```
dwmprof capture 10000          # record ten seconds, stop, then print once
dwmprof dump                   # read the last completed snapshot; no new capture
dwmprof dump C:\capture.txt    # save the last snapshot as text
dwmprof raw C:\capture.bin     # save the exact versioned SDK structure
dwmprof start                  # returns a session number
dwmprof stop SESSION           # stop that session and print its snapshot
```

Move windows or run a workload during the capture interval. `capture` performs
no polling or printing while recording. Output goes to stdout or the requested
file. Add `--serial` explicitly to mirror the finished text dump to the debugger.
`selftest 10000` checks rejection of competing starts/wrong-session stops and
byte-identical retrieval of the previous snapshot while a new capture records.

Exit codes: 0 complete data, 1 API/validation/I/O error, 2 one or more domains
unavailable. Inspect per-domain status even after a successful SDK call.

## SDK use

Include `reactos/dwmprof.h` and link the generated `libdwmprof.a` import library.
The runtime dependency is `dwmprof.dll`, not the graphics ICD or `dwmcore.dll`.

```c
DPT_SNAPSHOT snapshot;
HRESULT hr = DwmProfileCapture(10000, &snapshot, sizeof(snapshot));
/* Or read without starting any work: */
hr = DwmProfileReadLastCapture(&snapshot, sizeof(snapshot));
```

Exports: `DwmProfileGetSnapshotSize`, `DwmProfileStartCapture`,
`DwmProfileStopCapture`, `DwmProfileReadLastCapture`, `DwmProfileCapture`, and
`DwmPresentationTraceControl` (versioned lower-level request API).

The last completed snapshot stays in DWM memory until another capture stops or
DWM exits. A new START does not erase it. READ-LAST sends one copied request and
receives the cached snapshot; it does not call the ICD or kernel. STOP is
idempotent for the current session. An abandoned explicit START can be stopped
using the returned session number; the bounded `capture` command is preferable
for unattended measurements.

The transport uses DWM's host notification thread, independent of its rendering
thread. Buffers contain fixed-width values, explicit size/version fields and no
pointers. Version 1 has a 16-byte request, 48-byte counter, 1624-byte domain and
4920-byte combined snapshot, identically packed on ARM64, AMD64 and i686.

## Meaning and overhead

Each timed scope records entered/completed/failed counts, total and maximum QPC
ticks, and bytes where meaningful. `Entered - Completed` reports unfinished
operations at the capture boundary. Entries begun before START are excluded;
late completions after STOP cannot modify that snapshot or the next capture.
Failures describe returned failure status; void calls have no failure status.

The dirty GPU frame spans metadata fetch through surface acknowledgement after
SwapBuffers. Other stages cover scene preparation, window/texture rendering,
blur/shadows, scanout blit command issue, flush, device mutex wait, fence wait,
cache invalidation, KMT admission, miniport Render/Submit, queue-to-dispatch,
dispatch-to-retirement and fence publication. Blur hits/filter requests and BO
creation also have clock-free event/byte counters. Sampler shadow updates count
the work requested from the blitter; that metric alone does not identify CPU
versus GPU execution.

DWM/ICD counters cover the DWM process. **Kernel counters are system-wide across
adapters/processes**, explicitly identified by PID 0; concurrent applications can
contribute. They must not be assumed to correspond one-for-one with DWM frames.
Each domain records its own start/stop QPC boundaries and frequency. Boundaries
are sequential, not a simultaneous cross-domain hardware latch. Process CPU
kernel/user time is sampled only at DWM capture boundaries.

Times are inclusive elapsed wall time. Nested stages overlap and must not be
summed as independent costs. `dispatch_to_retire` includes completion handling;
it is not a GPU execution timestamp. This does not measure mouse-input latency,
scanout/vblank delivery or photons on the display. Byte counters describe
requested work, not measured memory-bus bandwidth. CPU texture upload counts
actual `glTexSubImage2D` calls; GPU blit bytes describe the requested pixel extent.

When disabled, a scope begins with one acquire load and performs no clock call,
allocation, syscall, output or counter update. Active scopes use two clock reads
and atomic count/sum/max updates; event counters use no clocks. No new graphics
locks, polling IOCTLs or per-event buffers are introduced. STOP disables entry and
waits only for short counter updates to quiesce, never for the measured GPU job.
An epoch protects reset/restart from old completions. This is low overhead, not
zero overhead; host timing tests are not Pi performance measurements.
