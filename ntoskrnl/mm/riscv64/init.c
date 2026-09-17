/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Memory manager initialization for RISC-V64 (Sv39, ABI-125)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>

extern PMMPTE MmDebugPte;

#define IS_PAGE_ALIGNED(addr) ((((ULONG_PTR)(addr)) & (PAGE_SIZE - 1)) == 0)
#define MI_RISCV_BUGCHECK_INIT 0x5256494EUL /* 'RVIN' */

/* Template PDE for a demand-zero page-table page (shared fault path). */
MMPDE DemandZeroPde = {{MM_EXECUTE_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS}};

PVOID MiSessionViewEnd;
PVOID MiSystemPteSpaceStart;
PVOID MiSystemPteSpaceEnd;
BOOLEAN MiPfnsInitialized;
static PFN_NUMBER MiRiscvBootRootPfn;

/* SESSION LAYOUT *************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
MiInitializeSessionSpaceLayout(VOID)
{
    PMI_SYSTEM_VA_ASSIGNMENT Region = &MiSystemVaRegions[AssignedRegionSession];

    ASSERT(Region->NumberOfBytes >= MI_SESSION_SIZE);
    MmSessionSize = MI_SESSION_SIZE;
    MiSessionSpaceEnd = Add2Ptr(Region->BaseAddress, MmSessionSize);

    MmSessionImageSize = MI_SESSION_IMAGE_SIZE;
    MiSessionImageEnd = MiSessionSpaceEnd;
    MiSessionImageStart = (PUCHAR)MiSessionImageEnd - MmSessionImageSize;
    ASSERT(IS_PAGE_ALIGNED(MiSessionImageStart));

    MiSessionSpaceWs = (PUCHAR)MiSessionImageStart - MI_SESSION_WORKING_SET_SIZE;

    MmSessionViewSize = MI_SESSION_VIEW_SIZE;
    MiSessionViewEnd = MiSessionSpaceWs;
    MiSessionViewStart = (PUCHAR)MiSessionViewEnd - MmSessionViewSize;
    ASSERT(IS_PAGE_ALIGNED(MiSessionViewStart));

    MmSessionPoolSize = MI_SESSION_POOL_SIZE;
    MiSessionPoolEnd = MiSessionViewStart;
    MiSessionPoolStart = (PUCHAR)MiSessionPoolEnd - MmSessionPoolSize;
    ASSERT(IS_PAGE_ALIGNED(MiSessionPoolStart));

    MmSessionBase = MiSessionPoolStart;
    ASSERT(MmSessionBase == Region->BaseAddress);

    /* System view space sits directly below session space. */
    MmSystemViewSize = MI_SYSTEM_VIEW_SIZE;
    MiSystemViewStart = (PUCHAR)MmSessionBase - MmSystemViewSize;
    ASSERT(MiSystemViewStart == MiSystemVaRegions[AssignedRegionSystemView].BaseAddress);

    ASSERT(Add2Ptr(MmSessionBase, MmSessionSize) == MiSessionSpaceEnd);
    ASSERT(MiSessionViewEnd <= MiSessionImageStart);
    ASSERT(MmSessionBase <= MiSessionPoolStart);

    MiSessionImagePteStart = MiAddressToPte(MiSessionImageStart);
    MiSessionImagePteEnd = MiAddressToPte(MiSessionImageEnd);
    MiSessionBasePte = MiAddressToPte(MmSessionBase);
    MiSessionLastPte = MiAddressToPte(MiSessionSpaceEnd);
    MmSessionSpace = (PMM_SESSION_SPACE)Add2Ptr(MiSessionImageStart, 0x10000);
}

/* TABLE MAPPING **************************************************************/

