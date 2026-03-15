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

/* PRIVATE FUNCTIONS **********************************************************/

/* Shadow MM functions (MiArm64HandleUserDemandZero, MiArm64HandleUserPrototypeFault)
 * removed: no longer needed with recursive self-map fix via merged PXE page. */

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
#ifdef _M_ARM64
            {
                PKTRAP_FRAME Tf = KeGetCurrentThread()->TrapFrame;
                DPRINT1("User-mode fault on system address %p (proc %s pid %lu tid %lu) PC=%p LR=%p\n",
                        Address,
                        Process ? Process->ImageFileName : "<unknown>",
                        (ULONG)(ULONG_PTR)PsGetCurrentProcessId(),
                        (ULONG)(ULONG_PTR)PsGetCurrentThreadId(),
                        Tf ? (PVOID)Tf->Pc : NULL,
                        Tf ? (PVOID)Tf->Lr : NULL);
            }
#else
            DPRINT1("User-mode fault on system address %p (proc %s pid %lu tid %lu)\n",
                    Address,
                    Process ? Process->ImageFileName : "<unknown>",
                    (ULONG)(ULONG_PTR)PsGetCurrentProcessId(),
                    (ULONG)(ULONG_PTR)PsGetCurrentThreadId());
#endif
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
#if defined(_M_ARM64)
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        return MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
    }
#endif

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
     * ARM64 User Address Fault Routing:
     *
     * NOT-PRESENT faults go to MmArmAccessFault (ARM3 demand paging).
     * ACCESS FLAG faults (DFSC 0x08-0x0B) are resolved by setting AF via KSEG0.
     * INSTRUCTION FETCH faults check region protection and clear UXN if allowed.
     * PRESENT-PAGE faults (write/CoW) go to Retry -> MmpAccessFault.
     */
    if (Address < MmSystemRangeStart)
    {
        if (MI_IS_NOT_PRESENT_FAULT(FaultCode))
        {
            return MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
        }

        /* Access flag faults: walk TTBR0 via KSEG0 and set AF bit */
        if (TrapInformation != NULL &&
            (ULONG_PTR)TrapInformation >= (ULONG_PTR)MmSystemRangeStart)
        {
            PKTRAP_FRAME TrapFrame = (PKTRAP_FRAME)TrapInformation;
            ULONG Dfsc = TrapFrame->Esr & 0x3FULL;

            if ((Dfsc & 0x3C) == 0x08)
            {
                if (MiArm64HandleUserAccessFlagFault(Address, Dfsc & 0x03))
                {
                    ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
                    __asm__ __volatile__("dsb ishst" ::: "memory");
                    __asm__ __volatile__("tlbi vae1is, %0" :: "r"(VaForTlbi) : "memory");
                    __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
                    return STATUS_SUCCESS;
                }
                goto Retry;
            }
        }

        /*
         * Instruction fetch permission faults: page is present but UXN=1.
         * Check region/VAD protection; if execute is allowed, clear UXN.
         */
        if (MI_IS_INSTRUCTION_FETCH(FaultCode))
        {
            /* Read L3 PTE via KSEG0 — bypasses stale self-map for user PTEs */
            PMMPTE L3Pte = MiArm64UserPteKseg0(Address);
            if (L3Pte != NULL)
            {
                ULONG64 L3Entry = *(volatile ULONG64 *)L3Pte;
                if ((L3Entry & (1ULL << 54)) && (L3Entry & 0x1ULL))
                {
                    PMMSUPPORT As = &PsGetCurrentProcess()->Vm;
                    BOOLEAN ShouldExec = FALSE;

                    MmLockAddressSpace(As);
                    {
                        MEMORY_AREA *Ma = MmLocateMemoryAreaByAddress(As, Address);
                        if (Ma != NULL && !Ma->DeleteInProgress &&
                            Ma->Type == MEMORY_AREA_SECTION_VIEW)
                        {
                            PMM_REGION Region = MmFindRegion(
                                (PVOID)MA_GetStartingAddress(Ma),
                                &Ma->SectionData.RegionListHead,
                                Address, NULL);
                            if (Region && (Region->Protect & PAGE_IS_EXECUTABLE))
                                ShouldExec = TRUE;
                        }
                        else if (Ma == NULL)
                        {
                            PMMVAD IfVad = MiLocateAddress(Address);
                            if (IfVad != NULL)
                            {
                                ULONG VadProt = IfVad->u.VadFlags.Protection;
                                if (VadProt == MM_EXECUTE ||
                                    VadProt == MM_EXECUTE_READ ||
                                    VadProt == MM_EXECUTE_READWRITE ||
                                    VadProt == MM_EXECUTE_WRITECOPY)
                                    ShouldExec = TRUE;
                            }
                        }
                    }
                    MmUnlockAddressSpace(As);

                    if (ShouldExec)
                    {
                        /* Clear UXN bit via KSEG0 and invalidate TLB */
                        *(volatile ULONG64 *)L3Pte = L3Entry & ~(1ULL << 54);
                        ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
                        __asm__ __volatile__("dsb ishst" ::: "memory");
                        __asm__ __volatile__("tlbi vae1is, %0" :: "r"(VaForTlbi) : "memory");
                        __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
                        return STATUS_SUCCESS;
                    }
                }
            }
            /* Fall through to Retry — MmpAccessFault returns ACCESS_VIOLATION */
        }

        /* Present-page write fault: handle dirty-bit promotion inline.
         *
         * A write permission fault fires when:
         *   - SW Writable=1 (bit 55) but HW NotDirty=1 (AP[2]=1 = read-only)
         *     → dirty bit promotion: clear NotDirty, TLB flush, return SUCCESS.
         *   - SW Writable=0, CopyOnWrite=1 (bit 56)
         *     → COW: fall through to MmpAccessFault (RosMM handles section COW).
         *   - SW Writable=0, CopyOnWrite=0
         *     → truly read-only: ACCESS_VIOLATION via MmpAccessFault.
         *
         * Routing ALL write faults to MmArmAccessFault is wrong: MmArmAccessFault
         * re-maps section COW pages with the same read-only protection, creating
         * an infinite fault loop (STATUS_SUCCESS returned but page still HW RO).
         */
        if (MI_IS_WRITE_ACCESS(FaultCode))
        {
            PMMPTE WritePte = MiArm64UserPteKseg0(Address);
            if (WritePte != NULL && WritePte->u.Hard.Valid &&
                WritePte->u.Hard.Writable)
            {
                /* Dirty bit promotion: SW Writable=1 but HW NotDirty=1.
                 * Clear NotDirty (AP[2]=0) to make the page HW writable. */
                ULONG64 VaForTlbi;
                MI_MAKE_DIRTY_PAGE(WritePte);
                VaForTlbi = ((ULONG64)(ULONG_PTR)PAGE_ALIGN(Address)) >> 12;
                __asm__ __volatile__("dsb ishst" ::: "memory");
                __asm__ __volatile__("tlbi vae1is, %0" :: "r"(VaForTlbi) : "memory");
                __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
                return STATUS_SUCCESS;
            }
            /* CopyOnWrite or truly RO: let MmpAccessFault decide. */
        }

        /* Other present-page faults (CoW, etc.): use RosMM handlers */
        goto Retry;
    }
