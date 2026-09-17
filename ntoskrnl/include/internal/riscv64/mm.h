/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RV64 memory-manager interfaces.
 *
 * Sv39, three levels. ARM3 names them PPE (level 2, the satp root), PDE
 * (level 1) and PTE (level 0). PTE/PDE/PPE pointers are addresses inside the
 * page-table window described by docs/riscv64/page-table-window.md
 * (ABI-125): a mirror hierarchy maintained by mm/riscv64/window.c gives the
 * shared ARM3 code the linear entry arrays it expects. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* All supervisor page-table levels use the same 64-bit entry format. */
typedef MMPTE MMPDE, *PMMPDE;
typedef MMPTE MMPPE, *PMMPPE;

#define _MI_PAGING_LEVELS 3
#define _MI_HAS_NO_EXECUTE 1
#define PTI_SHIFT 12
#define PDI_SHIFT 21
#define PPI_SHIFT 30
#define PTE_PER_PAGE 512
#define PDE_PER_PAGE 512
#define PPE_PER_PAGE 512
#define PTI_MASK (PTE_PER_PAGE - 1)
#define PDI_MASK (PDE_PER_PAGE - 1)
#define PPI_MASK (PPE_PER_PAGE - 1)
#define PDE_MAPPED_VA (1ULL << PDI_SHIFT)
#define PPE_MAPPED_VA (1ULL << PPI_SHIFT)
#define SESSION_POOL_LOOKASIDES 21

/* Sv39 canonical halves. Keep a 64 KiB guard at each address-space top;
 * the kernel guard also excludes the prototype-PTE lookup sentinel range. */
#define MI_DEFAULT_SYSTEM_RANGE_START 0xFFFFFFC000000000ULL
#define MI_USER_PROBE_ADDRESS         0x0000003FFFFF0000ULL
#define MI_HIGHEST_SYSTEM_ADDRESS     0xFFFFFFFFFFFEFFFFULL
#define MI_RISCV_GIB(Index)           (MI_DEFAULT_SYSTEM_RANGE_START + ((ULONG64)(Index) << PPI_SHIFT))

/*
 * Page-table window (ABI-125). Root slot W = 0x17F of the supervisor half.
 * The arithmetic is the x64 self-map arithmetic with 39-bit addresses.
 */
#define MI_RISCV_WINDOW_INDEX 0x17FULL
#define MI_RISCV_HYPERSPACE_INDEX 0x101ULL
#define PTE_BASE    0xFFFFFFDFC0000000ULL
#define PTE_TOP     0xFFFFFFDFFFFFFFFFULL
#define PDE_BASE    0xFFFFFFDFEFE00000ULL
#define PDE_TOP     0xFFFFFFDFEFFFFFFFULL
#define PPE_BASE    0xFFFFFFDFEFF7F000ULL
#define PPE_TOP     0xFFFFFFDFEFF7FFFFULL
#define PPE_SELFMAP (PPE_BASE + MI_RISCV_WINDOW_INDEX * sizeof(MMPTE))
#define MI_RISCV_VA_MASK ((1ULL << 39) - 1)
C_ASSERT((PTE_BASE & MI_RISCV_VA_MASK) == (MI_RISCV_WINDOW_INDEX << PPI_SHIFT));
C_ASSERT(PDE_BASE == PTE_BASE + (((PTE_BASE & MI_RISCV_VA_MASK) >> PTI_SHIFT) << 3));
C_ASSERT(PPE_BASE == PTE_BASE + (((PDE_BASE & MI_RISCV_VA_MASK) >> PTI_SHIFT) << 3));
C_ASSERT(PPE_BASE == PDE_BASE + (MI_RISCV_WINDOW_INDEX << PTI_SHIFT));
C_ASSERT(PTE_TOP == PTE_BASE + (1ULL << PPI_SHIFT) - 1);
C_ASSERT(PDE_TOP == PDE_BASE + (1ULL << PDI_SHIFT) - 1);

