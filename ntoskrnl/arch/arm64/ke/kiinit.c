/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/kiinit.c
 * PURPOSE:         Kernel initialization stubs for ARM64
 */

#include <ntoskrnl.h>
#include "../include/fpstate.h"
#define NDEBUG
#include <debug.h>
#include "../include/arm64pl011.h"

struct _KPCR;

FORCEINLINE
ULONG64
KiArm64TtbrToPa(
    _In_ ULONG64 Ttbr)
{
    return Ttbr & 0x0000FFFFFFFFF000ULL;
}

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

VOID KiArm64RawPuts(const char *str) {
    volatile UCHAR *u = (volatile UCHAR *)0xFFFF800009000000ULL;
    while (*str) *u = *str++;
}

FORCEINLINE VOID KiArm64RawPutHex(ULONG64 value, ULONG width) {
    UNREFERENCED_PARAMETER(value);
    UNREFERENCED_PARAMETER(width);
}

VOID
KdpDprintf(
    _In_z_ PCSTR Format,
    ...);
extern ULONGLONG KdpTimeStampOffsetMicroseconds;

extern BOOLEAN KdDebuggerNotPresent;
extern BOOLEAN RtlpUse16ByteSLists;
extern VOID NTAPI ExInitPoolLookasidePointers(VOID);

