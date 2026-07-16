# ReactOS Performance Analyzer

ReactOS Performance Analyzer (`rosprofiler`) is a clean-room GUI profiler for
ReactOS with an offline, host-portable recording format.  The current source
provides an end-to-end workflow:

1. launch a program under the recorder or attach to a running process;
2. periodically capture bounded userspace stacks from its threads;
3. save normalized samples, module identity, lifecycle, and loss metadata to a
   bounded, versioned `.rperf` log;
4. reopen a complete or recoverable interrupted log; and
5. inspect hot functions, merged flame paths, sample timing, filters, and
   session diagnostics in one GUI.

Four recorder adapters are explicit in the GUI: intrusive userspace snapshots,
the ReactOS RosProf kernel device, documented Windows ETW sampled profiles,
and a deterministic synthetic test backend.  A requested backend is never
silently replaced by another one.  The source-level implementation is not yet
a production-readiness claim: broader native ReactOS SMP/failure testing,
Windows 11 ETW testing, security review, PMU support, and full kernel/user call
chains remain acceptance gates.

## Does it require kernel changes?

The intrusive fallback does not.  It uses existing process, thread, context,
symbol, timing, and file APIs and remains useful when no kernel profiler is
available.  It briefly suspends target threads and measures wall-clock stack
snapshots rather than on-CPU execution.

Low-intrusion ReactOS profiling does require kernel support.  This tree now
contains a versioned `\\.\RosProf` device contract, per-handle session
ownership, validated configure/start/stop/status/reset operations, bounded
per-CPU rings, batch and cancellable reads, sequence/loss accounting, timer
instruction-pointer sampling, process/thread/image lifecycle, scheduler
switch/wakeup records, clock synchronization, scope checks, and secure buffer
teardown.  Image records carry load generation, architecture, base/size,
timestamp/checksum, RSDS GUID/age when present, and a bounded UTF-8 path.

The current kernel sampler advertises only capabilities it implements.  In
particular, it currently emits one interrupted instruction-pointer frame for a
timer sample; PMU events and non-intrusive user/kernel call-chain unwinding are
not implemented.  The ABI and decoder build and have deterministic parser
coverage.  A native amd64 four-CPU RosProf launch/capture/save/reopen smoke has
passed, including per-CPU draining and embedded-rsym symbolization, but the
broader SMP, loss, failure, and security matrices remain before it can be
called production-ready.  ReactOS's existing flat `NtCreateProfile` bucket API
remains separate and also needs its own SMP audit.

On Windows 11, the comparable supported operating-system recording path is
ETW (normally orchestrated by tools such as WPR), not the ReactOS RosProf
device.  The production userspace boundary has separate recorder adapters:
the Windows adapter dynamically resolves the documented `StartTraceW`,
`TraceSetInformation`, `TraceQueryInformation`, `OpenTraceW`, `ProcessTrace`,
`ControlTraceW`, and `CloseTrace` APIs, while the ReactOS adapter uses the
public `sdk/include/reactos/rosprof.h` per-handle device contract.  It never
uses `NtTraceControl` or assumes that ETW entry points exist at link time.

ETW permission failures, unavailable sampled-profile support, unavailable
stack walking, and unsupported scopes or events are reported as capabilities
or precise start errors.  A requested ETW/on-CPU capture is never silently
replaced by intrusive thread suspension.  The user must explicitly choose a
different backend.

The GUI marks unavailable backends in place.  Non-intrusive captures run
through the pluggable recorder contract and are saved as binary v2 `.rperf`
recordings after capture stops.  The loader preserves normalized lifecycle,
scheduler, image, clock, security, and loss records; the current flame,
function, and sample-timeline widgets consume only the sample projection, so
richer scheduler/off-CPU presentation remains tracked in [TODO.md](TODO.md).

## Clean-room statement

