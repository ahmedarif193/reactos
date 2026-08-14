/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/awesup.c
 * PURPOSE:         ARM Memory Manager Address Windowing Extensions Support
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

#define TAG_MI_AWE 'ewAM'

/* Pages processed per PFN lock hold / stack-capture threshold */
#define MI_AWE_CHUNK 64

/* PteFrame marker for AWE-owned pages, in the spirit of the 0x1FFEDCB
   marker MiAllocatePagesForMdl uses for MDL-owned pages */
#define MI_AWE_PTE_FRAME 0x1FFEDAE

/* Free pages Mm always keeps for itself when granting AWE pages */
#define MI_AWE_MINIMUM_AVAILABLE_PAGES 128

/* Legacy 24-bit DMA devices can only address pages below 16 MiB */
#define MI_AWE_LOW_DMA_PAGES (0x1000000 / PAGE_SIZE)

/* The AWE page ownership of one process, hung off EPROCESS::AweInfo on the
   first NtAllocateUserPhysicalPages call. One bit per physical page; a set
   bit means the page is currently granted to the process. The bitmap and
   the page counter are only accessed while holding the PFN lock.

   The MMPFN of a granted page follows the MDL-page idiom: one reference,
   share count one, PteFrame holding MI_AWE_PTE_FRAME. PteAddress doubles
   as the mapping state: the MI_SET_PFN_DELETED low bit means "not mapped",
   otherwise it holds the self-map alias of the (single) user PTE mapping
   the page, so MiPteToAddress() recovers the mapped VA. The private
   MI_AWE_RESERVED_PTE value claims an unmapped PFN while a fully validated
   mapping request materializes its page tables. On ARM64 the mapping alias
   is an identity only and is never dereferenced; the hardware PTE is always
   reached through the TTBR0/KSEG0 walk. */
typedef struct _MI_AWE_INFO
{
    RTL_BITMAP PfnBitMap;
    PFN_NUMBER PagesAllocated;
    SIZE_T QuotaCharge;
} MI_AWE_INFO, *PMI_AWE_INFO;

#define MI_AWE_UNMAPPED_PTE ((PMMPTE)(ULONG_PTR)1)
#define MI_AWE_RESERVED_PTE ((PMMPTE)(ULONG_PTR)2)

/* Kernel-side capture of an untrusted ULONG_PTR array. All user buffers are
   captured before any address space or working set lock is acquired: a fault
   on a probed-but-paged-out buffer while owning the working set lock would
   recurse into the fault handler's working set acquire. */
