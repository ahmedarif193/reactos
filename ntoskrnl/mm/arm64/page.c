/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/arm64/page.c
 * PURPOSE:         ARM64 virtual memory helper routines
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* ARM64 PTE address mask - extracts physical address from page table entry (bits 47:12) */
#ifndef ARM64_PTE_ADDR_MASK
#define ARM64_PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL
#endif


static
NTSTATUS
MmCreateVirtualMappingUnsafeEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical);

static
BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte);

/* MI_ARM64_USER_PTE_WALK lives in internal/arm64/mm.h; the AWE support in
 * ARM3/awesup.c shares the walk and release helpers. */

/*
 * Sanity-check a table physical address without probing or mapping. The
 * read-only walkers use this: every live table page is KSEG0-mapped at
 * allocation time, so the AT-probe/ensure machinery is pure overhead there.
 */
FORCEINLINE
BOOLEAN
MiArm64IsValidTablePa(
    _In_ ULONG64 TablePa)
{
    PFN_NUMBER Pfn;

    if ((TablePa & (PAGE_SIZE - 1)) != 0)
    {
        return FALSE;
    }

    Pfn = (PFN_NUMBER)(TablePa >> PAGE_SHIFT);
    return ((Pfn != 0) && (Pfn <= MmHighestPhysicalPage));
}

static
BOOLEAN
MiArm64EnsureTablePageMapped(
    _In_ ULONG64 TablePa)
{
    PFN_NUMBER Pfn;

    if (!MiArm64IsValidTablePa(TablePa))
    {
        return FALSE;
    }

    Pfn = (PFN_NUMBER)(TablePa >> PAGE_SHIFT);

    /*
     * Fast path: probe the KSEG0 alias translation with AT S1E1R. Frames
     * already covered by the boot KSEG0 direct map (the steady state) skip
     * the identity-mapping machinery, which costs several table walks per
     * call and runs four times per user PTE walk.
     */
    if (MiArm64ProbeForAccess((PVOID)MI_ARM64_PFN_TO_VA(Pfn), FALSE))
    {
        return TRUE;
    }

    MiArm64MapKseg0Page(Pfn);
    return MiArm64ProbeForAccess((PVOID)MI_ARM64_PFN_TO_VA(Pfn), FALSE);
}

static
BOOLEAN
MiArm64GetUserPteAddress(
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk);

static
MMPTE
MiArm64ClearKernelPte(
    _In_ PVOID Address,
    _Inout_ PMMPTE PointerPte,
    _In_ PMMPTE Kseg0Pte);

static
MMPTE
MiArm64ClearUserPte(
    _In_ PVOID Address,
    _In_ PMI_ARM64_USER_PTE_WALK Walk);

static
BOOLEAN
MiArm64ConsumeDirtyState(
    _In_ MMPTE OldPte,
    _In_ BOOLEAN IsPhysical);

static
VOID
MiArm64PreserveDirtyStateForProtect(
    _In_ MMPTE OldPte,
    _In_ MMPTE NewPte);

static
VOID
MiArm64ReleaseMappedPageReference(
    _In_ PFN_NUMBER PageFrameNumber);

FORCEINLINE
VOID
MiArm64WritePteEntry(
    _Inout_ volatile ULONG64 *Entry,
    _In_ ULONG64 Value);

typedef enum _MI_ARM64_DCACHE_OPERATION
{
    MiArm64DcacheInvalidate,
    MiArm64DcacheCleanInvalidate
} MI_ARM64_DCACHE_OPERATION;

FORCEINLINE
VOID
MiArm64MaintainPageCache(
    _In_ PVOID Address,
    _In_ MI_ARM64_DCACHE_OPERATION DcacheOperation,
    _In_ BOOLEAN SweepIcache)
{
    ULONG DcacheLineSize;
    ULONG IcacheLineSize;
    ULONG_PTR Va;

    KiArm64GetCacheLineSizes(&DcacheLineSize, &IcacheLineSize);
    Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    /* Inner-shareable barriers: every observer of this maintenance (other
     * CPUs, I-fetch, IS-broadcast cache/TLB ops) is in the IS domain; DMA
     * coherency is owned by KeFlushIoBuffers, which uses the same policy. */
    __asm__ __volatile__("dsb ish" ::: "memory");
    for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += DcacheLineSize)
    {
        if (DcacheOperation == MiArm64DcacheCleanInvalidate)
        {
            __asm__ __volatile__("dc civac, %0" :: "r"(Va + Offset) : "memory");
        }
        else
        {
            __asm__ __volatile__("dc ivac, %0" :: "r"(Va + Offset) : "memory");
        }
    }
    __asm__ __volatile__("dsb ish" ::: "memory");

    if (SweepIcache)
    {
        for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += IcacheLineSize)
        {
            __asm__ __volatile__("ic ivau, %0" :: "r"(Va + Offset) : "memory");
        }
        __asm__ __volatile__("dsb ish" ::: "memory");
    }

    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
MiArm64InvalidatePageByPfnAlias(
    _In_ PFN_NUMBER PageFrameNumber)
{
    MiArm64MaintainPageCache((PVOID)MI_ARM64_PFN_TO_VA(PageFrameNumber),
                             MiArm64DcacheInvalidate,
                             FALSE);
}

FORCEINLINE
VOID
MiArm64PublishPageByPfnAlias(
    _In_ PFN_NUMBER PageFrameNumber,
    _In_ BOOLEAN SweepIcache)
{
    MiArm64MaintainPageCache((PVOID)MI_ARM64_PFN_TO_VA(PageFrameNumber),
                             MiArm64DcacheCleanInvalidate,
                             SweepIcache);
}

FORCEINLINE
VOID
MiArm64SyncMappedPfnCacheAttribute(
    _In_ PMMPFN Pfn1,
    _In_ MMPTE FinalPte)
{
    MI_PFN_CACHE_ATTRIBUTE NewCache = MiGetPteCacheAttribute(&FinalPte);

    if (Pfn1->u3.e1.CacheAttribute != NewCache)
    {
        Pfn1->u3.e1.CacheAttribute = NewCache;
    }
}