static
VOID
MiMapPPEs(
    _In_ PVOID StartAddress,
    _In_ PVOID EndAddress)
{
    PMMPPE PointerPpe;
    MMPPE TmplPpe = ValidKernelPpe;

    for (PointerPpe = MiAddressToPpe(StartAddress); PointerPpe <= MiAddressToPpe(EndAddress); PointerPpe++)
    {
        if (!PointerPpe->u.Hard.Valid)
        {
            TmplPpe.u.Hard.PageFrameNumber = MiRiscvAllocateTablePage();
            MI_WRITE_VALID_PPE(PointerPpe, TmplPpe);
        }
    }
}

static
VOID
MiMapPDEs(
    _In_ PVOID StartAddress,
    _In_ PVOID EndAddress)
{
    PMMPDE PointerPde;
    MMPDE TmplPde = ValidKernelPde;

    for (PointerPde = MiAddressToPde(StartAddress); PointerPde <= MiAddressToPde(EndAddress); PointerPde++)
    {
        if (!PointerPde->u.Hard.Valid)
        {
            TmplPde.u.Hard.PageFrameNumber = MiRiscvAllocateTablePage();
            MI_WRITE_VALID_PDE(PointerPde, TmplPde);
        }
    }
}

static
VOID
MiMapPTEs(
    _In_ PVOID StartAddress,
    _In_ PVOID EndAddress)
{
    PMMPTE PointerPte;
    MMPTE TmplPte = ValidKernelPte;

    for (PointerPte = MiAddressToPte(StartAddress); PointerPte <= MiAddressToPte(EndAddress); PointerPte++)
    {
        if (!PointerPte->u.Hard.Valid)
        {
            TmplPte.u.Hard.PageFrameNumber = MxGetNextPage(1);
            MI_WRITE_VALID_PTE(PointerPte, TmplPte);
            RtlZeroMemory(MiPteToAddress(PointerPte), PAGE_SIZE);
        }
    }
}

/* Make one upper-level entry valid, allocating and accounting a table page. */
static
BOOLEAN
MiRiscvEnsureTableEntry(
    _Inout_ PMMPTE PointerPte)
{
    KIRQL OldIrql;
    PFN_NUMBER PageFrameNumber;
    MMPTE TempPte;

    if (PointerPte->u.Hard.Valid != 0)
        return TRUE;
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        return FALSE;

    if (!MiPfnsInitialized)
    {
        TempPte.u.Long = MiRiscvTablePointer(MiRiscvAllocateTablePage());
        MI_WRITE_VALID_PTE(PointerPte, TempPte);
        return TRUE;
    }

    OldIrql = MiAcquirePfnLock();
    if (PointerPte->u.Hard.Valid != 0)
    {
        MiReleasePfnLock(OldIrql);
        return TRUE;
    }
    if (PointerPte->u.Long != 0)
    {
        ASSERT(FALSE);
        MiReleasePfnLock(OldIrql);
        return FALSE;
    }

    MI_SET_USAGE(MI_USAGE_PAGE_TABLE);
    PageFrameNumber = MiRemoveZeroPage(MI_GET_NEXT_COLOR());
    if (PageFrameNumber == 0)
    {
        MiReleasePfnLock(OldIrql);
        return FALSE;
    }

    MiInitializePfn(PageFrameNumber, PointerPte, TRUE);
    KeGetCurrentPrcb()->MmDemandZeroCount++;
    TempPte.u.Long = MiRiscvTablePointer(PageFrameNumber);
    MI_WRITE_VALID_PTE(PointerPte, TempPte);
    MiReleasePfnLock(OldIrql);
    return TRUE;
}

static
BOOLEAN
MiRiscvEnsureKernelPageTablesValid(
    _In_ PVOID Address)
{
    if (!MiRiscvEnsureTableEntry(MiAddressToPpe(Address)))
        return FALSE;
    return MiRiscvEnsureTableEntry(MiAddressToPde(Address));
}

