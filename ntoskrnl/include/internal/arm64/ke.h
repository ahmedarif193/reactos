/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#endif

#include <ndk/arm64/ketypes.h>
#include "intrin_i.h"

/* Local KSEG0 base for physical-to-virtual mapping in inline functions.
 * Avoids dependency on mm.h include order. Same value as KSEG0_BASE in mm.h. */
#ifndef KE_ARM64_KSEG0_BASE
#define KE_ARM64_KSEG0_BASE 0xFFFF800000000000ULL
#endif

#define KE_ARM64_TTBR1_L0_OFFSET 0x800ULL

#define ARM64_SYNC_BARRIER() do { __dmb(_ARM64_BARRIER_SY); __isb(_ARM64_BARRIER_SY); } while (0)

#define ARM64_PSTATE_ASYNC_ABORT_MASK 0x100UL
#define ARM64_PSTATE_IRQ_MASK         0x080UL

#ifndef KI_MAX_NUMA_NODES
#define KI_MAX_NUMA_NODES MAXIMUM_PROCESSORS
#endif

typedef struct _KSWITCHFRAME
{
    ULONG64 Dummy;
} KSWITCHFRAME, *PKSWITCHFRAME;

extern NTKERNELAPI PVOID MmSystemRangeStart;
extern NTKERNELAPI PVOID MmHighestUserAddress;

#define KI_ARM64_NUMA_RANGE_VERSION 1

typedef struct _KI_ARM64_NUMA_NODE_RANGE
{
    ULONGLONG BaseAddress;
    ULONGLONG LimitAddress;
    ULONGLONG Length;
    ULONG ProximityDomain;
    BOOLEAN Enabled;
} KI_ARM64_NUMA_NODE_RANGE, *PKI_ARM64_NUMA_NODE_RANGE;

extern KI_ARM64_NUMA_NODE_RANGE KiArm64NumaNodeRanges[KI_MAX_NUMA_NODES];
extern UCHAR KiArm64NumaNodeCount;

