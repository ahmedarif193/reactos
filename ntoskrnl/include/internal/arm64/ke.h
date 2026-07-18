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

/* TLBI VA operands carry VA[55:12] in bits [43:0]. */
#define KI_ARM64_TLBI_VA_MASK 0x00000FFFFFFFFFFFULL

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
extern BOOLEAN KiArm64SmtFinalized;

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

/* Reads CCSIDR_EL1 geometry for the CSSELR-selected cache (LevelIndex is
   0-based). The CSSELR/CCSIDR pair is per-PE state: the caller must keep
   the walk on one processor. */
VOID
NTAPI
KiArm64ReadCcsidr(
    _In_ ULONG LevelIndex,
    _In_ BOOLEAN InstructionSide,
    _Out_ PULONG LineShift,
    _Out_ PULONG Ways,
    _Out_ PULONG Sets);

#define SYNCH_LEVEL 12

#define KD_BREAKPOINT_TYPE        ULONG
#define KD_BREAKPOINT_SIZE        sizeof(ULONG)
#define KD_BREAKPOINT_VALUE       0xD43E0000
#define KD_ASSERT_BREAKPOINT_SIZE KD_BREAKPOINT_SIZE
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
    ULONG_PTR Va = ((ULONG_PTR)Address >> PAGE_SHIFT) & KI_ARM64_TLBI_VA_MASK;

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

/*
 * Keep short mapping ranges targeted. The cutoff is deliberately conservative:
 * one global invalidation is preferable once issuing a long run of VA operands
 * would dominate, while the common protection/MDL ranges avoid repeated
 * completion barriers and whole-TLB broadcasts.
 */
#define KI_ARM64_TLBI_RANGE_FULL_THRESHOLD 256UL

FORCEINLINE
VOID
KeInvalidateTlbRange(
    _In_ PVOID BaseAddress,
    _In_ ULONG PageCount)
{
    ULONG_PTR Va;
    ULONG Index;

    if (PageCount == 0)
    {
        return;
    }

    __asm__ __volatile__("dsb ishst" ::: "memory");

    if (PageCount >= KI_ARM64_TLBI_RANGE_FULL_THRESHOLD)
    {
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    }
    else
    {
        Va = ((ULONG_PTR)BaseAddress >> PAGE_SHIFT) & KI_ARM64_TLBI_VA_MASK;
        for (Index = 0; Index < PageCount; Index++, Va++)
        {
            /* All-ASID, all-level invalidation also covers a removed table. */
            __asm__ __volatile__("tlbi vaae1is, %0" :: "r"(Va) : "memory");
        }
    }

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
 * Fallback for firmware that leaves CNTFRQ_EL0 unprogrammed or bogus.
 * 62.5 MHz is the QEMU-virt value; real hardware varies widely (Cortex-A5x:
 * 19.2/24.576 MHz, Apple M1: 24 MHz). One constant shared by cycle accounting
 * (kiinit.c) and the clock tick period (interrupt.c) so they cannot disagree.
 */
#define KI_ARM64_COUNTER_FREQUENCY_FALLBACK 62500000ULL

FORCEINLINE
ULONGLONG
KiArm64GetCounterFrequency(VOID)
{
    ULONGLONG Frequency;

    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(Frequency));
    if ((Frequency == 0) || (Frequency > 10000000000ULL))
    {
        return KI_ARM64_COUNTER_FREQUENCY_FALLBACK;
    }

    return Frequency;
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
 * Resolve a user-mode dispatcher target: convert a kernel-mapped ntdll
 * address to its user alias, falling back to the raw pointer when it is
 * already a user-space VA. Returns NULL when no user-resolvable address
 * exists; callers decide how to fail. All ARM64 dispatcher paths must use
 * this single implementation of the fallback policy.
 */
static inline PVOID
KiResolveUserDispatcherAddress(
    _In_opt_ PVOID KernelAddress,
    _In_ PEPROCESS Process)
{
    PVOID UserAddress = KiConvertSystemDllAddressToUser(KernelAddress, Process);

    if (UserAddress != NULL) return UserAddress;
    if (KernelAddress == NULL) return NULL;
    if ((ULONG_PTR)KernelAddress >= (ULONG_PTR)MmSystemRangeStart) return NULL;

    return KernelAddress;
}

/*
 * The TTBR masks isolate translation-table bases from tagged TTBR values.
 * TTBR1 retains bit 11 because T1SZ=17 selects the upper 256-entry half of the
 * shared L0 page. TCR.A1=1 is forced on every CPU, so the current 8-bit ASID
 * belongs in TTBR1_EL1[55:48]. User and process-local leaf descriptors are nG;
 * shared kernel leaf descriptors remain global.
 */
#define KI_ARM64_TTBR_ADDR_MASK   0x0000FFFFFFFFF000ULL
#define KI_ARM64_TTBR1_ADDR_MASK  0x0000FFFFFFFFF800ULL
#define KI_ARM64_TTBR_ASID_SHIFT  48

FORCEINLINE
ULONGLONG
KiArm64KernelTtbrBase(
    _In_ ULONGLONG UserDirectoryBase)
{
    ULONGLONG RootBase = UserDirectoryBase & KI_ARM64_TTBR_ADDR_MASK;
#if DBG
    ULONGLONG TcrEl1;
    ULONGLONG T1sz;

    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(TcrEl1));
    T1sz = (TcrEl1 >> 16) & 0x3FULL;
    ASSERT(T1sz == 17);
#endif
    return RootBase + KE_ARM64_TTBR1_L0_OFFSET;
}