typedef struct _MI_AWE_CAPTURE
{
    PULONG_PTR Buffer;
    ULONG_PTR StackBuffer[MI_AWE_CHUNK];
} MI_AWE_CAPTURE, *PMI_AWE_CAPTURE;

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
MiAweCaptureUlongPtrArray(
    _Out_ PMI_AWE_CAPTURE Capture,
    _In_reads_(Count) PULONG_PTR UserArray,
    _In_ ULONG_PTR Count,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    SIZE_T Size = Count * sizeof(ULONG_PTR);

    /* Callers bound Count against MAXULONG_PTR / sizeof(ULONG_PTR) */
    ASSERT(Count != 0);

    Capture->Buffer = Capture->StackBuffer;
    if (Count > MI_AWE_CHUNK)
    {
        /* NonPagedPool: chunks of this copy are read under the PFN lock */
        Capture->Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_MI_AWE);
        if (Capture->Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(UserArray, Size, sizeof(ULONG_PTR));
        }
        RtlCopyMemory(Capture->Buffer, UserArray, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        if (Capture->Buffer != Capture->StackBuffer)
        {
            ExFreePoolWithTag(Capture->Buffer, TAG_MI_AWE);
        }
        Capture->Buffer = NULL;
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

static
VOID
MiAweReleaseCapture(
    _Inout_ PMI_AWE_CAPTURE Capture)
{
    if ((Capture->Buffer != NULL) && (Capture->Buffer != Capture->StackBuffer))
    {
        ExFreePoolWithTag(Capture->Buffer, TAG_MI_AWE);
    }
    Capture->Buffer = NULL;
}

static
NTSTATUS
MiAweGetInfo(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Create,
    _Outptr_result_maybenull_ PMI_AWE_INFO *ReturnedAweInfo)
{
    PMI_AWE_INFO AweInfo, OldInfo;
    ULONG BitmapBits;
    SIZE_T Size;
    NTSTATUS Status;

    AweInfo = Process->AweInfo;
    if ((AweInfo != NULL) || (Create == FALSE))
    {
        *ReturnedAweInfo = AweInfo;
        return STATUS_SUCCESS;
    }

    /* RTL_BITMAP uses ULONG indexes and cannot describe 2^32 PFNs. */
    if (MmHighestPhysicalPage >= (PFN_NUMBER)MAXULONG)
    {
        *ReturnedAweInfo = NULL;
        return STATUS_NOT_SUPPORTED;
    }

    /* One bit for every physical page in the machine */
    BitmapBits = (ULONG)(MmHighestPhysicalPage + 1);
    Size = sizeof(MI_AWE_INFO) + ((((SIZE_T)BitmapBits + 31) / 32) * sizeof(ULONG));

    Status = PsChargeProcessNonPagedPoolQuota(Process, Size);
    if (!NT_SUCCESS(Status))
    {
        *ReturnedAweInfo = NULL;
        return Status;
    }

    AweInfo = ExAllocatePoolZero(NonPagedPool, Size, TAG_MI_AWE);
    if (AweInfo == NULL)
    {
        PsReturnProcessNonPagedPoolQuota(Process, Size);
        *ReturnedAweInfo = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    AweInfo->QuotaCharge = Size;
    RtlInitializeBitMap(&AweInfo->PfnBitMap, (PULONG)(AweInfo + 1), BitmapBits);

    /* Publish it, racing against other threads of the process */
    OldInfo = InterlockedCompareExchangePointer(&Process->AweInfo, AweInfo, NULL);
    if (OldInfo != NULL)
    {
        ExFreePoolWithTag(AweInfo, TAG_MI_AWE);
        PsReturnProcessNonPagedPoolQuota(Process, Size);
        AweInfo = OldInfo;
    }

    *ReturnedAweInfo = AweInfo;
    return STATUS_SUCCESS;
}

static
BOOLEAN
MiAweOwnsPage(
    _In_ PMI_AWE_INFO AweInfo,
    _In_ ULONG_PTR Page)
{
    MI_ASSERT_PFN_LOCK_HELD();
    if (Page >= AweInfo->PfnBitMap.SizeOfBitMap) return FALSE;
    return RtlCheckBit(&AweInfo->PfnBitMap, (ULONG)Page) != 0;
}

static
PFN_NUMBER
MiAweRemovePageFromList(
    _In_ PMMPFNLIST ListHead,
    _In_ PFN_NUMBER LowestPage)
{
    PFN_NUMBER Page;
    PMMPFN Pfn1;

    MI_ASSERT_PFN_LOCK_HELD();

    /* Walk backward so boot-time ascending lists yield high pages first. */
    Page = ListHead->Blink;
    while (Page != LIST_HEAD)
    {
        Pfn1 = MiGetPfnEntry(Page);
        ASSERT(Pfn1 != NULL);
        if (Page >= LowestPage)
        {
            return MiRemovePageByColor(Page, (ULONG)(Page & MmSecondaryColorMask));
        }
        Page = Pfn1->u2.Blink;
    }

    return 0;
}

static
PFN_NUMBER
MiAweRemovePage(
    _Out_ PBOOLEAN NeedsZero)
{
    PFN_NUMBER LowestPage, Page;

    MI_ASSERT_PFN_LOCK_HELD();

    LowestPage = MmLowestPhysicalPage;
    if (MmNumberOfPhysicalPages > (MI_AWE_LOW_DMA_PAGES + MI_AWE_MINIMUM_AVAILABLE_PAGES))
    {
        LowestPage = max(LowestPage, (PFN_NUMBER)MI_AWE_LOW_DMA_PAGES);
    }

    *NeedsZero = FALSE;
    Page = MiAweRemovePageFromList(&MmZeroedPageListHead, LowestPage);
    if (Page != 0) return Page;

    Page = MiAweRemovePageFromList(&MmFreePageListHead, LowestPage);
    if (Page != 0) *NeedsZero = TRUE;
    return Page;
}

static
VOID
MiAweReleaseReservations(
    _In_ PMI_AWE_INFO AweInfo,
    _In_reads_(Count) PULONG_PTR PfnArray,
    _In_ ULONG_PTR Count,
    _In_ BOOLEAN AllowZero)
{
    ULONG_PTR Index, End;
    PFN_NUMBER Page;
    PMMPFN Pfn1;
    KIRQL OldIrql;

    Index = 0;
    while (Index < Count)
    {
        End = min(Count, Index + MI_AWE_CHUNK);
        OldIrql = MiAcquirePfnLock();
        while (Index < End)
        {
            Page = PfnArray[Index++];
            if (AllowZero && (Page == 0)) continue;

            if (MiAweOwnsPage(AweInfo, Page))
            {
                Pfn1 = MiGetPfnEntry(Page);
                if (Pfn1->PteAddress == MI_AWE_RESERVED_PTE)
                {
                    Pfn1->PteAddress = MI_AWE_UNMAPPED_PTE;
                }
            }
        }
        MiReleasePfnLock(OldIrql);
    }
}

static
NTSTATUS
MiAweReservePages(
    _In_ PMI_AWE_INFO AweInfo,
    _In_reads_(Count) PULONG_PTR PfnArray,
    _In_ ULONG_PTR Count,
    _In_ BOOLEAN AllowZero)
{
    ULONG_PTR Index, End, Validated;
    PFN_NUMBER Page;
    PMMPFN Pfn1;
    KIRQL OldIrql;
    BOOLEAN Invalid;

    Index = 0;
    while (Index < Count)
    {
        End = min(Count, Index + MI_AWE_CHUNK);
        Invalid = FALSE;
        OldIrql = MiAcquirePfnLock();
        while (Index < End)
        {
            Page = PfnArray[Index];
            if (AllowZero && (Page == 0))
            {
                Index++;
                continue;
            }

            if (!MiAweOwnsPage(AweInfo, Page))
            {
                Invalid = TRUE;
                break;
            }

            Pfn1 = MiGetPfnEntry(Page);
            if (Pfn1->PteAddress != MI_AWE_UNMAPPED_PTE)
            {
                Invalid = TRUE;
                break;
            }

            Pfn1->PteAddress = MI_AWE_RESERVED_PTE;
            Index++;
        }
        Validated = Index;
        MiReleasePfnLock(OldIrql);

        if (Invalid)
        {
            MiAweReleaseReservations(AweInfo, PfnArray, Validated, AllowZero);
            return STATUS_INVALID_PARAMETER_3;
        }
    }

    return STATUS_SUCCESS;
}

static
VOID
MiAweReleasePage(
    _In_ PMI_AWE_INFO AweInfo,
    _In_ PFN_NUMBER Page)
{
    PMMPFN Pfn1 = MiGetPfnEntry(Page);

    /* Give an unmapped AWE page back, following the MmFreePagesFromMdl idiom */
    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT(Pfn1->u4.PteFrame == MI_AWE_PTE_FRAME);
    ASSERT(Pfn1->u2.ShareCount == 1);
    ASSERT(MI_IS_PFN_DELETED(Pfn1) == TRUE);
    Pfn1->u3.e1.PageLocation = StandbyPageList;
    Pfn1->u2.ShareCount = 0;
    ASSERT(Pfn1->u3.e2.ReferenceCount != 0);
    MiDecrementReferenceCount(Pfn1, Page);

    RtlClearBit(&AweInfo->PfnBitMap, (ULONG)Page);
    AweInfo->PagesAllocated--;
}

static
NTSTATUS
MiAweEnsureMappingPte(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR Va)
{
#if defined(_M_ARM64)
    PMMPTE PointerPte;
    NTSTATUS Status;

    Status = MiArm64EnsureUserPte(Process, (PVOID)Va, &PointerPte, NULL);
    if (!NT_SUCCESS(Status))
    {
        MiArm64PruneEmptyUserPageTables(Process, (PVOID)Va);
    }
    return Status;
#else
    MiMakePdeExistAndMakeValid(MiAddressToPde((PVOID)Va), Process, MM_NOIRQL);
    return STATUS_SUCCESS;
#endif
}

#if defined(_M_ARM64)

/*
 * ARM64: user mappings translate through TTBR0, and the recursive self-map
 * is not authoritative for user addresses. Every hardware PTE access goes
 * through the TTBR0/KSEG0 walk (MiArm64EnsureUserPte and friends), with the
 * page table accounting of MmCreateVirtualMappingUnsafeEx: a fresh valid
 * leaf holds one share and one used-entry reference on its L3 table.
 */

static
VOID
MiAweMapVa(
    _In_ PEPROCESS Process,
    _In_ PMI_AWE_INFO AweInfo,
    _In_ ULONG_PTR Va,
    _In_ PFN_NUMBER Page)
{
    PMMPTE PointerPte;
    PMMPFN Pfn1, OldPfn, TablePfn;
    PFN_NUMBER L3Pfn;
    MMPTE TempPte;
    BOOLEAN WasValid;
    KIRQL OldIrql;
    MI_ARM64_USER_PTE_WALK Walk;
    BOOLEAN Found;

    /* Every leaf was materialized before any PTE in the request changed. */
    Found = MiArm64GetUserPteAddressForProcess(Process, (PVOID)Va, &Walk);
    ASSERT(Found && (Walk.PointerPte != NULL));
    PointerPte = (PMMPTE)Walk.PointerPte;
    L3Pfn = Walk.LevelPfn[3];

    MI_MAKE_HARDWARE_PTE_USER(&TempPte, MiAddressToPte(Va), MM_READWRITE, Page);
    if (MI_IS_PAGE_WRITEABLE(&TempPte)) MI_MAKE_DIRTY_PAGE(&TempPte);

    OldIrql = MiAcquirePfnLock();

    /* The complete request claimed every PFN before changing any PTE. */
    ASSERT(MiAweOwnsPage(AweInfo, Page));
    Pfn1 = MiGetPfnEntry(Page);
    ASSERT(Pfn1->PteAddress == MI_AWE_RESERVED_PTE);

    WasValid = (PointerPte->u.Hard.Valid != 0);
    if (WasValid)
    {
        /* Replacing an existing AWE mapping detaches the old page. Break
           before make so the walker never sees both descriptors, and the
           table references stay balanced across the rewrite. */
        OldPfn = MiGetPfnEntry(PFN_FROM_PTE(PointerPte));
        ASSERT(OldPfn->u4.PteFrame == MI_AWE_PTE_FRAME);
        ASSERT(OldPfn->PteAddress == MiAddressToPte(Va));
        OldPfn->PteAddress = MI_AWE_UNMAPPED_PTE;
        MI_ERASE_PTE(PointerPte);
        MiArm64InvalidateUserAddress((PVOID)Va);
    }
    else
    {
        /* AWE leaves are only ever zero or valid */
        ASSERT(PointerPte->u.Long == 0);
        TablePfn = MiGetPfnEntry(L3Pfn);
        TablePfn->u2.ShareCount++;
        TablePfn->OriginalPte.u.Soft.UsedPageTableEntries++;
        ASSERT(TablePfn->OriginalPte.u.Soft.UsedPageTableEntries <= PTE_PER_PAGE);
    }

    /* Publish through the KSEG0 slot; PteAddress keeps the self-map alias
       as the mapping identity so MiPteToAddress() recovers the VA */
    PointerPte->u.Long = TempPte.u.Long;
    MiArm64CleanEntryToPoC(PointerPte);
    MiArm64InvalidateUserAddress((PVOID)Va);
    Pfn1->PteAddress = MiAddressToPte(Va);

    MiReleasePfnLock(OldIrql);
}

static
VOID
MiAweUnmapVa(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR Va)
{
    MI_ARM64_USER_PTE_WALK Walk;
    PMMPTE PointerPte;
    PFN_NUMBER Page;
    PMMPFN Pfn1;
    KIRQL OldIrql;

    if (!MiArm64GetUserPteAddressForProcess(Process, (PVOID)Va, &Walk) ||
        (Walk.PointerPte == NULL))
    {
        return;
    }
    PointerPte = (PMMPTE)Walk.PointerPte;

    OldIrql = MiAcquirePfnLock();
    if (!PointerPte->u.Hard.Valid)
    {
        MiReleasePfnLock(OldIrql);
        return;
    }

    /* Detach the page from the PTE, but keep it granted to the process.
       The scatter unwind can present addresses outside any AWE region;
       leave any mapping that is not an AWE page untouched, including
       device/MMIO pages whose frame has no PFN database entry. */
    Page = PFN_FROM_PTE(PointerPte);
    Pfn1 = MiGetPfnEntry(Page);
    if ((Pfn1 == NULL) || (Pfn1->u4.PteFrame != MI_AWE_PTE_FRAME))
    {
        MiReleasePfnLock(OldIrql);
        return;
    }
    ASSERT(Pfn1->PteAddress == MiAddressToPte(Va));
    MI_ERASE_PTE(PointerPte);
    MiArm64InvalidateUserAddress((PVOID)Va);
    Pfn1->PteAddress = MI_AWE_UNMAPPED_PTE;
    MiReleasePfnLock(OldIrql);

    /* Drop the share and used-entry references the mapping held; empty
       tables collapse inside the helper, which takes the PFN lock itself */
    MiArm64ReleaseUserPageTableReference(Process, (PVOID)Va, TRUE, &Walk);
}

VOID
NTAPI
MiAweUnmapRange(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR Va,
    _In_ ULONG_PTR EndingAddress)
{
    MI_ARM64_USER_PTE_WALK Walk;

    /* The exclusive working set lock keeps the hierarchy stable */
    ASSERT(PsGetCurrentThread()->OwnsProcessWorkingSetExclusive);

    while (Va <= EndingAddress)
    {
        if (!MiArm64GetUserPteAddressForProcess(Process, (PVOID)Va, &Walk) ||
            (Walk.PointerPte == NULL))
        {
            /* No L3 table here, skip the rest of this table's range */
            Va = (Va & ~((ULONG_PTR)PDE_MAPPED_VA - 1)) + PDE_MAPPED_VA;
            continue;
        }

        /* Retire the leaf and its accounting together. Erasing a batch first
           would make the table validator observe fewer live entries than
           UsedPageTableEntries until the rest of the batch was released. */
        MiAweUnmapVa(Process, Va);
        Va += PAGE_SIZE;
    }
}

#else /* !defined(_M_ARM64) */

static
VOID
MiAweUnmapPte(
    _In_ PMMPTE PointerPte)
{
    PFN_NUMBER Page;
    PMMPFN Pfn1;

    /* Called with the PFN lock and the exclusive working set lock held */
    MI_ASSERT_PFN_LOCK_HELD();
    ASSERT(PointerPte->u.Hard.Valid == 1);

    /* Detach the page from the PTE, but keep it granted to the process */
    Page = PFN_FROM_PTE(PointerPte);
    Pfn1 = MiGetPfnEntry(Page);
    ASSERT(Pfn1->u4.PteFrame == MI_AWE_PTE_FRAME);
    ASSERT(Pfn1->PteAddress == PointerPte);
    MI_ERASE_PTE(PointerPte);
    Pfn1->PteAddress = MI_AWE_UNMAPPED_PTE;
}

static
VOID
MiAweMapVa(
    _In_ PEPROCESS Process,
    _In_ PMI_AWE_INFO AweInfo,
    _In_ ULONG_PTR Va,
    _In_ PFN_NUMBER Page)
{
    PMMPTE PointerPte;
    PMMPFN Pfn1, OldPfn;
    MMPTE TempPte;
    BOOLEAN WasValid;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Process);

    OldIrql = MiAcquirePfnLock();

    /* The complete request claimed every PFN before changing any PTE. */
    ASSERT(MiAweOwnsPage(AweInfo, Page));
    Pfn1 = MiGetPfnEntry(Page);
    ASSERT(Pfn1->PteAddress == MI_AWE_RESERVED_PTE);

    /* Replacing an existing AWE mapping detaches the old page; a fresh
       mapping references the page table instead */
    PointerPte = MiAddressToPte((PVOID)Va);
    WasValid = (PointerPte->u.Hard.Valid != 0);
    if (WasValid)
    {
        OldPfn = MiGetPfnEntry(PFN_FROM_PTE(PointerPte));
        ASSERT(OldPfn->u4.PteFrame == MI_AWE_PTE_FRAME);
        ASSERT(OldPfn->PteAddress == PointerPte);
        OldPfn->PteAddress = MI_AWE_UNMAPPED_PTE;
        MI_ERASE_PTE(PointerPte);
    }
    else
    {
        MiIncrementPageTableReferences((PVOID)Va);
    }

    MI_MAKE_HARDWARE_PTE_USER(&TempPte, PointerPte, MM_READWRITE, Page);
    if (MI_IS_PAGE_WRITEABLE(&TempPte)) MI_MAKE_DIRTY_PAGE(&TempPte);
    MI_WRITE_VALID_PTE(PointerPte, TempPte);
    if (WasValid) MiFlushTbForAddress((PVOID)Va);
    Pfn1->PteAddress = PointerPte;

    MiReleasePfnLock(OldIrql);
}

static
VOID
MiAweUnmapVa(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR Va)
{
    PMMPDE PointerPde;
    PMMPTE PointerPte;
    PMMPFN Pfn1;
#if (_MI_PAGING_LEVELS >= 4)
    PMMPXE PointerPxe;
#endif
#if (_MI_PAGING_LEVELS >= 3)
    PMMPPE PointerPpe;
#endif
    KIRQL OldIrql;

    /* Called with the exclusive working set lock held. Fault any paged-out
       page-table level in here, at PASSIVE_LEVEL and without the PFN lock:
       the fault path takes the working set lock, so doing it under the PFN
       lock (MiMakeSystemAddressValidPfn) would re-enter the working set lock
       we already own. Levels that were never materialized are simply
       skipped. This mirrors MiAweUnmapRange. */
#if (_MI_PAGING_LEVELS >= 4)
    PointerPxe = MiAddressToPxe((PVOID)Va);
    if (!PointerPxe->u.Long) return;
    if (!PointerPxe->u.Hard.Valid)
    {
        MiMakeSystemAddressValid(MiPteToAddress((PMMPTE)PointerPxe), Process);
    }
#endif
#if (_MI_PAGING_LEVELS >= 3)
    PointerPpe = MiAddressToPpe((PVOID)Va);
    if (!PointerPpe->u.Long) return;
    if (!PointerPpe->u.Hard.Valid)
    {
        MiMakeSystemAddressValid(MiPteToAddress((PMMPTE)PointerPpe), Process);
    }
#endif
    PointerPde = MiAddressToPde((PVOID)Va);
    if (!PointerPde->u.Long) return;
    if (!PointerPde->u.Hard.Valid)
    {
        MiMakeSystemAddressValid(MiPteToAddress(PointerPde), Process);
    }

    OldIrql = MiAcquirePfnLock();

    /* The scatter unwind can present addresses outside any AWE region;
       leave any mapping that is not an AWE page untouched, including
       device/MMIO pages whose frame has no PFN database entry */
    PointerPte = MiAddressToPte((PVOID)Va);
    if (PointerPte->u.Hard.Valid)
    {
        Pfn1 = MiGetPfnEntry(PFN_FROM_PTE(PointerPte));
        if ((Pfn1 != NULL) && (Pfn1->u4.PteFrame == MI_AWE_PTE_FRAME))
        {
            MiAweUnmapPte(PointerPte);
            MiFlushProcessTbRange((PVOID)Va, 1);
            if (MiDecrementPageTableReferences((PVOID)Va) == 0)
            {
                MiDeletePde(MiAddressToPde((PVOID)Va), Process, TRUE);
            }
        }
    }

    MiReleasePfnLock(OldIrql);
}

VOID
NTAPI
MiAweUnmapRange(
    _In_ PEPROCESS Process,
    _In_ ULONG_PTR Va,
    _In_ ULONG_PTR EndingAddress)
{
    PMMPDE PointerPde;
    PMMPTE PointerPte;
#if (_MI_PAGING_LEVELS >= 4)
    PMMPXE PointerPxe;
#endif
#if (_MI_PAGING_LEVELS >= 3)
    PMMPPE PointerPpe;
#endif
    KIRQL OldIrql;
    BOOLEAN PdeDeleted;
    ULONG_PTR UnmapVa[MI_AWE_CHUNK];
    ULONG UnmapCount, i;

    /* The caller holds the exclusive working set lock; walk the range the
       same way MiDeleteVirtualAddresses does, but only detach AWE PTEs so
       the physical pages stay granted to the process */
    ASSERT(PsGetCurrentThread()->OwnsProcessWorkingSetExclusive);

    while (Va <= EndingAddress)
    {
#if (_MI_PAGING_LEVELS >= 4)
        /* Skip ranges whose upper hierarchy was never materialized */
        PointerPxe = MiAddressToPxe((PVOID)Va);
        if (!PointerPxe->u.Long)
        {
            Va = (ULONG_PTR)MiPxeToAddress(PointerPxe + 1);
            continue;
        }
        if (!PointerPxe->u.Hard.Valid)
        {
            MiMakeSystemAddressValid(MiPteToAddress((PMMPTE)PointerPxe), Process);
        }
#endif
#if (_MI_PAGING_LEVELS >= 3)
        PointerPpe = MiAddressToPpe((PVOID)Va);
        if (!PointerPpe->u.Long)
        {
            Va = (ULONG_PTR)MiPpeToAddress(PointerPpe + 1);
            continue;
        }
        if (!PointerPpe->u.Hard.Valid)
        {
            MiMakeSystemAddressValid(MiPteToAddress((PMMPTE)PointerPpe), Process);
        }
#endif
        /* Skip page tables that were never materialized */
        PointerPde = MiAddressToPde((PVOID)Va);
        if (!PointerPde->u.Long)
        {
            Va = (ULONG_PTR)MiPdeToAddress(PointerPde + 1);
            continue;
        }

        /* Bring a paged-out page table back before touching its PTEs */
        if (!PointerPde->u.Hard.Valid)
        {
            MiMakeSystemAddressValid(MiPteToAddress(PointerPde), Process);
        }

        PdeDeleted = FALSE;
        UnmapCount = 0;
        OldIrql = MiAcquirePfnLock();
        PointerPte = MiAddressToPte((PVOID)Va);
        do
        {
            if (PointerPte->u.Hard.Valid)
            {
                if (UnmapCount == MI_AWE_CHUNK) break;
                MiAweUnmapPte(PointerPte);
                UnmapVa[UnmapCount++] = Va;
            }

            Va += PAGE_SIZE;
            PointerPte++;
        } while ((Va & (PDE_MAPPED_VA - 1)) && (Va <= EndingAddress));

        if (UnmapCount != 0)
        {
            MiFlushProcessTbRange((PVOID)UnmapVa[0], BYTES_TO_PAGES(UnmapVa[UnmapCount - 1] - UnmapVa[0] + PAGE_SIZE));

            for (i = 0; i < UnmapCount; i++)
            {
                if (MiDecrementPageTableReferences((PVOID)UnmapVa[i]) == 0)
                {
                    MiDeletePde(MiAddressToPde((PVOID)UnmapVa[i]), Process, TRUE);
                    PdeDeleted = TRUE;
                    break;
                }
            }
        }
        MiReleasePfnLock(OldIrql);

        if (PdeDeleted)
        {
            Va = (ULONG_PTR)MiPdeToAddress(PointerPde + 1);
        }
    }
}

#endif /* !defined(_M_ARM64) */

VOID
NTAPI
MiAweProcessCleanup(
    _In_ PEPROCESS Process)
{
    PMI_AWE_INFO AweInfo;
    PULONG Buffer;
    ULONG WordIndex, WordCount;
    ULONG BitIndex;
    ULONG Word;
    KIRQL OldIrql;
    SIZE_T QuotaCharge;

    /* All VADs are gone by now, so no AWE page can still be mapped; give
       every page the process still owns back to the free list */
    AweInfo = Process->AweInfo;
    if (AweInfo == NULL) return;
    Process->AweInfo = NULL;

    Buffer = AweInfo->PfnBitMap.Buffer;
    WordCount = ALIGN_UP_BY(AweInfo->PfnBitMap.SizeOfBitMap, 32) / 32;
    for (WordIndex = 0; WordIndex < WordCount; WordIndex++)
    {
        if (Buffer[WordIndex] == 0) continue;

        OldIrql = MiAcquirePfnLock();
        Word = Buffer[WordIndex];
        while (Word != 0)
        {
            BitIndex = RtlFindLeastSignificantBit((ULONGLONG)Word);
            Word &= ~(1UL << BitIndex);
            MiAweReleasePage(AweInfo, (PFN_NUMBER)WordIndex * 32 + BitIndex);
        }
        MiReleasePfnLock(OldIrql);
    }
    ASSERT(AweInfo->PagesAllocated == 0);

    QuotaCharge = AweInfo->QuotaCharge;
    ExFreePoolWithTag(AweInfo, TAG_MI_AWE);
    PsReturnProcessNonPagedPoolQuota(Process, QuotaCharge);
}

static
NTSTATUS
MiAweReferenceProcess(
    _In_ HANDLE ProcessHandle,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Outptr_ PEPROCESS *ReturnedProcess)
{
    return ObReferenceObjectByHandle(ProcessHandle, PROCESS_VM_OPERATION, PsProcessType, PreviousMode, (PVOID*)ReturnedProcess, NULL);
}

static
NTSTATUS
MiAweMapPages(
    _In_ PEPROCESS Process,
    _In_ PMI_AWE_INFO AweInfo,
    _In_ ULONG_PTR BaseAddress,
    _In_ ULONG_PTR NumberOfPages,
    _In_reads_(NumberOfPages) PULONG_PTR PfnArray)
{
    ULONG_PTR Done;
    NTSTATUS Status;

    /* Called with the address space and exclusive working set locks held */

    /* Validate and claim every PFN before replacing the first PTE. This also
       rejects duplicates, because a second occurrence sees the claim marker. */
    Status = MiAweReservePages(AweInfo, PfnArray, NumberOfPages, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    /* Materialize the complete hierarchy before any existing mapping changes. */
    for (Done = 0; Done < NumberOfPages; Done++)
    {
        Status = MiAweEnsureMappingPte(Process, BaseAddress + (Done << PAGE_SHIFT));
        if (!NT_SUCCESS(Status))
        {
#if defined(_M_ARM64)
            /* The failing address was pruned by MiAweEnsureMappingPte. */
            while (Done != 0)
            {
                Done--;
                MiArm64PruneEmptyUserPageTables(Process, (PVOID)(BaseAddress + (Done << PAGE_SHIFT)));
            }
#endif
            MiAweReleaseReservations(AweInfo, PfnArray, NumberOfPages, FALSE);
            return Status;
        }
    }

    /* The prepared hierarchy and claimed PFNs make this phase infallible. */
    for (Done = 0; Done < NumberOfPages; Done++)
    {
        MiAweMapVa(Process, AweInfo, BaseAddress + (Done << PAGE_SHIFT), PfnArray[Done]);
    }

    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtAllocateUserPhysicalPages(IN HANDLE ProcessHandle,
                            IN OUT PULONG_PTR NumberOfPages,
                            IN OUT PULONG_PTR UserPfnArray)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    PMI_AWE_INFO AweInfo;
    PFN_NUMBER Chunk[MI_AWE_CHUNK];
    ULONG64 ZeroMask;
    ULONG_PTR Request, Granted;
    ULONG ChunkCount, ChunkWanted, i;
    PFN_NUMBER Page;
    PMMPFN Pfn1;
    BOOLEAN NeedsZero;
    KIRQL OldIrql;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    /* Capture and probe the parameters */
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(NumberOfPages, sizeof(*NumberOfPages), sizeof(ULONG_PTR));
        }
        Request = *NumberOfPages;
        if ((PreviousMode != KernelMode) && (Request != 0))
        {
            if (Request > (MAXULONG_PTR / sizeof(ULONG_PTR)))
            {
                _SEH2_YIELD(return STATUS_INVALID_PARAMETER_2);
            }
            ProbeForWrite(UserPfnArray, Request * sizeof(ULONG_PTR), sizeof(ULONG_PTR));
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /* Physical page ownership requires the lock memory privilege */
    if (!SeSinglePrivilegeCheck(SeLockMemoryPrivilege, PreviousMode))
    {
        DPRINT1("Privilege not held for NtAllocateUserPhysicalPages\n");
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = MiAweReferenceProcess(ProcessHandle, PreviousMode, &Process);
    if (!NT_SUCCESS(Status)) return Status;

    if (Request == 0)
    {
        _SEH2_TRY
        {
            *NumberOfPages = 0;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ObDereferenceObject(Process);
        return Status;
    }

    MmLockAddressSpace(&Process->Vm);
    if (Process->VmDeleted)
    {
        Status = STATUS_PROCESS_IS_TERMINATING;
        goto Cleanup;
    }

    Status = MiAweGetInfo(Process, TRUE, &AweInfo);
    if (!NT_SUCCESS(Status)) goto Cleanup;

    Status = STATUS_SUCCESS;
    Granted = 0;
    while (Granted < Request)
    {
        ChunkWanted = (ULONG)min(Request - Granted, MI_AWE_CHUNK);

        /* Grab a chunk of pages under a single PFN lock hold. The pages are
           claimed but not yet made visible in the ownership bitmap. */
        ZeroMask = 0;
        ChunkCount = 0;
        OldIrql = MiAcquirePfnLock();
        while (ChunkCount < ChunkWanted)
        {
            /* Always leave a healthy amount of pages to the rest of Mm;
               a partial grant is expected AWE behavior */
            if (MmAvailablePages < MI_AWE_MINIMUM_AVAILABLE_PAGES) break;

            MI_SET_USAGE(MI_USAGE_MDL);
            MI_SET_PROCESS2(Process->ImageFileName);
            Page = MiAweRemovePage(&NeedsZero);
            if (!Page) break;
            if (NeedsZero) ZeroMask |= 1ULL << ChunkCount;

            /* Claim the page the same way MDL pages are owned: referenced,
               share count one and no PTE mapping it yet */
            Pfn1 = MiGetPfnEntry(Page);
            ASSERT(Pfn1->u3.e2.ReferenceCount == 0);
            Pfn1->u3.e2.ReferenceCount = 1;
            Pfn1->u2.ShareCount = 1;
            Pfn1->u3.e1.PageLocation = ActiveAndValid;
            Pfn1->PteAddress = MI_AWE_UNMAPPED_PTE;
            Pfn1->u4.PteFrame = MI_AWE_PTE_FRAME;
            Pfn1->u4.VerifierAllocation = 0;

            Chunk[ChunkCount++] = Page;
        }
        MiReleasePfnLock(OldIrql);

        /* Zero the fallback pages before publishing any ownership: the
           moment a bitmap bit is set, other threads of the process can map
           the page or free it, so it must not carry stale contents and it
           must not be zeroed after a concurrent free reuses it */
        for (i = 0; i < ChunkCount; i++)
        {
            if (ZeroMask & (1ULL << i)) MiZeroPfn(Chunk[i]);
        }

        /* Now make the chunk visible as process-owned */
        OldIrql = MiAcquirePfnLock();
        for (i = 0; i < ChunkCount; i++)
        {
            RtlSetBit(&AweInfo->PfnBitMap, (ULONG)Chunk[i]);
        }
        AweInfo->PagesAllocated += ChunkCount;
        MiReleasePfnLock(OldIrql);

        /* Publish the chunk to the caller. Once any PFN may have reached user
           mode it is no longer private to this call: another thread can use
           it as soon as the address-space lock is released. A write fault
           therefore leaves the whole published chunk owned by the process. */
        _SEH2_TRY
        {
            for (i = 0; i < ChunkCount; i++)
            {
                UserPfnArray[Granted + i] = Chunk[i];
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Granted += ChunkCount;
        if (ChunkCount < ChunkWanted) break;
    }

    if (Granted == 0)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    /* Report how many pages the process actually received on success */
    _SEH2_TRY
    {
        *NumberOfPages = Granted;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

Cleanup:
    MmUnlockAddressSpace(&Process->Vm);
    ObDereferenceObject(Process);
    return Status;
}

NTSTATUS
NTAPI
NtFreeUserPhysicalPages(IN HANDLE ProcessHandle,
                        IN OUT PULONG_PTR NumberOfPages,
                        IN OUT PULONG_PTR UserPfnArray)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    PETHREAD Thread = PsGetCurrentThread();
    PMI_AWE_INFO AweInfo;
    MI_AWE_CAPTURE Capture;
    KAPC_STATE ApcState;
    PFN_NUMBER MappedPage[MI_AWE_CHUNK];
    ULONG_PTR MappedVa[MI_AWE_CHUNK];
    ULONG MappedCount;
    ULONG_PTR Request, Processed, Freed, Page;
    ULONG ChunkCount, ValidCount, i, j;
    PMMPFN Pfn1;
    KIRQL OldIrql;
    BOOLEAN Attached = FALSE;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    /* Capture and probe the page count */
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWrite(NumberOfPages, sizeof(*NumberOfPages), sizeof(ULONG_PTR));
        }
        Request = *NumberOfPages;
        *NumberOfPages = 0;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Request > (MAXULONG_PTR / sizeof(ULONG_PTR)))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    Status = MiAweReferenceProcess(ProcessHandle, PreviousMode, &Process);
    if (!NT_SUCCESS(Status)) return Status;
    Status = STATUS_SUCCESS;

    Freed = 0;
    Capture.Buffer = NULL;
    if (Request == 0) goto WriteBack;

    /* Capture the whole frame list before attaching or taking any Mm lock. */
    Status = MiAweCaptureUlongPtrArray(&Capture, UserPfnArray, Request, PreviousMode);
    if (!NT_SUCCESS(Status)) goto WriteBack;

    if (Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
        Attached = TRUE;
    }

    MmLockAddressSpace(&Process->Vm);
    if (Process->VmDeleted)
    {
        Status = STATUS_PROCESS_IS_TERMINATING;
        goto UnlockAddressSpace;
    }

    Status = MiAweGetInfo(Process, FALSE, &AweInfo);
    ASSERT(NT_SUCCESS(Status));
    if (AweInfo == NULL)
    {
        Status = STATUS_INVALID_PARAMETER_3;
        goto UnlockAddressSpace;
    }

    MiLockProcessWorkingSetUnsafe(Process, Thread);

    Processed = 0;
    while (Processed < Request)
    {
        ChunkCount = (ULONG)min(Request - Processed, MI_AWE_CHUNK);

        /* Free the unmapped pages under the PFN lock and collect the ones
           that are still mapped; the exclusive working set lock keeps the
           mapping state stable while the lock is dropped in between */
        MappedCount = 0;
        ValidCount = 0;
        OldIrql = MiAcquirePfnLock();
        for (i = 0; i < ChunkCount; i++)
        {
            Page = Capture.Buffer[Processed + i];
            if (!MiAweOwnsPage(AweInfo, Page))
            {
                Status = STATUS_INVALID_PARAMETER_3;
                break;
            }

            Pfn1 = MiGetPfnEntry(Page);
            if (!MI_IS_PFN_DELETED(Pfn1))
            {
                for (j = 0; j < MappedCount; j++)
                {
                    if (MappedPage[j] == Page) break;
                }
                if (j != MappedCount)
                {
                    Status = STATUS_INVALID_PARAMETER_3;
                    break;
                }

                MappedPage[MappedCount] = Page;
                MappedVa[MappedCount] = (ULONG_PTR)MiPteToAddress(Pfn1->PteAddress);
                MappedCount++;
            }
            else
            {
                MiAweReleasePage(AweInfo, Page);
                Freed++;
            }
            ValidCount++;
        }
        MiReleasePfnLock(OldIrql);

        /* A page that is still mapped gets unmapped by the free */
        for (i = 0; i < MappedCount; i++)
        {
            MiAweUnmapVa(Process, MappedVa[i]);
        }

        if (MappedCount != 0)
        {
            OldIrql = MiAcquirePfnLock();
            for (i = 0; i < MappedCount; i++)
            {
                ASSERT(MiAweOwnsPage(AweInfo, MappedPage[i]));
                MiAweReleasePage(AweInfo, MappedPage[i]);
                Freed++;
            }
            MiReleasePfnLock(OldIrql);
        }

        Processed += ValidCount;
        if (!NT_SUCCESS(Status)) break;
    }

    MiUnlockProcessWorkingSetUnsafe(Process, Thread);

UnlockAddressSpace:
    MmUnlockAddressSpace(&Process->Vm);
    if (Attached) KeUnstackDetachProcess(&ApcState);
    MiAweReleaseCapture(&Capture);

WriteBack:
    /* Report how many pages were freed, even on failure */
    _SEH2_TRY
    {
        *NumberOfPages = Freed;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* The pages are gone regardless */
    }
    _SEH2_END;

    ObDereferenceObject(Process);
    return Status;
}

NTSTATUS
NTAPI
NtMapUserPhysicalPages(IN PVOID VirtualAddresses,
                       IN ULONG_PTR NumberOfPages,
                       IN OUT PULONG_PTR UserPfnArray)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();
    PMI_AWE_INFO AweInfo;
    MI_AWE_CAPTURE Capture;
    PMMVAD Vad;
    ULONG_PTR BaseAddress, EndingAddress;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (NumberOfPages == 0) return STATUS_SUCCESS;
    if ((NumberOfPages > (MAXULONG_PTR / PAGE_SIZE)) ||
        (NumberOfPages > (MAXULONG_PTR / sizeof(ULONG_PTR))))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    /* The whole run must lie in user space */
    BaseAddress = (ULONG_PTR)PAGE_ALIGN(VirtualAddresses);
    EndingAddress = BaseAddress + (NumberOfPages << PAGE_SHIFT) - 1;
    if ((EndingAddress < BaseAddress) ||
        (EndingAddress > (ULONG_PTR)MM_HIGHEST_VAD_ADDRESS))
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    /* Capture the frame list before taking any Mm lock */
    Capture.Buffer = NULL;
    if (UserPfnArray != NULL)
    {
        Status = MiAweCaptureUlongPtrArray(&Capture, UserPfnArray, NumberOfPages, PreviousMode);
        if (!NT_SUCCESS(Status)) return Status;
    }

    MmLockAddressSpace(&Process->Vm);
    if (Process->VmDeleted)
    {
        Status = STATUS_PROCESS_IS_TERMINATING;
        goto Cleanup;
    }

    Status = MiAweGetInfo(Process, FALSE, &AweInfo);
    ASSERT(NT_SUCCESS(Status));
    if ((AweInfo == NULL) && (UserPfnArray != NULL))
    {
        /* No page was ever granted to this process */
        Status = STATUS_INVALID_PARAMETER_3;
        goto Cleanup;
    }

    /* The run must lie within a single AWE region */
    Vad = MiLocateAddress((PVOID)BaseAddress);
    if ((Vad == NULL) ||
        (Vad->u.VadFlags.VadType != VadAwe) ||
        ((EndingAddress >> PAGE_SHIFT) > Vad->EndingVpn))
    {
        Status = STATUS_INVALID_PARAMETER_1;
        goto Cleanup;
    }

    MiLockProcessWorkingSetUnsafe(Process, Thread);

    if (UserPfnArray != NULL)
    {
        Status = MiAweMapPages(Process, AweInfo, BaseAddress, NumberOfPages, Capture.Buffer);
    }
    else
    {
        MiAweUnmapRange(Process, BaseAddress, EndingAddress);
        Status = STATUS_SUCCESS;
    }

    MiUnlockProcessWorkingSetUnsafe(Process, Thread);

Cleanup:
    MmUnlockAddressSpace(&Process->Vm);
    MiAweReleaseCapture(&Capture);
    return Status;
}

NTSTATUS
NTAPI
NtMapUserPhysicalPagesScatter(IN PVOID *VirtualAddresses,
                              IN ULONG_PTR NumberOfPages,
                              IN OUT PULONG_PTR UserPfnArray)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();
    PMI_AWE_INFO AweInfo;
    MI_AWE_CAPTURE VaCapture, PfnCapture;
    PMMVAD Vad;
    ULONG_PTR Index, Page, CurrentVa, Vpn;
    ULONG_PTR CachedStartVpn = 1, CachedEndVpn = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (NumberOfPages == 0) return STATUS_SUCCESS;
    if (NumberOfPages > (MAXULONG_PTR / sizeof(ULONG_PTR)))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    /* Capture both untrusted arrays before taking any Mm lock. */
    VaCapture.Buffer = NULL;
    PfnCapture.Buffer = NULL;
    Status = MiAweCaptureUlongPtrArray(&VaCapture, (PULONG_PTR)VirtualAddresses, NumberOfPages, PreviousMode);
    if (!NT_SUCCESS(Status)) return Status;

    if (UserPfnArray != NULL)
    {
        Status = MiAweCaptureUlongPtrArray(&PfnCapture, UserPfnArray, NumberOfPages, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            MiAweReleaseCapture(&VaCapture);
            return Status;
        }
    }

    MmLockAddressSpace(&Process->Vm);
    if (Process->VmDeleted)
    {
        MmUnlockAddressSpace(&Process->Vm);
        Status = STATUS_PROCESS_IS_TERMINATING;
        goto CleanupNoLock;
    }
    MiLockProcessWorkingSetUnsafe(Process, Thread);

    Status = MiAweGetInfo(Process, FALSE, &AweInfo);
    ASSERT(NT_SUCCESS(Status));
    if ((AweInfo == NULL) && (UserPfnArray != NULL))
    {
        Status = STATUS_INVALID_PARAMETER_3;
        goto Unlock;
    }

    /* Validate every address before changing the first PTE. */
    for (Index = 0; Index < NumberOfPages; Index++)
    {
        CurrentVa = (ULONG_PTR)PAGE_ALIGN(VaCapture.Buffer[Index]);

        /* Every address must lie in an AWE region of the process */
        Vpn = CurrentVa >> PAGE_SHIFT;
        if ((Vpn < CachedStartVpn) || (Vpn > CachedEndVpn))
        {
            Vad = MiLocateAddress((PVOID)CurrentVa);
            if ((Vad == NULL) || (Vad->u.VadFlags.VadType != VadAwe))
            {
                Status = STATUS_INVALID_PARAMETER_1;
                goto Unlock;
            }
            CachedStartVpn = Vad->StartingVpn;
            CachedEndVpn = Vad->EndingVpn;
        }
    }

    if (UserPfnArray != NULL)
    {
        /* Validate and claim the complete PFN list before doing fallible page
           table work. Release every claim if materialization fails. */
        Status = MiAweReservePages(AweInfo, PfnCapture.Buffer, NumberOfPages, TRUE);
        if (!NT_SUCCESS(Status)) goto Unlock;

        /* Materialize every required page table before any target PTE changes. */
        for (Index = 0; Index < NumberOfPages; Index++)
        {
            Page = PfnCapture.Buffer[Index];
            if (Page == 0) continue;

            CurrentVa = (ULONG_PTR)PAGE_ALIGN(VaCapture.Buffer[Index]);
            Status = MiAweEnsureMappingPte(Process, CurrentVa);
            if (!NT_SUCCESS(Status))
            {
#if defined(_M_ARM64)
                /* The failing address was pruned by MiAweEnsureMappingPte. */
                while (Index != 0)
                {
                    Index--;
                    if (PfnCapture.Buffer[Index] == 0) continue;
                    CurrentVa = (ULONG_PTR)PAGE_ALIGN(VaCapture.Buffer[Index]);
                    MiArm64PruneEmptyUserPageTables(Process, (PVOID)CurrentVa);
                }
#endif
                MiAweReleaseReservations(AweInfo, PfnCapture.Buffer, NumberOfPages, TRUE);
                goto Unlock;
            }
        }
    }

    /* All fallible validation is complete, so no rollback can touch an
       unvalidated address or destroy a mapping that predated a failed call. */
    for (Index = 0; Index < NumberOfPages; Index++)
    {
        CurrentVa = (ULONG_PTR)PAGE_ALIGN(VaCapture.Buffer[Index]);
        Page = (UserPfnArray != NULL) ? PfnCapture.Buffer[Index] : 0;
        if (Page == 0)
        {
            MiAweUnmapVa(Process, CurrentVa);
        }
        else
        {
            MiAweMapVa(Process, AweInfo, CurrentVa, Page);
        }
    }

    Status = STATUS_SUCCESS;

Unlock:
    MiUnlockProcessWorkingSetUnsafe(Process, Thread);
    MmUnlockAddressSpace(&Process->Vm);

CleanupNoLock:
    MiAweReleaseCapture(&VaCapture);
    MiAweReleaseCapture(&PfnCapture);
    return Status;
}

/* EOF */
