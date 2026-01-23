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

#if defined(_M_ARM64) || defined(__aarch64__)
/*
 * ARM64-specific: Handle demand-zero fault for ARM3 VADs (e.g., PEB/TEB).
 *
 * ARM3 allocates PEB/TEB via MiInsertVadEx which creates ARM3 VADs, not ROS
 * memory areas. When a page fault occurs on these addresses, MmLocateMemoryArea
 * returns NULL (since it only finds ROS memory areas), but we still need to
 * handle the demand-zero fault.
 *
 * This function:
 * 1. Allocates a physical page
 * 2. Creates the page table hierarchy in TTBR0 via KSEG0 (direct mapping)
 * 3. Maps the page at the faulting address
 */
NTSTATUS
NTAPI
MiArm64HandleUserDemandZero(
    _In_ PVOID Address,
    _In_ ULONG ProtectionCode,
    _In_ PMMVAD Vad)
{
    NTSTATUS Status;
    PFN_NUMBER PageFrameIndex;
    PEPROCESS Process;
    PETHREAD Thread;
    KIRQL OldIrql;
    ULONG_PTR VirtualAddress = (ULONG_PTR)Address;

    /* Debug output */
Process = PsGetCurrentProcess();
    Thread = PsGetCurrentThread();

    /* Lock the working set */
    MiLockProcessWorkingSet(Process, Thread);

    /* Allocate a zero page */
    OldIrql = MiAcquirePfnLock();

    MI_SET_USAGE(MI_USAGE_PEB_TEB);
    MI_SET_PROCESS(Process);

    /* Get a zeroed page - track if we need to zero it ourselves */
    BOOLEAN NeedToZeroPage = FALSE;
    PageFrameIndex = MiRemoveZeroPage(MI_GET_NEXT_PROCESS_COLOR(Process));
    if (PageFrameIndex == 0)
    {
        /* No zeroed pages available, get any page */
        PageFrameIndex = MiRemoveAnyPage(MI_GET_NEXT_PROCESS_COLOR(Process));
        if (PageFrameIndex == 0)
        {
            MiReleasePfnLock(OldIrql);
            MiUnlockProcessWorkingSet(Process, Thread);
            return STATUS_NO_MEMORY;
        }
        /* CRITICAL: Must zero pages from MiRemoveAnyPage to prevent info leak */
        NeedToZeroPage = TRUE;
    }

    MiReleasePfnLock(OldIrql);

    /*
     * SECURITY FIX: Zero the page if we got it from MiRemoveAnyPage.
     * Failure to do so exposes stale physical memory (potentially containing
     * sensitive data like passwords, kernel data, etc.) to user space.
     *
     * Use MiZeroPhysicalPage which maps via hyperspace and zeros safely.
     */
    if (NeedToZeroPage)
    {
        MiZeroPhysicalPage(PageFrameIndex);
    }

    /* Create the page table mapping via KSEG0 */

    /* Convert MM protection to PTE protection */
    ULONG PteProtection;
    switch (ProtectionCode)
    {
        case MM_READONLY:
            PteProtection = PAGE_READONLY;
            break;
        case MM_READWRITE:
            PteProtection = PAGE_READWRITE;
            break;
        case MM_EXECUTE:
            PteProtection = PAGE_EXECUTE;
            break;
        case MM_EXECUTE_READ:
            PteProtection = PAGE_EXECUTE_READ;
            break;
        case MM_EXECUTE_READWRITE:
            PteProtection = PAGE_EXECUTE_READWRITE;
            break;
        default:
            PteProtection = PAGE_READWRITE;
            break;
    }

    Status = MiArm64MapUserPage(VirtualAddress, PageFrameIndex, PteProtection);

    if (!NT_SUCCESS(Status))
    {
        /* Failed to map - return the page */
        OldIrql = MiAcquirePfnLock();
        MiInsertPageInFreeList(PageFrameIndex);
        MiReleasePfnLock(OldIrql);

        MiUnlockProcessWorkingSet(Process, Thread);

return Status;
    }

    /* Initialize the PFN entry */
    OldIrql = MiAcquirePfnLock();

    /*
     * For ARM64 user demand-zero pages, we use a simplified PFN setup.
     * The page is private to the process and demand-paged.
     *
     * TODO: PteFrame should be set to the PFN of the page table page containing
     * this PTE. For now we use 0 which is technically wrong but works for
     * bring-up because we don't do trimming/outswapping of these pages yet.
     * This MUST be fixed before enabling working set trimming on ARM64.
     */
    PMMPFN Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);
    Pfn1->u3.e2.ReferenceCount = 1;
    Pfn1->u2.ShareCount = 1;
    Pfn1->u3.e1.PageLocation = ActiveAndValid;
    Pfn1->u3.e1.PrototypePte = 0;
    /* TODO: Calculate actual PteFrame from the page table page PFN */
    Pfn1->u4.PteFrame = 0;
    Pfn1->OriginalPte.u.Long = 0;
    Pfn1->OriginalPte.u.Soft.Protection = ProtectionCode;

    MiReleasePfnLock(OldIrql);

    MiUnlockProcessWorkingSet(Process, Thread);

