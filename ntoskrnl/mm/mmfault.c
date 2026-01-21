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

/* Debug logging for kernel section view fault routing - disabled in production */
#define MMFAULT_DEBUG 0

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
MmpAccessFault(KPROCESSOR_MODE Mode,
               ULONG_PTR Address,
               BOOLEAN FromMdl,
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

    /*
     * Lock the address space if not already locked by caller.
     * For kernel address space, check if we already hold the lock to avoid
     * recursive locking during nested page faults.
     *
     * We track both:
     * - WeAcquiredLock: TRUE if we acquired the lock in this call (need to release)
     * - LockHeld: TRUE if the lock is held by us (passed to handlers)
     */
    BOOLEAN WeAcquiredLock = FALSE;
    BOOLEAN LockHeld;

    if (!FromMdl)
    {
        PEPROCESS KernelProcess = CONTAINING_RECORD(MmGetKernelAddressSpace(), EPROCESS, Vm);
        PKGUARDED_MUTEX KernelLock = &KernelProcess->AddressCreationLock;

        /* Only lock if we don't already own it */
        if (AddressSpace == MmGetKernelAddressSpace() &&
            KernelLock->Owner == KeGetCurrentThread())
        {
            /* Already locked by us - nested fault */
            WeAcquiredLock = FALSE;
        }
        else
        {
            MmLockAddressSpace(AddressSpace);
            WeAcquiredLock = TRUE;
        }
        LockHeld = TRUE;
    }
    else
    {
        LockHeld = FALSE;
    }
    do
    {
        MemoryArea = MmLocateMemoryAreaByAddress(AddressSpace, (PVOID)Address);
        if (MemoryArea == NULL || MemoryArea->DeleteInProgress)
        {
            if (WeAcquiredLock)
            {
                MmUnlockAddressSpace(AddressSpace);
            }
            return (STATUS_ACCESS_VIOLATION);
        }

        switch (MemoryArea->Type)
        {
        case MEMORY_AREA_SECTION_VIEW:
            Status = MmAccessFaultSectionView(AddressSpace,
                                              MemoryArea,
                                              (PVOID)Address,
                                              LockHeld);
            break;
#ifdef NEWCC
        case MEMORY_AREA_CACHE:
            // This code locks for itself to keep from having to break a lock
            // passed in.
            if (WeAcquiredLock)
                MmUnlockAddressSpace(AddressSpace);
            Status = MmAccessFaultCacheSection(Mode, Address, FromMdl);
            if (WeAcquiredLock)
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
    if (WeAcquiredLock)
    {
        MmUnlockAddressSpace(AddressSpace);
    }
    return(Status);
}

NTSTATUS
NTAPI
MmNotPresentFault(KPROCESSOR_MODE Mode,
                  ULONG_PTR Address,
                  BOOLEAN FromMdl)
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
            PEPROCESS Process = PsGetCurrentProcess();

            DPRINT1("User-mode fault on system address %p (proc %s pid %lu tid %lu)\n",
                    Address,
                    Process ? Process->ImageFileName : "<unknown>",
                    (ULONG)(ULONG_PTR)PsGetCurrentProcessId(),
                    (ULONG)(ULONG_PTR)PsGetCurrentThreadId());
            return(STATUS_ACCESS_VIOLATION);
        }
        AddressSpace = MmGetKernelAddressSpace();
    }
    else
    {
        AddressSpace = &PsGetCurrentProcess()->Vm;
    }

    /*
     * Lock the address space if not already locked by caller.
     * For kernel address space, check if we already hold the lock to avoid
     * recursive locking during nested page faults (e.g., when
     * MmNotPresentFaultSectionView faults on a page table while handling
     * the original section view fault).
     *
     * We track both:
     * - WeAcquiredLock: TRUE if we acquired the lock in this call (need to release)
     * - LockHeld: TRUE if the lock is held by us (passed to handlers)
     */
    BOOLEAN WeAcquiredLock = FALSE;
    BOOLEAN LockHeld;

    if (!FromMdl)
    {
        PEPROCESS KernelProcess = CONTAINING_RECORD(MmGetKernelAddressSpace(), EPROCESS, Vm);
        PKGUARDED_MUTEX KernelLock = &KernelProcess->AddressCreationLock;

        /* Only lock if we don't already own it */
        if (AddressSpace == MmGetKernelAddressSpace() &&
            KernelLock->Owner == KeGetCurrentThread())
        {
            /* Already locked by us - nested fault */
            WeAcquiredLock = FALSE;
        }
        else
        {
            MmLockAddressSpace(AddressSpace);
            WeAcquiredLock = TRUE;
        }
        LockHeld = TRUE;
    }
    else
    {
        LockHeld = FALSE;
    }

    /*
     * Call the memory area specific fault handler
     */
    do
    {
        MemoryArea = MmLocateMemoryAreaByAddress(AddressSpace, (PVOID)Address);
        if (MemoryArea == NULL || MemoryArea->DeleteInProgress)
        {
            if (WeAcquiredLock)
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
                                                  LockHeld);
            break;
#ifdef NEWCC
        case MEMORY_AREA_CACHE:
            // This code locks for itself to keep from having to break a lock
            // passed in.
            if (WeAcquiredLock)
                MmUnlockAddressSpace(AddressSpace);
            Status = MmNotPresentFaultCacheSection(Mode, Address, FromMdl);
            if (WeAcquiredLock)
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
    if (WeAcquiredLock)
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
MmAccessFault(IN ULONG FaultCode,
              IN PVOID Address,
              IN KPROCESSOR_MODE Mode,
              IN PVOID TrapInformation)
{
    PMMVAD Vad = NULL;
    NTSTATUS Status;
    BOOLEAN IsArm3Fault = FALSE;

    /*
     * ARM64 FIX: At elevated IRQL (> APC_LEVEL), we cannot acquire working set
     * locks which are required for the VAD lookup path. Route directly to
     * MmArmAccessFault which has proper high-IRQL fault handling.
     *
     * This can happen when:
     * 1. A page fault occurs while holding a spinlock (DISPATCH_LEVEL)
     * 2. The fault handler needs to resolve a demand-paged kernel address
     * 3. The PTE chain lookup causes a nested fault on self-map aliases
     *
     * MmArmAccessFault will either:
     * - Handle the fault if the page tables are already mapped
     * - Bugcheck if the fault cannot be resolved at high IRQL
     */
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        return MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
    }

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

    /* Handle shared user page / page table, which don't have a VAD / MemoryArea */
    if ((PAGE_ALIGN(Address) == (PVOID)MM_SHARED_USER_DATA_VA) ||
        MI_IS_PAGE_TABLE_ADDRESS(Address))
    {
        /* This is an ARM3 fault */
        DPRINT("ARM3 fault %p\n", Address);
        return MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
    }

    /* Is there a ReactOS address space yet? */
    if (MmGetKernelAddressSpace())
    {
#if defined(_M_ARM64)
        /* Debug: trace user-space faults */
        if (Address <= MM_HIGHEST_USER_ADDRESS && (ULONG_PTR)Address >= 0xFE000000)
        {
            DPRINT1("[arm64] MmAccessFault: HIGH USER addr=%p FaultCode=0x%lx Mode=%d\n",
                    Address, FaultCode, Mode);
        }
#endif
        if (Address > MM_HIGHEST_USER_ADDRESS)
        {
            /* Check if this is an ARM3 memory area */
            MiLockWorkingSetShared(PsGetCurrentThread(), &MmSystemCacheWs);
            Vad = MiLocateVad(&MiRosKernelVadRoot, Address);

#if defined(_M_ARM64) && DBG
            /* Debug System View Space faults */
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)  /* First 16MB */
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmAccessFault: System View Space addr=%p Vad=%p IsRosVad=%d\n",
                    Address, Vad, Vad ? (MI_IS_ROSMM_VAD(Vad) ? 1 : 0) : -1);
            }
