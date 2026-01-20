/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/pagfault.c
 * PURPOSE:         ARM Memory Manager Page Fault Handling
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#if defined(_M_AMD64)
static
VOID
MiZeroPhysicalPageBootstrap(IN PFN_NUMBER PageFrameNumber)
{
    KIRQL HyperIrql;
    PVOID Va;
    PEPROCESS HyperProcess;

    /*
     * Early zeroing runs before the PFN database is live, but we are already
     * executing in the context of the system process.  Assert this invariant so
     * hyperspace locking has a valid owner while MiPfnsInitialized is FALSE.
     */
    HyperProcess = PsGetCurrentProcess();
    ASSERT(HyperProcess != NULL);
    if (PsInitialSystemProcess != NULL)
    {
        ASSERT(HyperProcess == PsInitialSystemProcess);
    }

    Va = MiMapPageInHyperSpace(HyperProcess, PageFrameNumber, &HyperIrql);
    ASSERT(Va != NULL);
    KeZeroPages(Va, PAGE_SIZE);
    MiUnmapPageInHyperSpace(HyperProcess, Va, HyperIrql);
}
#endif

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

VOID
NTAPI
MmRebalanceMemoryConsumersAndWait(VOID);

/* GLOBALS ********************************************************************/

#if defined(_M_ARM64) || defined(__aarch64__)
/* Debug counter to verify MmArmAccessFault entry */
volatile LONG MmArmAccessFaultEntryCount = 0;
volatile PVOID MmArmAccessFaultLastAddress = NULL;
volatile LONG MmArmAccessFaultInFunction = 0;
#endif

#define HYDRA_PROCESS (PEPROCESS)1
#if MI_TRACE_PFNS
BOOLEAN UserPdeFault = FALSE;
#endif

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
NTAPI
MiCheckForUserStackOverflow(IN PVOID Address,
                            IN PVOID TrapInformation)
{
    PETHREAD CurrentThread = PsGetCurrentThread();
    PTEB Teb = CurrentThread->Tcb.Teb;
    PVOID StackBase, DeallocationStack;
    PVOID OldGuardBase, GuardAddress, CommitAddress, AlignedDeallocation;
    SIZE_T GuaranteedSize;
    SIZE_T GuardPageSize, AccessibleBytes, AdditionalCommit;
    SIZE_T GuardRegionSize, CommitRegionSize;
    NTSTATUS Status;

    /* Do we own the address space lock? */
    if (CurrentThread->AddressSpaceOwner == 1)
    {
        /* This isn't valid */
        DPRINT1("Process owns address space lock\n");
        ASSERT(KeAreAllApcsDisabled() == TRUE);
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    /* Are we attached? */
    if (KeIsAttachedProcess())
    {
        /* This isn't valid */
        DPRINT1("Process is attached\n");
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    /* Read the current settings */
    StackBase = Teb->NtTib.StackBase;
    DeallocationStack = Teb->DeallocationStack;
    GuaranteedSize = Teb->GuaranteedStackBytes;
    GuardPageSize = PAGE_SIZE;

    if (GuaranteedSize)
    {
        AccessibleBytes = ROUND_TO_PAGES(GuaranteedSize);
        if (AccessibleBytes < GuardPageSize)
        {
            AccessibleBytes = GuardPageSize;
        }
    }
    else
    {
        AccessibleBytes = GuardPageSize;
    }

    AdditionalCommit = AccessibleBytes - GuardPageSize;
    OldGuardBase = (PVOID)PAGE_ALIGN(Address);
    AlignedDeallocation = PAGE_ALIGN(DeallocationStack);
    CommitAddress = (PVOID)((ULONG_PTR)OldGuardBase - AdditionalCommit);
    GuardAddress = (PVOID)((ULONG_PTR)CommitAddress - GuardPageSize);

    DPRINT("Handling guard page fault with Stack Addresses 0x%p and 0x%p, guarantee: %Ix (%Iu commit bytes)\n",
           StackBase,
           DeallocationStack,
           GuaranteedSize,
           AccessibleBytes);

    if ((ULONG_PTR)OldGuardBase <= (ULONG_PTR)AlignedDeallocation + GuardPageSize)
    {
        DPRINT1("Stack near base, cannot place guard page\n");
        goto StackOverflow;
    }

    if ((ULONG_PTR)GuardAddress < (ULONG_PTR)AlignedDeallocation)
    {
        DPRINT1("Insufficient stack space for requested guarantee\n");
        goto StackOverflow;
    }

    if (AdditionalCommit)
    {
        CommitRegionSize = AdditionalCommit;
        Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                         &CommitAddress,
                                         0,
                                         &CommitRegionSize,
                                         MEM_COMMIT,
                                         PAGE_READWRITE);
        if (!NT_SUCCESS(Status) && (Status != STATUS_ALREADY_COMMITTED))
        {
            DPRINT1("Failed to extend stack by %Iu bytes: %lx\n",
                    CommitRegionSize,
                    Status);
            goto StackOverflow;
        }

        /* Use the actual region returned by ZwAllocateVirtualMemory */
        GuardAddress = (PVOID)((ULONG_PTR)CommitAddress - GuardPageSize);
        if ((ULONG_PTR)GuardAddress < (ULONG_PTR)AlignedDeallocation)
        {
            DPRINT1("Stack extension overlapped deallocation region\n");
            goto StackOverflow;
        }
    }

    /* Does this faulting stack address actually exist in the stack? */
    if ((Address >= StackBase) || (Address < DeallocationStack))
    {
        /* That's odd... */
        PEPROCESS Process = PsGetCurrentProcess();

        DPRINT1("Faulting address outside of stack bounds. Address=%p, StackBase=%p, DeallocationStack=%p (proc %s pid %lu tid %lu)\n",
                Address,
                StackBase,
                DeallocationStack,
                Process ? Process->ImageFileName : "<unknown>",
                (ULONG)(ULONG_PTR)PsGetCurrentProcessId(),
                (ULONG)(ULONG_PTR)PsGetCurrentThreadId());
#if defined(_M_AMD64) || defined(_M_IX86)
        {
            PKTRAP_FRAME TrapFrame = KeGetCurrentThread()->TrapFrame;
            if (TrapFrame)
            {
#if defined(_M_AMD64)
                DPRINT1("Trap frame during guard fault: RIP=%p RSP=%p RBP=%p RCX=%p RDX=%p\n",
                        (PVOID)TrapFrame->Rip,
                        (PVOID)TrapFrame->Rsp,
                        (PVOID)TrapFrame->Rbp,
                        (PVOID)TrapFrame->Rcx,
                        (PVOID)TrapFrame->Rdx);
#else
                DPRINT1("Trap frame during guard fault: EIP=%p ESP=%p EBP=%p ECX=%p EDX=%p\n",
                        (PVOID)TrapFrame->Eip,
                        (PVOID)TrapFrame->HardwareEsp,
                        (PVOID)TrapFrame->Ebp,
                        (PVOID)TrapFrame->Ecx,
                        (PVOID)TrapFrame->Edx);
#endif
            }
        }
#endif
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    /* Don't handle this flag yet */
    ASSERT((PsGetCurrentProcess()->Peb->NtGlobalFlag & FLG_DISABLE_STACK_EXTENSION) == 0);

    /* Update the stack limit with the committed base */
    Teb->NtTib.StackLimit = CommitAddress;

    /* Install the new guard page */
    GuardRegionSize = GuardPageSize;
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     &GuardAddress,
                                     0,
                                     &GuardRegionSize,
                                     MEM_COMMIT,
                                     PAGE_READWRITE | PAGE_GUARD);
    if ((NT_SUCCESS(Status)) || (Status == STATUS_ALREADY_COMMITTED))
    {
        DPRINT("Guard page handled successfully for %p\n", Address);
        return STATUS_PAGE_FAULT_GUARD_PAGE;
    }

    DPRINT1("Guard page failure: %lx\n", Status);
    ASSERT(FALSE);
    return STATUS_STACK_OVERFLOW;

StackOverflow:
    {
        SIZE_T RemainingSize;
        PVOID RemainingBase;
        PEPROCESS Process = PsGetCurrentProcess();

        DPRINT1("Close to our death... stack exhausted for proc %s pid %lu tid %lu at %p\n",
                Process ? Process->ImageFileName : "<unknown>",
                (ULONG)(ULONG_PTR)PsGetCurrentProcessId(),
                (ULONG)(ULONG_PTR)PsGetCurrentThreadId(),
                Address);
#if defined(_M_AMD64) || defined(_M_IX86)
        {
            PKTRAP_FRAME TrapFrame = KeGetCurrentThread()->TrapFrame;
            if (TrapFrame)
            {
#if defined(_M_AMD64)
                DPRINT1("Guard overflow context: RIP=%p RSP=%p RBP=%p RCX=%p RDX=%p\n",
                        (PVOID)TrapFrame->Rip,
                        (PVOID)TrapFrame->Rsp,
                        (PVOID)TrapFrame->Rbp,
                        (PVOID)TrapFrame->Rcx,
                        (PVOID)TrapFrame->Rdx);
#else
                DPRINT1("Guard overflow context: EIP=%p ESP=%p EBP=%p ECX=%p EDX=%p\n",
                        (PVOID)TrapFrame->Eip,
                        (PVOID)TrapFrame->HardwareEsp,
                        (PVOID)TrapFrame->Ebp,
                        (PVOID)TrapFrame->Ecx,
                        (PVOID)TrapFrame->Edx);
#endif
            }
        }
#endif

        RemainingBase = AlignedDeallocation;
        RemainingSize = (ULONG_PTR)OldGuardBase - (ULONG_PTR)RemainingBase;
        if (RemainingSize)
        {
            Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                             &RemainingBase,
                                             0,
                                             &RemainingSize,
                                             MEM_COMMIT,
                                             PAGE_READWRITE);
            if (NT_SUCCESS(Status) || (Status == STATUS_ALREADY_COMMITTED))
            {
                Teb->NtTib.StackLimit = (PVOID)((ULONG_PTR)RemainingBase + RemainingSize);
            }
            else
            {
                DPRINT1("Failed to allocate remaining stack memory: %lx\n", Status);
            }
        }

        return STATUS_STACK_OVERFLOW;
    }
}

FORCEINLINE
BOOLEAN
MiIsAccessAllowed(
    _In_ ULONG ProtectionMask,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN Execute)
{
    #define _BYTE_MASK(Bit0, Bit1, Bit2, Bit3, Bit4, Bit5, Bit6, Bit7) \
        (Bit0) | ((Bit1) << 1) | ((Bit2) << 2) | ((Bit3) << 3) | \
        ((Bit4) << 4) | ((Bit5) << 5) | ((Bit6) << 6) | ((Bit7) << 7)
    static const UCHAR AccessAllowedMask[2][2] =
    {
        {   // Protect 0  1  2  3  4  5  6  7
            _BYTE_MASK(0, 1, 1, 1, 1, 1, 1, 1), // READ
            _BYTE_MASK(0, 0, 1, 1, 0, 0, 1, 1), // EXECUTE READ
        },
        {
            _BYTE_MASK(0, 0, 0, 0, 1, 1, 1, 1), // WRITE
            _BYTE_MASK(0, 0, 0, 0, 0, 0, 1, 1), // EXECUTE WRITE
        }
    };

    /* We want only the lower access bits */
    ProtectionMask &= MM_PROTECT_ACCESS;

    /* Look it up in the table */
    return (AccessAllowedMask[Write != 0][Execute != 0] >> ProtectionMask) & 1;
}

static
NTSTATUS
NTAPI
MiAccessCheck(IN PMMPTE PointerPte,
              IN BOOLEAN StoreInstruction,
              IN KPROCESSOR_MODE PreviousMode,
              IN ULONG_PTR ProtectionMask,
              IN PVOID TrapFrame,
              IN BOOLEAN LockHeld)
{
    MMPTE TempPte;

    /* Check for invalid user-mode access */
    if ((PreviousMode == UserMode) && (PointerPte > MiHighestUserPte))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    /* Capture the PTE -- is it valid? */
    TempPte = *PointerPte;
    if (TempPte.u.Hard.Valid)
    {
        /* Was someone trying to write to it? */
        if (StoreInstruction)
        {
            /* Is it writable?*/
            if (MI_IS_PAGE_WRITEABLE(&TempPte) ||
                MI_IS_PAGE_COPY_ON_WRITE(&TempPte))
            {
                /* Then there's nothing to worry about */
                return STATUS_SUCCESS;
            }

            /* Oops! This isn't allowed */
            return STATUS_ACCESS_VIOLATION;
        }

        /* Someone was trying to read from a valid PTE, that's fine too */
        return STATUS_SUCCESS;
    }

    /* Check if the protection on the page allows what is being attempted */
    if (!MiIsAccessAllowed(ProtectionMask, StoreInstruction, FALSE))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    /* Check if this is a guard page */
    if ((ProtectionMask & MM_PROTECT_SPECIAL) == MM_GUARDPAGE)
    {
        ASSERT(ProtectionMask != MM_DECOMMIT);

        /* Attached processes can't expand their stack */
        if (KeIsAttachedProcess()) return STATUS_ACCESS_VIOLATION;

        /* No support for prototype PTEs yet */
        ASSERT(TempPte.u.Soft.Prototype == 0);

        /* Remove the guard page bit, and return a guard page violation */
        TempPte.u.Soft.Protection = ProtectionMask & ~MM_GUARDPAGE;
        ASSERT(TempPte.u.Long != 0);
        MI_WRITE_INVALID_PTE(PointerPte, TempPte);
        return STATUS_GUARD_PAGE_VIOLATION;
    }

    /* Nothing to do */
    return STATUS_SUCCESS;
}

static
PMMPTE
NTAPI
MiCheckVirtualAddress(IN PVOID VirtualAddress,
                      OUT PULONG ProtectCode,
                      OUT PMMVAD *ProtoVad)
{
    PMMVAD Vad;
    PMMPTE PointerPte;

    /* No prototype/section support for now */
    *ProtoVad = NULL;

    /* User or kernel fault? */
    if (VirtualAddress <= MM_HIGHEST_USER_ADDRESS)
    {
        /* Special case for shared data */
        if (PAGE_ALIGN(VirtualAddress) == (PVOID)MM_SHARED_USER_DATA_VA)
        {
            /* It's a read-only page */
            *ProtectCode = MM_READONLY;
            return MmSharedUserDataPte;
        }

        /* Find the VAD, it might not exist if the address is bogus */
        Vad = MiLocateAddress(VirtualAddress);
        if (!Vad)
        {
            /* Bogus virtual address */
            *ProtectCode = MM_NOACCESS;
            return NULL;
        }

        /* ReactOS does not handle physical memory VADs yet */
        ASSERT(Vad->u.VadFlags.VadType != VadDevicePhysicalMemory);

        /* Check if it's a section, or just an allocation */
        if (Vad->u.VadFlags.PrivateMemory)
        {
            /* ReactOS does not handle AWE VADs yet */
            ASSERT(Vad->u.VadFlags.VadType != VadAwe);

            /* This must be a TEB/PEB VAD */
            if (Vad->u.VadFlags.MemCommit)
            {
                /* It's committed, so return the VAD protection */
                *ProtectCode = (ULONG)Vad->u.VadFlags.Protection;
            }
            else
            {
                /* It has not yet been committed, so return no access */
                *ProtectCode = MM_NOACCESS;
            }

            /* In both cases, return no PTE */
            return NULL;
        }
        else
        {
            /* ReactOS does not supoprt these VADs yet */
            ASSERT(Vad->u.VadFlags.VadType != VadImageMap);
            ASSERT(Vad->u2.VadFlags2.ExtendableFile == 0);

            /* Return the proto VAD */
            *ProtoVad = Vad;

            /* Get the prototype PTE for this page */
            PointerPte = (((ULONG_PTR)VirtualAddress >> PAGE_SHIFT) - Vad->StartingVpn) + Vad->FirstPrototypePte;
            ASSERT(PointerPte != NULL);
            ASSERT(PointerPte <= Vad->LastContiguousPte);

            /* Return the Prototype PTE and the protection for the page mapping */
            *ProtectCode = (ULONG)Vad->u.VadFlags.Protection;
            return PointerPte;
        }
    }
    else if (MI_IS_PAGE_TABLE_ADDRESS(VirtualAddress))
    {
        /* This should never happen, as these addresses are handled by the double-maping */
        if (((PMMPTE)VirtualAddress >= MiAddressToPte(MmPagedPoolStart)) &&
            ((PMMPTE)VirtualAddress <= MmPagedPoolInfo.LastPteForPagedPool))
        {
            /* Fail such access */
            *ProtectCode = MM_NOACCESS;
            return NULL;
        }

        /* Return full access rights */
        *ProtectCode = MM_EXECUTE_READWRITE;
        return NULL;
    }
    else if (MI_IS_SESSION_ADDRESS(VirtualAddress))
    {
        /* ReactOS does not have an image list yet, so bail out to failure case */
        ASSERT(IsListEmpty(&MmSessionSpace->ImageList));
    }
    /*
     * NOTE: Kernel section views (like VACB buffers) are NOT handled here.
     * They are handled by the ROS memory manager (MmNotPresentFaultSectionView)
     * which is called via MmNotPresentFault. The fault routing in MmAccessFault
     * (mmfault.c) is responsible for directing kernel section view faults
     * to the correct handler based on whether the VAD is marked as ROS VAD.
     *
     * If we reach MmArmAccessFault for a kernel section view, it means the
     * fault routing is broken and needs to be fixed in MmAccessFault, not here.
     */

    /* Default case -- failure */
    *ProtectCode = MM_NOACCESS;
    return NULL;
}

