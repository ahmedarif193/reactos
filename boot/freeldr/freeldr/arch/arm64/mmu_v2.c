/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 MMU and memory management for ReactOS
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>
#include <debug.h>
#include <Uefi.h>

DBG_DEFAULT_CHANNEL(WARNING);

/* -------------------------------------------------------------------------- */
/* Page-table view (self-map) & core layout constants                         */
/* -------------------------------------------------------------------------- */

#define ARM64_SELF_PTE_BASE   0xFFFFE48000000000ULL
#define ARM64_SELF_PDE_BASE   0xFFFFE4F240000000ULL
#define ARM64_SELF_PPE_BASE   0xFFFFE4F279200000ULL
#define ARM64_SELF_PXE_BASE   0xFFFFE4F2793C9000ULL

#define ARM64_PFN_DB_BASE          0xFFFFFA8000000000ULL
#define ARM64_PFN_DB_RESERVE_SIZE  (512ULL << 20) /* 512MB for PFN DB + metadata */
#define ARM64_NONPAGED_RESERVE_SIZE (512ULL << 20)

#define ARM64_PAGED_POOL_BASE       0xFFFFF8A000000000ULL
#define ARM64_PAGED_POOL_INIT_BYTES (64ULL << 20)

#define ARM64_HYPERSPACE_BASE      0xFFFFF70000000000ULL
#define ARM64_HYPERSPACE_BYTES     (256ULL * PAGE_SIZE)

#define ARM64_DEBUG_MAPPING_BASE   0xFFFFF89FFFFFF000ULL

#define ARM64_ALIGN_UP(value, alignment) \
    (((value) + ((alignment) - 1ULL)) & ~((alignment) - 1ULL))
#define ARM64_ALIGN_DOWN(value, alignment) \
    ((value) & ~((alignment) - 1ULL))

#define ARM64_DEFAULT_SECONDARY_COLORS   64ULL
#define ARM64_MINIMUM_NONPAGED_POOL_SIZE (256ULL * 1024ULL)
#define ARM64_MIN_ADDITION_NONPAGED_PER_MB (32ULL * 1024ULL)
#define ARM64_MAX_INIT_NONPAGED_POOL_SIZE (128ULL * 1024ULL * 1024ULL * 1024ULL)
#define ARM64_NONPAGED_POOL_PERCENT       0U

#define ARM64_SIZEOF_MMPFN             0x58ULL /* FIXME: update if sizeof(MMPFN) changes (ntoskrnl/include/internal/mm.h) */
#define ARM64_SIZEOF_MMCOLOR_TABLES    0x18ULL

#define ARM64_DEBUG_MAPPING_BYTES  PAGE_SIZE

#define ARM64_PX_MASK         0x1FFULL
#define ARM64_PXI_SHIFT       39

/* UART output using runtime-detected address from early_uart.h */
#include <reactos/arm64/early_uart.h>

static inline VOID UartPuts(const char *String) { EarlyUartPuts(String); }

/* ARRAYSIZE fallback if not defined */
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* Presently identity, but use consistently when placing PAs into PTEs */
#ifndef VA_TO_PA
#define VA_TO_PA(x) ((UINT64)(x))
#endif

/* Presently identity too; use when turning PA from a PTE into a C pointer */
#ifndef PA_TO_VA
#define PA_TO_VA(x) ((UINT64)(x))
#endif

#ifndef ARM64_MAP_ATTR_UXN
#define ARM64_MAP_ATTR_UXN 0
#endif
#ifndef ARM64_MAP_ATTR_PXN
#define ARM64_MAP_ATTR_PXN 0
#endif

/* Block size fallbacks if arch headers don't provide them. */
#ifndef ARM64_BLOCK_SIZE_1G
#define ARM64_BLOCK_SIZE_1G            (1ULL << 30)
#define ARM64_BLOCK_MASK_1G            (ARM64_BLOCK_SIZE_1G - 1ULL)
#endif
#ifndef ARM64_BLOCK_SIZE_2M
#define ARM64_BLOCK_SIZE_2M            (1ULL << 21)
#define ARM64_BLOCK_MASK_2M            (ARM64_BLOCK_SIZE_2M - 1ULL)
#endif

extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern LIST_ENTRY FrLdrModuleList;

/*
 * Set TRUE in UefiExitBootServices() the moment EBS succeeds. Some firmwares
 * (observed with OVMF) unmap the BootServices code pages on EBS, so calling
 * bs->AllocatePages afterwards faults with a synchronous abort at the
 * firmware code address. We treat the flag as a
 * second-level gate alongside the GlobalSystemTable pointer check below.
 */
extern volatile BOOLEAN BootServicesExitedFlag;

/* ---------- Barrier & TLBI helpers (multicore-safe) ---------- */

#define ARM64_DSB_ISH()   __asm__ volatile("dsb ish" ::: "memory")
#define ARM64_DSB_ISHST() __asm__ volatile("dsb ishst" ::: "memory")

/* Global/all-ASID TLBI */
#define TLBI_VMALLE1IS()  __asm__ volatile("tlbi vmalle1is" ::: "memory")

/* Per-VA, all ASIDs (EL1&0) */
static inline void tlbi_vaae1is_by_va(ULONGLONG va)
{
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(va >> 12) : "memory");
}

static inline void tlbi_va_entry(ULONGLONG va, ULONGLONG size)
{
    (void)size;
    tlbi_vaae1is_by_va(va);
}

/* ---------- Descriptor bits ---------- */

#define PTE_TYPE_MASK           (3ULL << 0)
#define PTE_TYPE_FAULT          (0ULL << 0)
#define PTE_TYPE_TABLE          (3ULL << 0)  /* next-level pointer */
#define PTE_TYPE_PAGE           (3ULL << 0)  /* leaf at L3 */
#define PTE_TYPE_BLOCK          (1ULL << 0)  /* leaf at L1/L2 */
#define PTE_TYPE_VALID          (1ULL << 0)
#define PTE_ADDR_MASK           0x0000FFFFFFFFF000ULL

/* Block/Page attributes */
#define PTE_BLOCK_MEMTYPE(x)    ((x) << 2)
#define PTE_BLOCK_NS            (1ULL << 5)
#define PTE_BLOCK_NON_SHARE     (0ULL << 8)
#define PTE_BLOCK_OUTER_SHARE   (2ULL << 8)
#define PTE_BLOCK_INNER_SHARE   (3ULL << 8)
#define PTE_BLOCK_AF            (1ULL << 10)
#define PTE_BLOCK_NG            (1ULL << 11)
#define PTE_BLOCK_PXN           (1ULL << 53)
#define PTE_BLOCK_UXN           (1ULL << 54)
#define PTE_BLOCK_RO            (1ULL << 7)
#define PTE_TABLE_NT_FLAGS      (PTE_BLOCK_NG | PTE_BLOCK_PXN | PTE_BLOCK_UXN)
#define PTE_BLOCK_ADDR_MASK_1G  (PTE_ADDR_MASK & ~ARM64_BLOCK_MASK_1G)
#define PTE_BLOCK_ADDR_MASK_2M  (PTE_ADDR_MASK & ~ARM64_BLOCK_MASK_2M)

/*
 * Windows/NT self-map compatibility: every upper-level table descriptor is
 * visible through PXE/PPE/PDE aliases. Preserve the NT-observed ignored table
 * policy bits while keeping address extraction strictly masked by PTE_ADDR_MASK.
 */
#define PTE_TABLE_ATTRS         (PTE_TYPE_VALID | PTE_TYPE_TABLE | \
                                 PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) | \
                                 PTE_BLOCK_INNER_SHARE | \
                                 PTE_BLOCK_AF | \
                                 PTE_TABLE_NT_FLAGS)
#define PTE_SELFREF_ATTRS       PTE_TABLE_ATTRS

typedef char arm64_selfmap_index_must_match_nt[
    (((ARM64_SELF_PXE_BASE >> ARM64_PXI_SHIFT) & ARM64_PX_MASK) == 457ULL) ? 1 : -1];
typedef char arm64_table_attrs_must_match_nt[
    (PTE_TABLE_ATTRS == 0x0060000000000F13ULL) ? 1 : -1];

#define PTE_BLOCK_MEMTYPE_MASK  (7ULL << 2)

static inline UINT64
sanitize_block_attrs(UINT64 attrs)
{
    UINT64 sanitized = attrs;

    if ((sanitized & PTE_BLOCK_MEMTYPE_MASK) == 0)
        sanitized |= PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB);

    if ((sanitized & PTE_BLOCK_AF) == 0)
        sanitized |= PTE_BLOCK_AF;

    if ((sanitized & (3ULL << 8)) == 0)
        sanitized |= PTE_BLOCK_INNER_SHARE;

    return sanitized;
}

/* Descriptor classification helpers */
#define DESC_VALID(e)     (((e) & PTE_TYPE_VALID) != 0)
#define DESC_TYPE(e)      ((e) & PTE_TYPE_MASK)
/* At L0/L1/L2, type==3 is a table descriptor. AF may be set for self-map ABI. */
#define DESC_IS_TABLE(e) \
    (DESC_VALID(e) && (DESC_TYPE(e) == PTE_TYPE_TABLE))
/* Leaf (block or page) */
#define DESC_IS_BLOCK(e)  (DESC_VALID(e) && (DESC_TYPE(e) == PTE_TYPE_BLOCK))
#define DESC_IS_PAGE(e) \
    (DESC_VALID(e) && (DESC_TYPE(e) == PTE_TYPE_PAGE) && ((e) & PTE_BLOCK_AF))
#define DESC_IS_LEAF(e)   (DESC_IS_BLOCK(e) || DESC_IS_PAGE(e))

/* ---------- MAIR / memory types ---------- */

#define MEMORY_ATTRIBUTES       ((0xFFULL << (0U * 8)) |                            \
                                (0x00ULL << (ARM64_MEM_ATTR_DEVICE_nGnRnE * 8)) |   \
                                (0x44ULL << (ARM64_MEM_ATTR_NORMAL_NC * 8)) |       \
                                (0x44ULL << (ARM64_MEM_ATTR_NORMAL_WC * 8)) |       \
                                (0xFFULL << (ARM64_MEM_ATTR_NORMAL_WB * 8)) |       \
                                (0x00ULL << (5U * 8)) |                            \
                                (0x44ULL << (6U * 8)) |                            \
                                (0x44ULL << (7U * 8)))
typedef char arm64_mair_must_match_nt[
    (MEMORY_ATTRIBUTES == 0x444400FF444400FFULL) ? 1 : -1];

/* ---------- TCR helpers ---------- */
#define TCR_T0SZ(x)             ((64ULL - (x)) << 0)
#define TCR_IRGN_WBWA           (1ULL << 8)
#define TCR_ORGN_WBWA           (1ULL << 10)
#define TCR_SHARED_INNER        (3ULL << 12)
#define TCR_TG0_4K              (0ULL << 14)
#define TCR_T1SZ(x)             ((64ULL - (x)) << 16)
#define TCR_IRGN1_WBWA          (1ULL << 24)
#define TCR_ORGN1_WBWA          (1ULL << 26)
#define TCR_SHARED1_INNER       (3ULL << 28)
#define TCR_TG1_4K              (2ULL << 30)
#define TCR_ASID16              (1ULL << 36)
#define TCR_TBI0                (1ULL << 37)
#define TCR_TBI1                (1ULL << 38)
#define TCR_TBID0               (1ULL << 51)
#define TCR_TBID1               (1ULL << 52)
#define TCR_A1                  (1ULL << 22)
#define TCR_EPD0                (1ULL << 7)
#define TCR_EPD1                (1ULL << 23)

#define TCR_EL1_RSVD            0ULL
#define TCR_EL2_RSVD            0ULL

/* From entry.S; keeps execution at EL1 like Windows boot loaders */
VOID Arm64EnsureEl1EntryState(VOID);

/* SCTLR_EL1 bits */
#define SCTLR_EL1_M             (1ULL << 0)
#define SCTLR_EL1_A             (1ULL << 1)
#define SCTLR_EL1_C             (1ULL << 2)
#define SCTLR_EL1_SA            (1ULL << 3)
#define SCTLR_EL1_I             (1ULL << 12)

/* UEFI memory management integration */
extern FREELDR_MEMORY_DESCRIPTOR* UefiMemGetMemoryMap(PULONG MaxMemoryMapSize);
static BOOLEAN page_tables_initialized = FALSE;

#define ARM64_KSEG0_L0_INDEX      (((ULONGLONG)ARM64_KSEG0_BASE >> 39) & 0x1FFULL)
#define ARM64_SYSTEM_L0_INDEX     (((ULONGLONG)ARM64_SYSTEM_RANGE_BASE >> 39) & 0x1FFULL)
#define ARM64_PHYS_MAP_L0_INDEX   (((ULONGLONG)ARM64_PHYS_MAP_BASE >> 39) & 0x1FFULL)
#define ARM64_KERNEL_L1_TABLES    4U
/* 8 L1 tables to cover 4TB user address range for high physical addresses */
#define ARM64_USER_L1_TABLES      8U

/* Page table bookkeeping */
#define ARM64_PT_ENTRIES           512U
#define ARM64_PT_BYTES             (ARM64_PT_ENTRIES * sizeof(UINT64))
#define ARM64_TTBR1_L0_OFFSET      ((ARM64_PT_ENTRIES / 2U) * sizeof(UINT64))
#define ARM64_L2_TABLES_PER_L1     2U
/*
 * L3 table pool sizing:
 * Keep only a compact seed pool inside the EFI image. The mapper spills to
 * AllocatePages(), or the post-EBS static arena, when a slot needs more tables.
 * This keeps uefildr's PE SizeOfImage near the real loader size instead of
 * reserving hundreds of MiB of BSS for worst-case page-table coverage.
 */
#define ARM64_L3_TABLES_PER_L2     8U
#define ARM64_L3_TABLES_PER_L0     (ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2)  /* Total L3 tables per L0 slot */

/*
 * Extra kernel L0 slots for special regions (outside KSEG0).
 * These need pre-allocated page tables since they are mapped after ExitBootServices.
 *
 * L0 indices for special kernel regions:
 *   - 457 (0x1C9): Self-map (ARM64_SELF_PXE_BASE)
 *   - 494 (0x1EE): Hyperspace (ARM64_HYPERSPACE_BASE)
 *   - 497 (0x1F1): Paged Pool / Debug mapping
 *   - 501 (0x1F5): PFN Database
 */
#define ARM64_EXTRA_L0_SLOT_SELFMAP    457U  /* 0x1C9 - Self-map region */
#define ARM64_EXTRA_L0_SLOT_HYPERSPACE 494U  /* 0x1EE - Hyperspace */
#define ARM64_EXTRA_L0_SLOT_PAGEDPOOL  497U  /* 0x1F1 - Paged pool / Debug */
#define ARM64_EXTRA_L0_SLOT_PFNDB      501U  /* 0x1F5 - PFN Database */

#define ARM64_EXTRA_KERNEL_SLOTS       4U    /* Number of extra kernel L0 slots */
#define ARM64_EXTRA_L2_PER_SLOT        1U
#define ARM64_EXTRA_L3_PER_L2          32U
#define ARM64_EXTRA_L3_PER_SLOT        (ARM64_EXTRA_L2_PER_SLOT * ARM64_EXTRA_L3_PER_L2)

/* Page tables allocated from low memory so VA==PA during boot */
static UINT64                *arm64_l0_page_table;
static UINT64 (*arm64_l1_page_tables)[ARM64_PT_ENTRIES];
static UINT64                *arm64_kernel_l0_table;
static UINT64 (*arm64_kernel_l1_tables)[ARM64_PT_ENTRIES];

