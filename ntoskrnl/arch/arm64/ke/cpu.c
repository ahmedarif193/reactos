/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/cpu.c
 * PURPOSE:         CPU management stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

#define READ_SYSREG64(_var, _reg) __asm__ __volatile__("mrs %0, " #_reg : "=r"(_var))
#define WRITE_SYSREG64(_reg, _val) __asm__ __volatile__("msr " #_reg ", %0" :: "r"(_val))

#define READ_DBG_SLOT(_array, _slot, _name)                          \
    do                                                               \
    {                                                                \
        ULONGLONG _value;                                            \
        __asm__ __volatile__("mrs %0, " _name #_slot "_el1"         \
                             : "=r"(_value));                        \
        ProcessorState->SpecialRegisters._array[_slot] = _value;      \
    } while (0)

#define WRITE_DBG_SLOT(_array, _slot, _name)                         \
    do                                                               \
    {                                                                \
        ULONGLONG _value = ProcessorState->SpecialRegisters._array[_slot]; \
        __asm__ __volatile__("msr " _name #_slot "_el1, %0"         \
                             :: "r"(_value));                        \
    } while (0)

ULONG KeFixedTbEntries;
ULONG KiDmaIoCoherency;
ULONG KeIcacheFlushCount;
ULONG KeDcacheFlushCount;
ULONG KeLargestCacheLine = 64;
static LONG KiArm64IoBufferTraceCount;
extern PVOID MmPagedPoolStart;

/*
 * Number of hardware breakpoint and watchpoint registers.
 * Read from ID_AA64DFR0_EL1 during early init.
 * BRPs field (bits 15:12) gives breakpoints-1, WRPs field (bits 23:20) gives watchpoints-1.
 * Default to conservative values (6 breakpoints, 2 watchpoints) until initialized.
 */
ULONG KiArm64NumBreakpoints = 6;
ULONG KiArm64NumWatchpoints = 2;

/*
 * Initialize the debug register counts by reading ID_AA64DFR0_EL1.
 * Called early during CPU initialization.
 */
VOID
KiInitializeDebugRegisterCounts(VOID)
{
    ULONGLONG Dfr0;

    __asm__ __volatile__("mrs %0, id_aa64dfr0_el1" : "=r"(Dfr0));

    /* BRPs field (bits 15:12): number of breakpoints minus 1 */
    KiArm64NumBreakpoints = ((Dfr0 >> 12) & 0xF) + 1;

    /* WRPs field (bits 23:20): number of watchpoints minus 1 */
    KiArm64NumWatchpoints = ((Dfr0 >> 20) & 0xF) + 1;

    /* Clamp to maximum supported by our structures (8 BPs, 2 WPs) */
    if (KiArm64NumBreakpoints > 8)
        KiArm64NumBreakpoints = 8;
    if (KiArm64NumWatchpoints > 2)
        KiArm64NumWatchpoints = 2;
}

#if DBG
CODE_SEG("INIT")
VOID
KiReportCpuFeatures(IN PKPRCB Prcb)
{
    /* Debug logging removed */
    UNREFERENCED_PARAMETER(Prcb);
}
#endif

