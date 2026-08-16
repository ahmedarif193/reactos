/*
 * COPYRIGHT:       See COPYING in the top directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/mm/mmfault.c
 * PURPOSE:         Kernel memory management functions
 * PROGRAMMERS:     David Welch (welch@cwcom.net)
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include <cache/section/newmm.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include "ARM3/miarm.h"

extern MM_AVL_TABLE MiRosKernelVadRoot;

typedef enum _MI_FAULT_WS_TYPE
{
    MiFaultWsNone,
    MiFaultWsProcess,
    MiFaultWsSystem,
    MiFaultWsSession
} MI_FAULT_WS_TYPE;

typedef struct _MI_FAULT_WS_STATE
{
    MI_FAULT_WS_TYPE Type;
    PEPROCESS Process;
    PMMSUPPORT WorkingSet;
    BOOLEAN Safe;
    BOOLEAN Shared;
} MI_FAULT_WS_STATE, *PMI_FAULT_WS_STATE;

FORCEINLINE
VOID
MiUnlockWorkingSetForFault(
    _Out_ PMI_FAULT_WS_STATE State)
{
#if defined(_M_ARM64)
    PETHREAD Thread = PsGetCurrentThread();
#endif

    State->Type = MiFaultWsNone;
    State->Process = NULL;
    State->WorkingSet = NULL;
    State->Safe = FALSE;
    State->Shared = FALSE;

#if defined(_M_ARM64)
    if (!MM_ANY_WS_LOCK_HELD(Thread))
        return;

    if (Thread->OwnsProcessWorkingSetExclusive ||
        Thread->OwnsProcessWorkingSetShared)
    {
        State->Type = MiFaultWsProcess;
        State->Process = PsGetCurrentProcess();
        MiUnlockProcessWorkingSetForFault(State->Process,
                                          Thread,
                                          &State->Safe,
                                          &State->Shared);
        return;
    }

    if (Thread->OwnsSystemWorkingSetExclusive ||
        Thread->OwnsSystemWorkingSetShared)
    {
        State->Type = MiFaultWsSystem;
        State->WorkingSet = &MmSystemCacheWs;
        State->Shared = Thread->OwnsSystemWorkingSetShared;
    }
    else if (Thread->OwnsSessionWorkingSetExclusive ||
             Thread->OwnsSessionWorkingSetShared)
    {
        State->Type = MiFaultWsSession;
        State->WorkingSet = &MmSessionSpace->GlobalVirtualAddress->Vm;
        State->Shared = Thread->OwnsSessionWorkingSetShared;
    }
    else
    {
        ASSERT(FALSE);
        return;
    }

    if (State->Shared)
        MiUnlockWorkingSetShared(Thread, State->WorkingSet);
    else
        MiUnlockWorkingSet(Thread, State->WorkingSet);
#endif
}

FORCEINLINE
VOID
MiRelockWorkingSetForFault(
    _In_ PMI_FAULT_WS_STATE State)
{
#if defined(_M_ARM64)
    PETHREAD Thread = PsGetCurrentThread();

    switch (State->Type)
    {
        case MiFaultWsNone:
            return;

        case MiFaultWsProcess:
            MiLockProcessWorkingSetForFault(State->Process,
                                            Thread,
                                            State->Safe,
                                            State->Shared);
            return;

        case MiFaultWsSystem:
        case MiFaultWsSession:
            if (State->Shared)
                MiLockWorkingSetShared(Thread, State->WorkingSet);
            else
                MiLockWorkingSet(Thread, State->WorkingSet);
            return;
    }

    ASSERT(FALSE);
#else
    UNREFERENCED_PARAMETER(State);
#endif
}

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
MmpAccessFault(KPROCESSOR_MODE Mode,
               ULONG_PTR Address,
               BOOLEAN AddressSpaceLocked,
               ULONG FaultCode)
{
    PMMSUPPORT AddressSpace;
    MEMORY_AREA* MemoryArea;
    NTSTATUS Status;

    DPRINT("MmAccessFault(Mode %d, Address %x)\n", Mode, Address);

    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        DPRINT1("Page fault at high IRQL was %u\n", KeGetCurrentIrql());
        return(STATUS_UNSUCCESSFUL);
    }

    /* Instruction fetch and the page is present.
       This means the page is NX and we cannot do anything to "fix" it. */
    if (MI_IS_INSTRUCTION_FETCH(FaultCode))
    {
        DPRINT1("Page fault instruction fetch at %p\n", Address);
        return STATUS_ACCESS_VIOLATION;
    }

    /*
     * Find the memory area for the faulting address
     */
    if (Address >= (ULONG_PTR)MmSystemRangeStart)
    {
        /*
         * Check permissions
         */
        if (Mode != KernelMode)
        {
            DPRINT1("MmAccessFault(Mode %d, Address %x)\n", Mode, Address);
            return(STATUS_ACCESS_VIOLATION);
        }
        AddressSpace = MmGetKernelAddressSpace();
    }
    else
    {
        AddressSpace = &PsGetCurrentProcess()->Vm;
    }

    if (!AddressSpaceLocked)
    {
        MmLockAddressSpace(AddressSpace);
    }
    do
    {
        MemoryArea = MmLocateMemoryAreaByAddress(AddressSpace, (PVOID)Address);
        if (MemoryArea == NULL || MemoryArea->DeleteInProgress)
        {
            if (!AddressSpaceLocked)
            {
                MmUnlockAddressSpace(AddressSpace);
            }
            return (STATUS_ACCESS_VIOLATION);
        }

        switch (MemoryArea->Type)
        {
        case MEMORY_AREA_SECTION_VIEW:
            Status = MmAccessFaultSectionView(Mode, FaultCode, AddressSpace, MemoryArea, (PVOID)Address, TRUE);
            break;
#ifdef NEWCC
        case MEMORY_AREA_CACHE:
            // This code locks for itself to keep from having to break a lock
            // passed in.
            if (!AddressSpaceLocked)
                MmUnlockAddressSpace(AddressSpace);
            Status = MmAccessFaultCacheSection(Mode, Address, AddressSpaceLocked);
            if (!AddressSpaceLocked)
                MmLockAddressSpace(AddressSpace);
            break;
#endif
        default:
            Status = STATUS_ACCESS_VIOLATION;
            break;
        }
    }
    while (Status == STATUS_MM_RESTART_OPERATION);

    DPRINT("Completed page fault handling\n");
    if (!AddressSpaceLocked)
    {
        MmUnlockAddressSpace(AddressSpace);
    }
    return(Status);
}

