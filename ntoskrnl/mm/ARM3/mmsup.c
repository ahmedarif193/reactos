/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/mmsup.c
 * PURPOSE:         ARM Memory Manager Support Routines
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

SIZE_T MmMinimumWorkingSetSize;
SIZE_T MmMaximumWorkingSetSize;
SIZE_T MmPagesAboveWsMinimum;

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
MmMapUserAddressesToPage(IN PVOID BaseAddress,
                         IN SIZE_T NumberOfBytes,
                         IN PVOID PageAddress)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
MmAdjustWorkingSetSize(IN SIZE_T WorkingSetMinimumInBytes,
                       IN SIZE_T WorkingSetMaximumInBytes,
                       IN ULONG SystemCache,
                       IN BOOLEAN IncreaseOkay)
{
    SIZE_T MinimumWorkingSetSize, MaximumWorkingSetSize;
    SSIZE_T Delta;
    PMMSUPPORT Ws;
    NTSTATUS Status;

    /* Check for special case: empty the working set */
    if ((WorkingSetMinimumInBytes == -1) &&
        (WorkingSetMaximumInBytes == -1))
    {
        UNIMPLEMENTED;
        return STATUS_NOT_IMPLEMENTED;
    }

    /* Assume success */
    Status = STATUS_SUCCESS;

    /* Get the working set and lock it */
    Ws = &PsGetCurrentProcess()->Vm;
    MiLockWorkingSet(PsGetCurrentThread(), Ws);

    /* Calculate the actual minimum and maximum working set size to set */
    MinimumWorkingSetSize = (WorkingSetMinimumInBytes != 0) ?
        (WorkingSetMinimumInBytes / PAGE_SIZE) : Ws->MinimumWorkingSetSize;
    MaximumWorkingSetSize = (WorkingSetMaximumInBytes != 0) ?
        (WorkingSetMaximumInBytes / PAGE_SIZE) : Ws->MaximumWorkingSetSize;

    /* Check if the new maximum exceeds the global maximum */
    if (MaximumWorkingSetSize > MmMaximumWorkingSetSize)
    {
        MaximumWorkingSetSize = MmMaximumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    /* Check if the new minimum is below the global minimum */
    if (MinimumWorkingSetSize < MmMinimumWorkingSetSize)
    {
        MinimumWorkingSetSize = MmMinimumWorkingSetSize;
        Status = STATUS_WORKING_SET_LIMIT_RANGE;
    }

    /* Check if the new minimum exceeds the new maximum */
    if (MinimumWorkingSetSize > MaximumWorkingSetSize)
    {
        DPRINT1("MinimumWorkingSetSize (%lu) > MaximumWorkingSetSize (%lu)\n",
                MinimumWorkingSetSize, MaximumWorkingSetSize);
        Status = STATUS_BAD_WORKING_SET_LIMIT;
        goto Cleanup;
    }

    /* Calculate the minimum WS size adjustment and check if we increase */
    Delta = MinimumWorkingSetSize - Ws->MinimumWorkingSetSize;
    if (Delta > 0)
    {
        /* Is increasing ok? */
        if (!IncreaseOkay)
        {
            DPRINT1("Privilege for WS size increase not held\n");
            Status = STATUS_PRIVILEGE_NOT_HELD;
            goto Cleanup;
        }

        /* Check if the number of available pages is large enough */
        if (((SIZE_T)Delta / 1024) > (MmAvailablePages - 128))
        {
            DPRINT1("Not enough available pages\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        /* Check if there are enough resident available pages */
        if ((SIZE_T)Delta >
            (MmResidentAvailablePages - MmSystemLockPagesCount - 256))
        {
            DPRINT1("Not enough resident pages\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    /* Update resident available pages */
    if (Delta != 0)
    {
        InterlockedExchangeAddSizeT(&MmResidentAvailablePages, -Delta);
    }

    /* Calculate new pages above minimum WS size */
    Delta += max((SSIZE_T)Ws->WorkingSetSize - MinimumWorkingSetSize, 0);

    /* Subtract old pages above minimum WS size */
    Delta -= max((SSIZE_T)Ws->WorkingSetSize - Ws->MinimumWorkingSetSize, 0);

    /* If it changed, add it to the global variable */
    if (Delta != 0)
    {
        InterlockedExchangeAddSizeT(&MmPagesAboveWsMinimum, Delta);
    }

    /* Set the new working set size */
    Ws->MinimumWorkingSetSize = MinimumWorkingSetSize;
    Ws->MaximumWorkingSetSize = MaximumWorkingSetSize;

Cleanup:

    /* Unlock the working set and return the status */
    MiUnlockWorkingSet(PsGetCurrentThread(), Ws);
    return Status;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
MmSetAddressRangeModified(IN PVOID Address,
                          IN SIZE_T Length)
{
   UNIMPLEMENTED;
   return FALSE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
MmIsAddressValid(IN PVOID VirtualAddress)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64-specific implementation using the AT (Address Translate) instruction.
     *
     * The self-map mechanism used by MiAddressToPte/Pde/Ppe/Pxe can fault if the
     * page table hierarchy is not fully populated. On ARM64, we use the AT S1E1R
     * instruction to probe the address translation without causing a fault.
     *
     * AT S1E1R translates the address as a stage 1 read at EL1, and stores the
     * result in PAR_EL1. If the translation fails, PAR_EL1.F bit is set.
     */
    ULONG64 par;

    /* Issue AT S1E1R instruction to translate the virtual address */
    __asm__ volatile(
        "at s1e1r, %1\n\t"
        "isb\n\t"
        "mrs %0, par_el1"
        : "=r" (par)
        : "r" ((ULONG64)VirtualAddress)
        : "memory"
    );

    /* Check the F (Fault) bit - bit 0 of PAR_EL1 */
    if (par & 1)
    {
        /* Translation failed - address is not valid */
        return FALSE;
    }

    /* Translation succeeded - address is valid */
    return TRUE;
#else
    /* Non-ARM64 implementation using self-map page table walk */
#if _MI_PAGING_LEVELS >= 4
    /* Check if the PXE is valid */
    if (MiAddressToPxe(VirtualAddress)->u.Hard.Valid == 0) return FALSE;
#endif

#if _MI_PAGING_LEVELS >= 3
    /* Check if the PPE is valid */
    if (MiAddressToPpe(VirtualAddress)->u.Hard.Valid == 0) return FALSE;
#endif

#if _MI_PAGING_LEVELS >= 2
    {
        PMMPTE PointerPde = MiAddressToPde(VirtualAddress);

        /* Check if the PDE is valid */
        if (PointerPde->u.Hard.Valid == 0) return FALSE;

        /* Check if this is a large page (2MB block mapping on ARM64/AMD64).
         * Large pages have no L3/PTE table - the PDE itself is the final mapping.
         * Trying to access MiAddressToPte() for a large page will cause a fault
         * because the self-map expects an L3 table that doesn't exist. */
        if (MI_IS_PAGE_LARGE(PointerPde)) return TRUE;
    }
#endif

    /* Check if the PTE is valid */
    if (MiAddressToPte(VirtualAddress)->u.Hard.Valid == 0) return FALSE;

    /* This address is valid now, but it will only stay so if the caller holds
     * the PFN lock */
    return TRUE;
#endif /* ARM64 */
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
MmIsNonPagedSystemAddressValid(IN PVOID VirtualAddress)
{
    DPRINT1("WARNING: %s returns bogus result\n", __FUNCTION__);
    return MmIsAddressValid(VirtualAddress);
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
MmSetBankedSection(IN HANDLE ProcessHandle,
                   IN PVOID VirtualAddress,
                   IN ULONG BankLength,
                   IN BOOLEAN ReadWriteBank,
                   IN PVOID BankRoutine,
                   IN PVOID Context)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
MmIsRecursiveIoFault(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();

    //
    // If any of these is true, this is a recursive fault
    //
    return ((Thread->DisablePageFaultClustering) | (Thread->ForwardClusterOnly));
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
MmIsThisAnNtAsSystem(VOID)
{
    /* Return if this is a server system */
    return MmProductType & 0xFF;
}

/*
 * @implemented
 */
MM_SYSTEMSIZE
NTAPI
MmQuerySystemSize(VOID)
{
    /* Return the low, medium or high memory system type */
    return MmSystemSize;
}

NTSTATUS
NTAPI
MmCreateMirror(VOID)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

/* EOF */