BOOLEAN
MiArm64GetUserPteAddressForProcess(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk)
{
    ULONG64 RootPa, L1Pa, L2Pa, L3Pa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG64 L0Entry, L1Entry, L2Entry;

    RtlZeroMemory(Walk, sizeof(*Walk));

    if (Address >= MmSystemRangeStart)
    {
        return FALSE;
    }

    ASSERT(Process != NULL);
    if (Process == PsGetCurrentProcess())
    {
        ULONG64 Ttbr0;

        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;
    }
    else
    {
        RootPa = Process->Pcb.DirectoryTableBase[0] & ARM64_PTE_ADDR_MASK;
    }

    if (RootPa == 0)
    {
        return FALSE;
    }

    L0Idx = ((ULONG64)(ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64;
    L3Idx = ((ULONG64)(ULONG_PTR)Address >> PTI_SHIFT) & PTI_MASK_ARM64;

    if (!MiArm64IsValidTablePa(RootPa))
    {
        return FALSE;
    }

    L0Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    Walk->LevelTable[0] = L0Table;
    Walk->LevelPfn[0] = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
    {
        return FALSE;
    }
    Walk->Depth = 1;

    L1Pa = L0Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64IsValidTablePa(L1Pa))
    {
        return FALSE;
    }

    L1Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1Pa);
    Walk->LevelTable[1] = L1Table;
    Walk->LevelPfn[1] = (PFN_NUMBER)(L1Pa >> PAGE_SHIFT);
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x1ULL) == 0)
    {
        return FALSE;
    }
    if ((L1Entry & 0x3ULL) == 0x1ULL)
    {
        Walk->PteValue = L1Entry;
        return FALSE;
    }
    if ((L1Entry & 0x3ULL) != 0x3ULL)
    {
        return FALSE;
    }
    Walk->Depth = 2;

    L2Pa = L1Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64IsValidTablePa(L2Pa))
    {
        return FALSE;
    }

    L2Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2Pa);
    Walk->LevelTable[2] = L2Table;
    Walk->LevelPfn[2] = (PFN_NUMBER)(L2Pa >> PAGE_SHIFT);
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x1ULL) == 0)
    {
        return FALSE;
    }
    if ((L2Entry & 0x3ULL) == 0x1ULL)
    {
        Walk->PteValue = L2Entry;
        return FALSE;
    }
    if ((L2Entry & 0x3ULL) != 0x3ULL)
    {
        return FALSE;
    }
    Walk->Depth = 3;

    L3Pa = L2Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64IsValidTablePa(L3Pa))
    {
        return FALSE;
    }

    L3Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L3Pa);
    Walk->LevelTable[3] = L3Table;
    Walk->LevelPfn[3] = (PFN_NUMBER)(L3Pa >> PAGE_SHIFT);
    Walk->PointerPte = &L3Table[L3Idx];
    Walk->PteValue = L3Table[L3Idx];
    if ((Walk->PteValue & 0x3ULL) == 0x3ULL)
    {
        Walk->Depth = 4;
    }

    return TRUE;
}

static
BOOLEAN
MiArm64GetUserPteAddress(
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk)
{
    return MiArm64GetUserPteAddressForProcess(PsGetCurrentProcess(), Address, Walk);
}

/* Walk the active TTBR0 hierarchy via KSEG0 and return the L3 PTE state. */
static
BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte)
{
    MI_ARM64_USER_PTE_WALK Walk;
    BOOLEAN Reached = MiArm64GetUserPteAddress(Address, &Walk);

    if (OutPte) *OutPte = Walk.PteValue;
    return (Reached && ((Walk.PteValue & 0x3ULL) == 0x3ULL));
}

NTSTATUS
MiArm64AllocateCleanPage(
    _Inout_ PEPROCESS Process,
    _Out_ PPFN_NUMBER PageFrame)
{
    KIRQL OldIrql;
    ULONG Color;
    PFN_NUMBER PageFrameIndex;
    BOOLEAN NeedZero = FALSE;

    OldIrql = MiAcquirePfnLock();
    Color = MI_GET_NEXT_PROCESS_COLOR(Process);
    PageFrameIndex = MiRemoveZeroPageSafe(Color);
    if (PageFrameIndex == 0)
    {
        PageFrameIndex = MiRemoveAnyPage(Color);
        if (PageFrameIndex == 0)
        {
            MiReleasePfnLock(OldIrql);
            return STATUS_NO_MEMORY;
        }

        NeedZero = TRUE;
    }
    MiReleasePfnLock(OldIrql);

    if (NeedZero)
    {
        MiZeroPhysicalPage(PageFrameIndex);
    }

    MiArm64MapKseg0Page(PageFrameIndex);
    MiArm64CleanPageToPoC((PVOID)MI_ARM64_PFN_TO_VA(PageFrameIndex));
    *PageFrame = PageFrameIndex;
    return STATUS_SUCCESS;
}

static
VOID
MiArm64InitializeUserTablePage(
    _In_ PFN_NUMBER TablePfn,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER ParentPfn)
{
    KIRQL OldIrql;
    PMMPFN Pfn;
    PMMPFN ParentPfnEntry;

    OldIrql = MiAcquirePfnLock();

    Pfn = MiGetPfnEntry(TablePfn);
    ASSERT(Pfn != NULL);
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);

    Pfn->PteAddress = PteAddress;
    MI_MAKE_SOFTWARE_PTE(&Pfn->OriginalPte, MM_READWRITE);
    Pfn->u3.e2.ReferenceCount = 1;
    Pfn->u2.ShareCount = 1;
    Pfn->u3.e1.PageLocation = ActiveAndValid;
    Pfn->u3.e1.Modified = TRUE;
    Pfn->u4.InPageError = FALSE;
    Pfn->u4.PteFrame = ParentPfn;

    ParentPfnEntry = MiGetPfnEntry(ParentPfn);
    ASSERT(ParentPfnEntry != NULL);
    ParentPfnEntry->u2.ShareCount++;
    ParentPfnEntry->OriginalPte.u.Soft.UsedPageTableEntries++;
    ASSERT(ParentPfnEntry->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);

    MiReleasePfnLock(OldIrql);
}