VOID
NTAPI
KiArm64DiscoverNumaTopology(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
KiArm64InitializeProcessorNumaTopology(
    _In_ ULONG ProcessorNumber,
    _Inout_ PKPRCB Prcb,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
NTAPI
KiArm64FinalizeNumaTopology(VOID);

VOID
NTAPI
KiArm64FinalizeSmtTopology(VOID);

#define SYNCH_LEVEL 12

#define KD_BREAKPOINT_TYPE        ULONG
#define KD_BREAKPOINT_SIZE        sizeof(ULONG)
#define KD_BREAKPOINT_VALUE       0xD43E0000
#define MM_SYSTEM_RANGE_START         MmSystemRangeStart

// Interrupt state helper. The DAIF.I bit (bit 7) mirrors the PSR interrupt
// mask, but for now treat trap frames as having interrupts disabled until the
// real trap exit code is in place.
#define KeGetTrapFrameInterruptState(TrapFrame) 0
#define KeGetContextSwitches(Prcb)  ((Prcb)->KeContextSwitches)

// HAL DMA entry points are not declared by MinGW for arm64. Mirror the
// Windows kernel prototypes so the I/O manager can call into the HAL.
NTHALAPI
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine);

FORCEINLINE
VOID
KeInvalidateTlbEntry(
    _In_ PVOID Address)
{
    ULONG_PTR Va = (ULONG_PTR)Address >> PAGE_SHIFT;

    /*
     * ARM64 single-address invalidation must evict translations regardless of
     * the current ASID because the kernel uses global TTBR1 mappings and the
     * bring-up path still runs with incomplete ASID discipline in some flows.
     *
     * Use VAAE1IS here instead of VAE1IS so a resolved kernel fault does not
     * keep re-hitting a stale translation-fault entry cached against another
     * ASID/global context. This keeps the operation targeted to one VA while
     * avoiding the broad vmalle1is workaround.
     */
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaae1is, %0" :: "r"(Va) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
KeFlushProcessTb(VOID)
{
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

/*
 * Conservative upper bound for the kernel ntdll mapping size.
 * Typical ntdll is well under 4MB; 16MB is generous headroom.
 * Used until PspSystemDllSize (or SizeOfImage from the PE header) is
 * properly exported and cached.
 */
#define KI_MAX_NTDLL_KERNEL_MAPPING_SIZE (16ULL * 1024 * 1024)

/*
 * Convert a kernel-mapped ntdll address to the equivalent user-space address.
 * Returns NULL if KernelAddress is NULL, not within the kernel ntdll mapping,
 * or exceeds the conservative size bound.
 *
 * All ARM64 files that need kernel-to-user ntdll address translation must use
 * this single implementation rather than defining local copies.
 */
static inline PVOID
KiConvertSystemDllAddressToUser(
    _In_opt_ PVOID KernelAddress,
    _In_ PEPROCESS Process)
{
    extern PVOID PspSystemDllBase;
    ULONG_PTR Addr;
    ULONG_PTR Base;
    ULONG_PTR Offset;

    if (!KernelAddress) return NULL;
    if (!PspSystemDllBase) return NULL;

    Addr = (ULONG_PTR)KernelAddress;
    Base = (ULONG_PTR)PspSystemDllBase;

    /* Validate the address is within the kernel ntdll mapping */
    if (Addr < Base) return NULL;

    Offset = Addr - Base;

    /* Upper-bound check: reject addresses past the expected ntdll image end */
    if (Offset >= KI_MAX_NTDLL_KERNEL_MAPPING_SIZE) return NULL;

    return (PVOID)((ULONG_PTR)PspSystemDllBase + Offset);
}

/*
 * ARM64 ASID encoding inside KPROCESS DirectoryTableBase[0] (== TTBR0_EL1).
 *
 *   [63:56] ignored (8-bit ASID, TCR.AS=0)   [55:48] ASID   [47:12] table base
 *
 * KI_ARM64_TTBR_ADDR_MASK isolates the table base; KI_ARM64_TTBR_ASID_MASK the
 * 8-bit ASID. The ASID allocator (ke/arm64/trapc.c, gated by KiArm64AsidEnabled,
 * default off) writes the ASID into DirectoryTableBase[0]; with the default
 * ASID == 0 every switch broadcast-flushes exactly as the original bring-up code
 * did, so the encoding is behaviour-neutral until the allocator is enabled.
 *
 * Note: enabling targeted (flush-free) switching requires user leaf PTEs to be
 * non-global (nG=1) and TCR.A1=0 so TTBR0.ASID is the current ASID.
 */
#define KI_ARM64_TTBR_ADDR_MASK   0x0000FFFFFFFFF000ULL
#define KI_ARM64_TTBR_ASID_SHIFT  48
#define KI_ARM64_TTBR_ASID_MASK   (0xFFULL << KI_ARM64_TTBR_ASID_SHIFT)

FORCEINLINE
ULONGLONG
KiArm64KernelTtbrBase(
    _In_ ULONGLONG UserDirectoryBase)
{
    ULONGLONG RootBase = UserDirectoryBase & KI_ARM64_TTBR_ADDR_MASK;
    ULONGLONG TcrEl1;
    ULONGLONG T1sz;

    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(TcrEl1));
    T1sz = (TcrEl1 >> 16) & 0x3FULL;
    return (T1sz == 17) ? RootBase + KE_ARM64_TTBR1_L0_OFFSET : RootBase;
}

FORCEINLINE
VOID
KiArm64WriteUserTtbr(
    _In_ ULONGLONG UserDirectoryBase)
{
    /*
     * ARM64 ASID-aware process switch.
     *
     * When the ASID allocator is disabled (default), DirectoryTableBase[0] has
     * ASID == 0: we write the bare table base and broadcast-flush the whole TLB,
     * exactly as the original single-ASID bring-up code did.
     *
     * When the allocator has tagged this process with a non-zero ASID, we write
     * the ASID into TTBR0 and skip the flush entirely: the incoming process's
     * translations are cached under its own ASID and the outgoing process's stay
     * cached under theirs, so no eviction is needed on the switch. (ASIDs are
     * recycled by degrading to ASID 0, never by reuse, so a freshly assigned
     * ASID can never alias stale entries.)
     */
    ULONGLONG RootBase = UserDirectoryBase & KI_ARM64_TTBR_ADDR_MASK;
    ULONGLONG Asid = UserDirectoryBase & KI_ARM64_TTBR_ASID_MASK;
    ULONGLONG KernelRootBase = KiArm64KernelTtbrBase(UserDirectoryBase);
    ULONGLONG SavedDaif;

    __asm__ __volatile__("mrs %0, daif" : "=r"(SavedDaif));
    __asm__ __volatile__("msr daifset, #0xF" ::: "memory");

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(RootBase | Asid) : "memory");
    __asm__ __volatile__("isb" ::: "memory");
    __asm__ __volatile__("msr ttbr1_el1, %0" :: "r"(KernelRootBase) : "memory");
    __asm__ __volatile__("isb" ::: "memory");

    if (Asid == 0)
    {
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");
    }

    __asm__ __volatile__("msr daif, %0" :: "r"(SavedDaif) : "memory");
}

FORCEINLINE
VOID
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    ULONG64 Ctr;
    ULONG DLine, ILine;
    ULONG_PTR Start, End, Addr;

    if (!BaseAddress || FlushSize == 0)
    {
        /* ic ialluis broadcasts to all CPUs in inner shareable domain (SMP-safe) */
        __asm__ __volatile__("ic ialluis\n\tdsb ish\n\tisb" ::: "memory");
        return;
    }

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DLine = 4u << ((Ctr >> 16) & 0xF);
    ILine = 4u << (Ctr & 0xF);

    /* Clean D-cache to PoU for the modified range before invalidating I-cache. */
    Start = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(DLine - 1);
    End = (ULONG_PTR)BaseAddress + FlushSize;
    for (Addr = Start; Addr < End; Addr += DLine)
    {
        /*
         * TODO: Revisit once ARM64 user/kernel executable mappings are proven
         * alias-safe. CIVAC is stronger than CVAU and can hurt performance for
         * large or frequent code-publication ranges because it also invalidates
         * the data-cache line and pushes maintenance to the point of coherency.
         */
        __asm__ __volatile__("dc civac, %0" :: "r"(Addr) : "memory");
    }
    __asm__ __volatile__("dsb ish" ::: "memory");

    Start = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(ILine - 1);
    for (Addr = Start; Addr < End; Addr += ILine)
    {
        __asm__ __volatile__("ic ivau, %0" :: "r"(Addr) : "memory");
    }
    __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
}

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID);

ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber);

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG_PTR Flags;

    __asm__ __volatile__("mrs %0, daif" : "=r"(Flags));
    __asm__ __volatile__("msr daifset, #2" ::: "memory");

    return ((Flags & ARM64_PSTATE_IRQ_MASK) == 0);
}

