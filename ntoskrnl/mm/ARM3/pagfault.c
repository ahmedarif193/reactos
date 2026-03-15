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
#include <mm/ARM3/mibugchk.h>

VOID
NTAPI
MmRebalanceMemoryConsumersAndWait(VOID);

#if defined(_M_ARM64) || defined(__aarch64__)
/* RosMM not-present fault handler - used for ARM64 user address routing */
NTSTATUS
NTAPI
MmNotPresentFault(KPROCESSOR_MODE Mode, ULONG_PTR Address, BOOLEAN FromMdl);
#endif

/* GLOBALS ********************************************************************/

#define HYDRA_PROCESS (PEPROCESS)1
#if MI_TRACE_PFNS
BOOLEAN UserPdeFault = FALSE;
#endif

/* PRIVATE FUNCTIONS **********************************************************/

#if defined(_M_ARM64) || defined(__aarch64__)

FORCEINLINE
ULONG64
MiArm64CurrentTtbr0Base(VOID)
{
    ULONG64 Ttbr0;
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    return (Ttbr0 & 0x0000FFFFFFFFF000ULL);
}

FORCEINLINE
BOOLEAN
MiArm64ProcessMatchesTtbr0(
    _In_opt_ PEPROCESS Process,
    _In_ ULONG64 Ttbr0Base)
{
    if ((Process == NULL) || (Process->Pcb.DirectoryTableBase[0] == 0))
        return FALSE;

    return ((((ULONG64)Process->Pcb.DirectoryTableBase[0]) & 0x0000FFFFFFFFF000ULL) == Ttbr0Base);
}

FORCEINLINE
PEPROCESS
MiArm64SelectFaultProcess(
    _In_ PETHREAD Thread,
    _In_ KPROCESSOR_MODE Mode)
{
    PKAPC_STATE ActiveApcState;
    PEPROCESS CurProcess;

    ActiveApcState = Thread->Tcb.ApcStatePointer[Thread->Tcb.ApcStateIndex];
    CurProcess = (PEPROCESS)((ActiveApcState != NULL) ?
                             ActiveApcState->Process :
                             Thread->Tcb.ApcState.Process);
    if (CurProcess == NULL)
        CurProcess = PsGetCurrentProcess();

    if (Mode == KernelMode)
    {
        ULONG64 Ttbr0Base = MiArm64CurrentTtbr0Base();
        PEPROCESS ApcProcess = (PEPROCESS)Thread->Tcb.ApcState.Process;
        PEPROCESS SavedProcess = (PEPROCESS)Thread->Tcb.SavedApcState.Process;
        PEPROCESS CurrentProcess = PsGetCurrentProcess();

        if (MiArm64ProcessMatchesTtbr0(CurProcess, Ttbr0Base)) return CurProcess;
        if (MiArm64ProcessMatchesTtbr0(ApcProcess, Ttbr0Base)) return ApcProcess;
        if (MiArm64ProcessMatchesTtbr0(SavedProcess, Ttbr0Base)) return SavedProcess;
        if (MiArm64ProcessMatchesTtbr0(CurrentProcess, Ttbr0Base)) return CurrentProcess;
    }

    return CurProcess;
}

/*
 * MiArm64HandleEarlyBootFault - Handle kernel page faults during early boot.
 *
 * During early boot (before MmArmInitSystem / MiScanMemoryDescriptors has run),
 * the MM page allocator (MxGetNextPage) is not functional because MxFreeDescriptor
 * is NULL. The recursive self-map depends on page table pages being allocated,
 * and the page allocator is not yet functional during early boot.
 *
 * Without this early-boot handler, a page fault on a kernel address during early
 * boot would:
 * 1. Try to create self-map aliases (fails due to no page allocator)
 * 2. Return STATUS_SUCCESS (claiming the fault was handled)
 * 3. The instruction retries, faults again -> infinite loop or nested fault
 *
 * This function bypasses the self-map entirely by walking TTBR1 page tables
 * physically via KSEG0 (0xFFFF800000000000 | PA). It can resolve:
 * - Access Flag faults: Set AF bit directly on the hardware PTE via KSEG0
 * - Translation faults: Return failure (page genuinely doesn't exist)
 *
 * Returns:
 *   STATUS_SUCCESS if the fault was resolved (AF was set)
 *   STATUS_MORE_PROCESSING_REQUIRED if not an early-boot situation
 *   STATUS_ACCESS_VIOLATION if the fault cannot be resolved
 */
