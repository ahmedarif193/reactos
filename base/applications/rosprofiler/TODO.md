# ReactOS Performance Analyzer TODO

This roadmap keeps implemented source, deterministic host/Wine proof, and
native platform acceptance separate.  The tree now contains userspace,
recording, DbgHelp symbolization, ETW, and RosProf kernel-source paths, and one
native amd64 four-CPU RosProf capture has passed.  The unchecked broader SMP,
security, PMU, unwind, failure, and performance gates remain required before
the profiler is a production default.

Checked MVP entries describe functionality present in the current source.
They do not claim production readiness, completed acceptance testing, or Linux
`perf` feature parity.  Validation work has separate checkboxes so a feature is
not confused with proof that it is safe under every failure mode.

## P0: userspace MVP

- [x] Provide one native GUI for capture and offline analysis.
- [x] Enumerate running processes and attach to an existing target process.
- [x] Launch an executable with arguments and working directory, start the
  recorder while its initial thread is suspended, then resume it exactly once.
- [x] Periodically enumerate target threads at a configurable interval.
- [x] Suspend one thread at a time, capture its userspace context and bounded
  frame chain, and resume it promptly.
- [x] Run capture on a worker thread and check manual stop, target exit, and
  bounded duration during each thread sweep.
- [x] Stop a capture without terminating an attached target process.
- [x] Write captured stacks to an independently designed, versioned `.rperf`
  file as capture progresses.
- [x] Finish normal logs with elapsed-time, diagnostic, completion-reason, and
  error metadata.
- [x] Open a complete log without a live target and salvage complete records
  from a footerless interrupted log while marking the session incomplete.
- [x] Keep raw program counters separate from canonical function addresses in
  the recording and aggregation model.
- [x] Timestamp samples from a performance counter and store elapsed
  microseconds; identify the mode as intrusive all-thread wall-clock snapshots.
- [x] Reject a detected cross-bitness target instead of attempting an invalid
  stack walk.
- [x] Aggregate per-function self and inclusive sample counts.
- [x] Display hot functions in a sortable GUI table.
- [x] Render merged stacks as an interactive flame graph with inspection,
  search highlighting, subtree zoom, and reset navigation.
- [x] Filter hot functions and flame paths by thread and inclusive time range,
  using the filtered sample count as the percentage denominator.
- [x] Display per-thread sample markers in a timeline and use a dragged time
  selection as the analysis range.
- [x] Display capture settings, completion state, sample totals, process times,
  and available gap/truncation counters in a session summary.
- [x] Register loaded modules independently of DbgHelp invasion and refresh
  them during capture so DLLs loaded after launch/resume can be unwound and
  symbolized on a best-effort basis.

## P0: harden the MVP before wider use

- [x] Build the GUI, self-test, GUI test, and kernel with GCC and Clang amd64;
  also compile the application for Clang ARM64.
- [ ] Extend compiler coverage to every supported configuration, including
  remaining x86 and ARM64 compiler combinations, and fix warnings without
  unrelated cleanup.
- [x] Run a native ReactOS amd64 four-CPU launch smoke: choose the output,
  capture `cpubench.exe` through RosProf, complete, reopen the saved log, and
  verify nonempty symbolized flame/function data.
- [ ] Extend the native GUI matrix to attach, manual stop, close/relaunch,
  incomplete recovery, failure injection, and automated comparison of every
  view.
- [x] Guarantee balanced suspend/resume handling on every success, error,
  cancellation, process-exit, and application-shutdown path: the capture has a
  single suspend site with one unconditional resume before any return, stop
  and exit checks run between threads, shutdown joins the worker, and the
  invariant is documented at the suspend site.
- [x] Move file parsing, symbolization, presentation conversion, and aggregation
  away from the UI thread with progress, bounded cancellation, transactional
  publication, and late-cancel ownership tests.
- [x] Count attempted, successful, failed, truncated, and skipped thread
  samples by reason and show them in the session summary.
- [x] Detect target exit and thread churn without stale handles or misleading
  error storms: every sweep re-enumerates threads (no handles are cached
  across sweeps), a thread that vanished between snapshot and open or whose
  id was reused counts as a skipped churn sample instead of a failure, and
  target exit is checked before each thread through the process handle.
- [ ] Test performance-counter monotonicity and conversion overflow across long
  captures and every supported platform clock implementation.
