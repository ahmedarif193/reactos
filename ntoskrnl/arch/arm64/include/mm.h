/*
 * Kernel internal memory management definitions for arm64
 *
 * The layout follows the Windows 11 ARM64 split where user space occupies
 * the lower 48 bits and kernel space begins at 0xFFFF0000`00000000.
 */
#pragma once

#define PTI_SHIFT  12L
#define PDI_SHIFT  21L
#define PPI_SHIFT  30L
#define PXI_SHIFT  39L
#define PTE_PER_PAGE 512
#define PDE_PER_PAGE 512
#define PPE_PER_PAGE 512
#define PXE_PER_PAGE 512
#define PTI_MASK_ARM64 (PTE_PER_PAGE - 1)
#define PDI_MASK_ARM64 (PDE_PER_PAGE - 1)
#define PPI_MASK       (PPE_PER_PAGE - 1)
#define PXI_MASK       (PXE_PER_PAGE - 1)

/*
 * ARM64 Self-Map Architecture for TTBR0/TTBR1 Split
 *
 * On ARM64, user addresses (0x0000...) are translated via TTBR0_EL1 and
 * kernel addresses (0xFFFF...) are translated via TTBR1_EL1. The NT Memory
 * Manager's self-map is built into TTBR1's hierarchy at L0 index 493.
 *
 * PROBLEM: MiAddressToPte(UserAddress) returns a VA in TTBR1's self-map space,
 * but that doesn't map TTBR0's page tables. The self-map only covers TTBR1.
 *
 * SOLUTION: Reserve L0 index 494 in TTBR1's hierarchy as a "TTBR0 Alias" slot.
 * This slot's L0 entry points to the current process's TTBR0 L0 page table.
 * On context switch (KiSwapProcess), we update this alias entry.
 *
 * Layout:
 *   L0[493] = Self-map recursive entry (points to TTBR1's own L0)
 *   L0[494] = TTBR0 alias entry (points to current TTBR0's L0)
 *
 * When accessing PTEs for user addresses:
 *   - Original self-map formula returns addresses in 0xFFFFF68000000000 range
 *   - We detect user-range PTEs and redirect to 0xFFFFF70000000000 range
 *   - The L0[494] alias ensures we actually walk TTBR0's page tables
 *
 * Self-map L0 index: 493 (0x1ED)
 * TTBR0 alias L0 index: 494 (0x1EE)
 */
#define PXE_SELFMAP_INDEX   493
#define PXE_TTBR0_ALIAS_INDEX 494

#define PXE_BASE    0xFFFFF6FB7DBED000ULL
#define PXE_SELFMAP 0xFFFFF6FB7DBEDF68ULL
#define PPE_BASE    0xFFFFF6FB7DA00000ULL
#define PDE_BASE    0xFFFFF6FB40000000ULL
#define PTE_BASE    0xFFFFF68000000000ULL
#define PXE_TOP     0xFFFFF6FB7DBEDFFFULL
#define PPE_TOP     0xFFFFF6FB7DBFFFFFULL
#define PDE_TOP     0xFFFFF6FB7FFFFFFFULL
#define PTE_TOP     0xFFFFF6FFFFFFFFFFULL

/*
 * TTBR0 Alias Self-Map Bases
 *
 * These addresses correspond to viewing TTBR0's page tables through the
 * TTBR0 alias slot at L0[494]. The derivation uses the L0-index delta from
 * the self-map base addresses.
 *
 * Derivation:
 *   delta = (PXE_TTBR0_ALIAS_INDEX - PXE_SELFMAP_INDEX) << 39
 *         = (494 - 493) << 39
 *         = 1 << 39
 *         = 0x8000000000 (512 GB)
 *
 *   PTE_BASE_TTBR0 = PTE_BASE + delta
 *                  = 0xFFFFF68000000000 + 0x8000000000
 *                  = 0xFFFFF70000000000
 *
 *   PDE_BASE_TTBR0 = PDE_BASE + delta
 *                  = 0xFFFFF6FB40000000 + 0x8000000000
 *                  = 0xFFFFF6FBC0000000
 *
 *   PPE_BASE_TTBR0 = PPE_BASE + delta
 *                  = 0xFFFFF6FB7DA00000 + 0x8000000000
 *                  = 0xFFFFF6FBFDA00000
 *
 *   PXE_BASE_TTBR0 = PXE_BASE + delta
 *                  = 0xFFFFF6FB7DBED000 + 0x8000000000
 *                  = 0xFFFFF6FBFDBED000
 *
 * For user addresses 0x0000... through 0x0000FFFFFFFFFFFF:
 *   PTE for user VA = PTE_BASE_TTBR0 + (VA >> 9)
 *
 * Note: L0[494] must be updated in KiSwapProcess to point to the new
 * process's TTBR0 L0 page table.
 */
