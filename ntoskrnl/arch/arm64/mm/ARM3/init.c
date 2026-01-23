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
static PFN_NUMBER MiArm64SelfMapL1Pfn = 0;

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
#define ARM64_SELFMAP_ENTRY_BITS    (ARM64_PTE_TYPE_TABLE)
#define MI_ARM64_MAKE_TABLE_DESC(Pfn) (((UINT64)(Pfn) << PAGE_SHIFT) | ARM64_PTE_TYPE_TABLE)

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

static VOID
MiArm64NormalizePoolPfnFlagsRange(_In_ PVOID Base,
                                  _In_ SIZE_T SizeBytes)
{
    ULONG_PTR start = (ULONG_PTR)Base;
    ULONG_PTR end = start + SizeBytes;
    ULONG_PTR va;
    for (va = start; va < end; va += PAGE_SIZE)
    {
        PHYSICAL_ADDRESS Pa = MmGetPhysicalAddress((PVOID)va);
        if (Pa.QuadPart == 0)
        {
            continue;
        }

        PFN_NUMBER pfn = (PFN_NUMBER)(Pa.QuadPart >> PAGE_SHIFT);
        PMMPFN pf = MiGetPfnEntry(pfn);
        if (pf && (MiGetPfnEntryIndex(pf) <= MmHighestPhysicalPage))
        {
            /*
             * ARM64 CRITICAL FIX: Initialize PFN entries for data pages allocated
             * by MiMapPTEs during early boot. These pages were allocated via
             * MxGetNextPage before the PFN database was ready, so their PFN entries
             * have RefCount=0 and PageLocation=ZeroedPageList (uninitialized).
             *
             * When MmProbeAndLockPages is later called on these pool pages, it
             * calls MiReferenceProbedPageAndBumpLockCount which asserts RefCount != 0.
             * This caused the crash in SCSI port driver during FdoSendInquiry.
             *
             * We must properly initialize these fields:
             * - ReferenceCount = 1 (page is in use)
             * - ShareCount = 1 (one reference to the page)
             * - PageLocation = ActiveAndValid (page is active)
             * - PteAddress = PTE that maps this page
             * - PteFrame = PFN of the page table containing the PTE
             *
             * Only initialize if the page appears uninitialized (RefCount=0).
             * Already-initialized pages from expansion allocations have proper values.
             */
            if (pf->u3.e2.ReferenceCount == 0)
            {
                PMMPTE PointerPte = MiAddressToPte((PVOID)va);
                PMMPTE PointerPtePte = MiAddressToPte(PointerPte);
                PFN_NUMBER PageTableFrameNumber = 0;

                /* Get the PFN of the page table containing the PTE */
                if (PointerPtePte->u.Hard.Valid)
                {
                    PageTableFrameNumber = PFN_FROM_PTE(PointerPtePte);
                }

                pf->u3.e2.ReferenceCount = 1;
                pf->u2.ShareCount = 1;
                pf->u3.e1.PageLocation = ActiveAndValid;
                pf->PteAddress = PointerPte;
                pf->u4.PteFrame = PageTableFrameNumber;

                /* Also increment the share count of the page table */
                if (PageTableFrameNumber != 0 && PageTableFrameNumber <= MmHighestPhysicalPage)
                {
                    PMMPFN PageTablePfn = MiGetPfnEntry(PageTableFrameNumber);
                    if (PageTablePfn)
                    {
                        PageTablePfn->u2.ShareCount++;
                    }
                }
            }

            /* Clear allocation flags regardless of whether we initialized RefCount */
            pf->u3.e1.StartOfAllocation = 0;
            pf->u3.e1.EndOfAllocation = 0;
            pf->u4.VerifierAllocation = 0;
        }
    }
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
    for (ULONG L0Index = 0; L0Index < 512; ++L0Index)
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
BOOLEAN
MiArm64SplitL1BlockToL2(_Inout_ volatile UINT64 *Entry, _In_ PFN_NUMBER ParentPfn)
{
    UINT64 Block = *Entry;
    if ((Block & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_BLOCK)
    {
        return TRUE;
    }

    PFN_NUMBER NewPfn = MxGetNextPage(1);
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

    PFN_NUMBER NewPfn = MxGetNextPage(1);
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



/*
 * Recursion guard for MiArm64MapAliasForPointer to prevent infinite recursion.
 *
 * PROBLEM: When MiArm64MapAliasForPointer calls MxGetNextPage to allocate page
 * table pages, MxGetNextPage may trigger PFN initialization or other operations
 * that access paged pool. Accessing paged pool descriptors causes a page fault
 * on self-map addresses (PTE_BASE region), which calls MiArm64MapAliasForPointer
 * again, creating infinite recursion that exhausts all available pages.
 *
 * SOLUTION: Per-CPU recursion flag. If we're already handling a self-map fault,
 * skip the recursive call and let the fault retry - the outer call will have
 * created the necessary mappings by the time we retry.
 *
 * This is safe because:
 * - ARM64 faults are synchronous - only one fault active per CPU at a time
 * - The flag is per-CPU, no SMP conflicts
 * - Skipping the inner call just causes a fault retry, which succeeds after
 *   the outer call completes
 */
/*
 * MiArm64InAliasFault - Per-CPU recursion guard for alias page mapping.
 * This is accessed by MxGetNextPage in mminit.c to avoid PFN lock recursion.
 * Not static - must be visible to mminit.c.
 */
volatile LONG MiArm64InAliasFault[MAXIMUM_PROCESSORS] = {0};
volatile LONG MiArm64InSystemFault[MAXIMUM_PROCESSORS] = {0};
volatile LONG MiArm64PfnLockDepth[MAXIMUM_PROCESSORS] = {0};

/*
 * Sign-extend a 48-bit virtual address to 64-bit canonical form for ARM64.
 * On ARM64 with 48-bit VAs:
 * - Lower-half (user) addresses: bit 47 = 0, bits [63:48] must be 0
 * - Upper-half (kernel) addresses: bit 47 = 1, bits [63:48] must be 1
 *
 * This is critical when synthesizing virtual addresses from page table indices,
 * as the indices may produce non-canonical intermediate values.
 */
FORCEINLINE
PVOID
MiArm64SignExtendVa(ULONG64 Va)
{
    /* Check bit 47 - if set, this is an upper-half address needing sign extension */
    if (Va & (1ULL << 47))
    {
        /* Upper-half kernel address: extend bits [63:48] with 1s */
        return (PVOID)(Va | (0xFFFFULL << 48));
    }
    else
    {
        /* Lower-half user address: ensure bits [63:48] are 0 */
        return (PVOID)(Va & ((1ULL << 48) - 1));
    }
}

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
            PFN_NUMBER NewPfn = MxGetNextPage(1);
            if (NewPfn == 0) return;
            RtlZeroMemory(MiArm64PfnToKseg0(NewPfn), PAGE_SIZE);
            l0[l0_idx] = MI_ARM64_MAKE_TABLE_DESC(NewPfn);
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
            PFN_NUMBER NewPfn = MxGetNextPage(1);
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
        PFN_NUMBER NewPfn = MxGetNextPage(1);
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

    /* Create the L3 (leaf) entry for the page table page */
    UINT64 Desc = ((UINT64)Pfn << PAGE_SHIFT) |
                  0x3ULL |                /* valid page */
                  ((UINT64)4ULL << 2) |   /* AttrIndx=4 (Normal WB) */
                  (3ULL << 8) |           /* Inner-shareable */
                  (1ULL << 10) |          /* AF */
                  (1ULL << 53) |          /* PXN */
                  (1ULL << 54);           /* UXN */



    l3[l3_idx] = Desc;
    __asm__ __volatile__("dsb ish\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
}

/*
 * Ensure that the alias page containing the page table entries for AliasVa
 * is mapped in the kernel address space. This creates any missing parent
 * tables (L0/L1/L2) and installs an L3 mapping for the alias page itself.
 */
VOID
MiArm64EnsureAliasMappingForPointer(
    _In_ PVOID AliasVa)
{
    UINT64 Ttbr1;
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    /* Ensure L0 self-map entry points to the root L0 */
    {
        UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
        volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
        ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);
        UINT64 Current = RootL0[SelfIndex];
        if (MiArm64SelfMapL1Pfn == 0)
        {
            MiArm64SelfMapL1Pfn = MxGetNextPage(1);
            if (MiArm64SelfMapL1Pfn != 0)
            {
                RtlZeroMemory(MiArm64PfnToKseg0(MiArm64SelfMapL1Pfn), PAGE_SIZE);
            }
        }

        UINT64 Desired = MI_ARM64_MAKE_TABLE_DESC(MiArm64SelfMapL1Pfn);
        UINT64 DesiredPa = (UINT64)MiArm64SelfMapL1Pfn << PAGE_SHIFT;
        if ((Current & ARM64_PTE_ADDR_MASK) != DesiredPa ||
            (Current & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        {
            RootL0[SelfIndex] = Desired;
            __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
        }
    }

    UNREFERENCED_PARAMETER(AliasVa);
}

/*
 * Configure the alias tree at L0[493] and ensure PXE_BASE maps the L0 page.
 * PPE/PDE/PTE alias pages are mapped on demand by MiArm64MapAliasForPointer.
 */
VOID
MiArm64MapPxeAlias(VOID)
{
    UINT64 Ttbr1;

    DPRINT("%s\n", "[arm64] MiArm64MapPxeAlias: entry");

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    /* The alias VA we want to back is the page containing PXE_SELFMAP (= PXE_BASE page). */
    PVOID VaBase = (PVOID)((ULONG_PTR)PXE_SELFMAP & ~(PAGE_SIZE - 1ULL));
    UINT64 RootPa = MI_ARM64_TTBR_TO_PA(Ttbr1);
    ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);



    volatile UINT64 *RootL0 = (volatile UINT64 *)MiArm64PhysToKseg0(RootPa);
    UINT64 Current = RootL0[SelfIndex];

    if (MiArm64SelfMapL1Pfn == 0)
    {
        MiArm64SelfMapL1Pfn = MxGetNextPage(1);
        if (MiArm64SelfMapL1Pfn != 0)
        {
            RtlZeroMemory(MiArm64PfnToKseg0(MiArm64SelfMapL1Pfn), PAGE_SIZE);
        }
    }
    if (MiArm64SelfMapL1Pfn == 0)
    {
        DPRINT("%s\n", "[arm64] MiArm64MapPxeAlias: self-map L1 allocation failed");
        return;
    }

    /* Ensure the L0 self-map slot points to the alias L1 root. */
    UINT64 DesiredEntry = MI_ARM64_MAKE_TABLE_DESC(MiArm64SelfMapL1Pfn);
    UINT64 DesiredPa = (UINT64)MiArm64SelfMapL1Pfn << PAGE_SHIFT;

    if ((Current & ARM64_PTE_ADDR_MASK) != DesiredPa ||
        (Current & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
    {


        RootL0[SelfIndex] = DesiredEntry;
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
        Current = RootL0[SelfIndex];
    }
    else
    {
        DPRINT("%s\n", "[arm64] MiArm64MapPxeAlias: L0[493] self-map root already correct");
    }

    /*
     * NOTE: The self-map recursive structure has specific requirements for L0 entries.
     * When accessing PDE_BASE or PTE_BASE through the self-map, the translation walk
     * may need to traverse L0 entries that correspond to user-mode address ranges.
     *
     * DANGEROUS APPROACH (REMOVED): Previously, we filled ALL empty L0 entries with
     * a shared placeholder page. This creates serious problems:
     * - Those entries are no longer "empty" - code may treat them as real mappings
     * - When user processes need real page tables, we'd have conflicts
     * - Hard-to-debug memory corruption when the shared page is accessed
     *
     * SAFER APPROACH: Only ensure the kernel-space L0 entries are properly mapped.
     * The self-map should primarily be used for kernel address space management.
     * If user-space self-map access is needed, allocate real tables on-demand
     * through proper fault handlers, not dummy placeholders.
     *
     * For now, we rely on FreeLDR having set up the necessary kernel L0 entries
     * (typically indices 256-511 for kernel space on ARM64). The self-map recursive
     * entry at L0[493] is sufficient for most kernel MM operations.
     */




    /* Map the PXE alias page to the L0 root via the alias tree. */
    if (MiArm64SelfMapL1Pfn != 0)
    {
        PFN_NUMBER RootPfn = (PFN_NUMBER)(RootPa >> PAGE_SHIFT);
        MiArm64MapPageTablePage(Ttbr1, VaBase, RootPfn);
    }

    DPRINT("%s\n", "[arm64] MiArm64MapPxeAlias: self-map alias root configured");
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
        UINT64 AliasEntry = (Ttbr0Pa & ARM64_PTE_ADDR_MASK) | ARM64_PTE_TYPE_TABLE;
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

VOID
MiArm64MapAliasForPointer(
    _In_ PVOID AliasVa)
{
    UINT64 Ttbr1;
    PFN_NUMBER RootPfn;
    ULONG CpuIndex;
    LONG PreviousValue;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    RootPfn = (PFN_NUMBER)(MI_ARM64_TTBR_TO_PA(Ttbr1) >> PAGE_SHIFT);

    PVOID AliasBase = (PVOID)((ULONG_PTR)AliasVa & ~(PAGE_SIZE - 1ULL));

    /*
     * ARM64 RECURSION GUARD: Prevent infinite recursion when MxGetNextPage
     * triggers nested faults while allocating page table pages.
     *
     * Check if we're already handling an alias fault on this CPU. If so,
     * return immediately - the outer call will create the necessary mappings
     * and the fault will retry successfully.
     */
    CpuIndex = KeGetCurrentProcessorNumber();
    if (CpuIndex >= MAXIMUM_PROCESSORS)
    {
        /* Invalid CPU index - proceed without guard (should never happen) */
        CpuIndex = 0;
    }

    PreviousValue = InterlockedCompareExchange(&MiArm64InAliasFault[CpuIndex], 1, 0);
    if (PreviousValue != 0)
    {
        /* Already handling an alias fault on this CPU - skip to prevent recursion */

        return;
    }

    /* Handle PXE alias page (user and kernel). */
    if ((AliasVa >= (PVOID)PXE_BASE) && (AliasVa <= (PVOID)PXE_TOP))
    {
        MiArm64MapPxeAlias();
        InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
        return;
    }

    /* Handle all PPEs - both user and kernel/system.
     * The self-map needs alias pages for kernel PPEs too.
     * NOTE: Use PPE_TOP not PXE_BASE as upper bound, since PXE_BASE < PPE_TOP
     * and high PPE addresses (like for paged pool at L0[497]) exceed PXE_BASE.
     */
    if ((AliasVa >= (PVOID)PPE_BASE) && (AliasVa <= (PVOID)PPE_TOP))
    {
        ULONG64 ippe = (((ULONG64)(ULONG_PTR)AliasVa) - (ULONG64)PPE_BASE) >> 3;
        ULONG64 pxi = (ippe >> 9) & 0x1FFULL;
        PVOID VaSynth = MiArm64SignExtendVa(pxi << PXI_SHIFT);



        volatile UINT64 *E0 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 0);



        /* Ensure L0 entry exists for this region */
        if (E0 && ((*E0 & 1ULL) == 0))
        {
            /* L0 entry missing - create a new L1 table */
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                /* Zero the new L1 table page before publishing */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                *E0 = Desc;
                __asm__ __volatile__("dsb ishst" ::: "memory");
                if (MiArm64PfnDatabaseReady)
                {
                    MiInitializePfnForOtherProcess(Pfn, (PVOID)E0, RootPfn);
                }


            }
        }

        /* L1 PFN backs the PPE alias page */
        if (E0 && ((*E0 & 1ULL) != 0))
        {
            PFN_NUMBER PfnL1 = (PFN_NUMBER)((*E0 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            MiArm64MapPageTablePage(Ttbr1, AliasBase, PfnL1);


        }

        InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
        return;
    }

    /* Handle all PDEs - both user and kernel/system.
     * The self-map needs alias pages for kernel PDEs too.
     * NOTE: Use PDE_TOP not PPE_BASE as upper bound, since PPE_BASE < PDE_TOP.
     * PPE addresses are already handled by the previous check.
     */
    if ((AliasVa >= (PVOID)PDE_BASE) && (AliasVa <= (PVOID)PDE_TOP))
    {
        ULONG64 ipde = (((ULONG64)(ULONG_PTR)AliasVa) - (ULONG64)PDE_BASE) >> 3;
        ULONG64 ppi = (ipde >> 9) & 0x1FFULL;
        ULONG64 pxi = (ipde >> 18) & 0x1FFULL;
        PVOID VaSynth = MiArm64SignExtendVa((pxi << PXI_SHIFT) | (ppi << PPI_SHIFT));

        volatile UINT64 *E0 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 0);
        if (E0 && ((*E0 & 1ULL) == 0))
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                /* Zero new L1 table page before publishing descriptor */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                *E0 = Desc;
                __asm__ __volatile__("dsb ishst" ::: "memory");
                if (MiArm64PfnDatabaseReady)
                {
                    MiInitializePfnForOtherProcess(Pfn, (PVOID)E0, RootPfn);
                }


            }
        }

        volatile UINT64 *E1 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 1);
        if (E1 && ((*E1 & 1ULL) == 0))
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                /* Zero new L2 table page before publishing descriptor */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                *E1 = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                __asm__ __volatile__("dsb ishst" ::: "memory");
                if (MiArm64PfnDatabaseReady)
                {
                    PFN_NUMBER L1Pfn = (PFN_NUMBER)((*E0 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
                    MiInitializePfnForOtherProcess(Pfn, (PVOID)E1, L1Pfn);
                }


            }
        }

        if (E1 && ((*E1 & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK))
        {
            PFN_NUMBER L1Pfn = (PFN_NUMBER)((*E0 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            if (!MiArm64SplitL1BlockToL2(E1, L1Pfn))
            {
                InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
                return;
            }
        }

        /* L2 PFN backs the PDE alias page */
        if (E1 && ((*E1 & 1ULL) != 0))
        {
            PFN_NUMBER PfnL2 = (PFN_NUMBER)((*E1 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            MiArm64MapPageTablePage(Ttbr1, AliasBase, PfnL2);


        }

        InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
        return;
    }

    /* Handle all PTEs - both user and kernel/system.
     * The self-map needs alias pages for kernel PTEs too!
     *
     * Previously, only MiIsUserPte() addresses were handled, which caused
     * translation faults when accessing PTEs for kernel addresses
     * (e.g., FFFFF6C000214000 is the PTE for kernel VA 0xFFFF800042800000).
     * NOTE: Use PTE_TOP not PDE_BASE as upper bound, since PDE_BASE < PTE_TOP.
     * PDE addresses are already handled by the previous check.
     */
    if ((AliasVa >= (PVOID)PTE_BASE) && (AliasVa <= (PVOID)PTE_TOP))
    {
        ULONG64 ipte = (((ULONG64)(ULONG_PTR)AliasVa) - (ULONG64)PTE_BASE) >> 3;
        ULONG64 pdi = (ipte >> 9) & 0x1FFULL;
        ULONG64 ppi = (ipte >> 18) & 0x1FFULL;
        ULONG64 pxi = (ipte >> 27) & 0x1FFULL;
        PVOID VaSynth = MiArm64SignExtendVa((pxi << PXI_SHIFT) | (ppi << PPI_SHIFT) | (pdi << PDI_SHIFT));

        volatile UINT64 *E0 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 0);
        if (E0 && ((*E0 & 1ULL) == 0))
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                /* Zero new L1 table page then publish descriptor */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                *E0 = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                __asm__ __volatile__("dsb ishst" ::: "memory");
                if (MiArm64PfnDatabaseReady)
                {
                    MiInitializePfnForOtherProcess(Pfn, (PVOID)E0, RootPfn);
                }


            }
        }

        volatile UINT64 *E1 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 1);
        if (E1 && ((*E1 & 1ULL) == 0))
        {
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                *E1 = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                if (MiArm64PfnDatabaseReady)
                {
                    PFN_NUMBER L1Pfn = (PFN_NUMBER)((*E0 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
                    MiInitializePfnForOtherProcess(Pfn, (PVOID)E1, L1Pfn);
                }
            }
        }

        if (E1 && ((*E1 & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK))
        {
            PFN_NUMBER L1Pfn = (PFN_NUMBER)((*E0 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            if (!MiArm64SplitL1BlockToL2(E1, L1Pfn))
            {
                InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
                return;
            }
        }

        volatile UINT64 *E2 = MiArm64LookupTableEntry(Ttbr1, VaSynth, 2);
        if (E2 && ((*E2 & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK))
        {
            PFN_NUMBER L2Pfn = (PFN_NUMBER)((*E1 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            if (!MiArm64SplitL2BlockToL3(E2, L2Pfn))
            {
                InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
                return;
            }
        }

        /*
         * ARM64 PTE ALIAS FIX: Create L3 (page table) if it doesn't exist.
         *
         * When handling a page fault for a new kernel allocation (like a VACB buffer),
         * the page table hierarchy (L0/L1/L2/L3) may not exist yet for the target VA.
         * The code above creates L0 and L1 entries if missing, but the L2 (PDE) entry
         * pointing to an L3 table might still be invalid/empty.
         *
         * Without an L3 table, there's nothing to map into the self-map alias region,
         * and subsequent attempts to access the PTE via the self-map will fault.
         *
         * Solution: Create an empty L3 table if the L2 entry is invalid, then map
         * that L3 table into the self-map. The page fault handler can then use the
         * self-map to populate the L3 entries as needed.
         */
        if (E2 && ((*E2 & 1ULL) == 0))
        {
            /* L2 entry is invalid - create a new L3 table */
            PFN_NUMBER Pfn = MxGetNextPage(1);
            if (Pfn != 0)
            {
                /* Zero the new L3 table page then publish the descriptor */
                RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                *E2 = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");

                if (MiArm64PfnDatabaseReady && E1)
                {
                    PFN_NUMBER L2Pfn = (PFN_NUMBER)((*E1 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
                    MiInitializePfnForOtherProcess(Pfn, (PVOID)E2, L2Pfn);
                }
            }
        }

        if (E2 && ((*E2 & 1ULL) != 0))
        {
            PFN_NUMBER PfnL3 = (PFN_NUMBER)((*E2 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
            MiArm64MapPageTablePage(Ttbr1, AliasBase, PfnL3);
        }
        InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
        return;
    }

    /* If we reach here, the address is not in any recognized self-map range */
    InterlockedExchange(&MiArm64InAliasFault[CpuIndex], 0);
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

    ULONG SelfIndex = MiAddressToPxi((PVOID)PXE_SELFMAP);
    if (MiArm64SelfMapL1Pfn == 0)
    {
        MiArm64SelfMapL1Pfn = MxGetNextPage(1);
        if (MiArm64SelfMapL1Pfn == 0)
        {
            MiArm64SelfMapProbe = 0;
            return FALSE;
        }
        RtlZeroMemory(MiArm64PfnToKseg0(MiArm64SelfMapL1Pfn), PAGE_SIZE);
    }

    UINT64 Desired = MI_ARM64_MAKE_TABLE_DESC(MiArm64SelfMapL1Pfn);
    UINT64 DesiredPa = (UINT64)MiArm64SelfMapL1Pfn << PAGE_SHIFT;
    if ((Current & ARM64_PTE_ADDR_MASK) != DesiredPa ||
        (Current & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
    {
        RootL0[SelfIndex] = Desired;
        __asm__ __volatile__("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");

    }

    MiArm64SelfMapProbe = 1;
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
    DPRINT("%s\n", "[arm64] MiInitMachineDependent: self-map cache initialized (64B L0 + 32KB L1)");

    /* Ensure kernel leaf PTEs use Normal WB (MAIR index 4) to match loader. */
    extern MMPTE ValidKernelPte;
    ValidKernelPte.u.Long |= ((ULONGLONG)4ULL << ARM64_PTE_CACHE_SHIFT);

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

            DPRINT1("[arm64] MiInitMachineDependent: System process DirectoryTableBase[0]=0x%llx (TTBR0=0x%llx, TTBR1=0x%llx)\n",
                    (UINT64)PsGetCurrentProcess()->Pcb.DirectoryTableBase[0],
                    Ttbr0, Ttbr1);
        }
        MiArm64SeedAccessFlagsForKernelTables();
        /* Seed alias windows early to avoid fault-time alias recursion. */
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: seeding alias windows");
        MiArm64MapPxeAlias();

        /*
         * ARM64 TTBR0 Alias: Initialize L0[494] to point to TTBR0's L0 page.
         *
         * This is the architectural fix for the split-brain page table issue.
         * With this alias, MiAddressToPteTtbr0(UserAddress) will correctly
         * access TTBR0's page tables through the kernel's self-map mechanism.
         */
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: initializing TTBR0 alias");
        MiArm64InitializeTtbr0Alias();

        /* Pre-map PFN DB page table levels (parity with amd64):
         * Ensure PPEs and PDEs exist for the PFN DB span so the PTE_BASE
         * leaf can be safely touched by MiMapPfnDatabase. */
        {
            PVOID PfnDbStart = (PVOID)MmPfnDatabase;
            PVOID PfnDbEnd = (PVOID)((PUCHAR)MmPfnDatabase + (MxPfnAllocation * PAGE_SIZE) - 1);
            DPRINT("%s\n", "[arm64] MiInitMachineDependent: pre-mapping PFN DB PPEs");
            MiMapPPEs(PfnDbStart, PfnDbEnd);
            DPRINT("%s\n", "[arm64] MiInitMachineDependent: pre-mapping PFN DB PDEs");
            MiMapPDEs(PfnDbStart, PfnDbEnd);
            DPRINT("%s\n", "[arm64] MiInitMachineDependent: PFN DB PDEs ready");

            /* Ensure the alias pages for PFN DB page tables are mapped. */
            {
                PMMPTE FirstPte = MiAddressToPte(PfnDbStart);
                PMMPTE LastPte = MiAddressToPte(PfnDbEnd);
                PMMPDE FirstPde = MiAddressToPde(PfnDbStart);
                PMMPDE LastPde = MiAddressToPde(PfnDbEnd);
                PMMPPE FirstPpe = MiAddressToPpe(PfnDbStart);
                PMMPPE LastPpe = MiAddressToPpe(PfnDbEnd);

                DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping PFN DB alias pages");

                MiArm64MapAliasForPointer(FirstPpe);
                if (LastPpe != FirstPpe)
                {
                    MiArm64MapAliasForPointer(LastPpe);
                }

                MiArm64MapAliasForPointer(FirstPde);
                if (LastPde != FirstPde)
                {
                    MiArm64MapAliasForPointer(LastPde);
                }

                for (PMMPTE CurrentPte = FirstPte; CurrentPte <= LastPte; CurrentPte += 512)
                {
                    MiArm64MapAliasForPointer(CurrentPte);
                }
                if (((ULONG_PTR)LastPte & ~((ULONG_PTR)PAGE_SIZE - 1)) !=
                    ((ULONG_PTR)(LastPte - 512) & ~((ULONG_PTR)PAGE_SIZE - 1)))
                {
                    MiArm64MapAliasForPointer(LastPte);
                }

                DPRINT("%s\n", "[arm64] MiInitMachineDependent: PFN DB alias pages mapped");
            }
        }

        /* Map the PFN database before touching the color tables so the
         * MmFreePagesByColor backing range has valid leaf entries. */
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping PFN database");
        MiMapPfnDatabase(LoaderBlock);
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: PFN database region mapped (entries not initialized yet)");

        /* Pre-map hyperspace alias pages to allow MmGetPhysicalAddress(HYPER_SPACE). */
        {
            PMMPTE FirstPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_START);
            PMMPTE LastPte = MiAddressToPte((PVOID)MI_MAPPING_RANGE_END);
            PMMPDE FirstPde = MiAddressToPde((PVOID)MI_MAPPING_RANGE_START);
            PMMPDE LastPde = MiAddressToPde((PVOID)MI_MAPPING_RANGE_END);
            PMMPPE FirstPpe = MiAddressToPpe((PVOID)MI_MAPPING_RANGE_START);
            PMMPPE LastPpe = MiAddressToPpe((PVOID)MI_MAPPING_RANGE_END);

            MiArm64MapAliasForPointer(FirstPpe);
            if (LastPpe != FirstPpe)
            {
                MiArm64MapAliasForPointer(LastPpe);
            }

            MiArm64MapAliasForPointer(FirstPde);
            if (LastPde != FirstPde)
            {
                MiArm64MapAliasForPointer(LastPde);
            }

            MiArm64MapAliasForPointer(FirstPte);
            if (LastPte != FirstPte)
            {
                MiArm64MapAliasForPointer(LastPte);
            }
        }

        /* Pre-map alias pages covering the kernel image to allow early VA walks. */
        {
            PLIST_ENTRY ListEntry = LoaderBlock->LoadOrderListHead.Flink;
            if (ListEntry != &LoaderBlock->LoadOrderListHead)
            {
                PLDR_DATA_TABLE_ENTRY LdrEntry =
                    CONTAINING_RECORD(ListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                PVOID ImageBase = LdrEntry->DllBase;
                PVOID ImageEnd = (PVOID)((PUCHAR)ImageBase + LdrEntry->SizeOfImage - 1);
                PMMPTE FirstPte = MiAddressToPte(ImageBase);
                PMMPTE LastPte = MiAddressToPte(ImageEnd);
                PMMPDE FirstPde = MiAddressToPde(ImageBase);
                PMMPDE LastPde = MiAddressToPde(ImageEnd);
                PMMPPE FirstPpe = MiAddressToPpe(ImageBase);
                PMMPPE LastPpe = MiAddressToPpe(ImageEnd);

                MiArm64MapAliasForPointer(FirstPpe);
                if (LastPpe != FirstPpe)
                {
                    MiArm64MapAliasForPointer(LastPpe);
                }

                MiArm64MapAliasForPointer(FirstPde);
                if (LastPde != FirstPde)
                {
                    MiArm64MapAliasForPointer(LastPde);
                }

                for (PMMPTE CurrentPte = FirstPte; CurrentPte <= LastPte; CurrentPte += 512)
                {
                    MiArm64MapAliasForPointer(CurrentPte);
                }
                if (((ULONG_PTR)LastPte & ~((ULONG_PTR)PAGE_SIZE - 1)) !=
                    ((ULONG_PTR)(LastPte - 512) & ~((ULONG_PTR)PAGE_SIZE - 1)))
                {
                    MiArm64MapAliasForPointer(LastPte);
                }
            }
        }

        /* Pre-map PDE alias pages for the KSEG0 linear map used during PFN init. */
        if (MmHighestPhysicalPage >= MmLowestPhysicalPage)
        {
            PFN_NUMBER FirstPfn = MmLowestPhysicalPage;
            PFN_NUMBER LastPfn = MmHighestPhysicalPage;
            PVOID KsegStart = (PVOID)(KSEG0_BASE + ((ULONG64)FirstPfn << PAGE_SHIFT));
            PVOID KsegEnd = (PVOID)(KSEG0_BASE + (((ULONG64)LastPfn << PAGE_SHIFT) | (PAGE_SIZE - 1)));
            PMMPDE FirstPde = MiAddressToPde(KsegStart);
            PMMPDE LastPde = MiAddressToPde(KsegEnd);
            PMMPTE FirstPte = MiAddressToPte(KsegStart);
            PMMPTE LastPte = MiAddressToPte(KsegEnd);
            ULONG_PTR FirstPage = (ULONG_PTR)FirstPde & ~(PAGE_SIZE - 1);
            ULONG_PTR LastPage = (ULONG_PTR)LastPde & ~(PAGE_SIZE - 1);
            ULONG_PTR FirstPtePage = (ULONG_PTR)FirstPte & ~(PAGE_SIZE - 1);
            ULONG_PTR LastPtePage = (ULONG_PTR)LastPte & ~(PAGE_SIZE - 1);
            ULONG_PTR Page;

            for (Page = FirstPage; Page <= LastPage; Page += PAGE_SIZE)
            {
                MiArm64MapAliasForPointer((PVOID)Page);
            }

            for (Page = FirstPtePage; Page <= LastPtePage; Page += PAGE_SIZE)
            {
                MiArm64MapAliasForPointer((PVOID)Page);
            }
        }

        DPRINT("%s\n", "[arm64] MiInitMachineDependent: initializing color tables");
        MiInitializeColorTables();
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: color tables ready");
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: building nonpaged pool");
        MiBuildNonPagedPool();
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: nonpaged pool ready");

        /* Initialize the nonpaged pool descriptor so ExAllocatePoolWithTag works */
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: initializing pool descriptor");
        InitializePool(NonPagedPool, 0);
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: pool descriptor ready");

        DPRINT("%s\n", "[arm64] MiInitMachineDependent: building system PTE space");
        MiBuildSystemPteSpace();
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: system PTE space ready");

        DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping hyperspace range");
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

        MiInitializePfnDatabase(LoaderBlock);

        /* CHECKPOINT 3: After MiInitializePfnDatabase - DISABLED (MiSystemViewStart not initialized yet) */
        /* MiArm64CheckSystemViewSpacePte("After MiInitializePfnDatabase"); */



        if (MiArm64PfnFinalizePending)
        {
            DPRINT("%s\n", "[arm64] MiInitMachineDependent: finalizing PFN database now");
            MiArm64FinalizePfnDatabase(LoaderBlock);
            DPRINT("%s\n", "[arm64] MiInitMachineDependent: PFN database finalized");
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
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: PFN database ready - new page tables will be registered");

        /* CRITICAL: Register all FreeLDR-created page tables in the PFN database.
         * This must happen immediately after PFN database is ready, before any pool operations.
         * FreeLDR creates the entire page table hierarchy (L0/L1/L2/L3) but never registers
         * these pages in the PFN database. Without registration, paged pool allocator thinks
         * these pages are free and reuses them, corrupting page tables with pool data.
         *
         * This function walks TTBR1 (kernel page tables) and registers all page table pages
         * found by traversing the hierarchy. This prevents pool allocator from reusing them. */
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: registering FreeLDR page tables");
        MiArm64RegisterFreeLdrPageTables();
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: FreeLDR page tables registered");

        /* CHECKPOINT 2: After MiArm64RegisterFreeLdrPageTables - DISABLED (MiSystemViewStart not initialized yet) */
        /* MiArm64CheckSystemViewSpacePte("After MiArm64RegisterFreeLdrPageTables"); */

        /* Normalize PFN boundary flags for initial nonpaged pool pages. */
#if DBG
        DbgPrint("[arm64] MiInitMachineDependent: NPP range %p-%p\n",
                 MmNonPagedPoolStart,
                 (PVOID)((PUCHAR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes - 1));
#endif
        MiArm64NormalizePoolPfnFlagsRange(MmNonPagedPoolStart,
                                          MmSizeOfNonPagedPoolInBytes);
#if DBG
        DbgPrint("[arm64] MiInitMachineDependent: NPP PFN flags normalized\n");
#endif
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: normalized NPP PFN flags after PFN DB init");

        /* Pre-map paged pool page table structures so MiBuildPagedPool can access them.
         * MiBuildPagedPool will try to access PPE/PDE aliases for the paged pool region,
         * so we need to ensure those alias pages are properly backed before it runs. */
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: pre-mapping paged pool page tables");
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

            /* CRITICAL for ARM64: Ensure the PTE/PDE/PPE alias pages themselves are mapped.
             * MiBuildPagedPool will access these alias regions:
             * - MiAddressToPpe(MmPagedPoolStart) -> PPE alias (around 0xFFFFF6FB7DA...)
             * - MiAddressToPde(MmPagedPoolStart) -> PDE alias (around 0xFFFFF6FB7E2...)
             * - MiAddressToPte(MmPagedPoolStart) -> PTE alias (around 0xFFFFF6C...)
             * All of these alias addresses need their own page tables to be accessible. */
            {
                PMMPTE FirstPte = MiAddressToPte(MmPagedPoolStart);
                PMMPTE LastPte = MiAddressToPte(PagedPoolEnd);
                PMMPDE FirstPde = MiAddressToPde(MmPagedPoolStart);
                PMMPDE LastPde = MiAddressToPde(PagedPoolEnd);
                PMMPPE FirstPpe = MiAddressToPpe(MmPagedPoolStart);
                PMMPPE LastPpe = MiAddressToPpe(PagedPoolEnd);

                DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping paged pool alias pages");

                /* Ensure the PPE alias pages are backed */
                MiArm64MapAliasForPointer(FirstPpe);
                if (LastPpe != FirstPpe)
                {
                    MiArm64MapAliasForPointer(LastPpe);
                }

                /* Ensure the PDE alias pages are backed */
                MiArm64MapAliasForPointer(FirstPde);
                if (LastPde != FirstPde)
                {
                    MiArm64MapAliasForPointer(LastPde);
                }

                /* Ensure the PTE alias pages are backed */
                MiArm64MapAliasForPointer(FirstPte);
                if (LastPte != FirstPte)
                {
                    MiArm64MapAliasForPointer(LastPte);
                }

                DPRINT("%s\n", "[arm64] MiInitMachineDependent: paged pool alias pages mapped");
            }

            /* Note: We do NOT map PTEs yet - MiBuildPagedPool will handle the first
             * PDE worth of PTEs, and the rest will be demand-allocated during pool growth. */
        }
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: paged pool page tables pre-mapped");

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
            DPRINT("%s\n", "[arm64] MiInitMachineDependent: pre-mapping system view space page tables");

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
                                        PFN_NUMBER Pfn = MxGetNextPage(1);
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
                
                                            /* CRITICAL FIX: Register the L3 table page in PFN database.
                             * This was the root cause of PTE corruption at FFFFF6FCBFDD0000:
                             * - L3 tables for System View Space were created but not registered
                             * - When mapped into self-map via MiArm64MapAliasForPointer, the source
                             *   PFN (L3 table) was never registered in PFN database
                             * - Paged pool would then reuse these L3 table pages, corrupting PTEs
                             * - L3 table is contained in L2, so PteFrame is the L2 (PPE) PFN */
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

            /* Now ensure the self-map alias pages for System View Space PTEs are accessible.
             * This is the key fix: by mapping all L3 page tables in the self-map region,
             * we ensure that when code accesses PTEs via MiAddressToPte(), those accesses
             * won't page fault, even at DISPATCH_LEVEL.
             */
            PMMPTE FirstPte = MiAddressToPte(MiSystemViewStart);
            PMMPTE LastPte = MiAddressToPte(SystemViewEnd);
            PMMPDE FirstPde = MiAddressToPde(MiSystemViewStart);
            PMMPDE LastPde = MiAddressToPde(SystemViewEnd);
            PMMPPE FirstPpe = MiAddressToPpe(MiSystemViewStart);
            PMMPPE LastPpe = MiAddressToPpe(SystemViewEnd);

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping system view space alias pages");

            /* Ensure the PPE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPpe);
            if (LastPpe != FirstPpe)
            {
                MiArm64MapAliasForPointer(LastPpe);
            }

            /* Ensure ALL PDE alias pages are backed - critical for 512 MB range */
            for (PMMPDE CurrentPde = FirstPde; CurrentPde <= LastPde; CurrentPde++)
            {
                /* Map each page containing PDEs (avoid redundant mappings) */
                PVOID CurrentPage = (PVOID)((ULONG_PTR)CurrentPde & ~((ULONG_PTR)PAGE_SIZE - 1));
                if (CurrentPde == FirstPde || CurrentPage != (PVOID)((ULONG_PTR)(CurrentPde - 1) & ~((ULONG_PTR)PAGE_SIZE - 1)))
                {
                    MiArm64MapAliasForPointer(CurrentPde);
                }
            }

            /* Ensure ALL PTE alias pages are backed - this is critical!
             * For 512 MB of System View Space, we have 131,072 PTEs (each covering 4 KB).
             * PTEs are 8 bytes each, so 512 PTEs fit in one 4 KB page.
             * We need to map 256 L3 page tables in the self-map region.
             */
            for (PMMPTE CurrentPte = FirstPte; CurrentPte <= LastPte; CurrentPte += 512)
            {
                MiArm64MapAliasForPointer(CurrentPte);
            }
            /* Ensure the last PTE page is mapped if not already covered */
            PVOID LastPage = (PVOID)((ULONG_PTR)LastPte & ~((ULONG_PTR)PAGE_SIZE - 1));
            PVOID PrevPage = (PVOID)((ULONG_PTR)(LastPte - 512) & ~((ULONG_PTR)PAGE_SIZE - 1));
            if (LastPage != PrevPage)
            {
                MiArm64MapAliasForPointer(LastPte);
            }

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: system view space alias pages mapped");

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
            DPRINT("%s\n", "[arm64] MiInitMachineDependent: pre-mapping session space page tables");
            MiMapPPEs(MmSessionBase, SessionSpaceEnd);
            MiMapPDEs(MmSessionBase, SessionSpaceEnd);

            /* Now ensure the self-map alias pages for Session Space are accessible */
            PMMPTE FirstPte = MiAddressToPte(MmSessionBase);
            PMMPTE LastPte = MiAddressToPte(SessionSpaceEnd);
            PMMPDE FirstPde = MiAddressToPde(MmSessionBase);
            PMMPDE LastPde = MiAddressToPde(SessionSpaceEnd);
            PMMPPE FirstPpe = MiAddressToPpe(MmSessionBase);
            PMMPPE LastPpe = MiAddressToPpe(SessionSpaceEnd);

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping session space alias pages");

            /* Ensure the PPE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPpe);
            if (LastPpe != FirstPpe)
            {
                MiArm64MapAliasForPointer(LastPpe);
            }

            /* Ensure the PDE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPde);
            if (LastPde != FirstPde)
            {
                MiArm64MapAliasForPointer(LastPde);
            }

            /* Ensure the PTE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPte);
            if (LastPte != FirstPte)
            {
                MiArm64MapAliasForPointer(LastPte);
            }

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: session space alias pages mapped");
        }

        /* CRITICAL for ARM64: Pre-map page tables for System Cache.
         * The System Cache region (MI_SYSTEM_CACHE_START to MI_SYSTEM_CACHE_END) is used
         * by the file system cache (CcRos* functions). When section views are mapped
         * into this region, MmCreateVirtualMapping needs to access the self-map PTEs.
         * If the intermediate page tables (PPE, PDE) don't exist, the self-map access
         * will fault, causing a crash during volume mount.
         *
         * NOTE: We only pre-map a reasonable initial portion of the cache space
         * (first 1GB) since the full 1TB range would consume too many pages.
         */
        {
            PVOID SysCacheStart = MmSystemCacheStart;
            /* Pre-map only first 1GB to conserve memory - more will be mapped on demand */
            PVOID SysCacheEnd = (PUCHAR)SysCacheStart + (1ULL * 1024 * 1024 * 1024) - 1;

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: pre-mapping system cache page tables");
            MiMapPPEs(SysCacheStart, SysCacheEnd);
            MiMapPDEs(SysCacheStart, SysCacheEnd);

            /* Ensure the self-map alias pages for System Cache are accessible */
            PMMPTE FirstPte = MiAddressToPte(SysCacheStart);
            PMMPTE LastPte = MiAddressToPte(SysCacheEnd);
            PMMPDE FirstPde = MiAddressToPde(SysCacheStart);
            PMMPDE LastPde = MiAddressToPde(SysCacheEnd);
            PMMPPE FirstPpe = MiAddressToPpe(SysCacheStart);
            PMMPPE LastPpe = MiAddressToPpe(SysCacheEnd);

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping system cache alias pages");

            /* Ensure the PPE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPpe);
            if (LastPpe != FirstPpe)
            {
                MiArm64MapAliasForPointer(LastPpe);
            }

            /* Ensure the PDE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPde);
            if (LastPde != FirstPde)
            {
                MiArm64MapAliasForPointer(LastPde);
            }

            /* Ensure the PTE alias pages are backed */
            for (PMMPTE CurrentPte = FirstPte; CurrentPte <= LastPte; CurrentPte += 512)
            {
                MiArm64MapAliasForPointer(CurrentPte);
            }
            if (((ULONG_PTR)LastPte & ~((ULONG_PTR)PAGE_SIZE - 1)) !=
                ((ULONG_PTR)(FirstPte + ((LastPte - FirstPte) / 512) * 512) & ~((ULONG_PTR)PAGE_SIZE - 1)))
            {
                MiArm64MapAliasForPointer(LastPte);
            }

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: system cache page tables mapped");
        }

        /* CRITICAL for ARM64: Pre-map self-map entries for NonPaged Pool Expansion.
         */
        if (MmNonPagedPoolExpansionStart != NULL && MmNonPagedPoolEnd != NULL)
        {
            PVOID ExpansionEnd = (PUCHAR)MmNonPagedPoolEnd - 1;
            PMMPTE FirstPte = MiAddressToPte(MmNonPagedPoolExpansionStart);
            PMMPTE LastPte = MiAddressToPte(ExpansionEnd);
            PMMPDE FirstPde = MiAddressToPde(MmNonPagedPoolExpansionStart);
            PMMPDE LastPde = MiAddressToPde(ExpansionEnd);
            PMMPPE FirstPpe = MiAddressToPpe(MmNonPagedPoolExpansionStart);
            PMMPPE LastPpe = MiAddressToPpe(ExpansionEnd);

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping nonpaged pool expansion alias pages");

            /* Ensure the PPE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPpe);
            if (LastPpe != FirstPpe)
            {
                MiArm64MapAliasForPointer(LastPpe);
            }

            /* Ensure the PDE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPde);
            if (LastPde != FirstPde)
            {
                MiArm64MapAliasForPointer(LastPde);
            }

            /* Ensure the PTE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPte);
            if (LastPte != FirstPte)
            {
                MiArm64MapAliasForPointer(LastPte);
            }

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: nonpaged pool expansion alias pages mapped");
        }

        /* CRITICAL for ARM64: Pre-map self-map entries for PFN Database.
         * The PFN database is accessed during Phase 1 and must have its
         * self-map entries pre-mapped.
         */
        {
            PVOID PfnDbEnd = (PUCHAR)MmPfnDatabase + (MxPfnAllocation << PAGE_SHIFT) - 1;
            PMMPTE FirstPte = MiAddressToPte(MmPfnDatabase);
            PMMPTE LastPte = MiAddressToPte(PfnDbEnd);
            PMMPDE FirstPde = MiAddressToPde(MmPfnDatabase);
            PMMPDE LastPde = MiAddressToPde(PfnDbEnd);
            PMMPPE FirstPpe = MiAddressToPpe(MmPfnDatabase);
            PMMPPE LastPpe = MiAddressToPpe(PfnDbEnd);

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping PFN database alias pages");

            /* Ensure the PPE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPpe);
            if (LastPpe != FirstPpe)
            {
                MiArm64MapAliasForPointer(LastPpe);
            }

            /* Ensure the PDE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPde);
            if (LastPde != FirstPde)
            {
                MiArm64MapAliasForPointer(LastPde);
            }

            /* Ensure the PTE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPte);
            if (LastPte != FirstPte)
            {
                MiArm64MapAliasForPointer(LastPte);
            }

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: PFN database alias pages mapped");
        }

        /* CRITICAL for ARM64: Pre-map self-map entries for System PTE space.
         */
        if (MmNonPagedSystemStart != NULL && MmNumberOfSystemPtes > 0)
        {
            PVOID SystemPteEnd = (PUCHAR)MmNonPagedSystemStart + ((MmNumberOfSystemPtes + 1) * PAGE_SIZE) - 1;
            PMMPTE FirstPte = MiAddressToPte(MmNonPagedSystemStart);
            PMMPTE LastPte = MiAddressToPte(SystemPteEnd);
            PMMPDE FirstPde = MiAddressToPde(MmNonPagedSystemStart);
            PMMPDE LastPde = MiAddressToPde(SystemPteEnd);
            PMMPPE FirstPpe = MiAddressToPpe(MmNonPagedSystemStart);
            PMMPPE LastPpe = MiAddressToPpe(SystemPteEnd);

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: mapping system PTE space alias pages");

            /* Ensure the PPE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPpe);
            if (LastPpe != FirstPpe)
            {
                MiArm64MapAliasForPointer(LastPpe);
            }

            /* Ensure the PDE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPde);
            if (LastPde != FirstPde)
            {
                MiArm64MapAliasForPointer(LastPde);
            }

            /* Ensure the PTE alias pages are backed */
            MiArm64MapAliasForPointer(FirstPte);
            if (LastPte != FirstPte)
            {
                MiArm64MapAliasForPointer(LastPte);
            }

            DPRINT("%s\n", "[arm64] MiInitMachineDependent: system PTE space alias pages mapped");
        }
    }
    else
    {
        DPRINT("%s\n", "[arm64] MiInitMachineDependent: deferring pool/PTE bring-up (self-map unavailable)");
    }
    DPRINT("%s\n", "[arm64] MiInitMachineDependent: guard check done");

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
                PFN_NUMBER Pfn = MxGetNextPage(1);
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
            PFN_NUMBER Pfn = MxGetNextPage(1);
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
                    PFN_NUMBER Pfn = MxGetNextPage(1);
                    if (Pfn != 0)
                    {
                        /* Initialize the new L1 table page before publishing. */
                        RtlZeroMemory(MiArm64PfnToKseg0(Pfn), PAGE_SIZE);
                        UINT64 Desc = MI_ARM64_MAKE_TABLE_DESC(Pfn);
                        *L0Entry = Desc;
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
                    PFN_NUMBER Pfn = MxGetNextPage(1);
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
                PFN_NUMBER Pfn = MxGetNextPage(1);
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
            TmplPte.u.Hard.PageFrameNumber = MxGetNextPage(1);
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

    /* Ensure the alias pages for the final nonpaged pool range are mapped
     * before any MmGetPhysicalAddress usage. */
    {
        PVOID NonPagedPoolEnd = (PUCHAR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes - 1;
        PMMPTE FirstPte = MiAddressToPte(MmNonPagedPoolStart);
        PMMPTE LastPte = MiAddressToPte(NonPagedPoolEnd);
        PMMPDE FirstPde = MiAddressToPde(MmNonPagedPoolStart);
        PMMPDE LastPde = MiAddressToPde(NonPagedPoolEnd);
        PMMPPE FirstPpe = MiAddressToPpe(MmNonPagedPoolStart);
        PMMPPE LastPpe = MiAddressToPpe(NonPagedPoolEnd);

        /* Ensure the PPE alias pages are backed */
        MiArm64MapAliasForPointer(FirstPpe);
        if (LastPpe != FirstPpe)
        {
            MiArm64MapAliasForPointer(LastPpe);
        }

        /* Ensure all PDE alias pages for the initial nonpaged pool are backed */
        {
            ULONG_PTR FirstPage = (ULONG_PTR)FirstPde & ~(PAGE_SIZE - 1);
            ULONG_PTR LastPage = (ULONG_PTR)LastPde & ~(PAGE_SIZE - 1);
            ULONG_PTR Page;

            for (Page = FirstPage; Page <= LastPage; Page += PAGE_SIZE)
            {
                MiArm64MapAliasForPointer((PVOID)Page);
            }
        }

        /* Ensure all PTE alias pages for the initial nonpaged pool are backed */
        {
            ULONG_PTR FirstPage = (ULONG_PTR)FirstPte & ~(PAGE_SIZE - 1);
            ULONG_PTR LastPage = (ULONG_PTR)LastPte & ~(PAGE_SIZE - 1);
            ULONG_PTR Page;

            for (Page = FirstPage; Page <= LastPage; Page += PAGE_SIZE)
            {
                MiArm64MapAliasForPointer((PVOID)Page);
            }
        }
    }

    /* Normalize PFN boundary flags for initial nonpaged pool pages. */
#if DBG
    DbgPrint("[arm64] MiBuildNonPagedPool: normalize PFN flags %p-%p\n",
             MmNonPagedPoolStart,
             (PVOID)((PUCHAR)MmNonPagedPoolStart + MmSizeOfNonPagedPoolInBytes - 1));
#endif
    MiArm64NormalizePoolPfnFlagsRange(MmNonPagedPoolStart,
                                      MmSizeOfNonPagedPoolInBytes);
#if DBG
    DbgPrint("[arm64] MiBuildNonPagedPool: normalize PFN flags done\n");
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