static
BOOLEAN
MiRiscvEnsureKernelPdeRangeBacked(
    _In_ PVOID BaseVa,
    _In_ SIZE_T NumberOfBytes)
{
    PVOID EndVa;
    PMMPDE PointerPde, LastPde;

    if (NumberOfBytes == 0)
        return TRUE;
    EndVa = Add2Ptr(BaseVa, NumberOfBytes - 1);
    PointerPde = MiAddressToPde(BaseVa);
    LastPde = MiAddressToPde(EndVa);
    while (PointerPde <= LastPde)
    {
        if (!MiRiscvEnsureKernelPageTablesValid(MiPdeToAddress(PointerPde)))
            return FALSE;
        ASSERT(PointerPde->u.Hard.Valid == 1);
        PointerPde++;
    }
    return TRUE;
}

BOOLEAN
NTAPI
MiEnsureSystemPtesBacked(
    _In_ PMMPTE StartingPte,
    _In_ ULONG NumberOfPtes)
{
    ASSERT(NumberOfPtes != 0);
    return MiRiscvEnsureKernelPdeRangeBacked(MiPteToAddress(StartingPte), (SIZE_T)NumberOfPtes << PAGE_SHIFT);
}

BOOLEAN
NTAPI
MiEnsureNonPagedPoolExpansionPtesBacked(
    _In_ PMMPTE StartingPte,
    _In_ ULONG NumberOfPtes)
{
    ASSERT(NumberOfPtes != 0);
    return MiRiscvEnsureKernelPdeRangeBacked(MiPteToAddress(StartingPte - 1), ((SIZE_T)NumberOfPtes + 2) << PAGE_SHIFT);
}

BOOLEAN
NTAPI
MiEnsureSessionPageTablesBacked(
    _In_ PVOID BaseVa,
    _In_ SIZE_T NumberOfBytes)
{
    PVOID EndVa;
    PMMPPE PointerPpe, LastPpe;

    if (NumberOfBytes == 0)
        return TRUE;
    /* Leave the session PDEs clear; MiSessionCreateInternal fills them. */
    EndVa = Add2Ptr(BaseVa, NumberOfBytes - 1);
    PointerPpe = MiAddressToPpe(BaseVa);
    LastPpe = MiAddressToPpe(EndVa);
    while (PointerPpe <= LastPpe)
    {
        if (!MiRiscvEnsureTableEntry(PointerPpe))
            return FALSE;
        PointerPpe++;
    }
    return TRUE;
}

/* BOOT PAGE TABLES ***********************************************************/

CODE_SEG("INIT")
static
VOID
MiInitializePageTable(VOID)
{
    PFN_NUMBER RootPfn = MiRiscvCurrentRootPfn();
    PMMPTE Root = MiRiscvTablePage(RootPfn);
    PEPROCESS Process = PsGetCurrentProcess();
    ULONG Index;
    MMPTE TmplPte;

    if ((Process->Pcb.DirectoryTableBase >> PAGE_SHIFT) != RootPfn)
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_INIT, 1, RootPfn, Process->Pcb.DirectoryTableBase);
    MiRiscvBootRootPfn = RootPfn;

    /* The loader identity map lives in the user half; drop it before the
     * window exists so no mirror is built for it. */
    for (Index = 0; Index < PPE_PER_PAGE / 2; ++Index)
        Root[Index].u.Long = 0;
    KeFlushCurrentTb();

    MiRiscvBuildBootWindow();
    ASSERT(MiRiscvWindowConsistent(RootPfn));

    /* Hyperspace PTEs are per process: never global. */
    TmplPte = ValidKernelPteLocal;
    HyperTemplatePte = TmplPte;

    /* Create the level-1 tables for every kernel region now, so that every
     * process root copied later already holds the complete kernel half. */
    for (Index = 0; Index < AssignedRegionMaximum; ++Index)
    {
        PMI_SYSTEM_VA_ASSIGNMENT Region = &MiSystemVaRegions[Index];

        if ((Region->BaseAddress == NULL) ||
            (Index == AssignedRegionKernelStacks) ||
            (Index == AssignedRegionHyperSpace) ||
            (Index == AssignedRegionPageTables) ||
            (Index == AssignedRegionPhysicalMap))
        {
            continue;
        }
        MiMapPPEs(Region->BaseAddress, Add2Ptr(Region->BaseAddress, Region->NumberOfBytes - 1));
    }

    /* The boot process gets its private hyperspace level-1 table. */
    MiMapPPEs((PVOID)HYPER_SPACE, (PVOID)HYPER_SPACE_END);
    Process->Pcb.Unused0 = (ULONG_PTR)PFN_FROM_PPE(MiAddressToPpe((PVOID)HYPER_SPACE)) << PAGE_SHIFT;

    MiMapPDEs((PVOID)MI_MAPPING_RANGE_START, (PVOID)MI_MAPPING_RANGE_END);
    MmFirstReservedMappingPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_START);
    MmLastReservedMappingPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_END);
    MmFirstReservedMappingPte->u.Hard.PageFrameNumber = MI_HYPERSPACE_PTES;

    MiMapPDEs((PVOID)MI_VAD_BITMAP, (PVOID)(MI_WORKING_SET_LIST + PAGE_SIZE - 1));
    MiMapPTEs((PVOID)MI_VAD_BITMAP, (PVOID)(MI_WORKING_SET_LIST + PAGE_SIZE - 1));
}

