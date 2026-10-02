/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC kernel startup
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef enum _KI_PPC_STARTUP_FAILURE
{
    KiPpcStartupNoFailure = 0,
    KiPpcStartupInvalidLoaderPointer,
    KiPpcStartupInvalidPayloadVersion,
    KiPpcStartupInvalidAddressLayout,
    KiPpcStartupInvalidHashTable,
    KiPpcStartupInvalidPcr,
    KiPpcStartupInvalidStack,
    KiPpcStartupPcrPublicationFailure,
    KiPpcStartupVectorFailure
} KI_PPC_STARTUP_FAILURE;

volatile ULONG KiPpcStartupFailure;
ULONG ProcessCount;

C_ASSERT(sizeof(KPROCESS) <= PAGE_SIZE);
C_ASSERT(sizeof(KTHREAD) <= PAGE_SIZE);

extern UCHAR KiPpcVectorBlob[], KiPpcVectorBlobEnd[];
extern IMAGE_DOS_HEADER __ImageBase;

static
VOID
KiPpcStartupPrintHex(_In_ ULONG Value)
{
    static const CHAR Digits[] = "0123456789ABCDEF";
    CHAR Buffer[8];
    ULONG Index;

    for (Index = 0; Index < 8; Index++)
        Buffer[7 - Index] = Digits[(Value >> (4 * Index)) & 0xF];
    KiPpcConsoleWrite(Buffer, sizeof(Buffer));
}

DECLSPEC_NORETURN
static
VOID
KiPpcStartupStop(_In_ KI_PPC_STARTUP_FAILURE Failure)
{
    static const CHAR Message[] = "\nKiPpcSystemStartup: fatal startup failure ";

    KiPpcStartupFailure = Failure;
    _disable();
    if (KiPpcConsoleReady())
    {
        KiPpcConsoleWrite(Message, sizeof(Message) - 1);
        KiPpcStartupPrintHex(Failure);
        KiPpcConsoleWrite("\n", 1);
    }
    for (;;)
        YieldProcessor();
}

static
BOOLEAN
KiPpcAddressInKseg0(_In_ ULONG_PTR Address, _In_ SIZE_T Size, _In_ PPPC_LOADER_BLOCK PpcBlock)
{
    ULONG_PTR Offset;

    if ((Size == 0) || (Address < PPC_LOADER_KSEG0_BASE))
        return FALSE;
    Offset = Address - PPC_LOADER_KSEG0_BASE;
    return (Offset <= PpcBlock->HighestMappedPhysicalAddress) && (Size - 1 <= PpcBlock->HighestMappedPhysicalAddress - Offset);
}

static
KI_PPC_STARTUP_FAILURE
KiPpcValidateLoaderBlock(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPPC_LOADER_BLOCK PpcBlock;

    if (((ULONG_PTR)LoaderBlock < PPC_LOADER_KSEG0_BASE) || ((ULONG_PTR)LoaderBlock >= PPC_LOADER_KSEG0_BASE + PPC_LOADER_KSEG0_SIZE))
        return KiPpcStartupInvalidLoaderPointer;

    PpcBlock = &LoaderBlock->u.PowerPC;
    if ((PpcBlock->Version != PPC_LOADER_BLOCK_VERSION) || (PpcBlock->Size != sizeof(*PpcBlock)))
        return KiPpcStartupInvalidPayloadVersion;
    if ((PpcBlock->Kseg0Base != PPC_LOADER_KSEG0_BASE) || (PpcBlock->Kseg0Size != PPC_LOADER_KSEG0_SIZE) || (PpcBlock->HighestMappedPhysicalAddress < PAGE_SIZE) || (PpcBlock->HighestMappedPhysicalAddress >= PPC_LOADER_KSEG0_SIZE) || (PpcBlock->PageTableRoot & (PAGE_SIZE - 1)) || (PpcBlock->PageTableRoot > PpcBlock->HighestMappedPhysicalAddress))
        return KiPpcStartupInvalidAddressLayout;
    if (!(PpcBlock->Flags & PPC_LOADER_FLAG_HASH_TABLE) || (PpcBlock->HashTableSize < 0x10000) || (PpcBlock->HashTableSize & (PpcBlock->HashTableSize - 1)) || (PpcBlock->HashTable & (PpcBlock->HashTableSize - 1)) || !KiPpcAddressInKseg0(PPC_LOADER_KSEG0_BASE + PpcBlock->HashTable, PpcBlock->HashTableSize, PpcBlock))
        return KiPpcStartupInvalidHashTable;
    if ((PpcBlock->PcrPage & (PAGE_SIZE - 1)) || !KiPpcAddressInKseg0(PPC_LOADER_KSEG0_BASE + PpcBlock->PcrPage, sizeof(KPCR), PpcBlock))
        return KiPpcStartupInvalidPcr;
    if ((PpcBlock->DpcStack & 15) || !KiPpcAddressInKseg0(PpcBlock->DpcStack - KERNEL_STACK_SIZE, KERNEL_STACK_SIZE, PpcBlock) || (LoaderBlock->KernelStack & 15) || !KiPpcAddressInKseg0(LoaderBlock->KernelStack - KERNEL_STACK_SIZE, KERNEL_STACK_SIZE, PpcBlock) || !KiPpcAddressInKseg0(LoaderBlock->Thread, sizeof(KTHREAD), PpcBlock) || !KiPpcAddressInKseg0(LoaderBlock->Process, sizeof(KPROCESS), PpcBlock))
        return KiPpcStartupInvalidStack;
    return KiPpcStartupNoFailure;
}

/* Copy the vector code to physical 0 (through KSEG0) and make it visible to
 * instruction fetch. Real-mode vectors see it at its physical address. */