/* Per-process region: hyperspace, dummy PTE, VAD bitmap, working-set list. */
#define HYPER_SPACE            MI_RISCV_GIB(MI_RISCV_HYPERSPACE_INDEX - PPE_PER_PAGE / 2)
#define HYPER_SPACE_END        (HYPER_SPACE + PPE_MAPPED_VA - 1)
#define MI_HYPERSPACE_PTES     (256 - 1)
#define MI_ZERO_PTES           32
#define MI_MAPPING_RANGE_START HYPER_SPACE
#define MI_MAPPING_RANGE_END   (MI_MAPPING_RANGE_START + MI_HYPERSPACE_PTES * PAGE_SIZE)
#define MI_DUMMY_PTE           (MI_MAPPING_RANGE_END + PAGE_SIZE)
#define MI_VAD_BITMAP          (MI_DUMMY_PTE + PAGE_SIZE)
#define MI_WORKING_SET_LIST    (MI_VAD_BITMAP + PAGE_SIZE)
C_ASSERT(MI_WORKING_SET_LIST + PAGE_SIZE <= HYPER_SPACE + PDE_MAPPED_VA);

/* Session layout carved from the reserved session region (see layout.c). */
#define MI_SESSION_VIEW_SIZE          (512ULL * 1024 * 1024)
#define MI_SESSION_POOL_SIZE          (64ULL * 1024 * 1024)
#define MI_SESSION_IMAGE_SIZE         (16ULL * 1024 * 1024)
#define MI_SESSION_WORKING_SET_SIZE   (16ULL * 1024 * 1024)
#define MI_SESSION_SIZE               (MI_SESSION_VIEW_SIZE + MI_SESSION_POOL_SIZE + \
                                       MI_SESSION_IMAGE_SIZE + MI_SESSION_WORKING_SET_SIZE)
#define MI_SYSTEM_VIEW_SIZE           (512ULL * 1024 * 1024)

/* Common pool buckets and sizing policy, not hardware geometry. */
#define MI_MAX_FREE_PAGE_LISTS 4
/* 242000 PTEs = 945 MiB, inside the 1 GiB system-PTE reservation. */
#define MI_NUMBER_SYSTEM_PTES (11000 * 22)
#define MI_MIN_INIT_PAGED_POOLSIZE (32ULL * 1024 * 1024)
#define MI_MAX_INIT_NONPAGED_POOL_SIZE (8ULL * 1024 * 1024 * 1024)
#define MI_MAX_NONPAGED_POOL_SIZE (8ULL * 1024 * 1024 * 1024)
#define MI_MIN_PAGES_FOR_NONPAGED_POOL_TUNING ((255ULL * 1024 * 1024) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_TUNING ((19ULL * 1024 * 1024) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST ((32ULL * 1024 * 1024) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST_BOOST ((256ULL * 1024 * 1024) >> PAGE_SHIFT)
#define MI_MIN_ALLOCATION_FRAGMENT (4ULL * 1024)
#define MI_ALLOCATION_FRAGMENT (64ULL * 1024)
#define MI_MAX_ALLOCATION_FRAGMENT (2ULL * 1024 * 1024)
#define MI_MIN_SECONDARY_COLORS 8
#define MI_SECONDARY_COLORS 64
#define MI_MAX_SECONDARY_COLORS 1024

/* Retain the common NT allocation margins within the Sv39 user half. */
#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS ((PVOID)MM_ALLOCATION_GRANULARITY)
#endif
#define MM_HIGHEST_VAD_ADDRESS ((PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE)))
/* NT's fixed user shared-data view; the writable kernel alias is separate. */
#define MM_SHARED_USER_DATA_VA 0x7FFE0000UL
/* The 64-bit NT ZeroBits count limit is independent of the Sv39 VA limit. */
#define MI_MAX_ZERO_BITS 53

/* Private MmAccessFault flags, never raw scause. Trap entry must derive
 * PRESENT from the stabilized translation and WRITE/EXECUTE from the cause. */