#if (_MI_PAGING_LEVELS == 2)
static
NTSTATUS
FASTCALL
MiCheckPdeForSessionSpace(IN PVOID Address)
{
    MMPTE TempPde;
    PMMPDE PointerPde;
    PVOID SessionAddress;
    ULONG Index;

    /* Is this a session PTE? */
    if (MI_IS_SESSION_PTE(Address))
    {
        /* Make sure the PDE for session space is valid */
        PointerPde = MiAddressToPde(MmSessionSpace);
        if (!PointerPde->u.Hard.Valid)
        {
            /* This means there's no valid session, bail out */
            DbgPrint("MiCheckPdeForSessionSpace: No current session for PTE %p\n",
                     Address);
            DbgBreakPoint();
            return STATUS_ACCESS_VIOLATION;
        }

        /* Now get the session-specific page table for this address */
        SessionAddress = MiPteToAddress(Address);
        PointerPde = MiAddressToPte(Address);
        if (PointerPde->u.Hard.Valid) return STATUS_WAIT_1;

        /* It's not valid, so find it in the page table array */
        Index = ((ULONG_PTR)SessionAddress - (ULONG_PTR)MmSessionBase) >> 22;
        TempPde.u.Long = MmSessionSpace->PageTables[Index].u.Long;
        if (TempPde.u.Hard.Valid)
        {
            /* The copy is valid, so swap it in */
            InterlockedExchange((PLONG)PointerPde, TempPde.u.Long);
            return STATUS_WAIT_1;
        }

        /* We don't seem to have allocated a page table for this address yet? */
        DbgPrint("MiCheckPdeForSessionSpace: No Session PDE for PTE %p, %p\n",
                 PointerPde->u.Long, SessionAddress);
        DbgBreakPoint();
        return STATUS_ACCESS_VIOLATION;
    }

    /* Is the address also a session address? If not, we're done */
    if (!MI_IS_SESSION_ADDRESS(Address)) return STATUS_SUCCESS;

    /* It is, so again get the PDE for session space */
    PointerPde = MiAddressToPde(MmSessionSpace);
    if (!PointerPde->u.Hard.Valid)
    {
        /* This means there's no valid session, bail out */
        DbgPrint("MiCheckPdeForSessionSpace: No current session for VA %p\n",
                    Address);
        DbgBreakPoint();
        return STATUS_ACCESS_VIOLATION;
    }

    /* Now get the PDE for the address itself */
    PointerPde = MiAddressToPde(Address);
    if (!PointerPde->u.Hard.Valid)
    {
        /* Do the swap, we should be good to go */
        Index = ((ULONG_PTR)Address - (ULONG_PTR)MmSessionBase) >> 22;
        PointerPde->u.Long = MmSessionSpace->PageTables[Index].u.Long;
        if (PointerPde->u.Hard.Valid) return STATUS_WAIT_1;

        /* We had not allocated a page table for this session address yet, fail! */
        DbgPrint("MiCheckPdeForSessionSpace: No Session PDE for VA %p, %p\n",
                 PointerPde->u.Long, Address);
        DbgBreakPoint();
        return STATUS_ACCESS_VIOLATION;
    }

    /* It's valid, so there's nothing to do */
    return STATUS_SUCCESS;
}

NTSTATUS
FASTCALL
MiCheckPdeForPagedPool(IN PVOID Address)
{
    PMMPDE PointerPde;
    NTSTATUS Status = STATUS_SUCCESS;

    /* Check session PDE */
    if (MI_IS_SESSION_ADDRESS(Address)) return MiCheckPdeForSessionSpace(Address);
    if (MI_IS_SESSION_PTE(Address)) return MiCheckPdeForSessionSpace(Address);

    //
    // Check if this is a fault while trying to access the page table itself
    //
    if (MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address))
    {
        //
        // Send a hint to the page fault handler that this is only a valid fault
        // if we already detected this was access within the page table range
        //
        PointerPde = (PMMPDE)MiAddressToPte(Address);
        Status = STATUS_WAIT_1;
    }
    else if (Address < MmSystemRangeStart)
    {
        //
        // This is totally illegal
        //
        return STATUS_ACCESS_VIOLATION;
    }
    else
    {
        //
        // Get the PDE for the address
        //
        PointerPde = MiAddressToPde(Address);
    }

    //
    // Check if it's not valid
    //
    if (PointerPde->u.Hard.Valid == 0)
    {
        //
        // Copy it from our double-mapped system page directory
        //
        InterlockedExchangePte(PointerPde,
                               MmSystemPagePtes[((ULONG_PTR)PointerPde & (SYSTEM_PD_SIZE - 1)) / sizeof(MMPTE)].u.Long);
    }

    //
    // Return status
    //
    return Status;
}
#else
NTSTATUS
FASTCALL
MiCheckPdeForPagedPool(IN PVOID Address)
{
    return STATUS_ACCESS_VIOLATION;
}
#endif

VOID
NTAPI
MiZeroPfn(IN PFN_NUMBER PageFrameNumber)
{
    PMMPTE ZeroPte;
    MMPTE TempPte;
    PMMPFN Pfn1;
    PVOID ZeroAddress;

    /* Get the PFN for this page */
    Pfn1 = MiGetPfnEntry(PageFrameNumber);
    ASSERT(Pfn1);

    /* Grab a system PTE we can use to zero the page */
    ZeroPte = MiReserveSystemPtes(1, SystemPteSpace);
    ASSERT(ZeroPte);

    /* Initialize the PTE for it */
    TempPte = ValidKernelPte;
    TempPte.u.Hard.PageFrameNumber = PageFrameNumber;

    /* Setup caching */
    if (Pfn1->u3.e1.CacheAttribute == MiWriteCombined)
    {
        /* Write combining, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_COMBINED(&TempPte);
    }
    else if (Pfn1->u3.e1.CacheAttribute == MiNonCached)
    {
        /* Write through, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_THROUGH(&TempPte);
    }

    /* Make the system PTE valid with our PFN */
    MI_WRITE_VALID_PTE(ZeroPte, TempPte);

    /* Get the address it maps to, and zero it out */
    ZeroAddress = MiPteToAddress(ZeroPte);
    KeZeroPages(ZeroAddress, PAGE_SIZE);

    /* Now get rid of it */
    MiReleaseSystemPtes(ZeroPte, 1, SystemPteSpace);
}

VOID
NTAPI
MiCopyPfn(
    _In_ PFN_NUMBER DestPage,
    _In_ PFN_NUMBER SrcPage)
{
    PMMPTE SysPtes;
    MMPTE TempPte;
    PMMPFN DestPfn, SrcPfn;
    PVOID DestAddress;
    const VOID* SrcAddress;

    /* Get the PFNs */
    DestPfn = MiGetPfnEntry(DestPage);
    ASSERT(DestPfn);
    SrcPfn = MiGetPfnEntry(SrcPage);
    ASSERT(SrcPfn);

    /* Grab 2 system PTEs */
    SysPtes = MiReserveSystemPtes(2, SystemPteSpace);
    ASSERT(SysPtes);

    /* Initialize the destination PTE */
    TempPte = ValidKernelPte;
    TempPte.u.Hard.PageFrameNumber = DestPage;

    /* Setup caching */
    if (DestPfn->u3.e1.CacheAttribute == MiWriteCombined)
    {
        /* Write combining, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_COMBINED(&TempPte);
    }
    else if (DestPfn->u3.e1.CacheAttribute == MiNonCached)
    {
        /* Write through, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_THROUGH(&TempPte);
    }

    /* Make the system PTE valid with our PFN */
    MI_WRITE_VALID_PTE(&SysPtes[0], TempPte);

    /* Initialize the source PTE */
    TempPte = ValidKernelPte;
    TempPte.u.Hard.PageFrameNumber = SrcPage;

    /* Setup caching */
    if (SrcPfn->u3.e1.CacheAttribute == MiNonCached)
    {
        MI_PAGE_DISABLE_CACHE(&TempPte);
    }

    /* Make the system PTE valid with our PFN */
    MI_WRITE_VALID_PTE(&SysPtes[1], TempPte);

    /* Get the addresses and perform the copy */
    DestAddress = MiPteToAddress(&SysPtes[0]);
    SrcAddress = MiPteToAddress(&SysPtes[1]);
    RtlCopyMemory(DestAddress, SrcAddress, PAGE_SIZE);

    /* Now get rid of it */
    MiReleaseSystemPtes(SysPtes, 2, SystemPteSpace);
}

static
NTSTATUS
NTAPI
MiResolveDemandZeroFault(IN PVOID Address,
                         IN PMMPTE PointerPte,
                         IN ULONG Protection,
                         IN PEPROCESS Process,
                         IN KIRQL OldIrql)
{
    PFN_NUMBER PageFrameNumber = 0;
    MMPTE TempPte;
    BOOLEAN NeedZero = FALSE, HaveLock = FALSE;
    BOOLEAN BootstrapAllocation = FALSE;
    ULONG Color;
    PMMPFN Pfn1;
    DPRINT("ARM3 Demand Zero Page Fault Handler for address: %p in process: %p\n",
            Address,
            Process);

    /* Must currently only be called by paging path */
    if ((Process > HYDRA_PROCESS) && (OldIrql == MM_NOIRQL))
    {
        /* Sanity check */
        ASSERT(MI_IS_PAGE_TABLE_ADDRESS(PointerPte));

        /* No forking yet */
        ASSERT(Process->ForkInProgress == NULL);

        /* Get process color */
        Color = MI_GET_NEXT_PROCESS_COLOR(Process);
        ASSERT(Color != 0xFFFFFFFF);

        /* We'll need a zero page */
        NeedZero = TRUE;
    }
    else
    {
        /* Check if we need a zero page */
        NeedZero = (OldIrql != MM_NOIRQL);

        /* Session-backed image views must be zeroed */
        if ((Process == HYDRA_PROCESS) &&
            ((MI_IS_SESSION_IMAGE_ADDRESS(Address)) ||
             ((Address >= MiSessionViewStart) && (Address < MiSessionSpaceWs))))
        {
            NeedZero = TRUE;
        }

        /* Hardcode unknown color */
        Color = 0xFFFFFFFF;
    }

    /* Check if the PFN database should be acquired */
    if (OldIrql == MM_NOIRQL)
    {
        /* Acquire it and remember we should release it after */
        OldIrql = MiAcquirePfnLock();
        HaveLock = TRUE;
    }

    /* We either manually locked the PFN DB, or already came with it locked */
    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT(PointerPte->u.Hard.Valid == 0);

    /* Assert we have enough pages */
    //ASSERT(MmAvailablePages >= 32);

#if MI_TRACE_PFNS
    if (UserPdeFault) MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    if (!UserPdeFault) MI_SET_USAGE(MI_USAGE_DEMAND_ZERO);
#endif
    if (Process == HYDRA_PROCESS) MI_SET_PROCESS2("Hydra");
    else if (Process) MI_SET_PROCESS2(Process->ImageFileName);
    else MI_SET_PROCESS2("Kernel Demand 0");

    /* When the PFN database is still being constructed, fall back to the
       loader's bootstrap allocator which does not rely on the coloured lists. */
#ifdef _M_AMD64
    if (!MiPfnsInitialized)
    {
        PageFrameNumber = MxGetNextPage(1);
        if (PageFrameNumber == 0)
        {
            if (HaveLock)
            {
                MiReleasePfnLock(OldIrql);
                HaveLock = FALSE;
            }
            return STATUS_NO_MEMORY;
        }

        NeedZero = TRUE;
        BootstrapAllocation = TRUE;
        if (HaveLock)
        {
            MiReleasePfnLock(OldIrql);
            HaveLock = FALSE;
        }
    }
#endif

    if (!BootstrapAllocation)
    {
        /* Do we need a zero page? */
        if (Color != 0xFFFFFFFF)
        {
            /* Try to get one, if we couldn't grab a free page and zero it */
            PageFrameNumber = MiRemoveZeroPageSafe(Color);
            if (!PageFrameNumber)
            {
                /* We'll need a free page and zero it manually */
                PageFrameNumber = MiRemoveAnyPage(Color);
                NeedZero = TRUE;
            }
            else
            {
                /* Page guaranteed to be zero-filled */
                NeedZero = FALSE;
            }
        }
        else
        {
            /* Get a color, and see if we should grab a zero or non-zero page */
            Color = MI_GET_NEXT_COLOR();
            if (!NeedZero)
            {
                /* Process or system doesn't want a zero page, grab anything */
                PageFrameNumber = MiRemoveAnyPage(Color);
            }
            else
            {
                /* System wants a zero page, obtain one */
                PageFrameNumber = MiRemoveZeroPage(Color);
                /* No need to zero-fill it */
                NeedZero = FALSE;
            }
        }
    }

    if (PageFrameNumber == 0)
    {
        MiReleasePfnLock(OldIrql);
        return STATUS_NO_MEMORY;
    }

    /* Initialize PFN bookkeeping when the database is available */
    if (!BootstrapAllocation)
    {
        MiInitializePfn(PageFrameNumber, PointerPte, TRUE);
    }

    /* Increment demand zero faults */
    KeGetCurrentPrcb()->MmDemandZeroCount++;

    /* Do we have the lock? */
    if (HaveLock)
    {
        /* Release it */
        MiReleasePfnLock(OldIrql);

        /* Update performance counters */
        if (Process > HYDRA_PROCESS) Process->NumberOfPrivatePages++;
    }

    /* Zero the page if need be */
    if (NeedZero)
    {
#ifdef _M_AMD64
        if (BootstrapAllocation)
        {
            MiZeroPhysicalPageBootstrap(PageFrameNumber);
        }
        else
#endif
        {
            MiZeroPfn(PageFrameNumber);
        }
    }

    /* Fault on user PDE, or fault on user PTE? */
    if (PointerPte <= MiHighestUserPte)
    {
        /* User fault, build a user PTE */
        MI_MAKE_HARDWARE_PTE_USER(&TempPte,
                                  PointerPte,
                                  Protection,
                                  PageFrameNumber);
    }
    else
    {
        /* This is a user-mode PDE, create a kernel PTE for it */
        MI_MAKE_HARDWARE_PTE(&TempPte,
                             PointerPte,
                             Protection,
                             PageFrameNumber);
    }

    /* Set it dirty if it's a writable page */
    if (MI_IS_PAGE_WRITEABLE(&TempPte)) MI_MAKE_DIRTY_PAGE(&TempPte);

    /* Write it */
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

#if defined(_M_ARM64) && DBG
    /* ARM64 Debug: Log prototype PTE creation */
    extern PVOID MiSystemViewStart;
    if (!MI_IS_PAGE_TABLE_ADDRESS(PointerPte))  /* This is a prototype PTE */
    {
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MiResolveDemandZeroFault: Wrote prototype PTE\n"
                "  Address=%p\n"
                "  PointerPte=%p (PROTO PTE in paged pool)\n"
                "  TempPte.u.Long=0x%llx\n"
                "  PageFrameNumber=0x%llx (PA=0x%llx)\n",
                Address,
                PointerPte,
                (ULONG64)TempPte.u.Long,
                (ULONG64)PageFrameNumber, (ULONG64)(PageFrameNumber << PAGE_SHIFT));
        }
    }
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64: Memory Ordering After PTE Write
     *
     * MiResolveDemandZeroFault can be called in two scenarios:
     *
     * 1. Direct demand-zero fault: PointerPte is the actual page table PTE,
     *    and Address is the faulting VA. In this case, we need TLB invalidation.
     *
     * 2. Prototype PTE resolution: Called from MiResolveProtoPteFault with
     *    PointerPte = PointerProtoPte (the prototype PTE in paged pool).
     *    In this case, we're writing to the prototype PTE, not the actual PTE,
     *    so we DON'T invalidate the TLB for Address (that happens later in
     *    MiCompleteProtoPteFault when the actual PTE is written).
     *
     * We can distinguish these cases by checking if PointerPte is in the
     * page table address range (PTE_BASE region) or in paged pool.
     */
    if (MI_IS_PAGE_TABLE_ADDRESS(PointerPte))
    {
        /*
         * This is an actual page table PTE, not a prototype PTE.
         * Invalidate the TLB for the faulting address.
         */
        ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
        __asm__ __volatile__(
            "dsb ishst\n\t"           /* Ensure PTE write is visible */
            "tlbi vae1is, %0\n\t"     /* Invalidate TLB for this VA */
            "dsb ish\n\t"             /* Wait for TLB invalidation */
            "isb"                      /* Synchronize instruction pipeline */
            : : "r"(VaForTlbi) : "memory");
    }
    else
    {
        /*
         * This is a prototype PTE in paged pool. The actual TLB invalidation
         * will happen in MiCompleteProtoPteFault. We still need a memory
         * barrier to ensure the PTE write is visible before we return.
         */
        __asm__ __volatile__("dsb ishst" ::: "memory");
    }
#endif

    /* Did we manually acquire the lock */
    if (HaveLock)
    {
        /* Get the PFN entry */
        Pfn1 = MI_PFN_ELEMENT(PageFrameNumber);

        /* Windows does these sanity checks */
        ASSERT(Pfn1->u1.Event == 0);
        ASSERT(Pfn1->u3.e1.PrototypePte == 0);
    }

    //
    // It's all good now
    //
    DPRINT("Demand zero page has now been paged in\n");
    return STATUS_PAGE_FAULT_DEMAND_ZERO;
}

