/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/mm/ARM3/init.c
 * PURPOSE:         Memory manager initialization stubs for ARM64
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
VOID MiArm64DumpPoolDescriptors(_In_ PVOID VirtualAddress,
                                _In_z_ PCSTR ContextTag);
static VOID MiBuildNonPagedPool(VOID);
static VOID MiBuildSystemPteSpace(VOID);
VOID MiArm64BuildPageTablePfnBitmap(VOID);
BOOLEAN MiArm64IsPageTablePfn(_In_ PFN_NUMBER Pfn);
static BOOLEAN MiArm64CanTouchSystemPageTables(VOID);
static __inline PVOID MiArm64PhysToKseg0(UINT64 Phys);
static __inline PVOID MiArm64PfnToKseg0(PFN_NUMBER Pfn);
extern PVOID MiSystemViewStart;
PVOID MiSystemPteSpaceStart;
PVOID MiSystemPteSpaceEnd;

static LONG MiArm64SelfMapProbe = -1;
/* Control whether MiMapPTEs zeroes newly allocated leaf pages (data pages). */
static volatile BOOLEAN MiArm64ZeroLeafPages = TRUE;
/* Optional boot-time cap for initial nonpaged pool mapping (in MiB). 0 = no cap. */
static ULONG MiArm64NonPagedPoolCapMb = 0;

/* Tracks whether PFN database is ready for access. FALSE during early bootstrap. */
static BOOLEAN MiArm64PfnDatabaseReady = FALSE;

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
 * Bug #37: Per-CPU merged PXE alias pages.
 *
 * The PXE alias page must contain L0 entries from BOTH TTBR0 (user, indices
 * 0-255) and TTBR1 (kernel, indices 256-511). Since these are in separate
 * physical pages, we allocate a merged page and keep it in sync.
 *
 * On every TTBR0 switch the lower half of the merged page is overwritten
 * with the new process's L0 entries. A single global page is not SMP-safe
 * because two CPUs running different processes would race on the same page.
 * We therefore allocate one merged page per processor.
 *
 * MiArm64PxeMergedPfn[cpu]: Per-CPU physical page holding merged PXE entries.
 * MiArm64PxeKernelPfn:      The TTBR1 L0 root PFN (for kernel entries).
 *
 * During early boot only index [0] (BSP) is allocated via MxGetNextPage.
 * AP processors must have their merged pages allocated during their init
 * before they call any code that touches this array.
 */
PFN_NUMBER MiArm64PxeMergedPfn[MAXIMUM_PROCESSORS] = {0};
static PFN_NUMBER MiArm64PxeKernelPfn = 0;

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

/*
 * Sync an L0 table descriptor to the merged PXE page.
 * Must be called after writing a new L0 entry to the real TTBR1 L0 table,
 * because the self-map walks through L0[493] → merged page, not the real L0.
 */
static __inline VOID
MiArm64SyncL0ToMerged(ULONG L0Index, UINT64 Desc)
{
    if (MiArm64PxeMergedPfn[0] != 0)
    {
        volatile UINT64 *Merged = (volatile UINT64 *)MiArm64PfnToKseg0(MiArm64PxeMergedPfn[0]);
        Merged[L0Index] = Desc;
        __asm__ __volatile__("dsb ishst" ::: "memory");
    }
}

/*
 * DIAGNOSTIC: Helper function to verify System View Space PTE integrity.
 * This function checks if the first PTE of System View Space has been corrupted.
 * It's called at strategic checkpoints to identify exactly when corruption occurs.
 */
VOID
MiArm64CheckSystemViewSpacePte(_In_z_ PCSTR Location)
{
    UNREFERENCED_PARAMETER(Location);
}

#define ARM64_PTE_AF                (1ULL << 10)  /* Access Flag - required for L3 page entries */
#define ARM64_PTE_SH_INNER          (3ULL << 8)   /* Inner Shareable */
#define ARM64_PTE_AP_RW_EL1         (0ULL << 6)   /* EL1 R/W, EL0 no access */
#define ARM64_TCR_HA                (1ULL << 39)  /* Hardware Access Flag update */
#define ARM64_TCR_TSZ_MASK          0x3FULL
#define ARM64_TCR_T1SZ_SHIFT        16
#define ARM64_PTE_TABLE_COMPAT      (ARM64_PTE_AF | ARM64_PTE_SH_INNER)
/* Self-map entry now points to a dedicated alias L1 table. */
#define ARM64_SELFMAP_ENTRY_BITS    (ARM64_PTE_TYPE_TABLE | ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2))
#define MI_ARM64_MAKE_TABLE_DESC(Pfn) (((UINT64)(Pfn) << PAGE_SHIFT) | ARM64_SELFMAP_ENTRY_BITS)

/*
 * MI_ARM64_MAKE_SELFMAP_DESC - Create a self-referential page table descriptor.
 *
 * The self-referential entry in the merged PXE page (merged[493]) is special:
 * the MMU reads it as a table descriptor at L0/L1/L2 levels AND as a page
 * descriptor at L3 level (the deepest level in the 4-level recursive walk).
 *
 * At L3, the entry MUST have:
 *   - AF (bit 10) set — otherwise the first access triggers an Access Flag
 *     fault, which causes recursive page faults → stack overflow.
 *   - SH (bits 9:8) set to Inner Shareable — for cache coherency.
 *   - AttrIndx (bits 4:2) set to Normal WB - to match KSEG0 mapping caching!
 *
 * These extra bits are harmless when the entry is read as a table descriptor
 * at L0/L1/L2 (bits 10:8 are in the "ignored" range for table descriptors
 * per ARMv8-A Architecture Reference Manual D5.3.3).
 */
#define MI_ARM64_MAKE_SELFMAP_DESC(Pfn) \
    (((UINT64)(Pfn) << PAGE_SHIFT) | ARM64_PTE_TYPE_TABLE | ARM64_PTE_AF | ARM64_PTE_SH_INNER | ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2))

VOID
MiArm64DumpPoolDescriptors(
    _In_ PVOID VirtualAddress,
    _In_z_ PCSTR ContextTag)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(ContextTag);
}

static __inline PVOID
MiArm64PhysToKseg0(UINT64 Phys)
{
    /* Use the identity-mapped view of physical memory for page table
     * manipulation during early ARM64 bring-up. The identity mapping
     * established by KiArm64EnsureIdentityMapping covers all physical
     * RAM used for TTBR1 tables, so we do not need to rely on a KSEG0
     * alias for these internal MM operations. */
    return (PVOID)(ULONG_PTR)(KSEG0_BASE | Phys);
}

static __inline PVOID
MiArm64PfnToKseg0(PFN_NUMBER Pfn)
{
    return MiArm64PhysToKseg0(((UINT64)Pfn) << PAGE_SHIFT);
}


static RTL_BITMAP MiArm64PageTablePfnBitmap;
static PULONG MiArm64PageTablePfnBitmapBuffer;
static BOOLEAN MiArm64PageTablePfnBitmapReady;