static UINT64 (*arm64_kernel_l2_tables)[ARM64_L2_TABLES_PER_L1][ARM64_PT_ENTRIES];
static UINT64 (*arm64_kernel_l3_tables)
    [ARM64_L2_TABLES_PER_L1]
    [ARM64_L3_TABLES_PER_L2]
    [ARM64_PT_ENTRIES];
static UINT64 (*arm64_user_l2_tables)[ARM64_L2_TABLES_PER_L1][ARM64_PT_ENTRIES];
static UINT64 (*arm64_user_l3_tables)
    [ARM64_L2_TABLES_PER_L1]
    [ARM64_L3_TABLES_PER_L2]
    [ARM64_PT_ENTRIES];
static UINT64 arm64_l2_next_index[ARM64_KERNEL_L1_TABLES] = {0};
/*
 * L3 pool tracking: use FLAT per-L0-slot index instead of per-L2-slot.
 * This avoids exhaustion when memory descriptors cause uneven L2 slot usage.
 * The L3 tables are stored in a 3D array [l0_slot][l2_slot][index], but
 * we allocate linearly across all L2 slots within an L0 slot.
 */
static UINT64 arm64_l3_next_index[ARM64_KERNEL_L1_TABLES] = {0};  /* FLAT: per-L0 slot, not per-L2 slot */
static UINT64 arm64_user_l2_next_index[ARM64_USER_L1_TABLES] = {0};
static UINT64 arm64_user_l3_next_index[ARM64_USER_L1_TABLES] = {0};  /* FLAT: per-L0 slot */

/*
 * Extra kernel L0 slots for special regions (Hyperspace, Paged Pool, PFN DB, etc.)
 * These are pre-allocated to avoid needing Boot Services during kernel mapping.
 */
static const UINT64 arm64_extra_kernel_l0_slots[ARM64_EXTRA_KERNEL_SLOTS] = {
    ARM64_EXTRA_L0_SLOT_SELFMAP,    /* 457 */
    ARM64_EXTRA_L0_SLOT_HYPERSPACE, /* 494 */
    ARM64_EXTRA_L0_SLOT_PAGEDPOOL,  /* 497 */
    ARM64_EXTRA_L0_SLOT_PFNDB       /* 501 */
};

/* Page tables for extra kernel slots - pointers set during allocation */
static UINT64 *arm64_extra_l1_tables[ARM64_EXTRA_KERNEL_SLOTS];
static UINT64 (*arm64_extra_l2_tables)[ARM64_EXTRA_L2_PER_SLOT][ARM64_PT_ENTRIES];
static UINT64 (*arm64_extra_l3_tables)[ARM64_EXTRA_L2_PER_SLOT][ARM64_EXTRA_L3_PER_L2][ARM64_PT_ENTRIES];

/* Tracking indices for extra kernel slots - FLAT allocation like main kernel pool */
static UINT64 arm64_extra_l2_next_index[ARM64_EXTRA_KERNEL_SLOTS] = {0};
static UINT64 arm64_extra_l3_next_index[ARM64_EXTRA_KERNEL_SLOTS] = {0};  /* FLAT: per-extra-slot, not per-L2 slot */

static BOOLEAN mmu_enabled = FALSE;
static BOOLEAN page_table_memory_allocated = FALSE;

/* Cached physical limit so TCR composition needs no UEFI calls post-EBS */
static UINT64 cached_max_physical_address = 0x100000000ULL;

/* EL1/EL2-with-E2H register aliases */
#define SCTLR_EL12_SYSREG  "S3_5_C1_C0_0"
#define TTBR0_EL12_SYSREG  "S3_5_C2_C0_0"
#define TTBR1_EL12_SYSREG  "S3_5_C2_C0_1"
#define TCR_EL12_SYSREG    "S3_5_C2_C0_2"
#define MAIR_EL12_SYSREG   "S3_5_C10_C2_0"

/* Convert identity-mapped pointer to physical address */
static UINT64 phys_from_ptr(const VOID *ptr)
{
    UINT64 va = (UINT64)(uintptr_t)ptr;

    if (va == 0)
    {
        for (;;) __asm__ volatile("wfi");
    }

    if (va >= ARM64_KSEG0_BASE)
    {
        EarlyUartPuts("[PT] FATAL: VA outside identity window\n");
        for (;;) __asm__ volatile("wfi");
    }

    return va;
}

/* Static fallback storage (used once Boot Services are gone) */
static UINT8 arm64_l0_page_table_storage_raw[ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_kernel_l0_table_storage_raw[ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));

static UINT8 arm64_l1_page_tables_storage_raw[ARM64_USER_L1_TABLES * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_kernel_l1_tables_storage_raw[ARM64_KERNEL_L1_TABLES * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));

static UINT8 arm64_kernel_l2_tables_storage_raw[
    ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_kernel_l3_tables_storage_raw[
    ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2 * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_user_l2_tables_storage_raw[
    ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_user_l3_tables_storage_raw[
    ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2 * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));

/* Static storage for extra kernel L0 slots (Hyperspace, Paged Pool, PFN DB, etc.) */
static UINT8 arm64_extra_l1_tables_storage_raw[
    ARM64_EXTRA_KERNEL_SLOTS * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_extra_l2_tables_storage_raw[
    ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_extra_l3_tables_storage_raw[
    ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT * ARM64_EXTRA_L3_PER_L2 * ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));

#define ARM64_STATIC_EXTRA_PT_PAGES 1024
static UINT8 arm64_static_extra_pt_arena[ARM64_STATIC_EXTRA_PT_PAGES * PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT64 arm64_static_extra_pt_offset = 0;

/* Retype page-table allocations so the kernel won't reclaim them as LoaderLoadedProgram. */
typedef struct _ARM64_PT_ALLOCATION
{
    EFI_PHYSICAL_ADDRESS Base;
    UINTN Pages;
} ARM64_PT_ALLOCATION;

#define ARM64_PT_ALLOCATION_MAX 2048
static ARM64_PT_ALLOCATION Arm64PtAllocations[ARM64_PT_ALLOCATION_MAX];
static UINTN Arm64PtAllocationCount = 0;
static BOOLEAN Arm64PtAllocationsApplied = FALSE;

VOID Arm64ApplyDeferredPageTableMemoryTypes(VOID);

extern PFN_NUMBER MmLowestPhysicalPage;
extern PFN_NUMBER MmHighestPhysicalPage;

static BOOLEAN
Arm64LookupTableCoversPageTableAllocation(EFI_PHYSICAL_ADDRESS Base, UINTN Pages)
{
    PFN_NUMBER StartPage = (PFN_NUMBER)(Base >> PAGE_SHIFT);
    PFN_NUMBER EndPage = StartPage + Pages;
    PFN_NUMBER LowestPage = MmLowestPhysicalPage;
    PFN_NUMBER LookupEndPage = LowestPage + MmGetTotalPagesInLookupTable();

    return Pages != 0 &&
           StartPage >= LowestPage &&
           EndPage <= LookupEndPage;
}

static VOID
Arm64RecordPageTableAllocation(EFI_PHYSICAL_ADDRESS Base, UINTN Pages)
{
    if (Pages == 0)
        return;

    /*
     * Post-EBS we cannot touch the freeldr MM page-lookup-table (it lives in
     * memory the firmware may have torn down out from under us). Just record
     * the allocation in our local list and let
     * Arm64ApplyDeferredPageTableMemoryTypes() flush it later when the kernel
     * actually walks the loader memory descriptors.
     */
    if (BootServicesExitedFlag)
    {
        if (Arm64PtAllocationCount < ARRAYSIZE(Arm64PtAllocations))
        {
            Arm64PtAllocations[Arm64PtAllocationCount].Base = Base;
            Arm64PtAllocations[Arm64PtAllocationCount].Pages = Pages;
            Arm64PtAllocationCount++;
        }
        return;
    }

    if (PageLookupTableAddress && Arm64PtAllocationsApplied)
    {
        if (Arm64LookupTableCoversPageTableAllocation(Base, Pages))
        {
            MmSetMemoryType((PVOID)(ULONG_PTR)Base, Pages * PAGE_SIZE, LoaderMemoryData);
        }
        return;
    }

    if (Arm64PtAllocationCount < ARRAYSIZE(Arm64PtAllocations))
    {
        Arm64PtAllocations[Arm64PtAllocationCount].Base = Base;
        Arm64PtAllocations[Arm64PtAllocationCount].Pages = Pages;
        Arm64PtAllocationCount++;
    }
    else
    {
        ERR("ARM64: PT allocation tracking overflow (base=0x%llx pages=%llu)\n",
            (unsigned long long)Base,
            (unsigned long long)Pages);
    }

    if (PageLookupTableAddress && !Arm64PtAllocationsApplied)
    {
        Arm64ApplyDeferredPageTableMemoryTypes();
    }
}

VOID
Arm64ApplyDeferredPageTableMemoryTypes(VOID)
{
    if (Arm64PtAllocationsApplied || !PageLookupTableAddress)
        return;

    /*
     * Post-EBS: MmSetMemoryType writes to the freeldr page-lookup-table whose
     * pages may have been reclaimed/torn down by the firmware on EBS, faulting
     * with a synchronous abort. The page-type tracking is an optimization for
     * the kernel's later memory-descriptor walk; skipping it is safe because
     * the static-arena pages live inside the loader image (already
     * LoaderMemoryData-typed for that image).
     */
    if (BootServicesExitedFlag)
    {
        Arm64PtAllocationsApplied = TRUE;
        return;
    }

    for (UINTN i = 0; i < Arm64PtAllocationCount; ++i)
    {
        if (Arm64PtAllocations[i].Pages == 0)
            continue;

        if (!Arm64LookupTableCoversPageTableAllocation(Arm64PtAllocations[i].Base,
                                                       Arm64PtAllocations[i].Pages))
        {
            continue;
        }

        MmSetMemoryType((PVOID)(ULONG_PTR)Arm64PtAllocations[i].Base,
                        Arm64PtAllocations[i].Pages * PAGE_SIZE,
                        LoaderMemoryData);
    }

    Arm64PtAllocationsApplied = TRUE;
}

static VOID verify_page_aligned(const VOID *ptr, const char *name)
{
    if (((UINT64)(uintptr_t)ptr & (PAGE_SIZE - 1ULL)) == 0)
        return;

    ERR("ARM64: static table %s misaligned @ 0x%llx\n",
        name, (unsigned long long)(UINT64)(uintptr_t)ptr);
    UartPuts("ARM64: FATAL static table misalignment\n");
    for (;;)
        __asm__ volatile("wfi");
}

static VOID* align_page_storage(VOID *storage)
{
    UINT64 addr = (UINT64)(uintptr_t)storage;
    addr = (addr + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    return (VOID *)(uintptr_t)addr;
}

/*
 * Look up the index into arm64_extra_kernel_l0_slots for a given L0 index.
 * Returns the slot index (0..ARM64_EXTRA_KERNEL_SLOTS-1) if found, or
 * ARM64_EXTRA_KERNEL_SLOTS if not an extra kernel slot.
 */
static inline UINT64 get_extra_kernel_slot_index(UINT64 l0_idx)
{
    for (UINT64 i = 0; i < ARM64_EXTRA_KERNEL_SLOTS; ++i)
    {
        if (arm64_extra_kernel_l0_slots[i] == l0_idx)
            return i;
    }
    return ARM64_EXTRA_KERNEL_SLOTS;  /* Not found */
}

/*
 * Check if an L0 index belongs to the extra kernel slots.
 */
static __attribute__((unused)) inline BOOLEAN is_extra_kernel_slot(UINT64 l0_idx)
{
    return get_extra_kernel_slot_index(l0_idx) < ARM64_EXTRA_KERNEL_SLOTS;
}

/* -------------------------------------------------------------------------- */
/* Prototypes                                                                 */
/* -------------------------------------------------------------------------- */

static UINT64 get_tcr(UINT64 *pips, UINT64 *pva_bits);
static UINT64 get_tcr_for_el(UINT64 *pips, UINT64 *pva_bits, int el, BOOLEAN el12);
static int get_effective_el(VOID);
static BOOLEAN use_el12_registers(VOID);
static VOID setup_pgtables(VOID);
static VOID ensure_page_tables_initialized(VOID);
static VOID set_ttbr_tcr_mair(int el, UINT64 table0, UINT64 table1, UINT64 tcr, UINT64 attr);
static BOOLEAN Arm64MapEarlyUart(VOID);

static BOOLEAN map_region_hierarchical(UINT64 va, UINT64 pa, UINT64 size, UINT64 attrs);
static UINT64* ensure_l2_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l1_table, UINT64 l1_index, UINT64 va);
static UINT64* ensure_l3_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table, UINT64 l2_index, UINT64 va);
static BOOLEAN Arm64UpdateMappingAttributes(ULONGLONG Va, ULONGLONG Size, UINT64 set_mask, UINT64 clear_mask);
static VOID Arm64ApplyImageSectionProtections(VOID);


static VOID use_static_page_tables(VOID)
{
    arm64_l0_page_table = (UINT64 *)align_page_storage(arm64_l0_page_table_storage_raw);
    arm64_kernel_l0_table = (UINT64 *)align_page_storage(arm64_kernel_l0_table_storage_raw);
    arm64_l1_page_tables = (UINT64 (*)[ARM64_PT_ENTRIES])
        align_page_storage(arm64_l1_page_tables_storage_raw);
    arm64_kernel_l1_tables = (UINT64 (*)[ARM64_PT_ENTRIES])
        align_page_storage(arm64_kernel_l1_tables_storage_raw);
    arm64_kernel_l2_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_PT_ENTRIES])
        align_page_storage(arm64_kernel_l2_tables_storage_raw);
    arm64_kernel_l3_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_L3_TABLES_PER_L2][ARM64_PT_ENTRIES])
        align_page_storage(arm64_kernel_l3_tables_storage_raw);
    arm64_user_l2_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_PT_ENTRIES])
        align_page_storage(arm64_user_l2_tables_storage_raw);
    arm64_user_l3_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_L3_TABLES_PER_L2][ARM64_PT_ENTRIES])
        align_page_storage(arm64_user_l3_tables_storage_raw);

    /* Initialize extra kernel L0 slot tables (Hyperspace, Paged Pool, PFN DB, etc.) */
    {
        UINT8 *l1_base = (UINT8 *)align_page_storage(arm64_extra_l1_tables_storage_raw);
        for (UINT64 i = 0; i < ARM64_EXTRA_KERNEL_SLOTS; ++i)
        {
            arm64_extra_l1_tables[i] = (UINT64 *)(l1_base + i * ARM64_PT_BYTES);
        }
    }
    arm64_extra_l2_tables = (UINT64 (*)[ARM64_EXTRA_L2_PER_SLOT][ARM64_PT_ENTRIES])
        align_page_storage(arm64_extra_l2_tables_storage_raw);
    arm64_extra_l3_tables = (UINT64 (*)[ARM64_EXTRA_L2_PER_SLOT][ARM64_EXTRA_L3_PER_L2][ARM64_PT_ENTRIES])
        align_page_storage(arm64_extra_l3_tables_storage_raw);

    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_l0_page_table, 1);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_kernel_l0_table, 1);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_l1_page_tables,
                                   ARM64_USER_L1_TABLES);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_kernel_l1_tables,
                                   ARM64_KERNEL_L1_TABLES);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_kernel_l2_tables,
                                   ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_kernel_l3_tables,
                                   ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 *
                                       ARM64_L3_TABLES_PER_L2);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_user_l2_tables,
                                   ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_user_l3_tables,
                                   ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1 *
                                       ARM64_L3_TABLES_PER_L2);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_extra_l1_tables[0],
                                   ARM64_EXTRA_KERNEL_SLOTS);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_extra_l2_tables,
                                   ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_extra_l3_tables,
                                   ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT *
                                       ARM64_EXTRA_L3_PER_L2);

    {
        const struct {
            const char *name;
            const VOID *ptr;
        } tables[] = {
            {"TTBR0_L0", arm64_l0_page_table},
            {"TTBR1_L0", arm64_kernel_l0_table},
            {"TTBR0_L1", arm64_l1_page_tables},
            {"TTBR1_L1", arm64_kernel_l1_tables},
            {"TTBR1_L2", arm64_kernel_l2_tables},
            {"TTBR1_L3", arm64_kernel_l3_tables},
            {"TTBR0_L2", arm64_user_l2_tables},
            {"TTBR0_L3", arm64_user_l3_tables},
            {"EXTRA_L2", arm64_extra_l2_tables},
            {"EXTRA_L3", arm64_extra_l3_tables},
        };

        for (ULONG i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i)
            verify_page_aligned(tables[i].ptr, tables[i].name);

        /* Also verify extra L1 tables */
        for (UINT64 i = 0; i < ARM64_EXTRA_KERNEL_SLOTS; ++i)
        {
            verify_page_aligned(arm64_extra_l1_tables[i], "EXTRA_L1");
        }
    }
}