#define MI_RISCV_FAULT_PRESENT 0x01
#define MI_RISCV_FAULT_WRITE   0x02
#define MI_RISCV_FAULT_EXECUTE 0x10
#define MI_IS_NOT_PRESENT_FAULT(Code) (!BooleanFlagOn((Code), MI_RISCV_FAULT_PRESENT))
#define MI_IS_WRITE_ACCESS(Code) BooleanFlagOn((Code), MI_RISCV_FAULT_WRITE)
#define MI_IS_INSTRUCTION_FETCH(Code) BooleanFlagOn((Code), MI_RISCV_FAULT_EXECUTE)

#define MM_PTE_SOFTWARE_PROTECTION_BITS 1
#define MM_EMPTY_PTE_LIST 0xFFFFFFFFULL
#define MM_EMPTY_LIST ((ULONG_PTR)-1)

#define MI_RISCV_PTE_VALID       0x001ULL
#define MI_RISCV_PTE_READ        0x002ULL
#define MI_RISCV_PTE_WRITE       0x004ULL
#define MI_RISCV_PTE_EXECUTE     0x008ULL
#define MI_RISCV_PTE_OWNER       0x010ULL
#define MI_RISCV_PTE_GLOBAL      0x020ULL
#define MI_RISCV_PTE_ACCESSED    0x040ULL
#define MI_RISCV_PTE_DIRTY       0x080ULL
#define MI_RISCV_PTE_COPYONWRITE 0x100ULL
#define MI_RISCV_PTE_TRANSITION  0x100ULL /* Only when Valid == 0. */
#define MI_RISCV_PTE_PROTOTYPE   0x200ULL
#define MI_RISCV_PTE_LEAF_MASK   (MI_RISCV_PTE_READ | MI_RISCV_PTE_WRITE | MI_RISCV_PTE_EXECUTE)
#define MI_RISCV_PTE_PFN_MASK    0x003FFFFFFFFFFC00ULL
#define MI_RISCV_PTE_PFN_SHIFT   10
#define MI_RISCV_PTE_RESERVED    0xFFC0000000000000ULL
#define MI_RISCV_PFN_MAX         0x00000FFFFFFFFFFFULL
#define MI_TRANSITION_PFN_MASK MI_RISCV_PTE_PFN_MASK

/* Reserve the top two pagefile offsets for wait and prototype lookup states. */
#define MI_RISCV_PAGEFILE_WAIT 0xFFFFFFFEULL
#define MI_RISCV_PTE_LOOKUP_NEEDED 0xFFFFFFFFULL /* Equals miarm.h MI_PTE_LOOKUP_NEEDED. */
#define MI_RISCV_MAXIMUM_PAGEFILE_SIZE (MI_RISCV_PAGEFILE_WAIT * PAGE_SIZE)

/* Hardware access bits only. Cache attributes are a separate platform policy. */
#define PTE_READONLY MI_RISCV_PTE_READ
/* NT PAGE_EXECUTE retains the readable-executable behavior used by the
 * loader with FILE_EXECUTE-only handles, as on the other NT targets. Keep
 * the raw ISA X bit separate; this is not a global MXR relaxation. */
#define PTE_EXECUTE (MI_RISCV_PTE_READ | MI_RISCV_PTE_EXECUTE)
#define PTE_EXECUTE_READ (MI_RISCV_PTE_READ | MI_RISCV_PTE_EXECUTE)
#define PTE_READWRITE (MI_RISCV_PTE_READ | MI_RISCV_PTE_WRITE)
#define PTE_WRITECOPY (MI_RISCV_PTE_READ | MI_RISCV_PTE_COPYONWRITE)
#define PTE_EXECUTE_READWRITE (PTE_READWRITE | MI_RISCV_PTE_EXECUTE)
#define PTE_EXECUTE_WRITECOPY (PTE_WRITECOPY | MI_RISCV_PTE_EXECUTE)
#define PTE_PROTOTYPE MI_RISCV_PTE_PROTOTYPE
#define PTE_VALID MI_RISCV_PTE_VALID
#define PTE_ACCESSED MI_RISCV_PTE_ACCESSED
#define PTE_DIRTY MI_RISCV_PTE_DIRTY
#define PTE_PROTECT_MASK (PTE_EXECUTE_READWRITE | MI_RISCV_PTE_COPYONWRITE)