static
VOID
MiArm64AccountUserLeafPte(
    _In_ PFN_NUMBER PteFrame,
    _In_ BOOLEAN AddShareCount,
    _In_ BOOLEAN AddUsedEntry)
{
    KIRQL OldIrql;
    PMMPFN Pfn;

    OldIrql = MiAcquirePfnLock();

    Pfn = MiGetPfnEntry(PteFrame);
    ASSERT(Pfn != NULL);

    if (AddShareCount)
    {
        Pfn->u2.ShareCount++;
    }

    if (AddUsedEntry)
    {
        Pfn->OriginalPte.u.Soft.UsedPageTableEntries++;
        ASSERT(Pfn->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
    }

    MiReleasePfnLock(OldIrql);
}

VOID
MiArm64IncrementUserLeafPteCount(
    _In_ PFN_NUMBER PteFrame)
{
    MiArm64AccountUserLeafPte(PteFrame, FALSE, TRUE);
}

/*
 * Steady-state predicate: TRUE when the table PFN is already fully
 * accounted for. Caller holds the PFN lock.
 */
FORCEINLINE
BOOLEAN
MiArm64UserTablePfnHealthy(
    _In_ PFN_NUMBER TablePfn,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER ParentPfn)
{
    PMMPFN Pfn = MiGetPfnEntry(TablePfn);

    if (Pfn == NULL)
    {
        return FALSE;
    }

    return ((Pfn->u3.e1.PageLocation == ActiveAndValid) &&
            (Pfn->u3.e2.ReferenceCount != 0) &&
            (Pfn->PteAddress == PteAddress) &&
            ((ParentPfn == 0) || (Pfn->u4.PteFrame == ParentPfn)));
}

NTSTATUS
MiArm64EnsureUserPte(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _Outptr_ PMMPTE *PointerPte,
    _Out_opt_ PPFN_NUMBER L3TablePfn)
{
    ULONG64 Ttbr0, RootPa;
    volatile ULONG64 *Table;
    ULONG64 Entry;
    PFN_NUMBER ParentPfn, ChildPfn;
    PFN_NUMBER RootPfn;
    PFN_NUMBER TablePfn[3];
    PVOID LevelPteAddress[3];
    ULONG LevelIndex[4];
    NTSTATUS Status;
    ULONG Level;
    KIRQL OldIrql;
    BOOLEAN Healthy;
    BOOLEAN AllocatedTable = FALSE;

    *PointerPte = NULL;
    if (L3TablePfn != NULL)
    {
        *L3TablePfn = 0;
    }

    if (Address >= MmSystemRangeStart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(PsGetCurrentThread()->OwnsProcessWorkingSetExclusive);

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RootPfn = RootPa >> PAGE_SHIFT;
    LevelIndex[0] = ((ULONG64)(ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
    LevelIndex[1] = ((ULONG64)(ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK;
    LevelIndex[2] = ((ULONG64)(ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64;
    LevelIndex[3] = ((ULONG64)(ULONG_PTR)Address >> PTI_SHIFT) & PTI_MASK_ARM64;
    LevelPteAddress[0] = MiAddressToPxe(Address);
    LevelPteAddress[1] = MiAddressToPpe(Address);
    LevelPteAddress[2] = MiAddressToPde(Address);

    if (!MiArm64EnsureTablePageMapped(RootPa))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    ParentPfn = RootPfn;

    /*
     * Descend L0->L3, allocating any missing table level. The three level
     * steps are identical except for the child PteAddress.
     */
    for (Level = 0; Level < 3; Level++)
    {
        Entry = Table[LevelIndex[Level]];

        if ((Level == 2) && ((Entry & 0x3ULL) == 0x1ULL))
        {
            /*
             * ARM3 creates user mappings as L3 pages. Replacing an existing
             * L2 block here would discard the other 511 pages in its 2 MB
             * range; a block is therefore an address-space conflict, not an
             * implicit request to demolish the mapping.
             */
            return STATUS_CONFLICTING_ADDRESSES;
        }

        if ((Entry & 0x3ULL) == 0)
        {
            Status = MiArm64AllocateCleanPage(Process, &ChildPfn);
            if (!NT_SUCCESS(Status))
            {
                if (AllocatedTable)
                {
                    MiArm64PruneEmptyUserPageTables(Process, Address);
                }
                return Status;
            }

            MiArm64InitializeUserTablePage(ChildPfn, LevelPteAddress[Level], ParentPfn);

            MiArm64WritePteEntry(&Table[LevelIndex[Level]], ARM64_MAKE_TABLE_DESCRIPTOR(ChildPfn));
            AllocatedTable = TRUE;
        }
        else if ((Entry & 0x3ULL) == 0x3ULL)
        {
            ChildPfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
        }
        else
        {
            return STATUS_CONFLICTING_ADDRESSES;
        }

        if (!MiArm64EnsureTablePageMapped((ULONG64)ChildPfn << PAGE_SHIFT))
        {
            if (AllocatedTable)
            {
                MiArm64PruneEmptyUserPageTables(Process, Address);
            }
            return STATUS_INVALID_PARAMETER;
        }

        TablePfn[Level] = ChildPfn;
        Table = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(ChildPfn);
        ParentPfn = ChildPfn;
    }

    /* Every live descriptor must reference the PFN initialized for it. */
    OldIrql = MiAcquirePfnLock();
    Healthy = MiArm64UserTablePfnHealthy(TablePfn[0], LevelPteAddress[0], RootPfn) &&
              MiArm64UserTablePfnHealthy(TablePfn[1], LevelPteAddress[1], TablePfn[0]) &&
              MiArm64UserTablePfnHealthy(TablePfn[2], LevelPteAddress[2], TablePfn[1]);
    MiReleasePfnLock(OldIrql);

    if (!Healthy)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT,
                     0xA642,
                     (ULONG_PTR)Address,
                     TablePfn[0],
                     TablePfn[2]);
    }

    *PointerPte = (PMMPTE)&Table[LevelIndex[3]];
    if (L3TablePfn != NULL)
    {
        *L3TablePfn = TablePfn[2];
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MiArm64ProbeAndLockUserPages(
    _Inout_ PMDL Mdl,
    _In_ PVOID StartAddress,
    _In_ ULONG TotalPages,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation,
    _In_ PEPROCESS CurrentProcess)
{
    PPFN_NUMBER MdlPages;
    PVOID PageAddress;
    ULONG PageIndex;
    ULONG Attempt;
    NTSTATUS Status;
    PFN_NUMBER PageFrameIndex;
    PFN_NUMBER L3Pfn;
    PMMPFN Pfn1;
    volatile ULONG64 *PteSlot;
    MMPTE Pte;
    ULONG NotPresentFaultCode;

    ASSERT(TotalPages != 0);
    ASSERT(CurrentProcess == PsGetCurrentProcess());

    NotPresentFaultCode = (Operation == IoReadAccess) ? 0x0 : 0x2;

    MdlPages = (PPFN_NUMBER)(Mdl + 1);
    PageAddress = PAGE_ALIGN(StartAddress);
    *MdlPages = LIST_HEAD;

    if (Operation != IoReadAccess)
    {
        Mdl->MdlFlags |= MDL_WRITE_OPERATION;
    }
    else
    {
        Mdl->MdlFlags &= ~(MDL_WRITE_OPERATION);
    }

    Mdl->MdlFlags |= MDL_PAGES_LOCKED;
    Mdl->Process = CurrentProcess;

    InterlockedExchangeAddSizeT(&CurrentProcess->NumberOfLockedPages,
                                TotalPages);

    /*
     * Hold the working-set lock across resident runs: the KSEG0 L3 slot
     * advances linearly within each 2MB chunk, so the steady state costs one
     * walk and one lock round-trip per chunk instead of two walks and a lock
     * cycle per page. The lock is dropped only to fault a page in, and the
     * slot is re-resolved after every fault (and at every table-page
     * boundary, which the pointer alignment check detects).
     */
    MiLockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());

    PteSlot = NULL;
    for (PageIndex = 0; PageIndex < TotalPages; PageIndex++)
    {
        MdlPages[PageIndex] = LIST_HEAD;

        for (Attempt = 0; ; Attempt++)
        {
            ULONG FaultCode;

            if ((PteSlot == NULL) || (((ULONG_PTR)PteSlot & (PAGE_SIZE - 1)) == 0))
            {
                PteSlot = (volatile ULONG64 *)MiArm64UserPteKseg0ForPfn(PageAddress, &L3Pfn);
            }

            FaultCode = NotPresentFaultCode;
            if (PteSlot != NULL)
            {
                Pte.u.Long = *PteSlot;
                if (Pte.u.Hard.Valid)
                {
                    if ((Operation == IoReadAccess) ||
                        (MI_IS_PAGE_WRITEABLE(&Pte) && MI_IS_PAGE_DIRTY(&Pte)))
                    {
                        break;
                    }

                    FaultCode = 0x3;
                }
            }

            if (Attempt >= 4)
            {
                Status = STATUS_ACCESS_VIOLATION;
                goto FailUnlock;
            }

            MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            Status = MmAccessFaultEx(FaultCode, PageAddress, AccessMode, NULL, FALSE);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            KeInvalidateTlbEntry(PageAddress);
            MiLockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            PteSlot = NULL;
        }

        PageFrameIndex = PFN_FROM_PTE(&Pte);
        Pfn1 = MiGetPfnEntry(PageFrameIndex);
        if (Pfn1 == NULL)
        {
            Status = STATUS_ACCESS_VIOLATION;
            goto FailUnlock;
        }

        ASSERT(CurrentProcess->PhysicalVadRoot == NULL);
        MiReferenceProbedPageAndBumpLockCount(Pfn1);

        MdlPages[PageIndex] = PageFrameIndex;
        PageAddress = (PVOID)((ULONG_PTR)PageAddress + PAGE_SIZE);
        PteSlot = (PteSlot != NULL) ? (PteSlot + 1) : NULL;
    }

    MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
    return STATUS_SUCCESS;

FailUnlock:
    MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());

Cleanup:
    ASSERT(!NT_SUCCESS(Status));
    MmUnlockPages(Mdl);
    return Status;
}

FORCEINLINE
ULONG64
MiArm64ReadPteEntry(
    _In_ volatile ULONG64 *Entry)
{
    return *Entry;
}

FORCEINLINE
VOID
MiArm64WritePteEntry(
    _Inout_ volatile ULONG64 *Entry,
    _In_ ULONG64 Value)
{
    *Entry = Value;
    MiArm64CleanEntryToPoC(Entry);
}

FORCEINLINE
ULONG64
MiArm64ExchangePteEntry(
    _Inout_ volatile ULONG64 *Entry,
    _In_ ULONG64 Value)
{
    ULONG64 OldValue = MiArm64ReadPteEntry(Entry);

    *Entry = Value;
    MiArm64CleanEntryToPoC(Entry);
    return OldValue;
}

static
ULONG
MiArm64CountUserTableEntries(
    _In_reads_(PTE_PER_PAGE) volatile ULONG64 *Table,
    _In_ ULONG Level,
    _Out_ PULONG ShareReferences)
{
    ULONG Index;
    ULONG UsedEntries = 0;
    ULONG SharedEntries = 0;

    for (Index = 0; Index < PTE_PER_PAGE; Index++)
    {
        MMPTE Entry;

        Entry.u.Long = Table[Index];
        if (Entry.u.Long == 0)
        {
            continue;
        }

        UsedEntries++;
        if (Level < 3)
        {
            if ((Entry.u.Long & 1ULL) != 0)
            {
                SharedEntries++;
            }
        }
        else if (Entry.u.Hard.Valid || Entry.u.Soft.Transition)
        {
            SharedEntries++;
        }
    }

    *ShareReferences = SharedEntries;
    return UsedEntries;
}

static
MMPTE
MiArm64ClearKernelPte(
    _In_ PVOID Address,
    _Inout_ PMMPTE PointerPte,
    _In_ PMMPTE Kseg0Pte)
{
    MMPTE OldPte;

    OldPte.u.Long = PointerPte->u.Long;
    if (OldPte.u.Hard.Valid)
    {
        /* Sweep the I-cache only for pages that were EL1-executable */
        MiArm64MaintainPageCache(Address,
                                 MiArm64DcacheCleanInvalidate,
                                 OldPte.u.Hard.PrivilegedNoExecute == 0);
    }

    /* KeInvalidateTlbEntry opens with dsb ishst; the mirror write is sealed
     * by MiArm64CleanEntryToPoC inside the WriteTo helper */
    PointerPte->u.Long = 0;
    MiArm64SyncKernelLeafPteWriteTo(PointerPte, Kseg0Pte);
    KeInvalidateTlbEntry(Address);

    return OldPte;
}

static
MMPTE
MiArm64ClearUserPte(
    _In_ PVOID Address,
    _In_ PMI_ARM64_USER_PTE_WALK Walk)
{
    MMPTE OldPte;

    OldPte.u.Long = Walk->PteValue;
    if (OldPte.u.Hard.Valid && (OldPte.u.Hard.UserNoExecute == 0))
    {
        /*
         * I-fetch coherency publish for executable pages only. Data pages
         * need no PoC maintenance here: CPU observers are PIPT-coherent and
         * DMA is handled at the I/O layer by KeFlushIoBuffers (the ARM3
         * fault path installs data PTEs with no D-publish at all).
         */
        MiArm64PublishPageByPfnAlias(OldPte.u.Hard.PageFrameNumber, TRUE);
    }

    if (OldPte.u.Long != 0)
    {
        MiArm64WritePteEntry(Walk->PointerPte, 0);
        MiArm64InvalidateUserAddress(Address);
    }

    return OldPte;
}

static
BOOLEAN
MiArm64ConsumeDirtyState(
    _In_ MMPTE OldPte,
    _In_ BOOLEAN IsPhysical)
{
    PFN_NUMBER PageFrameNumber;
    PMMPFN PfnEntry;
    KIRQL OldIrql;
    BOOLEAN Dirty;

    if (!OldPte.u.Hard.Valid)
    {
        return FALSE;
    }

    if (MI_IS_PAGE_DIRTY(&OldPte))
    {
        return TRUE;
    }

    if (IsPhysical)
    {
        return FALSE;
    }

    PageFrameNumber = OldPte.u.Hard.PageFrameNumber;
    if ((PageFrameNumber == 0) || (PageFrameNumber > MmHighestPhysicalPage))
    {
        return FALSE;
    }

    Dirty = FALSE;
    OldIrql = MiAcquirePfnLock();
    PfnEntry = MiGetPfnEntry(PageFrameNumber);
    if ((PfnEntry != NULL) && PfnEntry->u3.e1.Modified)
    {
        PfnEntry->u3.e1.Modified = 0;
        Dirty = TRUE;
    }
    MiReleasePfnLock(OldIrql);

    return Dirty;
}

static
VOID
MiArm64PreserveDirtyStateForProtect(
    _In_ MMPTE OldPte,
    _In_ MMPTE NewPte)
{
    PFN_NUMBER PageFrameNumber;
    PMMPFN PfnEntry;
    KIRQL OldIrql;

    if (!OldPte.u.Hard.Valid ||
        !MI_IS_PAGE_DIRTY(&OldPte) ||
        MI_IS_PAGE_DIRTY(&NewPte))
    {
        return;
    }

    PageFrameNumber = OldPte.u.Hard.PageFrameNumber;
    if ((PageFrameNumber == 0) || (PageFrameNumber > MmHighestPhysicalPage))
    {
        return;
    }

    OldIrql = MiAcquirePfnLock();
    PfnEntry = MiGetPfnEntry(PageFrameNumber);
    if (PfnEntry != NULL)
    {
        PfnEntry->u3.e1.Modified = 1;
    }
    MiReleasePfnLock(OldIrql);
}

static
VOID
MiArm64ReleaseMappedPageReference(
    _In_ PFN_NUMBER PageFrameNumber)
{
    KIRQL OldIrql;
    PMMPFN PfnEntry;

    if ((PageFrameNumber == 0) || (PageFrameNumber > MmHighestPhysicalPage))
    {
        return;
    }

    OldIrql = MiAcquirePfnLock();
    PfnEntry = MiGetPfnEntry(PageFrameNumber);
    if ((PfnEntry != NULL) && (PfnEntry->u2.ShareCount > 0))
    {
        if (--PfnEntry->u2.ShareCount == 0)
        {
            PfnEntry->u3.e1.PageLocation = TransitionPage;
        }
    }
    MiReleasePfnLock(OldIrql);
}

static
BOOLEAN
MiArm64ReleaseUserPageTableReferenceLockedInternal(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare,
    _In_ BOOLEAN ReleaseLeafEntry,
    _In_ PMI_ARM64_USER_PTE_WALK Walk)
{
    ULONG Level, TopLevel;
    ULONG Index[3];
    ULONG ActualEntries;
    ULONG ActualShareReferences;
    ULONG ExpectedShareCount;
    PMMPFN Pfn[4];
    BOOLEAN DecrementUsedEntry;
    BOOLEAN LeafTableDeleted = FALSE;

    ASSERT(Process != NULL);
    UNREFERENCED_PARAMETER(Process);
    MI_ASSERT_PFN_LOCK_HELD();

    if (ReleaseLeafEntry)
    {
        /* A removed leaf can only be accounted against an L3 table. */
        if (Walk->Depth < 3)
        {
            return FALSE;
        }
        TopLevel = 3;
    }
    else
    {
        /* A failed table build may stop at any allocated child level. */
        if (Walk->Depth == 0)
        {
            return FALSE;
        }
        TopLevel = min(Walk->Depth, 3);
    }

    Index[0] = ((ULONG64)(ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
    Index[1] = ((ULONG64)(ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK;
    Index[2] = ((ULONG64)(ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64;

    for (Level = 0; Level <= TopLevel; Level++)
    {
        Pfn[Level] = MiGetPfnEntry(Walk->LevelPfn[Level]);
        if (Pfn[Level] == NULL)
        {
            return FALSE;
        }
    }

    if (ReleaseLeafShare)
    {
        MiDecrementShareCount(Pfn[3], Walk->LevelPfn[3]);
    }

    /*
     * Cascade from the deepest table to L1 (the root is never freed).
     * Normal unmap starts by releasing the removed leaf's used-entry
     * reference. Rollback starts with an already-empty table, then releases
     * parent descriptor references as each child is removed.
     */
    DecrementUsedEntry = ReleaseLeafEntry;
    for (Level = TopLevel; Level >= 1; Level--)
    {
        PMMPFN TablePfn = Pfn[Level];
        PMMPFN ParentPfn = Pfn[Level - 1];

        if (DecrementUsedEntry)
        {
            if (TablePfn->OriginalPte.u.Soft.UsedPageTableEntries > 0)
            {
                TablePfn->OriginalPte.u.Soft.UsedPageTableEntries--;
            }
            else
            {
                break;
            }
        }

        ActualEntries = MiArm64CountUserTableEntries(Walk->LevelTable[Level],
                                                     Level,
                                                     &ActualShareReferences);
        ExpectedShareCount = ActualShareReferences + 1;
        if ((TablePfn->OriginalPte.u.Soft.UsedPageTableEntries != ActualEntries) ||
            (TablePfn->u2.ShareCount != ExpectedShareCount))
        {
            KeBugCheckEx(MEMORY_MANAGEMENT,
                         0xA643,
                         (ULONG_PTR)Address,
                         Walk->LevelPfn[Level],
                         ((ULONG_PTR)TablePfn->OriginalPte.u.Soft.UsedPageTableEntries << 48) |
                         ((ULONG_PTR)ActualEntries << 32) |
                         ((ULONG_PTR)TablePfn->u2.ShareCount << 16) |
                         ExpectedShareCount);
        }

        if (ActualEntries != 0)
        {
            break;
        }

        ASSERT(TablePfn->u2.ShareCount == 1);

        MiArm64WritePteEntry(&Walk->LevelTable[Level - 1][Index[Level - 1]], 0);
        MiArm64InvalidateUserAddress(Address);
        MiDecrementShareCount(ParentPfn, Walk->LevelPfn[Level - 1]);
        MI_SET_PFN_DELETED(TablePfn);
        MiDecrementShareCount(TablePfn, Walk->LevelPfn[Level]);
        DecrementUsedEntry = TRUE;
        if (Level == 3)
        {
            LeafTableDeleted = TRUE;
        }

        if (Level == 1)
        {
            if (ParentPfn->OriginalPte.u.Soft.UsedPageTableEntries > 0)
            {
                ParentPfn->OriginalPte.u.Soft.UsedPageTableEntries--;
            }
        }
    }

    return LeafTableDeleted;
}

static
VOID
MiArm64ReleaseUserPageTableReferenceInternal(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare,
    _In_ BOOLEAN ReleaseLeafEntry,
    _In_ PMI_ARM64_USER_PTE_WALK Walk)
{
    KIRQL OldIrql;

    OldIrql = MiAcquirePfnLock();
    MiArm64ReleaseUserPageTableReferenceLockedInternal(Process,
                                                       Address,
                                                       ReleaseLeafShare,
                                                       ReleaseLeafEntry,
                                                       Walk);
    MiReleasePfnLock(OldIrql);
}

BOOLEAN
MiArm64ReleaseUserPageTableReferenceLocked(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare,
    _In_ PMI_ARM64_USER_PTE_WALK Walk)
{
    return MiArm64ReleaseUserPageTableReferenceLockedInternal(Process,
                                                              Address,
                                                              ReleaseLeafShare,
                                                              TRUE,
                                                              Walk);
}

VOID
MiArm64ReleaseUserPageTableReference(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare,
    _In_ PMI_ARM64_USER_PTE_WALK Walk)
{
    MiArm64ReleaseUserPageTableReferenceInternal(Process, Address, ReleaseLeafShare, TRUE, Walk);
}

VOID
MiArm64PruneEmptyUserPageTables(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    MI_ARM64_USER_PTE_WALK Walk;

    /* A failed walk still records every table level it reached. */
    MiArm64GetUserPteAddressForProcess(Process, Address, &Walk);
    MiArm64ReleaseUserPageTableReferenceInternal(Process, Address, FALSE, FALSE, &Walk);
}

static
PFN_NUMBER
MiArm64GetUserPfn(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    MI_ARM64_USER_PTE_WALK Walk;
    MMPTE Pte;

    ASSERT(Address < MmSystemRangeStart);
    ASSERT(Process != NULL);

    if (!MiArm64GetUserPteAddressForProcess(Process, Address, &Walk))
    {
        return 0;
    }

    Pte.u.Long = Walk.PteValue;
    return Pte.u.Hard.Valid ? Pte.u.Hard.PageFrameNumber : 0;
}

static
ULONG
MiProtectionFromPte(
    _In_ MMPTE Pte);

static
BOOLEAN
MmDeleteVirtualMappingEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical);

NTSTATUS
NTAPI
MmCreateVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Page == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!MmIsPageInUse(Page))
    {
        DPRINT1("Page %Ix is not in use (Addr=%p Proc=%p)\n", Page, Address, Process);
#if defined(CONFIG_SMP)
        /*
         * SMP: Page may have been freed by another CPU during a race
         * in the section fault handler. Log and fail gracefully.
         */
        return STATUS_UNSUCCESSFUL;
#else
        KeBugCheck(MEMORY_MANAGEMENT);
#endif
    }

    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, FALSE);
}

NTSTATUS
NTAPI
MmCreateVirtualMappingUnsafe(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, FALSE);
}

NTSTATUS
NTAPI
MmCreatePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, TRUE);
}

static
NTSTATUS
MmCreateVirtualMappingUnsafeEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical)
{
    PMMPTE PointerPte;
    MMPTE TempPte;
    ULONG ProtectionMask;
    PETHREAD CurrentThread = PsGetCurrentThread();

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    /* Ordinary mappings must reference an owned PFN. Explicit physical
       mappings may address reserved page zero through \Device\PhysicalMemory. */
    if ((Page == 0) && !IsPhysical)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);
    ASSERT(ProtectionMask != MM_NOACCESS);
    ASSERT(ProtectionMask != MM_ZERO_ACCESS);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);
        ASSERT(ProtectionMask != MM_WRITECOPY);
        ASSERT(ProtectionMask != MM_EXECUTE_WRITECOPY);

        /*
         * MiMakeSystemAddressValid may have to fault in the page table page
         * containing this PTE. Kernel page-table VAs are protected by the
         * system working set, so satisfy that helper's lock contract here.
         */
        MiLockWorkingSet(CurrentThread, &MmSystemCacheWs);
        MiMakeSystemAddressValid(MiAddressToPte(Address), PsGetCurrentProcess());
        MiUnlockWorkingSet(CurrentThread, &MmSystemCacheWs);
    }
    else
    {
        MMPTE OldPte, FinalPte;
        PFN_NUMBER PteFrame;
        NTSTATUS Status;
        BOOLEAN OldEntryEmpty, OldEntryHasPageTableShare;

        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSet(Process, PsGetCurrentThread());

        Status = MiArm64EnsureUserPte(Process, Address, &PointerPte, &PteFrame);
        if (!NT_SUCCESS(Status))
        {
            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
            return Status;
        }

        OldPte = *PointerPte;
        OldEntryEmpty = (OldPte.u.Long == 0);
        OldEntryHasPageTableShare = OldPte.u.Hard.Valid || OldPte.u.Soft.Transition;

        /* Bit-for-bit the hand-built user PTE (the protection mask supplies
         * NonGlobal on every reachable protection) */
        MI_MAKE_HARDWARE_PTE_USER(&FinalPte, MiAddressToPte(Address), ProtectionMask, Page);

        if (FinalPte.u.Hard.Writable)
        {
            MI_MAKE_DIRTY_PAGE(&FinalPte);
        }
        else
        {
            FinalPte.u.Hard.NotDirty = 1;
        }

        /* One PFN-lock section covers the data-page reference and the
         * page-table accounting */
        {
            KIRQL OldIrql;

            OldIrql = MiAcquirePfnLock();

            if (!IsPhysical)
            {
                PMMPFN Pfn1 = MiGetPfnEntry(Page);

                if (Pfn1 == NULL)
                {
                    MiReleasePfnLock(OldIrql);
                    MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                    return STATUS_INVALID_PARAMETER;
                }

                Pfn1->u2.ShareCount++;
                Pfn1->u3.e1.PageLocation = ActiveAndValid;
                MiArm64SyncMappedPfnCacheAttribute(Pfn1, FinalPte);
            }

            if (!OldEntryHasPageTableShare || OldEntryEmpty)
            {
                PMMPFN TablePfnEntry = MiGetPfnEntry(PteFrame);

                ASSERT(TablePfnEntry != NULL);
                if (TablePfnEntry != NULL)
                {
                    if (!OldEntryHasPageTableShare)
                    {
                        TablePfnEntry->u2.ShareCount++;
                    }
                    if (OldEntryEmpty)
                    {
                        TablePfnEntry->OriginalPte.u.Soft.UsedPageTableEntries++;
                        ASSERT(TablePfnEntry->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
                    }
                }
            }

            MiReleasePfnLock(OldIrql);
        }

        if (OldPte.u.Hard.Valid && OldPte.u.Hard.NotLargePage)
        {
            MiArm64InvalidatePageByPfnAlias(OldPte.u.Hard.PageFrameNumber);
        }

        PointerPte->u.Long = FinalPte.u.Long;
        MiArm64CleanEntryToPoC(PointerPte);
        if (FinalPte.u.Hard.UserNoExecute == 0)
        {
            /* I-fetch coherency publish for executable mappings only (see
             * MiArm64ClearUserPte for the data-page rationale) */
            MiArm64PublishPageByPfnAlias(Page, TRUE);
        }
        MiArm64InvalidateUserAddress(Address);

        MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());

        return STATUS_SUCCESS;
    }

    /* Kernel address path - uses self-mapping */
    PointerPte = MiAddressToPte(Address);
    MI_MAKE_HARDWARE_PTE_KERNEL(&TempPte, PointerPte, ProtectionMask, Page);

    if (!IsPhysical)
    {
        KIRQL OldIrql = MiAcquirePfnLock();
        PMMPFN Pfn1 = MiGetPfnEntry(Page);

        Pfn1->u2.ShareCount++;
        Pfn1->u3.e1.PageLocation = ActiveAndValid;
        MiArm64SyncMappedPfnCacheAttribute(Pfn1, TempPte);
        MiReleasePfnLock(OldIrql);
    }

    if (PointerPte->u.Hard.Valid)
    {
        MiArm64MaintainPageCache(Address, MiArm64DcacheCleanInvalidate, FALSE);
    }

    {
        ULONG_PTR OldPteValue = PointerPte->u.Long;

        PointerPte->u.Long = TempPte.u.Long;
        MiArm64SyncKernelLeafPteWrite(PointerPte);

        if (OldPteValue != 0)
        {
            DPRINT1("Mapping collision at %p\n", Address);
            KeBugCheck(MEMORY_MANAGEMENT);
        }
    }

    KeInvalidateTlbEntry(Address);

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
MmDeleteVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    return MmDeleteVirtualMappingEx(Process, Address, WasDirty, Page, FALSE);
}

BOOLEAN
NTAPI
MmDeletePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    return MmDeleteVirtualMappingEx(Process, Address, WasDirty, Page, TRUE);
}