- [x] Define maximum thread count, frame depth, string length, record/sample/
  symbol/module count, file size, per-record size, and allocation limits.
- [x] Enforce current development ceilings for frames, samples, symbols, and
  flame nodes before allocating or traversing them.
- [ ] Validate every `.rperf` offset, length, count, version, and arithmetic
  operation before allocation or traversal.
- [x] Specify byte order, producer address width, pointer-free packed records,
  alignment, unknown-record skipping, and incompatible-version behavior for
  binary v2.
- [ ] Freeze and version the footer/completion contract after testing clean
  completion, every error path, abrupt process loss, and partial final writes.
- [x] Capture immutable module identity, image size/load ranges, architecture,
  PE timestamp/checksum, RSDS GUID/age, embedded-rossym state, and load/unload
  generation so raw addresses can be re-symbolized after capture.
- [ ] Handle unresolved, recursive, repeated, and truncated frames consistently
  in both hot-functions and flame-graph aggregation.
- [ ] Define and test native, WOW64, and cross-bitness behavior on every host
  and target architecture; the rejection dialog now names both the profiler
  and target bitness, but the per-architecture test matrix remains.
- [ ] Measure profiler CPU cost, target pause time, file growth, and sample-rate
  error across idle, CPU-bound, multithreaded, and thread-churn workloads.
- [x] Mark the userspace-suspension mode as intrusive in the GUI and save its
  `wall-clock-all-threads` identity in the log configuration.
- [x] Tag each intrusive sample with the sampled thread's scheduler state
  (running versus waiting, including the wait reason) taken from the same
  enumeration snapshot, and persist that state in the log so blocked time is
  distinguishable from execution time.
- [x] Provide an on-CPU-only analysis mode that excludes waiting-thread samples
  from hot functions, the flame graph, and filtered denominators, keeping
  wall-clock and CPU-time percentages explicitly separate; untagged legacy
  samples stay included in both views.
- [x] Disable and gate every analysis view while the worker owns mutable
  session data, then restore it only after the worker is joined.

## P1: production GUI and operations

- [x] Parse, symbolize, convert, and aggregate recordings on cancellable workers
  with progress, bounded memory, continuous busy-state isolation, and
  transactional preservation of the previously displayed profile.
- [ ] Virtualize large tables and timelines; set measured budgets for open
  time, filter latency, repaint latency, and peak memory.
- [ ] Before capture, show the stable target identity, scope, intrusive/source
  mode, output path, estimated data rate, free-space check, and stop conditions.
- [ ] Keep completion, incomplete-recovery, capture errors, missed snapshots,
  truncated stacks, and future kernel losses visible in every relevant view;
  never hide them only on the session page.
- [ ] Add filter/zoom history, flame breadcrumbs, forward/back navigation, and a
  one-action return to the unfiltered session without losing the recording.
- [ ] Make launch, attach, stop, open, tabs, filters, timeline selection, and
  flame navigation fully keyboard accessible with documented shortcuts.
- [ ] Test high DPI, font scaling, high-contrast themes, color-vision-safe
  palettes, screen-reader names, focus order, localization, and long Unicode
  target/output paths.
- [ ] Persist recent files and reusable capture settings without storing
  command-line secrets, environment values, or inaccessible target metadata by
  default; provide an explicit history-clear action.
- [ ] Add crash-safe session finalization and recovery diagnostics that identify
  the last validated record and never overwrite the original interrupted log.
- [ ] Provide deterministic export of the active filtered population with its
  denominator, capture mode, completion state, and loss diagnostics so a shared
  report cannot be mistaken for the full session.

## P1: Windows ETW compatibility backend

- [ ] Validate the dynamically resolved, documented ETW backend on supported
  Windows 11 editions without linking to ETW entry points or using
  `NtTraceControl`.
- [ ] Exercise sampled-profile interval query/set, process/thread/image kernel
  events, event-record stack extensions, target filtering, clean stop, target
  exit, buffer loss, and permission denial.
- [x] Report each unavailable ETW capability explicitly and require an
  intentional user choice before changing to another recorder backend.
- [x] Keep `.etl`, RosProf batches, and `.rperf` as distinct formats connected
  only through validated normalized records; do not advertise `.etl` import
  until a separately reviewed reader exists.

## P1: pluggable recorder integration