VOID
KiFlushSingleTb(_In_ BOOLEAN Invalid,
                _In_ PVOID VirtualAddress)
{
    ULONG_PTR Addr = (ULONG_PTR)VirtualAddress;
    BOOLEAN KernelAddress = (VirtualAddress >= MmSystemRangeStart);

    UNREFERENCED_PARAMETER(Invalid);

    /*
     * ARM64 TLB invalidation for a single virtual address.
     *
     * The ARM64 architecture uses a weakly-ordered memory model and requires
     * explicit synchronization barriers to ensure TLB invalidations are
     * properly ordered with respect to page table updates:
     *
     * 1. DSB ISH - Ensure all prior stores (PTE writes) are visible to
     *              all cores before the TLB invalidation
     * 2. TLBI     - Invalidate the TLB entry for this specific VA
     * 3. DSB ISH - Ensure the TLB invalidation completes before subsequent
     *              memory accesses
     * 4. ISB      - Synchronize instruction fetch (required for code pages)
     *
     * TLBI VAE1IS: TLB Invalidate by VA, EL1, Inner Shareable
     * - Invalidates TLB entries for the specified VA in EL1
     * - Inner Shareable ensures invalidation is broadcast to all cores
     *   in the inner shareable domain
     * - The VA must be shifted right by 12 bits (page aligned)
     *
     * Windows ARM64 Compliance: This matches the Windows kernel's approach
     * of using inner-shareable TLB operations for single-address invalidation.
     */

    /* Shift VA right by PAGE_SHIFT (12 bits) as required by TLBI instruction */
    Addr >>= PAGE_SHIFT;

    /* Ensure all prior PTE stores are visible before TLB invalidation */
    __asm__ __volatile__("dsb ish" ::: "memory");

    /*
     * Kernel mappings live in TTBR1/global space, so invalidate regardless of
     * current ASID. User mappings stay on the narrower current-ASID path.
     */
    if (KernelAddress)
    {
        __asm__ __volatile__("tlbi vaae1is, %0" :: "r"(Addr) : "memory");
    }
    else
    {
        __asm__ __volatile__("tlbi vae1is, %0" :: "r"(Addr) : "memory");
    }

    /* Ensure TLB invalidation completes before subsequent memory accesses */
    __asm__ __volatile__("dsb ish" ::: "memory");

    /* Synchronize instruction fetch (critical for executable pages) */
    __asm__ __volatile__("isb" ::: "memory");
}

VOID
KeFlushTb(VOID)
{
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    KeFlushTb();
}

VOID
FASTCALL
KeZeroPages(_Out_writes_bytes_(Size) PVOID Address,
            _In_ ULONG Size)
{
    RtlZeroMemory(Address, Size);
}

VOID
NTAPI
KiSaveProcessorControlState(_Out_ PKPROCESSOR_STATE ProcessorState)
{
    ULONGLONG Value;
    ULONG NumBps, NumWps;

    if (ProcessorState == NULL)
    {
        return;
    }

    READ_SYSREG64(ProcessorState->SpecialRegisters.Elr_El1, elr_el1);
    READ_SYSREG64(Value, spsr_el1);
    ProcessorState->SpecialRegisters.Spsr_El1 = (ULONG)Value;
    READ_SYSREG64(ProcessorState->SpecialRegisters.Tpidr_El0, tpidr_el0);
    READ_SYSREG64(ProcessorState->SpecialRegisters.Tpidrro_El0, tpidrro_el0);
    READ_SYSREG64(ProcessorState->SpecialRegisters.Tpidr_El1, tpidr_el1);

    /*
     * Save hardware breakpoint registers. Only access registers that exist
     * according to ID_AA64DFR0_EL1. Accessing non-existent registers causes
     * an undefined instruction exception on some implementations (e.g., QEMU).
     */
    NumBps = KiArm64NumBreakpoints;

    /* Zero out all slots first to ensure clean state */
    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelBvr,
                  sizeof(ProcessorState->SpecialRegisters.KernelBvr));
    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelBcr,
                  sizeof(ProcessorState->SpecialRegisters.KernelBcr));

    /* Save only the breakpoint registers that exist */
    if (NumBps > 0) { READ_DBG_SLOT(KernelBvr, 0, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 0, "dbgbcr"); }
    if (NumBps > 1) { READ_DBG_SLOT(KernelBvr, 1, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 1, "dbgbcr"); }
    if (NumBps > 2) { READ_DBG_SLOT(KernelBvr, 2, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 2, "dbgbcr"); }
    if (NumBps > 3) { READ_DBG_SLOT(KernelBvr, 3, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 3, "dbgbcr"); }
    if (NumBps > 4) { READ_DBG_SLOT(KernelBvr, 4, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 4, "dbgbcr"); }
    if (NumBps > 5) { READ_DBG_SLOT(KernelBvr, 5, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 5, "dbgbcr"); }
    if (NumBps > 6) { READ_DBG_SLOT(KernelBvr, 6, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 6, "dbgbcr"); }
    if (NumBps > 7) { READ_DBG_SLOT(KernelBvr, 7, "dbgbvr"); READ_DBG_SLOT(KernelBcr, 7, "dbgbcr"); }

    /*
     * Save hardware watchpoint registers. Same principle as breakpoints.
     */
    NumWps = KiArm64NumWatchpoints;

    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelWvr,
                  sizeof(ProcessorState->SpecialRegisters.KernelWvr));
    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelWcr,
                  sizeof(ProcessorState->SpecialRegisters.KernelWcr));

    if (NumWps > 0) { READ_DBG_SLOT(KernelWvr, 0, "dbgwvr"); READ_DBG_SLOT(KernelWcr, 0, "dbgwcr"); }
    if (NumWps > 1) { READ_DBG_SLOT(KernelWvr, 1, "dbgwvr"); READ_DBG_SLOT(KernelWcr, 1, "dbgwcr"); }

    READ_SYSREG64(ProcessorState->ArchState.Midr_El1, midr_el1);
    READ_SYSREG64(ProcessorState->ArchState.Sctlr_El1, sctlr_el1);
    READ_SYSREG64(ProcessorState->ArchState.Actlr_El1, actlr_el1);
    READ_SYSREG64(ProcessorState->ArchState.Cpacr_El1, cpacr_el1);
    READ_SYSREG64(ProcessorState->ArchState.Tcr_El1, tcr_el1);
    READ_SYSREG64(ProcessorState->ArchState.Ttbr0_El1, ttbr0_el1);
    READ_SYSREG64(ProcessorState->ArchState.Ttbr1_El1, ttbr1_el1);
    READ_SYSREG64(ProcessorState->ArchState.Esr_El1, esr_el1);
    READ_SYSREG64(ProcessorState->ArchState.Far_El1, far_el1);
    READ_SYSREG64(ProcessorState->ArchState.Pmcr_El0, pmcr_el0);
    READ_SYSREG64(ProcessorState->ArchState.Pmcntenset_El0, pmcntenset_el0);
    READ_SYSREG64(ProcessorState->ArchState.Pmccntr_El0, pmccntr_el0);
    READ_SYSREG64(ProcessorState->ArchState.Pmovsclr_El0, pmovsclr_el0);
    READ_SYSREG64(ProcessorState->ArchState.Pmselr_El0, pmselr_el0);
    READ_SYSREG64(ProcessorState->ArchState.Pmuserenr_El0, pmuserenr_el0);
    READ_SYSREG64(ProcessorState->ArchState.Mair_El1, mair_el1);
    READ_SYSREG64(ProcessorState->ArchState.Vbar_El1, vbar_el1);

    RtlZeroMemory(ProcessorState->ArchState.Pmxevcntr_El0,
                  sizeof(ProcessorState->ArchState.Pmxevcntr_El0));
    RtlZeroMemory(ProcessorState->ArchState.Pmxevtyper_El0,
                  sizeof(ProcessorState->ArchState.Pmxevtyper_El0));
}