static
BOOLEAN
MmDeleteVirtualMappingEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical)
{
    MMPTE OldPte;
    MI_ARM64_USER_PTE_WALK Walk;
    BOOLEAN ProcessWorkingSetLocked = FALSE;

    OldPte.u.Long = 0;
    Walk.Depth = 0;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Process == NULL)
    {
        PMMPTE Kseg0Pte;

        ASSERT(Address >= MmSystemRangeStart);

        /* One TTBR1 walk resolves both the PDE-validity gate and the KSEG0
         * mirror slot the clear helper needs */
        Kseg0Pte = MiArm64KernelPteKseg0(Address);
        if (Kseg0Pte != NULL)
        {
            OldPte = MiArm64ClearKernelPte(Address, MiAddressToPte(Address), Kseg0Pte);
        }
    }
    else
    {
        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
        ProcessWorkingSetLocked = TRUE;

        if (MiArm64GetUserPteAddress(Address, &Walk))
        {
            OldPte = MiArm64ClearUserPte(Address, &Walk);
        }
    }

    if (OldPte.u.Long != 0)
    {
        if (WasDirty)
        {
            *WasDirty = MiArm64ConsumeDirtyState(OldPte, IsPhysical);
        }
        if (Page)
            *Page = OldPte.u.Hard.PageFrameNumber;
    }
    else
    {
        if (WasDirty)
            *WasDirty = FALSE;
        if (Page)
            *Page = 0;
    }

    if (!IsPhysical && OldPte.u.Hard.Valid)
    {
        MiArm64ReleaseMappedPageReference(OldPte.u.Hard.PageFrameNumber);
    }

    if (Process != NULL && OldPte.u.Long != 0)
    {
        MiArm64ReleaseUserPageTableReference(Process,
                                             Address,
                                             OldPte.u.Hard.Valid || OldPte.u.Soft.Transition,
                                             &Walk);
    }

    if (ProcessWorkingSetLocked)
    {
        MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    }

    return OldPte.u.Long != 0;
}

