/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/mm/page.c
 * PURPOSE:         ARM64 virtual memory helper routines
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>
#include "../include/ke.h"

static
NTSTATUS
MmCreateVirtualMappingUnsafeEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical);

static
BOOLEAN
MiIsPageTablePresent(
    _In_ PVOID Address);

/*
 * MiArm64ReadUserPtePhysically - Walk TTBR0's page tables via KSEG0 and read PTE.
 *
 * This function walks the user page table hierarchy PHYSICALLY using KSEG0
 * direct mapping. It NEVER accesses TTBR0 alias addresses, avoiding all
 * nested fault issues that plague the TTBR0 alias approach.
 *
 * Parameters:
 *   Address   - User-space virtual address to look up
 *   OutPte    - If non-NULL, receives the L3 PTE value (or 0 if not mapped)
 *   OutDepth  - If non-NULL, receives the depth reached:
 *               0 = Invalid TTBR0 or L0 invalid
 *               1 = L0 valid, L1 invalid
 *               2 = L1 valid (or block), L2 invalid
 *               3 = L2 valid (or block), L3 reached
 *               4 = L3 is valid page descriptor
 *
 * Returns:
 *   TRUE if the address is mapped (page exists), FALSE otherwise.
 */
static
BOOLEAN
MiArm64ReadUserPtePhysically(
    _In_ PVOID Address,
    _Out_opt_ PULONG64 OutPte,
    _Out_opt_ PULONG OutDepth)
{
    ULONG64 Ttbr0, RootPa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;

    /* Initialize outputs */
    if (OutPte) *OutPte = 0;
    if (OutDepth) *OutDepth = 0;

    /* Must be a user address */
    if (Address >= MmSystemRangeStart)
        return FALSE;

    /* Read TTBR0 to get the user page table root */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPa = Ttbr0 & 0x0000FFFFFFFFF000ULL;  /* Extract PA, mask ASID bits */
    if (RootPa == 0)
        return FALSE;

    /* Calculate indices for each level */
    L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
    L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
    L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
    L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

    /* Access L0 table via KSEG0 direct mapping */
    L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
    L0Entry = L0Table[L0Idx];

    /* Check L0 entry validity - must be a table descriptor (bits[1:0]=0b11) */
    if ((L0Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    if (OutDepth) *OutDepth = 1;

    /* Access L1 table */
    L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & 0x0000FFFFFFFFF000ULL));
    L1Entry = L1Table[L1Idx];

    /*
     * L1 could be a 1GB block (bits[1:0]=0b01) or table (0b11) or invalid.
     *
     * ARM64 Critical: Block descriptors (1GB/2MB) in user space are from FreeLoader's
     * identity mapping, NOT from the Memory Manager. We must NOT treat them as
     * "present" pages because:
     * 1. They point to wrong physical memory (old identity-mapped addresses)
     * 2. ReactOS MM creates only 4KB page mappings for user addresses
     * 3. Section views need proper L3 PTEs to work correctly
     *
     * Return FALSE for blocks - callers like MmIsPagePresent need to know that
     * no proper MM-created mapping exists at this address.
     */
    if ((L1Entry & 0x1ULL) == 0)
        return FALSE;  /* L1 invalid */
    if ((L1Entry & 0x3ULL) == 0x1ULL)
    {
        /* L1 is 1GB block - FreeLoader identity mapping, treat as not present */
        if (OutPte) *OutPte = L1Entry;
        if (OutDepth) *OutDepth = 1;  /* Stopped at L1 block, not a proper page */
        return FALSE;
    }

    if (OutDepth) *OutDepth = 2;

    /* L1 is table, access L2 */
    L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & 0x0000FFFFFFFFF000ULL));
    L2Entry = L2Table[L2Idx];

    /* L2 could be a 2MB block or table or invalid */
    if ((L2Entry & 0x1ULL) == 0)
        return FALSE;  /* L2 invalid */
    if ((L2Entry & 0x3ULL) == 0x1ULL)
    {
        /* L2 is 2MB block - FreeLoader identity mapping, treat as not present */
        if (OutPte) *OutPte = L2Entry;
        if (OutDepth) *OutDepth = 2;  /* Stopped at L2 block, not a proper page */
        return FALSE;
    }

    if (OutDepth) *OutDepth = 3;

    /* L2 is table, access L3 */
    L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & 0x0000FFFFFFFFF000ULL));
    L3Entry = L3Table[L3Idx];

    if (OutPte) *OutPte = L3Entry;

    /* L3 entry: page descriptor (bits[1:0]=0b11) means page is present (proper 4KB mapping) */
    if ((L3Entry & 0x3ULL) == 0x3ULL)
    {
        if (OutDepth) *OutDepth = 4;
        return TRUE;
    }

    return FALSE;
}

/*
 * ARM64 Cache Invalidation Functions
 *
 * Cache coherency on ARM64 requires careful handling depending on data direction:
 *
 * INCOMING data (DMA/ramdisk populated page, read fault):
 *   The data source (device, ramdisk) has already written to RAM.
 *   CPU cache may have stale/garbage data for the VA.
 *   Use DC IVAC (Invalidate only) - discard cache lines without writeback.
 *   CRITICAL: DC CIVAC is WRONG - it writes back garbage to RAM first!
 *
 * OUTGOING data (CPU wrote, needs visibility to DMA/other observers):
 *   CPU has written data that may still be in cache.
 *   Use DC CIVAC (Clean & Invalidate) - write back dirty lines first.
 *
 * CTR_EL0 layout:
 *   Bits [3:0]   = IminLine (I-cache line size as log2(words))
 *   Bits [19:16] = DminLine (D-cache line size as log2(words))
 *   Line size = 4 << field_value (in bytes)
 */

/*
 * MiInvalidateDCachePageIncoming - Invalidate D-cache for INCOMING data.
 *
 * Use this when the page has been populated by an external source (DMA, ramdisk,
 * PIO) and the CPU needs to see fresh data. This DISCARDS any stale cache
 * contents without writing them back.
 *
 * WARNING: Using this on pages with valid CPU-written data will LOSE that data!
 */
VOID
MiInvalidateDCachePageIncoming(
    _In_ PVOID Address)
{
    ULONG64 Ctr;
    ULONG DcacheLineSize, IcacheLineSize;
    ULONG_PTR Va;

    /* Read CTR_EL0 to get cache line sizes */
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);  /* Bits [19:16] = DminLine */
    IcacheLineSize = 4u << (Ctr & 0xF);          /* Bits [3:0] = IminLine */

    /* Align address to page boundary */
    Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    /* Ensure all prior memory operations complete before cache invalidation */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * DC IVAC (Invalidate by VA to PoC) - invalidate only, no writeback.
     * This is correct for incoming data: discard stale cache, read fresh RAM.
     *
     * Note: DC IVAC is permitted at EL1 regardless of SCTLR_EL1.UCI setting.
     * The UCI bit only affects EL0 access to cache maintenance instructions.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
    {
        __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure D-cache invalidation completes before I-cache ops */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * IC IVAU (Invalidate I-cache by VA to PoU).
     * For executable pages, also invalidate I-cache using correct line size.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += IcacheLineSize)
    {
        __asm__ __volatile__("ic ivau, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure I-cache operations complete and synchronize instruction stream */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

/*
 * MiInvalidateDCachePageOutgoing - Clean & Invalidate D-cache for OUTGOING data.
 *
 * Use this when the CPU has written data that needs to be visible to external
 * observers (DMA, other CPUs). This writes back dirty cache lines to RAM first,
 * then invalidates them.
 */
VOID
MiInvalidateDCachePageOutgoing(
    _In_ PVOID Address)
{
    ULONG64 Ctr;
    ULONG DcacheLineSize, IcacheLineSize;
    ULONG_PTR Va;

    /* Read CTR_EL0 to get cache line sizes */
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);  /* Bits [19:16] = DminLine */
    IcacheLineSize = 4u << (Ctr & 0xF);          /* Bits [3:0] = IminLine */

    /* Align address to page boundary */
    Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    /* Ensure all prior stores complete before cache operations */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * DC CIVAC (Clean and Invalidate by VA to PoC).
     * Writes back any dirty data to RAM, then invalidates the cache line.
     * This ensures CPU writes reach main memory before DMA/others read.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
    {
        __asm__ __volatile__("dc civac, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure D-cache operations complete before I-cache ops */
    __asm__ __volatile__("dsb sy" ::: "memory");

    /*
     * IC IVAU (Invalidate I-cache by VA to PoU).
     * For self-modifying code scenarios, invalidate I-cache using correct line size.
     */
    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += IcacheLineSize)
    {
        __asm__ __volatile__("ic ivau, %0" :: "r"(Va + offset) : "memory");
    }

    /* Ensure I-cache operations complete and synchronize instruction stream */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

/*
 * MiInvalidateDCachePage - Legacy wrapper, uses CIVAC (outgoing) semantics.
 *
 * This maintains backward compatibility with existing callers.
 * New code should use MiInvalidateDCachePageIncoming or MiInvalidateDCachePageOutgoing
 * explicitly based on the data flow direction.
 */
VOID
MiInvalidateDCachePage(
    _In_ PVOID Address)
{
    /* Default to outgoing semantics for backward compatibility */
    MiInvalidateDCachePageOutgoing(Address);
}

/*
 * ARM64-specific helper to get PFN for user addresses by walking the
 * TTBR0 page table hierarchy directly using system PTEs.
 *
 * On ARM64 with TTBR0/TTBR1 split, the kernel's self-mapping (in TTBR1)
 * cannot access user page tables (in TTBR0). We must use the CURRENTLY
 * ACTIVE TTBR0 register to walk the hierarchy, not DirectoryTableBase
 * (which may point to kernel tables for the System process).
 *
 * Returns 0 if the page is not mapped.
 */
static
PFN_NUMBER
MiArm64GetUserPfn(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PFN_NUMBER RootPfn, PpePfn, PdePfn, PtePfn, PagePfn = 0;
    PMMPTE MappingPte;
    PMMPTE MappedPage;
    MMPTE MapPte;
    ULONG PxeIndex, PpeIndex, PdeIndex, PteIndex;
    ULONG64 Ttbr0;

    UNREFERENCED_PARAMETER(Process);
    ASSERT(Address < MmSystemRangeStart);

    /*
     * Get the root table PFN from the CURRENT TTBR0 register.
     * This is critical because for the System process, DirectoryTableBase[0]
     * points to kernel page tables (TTBR1), not user page tables.
     * User address translations ALWAYS go through TTBR0.
     */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPfn = (Ttbr0 & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
    if (RootPfn == 0)
        return 0;

    /* Calculate indices */
    PxeIndex = MiAddressToPxi(Address);
    PpeIndex = (((ULONG64)Address >> PPI_SHIFT) & 0x1FF);
    PdeIndex = (((ULONG64)Address >> PDI_SHIFT) & 0x1FF);
    PteIndex = MiAddressToPti(Address);

    /* Reserve a system PTE for mapping */
    MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!MappingPte)
        return 0;

    /* Map the root table (L0) */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, RootPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L0 (PXE) */
    if (!MappedPage[PxeIndex].u.Hard.Valid)
        goto Cleanup;
    PpePfn = MappedPage[PxeIndex].u.Hard.PageFrameNumber;

    /* Remap to L1 (PPE table) */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = PpePfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L1 (PPE) */
    if (!MappedPage[PpeIndex].u.Hard.Valid)
        goto Cleanup;
    PdePfn = MappedPage[PpeIndex].u.Hard.PageFrameNumber;

    /* Remap to L2 (PDE table) */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = PdePfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L2 (PDE) */
    if (!MappedPage[PdeIndex].u.Hard.Valid)
        goto Cleanup;
    PtePfn = MappedPage[PdeIndex].u.Hard.PageFrameNumber;

    /* Remap to L3 (PTE table) */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = PtePfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Check L3 (PTE) and get the page PFN */
    if (MappedPage[PteIndex].u.Hard.Valid)
        PagePfn = MappedPage[PteIndex].u.Hard.PageFrameNumber;

Cleanup:
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

    return PagePfn;
}

/*
 * ARM64-specific helper to create page table entries in TTBR0's hierarchy
 * and write a demand-zero PTE for a user address.
 *
 * This is necessary because on ARM64 with TTBR0/TTBR1 split:
 * - The self-mapping (via MiAddressToPte) is in TTBR1's hierarchy
 * - User addresses are translated via TTBR0's hierarchy
 * - Writing a PTE via self-mapping only updates TTBR1, not TTBR0
 *
 * This function walks TTBR0 directly and creates page tables as needed,
 * then writes the demand-zero PTE. It also writes to TTBR1's self-mapping
 * to keep it in sync for subsequent kernel operations.
 *
 * Returns TRUE if successful, FALSE on failure.
 */
BOOLEAN
MiArm64WriteDemandZeroPteToTtbr0(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ MMPTE DemandZeroPte)
{
    PFN_NUMBER RootPfn, L1Pfn, L2Pfn, L3Pfn;
    PFN_NUMBER NewPagePfn;
    PMMPTE MappingPte;
    PMMPTE MappedPage;
    MMPTE MapPte, TempPte, NewTableEntry;
    ULONG L0Index, L1Index, L2Index, L3Index;
    ULONG64 Ttbr0;
    KIRQL OldIrql;
    ULONG Color;
    BOOLEAN Success = FALSE;

    UNREFERENCED_PARAMETER(Process);
    ASSERT(Address < MmSystemRangeStart);

    /* Get the root table PFN from current TTBR0 */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPfn = (Ttbr0 & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
    if (RootPfn == 0)
        return FALSE;

    /* Calculate indices for each level */
    L0Index = MiAddressToPxi(Address);
    L1Index = (((ULONG64)Address >> PPI_SHIFT) & 0x1FF);
    L2Index = (((ULONG64)Address >> PDI_SHIFT) & 0x1FF);
    L3Index = MiAddressToPti(Address);

    /* Reserve a system PTE for mapping */
    MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!MappingPte)
        return FALSE;

    /* Build the PTE template for mapping physical pages into kernel space */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, RootPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);

    /* Build the table entry template for new page table descriptors */
    NewTableEntry.u.Long = 0;
    NewTableEntry.u.Hard.Valid = 1;
    NewTableEntry.u.Hard.NotLargePage = 1;  /* Table descriptor */
    NewTableEntry.u.Hard.Accessed = 1;
    NewTableEntry.u.Hard.Shareability = 3;  /* Inner Shareable */

    /*
     * Walk TTBR0 page tables and create missing levels.
     * We work from L0 (root) down to L3 (page table).
     */

    /* === L0 -> L1 === */
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L0Index].u.Long & 1))
    {
        /* L1 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L0 entry pointing to new L1 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L0Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L1Pfn = MappedPage[L0Index].u.Hard.PageFrameNumber;

    /* === L1 -> L2 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L1Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L1Index].u.Long & 1))
    {
        /* L2 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L1 entry pointing to new L2 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L1Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");

    }
    L2Pfn = MappedPage[L1Index].u.Hard.PageFrameNumber;

    /* === L2 -> L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L2Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L2Index].u.Long & 1))
    {
        /* L3 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        /* Write L2 entry pointing to new L3 table */
        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L2Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");

    }
    L3Pfn = MappedPage[L2Index].u.Hard.PageFrameNumber;

    /* === Write final PTE to L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L3Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Write the demand-zero PTE */
    if (MappedPage[L3Index].u.Long == 0)
    {
        MappedPage[L3Index] = DemandZeroPte;
        __asm__ __volatile__("dsb sy" ::: "memory");
        Success = TRUE;

    }

    /* Cleanup */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

    /* TLB flush for the user address */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    return Success;
}