VOID
NTAPI
KiRestoreProcessorControlState(_In_ PKPROCESSOR_STATE ProcessorState)
{
    ULONG NumBps, NumWps;

    if (ProcessorState == NULL)
    {
        return;
    }

    WRITE_SYSREG64(tpidr_el0, ProcessorState->SpecialRegisters.Tpidr_El0);
    WRITE_SYSREG64(tpidrro_el0, ProcessorState->SpecialRegisters.Tpidrro_El0);
    WRITE_SYSREG64(tpidr_el1, ProcessorState->SpecialRegisters.Tpidr_El1);

    /*
     * Restore hardware breakpoint registers. Only access registers that exist
     * according to ID_AA64DFR0_EL1.
     */
    NumBps = KiArm64NumBreakpoints;

    if (NumBps > 0) { WRITE_DBG_SLOT(KernelBvr, 0, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 0, "dbgbcr"); }
    if (NumBps > 1) { WRITE_DBG_SLOT(KernelBvr, 1, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 1, "dbgbcr"); }
    if (NumBps > 2) { WRITE_DBG_SLOT(KernelBvr, 2, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 2, "dbgbcr"); }
    if (NumBps > 3) { WRITE_DBG_SLOT(KernelBvr, 3, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 3, "dbgbcr"); }
    if (NumBps > 4) { WRITE_DBG_SLOT(KernelBvr, 4, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 4, "dbgbcr"); }
    if (NumBps > 5) { WRITE_DBG_SLOT(KernelBvr, 5, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 5, "dbgbcr"); }
    if (NumBps > 6) { WRITE_DBG_SLOT(KernelBvr, 6, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 6, "dbgbcr"); }
    if (NumBps > 7) { WRITE_DBG_SLOT(KernelBvr, 7, "dbgbvr"); WRITE_DBG_SLOT(KernelBcr, 7, "dbgbcr"); }

    /*
     * Restore hardware watchpoint registers.
     */
    NumWps = KiArm64NumWatchpoints;

    if (NumWps > 0) { WRITE_DBG_SLOT(KernelWvr, 0, "dbgwvr"); WRITE_DBG_SLOT(KernelWcr, 0, "dbgwcr"); }
    if (NumWps > 1) { WRITE_DBG_SLOT(KernelWvr, 1, "dbgwvr"); WRITE_DBG_SLOT(KernelWcr, 1, "dbgwcr"); }

    WRITE_SYSREG64(tcr_el1, ProcessorState->ArchState.Tcr_El1);
    WRITE_SYSREG64(ttbr0_el1, ProcessorState->ArchState.Ttbr0_El1);
    WRITE_SYSREG64(ttbr1_el1, ProcessorState->ArchState.Ttbr1_El1);
    WRITE_SYSREG64(mair_el1, ProcessorState->ArchState.Mair_El1);
    WRITE_SYSREG64(vbar_el1, ProcessorState->ArchState.Vbar_El1);
    WRITE_SYSREG64(sctlr_el1, ProcessorState->ArchState.Sctlr_El1);
    WRITE_SYSREG64(actlr_el1, ProcessorState->ArchState.Actlr_El1);
    WRITE_SYSREG64(cpacr_el1, ProcessorState->ArchState.Cpacr_El1);
    WRITE_SYSREG64(pmcr_el0, ProcessorState->ArchState.Pmcr_El0);
    WRITE_SYSREG64(pmcntenset_el0, ProcessorState->ArchState.Pmcntenset_El0);
    WRITE_SYSREG64(pmuserenr_el0, ProcessorState->ArchState.Pmuserenr_El0);
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    /*
     * ARM64 cache maintenance for coherency.
     *
     * Unlike x86/AMD64 which has hardware cache coherency, ARM64 requires
     * explicit cache maintenance operations. When mapping physical RAM
     * (e.g., ramdisk) with cached attributes, we must ensure data cache
     * coherency to prevent stale cache lines from being read.
     *
     * Sequence:
     * 1. DC CIVAC (Data Cache Clean and Invalidate by VA to PoC) - Not
     *    practical here as we don't have specific VAs, so we use set/way ops
     * 2. IC IALLU (Instruction Cache Invalidate All to PoU)
     * 3. DSB + ISB to ensure ordering
     *
     * For set/way operations, we perform a full D-cache clean and invalidate
     * operation to ensure all dirty data is written back and stale lines removed.
     * This is necessary when establishing new mappings to RAM (MmMapIoSpace with
     * MmCached) to prevent the cache manager from reading stale data.
     */
    __asm__ __volatile__("dsb ish" ::: "memory");

    /*
     * Clean and invalidate entire data cache to Point of Coherency.
     * This ensures that:
     * - All dirty cache lines are written back to RAM
     * - All cache lines are invalidated so subsequent reads fetch from RAM
     *
     * We use a simplified loop over cache levels. A production implementation
     * would read CLIDR_EL1 to determine actual cache levels and sizes.
     */
    {
        ULONG64 Clidr, Ccsidr, NumSets, NumWays, LineSize;
        ULONG Level, SetLoop, WayLoop, WayShift, SetShift;
        ULONG64 SetWayValue;

        /* Read Cache Level ID Register to get cache topology */
        __asm__ __volatile__("mrs %0, clidr_el1" : "=r"(Clidr));

        /* Clean and invalidate all data/unified caches (typically L1D, L2) */
        for (Level = 0; Level < 3; Level++)
        {
            /* Check if this level has a data or unified cache */
            ULONG CacheType = (Clidr >> (Level * 3)) & 0x7;
            if (CacheType < 2) /* 0=none, 1=I-cache only */
                break;

            /* Select the cache level in CSSELR_EL1 (Level:1, 0=data cache) */
            __asm__ __volatile__("msr csselr_el1, %0" :: "r"((ULONG64)(Level << 1)) : "memory");
            __asm__ __volatile__("isb" ::: "memory");

            /* Read Current Cache Size ID Register */
            __asm__ __volatile__("mrs %0, ccsidr_el1" : "=r"(Ccsidr));

            /* Extract cache geometry: LineSize = 2^(LineSize+4) bytes */
            LineSize = (Ccsidr & 0x7) + 4;
            NumWays = ((Ccsidr >> 3) & 0x3FF) + 1;
            NumSets = ((Ccsidr >> 13) & 0x7FFF) + 1;

            /* Calculate bit positions for set/way operations */
            WayShift = __builtin_clz((unsigned int)NumWays - 1);
            SetShift = LineSize;

            /* Clean and invalidate all sets and ways */
            for (WayLoop = 0; WayLoop < NumWays; WayLoop++)
            {
                for (SetLoop = 0; SetLoop < NumSets; SetLoop++)
                {
                    SetWayValue = (((ULONG64)Level) << 1) |
                                  (((ULONG64)SetLoop) << SetShift) |
                                  (((ULONG64)WayLoop) << WayShift);
                    __asm__ __volatile__("dc cisw, %0" :: "r"(SetWayValue) : "memory");
                }
            }
        }
    }

    __asm__ __volatile__("dsb ish" ::: "memory");

    /* Invalidate instruction cache */
    __asm__ __volatile__("ic iallu" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    return TRUE;
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    return KeLargestCacheLine;
}

VOID
NTAPI
KeFlushEntireTb(_In_ BOOLEAN Invalid,
                _In_ BOOLEAN AllProcessors)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Invalid);
    UNREFERENCED_PARAMETER(AllProcessors);

    /* Serialize with other TLB modifications. */
    OldIrql = KeRaiseIrqlToSynchLevel();

    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    KeLowerIrql(OldIrql);
}