#define PFN_FROM_PTE(Pte) ((Pte)->u.Hard.PageFrameNumber)
#define PFN_FROM_PDE(Pde) ((Pde)->u.Hard.PageFrameNumber)
#define PFN_FROM_PPE(Ppe) ((Ppe)->u.Hard.PageFrameNumber)
#define MI_IS_PAGE_WRITEABLE(Pte) ((Pte)->u.Hard.Write != 0)
#define MI_IS_PAGE_COPY_ON_WRITE(Pte) ((Pte)->u.Hard.CopyOnWrite != 0)
#define MI_IS_PAGE_EXECUTABLE(Pte) ((Pte)->u.Hard.Execute != 0)
#define MI_IS_PAGE_DIRTY(Pte) ((Pte)->u.Hard.Dirty != 0)
/* Only meaningful for PDE/PPE entries: a leaf there is a large page. The
 * adopted hierarchy never contains one (ABI-125), so this is always false
 * for kernel tables; it stays correct if a future decision adds them. */
#define MI_IS_PAGE_LARGE(Pte) ((Pte)->u.Hard.Valid && ((Pte)->u.Hard.Read || (Pte)->u.Hard.Execute))

/* Explicit unavailable operations, not invented PCD/PWT/PAT encodings. */
#define MI_PAGE_DISABLE_CACHE(Pte) MiRiscvUnsupportedPteCacheOperation((Pte), 1)
#define MI_PAGE_WRITE_THROUGH(Pte) MiRiscvUnsupportedPteCacheOperation((Pte), 2)
#define MI_PAGE_WRITE_COMBINED(Pte) MiRiscvUnsupportedPteCacheOperation((Pte), 3)

/* These operations preserve the other bits in the full eight-byte entry. */
#define MI_MAKE_DIRTY_PAGE(Pte) __atomic_fetch_or(&(Pte)->u.Long, MI_RISCV_PTE_DIRTY, __ATOMIC_SEQ_CST)
#define MI_MAKE_CLEAN_PAGE(Pte) __atomic_fetch_and(&(Pte)->u.Long, ~MI_RISCV_PTE_DIRTY, __ATOMIC_SEQ_CST)
#define MI_MAKE_ACCESSED_PAGE(Pte) __atomic_fetch_or(&(Pte)->u.Long, MI_RISCV_PTE_ACCESSED, __ATOMIC_SEQ_CST)
#define MI_MAKE_OWNER_PAGE(Pte) __atomic_fetch_or(&(Pte)->u.Long, MI_RISCV_PTE_OWNER, __ATOMIC_SEQ_CST)
#define MI_MAKE_WRITE_PAGE(Pte) __atomic_fetch_or(&(Pte)->u.Long, PTE_READWRITE | MI_RISCV_PTE_DIRTY, __ATOMIC_SEQ_CST)

/* There are no large pages; nothing is "physically" mapped in the x86 sense. */
#define MI_HAS_ARCH_IS_PHYSICAL_ADDRESS
#define MI_IS_PHYSICAL_ADDRESS(Address) ((VOID)(Address), FALSE)

/* The write hooks in miarm.h call this after every entry write. */
#define MI_ARCH_SYNC_PTE_WRITE(PointerPte) MiRiscvSyncWindowEntry((PMMPTE)(PointerPte))

extern PMMPTE MiHighestUserPte;
extern PMMPDE MiHighestUserPde;
extern PMMPPE MiHighestUserPpe;
extern MMPTE ValidKernelPte;
extern MMPDE ValidKernelPde;
extern MMPPE ValidKernelPpe;
extern const ULONG_PTR MmProtectToPteMask[32];

/* Address <-> entry conversions (x64 self-map arithmetic, 39-bit VA). */
FORCEINLINE
PMMPTE
_MiAddressToPte(PVOID Address)
{
    ULONG64 Offset = (((ULONG64)Address & MI_RISCV_VA_MASK) >> PTI_SHIFT) << 3;
    return (PMMPTE)(PTE_BASE + Offset);
}
#define MiAddressToPte(x) _MiAddressToPte((PVOID)(x))

