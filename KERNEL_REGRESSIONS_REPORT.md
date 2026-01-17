# Kernel Regressions Report (unstaged diff)

Scope: Unstaged kernel/core-driver changes; items below are potential regressions or behavior risks introduced by the diff.

## Boot and initialization sequencing risks
- IoInitSystem no longer aborts on IopReassignSystemRoot failure, so SystemRoot may remain unset or incorrect and later driver loads may fail in non-obvious ways (ntoskrnl/io/iomgr/iomgr.c).
- ARM64 boot tolerates missing boot device/ARC names (IopMarkBootPartition/IopCreateArcNames), which can mask storage enumeration failures and defer root resolution beyond expected phases (ntoskrnl/io/iomgr/iomgr.c, ntoskrnl/io/iomgr/arcname.c).
- ARM64 PnP device actions are now always queued asynchronously; code that assumed synchronous root enumeration may observe devices later than before (ntoskrnl/io/pnpmgr/devaction.c).

## Stricter validation now bugchecks on latent bugs
- KeWaitForSingleObject and queue/timer paths validate dispatcher objects and list links and bugcheck on corruption instead of tolerating it (ntoskrnl/ke/wait.c, ntoskrnl/include/internal/ke_x.h, ntoskrnl/ke/queue.c).
- IofCallDriver/IoQueueWorkItem now validate dispatch/worker routine pointers on ARM64 and bugcheck if they are outside executable kernel ranges (ntoskrnl/io/iomgr/irp.c, ntoskrnl/io/iomgr/iowork.c, ntoskrnl/ex/work.c).
- KeSetEventBoostPriority only unwaits threads that are actually in Waiting/GateWait, which can strand waiters if their state is transiently inconsistent (ntoskrnl/ke/eventobj.c).

## Completion-path behavior changes
- IofCompleteRequest now completes certain synchronous buffered IOCTLs inline and frees the IRP immediately; if an APC was already queued or a driver expects deferred completion, this changes timing and could expose ordering bugs (ntoskrnl/io/iomgr/irp.c).

## Object-type flag reset side effects
- Reinitializing OBJECT_TYPE_INITIALIZER per type resets per-type flags (e.g., CaseInsensitive) that previously leaked from prior types; if user-mode relied on case-insensitive opens for those object types, behavior may change (ntoskrnl/io/iomgr/iomgr.c, ntoskrnl/lpc/port.c, ntoskrnl/ps/psmgr.c).

## Debug-only detection regressions
- Pool list integrity checks are effectively disabled on ARM64 (early return), reducing detection of corruption in debug builds (ntoskrnl/mm/ARM3/expool.c).

## Silent success paths that can mask real failures
- ExpWin32SessionCallout returns STATUS_SUCCESS when callout is NULL; if callouts fail to register later, failures may be hidden rather than surfaced (ntoskrnl/ex/win32k.c).

## Per-file details

### Boot and initialization sequencing
- `ntoskrnl/io/iomgr/iomgr.c`: SystemRoot reassignment failure is no longer fatal on ARM64, which can hide boot-volume failures and defer root resolution.
- `ntoskrnl/io/iomgr/arcname.c`: tolerates missing CD-ROM ARC names on ARM64 and recreates links on collision; can mask storage failures or overwrite stale links.
- `ntoskrnl/io/pnpmgr/devaction.c`: ARM64 queues device actions asynchronously; any code assuming synchronous enumeration may observe devices later.
- `ntoskrnl/io/pnpmgr/pnpirp.c`: clears UserEvent/UserIosb for synchronous completions; if a caller relied on APC signaling later, that signal will be suppressed.
- `ntoskrnl/ex/win32k.c`: NULL callout now returns STATUS_SUCCESS, which can hide missing win32k callouts during early boot.

