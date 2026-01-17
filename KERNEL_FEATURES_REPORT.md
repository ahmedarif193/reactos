# Kernel Features Report (unstaged diff)

Scope: Unstaged kernel/core-driver changes in this worktree; emphasis on NT kernel, MM, I/O, PnP, and debugger paths.

## Synchronization and scheduling
- ARM64 spinlocks now use LDAXR/STXR + STLR/SEV with explicit acquire/release semantics, plus safe test path with LDAR (ntoskrnl/include/internal/spinlock.h, ntoskrnl/ke/spinlock.c).
- Event/wait paths add ARM64 barriers and list validation to avoid missed signals and corrupted wait lists (ntoskrnl/ke/eventobj.c, ntoskrnl/ke/wait.c, ntoskrnl/include/internal/ke_x.h).
- DPC queue insertion and dispatcher paths add ARM64 barriers and idle wakeups to prevent stuck standby threads (ntoskrnl/ke/dpc.c, ntoskrnl/ke/thrdschd.c).
- Worker thread startup now yields on ARM64 to allow queues to drain during early boot (ntoskrnl/ex/work.c).
- Thread/queue structures now explicitly initialized to avoid stale list links (ntoskrnl/ke/thrdobj.c, ntoskrnl/ke/queue.c).

## I/O manager and PnP
- Object type initialization split per type to avoid ARM64 compiler misaddressing and to reset per-type flags (ntoskrnl/io/iomgr/iomgr.c, ntoskrnl/ps/psmgr.c, ntoskrnl/lpc/port.c, ntoskrnl/ex/win32k.c).
- I/O timer start deferred until after object types are created to avoid IRQL asserts on ARM64 (ntoskrnl/io/iomgr/iomgr.c).
- Boot device handling improved with CD-ROM retries and tolerant ARC name handling, including link recreation when collisions occur (ntoskrnl/io/iomgr/iomgr.c, ntoskrnl/io/iomgr/arcname.c).
- IRP completion now handles synchronous buffered IOCTLs inline and enforces ARM64 ordering around user events (ntoskrnl/io/iomgr/irp.c).
- I/O work items add ARM64 release/acquire barriers plus executable-pointer validation to catch corrupted queue items early (ntoskrnl/io/iomgr/iowork.c, ntoskrnl/ex/work.c).

## Memory manager
- ARM64 PFN database mapping now builds per-range PTE mappings, flushes TB, and zeroes PFN entries after mapping (ntoskrnl/mm/ARM3/mminit.c).
- Free list population on ARM64 skips pages already consumed by early allocations and excludes page-table PFNs (ntoskrnl/mm/ARM3/mminit.c, ntoskrnl/mm/ARM3/miarm.h).
- Paged/nonpaged pool now initializes PFN PteFrame correctly, preventing share-count mismatches on free (ntoskrnl/mm/ARM3/pool.c, ntoskrnl/mm/ARM3/virtual.c).
- Nested fault handling avoids re-locking kernel address space and adds ARM64 recursion for prototype-PTE page faults (ntoskrnl/mm/mmfault.c, ntoskrnl/mm/ARM3/pagfault.c).
- System address validation now tolerates kernel address space locking paths and forces real fault handling where required (ntoskrnl/mm/ARM3/virtual.c).

## Pool and registry safety
- Pool allocator adds double-free detection and SLIST head sanity checks to survive corrupted free lists (ntoskrnl/mm/ARM3/expool.c).
- CM KCB allocator now detects stale or double frees with page-level sentinels and a freed-page cache (ntoskrnl/config/cmalloc.c).
- Guarded mutex reinitialization for ARM64 BSS clearing ensures paged-pool mutex validity in phase 1 (ntoskrnl/mm/ARM3/pool.c).

