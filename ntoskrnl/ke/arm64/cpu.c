/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/cpu.c
 * PURPOSE:         CPU management stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

#define READ_SYSREG64(_var, _reg) __asm__ __volatile__("mrs %0, " #_reg : "=r"(_var))
#define WRITE_SYSREG64(_reg, _val) __asm__ __volatile__("msr " #_reg ", %0" :: "r"(_val))

#define ARM64_MDSCR_SS             (1ULL << 0)
#define ARM64_MDSCR_KDE            (1ULL << 13)
#define ARM64_MDSCR_MDE            (1ULL << 15)
#define ARM64_PSTATE_SS            (1ULL << 21)
#define ARM64_DEBUG_CONTROL_ENABLE 1UL

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
extern PVOID MmPagedPoolStart;

#define ARM64_MAX_CACHE_LEVELS 7

typedef struct _ARM64_CACHE_SWEEP_LEVEL
{
    ULONG Level;
    ULONG NumSets;
    ULONG NumWays;
    ULONG SetShift;
    ULONG WayShift;
} ARM64_CACHE_SWEEP_LEVEL, *PARM64_CACHE_SWEEP_LEVEL;

static ARM64_CACHE_SWEEP_LEVEL KiArm64DcacheSweepLevels[ARM64_MAX_CACHE_LEVELS];
static ULONG KiArm64DcacheSweepLevelCount;
static volatile LONG KiArm64DcacheSweepGeometryValid;

VOID
NTAPI
KiArm64ReadCcsidr(
    _In_ ULONG LevelIndex,
    _In_ BOOLEAN InstructionSide,
    _Out_ PULONG LineShift,
    _Out_ PULONG Ways,
    _Out_ PULONG Sets)
{
    ULONG64 Csselr = ((ULONG64)LevelIndex << 1) | (InstructionSide ? 1 : 0);
    ULONG64 Ccsidr;

    /* Select the cache; the ISB orders the CSSELR_EL1 write before the
       CCSIDR_EL1 read */
    __asm__ __volatile__("msr csselr_el1, %0" :: "r"(Csselr) : "memory");
    __asm__ __volatile__("isb" ::: "memory");
    __asm__ __volatile__("mrs %0, ccsidr_el1" : "=r"(Ccsidr));

    *LineShift = (ULONG)(Ccsidr & 0x7) + 4;
    *Ways = (ULONG)((Ccsidr >> 3) & 0x3FF) + 1;
    *Sets = (ULONG)((Ccsidr >> 13) & 0x7FFF) + 1;
}

static
VOID
KiArm64EnsureDcacheSweepGeometry(VOID)
{
    ARM64_CACHE_SWEEP_LEVEL LocalLevels[ARM64_MAX_CACHE_LEVELS];
    ULONG LocalCount = 0;
    ULONG LargestLine = 64;
    ULONG64 Clidr;
    ULONG Level;

    if (KiArm64DcacheSweepGeometryValid)
    {
        __asm__ __volatile__("dmb ish" ::: "memory");
        return;
    }

    __asm__ __volatile__("mrs %0, clidr_el1" : "=r"(Clidr));

    for (Level = 0; Level < ARM64_MAX_CACHE_LEVELS; Level++)
    {
        ULONG CacheType = (Clidr >> (Level * 3)) & 0x7;
        ULONG LineShift;
        ULONG NumWays;
        ULONG NumSets;

        if (CacheType < 2)
        {
            continue;
        }

        KiArm64ReadCcsidr(Level, FALSE, &LineShift, &NumWays, &NumSets);

        LocalLevels[LocalCount].Level = Level;
        LocalLevels[LocalCount].NumSets = NumSets;
        LocalLevels[LocalCount].NumWays = NumWays;
        LocalLevels[LocalCount].SetShift = LineShift;
        LocalLevels[LocalCount].WayShift =
            (NumWays <= 1) ? 32 : (ULONG)__builtin_clz(NumWays - 1);
        LocalCount++;

        if ((1UL << LineShift) > LargestLine)
        {
            LargestLine = 1UL << LineShift;
        }
    }

    RtlCopyMemory(KiArm64DcacheSweepLevels,
                  LocalLevels,
                  LocalCount * sizeof(LocalLevels[0]));
    KiArm64DcacheSweepLevelCount = LocalCount;
    KeLargestCacheLine = LargestLine;
    __asm__ __volatile__("dmb ish" ::: "memory");
    KiArm64DcacheSweepGeometryValid = 1;
}

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

