/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/arm64/init.c
 * PURPOSE:         ARM64 memory manager initialization
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include <mm/ARM3/miarm.h>

#define IS_PAGE_ALIGNED(addr) ((((ULONG_PTR)(addr)) & (PAGE_SIZE - 1)) == 0)

/* TTBR1 contains ASID in bits[63:48] when TCR.A1=1; mask to PA bits
 * (ARM64_PTE_ADDR_MASK comes from internal/arm64/mm.h). */
#define MI_ARM64_TTBR_TO_PA(_Ttbr) ((UINT64)(_Ttbr) & ARM64_PTE_ADDR_MASK)

BOOLEAN MiArm64SelfMapReady = FALSE;
BOOLEAN ExpArm64PoolBootstrapMode = FALSE;
static VOID MiBuildNonPagedPool(VOID);
static VOID MiBuildSystemPteSpace(VOID);
static BOOLEAN MiArm64CanTouchSystemPageTables(VOID);
static BOOLEAN MiArm64InitializeKernelSelfMap(VOID);
static __inline PVOID MiArm64PhysToKseg0(UINT64 Phys);
static __inline PVOID MiArm64PfnToKseg0(PFN_NUMBER Pfn);
extern MMPTE ValidKernelPte;
extern PVOID MiSystemViewStart;
PVOID MiSystemPteSpaceStart;
extern KSPIN_LOCK NonPagedPoolLock;
extern KSPIN_LOCK MmNonPagedPoolLock;

extern PKIPCR KeArm64CurrentPcr;
extern PKTHREAD KeArm64CurrentThread;
extern PVOID KiArm64P0BootStack;
extern PVOID KiArm64P0BootStackLimit;

static LONG MiArm64SelfMapProbe = -1;
/* Tracks whether PFN database is ready for access. FALSE during early bootstrap. */
static BOOLEAN MiArm64PfnDatabaseReady = FALSE;
static BOOLEAN MiArm64PfnFreeListsReady = FALSE;

static VOID MiMapPPEPages(PVOID StartAddress, PVOID EndAddress);
static VOID MiMapPPEs(PVOID StartAddress, PVOID EndAddress);
static VOID MiMapPDEs(PVOID StartAddress, PVOID EndAddress);
static VOID MiMapPTEs(PVOID StartAddress, PVOID EndAddress);
static VOID MiArm64MapKseg0IdentityRange(PVOID BaseAddress, SIZE_T Size);
static VOID MiArm64MapKseg0IdentityRangeWithAttr(PVOID BaseAddress, SIZE_T Size, ULONG AttrIndex, BOOLEAN FlushOnMap);
static VOID MiArm64MapEarlyAliasRange(PVOID BaseAddress, SIZE_T Size);
static VOID MiArm64MapEarlyAliasRangeWithAttr(PVOID BaseAddress, SIZE_T Size, ULONG AttrIndex);
static VOID MiArm64MapEarlyDeviceRange(ULONGLONG BaseAddress, SIZE_T Size);
static VOID MiArm64MapEarlyDeviceRanges(PARM64_LOADER_BLOCK Arm64Block);
static VOID MiArm64MapEarlyStackRange(ULONG_PTR StackLimit, ULONG_PTR StackTop);
static VOID MiArm64MapCurrentStackAlias(VOID);
VOID MiArm64MapPageTablePage(UINT64 Ttbr1, PVOID TableVa, PFN_NUMBER Pfn);
static VOID MiArm64MapLoaderProcessorState(PLOADER_PARAMETER_BLOCK LoaderBlock);
static VOID MiArm64MapLoaderPhysicalMemory(PLOADER_PARAMETER_BLOCK LoaderBlock);

static __inline VOID
MiArm64FlushTranslationChanges(VOID)
{
    __asm__ __volatile__(
        "dsb ishst\n\t"
        "tlbi vmalle1is\n\t"
        "dsb ish\n\t"
        "isb"
        ::: "memory");
}

static __inline VOID
MiArm64SyncL0ToRoot(ULONG L0Index, UINT64 Desc)
{
    volatile UINT64 *RootL0;

    ASSERT(L0Index < PXE_PER_PAGE);

    /*
     * PXE_BASE is the recursive view of the active TTBR1 root. Keep L0
     * updates on the self-map path instead of relying on early physical-map
     * coverage for the root page, which can live high in large-memory boots.
     */
    RootL0 = (volatile UINT64 *)PXE_BASE;
    RootL0[L0Index] = Desc;
    MiArm64CleanEntryToPoC(&RootL0[L0Index]);
    MiArm64FlushTranslationChanges();
}

#define ARM64_PTE_AF                PTE_ACCESSED  /* Access Flag - required for L3 page entries */

static __inline PVOID
MiArm64PhysToKseg0(UINT64 Phys)
{
    return (PVOID)MI_ARM64_PHYS_TO_VA(Phys);
}

static __inline PVOID
MiArm64PfnToKseg0(PFN_NUMBER Pfn)
{
    return MiArm64PhysToKseg0(((UINT64)Pfn) << PAGE_SHIFT);
}

static
BOOLEAN
MiArm64MapKseg0IdentityBlocks(
    _In_ ULONG_PTR StartVa,
    _In_ ULONG_PTR EndVa,
    _In_ BOOLEAN FlushOnMap)
{
    ULONG_PTR StartBlock, EndBlock, Va;
    BOOLEAN MappedAny = FALSE;

    StartBlock = ALIGN_DOWN_BY(StartVa, 1ULL << PDI_SHIFT);
    EndBlock = ALIGN_DOWN_BY(EndVa, 1ULL << PDI_SHIFT);

    for (Va = StartBlock; ; Va += (1ULL << PDI_SHIFT))
    {
        PMMPDE PointerPde;

        MiMapPPEs((PVOID)Va, (PVOID)Va);
        PointerPde = MiAddressToPde((PVOID)Va);

        if ((PointerPde->u.Long & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_TABLE)
        {
            return FALSE;
        }

        if (Va == EndBlock)
        {
            break;
        }
    }

    for (Va = StartBlock; ; Va += (1ULL << PDI_SHIFT))
    {
        PMMPDE PointerPde;
        UINT64 Entry;
        UINT64 BlockPa;

        PointerPde = MiAddressToPde((PVOID)Va);
        Entry = PointerPde->u.Long;
        if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_INVALID)
        {
            BlockPa = Va - (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE;
            PointerPde->u.Long = (BlockPa & ARM64_PTE_ADDR_MASK) |
                                 ARM64_PTE_TYPE_BLOCK |
                                 ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2) |
                                 ARM64_PTE_SH_INNER |
                                 ARM64_PTE_AF |
                                 ARM64_PTE_PXN |
                                 ARM64_PTE_UXN;
            MiArm64CleanEntryToPoC((volatile UINT64 *)PointerPde);
            MappedAny = TRUE;
        }

        if (Va == EndBlock)
        {
            break;
        }
    }

    if (MappedAny && FlushOnMap)
    {
        MiArm64FlushTranslationChanges();
    }

    return TRUE;
}