static VOID*
allocate_static_pt_pages(UINTN pages, const char *label)
{
    SIZE_T bytes = (SIZE_T)pages * PAGE_SIZE;
    UINT64 arena_base = (UINT64)(uintptr_t)arm64_static_extra_pt_arena;
    UINT64 current_abs = arena_base + arm64_static_extra_pt_offset;
    UINT64 aligned_abs = (current_abs + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
    UINT64 new_offset = aligned_abs - arena_base;
    UINT64 required = new_offset + (UINT64)pages * PAGE_SIZE;
    VOID *ptr;

    if (required > sizeof(arm64_static_extra_pt_arena))
    {
        ERR("ARM64: static PT arena exhausted while allocating %s\n", label);
        UartPuts("ARM64: ERROR allocating page tables (static arena exhausted)\n");
        return NULL;
    }

    ptr = (VOID *)(uintptr_t)aligned_abs;
    arm64_static_extra_pt_offset = required;
    RtlZeroMemory(ptr, bytes);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(UINT64)(uintptr_t)ptr, pages);

    /* Verify alignment before returning */
    if (((UINT64)(uintptr_t)ptr & (PAGE_SIZE - 1ULL)) != 0)
    {
        ERR("ARM64: Static PT arena returned misaligned %s @ 0x%llx\n",
            label, (unsigned long long)(UINT64)(uintptr_t)ptr);
        UartPuts("ARM64: FATAL: page table allocation misaligned\n");
        return NULL;
    }

    return ptr;
}

static VOID*
allocate_pt_pages(UINTN pages, const char *label)
{
    EFI_BOOT_SERVICES *bs;
    EFI_PHYSICAL_ADDRESS addr;
    EFI_STATUS status;
    VOID *ptr;

    if (BootServicesExitedFlag || !GlobalSystemTable || !(bs = GlobalSystemTable->BootServices))
    {
        /*
         * After ExitBootServices we cannot trust new heap allocations to be
         * mapped. Stick to the static arena that lives inside the loader image.
         */
        return allocate_static_pt_pages(pages, label);
    }

    addr = 0xFFFFFFFFULL;
    status = bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &addr);
    if (EFI_ERROR(status))
    {
        EFI_STATUS low_status = status;

        /*
         * Large-memory ARM64 guests can exhaust or fragment the low 4GB EFI
         * loader range before all seed and spill page tables are allocated.
         * UEFI on AArch64 uses identity-addressable loader memory, and the
         * table descriptors carry real physical addresses, so high loader
         * pages are valid page-table backing as long as we record them for the
         * NT memory map handoff.
         */
        addr = 0;
        status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &addr);
        if (EFI_ERROR(status))
        {
            /*
             * UefiMemGetMemoryMap() deliberately converts conventional EFI
             * memory into FreeLoader-managed memory, so later EFI allocations
             * can fail even though the loader still owns free pages.
             */
            if (PageLookupTableAddress)
            {
                ptr = MmAllocateMemoryWithType((SIZE_T)pages * PAGE_SIZE,
                                               LoaderMemoryData);
                if (ptr)
                {
                    addr = (EFI_PHYSICAL_ADDRESS)(UINT64)(uintptr_t)ptr;
                    status = EFI_SUCCESS;
                }
            }

            if (EFI_ERROR(status))
            {
                ptr = allocate_static_pt_pages(pages, label);
                if (ptr)
                    return ptr;

                ERR("ARM64: AllocatePages failed for %s (%u pages): low=%lx any=%lx\n",
                    label, (unsigned)pages, (ULONG_PTR)low_status, (ULONG_PTR)status);
                UartPuts("ARM64: ERROR allocating page table memory\n");
                return NULL;
            }
        }
    }

    /*
     * CRITICAL: Validate the returned address.
     * Some UEFI firmwares (e.g., RPi5) may return 0 or an otherwise invalid
     * address even when AllocatePages reports success. Address 0 is never
     * valid for page tables, and addresses must be page-aligned.
     */
    if (addr == 0 || (addr & (PAGE_SIZE - 1)) != 0)
    {
        ERR("ARM64: AllocatePages returned invalid address 0x%llx for %s\n",
            (unsigned long long)addr, label);
        UartPuts("ARM64: ERROR invalid page table address from UEFI\n");
        /* Free the bogus allocation if possible (addr=0 free is harmless) */
        bs->FreePages(addr, pages);
        return NULL;
    }

    ptr = (VOID *)(uintptr_t)addr;
    RtlZeroMemory(ptr, pages * PAGE_SIZE);
    Arm64RecordPageTableAllocation(addr, pages);
    return ptr;
}

static BOOLEAN allocate_page_table_memory(VOID)
{
    if (page_table_memory_allocated)
        return TRUE;

    if (BootServicesExitedFlag || !GlobalSystemTable || !GlobalSystemTable->BootServices)
    {
        use_static_page_tables();
        page_table_memory_allocated = TRUE;
        return TRUE;
    }

    arm64_l0_page_table = allocate_pt_pages(1, "TTBR0 L0");
    arm64_kernel_l0_table = allocate_pt_pages(1, "TTBR1 L0");

    if (!arm64_l0_page_table || !arm64_kernel_l0_table)
    {
        use_static_page_tables();
        page_table_memory_allocated = TRUE;
        return TRUE;
    }

    arm64_l1_page_tables = (UINT64 (*)[ARM64_PT_ENTRIES])
        allocate_pt_pages(ARM64_USER_L1_TABLES, "TTBR0 L1 pool");
    arm64_kernel_l1_tables = (UINT64 (*)[ARM64_PT_ENTRIES])
        allocate_pt_pages(ARM64_KERNEL_L1_TABLES, "TTBR1 L1 pool");

    if (!arm64_l1_page_tables || !arm64_kernel_l1_tables)
    {
        use_static_page_tables();
        page_table_memory_allocated = TRUE;
        return TRUE;
    }

    arm64_kernel_l2_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_PT_ENTRIES])
        allocate_pt_pages(ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1,
                          "TTBR1 L2 pool");
    arm64_kernel_l3_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_L3_TABLES_PER_L2][ARM64_PT_ENTRIES])
        allocate_pt_pages(ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2,
                          "TTBR1 L3 pool");

    arm64_user_l2_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_PT_ENTRIES])
        allocate_pt_pages(ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1,
                          "TTBR0 L2 pool");
    arm64_user_l3_tables = (UINT64 (*)[ARM64_L2_TABLES_PER_L1][ARM64_L3_TABLES_PER_L2][ARM64_PT_ENTRIES])
        allocate_pt_pages(ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2,
                          "TTBR0 L3 pool");

    if (!arm64_kernel_l2_tables || !arm64_kernel_l3_tables ||
        !arm64_user_l2_tables || !arm64_user_l3_tables)
    {
        use_static_page_tables();
        page_table_memory_allocated = TRUE;
        return TRUE;
    }

    /* Allocate extra kernel L0 slot tables (Hyperspace, Paged Pool, PFN DB, etc.) */
    {
        BOOLEAN alloc_failed = FALSE;
        for (UINT64 i = 0; i < ARM64_EXTRA_KERNEL_SLOTS; ++i)
        {
            arm64_extra_l1_tables[i] = allocate_pt_pages(1, "EXTRA L1");
            if (!arm64_extra_l1_tables[i])
            {
                alloc_failed = TRUE;
                break;
            }
        }
        if (!alloc_failed)
        {
            arm64_extra_l2_tables = (UINT64 (*)[ARM64_EXTRA_L2_PER_SLOT][ARM64_PT_ENTRIES])
                allocate_pt_pages(ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT,
                                  "EXTRA L2 pool");
            arm64_extra_l3_tables = (UINT64 (*)[ARM64_EXTRA_L2_PER_SLOT][ARM64_EXTRA_L3_PER_L2][ARM64_PT_ENTRIES])
                allocate_pt_pages(ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT * ARM64_EXTRA_L3_PER_L2,
                                  "EXTRA L3 pool");
            if (!arm64_extra_l2_tables || !arm64_extra_l3_tables)
                alloc_failed = TRUE;
        }
        if (alloc_failed)
        {
            /* Fallback to static storage for extra tables only */
            UINT8 *l1_base = (UINT8 *)align_page_storage(arm64_extra_l1_tables_storage_raw);
            for (UINT64 i = 0; i < ARM64_EXTRA_KERNEL_SLOTS; ++i)
            {
                arm64_extra_l1_tables[i] = (UINT64 *)(l1_base + i * ARM64_PT_BYTES);
            }
            arm64_extra_l2_tables = (UINT64 (*)[ARM64_EXTRA_L2_PER_SLOT][ARM64_PT_ENTRIES])
                align_page_storage(arm64_extra_l2_tables_storage_raw);
            arm64_extra_l3_tables = (UINT64 (*)[ARM64_EXTRA_L2_PER_SLOT][ARM64_EXTRA_L3_PER_L2][ARM64_PT_ENTRIES])
                align_page_storage(arm64_extra_l3_tables_storage_raw);
            TRACE("ARM64: Using static storage for extra kernel tables\n");
        }
    }

    page_table_memory_allocated = TRUE;
    return TRUE;
}

static inline UINT64 read_ttbr1(BOOLEAN el12)
{
    UINT64 value;
    if (el12)
        __asm__ volatile("mrs %0, " TTBR1_EL12_SYSREG : "=r"(value));
    else
        __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(value));
    return value;
}

static inline VOID write_ttbr1(BOOLEAN el12, UINT64 value)
{
    if (el12)
        __asm__ volatile("msr " TTBR1_EL12_SYSREG ", %0" :: "r"(value) : "memory");
    else
        __asm__ volatile("msr ttbr1_el1, %0" :: "r"(value) : "memory");
}

static inline UINT64
arm64_ttbr1_l0_base(UINT64 RootPa)
{
    return (RootPa & ~(UINT64)(PAGE_SIZE - 1ULL)) + ARM64_TTBR1_L0_OFFSET;
}

typedef enum _ARM64_MAPPING_TARGET
{
    Arm64MappingIdentity,
    Arm64MappingKernel,
    Arm64MappingPhysicalAlias
} ARM64_MAPPING_TARGET;

#define ARM64_PLAN_MAX_REQUESTS 512

typedef struct _ARM64_MAPPING_REQUEST
{
    ARM64_MAPPING_TARGET Target;
    ULONGLONG VirtualBase;
    ULONGLONG PhysicalBase;
    ULONGLONG Size;
    ULONG Attributes;
    BOOLEAN TableOnly;
} ARM64_MAPPING_REQUEST, *PARM64_MAPPING_REQUEST;

typedef struct _ARM64_MAPPING_PLAN
{
    FREELDR_MEMORY_DESCRIPTOR *MemoryMap;
    ULONG DescriptorCount;
    ARM64_MAPPING_REQUEST Requests[ARM64_PLAN_MAX_REQUESTS];
    ULONG RequestCount;
} ARM64_MAPPING_PLAN, *PARM64_MAPPING_PLAN;

typedef struct _ARM64_RESERVATION_HINTS
{
    ULONGLONG HighestPage;
    ULONGLONG TotalPages;
    ULONGLONG PfnBytes;
    ULONGLONG NonPagedBytes;
} ARM64_RESERVATION_HINTS, *PARM64_RESERVATION_HINTS;

static VOID Arm64MappingPlanInit(PARM64_MAPPING_PLAN Plan,
                                 FREELDR_MEMORY_DESCRIPTOR *Map,
                                 ULONG Count);
static BOOLEAN Arm64MappingPlanApply(ARM64_MAPPING_PLAN *Plan,
                                     ARM64_MAPPING_TARGET Target);
static UINT64 Arm64MemoryAttributesForDescriptor(const FREELDR_MEMORY_DESCRIPTOR *Descriptor,
                                                 BOOLEAN IdentityMap);
static BOOLEAN Arm64DescriptorMapsInKernel(const FREELDR_MEMORY_DESCRIPTOR *Descriptor);
static BOOLEAN Arm64DescriptorIsSystemMemory(const FREELDR_MEMORY_DESCRIPTOR *Descriptor);
static BOOLEAN Arm64MappingPlanAddMapping(PARM64_MAPPING_PLAN Plan,
                                          ARM64_MAPPING_TARGET Target,
                                          ULONGLONG VirtualBase,
                                          ULONGLONG PhysicalBase,
                                          ULONGLONG Size,
                                          ULONG Attributes,
                                          BOOLEAN TableOnly);
static BOOLEAN Arm64MappingPlanEnsureTables(PARM64_MAPPING_PLAN Plan,
                                            ULONGLONG VirtualBase,
                                            ULONGLONG Size);
static BOOLEAN Arm64EnsureRangeTables(ULONGLONG VirtualBase,
                                      ULONGLONG Size,
                                      BOOLEAN KernelSpace);

/* Self-map window functions */
static BOOLEAN Arm64SetupSelfMapWindows(VOID);

/* ---------- Hardware capability query ---------- */
/* ID_AA64MMFR0_EL1: PARange[3:0], TGran4[31:28] (0 = supported) */
static BOOLEAN g_granule4k_supported = TRUE;
static UINT64  g_parange_field      = 0;

static VOID query_mmfr0_caps(VOID)
{
    UINT64 mmfr0;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    g_parange_field = (mmfr0 & 0xFULL);               /* PARange */
    UINT64 tgran4   = (mmfr0 >> 28) & 0xFULL;         /* TGran4 */
    /*
     * ARM ARM D17.2.97 ID_AA64MMFR0_EL1.TGran4:
     *   0b0000 = 4KB granule supported
     *   0b0001 = 4KB granule supported with FEAT_LPA2 (52-bit IPA/PA)
     *   0b1111 = 4KB granule NOT supported
     * QEMU `-cpu max` advertises FEAT_LPA2 (tgran4 == 1), so accepting only
     * 0b0000 was too narrow.
     */
    g_granule4k_supported = (tgran4 != 0xF);
}

/* ---------- EL helpers ---------- */

static int get_effective_el(VOID)
{
    return (int)((ARM64_READ_SYSREG(CurrentEL) >> 2) & 3);
}

