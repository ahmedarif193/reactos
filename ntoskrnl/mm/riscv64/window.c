/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Sv39 page-table window mirror maintenance (ABI-125)
 *
 * The hardware hierarchy R -> T1[i] -> T0[i][j] is authoritative. The window
 * adds M1 (at R[W]), M0W (at M1[W]) and one M0[i] per T1[i]:
 *   M0[i][j] = leaf(PPN(T1[i][j]))  maps PTE_BASE + (i << 21) + (j << 12)
 *   M0W[i]   = leaf(PPN(R[i]))      maps PDE_BASE + (i << 12)
 *   M0W[W]   = leaf(R)              maps PPE_BASE
 * Every routine here reaches table pages through the direct map only.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>

BOOLEAN MiRiscvWindowReady;

#define MI_RISCV_BUGCHECK_WINDOW 0x5256574EUL /* 'RVWN' */
#define MI_RISCV_KERNEL_INDEX (PPE_PER_PAGE / 2)

static
BOOLEAN
MiRiscvIsKernelIndex(_In_ ULONG Index)
{
    return Index >= MI_RISCV_KERNEL_INDEX;
}

/* A zeroed page for a table or mirror page, from the loader free descriptor
 * before the PFN database exists and from the zeroed list afterwards. */
PFN_NUMBER
NTAPI
MiRiscvAllocateTablePage(VOID)
{
    PFN_NUMBER Pfn;

    if (!MiPfnsInitialized)
    {
        Pfn = MxGetNextPage(1);
        RtlZeroMemory(MiRiscvPfnToDirectMap(Pfn), PAGE_SIZE);
        return Pfn;
    }

    /* Callers at DISPATCH_LEVEL already own the PFN lock (ARM3 convention). */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        KIRQL OldIrql = MiAcquirePfnLock();
        MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
        Pfn = MiRemoveZeroPage(MI_GET_NEXT_COLOR());
        MiReleasePfnLock(OldIrql);
    }
    else
    {
        MI_ASSERT_PFN_LOCK_HELD();
        MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
        Pfn = MiRemoveZeroPage(MI_GET_NEXT_COLOR());
    }
    if (Pfn == 0)
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 1, 0, 0);
    return Pfn;
}

/* Record a mirror page in the PFN database once it exists. PteAddress is
 * the direct-map address of the real entry that references the page. */
static
VOID
MiRiscvAccountMirrorPage(
    _In_ PFN_NUMBER Pfn,
    _In_ PMMPTE PteAddress,
    _In_ PFN_NUMBER PteFrame)
{
    if (!MiPfnsInitialized)
        return;
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        KIRQL OldIrql = MiAcquirePfnLock();
        MiInitializePfnForOtherProcess(Pfn, PteAddress, PteFrame);
        MiReleasePfnLock(OldIrql);
    }
    else
    {
        MiInitializePfnForOtherProcess(Pfn, PteAddress, PteFrame);
    }
}

static
VOID
MiRiscvReleaseMirrorPage(
    _In_ PFN_NUMBER Pfn)
{
    PMMPFN Pfn1;
    PFN_NUMBER Parent;
    KIRQL OldIrql = DISPATCH_LEVEL;
    BOOLEAN AcquireLock;

    if (!MiPfnsInitialized)
        return;
    AcquireLock = (KeGetCurrentIrql() < DISPATCH_LEVEL);
    if (AcquireLock)
        OldIrql = MiAcquirePfnLock();
    MI_ASSERT_PFN_LOCK_HELD();
    Pfn1 = MiGetPfnEntry(Pfn);
    Parent = Pfn1->u4.PteFrame;
    ASSERT(Pfn1->u2.ShareCount == 1);
    ASSERT(Pfn1->u3.e2.ReferenceCount == 1);
    /* Mirror leaves are aliases, not references to their target PFNs.
     * Balance the one parent reference established when M0 was allocated. */
    MI_SET_PFN_DELETED(Pfn1);
    MiDecrementShareCount(MiGetPfnEntry(Parent), Parent);
    MiDecrementShareCount(Pfn1, Pfn);
    if (AcquireLock)
        MiReleasePfnLock(OldIrql);
}