## Debugger and diagnostics
- ARM64 KD uses serial input unconditionally and skips PS/2 paths/VT100 probing to avoid hangs and garbage output (ntoskrnl/kd/kdmain.c, ntoskrnl/kd/kdps2kbd.c, ntoskrnl/kd/kdterminal.c).
- KD print now provides monotonic timestamps with ARM64 barriers and a global spinlock to avoid reordering (ntoskrnl/kd64/kdprint.c).
- KDBG backtrace and symbol logic improved with reentrancy guards, safer PE section checks, and ARM64 unwind heuristics (ntoskrnl/kdbg/kdb_cli.c, ntoskrnl/kdbg/kdb_symbols.c).
- ARM64 reentrancy tracking for DbgPrint avoids recursive logging hazards (ntoskrnl/rtl/libsupp.c).

## Per-file details

### Executive and core
- `ntoskrnl/ex/init.c`: ARM64 NLS init skips redundant user mapping copy; adds boot-stage diagnostics in early init.
- `ntoskrnl/ex/lookas.c`: ARM64 memory barrier before PRCB read in lookaside init to avoid stale PCR data.
- `ntoskrnl/ex/work.c`: ARM64 worker routine validation plus early-boot yields so worker threads reach wait state.
- `ntoskrnl/ex/win32k.c`: creates Win32 object types in phase 1 with NULL callout tolerance and per-type initializers.
- `ntoskrnl/ex/fmutex.c`: explicit fast mutex acquire path mirrors inline semantics for ARM64.
- `ntoskrnl/ob/oblife.c`: expanded parameter validation in object type creation for clearer early errors.

### Synchronization and scheduler
- `ntoskrnl/include/internal/spinlock.h`: ARM64 LDAXR/STXR acquire and STLR/SEV release, plus debug owner tracking.
- `ntoskrnl/ke/spinlock.c`: ARM64 try-acquire and test path use exclusive and acquire loads with 64-bit lock width.
- `ntoskrnl/ke/eventobj.c`: ARM64 barriers after SignalState updates and wait-thread state validation in boost path.
- `ntoskrnl/ke/wait.c`: ARM64 barrier sequences plus list integrity checks before unlinking wait blocks.
- `ntoskrnl/include/internal/ke_x.h`: barrier macro, list-entry validation in timers and ready list, unwait barriers.
- `ntoskrnl/ke/dpc.c`: ARM64 barrier after DPC queue insertion to publish queue depth updates.
- `ntoskrnl/ke/thrdschd.c`: request dispatch interrupt on UP when a thread becomes standby; clamp bad WaitIrql.
- `ntoskrnl/ke/thrdobj.c`: initializes QueueListEntry and Queue fields to avoid stale list entries.
- `ntoskrnl/ke/queue.c`: ARM64 guard against queue entries that alias the queue object itself.
- `ntoskrnl/ke/gate.c`: removes verbose debug dumps before gate assertions to reduce noise.

### Process manager
- `ntoskrnl/ps/psmgr.c`: per-type OBJECT_TYPE_INITIALIZER split; ARM64 SLIST user entrypoints disabled; extra logging.
- `ntoskrnl/ps/thread.c`: validates system thread start routine before calling to prevent NULL execution.

### I/O manager
- `ntoskrnl/io/iomgr/iomgr.c`: per-type object initializers, defer I/O timer start, CD-ROM boot retries, ARM64 boot tolerance.
- `ntoskrnl/io/iomgr/arcname.c`: ARC alias recreation on collision, ARM64 CD-ROM tolerance, event init before IOCTL.
- `ntoskrnl/io/iomgr/irp.c`: inline completion for synchronous buffered IOCTLs, ARM64 barriers, dispatch pointer validation.
- `ntoskrnl/io/iomgr/iowork.c`: ARM64 release/acquire barriers for work item fields and executable pointer validation.
- `ntoskrnl/io/iomgr/driver.c`: zeroed load params and added tracing around boot/system driver init paths.
- `ntoskrnl/io/iomgr/ramdisk.c`: improved candidate selection scoring and verbose logging for descriptor choice.
- `ntoskrnl/io/iomgr/iomdl.c`: formatting-only changes; no functional delta.
- `ntoskrnl/io/iomgr/volume.c`: comment encoding change only; no functional delta.