static BOOLEAN use_el12_registers(VOID)
{
    int el = get_effective_el();
    if (el != 2) return FALSE;
    UINT64 hcr_el2;
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(hcr_el2));
    return (hcr_el2 & (1ULL << 34)) != 0; /* HCR_EL2.E2H */
}

/* ---------- Safe PTE write & BBM helpers ---------- */

static inline void pte_write(UINT64 *entry, UINT64 val)
{
    *entry = val;
    Arm64CleanDataCacheToPoC((ULONGLONG)(uintptr_t)entry);
    ARM64_DSB_ISHST(); /* Ensure PTE store is visible before we invalidate TLBs */
}

/* Heavy (boot-safe) BBM: clear -> TLBI range -> DSB ISH -> ISB -> set */
static inline void pte_replace_break_before_make(UINT64 *entry, UINT64 newval, UINT64 va, UINT64 size)
{
    if (*entry == newval) {
        return;
    }
    if (DESC_VALID(*entry)) {
        pte_write(entry, 0);
        tlbi_va_entry(va, size);
        ARM64_DSB_ISH();
        ARM64_ISB();
    }
    pte_write(entry, newval);
}

/* ---------- TCR composition (now clamped by hw caps) ---------- */

static UINT64
get_tcr(
    UINT64 *pips,
    UINT64 *pva_bits)
{
    return get_tcr_for_el(pips,
                          pva_bits,
                          get_effective_el(),
                          use_el12_registers());
}

static UINT64
get_tcr_for_el(
    UINT64 *pips,
    UINT64 *pva_bits,
    int el,
    BOOLEAN el12)
{
    query_mmfr0_caps();
    if (!g_granule4k_supported) {
        ERR("ARM64: 4KiB granule not supported on this CPU; MMU setup aborted.\n");
        if (pips) *pips = 0;
        if (pva_bits) *pva_bits = 0;
        return 0;
    }

    UINT64 max_addr = cached_max_physical_address;

    /* Refresh the cached physical limit while firmware services are usable */
    if (!BootServicesExitedFlag && GlobalSystemTable && GlobalSystemTable->BootServices) {
        ULONG MemoryMapSize;
        FREELDR_MEMORY_DESCRIPTOR* MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
        if (MemoryMap) {
            max_addr = 0;
            for (ULONG i = 0; i < MemoryMapSize; i++) {
                UINT64 end_addr = (UINT64)(MemoryMap[i].BasePage + MemoryMap[i].PageCount) * PAGE_SIZE;
                if (end_addr > max_addr) max_addr = end_addr;
            }
            cached_max_physical_address = max_addr;
        }
    }

    /*
     * Ensure the identity view stays large enough to cover the loader itself.
     * Once Boot Services are gone the firmware map may only expose <=4 GiB,
     * but FreeLDR may be linked above 4 GiB.  If we shrink T0SZ to 32 bits
     * the write to TCR_EL1 could fault because the current PC sits outside
     * the configured TTBR0 range.  Fold the active PC into the size estimate
     * so we always keep at least the loader region mapped while we transition
     * to the kernel view.
     */
    {
        UINT64 current_pc;
        __asm__ volatile("adr %0, ." : "=r"(current_pc));

        if (current_pc > max_addr)
            max_addr = current_pc;
    }

    /* Desired IPS from memory size */
    UINT64 ips_desired;
    UINT64 va_bits = (max_addr > (1ULL << 44)) ? 48 :
                     (max_addr > (1ULL << 42)) ? 44 :
                     (max_addr > (1ULL << 40)) ? 42 :
                     (max_addr > (1ULL << 36)) ? 40 :
                     (max_addr > (1ULL << 32)) ? 36 : 32;

    /*
     * CRITICAL FIX: Our page table structure uses 4-level tables (L0->L1->L2->L3).
     * With 4KB granule:
     *   - T0SZ <= 25 (VA >= 39 bits): 4 levels starting at L0
     *   - T0SZ >= 26 (VA <= 38 bits): 3 levels starting at L1
     *
     * If va_bits < 39, the CPU interprets TTBR0 as pointing to L1 instead of L0,
     * causing translation faults because our L0 table entries are read as L1.
     *
     * Match the NT ARM64 contract: 47-bit TTBR0/TTBR1 VA space with a 4-level
     * root page split between the lower TTBR0 half and upper TTBR1 half.
     */
    if (va_bits < 47) {
        TRACE("ARM64: Forcing va_bits from %llu to 47 for NT ARM64 page tables\n",
              (unsigned long long)va_bits);
        va_bits = 47;
    }

    ips_desired = 5;

    /* Keep TTBR0 wide enough for the loader even if firmware reported <4 GiB. */
    if (va_bits < 36) {
        va_bits = 36;
        if (ips_desired < 1)
            ips_desired = (g_parange_field >= 1) ? 1 : ips_desired;
    }

    /* Clamp IPS to hardware PARange */
    UINT64 ips_hw = g_parange_field; /* 0..6 per ARM ARM; we only use up to 5 */
    if (ips_desired > ips_hw) ips_desired = ips_hw;

    UINT64 tcr;
    if (el == 1 || el12) {
        tcr = TCR_EL1_RSVD | (ips_desired << 32);
    } else {
        tcr = TCR_EL2_RSVD | (ips_desired << 16);
    }

    /* TTBR0 (user/identity) */
    tcr |= TCR_T0SZ(va_bits) | TCR_SHARED_INNER | TCR_ORGN_WBWA | TCR_IRGN_WBWA | TCR_TG0_4K;
    /* TTBR1 (kernel window fixed to the NT ARM64 47-bit contract) */
    tcr |= TCR_T1SZ(47) | TCR_SHARED1_INNER | TCR_ORGN1_WBWA | TCR_IRGN1_WBWA | TCR_TG1_4K
            | TCR_A1;

    /* Keep hardware Access Flag updates disabled; ReactOS seeds AF in entries
     * and handles AF faults in software, matching the NT ARM64 TCR contract. */

    if (pips) *pips = ips_desired;
    if (pva_bits) *pva_bits = va_bits;
    return tcr;
}

/* ---------- Register programming ---------- */

static VOID set_ttbr_tcr_mair(int el, UINT64 table0, UINT64 table1, UINT64 tcr, UINT64 attr)
{
    BOOLEAN el12 = use_el12_registers();
    const VOID *table0_ptr = (const VOID *)(uintptr_t)table0;
    const VOID *table1_ptr = (const VOID *)(uintptr_t)table1;
    UINT64 phys_table0 = phys_from_ptr(table0_ptr);
    UINT64 phys_table1 = phys_from_ptr(table1_ptr);
    UINT64 ttbr1_table_base = arm64_ttbr1_l0_base(phys_table1);

    ARM64_DSB_ISH();

    if (el == 1 || el12) {
        if (el12) {
            __asm__ volatile("msr " TTBR0_EL12_SYSREG ", %0" :: "r"(phys_table0) : "memory");
            write_ttbr1(TRUE, ttbr1_table_base);
            __asm__ volatile("msr " TCR_EL12_SYSREG   ", %0" :: "r"(tcr)    : "memory");
            __asm__ volatile("msr " MAIR_EL12_SYSREG  ", %0" :: "r"(attr)   : "memory");
        } else {
            __asm__ volatile("msr ttbr0_el1, %0" :: "r"(phys_table0) : "memory");
            write_ttbr1(FALSE, ttbr1_table_base);
            __asm__ volatile("msr tcr_el1, %0"  :: "r"(tcr)     : "memory");
            __asm__ volatile("msr mair_el1, %0" :: "r"(attr)    : "memory");
        }
    } else if (el == 2) {
        __asm__ volatile("msr ttbr0_el2, %0" :: "r"(phys_table0) : "memory");
        __asm__ volatile("msr tcr_el2, %0"   :: "r"(tcr)    : "memory");
        __asm__ volatile("msr mair_el2, %0"  :: "r"(attr)   : "memory");
    }

    ARM64_ISB();
}

/* ---------- Page-table allocation helpers ---------- */

