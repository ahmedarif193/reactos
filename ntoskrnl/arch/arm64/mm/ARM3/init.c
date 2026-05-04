/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/mm/ARM3/init.c
 * PURPOSE:         ARM64 memory manager initialization
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include <mm/ARM3/miarm.h>

#define IS_PAGE_ALIGNED(addr) ((((ULONG_PTR)(addr)) & (PAGE_SIZE - 1)) == 0)

/* ARM64 descriptor address mask: bits [47:12] contain the output address in 48-bit PA space.
 * Upper bits [63:48] may contain attribute bits (UXN, PXN, etc.) that must be masked out.
 * Using ~0xFFFULL is insufficient as it preserves the upper attribute bits. */
#define ARM64_PTE_ADDR_MASK     0x0000FFFFFFFFF000ULL
/* TTBR1 contains ASID in bits[63:48] when TCR.A1=1; mask to PA bits. */
#define MI_ARM64_TTBR_TO_PA(_Ttbr) ((UINT64)(_Ttbr) & ARM64_PTE_ADDR_MASK)

BOOLEAN MiArm64PfnFinalizePending = FALSE;
BOOLEAN ExpArm64PoolBootstrapMode = FALSE;
VOID MiArm64FinalizePfnDatabase(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);
static VOID MiBuildNonPagedPool(VOID);
static VOID MiBuildSystemPteSpace(VOID);
static BOOLEAN MiArm64CanTouchSystemPageTables(VOID);
static BOOLEAN MiArm64InitializeKernelSelfMap(VOID);
static __inline PVOID MiArm64PhysToKseg0(UINT64 Phys);
static __inline PVOID MiArm64PfnToKseg0(PFN_NUMBER Pfn);
static PFN_NUMBER MiArm64AllocatePageTablePage(VOID);
extern MMPTE ValidKernelPte;
extern PVOID MiSystemViewStart;
PVOID MiSystemPteSpaceStart;
PVOID MiSystemPteSpaceEnd;

static LONG MiArm64SelfMapProbe = -1;
/* Control whether MiMapPTEs zeroes newly allocated leaf pages (data pages). */
static volatile BOOLEAN MiArm64ZeroLeafPages = TRUE;

/* Tracks whether PFN database is ready for access. FALSE during early bootstrap. */
static BOOLEAN MiArm64PfnDatabaseReady = FALSE;
static BOOLEAN MiArm64PfnFreeListsReady = FALSE;

/*
 * Self-map cache to eliminate redundant L0/L1/L2 allocations.
 *
 * The self-map region spans indices [493,*,*,*] for the recursive entry.
 * We track which L0/L1/L2 entries have been created to avoid re-checking
 * and re-allocating them on every MiArm64MapPageTablePage call.
 *
 * Cache organization:
 * - L0 cache: 512 bits (one per L0 entry) = 64 bytes
 * - L1 cache: 512*512 bits (one per L0.L1 combination) = 32KB
 * - L2 cache: Would be 512*512*512 bits = 16MB, too large
 *
 * Optimization: We only cache L0 and L1 levels since:
 * - L0 has 512 entries, very small cache (64 bytes)
 * - L1 has 512*512 = 262,144 entries, manageable (32KB)
 * - L2 would require 16MB, too large for early boot
 *
 * For L2, we accept the redundant check (read existing entry) since it's
 * still much cheaper than allocating a page unnecessarily.
 */
#define MI_SELFMAP_CACHE_L0_SIZE 64   /* 512 bits / 8 = 64 bytes */
#define MI_SELFMAP_CACHE_L1_SIZE 32768 /* 512*512 bits / 8 = 32KB */

static UCHAR MiArm64SelfMapL0Cache[MI_SELFMAP_CACHE_L0_SIZE];
static UCHAR MiArm64SelfMapL1Cache[MI_SELFMAP_CACHE_L1_SIZE];
static BOOLEAN MiArm64SelfMapCacheInitialized = FALSE;

/*
 * Check if an L0 entry has been created in the self-map.
 * Returns TRUE if the entry is already marked as created in cache.
 */
static __inline BOOLEAN
MiArm64SelfMapL0Exists(ULONG L0Index)
{
    if (!MiArm64SelfMapCacheInitialized)
        return FALSE;

    ULONG ByteIndex = L0Index / 8;
    ULONG BitIndex = L0Index % 8;
    return (MiArm64SelfMapL0Cache[ByteIndex] & (1 << BitIndex)) != 0;
}

/*
 * Mark an L0 entry as created in the self-map cache.
 */
static __inline VOID
MiArm64SelfMapL0MarkCreated(ULONG L0Index)
{
    ULONG ByteIndex = L0Index / 8;
    ULONG BitIndex = L0Index % 8;
    MiArm64SelfMapL0Cache[ByteIndex] |= (1 << BitIndex);
}

/*
 * Check if an L1 entry has been created in the self-map.
 * Returns TRUE if the entry is already marked as created in cache.
 */
static __inline BOOLEAN
MiArm64SelfMapL1Exists(ULONG L0Index, ULONG L1Index)
{
    if (!MiArm64SelfMapCacheInitialized)
        return FALSE;

    ULONG LinearIndex = (L0Index * 512) + L1Index;
    ULONG ByteIndex = LinearIndex / 8;
    ULONG BitIndex = LinearIndex % 8;
    return (MiArm64SelfMapL1Cache[ByteIndex] & (1 << BitIndex)) != 0;
}

/*
 * Mark an L1 entry as created in the self-map cache.
 */
static __inline VOID
MiArm64SelfMapL1MarkCreated(ULONG L0Index, ULONG L1Index)
{
    ULONG LinearIndex = (L0Index * 512) + L1Index;
    ULONG ByteIndex = LinearIndex / 8;
    ULONG BitIndex = LinearIndex % 8;
    MiArm64SelfMapL1Cache[ByteIndex] |= (1 << BitIndex);
}

static VOID MiMapPPEs(PVOID StartAddress, PVOID EndAddress);
static VOID MiMapPDEs(PVOID StartAddress, PVOID EndAddress);
static VOID MiMapPTEs(PVOID StartAddress, PVOID EndAddress);
static VOID MiArm64MapKseg0IdentityRange(PVOID BaseAddress, SIZE_T Size);
static VOID MiArm64MapKseg0IdentityRangeWithAttr(PVOID BaseAddress, SIZE_T Size, ULONG AttrIndex);
static VOID MiArm64MapLoaderProcessorState(PLOADER_PARAMETER_BLOCK LoaderBlock);

static __inline VOID
MiArm64SyncL0ToRoot(ULONG L0Index, UINT64 Desc)
{
    UINT64 Root;
    volatile UINT64 *RootL0;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Root));

    Root = MI_ARM64_TTBR_TO_PA(Root);
    if (Root != 0)
    {
        RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(Root);
        RootL0[L0Index] = Desc;
    }

    __asm__ __volatile__("dsb ishst" ::: "memory");
}

#define ARM64_PTE_AF                (1ULL << 10)  /* Access Flag - required for L3 page entries */
#define ARM64_PTE_SH_INNER          (3ULL << 8)   /* Inner Shareable */
#define ARM64_PTE_AP_RW_EL1         (0ULL << 6)   /* EL1 R/W, EL0 no access */
#define ARM64_TCR_HA                (1ULL << 39)  /* Hardware Access Flag update */
#define ARM64_TCR_TSZ_MASK          0x3FULL
#define ARM64_TCR_T1SZ_SHIFT        16
#define ARM64_PTE_TABLE_COMPAT      (ARM64_PTE_AF | ARM64_PTE_SH_INNER)
#define ARM64_SELFMAP_ENTRY_BITS    (ARM64_PTE_TYPE_TABLE | ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2))
#define MI_ARM64_MAKE_TABLE_DESC(Pfn) (((UINT64)(Pfn) << PAGE_SHIFT) | ARM64_SELFMAP_ENTRY_BITS)

#define MI_ARM64_MAKE_SELFMAP_DESC(Pfn) \
    (((UINT64)(Pfn) << PAGE_SHIFT) | ARM64_PTE_TYPE_TABLE | ARM64_PTE_AF | ARM64_PTE_SH_INNER | ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2))

static __inline PVOID
MiArm64PhysToKseg0(UINT64 Phys)
{
    return (PVOID)(ULONG_PTR)(KSEG0_BASE | (Phys & ARM64_PTE_ADDR_MASK));
}

static __inline PVOID
MiArm64PfnToKseg0(PFN_NUMBER Pfn)
{
    return MiArm64PhysToKseg0(((UINT64)Pfn) << PAGE_SHIFT);
}

