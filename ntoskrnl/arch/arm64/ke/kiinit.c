/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/kiinit.c
 * PURPOSE:         Kernel initialization stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
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

VOID
KiArm64BootStageLog(_In_z_ PCSTR Stage);
VOID
KdpDprintf(
    _In_z_ PCSTR Format,
    ...);
extern ULONGLONG KdpTimeStampOffsetMicroseconds;

static ULONGLONG KiArm64PcrBannerFallbackCounter;
static VOID
KiArm64EmitStageLog(_In_z_ PCSTR Stage)
{
    KiArm64BootStageLog(Stage);
}

extern BOOLEAN KdDebuggerNotPresent;
extern BOOLEAN RtlpUse16ByteSLists;

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

    /* Keep stage-log helpers reachable to avoid unused warnings on GCC. */
    if (0)
    {
        KiArm64EmitStageLog("KiArm64PrepareBootPcr");
        ++KiArm64PcrBannerFallbackCounter;
    }

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
    KiArm64BootStageLog("[arm64] KiInitializeKernel: entry");
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
        KiArm64BootStageLog("[arm64] KiInitializeKernel: before KiInitSystem");
        KiInitSystem();
        KiArm64BootStageLog("[arm64] KiInitializeKernel: after KiInitSystem");

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

    KiArm64BootStageLog("[arm64] KiInitializeKernel: before ExpInitializeExecutive");

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        UINT64 sp_el0, current_sp, daif, sctlr;
        CHAR buf[200];

        /* Read SP_EL0 to check if it has a stale value */
        __asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
        __asm__ volatile("mov %0, sp" : "=r"(current_sp));
        __asm__ volatile("mrs %0, daif" : "=r"(daif));
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

        RtlStringCbPrintfA(buf, sizeof(buf),
            "[arm64] SP_EL0=0x%llx SP=0x%llx DAIF=0x%llx SCTLR=0x%llx",
            (unsigned long long)sp_el0,
            (unsigned long long)current_sp,
            (unsigned long long)daif,
            (unsigned long long)sctlr);
        KiArm64BootStageLog(buf);

        /* Clear SP_EL0 to a known value so we can detect if it's being used */
        __asm__ volatile("msr sp_el0, %0" :: "r"((UINT64)0xDEAD0000DEAD0000ULL));
        __asm__ volatile("isb");

        KiArm64BootStageLog("[arm64] SP_EL0 set to sentinel, calling ExpInitializeExecutive");
    }
#endif

    ExpInitializeExecutive(Number, LoaderBlock);
    KiArm64BootStageLog("[arm64] KiInitializeKernel: after ExpInitializeExecutive");

#if defined(_M_ARM64)
    /*
     * ARM64 parity with amd64: Do NOT invoke Phase1Initialization directly
     * from the Idle thread. PsInitSystem (phase 0) creates a dedicated
     * system thread to run Phase1Initialization. The scheduler will pick it
     * up after we drop Idle's priority below normal.
     */
    KiArm64BootStageLog("[arm64] KiInitializeKernel: Phase 1 will run in a system thread");
