/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/arm64/maputils.c
 * PURPOSE:         Small ARM64 MM helpers used by common ARM3 code
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
    PEPROCESS Process = PsGetCurrentProcess();

    KeInitializeSpinLock(&MiArm64SystemPageDirectoryLock);
    InitializeListHead(&MmProcessList);

    /*
     * The replication walk identifies each root through its translation base,
     * so a root published without one would silently never receive any new
     * kernel L0 descriptor. Every later root is published with its base set
     * under the lock; assert the same for the one seeded here.
     */
    if (KPROCESS_DTB0(&Process->Pcb) == 0)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, (ULONG_PTR)Process, 0, 0, 0);
    }

    InsertTailList(&MmProcessList, &Process->MmProcessLinks);
}

static
VOID
MiArm64ReplicateTopLevelEntry(
    _In_ PFN_NUMBER SourceRootPage,
    _In_ ULONG Index,
    _In_ ULONG64 Descriptor)
{
    PLIST_ENTRY ListEntry;

    /*
     * The self-map and hyperspace top-level descriptors are per-process: each
     * root points at its own tables there, installed by
     * MiArm64FinalizeProcessAddressSpace. Replicating one would stamp a single
     * process's private mappings over every other root, so leave those slots
     * alone. Hyperspace covers the mapping range, dummy PTE, VAD bitmap and
     * working set list, which all share its top-level slot. This mirrors the
     * PTE_BASE..HYPER_SPACE_END exclusion in MiArm64SyncKernelHierarchyEntryWrite.
     */
    if ((Index == MiAddressToPxi((PVOID)PXE_SELFMAP)) ||
        (Index == MiAddressToPxi((PVOID)HYPER_SPACE)))
    {
        return;
    }

    for (ListEntry = MmProcessList.Flink;
         ListEntry != &MmProcessList;
         ListEntry = ListEntry->Flink)
    {
        PEPROCESS Process;
        PFN_NUMBER TargetRootPage;
        volatile ULONG64 *TargetRoot;
        ULONG64 TargetDescriptor;

        Process = CONTAINING_RECORD(ListEntry, EPROCESS, MmProcessLinks);
        TargetRootPage = (KPROCESS_DTB0(&Process->Pcb) & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
        if (TargetRootPage == 0)
        {
            /*
             * A published root always carries its translation base: it is set
             * under this lock before the process joins the list, and the
             * process leaves the list before the base is torn down. Skipping
             * one would leave that root missing this descriptor and reproduce
             * the exact fault this replication exists to prevent.
             */
            KeBugCheckEx(MEMORY_MANAGEMENT,
                         (ULONG_PTR)Process,
                         Index,
                         Descriptor,
                         0);
        }

        if (TargetRootPage == SourceRootPage)
        {
            continue;
        }

        TargetRoot = (volatile ULONG64 *)MI_ARM64_PFN_TO_VA(TargetRootPage);
        TargetDescriptor = TargetRoot[Index];
        if (TargetDescriptor == Descriptor)
        {
            continue;
        }

        if ((TargetDescriptor & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_INVALID)
        {
            KeBugCheckEx(MEMORY_MANAGEMENT,
                         (ULONG_PTR)&TargetRoot[Index],
                         TargetDescriptor,
                         Descriptor,
                         Index);
        }

        TargetRoot[Index] = Descriptor;
        MiArm64CleanEntryToPoC(&TargetRoot[Index]);
    }
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

/*
 * Ensure the kernel L0->L1(->L2) table hierarchy exists for TargetAddress.
 * StopAtPpe stops after the PPE level and returns the L2 page-directory page;
 * otherwise the walk continues and returns the L3 page-table page. Returns 0
 * on allocation failure. RootPage is the (loop-invariant) TTBR1 root frame.
 */
static
PFN_NUMBER
MiArm64EnsureKernelHierarchy(
    _In_ PFN_NUMBER RootPage,
    _In_ PVOID TargetAddress,
    _In_ BOOLEAN StopAtPpe,
    _Inout_ PBOOLEAN FlushHierarchy)
{
    PMMPTE KernelPxe, KernelPpe, KernelPde;
    PULONG64 Table;
    PFN_NUMBER PpePage, PdePage, PageFrameIndex;
    BOOLEAN PxeCreated;

    Table = (PULONG64)MI_ARM64_PFN_TO_VA(RootPage);
    KernelPxe = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PXI_SHIFT) & PXI_MASK];
    PxeCreated = MiArm64EnsureSystemTableEntry(KernelPxe,
                                               MiAddressToPxe(TargetAddress),
                                               RootPage,
                                               &PpePage);
    *FlushHierarchy |= PxeCreated;
    if (PpePage == 0)
    {
        return 0;
    }

    if (PxeCreated)
    {
        MiArm64ReplicateTopLevelEntry(RootPage,
                                      ((ULONG_PTR)TargetAddress >> PXI_SHIFT) & PXI_MASK,
                                      KernelPxe->u.Long);
    }

    Table = (PULONG64)MI_ARM64_PFN_TO_VA(PpePage);
    KernelPpe = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PPI_SHIFT) & PPI_MASK];
    *FlushHierarchy |= MiArm64EnsureSystemTableEntry(KernelPpe,
                                                     MiAddressToPpe(TargetAddress),
                                                     PpePage,
                                                     &PdePage);
    if (StopAtPpe || (PdePage == 0))
    {
        return PdePage;
    }

    Table = (PULONG64)MI_ARM64_PFN_TO_VA(PdePage);
    KernelPde = (PMMPTE)&Table[((ULONG_PTR)TargetAddress >> PDI_SHIFT) & PDI_MASK_ARM64];
    *FlushHierarchy |= MiArm64EnsureSystemTableEntry(KernelPde,
                                                     (PMMPTE)MiAddressToPde(TargetAddress),
                                                     PdePage,
                                                     &PageFrameIndex);
    return PageFrameIndex;
}