NTSTATUS
NTAPI
MmNotPresentFault(KPROCESSOR_MODE Mode,
                  ULONG_PTR Address,
                  BOOLEAN AddressSpaceLocked)
{
    PMMSUPPORT AddressSpace;
    MEMORY_AREA* MemoryArea;
    NTSTATUS Status;

    DPRINT("MmNotPresentFault(Mode %d, Address %x)\n", Mode, Address);

    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        DPRINT1("Page fault at high IRQL was %u, address %x\n", KeGetCurrentIrql(), Address);
        return(STATUS_UNSUCCESSFUL);
    }

    /*
     * Find the memory area for the faulting address
     */
    if (Address >= (ULONG_PTR)MmSystemRangeStart)
    {
        /*
         * Check permissions
         */
        if (Mode != KernelMode)
        {
            DPRINT1("Address: %x\n", Address);
            return(STATUS_ACCESS_VIOLATION);
        }
        AddressSpace = MmGetKernelAddressSpace();
    }
    else
    {
        AddressSpace = &PsGetCurrentProcess()->Vm;
    }

    if (!AddressSpaceLocked)
    {
        MmLockAddressSpace(AddressSpace);
    }

    /*
     * Call the memory area specific fault handler
     */
    do
    {
        MemoryArea = MmLocateMemoryAreaByAddress(AddressSpace, (PVOID)Address);
        if (MemoryArea == NULL || MemoryArea->DeleteInProgress)
        {
            if (!AddressSpaceLocked)
            {
                MmUnlockAddressSpace(AddressSpace);
            }
            return (STATUS_ACCESS_VIOLATION);
        }

        switch (MemoryArea->Type)
        {
        case MEMORY_AREA_SECTION_VIEW:
            Status = MmNotPresentFaultSectionView(AddressSpace,
                                                  MemoryArea,
                                                  (PVOID)Address,
                                                  TRUE);
            break;
#ifdef NEWCC
        case MEMORY_AREA_CACHE:
            // This code locks for itself to keep from having to break a lock
            // passed in.
            if (!AddressSpaceLocked)
                MmUnlockAddressSpace(AddressSpace);
            Status = MmNotPresentFaultCacheSection(Mode,
                                                   Address,
                                                   AddressSpaceLocked,
                                                   MI_IS_WRITE_ACCESS(FaultCode));
            if (!AddressSpaceLocked)
                MmLockAddressSpace(AddressSpace);
            break;
#endif
        default:
            Status = STATUS_ACCESS_VIOLATION;
            break;
        }
    }
    while (Status == STATUS_MM_RESTART_OPERATION);

    DPRINT("Completed page fault handling\n");
    if (!AddressSpaceLocked)
    {
        MmUnlockAddressSpace(AddressSpace);
    }
    return(Status);
}