static
VOID
MiArm64MapEarlyAliasRangeWithAttr(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T Size,
    _In_ ULONG AttrIndex)
{
    ULONG_PTR StartVa, EndVa, Va, StartPa;
    BOOLEAN MappedAny = FALSE;

    if ((BaseAddress == NULL) || (Size == 0))
    {
        return;
    }

    StartVa = ALIGN_DOWN_BY((ULONG_PTR)BaseAddress, PAGE_SIZE);
    EndVa = ALIGN_DOWN_BY((ULONG_PTR)BaseAddress + Size - 1, PAGE_SIZE);

    if (StartVa >= (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE)
    {
        StartPa = StartVa - (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE;
    }
    else if (StartVa >= (ULONG_PTR)MI_ARM64_BOOT_IMAGE_BASE)
    {
        StartPa = StartVa - (ULONG_PTR)MI_ARM64_BOOT_IMAGE_BASE;
    }
    else if (StartVa >= (ULONG_PTR)KSEG0_BASE)
    {
        StartPa = StartVa - (ULONG_PTR)KSEG0_BASE;
    }
    else
    {
        StartPa = StartVa;
    }

    MiMapPDEs((PVOID)StartVa, (PVOID)EndVa);

    for (Va = StartVa; Va <= EndVa; Va += PAGE_SIZE)
    {
        PMMPTE PointerPte = MiAddressToPte((PVOID)Va);
        MMPTE Pte = ValidKernelPte;
        PFN_NUMBER Pfn = (PFN_NUMBER)((StartPa + (Va - StartVa)) >> PAGE_SHIFT);

        Pte.u.Hard.PageFrameNumber = Pfn;
        MI_SET_PTE_ATTR_INDEX(&Pte, AttrIndex);

        if (PointerPte->u.Long != Pte.u.Long)
        {
            *PointerPte = Pte;
            MiArm64CleanEntryToPoC((volatile UINT64 *)PointerPte);
            MappedAny = TRUE;
        }

        if (Va == EndVa)
        {
            break;
        }
    }

    if (MappedAny)
    {
        MiArm64FlushTranslationChanges();
    }
}

static
VOID
MiArm64MapEarlyAliasRange(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T Size)
{
    MiArm64MapEarlyAliasRangeWithAttr(BaseAddress,
                                      Size,
                                      MI_ARM64_MAIR_NORMAL_WB_IDX);
}

static
VOID
MiArm64MapEarlyDeviceRange(
    _In_ ULONGLONG BaseAddress,
    _In_ SIZE_T Size)
{
    if ((BaseAddress == 0) || (Size == 0))
    {
        return;
    }

    MiArm64MapKseg0IdentityRangeWithAttr((PVOID)(ULONG_PTR)BaseAddress,
                                         Size,
                                         MI_ARM64_MAIR_DEVICE_nGnRnE_IDX,
                                         TRUE);
    MiArm64MapEarlyAliasRangeWithAttr((PVOID)(ULONG_PTR)BaseAddress,
                                      Size,
                                      MI_ARM64_MAIR_DEVICE_nGnRnE_IDX);
}

static
VOID
MiArm64MapEarlyDeviceRanges(
    _In_ PARM64_LOADER_BLOCK Arm64Block)
{
    ULONG Count;

    if (Arm64Block == NULL)
    {
        return;
    }

    Count = Arm64Block->EarlyDeviceRangeCount;
    if (Count > ARM64_LOADER_MAX_EARLY_DEVICE_RANGES)
    {
        Count = ARM64_LOADER_MAX_EARLY_DEVICE_RANGES;
    }

    for (ULONG Index = 0; Index < Count; ++Index)
    {
        PARM64_LOADER_EARLY_DEVICE_RANGE Range;

        Range = &Arm64Block->EarlyDeviceRanges[Index];
        MiArm64MapEarlyDeviceRange(Range->BaseAddress,
                                   (SIZE_T)Range->Length);
    }
}

static
VOID
MiArm64MapKseg0IdentityRangeWithAttr(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T Size,
    _In_ ULONG AttrIndex,
    _In_ BOOLEAN FlushOnMap)
{
    ULONG_PTR StartVa, EndVa, Va;
    BOOLEAN MappedAny = FALSE;

    if ((BaseAddress == NULL) || (Size == 0))
    {
        return;
    }

    StartVa = ALIGN_DOWN_BY((ULONG_PTR)BaseAddress, PAGE_SIZE);
    EndVa = ALIGN_DOWN_BY((ULONG_PTR)BaseAddress + Size - 1, PAGE_SIZE);

    if (StartVa >= (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE)
    {
        StartVa -= (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE;
        EndVa -= (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE;
    }
    else if (StartVa >= (ULONG_PTR)MI_ARM64_BOOT_IMAGE_BASE)
    {
        StartVa -= (ULONG_PTR)MI_ARM64_BOOT_IMAGE_BASE;
        EndVa -= (ULONG_PTR)MI_ARM64_BOOT_IMAGE_BASE;
    }
    else if (StartVa >= (ULONG_PTR)KSEG0_BASE)
    {
        StartVa -= (ULONG_PTR)KSEG0_BASE;
        EndVa -= (ULONG_PTR)KSEG0_BASE;
    }

    StartVa += (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE;
    EndVa += (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE;

    if (AttrIndex == MI_ARM64_MAIR_NORMAL_WB_IDX)
    {
        if (MiArm64MapKseg0IdentityBlocks(StartVa, EndVa, FlushOnMap))
        {
            return;
        }

        if (MiArm64PfnDatabaseReady)
        {
            UINT64 Ttbr1;

            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
            for (Va = StartVa; ; Va += PAGE_SIZE)
            {
                MiArm64MapPageTablePage(Ttbr1,
                                        (PVOID)Va,
                                        (PFN_NUMBER)((Va - (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE) >> PAGE_SHIFT));
                if (Va == EndVa)
                {
                    break;
                }
            }
            return;
        }
    }

    MiMapPDEs((PVOID)StartVa, (PVOID)EndVa);

    for (Va = StartVa; Va <= EndVa; Va += PAGE_SIZE)
    {
        PMMPTE PointerPte = MiAddressToPte((PVOID)Va);
        MMPTE Pte = ValidKernelPte;
        PFN_NUMBER Pfn = (PFN_NUMBER)((Va - (ULONG_PTR)MI_ARM64_PHYS_MAP_BASE) >> PAGE_SHIFT);

        Pte.u.Hard.PageFrameNumber = Pfn;
        MI_SET_PTE_ATTR_INDEX(&Pte, AttrIndex);

        if (PointerPte->u.Long != Pte.u.Long)
        {
            *PointerPte = Pte;
            MiArm64CleanEntryToPoC((volatile UINT64 *)PointerPte);
            MappedAny = TRUE;
        }

        if (Va == EndVa)
        {
            break;
        }
    }

    if (MappedAny && FlushOnMap)
    {
        MiArm64FlushTranslationChanges();
    }
}

static
VOID
MiArm64MapKseg0IdentityRange(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T Size)
{
    MiArm64MapKseg0IdentityRangeWithAttr(BaseAddress,
                                         Size,
                                         MI_ARM64_MAIR_NORMAL_WB_IDX,
                                         TRUE);
}

VOID
MiArm64MapKseg0Page(
    _In_ PFN_NUMBER PageFrameNumber)
{
    MiArm64MapKseg0IdentityRange((PVOID)(ULONG_PTR)(((UINT64)PageFrameNumber) << PAGE_SHIFT),
                                 PAGE_SIZE);
}

static
BOOLEAN
MiArm64ShouldMapKseg0Descriptor(
    _In_ TYPE_OF_MEMORY MemoryType)
{
    if ((MemoryType == LoaderBad) || MiIsMemoryTypeInvisible(MemoryType))
    {
        return FALSE;
    }

    return TRUE;
}

static
VOID
MiArm64MapKseg0PhysicalRange(
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount)
{
    PFN_NUMBER LastPage;
    ULONG_PTR Current;
    ULONG_PTR End;

    if (PageCount == 0)
    {
        return;
    }

    if (BasePage > MmHighestPhysicalPage)
    {
        return;
    }

    LastPage = BasePage + PageCount - 1;
    if ((LastPage < BasePage) || (LastPage > MmHighestPhysicalPage))
    {
        LastPage = MmHighestPhysicalPage;
    }

    Current = (ULONG_PTR)BasePage << PAGE_SHIFT;
    End = ((ULONG_PTR)LastPage << PAGE_SHIFT) + PAGE_SIZE - 1;

    while (Current <= End)
    {
        ULONG_PTR ChunkEnd;
        ULONG_PTR BlockEnd;

        BlockEnd = ALIGN_DOWN_BY(Current, 1ULL << PDI_SHIFT) +
                   (1ULL << PDI_SHIFT) - 1;
        ChunkEnd = min(BlockEnd, End);

        /* Defer the broadcast TLB flush: every chunk here transitions
         * invalid->valid, so one flush per range replaces one per 2MB chunk. */
        MiArm64MapKseg0IdentityRangeWithAttr((PVOID)Current,
                                             ChunkEnd - Current + 1,
                                             MI_ARM64_MAIR_NORMAL_WB_IDX,
                                             FALSE);

        if (ChunkEnd == End)
        {
            break;
        }

        Current = ChunkEnd + 1;
    }

    MiArm64FlushTranslationChanges();
}

static
VOID
MiArm64MapLoaderPhysicalMemory(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY NextEntry;

    if (LoaderBlock == NULL)
    {
        return;
    }

    NextEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
    while (NextEntry != &LoaderBlock->MemoryDescriptorListHead)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;

        Descriptor = CONTAINING_RECORD(NextEntry,
                                       MEMORY_ALLOCATION_DESCRIPTOR,
                                       ListEntry);

        if (MiArm64ShouldMapKseg0Descriptor(Descriptor->MemoryType))
        {
            if ((Descriptor == MxFreeDescriptor) &&
                (MxOldFreeDescriptor.PageCount != 0))
            {
                /*
                 * Early ARM64 page tables are allocated from MxFreeDescriptor
                 * before the KSEG0 direct map is finalized. Map the original
                 * run so those consumed table pages keep a stable direct alias.
                 */
                MiArm64MapKseg0PhysicalRange(MxOldFreeDescriptor.BasePage,
                                             MxOldFreeDescriptor.PageCount);
            }
            else
            {
                MiArm64MapKseg0PhysicalRange(Descriptor->BasePage,
                                             Descriptor->PageCount);
            }
        }

        NextEntry = Descriptor->ListEntry.Flink;
    }
}

static
VOID
MiArm64MapEarlyStackRange(
    _In_ ULONG_PTR StackLimit,
    _In_ ULONG_PTR StackTop)
{
    SIZE_T StackSize;

    if ((StackLimit == 0) || (StackTop == 0) || (StackLimit >= StackTop))
    {
        return;
    }

    StackLimit = ALIGN_DOWN_BY(StackLimit, PAGE_SIZE);
    StackTop = ALIGN_UP_BY(StackTop, PAGE_SIZE);
    StackSize = StackTop - StackLimit;

    MiArm64MapKseg0IdentityRange((PVOID)StackLimit, StackSize);
    MiArm64MapEarlyAliasRange((PVOID)StackLimit, StackSize);
}

static
VOID
MiArm64MapCurrentStackAlias(VOID)
{
    ULONG_PTR CurrentSp;
    ULONG_PTR StackBase;
    SIZE_T StackSize;

    __asm__ __volatile__("mov %0, sp" : "=r"(CurrentSp));

    if (CurrentSp <= KERNEL_STACK_SIZE)
    {
        return;
    }

    StackBase = ALIGN_DOWN_BY(CurrentSp - KERNEL_STACK_SIZE, PAGE_SIZE);
    StackSize = ALIGN_UP_BY((CurrentSp - StackBase) + PAGE_SIZE, PAGE_SIZE);

    MiArm64MapEarlyStackRange(StackBase, StackBase + StackSize);
}

static
VOID
MiArm64MapLoaderProcessorState(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARM64_LOADER_BLOCK Arm64Block;
    UINT64 Ttbr0, Ttbr1;

    if (LoaderBlock == NULL)
    {
        return;
    }

    Arm64Block = &LoaderBlock->u.Arm64;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    MiArm64MapCurrentStackAlias();
    MiArm64MapEarlyDeviceRanges(Arm64Block);

    if (MI_ARM64_TTBR_TO_PA(Ttbr0) != 0)
    {
        MiArm64MapKseg0IdentityRange((PVOID)(ULONG_PTR)MI_ARM64_TTBR_TO_PA(Ttbr0),
                                     PAGE_SIZE);
    }

    if (MI_ARM64_TTBR_TO_PA(Ttbr1) != 0)
    {
        MiArm64MapKseg0IdentityRange((PVOID)(ULONG_PTR)MI_ARM64_TTBR_TO_PA(Ttbr1),
                                     PAGE_SIZE);
    }

    MiArm64MapKseg0IdentityRange((PVOID)(ULONG_PTR)Arm64Block->PcrPage,
                                 8 * PAGE_SIZE);
    MiArm64MapEarlyAliasRange((PVOID)(ULONG_PTR)Arm64Block->PcrPage,
                              8 * PAGE_SIZE);

    if (LoaderBlock->KernelStack != 0)
    {
        ULONG_PTR StackTop = (ULONG_PTR)LoaderBlock->KernelStack;
        ULONG_PTR StackLimit = StackTop - KERNEL_STACK_SIZE;

        if ((StackTop == (ULONG_PTR)KiArm64P0BootStack) &&
            (KiArm64P0BootStackLimit != NULL) &&
            ((ULONG_PTR)KiArm64P0BootStackLimit < StackTop))
        {
            StackLimit = (ULONG_PTR)KiArm64P0BootStackLimit;
        }

        MiArm64MapEarlyStackRange(StackLimit, StackTop);
    }

    if (LoaderBlock->Thread != 0)
    {
        PKTHREAD Thread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;

        MiArm64MapEarlyStackRange((ULONG_PTR)Thread->StackLimit,
                                  (ULONG_PTR)Thread->StackBase);

        if (Thread->KernelStack != NULL)
        {
            ULONG_PTR StackTop = (ULONG_PTR)Thread->KernelStack;
            MiArm64MapEarlyStackRange(StackTop - KERNEL_STACK_SIZE,
                                      StackTop);
        }
    }

    if (Arm64Block->PanicStack != 0)
    {
        MiArm64MapEarlyStackRange((ULONG_PTR)Arm64Block->PanicStack -
                                  KERNEL_STACK_SIZE,
                                  (ULONG_PTR)Arm64Block->PanicStack);
    }

    if (Arm64Block->InterruptStack != 0)
    {
        MiArm64MapEarlyStackRange((ULONG_PTR)Arm64Block->InterruptStack -
                                  KERNEL_STACK_SIZE,
                                  (ULONG_PTR)Arm64Block->InterruptStack);
    }

}


/*
 * MiArm64BuildPfnDatabaseFromPages - Initialize PFN entries for boot-loaded modules
 *
 * Walk the TTBR1 recursive self-map to initialize PFN entries for both data
 * pages and page-table pages of boot-loaded kernel modules.
 *
 * On amd64, MiBuildPfnDatabaseFromPages walks all valid PDEs in the self-map and
 * initializes each page table PFN with ShareCount = 1 + N (one per valid PTE).
 * On ARM64, the self-map may not be fully valid at this early stage, and the
 * previous implementation only initialized data page PFNs (not page table PFNs).
 *
 * Without page table PFN initialization:
 * - Page table PFNs have RefCount=0, get inserted into the free list
 * - MiDeleteSystemPageableVm (INIT section discard) calls MiDecrementShareCount
 *   on the page table PFN → PageLocation=FreePageList → PFN_LIST_CORRUPT 0x4E/0x99
 *
 * Without proper ShareCount tracking:
 * - Page table PFNs have ShareCount=1 (from MiBuildPfnDatabaseFromLoaderBlock default case)
 * - MiDeleteSystemPageableVm decrements ShareCount N times (once per INIT PTE)
 * - If N > 1, ShareCount underflows → MiDecrementShareCount frees the page table
 *   prematurely → subsequent PTE decrements crash
 */
CODE_SEG("INIT")
VOID
NTAPI
MiArm64BuildPfnDatabaseFromPages(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY ListHead, NextEntry;
    PLDR_DATA_TABLE_ENTRY DataEntry;
    UINT64 Ttbr1;
    PFN_NUMBER L0Pfn;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    L0Pfn = (PFN_NUMBER)(MI_ARM64_TTBR_TO_PA(Ttbr1) >> PAGE_SHIFT);

    ListHead = &LoaderBlock->LoadOrderListHead;
    NextEntry = ListHead->Flink;

    while (NextEntry != ListHead)
    {
        DataEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        NextEntry = NextEntry->Flink;

        if (DataEntry->DllBase && DataEntry->SizeOfImage)
        {
            ULONG_PTR Base = (ULONG_PTR)DataEntry->DllBase;
            ULONG_PTR End = Base + DataEntry->SizeOfImage;
            ULONG_PTR Va;
            ULONG PrevL0Idx = ~0U, PrevL1Idx = ~0U, PrevL2Idx = ~0U;
            PFN_NUMBER L1Pfn = 0, L2Pfn = 0, L3Pfn = 0;
            PMMPFN PfnL3 = NULL;

            for (Va = Base; Va < End; Va += PAGE_SIZE)
            {
                ULONG L0Idx = (Va >> PXI_SHIFT) & PXI_MASK;
                ULONG L1Idx = (Va >> PPI_SHIFT) & PPI_MASK;
                ULONG L2Idx = (Va >> PDI_SHIFT) & PDI_MASK_ARM64;
                UINT64 Entry;
                PFN_NUMBER DataPfn;
                PMMPFN Pfn;
                PMMPTE PointerPxe;
                PMMPTE PointerPpe;
                PMMPDE PointerPde;
                PMMPTE PointerPte;

                /* Walk L0 -> L1 */
                if (L0Idx != PrevL0Idx)
                {
                    PointerPxe = MiAddressToPxe((PVOID)Va);
                    Entry = PointerPxe->u.Long;
                    if ((Entry & 3) != ARM64_PTE_TYPE_TABLE) break;
                    L1Pfn = PFN_FROM_PTE(PointerPxe);
                    PrevL0Idx = L0Idx;
                    PrevL1Idx = ~0U;
                    PrevL2Idx = ~0U;

                    /* Initialize L1 page table PFN */
                    if (L1Pfn <= MmHighestPhysicalPage)
                    {
                        Pfn = MiGetPfnEntry(L1Pfn);
                        if (Pfn->u3.e2.ReferenceCount == 0)
                        {
                            Pfn->u3.e2.ReferenceCount = 1;
                            Pfn->u2.ShareCount = 1;
                            Pfn->u3.e1.PageLocation = ActiveAndValid;
                            Pfn->u3.e1.CacheAttribute = MiNonCached;
                            Pfn->u4.PteFrame = L0Pfn;
                            Pfn->PteAddress = PointerPxe;
                        }
                    }
                }

                /* Walk L1 -> L2 */
                if (L1Idx != PrevL1Idx)
                {
                    PointerPpe = MiAddressToPpe((PVOID)Va);
                    Entry = PointerPpe->u.Long;
                    if ((Entry & 3) != ARM64_PTE_TYPE_TABLE)
                    {
                        PrevL1Idx = L1Idx;
                        PfnL3 = NULL;
                        PrevL2Idx = ~0U;
                        continue;
                    }
                    L2Pfn = PFN_FROM_PTE(PointerPpe);
                    PrevL1Idx = L1Idx;
                    PrevL2Idx = ~0U;

                    /* Initialize L2 page table PFN */
                    if (L2Pfn <= MmHighestPhysicalPage)
                    {
                        Pfn = MiGetPfnEntry(L2Pfn);
                        if (Pfn->u3.e2.ReferenceCount == 0)
                        {
                            Pfn->u3.e2.ReferenceCount = 1;
                            Pfn->u2.ShareCount = 1;
                            Pfn->u3.e1.PageLocation = ActiveAndValid;
                            Pfn->u3.e1.CacheAttribute = MiNonCached;
                            Pfn->u4.PteFrame = L1Pfn;
                            Pfn->PteAddress = PointerPpe;
                        }
                    }
                }

                /* Walk L2 -> L3 */
                if (L2Idx != PrevL2Idx)
                {
                    PointerPde = MiAddressToPde((PVOID)Va);
                    Entry = PointerPde->u.Long;
                    if ((Entry & 3) != ARM64_PTE_TYPE_TABLE)
                    {
                        PrevL2Idx = L2Idx;
                        PfnL3 = NULL;
                        continue;
                    }
                    L3Pfn = PFN_FROM_PTE(PointerPde);
                    PrevL2Idx = L2Idx;

                    /* Initialize L3 page table PFN */
                    PfnL3 = NULL;
                    if (L3Pfn <= MmHighestPhysicalPage)
                    {
                        PfnL3 = MiGetPfnEntry(L3Pfn);
                        if (PfnL3->u3.e2.ReferenceCount == 0)
                        {
                            PfnL3->u3.e2.ReferenceCount = 1;
                            PfnL3->u2.ShareCount = 1; /* baseline for L2→L3 entry */
                            PfnL3->u3.e1.PageLocation = ActiveAndValid;
                            PfnL3->u3.e1.CacheAttribute = MiNonCached;
                            PfnL3->u4.PteFrame = L2Pfn;
                            PfnL3->PteAddress = (PMMPTE)PointerPde;
                        }
                    }
                }

                /* Read L3 leaf entry */
                if (!PfnL3) continue;
                PointerPte = MiAddressToPte((PVOID)Va);
                Entry = PointerPte->u.Long;
                if (!(Entry & 1)) continue;

                DataPfn = PFN_FROM_PTE(PointerPte);
                if (DataPfn > MmHighestPhysicalPage) continue;

                /*
                 * Increment L3 page table ShareCount for this data page PTE.
                 * This mirrors the amd64 inner loop in MiBuildPfnDatabaseFromPages
                 * (mminit.c line 1015: Pfn1->u2.ShareCount++) which increments the
                 * PDE page's ShareCount for each valid PTE.
                 *
                 * MiDeleteSystemPageableVm decrements L3 ShareCount once per valid
                 * PTE it frees. Without this increment, ShareCount underflows.
                 */
                if (PfnL3) PfnL3->u2.ShareCount++;

                /* Initialize data page PFN */
                Pfn = MiGetPfnEntry(DataPfn);
                if (Pfn->u3.e2.ReferenceCount == 0)
                {
                    Pfn->u3.e2.ReferenceCount = 1;
                    Pfn->u2.ShareCount = 1;
                    Pfn->u3.e1.PageLocation = ActiveAndValid;
                    Pfn->u3.e1.CacheAttribute = MiNonCached;
                    Pfn->u4.PteFrame = L3Pfn;
                    Pfn->PteAddress = PointerPte;
                }
            }
        }
    }
}

static __inline volatile UINT64*
MiArm64LookupTableEntry(PVOID Va, ULONG Level)
{
    volatile UINT64 *Entry;

    Entry = (volatile UINT64 *)MiAddressToPxe(Va);
    if (Level == 0)
        return Entry;

    if ((*Entry & 1ULL) == 0)
        return NULL;

    Entry = (volatile UINT64 *)MiAddressToPpe(Va);
    if (Level == 1)
        return Entry;

    if ((*Entry & 1ULL) == 0)
        return NULL;

    Entry = (volatile UINT64 *)MiAddressToPde(Va);
    if (Level == 2)
        return Entry;

    if ((*Entry & 1ULL) == 0)
        return NULL;

    return (volatile UINT64 *)MiAddressToPte(Va);
}

static __inline UINT64
MiArm64BlockEntryAttrs(_In_ UINT64 Entry)
{
    return Entry & ~(ARM64_PTE_ADDR_MASK | ARM64_PTE_TYPE_MASK);
}

static __inline VOID
MiArm64BreakBeforeMake(_Inout_ volatile UINT64 *Entry)
{
    *Entry = 0;
    MiArm64CleanEntryToPoC(Entry);
    MiArm64FlushTranslationChanges();
}

static __inline VOID
MiArm64PublishTableDesc(_Inout_ volatile UINT64 *Entry, _In_ PFN_NUMBER Pfn)
{
    *Entry = ARM64_MAKE_TABLE_DESCRIPTOR(Pfn);
    MiArm64CleanEntryToPoC(Entry);
    MiArm64FlushTranslationChanges();
}

static __inline VOID
MiArm64PublishAndZeroTableDesc(
    _Inout_ volatile UINT64 *Entry,
    _In_ PFN_NUMBER Pfn,
    _In_ PVOID SelfMapAddress)
{
    MiArm64PublishTableDesc(Entry, Pfn);
    RtlZeroMemory((PVOID)ALIGN_DOWN_BY((ULONG_PTR)SelfMapAddress, PAGE_SIZE),
                  PAGE_SIZE);
    MiArm64CleanPageToPoC(SelfMapAddress);
    __asm__ __volatile__("dsb ishst" ::: "memory");
}

static __inline BOOLEAN
MiArm64SelfMapEntryMatchesRoot(
    _In_ UINT64 Entry,
    _In_ PFN_NUMBER RootPfn)
{
    return (((Entry & ARM64_PTE_ADDR_MASK) == ((UINT64)RootPfn << PAGE_SHIFT)) &&
            ((Entry & ~ARM64_PTE_ADDR_MASK) == ARM64_PTE_TABLE_DESCRIPTOR_ATTRS));
}

PFN_NUMBER
MiArm64AllocatePageTablePage(VOID)
{
    PFN_NUMBER Pfn;
    KIRQL OldIrql;
    ULONG CpuIndex;
    BOOLEAN ReleasePfnLock;

    if (MiArm64PfnDatabaseReady && MiArm64PfnFreeListsReady)
    {
        CpuIndex = KeGetCurrentProcessorNumber();
        ReleasePfnLock = TRUE;

        if ((CpuIndex < MAXIMUM_PROCESSORS) &&
            (MiArm64PfnLockDepth[CpuIndex].Depth > 0))
        {
            OldIrql = DISPATCH_LEVEL;
            ReleasePfnLock = FALSE;
        }
        else
        {
            OldIrql = MiAcquirePfnLock();
        }

        Pfn = MiRemoveAnyPage(MI_GET_NEXT_COLOR());
        if (ReleasePfnLock)
        {
            MiReleasePfnLock(OldIrql);
        }
        if (Pfn == 0)
        {
            KeBugCheckEx(INSTALL_MORE_MEMORY,
                         MmNumberOfPhysicalPages,
                         MmLowestPhysicalPage,
                         MmHighestPhysicalPage,
                         2);
        }
        return Pfn;
    }

    return MxGetNextPage(1);
}

BOOLEAN
MiArm64IsPfnDatabaseReady(VOID)
{
    return MiArm64PfnDatabaseReady;
}

/*
 * Split a block descriptor into a table of 512 next-level entries covering
 * the same range (break-before-make). EntryShift/LeafType select the level:
 * L1->L2 uses (PDI_SHIFT, ARM64_PTE_TYPE_BLOCK), L2->L3 uses
 * (PAGE_SHIFT, ARM64_PTE_TYPE_PAGE).
 */
static
VOID
MiArm64SplitBlock(
    _Inout_ volatile UINT64 *Entry,
    _In_ PFN_NUMBER ParentPfn,
    _In_ ULONG EntryShift,
    _In_ UINT64 LeafType)
{
    UINT64 Block = *Entry;
    if ((Block & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_BLOCK)
    {
        return;
    }

    PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();

    volatile UINT64 *Table = (volatile UINT64 *)MiArm64PfnToKseg0(NewPfn);
    UINT64 Base = Block & ARM64_PTE_ADDR_MASK;
    UINT64 Attrs = MiArm64BlockEntryAttrs(Block) | ARM64_PTE_AF;

    for (ULONG Index = 0; Index < 512; ++Index)
    {
        UINT64 Pa = Base + ((UINT64)Index << EntryShift);
        Table[Index] = (Pa & ARM64_PTE_ADDR_MASK) | Attrs | LeafType;
    }

    MiArm64CleanPageToPoC((PVOID)Table);
    __asm__ __volatile__("dsb ishst" ::: "memory");
    MiArm64BreakBeforeMake(Entry);
    MiArm64PublishTableDesc(Entry, NewPfn);

    if (MiArm64PfnDatabaseReady)
    {
        MiInitializePfnForOtherProcess(NewPfn, (PVOID)Entry, ParentPfn);
    }
}

static
VOID
MiArm64SplitL1BlockToL2(_Inout_ volatile UINT64 *Entry, _In_ PFN_NUMBER ParentPfn)
{
    MiArm64SplitBlock(Entry, ParentPfn, PDI_SHIFT, ARM64_PTE_TYPE_BLOCK);
}

static
VOID
MiArm64SplitL2BlockToL3(_Inout_ volatile UINT64 *Entry, _In_ PFN_NUMBER ParentPfn)
{
    MiArm64SplitBlock(Entry, ParentPfn, PAGE_SHIFT, ARM64_PTE_TYPE_PAGE);
}



MI_ARM64_PFN_LOCK_DEPTH MiArm64PfnLockDepth[MAXIMUM_PROCESSORS] = {{0}};

VOID
MiArm64MapPageTablePage(UINT64 Ttbr1, PVOID TableVa, PFN_NUMBER Pfn)
{
    UINT64 root_pa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    volatile UINT64 *l0 = (volatile UINT64 *)MiArm64PhysToKseg0(root_pa);
    ULONG l0_idx = MiAddressToPxi(TableVa);

    if ((l0[l0_idx] & 1ULL) == 0)
    {
        PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
        RtlZeroMemory(MiArm64PfnToKseg0(NewPfn), PAGE_SIZE);
        MiArm64CleanPageToPoC(MiArm64PfnToKseg0(NewPfn));
        MiArm64PublishTableDesc(&l0[l0_idx], NewPfn);
        MiArm64SyncL0ToRoot(l0_idx, l0[l0_idx]);
        if (MiArm64PfnDatabaseReady)
        {
            PFN_NUMBER L0Pfn = root_pa >> PAGE_SHIFT;
            volatile UINT64 *L0Entry = &l0[l0_idx];
            MiInitializePfnForOtherProcess(NewPfn,
                                           (PVOID)L0Entry,
                                           L0Pfn);
        }
    }

    volatile UINT64 *l1 = (volatile UINT64 *)MiArm64PhysToKseg0(l0[l0_idx] & ARM64_PTE_ADDR_MASK);
    ULONG l1_idx = (((ULONG_PTR)TableVa) >> PPI_SHIFT) & 0x1FF;

    if ((l1[l1_idx] & 1ULL) == 0)
    {
        PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
        RtlZeroMemory(MiArm64PfnToKseg0(NewPfn), PAGE_SIZE);
        MiArm64CleanPageToPoC(MiArm64PfnToKseg0(NewPfn));
        MiArm64PublishTableDesc(&l1[l1_idx], NewPfn);
        if (MiArm64PfnDatabaseReady)
        {
            PFN_NUMBER L1Pfn = (l0[l0_idx] & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
            volatile UINT64 *L1Entry = &l1[l1_idx];
            MiInitializePfnForOtherProcess(NewPfn,
                                           (PVOID)L1Entry,
                                           L1Pfn);
        }
    }
    else if ((l1[l1_idx] & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
    {
        PFN_NUMBER L0Pfn = root_pa >> PAGE_SHIFT;
        MiArm64SplitL1BlockToL2(&l1[l1_idx], L0Pfn);
    }

    volatile UINT64 *l2 = (volatile UINT64 *)MiArm64PhysToKseg0(l1[l1_idx] & ARM64_PTE_ADDR_MASK);
    ULONG l2_idx = (((ULONG_PTR)TableVa) >> PDI_SHIFT) & 0x1FF;

    if ((l2[l2_idx] & 1ULL) == 0)
    {
        PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
        RtlZeroMemory(MiArm64PfnToKseg0(NewPfn), PAGE_SIZE);
        MiArm64CleanPageToPoC(MiArm64PfnToKseg0(NewPfn));
        MiArm64PublishTableDesc(&l2[l2_idx], NewPfn);
        if (MiArm64PfnDatabaseReady)
        {
            PFN_NUMBER L2Pfn = (l1[l1_idx] & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
            volatile UINT64 *L2Entry = &l2[l2_idx];

            MiInitializePfnForOtherProcess(NewPfn,
                                           (PVOID)L2Entry,
                                           L2Pfn);
        }
    }
    else if ((l2[l2_idx] & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
    {
        PFN_NUMBER L1Pfn = (l0[l0_idx] & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
        MiArm64SplitL2BlockToL3(&l2[l2_idx], L1Pfn);
    }

    volatile UINT64 *l3 = (volatile UINT64 *)MiArm64PhysToKseg0(l2[l2_idx] & ARM64_PTE_ADDR_MASK);
    ULONG l3_idx = MiAddressToPteOffset(TableVa);

    UINT64 Desc = ((UINT64)Pfn << PAGE_SHIFT) |
                  ARM64_PTE_TYPE_PAGE |
                  ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << ARM64_PTE_CACHE_SHIFT) |
                  ARM64_PTE_SH_INNER |
                  ARM64_PTE_AF |
                  ARM64_PTE_PXN |
                  ARM64_PTE_UXN;

    if (l3[l3_idx] == Desc)
        return;

    l3[l3_idx] = Desc;
    MiArm64CleanEntryToPoC(&l3[l3_idx]);
    MiArm64FlushTranslationChanges();
}

static
BOOLEAN
MiArm64InitializeKernelSelfMap(VOID)
{
    UINT64 Ttbr1;
    UINT64 RootPa;
    PFN_NUMBER RootPfn;
    volatile UINT64 *RootL0;
    UINT64 SelfEntry;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    if ((RootPa == 0) || ((RootPa & (PAGE_SIZE - 1)) != 0))
    {
        return FALSE;
    }

    RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
    /*
     * A Windows-style ARM64 loader owns the TTBR1 hierarchy at handoff. The
     * kernel validates and adopts the recursive slot instead of rebuilding it:
     * touching PXE_BASE is only safe when L0[457] already points back to TTBR1.
     */
    RootL0 = (volatile UINT64 *)PXE_BASE;
    SelfEntry = RootL0[PXE_SELFMAP_INDEX];

    if (!MiArm64SelfMapEntryMatchesRoot(SelfEntry, RootPfn))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
MiArm64CanTouchSystemPageTables(VOID)
{
    if (MiArm64SelfMapProbe != -1)
    {
        return MiArm64SelfMapProbe != 0;
    }

    UINT64 Ttbr1;
    UINT64 RootPa;
    PFN_NUMBER RootPfn;
    volatile UINT64 *RootL0;
    _SEH2_TRY
    {
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        MiArm64SelfMapProbe = 0;
        _SEH2_YIELD(EXCEPTION_EXECUTE_HANDLER);
    }
    _SEH2_END;

    BOOLEAN Faulted = FALSE;
    UINT64 Current = 0;

    _SEH2_TRY
    {
        if (!MiArm64InitializeKernelSelfMap())
        {
            Faulted = TRUE;
        }
        else
        {
            RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
            RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
            RootL0 = (volatile UINT64 *)PXE_BASE;
            Current = RootL0[PXE_SELFMAP_INDEX];

            if (((Current & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT) != RootPfn)
            {
                Faulted = TRUE;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Faulted = TRUE;
    }
    _SEH2_END;

    if (Faulted)
    {
        MiArm64SelfMapProbe = 0;
        MiArm64SelfMapReady = FALSE;
        return FALSE;
    }

    MiArm64SelfMapProbe = 1;
    MiArm64SelfMapReady = TRUE;

#if DBG
    {
        RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
        RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
        RootL0 = (volatile UINT64 *)PXE_BASE;

        /* 1. Verify TTBR1 L0[457] is self-referential. */
        UINT64 SelfEntry = RootL0[PXE_SELFMAP_INDEX];
        PFN_NUMBER SelfPfn = (PFN_NUMBER)((SelfEntry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
        if (SelfPfn != RootPfn)
        {
            ASSERT(SelfPfn == RootPfn);
        }

        /* 2. Verify PXE_BASE reads the same root page through recursion. */
        PMMPTE PxeEntry = MiAddressToPxe((PVOID)PTE_BASE);
        PFN_NUMBER PxePfn = PFN_FROM_PTE(PxeEntry);
        if (PxePfn != RootPfn)
        {
            ASSERT(PxePfn == RootPfn);
        }

        /* 3. Verify kernel PXE self-map agrees with the real TTBR1 root. */
        {
            PMMPTE KernPxe = MiAddressToPxe((PVOID)KSEG0_BASE);
            ULONG KernL0Idx = ((ULONG_PTR)KSEG0_BASE >> PXI_SHIFT) & PXI_MASK;
            UINT64 DirectEntry = RootL0[KernL0Idx];
            if (KernPxe->u.Long != DirectEntry)
            {
                ASSERT(KernPxe->u.Long == DirectEntry);
            }
        }
    }
#endif /* DBG */

    return TRUE;
}

static __inline PVOID
MiArm64CanonicalVaFromIndexes(
    _In_ ULONG L0Index,
    _In_ ULONG L1Index,
    _In_ ULONG L2Index,
    _In_ ULONG L3Index)
{
    ULONG_PTR Va;

    Va = ((ULONG_PTR)L0Index << PXI_SHIFT) |
         ((ULONG_PTR)L1Index << PPI_SHIFT) |
         ((ULONG_PTR)L2Index << PDI_SHIFT) |
         ((ULONG_PTR)L3Index << PTI_SHIFT);

    if (Va & (1ULL << 47))
    {
        Va |= 0xFFFF000000000000ULL;
    }

    return (PVOID)Va;
}

static
VOID
MiArm64SeedAccessFlagsForKernelTables(VOID)
{
    static BOOLEAN Seeded = FALSE;
    volatile UINT64 *L0;
    ULONG SelfIndex;
    ULONG Updated = 0;

    if (Seeded)
        return;

    L0 = (volatile UINT64 *)PXE_BASE;
    SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);

    for (ULONG L0Index = 256; L0Index < 512; ++L0Index)
    {
        UINT64 E0 = L0[L0Index];
        UINT64 Type0 = E0 & ARM64_PTE_TYPE_MASK;

        if (L0Index == SelfIndex)
            continue;

        if (Type0 == ARM64_PTE_TYPE_BLOCK)
        {
            if ((E0 & ARM64_PTE_AF) == 0)
            {
                L0[L0Index] = E0 | ARM64_PTE_AF;
                Updated++;
            }
            continue;
        }

        if (Type0 != ARM64_PTE_TYPE_TABLE)
            continue;

        for (ULONG L1Index = 0; L1Index < 512; ++L1Index)
        {
            PVOID Va1 = MiArm64CanonicalVaFromIndexes(L0Index, L1Index, 0, 0);
            volatile UINT64 *L1Entry = (volatile UINT64 *)MiAddressToPpe(Va1);
            UINT64 E1 = *L1Entry;
            UINT64 Type1 = E1 & ARM64_PTE_TYPE_MASK;

            if (Type1 == ARM64_PTE_TYPE_BLOCK)
            {
                if ((E1 & ARM64_PTE_AF) == 0)
                {
                    *L1Entry = E1 | ARM64_PTE_AF;
                    Updated++;
                }
                continue;
            }

            if (Type1 != ARM64_PTE_TYPE_TABLE)
                continue;

            for (ULONG L2Index = 0; L2Index < 512; ++L2Index)
            {
                PVOID Va2 = MiArm64CanonicalVaFromIndexes(L0Index, L1Index, L2Index, 0);
                volatile UINT64 *L2Entry = (volatile UINT64 *)MiAddressToPde(Va2);
                UINT64 E2 = *L2Entry;
                UINT64 Type2 = E2 & ARM64_PTE_TYPE_MASK;

                if (Type2 == ARM64_PTE_TYPE_BLOCK)
                {
                    if ((E2 & ARM64_PTE_AF) == 0)
                    {
                        *L2Entry = E2 | ARM64_PTE_AF;
                        Updated++;
                    }
                    continue;
                }

                if (Type2 != ARM64_PTE_TYPE_TABLE)
                    continue;

                for (ULONG L3Index = 0; L3Index < 512; ++L3Index)
                {
                    PVOID Va3 = MiArm64CanonicalVaFromIndexes(L0Index, L1Index, L2Index, L3Index);
                    volatile UINT64 *L3Entry = (volatile UINT64 *)MiAddressToPte(Va3);
                    UINT64 E3 = *L3Entry;
                    if ((E3 & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_PAGE)
                        continue;

                    if ((E3 & ARM64_PTE_AF) == 0)
                    {
                        *L3Entry = E3 | ARM64_PTE_AF;
                        Updated++;
                    }
                }
            }
        }
    }

    if (Updated)
    {
        MiArm64FlushTranslationChanges();
    }

    Seeded = TRUE;
}

PVOID MiSessionViewEnd;

static
CODE_SEG("INIT")
VOID
MiArm64RegisterPageTablePfn(
    _In_ PFN_NUMBER PageTablePfn,
    _In_ PVOID PteAddress,
    _In_ PFN_NUMBER PteFrame)
{
    KIRQL OldIrql;
    BOOLEAN UsePfnLock;
    PMMPFN PfnEntry;
    PMMPFN ParentPfn;

    if (PageTablePfn > MmHighestPhysicalPage)
    {
        return;
    }

    UsePfnLock = MiArm64PfnDatabaseReady || MiArm64PfnFreeListsReady;
    if (UsePfnLock)
    {
        OldIrql = MiAcquirePfnLock();
    }

    PfnEntry = MI_PFN_ELEMENT(PageTablePfn);
    if (PfnEntry->u3.e1.PageLocation == ActiveAndValid)
    {
        if (UsePfnLock) MiReleasePfnLock(OldIrql);
        return;
    }

    if ((PfnEntry->u3.e1.PageLocation == FreePageList) ||
        (PfnEntry->u3.e1.PageLocation == ZeroedPageList))
    {
        ASSERT(PfnEntry->u3.e2.ReferenceCount == 0);

        if (MiArm64PfnFreeListsReady)
        {
            MiUnlinkFreeOrZeroedPage(PfnEntry);
            PfnEntry = MI_PFN_ELEMENT(PageTablePfn);
        }
    }
    else if (PfnEntry->u3.e2.ReferenceCount != 0)
    {
        ASSERT(FALSE);
        if (UsePfnLock) MiReleasePfnLock(OldIrql);
        return;
    }

    PfnEntry->PteAddress = PteAddress;
    MI_MAKE_SOFTWARE_PTE(&PfnEntry->OriginalPte, MM_READWRITE);
    PfnEntry->u1.Flink = 0;
    PfnEntry->u2.ShareCount = 1;
    PfnEntry->u3.e2.ReferenceCount = 1;
    PfnEntry->u3.e1.PageLocation = ActiveAndValid;
    PfnEntry->u3.e1.Modified = TRUE;
    PfnEntry->u4.EntireFrame = 0;
    PfnEntry->u4.PteFrame = PteFrame;

    if (PteFrame != 0)
    {
        if (PteFrame <= MmHighestPhysicalPage)
        {
            ParentPfn = MI_PFN_ELEMENT(PteFrame);
            ParentPfn->u2.ShareCount++;
        }
    }

    if (UsePfnLock) MiReleasePfnLock(OldIrql);
}

static
CODE_SEG("INIT")
VOID
MiArm64RegisterFreeLdrPageTables(VOID)
{
    UINT64 Ttbr1;
    UINT64 RootPa;
    PFN_NUMBER RootPfn;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);

    MiArm64RegisterPageTablePfn(RootPfn, (PVOID)(ULONG_PTR)RootPa, 0);

    for (ULONG L0Index = 256; L0Index < 512; L0Index++)
    {
        PVOID L0Va = MiArm64CanonicalVaFromIndexes(L0Index, 0, 0, 0);
        PMMPTE PointerPxe = MiAddressToPxe(L0Va);
        UINT64 L0Entry = PointerPxe->u.Long;
        UINT64 L1TablePa;
        PFN_NUMBER L1Pfn;

        if ((L0Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
            continue;

        L1TablePa = L0Entry & ARM64_PTE_ADDR_MASK;
        L1Pfn = (PFN_NUMBER)(L1TablePa >> PAGE_SHIFT);
        MiArm64RegisterPageTablePfn(L1Pfn, PointerPxe, RootPfn);

        for (ULONG L1Index = 0; L1Index < 512; L1Index++)
        {
            PVOID L1Va = MiArm64CanonicalVaFromIndexes(L0Index, L1Index, 0, 0);
            PMMPTE PointerPpe = MiAddressToPpe(L1Va);
            UINT64 L1Entry = PointerPpe->u.Long;
            UINT64 L2TablePa;
            PFN_NUMBER L2Pfn;

            if ((L1Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
                continue;

            L2TablePa = L1Entry & ARM64_PTE_ADDR_MASK;
            L2Pfn = (PFN_NUMBER)(L2TablePa >> PAGE_SHIFT);
            MiArm64RegisterPageTablePfn(L2Pfn, PointerPpe, L1Pfn);

            for (ULONG L2Index = 0; L2Index < 512; L2Index++)
            {
                PVOID L2Va = MiArm64CanonicalVaFromIndexes(L0Index, L1Index, L2Index, 0);
                PMMPDE PointerPde = MiAddressToPde(L2Va);
                UINT64 L2Entry = PointerPde->u.Long;
                UINT64 L3TablePa;
                PFN_NUMBER L3Pfn;

                if ((L2Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
                    continue;

                L3TablePa = L2Entry & ARM64_PTE_ADDR_MASK;
                L3Pfn = (PFN_NUMBER)(L3TablePa >> PAGE_SHIFT);
                MiArm64RegisterPageTablePfn(L3Pfn, (PMMPTE)PointerPde, L2Pfn);
            }
        }
    }
}

/* Win10 19H1+ contract: map a writable alias of the KUSER_SHARED_DATA page in system PTE space */
CODE_SEG("INIT")
static
VOID
MiInitializeSharedUserData(VOID)
{
    PMMPTE CanonicalPte;
    PMMPTE AliasPte;
    MMPTE TempPte;
    PFN_NUMBER PageFrameIndex;

    CanonicalPte = MiAddressToPte((PVOID)KI_USER_SHARED_DATA);
    ASSERT(CanonicalPte->u.Hard.Valid == 1);
    PageFrameIndex = PFN_FROM_PTE(CanonicalPte);

    AliasPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (AliasPte == NULL)
    {
        return;
    }

    TempPte = ValidKernelPte;
    TempPte.u.Long |= PTE_NOEXECUTE;
    TempPte.u.Hard.PageFrameNumber = PageFrameIndex;
    /* MI_WRITE_VALID_PTE publishes with dsb ishst, so the alias walks before anyone writes through it */
    MI_WRITE_VALID_PTE(AliasPte, TempPte);
    MmWriteableSharedUserData = (PKUSER_SHARED_DATA)MiPteToAddress(AliasPte);
}

/* Win10 19H1+ contract: the canonical KUSER_SHARED_DATA kernel VA is read-only at EL1 */
CODE_SEG("INIT")
static
VOID
MiProtectSharedUserPage(VOID)
{
    PMMPTE CanonicalPte;
    MMPTE TempPte;

    /* Keep the canonical VA writable if the alias could not be created */
    if (MmWriteableSharedUserData == SharedUserData)
    {
        return;
    }

    CanonicalPte = MiAddressToPte((PVOID)KI_USER_SHARED_DATA);
    TempPte = *CanonicalPte;
    ASSERT(TempPte.u.Hard.Valid == 1);
    TempPte.u.Hard.NotDirty = 1;
    TempPte.u.Hard.Dbm = 0;
    TempPte.u.Hard.Writable = 0;
    /* Permission tightening only, so the ARM ARM allows skipping break-before-make */
    CanonicalPte->u.Long = TempPte.u.Long;
    MiArm64CleanEntryToPoC((volatile UINT64 *)CanonicalPte);
    /* Same last-level all-ASID sequence the user paths use; VA-based, so it
     * applies to the kernel canonical VA as well. */
    MiArm64InvalidateUserAddress((PVOID)KI_USER_SHARED_DATA);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
MiInitMachineDependent(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    extern MMPTE ValidKernelPte;
    extern MMPTE ValidKernelPteLocal;
    extern MMPDE ValidKernelPde;
    extern MMPDE ValidKernelPdeLocal;

#define MI_ARM64_SET_TEMPLATE_NORMAL_WB(Template)                                      \
    do                                                                                 \
    {                                                                                  \
        (Template).u.Long &= ~((ULONGLONG)ARM64_PTE_CACHE_MASK);                       \
        (Template).u.Long |= ((ULONGLONG)MI_ARM64_MAIR_NORMAL_WB_IDX << ARM64_PTE_CACHE_SHIFT); \
        (Template).u.Long &= ~(3ULL << 8);                                             \
        (Template).u.Long |= (3ULL << 8);                                              \
    } while (0)

    MI_ARM64_SET_TEMPLATE_NORMAL_WB(ValidKernelPte);
    MI_ARM64_SET_TEMPLATE_NORMAL_WB(ValidKernelPteLocal);
    MI_ARM64_SET_TEMPLATE_NORMAL_WB(ValidKernelPde);
    MI_ARM64_SET_TEMPLATE_NORMAL_WB(ValidKernelPdeLocal);
#undef MI_ARM64_SET_TEMPLATE_NORMAL_WB

    {
        UINT64 MairVal;
        UINT64 Slot;

        __asm__ __volatile__("mrs %0, mair_el1" : "=r"(MairVal));
        Slot = (MairVal >> (MI_ARM64_MAIR_NORMAL_WB_IDX * 8)) & 0xFF;
        if (Slot != 0xFF)
        {
            KeBugCheckEx(MEMORY_MANAGEMENT,
                         0x4D414952,
                         (ULONG_PTR)MairVal,
                         (ULONG_PTR)Slot,
                         MI_ARM64_MAIR_NORMAL_WB_IDX);
        }
    }

    if (MmSecondaryColors == 0)
    {
        MmSecondaryColors = MI_SECONDARY_COLORS;
        MmSecondaryColorMask = MmSecondaryColors - 1;
    }

    if (MmNonPagedSystemStart == NULL)
    {
        MmNonPagedSystemStart = (PVOID)MI_SYSTEM_SPACE_START;
    }

    if (MmHyperSpaceEnd == NULL)
    {
        MmHyperSpaceEnd = (PVOID)HYPER_SPACE_END;
    }

    if (MmSystemCacheStart == NULL)
    {
        MmSystemCacheStart = MiSystemVaRegions[AssignedRegionSystemCache].BaseAddress;
    }

    if (MmSystemCacheEnd == NULL)
    {
        MmSystemCacheEnd = Add2Ptr(MmSystemCacheStart, MiSystemVaRegions[AssignedRegionSystemCache].NumberOfBytes - 1);
    }

    if (MmNonPagedPoolEnd == NULL)
    {
        MmNonPagedPoolEnd = (PVOID)MI_NONPAGED_POOL_END;
    }

    if (MmNonPagedPoolStart == NULL)
    {
        MmNonPagedPoolStart = MiSystemVaRegions[AssignedRegionNonPagedPool].BaseAddress;
    }

    if (MmNonPagedPoolExpansionStart == NULL)
    {
        MmNonPagedPoolExpansionStart = MmNonPagedPoolEnd;
    }

    if (MmPagedPoolStart == NULL)
    {
        MmPagedPoolStart = (PVOID)MI_PAGED_POOL_START;
    }

    if (MmPagedPoolEnd == NULL)
    {
        MmPagedPoolEnd = MmPagedPoolStart;
    }

    if (MmSizeOfPagedPoolInBytes == 0)
    {
        MmSizeOfPagedPoolInBytes = MI_MIN_INIT_PAGED_POOLSIZE;
    }

    if (MmWorkingSetList == NULL)
    {
        MmWorkingSetList = (PMMWSL)MI_WORKING_SET_LIST;
    }

    if (MmPfnDatabase == NULL)
    {
        MmPfnDatabase = (PMMPFN)MiSystemVaRegions[AssignedRegionPfnDatabase].BaseAddress;
    }

    if (!MiArm64CanTouchSystemPageTables())
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x534D4150, 0, 0, 0);
    }

    {
        {
            UINT64 Ttbr1;
            UINT64 RootPa;

            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
            RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
            if (RootPa == 0)
            {
                KeBugCheckEx(INSTALL_MORE_MEMORY,
                             MmNumberOfPhysicalPages,
                             MmLowestPhysicalPage,
                             MmHighestPhysicalPage,
                             0);
            }

            {
                PKIPCR CurrentPcr = KeGetPcr();
                PKTHREAD CurrentThread = KeGetCurrentThread();
                PEPROCESS CurrentProcess;
                volatile UINT64 *RootL0;
                ULONG Index;

                if (CurrentThread == NULL)
                {
                    CurrentThread = KeArm64CurrentThread;
                }

                if ((CurrentThread == NULL) &&
                    (LoaderBlock != NULL) &&
                    (LoaderBlock->Thread != 0))
                {
                    CurrentThread = (PKTHREAD)(ULONG_PTR)LoaderBlock->Thread;
                }

                if (CurrentThread != NULL)
                {
                    if (KeArm64CurrentThread == NULL)
                    {
                        KeArm64CurrentThread = CurrentThread;
                    }

                    if (CurrentPcr != NULL)
                    {
                        CurrentPcr->Prcb.CurrentThread = CurrentThread;
                        if (CurrentPcr->Prcb.IdleThread == NULL)
                        {
                            CurrentPcr->Prcb.IdleThread = CurrentThread;
                        }
                    }

                    if ((KeArm64CurrentPcr != NULL) &&
                        (KeArm64CurrentPcr != CurrentPcr))
                    {
                        KeArm64CurrentPcr->Prcb.CurrentThread = CurrentThread;
                        if (KeArm64CurrentPcr->Prcb.IdleThread == NULL)
                        {
                            KeArm64CurrentPcr->Prcb.IdleThread = CurrentThread;
                        }
                    }

                    if ((CurrentThread->ApcState.Process == NULL) &&
                        (LoaderBlock != NULL) &&
                        (LoaderBlock->Process != 0))
                    {
                        CurrentProcess = (PEPROCESS)(ULONG_PTR)LoaderBlock->Process;
                        CurrentThread->ApcState.Process = (PKPROCESS)CurrentProcess;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
                        ((PETHREAD)CurrentThread)->Tcb.Process = (PKPROCESS)CurrentProcess;
#else
                        ((PETHREAD)CurrentThread)->ThreadsProcess = CurrentProcess;
#endif
                    }
                }

                if ((CurrentThread == NULL) ||
                    (CurrentThread->ApcState.Process == NULL))
                {
                    KeBugCheckEx(PROCESS_INITIALIZATION_FAILED,
                                 (ULONG_PTR)CurrentThread,
                                 (ULONG_PTR)KeArm64CurrentThread,
                                 (ULONG_PTR)LoaderBlock->Thread,
                                 (ULONG_PTR)RootPa);
                }

                CurrentProcess = (PEPROCESS)CurrentThread->ApcState.Process;
                /*
                 * Preserve the loader-installed TTBR1 root and recursive slot,
                 * but do not keep the firmware/loader TTBR0 identity root live.
                 * ARM3's current-process bookkeeping below records RootPa in
                 * DTB0, so hardware TTBR0 must be synchronized to that root
                 * before early user allocations start faulting in pages.
                 */
                RootL0 = (volatile UINT64 *)PXE_BASE;
                for (Index = 0; Index < (PXE_PER_PAGE / 2); Index++)
                {
                    RootL0[Index] = 0;
                }
                MiArm64CleanPageToPoC((PVOID)RootL0);

                CurrentProcess->Pcb.DirectoryTableBase[0] = (ULONG_PTR)RootPa;
                CurrentProcess->Pcb.Unused0 = (ULONG_PTR)RootPa;   /* hyperspace slot (0x030 is the Win11 ASID field) */

                if ((PsIdleProcess != NULL) &&
                    (PsIdleProcess != CurrentProcess))
                {
                    PsIdleProcess->Pcb.DirectoryTableBase[0] = (ULONG_PTR)RootPa;
                    PsIdleProcess->Pcb.Unused0 = (ULONG_PTR)RootPa;
                }

                /*
                 * The boot stack, PCR and early device MMIO ranges must be
                 * reachable through the process root before TTBR0 is
                 * synchronized to it. An interrupt or exception can arrive
                 * immediately after the switch and its vector prologue needs
                 * stack and device access before C code can recover.
                 */
                MiArm64MapLoaderProcessorState(LoaderBlock);

                __asm__ __volatile__("dsb ishst" ::: "memory");
                __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(RootPa) : "memory");
                __asm__ __volatile__("isb" ::: "memory");
                __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }

        }
        MiArm64SeedAccessFlagsForKernelTables();

        /*
         * FreeLDR owns the boot PCR and early stacks. They stay live after
         * ARM3 takes over TTBR1, so seed explicit KSEG0 identity mappings
         * instead of depending on loader block mappings that can disappear.
         */
        /* Self-map validity was already bugcheck-verified by
         * MiArm64CanTouchSystemPageTables() earlier in this function */
        MiArm64MapLoaderPhysicalMemory(LoaderBlock);

        {
            PVOID PfnDbStart = (PVOID)MmPfnDatabase;
            PVOID PfnDbEnd = (PVOID)((PUCHAR)MmPfnDatabase + (MxPfnAllocation * PAGE_SIZE) - 1);
            MiMapPDEs(PfnDbStart, PfnDbEnd);
        }

        MiMapPfnDatabase(LoaderBlock);

        MiInitializeColorTables();
        MiArm64InitializeSystemPageDirectoryLock();
        MiBuildNonPagedPool();

        ExpArm64PoolBootstrapMode = TRUE;
        KeInitializeSpinLock(&MmNonPagedPoolLock);
        InitializePool(NonPagedPool, 0);
        KeInitializeSpinLock(&NonPagedPoolLock);
        ExpArm64PoolBootstrapMode = FALSE;

        MiBuildSystemPteSpace();

        MiMapPDEs((PVOID)MI_MAPPING_RANGE_START, (PVOID)MI_MAPPING_RANGE_END);
        MmFirstReservedMappingPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_START);
        MmLastReservedMappingPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_END);

        MmFirstReservedMappingPte->u.Hard.PageFrameNumber = MI_HYPERSPACE_PTES - 1;

        {
            volatile UINT64 *RootL0;
            UINT64 HyperL0Entry;
            ULONG HyperIndex;

            RootL0 = (volatile UINT64 *)PXE_BASE;
            HyperIndex = MiAddressToPxi((PVOID)HYPER_SPACE);
            HyperL0Entry = RootL0[HyperIndex];

            if (HyperL0Entry & 1ULL)
            {
                ULONG_PTR HyperL1Pa = (ULONG_PTR)(HyperL0Entry & ARM64_PTE_ADDR_MASK);

                PsGetCurrentProcess()->Pcb.Unused0 = HyperL1Pa;   /* hyperspace slot (0x030 is the Win11 ASID field) */

                if (PsIdleProcess != NULL &&
                    PsIdleProcess != PsGetCurrentProcess())
                {
                    PsIdleProcess->Pcb.Unused0 = HyperL1Pa;
                }
            }
            else
            {
                KeBugCheckEx(MEMORY_MANAGEMENT,
                             0x48595045,
                             HyperIndex,
                             (ULONG_PTR)HyperL0Entry,
                             0);
            }
        }

        MiArm64RegisterFreeLdrPageTables();

        MiInitializePfnDatabase(LoaderBlock);

        MmInitializeBalancer((ULONG)MmAvailablePages, 0);

        MiArm64PfnFreeListsReady = TRUE;
        MiArm64PfnDatabaseReady = TRUE;

        /* Session space page-directory pages are now backed on demand at
         * session creation (MiEnsureSessionPageTablesBacked) instead of being
         * eagerly premapped here. */

        MiArm64FlushTranslationChanges();

        MiInitializeSharedUserData();
        MiProtectSharedUserPage();
    }

    if (MmSystemPtesStart[SystemPteSpace] == NULL)
    {
        MmSystemPtesStart[SystemPteSpace] = MiAddressToPte(KSEG0_BASE);
        MmSystemPtesEnd[SystemPteSpace] = MmSystemPtesStart[SystemPteSpace];
    }

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
MiInitializeSessionSpaceLayout(VOID)
{
    MmSessionSize = MI_SESSION_SIZE;
    MiSessionSpaceEnd = (PVOID)MI_SESSION_SPACE_END;

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

    MmSystemViewSize = MI_SYSTEM_VIEW_SIZE;
    MiSystemViewStart = (PUCHAR)MmSessionBase - MmSystemViewSize;
    ASSERT(IS_PAGE_ALIGNED(MiSystemViewStart));

    ASSERT(Add2Ptr(MmSessionBase, MmSessionSize) == MiSessionSpaceEnd);
    ASSERT(MiSessionViewEnd <= MiSessionImageStart);
    ASSERT(MmSessionBase <= MiSessionPoolStart);

    MiSessionImagePteStart = MiAddressToPte(MiSessionImageStart);
    MiSessionImagePteEnd = MiAddressToPte(MiSessionImageEnd);
    MiSessionBasePte = MiAddressToPte(MmSessionBase);
    MiSessionLastPte = MiAddressToPte(MiSessionSpaceEnd);

    MmSessionSpace = (PMM_SESSION_SPACE)Add2Ptr(MiSessionImageStart, 0x10000);
}
static VOID
MiMapPPEPages(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPDE PointerPpe;
    PMMPDE BasePpe;
    PMMPDE EndPpe;

    UINT64 Ttbr1;

    /* TTBR1 cannot change mid-loop; read it once for PFN parentage. */
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    BasePpe = MiAddressToPpe(StartAddress);
    EndPpe = MiAddressToPpe(EndAddress);

    for (PointerPpe = BasePpe;
         PointerPpe <= EndPpe;
         PointerPpe++)
    {
        PVOID TargetVa = MiPpeToAddress(PointerPpe);

        volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(TargetVa, 1);

        if (!EntryPhys)
        {
            volatile UINT64 *L0Entry = MiArm64LookupTableEntry(TargetVa, 0);
            if (L0Entry && ((*L0Entry & 1ULL) == 0))
            {
                PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                UINT64 Desc = ARM64_MAKE_TABLE_DESCRIPTOR(Pfn);
                MiArm64PublishAndZeroTableDesc(L0Entry, Pfn, MiAddressToPpe(TargetVa));
                MiArm64SyncL0ToRoot(MiAddressToPxi(TargetVa), Desc);

                if (MiArm64PfnDatabaseReady)
                {
                    UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
                    PFN_NUMBER L0Pfn = RootPa >> PAGE_SHIFT;
                    MiInitializePfnForOtherProcess(Pfn,
                                                   (PVOID)L0Entry,
                                                   L0Pfn);
                }
            }
        }
    }
}

static VOID
MiMapPPEs(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPDE PointerPpe;
    PMMPDE BasePpe;
    PMMPDE EndPpe;

    MiMapPPEPages(StartAddress, EndAddress);

    BasePpe = MiAddressToPpe(StartAddress);
    EndPpe = MiAddressToPpe(EndAddress);

    for (PointerPpe = BasePpe;
         PointerPpe <= EndPpe;
         PointerPpe++)
    {
        PVOID TargetVa = MiPpeToAddress(PointerPpe);

        volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(TargetVa, 1);

        if (!EntryPhys)
        {
            continue;
        }

        UINT64 Entry = *EntryPhys;
        if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
        {
            PFN_NUMBER L0Pfn = 0;
            volatile UINT64 *L0Entry = MiArm64LookupTableEntry(TargetVa, 0);
            if (L0Entry && (*L0Entry & 1ULL))
            {
                L0Pfn = (*L0Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
            }

            MiArm64SplitL1BlockToL2(EntryPhys, L0Pfn);
            Entry = *EntryPhys;
        }
        if ((Entry & 1ULL) == 0)
        {
            PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
            MiArm64PublishAndZeroTableDesc(EntryPhys, Pfn, MiAddressToPde(TargetVa));

            if (MiArm64PfnDatabaseReady)
            {
                volatile UINT64 *L0Entry = MiArm64LookupTableEntry(TargetVa, 0);
                PFN_NUMBER L1Pfn = 0;
                if (L0Entry && (*L0Entry & 1ULL))
                {
                    L1Pfn = (*L0Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
                }
                MiInitializePfnForOtherProcess(Pfn,
                                               (PVOID)EntryPhys,
                                               L1Pfn);
            }

        }
    }
}

static VOID
MiMapPDEs(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPDE PointerPde;
    PMMPDE BasePde;
    PMMPDE EndPde;
    BOOLEAN PerformedPdeMappings = FALSE;

    /* MiMapPPEs owns the L0/L1 hierarchy (including L1 block splits); this
     * function only ensures the L2 page-directory entries. */
    MiMapPPEs(StartAddress, EndAddress);

    BasePde = MiAddressToPde(StartAddress);
    EndPde = MiAddressToPde(EndAddress);

    for (PointerPde = BasePde;
         PointerPde <= EndPde;
         PointerPde++)
    {
        PVOID TargetVa = MiPdeToAddress(PointerPde);
        volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(TargetVa, 2);

        if (!EntryPhys)
        {
            continue;
        }

        {
            UINT64 Entry = *EntryPhys;
            if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
            {
                PFN_NUMBER L1Pfn = 0;
                volatile UINT64 *PpeEntry = MiArm64LookupTableEntry(TargetVa, 1);
                if (PpeEntry && (*PpeEntry & 1ULL))
                {
                    L1Pfn = (*PpeEntry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
                }

                MiArm64SplitL2BlockToL3(EntryPhys, L1Pfn);
                Entry = *EntryPhys;
            }
            if ((Entry & 1ULL) == 0)
            {
                PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                MiArm64PublishAndZeroTableDesc(EntryPhys, Pfn, MiAddressToPte(TargetVa));

                if (MiArm64PfnDatabaseReady)
                {
                    volatile UINT64 *PpeEntry = MiArm64LookupTableEntry(TargetVa, 1);
                    PFN_NUMBER L2Pfn = 0;
                    if (PpeEntry && (*PpeEntry & 1ULL))
                    {
                        L2Pfn = (*PpeEntry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
                    }

                    MiInitializePfnForOtherProcess(Pfn,
                                                   (PVOID)EntryPhys,
                                                   L2Pfn);
                }

                PerformedPdeMappings = TRUE;
            }
        }
    }

    if (PerformedPdeMappings)
    {
        MiArm64FlushTranslationChanges();
    }
}

VOID
MiMapPTEs(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPTE PointerPte;
    MMPTE TmplPte = ValidKernelPte;
    PMMPTE BasePte;
    PMMPTE EndPte;
    BOOLEAN PerformedMappings = FALSE;

    TmplPte.u.Hard.PrivilegedNoExecute = 1;
    TmplPte.u.Hard.UserNoExecute = 1;

    BasePte = MiAddressToPte(StartAddress);
    EndPte = MiAddressToPte(EndAddress);

    for (PointerPte = BasePte;
         PointerPte <= EndPte;
         PointerPte++)
    {
        if (!PointerPte->u.Hard.Valid)
        {
            PFN_NUMBER PageFrameNumber;
            PMMPDE PointerPde;
            PMMPFN Pfn;

            PageFrameNumber = MiArm64AllocatePageTablePage();
            TmplPte.u.Hard.PageFrameNumber = PageFrameNumber;
            *PointerPte = TmplPte;

            Pfn = MiGetPfnEntry(PageFrameNumber);
            ASSERT(Pfn != NULL);
            ASSERT(Pfn->u3.e2.ReferenceCount == 0);

            PointerPde = MiAddressToPde(MiPteToAddress(PointerPte));
            Pfn->u4.PteFrame = PFN_FROM_PDE(PointerPde);
            Pfn->PteAddress = PointerPte;
            Pfn->u2.ShareCount = 1;
            Pfn->u3.e2.ReferenceCount = 1;
            Pfn->u3.e1.PageLocation = ActiveAndValid;

            PerformedMappings = TRUE;
        }
    }

    if (PerformedMappings)
    {
        MiArm64FlushTranslationChanges();
    }
}

static
VOID
MiBuildNonPagedPool(VOID)
{
    PFN_NUMBER PoolSizingPages = MmNumberOfPhysicalPages;
    PVOID InitialNonPagedPoolEnd;

    if (!MxFreeDescriptor)
    {
        KeBugCheckEx(INSTALL_MORE_MEMORY,
                     MmNumberOfPhysicalPages,
                     MmLowestPhysicalPage,
                     MmHighestPhysicalPage,
                     1);
    }

    if (PoolSizingPages > MI_ARM64_BOOT_POOL_SIZING_PAGES)
    {
        PoolSizingPages = MI_ARM64_BOOT_POOL_SIZING_PAGES;
    }

    /* Check if this is a machine with less than 256MB of RAM, and no override */
    if ((PoolSizingPages <= MI_MIN_PAGES_FOR_NONPAGED_POOL_TUNING) &&
        !(MmSizeOfNonPagedPoolInBytes))
    {
        /* Force the non paged pool to be 2MB so we can reduce RAM usage */
        MmSizeOfNonPagedPoolInBytes = 2 * _1MB;
    }

    /* Check if the user gave a ridiculously large nonpaged pool RAM size */
    if ((MmSizeOfNonPagedPoolInBytes >> PAGE_SHIFT) >
        (MmNumberOfPhysicalPages * 7 / 8))
    {
        /* More than 7/8ths of RAM was dedicated to nonpaged pool, ignore! */
        MmSizeOfNonPagedPoolInBytes = 0;
    }

    /* Check if no registry setting was set, or if the setting was too low */
    if (MmSizeOfNonPagedPoolInBytes < MmMinimumNonPagedPoolSize)
    {
        SIZE_T AdditionalMb;

        /* Start with the minimum (256 KB) and add 32 KB for each MB above 4 */
        MmSizeOfNonPagedPoolInBytes = MmMinimumNonPagedPoolSize;

        if (PoolSizingPages > 1024)
        {
            /* 256 pages (4 KiB each) represent one MiB of physical memory */
            AdditionalMb = (SIZE_T)((PoolSizingPages - 1024) / 256);
            MmSizeOfNonPagedPoolInBytes += AdditionalMb *
                                           (SIZE_T)MmMinAdditionNonPagedPoolPerMb;
        }
    }

    /* Check if the registry setting or our dynamic calculation was too high */
    if (MmSizeOfNonPagedPoolInBytes > MI_MAX_INIT_NONPAGED_POOL_SIZE)
    {
        /* Set it to the maximum */
        MmSizeOfNonPagedPoolInBytes = MI_MAX_INIT_NONPAGED_POOL_SIZE;
    }

    /* Check if a percentage cap was set through the registry */
    if (MmMaximumNonPagedPoolPercent)
    {
        MmMaximumNonPagedPoolPercent = 0;
    }

    /* Page-align the nonpaged pool size */
    MmSizeOfNonPagedPoolInBytes &= ~(PAGE_SIZE - 1);

    /* Now, check if there was a registry size for the maximum size */
    if (!MmMaximumNonPagedPoolInBytes)
    {
        SIZE_T AdditionalMb;

        /* Start with the default (1MB) and add 400 KB for each MB above 4 */
        MmMaximumNonPagedPoolInBytes = MmDefaultMaximumNonPagedPool;

        if (PoolSizingPages > 1024)
        {
            /* 256 pages (4 KiB each) represent one MiB of physical memory */
            AdditionalMb = (SIZE_T)((PoolSizingPages - 1024) / 256);
            MmMaximumNonPagedPoolInBytes += AdditionalMb *
                                             (SIZE_T)MmMaxAdditionNonPagedPoolPerMb;
        }
    }

    /* Don't let the maximum go too high */
    if (MmMaximumNonPagedPoolInBytes > MiSystemVaRegions[AssignedRegionNonPagedPool].NumberOfBytes)
    {
        MmMaximumNonPagedPoolInBytes = MiSystemVaRegions[AssignedRegionNonPagedPool].NumberOfBytes;
    }

    /* Convert nonpaged pool size from bytes to pages */
    MmMaximumNonPagedPoolInPages = MmMaximumNonPagedPoolInBytes >> PAGE_SHIFT;

    /* Non paged pool starts at the assigned nonpaged pool region */
    MmNonPagedPoolStart = MiSystemVaRegions[AssignedRegionNonPagedPool].BaseAddress;


    /* Calculate the nonpaged pool expansion start region */
    MmNonPagedPoolExpansionStart = (PCHAR)MmNonPagedPoolStart +
                                          MmSizeOfNonPagedPoolInBytes;
    ASSERT(IS_PAGE_ALIGNED(MmNonPagedPoolExpansionStart));

    /* And this is where the non paged pool ends */
    MmNonPagedPoolEnd = (PCHAR)MmNonPagedPoolStart + MmMaximumNonPagedPoolInBytes;
    InitialNonPagedPoolEnd = (PCHAR)MmNonPagedPoolExpansionStart - 1;

    MiMapPDEs(MmNonPagedPoolStart, InitialNonPagedPoolEnd);

    MiMapPTEs(MmNonPagedPoolStart, InitialNonPagedPoolEnd);

    MiSystemPteMetadataSize = MiGetSystemPteMetadataSize(MI_NUMBER_SYSTEM_PTES);

    MiInitializeNonPagedPool();
    MiInitializeNonPagedPoolThresholds();
}

static
VOID
MiBuildSystemPteSpace(VOID)
{
    PMMPTE PointerPte;
    SIZE_T NonPagedSystemSize;

    /* Use the default number of system PTEs */
    MmNumberOfSystemPtes = MI_NUMBER_SYSTEM_PTES;
    NonPagedSystemSize = (MmNumberOfSystemPtes + 1) * PAGE_SIZE;

    /* Put system PTEs at the start of the assigned system PTE region */
    MiSystemPteSpaceStart = MiSystemVaRegions[AssignedRegionSystemPtes].BaseAddress;

    /* Publish the location for RosMm and the address space dump */
    MmSystemPteSpaceStart = MiSystemPteSpaceStart;

    PointerPte = MiAddressToPte(MiSystemPteSpaceStart);
    MiInitializeSystemPtes(PointerPte, MmNumberOfSystemPtes, SystemPteSpace, MiSystemPteMetadataBuffer);

    /* Reserve system PTEs for zeroing PTEs and clear them */
    MiFirstReservedZeroingPte = MiReserveSystemPtes(MI_ZERO_PTES + 1,
                                                    SystemPteSpace);
    RtlZeroMemory(MiFirstReservedZeroingPte, (MI_ZERO_PTES + 1) * sizeof(MMPTE));
    MiFirstReservedZeroingPte->u.Hard.PageFrameNumber = MI_ZERO_PTES;

    /* Allocate the debug PTE from system PTEs */
    MmDebugPte = MiReserveSystemPtes(1, SystemPteSpace);
    MiDebugMapping = MiPteToAddress(MmDebugPte);
}