KINTERRUPT KxUnexpectedInterrupt;
ULONG KeNumberProcessIds;
ULONG KeNumberTbEntries;
ULONG ProcessCount;
PKIPCR KeArm64CurrentPcr;
PKTHREAD KeArm64CurrentThread;
KIRQL KeArm64CurrentIrql;
BOOLEAN KeArm64DpcRoutineActive;

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
        Pcr->Prcb.RspBase = (UINT64)(ULONG_PTR)LoaderBlock->KernelStack;
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

    KiArm64RawPuts("[KiInitKernel] ENTRY\n");

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
    Prcb->RspBase = (ULONG_PTR)IdleStack;

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
         * ARM64 CRITICAL: Configure TCR_EL1.T0SZ for 48-bit user VA.
         *
         * T0SZ (bits [5:0]) controls TTBR0 (user) VA size: VA_bits = 64 - T0SZ.
         * The bootloader preserves firmware's T0SZ because it runs from identity-
         * mapped (TTBR0) addresses. Now that we're running entirely from TTBR1
         * (kernel addresses), we can safely reconfigure T0SZ for user mode.
         */
        {
            ULONG64 TcrEl1;
            ULONG64 T0sz;
            ULONG64 NewTcr;

            __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(TcrEl1));
            T0sz = TcrEl1 & 0x3FULL;

            if (T0sz != 16)
            {
                /* Build new TCR with T0SZ = 16 for 48-bit user VA */
                NewTcr = TcrEl1 & ~0x3FULL;
                NewTcr |= 16ULL;

                /* Ensure proper TTBR0 attributes for 4KB granule, 48-bit VA */
                NewTcr &= ~(0x3ULL << 14);   /* TG0 = 0b00 (4KB) */
                NewTcr |= (0x3ULL << 12);    /* SH0 = Inner Shareable */
                NewTcr &= ~(0x3ULL << 10);
                NewTcr |= (0x1ULL << 10);    /* ORGN0 = Write-Back Write-Allocate */
                NewTcr &= ~(0x3ULL << 8);
                NewTcr |= (0x1ULL << 8);     /* IRGN0 = Write-Back Write-Allocate */

                /*
                 * Hardware Access Flag management (ARMv8.1+ optional feature).
                 *
                 * CRITICAL: We must check ID_AA64MMFR1_EL1.HAFDBS before enabling
                 * TCR.HA, as not all ARMv8.0 implementations support it.
                 *
                 * HAFDBS (bits [3:0] of MMFR1):
                 *   0b0000 = Not supported
                 *   0b0001 = Access flag hardware update supported (HA)
                 *   0b0010 = AF + Dirty state hardware update supported (HA + HD)
                 *
                 * We check for this below after reading MMFR1 (at line ~729).
                 * For now, do NOT unconditionally enable TCR.HA here.
                 */

                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("msr tcr_el1, %0" :: "r"(NewTcr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
                __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
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
        SharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE128] = TRUE;

        KeLowerIrql(APC_LEVEL);
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

    InitThread->NextProcessor = Number;
    InitThread->Priority = HIGH_PRIORITY;
    InitThread->State = Running;
    InitThread->Affinity = ((KAFFINITY)1 << Number);
    InitThread->WaitIrql = DISPATCH_LEVEL;
    InitProcess->ActiveProcessors |= ((KAFFINITY)1 << Number);
    ((PETHREAD)InitThread)->ThreadsProcess = (PEPROCESS)InitProcess;
    /* quiet */

    Prcb->CurrentThread = InitThread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = InitThread;
    /* quiet */

    /* Clear SP_EL0 to a known value so we can detect if it's being used */
    __asm__ volatile("msr sp_el0, %0" :: "r"((UINT64)0xDEAD0000DEAD0000ULL));
    __asm__ volatile("isb");

    ExpInitializeExecutive(Number, LoaderBlock);
    KiArm64RawPuts("[KiInitKernel] ExpInitializeExecutive done\n");

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
     * ARM64: Raise to SYNCH_LEVEL (not DISPATCH_LEVEL) for scheduler operations.
     * On x86/amd64, SYNCH_LEVEL == DISPATCH_LEVEL == 2, so this doesn't matter.
     * On ARM64, SYNCH_LEVEL (12) > DISPATCH_LEVEL (2). The dispatcher lock
     * (KiAcquireDispatcherLockAtSynchLevel) asserts IRQL >= SYNCH_LEVEL.
     * KeSetPriorityThread internally acquires the dispatcher lock.
     */
    KiArm64RawPuts("[KiInitKernel] raising to SYNCH_LEVEL\n");
    KfRaiseIrql(SYNCH_LEVEL);
    KiArm64RawPuts("[KiInitKernel] calling KeSetPriorityThread\n");
    KeSetPriorityThread(InitThread, 0);
    KiArm64RawPuts("[KiInitKernel] KeSetPriorityThread done\n");

    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
    {
        KiIdleSummary |= ((KAFFINITY)1 << Number);
    }
    KiReleasePrcbLock(Prcb);
    KiArm64RawPuts("[KiInitKernel] PrcbLock released\n");

    KfRaiseIrql(HIGH_LEVEL);
    KiArm64RawPuts("[KiInitKernel] raised to HIGH_LEVEL\n");
    LoaderBlock->Prcb = 0;

    Thread = KeGetCurrentThread();
    if (Thread != NULL)
    {
        Thread->WaitIrql = DISPATCH_LEVEL;
    }
    KiArm64RawPuts("[KiInitKernel] entering KiIdleLoop\n");
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

    UNREFERENCED_PARAMETER(PanicStack);
    UNREFERENCED_PARAMETER(InterruptStack);

    KiArm64RawPuts("[InitPcr] entry\n");
    KiArm64RawPuts("[InitPcr] zeroing Pcr struct\n");
    EntryPcr = KeGetPcr();
    RtlZeroMemory(Pcr, sizeof(*Pcr));
    KiArm64RawPuts("[InitPcr] Pcr zeroed\n");

    if (SetCurrentPcr)
    {
        /*
         * ARM64 CRITICAL: initialize TPIDR_EL1 before any code that may call
         * KeGetPcr(). This path is only valid when we are bringing up the
         * currently running CPU.
         */
        KiArm64RawPuts("[InitPcr] writing TPIDR_EL1 = Pcr\n");
        __asm__ __volatile__("msr tpidr_el1, %0" : : "r"(Pcr) : "memory");
        __asm__ __volatile__("isb" ::: "memory");
        KiArm64RawPuts("[InitPcr] TPIDR_EL1 set + ISB\n");

        /* Verify TPIDR_EL1 was set correctly (diagnostic check) */
        {
            PVOID VerifyPcr;
            __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(VerifyPcr));
            DPRINT1("CPU %lu: TPIDR_EL1 initialized to PCR @ %p (verify: %p)\n",
                    ProcessorNumber, Pcr, VerifyPcr);
            if (VerifyPcr != Pcr)
            {
                KiArm64RawPuts("[InitPcr] FATAL: TPIDR_EL1 readback mismatch\n");
                KeBugCheckEx(PHASE0_INITIALIZATION_FAILED,
                             ('A' << 24) | ('R' << 16) | ('M' << 8) | '6',
                             (ULONG_PTR)Pcr,
                             (ULONG_PTR)VerifyPcr,
                             1);
            }
        }
        KiArm64RawPuts("[InitPcr] TPIDR_EL1 readback OK\n");

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
            KiArm64RawPuts("[InitPcr] BSP: setting Arm64 globals\n");
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

    KiArm64RawPuts("[InitPcr] populating Pcr fields\n");
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
    Pcr->Prcb.SetMember = 1ULL << ProcessorNumber;
    Pcr->Prcb.MultiThreadProcessorSet = Pcr->Prcb.SetMember;
    KiArm64RawPuts("[InitPcr] reading KeNodeBlock[0]\n");
    Pcr->Prcb.ParentNode = KeNodeBlock[0];
    KiArm64RawPuts("[InitPcr] writing ParentNode->ProcessorMask\n");
    Pcr->Prcb.ParentNode->ProcessorMask |= Pcr->Prcb.SetMember;
    KiArm64RawPuts("[InitPcr] ParentNode wired\n");

    Pcr->Prcb.CurrentThread = IdleThread;
    Pcr->Prcb.IdleThread = IdleThread;
    Pcr->Prcb.NextThread = NULL;

    KiProcessorBlock[ProcessorNumber] = Pcr->CurrentPrcb;

    Pcr->StallScaleFactor = 50;
    Pcr->SecondLevelCacheSize = 0;
    Pcr->SecondLevelCacheAssociativity = 0;

    KiArm64RawPuts("[InitPcr] reading KeLoaderBlock->u.Arm64\n");
    Arm64Block = (KeLoaderBlock != NULL) ? &KeLoaderBlock->u.Arm64 : NULL;

    if (Arm64Block != NULL)
    {
        KiArm64RawPuts("[InitPcr] LoaderBlock->u.Arm64 present, applying caches\n");
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
    else
    {
        KiArm64RawPuts("[InitPcr] WARNING: KeLoaderBlock->u.Arm64 is NULL\n");
    }

    Pcr->Prcb.CacheCount = CacheCount;
    KiArm64RawPuts("[InitPcr] returning, CacheCount set\n");
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

    KiArm64RawPuts("[KiInitSys] ENTRY\n");

    if (LoaderBlock == NULL)
    {
        KiArm64RawPuts("[KiInitSys] LoaderBlock NULL - BUGCHECK\n");
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
        KiArm64RawPuts("[KiInitSys] BSP PrepareBootPcr done\n");
    }
    else
    {
        /*
         * AP path: do NOT overwrite BSP globals (KeArm64CurrentPcr, etc.)
         * and do NOT call KiArm64PrepareBootPcr. The AP's PCR is pre-allocated
         * by KeStartAllProcessors and passed via Arm64Block->PcrPage.
         * Addresses are already in kernel VA space - no KSEG0 conversion needed.
         */
        KiArm64RawPuts("[KiInitSys] AP path (skip PrepareBootPcr)\n");
    }

    KiArm64RawPuts("[KiInitSys] LoaderBlock OK\n");
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

    KiArm64RawPuts("[KiInitSys] getting Pcr\n");
    Pcr = (Arm64Block->PcrPage != 0) ?
          (PKIPCR)(ULONG_PTR)Arm64Block->PcrPage :
          KeArm64CurrentPcr;

    KiArm64RawPuts("[KiInitSys] Pcr obtained\n");
    if (Pcr != NULL)
    {
        KiArm64RawPuts("[KiInitSys] calling KiInitializePcr\n");
        KiInitializePcr(ProcessorNumber,
                        Pcr,
                        InitialThread,
                        TRUE,
                        (PVOID)(ULONG_PTR)Arm64Block->PanicStack,
                        (PVOID)(ULONG_PTR)Arm64Block->InterruptStack);
        KiArm64RawPuts("[KiInitSys] KiInitializePcr done\n");

        if (LoaderBlock->KernelStack != 0)
        {
            Pcr->Prcb.RspBase = (ULONG_PTR)LoaderBlock->KernelStack;
        }
    }

    KiArm64RawPuts("[KiInitSys] calling ExInitPoolLookasidePointers\n");
    ExInitPoolLookasidePointers();
    KiArm64RawPuts("[KiInitSys] ExInitPoolLookasidePointers done\n");

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

    KiArm64RawPuts("[KiInitSys] calling HalInitializeProcessor\n");
    HalInitializeProcessor(ProcessorNumber, KeLoaderBlock);
    KiArm64RawPuts("[KiInitSys] HalInitializeProcessor done\n");
    /* Skip DbgPrintEx for now, KD not yet initialized */

    KiArm64RawPuts("[KiInitSys] CPU features config start\n");
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
    if (ProcessorNumber == 0)
    {
        ULONG64 Pfr0, Pfr1, Mmfr1, Cpacr;
        BOOLEAN HasSve, HasSme;

        /* Read Processor Feature Registers to detect hardware capabilities */
        __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(Pfr0));
        __asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(Pfr1));
        __asm__ __volatile__("mrs %0, id_aa64mmfr1_el1" : "=r"(Mmfr1));

        /*
         * NT code expects ordinary unaligned data accesses to normal memory to
         * work on ARM64. Keep stack alignment checking, but force SCTLR_EL1.A off
         * in case firmware or the loader left strict alignment checking enabled.
         */
        {
            ULONG64 Sctlr;

            __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Sctlr));
            if (Sctlr & (1ULL << 1))
            {
                KiArm64RawPuts("[KiInitSys] clearing SCTLR_EL1.A\n");
                Sctlr &= ~(1ULL << 1);
                __asm__ __volatile__("msr sctlr_el1, %0" :: "r"(Sctlr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }
        }

        /* Check bits [35:32] of PFR0 for SVE support */
        HasSve = ((Pfr0 >> 32) & 0xF) != 0;

        /* Check bits [27:24] of PFR1 for SME support */
        HasSme = ((Pfr1 >> 24) & 0xF) != 0;

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
        if (((Mmfr1 >> 20) & 0xFULL) != 0)
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

        /*
         * Hardware Access Flag (HA) capability detection and enablement.
         *
         * Check ID_AA64MMFR1_EL1.HAFDBS (bits [3:0]) to determine if the CPU
         * supports hardware-managed Access Flags. Only enable TCR.HA if supported.
         *
         * Without HA, the CPU generates Access Flag faults (FSC=0x09/0x0A/0x0B)
         * on first access to pages with AF=0. With HA, the CPU automatically sets
         * AF=1, eliminating most AF faults.
         */
        {
            ULONG Hafdbs = (ULONG)(Mmfr1 & 0xFULL);
            BOOLEAN HasHardwareAF = (Hafdbs >= 1); /* 0x1 or 0x2 = HA supported */

            if (HasHardwareAF)
            {
                ULONG64 CurrentTcr;
                __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(CurrentTcr));

                /* Enable TCR.HA (bit 39) */
                CurrentTcr |= (1ULL << 39);

                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("msr tcr_el1, %0" :: "r"(CurrentTcr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
                __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");

                DPRINT1("ARM64: Hardware Access Flag management ENABLED (HAFDBS=0x%lx, TCR.HA=1)\n", Hafdbs);
            }
            else
            {
                /*
                 * HA not supported - CPU will continue generating AF faults.
                 * The fault handler in pagfault.c must handle these (FSC=0x09/0x0A/0x0B).
                 */
                DPRINT1("ARM64: Hardware Access Flag management NOT supported (HAFDBS=0x%lx, TCR.HA=0)\n", Hafdbs);
                DPRINT1("       AF faults will be handled in software (expect higher fault rate)\n");
            }
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
        if (HasSve)
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
        if (HasSme)
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
        ULONG64 Cpacr, Mmfr1;

        /* Read AP's feature register (should match BSP) */
        __asm__ __volatile__("mrs %0, id_aa64mmfr1_el1" : "=r"(Mmfr1));

        /*
         * Match BSP alignment policy. Ordinary unaligned data accesses are
         * permitted; SP alignment checking remains controlled by SCTLR_EL1.SA.
         */
        {
            ULONG64 Sctlr;

            __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Sctlr));
            if (Sctlr & (1ULL << 1))
            {
                KiArm64RawPuts("[KiInitSys] AP clearing SCTLR_EL1.A\n");
                Sctlr &= ~(1ULL << 1);
                __asm__ __volatile__("msr sctlr_el1, %0" :: "r"(Sctlr) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }
        }

        /*
         * PAN: Disable on this AP (same as BSP).
         * Without this, kernel accesses to user addresses fault with DFSC=0xB.
         */
        if (((Mmfr1 >> 20) & 0xFULL) != 0)
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

        /*
         * Hardware Access Flag: Enable on this AP if the CPU supports it.
         */
        {
            ULONG Hafdbs = (ULONG)(Mmfr1 & 0xFULL);
            if (Hafdbs >= 1)
            {
                ULONG64 CurrentTcr;
                __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(CurrentTcr));
                CurrentTcr |= (1ULL << 39); /* TCR.HA = 1 */
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("msr tcr_el1, %0" :: "r"(CurrentTcr) : "memory");
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

        if (KiArm64HasSve)
        {
            Cpacr &= ~(3ULL << 16); /* ZEN = 00 (trap SVE) */
        }

        /* SME: trap on APs too */
        Cpacr &= ~(3ULL << 24); /* SMEN = 00 (trap SME) */

        __asm__ __volatile__("msr cpacr_el1, %0" : : "r"(Cpacr));
        __asm__ __volatile__("isb" ::: "memory");

        DPRINT1("CPU %lu: Per-CPU registers configured (CPACR, PAN, HA)\n",
                ProcessorNumber);
    }

    KiArm64RawPuts("[KiInitSys] CPU features config done\n");

    ProcessorMask = (Pcr != NULL) ?
                    Pcr->Prcb.SetMember :
                    ((KAFFINITY)1 << ProcessorNumber);

    KeActiveProcessors |= ProcessorMask;
    KeNumberProcessors++;

    KiArm64RawPuts("[KiInitSys] calling KfRaiseIrql(HIGH_LEVEL)\n");
    KfRaiseIrql(HIGH_LEVEL);
    KiArm64RawPuts("[KiInitSys] KfRaiseIrql done\n");

    if (ProcessorNumber == 0)
    {
        KiArm64RawPuts("[KiInitSys] Pre-seed core modules\n");
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
        KiArm64RawPuts("[KiInitSys] Pre-seed done\n");

        KiArm64RawPuts("[KiInitSys] calling KeInitInterrupts\n");
        /* Initialize interrupts (arch/HAL stub), then install final vectors */
        KeInitInterrupts();
        KiArm64RawPuts("[KiInitSys] KeInitInterrupts done\n");
        /* Install final exception vectors and configure traps before KD */
        KiArm64RawPuts("[KiInitSys] calling KeInitExceptions\n");
        KeInitExceptions();
        KiArm64RawPuts("[KiInitSys] KeInitExceptions done\n");

        /*
         * Initialize debug register counts from ID_AA64DFR0_EL1 before KD init.
         * This must happen before any code path that calls KiSaveProcessorControlState,
         * which occurs during KD symbol loading.
         */
        KiArm64RawPuts("[KiInitSys] calling KiInitializeDebugRegisterCounts\n");
        KiInitializeDebugRegisterCounts();
        KiArm64RawPuts("[KiInitSys] calling KdInitSystem\n");

        KdInitSystem(0, KeLoaderBlock);
        KiArm64RawPuts("[KiInitSys] KdInitSystem done\n");

        /* KD is present right after banner; continue */
        if (KdPollBreakIn())
        {
            DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
        }
    }

    KiArm64RawPuts("[KiInitSys] lowering IRQL to DISPATCH_LEVEL\n");
    KfLowerIrql(DISPATCH_LEVEL);
    if (Pcr != NULL)
    {
        Pcr->CurrentIrql = DISPATCH_LEVEL;
    }
    KiArm64RawPuts("[KiInitSys] IRQL lowered\n");

    /*
     * Defer FP trap-on-first-use activation until later scheduling paths.
     * Early KiInitializeKernel execution still relies on fully enabled FP.
     */
    KiArm64RawPuts("[KiInitSys] FP trap-on-first-use deferred\n");

    if (Pcr == NULL)
    {
        Pcr = KeArm64CurrentPcr;
    }

    if ((InitialThread != NULL) && (Pcr != NULL))
    {
        KiArm64RawPuts("[KiInitSys] calling KiInitializeKernel\n");
        KiInitializeKernel((PKPROCESS)(ULONG_PTR)LoaderBlock->Process,
                           InitialThread,
                           (PVOID)(ULONG_PTR)LoaderBlock->KernelStack,
                           &Pcr->Prcb,
                           (CCHAR)ProcessorNumber,
                           KeLoaderBlock);
        KiArm64RawPuts("[KiInitSys] KiInitializeKernel returned\n");
    }
    else
    {
        KiArm64RawPuts("[KiInitSys] SKIP KiInitializeKernel (InitialThread/Pcr NULL)\n");
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
