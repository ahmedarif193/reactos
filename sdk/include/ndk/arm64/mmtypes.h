
#ifndef _ARM64_MMTYPES_H
#define _ARM64_MMTYPES_H

#ifdef __cplusplus
extern "C" {
#endif

//
// Page-related Macros
//
#ifndef PAGE_SIZE
#define PAGE_SIZE                         0x1000
#endif
#define PAGE_SHIFT                        12L
#define MM_ALLOCATION_GRANULARITY         0x10000
#define MM_ALLOCATION_GRANULARITY_SHIFT   16L
#define MM_PAGE_FRAME_NUMBER_SIZE         20

//
// User space range limit (ARM64 uses 48-bit virtual addresses for user space)
//
#define MI_HIGHEST_USER_ADDRESS         (PVOID)0x0000FFFFFFFFFFFFULL

/* Following structs are based on WoA symbols */
typedef struct _HARDWARE_PTE
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    union {
        ULONG64 NotLargePage:1;
        ULONG64 LargePage:1;  /* Inverted logic */
    };
    union {
        ULONG64 CacheType:2;
        struct {
            ULONG64 CacheDisable:1;
            ULONG64 WriteThrough:1;
        };
    };
    ULONG64 OsAvailable2:1;
    ULONG64 NonSecure:1;
    ULONG64 Owner:1;
    union {
        ULONG64 NotDirty:1;
        ULONG64 Dirty:1;  /* Alias for compatibility - inverted logic */
    };
    ULONG64 Shareability:2;
    ULONG64 Accessed:1;
    union {
        ULONG64 NonGlobal:1;
        ULONG64 Global:1;  /* Inverted logic for compatibility */
    };
    ULONG64 PageFrameNumber:36;
    ULONG64 RsvdZ1:4;
    ULONG64 ContigousBit:1;
    ULONG64 PrivilegedNoExecute:1;
    ULONG64 UserNoExecute:1;
    union {
        ULONG64 Writable:1;
        ULONG64 Write:1;
    };
    ULONG64 CopyOnWrite:1;
    ULONG64 OsAvailable:2;
    ULONG64 PxnTable:1;
    ULONG64 UxnTable:1;
    ULONG64 ApTable:2;
    ULONG64 NsTable:1;
} HARDWARE_PTE, *PHARDWARE_PTE;

typedef struct _MMPTE_SOFTWARE
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 PageFileLow:4;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 PageFileReserved:1;
    ULONG64 PageFileAllocated:1;
    ULONG64 UsedPageTableEntries:10;
    ULONG64 ColdPage:1;
    ULONG64 OnStandbyLookaside:1;
    ULONG64 RsvdZ1:6;
    ULONG64 PageFileHigh:32;
} MMPTE_SOFTWARE;

typedef struct _MMPTE_TRANSITION
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 Spare:2;
    ULONG64 OnStandbyLookaside:1;
    ULONG64 IoTracker:1;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 PageFrameNumber:40;
    ULONG64 RsvdZ1:12;
} MMPTE_TRANSITION;

typedef struct _MMPTE_PROTOTYPE
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 HiberVerifyConverted:1;
    ULONG64 Unused1:1;
    ULONG64 ReadOnly:1;
    ULONG64 Combined:1;
    ULONG64 Prototype:1;
    ULONG64 DemandFillProto:1;
    ULONG64 RsvdZ1:4;
    ULONG64 ProtoAddress:48;
} MMPTE_PROTOTYPE;

typedef struct _MMPTE_SUBSECTION
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 OnStandbyLookaside:1;
    ULONG64 WhichPool:2;
    ULONG64 RsvdZ1:1;
    ULONG64 Prototype:1;
    ULONG64 ColdPage:1;
    ULONG64 RsvdZ2:4;
    union {
        ULONG64 SubsectionAddress:48;
        struct {
            ULONG64 SubsectionAddressLow:32;
            ULONG64 SubsectionAddressHigh:16;
        };
    };
} MMPTE_SUBSECTION;

typedef struct _MMPTE_TIMESTAMP
{
    /* 8 Byte struct */
    ULONG64 MustBeZero:1;
    ULONG64 Protection:5;
    ULONG64 PageFileLow:4;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 RsvdZ1:20;
    ULONG64 GlobalTimeStamp:32;
} MMPTE_TIMESTAMP;

typedef struct _MMPTE_LIST
{
    /* 8 Byte struct */
    ULONG64 Valid:1;
    ULONG64 Protection:5;
    ULONG64 OneEntry:1;
    ULONG64 RsvdZ1:3;
    ULONG64 Prototype:1;
    ULONG64 Transition:1;
    ULONG64 RsvdZ2:16;
    ULONG64 NextEntry:36;
} MMPTE_LIST;

typedef struct _MMPTE
{
    union
    {
        ULONG_PTR Long;
        HARDWARE_PTE Flush;
        HARDWARE_PTE Hard;
        MMPTE_PROTOTYPE Proto;
        MMPTE_SOFTWARE Soft;
        MMPTE_TRANSITION Trans;
        MMPTE_SUBSECTION Subsect;
        MMPTE_LIST List;
    } u;
} MMPTE, *PMMPTE;

