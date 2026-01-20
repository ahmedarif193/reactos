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

#define PXE_BASE    0xFFFFF6FB7DBED000ULL
#define PXE_SELFMAP 0xFFFFF6FB7DBEDF68ULL
#define PPE_BASE    0xFFFFF6FB7DA00000ULL
#define PDE_BASE    0xFFFFF6FB40000000ULL
#define PTE_BASE    0xFFFFF68000000000ULL
#define PXE_TOP     0xFFFFF6FB7DBEDFFFULL
#define PPE_TOP     0xFFFFF6FB7DBFFFFFULL
#define PDE_TOP     0xFFFFF6FB7FFFFFFFULL
#define PTE_TOP     0xFFFFF6FFFFFFFFFFULL

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
#define HYPER_SPACE                            0xFFFFF70000000000ULL
#define HYPER_SPACE_END                        0xFFFFF77FFFFFFFFFULL
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
#define MI_HYPERSPACE_PTES                     (256 - 1)
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
 */
FORCEINLINE
BOOLEAN
MiIsPdeForAddressValid(PVOID Address)
{
    return ((MiAddressToPxe(Address)->u.Hard.Valid) &&
            (MiAddressToPpe(Address)->u.Hard.Valid) &&
            (MiAddressToPde(Address)->u.Hard.Valid));
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

#define MiSubsectionPteToSubsection(x)                              \
        (PMMPTE)((LONG64)(x)->u.Subsect.SubsectionAddress)

//
// ARM64-specific MM diagnostics and helper functions
//
VOID
MiArm64CheckSystemViewSpacePte(
    _In_z_ PCSTR Location
);

// ARM64 mm.h included successfully - shift-based MI_MAKE_PROTOTYPE_PTE active