/* POOLS AND SYSTEM PTES ******************************************************/

CODE_SEG("INIT")
static
VOID
MiBuildNonPagedPool(VOID)
{
    ULONG64 SizeOfSystemRamInBytes = (ULONG64)MmNumberOfPhysicalPages * PAGE_SIZE;
    PVOID InitialNonPagedPoolEnd;

    if (MmMaximumNonPagedPoolPercent != 0)
    {
        if (MmMaximumNonPagedPoolPercent > 90)
            MmMaximumNonPagedPoolPercent = 90;
    }
    else
    {
        MmMaximumNonPagedPoolPercent = 75;
    }

    MmMaximumNonPagedPoolInBytes = (SizeOfSystemRamInBytes * MmMaximumNonPagedPoolPercent) / 100;
    MmMaximumNonPagedPoolInBytes = ROUND_TO_PAGES(MmMaximumNonPagedPoolInBytes);
    if (MmMaximumNonPagedPoolInBytes > MiSystemVaRegions[AssignedRegionNonPagedPool].NumberOfBytes)
        MmMaximumNonPagedPoolInBytes = MiSystemVaRegions[AssignedRegionNonPagedPool].NumberOfBytes;

    if (MmSizeOfNonPagedPoolInBytes == 0)
        MmSizeOfNonPagedPoolInBytes = (SizeOfSystemRamInBytes * 3 / 100);
    if (MmSizeOfNonPagedPoolInBytes < 40 * _1MB)
    {
        if (SizeOfSystemRamInBytes >= (400 * _1MB))
            MmSizeOfNonPagedPoolInBytes = 40 * _1MB;
        else
            MmSizeOfNonPagedPoolInBytes = SizeOfSystemRamInBytes / 10;
    }
    MmSizeOfNonPagedPoolInBytes = ROUND_TO_PAGES(MmSizeOfNonPagedPoolInBytes);
    if (MmSizeOfNonPagedPoolInBytes > MmMaximumNonPagedPoolInBytes)
        MmSizeOfNonPagedPoolInBytes = MmMaximumNonPagedPoolInBytes;

    MmMaximumNonPagedPoolInPages = BYTES_TO_PAGES(MmMaximumNonPagedPoolInBytes);
    MmNonPagedPoolStart = MiSystemVaRegions[AssignedRegionNonPagedPool].BaseAddress;
    MmNonPagedPoolExpansionStart = Add2Ptr(MmNonPagedPoolStart, MmSizeOfNonPagedPoolInBytes);
    ASSERT(IS_PAGE_ALIGNED(MmNonPagedPoolExpansionStart));
    MmNonPagedPoolEnd = Add2Ptr(MmNonPagedPoolStart, MmMaximumNonPagedPoolInBytes);
    InitialNonPagedPoolEnd = Add2Ptr(MmNonPagedPoolExpansionStart, -1);

    /* Map the initial nonpaged pool. Expansion is backed on demand. */
    MiMapPPEs(MmNonPagedPoolStart, InitialNonPagedPoolEnd);
    MiMapPDEs(MmNonPagedPoolStart, InitialNonPagedPoolEnd);
    MiMapPTEs(MmNonPagedPoolStart, InitialNonPagedPoolEnd);

    MiSystemPteMetadataSize = MiGetSystemPteMetadataSize(MI_NUMBER_SYSTEM_PTES);
    MiInitializeNonPagedPool();
    MiInitializeNonPagedPoolThresholds();
}

