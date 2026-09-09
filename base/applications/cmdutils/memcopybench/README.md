<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
-->

# memcopybench

Run `memcopybench` for CPU RAM-to-RAM copy measurements, or
`memcopybench --serial --special` to also send the completed results to the
debug channel and test noncached/write-combined allocations.

The tool resolves the installed MSVCRT `memcpy`/`memmove` and NTDLL `memcpy`
exports at runtime. ARM64 builds also include explicit scalar and NEON 64-byte
copy loops as experimental **disjoint, normal-memory-only** comparisons. These
loops are not replacements for the shared kernel/boot copy routine.

Measurements cover 64 bytes through 16 MiB, including an 8,294,400-byte 1080p
32-bit surface. Source/destination offsets are (0,0), (1,1), (0,4), and (1,3).
Repeated mode reuses a single block; rotating mode traverses a 32 MiB arena
per buffer. A repeated block larger than the cache is not a cache-resident
measurement. Neither mode promises a particular cache level or flushed cache.

The harness is optimized even in the DEBUG OS build. It prefaults allocations,
warms the selected working set, requests affinity to CPU 2 when available,
calibrates batches toward 25 ms, and records three trials per case. Each batch
has two performance-counter reads and one final ARM64 `dsb ish`; no per-copy
timers or log output are used. Correctness checks, thread CPU accounting, and
result printing occur outside the timed copy batches. Method order alternates
between cases to reduce fixed ordering bias. The separate empty-call result
estimates harness overhead; it is not subtracted from the copy results.

`MEMCOPY_ROW` reports median/minimum/maximum ticks, payload MB/s (10^6 bytes),
MiB/s (2^20 bytes), and nanoseconds per copy. Payload counts each byte once;
this is not the total DRAM read/write/cache-maintenance traffic. `cpu_100ns`
is accumulated thread CPU time across all three trials, with the OS accounting
resolution. These are wall-time throughput tests with ordinary scheduling,
not isolated hardware bandwidth limits or GPU/DMA benchmarks.

`--special` requests PAGE_NOCACHE and PAGE_WRITECOMBINE through VirtualAlloc
and reports VirtualQuery protection flags, allocation failures, and caught
exceptions. Those flags alone do not independently establish live PTE/MAIR
attributes. The special cases use aligned 1 MiB buffers in each direction.

Results are buffered until measurements finish, so a serial watchdog must
allow at least several minutes of silence. The application exits nonzero
on validation/measurement failures. It does not enable persistent profiling,
install a startup task, or change memory-management policy.

`--nc-only` runs the cached 1 MiB control and NC/WC cases. Additional diagnostics
compare paired SIMD loads, LD1 element widths, bounded 256/512-byte prefetches,
and non-temporal stores. UCRT exports are also measured when its DLL loads.

`--streaming` uses **128 MiB per buffer**, 250 ms target trials, and selected
runtime/scalar/SIMD candidates. It covers rotating 4 KiB, 1 MiB, 16 MiB and
32 MiB copies plus random 4 KiB copies. Independent source/destination page
permutations are precomputed outside timing; there is no RNG in the timed loop.
All random destinations are unique within a permutation, and every block is
validated after the trials. This mode cannot be combined with the NC switches.

For direct comparisons of Mesa's actual tile helper, configure the optional
`MEMCOPYBENCH_MESA_BEFORE` and `MEMCOPYBENCH_MESA_AFTER` CMake paths to two
`v3d_cpu_tiling.h` snapshots. The default build has no Mesa dependency. Such a
build also measures both 8-byte and 16-byte utile-row layouts from each header.

`--pi-clocks` optionally opens the Raspberry Pi RPIQ mailbox driver and captures
configured/maximum clocks plus measured ARM/core/SDRAM clocks and temperature
before and after each measured case. All requests and printing are outside
copy timing; firmware requests can still perturb conditions between batches.
Errors are reported per field, so a missing driver or unsupported firmware tag
is not mistaken for a zero clock. Samples bracket the trials; they are not
continuous clock measurements. Other boards can omit this switch.

The common build stays `armv8-a` / generic tuning. Experimental pipelined and
interleaved copy loops also use only baseline ARMv8-A instructions. CPU-specific
system-register programming, fixed cache-block sizes, overclocking, SVE, SME,
and newer copy extensions are not introduced by these experiments. Pi 3/A53
performance measurements do not establish Pi 4/A72 or Pi 5/A76 performance;
emulated correctness checks on those CPU models are recorded separately.

`--placement` sweeps destination offsets 0, 64, 128, 256, 512, 1024, 2048 and
4096 for 1 MiB/16 MiB rotating copies in 128 MiB arenas. It compares installed
NTDLL and the scalar diagnostic without assuming any physical cache coloring.
`--unaligned` focuses on 64 KiB, 1 MiB, 1080p and 16 MiB cached copies with all
four alignments, plus 1 MiB NC/WC cases in both directions with those same
alignments. It selects NTDLL, UCRT, scalar and paired-SIMD methods. These modes
are mutually exclusive with `--streaming` and with each other.

`--trial-ms 10..1000` overrides the per-trial target (normally 25 ms, or
250 ms for streaming/placement). Longer batches help assess noisy rows;
minimum/maximum/median durations remain available rather than hiding variation.
