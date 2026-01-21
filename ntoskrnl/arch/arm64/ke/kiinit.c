/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/kiinit.c
 * PURPOSE:         Kernel initialization stubs for ARM64
 */

#include <ntoskrnl.h>
//#define NDEBUG  /* Temporarily disabled for timer debugging */
#include <debug.h>

struct _KPCR;

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

/* CYCLE31: Raw UART output for debugging - works even if DPRINT doesn't */
#define PL011_VA 0xFFFF800009000000ULL
FORCEINLINE VOID KiArm64RawPuts(const char *str) {
    volatile ULONG *uart = (volatile ULONG *)PL011_VA;
    while (*str) {
        while (uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}  /* Wait for TXFF */
        uart[0] = *str++;
    }
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
        /* quiet */
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

    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(InitThread, 0);

    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
    {
        KiIdleSummary |= ((KAFFINITY)1 << Number);
    }
    KiReleasePrcbLock(Prcb);

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
                _In_opt_ PVOID PanicStack,
                _In_opt_ PVOID InterruptStack)
{
    ULONG CacheCount = 0;
    PARM64_LOADER_BLOCK Arm64Block;
    PCACHE_DESCRIPTOR Cache;

    UNREFERENCED_PARAMETER(PanicStack);
    UNREFERENCED_PARAMETER(InterruptStack);

    RtlZeroMemory(Pcr, sizeof(*Pcr));

    KeArm64CurrentPcr = Pcr;
    KeArm64CurrentIrql = PASSIVE_LEVEL;
    KeArm64DpcRoutineActive = FALSE;
    KeArm64CurrentThread = IdleThread;

    /*
     * ARM64 CRITICAL: Issue a data memory barrier after updating global
     * processor state variables. On ARM64's weakly-ordered memory model,
     * stores to global variables may not be visible to subsequent reads
     * (even on the same CPU in early boot before caches are fully coherent)
     * without an explicit barrier.
     *
     * This ensures that when ExInitPoolLookasidePointers (or any other code)
     * reads KeArm64CurrentPcr, it sees the value we just wrote, not stale
     * data or uninitialized memory.
     *
     * DMB ISH (Inner Shareable) is sufficient here as we're on a single CPU
     * during early boot, but it ensures proper ordering with respect to any
     * memory-mapped I/O or cache operations.
     */
    __asm__ __volatile__("dmb ish" ::: "memory");

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

    Pcr->Prcb.CurrentThread = IdleThread;
    Pcr->Prcb.IdleThread = IdleThread;
    Pcr->Prcb.NextThread = NULL;

    KiProcessorBlock[ProcessorNumber] = Pcr->CurrentPrcb;

    Pcr->StallScaleFactor = 50;
    Pcr->SecondLevelCacheSize = 0;
    Pcr->SecondLevelCacheAssociativity = 0;

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

    Pcr->Prcb.CacheCount = CacheCount;
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

    KiArm64RawPuts("[KiInitSys] ENTRY\n");
    KiArm64PrepareBootPcr(LoaderBlock);
    KiArm64RawPuts("[KiInitSys] PrepareBootPcr done\n");

    if (LoaderBlock == NULL)
    {
        KiArm64RawPuts("[KiInitSys] LoaderBlock NULL - BUGCHECK\n");
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, 'A64K', 'LDR', 0, 0);
    }

    KiArm64RawPuts("[KiInitSys] LoaderBlock OK\n");
    KeLoaderBlock = LoaderBlock;
    Arm64Block = &LoaderBlock->u.Arm64;

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

    InitialThread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;
    InitialProcess = (PKPROCESS)(ULONG_PTR)LoaderBlock->Process;

    if (InitialThread != NULL)
    {
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
        ULONG64 Pfr0, Pfr1, Cpacr;
        BOOLEAN HasSve, HasSme;

        /* Read Processor Feature Registers to detect hardware capabilities */
        __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(Pfr0));
        __asm__ __volatile__("mrs %0, id_aa64pfr1_el1" : "=r"(Pfr1));

        /* Check bits [35:32] of PFR0 for SVE support */
        HasSve = ((Pfr0 >> 32) & 0xF) != 0;

        /* Check bits [27:24] of PFR1 for SME support */
        HasSme = ((Pfr1 >> 24) & 0xF) != 0;

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

    KiArm64RawPuts("[KiInitSys] calling HalDisplayString\n");
    HalDisplayString("[KiInitSys] Raised to HIGH_LEVEL\r\n");
    KiArm64RawPuts("[KiInitSys] HalDisplayString done\n");
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

    /*
     * Unmask IRQ and FIQ (bits 1, 2) but keep SError masked (bit 3) for now.
     * SErrors can be stale from UEFI/FreeLdr and we don't want them delivered
     * until we're ready to handle them properly.
     * DAIF immediate bits: D=3, A=2, I=1, F=0 -> clear I+F = 0x3
     */
    __asm__ __volatile__("msr daifclr, #0x3" ::: "memory");
    __asm__ volatile("isb");

    KfLowerIrql(DISPATCH_LEVEL);
    if (Pcr != NULL)
    {
        Pcr->CurrentIrql = DISPATCH_LEVEL;
    }

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