/*
 * ARM64-specific helper to write a PTE value to a user address's page table
 * entry directly in TTBR0's hierarchy.
 *
 * This is needed because on ARM64:
 * - The self-mapping (MiAddressToPte, etc.) is in TTBR1's hierarchy
 * - User addresses are translated via TTBR0's hierarchy
 * - Writing to self-mapping only updates TTBR1, not TTBR0
 *
 * This function walks TTBR0, finds the L3 page table entry for the given
 * user address, and writes the PTE value to it.
 *
 * Returns TRUE on success, FALSE on failure (e.g., page tables don't exist).
 */
BOOLEAN
MiArm64WritePteToTtbr0(
    _In_ PVOID UserVirtualAddress,
    _In_ MMPTE PteValue)
{
    PFN_NUMBER RootPfn, L1Pfn, L2Pfn, L3Pfn;
    PFN_NUMBER NewPagePfn;
    PMMPTE MappingPte;
    PMMPTE MappedPage;
    MMPTE MapPte, TempPte, NewTableEntry;
    ULONG L0Index, L1Index, L2Index, L3Index;
    ULONG64 Ttbr0;
    KIRQL OldIrql;
    ULONG Color;
    BOOLEAN Success = FALSE;

    ASSERT(UserVirtualAddress < MmSystemRangeStart);

    /* Get the root table PFN from current TTBR0 */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPfn = (Ttbr0 & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
    if (RootPfn == 0)
        return FALSE;

    /* Calculate indices for each level */
    L0Index = MiAddressToPxi(UserVirtualAddress);
    L1Index = (((ULONG64)UserVirtualAddress >> PPI_SHIFT) & 0x1FF);
    L2Index = (((ULONG64)UserVirtualAddress >> PDI_SHIFT) & 0x1FF);
    L3Index = MiAddressToPti(UserVirtualAddress);

    /* Reserve a system PTE for mapping */
    MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
    if (!MappingPte)
        return FALSE;

    /* Build the PTE template for mapping physical pages into kernel space */
    MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, RootPfn);
    MI_MAKE_DIRTY_PAGE(&MapPte);

    /* Build the table entry template for new page table descriptors */
    NewTableEntry.u.Long = 0;
    NewTableEntry.u.Hard.Valid = 1;
    NewTableEntry.u.Hard.NotLargePage = 1;  /* Table descriptor */
    NewTableEntry.u.Hard.Accessed = 1;
    NewTableEntry.u.Hard.Shareability = 3;  /* Inner Shareable */

    /*
     * Walk TTBR0 page tables and create missing levels.
     * We work from L0 (root) down to L3 (page table).
     */

    /* === L0 -> L1 === */
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L0Index].u.Long & 1))
    {
        /* L1 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L0Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L1Pfn = MappedPage[L0Index].u.Hard.PageFrameNumber;

    /* === L1 -> L2 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L1Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L1Index].u.Long & 1))
    {
        /* L2 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L1Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L2Pfn = MappedPage[L1Index].u.Hard.PageFrameNumber;

    /* === L2 -> L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L2Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    if (!(MappedPage[L2Index].u.Long & 1))
    {
        /* L3 table doesn't exist - create it */
        OldIrql = MiAcquirePfnLock();
        Color = MI_GET_NEXT_COLOR();
        NewPagePfn = MiRemoveZeroPageSafe(Color);
        if (!NewPagePfn)
        {
            NewPagePfn = MiRemoveAnyPage(Color);
            MiReleasePfnLock(OldIrql);
            MiZeroPhysicalPage(NewPagePfn);
        }
        else
        {
            MiReleasePfnLock(OldIrql);
        }

        TempPte = NewTableEntry;
        TempPte.u.Hard.PageFrameNumber = NewPagePfn;
        MappedPage[L2Index] = TempPte;
        __asm__ __volatile__("dsb sy" ::: "memory");
    }
    L3Pfn = MappedPage[L2Index].u.Hard.PageFrameNumber;

    /* === Write PTE to L3 === */
    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MapPte.u.Hard.PageFrameNumber = L3Pfn;
    MI_WRITE_VALID_PTE(MappingPte, MapPte);
    MappedPage = MiPteToAddress(MappingPte);

    /* Write the PTE value */
    MappedPage[L3Index] = PteValue;
    __asm__ __volatile__("dsb sy" ::: "memory");
    Success = TRUE;

    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
    KeInvalidateTlbEntry(MappedPage);
    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

    /* TLB flush for the user address */
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");

    return Success;
}

static
ULONG
MiProtectionFromPte(
    _In_ MMPTE Pte);

static
BOOLEAN
MmDeleteVirtualMappingEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical);

NTSTATUS
NTAPI
MmCreateVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (!MmIsPageInUse(Page))
    {
        DPRINT1("Page %Ix is not in use\n", Page);
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, FALSE);
}

NTSTATUS
NTAPI
MmCreateVirtualMappingUnsafe(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, FALSE);
}

NTSTATUS
NTAPI
MmCreatePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page)
{
    return MmCreateVirtualMappingUnsafeEx(Process, Address, Protection, Page, TRUE);
}