VOID
MiArm64BuildPageTablePfnBitmap(VOID)
{
    UINT64 Ttbr1;
    UINT64 RootPa;
    PFN_NUMBER RootPfn;
    ULONG TotalBits;
    SIZE_T BufferBytes;
    volatile UINT64 *L0;

    if (MiArm64PageTablePfnBitmapReady)
    {
        return;
    }

    if (!MiArm64CanTouchSystemPageTables())
    {
        return;
    }

    TotalBits = (ULONG)(MmHighestPhysicalPage + 1);
    if (TotalBits == 0)
    {
        return;
    }

    BufferBytes = ((SIZE_T)TotalBits + 31) / 32 * sizeof(ULONG);
    MiArm64PageTablePfnBitmapBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                                            BufferBytes,
                                                            'tBmA');
    if (!MiArm64PageTablePfnBitmapBuffer)
    {
        DPRINT("%s\n", "[arm64] MiArm64BuildPageTablePfnBitmap: allocation failed");
        return;
    }

    RtlInitializeBitMap(&MiArm64PageTablePfnBitmap,
                        MiArm64PageTablePfnBitmapBuffer,
                        TotalBits);
    RtlClearAllBits(&MiArm64PageTablePfnBitmap);

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
    if (RootPfn <= MmHighestPhysicalPage)
    {
        RtlSetBit(&MiArm64PageTablePfnBitmap, (ULONG)RootPfn);
    }

    L0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
    /*
     * TTBR1 only serves the upper canonical half; limit the bitmap walk to
     * kernel L0 slots to avoid scanning irrelevant lower-half entries.
     */
    for (ULONG L0Index = 256; L0Index < 512; ++L0Index)
    {
        UINT64 L0Entry = L0[L0Index];
        if ((L0Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        {
            continue;
        }

        PFN_NUMBER L1Pfn = (PFN_NUMBER)((L0Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
        if (L1Pfn <= MmHighestPhysicalPage)
        {
            RtlSetBit(&MiArm64PageTablePfnBitmap, (ULONG)L1Pfn);
        }

        volatile UINT64 *L1 = (volatile UINT64 *)MiArm64PhysToKseg0(L0Entry & ARM64_PTE_ADDR_MASK);
        for (ULONG L1Index = 0; L1Index < 512; ++L1Index)
        {
            UINT64 L1Entry = L1[L1Index];
            if ((L1Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
            {
                continue;
            }

            PFN_NUMBER L2Pfn = (PFN_NUMBER)((L1Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            if (L2Pfn <= MmHighestPhysicalPage)
            {
                RtlSetBit(&MiArm64PageTablePfnBitmap, (ULONG)L2Pfn);
            }

            volatile UINT64 *L2 = (volatile UINT64 *)MiArm64PhysToKseg0(L1Entry & ARM64_PTE_ADDR_MASK);
            for (ULONG L2Index = 0; L2Index < 512; ++L2Index)
            {
                UINT64 L2Entry = L2[L2Index];
                if ((L2Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
                {
                    continue;
                }

                PFN_NUMBER L3Pfn = (PFN_NUMBER)((L2Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
                if (L3Pfn <= MmHighestPhysicalPage)
                {
                    RtlSetBit(&MiArm64PageTablePfnBitmap, (ULONG)L3Pfn);
                }
            }
        }
    }

    MiArm64PageTablePfnBitmapReady = TRUE;
}

BOOLEAN
MiArm64IsPageTablePfn(_In_ PFN_NUMBER Pfn)
{
    if (!MiArm64PageTablePfnBitmapReady)
    {
        return FALSE;
    }

    if (Pfn > MmHighestPhysicalPage)
    {
        return FALSE;
    }

    return RtlCheckBit(&MiArm64PageTablePfnBitmap, (ULONG)Pfn) ? TRUE : FALSE;
}

BOOLEAN
MiArm64RangeHasPageTablePfn(_In_ PFN_NUMBER StartPfn,
                            _In_ PFN_NUMBER PageCount)
{
    PFN_NUMBER EndPfn;
    ULONG StartBit, LengthBits;

    if (!MiArm64PageTablePfnBitmapReady || (PageCount == 0))
    {
        return FALSE;
    }

    if (StartPfn > MmHighestPhysicalPage)
    {
        return FALSE;
    }

    EndPfn = StartPfn + PageCount - 1;
    if (EndPfn < StartPfn)
    {
        EndPfn = MmHighestPhysicalPage;
    }
    else if (EndPfn > MmHighestPhysicalPage)
    {
        EndPfn = MmHighestPhysicalPage;
    }

    StartBit = (ULONG)StartPfn;
    LengthBits = (ULONG)(EndPfn - StartPfn + 1);

    return !RtlAreBitsClear(&MiArm64PageTablePfnBitmap, StartBit, LengthBits);
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
    UINT64 Ttbr1;
    ULONG_PTR PoolStart, PoolEnd, Va;
    ULONG PrevL0 = ~0U, PrevL1 = ~0U, PrevL2 = ~0U;
    volatile UINT64 *L0, *L1 = NULL, *L2 = NULL, *L3 = NULL;
    PFN_NUMBER L3TablePfn = 0;

    PoolStart = (ULONG_PTR)MmNonPagedPoolStart;
    PoolEnd = PoolStart + MmSizeOfNonPagedPoolInBytes;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    L0 = (volatile UINT64 *)MiArm64PhysToKseg0(MI_ARM64_TTBR_TO_PA(Ttbr1));

    for (Va = PoolStart; Va < PoolEnd; Va += PAGE_SIZE)
    {
        ULONG L0Idx = (Va >> 39) & 0x1FF;
        ULONG L1Idx = (Va >> 30) & 0x1FF;
        ULONG L2Idx = (Va >> 21) & 0x1FF;
        ULONG L3Idx = (Va >> 12) & 0x1FF;
        UINT64 Entry;
        PFN_NUMBER DataPfn;
        PMMPFN Pfn;

        /* Refresh cached table pointers only when index changes */
        if (L0Idx != PrevL0)
        {
            Entry = L0[L0Idx];
            if ((Entry & 3) != ARM64_PTE_TYPE_TABLE) break;
            L1 = (volatile UINT64 *)MiArm64PhysToKseg0(Entry & ARM64_PTE_ADDR_MASK);
            PrevL0 = L0Idx;
            PrevL1 = ~0U;
        }

        if (L1Idx != PrevL1)
        {
            Entry = L1[L1Idx];
            if ((Entry & 3) != ARM64_PTE_TYPE_TABLE) break;
            L2 = (volatile UINT64 *)MiArm64PhysToKseg0(Entry & ARM64_PTE_ADDR_MASK);
            PrevL1 = L1Idx;
            PrevL2 = ~0U;
        }

        if (L2Idx != PrevL2)
        {
            Entry = L2[L2Idx];
            if ((Entry & 3) != ARM64_PTE_TYPE_TABLE) break;
            L3 = (volatile UINT64 *)MiArm64PhysToKseg0(Entry & ARM64_PTE_ADDR_MASK);
            L3TablePfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            PrevL2 = L2Idx;
        }

        /* Read leaf (L3) entry */
        Entry = L3[L3Idx];
        if (!(Entry & 1))
            continue;

        DataPfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
        if (DataPfn > MmHighestPhysicalPage)
            continue;

        Pfn = MiGetPfnEntry(DataPfn);
        Pfn->u4.PteFrame = L3TablePfn;
        Pfn->PteAddress = MiAddressToPte((PVOID)Va);
        Pfn->u2.ShareCount++;
        Pfn->u3.e2.ReferenceCount = 1;
        Pfn->u3.e1.PageLocation = ActiveAndValid;
        Pfn->u3.e1.CacheAttribute = MiNonCached;
    }
}

/*
 * MiArm64InitLoaderMappedPfnEntries - Initialize PFN entries for boot-loaded modules
 *
 * Bug #46: Walks the TTBR1 page tables via KSEG0 (bypassing the self-map) to
 * initialize PFN entries for both DATA pages AND PAGE TABLE pages of boot-loaded
 * kernel modules (ntoskrnl, HAL, boot drivers).
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
    volatile UINT64 *L0;
    PFN_NUMBER L0Pfn;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    L0 = (volatile UINT64 *)MiArm64PhysToKseg0(MI_ARM64_TTBR_TO_PA(Ttbr1));
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
            volatile UINT64 *L1 = NULL, *L2 = NULL, *L3 = NULL;
            PFN_NUMBER L1Pfn = 0, L2Pfn = 0, L3Pfn = 0;
            PMMPFN PfnL3 = NULL;

            for (Va = Base; Va < End; Va += PAGE_SIZE)
            {
                ULONG L0Idx = (Va >> 39) & 0x1FF;
                ULONG L1Idx = (Va >> 30) & 0x1FF;
                ULONG L2Idx = (Va >> 21) & 0x1FF;
                ULONG L3Idx = (Va >> 12) & 0x1FF;
                UINT64 Entry;
                PFN_NUMBER DataPfn;
                PMMPFN Pfn;

                /* Walk L0 -> L1 */
                if (L0Idx != PrevL0Idx)
                {
                    Entry = L0[L0Idx];
                    if ((Entry & 3) != ARM64_PTE_TYPE_TABLE) break;
                    L1Pfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
                    L1 = (volatile UINT64 *)MiArm64PhysToKseg0(Entry & ARM64_PTE_ADDR_MASK);
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
                            Pfn->PteAddress = (PMMPTE)MiAddressToPxe((PVOID)Va);
                            TotalPtPages++;
                        }
                    }
                }

                /* Walk L1 -> L2 */
                if (L1Idx != PrevL1Idx)
                {
                    Entry = L1[L1Idx];
                    if ((Entry & 3) != ARM64_PTE_TYPE_TABLE)
                    {
                        PrevL1Idx = L1Idx;
                        L2 = NULL; L3 = NULL; PfnL3 = NULL;
                        PrevL2Idx = ~0U;
                        continue;
                    }
                    L2Pfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
                    L2 = (volatile UINT64 *)MiArm64PhysToKseg0(Entry & ARM64_PTE_ADDR_MASK);
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
                            Pfn->PteAddress = (PMMPTE)MiAddressToPpe((PVOID)Va);
                            TotalPtPages++;
                        }
                    }
                }

                /* Walk L2 -> L3 */
                if (L2Idx != PrevL2Idx)
                {
                    if (!L2) { PrevL2Idx = L2Idx; L3 = NULL; PfnL3 = NULL; continue; }
                    Entry = L2[L2Idx];
                    if ((Entry & 3) != ARM64_PTE_TYPE_TABLE)
                    {
                        PrevL2Idx = L2Idx;
                        L3 = NULL; PfnL3 = NULL;
                        continue;
                    }
                    L3Pfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
                    L3 = (volatile UINT64 *)MiArm64PhysToKseg0(Entry & ARM64_PTE_ADDR_MASK);
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
                            PfnL3->PteAddress = (PMMPTE)MiAddressToPde((PVOID)Va);
                            TotalPtPages++;
                        }
                    }
                }

                /* Read L3 leaf entry */
                if (!L3) continue;
                Entry = L3[L3Idx];
                if (!(Entry & 1)) continue;

                DataPfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
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
                    Pfn->PteAddress = MiAddressToPte((PVOID)Va);
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

static __inline volatile UINT64*
MiArm64LookupTableEntry(UINT64 Ttbr1, PVOID Va, ULONG Level)
{
    UINT64 root_pa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    volatile UINT64 *l0 = (volatile UINT64 *)MiArm64PhysToKseg0(root_pa);
    ULONG l0_idx = MiAddressToPxi(Va);

    if (Level == 0)
        return &l0[l0_idx];

    UINT64 e0 = l0[l0_idx];
    if ((e0 & 1ULL) == 0)
        return NULL;

    volatile UINT64 *l1 = (volatile UINT64 *)MiArm64PhysToKseg0(e0 & ARM64_PTE_ADDR_MASK);
    ULONG l1_idx = (((ULONG_PTR)Va) >> PPI_SHIFT) & 0x1FF;
    if (Level == 1)
        return &l1[l1_idx];

    UINT64 e1 = l1[l1_idx];
    if ((e1 & 1ULL) == 0)
        return NULL;

    volatile UINT64 *l2 = (volatile UINT64 *)MiArm64PhysToKseg0(e1 & ARM64_PTE_ADDR_MASK);
    ULONG l2_idx = (((ULONG_PTR)Va) >> PDI_SHIFT) & 0x1FF;
    if (Level == 2)
        return &l2[l2_idx];

    UINT64 e2 = l2[l2_idx];
    if ((e2 & 1ULL) == 0)
        return NULL;

    volatile UINT64 *l3 = (volatile UINT64 *)MiArm64PhysToKseg0(e2 & ARM64_PTE_ADDR_MASK);
    ULONG l3_idx = MiAddressToPteOffset(Va);
    return &l3[l3_idx];
}

/*
 * MiArm64WritePteViaPhysical - Write a PTE via physical addressing (KSEG0).
 *
 * This function is used during page fault handling when we need to write a PTE
 * but cannot safely access the self-map region (because the self-map PTE itself
 * might not be mapped). It walks the page table hierarchy via physical addressing
 * and writes the PTE directly via KSEG0.
 *
 * Parameters:
 *   Address  - The faulting virtual address whose PTE we want to update.
 *   PteValue - The 64-bit PTE value to write.
 *
 * Returns:
 *   TRUE if the PTE was successfully written.
 *   FALSE if the page table hierarchy is not set up for this address.
 */
BOOLEAN
MiArm64WritePteViaPhysical(
    _In_ PVOID Address,
    _In_ UINT64 PteValue)
{
    UINT64 Ttbr1;

    /* Read TTBR1_EL1 to get the root page table address */
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    /* Look up the L3 (PTE) table entry via physical walk */
    volatile UINT64 *L3Entry = MiArm64LookupTableEntry(Ttbr1, Address, 3);
    if (L3Entry == NULL)
    {
        /* Page table hierarchy not set up for this address - check L2 */
        volatile UINT64 *L2Entry = MiArm64LookupTableEntry(Ttbr1, Address, 2);
        if (L2Entry == NULL || (*L2Entry & 1) == 0)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[MiArm64WritePteViaPhysical] L2 entry not valid for Addr=%p\n",
                Address);
            return FALSE;
        }

        /* L2 is valid but might be a block entry - check type */
        if ((*L2Entry & 3) == 1)
        {
            /* Block entry - cannot write PTE, need L3 table */
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                "[MiArm64WritePteViaPhysical] L2 is block entry for Addr=%p, cannot write PTE\n",
                Address);
            return FALSE;
        }

        /* L2 points to L3 table, but our lookup returned NULL - should not happen */
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
            "[MiArm64WritePteViaPhysical] L2 valid but L3 lookup failed for Addr=%p\n",
            Address);
        return FALSE;
    }

    /* Write the PTE via KSEG0 (physical addressing) */
    *L3Entry = PteValue;

    /* Memory barrier to ensure write is visible before TLB invalidation */
    __asm__ __volatile__("dsb ishst" ::: "memory");

    return TRUE;
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
     * in INIT text. Once PFNs are initialized, allocate directly from a free
     * list page under the PFN lock.
     */
    if (MiArm64PfnDatabaseReady)
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
            MiArm64SyncL0ToMerged(l0_idx, l0[l0_idx]);
            __asm__ __volatile__("dsb ishst" ::: "memory");

            /* Register the L1 table page in PFN database to prevent reuse by paged pool.
             * L1 table is contained in L0, so PteFrame is the L0's PFN.
             * CRITICAL: Skip PFN registration during early bootstrap (see MiMapPPEs). */
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

            /* Register the L2 table page in PFN database to prevent reuse by paged pool.
             * L2 table is contained in L1, so PteFrame is the L1's PFN.
             * CRITICAL: Skip PFN registration during early bootstrap (see MiMapPPEs). */
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

        /* Register the L3 table page in PFN database to prevent reuse by paged pool.
         * L3 table is contained in L2, so PteFrame is the L2's PFN.
         * CRITICAL: Skip PFN registration during early bootstrap (see MiMapPPEs). */
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



    l3[l3_idx] = Desc;
    __asm__ __volatile__("dsb ish\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
}

static VOID MiArm64MapPxeAlias(VOID);

/*
 * MiArm64MapPxeAlias - Set up recursive self-map through merged PXE page.
 *
 * ARM64 RECURSIVE SELF-MAP ARCHITECTURE:
 *
 * On AMD64, MiAddressToPte(VA) works for both user and kernel addresses because
 * the recursive self-map entry in L4 points to L4 itself, creating a structure
 * where all page table levels are accessible through PTE_BASE/PDE_BASE/etc.
 *
 * On ARM64, user (TTBR0) and kernel (TTBR1) address spaces have separate L0
 * page tables. The recursive self-map in TTBR1's L0 only provides access to
 * TTBR1's page table hierarchy, not TTBR0's.
 *
 * THE FIX: Allocate a "merged PXE page" that combines:
 *   - Entries [0..255] from TTBR0 L0 (user page tables)
 *   - Entries [256..511] from TTBR1 L0 (kernel page tables)
 *   - Entry [493] self-referential (points to merged page itself)
 *
 * Then set TTBR1 L0[493] to point to this merged page instead of TTBR1's own
 * L0. This creates a true recursive self-map that covers BOTH address spaces:
 *
 *   MiAddressToPte(UserVA):
 *     L0[493] -> merged -> merged[user_L0_idx] -> TTBR0 L1 -> L2 -> L3 PTE
 *
 *   MiAddressToPte(KernelVA):
 *     L0[493] -> merged -> merged[kern_L0_idx] -> TTBR1 L1 -> L2 -> L3 PTE
 *
 *   MiAddressToPde(VA):
 *     L0[493] -> merged -> merged[493] -> merged -> merged[L0_idx] -> L1 -> L2 PDE
 *
 *   MiAddressToPxe(VA):
 *     L0[493] -> merged -> merged[493] -> merged -> merged[493] -> merged -> PXE entry
 *
 * On context switch (KiArm64WriteUserTtbr), entries [0..255] are resynced
 * with the new process's TTBR0 L0 entries. This is already implemented.
 *
 * IMPORTANT: Writes to PXE-level entries through the self-map go to the merged
 * page, NOT to the real TTBR0/TTBR1 L0 pages. ARM3 code that creates new PXE
 * entries must use MI_WRITE_VALID_PXE which dual-writes to both locations.
 */
VOID
MiArm64MapPxeAlias(VOID)
{
    UINT64 Ttbr1;
    UINT64 RootPa;
    volatile UINT64 *RootL0;
    volatile UINT64 *Merged;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);

    MiArm64PxeKernelPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);

    /*
     * Step 1: Allocate the merged PXE page (one per CPU, index [0] for BSP).
     */
    if (MiArm64PxeMergedPfn[0] == 0)
    {
        MiArm64PxeMergedPfn[0] = MiArm64AllocatePageTablePage();
    }

    if (MiArm64PxeMergedPfn[0] == 0)
    {
        DPRINT1("[arm64] MiArm64MapPxeAlias: FATAL - merged page allocation failed\n");
        return;
    }

    Merged = (volatile UINT64 *)MiArm64PfnToKseg0(MiArm64PxeMergedPfn[0]);

    /*
     * Step 2: Populate the merged page.
     *
     * Copy all 512 entries from TTBR1 L0 first (kernel entries at 256-511,
     * lower half typically empty). Then overlay TTBR0 entries for indices 0-255.
     */
    RtlCopyMemory((PVOID)Merged, (PVOID)RootL0, PAGE_SIZE);

    {
        UINT64 Ttbr0;
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        UINT64 Ttbr0Pa = Ttbr0 & ARM64_PTE_ADDR_MASK;
        if (Ttbr0Pa != 0)
        {
            volatile UINT64 *UserL0 = (volatile UINT64 *)(KSEG0_BASE | Ttbr0Pa);
            int i;
            for (i = 0; i < 256; i++)
            {
                Merged[i] = UserL0[i];
            }
        }
    }

    /*
     * Step 3: Make entry [493] self-referential.
     *
     * This is the KEY to the recursive self-map. merged[493] points back to
     * the merged page itself, creating the same recursive structure that
     * AMD64 has with L4[recursive] = L4.
     *
     * The self-referential entry ensures that:
     *   PXE_BASE walk: L0[493]->merged->merged[493]->merged->merged[493]->merged->... (PXE view)
     *   PDE_BASE walk: L0[493]->merged->merged[493]->merged->merged[L0_idx]->L1 (PDE view)
     *   PTE_BASE walk: L0[493]->merged->merged[L0_idx]->L1->L2->L3 (PTE view)
     */
    Merged[PXE_SELFMAP_INDEX] = MI_ARM64_MAKE_SELFMAP_DESC(MiArm64PxeMergedPfn[0]);

    __asm__ __volatile__("dsb ishst" ::: "memory");

    /*
     * Step 4: Point TTBR1 L0[493] to the merged page.
     *
     * This redirects the self-map root from TTBR1's own L0 page to the merged
     * page. The hardware walk for any self-map address (PTE_BASE, PDE_BASE, etc.)
     * now goes through the merged page which has entries for BOTH TTBR0 and TTBR1.
     *
     * Normal kernel VA resolution is NOT affected - it still uses the other
     * TTBR1 L0 entries (256-511) directly, bypassing L0[493].
     */
    RootL0[PXE_SELFMAP_INDEX] = MI_ARM64_MAKE_SELFMAP_DESC(MiArm64PxeMergedPfn[0]);

    __asm__ __volatile__(
        "dsb ishst\n\t"
        "tlbi vmalle1is\n\t"
        "dsb ish\n\t"
        "isb"
        ::: "memory"
    );

    DPRINT("[arm64] MiArm64MapPxeAlias: recursive self-map via merged page (PFN 0x%lx)\n",
           (ULONG)MiArm64PxeMergedPfn[0]);
}