static
NTSTATUS
MiArm64HandleEarlyBootFault(
    _In_ PVOID Address,
    _In_ PVOID TrapInformation)
{
    ULONG64 Ttbr1, RootPa;
    volatile ULONG64 *Table;
    ULONG Idx;
    ULONG64 Entry;
    ULONG FaultStatus = 0;
    BOOLEAN IsAfFault = FALSE;

    /*
     * Self-map address protection:
     * Faults on self-map VAs (PTE_BASE..PTE_BASE+1TB) can only be resolved
     * by the self-map itself — but if the merged PXE page isn't set up yet,
     * attempting to resolve them causes infinite recursion → stack overflow.
     *
     * Handle self-map faults via KSEG0 physical walk regardless of boot phase.
     */
    if (((ULONG_PTR)Address >= (ULONG_PTR)PTE_BASE) &&
        ((ULONG_PTR)Address < (ULONG_PTR)PTE_BASE + (2ULL << 39)))
    {
        /* Try to resolve via KSEG0 if merged PXE is set up */
        if (MiArm64PxeMergedPfn[0] != 0)
        {
            ULONG64 MergedPa = (ULONG64)MiArm64PxeMergedPfn[0] << PAGE_SHIFT;
            volatile ULONG64 *MergedL0 = (volatile ULONG64 *)(KSEG0_BASE | MergedPa);
            ULONG64 SelfMapEntry = MergedL0[PXE_SELFMAP_INDEX];

            /* If the self-referential entry is valid, let normal path handle it */
            if ((SelfMapEntry & 3) == 3)
                goto SelfMapReady;
        }

        /* Self-map not ready or not self-referential — cannot resolve */
        return STATUS_ACCESS_VIOLATION;
    }
SelfMapReady:

    /* Only active during early boot when MxFreeDescriptor is not yet initialized */
    if (MxFreeDescriptor != NULL)
        return STATUS_MORE_PROCESSING_REQUIRED;

    /* Only handle kernel addresses */
    if (Address < MmSystemRangeStart)
        return STATUS_MORE_PROCESSING_REQUIRED;

    /* Determine if this is an Access Flag fault from the ESR */
    if (TrapInformation != NULL &&
        (ULONG_PTR)TrapInformation >= (ULONG_PTR)MmSystemRangeStart)
    {
        PKTRAP_FRAME TrapFrame = (PKTRAP_FRAME)TrapInformation;
        FaultStatus = TrapFrame->Esr & 0x3FULL;
        /* AF faults: DFSC/IFSC 0x08-0x0B (access flag fault at level 0-3) */
        IsAfFault = ((FaultStatus & 0x3C) == 0x08);
    }

    /* Read TTBR1 to get the kernel page table root */
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = Ttbr1 & 0x0000FFFFFFFFF000ULL;
    if (RootPa == 0)
        return STATUS_ACCESS_VIOLATION;

    /* Walk L0 -> L1 -> L2 -> L3 via KSEG0 */
    /* L0 */
    Table = (volatile ULONG64 *)(0xFFFF800000000000ULL | RootPa);
    Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
    Entry = Table[Idx];

    if ((Entry & 0x3ULL) != 0x3ULL)
        return STATUS_ACCESS_VIOLATION;  /* L0 not a valid table descriptor */

    /* Check AF at L0 level (for completeness, though L0 AF faults are rare) */
    if (IsAfFault && (FaultStatus & 0x3) == 0)
    {
        if ((Entry & (1ULL << 10)) == 0)
        {
            /* This is a table descriptor at L0 - AF is architecturally ignored.
             * A level-0 AF fault should not occur for table descriptors.
             * Return failure. */
            return STATUS_ACCESS_VIOLATION;
        }
    }

    /* L1 */
    Table = (volatile ULONG64 *)(0xFFFF800000000000ULL | (Entry & 0x0000FFFFFFFFF000ULL));
    Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
    Entry = Table[Idx];

    if ((Entry & 0x1ULL) == 0)
        return STATUS_ACCESS_VIOLATION;  /* L1 invalid */

    /* L1 block (1GB page) */
    if ((Entry & 0x3ULL) == 0x1ULL)
    {
        if (IsAfFault && (FaultStatus & 0x3) == 1 && (Entry & (1ULL << 10)) == 0)
        {
            Table[Idx] = Entry | (1ULL << 10);  /* Set AF */
            __asm__ __volatile__(
                "dsb ishst\n\t"
                "tlbi vmalle1is\n\t"
                "dsb ish\n\t"
                "isb\n\t"
                ::: "memory"
            );
            return STATUS_SUCCESS;
        }
        /* Not an AF fault on this block → permission fault or other error */
        return STATUS_ACCESS_VIOLATION;
    }

    /* L2 */
    Table = (volatile ULONG64 *)(0xFFFF800000000000ULL | (Entry & 0x0000FFFFFFFFF000ULL));
    Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
    Entry = Table[Idx];

    if ((Entry & 0x1ULL) == 0)
        return STATUS_ACCESS_VIOLATION;  /* L2 invalid */

    /* L2 block (2MB page) */
    if ((Entry & 0x3ULL) == 0x1ULL)
    {
        if (IsAfFault && (FaultStatus & 0x3) == 2 && (Entry & (1ULL << 10)) == 0)
        {
            Table[Idx] = Entry | (1ULL << 10);  /* Set AF */
            __asm__ __volatile__(
                "dsb ishst\n\t"
                "tlbi vmalle1is\n\t"
                "dsb ish\n\t"
                "isb\n\t"
                ::: "memory"
            );
            return STATUS_SUCCESS;
        }
        /* Not an AF fault on this block → permission fault or other error */
        return STATUS_ACCESS_VIOLATION;
    }

    /* L3 */
    Table = (volatile ULONG64 *)(0xFFFF800000000000ULL | (Entry & 0x0000FFFFFFFFF000ULL));
    Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;
    Entry = Table[Idx];

    if ((Entry & 0x1ULL) == 0)
        return STATUS_ACCESS_VIOLATION;  /* L3/PTE invalid */

    /* L3 page descriptor - AF fault handling */
    if (IsAfFault && (FaultStatus & 0x3) == 3 && (Entry & (1ULL << 10)) == 0)
    {
        Table[Idx] = Entry | (1ULL << 10);  /* Set AF */
        __asm__ __volatile__(
            "dsb ishst\n\t"
            "tlbi vmalle1is\n\t"
            "dsb ish\n\t"
            "isb\n\t"
            ::: "memory"
        );
        return STATUS_SUCCESS;
    }

    /* Page exists and AF is already set.
     * If this is a translation fault, it should have been resolved by the walk.
     * Return success to let the instruction retry. */
    if (!IsAfFault)
    {
        /* Not an AF fault, and the page exists - might be a permission fault
         * or other issue. Return STATUS_MORE_PROCESSING_REQUIRED to fall through
         * to normal handling (though this may still fail during early boot). */
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    return STATUS_ACCESS_VIOLATION;
}

#endif /* _M_ARM64 */

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
#if defined(_M_ARM64) || defined(__aarch64__)
    PFN_NUMBER FaultL3PfnBefore = 0, FaultL3PfnAfter = 0;
    PMMPTE FaultPteBefore = NULL, FaultPteAfter = NULL;
    MMPTE FaultPteBeforeContents, FaultPteAfterContents;
#endif

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
#if defined(_M_ARM64) || defined(__aarch64__)
    FaultPteBefore = MiArm64UserPteKseg0ForPfn(OldGuardBase, &FaultL3PfnBefore);
    FaultPteBeforeContents.u.Long = (FaultPteBefore != NULL) ? FaultPteBefore->u.Long : 0ULL;
    DPRINT("[arm64][STACK] enter Addr=%p OldGuardBase=%p CommitAddr=%p GuardAddr=%p "
           "StackBase=%p StackLimit=%p Dealloc=%p Guarantee=%Ix AddCommit=%Iu "
           "PTE(before)=0x%llx L3Pfn=%Ix\n",
            Address,
            OldGuardBase,
            CommitAddress,
            GuardAddress,
            StackBase,
            Teb->NtTib.StackLimit,
            DeallocationStack,
            GuaranteedSize,
            AdditionalCommit,
            (unsigned long long)FaultPteBeforeContents.u.Long,
            (ULONG_PTR)FaultL3PfnBefore);
#endif

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
        DPRINT("[arm64][STACK] commit Status=0x%lx CommitAddr(out)=%p CommitSize=%Iu\n",
               Status, CommitAddress, CommitRegionSize);
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

    /*
     * Ensure the page we are promoting from guard (or stack gap recovery page)
     * is committed. In normal guard faults this is already committed and
     * returns STATUS_ALREADY_COMMITTED; in gap cases this commits the missing
     * page so retries can succeed.
     */
    {
        PVOID CurrentCommitBase = OldGuardBase;
        SIZE_T CurrentCommitSize = PAGE_SIZE;
        NTSTATUS CommitStatus;

        CommitStatus = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                               &CurrentCommitBase,
                                               0,
                                               &CurrentCommitSize,
                                               MEM_COMMIT,
                                               PAGE_READWRITE);
        DPRINT("[arm64][STACK] old-guard commit Status=0x%lx Base(out)=%p Size=%Iu\n",
               CommitStatus, CurrentCommitBase, CurrentCommitSize);
        if (!NT_SUCCESS(CommitStatus) && (CommitStatus != STATUS_ALREADY_COMMITTED))
        {
            DPRINT1("Failed to commit old guard page %p: %lx\n",
                    OldGuardBase, CommitStatus);
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
    DPRINT("[arm64][STACK] guard Status=0x%lx GuardAddr(out)=%p GuardSize=%Iu\n",
           Status, GuardAddress, GuardRegionSize);
#if defined(_M_ARM64) || defined(__aarch64__)
    FaultPteAfter = MiArm64UserPteKseg0ForPfn(OldGuardBase, &FaultL3PfnAfter);
    FaultPteAfterContents.u.Long = (FaultPteAfter != NULL) ? FaultPteAfter->u.Long : 0ULL;
    DPRINT("[arm64][STACK] exit Addr=%p StackLimit(now)=%p "
           "PTE(after)=0x%llx L3Pfn=%Ix\n",
            Address,
            Teb->NtTib.StackLimit,
            (unsigned long long)FaultPteAfterContents.u.Long,
            (ULONG_PTR)FaultL3PfnAfter);
#endif
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

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 RACE CONDITION FIX: On ARM64's weakly-ordered memory model,
     * even with the PFN lock held, we might see stale data at the initial
     * validity check but fresh data here after intervening memory operations
     * (page allocation, PFN init, zeroing) that contain memory barriers.
     *
     * This can happen when:
     * 1. CPU A checks PTE validity (sees invalid - stale)
     * 2. CPU A allocates page, inits PFN (operations with barriers)
     * 3. CPU A reaches here and sees PTE is NOW valid (another CPU wrote it)
     *
     * If the PTE is already valid, another CPU resolved this fault.
     * We need to clean up the page we allocated and return success.
     *
     * For user addresses, read the PTE via KSEG0 (walking TTBR0 directly)
     * instead of through the self-map, which can be stale (Bugs #42/#43).
     */
    {
        BOOLEAN PteAlreadyValid = FALSE;
        __asm__ __volatile__("dmb ish" ::: "memory");
        if (Address < MmSystemRangeStart)
        {
            PMMPTE Kseg0Pte = MiArm64UserPteKseg0(Address);
            PteAlreadyValid = (Kseg0Pte != NULL && Kseg0Pte->u.Hard.Valid == 1);
        }
        else
        {
            PteAlreadyValid = (PointerPte->u.Hard.Valid == 1);
        }
        if (PteAlreadyValid)
        {
            /* PTE already valid - another CPU resolved this fault */
            if (!BootstrapAllocation)
            {
                PMMPFN Pfn1 = MI_PFN_ELEMENT(PageFrameNumber);
                /* Remove the PFN from use - mark it as deleted so it can be freed */
                MI_SET_PFN_DELETED(Pfn1);
                MiDecrementShareCount(Pfn1, PageFrameNumber);
            }
            return STATUS_SUCCESS;
        }
    }

    /*
     * ARM64: Write user PTEs via KSEG0 to directly modify TTBR0 page tables.
     * This avoids the TTBR1 self-map entirely and its stale-alias issues.
     */
    if (MI_IS_PAGE_TABLE_ADDRESS(PointerPte) && Address < MmSystemRangeStart)
        MiArm64WriteValidUserPte(Address, TempPte);
    else
#endif
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

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

    /* Get the PFN entry and set it as a prototype PTE */
    Pfn1 = MiGetPfnEntry(PageFrameIndex);
    Pfn1->u3.e1.PrototypePte = 1;

    /* Increment the share count for the page table */
    PageTablePte = MiAddressToPte(PointerPte);

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64: Get the L3 page table PFN for user addresses via self-map.
     * The merged PXE ensures MiAddressToPde(Address) resolves correctly
     * for both user and kernel addresses.
     */
    if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
    {
        PFN_NUMBER L3Pfn;
        (void)MiArm64UserPteKseg0ForPfn(Address, &L3Pfn);
        if (L3Pfn == 0)
        {
            DPRINT1("[pagfault] MiCompleteProtoPteFault: L3 walk failed for VA=%p!\n",
                    Address);
            MiReleasePfnLock(OldIrql);
            return STATUS_INTERNAL_ERROR;
        }
        Pfn2 = MiGetPfnEntry(L3Pfn);
    }
    else
#endif
    {
        if (!PageTablePte->u.Hard.Valid)
        {
            DPRINT1("[pagfault] MiCompleteProtoPteFault: PageTablePte is INVALID!\n");
            DPRINT1("[pagfault]   PointerPte=%p PageTablePte=%p PageTablePte->u.Long=0x%llx\n",
                    PointerPte, PageTablePte, (unsigned long long)PageTablePte->u.Long);
            MiReleasePfnLock(OldIrql);
            return STATUS_INTERNAL_ERROR;
        }
        Pfn2 = MiGetPfnEntry(PageTablePte->u.Hard.PageFrameNumber);
    }
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

    /*
     * RACE CONDITION FIX: After releasing the PFN lock, another CPU may have
     * already completed the same prototype PTE fault. Check if the actual PTE
     * is already valid before attempting to write it.
     *
     * This race can occur because:
     * 1. CPU A resolves demand-zero prototype PTE, releases PFN lock
     * 2. CPU B faults on same VA, sees prototype PTE is valid
     * 3. CPU B completes its MiCompleteProtoPteFault, writes actual PTE
     * 4. CPU A reaches here, but PTE is already valid
     *
     * If the PTE is already valid with the correct PFN, another CPU already
     * handled this fault. We can safely return success - the share count
     * increments we did are correct (we're just another reference).
     *
     * NOTE: On ARM64, we must ensure memory ordering when reading the PTE
     * to see updates from other CPUs. Use a data memory barrier.
     * For user addresses, read via KSEG0 to avoid stale self-map (Bugs #42/#43).
     */
#if defined(_M_ARM64) || defined(__aarch64__)
    __asm__ __volatile__("dmb ish" ::: "memory");
    {
        BOOLEAN PteAlreadyValid = FALSE;
        PFN_NUMBER ExistingPfn = 0;
        if (Address < MmSystemRangeStart)
        {
            PMMPTE Kseg0Pte = MiArm64UserPteKseg0(Address);
            if (Kseg0Pte != NULL && Kseg0Pte->u.Hard.Valid == 1)
            {
                PteAlreadyValid = TRUE;
                ExistingPfn = PFN_FROM_PTE(Kseg0Pte);
            }
        }
        else
        {
            if (PointerPte->u.Hard.Valid == 1)
            {
                PteAlreadyValid = TRUE;
                ExistingPfn = PFN_FROM_PTE(PointerPte);
            }
        }
        if (PteAlreadyValid)
        {
            if (ExistingPfn == PageFrameIndex)
            {
                /* Same page, fault already handled - return success */
                return STATUS_SUCCESS;
            }
            /*
             * Different page frame - this shouldn't happen in normal operation.
             * It might indicate a serious bug, but we'll let it proceed and
             * the subsequent assertion in MI_WRITE_VALID_PTE will catch it.
             */
        }
    }
#else
    if (PointerPte->u.Hard.Valid == 1)
    {
        /*
         * PTE is already valid - another CPU completed the fault.
         * Verify it points to the same page frame we resolved.
         * The share count increments we did above are still valid since
         * the page is now mapped at this VA (by us or another CPU).
         */
        if (PFN_FROM_PTE(PointerPte) == PageFrameIndex)
        {
            /* Same page, fault already handled - return success */
            return STATUS_SUCCESS;
        }
        /*
         * Different page frame - this shouldn't happen in normal operation.
         * It might indicate a serious bug, but we'll let it proceed and
         * the subsequent assertion in MI_WRITE_VALID_PTE will catch it.
         */
    }
#endif

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

#if defined(_M_ARM64) || defined(__aarch64__)
    /* ARM64: Write user PTEs via KSEG0 to bypass self-map */
    if (Address < MmSystemRangeStart)
        MiArm64WriteValidUserPte(Address, TempPte);
    else
#endif
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 CRITICAL: TLB and D-cache Invalidation After PTE Update
     *
     * CORRECT ORDER (cache ops AFTER PTE is valid):
     * 1. Write PTE (already done above)
     * 2. DSB ISHST: Ensure PTE write is visible to table walkers
     * 3. TLBI VAE1IS: Invalidate TLB entry (now table walkers see new PTE)
     * 4. DSB ISH + ISB: Wait for TLB invalidation
     * 5. DC IVAC: Now D-cache invalidation uses the CORRECT PA from new PTE
     * 6. DSB ISH: Wait for cache operations
     *
     * Use DC IVAC (Invalidate only), NOT DC CIVAC:
     * - This is incoming data (mapped from ramdisk/disk/prototype)
     * - We want to discard stale cache, not write garbage back to RAM
     * - DC CIVAC would corrupt data by writing back uninitialized cache lines
     */
    {
        ULONG64 Ctr;
        ULONG DcacheLineSize;
        ULONG_PTR Va;

        /* STEP 1: TLB invalidation FIRST (before D-cache invalidation) */
        ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
        __asm__ __volatile__(
            "dsb ishst\n\t"           /* Ensure PTE write is visible to table walkers */
            "tlbi vae1is, %0\n\t"     /* Invalidate TLB for this VA */
            "dsb ish\n\t"             /* Wait for TLB invalidation to complete */
            "isb"                      /* Ensure CPU sees new translations */
            : : "r"(VaForTlbi) : "memory");

        /* STEP 2: Now D-cache invalidation (with correct TLB entry) */
        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
        DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);
        Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

        /* Use DC IVAC for incoming data - discards stale cache without writeback */
        for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
        {
            __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
        }

        __asm__ __volatile__("dsb ish" ::: "memory");
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

#if defined(_M_ARM64) || defined(__aarch64__)
    /* ARM64: Write user PTEs via KSEG0 to bypass self-map */
    if (MI_IS_PAGE_TABLE_ADDRESS(PointerPte) && FaultingAddress < MmSystemRangeStart)
        MiArm64WriteValidUserPte(FaultingAddress, TempPte);
    else
#endif
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64: TLB and D-cache invalidation after PTE write.
     *
     * After reading page from pagefile and installing the PTE, we must
     * invalidate TLB FIRST, then D-cache. See MiCompleteProtoPteFault for
     * the full explanation of why this order is critical (Cycle 56 fix).
     */
    {
        ULONG64 Ctr;
        ULONG DcacheLineSize;
        ULONG_PTR Va;

        /* STEP 1: TLB invalidation FIRST */
        ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)FaultingAddress) >> 12;
        __asm__ __volatile__(
            "dsb ishst\n\t"           /* Ensure PTE write is visible */
            "tlbi vae1is, %0\n\t"     /* Invalidate TLB for this VA */
            "dsb ish\n\t"             /* Wait for TLB invalidation */
            "isb"                      /* Ensure CPU sees new translations */
            : : "r"(VaForTlbi) : "memory");

        /* STEP 2: D-cache invalidation (now with correct TLB entry) */
        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
        DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

        Va = (ULONG_PTR)FaultingAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);

        /* Use DC IVAC for incoming data (read from page file) */
        for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
        {
            __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
        }

        __asm__ __volatile__("dsb ish" ::: "memory");
    }