#define TTBR0_ALIAS_DELTA       (1ULL << 39)  /* 512 GB = (494 - 493) << 39 */
#define PTE_BASE_TTBR0  (PTE_BASE + TTBR0_ALIAS_DELTA)  /* 0xFFFFF70000000000 */
#define PDE_BASE_TTBR0  (PDE_BASE + TTBR0_ALIAS_DELTA)  /* 0xFFFFF6FBC0000000 */
#define PPE_BASE_TTBR0  (PPE_BASE + TTBR0_ALIAS_DELTA)  /* 0xFFFFF6FBFDA00000 */
#define PXE_BASE_TTBR0  (PXE_BASE + TTBR0_ALIAS_DELTA)  /* 0xFFFFF6FBFDBED000 */

#define KSEG0_BASE  0xFFFF800000000000ULL

#define _MI_PAGING_LEVELS 4
#define _MI_HAS_NO_EXECUTE 1

/* Virtual address layout for ARM64.
 * The kernel is mapped at KSEG0_BASE (0xFFFF800000000000), so system range
 * must start at or below that address and have bit 47 set for valid canonical
 * upper-half addresses on ARM64 with 48-bit VAs.
 */
#define MI_USER_PROBE_ADDRESS           (PVOID)0x000007FFFFFE0000ULL
#define MI_DEFAULT_SYSTEM_RANGE_START   (PVOID)0xFFFF800000000000ULL
#define MI_REAL_SYSTEM_RANGE_START             0xFFFF800000000000ULL
/*
 * ARM64 HYPER_SPACE Location Fix:
 *
 * PROBLEM: The original HYPER_SPACE at 0xFFFFF70000000000 overlapped with
 * PTE_BASE_TTBR0 (the TTBR0 alias region at L0[494]). This caused hyperspace
 * PTEs to be inaccessible because the self-map only properly covers L0[493].
 *
 * When MiMapPageInHyperSpace accessed MiAddressToPte(HYPER_SPACE), the
 * resulting PTE address was in a region without valid intermediate page tables,
 * causing Data Abort crashes.
 *
 * SOLUTION: Relocate HYPER_SPACE to 0xFFFFF60000000000, which is at the start
 * of L0[493] (the normal kernel self-map region). This ensures hyperspace PTEs
 * are accessible through the standard self-map mechanism.
 *
 * Memory layout change:
 *   Old: HYPER_SPACE = 0xFFFFF70000000000 (L0[494], TTBR0 alias - BROKEN)
 *   New: HYPER_SPACE = 0xFFFFF60000000000 (L0[493], normal self-map - WORKING)
 */
#define HYPER_SPACE                            0xFFFFF60000000000ULL
#define HYPER_SPACE_END                        0xFFFFF67FFFFFFFFFULL
#define MI_SYSTEM_CACHE_WS_START               0xFFFFF78000001000ULL
#define MI_SYSTEM_SPACE_START                  0xFFFFF88000000000ULL
#define MI_DEBUG_MAPPING                (PVOID)0xFFFFF89FFFFFF000ULL
#define MI_PAGED_POOL_START             (PVOID)0xFFFFF8A000000000ULL
#define MI_SESSION_SPACE_END                   0xFFFFF98000000000ULL
#define MI_SYSTEM_CACHE_START                  0xFFFFF98000000000ULL
#define MI_SYSTEM_CACHE_END                    0xFFFFFA7FFFFFFFFFULL
#define MI_PFN_DATABASE                        0xFFFFFA8000000000ULL
#define MI_NONPAGED_POOL_END            (PVOID)0xFFFFFFFFFFBFFFFFULL
#define MI_HIGHEST_SYSTEM_ADDRESS       (PVOID)0xFFFFFFFFFFFFFFFFULL