VOID
NTAPI
KeSetDmaIoCoherency(_In_ ULONG Coherency)
{
    KiDmaIoCoherency = Coherency;
}

VOID
__cdecl
KeSaveStateForHibernate(_In_ PKPROCESSOR_STATE State)
{
    UNREFERENCED_PARAMETER(State);
    ARM64_STUB();
}

VOID
NTAPI
KiSaveProcessorState(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb;
    PCONTEXT Context;

    Prcb = KeGetCurrentPrcb();
    if ((Prcb == NULL) || (TrapFrame == NULL))
    {
        return;
    }

    Context = &Prcb->ProcessorState.ContextFrame;
    Context->ContextFlags = CONTEXT_FULL |
                             CONTEXT_FLOATING_POINT |
                             CONTEXT_DEBUG_REGISTERS |
                             CONTEXT_ARM64 |
                             CONTEXT_X18;

    KeTrapFrameToContext(TrapFrame, ExceptionFrame, Context);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
}

VOID
NTAPI
KiRestoreProcessorState(
    _Out_ PKTRAP_FRAME TrapFrame,
    _Out_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb;
    KPROCESSOR_MODE PreviousMode;

    Prcb = KeGetCurrentPrcb();
    if ((Prcb == NULL) || (TrapFrame == NULL))
    {
        return;
    }

    PreviousMode = (TrapFrame->PreviousMode == UserMode) ? UserMode : KernelMode;

    KeContextToTrapFrame(&Prcb->ProcessorState.ContextFrame,
                         ExceptionFrame,
                         TrapFrame,
                         CONTEXT_FULL |
                         CONTEXT_FLOATING_POINT |
                         CONTEXT_DEBUG_REGISTERS |
                         CONTEXT_ARM64 |
                         CONTEXT_X18,
                         PreviousMode);

    KiRestoreProcessorControlState(&Prcb->ProcessorState);
}