FORCEINLINE
PMMPDE
_MiAddressToPde(PVOID Address)
{
    ULONG64 Offset = (((ULONG64)Address & MI_RISCV_VA_MASK) >> PDI_SHIFT) << 3;
    return (PMMPDE)(PDE_BASE + Offset);
}
#define MiAddressToPde(x) _MiAddressToPde((PVOID)(x))

FORCEINLINE
PMMPPE
MiAddressToPpe(PVOID Address)
{
    ULONG64 Offset = (((ULONG64)Address & MI_RISCV_VA_MASK) >> PPI_SHIFT) << 3;
    return (PMMPPE)(PPE_BASE + Offset);
}

FORCEINLINE ULONG MiAddressToPti(PVOID Address) { return (((ULONG64)Address) >> PTI_SHIFT) & PTI_MASK; }
FORCEINLINE ULONG MiAddressToPdi(PVOID Address) { return (((ULONG64)Address) >> PDI_SHIFT) & PDI_MASK; }
FORCEINLINE ULONG MiAddressToPpi(PVOID Address) { return (((ULONG64)Address) >> PPI_SHIFT) & PPI_MASK; }
#define MiAddressToPteOffset(x) MiAddressToPti(x)
#define MiAddressToPdeOffset(x) MiAddressToPdi(x)
#define MiGetPdeOffset(x) MiAddressToPdi(x)

/* Unlike the recursive-map layouts, Sv39 hyperspace is below and disjoint
 * from the page-table window. Do not classify the gap as page-table space. */
#define MI_IS_PAGE_TABLE_ADDRESS(Address) \
    (((ULONG_PTR)(Address) >= PTE_BASE) && ((ULONG_PTR)(Address) <= PTE_TOP))
#define MI_IS_SYSTEM_PAGE_TABLE_ADDRESS(Address) \
    (((ULONG_PTR)(Address) >= (ULONG_PTR)MiAddressToPte(MmSystemRangeStart)) && ((ULONG_PTR)(Address) <= PTE_TOP))
#define MI_IS_PAGE_TABLE_OR_HYPER_ADDRESS(Address) \
    (MI_IS_PAGE_TABLE_ADDRESS(Address) || \
     (((ULONG_PTR)(Address) >= HYPER_SPACE) && ((ULONG_PTR)(Address) <= (ULONG_PTR)MmHyperSpaceEnd)))

FORCEINLINE PVOID MiPteToAddress(PMMPTE PointerPte) { return (PVOID)(((LONG64)PointerPte << 34) >> 25); }
FORCEINLINE PVOID MiPdeToAddress(PMMPDE PointerPde) { return (PVOID)(((LONG64)PointerPde << 43) >> 25); }
FORCEINLINE PVOID MiPpeToAddress(PMMPPE PointerPpe) { return (PVOID)(((LONG64)PointerPpe << 52) >> 25); }
FORCEINLINE PMMPTE MiPdeToPte(PMMPDE PointerPde) { return (PMMPTE)MiPteToAddress(PointerPde); }
FORCEINLINE PMMPTE MiPpeToPte(PMMPPE PointerPpe) { return (PMMPTE)MiPdeToAddress(PointerPpe); }
FORCEINLINE PMMPDE MiPteToPde(PMMPTE PointerPte) { return (PMMPDE)MiAddressToPte(PointerPte); }
FORCEINLINE PMMPPE MiPteToPpe(PMMPTE PointerPte) { return (PMMPPE)MiAddressToPde(PointerPte); }
FORCEINLINE PMMPPE MiPdeToPpe(PMMPDE PointerPde) { return (PMMPPE)MiAddressToPte(PointerPde); }

#define MiIsPteOnPdeBoundary(PointerPte) ((((ULONG_PTR)(PointerPte)) & (PAGE_SIZE - 1)) == 0)
#define MiIsPteOnPpeBoundary(PointerPte) ((((ULONG_PTR)(PointerPte)) & (PDE_PER_PAGE * PAGE_SIZE - 1)) == 0)