static
NTSTATUS
NTAPI
MiCompleteProtoPteFault(IN BOOLEAN StoreInstruction,
                        IN PVOID Address,
                        IN PMMPTE PointerPte,
                        IN PMMPTE PointerProtoPte,
                        IN KIRQL OldIrql,
                        IN PMMPFN* LockedProtoPfn)
{
    MMPTE TempPte;
    PMMPTE OriginalPte, PageTablePte;
    ULONG_PTR Protection;
    PFN_NUMBER PageFrameIndex;
    PMMPFN Pfn1, Pfn2;
    BOOLEAN OriginalProtection, DirtyPage;

    /* Must be called with an valid prototype PTE, with the PFN lock held */
    MI_ASSERT_PFN_LOCK_HELD();

    ASSERT(PointerProtoPte->u.Hard.Valid == 1);

    /* Get the page */
    PageFrameIndex = PFN_FROM_PTE(PointerProtoPte);

#if defined(_M_ARM64) && DBG
    /* ARM64 Debug: Log PFN extraction from prototype PTE */
    extern PVOID MiSystemViewStart;
    if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
        (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[arm64] MiCompleteProtoPteFault: PFN extraction\n"
            "  Address=%p\n"
            "  PointerProtoPte=%p ProtoPte->u.Long=0x%llx\n"
            "  PageFrameNumber=0x%llx (PA=0x%llx)\n",
            Address,
            PointerProtoPte, (ULONG64)PointerProtoPte->u.Long,
            (ULONG64)PageFrameIndex, (ULONG64)(PageFrameIndex << PAGE_SHIFT));
    }
#endif

    /* Get the PFN entry and set it as a prototype PTE */
    Pfn1 = MiGetPfnEntry(PageFrameIndex);
    Pfn1->u3.e1.PrototypePte = 1;

    /* Increment the share count for the page table */
    PageTablePte = MiAddressToPte(PointerPte);

#if defined(_M_ARM64) || defined(__aarch64__)
    //
    // ARM64: Validate that the page table PTE is valid before accessing PageFrameNumber.
    // This should always be true since we just wrote to PointerPte, but verify to catch
    // any subtle issues with page table mapping.
    //
    if (!PageTablePte->u.Hard.Valid)
    {
        DPRINT1("[pagfault] MiCompleteProtoPteFault: PageTablePte is INVALID!\n");
        DPRINT1("[pagfault]   PointerPte=%p PageTablePte=%p PageTablePte->u.Long=0x%llx\n",
                PointerPte, PageTablePte, (unsigned long long)PageTablePte->u.Long);
        DPRINT1("[pagfault]   This should never happen - the page table must be mapped to write PointerPte\n");
        //
        // On ARM64, accessing u.Hard.PageFrameNumber on an invalid PTE can return 0,
        // which would cause a crash when accessing PFN database entry 0.
        // Return an error instead.
        //
        MiReleasePfnLock(OldIrql);
        return STATUS_INTERNAL_ERROR;
    }
#endif

    Pfn2 = MiGetPfnEntry(PageTablePte->u.Hard.PageFrameNumber);
    Pfn2->u2.ShareCount++;

    /* Check where we should be getting the protection information from */
    if (PointerPte->u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED)
    {
        /* Get the protection from the PTE, there's no real Proto PTE data */
        Protection = PointerPte->u.Soft.Protection;

        /* Remember that we did not use the proto protection */
        OriginalProtection = FALSE;
    }
    else
    {
        /* Get the protection from the original PTE link */
        OriginalPte = &Pfn1->OriginalPte;
        Protection = OriginalPte->u.Soft.Protection;

        /* Remember that we used the original protection */
        OriginalProtection = TRUE;

        /* Check if this was a write on a read only proto */
        if ((StoreInstruction) && !(Protection & MM_READWRITE))
        {
            /* Clear the flag */
            StoreInstruction = 0;
        }
    }

    /* Check if this was a write on a non-COW page */
    DirtyPage = FALSE;
    if ((StoreInstruction) && ((Protection & MM_WRITECOPY) != MM_WRITECOPY))
    {
        /* Then the page should be marked dirty */
        DirtyPage = TRUE;

        /* ReactOS check */
        ASSERT(Pfn1->OriginalPte.u.Soft.Prototype != 0);
    }

    /* Did we get a locked incoming PFN? */
    if (*LockedProtoPfn)
    {
        /* Drop a reference */
        ASSERT((*LockedProtoPfn)->u3.e2.ReferenceCount >= 1);
        MiDereferencePfnAndDropLockCount(*LockedProtoPfn);
        *LockedProtoPfn = NULL;
    }

    /* Release the PFN lock */
    MiReleasePfnLock(OldIrql);

    /* Remove special/caching bits */
    Protection &= ~MM_PROTECT_SPECIAL;

    /* Setup caching */
    if (Pfn1->u3.e1.CacheAttribute == MiWriteCombined)
    {
        /* Write combining, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_COMBINED(&TempPte);
    }
    else if (Pfn1->u3.e1.CacheAttribute == MiNonCached)
    {
        /* Write through, no caching */
        MI_PAGE_DISABLE_CACHE(&TempPte);
        MI_PAGE_WRITE_THROUGH(&TempPte);
    }

    /* Check if this is a kernel or user address */
    if (Address < MmSystemRangeStart)
    {
        /* Build the user PTE */
        MI_MAKE_HARDWARE_PTE_USER(&TempPte, PointerPte, Protection, PageFrameIndex);
    }
    else
    {
        /* Build the kernel PTE */
        MI_MAKE_HARDWARE_PTE(&TempPte, PointerPte, Protection, PageFrameIndex);
    }

    /* Set the dirty flag if needed */
    if (DirtyPage) MI_MAKE_DIRTY_PAGE(&TempPte);

    /* Write the PTE */
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

#if defined(_M_ARM64) && DBG
    /* ARM64 Debug: Log final PTE write */
    extern PVOID MiSystemViewStart;
    if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
        (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[arm64] MiCompleteProtoPteFault: Wrote FINAL PTE\n"
            "  Address=%p\n"
            "  PointerPte=%p (actual page table PTE)\n"
            "  TempPte.u.Long=0x%llx\n"
            "  PageFrameNumber=0x%llx (PA=0x%llx)\n"
            "  Protection=0x%lx\n",
            Address,
            PointerPte,
            (ULONG64)TempPte.u.Long,
            (ULONG64)PageFrameIndex, (ULONG64)(PageFrameIndex << PAGE_SHIFT),
            (ULONG)Protection);
    }
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 CRITICAL: TLB Invalidation After PTE Update
     *
     * PROBLEM: On ARM64, the TLB operates under a weakly-ordered memory model.
     * When we write a new PTE to memory, the TLB may still contain:
     * 1. A cached "translation fault" entry for this VA from the initial fault
     * 2. An invalid/stale translation from a previous mapping
     * 3. No entry at all, which is fine
     *
     * Without explicit TLB invalidation, the CPU will continue using the stale
     * TLB entry, causing the fault to repeat infinitely even though we just
     * installed a valid PTE.
     *
     * SOLUTION: Invalidate the TLB entry for this specific virtual address
     * immediately after writing the PTE. The sequence is:
     * 1. DSB ISHST: Ensure the PTE write is visible to other cores
     * 2. TLBI VAE1IS: Invalidate TLB entry for this VA (Inner Shareable)
     * 3. DSB ISH: Wait for TLB invalidation to complete on all cores
     * 4. ISB: Synchronize instruction fetch pipeline
     *
     * This applies to ALL addresses (user and kernel), but is especially
     * critical for kernel addresses like System View Space where faults
     * happen at elevated IRQL and we can't afford repeated faults.
     *
     * ARM64 Architecture Reference Manual (DDI 0487):
     * - D5.10.2: TLB maintenance operations
     * - D5.4.4: Ordering of TLB operations
     * - B2.3.8: Memory barriers and TLB maintenance
     */
    {
        ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
        __asm__ __volatile__(
            "dsb ishst\n\t"           /* Ensure PTE write is visible */
            "tlbi vae1is, %0\n\t"     /* Invalidate TLB for this VA */
            "dsb ish\n\t"             /* Wait for TLB invalidation */
            "isb"                      /* Synchronize instruction pipeline */
            : : "r"(VaForTlbi) : "memory");
    }
#endif

#if defined(_M_ARM64) && DBG
    /* Debug: Log when we complete prototype PTE fault for System View Space */
    extern PVOID MiSystemViewStart;
    if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
        (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[arm64] MiCompleteProtoPteFault: SUCCESS addr=%p PointerPte=%p->0x%llx PFN=0x%lx\n",
            Address, PointerPte, (ULONG64)TempPte.u.Long, PageFrameIndex);
    }
#endif

    /* Reset the protection if needed */
    if (OriginalProtection) Protection = MM_ZERO_ACCESS;

    /* Return success */
    ASSERT(PointerPte == MiAddressToPte(Address));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
MiResolvePageFileFault(_In_ BOOLEAN StoreInstruction,
                       _In_ PVOID FaultingAddress,
                       _In_ PMMPTE PointerPte,
                       _In_ PEPROCESS CurrentProcess,
                       _Inout_ KIRQL *OldIrql)
{
    ULONG Color;
    PFN_NUMBER Page;
    NTSTATUS Status;
    MMPTE TempPte = *PointerPte;
    PMMPFN Pfn1;
    ULONG PageFileIndex = TempPte.u.Soft.PageFileLow;
    ULONG_PTR PageFileOffset = TempPte.u.Soft.PageFileHigh;
    ULONG Protection = TempPte.u.Soft.Protection;

    /* Things we don't support yet */
    ASSERT(CurrentProcess > HYDRA_PROCESS);
    ASSERT(*OldIrql != MM_NOIRQL);

    MI_SET_USAGE(MI_USAGE_PAGE_FILE);
    MI_SET_PROCESS(CurrentProcess);

    /* We must hold the PFN lock */
    MI_ASSERT_PFN_LOCK_HELD();

    /* Some sanity checks */
    ASSERT(TempPte.u.Hard.Valid == 0);
    ASSERT(TempPte.u.Soft.PageFileHigh != 0);
    ASSERT(TempPte.u.Soft.PageFileHigh != MI_PTE_LOOKUP_NEEDED);

    /* Get any page, it will be overwritten */
    Color = MI_GET_NEXT_PROCESS_COLOR(CurrentProcess);
    Page = MiRemoveAnyPage(Color);
    if (Page == 0)
    {
        return STATUS_NO_MEMORY;
    }

    /* Initialize this PFN */
    MiInitializePfn(Page, PointerPte, StoreInstruction);

    /* Sets the PFN as being in IO operation */
    Pfn1 = MI_PFN_ELEMENT(Page);
    ASSERT(Pfn1->u1.Event == NULL);
    ASSERT(Pfn1->u3.e1.ReadInProgress == 0);
    ASSERT(Pfn1->u3.e1.WriteInProgress == 0);
    Pfn1->u3.e1.ReadInProgress = 1;

    /* We must write the PTE now as the PFN lock will be released while performing the IO operation */
    MI_MAKE_TRANSITION_PTE(&TempPte, Page, Protection);

    MI_WRITE_INVALID_PTE(PointerPte, TempPte);

    /* Release the PFN lock while we proceed */
    MiReleasePfnLock(*OldIrql);

    /* Do the paging IO */
    Status = MiReadPageFile(Page, PageFileIndex, PageFileOffset);

    /* Lock the PFN database again */
    *OldIrql = MiAcquirePfnLock();

    /* Nobody should have changed that while we were not looking */
    ASSERT(Pfn1->u3.e1.ReadInProgress == 1);
    ASSERT(Pfn1->u3.e1.WriteInProgress == 0);

    if (!NT_SUCCESS(Status))
    {
        /* Malheur! */
        ASSERT(FALSE);
        Pfn1->u4.InPageError = 1;
        Pfn1->u1.ReadStatus = Status;
    }

    /* And the PTE can finally be valid */
    MI_MAKE_HARDWARE_PTE(&TempPte, PointerPte, Protection, Page);
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

    Pfn1->u3.e1.ReadInProgress = 0;
    /* Did someone start to wait on us while we proceeded ? */
    if (Pfn1->u1.Event)
    {
        /* Tell them we're done */
        KeSetEvent(Pfn1->u1.Event, IO_NO_INCREMENT, FALSE);
    }

    return Status;
}

static
NTSTATUS
NTAPI
MiResolveTransitionFault(IN BOOLEAN StoreInstruction,
                         IN PVOID FaultingAddress,
                         IN PMMPTE PointerPte,
                         IN PEPROCESS CurrentProcess,
                         IN KIRQL OldIrql,
                         OUT PKEVENT **InPageBlock)
{
    PFN_NUMBER PageFrameIndex;
    PMMPFN Pfn1;
    MMPTE TempPte;
    PMMPTE PointerToPteForProtoPage;
    DPRINT("Transition fault on 0x%p with PTE 0x%p in process %s\n",
            FaultingAddress, PointerPte, CurrentProcess->ImageFileName);

    /* Windowss does this check */
    ASSERT(*InPageBlock == NULL);

    /* ARM3 doesn't support this path */
    ASSERT(OldIrql != MM_NOIRQL);

    /* Capture the PTE and make sure it's in transition format */
    TempPte = *PointerPte;
    ASSERT((TempPte.u.Soft.Valid == 0) &&
           (TempPte.u.Soft.Prototype == 0) &&
           (TempPte.u.Soft.Transition == 1));

    /* Get the PFN and the PFN entry */
    PageFrameIndex = TempPte.u.Trans.PageFrameNumber;
    DPRINT("Transition PFN: %lx\n", PageFrameIndex);
    Pfn1 = MiGetPfnEntry(PageFrameIndex);

    /* One more transition fault! */
    InterlockedIncrement(&KeGetCurrentPrcb()->MmTransitionCount);

    /* This is from ARM3 -- Windows normally handles this here */
    ASSERT(Pfn1->u4.InPageError == 0);

    /* See if we should wait before terminating the fault */
    if ((Pfn1->u3.e1.ReadInProgress == 1)
            || ((Pfn1->u3.e1.WriteInProgress == 1) && StoreInstruction))
    {
        DPRINT1("The page is currently in a page transition !\n");
        *InPageBlock = &Pfn1->u1.Event;
        if (PointerPte == Pfn1->PteAddress)
        {
            DPRINT1("And this if for this particular PTE.\n");
            /* The PTE will be made valid by the thread serving the fault */
            return STATUS_SUCCESS; // FIXME: Maybe something more descriptive
        }
    }

    /* Windows checks there's some free pages and this isn't an in-page error */
    ASSERT(MmAvailablePages > 0);
    ASSERT(Pfn1->u4.InPageError == 0);

    /* ReactOS checks for this */
    ASSERT(MmAvailablePages > 32);

    /* Was this a transition page in the valid list, or free/zero list? */
    if (Pfn1->u3.e1.PageLocation == ActiveAndValid)
    {
        /* All Windows does here is a bunch of sanity checks */
        DPRINT("Transition in active list\n");
        ASSERT((Pfn1->PteAddress >= MiAddressToPte(MmPagedPoolStart)) &&
               (Pfn1->PteAddress <= MiAddressToPte(MmPagedPoolEnd)));
        ASSERT(Pfn1->u2.ShareCount != 0);
        ASSERT(Pfn1->u3.e2.ReferenceCount != 0);
    }
    else
    {
        /* Otherwise, the page is removed from its list */
        DPRINT("Transition page in free/zero list\n");
        MiUnlinkPageFromList(Pfn1);
        MiReferenceUnusedPageAndBumpLockCount(Pfn1);
    }

    /* At this point, there should no longer be any in-page errors */
    ASSERT(Pfn1->u4.InPageError == 0);

    /* Check if this was a PFN with no more share references */
    if (Pfn1->u2.ShareCount == 0) MiDropLockCount(Pfn1);

    /* Bump the share count and make the page valid */
    Pfn1->u2.ShareCount++;
    Pfn1->u3.e1.PageLocation = ActiveAndValid;

    /* Prototype PTEs are in paged pool, which itself might be in transition */
    if (FaultingAddress >= MmSystemRangeStart)
    {
        /* Check if this is a paged pool PTE in transition state */
        PointerToPteForProtoPage = MiAddressToPte(PointerPte);
        TempPte = *PointerToPteForProtoPage;
        if ((TempPte.u.Hard.Valid == 0) && (TempPte.u.Soft.Transition == 1))
        {
            /* This isn't yet supported */
            DPRINT1("Double transition fault not yet supported\n");
            ASSERT(FALSE);
        }
    }

    /* Build the final PTE */
    ASSERT(PointerPte->u.Hard.Valid == 0);
    ASSERT(PointerPte->u.Trans.Prototype == 0);
    ASSERT(PointerPte->u.Trans.Transition == 1);
    TempPte.u.Long = (PointerPte->u.Long & ~0xFFF) |
                     (MmProtectToPteMask[PointerPte->u.Trans.Protection]) |
                     MiDetermineUserGlobalPteMask(PointerPte);

    /* Is the PTE writeable? */
    if ((Pfn1->u3.e1.Modified) &&
        MI_IS_PAGE_WRITEABLE(&TempPte) &&
        !MI_IS_PAGE_COPY_ON_WRITE(&TempPte))
    {
        /* Make it dirty */
        MI_MAKE_DIRTY_PAGE(&TempPte);
    }
    else
    {
        /* Make it clean */
        MI_MAKE_CLEAN_PAGE(&TempPte);
    }

    /* Write the valid PTE */
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

    /* Return success */
    return STATUS_PAGE_FAULT_TRANSITION;
}