#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS         ((PVOID)0x0000000000010000ULL)
#endif

/* WOW64 compatibility. */
#define MM_HIGHEST_USER_ADDRESS_WOW64   0x7FFEFFFF
#define MM_SYSTEM_RANGE_START_WOW64     0x80000000

/* The size of the virtual memory area mapped by a single PDE. */
#define PDE_MAPPED_VA (PTE_PER_PAGE * PAGE_SIZE)

#define MI_SYSTEM_PTE_BASE              (PVOID)MiAddressToPte(KSEG0_BASE)
#define MM_HIGHEST_VAD_ADDRESS          (PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE))
#define MI_MAPPING_RANGE_START          HYPER_SPACE
#define MI_MAPPING_RANGE_END            (MI_MAPPING_RANGE_START + MI_HYPERSPACE_PTES * PAGE_SIZE)
#define MI_DUMMY_PTE                    (MI_MAPPING_RANGE_END + PAGE_SIZE)
#define MI_VAD_BITMAP                   (MI_DUMMY_PTE + PAGE_SIZE)
#define MI_WORKING_SET_LIST             (MI_VAD_BITMAP + PAGE_SIZE)

#define MI_MIN_PAGES_FOR_NONPAGED_POOL_TUNING   ((255 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_TUNING          ((19 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST           ((32 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST_BOOST     ((256 * _1MB) >> PAGE_SHIFT)
#define MI_MIN_INIT_PAGED_POOLSIZE              (32ULL * _1MB)
#define MI_MAX_INIT_NONPAGED_POOL_SIZE          (128ULL * 1024 * 1024 * 1024)
#define MI_MAX_NONPAGED_POOL_SIZE               (128ULL * 1024 * 1024 * 1024)
#define MI_SYSTEM_VIEW_SIZE                     (512 * _1MB)
#define MI_SESSION_VIEW_SIZE                    (512 * _1MB)
#define MI_SESSION_POOL_SIZE                    (64 * _1MB)
#define MI_SESSION_IMAGE_SIZE                   (16 * _1MB)
#define MI_SESSION_WORKING_SET_SIZE             (16 * _1MB)
#define MI_SESSION_SIZE                         (MI_SESSION_VIEW_SIZE + \
                                                 MI_SESSION_POOL_SIZE + \
                                                 MI_SESSION_IMAGE_SIZE + \
                                                 MI_SESSION_WORKING_SET_SIZE)
#define MI_MIN_ALLOCATION_FRAGMENT              (4 * _1KB)
#define MI_ALLOCATION_FRAGMENT                  (64 * _1KB)
#define MI_MAX_ALLOCATION_FRAGMENT              (2  * _1MB)

#define MM_PTE_SOFTWARE_PROTECTION_BITS         1
#define MI_MIN_SECONDARY_COLORS                 8
#define MI_SECONDARY_COLORS                     64
#define MI_MAX_SECONDARY_COLORS                 1024
#define MI_NUMBER_SYSTEM_PTES                   22000
#define MI_MAX_FREE_PAGE_LISTS                  4
#define MI_HYPERSPACE_PTES                     256
#define MI_ZERO_PTES                           (32)
#define MI_MAX_ZERO_BITS                        53
#define SESSION_POOL_LOOKASIDES                 21

#ifndef MM_HIGHEST_USER_ADDRESS
#define MM_HIGHEST_USER_ADDRESS        MI_HIGHEST_USER_ADDRESS
#endif
#ifndef MM_SYSTEM_RANGE_START
#define MM_SYSTEM_RANGE_START          MI_DEFAULT_SYSTEM_RANGE_START
#endif

