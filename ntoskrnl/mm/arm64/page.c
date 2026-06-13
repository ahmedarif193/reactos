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

#define MI_ARM64_SWAP_ENTRY_MARKER 0x400ULL
#define MI_ARM64_SWAP_FILE_FROM_ENTRY(Entry) ((ULONG)((Entry) & 0x0F))
#define MI_ARM64_SWAP_OFFSET_FROM_ENTRY(Entry) ((ULONG_PTR)((Entry) >> 11))
#define MI_ARM64_SWAP_ENTRY_FROM_FILE_OFFSET(File, Offset) \
    ((SWAPENTRY)((File) | ((ULONG_PTR)(Offset) << 11) | MI_ARM64_SWAP_ENTRY_MARKER))

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
MiIsPageTablePresent(
    _In_ PVOID Address);

BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte,
    _Out_opt_ PULONG OutDepth);

typedef struct _MI_ARM64_USER_PTE_WALK
{
    volatile ULONG64 *PointerPte;
    ULONG64 PteValue;
    ULONG Depth;
} MI_ARM64_USER_PTE_WALK, *PMI_ARM64_USER_PTE_WALK;

static
BOOLEAN
MiArm64EnsureTablePageMapped(
    _In_ ULONG64 TablePa)
{
    PFN_NUMBER Pfn;

    if ((TablePa & (PAGE_SIZE - 1)) != 0)
    {
        return FALSE;
    }

    Pfn = (PFN_NUMBER)(TablePa >> PAGE_SHIFT);
    if ((Pfn == 0) || (Pfn > MmHighestPhysicalPage))
    {
        return FALSE;
    }

    MiArm64MapKseg0Page(Pfn);
    return TRUE;
}

static
BOOLEAN
MiArm64GetUserPteAddressForProcess(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk);

static
BOOLEAN
MiArm64GetUserPteAddress(
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk);

static
MMPTE
MiArm64ClearKernelPte(
    _In_ PVOID Address,
    _Inout_ PMMPTE PointerPte);

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

static
VOID
MiArm64ReleaseUserPageTableReference(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare);

static
VOID
MiArm64SynchronizeUserTablePfn(
    _Inout_ PEPROCESS Process,
    _In_ PFN_NUMBER TablePfn,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER ParentPfn,
    _In_ ULONG Level,
    _In_reads_(PTE_PER_PAGE) volatile ULONG64 *Table);

typedef enum _MI_ARM64_DCACHE_OPERATION
{
    MiArm64DcacheInvalidate,
    MiArm64DcacheCleanInvalidate
} MI_ARM64_DCACHE_OPERATION;

FORCEINLINE
VOID
MiArm64CacheLineSizes(
    _Out_ PULONG DcacheLineSize,
    _Out_ PULONG IcacheLineSize)
{
    KiArm64GetCacheLineSizes(DcacheLineSize, IcacheLineSize);
}

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

    MiArm64CacheLineSizes(&DcacheLineSize, &IcacheLineSize);
    Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    __asm__ __volatile__("dsb sy" ::: "memory");
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
    __asm__ __volatile__("dsb sy" ::: "memory");

    if (SweepIcache)
    {
        for (ULONG_PTR Offset = 0; Offset < PAGE_SIZE; Offset += IcacheLineSize)
        {
            __asm__ __volatile__("ic ivau, %0" :: "r"(Va + Offset) : "memory");
        }
        __asm__ __volatile__("dsb sy" ::: "memory");
    }

    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
MiArm64CleanInvalidateCacheLine(
    _In_ PVOID Address)
{
    __asm__ __volatile__("dmb ish" ::: "memory");
    __asm__ __volatile__("dc civac, %0" :: "r"(Address) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
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
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ PFN_NUMBER PageFrameNumber,
    _In_ MMPTE FinalPte)
{
    PMMPFN Pfn1;
    MI_PFN_CACHE_ATTRIBUTE OldCache;
    MI_PFN_CACHE_ATTRIBUTE NewCache;

    Pfn1 = MiGetPfnEntry(PageFrameNumber);
    if (Pfn1 == NULL)
    {
        return;
    }

    OldCache = Pfn1->u3.e1.CacheAttribute;
    NewCache = MiGetPteCacheAttribute(&FinalPte);
    if (OldCache == NewCache)
    {
        return;
    }

    Pfn1->u3.e1.CacheAttribute = NewCache;
}

static
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

    Walk->PointerPte = NULL;
    Walk->PteValue = 0;
    Walk->Depth = 0;

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

    L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
    L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

    if (!MiArm64EnsureTablePageMapped(RootPa))
    {
        return FALSE;
    }

    L0Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
    {
        return FALSE;
    }
    Walk->Depth = 1;

    L1Pa = L0Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L1Pa))
    {
        return FALSE;
    }

    L1Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1Pa);
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
    if (!MiArm64EnsureTablePageMapped(L2Pa))
    {
        return FALSE;
    }

    L2Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2Pa);
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
    if (!MiArm64EnsureTablePageMapped(L3Pa))
    {
        return FALSE;
    }

    L3Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L3Pa);
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
BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte,
    _Out_opt_ PULONG OutDepth)
{
    MI_ARM64_USER_PTE_WALK Walk;

    if (!MiArm64GetUserPteAddress(Address, &Walk))
    {
        if (OutPte) *OutPte = Walk.PteValue;
        if (OutDepth) *OutDepth = Walk.Depth;
        return FALSE;
    }

    if (OutPte) *OutPte = Walk.PteValue;
    if (OutDepth) *OutDepth = Walk.Depth;
    return ((Walk.PteValue & 0x3ULL) == 0x3ULL);
}