/*
 * MiArm64InitializeTtbr0Alias - Initialize the TTBR0 alias in TTBR1's self-map
 *
 * This function sets up L0[494] in TTBR1's page table hierarchy to point to
 * the current TTBR0's L0 page table. This enables MiAddressToPteTtbr0() and
 * related functions to correctly access user-space page table entries through
 * the kernel's self-map mechanism.
 *
 * This is the architectural fix for the "split-brain page table" issue where
 * MiAddressToPte(UserAddress) returns a VA in TTBR1's self-map space that
 * doesn't actually map TTBR0's page tables.
 *
 * Called during MiInitMachineDependent after the basic self-map is set up.
 */
VOID
MiArm64InitializeTtbr0Alias(VOID)
{
    UINT64 Ttbr0, Ttbr1;
    UINT64 Ttbr0Pa, Ttbr1Pa;
    volatile UINT64 *L0Table;

    DPRINT("%s\n", "[arm64] MiArm64InitializeTtbr0Alias: entry");

    /* Read both TTBRs */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    Ttbr0Pa = MI_ARM64_TTBR_TO_PA(Ttbr0);
    Ttbr1Pa = MI_ARM64_TTBR_TO_PA(Ttbr1);

    /* Access L0 table via KSEG0 direct mapping */
    L0Table = (volatile UINT64 *)MiArm64PhysToKseg0(Ttbr1Pa);

    /*
     * Set L0[494] to point to TTBR0's L0 page table.
     *
     * This creates an alias in TTBR1's self-map that allows accessing
     * TTBR0's page table hierarchy. When accessing PTEs for user addresses,
     * we use PTE_BASE_TTBR0 which routes through L0[494] instead of the
     * normal self-map at L0[493].
     *
     * Descriptor format: Table descriptor (type = 0b11) pointing to TTBR0's L0 PA
     */
    {
        UINT64 AliasEntry = (Ttbr0Pa & ARM64_PTE_ADDR_MASK) | ARM64_PTE_TYPE_TABLE | ((UINT64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2);
        UINT64 CurrentEntry = L0Table[PXE_TTBR0_ALIAS_INDEX];

        if (CurrentEntry != AliasEntry)
        {


            L0Table[PXE_TTBR0_ALIAS_INDEX] = AliasEntry;

            /* Memory barriers and TLB invalidation */
            __asm__ __volatile__(
                "dsb ishst\n\t"
                "tlbi vmalle1is\n\t"
                "dsb ish\n\t"
                "isb"
                ::: "memory"
            );
        }
        else
        {
            DPRINT("%s\n", "[arm64] MiArm64InitializeTtbr0Alias: L0[494] already configured");
        }
    }

    DPRINT("%s\n", "[arm64] MiArm64InitializeTtbr0Alias: TTBR0 alias initialized");
}

/* MiArm64MapAliasForPointer removed: the recursive self-map through the
 * merged PXE page makes explicit alias mapping unnecessary. MiMapPPEs/MiMapPDEs
 * internally call MiArm64MapPageTablePage which creates self-map entries. */

static
BOOLEAN
MiArm64CanTouchSystemPageTables(VOID)
{
    if (MiArm64SelfMapProbe != -1)
    {
        return MiArm64SelfMapProbe != 0;
    }

    UINT64 Ttbr1;
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

    UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);

    /* CRITICAL FIX: Must access physical page tables via KSEG0 mapping, not raw physical address!
     * KSEG0_BASE | PhysAddr gives the kernel's direct-map window for physical memory.
     */
    volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);

    BOOLEAN Faulted = FALSE;
    UINT64 Current = 0;

    _SEH2_TRY
    {
        Current = RootL0[MiAddressToPxi((PVOID)PXE_SELFMAP)];
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Faulted = TRUE;
    }
    _SEH2_END;

    if (Faulted)
    {
        /* This should not happen if FreeLDR set up KSEG0 correctly */
        DPRINT("%s\n", "[arm64] MiArm64CanTouchSystemPageTables: L0 access fault via KSEG0");
        MiArm64SelfMapProbe = 0;
        return FALSE;
    }

    /*
     * Set up the recursive self-map via merged PXE page if not already done.
     * MiArm64MapPxeAlias allocates the merged page, populates it with both
     * TTBR0 and TTBR1 L0 entries, makes entry[493] self-referential, and
     * sets L0[493] to point to the merged page.
     */
    if (MiArm64PxeMergedPfn[0] == 0)
    {
        MiArm64MapPxeAlias();
        if (MiArm64PxeMergedPfn[0] == 0)
        {
            MiArm64SelfMapProbe = 0;
            return FALSE;
        }
    }
    else
    {
        /* Verify L0[493] points to the merged page */
        ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);
        UINT64 MergedPa = (UINT64)MiArm64PxeMergedPfn[0] << PAGE_SHIFT;
        if ((Current & ARM64_PTE_ADDR_MASK) != MergedPa)
        {
            RootL0[SelfIndex] = MI_ARM64_MAKE_SELFMAP_DESC(MiArm64PxeMergedPfn[0]);
            __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
        }
    }

    MiArm64SelfMapProbe = 1;

    /*
     * SELF-MAP VALIDATION: Verify the recursive self-map is working correctly.
     *
     * The merged PXE page should make MiAddressToPte/Pde/Ppe/Pxe work for
     * both user and kernel addresses. Validate by cross-checking self-map
     * reads against direct KSEG0 physical walks.
     */