NTSTATUS
NTAPI
MmCreatePageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ SWAPENTRY SwapEntry)
{
    ASSERT(Process != NULL);
    ASSERT(Address < MmSystemRangeStart);
    ASSERT(Process == PsGetCurrentProcess());

    if (SwapEntry & ((ULONG_PTR)1 << (RTL_BITS_OF(SWAPENTRY) - 1)))
    {
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    {
        PMMPTE PointerPte;
        MMPTE OldPte;
        MMPTE NewPte;
        PFN_NUMBER PteFrame;

        NTSTATUS Status;

        Status = MiArm64EnsureUserPte(Process, Address, &PointerPte, &PteFrame);
        if (!NT_SUCCESS(Status))
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return Status;
        }

        OldPte.u.Long = PointerPte->u.Long;
        if (OldPte.u.Long != 0)
        {
            DPRINT1("Unexpected PTE 0x%I64x while creating pagefile mapping at %p\n",
                    OldPte.u.Long, Address);
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            KeBugCheckEx(MEMORY_MANAGEMENT, OldPte.u.Long, (ULONG_PTR)Process, (ULONG_PTR)Address, 0);
        }

        NewPte.u.Long = 0;
        NewPte.u.Soft.PageFileLow = (ULONG)MM_SWAP_FILE_FROM_ENTRY(SwapEntry);
        NewPte.u.Soft.PageFileHigh = (ULONG_PTR)MM_SWAP_OFFSET_FROM_ENTRY(SwapEntry);
        NewPte.u.Soft.Prototype = 0;
        NewPte.u.Soft.Protection = MM_READWRITE;

        MiArm64WritePteEntry((volatile ULONG64 *)PointerPte, NewPte.u.Long);
        MiArm64AccountUserLeafPte(PteFrame, FALSE, TRUE);
    }

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    return STATUS_SUCCESS;
}