- [x] Compile the modular production sources (model, codec, recorders, jobs,
  controller, symbolizer, analysis, view models, exports, product helpers)
  into `rosprofiler_core` and link it into the GUI and the self test.
- [x] Select the capture backend explicitly in the GUI (intrusive userspace,
  RosProf kernel sampling, documented ETW, synthetic test recorder), mark
  unavailable backends in the selector, and fail the start with the backend's
  own capability description instead of silently substituting another mode.
- [x] Route non-intrusive captures through the pluggable recorder contract
  (query/validate/create/start/stop/join/take) on a monitor thread, save the
  taken recording as a binary v2 log, and reuse the existing loader, filters, and
  analysis views on the result.
- [x] Prove the recorder contract end to end in the self test: fake-backend
  capture, v2 codec save/reopen, presentation bridge, and aggregation assertions, built
  and run with GCC and Clang amd64 binaries under Wine.
- [x] Boot-validate the RosProf kernel backend on ReactOS: capability query,
  session configure/start/stop, per-CPU ring drain, loss accounting, and the
  saved log's analysis views against a known workload.
- [x] Stream recorder-backend captures to disk during capture instead of
  converting after stop: a record sink on the recording feeds an incremental
  v2 chunk stream (event chunks live; string/session/source/module/symbol
  tables and image events at finalization, which the type-pass loader
  permits), with a whole-file save fallback when streaming fails; kernel loss
  records reach the session summary through the recording counters carried by
  the GUI bridge.
- [x] Carry the true backend and metric identity through v1 and v2; the session
  summary and About text describe each backend's actual semantics instead of
  labeling every recording as thread suspension.
- [x] Reopen v2 directly and retain normalized scheduler/lifecycle/image/clock/
  security/loss records in the controller-owned recording while projecting
  samples into the current legacy widgets.

## P1: repair and reuse the native profile path first

ReactOS already has profile objects and the `NtCreateProfile`,
`NtStartProfile`, `NtStopProfile`, `NtQueryIntervalProfile`, and
`NtSetIntervalProfile` APIs in `ntoskrnl/ex/profile.c` and
`ntoskrnl/ke/profobj.c`.  That path produces flat address buckets and cannot by
itself supply call chains or lifecycle records, but it is the existing kernel
sampling boundary.  Do not add a parallel kernel mechanism until its contract
and SMP behavior are understood and repaired.

- [x] Move the HAL profile timer arm/disarm out of the KiProfileLock region:
  the all-processor rendezvous must never wait behind PROFILE_LEVEL spinners
  (amd64 PROFILE_LEVEL is above IPI_LEVEL), and boot-phase callers at raised
  IRQL program only the local APIC.  Physical source transitions serialize on
  a passive-level mutex; KeStartProfile/KeStopProfile now require APC_LEVEL
  or below.  Needs an SMP boot test with repeated start/stop under sampling.
- [x] Defer the final trace-session rundown-release signaling off high-IRQL
  producers: producers above DISPATCH_LEVEL queue their rundown release to a
  per-session DPC (pending-release counter drained at DISPATCH_LEVEL), so the
  stop path's rundown wait is never signaled from PROFILE_LEVEL or
  SYNCH_LEVEL.
- [ ] Align the i386 APIC profile vector (delivered at HIGH_LEVEL) with
  KiProfileIrql (PROFILE_LEVEL, 27) before enabling the sampler on x86:
  every raise-to-KiProfileIrql lock site can self-deadlock against its own
  profile interrupt there.
- [ ] Implement the i386 SMP KeIpiGenericCall completion wait (asserts
  not-implemented today) before the profile rendezvous is used on x86 SMP.
- [x] Make HalIsProfiling and HalCurProfileInterval interlocked/volatile and
  close the AP-startup races: the armed flag is published with interlocked
  stores before the start broadcast and cleared before the stop broadcast, and
  the 64-bit interval is read through an interlocked compare so a starting
  processor arms consistently from the flag on 32-bit builds too.
- [ ] Extend the existing single-process timer-source sampling smoke test to
  cover exact bucket placement, interval behavior, range boundaries,
  start/stop, process scope, privilege checks, and repeated sessions.
- [ ] Run those tests on SMP and audit processor affinity enforcement,
  per-processor source programming, shared-source ownership, concurrent
  sessions, and stopping one session while another uses the same source.