The observable workflow and presentation requirements were derived only from
the published documentation and product pages listed under
[Public references](#public-references).  No implementation source from an
external profiler, including Linux `perf`, Brendan Gregg's FlameGraph tools,
or KDAB Hotspot, was inspected, copied, translated, or used as implementation
material.  The ReactOS implementation and `.rperf` format are independent
designs.  Existing ReactOS capture APIs and in-tree stack-walking examples were
inspected as the platform interfaces this tool must use or extend.

The references establish useful public behavior, not compatibility.
`rosprofiler` does not read or write `perf.data`.

## Capture workflow

### Launch or attach

For an existing process, refresh the process list, select the target, set the
snapshot interval and optional duration, then choose **Attach**.  For a new
process, choose **Launch**, provide the executable, arguments, and working
directory, and use the same capture settings.  Launch creates the process
suspended, starts the recorder, then resumes the initial thread.  It does not
yet guarantee a sample before the first user instruction or follow child
processes.

The output path is chosen before recording starts; there is no separate action
required to make a recording portable.  The intrusive compatibility recorder
streams its v1 text records as samples are accepted.  Pluggable RosProf, ETW,
and synthetic captures currently retain a bounded normalized recording and
atomically save binary v2 after stop; streaming those backends directly to the
codec is still open work.  A duration of zero records until **Stop**, target
exit, an error, or application shutdown.  Passing one `.rperf` path on the
command line opens that recording at startup.

### Userspace wall-clock snapshots

At each scheduled sweep, the recorder enumerates the target's current threads
and handles them one at a time:

1. open the thread with the required query, suspend, and context rights;
2. verify that the thread still belongs to the target process;
3. suspend it;
4. read its register context and walk a bounded userspace stack; and
5. resume it immediately.

Each successful walk becomes one timestamped process/thread stack sample.
Capture runs on a worker thread, and stop, target-exit, and duration checks are
performed during a sweep so a large thread set does not make cancellation wait
for the next complete sweep.

The worker registers the target's loaded PE modules explicitly and refreshes
that list during capture.  This keeps launch-mode symbolization and unwinding
useful when DLLs arrive after the process is resumed, without depending on the
currently incomplete native DbgHelp module-refresh path.  It is still a
best-effort userspace snapshot rather than an authoritative image lifecycle
trace.

This is **wall-clock all-thread snapshot sampling**, not on-CPU sampling.  A
waiting or sleeping thread can be sampled, different threads in one sweep are
captured at slightly different times, and a process with more threads can
contribute more samples per sweep.  The resulting percentages describe the
population of successfully captured stacks.  They do not measure CPU cycles,
invocation counts, scheduler residency, or blocked duration.

The mechanism is intrusive because it briefly suspends target threads and the
capture work itself changes timing.  Do not use it on safety-critical or
production workloads.

## `.rperf` recording and recovery

`.rperf` is the experimental native recording format for this tool.  The binary
v2 format is little-endian, pointer-free, checksummed, explicitly aligned, and
framed so unknown non-critical record kinds can be skipped.  It records the
producer architecture and address width rather than depending on the machine
that later opens it.  The legacy intrusive recorder also has a bounded v1 text
format, and both versions reopen through the same normalized model.

Besides capture configuration and timestamped samples, v2 stores immutable
module identity: architecture, load base and size, PE timestamp and checksum,
RSDS GUID and age, embedded-rossym presence, recorded path, and image
load/unload generation.  Raw program counters and resolved function addresses
are kept separately so multiple PCs in one function aggregate as one function
while the original captured address remains available for re-symbolization.

A normal recording ends with a footer containing elapsed time, process user
and kernel time, capture-gap counters, completion reason, and capture error.
The completion reason distinguishes duration, user stop, target exit, and
error.  If recording is interrupted before the footer, the reader can salvage
the complete sample records already written and marks the session explicitly
as incomplete.  It does not present that recovery as a cleanly completed log.

The reader validates framing, checksums, declared sizes and offsets, versions,
record order, numeric fields, sample depth, monotonic time, process identity,
symbol/address relationships, and finalization state before publishing a new
session.  Current defaults bound a file to 16 GiB, a recording to 4,000,000
records and 1,000,000 samples, symbols to 524,288, modules and threads to
65,536 each, strings and individual records to 1 MiB, and GUI call chains to
64 frames.  The legacy analysis model separately caps flame nodes at 2,000,000.
Indexes avoid quadratic symbol ingestion and wide-tree construction.  The
format remains under development: long-term compatibility and a fully fuzzed
untrusted-input contract are not yet frozen.

`.rperf` is independent of both Windows `.etl` files and the RosProf kernel
batch layout.  Recorder adapters validate and normalize their native records
into fixed-width process, thread, image, sample, scheduler, clock, security,
and loss records before the file codec sees them.  This keeps an ETW schema or
kernel ABI revision from becoming the offline-analysis file format.

The production userspace implementation is split into independently owned
modules: immutable recordings (`profiler_model`), versioned v1/v2 codecs
(`profiler_codec`), intrusive/fake/ETW/RosProf recorders
(`profiler_recorder*`), cancellable transactional jobs
(`profiler_jobs`/`profiler_controller`), symbol providers
(`profiler_symbolizer`), metric-aware top-down/bottom-up/caller-callee
analysis (`profiler_analysis`), bounded virtual view models
(`profiler_viewmodel`), deterministic exports/comparison
(`profiler_export`), and preflight/settings/accessibility support
(`profiler_product`).

Opening a log is an offline operation; the target process does not need to
remain alive.  Parsing, symbolization, conversion to the current presentation
model, and flame/function aggregation run as cancellable worker transactions.
The GUI keeps the previous profile intact until every stage succeeds, reports
progress continuously, and treats a late cancel as cancellation rather than
publishing a raced result.  Capture and analysis are separate operations in the
same observable style as the documented `perf record` and `perf report`
workflow.

## Offline symbols

Saving raw addresses together with module identity makes a recording useful on
another machine.  On open, the GUI builds a local-only search path from the log
directory, sibling `images` and `symbols` directories, and the local Windows,
System32, and drivers directories.  A recorded image path is tried only when it
exists locally.  Network shares, `srv*` symbol-server paths, and HTTP paths are
rejected unless a future explicit setting enables network access.

Each candidate PE is parsed with bounds checks and must match the recorded
architecture, timestamp, checksum, image size, and RSDS GUID/age wherever both
sides provide those fields.  The GUI then uses the public DbgHelp API to load
the image at an isolated synthetic base and resolve symbols and source lines.
The in-tree DbgHelp supports matching PDBs, embedded `.rossym`, DWARF,
COFF/CodeView, and exports.  Missing images, missing symbols, load errors, and
identity mismatches remain visible in the session status; affected addresses
fall back to `module+0xoffset` and are never assigned a symbol from a merely
same-named binary.

The fixed preinstalled debug image has a size budget rather than copying every
symbol file blindly.  For the 400 MiB image, the default policy limits packaged
PDB payloads to 160 MiB while reserving 32 MiB free space plus 4 MiB for FAT and
directory overhead.  It prioritizes `rosprofiler`, DbgHelp, the kernel, HAL,
and core runtime PDBs, accounts in 4 KiB allocation units, and writes the exact
selection and fallback reason to `reactos\symbols\rosprofiler-symbols.txt`.
PDB packaging is enabled by default only for Debug MSVC images.  Current Clang
and GCC amd64 debug builds instead embed lightweight `.rossym` data in their PE
images.  The manifest distinguishes counts of enumerated PDB candidates from
the build-wide embedded-rsym fallback policy.  If an MSVC PDB does not fit,
that module honestly falls back to module+offset; the build does not claim an
embedded rossym that it did not produce.

## Analysis views

Thread and inclusive time-range filters rebuild the shared aggregation used by
the hot-functions and flame-graph views.  The active sample count is the
denominator for displayed percentages.  Text search helps find symbols without
changing the sample population.  Filters can be entered directly or selected
by dragging a range in the timeline.

### Hot functions

The hot-functions table reports:

- **Self samples**: accepted samples whose leaf program counter resolves to the
  function.
- **Inclusive samples**: accepted samples in which the function appears
  anywhere in the call chain.  Recursion is counted once per sample for this
  global per-function value.

The table can be sorted by its displayed columns.  A function can have low
self cost and high inclusive cost when most sampled stacks are in descendants.
The values are statistical stack proportions, not exact durations.

### Flame graph

The flame graph merges equal caller-to-callee paths.  Rectangle width is the
number of filtered samples containing that **specific contextual path**;
vertical position is stack depth.  Horizontal placement groups stacks and is
not time.  Hovering a frame shows its symbol and sample population in the
status bar.  Click a frame to zoom into that subtree, right-click to go back,
or reset the zoom from the main window.  Search visually de-emphasizes
non-matching frames.

The contextual count is deliberately different from the hot-functions global
inclusive count.  A function reached through two callers appears in two flame
rectangles, while the hot-functions table aggregates those occurrences under
one function.

### Timeline and session summary

The timeline shows when each successfully captured thread stack was taken.
Dragging a time range applies that range to the analysis; double-clicking
resets it.  These are sample markers, not scheduler execution spans.  The view
does not show CPU assignment, wakeups, blocking reasons, or off-CPU intervals.

The session page exposes capture configuration, completion state, wall-clock
elapsed time, filtered and total sample counts, process CPU-time metadata, and
missed/truncated diagnostics.  A missed userspace thread snapshot is not the
same as a kernel ring-buffer loss record.

## Design boundary

The implementation keeps four responsibilities separate:

- **Capture** produces bounded, timestamped records for the selected scope.
- **Recording** serializes capture metadata and samples incrementally.
- **Analysis** validates and aggregates a recording without a live target.
- **Presentation** renders multiple views from one filtered analysis model.

That boundary allows the intrusive sampler to be replaced by a kernel-backed
source without rewriting the log analysis and GUI.

## Validation

The `rosprofiler_selftest` target exercises deterministic complete,
incomplete, and malformed logs, transactional load failure, canonical function
aggregation across multiple raw PCs, thread/time filtering, and flame-tree
counts.  Its scale fixture loads 16,384 descending symbol records and builds
16,384 distinct root paths, covering both hash indexes under adversarial input.
It also proves cancellable asynchronous open/presentation ownership, including
late-cancel and blocked-worker cases; checks RosProf image identity parsing;
decodes ETW Image_Load v1 and v2 in both 32- and 64-bit layouts plus malformed
fixtures; and resolves the running image through DbgHelp with identity checks
and embedded symbols.  Finally it spawns a synchronized recursive workload,
records live stacks, checks the completed footer and in-memory analysis,
reopens the emitted log, and compares the reloaded analysis.

The `rosprofiler_guitest` target verifies the application icon and drives the
actual application through launch, save, streamed capture, manual stop,
analysis, reopen from a distinct source path, timeline selection, thread and
search filters, reset, and every tab.  It
waits on observable GUI state rather than fixed completion sleeps and verifies
that capture/open/symbolization/view construction form one continuous busy
transaction whose controls cannot access changing session data.  The symbol
test must use the matching in-tree DbgHelp rather than Wine's built-in DbgHelp,
which does not understand ReactOS `.rossym`.  For example:

```text
ninja -C <build-dir> rosprofiler_selftest rosprofiler_guitest
mkdir -p <test-dir>
cp <build-dir>/base/applications/rosprofiler/rosprofiler*.exe <test-dir>/
cp <build-dir>/dll/win32/dbghelp/dbghelp.dll <test-dir>/
WINEDLLOVERRIDES=dbghelp=n wine <test-dir>/rosprofiler_selftest.exe
WINEDLLOVERRIDES=dbghelp=n wine <test-dir>/rosprofiler_guitest.exe
```

The self-test also has host-log diagnostics used by the native acceptance path:
`--inspect-log <log>`, `--inspect-log-modules <log>`, and
`--analyze-log <log> <image-path> [symbol-path]`.  The last form applies the
same identity-checked DbgHelp provider and analysis model as the GUI.

GCC and Clang amd64 runtime tests have passed under Wine, the application also
builds for Clang ARM64, and a native four-CPU Clang amd64 image completed a
five-second RosProf capture of `cpubench.exe`.  That native log reopened with
231 of 231 samples and embedded-rsym symbols, then decoded and symbolized from
the host after extraction.  This still does not replace the failure,
performance, security, and architecture matrices in [TODO.md](TODO.md).

## Current limitations

- **Backend-dependent accuracy.** The compatibility recorder suspends every
  observed thread, perturbs the workload, and measures wall-clock userspace
  stacks rather than CPU execution.  RosProf and ETW are on-CPU adapters, but
  RosProf currently emits one interrupted instruction pointer per timer sample
  and neither native backend has completed its platform acceptance matrix.
- **Unwind limits.** Optimized code, omitted or corrupted frame information,
  architecture transitions, and unusual stacks can truncate a call chain.
- **Statistical gaps.** Short or rare paths may not be sampled, fixed intervals
  can alias with periodic work, and failed snapshots bias the population.
- **Scheduler UI is incomplete.** The v2 model and RosProf/ETW adapters preserve
  lifecycle, scheduler, clock, security, and loss records, but the current GUI
  projects samples into its timeline and does not yet render scheduler lanes,
  wait reasons, or off-CPU intervals.
- **No PMU events.** Cycles, instructions, cache/branch events, multiplexing,
  and scaled hardware counts are not collected.
- **Lifecycle quality depends on the backend.** ETW and RosProf normalize image
  and thread lifecycle records into v2; intrusive capture has only best-effort
  module snapshots and can race with load/unload or thread churn.
- **Symbol presentation is not complete.** Local PDB, embedded rossym, DWARF,
  COFF, export, source-file, and line resolution exist, with module+offset
  fallback.  Inline-frame presentation, an interactive source view,
  disassembly, path remapping, and opt-in symbol-server retrieval remain.
- **GUI capture is process-scoped.** The recorder contract and RosProf ABI model
  system scope, but the current GUI configures one process.  Child following,
  system-wide GUI workflows, and remote capture are not implemented or native
  acceptance-tested.
- **Process-rights boundary.** Protected, system, cross-bitness, or otherwise
  inaccessible targets may be rejected.  The GUI does not silently elevate.
- **Experimental file format.** Treat `.rperf` files as untrusted and do not
  rely on compatibility between development versions.

## Why these public behaviors were selected

The documented Linux workflow separates recording from later reporting and
offers a raw trace-oriented view.  That supports an independent recording
between capture and analysis.  Linux's published perf ring-buffer contract
also makes producer/consumer synchronization and lost-record reporting part of
a serious capture path.  Those are future kernel requirements, not features
attributed to the userspace recorder.

Published flame-graph documentation defines the key visualization semantics:
width represents sample population, height represents stack depth, and the
horizontal axis is not time.  KDAB's published Hotspot pages demonstrate the
usability of combining launch/attach, offline reports, filtering, tables,
flame graphs, and timelines in one GUI.  This implementation follows those
observable ideas with native ReactOS mechanisms and an independent design.

## Public references

Only the following public documentation and descriptive pages were used to
derive comparison requirements (accessed 2026-07-15):

- Linux's published [`perf-record` documentation](https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/perf-record.txt)
  describes recording profiles, process selection, call-graph capture, event
  selection, and output files.
- Linux's published [`perf-report` documentation](https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/perf-report.txt)
  describes offline aggregation, symbols, call graphs, sorting, and filters.
- Linux's published [`perf-script` documentation](https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/perf-script.txt)
  describes reading recorded samples as a trace-oriented stream.
- Microsoft's documented [Image_Load v2](https://learn.microsoft.com/en-us/windows/win32/etw/image-load)
  and [Image_Load v1](https://learn.microsoft.com/en-us/windows/win32/etw/image-v1-load)
  layouts define the versioned ETW image metadata decoded by the Windows
  adapter.
- [Linux perf ring-buffer documentation](https://docs.kernel.org/userspace-api/perf_ring_buffer.html)
  describes kernel-to-userspace transport, head/tail synchronization, and
  lost-record reporting.
- [Linux perf security documentation](https://docs.kernel.org/admin-guide/perf-security.html)
  describes profiling data sensitivity and least-privilege controls.
- [Brendan Gregg's Flame Graphs](https://www.brendangregg.com/flamegraphs.html)
  and [CPU Flame Graphs](https://www.brendangregg.com/FlameGraphs/cpuflamegraphs.html)
  document stack-population visualization and interactive use.
- [KDAB's Hotspot overview](https://www.kdab.com/software-technologies/developer-tools/hotspot/)
  and [recording/timeline announcement](https://www.kdab.com/hotspot-v1-1-0-adds-timeline-recording-features/)
  describe a GUI for recording or opening profiles and presenting tables,
  flame graphs, filters, and timelines.