FORCEINLINE BOOLEAN MiIsUserPte(PVOID Address) { return (Address >= (PVOID)PTE_BASE) && (Address <= (PVOID)MiHighestUserPte); }
FORCEINLINE BOOLEAN MiIsUserPde(PVOID Address) { return (Address >= (PVOID)PDE_BASE) && (Address <= (PVOID)MiHighestUserPde); }
FORCEINLINE BOOLEAN MiIsUserPpe(PVOID Address) { return (Address >= (PVOID)PPE_BASE) && (Address <= (PVOID)MiHighestUserPpe); }

/* An entry address inside the PDE window (which contains the PPE page). */
FORCEINLINE
BOOLEAN
MiRiscvIsTableEntryAddress(PVOID Address)
{
    return ((ULONG_PTR)Address >= PDE_BASE) && ((ULONG_PTR)Address <= PDE_TOP);
}

FORCEINLINE
BOOLEAN
MiIsPdeForAddressValid(PVOID Address)
{
    return (MiAddressToPpe(Address)->u.Hard.Valid) && (MiAddressToPde(Address)->u.Hard.Valid);
}

FORCEINLINE
BOOLEAN
MiRiscvIsCanonicalAddress(_In_ ULONG_PTR Address)
{
    return ((ULONG_PTR)((LONG64)(Address << 25) >> 25) == Address);
}

/* Leaf template bits: A always, D for writable, U below the user limit,
 * G for supervisor mappings. Non-leaf targets get a bare table pointer. */
FORCEINLINE
ULONG_PTR
MiDetermineUserGlobalPteMask(IN PVOID PointerPte)
{
    ULONG_PTR Mask = MI_RISCV_PTE_VALID | MI_RISCV_PTE_ACCESSED;

    /* Like amd64, no global bit yet: one ASID, full flush on root switch. */
    if (MiIsUserPpe(PointerPte) || MiIsUserPde(PointerPte) || MiIsUserPte(PointerPte))
        Mask |= MI_RISCV_PTE_OWNER;
    return Mask;
}

FORCEINLINE
VOID
MI_MAKE_HARDWARE_PTE(IN PMMPTE NewPte, IN PMMPTE MappingPte, IN ULONG_PTR ProtectionMask, IN PFN_NUMBER PageFrameNumber)
{
    /* Callers validate ProtectionMask (miarm.h is included after this file). */
    if (MiRiscvIsTableEntryAddress(MappingPte))
    {
        /* A page-table page: pointer entry, no leaf/A/D/U bits. */
        NewPte->u.Long = MI_RISCV_PTE_VALID | ((ULONG64)PageFrameNumber << MI_RISCV_PTE_PFN_SHIFT);
        return;
    }
    NewPte->u.Long = MiDetermineUserGlobalPteMask(MappingPte) | MmProtectToPteMask[ProtectionMask];
    if (NewPte->u.Long & MI_RISCV_PTE_WRITE)
        NewPte->u.Long |= MI_RISCV_PTE_DIRTY;
    NewPte->u.Hard.PageFrameNumber = PageFrameNumber;
}

FORCEINLINE
VOID
MI_MAKE_HARDWARE_PTE_KERNEL(IN PMMPTE NewPte, IN PMMPTE MappingPte, IN ULONG_PTR ProtectionMask, IN PFN_NUMBER PageFrameNumber)
{
    ASSERT(MappingPte > MiHighestUserPte);
    MI_MAKE_HARDWARE_PTE(NewPte, MappingPte, ProtectionMask, PageFrameNumber);
}

FORCEINLINE
VOID
MI_MAKE_HARDWARE_PTE_USER(IN PMMPTE NewPte, IN PMMPTE MappingPte, IN ULONG_PTR ProtectionMask, IN PFN_NUMBER PageFrameNumber)
{
    ASSERT(MappingPte <= MiHighestUserPte);
    MI_MAKE_HARDWARE_PTE(NewPte, MappingPte, ProtectionMask, PageFrameNumber);
}

FORCEINLINE
PMMPTE
MiProtoPteToPte(_In_ PMMPTE Pte)
{
    return (PMMPTE)(LONG64)Pte->u.Proto.ProtoAddress;
}

FORCEINLINE
PVOID
MiSubsectionPteToSubsection(_In_ PMMPTE Pte)
{
    return (PVOID)(LONG64)Pte->u.Subsect.SubsectionAddress;
}