FORCEINLINE
VOID
KiArm64WriteUserTtbr(
    _In_ ULONGLONG UserDirectoryBase,
    _In_ ULONGLONG KernelRootBase,
    _In_ UCHAR Asid,
    _In_ BOOLEAN FlushAll)
{
    /*
     * Process switch: TCR.A1 selects the tag in TTBR1. A generation-live ASID
     * needs no invalidation; ASID-0 fallback retains the old full-flush safety
     * contract. Interrupts stay masked across the transient TTBR0/TTBR1 pair.
     */
    ULONGLONG RootBase = UserDirectoryBase & KI_ARM64_TTBR_ADDR_MASK;
    ULONGLONG TaggedKernelRoot = (KernelRootBase & KI_ARM64_TTBR1_ADDR_MASK) |
                                 ((ULONGLONG)Asid << KI_ARM64_TTBR_ASID_SHIFT);
    ULONGLONG SavedDaif;

    __asm__ __volatile__("mrs %0, daif" : "=r"(SavedDaif));
    __asm__ __volatile__("msr daifset, #0xF" ::: "memory");

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(RootBase) : "memory");
    __asm__ __volatile__("isb" ::: "memory");
    __asm__ __volatile__("msr ttbr1_el1, %0" :: "r"(TaggedKernelRoot) : "memory");
    __asm__ __volatile__("isb" ::: "memory");

    if (FlushAll)
    {
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");
    }

    __asm__ __volatile__("msr daif, %0" :: "r"(SavedDaif) : "memory");
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

/* Same EL0 test for a CONTEXT, whose Cpsr field carries the saved PSTATE */
#define KiGetContextPreviousMode(Context) \
    (((Context)->Cpsr & 0xF) == 0 ? UserMode : KernelMode)

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

/*
 * Publish the logical IRQL. Inline because this sits on every raise/lower
 * (i.e. every spinlock acquire/release); the fast path is one strb via x18.
 * The global fallback is valid only during BSP bootstrap, before x18 is set.
 */
FORCEINLINE
VOID
KiSetCurrentIrql(
    _In_ KIRQL Irql)
{
    ULONG Value = Irql;

    if (KeGetPcr() == NULL)
    {
        KeArm64CurrentIrql = Irql;
        return;
    }

    __asm__ __volatile__("strb %w0, [x18, #" ARM64_KPCR_STRINGIFY(ARM64_KPCR_CURRENT_IRQL) "]"
                         :: "r"(Value) : "memory");
}

VOID
NTAPI
KiRestoreTrapFrameIrql(
    _In_ KIRQL Irql);

/* Set after HAL phase 1 makes GIC priority masking available. */
extern BOOLEAN KiHalInitialized;

/* Keep the logical IRQL and GIC priority mask together; raises remain lazy. */
FORCEINLINE
VOID
KiSetIrqlAndPriorityMask(
    _In_ KIRQL Irql)
{
    KiSetCurrentIrql(Irql);

    if (KiHalInitialized)
    {
        HalSetGicPriorityMask(Irql);
    }
}

VOID
NTAPI
KeReenableTimerInterrupt(
    VOID);

VOID
NTAPI
KeStartArm64ProcessorTimer(
    VOID);

VOID
NTAPI
KiArm64SaveProcessorClock(
    _In_ ULONG ProcessorNumber);

ULONG
NTAPI
KiArm64GetProcessorClockMHz(
    _In_ ULONG ProcessorNumber);

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
    ULONG El2Implemented:1;     /* ID_AA64PFR0_EL1[11:8]   EL2 implemented       */
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

FORCEINLINE
VOID
KeSweepICache(
    _In_opt_ PVOID BaseAddress,
    _In_ SIZE_T FlushSize)
{
    ULONG DLine, ILine;
    ULONG_PTR Start, End, Addr;

    if (!BaseAddress || FlushSize == 0)
    {
        /* ic ialluis broadcasts to all CPUs in inner shareable domain (SMP-safe) */
        __asm__ __volatile__("ic ialluis\n\tdsb ish\n\tisb" ::: "memory");
        return;
    }

    KiArm64GetCacheLineSizes(&DLine, &ILine);

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
#endif