VOID
NTAPI
MmDeletePageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Inout_ SWAPENTRY *SwapEntry)
{
    MMPTE OldPte;
    MI_ARM64_USER_PTE_WALK Walk;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    if (!MiArm64GetUserPteAddress(Address, &Walk))
    {
        *SwapEntry = 0;
        MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
        return;
    }

    OldPte.u.Long = MiArm64ExchangePteEntry(Walk.PointerPte, 0);

    if (OldPte.u.Hard.Valid ||
        OldPte.u.Soft.Prototype ||
        OldPte.u.Soft.Transition ||
        (OldPte.u.Soft.PageFileHigh == 0))
    {
        DPRINT1("Expected pagefile PTE at %p\n", Address);
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    *SwapEntry = (SWAPENTRY)MM_SWAP_ENTRY_FROM_FILE_OFFSET((ULONG_PTR)OldPte.u.Soft.PageFileLow,
                                                            (ULONG_PTR)OldPte.u.Soft.PageFileHigh);

    MiArm64ReleaseUserPageTableReference(Process, Address, FALSE, &Walk);

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

VOID
NTAPI
MmGetPageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ SWAPENTRY *SwapEntry)
{
    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    if (Address < MmSystemRangeStart)
    {
        MI_ARM64_USER_PTE_WALK Walk;

        MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());

        if (!MiArm64GetUserPteAddress(Address, &Walk))
        {
            *SwapEntry = 0;
        }
        else
        {
            MMPTE TempPte;
            TempPte.u.Long = Walk.PteValue;

            if (TempPte.u.Hard.Valid ||
                TempPte.u.Soft.Prototype ||
                TempPte.u.Soft.Transition ||
                (TempPte.u.Soft.PageFileHigh == 0))
                *SwapEntry = 0;
            else
                *SwapEntry = (SWAPENTRY)MM_SWAP_ENTRY_FROM_FILE_OFFSET(TempPte.u.Soft.PageFileLow,
                                                                       TempPte.u.Soft.PageFileHigh);
        }

        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    }
    else
    {
        PMMPTE PointerPte;

        MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());
        PointerPte = MiAddressToPte(Address);

        if (PointerPte->u.Hard.Valid ||
            PointerPte->u.Soft.Prototype ||
            PointerPte->u.Soft.Transition ||
            (PointerPte->u.Soft.PageFileHigh == 0))
            *SwapEntry = 0;
        else
            *SwapEntry = (SWAPENTRY)MM_SWAP_ENTRY_FROM_FILE_OFFSET(PointerPte->u.Soft.PageFileLow,
                                                                   PointerPte->u.Soft.PageFileHigh);

        MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
    }
}