/*
 * @implemented
 * KeFlushIoBuffers - Flush CPU data caches for DMA correctness on ARM64.
 *
 * ARM64 uses a weakly-ordered memory model and does NOT have cache-coherent DMA
 * by default (unlike x86/AMD64). This means that when a DMA device reads or writes
 * memory, the CPU caches may contain stale data or unflushed writes.
 *
 * For DMA correctness, we must perform explicit cache maintenance:
 *
 * - ReadOperation == TRUE (device-to-memory DMA, e.g., disk read):
 *   The device will write data directly to RAM. We must INVALIDATE the CPU's
 *   data cache lines covering this memory region so subsequent CPU reads fetch
 *   the fresh data from RAM rather than stale cached data. We use DC CIVAC
 *   (Clean and Invalidate by VA to Point of Coherency) which ensures any dirty
 *   cache data is written back before invalidation.
 *
 * - ReadOperation == FALSE (memory-to-device DMA, e.g., disk write):
 *   The CPU has written data that the device will read. We must CLEAN (flush)
 *   the CPU's data cache to ensure all modified cache lines are written back
 *   to RAM so the device sees the correct data. We use DC CVAC (Clean by VA
 *   to Point of Coherency).
 *
 * The DmaOperation parameter indicates whether this is for an actual DMA transfer.
 * If KiDmaIoCoherency is set, the hardware provides DMA coherency and we can
 * skip cache maintenance for DMA operations.
 *
 * Cache line alignment is critical: ARM64 cache maintenance operates on cache
 * lines (typically 64 bytes). We must align to cache line boundaries to avoid
 * corrupting adjacent data.
 *
 * Memory barriers (DSB ISH) ensure cache operations complete before returning.
 */