#define MM_EMPTY_PTE_LIST  ((ULONG64)0xFFFFFFFF)
#define MM_EMPTY_LIST      ((ULONG_PTR)-1)

#define PFN_FROM_PTE(v) ((v)->u.Hard.PageFrameNumber)
#define PFN_FROM_PDE(v) ((v)->u.Hard.PageFrameNumber)
#define PFN_FROM_PPE(v) ((v)->u.Hard.PageFrameNumber)
#define PFN_FROM_PXE(v) ((v)->u.Hard.PageFrameNumber)

#define MI_MAKE_DIRTY_PAGE(x)      ((x)->u.Hard.NotDirty = 0)
#define MI_MAKE_CLEAN_PAGE(x)      ((x)->u.Hard.NotDirty = 1)
#define MI_MAKE_ACCESSED_PAGE(x)   ((x)->u.Hard.Accessed = 1)
#define MI_SET_PTE_ATTR_INDEX(x, idx)            \
    do                                           \
    {                                            \
        (x)->u.Hard.CacheType = (ULONG)((idx) & 0x3); \
    } while (0)
#define MI_PAGE_DISABLE_CACHE(x)   MI_SET_PTE_ATTR_INDEX((x), 1)
#define MI_PAGE_WRITE_THROUGH(x)   MI_SET_PTE_ATTR_INDEX((x), 1)
#define MI_PAGE_WRITE_COMBINED(x)  MI_SET_PTE_ATTR_INDEX((x), 2)
#define MI_IS_PAGE_LARGE(x)        ((x)->u.Hard.NotLargePage == 0)
#define MI_IS_PAGE_WRITEABLE(x)    ((x)->u.Hard.Writable == 1)
#define MI_IS_PAGE_COPY_ON_WRITE(x)((x)->u.Hard.CopyOnWrite == 1)
#define MI_IS_PAGE_EXECUTABLE(x)   ((x)->u.Hard.UserNoExecute == 0)
#define MI_IS_PAGE_DIRTY(x)        ((x)->u.Hard.NotDirty == 0)
#define MI_MAKE_OWNER_PAGE(x)      ((x)->u.Hard.Owner = 1)
#define MI_MAKE_WRITE_PAGE(x)      ((x)->u.Hard.Writable = 1)

#define MI_IS_NOT_PRESENT_FAULT(FaultCode)  !BooleanFlagOn(FaultCode, 0x00000001)
#define MI_IS_WRITE_ACCESS(FaultCode)        BooleanFlagOn(FaultCode, 0x00000002)
#define MI_IS_INSTRUCTION_FETCH(FaultCode)   BooleanFlagOn(FaultCode, 0x00000020)

#define MI_WRITE_VALID_PPE MI_WRITE_VALID_PTE
#define ValidKernelPpe ValidKernelPde

FORCEINLINE
PMMPTE
_MiAddressToPte(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PTI_SHIFT - 3);
    Offset &= 0xFFFFFFFFFULL << 3;
    return (PMMPTE)(PTE_BASE + Offset);
}
#define MiAddressToPte(x) _MiAddressToPte((PVOID)(x))

FORCEINLINE
PMMPTE
_MiAddressToPde(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PDI_SHIFT - 3);
    Offset &= 0x7FFFFFFULL << 3;
    return (PMMPTE)(PDE_BASE + Offset);
}
#define MiAddressToPde(x) _MiAddressToPde((PVOID)(x))

FORCEINLINE
PMMPTE
MiAddressToPpe(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PPI_SHIFT - 3);
    Offset &= 0x3FFFFULL << 3;
    return (PMMPTE)(PPE_BASE + Offset);
}

FORCEINLINE
PMMPTE
MiAddressToPxe(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PXI_SHIFT - 3);
    Offset &= PXI_MASK << 3;
    return (PMMPTE)(PXE_BASE + Offset);
}

FORCEINLINE
ULONG
MiAddressToPti(PVOID Address)
{
    return ((((ULONG64)Address) >> PTI_SHIFT) & 0x1FF);
}
#define MiAddressToPteOffset(x) MiAddressToPti(x)

