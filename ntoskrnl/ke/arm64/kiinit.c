/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/kiinit.c
 * PURPOSE:         Kernel initialization stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <fpstate.h>
#define NDEBUG
#include <debug.h>
#include <arm64pl011.h>

ARM64_CPU_FEATURES Arm64CpuFeatures;

static LONG KiArm64OnlineProcessorCount;

KSCHEDULER_SUBNODE KiNode0SubNode;

struct _KPCR;

FORCEINLINE
ULONG64
KiArm64TtbrToPa(
    _In_ ULONG64 Ttbr)
{
    return Ttbr & 0x0000FFFFFFFFF000ULL;
}

#define SCTLR_EL1_A     (1ULL << 1)
#define SCTLR_EL1_SA    (1ULL << 3)
#define SCTLR_EL1_SA0   (1ULL << 4)
#define SCTLR_EL1_I     (1ULL << 12)
#define SCTLR_EL1_BT0   (1ULL << 35)
#define SCTLR_EL1_BT1   (1ULL << 36)
#define KI_ARM64_ID_AA64ISAR0_ATOMIC_LSE 2

static
ARM64_CPU_FEATURES
KiArm64ReadCpuFeatures(VOID)
{
    ARM64_CPU_FEATURES Features = {0};
    ULONG64 Pfr0, Pfr1, Isar0, Mmfr0, Mmfr1, Ctr;
    ULONG AsidField;

    __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(Pfr0));
    __asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(Pfr1));
    __asm__ __volatile__("mrs %0, id_aa64isar0_el1" : "=r"(Isar0));
    __asm__ __volatile__("mrs %0, id_aa64mmfr0_el1" : "=r"(Mmfr0));
    __asm__ __volatile__("mrs %0, id_aa64mmfr1_el1" : "=r"(Mmfr1));
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));

    Features.HafdbsSupported = (ULONG)(Mmfr1 & 0xFULL);
    Features.PanSupported = (((Mmfr1 >> 20) & 0xFULL) != 0);
    Features.SveSupported = (((Pfr0 >> 32) & 0xFULL) != 0);
    Features.SmeSupported = (((Pfr1 >> 24) & 0xFULL) != 0);
    Features.El2Implemented = (((Pfr0 >> 8) & 0xFULL) != 0);
    Features.AtomicSupported = (ULONG)((Isar0 >> 20) & 0xFULL);
    Features.NeonSupported = (((Pfr0 >> 20) & 0xFULL) != 0xFULL);
    AsidField = (ULONG)((Mmfr0 >> 4) & 0xFULL);
    Features.AsidBits = (AsidField >= 2) ? 16 : 8;
    Features.DcacheLineSize = 4u << ((Ctr >> 16) & 0xFULL);
    Features.IcacheLineSize = 4u << (Ctr & 0xFULL);

    return Features;
}

static
ULONG_PTR
KiArm64FeatureSignature(_In_ ARM64_CPU_FEATURES Features)
{
    return (ULONG_PTR)((Features.HafdbsSupported & 0xF) |
                       ((Features.PanSupported & 0x1) << 4) |
                       ((Features.SveSupported & 0x1) << 5) |
                       ((Features.SmeSupported & 0x1) << 6) |
                       ((Features.AtomicSupported & 0xF) << 8) |
                       ((Features.NeonSupported & 0x1) << 12) |
                       ((Features.AsidBits & 0x1F) << 13) |
                       ((Features.HaEnabled & 0x1) << 18) |
                       ((Features.HdEnabled & 0x1) << 19) |
                       ((Features.DcacheLineSize & 0xFF) << 20) |
                       ((Features.IcacheLineSize & 0xFF) << 28));
}

static
VOID
KiArm64ValidateProcessorFeatures(
    _In_ ULONG ProcessorNumber,
    _In_ ARM64_CPU_FEATURES LocalFeatures)
{
    BOOLEAN Mismatch = FALSE;

    if ((Arm64CpuFeatures.NeonSupported && !LocalFeatures.NeonSupported) ||
        (Arm64CpuFeatures.PanSupported && !LocalFeatures.PanSupported) ||
        (Arm64CpuFeatures.SveSupported && !LocalFeatures.SveSupported) ||
        (Arm64CpuFeatures.SmeSupported && !LocalFeatures.SmeSupported))
    {
        Mismatch = TRUE;
    }

    if ((Arm64CpuFeatures.HaEnabled && (LocalFeatures.HafdbsSupported < 1)) ||
        (Arm64CpuFeatures.HdEnabled && (LocalFeatures.HafdbsSupported < 2)))
    {
        Mismatch = TRUE;
    }

    if ((Arm64CpuFeatures.AtomicSupported >= KI_ARM64_ID_AA64ISAR0_ATOMIC_LSE) &&
        (LocalFeatures.AtomicSupported < KI_ARM64_ID_AA64ISAR0_ATOMIC_LSE))
    {
        Mismatch = TRUE;
    }

    if ((Arm64CpuFeatures.DcacheLineSize != LocalFeatures.DcacheLineSize) ||
        (Arm64CpuFeatures.IcacheLineSize != LocalFeatures.IcacheLineSize) ||
        (Arm64CpuFeatures.AsidBits != LocalFeatures.AsidBits))
    {
        Mismatch = TRUE;
    }

    if (Mismatch)
    {
        KeBugCheckEx(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED,
                     ProcessorNumber,
                     KiArm64FeatureSignature(Arm64CpuFeatures),
                     KiArm64FeatureSignature(LocalFeatures),
                     0);
    }
}

FORCEINLINE
VOID
KiArm64ApplySctlrPolicy(VOID)
{
    ULONG64 Sctlr;
    ULONG64 NewSctlr;

    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Sctlr));
    NewSctlr = (Sctlr & ~(SCTLR_EL1_A | SCTLR_EL1_BT0 | SCTLR_EL1_BT1)) |
               SCTLR_EL1_SA | SCTLR_EL1_SA0 | SCTLR_EL1_I;
    if (NewSctlr != Sctlr)
    {
        __asm__ __volatile__("msr sctlr_el1, %0" :: "r"(NewSctlr) : "memory");
        __asm__ __volatile__("isb" ::: "memory");
    }
}