/* Fill M0 from T1: every table pointer becomes a leaf, everything else 0. */
static
VOID
MiRiscvMirrorLevel1Table(
    _In_ PMMPTE Mirror,
    _In_ PMMPTE Table,
    _In_ BOOLEAN Global)
{
    ULONG Index;

    for (Index = 0; Index < PDE_PER_PAGE; ++Index)
    {
        MMPTE Entry;
        Entry.u.Long = __atomic_load_n(&Table[Index].u.Long, __ATOMIC_ACQUIRE);
        Mirror[Index].u.Long = MiRiscvIsTablePointer(Entry) ? MiRiscvMirrorLeaf(Entry.u.Hard.PageFrameNumber, Global) : 0;
    }
}

/* Make M1[i]/M0W[i] agree with R[i]. Root, M1 and M0W are direct-map pointers. */
static
VOID
MiRiscvSyncRootSlot(
    _In_ PMMPTE Root,
    _In_ PFN_NUMBER RootPfn,
    _In_ PMMPTE M1,
    _In_ PFN_NUMBER M1Pfn,
    _In_ PMMPTE M0W,
    _In_ ULONG Index)
{
    MMPTE Entry, MirrorEntry;
    BOOLEAN Global = MiRiscvIsKernelIndex(Index);

    ASSERT(Index != MI_RISCV_WINDOW_INDEX);
    Entry.u.Long = __atomic_load_n(&Root[Index].u.Long, __ATOMIC_ACQUIRE);
    MirrorEntry.u.Long = M1[Index].u.Long;

    if (MiRiscvIsTablePointer(Entry))
    {
        PFN_NUMBER TablePfn = Entry.u.Hard.PageFrameNumber;
        PMMPTE Mirror;

        if (!MiRiscvIsTablePointer(MirrorEntry))
        {
            PFN_NUMBER MirrorPfn = MiRiscvAllocateTablePage();
            Mirror = MiRiscvTablePage(MirrorPfn);
            MiRiscvMirrorLevel1Table(Mirror, MiRiscvTablePage(TablePfn), Global);
            __atomic_store_n(&M1[Index].u.Long, MiRiscvTablePointer(MirrorPfn), __ATOMIC_RELEASE);
            MiRiscvAccountMirrorPage(MirrorPfn, &M1[Index], M1Pfn);
        }
        else
        {
            Mirror = MiRiscvTablePage(MirrorEntry.u.Hard.PageFrameNumber);
            MiRiscvMirrorLevel1Table(Mirror, MiRiscvTablePage(TablePfn), Global);
        }
        __atomic_store_n(&M0W[Index].u.Long, MiRiscvMirrorLeaf(TablePfn, Global), __ATOMIC_RELEASE);
    }
    else
    {
        __atomic_store_n(&M0W[Index].u.Long, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&M1[Index].u.Long, 0, __ATOMIC_RELEASE);
        if (MiRiscvIsTablePointer(MirrorEntry))
        {
            /* Order: the mapping is gone before the page can be reused. */
            __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
            MiRiscvReleaseMirrorPage(MirrorEntry.u.Hard.PageFrameNumber);
        }
    }
    UNREFERENCED_PARAMETER(RootPfn);
}