#endif

    /* Is there a ReactOS address space yet? */
    if (MmGetKernelAddressSpace())
    {
        if (Address > MM_HIGHEST_USER_ADDRESS)
        {
#if defined(_M_ARM64)
            /*
             * ARM64 PAGE TABLE SELF-MAP FAST PATH:
             *
             * Addresses in the page table self-map region (L0[493]) MUST be
             * routed directly to MmArmAccessFault WITHOUT acquiring any
             * working set locks (nested fault during page table manipulation).
             */
            if (((ULONG_PTR)Address >= (ULONG_PTR)PTE_BASE) &&
                ((ULONG_PTR)Address < (ULONG_PTR)PTE_TOP))
            {
                return MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
            }

            /*
             * ARM64 SYSTEM CACHE FAST PATH:
             *
             * System Cache addresses MUST skip the VAD lookup below to avoid
             * deadlock (same thread may already hold MmSystemCacheWs lock).
             */
            if (((ULONG_PTR)Address >= MI_SYSTEM_CACHE_START &&
                 (ULONG_PTR)Address <= MI_SYSTEM_CACHE_END))
            {
                goto Retry;
            }
#endif

            /*
             * ARM64: avoid recursive WS lock acquisition.
             *
             * This kernel VAD probe takes MmSystemCacheWs shared. If the fault
             * happens while the thread already owns any WS lock (common in nested
             * fault paths), MiLockWorkingSetShared asserts on !MM_ANY_WS_LOCK_HELD.
             *
             * In that context, route directly to ARM3 fault handling. The special
             * RosMM system cache range above already jumps to Retry, so this
             * fallback only applies to non-system-cache kernel addresses.
             */
            if (
#if defined(_M_ARM64) || defined(__aarch64__)
                MM_ANY_WS_LOCK_HELD(PsGetCurrentThread())
#else
                FALSE
#endif
                )
            {
                IsArm3Fault = TRUE;
            }
            else
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
        }
        else
        {
            /*
             * ARM64: same recursive-lock hazard for user VAD probe.
             * If a WS lock is already held, skip the shared-lock lookup and
             * route to ARM3 fault handling.
             */
            if (
#if defined(_M_ARM64) || defined(__aarch64__)
                MM_ANY_WS_LOCK_HELD(PsGetCurrentThread())
#else
                FALSE
#endif
                )
            {
                IsArm3Fault = TRUE;
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

        {
            NTSTATUS Status = MmArmAccessFault(FaultCode, Address, Mode, TrapInformation);
            return Status;
        }
    }

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
