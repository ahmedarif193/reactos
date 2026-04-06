/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 multiprocessor startup orchestration
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2025 ReactOS Team
 *
 * DESCRIPTION:
 *     Allocates per-AP resources (PCR, PRCB, GDT, IDT, TSS, stacks) and
 *     calls HalStartNextProcessor() to spin up each Application Processor.
 *     Once the AP enters KiSystemStartup(), it signals completion by
 *     zeroing LoaderBlock->Prcb.
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* Forward declaration for KiInitializeProcessorBootStructures (in kiinit.c) */
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

/*
 * Per-AP allocation block.
 * Contains all the structures that an AP needs to boot and run.
 * Allocated as a single pool block for simplicity and to ensure
 * proper page alignment.
 */
typedef struct _APINFO
{
    /* IDT: Must be page-aligned (256 entries of KIDTENTRY64) */
    DECLSPEC_ALIGN(PAGE_SIZE) KIDTENTRY64 Idt[256];

    /* GDT: 16-byte aligned for best performance. Enough entries for 64-bit kernel. */
    DECLSPEC_ALIGN(16) KGDTENTRY64 Gdt[128];

    /* Stacks for double-fault and NMI handling */
    DECLSPEC_ALIGN(16) UINT8 DoubleFaultStackData[DOUBLE_FAULT_STACK_SIZE];
    DECLSPEC_ALIGN(16) UINT8 NmiStackData[DOUBLE_FAULT_STACK_SIZE];

    /* Per-processor structures */
    KIPCR Pcr;
    ETHREAD IdleThread;
    KTSS64 Tss;
} APINFO, *PAPINFO;

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
KeStartAllProcessors(VOID)
{
    PVOID KernelStack, DPCStack;
    PAPINFO APInfo;
    ULONG ProcessorCount;
    ULONG MaximumProcessors;
    PKPROCESSOR_STATE ProcessorState;
    KDESCRIPTOR BspGdt, BspIdt;
    PKGDTENTRY64 TssEntry;
    ULONG64 DoubleFaultStackTop, NmiStackTop;

    /*
     * Determine the maximum number of processors to start.
     * NOTE: NT6+ HAL exports HalEnumerateProcessors() and
     * HalQueryMaximumProcessorCount() but we don't use those yet.
     */
    MaximumProcessors = KeMaximumProcessors;

    /* Runtime limit */
    if (KeNumprocSpecified)
        MaximumProcessors = min(MaximumProcessors, KeNumprocSpecified);

    /* Boot-time limit */
    if (KeBootprocSpecified)
        MaximumProcessors = min(MaximumProcessors, KeBootprocSpecified);

    DPRINT1("KeStartAllProcessors: Max processors = %lu\n", MaximumProcessors);

    /* Start ProcessorCount at 1 because the BSP is already running */
    for (ProcessorCount = 1; ProcessorCount < MaximumProcessors; ++ProcessorCount)
    {
        KernelStack = NULL;
        DPCStack = NULL;
        APInfo = NULL;

        /*
         * Allocate the per-AP structure block.
         * ExAllocatePoolZero returns page-aligned memory for allocations
         * that contain PAGE_SIZE-aligned members.
         */
        APInfo = ExAllocatePoolZero(NonPagedPool, sizeof(*APInfo), TAG_KERNEL);
        if (!APInfo)
        {
            DPRINT1("KeStartAllProcessors: Failed to allocate APINFO for CPU %lu\n",
                    ProcessorCount);
            break;
        }
        ASSERT(ALIGN_DOWN_POINTER_BY(APInfo, PAGE_SIZE) == APInfo);

        /* Allocate the kernel stack for the AP's idle thread */
        KernelStack = MmCreateKernelStack(FALSE, 0);
        if (!KernelStack)
        {
            DPRINT1("KeStartAllProcessors: Failed to allocate kernel stack for CPU %lu\n",
                    ProcessorCount);
            break;
        }

        /* Allocate the DPC stack for the AP */
        DPCStack = MmCreateKernelStack(FALSE, 0);
        if (!DPCStack)
        {
            DPRINT1("KeStartAllProcessors: Failed to allocate DPC stack for CPU %lu\n",
                    ProcessorCount);
            break;
        }

        /*
         * Initialize the PCR for this AP.
         * KiInitializeProcessorBootStructures sets up PCR, GDT/IDT pointers,
         * TSS, and stacks.
         */
        DoubleFaultStackTop = (ULONG64)&APInfo->DoubleFaultStackData[DOUBLE_FAULT_STACK_SIZE];
        NmiStackTop = (ULONG64)&APInfo->NmiStackData[DOUBLE_FAULT_STACK_SIZE];

        KiInitializeProcessorBootStructures(
            ProcessorCount,
            &APInfo->Pcr,
            &APInfo->Gdt[0],
            &APInfo->Idt[0],
            &APInfo->Tss,
            (PKTHREAD)&APInfo->IdleThread,
            KernelStack,
            DPCStack,
            (PVOID)DoubleFaultStackTop,
            (PVOID)NmiStackTop);

        /*
         * Copy the BSP's GDT and IDT to the AP.
         * Each AP gets its own copy because the GDT contains per-CPU
         * entries (TSS descriptor, etc.).
         */
        __sgdt(&BspGdt.Limit);
        __sidt(&BspIdt.Limit);
        RtlCopyMemory(&APInfo->Gdt, BspGdt.Base, BspGdt.Limit + 1);
        RtlCopyMemory(&APInfo->Idt, BspIdt.Base, BspIdt.Limit + 1);

        /*
         * Update the TSS descriptor in the AP's GDT.
         * The TSS GDT entry must point to this AP's TSS, not the BSP's.
         */
        TssEntry = KiGetGdtEntry(&APInfo->Gdt, KGDT64_SYS_TSS);
        KiInitGdtEntry(TssEntry, (ULONG64)&APInfo->Tss, sizeof(KTSS64), AMD64_TSS, 0);

        /*
         * Fill in the ProcessorState for the AP.
         * This is read by the HAL trampoline to configure the AP's
         * control registers, page tables, GDT, IDT, TSS, and to set
         * the initial instruction pointer and stack.
         */
        ProcessorState = &APInfo->Pcr.Prcb.ProcessorState;
        RtlZeroMemory(ProcessorState, sizeof(*ProcessorState));

        /* Copy the BSP's control register values */
        ProcessorState->SpecialRegisters.Cr0 = __readcr0();
        ProcessorState->SpecialRegisters.Cr3 = __readcr3();
        ProcessorState->SpecialRegisters.Cr4 = __readcr4();

        /* Set up the GDT and IDT descriptors for this AP */
        ProcessorState->SpecialRegisters.Gdtr.Base = (PVOID)APInfo->Gdt;
        ProcessorState->SpecialRegisters.Gdtr.Limit = sizeof(APInfo->Gdt) - 1;
        ProcessorState->SpecialRegisters.Idtr.Base = (PVOID)APInfo->Idt;
        ProcessorState->SpecialRegisters.Idtr.Limit = sizeof(APInfo->Idt) - 1;

        /* TSS selector */
        ProcessorState->SpecialRegisters.Tr = KGDT64_SYS_TSS;

        /*
         * Set the initial instruction pointer to KiSystemStartup.
         * The trampoline will jump here after mode transition.
         */
        ProcessorState->ContextFrame.Rip = (ULONG64)KiSystemStartup;

        /*
         * Set the initial stack pointer.
         * The stack grows downward; KernelStack points to the top.
         * Reserve space for the x64 calling convention (shadow space + alignment).
         */
        ProcessorState->ContextFrame.Rsp = (ULONG64)KernelStack;

        /*
         * Set RCX = KeLoaderBlock.
         * On AMD64, the first parameter is passed in RCX.
         * KiSystemStartup(PLOADER_PARAMETER_BLOCK LoaderBlock) reads it from RCX.
         */
        ProcessorState->ContextFrame.Rcx = (ULONG64)KeLoaderBlock;

        /* Disable interrupts in the initial EFLAGS */
        ProcessorState->ContextFrame.EFlags = __readeflags() & ~EFLAGS_INTERRUPT_MASK;

        /*
         * Update the LOADER_PARAMETER_BLOCK for this AP.
         * KiSystemStartup reads these fields to find the AP's PCR, thread, etc.
         */
        KeLoaderBlock->KernelStack = (ULONG_PTR)KernelStack;
        KeLoaderBlock->Prcb = (ULONG_PTR)&APInfo->Pcr.Prcb;
        KeLoaderBlock->Thread = (ULONG_PTR)&APInfo->IdleThread;

        /* Attempt to start the CPU */
        DPRINT1("KeStartAllProcessors: Starting CPU %lu\n", ProcessorCount);
        if (!HalStartNextProcessor(KeLoaderBlock, ProcessorState))
        {
            DPRINT1("KeStartAllProcessors: HalStartNextProcessor failed for CPU %lu\n",
                    ProcessorCount);
            break;
        }

        /*
         * Wait for the AP to signal that it has started.
         * KiSystemStartup on the AP will zero LoaderBlock->Prcb when ready.
         */
        {
            volatile ULONG Timeout;
            for (Timeout = 0; Timeout < 50000000; Timeout++)
            {
                if (KeLoaderBlock->Prcb == 0)
                    break;
                KeMemoryBarrier();
                YieldProcessor();
            }
            if (KeLoaderBlock->Prcb != 0)
            {
                DPRINT1("KeStartAllProcessors: CPU %lu FAILED to start (timeout), Prcb=0x%I64x\n",
                        ProcessorCount, KeLoaderBlock->Prcb);
                break;
            }
        }

        DPRINT1("KeStartAllProcessors: CPU %lu started successfully\n", ProcessorCount);
    }

    /* The last iteration didn't start a CPU - adjust the count */
    ProcessorCount--;

    /* Clean up resources from the failed/unused last allocation */
    if (APInfo)
        ExFreePoolWithTag(APInfo, TAG_KERNEL);
    if (KernelStack)
        MmDeleteKernelStack(KernelStack, FALSE);
    if (DPCStack)
        MmDeleteKernelStack(DPCStack, FALSE);

    DPRINT1("KeStartAllProcessors: %lu processors started successfully\n", ProcessorCount);
}