### PnP and arbiters
- `ntoskrnl/io/pnpmgr/arb/arbbus.c`: remove PAGED_CODE in callbacks; INIT-only setup to allow high IRQL in boot.
- `ntoskrnl/io/pnpmgr/arb/arbdma.c`: same as arbbus.c for DMA arbiter callbacks and init.
- `ntoskrnl/io/pnpmgr/arb/arbirq.c`: same as arbbus.c for IRQ arbiter callbacks and init.
- `ntoskrnl/io/pnpmgr/arb/arbmem.c`: same as arbbus.c for memory arbiter callbacks and init.
- `ntoskrnl/io/pnpmgr/arb/arbport.c`: same as arbbus.c for port arbiter callbacks and init.
- `ntoskrnl/io/pnpmgr/devaction.c`: zero IoStatusBlock for PnP IRPs; ARM64 async queueing; wrapper zeroing.
- `ntoskrnl/io/pnpmgr/pnpinit.c`: INIT-only arbiter init and debug IRQL logging around lock setup.
- `ntoskrnl/io/pnpmgr/pnpirp.c`: synchronous PnP calls copy IoStatus and clear UserEvent/UserIosb safely.
- `ntoskrnl/io/pnpmgr/pnpres.c`: allow empty resource lists; ARM64 MSI vector range; vector IRQL adjustment.

### Memory manager
- `ntoskrnl/include/internal/mm.h`: PFN lock debug tracking for ARM64 and alias map helper prototype.
- `ntoskrnl/mm/ARM3/miarm.h`: ARM64 PTE cache attribute encoding fixes; NotLargePage for L3 PTEs; TLB flush on PTE write.
- `ntoskrnl/mm/ARM3/mminit.c`: PFN DB mapping by range with TB flush, PFN zeroing, and free list corrections.
- `ntoskrnl/mm/ARM3/pfnlist.c`: avoid standby insert for non-prototype pages; unlink free pages before initialization.
- `ntoskrnl/mm/ARM3/pagfault.c`: validate page-table PTEs and handle recursive faults for proto PTE pages.
- `ntoskrnl/mm/ARM3/pool.c`: initialize PteFrame for pool pages and guard paged pool mutex validity on ARM64.
- `ntoskrnl/mm/ARM3/expool.c`: freed-page cache and SLIST sanity checks to catch double-frees and corruption.
- `ntoskrnl/mm/ARM3/iosup.c`: ARM64 MMIO uses Device-nGnRnE attributes for non-cached IO mappings.
- `ntoskrnl/mm/ARM3/largepag.c`: validate driver list length before parsing large-page driver list.
- `ntoskrnl/mm/ARM3/section.c`: ARM64 diagnostics for segment allocation and prototype PTE fills.
- `ntoskrnl/mm/ARM3/sysldr.c`: PFN linkage fix after relocation; ARM64 I-cache sweep; System View overlap checks.
- `ntoskrnl/mm/ARM3/virtual.c`: avoids WS lock when not held; corrects page table share count on delete.
- `ntoskrnl/mm/mmfault.c`: avoids double-locking kernel address space during nested faults.
- `ntoskrnl/mm/mminit.c`: pre-map KI_USER_SHARED_DATA PTE on ARM64 and add phase 1 checkpoints.
- `ntoskrnl/mm/section.c`: RawLength fix for SEC_RESERVE file-backed sections; TLB invalidate on present faults.
- `ntoskrnl/mm/balance.c`: ARM64 barriers around balancer init flag and added diagnostics.
- `ntoskrnl/mm/rmap.c`: improved debug print formatting for PFN mismatch warnings.