#endif

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
#if defined(_M_ARM64) || defined(__aarch64__)
    /* ARM64: Write user PTEs via KSEG0 to bypass self-map */
    if (MI_IS_PAGE_TABLE_ADDRESS(PointerPte) && FaultingAddress < MmSystemRangeStart)
        MiArm64WriteValidUserPte(FaultingAddress, TempPte);
    else
#endif
    MI_WRITE_VALID_PTE(PointerPte, TempPte);

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64: TLB invalidation after PTE write for transition faults.
     *
     * Transition faults bring a page from standby/modified list back to
     * active state. The physical page already has correct data.
     *
     * Only TLB invalidation is needed here — ARM64 D-caches are PIPT
     * (Physically Indexed, Physically Tagged), so cache lines are keyed
     * by physical address. The physical page data is already correct in
     * cache from its prior use. No D-cache invalidation required.
     *
     * Note: DC IVAC on a user VA at DISPATCH_LEVEL would fault because
     * the VA translation fails at elevated IRQL.
     */
    {
        ULONG64 VaForTlbi;

        VaForTlbi = ((ULONG64)(ULONG_PTR)FaultingAddress) >> 12;
        __asm__ __volatile__(
            "dsb ishst\n\t"
            "tlbi vae1is, %0\n\t"
            "dsb ish\n\t"
            "isb"
            : : "r"(VaForTlbi) : "memory");
    }
#endif

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
        /*
         * Prototype PTE already valid -- another process (or this process
         * via a different VAD) already resolved the demand-zero or brought
         * the page in from disk.  We are adding one more process PTE that
         * maps this physical page.
         *
         * NT PFN model for prototype pages:
         *   ShareCount  = number of process PTEs currently mapping the page.
         *   ReferenceCount = 1 while the page is Active (set when first
         *                    faulted in).  It stays at 1 regardless of how
         *                    many processes share the page.
         *
         * Only ShareCount is incremented here.  ReferenceCount is NOT
         * bumped because MiDecrementShareCount (called from MiDeletePte
         * during unmap) does not decrement ReferenceCount -- it only
         * transitions the prototype PTE to the Transition state when
         * ShareCount drops to 0.  Incrementing ReferenceCount here would
         * cause it to grow unbounded and leak pages.
         *
         * Contrast with the transition case (standby page) below where
         * BOTH counts are incremented because the page is being pulled
         * off a list (ReferenceCount was 0).
         */
        PageFrameIndex = PFN_FROM_PTE(&TempPte);

        Pfn1 = MiGetPfnEntry(PageFrameIndex);
        ASSERT(Pfn1->u3.e2.ReferenceCount >= 1);
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

#if defined(_M_ARM64) || defined(__aarch64__)
        /*
         * ARM64 CRITICAL: COW Page Fault Cache Handling
         *
         * Copy-On-Write sequence:
         * 1. MiCopyPfn copies original page to new private page (via kernel addresses)
         * 2. New PTE points to the new private copy
         * 3. D-cache may have stale lines for this VA pointing to OLD PA
         *
         * CORRECT ORDER (cache ops AFTER PTE is valid):
         * 1. Write new PTE (now VA maps to new PA)
         * 2. DSB ISHST (ensure PTE visible)
         * 3. TLBI (invalidate old TLB entry)
         * 4. DSB ISH + ISB (TLB sees new mapping)
         * 5. DC IVAC (invalidate stale cache - now targets correct PA!)
         *
         * Use DC IVAC (Invalidate only):
         * - The new page has correct data (copied by MiCopyPfn)
         * - We want to discard stale cache lines (old PA's data)
         * - DC CIVAC would write back stale data to the WRONG page!
         */
        /* Cache invalidation moved to AFTER PTE write - see below */
#endif

        /* Write the valid PTE first */
#if defined(_M_ARM64) || defined(__aarch64__)
        /* ARM64: Write user PTEs via KSEG0 to bypass self-map */
        if (Address < MmSystemRangeStart)
            MiArm64WriteValidUserPte(Address, PteContents);
        else
#endif
        MI_WRITE_VALID_PTE(PointerPte, PteContents);

#if defined(_M_ARM64) || defined(__aarch64__)
        /* D-cache invalidation for COW page (incoming data from MiCopyPfn) */
        {
            ULONG64 Ctr;
            ULONG DcacheLineSize;
            ULONG_PTR Va;

            __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
            DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);
            Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

            /* DC IVAC for incoming data - discard stale cache without writeback */
            for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
            {
                __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
            }

            __asm__ __volatile__("dsb ish" ::: "memory");
        }
#endif

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

    /*
     * Complete the prototype PTE fault -- this will release the PFN lock.
     *
     * NOTE: We previously asserted PointerPte->u.Hard.Valid == 0 here, but
     * this assertion can fail on SMP systems due to a race condition:
     * Another CPU may have already completed the same fault while we were
     * resolving the prototype PTE. MiCompleteProtoPteFault now handles this
     * race gracefully by checking PTE validity after releasing the PFN lock.
     */
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
                    /*
                     * Prototype PTE already valid -- only ShareCount is
                     * bumped.  See the identical case in MiResolveProtoPteFault
                     * for the full rationale on why ReferenceCount is NOT
                     * incremented here.
                     */
                    PageFrameIndex = PFN_FROM_PTE(&TempPte);
                    Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);
                    ASSERT(Pfn1->u3.e2.ReferenceCount >= 1);
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

                    /*
                     * Write the prototype PTE.
                     *
                     * Prototype PTEs live in paged pool, not in a real page
                     * table.  MI_WRITE_VALID_PTE contains ARM64-specific
                     * TLB invalidation logic that derives a virtual address
                     * from the PTE pointer via MiPteToAddress().  For a
                     * prototype PTE this yields a garbage VA, so the TLBI
                     * would evict an unrelated TLB entry.
                     *
                     * Use a direct store with a compiler barrier instead.
                     * The PFN lock serialises concurrent access to the
                     * prototype PTE, and a DSB will be issued later when
                     * the real page-table PTE is written via
                     * MI_WRITE_VALID_PTE in MiCompleteProtoPteFault.
                     */
#if defined(_M_ARM64) || defined(__aarch64__)
                    ASSERT(TempPte.u.Hard.Valid == 1);
                    *PointerProtoPte = TempPte;
                    __asm__ __volatile__("" ::: "memory"); /* compiler barrier */
#else
                    MI_WRITE_VALID_PTE(PointerProtoPte, TempPte);
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
                    if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
                    {
                        /* User PTE must still be invalid via KSEG0 before mapping */
                        PMMPTE _KsegPte = MiArm64UserPteKseg0(Address);
                        ASSERT(_KsegPte == NULL || _KsegPte->u.Hard.Valid == 0);
                    }
                    else
#endif
                    {
                        ASSERT(PointerPte->u.Hard.Valid == 0);
                    }
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
#if defined(_M_ARM64) || defined(__aarch64__)
            if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
            {
                /* User PTE must still be invalid via KSEG0 before resolution */
                PMMPTE _KsegPte = MiArm64UserPteKseg0(Address);
                ASSERT(_KsegPte == NULL || _KsegPte->u.Hard.Valid == 0);
            }
            else
