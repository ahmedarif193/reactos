/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/arm64/maputils.c
 * PURPOSE:         Small ARM64 MM helpers used by common ARM3 code
 *
 * Rationale:
 *  - The boot loader (FreeLdr) often maps initial images using upper-level
 *    block entries (L1/L2) for identity and hand-off mappings. Common code
 *    in ARM3 (e.g., MiReloadBootLoadedDrivers) that assumed a final-level
 *    leaf PTE would assert on ARM64 during bring-up.
 *  - These helpers let shared code ask simple questions without forcing a
 *    leaf-PTE assumption: "is this VA mapped at all?" and "is the given PTE
 *    a leaf entry?". The first uses MmGetPhysicalAddress which tolerates
 *    block descriptors; the second remains a straight check of the PTE.
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

static KSPIN_LOCK MiArm64SystemPageDirectoryLock;

VOID
MiArm64InitializeSystemPageDirectoryLock(VOID)
{
    KeInitializeSpinLock(&MiArm64SystemPageDirectoryLock);
}

BOOLEAN
MiArm64VaIsMapped(
    _In_ PVOID Va)
{
    PHYSICAL_ADDRESS Pa = MmGetPhysicalAddress(Va);
    return (Pa.QuadPart != 0);
}

BOOLEAN
MiArm64PteIsLeaf(
    _In_ PMMPTE Pte)
{
    return (Pte->u.Hard.Valid == 1);
}

static
BOOLEAN
MiArm64EnsureSystemTableEntry(
    _Inout_ PMMPTE Entry,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER ParentPage,
    _Out_ PFN_NUMBER *TablePage)
{
    PFN_NUMBER PageFrameIndex = 0;
    ULONG64 EntryType;
    BOOLEAN Created = FALSE;

    EntryType = Entry->u.Long & ARM64_PTE_TYPE_MASK;
    if (EntryType == ARM64_PTE_TYPE_INVALID)
    {
        if (MiArm64IsPfnDatabaseReady())
        {
            MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
            MI_SET_PROCESS2(PsGetCurrentProcess()->ImageFileName);
        }

        PageFrameIndex = MiArm64AllocatePageTablePage();
        if (PageFrameIndex == 0)
        {
            *TablePage = 0;
            return FALSE;
        }

        if (MiArm64IsPfnDatabaseReady())
        {
            MiInitializePfnForOtherProcess(PageFrameIndex, PteAddress, ParentPage);
        }
        Created = TRUE;
    }
    else
    {
        ASSERT(EntryType == ARM64_PTE_TYPE_TABLE);
        if (EntryType != ARM64_PTE_TYPE_TABLE)
        {
            *TablePage = 0;
            return FALSE;
        }
    }

    *TablePage = Created ? PageFrameIndex : PFN_FROM_PTE(Entry);

    if (Created)
    {
        MiArm64MapKseg0Page(PageFrameIndex);
        RtlZeroMemory((PVOID)MI_ARM64_PFN_TO_VA(PageFrameIndex), PAGE_SIZE);
        MiArm64CleanPageToPoC((PVOID)MI_ARM64_PFN_TO_VA(PageFrameIndex));
        Entry->u.Long = ARM64_MAKE_TABLE_DESCRIPTOR(PageFrameIndex);
        MiArm64CleanEntryToPoC((volatile UINT64 *)Entry);
    }

    return Created;
}

VOID
MiArm64FillSystemPageDirectory(
    _In_ PVOID Base,
    _In_ SIZE_T NumberOfBytes)
{
    PMMPDE PointerPde, LastPde;
    BOOLEAN FlushHierarchy = FALSE;

    PointerPde = MiAddressToPde(Base);
    LastPde = MiAddressToPde((PVOID)((ULONG_PTR)Base + NumberOfBytes - 1));

    while (PointerPde <= LastPde)
    {
        PVOID TargetAddress;
        PMMPTE KernelPxe, KernelPpe, KernelPde;
        PULONG64 Table;
        PFN_NUMBER RootPage, PpePage, PdePage, PageFrameIndex;
        ULONG64 Ttbr1;

        TargetAddress = MiPdeToAddress(PointerPde);

        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        RootPage = (PFN_NUMBER)((Ttbr1 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);

        Table = (PULONG64)MI_ARM64_PFN_TO_VA(RootPage);
        KernelPxe = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PXI_SHIFT) & PXI_MASK];

        FlushHierarchy |= MiArm64EnsureSystemTableEntry(KernelPxe,
                                                        MiAddressToPxe(TargetAddress),
                                                        RootPage,
                                                        &PpePage);
        if (PpePage == 0)
        {
            goto FlushAndReturn;
        }

        Table = (PULONG64)MI_ARM64_PFN_TO_VA(PpePage);
        KernelPpe = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PPI_SHIFT) & PPI_MASK];

        FlushHierarchy |= MiArm64EnsureSystemTableEntry(KernelPpe,
                                                        MiAddressToPpe(TargetAddress),
                                                        PpePage,
                                                        &PdePage);
        if (PdePage == 0)
        {
            goto FlushAndReturn;
        }

        Table = (PULONG64)MI_ARM64_PFN_TO_VA(PdePage);
        KernelPde = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PDI_SHIFT) & PDI_MASK_ARM64];

        FlushHierarchy |= MiArm64EnsureSystemTableEntry(KernelPde,
                                                        (PMMPTE)PointerPde,
                                                        PdePage,
                                                        &PageFrameIndex);
        if (PageFrameIndex == 0)
        {
            goto FlushAndReturn;
        }

        PointerPde++;
    }