### Registry / CM
- `ntoskrnl/config/cmalloc.c`: KCB double-free detection with signature and freed-page cache.
- `ntoskrnl/config/cmdelay.c`: KCB signature validation in delayed close/deref workers.
- `ntoskrnl/config/cmkcbncb.c`: validates KCB signature before dereferencing, preventing stale pointer use.
- `ntoskrnl/include/internal/cm.h`: adds `CM_KCB_ON_FREE_LIST_SIGNATURE` for free-list tracking.

### Debugger / KD / KDBG
- `ntoskrnl/kd/kdmain.c`: ARM64 forces serial input for KD to avoid PS/2 hangs.
- `ntoskrnl/kd/kdps2kbd.c`: ARM64 stubs PS/2 keyboard support paths.
- `ntoskrnl/kd/kdterminal.c`: ARM64 skips VT100 probe and drains serial input before enabling.
- `ntoskrnl/kd/kdio.c`: early logger event init; guard event signaling until thread is live; prompt handling tweak.
- `ntoskrnl/kd64/kdprint.c`: monotonic timestamp source with spinlock and ARM64 barriers.
- `ntoskrnl/kd64/kdinit.c`: initializes timestamp lock; avoids early KdpDprintf on ARM64.
- `ntoskrnl/kd64/kdlock.c`: avoids re-entry while debugger holds port.
- `ntoskrnl/kd64/kddata.c`: comment update only; no functional change.
- `ntoskrnl/include/internal/kd64.h`: exports monotonic timestamp helper prototypes.
- `ntoskrnl/kdbg/kdb.c`: ARM64 trap-frame handling, step-over detection, and flag handling.
- `ntoskrnl/kdbg/kdb_cli.c`: ARM64 register display, reentrancy guards, safer unwinding heuristics.
- `ntoskrnl/kdbg/kdb_expr.c`: ARM64 register mapping for expression evaluator.
- `ntoskrnl/kdbg/kdb_print.c`: KDBG timestamps unified with KD monotonic timestamps.
- `ntoskrnl/kdbg/kdb_symbols.c`: safe PE section checks for code addresses and safer symbol lookup.
- `ntoskrnl/kdbg/kdb.h`: declares `KdbpSymIsCodeAddress`.
- `ntoskrnl/rtl/libsupp.c`: ARM64 DbgPrint reentrancy tracking per thread.

### Cache and CC diagnostics
- `ntoskrnl/cache/section/reqtools.c`: adds paging I/O diagnostics around IoPageRead and MDL handling.
- `ntoskrnl/cache/section/io.c`: adds detailed paging I/O status logging in `_MiSimpleRead`.
- `ntoskrnl/cc/copy.c`: adds read-ahead diagnostics around VACB resident handling.

### Build and exports
- `ntoskrnl/ntos.cmake`: ARM64 now builds full KDB sources instead of a shim-only path.
- `ntoskrnl/ntoskrnl.spec`: ARM64 SLIST exports and `RtlPcToFileHeader` export adjustments.
- `ntoskrnl/CMakeLists.txt`: stop stripping ARM64 ntoskrnl to preserve DWARF for rossym parsing.

### SDK / build system
- `sdk/cmake/CMakeMacros.cmake`: adds non-MSVC WinMain/wWinMain bridge objects for GUI apps and allows multiple definitions for CRT startup objects.
- `sdk/cmake/gcc.cmake`: standardizes DWARF-only symbol flow, ARM64 warning policy tweaks, and links libgcc + gcc-compat for ARM64 user-mode modules.
- `sdk/cmake/host-tools.cmake`: removes `rsym` host tool from the non-MSVC tool list.
- `sdk/cmake/rust.cmake`: normalizes ARM64 Rust triples to gnullvm, adds linker wrapper logic for GCC, and disables Rust for MinGW ARM64 without libunwind.
- `sdk/cmake/baseaddress_arm64.cmake`: new ARM64 baseaddress map ensuring import libs fit within 32-bit relocation range.

## Additional commits reviewed (3ac3e6193f58fcc4d2d321f2c9eaa88da2700dad, 033dee879fea8f7a412bbbeb40e714cdf8a08d26)