#if DBG
    {
        volatile UINT64 *MergedPage = (volatile UINT64 *)MiArm64PfnToKseg0(MiArm64PxeMergedPfn[0]);

        /* 1. Verify merged[493] is self-referential */
        UINT64 SelfEntry = MergedPage[PXE_SELFMAP_INDEX];
        PFN_NUMBER SelfPfn = (PFN_NUMBER)((SelfEntry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
        if (SelfPfn != MiArm64PxeMergedPfn[0])
        {
            DPRINT1("MERGED PXE BROKEN: merged[493] PFN=0x%lx expected=0x%lx\n",
                    (ULONG)SelfPfn, (ULONG)MiArm64PxeMergedPfn[0]);
            ASSERT(SelfPfn == MiArm64PxeMergedPfn[0]);
        }

        /* 2. Verify TTBR1 L0[493] points to the merged page */
        UINT64 L0Entry = RootL0[PXE_SELFMAP_INDEX];
        PFN_NUMBER L0Pfn = (PFN_NUMBER)((L0Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
        if (L0Pfn != MiArm64PxeMergedPfn[0])
        {
            DPRINT1("TTBR1 L0[493] BROKEN: PFN=0x%lx expected=0x%lx\n",
                    (ULONG)L0Pfn, (ULONG)MiArm64PxeMergedPfn[0]);
            ASSERT(L0Pfn == MiArm64PxeMergedPfn[0]);
        }

        /* 3. Verify PXE_BASE reads from the merged page (self-referential chain) */
        PMMPTE PxeEntry = MiAddressToPxe((PVOID)PTE_BASE);
        PFN_NUMBER PxePfn = PFN_FROM_PTE(PxeEntry);
        if (PxePfn != MiArm64PxeMergedPfn[0])
        {
            DPRINT1("PXE SELFMAP READ BROKEN: PXE for PTE_BASE PFN=0x%lx expected=0x%lx\n",
                    (ULONG)PxePfn, (ULONG)MiArm64PxeMergedPfn[0]);
            ASSERT(PxePfn == MiArm64PxeMergedPfn[0]);
        }

        /* 4. Verify kernel PTE self-map works for a known kernel address (KSEG0_BASE) */
        {
            PMMPTE KernPxe = MiAddressToPxe((PVOID)KSEG0_BASE);
            ULONG KernL0Idx = ((ULONG_PTR)KSEG0_BASE >> PXI_SHIFT) & PXI_MASK;
            UINT64 DirectEntry = MergedPage[KernL0Idx];
            if (KernPxe->u.Long != DirectEntry)
            {
                DPRINT1("KERNEL PXE MISMATCH: selfmap=0x%llx direct=0x%llx idx=%u\n",
                        (ULONG64)KernPxe->u.Long, DirectEntry, KernL0Idx);
                ASSERT(KernPxe->u.Long == DirectEntry);
            }
        }

        DPRINT("[arm64] Self-map validation passed: merged PFN=0x%lx\n",
               (ULONG)MiArm64PxeMergedPfn[0]);
    }
#endif /* DBG */

    return TRUE;
}

static
VOID
MiArm64SeedAccessFlagsForKernelTables(VOID)
{
    static BOOLEAN Seeded = FALSE;
    UINT64 Tcr;
    UINT64 Ttbr1;
    UINT64 RootPa;
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

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    L0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
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

        if ((E0 & ARM64_PTE_ADDR_MASK) == RootPa)
            continue;

        volatile UINT64 *L1 = (volatile UINT64 *)MiArm64PhysToKseg0(E0 & ARM64_PTE_ADDR_MASK);
        for (ULONG L1Index = 0; L1Index < 512; ++L1Index)
        {
            UINT64 E1 = L1[L1Index];
            UINT64 Type1 = E1 & ARM64_PTE_TYPE_MASK;

            if (Type1 == ARM64_PTE_TYPE_BLOCK)
            {
                if ((E1 & ARM64_PTE_AF) == 0)
                {
                    L1[L1Index] = E1 | ARM64_PTE_AF;
                    Updated++;
                }
                continue;
            }

            if (Type1 != ARM64_PTE_TYPE_TABLE)
                continue;

            volatile UINT64 *L2 = (volatile UINT64 *)MiArm64PhysToKseg0(E1 & ARM64_PTE_ADDR_MASK);
            for (ULONG L2Index = 0; L2Index < 512; ++L2Index)
            {
                UINT64 E2 = L2[L2Index];
                UINT64 Type2 = E2 & ARM64_PTE_TYPE_MASK;

                if (Type2 == ARM64_PTE_TYPE_BLOCK)
                {
                    if ((E2 & ARM64_PTE_AF) == 0)
                    {
                        L2[L2Index] = E2 | ARM64_PTE_AF;
                        Updated++;
                    }
                    continue;
                }

                if (Type2 != ARM64_PTE_TYPE_TABLE)
                    continue;

                volatile UINT64 *L3 = (volatile UINT64 *)MiArm64PhysToKseg0(E2 & ARM64_PTE_ADDR_MASK);
                for (ULONG L3Index = 0; L3Index < 512; ++L3Index)
                {
                    UINT64 E3 = L3[L3Index];
                    if ((E3 & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_PAGE)
                        continue;

                    if ((E3 & ARM64_PTE_AF) == 0)
                    {
                        L3[L3Index] = E3 | ARM64_PTE_AF;
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

    /* Access L0 table via KSEG0 direct mapping */
    volatile UINT64 *L0Table = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);

    /* Register the L0 root table itself - SKIP, already registered by FreeLDR or kernel */
    if (RootPfn <= MmHighestPhysicalPage)
    {
        PMMPFN RootPfnEntry = MiGetPfnEntry(RootPfn);
        /* Skip if already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
        if (RootPfnEntry && RootPfnEntry->u3.e1.PageLocation != ActiveAndValid)
        {
            /* CRITICAL FIX: Check if the PFN is actually linked in a list before registering.
             * The issue: MxGetNextPage() allocates pages during early init (before PFN DB is ready)
             * by removing them from MxFreeDescriptor. Later, MiBuildPfnDatabaseFromLoaderBlock()
             * processes the ORIGINAL loader block descriptors (which don't reflect MxGetNextPage
             * allocations), creating orphaned PFN entries with PageLocation=ZeroedPageList but
             * Flink/Blink=0 (not actually in any list).
             *
             * If we try to register such orphaned entries, MiInitializePfnForOtherProcess() will
             * call MiUnlinkFreeOrZeroedPage(), which asserts that ListHead->Total != 0, causing
             * a fatal assertion failure at pfnlist.c:161.
             *
             * Solution: Only register the page if it's NOT already initialized (ReferenceCount == 0)
             * OR if it's genuinely in a free/zero list (Flink != 0 AND Blink != 0). */
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
        UINT64 L0Entry = L0Table[L0Index];

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

            /* Skip if already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
            if (L1PfnEntry && L1PfnEntry->u3.e1.PageLocation != ActiveAndValid)
            {
                /* CRITICAL FIX: Check if the PFN is actually linked in a list before registering.
                 * Same orphaned entry check as for L0 table. */
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
                                                   (PVOID)(ULONG_PTR)&L0Table[L0Index],
                                                   RootPfn);
                }
                else if (ManualRegister)
                {
                    L1PfnEntry->u3.e2.ReferenceCount = 1;
                    L1PfnEntry->u2.ShareCount = 1;
                    L1PfnEntry->u3.e1.PageLocation = ActiveAndValid;
                    L1PfnEntry->u3.e1.Modified = 1;
                    L1PfnEntry->u4.PteFrame = RootPfn;
                    L1PfnEntry->PteAddress = (PVOID)(ULONG_PTR)&L0Table[L0Index];
                    MiGetPfnEntry(RootPfn)->u2.ShareCount++;
                    DPRINT("[arm64] Manually registered L1 PFN %lu (orphaned)\n", (ULONG)L1Pfn);
                }
            }
        }

        /* Access L1 table and walk its entries */
        volatile UINT64 *L1Table = (volatile UINT64 *)MiArm64PhysToKseg0(L1TablePa);

        for (ULONG L1Index = 0; L1Index < 512; L1Index++)
        {
            UINT64 L1Entry = L1Table[L1Index];

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
                /* Skip if already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
                if (L2PfnEntry && L2PfnEntry->u3.e1.PageLocation != ActiveAndValid)
                {
                    /* CRITICAL FIX: Check if the PFN is actually linked in a list before registering.
                     * Same orphaned entry check as for L0/L1 tables. */
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
                                                       (PVOID)(ULONG_PTR)&L1Table[L1Index],
                                                       L1Pfn);
                    }
                    else if (ManualRegister)
                    {
                        L2PfnEntry->u3.e2.ReferenceCount = 1;
                        L2PfnEntry->u2.ShareCount = 1;
                        L2PfnEntry->u3.e1.PageLocation = ActiveAndValid;
                        L2PfnEntry->u3.e1.Modified = 1;
                        L2PfnEntry->u4.PteFrame = L1Pfn;
                        L2PfnEntry->PteAddress = (PVOID)(ULONG_PTR)&L1Table[L1Index];
                        MiGetPfnEntry(L1Pfn)->u2.ShareCount++;
                        DPRINT("[arm64] Manually registered L2 PFN %lu (orphaned)\n", (ULONG)L2Pfn);
                    }
                }
            }

            /* Access L2 table and walk its entries */
            volatile UINT64 *L2Table = (volatile UINT64 *)MiArm64PhysToKseg0(L2TablePa);

            for (ULONG L2Index = 0; L2Index < 512; L2Index++)
            {
                UINT64 L2Entry = L2Table[L2Index];

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
                    /* Skip if already active (registered by MiBuildPfnDatabaseFromPages or kernel) */
                    if (L3PfnEntry && L3PfnEntry->u3.e1.PageLocation != ActiveAndValid)
                    {
                        /* CRITICAL FIX: Check if the PFN is actually linked in a list before registering.
                         * Same orphaned entry check as for L0/L1/L2 tables. */
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
                                                           (PVOID)(ULONG_PTR)&L2Table[L2Index],
                                                           L2Pfn);
                        }
                        else if (ManualRegister)
                        {
                            L3PfnEntry->u3.e2.ReferenceCount = 1;
                            L3PfnEntry->u2.ShareCount = 1;
                            L3PfnEntry->u3.e1.PageLocation = ActiveAndValid;
                            L3PfnEntry->u3.e1.Modified = 1;
                            L3PfnEntry->u4.PteFrame = L2Pfn;
                            L3PfnEntry->PteAddress = (PVOID)(ULONG_PTR)&L2Table[L2Index];
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

    /* TODO: Flesh this out with proper ARM64 system VA construction. */

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


        /* Also log the L0 self-map entry via KSEG0 to confirm
         * the recursive slot points at the root table. */
        {
                        UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
                        volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
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
            UINT64 Ttbr0, Ttbr1;
            __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

            /* Set DirectoryTableBase[0] to TTBR0 (user space page tables) */
            PsGetCurrentProcess()->Pcb.DirectoryTableBase[0] =
                (ULONG_PTR)MI_ARM64_TTBR_TO_PA(Ttbr0);
            /* Keep the process KPROCESS DTB pair coherent for scheduler checks. */
            PsGetCurrentProcess()->Pcb.DirectoryTableBase[1] =
                (ULONG_PTR)MI_ARM64_TTBR_TO_PA(Ttbr1);

        }
        MiArm64SeedAccessFlagsForKernelTables();
        /* Seed alias windows early to avoid fault-time alias recursion. */
        MiArm64MapPxeAlias();

        /*
         * ARM64 TTBR0 Alias: Initialize L0[494] to point to TTBR0's L0 page.
         *
         * This is the architectural fix for the split-brain page table issue.
         * With this alias, MiAddressToPteTtbr0(UserAddress) will correctly
         * access TTBR0's page tables through the kernel's self-map mechanism.
         */
        MiArm64InitializeTtbr0Alias();

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

        /* KSEG0 alias pages are now created on demand by the alias fault path. */
#if DBG
        DbgPrint("[arm64][MMT] KSEG0 alias pre-map: skipped (on-demand)\n");
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

        /*
         * ARM64: Fix System/Idle process DirectoryTableBase[1].
         *
         * DirectoryTableBase[1] stores the hyperspace L1 table PFN (physical
         * address). KiSwapProcess uses it to patch L0[492] in the merged page
         * during context switches.
         *
         * Earlier, DirectoryTableBase[1] was incorrectly set to TTBR1's PA
         * (the kernel L0 root page). When KiSwapProcess switched back to the
         * System process, it put the kernel L0 root into merged_page[492].
         * The self-map walk for hyperspace PTEs then went through:
         *   merged[493] -> merged -> merged[492] -> kernel_L0_root ->
         *   kernel_L0_root[0] -> FAULT (no valid entry at L0[0])
         *
         * Fix: Extract the actual hyperspace L1 PFN from L0[492] of the
         * TTBR1 root page, which MiMapPPEs just set up correctly.
         */
        {
            UINT64 Ttbr1Val;
            volatile UINT64 *RootL0;
            UINT64 HyperL0Entry;
            ULONG HyperIndex;

            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1Val));
            RootL0 = (volatile UINT64 *)(KSEG0_BASE | MI_ARM64_TTBR_TO_PA(Ttbr1Val));
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

        /* CHECKPOINT 3: After MiInitializePfnDatabase - DISABLED (MiSystemViewStart not initialized yet) */
        /* MiArm64CheckSystemViewSpacePte("After MiInitializePfnDatabase"); */



        if (MiArm64PfnFinalizePending)
        {
            MiArm64FinalizePfnDatabase(LoaderBlock);
        }

        /* CRITICAL: Now that MiInitializePfnDatabase has completed and scanned all existing page tables,
         * we can safely enable PFN registration for newly created page tables. This ensures that:
         * 1. MiMapPfnDatabase has mapped and zeroed the PFN database region
         * 2. MiInitializePfnDatabase has scanned all boot/early page tables and initialized their PFN entries
         * 3. Any page tables created from this point forward will be correctly registered in the PFN database
         *
         * Previously, setting this flag too early (before MiInitializePfnDatabase) caused a critical bug:
         * - Early page tables were registered via MiInitializePfnForOtherProcess
         * - MiMapPfnDatabase then ZEROED the PFN database, destroying those registrations
         * - MiInitializePfnDatabase re-scanned and set INCORRECT PteFrame values (all pointing to root PD)
         * - Later page tables (System View Space) had correct registrations, but earlier ones were corrupted
         * - This allowed paged pool to reuse page table pages, causing PTE corruption */
        MiArm64PfnDatabaseReady = TRUE;

        /* CRITICAL: Register all FreeLDR-created page tables in the PFN database.
         * This must happen immediately after PFN database is ready, before any pool operations.
         * FreeLDR creates the entire page table hierarchy (L0/L1/L2/L3) but never registers
         * these pages in the PFN database. Without registration, paged pool allocator thinks
         * these pages are free and reuses them, corrupting page tables with pool data.
         *
         * This function walks TTBR1 (kernel page tables) and registers all page table pages
         * found by traversing the hierarchy. This prevents pool allocator from reusing them. */
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
            /* The PXE alias itself is already set up by MiArm64MapPxeAlias() */

            /* Map PPE entries (L1) for the paged pool range through physical page tables.
             * This ensures the L2 page tables exist so MiAddressToPde() can be dereferenced.
             * NOTE: We do NOT call MiMapPDEs here - MiBuildPagedPool expects to create
             * the PDEs (L2->L3 table entries) itself via MI_WRITE_VALID_PDE. */
            MiMapPPEs(MmPagedPoolStart, PagedPoolEnd);

            /* Note: We do NOT map PTEs yet - MiBuildPagedPool will handle the first
             * PDE worth of PTEs, and the rest will be demand-allocated during pool growth. */
        }

        /* CHECKPOINT 4: After pool initialization (paged pool page tables pre-mapped) - DISABLED (MiSystemViewStart not initialized yet) */
        /* MiArm64CheckSystemViewSpacePte("After paged pool page tables pre-mapped"); */

        /* CRITICAL for ARM64: Pre-map page tables for System View Space.
         * System View Space is accessed during Phase 1 at DISPATCH_LEVEL (IRQL 2),
         * particularly in MiCreateArm3StaticMemoryArea. We need to ensure:
         * 1) The actual PPE/PDE page tables exist for System View Space VA range
         * 2) The PTE/PDE/PPE self-map alias pages for accessing those page tables
         *
         * This matches AMD64 which calls MiMapPPEs() for System View Space in Phase 0.
         * Without this, any access to System View Space will fault trying to read
         * the PTE through the self-map, which cannot be serviced at elevated IRQL.
         */
        {
            PVOID SystemViewEnd = (PUCHAR)MiSystemViewStart + MmSystemViewSize - 1;

            /* First, ensure the page table hierarchy exists for System View Space itself.
             * This is analogous to AMD64's MiMapPPEs(MiSystemViewStart, ...) call.
             * We map PPEs and PDEs to ensure the page directory structure exists.
             *
             * CRITICAL: Unlike System PTE Space, System View Space is used to map sections
             * via MmMapViewInSystemSpace. When MiFillSystemPageDirectory is called to create
             * PDEs for a view mapping, and then MiAddMappedPtes is called to write prototype
             * PTEs, these operations can occur at DISPATCH_LEVEL (IRQL 2). On ARM64, accessing
             * PTEs through the self-map requires the underlying page table pages to exist.
             *
             * IMPORTANT: We do NOT call MiMapPTEs here because that would pre-allocate and
             * map 512 MB worth of physical pages (131,072 pages), and MiAddMappedPtes expects
             * PTEs to be zero. Instead, we ensure the self-map aliases for PTE addresses are
             * mapped, which creates the L3 page tables without mapping the data pages.
             */

            /* Disable interrupts while mapping System View Space to prevent
             * timer interrupt handler from accessing System View Space before page tables exist. */
            ULONG64 SavedDaif;
            __asm__ __volatile__(
                "mrs %0, daif\n\t"
                "msr daifset, #2"  /* Set I bit to mask IRQ */
                : "=r"(SavedDaif)
                :
                : "memory");

            MiMapPPEs(MiSystemViewStart, SystemViewEnd);
            MiMapPDEs(MiSystemViewStart, SystemViewEnd);

            /* Restore interrupt state */
            __asm__ __volatile__(
                "msr daif, %0"
                :
                : "r"(SavedDaif)
                : "memory");

            /* CRITICAL: Create L3 page tables for all PDEs in System View Space.
             * After MiMapPDEs, we have L2 tables (PDEs), but we need L3 tables (which hold PTEs).
             * We must create empty (zero-filled) L3 tables so that when a page fault occurs
             * in System View Space, the fault handler can read the PTEs (which will be zero).
             */
            {
                                PMMPDE CurrentPde;
                                PMMPDE BasePde = MiAddressToPde(MiSystemViewStart);
                                PMMPDE EndPde = MiAddressToPde(SystemViewEnd);
                
                                for (CurrentPde = BasePde; CurrentPde <= EndPde; CurrentPde++)
                                {
                                    /* Check if this PDE already points to an L3 table */
                                    if (!CurrentPde->u.Hard.Valid)
                                    {
                                        /* Allocate a physical page for the L3 table */
                                        PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                                        if (Pfn != 0)
                                        {
                                            /* Zero the L3 table - critical so PTEs start as zero */
                                            PVOID L3TableKseg0 = MiArm64PfnToKseg0(Pfn);
                                            RtlZeroMemory(L3TableKseg0, PAGE_SIZE);
                
                                            /* ARM64 CRITICAL: Flush data cache to Point of Coherency to ensure
                                             * the zeroed content is visible to all observers (MMU table walks, other CPUs).
                                             * Without this, the MMU might see stale (uninitialized) data in the L3 table.
                                             *
                                             * We must clean every cache line in the page. ARM64 cache line size is typically
                                             * 64 bytes (CTR_EL0.DminLine), so a 4KB page has 64 cache lines. */
                                            for (ULONG_PTR CacheLine = (ULONG_PTR)L3TableKseg0;
                                                 CacheLine < (ULONG_PTR)L3TableKseg0 + PAGE_SIZE;
                                                 CacheLine += 64)  /* 64-byte cache line size */
                                            {
                                                __asm__ __volatile__("dc cvac, %0" : : "r"(CacheLine) : "memory");
                                            }
                                            __asm__ __volatile__("dsb ish" ::: "memory");  /* Ensure all cleans complete */
                                            __asm__ __volatile__("isb" ::: "memory");      /* Synchronize instruction fetch */
                
                                            /* Create a table descriptor pointing to this L3 table */
                                            MMPDE TempPde = ValidKernelPde;
                                            TempPde.u.Hard.PageFrameNumber = Pfn;
                                            MI_WRITE_VALID_PDE(CurrentPde, TempPde);
                
                                            /* Register the L3 table page in PFN database to prevent
                             * paged pool from reusing these pages.
                             * L3 table is contained in L2, so PteFrame is the L2 (PPE) PFN. */
                            if (MiArm64PfnDatabaseReady)
                            {
                                /* Find the PPE that contains this PDE to get the L2 PFN */
                                PMMPPE CurrentPpe = MiAddressToPpe((PVOID)CurrentPde);
                                if (CurrentPpe->u.Hard.Valid)
                                {
                                    PFN_NUMBER L2Pfn = CurrentPpe->u.Hard.PageFrameNumber;

                                    /* DIAGNOSTIC: Check PFN state BEFORE registration */
                                    MiInitializePfnForOtherProcess(Pfn,
                                                                   (PVOID)CurrentPde,
                                                                   L2Pfn);
                                }
                            }
                        }
                    }
                }


            }

            /* CHECKPOINT 1: After System View Space mapping completes */
            /* MiArm64CheckSystemViewSpacePte("After System View Space MiMapPDEs + alias mapping"); */
        }

        /* CRITICAL for ARM64: Pre-map page tables for Session Space.
         * Session Space is also accessed during Phase 1 at DISPATCH_LEVEL (IRQL 2),
         * and like System View Space, must have both its page table hierarchy and
         * self-map alias pages pre-mapped to avoid page faults at elevated IRQL.
         */
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

    /* CHECKPOINT 5: Before MiInitMachineDependent returns */
    /* MiArm64CheckSystemViewSpacePte("Before MiInitMachineDependent returns"); */



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
                    /* Initialize the new L1 table page before publishing. */
                    /* Use manual zeroing instead of RtlZeroMemory to avoid
                     * potential page fault in optimized memset implementation. */
                    {
                        volatile UINT64 *ZeroPtr = (volatile UINT64 *)MiArm64PfnToKseg0(Pfn);
                        for (SIZE_T i = 0; i < PAGE_SIZE / sizeof(UINT64); i++)
                        {
                            ZeroPtr[i] = 0;
                        }
                    }

                    UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                    *L0Entry = Desc;
                    MiArm64SyncL0ToMerged(MiAddressToPxi(TargetVa), Desc);
                    __asm__ __volatile__("dsb ishst" ::: "memory");

                    /* Register the L1 table page in PFN database to prevent reuse by paged pool.
                     * L1 table is contained in L0, so PteFrame is the L0's PFN.
                     * CRITICAL: Skip PFN registration during early bootstrap when mapping the
                     * PFN database region itself, as this would cause circular dependency:
                     * MiInitializePfnForOtherProcess accesses MmPfnDatabase which isn't mapped yet. */
                    if (MiArm64PfnDatabaseReady)
                    {
                        UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
                        PFN_NUMBER L0Pfn = RootPa >> PAGE_SHIFT;
                        MiInitializePfnForOtherProcess(Pfn,
                                                       (PVOID)L0Entry,
                                                       L0Pfn);
                    }

                    PVOID SelfVa = (PVOID)((ULONG_PTR)PointerPpe & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                    MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
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
            /* Initialize the L2 table page before publishing. */
            RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
            UINT64 table_desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
            *EntryPhys = table_desc;
            __asm__ __volatile__("dsb ishst" ::: "memory");

            /* Register the L2 table page in PFN database to prevent reuse by paged pool.
             * L2 table is contained in L1, so PteFrame is the L1's PFN.
             * EntryPhys points to an L1 entry, extract its parent page.
             * CRITICAL: Skip PFN registration during early bootstrap (see above). */
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

            PMMPDE PdePointer = MiAddressToPde(TargetVa);
            PVOID SelfVa = (PVOID)((ULONG_PTR)PdePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
            MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
        }
        else
        {
            /* L1 entry already valid - ensure the L2 table is accessible via self-map. */
            PFN_NUMBER ExistingPfn = (PFN_NUMBER)((Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            PMMPDE PdePointer = MiAddressToPde(TargetVa);
            PVOID SelfVa = (PVOID)((ULONG_PTR)PdePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
            MiArm64MapPageTablePage(Ttbr1, SelfVa, ExistingPfn);
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
                        /* Initialize the new L1 table page before publishing. */
                        RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                        UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                        *L0Entry = Desc;
                        MiArm64SyncL0ToMerged(MiAddressToPxi(TargetVa), Desc);
                        __asm__ __volatile__("dsb ishst" ::: "memory");

                        /* Register the L1 table page in PFN database to prevent reuse by paged pool.
                         * L1 table is contained in L0, so PteFrame is the L0's PFN.
                         * CRITICAL: Skip PFN registration during early bootstrap (see MiMapPPEs). */
                        if (MiArm64PfnDatabaseReady)
                        {
                            UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
                            PFN_NUMBER L0Pfn = RootPa >> PAGE_SHIFT;
                            MiInitializePfnForOtherProcess(Pfn,
                                                           (PVOID)L0Entry,
                                                           L0Pfn);
                        }

                        PMMPDE PpePointer = MiAddressToPpe(TargetVa);
                        PVOID SelfVa = (PVOID)((ULONG_PTR)PpePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                        MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
                        PerformedPdeMappings = TRUE;
                    }
                }

                if (PpeEntry && ((*PpeEntry & 1ULL) == 0))
                {
                    PFN_NUMBER Pfn = MiArm64AllocatePageTablePage();
                    if (Pfn != 0)
                    {
                        /* Initialize the new L2 table page before publishing. */
                        RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                        UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                        *PpeEntry = Desc;
                        __asm__ __volatile__("dsb ishst" ::: "memory");

                        /* Register the L2 table page in PFN database to prevent reuse by paged pool.
                         * L2 table is contained in L1, so PteFrame is the L1's PFN.
                         * PpeEntry points to an L1 entry, extract its parent page.
                         * CRITICAL: Skip PFN registration during early bootstrap (see MiMapPPEs). */
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

                        PVOID SelfVa = (PVOID)((ULONG_PTR)PointerPde & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                        MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
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
                /* Initialize the new L3 table page before publishing. */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                UINT64 table_desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                *EntryPhys = table_desc;
                __asm__ __volatile__("dsb ishst" ::: "memory");

                /* CRITICAL FIX: Register the L3 table page in PFN database to prevent reuse by paged pool.
                 * This was the root cause of the aliasing bug where L3 page tables were
                 * reused for paged pool allocations, causing PTE corruption.
                 * L3 table is contained in L2, so PteFrame is the L2's PFN.
                 * EntryPhys points to an L2 entry, extract its parent page.
                 * CRITICAL: Skip PFN registration during early bootstrap (see MiMapPPEs). */
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

                PMMPTE PtePointer = MiAddressToPte(TargetVa);
                PVOID SelfVa = (PVOID)((ULONG_PTR)PtePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));
                MiArm64MapPageTablePage(Ttbr1, SelfVa, Pfn);
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
                /* L2 entry already valid - FreeLDR created this L3 table.
                 * We must still map it in the self-map for PTE access.
                 * This is critical for hyperspace and other regions where
                 * FreeLDR pre-creates L3 tables but the kernel needs to
                 * access them via the self-map (PTE_BASE). */
                PFN_NUMBER ExistingPfn = (Entry & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT;
                PMMPTE PtePointer = MiAddressToPte(TargetVa);
                PVOID SelfVa = (PVOID)((ULONG_PTR)PtePointer & ~((ULONG_PTR)PAGE_SIZE - 1ULL));

                MiArm64MapPageTablePage(Ttbr1, SelfVa, ExistingPfn);
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
            MI_WRITE_VALID_PTE(PointerPte, TmplPte);

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

    /* Optional explicit cap for initial nonpaged pool size */
    if (MiArm64NonPagedPoolCapMb != 0)
    {
        ULONGLONG CapBytes = (ULONGLONG)MiArm64NonPagedPoolCapMb * 1024ULL * 1024ULL;
        if (MmSizeOfNonPagedPoolInBytes > CapBytes)
            MmSizeOfNonPagedPoolInBytes = (SIZE_T)CapBytes;
        if (MmMaximumNonPagedPoolInBytes > CapBytes)
            MmMaximumNonPagedPoolInBytes = (SIZE_T)CapBytes;

    }

    /* Convert nonpaged pool size from bytes to pages */
    MmMaximumNonPagedPoolInPages = MmMaximumNonPagedPoolInBytes >> PAGE_SHIFT;

    /* Non paged pool starts after the PFN database */
    MmNonPagedPoolStart = MmPfnDatabase + MxPfnAllocation * PAGE_SIZE;


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

    /* Pool page PFN entries are initialized later by MiArm64InitPoolPfnEntries()
     * during MiInitializePfnDatabase, which walks the page tables via KSEG0. */

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

    MiArm64DumpPoolDescriptors(MiSystemPteSpaceStart, "syspte-pre");
    MiArm64DumpPoolDescriptors(SystemPteRangeEnd, "syspte-end");
    MiMapPPEs(MiSystemPteSpaceStart, SystemPteRangeEnd);
    MiMapPDEs(MiSystemPteSpaceStart, SystemPteRangeEnd);
    MiMapPTEs(MiSystemPteSpaceStart, SystemPteRangeEnd);
    MiArm64DumpPoolDescriptors(MiSystemPteSpaceStart, "syspte-post-map");
    DPRINT("%s\n", "[arm64] MiBuildSystemPteSpace: ranges mapped");

    /* Initialize the system PTE space */
    PointerPte = MiAddressToPte(MiSystemPteSpaceStart);
    MiArm64DumpPoolDescriptors(MiSystemPteSpaceStart, "syspte-before-init");
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