BOOLEAN
NTAPI
MmIsPagePresent(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if the page table hierarchy is valid.
         * We must NOT call MiMakeSystemAddressValid(Address) here because:
         * 1. This function is called during page fault handling to check if
         *    a page is already present.
         * 2. MiMakeSystemAddressValid would try to fault in the page,
         *    causing infinite recursion.
         * 3. If the PDE is not valid, the page cannot be present.
         *
         * Use MiIsPdeForAddressValid to check page table validity without
         * causing recursive faults.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            /* No valid page table for this address - page cannot be present */
            return FALSE;
        }

        return MiAddressToPte(Address)->u.Hard.Valid != 0;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    return MiArm64ReadUserPtePhysically(Address, NULL);
}

BOOLEAN
NTAPI
MmIsDisabledPage(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    {
        ULONG64 PteValue;
        MMPTE TempPte;

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue))
            return FALSE;

        TempPte.u.Long = PteValue;
        if (!TempPte.u.Hard.Valid)
            return FALSE;

        return (TempPte.u.Hard.Writable == 0) && (TempPte.u.Hard.CopyOnWrite == 0);
    }
}

BOOLEAN
NTAPI
MmIsPageSwapEntry(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    {
        MI_ARM64_USER_PTE_WALK Walk;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
            return FALSE;

        MMPTE TempPte;

        TempPte.u.Long = Walk.PteValue;
        return !TempPte.u.Hard.Valid &&
               !TempPte.u.Soft.Prototype &&
               !TempPte.u.Soft.Transition &&
               (TempPte.u.Soft.PageFileHigh != 0);
    }
}