static
NTSTATUS
MmCreateVirtualMappingUnsafeEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection,
    _In_ PFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical)
{
    PMMPTE PointerPte;
    MMPTE TempPte;
    ULONG ProtectionMask;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);
    ASSERT(ProtectionMask != MM_NOACCESS);
    ASSERT(ProtectionMask != MM_ZERO_ACCESS);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);
        ASSERT(ProtectionMask != MM_WRITECOPY);
        ASSERT(ProtectionMask != MM_EXECUTE_WRITECOPY);

        /*
         * For kernel address mappings, we need to ensure the page table entry
         * (PTE) for the target address is accessible. This means the page table
         * page containing the PTE must be valid.
         *
         * NOTE: We pass MiAddressToPte(Address) - the PTE's virtual address -
         * NOT the target Address itself. MiMakeSystemAddressValid is designed
         * to fault in PAGE TABLE addresses, not arbitrary addresses.
         *
         * If we passed Address here, it would cause infinite recursion during
         * page fault handling for section views:
         *   MmNotPresentFaultSectionView -> MmCreateVirtualMapping ->
         *   MiMakeSystemAddressValid(Address) -> MmAccessFault(Address) ->
         *   MmNotPresentFaultSectionView -> ... (infinite loop)
         */
        MiMakeSystemAddressValid(MiAddressToPte(Address), PsGetCurrentProcess());
    }
    else
    {
        /*
         * ARM64 user-space page table creation.
         *
         * On ARM64, the self-mapping mechanism requires existing page table entries
         * to walk the hierarchy. For a new process, user-space PXE entries (indices
         * 0-255) are zero-initialized. When we need to create a mapping, we must:
         *
         * 1. Check if PXE exists (can access PXE directly as it's in kernel space)
         * 2. If PXE doesn't exist, allocate a PPE page and write PXE
         * 3. Use a system PTE to map the PPE page and check/create PPE entry
         * 4. Use a system PTE to map the PDE page and check/create PDE entry
         *
         * We cannot use self-mapping addresses (MiAddressToPpe, MiAddressToPde) to
         * check validity of intermediate levels because the self-mapping walk would
         * fail on the zero entries.
         */
        PMMPTE MappingPte = NULL;
        PMMPTE MappedPage;
        MMPTE MapPte, TempPte;
        PFN_NUMBER PxePfn = 0, PpePfn = 0, PdePfn = 0, PtePfn = 0;
        ULONG PxeIndex, PpeIndex, PdeIndex;
        KIRQL OldIrql;
        ULONG Color;

        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        MiLockProcessWorkingSet(Process, PsGetCurrentThread());

        /* Get system PTE for temporary mapping */
        MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
        if (!MappingPte)
        {
            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Calculate indices for each level */
        PxeIndex = MiAddressToPxi(Address);
        PpeIndex = (((ULONG64)Address >> PPI_SHIFT) & 0x1FF);
        PdeIndex = (((ULONG64)Address >> PDI_SHIFT) & 0x1FF);

        /*
         * Level 0 (PXE) - CRITICAL: We must map the CURRENTLY ACTIVE TTBR0
         * table, not DirectoryTableBase. On ARM64:
         *
         * - TTBR0 controls user address translation (addresses < 0x0000FFFFFFFFFFFF)
         * - TTBR1 controls kernel address translation (addresses >= 0xFFFF...)
         * - DirectoryTableBase[0] for System/Idle process is set from TTBR1 during
         *   boot, which means it points to KERNEL page tables, not user tables!
         *
         * The CPU uses TTBR0 to translate user addresses, so we MUST write our
         * page table entries to the TTBR0 hierarchy, not DirectoryTableBase.
         *
         * Note: MiAddressToPxe() returns a self-mapping address in TTBR1 (kernel
         * space) which cannot access user page tables in TTBR0.
         */
        {
            ULONG64 Ttbr0Actual;
            __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Actual));
            PxePfn = (Ttbr0Actual & ~(ULONG64)0xFFF) >> PAGE_SHIFT;
        }

        /* Map the user's root table (L0) via system PTE */
        MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, PxePfn);
        MI_MAKE_DIRTY_PAGE(&MapPte);
        MI_WRITE_VALID_PTE(MappingPte, MapPte);
        MappedPage = MiPteToAddress(MappingPte);

        if (!MappedPage[PxeIndex].u.Hard.Valid)
        {
            /* Allocate page for PPE table (L1) */
            OldIrql = MiAcquirePfnLock();
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
            PpePfn = MiRemoveZeroPageSafe(Color);
            if (!PpePfn)
            {
                PpePfn = MiRemoveAnyPage(Color);
                if (!PpePfn)
                {
                    MiReleasePfnLock(OldIrql);
                    /* Cleanup: invalidate mapping, release PTE */
                    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                    KeInvalidateTlbEntry(MappedPage);
                    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                    MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                    return STATUS_NO_MEMORY;
                }
                MiReleasePfnLock(OldIrql);
                MiZeroPhysicalPage(PpePfn);
            }
            else
            {
                MiReleasePfnLock(OldIrql);
            }

            /*
             * Use MiInitializePfnForOtherProcess because we can't use MiInitializePfn -
             * it would try to dereference the self-mapping address to read OriginalPte,
             * which doesn't exist yet. MiInitializePfnForOtherProcess just takes the
             * raw PTE address and parent frame.
             *
             * Pass the self-mapping address for bookkeeping (PteAddress field) but
             * the parent frame is the root table's PFN.
             */
            MiInitializePfnForOtherProcess(PpePfn, MiAddressToPxe(Address), PxePfn);
            Process->NumberOfPrivatePages++;

            /* Write PXE entry into the mapped user root table */
            TempPte = ValidKernelPte;
            TempPte.u.Hard.PageFrameNumber = PpePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MappedPage[PxeIndex] = TempPte;

        }
        else
        {
            /* PXE exists, get the PPE PFN from it */
            PpePfn = MappedPage[PxeIndex].u.Hard.PageFrameNumber;
        }


        /* Invalidate the mapping before reusing the system PTE for PPE level */
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(MappedPage);

        /* Level 1 (PPE) - map the PPE page via system PTE to check/write it */
        MI_MAKE_HARDWARE_PTE_KERNEL(&MapPte, MappingPte, MM_READWRITE, PpePfn);
        MI_MAKE_DIRTY_PAGE(&MapPte);
        MI_WRITE_VALID_PTE(MappingPte, MapPte);
        MappedPage = MiPteToAddress(MappingPte);

        if (!MappedPage[PpeIndex].u.Hard.Valid)
        {
            /* Allocate page for PDE table (L2) */
            BOOLEAN ReleasePfnLock = TRUE;
            {
                extern volatile LONG MiArm64PfnLockDepth[MAXIMUM_PROCESSORS];
                ULONG CpuIndex = KeGetCurrentProcessorNumber();

                /*
                 * Avoid deadlocking on recursive PFN lock acquisition on UP during
                 * early user TTBR0 bring-up. If the PFN lock is already held on this
                 * CPU, reuse it and do not release it here.
                 */
                if (CpuIndex < MAXIMUM_PROCESSORS && MiArm64PfnLockDepth[CpuIndex] > 0)
                {
                    OldIrql = DISPATCH_LEVEL;
                    ReleasePfnLock = FALSE;
                }
                else
                {
                    OldIrql = MiAcquirePfnLock();
                }
            }
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
            PdePfn = MiRemoveZeroPageSafe(Color);
            if (!PdePfn)
            {
                PdePfn = MiRemoveAnyPage(Color);
                if (!PdePfn)
                {
                    if (ReleasePfnLock) MiReleasePfnLock(OldIrql);
                    /* Cleanup: invalidate mapping, release PTE */
                    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                    KeInvalidateTlbEntry(MappedPage);
                    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                    MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                    return STATUS_NO_MEMORY;
                }
                if (ReleasePfnLock) MiReleasePfnLock(OldIrql);
                MiZeroPhysicalPage(PdePfn);
            }
            else
            {
                if (ReleasePfnLock) MiReleasePfnLock(OldIrql);
            }

            /*
             * Use MiInitializePfnForOtherProcess - the PTE address is the self-mapping
             * address (for bookkeeping), and the parent frame is the PPE page.
             */
            MiInitializePfnForOtherProcess(PdePfn, MiAddressToPpe(Address), PpePfn);
            Process->NumberOfPrivatePages++;

            /* Write PPE entry into the mapped page */
            TempPte = ValidKernelPte;
            TempPte.u.Hard.PageFrameNumber = PdePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MappedPage[PpeIndex] = TempPte;

        }
        else
        {
            /* PPE exists, get PDE PFN */
            PdePfn = MappedPage[PpeIndex].u.Hard.PageFrameNumber;
        }

        /* Invalidate the mapping before reusing the system PTE */
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(MappedPage);

        /* Level 2 (PDE) - map the PDE page via system PTE */
        MapPte.u.Hard.PageFrameNumber = PdePfn;
        MI_WRITE_VALID_PTE(MappingPte, MapPte);
        MappedPage = MiPteToAddress(MappingPte);

        /*
         * ARM64: Check if the existing PDE is a 2MB block descriptor (from FreeLoader's
         * identity mapping) rather than a table descriptor. If so, we must break it up.
         *
         * On ARM64:
         * - Block descriptor (2MB at L2): Valid=1, NotLargePage=0, bits[1:0]=01
         * - Table descriptor: Valid=1, NotLargePage=1, bits[1:0]=11
         * - Invalid: Valid=0, bit[0]=0
         *
         * FreeLoader creates 2MB block descriptors for identity mapping in user space.
         * If the kernel tries to create a 4KB page mapping in the same range, we must:
         * 1. Detect the block descriptor (Valid=1 but NotLargePage=0)
         * 2. Clear it (we don't need to preserve the identity mapping)
         * 3. Allocate a fresh L3 page table
         */
        if (MappedPage[PdeIndex].u.Hard.Valid && !MappedPage[PdeIndex].u.Hard.NotLargePage)
        {
            /* This is a 2MB block descriptor - clear it */
            /* Invalidate TLB before clearing the block descriptor */
            __asm__ __volatile__("dsb ishst\n\t"
                                 "tlbi vaale1is, %0\n\t"
                                 "dsb ish\n\t"
                                 "isb"
                                 :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");

            /* Clear the 2MB block descriptor */
            MappedPage[PdeIndex].u.Long = 0;

            /* Ensure the clear is visible before proceeding */
            __asm__ __volatile__("dsb ishst" ::: "memory");

            /* Invalidate the entire 2MB range that was covered by this block */
            __asm__ __volatile__("tlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
        }

        if (!MappedPage[PdeIndex].u.Hard.Valid)
        {
            /* Allocate page for PTE table (L3) */
            OldIrql = MiAcquirePfnLock();
            Color = MI_GET_NEXT_PROCESS_COLOR(Process);
            PtePfn = MiRemoveZeroPageSafe(Color);
            if (!PtePfn)
            {
                PtePfn = MiRemoveAnyPage(Color);
                if (!PtePfn)
                {
                    MiReleasePfnLock(OldIrql);
                    MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                    KeInvalidateTlbEntry(MappedPage);
                    MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
                    MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
                    return STATUS_NO_MEMORY;
                }
                MiReleasePfnLock(OldIrql);
                MiZeroPhysicalPage(PtePfn);
            }
            else
            {
                MiReleasePfnLock(OldIrql);
            }

            /*
             * Use MiInitializePfnForOtherProcess - the PTE address is the self-mapping
             * address (for bookkeeping), and the parent frame is the PDE page.
             */
            MiInitializePfnForOtherProcess(PtePfn, MiAddressToPde(Address), PdePfn);
            Process->NumberOfPrivatePages++;

            /* Write PDE entry */
            TempPte = ValidKernelPte;
            TempPte.u.Hard.PageFrameNumber = PtePfn;
            TempPte.u.Hard.Owner = 0;  /* No APTable restriction */
            MappedPage[PdeIndex] = TempPte;
        }
        else
        {
            /* PDE exists, get PTE PFN */
            PtePfn = MappedPage[PdeIndex].u.Hard.PageFrameNumber;
        }

        /* Invalidate the mapping before reusing for PTE table */
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(MappedPage);

        /*
         * Now map the PTE table page via system PTE to write the final PTE.
         * We cannot use the self-mapping (MiAddressToPte) because the self-mapping
         * chain doesn't have valid entries for user address page tables.
         */
        {
            ULONG PteIndex = MiAddressToPti(Address);
            MMPTE FinalPte;

            MapPte.u.Hard.PageFrameNumber = PtePfn;
            MI_WRITE_VALID_PTE(MappingPte, MapPte);
            MappedPage = MiPteToAddress(MappingPte);

            /*
             * Build the final PTE for the user page.
             *
             * CRITICAL ARM64 FIX: We cannot use MI_MAKE_HARDWARE_PTE directly because
             * it calls MiDetermineUserGlobalPteMask() which checks if the PTE address
             * is in user PTE space to set the Owner bit. However, we are writing to
             * user page tables via a SYSTEM PTE (MappedPage), so the PTE address is
             * in kernel space, and MiDetermineUserGlobalPteMask would NOT set Owner=1.
             *
             * Without Owner=1 (AP[0]=1), the ARM64 MMU treats the page as EL1-only,
             * preventing user mode (EL0) from accessing it. This causes writes to
             * appear to fail (reads return garbage/0xAA poison bytes).
             *
             * Solution: Manually build the PTE with proper user-accessible attributes:
             * - Valid=1, NotLargePage=1 for L3 page descriptor (bits [1:0]=0b11)
             * - Owner=1 for user access (AP[0]=1 allows EL0 access)
             * - NotDirty=0 for writable pages (AP[1]=0 allows writes)
             * - Accessed=1 (AF bit) required by ARM64
             * - Protection mask for cache and execute permissions
             */
            FinalPte.u.Long = 0;
            FinalPte.u.Hard.Valid = 1;
            FinalPte.u.Hard.NotLargePage = 1;  /* ARM64 L3 page descriptor */
            FinalPte.u.Hard.Owner = 1;         /* User accessible (AP[0]=1) */
            FinalPte.u.Hard.Accessed = 1;      /* Access Flag must be set */
            FinalPte.u.Hard.Shareability = 3;  /* Inner Shareable for SMP coherency */
            FinalPte.u.Hard.PageFrameNumber = Page;
            FinalPte.u.Long |= MmProtectToPteMask[ProtectionMask];

            /*
             * ARM64 Dirty Bit Management:
             *
             * On ARM64, the NotDirty bit (bit 7, AP[1]) controls both write permission
             * and dirty tracking:
             * - NotDirty=0 (AP[1]=0): Page is read-write, considered "dirty"
             * - NotDirty=1 (AP[1]=1): Page is read-only, considered "not dirty"
             *
             * For WRITABLE pages: Set NotDirty=0 to allow writes.
             * For READ-ONLY pages: Set NotDirty=1 to mark as clean. This is CRITICAL
             * because MmDeleteVirtualMapping checks NotDirty to determine if the page
             * was written to. If we leave NotDirty=0 for read-only pages, they will
             * incorrectly report as dirty when unmapped, causing assertions in
             * MmUnsharePageEntrySectionSegment.
             */
            if (FinalPte.u.Hard.Writable)
            {
                MI_MAKE_DIRTY_PAGE(&FinalPte);  /* NotDirty=0 for writable */
            }
            else
            {
                /* Read-only page: set NotDirty=1 to mark as clean */
                FinalPte.u.Hard.NotDirty = 1;
            }

            if (!IsPhysical)
            {
                KIRQL PfnOldIrql = MiAcquirePfnLock();
                PMMPFN Pfn1 = MiGetPfnEntry(Page);

                Pfn1->u2.ShareCount++;
                Pfn1->u3.e1.PageLocation = ActiveAndValid;
                MiReleasePfnLock(PfnOldIrql);
            }

            /*
             * Check for mapping collision.
             *
             * ARM64 Cycle 57: FreeLoader creates identity mappings in user space (TTBR0)
             * for its own use during boot. When the kernel starts, these identity mappings
             * may still exist. If a section view is mapped at a VA that has an existing
             * identity mapping, we must clear the old PTE before installing the new one.
             *
             * This handles the case where:
             * - FreeLoader identity-mapped physical address 0xFF000000
             * - Kernel maps ntdll.dll section view at VA 0xFF000000
             * - Old PTE would cause reads to return identity-mapped (stale) data
             *
             * Solution: Invalidate TLB and clear the existing PTE before writing the new one.
             */
            if (MappedPage[PteIndex].u.Long != 0)
            {
                DPRINT1("[arm64] Clearing stale PTE at %p (old PTE=0x%llx) - FreeLoader identity mapping?\n",
                        Address, MappedPage[PteIndex].u.Long);

                /* Invalidate TLB for this VA first - use VAALE1IS to flush ALL ASIDs including Global entries */
                __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");

                /* Also invalidate the D-cache to prevent stale data reads */
                {
                    ULONG64 Ctr;
                    ULONG DcacheLineSize;
                    ULONG_PTR Va, EndVa;

                    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
                    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

                    Va = (ULONG_PTR)Address;
                    EndVa = Va + PAGE_SIZE;
                    while (Va < EndVa)
                    {
                        __asm__ __volatile__("dc ivac, %0" :: "r"(Va) : "memory");
                        Va += DcacheLineSize;
                    }
                    __asm__ __volatile__("dsb ish" ::: "memory");
                }

                /* Now clear the old PTE */
                MappedPage[PteIndex].u.Long = 0;
            }

            /* Write the final PTE */
            MappedPage[PteIndex] = FinalPte;

            /* DEBUG: Log user PTE write for problematic address range */
            if ((ULONG_PTR)Address >= 0x30000 && (ULONG_PTR)Address < 0x50000)
            {
                ULONG64 ReadbackPte = MappedPage[PteIndex].u.Long;
                DPRINT1("[arm64] MmCreateVM-USER: addr=%p PteIdx=%u PtePfn=0x%lx wrote=0x%llx readback=0x%llx\n",
                        Address, PteIndex, (unsigned long)PtePfn, (unsigned long long)FinalPte.u.Long, (unsigned long long)ReadbackPte);
            }

            /*
             * Cycle 53: ARM64 cache maintenance after PTE update for user-space sections.
             *
             * CRITICAL: When reusing a VA for different section mappings (e.g., 0x40000 in System process),
             * we must invalidate TLB and D-cache to prevent reading stale data from the previous
             * physical page that was mapped at this VA.
             *
             * This fixes the root cause identified in Cycle 52:
             * - Stale cache data (0x3F 0xE5...) exists BEFORE RtlImageNtHeaderEx reads the header
             * - The cache is poisoned when PTEs are created/updated for section mappings
             * - We must flush at PTE write time, not later when reading data
             *
             * Order of operations:
             * 1. Write PTE (mapping is now active)
             * 2. Ensure PTE write completes (DSB)
             * 3. Invalidate TLB entry for this specific VA (removes stale translation)
             * 4. Invalidate D-cache for this page (removes stale data)
             * 5. Cleanup system PTE
             * 6. Final barrier synchronization
             */
            __asm__ __volatile__("dsb sy" ::: "memory");

            /* 1. Invalidate TLB entry for this VA - use VAALE1IS to flush ALL ASIDs including Global entries.
             * ARM64 TLBI encoding: The operand contains VA[63:12] in bits [43:0].
             * This means we must shift the VA right by 12 bits (PAGE_SHIFT).
             * Masking lower bits is NOT correct - we must SHIFT.
             * VAALE1IS = VA, All ASIDs, Last-level, EL1, Inner Shareable
             */
            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> PAGE_SHIFT) : "memory");
            __asm__ __volatile__("dsb ish" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");

            /* 2. Invalidate D-cache for this page */
            {
                ULONG64 Ctr;
                ULONG DcacheLineSize;
                ULONG_PTR Va;

                /* Read CTR_EL0 to get D-cache line size */
                __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
                DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

                /* Invalidate D-cache for the user address */
                Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

                for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
                {
                    __asm__ __volatile__("dc civac, %0" :: "r"(Va + offset) : "memory");
                }

                /* Ensure cache operations complete */
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");
            }

            /* Cleanup system PTE */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(MappedPage);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

            /*
             * ARM64 requires aggressive TLB invalidation after modifying page tables.
             * The CPU can cache "negative" translation results (translation faults),
             * so we must ensure all levels of the page table walk are re-fetched.
             *
             * Use a full EL1 TLB flush (vmalle1is) to invalidate all cached translations
             * including any cached translation fault entries from the earlier page fault.
             * The sequence is:
             * 1. DSB SY - Ensure all page table writes are complete
             * 2. TLBI VMALLE1IS - Broadcast full TLB invalidation to all cores
             * 3. DSB SY - Wait for TLB invalidation to complete
             * 4. ISB - Synchronize instruction stream
             */
            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
            __asm__ __volatile__("dsb sy" ::: "memory");
            __asm__ __volatile__("isb" ::: "memory");

            /*
             * No TTBR0 switch needed - we now write page tables directly to the
             * TTBR0 hierarchy (read at the start of this function). This ensures
             * the mapping is immediately visible to the CPU for user address
             * translation without needing to switch TTBR0.
             */

            /*
             * Skip MiIncrementPageTableReferences - it uses self-mapping which
             * doesn't work for user addresses. The PFN reference counts were
             * already set up by MiInitializePfnForOtherProcess.
             *
             * TODO: If we need proper page table reference tracking, we would
             * need to manually increment the UsedPageTableEntries count in the
             * PTE table page's PFN entry.
             */

            MiUnlockProcessWorkingSet(Process, PsGetCurrentThread());
        }

        return STATUS_SUCCESS;
    }

    /* Kernel address path - uses self-mapping */
    PointerPte = MiAddressToPte(Address);
    MI_MAKE_HARDWARE_PTE(&TempPte, PointerPte, ProtectionMask, Page);

    if (!IsPhysical)
    {
        KIRQL OldIrql = MiAcquirePfnLock();
        PMMPFN Pfn1 = MiGetPfnEntry(Page);

        Pfn1->u2.ShareCount++;
        Pfn1->u3.e1.PageLocation = ActiveAndValid;
        MiReleasePfnLock(OldIrql);
    }

    /*
     * ARM64 Cache Coherency for Kernel Mappings - CRITICAL FIX (Cycle 39).
     *
     * For REMAPPINGS (old PTE != 0), we must invalidate the cache before
     * writing the new PTE, because the cache may contain stale data from
     * the previous physical page that was mapped at this VA.
     *
     * For NEW mappings (old PTE == 0), we MUST NOT use DC CIVAC because:
     * 1. The VA has no valid translation - DC CIVAC will cause a Data Abort
     * 2. There's no stale cache data for an unmapped VA anyway
     *
     * Original problem this fixed:
     * When mapping ramdisk pages that were written by the bootloader, if the VA
     * was previously mapped to a DIFFERENT physical page, the cache could have
     * stale data for this VA. This caused "Invalid image DOS signature" errors.
     *
     * DC CIVAC (Clean and Invalidate by VA to PoC):
     * - Writes back any dirty data (preserves previous mappings' writes)
     * - Invalidates cache lines for this VA
     * - Forces subsequent reads to fetch from physical memory
     * - REQUIRES a valid VA translation - faults on unmapped addresses!
     *
     * Order of operations:
     * 1. Check if current PTE is valid (remapping case)
     * 2. If valid: DC CIVAC (invalidate cache for VA) - BEFORE PTE write
     * 3. DSB ISH (ensure cache ops complete)
     * 4. Write new PTE (make mapping valid)
     * 5. TLBI (invalidate TLB)
     * 6. Return to faulting instruction which will now see correct data
     */
    {
        MMPTE OldPte;
        PMMPTE CheckPte;

        /*
         * Read the current PTE to check if this is a remapping.
         * For user addresses, we need to check via TTBR0 alias.
         * For kernel addresses, we can use the self-map.
         */
        if (Address < MmSystemRangeStart)
        {
            CheckPte = MiAddressToPteTtbr0(Address);
        }
        else
        {
            CheckPte = PointerPte;
        }

        OldPte.u.Long = CheckPte->u.Long;

        /*
         * Only perform cache invalidation if there's an existing valid mapping.
         * DC CIVAC requires a valid VA translation - it will fault on unmapped addresses!
         */
        if (OldPte.u.Long != 0 && OldPte.u.Hard.Valid)
        {
            ULONG64 Ctr;
            ULONG DcacheLineSize;
            ULONG_PTR Va;

            /* Read CTR_EL0 to get D-cache line size */
            __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
            DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

            /* Invalidate D-cache for the target virtual address */
            Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

            /* Invalidate each cache line in the page */
            for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
            {
                __asm__ __volatile__("dc civac, %0" :: "r"(Va + offset) : "memory");
            }

            /* Ensure cache operations complete BEFORE writing PTE */
            __asm__ __volatile__("dsb ish" ::: "memory");
        }
    }

    /*
     * ARM64 TTBR0 Alias Fix: Write user-space PTEs via TTBR0 alias.
     *
     * On ARM64, user addresses use TTBR0's page tables while kernel addresses
     * use TTBR1's page tables. The self-map at PTE_BASE is in TTBR1's hierarchy.
     *
     * L0[494] in TTBR1 now points to TTBR0's L0 page (the "TTBR0 alias").
     * For user addresses, MiAddressToPteTtbr0() goes through this alias to
     * correctly access TTBR0's page table hierarchy.
     *
     * This replaces the old MiArm64WritePteToTtbr0() workaround.
     */
    if (Address < MmSystemRangeStart)
    {
        /* User address: Write to TTBR0's page tables via the TTBR0 alias */
        PMMPTE Ttbr0Pte = MiAddressToPteTtbr0(Address);
        ULONG_PTR OldVal;

        /* DEBUG: Log user PTE write for problematic address range */
        if ((ULONG_PTR)Address >= 0x30000 && (ULONG_PTR)Address < 0x50000)
        {
            ULONG64 Ttbr0Val, Ttbr1Val;
            __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Val));
            __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1Val));
            DPRINT1("[arm64] MmCreateVM: USER addr=%p Pte@%p TTBR0=0x%llx TTBR1=0x%llx NewPte=0x%llx\n",
                    Address, Ttbr0Pte,
                    (unsigned long long)Ttbr0Val,
                    (unsigned long long)Ttbr1Val,
                    (unsigned long long)TempPte.u.Long);
        }

        OldVal = InterlockedExchangePte(Ttbr0Pte, TempPte.u.Long);
        if (OldVal != 0)
        {
            DPRINT1("User mapping collision at %p (old=0x%llx)\n", Address, (unsigned long long)OldVal);
            KeBugCheck(MEMORY_MANAGEMENT);
        }

        /* DEBUG: Verify PTE was written correctly */
        if ((ULONG_PTR)Address >= 0x30000 && (ULONG_PTR)Address < 0x50000)
        {
            ULONG_PTR Readback = *(volatile ULONG_PTR *)Ttbr0Pte;
            DPRINT1("[arm64] MmCreateVM: USER PTE readback=0x%llx (expected=0x%llx)\n",
                    (unsigned long long)Readback,
                    (unsigned long long)TempPte.u.Long);
        }
    }
    else
    {
        ULONG_PTR OldPteValue;

        /* Kernel address: Self-Map is valid for TTBR1 */
        OldPteValue = InterlockedExchangePte(PointerPte, TempPte.u.Long);

        /* Debug: For SYSCACHE, log the PTE write */
        if ((ULONG_PTR)Address >= 0xFFFFF98000000000ULL &&
            (ULONG_PTR)Address < 0xFFFFFA8000000000ULL)
        {
            DPRINT1("[arm64] MmCreateVirtualMapping: SYSCACHE VA=%p PTE@%p OldPte=0x%llx NewPte=0x%llx PFN=0x%llx\n",
                    Address, PointerPte, (ULONG64)OldPteValue, (ULONG64)TempPte.u.Long,
                    (ULONG64)TempPte.u.Hard.PageFrameNumber);

            /* Verify the PTE was written correctly */
            {
                MMPTE Readback;
                Readback.u.Long = *(volatile ULONG_PTR *)PointerPte;
                DPRINT1("[arm64] MmCreateVirtualMapping: SYSCACHE PTE readback=0x%llx Valid=%d\n",
                        (ULONG64)Readback.u.Long, (int)Readback.u.Hard.Valid);
            }
        }

        if (OldPteValue != 0)
        {
            DPRINT1("Mapping collision at %p\n", Address);
            KeBugCheck(MEMORY_MANAGEMENT);
        }
    }

    /*
     * ARM64: Invalidate the TLB entry for this address.
     * Even for new mappings (where old PTE was 0), the CPU might have a
     * "negative" TLB entry cached from the page fault that triggered this
     * mapping. We must invalidate it so the CPU picks up the new mapping.
     */
    KeInvalidateTlbEntry(Address);

    /*
     * ARM64: D-cache invalidation for ALL new mappings.
     *
     * CRITICAL FIX: Even when the old PTE was 0 (no previous mapping),
     * the D-cache may contain stale data from a MUCH EARLIER mapping at
     * the same VA. The sequence can be:
     *
     * 1. VA 0x40000 mapped to physical page A (long time ago)
     * 2. CPU caches data from page A in D-cache for VA 0x40000
     * 3. VA 0x40000 unmapped (PTE cleared to 0)
     * 4. D-cache STILL has stale data for VA 0x40000!
     * 5. VA 0x40000 now mapped to physical page B
     * 6. Without cache invalidation, reads return stale data from page A
     *
     * We must ALWAYS invalidate D-cache after creating a new mapping to
     * ensure reads return data from the CURRENT physical page.
     *
     * Now that the PTE is valid and TLB is updated, we can use DC IVAC
     * (Invalidate by VA to PoC) which will correctly translate the VA.
     */
    if (Address < MmSystemRangeStart)
    {
        ULONG64 Ctr;
        ULONG DcacheLineSize;
        ULONG_PTR Va, EndVa;

        /* Get D-cache line size from CTR_EL0 */
        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
        DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

        /* Invalidate D-cache for the entire page */
        Va = (ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1);
        EndVa = Va + PAGE_SIZE;

        while (Va < EndVa)
        {
            __asm__ __volatile__("dc ivac, %0" :: "r"(Va) : "memory");
            Va += DcacheLineSize;
        }

        /* Ensure cache operations complete */
        __asm__ __volatile__("dsb ish" ::: "memory");
    }

    /* Debug: For SYSCACHE, verify PTE is still valid after TLB flush */
    if ((ULONG_PTR)Address >= 0xFFFFF98000000000ULL &&
        (ULONG_PTR)Address < 0xFFFFFA8000000000ULL)
    {
        MMPTE FinalPte;
        FinalPte.u.Long = *(volatile ULONG_PTR *)PointerPte;
        DPRINT1("[arm64] MmCreateVirtualMapping: SYSCACHE final PTE=0x%llx Valid=%d\n",
                (ULONG64)FinalPte.u.Long, (int)FinalPte.u.Hard.Valid);
    }

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
MmDeleteVirtualMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    return MmDeleteVirtualMappingEx(Process, Address, WasDirty, Page, FALSE);
}