#endif

#if MMFAULT_DEBUG
            if ((ULONG_PTR)Address >= 0xFFFF8000B0000000ULL &&
                (ULONG_PTR)Address < 0xFFFF8000E0000000ULL)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[MmAccessFault] Kernel addr %p: Vad=%p IsRosMmVad=%d\n",
                    Address, Vad,
                    Vad ? (MI_IS_ROSMM_VAD(Vad) ? 1 : 0) : -1);
                if (Vad)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                        "[MmAccessFault] Vad->StartingVpn=0x%llx EndingVpn=0x%llx Spare=0x%x\n",
                        (ULONGLONG)Vad->StartingVpn,
                        (ULONGLONG)Vad->EndingVpn,
                        Vad->u.VadFlags.Spare);
                }
            }
#endif

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

#if defined(_M_ARM64)
            /* Debug: trace high user address VAD lookup */
            if ((ULONG_PTR)Address >= 0xFE000000)
            {
                DPRINT1("[arm64] MmAccessFault: User VAD lookup addr=%p Vad=%p IsRos=%d\n",
                        Address, Vad, Vad ? (MI_IS_ROSMM_VAD(Vad) ? 1 : 0) : -1);
            }
#endif

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

#if defined(_M_ARM64) && DBG
        /* Debug System View Space routing */
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmAccessFault: Routing to MmArmAccessFault for %p\n", Address);
        }