static
NTSTATUS
NTAPI
MiResolveProtoPteFault(IN BOOLEAN StoreInstruction,
                       IN PVOID Address,
                       IN PMMPTE PointerPte,
                       IN PMMPTE PointerProtoPte,
                       IN OUT PMMPFN *OutPfn,
                       OUT PVOID *PageFileData,
                       OUT PMMPTE PteValue,
                       IN PEPROCESS Process,
                       IN KIRQL OldIrql,
                       IN PVOID TrapInformation)
{
    MMPTE TempPte, PteContents;
    PMMPFN Pfn1;
    PFN_NUMBER PageFrameIndex;
    NTSTATUS Status;
    PKEVENT* InPageBlock = NULL;
    ULONG Protection;

    /* Must be called with an invalid, prototype PTE, with the PFN lock held */
    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT(PointerPte->u.Hard.Valid == 0);
    ASSERT(PointerPte->u.Soft.Prototype == 1);

    /* Read the prototype PTE and check if it's valid */
    TempPte = *PointerProtoPte;

    if (TempPte.u.Hard.Valid == 1)
    {
        /* One more user of this mapped page */
        PageFrameIndex = PFN_FROM_PTE(&TempPte);

        Pfn1 = MiGetPfnEntry(PageFrameIndex);
        Pfn1->u2.ShareCount++;

        /* Call it a transition */
        InterlockedIncrement(&KeGetCurrentPrcb()->MmTransitionCount);

        /* Complete the prototype PTE fault -- this will release the PFN lock */
        return MiCompleteProtoPteFault(StoreInstruction,
                                       Address,
                                       PointerPte,
                                       PointerProtoPte,
                                       OldIrql,
                                       OutPfn);
    }

    /* Make sure there's some protection mask */
    if (TempPte.u.Long == 0)
    {
        /* Release the lock */
        DPRINT1("Access on reserved section?\n");
        DPRINT1("[pagfault] MiResolveProtoPteFault: Zero prototype PTE at %p for VA %p\n",
                PointerProtoPte, Address);
        MiReleasePfnLock(OldIrql);
        return STATUS_ACCESS_VIOLATION;
    }

    /* There is no such thing as a decommitted prototype PTE */
    if (TempPte.u.Long == MmDecommittedPte.u.Long)
    {
        DPRINT1("[pagfault] ERROR: Decommitted prototype PTE!\n");
        DPRINT1("[pagfault]   VA: %p\n", Address);
        DPRINT1("[pagfault]   PointerPte: %p (contains 0x%I64x)\n", PointerPte, PointerPte->u.Long);
        DPRINT1("[pagfault]   PointerProtoPte: %p (contains 0x%I64x)\n", PointerProtoPte, TempPte.u.Long);
        DPRINT1("[pagfault]   MmDecommittedPte: 0x%I64x\n", MmDecommittedPte.u.Long);

        /* Check if this is System View Space */
        extern PVOID MiSystemViewStart;
        extern SIZE_T MmSystemViewSize;
        if (MiSystemViewStart != NULL &&
            (ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < ((ULONG_PTR)MiSystemViewStart + MmSystemViewSize))
        {
            DPRINT1("[pagfault] *** This is in System View Space! (Start=%p Size=0x%lx) ***\n",
                    MiSystemViewStart, (ULONG)MmSystemViewSize);
        }
    }
    ASSERT(TempPte.u.Long != MmDecommittedPte.u.Long);

    /* Check for access rights on the PTE proper */
    PteContents = *PointerPte;
    if (PteContents.u.Soft.PageFileHigh != MI_PTE_LOOKUP_NEEDED)
    {
        if (!PteContents.u.Proto.ReadOnly)
        {
            Protection = TempPte.u.Soft.Protection;
        }
        else
        {
            Protection = MM_READONLY;
        }
        /* Check for page acess in software */
        Status = MiAccessCheck(PointerProtoPte,
                               StoreInstruction,
                               KernelMode,
                               TempPte.u.Soft.Protection,
                               TrapInformation,
                               TRUE);
        ASSERT(Status == STATUS_SUCCESS);
    }
    else
    {
        Protection = PteContents.u.Soft.Protection;
    }

    /* Check for writing copy on write page */
    if (((Protection & MM_WRITECOPY) == MM_WRITECOPY) && StoreInstruction)
    {
        PFN_NUMBER PageFrameIndex, ProtoPageFrameIndex;
        ULONG Color;

        /* Resolve the proto fault as if it was a read operation */
        Status = MiResolveProtoPteFault(FALSE,
                                        Address,
                                        PointerPte,
                                        PointerProtoPte,
                                        OutPfn,
                                        PageFileData,
                                        PteValue,
                                        Process,
                                        OldIrql,
                                        TrapInformation);

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        /* Lock again the PFN lock, MiResolveProtoPteFault unlocked it */
        OldIrql = MiAcquirePfnLock();

        /* And re-read the proto PTE */
        TempPte = *PointerProtoPte;
        ASSERT(TempPte.u.Hard.Valid == 1);
        ProtoPageFrameIndex = PFN_FROM_PTE(&TempPte);

        MI_SET_USAGE(MI_USAGE_COW);
        MI_SET_PROCESS(Process);

        /* Get a new page for the private copy */
        if (Process > HYDRA_PROCESS)
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
        else
            Color = MI_GET_NEXT_COLOR();

        PageFrameIndex = MiRemoveAnyPage(Color);
        if (PageFrameIndex == 0)
        {
            MiReleasePfnLock(OldIrql);
            return STATUS_NO_MEMORY;
        }

        /* Perform the copy */
        MiCopyPfn(PageFrameIndex, ProtoPageFrameIndex);

        /* This will drop everything MiResolveProtoPteFault referenced */
        MiDeletePte(PointerPte, Address, Process, PointerProtoPte);

        /* Because now we use this */
        Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);
        MiInitializePfn(PageFrameIndex, PointerPte, TRUE);

        /* Fix the protection */
        Protection &= ~MM_WRITECOPY;
        Protection |= MM_READWRITE;
        if (Address < MmSystemRangeStart)
        {
            /* Build the user PTE */
            MI_MAKE_HARDWARE_PTE_USER(&PteContents, PointerPte, Protection, PageFrameIndex);
        }
        else
        {
            /* Build the kernel PTE */
            MI_MAKE_HARDWARE_PTE(&PteContents, PointerPte, Protection, PageFrameIndex);
        }

        /* And finally, write the valid PTE */
        MI_WRITE_VALID_PTE(PointerPte, PteContents);

        /* The caller expects us to release the PFN lock */
        MiReleasePfnLock(OldIrql);
        return Status;
    }

    /* Check for clone PTEs */
    if (PointerPte <= MiHighestUserPte) ASSERT(Process->CloneRoot == NULL);

    /* We don't support mapped files yet */
    ASSERT(TempPte.u.Soft.Prototype == 0);

    /* We might however have transition PTEs */
    if (TempPte.u.Soft.Transition == 1)
    {
        /* Resolve the transition fault */
        ASSERT(OldIrql != MM_NOIRQL);
        Status = MiResolveTransitionFault(StoreInstruction,
                                          Address,
                                          PointerProtoPte,
                                          Process,
                                          OldIrql,
                                          &InPageBlock);
        ASSERT(NT_SUCCESS(Status));
    }
    else
    {
        /* We also don't support paged out pages */
        ASSERT(TempPte.u.Soft.PageFileHigh == 0);

#if defined(_M_ARM64) && DBG
        /* Debug: Log demand-zero prototype PTE resolution for System View Space */
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MiResolveProtoPteFault: Demand-zero for addr=%p ProtoPte=%p (0x%llx) Protection=%lx\n",
                Address, PointerProtoPte, (ULONG64)TempPte.u.Long, (ULONG)TempPte.u.Soft.Protection);
        }
#endif

        /* Resolve the demand zero fault */
        Status = MiResolveDemandZeroFault(Address,
                                          PointerProtoPte,
                                          (ULONG)TempPte.u.Soft.Protection,
                                          Process,
                                          OldIrql);

#if MI_TRACE_PFNS
        /* Update debug info */
        if (TrapInformation)
            MiGetPfnEntry(PointerProtoPte->u.Hard.PageFrameNumber)->CallSite = (PVOID)((PKTRAP_FRAME)TrapInformation)->Eip;
        else
            MiGetPfnEntry(PointerProtoPte->u.Hard.PageFrameNumber)->CallSite = _ReturnAddress();
#endif

        ASSERT(NT_SUCCESS(Status));
    }

    /* Complete the prototype PTE fault -- this will release the PFN lock */
    ASSERT(PointerPte->u.Hard.Valid == 0);
    return MiCompleteProtoPteFault(StoreInstruction,
                                   Address,
                                   PointerPte,
                                   PointerProtoPte,
                                   OldIrql,
                                   OutPfn);
}

NTSTATUS
NTAPI
MiDispatchFault(IN ULONG FaultCode,
                IN PVOID Address,
                IN PMMPTE PointerPte,
                IN PMMPTE PointerProtoPte,
                IN BOOLEAN Recursive,
                IN PEPROCESS Process,
                IN PVOID TrapInformation,
                IN PMMVAD Vad)
{
    MMPTE TempPte;
    KIRQL OldIrql, LockIrql;
    NTSTATUS Status;
    PMMPTE SuperProtoPte;
    PMMPFN Pfn1, OutPfn = NULL;
    PFN_NUMBER PageFrameIndex;
    PFN_COUNT PteCount, ProcessedPtes;
    DPRINT("ARM3 Page Fault Dispatcher for address: %p in process: %p\n",
             Address,
             Process);

    /* Make sure the addresses are ok */
    ASSERT(PointerPte == MiAddressToPte(Address));

    //
    // Make sure APCs are off and we're not at dispatch
    //
    OldIrql = KeGetCurrentIrql();
    ASSERT(OldIrql <= APC_LEVEL);
    ASSERT(KeAreAllApcsDisabled() == TRUE);

    //
    // Grab a copy of the PTE
    //
    TempPte = *PointerPte;

    /* Do we have a prototype PTE? */
    if (PointerProtoPte)
    {
#if defined(_M_ARM64) && DBG
        /* Debug: Log when we enter prototype PTE handling for System View Space */
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MiDispatchFault: Handling ProtoPTE for addr=%p ProtoPte=%p\n",
                Address, PointerProtoPte);
        }