static UINT64* ensure_l2_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l1_table, UINT64 l1_index, UINT64 va)
{
    UINT64 entry = l1_table[l1_index];

    if (DESC_VALID(entry)) {
        if (DESC_IS_TABLE(entry))
            return (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);

        if (DESC_IS_LEAF(entry))
        {
            UINT64 block_base = entry & PTE_BLOCK_ADDR_MASK_1G;
            UINT64 block_attrs = sanitize_block_attrs(entry & ~((UINT64)ARM64_BLOCK_MASK_1G | PTE_TYPE_MASK));
            UINT64 *split_table;

            if (is_kernel)
            {
                if (l0_slot < ARM64_KERNEL_L1_TABLES)
                {
                    if (arm64_l2_next_index[l0_slot] < ARM64_L2_TABLES_PER_L1)
                    {
                        UINT64 index = arm64_l2_next_index[l0_slot]++;
                        split_table = arm64_kernel_l2_tables[l0_slot][index];
                    }
                    else
                    {
                        split_table = allocate_pt_pages(1, "TTBR1 L2 (kernel spill split)");
                        if (!split_table)
                            return NULL;
                    }
                }
                else
                {
                    /* Check if this is a pre-allocated extra kernel slot */
                    UINT64 extra_slot = get_extra_kernel_slot_index(l0_slot);
                    if (extra_slot < ARM64_EXTRA_KERNEL_SLOTS && arm64_extra_l2_tables)
                    {
                        if (arm64_extra_l2_next_index[extra_slot] < ARM64_EXTRA_L2_PER_SLOT)
                        {
                            UINT64 index = arm64_extra_l2_next_index[extra_slot]++;
                            split_table = arm64_extra_l2_tables[extra_slot][index];
                        }
                        else
                        {
                            split_table = allocate_pt_pages(1, "TTBR1 L2 (extra spill split)");
                            if (!split_table)
                                return NULL;
                        }
                    }
                    else
                    {
                        /* Fallback: allocate a one-off L2 table for non-KSEG0 kernel slot */
                        split_table = allocate_pt_pages(1, "TTBR1 L2 (split)");
                        if (!split_table)
                            return NULL;
                    }
                }
            }
            else
            {
                if (l0_slot >= ARM64_USER_L1_TABLES) return NULL;
                if (arm64_user_l2_next_index[l0_slot] < ARM64_L2_TABLES_PER_L1)
                {
                    UINT64 index = arm64_user_l2_next_index[l0_slot]++;
                    split_table = arm64_user_l2_tables[l0_slot][index];
                }
                else
                {
                    split_table = allocate_pt_pages(1, "TTBR0 L2 (user spill split)");
                    if (!split_table)
                        return NULL;
                }
            }

            RtlZeroMemory(split_table, PAGE_SIZE);
            for (ULONG i = 0; i < 512; ++i)
            {
                UINT64 pa = block_base + ((UINT64)i << 21);
                split_table[i] = pa | PTE_TYPE_VALID | PTE_TYPE_BLOCK | block_attrs;
            }

            UINT64 va_base = va & ~ARM64_BLOCK_MASK_1G;
            pte_replace_break_before_make(&l1_table[l1_index],
                                          phys_from_ptr(split_table) | PTE_TABLE_ATTRS,
                                          va_base,
                                          ARM64_BLOCK_SIZE_1G);
            return split_table;
        }

        TRACE("ARM64: ensure_l2_table unexpected descriptor L1[%llu]=0x%llx\n",
              (unsigned long long)l1_index,
              (unsigned long long)entry);
        return NULL;
    }

    if (is_kernel) {
        if (l0_slot < ARM64_KERNEL_L1_TABLES)
        {
            UINT64 *new_table;

            if (arm64_l2_next_index[l0_slot] < ARM64_L2_TABLES_PER_L1)
            {
                UINT64 index = arm64_l2_next_index[l0_slot]++;
                new_table = arm64_kernel_l2_tables[l0_slot][index];
            }
            else
            {
                new_table = allocate_pt_pages(1, "TTBR1 L2 (kernel spill)");
                if (!new_table)
                    return NULL;
            }

            RtlZeroMemory(new_table, PAGE_SIZE);
            pte_write(&l1_table[l1_index],
                      (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
            return new_table;
        }
        else
        {
            /* Check if this is a pre-allocated extra kernel slot */
            UINT64 extra_slot = get_extra_kernel_slot_index(l0_slot);
            if (extra_slot < ARM64_EXTRA_KERNEL_SLOTS && arm64_extra_l2_tables)
            {
                UINT64 *new_table;

                if (arm64_extra_l2_next_index[extra_slot] < ARM64_EXTRA_L2_PER_SLOT)
                {
                    UINT64 index = arm64_extra_l2_next_index[extra_slot]++;
                    new_table = arm64_extra_l2_tables[extra_slot][index];
                }
                else
                {
                    new_table = allocate_pt_pages(1, "TTBR1 L2 (extra spill)");
                    if (!new_table)
                        return NULL;
                }

                RtlZeroMemory(new_table, PAGE_SIZE);
                pte_write(&l1_table[l1_index],
                          (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
                return new_table;
            }
            else
            {
                /* Fallback: allocate a one-off L2 table when outside KSEG0 pool */
                UINT64 *new_table = allocate_pt_pages(1, "TTBR1 L2 (extra)");
                if (!new_table)
                    return NULL;
                RtlZeroMemory(new_table, PAGE_SIZE);
                pte_write(&l1_table[l1_index],
                          (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
                return new_table;
            }
        }
    }

    if (l0_slot >= ARM64_USER_L1_TABLES) return NULL;
    UINT64 *new_table;

    if (arm64_user_l2_next_index[l0_slot] < ARM64_L2_TABLES_PER_L1)
    {
        UINT64 index = arm64_user_l2_next_index[l0_slot]++;
        new_table = arm64_user_l2_tables[l0_slot][index];
    }
    else
    {
        new_table = allocate_pt_pages(1, "TTBR0 L2 (user spill)");
        if (!new_table)
            return NULL;
    }

    RtlZeroMemory(new_table, PAGE_SIZE);
    pte_write(&l1_table[l1_index],
              (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
    return new_table;
}

/*
 * Helper to allocate an L3 table from a flat pool.
 * The L3 tables are stored in a 3D array [l0_slot][l2_slot][index], but we
 * allocate linearly using a single counter per L0 slot. This avoids per-L2-slot
 * exhaustion issues when memory descriptors cause uneven L2 slot usage.
 *
 * Returns the allocated L3 table pointer, or NULL on failure.
 */
static UINT64* alloc_kernel_l3_from_flat_pool(UINT64 l0_slot)
{
    UINT64 flat_idx = arm64_l3_next_index[l0_slot];

    /* Debug: check if arm64_kernel_l3_tables is NULL */
    if (!arm64_kernel_l3_tables)
    {
        UartPuts("[L3] kernel L3 seed pool is NULL; allocating spill table\n");
        return allocate_pt_pages(1, "TTBR1 L3 (kernel no-pool spill)");
    }

    if (flat_idx >= ARM64_L3_TABLES_PER_L0)
    {
        return allocate_pt_pages(1, "TTBR1 L3 (kernel spill)");
    }

    /* Convert flat index to [l2_slot][index] for array access */
    UINT64 l2_slot_for_alloc = flat_idx / ARM64_L3_TABLES_PER_L2;
    UINT64 idx_in_l2_slot = flat_idx % ARM64_L3_TABLES_PER_L2;

    UINT64 *result = arm64_kernel_l3_tables[l0_slot][l2_slot_for_alloc][idx_in_l2_slot];

    arm64_l3_next_index[l0_slot]++;
    return result;
}

static UINT64* alloc_user_l3_from_flat_pool(UINT64 l0_slot)
{
    UINT64 flat_idx = arm64_user_l3_next_index[l0_slot];
    if (flat_idx >= ARM64_L3_TABLES_PER_L0)
    {
        return allocate_pt_pages(1, "TTBR0 L3 (user spill)");
    }

    UINT64 l2_slot_for_alloc = flat_idx / ARM64_L3_TABLES_PER_L2;
    UINT64 idx_in_l2_slot = flat_idx % ARM64_L3_TABLES_PER_L2;

    UINT64 *result = arm64_user_l3_tables[l0_slot][l2_slot_for_alloc][idx_in_l2_slot];

    arm64_user_l3_next_index[l0_slot]++;
    return result;
}

/*
 * Helper to allocate an L3 table from the FLAT extra kernel pool.
 * Similar to alloc_kernel_l3_from_flat_pool but for extra kernel slots
 * (Hyperspace, Paged Pool, PFN DB, etc.). Uses a single flat index per
 * extra slot to avoid per-L2-slot exhaustion issues.
 *
 * Returns the allocated L3 table pointer, or NULL on failure.
 */
static UINT64* alloc_extra_l3_from_flat_pool(UINT64 extra_slot)
{
    if (extra_slot >= ARM64_EXTRA_KERNEL_SLOTS)
    {
        UartPuts("[L3] FATAL: invalid extra_slot in alloc_extra_l3_from_flat_pool\n");
        return NULL;
    }

    if (!arm64_extra_l3_tables)
    {
        UartPuts("[L3] extra L3 seed pool is NULL; allocating spill table\n");
        return allocate_pt_pages(1, "TTBR1 L3 (extra no-pool spill)");
    }

    UINT64 flat_idx = arm64_extra_l3_next_index[extra_slot];

    if (flat_idx >= ARM64_EXTRA_L3_PER_SLOT)
    {
        return allocate_pt_pages(1, "TTBR1 L3 (extra spill)");
    }

    /* Convert flat index to [l2_slot][index] for array access */
    UINT64 l2_slot_for_alloc = flat_idx / ARM64_EXTRA_L3_PER_L2;
    UINT64 idx_in_l2_slot = flat_idx % ARM64_EXTRA_L3_PER_L2;

    UINT64 *result = arm64_extra_l3_tables[extra_slot][l2_slot_for_alloc][idx_in_l2_slot];

    arm64_extra_l3_next_index[extra_slot]++;
    return result;
}

static UINT64* ensure_l3_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table, UINT64 l2_index, UINT64 va)
{
    UINT64 entry = l2_table[l2_index];
    BOOLEAN kernel_pool = is_kernel && (l0_slot < ARM64_KERNEL_L1_TABLES);
    BOOLEAN user_pool = (!is_kernel) && (l0_slot < ARM64_USER_L1_TABLES);
    /* Check if this is an extra kernel slot */
    UINT64 extra_slot = is_kernel ? get_extra_kernel_slot_index(l0_slot) : ARM64_EXTRA_KERNEL_SLOTS;
    BOOLEAN extra_pool = is_kernel && (extra_slot < ARM64_EXTRA_KERNEL_SLOTS) && arm64_extra_l3_tables;

    if (DESC_VALID(entry)) {
        if (DESC_IS_TABLE(entry))
            return (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);

        if (DESC_IS_LEAF(entry))
        {
            UINT64 block_base = entry & PTE_BLOCK_ADDR_MASK_2M;
            UINT64 block_attrs = sanitize_block_attrs(entry & ~((UINT64)ARM64_BLOCK_MASK_2M | PTE_TYPE_MASK));
            UINT64 *split_table;

            if (is_kernel)
            {
                if (kernel_pool)
                {
                    split_table = alloc_kernel_l3_from_flat_pool(l0_slot);
                    if (!split_table)
                        return NULL;
                }
                else if (extra_pool)
                {
                    /* Use pre-allocated extra L3 tables with FLAT pool allocation */
                    split_table = alloc_extra_l3_from_flat_pool(extra_slot);
                    if (!split_table)
                        return NULL;
                }
                else
                {
                    split_table = allocate_pt_pages(1, "TTBR1 L3 (split)");
                    if (!split_table)
                        return NULL;
                }
            }
            else
            {
                if (!user_pool) return NULL;
                split_table = alloc_user_l3_from_flat_pool(l0_slot);
                if (!split_table)
                    return NULL;
            }

            RtlZeroMemory(split_table, PAGE_SIZE);
            for (ULONG i = 0; i < 512; ++i)
            {
                UINT64 pa = block_base + ((UINT64)i << 12);
                split_table[i] = pa | PTE_TYPE_VALID | PTE_TYPE_PAGE | block_attrs;
            }

            UINT64 va_base = va & ~ARM64_BLOCK_MASK_2M;
            pte_replace_break_before_make(&l2_table[l2_index],
                                          phys_from_ptr(split_table) | PTE_TABLE_ATTRS,
                                          va_base,
                                          ARM64_BLOCK_SIZE_2M);
            return split_table;
        }

        TRACE("ARM64: ensure_l3_table unexpected descriptor L2[%llu]=0x%llx\n",
              (unsigned long long)l2_index,
              (unsigned long long)entry);
        return NULL;
    }

    /* Entry is not valid - allocate a new L3 table */
    if (is_kernel)
    {
        if (kernel_pool)
        {
            UINT64 *new_table = alloc_kernel_l3_from_flat_pool(l0_slot);
            if (!new_table)
                return NULL;
            RtlZeroMemory(new_table, PAGE_SIZE);
            pte_write(&l2_table[l2_index],
                      (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
            return new_table;
        }
        else if (extra_pool)
        {
            /* Use pre-allocated extra L3 tables with FLAT pool allocation */
            UINT64 *new_table = alloc_extra_l3_from_flat_pool(extra_slot);
            if (!new_table)
                return NULL;
            RtlZeroMemory(new_table, PAGE_SIZE);
            pte_write(&l2_table[l2_index],
                      (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
            return new_table;
        }
        else
        {
            /* Fallback to dynamic allocation */
            UINT64 *new_table = allocate_pt_pages(1, "TTBR1 L3 (extra)");
            if (!new_table)
                return NULL;
            RtlZeroMemory(new_table, PAGE_SIZE);
            pte_write(&l2_table[l2_index],
                      (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
            return new_table;
        }
    }

    /* User space path - use flat pool allocation */
    if (!user_pool) {
        UartPuts("[L3] ensure_l3_table: not user_pool\n");
        return NULL;
    }

    UINT64 *new_table = alloc_user_l3_from_flat_pool(l0_slot);
    if (!new_table)
        return NULL;
    RtlZeroMemory(new_table, PAGE_SIZE);
    pte_write(&l2_table[l2_index],
              (phys_from_ptr(new_table) | PTE_TABLE_ATTRS));
    return new_table;
}

/* ---------- Self-map helper functions ---------- */

static BOOLEAN Arm64SetupSelfMapWindows(VOID)
{
    const UINT64 self_idx = (ARM64_SELF_PXE_BASE >> ARM64_PXI_SHIFT) & ARM64_PX_MASK;
    const UINT64 root_pa = phys_from_ptr(arm64_kernel_l0_table);
    const UINT64 desired = root_pa | PTE_SELFREF_ATTRS;
    UINT64 current = arm64_kernel_l0_table[self_idx];

    if (current != desired)
    {
        pte_write(&arm64_kernel_l0_table[self_idx], desired);
    }

    tlbi_vaae1is_by_va(ARM64_SELF_PXE_BASE);
    tlbi_vaae1is_by_va(ARM64_SELF_PPE_BASE);
    tlbi_vaae1is_by_va(ARM64_SELF_PDE_BASE);
    tlbi_vaae1is_by_va(ARM64_SELF_PTE_BASE);
    ARM64_DSB_ISH();
    ARM64_ISB();

    return TRUE;
}

static inline VOID arm64_icache_sync_range(UINT64 start, UINT64 end)
{
    if (end <= start)
        return;

    if (mmu_enabled)
    {
        Arm64InvalidateInstructionCacheRange(start, end);
        return;
    }

    __asm__ volatile("ic iallu" ::: "memory");
    ARM64_DSB_ISH();
    ARM64_ISB();
}

/* ---------- Page-table construction ---------- */

static BOOLEAN map_region_hierarchical(UINT64 va, UINT64 pa, UINT64 size, UINT64 attrs)
{
    UINT64 end = va + size;
    const UINT64 self_idx = (ARM64_SELF_PXE_BASE >> ARM64_PXI_SHIFT) & ARM64_PX_MASK;
    BOOLEAN executable = ((attrs & (PTE_BLOCK_PXN | PTE_BLOCK_UXN)) !=
                          (PTE_BLOCK_PXN | PTE_BLOCK_UXN));
    BOOLEAN tlbi_needed = FALSE;
    UINT64 flush_start, flush_end;

    /* Align to 4K */
    va &= ~(PAGE_SIZE - 1);
    pa &= ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    flush_start = va;
    flush_end = end;

    while (va < end)
    {
        UINT64 l0_idx = (va >> 39) & 0x1FF;
        UINT64 l1_idx = (va >> 30) & 0x1FF;
        UINT64 l2_idx = (va >> 21) & 0x1FF;
        UINT64 l3_idx = (va >> 12) & 0x1FF;
        UINT64 *l0_table, *l1_table;
        BOOLEAN is_kernel = (va >= ARM64_SYSTEM_RANGE_BASE);
        BOOLEAN is_phys_map = (l0_idx == ARM64_PHYS_MAP_L0_INDEX);

        /* Never allow generic mappings in the self-map L0 slot. */
        if (is_kernel && l0_idx == self_idx)
        {
            ERR("ARM64: Refusing to map VA 0x%llx in self-map L0 slot %llu\n",
                (unsigned long long)va,
                (unsigned long long)l0_idx);
            return FALSE;
        }

        if (is_kernel) {
            l0_table = arm64_kernel_l0_table;
        } else {
            l0_table = arm64_l0_page_table;
            if (l0_idx >= ARM64_USER_L1_TABLES)
            {
                UartPuts("[MAP] User L0 index out of range\n");
                return FALSE;
            }
        }

        if (is_kernel) {
            BOOLEAN in_pool = (l0_idx >= ARM64_KSEG0_L0_INDEX) &&
                              (l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES));
            if (in_pool) {
                UINT64 slot = l0_idx - ARM64_KSEG0_L0_INDEX;
                if (!DESC_VALID(l0_table[l0_idx])) {
                    pte_write(&l0_table[l0_idx],
                              (phys_from_ptr(&arm64_kernel_l1_tables[slot][0]) | PTE_TABLE_ATTRS));
                }
                /* Use direct pointer for in-pool tables - safe before MMU enabled */
                l1_table = arm64_kernel_l1_tables[slot];
            } else {
                if (!DESC_VALID(l0_table[l0_idx])) {
                    UINT64 *new_l1 = allocate_pt_pages(1, "TTBR1 L1 (extra)");
                    if (!new_l1) {
                        UartPuts("[MAP] allocate_pt_pages failed for extra L1\n");
                        return FALSE;
                    }
                    RtlZeroMemory(new_l1, PAGE_SIZE);
                    pte_write(&l0_table[l0_idx],
                              (phys_from_ptr(new_l1) | PTE_TABLE_ATTRS));
                    /* Use the pointer we just allocated */
                    l1_table = new_l1;
                } else {
                    /*
                     * Entry already valid but not in pool - must have been
                     * allocated earlier. PA_TO_VA is identity, works under UEFI
                     * identity mapping for allocated memory.
                     */
                    l1_table = (UINT64 *)PA_TO_VA(l0_table[l0_idx] & PTE_ADDR_MASK);
                }
            }
        } else {
            if (!DESC_VALID(l0_table[l0_idx])) {
                pte_write(&l0_table[l0_idx],
                          (phys_from_ptr(&arm64_l1_page_tables[l0_idx][0]) | PTE_TABLE_ATTRS));
            }
            l1_table = arm64_l1_page_tables[l0_idx];
        }

        /*
         * Block mappings policy:
         *
         * For ordinary kernel space (TTBR1): Always use 4KB pages. The self-map
         * at L0[457] requires all intermediate entries to be TABLE descriptors
         * to allow PTE access via the recursive self-map structure.
         *
         * The private physical alias is not part of the public NT self-map
         * contract, so it can use 2MB blocks for a fast direct map.
         *
         * For identity mapping (TTBR0): Use 2MB blocks for efficiency. The
         * identity mapping is ONLY used during early boot before the kernel
         * switches to its own page tables. Page fault handling for user
         * processes is not a concern because:
         * 1. User processes don't exist during boot
         * 2. The kernel will create its own page tables for user processes
         * 3. This identity mapping is discarded after boot completes
         *
         * Using 2MB blocks reduces mapping time from millions of iterations
         * to thousands, making boot feasible within reasonable time.
         */
        UINT64 remaining = end - va;

        /* 2MiB block if possible (identity mapping or private physical alias). */
        if ((!is_kernel || is_phys_map) &&
            (va & 0x1FFFFFULL) == 0 && (pa & 0x1FFFFFULL) == 0 && remaining >= 0x200000ULL)
        {
            BOOLEAN in_pool = is_kernel && (l0_idx >= ARM64_KSEG0_L0_INDEX) &&
                              (l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES));
            UINT64 l0_slot = is_kernel ? (in_pool ? (l0_idx - ARM64_KSEG0_L0_INDEX) : l0_idx) : l0_idx;
            UINT64 *l2_table_ptr = ensure_l2_table(is_kernel, l0_slot, l1_table, l1_idx, va);
            if (!l2_table_ptr) {
                UartPuts("[MAP] ensure_l2_table FAILED (2MB block path)\n");
                return FALSE;
            }

            if (!DESC_IS_TABLE(l2_table_ptr[l2_idx])) {
                pte_replace_break_before_make(&l2_table_ptr[l2_idx],
                                              pa | PTE_TYPE_VALID | PTE_TYPE_BLOCK | attrs,
                                              va,
                                              ARM64_BLOCK_SIZE_2M);
                tlbi_va_entry(va, ARM64_BLOCK_SIZE_2M);
                tlbi_needed = TRUE;
                va += 0x200000ULL;
                pa += 0x200000ULL;
                continue;
            }
        }

        /* 4KiB page */
        {
            BOOLEAN in_pool = is_kernel && (l0_idx >= ARM64_KSEG0_L0_INDEX) &&
                              (l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES));
            UINT64 l0_slot = is_kernel ? (in_pool ? (l0_idx - ARM64_KSEG0_L0_INDEX) : l0_idx) : l0_idx;

            UINT64 *l2_table_ptr = ensure_l2_table(is_kernel, l0_slot, l1_table, l1_idx, va);
            if (!l2_table_ptr) {
                char buf[256];
                RtlStringCbPrintfA(buf, sizeof(buf),
                    "[MAP] ensure_l2_table FAILED l0_slot=%llu l1_idx=%llu\n",
                    (unsigned long long)l0_slot,
                    (unsigned long long)l1_idx);
                UartPuts(buf);
                return FALSE;
            }

            UINT64 *l3_table_ptr = ensure_l3_table(is_kernel, l0_slot, l2_table_ptr, l2_idx, va);
            if (!l3_table_ptr) {
                char buf[256];
                RtlStringCbPrintfA(buf, sizeof(buf),
                    "[MAP] ensure_l3_table FAILED l0_slot=%llu l2_idx=%llu flat_should_be=%llu\n",
                    (unsigned long long)l0_slot,
                    (unsigned long long)l2_idx,
                    (unsigned long long)(l1_idx * 512 + l2_idx));
                UartPuts(buf);
                return FALSE;
            }

            pte_replace_break_before_make(&l3_table_ptr[l3_idx],
                                          (pa & ~0xFFFULL) | PTE_TYPE_VALID | PTE_TYPE_PAGE | attrs,
                                          va,
                                          PAGE_SIZE);
            tlbi_va_entry(va, PAGE_SIZE);
            tlbi_needed = TRUE;
        }

        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }

    /* Ensure all issued TLB invalidations are complete. */
    if (tlbi_needed)
    {
        ARM64_DSB_ISH();
        ARM64_ISB();
        if (executable)
        {
            /* I-cache maintenance only when execution is permitted. */
            arm64_icache_sync_range(flush_start, flush_end);
        }
    }
    return TRUE;
}

static BOOLEAN
Arm64MapEarlyUart(VOID)
{
    UINT64 uart_pa = EarlyUartBaseAddress;
    UINT64 uart_attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_DEVICE_nGnRnE) |
                        PTE_BLOCK_OUTER_SHARE |
                        PTE_BLOCK_AF |
                        PTE_BLOCK_PXN |
                        PTE_BLOCK_UXN;
    BOOLEAN ok = TRUE;

    /* No UART discovered: nothing to map. EarlyUartReady() will return FALSE
     * and EarlyUartPutc/Puts no-op, so the missing mapping is never used. */
    if (uart_pa == 0)
        return TRUE;

    uart_pa &= ~(PAGE_SIZE - 1ULL);

    if (!map_region_hierarchical(uart_pa, uart_pa, PAGE_SIZE, uart_attrs))
    {
        UartPuts("[PT] FAILED to map early UART identity\n");
        ok = FALSE;
    }

    if (!map_region_hierarchical(ARM64_PHYS_MAP_BASE | uart_pa, uart_pa, PAGE_SIZE, uart_attrs))
    {
        UartPuts("[PT] FAILED to map early UART physical alias\n");
        ok = FALSE;
    }

    ARM64_DSB_ISHST();
    ARM64_ISB();
    return ok;
}

static VOID
Arm64MappingPlanInit(
    PARM64_MAPPING_PLAN Plan,
    FREELDR_MEMORY_DESCRIPTOR *Map,
    ULONG Count)
{
    Plan->MemoryMap = Map;
    Plan->DescriptorCount = Count;
    Plan->RequestCount = 0;
}

static BOOLEAN
Arm64DescriptorIsExecutable(
    const FREELDR_MEMORY_DESCRIPTOR *Descriptor)
{
    switch (Descriptor->MemoryType)
    {
        case LoaderLoadedProgram:
        case LoaderSystemCode:
        case LoaderHalCode:
        case LoaderBootDriver:
            return TRUE;
        default:
            return FALSE;
    }
}

static UINT64
Arm64MemoryAttributesForDescriptor(
    const FREELDR_MEMORY_DESCRIPTOR *Descriptor,
    BOOLEAN IdentityMap)
{
    UINT64 attrs;

    switch (Descriptor->MemoryType)
    {
        case LoaderFirmwarePermanent:
        case LoaderFirmwareTemporary:
            attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_DEVICE_nGnRnE) |
                    PTE_BLOCK_OUTER_SHARE |
                    PTE_BLOCK_AF |
                    PTE_BLOCK_PXN |
                    PTE_BLOCK_UXN;
            break;

        case LoaderFree:
        case LoaderLoadedProgram:
        case LoaderOsloaderHeap:
        case LoaderOsloaderStack:
        case LoaderMemoryData:
        case LoaderRegistryData:
        case LoaderSystemBlock:
        case LoaderSpecialMemory:
        default:
            attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) |
                    PTE_BLOCK_INNER_SHARE |
                    PTE_BLOCK_AF;
            break;
    }

    if (!Arm64DescriptorIsExecutable(Descriptor))
        attrs |= PTE_BLOCK_PXN | PTE_BLOCK_UXN;

    if (IdentityMap)
        attrs |= PTE_BLOCK_NG;

    return attrs;
}

static BOOLEAN
Arm64DescriptorMapsInKernel(
    const FREELDR_MEMORY_DESCRIPTOR *Descriptor)
{
    switch (Descriptor->MemoryType)
    {
        case LoaderExceptionBlock:
        case LoaderSystemBlock:
        case LoaderSystemCode:
        case LoaderHalCode:
        case LoaderBootDriver:
        case LoaderConsoleInDriver:
        case LoaderConsoleOutDriver:
        case LoaderStartupDpcStack:
        case LoaderStartupKernelStack:
        case LoaderStartupPanicStack:
        case LoaderStartupPcrPage:
        case LoaderStartupPdrPage:
        case LoaderRegistryData:
        case LoaderMemoryData:
        case LoaderNlsData:
        case LoaderXIPRom:
        case LoaderOsloaderHeap:
            return TRUE;

        default:
            return FALSE;
    }
}

static BOOLEAN
Arm64DescriptorIsSystemMemory(
    const FREELDR_MEMORY_DESCRIPTOR *Descriptor)
{
    switch (Descriptor->MemoryType)
    {
        case LoaderBad:
            return FALSE;
        default:
            return TRUE;
    }
}

static VOID
Arm64DeriveReservationHints(
    const FREELDR_MEMORY_DESCRIPTOR *MemoryMap,
    ULONG DescriptorCount,
    PARM64_RESERVATION_HINTS Hints)
{
    RtlZeroMemory(Hints, sizeof(*Hints));

    if (!MemoryMap || DescriptorCount == 0)
        return;

    ULONGLONG highest = 0;
    ULONGLONG total = 0;

    for (ULONG i = 0; i < DescriptorCount; ++i)
    {
        const FREELDR_MEMORY_DESCRIPTOR *Descriptor = &MemoryMap[i];
        if (!Arm64DescriptorIsSystemMemory(Descriptor))
            continue;

        ULONGLONG base = Descriptor->BasePage;
        ULONGLONG count = Descriptor->PageCount;
        if (count == 0)
            continue;

        ULONGLONG end = base + count - 1ULL;
        if (end > highest)
            highest = end;
        total += count;
    }

    Hints->HighestPage = highest;
    Hints->TotalPages = total;

    if (total == 0)
        return;

    ULONGLONG secondaryColors = ARM64_DEFAULT_SECONDARY_COLORS;
    ULONGLONG pfnBytes = (highest + 1ULL) * ARM64_SIZEOF_MMPFN;
    pfnBytes += (secondaryColors * ARM64_SIZEOF_MMCOLOR_TABLES * 2ULL);
    ULONGLONG pfnPages = (pfnBytes + PAGE_SIZE - 1ULL) >> PAGE_SHIFT;
    pfnPages += 1ULL;
    Hints->PfnBytes = ARM64_ALIGN_UP(pfnPages << PAGE_SHIFT, ARM64_BLOCK_SIZE_2M);

    ULONGLONG extraPages = (total > 1024ULL) ? (total - 1024ULL) : 0ULL;
    ULONGLONG nonPagedBytes = ARM64_MINIMUM_NONPAGED_POOL_SIZE +
                               (extraPages / 256ULL) * ARM64_MIN_ADDITION_NONPAGED_PER_MB;

    if (nonPagedBytes > ARM64_MAX_INIT_NONPAGED_POOL_SIZE)
        nonPagedBytes = ARM64_MAX_INIT_NONPAGED_POOL_SIZE;

    if (ARM64_NONPAGED_POOL_PERCENT != 0)
    {
        ULONGLONG percentPages = (total * ARM64_NONPAGED_POOL_PERCENT) / 100ULL;
        ULONGLONG percentBytes = percentPages << PAGE_SHIFT;
        if (percentBytes < nonPagedBytes)
            nonPagedBytes = percentBytes;
    }

    if (nonPagedBytes == 0)
        nonPagedBytes = ARM64_MINIMUM_NONPAGED_POOL_SIZE;

    Hints->NonPagedBytes = ARM64_ALIGN_UP(nonPagedBytes, ARM64_BLOCK_SIZE_2M);
}

static BOOLEAN
Arm64MappingPlanAddMapping(
    PARM64_MAPPING_PLAN Plan,
    ARM64_MAPPING_TARGET Target,
    ULONGLONG VirtualBase,
    ULONGLONG PhysicalBase,
    ULONGLONG Size,
    ULONG Attributes,
    BOOLEAN TableOnly)
{
    if (Size == 0)
        return TRUE;

    if (Plan->RequestCount >= ARM64_PLAN_MAX_REQUESTS)
    {
        ERR("ARM64: Mapping plan request overflow (max %u)\n", ARM64_PLAN_MAX_REQUESTS);
        return FALSE;
    }

    ULONGLONG alignedVa = VirtualBase & ~(PAGE_SIZE - 1ULL);
    ULONGLONG alignedPa = PhysicalBase & ~(PAGE_SIZE - 1ULL);
    ULONGLONG offset = PhysicalBase - alignedPa;
    ULONGLONG alignedSize = Size + offset;
    alignedSize = (alignedSize + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);

    ARM64_MAPPING_REQUEST *Request = &Plan->Requests[Plan->RequestCount++];
    Request->Target = Target;
    Request->VirtualBase = alignedVa;
    Request->PhysicalBase = alignedPa;
    Request->Size = alignedSize;
    Request->Attributes = Attributes;
    Request->TableOnly = TableOnly;

    return TRUE;
}

static BOOLEAN
Arm64MappingPlanEnsureTables(
    PARM64_MAPPING_PLAN Plan,
    ULONGLONG VirtualBase,
    ULONGLONG Size)
{
    return Arm64MappingPlanAddMapping(Plan,
                                      Arm64MappingKernel,
                                      VirtualBase,
                                      0,
                                      Size,
                                      0,
                                      TRUE);
}

static BOOLEAN
Arm64EnsureRangeTables(
    ULONGLONG VirtualBase,
    ULONGLONG Size,
    BOOLEAN KernelSpace)
{
    if (Size == 0)
        return TRUE;

    ULONGLONG Va = VirtualBase & ~(PAGE_SIZE - 1ULL);
    ULONGLONG End = (VirtualBase + Size + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);

    while (Va < End)
    {
        ULONGLONG l0_idx = (Va >> 39) & 0x1FFULL;
        ULONGLONG l1_idx = (Va >> 30) & 0x1FFULL;
        ULONGLONG l2_idx = (Va >> 21) & 0x1FFULL;
        UINT64 *l0_table;

        if (KernelSpace)
        {
            l0_table = arm64_kernel_l0_table;
        }
        else
        {
            if (l0_idx >= ARM64_USER_L1_TABLES)
            {
                ERR("ARM64: ensure tables out of user range for VA=0x%llx\n",
                    (unsigned long long)Va);
                return FALSE;
            }
            l0_table = arm64_l0_page_table;
        }

        UINT64 *l1_table = NULL;
        if (!DESC_VALID(l0_table[l0_idx]))
        {
            if (KernelSpace)
            {
                BOOLEAN in_pool = (l0_idx >= ARM64_KSEG0_L0_INDEX) &&
                                  (l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES));
                if (in_pool)
                {
                    UINT64 slot = l0_idx - ARM64_KSEG0_L0_INDEX;
                    pte_write(&l0_table[l0_idx],
                              phys_from_ptr(&arm64_kernel_l1_tables[slot][0]) | PTE_TABLE_ATTRS);
                    l1_table = arm64_kernel_l1_tables[slot];
                }
                else
                {
                    UINT64 *new_l1 = allocate_pt_pages(1, "TTBR1 L1 (extra)");
                    if (!new_l1)
                        return FALSE;
                    RtlZeroMemory(new_l1, PAGE_SIZE);
                    pte_write(&l0_table[l0_idx],
                              phys_from_ptr(new_l1) | PTE_TABLE_ATTRS);
                    l1_table = new_l1;
                }
            }
            else
            {
                pte_write(&l0_table[l0_idx],
                          phys_from_ptr(&arm64_l1_page_tables[l0_idx][0]) | PTE_TABLE_ATTRS);
                l1_table = arm64_l1_page_tables[l0_idx];
            }
        }
        else if (!DESC_IS_TABLE(l0_table[l0_idx]))
        {
            ERR("ARM64: ensure tables encountered block entry at L0 for VA=0x%llx\n",
                (unsigned long long)Va);
            return FALSE;
        }
        else
        {
            if (KernelSpace)
                l1_table = (UINT64 *)PA_TO_VA(l0_table[l0_idx] & PTE_ADDR_MASK);
            else
                l1_table = arm64_l1_page_tables[l0_idx];
        }

        BOOLEAN in_pool = KernelSpace && (l0_idx >= ARM64_KSEG0_L0_INDEX) &&
                          (l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES));
        UINT64 l0_slot = KernelSpace ? (in_pool ? (l0_idx - ARM64_KSEG0_L0_INDEX) : l0_idx) : l0_idx;

        UINT64 *l2_table_ptr = ensure_l2_table(KernelSpace, l0_slot, l1_table, l1_idx, Va);
        if (!l2_table_ptr) {
            UartPuts("[EnsureRange] ensure_l2_table FAILED\n");
            return FALSE;
        }

        UINT64 *l3_table_ptr = ensure_l3_table(KernelSpace, l0_slot, l2_table_ptr, l2_idx, Va);
        if (!l3_table_ptr) {
            char buf[256];
            RtlStringCbPrintfA(buf, sizeof(buf),
                "[EnsureRange] ensure_l3_table FAILED l0_slot=%llu l2_idx=%llu l1_idx=%llu flat_should_be=%llu\n",
                (unsigned long long)l0_slot,
                (unsigned long long)l2_idx,
                (unsigned long long)l1_idx,
                (unsigned long long)(l1_idx * 512 + l2_idx));
            UartPuts(buf);
            return FALSE;
        }

        ULONGLONG block_end = (Va & ~ARM64_BLOCK_MASK_2M) + ARM64_BLOCK_SIZE_2M;
        if (block_end <= Va)
            block_end = Va + PAGE_SIZE;
        if (block_end > End)
            block_end = End;
        Va = block_end;
    }

    return TRUE;
}

static BOOLEAN
Arm64MappingPlanApply(
    ARM64_MAPPING_PLAN *Plan,
    ARM64_MAPPING_TARGET Target)
{
    if (!Plan->MemoryMap || Plan->DescriptorCount == 0)
        return TRUE;

    for (ULONG Index = 0; Index < Plan->DescriptorCount; ++Index)
    {
        const FREELDR_MEMORY_DESCRIPTOR *Descriptor = &Plan->MemoryMap[Index];
        UINT64 PhysicalStart = (UINT64)Descriptor->BasePage * PAGE_SIZE;
        UINT64 Size = (UINT64)Descriptor->PageCount * PAGE_SIZE;
        UINT64 Attributes = Arm64MemoryAttributesForDescriptor(Descriptor,
                                                               Target == Arm64MappingIdentity);
        const UINT64 map_limit = ((UINT64)MM_MAX_PAGE_LOADER_MAPPED << PAGE_SHIFT);

        if (Size == 0)
            continue;

        if (PhysicalStart >= map_limit)
            continue;
        if (PhysicalStart + Size > map_limit)
            Size = map_limit - PhysicalStart;

        if (Target == Arm64MappingIdentity)
        {
            if (!map_region_hierarchical(PhysicalStart, PhysicalStart, Size, Attributes))
            {
                ERR("ARM64: identity map failure PA=0x%llx size=0x%llx type=%u\n",
                    (unsigned long long)PhysicalStart,
                    (unsigned long long)Size,
                    Descriptor->MemoryType);
                return FALSE;
            }
        }
        else if (Target == Arm64MappingKernel)
        {
            if (!Arm64DescriptorMapsInKernel(Descriptor))
                continue;

            UINT64 KernelVa = ARM64_KSEG0_BASE | PhysicalStart;
            if (!Arm64MappingPlanAddMapping(Plan,
                                            Arm64MappingKernel,
                                            KernelVa,
                                            PhysicalStart,
                                            Size,
                                            Attributes,
                                            FALSE))
            {
                return FALSE;
            }
        }
        else if (Target == Arm64MappingPhysicalAlias)
        {
            if (!Arm64DescriptorIsSystemMemory(Descriptor))
                continue;

            Attributes |= PTE_BLOCK_PXN | PTE_BLOCK_UXN;

            if (!Arm64MappingPlanAddMapping(Plan,
                                            Arm64MappingPhysicalAlias,
                                            ARM64_PHYS_MAP_BASE | PhysicalStart,
                                            PhysicalStart,
                                            Size,
                                            Attributes,
                                            FALSE))
            {
                return FALSE;
            }
        }
    }

    for (ULONG ReqIndex = 0; ReqIndex < Plan->RequestCount; ++ReqIndex)
    {
        const ARM64_MAPPING_REQUEST *Request = &Plan->Requests[ReqIndex];
        if (Request->Target != Target)
            continue;

        if (Request->TableOnly)
            continue;

        if (!map_region_hierarchical(Request->VirtualBase,
                                     Request->PhysicalBase,
                                     Request->Size,
                                     Request->Attributes))
        {
            UartPuts("[PLAN] map_region_hierarchical FAILED\n");
            return FALSE;
        }
    }

    /* Process table-only requests after descriptor-driven mappings */
    for (ULONG ReqIndex = 0; ReqIndex < Plan->RequestCount; ++ReqIndex)
    {
        const ARM64_MAPPING_REQUEST *Request = &Plan->Requests[ReqIndex];
        if (Request->Target != Target || !Request->TableOnly)
            continue;

        if (!Arm64EnsureRangeTables(Request->VirtualBase,
                                    Request->Size,
                                    (Target == Arm64MappingKernel)))
        {
            UartPuts("[PLAN] Arm64EnsureRangeTables FAILED\n");
            return FALSE;
        }
    }

    if (Target != Arm64MappingIdentity)
        Plan->RequestCount = 0;

    return TRUE;
}

static BOOLEAN
Arm64UpdateMappingAttributes(ULONGLONG Va, ULONGLONG Size, UINT64 set_mask, UINT64 clear_mask)
{
    if (Size == 0)
        return TRUE;

    ULONGLONG start = ARM64_ALIGN_DOWN(Va, PAGE_SIZE);
    ULONGLONG end = ARM64_ALIGN_UP(Va + Size, PAGE_SIZE);

    while (start < end)
    {
        BOOLEAN kernel_va = (start >= ARM64_SYSTEM_RANGE_BASE);
        UINT64 l0_idx = (start >> 39) & 0x1FF;
        UINT64 l1_idx = (start >> 30) & 0x1FF;
        UINT64 l2_idx = (start >> 21) & 0x1FF;
        UINT64 l3_idx = (start >> 12) & 0x1FF;
        UINT64 *l0_table = kernel_va ? arm64_kernel_l0_table : arm64_l0_page_table;
        UINT64 *l1_table;
        UINT64 l1_entry;

        if (!l0_table)
            return FALSE;

        if (!kernel_va && l0_idx >= ARM64_USER_L1_TABLES)
        {
            start += PAGE_SIZE;
            continue;
        }

        if (!DESC_VALID(l0_table[l0_idx]) || !DESC_IS_TABLE(l0_table[l0_idx]))
        {
            start += PAGE_SIZE;
            continue;
        }

        if (kernel_va)
        {
            BOOLEAN in_pool = (l0_idx >= ARM64_KSEG0_L0_INDEX) &&
                              (l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES));
            if (in_pool)
                l1_table = arm64_kernel_l1_tables[l0_idx - ARM64_KSEG0_L0_INDEX];
            else
                l1_table = (UINT64 *)PA_TO_VA(l0_table[l0_idx] & PTE_ADDR_MASK);
        }
        else
        {
            l1_table = arm64_l1_page_tables[l0_idx];
        }

        l1_entry = l1_table[l1_idx];
        if (!DESC_VALID(l1_entry))
        {
            start += PAGE_SIZE;
            continue;
        }

        BOOLEAN in_pool = kernel_va && (l0_idx >= ARM64_KSEG0_L0_INDEX) &&
                          (l0_idx < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES));
        UINT64 l0_slot = kernel_va ? (in_pool ? (l0_idx - ARM64_KSEG0_L0_INDEX) : l0_idx) : l0_idx;

        UINT64 *l2_table_ptr = ensure_l2_table(kernel_va, l0_slot, l1_table, l1_idx, start);
        if (!l2_table_ptr)
            return FALSE;

        if (!DESC_VALID(l2_table_ptr[l2_idx]))
        {
            start += PAGE_SIZE;
            continue;
        }

        UINT64 *l3_table_ptr = ensure_l3_table(kernel_va, l0_slot, l2_table_ptr, l2_idx, start);
        if (!l3_table_ptr)
            return FALSE;

        UINT64 *pte = &l3_table_ptr[l3_idx];
        if (!DESC_IS_PAGE(*pte))
        {
            start += PAGE_SIZE;
            continue;
        }

        UINT64 newval = (*pte & ~clear_mask) | set_mask;
        if (newval != *pte)
            pte_replace_break_before_make(pte, newval, start, PAGE_SIZE);

        start += PAGE_SIZE;
    }

    return TRUE;
}

static VOID
Arm64ApplySectionAttributes(ULONGLONG SectionBase,
                            ULONGLONG SectionSize,
                            BOOLEAN Executable,
                            BOOLEAN Writable)
{
    UINT64 set_mask = 0;
    UINT64 clear_mask = 0;

    if (Executable)
        clear_mask |= (PTE_BLOCK_PXN | PTE_BLOCK_UXN);
    else
        set_mask |= (PTE_BLOCK_PXN | PTE_BLOCK_UXN);

    if (Writable)
        clear_mask |= PTE_BLOCK_RO;
    else
        set_mask |= PTE_BLOCK_RO;

    Arm64UpdateMappingAttributes(SectionBase, SectionSize, set_mask, clear_mask);

    if (SectionBase < ARM64_KSEG0_BASE)
    {
        Arm64UpdateMappingAttributes(ARM64_KSEG0_BASE | SectionBase,
                                     SectionSize,
                                     set_mask,
                                     clear_mask);
    }
}

static VOID
Arm64ApplyImageSectionProtections(VOID)
{
    for (PLIST_ENTRY entry = FrLdrModuleList.Flink;
         entry != &FrLdrModuleList;
         entry = entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY dte =
            CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        ULONGLONG image_base = (ULONGLONG)(ULONG_PTR)dte->DllBase;

        if (!image_base || dte->SizeOfImage == 0)
            continue;

        PIMAGE_NT_HEADERS nt = RtlImageNtHeader((PVOID)(ULONG_PTR)image_base);
        if (!nt)
            continue;

        ULONG headers_size = nt->OptionalHeader.SizeOfHeaders;
        if (headers_size)
            Arm64ApplySectionAttributes(image_base, headers_size, FALSE, FALSE);

        PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
        for (ULONG i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            ULONG raw_size = section->SizeOfRawData;
            ULONG virt_size = section->Misc.VirtualSize;
            ULONGLONG sec_size = (virt_size > raw_size) ? virt_size : raw_size;
            if (sec_size == 0)
                continue;

            ULONGLONG sec_base = image_base + section->VirtualAddress;
            BOOLEAN exec = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            BOOLEAN write = (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;

            Arm64ApplySectionAttributes(sec_base, sec_size, exec, write);
        }
    }
}

static VOID setup_pgtables(VOID)
{
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;

    if (!arm64_l0_page_table || !arm64_kernel_l0_table || !arm64_l1_page_tables ||
        !arm64_kernel_l1_tables || !arm64_kernel_l2_tables || !arm64_kernel_l3_tables)
    {
        for (;;) __asm__ volatile("wfi");
    }

    RtlZeroMemory(arm64_l0_page_table, ARM64_PT_BYTES);
    RtlZeroMemory(arm64_l1_page_tables, ARM64_USER_L1_TABLES * ARM64_PT_BYTES);
    RtlZeroMemory(arm64_kernel_l0_table, ARM64_PT_BYTES);
    RtlZeroMemory(arm64_kernel_l1_tables, ARM64_KERNEL_L1_TABLES * ARM64_PT_BYTES);

    RtlZeroMemory(arm64_kernel_l2_tables,
                  ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_PT_BYTES);

    RtlZeroMemory(arm64_kernel_l3_tables,
                  ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2
                  * ARM64_PT_BYTES);

    RtlZeroMemory(arm64_user_l2_tables,
                  ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_PT_BYTES);
    RtlZeroMemory(arm64_user_l3_tables,
                  ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2 *
                  ARM64_PT_BYTES);
    RtlZeroMemory(arm64_l2_next_index, sizeof(arm64_l2_next_index));
    RtlZeroMemory(arm64_l3_next_index, sizeof(arm64_l3_next_index));
    RtlZeroMemory(arm64_user_l2_next_index, sizeof(arm64_user_l2_next_index));
    RtlZeroMemory(arm64_user_l3_next_index, sizeof(arm64_user_l3_next_index));

    /* Zero extra kernel slot tables if allocated */
    if (arm64_extra_l2_tables)
    {
        RtlZeroMemory(arm64_extra_l2_tables,
                      ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT * ARM64_PT_BYTES);
    }
    if (arm64_extra_l3_tables)
    {
        RtlZeroMemory(arm64_extra_l3_tables,
                      ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT * ARM64_EXTRA_L3_PER_L2
                      * ARM64_PT_BYTES);
    }
    RtlZeroMemory(arm64_extra_l2_next_index, sizeof(arm64_extra_l2_next_index));
    RtlZeroMemory(arm64_extra_l3_next_index, sizeof(arm64_extra_l3_next_index));

    /* TTBR0 L0 */
    for (i = 0; i < ARM64_USER_L1_TABLES; i++) {
        pte_write(&arm64_l0_page_table[i],
                  (phys_from_ptr(&arm64_l1_page_tables[i][0]) | PTE_TABLE_ATTRS));
    }

    /* TTBR1 L0 for KSEG0 */
    for (i = 0; i < ARM64_KERNEL_L1_TABLES; i++) {
        UINT64 l0_index = ARM64_KSEG0_L0_INDEX + i;
        if (l0_index < ARM64_PT_ENTRIES) {
            pte_write(&arm64_kernel_l0_table[l0_index],
                      (phys_from_ptr(&arm64_kernel_l1_tables[i][0]) | PTE_TABLE_ATTRS));
        }
    }

    /* Seed the recursive self-map so the kernel can access PXE/PPE/PDE/PTE windows. */
    {
        const UINT64 self_idx = (ARM64_SELF_PXE_BASE >> ARM64_PXI_SHIFT) & ARM64_PX_MASK;
        const UINT64 root_pa = phys_from_ptr(arm64_kernel_l0_table);
        /* Self-map entry: points the page table root back to itself. */
        const UINT64 self_entry = root_pa | PTE_SELFREF_ATTRS;

        pte_write(&arm64_kernel_l0_table[self_idx], self_entry);

        if (!Arm64SetupSelfMapWindows())
        {
            ERR("self-map windows setup failed\n");
        }

        {
            UINT64 verify_entry = arm64_kernel_l0_table[self_idx];
            if (verify_entry != self_entry) {
                pte_write(&arm64_kernel_l0_table[self_idx],
                          self_entry);
            }
        }
    }

    /* Pre-seed TTBR1 L0 slots for extra kernel regions using pre-allocated tables.
     * This ensures we don't need Boot Services allocations during kernel mapping.
     * The extra L0 slots (457, 494, 497, 501) map to:
     *   - 457: Self-map (ARM64_SELF_PXE_BASE)
     *   - 494: Hyperspace (ARM64_HYPERSPACE_BASE)
     *   - 497: Paged Pool / Debug mapping
     *   - 501: PFN Database
     */
    {
        for (UINT64 i = 0; i < ARM64_EXTRA_KERNEL_SLOTS; ++i)
        {
            UINT64 idx = arm64_extra_kernel_l0_slots[i];
            UINT64 e = arm64_kernel_l0_table[idx];
            if (!DESC_VALID(e))
            {
                UINT64 *l1_table = arm64_extra_l1_tables[i];
                if (l1_table)
                {
                    RtlZeroMemory(l1_table, PAGE_SIZE);
                    UINT64 desc = phys_from_ptr(l1_table) | PTE_TABLE_ATTRS;
                    pte_write(&arm64_kernel_l0_table[idx], desc);
                }
                else
                {
                    /* Fallback to allocation if pre-allocated table not available */
                    UINT64 *new_l1 = allocate_pt_pages(1, "TTBR1 L1 (extra seed)");
                    if (new_l1)
                    {
                        RtlZeroMemory(new_l1, PAGE_SIZE);
                        UINT64 desc = phys_from_ptr(new_l1) | PTE_TABLE_ATTRS;
                        pte_write(&arm64_kernel_l0_table[idx], desc);
                        arm64_extra_l1_tables[i] = new_l1;
                    }
                    else
                    {
                        ERR("ARM64: Failed to allocate seeded L1 for L0[%llu]\n",
                            (unsigned long long)idx);
                    }
                }
            }
        }
    }

    Arm64MapEarlyUart();

    /*
     * Post-EBS: skip the bulk identity-mapping pre-reservation. UefiMemGetMemoryMap
     * calls back into firmware (HandleProtocol/GetMemoryMap/AllocatePool/AllocatePages)
     * which on this OVMF aborts with a synchronous exception. The per-region
     * Arm64MapVirtualMemory calls done later still walk the page-table tree on
     * demand, so the bulk reservation is an optimization, not a requirement.
     */
    if (BootServicesExitedFlag)
    {
        return;
    }

    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap)
    {
        ARM64_MAPPING_PLAN Plan;
        ARM64_RESERVATION_HINTS Reservation = {0};

        Arm64DeriveReservationHints(MemoryMap, MemoryMapSize, &Reservation);

        ULONGLONG pfnReserveBytes = Reservation.PfnBytes ? Reservation.PfnBytes : ARM64_PFN_DB_RESERVE_SIZE;
        ULONGLONG nonPagedReserveBytes = Reservation.NonPagedBytes ? Reservation.NonPagedBytes : ARM64_NONPAGED_RESERVE_SIZE;

        pfnReserveBytes = ARM64_ALIGN_UP(pfnReserveBytes, ARM64_BLOCK_SIZE_2M);
        nonPagedReserveBytes = ARM64_ALIGN_UP(nonPagedReserveBytes, ARM64_BLOCK_SIZE_2M);

        Arm64MappingPlanInit(&Plan, MemoryMap, MemoryMapSize);

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_HYPERSPACE_BASE,
                                          ARM64_HYPERSPACE_BYTES))
        {
            UartPuts("[PT] FAILED to queue hyperspace\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_DEBUG_MAPPING_BASE,
                                          ARM64_DEBUG_MAPPING_BYTES))
        {
            UartPuts("[PT] FAILED to queue debug mapping\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_PFN_DB_BASE,
                                          pfnReserveBytes))
        {
            UartPuts("[PT] FAILED to queue PFN database\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_PFN_DB_BASE + pfnReserveBytes,
                                          nonPagedReserveBytes))
        {
            UartPuts("[PT] FAILED to queue nonpaged pool\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_PAGED_POOL_BASE,
                                          ARM64_PAGED_POOL_INIT_BYTES))
        {
            UartPuts("[PT] FAILED to queue paged pool\n");
            return;
        }

        if (!Arm64MappingPlanApply(&Plan, Arm64MappingIdentity))
        {
            UartPuts("[PT] FAILED to apply identity mapping\n");
            return;
        }

        if (!Arm64MappingPlanApply(&Plan, Arm64MappingKernel))
        {
            UartPuts("[PT] FATAL - Kernel mapping failed\n");
            for (;;)
                __asm__ volatile("wfi");
        }

        if (!Arm64MappingPlanApply(&Plan, Arm64MappingPhysicalAlias))
        {
            UartPuts("[PT] FATAL - Physical alias mapping failed\n");
            for (;;)
                __asm__ volatile("wfi");
        }
    }
}

#ifndef SCTLR_EL1_WXN
#define SCTLR_EL1_WXN (1ULL << 19)
#endif

VOID Arm64EnablePageTables(VOID)
{

    UINT64 tcr;
    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();
    BOOLEAN transition_to_el1 = FALSE;
    UINT64 sctlr = 0;

    if (!page_tables_initialized)
        ensure_page_tables_initialized();
    Arm64ApplyImageSectionProtections();

    /*
     * Windows loaders run EL1.  If firmware returned from ExitBootServices in
     * plain EL2, program the EL1 register set from EL2 first and only then drop
     * with ERET.  Dropping before TTBR/TCR/SCTLR_EL1 are ready faults on RPi5.
     */
    if (el == 2 && !el12)
    {
        transition_to_el1 = TRUE;
        el = 1;
        el12 = FALSE;
    }

    /* Compose TCR after ensuring final EL */
    tcr = get_tcr_for_el(NULL, NULL, el, el12);

    /* 1. Read current SCTLR and Disable MMU (M) and Cache (C) */
    if (el == 1 || el12) {
        if (el12) __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr));
        else      __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    }
    else if (el == 2) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    }


    UINT64 sctlr_off = sctlr & ~(SCTLR_EL1_M | SCTLR_EL1_C);

    /*
     * CRITICAL: Flush data caches before disabling them.
     * On real hardware (e.g., RPi5), cached writes to global state
     * (like page table pointers) may be lost if we disable caches
     * without cleaning. QEMU tends to mask this.
     */
    Arm64FlushDataCacheAll();
    ARM64_DSB_ISH();
    ARM64_ISB();

    if (el == 1 || el12) {
        if (el12) __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr_off) : "memory");
        else      __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_off) : "memory");
    }
    else if (el == 2) {
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr_off) : "memory");
    }
    ARM64_ISB();

    /* 2. Update Translation Registers (MAIR first, then TCR, then TTBRs) */

    /*
     * CRITICAL VALIDATION: Ensure page table pointers are valid.
     * On some platforms (e.g., RPi5), UEFI AllocatePages may return 0 or
     * allocation may fail silently, leaving these pointers NULL.
     */
    if (!arm64_l0_page_table || !arm64_kernel_l0_table)
    {
        /*
         * Emergency fallback: use static storage for ALL page tables.
         * We must initialize ALL page table pointers, not just L0 tables,
         * because setup_pgtables() validates all of them.
         */
        use_static_page_tables();

        /* If still NULL after fallback, we cannot proceed */
        if (!arm64_l0_page_table || !arm64_kernel_l0_table ||
            !arm64_l1_page_tables || !arm64_kernel_l1_tables ||
            !arm64_kernel_l2_tables || !arm64_kernel_l3_tables)
        {
            for (;;) __asm__ volatile("wfi");
        }

        /*
         * Re-run page table setup since we switched to static storage.
         * This is necessary because the original setup_pgtables() may have
         * populated different (now-invalid) page table memory.
         */
        setup_pgtables();
    }

    /* Get final pointers after any potential fallback */
    const VOID *table0_ptr = (const VOID *)(uintptr_t)arm64_l0_page_table;
    const VOID *table1_ptr = (const VOID *)(uintptr_t)arm64_kernel_l0_table;

    UINT64 phys_table0 = phys_from_ptr(table0_ptr);
    UINT64 phys_table1 = phys_from_ptr(table1_ptr);
    UINT64 ttbr1_table_base = arm64_ttbr1_l0_base(phys_table1);
    UINT64 mair = MEMORY_ATTRIBUTES;

    if (el == 1 || el12) {
        if (el12) {
            __asm__ volatile("msr " MAIR_EL12_SYSREG  ", %0" :: "r"(mair)   : "memory");
        } else {
            __asm__ volatile("msr mair_el1, %0" :: "r"(mair)    : "memory");
        }
    }
    else if (el == 2) {
        __asm__ volatile("msr mair_el2, %0"  :: "r"(mair)   : "memory");
    }
    if (el == 1 || el12) {
        if (el12) {
            __asm__ volatile("msr " TCR_EL12_SYSREG   ", %0" :: "r"(tcr)    : "memory");
        } else {
            __asm__ volatile("msr tcr_el1, %0"  :: "r"(tcr)     : "memory");
        }
    }
    else if (el == 2) {
        __asm__ volatile("msr tcr_el2, %0"   :: "r"(tcr)    : "memory");
    }
    if (el == 1 || el12) {
        if (el12) {
            __asm__ volatile("msr " TTBR0_EL12_SYSREG ", %0" :: "r"(phys_table0) : "memory");
            write_ttbr1(TRUE, ttbr1_table_base);
        } else {
            __asm__ volatile("msr ttbr0_el1, %0" :: "r"(phys_table0) : "memory");
            write_ttbr1(FALSE, ttbr1_table_base);
        }
        TLBI_VMALLE1IS();
    }
    else if (el == 2) {
        __asm__ volatile("msr ttbr0_el2, %0" :: "r"(phys_table0) : "memory");
        /* EL2 usually only uses TTBR0 unless E2H is set (handled above).
           If strict EL2 separation is needed, TTBR1 might be ignored or used differently. */
    }

    ARM64_DSB_ISH();
    ARM64_ISB();

    /*
     * ========== CRITICAL: Ensure PC/SP/VBAR regions are identity-mapped ==========
     *
     * On some platforms (e.g., Raspberry Pi 5), the bootloader may be loaded at
     * physical addresses that are not covered by UEFI memory descriptors, or the
     * memory map may not include the exact region where freeldr code resides.
     *
     * This section MUST run BEFORE verification to ensure we don't hang on
     * platforms where the PC is not already mapped by the memory descriptor loop.
     *
     * We map the critical regions:
     * 1. Current PC (where we're executing from) - 2MB aligned block + next block
     * 2. Stack pointer region - 2MB aligned block
     * 3. Exception vector region - 2MB aligned block
     */
    {
        UINT64 pc_val, sp_val;
        extern UINT8 arm64_exception_vectors[];
        UINT64 vbar = (UINT64)(uintptr_t)arm64_exception_vectors;

        /* Standard attributes for code/data: Normal WB, Inner Shareable, AF set */
        UINT64 code_attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) |
                            PTE_BLOCK_INNER_SHARE | PTE_BLOCK_AF;

        /* Get current PC and SP */
        __asm__ volatile("adr %0, ." : "=r"(pc_val));
        __asm__ volatile("mov %0, sp" : "=r"(sp_val));

        /* Map PC region (2MB aligned) */
        {
            UINT64 pc_2mb_base = pc_val & ~((1ULL << 21) - 1);
            map_region_hierarchical(pc_2mb_base, pc_2mb_base, 2 * 1024 * 1024, code_attrs);
            map_region_hierarchical(ARM64_KSEG0_BASE | pc_2mb_base, pc_2mb_base,
                                    2 * 1024 * 1024, code_attrs);
            /* Also map next 2MB block in case we're near a boundary */
            map_region_hierarchical(pc_2mb_base + (2*1024*1024), pc_2mb_base + (2*1024*1024),
                                    2*1024*1024, code_attrs);
            map_region_hierarchical(ARM64_KSEG0_BASE | (pc_2mb_base + (2*1024*1024)),
                                    pc_2mb_base + (2*1024*1024), 2*1024*1024,
                                    code_attrs);
        }

        /* Map SP region (2MB aligned) */
        {
            UINT64 sp_2mb_base = sp_val & ~((1ULL << 21) - 1);
            if (sp_2mb_base != (pc_val & ~((1ULL << 21) - 1))) {
                map_region_hierarchical(sp_2mb_base, sp_2mb_base, 2 * 1024 * 1024, code_attrs);
                map_region_hierarchical(ARM64_KSEG0_BASE | sp_2mb_base, sp_2mb_base,
                                        2 * 1024 * 1024, code_attrs);
            }
        }

        /* Map VBAR region (2MB aligned) */
        {
            UINT64 vbar_2mb_base = vbar & ~((1ULL << 21) - 1);
            UINT64 pc_2mb_base = pc_val & ~((1ULL << 21) - 1);
            UINT64 sp_2mb_base = sp_val & ~((1ULL << 21) - 1);
            if (vbar_2mb_base != pc_2mb_base && vbar_2mb_base != sp_2mb_base) {
                map_region_hierarchical(vbar_2mb_base, vbar_2mb_base, 2 * 1024 * 1024, code_attrs);
                map_region_hierarchical(ARM64_KSEG0_BASE | vbar_2mb_base, vbar_2mb_base,
                                        2 * 1024 * 1024, code_attrs);
            }
        }

        Arm64MapEarlyUart();
    }
    /* ========== END CRITICAL REGION MAPPING ========== */

    if (transition_to_el1)
    {
        Arm64EnsureEl1EntryState();
        if (get_effective_el() != 1)
        {
            return;
        }
    }

    /* 3. Re-enable MMU and Cache (Original SCTLR flags) */
    /* Ensure M and C are set.
       IMPORTANT: Clear WXN (Write-Execute-Never) to allow loader execution (which is mapped R/W) */
    UINT64 sctlr_on = (sctlr | SCTLR_EL1_M | SCTLR_EL1_C) & ~SCTLR_EL1_WXN;

    /*
     * CRITICAL: Clean and invalidate all data caches before enabling MMU.
     * The page table walker reads from memory (or its own cache), not from
     * the L1/L2 data caches. We must ensure all page table writes are visible
     * to the translation table walker.
     */
    __asm_dcache_all(0);  /* 0 = clean & invalidate */
    ARM64_DSB_ISH();

    if (el == 1 || el12) {
        if (el12) {
            __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr_on) : "memory");
        } else {
            __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_on) : "memory");
        }
    }
    else if (el == 2) {
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr_on) : "memory");
    }
    ARM64_ISB();

}