#endif

        {
            NTSTATUS Status = MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
#if defined(_M_ARM64) && DBG
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmAccessFault: MmArmAccessFault returned Status=0x%lx for addr=%p\n",
                    Status, Address);
            }
#endif
            return Status;
        }
    }

#if defined(_M_ARM64) && DBG
    /* Debug: we're taking the ROS path */
    extern PVOID MiSystemViewStart;
    if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
        (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[arm64] MmAccessFault: Taking ROS path for %p (Vad=%p)\n", Address, Vad);
    }
#endif

Retry:
    /* Keep same old ReactOS Behaviour */
    if (!MI_IS_NOT_PRESENT_FAULT(FaultCode))
    {
        /* Call access fault */
        Status = MmpAccessFault(Mode, (ULONG_PTR)Address, TrapInformation ? FALSE : TRUE, FaultCode);
    }
    else
    {
        /* Call not present */
        Status = MmNotPresentFault(Mode, (ULONG_PTR)Address, TrapInformation ? FALSE : TRUE);
    }

    if ((Status == STATUS_ACCESS_VIOLATION) &&
        (Mode != KernelMode) &&
        (ULONG_PTR)Address >= (ULONG_PTR)MmSystemRangeStart &&
        TrapInformation != NULL)
    {
#if defined(_M_AMD64) || defined(_M_IX86)
        PKTRAP_FRAME TrapFrame = (PKTRAP_FRAME)TrapInformation;

#if defined(_M_AMD64)
        DPRINT1("Page fault context: RIP=%p RSP=%p RBP=%p RCX=%p RDX=%p\n",
                (PVOID)TrapFrame->Rip,
                (PVOID)TrapFrame->Rsp,
                (PVOID)TrapFrame->Rbp,
                (PVOID)TrapFrame->Rcx,
                (PVOID)TrapFrame->Rdx);
#elif defined(_M_IX86)
        DPRINT1("Page fault context: EIP=%p ESP=%p EBP=%p ECX=%p EDX=%p\n",
                (PVOID)TrapFrame->Eip,
                (PVOID)TrapFrame->HardwareEsp,
                (PVOID)TrapFrame->Ebp,
                (PVOID)TrapFrame->Ecx,
                (PVOID)TrapFrame->Edx);
#endif
#endif
    }

    if (Status == STATUS_NO_MEMORY)
    {
        MmRebalanceMemoryConsumersAndWait();
        goto Retry;
    }

    return Status;
}