static
VOID
MiArm64MapKseg0IdentityRangeWithAttr(
    _In_ PVOID BaseAddress,
    _In_ SIZE_T Size,
    _In_ ULONG AttrIndex)
{
    ULONG_PTR StartVa, EndVa, Va;
    BOOLEAN MappedAny = FALSE;

    if ((BaseAddress == NULL) || (Size == 0))
    {
        return;
    }

    StartVa = ALIGN_DOWN_BY((ULONG_PTR)BaseAddress, PAGE_SIZE);
    EndVa = ALIGN_DOWN_BY((ULONG_PTR)BaseAddress + Size - 1, PAGE_SIZE);

    if (StartVa < (ULONG_PTR)KSEG0_BASE)
    {
        StartVa += (ULONG_PTR)KSEG0_BASE;
        EndVa += (ULONG_PTR)KSEG0_BASE;
    }

    MiMapPPEs((PVOID)StartVa, (PVOID)EndVa);
    MiMapPDEs((PVOID)StartVa, (PVOID)EndVa);

    for (Va = StartVa; Va <= EndVa; Va += PAGE_SIZE)
    {
        PMMPTE PointerPte = MiAddressToPte((PVOID)Va);
        MMPTE Pte = ValidKernelPte;
        PFN_NUMBER Pfn = (PFN_NUMBER)((Va - (ULONG_PTR)KSEG0_BASE) >> PAGE_SHIFT);

        Pte.u.Hard.PageFrameNumber = Pfn;
        MI_SET_PTE_ATTR_INDEX(&Pte, AttrIndex);

        if (PointerPte->u.Long != Pte.u.Long)
        {
            *PointerPte = Pte;
            MappedAny = TRUE;
        }

        if (Va == EndVa)
        {
            break;
        }
    }

    if (MappedAny)
    {
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
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
                                         MI_ARM64_MAIR_NORMAL_WB_IDX);
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

    if (LoaderBlock->KernelStack != 0)
    {
        MiArm64MapKseg0IdentityRange((PVOID)((ULONG_PTR)LoaderBlock->KernelStack -
                                             KERNEL_STACK_SIZE),
                                     KERNEL_STACK_SIZE);
    }

    if (Arm64Block->PanicStack != 0)
    {
        MiArm64MapKseg0IdentityRange((PVOID)((ULONG_PTR)Arm64Block->PanicStack -
                                             KERNEL_STACK_SIZE),
                                     KERNEL_STACK_SIZE);
    }

    if (Arm64Block->InterruptStack != 0)
    {
        MiArm64MapKseg0IdentityRange((PVOID)((ULONG_PTR)Arm64Block->InterruptStack -
                                             KERNEL_STACK_SIZE),
                                     KERNEL_STACK_SIZE);
    }

    if (Arm64Block->EarlyUartAddress != 0)
    {
        MiArm64MapKseg0IdentityRangeWithAttr((PVOID)(ULONG_PTR)Arm64Block->EarlyUartAddress,
                                             PAGE_SIZE,
                                             MI_ARM64_MAIR_DEVICE_nGnRnE_IDX);
    }

    DPRINT1("[arm64] Loader processor state KSEG0 mappings seeded: pcr=%p kernelStack=%p\n",
            (PVOID)(ULONG_PTR)Arm64Block->PcrPage,
            (PVOID)(ULONG_PTR)LoaderBlock->KernelStack);
}


/*
 * MiArm64InitPoolPfnEntries - Initialize PFN database entries for nonpaged pool
 *
 * Walks the hardware page tables via KSEG0 direct mapping to find all physical
 * pages mapped in the nonpaged pool VA range, then initializes their PFN entries.
 *
 * This replaces the generic MiBuildPfnDatabaseFromPages scan on ARM64, which
 * iterated 131K PDEs via MmIsAddressValid (taking ~30 seconds on 8GB systems)
 * to do useful work on only the nonpaged pool range.
 *
 * Note: Kernel image / boot driver pages are handled separately by
 * MiArm64InitLoaderMappedPfnEntries() which walks the loader module list.
 */
