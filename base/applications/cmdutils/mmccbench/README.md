# mmccbench

Baseline harness for the memory manager (`ntoskrnl/vmm`) and cache manager
(`ntoskrnl/cc`). It exists to give a *before* number, so that any later rewrite
of either subsystem can be judged against measurement rather than intuition.

Every metric is aggregate throughput across `-t` worker threads, timed with
`QueryPerformanceCounter` around a barrier-synchronised pass. Iteration counts
adapt until a pass reaches `-ms` milliseconds, so slow debug builds and fast
release builds both produce a stable figure. Nothing is printed during a
measured pass.

## Metrics

| Metric | Kernel path exercised |
| --- | --- |
| `mm_fault_demand_zero` | `MmAccessFault` demand-zero, PFN lock, zero page list |
| `mm_commit_decommit` | VAD split/merge, PTE fill and teardown |
| `mm_reserve_release` | VAD insert/remove only, no committed pages |
| `mm_section_map_unmap` | section create, view map/unmap, control area refcount |
| `cc_write_cached` | `CcCopyWrite`, VACB allocation, lazy writer queueing |
| `cc_read_hot` | `CcCopyRead` against a fully resident cache map |
| `cc_read_random_hot` | VACB lookup cost at 4K granularity |
| `cc_mapped_read` | mapped-view fault path against a cached file |

## Usage

    mmccbench [-t threads] [-f fileMB] [-a arenaMB] [-ms targetMs]

Defaults: 1 thread, 16 MB file, 32 MB arena, 1000 ms per pass.

Output is one `MMCC <metric> threads=<n> <value> <unit>` line per metric,
bracketed by `MMCC_BEGIN` and `MMCC_DONE`. The prefix makes results greppable
out of a kernel debug log when the binary is driven by `dbgprint --process`.

## Reading the numbers

Run the same binary at `--smp 1` and `--smp 4`. A metric that fails to scale
between the two is contended, not slow: `mm_fault_demand_zero` is bounded by
the PFN lock, and the `cc_*` metrics by per-shared-cache-map locking. Compare
`-t 1` against `-t 4` within one SMP setting to separate raw path cost from
contention.