#endif
            {
                ASSERT(PointerPte->u.Hard.Valid == 0);
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
    PMMPTE PointerPte;
    PMMPDE PointerPde;
#if (_MI_PAGING_LEVELS >= 3)
    PMMPDE PointerPpe;
#if (_MI_PAGING_LEVELS == 4)
    PMMPDE PointerPxe;
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
     * ARM64 EARLY BOOT FAULT HANDLING:
     *
     * During early boot (before MmArmInitSystem / MiScanMemoryDescriptors),
     * the self-map infrastructure cannot function because the merged PXE
     * page hasn't been set up yet. Accessing self-map VAs (MiAddressToPte etc.)
     * would cause recursive page faults → stack overflow.
     *
     * Handle kernel address faults directly via KSEG0 page table walk,
     * bypassing the self-map entirely. This resolves Access Flag faults
     * by setting AF on the hardware PTE through the KSEG0 physical mapping.
     *
     * CRITICAL: This must run BEFORE any MiAddressToPte/Pde/Ppe/Pxe calls.
     */
    {
        NTSTATUS EarlyStatus = MiArm64HandleEarlyBootFault(Address, TrapInformation);
        if (EarlyStatus != STATUS_MORE_PROCESSING_REQUIRED)
        {
            return EarlyStatus;
        }
    }
#endif

    /*
     * ARM64: With the recursive self-map through the merged PXE page,
     * MiAddressToPte/Pde/Ppe/Pxe now works correctly for BOTH user and
     * kernel addresses. The merged page combines TTBR0[0-255] and
     * TTBR1[256-511] entries, and merged[493] is self-referential.
     *
     * IMPORTANT: These must be computed AFTER the early boot handler above,
     * because the self-map pages are not accessible during early boot.
     */
    PointerPte = MiAddressToPte(Address);
    PointerPde = MiAddressToPde(Address);
#if (_MI_PAGING_LEVELS >= 3)
    PointerPpe = MiAddressToPpe(Address);
#if (_MI_PAGING_LEVELS == 4)
    PointerPxe = MiAddressToPxe(Address);
#endif
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64: Reject faults on L0[494] (old TTBR0 alias, no longer used).
     * The merged PXE page at L0[493] now covers both address spaces.
     */
    if (((ULONG_PTR)Address >= (ULONG_PTR)PTE_BASE + (1ULL << 39)) &&
        ((ULONG_PTR)Address < (ULONG_PTR)PTE_BASE + (2ULL << 39)))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    /*
     * ARM64 SELF-MAP VALIDATION (Debug builds only):
     *
     * Cross-check the recursive self-map (via merged PXE page) against
     * direct KSEG0 physical page table walks. This validates that
     * MiAddressToPte/Pde() returns correct results for both user and
     * kernel addresses now that MiArm64MapAliasForPointer is removed.
     *
     * For user addresses: compare self-map PTE content against KSEG0 walk.
     * For kernel addresses: verify PXE self-referential entry is intact.
     */
#if DBG
    {
        /* Verify merged PXE page self-referential entry */
        PMMPTE SelfPxe = MiAddressToPxe((PVOID)PTE_BASE);
        if (SelfPxe->u.Hard.Valid)
        {
            /* PXE_BASE[493] should point to the merged page itself.
             * Verify by checking that the PXE for PTE_BASE resolves
             * to the same PFN as the PXE for PDE_BASE (both go through
             * the self-referential entry). */
            PMMPTE PdePxe = MiAddressToPxe((PVOID)PDE_BASE);
            ASSERT(PFN_FROM_PTE(SelfPxe) == PFN_FROM_PTE(PdePxe));
        }

        /* Cross-check self-map vs KSEG0 for user addresses with existing page tables. */
        if (Address < MmSystemRangeStart)
        {
            /* Verify merged PXE page has correct TTBR0 L0 entry for this VA's L0 index. */
            {
                ULONG UserL0Idx = ((ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
                PMMPTE MergedEntry = MiAddressToPxe(Address);
                ULONG64 Ttbr0Val;
                __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Val));
                volatile ULONG64 *Ttbr0L0 = (volatile ULONG64 *)(KSEG0_BASE |
                    (Ttbr0Val & 0x0000FFFFFFFFF000ULL));
                ULONG64 RealL0Entry = Ttbr0L0[UserL0Idx];
                /* Mask AF: hardware HAFDBS may set AF on merged copy while
                 * original TTBR0 L0 is untouched. */
                ASSERT((MergedEntry->u.Long & ~(1ULL << 10)) == (RealL0Entry & ~(1ULL << 10)));
            }

            PMMPTE KsegPte = MiArm64UserPteKseg0(Address);
            if (KsegPte != NULL)
            {
                PMMPTE SelfMapPte = MiAddressToPte(Address);
                ASSERT(SelfMapPte->u.Long == KsegPte->u.Long);
            }

            /* Also cross-check PDE level */
            PMMPTE KsegPde = MiArm64UserPdeKseg0(Address);
            if (KsegPde != NULL)
            {
                PMMPTE SelfMapPde = MiAddressToPde(Address);
                ASSERT(SelfMapPde->u.Long == KsegPde->u.Long);
            }
        }
    }
#endif /* DBG */
#endif /* _M_ARM64 */

    /*
     * ARM64 software Access Flag handling (TCR.HA=0):
     * Some CPUs (cpu=host) do not support hardware AF updates, so valid entries
     * with AF=0 raise an Access Flag fault. Detect that case via ESR and set AF.
     *
     * NOTE: TrapInformation may be a dummy value like (PVOID)1 when called from
     * MiMakeSystemAddressValid() to indicate "not an MDL probe". We must validate
     * that TrapInformation is actually a valid kernel pointer before dereferencing.
     */
#if defined(_M_ARM64) || defined(__aarch64__)
    if (TrapInformation != NULL &&
        (ULONG_PTR)TrapInformation >= (ULONG_PTR)MmSystemRangeStart)
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
                /*
                 * With TCR.HA=1 enabled, hardware manages the Access Flag automatically,
                 * eliminating AF-related faults and recursion. The deep recursion
                 * workaround (KSEG0 path) is no longer needed.
                 */
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
#endif /* _M_ARM64 */

    /* Check for page fault on high IRQL */
    if (OldIrql > APC_LEVEL)
    {
#if (_MI_PAGING_LEVELS < 3)
        /* Could be a page table for paged pool, which we'll allow */
        if (MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address)) MiSynchronizeSystemPde((PMMPDE)PointerPte);
        MiCheckPdeForPagedPool(Address);
#endif
        /* Check if any of the top-level pages are invalid */
        BOOLEAN PtesInvalid = FALSE;
#if defined(_M_ARM64) || defined(__aarch64__)
        /*
         * ARM64: User-space faults at IRQL > APC_LEVEL cannot be resolved
         * (would need working set lock). Mark as invalid immediately.
         */
        if (Address < MmSystemRangeStart)
        {
            PtesInvalid = TRUE;
        }
        else
#endif
        {
            PtesInvalid = (
#if (_MI_PAGING_LEVELS == 4)
                (PointerPxe->u.Hard.Valid == 0) ||
#endif
#if (_MI_PAGING_LEVELS >= 3)
                (PointerPpe->u.Hard.Valid == 0) ||
#endif
                (PointerPde->u.Hard.Valid == 0)
#if defined(_M_ARM64) || defined(__aarch64__)
                /*
                 * ARM64: When the PDE is a 2MB block descriptor, there is no
                 * L3 page table. PointerPte points into the self-map for a
                 * non-existent L3 and contains garbage. Skip the PTE validity
                 * check - the address is fully mapped by the 2MB block.
                 */
                || (!MI_IS_PAGE_LARGE(PointerPde) && (PointerPte->u.Hard.Valid == 0))
#else
                || (PointerPte->u.Hard.Valid == 0)
#endif
                );
        }
        if (PtesInvalid)
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

        /*
         * ARM64 Large Page (2MB Block Descriptor) Handling at High IRQL
         *
         * On ARM64, the bootloader (FreeLDR) maps some kernel memory regions
         * using 2MB block descriptors at L2 (PDE level). When a page fault
         * occurs at high IRQL on an address within such a region:
         *
         * - PointerPde is a valid 2MB block descriptor (NotLargePage == 0)
         * - There is NO L3 page table, so PointerPte is meaningless
         * - The address IS fully mapped by the 2MB block
         * - The fault is typically an access flag fault (AF bit not set)
         *   or a condition that resolves on retry
         *
         * We handle this by:
         * 1. Setting the AF bit on the block descriptor if not already set
         * 2. Returning SUCCESS - the address is mapped, the fault will not recur
         *
         * We must NOT dereference PointerPte when the PDE is a large page,
         * as it does not point to a valid L3 entry.
         */
#if defined(_M_ARM64) || defined(__aarch64__)
        if (MI_IS_PAGE_LARGE(PointerPde))
        {
            /* The PDE is a 2MB block descriptor - address is already mapped */
            if (PointerPde->u.Hard.Valid)
            {
                /* Set Access Flag if not already set */
                if ((PointerPde->u.Long & PTE_ACCESSED) == 0)
                {
                    MMPTE NewPde;
                    NewPde.u.Long = PointerPde->u.Long | PTE_ACCESSED;
                    MI_UPDATE_VALID_PTE((PMMPTE)PointerPde, NewPde);
                }

                /* Write fault to a read-only large page is fatal */
                if (MI_IS_WRITE_ACCESS(FaultCode) &&
                    !MI_IS_PAGE_WRITEABLE((PMMPTE)PointerPde))
                {
                    KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                                 (ULONG_PTR)Address,
                                 PointerPde->u.Long,
                                 (ULONG_PTR)TrapInformation,
                                 11);
                }

                DPRINT("ARM64: Large page fault at high IRQL resolved for %p (PDE=0x%llx)\n",
                       Address, (unsigned long long)PointerPde->u.Long);
                return STATUS_SUCCESS;
            }
        }
#else
        /* Large page support is not yet implemented in ReactOS for non-ARM64 */
        ASSERT(MI_IS_PAGE_LARGE(PointerPde) == FALSE);
#endif
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

        /* Nothing is actually wrong */
        DPRINT("Fault at IRQL %u is ok (%p)\n", OldIrql, Address);
        return STATUS_SUCCESS;
    }

    /* Check for kernel fault address */
    if (Address >= MmSystemRangeStart)
    {
        /* Bail out, if the fault came from user mode */
        if (Mode == UserMode)
        {
            return STATUS_ACCESS_VIOLATION;
        }

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
            /* PXE/PPE/PDE (still) not valid, kill the system */
            KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                         (ULONG_PTR)Address,
                         FaultCode,
                         (ULONG_PTR)TrapInformation,
                         2);
        }

        /* Not handling session faults yet */
        IsSessionAddress = MI_IS_SESSION_ADDRESS(Address);

#if defined(_M_ARM64) || defined(__aarch64__)
        /*
         * ARM64 Large Page Handling for Kernel Address Faults
         *
         * When the PDE is a 2MB block descriptor, there is no L3 page table
         * and PointerPte is meaningless (it points into the self-map for a
         * non-existent L3). The address is fully mapped by the 2MB block.
         *
         * Handle this by setting the AF bit if needed and returning SUCCESS.
         * We must NOT fall through to code that dereferences PointerPte.
         */
        if (MI_IS_PAGE_LARGE(PointerPde) && PointerPde->u.Hard.Valid)
        {
            /* Set Access Flag if not already set */
            if ((PointerPde->u.Long & PTE_ACCESSED) == 0)
            {
                MMPTE NewPde;
                NewPde.u.Long = PointerPde->u.Long | PTE_ACCESSED;
                MI_UPDATE_VALID_PTE((PMMPTE)PointerPde, NewPde);
            }

            /* Write fault to a read-only large page is fatal */
            if (MI_IS_WRITE_ACCESS(FaultCode) &&
                !MI_IS_PAGE_WRITEABLE((PMMPTE)PointerPde))
            {
                KeBugCheckEx(ATTEMPTED_WRITE_TO_READONLY_MEMORY,
                             (ULONG_PTR)Address,
                             PointerPde->u.Long,
                             (ULONG_PTR)TrapInformation,
                             12);
            }

            /* Execution of non-executable large page is fatal */
            if (MI_IS_INSTRUCTION_FETCH(FaultCode) &&
                !MI_IS_PAGE_EXECUTABLE((PMMPTE)PointerPde))
            {
                KeBugCheckEx(ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY,
                             (ULONG_PTR)Address,
                             (ULONG_PTR)PointerPde->u.Long,
                             (ULONG_PTR)TrapInformation,
                             2);
            }

            DPRINT("ARM64: Large page kernel fault resolved for %p (PDE=0x%llx)\n",
                   Address, (unsigned long long)PointerPde->u.Long);
            return STATUS_SUCCESS;
        }
