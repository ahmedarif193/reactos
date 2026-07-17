/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         ARM64 process address space initialization
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

BOOLEAN
MiArchCreateProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase)
{
    PFN_NUMBER RootPfn, HyperPfn, HyperPdPfn, HyperPtPfn;
    PMMPTE MappingPte;
    PMMPTE PageTable;
    MMPTE TempPte, MapPte;
    ULONG Index;
    KIRQL OldIrql;

    ASSERT(DirectoryTableBase != NULL);

    RootPfn = DirectoryTableBase[0] >> PAGE_SHIFT;
    HyperPfn = DirectoryTableBase[1] >> PAGE_SHIFT;

    /* Allocate backing pages for the hyperspace PD/PT (colored, zeroed,
     * KSEG0-mapped and PoC-cleaned by the shared helper) */
    MI_SET_USAGE(MI_USAGE_PAGE_DIRECTORY);
    if (!NT_SUCCESS(MiArm64AllocateCleanPage(Process, &HyperPdPfn)))
    {
        DirectoryTableBase[0] = 0;
        DirectoryTableBase[1] = 0;
        return FALSE;
    }

    MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    if (!NT_SUCCESS(MiArm64AllocateCleanPage(Process, &HyperPtPfn)))
    {
        OldIrql = MiAcquirePfnLock();
        MiInsertPageInFreeList(HyperPdPfn);
        MiReleasePfnLock(OldIrql);
        DirectoryTableBase[0] = 0;
        DirectoryTableBase[1] = 0;
        return FALSE;
    }

    /*
     * The new root is copied from the current kernel half below. Ensure the
     * remaining process page-table pages are reachable through KSEG0 before
     * taking that copy, otherwise a later TTBR switch can leave the debugger
     * and ARM64 table walkers unable to inspect the active root.
     */
    MiArm64MapKseg0Page(RootPfn);
    MiArm64MapKseg0Page(HyperPfn);
    MiArm64MapKseg0Page(Process->WorkingSetPage);

    MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!MappingPte)
    {
        OldIrql = MiAcquirePfnLock();
        MiInsertPageInFreeList(HyperPtPfn);
        MiInsertPageInFreeList(HyperPdPfn);
        MiReleasePfnLock(OldIrql);
        DirectoryTableBase[0] = 0;
        DirectoryTableBase[1] = 0;
        return FALSE;
    }

    PageTable = MiPteToAddress(MappingPte);

    /* Map the new user root */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte,
                                MappingPte,
                                MM_READWRITE,
                                RootPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);
    MI_WRITE_VALID_PTE(MappingPte, MapPte);

    /* Copy kernel half from the master table */
    Index = PXE_PER_PAGE / 2;
    RtlZeroMemory(PageTable, Index * sizeof(MMPTE));
    RtlCopyMemory(PageTable + Index,
                  MiAddressToPxe(0) + Index,
                  PAGE_SIZE - Index * sizeof(MMPTE));

    /*
     * Install kernel recursive entry and hyperspace pointer.
     *
     * ARM64 FIX: Use ValidKernelPde (table descriptor template) instead of
     * ValidKernelPte (page descriptor template). On ARM64, table descriptors
     * (L0/L1/L2) have different attribute layouts than page descriptors (L3).
     * ValidKernelPte has Writable=1 at bit 55, which is reserved in table
     * descriptors and can cause translation faults.
     */
    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = RootPfn;
    Index = MiAddressToPxi((PVOID)PXE_SELFMAP);
    PageTable[Index] = TempPte;

    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = HyperPfn;
    Index = MiAddressToPxi((PVOID)HYPER_SPACE);
    PageTable[Index] = TempPte;

    MiArm64CleanPageToPoC((PVOID)PageTable);

    /* Map hyperspace PDPT (deliberate remap: the PFN changes, so this must
     * not go through MI_UPDATE_VALID_PTE, which asserts PFN equality) */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte,
                                MappingPte,
                                MM_READWRITE,
                                HyperPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);
    *MappingPte = MapPte;
    MiArm64CleanEntryToPoC((volatile UINT64 *)MappingPte);
    KeInvalidateTlbEntry(PageTable);

    /* L1 table entry - use table descriptor template */
    PageTable = MiPteToAddress(MappingPte);
    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = HyperPdPfn;
    PageTable[0] = TempPte;

    MiArm64CleanPageToPoC((PVOID)PageTable);

    /* Map hyperspace PD (remap, see above) */
    MapPte.u.Hard.PageFrameNumber = HyperPdPfn;
    *MappingPte = MapPte;
    MiArm64CleanEntryToPoC((volatile UINT64 *)MappingPte);
    KeInvalidateTlbEntry(PageTable);

    /* L2 table entry - use table descriptor template */
    PageTable = MiPteToAddress(MappingPte);
    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = HyperPtPfn;
    PageTable[0] = TempPte;

    MiArm64CleanPageToPoC((PVOID)PageTable);

    /* Map hyperspace PT (remap, see above) */
    MapPte.u.Hard.PageFrameNumber = HyperPtPfn;
    *MappingPte = MapPte;
    MiArm64CleanEntryToPoC((volatile UINT64 *)MappingPte);
    KeInvalidateTlbEntry(PageTable);

    PageTable = MiPteToAddress(MappingPte);
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = Process->WorkingSetPage;
    Index = MiAddressToPti(MmWorkingSetList);
    PageTable[Index] = TempPte;

    MiArm64CleanPageToPoC((PVOID)PageTable);

    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

    /* Remember the hyper table PFN in the directory base */
    DirectoryTableBase[1] = HyperPfn << PAGE_SHIFT;

    return TRUE;
}