CODE_SEG("INIT")
static
VOID
MiBuildSystemPteSpace(VOID)
{
    PMMPTE PointerPte;
    SIZE_T NonPagedSystemSize;

    MmNumberOfSystemPtes = MI_NUMBER_SYSTEM_PTES;
    NonPagedSystemSize = (MmNumberOfSystemPtes + 1) * PAGE_SIZE;
    MiSystemPteSpaceStart = MiSystemVaRegions[AssignedRegionSystemPtes].BaseAddress;
    MiSystemPteSpaceEnd = (PUCHAR)MiSystemPteSpaceStart + NonPagedSystemSize;
    MmSystemPteSpaceStart = MiSystemPteSpaceStart;
    ASSERT(NonPagedSystemSize <= MiSystemVaRegions[AssignedRegionSystemPtes].NumberOfBytes);

    PointerPte = MiAddressToPte(MiSystemPteSpaceStart);
    MiInitializeSystemPtes(PointerPte, MmNumberOfSystemPtes, SystemPteSpace, MiSystemPteMetadataBuffer);

    MiFirstReservedZeroingPte = MiReserveSystemPtes(MI_ZERO_PTES + 1, SystemPteSpace);
    RtlZeroMemory(MiFirstReservedZeroingPte, (MI_ZERO_PTES + 1) * sizeof(MMPTE));
    MiFirstReservedZeroingPte->u.Hard.PageFrameNumber = MI_ZERO_PTES;

    MmDebugPte = MiReserveSystemPtes(1, SystemPteSpace);
    MiDebugMapping = MiPteToAddress(MmDebugPte);
}

/* PFN DATABASE ***************************************************************/

/* A page-table or mirror page: one reference, one share, parent share bump. */
static
VOID
MiRiscvSetupPfnForTablePage(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_opt_ PMMPTE PteAddress,
    _In_ PFN_NUMBER PteFrame)
{
    PMMPFN Pfn = MiGetPfnEntry(PageFrameIndex);

    if ((PageFrameIndex > MmHighestPhysicalPage) || !MmIsAddressValid(Pfn) ||
        (Pfn->u3.e1.PageLocation != ActiveAndValid))
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, MI_RISCV_BUGCHECK_INIT, 2, PageFrameIndex, (ULONG_PTR)PteAddress);
    }
    Pfn->u1.WsIndex = 0;
    Pfn->u2.ShareCount = 1;
    Pfn->PteAddress = PteAddress;
    MI_MAKE_SOFTWARE_PTE(&Pfn->OriginalPte, MM_READWRITE);
    Pfn->u3.e1.PageLocation = ActiveAndValid;
    Pfn->u3.e1.CacheAttribute = MiCached;
    Pfn->u3.e2.ReferenceCount = 1;
    Pfn->u4.PteFrame = PteFrame;
    if (PteFrame != 0)
        MiGetPfnEntry(PteFrame)->u2.ShareCount++;
}

/* A page mapped by a loader leaf PTE (kernel image, boot data, direct map). */
static
VOID
MiRiscvSetupPfnForMappedPage(
    _In_ PFN_NUMBER PageFrameIndex,
    _In_ PMMPTE PointerPte,
    _In_ PFN_NUMBER TableFrame)
{
    PMMPFN Pfn = MiGetPfnEntry(PageFrameIndex);

    if ((PageFrameIndex <= MmHighestPhysicalPage) && MmIsAddressValid(Pfn) &&
        (Pfn->u3.e1.PageLocation == ActiveAndValid))
    {
        Pfn->u1.WsIndex = 0;
        Pfn->u2.ShareCount++;
        Pfn->PteAddress = PointerPte;
        Pfn->OriginalPte = *PointerPte;
        Pfn->u3.e1.CacheAttribute = MiCached;
        Pfn->u3.e2.ReferenceCount = 1;
        Pfn->u4.PteFrame = TableFrame;
    }
    MiGetPfnEntry(TableFrame)->u2.ShareCount++;
}