#endif

        /* This should never happen */
        ASSERT(!MI_IS_PHYSICAL_ADDRESS(PointerProtoPte));

        /* Check if this is a kernel-mode address */
        SuperProtoPte = MiAddressToPte(PointerProtoPte);

        if (Address >= MmSystemRangeStart)
        {
            /* Lock the PFN database */
            LockIrql = MiAcquirePfnLock();

            /* Has the PTE been made valid yet? */
            if (!SuperProtoPte->u.Hard.Valid)
            {
#if defined(_M_ARM64) || defined(__aarch64__)
                /*
                 * ARM64: The SuperProtoPte (PTE mapping the prototype PTE page) is invalid.
                 * This occurs when prototype PTEs in paged pool haven't been mapped yet.
                 *
                 * On ARM64, System View Space prototype PTEs are allocated in paged pool
                 * (e.g., 0xFFFFF8A000007B40), and the page containing these prototype PTEs
                 * might not be mapped initially. Unlike x86/AMD64 where paged pool pages
                 * are often pre-faulted, ARM64's memory initialization can leave these
                 * unmapped until first access.
                 *
                 * Solution: Release the PFN lock and recursively fault to bring in the
                 * SuperProtoPte page, then retry the original prototype PTE resolution.
                 *
                 * Windows NT behavior: According to Windows Internals and NT kernel sources,
                 * MmAccessFault can be called recursively to resolve nested faults. The
                 * MiDispatchFault Recursive parameter exists for this scenario, though
                 * the actual recursive call happens at the MmAccessFault level.
                 */
                PVOID ProtoPteAddress = PointerProtoPte;

                /* Release PFN lock before recursive fault - required for memory manager invariants */
                MiReleasePfnLock(LockIrql);

                DPRINT("ARM64: SuperProtoPte %p for ProtoPte %p is invalid, recursively faulting\n",
                       SuperProtoPte, PointerProtoPte);

                /*
                 * Recursively fault on the prototype PTE address itself to bring in
                 * the paged pool page containing the prototype PTE. This is a read
                 * access (not a write), so use fault code 0x0 for not-present read.
                 */
                Status = MmArmAccessFault(0, /* Not present, read access */
                                         ProtoPteAddress,
                                         KernelMode,
                                         TrapInformation);

                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("ARM64: Failed to fault in SuperProtoPte %p for ProtoPte %p, status=0x%lx\n",
                           SuperProtoPte, PointerProtoPte, Status);
                    return Status;
                }

                /*
                 * The recursive fault succeeded. Now re-acquire the PFN lock and
                 * verify the SuperProtoPte is now valid before proceeding.
                 */
                LockIrql = MiAcquirePfnLock();

                if (!SuperProtoPte->u.Hard.Valid)
                {
                    /*
                     * This should not happen - we just faulted it in. If we get here,
                     * it indicates a race condition or memory corruption.
                     */
                    MiReleasePfnLock(LockIrql);
                    DPRINT1("ARM64: SuperProtoPte %p still invalid after recursive fault\n", SuperProtoPte);
                    return STATUS_IN_PAGE_ERROR;
                }

                DPRINT("ARM64: SuperProtoPte %p now valid (PFN=0x%lx), continuing with ProtoPte resolution\n",
                       SuperProtoPte, SuperProtoPte->u.Hard.PageFrameNumber);
#else
                /*
                 * On x86/AMD64, the SuperProtoPte should always be valid by the time
                 * we reach this code path, as paged pool initialization pre-faults
                 * the necessary pages. If we hit this, it's a bug.
                 */
                ASSERT(FALSE);
#endif
            }
            else if (PointerPte->u.Hard.Valid == 1)
            {
                ASSERT(FALSE);
            }

            /* Resolve the fault -- this will release the PFN lock */
            Status = MiResolveProtoPteFault(!MI_IS_NOT_PRESENT_FAULT(FaultCode),
                                            Address,
                                            PointerPte,
                                            PointerProtoPte,
                                            &OutPfn,
                                            NULL,
                                            NULL,
                                            Process,
                                            LockIrql,
                                            TrapInformation);
            ASSERT(Status == STATUS_SUCCESS);

            /* Complete this as a transition fault */
            ASSERT(OldIrql == KeGetCurrentIrql());
            ASSERT(OldIrql <= APC_LEVEL);
            ASSERT(KeAreAllApcsDisabled() == TRUE);
            return Status;
        }
        else
        {
            /* We only handle the lookup path */
            ASSERT(PointerPte->u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED);

            /* Is there a non-image VAD? */
            if ((Vad) &&
                (Vad->u.VadFlags.VadType != VadImageMap) &&
                !(Vad->u2.VadFlags2.ExtendableFile))
            {
                /* One day, ReactOS will cluster faults */
                ASSERT(Address <= MM_HIGHEST_USER_ADDRESS);
                DPRINT("Should cluster fault, but won't\n");
            }

            /* Only one PTE to handle for now */
            PteCount = 1;
            ProcessedPtes = 0;

            /* Lock the PFN database */
            LockIrql = MiAcquirePfnLock();

            /* We only handle the valid path */
            ASSERT(SuperProtoPte->u.Hard.Valid == 1);

            /* Capture the PTE */
            TempPte = *PointerProtoPte;

            /* Loop to handle future case of clustered faults */
            while (TRUE)
            {
                /* For our current usage, this should be true */
                if (TempPte.u.Hard.Valid == 1)
                {
                    /* Bump the share count on the PTE */
                    PageFrameIndex = PFN_FROM_PTE(&TempPte);
                    Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);
                    Pfn1->u2.ShareCount++;
                }
                else if ((TempPte.u.Soft.Prototype == 0) &&
                         (TempPte.u.Soft.Transition == 1))
                {
                    /* This is a standby page, bring it back from the cache */
                    PageFrameIndex = TempPte.u.Trans.PageFrameNumber;
                    DPRINT("oooh, shiny, a soft fault! 0x%lx\n", PageFrameIndex);
                    Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);
                    ASSERT(Pfn1->u3.e1.PageLocation != ActiveAndValid);

                    /* Should not yet happen in ReactOS */
                    ASSERT(Pfn1->u3.e1.ReadInProgress == 0);
                    ASSERT(Pfn1->u4.InPageError == 0);

                    /* Get the page */
                    MiUnlinkPageFromList(Pfn1);

                    /* Bump its reference count */
                    ASSERT(Pfn1->u2.ShareCount == 0);
                    InterlockedIncrement16((PSHORT)&Pfn1->u3.e2.ReferenceCount);
                    Pfn1->u2.ShareCount++;

                    /* Make it valid again */
                    /* This looks like another macro.... */
                    Pfn1->u3.e1.PageLocation = ActiveAndValid;
                    ASSERT(PointerProtoPte->u.Hard.Valid == 0);
                    ASSERT(PointerProtoPte->u.Trans.Prototype == 0);
                    ASSERT(PointerProtoPte->u.Trans.Transition == 1);
                    TempPte.u.Long = (PointerProtoPte->u.Long & ~0xFFF) |
                                     MmProtectToPteMask[PointerProtoPte->u.Trans.Protection];
                    TempPte.u.Hard.Valid = 1;
                    MI_MAKE_ACCESSED_PAGE(&TempPte);

                    /* Is the PTE writeable? */
                    if ((Pfn1->u3.e1.Modified) &&
                        MI_IS_PAGE_WRITEABLE(&TempPte) &&
                        !MI_IS_PAGE_COPY_ON_WRITE(&TempPte))
                    {
                        /* Make it dirty */
                        MI_MAKE_DIRTY_PAGE(&TempPte);
                    }
                    else
                    {
                        /* Make it clean */
                        MI_MAKE_CLEAN_PAGE(&TempPte);
                    }

                    /* Write the valid PTE */
                    MI_WRITE_VALID_PTE(PointerProtoPte, TempPte);
                    ASSERT(PointerPte->u.Hard.Valid == 0);
                }
                else
                {
                    /* Page is invalid, get out of the loop */
                    break;
                }

                /* One more done, was it the last? */
                if (++ProcessedPtes == PteCount)
                {
                    /* Complete the fault */
                    MiCompleteProtoPteFault(!MI_IS_NOT_PRESENT_FAULT(FaultCode),
                                            Address,
                                            PointerPte,
                                            PointerProtoPte,
                                            LockIrql,
                                            &OutPfn);

                    /* THIS RELEASES THE PFN LOCK! */
                    break;
                }

                /* No clustered faults yet */
                ASSERT(FALSE);
            }

            /* Did we resolve the fault? */
            if (ProcessedPtes)
            {
                /* Bump the transition count */
                InterlockedExchangeAddSizeT(&KeGetCurrentPrcb()->MmTransitionCount, ProcessedPtes);
                ProcessedPtes--;

                /* Loop all the processing we did */
                ASSERT(ProcessedPtes == 0);

                /* Complete this as a transition fault */
                ASSERT(OldIrql == KeGetCurrentIrql());
                ASSERT(OldIrql <= APC_LEVEL);
                ASSERT(KeAreAllApcsDisabled() == TRUE);
                return STATUS_PAGE_FAULT_TRANSITION;
            }

            /* We did not -- PFN lock is still held, prepare to resolve prototype PTE fault */
            OutPfn = MI_PFN_ELEMENT(SuperProtoPte->u.Hard.PageFrameNumber);
            MiReferenceUsedPageAndBumpLockCount(OutPfn);
            ASSERT(OutPfn->u3.e2.ReferenceCount > 1);
            ASSERT(PointerPte->u.Hard.Valid == 0);

            /* Resolve the fault -- this will release the PFN lock */
            Status = MiResolveProtoPteFault(!MI_IS_NOT_PRESENT_FAULT(FaultCode),
                                            Address,
                                            PointerPte,
                                            PointerProtoPte,
                                            &OutPfn,
                                            NULL,
                                            NULL,
                                            Process,
                                            LockIrql,
                                            TrapInformation);
            //ASSERT(Status != STATUS_ISSUE_PAGING_IO);
            //ASSERT(Status != STATUS_REFAULT);
            //ASSERT(Status != STATUS_PTE_CHANGED);

            /* Did the routine clean out the PFN or should we? */
            if (OutPfn)
            {
                /* We had a locked PFN, so acquire the PFN lock to dereference it */
                ASSERT(PointerProtoPte != NULL);
                OldIrql = MiAcquirePfnLock();

                /* Dereference the locked PFN */
                MiDereferencePfnAndDropLockCount(OutPfn);
                ASSERT(OutPfn->u3.e2.ReferenceCount >= 1);

                /* And now release the lock */
                MiReleasePfnLock(OldIrql);
            }

            /* Complete this as a transition fault */
            ASSERT(OldIrql == KeGetCurrentIrql());
            ASSERT(OldIrql <= APC_LEVEL);
            ASSERT(KeAreAllApcsDisabled() == TRUE);
            return Status;
        }
    }

    /* Is this a transition PTE */
    if (TempPte.u.Soft.Transition)
    {
        PKEVENT* InPageBlock = NULL;
        PKEVENT PreviousPageEvent = NULL;
        KEVENT CurrentPageEvent;

        /* Lock the PFN database */
        LockIrql = MiAcquirePfnLock();

        /* Resolve */
        Status = MiResolveTransitionFault(!MI_IS_NOT_PRESENT_FAULT(FaultCode), Address, PointerPte, Process, LockIrql, &InPageBlock);

        ASSERT(NT_SUCCESS(Status));

        if (InPageBlock != NULL)
        {
            /* Another thread is reading or writing this page. Put us into the waiting queue. */
            KeInitializeEvent(&CurrentPageEvent, NotificationEvent, FALSE);
            PreviousPageEvent = *InPageBlock;
            *InPageBlock = &CurrentPageEvent;
        }

        /* And now release the lock and leave*/
        MiReleasePfnLock(LockIrql);

        if (InPageBlock != NULL)
        {
            KeWaitForSingleObject(&CurrentPageEvent, WrPageIn, KernelMode, FALSE, NULL);

            /* Let's the chain go on */
            if (PreviousPageEvent)
            {
                KeSetEvent(PreviousPageEvent, IO_NO_INCREMENT, FALSE);
            }
        }

        ASSERT(OldIrql == KeGetCurrentIrql());
        ASSERT(OldIrql <= APC_LEVEL);
        ASSERT(KeAreAllApcsDisabled() == TRUE);
        return Status;
    }

    /* Should we page the data back in ? */
    if (TempPte.u.Soft.PageFileHigh != 0)
    {
        /* Lock the PFN database */
        LockIrql = MiAcquirePfnLock();

        /* Resolve */
        Status = MiResolvePageFileFault(!MI_IS_NOT_PRESENT_FAULT(FaultCode), Address, PointerPte, Process, &LockIrql);

        /* And now release the lock and leave*/
        MiReleasePfnLock(LockIrql);

        ASSERT(OldIrql == KeGetCurrentIrql());
        ASSERT(OldIrql <= APC_LEVEL);
        ASSERT(KeAreAllApcsDisabled() == TRUE);
        return Status;
    }

    //
    // The PTE must be invalid but not completely empty. It must also not be a
    // prototype a transition or a paged-out PTE as those scenarii should've been handled above.
    // These are all Windows checks
    //
    ASSERT(TempPte.u.Hard.Valid == 0);
    ASSERT(TempPte.u.Soft.Prototype == 0);
    ASSERT(TempPte.u.Soft.Transition == 0);
    ASSERT(TempPte.u.Soft.PageFileHigh == 0);
    ASSERT(TempPte.u.Long != 0);

    //
    // If we got this far, the PTE can only be a demand zero PTE, which is what
    // we want. Go handle it!
    //
    Status = MiResolveDemandZeroFault(Address,
                                      PointerPte,
                                      (ULONG)TempPte.u.Soft.Protection,
                                      Process,
                                      MM_NOIRQL);
    ASSERT(KeAreAllApcsDisabled() == TRUE);
    if (NT_SUCCESS(Status))
    {
#if MI_TRACE_PFNS
        /* Update debug info */
        if (TrapInformation)
            MiGetPfnEntry(PointerPte->u.Hard.PageFrameNumber)->CallSite = (PVOID)((PKTRAP_FRAME)TrapInformation)->Eip;
        else
            MiGetPfnEntry(PointerPte->u.Hard.PageFrameNumber)->CallSite = _ReturnAddress();
#endif

        //
        // Make sure we're returning in a sane state and pass the status down
        //
        ASSERT(OldIrql == KeGetCurrentIrql());
        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
        return Status;
    }

    //
    // Return status
    //
    return Status;
}

NTSTATUS
NTAPI
MmArmAccessFault(IN ULONG FaultCode,
                 IN PVOID Address,
                 IN KPROCESSOR_MODE Mode,
                 IN PVOID TrapInformation)
{
    KIRQL OldIrql = KeGetCurrentIrql(), LockIrql;
    PMMPTE ProtoPte = NULL;
    PMMPTE PointerPte = MiAddressToPte(Address);
    PMMPDE PointerPde = MiAddressToPde(Address);
#if (_MI_PAGING_LEVELS >= 3)
    PMMPDE PointerPpe = MiAddressToPpe(Address);
#if (_MI_PAGING_LEVELS == 4)
    PMMPDE PointerPxe = MiAddressToPxe(Address);
#endif
#endif
    MMPTE TempPte;
    PETHREAD CurrentThread;
    PEPROCESS CurrentProcess;
    NTSTATUS Status;
    PMMSUPPORT WorkingSet;
    ULONG ProtectionCode;
    PMMVAD Vad = NULL;
    PFN_NUMBER PageFrameIndex;
    ULONG Color;
    BOOLEAN IsSessionAddress;
    PMMPFN Pfn1;

#if defined(_M_ARM64) || defined(__aarch64__)
    /* Mark entry before any other code */
    InterlockedExchange(&MmArmAccessFaultInFunction, 1);
    InterlockedIncrement(&MmArmAccessFaultEntryCount);
    MmArmAccessFaultLastAddress = Address;

    /* Debug System View Space faults */
    extern PVOID MiSystemViewStart;
    if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
        (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[arm64] MmArmAccessFault: ENTERED for System View addr=%p FaultCode=0x%lx\n",
            Address, FaultCode);
    }
#endif

    DPRINT("ARM3 FAULT AT: %p\n", Address);

#if defined(_M_ARM64) || defined(__aarch64__)
    /* Reject non-canonical addresses early to avoid bogus user-range walks. */
    {
        LONG64 CanonicalHigh = (LONG64)(LONG_PTR)Address >> 48;
        if ((CanonicalHigh != 0) && (CanonicalHigh != -1))
        {
            if (Mode == UserMode)
            {
                return STATUS_ACCESS_VIOLATION;
            }
            KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                         (ULONG_PTR)Address,
                         FaultCode,
                         (ULONG_PTR)TrapInformation,
                         0xA64BAD2);
        }
    }
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 self-map alias page mapping.
     *
     * On ARM64, the page table entries (PointerPxe, PointerPpe, PointerPde, PointerPte)
     * are accessed via a self-mapping region (starting at PTE_BASE = 0xFFFFF68000000000).
     * Unlike x86-64, ARM64 does not have a recursive page table structure that
     * automatically maps all page table pages into the self-map region.
     *
     * We must ensure the alias pages backing these pointers are mapped BEFORE
     * we try to dereference them. Otherwise, accessing PointerPte (which is in
     * the self-map region) will cause a nested page fault.
     *
     * This must be done for ALL kernel addresses, not just page table addresses,
     * because any kernel page fault requires us to read the page table entries.
     */
    if (Address >= MmSystemRangeStart)
    {
        extern VOID MiArm64MapAliasForPointer(_In_ PVOID AliasVa);
        extern PVOID MmHyperSpaceEnd;

        /*
         * CRITICAL FIX FOR CYCLE 15: Handle faults on PTE addresses themselves.
         * When the faulting address is ALREADY in the self-map region (PTE_BASE to
         * MmHyperSpaceEnd), we need to create the alias mapping for the address FIRST,
         * BEFORE trying to calculate its page table pointers (which would cause
         * recursive self-map address calculations and nested faults).
         */
        if (((PVOID)Address >= (PVOID)PTE_BASE) && ((PVOID)Address <= MmHyperSpaceEnd))
        {
            /* The faulting address is a PTE/PDE/PPE/PXE or Hyperspace address.
             * Create its alias mapping FIRST before accessing its page table entries.
             * This prevents infinite recursion where MiAddressToPte(PTE_addr) tries
             * to access another unmapped PTE address. */
            MiArm64MapAliasForPointer(Address);

            /* TLB invalidation for the alias page we just created */
            {
                ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
                __asm__ __volatile__(
                    "dsb ishst\n\t"           /* Ensure stores are visible */
                    "tlbi vae1is, %0\n\t"     /* Invalidate TLB entry */
                    "dsb ish\n\t"             /* Wait for TLB invalidation */
                    "isb\n\t"                 /* Synchronize context */
                    : : "r"(VaForTlbi) : "memory"
                );
            }

            /* Now that Address itself is accessible, we can return SUCCESS.
             * The fault will retry and the access will succeed. */
            DPRINT("ARM64: Created self-map alias for PTE/Hyperspace address %p\n", Address);
            InterlockedExchange(&MmArmAccessFaultInFunction, 0);
            return STATUS_SUCCESS;
        }

        /* For normal kernel addresses (not in self-map region), ensure the
         * page table pointers themselves (PointerPxe/Ppe/Pde/Pte) are accessible */
#if (_MI_PAGING_LEVELS == 4)
        MiArm64MapAliasForPointer(PointerPxe);
#endif
#if (_MI_PAGING_LEVELS >= 3)
        MiArm64MapAliasForPointer(PointerPpe);
#endif
        MiArm64MapAliasForPointer(PointerPde);
        MiArm64MapAliasForPointer(PointerPte);

        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
    }

    /*
     * ARM64 software Access Flag handling (TCR.HA=0):
     * Some CPUs (cpu=host) do not support hardware AF updates, so valid entries
     * with AF=0 raise an Access Flag fault. Detect that case via ESR and set AF.
     */
    if (TrapInformation != NULL)
    {
        PKTRAP_FRAME TrapFrame = (PKTRAP_FRAME)TrapInformation;
        ULONG FaultStatus = TrapFrame->Esr & 0x3FULL;

        /* Access flag fault at any level: 0x08-0x0B */
        if ((FaultStatus & 0x3C) == 0x08)
        {
            ULONG FaultLevel = FaultStatus & 0x3;
            PMMPTE AccessPte = PointerPte;

#if (_MI_PAGING_LEVELS >= 3)
            if (FaultLevel == 2)
            {
                AccessPte = (PMMPTE)PointerPde;
            }
            else if (FaultLevel == 1)
            {
                AccessPte = (PMMPTE)PointerPpe;
            }
#if (_MI_PAGING_LEVELS == 4)
            else if (FaultLevel == 0)
            {
                AccessPte = (PMMPTE)PointerPxe;
            }
#endif
#endif

            if (AccessPte != NULL)
            {
                MMPTE AccessedPte = *AccessPte;
                if (((AccessedPte.u.Long & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_INVALID) &&
                    ((AccessedPte.u.Long & PTE_ACCESSED) == 0))
                {
                    AccessedPte.u.Long |= PTE_ACCESSED;
                    MI_UPDATE_VALID_PTE(AccessPte, AccessedPte);
                    return STATUS_SUCCESS;
                }
            }
        }
    }
#endif

    /* Check for page fault on high IRQL */
    if (OldIrql > APC_LEVEL)
    {
#if (_MI_PAGING_LEVELS < 3)
        /* Could be a page table for paged pool, which we'll allow */
        if (MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address)) MiSynchronizeSystemPde((PMMPDE)PointerPte);
        MiCheckPdeForPagedPool(Address);
#endif
        /* Check if any of the top-level pages are invalid */
        if (
#if (_MI_PAGING_LEVELS == 4)
            (PointerPxe->u.Hard.Valid == 0) ||
#endif
#if (_MI_PAGING_LEVELS >= 3)
            (PointerPpe->u.Hard.Valid == 0) ||
#endif
            (PointerPde->u.Hard.Valid == 0) ||
            (PointerPte->u.Hard.Valid == 0))
        {
            /* This fault is not valid, print out some debugging help */
            DbgPrint("MM:***PAGE FAULT AT IRQL > 1  Va %p, IRQL %lx\n",
                     Address,
                     OldIrql);
            if (TrapInformation)
            {
#if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM) || defined(_M_ARM64) || defined(__aarch64__)
#ifdef _M_IX86
                DbgPrint("MM:***EIP %p, EFL %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->Eip,
                         ((PKTRAP_FRAME)TrapInformation)->EFlags);
                DbgPrint("MM:***EAX %p, ECX %p EDX %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->Eax,
                         ((PKTRAP_FRAME)TrapInformation)->Ecx,
                         ((PKTRAP_FRAME)TrapInformation)->Edx);
                DbgPrint("MM:***EBX %p, ESI %p EDI %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->Ebx,
                         ((PKTRAP_FRAME)TrapInformation)->Esi,
                         ((PKTRAP_FRAME)TrapInformation)->Edi);
#elif defined(_M_AMD64)
                DbgPrint("MM:***RIP %p, EFL %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->Rip,
                         ((PKTRAP_FRAME)TrapInformation)->EFlags);
                DbgPrint("MM:***RAX %p, RCX %p RDX %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->Rax,
                         ((PKTRAP_FRAME)TrapInformation)->Rcx,
                         ((PKTRAP_FRAME)TrapInformation)->Rdx);
                DbgPrint("MM:***RBX %p, RSI %p RDI %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->Rbx,
                         ((PKTRAP_FRAME)TrapInformation)->Rsi,
                         ((PKTRAP_FRAME)TrapInformation)->Rdi);
#elif defined(_M_ARM)
                DbgPrint("MM:***PC %p\n", ((PKTRAP_FRAME)TrapInformation)->Pc);
                DbgPrint("MM:***R0 %p, R1 %p R2 %p, R3 %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->R0,
                         ((PKTRAP_FRAME)TrapInformation)->R1,
                         ((PKTRAP_FRAME)TrapInformation)->R2,
                         ((PKTRAP_FRAME)TrapInformation)->R3);
                DbgPrint("MM:***R11 %p, R12 %p SP %p, LR %p\n",
                         ((PKTRAP_FRAME)TrapInformation)->R11,
                         ((PKTRAP_FRAME)TrapInformation)->R12,
                         ((PKTRAP_FRAME)TrapInformation)->Sp,
                         ((PKTRAP_FRAME)TrapInformation)->Lr);
#elif defined(_M_ARM64) || defined(__aarch64__)
                DbgPrint("MM:***PC %p\n", (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->Pc);
                DbgPrint("MM:***X0 %p, X1 %p X2 %p, X3 %p\n",
                         (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->X0,
                         (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->X1,
                         (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->X2,
                         (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->X3);
                DbgPrint("MM:***FP %p, SP %p, LR %p\n",
                         (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->Fp,
                         (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->Sp,
                         (PVOID)(ULONG_PTR)((PKTRAP_FRAME)TrapInformation)->Lr);
#endif
#else
                UNREFERENCED_PARAMETER(TrapInformation);
#endif
            }

            /* Tell the trap handler to fail */
            return STATUS_IN_PAGE_ERROR | 0x10000000;
        }

        /* Not yet implemented in ReactOS */
        ASSERT(MI_IS_PAGE_LARGE(PointerPde) == FALSE);
        ASSERT((!MI_IS_NOT_PRESENT_FAULT(FaultCode) && MI_IS_PAGE_COPY_ON_WRITE(PointerPte)) == FALSE);

        /* Check if this was a write */
        if (MI_IS_WRITE_ACCESS(FaultCode))
        {
            /* Was it to a read-only page? */
            Pfn1 = MI_PFN_ELEMENT(PointerPte->u.Hard.PageFrameNumber);
            if (!(PointerPte->u.Long & PTE_READWRITE) &&
                !(Pfn1->OriginalPte.u.Soft.Protection & MM_READWRITE))
            {
                /* Crash with distinguished bugcheck code */
                KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                             (ULONG_PTR)Address,
                             PointerPte->u.Long,
                             (ULONG_PTR)TrapInformation,
                             10);
            }
        }

#if defined(_M_ARM64) || defined(__aarch64__)
        /*
         * ARM64 HIGH-IRQL SELF-MAP FIX:
         *
         * On ARM64, even when PointerPxe/Ppe/Pde/Pte dereferences succeed
         * (because intermediate page tables exist), the actual faulting address
         * in the self-map window (PTE_BASE..MmHyperSpaceEnd) may still need
         * its alias mapping created.
         *
         * This is critical at HIGH IRQL because:
         * 1. Code accesses PTE/PDE/PPE addresses to manipulate page tables
         * 2. The pointer dereferences (PointerPte->u.Hard.Valid) work because
         *    the intermediate L0/L1/L2 tables already exist
         * 3. BUT the final L3 entry for the target address may not be mapped yet
         * 4. Without this fix, we return SUCCESS without creating the mapping,
         *    causing an infinite fault loop
         *
         * Example: Address FFFFF6C000214000 (PTE for kernel VA):
         *   - PointerPte points into page table structures (already valid)
         *   - But FFFFF6C000214000 itself needs an L3 entry via self-map
         *   - MiArm64MapAliasForPointer creates this missing L3 entry
         */
        if (MI_IS_PAGE_TABLE_OR_HYPER_ADDRESS(Address))
        {
            extern VOID MiArm64MapAliasForPointer(_In_ PVOID AliasVa);

            /* Create the self-map alias page backing for this address */
            MiArm64MapAliasForPointer(Address);

            /*
             * Invalidate TLB for the faulting address. The mapping was just created
             * via KSEG0 direct manipulation, so we need to ensure the MMU sees it.
             * Use TLBI VAE1IS (invalidate by VA, Inner Shareable) for SMP safety.
             */
            {
                ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
                __asm__ __volatile__(
                    "dsb ishst\n\t"           /* Ensure stores are visible */
                    "tlbi vae1is, %0\n\t"     /* Invalidate TLB entry */
                    "dsb ish\n\t"             /* Wait for TLB invalidation */
                    "isb\n\t"                 /* Synchronize context */
                    : : "r"(VaForTlbi) : "memory"
                );
            }

            DPRINT("ARM64: Created self-map alias at high IRQL for %p\n", Address);
        }
#endif

        /* Nothing is actually wrong */
        DPRINT("Fault at IRQL %u is ok (%p)\n", OldIrql, Address);
        return STATUS_SUCCESS;
    }

    /* Check for kernel fault address */
    if (Address >= MmSystemRangeStart)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        /* Targeted debug for VACB address */
        if ((ULONG64)Address >= 0xFFFF8000BCC00000ULL && (ULONG64)Address < 0xFFFF8000BD000000ULL)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[MmArmAccessFault] VACB addr=%p PXE=%llx PPE=%llx PDE=%llx PTE=%llx\n",
                       Address, PointerPxe->u.Long, PointerPpe->u.Long, PointerPde->u.Long,
                       PointerPte ? PointerPte->u.Long : 0);
        }

        /* Debug System View Space */
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: System View kernel path - PXE.Valid=%d PPE.Valid=%d PDE.Valid=%d PointerPte=%p Mode=%d\n",
                PointerPxe->u.Hard.Valid, PointerPpe->u.Hard.Valid, PointerPde->u.Hard.Valid, PointerPte, Mode);
        }
#endif
        /* Bail out, if the fault came from user mode */
#if defined(_M_ARM64) || defined(__aarch64__)
        /*
         * ARM64 WORKAROUND: Skip user mode check for kernel addresses.
         *
         * BUG: KiArm64PreviousModeFromSpsr is returning incorrect Mode values
         * due to SPSR being corrupted/misread from the exception context.
         * As a workaround, we check if the faulting address is in kernel
         * space - if so, we know it CAN'T be a user-mode fault, regardless
         * of what Mode says.
         *
         * TODO: Fix the root cause in the ARM64 exception handler where
         * SPSR is being saved/restored.
         */
        if ((Mode == UserMode) && ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart))
#else
        if (Mode == UserMode)
#endif
        {
#if defined(_M_ARM64) && DBG
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmArmAccessFault: RETURNING STATUS_ACCESS_VIOLATION due to Mode==UserMode\n");
            }
#endif
            return STATUS_ACCESS_VIOLATION;
        }