FORCEINLINE
VOID
MI_MAKE_PROTOTYPE_PTE(_Out_ PMMPTE NewPte, _In_ PMMPTE PointerPte)
{
    ASSERT(MiRiscvIsCanonicalAddress((ULONG_PTR)PointerPte));
    ASSERT(((ULONG_PTR)PointerPte & (sizeof(MMPTE) - 1)) == 0);
    NewPte->u.Long = ((ULONG64)PointerPte << 16) | MI_RISCV_PTE_PROTOTYPE;
    ASSERT(NewPte->u.Soft.PageFileHigh != MI_RISCV_PTE_LOOKUP_NEEDED);
    ASSERT(MiProtoPteToPte(NewPte) == PointerPte);
}

FORCEINLINE
VOID
MI_MAKE_SUBSECTION_PTE(_Out_ PMMPTE NewPte, _In_ PVOID Subsection)
{
    ASSERT(MiRiscvIsCanonicalAddress((ULONG_PTR)Subsection));
    NewPte->u.Long = ((ULONG64)Subsection << 16) | MI_RISCV_PTE_PROTOTYPE;
    ASSERT(NewPte->u.Soft.PageFileHigh != MI_RISCV_PTE_LOOKUP_NEEDED);
    ASSERT(MiSubsectionPteToSubsection(NewPte) == Subsection);
}

FORCEINLINE
BOOLEAN
MI_IS_MAPPED_PTE(_In_ PMMPTE Pte)
{
    return Pte->u.Hard.Valid || Pte->u.Soft.Prototype ||
           Pte->u.Soft.Transition || (Pte->u.Soft.PageFileHigh != 0);
}

/* Direct map: every RAM page and every early device window has a kernel
 * alias at DirectMapBase + physical (ABI-110/ABI-126). */
#define MI_RISCV_DIRECT_MAP_BASE RISCV64_LOADER_DIRECT_MAP_BASE
#define MI_RISCV_DIRECT_MAP_SIZE RISCV64_LOADER_DIRECT_MAP_SIZE
FORCEINLINE PVOID MiRiscvPfnToDirectMap(PFN_NUMBER Pfn) { return (PVOID)(MI_RISCV_DIRECT_MAP_BASE + ((ULONG64)Pfn << PAGE_SHIFT)); }
FORCEINLINE PMMPTE MiRiscvTablePage(PFN_NUMBER Pfn) { return (PMMPTE)MiRiscvPfnToDirectMap(Pfn); }
FORCEINLINE BOOLEAN MiRiscvIsTablePointer(MMPTE Entry) { return (Entry.u.Long & (MI_RISCV_PTE_VALID | MI_RISCV_PTE_LEAF_MASK)) == MI_RISCV_PTE_VALID; }
FORCEINLINE ULONG64 MiRiscvTablePointer(PFN_NUMBER Pfn) { return MI_RISCV_PTE_VALID | ((ULONG64)Pfn << MI_RISCV_PTE_PFN_SHIFT); }
FORCEINLINE ULONG64 MiRiscvMirrorLeaf(PFN_NUMBER Pfn, BOOLEAN Global)
{
    return MI_RISCV_PTE_VALID | MI_RISCV_PTE_READ | MI_RISCV_PTE_WRITE | MI_RISCV_PTE_ACCESSED | MI_RISCV_PTE_DIRTY |
           (Global ? MI_RISCV_PTE_GLOBAL : 0) | ((ULONG64)Pfn << MI_RISCV_PTE_PFN_SHIFT);
}

FORCEINLINE
PFN_NUMBER
MiRiscvCurrentRootPfn(VOID)
{
    ULONG_PTR Satp;
    __asm__ __volatile__("csrr %0, satp" : "=r"(Satp) :: "memory");
    return (PFN_NUMBER)(Satp & RISCV64_LOADER_SATP_PPN_MASK);
}

FORCEINLINE
VOID
MiRiscvFlushWindowVa(PVOID Address)
{
    __asm__ __volatile__("sfence.vma %0, zero" :: "r"(Address) : "memory");
}