#endif

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
    KiArm64BootStageLog("[arm64] KiInitializeKernel: entering idle loop");
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
KiInitializeSystem(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKIPCR Pcr;
    PKPROCESS InitialProcess;
    PKTHREAD InitialThread;
    PARM64_LOADER_BLOCK Arm64Block;
    KAFFINITY ProcessorMask;
    ULONG ProcessorNumber;
    ULONG_PTR VectorBase;
    ULONGLONG Ttbr0;
    ULONGLONG Ttbr1;
    KiArm64BootStageLog("[arm64] KiInitializeSystem: begin");
    KiArm64PrepareBootPcr(LoaderBlock);
    KiArm64BootStageLog("[arm64] KiInitializeSystem: PCR prepared");

    if (LoaderBlock == NULL)
    {
        KeBugCheckEx(PHASE0_INITIALIZATION_FAILED, 'A64K', 'LDR', 0, 0);
    }
    KiArm64BootStageLog("[arm64] KiInitializeSystem: loader validated");

    KeLoaderBlock = LoaderBlock;
    Arm64Block = &LoaderBlock->u.Arm64;

#if defined(_M_ARM64) || defined(__aarch64__)
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
#endif

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

        {
            CHAR Stage[200];
            if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                                              sizeof(Stage),
                                              "[arm64] init thread stacks: th=%p init=%p limit=%p kernel=%p",
                                              InitialThread,
                                              InitialThread->InitialStack,
                                              (PVOID)InitialThread->StackLimit,
                                              InitialThread->KernelStack)))
            {
                KiArm64BootStageLog(Stage);
            }
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
                        (PVOID)(ULONG_PTR)Arm64Block->PanicStack,
                        (PVOID)(ULONG_PTR)Arm64Block->InterruptStack);

        if (LoaderBlock->KernelStack != 0)
        {
            Pcr->Prcb.RspBase = (ULONG_PTR)LoaderBlock->KernelStack;
        }
    }

    if (ProcessorNumber == 0)
    {
        KiArm64BootStageLog("[arm64] KiInitializeSystem: preparing caches");
        KeFlushTb();
        HalSweepIcache();
        HalSweepDcache();
        KiArm64BootStageLog("[arm64] KiInitializeSystem: caches flushed");

        if ((InitialThread != NULL) && (InitialProcess != NULL))
        {
            InitialThread->ApcState.Process = InitialProcess;
        }
    }

    HalInitializeProcessor(ProcessorNumber, KeLoaderBlock);
    KiArm64BootStageLog("[arm64] KiInitializeSystem: HAL init complete");
    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_TRACE_LEVEL,
               "[arm64] KiInitializeSystem: cpu %lu HAL ready\n",
               ProcessorNumber);

    ProcessorMask = (Pcr != NULL) ?
                    Pcr->Prcb.SetMember :
                    ((KAFFINITY)1 << ProcessorNumber);

    KeActiveProcessors |= ProcessorMask;
    KeNumberProcessors++;

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
            KiArm64BootStageLog("[arm64] KiInitializeSystem: pre-seeded PsLoadedModuleList");
        }

        /* Initialize interrupts (arch/HAL stub), then install final vectors */
        KeInitInterrupts();
        /* Install final exception vectors and configure traps before KD */
        KiArm64BootStageLog("[arm64] KiInitializeSystem: installing final exceptions");
        KeInitExceptions();
        {
            CHAR Buf[128];
            __asm__ __volatile__("mrs %0, vbar_el1" : "=r"(VectorBase));
            if (NT_SUCCESS(RtlStringCbPrintfA(Buf, sizeof(Buf),
                                              "[arm64] KiInitializeSystem: after KeInitExceptions VBAR=%p final=%lu",
                                              (PVOID)VectorBase,
                                              (ULONG)KiArm64FinalVectorsInstalled)))
            {
                KiArm64BootStageLog(Buf);
            }
        }

        /*
         * Initialize debug register counts from ID_AA64DFR0_EL1 before KD init.
         * This must happen before any code path that calls KiSaveProcessorControlState,
         * which occurs during KD symbol loading.
         */
        KiInitializeDebugRegisterCounts();

        KiArm64BootStageLog("[arm64] KiInitializeSystem: enabling KD");
        /* After KD enable, use normal DPRINT1 path (parity with amd64) */
        DPRINT1("[arm64] KiInitializeSystem: boot cpu enabling KD\n");
        KdInitSystem(0, KeLoaderBlock);

        /* KD is present right after banner; continue */
        DPRINT1("[arm64] KD present after banner\n");

        /* removed noisy stage log around KdPollBreakIn */
        /* Skip GIC sysreg probe here; can trap on some firmware setups */

        KiArm64BootStageLog("[arm64] post-banner: entering KdPollBreakIn");
        if (KdPollBreakIn())
        {
            DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
        }
        KiArm64BootStageLog("[arm64] post-banner: after KdPollBreakIn");
        /* removed noisy stage log around KdPollBreakIn */
    }

    /*
     * Unmask IRQ and FIQ (bits 1, 2) but keep SError masked (bit 3) for now.
     * SErrors can be stale from UEFI/FreeLdr and we don't want them delivered
     * until we're ready to handle them properly.
     * DAIF immediate bits: D=3, A=2, I=1, F=0 -> clear I+F = 0x3
     */
    {
        UINT64 daif_before, daif_after;
        CHAR buf[200];
        __asm__ volatile("mrs %0, daif" : "=r"(daif_before));
        __asm__ __volatile__("msr daifclr, #0x3" ::: "memory");
        __asm__ volatile("isb");
        __asm__ volatile("mrs %0, daif" : "=r"(daif_after));
        RtlStringCbPrintfA(buf, sizeof(buf),
            "[arm64] DAIF before=0x%llx after=0x%llx (SError should be masked)",
            (unsigned long long)daif_before,
            (unsigned long long)daif_after);
        KiArm64BootStageLog(buf);
    }

    KfLowerIrql(DISPATCH_LEVEL);
    if (Pcr != NULL)
    {
        Pcr->CurrentIrql = DISPATCH_LEVEL;
    }
    /* removed redundant stage log: IRQL lowered (DPRINT1 covers it) */

    DPRINT1("[arm64] KiInitializeSystem: cpu %lu irql lowered to %lu\n",
            ProcessorNumber,
            (ULONG)DISPATCH_LEVEL);

    if (Pcr == NULL)
    {
        Pcr = KeArm64CurrentPcr;
    }

    __asm__ __volatile__("mrs %0, vbar_el1" : "=r"(VectorBase));
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    /* Emit a single KD-formatted banner (parity with amd64) */
    KdpDprintf("(%s:%d) Pcr = %p, Vbar = %p, TTBR0 = 0x%016llX, TTBR1 = 0x%016llX\n",
               __RELFILE__,
               __LINE__,
               (PVOID)Pcr,
               (PVOID)VectorBase,
               Ttbr0,
               Ttbr1);

    /* Also emit via DPRINT1 to validate DbgPrint path with masks enabled */
    DPRINT1("Pcr = %p, Vbar = %p, TTBR0 = 0x%016llX, TTBR1 = 0x%016llX\n",
            (PVOID)Pcr,
            (PVOID)VectorBase,
            Ttbr0,
            Ttbr1);
    /* KD already present */
        /* bring-up: avoid stray test print; rely on KD logs above */
    if ((InitialThread != NULL) && (Pcr != NULL))
    {
        DPRINT1("[arm64] KiInitializeSystem: cpu %lu entering KiInitializeKernel\n",
                ProcessorNumber);

        /* Switch to standard DPRINT1 (parity with amd64) */
        DPRINT1("[arm64] KiInitializeSystem: calling KiInitializeKernel\n");
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

    KiArm64BootStageLog("[arm64] KiInitializeSystem: entering idle loop");
    KiIdleLoop();
}