### Stricter validation and bugcheck exposure
- `ntoskrnl/ke/wait.c`: invalid object pointers or corrupted wait lists now bugcheck, converting latent corruption into hard failures.
- `ntoskrnl/include/internal/ke_x.h`: timer and ready list removal now validates list links; corrupted lists may skip removal or assert.
- `ntoskrnl/ke/queue.c`: queue entries that alias the queue object now bugcheck with INVALID_WORK_QUEUE_ITEM.
- `ntoskrnl/io/iomgr/irp.c`: ARM64 dispatch routine pointer validation can bugcheck drivers with invalid MajorFunction entries.
- `ntoskrnl/io/iomgr/iowork.c`: invalid work item routine pointer now bugchecks instead of failing later.
- `ntoskrnl/ex/work.c`: invalid worker routine now bugchecks; previously may have been silent corruption.
- `ntoskrnl/mm/ARM3/sysldr.c`: new System View overlap assertions can stop boot if image bases are corrupt.
- `ntoskrnl/ke/thrdschd.c`: WaitIrql clamping can mask corruption and alter IRQL restoration behavior.

### Completion-path timing changes
- `ntoskrnl/io/iomgr/irp.c`: synchronous buffered IOCTLs may complete inline with immediate buffer copy and IRP free, changing APC timing.

### MM cache and mapping semantics
- `ntoskrnl/mm/ARM3/miarm.h`: ARM64 cache attribute encoding changed; may alter caching behavior for existing mappings.
- `ntoskrnl/mm/ARM3/iosup.c`: forces Device-nGnRnE for MMIO on ARM64; can change device behavior/performance.
- `ntoskrnl/mm/section.c`: SEC_RESERVE file-backed sections now set RawLength to file size; if any code relied on RawLength==0, behavior changes.
- `ntoskrnl/mm/ARM3/pagfault.c`: invalid page-table PTE now returns STATUS_INTERNAL_ERROR; changes fault recovery path.

### Debug and logging overhead
- `ntoskrnl/cache/section/reqtools.c`: error-level DbgPrintEx in page-in path can flood logs and impact performance.
- `ntoskrnl/cache/section/io.c`: error-level paging I/O status logs in `_MiSimpleRead` are on a hot path.
- `ntoskrnl/cc/copy.c`: verbose read-ahead logging at error level can add overhead.
- `ntoskrnl/io/iomgr/ramdisk.c`: error-level logging for descriptor selection may be noisy on boot.

### Debugger behavior and build surface
- `ntoskrnl/kd/kdterminal.c`: ARM64 skips VT100 detection; terminal features like line-wrap detection are reduced.
- `ntoskrnl/kd/kdio.c`: prompt newline removal changes KD console behavior; some tooling may expect the extra newline.
- `ntoskrnl/ntos.cmake`: ARM64 now links full KDB sources, increasing debugger code surface and potential crash exposure.
- `ntoskrnl/ntoskrnl.spec`: export changes (ARM64 SLIST and RtlPcToFileHeader) can affect binary compatibility for external tools.

### Debug-only detection gaps
- `ntoskrnl/mm/ARM3/expool.c`: pool link integrity checks are disabled on ARM64, reducing debug-time detection.

### SDK / build system risks
- `sdk/cmake/CMakeMacros.cmake`: `--allow-multiple-definition` can mask duplicate CRT symbols; extra startup objects may change GUI entrypoint resolution.
- `sdk/cmake/gcc.cmake`: DWARF-only path removes rsym generation; if rossym_new or DWARF parsing is incomplete, symbolization may regress.
- `sdk/cmake/gcc.cmake`: ARM64 warning policy relaxes `-Werror` and `-Werror=maybe-uninitialized`, which can hide new warnings in Debug builds.
- `sdk/cmake/gcc.cmake`: ARM64 user-mode libgcc linking adds new dependencies; `--whole-archive` gcc-compat can increase binary size or pull unintended objects.
- `sdk/cmake/host-tools.cmake`: removal of `rsym` tool may break workflows expecting `.rossym` outputs.
- `sdk/cmake/rust.cmake`: gnullvm fallback and linker wrapper logic can mis-handle nonstandard toolchains; disabling Rust on MinGW ARM64 reduces coverage.
- `sdk/cmake/baseaddress_arm64.cmake`: fixed low 32-bit base addresses can collide with future DLLs or large modules, increasing relocation pressure.