### Boot/loader (UEFI, freeldr)
- `boot/boot_images.cmake`: UEFI efisys size raised to 8MB; separate setup/livecd efisys; i386 UEFI disabled; ISO EFI params split for setup/livecd; liveimg UEFI-only flag.
- `boot/tools/make_reactos_img.sh`: BIOS bootsector checks skipped when UEFI-only.
- `boot/freeldr/arch/arm64/macharm64.c`: ExitBootServices path redone; cache maintenance delayed until after exit; raw PL011 logging after exit; exception vectors delayed; timer init moved post-exit.
- `boot/freeldr/arch/arm64/mmu_v2.c`: PFN DB base moved, larger L3 pool, TLBI helpers, alignment helpers, yield in UART waits.
- `boot/freeldr/arch/arm64/trap.c`: UART wait uses `yield` instead of `wfi`.
- `boot/freeldr/arch/uefi/arm64/uefiasm.S`: safer ExitBootServices flow preserving LR/SP; cache maintenance after exit.
- `boot/freeldr/arch/uefi/ueficon.c`: avoids cursor sync after BootServices exit.
- `boot/freeldr/arch/uefi/uefildr.c`: GOP init optional; headless allowed.
- `boot/freeldr/arch/uefi/uefimem.c`: memory map via AllocatePool; pinning failures tolerated; FreePool used; ARM64 exception vectors decl.
- `boot/freeldr/arch/uefi/uefiserial.c`: treat ReservedMemory as MMIO; drop SPCR update on disable.
- `boot/freeldr/arch/uefi/uefisetup.c`: ARM64 MachInit routed to Arm64MachInit.
- `boot/freeldr/disk/ramdisk.c`: optional ramdisk flag; LoaderXIPRom for backing; optional UI warnings; new `RamDiskGetBackingStore`.
- `boot/freeldr/freeldr.spec`: exports optional alloc helpers and LoaderXIPRom alloc.
- `boot/freeldr/lib/mm/mm.c`: optional allocations (no UI) and shared helper.
- `boot/freeldr/lib/peloader.c`: drops unused thunk debug values.
- `boot/freeldr/ntldr/arch/arm64/winldr.c`: PCR size enlarged; exec mapping decision for identity map; PL011 debug prints; stack mapping tweaks.
- `boot/freeldr/ntldr/winldr.c`: ramdisk allocation failure fallback to read-only media.
- `boot/freeldr/ntldr/wlmemory.c`: map LoaderFree into KSEG0 on ARM64; PL011 debug prints.
- `boot/freeldr/ntldr/wlregistry.c`: ARM64 boot driver list debug dump.
- `boot/freeldr/pcat.cmake`: strip policy tightened; AMD64 uses objcopy --strip-all; .rossym removal dropped.
- `boot/freeldr/uefi.cmake`: rossym linked only under KDBG; gen DWARF preserved.
- `boot/freeldr/ui/ui.c`: NoUI fallback when GOP not ready; vtable init tidied.