static
BOOLEAN
KiArm64DebugRegistersEnabled(
    _In_ const KSPECIAL_REGISTERS* Registers,
    _In_ ULONG BreakpointCount,
    _In_ ULONG WatchpointCount)
{
    ULONG Slot;

    for (Slot = 0; Slot < BreakpointCount; Slot++)
    {
        if (Registers->KernelBcr[Slot] & ARM64_DEBUG_CONTROL_ENABLE)
            return TRUE;
    }

    for (Slot = 0; Slot < WatchpointCount; Slot++)
    {
        if (Registers->KernelWcr[Slot] & ARM64_DEBUG_CONTROL_ENABLE)
            return TRUE;
    }

    return FALSE;
}

VOID
NTAPI
KiRestoreProcessorControlState(_In_ PKPROCESSOR_STATE ProcessorState)
{
    ULONGLONG Mdscr;
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

    /*
     * The debug registers above only take effect once MDSCR_EL1 enables monitor
     * debug events (MDE) and debug exceptions at EL1 (KDE). Preserve unrelated
     * MDSCR state and derive the bits owned here from the state being restored.
     *
     * ContextFrame is authoritative for exception return: a debugger can modify
     * its CPSR after KiSaveProcessorControlState captured Spsr_El1, and
     * KiTrapReturn later copies that CPSR to SPSR_EL1.
     */
    READ_SYSREG64(Mdscr, mdscr_el1);
    Mdscr &= ~(ARM64_MDSCR_SS | ARM64_MDSCR_KDE | ARM64_MDSCR_MDE);

    if (KiArm64DebugRegistersEnabled(&ProcessorState->SpecialRegisters,
                                     NumBps,
                                     NumWps))
    {
        Mdscr |= ARM64_MDSCR_MDE | ARM64_MDSCR_KDE;
    }

    if (ProcessorState->ContextFrame.Cpsr & ARM64_PSTATE_SS)
        Mdscr |= ARM64_MDSCR_SS | ARM64_MDSCR_KDE;

    WRITE_SYSREG64(mdscr_el1, Mdscr);
    /* Synchronize the new debug state before returning from the exception. */
    __asm__ __volatile__("isb");

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

    KiArm64EnsureDcacheSweepGeometry();

    for (ULONG LevelIndex = 0; LevelIndex < KiArm64DcacheSweepLevelCount; LevelIndex++)
    {
        PARM64_CACHE_SWEEP_LEVEL Geometry = &KiArm64DcacheSweepLevels[LevelIndex];

        for (ULONG WayLoop = 0; WayLoop < Geometry->NumWays; WayLoop++)
        {
            for (ULONG SetLoop = 0; SetLoop < Geometry->NumSets; SetLoop++)
            {
                ULONG64 SetWayValue = (((ULONG64)Geometry->Level) << 1) |
                                      (((ULONG64)SetLoop) << Geometry->SetShift) |
                                      (((ULONG64)WayLoop) << Geometry->WayShift);
                __asm__ __volatile__("dc cisw, %0" :: "r"(SetWayValue) : "memory");
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

    /* Serialize with other TLB modifications. */
    OldIrql = KeRaiseIrqlToSynchLevel();

    __asm__ __volatile__("dsb ish" ::: "memory");
    if (AllProcessors)
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    else
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
    BOOLEAN UsePfnAlias = FALSE;
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

/*
 * Windows ARM64 treats the base FP/NEON register bank as part of the normal
 * kernel ABI. The WDK exposes KFLOATING_SAVE as an opaque dummy and inlines
 * these operations as successful no-ops. Keep real exports for precompiled
 * drivers that import the routines instead of using the inline definitions.
 */
NTSTATUS
NTAPI
KxSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatingState)
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KxRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatingState)
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
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