/* Called after every write through the PDE/PPE window (miarm.h hooks). */
VOID
NTAPI
MiRiscvSyncWindowEntrySlow(
    _In_ PMMPTE Entry)
{
    ULONG_PTR Address = (ULONG_PTR)Entry;
    PFN_NUMBER RootPfn = MiRiscvCurrentRootPfn();
    PMMPTE Root = MiRiscvTablePage(RootPfn);
    MMPTE RootEntry, M1Entry;
    PMMPTE M1, M0W;
    ULONG Index, Slot;

    if (!MiRiscvWindowReady)
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 2, Address, 0);

    RootEntry.u.Long = Root[MI_RISCV_WINDOW_INDEX].u.Long;
    if (!MiRiscvIsTablePointer(RootEntry))
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 3, Address, RootEntry.u.Long);
    M1 = MiRiscvTablePage(RootEntry.u.Hard.PageFrameNumber);
    M1Entry.u.Long = M1[MI_RISCV_WINDOW_INDEX].u.Long;
    if (!MiRiscvIsTablePointer(M1Entry))
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 4, Address, M1Entry.u.Long);
    M0W = MiRiscvTablePage(M1Entry.u.Hard.PageFrameNumber);

    if ((Address >= PPE_BASE) && (Address <= PPE_TOP))
    {
        /* A root entry changed. */
        Index = (ULONG)((Address - PPE_BASE) / sizeof(MMPTE));
        if (Index == MI_RISCV_WINDOW_INDEX)
            KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 5, Address, Root[Index].u.Long);
        MiRiscvSyncRootSlot(Root, RootPfn, M1, RootEntry.u.Hard.PageFrameNumber, M0W, Index);
        __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
        return;
    }

    /* A level-1 entry changed: T1[Index][Slot]. */
    Index = (ULONG)((Address - PDE_BASE) >> PAGE_SHIFT);
    Slot = (ULONG)(((Address - PDE_BASE) & (PAGE_SIZE - 1)) / sizeof(MMPTE));
    M1Entry.u.Long = M1[Index].u.Long;
    if (!MiRiscvIsTablePointer(M1Entry))
    {
        /* The write itself went through M0W[Index], so T1 exists; repair. */
        MiRiscvSyncRootSlot(Root, RootPfn, M1, RootEntry.u.Hard.PageFrameNumber, M0W, Index);
        __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
        return;
    }
    {
        PMMPTE Mirror = MiRiscvTablePage(M1Entry.u.Hard.PageFrameNumber);
        MMPTE Value;
        PVOID WindowVa = (PVOID)(PTE_BASE + ((ULONG64)Index << PDI_SHIFT) + ((ULONG64)Slot << PTI_SHIFT));

        Value.u.Long = __atomic_load_n(&Entry->u.Long, __ATOMIC_ACQUIRE);
        __atomic_store_n(&Mirror[Slot].u.Long,
                         MiRiscvIsTablePointer(Value) ? MiRiscvMirrorLeaf(Value.u.Hard.PageFrameNumber, MiRiscvIsKernelIndex(Index)) : 0,
                         __ATOMIC_RELEASE);
        MiRiscvFlushWindowVa(WindowVa);
    }
}

/* Build the mirror for the loader hierarchy in the current root. Only the
 * supervisor half is mirrored; the user-half identity map is discarded. */
VOID
NTAPI
MiRiscvBuildBootWindow(VOID)
{
    PFN_NUMBER RootPfn = MiRiscvCurrentRootPfn();
    PMMPTE Root = MiRiscvTablePage(RootPfn);
    PFN_NUMBER M1Pfn, M0WPfn;
    PMMPTE M1, M0W;
    ULONG Index;

    ASSERT(!MiRiscvWindowReady);
    ASSERT(!MiPfnsInitialized);
    if (Root[MI_RISCV_WINDOW_INDEX].u.Long != 0)
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 6, RootPfn, Root[MI_RISCV_WINDOW_INDEX].u.Long);

    M1Pfn = MiRiscvAllocateTablePage();
    M0WPfn = MiRiscvAllocateTablePage();
    M1 = MiRiscvTablePage(M1Pfn);
    M0W = MiRiscvTablePage(M0WPfn);
    M1[MI_RISCV_WINDOW_INDEX].u.Long = MiRiscvTablePointer(M0WPfn);
    M0W[MI_RISCV_WINDOW_INDEX].u.Long = MiRiscvMirrorLeaf(RootPfn, TRUE);

    for (Index = MI_RISCV_KERNEL_INDEX; Index < PPE_PER_PAGE; ++Index)
    {
        MMPTE Entry;

        if (Index == MI_RISCV_WINDOW_INDEX)
            continue;
        Entry.u.Long = Root[Index].u.Long;
        if (Entry.u.Hard.Valid && !MiRiscvIsTablePointer(Entry))
            KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 7, Index, Entry.u.Long);
        if (MiRiscvIsTablePointer(Entry))
        {
            PFN_NUMBER MirrorPfn = MiRiscvAllocateTablePage();
            PMMPTE Table = MiRiscvTablePage(Entry.u.Hard.PageFrameNumber);
            ULONG Slot;

            for (Slot = 0; Slot < PDE_PER_PAGE; ++Slot)
            {
                if (Table[Slot].u.Hard.Valid && !MiRiscvIsTablePointer(Table[Slot]))
                    KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_WINDOW, 8, Index, Slot);
            }
            MiRiscvMirrorLevel1Table(MiRiscvTablePage(MirrorPfn), Table, TRUE);
            M1[Index].u.Long = MiRiscvTablePointer(MirrorPfn);
            M0W[Index].u.Long = MiRiscvMirrorLeaf(Entry.u.Hard.PageFrameNumber, TRUE);
        }
    }

    __atomic_store_n(&Root[MI_RISCV_WINDOW_INDEX].u.Long, MiRiscvTablePointer(M1Pfn), __ATOMIC_RELEASE);
    __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
    MiRiscvWindowReady = TRUE;
}