static
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
    _Inout_ PEPROCESS Process,
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

    Process->NumberOfPrivatePages++;
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
MiArm64IncrementUserPageTableReferences(
    _In_ PVOID Address)
{
    PFN_NUMBER PteFrame;

    if (MiArm64UserPteKseg0ForPfn(Address, &PteFrame) == NULL)
    {
        return;
    }

    MiArm64AccountUserLeafPte(PteFrame, FALSE, TRUE);
}

NTSTATUS
MiArm64EnsureUserPte(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _Outptr_ PMMPTE *PointerPte)
{
    ULONG64 Ttbr0, RootPa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG64 Entry;
    PFN_NUMBER RootPfn, L1Pfn, L2Pfn, L3Pfn;
    MMPTE TempPte;
    NTSTATUS Status;

    *PointerPte = NULL;

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
    L0Idx = ((ULONG64)(ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64;
    L3Idx = ((ULONG64)(ULONG_PTR)Address >> PTI_SHIFT) & PTI_MASK_ARM64;

    if (!MiArm64EnsureTablePageMapped(RootPa))
    {
        return STATUS_INVALID_PARAMETER;
    }

    L0Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    Entry = L0Table[L0Idx];
    if ((Entry & 0x3ULL) == 0)
    {
        Status = MiArm64AllocateCleanPage(Process, &L1Pfn);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        MiArm64InitializeUserTablePage(Process, L1Pfn, MiAddressToPxe(Address), RootPfn);

        TempPte = ValidKernelPde;
        TempPte.u.Hard.PageFrameNumber = L1Pfn;
        TempPte.u.Hard.Owner = 0;
        L0Table[L0Idx] = TempPte.u.Long;
        MiArm64CleanEntryToPoC(&L0Table[L0Idx]);
    }
    else if ((Entry & 0x3ULL) == 0x3ULL)
    {
        L1Pfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
    }
    else
    {
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (!MiArm64EnsureTablePageMapped((ULONG64)L1Pfn << PAGE_SHIFT))
    {
        return STATUS_INVALID_PARAMETER;
    }

    L1Table = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(L1Pfn);
    MiArm64SynchronizeUserTablePfn(Process, L1Pfn, MiAddressToPxe(Address), RootPfn, 1, L1Table);
    Entry = L1Table[L1Idx];
    if ((Entry & 0x3ULL) == 0)
    {
        Status = MiArm64AllocateCleanPage(Process, &L2Pfn);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        MiArm64InitializeUserTablePage(Process, L2Pfn, MiAddressToPpe(Address), L1Pfn);

        TempPte = ValidKernelPde;
        TempPte.u.Hard.PageFrameNumber = L2Pfn;
        TempPte.u.Hard.Owner = 0;
        L1Table[L1Idx] = TempPte.u.Long;
        MiArm64CleanEntryToPoC(&L1Table[L1Idx]);
    }
    else if ((Entry & 0x3ULL) == 0x3ULL)
    {
        L2Pfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
    }
    else
    {
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (!MiArm64EnsureTablePageMapped((ULONG64)L2Pfn << PAGE_SHIFT))
    {
        return STATUS_INVALID_PARAMETER;
    }

    L2Table = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(L2Pfn);
    MiArm64SynchronizeUserTablePfn(Process, L2Pfn, MiAddressToPpe(Address), L1Pfn, 2, L2Table);
    Entry = L2Table[L2Idx];
    if ((Entry & 0x3ULL) == 0x1ULL)
    {
        L2Table[L2Idx] = 0;
        MiArm64CleanEntryToPoC(&L2Table[L2Idx]);
        __asm__ __volatile__("dsb ishst\n\t"
                             "tlbi vaale1is, %0\n\t"
                             "dsb ish\n\t"
                             "isb"
                             :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");
        Entry = 0;
    }

    if ((Entry & 0x3ULL) == 0)
    {
        Status = MiArm64AllocateCleanPage(Process, &L3Pfn);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        MiArm64InitializeUserTablePage(Process, L3Pfn, MiAddressToPde(Address), L2Pfn);

        TempPte = ValidKernelPde;
        TempPte.u.Hard.PageFrameNumber = L3Pfn;
        TempPte.u.Hard.Owner = 0;
        L2Table[L2Idx] = TempPte.u.Long;
        MiArm64CleanEntryToPoC(&L2Table[L2Idx]);
    }
    else if ((Entry & 0x3ULL) == 0x3ULL)
    {
        L3Pfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
    }
    else
    {
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (!MiArm64EnsureTablePageMapped((ULONG64)L3Pfn << PAGE_SHIFT))
    {
        return STATUS_INVALID_PARAMETER;
    }

    L3Table = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(L3Pfn);
    MiArm64SynchronizeUserTablePfn(Process, L3Pfn, MiAddressToPde(Address), L2Pfn, 3, L3Table);
    *PointerPte = (PMMPTE)&L3Table[L3Idx];
    return STATUS_SUCCESS;
}

static
NTSTATUS
MiArm64PrepareUserPageForMdl(
    _In_ PEPROCESS Process,
    _In_ PVOID PageAddress,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ LOCK_OPERATION Operation)
{
    ULONG Attempt;
    NTSTATUS Status = STATUS_ACCESS_VIOLATION;
    ULONG NotPresentFaultCode;

    NotPresentFaultCode = (Operation == IoReadAccess) ? 0x0 : 0x2;

    for (Attempt = 0; Attempt < 4; Attempt++)
    {
        MI_ARM64_USER_PTE_WALK Walk;
        MMPTE Pte;

        if (MiArm64GetUserPteAddressForProcess(Process, PageAddress, &Walk))
        {
            Pte.u.Long = Walk.PteValue;
            if (Pte.u.Hard.Valid)
            {
                if ((Operation == IoReadAccess) ||
                    (MI_IS_PAGE_WRITEABLE(&Pte) && MI_IS_PAGE_DIRTY(&Pte)))
                {
                    return STATUS_SUCCESS;
                }

                Status = MmAccessFaultEx(0x3, PageAddress, AccessMode, NULL, FALSE);
            }
            else
            {
                Status = MmAccessFaultEx(NotPresentFaultCode, PageAddress, AccessMode, NULL, FALSE);
            }
        }
        else
        {
            Status = MmAccessFaultEx(NotPresentFaultCode, PageAddress, AccessMode, NULL, FALSE);
        }

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        KeInvalidateTlbEntry(PageAddress);
    }

    return STATUS_ACCESS_VIOLATION;
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
    NTSTATUS Status;
    PFN_NUMBER PageFrameIndex;
    PMMPFN Pfn1;

    ASSERT(TotalPages != 0);
    ASSERT(CurrentProcess == PsGetCurrentProcess());

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

    for (PageIndex = 0; PageIndex < TotalPages; PageIndex++)
    {
        MI_ARM64_USER_PTE_WALK Walk;
        MMPTE Pte;

        *MdlPages = LIST_HEAD;

        Status = MiArm64PrepareUserPageForMdl(CurrentProcess,
                                              PageAddress,
                                              AccessMode,
                                              Operation);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        MiLockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());

        /*
         * ARM64 user VAs are translated through TTBR0, while the generic ARM3
         * self-map helpers describe TTBR1. Walk the process user page tables
         * directly and then fill the MDL with the resolved PFN.
         */
        if (!MiArm64GetUserPteAddressForProcess(CurrentProcess, PageAddress, &Walk))
        {
            Status = STATUS_ACCESS_VIOLATION;
            MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            goto Cleanup;
        }

        Pte.u.Long = Walk.PteValue;
        if (!Pte.u.Hard.Valid)
        {
            Status = STATUS_ACCESS_VIOLATION;
            MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            goto Cleanup;
        }

        if ((Operation != IoReadAccess) &&
            (!MI_IS_PAGE_WRITEABLE(&Pte) || !MI_IS_PAGE_DIRTY(&Pte)))
        {
            Status = STATUS_ACCESS_VIOLATION;
            MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            goto Cleanup;
        }

        PageFrameIndex = PFN_FROM_PTE(&Pte);
        Pfn1 = MiGetPfnEntry(PageFrameIndex);
        if (Pfn1 == NULL)
        {
            Status = STATUS_ACCESS_VIOLATION;
            MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());
            goto Cleanup;
        }

        ASSERT(CurrentProcess->PhysicalVadRoot == NULL);
        MiReferenceProbedPageAndBumpLockCount(Pfn1);

        *MdlPages++ = PageFrameIndex;

        MiUnlockProcessWorkingSet(CurrentProcess, PsGetCurrentThread());

        PageAddress = (PVOID)((ULONG_PTR)PageAddress + PAGE_SIZE);
    }

    return STATUS_SUCCESS;

Cleanup:
    ASSERT(!NT_SUCCESS(Status));
    MmUnlockPages(Mdl);
    return Status;
}

FORCEINLINE
VOID
MiArm64InvalidateUserAddress(
    _In_ PVOID Address)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
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
VOID
MiArm64SynchronizeUserTablePfn(
    _Inout_ PEPROCESS Process,
    _In_ PFN_NUMBER TablePfn,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER ParentPfn,
    _In_ ULONG Level,
    _In_reads_(PTE_PER_PAGE) volatile ULONG64 *Table)
{
    KIRQL OldIrql;
    ULONG UsedEntries;
    ULONG ShareReferences;
    ULONG ExpectedShareCount;
    PMMPFN Pfn;

    UsedEntries = MiArm64CountUserTableEntries(Table, Level, &ShareReferences);
    ExpectedShareCount = ShareReferences + 1;

    OldIrql = MiAcquirePfnLock();

    Pfn = MiGetPfnEntry(TablePfn);
    if (Pfn == NULL)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if ((Pfn->u3.e1.PageLocation == FreePageList) ||
        (Pfn->u3.e1.PageLocation == ZeroedPageList))
    {
        MiUnlinkFreeOrZeroedPage(Pfn);
        Pfn = MiGetPfnEntry(TablePfn);
    }

    if ((Pfn->u3.e1.PageLocation != ActiveAndValid) ||
        (Pfn->u3.e2.ReferenceCount == 0))
    {
        Pfn->PteAddress = PteAddress;
        MI_MAKE_SOFTWARE_PTE(&Pfn->OriginalPte, MM_READWRITE);
        Pfn->u3.e2.ReferenceCount = 1;
        Pfn->u3.e1.PageLocation = ActiveAndValid;
        Pfn->u3.e1.Modified = TRUE;
        Pfn->u4.InPageError = FALSE;
        Pfn->u4.PteFrame = ParentPfn;
        Pfn->u2.ShareCount = ExpectedShareCount;
        Pfn->OriginalPte.u.Soft.UsedPageTableEntries = UsedEntries;

        Process->NumberOfPrivatePages++;
        MiReleasePfnLock(OldIrql);
        return;
    }

    if ((ParentPfn != 0) && (Pfn->u4.PteFrame != ParentPfn))
    {
        Pfn->u4.PteFrame = ParentPfn;
    }

    Pfn->PteAddress = PteAddress;
    if (Pfn->OriginalPte.u.Soft.UsedPageTableEntries != UsedEntries)
    {
        Pfn->OriginalPte.u.Soft.UsedPageTableEntries = UsedEntries;
    }

    if (Pfn->u2.ShareCount < ExpectedShareCount)
    {
        Pfn->u2.ShareCount = ExpectedShareCount;
    }

    MiReleasePfnLock(OldIrql);
}

static
MMPTE
MiArm64ClearKernelPte(
    _In_ PVOID Address,
    _Inout_ PMMPTE PointerPte)
{
    MMPTE OldPte;

    OldPte.u.Long = PointerPte->u.Long;
    if (OldPte.u.Hard.Valid)
    {
        MiArm64MaintainPageCache(Address, MiArm64DcacheCleanInvalidate, TRUE);
    }

    PointerPte->u.Long = 0;
    MiArm64SyncKernelLeafPteWrite(PointerPte);
    __asm__ __volatile__("dsb ishst" ::: "memory");
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
    if (OldPte.u.Hard.Valid)
    {
        MiArm64PublishPageByPfnAlias(OldPte.u.Hard.PageFrameNumber,
                                     OldPte.u.Hard.UserNoExecute == 0);
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
VOID
MiArm64ReleaseUserPageTableReference(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare)
{
    ULONG64 RootPa, L1Pa, L2Pa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx;
    ULONG64 L0Entry, L1Entry, L2Entry;
    PFN_NUMBER RootPfn, L1Pfn, L2Pfn, L3Pfn;
    KIRQL OldIrql;
    PMMPFN PfnRoot, PfnL1, PfnL2, PfnL3;
    ULONG ActualEntries;
    ULONG ActualShareReferences;

    ASSERT(Process != NULL);
    RootPa = Process->Pcb.DirectoryTableBase[0] & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
    {
        return;
    }

    L0Idx = ((ULONG64)(ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64;

    RootPfn = RootPa >> PAGE_SHIFT;
    if (!MiArm64EnsureTablePageMapped(RootPa))
        return;

    L0Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
        return;

    L1Pa = L0Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L1Pa))
        return;

    L1Pfn = L1Pa >> PAGE_SHIFT;
    L1Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1Pa);
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x3ULL) != 0x3ULL)
        return;

    L2Pa = L1Entry & ARM64_PTE_ADDR_MASK;
    if (!MiArm64EnsureTablePageMapped(L2Pa))
        return;

    L2Pfn = L2Pa >> PAGE_SHIFT;
    L2Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2Pa);
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x3ULL) != 0x3ULL)
        return;

    L3Pfn = (L2Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
    if (!MiArm64EnsureTablePageMapped((ULONG64)L3Pfn << PAGE_SHIFT))
        return;

    L3Table = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(L3Pfn);

    OldIrql = MiAcquirePfnLock();
    PfnRoot = MiGetPfnEntry(RootPfn);
    PfnL1 = MiGetPfnEntry(L1Pfn);
    PfnL2 = MiGetPfnEntry(L2Pfn);
    PfnL3 = MiGetPfnEntry(L3Pfn);

    if (PfnL3 == NULL || PfnL2 == NULL || PfnL1 == NULL || PfnRoot == NULL)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (ReleaseLeafShare && (PfnL3->u2.ShareCount > 0))
    {
        MiDecrementShareCount(PfnL3, L3Pfn);
    }

    if (PfnL3->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnL3->OriginalPte.u.Soft.UsedPageTableEntries--;
    }
    else
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL3->OriginalPte.u.Soft.UsedPageTableEntries != 0)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    ActualEntries = MiArm64CountUserTableEntries(L3Table, 3, &ActualShareReferences);
    if (ActualEntries != 0)
    {
        PfnL3->OriginalPte.u.Soft.UsedPageTableEntries = ActualEntries;
        if (PfnL3->u2.ShareCount < (ActualShareReferences + 1))
        {
            PfnL3->u2.ShareCount = ActualShareReferences + 1;
        }
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL3->u2.ShareCount != 1)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    MiArm64WritePteEntry(&L2Table[L2Idx], 0);
    MiArm64InvalidateUserAddress(Address);
    MiDecrementShareCount(PfnL2, L2Pfn);
    MI_SET_PFN_DELETED(PfnL3);
    MiDecrementShareCount(PfnL3, L3Pfn);

    if (PfnL2->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnL2->OriginalPte.u.Soft.UsedPageTableEntries--;
    }
    else
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL2->OriginalPte.u.Soft.UsedPageTableEntries != 0)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    ActualEntries = MiArm64CountUserTableEntries(L2Table, 2, &ActualShareReferences);
    if (ActualEntries != 0)
    {
        PfnL2->OriginalPte.u.Soft.UsedPageTableEntries = ActualEntries;
        if (PfnL2->u2.ShareCount < (ActualShareReferences + 1))
        {
            PfnL2->u2.ShareCount = ActualShareReferences + 1;
        }
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL2->u2.ShareCount != 1)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    MiArm64WritePteEntry(&L1Table[L1Idx], 0);
    MiArm64InvalidateUserAddress(Address);
    MiDecrementShareCount(PfnL1, L1Pfn);
    MI_SET_PFN_DELETED(PfnL2);
    MiDecrementShareCount(PfnL2, L2Pfn);

    if (PfnL1->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnL1->OriginalPte.u.Soft.UsedPageTableEntries--;
    }
    else
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL1->OriginalPte.u.Soft.UsedPageTableEntries != 0)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    ActualEntries = MiArm64CountUserTableEntries(L1Table, 1, &ActualShareReferences);
    if (ActualEntries != 0)
    {
        PfnL1->OriginalPte.u.Soft.UsedPageTableEntries = ActualEntries;
        if (PfnL1->u2.ShareCount < (ActualShareReferences + 1))
        {
            PfnL1->u2.ShareCount = ActualShareReferences + 1;
        }
        MiReleasePfnLock(OldIrql);
        return;
    }

    if (PfnL1->u2.ShareCount != 1)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    MiArm64WritePteEntry(&L0Table[L0Idx], 0);
    MiArm64InvalidateUserAddress(Address);
    MiDecrementShareCount(PfnRoot, RootPfn);
    MI_SET_PFN_DELETED(PfnL1);
    MiDecrementShareCount(PfnL1, L1Pfn);

    if (PfnRoot->OriginalPte.u.Soft.UsedPageTableEntries > 0)
    {
        PfnRoot->OriginalPte.u.Soft.UsedPageTableEntries--;
    }

    MiReleasePfnLock(OldIrql);
}