FORCEINLINE
PFN_NUMBER
MiArm64ReadTtbr1RootPage(VOID)
{
    ULONG64 Ttbr1;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    return (PFN_NUMBER)((Ttbr1 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
}

static
BOOLEAN
MiArm64FillSystemPageDirectoryLocked(
    _In_ PVOID Base,
    _In_ SIZE_T NumberOfBytes)
{
    PMMPDE PointerPde, LastPde;
    PFN_NUMBER RootPage;
    BOOLEAN FlushHierarchy = FALSE;
    BOOLEAN Success = TRUE;

    RootPage = MiArm64ReadTtbr1RootPage();
    PointerPde = MiAddressToPde(Base);
    LastPde = MiAddressToPde((PVOID)((ULONG_PTR)Base + NumberOfBytes - 1));

    while (PointerPde <= LastPde)
    {
        PVOID TargetAddress = MiPdeToAddress(PointerPde);

        if (MiArm64EnsureKernelHierarchy(RootPage, TargetAddress, FALSE, &FlushHierarchy) == 0)
        {
            /* Out of pages for the hierarchy. Flush whatever was committed,
             * then report the shortfall: a caller that treats this range as
             * mapped would fault on first touch. */
            Success = FALSE;
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

    return Success;
}

BOOLEAN
MiArm64FillSystemPageDirectory(
    _In_ PVOID Base,
    _In_ SIZE_T NumberOfBytes)
{
    KIRQL OldIrql;
    BOOLEAN Success;

    if (NumberOfBytes == 0)
    {
        return TRUE;
    }

    KeAcquireSpinLock(&MiArm64SystemPageDirectoryLock, &OldIrql);
    Success = MiArm64FillSystemPageDirectoryLocked(Base, NumberOfBytes);
    KeReleaseSpinLock(&MiArm64SystemPageDirectoryLock, OldIrql);

    return Success;
}

VOID
MiArm64FinalizeProcessAddressSpace(
    _Inout_ PEPROCESS Process,
    _In_ PFN_NUMBER RootPfn,
    _In_ PFN_NUMBER HyperPfn)
{
    KIRQL OldIrql;
    PFN_NUMBER CurrentRootPfn;
    PMMPTE CurrentRoot, NewRoot;
    MMPTE TempPte;
    ULONG Index;

    NewRoot = (PMMPTE)MI_ARM64_PFN_TO_VA(RootPfn);

    KeAcquireSpinLock(&MiArm64SystemPageDirectoryLock, &OldIrql);

    CurrentRootPfn = MiArm64ReadTtbr1RootPage();
    CurrentRoot = (PMMPTE)MI_ARM64_PFN_TO_VA(CurrentRootPfn);
    Index = PXE_PER_PAGE / 2;
    RtlCopyMemory(NewRoot + Index,
                  CurrentRoot + Index,
                  PAGE_SIZE - Index * sizeof(MMPTE));

    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = RootPfn;
    NewRoot[MiAddressToPxi((PVOID)PXE_SELFMAP)] = TempPte;

    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = HyperPfn;
    NewRoot[MiAddressToPxi((PVOID)HYPER_SPACE)] = TempPte;

    MiArm64CleanPageToPoC(NewRoot);

    if ((Process->MmProcessLinks.Flink != NULL) ||
        (Process->MmProcessLinks.Blink != NULL))
    {
        KeBugCheckEx(MEMORY_MANAGEMENT,
                     (ULONG_PTR)Process,
                     (ULONG_PTR)Process->MmProcessLinks.Flink,
                     (ULONG_PTR)Process->MmProcessLinks.Blink,
                     RootPfn);
    }

    KPROCESS_DTB0(&Process->Pcb) = RootPfn << PAGE_SHIFT;
    InsertTailList(&MmProcessList, &Process->MmProcessLinks);

    KeReleaseSpinLock(&MiArm64SystemPageDirectoryLock, OldIrql);
}

VOID
MiArm64RemoveProcessAddressSpace(
    _Inout_ PEPROCESS Process)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&MiArm64SystemPageDirectoryLock, &OldIrql);

    if (Process->MmProcessLinks.Flink != NULL)
    {
        if (Process->MmProcessLinks.Blink == NULL)
        {
            KeBugCheckEx(MEMORY_MANAGEMENT,
                         (ULONG_PTR)Process,
                         (ULONG_PTR)Process->MmProcessLinks.Flink,
                         0,
                         KPROCESS_DTB0(&Process->Pcb));
        }

        RemoveEntryList(&Process->MmProcessLinks);
        Process->MmProcessLinks.Flink = NULL;
        Process->MmProcessLinks.Blink = NULL;
    }

    KeReleaseSpinLock(&MiArm64SystemPageDirectoryLock, OldIrql);
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

    /*
     * Steady-state fast path: kernel PDEs are create-only, so an unlocked
     * scan that finds every PDE in range already backed is monotonic-safe.
     * This keeps the global lock (which serializes every paged-pool fault
     * machine-wide) off the common already-backed path.
     */
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

    if (Backed)
    {
        return TRUE;
    }

    Backed = TRUE;

    KeAcquireSpinLock(&MiArm64SystemPageDirectoryLock, &OldIrql);
    MiArm64FillSystemPageDirectoryLocked(BaseVa, NumberOfBytes);

    PointerPde = MiAddressToPde(BaseVa);
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
    {
        PFN_NUMBER RootPage = MiArm64ReadTtbr1RootPage();

        for (Va = (ULONG_PTR)BaseVa & ~((1ULL << PPI_SHIFT) - 1);
             Va <= EndVa;
             Va += (1ULL << PPI_SHIFT))
        {
            /* Stop at the PPE level: the L2 page-directory page is ensured,
             * the leaf L2 PDEs are left clear for session setup to fill. */
            if (MiArm64EnsureKernelHierarchy(RootPage, (PVOID)Va, TRUE, &FlushHierarchy) == 0)
            {
                Backed = FALSE;
                break;
            }
        }
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