/* Compare the mirror with the real hierarchy of a root. Diagnostic only. */
BOOLEAN
NTAPI
MiRiscvWindowConsistent(
    _In_ PFN_NUMBER RootPfn)
{
    PMMPTE Root = MiRiscvTablePage(RootPfn);
    MMPTE RootEntry, M1Entry;
    PMMPTE M1, M0W;
    ULONG Index, Slot;
    BOOLEAN Consistent = TRUE;

    RootEntry.u.Long = Root[MI_RISCV_WINDOW_INDEX].u.Long;
    if (!MiRiscvIsTablePointer(RootEntry))
    {
        DPRINT1("Window: root %lx has no window slot (%llx)\n", RootPfn, RootEntry.u.Long);
        return FALSE;
    }
    M1 = MiRiscvTablePage(RootEntry.u.Hard.PageFrameNumber);
    M1Entry.u.Long = M1[MI_RISCV_WINDOW_INDEX].u.Long;
    if (!MiRiscvIsTablePointer(M1Entry))
    {
        DPRINT1("Window: M1 has no M0W (%llx)\n", M1Entry.u.Long);
        return FALSE;
    }
    M0W = MiRiscvTablePage(M1Entry.u.Hard.PageFrameNumber);
    if (M0W[MI_RISCV_WINDOW_INDEX].u.Long != MiRiscvMirrorLeaf(RootPfn, TRUE))
    {
        DPRINT1("Window: M0W[W]=%llx, expected root %lx\n", M0W[MI_RISCV_WINDOW_INDEX].u.Long, RootPfn);
        Consistent = FALSE;
    }

    for (Index = 0; Index < PPE_PER_PAGE; ++Index)
    {
        MMPTE Entry;
        BOOLEAN Global = MiRiscvIsKernelIndex(Index);

        if (Index == MI_RISCV_WINDOW_INDEX)
            continue;
        Entry.u.Long = Root[Index].u.Long;
        if (!MiRiscvIsTablePointer(Entry))
        {
            if ((M0W[Index].u.Long != 0) || (M1[Index].u.Long != 0))
            {
                DPRINT1("Window: stale mirror for root slot %lu (%llx/%llx)\n", Index, M1[Index].u.Long, M0W[Index].u.Long);
                Consistent = FALSE;
            }
            continue;
        }
        if (M0W[Index].u.Long != MiRiscvMirrorLeaf(Entry.u.Hard.PageFrameNumber, Global))
        {
            DPRINT1("Window: M0W[%lu]=%llx, root=%llx\n", Index, M0W[Index].u.Long, Entry.u.Long);
            Consistent = FALSE;
        }
        M1Entry.u.Long = M1[Index].u.Long;
        if (!MiRiscvIsTablePointer(M1Entry))
        {
            DPRINT1("Window: no M0 for root slot %lu\n", Index);
            Consistent = FALSE;
            continue;
        }
        {
            PMMPTE Table = MiRiscvTablePage(Entry.u.Hard.PageFrameNumber);
            PMMPTE Mirror = MiRiscvTablePage(M1Entry.u.Hard.PageFrameNumber);

            for (Slot = 0; Slot < PDE_PER_PAGE; ++Slot)
            {
                ULONG64 Expected = MiRiscvIsTablePointer(Table[Slot]) ? MiRiscvMirrorLeaf(Table[Slot].u.Hard.PageFrameNumber, Global) : 0;
                if (Mirror[Slot].u.Long != Expected)
                {
                    DPRINT1("Window: M0[%lu][%lu]=%llx, expected %llx\n", Index, Slot, Mirror[Slot].u.Long, Expected);
                    Consistent = FALSE;
                }
            }
        }
    }
    return Consistent;
}
