/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 System Call Handler
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* System service descriptor table - defined in procobj.c */
extern KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTable[SSDT_MAX_ENTRIES];
extern KSERVICE_TABLE_DESCRIPTOR KeServiceDescriptorTableShadow[SSDT_MAX_ENTRIES];

/* System call table (populated during initialization) */
extern PVOID KiServiceTable[];
extern ULONG KiServiceLimit;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
KiInitializeSystemCall(VOID)
{
    DPRINT1("ARM64: Initializing system call interface\n");
    
    /* Setup the main system service table */
    KeServiceDescriptorTable[0].Base = KiServiceTable;
    KeServiceDescriptorTable[0].Count = NULL;
    KeServiceDescriptorTable[0].Limit = KiServiceLimit;
    KeServiceDescriptorTable[0].Number = NULL;
    
    /* Copy to shadow table */
    RtlCopyMemory(KeServiceDescriptorTableShadow,
                  KeServiceDescriptorTable,
                  sizeof(KeServiceDescriptorTable));
    
    /* TODO: Setup Win32k service table when GUI is supported */
    
    DPRINT1("ARM64: System call interface initialized\n");
}

VOID
NTAPI
KiSystemService(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    ULONG ServiceNumber;
    ULONG TableIndex;
    ULONG ServiceIndex;
    PVOID *ServiceTable;
    ULONG ArgumentCount;
    PVOID ServiceRoutine;
    NTSTATUS Status;
    
    /* Get current thread */
    Thread = KeGetCurrentThread();
    
    /* Save trap frame in thread */
    Thread->TrapFrame = TrapFrame;
    
    /* Get system call number from X8 register (ARM64 convention) */
    ServiceNumber = (ULONG)TrapFrame->X8;
    
    /* Extract table index and service index */
    TableIndex = (ServiceNumber >> 12) & 0x1;
    ServiceIndex = ServiceNumber & 0xFFF;
    
    /* Validate table index */
    if (TableIndex >= SSDT_MAX_ENTRIES)
    {
        DPRINT1("Invalid system service table index: %lu\n", TableIndex);
        TrapFrame->X0 = STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }
    
    /* Get the service table */
    if (Thread->ServiceTable)
    {
        ServiceTable = ((PKSERVICE_TABLE_DESCRIPTOR)Thread->ServiceTable)[TableIndex].Base;
    }
    else
    {
        ServiceTable = KeServiceDescriptorTable[TableIndex].Base;
    }
    
    /* Validate service index */
    if (ServiceIndex >= KeServiceDescriptorTable[TableIndex].Limit)
    {
        DPRINT1("Invalid system service index: %lu\n", ServiceIndex);
        TrapFrame->X0 = STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }
    
    /* Get the service routine */
    ServiceRoutine = ServiceTable[ServiceIndex];
    
    /* Get argument count (encoded in service table) */
    ArgumentCount = ((PULONG)KeServiceDescriptorTable[TableIndex].Number)[ServiceIndex];
    
    /* Setup for system call */
    Thread->PreviousMode = KiGetPreviousMode(TrapFrame);
    
    /* Enable interrupts */
    _enable();
    
    /* Call the system service with parameters from registers */
    /* ARM64 passes first 8 parameters in X0-X7 */
    /* Package arguments into array */
    ULONG64 Arguments[8];
    Arguments[0] = TrapFrame->X0;
    Arguments[1] = TrapFrame->X1;
    Arguments[2] = TrapFrame->X2;
    Arguments[3] = TrapFrame->X3;
    Arguments[4] = TrapFrame->X4;
    Arguments[5] = TrapFrame->X5;
    Arguments[6] = TrapFrame->X6;
    Arguments[7] = TrapFrame->X7;
    
    Status = KiCallSystemService(ServiceRoutine,
                                  Arguments,
                                  ArgumentCount);
    
    /* Store return value in X0 */
    TrapFrame->X0 = (ULONG64)Status;
    
    /* Check for pending APCs */
    if (Thread->ApcState.UserApcPending)
    {
        KiDeliverUserApc(TrapFrame);
    }
    
    /* Clear trap frame from thread */
    Thread->TrapFrame = NULL;
    
    /* Status is already in X0 */
}