FORCEINLINE
ULONG
MiAddressToPdi(PVOID Address)
{
    return ((((ULONG64)Address) >> PDI_SHIFT) & 0x1FF);
}
#define MiAddressToPdeOffset(x) MiAddressToPdi(x)
#define MiGetPdeOffset(x) MiAddressToPdi(x)

FORCEINLINE
ULONG
MiAddressToPxi(PVOID Address)
{
    return ((((ULONG64)Address) >> PXI_SHIFT) & 0x1FF);
}

/*
 * MiIsUserAddress - Check if address is in user space (TTBR0 range)
 *
 * On ARM64 with 48-bit VAs:
 *   User space:   0x0000000000000000 - 0x0000FFFFFFFFFFFF (below 2^48)
 *   Kernel space: 0xFFFF000000000000 - 0xFFFFFFFFFFFFFFFF
 *
 * The check uses 0x0001000000000000 (2^48) as the boundary:
 *   - Addresses below 2^48 are user addresses (TTBR0)
 *   - Addresses at or above 0xFFFF000000000000 are kernel addresses (TTBR1)
 *
 * Note: The actual TTBR selection on ARM64 hardware is based on bit 55
 * (the highest implemented VA bit for 48-bit VA configurations), but
 * this simpler comparison works because:
 *   - Valid user addresses are always < 2^48
 *   - Valid kernel addresses are always >= 0xFFFF000000000000 (canonical form)
 *   - The gap between 0x0001000000000000 and 0xFFFF000000000000 contains
 *     no valid addresses (non-canonical)
 */
FORCEINLINE
BOOLEAN
MiIsUserAddress(PVOID Address)
{
    return ((ULONG64)Address < 0x0001000000000000ULL);
}

/*
 * MiAddressToPteTtbr0 - Get PTE address for a user-space address via TTBR0 alias
 *
 * This function returns the PTE address for a user-space virtual address,
 * routed through the TTBR0 alias slot at L0[494]. This ensures we're actually
 * accessing the page tables in TTBR0's hierarchy, not TTBR1's self-map.
 *
 * IMPORTANT: L0[494] must be initialized to point to the current process's
 * TTBR0 L0 page before calling this function.
 */
FORCEINLINE
PMMPTE
MiAddressToPteTtbr0(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PTI_SHIFT - 3);
    Offset &= 0xFFFFFFFFFULL << 3;  /* Mask to 36-bit entry offset (512GB space) */
    return (PMMPTE)(PTE_BASE_TTBR0 + Offset);
}

/*
 * MiAddressToPdeTtbr0 - Get PDE address for a user-space address via TTBR0 alias
 */
FORCEINLINE
PMMPTE
MiAddressToPdeTtbr0(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PDI_SHIFT - 3);
    Offset &= 0x7FFFFFFULL << 3;  /* Mask to 27-bit entry offset */
    return (PMMPTE)(PDE_BASE_TTBR0 + Offset);
}

/*
 * MiAddressToPpeTtbr0 - Get PPE address for a user-space address via TTBR0 alias
 */
FORCEINLINE
PMMPTE
MiAddressToPpeTtbr0(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PPI_SHIFT - 3);
    Offset &= 0x3FFFFULL << 3;  /* Mask to 18-bit entry offset */
    return (PMMPTE)(PPE_BASE_TTBR0 + Offset);
}

/*
 * MiAddressToPxeTtbr0 - Get PXE address for a user-space address via TTBR0 alias
 */
FORCEINLINE
PMMPTE
MiAddressToPxeTtbr0(PVOID Address)
{
    ULONG64 Offset = (ULONG64)Address >> (PXI_SHIFT - 3);
    Offset &= PXI_MASK << 3;  /* Mask to 9-bit entry offset */
    return (PMMPTE)(PXE_BASE_TTBR0 + Offset);
}

