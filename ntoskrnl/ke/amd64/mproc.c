/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Architecture specific source file to hold multiprocessor functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ****************************************************************/

CODE_SEG("INIT")
VOID
KiInitializeProcessorBootStructures(
    _In_ ULONG ProcessorNumber,
    _Out_ PKIPCR Pcr,
    _In_ PKGDTENTRY64 GdtBase,
    _In_ PKIDTENTRY64 IdtBase,
    _In_ PKTSS64 TssBase,
    _In_ PKTHREAD IdleThread,
    _In_ PVOID KernelStack,
    _In_ PVOID DpcStack,
    _In_ PVOID DoubleFaultStack,
    _In_ PVOID NmiStack);

typedef struct _APINFO
{
    DECLSPEC_ALIGN(PAGE_SIZE) KIDTENTRY64 Idt[256];
    DECLSPEC_ALIGN(PAGE_SIZE) KGDTENTRY64 Gdt[128];
    DECLSPEC_ALIGN(16) UINT8 DoubleFaultStack[DOUBLE_FAULT_STACK_SIZE];
    DECLSPEC_ALIGN(16) UINT8 NmiStack[DOUBLE_FAULT_STACK_SIZE];
    KIPCR Pcr;
    ETHREAD Thread;
    KTSS64 Tss;
} APINFO, *PAPINFO;

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
KeStartAllProcessors(VOID)
{
    PVOID KernelStack = NULL;
    PVOID DPCStack = NULL;
    PAPINFO APInfo = NULL;
    PKPROCESSOR_STATE ProcessorState;
    KDESCRIPTOR Gdtr, Idtr;
    ULONG ProcessorCount;
    ULONG MaximumProcessors;

    MaximumProcessors = KeMaximumProcessors;
    if (KeNumprocSpecified)
        MaximumProcessors = min(MaximumProcessors, KeNumprocSpecified);
    if (KeBootprocSpecified)
        MaximumProcessors = min(MaximumProcessors, KeBootprocSpecified);

    /* Start at 1, the boot processor is already running */
    for (ProcessorCount = 1; ProcessorCount < MaximumProcessors; ++ProcessorCount)
    {
        KernelStack = NULL;
        DPCStack = NULL;

        APInfo = ExAllocatePoolZero(NonPagedPool, sizeof(*APInfo), TAG_KERNEL);
        if (!APInfo)
            break;
        ASSERT(ALIGN_DOWN_POINTER_BY(APInfo, PAGE_SIZE) == APInfo);

        KernelStack = MmCreateKernelStack(FALSE, 0);
        if (!KernelStack)
            break;

        DPCStack = MmCreateKernelStack(FALSE, 0);
        if (!DPCStack)
            break;

        /* Clone the boot processor's descriptor tables */
        __sgdt(&Gdtr.Limit);
        __sidt(&Idtr.Limit);
        RtlCopyMemory(APInfo->Gdt, Gdtr.Base, Gdtr.Limit + 1);
        RtlCopyMemory(APInfo->Idt, Idtr.Base, Idtr.Limit + 1);

        /* Initialize the PCR, TSS and the GDT TSS descriptor for this CPU */
        KiInitializeProcessorBootStructures(ProcessorCount,
                                            &APInfo->Pcr,
                                            APInfo->Gdt,
                                            APInfo->Idt,
                                            &APInfo->Tss,
                                            (PKTHREAD)&APInfo->Thread,
                                            KernelStack,
                                            DPCStack,
                                            &APInfo->DoubleFaultStack[DOUBLE_FAULT_STACK_SIZE],
                                            &APInfo->NmiStack[DOUBLE_FAULT_STACK_SIZE]);

        /* Build the processor state the trampoline restores in long mode */
        ProcessorState = &APInfo->Pcr.Prcb.ProcessorState;
        RtlZeroMemory(ProcessorState, sizeof(*ProcessorState));

        ProcessorState->SpecialRegisters.Cr0 = __readcr0();
        ProcessorState->SpecialRegisters.Cr3 = __readcr3();
        ProcessorState->SpecialRegisters.Cr4 = __readcr4();

        ProcessorState->ContextFrame.SegCs = KGDT64_R0_CODE;
        ProcessorState->ContextFrame.SegDs = KGDT64_R0_DATA;
        ProcessorState->ContextFrame.SegEs = KGDT64_R0_DATA;
        ProcessorState->ContextFrame.SegSs = KGDT64_R0_DATA;
        ProcessorState->ContextFrame.SegFs = KGDT64_R0_DATA;
        ProcessorState->ContextFrame.SegGs = KGDT64_R0_DATA;

        ProcessorState->SpecialRegisters.Gdtr.Base = (PVOID)APInfo->Gdt;
        ProcessorState->SpecialRegisters.Gdtr.Limit = sizeof(APInfo->Gdt) - 1;
        ProcessorState->SpecialRegisters.Idtr.Base = (PVOID)APInfo->Idt;
        ProcessorState->SpecialRegisters.Idtr.Limit = sizeof(APInfo->Idt) - 1;
        ProcessorState->SpecialRegisters.Tr = KGDT64_SYS_TSS;

        ProcessorState->ContextFrame.Rsp = ALIGN_DOWN_BY((ULONG64)KernelStack, 16) - 8;
        ProcessorState->ContextFrame.Rip = (ULONG64)(ULONG_PTR)KiSystemStartup;
        ProcessorState->ContextFrame.Rcx = (ULONG64)(ULONG_PTR)KeLoaderBlock;
        ProcessorState->ContextFrame.EFlags = __readeflags() & ~EFLAGS_INTERRUPT_MASK;

        /* Hand the new processor's data to the loader block */
        KeLoaderBlock->KernelStack = (ULONG_PTR)KernelStack;
        KeLoaderBlock->Prcb = (ULONG_PTR)&APInfo->Pcr.Prcb;
        KeLoaderBlock->Thread = (ULONG_PTR)&APInfo->Thread;

        DPRINT1("KeStartAllProcessors: starting CPU %lu\n", ProcessorCount);
        if (!HalStartNextProcessor(KeLoaderBlock, ProcessorState))
            break;

        /* Wait for the AP to consume the loader block (KiInitializeKernel) */
        while (KeLoaderBlock->Prcb != 0)
        {
            KeMemoryBarrier();
            YieldProcessor();
        }
    }

    /* The last allocation belongs to a CPU that did not start */
    ProcessorCount--;

    if (APInfo)
        ExFreePoolWithTag(APInfo, TAG_KERNEL);
    if (KernelStack)
        MmDeleteKernelStack(KernelStack, FALSE);
    if (DPCStack)
        MmDeleteKernelStack(DPCStack, FALSE);

    DPRINT1("KeStartAllProcessors: started %lu processor(s)\n", ProcessorCount);
}