NTSTATUS
NTAPI
KiCallSystemService(IN PVOID ServiceRoutine,
                    IN PVOID Arguments,
                    IN ULONG ArgumentCount)
{
    NTSTATUS Status;
    PULONG64 Args = (PULONG64)Arguments;
    
    /* Type for system service with up to 8 arguments */
    typedef NTSTATUS (NTAPI *PSYSTEM_SERVICE_8)(
        ULONG64, ULONG64, ULONG64, ULONG64,
        ULONG64, ULONG64, ULONG64, ULONG64);
    
    /* Call the service routine */
    /* ARM64 can pass up to 8 arguments in registers */
    if (ArgumentCount <= 8)
    {
        Status = ((PSYSTEM_SERVICE_8)ServiceRoutine)(
            Args[0], Args[1], Args[2], Args[3], 
            Args[4], Args[5], Args[6], Args[7]);
    }
    else
    {
        /* More than 8 arguments require stack access */
        /* TODO: Implement stack-based argument passing */
        DPRINT1("System service with >8 arguments not yet supported\n");
        Status = STATUS_NOT_IMPLEMENTED;
    }
    
    return Status;
}

BOOLEAN
NTAPI
KiDeliverUserApc(PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    PKAPC Apc;
    PKNORMAL_ROUTINE NormalRoutine;
    PVOID NormalContext;
    PVOID SystemArgument1;
    PVOID SystemArgument2;
    
    /* Get current thread */
    Thread = KeGetCurrentThread();
    
    /* Raise IRQL to APC_LEVEL */
    KIRQL OldIrql;
    KeRaiseIrql(APC_LEVEL, &OldIrql);
    
    /* Acquire APC queue lock */
    KeAcquireSpinLockAtDpcLevel(&Thread->ApcQueueLock);
    
    /* Process user APCs */
    while (!IsListEmpty(&Thread->ApcState.ApcListHead[UserMode]))
    {
        /* Get the APC */
        Apc = CONTAINING_RECORD(Thread->ApcState.ApcListHead[UserMode].Flink,
                                KAPC,
                                ApcListEntry);
        
        /* Remove from queue */
        RemoveEntryList(&Apc->ApcListEntry);
        
        /* Get APC parameters */
        NormalRoutine = Apc->NormalRoutine;
        NormalContext = Apc->NormalContext;
        SystemArgument1 = Apc->SystemArgument1;
        SystemArgument2 = Apc->SystemArgument2;
        
        /* Release lock before calling APC */
        KeReleaseSpinLockFromDpcLevel(&Thread->ApcQueueLock);
        
        /* Initialize user APC */
        KiInitializeUserApc(NULL,
                            TrapFrame,
                            NormalRoutine,
                            NormalContext,
                            SystemArgument1,
                            SystemArgument2);
        
        /* Free the APC object */
        ExFreePoolWithTag(Apc, 'cpaK');
        
        /* Reacquire lock for next iteration */
        KeAcquireSpinLockAtDpcLevel(&Thread->ApcQueueLock);
    }
    
    /* Clear user APC pending flag */
    Thread->ApcState.UserApcPending = FALSE;
    
    /* Release APC queue lock */
    KeReleaseSpinLockFromDpcLevel(&Thread->ApcQueueLock);
    
    /* Lower IRQL */
    KeLowerIrql(OldIrql);
    
    /* Return TRUE if we delivered any APCs */
    return TRUE;
}

VOID
FASTCALL
KiCheckForApcDelivery(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    
    /* Get current thread */
    Thread = KeGetCurrentThread();
    
    /* Check if we have user APCs pending and we're at passive level */
    if (Thread->ApcState.UserApcPending && 
        KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        /* Deliver the APCs */
        KiDeliverUserApc(TrapFrame);
    }
}

VOID
NTAPI
KiServiceExit2(IN PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    
    /* Get current thread */
    Thread = KeGetCurrentThread();
    
    /* Check for pending APCs or alerts */
    if (Thread->ApcState.UserApcPending || Thread->Alerted[UserMode])
    {
        /* Handle pending operations */
        KiCheckForApcDelivery(TrapFrame);
    }
    
    /* Check if thread quantum expired */
    if (Thread->Quantum <= 0)
    {
        /* Request thread scheduling */
        KiRequestThreadScheduling();
    }
    
    /* Return to user mode will be handled by assembly code */
}

VOID
NTAPI
KiRequestThreadScheduling(VOID)
{
    PKPRCB Prcb;
    
    /* Get current processor control block */
    Prcb = KeGetCurrentPrcb();
    
    /* Set the thread scheduling request flag */
    Prcb->QuantumEnd = TRUE;
    
    /* Request a DPC software interrupt to handle scheduling */
    HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
}

/* EOF */