BOOLEAN
NTAPI
MmDeletePhysicalMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page)
{
    return MmDeleteVirtualMappingEx(Process, Address, WasDirty, Page, TRUE);
}

static
BOOLEAN
MmDeleteVirtualMappingEx(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_opt_ PBOOLEAN WasDirty,
    _Out_opt_ PPFN_NUMBER Page,
    _In_ BOOLEAN IsPhysical)
{
    MMPTE OldPte;
    BOOLEAN ValidPde = FALSE;

    OldPte.u.Long = 0;

    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);

    if (Process == NULL)
    {
        ASSERT(Address >= MmSystemRangeStart);

        /*
         * For kernel addresses, check if the page table hierarchy is valid.
         * We cannot call MiMakeSystemAddressValid(Address) here because:
         * 1. This function is used during page fault cleanup/unmapping
         * 2. MiMakeSystemAddressValid would try to fault in the address
         * 3. This would cause infinite recursion
         *
         * Instead, use MiIsPdeForAddressValid to check without faulting.
         * If page tables don't exist, there's nothing to delete.
         */
        ValidPde = MiIsPdeForAddressValid(Address);
        if (ValidPde)
        {
            PMMPTE PointerPte = MiAddressToPte(Address);

            OldPte.u.Long = PointerPte->u.Long;
            if (OldPte.u.Hard.Valid)
            {
                MiInvalidateDCachePage(Address);
            }
            OldPte.u.Long = InterlockedExchangePte(PointerPte, 0);
            KeInvalidateTlbEntry(Address);
        }
    }
    else
    {
        ASSERT(Address < MmSystemRangeStart);
        ASSERT(Process == PsGetCurrentProcess());

        /*
         * ARM64 TTBR0 Alias Avoidance: Delete user PTEs via physical walking.
         *
         * The TTBR0 alias addresses are broken on ARM64. We must access
         * user page tables via KSEG0 direct physical mapping.
         */
        {
            ULONG64 PteValue;
            ULONG Depth;

            /* Walk TTBR0 page tables physically and get the L3 PTE */
            if (MiArm64ReadUserPtePhysically(Address, &PteValue, &Depth) && Depth >= 3)
            {
                OldPte.u.Long = PteValue;
                ValidPde = TRUE;

                /* If PTE is valid, clear it via physical memory */
                if (OldPte.u.Hard.Valid)
                {
                    ULONG64 Ttbr0, RootPa;
                    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
                    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
                    ULONG64 L0Entry, L1Entry, L2Entry;

                    /* Invalidate D-cache before clearing PTE */
                    MiInvalidateDCachePage(Address);

                    /* Walk to L3 and clear the PTE */
                    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
                    RootPa = Ttbr0 & 0x0000FFFFFFFFF000ULL;

                    L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
                    L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
                    L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
                    L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

                    L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
                    L0Entry = L0Table[L0Idx];
                    L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & 0x0000FFFFFFFFF000ULL));
                    L1Entry = L1Table[L1Idx];

                    /* Check for 1GB block (shouldn't happen for user space) */
                    if ((L1Entry & 0x3ULL) == 0x3ULL)
                    {
                        L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & 0x0000FFFFFFFFF000ULL));
                        L2Entry = L2Table[L2Idx];

                        /* Check for 2MB block */
                        if ((L2Entry & 0x3ULL) == 0x3ULL)
                        {
                            L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & 0x0000FFFFFFFFF000ULL));

                            /* Clear the L3 PTE atomically */
                            __atomic_exchange_n(&L3Table[L3Idx], 0, __ATOMIC_SEQ_CST);

                            /* Full barrier and TLB invalidation */
                            __asm__ __volatile__("dsb ish" ::: "memory");
                            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)Address >> 12) : "memory");
                            __asm__ __volatile__("dsb ish" ::: "memory");
                            __asm__ __volatile__("isb" ::: "memory");
                        }
                    }
                }
            }
            else if (Depth >= 3)
            {
                /* Page table exists but PTE is not valid - nothing to delete */
                OldPte.u.Long = PteValue;
                ValidPde = TRUE;
            }
        }
    }

    if (ValidPde && OldPte.u.Hard.Valid)
    {
        /* Full cache and TLB flush for coherency */
        __asm__ __volatile__("ic ialluis" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");
    }

    if (OldPte.u.Long != 0)
    {
        if (WasDirty)
            *WasDirty = (OldPte.u.Hard.Valid && (OldPte.u.Hard.NotDirty == 0));
        if (Page)
            *Page = OldPte.u.Hard.PageFrameNumber;
    }
    else
    {
        if (WasDirty)
            *WasDirty = FALSE;
        if (Page)
            *Page = 0;
    }

    if (Process != NULL)
    {
        /* ARM64: Skip page table reference tracking for now - complex with physical walking */

        if (!IsPhysical && OldPte.u.Hard.Valid)
        {
            KIRQL OldIrql = MiAcquirePfnLock();
            PMMPFN Pfn1 = MiGetPfnEntry(OldPte.u.Hard.PageFrameNumber);

            ASSERT(Pfn1->u2.ShareCount > 0);
            if (--Pfn1->u2.ShareCount == 0)
                Pfn1->u3.e1.PageLocation = TransitionPage;

            MiReleasePfnLock(OldIrql);
        }
    }

    return OldPte.u.Long != 0;
}