### ARM64 kernel core
- `ntoskrnl/arch/arm64/config/cmhardwr.c`: real machine-dependent registry population for CPUs (VendorIdentifier, MHz, identifiers).
- `ntoskrnl/arch/arm64/ex/ioport.c`: exported MMIO accessors with DMB barriers (NO_PORT_MACROS).
- `ntoskrnl/arch/arm64/include/fpstate.h`, `ke/fpstate.c`: FP/SVE/SME lazy context switch support.
- `ntoskrnl/arch/arm64/include/ke.h`: TLB invalidate uses DSB SY; cache sweep per range; trap frame FP getter.
- `ntoskrnl/arch/arm64/include/mm.h`: ARM64 cache attribute helper; prototype PTE sign-extend; PTE hierarchy validity check; System View diagnostic hook.
- `ntoskrnl/arch/arm64/kd/kdfallback.c`: fallback print now quiet (returns TRUE).
- `ntoskrnl/arch/arm64/kdbg/kdb_help.S`: stack switch helper saves/restores callee-saved regs and frames.
- `ntoskrnl/arch/arm64/kdbg/kdb_shim.c`: trimmed to ARM64 helpers, relying on common KDB.
- `ntoskrnl/arch/arm64/ke/atomics.c`: explicit ExpInterlocked*SList wrappers for ARM64.
- `ntoskrnl/arch/arm64/ke/boot.c`: major bring-up (ACPI/GIC parsing, UART debug, memory attributes).
- `ntoskrnl/arch/arm64/ke/cpu.c`: real TLB invalidation sequence; KeFlushIoBuffers with cache maintenance.
- `ntoskrnl/arch/arm64/ke/ctxswitch.S`: thread startup lowers IRQL; stack align; ApcBypass init; dual return handling.
- `ntoskrnl/arch/arm64/ke/exceptinit.c`: stage logging via DPRINT1.
- `ntoskrnl/arch/arm64/ke/interrupt.c`: GIC LPI support, virtual vs physical timer handling, timer ISR reload first.
- `ntoskrnl/arch/arm64/ke/irql.c`: per-CPU reentrancy guard in PRCB; garbage IRQL clamp/logging; DAIF mask uses IRQ only.
- `ntoskrnl/arch/arm64/ke/kiinit.c`: SP_EL0 sentinel; memory barriers after PCR init; KDB banner cleanup.
- `ntoskrnl/arch/arm64/ke/rtlshim.c`: 16-byte atomics via ldaxp/stlxp; DAIF mask only IRQ.
- `ntoskrnl/arch/arm64/ke/spinlock.c`: PFN lock depth tracking; bootstrap pool bypass; DPRINT diagnostics.
- `ntoskrnl/arch/arm64/ke/stubs.c`: KeStartAllProcessors implemented (PSCI path, PCR init); MmDecommittedPte uses MM_DECOMMIT.
- `ntoskrnl/arch/arm64/ke/thrdini.c`: stack alignment fix; ApcBypass uses APC_LEVEL; idle loop diagnostics and ready-thread recovery.
- `ntoskrnl/arch/arm64/ke/trapc.c`: MmAccessFault dispatch used; trap logging via DPRINT1; working-set release logging.
- `ntoskrnl/arch/arm64/ke/trapdump.c`: stage log sink uses DPRINT1; optional PTE/backtrace dumps guarded.
- `ntoskrnl/arch/arm64/ke/traphdlr.c`: KiHandleKernelSListFault stub exported only on ARM64.
- `ntoskrnl/arch/arm64/rtl/rtlexcpt.c`: reentrancy guard for stack walk; safe memory reads; skip RtlLookupFunctionEntry at high IRQL.
- `ntoskrnl/arch/arm64/mm/ARM3/init.c`, `mm/page.c`: large ARM64 bring-up changes (PFN mapping, aliasing).
- `ntoskrnl/arch/arm64/rtl/rtlexcpt.c`: guard against recursive frame walks, safe reads.

