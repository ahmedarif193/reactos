/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V boot-hart loader contract adoption
 */

#include <ntoskrnl.h>
#include <reactos/riscv64/fdt.h>
#define NDEBUG
#include <debug.h>

/* These values are kernel-private diagnostics for a debugger attached before
 * any post-firmware console exists. They are not an NT ABI or a boot result. */
typedef enum _KI_RISCV_STARTUP_FAILURE
{
    KiRiscvStartupNoFailure = 0,
    KiRiscvStartupInvalidLoaderPointer,
    KiRiscvStartupInvalidPayloadVersion,
    KiRiscvStartupInvalidPayloadFlags,
    KiRiscvStartupInvalidBootProtocol,
    KiRiscvStartupInvalidAddressLayout,
    KiRiscvStartupInvalidSatp,
    KiRiscvStartupInvalidPageTableRoot,
    KiRiscvStartupInvalidLoaderMapping,
    KiRiscvStartupInvalidPcrMapping,
    KiRiscvStartupInvalidThreadMapping,
    KiRiscvStartupInvalidProcessMapping,
    KiRiscvStartupInvalidStackMapping,
    KiRiscvStartupInvalidSharedDataMapping,
    KiRiscvStartupInvalidDeviceTreeMapping,
    KiRiscvStartupInvalidDeviceTreeHeader,
    KiRiscvStartupPcrPublicationFailure,
    KiRiscvStartupTrapVectorFailure
} KI_RISCV_STARTUP_FAILURE;

typedef enum _KI_RISCV_STARTUP_PHASE
{
    KiRiscvStartupEntered = 1,
    KiRiscvStartupPayloadValidated,
    KiRiscvStartupMappingsValidated,
    KiRiscvStartupPcrPublished,
    KiRiscvStartupTrapVectorInstalled,
    KiRiscvStartupPortableKernelInitialized,
    KiRiscvStartupInitialThreadInitialized,
    KiRiscvStartupExecutiveEntered
} KI_RISCV_STARTUP_PHASE;

volatile ULONG KiRiscvStartupPhase;
volatile ULONG KiRiscvStartupFailure;
ULONG ProcessCount;

C_ASSERT(sizeof(KPROCESS) <= PAGE_SIZE);
C_ASSERT(sizeof(KTHREAD) <= PAGE_SIZE);
C_ASSERT(sizeof(EPROCESS) <= PAGE_SIZE);
C_ASSERT(sizeof(ETHREAD) <= PAGE_SIZE);

static
VOID
KiRiscvStartupPrintHex(
    _In_ ULONG_PTR Value)
{
    static const CHAR Digits[] = "0123456789ABCDEF";
    CHAR Buffer[2 * sizeof(ULONG_PTR)];
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Buffer); Index++)
        Buffer[RTL_NUMBER_OF(Buffer) - 1 - Index] = Digits[(Value >> (4 * Index)) & 0xF];
    KiRiscvConsoleWrite(Buffer, sizeof(Buffer));
}

static
DECLSPEC_NORETURN
VOID
KiRiscvStartupStop(
    _In_ KI_RISCV_STARTUP_FAILURE Failure)
{
    static const CHAR Message[] = "\r\nKiRiscvSystemStartup: fatal startup failure ";
    static const CHAR PhaseText[] = " in phase ";
    static const CHAR Tail[] = ". Halting.\r\n";

    KiRiscvStartupFailure = Failure;
    __asm__ __volatile__("fence rw, rw\n\t"
                         "csrci sstatus, 2\n\t"
                         "csrw sie, zero" ::: "memory");

    /* The console is up right after payload validation; earlier failures
     * remain visible only through the KiRiscvStartupFailure variable. */
    if (KiRiscvConsoleReady())
    {
        KiRiscvConsoleWrite(Message, sizeof(Message) - 1);
        KiRiscvStartupPrintHex(Failure);
        KiRiscvConsoleWrite(PhaseText, sizeof(PhaseText) - 1);
        KiRiscvStartupPrintHex(KiRiscvStartupPhase);
        KiRiscvConsoleWrite(Tail, sizeof(Tail) - 1);
    }
    for (;;)
        __asm__ __volatile__("wfi" ::: "memory");
}