#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: Past user mode check, Mode=%d\n", Mode);
        }
#endif

#if (_MI_PAGING_LEVELS == 2)
        if (MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address)) MiSynchronizeSystemPde((PMMPDE)PointerPte);
        MiCheckPdeForPagedPool(Address);
#endif

        /* Check if the higher page table entries are invalid */
        if (
#if (_MI_PAGING_LEVELS == 4)
            /* AMD64 system, check if PXE is invalid */
            (PointerPxe->u.Hard.Valid == 0) ||
#endif
#if (_MI_PAGING_LEVELS >= 3)
            /* PAE/AMD64 system, check if PPE is invalid */
            (PointerPpe->u.Hard.Valid == 0) ||
#endif
            /* Always check if the PDE is valid */
            (!PointerPde->u.Hard.Valid))
        {
#if defined(_M_ARM64) && DBG
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmArmAccessFault: About to bugcheck - PXE.Valid=%d PPE.Valid=%d PDE.Valid=%d\n",
                    PointerPxe->u.Hard.Valid, PointerPpe->u.Hard.Valid, PointerPde->u.Hard.Valid);
            }
#endif
            /* PXE/PPE/PDE (still) not valid, kill the system */
            KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                         (ULONG_PTR)Address,
                         FaultCode,
                         (ULONG_PTR)TrapInformation,
                         2);
        }

        /* Not handling session faults yet */
        IsSessionAddress = MI_IS_SESSION_ADDRESS(Address);

#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: IsSessionAddress=%d, about to read PTE at %p\n",
                IsSessionAddress, PointerPte);
        }
#endif

        /* The PDE is valid, so read the PTE */
#if defined(_M_ARM64) || defined(__aarch64__)
        /*
         * ARM64 CRITICAL FIX: PointerPte Self-Map Backing
         *
         * PROBLEM: When the faulting address is in System View Space or other
         * kernel ranges, PointerPte itself is calculated using MiAddressToPte(),
         * which returns an address in the self-map region (PTE_BASE range).
         * On ARM64, these self-map addresses require on-demand creation of
         * backing page tables.
         *
         * Example:
         *   Faulting address: FFFFF97FBA000000 (System View Space)
         *   PointerPte = MiAddressToPte(FFFFF97FBA000000) = FFFFF6FCBFDD0000
         *   PointerPte is in [PTE_BASE, PTE_TOP] self-map range
         *
         * If we try to dereference PointerPte without ensuring its backing
         * page exists, we'll get a nested page fault and crash.
         *
         * SOLUTION: Check if PointerPte is in the self-map region. If yes,
         * ensure the backing page exists by calling MiArm64MapAliasForPointer.
         */
#if DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            extern PVOID MmHyperSpaceEnd;
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: PointerPte=%p PTE_BASE=%p MmHyperSpaceEnd=%p\n",
                PointerPte, (PVOID)PTE_BASE, MmHyperSpaceEnd);
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: About to check MI_IS_PAGE_TABLE_OR_HYPER_ADDRESS\n");
        }
#endif

        if (((PVOID)PointerPte >= (PVOID)PTE_BASE) && ((PVOID)PointerPte <= (PVOID)MmHyperSpaceEnd))
        {
            extern VOID MiArm64MapAliasForPointer(_In_ PVOID AliasVa);

#if DBG
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmArmAccessFault: PointerPte %p is in self-map range - creating backing\n",
                    PointerPte);
            }
#endif

            /* Create the self-map alias page backing for PointerPte */
            MiArm64MapAliasForPointer(PointerPte);

            /*
             * Invalidate TLB for PointerPte. The mapping was just created
             * via KSEG0 direct manipulation.
             */
            {
                ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)PointerPte) >> 12;
                __asm__ __volatile__(
                    "dsb ishst\n\t"           /* Ensure stores are visible */
                    "tlbi vae1is, %0\n\t"     /* Invalidate TLB entry */
                    "dsb ish\n\t"             /* Wait for TLB invalidation */
                    "isb\n\t"                 /* Synchronize context */
                    : : "r"(VaForTlbi) : "memory"
                );
            }
        }
#endif

#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: About to dereference PointerPte at %p\n",
                PointerPte);
        }
#endif
        TempPte = *PointerPte;

#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: Read PTE at %p = 0x%llx (Valid=%d Prototype=%d)\n",
                PointerPte, (ULONG64)TempPte.u.Long,
                TempPte.u.Hard.Valid, TempPte.u.Soft.Prototype);
        }
#endif

        /*
         * ARM64: Check if the PTE is valid using architecture-specific rules.
         * On ARM64, L3 page descriptors require bits [1:0] = 0b11 for validity.
         * A value of 0b01 (Valid=1, NotLargePage=0) is a block descriptor,
         * which is INVALID at L3 level. Using MI_IS_PTE_VALID_ARM64 ensures
         * we correctly identify only valid page descriptors.
         */
#if defined(_M_ARM64) || defined(__aarch64__)
        if (MI_IS_PTE_VALID_ARM64(TempPte))
#else
        if (TempPte.u.Hard.Valid == 1)
#endif
        {
#if defined(_M_ARM64) && DBG
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmArmAccessFault: PTE is VALID (ARM64 check passed) - entering valid PTE path\n");
            }
#endif
            /* Check if this was system space or session space */
            if (!IsSessionAddress)
            {
                /* Check if the PTE is still valid under PFN lock */
                OldIrql = MiAcquirePfnLock();
                TempPte = *PointerPte;
                /*
                 * ARM64: Re-check PTE validity with proper architecture-specific check.
                 * This is the second check after acquiring PFN lock to ensure the PTE
                 * didn't change. Must use the same ARM64-aware validity check.
                 */
#if defined(_M_ARM64) || defined(__aarch64__)
                if (MI_IS_PTE_VALID_ARM64(TempPte))
#else
                if (TempPte.u.Hard.Valid)
#endif
                {
                    /* Check if this was a write */
                    if (MI_IS_WRITE_ACCESS(FaultCode))
                    {
                        /* Was it to a read-only page? */
                        Pfn1 = MI_PFN_ELEMENT(PointerPte->u.Hard.PageFrameNumber);
                        if (!(PointerPte->u.Long & PTE_READWRITE) &&
                            !(Pfn1->OriginalPte.u.Soft.Protection & MM_READWRITE))
                        {
                            /* Crash with distinguished bugcheck code */
                            KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                                         (ULONG_PTR)Address,
                                         PointerPte->u.Long,
                                         (ULONG_PTR)TrapInformation,
                                         11);
                        }
                    }

                    /* Check for execution of non-executable memory */
                    if (MI_IS_INSTRUCTION_FETCH(FaultCode) &&
                        !MI_IS_PAGE_EXECUTABLE(&TempPte))
                    {
                        KeBugCheckEx(ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY,
                                     (ULONG_PTR)Address,
                                     (ULONG_PTR)TempPte.u.Long,
                                     (ULONG_PTR)TrapInformation,
                                     1);
                    }
                }

                /* Release PFN lock and return all good */
                MiReleasePfnLock(OldIrql);
#if defined(_M_ARM64) || defined(__aarch64__)
                /*
                 * ARM64 CRITICAL FIX: TLB Invalidation for Self-Map Addresses
                 *
                 * PROBLEM: The PTE is valid, but we took a translation fault. This means
                 * the ARM64 TLB has cached a "translation fault" result for this VA.
                 * Simply returning STATUS_SUCCESS will cause the fault to repeat infinitely
                 * because the TLB still contains the stale fault entry.
                 *
                 * ROOT CAUSE: ARM64's weakly-ordered memory model and TLB behavior:
                 * 1. When a translation fault occurs, the TLB may cache the fault result
                 * 2. If another CPU or previous code created the PTE mapping, the local
                 *    CPU's TLB doesn't automatically see it
                 * 3. The fault handler finds a valid PTE but the TLB entry says "fault"
                 * 4. Without TLB invalidation, we loop forever
                 *
                 * SOLUTION: For self-map addresses (PTE_BASE region), explicitly invalidate
                 * the TLB entry before returning SUCCESS. This ensures the MMU refetches
                 * the translation and sees the valid PTE.
                 *
                 * WHY SELF-MAP ONLY: Self-map addresses are special because:
                 * - They're created on-demand by MiArm64MapAliasForPointer
                 * - The PTE might be created by another code path or CPU
                 * - The TLB state is ambiguous (fault cached, but PTE valid)
                 * - Regular pages don't have this race condition
                 */
                if (MI_IS_PAGE_TABLE_OR_HYPER_ADDRESS(Address))
                {
                    /*
                     * Invalidate TLB for this specific VA using TLBI VAE1IS.
                     * - DSB ISHST: Ensure all prior stores (PTE writes) are visible
                     * - TLBI VAE1IS: Invalidate TLB entry for this VA (Inner Shareable)
                     * - DSB ISH: Wait for TLB invalidation to complete
                     * - ISB: Synchronize instruction fetch pipeline
                     */
                    ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
                    __asm__ __volatile__(
                        "dsb ishst\n\t"           /* Ensure stores are visible */
                        "tlbi vae1is, %0\n\t"     /* Invalidate TLB for this VA */
                        "dsb ish\n\t"             /* Barrier before instruction fetch */
                        "isb"                      /* Synchronize context */
                        : : "r"(VaForTlbi) : "memory");
                }
#endif
                return STATUS_SUCCESS;
            }
        }
#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: PTE is NOT valid (ARM64 check failed) - continuing to other checks\n");
        }
#endif
#if (_MI_PAGING_LEVELS == 2)
        /* Check if this was a session PTE that needs to remap the session PDE */
        if (MI_IS_SESSION_PTE(Address))
        {
            /* Do the remapping */
            Status = MiCheckPdeForSessionSpace(Address);
            if (!NT_SUCCESS(Status))
            {
                /* It failed, this address is invalid */
                KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                             (ULONG_PTR)Address,
                             FaultCode,
                             (ULONG_PTR)TrapInformation,
                             6);
            }
        }
#else

_WARN("Session space stuff is not implemented yet!")