CODE_SEG("INIT")
static
VOID
MiRiscvBuildPfnDatabaseFromPageTables(VOID)
{
    PFN_NUMBER RootPfn = MiRiscvBootRootPfn;
    PMMPTE Root = MiRiscvTablePage(RootPfn);
    PMMPFN Pfn;
    ULONG Index, Slot, Leaf;

    /* The root has no parent. */
    Pfn = MiGetPfnEntry(RootPfn);
    ASSERT(Pfn->u3.e1.PageLocation == ActiveAndValid);
    Pfn->u1.WsIndex = 0;
    Pfn->u2.ShareCount = 1;
    Pfn->PteAddress = NULL;
    Pfn->u3.e1.CacheAttribute = MiCached;
    Pfn->u3.e2.ReferenceCount = 1;
    Pfn->u4.PteFrame = 0;

    for (Index = 0; Index < PPE_PER_PAGE; ++Index)
    {
        MMPTE Entry = Root[Index];
        PMMPTE Level1;

        if (!MiRiscvIsTablePointer(Entry))
        {
            ASSERT(Entry.u.Long == 0);
            continue;
        }
        Level1 = MiRiscvTablePage(Entry.u.Hard.PageFrameNumber);

        if (Index == MI_RISCV_WINDOW_INDEX)
        {
            /* M1 and its M0 children are tables, not mapped data pages. */
            MiRiscvSetupPfnForTablePage(Entry.u.Hard.PageFrameNumber, (PMMPTE)PPE_SELFMAP, RootPfn);
            for (Slot = 0; Slot < PDE_PER_PAGE; ++Slot)
            {
                if (MiRiscvIsTablePointer(Level1[Slot]))
                {
                    MiRiscvSetupPfnForTablePage(Level1[Slot].u.Hard.PageFrameNumber,
                                                &Level1[Slot],
                                                Entry.u.Hard.PageFrameNumber);
                }
            }
            continue;
        }

        MiRiscvSetupPfnForTablePage(Entry.u.Hard.PageFrameNumber, MiAddressToPpe(NULL) + Index, RootPfn);
        for (Slot = 0; Slot < PDE_PER_PAGE; ++Slot)
        {
            MMPTE Pde = Level1[Slot];
            PMMPTE Level0;
            PMMPTE WindowPte;

            if (!MiRiscvIsTablePointer(Pde))
            {
                ASSERT(!Pde.u.Hard.Valid);
                continue;
            }
            MiRiscvSetupPfnForTablePage(Pde.u.Hard.PageFrameNumber,
                                        (PMMPTE)(PDE_BASE + ((ULONG64)Index << PAGE_SHIFT)) + Slot,
                                        Entry.u.Hard.PageFrameNumber);
            Level0 = MiRiscvTablePage(Pde.u.Hard.PageFrameNumber);
            WindowPte = (PMMPTE)(PTE_BASE + ((ULONG64)Index << PDI_SHIFT) + ((ULONG64)Slot << PTI_SHIFT));
            for (Leaf = 0; Leaf < PTE_PER_PAGE; ++Leaf)
            {
                if (Level0[Leaf].u.Hard.Valid)
                    MiRiscvSetupPfnForMappedPage(Level0[Leaf].u.Hard.PageFrameNumber, WindowPte + Leaf, Pde.u.Hard.PageFrameNumber);
            }
        }
    }
}