- [x] Make simultaneous bucket updates safe on SMP: bucket increments are
  interlocked in the profile-interrupt list walk; the native SMP tear/loss
  proof stays under the kernel test gates.
- [ ] Audit inclusive/exclusive range handling, bucket-shift arithmetic, buffer
  probing/locking, overflow, and teardown so malformed configurations cannot
  address outside the caller's bucket buffer.
- [ ] Verify and repair the timer/profile interrupt path on every supported
  architecture before using native profile objects as the GUI's flat on-CPU
  sampling fallback.
- [ ] Prove interrupt-side profile-list lifetime and synchronization while a
  concurrent stop removes a profile and releases its source state; treat the
  current unlocked traversal as a source-level hazard until runtime-tested.
- [ ] Exercise the system-wide/null-process contract from create through start;
  verify the start path never dereferences a missing process object.
- [ ] Fault-inject profile MDL allocation, user-buffer mapping, and
  `KeStartProfile` failures and verify status propagation plus complete unwind
  of locks, mappings, references, and source state.
- [ ] Decide, from tested requirements, whether stack samples and event records
  can extend the profile-object family compatibly or require a distinct trace
  session.  Do not overload flat buckets with an incompatible record stream.

## P1: kernel trace-session contract

Capabilities beyond repaired flat profiling use the versioned public
`rosprof.h` ABI and the source implementation in `ntoskrnl/wmi/rosprofdrv.c`.
The checked items below describe that source contract, not native runtime
acceptance.  Keep policy, transport, records, and analysis data distinct so the
ABI can evolve without exposing internal kernel structures.

### Session ownership and configuration

- [x] Define capability/configure/start/stop/status/reset/batch-read operations
  on an opaque per-handle trace session.
- [x] Bind every session to an authenticated owner and immutable capture scope:
  the device handle captures the opener's process and token-user SID, every
  control and read operation verifies both (impersonation of a different
  principal is denied), a configured session rejects reconfiguration, and a
  reset-then-reconfigure re-runs the full privilege checks before a new
  session with a new identity is created.
- [x] Define event selection, sample frequency or period, maximum call-chain
  depth, user/kernel inclusion, target filters, per-CPU buffer size, and clock
  choice.
- [x] Validate the whole configuration before starting; return precise status
  codes for unsupported events, scopes, architectures, or privileges.
- [ ] Specify behavior for concurrent sessions, CPU hotplug, processor groups,
  target exit, owner exit, cancellation, and shutdown.
- [x] Make stop idempotent and define the point after which no new records can
  be produced and all existing records can be drained.

### Non-intrusive user and kernel call chains

- [x] Sample one interrupted instruction pointer from the timer path without
  suspending target threads from userspace.
- [x] Capture bounded kernel-mode call chains at the amd64 profile interrupt
  through a lock-free seqlock-published image/.pdata table with a
  per-processor reentrancy guard, append the frames to variable-size ring
  records, and surface truncated/stopped/user-boundary state through the
  public sample record and the GUI recorder.
- [ ] Prove the interrupt-time unwinder on native SMP: image-unload grace
  period, IST/DPC stack windows, paged .pdata/.xdata access guards, and chain
  accuracy against known workloads; the unload race documented in profobj.c
  stays open until then.
- [x] Capture the interrupted process/thread identity, CPU, timestamp, event,
  instruction pointer, and event weight/period.
- [x] Define explicit user-to-kernel context markers so a mixed call chain is
  unambiguous: samples carry separate user/kernel depths, frames are leaf
  first user-then-kernel, and a user-boundary flag marks chains that ended at
  the mode transition.
- [x] Bound the implemented one-IP interrupt path: no blocking, pageable access,
  ordinary allocation, or unbounded traversal.
- [ ] Define safe architecture-specific kernel unwinders and the conditions
  under which a user context can be copied for deferred unwinding.
- [ ] Mark truncated, unavailable, corrupt, cross-bitness, and unwind-failed
  call chains instead of silently treating them as complete.
- [ ] Decide whether user stacks are walked in kernel context or captured as a
  bounded snapshot for a trusted userspace unwinder; document security and
  overhead tradeoffs before implementation.

### Stable record stream

- [x] Give every record a fixed common prefix containing at least record size,
  type, ABI version, flags, timestamp, CPU, process ID, thread ID, and sequence
  number.