/*
 * MiAddressToPteSafe / MiAddressToPdeSafe / etc.
 *
 * ARM64 split page table aware macros that automatically select the correct
 * self-map alias based on whether the address is in user or kernel space.
 *
 * For user addresses (< MmSystemRangeStart): Use TTBR0 alias (L0[494])
 * For kernel addresses (>= MmSystemRangeStart): Use TTBR1 self-map
 *
 * These macros MUST be used in any code that accesses page tables for addresses
 * that could be either user or kernel space (e.g., generic MM code that handles
 * both user process mappings and kernel mappings).
 */
FORCEINLINE
PMMPTE
MiAddressToPteSafe(PVOID Address)
{
    if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
        return MiAddressToPteTtbr0(Address);
    else
        return MiAddressToPte(Address);
}

FORCEINLINE
PMMPTE
MiAddressToPdeSafe(PVOID Address)
{
    if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
        return MiAddressToPdeTtbr0(Address);
    else
        return MiAddressToPde(Address);
}

FORCEINLINE
PMMPTE
MiAddressToPpeSafe(PVOID Address)
{
    if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
        return MiAddressToPpeTtbr0(Address);
    else
        return MiAddressToPpe(Address);
}

FORCEINLINE
PMMPTE
MiAddressToPxeSafe(PVOID Address)
{
    if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
        return MiAddressToPxeTtbr0(Address);
    else
        return MiAddressToPxe(Address);
}

FORCEINLINE
PVOID
MiPteToAddress(PMMPTE PointerPte)
{
    return (PVOID)(((LONG64)PointerPte << 25) >> 16);
}

FORCEINLINE
PVOID
MiPdeToAddress(PMMPTE PointerPde)
{
    return (PVOID)(((LONG64)PointerPde << 34) >> 16);
}

FORCEINLINE
PVOID
MiPpeToAddress(PMMPTE PointerPpe)
{
    return (PVOID)(((LONG64)PointerPpe << 43) >> 16);
}

FORCEINLINE
PVOID
MiPxeToAddress(PMMPTE PointerPxe)
{
    return (PVOID)(((LONG64)PointerPxe << 52) >> 16);
}

FORCEINLINE
PMMPTE
MiPdeToPte(PMMPDE PointerPde)
{
    return (PMMPTE)MiPteToAddress(PointerPde);
}

FORCEINLINE
PMMPTE
MiPpeToPte(PMMPPE PointerPpe)
{
    return (PMMPTE)MiPdeToAddress(PointerPpe);
}

FORCEINLINE
PMMPTE
MiPxeToPte(PMMPXE PointerPxe)
{
    return (PMMPTE)MiPpeToAddress(PointerPxe);
}

FORCEINLINE
PMMPDE
MiPteToPde(PMMPTE PointerPte)
{
    return (PMMPDE)MiAddressToPte(PointerPte);
}

FORCEINLINE
PMMPPE
MiPteToPpe(PMMPTE PointerPte)
{
    return (PMMPPE)MiAddressToPde(PointerPte);
}

FORCEINLINE
PMMPXE
MiPteToPxe(PMMPTE PointerPte)
{
    return (PMMPXE)MiAddressToPpe(PointerPte);
}

FORCEINLINE
PMMPPE
MiPdeToPpe(PMMPDE PointerPde)
{
    return (PMMPPE)MiAddressToPte(PointerPde);
}

FORCEINLINE
PMMPXE
MiPdeToPxe(PMMPDE PointerPde)
{
    return (PMMPXE)MiAddressToPde(PointerPde);
}

#define MiIsPteOnPdeBoundary(PointerPte) \
    ((((ULONG_PTR)PointerPte) & (PAGE_SIZE - 1)) == 0)
#define MiIsPteOnPpeBoundary(PointerPte) \
    ((((ULONG_PTR)PointerPte) & (PDE_PER_PAGE * PAGE_SIZE - 1)) == 0)
#define MiIsPteOnPxeBoundary(PointerPte) \
    ((((ULONG_PTR)PointerPte) & (PPE_PER_PAGE * PDE_PER_PAGE * PAGE_SIZE - 1)) == 0)