VOID
MiInvalidateDCachePageIncoming(
    _In_ PVOID Address)
{
    MiArm64MaintainPageCache(Address, MiArm64DcacheInvalidate, TRUE);
}

VOID
MiInvalidateDCachePageOutgoing(
    _In_ PVOID Address)
{
    MiArm64MaintainPageCache(Address, MiArm64DcacheCleanInvalidate, TRUE);
}

VOID
MiInvalidateDCachePage(
    _In_ PVOID Address)
{
    MiInvalidateDCachePageOutgoing(Address);
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

    /* Reject PFN 0 - physical page 0 is reserved and should never be mapped */
    if (Page == 0)
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

        Status = MiArm64EnsureUserPte(Process, Address, &PointerPte);
        if (!NT_SUCCESS(Status))
        {
            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
            return Status;
        }

        if (MiArm64UserPteKseg0ForPfn(Address, &PteFrame) != PointerPte)
        {
            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
            return STATUS_INVALID_ADDRESS;
        }

        OldPte = *PointerPte;
        OldEntryEmpty = (OldPte.u.Long == 0);
        OldEntryHasPageTableShare = OldPte.u.Hard.Valid || OldPte.u.Soft.Transition;

        FinalPte.u.Long = 0;
        FinalPte.u.Hard.Valid = 1;
        FinalPte.u.Hard.NotLargePage = 1;
        FinalPte.u.Hard.Owner = 1;
        FinalPte.u.Hard.Accessed = 1;
        FinalPte.u.Hard.Shareability = 3;
        FinalPte.u.Hard.NonGlobal = 1;
        FinalPte.u.Hard.PageFrameNumber = Page;
        FinalPte.u.Long |= MmProtectToPteMask[ProtectionMask];

        if (FinalPte.u.Hard.Writable)
        {
            MI_MAKE_DIRTY_PAGE(&FinalPte);
        }
        else
        {
            FinalPte.u.Hard.NotDirty = 1;
        }

        if (!IsPhysical)
        {
            KIRQL OldIrql;
            PMMPFN Pfn1;

            OldIrql = MiAcquirePfnLock();
            Pfn1 = MiGetPfnEntry(Page);
            if (Pfn1 == NULL)
            {
                MiReleasePfnLock(OldIrql);
                MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                return STATUS_INVALID_PARAMETER;
            }

            Pfn1->u2.ShareCount++;
            Pfn1->u3.e1.PageLocation = ActiveAndValid;
            MiArm64SyncMappedPfnCacheAttribute(Process, Address, Page, FinalPte);
            MiReleasePfnLock(OldIrql);
        }

        if (!OldEntryHasPageTableShare || OldEntryEmpty)
        {
            MiArm64AccountUserLeafPte(PteFrame,
                                      !OldEntryHasPageTableShare,
                                      OldEntryEmpty);
        }

        if (OldPte.u.Hard.Valid && OldPte.u.Hard.NotLargePage)
        {
            MiArm64InvalidatePageByPfnAlias(OldPte.u.Hard.PageFrameNumber);
        }

        PointerPte->u.Long = FinalPte.u.Long;
        MiArm64CleanEntryToPoC(PointerPte);
        __asm__ __volatile__("dsb sy" ::: "memory");
        MiArm64PublishPageByPfnAlias(Page, FinalPte.u.Hard.UserNoExecute == 0);
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
        MiArm64SyncMappedPfnCacheAttribute(Process,
                                           Address,
                                           Page,
                                           TempPte);
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
        __asm__ __volatile__("dsb ishst" ::: "memory");

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
    BOOLEAN ProcessWorkingSetLocked = FALSE;

    OldPte.u.Long = 0;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);

        if (MiIsPdeForAddressValid(Address))
        {
            OldPte = MiArm64ClearKernelPte(Address, MiAddressToPte(Address));
        }
    }
    else
    {
        MI_ARM64_USER_PTE_WALK Walk;

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
                                             OldPte.u.Hard.Valid || OldPte.u.Soft.Transition);
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

        NTSTATUS Status;

        Status = MiArm64EnsureUserPte(Process, Address, &PointerPte);
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
        NewPte.u.Soft.PageFileLow = MI_ARM64_SWAP_FILE_FROM_ENTRY(SwapEntry);
        NewPte.u.Soft.PageFileHigh = MI_ARM64_SWAP_OFFSET_FROM_ENTRY(SwapEntry);
        NewPte.u.Soft.Prototype = 0;
        NewPte.u.Soft.Protection = MM_READWRITE;

        MiArm64WritePteEntry((volatile ULONG64 *)PointerPte, NewPte.u.Long);
        MiArm64IncrementUserPageTableReferences(Address);
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

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());

    {
        MI_ARM64_USER_PTE_WALK Walk;

        if (!MiArm64GetUserPteAddress(Address, &Walk))
        {
            *SwapEntry = 0;
            MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
            return;
        }

        OldPte.u.Long = MiArm64ExchangePteEntry(Walk.PointerPte, 0);
    }

    if (OldPte.u.Hard.Valid ||
        OldPte.u.Soft.Prototype ||
        OldPte.u.Soft.Transition ||
        (OldPte.u.Soft.PageFileHigh == 0))
    {
        DPRINT1("Expected pagefile PTE at %p\n", Address);
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    *SwapEntry = MI_ARM64_SWAP_ENTRY_FROM_FILE_OFFSET(OldPte.u.Soft.PageFileLow,
                                                       OldPte.u.Soft.PageFileHigh);

    MiArm64ReleaseUserPageTableReference(Process, Address, FALSE);

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
                *SwapEntry = MI_ARM64_SWAP_ENTRY_FROM_FILE_OFFSET(TempPte.u.Soft.PageFileLow,
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
            *SwapEntry = MI_ARM64_SWAP_ENTRY_FROM_FILE_OFFSET(PointerPte->u.Soft.PageFileLow,
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

    return MiArm64ReadUserPtePhysically(Address, NULL, NULL);
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

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue, NULL))
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

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue, NULL))
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

        ASSERT(Process == NULL);

        if (!MiIsPdeForAddressValid(Address))
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
        MiArm64SyncKernelLeafPteWrite(PointerPte);

        MiArm64PreserveDirtyStateForProtect(OldPte, TempPte);

        if (OldPte.u.Long != TempPte.u.Long)
        {
            __asm__ __volatile__("dsb ishst" ::: "memory");
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
            __asm__ __volatile__("dsb ishst" ::: "memory");
            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> 12) : "memory");
            __asm__ __volatile__("dsb ish" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
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
        MMPTE OldPte, TempPte;

        ASSERT(Process == NULL);

        if (!MiIsPdeForAddressValid(Address))
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
        MiArm64SyncKernelLeafPteWrite(PointerPte);

        if (OldPte.u.Long != TempPte.u.Long)
        {
            __asm__ __volatile__("dsb ishst" ::: "memory");
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
            __asm__ __volatile__("dsb ishst" ::: "memory");
            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> 12) : "memory");
            __asm__ __volatile__("dsb ish" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");
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
BOOLEAN
__attribute__((unused))
MiIsPageTablePresent(
    _In_ PVOID Address)
{
#if _MI_PAGING_LEVELS == 2
    BOOLEAN Ret = MmWorkingSetList->UsedPageTableEntries[MiGetPdeOffset(Address)] != 0;
    ASSERT(Ret == (MiAddressToPde(Address)->u.Hard.Valid != 0));
    return Ret;
#else
    PMMPDE PointerPde;
    PMMPPE PointerPpe;
#if _MI_PAGING_LEVELS == 4
    PMMPXE PointerPxe;
#endif

    ASSERT((PsGetCurrentThread()->OwnsProcessWorkingSetExclusive) ||
           (PsGetCurrentThread()->OwnsProcessWorkingSetShared));
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    if (Address < MmSystemRangeStart)
    {
        MI_ARM64_USER_PTE_WALK Walk;
        return MiArm64GetUserPteAddress(Address, &Walk);
    }

#if _MI_PAGING_LEVELS == 4
    PointerPxe = MiAddressToPxe(Address);

    if (PointerPxe->u.Hard.Valid == 0 && PointerPxe->u.Soft.Transition == 0)
    {
        if (PointerPxe->u.Long == 0)
            return FALSE;
    }

    if (PointerPxe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPxe), PsGetCurrentProcess());
#endif

    PointerPpe = MiAddressToPpe(Address);

    if (PointerPpe->u.Hard.Valid == 0 && PointerPpe->u.Soft.Transition == 0)
    {
        if (PointerPpe->u.Long == 0)
            return FALSE;
    }

    if (PointerPpe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPpe), PsGetCurrentProcess());

    PointerPde = MiAddressToPde(Address);

    if (PointerPde->u.Hard.Valid == 1)
        return TRUE;

    if (PointerPde->u.Soft.Transition == 1)
        return TRUE;

    return PointerPde->u.Long != 0;