- [x] Define alignment, byte order, integer widths, timestamp domain, maximum
  record size, and rules for skipping unknown record types.
- [x] Define `SESSION_INFO` and `SESSION_END` records with capture settings,
  clock information, architecture, completion state, and aggregate loss.
- [x] Define `SAMPLE` records for user, kernel, and mixed call chains, including
  event identity, weight/period, frame count, and truncation state.
- [x] Define process start/exit and thread start/exit records with stable IDs
  that disambiguate numeric ID reuse.
- [x] Define image load/unload records with path, base, size, image identity,
  checksum/build identity, and address-space ownership.
- [x] Define name/command-line metadata records without making later samples
  depend on mutable names.
- [x] Keep symbols, source text, and disassembly out of the kernel ABI;
  userspace should symbolize immutable addresses and image identities.

### Buffer transport and loss accounting

- [x] Use bounded per-CPU producer buffers so sampling does not serialize all
  CPUs on a global lock.
- [x] Define kernel producer/userspace consumer head and tail ownership,
  wraparound, alignment, wakeup thresholds, and required memory barriers: the
  contract is documented at the ring definition (producer-owned Head,
  consumer-owned Tail, monotonic byte counters masked by the power-of-two
  size, whole-record reserve/copy under the per-ring lock whose barriers
  order data against counters) and the read watermark defines the wakeup
  threshold.
- [x] Use a bounded batched read interface with a cancellable wait that cannot
  miss session stop or data-ready notification.
- [x] Emit explicit loss records with first/last sequence, count, CPU, time
  range, and reason, including buffer full, allocation failure, recursion,
  disabled event, and unsafe unwind.
- [x] Distinguish samples deliberately filtered out from samples unexpectedly
  lost: configuration-driven skips (owner exclusion, affinity, source
  mismatch) count into a per-session FilteredSamples statistic surfaced
  through the session status, separate from ring-full loss records.
- [x] Preserve loss totals even when the buffer has no room for an immediate
  loss record; publish final per-CPU counters when draining the session.
- [ ] Test slow consumers, buffer wrap, simultaneous CPU writers, abrupt reader
  exit, stop during overflow, and sequence gaps.

### Security and privilege

- [ ] Write a threat model covering exposed instruction pointers, kernel
  addresses, registers, process/image paths, names, command lines, and any user
  stack bytes.
- [ ] Permit same-owner, same-process-scope profiling only where ReactOS object
  security can enforce it; require an explicit least-privilege right for other
  processes, kernel call chains, and system-wide sessions.
- [ ] Define a dedicated profiling access check instead of treating broad
  administrative privilege as the normal GUI execution mode.
- [x] Perform access checks when the session is created and when its scope is
  changed; a duplicated or inherited handle cannot widen scope because every
  operation re-verifies the opener's process and token identity.
- [ ] Gate or redact kernel addresses and sensitive metadata according to
  policy, including data written to `.rperf`.
- [ ] Keep the GUI unelevated.  If privileged capture needs a broker, define a
  narrow authenticated protocol and make elevation explicit to the user.
- [ ] Audit start/stop, target scope, requested events, privilege failures, and
  security-relevant configuration without logging captured stack contents.
- [ ] Add denial tests for unrelated users, protected/system targets, stale
  handles, duplicated handles, malformed requests, and privilege removal.

## P1: hardware PMU support

- [ ] Add a capability query for architectural and model-specific PMU events,
  counter count/width, fixed counters, supported privilege modes, and precise
  sampling facilities.
- [x] Support a timer-based software event as the portable current source and make
  the selected source visible in each session.
- [ ] Validate event encodings in the kernel; never accept raw register values
  from an untrusted GUI without policy checks.
- [ ] Program and save/restore per-CPU counters safely across context switches,
  interrupts, CPU idle/hotplug, and competing kernel users.
- [ ] Handle counter overflow, skid, interrupt throttling, recursion, and
  spurious overflow without unbounded interrupt work.
- [ ] Support frequency and period modes with the actual observed period stored
  per sample.
- [ ] Define event groups, multiplexing, enabled/running time, and scaling so
  counts from oversubscribed counters are not presented as raw totals.
- [ ] Surface unavailable or virtualized PMUs clearly and fall back without
  changing the requested metric silently.
- [ ] Add architecture-specific conformance tests against deterministic
  instruction, branch, and cache microbenchmarks where the hardware permits.

