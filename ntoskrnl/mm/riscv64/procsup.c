/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V process address-space lifetime (ABI-125/ABI-050)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

#define MI_RISCV_BUGCHECK_PROCESS 0x52565053UL /* 'RVPS' */
#define MI_RISCV_KERNEL_INDEX (PPE_PER_PAGE / 2)

/* Grab a zeroed page for a table, releasing the PFN lock if zeroing is needed. */
static
PFN_NUMBER
MiRiscvGetProcessTablePage(
    _In_ PEPROCESS Process,
    _Inout_ PKIRQL OldIrql)
{
    ULONG Color = MI_GET_NEXT_PROCESS_COLOR(Process);
    PFN_NUMBER Pfn;

    MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    Pfn = MiRemoveZeroPageSafe(Color);
    if (Pfn == 0)
    {
        Pfn = MiRemoveAnyPage(Color);
        MiReleasePfnLock(*OldIrql);
        MiZeroPhysicalPage(Pfn);
        *OldIrql = MiAcquirePfnLock();
    }
    return Pfn;
}

/*
 * Build a new root: kernel half copied from the current root, private window
 * (M1', M0W'), private hyperspace level-1 table with its level-0 table and
 * mirror page, and the working-set list page mapped in hyperspace.
 */
BOOLEAN
MiArchCreateProcessAddressSpace(
    _In_ PEPROCESS Process,
    _In_ PULONG_PTR DirectoryTableBase)
{
    KIRQL OldIrql;
    PFN_NUMBER RootPfn, HyperPfn, HyperPtPfn, M1Pfn, M0WPfn, M0HyperPfn;
    PFN_NUMBER CurrentRootPfn;
    PMMPTE Root, M1, M0W, M0Hyper, HyperTable, HyperPt;
    PMMPTE CurrentRoot, CurrentM1, CurrentM0W;
    MMPTE TempPte;
    ULONG Index;

    RootPfn = DirectoryTableBase[0] >> PAGE_SHIFT;
    HyperPfn = DirectoryTableBase[1] >> PAGE_SHIFT;

    OldIrql = MiAcquirePfnLock();
    HyperPtPfn = MiRiscvGetProcessTablePage(Process, &OldIrql);
    M1Pfn = MiRiscvGetProcessTablePage(Process, &OldIrql);
    M0WPfn = MiRiscvGetProcessTablePage(Process, &OldIrql);
    M0HyperPfn = MiRiscvGetProcessTablePage(Process, &OldIrql);
    MiReleasePfnLock(OldIrql);

    Root = MiRiscvTablePage(RootPfn);
    M1 = MiRiscvTablePage(M1Pfn);
    M0W = MiRiscvTablePage(M0WPfn);
    M0Hyper = MiRiscvTablePage(M0HyperPfn);
    HyperTable = MiRiscvTablePage(HyperPfn);
    HyperPt = MiRiscvTablePage(HyperPtPfn);

    /* Kernel half: the real root entries, and the shared mirror pages. */
    CurrentRootPfn = MiRiscvCurrentRootPfn();
    CurrentRoot = MiRiscvTablePage(CurrentRootPfn);
    CurrentM1 = MiRiscvTablePage(CurrentRoot[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber);
    CurrentM0W = MiRiscvTablePage(CurrentM1[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber);
    for (Index = MI_RISCV_KERNEL_INDEX; Index < PPE_PER_PAGE; ++Index)
    {
        if ((Index == MI_RISCV_WINDOW_INDEX) || (Index == MI_RISCV_HYPERSPACE_INDEX))
            continue;
        Root[Index].u.Long = CurrentRoot[Index].u.Long;
        M1[Index].u.Long = CurrentM1[Index].u.Long;
        M0W[Index].u.Long = CurrentM0W[Index].u.Long;
    }

    /* Private window slot. */
    Root[MI_RISCV_WINDOW_INDEX].u.Long = MiRiscvTablePointer(M1Pfn);
    M1[MI_RISCV_WINDOW_INDEX].u.Long = MiRiscvTablePointer(M0WPfn);
    M0W[MI_RISCV_WINDOW_INDEX].u.Long = MiRiscvMirrorLeaf(RootPfn, TRUE);

    /* Private hyperspace: R[H] -> HyperTable, HyperTable[0] -> HyperPt. */
    Root[MI_RISCV_HYPERSPACE_INDEX].u.Long = MiRiscvTablePointer(HyperPfn);
    HyperTable[0].u.Long = MiRiscvTablePointer(HyperPtPfn);
    M1[MI_RISCV_HYPERSPACE_INDEX].u.Long = MiRiscvTablePointer(M0HyperPfn);
    M0W[MI_RISCV_HYPERSPACE_INDEX].u.Long = MiRiscvMirrorLeaf(HyperPfn, TRUE);
    M0Hyper[0].u.Long = MiRiscvMirrorLeaf(HyperPtPfn, TRUE);

    /* Working-set list page inside hyperspace. */
    TempPte = ValidKernelPteLocal;
    TempPte.u.Hard.PageFrameNumber = Process->WorkingSetPage;
    HyperPt[MiAddressToPti(MmWorkingSetList)] = TempPte;

    /* The mapping range counter, as the boot process has. */
    HyperPt[MiAddressToPti((PVOID)MI_MAPPING_RANGE_START)].u.Hard.PageFrameNumber = MI_HYPERSPACE_PTES;

    __asm__ __volatile__("fence rw, rw" ::: "memory");
    ASSERT(MiRiscvWindowConsistent(RootPfn));
    return TRUE;
}

/*
 * Called by MmInitializeProcessAddressSpace while attached to the process,
 * with the working-set and PFN locks held: account the root, the window
 * pages, the hyperspace tables and the working-set page, and publish the
 * working-set list address.
 */
NTSTATUS
NTAPI
MiRiscvInitializeProcessPageTables(
    _Inout_ PEPROCESS Process)
{
    PFN_NUMBER RootPfn = MiRiscvCurrentRootPfn();
    PMMPTE Root = MiRiscvTablePage(RootPfn);
    PMMPTE M1;
    PFN_NUMBER M1Pfn, M0WPfn, M0HyperPfn, HyperPfn, HyperPtPfn, WsPfn;
    PMMPFN Pfn;

    MI_ASSERT_PFN_LOCK_HELD();
    if ((Process->Pcb.DirectoryTableBase >> PAGE_SHIFT) != RootPfn)
        return STATUS_INVALID_ADDRESS;
    if (!MiRiscvIsTablePointer(Root[MI_RISCV_WINDOW_INDEX]) ||
        !MiRiscvIsTablePointer(Root[MI_RISCV_HYPERSPACE_INDEX]))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    M1Pfn = Root[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber;
    M1 = MiRiscvTablePage(M1Pfn);
    if (!MiRiscvIsTablePointer(M1[MI_RISCV_WINDOW_INDEX]) ||
        !MiRiscvIsTablePointer(M1[MI_RISCV_HYPERSPACE_INDEX]))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    M0WPfn = M1[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber;
    M0HyperPfn = M1[MI_RISCV_HYPERSPACE_INDEX].u.Hard.PageFrameNumber;
    HyperPfn = Root[MI_RISCV_HYPERSPACE_INDEX].u.Hard.PageFrameNumber;
    if (Process->Pcb.Unused0 != (HyperPfn << PAGE_SHIFT))
        return STATUS_INVALID_ADDRESS;
    if (!MiAddressToPde((PVOID)HYPER_SPACE)->u.Hard.Valid || !MiAddressToPte(MmWorkingSetList)->u.Hard.Valid)
        return STATUS_INVALID_DEVICE_STATE;
    HyperPtPfn = PFN_FROM_PDE(MiAddressToPde((PVOID)HYPER_SPACE));
    WsPfn = PFN_FROM_PTE(MiAddressToPte(MmWorkingSetList));

    /* Root: self-parented, like the x64 PML4. */
    MiInitializePfnForOtherProcess(RootPfn, (PMMPTE)PPE_SELFMAP, 0);
    Pfn = MiGetPfnEntry(RootPfn);
    Pfn->u4.PteFrame = RootPfn;
    Pfn->u2.ShareCount++;
    Pfn->u1.Event = (PKEVENT)Process;

    /* Window pages. */
    MiInitializePfnForOtherProcess(M1Pfn, (PMMPTE)PPE_SELFMAP, RootPfn);
    MiInitializePfnForOtherProcess(M0WPfn, &M1[MI_RISCV_WINDOW_INDEX], M1Pfn);
    MiInitializePfnForOtherProcess(M0HyperPfn, &M1[MI_RISCV_HYPERSPACE_INDEX], M1Pfn);

    /* Hyperspace tables and the working-set page: real window entries,
     * explicit parents (MiInitializePfn would derive M1 as the root's page). */
    MiInitializePfnForOtherProcess(HyperPfn, MiAddressToPpe((PVOID)HYPER_SPACE), RootPfn);
    MiInitializePfnForOtherProcess(HyperPtPfn, MiAddressToPde((PVOID)HYPER_SPACE), HyperPfn);
    MiInitializePfnForOtherProcess(WsPfn, MiAddressToPte(MmWorkingSetList), HyperPtPfn);

    Process->Vm.VmWorkingSetList = MmWorkingSetList;
    ASSERT(((ULONG_PTR)MmWorkingSetList >= MI_MAPPING_RANGE_END) && ((ULONG_PTR)MmWorkingSetList <= HYPER_SPACE_END));
    return STATUS_SUCCESS;
}

/* The caller has removed the mapping and invalidated its translations.
 * Phase 1 owns allocated pages; phase 2 additionally owns PFN references. */
static
VOID
MiRiscvDeleteProcessPage(
    _In_ PFN_NUMBER Page,
    _In_ PFN_NUMBER Parent,
    _In_ BOOLEAN Accounted)
{
    PMMPFN Pfn = MiGetPfnEntry(Page);

    MI_ASSERT_PFN_LOCK_HELD();
    if (!Accounted)
    {
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
        MiInsertPageInFreeList(Page);
        return;
    }

    if ((Pfn->u2.ShareCount != 1) || (Pfn->u3.e2.ReferenceCount != 1) ||
        (Pfn->u4.PteFrame != Parent) || Pfn->u3.e1.PrototypePte)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_PROCESS, Page, Pfn->u2.ShareCount, Parent);
    }
    MI_SET_PFN_DELETED(Pfn);
    if (Parent)
        MiDecrementShareCount(MiGetPfnEntry(Parent), Parent);
    MiDecrementShareCount(Pfn, Page);
    ASSERT(Pfn->u3.e2.ReferenceCount == 0);
}

/* Final reclamation runs detached, after VAD/section cleanup, on one hart.
 * Do not walk the shared supervisor half or free the targets of aliases. */
VOID
NTAPI
MiRiscvDeleteProcessPageTables(
    _Inout_ PEPROCESS Process)
{
    PFN_NUMBER RootPfn, M1Pfn, M0WPfn, HyperPfn, HyperPtPfn, M0HyperPfn;
    PMMPTE Root, M1, M0W, Hyper, HyperPt;
    PMMPFN Pfn;
    BOOLEAN Accounted = (Process->AddressSpaceInitialized == 2);
    ULONG Index;

    MI_ASSERT_PFN_LOCK_HELD();
    RootPfn = Process->Pcb.DirectoryTableBase >> PAGE_SHIFT;
    if (!RootPfn)
        return;
    if ((RootPfn == MiRiscvCurrentRootPfn()) || (KeNumberProcessors != 1) ||
        Process->ActiveThreads || ((Process->AddressSpaceInitialized != 1) && !Accounted))
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_PROCESS, (ULONG_PTR)Process, RootPfn, Process->AddressSpaceInitialized);
    }

    Root = MiRiscvTablePage(RootPfn);
    M1Pfn = Root[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber;
    M1 = MiRiscvTablePage(M1Pfn);
    M0WPfn = M1[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber;
    M0W = MiRiscvTablePage(M0WPfn);
    HyperPfn = Root[MI_RISCV_HYPERSPACE_INDEX].u.Hard.PageFrameNumber;
    Hyper = MiRiscvTablePage(HyperPfn);
    HyperPtPfn = Hyper[0].u.Hard.PageFrameNumber;
    HyperPt = MiRiscvTablePage(HyperPtPfn);
    M0HyperPfn = M1[MI_RISCV_HYPERSPACE_INDEX].u.Hard.PageFrameNumber;

    /* All user table levels are removed by MiDeletePde's cascade. A live
     * entry here is an incomplete address-space cleanup, not a free page. */
    for (Index = 0; Index < MI_RISCV_KERNEL_INDEX; ++Index)
    {
        if (Root[Index].u.Long || M1[Index].u.Long || M0W[Index].u.Long)
            KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_PROCESS, RootPfn, Index, Root[Index].u.Long);
    }

    /* This allocator currently creates one hyperspace PT and one WS page.
     * Mapping slots through MI_MAPPING_RANGE_END are temporary aliases only. */
    for (Index = 1; Index < PDE_PER_PAGE; ++Index)
        ASSERT(Hyper[Index].u.Long == 0);
    for (Index = MiAddressToPti((PVOID)MI_MAPPING_RANGE_END) + 1; Index < PTE_PER_PAGE; ++Index)
    {
        if (Index == MiAddressToPti(MmWorkingSetList))
            ASSERT(HyperPt[Index].u.Hard.PageFrameNumber == Process->WorkingSetPage);
        else
            ASSERT(HyperPt[Index].u.Long == 0);
    }

    /* ASID zero, no active hart uses this root. Flush also the private
     * global hyperspace/window aliases before any PFN becomes reusable. */
    __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");
    RtlZeroMemory(HyperPt, PAGE_SIZE);
    MiRiscvDeleteProcessPage(Process->WorkingSetPage, HyperPtPfn, Accounted);
    Hyper[0].u.Long = 0;
    MiRiscvDeleteProcessPage(HyperPtPfn, HyperPfn, Accounted);
    Root[MI_RISCV_HYPERSPACE_INDEX].u.Long = 0;
    MiRiscvDeleteProcessPage(HyperPfn, RootPfn, Accounted);
    M1[MI_RISCV_HYPERSPACE_INDEX].u.Long = 0;
    MiRiscvDeleteProcessPage(M0HyperPfn, M1Pfn, Accounted);
    M1[MI_RISCV_WINDOW_INDEX].u.Long = 0;
    MiRiscvDeleteProcessPage(M0WPfn, M1Pfn, Accounted);
    Root[MI_RISCV_WINDOW_INDEX].u.Long = 0;
    MiRiscvDeleteProcessPage(M1Pfn, RootPfn, Accounted);

    if (Accounted)
    {
        /* The root retains its allocation and self-parent references. */
        Pfn = MiGetPfnEntry(RootPfn);
        ASSERT(Pfn->u2.ShareCount == 2);
        ASSERT(Pfn->u4.PteFrame == RootPfn);
        MiDecrementShareCount(Pfn, RootPfn);
        /* Drop the allocation separately; the self-parent was just dropped. */
        MI_SET_PFN_DELETED(Pfn);
        MiDecrementShareCount(Pfn, RootPfn);
        ASSERT(Pfn->u3.e2.ReferenceCount == 0);
    }
    else
    {
        MiRiscvDeleteProcessPage(RootPfn, 0, FALSE);
    }
    Process->WorkingSetPage = 0;
    Process->Vm.VmWorkingSetList = NULL;
    Process->AddressSpaceInitialized = 0;
}