#endif

        /* Check for a fault on the page table or hyperspace */
        if (MI_IS_PAGE_TABLE_OR_HYPER_ADDRESS(Address))
        {
#if (_MI_PAGING_LEVELS < 3)
            /* Windows does this check but I don't understand why -- it's done above! */
            ASSERT(MiCheckPdeForPagedPool(Address) != STATUS_WAIT_1);
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
            /*
             * ARM64 CRITICAL FIX: Self-map alias pages must be created on-demand.
             *
             * When code accesses PTE_BASE, PDE_BASE, PPE_BASE, or PXE_BASE addresses,
             * these are recursive self-map windows that require intermediate page table
             * mappings to exist. Unlike x86_64 where the self-map works more directly,
             * ARM64's 4-level translation requires explicit L0/L1/L2 table entries.
             *
             * Example: Accessing PTE for kernel VA 0xFFFF800042800000:
             *   PTE address = PTE_BASE + ((VA >> 12) << 3) = FFFFF6C000214000
             *   This address itself needs page tables:
             *     L0[493] -> L0 (self-map entry)
             *     L0[493] -> L0 again (still recursive)
             *     L0[XXX] -> L1 table (must exist!)
             *     L1[YYY] -> L2 table (must exist!)
             *     L2[ZZZ] -> L3 table (must exist!)
             *     L3[offset] = the actual PTE entry
             *
             * MiArm64MapAliasForPointer creates these intermediate tables on-demand
             * and maps the final L3 page via the KSEG0 direct-map window.
             */
            extern VOID MiArm64MapAliasForPointer(_In_ PVOID AliasVa);

            /* Ensure the self-map alias page is backed by physical memory */
            MiArm64MapAliasForPointer(Address);

            /*
             * Invalidate TLB for the faulting address. The mapping was just created
             * via KSEG0 direct manipulation, so we need to ensure the MMU sees it.
             * Use TLBI VAE1IS (invalidate by VA, Inner Shareable) for SMP safety.
             */
            {
                ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
                __asm__ __volatile__(
                    "dsb ishst\n\t"           /* Ensure stores are visible */
                    "tlbi vae1is, %0\n\t"     /* Invalidate TLB for this VA */
                    "dsb ish\n\t"             /* Barrier before instruction fetch */
                    "isb"                      /* Synchronize context */
                    : : "r"(VaForTlbi) : "memory");
            }

            /* Return success - the faulting instruction will retry and succeed */
            return STATUS_SUCCESS;
#else
            /* Handle this as a user mode fault */
            goto UserFault;
#endif
        }

        /* Get the current thread */
        CurrentThread = PsGetCurrentThread();

#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: Past MI_IS_PAGE_TABLE check - getting working set for addr=%p\n", Address);
        }
#endif

        /* What kind of address is this */
        if (!IsSessionAddress)
        {
            /* Use the system working set */
            WorkingSet = &MmSystemCacheWs;
            CurrentProcess = NULL;

#if defined(_M_ARM64) && DBG
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmArmAccessFault: Checking working set locks - ProcExcl=%d ProcShared=%d SysExcl=%d SysShared=%d SessExcl=%d SessShared=%d\n",
                    CurrentThread->OwnsProcessWorkingSetExclusive,
                    CurrentThread->OwnsProcessWorkingSetShared,
                    CurrentThread->OwnsSystemWorkingSetExclusive,
                    CurrentThread->OwnsSystemWorkingSetShared,
                    CurrentThread->OwnsSessionWorkingSetExclusive,
                    CurrentThread->OwnsSessionWorkingSetShared);
            }
#endif

            /* Make sure we don't have a recursive working set lock */
            if ((CurrentThread->OwnsProcessWorkingSetExclusive) ||
                (CurrentThread->OwnsProcessWorkingSetShared) ||
                (CurrentThread->OwnsSystemWorkingSetExclusive) ||
                (CurrentThread->OwnsSystemWorkingSetShared) ||
                (CurrentThread->OwnsSessionWorkingSetExclusive) ||
                (CurrentThread->OwnsSessionWorkingSetShared))
            {
#if defined(_M_ARM64) && DBG
                extern PVOID MiSystemViewStart;
                if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                    (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                        "[arm64] MmArmAccessFault: RECURSIVE WORKING SET LOCK DETECTED - returning STATUS_IN_PAGE_ERROR\n");
                }
#endif
                /* Fail */
                return STATUS_IN_PAGE_ERROR | 0x10000000;
            }
        }
        else
        {
            /* Use the session process and working set */
            CurrentProcess = HYDRA_PROCESS;
            WorkingSet = &MmSessionSpace->GlobalVirtualAddress->Vm;

            /* Make sure we don't have a recursive working set lock */
            if ((CurrentThread->OwnsSessionWorkingSetExclusive) ||
                (CurrentThread->OwnsSessionWorkingSetShared))
            {
                /* Fail */
                return STATUS_IN_PAGE_ERROR | 0x10000000;
            }
        }
RetryKernel:
#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: About to acquire working set lock\n");
        }
#endif
        /* Acquire the working set lock */
        KeRaiseIrql(APC_LEVEL, &LockIrql);
        MiLockWorkingSet(CurrentThread, WorkingSet);

#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: Acquired working set lock - re-reading PTE at %p\n", PointerPte);
        }
#endif

        /* Re-read PTE now that we own the lock */
        TempPte = *PointerPte;
#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: Re-read PTE = 0x%llx (Valid=%d)\n",
                (ULONG64)TempPte.u.Long, TempPte.u.Hard.Valid);
        }
#endif
        if (TempPte.u.Hard.Valid == 1)
        {
            /* Check if this was a write */
            if (MI_IS_WRITE_ACCESS(FaultCode))
            {
                /* Was it to a read-only page that is not copy on write? */
                Pfn1 = MI_PFN_ELEMENT(PointerPte->u.Hard.PageFrameNumber);
                if (!(TempPte.u.Long & PTE_READWRITE) &&
                    !(Pfn1->OriginalPte.u.Soft.Protection & MM_READWRITE) &&
                    !MI_IS_PAGE_COPY_ON_WRITE(&TempPte))
                {
                    /* Case not yet handled */
                    ASSERT(!IsSessionAddress);

                    /* Crash with distinguished bugcheck code */
                    KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                                 (ULONG_PTR)Address,
                                 TempPte.u.Long,
                                 (ULONG_PTR)TrapInformation,
                                 12);
                }
            }

            /* Check for execution of non-executable memory */
            if (MI_IS_INSTRUCTION_FETCH(FaultCode) &&
                !MI_IS_PAGE_EXECUTABLE(&TempPte))
            {
                KeBugCheckEx(ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY,
                             (ULONG_PTR)Address,
                             (ULONG_PTR)TempPte.u.Long,
                             (ULONG_PTR)TrapInformation,
                             2);
            }

            /* Check for read-only write in session space */
            if ((IsSessionAddress) &&
                MI_IS_WRITE_ACCESS(FaultCode) &&
                !MI_IS_PAGE_WRITEABLE(&TempPte))
            {
                /* Sanity check */
                ASSERT(MI_IS_SESSION_IMAGE_ADDRESS(Address));

                /* Was this COW? */
                if (!MI_IS_PAGE_COPY_ON_WRITE(&TempPte))
                {
                    /* Then this is not allowed */
                    KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                                 (ULONG_PTR)Address,
                                 (ULONG_PTR)TempPte.u.Long,
                                 (ULONG_PTR)TrapInformation,
                                 13);
                }

                /* Otherwise, handle COW */
                ASSERT(FALSE);
            }

            /* Release the working set */
            MiUnlockWorkingSet(CurrentThread, WorkingSet);
            KeLowerIrql(LockIrql);

            /* Otherwise, the PDE was probably invalid, and all is good now */
            return STATUS_SUCCESS;
        }

#if defined(_M_ARM64) && DBG
        /* Debug: Log PTE value before prototype check for System View Space */
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: Before prototype check - TempPte=0x%llx Prototype=%d Valid=%d\n",
                (ULONG64)TempPte.u.Long, TempPte.u.Soft.Prototype, TempPte.u.Hard.Valid);
        }
#endif

        /* Check one kind of prototype PTE */
        if (TempPte.u.Soft.Prototype)
        {
#if defined(_M_ARM64) && DBG
            extern PVOID MiSystemViewStart;
            if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[arm64] MmArmAccessFault: PROTOTYPE PTE DETECTED! addr=%p PTE.u.Long=0x%llx PageFileHigh=0x%lx\n",
                    Address, (ULONG64)TempPte.u.Long, (ULONG)TempPte.u.Soft.PageFileHigh);
            }
#endif

            /* Make sure protected pool is on, and that this is a pool address */
            if ((MmProtectFreedNonPagedPool) &&
                (((Address >= MmNonPagedPoolStart) &&
                  (Address < (PVOID)((ULONG_PTR)MmNonPagedPoolStart +
                                     MmSizeOfNonPagedPoolInBytes))) ||
                 ((Address >= MmNonPagedPoolExpansionStart) &&
                  (Address < MmNonPagedPoolEnd))))
            {
                /* Bad boy, bad boy, whatcha gonna do, whatcha gonna do when ARM3 comes for you! */
                KeBugCheckEx(DRIVER_CAUGHT_MODIFYING_FREED_POOL,
                             (ULONG_PTR)Address,
                             FaultCode,
                             Mode,
                             4);
            }

            /*
             * Check if we need to locate the prototype PTE via VAD lookup.
             * When PageFileHigh == MI_PTE_LOOKUP_NEEDED, the prototype PTE address
             * is not encoded in the PTE itself and must be looked up.
             *
             * ARM64: This applies to System View Space and session space.
             * The PTE format in these cases uses MMPTE_SOFTWARE with
             * PageFileHigh=MI_PTE_LOOKUP_NEEDED as a sentinel value.
             */
            if (TempPte.u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED)
            {
                /* Look up the prototype PTE via VAD */
                ProtoPte = MiCheckVirtualAddress(Address,
                                                 &ProtectionCode,
                                                 &Vad);
                if (!ProtoPte)
                {
                    /* Invalid address - bugcheck */
                    KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                                 (ULONG_PTR)Address,
                                 FaultCode,
                                 (ULONG_PTR)TrapInformation,
                                 0x100);
                }
            }
            else
            {
                /*
                 * The prototype PTE address is encoded in the PTE.
                 * On ARM64/x64, this uses MMPTE_PROTOTYPE format with ProtoAddress
                 * in bits 16-63 (sign-extended from bit 47).
                 *
                 * Decode the prototype PTE address from the PTE value.
                 */
                ProtoPte = MiProtoPteToPte(&TempPte);

#if defined(_M_ARM64) || defined(__aarch64__)
                /* Debug: Log the decoded prototype PTE address for System View Space */
                extern PVOID MiSystemViewStart;
                if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
                    (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                        "[arm64] MmArmAccessFault: Decoded ProtoPte=%p from PTE=0x%llx\n",
                        ProtoPte, (ULONG64)TempPte.u.Long);
                }

                /* Sanity check: prototype PTEs should be in kernel space */
                if ((ULONG_PTR)ProtoPte < 0xFFFF000000000000ULL)
                {
                    KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                                 (ULONG_PTR)Address,
                                 (ULONG_PTR)ProtoPte,
                                 TempPte.u.Long,
                                 0x200);
                }
#endif
            }
        }
        else
        {
            /* We don't implement transition PTEs */
            ASSERT(TempPte.u.Soft.Transition == 0);

            /* Check for no-access PTE */
            if (TempPte.u.Soft.Protection == MM_NOACCESS)
            {
                /* Bugcheck the system! */
                KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                             (ULONG_PTR)Address,
                             FaultCode,
                             (ULONG_PTR)TrapInformation,
                             1);
            }

            /* Check for no protection at all */
            if (TempPte.u.Soft.Protection == MM_ZERO_ACCESS)
            {
                /*
                 * ARM64/AMD64 KERNEL SECTION VIEW DEBUG:
                 *
                 * If we reach here with a kernel section view address, it means
                 * the fault routing in MmAccessFault (mmfault.c) is broken.
                 * Kernel section views should be routed to MmNotPresentFault,
                 * NOT to MmArmAccessFault.
                 *
                 * Log diagnostic info before bugchecking.
                 */
#if defined(_M_ARM64) || defined(__aarch64__)
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[MmArmAccessFault] MM_ZERO_ACCESS at %p - should not be here for section views!\n",
                    Address);
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                    "[MmArmAccessFault] TempPte=0x%llx FaultCode=%x\n",
                    TempPte.u.Long, FaultCode);

                /* Bugcheck - this address has no protection */
                KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                             (ULONG_PTR)Address,
                             FaultCode,
                             (ULONG_PTR)TrapInformation,
                             0);
#endif
            }
        }

// HandleProtoPte: /* Unused label - may be needed for future proto PTE handling */
        /* Check for demand page */
        if (MI_IS_WRITE_ACCESS(FaultCode) &&
            !(ProtoPte) &&
            !(IsSessionAddress) &&
            !(TempPte.u.Hard.Valid))
        {
            /* Get the protection code */
            ASSERT(TempPte.u.Soft.Transition == 0);
            if (!(TempPte.u.Soft.Protection & MM_READWRITE))
            {
                /* Bugcheck the system! */
                KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                             (ULONG_PTR)Address,
                             TempPte.u.Long,
                             (ULONG_PTR)TrapInformation,
                             14);
            }
        }

        /* Now do the real fault handling */
        Status = MiDispatchFault(FaultCode,
                                 Address,
                                 PointerPte,
                                 ProtoPte,
                                 FALSE,
                                 CurrentProcess,
                                 TrapInformation,
                                 NULL);

        /* Release the working set */
        ASSERT(KeAreAllApcsDisabled() == TRUE);
#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: About to release working set lock - Status=0x%lx\n", Status);
        }
#endif
        MiUnlockWorkingSet(CurrentThread, WorkingSet);
        KeLowerIrql(LockIrql);
#if defined(_M_ARM64) && DBG
        extern PVOID MiSystemViewStart;
        if ((ULONG_PTR)Address >= (ULONG_PTR)MiSystemViewStart &&
            (ULONG_PTR)Address < (ULONG_PTR)MiSystemViewStart + 0x1000000)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[arm64] MmArmAccessFault: Released working set lock successfully\n");
        }
#endif

        if (Status == STATUS_NO_MEMORY)
        {
            MmRebalanceMemoryConsumersAndWait();
            goto RetryKernel;
        }

        /* We are done! */
        DPRINT("Fault resolved with status: %lx\n", Status);
        return Status;
    }

    /* This is a user fault */
UserFault:
    CurrentThread = PsGetCurrentThread();
    CurrentProcess = (PEPROCESS)CurrentThread->Tcb.ApcState.Process;

    /* Lock the working set */
    MiLockProcessWorkingSet(CurrentProcess, CurrentThread);

    ProtectionCode = MM_INVALID_PROTECTION;

#if (_MI_PAGING_LEVELS == 4)
    /* Check if the PXE is valid */
    if (PointerPxe->u.Hard.Valid == 0)
    {
        /* Right now, we only handle scenarios where the PXE is totally empty */
        ASSERT(PointerPxe->u.Long == 0);

        /* This is only possible for user mode addresses! */
        if (PointerPte > MiHighestUserPte)
        {
            Status = STATUS_ACCESS_VIOLATION;
            goto ExitUser;
        }

        /* Check if we have a VAD */
        MiCheckVirtualAddress(Address, &ProtectionCode, &Vad);
        if (ProtectionCode == MM_NOACCESS)
        {
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            return STATUS_ACCESS_VIOLATION;
        }

        /* Resolve a demand zero fault */
        Status = MiResolveDemandZeroFault(PointerPpe,
                                 PointerPxe,
                                 MM_EXECUTE_READWRITE,
                                 CurrentProcess,
                                 MM_NOIRQL);
        if (!NT_SUCCESS(Status))
        {
            goto ExitUser;
        }

        /* We should come back with a valid PXE */
        ASSERT(PointerPxe->u.Hard.Valid == 1);
    }
#endif

#if (_MI_PAGING_LEVELS >= 3)
    /* Check if the PPE is valid */
    if (PointerPpe->u.Hard.Valid == 0)
    {
        /* Right now, we only handle scenarios where the PPE is totally empty */
        ASSERT(PointerPpe->u.Long == 0);

        /* This is only possible for user mode addresses! */
        if (PointerPte > MiHighestUserPte)
        {
            Status = STATUS_ACCESS_VIOLATION;
            goto ExitUser;
        }

        /* Check if we have a VAD, unless we did this already */
        if (ProtectionCode == MM_INVALID_PROTECTION)
        {
            MiCheckVirtualAddress(Address, &ProtectionCode, &Vad);
        }

        if (ProtectionCode == MM_NOACCESS)
        {
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            return STATUS_ACCESS_VIOLATION;
        }

        /* Resolve a demand zero fault */
        Status = MiResolveDemandZeroFault(PointerPde,
                                 PointerPpe,
                                 MM_EXECUTE_READWRITE,
                                 CurrentProcess,
                                 MM_NOIRQL);
        if (!NT_SUCCESS(Status))
        {
            goto ExitUser;
        }

        /* We should come back with a valid PPE */
        ASSERT(PointerPpe->u.Hard.Valid == 1);
        MiIncrementPageTableReferences(PointerPde);
    }