NTSTATUS
NTAPI
MmCreatePageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ SWAPENTRY SwapEntry)
{
    PMMPTE PointerPte;

    ASSERT(Process != NULL);
    ASSERT(Address < MmSystemRangeStart);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    MiMakePdeExistAndMakeValid(MiAddressToPdeSafe(Address), Process, MM_NOIRQL);

    PointerPte = MiAddressToPteSafe(Address);

    if (PointerPte->u.Hard.Valid)
    {
        MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
        return STATUS_CONFLICTING_ADDRESSES;
    }

    PointerPte->u.Long = 0;
    PointerPte->u.Soft.PageFileLow = SwapEntry & 0xF;
    PointerPte->u.Soft.PageFileHigh = SwapEntry >> 4;
    PointerPte->u.Soft.Prototype = 0;
    PointerPte->u.Soft.Protection = MM_READWRITE;

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    return STATUS_SUCCESS;
}

VOID
NTAPI
MmDeletePageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Inout_ SWAPENTRY *SwapEntry)
{
    PMMPTE PointerPte;
    MMPTE OldPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    PointerPte = MiAddressToPteSafe(Address);

    OldPte.u.Long = InterlockedExchangePte(PointerPte, 0);
    if (!FlagOn(OldPte.u.Long, 0x800) || OldPte.u.Hard.Valid)
    {
        DPRINT1("Expected pagefile PTE at %p\n", Address);
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    *SwapEntry = (SWAPENTRY)(((ULONG64)OldPte.u.Soft.PageFileHigh << 4) |
                              OldPte.u.Soft.PageFileLow);

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

VOID
NTAPI
MmGetPageFileMapping(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ SWAPENTRY *SwapEntry)
{
    PMMPTE PointerPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    MiLockProcessWorkingSetShared(Process, PsGetCurrentThread());
    PointerPte = MiAddressToPteSafe(Address);

    if (!FlagOn(PointerPte->u.Long, 0x800) || PointerPte->u.Hard.Valid)
        *SwapEntry = 0;
    else
        *SwapEntry = (SWAPENTRY)(((ULONG64)PointerPte->u.Soft.PageFileHigh << 4) |
                                  PointerPte->u.Soft.PageFileLow);

    MiUnlockProcessWorkingSetShared(Process, PsGetCurrentThread());
}

BOOLEAN
NTAPI
MmIsPagePresent(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if the page table hierarchy is valid.
         * We must NOT call MiMakeSystemAddressValid(Address) here because:
         * 1. This function is called during page fault handling to check if
         *    a page is already present.
         * 2. MiMakeSystemAddressValid would try to fault in the page,
         *    causing infinite recursion.
         * 3. If the PDE is not valid, the page cannot be present.
         *
         * Use MiIsPdeForAddressValid to check page table validity without
         * causing recursive faults.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            /* No valid page table for this address - page cannot be present */
            return FALSE;
        }

        return MiAddressToPte(Address)->u.Hard.Valid != 0;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance Fix:
     *
     * PROBLEM: The TTBR0 alias addresses (PTE_BASE_TTBR0, etc.) are fundamentally
     * broken. Accessing them causes nested page faults that lead to WS lock
     * recursion and assertion failures.
     *
     * SOLUTION: Walk TTBR0's page tables PHYSICALLY via KSEG0 direct mapping.
     * This avoids all TTBR0 alias addresses and cannot cause nested faults.
     *
     * We don't need any locks for this because:
     * 1. We're only reading page table entries (no modification)
     * 2. Physical memory access via KSEG0 doesn't involve page table walks
     * 3. Even if the page tables change during our read, we'll get a consistent
     *    snapshot (either old or new state, both valid)
     */
    {
        ULONG64 Ttbr0, RootPa;
        volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
        ULONG L0Idx, L1Idx, L2Idx, L3Idx;
        ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;

        /* Read TTBR0 to get the user page table root */
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
        RootPa = Ttbr0 & 0x0000FFFFFFFFF000ULL;  /* Extract PA, mask ASID bits */
        if (RootPa == 0)
            return FALSE;

        /* Calculate indices for each level */
        L0Idx = ((ULONG64)(ULONG_PTR)Address >> 39) & 0x1FF;
        L1Idx = ((ULONG64)(ULONG_PTR)Address >> 30) & 0x1FF;
        L2Idx = ((ULONG64)(ULONG_PTR)Address >> 21) & 0x1FF;
        L3Idx = ((ULONG64)(ULONG_PTR)Address >> 12) & 0x1FF;

        /* Access L0 table via KSEG0 direct mapping */
        L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
        L0Entry = L0Table[L0Idx];

        /* Check L0 entry validity - must be a table descriptor (bits[1:0]=0b11) */
        if ((L0Entry & 0x3ULL) != 0x3ULL)
            return FALSE;

        /* Access L1 table */
        L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & 0x0000FFFFFFFFF000ULL));
        L1Entry = L1Table[L1Idx];

        /*
         * L1 could be a 1GB block (bits[1:0]=0b01) or table (0b11) or invalid.
         *
         * ARM64 Critical Fix: Block descriptors (1GB or 2MB) in user space are
         * from FreeLoader's identity mapping, NOT from the Memory Manager.
         * ReactOS MM creates only 4KB page mappings for user addresses.
         *
         * We MUST return FALSE for block descriptors so that:
         * 1. MmNotPresentFaultSectionView will create proper section mappings
         * 2. The section data gets loaded instead of reading stale identity-mapped memory
         *
         * The old code returned TRUE for blocks, which caused fs_rec.sys loading to fail
         * because MmNotPresentFaultSectionView thought the page was already present.
         */
        if ((L1Entry & 0x1ULL) == 0)
            return FALSE;  /* L1 invalid */
        if ((L1Entry & 0x3ULL) == 0x1ULL)
            return FALSE;  /* L1 is 1GB block - FreeLoader identity map, treat as not present */

        /* L1 is table, access L2 */
        L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & 0x0000FFFFFFFFF000ULL));
        L2Entry = L2Table[L2Idx];

        /* L2 could be a 2MB block or table or invalid */
        if ((L2Entry & 0x1ULL) == 0)
            return FALSE;  /* L2 invalid */
        if ((L2Entry & 0x3ULL) == 0x1ULL)
            return FALSE;  /* L2 is 2MB block - FreeLoader identity map, treat as not present */

        /* L2 is table, access L3 */
        L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & 0x0000FFFFFFFFF000ULL));
        L3Entry = L3Table[L3Idx];

        /* L3 entry: page descriptor (bits[1:0]=0b11) means page is present (proper 4KB mapping) */
        {
            BOOLEAN Result = (L3Entry & 0x3ULL) == 0x3ULL;

            /* DEBUG: Log for problem address range */
            if ((ULONG_PTR)Address >= 0x30000 && (ULONG_PTR)Address < 0x50000)
            {
                DPRINT1("[arm64] MmIsPagePresent: addr=%p L0[%u]=0x%llx L1[%u]=0x%llx L2[%u]=0x%llx L3[%u]=0x%llx Result=%d\n",
                        Address, L0Idx, (unsigned long long)L0Entry,
                        L1Idx, (unsigned long long)L1Entry,
                        L2Idx, (unsigned long long)L2Entry,
                        L3Idx, (unsigned long long)L3Entry,
                        Result);
            }

            return Result;
        }
    }
}

