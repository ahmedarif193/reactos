# PR1 staging — ARM64 SDK enablement

This directory contains the contents of the first upstream PR from the
dev-nt6-1-arm64 fork to reactos/reactos. Scope is intentionally narrow:
**SDK-level ARM64 enablement only** — no kernel, no HAL, no driver code.

## Goal of PR1

Make `configure.sh -DARCH=arm64` produce a usable Clang/GCC ARM64
toolchain invocation and allow ARM64 sources to compile cleanly,
without changing behavior or ABI on any existing arch.

## Contents

### Whole-file replacements (drop in over upstream)

| Path | Type | Notes |
|---|---|---|
| `sdk/include/vcruntime/mingw32/intrin_arm64.h` | Replace | 850 lines. Fixes 3 bugs (`_AddressOfReturnAddress`, `_InterlockedExchange*` ordering, `_InterlockedCompareExchange128` libcall). Adds `__yield`, `__dmb/__dsb/__isb`, `_InterlockedAdd/64`, `__readx18*`/`__writex18*`. Adds defensive `HAS_BUILTIN`/`__INTRIN_INLINE` fallbacks. Verified against Win11 ARM64 ntoskrnl disasm (uses `casal`/`ldaddal`/`swpal`). |
| `sdk/include/xdk/ntbasedef.h` | Replace | 4-line guard widening (also see ntbasedef.h.patch for the minimal hunk). |
| `sdk/include/asm/ksarm64.template.h` | Replace | Adds the `CONTEXT` `OFFSET()` block. CONTEXT struct is correct in upstream's xdk/arm64/ke.h (PDB-verified), so generated offsets will be right. **Intentionally omits** KTRAP_FRAME, KEXCEPTION_FRAME, KSWITCH_FRAME, KSTART_FRAME, UCALLOUT/KCALLOUT_FRAME, KIPCR offset blocks — those need NDK struct rewrites first (see "Deferred" below). |

### Targeted patches (apply with `git apply`)

| Patch | Hunk size | Notes |
|---|---|---|
| `ketypes.h.patch` | 1 line | `KSEG0_BASE` value fix: `0xfffff80000000000` → `0xFFFF800000000000`. The previous value was the AMD64 KSEG0 mistakenly carried over. Verified against Win11 PDB and absolute references in scheduler disasm. |
| `gcc.cmake.patch` | 2 lines | Adds the ARM64 `elseif()` branch with `-fno-optimize-sibling-calls -fno-omit-frame-pointer -mstrict-align`. |
| `ntbasedef.h.patch` | 4 lines | Same as the whole-file replacement above, but as a minimal hunk for cleaner review. |

## Verification done before PR

- `intrin_arm64.h`: built standalone via `clang --target=aarch64-w64-mingw32 -fsyntax-only` through the umbrella `intrin.h` path — passes.
- 128-bit CAS asm: hand-disassembled `-S` output — emits the intended `ldaxp/cmp/ccmp/stlxp` LL/SC loop, no libcall, no x18 touch.
- `_InterlockedExchange*` ordering: confirmed switch to `__atomic_exchange_n SEQ_CST` lowers to `swpal` (LSE) or full `ldaxr/stlxr`+`dmb` (baseline).
- All `OFFSET()` macros in template resolve against existing struct definitions — no build breaks.

## Deferred (NOT in PR1)

Each item below needs its own dedicated PR with PDB cross-reference in
the commit message. The fork carries these but they have known
correctness or ABI issues that block bundling them into PR1.

### CRT math (`sdk/lib/crt/math/arm64/`)
The fork's 13 files include 6 with confirmed `__builtin_*`-to-self
infinite-loop bugs (`atan`, `exp`, `fmod`, `ldexp`, `logb`) — verified
via clang `-S` codegen (clang lowers e.g. `__builtin_exp(x)` to `bl
exp` which becomes `b .loop_self` after tail-call optimization). The
fork's bring-up doesn't exercise these so it appears to "work," but
shipping them upstream would put hangs in printf("%f"). Three dead
files (`atan2.c`, `atan2.s`, `atan2_backup.s` — none referenced by
cmake) have been deleted from the fork. The remaining files need
proper fdlibm-style implementations (`log.c` shows the right pattern)
before they're shippable.

### NDK ARM64 struct rewrites
Per the binary-ABI verification report (PDB cross-check against
`~/Downloads/win11-arm64-disasm/.../symbols/ntkrnlmp.pdb`), the
fork's `KTRAP_FRAME` (sizeof 352 vs Win11's 336), `KEXCEPTION_FRAME`
(AMD64-shaped instead of ARM64's X19-X28+Fp+Return), `KSWITCH_FRAME`
(112 vs 32 bytes), `KIPCR` (Prcb at offset 3392 vs 2432), and `KPRCB`
(AMD64-shaped) are not Windows ARM64 ABI compatible. Each needs a
dedicated PR with the right layout. The CONTEXT struct and the
KSPECIAL_REGISTERS order in upstream are already correct.

### Toolchain restructuring
The fork carries a 761-line standalone `sdk/cmake/clang.cmake` that
forks Clang support out of `gcc.cmake`. This is a meaningful
infrastructure decision that should be reviewed on its own merits in
a dedicated PR, not bundled with ARM64 enablement. The minimal
gcc.cmake hunk in this PR1 is sufficient to enable ARM64 builds with
upstream's current structure.

### `config.cmake` rework
Fork carries `SEPARATE_DBG`/`WITH_DEBUG_SYMBOLS` debug-symbol
generation, `REACTOS_TARGET_NT` version selector, and
`ENABLE_FEX_ARM64EC` option. None ARM64-specific — defer to separate
infra PRs.

### Kernel and HAL (`ntoskrnl/arch/arm64/`, `hal/halarm64/`)
59 + 21 new files. Each needs the NDK struct rewrites above to land
first.

### freeldr UEFI ARM64 (`boot/freeldr/freeldr/arch/uefi/arm64/`)
4 files. Needs freeldr cmake to gain an ARM64 target — its own PR.

## PR title and description (draft)

```
[NDK][SDK][CMAKE] Enable ARM64 SDK builds

Adds the SDK-level prerequisites to build ARM64 sources under
aarch64-w64-mingw32 clang. No behavioral change for i386/amd64/arm.

* sdk/include/vcruntime/mingw32/intrin_arm64.h
  - Fix _AddressOfReturnAddress to point at AAPCS64 LR slot
  - Switch _InterlockedExchange* to __atomic_exchange_n SEQ_CST
    (was acquire-only via __sync_lock_test_and_set)
  - Replace _InterlockedCompareExchange128 with baseline-armv8-a
    ldaxp/stlxp inline asm (avoids __sync_*_16 libcall fallback)
  - Add __yield, __dmb/__dsb/__isb, _InterlockedAdd[64],
    __readx18*/__writex18*
* sdk/include/asm/ksarm64.template.h
  - Add CONTEXT offset block (matches PDB-verified xdk CONTEXT)
* sdk/include/xdk/ntbasedef.h
  - Enable RotateLeft8/16 helpers on _M_ARM64
* sdk/include/ndk/arm64/ketypes.h
  - Fix KSEG0_BASE to Win11 ARM64 value 0xFFFF800000000000
* sdk/cmake/gcc.cmake
  - Add ARM64 codegen flags (-mstrict-align, frame pointer)

Verified against Win11 ARM64 ntkrnlmp.pdb and ntoskrnl disasm.
Follow-up PRs will land NDK struct rewrites (KTRAP_FRAME etc.),
CRT math implementations, freeldr/HAL/kernel.
```