#ifndef KE_ARM64_TTBR1_L0_OFFSET
#define KE_ARM64_TTBR1_L0_OFFSET 0x800ULL
#endif

#ifndef PCR_MAJOR_VERSION
#define PCR_MAJOR_VERSION 1
#endif

#ifndef PCR_MINOR_VERSION
#define PCR_MINOR_VERSION 1
#endif

#ifndef PRCB_BUILD_UNIPROCESSOR
#define PRCB_BUILD_UNIPROCESSOR 0x0001
#endif

#ifndef PRCB_BUILD_DEBUG
#define PRCB_BUILD_DEBUG 0x0002
#endif

#ifndef PRCB_MAJOR_VERSION
#define PRCB_MAJOR_VERSION 1
#endif

#ifndef PRCB_MINOR_VERSION
#define PRCB_MINOR_VERSION 1
#endif

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

VOID
KdpDprintf(
    _In_z_ PCSTR Format,
    ...);
extern ULONGLONG KdpTimeStampOffsetMicroseconds;

extern BOOLEAN KdDebuggerNotPresent;
extern BOOLEAN RtlpUse16ByteSLists;
extern VOID NTAPI ExInitPoolLookasidePointers(VOID);
extern PVOID KiArm64P0BootStack;
extern PVOID KiArm64P0BootStackLimit;

KINTERRUPT KxUnexpectedInterrupt;
ULONG KeNumberProcessIds;
ULONG KeNumberTbEntries;
ULONG ProcessCount;
PKIPCR KeArm64CurrentPcr;
PKTHREAD KeArm64CurrentThread;
KIRQL KeArm64CurrentIrql;
BOOLEAN KeArm64DpcRoutineActive;
PVOID KiArm64PanicStack;
PVOID KiArm64InterruptStack;

static KIPCR KiArm64PcrStub;
static KIPCR KiArm64BootPcr;

extern const UINT64 KiArm64EarlyVectorTable[];

typedef enum _ARM64_PRCB_CACHE_INDEX
{
    Arm64CacheL1D = 0,
    Arm64CacheL2D,
    Arm64CacheL1I,
    Arm64CacheL2I,
    Arm64CacheUnified,
    Arm64CacheDescriptorCount
} ARM64_PRCB_CACHE_INDEX;

static
VOID
KiArm64InitCacheDescriptor(_Out_ PCACHE_DESCRIPTOR Cache,
                           _In_ UCHAR Level,
                           _In_ PROCESSOR_CACHE_TYPE Type,
                           _In_ ULONG Size,
                           _In_ ULONG LineSize)
{
    RtlZeroMemory(Cache, sizeof(*Cache));
    Cache->Level = Level;
    Cache->Associativity = 0;
    Cache->LineSize = (USHORT)LineSize;
    Cache->Size = Size;
    Cache->Type = Type;
}

static VOID NTAPI KiArm64SystemStartupWrapper(PKSTART_ROUTINE StartRoutine,
                                          PVOID StartContext);

static VOID NTAPI
KiArm64IdleStartRoutine(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    KiIdleLoop();
}

static VOID NTAPI
KiArm64SystemStartupWrapper(PKSTART_ROUTINE StartRoutine,
                            PVOID StartContext)
{
    if (StartRoutine != NULL)
    {
        StartRoutine(StartContext);
    }
    KiIdleLoop();
}

static
VOID
KiArm64PrepareBootPcr(_Inout_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKIPCR Pcr;

    RtlZeroMemory(&KiArm64BootPcr, sizeof(KiArm64BootPcr));

    Pcr = &KiArm64BootPcr;
    KeArm64CurrentPcr = Pcr;
    Pcr->Self = (struct _KPCR *)Pcr;
    Pcr->CurrentPrcb = &Pcr->Prcb;
    Pcr->CurrentIrql = PASSIVE_LEVEL;

    KeArm64CurrentIrql = PASSIVE_LEVEL;
    KeArm64DpcRoutineActive = FALSE;

    if (LoaderBlock)
    {
        KeArm64CurrentThread = (PKTHREAD)LoaderBlock->Thread;
        Pcr->Prcb.CurrentThread = KeArm64CurrentThread;
        Pcr->Prcb.IdleThread = KeArm64CurrentThread;
        Pcr->Prcb.NextThread = NULL;
        Pcr->Prcb.SpBase = (PVOID)LoaderBlock->KernelStack;
    }
    else
    {
        KeArm64CurrentThread = NULL;
    }
}

static VOID
KiArm64InitializeStubPcr(VOID)
{
    RtlZeroMemory(&KiArm64PcrStub, sizeof(KiArm64PcrStub));
    KeArm64CurrentPcr = &KiArm64PcrStub;
    KeArm64CurrentPcr->CurrentPrcb = &KeArm64CurrentPcr->Prcb;
    KeArm64CurrentPcr->Self = (struct _KPCR *)KeArm64CurrentPcr;
    KeArm64CurrentPcr->CurrentIrql = PASSIVE_LEVEL;
    RtlZeroMemory(&KeArm64CurrentPcr->Prcb, sizeof(KeArm64CurrentPcr->Prcb));
    KeArm64CurrentIrql = PASSIVE_LEVEL;
    KeArm64DpcRoutineActive = FALSE;
}

VOID
NTAPI
KiInitMachineDependent(VOID)
{
    /* Only initialize stub PCR if we don't have a real PCR yet.
     * During normal boot, KiInitializeSystem already set up the real PCR
     * via KiInitializePcr before calling KiInitializeKernel, which in turn
     * calls KiInitSpinLocks to initialize the PRCB LockQueue.
     * We must NOT overwrite KeArm64CurrentPcr with the stub if it's already
     * pointing to a real, initialized PCR/PRCB. Check if LockQueue has been
     * initialized (LockQueue[0].Lock should be non-NULL after KiInitSpinLocks). */
    if (KeArm64CurrentPcr == NULL ||
        KeArm64CurrentPcr == &KiArm64PcrStub ||
        KeArm64CurrentPcr->Prcb.LockQueue[LockQueueDispatcherLock].Lock == NULL)
    {
        /* PCR not initialized yet, use stub */
        KiArm64InitializeStubPcr();
    }
    KiInitializeMachineType();
}