VOID
NTAPI
KeFlushIoBuffers(_Inout_ PMDL Mdl,
                 _In_ BOOLEAN ReadOperation,
                 _In_ BOOLEAN DmaOperation)
{
    PVOID VirtualAddress;
    PVOID OriginalAddress;
    ULONG Length;
    ULONG_PTR StartAddress;
    ULONG_PTR EndAddress;
    ULONG_PTR CacheAddress;
    ULONG CacheLineSize;
    BOOLEAN MappedByUs = FALSE;
    BOOLEAN TraceTarget = FALSE;
    BOOLEAN UsePfnAlias = FALSE;
    PVOID TracePage = NULL;
    PPFN_NUMBER MdlPages = NULL;
    ULONG PageCount = 0;
    ULONG ByteOffset;
    ULONG RemainingLength;
    ULONG PageIndex;

    /*
     * If the MDL is NULL or has zero byte count, there is nothing to flush.
     */
    if (Mdl == NULL || Mdl->ByteCount == 0)
    {
        return;
    }

    /*
     * For DMA operations, check if the hardware provides DMA coherency.
     * If KiDmaIoCoherency is non-zero, the system has cache-coherent DMA
     * and explicit cache maintenance is not needed.
     */
    if (DmaOperation && KiDmaIoCoherency)
    {
        return;
    }

    OriginalAddress = MmGetMdlVirtualAddress(Mdl);
    Length = Mdl->ByteCount;
    ByteOffset = MmGetMdlByteOffset(Mdl);

    if ((OriginalAddress != NULL) &&
        (MmPagedPoolStart != NULL))
    {
        ULONG_PTR TraceVa = (ULONG_PTR)MmPagedPoolStart + 0xB5000;
        ULONG_PTR TraceStart = (ULONG_PTR)PAGE_ALIGN(OriginalAddress);
        ULONG_PTR TraceEnd = (ULONG_PTR)PAGE_ALIGN((PVOID)((ULONG_PTR)OriginalAddress +
                                                           Length - 1));
        TraceTarget = ((TraceStart <= TraceVa) && (TraceVa <= TraceEnd));
        if (TraceTarget)
        {
            TracePage = (PVOID)TraceVa;
        }
    }

    CacheLineSize = KeLargestCacheLine;
    if (CacheLineSize == 0)
    {
        CacheLineSize = 64;  /* Default ARM64 cache line size */
    }

    /*
     * Prefer the stable PFN/KSEG0 alias for RAM-backed MDLs. This avoids cache
     * maintenance through transient user or pageable kernel virtual aliases,
     * which has already produced deferred SError failures on ARM64 bring-up.
     */
    if (((Mdl->MdlFlags & MDL_IO_SPACE) == 0) && (OriginalAddress != NULL))
    {
        MdlPages = MmGetMdlPfnArray(Mdl);
        PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(OriginalAddress, Length);
        UsePfnAlias = (MdlPages != NULL) && (PageCount != 0);
    }

    if (UsePfnAlias)
    {
        if (TraceTarget)
        {
            LONG TraceIndex = InterlockedIncrement(&KiArm64IoBufferTraceCount);
            if (TraceIndex <= 32)
            {
                DPRINT1("[arm64][IOFLUSH] mdl[%ld] va=%p len=0x%lx read=%u dma=%u flags=0x%x alias=kseg0 target=%p firstPfn=%Ix\n",
                        TraceIndex,
                        OriginalAddress,
                        Length,
                        ReadOperation,
                        DmaOperation,
                        Mdl->MdlFlags,
                        TracePage,
                        MdlPages[0]);
            }
        }

        if (ReadOperation)
        {
            __asm__ __volatile__("dsb ish" ::: "memory");
        }

        RemainingLength = Length;
        for (PageIndex = 0; PageIndex < PageCount; PageIndex++)
        {
            ULONG ChunkLength;
            ULONG_PTR AliasVa;

            ChunkLength = min(RemainingLength, (ULONG)(PAGE_SIZE - ByteOffset));
            AliasVa = MI_ARM64_PFN_TO_VA(MdlPages[PageIndex]);
            StartAddress = (AliasVa + ByteOffset) & ~((ULONG_PTR)CacheLineSize - 1);
            EndAddress = (AliasVa + ByteOffset + ChunkLength + CacheLineSize - 1) &
                         ~((ULONG_PTR)CacheLineSize - 1);

            for (CacheAddress = StartAddress;
                 CacheAddress < EndAddress;
                 CacheAddress += CacheLineSize)
            {
                if (ReadOperation)
                {
                    __asm__ __volatile__("dc civac, %0" :: "r"(CacheAddress) : "memory");
                }
                else
                {
                    __asm__ __volatile__("dc cvac, %0" :: "r"(CacheAddress) : "memory");
                }
            }

            RemainingLength -= ChunkLength;
            ByteOffset = 0;
        }

        __asm__ __volatile__("dsb ish" ::: "memory");
        return;
    }

    /*
     * Fall back to the original system-VA path for MDL_IO_SPACE or other
     * mappings that do not carry a normal RAM PFN array.
     */
    if (Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL))
    {
        VirtualAddress = Mdl->MappedSystemVa;
    }
    else
    {
        VirtualAddress = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (VirtualAddress == NULL)
        {
            DPRINT1("KeFlushIoBuffers: MDL %p not mapped and map failed, skipping cache maintenance (ReadOperation=%d DmaOperation=%d)\n",
                    Mdl, ReadOperation, DmaOperation);
            return;
        }
        MappedByUs = TRUE;
    }

    if (VirtualAddress == NULL)
    {
        return;
    }

    if (TraceTarget)
    {
        LONG TraceIndex = InterlockedIncrement(&KiArm64IoBufferTraceCount);
        if (TraceIndex <= 32)
        {
            DPRINT1("[arm64][IOFLUSH] mdl[%ld] va=%p len=0x%lx read=%u dma=%u flags=0x%x alias=va target=%p mapped=%p\n",
                    TraceIndex,
                    OriginalAddress,
                    Length,
                    ReadOperation,
                    DmaOperation,
                    Mdl->MdlFlags,
                    TracePage,
                    VirtualAddress);
        }
    }

    StartAddress = (ULONG_PTR)VirtualAddress & ~((ULONG_PTR)CacheLineSize - 1);
    EndAddress = ((ULONG_PTR)VirtualAddress + Length + CacheLineSize - 1) &
                 ~((ULONG_PTR)CacheLineSize - 1);

    if (ReadOperation)
    {
        __asm__ __volatile__("dsb ish" ::: "memory");

        for (CacheAddress = StartAddress; CacheAddress < EndAddress;
             CacheAddress += CacheLineSize)
        {
            __asm__ __volatile__("dc civac, %0" :: "r"(CacheAddress) : "memory");
        }

        __asm__ __volatile__("dsb ish" ::: "memory");
    }
    else
    {
        for (CacheAddress = StartAddress; CacheAddress < EndAddress;
             CacheAddress += CacheLineSize)
        {
            __asm__ __volatile__("dc cvac, %0" :: "r"(CacheAddress) : "memory");
        }

        __asm__ __volatile__("dsb ish" ::: "memory");
    }

    /*
     * If we mapped the MDL ourselves, unmap it now to leave the MDL in the
     * same state we found it. This prevents leaving the MDL mapped when the
     * caller expects it to be unmapped (e.g., before IoFreeMdl).
     */
    if (MappedByUs && (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
    {
        MmUnmapLockedPages(VirtualAddress, Mdl);
    }
}

NTSTATUS
NTAPI
NtVdmControl(_In_ ULONG ControlCode,
             _Inout_ PVOID ControlData)
{
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(ControlData);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetLdtEntries(_In_ ULONG Selector1,
                _In_ LDT_ENTRY LdtEntry1,
                _In_ ULONG Selector2,
                _In_ LDT_ENTRY LdtEntry2)
{
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}