BOOLEAN
NTAPI
MmIsDisabledPage(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance: Walk TTBR0 physically via KSEG0.
     *
     * "Disabled page" means: Valid && !Writable && !CopyOnWrite
     * This is a read-only non-COW mapping - typically a private data section
     * that cannot be written.
     */
    {
        ULONG64 PteValue;
        MMPTE TempPte;

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue, NULL))
            return FALSE;

        TempPte.u.Long = PteValue;
        if (!TempPte.u.Hard.Valid)
            return FALSE;

        return (TempPte.u.Hard.Writable == 0) && (TempPte.u.Hard.CopyOnWrite == 0);
    }
}

BOOLEAN
NTAPI
MmIsPageSwapEntry(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);
        return FALSE;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance: Walk TTBR0 physically via KSEG0.
     *
     * Swap entry check: !Valid && bit 0x800 is set (swap entry marker).
     * We need the raw PTE value to check these bits.
     */
    {
        ULONG64 PteValue;
        ULONG Depth;

        /* Get the PTE value by walking physically */
        MiArm64ReadUserPtePhysically(Address, &PteValue, &Depth);

        /* If we didn't reach L3 level (depth < 3), no PTE exists */
        if (Depth < 3)
            return FALSE;

        /* Check for swap entry: !Valid && bit 0x800 set */
        return ((PteValue & 0x3ULL) != 0x3ULL) && ((PteValue & 0x800ULL) != 0);
    }
}