static
BOOLEAN
KiRiscvAddressInKseg0(
    _In_ ULONG_PTR Address,
    _In_ SIZE_T Size)
{
    ULONG64 Offset;

    if ((Size == 0) || !MiRiscvIsCanonicalAddress(Address) ||
        (Address < RISCV64_LOADER_KSEG0_BASE))
    {
        return FALSE;
    }

    Offset = Address - RISCV64_LOADER_KSEG0_BASE;
    return (Offset < RISCV64_LOADER_PHYSICAL_LIMIT) &&
           (Size <= RISCV64_LOADER_PHYSICAL_LIMIT - Offset);
}

/* Loader data must be a global, supervisor-only, read/write, non-executable
 * leaf mapping the matching physical page. */
static
BOOLEAN
KiRiscvValidateDataPage(
    _In_ PRISCV64_LOADER_BLOCK RiscvBlock,
    _In_ ULONG_PTR Address,
    _In_ ULONG64 PhysicalAddress)
{
    MI_RISCV_PAGE_WALK Walk;

    return NT_SUCCESS(MiRiscvWalkPageTables(RiscvBlock->PageTableRoot >> PAGE_SHIFT, Address, &Walk)) &&
           (Walk.PhysicalAddress.QuadPart == PhysicalAddress) &&
           Walk.Value.u.Hard.Read && Walk.Value.u.Hard.Write &&
           !Walk.Value.u.Hard.Execute && !Walk.Value.u.Hard.Owner && Walk.Global;
}

static
BOOLEAN
KiRiscvValidateMappedRange(
    _In_ PRISCV64_LOADER_BLOCK RiscvBlock,
    _In_ ULONG_PTR Address,
    _In_ SIZE_T Size)
{
    ULONG_PTR Current, Last;

    if (!KiRiscvAddressInKseg0(Address, Size))
        return FALSE;

    Last = Address + Size - 1;
    if ((Address - RiscvBlock->Kseg0Base >
         RiscvBlock->HighestMappedPhysicalAddress) ||
        (Last - RiscvBlock->Kseg0Base >
         RiscvBlock->HighestMappedPhysicalAddress))
    {
        return FALSE;
    }

    for (Current = ALIGN_DOWN_BY(Address, PAGE_SIZE); ; Current += PAGE_SIZE)
    {
        if (!KiRiscvValidateDataPage(RiscvBlock, Current, Current - RiscvBlock->Kseg0Base))
            return FALSE;

        if (Current >= ALIGN_DOWN_BY(Last, PAGE_SIZE))
            break;
    }

    return TRUE;
}