return STATUS_SUCCESS;
}
#endif /* _M_ARM64 */

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

#if defined(_M_ARM64) || defined(__aarch64__)
    /* Debug: trace entry to MmNotPresentFault for user addresses */
#endif

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
#if defined(_M_ARM64) || defined(__aarch64__)
            /*
             * ARM64: Check if this is an ARM3 VAD (e.g., PEB/TEB allocation).
             * MmLocateMemoryAreaByAddress returns NULL for ARM3 VADs because
             * they're not ROS memory areas. We need to handle demand-zero
             * faults for these allocations separately.
             *
             * LOCKING: MiLocateAddress accesses PsGetCurrentProcess()->VadRoot
             * which requires holding the process working set lock or
             * AddressCreationLock. We verify we're faulting in our own process
             * context (which is always true for user-mode page faults), and
             * MmLockAddressSpace acquires AddressCreationLock for user spaces.
             *
             * CRITICAL: Only do this for USER addresses in our own process.
             */
            PMMVAD Arm3Vad = NULL;

            /* Only check ARM3 VAD for user addresses in current process */
            if (Address < (ULONG_PTR)MmSystemRangeStart)
            {
                PEPROCESS CurrentProcess = PsGetCurrentProcess();

                /*
                 * Verify we're faulting in our own process context.
                 * User faults should always be in current process; if not,
                 * we cannot safely access the VAD tree.
                 */
                if (AddressSpace == &CurrentProcess->Vm)
                {
                    /* Check if there's an ARM3 VAD for this address */
                    Arm3Vad = MiLocateAddress((PVOID)Address);
                }
            }

            if (Arm3Vad != NULL)
            {
                /*
                 * Get protection from the VAD. For PEB/TEB allocations,
                 * the protection is stored in Vad->u.VadFlags.Protection.
                 */
                ULONG ProtectionCode = Arm3Vad->u.VadFlags.Protection;

                /* This is an ARM3 VAD - handle via MiArm64HandleUserDemandZero */
                Status = MiArm64HandleUserDemandZero((PVOID)Address, ProtectionCode, Arm3Vad);
                if (WeAcquiredLock)
                {
                    MmUnlockAddressSpace(AddressSpace);
                }
                return Status;
            }
#endif
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

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 User Address Fast Path:
     *
     * For user addresses on ARM64, we MUST route to MmArmAccessFault first.
     * MmArmAccessFault has been updated with proper TTBR0 alias handling
     * (using MiAddressToPteTtbr0 instead of MiAddressToPte) for user addresses.
     *
     * If we continue to the VAD lookup path below, code may inadvertently
     * access page tables via the regular self-map (MiAddressToPde), which
     * doesn't work for user addresses on ARM64 (causes fault at PDE_BASE).
     *
     * MmArmAccessFault will handle the fault properly and route back to
     * ReactOS section handling if needed.
     */
    if (Address < MmSystemRangeStart)
    {
        return MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
    }