BOOLEAN
NTAPI
KiPpcInstallVectors(VOID)
{
    SIZE_T Size = KiPpcVectorBlobEnd - KiPpcVectorBlob;
    PVOID Target = (PVOID)PPC_LOADER_KSEG0_BASE;

    if ((Size == 0) || (Size > 0x3000) || (KiPpcReadMsr() & MSR_EE))
        return FALSE;
    RtlCopyMemory(Target, KiPpcVectorBlob, Size);
    KeSweepICache(Target, Size);
    return RtlCompareMemory(Target, KiPpcVectorBlob, Size) == Size;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiPpcSystemStartup(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    static DECLSPEC_ALIGN(16) UCHAR KiPpcPanicStack[KERNEL_STACK_SIZE];
    KI_PPC_STARTUP_FAILURE Failure;
    PPPC_LOADER_BLOCK PpcBlock;
    PKPROCESS InitialProcess;
    PKTHREAD InitialThread;
    PKPCR Pcr;
    PKPRCB Prcb;
    ULONG_PTR DirectoryTableBase;

    Failure = KiPpcValidateLoaderBlock(LoaderBlock);
    PpcBlock = &LoaderBlock->u.PowerPC;
    if (Failure == KiPpcStartupNoFailure || Failure > KiPpcStartupInvalidPayloadVersion)
        KiPpcConsoleInitialize(LoaderBlock);
    if (Failure != KiPpcStartupNoFailure)
        KiPpcStartupStop(Failure);

    KiPpcConsoleWrite("\nReactOS PowerPC kernel starting, image base ", 44);
    KiPpcStartupPrintHex((ULONG_PTR)&__ImageBase);
    KiPpcConsoleWrite("\n", 1);

    InitialProcess = (PKPROCESS)LoaderBlock->Process;
    InitialThread = (PKTHREAD)LoaderBlock->Thread;
    InitialThread->ApcState.Process = InitialProcess;
    KeLoaderBlock = LoaderBlock;
    KeMemoryBarrier();

    Pcr = (PKPCR)(PPC_LOADER_KSEG0_BASE + PpcBlock->PcrPage);
    if (!KiPpcInitializeBootPcr(Pcr, InitialThread, (PVOID)PpcBlock->DpcStack, KiPpcPanicStack + sizeof(KiPpcPanicStack)))
        KiPpcStartupStop(KiPpcStartupPcrPublicationFailure);
    Pcr->PageTableRoot = PpcBlock->PageTableRoot;
    Pcr->HashTable = PpcBlock->HashTable;
    Pcr->HashTableMask = (PpcBlock->HashTableSize >> 6) - 1;
    Pcr->InitialStack = LoaderBlock->KernelStack;
    LoaderBlock->Prcb = (ULONG_PTR)&Pcr->Prcb;

    if (!KiPpcInstallVectors())
        KiPpcStartupStop(KiPpcStartupVectorFailure);
    KiPpcConsoleWrite("ReactOS PowerPC kernel: vectors installed\n", 42);
    KiPpcIdentifyProcessor(LoaderBlock);

    KeSetDmaIoCoherency(1);
    Prcb = KeGetCurrentPrcb();
    KiNode0.ProcessorMask |= Prcb->SetMember;
    PoInitializePrcb(Prcb);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    ExInitPoolLookasidePointers();

    /* Portable initialization takes locks at APC level. */
    KfLowerIrql(APC_LEVEL);
    MiInitializeKernelVaLayout(LoaderBlock);
    KiInitSystem();

    DirectoryTableBase = PpcBlock->PageTableRoot;
    InitializeListHead(&KiProcessListHead);
    KeInitializeProcess(InitialProcess, 0, MAXULONG_PTR, &DirectoryTableBase, FALSE);
    InitialProcess->QuantumReset = MAXCHAR;

    KeInitializeThread(InitialProcess, InitialThread, NULL, NULL, NULL, NULL, NULL, (PVOID)LoaderBlock->KernelStack);
    InitialThread->NextProcessor = 0;
    InitialThread->IdealProcessor = 0;
    InitialThread->Priority = HIGH_PRIORITY;
    InitialThread->State = Running;
#if (NTDDI_VERSION >= NTDDI_WIN7)
    InitialThread->Running = TRUE;
#endif
    RtlZeroMemory(&InitialThread->Affinity, sizeof(InitialThread->Affinity));
    InitialThread->Affinity.Mask = 1;
    InitialThread->WaitIrql = DISPATCH_LEVEL;
    InitialProcess->ActiveProcessors = 1;
    Prcb->CurrentThread = InitialThread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = InitialThread;

    KdInitSystem(0, LoaderBlock);
    ExpInitializeExecutive(0, LoaderBlock);

    /* Phase 0 created the phase-1 thread. Dropping the boot thread to idle
     * priority selects it; the idle loop performs the first switch. */
    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(InitialThread, 0);
    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
        InterlockedOr((PLONG)&KiIdleSummary, Prcb->SetMember);
    KiReleasePrcbLock(Prcb);

    KfRaiseIrql(HIGH_LEVEL);
    LoaderBlock->Prcb = 0;
    InitialThread->Priority = 0;
    KfLowerIrql(DISPATCH_LEVEL);
    InitialThread->WaitIrql = DISPATCH_LEVEL;
    _enable();
    KiIdleLoop();
}

VOID
NTAPI
KiInitMachineDependent(VOID)
{
    PKPCR Pcr = KeGetPcr();

    if ((Pcr->Prcb.Number >= (ULONG)(UCHAR)KeNumberProcessors) || (KiProcessorBlock[Pcr->Prcb.Number] != &Pcr->Prcb))
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, STATUS_INVALID_DEVICE_STATE, (ULONG_PTR)Pcr, 0, 0);
}
