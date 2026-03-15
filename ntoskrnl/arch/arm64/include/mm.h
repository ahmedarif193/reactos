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
#define MI_NUMBER_SYSTEM_PTES                   50000
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
/*
 * MI_SET_PTE_ATTR_INDEX - Set the full 3-bit AttrIndx field (bits [4:2]).
 *
 * ARM64 page descriptors use bits [4:2] as the MAIR attribute index.
 * The HARDWARE_PTE struct splits this across CacheType (bits [3:2]) and
 * OsAvailable2 (bit [4]).  We must clear and set ALL THREE bits to avoid
 * inheriting bit [4] from the ValidKernelPte template (which has
 * AttrIndx=4 = Normal WB).  Without this, MmMapIoSpace Device-nGnRnE
 * (AttrIndx=0) mappings silently become Normal WB (AttrIndx=4), causing
 * GIC MMIO accesses to be cached and stalling the system.
 */
#define MI_SET_PTE_ATTR_INDEX(x, idx)            \
    do                                           \
    {                                            \
        (x)->u.Hard.CacheType = (ULONG)((idx) & 0x3); \
        (x)->u.Hard.OsAvailable2 = (ULONG)(((idx) >> 2) & 0x1); \
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

/*
 * MiArm64SyncPxeWrite - Propagate PXE-level self-map writes to real L0 pages.
 *
 * The recursive self-map through the merged PXE page means that writes to
 * PXE-level entries (addresses in [PXE_BASE..PXE_TOP]) modify the merged
 * page, NOT the real TTBR0/TTBR1 L0 page. This function reads back the
 * current value from the merged page and propagates it to the appropriate
 * real L0 page so the hardware TLB walker sees the change.
 *
 * For user indices (0-255): write to TTBR0's L0 via KSEG0
 * For kernel indices (256-511, except 493): write to TTBR1's L0 via KSEG0
 * Index 493 (the self-map entry) is NOT propagated - it only exists in the
 * merged page and must NOT overwrite the real L0[493] which also points to
 * the merged page.
 *
 * Called after every PTE write operation (MI_WRITE_VALID_PTE, etc.) when the
 * target address falls in the PXE self-map range.
 */
FORCEINLINE
VOID
MiArm64SyncPxeWrite(
    _In_ PMMPTE PointerPxe)
{
    ULONG_PTR Addr = (ULONG_PTR)PointerPxe;
    ULONG Index;
    UINT64 Ttbr;
    volatile UINT64 *RealL0;

    /* Only act on PXE-level self-map entries */
    if (Addr < PXE_BASE || Addr > PXE_TOP)
        return;

    Index = (ULONG)((Addr - PXE_BASE) / sizeof(MMPTE));

    /* Don't propagate the self-referential entry - it only lives in merged page */
    if (Index == PXE_SELFMAP_INDEX)
        return;

    if (Index < 256)
    {
        /* User L0 entry - propagate to TTBR0's real L0 */
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr));
    }
    else
    {
        /* Kernel L0 entry - propagate to TTBR1's real L0 */
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr));
    }

    RealL0 = (volatile UINT64 *)(KSEG0_BASE | (Ttbr & 0x0000FFFFFFFFF000ULL));
    RealL0[Index] = PointerPxe->u.Long;
    __asm__ __volatile__("dsb ishst" ::: "memory");
}

/*
 * ARM64 KSEG0-Based User PTE Access
 * ===================================
 *
 * Walk TTBR0 page tables via KSEG0 physical identity mapping to access
 * user-space PTEs directly, without going through the TTBR1 self-map.
 *
 * This eliminates the stale self-map alias problem (Bugs #42, #43, #56, #60,
 * #61, #62) by reading the CURRENT TTBR0_EL1 and walking physical page tables.
 *
 * Returns KSEG0-mapped pointer (safe at any IRQL, never stale), or NULL
 * if the page table hierarchy doesn't extend to the requested level.
 *
 * Performance: 3 memory reads (L0→L1→L2) to reach L3 PTE.
 *   Compare: MiArm64SyncSelfMapForUserAddress does 4 reads + 4 writes = SLOWER.
 *   For bulk iteration, use MiArm64UserL3BaseKseg0 to get L3 base, iterate in-page.
 *
 * Writes through returned pointer go directly to hardware page tables.
 * Caller must issue TLBI (KeInvalidateTlbEntry) for the MAPPED VA after writes.
 */