#endif
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

FORCEINLINE
ULONG64
MiArm64NextVaRange(
    _In_ ULONG64 Va,
    _In_ ULONG64 RangeSize)
{
    return (Va + RangeSize) & ~(RangeSize - 1);
}

FORCEINLINE
VOID
MiArm64ClearUserDescriptor(
    _Inout_ volatile ULONG64 *Entry)
{
    MiArm64WritePteEntry(Entry, 0);
    MiArm64CleanInvalidateCacheLine((PVOID)Entry);
}

static
BOOLEAN
MiArm64ClearBootUserMappingAtVa(
    _In_ ULONG64 Va,
    _Out_ PULONG64 NextVa,
    _Out_ PULONG ClearedPages)
{
    ULONG64 Ttbr0;
    ULONG64 RootPa;
    ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;
    volatile ULONG64 *L0Table;
    volatile ULONG64 *L1Table;
    volatile ULONG64 *L2Table;
    volatile ULONG64 *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;

    *NextVa = Va + PAGE_SIZE;
    *ClearedPages = 0;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPa = Ttbr0 & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
    {
        return FALSE;
    }

    L0Idx = (Va >> 39) & 0x1FF;
    L1Idx = (Va >> 30) & 0x1FF;
    L2Idx = (Va >> 21) & 0x1FF;
    L3Idx = (Va >> 12) & 0x1FF;

    L0Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 39);
        return FALSE;
    }

    L1Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L0Entry & ARM64_PTE_ADDR_MASK);
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x1ULL) == 0)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 30);
        return FALSE;
    }

    if ((L1Entry & 0x3ULL) == 0x1ULL)
    {
        MiArm64ClearUserDescriptor(&L1Table[L1Idx]);
        MiArm64InvalidateUserAddress((PVOID)Va);
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 30);
        *ClearedPages = (1ULL << 30) >> PAGE_SHIFT;
        return TRUE;
    }

    if ((L1Entry & 0x3ULL) != 0x3ULL)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 30);
        return FALSE;
    }

    L2Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L1Entry & ARM64_PTE_ADDR_MASK);
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x1ULL) == 0)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 21);
        return FALSE;
    }

    if ((L2Entry & 0x3ULL) == 0x1ULL)
    {
        MiArm64ClearUserDescriptor(&L2Table[L2Idx]);
        MiArm64InvalidateUserAddress((PVOID)Va);
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 21);
        *ClearedPages = (1ULL << 21) >> PAGE_SHIFT;
        return TRUE;
    }

    if ((L2Entry & 0x3ULL) != 0x3ULL)
    {
        *NextVa = MiArm64NextVaRange(Va, 1ULL << 21);
        return FALSE;
    }

    L3Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(L2Entry & ARM64_PTE_ADDR_MASK);
    L3Entry = L3Table[L3Idx];
    if ((L3Entry & 0x1ULL) == 0)
    {
        return FALSE;
    }

    if ((L3Entry & 0x3ULL) == 0x3ULL)
    {
        MiArm64InvalidatePageByPfnAlias((PFN_NUMBER)((L3Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT));
    }

    MiArm64ClearUserDescriptor(&L3Table[L3Idx]);
    MiArm64InvalidateUserAddress((PVOID)Va);
    *ClearedPages = 1;
    return TRUE;
}