extern BOOLEAN Mmi386MakeKernelPageTableGlobal(PVOID Address);

VOID
NTAPI
MmRebalanceMemoryConsumersAndWait(VOID);

NTSTATUS
NTAPI
MmAccessFaultEx(IN ULONG FaultCode,
                IN PVOID Address,
                IN KPROCESSOR_MODE Mode,
                IN PVOID TrapInformation,
                IN BOOLEAN AddressSpaceLocked)
{
    PMMVAD Vad = NULL;
    NTSTATUS Status;
    BOOLEAN IsArm3Fault = FALSE;
    MI_FAULT_WS_STATE FaultWsState;

    /* Cute little hack for ROS */
    if ((ULONG_PTR)Address >= (ULONG_PTR)MmSystemRangeStart)
    {
#ifdef _M_IX86
        /* Check for an invalid page directory in kernel mode */
        if (Mmi386MakeKernelPageTableGlobal(Address))
        {
            /* All is well with the world */
            return STATUS_SUCCESS;
        }
#endif
    }

    MiUnlockWorkingSetForFault(&FaultWsState);

    /* Handle shared user page / page table, which don't have a VAD / MemoryArea */
    if ((PAGE_ALIGN(Address) == (PVOID)MM_SHARED_USER_DATA_VA) ||
        MI_IS_PAGE_TABLE_ADDRESS(Address))
    {
        /* This is an ARM3 fault */
        DPRINT("ARM3 fault %p\n", Address);
        Status = MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
        goto Exit;
    }

    /* Is there a ReactOS address space yet? */
    if (MmGetKernelAddressSpace())
    {
        if (Address > MM_HIGHEST_USER_ADDRESS)
        {
            /* Check if this is an ARM3 memory area */
            MiLockWorkingSetShared(PsGetCurrentThread(), &MmSystemCacheWs);
            Vad = MiLocateVad(&MiRosKernelVadRoot, Address);

            if ((Vad != NULL) && !MI_IS_ROSMM_VAD(Vad))
            {
                IsArm3Fault = TRUE;
            }

            MiUnlockWorkingSetShared(PsGetCurrentThread(), &MmSystemCacheWs);
        }
        else
        {
            /* Could this be a VAD fault from user-mode? */
            MiLockProcessWorkingSetShared(PsGetCurrentProcess(), PsGetCurrentThread());
            Vad = MiLocateVad(&PsGetCurrentProcess()->VadRoot, Address);

            if ((Vad != NULL) && !MI_IS_ROSMM_VAD(Vad))
            {
                IsArm3Fault = TRUE;
            }

            MiUnlockProcessWorkingSetShared(PsGetCurrentProcess(), PsGetCurrentThread());
        }
    }

    /* Is this an ARM3 VAD, or is there no address space yet? */
    if (IsArm3Fault ||
        ((Vad == NULL) &&
         ((ULONG_PTR)Address >= (ULONG_PTR)MmPagedPoolStart) &&
         ((ULONG_PTR)Address < (ULONG_PTR)MmPagedPoolEnd)) ||
        (!MmGetKernelAddressSpace()))
    {
        /* This is an ARM3 fault */
        DPRINT("ARM3 fault %p\n", Vad);
        Status = MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
        goto Exit;
    }

Retry:
    /* Keep same old ReactOS Behaviour */
    if (!MI_IS_NOT_PRESENT_FAULT(FaultCode))
    {
        /* Call access fault */
        Status = MmpAccessFault(Mode, (ULONG_PTR)Address, AddressSpaceLocked, FaultCode);
    }
    else
    {
        /* Call not present */
        Status = MmNotPresentFault(Mode, (ULONG_PTR)Address, AddressSpaceLocked);
    }

    if (Status == STATUS_NO_MEMORY)
    {
        MmRebalanceMemoryConsumersAndWait();
        goto Retry;
    }

Exit:
    MiRelockWorkingSetForFault(&FaultWsState);
    return Status;
}

NTSTATUS
NTAPI
MmAccessFault(IN ULONG FaultCode,
              IN PVOID Address,
              IN KPROCESSOR_MODE Mode,
              IN PVOID TrapInformation)
{
    return MmAccessFaultEx(FaultCode,
                           Address,
                           Mode,
                           TrapInformation,
                           TrapInformation ? FALSE : TRUE);
}