static
KI_RISCV_STARTUP_FAILURE
KiRiscvValidateLoaderBlock(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRISCV64_LOADER_BLOCK RiscvBlock;
    ULONG_PTR Status, Satp, StackBase;
    ULONG DeviceTreeSize;

    if (!KiRiscvAddressInKseg0((ULONG_PTR)LoaderBlock,
                               sizeof(*LoaderBlock)))
    {
        return KiRiscvStartupInvalidLoaderPointer;
    }

    RiscvBlock = &LoaderBlock->u.Riscv64;
    if ((RiscvBlock->Version != RISCV64_LOADER_BLOCK_VERSION) ||
        (RiscvBlock->Size != sizeof(*RiscvBlock)))
    {
        return KiRiscvStartupInvalidPayloadVersion;
    }
    /* Every required flag must be set; only the optional ones may be added. */
    if (((RiscvBlock->Flags & RISCV64_LOADER_REQUIRED_FLAGS) !=
         RISCV64_LOADER_REQUIRED_FLAGS) ||
        (RiscvBlock->Flags &
         ~(RISCV64_LOADER_REQUIRED_FLAGS | RISCV64_LOADER_OPTIONAL_FLAGS)))
    {
        return KiRiscvStartupInvalidPayloadFlags;
    }
    if (RiscvBlock->FirmwareBootProtocolRevision <
        RISCV64_LOADER_BOOT_PROTOCOL_MINIMUM_REVISION)
    {
        return KiRiscvStartupInvalidBootProtocol;
    }
    if ((RiscvBlock->Kseg0Base != RISCV64_LOADER_KSEG0_BASE) ||
        (RiscvBlock->DirectMapBase != RISCV64_LOADER_DIRECT_MAP_BASE) ||
        (RiscvBlock->PhysicalLimit != RISCV64_LOADER_PHYSICAL_LIMIT) ||
        (RiscvBlock->HighestMappedPhysicalAddress < PAGE_SIZE) ||
        (RiscvBlock->HighestMappedPhysicalAddress >=
         RiscvBlock->PhysicalLimit))
    {
        return KiRiscvStartupInvalidAddressLayout;
    }

    /* Every table page is RAM reached through the direct map. */
    MiRiscvInitializePageTableAccess(RiscvBlock->HighestMappedPhysicalAddress >> PAGE_SHIFT);

    __asm__ __volatile__("csrr %0, sstatus\n\tcsrr %1, satp"
                         : "=r"(Status), "=r"(Satp) :: "memory");
    if ((Status & RISCV_SSTATUS_SIE) ||
        (Satp != RiscvBlock->Satp) ||
        ((Satp & RISCV64_LOADER_SATP_MODE_MASK) !=
         RISCV64_LOADER_SATP_MODE_SV39) ||
        (Satp & RISCV64_LOADER_SATP_ASID_MASK))
    {
        return KiRiscvStartupInvalidSatp;
    }
    if ((RiscvBlock->PageTableRoot & (PAGE_SIZE - 1)) ||
        (RiscvBlock->PageTableRoot >
         RiscvBlock->HighestMappedPhysicalAddress) ||
        (PAGE_SIZE - 1 > RiscvBlock->HighestMappedPhysicalAddress -
                         RiscvBlock->PageTableRoot) ||
        ((Satp & RISCV64_LOADER_SATP_PPN_MASK) !=
         (RiscvBlock->PageTableRoot >> PAGE_SHIFT)))
    {
        return KiRiscvStartupInvalidPageTableRoot;
    }

    KiRiscvStartupPhase = KiRiscvStartupPayloadValidated;
    if (!KiRiscvValidateMappedRange(RiscvBlock,
                                    (ULONG_PTR)LoaderBlock,
                                    sizeof(*LoaderBlock)))
    {
        return KiRiscvStartupInvalidLoaderMapping;
    }
    if ((RiscvBlock->PcrPage & (PAGE_SIZE - 1)) ||
        (LoaderBlock->Prcb !=
         RiscvBlock->PcrPage + FIELD_OFFSET(KPCR, Prcb)) ||
        !KiRiscvValidateMappedRange(RiscvBlock,
                                    RiscvBlock->PcrPage,
                                    sizeof(KPCR)))
    {
        return KiRiscvStartupInvalidPcrMapping;
    }
    if ((RiscvBlock->DpcStack & 15) ||
        (RiscvBlock->DpcStack <
         RiscvBlock->Kseg0Base + KERNEL_STACK_SIZE) ||
        !KiRiscvValidateMappedRange(RiscvBlock,
                                    RiscvBlock->DpcStack - KERNEL_STACK_SIZE,
                                    KERNEL_STACK_SIZE))
    {
        return KiRiscvStartupInvalidStackMapping;
    }
    if ((LoaderBlock->Thread & (PAGE_SIZE - 1)) ||
        !KiRiscvValidateMappedRange(RiscvBlock,
                                    LoaderBlock->Thread,
                                    sizeof(KTHREAD)))
    {
        return KiRiscvStartupInvalidThreadMapping;
    }
    if ((LoaderBlock->Process & (PAGE_SIZE - 1)) ||
        !KiRiscvValidateMappedRange(RiscvBlock,
                                    LoaderBlock->Process,
                                    sizeof(KPROCESS)))
    {
        return KiRiscvStartupInvalidProcessMapping;
    }
    if ((LoaderBlock->KernelStack & 15) ||
        (LoaderBlock->KernelStack <
         RiscvBlock->Kseg0Base + KERNEL_STACK_SIZE))
    {
        return KiRiscvStartupInvalidStackMapping;
    }
    StackBase = LoaderBlock->KernelStack - KERNEL_STACK_SIZE;
    if (!KiRiscvValidateMappedRange(RiscvBlock,
                                    StackBase,
                                    KERNEL_STACK_SIZE))
    {
        return KiRiscvStartupInvalidStackMapping;
    }
#if (NTDDI_VERSION >= NTDDI_WIN8)
    if (LoaderBlock->KernelStackSize != KERNEL_STACK_SIZE)
        return KiRiscvStartupInvalidStackMapping;
#endif
    if ((RiscvBlock->SharedUserDataPage & (PAGE_SIZE - 1)) ||
        (RiscvBlock->SharedUserDataPage > RiscvBlock->HighestMappedPhysicalAddress) ||
        !KiRiscvValidateDataPage(RiscvBlock,
                                 KI_USER_SHARED_DATA,
                                 RiscvBlock->SharedUserDataPage))
    {
        return KiRiscvStartupInvalidSharedDataMapping;
    }
    if ((RiscvBlock->DeviceTree & (PAGE_SIZE - 1)) ||
        (RiscvBlock->DeviceTreeSize < RISCV_FDT_HEADER_SIZE) ||
        (RiscvBlock->DeviceTreeSize > RISCV_FDT_MAXIMUM_SIZE) ||
        !KiRiscvValidateMappedRange(RiscvBlock,
                                    RiscvBlock->DeviceTree,
                                    (SIZE_T)RiscvBlock->DeviceTreeSize))
    {
        return KiRiscvStartupInvalidDeviceTreeMapping;
    }
    if (!RiscvFdtValidateHeader(
            (PVOID)(ULONG_PTR)RiscvBlock->DeviceTree,
            (SIZE_T)RiscvBlock->DeviceTreeSize,
            &DeviceTreeSize) ||
        (DeviceTreeSize != RiscvBlock->DeviceTreeSize))
    {
        return KiRiscvStartupInvalidDeviceTreeHeader;
    }

    KiRiscvStartupPhase = KiRiscvStartupMappingsValidated;
    return KiRiscvStartupNoFailure;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiRiscvSystemStartup(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    KI_RISCV_STARTUP_FAILURE Failure;
    PRISCV64_LOADER_BLOCK RiscvBlock;
    PKPROCESS InitialProcess;
    PKTHREAD InitialThread;
    PKPRCB Prcb;
    ULONG_PTR DirectoryTableBase[2];

    KiRiscvStartupPhase = KiRiscvStartupEntered;
    KiRiscvStartupFailure = KiRiscvStartupNoFailure;
    Failure = KiRiscvValidateLoaderBlock(LoaderBlock);
    if (Failure != KiRiscvStartupNoFailure)
        KiRiscvStartupStop(Failure);

    /* Bring up the firmware-described console first so every later failure
     * can print. A missing console is not fatal (ABI-126). */
    RiscvBlock = &LoaderBlock->u.Riscv64;
    KiRiscvConsoleInitialize(LoaderBlock);
    KiRiscvIdentifyProcessor(LoaderBlock);

    InitialProcess = (PKPROCESS)(ULONG_PTR)LoaderBlock->Process;
    InitialThread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;
    InitialThread->ApcState.Process = InitialProcess;
    KeLoaderBlock = LoaderBlock;
    KeMemoryBarrier();

    if (!KiRiscvInitializeBootPcr(
            (PKPCR)(ULONG_PTR)RiscvBlock->PcrPage,
            InitialThread,
            (ULONG_PTR)RiscvBlock->BootHartId,
            (PVOID)(ULONG_PTR)RiscvBlock->DpcStack))
    {
        KiRiscvStartupStop(KiRiscvStartupPcrPublicationFailure);
    }
    KiRiscvStartupPhase = KiRiscvStartupPcrPublished;

    if (!KiRiscvInitializeTrapVector())
        KiRiscvStartupStop(KiRiscvStartupTrapVectorFailure);
    KiRiscvStartupPhase = KiRiscvStartupTrapVectorInstalled;

    /* ABI-132: NT kernel code reads and writes user buffers directly (under
     * probing and SEH), so supervisor access to U pages is permitted for the
     * lifetime of the system. MXR stays clear: execute-only is not readable. */
    __asm__ __volatile__("li t0, 0x40000\n\tcsrs sstatus, t0\n\tli t0, 0x80000\n\tcsrc sstatus, t0" ::: "t0", "memory");

    /* Report the native ReactOS RISC-V64 architecture. Level, revision, and
     * portable x86 feature bits are not inferred from an ISA string or
     * inaccessible machine CSRs. */
    KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_RISCV64;
    KeProcessorLevel = 0;
    KeProcessorRevision = 0;
    KeFeatureBits = 0;
    KeSetDmaIoCoherency(0);

    Prcb = KeGetCurrentPrcb();
    KiNode0.ProcessorMask |= Prcb->SetMember;
    PoInitializePrcb(Prcb);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    ExInitPoolLookasidePointers();

    /* Portable initialization takes locks at APC level. Interrupt delivery
     * remains globally disabled until the HAL has installed real sources. */
    KfLowerIrql(APC_LEVEL);
    MiInitializeKernelVaLayout(LoaderBlock);
    KiInitSystem();
    KiRiscvStartupPhase = KiRiscvStartupPortableKernelInitialized;

    /* One Sv39 root owns both canonical halves. Store its physical address in
     * the existing process root field; the second generic slot stays zero and
     * no ASID is allocated during boot-process adoption. */
    DirectoryTableBase[0] = RiscvBlock->PageTableRoot;
    DirectoryTableBase[1] = 0;
    InitializeListHead(&KiProcessListHead);
    KeInitializeProcess(InitialProcess,
                        0,
                        MAXULONG_PTR,
                        DirectoryTableBase,
                        FALSE);
    InitialProcess->QuantumReset = MAXCHAR;

    /* This is the already executing boot thread. The architecture context
     * initializer accepts only that special case until resumable start and
     * switch frames are implemented. */
    KeInitializeThread(InitialProcess,
                       InitialThread,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       (PVOID)(ULONG_PTR)LoaderBlock->KernelStack);
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
    KiRiscvStartupPhase = KiRiscvStartupInitialThreadInitialized;

    /* Same point as the x64 KiInitializeKernel: the debugger (and with it
     * every DPRINT) becomes live before the executive starts. */
    KdInitSystem(0, LoaderBlock);
    KiRiscvReportProcessorFeatures();

    KiRiscvStartupPhase = KiRiscvStartupExecutiveEntered;
    ExpInitializeExecutive(0, LoaderBlock);

    /* Phase 0 created the phase-1 thread on the ready list. Dropping the boot
     * thread to the idle priority selects it as Prcb->NextThread; the idle
     * loop then performs the first real context switch (amd64 sequence). */
    KfRaiseIrql(DISPATCH_LEVEL);
    KeSetPriorityThread(InitialThread, 0);
    KiAcquirePrcbLock(Prcb);
    if (Prcb->NextThread == NULL)
        InterlockedOr64((PLONG64)&KiIdleSummary, Prcb->SetMember);
    KiReleasePrcbLock(Prcb);

    KfRaiseIrql(HIGH_LEVEL);
    LoaderBlock->Prcb = 0;
    InitialThread->Priority = 0;

    /* Interrupt delivery stays masked until the HAL enables a source; the
     * idle loop unmasks around wfi only once one exists. */
    KfLowerIrql(DISPATCH_LEVEL);
    InitialThread->WaitIrql = DISPATCH_LEVEL;
    KiIdleLoop();
}

VOID
NTAPI
KiInitMachineDependent(VOID)
{
    ULONG_PTR Satp, Vector;
    PKPCR Pcr = KeGetPcr();

    __asm__ __volatile__("csrr %0, satp\n\tcsrr %1, stvec"
                         : "=r"(Satp), "=r"(Vector) :: "memory");
    if ((Pcr->Prcb.Number >= (ULONG)(UCHAR)KeNumberProcessors) ||
        (KiProcessorBlock[Pcr->Prcb.Number] != &Pcr->Prcb) ||
        ((Satp & RISCV64_LOADER_SATP_MODE_MASK) !=
         RISCV64_LOADER_SATP_MODE_SV39) ||
        (Vector != (ULONG_PTR)KiRiscvTrapEntry))
    {
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED,
                     STATUS_INVALID_DEVICE_STATE,
                     Satp,
                     Vector,
                     (ULONG_PTR)Pcr);
    }
}