/* ARM64 4-Level Page Table Definitions */
#define PML4_BASE   0xFFFFF6FB7DBED000ULL
#define PML4_TOP    0xFFFFF6FB7DBEDFFFULL

#define PDP_BASE    0xFFFFF6FB7DA00000ULL
#define PDP_TOP     0xFFFFF6FB7DBFFFFFULL

#define PD_BASE     0xFFFFF6FB40000000ULL
#define PD_TOP      0xFFFFF6FB7FFFFFFFULL

#define PT_BASE     0xFFFFF68000000000ULL
#define PT_TOP      0xFFFFF6FFFFFFFFFFULL

/* ARM64 Page Table Entry Types */
typedef MMPTE MMPML4E;
typedef PMMPTE PMMPML4E;
typedef MMPTE MMPDPTE;
typedef PMMPTE PMMPDPTE;
typedef MMPTE MMPDE;
typedef PMMPTE PMMPDE;

/* Page Table Index Macros */
#define MiAddressToPml4Index(x) (((ULONG_PTR)(x) >> 39) & 0x1FF)
#define MiAddressToPdpIndex(x)  (((ULONG_PTR)(x) >> 30) & 0x1FF)
#define MiAddressToPdeIndex(x)  (((ULONG_PTR)(x) >> 21) & 0x1FF)
#define MiAddressToPteIndex(x)  (((ULONG_PTR)(x) >> 12) & 0x1FF)

/* Page Table Address Macros */
#define MiAddressToPml4(x) ((PMMPML4E)(PML4_BASE + (((ULONG_PTR)(x) >> 36) & 0x1FF8)))
#define MiAddressToPdp(x)  ((PMMPDPTE)(PDP_BASE + (((ULONG_PTR)(x) >> 27) & 0x3FFFF8)))
#define MiAddressToPde(x)  ((PMMPDE)(PD_BASE + (((ULONG_PTR)(x) >> 18) & 0x7FFFFFF8)))
#define MiAddressToPte(x)  ((PMMPTE)(PT_BASE + (((ULONG_PTR)(x) >> 9) & 0xFFFFFFFFF8)))

/* ARM64 memory regions */
#ifndef KSEG0_BASE
#define KSEG0_BASE 0xFFFF800000000000ULL
#endif

/* Check if address is valid - commented out, defined as function in miarm.h */
/* #define MI_IS_PHYSICAL_ADDRESS(x) ((ULONG_PTR)(x) < KSEG0_BASE) */

/* Page frame number macros */
#define PFN_FROM_PTE(x) ((x)->u.Hard.PageFrameNumber)
#define PFN_FROM_PDE(x) ((x)->u.Hard.PageFrameNumber)
#define PFN_FROM_PPE(x) ((x)->u.Hard.PageFrameNumber)
#define PFN_FROM_PXE(x) ((x)->u.Hard.PageFrameNumber)

/* PDE to PTE conversion */
#define MiPdeToPte(x) ((PMMPTE)(PT_BASE + (((ULONG_PTR)(x) & 0xFFFFFFFFF000ULL) >> 10)))

/* PTE to Address conversion - Fixed for ARM64 */
#define MiPteToAddress(x) ((PVOID)((((ULONG_PTR)(x) - PT_BASE) << 9) & 0xFFFFFFFFF000ULL))

/* PTE to PDE conversion */
#define MiPteToPde(x) ((PMMPDE)(PD_BASE + ((((ULONG_PTR)(x) - PT_BASE) >> 12) << 3)))

/* PDE to PPE conversion */
#define MiPdeToPpe(x) ((PMMPPE)(PDP_BASE + ((((ULONG_PTR)(x) - PD_BASE) >> 12) << 3)))

/* PDE to Address conversion */
#define MiPdeToAddress(x) ((PVOID)((((ULONG_PTR)(x) - PD_BASE) << 18) & 0xFFFFFFFFF000ULL))

/* Check if PTE is on PDE boundary */
#define MiIsPteOnPdeBoundary(x) ((((ULONG_PTR)(x) & 0xFFF) == 0))

/* Check if PTE is mapped */
#define MI_IS_MAPPED_PTE(x) ((x)->u.Hard.Valid)

/* Proto PTE to PTE conversion */
#define MiProtoPteToPte(x) ((PMMPTE)((x)->u.Proto.ProtoAddress))

/* Page size for PDE */
#define PDE_MAPPED_VA (1 << 21)  /* 2MB pages */

/* Check if page is large */
#define MI_IS_PAGE_LARGE(x) ((x)->u.Hard.LargePage)

/* Page dirty/clean macros */
#define MI_IS_PAGE_DIRTY(x) (!((x)->u.Hard.NotDirty))
#define MI_MAKE_DIRTY_PAGE(x) ((x)->u.Hard.NotDirty = 0)
#define MI_MAKE_CLEAN_PAGE(x) ((x)->u.Hard.NotDirty = 1)