/* The mapping callback must return the actual, page-aligned table page. */
typedef PMMPTE (NTAPI *PMI_RISCV_MAP_TABLE_PAGE)(
    _In_ PFN_NUMBER PageFrameNumber,
    _In_opt_ PVOID Context);

typedef struct _MI_RISCV_PAGE_WALK
{
    PMMPTE Entry;
    MMPTE Value;
    PFN_NUMBER TablePageFrame;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Level;
    BOOLEAN Global;
} MI_RISCV_PAGE_WALK, *PMI_RISCV_PAGE_WALK;

DECLSPEC_NORETURN VOID NTAPI MiRiscvUnsupportedPteCacheOperation(_In_ PMMPTE Pte, _In_ ULONG Operation);
BOOLEAN NTAPI MiRiscvIsCachedMdl(_In_ PMDL Mdl);
BOOLEAN NTAPI MiRiscvValidateIoMapping(_In_ PHYSICAL_ADDRESS Address, _In_ SIZE_T Length, _In_ MEMORY_CACHING_TYPE CacheType);
NTSTATUS NTAPI MiRiscvInitializeProcessPageTables(_Inout_ PEPROCESS Process);
VOID NTAPI MiRiscvDeleteProcessPageTables(_Inout_ PEPROCESS Process);
BOOLEAN NTAPI MiRiscvSystemVaLayoutAssigned(VOID);
VOID NTAPI MiRiscvReserveSystemMemoryAreas(VOID);
VOID NTAPI MiRiscvDumpSystemVaLayout(VOID);

/* Window mirror maintenance (mm/riscv64/window.c). */
VOID NTAPI MiRiscvSyncWindowEntrySlow(_In_ PMMPTE Entry);
VOID NTAPI MiRiscvBuildBootWindow(VOID);
BOOLEAN NTAPI MiRiscvWindowConsistent(_In_ PFN_NUMBER RootPfn);
PFN_NUMBER NTAPI MiRiscvAllocateTablePage(VOID);
extern BOOLEAN MiRiscvWindowReady;
extern BOOLEAN MiPfnsInitialized;

FORCEINLINE
VOID
MiRiscvSyncWindowEntry(_In_ PMMPTE Entry)
{
    if (MiRiscvIsTableEntryAddress(Entry))
        MiRiscvSyncWindowEntrySlow(Entry);
}

FORCEINLINE
VOID
MI_WRITE_VALID_PPE(_Inout_ PMMPPE PointerPpe, _In_ MMPPE Value)
{
    ASSERT(PointerPpe->u.Hard.Valid == 0);
    ASSERT(MiRiscvIsTablePointer(Value));
    *PointerPpe = Value;
    MiRiscvSyncWindowEntry((PMMPTE)PointerPpe);
}

NTSTATUS
NTAPI
MiRiscvWalkPageTables(
    _In_ PFN_NUMBER RootPageFrame,
    _In_ ULONG_PTR VirtualAddress,
    _In_ PFN_NUMBER MaximumPageFrame,
    _In_ PMI_RISCV_MAP_TABLE_PAGE MapTablePage,
    _In_opt_ PVOID MappingContext,
    _Out_ PMI_RISCV_PAGE_WALK Result);

NTSTATUS NTAPI MiRiscvInitializePageTableAccess(_In_ PMI_RISCV_MAP_TABLE_PAGE MapTablePage, _In_opt_ PVOID MappingContext, _In_ PFN_NUMBER MaximumPageFrame);
VOID NTAPI MiRiscvAdoptPageTableAccess(VOID);
NTSTATUS NTAPI MiRiscvWalkCurrentPageTables(_In_ PVOID Address, _Out_ PMI_RISCV_PAGE_WALK Result);

BOOLEAN NTAPI MiRiscvEncodeSwapPte(_In_ ULONG_PTR SwapEntry, _In_ ULONG Protection, _Out_ PMMPTE Pte);
BOOLEAN NTAPI MiRiscvDecodeSwapPte(_In_ MMPTE Pte, _Out_ PULONG_PTR SwapEntry);

#ifdef __cplusplus
}
#endif