## Additional commits reviewed (3ac3e6193f58fcc4d2d321f2c9eaa88da2700dad, 033dee879fea8f7a412bbbeb40e714cdf8a08d26)

### Boot/loader risks
- `boot/boot_images.cmake`: larger (8MB) efisys and split setup/livecd EFIs increase ISO size and may break tooling expecting single efisys.bin; i386 UEFI disabled (compat regression).
- `boot/tools/make_reactos_img.sh`: skipping BIOS bootsector checks under `--uefi-only` can hide missing artifacts for mixed-boot targets.
- `boot/freeldr/arch/arm64/macharm64.c`: skipping UefiInitializeDebugImageInfo refresh and delaying vectors/timer until after ExitBootServices may leave stale firmware mappings if boot fails early; raw UART assumes PL011 base 0x09000000.
- `boot/freeldr/arch/arm64/mmu_v2.c`: PFN DB base shift and new L3 pool sizing risk mismatched loader/kernel assumptions; TLBI helpers must match CPU.
- `boot/freeldr/arch/uefi/arm64/uefiasm.S`: altered ExitBootServices path could regress on firmware sensitive to cache maintenance ordering.
- `boot/freeldr/arch/uefi/uefildr.c`: proceeding headless if GOP init fails can mask display regressions.
- `boot/freeldr/arch/uefi/uefimem.c`: ignoring AllocatePages failures for pinning may allow firmware reuse of memory between map fetch and exit; AllocatePool map buffer loses page-alignment guarantee.
- `boot/freeldr/arch/uefi/uefiserial.c`: treating ReservedMemory as MMIO can mis-detect UART; dropping SPCR update removes firmware hand-off.
- `boot/freeldr/arch/uefi/uefisetup.c`: ARM64-only MachInit bypass may diverge from x86 paths.
- `boot/freeldr/disk/ramdisk.c`: optional ramdisk suppresses UI errors; LoaderXIPRom use and optional allocs change kernel ramdisk detection; failure paths may now silently continue.
- `boot/freeldr/pcat.cmake`: AMD64 objcopy --strip-all risks stripping needed symbol info for diagnostics.
- `boot/freeldr/uefi.cmake`: rossym only under KDBG may break symbolization when KDBG off.
- `boot/freeldr/ui/ui.c`: forced NoUI when GOP not ready can hide video init faults.

### ARM64 kernel risk surface
- `ntoskrnl/arch/arm64/config/cmhardwr.c`: new registry writes (VendorIdentifier/MHz) may not match firmware values; wrong strings could confuse setup tools.
- `ntoskrnl/arch/arm64/ex/ioport.c`: added DMBs change MMIO ordering; driver assumptions about relaxed ordering may differ.
- `ntoskrnl/arch/arm64/include/ke.h`: stronger TLB barriers (DSB SY) increase cost; mis-sized icache flush can degrade perf.
- `ntoskrnl/arch/arm64/include/mm.h`: MI_SET_PTE_ATTR_INDEX modifies OsAvailable2; kernel must agree on attr encoding or cache attributes corrupt.
- `ntoskrnl/arch/arm64/ke/cpu.c`: KeFlushIoBuffers now executes cache maintenance; drivers relying on implicit coherency may see new latency; failure paths return silently on unmapped MDLs.
- `ntoskrnl/arch/arm64/ke/ctxswitch.S`: stack alignment and ApcBypass init changes could expose latent ABI mismatches; dual-return handling must match KiSwapContextInternal.
- `ntoskrnl/arch/arm64/ke/interrupt.c`: virtual vs physical timer toggle and LPI support alters interrupt routing; incorrect choice may stall ticks.
- `ntoskrnl/arch/arm64/ke/irql.c`: garbage IRQL clamp logs only; still allows silent clamp; DAIF only masks IRQ leaving FIQ/SError unmasked.
- `ntoskrnl/arch/arm64/ke/rtlshim.c`: __atomic_compare_exchange_16 loops may spin if cache lines shared with devices; DAIF masking only IRQ may differ from callers expecting full mask.
- `ntoskrnl/arch/arm64/ke/spinlock.c`: PFN lock depth tracking only for PFN lock; diagnostics may run at DISPATCH_LEVEL; DPRINT noise possible.
- `ntoskrnl/arch/arm64/ke/stubs.c`: SMP bring-up implementation unverified; HalStartNextProcessor failure handling frees stacks after loop—potential leak/misfree if partial success.
- `ntoskrnl/arch/arm64/ke/thrdini.c`: idle loop DPRINTs at high rate can flood serial; forced ready-thread recovery may pick wrong thread.
- `ntoskrnl/arch/arm64/ke/trapc.c`: MmAccessFault dispatch changes fault path; incorrect build flags could route all faults to ARM3 handler.
- `ntoskrnl/arch/arm64/ke/trapdump.c`: conditional PTE/backtrace dumping may be compiled out, reducing diagnostics.
- `ntoskrnl/arch/arm64/rtl/rtlexcpt.c`: skipping RtlLookupFunctionEntry at IRQL>DISPATCH reduces backtrace accuracy; reentrancy guard may hide frames.

