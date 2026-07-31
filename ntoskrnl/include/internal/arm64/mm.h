/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * Kernel internal memory management definitions for arm64
 *
 * The layout follows the Windows 11 ARM64 split where user space occupies
 * the lower 47-bit window and kernel space begins at 0xFFFF8000`00000000.
 */
#pragma once

extern NTKERNELAPI ULONG64 MmUserProbeAddress;
#define MM_USER_PROBE_ADDRESS MmUserProbeAddress

VOID
NTAPI
MiArm64ProbeForWrite(
    _In_ ULONG_PTR Current,
    _In_ ULONG_PTR Last);

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

#define ARM64_PTE_CACHE_SHIFT        2
#define ARM64_PTE_CACHE_MASK         (7ULL << ARM64_PTE_CACHE_SHIFT)
#define ARM64_PTE_CACHE_WB           (4ULL << ARM64_PTE_CACHE_SHIFT)
#define ARM64_PTE_CACHE_UC           (2ULL << ARM64_PTE_CACHE_SHIFT)
#define ARM64_PTE_CACHE_WC           (3ULL << ARM64_PTE_CACHE_SHIFT)
#define ARM64_PTE_NG                 (1ULL << 11)
#define ARM64_PTE_PXN                (1ULL << 53)
#define ARM64_PTE_UXN                (1ULL << 54)
#define ARM64_PTE_DBM                (1ULL << 51)
#define ARM64_PTE_WRITE              (1ULL << 55)
#define ARM64_PTE_COPY_ON_WRITE      (1ULL << 56)
#define ARM64_PTE_SH_INNER           (3ULL << 8)

#define PTE_READONLY            (ARM64_PTE_PXN | ARM64_PTE_UXN)
#define PTE_EXECUTE             ARM64_PTE_PXN
#define PTE_EXECUTE_READ        ARM64_PTE_PXN
#define PTE_READWRITE           (ARM64_PTE_PXN | ARM64_PTE_UXN | ARM64_PTE_WRITE)
#define PTE_WRITECOPY           (ARM64_PTE_PXN | ARM64_PTE_UXN | ARM64_PTE_COPY_ON_WRITE)
#define PTE_EXECUTE_READWRITE   (ARM64_PTE_PXN | ARM64_PTE_WRITE)
#define PTE_EXECUTE_WRITECOPY   (ARM64_PTE_PXN | ARM64_PTE_COPY_ON_WRITE)
#define PTE_PROTOTYPE           0x0000000000000400ULL

/*
 * Match the Win11 ARM64 MAIR layout visible to kernel debuggers:
 * slot 0 and 4 are Normal WB, slot 1 is Device-nGnRnE, and slots 2/3
 * are Normal NC. Keep ordinary ARM3 mappings on slot 4 so existing PTEs
 * stay Normal WB.
 */
#define PTE_ENABLE_CACHE        ARM64_PTE_CACHE_WB
#define PTE_DISABLE_CACHE       ARM64_PTE_CACHE_UC
#define PTE_WRITECOMBINED_CACHE ARM64_PTE_CACHE_WC
#define PTE_PROTECT_MASK        (ARM64_PTE_PXN | ARM64_PTE_UXN | ARM64_PTE_WRITE | ARM64_PTE_COPY_ON_WRITE | ARM64_PTE_CACHE_MASK)
#define PTE_VALID               0x0000000000000001ULL
#define PTE_ACCESSED            0x0000000000000400ULL
#define PTE_DIRTY               0ULL   /* ARM64 uses AP[2]+DBM instead of a software dirty bit. */

#define ARM64_PTE_TYPE_MASK     0x0000000000000003ULL
#define ARM64_PTE_TYPE_INVALID  0x0000000000000000ULL
#define ARM64_PTE_TYPE_BLOCK    0x0000000000000001ULL
#define ARM64_PTE_TYPE_PAGE     0x0000000000000003ULL
#define ARM64_PTE_TYPE_TABLE    0x0000000000000003ULL
#define ARM64_PTE_ADDR_MASK     0x0000FFFFFFFFF000ULL
#define ARM64_PTE_TABLE_NT_FLAGS (ARM64_PTE_NG | ARM64_PTE_PXN | ARM64_PTE_UXN | ARM64_PTE_WRITE)

/*
 * Windows ARM64 exposes upper-level table entries through the recursive
 * PXE/PPE/PDE views. Preserve the NT-observed ignored table policy bits while
 * software walkers mask physical addresses with ARM64_PTE_ADDR_MASK. Table
 * descriptors do not carry a leaf AttrIndx; Win11 leaves bits [4:2] clear.
 */
#define ARM64_PTE_TABLE_DESCRIPTOR_ATTRS \
    (ARM64_PTE_TYPE_TABLE | ARM64_PTE_SH_INNER | PTE_ACCESSED | ARM64_PTE_TABLE_NT_FLAGS)
#define ARM64_MAKE_TABLE_DESCRIPTOR(Pfn) \
    ((((ULONG64)(Pfn)) << PAGE_SHIFT) | ARM64_PTE_TABLE_DESCRIPTOR_ATTRS)

/*
 * ARM64 Self-Map Architecture for TTBR0/TTBR1 Split
 *
 * On ARM64, user addresses (0x0000...) are translated via TTBR0_EL1 and
 * kernel addresses (0xFFFF...) are translated via TTBR1_EL1. The NT Memory
 * Manager's self-map is built into TTBR1's hierarchy at L0 index 493.
 *
 * L0[493] points back to the active TTBR1 root and L0[494] is hyperspace,
 * matching the Windows ARM64 contract expected by boot loaders and ARM3.
 */
#define PXE_SELFMAP_INDEX   493

#define PXE_BASE    0xFFFFF6FB7DBED000ULL
#define PXE_SELFMAP 0xFFFFF6FB7DBEDF68ULL
#define PPE_BASE    0xFFFFF6FB7DA00000ULL
#define PDE_BASE    0xFFFFF6FB40000000ULL
#define PTE_BASE    0xFFFFF68000000000ULL
#define PXE_TOP     0xFFFFF6FB7DBEDFFFULL
#define PPE_TOP     0xFFFFF6FB7DBFFFFFULL
#define PDE_TOP     0xFFFFF6FB7FFFFFFFULL
#define PTE_TOP     0xFFFFF6FFFFFFFFFFULL

C_ASSERT(PXE_SELFMAP_INDEX == 493);
C_ASSERT((((PXE_BASE >> PXI_SHIFT) & PXI_MASK) == PXE_SELFMAP_INDEX));
C_ASSERT((((PPE_BASE >> PXI_SHIFT) & PXI_MASK) == PXE_SELFMAP_INDEX));
C_ASSERT((((PDE_BASE >> PXI_SHIFT) & PXI_MASK) == PXE_SELFMAP_INDEX));
C_ASSERT((((PTE_BASE >> PXI_SHIFT) & PXI_MASK) == PXE_SELFMAP_INDEX));
C_ASSERT(PXE_SELFMAP == (PXE_BASE + (PXE_SELFMAP_INDEX * 8ULL)));
C_ASSERT(ARM64_PTE_TABLE_DESCRIPTOR_ATTRS == 0x00E0000000000F03ULL);

#define KSEG0_BASE  0xFFFF800000000000ULL
#define MI_ARM64_BOOT_IMAGE_BASE       0xFFFFF80000000000ULL
#define MI_ARM64_PHYS_MAP_BASE         0xFFFFFC0000000000ULL
#define MI_ARM64_PHYS_ADDR_MASK        0x0000FFFFFFFFFFFFULL
#define MI_LOADER_MAPPINGS             MI_ARM64_BOOT_IMAGE_BASE
#define MI_ARM64_PHYS_TO_VA(Phys) \
    ((ULONG_PTR)(MI_ARM64_PHYS_MAP_BASE | ((ULONG64)(Phys) & MI_ARM64_PHYS_ADDR_MASK)))
#define MI_ARM64_PFN_TO_VA(Pfn) \
    MI_ARM64_PHYS_TO_VA(((ULONG64)(Pfn)) << PAGE_SHIFT)

#define _MI_PAGING_LEVELS 4
#define _MI_HAS_NO_EXECUTE 1

/* Virtual address layout for ARM64.
 * The kernel is mapped at KSEG0_BASE (0xFFFF800000000000), so system range
 * must start at or below that address and have bit 47 set for valid canonical
 * upper-half addresses on ARM64 with 48-bit VAs.
 */
#define MI_USER_PROBE_ADDRESS           (PVOID)0x00007FFFFFFF0000ULL
#define MI_DEFAULT_SYSTEM_RANGE_START   (PVOID)0xFFFF800000000000ULL
#define MI_REAL_SYSTEM_RANGE_START             0xFFFF800000000000ULL
#define HYPER_SPACE                            0xFFFFF70000000000ULL
#define HYPER_SPACE_END                        0xFFFFF77FFFFFFFFFULL
#define MI_SYSTEM_SPACE_START                  0xFFFFF88000000000ULL
#define MI_SESSION_SPACE_START                 0xFFFFF90000000000ULL
#define MI_SESSION_SPACE_END                   0xFFFFF98000000000ULL
#define MI_HIGHEST_SYSTEM_ADDRESS       (PVOID)0xFFFFFFFFFFFFFFFFULL

#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS         ((PVOID)0x0000000000010000ULL)
#endif

/*
 * Keep blind user allocations out of the first and last TTBR0 L1 slots.
 * These slots remain available for explicit callers, but Windows ARM64 does
 * not populate them with ordinary process setup allocations.
 */
#define MI_LOWEST_AUTOMATIC_USER_ADDRESS  0x0000000040000000ULL
#define MI_HIGHEST_AUTOMATIC_USER_ADDRESS 0x00007FFFBFFFFFFFULL

/* WOW64 compatibility. */
#define MM_HIGHEST_USER_ADDRESS_WOW64   0x7FFEFFFF
#define MM_SYSTEM_RANGE_START_WOW64     0x80000000

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
C_ASSERT(MI_SECONDARY_COLORS == 64);
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
#define MI_PAGE_DISABLE_CACHE(x)   MI_SET_PTE_ATTR_INDEX((x), MI_ARM64_MAIR_NORMAL_NC_IDX)
#define MI_PAGE_WRITE_THROUGH(x)   MI_SET_PTE_ATTR_INDEX((x), MI_ARM64_MAIR_NORMAL_NC_IDX)
#define MI_PAGE_WRITE_COMBINED(x)  MI_SET_PTE_ATTR_INDEX((x), MI_ARM64_MAIR_NORMAL_WC_IDX)
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

#define MI_WRITE_VALID_PPE MI_WRITE_VALID_PDE
#define MI_WRITE_VALID_PXE MI_WRITE_VALID_PDE
#define ValidKernelPpe ValidKernelPde

#if defined(_M_ARM64)
extern BOOLEAN MiArm64SelfMapReady;

VOID
MiArm64InitializeSystemPageDirectoryLock(VOID);

VOID
MiArm64MapKseg0Page(
    _In_ PFN_NUMBER PageFrameNumber);

VOID
NTAPI
MiArm64UnmapEarlyDeviceAliases(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

_Must_inspect_result_
BOOLEAN
MiArm64FillSystemPageDirectory(
    _In_ PVOID Base,
    _In_ SIZE_T NumberOfBytes);

VOID
MiArm64FinalizeProcessAddressSpace(
    _Inout_ PEPROCESS Process,
    _In_ PFN_NUMBER RootPfn,
    _In_ PFN_NUMBER HyperPfn);

VOID
MiArm64RemoveProcessAddressSpace(
    _Inout_ PEPROCESS Process);

BOOLEAN
MiArm64ResolveSoftwareDirtyFault(
    _In_ PVOID FaultAddress,
    _Inout_ PMMPTE PointerPte);

/*
 * Per-CPU PFN-lock recursion depth (writer: ke/arm64/spinlock.c on every
 * PFN-lock acquire/release; reader: MiArm64AllocatePageTablePage, own CPU
 * only). One slot per cache line so the hottest MM lock's accounting never
 * bounces a shared line between CPUs.
 */
typedef struct DECLSPEC_CACHEALIGN _MI_ARM64_PFN_LOCK_DEPTH
{
    volatile LONG Depth;
} MI_ARM64_PFN_LOCK_DEPTH;

extern MI_ARM64_PFN_LOCK_DEPTH MiArm64PfnLockDepth[];

VOID
MiArm64IncrementUserLeafPteCount(
    _In_ PFN_NUMBER PteFrame);

NTSTATUS
MiArm64AllocateCleanPage(
    _Inout_ PEPROCESS Process,
    _Out_ PPFN_NUMBER PageFrame);

NTSTATUS
MiArm64EnsureUserPte(
    _Inout_ PEPROCESS Process,
    _In_ PVOID Address,
    _Outptr_ PMMPTE *PointerPte,
    _Out_opt_ PPFN_NUMBER L3TablePfn);

typedef struct _MI_ARM64_USER_PTE_WALK
{
    volatile ULONG64 *PointerPte;
    ULONG64 PteValue;
    ULONG Depth;
    /* Per-level KSEG0 table bases and table-page PFNs (L0..L3) captured
     * during the walk so release paths never re-walk the hierarchy. */
    volatile ULONG64 *LevelTable[4];
    PFN_NUMBER LevelPfn[4];
} MI_ARM64_USER_PTE_WALK, *PMI_ARM64_USER_PTE_WALK;

BOOLEAN
MiArm64GetUserPteAddressForProcess(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ PMI_ARM64_USER_PTE_WALK Walk);

BOOLEAN
MiArm64ReleaseUserPageTableReferenceLocked(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare,
    _In_ PMI_ARM64_USER_PTE_WALK Walk);

VOID
MiArm64ReleaseUserPageTableReference(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _In_ BOOLEAN ReleaseLeafShare,
    _In_ PMI_ARM64_USER_PTE_WALK Walk);

VOID
MiArm64PruneEmptyUserPageTables(
    _In_ PEPROCESS Process,
    _In_ PVOID Address);
#endif

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

FORCEINLINE
BOOLEAN
MiIsUserPxe(PVOID Address)
{
    return MiIsUserAddress(Address);
}

FORCEINLINE
BOOLEAN
MiIsUserPpe(PVOID Address)
{
    return MiIsUserAddress(Address);
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

FORCEINLINE
BOOLEAN
MiArm64IsAddressValid(
    _In_ PVOID Address)
{
    ULONG64 Ttbr;
    ULONG64 RootPa;
    volatile ULONG64 *Table;
    ULONG64 Entry;
    ULONG_PTR Va;
    BOOLEAN IsPageTableAddress;

    Va = (ULONG_PTR)Address;
    IsPageTableAddress = (Va >= PTE_BASE) && (Va <= PTE_TOP);

    if ((MiArm64SelfMapReady) &&
        (Va >= (ULONG_PTR)MmSystemRangeStart) &&
        !IsPageTableAddress)
    {
        Entry = ((volatile MMPTE *)MiAddressToPxe(Address))->u.Long;
        if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
        {
            return TRUE;
        }
        if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        {
            return FALSE;
        }

        Entry = ((volatile MMPTE *)MiAddressToPpe(Address))->u.Long;
        if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
        {
            return TRUE;
        }
        if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        {
            return FALSE;
        }

        Entry = ((volatile MMPTE *)MiAddressToPde(Address))->u.Long;
        if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
        {
            return TRUE;
        }
        if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
        {
            return FALSE;
        }

        Entry = ((volatile MMPTE *)MiAddressToPte(Address))->u.Long;
        return ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_PAGE);
    }

    if (MiIsUserAddress(Address))
    {
        __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr));
    }
    else if (Va >= (ULONG_PTR)MmSystemRangeStart)
    {
        __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr));
    }
    else
    {
        return FALSE;
    }

    RootPa = Ttbr & ARM64_PTE_ADDR_MASK;
    if (RootPa == 0)
    {
        return FALSE;
    }

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
    Entry = Table[(Va >> PXI_SHIFT) & PXI_MASK];
    if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
    {
        return FALSE;
    }

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
    Entry = Table[(Va >> PPI_SHIFT) & PPI_MASK];
    if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
    {
        return TRUE;
    }
    if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
    {
        return FALSE;
    }

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
    Entry = Table[(Va >> PDI_SHIFT) & PDI_MASK_ARM64];
    if ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_BLOCK)
    {
        return TRUE;
    }
    if ((Entry & ARM64_PTE_TYPE_MASK) != ARM64_PTE_TYPE_TABLE)
    {
        return FALSE;
    }

    Table = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
    Entry = Table[(Va >> PTI_SHIFT) & PTI_MASK_ARM64];

    return ((Entry & ARM64_PTE_TYPE_MASK) == ARM64_PTE_TYPE_PAGE);
}

FORCEINLINE
BOOLEAN
MiArm64ProbeForAccess(
    _In_ PVOID Address,
    _In_ BOOLEAN Write)
{
    ULONG64 Par;

    if (Write)
    {
        __asm__ __volatile__("at s1e1w, %0" :: "r"(Address));
    }
    else
    {
        __asm__ __volatile__("at s1e1r, %0" :: "r"(Address));
    }
    __asm__ __volatile__("isb" ::: "memory");
    __asm__ __volatile__("mrs %0, par_el1" : "=r"(Par));

    return ((Par & 1ULL) == 0);
}

/*
 * Hardware address translation via AT. Owns the AT/PAR idiom together with
 * MiArm64ProbeForAccess; MmGetPhysicalAddress dispatches here on ARM64.
 */
FORCEINLINE
BOOLEAN
MiArm64TranslateToPhysical(
    _In_ PVOID Address,
    _Out_ PPHYSICAL_ADDRESS PhysicalAddress)
{
    ULONG_PTR Va = (ULONG_PTR)Address;
    ULONG64 Par;

    PhysicalAddress->QuadPart = 0;

    if (MiIsUserAddress(Address))
    {
        __asm__ __volatile__(
            "at s1e0r, %1\n\t"
            "isb\n\t"
            "mrs %0, par_el1"
            : "=r"(Par)
            : "r"(Va)
            : "memory");
    }
    else
    {
        __asm__ __volatile__(
            "at s1e1r, %1\n\t"
            "isb\n\t"
            "mrs %0, par_el1"
            : "=r"(Par)
            : "r"(Va)
            : "memory");
    }

    if (Par & 1ULL)
        return FALSE;

    PhysicalAddress->QuadPart =
        (Par & ARM64_PTE_ADDR_MASK) | (Va & (PAGE_SIZE - 1));
    return TRUE;
}

FORCEINLINE
VOID
MiArm64CleanEntryToPoC(
    _In_ volatile VOID *Entry)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("dc civac, %0" :: "r"(Entry) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
}

FORCEINLINE
VOID
MiArm64CleanPteRangeToPoC(
    _In_reads_(EntryCount) PMMPTE FirstEntry,
    _In_ ULONG EntryCount)
{
    ULONG DcacheLineSize;
    ULONG IcacheLineSize;
    ULONG_PTR Current;
    ULONG_PTR End;

    if (EntryCount == 0)
    {
        return;
    }

    KiArm64GetCacheLineSizes(&DcacheLineSize, &IcacheLineSize);
    UNREFERENCED_PARAMETER(IcacheLineSize);
    Current = (ULONG_PTR)FirstEntry & ~(ULONG_PTR)(DcacheLineSize - 1);
    End = (ULONG_PTR)(FirstEntry + EntryCount);

    __asm__ __volatile__("dsb ishst" ::: "memory");
    while (Current < End)
    {
        __asm__ __volatile__("dc civac, %0" :: "r"(Current) : "memory");
        Current += DcacheLineSize;
    }
    __asm__ __volatile__("dsb ish" ::: "memory");
}

/* Single owner of the per-VA user TLB invalidate sequence (all-ASID, all-level, broadcast). */
FORCEINLINE
VOID
MiArm64InvalidateUserAddress(
    _In_ PVOID Address)
{
    ULONG_PTR Va = ((ULONG_PTR)Address >> PAGE_SHIFT) & KI_ARM64_TLBI_VA_MASK;

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaae1is, %0" :: "r"(Va) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
MiArm64CleanPageToPoC(
    _In_ PVOID PageVa)
{
    ULONG_PTR Va = (ULONG_PTR)PAGE_ALIGN(PageVa);
    ULONG_PTR End = Va + PAGE_SIZE;
    ULONG DcacheLineSize;
    ULONG IcacheLineSize;

    /* Honor the real D-line size: a hardcoded 64 under-cleans on 128-byte-line cores. */
    KiArm64GetCacheLineSizes(&DcacheLineSize, &IcacheLineSize);

    for (; Va < End; Va += DcacheLineSize)
    {
        __asm__ __volatile__("dc civac, %0" :: "r"((PVOID)Va) : "memory");
    }
    __asm__ __volatile__("dsb ish" ::: "memory");
}

/*
 * MiArm64SyncPxeWrite - Propagate PXE-level self-map writes to the TTBR1 root.
 *
 * PXE_BASE is the kernel recursive view. User page tables are reached through
 * TTBR0 physical walks, so PXE writes must never be redirected to TTBR0 based
 * on the entry index.
 */
FORCEINLINE
VOID
MiArm64SyncPxeWrite(
    _In_ PMMPTE PointerPxe)
{
    ULONG_PTR Addr = (ULONG_PTR)PointerPxe;
    ULONG_PTR Index;
    ULONG64 Root;
    volatile ULONG64 *RootL0;

    /* Only act on PXE-level self-map entries */
    if (Addr < PXE_BASE || Addr > PXE_TOP)
        return;

    Index = (Addr - PXE_BASE) / sizeof(MMPTE);
    if (Index >= PXE_PER_PAGE)
        return;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Root));

    Root &= ARM64_PTE_ADDR_MASK;
    if (Root == 0)
        return;

    RootL0 = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(Root);
    RootL0[Index] = PointerPxe->u.Long;
    MiArm64CleanEntryToPoC(&RootL0[Index]);
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
 * Writes through returned pointer go directly to hardware page tables.
 * Caller must issue TLBI (KeInvalidateTlbEntry) for the MAPPED VA after writes.
 */

/*
 * MiArm64KernelTableKseg0 - single TTBR1 KSEG0 descent core.
 *
 * StopLevel selects the returned entry: 1 = PPE (L1), 2 = PDE (L2),
 * 3 = PTE (L3). Returns NULL if any intermediate entry is not a valid
 * table descriptor. StopLevel is always a constant at the call sites, so
 * FORCEINLINE unrolls the loop to the same code as the previous per-level
 * copies.
 */
FORCEINLINE
PMMPTE
MiArm64KernelTableKseg0(
    _In_ PVOID Address,
    _In_ ULONG StopLevel)
{
    ULONG64 Ttbr1;
    ULONG64 Entry;
    PULONG64 Table;
    ULONG Level;

    ASSERT(Address >= MmSystemRangeStart);

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    Table = (PULONG64)MI_ARM64_PHYS_TO_VA(Ttbr1 & ARM64_PTE_ADDR_MASK);

    for (Level = 0; Level < StopLevel; Level++)
    {
        Entry = Table[((ULONG_PTR)Address >> (PXI_SHIFT - (Level * 9))) & PXI_MASK];
        if ((Entry & 3) != 3) return NULL;
        Table = (PULONG64)MI_ARM64_PHYS_TO_VA(Entry & ARM64_PTE_ADDR_MASK);
    }

    return (PMMPTE)&Table[((ULONG_PTR)Address >> (PXI_SHIFT - (StopLevel * 9))) & PXI_MASK];
}

/* Get L1 (PPE-level) entry for kernel VA via KSEG0, or NULL */
FORCEINLINE
PMMPTE
MiArm64KernelPpeKseg0(_In_ PVOID Address)
{
    return MiArm64KernelTableKseg0(Address, 1);
}

/* Get L2 (PDE-level) entry for kernel VA via KSEG0, or NULL */
FORCEINLINE
PMMPTE
MiArm64KernelPdeKseg0(_In_ PVOID Address)
{
    return MiArm64KernelTableKseg0(Address, 2);
}

/* Check whether the TTBR1 hierarchy for a kernel VA has a valid PDE. */
FORCEINLINE
BOOLEAN
MiIsPdeForAddressValid(PVOID Address)
{
    PMMPTE Pde;

    ASSERT(Address >= MmSystemRangeStart);

    Pde = MiArm64KernelPdeKseg0(Address);
    return ((Pde != NULL) && ((Pde->u.Long & 1ULL) != 0));
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
    L0 = (PULONG64)MI_ARM64_PHYS_TO_VA(Ttbr0 & ARM64_PTE_ADDR_MASK);

    E0 = L0[((ULONG_PTR)Address >> PXI_SHIFT) & PXI_MASK];
    if ((E0 & 3) != 3) return NULL;

    L1 = (PULONG64)MI_ARM64_PHYS_TO_VA(E0 & ARM64_PTE_ADDR_MASK);
    E1 = L1[((ULONG_PTR)Address >> PPI_SHIFT) & PPI_MASK];
    if ((E1 & 3) != 3) return NULL;

    L2 = (PULONG64)MI_ARM64_PHYS_TO_VA(E1 & ARM64_PTE_ADDR_MASK);
    E2 = L2[((ULONG_PTR)Address >> PDI_SHIFT) & PDI_MASK_ARM64];
    if ((E2 & 3) != 3) return NULL;

    *L3TablePfn = (PFN_NUMBER)((E2 & ARM64_PTE_ADDR_MASK) >> PAGE_SHIFT);
    L3 = (PULONG64)MI_ARM64_PHYS_TO_VA(E2 & ARM64_PTE_ADDR_MASK);
    return (PMMPTE)&L3[((ULONG_PTR)Address >> PTI_SHIFT) & PTI_MASK_ARM64];
}

/* Get L3 (PTE-level) entry for user VA via KSEG0, or NULL if hierarchy missing */
FORCEINLINE
PMMPTE
MiArm64UserPteKseg0(_In_ PVOID Address)
{
    PFN_NUMBER Unused;

    return MiArm64UserPteKseg0ForPfn(Address, &Unused);
}

/*
 * MiArm64KernelPteKseg0 - Get the real TTBR1 L3 PTE entry via KSEG0.
 *
 * Kernel mappings are translated through TTBR1. The recursive self-map can
 * diverge from the real TTBR1 walk for ordinary kernel mappings, so callers
 * that need to guarantee a hardware-visible PTE update must use this helper.
 */
FORCEINLINE
PMMPTE
MiArm64KernelPteKseg0(
    _In_ PVOID Address)
{
    return MiArm64KernelTableKseg0(Address, 3);
}

FORCEINLINE
VOID
MiArm64SyncKernelHierarchyEntryWrite(
    _In_ PMMPTE PointerEntry);

/*
 * MiArm64SyncKernelLeafPteWriteTo - mirror-store tail for callers that have
 * already resolved the KSEG0 L3 slot (avoids a second TTBR1 walk).
 */
FORCEINLINE
VOID
MiArm64SyncKernelLeafPteWriteTo(
    _In_ PMMPTE PointerPte,
    _In_ PMMPTE Kseg0Pte)
{
    *(volatile ULONG64 *)Kseg0Pte = PointerPte->u.Long;
    MiArm64CleanEntryToPoC(Kseg0Pte);
}

/*
 * MiArm64SyncKernelLeafPteWrite - Mirror a kernel leaf PTE write into TTBR1.
 *
 * For ordinary kernel mappings, writes through the recursive self-map update
 * the alias view but can fail to reach the real TTBR1 L3 entry. Mirror the
 * new value into the hardware page table before any TLBI so the retry sees the
 * same PTE that the kernel just installed.
 */
FORCEINLINE
VOID
MiArm64SyncKernelLeafPteWrite(
    _In_ PMMPTE PointerPte)
{
    PMMPTE PteForAddr;
    PVOID VirtualAddress;
    PMMPTE Kseg0Pte;

    if (((ULONG_PTR)PointerPte < PTE_BASE) ||
        ((ULONG_PTR)PointerPte > PTE_TOP))
    {
        MiArm64CleanEntryToPoC(PointerPte);
        return;
    }

    VirtualAddress = MiPteToAddress(PointerPte);
    if ((ULONG_PTR)VirtualAddress < (ULONG_PTR)MmSystemRangeStart)
    {
        MiArm64CleanEntryToPoC(PointerPte);
        return;
    }
    if (((ULONG_PTR)VirtualAddress >= PTE_BASE) &&
        ((ULONG_PTR)VirtualAddress <= HYPER_SPACE_END))
    {
        return;
    }

    Kseg0Pte = MiArm64KernelPteKseg0(VirtualAddress);
    if (Kseg0Pte == NULL)
    {
        MiArm64SyncKernelHierarchyEntryWrite((PMMPTE)MiAddressToPxe(VirtualAddress));
        MiArm64SyncKernelHierarchyEntryWrite((PMMPTE)MiAddressToPpe(VirtualAddress));
        MiArm64SyncKernelHierarchyEntryWrite((PMMPTE)MiAddressToPde(VirtualAddress));
        Kseg0Pte = MiArm64KernelPteKseg0(VirtualAddress);
    }

    ASSERT(Kseg0Pte != NULL);
    if (Kseg0Pte == NULL)
        return;

    MiArm64SyncKernelLeafPteWriteTo(PointerPte, Kseg0Pte);
}

/*
 * MiArm64SyncKernelHierarchyEntryWrite - Mirror kernel PXE/PPE/PDE writes into TTBR1.
 *
 * Upper-level kernel self-map writes can diverge from the real TTBR1 hierarchy
 * just like leaf PTE writes. Propagate the updated entry to the hardware-visible
 * table before any caller-issued TLBI so the next walk sees the new hierarchy.
 */
FORCEINLINE
VOID
MiArm64SyncKernelHierarchyEntryWrite(
    _In_ PMMPTE PointerEntry)
{
    ULONG_PTR EntryAddress = (ULONG_PTR)PointerEntry;
    PVOID VirtualAddress = NULL;
    PMMPTE Kseg0Entry = NULL;

    if ((EntryAddress >= PXE_BASE) && (EntryAddress <= PXE_TOP))
    {
        MiArm64SyncPxeWrite(PointerEntry);
        return;
    }

    if ((EntryAddress >= PPE_BASE) && (EntryAddress <= PPE_TOP))
    {
        VirtualAddress = MiPpeToAddress(PointerEntry);
        if ((ULONG_PTR)VirtualAddress < (ULONG_PTR)MmSystemRangeStart)
        {
            MiArm64CleanEntryToPoC(PointerEntry);
            return;
        }
        Kseg0Entry = MiArm64KernelPpeKseg0(VirtualAddress);
    }
    else if ((EntryAddress >= PDE_BASE) && (EntryAddress <= PDE_TOP))
    {
        VirtualAddress = MiPdeToAddress(PointerEntry);
        if ((ULONG_PTR)VirtualAddress < (ULONG_PTR)MmSystemRangeStart)
        {
            MiArm64CleanEntryToPoC(PointerEntry);
            return;
        }
        Kseg0Entry = MiArm64KernelPdeKseg0(VirtualAddress);
    }
    else
    {
        return;
    }

    if (((ULONG_PTR)VirtualAddress >= PTE_BASE) &&
        ((ULONG_PTR)VirtualAddress <= HYPER_SPACE_END))
    {
        return;
    }

    ASSERT(Kseg0Entry != NULL);
    if (Kseg0Entry == NULL)
        return;

    *(volatile ULONG64 *)Kseg0Entry = PointerEntry->u.Long;
    MiArm64CleanEntryToPoC(Kseg0Entry);
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
// ARM64 canonical kernel addresses need signed shift encoding. Bitfield storage
// truncates the upper 16 address bits before decode can sign-extend them.
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

FORCEINLINE
VOID
MI_MAKE_SUBSECTION_PTE(
    _Out_ PMMPTE NewPte,
    _In_ PVOID Segment)
{
    /* Mark this as a prototype */
    NewPte->u.Long = 0;
    NewPte->u.Subsect.Prototype = 1;

    /* Store the lower 48 bits of the Segment address */
    NewPte->u.Subsect.SubsectionAddress = ((ULONG_PTR)Segment & 0x0000FFFFFFFFFFFF);
}

FORCEINLINE
BOOLEAN
MI_IS_MAPPED_PTE(
    _In_ PMMPTE PointerPte)
{
    return ((PointerPte->u.Hard.Valid != 0) ||
            (PointerPte->u.Proto.Prototype != 0) ||
            (PointerPte->u.Trans.Transition != 0) ||
            (PointerPte->u.Hard.PageFrameNumber != 0));
}

#define MI_HAS_ARCH_IS_PHYSICAL_ADDRESS
FORCEINLINE
BOOLEAN
MI_IS_PHYSICAL_ADDRESS(
    _In_ PVOID Address)
{
    PMMPDE PointerPde;

    if (MiIsUserAddress(Address))
    {
        return FALSE;
    }

    if ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart)
    {
        return FALSE;
    }

    PointerPde = MiAddressToPde(Address);
    return (PointerPde->u.Hard.Valid && MI_IS_PAGE_LARGE(PointerPde));
}

/*
 * ARM64 MAIR (Memory Attribute Indirection Register) Index Constants
 *
 * These constants define which MAIR_EL1 slot corresponds to which memory
 * attribute encoding. Both the bootloader (FreeLoader mmu_v2.c) and the
 * kernel (boot.c KiArm64EnsureMairNormalWb) program MAIR_EL1 with the
 * following layout:
 *
 *   Index 0: Normal WB      (0xFF)
 *   Index 1: Device-nGnRnE  (0x00)
 *   Index 2: Normal-NC      (0x44)
 *   Index 3: Normal-NC      (0x44)
 *   Index 4: Normal WB      (0xFF)
 *   Index 5: Device-nGnRnE  (0x00)
 *   Index 6: Normal-NC      (0x44)
 *   Index 7: Normal-NC      (0x44)
 *
 * All code that constructs AttrIndx fields in PTEs MUST use these constants
 * instead of hardcoding the numeric index. The AttrIndx field occupies bits
 * [4:2] of an L3 page descriptor (or block descriptor).
 *
 * Usage:  pte |= ((ULONG64)MI_ARM64_MAIR_NORMAL_WB_IDX << 2);
 */
#define MI_ARM64_MAIR_DEVICE_nGnRnE_IDX    1
#define MI_ARM64_MAIR_NORMAL_NC_IDX         2
#define MI_ARM64_MAIR_NORMAL_WC_IDX         3
#define MI_ARM64_MAIR_NORMAL_WB_IDX         4

/*
 * ARM64 Execute Permission Architectural Constants
 *
 * These values are architectural (ARMv8-A) and are normally defined with
 * the rest of the ARM64 PTE bit layout near the top of this header.
 * The guards keep this section local if it is reused in a narrower include
 * context later.
 */
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

FORCEINLINE
ULONG
MiGetPteCacheAttribute(
    _In_ PMMPTE PointerPte)
{
    return (ULONG)(PointerPte->u.Hard.CacheType | (PointerPte->u.Hard.OsAvailable2 << 2));
}


// ARM64 mm.h included successfully - shift-based MI_MAKE_PROTOTYPE_PTE active