#endif

        /* The PDE is valid, so read the PTE */
        TempPte = *PointerPte;

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
                 * - They're resolved on-demand via the recursive self-map
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
#if defined(_M_ARM64)
            /*
             * ARM64: On-demand PDE creation for kernel PTE self-map faults.
             *
             * On ARM64, kernel PDEs (L2 entries) are NOT all pre-created during init
             * (unlike AMD64's MiInitializePageTable). When code accesses a PTE through
             * the self-map (e.g., MiAddressToPte(SystemCacheAddr)), the hardware walk
             * may encounter a zero L2 entry, causing a fault at the PTE self-map address.
             *
             * Handle this by creating the missing PDE on demand: allocate an L3 page
             * table page and write the PDE entry. This is the ARM64 equivalent of
             * MiFillSystemPageDirectory / MiCheckPdeForPagedPool.
             */
            if ((ULONG_PTR)Address >= PTE_BASE && (ULONG_PTR)Address <= PTE_TOP)
            {
                PVOID OrigAddress = MiPteToAddress((PMMPTE)PAGE_ALIGN(Address));
                if ((ULONG_PTR)OrigAddress >= (ULONG_PTR)MmSystemRangeStart)
                {
                    extern MMPDE ValidKernelPde;
                    MMPDE TempEntry;
                    KIRQL PfnIrql;
                    PFN_NUMBER PageFrameIndex, ParentPage;
                    BOOLEAN Created = FALSE;

                    /*
                     * ARM64: On-demand kernel page table creation for ALL levels.
                     *
                     * Unlike AMD64 where MiInitializePageTable pre-creates all kernel
                     * PDEs during boot, ARM64 may have unpopulated PXE (L0), PPE (L1),
                     * or PDE (L2) entries for kernel regions first accessed at runtime
                     * (e.g., system cache at 0xFFFFF98000000000).
                     *
                     * Key insight: MiAddressToPxe() always works because the self-map
                     * walk (L0[493]^4) only traverses L0[493] entries, which are always
                     * valid. This lets us safely read/write PXE entries.
                     *
                     * After creating PXE, MiAddressToPpe() works (goes through L0[493]^3
                     * then the new PXE). After PPE, MiAddressToPde() works similarly.
                     */

                    /* Level 0 (PXE): MiAddressToPxe always works via self-map^4 */
                    PMMPTE OrigPxe = MiAddressToPxe(OrigAddress);
                    if (!OrigPxe->u.Hard.Valid)
                    {
                        TempEntry = ValidKernelPde;
                        PfnIrql = MiAcquirePfnLock();
                        MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
                        MI_SET_PROCESS2("Kernel");
                        PageFrameIndex = MiRemoveZeroPage(MI_GET_NEXT_COLOR());
                        if (PageFrameIndex)
                        {
                            TempEntry.u.Hard.PageFrameNumber = PageFrameIndex;
                            ParentPage = MiAddressToPxe((PVOID)PTE_BASE)->u.Hard.PageFrameNumber;
                            MiInitializePfnForOtherProcess(PageFrameIndex,
                                                           (PMMPTE)OrigPxe,
                                                           ParentPage);
                            MI_WRITE_VALID_PDE(OrigPxe, TempEntry);
                            __asm__ __volatile__("dsb ishst" ::: "memory");
                            __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                            __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
                            Created = TRUE;
                            DPRINT1("[arm64] On-demand PXE created for %p (L0[%lu])\n",
                                    OrigAddress, (ULONG)(((ULONG_PTR)OrigAddress >> 39) & 0x1FF));
                        }
                        MiReleasePfnLock(PfnIrql);
                        if (Created)
                            return STATUS_SUCCESS;
                    }

                    /* Level 1 (PPE): safe now since PXE is valid */
                    PMMPTE OrigPpe = MiAddressToPpe(OrigAddress);
                    if (!OrigPpe->u.Hard.Valid)
                    {
                        TempEntry = ValidKernelPde;
                        PfnIrql = MiAcquirePfnLock();
                        MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
                        MI_SET_PROCESS2("Kernel");
                        PageFrameIndex = MiRemoveZeroPage(MI_GET_NEXT_COLOR());
                        if (PageFrameIndex)
                        {
                            TempEntry.u.Hard.PageFrameNumber = PageFrameIndex;
                            ParentPage = OrigPxe->u.Hard.PageFrameNumber;
                            MiInitializePfnForOtherProcess(PageFrameIndex,
                                                           (PMMPTE)OrigPpe,
                                                           ParentPage);
                            MI_WRITE_VALID_PDE(OrigPpe, TempEntry);
                            __asm__ __volatile__("dsb ishst" ::: "memory");
                            __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                            __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
                            Created = TRUE;
                            DPRINT1("[arm64] On-demand PPE created for %p\n", OrigAddress);
                        }
                        MiReleasePfnLock(PfnIrql);
                        if (Created)
                            return STATUS_SUCCESS;
                    }

                    /* Level 2 (PDE): safe now since PPE is valid */
                    PMMPDE OrigPde = MiAddressToPde(OrigAddress);
                    if (!OrigPde->u.Hard.Valid)
                    {
                        if (OrigPpe->u.Hard.Valid)
                        {
                            TempEntry = ValidKernelPde;
                            PfnIrql = MiAcquirePfnLock();

                            MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
                            MI_SET_PROCESS2("Kernel");
                            PageFrameIndex = MiRemoveZeroPage(MI_GET_NEXT_COLOR());
                            if (PageFrameIndex)
                            {
                                TempEntry.u.Hard.PageFrameNumber = PageFrameIndex;
                                ParentPage = OrigPpe->u.Hard.PageFrameNumber;
                                MiInitializePfnForOtherProcess(PageFrameIndex,
                                                               (PMMPTE)OrigPde,
                                                               ParentPage);
                                MI_WRITE_VALID_PDE(OrigPde, TempEntry);

                                {
                                    ULONG64 VaForTlbi = ((ULONG64)(ULONG_PTR)Address) >> 12;
                                    __asm__ __volatile__(
                                        "dsb ishst\n\t"
                                        "tlbi vae1is, %0\n\t"
                                        "dsb ish\n\t"
                                        "isb"
                                        : : "r"(VaForTlbi) : "memory");
                                }
                                Created = TRUE;
                                DPRINT1("[arm64] On-demand PDE created for %p\n", OrigAddress);
                            }

                            MiReleasePfnLock(PfnIrql);

                            if (Created)
                            {
                                return STATUS_SUCCESS;
                            }
                        }
                    }
                }
            }
#endif
#if (_MI_PAGING_LEVELS < 3)
            /* Windows does this check but I don't understand why -- it's done above! */
            ASSERT(MiCheckPdeForPagedPool(Address) != STATUS_WAIT_1);
