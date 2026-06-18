/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         ARM64 process address space initialization
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

#define MI_ARM64_ASID_COUNT 256
#define MI_ARM64_ASID_MASK 0xFF

static KSPIN_LOCK MiArm64AsidLock;
static volatile LONG MiArm64AsidLockState;
static ULONG64 MiArm64AsidBitmap[4] = { 1 };
static ULONG MiArm64NextAsid = 1;

static
VOID
MiArm64EnsureAsidLockReady(VOID)
{
    if (MiArm64AsidLockState == 2)
    {
        return;
    }

    if (InterlockedCompareExchange(&MiArm64AsidLockState, 1, 0) == 0)
    {
        KeInitializeSpinLock(&MiArm64AsidLock);
        InterlockedExchange(&MiArm64AsidLockState, 2);
        return;
    }

    while (MiArm64AsidLockState != 2)
    {
        YieldProcessor();
        KeMemoryBarrier();
    }
}

static
ULONG
MiArm64AllocateAsid(VOID)
{
    KIRQL OldIrql;
    ULONG Candidate;
    ULONG Count;

    MiArm64EnsureAsidLockReady();
    KeAcquireSpinLock(&MiArm64AsidLock, &OldIrql);

    Candidate = MiArm64NextAsid;
    if ((Candidate == 0) || (Candidate >= MI_ARM64_ASID_COUNT))
    {
        Candidate = 1;
    }

    for (Count = 1; Count < MI_ARM64_ASID_COUNT; Count++)
    {
        ULONG Word = Candidate / 64;
        ULONG Bit = Candidate % 64;
        ULONG64 Mask = 1ULL << Bit;

        if ((MiArm64AsidBitmap[Word] & Mask) == 0)
        {
            MiArm64AsidBitmap[Word] |= Mask;
            MiArm64NextAsid = Candidate + 1;
            if (MiArm64NextAsid >= MI_ARM64_ASID_COUNT)
            {
                MiArm64NextAsid = 1;
            }
            KeReleaseSpinLock(&MiArm64AsidLock, OldIrql);
            return Candidate;
        }

        Candidate++;
        if (Candidate >= MI_ARM64_ASID_COUNT)
        {
            Candidate = 1;
        }
    }

    KeReleaseSpinLock(&MiArm64AsidLock, OldIrql);
    return 0;
}

static
VOID
MiArm64ReleaseAsid(
    _In_ ULONG Asid)
{
    KIRQL OldIrql;
    ULONG Word;
    ULONG Bit;
    ULONG64 Operand;

    if ((Asid == 0) || (Asid >= MI_ARM64_ASID_COUNT))
    {
        return;
    }

    MiArm64EnsureAsidLockReady();
    KeAcquireSpinLock(&MiArm64AsidLock, &OldIrql);
    Word = Asid / 64;
    Bit = Asid % 64;

    Operand = (ULONG64)Asid << KI_ARM64_TTBR_ASID_SHIFT;
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi aside1is, %0" :: "r"(Operand) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    MiArm64AsidBitmap[Word] &= ~(1ULL << Bit);
    KeReleaseSpinLock(&MiArm64AsidLock, OldIrql);
}

VOID
MiArm64AssignProcessAsid(
    _Inout_ PULONG_PTR DirectoryTableBase)
{
    ULONG Asid;

    if ((*DirectoryTableBase & KI_ARM64_TTBR_ASID_MASK) != 0)
    {
        return;
    }

    Asid = MiArm64AllocateAsid();
    if (Asid != 0)
    {
        *DirectoryTableBase = (*DirectoryTableBase & ARM64_PTE_ADDR_MASK) |
                              ((ULONG_PTR)Asid << KI_ARM64_TTBR_ASID_SHIFT);
    }
}

VOID
MiArm64ReleaseProcessAsid(
    _Inout_ PEPROCESS Process)
{
    ULONG Asid;

    Asid = (ULONG)((Process->Pcb.DirectoryTableBase[0] >> KI_ARM64_TTBR_ASID_SHIFT) & MI_ARM64_ASID_MASK);
    MiArm64ReleaseAsid(Asid);
}

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
    ULONG PageColor;
    KIRQL OldIrql;

    ASSERT(DirectoryTableBase != NULL);

    RootPfn = (DirectoryTableBase[0] & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
    HyperPfn = DirectoryTableBase[1] >> PAGE_SHIFT;

    /* Allocate backing pages for the hyperspace PD/PT */
    OldIrql = MiAcquirePfnLock();

    MI_SET_USAGE(MI_USAGE_PAGE_DIRECTORY);
    PageColor = MI_GET_NEXT_PROCESS_COLOR(Process);
    HyperPdPfn = MiRemoveZeroPageSafe(PageColor);
    if (!HyperPdPfn)
    {
        HyperPdPfn = MiRemoveAnyPage(PageColor);
        MiReleasePfnLock(OldIrql);
        MiZeroPhysicalPage(HyperPdPfn);
        OldIrql = MiAcquirePfnLock();
    }

    MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    PageColor = MI_GET_NEXT_PROCESS_COLOR(Process);
    HyperPtPfn = MiRemoveZeroPageSafe(PageColor);
    if (!HyperPtPfn)
    {
        HyperPtPfn = MiRemoveAnyPage(PageColor);
        MiReleasePfnLock(OldIrql);
        MiZeroPhysicalPage(HyperPtPfn);
    }
    else
    {
        MiReleasePfnLock(OldIrql);
    }

    /*
     * The new root is copied from the current kernel half below. Ensure the
     * freshly allocated process page-table pages are reachable through KSEG0
     * before taking that copy, otherwise a later TTBR switch can leave the
     * debugger and ARM64 table walkers unable to inspect the active root.
     */
    MiArm64MapKseg0Page(RootPfn);
    MiArm64MapKseg0Page(HyperPfn);
    MiArm64MapKseg0Page(HyperPdPfn);
    MiArm64MapKseg0Page(HyperPtPfn);
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

    /* Map hyperspace PDPT */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte,
                                MappingPte,
                                MM_READWRITE,
                                HyperPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);
    *MappingPte = MapPte;
    KeInvalidateTlbEntry(PageTable);

    /* L1 table entry - use table descriptor template */
    PageTable = MiPteToAddress(MappingPte);
    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = HyperPdPfn;
    PageTable[0] = TempPte;

    MiArm64CleanPageToPoC((PVOID)PageTable);

    /* Map hyperspace PD */
    MapPte.u.Hard.PageFrameNumber = HyperPdPfn;
    *MappingPte = MapPte;
    KeInvalidateTlbEntry(PageTable);

    /* L2 table entry - use table descriptor template */
    PageTable = MiPteToAddress(MappingPte);
    TempPte.u.Long = ValidKernelPde.u.Long;
    TempPte.u.Hard.PageFrameNumber = HyperPtPfn;
    PageTable[0] = TempPte;

    MiArm64CleanPageToPoC((PVOID)PageTable);

    /* Map hyperspace PT */
    MapPte.u.Hard.PageFrameNumber = HyperPtPfn;
    *MappingPte = MapPte;
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