VOID
NTAPI
KiInitializeKernel(_Inout_ PKPROCESS InitProcess,
                   _Inout_ PKTHREAD InitThread,
                   _In_ PVOID IdleStack,
                   _Inout_ PKPRCB Prcb,
                   _In_ CCHAR Number,
                   _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKTHREAD Thread;
    ULONG_PTR DirectoryTableBase[2] = {0, 0};
    /* Quiet bring-up: suppress verbose kernel init traces */


    if ((InitProcess == NULL) ||
        (InitThread == NULL) ||
        (Prcb == NULL) ||
        (LoaderBlock == NULL))
    {
        KdPrintEx((DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[arm64] KiInitializeKernel missing arguments\n"));
        return;
    }

    /* Initialize spin locks and DPC bookkeeping */
    KiInitSpinLocks(Prcb, Number);

    /* Bind the idle stack */
    Prcb->SpBase = IdleStack;

    /* Boot CPU only work */
    if (Number == 0)
    {
        {
            ULONG64 Ttbr0;
            ULONG64 Ttbr1;

            __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

            DirectoryTableBase[0] = (ULONG_PTR)KiArm64TtbrToPa(Ttbr0);
            DirectoryTableBase[1] = (ULONG_PTR)KiArm64TtbrToPa(Ttbr1);
        }

        /*
         * Match the NT ARM64 translation contract: a shared 4 KB L0 root is
         * split between 47-bit TTBR0 and TTBR1 walks. TTBR0 starts at the page
         * base and TTBR1 starts at the upper-half L0 slot offset.
         */
        {
            ULONG64 TcrEl1;
            ULONG64 T0sz;
            ULONG64 T1sz;
            ULONG64 Ips;
            ULONG64 NewTcr;
            ULONG64 Ttbr1;
            ULONG64 Ttbr1Root;

            __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(TcrEl1));
            T0sz = TcrEl1 & 0x3FULL;
            T1sz = (TcrEl1 >> 16) & 0x3FULL;
            Ips = (TcrEl1 >> 32) & 0x7ULL;

            if ((T0sz != 17) || (T1sz != 17) || (Ips != 5))
            {
                __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
                Ttbr1Root = KiArm64TtbrToPa(Ttbr1);

                if ((Ttbr1Root != 0) && (T1sz != 17))
                {
                    ULONG64 Ttbr1Upper = Ttbr1Root + KE_ARM64_TTBR1_L0_OFFSET;

                    __asm__ __volatile__("dsb ishst" ::: "memory");
                    __asm__ __volatile__("msr ttbr1_el1, %0" :: "r"(Ttbr1Upper) : "memory");
                    __asm__ __volatile__("isb" ::: "memory");
                }

                NewTcr = TcrEl1;

                /* T0SZ/T1SZ = 17 gives the Win11-visible 47-bit VA geometry. */
                NewTcr &= ~0x3FULL;
                NewTcr |= 17ULL;
                NewTcr &= ~(0x3FULL << 16);
                NewTcr |= (17ULL << 16);

                /* IPS = 0b101 exposes a 48-bit physical address size. */
                NewTcr &= ~(0x7ULL << 32);
                NewTcr |= (5ULL << 32);

                /* Ensure proper TTBR0 attributes for 4KB granule. */
                NewTcr &= ~(0x3ULL << 14);   /* TG0 = 0b00 (4KB) */
                NewTcr |= (0x3ULL << 12);    /* SH0 = Inner Shareable */
                NewTcr &= ~(0x3ULL << 10);
                NewTcr |= (0x1ULL << 10);    /* ORGN0 = Write-Back Write-Allocate */
                NewTcr &= ~(0x3ULL << 8);
                NewTcr |= (0x1ULL << 8);     /* IRGN0 = Write-Back Write-Allocate */
                NewTcr &= ~(0x3ULL << 30);   /* TG1 = 0b10 (4KB) */
                NewTcr |= (0x2ULL << 30);
                NewTcr &= ~(0x3ULL << 28);
                NewTcr |= (0x3ULL << 28);    /* SH1 = Inner Shareable */
                NewTcr &= ~(0x3ULL << 26);
                NewTcr |= (0x1ULL << 26);    /* ORGN1 = Write-Back Write-Allocate */
                NewTcr &= ~(0x3ULL << 24);
                NewTcr |= (0x1ULL << 24);    /* IRGN1 = Write-Back Write-Allocate */
                NewTcr |= (1ULL << 22);      /* A1=1: TTBR1 defines the current ASID (matches Win11 + the AP path) */
                NewTcr &= ~((1ULL << 36) |   /* AS */
                            (1ULL << 37) |   /* TBI0 */
                            (1ULL << 38) |   /* TBI1 */
                            (1ULL << 39) |   /* HA */
                            (1ULL << 40));   /* HD */

                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("msr tcr_el1, %0" :: "r"(NewTcr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
                __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }
        }

        /*
         * Force TCR_EL1.A1=1 (TTBR1 defines the current ASID) on the BSP,
         * independent of the geometry fixup above, which only runs when
         * firmware leaves the wrong VA/PA sizes. Otherwise the BSP keeps
         * firmware's arbitrary A1 while every AP sets A1=1 (secondary path
         * below), so MmSelfMap's A1 check passes or fails depending on which
         * CPU it ran on. ASID tagging is disabled by default (every switch
         * full-flushes) so A1 only selects which TTBR's ASID field is current;
         * keeping it 1 on all CPUs matches the Windows ARM64 contract.
         */
        {
            ULONG64 CurrentTcr, FixedTcr;

            __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(CurrentTcr));
            FixedTcr = CurrentTcr | (1ULL << 22);
            if (FixedTcr != CurrentTcr)
            {
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("msr tcr_el1, %0" :: "r"(FixedTcr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }
        }

        KxUnexpectedInterrupt.DispatchAddress = KiUnexpectedInterrupt;
        RtlZeroMemory(&KxUnexpectedInterrupt.DispatchCode,
                      sizeof(KxUnexpectedInterrupt.DispatchCode));

        KiDmaIoCoherency = 0;

        KeProcessorArchitecture = 12; /* PROCESSOR_ARCHITECTURE_ARM64 */
        KeFeatureBits = 0;
        KeProcessorLevel = 0;
        KeProcessorRevision = 0;

        /* ARM64 uses 16-byte SLIST headers and 128-bit CAS */
        RtlpUse16ByteSLists = TRUE;
        MmWriteableSharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE128] = TRUE;

        /* Report firmware virtualization support when EL2 is implemented */
        if (Arm64CpuFeatures.El2Implemented)
        {
            MmWriteableSharedUserData->ProcessorFeatures[PF_VIRT_FIRMWARE_ENABLED] = TRUE;
        }

        KeLowerIrql(APC_LEVEL);

        /* Initialize the kernel VA layout (needs IRQL <= APC_LEVEL because
         * the random number generator lives in pageable code) */
        MiInitializeKernelVaLayout(LoaderBlock);

        KiInitSystem();

#if DBG
        /* Print CPU features banner using KD (parity with amd64) */
        KiReportCpuFeatures(Prcb);
#endif

        InitializeListHead(&KiProcessListHead);

        KeInitializeProcess(InitProcess,
                            0,
                            MAXULONG_PTR,
                            DirectoryTableBase,
                            FALSE);
        InitProcess->QuantumReset = MAXCHAR;
    }
    /* quiet */
    KeInitializeThread(InitProcess,
                       InitThread,
                       KiArm64SystemStartupWrapper,
                       KiArm64IdleStartRoutine,
                       NULL,
                       NULL,
                       NULL,
                       IdleStack);

    if ((IdleStack == KiArm64P0BootStack) && (KiArm64P0BootStackLimit != NULL))
    {
        InitThread->InitialStack = KiArm64P0BootStack;
        InitThread->StackBase = KiArm64P0BootStack;
        InitThread->StackLimit = (ULONG_PTR)KiArm64P0BootStackLimit;
    }

    InitThread->NextProcessor = Number;
    InitThread->Priority = HIGH_PRIORITY;
    InitThread->State = Running;
    InitThread->Affinity = ((KAFFINITY)1 << Number);
    InitThread->WaitIrql = DISPATCH_LEVEL;
    InitProcess->ActiveProcessors |= ((KAFFINITY)1 << Number);
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ((PETHREAD)InitThread)->Tcb.Process = InitProcess;
#else
    ((PETHREAD)InitThread)->ThreadsProcess = (PEPROCESS)InitProcess;
#endif
    /* quiet */

    Prcb->CurrentThread = InitThread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = InitThread;
    KeArm64CurrentThread = InitThread;
    /* quiet */

    /* Clear SP_EL0 to a known value so we can detect if it's being used */
    __asm__ volatile("msr sp_el0, %0" :: "r"((UINT64)0xDEAD0000DEAD0000ULL));
    __asm__ volatile("isb");

    ExpInitializeExecutive(Number, LoaderBlock);

    /*
     * ARM64 parity with amd64: Do NOT invoke Phase1Initialization directly
     * from the Idle thread. PsInitSystem (phase 0) creates a dedicated
     * system thread to run Phase1Initialization. The scheduler will pick it
     * up after we drop Idle's priority below normal.
     */
    if (Number == 0)
    {
        KiTimeIncrementReciprocal =
            KiComputeReciprocal(KeMaximumIncrement,
                                &KiTimeIncrementShiftCount);

        Prcb->MaximumDpcQueueDepth = KiMaximumDpcQueueDepth;
        Prcb->MinimumDpcRate = KiMinimumDpcRate;
        Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;
    }

    /*
     * ExpInitializeExecutive may run the first system thread before returning
     * here. Re-read the boot idle thread from the current PRCB instead of using
     * the original argument state after that scheduler handoff.
     */
    Prcb = KeGetCurrentPrcb();
    Thread = (Prcb != NULL) ? Prcb->IdleThread : NULL;
    if ((Thread == NULL) && (Prcb != NULL))
    {
        Thread = Prcb->CurrentThread;
    }
    if (Thread == NULL)
    {
        Thread = KeArm64CurrentThread;
    }
    if (Thread == NULL)
    {
        Thread = InitThread;
    }

    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(Thread, 0);

    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
    {
        InterlockedOr64((PLONG64)&KiIdleSummary, (LONG64)((KAFFINITY)1 << Number));
    }
    KiReleasePrcbLock(Prcb);

    if (Number != 0)
    {
        _disable();
        KeStartArm64ProcessorTimer();
        InterlockedOr64((PLONG64)&KeActiveProcessors, (LONG64)((KAFFINITY)1 << Number));
        KeNumberProcessors = (CCHAR)InterlockedIncrement((PLONG)&KiArm64OnlineProcessorCount);
    }

    KfRaiseIrql(HIGH_LEVEL);
    LoaderBlock->Prcb = 0;

    Thread = KeGetCurrentThread();
    if (Thread != NULL)
    {
        Thread->WaitIrql = DISPATCH_LEVEL;
    }
    KiIdleLoop();
}

VOID
NTAPI
KiInitializePcr(_In_ ULONG ProcessorNumber,
                _Inout_ PKIPCR Pcr,
                _Inout_ PKTHREAD IdleThread,
                _In_ BOOLEAN SetCurrentPcr,
                _In_opt_ PVOID PanicStack,
                _In_opt_ PVOID InterruptStack)
{
    ULONG CacheCount = 0;
    PARM64_LOADER_BLOCK Arm64Block;
    PCACHE_DESCRIPTOR Cache;
    PKIPCR EntryPcr;

    EntryPcr = KeGetPcr();
    RtlZeroMemory(Pcr, sizeof(*Pcr));

    Pcr->Self = (struct _KPCR *)Pcr;
    Pcr->CurrentPrcb = &Pcr->Prcb;
    Pcr->CurrentIrql = PASSIVE_LEVEL;
    Pcr->Prcb.CurrentThread = IdleThread;
    Pcr->Prcb.IdleThread = IdleThread;
    Pcr->Prcb.NextThread = NULL;
    Pcr->Prcb.DpcStack = InterruptStack;
    if (KeLoaderBlock != NULL)
    {
        Pcr->Prcb.SpBase = (PVOID)KeLoaderBlock->KernelStack;
    }

    if (SetCurrentPcr)
    {
        KiArm64PanicStack = PanicStack;
        KiArm64InterruptStack = InterruptStack;
    }

    if (SetCurrentPcr)
    {
        /*
         * ARM64 CRITICAL: initialize TPIDR_EL1 before any code that may call
         * KeGetPcr(). This path is only valid when we are bringing up the
         * currently running CPU.
         */
        __asm__ __volatile__("msr tpidr_el1, %0" : : "r"(Pcr) : "memory");
        __asm__ __volatile__("mov x18, %0" : : "r"(Pcr) : "memory");
        __asm__ __volatile__("isb" ::: "memory");

        /*
         * Set global early-boot current CPU state.
         *
         * SMP CRITICAL: These globals are only valid for the BSP (CPU 0).
         * APs must NOT overwrite them - doing so corrupts the BSP's IRQL
         * tracking, breaking spinlocks and pool operations on the BSP.
         * APs rely on TPIDR_EL1 → PCR for per-CPU state.
         */
        if (ProcessorNumber == 0)
        {
            KeArm64CurrentPcr = Pcr;
            KeArm64CurrentIrql = PASSIVE_LEVEL;
            KeArm64DpcRoutineActive = FALSE;
            KeArm64CurrentThread = IdleThread;
        }
        __asm__ __volatile__("dmb ish" ::: "memory");
    }
    else
    {
        /* Offline AP setup must not switch the currently executing CPU context. */
        ASSERT(KeGetPcr() == EntryPcr);
    }

    Pcr->Self = (struct _KPCR *)Pcr;
    Pcr->CurrentPrcb = &Pcr->Prcb;
    Pcr->CurrentIrql = PASSIVE_LEVEL;

    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;

    Pcr->Prcb.MajorVersion = PRCB_MAJOR_VERSION;
    Pcr->Prcb.MinorVersion = PRCB_MINOR_VERSION;
    Pcr->Prcb.BuildType = 0;
#ifndef CONFIG_SMP
    Pcr->Prcb.BuildType |= PRCB_BUILD_UNIPROCESSOR;
#endif
#if DBG
    Pcr->Prcb.BuildType |= PRCB_BUILD_DEBUG;
#endif

    Pcr->Prcb.Number = (UCHAR)ProcessorNumber;
    Pcr->Prcb.Group = 0;
    Pcr->Prcb.GroupIndex = (UCHAR)ProcessorNumber;
    Pcr->Prcb.SetMember = 1ULL << ProcessorNumber;
    Pcr->Prcb.MultiThreadProcessorSet = Pcr->Prcb.SetMember;
    Pcr->Prcb.CoresPerPhysicalProcessor = 1;
    Pcr->Prcb.LogicalProcessorsPerCore = 1;
    KiArm64InitializeProcessorNumaTopology(ProcessorNumber, &Pcr->Prcb, KeLoaderBlock);

    Pcr->Prcb.SchedulerSubNode = &KiNode0SubNode;
    KiNode0SubNode.ParentNodeNumber = 0;
    KiNode0SubNode.SubNodeNumber = 0;
    KiNode0SubNode.Affinity |= Pcr->Prcb.SetMember;
    KiNode0SubNode.SiblingMask |= Pcr->Prcb.SetMember;
    KiNode0SubNode.NonParkedSet |= Pcr->Prcb.SetMember;
    if (ProcessorNumber > KiNode0SubNode.Highest)
        KiNode0SubNode.Highest = ProcessorNumber;

    Pcr->Prcb.CurrentThread = IdleThread;
    Pcr->Prcb.IdleThread = IdleThread;
    Pcr->Prcb.NextThread = NULL;

    KiProcessorBlock[ProcessorNumber] = Pcr->CurrentPrcb;

    Pcr->StallScaleFactor = 50;
    Pcr->SecondLevelCacheSize = 0;
    Pcr->SecondLevelCacheAssociativity = 0;

    {
        ULONG64 CounterFrequency = KiArm64GetCounterFrequency();

        Pcr->Prcb.CycleCounterFrequency = CounterFrequency;
        Pcr->Prcb.MHz = (ULONG)((CounterFrequency + 999999ULL) / 1000000ULL);
    }

    Arm64Block = (KeLoaderBlock != NULL) ? &KeLoaderBlock->u.Arm64 : NULL;

    if (Arm64Block != NULL)
    {
        if (Arm64Block->SecondLevelDcacheSize != 0)
        {
            Pcr->SecondLevelCacheSize = Arm64Block->SecondLevelDcacheSize;
        }

        Cache = &Pcr->Prcb.Cache[Arm64CacheL1D];
        if (Arm64Block->FirstLevelDcacheSize != 0)
        {
            KiArm64InitCacheDescriptor(Cache,
                                       1,
                                       CacheData,
                                       Arm64Block->FirstLevelDcacheSize,
                                       Arm64Block->FirstLevelDcacheFillSize);
            CacheCount++;
        }

        Cache = &Pcr->Prcb.Cache[Arm64CacheL2D];
        if (Arm64Block->SecondLevelDcacheSize != 0)
        {
            KiArm64InitCacheDescriptor(Cache,
                                       2,
                                       CacheData,
                                       Arm64Block->SecondLevelDcacheSize,
                                       Arm64Block->SecondLevelDcacheFillSize);
            CacheCount++;
        }

        Cache = &Pcr->Prcb.Cache[Arm64CacheL1I];
        if (Arm64Block->FirstLevelIcacheSize != 0)
        {
            KiArm64InitCacheDescriptor(Cache,
                                       1,
                                       CacheInstruction,
                                       Arm64Block->FirstLevelIcacheSize,
                                       Arm64Block->FirstLevelIcacheFillSize);
            CacheCount++;
        }

        Cache = &Pcr->Prcb.Cache[Arm64CacheL2I];
        if (Arm64Block->SecondLevelIcacheSize != 0)
        {
            KiArm64InitCacheDescriptor(Cache,
                                       2,
                                       CacheInstruction,
                                       Arm64Block->SecondLevelIcacheSize,
                                       Arm64Block->SecondLevelIcacheFillSize);
            CacheCount++;
        }

        Cache = &Pcr->Prcb.Cache[Arm64CacheUnified];
        if (Arm64Block->SecondLevelDcacheSize != 0)
        {
            KiArm64InitCacheDescriptor(Cache,
                                       2,
                                       CacheUnified,
                                       Arm64Block->SecondLevelDcacheSize,
                                       Arm64Block->SecondLevelDcacheFillSize);
            CacheCount++;
        }
        else if (Arm64Block->FirstLevelDcacheSize != 0)
        {
            KiArm64InitCacheDescriptor(Cache,
                                       1,
                                       CacheUnified,
                                       Arm64Block->FirstLevelDcacheSize,
                                       Arm64Block->FirstLevelDcacheFillSize);
            CacheCount++;
        }
    }
    Pcr->Prcb.CacheCount = (UCHAR)CacheCount;
}

VOID
KiInitializeMachineType(VOID)
{
    ULONGLONG Midr = 0;
    PARM64_LOADER_BLOCK Arm64Info;

    Arm64Info = (KeLoaderBlock != NULL) ? &KeLoaderBlock->u.Arm64 : NULL;

    __asm__ __volatile__("mrs %0, midr_el1" : "=r"(Midr));

    KeNumberTbEntries = 64;
    KeNumberProcessIds = 256;

    KeProcessorLevel = (USHORT)((Midr >> 4) & 0x0FFFU);
    KeProcessorRevision = (USHORT)(Midr & 0x0FU);

    if (Arm64Info != NULL)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "[arm64] cache L1D=%lu L1I=%lu L2D=%lu L2I=%lu\n",
                   Arm64Info->FirstLevelDcacheSize,
                   Arm64Info->FirstLevelIcacheSize,
                   Arm64Info->SecondLevelDcacheSize,
                   Arm64Info->SecondLevelIcacheSize);
    }
}

DECLSPEC_NORETURN
VOID
NTAPI
KiInitializeSystem(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKIPCR Pcr;
    PKPROCESS InitialProcess;
    PKTHREAD InitialThread;
    PARM64_LOADER_BLOCK Arm64Block;
    KAFFINITY ProcessorMask;
    ULONG ProcessorNumber;

    BOOLEAN IsBsp;


    if (LoaderBlock == NULL)
    {
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, 'A64K', 'LDR', 0, 0);
    }

    /*
     * Detect BSP vs AP: the BSP calls this first (KeNumberProcessors == 0).
     * APs call this after KeStartAllProcessors has incremented the count.
     */
    IsBsp = (KeNumberProcessors == 0);

    if (IsBsp)
    {
        /*
         * BSP path: set up bootstrap PCR and convert FreeLdr physical
         * addresses to KSEG0 virtual addresses.
         */
        KiArm64PrepareBootPcr(LoaderBlock);
    }
    else
    {
        /*
         * AP path: do NOT overwrite BSP globals (KeArm64CurrentPcr, etc.)
         * and do NOT call KiArm64PrepareBootPcr. The AP's PCR is pre-allocated
         * by KeStartAllProcessors and passed via Arm64Block->PcrPage.
         * Addresses are already in kernel VA space - no KSEG0 conversion needed.
         */
    }

    KeLoaderBlock = LoaderBlock;
    Arm64Block = &LoaderBlock->u.Arm64;

    if (IsBsp)
    {
        /*
         * BSP only: FreeLdr passes physical addresses that need KSEG0 conversion.
         * AP addresses are already kernel VAs set by KeStartAllProcessors.
         */
#define ARM64_LDR_TO_VIRT(Value) \
    (((ULONG_PTR)(Value) < (ULONG_PTR)KSEG0_BASE) ? \
        ((ULONG_PTR)(Value) + (ULONG_PTR)KSEG0_BASE) : \
        (ULONG_PTR)(Value))

        LoaderBlock->Thread = ARM64_LDR_TO_VIRT(LoaderBlock->Thread);
        LoaderBlock->Process = ARM64_LDR_TO_VIRT(LoaderBlock->Process);
        if (LoaderBlock->KernelStack != 0)
        {
            LoaderBlock->KernelStack = ARM64_LDR_TO_VIRT(LoaderBlock->KernelStack);
        }
        Arm64Block->PcrPage = ARM64_LDR_TO_VIRT(Arm64Block->PcrPage);
        Arm64Block->PanicStack = ARM64_LDR_TO_VIRT(Arm64Block->PanicStack);
        Arm64Block->InterruptStack = ARM64_LDR_TO_VIRT(Arm64Block->InterruptStack);

#undef ARM64_LDR_TO_VIRT
    }

    InitialThread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;
    InitialProcess = (PKPROCESS)(ULONG_PTR)LoaderBlock->Process;

    if (IsBsp && InitialThread != NULL)
    {
        /* BSP only: Convert stack addresses from FreeLdr physical to KSEG0 */
        if ((ULONG_PTR)InitialThread->InitialStack < (ULONG_PTR)KSEG0_BASE)
        {
            InitialThread->InitialStack = (PVOID)((ULONG_PTR)InitialThread->InitialStack +
                                                  (ULONG_PTR)KSEG0_BASE);
        }

        if (InitialThread->StackLimit < (ULONG_PTR)KSEG0_BASE)
        {
            InitialThread->StackLimit += (ULONG_PTR)KSEG0_BASE;
        }

        if ((ULONG_PTR)InitialThread->KernelStack < (ULONG_PTR)KSEG0_BASE)
        {
            InitialThread->KernelStack = (PVOID)((ULONG_PTR)InitialThread->KernelStack +
                                                 (ULONG_PTR)KSEG0_BASE);
        }
    }

    if (InitialThread != NULL)
    {
        InitializeListHead(&InitialThread->ApcState.ApcListHead[KernelMode]);
    }

    ProcessorNumber = (ULONG)(UCHAR)KeNumberProcessors;

    Pcr = (Arm64Block->PcrPage != 0) ?
          (PKIPCR)(ULONG_PTR)Arm64Block->PcrPage :
          KeArm64CurrentPcr;

    if (Pcr != NULL)
    {
        KiInitializePcr(ProcessorNumber,
                        Pcr,
                        InitialThread,
                        TRUE,
                        (PVOID)(ULONG_PTR)Arm64Block->PanicStack,
                        (PVOID)(ULONG_PTR)Arm64Block->InterruptStack);

        if (LoaderBlock->KernelStack != 0)
        {
            Pcr->Prcb.SpBase = (PVOID)LoaderBlock->KernelStack;
        }
    }

    ExInitPoolLookasidePointers();

    if (ProcessorNumber == 0)
    {
        KeFlushTb();
        HalSweepIcache();
        HalSweepDcache();

        if ((InitialThread != NULL) && (InitialProcess != NULL))
        {
            InitialThread->ApcState.Process = InitialProcess;
        }
    }

    HalInitializeProcessor(ProcessorNumber, KeLoaderBlock);
    /* Skip DbgPrintEx for now, KD not yet initialized */

    /*
     * ARM64: Configure CPU features based on hardware capabilities.
     * Use ID registers to detect features before disabling unsupported ones.
     *
     * LAZY FP/SVE CONTEXT SWITCHING:
     * We now support lazy floating-point context switching. This means:
     * 1. FP/NEON access is initially enabled for the kernel (essential)
     * 2. SVE access causes a trap, which allocates state on first use
     * 3. SME access causes a trap (not yet fully implemented)
     * 4. Per-thread FP state is saved/restored only when needed
     *
     * This provides optimal performance for threads that don't use FP/SVE.
     */
    KiArm64ApplySctlrPolicy();

    if (ProcessorNumber == 0)
    {
        ULONG64 Cpacr;

        /* Read and publish the BSP hardware capability policy once. */
        Arm64CpuFeatures = KiArm64ReadCpuFeatures();

        /*
         * PAN (Privileged Access Never):
         *
         * ReactOS currently performs a number of kernel-mode accesses to user
         * virtual addresses (e.g. ProbeForWrite) without explicit uaccess guards.
         * If PAN is enabled, these accesses will repeatedly fault with a
         * permission fault (DFSC=0xB) and can wedge early boot.
         *
         * Disable PAN globally for now when the CPU advertises the feature.
         * Follow-up work should implement proper uaccess enable/disable around
         * all user-pointer dereferences (Windows 10/11 behavior).
         */
        if (Arm64CpuFeatures.PanSupported)
        {
            ULONG64 PanVal = 0;
            ULONG64 Sctlr;

            /*
             * LLVM's assembler rejects the architectural "msr pan, #imm"
             * syntax when building for -march=armv8-a. Use the encoded sysreg
             * form for PSTATE.PAN instead (op0=3, op1=0, CRn=4, CRm=2, op2=3).
             */
            __asm__ __volatile__("msr s3_0_c4_c2_3, %0" :: "r"(PanVal) : "memory");

            /*
             * CRITICAL: Set SCTLR_EL1.SPAN (bit 23) to prevent the CPU from
             * automatically re-enabling PAN on every exception entry.  Without
             * this, the PSTATE.PAN clear above is immediately undone by the
             * first interrupt or syscall, and all kernel-mode accesses to user
             * memory (ProbeForWrite, memmove in APC delivery, etc.) fault with
             * a PAN permission fault that the fault handler cannot resolve.
             */
            __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Sctlr));
            Sctlr |= (1ULL << 23);  /* SPAN = 1 */
            __asm__ __volatile__("msr sctlr_el1, %0" :: "r"(Sctlr) : "memory");
            __asm__ __volatile__("isb" ::: "memory");

        }

        /* Enable HA/HD if the CPU advertises FEAT_HAFDBS. */
        {
            BOOLEAN EnableHa = (Arm64CpuFeatures.HafdbsSupported >= 1);
            BOOLEAN EnableHd = (Arm64CpuFeatures.HafdbsSupported >= 2);
            ULONG64 CurrentTcr, NewTcr;

            __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(CurrentTcr));
            NewTcr = CurrentTcr & ~((1ULL << 39) | (1ULL << 40));
            if (EnableHa)
                NewTcr |= (1ULL << 39);
            if (EnableHd)
                NewTcr |= (1ULL << 40);
            if (NewTcr != CurrentTcr)
            {
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("msr tcr_el1, %0" :: "r"(NewTcr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
                __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }
            Arm64CpuFeatures.HaEnabled = EnableHa;
            Arm64CpuFeatures.HdEnabled = EnableHd;
        }

        /* Read current CPACR_EL1 */
        __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));

        /*
         * Enable FP/ASIMD for kernel initialization.
         * During boot we need FP enabled, but after thread scheduling starts,
         * we use lazy context switching (trap-on-first-use).
         */
        Cpacr |= (3ULL << 20); /* FPEN = 11 (no trap on FP) */

        /*
         * SVE: Enable with lazy context switching via trap-on-first-use.
         * When a thread first uses SVE, it will trap and we allocate state.
         * This is more efficient than disabling SVE entirely because:
         * 1. User-mode apps with SVE-optimized libraries can still work
         * 2. We only pay the cost of SVE state for threads that use it
         */
        if (Arm64CpuFeatures.SveSupported)
        {
            Cpacr &= ~(3ULL << 16); /* ZEN = 00 (trap SVE -> lazy context switch) */
        }

        /*
         * SME: Trap for now. Full SME support would require:
         * 1. Streaming SVE mode context save/restore
         * 2. ZA (matrix) state management
         * 3. PSTATE.SM/ZA bit handling
         * For now, SME traps are handled but state is not preserved.
         */
        if (Arm64CpuFeatures.SmeSupported)
        {
            Cpacr &= ~(3ULL << 24); /* SMEN = 00 (trap SME instructions) */
        }

        /* Apply configuration */
        __asm__ __volatile__("msr cpacr_el1, %0" : : "r"(Cpacr));
        __asm__ __volatile__("isb" ::: "memory");

        /* Publish hardware FP capability policy used by trap handlers. */
        KiArm64InitializeFpSupport();
    }
    else
    {
        /*
         * AP (secondary processor) initialization:
         *
         * Per-CPU system registers (CPACR_EL1, SCTLR_EL1, TCR_EL1, PSTATE.PAN)
         * are NOT shared across CPUs. Each AP starts with reset defaults after
         * PSCI CPU_ON and must be configured independently.
         *
         * Feature detection globals (KiArm64HasNeon, KiArm64HasSve, etc.)
         * were set by the BSP and are valid for all CPUs in a homogeneous system.
         */
        ARM64_CPU_FEATURES LocalFeatures;
        ULONG64 Cpacr;

        LocalFeatures = KiArm64ReadCpuFeatures();
        KiArm64ValidateProcessorFeatures(ProcessorNumber, LocalFeatures);

        /*
         * Match BSP alignment policy. Ordinary unaligned data accesses are
         * permitted; EL1/EL0 SP alignment checking remains enabled. BTI
         * enforcement is disabled because loaded images do not carry BTI pads.
         */
        KiArm64ApplySctlrPolicy();

        /*
         * PAN: Disable on this AP (same as BSP).
         * Without this, kernel accesses to user addresses fault with DFSC=0xB.
         */
        if (Arm64CpuFeatures.PanSupported)
        {
            ULONG64 PanVal = 0;
            ULONG64 Sctlr;

            __asm__ __volatile__("msr s3_0_c4_c2_3, %0" :: "r"(PanVal) : "memory");

            /* SCTLR_EL1.SPAN = 1 to prevent PAN auto-enable on exception entry */
            __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Sctlr));
            Sctlr |= (1ULL << 23);
            __asm__ __volatile__("msr sctlr_el1, %0" :: "r"(Sctlr) : "memory");
            __asm__ __volatile__("isb" ::: "memory");
        }

        /* Apply the same HA/HD TCR_EL1 bits configured by the BSP. */
        {
            ULONG64 CurrentTcr, NewTcr;

            __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(CurrentTcr));
            NewTcr = CurrentTcr & ~((1ULL << 39) | (1ULL << 40));
            NewTcr |= (1ULL << 22); /* A1: TTBR1 ASID selector */
            NewTcr &= ~(1ULL << 36);
            if (Arm64CpuFeatures.HaEnabled)
                NewTcr |= (1ULL << 39);
            if (Arm64CpuFeatures.HdEnabled)
                NewTcr |= (1ULL << 40);
            if (NewTcr != CurrentTcr)
            {
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("msr tcr_el1, %0" :: "r"(NewTcr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
                __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }
        }

        /*
         * CPACR_EL1: Configure FP/SVE/SME access on this AP.
         * Without this, FPEN=00 (reset default) causes EC=0x7 (FP trap) on
         * the first FP/SIMD instruction, which triggers recursive trap reentry
         * because the trap handler itself may use compiler-generated SIMD code.
         */
        __asm__ __volatile__("mrs %0, cpacr_el1" : "=r"(Cpacr));
        Cpacr |= (3ULL << 20); /* FPEN = 11 (no trap on FP) */

        if (Arm64CpuFeatures.SveSupported)
        {
            Cpacr &= ~(3ULL << 16); /* ZEN = 00 (trap SVE) */
        }

        if (Arm64CpuFeatures.SmeSupported)
        {
            Cpacr &= ~(3ULL << 24); /* SMEN = 00 (trap SME) */
        }

        __asm__ __volatile__("msr cpacr_el1, %0" : : "r"(Cpacr));
        __asm__ __volatile__("isb" ::: "memory");

        DPRINT("CPU %lu: Per-CPU registers configured (CPACR, PAN, HA)\n",
                ProcessorNumber);
    }


    ProcessorMask = (Pcr != NULL) ?
                    Pcr->Prcb.SetMember :
                    ((KAFFINITY)1 << ProcessorNumber);

    if (ProcessorNumber == 0)
    {
        KeActiveProcessors |= ProcessorMask;
        KeNumberProcessors++;
        KiArm64OnlineProcessorCount = 1;
    }

    KfRaiseIrql(HIGH_LEVEL);

    if (ProcessorNumber == 0)
    {
        /* Pre-seed core modules for KD banner parity (mirror amd64 minimal) */
        {
            PLIST_ENTRY Entry;
            PLDR_DATA_TABLE_ENTRY LdrEntry;
            static LDR_DATA_TABLE_ENTRY LdrCoreCopy[3];
            ULONG i = 0;

            InitializeListHead(&PsLoadedModuleList);
            for (Entry = LoaderBlock->LoadOrderListHead.Flink, i = 0;
                 Entry != &LoaderBlock->LoadOrderListHead && i < 3;
                 Entry = Entry->Flink, ++i)
            {
                LdrEntry = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                LdrCoreCopy[i] = *LdrEntry;
                InsertTailList(&PsLoadedModuleList, &LdrCoreCopy[i].InLoadOrderLinks);
            }
        }

        /* Initialize interrupts (arch/HAL stub), then install final vectors */
        KeInitInterrupts();
        /* Install final exception vectors and configure traps before KD */
        KeInitExceptions();

        /*
         * Initialize debug register counts from ID_AA64DFR0_EL1 before KD init.
         * This must happen before any code path that calls KiSaveProcessorControlState,
         * which occurs during KD symbol loading.
         */
        KiInitializeDebugRegisterCounts();

        KdInitSystem(0, KeLoaderBlock);

        __asm__ __volatile__("msr daifclr, #0x4");

        /* KD is present right after banner; continue */
        if (KdPollBreakIn())
        {
            DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
        }
    }

    KfLowerIrql(DISPATCH_LEVEL);
    if (Pcr != NULL)
    {
        Pcr->CurrentIrql = DISPATCH_LEVEL;
    }

    /*
     * Defer FP trap-on-first-use activation until later scheduling paths.
     * Early KiInitializeKernel execution still relies on fully enabled FP.
     */

    if (Pcr == NULL)
    {
        Pcr = KeArm64CurrentPcr;
    }

    if ((InitialThread != NULL) && (Pcr != NULL))
    {
        KiInitializeKernel((PKPROCESS)(ULONG_PTR)LoaderBlock->Process,
                           InitialThread,
                           (PVOID)(ULONG_PTR)LoaderBlock->KernelStack,
                           &Pcr->Prcb,
                           (CCHAR)ProcessorNumber,
                           KeLoaderBlock);
    }
    {
        PKTHREAD Thread = KeGetCurrentThread();

        if (Thread != NULL)
        {
            KeSetPriorityThread(Thread, 0);
            Thread->WaitIrql = DISPATCH_LEVEL;
        }
    }

    KiIdleLoop();
}