ULONG
NTAPI
MmGetPageProtect(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    if (Address >= MmSystemRangeStart)
    {
        PMMPTE PointerPte;

        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if page tables exist without faulting.
         * We cannot call MiMakeSystemAddressValid here as it could cause
         * infinite recursion during page fault handling.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            return PAGE_NOACCESS;
        }

        PointerPte = MiAddressToPte(Address);
        return PointerPte->u.Hard.Valid ? MiProtectionFromPte(*PointerPte) : PAGE_NOACCESS;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64 TTBR0 Alias Avoidance: Walk TTBR0 physically via KSEG0.
     */
    {
        ULONG64 PteValue;
        MMPTE TempPte;

        if (!MiArm64ReadUserPtePhysically(Address, &PteValue, NULL))
            return PAGE_NOACCESS;

        TempPte.u.Long = PteValue;
        return TempPte.u.Hard.Valid ? MiProtectionFromPte(TempPte) : PAGE_NOACCESS;
    }
}

VOID
NTAPI
MmSetPageProtect(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ ULONG Protection)
{
    ULONG ProtectionMask;
    PMMPTE PointerPte;
    MMPTE TempPte, OldPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(Address < MmSystemRangeStart);

    ProtectionMask = MiMakeProtectionMask(Protection);
    ASSERT(ProtectionMask != MM_INVALID_PROTECTION);

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    MiMakePdeExistAndMakeValid(MiAddressToPdeSafe(Address), Process, MM_NOIRQL);

    PointerPte = MiAddressToPteSafe(Address);

    TempPte.u.Long = MiDetermineUserGlobalPteMask(PointerPte);
    TempPte.u.Long |= MmProtectToPteMask[ProtectionMask];
    TempPte.u.Hard.PageFrameNumber = PointerPte->u.Hard.PageFrameNumber;

    if ((ProtectionMask != MM_NOACCESS) && !FlagOn(ProtectionMask, MM_GUARDPAGE))
        TempPte.u.Hard.Valid = 1;

    if (PointerPte->u.Hard.Accessed)
        TempPte.u.Hard.Accessed = 1;
    if (PointerPte->u.Hard.NotDirty == 0)
        MI_MAKE_DIRTY_PAGE(&TempPte);

    OldPte.u.Long = InterlockedExchangePte(PointerPte, TempPte.u.Long);

    if (!OldPte.u.Hard.Valid && FlagOn(OldPte.u.Long, 0x800))
    {
        DPRINT1("Unexpected non-present PTE during protection change\n");
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    if (OldPte.u.Long != TempPte.u.Long)
        KeInvalidateTlbEntry(Address);

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

VOID
NTAPI
MmSetDirtyBit(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN Dirty)
{
    PMMPTE PointerPte;

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());
    ASSERT(Address < MmSystemRangeStart);

    MiLockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
    MiMakePdeExistAndMakeValid(MiAddressToPdeSafe(Address), Process, MM_NOIRQL);

    PointerPte = MiAddressToPteSafe(Address);

    if (!PointerPte->u.Hard.Valid && FlagOn(PointerPte->u.Long, 0x800))
    {
        DPRINT1("Invalid PTE for MmSetDirtyBit\n");
        KeBugCheck(MEMORY_MANAGEMENT);
    }

    if (Dirty)
        MI_MAKE_DIRTY_PAGE(PointerPte);
    else
        MI_MAKE_CLEAN_PAGE(PointerPte);

    if (!Dirty)
        KeInvalidateTlbEntry(Address);

    MiUnlockProcessWorkingSetUnsafe(Process, PsGetCurrentThread());
}

PFN_NUMBER
NTAPI
MmGetPfnForProcess(
    _Inout_opt_ PEPROCESS Process,
    _In_ PVOID Address)
{
    PMMPTE PointerPte;
    PFN_NUMBER PageFrame = 0;

    if (Address >= MmSystemRangeStart)
    {
        ASSERT(Process == NULL);

        /*
         * For kernel addresses, check if page tables exist without faulting.
         * We cannot call MiMakeSystemAddressValid here as it could cause
         * infinite recursion during page fault handling.
         */
        if (!MiIsPdeForAddressValid(Address))
        {
            return 0;
        }

        PointerPte = MiAddressToPte(Address);
        if (PointerPte->u.Hard.Valid)
            PageFrame = PointerPte->u.Hard.PageFrameNumber;

        return PageFrame;
    }

    ASSERT(Process != NULL);
    ASSERT(Process == PsGetCurrentProcess());

    /*
     * ARM64: Use MiArm64GetUserPfn to walk user page tables directly.
     *
     * On ARM64 with TTBR0/TTBR1 split architecture, the kernel's self-mapping
     * (accessible via MiAddressToPxe/Ppe/Pde/Pte) is in TTBR1 and cannot access
     * user page tables which are in TTBR0. We must walk the user's page table
     * hierarchy using the process's DirectoryTableBase.
     */
    return MiArm64GetUserPfn(Process, Address);
}

static
BOOLEAN
__attribute__((unused))
MiIsPageTablePresent(
    _In_ PVOID Address)
{
#if _MI_PAGING_LEVELS == 2
    BOOLEAN Ret = MmWorkingSetList->UsedPageTableEntries[MiGetPdeOffset(Address)] != 0;
    ASSERT(Ret == (MiAddressToPde(Address)->u.Hard.Valid != 0));
    return Ret;
#else
    PMMPDE PointerPde;
    PMMPPE PointerPpe;
#if _MI_PAGING_LEVELS == 4
    PMMPXE PointerPxe;
#endif

    ASSERT((PsGetCurrentThread()->OwnsProcessWorkingSetExclusive) ||
           (PsGetCurrentThread()->OwnsProcessWorkingSetShared));
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    /*
     * ARM64: For user-space addresses, we cannot safely use the TTBR0 alias
     * while holding the WS lock, as accessing the alias addresses may fault
     * and cause lock re-entry issues.
     *
     * Instead, we rely on the UsedPageTableEntries counters in MmWorkingSetList.
     * These counters track which page directory entries have been allocated.
     *
     * Note: This is a conservative check. If we can't determine page table
     * presence safely, we return FALSE (page not present).
     */
    if (Address < MmSystemRangeStart)
    {
        /*
         * Use UsedPageTableEntries to check if there's a PDE for this address.
         * This doesn't access TTBR0 alias addresses and is safe while holding WS lock.
         *
         * For now, use a simple check: if there are no page table entries used
         * at the PDE offset for this address, the page table doesn't exist.
         */
        ULONG PdeOffset = MiGetPdeOffset(Address);
        if (MmWorkingSetList != NULL)
        {
            if (MmWorkingSetList->UsedPageTableEntries[PdeOffset] == 0)
                return FALSE;
        }
        else
        {
            /* WorkingSetList not available - assume page table doesn't exist */
            return FALSE;
        }

        /*
         * UsedPageTableEntries says there's a page table at this PDE.
         * The full page table hierarchy should exist.
         */
        return TRUE;
    }

    /*
     * ARM64/ReactOS Fix: Check actual page table validity instead of
     * UsedPageTableEntries counters.
     *
     * The ReactOS section view code path (MmCreateVirtualMapping ->
     * MiMakePdeExistAndMakeValid) creates page tables via kernel-mode
     * fault handling, which doesn't properly increment the UsedPageTableEntries
     * counters at PXE/PPE levels. This causes MiIsPageTablePresent to
     * incorrectly return FALSE even when the page tables are valid.
     *
     * The fix is to check the actual validity of each level of the page
     * table hierarchy. If all levels (PXE, PPE, PDE) are valid, then the
     * page table is present. If any level is invalid or in transition,
     * we fault it in and continue.
     */

#if _MI_PAGING_LEVELS == 4
    PointerPxe = MiAddressToPxe(Address);

    /* If PXE is not valid and not in transition, page table cannot be present */
    if (PointerPxe->u.Hard.Valid == 0 && PointerPxe->u.Soft.Transition == 0)
    {
        /* Check if there are any pending entries (non-zero soft PTE) */
        if (PointerPxe->u.Long == 0)
            return FALSE;
        /* There's a soft PTE, page table might exist - continue checking */
    }

    /* Ensure PXE is valid before accessing PPE */
    if (PointerPxe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPxe), PsGetCurrentProcess());
#endif

    PointerPpe = MiAddressToPpe(Address);

    /* If PPE is not valid and not in transition, page table cannot be present */
    if (PointerPpe->u.Hard.Valid == 0 && PointerPpe->u.Soft.Transition == 0)
    {
        /* Check if there are any pending entries (non-zero soft PTE) */
        if (PointerPpe->u.Long == 0)
            return FALSE;
        /* There's a soft PTE, page table might exist - continue checking */
    }

    /* Ensure PPE is valid before accessing PDE */
    if (PointerPpe->u.Hard.Valid == 0)
        MiMakeSystemAddressValid(MiPteToAddress(PointerPpe), PsGetCurrentProcess());

    PointerPde = MiAddressToPde(Address);

    /*
     * The page table for this address exists if the PDE is valid.
     * We don't rely on UsedPageTableEntries counters here because
     * they may not be properly maintained by the ROS section view path.
     */
    if (PointerPde->u.Hard.Valid == 1)
        return TRUE;

    /* PDE is not valid - check if it's in transition or has soft PTE data */
    if (PointerPde->u.Soft.Transition == 1)
        return TRUE;

    /* PDE is completely empty - no page table present */
    return PointerPde->u.Long != 0;
#endif
}

static
ULONG
MiProtectionFromPte(
    _In_ MMPTE Pte)
{
    ULONG Mask = Pte.u.Long & PTE_PROTECT_MASK;

    for (ULONG i = 0; i < ARRAYSIZE(MmProtectToPteMask); ++i)
    {
        if ((MmProtectToPteMask[i] & PTE_PROTECT_MASK) == Mask)
            return MmProtectToValue[i];
    }

    return PAGE_NOACCESS;
}