#endif
            /* Handle this as a user mode fault */
            goto UserFault;
        }

        /* Get the current thread */
        CurrentThread = PsGetCurrentThread();

        /* What kind of address is this */
        if (!IsSessionAddress)
        {
            /* Use the system working set */
            WorkingSet = &MmSystemCacheWs;
            CurrentProcess = NULL;

            /* Make sure we don't have a recursive working set lock */
            if ((CurrentThread->OwnsProcessWorkingSetExclusive) ||
                (CurrentThread->OwnsProcessWorkingSetShared) ||
                (CurrentThread->OwnsSystemWorkingSetExclusive) ||
                (CurrentThread->OwnsSystemWorkingSetShared) ||
                (CurrentThread->OwnsSessionWorkingSetExclusive) ||
                (CurrentThread->OwnsSessionWorkingSetShared))
            {
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
        /* Acquire the working set lock */
        KeRaiseIrql(APC_LEVEL, &LockIrql);
        MiLockWorkingSet(CurrentThread, WorkingSet);

        /* Re-read PTE now that we own the lock */
        TempPte = *PointerPte;
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

        /* Check one kind of prototype PTE */
        if (TempPte.u.Soft.Prototype)
        {
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
                /* Bugcheck - this address has no protection */
                KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                             (ULONG_PTR)Address,
                             FaultCode,
                             (ULONG_PTR)TrapInformation,
                             0);
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
        MiUnlockWorkingSet(CurrentThread, WorkingSet);
        KeLowerIrql(LockIrql);

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
#if defined(_M_ARM64) || defined(__aarch64__)
    /*
     * ARM64 USER FAULT HANDLING
     *
     * On ARM64, user page tables live in TTBR0, separate from the kernel's TTBR1.
     * Writing to user PTE entries through the self-map only updates the merged
     * PXE page, NOT the actual TTBR0 page table root. Therefore, user page fault
     * handling must use KSEG0-based page table access.
     *
     * The fault handling strategy:
     * - SharedUserData: special shared-page mapping via KSEG0
     * - ARM3 committed private VADs (TEB, PEB, stack): demand-zero via KSEG0
     * - Section views (ntdll, mapped files): handled by RosMM's MmNotPresentFault
     *
     * All user PTE writes go through MmCreateVirtualMappingUnsafe which correctly
     * modifies the actual TTBR0 page table hierarchy via KSEG0.
     */
    if (Address < MmSystemRangeStart)
    {
        /*
         * SharedUserData: map the kernel's shared page read-only into user space.
         * This page shares the same physical frame as KI_USER_SHARED_DATA.
         */
        if (PAGE_ALIGN(Address) == (PVOID)MM_SHARED_USER_DATA_VA)
        {
            extern PMMPTE MmSharedUserDataPte;
            MMPTE SharedPte = *MmSharedUserDataPte;
            PFN_NUMBER SharedPfn;

            if (SharedPte.u.Hard.Valid == 0)
                return STATUS_ACCESS_VIOLATION;

            SharedPfn = PFN_FROM_PTE(&SharedPte);

            /* Map via KSEG0 (MmCreatePhysicalMapping handles page table creation) */
            return MmCreatePhysicalMapping(
                PsGetCurrentProcess(), PAGE_ALIGN(Address),
                PAGE_READONLY, SharedPfn);
        }

        /*
         * Check ARM3 VAD tree for committed private memory (TEB, PEB, stack, etc.)
         *
         * ARM3 creates VADs via MiInsertVadEx but does NOT create RosMM memory
         * areas. MmNotPresentFault only searches the memory area tree, so it
         * returns STATUS_ACCESS_VIOLATION for ARM3-only VADs.
         *
         * We use MiLocateAddress directly (NOT MiCheckVirtualAddress) to avoid
         * triggering assertions for VadImageMap types. Only handle private
         * committed memory here; everything else falls through to RosMM.
         */
        {
            PETHREAD CurThread = PsGetCurrentThread();
            PEPROCESS CurProcess;
            PEPROCESS ActiveProcess;
            ULONG ProtCode = MM_INVALID_PROTECTION;
            PMMVAD Vad = NULL;
            PMMPTE SectionProtoPte = NULL;
            ULONG SectionProtCode = 0;
            BOOLEAN StackExpandFault = FALSE;

            CurProcess = MiArm64SelectFaultProcess(CurThread, Mode);
            ActiveProcess = PsGetCurrentProcess();

            /* Trace SMSS init user faults */
            DPRINT("[arm64][UF] enter VA=%p fc=0x%x proc=%.16s cpu=%lu\n",
                   Address, FaultCode,
                   CurProcess ? CurProcess->ImageFileName : "<none>",
                   KeGetCurrentProcessorNumber());

            if ((ULONG_PTR)Address >= 0x0000070000000000ULL)
            {
                DPRINT("[arm64][FAULT] User fault select: VA=%p Mode=%d Cur=%p(%.16s) Active=%p(%.16s) "
                       "ApcIdx=%lu Attached=%d Apc=%p(%.16s) Saved=%p(%.16s)\n",
                       Address, (int)Mode,
                       CurProcess, CurProcess ? CurProcess->ImageFileName : "<none>",
                       ActiveProcess, ActiveProcess ? ActiveProcess->ImageFileName : "<none>",
                       (ULONG)CurThread->Tcb.ApcStateIndex,
                       KeIsAttachedProcess() ? 1 : 0,
                       CurThread->Tcb.ApcState.Process,
                       CurThread->Tcb.ApcState.Process ?
                           ((PEPROCESS)CurThread->Tcb.ApcState.Process)->ImageFileName : "<none>",
                       CurThread->Tcb.SavedApcState.Process,
                       CurThread->Tcb.SavedApcState.Process ?
                           ((PEPROCESS)CurThread->Tcb.SavedApcState.Process)->ImageFileName : "<none>");
            }

            DPRINT("[arm64][UF] locking WS for VA=%p\n", Address);
            MiLockProcessWorkingSet(CurProcess, CurThread);
            DPRINT("[arm64][UF] WS locked, looking up VAD for VA=%p\n", Address);
            Vad = MiLocateVad(&CurProcess->VadRoot, Address);
            if (Vad != NULL)
            {
                if (Vad->u.VadFlags.PrivateMemory)
                {
                    if (Vad->u.VadFlags.MemCommit)
                    {
                        ProtCode = (ULONG)Vad->u.VadFlags.Protection;
                    }
                    else
                    {
                        PTEB Teb = CurThread->Tcb.Teb;

                        /*
                         * Stack VADs are typically reserve-heavy (MemCommit=0 on the
                         * VAD while per-page state is tracked in PTEs). Route these
                         * faults through the guarded stack-growth logic.
                         */
                        if ((Teb != NULL) &&
                            (Address < Teb->NtTib.StackBase) &&
                            (Address >= Teb->DeallocationStack))
                        {
                            PFN_NUMBER StackL3Pfn = 0;
                            PMMPTE StackPte;
                            MMPTE StackPteContents;
                            BOOLEAN StackSoftUsable = FALSE;

                            StackPte = MiArm64UserPteKseg0ForPfn(PAGE_ALIGN(Address), &StackL3Pfn);
                            StackPteContents.u.Long = (StackPte != NULL) ? StackPte->u.Long : 0ULL;

                            if ((StackPte != NULL) &&
                                (StackPteContents.u.Hard.Valid == 0) &&
                                (StackPteContents.u.Long != 0) &&
                                (StackPteContents.u.Soft.Prototype == 0) &&
                                (StackPteContents.u.Soft.Transition == 0) &&
                                (StackPteContents.u.Soft.PageFileHigh == 0) &&
                                (StackPteContents.u.Soft.Protection != MM_DECOMMIT) &&
                                (StackPteContents.u.Soft.Protection != MM_NOACCESS) &&
                                (StackPteContents.u.Soft.Protection != MM_ZERO_ACCESS))
                            {
                                StackSoftUsable = TRUE;
                            }

                            if (StackSoftUsable &&
                                ((StackPteContents.u.Soft.Protection & MM_PROTECT_SPECIAL) == MM_GUARDPAGE))
                            {
                                StackExpandFault = TRUE;
                            }
                            else if ((StackPte != NULL) &&
                                     (StackPteContents.u.Hard.Valid == 0) &&
                                     (StackPteContents.u.Long != 0) &&
                                     !StackSoftUsable &&
                                     (Address < Teb->NtTib.StackLimit))
                            {
                                DPRINT("[arm64][FAULT] stack PTE malformed/non-ARM3 VA=%p PTE=0x%llx "
                                       "Prot=%lx Proto=%llu Trans=%llu PFH=%llx "
                                       "StackLimit=%p Dealloc=%p\n",
                                        Address,
                                        (unsigned long long)StackPteContents.u.Long,
                                        (ULONG)StackPteContents.u.Soft.Protection,
                                        (unsigned long long)StackPteContents.u.Soft.Prototype,
                                        (unsigned long long)StackPteContents.u.Soft.Transition,
                                        (unsigned long long)StackPteContents.u.Soft.PageFileHigh,
                                        Teb->NtTib.StackLimit,
                                        Teb->DeallocationStack);

                                /*
                                 * Stale/non-ARM3 invalid encoding inside a private stack VAD.
                                 * Normalize to zero so the regular stack-gap grow path can
                                 * commit the page and install a new guard cleanly.
                                 */
                                StackPte->u.Long = 0ULL;
                                __asm__ __volatile__("dsb ishst" ::: "memory");
                                KeInvalidateTlbEntry(PAGE_ALIGN(Address));
                                __asm__ __volatile__("dsb ish" ::: "memory");
                                __asm__ __volatile__("isb" ::: "memory");
                                StackExpandFault = TRUE;
                            }
                            else if ((StackPte != NULL) &&
                                     ((StackPteContents.u.Hard.Valid != 0) ||
                                      ((StackPteContents.u.Long != 0) &&
                                       (StackPteContents.u.Soft.Prototype == 0) &&
                                       (StackPteContents.u.Soft.Transition == 0) &&
                                       (StackPteContents.u.Soft.PageFileHigh == 0) &&
                                       (StackPteContents.u.Soft.Protection != MM_DECOMMIT) &&
                                       (StackPteContents.u.Soft.Protection != MM_NOACCESS) &&
                                       (StackPteContents.u.Soft.Protection != MM_ZERO_ACCESS))))
                            {
                                ProtCode = (ULONG)Vad->u.VadFlags.Protection;
                            }

                            DPRINT("[arm64][FAULT] stack-region decide VA=%p PTE=0x%llx "
                                   "Guard=%d Expand=%d ProtCode=%lx L3Pfn=%Ix\n",
                                    Address,
                                    (unsigned long long)StackPteContents.u.Long,
                                    ((StackPteContents.u.Soft.Protection & MM_PROTECT_SPECIAL) == MM_GUARDPAGE) ? 1 : 0,
                                    StackExpandFault ? 1 : 0,
                                    ProtCode,
                                    (ULONG_PTR)StackL3Pfn);

                            /*
                             * Some ARM64 user code can fault below the current
                             * stack limit without first touching the guard page.
                             * If we're still inside the stack VAD, treat this as
                             * stack growth instead of immediate access violation.
                             */
                            if (!StackExpandFault &&
                                (ProtCode == MM_INVALID_PROTECTION) &&
                                (StackPteContents.u.Long == 0) &&
                                (Address < Teb->NtTib.StackLimit) &&
                                (Address >= Teb->DeallocationStack))
                            {
                                StackExpandFault = TRUE;
                                DPRINT("[arm64][FAULT] stack-gap expand VA=%p StackLimit=%p Dealloc=%p\n",
                                        Address,
                                        Teb->NtTib.StackLimit,
                                        Teb->DeallocationStack);
                            }
                        }
                        else
                        {
                            PFN_NUMBER CommitL3Pfn = 0;
                            PMMPTE CommitPte;
                            MMPTE CommitPteContents;

                            /*
                             * Private VAD without whole-range commit.
                             * This happens with NtAllocateVirtualMemory(MEM_RESERVE)
                             * followed by partial MEM_COMMIT. The commit state is
                             * tracked per-page in the PTE array, not on the VAD.
                             *
                             * Resolve commit state from the actual TTBR0 L3 PTE:
                             * - valid PTE: already committed/mapped.
                             * - demand-zero software PTE: committed but not yet faulted in.
                             * - zero/decommitted/noaccess PTE: not committed for this VA.
                             */
                            CommitPte = MiArm64UserPteKseg0ForPfn(PAGE_ALIGN(Address), &CommitL3Pfn);
                            if (CommitPte != NULL)
                            {
                                CommitPteContents = *CommitPte;

                                if (CommitPteContents.u.Hard.Valid)
                                {
                                    ProtCode = (ULONG)Vad->u.VadFlags.Protection;
                                }
                                else if ((CommitPteContents.u.Long != 0) &&
                                         (CommitPteContents.u.Soft.Prototype == 0) &&
                                         (CommitPteContents.u.Soft.Transition == 0) &&
                                         (CommitPteContents.u.Soft.PageFileHigh == 0) &&
                                         (CommitPteContents.u.Soft.Protection != MM_DECOMMIT) &&
                                         (CommitPteContents.u.Soft.Protection != MM_NOACCESS) &&
                                         (CommitPteContents.u.Soft.Protection != MM_ZERO_ACCESS))
                                {
                                    ProtCode = (ULONG)Vad->u.VadFlags.Protection;
                                }
                            }

                            if (ProtCode == MM_INVALID_PROTECTION)
                            {
                                PMMPTE DiagKseg0 = MiArm64UserPteKseg0(PAGE_ALIGN(Address));
                                DPRINT("[arm64] Private VAD not committed at VA=%p "
                                      "(L3Pfn=%Ix SelfMapPTE=0x%llx Kseg0PTE=0x%llx CommitPte=%p) in %s\n",
                                       PAGE_ALIGN(Address),
                                       (ULONG_PTR)CommitL3Pfn,
                                       (unsigned long long)((CommitPte != NULL) ? CommitPte->u.Long : 0ULL),
                                       (unsigned long long)(DiagKseg0 ? DiagKseg0->u.Long : 0xDEADULL),
                                       CommitPte,
                                       CurProcess->ImageFileName);
                            }
                        }
                    }
                }
                else if (Vad->FirstPrototypePte != NULL)
                {
                    /*
                     * Section view (NLS tables, shared memory, image mappings).
                     *
                     * ARM3 creates section view VADs via MmMapViewOfSection but
                     * does NOT create RosMM memory areas. MmNotPresentFault only
                     * searches RosMM memory areas, so section view faults would
                     * fail with STATUS_ACCESS_VIOLATION if we fell through.
                     *
                     * Handle prototype PTE resolution here: find the prototype
                     * PTE for this page from the VAD, and if the backing page
                     * is already in memory (valid proto PTE), map it directly
                     * into user space via KSEG0.
                     */
                    SectionProtoPte = (((ULONG_PTR)Address >> PAGE_SHIFT) -
                                       Vad->StartingVpn) + Vad->FirstPrototypePte;
                    SectionProtCode = (ULONG)Vad->u.VadFlags.Protection;
                }
            }
            MiUnlockProcessWorkingSet(CurProcess, CurThread);

            DPRINT("[arm64][UF] WS unlocked VA=%p Vad=%p ProtCode=%lu SectionProto=%p StackExpand=%d\n",
                   Address, Vad, ProtCode, SectionProtoPte, StackExpandFault);

            if (Vad == NULL)
            {
                DPRINT("[arm64][FAULT] No ARM3 VAD for VA=%p in Cur=%p(%.16s), Active=%p(%.16s)\n",
                        Address,
                        CurProcess, CurProcess ? CurProcess->ImageFileName : "<none>",
                        ActiveProcess, ActiveProcess ? ActiveProcess->ImageFileName : "<none>");
            }

            if (StackExpandFault)
            {
                PFN_NUMBER StackL3Pfn = 0;
                PMMPTE StackPte = MiArm64UserPteKseg0ForPfn(PAGE_ALIGN(Address), &StackL3Pfn);
                MMPTE StackPteBefore, StackPteAfter;
                NTSTATUS StackStatus;

                StackPteBefore.u.Long = (StackPte != NULL) ? StackPte->u.Long : 0ULL;
                DPRINT("[arm64][FAULT] stack-expand pre VA=%p ProtCode=%lx Vad=[%p..%p] MemCommit=%u "
                       "TEB(StackBase=%p StackLimit=%p Dealloc=%p) PTE=0x%llx L3Pfn=%Ix\n",
                        Address,
                        ProtCode,
                        Vad ? (PVOID)((ULONG_PTR)Vad->StartingVpn << PAGE_SHIFT) : NULL,
                        Vad ? (PVOID)(((ULONG_PTR)Vad->EndingVpn << PAGE_SHIFT) + (PAGE_SIZE - 1)) : NULL,
                        Vad ? Vad->u.VadFlags.MemCommit : 0,
                        CurThread->Tcb.Teb ? CurThread->Tcb.Teb->NtTib.StackBase : NULL,
                        CurThread->Tcb.Teb ? CurThread->Tcb.Teb->NtTib.StackLimit : NULL,
                        CurThread->Tcb.Teb ? CurThread->Tcb.Teb->DeallocationStack : NULL,
                        (unsigned long long)StackPteBefore.u.Long,
                        (ULONG_PTR)StackL3Pfn);

                if ((StackPte != NULL) &&
                    (StackPte->u.Hard.Valid == 0) &&
                    ((StackPte->u.Soft.Protection & MM_PROTECT_SPECIAL) == MM_GUARDPAGE))
                {
                    MMPTE TempPte = *StackPte;
                    TempPte.u.Soft.Protection &= ~MM_GUARDPAGE;
                    MI_WRITE_INVALID_PTE(StackPte, TempPte);
                }

                StackStatus = MiCheckForUserStackOverflow(Address, TrapInformation);
                StackPte = MiArm64UserPteKseg0ForPfn(PAGE_ALIGN(Address), &StackL3Pfn);
                StackPteAfter.u.Long = (StackPte != NULL) ? StackPte->u.Long : 0ULL;
                DPRINT("[arm64][FAULT] stack-expand post VA=%p Status=0x%lx "
                       "TEB(StackLimit=%p) PTE=0x%llx L3Pfn=%Ix\n",
                        Address,
                        StackStatus,
                        CurThread->Tcb.Teb ? CurThread->Tcb.Teb->NtTib.StackLimit : NULL,
                        (unsigned long long)StackPteAfter.u.Long,
                        (ULONG_PTR)StackL3Pfn);
                return StackStatus;
            }

            if (ProtCode != MM_INVALID_PROTECTION)
            {
                /*
                 * Before doing demand-zero allocation, check if the page is
                 * ALREADY validly mapped in the TTBR0 page tables. This handles
                 * pages written by the kernel during KeAttachProcess (e.g.,
                 * MmCreatePeb writing PEB, MmCreateTeb writing TEB) whose
                 * mappings survive but cause spurious faults.
                 *
                 * Walk TTBR0 via self-map to check the actual L3 PTE. If it's
                 * already valid, the mapping exists - return success without
                 * overwriting it with a new zero page.
                 */
                {
                    PFN_NUMBER ExistingL3Pfn = 0;
                    PMMPTE ExistingPte = MiArm64UserPteKseg0ForPfn(
                        PAGE_ALIGN(Address), &ExistingL3Pfn);

                    DPRINT("[arm64][UF] already-mapped check VA=%p ExistingPte=%p val=0x%llx L3Pfn=%Ix\n",
                           Address, ExistingPte,
                           ExistingPte ? (ULONG64)ExistingPte->u.Long : 0ULL,
                           (ULONG_PTR)ExistingL3Pfn);

                    if (ExistingPte != NULL && ExistingPte->u.Hard.Valid)
                    {
                        DPRINT("[arm64][UF] ALREADY MAPPED VA=%p PFN=%Ix AF=%u - returning SUCCESS\n",
                               PAGE_ALIGN(Address),
                               (ULONG_PTR)ExistingPte->u.Hard.PageFrameNumber,
                               (ULONG)ExistingPte->u.Hard.Accessed);
                        return STATUS_SUCCESS;
                    }
                }

                /*
                 * Committed private memory (demand-zero).
                 *
                 * Use ARM3-style page allocation (MiRemoveAnyPage) instead of
                 * RosMM's MmRequestPageMemoryConsumer. ARM3's decommit path
                 * (NtFreeVirtualMemory → MiDecommitPages) asserts that valid
                 * PTEs point to ARM3-managed PFNs (MI_IS_ROS_PFN == FALSE).
                 * MmRequestPageMemoryConsumer sets AweAllocation=TRUE (RosMM),
                 * causing the assertion to fire.
                 *
                 * We still use MmCreateVirtualMappingUnsafe for the actual PTE
                 * write because it correctly creates the TTBR0 page table
                 * hierarchy (L0→L1→L2→L3) via system PTEs.
                 */
                PFN_NUMBER PageFrameIndex = 0;
                ULONG Color;
                PMMPFN Pfn1;
                KIRQL PfnIrql;

                /* Handle guard pages (stack expansion) */
                if ((ProtCode & MM_PROTECT_SPECIAL) == MM_GUARDPAGE)
                {
                    return MiCheckForUserStackOverflow(Address, TrapInformation);
                }

                /* Allocate a physical page via ARM3 allocator */
                PfnIrql = MiAcquirePfnLock();
                Color = MI_GET_NEXT_PROCESS_COLOR(CurProcess);
                PageFrameIndex = MiRemoveZeroPageSafe(Color);
                if (PageFrameIndex == 0)
                {
                    PageFrameIndex = MiRemoveAnyPage(Color);
                }
                if (PageFrameIndex == 0)
                {
                    MiReleasePfnLock(PfnIrql);
                    return STATUS_NO_MEMORY;
                }

                /*
                 * Initialize PFN for ARM3 compatibility.
                 *
                 * We can't use MiInitializePfn because it reads *PointerPte
                 * and MiAddressToPte(PointerPte) via the self-map, which is
                 * broken for ARM64 user addresses. Set up fields manually.
                 * PteFrame will be set after MmCreateVirtualMappingUnsafe
                 * creates the L3 page table.
                 *
                 * Note: Set ShareCount=0 here because MmCreateVirtualMappingUnsafe
                 * will increment it to 1 when writing the PTE.
                 */
                Pfn1 = MI_PFN_ELEMENT(PageFrameIndex);
                Pfn1->PteAddress = MiAddressToPte(Address);
                MI_MAKE_SOFTWARE_PTE(&Pfn1->OriginalPte, MM_READWRITE);
                Pfn1->u3.e2.ReferenceCount = 1;
                Pfn1->u2.ShareCount = 0;
                Pfn1->u3.e1.PageLocation = ActiveAndValid;
                Pfn1->u3.e1.Modified = 1;
                Pfn1->u3.e1.PrototypePte = 0;
                Pfn1->u3.e1.Rom = 0;
                Pfn1->u4.PteFrame = 0;  /* Set after page table creation */
                MiReleasePfnLock(PfnIrql);

                /* Zero the page */
                MiZeroPhysicalPage(PageFrameIndex);

                /* Create the mapping in TTBR0 page tables via KSEG0 */
                {
                    ULONG PageProtection = MmProtectToValue[ProtCode & 0x1F];
                    NTSTATUS MapStatus;

                    MapStatus = MmCreateVirtualMappingUnsafe(
                        CurProcess, PAGE_ALIGN(Address),
                        PageProtection, PageFrameIndex);

                    if (NT_SUCCESS(MapStatus))
                    {
                        PFN_NUMBER L3Pfn = 0;

                        /*
                         * Set PteFrame: walk self-map to find the L3 page table
                         * PFN that contains our PTE.
                         *
                         * Note: L3 ShareCount is now incremented centrally in
                         * MmCreateVirtualMappingUnsafeEx, so we only set PteFrame here.
                         */
                        PfnIrql = MiAcquirePfnLock();
                        (void)MiArm64UserPteKseg0ForPfn(PAGE_ALIGN(Address), &L3Pfn);
                        if (L3Pfn != 0)
                        {
                            Pfn1->u4.PteFrame = L3Pfn;
                            ASSERT(Pfn1->u4.PteFrame != 0);
                        }
                        else
                        {
                            DPRINT1("[arm64] Failed to recover L3 PFN for VA=%p after map\n",
                                    PAGE_ALIGN(Address));
                        }
                        ASSERT(Pfn1->u2.ShareCount >= 1);
                        MiReleasePfnLock(PfnIrql);

                        CurProcess->NumberOfPrivatePages++;

                        DPRINT("[arm64][UF] Demand-zero mapped %p in %s (PFN %Ix, L3=%Ix, RefCnt=%u, ShareCnt=%u)\n",
                               PAGE_ALIGN(Address),
                               CurProcess->ImageFileName,
                               PageFrameIndex, L3Pfn,
                               (ULONG)Pfn1->u3.e2.ReferenceCount,
                               (ULONG)Pfn1->u2.ShareCount);
                        DPRINT("[arm64] Demand-zero mapped %p in %s (PFN %Ix, L3=%Ix)\n",
                               PAGE_ALIGN(Address),
                               CurProcess->ImageFileName,
                               PageFrameIndex, L3Pfn);
                        return STATUS_SUCCESS;
                    }

                    /* Mapping failed - free the page */
                    PfnIrql = MiAcquirePfnLock();
                    MI_SET_PFN_DELETED(Pfn1);
                    MiDecrementShareCount(Pfn1, PageFrameIndex);
                    MiReleasePfnLock(PfnIrql);
                }

                return STATUS_NO_MEMORY;
            }

            /*
             * Section view prototype PTE resolution.
             *
             * On AMD64, this goes through the full PXE/PPE/PDE/PTE cascade
             * with MiResolveProtoPteFault. On ARM64, we handle prototype PTE
             * resolution directly here via the self-map (MiAddressToPte).
             *
             * For valid prototype PTEs (backing page already in memory),
             * we map the shared page into user space via MmCreateVirtualMappingUnsafe.
             * This covers NLS tables, shared data sections, and image mappings
             * whose pages have been previously faulted in.
             */
            if (SectionProtoPte != NULL)
            {
                MMPTE ProtoContents = *SectionProtoPte;

                if (ProtoContents.u.Hard.Valid == 1)
                {
                    /* Prototype PTE is valid - page is resident in memory */
                    PFN_NUMBER ProtoPageFrame = PFN_FROM_PTE(&ProtoContents);
                    ULONG PageProtection = MmProtectToValue[SectionProtCode & 0x1F];
                    NTSTATUS MapStatus;

                    /* Create the mapping in TTBR0 page tables via KSEG0 */
                    MapStatus = MmCreateVirtualMappingUnsafe(
                        CurProcess, PAGE_ALIGN(Address),
                        PageProtection, ProtoPageFrame);

                    if (NT_SUCCESS(MapStatus))
                    {
                        DPRINT("[arm64] Section view mapped %p -> PFN %Ix in %s\n",
                               PAGE_ALIGN(Address), ProtoPageFrame,
                               CurProcess->ImageFileName);
                        return STATUS_SUCCESS;
                    }

                    return STATUS_NO_MEMORY;
                }
                else if (ProtoContents.u.Long == 0)
                {
                    /* Zero prototype PTE - reserved but uncommitted range */
                    DPRINT1("[arm64] Section view: zero proto PTE for VA=%p\n", Address);
                    return STATUS_ACCESS_VIOLATION;
                }
                else if ((ProtoContents.u.Soft.Prototype == 0) &&
                         (ProtoContents.u.Soft.Transition == 1))
                {
                    /*
                     * Transition prototype PTE.
                     * Resolve the prototype page first, then map it into the
                     * faulting process via TTBR0.
                     */
                    KIRQL LockIrql;
                    PKEVENT* InPageBlock = NULL;
                    PKEVENT PreviousPageEvent = NULL;
                    KEVENT CurrentPageEvent;
                    NTSTATUS ResolveStatus;
                    PFN_NUMBER ProtoPageFrame;
                    ULONG PageProtection = MmProtectToValue[SectionProtCode & 0x1F];
                    NTSTATUS MapStatus;

                    LockIrql = MiAcquirePfnLock();
                    ResolveStatus = MiResolveTransitionFault(!MI_IS_NOT_PRESENT_FAULT(FaultCode),
                                                             PAGE_ALIGN(Address),
                                                             SectionProtoPte,
                                                             CurProcess,
                                                             LockIrql,
                                                             &InPageBlock);
                    if (NT_SUCCESS(ResolveStatus) && (InPageBlock != NULL))
                    {
                        KeInitializeEvent(&CurrentPageEvent, NotificationEvent, FALSE);
                        PreviousPageEvent = *InPageBlock;
                        *InPageBlock = &CurrentPageEvent;
                    }
                    MiReleasePfnLock(LockIrql);

                    if (!NT_SUCCESS(ResolveStatus))
                        return ResolveStatus;

                    if (InPageBlock != NULL)
                    {
                        KeWaitForSingleObject(&CurrentPageEvent,
                                              WrPageIn,
                                              KernelMode,
                                              FALSE,
                                              NULL);
                        if (PreviousPageEvent)
                            KeSetEvent(PreviousPageEvent, IO_NO_INCREMENT, FALSE);
                    }

                    ProtoContents = *SectionProtoPte;
                    if (ProtoContents.u.Hard.Valid != 1)
                    {
                        DPRINT1("[arm64] Section view: transition resolve left invalid proto "
                                "VA=%p PTE=0x%llx\n",
                                Address, (unsigned long long)ProtoContents.u.Long);
                        return STATUS_ACCESS_VIOLATION;
                    }

                    ProtoPageFrame = PFN_FROM_PTE(&ProtoContents);
                    MapStatus = MmCreateVirtualMappingUnsafe(CurProcess,
                                                             PAGE_ALIGN(Address),
                                                             PageProtection,
                                                             ProtoPageFrame);
                    if (NT_SUCCESS(MapStatus))
                    {
                        DPRINT("[arm64] Section view transition resolved %p -> PFN %Ix in %s\n",
                               PAGE_ALIGN(Address), ProtoPageFrame,
                               CurProcess->ImageFileName);
                        return STATUS_SUCCESS;
                    }

                    return STATUS_NO_MEMORY;
                }
                else if ((ProtoContents.u.Soft.Prototype == 0) &&
                         (ProtoContents.u.Soft.Transition == 0) &&
                         (ProtoContents.u.Soft.PageFileHigh != 0) &&
                         (ProtoContents.u.Soft.PageFileHigh != MI_PTE_LOOKUP_NEEDED))
                {
                    /*
                     * Paged-out prototype PTE.
                     * Page the backing data in, then map the now-resident page.
                     */
                    KIRQL LockIrql;
                    NTSTATUS ResolveStatus;
                    PFN_NUMBER ProtoPageFrame;
                    ULONG PageProtection = MmProtectToValue[SectionProtCode & 0x1F];
                    NTSTATUS MapStatus;

                    LockIrql = MiAcquirePfnLock();
                    ResolveStatus = MiResolvePageFileFault(!MI_IS_NOT_PRESENT_FAULT(FaultCode),
                                                           PAGE_ALIGN(Address),
                                                           SectionProtoPte,
                                                           CurProcess,
                                                           &LockIrql);
                    MiReleasePfnLock(LockIrql);

                    if (!NT_SUCCESS(ResolveStatus))
                        return ResolveStatus;

                    ProtoContents = *SectionProtoPte;
                    if (ProtoContents.u.Hard.Valid != 1)
                    {
                        DPRINT1("[arm64] Section view: pagefile resolve left invalid proto "
                                "VA=%p PTE=0x%llx\n",
                                Address, (unsigned long long)ProtoContents.u.Long);
                        return STATUS_ACCESS_VIOLATION;
                    }

                    ProtoPageFrame = PFN_FROM_PTE(&ProtoContents);
                    MapStatus = MmCreateVirtualMappingUnsafe(CurProcess,
                                                             PAGE_ALIGN(Address),
                                                             PageProtection,
                                                             ProtoPageFrame);
                    if (NT_SUCCESS(MapStatus))
                    {
                        DPRINT("[arm64] Section view pagefile resolved %p -> PFN %Ix in %s\n",
                               PAGE_ALIGN(Address), ProtoPageFrame,
                               CurProcess->ImageFileName);
                        return STATUS_SUCCESS;
                    }

                    return STATUS_NO_MEMORY;
                }
                else if (ProtoContents.u.Soft.Transition == 0 &&
                         ProtoContents.u.Soft.Prototype == 0 &&
                         ProtoContents.u.Soft.PageFileHigh == 0)
                {
                    /*
                     * Demand-zero prototype PTE.
                     * The page hasn't been faulted in yet. Allocate a zero page,
                     * make the prototype PTE valid, then map into user space.
                     *
                     * This handles pagefile-backed section views (e.g. CSRSRV
                     * SharedSection) where pages start as demand-zero.
                     */
                    PFN_NUMBER NewPageFrame;
                    PMMPFN Pfn1;
                    MMPTE ProtoTempPte;
                    ULONG Color;
                    KIRQL PfnIrql;
                    BOOLEAN NeedZero = FALSE;
                    ULONG PageProtection = MmProtectToValue[SectionProtCode & 0x1F];
                    ULONG ProtoProtection = (ULONG)ProtoContents.u.Soft.Protection;
                    NTSTATUS MapStatus;

                    /* Allocate a page */
                    PfnIrql = MiAcquirePfnLock();
                    Color = MI_GET_NEXT_COLOR();
                    NewPageFrame = MiRemoveZeroPageSafe(Color);
                    if (NewPageFrame == 0)
                    {
                        NewPageFrame = MiRemoveAnyPage(Color);
                        NeedZero = TRUE;
                    }
                    if (NewPageFrame == 0)
                    {
                        MiReleasePfnLock(PfnIrql);
                        return STATUS_NO_MEMORY;
                    }

                    /* Initialize PFN for prototype-backed page */
                    Pfn1 = MiGetPfnEntry(NewPageFrame);
                    Pfn1->PteAddress = SectionProtoPte;
                    Pfn1->OriginalPte = ProtoContents;
                    Pfn1->u3.e2.ReferenceCount = 1;
                    Pfn1->u2.ShareCount = 0;  /* MmCreateVirtualMappingUnsafe increments to 1 */
                    Pfn1->u3.e1.PageLocation = ActiveAndValid;
                    Pfn1->u3.e1.Modified = 1;
                    Pfn1->u3.e1.PrototypePte = 1;
                    Pfn1->u3.e1.Rom = 0;
                    Pfn1->u4.PteFrame = 0;

                    /* PteFrame = PFN of page containing the prototype PTE (kernel address) */
                    {
                        PMMPTE ProtoTablePte = MiAddressToPte(SectionProtoPte);
                        if (ProtoTablePte->u.Hard.Valid)
                            Pfn1->u4.PteFrame = PFN_FROM_PTE(ProtoTablePte);
                    }

                    MiReleasePfnLock(PfnIrql);

                    /* Zero the page if needed (outside PFN lock) */
                    if (NeedZero)
                        MiZeroPhysicalPage(NewPageFrame);

                    /* Write valid hardware PTE to prototype PTE (kernel address, self-map OK) */
                    MI_MAKE_HARDWARE_PTE(&ProtoTempPte, SectionProtoPte,
                                         ProtoProtection, NewPageFrame);
                    if (MI_IS_PAGE_WRITEABLE(&ProtoTempPte))
                        MI_MAKE_DIRTY_PAGE(&ProtoTempPte);
                    MI_WRITE_VALID_PTE(SectionProtoPte, ProtoTempPte);

                    /* Map into user's address space via TTBR0 page tables */
                    MapStatus = MmCreateVirtualMappingUnsafe(
                        CurProcess, PAGE_ALIGN(Address),
                        PageProtection, NewPageFrame);

                    if (NT_SUCCESS(MapStatus))
                    {
                        /*
                         * L3 ShareCount is now incremented centrally in
                         * MmCreateVirtualMappingUnsafeEx - no duplicate needed here.
                         */

                        DPRINT("[arm64] Section view demand-zero resolved %p -> PFN %Ix in %s\n",
                               PAGE_ALIGN(Address), NewPageFrame,
                               CurProcess->ImageFileName);
                        return STATUS_SUCCESS;
                    }

                    /* Mapping failed, rollback prototype PTE and free page */
                    PfnIrql = MiAcquirePfnLock();
                    MI_WRITE_INVALID_PTE(SectionProtoPte, ProtoContents);
                    Pfn1 = MiGetPfnEntry(NewPageFrame);
                    MI_SET_PFN_DELETED(Pfn1);
                    MiDecrementShareCount(Pfn1, NewPageFrame);
                    MiReleasePfnLock(PfnIrql);

                    return STATUS_NO_MEMORY;
                }
                else
                {
                    /*
                     * Subsection/prototype-pointer or other unsupported state.
                     * Fall through to RosMM and keep diagnostic context.
                     */
                    DPRINT1("[arm64] Section view: unhandled proto PTE state "
                            "VA=%p PTE=0x%llx (Proto=%llu Trans=%llu PFH=%llx) - falling through\n",
                            Address,
                            (unsigned long long)ProtoContents.u.Long,
                            (unsigned long long)ProtoContents.u.Soft.Prototype,
                            (unsigned long long)ProtoContents.u.Soft.Transition,
                            (unsigned long long)ProtoContents.u.Soft.PageFileHigh);
                }
            }

            /*
             * Not handled above - fall through to RosMM.
             * This covers addresses with no VAD and unresolved section states.
             */
        }

        /* Section views and other RosMM memory areas */
        {
            NTSTATUS RosMmStatus = MmNotPresentFault(Mode, (ULONG_PTR)Address, FALSE);

            if (!NT_SUCCESS(RosMmStatus))
            {
                /*
                 * Diagnostic: When a user fault falls through to RosMM and fails,
                 * print the full decision context to help identify why ARM3 didn't
                 * handle it. Walk TTBR0 via KSEG0 for ground truth.
                 */
                PMMPTE DiagPteKseg0 = MiArm64UserPteKseg0(Address);
                PMMPTE DiagPdeKseg0 = MiArm64UserPdeKseg0(Address);
                PETHREAD DiagThread = PsGetCurrentThread();
                PEPROCESS DiagProc = PsGetCurrentProcess();
                PMMVAD DiagVad = NULL;

                DPRINT1("[arm64][FAULT] User fault FAILED: Va=%p Mode=%d Write=%d Status=0x%lx "
                        "Process=%p(%.16s)\n",
                        Address, (int)Mode, MI_IS_WRITE_ACCESS(FaultCode) ? 1 : 0,
                        RosMmStatus, DiagProc, DiagProc->ImageFileName);

                /* Re-lookup the VAD for diagnostic printing */
                MiLockProcessWorkingSet(DiagProc, DiagThread);
                DiagVad = MiLocateVad(&DiagProc->VadRoot, Address);
                if (DiagVad != NULL)
                {
                    DPRINT1("[arm64][FAULT]   Vad: [%p..%p] Private=%u MemCommit=%u Protection=%lu "
                            "VadType=%u FirstProto=%p\n",
                            (PVOID)((ULONG_PTR)DiagVad->StartingVpn << PAGE_SHIFT),
                            (PVOID)(((ULONG_PTR)DiagVad->EndingVpn << PAGE_SHIFT) | (PAGE_SIZE - 1)),
                            DiagVad->u.VadFlags.PrivateMemory,
                            DiagVad->u.VadFlags.MemCommit,
                            (ULONG)DiagVad->u.VadFlags.Protection,
                            (ULONG)DiagVad->u.VadFlags.VadType,
                            DiagVad->FirstPrototypePte);
                }
                else
                {
                    DPRINT1("[arm64][FAULT]   No VAD found for VA=%p!\n", Address);
                }
                MiUnlockProcessWorkingSet(DiagProc, DiagThread);

                DPRINT1("[arm64][FAULT]   KSEG0 PDE=%p(0x%llx) PTE=%p(0x%llx)\n",
                        DiagPdeKseg0,
                        DiagPdeKseg0 ? (unsigned long long)DiagPdeKseg0->u.Long : 0ULL,
                        DiagPteKseg0,
                        DiagPteKseg0 ? (unsigned long long)DiagPteKseg0->u.Long : 0ULL);
            }

            return RosMmStatus;
        }
    }
#endif

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

#if defined(_M_ARM64) || defined(__aarch64__)
                /* Always user-mode COW path: write via KSEG0 */
                MiArm64WriteValidUserPte(Address, TempPte);
#else
                MI_WRITE_VALID_PTE(PointerPte, TempPte);
#endif

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
                /* Always user-mode path: update via KSEG0 */
                MiArm64UpdateValidUserPte(Address, TempPte);
#else
                TempPte.u.Hard.NoExecute = 0;
                MI_UPDATE_VALID_PTE(PointerPte, TempPte);
#endif
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

#if defined(_M_ARM64) || defined(__aarch64__)
            /* ARM64: Write user PTEs via KSEG0 to bypass self-map */
            if (PointerPte <= MiHighestUserPte)
                MiArm64WriteValidUserPte(Address, TempPte);
            else
#endif
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