/*
 * MiIsPdeForAddressValid - Check if all page table levels are valid for an address.
 *
 * This function checks if the PXE, PPE, and PDE for a given address are all
 * valid (present) in the page table hierarchy. It does NOT cause page faults
 * and is safe to call during page fault handling.
 *
 * ARM64 uses 4 levels of page tables (PXE -> PPE -> PDE -> PTE), so we must
 * check all three upper levels before accessing the PTE.
 *
 * CRITICAL FIX: On ARM64, we CANNOT use the self-map addresses (MiAddressToPxe,
 * MiAddressToPpe, MiAddressToPde) directly because the self-map page table entries
 * themselves may not be initialized. Accessing an uninitialized self-map address
 * during page fault handling causes a double fault (recursive page fault).
 *
 * For example, when checking if address 0xFFFFF98000004008 is valid:
 * - MiAddressToPde(0xFFFFF98000004008) returns 0xFFFFF6FB7DBF3000
 * - But the self-map entry for 0xFFFFF6FB7DBF3000 may not be present
 * - Accessing it causes another page fault, leading to infinite recursion
 *
 * Solution: Walk the page table hierarchy via physical addresses using KSEG0
 * (the identity-mapped physical memory region at 0xFFFF800000000000). This
 * approach reads the page tables directly from physical memory without relying
 * on the self-map being initialized.
 */
FORCEINLINE
BOOLEAN
MiIsPdeForAddressValid(PVOID Address)
{
    UINT64 Ttbr1;
    UINT64 RootPa;
    volatile UINT64 *L0, *L1, *L2;
    UINT64 E0, E1, E2;
    ULONG L0Index, L1Index, L2Index;

    /* Mask for extracting physical address from page table entry (bits 47:12) */
    #define ARM64_PTE_ADDR_MASK_LOCAL 0x0000FFFFFFFFF000ULL

    /* Read TTBR1_EL1 to get the root page table physical address */
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));

    /* Extract physical address from TTBR1 (mask out ASID bits) */
    RootPa = Ttbr1 & ARM64_PTE_ADDR_MASK_LOCAL;

    /* Map root table via KSEG0 (identity-mapped physical memory) */
    L0 = (volatile UINT64 *)(KSEG0_BASE | RootPa);

    /* Calculate indices for each level */
    L0Index = MiAddressToPxi(Address);
    L1Index = (((ULONG64)Address >> PPI_SHIFT) & 0x1FF);
    L2Index = (((ULONG64)Address >> PDI_SHIFT) & 0x1FF);

    /* Check L0 (PXE) - read via KSEG0, no self-map dependency */
    E0 = L0[L0Index];
    if ((E0 & 1ULL) == 0)
        return FALSE;

    /* Map L1 table via KSEG0 */
    L1 = (volatile UINT64 *)(KSEG0_BASE | (E0 & ARM64_PTE_ADDR_MASK_LOCAL));

    /* Check L1 (PPE) */
    E1 = L1[L1Index];
    if ((E1 & 1ULL) == 0)
        return FALSE;

    /* Map L2 table via KSEG0 */
    L2 = (volatile UINT64 *)(KSEG0_BASE | (E1 & ARM64_PTE_ADDR_MASK_LOCAL));

    /* Check L2 (PDE) */
    E2 = L2[L2Index];
    if ((E2 & 1ULL) == 0)
        return FALSE;

    #undef ARM64_PTE_ADDR_MASK_LOCAL

    return TRUE;
}

//
// Decodes a Prototype PTE into the underlying PTE
// Must shift the entire 64-bit value to ensure sign extension from bit 47
//
#define MiProtoPteToPte(x)                  \
    (PMMPTE)(((LONG64)(x)->u.Long) >> 16) /* Sign extend 48 bits */