#endif

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
#if defined(_M_ARM64)
            /*
             * ARM64 PAGE TABLE / HYPERSPACE FAST PATH:
             *
             * Addresses in the page table self-map region (PTE_BASE to MmHyperSpaceEnd)
             * or TTBR0 alias region (PTE_BASE_TTBR0 to HYPER_SPACE_END) MUST be routed
             * directly to MmArmAccessFault WITHOUT acquiring any working set locks.
             *
             * These faults typically occur during nested page table manipulation:
             * 1. Code handles a user fault and acquires process WS lock
             * 2. Page table manipulation accesses self-map addresses
             * 3. Self-map address faults (page table page not yet mapped)
             * 4. If we try to acquire MmSystemCacheWs lock here, we hit:
             *    "Assertion failed: !MM_ANY_WS_LOCK_HELD(Thread)"
             *
             * MmArmAccessFault handles self-map/hyperspace faults specially via
             * MiArm64MapAliasForPointer without needing any VAD lookup.
             *
             * ARM64 FIX: Use PXE_TOP (not MmHyperSpaceEnd) as the upper bound.
             * HYPER_SPACE is at L0[492], while the self-map (PTE_BASE) starts at L0[493].
             * HYPER_SPACE_END < PTE_BASE, so using it would never match self-map addresses!
             */
            if (((ULONG_PTR)Address >= PTE_BASE && (ULONG_PTR)Address <= (ULONG_PTR)PXE_TOP))
            {
                return MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
            }
#endif

#if defined(_M_ARM64)
            /* ARM64 DEBUG: Log System Cache faults */
            if ((ULONG_PTR)Address >= 0xFFFFF98000000000ULL &&
                (ULONG_PTR)Address < 0xFFFFFA8000000000ULL)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmAccessFault: SYSCACHE ENTRY addr=%p FaultCode=0x%lx\n",
                    Address, FaultCode);
            }
#endif
            /* Check if this is an ARM3 memory area */
            MiLockWorkingSetShared(PsGetCurrentThread(), &MmSystemCacheWs);
#if defined(_M_ARM64)
            if ((ULONG_PTR)Address >= 0xFFFFF98000000000ULL &&
                (ULONG_PTR)Address < 0xFFFFFA8000000000ULL)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmAccessFault: SYSCACHE after WS lock addr=%p\n", Address);
            }
#endif
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

#if defined(_M_ARM64)
            if ((ULONG_PTR)Address >= 0xFFFFF98000000000ULL &&
                (ULONG_PTR)Address < 0xFFFFFA8000000000ULL)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmAccessFault: SYSCACHE Vad=%p IsRos=%d IsArm3=%d\n",
                    Vad, Vad ? (MI_IS_ROSMM_VAD(Vad) ? 1 : 0) : -1, IsArm3Fault);
            }
#endif
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
#if defined(_M_ARM64)
    /* ARM64 DEBUG: Log System Cache routing to ROS path */
    if ((ULONG_PTR)Address >= 0xFFFFF98000000000ULL &&
        (ULONG_PTR)Address < 0xFFFFFA8000000000ULL)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[arm64] MmAccessFault: SYSCACHE ROS path addr=%p NotPresent=%d\n",
            Address, MI_IS_NOT_PRESENT_FAULT(FaultCode));
    }
#endif
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
#if defined(_M_ARM64)
    /* ARM64 DEBUG: Log System Cache result */
    if ((ULONG_PTR)Address >= 0xFFFFF98000000000ULL &&
        (ULONG_PTR)Address < 0xFFFFFA8000000000ULL)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[arm64] MmAccessFault: SYSCACHE result addr=%p Status=0x%lx\n",
            Address, Status);
    }
#endif

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
