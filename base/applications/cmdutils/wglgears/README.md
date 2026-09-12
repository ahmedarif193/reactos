<!-- SPDX-License-Identifier: GPL-3.0-or-later
     SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com> -->

# Gears presentation profiling

Run `wglgears_runner --profile` for one bounded capture. It runs the usual
gears workload, closes its window after three complete five-second samples, and prints the report.
`glgears --profile` captures until the window is closed manually.
The runner stops its desktop capture after the child exits, including a crash
or forced termination, and reports any cleanup failure.

The report contains:

- Minimum, average, maximum, total time and call counts for app drawing,
  SwapBuffers, WGL flushing and pacing, shared-surface composition, Mesa
  buffer waits, DWM composition, KMT submission, kernel dispatch, retirement,
  and fence publication.
- App and DWM CPU time, frame interval percentiles, each second's slowest
  frame, and the 20 worst intervals split into drawing, swap and time outside
  the draw routine. Absolute QPC timestamps permit comparison with other logs.
- Domain availability, failures, unfinished operations and lost frame records.

All durations are QPC wall time. Nested stages overlap and must not be summed
as independent costs. Dispatch-to-retire includes time queued in the miniport
and delivery of completion; it is not a hardware GPU timestamp. App FPS counts
SwapBuffers returns, including immediate frames that DWM may discard. It does
not count frames displayed by the monitor. Kernel counters include other GPU
clients while the capture is active.
`buffer_fence_wait` includes both readiness polls and blocking waits; its
average covers all of those calls.

The shared profiling ABI is versioned. Deploy matching dwmcore.dll,
dwmprof.dll, cdd.dll, dxgkrnl.sys and the Mesa ICD. A mismatched or unavailable
domain makes the report incomplete rather than silently reporting zero cost.
For an incremental build, include the SDK runtime target `dwmprof` as well as
the command-line target `dwmprof_tool`:

```
ninja glgears wglgears_runner dwmprof dwmprof_tool dwmcore cdd dxgkrnl mesa_gallium
```

Normal runs do not start a capture or allocate its frame buffer. Inactive
kernel/ICD scopes only inspect the enable epoch. Active scopes read clocks and
update counters; they do not print, allocate or wait for a profiler lock. Only
the userspace start/stop controls wait for counter writers. The first 8192
frame records are retained; aggregate counters continue after that limit, and
record loss is explicit. Sorting and report output happen after capture stops.

Compare normal and profiled runs on the same board and workload to quantify
observer overhead. A profiled FPS result is not an unprofiled benchmark score.