static VOID ensure_page_tables_initialized(VOID)
{
    if (page_tables_initialized)
    {
        return;
    }

    if (!allocate_page_table_memory())
    {
        ERR("ARM64: Page table allocation failed\n");
        UartPuts("ARM64: FATAL unable to allocate page table arena\n");
        for (;;)
            __asm__ volatile("wfi");
    }

    setup_pgtables();
    page_tables_initialized = TRUE;
}

/* ---------- Public MMU control ---------- */

VOID Arm64PreparePageTables(VOID)
{
    ensure_page_tables_initialized();
}

VOID Arm64InitializeMMU(VOID)
{
    UINT64 tcr, sctlr;
    UINT64 ips, va_bits;
    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();

    TRACE("ARM64: Initializing MMU (EL%d)\n", el);

    ensure_page_tables_initialized();

    tcr = get_tcr(&ips, &va_bits);
    if (!tcr) {
        ERR("ARM64: TCR composition failed (granule unsupported?)\n");
        return;
    }

    TRACE("ARM64: Using %llu-bit VA, IPS=%llu\n", va_bits, ips);

    set_ttbr_tcr_mair(el,
                      (UINT64)arm64_l0_page_table,
                      (UINT64)arm64_kernel_l0_table,
                      tcr,
                      MEMORY_ATTRIBUTES);

    /* Enable MMU + caches */
    if (el == 1 || el12) {
        if (el12)
            __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr));
        else
            __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

        /*
         * Do not inherit firmware SCTLR_EL1.A. Windows/NT code routinely uses
         * ordinary unaligned data accesses; only SP alignment checking stays on.
         */
        sctlr &= ~SCTLR_EL1_A;
        sctlr |= SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I | SCTLR_EL1_SA;

        if (el12)
            __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr) : "memory");
        else
            __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
    } else if (el == 2) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
        sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr) : "memory");
    } else if (el == 3) {
        __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
        sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr) : "memory");
    }
    ARM64_ISB();

    mmu_enabled = TRUE;

    TRACE("ARM64: MMU enabled\n");
}