/* Get L3 (PTE-level) entry for user VA via KSEG0, or NULL if hierarchy missing */
FORCEINLINE
PMMPTE
MiArm64UserPteKseg0(_In_ PVOID Address)
{
    ULONG64 Ttbr0;
    PULONG64 L0, L1, L2, L3;
    ULONG64 E0, E1, E2;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    L0 = (PULONG64)(KSEG0_BASE | (Ttbr0 & 0x0000FFFFFFFFF000ULL));

    E0 = L0[((ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK];
    if ((E0 & 3) != 3) return NULL;

    L1 = (PULONG64)(KSEG0_BASE | (E0 & 0x0000FFFFFFFFF000ULL));
    E1 = L1[((ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK];
    if ((E1 & 3) != 3) return NULL;

    L2 = (PULONG64)(KSEG0_BASE | (E1 & 0x0000FFFFFFFFF000ULL));
    E2 = L2[((ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64];
    if ((E2 & 3) != 3) return NULL;

    L3 = (PULONG64)(KSEG0_BASE | (E2 & 0x0000FFFFFFFFF000ULL));
    return (PMMPTE)&L3[((ULONG_PTR)Address >> PTI_SHIFT) & PTI_MASK_ARM64];
}

/* Get L2 (PDE-level) entry for user VA via KSEG0, or NULL */
FORCEINLINE
PMMPTE
MiArm64UserPdeKseg0(_In_ PVOID Address)
{
    ULONG64 Ttbr0;
    PULONG64 L0, L1, L2;
    ULONG64 E0, E1;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    L0 = (PULONG64)(KSEG0_BASE | (Ttbr0 & 0x0000FFFFFFFFF000ULL));

    E0 = L0[((ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK];
    if ((E0 & 3) != 3) return NULL;

    L1 = (PULONG64)(KSEG0_BASE | (E0 & 0x0000FFFFFFFFF000ULL));
    E1 = L1[((ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK];
    if ((E1 & 3) != 3) return NULL;

    L2 = (PULONG64)(KSEG0_BASE | (E1 & 0x0000FFFFFFFFF000ULL));
    return (PMMPTE)&L2[((ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64];
}

/*
 * MiArm64UserL3BaseKseg0 — Get KSEG0 base of L3 table for efficient iteration.
 *
 * Returns a pointer to the START of the L3 page table containing the PTE for
 * the given address. Index with MiAddressToPti(Va) for individual PTEs.
 *
 * Example:
 *   PMMPTE L3Base = MiArm64UserL3BaseKseg0((PVOID)Va);
 *   if (L3Base) {
 *       PMMPTE Pte = &L3Base[MiAddressToPti((PVOID)Va)];
 *       for (...) { process *Pte; Pte++; }
 *   }
 */
FORCEINLINE
PMMPTE
MiArm64UserL3BaseKseg0(_In_ PVOID Address)
{
    ULONG64 Ttbr0;
    PULONG64 L0, L1, L2;
    ULONG64 E0, E1, E2;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    L0 = (PULONG64)(KSEG0_BASE | (Ttbr0 & 0x0000FFFFFFFFF000ULL));

    E0 = L0[((ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK];
    if ((E0 & 3) != 3) return NULL;

    L1 = (PULONG64)(KSEG0_BASE | (E0 & 0x0000FFFFFFFFF000ULL));
    E1 = L1[((ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK];
    if ((E1 & 3) != 3) return NULL;

    L2 = (PULONG64)(KSEG0_BASE | (E1 & 0x0000FFFFFFFFF000ULL));
    E2 = L2[((ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64];
    if ((E2 & 3) != 3) return NULL;

    return (PMMPTE)(KSEG0_BASE | (E2 & 0x0000FFFFFFFFF000ULL));
}

/*
 * MiArm64UserPteKseg0ForPfn — Get L3 PTE entry and its L3 table PFN.
 *
 * Combined helper for callers that need both the PTE pointer and the
 * page table PFN (for ShareCount/UsedPageTableEntries tracking).
 * Avoids redundant TTBR0 walks.
 */
FORCEINLINE
PMMPTE
MiArm64UserPteKseg0ForPfn(
    _In_ PVOID Address,
    _Out_ PFN_NUMBER *L3TablePfn)
{
    ULONG64 Ttbr0;
    PULONG64 L0, L1, L2, L3;
    ULONG64 E0, E1, E2;

    *L3TablePfn = 0;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    L0 = (PULONG64)(KSEG0_BASE | (Ttbr0 & 0x0000FFFFFFFFF000ULL));

    E0 = L0[((ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK];
    if ((E0 & 3) != 3) return NULL;

    L1 = (PULONG64)(KSEG0_BASE | (E0 & 0x0000FFFFFFFFF000ULL));
    E1 = L1[((ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK];
    if ((E1 & 3) != 3) return NULL;

    L2 = (PULONG64)(KSEG0_BASE | (E1 & 0x0000FFFFFFFFF000ULL));
    E2 = L2[((ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64];
    if ((E2 & 3) != 3) return NULL;

    *L3TablePfn = (PFN_NUMBER)((E2 & 0x0000FFFFFFFFF000ULL) >> PAGE_SHIFT);
    L3 = (PULONG64)(KSEG0_BASE | (E2 & 0x0000FFFFFFFFF000ULL));
    return (PMMPTE)&L3[((ULONG_PTR)Address >> PTI_SHIFT) & PTI_MASK_ARM64];
}

/*
 * MiArm64UserPteSelfMapForPfn — Self-map based PTE lookup for user addresses.
 *
 * Equivalent to MiArm64UserPteKseg0ForPfn but uses the recursive self-map
 * (MiAddressToPxe/Ppe/Pde/Pte) instead of KSEG0 physical walks.
 * Returns NULL if any page table level is not yet allocated.
 * The L3 table PFN is extracted from the PDE (which points to the L3 table).
 */
FORCEINLINE
PMMPTE
MiArm64UserPteSelfMapForPfn(
    _In_ PVOID Address,
    _Out_ PFN_NUMBER *L3TablePfn)
{
    PMMPTE PointerPxe, PointerPpe, PointerPde;

    *L3TablePfn = 0;

    PointerPxe = MiAddressToPxe(Address);
    if (PointerPxe->u.Hard.Valid == 0) return NULL;

    PointerPpe = MiAddressToPpe(Address);
    if (PointerPpe->u.Hard.Valid == 0) return NULL;

    PointerPde = MiAddressToPde(Address);
    if (PointerPde->u.Hard.Valid == 0) return NULL;

    *L3TablePfn = PFN_FROM_PTE(PointerPde);
    return MiAddressToPte(Address);
}

/*
 * MiArm64WriteValidUserPte - Write a valid PTE for a user address via KSEG0.
 *
 * Bypasses the TTBR1 recursive self-map entirely: reads TTBR0_EL1 and walks
 * L0->L1->L2 physical page tables through KSEG0 to locate the L3 PTE slot,
 * then writes TempPte directly into the hardware page table.
 *
 * TLB coherency: DSB ISHST -> TLBI VAE1IS -> DSB ISH -> ISB after the write.
 *
 * Race check: if the PTE slot already contains the exact same value (another
 * CPU or earlier path raced us), the write and TLBI are skipped.
 *
 * On NULL return from the KSEG0 walk (page table hierarchy incomplete),
 * KeBugCheckEx is issued -- the caller asserts that hierarchy exists.
 *
 * @param UserAddress   User virtual address (must be < MmSystemRangeStart)
 * @param TempPte       The valid PTE value to write
 */
FORCEINLINE
VOID
MiArm64WriteValidUserPte(
    _In_ PVOID UserAddress,
    _In_ MMPTE TempPte)
{
    PMMPTE Kseg0Pte;
    ULONG64 OldValue;

    /* Assert user address and valid PTE */
    ASSERT((ULONG_PTR)UserAddress < MI_REAL_SYSTEM_RANGE_START);
    ASSERT(TempPte.u.Hard.Valid == 1);

    /* Walk TTBR0 page tables via KSEG0 to get the L3 PTE pointer */
    Kseg0Pte = MiArm64UserPteKseg0(UserAddress);
    if (Kseg0Pte == NULL)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, 0xBAD00001,
                     (ULONG_PTR)UserAddress, 0, 0);
    }

    /* DMB ISH: ensure all prior stores are visible before reading old PTE */
    __asm__ __volatile__("dmb ish" ::: "memory");

    /* Race check: skip if exact same value already present */
    OldValue = *(volatile ULONG64 *)Kseg0Pte;
    if (OldValue == TempPte.u.Long)
        return;

    /* Write the PTE through KSEG0 (directly into hardware page table) */
    *(volatile ULONG64 *)Kseg0Pte = TempPte.u.Long;

    /* TLB invalidation sequence for the user VA */
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vae1is, %0" :: "r"((ULONG_PTR)UserAddress >> PAGE_SHIFT) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

/*
 * MiArm64UpdateValidUserPte - Update permissions on an already-valid user PTE.
 *
 * Similar to MiArm64WriteValidUserPte but for permission changes on an
 * already-mapped page. Asserts that the old PTE is valid and that the PFN
 * is unchanged (only permission/attribute bits may differ).
 *
 * @param UserAddress   User virtual address (must be < MmSystemRangeStart)
 * @param TempPte       The new valid PTE value (same PFN, different permissions)
 */
FORCEINLINE
VOID
MiArm64UpdateValidUserPte(
    _In_ PVOID UserAddress,
    _In_ MMPTE TempPte)
{
    PMMPTE Kseg0Pte;
    MMPTE OldPte;

    /* Assert user address and valid PTE */
    ASSERT((ULONG_PTR)UserAddress < MI_REAL_SYSTEM_RANGE_START);
    ASSERT(TempPte.u.Hard.Valid == 1);

    /* Walk TTBR0 page tables via KSEG0 to get the L3 PTE pointer */
    Kseg0Pte = MiArm64UserPteKseg0(UserAddress);
    if (Kseg0Pte == NULL)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, 0xBAD00001,
                     (ULONG_PTR)UserAddress, 0, 0);
    }

    /* DMB ISH: ensure all prior stores are visible before reading old PTE */
    __asm__ __volatile__("dmb ish" ::: "memory");

    /* Read old PTE and assert it is valid with unchanged PFN */
    OldPte.u.Long = *(volatile ULONG64 *)Kseg0Pte;
    ASSERT(OldPte.u.Hard.Valid == 1);
    ASSERT(OldPte.u.Hard.PageFrameNumber == TempPte.u.Hard.PageFrameNumber);

    /* Race check: skip if exact same value already present */
    if (OldPte.u.Long == TempPte.u.Long)
        return;

    /* Write the updated PTE through KSEG0 */
    *(volatile ULONG64 *)Kseg0Pte = TempPte.u.Long;

    /* TLB invalidation sequence for the user VA */
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vae1is, %0" :: "r"((ULONG_PTR)UserAddress >> PAGE_SHIFT) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
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

/*
 * ARM64 MAIR (Memory Attribute Indirection Register) Index Constants
 *
 * These constants define which MAIR_EL1 slot corresponds to which memory
 * attribute encoding. Both the bootloader (FreeLoader mmu_v2.c) and the
 * kernel (boot.c KiArm64EnsureMairNormalWb) program MAIR_EL1 with the
 * following layout:
 *
 *   Index 0: Device-nGnRnE  (0x00)
 *   Index 1: Normal-NC      (0x44) -- also used as Device-nGnRE in loader
 *   Index 2: Normal-WC      (0x44) -- also used as Device-GRE in loader
 *   Index 3: Normal-NC      (0x44)
 *   Index 4: Normal WB      (0xFF) -- Inner/Outer Write-Back, Read/Write Allocate
 *   Index 5-7: unused / reserved
 *
 * All code that constructs AttrIndx fields in PTEs MUST use these constants
 * instead of hardcoding the numeric index. The AttrIndx field occupies bits
 * [4:2] of an L3 page descriptor (or block descriptor).
 *
 * Usage:  pte |= ((ULONG64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2);
 */
#define MI_ARM64_MAIR_DEVICE_nGnRnE_IDX    0
#define MI_ARM64_MAIR_NORMAL_NC_IDX         1
#define MI_ARM64_MAIR_NORMAL_WC_IDX         2
#define MI_ARM64_MAIR_NORMAL_WB_IDX         4

/*
 * ARM64 Execute Permission Architectural Constants
 *
 * These are raw bit definitions for PXN and UXN, defined here so that
 * mm.h macros and inline functions can reference them without depending
 * on miarm.h's ARM64_PTE_PXN/ARM64_PTE_UXN (which is included after
 * this header). These values are architectural (ARMv8-A) and will not
 * change.
 *
 * If ARM64_PTE_PXN is already defined (when included from miarm.h
 * context after the #define), the compiler will simply use the same
 * value. The #ifndef guards prevent redefinition warnings.
 */
#ifndef ARM64_PTE_PXN
#define ARM64_PTE_PXN           (1ULL << 53)
#endif
#ifndef ARM64_PTE_UXN
#define ARM64_PTE_UXN           (1ULL << 54)
#endif
#ifndef ARM64_PTE_WRITE
#define ARM64_PTE_WRITE         (1ULL << 55)
#endif
#ifndef ARM64_PTE_COPY_ON_WRITE
#define ARM64_PTE_COPY_ON_WRITE (1ULL << 56)
#endif

/*
 * ARM64 Execute Permission Policy
 *
 * On ARM64, execute permissions are controlled by TWO independent bits:
 *   PXN (bit 53): Privileged eXecute Never -- blocks EL1 (kernel) execution
 *   UXN (bit 54): Unprivileged eXecute Never -- blocks EL0 (user) execution
 *
 * The correct execute policy depends on whether the page is a USER or KERNEL
 * mapping:
 *
 *   User executable:    PXN=1, UXN=0  (user can execute; kernel cannot = SMEP)
 *   User non-exec:      PXN=1, UXN=1  (neither can execute)
 *   Kernel executable:  PXN=0, UXN=1  (kernel can execute; user cannot)
 *   Kernel non-exec:    PXN=1, UXN=1  (neither can execute)
 *
 * The generic PTE_EXECUTE / PTE_EXECUTE_READ / PTE_EXECUTE_READWRITE macros
 * in miarm.h use USER semantics (PXN=1, UXN=0) because MmProtectToPteMask[]
 * is consumed by MI_MAKE_HARDWARE_PTE / MI_MAKE_HARDWARE_PTE_USER which
 * create user-mode page table entries.
 *
 * PROBLEM: MI_MAKE_HARDWARE_PTE_KERNEL also indexes MmProtectToPteMask[],
 * which means kernel executable mappings (kernel .text, drivers) get PXN=1,
 * making them non-executable at EL1 -- an instant crash.
 *
 * SOLUTION: Provide kernel-specific execute macros and a fixup function that
 * converts user-execute PTE bits to kernel-execute PTE bits. The fixup swaps
 * PXN and UXN: where user-execute sets PXN=1/UXN=0, kernel-execute needs
 * PXN=0/UXN=1.
 *
 * NOTE: PTE_NOEXECUTE (PXN=1, UXN=1) is the safe default and is identical
 * to PTE_READONLY's execute bits. Non-executable pages always set both bits.
 */

/* User-mode execute bits (same as PTE_EXECUTE in miarm.h, repeated for clarity) */
#define PTE_EXECUTE_USER         (ARM64_PTE_PXN)                              /* PXN=1, UXN=0 */
#define PTE_EXECUTE_USER_READ    (ARM64_PTE_PXN)                              /* PXN=1, UXN=0 */
#define PTE_EXECUTE_USER_RW      (ARM64_PTE_PXN | ARM64_PTE_WRITE)            /* PXN=1, UXN=0, W */
#define PTE_EXECUTE_USER_WC      (ARM64_PTE_PXN | ARM64_PTE_COPY_ON_WRITE)    /* PXN=1, UXN=0, CoW */

/* Kernel-mode execute bits: PXN=0 allows EL1 execution, UXN=1 blocks EL0 */
#define PTE_EXECUTE_KERNEL       (ARM64_PTE_UXN)                              /* PXN=0, UXN=1 */
#define PTE_EXECUTE_KERNEL_READ  (ARM64_PTE_UXN)                              /* PXN=0, UXN=1 */
#define PTE_EXECUTE_KERNEL_RW    (ARM64_PTE_UXN | ARM64_PTE_WRITE)            /* PXN=0, UXN=1, W */
#define PTE_EXECUTE_KERNEL_WC    (ARM64_PTE_UXN | ARM64_PTE_COPY_ON_WRITE)    /* PXN=0, UXN=1, CoW */

/* No-execute for either privilege level */
#define PTE_NOEXECUTE            (ARM64_PTE_PXN | ARM64_PTE_UXN)              /* PXN=1, UXN=1 */

/*
 * MiArm64FixupKernelExecutePte - Convert user-execute PTE bits to kernel-execute.
 *
 * When MI_MAKE_HARDWARE_PTE_KERNEL applies MmProtectToPteMask[], the execute
 * bits are in user-mode format (PXN=1, UXN=0). For kernel pages, we need the
 * opposite: PXN=0, UXN=1.
 *
 * This function detects the user-execute pattern and swaps PXN/UXN:
 *   If PXN=1 and UXN=0 (user-executable), change to PXN=0 and UXN=1 (kernel-executable).
 *   If PXN=1 and UXN=1 (non-executable), leave unchanged (correct for both).
 *   If PXN=0 and UXN=0 (both-executable), leave unchanged (not typically used).
 *   If PXN=0 and UXN=1 (already kernel-executable), leave unchanged.
 *
 * Call this AFTER applying MmProtectToPteMask[] but BEFORE writing the PTE.
 */
FORCEINLINE
VOID
MiArm64FixupKernelExecutePte(
    _Inout_ PMMPTE Pte)
{
    ULONG64 PxnBit = Pte->u.Long & ARM64_PTE_PXN;
    ULONG64 UxnBit = Pte->u.Long & ARM64_PTE_UXN;

    if (PxnBit && !UxnBit)
    {
        /*
         * User-execute pattern detected (PXN=1, UXN=0).
         * Convert to kernel-execute (PXN=0, UXN=1).
         */
        Pte->u.Long &= ~ARM64_PTE_PXN;
        Pte->u.Long |= ARM64_PTE_UXN;
    }
    /* All other combinations are already correct for kernel mappings:
     * - PXN=1, UXN=1: non-executable (correct)
     * - PXN=0, UXN=1: kernel-executable (already correct)
     * - PXN=0, UXN=0: both-executable (permissive but valid)
     */
}

/*
 * MI_IS_PAGE_KERNEL_EXECUTABLE - Check if a page is executable at EL1.
 *
 * On ARM64, kernel execution requires PXN=0. UXN is irrelevant for EL1.
 * The HARDWARE_PTE field name is PrivilegedNoExecute (= PXN bit 53).
 */
#define MI_IS_PAGE_KERNEL_EXECUTABLE(x) ((x)->u.Hard.PrivilegedNoExecute == 0)

/*
 * MmProtectToPteMaskKernel - Kernel-mode protection to PTE mask table.
 *
 * This table is the kernel-execute counterpart to MmProtectToPteMask[].
 * It uses PXN=0/UXN=1 for executable entries instead of PXN=1/UXN=0.
 *
 * Usage: MI_MAKE_HARDWARE_PTE_KERNEL should use this table instead of
 * MmProtectToPteMask[], or alternatively call MiArm64FixupKernelExecutePte()
 * after applying MmProtectToPteMask[].
 */
extern const ULONG_PTR MmProtectToPteMaskKernel[32];

/*
 * MiArm64HandleUserAccessFlagFault - Set AF on user page table entry.
 *
 * ARM64 hardware (with TCR.HA=0) raises access flag faults (DFSC 0x08-0x0B)
 * when a page table entry has AF=0 (bit 10). This walks TTBR0 page tables
 * via KSEG0 to the faulting level and sets AF=1.
 *
 * Returns TRUE if AF was set, FALSE if the walk failed (entry not present).
 * Caller must issue TLBI after a successful return.
 */
FORCEINLINE
BOOLEAN
MiArm64HandleUserAccessFlagFault(
    _In_ PVOID Address,
    _In_ ULONG FaultLevel)
{
    ULONG64 Ttbr0;
    PULONG64 Table;
    ULONG64 Entry;
    ULONG Idx;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    ULONG64 RootPa = Ttbr0 & 0x0000FFFFFFFFF000ULL;
    if (RootPa == 0) return FALSE;

    Table = (PULONG64)(KSEG0_BASE | RootPa);

    /* L0 */
    Idx = ((ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK;
    Entry = Table[Idx];
    if (FaultLevel == 0) { if (!(Entry & 1ULL)) return FALSE; Table[Idx] = Entry | (1ULL << 10); return TRUE; }
    if ((Entry & 3ULL) != 3ULL) return FALSE;

    /* L1 */
    Table = (PULONG64)(KSEG0_BASE | (Entry & 0x0000FFFFFFFFF000ULL));
    Idx = ((ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK;
    Entry = Table[Idx];
    if (FaultLevel == 1) { if (!(Entry & 1ULL)) return FALSE; Table[Idx] = Entry | (1ULL << 10); return TRUE; }
    if ((Entry & 3ULL) != 3ULL) return FALSE;

    /* L2 */
    Table = (PULONG64)(KSEG0_BASE | (Entry & 0x0000FFFFFFFFF000ULL));
    Idx = ((ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64;
    Entry = Table[Idx];
    if (FaultLevel == 2) { if (!(Entry & 1ULL)) return FALSE; Table[Idx] = Entry | (1ULL << 10); return TRUE; }
    if ((Entry & 3ULL) != 3ULL) return FALSE;

    /* L3 */
    Table = (PULONG64)(KSEG0_BASE | (Entry & 0x0000FFFFFFFFF000ULL));
    Idx = ((ULONG_PTR)Address >> PTI_SHIFT) & PTI_MASK_ARM64;
    Entry = Table[Idx];
    if (FaultLevel == 3) { if (!(Entry & 1ULL)) return FALSE; Table[Idx] = Entry | (1ULL << 10); return TRUE; }

    return FALSE;
}

/**
 * MiArm64ClearTtbr0Entry - Clear a TTBR0 page table entry at the specified level.
 *
 * Walks TTBR0 via KSEG0 to the specified level and zeros the entry.
 * Issues TLBI + barriers to ensure coherency.
 *
 * @param UserVa   User virtual address identifying the entry
 * @param Level    Which level to clear: 0=L0, 1=L1, 2=L2
 */
FORCEINLINE
VOID
MiArm64ClearTtbr0Entry(
    _In_ PVOID UserVa,
    _In_ ULONG Level)
{
    ULONG64 Ttbr0Pa;
    volatile ULONG64 *Table;
    ULONG64 Entry;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0Pa));
    Ttbr0Pa &= 0x0000FFFFFFFFF000ULL;
    if (Ttbr0Pa == 0) return;

    Table = (volatile ULONG64 *)(KSEG0_BASE | Ttbr0Pa);

    if (Level == 0)
    {
        /* Clear L0 entry */
        Table[((ULONG64)(ULONG_PTR)UserVa >> 39) & 0x1FF] = 0;
    }
    else
    {
        Entry = Table[((ULONG64)(ULONG_PTR)UserVa >> 39) & 0x1FF];
        if ((Entry & 0x3ULL) != 0x3ULL) return;
        Table = (volatile ULONG64 *)(KSEG0_BASE | (Entry & 0x0000FFFFFFFFF000ULL));

        if (Level == 1)
        {
            /* Clear L1 entry */
            Table[((ULONG64)(ULONG_PTR)UserVa >> 30) & 0x1FF] = 0;
        }
        else
        {
            Entry = Table[((ULONG64)(ULONG_PTR)UserVa >> 30) & 0x1FF];
            if ((Entry & 0x3ULL) != 0x3ULL) return;
            Table = (volatile ULONG64 *)(KSEG0_BASE | (Entry & 0x0000FFFFFFFFF000ULL));
            /* Clear L2 entry */
            Table[((ULONG64)(ULONG_PTR)UserVa >> 21) & 0x1FF] = 0;
        }
    }

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaale1is, %0" :: "r"((ULONG_PTR)UserVa >> PAGE_SHIFT) : "memory");
    __asm__ __volatile__("dsb ish\n\tisb" ::: "memory");
}

// ARM64 mm.h included successfully - shift-based MI_MAKE_PROTOTYPE_PTE active