//
// Builds a Prototype PTE for the address of the PTE
//
// CRITICAL: This must use the shift-based approach, NOT bitfield assignment.
// ARM64 canonical kernel addresses have bits 48-63 all set to 1 (sign extension
// from bit 47). If we use bitfield assignment (ProtoAddress:48), the upper 16
// bits get truncated and cannot be recovered by sign extension.
//
// The shift-based approach:
// 1. Shifts the address left by 16 bits: 0xFFFFF8A0...BA0 << 16 = 0xF8A0...BA00000
// 2. Stores in u.Long (bits 16-63 contain the shifted address, bit 63=1)
// 3. Sets Prototype bit
// 4. On decode: u.Long >> 16 (arithmetic shift) sign-extends from bit 63,
//    restoring: 0xFFFFF8A0...BA0
//
// MinGW fix: MinGW's bitfield handling causes incorrect sign extension,
// resulting in 0xFFFFFFF8A0...BA0 instead of 0xFFFFF8A0...BA0.
//
FORCEINLINE
VOID
MI_MAKE_PROTOTYPE_PTE(
    _Out_ PMMPTE NewPte,
    _In_ PMMPTE PointerPte)
{
    /* Store the address by shifting it into position */
    NewPte->u.Long = (ULONG64)PointerPte << 16;

    /* Mark it as a prototype PTE */
    NewPte->u.Proto.Prototype = 1;

#if DBG
    /* Verify encoding/decoding works */
    PMMPTE Decoded = MiProtoPteToPte(NewPte);
    if (Decoded != PointerPte)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[arm64] MI_MAKE_PROTOTYPE_PTE: ENCODING ERROR! Original=%p Encoded=0x%llx Decoded=%p\n",
                   PointerPte, (ULONG64)NewPte->u.Long, Decoded);
    }
#endif
}

//
// ARM64 Prototype PTE Decoder Macros
//
// These macros decode the shift-based prototype PTE encoding used by ARM64.
// MI_MAKE_PROTOTYPE_PTE encodes: PTE.u.Long = (ProtoPteAddress << 16) | 0x0400 (Prototype bit)
//

//
// Macro: MI_IS_PROTO_PTE
// Check if a PTE is a prototype PTE pointer (not a valid hardware PTE)
//
#define MI_IS_PROTO_PTE(Pte) \
    (((Pte)->u.Proto.Prototype == 1) && ((Pte)->u.Hard.Valid == 0))

//
// Macro: MI_PROTO_PTE_ADDRESS
// Decode a prototype PTE pointer to get the address of the actual prototype PTE
//
// On ARM64, the address is encoded by shifting left by 16 bits.
// To decode: shift right by 16 bits with sign extension to preserve high kernel bits.
//
// Example:
//   Original: 0xFFFFF8A000007C30 (kernel address)
//   Encoded:  0xFFFFF8A000007C30 << 16 = 0xF8A000007C300000
//                                         (0x400 bit set for Prototype)
//                                       = 0xF8A000007C300400
//   Decoded:  (INT64)0xF8A000007C300400 >> 16 = 0xFFFFF8A000007C30 (sign-extended)
//
// CRITICAL: Must use signed right-shift (INT64) to preserve high bits for kernel addresses.
//
#define MI_PROTO_PTE_ADDRESS(Pte) \
    ((PMMPTE)(((INT64)(Pte)->u.Long) >> 16))

//
// Alternative decoder that extracts the address from the bitfield union
// (in case the shift-based encoding changes in the future)
//
// Note: On ARM64, we rely on the simple shift encoding for performance.
// The bitfield union (ProtoAddressLow/High) is not used.
//
#define MI_PROTO_PTE_ADDRESS_FROM_BITFIELD(Pte) \
    ((PMMPTE)(((ULONG_PTR)(Pte)->u.Proto.ProtoAddressHigh << 32) | \
              ((ULONG_PTR)(Pte)->u.Proto.ProtoAddressLow)))

#define MiSubsectionPteToSubsection(x)                              \
        (PMMPTE)((LONG64)(x)->u.Subsect.SubsectionAddress)

//
// ARM64-specific MM diagnostics and helper functions
//
VOID
MiArm64CheckSystemViewSpacePte(
    _In_z_ PCSTR Location
);

//
// ARM64-specific internal MM functions
//
NTSTATUS
NTAPI
MiArm64MapUserPage(
    _In_ ULONG_PTR VirtualAddress,
    _In_ PFN_NUMBER PageFrameNumber,
    _In_ ULONG Protection
);

// ARM64 mm.h included successfully - shift-based MI_MAKE_PROTOTYPE_PTE active