FORCEINLINE
VOID
KeRestoreInterrupts(
    _In_ BOOLEAN WereEnabled)
{
    if (WereEnabled)
    {
        __asm__ __volatile__("msr daifclr, #2" ::: "memory");
    }
}

FORCEINLINE
VOID
KiRundownThread(
    _In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
}

FORCEINLINE
ULONG_PTR
KeGetContextPc(
    _In_ PCONTEXT Context)
{
    return Context->Pc;
}

FORCEINLINE
VOID
KeSetContextPc(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR ProgramCounter)
{
    Context->Pc = ProgramCounter;
}

FORCEINLINE
ULONG_PTR
KeGetContextReturnRegister(
    _In_ PCONTEXT Context)
{
    return Context->X0;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR ReturnValue)
{
    Context->X0 = ReturnValue;
}

FORCEINLINE
ULONG_PTR
KeGetContextStackRegister(
    _In_ PCONTEXT Context)
{
    return Context->Sp;
}

FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(
    _In_ PCONTEXT Context)
{
    return Context->Fp;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(
    _Inout_ PCONTEXT Context,
    _In_ ULONG_PTR Frame)
{
    Context->Fp = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFramePc(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Pc;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Sp;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameFrameRegister(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Fp;
}

FORCEINLINE
PULONG_PTR
KiGetUserModeStackAddress(void)
{
    return &PsGetCurrentThread()->Tcb.TrapFrame->Sp;
}

FORCEINLINE
PKTRAP_FRAME
KiGetLinkedTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return (PKTRAP_FRAME)(TrapFrame->TrapFrame);
}

#define KeGetTrapFrame(Thread) ((PKTRAP_FRAME)((Thread)->TrapFrame))

/*
 * KeGetExceptionFrame - Get the exception frame for a thread
 *
 * On ARM64, the user thread stack layout (KUINIT_FRAME) is:
 *   KSWITCH_FRAME    <- lowest address (Thread->KernelStack)
 *   KSTART_FRAME
 *   KEXCEPTION_FRAME <- exception frame is BEFORE trap frame
 *   KTRAP_FRAME      <- highest address (Thread->TrapFrame)
 *
 * So the exception frame is at (TrapFrame - sizeof(KEXCEPTION_FRAME)).
 * This matches AMD64 and ARM32 behavior.
 */
#define KeGetExceptionFrame(Thread) \
    ((PKEXCEPTION_FRAME)((ULONG_PTR)KeGetTrapFrame(Thread) - sizeof(KEXCEPTION_FRAME)))

#define KiGetPreviousMode(TrapFrame) \
    (((TrapFrame)->Spsr & 0xF) == 0 ? UserMode : KernelMode)

VOID
KeFlushTb(VOID);

VOID
HalSweepDcache(VOID);

VOID
HalSweepIcache(VOID);

/* ARM64 GIC Priority Masking for IRQL Management */
VOID
FASTCALL
HalSetGicPriorityMask(
    _In_ KIRQL Irql);

VOID
FASTCALL
HalRaiseGicPriorityMask(
    _In_ KIRQL Irql);

VOID
KiSetCurrentIrql(
    _In_ KIRQL Irql);

VOID
NTAPI
KeReenableTimerInterrupt(
    VOID);

VOID
NTAPI
KeStartArm64ProcessorTimer(
    VOID);

ULONG
FASTCALL
HalGetGicPriorityMask(VOID);

/* Final exception/interrupt readiness flags (for bring-up diagnostics) */
extern BOOLEAN KiArm64FinalVectorsInstalled;
extern BOOLEAN KiArm64SvcConfigured;
extern BOOLEAN KiArm64IrqFiqConfigured;

/* Debug register counts from ID_AA64DFR0_EL1 */
extern ULONG KiArm64NumBreakpoints;
extern ULONG KiArm64NumWatchpoints;

/*
 * CPU feature flags populated from ID_AA64* register reads during
 * KiInitSystem (BSP path). Used by MM, fault handler, and HAL code
 * to avoid re-reading system registers or pulling single nibbles out
 * of 64-bit values at every query site.
 */
typedef struct _ARM64_CPU_FEATURES
{
    ULONG HafdbsSupported:4;    /* ID_AA64MMFR1_EL1[3:0]  0=none 1=HA 2=HA+HD  */
    ULONG PanSupported:1;       /* ID_AA64MMFR1_EL1[23:20] PAN present          */
    ULONG SveSupported:1;       /* ID_AA64PFR0_EL1[35:32]  SVE present           */
    ULONG SmeSupported:1;       /* ID_AA64PFR1_EL1[27:24]  SME present           */
    ULONG AtomicSupported:4;    /* ID_AA64ISAR0_EL1[23:20] 2=FEAT_LSE           */
    ULONG NeonSupported:1;      /* ID_AA64PFR0_EL1[23:20]  FP/SIMD present       */
    ULONG AsidBits:5;           /* ID_AA64MMFR0_EL1[7:4] decoded as 8 or 16      */
    ULONG HaEnabled:1;          /* TCR_EL1.HA committed by BSP                   */
    ULONG HdEnabled:1;          /* TCR_EL1.HD committed (DBM hardware dirty)     */
    ULONG DcacheLineSize;       /* CTR_EL0.DminLine decoded in bytes             */
    ULONG IcacheLineSize;       /* CTR_EL0.IminLine decoded in bytes             */
} ARM64_CPU_FEATURES;

extern ARM64_CPU_FEATURES Arm64CpuFeatures;

FORCEINLINE
VOID
KiArm64GetCacheLineSizes(
    _Out_ PULONG DcacheLineSize,
    _Out_ PULONG IcacheLineSize)
{
    ULONG64 Ctr;

    if ((Arm64CpuFeatures.DcacheLineSize != 0) &&
        (Arm64CpuFeatures.IcacheLineSize != 0))
    {
        *DcacheLineSize = Arm64CpuFeatures.DcacheLineSize;
        *IcacheLineSize = Arm64CpuFeatures.IcacheLineSize;
        return;
    }

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    *DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);
    *IcacheLineSize = 4u << (Ctr & 0xF);
}

VOID
KiInitializeDebugRegisterCounts(VOID);

#define Ki386PerfEnd()
#define KiEndInterrupt(TrapFrame, TrapStatus)

DECLSPEC_NORETURN
VOID
KiUserCallbackExit(
    _In_ PKTRAP_FRAME TrapFrame);

DECLSPEC_NORETURN
VOID
KiExceptionExit(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

BOOLEAN
KiProcessorFreezeHandler(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame);

/* Debug CPU features banner */
#if DBG
VOID KiReportCpuFeatures(IN PKPRCB Prcb);

PKTRAP_FRAME
KiArm64ActiveInterruptTrapFrame(VOID);

#endif
