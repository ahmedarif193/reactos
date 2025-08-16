/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Kernel Initialization
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* ARM64 specific globals */
PVOID KiTrapTableStart;
PVOID KiTrapTableEnd;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
KiInitializeKernel(IN PKPROCESS InitProcess,
                   IN PKTHREAD InitThread,
                   IN PVOID IdleStack,
                   IN PKPRCB Prcb,
                   IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* Set the Current Thread and Process on the stack */
    InitThread->ApcState.Process = InitProcess;
    
    /* Setup the process and thread list heads/entries */
    InitProcess->ThreadListHead.Flink = &InitThread->ThreadListEntry;
    InitProcess->ThreadListHead.Blink = &InitThread->ThreadListEntry;
    InitThread->ThreadListEntry.Flink = &InitProcess->ThreadListHead;
    InitThread->ThreadListEntry.Blink = &InitProcess->ThreadListHead;
    
    /* Initialize the Idle Thread */
    KeInitializeThread(InitProcess,
                       InitThread,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       IdleStack);
    
    /* Set the thread to running */
    InitThread->State = Running;
    
    /* Set up the thread-related fields in the PRCB */
    Prcb->CurrentThread = InitThread;
    Prcb->NextThread = NULL;
    Prcb->IdleThread = InitThread;
    
    /* Initialize the Kernel Executive */
    ExpInitializeExecutive(0, LoaderBlock);
    
    /* Set the CPU Type */
    Prcb->CpuType = 0x64;  /* ARM64 - use valid CHAR value */
    
    /* Set the initial IRQL to PASSIVE_LEVEL */
    KeGetPcr()->CurrentIrql = PASSIVE_LEVEL;
    
    /* Initialize the idle loop */
    KiIdleLoop();
}

VOID
NTAPI
KiSystemStartup(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKPRCB Prcb;
    PKTHREAD Thread;
    PKPROCESS Process;
    PVOID IdleStack;
    PKPCR Pcr;
    
    /* Get the PCR from TPIDR_EL1 */
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    
    /* Get the PRCB */
    Prcb = Pcr->Prcb;
    
    /* Save Number and LoaderBlock Data */
    Prcb->Number = 0;  /* Boot CPU is always 0 */
    KeLoaderBlock = LoaderBlock;
    
    /* Initialize the PCR */
    RtlZeroMemory(Pcr, sizeof(KPCR));
    
    /* Set the PCR pointer to itself */
    Pcr->Self = Pcr;
    Pcr->Prcb = Prcb;
    
    /* Initialize the kernel */
    Process = &PsIdleProcess->Pcb;
    Thread = CONTAINING_RECORD(Process->ThreadListHead.Flink, KTHREAD, ThreadListEntry);
    IdleStack = (PVOID)((ULONG_PTR)LoaderBlock->KernelStack + KERNEL_STACK_SIZE);
    
    KiInitializeKernel(Process, Thread, IdleStack, Prcb, LoaderBlock);
    
    /* Should not return */
    KeBugCheck(NO_PAGES_AVAILABLE);
}

/* KiInitializeDpc removed - DPC initialization handled elsewhere */

VOID
NTAPI
KiInitMachineDependent(VOID)
{
    /* Initialize ARM64-specific CPU features */
    DPRINT1("KiInitMachineDependent: Initializing ARM64 CPU features\n");
    
    /* TODO: Initialize ARM64-specific features like:
     * - FPU/NEON
     * - System registers
     * - Performance counters
     * - Cache configuration
     */
}

VOID
NTAPI
KiIdleLoop(VOID)
{
    /* ARM64 idle loop */
    while (TRUE)
    {
        /* Check for DPCs */
        if (KeGetCurrentPrcb()->DpcData[0].DpcQueueDepth)
        {
            /* Dispatch DPCs */
            KiRetireDpcList(KeGetCurrentPrcb());
        }
        
        /* Wait for interrupt - ARM64 WFI instruction */
        __asm__ __volatile__("wfi");
    }
}