### SDK / CRT / tools risk points
- `sdk/lib/cportlib/cport_arm64.c`: removing echo suppression may break half-duplex setups; Wait=FALSE tries once could miss slow UART data.
- `sdk/lib/crt/startup/arm64/libgcc_compat.c`: weak heap shims may conflict with real CRT if linked; abort raises STATUS_UNSUCCESSFUL (no diagnostics).
- `sdk/lib/crt/startup/crt0_c.c`, `crt0_w.c`: dummy main/wmain can hide missing entrypoints; built into GUI apps only.
- `sdk/lib/crt/startup/crt_handler.c`: skipping emu pdata/xdata on ARM64 reduces SEH coverage.
- `sdk/lib/drivers/arbiter/*`: ownership flags rely on callers not reusing WorkSpace bits; bugcheck on NULL mutex increases crash likelihood in early boot.
- `sdk/lib/rossym*`: new DWARF cache and error tracking add allocations; cache build failures set built=-1 and disable lookups; more verbose debug can slow symbolization.
- `sdk/lib/rtl/arm64/unwind.c`: stack bound checks can return FALSE and raise STATUS_BAD_STACK, aborting unwind where it previously continued.
- `sdk/lib/rtl/image.c`: ARM64 relocation handling risks mis-patching ADRP/ADD/LDR if delta exceeds encoded range.
- `sdk/lib/rtl/slist.c`: disabling redefine_extname on ARM64 requires kernel wrappers; absence breaks ExpInterlocked*SList callers in user-mode builds.
- `sdk/lib/ucrt/inc/corecrt_internal_stdio_*`: constructor init change could differ under older compilers.
- `sdk/lib/vcruntime/arm64/longjmp.c`: __builtin_unreachable may trap on some toolchains if longjmp returns.

### SDK headers / ABI risks
- `sdk/include/asm/asm.inc`: retfq use and removal of MASM alias macros can break existing asm sources; io_delay uses OUT 0x80 (x86-only semantics).
- `sdk/include/asm/syscalls.inc`: Zw* jump to Nt* eliminates SVC; code expecting SVC side effects (trace/hook) loses visibility.
- `sdk/include/ddk/ioaccess.h`: ARM64 MMIO macros add DSB barriers and buffer ordering; can hurt perf or differ from kernel exports; includes minimal declarations only.
- `sdk/include/ndk/arm64/ketypes.h`: new InHighLevelTransition field changes PRCB layout/size (ABI impact to drivers using private structs).
- `sdk/include/ndk/halfuncs.h`, `reactos/hal/acpi_pci.h`: new MSI/timer APIs may be unimplemented in HAL; callers may fail.
- `sdk/include/xdk/arm64/ke.h`: kernel stack size doubled; drivers assuming 12KB may allocate less for custom stacks.
- `sdk/include/vcruntime/mingw32/intrin_arm64.h`: added atomics can emit ldaxr/stlxr loops; may be unsupported on very old CPUs.