CODE_SEG("INIT")
static
VOID
MiAddDescriptorToDatabase(
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount,
    _In_ TYPE_OF_MEMORY MemoryType)
{
    PMMPFN Pfn;

    ASSERT(!MiIsMemoryTypeInvisible(MemoryType));
    if (MiIsMemoryTypeFree(MemoryType))
    {
        Pfn = &MmPfnDatabase[BasePage + PageCount - 1];
        while (PageCount--)
        {
            Pfn->u3.e1.CacheAttribute = MiCached;
            MiInsertPageInFreeList(BasePage + PageCount);
            Pfn--;
        }
    }
    else if (MemoryType == LoaderXIPRom)
    {
        Pfn = &MmPfnDatabase[BasePage];
        while (PageCount--)
        {
            Pfn->PteAddress = 0;
            Pfn->u1.Flink = 0;
            Pfn->u2.ShareCount = 0;
            Pfn->u3.e1.PageLocation = 0;
            Pfn->u3.e1.CacheAttribute = MiCached;
            Pfn->u3.e1.Rom = 1;
            Pfn->u3.e1.PrototypePte = 1;
            Pfn->u3.e2.ReferenceCount = 0;
            Pfn->u4.InPageError = 0;
            Pfn->u4.PteFrame = 0;
            Pfn++;
        }
    }
    else if (MemoryType == LoaderBad)
    {
        ASSERT(FALSE);
    }
    else
    {
        Pfn = &MmPfnDatabase[BasePage];
        while (PageCount--)
        {
            Pfn->u3.e1.PageLocation = ActiveAndValid;
            Pfn->u3.e1.CacheAttribute = MiCached;
            Pfn++;
        }
    }
}