ULONG
NTAPI
MmGetPageProtect(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        PMMPTE PointerPte;

        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if page tables exist without faulting.
         * We cannot call MiMakeSystemAddressValid here as it could cause
         * infinite recursion during page fault handling.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            return PAGE_NOACCESS;
        }

        PointerPte = MiAddressToPte(Address);
        return PointerPte->u.Hard.Valid ? MiProtectionFromPte(*PointerPte) : PAGE_NOACCESS;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    {
        ULONG64 PteValue;
        MMPTE TempPte;

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue))
            return PAGE_NOACCESS;

        TempPte.u.Long = PteValue;
        return TempPte.u.Hard.Valid ? MiProtectionFromPte(TempPte) : PAGE_NOACCESS;
    }
}

VOID
NTAPI
MmSetPageProtect(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection)
{
    ULONG ProtectionMask;
    MMPTE TempPte, OldPte;

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);

    if (Address >= MmSystemRangeStart)
    {
        PMMPTE PointerPte;
        PMMPTE Kseg0Pte;

        ASSERT(Process == NULL);

        /* One TTBR1 walk resolves the validity gate and the mirror slot */
        Kseg0Pte = MiArm64KernelPteKseg0(Address);
        if (Kseg0Pte == NULL)
            return;

        PointerPte = MiAddressToPte(Address);
        OldPte.u.Long = PointerPte->u.Long;
        if (!OldPte.u.Hard.Valid)
            return;

        TempPte.u.Long = OldPte.u.Long;
        TempPte.u.Long &= ~PTE_PROTECT_MASK;
        TempPte.u.Long |= MmProtectToPteMaskKernel[ProtectionMask];

        if ((ProtectionMask != MM_NOACCESS) && !FlagOn(ProtectionMask, MM_GUARDPAGE))
        {
            TempPte.u.Hard.Valid = 1;
            TempPte.u.Hard.NotLargePage = 1;
        }
        else
        {
            TempPte.u.Hard.Valid = 0;
        }

        if (TempPte.u.Hard.Writable)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        PointerPte->u.Long = TempPte.u.Long;
        MiArm64SyncKernelLeafPteWriteTo(PointerPte, Kseg0Pte);

        MiArm64PreserveDirtyStateForProtect(OldPte, TempPte);

        if (OldPte.u.Long != TempPte.u.Long)
        {
            KeInvalidateTlbEntry(Address);
        }

        return;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    {
        MI_ARM64_USER_PTE_WALK Walk;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
        {
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        OldPte.u.Long = Walk.PteValue;

        TempPte.u.Long = 0;
        TempPte.u.Long |= MmProtectToPteMask[ProtectionMask];
        TempPte.u.Hard.PageFrameNumber = OldPte.u.Hard.PageFrameNumber;
        TempPte.u.Hard.Owner = 1;          /* User accessible (AP[0]=1) */
        TempPte.u.Hard.Shareability = 3;   /* Inner Shareable for SMP coherency */

        if ((ProtectionMask != MM_NOACCESS) && !FlagOn(ProtectionMask, MM_GUARDPAGE))
            TempPte.u.Hard.Valid = 1;

        if (OldPte.u.Hard.Accessed)
            TempPte.u.Hard.Accessed = 1;

        if (TempPte.u.Hard.Valid)
            TempPte.u.Hard.NotLargePage = 1;

        if (TempPte.u.Hard.Writable)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        OldPte.u.Long = MiArm64ExchangePteEntry(Walk.PointerPte, TempPte.u.Long);

        MiArm64PreserveDirtyStateForProtect(OldPte, TempPte);

        if (OldPte.u.Long != TempPte.u.Long)
        {
            MiArm64InvalidateUserAddress(Address);
        }
    }

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

VOID
NTAPI
MmSetDirtyBit(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN Dirty)
{
    if (Address >= MmSystemRangeStart)
    {
        PMMPTE PointerPte;
        PMMPTE Kseg0Pte;
        MMPTE OldPte, TempPte;

        ASSERT(Process == NULL);

        /* One TTBR1 walk resolves the validity gate and the mirror slot */
        Kseg0Pte = MiArm64KernelPteKseg0(Address);
        if (Kseg0Pte == NULL)
            return;

        PointerPte = MiAddressToPte(Address);
        OldPte.u.Long = PointerPte->u.Long;
        TempPte.u.Long = OldPte.u.Long;
        if (!TempPte.u.Hard.Valid)
            return;

        if (Dirty)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        PointerPte->u.Long = TempPte.u.Long;
        MiArm64SyncKernelLeafPteWriteTo(PointerPte, Kseg0Pte);

        if (OldPte.u.Long != TempPte.u.Long)
        {
            KeInvalidateTlbEntry(Address);
        }

        return;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    {
        MI_ARM64_USER_PTE_WALK Walk;
        MMPTE OldPte;
        MMPTE TempPte;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
            goto DirtyBitDone;

        OldPte.u.Long = Walk.PteValue;
        TempPte.u.Long = OldPte.u.Long;

        if (!TempPte.u.Hard.Valid)
            goto DirtyBitDone;

        if (Dirty)
            MI_MAKE_DIRTY_PAGE(&TempPte);
        else
            MI_MAKE_CLEAN_PAGE(&TempPte);

        MiArm64WritePteEntry(Walk.PointerPte, TempPte.u.Long);

        if (OldPte.u.Long != TempPte.u.Long)
        {
            MiArm64InvalidateUserAddress(Address);
        }
    }

DirtyBitDone:
    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

PFN_NUMBER
NTAPI
MmGetPfnForProcess(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;
    PFN_NUMBER PageFrame = 0;

    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if page tables exist without faulting.
         * We cannot call MiMakeSystemAddressValid here as it could cause
         * infinite recursion during page fault handling.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            return 0;
        }

        PointerPte = MiAddressToPte(Address);
        if (PointerPte->u.Hard.Valid)
            PageFrame = PointerPte->u.Hard.PageFrameNumber;

        return PageFrame;
    }

    ASSERT(Process != NULL);
    return MiArm64GetUserPfn(Process, Address);
}

static
ULONG
MiProtectionFromPte(
    _In_ MMPTE Pte)
{
    ULONG Mask = Pte.u.Long & PTE_PROTECT_MASK;

    for (ULONG i = 0; i < ARRAYSIZE(MmProtectToPteMask); ++i)
    {
        if ((MmProtectToPteMask[i] & PTE_PROTECT_MASK) == Mask)
            return MmProtectToValue[i];
    }

    return PAGE_NOACCESS;
}