VOID
NTAPI
MiArm64ClearStaleUserPtes(
    _In_ PVOID StartVa,
    _In_ SIZE_T Size,
    _In_ PEPROCESS Process)
{
    ULONG64 Va, EndVa;
    ULONG ClearedCount = 0;

    ASSERT(Process == PsGetCurrentProcess());
    UNREFERENCED_PARAMETER(Process);

    if ((ULONG_PTR)StartVa >= (ULONG_PTR)MmSystemRangeStart)
        return;

    EndVa = (ULONG64)StartVa + Size;
    if (EndVa > (ULONG64)(ULONG_PTR)MmSystemRangeStart)
    {
        EndVa = (ULONG64)(ULONG_PTR)MmSystemRangeStart;
    }
    Va = (ULONG64)StartVa;

    while (Va < EndVa)
    {
        ULONG64 NextVa;
        ULONG ClearedPages;

        MiArm64ClearBootUserMappingAtVa(Va, &NextVa, &ClearedPages);
        ClearedCount += ClearedPages;
        Va = (NextVa < EndVa) ? NextVa : EndVa;
    }

    if (ClearedCount > 0)
    {
        DPRINT("[arm64] MiArm64ClearStaleUserPtes: Cleared %lu stale PTEs in range %p-%p\n",
                ClearedCount, StartVa, (PVOID)EndVa);
    }
}