/* ---------- Mapping API ---------- */

/*
 * Map a virtual range to a physical range.
 * - Requires 4KiB alignment for VA, PA, and Size.
 * - Uses block mappings where possible, otherwise 4KiB pages.
 * - Safe for use during early boot; range-based TLB invalidation and
 *   conditional I-cache maintenance for executable mappings.
 */
BOOLEAN Arm64MapVirtualMemory(ULONGLONG VirtualAddress,
                              ULONGLONG PhysicalAddress,
                              ULONGLONG Size,
                              ULONG Attributes)
{
    UINT64 attrs;
    BOOLEAN executable = (Attributes & ARM64_MAP_ATTR_EXECUTE) != 0;
    ULONG mem_type = Attributes & ARM64_MAP_ATTR_TYPE_MASK;

    ensure_page_tables_initialized();

    if (((VirtualAddress | PhysicalAddress | Size) & (PAGE_SIZE - 1)) != 0)
    {
        TRACE("ARM64: Map requires 4KiB alignment\n");
        return FALSE;
    }

    attrs = PTE_BLOCK_MEMTYPE(mem_type) | PTE_BLOCK_INNER_SHARE | PTE_BLOCK_AF;
    if (!executable)
        attrs |= PTE_BLOCK_PXN | PTE_BLOCK_UXN;
    if (VirtualAddress < ARM64_SYSTEM_RANGE_BASE)
        attrs |= PTE_BLOCK_NG;

    if (!map_region_hierarchical(VirtualAddress, PhysicalAddress, Size, attrs))
    {
        TRACE("ARM64: map_region_hierarchical failed for VA 0x%llx size 0x%llx\n",
              VirtualAddress, Size);
        return FALSE;
    }

    /* map_region_hierarchical already does range-based TLBI + I-cache maintenance. */
    return TRUE;
}

/*
 * Clear the TTBR0 identity mappings just before the jump to the kernel.
 * Called from kernel_jump.S once PC and SP run from KSEG0, so dropping the
 * identity view can no longer kill the loader's final instructions. The NT
 * kernel expects the low half to start out unmapped.
 */
VOID Arm64ClearIdentityMappings(VOID)
{
    ULONG i;

    if (!page_tables_initialized || !mmu_enabled || !arm64_l0_page_table)
    {
        return;
    }

    for (i = 0; i < ARM64_USER_L1_TABLES; i++)
    {
        if (arm64_l0_page_table[i] != 0)
        {
            arm64_l0_page_table[i] = 0;
        }
    }

    ARM64_DSB_ISHST();
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    ARM64_DSB_ISH();
    ARM64_ISB();
}