/*
 * ARM64 Cycle 57: Clear stale user-space PTEs from FreeLoader's identity mapping.
 *
 * FreeLoader creates identity mappings in user space (TTBR0) during boot.
 * These mappings cover physical addresses up to 4GB. When the kernel maps
 * section views at user addresses that overlap with this identity mapping,
 * reads would return identity-mapped data instead of section data.
 *
 * This function walks the TTBR0 page table hierarchy for the specified VA range
 * and invalidates any valid PTEs found. This ensures page faults occur when
 * the section is accessed, allowing proper demand-loading of section data.
 *
 * @param StartVa   Starting virtual address of the range to clear
 * @param Size      Size of the range in bytes
 * @param Process   Process whose user page tables should be cleared
 */
VOID
NTAPI
MiArm64ClearStaleUserPtes(
    _In_ PVOID StartVa,
    _In_ SIZE_T Size,
    _In_ PEPROCESS Process)
{
    ULONG64 Ttbr0;
    PFN_NUMBER L0Pfn, L1Pfn, L2Pfn, L3Pfn;
    PMMPTE L0Table, L1Table, L2Table, L3Table;
    PMMPTE MappingPte;
    ULONG64 Va, EndVa;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG ClearedCount = 0;
    ULONG64 Ctr;
    ULONG DcacheLineSize;

    /* Only for user-space addresses */
    if ((ULONG_PTR)StartVa >= (ULONG_PTR)MmSystemRangeStart)
        return;

    /* Get D-cache line size for invalidation */
    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

    /* Get TTBR0 (user page table root) */
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    L0Pfn = (Ttbr0 >> PAGE_SHIFT) & ((1ULL << 36) - 1);  /* Get PA without ASID bits */

    EndVa = (ULONG64)StartVa + Size;
    Va = (ULONG64)StartVa;

    while (Va < EndVa)
    {
        L0Idx = (Va >> 39) & 0x1FF;
        L1Idx = (Va >> 30) & 0x1FF;
        L2Idx = (Va >> 21) & 0x1FF;
        L3Idx = (Va >> 12) & 0x1FF;

        /* Map L0 table */
        MappingPte = MiReserveSystemPtes(1, SystemPteSpace);
        if (!MappingPte)
        {
            DPRINT1("[arm64] MiArm64ClearStaleUserPtes: Failed to get system PTE\n");
            return;
        }

        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L0Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L0Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L0 entry is valid */
        if (!(L0Table[L0Idx].u.Long & 1))
        {
            /* No L0 entry, skip this 512GB range */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(L0Table);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
            Va = (Va + (1ULL << 39)) & ~((1ULL << 39) - 1);
            continue;
        }

        L1Pfn = L0Table[L0Idx].u.Hard.PageFrameNumber;
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L0Table);

        /* Map L1 table */
        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L1Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L1Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L1 entry is valid */
        if (!(L1Table[L1Idx].u.Long & 1))
        {
            /* No L1 entry, skip this 1GB range */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(L1Table);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
            Va = (Va + (1ULL << 30)) & ~((1ULL << 30) - 1);
            continue;
        }

        L2Pfn = L1Table[L1Idx].u.Hard.PageFrameNumber;
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L1Table);

        /* Map L2 table */
        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L2Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L2Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L2 entry is valid */
        if (!(L2Table[L2Idx].u.Long & 1))
        {
            /* No L2 entry, skip this 2MB range */
            MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
            KeInvalidateTlbEntry(L2Table);
            MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);
            Va = (Va + (1ULL << 21)) & ~((1ULL << 21) - 1);
            continue;
        }

        /* L2 entry is valid - but is it a block descriptor or table descriptor? */
        /* bits [1:0] = 0b01 for block, 0b11 for table */
        {
            ULONG64 L2Entry = L2Table[L2Idx].u.Long;
            if ((L2Entry & 0x3) == 0x1)
            {
                /*
                 * Block descriptor (2MB page) - this is a FreeLoader identity mapping!
                 * We need to CLEAR this block descriptor to ensure page faults occur.
                 */
                DPRINT1("[arm64] MiArm64ClearStaleUserPtes: L2 entry at idx=%lu is BLOCK (2MB) entry=0x%llx - CLEARING IT\n",
                        (ULONG)L2Idx, L2Entry);

                /*
                 * Invalidate TLB entries for the 2MB block.
                 * We use TLBI ASIDE1 to invalidate by ASID, which is more efficient
                 * than invalidating each page individually.
                 * But for simplicity, just invalidate everything with TLBI VMALLE1.
                 */
                __asm__ __volatile__("tlbi vmalle1" ::: "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
                __asm__ __volatile__("isb" ::: "memory");

                /*
                 * NOTE: We intentionally do NOT invalidate D-cache for the entire 2MB block here.
                 * D-cache invalidation for user VAs at this point can cause issues:
                 * 1. The VAs are no longer mapped (we just cleared the L2 block)
                 * 2. DC IVAC on unmapped VAs may cause issues on some implementations
                 * 3. The next access will fault anyway and bring in fresh data
                 *
                 * The TLB invalidation above is sufficient - cache lines will be naturally
                 * evicted or will return stale data on next access, which will fault.
                 */

                /* Clear the L2 block descriptor */
                {
                    ULONG_PTR PteAddr = (ULONG_PTR)&L2Table[L2Idx];
                    L2Table[L2Idx].u.Long = 0;
                    __asm__ __volatile__("dmb ish" ::: "memory");
                    __asm__ __volatile__("dc civac, %0" :: "r"(PteAddr) : "memory");
                    __asm__ __volatile__("dsb ish" ::: "memory");
                }

                /* Verify the L2 entry was cleared */
                if (L2Table[L2Idx].u.Long != 0)
                {
                    DPRINT1("[arm64] MiArm64ClearStaleUserPtes: WARNING! L2 BLOCK entry NOT cleared! Still=0x%llx\n",
                            L2Table[L2Idx].u.Long);
                }
                else
                {
                    DPRINT1("[arm64] MiArm64ClearStaleUserPtes: Successfully cleared L2 BLOCK entry for 2MB at VA=%p\n",
                            (PVOID)(Va & ~((1ULL << 21) - 1)));
                }

                MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
                KeInvalidateTlbEntry(L2Table);
                MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

                /* Skip the entire 2MB block */
                ClearedCount += (1ULL << 21) >> PAGE_SHIFT;  /* Count as 512 pages */
                Va = (Va + (1ULL << 21)) & ~((1ULL << 21) - 1);
                continue;
            }
            /* Otherwise it's a table descriptor pointing to L3 table */
            DPRINT1("[arm64] MiArm64ClearStaleUserPtes: L2 entry at idx=%lu = 0x%llx (table descriptor)\n",
                    (ULONG)L2Idx, L2Entry);
        }

        L3Pfn = L2Table[L2Idx].u.Hard.PageFrameNumber;
        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L2Table);

        /* Map L3 table */
        MI_WRITE_VALID_PTE(MappingPte, ValidKernelPte);
        MappingPte->u.Hard.PageFrameNumber = L3Pfn;
        KeInvalidateTlbEntry(MiPteToAddress(MappingPte));
        __asm__ __volatile__("dsb ish" ::: "memory");

        L3Table = (PMMPTE)MiPteToAddress(MappingPte);

        /* Check if L3 entry (PTE) has bit 0 set (potentially valid) */
        if (L3Table[L3Idx].u.Long & 1)
        {
            ULONG64 OldPte = L3Table[L3Idx].u.Long;

            /* Found a PTE with Valid bit set - clear it. */
            DPRINT1("[arm64] MiArm64ClearStaleUserPtes: Clearing stale PTE at VA=%p (PTE=0x%llx) L3TableVA=%p L3Idx=%lu L3Pfn=0x%llx\n",
                    (PVOID)Va, OldPte, L3Table, (ULONG)L3Idx, (ULONG64)L3Pfn);
            DPRINT1("[arm64]   Page table walk: L0Pfn=0x%llx L1Pfn=0x%llx L2Pfn=0x%llx L3Pfn=0x%llx\n",
                    (ULONG64)L0Pfn, (ULONG64)L1Pfn, (ULONG64)L2Pfn, (ULONG64)L3Pfn);
            DPRINT1("[arm64]   Indices: L0=%lu L1=%lu L2=%lu L3=%lu\n",
                    (ULONG)L0Idx, (ULONG)L1Idx, (ULONG)L2Idx, (ULONG)L3Idx);

            /* Invalidate TLB for this VA - use VAALE1IS to flush ALL ASIDs including Global entries.
             * FreeLoader may have created Global mappings (nG bit not set), so VAE1 (current ASID only)
             * would NOT flush them. VAALE1IS = VA, All ASIDs, Last-level, EL1, Inner Shareable */
            __asm__ __volatile__("tlbi vaale1is, %0" :: "r"(Va >> PAGE_SHIFT) : "memory");
            __asm__ __volatile__("dsb ish" ::: "memory");

            /* Invalidate D-cache for this page */
            {
                ULONG_PTR CacheVa;
                for (CacheVa = Va; CacheVa < Va + PAGE_SIZE; CacheVa += DcacheLineSize)
                {
                    __asm__ __volatile__("dc ivac, %0" :: "r"(CacheVa) : "memory");
                }
            }
            __asm__ __volatile__("dsb ish" ::: "memory");

            /* Clear the PTE with proper cache maintenance */
            {
                ULONG_PTR PteAddr = (ULONG_PTR)&L3Table[L3Idx];

                /* Write the zero value */
                L3Table[L3Idx].u.Long = 0;

                /* Data memory barrier before cache op */
                __asm__ __volatile__("dmb ish" ::: "memory");

                /* Clean and invalidate this cache line to PoC - ensure write reaches memory */
                __asm__ __volatile__("dc civac, %0" :: "r"(PteAddr) : "memory");

                /* DSB to ensure cache op completes */
                __asm__ __volatile__("dsb ish" ::: "memory");

                /* Re-read to verify (need to invalidate cache first to get fresh read) */
                __asm__ __volatile__("dc ivac, %0" :: "r"(PteAddr) : "memory");
                __asm__ __volatile__("dsb ish" ::: "memory");
            }

            /* Verify the PTE was actually cleared */
            if (L3Table[L3Idx].u.Long != 0)
            {
                DPRINT1("[arm64] MiArm64ClearStaleUserPtes: WARNING! PTE at VA=%p was NOT cleared! Still=0x%llx\n",
                        (PVOID)Va, L3Table[L3Idx].u.Long);
            }

            ClearedCount++;
        }

        MI_WRITE_INVALID_PTE(MappingPte, MmDecommittedPte);
        KeInvalidateTlbEntry(L3Table);
        MiReleaseSystemPtes(MappingPte, 1, SystemPteSpace);

        Va += PAGE_SIZE;
    }

    if (ClearedCount > 0)
    {
        DPRINT1("[arm64] MiArm64ClearStaleUserPtes: Cleared %lu stale PTEs in range %p-%p\n",
                ClearedCount, StartVa, (PVOID)EndVa);
    }
}