## P2: lifecycle, off-CPU, and scheduler timeline analysis

- [ ] Record scheduler switch, ready/wakeup, block, migration, and priority
  changes with timestamps, CPU, old/new thread identity, and state/reason.
- [ ] Correlate scheduler records with process/thread lifecycle records without
  relying on reusable numeric IDs alone.
- [ ] Define off-CPU intervals and their attribution rules, including sleep,
  synchronization wait, I/O wait, preemption, and unknown wait reason.
- [ ] Optionally capture a bounded blocking-site stack at the transition to a
  waiting state.
- [ ] Replace or augment userspace snapshot markers with per-thread and per-CPU
  scheduler lanes, zoom, pan, and precise visible-range summaries.
- [x] Apply the current sample timeline's time selection consistently to hot
  functions and the flame graph.
- [ ] Keep CPU-sample cost, wall-clock latency, and off-CPU duration as separate
  metrics; never combine their percentages without an explicit mode.
- [ ] Show gaps and lost intervals in the timeline rather than interpolating
  apparently complete activity.

## P2: launch and capture workflows

- [ ] Extend basic executable/argument/working-directory launch with explicit
  environment and inherited-handle controls.
- [ ] Define whether launch begins suspended so image/lifecycle metadata and
  sampling are active before the first user instruction, then resume exactly
  once.
- [ ] Support attach/detach without terminating the target and make detach
  completion observable.
- [ ] Support optional child-process following with explicit process-tree
  semantics for already-running and newly-created children.
- [ ] Add recent configurations without persisting secrets from command lines
  or environments by default.
- [x] Support bounded-duration and stop-on-process-exit capture conditions.
- [ ] Add sample-limit and file-size capture conditions.
- [ ] Consider headless recording only after it uses the same session and file
  contracts as the GUI; do not fork a second capture implementation.

## P2: filters and comparative views

- [x] Filter the current single-process profile by thread and time range and
  provide symbol text search/highlighting.
- [ ] Add CPU, event, user/kernel context, module, resolved/unresolved state,
  and complete/truncated-call-chain filters when those dimensions exist.
- [x] Compose the current thread/time filters and use the active sample count
  beside filtered percentages.
- [ ] Extend one shared filter model to future call trees, scheduler timelines,
  and exports without changing current hot-function/flame semantics.
- [ ] Add top-down, bottom-up, and caller/callee views using the same stack
  aggregation rules as the flame graph.
- [x] Add case-insensitive symbol search highlighting to the flame graph.
- [ ] Add breadcrumb navigation, a minimum-cost threshold, cross-session stable
  colors, and complete accessible keyboard navigation to the flame graph.
- [ ] Add folded-stack, CSV, and a documented raw-record export without making
  exported data a substitute for a stable `.rperf` reader.
- [ ] Add two-profile comparison with explicit normalization and differential
  self/inclusive costs.

## P3: symbols, source, and disassembly

- [x] Resolve PE image addresses against immutable image identity, load base,
  section layout, and architecture captured in the recording.
- [x] Support matching PDB, embedded `.rossym`, DWARF, COFF/CodeView, and export
  symbols through a DbgHelp-backed provider; keep network retrieval disabled by
  default and explicit.
- [x] Validate symbol/image identity before use and show mismatches rather than
  applying symbols by filename alone.
- [x] Cache resolved symbols with bounded per-provider storage; construct a new
  provider when the image identity or symbol settings change.
- [x] Package priority PDBs only within the fixed debug-image budget, record
  every inclusion/fallback in an image manifest, and distinguish real embedded
  rossym fallback from module+offset fallback.
- [ ] Represent inline frames and source locations without altering the raw
  recorded call chain.
- [ ] Add a source view with path remapping and clear unavailable/stale-source
  states.
- [ ] Add architecture-aware disassembly with instruction addresses, symbol
  boundaries, sampled cost, and safe handling of missing image bytes.
- [ ] Attribute samples to instructions and source lines with an explicit rule
  for return-address adjustment and skid.
- [x] Never execute a profiled binary merely to symbolize it.

## P0-P3: tests and acceptance gates

### MVP unit and integration tests

- [x] Load deterministic complete multi-thread fixtures and verify metadata,
  canonical function aggregation, thread/time filtering, and root/path counts.