### SDK libraries (ARM64 enablement)
- `sdk/lib/atl/atlcoll.h`: clang warning suppression for nontrivial memcall.
- `sdk/lib/cportlib/cport_arm64.c`: echo suppression removed; Wait=FALSE tries once; TX wait bounded with yield.
- `sdk/lib/crt/libgcc-compat.cmake`: build gcc-compat for ARM64.
- `sdk/lib/crt/math/libm.h`: allow 8- or 16-byte long double on ARM64.
- `sdk/lib/crt/mem/arm64/memset_asm.S`: new ARM64 memset asm; mem.cmake adds ARM64 mem sources.
- `sdk/lib/crt/precomp.h`: TRACE/WARN remapped with undef to avoid dllimport issues.
- `sdk/lib/crt/startup/arm64/libgcc_compat.c`: libgcc shims mapping malloc/abort/atexit/etc. to NT heap on ARM64.
- `sdk/lib/crt/startup/crt0_c.c`, `crt0_w.c`, `wcrtexe.c`: WinMain/wWinMain bridges compiled into GUI apps; dummy main/wmain.
- `sdk/lib/crt/startup/crt_handler.c`: skip pdata/xdata emulation on ARM64.
- `sdk/lib/crt/startup/startup.cmake`: removes crt0_c/crt0_w from common startup (linked per-GUI app).
- `sdk/lib/crt/stdlib/_set_abort_behavior.c`: includes stdlib; fixes __cdecl.
- `sdk/lib/crt/wine/cxx.h`: __thiscall empty on ARM/ARM64.
- `sdk/lib/drivers/arbiter/CMakeLists.txt`, `arbiter.c`, `arbiter.h`: ownership flags for assignments; overflow-safe range add; INIT-only paths; bugcheck if mutex missing; scoring constants documented.
- `sdk/lib/drivers/wdf/shared/inc/private/common/fxpkgio.hpp`: __fastcall undef/define for ARM/ARM64.
- `sdk/lib/pseh/include/pseh/pseh2.h`: dummy PSEH for ARM64.
- `sdk/lib/rossym/find.c`, `fromraw.c`: gap detection; NULL guard; 64-bit raw entry handling; debug prints.
- `sdk/lib/rossym_new/*`: DWARF cache with sorted ranges; address-range cache; safer line/function lookup; reduced noisy logs.
- `sdk/lib/rtl/arm64/unwind.c`: extra debug traces; stack bounds validation; boolean return for unwind apply.
- `sdk/lib/rtl/exception.c`: ARM64 register dump.
- `sdk/lib/rtl/heap.c`: remove unused attribute annotation.
- `sdk/lib/rtl/image.c`: ARM64 relocation support (ADRP/ADD/LDR, pageoffset).
- `sdk/lib/rtl/rtlp.h`: PAGED_CODE_RTL check only user-mode.
- `sdk/lib/rtl/slist.c`: disable redefine_extname on ARM64 (kernel provides wrappers).
- `sdk/lib/ucrt/inc/corecrt_internal_stdio_input.h`, `output.h`: valist init syntax tweak.
- `sdk/lib/vcruntime/arm64/longjmp.c`: __builtin_unreachable added.

### SDK headers / toolchain
- `sdk/include/asm/CMakeLists.txt`: genincdata marked NO_ROSSYM.
- `sdk/include/asm/asm.inc`: io_delay uses out 0x80; retf -> retfq; MASM alias cleanup; alignment macro fixes.
- `sdk/include/asm/syscalls.inc`: ARM64 Zw* call Nt* directly (no svc).
- `sdk/include/ddk/ioaccess.h`: ARM64 MMIO macros with DSB barriers; port macros reuse register access.
- `sdk/include/host/pecoff.h`: ARM64 machine type/exception structures.
- `sdk/include/ndk/arm64/ketypes.h`: PRCB gains InHighLevelTransition flag.
- `sdk/include/ndk/halfuncs.h`: ARM64 timer config query and MSI helpers.
- `sdk/include/reactos/drivers/cmreslist.h`: handle empty partial resource lists.
- `sdk/include/reactos/hal/acpi_pci.h`: MSI helpers and ARM64 variant.
- `sdk/include/reactos/libs/libmpg123/abi_align.h`: disable force_align_arg_pointer on ARM/ARM64.
- `sdk/include/reactos/rossym.h`: expose RosSymGetLastErrorString.
- `sdk/include/reactos/shlobj_undoc.h`: pack pragmas around DEFFOLDERSETTINGS.
- `sdk/include/vcruntime/_mingw.h`, `wine/config.h`: __thiscall/fastcall cleared for ARM; hotpatch attribute guarded.
- `sdk/include/xdk/arm64/ke.h`: kernel stack size increased to 24KB/60KB.
- `sdk/include/xdk/iotypes.h`: pack pragmas include ARM64.
- `sdk/include/vcruntime/mingw32/intrin_arm64.h`: bittest and 16-bit inc/dec intrinsics added.