/* Page write through */
#define MI_PAGE_WRITE_THROUGH(x) ((x)->u.Hard.WriteThrough = 1)

/* Fault code macros */
#define MI_IS_NOT_PRESENT_FAULT(x) (!((x) & 0x1))
#define MI_IS_WRITE_ACCESS(x) ((x) & 0x2)

/* Address to PTE offset */
#define MiAddressToPteOffset(x) (((ULONG_PTR)(x) >> 12) & 0x1FF)

/* Synchronize system PDE */
#define MiSynchronizeSystemPde(x) __asm__ __volatile__("dsb ish" : : : "memory")

/* Additional definitions for compatibility */
#define PTE_BASE    PT_BASE
#define PTE_TOP     PT_TOP
#define PDE_BASE    PD_BASE
#define PDE_TOP     PD_TOP

/* Session pool definitions */
#define SESSION_POOL_LOOKASIDES 21

/* Memory macros */
#define PTE_ACCESSED    0x0000000000000400ULL
#define PTE_OWNER       0x0000000000000004ULL
#define MI_MAKE_ACCESSED_PAGE(x) ((x)->u.Hard.Accessed = 1)
#define MI_MAKE_OWNER_PAGE(x) ((x)->u.Hard.Owner = 1)
#define MI_IS_PAGE_WRITEABLE(x) ((x)->u.Hard.Write)
#define MI_IS_PAGE_COPY_ON_WRITE(x) ((x)->u.Hard.CopyOnWrite)
#define MI_PAGE_DISABLE_CACHE(x) ((x)->u.Hard.CacheDisable = 1)
#define MI_PAGE_WRITE_COMBINED(x) ((x)->u.Hard.WriteThrough = 1)

/* Shared user data address */
#define MM_SHARED_USER_DATA_VA 0x7FFE0000

/* User address space limits */
#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS    0x10000
#endif
#ifndef MM_HIGHEST_USER_ADDRESS
#define MM_HIGHEST_USER_ADDRESS   0x7FFEFFFF
#endif

/* Memory management constants */
#define MI_MIN_INIT_PAGED_POOLSIZE (32 * 1024 * 1024)
#define MI_SYSTEM_CACHE_WS_START   0xFFFFF70000000000ULL
#define MI_SECONDARY_COLORS        256
#define MI_MIN_SECONDARY_COLORS    16
#define MI_MAX_SECONDARY_COLORS    1024
#define MI_MAX_FREE_PAGE_LISTS     32
#define MI_ZERO_PTES               32
#define MI_HYPERSPACE_PTES         256
#define MM_PTE_SOFTWARE_PROTECTION_BITS 5
#define PXE_SELFMAP                0x1ED
#define KD_BREAKPOINT_SIZE         4
#define HYPER_SPACE                0xFFFFF70000000000ULL
#define HYPER_SPACE_END            0xFFFFF77FFFFFFFFFULL
#define MI_MAPPING_RANGE_END       0xFFFFF67FFFFFFFFFULL
#define MI_DEBUG_MAPPING           ((PVOID)0xFFFFF78000000000ULL)
#define MI_SYSTEM_PTE_BASE         ((PVOID)0xFFFFF80000000000ULL)

/* System PTE tuning constants */
#define MI_MIN_PAGES_FOR_SYSPTE_TUNING     ((19 * 1024 * 1024) >> PAGE_SHIFT)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST      ((32 * 1024 * 1024) >> PAGE_SHIFT)
#define MI_HIGHEST_SYSTEM_ADDRESS          ((PVOID)0xFFFFFFFFFFFFFFFFULL)
#define MI_USER_PROBE_ADDRESS              ((PVOID)0x000007FFFFFFF000ULL)
#define MI_SYSTEM_CACHE_START              ((PVOID)0xFFFF700000000000ULL)
#define MI_PAGED_POOL_START                ((PVOID)0xFFFF800000000000ULL)
#define MI_NONPAGED_POOL_END               ((PVOID)0xFFFF900000000000ULL)
#define MI_MIN_PAGES_FOR_SYSPTE_BOOST_BOOST ((256 * 1024 * 1024) >> PAGE_SHIFT)
#define MI_ALLOCATION_FRAGMENT              (64 * 1024)
#define MI_MIN_ALLOCATION_FRAGMENT         (4 * PAGE_SIZE)
#define MI_MAX_ALLOCATION_FRAGMENT         (4 * 1024 * 1024)
#define MM_EMPTY_LIST                       ((ULONG_PTR)-1)
#define MM_EMPTY_PTE_LIST                   ((ULONG_PTR)0xFFFFF)

/* System range constants */
#define MI_DEFAULT_SYSTEM_RANGE_START      0xFFFF800000000000ULL
#define MI_MAX_ZERO_BITS                   21

/* Type definitions for page table entries */
typedef MMPTE MMPPE;
typedef PMMPTE PMMPPE;

#ifdef __cplusplus
}; // extern "C"
#endif

#endif