CODE_SEG("INIT")
VOID
MiArm64InitPoolPfnEntries(VOID)
{
    ULONG_PTR PoolStart, PoolEnd, Va;
    ULONG Initialized = 0, InvalidPtes = 0, InvalidPdes = 0, OutOfRange = 0;
    PFN_NUMBER FirstPfn = (PFN_NUMBER)-1, LastPfn = 0;

    PoolStart = (ULONG_PTR)MmNonPagedPoolStart;
    PoolEnd = PoolStart + MmSizeOfNonPagedPoolInBytes;

    for (Va = PoolStart; Va < PoolEnd; Va += PAGE_SIZE)
    {
        PMMPDE PointerPde;
        PMMPTE PointerPte;
        PFN_NUMBER DataPfn;
        PMMPFN Pfn;

        PointerPde = MiAddressToPde((PVOID)Va);
        if ((PointerPde->u.Long & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        {
            InvalidPdes++;
            continue;
        }

        PointerPte = MiAddressToPte((PVOID)Va);
        if ((PointerPte->u.Long & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_PAGE)
        {
            InvalidPtes++;
            continue;
        }

        DataPfn = PFN_FROM_PTE(PointerPte);
        if (DataPfn > MmHighestPhysicalPage)
        {
            OutOfRange++;
            continue;
        }

        Pfn = MiGetPfnEntry(DataPfn);
        RtlZeroMemory(Pfn, sizeof(*Pfn));
        Pfn->u4.PteFrame = PFN_FROM_PDE(PointerPde);
        Pfn->PteAddress = MiAddressToPte((PVOID)Va);
        Pfn->u2.ShareCount = 1;
        Pfn->u3.e2.ReferenceCount = 1;
        Pfn->u3.e1.PageLocation = ActiveAndValid;
        Pfn->u3.e1.CacheAttribute = MiNonCached;
        if (FirstPfn == (PFN_NUMBER)-1)
        {
            FirstPfn = DataPfn;
        }
        LastPfn = DataPfn;
        Initialized++;
    }

#if DBG
    DbgPrint("[arm64][MMT] MiArm64InitPoolPfnEntries: initialized=%lu invalid-pde=%lu invalid-pte=%lu out-of-range=%lu first=%I64x last=%I64x\n",
             Initialized,
             InvalidPdes,
             InvalidPtes,
             OutOfRange,
             (ULONGLONG)FirstPfn,
             (ULONGLONG)LastPfn);
#endif
}

/*
 * MiArm64InitLoaderMappedPfnEntries - Initialize PFN entries for boot-loaded modules
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
MiArm64InitLoaderMappedPfnEntries(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY ListHead, NextEntry;
    PLDR_DATA_TABLE_ENTRY DataEntry;
    ULONG TotalDataPages = 0, TotalPtPages = 0;
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
                ULONG L0Idx = (Va >> 39) & 0x1FF;
                ULONG L1Idx = (Va >> 30) & 0x1FF;
                ULONG L2Idx = (Va >> 21) & 0x1FF;
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
                            TotalPtPages++;
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
                            TotalPtPages++;
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
                            TotalPtPages++;
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
                    TotalDataPages++;
                }
            }
        }
    }

#if DBG
    DbgPrint("[arm64] MiArm64InitLoaderMappedPfnEntries: %u data pages, %u PT pages\n",
             TotalDataPages, TotalPtPages);
#endif
}

CODE_SEG("INIT")
VOID
NTAPI
MiArm64BuildPfnDatabaseFromPages(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
#if DBG
    DbgPrint("[arm64][MMT] MiArm64BuildPfnDatabaseFromPages: begin\n");
#endif
    MiArm64InitLoaderMappedPfnEntries(LoaderBlock);
#if DBG
    DbgPrint("[arm64][MMT] MiArm64BuildPfnDatabaseFromPages: end\n");
#endif
}

static __inline volatile UINT64*
MiArm64LookupTableEntry(UINT64 Ttbr1, PVOID Va, ULONG Level)
{
    volatile UINT64 *Entry;

    UNREFERENCED_PARAMETER(Ttbr1);

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
    __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
}

static __inline VOID
MiArm64PublishTableDesc(_Inout_ volatile UINT64 *Entry, _In_ PFN_NUMBER Pfn)
{
    *Entry = MI_ARM64_MAKE_TABLE_DESC(Pfn);
    __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
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
    __asm__ __volatile__("dsb ishst" ::: "memory");
}

static
PFN_NUMBER
MiArm64AllocatePageTablePage(VOID)
{
    PFN_NUMBER Pfn;
    KIRQL OldIrql;
    ULONG CpuIndex;
    BOOLEAN ReleasePfnLock;
    extern volatile LONG MiArm64PfnLockDepth[MAXIMUM_PROCESSORS];

    /*
     * Runtime self-map growth must not call MxGetNextPage() because it lives
     * in INIT text. During phase-0 machine-dependent setup, however, the PFN
     * database is mapped before the free-page lists are coherent with loader
     * descriptor consumption. Keep those early table allocations on the loader
     * descriptor path until the ARM3 free lists are explicitly ready.
     */
    if (MiArm64PfnDatabaseReady && MiArm64PfnFreeListsReady)
    {
        CpuIndex = KeGetCurrentProcessorNumber();
        ReleasePfnLock = TRUE;

        /*
         * Avoid recursive acquisition on paths that already hold MmPfnLock.
         * ARM64 queued spinlock code tracks per-CPU PFN lock depth.
         */
        if ((CpuIndex < MAXIMUM_PROCESSORS) &&
            (MiArm64PfnLockDepth[CpuIndex] > 0))
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
        return Pfn;
    }

    return MxGetNextPage(1);
}

static
BOOLEAN
MiArm64SplitL1BlockToL2(_Inout_ volatile UINT64 *Entry, _In_ PFN_NUMBER ParentPfn)
{
    UINT64 Block = *Entry;
    if ((Block & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_BLOCK)
    {
        return TRUE;
    }

    PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
    if (NewPfn == 0)
    {
        return FALSE;
    }

    volatile UINT64 *L2 = (volatile UINT64 *)MiArm64PfnToKseg0(NewPfn);
    UINT64 Base = Block & ARM64_PTE_ADDR_MASK;
    UINT64 Attrs = MiArm64BlockEntryAttrs(Block) | ARM64_PTE_AF;

    for (ULONG Index = 0; Index < 512; ++Index)
    {
        UINT64 Pa = Base + ((UINT64)Index << PDI_SHIFT);
        L2[Index] = (Pa & ARM64_PTE_ADDR_MASK) | Attrs | ARM64_PTE_TYPE_BLOCK;
    }

    __asm__ __volatile__("dsb ishst" ::: "memory");
    MiArm64BreakBeforeMake(Entry);
    MiArm64PublishTableDesc(Entry, NewPfn);

    if (MiArm64PfnDatabaseReady)
    {
        MiInitializePfnForOtherProcess(NewPfn, (PVOID)Entry, ParentPfn);
    }

    return TRUE;
}

static
BOOLEAN
MiArm64SplitL2BlockToL3(_Inout_ volatile UINT64 *Entry, _In_ PFN_NUMBER ParentPfn)
{
    UINT64 Block = *Entry;
    if ((Block & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_BLOCK)
    {
        return TRUE;
    }

    PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
    if (NewPfn == 0)
    {
        return FALSE;
    }

    volatile UINT64 *L3 = (volatile UINT64 *)MiArm64PfnToKseg0(NewPfn);
    UINT64 Base = Block & ARM64_PTE_ADDR_MASK;
    UINT64 Attrs = MiArm64BlockEntryAttrs(Block) | ARM64_PTE_AF;

    for (ULONG Index = 0; Index < 512; ++Index)
    {
        UINT64 Pa = Base + ((UINT64)Index << PAGE_SHIFT);
        L3[Index] = (Pa & ARM64_PTE_ADDR_MASK) | Attrs | ARM64_PTE_TYPE_PAGE;
    }

    __asm__ __volatile__("dsb ishst" ::: "memory");
    MiArm64BreakBeforeMake(Entry);
    MiArm64PublishTableDesc(Entry, NewPfn);

    if (MiArm64PfnDatabaseReady)
    {
        MiInitializePfnForOtherProcess(NewPfn, (PVOID)Entry, ParentPfn);
    }

    return TRUE;
}



volatile LONG MiArm64PfnLockDepth[MAXIMUM_PROCESSORS] = {0};

VOID
MiArm64MapPageTablePage(UINT64 Ttbr1, PVOID TableVa, PFN_NUMBER Pfn)
{
    UINT64 root_pa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    volatile UINT64 *l0 = (volatile UINT64 *)MiArm64PhysToKseg0(root_pa);
    ULONG l0_idx = MiAddressToPxi(TableVa);

    /*
     * OPTIMIZATION: Check cache before accessing page table hierarchy.
     * This eliminates redundant reads and allocations for already-created entries.
     */

    /* Ensure L0 entry exists - check cache first */
    if (!MiArm64SelfMapL0Exists(l0_idx))
    {
        /* Cache miss - check actual page table entry */
        if ((l0[l0_idx] & 1ULL) == 0)
        {
            PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
            if (NewPfn == 0) return;
            RtlZeroMemory(MiArm64PfnToKseg0(NewPfn), PAGE_SIZE);
            l0[l0_idx] = MI_ARM64_MAKE_TABLE_DESC(NewPfn);
            MiArm64SyncL0ToRoot(l0_idx, l0[l0_idx]);
            __asm__ __volatile__("dsb ishst" ::: "memory");
            /* Register the L1 table page once the PFN database is available. */
            if (MiArm64PfnDatabaseReady)
            {
                PFN_NUMBER L0Pfn = root_pa >> PAGE_SHIFT;
                volatile UINT64 *L0Entry = &l0[l0_idx];
                MiInitializePfnForOtherProcess(NewPfn,
                                               (PVOID)L0Entry,
                                               L0Pfn);
            }
        }
        /* Mark as created in cache (whether we just created it or found it existing) */
        MiArm64SelfMapL0MarkCreated(l0_idx);
    }


    volatile UINT64 *l1 = (volatile UINT64 *)MiArm64PhysToKseg0(l0[l0_idx] & ARM64_PTE_ADDR_MASK);
    ULONG l1_idx = (((ULONG_PTR)TableVa) >> PPI_SHIFT) & 0x1FF;

    /* Ensure L1 entry exists - check cache first */
    if (!MiArm64SelfMapL1Exists(l0_idx, l1_idx))
    {
        /* Cache miss - check actual page table entry */
        if ((l1[l1_idx] & 1ULL) == 0)
        {
            PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
            if (NewPfn == 0) return;
            RtlZeroMemory(MiArm64PfnToKseg0(NewPfn), PAGE_SIZE);
            l1[l1_idx] = MI_ARM64_MAKE_TABLE_DESC(NewPfn);
            __asm__ __volatile__("dsb ishst" ::: "memory");
            /* Register the L2 table page once the PFN database is available. */
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
            if (!MiArm64SplitL1BlockToL2(&l1[l1_idx], L0Pfn))
            {
                return;
            }
        }
        /* Mark as created in cache (whether we just created it or found it existing) */
        MiArm64SelfMapL1MarkCreated(l0_idx, l1_idx);
    }

    volatile UINT64 *l2 = (volatile UINT64 *)MiArm64PhysToKseg0(l1[l1_idx] & ARM64_PTE_ADDR_MASK);
    ULONG l2_idx = (((ULONG_PTR)TableVa) >> PDI_SHIFT) & 0x1FF;

    /*
     * L2 level: No cache (would be 16MB), but still optimize by checking before allocating.
     * This still avoids the allocation even though we must read the entry.
     */
    if ((l2[l2_idx] & 1ULL) == 0)
    {
        PFN_NUMBER NewPfn = MiArm64AllocatePageTablePage();
        if (NewPfn == 0) return;
        RtlZeroMemory(MiArm64PfnToKseg0(NewPfn), PAGE_SIZE);
        l2[l2_idx] = MI_ARM64_MAKE_TABLE_DESC(NewPfn);
        __asm__ __volatile__("dsb ishst" ::: "memory");
        /* Register the L3 table page once the PFN database is available. */
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
        if (!MiArm64SplitL2BlockToL3(&l2[l2_idx], L1Pfn))
        {
            return;
        }
    }

    volatile UINT64 *l3 = (volatile UINT64 *)MiArm64PhysToKseg0(l2[l2_idx] & ARM64_PTE_ADDR_MASK);
    ULONG l3_idx = MiAddressToPteOffset(TableVa);

    /* Create the L3 (leaf) entry for the page table page.
     * Page table pages are data (not executable), so set both PXN and UXN. */
    UINT64 Desc = ((UINT64)Pfn << PAGE_SHIFT) |
                  0x3ULL |                                                  /* valid page */
                  ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2) |             /* AttrIndx (Normal WB) */
                  (3ULL << 8) |                                             /* Inner-shareable */
                  (1ULL << 10) |                                            /* AF */
                  ARM64_PTE_PXN |                                           /* PXN=1 (not kernel-exec) */
                  ARM64_PTE_UXN;                                            /* UXN=1 (not user-exec) */



    /* Skip write + TLBI if the existing entry is already identical (no-op remap). */
    if (l3[l3_idx] == Desc)
        return;

    l3[l3_idx] = Desc;
    __asm__ __volatile__("dsb ish\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
}

static VOID MiArm64MapPxeAlias(VOID);

VOID
MiArm64MapPxeAlias(VOID)
{
    if (!MiArm64InitializeKernelSelfMap())
    {
        DPRINT1("[arm64] MiArm64MapPxeAlias: self-map initialization failed\n");
    }
}

static
BOOLEAN
MiArm64InitializeKernelSelfMap(VOID)
{
    UINT64 Ttbr1;
    UINT64 RootPa;
    PFN_NUMBER RootPfn;
    volatile UINT64 *RootL0;
    UINT64 SelfDesc;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    if ((RootPa == 0) || ((RootPa & (PAGE_SIZE - 1)) != 0))
    {
        return FALSE;
    }

    RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
    RootL0 = (volatile UINT64 *)PXE_BASE;
    SelfDesc = MI_ARM64_MAKE_SELFMAP_DESC(RootPfn);

    if (RootL0[PXE_SELFMAP_INDEX] != SelfDesc)
    {
        RootL0[PXE_SELFMAP_INDEX] = SelfDesc;
        __asm__ __volatile__(
            "dsb ishst\n\t"
            "tlbi vmalle1is\n\t"
            "dsb ish\n\t"
            "isb"
            ::: "memory");
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
        DPRINT("%s\n", "[arm64] MiArm64CanTouchSystemPageTables: ttbr1_el1 read fault");
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
        DPRINT("%s\n", "[arm64] MiArm64CanTouchSystemPageTables: recursive L0 access fault");
        MiArm64SelfMapProbe = 0;
        return FALSE;
    }

    MiArm64SelfMapProbe = 1;

#if DBG
    {
        RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
        RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
        RootL0 = (volatile UINT64 *)PXE_BASE;

        /* 1. Verify TTBR1 L0[493] is self-referential. */
        UINT64 SelfEntry = RootL0[PXE_SELFMAP_INDEX];
        PFN_NUMBER SelfPfn = (PFN_NUMBER)((SelfEntry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
        if (SelfPfn != RootPfn)
        {
            DPRINT1("TTBR1 SELFMAP BROKEN: L0[493] PFN=0x%lx expected=0x%lx\n",
                    (ULONG)SelfPfn, (ULONG)RootPfn);
            ASSERT(SelfPfn == RootPfn);
        }

        /* 2. Verify PXE_BASE reads the same root page through recursion. */
        PMMPTE PxeEntry = MiAddressToPxe((PVOID)PTE_BASE);
        PFN_NUMBER PxePfn = PFN_FROM_PTE(PxeEntry);
        if (PxePfn != RootPfn)
        {
            DPRINT1("PXE SELFMAP READ BROKEN: PXE for PTE_BASE PFN=0x%lx expected=0x%lx\n",
                    (ULONG)PxePfn, (ULONG)RootPfn);
            ASSERT(PxePfn == RootPfn);
        }

        /* 3. Verify kernel PXE self-map agrees with the real TTBR1 root. */
        {
            PMMPTE KernPxe = MiAddressToPxe((PVOID)KSEG0_BASE);
            ULONG KernL0Idx = ((ULONG_PTR)KSEG0_BASE >> PXI_SHIFT) & PXI_MASK;
            UINT64 DirectEntry = RootL0[KernL0Idx];
            if (KernPxe->u.Long != DirectEntry)
            {
                DPRINT1("KERNEL PXE MISMATCH: selfmap=0x%llx direct=0x%llx idx=%u\n",
                        (ULONG64)KernPxe->u.Long, DirectEntry, KernL0Idx);
                ASSERT(KernPxe->u.Long == DirectEntry);
            }
        }

        DPRINT("[arm64] Self-map validation passed: TTBR1 root PFN=0x%lx\n",
               (ULONG)RootPfn);
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
    UINT64 Tcr;
    volatile UINT64 *L0;
    ULONG SelfIndex;
    ULONG Updated = 0;

    if (Seeded)
        return;

    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(Tcr));
    if (Tcr & ARM64_TCR_HA)
    {
        /* Some firmware enables HA even when the CPU does not update AF. Seed anyway. */
    }

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
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
    }

    Seeded = TRUE;
}

PVOID MiSessionViewEnd;

/*
 * MiArm64RegisterFreeLdrPageTables - Register all FreeLDR-created page tables in PFN database
 *
 * PROBLEM:
 * - FreeLDR creates entire page table hierarchy (L0/L1/L2/L3) before kernel starts
 * - These page tables are never registered in the PFN database
 * - Paged pool allocator thinks these pages are free and reuses them
 * - This corrupts page tables with paged pool data
 *
 * SOLUTION:
 * Walk the entire TTBR1 (kernel) page table hierarchy and register all page table pages
 * in the PFN database using MiInitializePfnForOtherProcess. This prevents the paged pool
 * allocator from reusing these pages.
 *
 * This function must be called AFTER:
 * - MiInitializePfnDatabase has completed (PFN database is ready)
 * - MiArm64PfnDatabaseReady is set to TRUE
 *
 * Page table entry format on ARM64:
 * - Valid table descriptor: bits[1:0] = 0b11 (ARM64_PTE_TYPE_TABLE)
 * - Valid block descriptor: bits[1:0] = 0b01 (ARM64_PTE_TYPE_BLOCK)
 * - Table descriptor bits[47:12] contain the PFN of the next level table
 * - Use ARM64_PTE_ADDR_MASK (0x0000FFFFFFFFF000ULL) to extract physical address
 */
static
CODE_SEG("INIT")
VOID
MiArm64RegisterFreeLdrPageTables(VOID)
{
    UINT64 Ttbr1;

    /* Read TTBR1_EL1 to get the kernel page table base */
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    PFN_NUMBER RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);

    /* Register the L0 root table itself when it is not already active. */
    if (RootPfn <= MmHighestPhysicalPage)
    {
        PMMPFN RootPfnEntry = MiGetPfnEntry(RootPfn);
        /* Already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
        if (RootPfnEntry && RootPfnEntry->u3.e1.PageLocation != ActiveAndValid)
        {
            /* Loader allocations can leave PFN entries unlinked from free lists. */
            BOOLEAN ShouldRegister = FALSE;
            BOOLEAN ManualRegister = FALSE;

            if (RootPfnEntry->u3.e2.ReferenceCount == 0)
            {
                /* Page is completely uninitialized - safe to register */
                ShouldRegister = TRUE;
            }
            else if ((RootPfnEntry->u3.e1.PageLocation == FreePageList ||
                      RootPfnEntry->u3.e1.PageLocation == ZeroedPageList) &&
                     (RootPfnEntry->u1.Flink == 0 && RootPfnEntry->u2.Blink == 0))
            {
                /* Orphaned entry: Manually mark as ActiveAndValid */
                ManualRegister = TRUE;
            }
            else
            {
                /* Other states - safe to register */
                ShouldRegister = TRUE;
            }

            if (ShouldRegister)
            {
                /* Root page table is the top-level directory, PteFrame is 0 (no parent) */
                MiInitializePfnForOtherProcess(RootPfn, (PVOID)(ULONG_PTR)RootPa, 0);
            }
            else if (ManualRegister)
            {
                RootPfnEntry->u3.e2.ReferenceCount = 1;
                RootPfnEntry->u2.ShareCount = 1;
                RootPfnEntry->u3.e1.PageLocation = ActiveAndValid;
                RootPfnEntry->u3.e1.Modified = 1;
                RootPfnEntry->u4.PteFrame = 0;
                RootPfnEntry->PteAddress = (PVOID)(ULONG_PTR)RootPa;
                DPRINT("[arm64] Manually registered L0 PFN %lu (orphaned)\n", (ULONG)RootPfn);
            }
        }
    }

    /* Walk kernel space L0 entries (indices 256-511, kernel half of address space)
     * Also include the self-map entry at index 493 which is within this range */
    for (ULONG L0Index = 256; L0Index < 512; L0Index++)
    {
        PVOID L0Va = MiArm64CanonicalVaFromIndexes(L0Index, 0, 0, 0);
        PMMPTE PointerPxe = MiAddressToPxe(L0Va);
        UINT64 L0Entry = PointerPxe->u.Long;

        /* Check if this is a valid table descriptor (bits[1:0] == 0b11) */
        if ((L0Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
            continue;

        /* Extract L1 table physical address and PFN */
        UINT64 L1TablePa = L0Entry & ARM64_PTE_ADDR_MASK;
        PFN_NUMBER L1Pfn = (PFN_NUMBER)(L1TablePa >> PAGE_SHIFT);

        /* Register L1 table in PFN database */
        if (L1Pfn <= MmHighestPhysicalPage)
        {
            PMMPFN L1PfnEntry = MiGetPfnEntry(L1Pfn);

            /* Already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
            if (L1PfnEntry && L1PfnEntry->u3.e1.PageLocation != ActiveAndValid)
            {
                /* Loader allocations can leave PFN entries unlinked from free lists. */
                BOOLEAN ShouldRegister = FALSE;
                BOOLEAN ManualRegister = FALSE;

                if (L1PfnEntry->u3.e2.ReferenceCount == 0)
                {
                    /* Page is completely uninitialized - safe to register */
                    ShouldRegister = TRUE;
                }
                else if ((L1PfnEntry->u3.e1.PageLocation == FreePageList ||
                          L1PfnEntry->u3.e1.PageLocation == ZeroedPageList) &&
                         (L1PfnEntry->u1.Flink == 0 && L1PfnEntry->u2.Blink == 0))
                {
                    /* Orphaned entry: Manually mark as ActiveAndValid */
                    ManualRegister = TRUE;
                }
                else
                {
                    /* Other states - safe to register */
                    ShouldRegister = TRUE;
                }

                if (ShouldRegister)
                {
                    /* L1 table's parent is the L0 root table */
                    MiInitializePfnForOtherProcess(L1Pfn,
                                                   PointerPxe,
                                                   RootPfn);
                }
                else if (ManualRegister)
                {
                    L1PfnEntry->u3.e2.ReferenceCount = 1;
                    L1PfnEntry->u2.ShareCount = 1;
                    L1PfnEntry->u3.e1.PageLocation = ActiveAndValid;
                    L1PfnEntry->u3.e1.Modified = 1;
                    L1PfnEntry->u4.PteFrame = RootPfn;
                    L1PfnEntry->PteAddress = PointerPxe;
                    MiGetPfnEntry(RootPfn)->u2.ShareCount++;
                    DPRINT("[arm64] Manually registered L1 PFN %lu (orphaned)\n", (ULONG)L1Pfn);
                }
            }
        }

        for (ULONG L1Index = 0; L1Index < 512; L1Index++)
        {
            PVOID L1Va = MiArm64CanonicalVaFromIndexes(L0Index, L1Index, 0, 0);
            PMMPTE PointerPpe = MiAddressToPpe(L1Va);
            UINT64 L1Entry = PointerPpe->u.Long;

            /* Check for valid table descriptor (not block descriptor) */
            if ((L1Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
                continue;

            /* Extract L2 table physical address and PFN */
            UINT64 L2TablePa = L1Entry & ARM64_PTE_ADDR_MASK;
            PFN_NUMBER L2Pfn = (PFN_NUMBER)(L2TablePa >> PAGE_SHIFT);

            /* Register L2 table in PFN database */
            if (L2Pfn <= MmHighestPhysicalPage)
            {
                PMMPFN L2PfnEntry = MiGetPfnEntry(L2Pfn);
                /* Already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
                if (L2PfnEntry && L2PfnEntry->u3.e1.PageLocation != ActiveAndValid)
                {
                    /* Loader allocations can leave PFN entries unlinked from free lists. */
                    BOOLEAN ShouldRegister = FALSE;
                    BOOLEAN ManualRegister = FALSE;

                    if (L2PfnEntry->u3.e2.ReferenceCount == 0)
                    {
                        /* Page is completely uninitialized - safe to register */
                        ShouldRegister = TRUE;
                    }
                    else if ((L2PfnEntry->u3.e1.PageLocation == FreePageList ||
                              L2PfnEntry->u3.e1.PageLocation == ZeroedPageList) &&
                             (L2PfnEntry->u1.Flink == 0 && L2PfnEntry->u2.Blink == 0))
                    {
                        /* Orphaned entry: Manually mark as ActiveAndValid */
                        ManualRegister = TRUE;
                    }
                    else
                    {
                        /* Other states - safe to register */
                        ShouldRegister = TRUE;
                    }

                    if (ShouldRegister)
                    {
                        /* L2 table's parent is the L1 table */
                        MiInitializePfnForOtherProcess(L2Pfn,
                                                       PointerPpe,
                                                       L1Pfn);
                    }
                    else if (ManualRegister)
                    {
                        L2PfnEntry->u3.e2.ReferenceCount = 1;
                        L2PfnEntry->u2.ShareCount = 1;
                        L2PfnEntry->u3.e1.PageLocation = ActiveAndValid;
                        L2PfnEntry->u3.e1.Modified = 1;
                        L2PfnEntry->u4.PteFrame = L1Pfn;
                        L2PfnEntry->PteAddress = PointerPpe;
                        MiGetPfnEntry(L1Pfn)->u2.ShareCount++;
                        DPRINT("[arm64] Manually registered L2 PFN %lu (orphaned)\n", (ULONG)L2Pfn);
                    }
                }
            }

            for (ULONG L2Index = 0; L2Index < 512; L2Index++)
            {
                PVOID L2Va = MiArm64CanonicalVaFromIndexes(L0Index, L1Index, L2Index, 0);
                PMMPDE PointerPde = MiAddressToPde(L2Va);
                UINT64 L2Entry = PointerPde->u.Long;

                /* Check for valid table descriptor (not block descriptor or page) */
                if ((L2Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
                    continue;

                /* Extract L3 table physical address and PFN */
                UINT64 L3TablePa = L2Entry & ARM64_PTE_ADDR_MASK;
                PFN_NUMBER L3Pfn = (PFN_NUMBER)(L3TablePa >> PAGE_SHIFT);

                /* Register L3 table in PFN database */
                if (L3Pfn <= MmHighestPhysicalPage)
                {
                    PMMPFN L3PfnEntry = MiGetPfnEntry(L3Pfn);
                    /* Already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
                    if (L3PfnEntry && L3PfnEntry->u3.e1.PageLocation != ActiveAndValid)
                    {
                        /* Loader allocations can leave PFN entries unlinked from free lists. */
                        BOOLEAN ShouldRegister = FALSE;
                        BOOLEAN ManualRegister = FALSE;

                        if (L3PfnEntry->u3.e2.ReferenceCount == 0)
                        {
                            /* Page is completely uninitialized - safe to register */
                            ShouldRegister = TRUE;
                        }
                        else if ((L3PfnEntry->u3.e1.PageLocation == FreePageList ||
                                  L3PfnEntry->u3.e1.PageLocation == ZeroedPageList) &&
                                 (L3PfnEntry->u1.Flink == 0 && L3PfnEntry->u2.Blink == 0))
                        {
                            /* Orphaned entry: Manually mark as ActiveAndValid */
                            ManualRegister = TRUE;
                        }
                        else
                        {
                            /* Other states - safe to register */
                            ShouldRegister = TRUE;
                        }

                        if (ShouldRegister)
                        {
                            /* L3 table's parent is the L2 table */
                            MiInitializePfnForOtherProcess(L3Pfn,
                                                           PointerPde,
                                                           L2Pfn);
                        }
                        else if (ManualRegister)
                        {
                            L3PfnEntry->u3.e2.ReferenceCount = 1;
                            L3PfnEntry->u2.ShareCount = 1;
                            L3PfnEntry->u3.e1.PageLocation = ActiveAndValid;
                            L3PfnEntry->u3.e1.Modified = 1;
                            L3PfnEntry->u4.PteFrame = L2Pfn;
                            L3PfnEntry->PteAddress = (PMMPTE)PointerPde;
                            MiGetPfnEntry(L2Pfn)->u2.ShareCount++;
                            DPRINT("[arm64] Manually registered L3 PFN %lu (orphaned)\n", (ULONG)L3Pfn);
                        }
                    }
                }

                /* Note: We don't need to walk L3 entries because those point to
                 * data pages, not page tables. Only L0/L1/L2/L3 table pages need
                 * to be registered to prevent paged pool from reusing them. */
            }
        }
    }


}

CODE_SEG("INIT")
NTSTATUS
NTAPI
MiInitMachineDependent(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    /*
     * Initialize self-map cache to eliminate redundant L0/L1/L2 allocations.
     * This must happen before any MiArm64MapPageTablePage calls.
     */
    RtlZeroMemory(MiArm64SelfMapL0Cache, sizeof(MiArm64SelfMapL0Cache));
    RtlZeroMemory(MiArm64SelfMapL1Cache, sizeof(MiArm64SelfMapL1Cache));
    MiArm64SelfMapCacheInitialized = TRUE;

    /* Ensure kernel leaf PTEs use Normal WB (MAIR index MI_ARM64_MAIR_NORMAL_WB_IDX). */
    extern MMPTE ValidKernelPte;
    ValidKernelPte.u.Long |= ((ULONGLONG)MI_ARM64_MAIR_NORMAL_WB_IDX << ARM64_PTE_CACHE_SHIFT);

    /*
     * Boot-time MAIR validation: verify that MAIR_EL1 slot MI_ARM64_MAIR_NORMAL_WB_IDX
     * actually contains 0xFF (Normal, Inner/Outer Write-Back, Read/Write Allocate).
     * If the bootloader programmed a different layout, all subsequent kernel page
     * table entries will use the wrong memory attributes, leading to subtle data
     * corruption or performance issues.
     */
    {
        UINT64 MairVal;
        __asm__ __volatile__("mrs %0, mair_el1" : "=r"(MairVal));
        UINT64 Slot = (MairVal >> (MI_ARM64_MAIR_NORMAL_WB_IDX * 8)) & 0xFF;
        if (Slot != 0xFF)
        {
            DPRINT1("[arm64] MAIR VALIDATION FAILED: MAIR_EL1=0x%016llx, "
                    "slot %u = 0x%02llx (expected 0xFF for Normal WB)\n",
                    (ULONGLONG)MairVal, (unsigned)MI_ARM64_MAIR_NORMAL_WB_IDX,
                    (ULONGLONG)Slot);
            /* ASSERT in debug builds; in release, log and continue */
            ASSERT(Slot == 0xFF);
        }
    }

    if (MmSecondaryColors == 0)
    {
        MmSecondaryColors = MI_SECONDARY_COLORS;
        MmSecondaryColorMask = MmSecondaryColors - 1;
    }

    if (MmSystemCacheWs.MinimumWorkingSetSize == 0)
    {
        MmSystemCacheWs.MinimumWorkingSetSize = 0;
        MmSystemCacheWs.WorkingSetSize = 0;
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
        MmSystemCacheStart = (PVOID)MI_SYSTEM_CACHE_START;
        MmSystemCacheEnd = (PVOID)MI_SYSTEM_CACHE_END;
    }

    if (MmNonPagedPoolEnd == NULL)
    {
        MmNonPagedPoolEnd = (PVOID)MI_NONPAGED_POOL_END;
    }

    if (MmNonPagedPoolStart == NULL)
    {
        MmNonPagedPoolStart = (PVOID)(MI_SYSTEM_SPACE_START + (16 * _1MB));
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
        MmPfnDatabase = (PMMPFN)MI_PFN_DATABASE;
    }

    {
        UINT64 Ttbr1;
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        (void)Ttbr1;


        /* Touch the L0 self-map entry through the recursive PXE window. */
        {
            volatile UINT64 *RootL0 = (volatile UINT64 *)PXE_BASE;
            ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);
            _SEH2_TRY
            {
                (void)RootL0[SelfIndex];
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            _SEH2_END;
        }
    }

    if (MiArm64CanTouchSystemPageTables())
    {
        /* ARM64: On ARM64, DirectoryTableBase[0] represents the user address space
         * page table root, which is TTBR0_EL1 (not TTBR1_EL1).
         *
         * TTBR0_EL1 = User space page tables (addresses 0x0000000000000000 - 0x0000FFFFFFFFFFFF)
         * TTBR1_EL1 = Kernel space page tables (addresses 0xFFFF000000000000 - 0xFFFFFFFFFFFFFFFF)
         *
         * Windows/ReactOS convention:
         *   DirectoryTableBase[0] = User page table root (TTBR0 on ARM64, CR3 on x86)
         *   DirectoryTableBase[1] = Hyperspace page table root
         *
         * The Idle/System process needs a valid TTBR0 so it can allocate user-mode
         * memory for process parameters in ExpLoadInitialProcess.
         */
        {
            UINT64 Ttbr1;
            UINT64 RootPa;
            PFN_NUMBER RootPfn;
            volatile UINT64 *RootL0;
            ULONG Index;

            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
            RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
            RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
            if (RootPfn == 0)
            {
                KeBugCheckEx(INSTALL_MORE_MEMORY,
                             MmNumberOfPhysicalPages,
                             MmLowestPhysicalPage,
                             MmHighestPhysicalPage,
                             0);
            }

            RootL0 = (volatile UINT64 *)PXE_BASE;
            for (Index = 0; Index < (PXE_PER_PAGE / 2); Index++)
            {
                RootL0[Index] = 0;
            }
            __asm__ __volatile__("dsb ishst" ::: "memory");

            __asm__ __volatile__(
                "msr ttbr0_el1, %0\n\t"
                "isb\n\t"
                "tlbi vmalle1is\n\t"
                "dsb ish\n\t"
                "isb"
                :: "r"(RootPa)
                : "memory");

            PsGetCurrentProcess()->Pcb.DirectoryTableBase[0] =
                (ULONG_PTR)RootPa;
            PsGetCurrentProcess()->Pcb.DirectoryTableBase[1] =
                (ULONG_PTR)RootPa;
            if ((PsIdleProcess != NULL) &&
                (PsIdleProcess != PsGetCurrentProcess()))
            {
                PsIdleProcess->Pcb.DirectoryTableBase[0] = (ULONG_PTR)RootPa;
                PsIdleProcess->Pcb.DirectoryTableBase[1] = (ULONG_PTR)RootPa;
            }

        }
        MiArm64SeedAccessFlagsForKernelTables();

        /*
         * FreeLDR owns the boot PCR and early stacks. They stay live after
         * ARM3 takes over TTBR1, so seed explicit KSEG0 identity mappings
         * instead of depending on loader block mappings that can disappear.
         */
        MiArm64MapLoaderProcessorState(LoaderBlock);
        MiArm64MapPxeAlias();

        /* Pre-map PFN DB page table levels (parity with amd64):
         * Ensure PPEs and PDEs exist for the PFN DB span so the PTE_BASE
         * leaf can be safely touched by MiMapPfnDatabase. */
        {
            PVOID PfnDbStart = (PVOID)MmPfnDatabase;
            PVOID PfnDbEnd = (PVOID)((PUCHAR)MmPfnDatabase + (MxPfnAllocation * PAGE_SIZE) - 1);
            MiMapPPEs(PfnDbStart, PfnDbEnd);
            MiMapPDEs(PfnDbStart, PfnDbEnd);

        }

        /* Map the PFN database before touching the color tables so the
         * MmFreePagesByColor backing range has valid leaf entries. */
#if DBG
        DbgPrint("[arm64][MMT] MiMapPfnDatabase: begin\n");
#endif
        MiMapPfnDatabase(LoaderBlock);
#if DBG
        DbgPrint("[arm64][MMT] MiMapPfnDatabase: end\n");
#endif

        /* KSEG0 alias pages are created on demand by the alias fault path. */
#if DBG
        DbgPrint("[arm64][MMT] KSEG0 alias pre-map: on-demand fault path active\n");
#endif

        MiInitializeColorTables();
        MiBuildNonPagedPool();

        /* Initialize the nonpaged pool descriptor so ExAllocatePoolWithTag works */
        InitializePool(NonPagedPool, 0);

        MiBuildSystemPteSpace();

        MiMapPPEs((PVOID)MI_MAPPING_RANGE_START, (PVOID)MI_MAPPING_RANGE_END);
        MiMapPDEs((PVOID)MI_MAPPING_RANGE_START, (PVOID)MI_MAPPING_RANGE_END);
        MmFirstReservedMappingPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_START);
        MmLastReservedMappingPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_END);

        /*
         * ARM64: Initialize HyperSpace offset counter.
         *
         * NOTE: We do NOT clear the HyperSpace PTEs here because:
         * 1. FreeLDR may have set up PTEs with "reserved" descriptors (bits [1:0] = 01)
         * 2. Writing to certain PTEs (specifically index 72) during early boot can cause hangs
         * 3. The HyperSpace mapping code handles this by:
         *    - Pre-flushing TLB before modifying PTEs
         *    - Clearing "reserved" PTEs to 0 before writing valid values
         *    - Skipping problematic index 72 entirely
         *    - Post-flushing TLB after writing PTEs (via MI_WRITE_VALID_PTE)
         *
         * The HyperSpace PTEs will be properly initialized on first use via
         * MiMapPageInHyperSpace which handles all ARM64-specific requirements.
         */
        MmFirstReservedMappingPte->u.Hard.PageFrameNumber = MI_HYPERSPACE_PTES - 1;

        /* Store the actual hyperspace L1 base in the System/Idle DTB pair. */
        {
            UINT64 Ttbr1Val;
            volatile UINT64 *RootL0;
            UINT64 HyperL0Entry;
            ULONG HyperIndex;

            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1Val));
            (void)Ttbr1Val;
            RootL0 = (volatile UINT64 *)PXE_BASE;
            HyperIndex = MiAddressToPxi((PVOID)HYPER_SPACE);
            HyperL0Entry = RootL0[HyperIndex];

            if (HyperL0Entry & 1ULL) /* Valid table descriptor */
            {
                ULONG_PTR HyperL1Pa = (ULONG_PTR)(HyperL0Entry & ARM64_PTE_ADDR_MASK);

                /* Fix the System process */
                PsGetCurrentProcess()->Pcb.DirectoryTableBase[1] = HyperL1Pa;

                /* Fix the Idle process (shares boot hyperspace tables) */
                if (PsIdleProcess != NULL &&
                    PsIdleProcess != PsGetCurrentProcess())
                {
                    PsIdleProcess->Pcb.DirectoryTableBase[1] = HyperL1Pa;
                }

                DPRINT1("[arm64] Fixed System/Idle DTB[1]: 0x%p (HyperL1 PFN 0x%lx)\n",
                        (PVOID)HyperL1Pa, (ULONG)(HyperL1Pa >> PAGE_SHIFT));
            }
            else
            {
                DPRINT1("[arm64] WARNING: L0[%u] for HYPER_SPACE is not valid! Entry=0x%llx\n",
                        HyperIndex, (unsigned long long)HyperL0Entry);
            }
        }

        /* ARM64: System cache page table hierarchy is pre-mapped later
         * (after PFN database is ready) via MiMapPPEs. */

#if DBG
        DbgPrint("[arm64][MMT] MiInitializePfnDatabase: begin\n");
#endif
        MiInitializePfnDatabase(LoaderBlock);
#if DBG
        DbgPrint("[arm64][MMT] MiInitializePfnDatabase: end\n");
#endif

        if (MiArm64PfnFinalizePending)
        {
            MiArm64FinalizePfnDatabase(LoaderBlock);
        }

        /* New page tables can be registered after the PFN database scan. */
        MiArm64PfnDatabaseReady = TRUE;

        /* Register FreeLDR-created TTBR1 page tables before pool can reuse them. */
#if DBG
        DbgPrint("[arm64][MMT] MiArm64RegisterFreeLdrPageTables: begin\n");
#endif
        MiArm64RegisterFreeLdrPageTables();
#if DBG
        DbgPrint("[arm64][MMT] MiArm64RegisterFreeLdrPageTables: end\n");
#endif

        /* Pool page PFN entries already initialized by MiArm64InitPoolPfnEntries(). */

        /* Pre-map paged pool page table structures so MiBuildPagedPool can access them.
         * MiBuildPagedPool will try to access PPE/PDE aliases for the paged pool region,
         * so we need to ensure those alias pages are properly backed before it runs. */
        {
            PVOID PagedPoolEnd = (PVOID)(((ULONG_PTR)MmPagedPoolStart +
                                          MmSizeOfPagedPoolInBytes) - 1);

            /* Map PXE entries (L0) for paged pool - needed for 4-level paging */
            /* Map PPE entries (L1) for the paged pool range through physical page tables.
             * This ensures the L2 page tables exist so MiAddressToPde() can be dereferenced.
             * NOTE: We do NOT call MiMapPDEs here - MiBuildPagedPool expects to create
             * the PDEs (L2->L3 table entries) itself via MI_WRITE_VALID_PDE. */
            MiMapPPEs(MmPagedPoolStart, PagedPoolEnd);

            /* Note: We do NOT map PTEs yet - MiBuildPagedPool will handle the first
             * PDE worth of PTEs, and the rest will be demand-allocated during pool growth. */
        }

        /* Match the AMD64 phase-0 contract: pre-create the System View PPEs. */
        {
            PVOID SystemViewEnd = (PUCHAR)MiSystemViewStart + MmSystemViewSize - 1;
            MiMapPPEs(MiSystemViewStart, SystemViewEnd);
        }

        /* Pre-create Session Space page tables before phase-1 elevated IRQL use. */
        {
            PVOID SessionSpaceEnd = (PUCHAR)MiSessionSpaceEnd - 1;

            /* First, ensure the page table hierarchy exists for Session Space itself */
            MiMapPPEs(MmSessionBase, SessionSpaceEnd);
            MiMapPDEs(MmSessionBase, SessionSpaceEnd);

        }

        /* Pre-map page table hierarchy for System Cache range.
         * This is the ARM64 equivalent of AMD64's MiInitializePageTable which
         * creates PXE/PPE entries for the entire kernel space. Without this,
         * the on-demand PDE creation in the fault handler can't access the
         * PPE via the self-map. PDEs are created on demand by the fault handler. */
        MiMapPPEs(MmSystemCacheStart, (PVOID)MI_SYSTEM_CACHE_END);
    }
    else
    {
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
MiMapPPEs(
    PVOID StartAddress,
    PVOID EndAddress)
{
    PMMPDE PointerPpe;
    PMMPDE BasePpe;
    PMMPDE EndPpe;
        BasePpe = MiAddressToPpe(StartAddress);
        EndPpe = MiAddressToPpe(EndAddress);

        for (PointerPpe = BasePpe;
         PointerPpe <= EndPpe;
         PointerPpe++)
    {
        UINT64 Ttbr1;
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

        PVOID TargetVa = MiPpeToAddress(PointerPpe);

        volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);

        if (!EntryPhys)
        {
            volatile UINT64 *L0Entry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 0);
            if (L0Entry && ((*L0Entry & 1ULL) == 0))
            {
                PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                if (Pfn != 0)
                {
                    UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                    PVOID SelfMapAddress = MiAddressToPpe(TargetVa);
                    MiArm64PublishAndZeroTableDesc(L0Entry, Pfn, SelfMapAddress);
                    MiArm64SyncL0ToRoot(MiAddressToPxi(TargetVa), Desc);

                    /* Register the L1 table once the PFN database is available. */
                    if (MiArm64PfnDatabaseReady)
                    {
                        UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
                        PFN_NUMBER L0Pfn = RootPa >> PAGE_SHIFT;
                        MiInitializePfnForOtherProcess(Pfn,
                                                       (PVOID)L0Entry,
                                                       L0Pfn);
                    }

                }
                else
                {
                    DPRINT("%s\n", "[arm64] MiMapPPEs: failed to allocate TTBR1 L1 table");
                }
                EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);
            }

            if (!EntryPhys)
            {
                continue;
            }
        }

        UINT64 Entry = *EntryPhys;
        if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
        {
            PFN_NUMBER L0Pfn = 0;
            volatile UINT64 *L0Entry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 0);
            if (L0Entry && (*L0Entry & 1ULL))
            {
                L0Pfn = (*L0Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
            }

            if (!MiArm64SplitL1BlockToL2(EntryPhys, L0Pfn))
            {
                DPRINT("[arm64] MiMapPPEs: failed to split L1 block for %p\n", TargetVa);
                continue;
            }

            Entry = *EntryPhys;
        }
        if ((Entry & 1ULL) == 0)
        {
            PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
            MiArm64PublishAndZeroTableDesc(EntryPhys,
                                           Pfn,
                                           MiAddressToPde(TargetVa));

            /* Register the L2 table once the PFN database is available. */
            if (MiArm64PfnDatabaseReady)
            {
                volatile UINT64 *L0Entry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 0);
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
    
        BasePde = MiAddressToPde(StartAddress);
        EndPde = MiAddressToPde(EndAddress);
    
        BOOLEAN PerformedPdeMappings = FALSE;
    
        for (PointerPde = BasePde;
             PointerPde <= EndPde;
             PointerPde++)
        {
            UINT64 Ttbr1;
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
        PVOID TargetVa = MiPdeToAddress(PointerPde);
        volatile UINT64 *EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 2);

        if (!EntryPhys)
        {
            volatile UINT64 *PpeEntry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);
            if (!PpeEntry || ((*PpeEntry & 1ULL) == 0))
            {
                volatile UINT64 *L0Entry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 0);
                if (L0Entry && ((*L0Entry & 1ULL) == 0))
                {
                    PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                    if (Pfn != 0)
                    {
                        UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                        PVOID SelfMapAddress = MiAddressToPpe(TargetVa);
                        MiArm64PublishAndZeroTableDesc(L0Entry, Pfn, SelfMapAddress);
                        MiArm64SyncL0ToRoot(MiAddressToPxi(TargetVa), Desc);
            /* Register the L1 table page once the PFN database is available. */
                        if (MiArm64PfnDatabaseReady)
                        {
                            UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
                            PFN_NUMBER L0Pfn = RootPa >> PAGE_SHIFT;
                            MiInitializePfnForOtherProcess(Pfn,
                                                           (PVOID)L0Entry,
                                                           L0Pfn);
                        }

                        PerformedPdeMappings = TRUE;
                    }
                }

                if (PpeEntry && ((*PpeEntry & 1ULL) == 0))
                {
                    PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                    if (Pfn != 0)
                    {
                        MiArm64PublishAndZeroTableDesc(PpeEntry,
                                                       Pfn,
                                                       MiAddressToPde(TargetVa));

                        /* Register the L2 table once the PFN database is available. */
                        if (MiArm64PfnDatabaseReady)
                        {
                            volatile UINT64 *L0Entry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 0);
                            PFN_NUMBER L1Pfn = 0;
                            if (L0Entry && (*L0Entry & 1ULL))
                            {
                                L1Pfn = (*L0Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
                            }
                            MiInitializePfnForOtherProcess(Pfn,
                                                           (PVOID)PpeEntry,
                                                           L1Pfn);
                        }

                        PerformedPdeMappings = TRUE;
                    }
                }
            }

            EntryPhys = MiArm64LookupTableEntry(Ttbr1, TargetVa, 2);
            if (!EntryPhys)
            {
                continue;
            }
        }

        {
            UINT64 Entry = *EntryPhys;
            if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
            {
                PFN_NUMBER L1Pfn = 0;
                volatile UINT64 *PpeEntry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);
                if (PpeEntry && (*PpeEntry & 1ULL))
                {
                    L1Pfn = (*PpeEntry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
                }

                if (!MiArm64SplitL2BlockToL3(EntryPhys, L1Pfn))
                {
                    DPRINT("[arm64] MiMapPDEs: failed to split L2 block for %p\n", TargetVa);
                    continue;
                }

                Entry = *EntryPhys;
            }
            if ((Entry & 1ULL) == 0)
            {
                PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                MiArm64PublishAndZeroTableDesc(EntryPhys,
                                               Pfn,
                                               MiAddressToPte(TargetVa));

                /* Register the L3 table once the PFN database is available. */
                if (MiArm64PfnDatabaseReady)
                {
                    volatile UINT64 *PpeEntry = MiArm64LookupTableEntry(Ttbr1, TargetVa, 1);
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
                //CHAR Stage[160];
                // if (NT_SUCCESS(RtlStringCbPrintfA(Stage,
                //                                   sizeof(Stage),
                //                                   "[arm64] MiMapPDEs: table page mapped %p -> PFN %I64x",
                //                                   SelfVa,
                //                                   (ULONGLONG)Pfn)))
                // {
                //     DPRINT("%s\n", Stage);
                // }
            }
            else
            {
                /* The recursive TTBR1 self-map exposes existing L3 tables. */
            }
        }
    }

    if (PerformedPdeMappings)
    {
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
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
    SIZE_T TotalPtes;
    SIZE_T HeartbeatStride;

    BasePte = MiAddressToPte(StartAddress);
    EndPte = MiAddressToPte(EndAddress);
    TotalPtes = (SIZE_T)(EndPte - BasePte + 1);

    HeartbeatStride = (TotalPtes >= 8) ? (TotalPtes / 8) : TotalPtes;
        if (HeartbeatStride < 0x2000) HeartbeatStride = 0x2000;
        if (HeartbeatStride > TotalPtes) HeartbeatStride = TotalPtes;
    
        BOOLEAN PerformedMappings = FALSE;

    for (PointerPte = BasePte;
         PointerPte <= EndPte;
         PointerPte++)
    {
        /* Check if the PTE is already valid */
        if (!PointerPte->u.Hard.Valid)
        {
            /* Allocate a physical page for this PTE.
             * NOTE: By the time we reach MiMapPTEs, the page table hierarchy
             * (L0/L1/L2) for both the target VA and self-map region should
             * already exist because:
             * - MiMapPPEs created L1 tables and mapped them in the self-map
             * - MiMapPDEs created L2 tables and mapped them in the self-map
             *
             * MiMapPTEs only needs to create leaf (data) page mappings.
             * The previous implementation (lines 1526-1650) redundantly created
             * L0/L1/L2 tables for every PTE, consuming 900K pages before
             * paged pool initialization, causing bugcheck 0x5F.
             */
            TmplPte.u.Hard.PageFrameNumber = MiArm64AllocatePageTablePage();

            /*
             * Direct store instead of MI_WRITE_VALID_PTE: this is a boot-only
             * invalid-to-valid transition on freshly-created page tables with no
             * stale TLB entries. The broadcast TLBI at the end of this function
             * covers all new mappings. MI_WRITE_VALID_PTE's per-PTE
             * KiFlushSingleTb is correct for runtime but redundant here.
             */
            *PointerPte = TmplPte;

            /* Zero the page if requested */
            if (MiArm64ZeroLeafPages)
            {
                RtlZeroMemory(MiPteToAddress(PointerPte), PAGE_SIZE);
            }

            PerformedMappings = TRUE;
        }


    }

    /* If we installed any new mappings, perform a single broadcast TLB maintenance. */
    if (PerformedMappings)
    {
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
    }
}

static
VOID
MiBuildNonPagedPool(VOID)
{
    DPRINT("%s\n", "[arm64] MiBuildNonPagedPool: start");

    if (!MxFreeDescriptor)
    {
        DPRINT("%s\n", "[arm64] MiBuildNonPagedPool: MxFreeDescriptor is NULL");
    }
    /* Check if this is a machine with less than 256MB of RAM, and no override */
    if ((MmNumberOfPhysicalPages <= MI_MIN_PAGES_FOR_NONPAGED_POOL_TUNING) &&
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

        if (MmNumberOfPhysicalPages > 1024)
        {
            /* 256 pages (4 KiB each) represent one MiB of physical memory */
            AdditionalMb = (SIZE_T)((MmNumberOfPhysicalPages - 1024) / 256);
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

        if (MmNumberOfPhysicalPages > 1024)
        {
            /* 256 pages (4 KiB each) represent one MiB of physical memory */
            AdditionalMb = (SIZE_T)((MmNumberOfPhysicalPages - 1024) / 256);
            MmMaximumNonPagedPoolInBytes += AdditionalMb *
                                             (SIZE_T)MmMaxAdditionNonPagedPoolPerMb;
        }
    }

    /* Don't let the maximum go too high */
    if (MmMaximumNonPagedPoolInBytes > MI_MAX_NONPAGED_POOL_SIZE)
    {
        MmMaximumNonPagedPoolInBytes = MI_MAX_NONPAGED_POOL_SIZE;
    }

    /* Convert nonpaged pool size from bytes to pages */
    MmMaximumNonPagedPoolInPages = MmMaximumNonPagedPoolInBytes >> PAGE_SHIFT;

    /* Non paged pool starts after the PFN database */
    MmNonPagedPoolStart = (PUCHAR)MmPfnDatabase + MxPfnAllocation * PAGE_SIZE;


    /* Calculate the nonpaged pool expansion start region */
    MmNonPagedPoolExpansionStart = (PCHAR)MmNonPagedPoolStart +
                                          MmSizeOfNonPagedPoolInBytes;
    ASSERT(IS_PAGE_ALIGNED(MmNonPagedPoolExpansionStart));

    /* And this is where the non paged pool ends */
    MmNonPagedPoolEnd = (PCHAR)MmNonPagedPoolStart + MmMaximumNonPagedPoolInBytes;


    DPRINT("%s\n", "[arm64] MiBuildNonPagedPool: mapping address space");

    /* Map PPEs and PDEs for non paged pool (including expansion) */
    MiMapPPEs(MmNonPagedPoolStart, MmNonPagedPoolEnd);
    MiMapPDEs(MmNonPagedPoolStart, MmNonPagedPoolEnd);

    /* Map the nonpaged pool PTEs (without expansion). Avoid pre-zeroing data pages to speed boot. */
    MiArm64ZeroLeafPages = FALSE;
    MiMapPTEs(MmNonPagedPoolStart, (PCHAR)MmNonPagedPoolExpansionStart - 1);
    MiArm64ZeroLeafPages = TRUE;

    DPRINT("%s\n", "[arm64] MiBuildNonPagedPool: address space mapped");

    /*
     * The pool allocator marks allocation boundaries in PFN entries. Initialize
     * the mapped pool PFNs before InitializePool allocates tracker tables from
     * this range.
     */
#if DBG
    DbgPrint("[arm64][MMT] MiArm64InitPoolPfnEntries: begin\n");
#endif
    MiArm64InitPoolPfnEntries();
#if DBG
    DbgPrint("[arm64][MMT] MiArm64InitPoolPfnEntries: end\n");
#endif

    /* Initialize the ARM3 nonpaged pool */
#if DBG
    DbgPrint("[arm64] MiBuildNonPagedPool: initializing nonpaged pool\n");
#endif
    MiInitializeNonPagedPool();
#if DBG
    DbgPrint("[arm64] MiBuildNonPagedPool: nonpaged pool initialized\n");
#endif
    MiInitializeNonPagedPoolThresholds();
#if DBG
    DbgPrint("[arm64] MiBuildNonPagedPool: nonpaged pool thresholds initialized\n");
#endif



#if DBG
    DbgPrint("[arm64] MiBuildNonPagedPool: complete\n");
#endif
    DPRINT("%s\n", "[arm64] MiBuildNonPagedPool: complete");
}

static
VOID
MiBuildSystemPteSpace(VOID)
{
    DPRINT("%s\n", "[arm64] MiBuildSystemPteSpace: start");
    PMMPTE PointerPte;
    SIZE_T NonPagedSystemSize;
    PVOID SystemPteRangeEnd;

    /* Use the default number of system PTEs */
    MmNumberOfSystemPtes = MI_NUMBER_SYSTEM_PTES;
    NonPagedSystemSize = (MmNumberOfSystemPtes + 1) * PAGE_SIZE;

    /* Put system PTEs at the start of the system VA space */
    MiSystemPteSpaceStart = MmNonPagedSystemStart;
    MiSystemPteSpaceEnd = (PUCHAR)MiSystemPteSpaceStart + NonPagedSystemSize;

    /* Convert exclusive end into inclusive end for the mapping helpers */
    SystemPteRangeEnd = (PVOID)((PUCHAR)MiSystemPteSpaceEnd - 1);

    DPRINT("[ARM64] SystemPTE range: Start=%p End=%p (End-1 byte from %p)\n",
           MiSystemPteSpaceStart, SystemPteRangeEnd, MiSystemPteSpaceEnd);
    DPRINT("[ARM64] SystemPTE PTEs: Start=%p End=%p\n",
           MiAddressToPte(MiSystemPteSpaceStart), MiAddressToPte(SystemPteRangeEnd));

    MiMapPPEs(MiSystemPteSpaceStart, SystemPteRangeEnd);
    MiMapPDEs(MiSystemPteSpaceStart, SystemPteRangeEnd);

    /*
     * ARM64: Do NOT call MiMapPTEs for system PTE space.
     *
     * System PTEs are a dynamically managed VA range — leaf PTEs must start
     * INVALID so MiInitializeSystemPtes can build its free PTE list from them.
     * MiMapPPEs/MiMapPDEs already created the page table hierarchy (L1/L2/L3
     * table pages), which is all that's needed for the self-map to work.
     *
     * Previously, MiMapPTEs eagerly allocated a physical data page for each of
     * the 50,001 leaf PTEs (~195 MB), then MiInitializeSystemPtes immediately
     * zeroed those same PTEs back to invalid. The allocated physical pages were
     * permanently leaked — never freed, never tracked by the PFN database.
     */

    DPRINT("%s\n", "[arm64] MiBuildSystemPteSpace: ranges mapped");

    /* Initialize the system PTE space */
    PointerPte = MiAddressToPte(MiSystemPteSpaceStart);
    MiInitializeSystemPtes(PointerPte, MmNumberOfSystemPtes, SystemPteSpace);

    /* Reserve system PTEs for zeroing PTEs and clear them */
    MiFirstReservedZeroingPte = MiReserveSystemPtes(MI_ZERO_PTES + 1,
                                                    SystemPteSpace);
    RtlZeroMemory(MiFirstReservedZeroingPte, (MI_ZERO_PTES + 1) * sizeof(MMPTE));
    MiFirstReservedZeroingPte->u.Hard.PageFrameNumber = MI_ZERO_PTES;
    DPRINT("%s\n", "[arm64] MiBuildSystemPteSpace: complete");
}

VOID
MiArm64FinalizePfnDatabase(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    MiArm64PfnFinalizePending = FALSE;
}