CODE_SEG("INIT")
static
VOID
MiBuildPfnDatabase(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY ListEntry;
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    PFN_NUMBER BasePage, PageCount;
    KIRQL OldIrql;

    OldIrql = MiAcquirePfnLock();

    MiMapPPEs(MmPfnDatabase, (PUCHAR)MmPfnDatabase + (MxPfnAllocation * PAGE_SIZE) - 1);
    MiMapPDEs(MmPfnDatabase, (PUCHAR)MmPfnDatabase + (MxPfnAllocation * PAGE_SIZE) - 1);
    MiInitializeColorTables();

    for (ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
         ListEntry != &LoaderBlock->MemoryDescriptorListHead;
         ListEntry = ListEntry->Flink)
    {
        Descriptor = CONTAINING_RECORD(ListEntry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
        if (MiIsMemoryTypeInvisible(Descriptor->MemoryType)) continue;
        if (Descriptor == MxFreeDescriptor) Descriptor = &MxOldFreeDescriptor;

        BasePage = Descriptor->BasePage;
        PageCount = Descriptor->PageCount;
        MiMapPTEs(&MmPfnDatabase[BasePage], (PUCHAR)(&MmPfnDatabase[BasePage + PageCount]) - 1);
        if (Descriptor == &MxOldFreeDescriptor) continue;
        MiAddDescriptorToDatabase(BasePage, PageCount, Descriptor->MemoryType);
    }

    /* The free descriptor last; from now on MxGetNextPage is unavailable. */
    BasePage = MxFreeDescriptor->BasePage;
    PageCount = MxFreeDescriptor->PageCount;
    MiAddDescriptorToDatabase(BasePage, PageCount, LoaderFree);

    BasePage = MxOldFreeDescriptor.BasePage;
    PageCount = MxFreeDescriptor->BasePage - BasePage;
    MiAddDescriptorToDatabase(BasePage, PageCount, LoaderMemoryData);
    *MxFreeDescriptor = MxOldFreeDescriptor;

    MiRiscvBuildPfnDatabaseFromPageTables();
    MiPfnsInitialized = TRUE;
    MiReleasePfnLock(OldIrql);
}

/* ENTRY **********************************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
MiInitMachineDependent(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    ULONG Flags;
    PMMPFN Pfn;

    ASSERT(MxPfnAllocation != 0);
    if (!MiRiscvSystemVaLayoutAssigned())
        return STATUS_INVALID_DEVICE_STATE;

    MmHyperSpaceEnd = (PVOID)HYPER_SPACE_END;
    MmPfnDatabase = MiSystemVaRegions[AssignedRegionPfnDatabase].BaseAddress;
    MmWorkingSetList = (PVOID)MI_WORKING_SET_LIST;
    PrototypePte.u.Soft.PageFileHigh = MI_PTE_LOOKUP_NEEDED;

    MiInitializePageTable();
    DPRINT1("RISC-V page-table window live: PTE_BASE %p, root PFN %lx\n", (PVOID)PTE_BASE, MiRiscvBootRootPfn);

    MiBuildNonPagedPool();
    MiBuildSystemPteSpace();
    MiBuildPfnDatabase(LoaderBlock);
    MiRiscvAdoptPageTableAccess();
    DPRINT1("PFN database at %p (%Iu pages), %Iu pages available\n", MmPfnDatabase, MxPfnAllocation, MmAvailablePages);
    ASSERT(MiRiscvWindowConsistent(MiRiscvBootRootPfn));

    KdSetOwedBreakpoints();

    /* Reset the counts that MmInitializeProcessAddressSpace re-establishes. */
    {
        PMMPTE Root = MiRiscvTablePage(MiRiscvBootRootPfn);
        PMMPTE M1 = MiRiscvTablePage(Root[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber);

        Pfn = MiGetPfnEntry(Root[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber);
        Pfn->u2.ShareCount = 0;
        Pfn->u3.e2.ReferenceCount = 0;
        Pfn = MiGetPfnEntry(M1[MI_RISCV_WINDOW_INDEX].u.Hard.PageFrameNumber);
        Pfn->u2.ShareCount = 0;
        Pfn->u3.e2.ReferenceCount = 0;
        Pfn = MiGetPfnEntry(M1[MI_RISCV_HYPERSPACE_INDEX].u.Hard.PageFrameNumber);
        Pfn->u2.ShareCount = 0;
        Pfn->u3.e2.ReferenceCount = 0;
    }
    Pfn = MiGetPfnEntry(MiRiscvBootRootPfn);
    Pfn->u2.ShareCount = 0;
    Pfn->u3.e2.ReferenceCount = 0;
    Pfn = MiGetPfnEntry(PFN_FROM_PPE(MiAddressToPpe((PVOID)HYPER_SPACE)));
    Pfn->u2.ShareCount = 0;
    Pfn->u3.e2.ReferenceCount = 0;
    Pfn = MiGetPfnEntry(PFN_FROM_PDE(MiAddressToPde((PVOID)HYPER_SPACE)));
    Pfn->u2.ShareCount = 0;
    Pfn->u3.e2.ReferenceCount = 0;
    Pfn = MiGetPfnEntry(PFN_FROM_PTE(MiAddressToPte(MmWorkingSetList)));
    Pfn->u2.ShareCount = 0;
    Pfn->u3.e2.ReferenceCount = 0;

    InitializePool(NonPagedPool, 0);

    Flags = 0;
    Status = MmInitializeProcessAddressSpace(PsGetCurrentProcess(), NULL, NULL, &Flags, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("MmInitializeProcessAddressSpace failed: 0x%lx\n", Status);
        return Status;
    }

    MmInitializeBalancer((ULONG)MmAvailablePages, 0);

    ASSERT(MmPfnDatabase);
    ASSERT(MmNonPagedPoolStart);
    ASSERT(MmSizeOfNonPagedPoolInBytes);
    ASSERT(MmMaximumNonPagedPoolInBytes);
    ASSERT(MmNonPagedPoolExpansionStart);
    ASSERT(MmHyperSpaceEnd);
    ASSERT(MmNumberOfSystemPtes);
    ASSERT(MiAddressToPde(MmNonPagedPoolStart)->u.Hard.Valid);
    ASSERT(MiAddressToPte(MmNonPagedPoolStart)->u.Hard.Valid);
    return STATUS_SUCCESS;
}