- [x] Load a footerless fixture and verify explicit incomplete-session salvage.
- [x] Reject malformed numeric fields, mismatched process IDs, missing frames,
  out-of-order timestamps, impossible symbol ranges, and records after the
  footer without modifying the previously held session.
- [x] Build and run the deterministic parser/aggregation self-test with GCC and
  Clang amd64 binaries under Wine.
- [x] Spawn a synchronized recursive workload, record live user stacks, verify
  the completed in-memory analysis, reopen the emitted log, and compare its
  sample/tree metadata under GCC and Clang amd64 Wine builds.
- [ ] Add recursive, duplicate-frame, truncated, unresolved, maximum-depth, and
  maximum-valid-value round-trip fixtures.
- [ ] Reject bad magic, unsupported version, truncated headers/records,
  oversized counts, overlapping ranges, integer overflow, and trailing partial
  records without crashing or allocating unbounded memory.
- [ ] Verify global self/inclusive recursion rules and contextual flame-path
  counts against hand-written call chains with the same function under multiple
  callers.
- [ ] Verify rendered flame-graph widths equal the selected contextual sample
  population at awkward pixel widths and after zoom/search/filter changes.
- [ ] Test process exit and rapid thread create/exit during start, capture,
  stop, and application shutdown.
- [ ] Fault-inject every operation between suspend and resume and prove the
  target is not left suspended.
- [x] Add a deterministic recursive end-to-end workload and assert structural
  invariants plus nonzero statistical samples rather than an exact count.
- [x] Drive the real GUI through launch, streamed recording, manual stop,
  reopen, timeline range, thread/search filters, reset, and all tabs; verify
  analysis controls remain isolated during recording.
- [x] Load 16,384 descending raw symbols and build 16,384 root flame paths
  within a fixed test budget, covering indexed symbol and tree-edge lookup.
- [x] Prove cancellable asynchronous open and presentation ownership, including
  progress, detached-result lifetime, worker cancellation, and late cancel.
- [x] Decode RosProf image identity and reject malformed variable records in a
  deterministic userspace fixture.
- [x] Decode ETW Image_Load v1/v2 for 32- and 64-bit event layouts and reject
  truncated, unterminated, and over-limit metadata fixtures.
- [x] Resolve a real built PE through the target DbgHelp, validate image
  identity, and accept embedded rossym/PDB/DWARF/COFF symbol sources.
- [x] Resolve a matching private PDB and reject both a wrong GUID/age and a
  truncated PDB under the GCC and Clang amd64 builds without unsafe cleanup.
- [x] Extract a native RosProf `.rperf` log, decode it on the host, locate
  matching PE images by immutable identity, resolve embedded rsym, and build
  the same analysis graph used by the GUI.
- [ ] Tighten that workload to identify named hot leaf/caller functions and
  assert bounded statistical ranges when symbol availability is controlled.

### Kernel and performance tests

- [x] Unit-test RosProf and ETW image-record decoding independently of live
  kernel capture.
- [ ] Retain valid and malformed fixtures for every supported RosProf record
  type and `.rperf` file/record version.
- [ ] Stress per-CPU buffers with forced wrap and slow/no consumers; verify
  ordering rules, sequence gaps, and exact final loss totals.
- [ ] Stress process/thread/image churn, numeric ID reuse, CPU hotplug,
  concurrent sessions, target exit, caller exit, cancellation, and shutdown.
- [ ] Test mixed user/kernel chains on each architecture with deliberate
  unwind failures, maximum depth, nested interrupts, and guard-page boundaries.
- [ ] Fuzz kernel configuration requests and all userspace file/record parsers.
- [ ] Run the privilege matrix automatically and prove denied sessions expose
  no addresses or target metadata.
- [ ] Benchmark interrupt time, samples per second, buffer throughput, memory
  footprint, loss rate, target slowdown, GUI load time, and analysis latency.
- [ ] Define acceptance thresholds before replacing the intrusive sampler as
  the default capture path.

## Exit criteria

The userspace MVP acceptance gate is complete when it builds cleanly and the
launch-or-attach, record, stop-or-complete, reopen, filter, and analyze smoke
test passes on ReactOS with the documented limitations.

The production profiler is not complete until the kernel session ABI is
reviewed, security and loss behavior are tested, lifecycle data makes traces
self-describing, non-intrusive user/kernel call chains work on supported
architectures, and the measured overhead/loss thresholds are met.