#endif

    /* Check if the PDE is invalid */
    if (PointerPde->u.Hard.Valid == 0)
    {
        /* Right now, we only handle scenarios where the PDE is totally empty */
        ASSERT(PointerPde->u.Long == 0);

        /* And go dispatch the fault on the PDE. This should handle the demand-zero */
#if MI_TRACE_PFNS
        UserPdeFault = TRUE;
#endif
        /* Check if we have a VAD, unless we did this already */
        if (ProtectionCode == MM_INVALID_PROTECTION)
        {
            MiCheckVirtualAddress(Address, &ProtectionCode, &Vad);
        }

        if (ProtectionCode == MM_NOACCESS)
        {
#if (_MI_PAGING_LEVELS == 2)
            /* Could be a page table for paged pool */
            MiCheckPdeForPagedPool(Address);
#endif
            /* Has the code above changed anything -- is this now a valid PTE? */
            Status = (PointerPde->u.Hard.Valid == 1) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;

            /* Either this was a bogus VA or we've fixed up a paged pool PDE */
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            return Status;
        }

        /* Resolve a demand zero fault */
        Status = MiResolveDemandZeroFault(PointerPte,
                                 PointerPde,
                                 MM_EXECUTE_READWRITE,
                                 CurrentProcess,
                                 MM_NOIRQL);
        if (!NT_SUCCESS(Status))
        {
            goto ExitUser;
        }

#if _MI_PAGING_LEVELS >= 3
        MiIncrementPageTableReferences(PointerPte);
#endif

#if MI_TRACE_PFNS
        UserPdeFault = FALSE;
        /* Update debug info */
        if (TrapInformation)
            MiGetPfnEntry(PointerPde->u.Hard.PageFrameNumber)->CallSite = (PVOID)((PKTRAP_FRAME)TrapInformation)->Eip;
        else
            MiGetPfnEntry(PointerPde->u.Hard.PageFrameNumber)->CallSite = _ReturnAddress();
#endif
        /* We should come back with APCs enabled, and with a valid PDE */
        ASSERT(KeAreAllApcsDisabled() == TRUE);
        ASSERT(PointerPde->u.Hard.Valid == 1);
    }
    else
    {
        /* Not yet implemented in ReactOS */
        ASSERT(MI_IS_PAGE_LARGE(PointerPde) == FALSE);
    }

    /* Now capture the PTE. */
    TempPte = *PointerPte;

    /* Check if the PTE is valid */
    if (TempPte.u.Hard.Valid)
    {
        /* Check if this is a write on a readonly PTE */
        if (MI_IS_WRITE_ACCESS(FaultCode))
        {
            /* Is this a copy on write PTE? */
            if (MI_IS_PAGE_COPY_ON_WRITE(&TempPte))
            {
                PFN_NUMBER PageFrameIndex, OldPageFrameIndex;
                PMMPFN Pfn1;

                LockIrql = MiAcquirePfnLock();

                ASSERT(MmAvailablePages > 0);

                MI_SET_USAGE(MI_USAGE_COW);
                MI_SET_PROCESS(CurrentProcess);

                /* Allocate a new page and copy it */
                PageFrameIndex = MiRemoveAnyPage(MI_GET_NEXT_PROCESS_COLOR(CurrentProcess));
                if (PageFrameIndex == 0)
                {
                    MiReleasePfnLock(LockIrql);
                    Status = STATUS_NO_MEMORY;
                    goto ExitUser;
                }
                OldPageFrameIndex = PFN_FROM_PTE(&TempPte);

                MiCopyPfn(PageFrameIndex, OldPageFrameIndex);

                /* Dereference whatever this PTE is referencing */
                Pfn1 = MI_PFN_ELEMENT(OldPageFrameIndex);
                ASSERT(Pfn1->u3.e1.PrototypePte == 1);
                ASSERT(!MI_IS_PFN_DELETED(Pfn1));
                ProtoPte = Pfn1->PteAddress;
                MiDeletePte(PointerPte, Address, CurrentProcess, ProtoPte);

                /* And make a new shiny one with our page */
                MiInitializePfn(PageFrameIndex, PointerPte, TRUE);
                TempPte.u.Hard.PageFrameNumber = PageFrameIndex;
                MI_MAKE_WRITE_PAGE(&TempPte);
                TempPte.u.Hard.CopyOnWrite = 0;

                MI_WRITE_VALID_PTE(PointerPte, TempPte);

                MiReleasePfnLock(LockIrql);

                /* Return the status */
                MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
                return STATUS_PAGE_FAULT_COPY_ON_WRITE;
            }

            /* Is this a read-only PTE? */
            if (!MI_IS_PAGE_WRITEABLE(&TempPte))
            {
                /* Return the status */
                MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
                return STATUS_ACCESS_VIOLATION;
            }
        }

#if _MI_HAS_NO_EXECUTE
        /* Check for execution of non-executable memory */
        if (MI_IS_INSTRUCTION_FETCH(FaultCode) &&
            !MI_IS_PAGE_EXECUTABLE(&TempPte))
        {
            /* Check if execute enable was set */
            if (CurrentProcess->Pcb.Flags.ExecuteEnable)
            {
                /* Fix up the PTE to be executable */
#if defined(_M_ARM64) || defined(__aarch64__)
                TempPte.u.Hard.UserNoExecute = 0;
                TempPte.u.Hard.PrivilegedNoExecute = 0;
#else
                TempPte.u.Hard.NoExecute = 0;
#endif
                MI_UPDATE_VALID_PTE(PointerPte, TempPte);
                MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
                return STATUS_SUCCESS;
            }

            /* Return the status */
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            return STATUS_ACCESS_VIOLATION;
        }
#endif

        /* The fault has already been resolved by a different thread */
        MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
        return STATUS_SUCCESS;
    }

    /* Quick check for demand-zero */
    if ((TempPte.u.Long == (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS)) ||
        (TempPte.u.Long == (MM_EXECUTE_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS)))
    {
        /* Resolve the fault */
        Status = MiResolveDemandZeroFault(Address,
                                 PointerPte,
                                 TempPte.u.Soft.Protection,
                                 CurrentProcess,
                                 MM_NOIRQL);
        if (!NT_SUCCESS(Status))
        {
            goto ExitUser;
        }

#if MI_TRACE_PFNS
        /* Update debug info */
        if (TrapInformation)
            MiGetPfnEntry(PointerPte->u.Hard.PageFrameNumber)->CallSite = (PVOID)((PKTRAP_FRAME)TrapInformation)->Eip;
        else
            MiGetPfnEntry(PointerPte->u.Hard.PageFrameNumber)->CallSite = _ReturnAddress();
#endif

        /* Return the status */
        MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
        return STATUS_PAGE_FAULT_DEMAND_ZERO;
    }

    /* Check for zero PTE */
    if (TempPte.u.Long == 0)
    {
        /* Check if this address range belongs to a valid allocation (VAD) */
        ProtoPte = MiCheckVirtualAddress(Address, &ProtectionCode, &Vad);
        if (ProtectionCode == MM_NOACCESS)
        {
#if (_MI_PAGING_LEVELS == 2)
            /* Could be a page table for paged pool */
            MiCheckPdeForPagedPool(Address);
#endif
            /* Has the code above changed anything -- is this now a valid PTE? */
            Status = (PointerPte->u.Hard.Valid == 1) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;

            /* Either this was a bogus VA or we've fixed up a paged pool PDE */
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            return Status;
        }

        /*
         * Check if this is a real user-mode address or actually a kernel-mode
         * page table for a user mode address
         */
        if (Address <= MM_HIGHEST_USER_ADDRESS
#if _MI_PAGING_LEVELS >= 3
            || MiIsUserPte(Address)
#if _MI_PAGING_LEVELS == 4
            || MiIsUserPde(Address)
#endif
#endif
        )
        {
            /* Add an additional page table reference */
            MiIncrementPageTableReferences(Address);
        }

        /* Is this a guard page? */
        if ((ProtectionCode & MM_PROTECT_SPECIAL) == MM_GUARDPAGE)
        {
            /* The VAD protection cannot be MM_DECOMMIT! */
            ASSERT(ProtectionCode != MM_DECOMMIT);

            /* Remove the bit */
            TempPte.u.Soft.Protection = ProtectionCode & ~MM_GUARDPAGE;
            MI_WRITE_INVALID_PTE(PointerPte, TempPte);

            /* Not supported */
            ASSERT(ProtoPte == NULL);
            ASSERT(CurrentThread->ApcNeeded == 0);

            /* Drop the working set lock */
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            ASSERT(KeGetCurrentIrql() == OldIrql);

            /* Handle stack expansion */
            return MiCheckForUserStackOverflow(Address, TrapInformation);
        }

        /* Did we get a prototype PTE back? */
        if (!ProtoPte)
        {
            /* Is this PTE actually part of the PDE-PTE self-mapping directory? */
            if (PointerPde == MiAddressToPde(PTE_BASE))
            {
                /* Then it's really a demand-zero PDE (on behalf of user-mode) */
#ifdef _M_ARM
                _WARN("This is probably completely broken!");
                MI_WRITE_INVALID_PDE((PMMPDE)PointerPte, DemandZeroPde);
#else
                MI_WRITE_INVALID_PDE(PointerPte, DemandZeroPde);
#endif
            }
            else
            {
                /* No, create a new PTE. First, write the protection */
                TempPte.u.Soft.Protection = ProtectionCode;
                MI_WRITE_INVALID_PTE(PointerPte, TempPte);
            }

            /* Lock the PFN database since we're going to grab a page */
            OldIrql = MiAcquirePfnLock();

            /* Make sure we have enough pages */
            //ASSERT(MmAvailablePages >= 32);

            /* Try to get a zero page */
            MI_SET_USAGE(MI_USAGE_PEB_TEB);
            MI_SET_PROCESS2(CurrentProcess->ImageFileName);
            Color = MI_GET_NEXT_PROCESS_COLOR(CurrentProcess);
            PageFrameIndex = MiRemoveZeroPageSafe(Color);
            if (!PageFrameIndex)
            {
                /* Grab a page out of there. Later we should grab a colored zero page */
                PageFrameIndex = MiRemoveAnyPage(Color);

                /* Release the lock since we need to do some zeroing */
                MiReleasePfnLock(OldIrql);

                if (PageFrameIndex == 0)
                {
                    Status = STATUS_NO_MEMORY;
                    goto ExitUser;
                }

                /* Zero out the page, since it's for user-mode */
                MiZeroPfn(PageFrameIndex);

                /* Grab the lock again so we can initialize the PFN entry */
                OldIrql = MiAcquirePfnLock();
            }

            /* Initialize the PFN entry now */
            MiInitializePfn(PageFrameIndex, PointerPte, 1);

            /* Increment the count of pages in the process */
            CurrentProcess->NumberOfPrivatePages++;

            /* One more demand-zero fault */
            KeGetCurrentPrcb()->MmDemandZeroCount++;

            /* And we're done with the lock */
            MiReleasePfnLock(OldIrql);

            /* Fault on user PDE, or fault on user PTE? */
            if (PointerPte <= MiHighestUserPte)
            {
                /* User fault, build a user PTE */
                MI_MAKE_HARDWARE_PTE_USER(&TempPte,
                                          PointerPte,
                                          PointerPte->u.Soft.Protection,
                                          PageFrameIndex);
            }
            else
            {
                /* This is a user-mode PDE, create a kernel PTE for it */
                MI_MAKE_HARDWARE_PTE(&TempPte,
                                     PointerPte,
                                     PointerPte->u.Soft.Protection,
                                     PageFrameIndex);
            }

            /* Write the dirty bit for writeable pages */
            if (MI_IS_PAGE_WRITEABLE(&TempPte)) MI_MAKE_DIRTY_PAGE(&TempPte);

            /* And now write down the PTE, making the address valid */
            MI_WRITE_VALID_PTE(PointerPte, TempPte);
            Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);
            ASSERT(Pfn1->u1.Event == NULL);

            /* Demand zero */
            ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            return STATUS_PAGE_FAULT_DEMAND_ZERO;
        }

        /* We should have a valid protection here */
        ASSERT(ProtectionCode != 0x100);

        /* Write the prototype PTE */
        TempPte = PrototypePte;
        TempPte.u.Soft.Protection = ProtectionCode;
        ASSERT(TempPte.u.Long != 0);
        MI_WRITE_INVALID_PTE(PointerPte, TempPte);
    }
    else
    {
        /* Get the protection code and check if this is a proto PTE */
        ProtectionCode = (ULONG)TempPte.u.Soft.Protection;
        if (TempPte.u.Soft.Prototype)
        {
            /* Do we need to go find the real PTE? */
            if (TempPte.u.Soft.PageFileHigh == MI_PTE_LOOKUP_NEEDED)
            {
                /* Get the prototype pte and VAD for it */
                ProtoPte = MiCheckVirtualAddress(Address,
                                                 &ProtectionCode,
                                                 &Vad);
                if (!ProtoPte)
                {
                    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
                    MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
                    return STATUS_ACCESS_VIOLATION;
                }
            }
            else
            {
                /* Get the prototype PTE! */
                ProtoPte = MiProtoPteToPte(&TempPte);

                /* Is it read-only */
                if (TempPte.u.Proto.ReadOnly)
                {
                    /* Set read-only code */
                    ProtectionCode = MM_READONLY;
                }
                else
                {
                    /* Set unknown protection */
                    ProtectionCode = 0x100;
                    ASSERT(CurrentProcess->CloneRoot != NULL);
                }
            }
        }
    }

    /* Do we have a valid protection code? */
    if (ProtectionCode != 0x100)
    {
        /* Run a software access check first, including to detect guard pages */
        Status = MiAccessCheck(PointerPte,
                               !MI_IS_NOT_PRESENT_FAULT(FaultCode),
                               Mode,
                               ProtectionCode,
                               TrapInformation,
                               FALSE);
        if (Status != STATUS_SUCCESS)
        {
            /* Not supported */
            ASSERT(CurrentThread->ApcNeeded == 0);

            /* Drop the working set lock */
            MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);
            ASSERT(KeGetCurrentIrql() == OldIrql);

            /* Did we hit a guard page? */
            if (Status == STATUS_GUARD_PAGE_VIOLATION)
            {
                /* Handle stack expansion */
                return MiCheckForUserStackOverflow(Address, TrapInformation);
            }

            /* Otherwise, fail back to the caller directly */
            return Status;
        }
    }

    /* Dispatch the fault */
    Status = MiDispatchFault(FaultCode,
                             Address,
                             PointerPte,
                             ProtoPte,
                             FALSE,
                             CurrentProcess,
                             TrapInformation,
                             Vad);

ExitUser:

    /* Return the status */
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    MiUnlockProcessWorkingSet(CurrentProcess, CurrentThread);

    if (Status == STATUS_NO_MEMORY)
    {
        MmRebalanceMemoryConsumersAndWait();
        goto UserFault;
    }

    return Status;
}

NTSTATUS
NTAPI
MmGetExecuteOptions(IN PULONG ExecuteOptions)
{
    PKPROCESS CurrentProcess = &PsGetCurrentProcess()->Pcb;
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    *ExecuteOptions = 0;

    if (CurrentProcess->Flags.ExecuteDisable)
    {
        *ExecuteOptions |= MEM_EXECUTE_OPTION_DISABLE;
    }

    if (CurrentProcess->Flags.ExecuteEnable)
    {
        *ExecuteOptions |= MEM_EXECUTE_OPTION_ENABLE;
    }

    if (CurrentProcess->Flags.DisableThunkEmulation)
    {
        *ExecuteOptions |= MEM_EXECUTE_OPTION_DISABLE_THUNK_EMULATION;
    }

    if (CurrentProcess->Flags.Permanent)
    {
        *ExecuteOptions |= MEM_EXECUTE_OPTION_PERMANENT;
    }

    if (CurrentProcess->Flags.ExecuteDispatchEnable)
    {
        *ExecuteOptions |= MEM_EXECUTE_OPTION_EXECUTE_DISPATCH_ENABLE;
    }

    if (CurrentProcess->Flags.ImageDispatchEnable)
    {
        *ExecuteOptions |= MEM_EXECUTE_OPTION_IMAGE_DISPATCH_ENABLE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmSetExecuteOptions(IN ULONG ExecuteOptions)
{
    PKPROCESS CurrentProcess = &PsGetCurrentProcess()->Pcb;
    KLOCK_QUEUE_HANDLE ProcessLock;
    NTSTATUS Status = STATUS_ACCESS_DENIED;
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    /* Only accept valid flags */
    if (ExecuteOptions & ~MEM_EXECUTE_OPTION_VALID_FLAGS)
    {
        /* Fail */
        DPRINT1("Invalid no-execute options\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Change the NX state in the process lock */
    KiAcquireProcessLockRaiseToSynch(CurrentProcess, &ProcessLock);

    /* Don't change anything if the permanent flag was set */
    if (!CurrentProcess->Flags.Permanent)
    {
        /* Start by assuming it's not disabled */
        CurrentProcess->Flags.ExecuteDisable = FALSE;

        /* Now process each flag and turn the equivalent bit on */
        if (ExecuteOptions & MEM_EXECUTE_OPTION_DISABLE)
        {
            CurrentProcess->Flags.ExecuteDisable = TRUE;
        }
        if (ExecuteOptions & MEM_EXECUTE_OPTION_ENABLE)
        {
            CurrentProcess->Flags.ExecuteEnable = TRUE;
        }
        if (ExecuteOptions & MEM_EXECUTE_OPTION_DISABLE_THUNK_EMULATION)
        {
            CurrentProcess->Flags.DisableThunkEmulation = TRUE;
        }
        if (ExecuteOptions & MEM_EXECUTE_OPTION_PERMANENT)
        {
            CurrentProcess->Flags.Permanent = TRUE;
        }
        if (ExecuteOptions & MEM_EXECUTE_OPTION_EXECUTE_DISPATCH_ENABLE)
        {
            CurrentProcess->Flags.ExecuteDispatchEnable = TRUE;
        }
        if (ExecuteOptions & MEM_EXECUTE_OPTION_IMAGE_DISPATCH_ENABLE)
        {
            CurrentProcess->Flags.ImageDispatchEnable = TRUE;
        }

        /* These are turned on by default if no-execution is also enabled */
        if (CurrentProcess->Flags.ExecuteEnable)
        {
            CurrentProcess->Flags.ExecuteDispatchEnable = TRUE;
            CurrentProcess->Flags.ImageDispatchEnable = TRUE;
        }

        /* All good */
        Status = STATUS_SUCCESS;
    }

    /* Release the lock and return status */
    KiReleaseProcessLock(&ProcessLock);
    return Status;
}

/* EOF */