FlushAndReturn:
    if (FlushHierarchy)
    {
        __asm__ __volatile__(
            "dsb ish\n\t"
            "isb" ::: "memory");
    }
}

static
BOOLEAN
MiArm64EnsureSystemPdeRangeBacked(
    _In_ PVOID BaseVa,
    _In_ SIZE_T NumberOfBytes)
{
    KIRQL OldIrql;
    PVOID TargetVa;
    PMMPDE PointerPde, LastPde;
    BOOLEAN Backed = TRUE;

    if (NumberOfBytes == 0)
    {
        return TRUE;
    }

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        return FALSE;
    }

    KeAcquireSpinLock(&MiArm64SystemPageDirectoryLock, &OldIrql);
    MiArm64FillSystemPageDirectory(BaseVa, NumberOfBytes);

    PointerPde = MiAddressToPde(BaseVa);
    LastPde = MiAddressToPde(Add2Ptr(BaseVa, NumberOfBytes - 1));
    while (PointerPde <= LastPde)
    {
        TargetVa = MiPdeToAddress(PointerPde);
        if (!MiIsPdeForAddressValid(TargetVa) ||
            ((PointerPde->u.Long & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE))
        {
            Backed = FALSE;
            break;
        }

        PointerPde++;
    }

    KeReleaseSpinLock(&MiArm64SystemPageDirectoryLock, OldIrql);
    return Backed;
}

BOOLEAN
NTAPI
MiEnsureSystemPtesBacked(
    _In_ PMMPTE StartingPte,
    _In_ ULONG NumberOfPtes)
{
    ASSERT(NumberOfPtes != 0);
    return MiArm64EnsureSystemPdeRangeBacked(MiPteToAddress(StartingPte), (SIZE_T)NumberOfPtes << PAGE_SHIFT);
}

BOOLEAN
NTAPI
MiEnsureNonPagedPoolExpansionPtesBacked(
    _In_ PMMPTE StartingPte,
    _In_ ULONG NumberOfPtes)
{
    ASSERT(NumberOfPtes != 0);
    return MiArm64EnsureSystemPdeRangeBacked(MiPteToAddress(StartingPte - 1), ((SIZE_T)NumberOfPtes + 2) << PAGE_SHIFT);
}

BOOLEAN
NTAPI
MiEnsurePagedPoolPdeBacked(
    _In_ PVOID Address)
{
    return MiArm64EnsureSystemPdeRangeBacked(Address, PAGE_SIZE);
}

static
BOOLEAN
MiArm64EnsureSessionPageDirectoryPages(
    _In_ PVOID BaseVa,
    _In_ SIZE_T NumberOfBytes)
{
    KIRQL OldIrql;
    ULONG_PTR Va, EndVa;
    BOOLEAN FlushHierarchy = FALSE;
    BOOLEAN Backed = TRUE;

    if (NumberOfBytes == 0)
    {
        return TRUE;
    }

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        return FALSE;
    }

    EndVa = (ULONG_PTR)BaseVa + NumberOfBytes - 1;

    KeAcquireSpinLock(&MiArm64SystemPageDirectoryLock, &OldIrql);

    /*
     * Walk every L1 (PPE, 1 GB) entry covering the range and make sure the
     * L0->L1 hierarchy (the L2 page-directory page) exists.  We deliberately
     * stop at the PPE level: the leaf L2 PDEs are left clear so the session
     * setup can fill and assert on them.  This replaces the eager session-space
     * MiMapPPEs premap with on-demand backing, like AMD64.
     */
    for (Va = (ULONG_PTR)BaseVa & ~((1ULL << PPI_SHIFT) - 1);
         Va <= EndVa;
         Va += (1ULL << PPI_SHIFT))
    {
        PVOID TargetAddress = (PVOID)Va;
        PMMPTE KernelPxe, KernelPpe;
        PULONG64 Table;
        PFN_NUMBER RootPage, PpePage, PdePage;
        ULONG64 Ttbr1;

        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        RootPage = (PFN_NUMBER)((Ttbr1 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);

        Table = (PULONG64)MI_ARM64_PFN_TO_VA(RootPage);
        KernelPxe = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PXI_SHIFT) & PXI_MASK];
        FlushHierarchy |= MiArm64EnsureSystemTableEntry(KernelPxe,
                                                        MiAddressToPxe(TargetAddress),
                                                        RootPage,
                                                        &PpePage);
        if (PpePage == 0)
        {
            Backed = FALSE;
            break;
        }

        Table = (PULONG64)MI_ARM64_PFN_TO_VA(PpePage);
        KernelPpe = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PPI_SHIFT) & PPI_MASK];
        FlushHierarchy |= MiArm64EnsureSystemTableEntry(KernelPpe,
                                                        MiAddressToPpe(TargetAddress),
                                                        PpePage,
                                                        &PdePage);
        if (PdePage == 0)
        {
            Backed = FALSE;
            break;
        }

        /* PdePage is the L2 page-directory page; leave the L2 PDEs clear. */
    }

    if (FlushHierarchy)
    {
        __asm__ __volatile__(
            "dsb ish\n\t"
            "isb" ::: "memory");
    }

    KeReleaseSpinLock(&MiArm64SystemPageDirectoryLock, OldIrql);
    return Backed;
}

BOOLEAN
NTAPI
MiEnsureSessionPageTablesBacked(
    _In_ PVOID BaseVa,
    _In_ SIZE_T NumberOfBytes)
{
    return MiArm64EnsureSessionPageDirectoryPages(BaseVa, NumberOfBytes);
}
