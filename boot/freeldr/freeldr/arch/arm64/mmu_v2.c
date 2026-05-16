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

/* Assumed physical layout of kernel and loader; must match build/link config. */
#define ARM64_KERNEL_PHYSICAL_BASE   0x40000000ULL
#define ARM64_KERNEL_PHYSICAL_SIZE   0x10000000ULL /* 256MB */

#define ARM64_LOADER_PHYSICAL_BASE   0x13E000000ULL
#define ARM64_LOADER_PHYSICAL_SIZE   0x02000000ULL /* 32MB */

#define ARM64_ALIGN_UP(value, alignment) \
    (((value) + ((alignment) - 1ULL)) & ~((alignment) - 1ULL))
#define ARM64_ALIGN_DOWN(value, alignment) \
    ((value) & ~((alignment) - 1ULL))

#define ARM64_DEFAULT_SECONDARY_COLORS   64ULL
#define ARM64_MINIMUM_NONPAGED_POOL_SIZE (256ULL * 1024ULL)
#define ARM64_DEFAULT_MAX_NONPAGED_POOL  (1024ULL * 1024ULL)
#define ARM64_MIN_ADDITION_NONPAGED_PER_MB (32ULL * 1024ULL)
#define ARM64_MAX_ADDITION_NONPAGED_PER_MB (400ULL * 1024ULL)
#define ARM64_MAX_INIT_NONPAGED_POOL_SIZE (128ULL * 1024ULL * 1024ULL * 1024ULL)
#define ARM64_MAX_NONPAGED_POOL_SIZE      ARM64_MAX_INIT_NONPAGED_POOL_SIZE
#define ARM64_NONPAGED_POOL_PERCENT       0U

#define ARM64_SIZEOF_MMPFN             0x58ULL /* FIXME: update if sizeof(MMPFN) changes (ntoskrnl/include/internal/mm.h) */
#define ARM64_SIZEOF_MMCOLOR_TABLES    0x18ULL

typedef char arm64_secondary_colors_must_match_nt[
    (ARM64_DEFAULT_SECONDARY_COLORS == 64ULL) ? 1 : -1];

#define ARM64_DEBUG_MAPPING_BYTES  PAGE_SIZE

#define ARM64_PX_MASK         0x1FFULL
#define ARM64_PXI_SHIFT       39

/* UART output using runtime-detected address from early_uart.h */
#include <reactos/arm64/early_uart.h>

static inline VOID UartPuts(const char *String) { EarlyUartPuts(String); }
static inline VOID UartPutHex64(ULONGLONG Value) { EarlyUartPutHex(Value, 16); }
static inline VOID UartPutHex32(ULONG Value) { EarlyUartPutHex((UINT64)Value, 8); }

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
 * (e.g. the OVMF build in this user's harness) unmap the BootServices code
 * pages on EBS, so calling bs->AllocatePages afterwards faults with a
 * synchronous abort at the firmware code address. We treat the flag as a
 * second-level gate alongside the GlobalSystemTable pointer check below.
 */
extern volatile BOOLEAN BootServicesExitedFlag;

/* ---------- Barrier & TLBI helpers (multicore-safe) ---------- */

#define ARM64_DSB_ISH()   __asm__ volatile("dsb ish" ::: "memory")
#define ARM64_DSB_ISHST() __asm__ volatile("dsb ishst" ::: "memory")

/* Global/all-ASID TLBI */
#define TLBI_VMALLE1IS()  __asm__ volatile("tlbi vmalle1is" ::: "memory")
#define TLBI_ALLE2IS()    __asm__ volatile("tlbi alle2is" ::: "memory")
#define TLBI_ALLE3IS()    __asm__ volatile("tlbi alle3is" ::: "memory")

/* Per-VA, all ASIDs (EL1&0) */
static inline void tlbi_vaae1is_by_va(ULONGLONG va)
{
    __asm__ volatile("tlbi vaae1is, %0" :: "r"(va >> 12) : "memory");
}

static inline void tlbi_va_all_levels(ULONGLONG va)
{
    /* Invalidate TLB entries for this VA at all levels */
    tlbi_vaae1is_by_va(va);
}

static inline void tlbi_va_entry(ULONGLONG va, ULONGLONG size)
{
    if ((size >= ARM64_BLOCK_SIZE_1G) && ((va & ARM64_BLOCK_MASK_1G) == 0))
    {
        tlbi_vaae1is_by_va(va);
        return;
    }
    if ((size >= ARM64_BLOCK_SIZE_2M) && ((va & ARM64_BLOCK_MASK_2M) == 0))
    {
        tlbi_vaae1is_by_va(va);
        return;
    }
    tlbi_vaae1is_by_va(va);
}

static inline void tlbi_va_range(ULONGLONG start, ULONGLONG end)
{
    if (end <= start)
        return;

    ULONGLONG va = start & ~(PAGE_SIZE - 1);
    ULONGLONG finish = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (; va < finish; va += PAGE_SIZE)
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

/* ARM64_BLOCK_SIZE_* fallbacks moved above TLBI helpers. */

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
/* Enable hardware update of the Access flag so AF=0 entries do not fault */
#define TCR_HA                  (1ULL << 39)
/* #define TCR_HD               (1ULL << 40) */
#define TCR_TBI1                (1ULL << 38)
#define TCR_TBID0               (1ULL << 51)
#define TCR_TBID1               (1ULL << 52)
#define TCR_A1                  (1ULL << 22)
#define TCR_EPD0                (1ULL << 7)
#define TCR_EPD1                (1ULL << 23)

#define TCR_EL1_RSVD            0ULL
#define TCR_EL2_RSVD            0ULL
#define TCR_EL3_RSVD            0ULL

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
static BOOLEAN identity_mapping_enabled = FALSE;
static BOOLEAN page_tables_initialized = FALSE;

#define ARM64_KSEG0_L0_INDEX      (((ULONGLONG)ARM64_KSEG0_BASE >> 39) & 0x1FFULL)
#define ARM64_SYSTEM_L0_INDEX     (((ULONGLONG)ARM64_SYSTEM_RANGE_BASE >> 39) & 0x1FFULL)
#define ARM64_PHYS_MAP_L0_INDEX   (((ULONGLONG)ARM64_PHYS_MAP_BASE >> 39) & 0x1FFULL)
#define ARM64_KERNEL_L1_TABLES    4U
/* 8 L1 tables to cover 4TB user address range for high physical addresses */
#define ARM64_USER_L1_TABLES      8U
#define ARM64_KUSER_SHARED_L0_INDEX (((ULONGLONG)KI_USER_SHARED_DATA >> 39) & 0x1FFULL)

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

static UINT64                *arm64_kuser_l1_table;
static UINT64                *arm64_kuser_l2_table;
static UINT64                *arm64_kuser_l3_table;

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

/* Dedicated tables for KUSER shared page mappings */
static BOOLEAN mmu_enabled = FALSE;
static BOOLEAN page_table_memory_allocated = FALSE;

/* Cache computed address-space parameters so we can avoid UEFI calls post-EBS */
static BOOLEAN tcr_limits_cached = FALSE;
static UINT64 cached_max_physical_address = 0x100000000ULL;

#ifdef UEFIBOOT
/* Provided by the UEFI memory helper. Reflects the actual loader placement. */
extern PVOID OsLoaderBase;
extern SIZE_T OsLoaderSize;
#endif

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
static UINT8 arm64_kuser_l1_table_storage_raw[ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_kuser_l2_table_storage_raw[ARM64_PT_BYTES + PAGE_SIZE]
    __attribute__((aligned(4096)));
static UINT8 arm64_kuser_l3_table_storage_raw[ARM64_PT_BYTES + PAGE_SIZE]
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
static int get_effective_el(VOID);
static BOOLEAN use_el12_registers(VOID);
static VOID setup_pgtables(VOID);
static VOID ensure_page_tables_initialized(VOID);
static VOID set_ttbr_tcr_mair(int el, UINT64 table0, UINT64 table1, UINT64 tcr, UINT64 attr);
static VOID debug_dump_static_mapping(UINT64 va);
static BOOLEAN Arm64MapPageTableAllocationsIntoKseg0(VOID);
static BOOLEAN Arm64MapEarlyUart(VOID);

static BOOLEAN map_region_hierarchical(UINT64 va, UINT64 pa, UINT64 size, UINT64 attrs);
static UINT64* ensure_l2_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l1_table, UINT64 l1_index, UINT64 va);
static UINT64* ensure_l3_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table, UINT64 l2_index, UINT64 va);
static UINT64 get_l2_slot_index(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table);
static BOOLEAN Arm64UpdateMappingAttributes(ULONGLONG Va, ULONGLONG Size, UINT64 set_mask, UINT64 clear_mask);
static VOID Arm64ApplyImageSectionProtections(VOID);

/* PL011 UART debugging helpers - enabled for page table debugging */
static inline VOID Pl011RawPutc(char Ch) { EarlyUartPutc(Ch); }
static inline VOID Pl011RawPuts(const char *S) { EarlyUartPuts(S); }

#define ARM64_PT_VERBOSE 0
#define ARM64_PT_LOG(S) do { if (ARM64_PT_VERBOSE) Pl011RawPuts(S); } while (0)

/* NEW: public functions used before they are defined later in this file */
BOOLEAN Arm64MapVirtualMemory(ULONGLONG VirtualAddress,
                              ULONGLONG PhysicalAddress,
                              ULONGLONG Size,
                              ULONG Attributes);

VOID Arm64DebugDumpMapping(UINT64 VirtualAddress);

static BOOLEAN
Arm64TranslateKernelKseg0Va(UINT64 Va, UINT64 *PhysicalAddress)
{
    UINT64 l0, l1, l2, l3;
    UINT64 entry;
    UINT64 *l1_table;
    UINT64 *l2_table;
    UINT64 *l3_table;

    if (PhysicalAddress == NULL)
        return FALSE;

    if (Va < ARM64_SYSTEM_RANGE_BASE)
        return FALSE;

    l0 = (Va >> 39) & ARM64_PX_MASK;
    if (!DESC_IS_TABLE(arm64_kernel_l0_table[l0]))
    {
        return FALSE;
    }

    entry = arm64_kernel_l0_table[l0];
    if (!DESC_IS_TABLE(entry))
        return FALSE;

    l1_table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);
    l1 = (Va >> 30) & ARM64_PX_MASK;
    entry = l1_table[l1];
    if (DESC_IS_BLOCK(entry))
    {
        *PhysicalAddress = (entry & PTE_BLOCK_ADDR_MASK_1G) |
                           (Va & ARM64_BLOCK_MASK_1G);
        return TRUE;
    }
    if (!DESC_IS_TABLE(entry))
        return FALSE;

    l2_table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);
    l2 = (Va >> 21) & ARM64_PX_MASK;
    entry = l2_table[l2];
    if (DESC_IS_BLOCK(entry))
    {
        *PhysicalAddress = (entry & PTE_BLOCK_ADDR_MASK_2M) |
                           (Va & ARM64_BLOCK_MASK_2M);
        return TRUE;
    }
    if (!DESC_IS_TABLE(entry))
        return FALSE;

    l3_table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);
    l3 = (Va >> 12) & ARM64_PX_MASK;
    entry = l3_table[l3];
    if (!DESC_IS_PAGE(entry))
        return FALSE;

    *PhysicalAddress = (entry & PTE_ADDR_MASK) | (Va & 0xFFFULL);
    return TRUE;
}

static BOOLEAN
Arm64VerifyKseg0PhysicalPage(UINT64 PhysicalAddress, const char *Label)
{
    CHAR Buffer[192];
    UINT64 Pa = PhysicalAddress & ~(UINT64)(PAGE_SIZE - 1ULL);
    UINT64 Va = ARM64_PHYS_MAP_BASE | Pa;
    UINT64 Translated;

    if (Arm64TranslateKernelKseg0Va(Va, &Translated) &&
        ((Translated & ~(UINT64)(PAGE_SIZE - 1ULL)) == Pa))
    {
        return TRUE;
    }

    RtlStringCbPrintfA(Buffer,
                       sizeof(Buffer),
                       "[PT] KSEG0 mapping missing: %s pa=0x%llx va=0x%llx\n",
                       Label ? Label : "?",
                       (unsigned long long)Pa,
                       (unsigned long long)Va);
    Pl011RawPuts(Buffer);
    debug_dump_static_mapping(Va);
    return FALSE;
}

static BOOLEAN
Arm64VerifyKseg0HardwarePage(UINT64 PhysicalAddress, const char *Label)
{
    CHAR Buffer[224];
    UINT64 Pa = PhysicalAddress & ~(UINT64)(PAGE_SIZE - 1ULL);
    UINT64 Va = ARM64_PHYS_MAP_BASE | Pa;
    UINT64 Par;
    UINT64 Translated;

    __asm__ volatile("at s1e1r, %0" :: "r"(Va) : "memory");
    ARM64_ISB();
    __asm__ volatile("mrs %0, par_el1" : "=r"(Par));

    if ((Par & 1ULL) != 0)
    {
        RtlStringCbPrintfA(Buffer,
                           sizeof(Buffer),
                           "[PT] hardware translation fault: %s va=0x%llx par=0x%llx\n",
                           Label ? Label : "?",
                           (unsigned long long)Va,
                           (unsigned long long)Par);
        Pl011RawPuts(Buffer);
        debug_dump_static_mapping(Va);
        return FALSE;
    }

    Translated = (Par & 0x0000FFFFFFFFF000ULL) | (Va & 0xFFFULL);
    if ((Translated & ~(UINT64)(PAGE_SIZE - 1ULL)) == Pa)
        return TRUE;

    RtlStringCbPrintfA(Buffer,
                       sizeof(Buffer),
                       "[PT] hardware translation mismatch: %s va=0x%llx pa=0x%llx par=0x%llx\n",
                       Label ? Label : "?",
                       (unsigned long long)Va,
                       (unsigned long long)Pa,
                       (unsigned long long)Par);
    Pl011RawPuts(Buffer);
    debug_dump_static_mapping(Va);
    return FALSE;
}

static BOOLEAN
Arm64MapPhysicalRangeIntoKseg0(UINT64 PhysicalAddress, UINT64 Pages, UINT64 Attrs)
{
    UINT64 pa;
    UINT64 va;
    UINT64 size;

    if (Pages == 0)
        return TRUE;

    pa = PhysicalAddress & ~(UINT64)(PAGE_SIZE - 1ULL);
    va = ARM64_PHYS_MAP_BASE | pa;
    size = Pages * PAGE_SIZE;

    return map_region_hierarchical(va, pa, size, Attrs);
}

static BOOLEAN
Arm64MapPageTableRangeIntoKseg0(const VOID *Base, UINT64 Pages, UINT64 Attrs)
{
    if (!Base || Pages == 0)
        return TRUE;

    return Arm64MapPhysicalRangeIntoKseg0((UINT64)(uintptr_t)Base,
                                          Pages,
                                          Attrs);
}

static BOOLEAN
Arm64MapPageTableAllocationsIntoKseg0(VOID)
{
    const UINT64 attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) |
                         PTE_BLOCK_INNER_SHARE |
                         PTE_BLOCK_AF |
                         PTE_BLOCK_PXN |
                         PTE_BLOCK_UXN;

    const struct
    {
        const VOID *Base;
        UINT64 Pages;
    } Ranges[] =
    {
        {arm64_l0_page_table, 1},
        {arm64_kernel_l0_table, 1},
        {arm64_kuser_l1_table, 1},
        {arm64_kuser_l2_table, 1},
        {arm64_kuser_l3_table, 1},
        {arm64_l1_page_tables, ARM64_USER_L1_TABLES},
        {arm64_kernel_l1_tables, ARM64_KERNEL_L1_TABLES},
        {arm64_kernel_l2_tables, ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1},
        {arm64_kernel_l3_tables, ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2},
        {arm64_user_l2_tables, ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1},
        {arm64_user_l3_tables, ARM64_USER_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2},
        {arm64_extra_l1_tables[0], ARM64_EXTRA_KERNEL_SLOTS},
        {arm64_extra_l2_tables, ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT},
        {arm64_extra_l3_tables, ARM64_EXTRA_KERNEL_SLOTS * ARM64_EXTRA_L2_PER_SLOT * ARM64_EXTRA_L3_PER_L2},
        {arm64_static_extra_pt_arena, (arm64_static_extra_pt_offset + PAGE_SIZE - 1ULL) >> PAGE_SHIFT},
    };

    for (UINTN i = 0; i < ARRAYSIZE(Ranges); ++i)
    {
        if (!Arm64MapPageTableRangeIntoKseg0(Ranges[i].Base, Ranges[i].Pages, attrs))
        {
            Pl011RawPuts("[PT] FAILED to map active page-table range into KSEG0\n");
            return FALSE;
        }
    }

    for (UINTN i = 0; i < Arm64PtAllocationCount; ++i)
    {
        const VOID *base = (const VOID *)(uintptr_t)Arm64PtAllocations[i].Base;

        if (!Arm64MapPageTableRangeIntoKseg0(base, Arm64PtAllocations[i].Pages, attrs))
        {
            Pl011RawPuts("[PT] FAILED to map page-table allocation into KSEG0\n");
            return FALSE;
        }
    }

    return TRUE;
}

static VOID use_static_page_tables(VOID)
{
    arm64_l0_page_table = (UINT64 *)align_page_storage(arm64_l0_page_table_storage_raw);
    arm64_kernel_l0_table = (UINT64 *)align_page_storage(arm64_kernel_l0_table_storage_raw);
    arm64_kuser_l1_table = (UINT64 *)align_page_storage(arm64_kuser_l1_table_storage_raw);
    arm64_kuser_l2_table = (UINT64 *)align_page_storage(arm64_kuser_l2_table_storage_raw);
    arm64_kuser_l3_table = (UINT64 *)align_page_storage(arm64_kuser_l3_table_storage_raw);
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
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_kuser_l1_table, 1);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_kuser_l2_table, 1);
    Arm64RecordPageTableAllocation((EFI_PHYSICAL_ADDRESS)(ULONG_PTR)arm64_kuser_l3_table, 1);
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
            {"KUSER_L1", arm64_kuser_l1_table},
            {"KUSER_L2", arm64_kuser_l2_table},
            {"KUSER_L3", arm64_kuser_l3_table},
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
    arm64_kuser_l1_table = allocate_pt_pages(1, "KUSER L1");
    arm64_kuser_l2_table = allocate_pt_pages(1, "KUSER L2");
    arm64_kuser_l3_table = allocate_pt_pages(1, "KUSER L3");

    if (!arm64_l0_page_table || !arm64_kernel_l0_table ||
        !arm64_kuser_l1_table || !arm64_kuser_l2_table || !arm64_kuser_l3_table)
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

/* Prototypes */
static UINT64 get_tcr(UINT64 *pips, UINT64 *pva_bits);
static int get_effective_el(VOID);
static BOOLEAN use_el12_registers(VOID);
static VOID setup_pgtables(VOID);
static VOID ensure_page_tables_initialized(VOID);
static VOID set_ttbr_tcr_mair(int el, UINT64 table0, UINT64 table1, UINT64 tcr, UINT64 attr);
static VOID debug_dump_static_mapping(UINT64 va);

static BOOLEAN map_region_hierarchical(UINT64 va, UINT64 pa, UINT64 size, UINT64 attrs);
static UINT64* ensure_l2_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l1_table, UINT64 l1_index, UINT64 va);
static UINT64* ensure_l3_table(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table, UINT64 l2_index, UINT64 va);
static UINT64 get_l2_slot_index(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table);

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

static UINT64 get_tcr(UINT64 *pips, UINT64 *pva_bits)
{
    query_mmfr0_caps();
    if (!g_granule4k_supported) {
        ERR("ARM64: 4KiB granule not supported on this CPU; MMU setup aborted.\n");
        if (pips) *pips = 0;
        if (pva_bits) *pva_bits = 0;
        return 0;
    }

    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();

    UINT64 max_addr = cached_max_physical_address;
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;
    BOOLEAN boot_services_available = (GlobalSystemTable && GlobalSystemTable->BootServices);

    /* Refresh cached physical limit when possible */
    if (boot_services_available) {
        MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
        if (MemoryMap) {
            max_addr = 0;
            for (i = 0; i < MemoryMapSize; i++) {
                UINT64 end_addr = (UINT64)(MemoryMap[i].BasePage + MemoryMap[i].PageCount) * PAGE_SIZE;
                if (end_addr > max_addr) max_addr = end_addr;
            }
            cached_max_physical_address = max_addr;
            tcr_limits_cached = TRUE;
        } else if (!tcr_limits_cached) {
            max_addr = 0x100000000ULL;
        }
    } else if (!tcr_limits_cached) {
        max_addr = 0x100000000ULL;
    }

    if (!boot_services_available && tcr_limits_cached) {
        max_addr = cached_max_physical_address;
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
    } else if (el == 2) {
        tcr = TCR_EL2_RSVD | (ips_desired << 16);
    } else {
        tcr = TCR_EL3_RSVD | (ips_desired << 16);
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
    } else if (el == 3) {
        __asm__ volatile("msr ttbr0_el3, %0" :: "r"(phys_table0) : "memory");
        __asm__ volatile("msr tcr_el3, %0"   :: "r"(tcr)    : "memory");
        __asm__ volatile("msr mair_el3, %0"  :: "r"(attr)   : "memory");
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

static __attribute__((unused)) UINT64 get_l2_slot_index(BOOLEAN is_kernel, UINT64 l0_slot, UINT64 *l2_table)
{
    if (is_kernel)
    {
        if (l0_slot >= ARM64_KERNEL_L1_TABLES)
            return 0;
        return (UINT64)(l2_table - &arm64_kernel_l2_tables[l0_slot][0][0]) / 512ULL;
    }

    if (l0_slot >= ARM64_USER_L1_TABLES)
        return 0;

    return (UINT64)(l2_table - &arm64_user_l2_tables[l0_slot][0][0]) / 512ULL;
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
        Pl011RawPuts("[L3] kernel L3 seed pool is NULL; allocating spill table\n");
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

    if (!result)
    {
        Pl011RawPuts("[L3] kernel L3 seed entry is NULL; allocating spill table\n");
        return allocate_pt_pages(1, "TTBR1 L3 (kernel null spill)");
    }

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
    if (!result)
    {
        Pl011RawPuts("[L3] user L3 seed entry is NULL; allocating spill table\n");
        return allocate_pt_pages(1, "TTBR0 L3 (user null spill)");
    }

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
    char buf[256];

    if (extra_slot >= ARM64_EXTRA_KERNEL_SLOTS)
    {
        Pl011RawPuts("[L3] FATAL: invalid extra_slot in alloc_extra_l3_from_flat_pool\n");
        return NULL;
    }

    if (!arm64_extra_l3_tables)
    {
        Pl011RawPuts("[L3] extra L3 seed pool is NULL; allocating spill table\n");
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

    if (!result)
    {
        RtlStringCbPrintfA(buf, sizeof(buf),
            "[L3] FATAL: result is NULL from extra array at [%llu][%llu][%llu]\n",
            (unsigned long long)extra_slot,
            (unsigned long long)l2_slot_for_alloc,
            (unsigned long long)idx_in_l2_slot);
        Pl011RawPuts(buf);
        return allocate_pt_pages(1, "TTBR1 L3 (extra null spill)");
    }

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

    /* Debug: check the pool selection */
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
        Pl011RawPuts("[L3] ensure_l3_table: not user_pool\n");
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

    tlbi_va_all_levels(ARM64_SELF_PXE_BASE);
    tlbi_va_all_levels(ARM64_SELF_PPE_BASE);
    tlbi_va_all_levels(ARM64_SELF_PDE_BASE);
    tlbi_va_all_levels(ARM64_SELF_PTE_BASE);
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

    ARM64_PT_LOG("[MAP] map_region_hierarchical: entry\n");

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
                Pl011RawPuts("[MAP] User L0 index out of range\n");
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
                        Pl011RawPuts("[MAP] allocate_pt_pages failed for extra L1\n");
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
                Pl011RawPuts("[MAP] ensure_l2_table FAILED (2MB block path)\n");
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
                Pl011RawPuts(buf);
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
                Pl011RawPuts(buf);
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

    if (uart_pa == 0)
        uart_pa = ARM64_UART_DEFAULT;

    uart_pa &= ~(PAGE_SIZE - 1ULL);

    if (!map_region_hierarchical(uart_pa, uart_pa, PAGE_SIZE, uart_attrs))
    {
        Pl011RawPuts("[PT] FAILED to map early UART identity\n");
        ok = FALSE;
    }

    if (!map_region_hierarchical(ARM64_PHYS_MAP_BASE | uart_pa, uart_pa, PAGE_SIZE, uart_attrs))
    {
        Pl011RawPuts("[PT] FAILED to map early UART physical alias\n");
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

    ULONGLONG maxPool = ARM64_DEFAULT_MAX_NONPAGED_POOL +
                        (extraPages / 256ULL) * ARM64_MAX_ADDITION_NONPAGED_PER_MB;
    if (maxPool < nonPagedBytes)
        maxPool = nonPagedBytes;
    if (maxPool > ARM64_MAX_NONPAGED_POOL_SIZE)
        maxPool = ARM64_MAX_NONPAGED_POOL_SIZE;

    UNREFERENCED_PARAMETER(maxPool);

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

        /* Debug: check if l0_slot is out of range */
        if (KernelSpace && l0_slot >= 100 && l0_slot < ARM64_EXTRA_KERNEL_SLOTS + 100) {
            char buf[256];
            RtlStringCbPrintfA(buf, sizeof(buf),
                "[EnsureRange] DEBUG: Va=0x%llx l0_idx=%llu in_pool=%d l0_slot=%llu ARM64_KSEG0_L0_INDEX=%llu\n",
                (unsigned long long)Va,
                (unsigned long long)l0_idx,
                in_pool,
                (unsigned long long)l0_slot,
                (unsigned long long)ARM64_KSEG0_L0_INDEX);
            Pl011RawPuts(buf);
        }

        UINT64 *l2_table_ptr = ensure_l2_table(KernelSpace, l0_slot, l1_table, l1_idx, Va);
        if (!l2_table_ptr) {
            Pl011RawPuts("[EnsureRange] ensure_l2_table FAILED\n");
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
            Pl011RawPuts(buf);
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
            Pl011RawPuts("[PLAN] map_region_hierarchical FAILED\n");
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
            Pl011RawPuts("[PLAN] Arm64EnsureRangeTables FAILED\n");
            return FALSE;
        }
    }

    if (Target == Arm64MappingPhysicalAlias)
        Plan->RequestCount = 0;
    else if (Target == Arm64MappingKernel)
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

    if (!arm64_l0_page_table || !arm64_kernel_l0_table || !arm64_kuser_l1_table ||
        !arm64_kuser_l2_table || !arm64_kuser_l3_table || !arm64_l1_page_tables ||
        !arm64_kernel_l1_tables || !arm64_kernel_l2_tables || !arm64_kernel_l3_tables)
    {
        for (;;) __asm__ volatile("wfi");
    }

    RtlZeroMemory(arm64_l0_page_table, ARM64_PT_BYTES);
    RtlZeroMemory(arm64_l1_page_tables, ARM64_USER_L1_TABLES * ARM64_PT_BYTES);
    RtlZeroMemory(arm64_kernel_l0_table, ARM64_PT_BYTES);
    RtlZeroMemory(arm64_kernel_l1_tables, ARM64_KERNEL_L1_TABLES * ARM64_PT_BYTES);
    RtlZeroMemory(arm64_kuser_l1_table, ARM64_PT_BYTES);

    RtlZeroMemory(arm64_kernel_l2_tables,
                  ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_PT_BYTES);

    RtlZeroMemory(arm64_kernel_l3_tables,
                  ARM64_KERNEL_L1_TABLES * ARM64_L2_TABLES_PER_L1 * ARM64_L3_TABLES_PER_L2
                  * ARM64_PT_BYTES);

    RtlZeroMemory(arm64_kuser_l2_table, ARM64_PT_BYTES);
    RtlZeroMemory(arm64_kuser_l3_table, ARM64_PT_BYTES);
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
     *   - 502: PFN Database
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
        /* Post-EBS: skip the bulk identity-mapping pre-reservation. The
         * per-region Arm64MapVirtualMemory calls done later still walk the
         * page-table tree on demand. */
        return;
    }

    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap)
    {
        ARM64_MAPPING_PLAN Plan;
        ARM64_RESERVATION_HINTS Reservation = {0};

        /* ========== DIAGNOSTIC: Dump memory map for identity mapping ========== */
        #if 0  // Disabled - TRACE() doesn't work after ExitBootServices
        {
            UINT64 current_pc;
            UINT64 loader_start = 0, loader_end = 0;
            BOOLEAN loader_found = FALSE;

            __asm__ volatile("adr %0, ." : "=r"(current_pc));

            TRACE("ARM64-DIAG: ============================================\n");
            TRACE("ARM64-DIAG: MEMORY MAP FOR IDENTITY MAPPING\n");
            TRACE("ARM64-DIAG: Current PC = 0x%llx\n", (unsigned long long)current_pc);
            TRACE("ARM64-DIAG: Map limit = 0x%llx (MM_MAX_PAGE_LOADER_MAPPED)\n",
                  (unsigned long long)((UINT64)MM_MAX_PAGE_LOADER_MAPPED << PAGE_SHIFT));
            TRACE("ARM64-DIAG: Total descriptors: %lu\n", MemoryMapSize);
            TRACE("ARM64-DIAG: --------------------------------------------\n");

            for (ULONG i = 0; i < MemoryMapSize; i++)
            {
                const FREELDR_MEMORY_DESCRIPTOR *Desc = &MemoryMap[i];
                UINT64 base = (UINT64)Desc->BasePage * PAGE_SIZE;
                UINT64 size = (UINT64)Desc->PageCount * PAGE_SIZE;
                UINT64 end = base + size;
                const char *type_str;

                /* Get memory type string */
                switch (Desc->MemoryType)
                {
                    case LoaderFree: type_str = "Free"; break;
                    case LoaderLoadedProgram: type_str = "LoadedProgram"; break;
                    case LoaderFirmwareTemporary: type_str = "FirmwareTemp"; break;
                    case LoaderFirmwarePermanent: type_str = "FirmwarePerm"; break;
                    case LoaderOsloaderHeap: type_str = "OsloaderHeap"; break;
                    case LoaderOsloaderStack: type_str = "OsloaderStack"; break;
                    case LoaderSystemCode: type_str = "SystemCode"; break;
                    case LoaderHalCode: type_str = "HalCode"; break;
                    case LoaderBootDriver: type_str = "BootDriver"; break;
                    case LoaderRegistryData: type_str = "RegistryData"; break;
                    case LoaderMemoryData: type_str = "MemoryData"; break;
                    case LoaderNlsData: type_str = "NlsData"; break;
                    case LoaderSpecialMemory: type_str = "SpecialMemory"; break;
                    case LoaderReserve: type_str = "Reserve"; break;
                    case LoaderBad: type_str = "Bad"; break;
                    default: type_str = "Unknown"; break;
                }

                /* Check if this contains the current PC */
                BOOLEAN contains_pc = (current_pc >= base && current_pc < end);

                TRACE("ARM64-DIAG: [%2lu] 0x%llx-0x%llx (%lluMB) %s%s\n",
                      i,
                      (unsigned long long)base,
                      (unsigned long long)end,
                      (unsigned long long)(size >> 20),
                      type_str,
                      contains_pc ? " <-- CURRENT PC" : "");

                /* Track loader region */
                if (Desc->MemoryType == LoaderLoadedProgram && contains_pc)
                {
                    loader_start = base;
                    loader_end = end;
                    loader_found = TRUE;
                }
            }

            TRACE("ARM64-DIAG: --------------------------------------------\n");

            if (loader_found)
            {
                TRACE("ARM64-DIAG: Bootloader region: 0x%llx-0x%llx (%lluMB)\n",
                      (unsigned long long)loader_start,
                      (unsigned long long)loader_end,
                      (unsigned long long)((loader_end - loader_start) >> 20));
            }
            else
            {
                ERR("ARM64-DIAG: WARNING - Current PC 0x%llx not found in any LoadedProgram region!\n",
                    (unsigned long long)current_pc);

                /* Search for ANY region containing the PC */
                for (ULONG i = 0; i < MemoryMapSize; i++)
                {
                    const FREELDR_MEMORY_DESCRIPTOR *Desc = &MemoryMap[i];
                    UINT64 base = (UINT64)Desc->BasePage * PAGE_SIZE;
                    UINT64 size = (UINT64)Desc->PageCount * PAGE_SIZE;
                    UINT64 end = base + size;

                    if (current_pc >= base && current_pc < end)
                    {
                        ERR("ARM64-DIAG: PC is in region type %d at 0x%llx-0x%llx\n",
                            Desc->MemoryType,
                            (unsigned long long)base,
                            (unsigned long long)end);
                    }
                }
            }

            /* Check if loader is within identity map limit */
            if (loader_found)
            {
                UINT64 map_limit = (UINT64)MM_MAX_PAGE_LOADER_MAPPED << PAGE_SHIFT;
                if (loader_end > map_limit)
                {
                    ERR("ARM64-DIAG: CRITICAL - Loader extends beyond identity map limit!\n");
                    ERR("ARM64-DIAG: Loader end: 0x%llx, Map limit: 0x%llx\n",
                        (unsigned long long)loader_end,
                        (unsigned long long)map_limit);
                }
                else
                {
                    TRACE("ARM64-DIAG: Loader within identity map limit (OK)\n");
                }
            }

            TRACE("ARM64-DIAG: ============================================\n");
        }
        #endif  // End disabled diagnostic block
        /* ========== END DIAGNOSTIC ========== */

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
            Pl011RawPuts("[PT] FAILED to queue hyperspace\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_DEBUG_MAPPING_BASE,
                                          ARM64_DEBUG_MAPPING_BYTES))
        {
            Pl011RawPuts("[PT] FAILED to queue debug mapping\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_PFN_DB_BASE,
                                          pfnReserveBytes))
        {
            Pl011RawPuts("[PT] FAILED to queue PFN database\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_PFN_DB_BASE + pfnReserveBytes,
                                          nonPagedReserveBytes))
        {
            Pl011RawPuts("[PT] FAILED to queue nonpaged pool\n");
            return;
        }

        if (!Arm64MappingPlanEnsureTables(&Plan,
                                          ARM64_PAGED_POOL_BASE,
                                          ARM64_PAGED_POOL_INIT_BYTES))
        {
            Pl011RawPuts("[PT] FAILED to queue paged pool\n");
            return;
        }

        if (!Arm64MappingPlanApply(&Plan, Arm64MappingIdentity))
        {
            Pl011RawPuts("[PT] FAILED to apply identity mapping\n");
            return;
        }

        if (!Arm64MappingPlanApply(&Plan, Arm64MappingKernel))
        {
            Pl011RawPuts("[PT] FATAL - Kernel mapping failed\n");
            for (;;)
                __asm__ volatile("wfi");
        }

        if (!Arm64MappingPlanApply(&Plan, Arm64MappingPhysicalAlias))
        {
            Pl011RawPuts("[PT] FATAL - Physical alias mapping failed\n");
            for (;;)
                __asm__ volatile("wfi");
        }
    }
}

#ifndef SCTLR_EL1_WXN
#define SCTLR_EL1_WXN (1ULL << 19)
#endif

/*
 * Diagnostic: Walk page table hierarchy and verify mapping exists for a VA.
 * Returns TRUE if mapping is valid, FALSE otherwise.
 * NOTE: Called post-ExitBootServices, so TRACE/ERR macros removed to avoid hangs.
 */
static BOOLEAN
Arm64VerifyIdentityMapping(UINT64 va, UINT64 *out_pa, UINT64 *out_attrs)
{
    UINT64 l0_idx = (va >> 39) & 0x1FF;
    UINT64 l1_idx = (va >> 30) & 0x1FF;
    UINT64 l2_idx = (va >> 21) & 0x1FF;
    UINT64 l3_idx = (va >> 12) & 0x1FF;
    UINT64 entry;
    UINT64 *table;

    /* Check L0 */
    if (!arm64_l0_page_table)
    {
        return FALSE;
    }

    if (l0_idx >= ARM64_USER_L1_TABLES)
    {
        return FALSE;
    }

    entry = arm64_l0_page_table[l0_idx];

    if (!DESC_VALID(entry))
    {
        return FALSE;
    }

    if (!DESC_IS_TABLE(entry))
    {
        return FALSE;
    }

    /* Get L1 table */
    table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);

    entry = table[l1_idx];

    if (!DESC_VALID(entry))
    {
        return FALSE;
    }

    /* Check for 1GB block */
    if (DESC_IS_BLOCK(entry))
    {
        UINT64 block_pa = entry & PTE_BLOCK_ADDR_MASK_1G;
        UINT64 offset = va & 0x3FFFFFFFULL;
        if (out_pa) *out_pa = block_pa + offset;
        if (out_attrs) *out_attrs = entry & 0xFFF0000000000FFFULL;
        return TRUE;
    }

    if (!DESC_IS_TABLE(entry))
    {
        return FALSE;
    }

    /* Get L2 table */
    table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);

    entry = table[l2_idx];

    if (!DESC_VALID(entry))
    {
        return FALSE;
    }

    /* Check for 2MB block */
    if (DESC_IS_BLOCK(entry))
    {
        UINT64 block_pa = entry & PTE_BLOCK_ADDR_MASK_2M;
        UINT64 offset = va & 0x1FFFFFULL;
        if (out_pa) *out_pa = block_pa + offset;
        if (out_attrs) *out_attrs = entry & 0xFFF0000000000FFFULL;
        return TRUE;
    }

    if (!DESC_IS_TABLE(entry))
    {
        return FALSE;
    }

    /* Get L3 table */
    table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);

    entry = table[l3_idx];

    if (!DESC_VALID(entry))
    {
        return FALSE;
    }

    if (!DESC_IS_PAGE(entry))
    {
        return FALSE;
    }

    /* 4KB page */
    {
        UINT64 page_pa = entry & PTE_ADDR_MASK;
        UINT64 offset = va & 0xFFFULL;
        if (out_pa) *out_pa = page_pa + offset;
        if (out_attrs) *out_attrs = entry & 0xFFF0000000000FFFULL;
    }

    return TRUE;
}

/*
 * Diagnostic: Dump L2 pool allocation state for debugging
 */
static __attribute__((unused)) VOID
Arm64DumpL2PoolState(VOID)
{
    TRACE("ARM64-DIAG: L2 pool state (user/identity):\n");
    for (ULONG i = 0; i < ARM64_USER_L1_TABLES; i++)
    {
        TRACE("ARM64-DIAG:   L0 slot %lu: %llu/%u L2 tables used\n",
              i,
              (unsigned long long)arm64_user_l2_next_index[i],
              ARM64_L2_TABLES_PER_L1);
    }
}

VOID Arm64EnablePageTables(VOID)
{
    // TRACE("ARM64: Enabling page tables (Safe Switch: MMU Off -> Update -> MMU On)\n");
    // TRACE("ARM64: PT_EN: Start\n");

    UINT64 tcr;
    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();
    UINT64 sctlr = 0;

    if (!page_tables_initialized)
        ensure_page_tables_initialized();
    Arm64ApplyImageSectionProtections();

    /*
     * Windows loaders always run EL1; if we somehow arrived here in plain EL2
     * (no VHE), drop to EL1 before programming EL1/VHE register layout to
     * avoid reserved/invalid TCR_EL2 settings.
     */
    if (el == 2 && !el12)
    {
    // TRACE("ARM64: PT_EN: Forcing EL1 entry state for Windows parity\n");
        Arm64EnsureEl1EntryState();
        el = get_effective_el();
        el12 = use_el12_registers();
        if (el != 1 && !el12)
        {
    // ERR("ARM64: PT_EN: Unable to transition to EL1, aborting MMU enable\n");
            return;
        }
    }

    /* Compose TCR after ensuring final EL */
    tcr = get_tcr(NULL, NULL);

    /* 1. Read current SCTLR and Disable MMU (M) and Cache (C) */
    // TRACE("ARM64: PT_EN: Reading SCTLR\n");
    if (el == 1 || el12) {
        if (el12) __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr));
        else      __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    }
    else if (el == 2) {
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
    }

    // TRACE("ARM64: PT_EN: Orig SCTLR=0x%llx\n", (unsigned long long)sctlr);

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

    // TRACE("ARM64: PT_EN: Disabling MMU\n");
    if (el == 1 || el12) {
        if (el12) __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr_off) : "memory");
        else      __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr_off) : "memory");
    }
    else if (el == 2) {
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr_off) : "memory");
    }
    ARM64_ISB();
    // TRACE("ARM64: PT_EN: MMU Disabled\n");

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
         * because setup_pgtables() validates all of them:
         *   - arm64_l0_page_table, arm64_kernel_l0_table
         *   - arm64_kuser_l1_table, arm64_kuser_l2_table, arm64_kuser_l3_table
         *   - arm64_l1_page_tables, arm64_kernel_l1_tables
         *   - arm64_kernel_l2_tables, arm64_kernel_l3_tables
         */
        use_static_page_tables();

        /* If still NULL after fallback, we cannot proceed */
        if (!arm64_l0_page_table || !arm64_kernel_l0_table ||
            !arm64_kuser_l1_table || !arm64_kuser_l2_table || !arm64_kuser_l3_table ||
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

    // TRACE("ARM64: PT_EN: Writing MAIR\n");
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
    // TRACE("ARM64: PT_EN: Writing TCR\n");
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
    // TRACE("ARM64: PT_EN: Writing TTBRs\n");
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
    // TRACE("ARM64: PT_EN: TTBRs written\n");

    ARM64_DSB_ISH();
    ARM64_ISB();
    // TRACE("ARM64: PT_EN: TLB Invalidated\n");

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

        /* Clean dcache to ensure page table writes are visible to the table walker */
        __asm_dcache_all(0);
        ARM64_DSB_ISH();
    }
    /* ========== END CRITICAL REGION MAPPING ========== */

    /* ========== DIAGNOSTIC: Verify identity mapping before MMU enable ========== */
    /*
     * NOTE: This verification runs AFTER we've ensured the critical regions are
     * mapped. If verification still fails, there's a fundamental problem with
     * the page table structure or the mapping function.
     */
    {
        UINT64 current_pc;
        UINT64 mapped_pa;
        UINT64 mapped_attrs;
        BOOLEAN mapping_valid;

        /* Get current PC */
        __asm__ volatile("adr %0, ." : "=r"(current_pc));


        /* Verify current PC is identity-mapped */
        mapping_valid = Arm64VerifyIdentityMapping(current_pc, &mapped_pa, &mapped_attrs);

        if (!mapping_valid)
        {
            /* Don't proceed - hang here */
            for (;;)
                __asm__ volatile("wfi");
        }

    }
    /* ========== END DIAGNOSTIC ========== */

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

    // TRACE("ARM64: Page tables enabled (SCTLR=0x%llx)\n", (unsigned long long)sctlr_on);
    // TRACE("ARM64: PT_EN: End\n");
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
        Pl011RawPuts("[PT] ERROR: allocate_page_table_memory FAILED\n");
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
    identity_mapping_enabled = TRUE;

    TRACE("ARM64: MMU enabled\n");
}

VOID Arm64SetupKernelHandoffMMU(VOID)
{
    UINT64 tcr, ips, va_bits;
    int el = get_effective_el();
    BOOLEAN el12 = use_el12_registers();

    TRACE("ARM64: Setting up kernel handoff MMU (EL%d)\n", el);

    ensure_page_tables_initialized();

    {
        const UINT64 self_idx = (ARM64_SELF_PXE_BASE >> ARM64_PXI_SHIFT) & ARM64_PX_MASK;
        const UINT64 root_pa = phys_from_ptr(arm64_kernel_l0_table);
        const UINT64 desired = root_pa | PTE_SELFREF_ATTRS;
        UINT64 entry = arm64_kernel_l0_table[self_idx];

        /* Verify and repair recursive mapping if needed */
        if (entry != desired) {
            pte_write(&arm64_kernel_l0_table[self_idx], desired);
        }
    }

    tcr = get_tcr(&ips, &va_bits);
    if (!tcr) {
        ERR("ARM64: handoff TCR failed\n");
        return;
    }

    UINT64 phys_ttbr0 = phys_from_ptr(arm64_l0_page_table);
    UINT64 phys_ttbr1 = phys_from_ptr(arm64_kernel_l0_table);
    UINT64 ttbr1_hardware_base = arm64_ttbr1_l0_base(phys_ttbr1);
    BOOLEAN reprogram_ttbrs = TRUE;

    /* CRITICAL: Map kernel and loader regions BEFORE switching TTBR1! */
    {
        /* Map the kernel and drivers region (default 256MB window). */
        UINT64 kernel_region_pa = ARM64_KERNEL_PHYSICAL_BASE;
        UINT64 kernel_region_va = ARM64_KSEG0_BASE + kernel_region_pa;
        UINT64 kernel_region_size = ARM64_KERNEL_PHYSICAL_SIZE;

        /* Map with executable permissions */
        UINT64 attrs = PTE_BLOCK_MEMTYPE(ARM64_MT_NORMAL) |
                       PTE_BLOCK_INNER_SHARE |
                       PTE_BLOCK_AF;

        if (!map_region_hierarchical(kernel_region_va, kernel_region_pa,
                                     kernel_region_size, attrs))
        {
            ERR("failed to map kernel region\n");
            return;
        }

        Arm64EnsureRangeTables(ARM64_PAGED_POOL_BASE,
                               ARM64_PAGED_POOL_INIT_BYTES,
                               TRUE);

        /* Also map the loader itself both identity and in kernel space */
        UINT64 loader_region_pa = ARM64_LOADER_PHYSICAL_BASE;
        UINT64 loader_region_size = ARM64_LOADER_PHYSICAL_SIZE;
#ifdef UEFIBOOT
        if (OsLoaderBase && OsLoaderSize)
        {
            loader_region_pa = (UINT64)(uintptr_t)OsLoaderBase & ~(UINT64)(PAGE_SIZE - 1ULL);
            loader_region_size = ARM64_ALIGN_UP((UINT64)OsLoaderSize +
                                                ((UINT64)(uintptr_t)OsLoaderBase - loader_region_pa),
                                                PAGE_SIZE);
        }
#endif

        /* Identity map for current execution */
        if (!map_region_hierarchical(loader_region_pa, loader_region_pa,
                                     loader_region_size, attrs))
        {
            /* Loader identity mapping failed - may be out of range */
        }

        /* Also map loader into kernel space for transition */
        UINT64 loader_kernel_va = ARM64_KSEG0_BASE + loader_region_pa;
        map_region_hierarchical(loader_kernel_va, loader_region_pa,
                                loader_region_size, attrs);

        if (!Arm64MapPageTableAllocationsIntoKseg0())
        {
            ERR("failed to map page-table allocations into KSEG0\n");
            return;
        }

        {
            const UINT64 pt_attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) |
                                    PTE_BLOCK_INNER_SHARE |
                                    PTE_BLOCK_AF |
                                    PTE_BLOCK_PXN |
                                    PTE_BLOCK_UXN;

            if (!Arm64MapPhysicalRangeIntoKseg0(phys_ttbr0, 1, pt_attrs) ||
                !Arm64MapPhysicalRangeIntoKseg0(phys_ttbr1, 1, pt_attrs) ||
                !Arm64VerifyKseg0PhysicalPage(phys_ttbr0, "TTBR0 root") ||
                !Arm64VerifyKseg0PhysicalPage(phys_ttbr1, "TTBR1 root"))
            {
                ERR("active TTBR root is not reachable through KSEG0\n");
                return;
            }
        }
    }

    /* Ensure the loader stack has a higher-half mapping before TTBR1 is used */
    {
        UINT64 sp_value;
        __asm__ volatile("mov %0, sp" : "=r"(sp_value));
        UINT64 sp_page = sp_value & ~(PAGE_SIZE - 1ULL);
        UINT64 sp_kva = ARM64_KSEG0_BASE | sp_page;
        UINT64 sp_phys = VA_TO_PA(sp_page);
        ULONG stack_attrs = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_UXN | ARM64_MAP_ATTR_PXN;

        Arm64MapVirtualMemory(sp_kva, sp_phys, PAGE_SIZE, stack_attrs);
    }

    if (el == 1 || el12) {
        /* Read current TCR to preserve its configuration */
        UINT64 current_tcr;
        if (el12)
            __asm__ volatile("mrs %0, " TCR_EL12_SYSREG : "=r"(current_tcr));
        else
            __asm__ volatile("mrs %0, tcr_el1" : "=r"(current_tcr));

        /* Merge firmware-safe knobs with the loader-computed configuration */
        UINT64 new_tcr = tcr;
        const UINT64 preserve_mask = TCR_ASID16 | TCR_TBI0 | TCR_TBI1 | TCR_TBID0 | TCR_TBID1;
        new_tcr |= current_tcr & preserve_mask;

        /* Prefer the broader IPS field from firmware if it is larger */
        {
            const UINT64 ips_mask = (UINT64)0x7ULL << 32;
            UINT64 desired_ips = new_tcr & ips_mask;
            UINT64 firmware_ips = current_tcr & ips_mask;
            if (firmware_ips > desired_ips) {
                new_tcr = (new_tcr & ~ips_mask) | firmware_ips;
            }
        }

        /* Enforce 47-bit TTBR1 with cacheable, inner-shareable attributes */
        new_tcr &= ~(TCR_EPD0 | TCR_EPD1);
        new_tcr &= ~((UINT64)0x3FULL << 16);
        new_tcr |= TCR_T1SZ(47);
        new_tcr &= ~((UINT64)3ULL << 24);
        new_tcr |= TCR_IRGN1_WBWA;
        new_tcr &= ~((UINT64)3ULL << 26);
        new_tcr |= TCR_ORGN1_WBWA;
        new_tcr &= ~((UINT64)3ULL << 28);
        new_tcr |= TCR_SHARED1_INNER;
        new_tcr &= ~((UINT64)3ULL << 30);
        new_tcr |= TCR_TG1_4K;
        new_tcr |= TCR_A1;

        /*
         * Keep TTBR0 geometry untouched: we are executing from that identity
         * map, so changing T0SZ/TG0 mid-flight would alter the walk depth and
         * fault on the next fetch.
         */
        {
            const UINT64 t0_mask = ((UINT64)0x3FULL << 0) |  /* T0SZ */
                                   ((UINT64)1ULL    << 7) |  /* EPD0 */
                                   ((UINT64)3ULL    << 8) |  /* IRGN0 */
                                   ((UINT64)3ULL    << 10) | /* ORGN0 */
                                   ((UINT64)3ULL    << 12) | /* SH0 */
                                   ((UINT64)3ULL    << 14);  /* TG0 */
            new_tcr = (new_tcr & ~t0_mask) | (current_tcr & t0_mask);
        }

        tcr = new_tcr;

        UINT64 ttbr1_current = read_ttbr1(el12);
        BOOLEAN need_full_reprogram = reprogram_ttbrs;

        if (!reprogram_ttbrs || ttbr1_current == ttbr1_hardware_base)
        {
            need_full_reprogram = FALSE;
        }

        if (need_full_reprogram)
        {
            UINT64 sctlr_saved;

            if (el12)
                __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr_saved));
            else
                __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_saved));

            /* Get current PC and ensure it's mapped before changing TTBR1 */
            {
                UINT64 current_pc;
                __asm__ volatile("adr %0, ." : "=r"(current_pc));

                /* Map FreeLoader region into TTBR1 before switch */
                UINT64 loader_base = current_pc & ~(UINT64)0xFFFFFULL; /* Align to 1MB */
                UINT64 loader_kva = ARM64_KSEG0_BASE | loader_base;
                UINT64 loader_size = 0x200000; /* Map 2MB for safety */

                Arm64MapVirtualMemory(loader_kva, loader_base, loader_size,
                                      ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_EXECUTE);
            }

            /* Update TTBRs with MMU enabled */
            ARM64_DSB_ISH();
            TLBI_VMALLE1IS();
            ARM64_DSB_ISH();
            ARM64_ISB();

            if (el12) {
                write_ttbr1(TRUE, ttbr1_hardware_base);
                __asm__ volatile("msr " TCR_EL12_SYSREG   ", %0" :: "r" (tcr) : "memory");
                __asm__ volatile("msr " MAIR_EL12_SYSREG  ", %0" :: "r" (MEMORY_ATTRIBUTES) : "memory");
            } else {
                write_ttbr1(FALSE, ttbr1_hardware_base);
                ARM64_ISB();

                __asm__ volatile("msr tcr_el1, %0"   :: "r" (tcr) : "memory");
                ARM64_ISB();

                __asm__ volatile("msr mair_el1, %0"  :: "r" (MEMORY_ATTRIBUTES) : "memory");
            }

            ARM64_ISB();
            TLBI_VMALLE1IS();
            ARM64_DSB_ISH();
            ARM64_ISB();

            if (!Arm64VerifyKseg0PhysicalPage(phys_ttbr1, "TTBR1 root after switch") ||
                !Arm64VerifyKseg0HardwarePage(phys_ttbr1, "TTBR1 root after switch"))
            {
                ERR("active TTBR1 root KSEG0 mapping lost after TTBR switch\n");
                return;
            }
        }

        if (!Arm64VerifyKseg0PhysicalPage(phys_ttbr1, "TTBR1 root final") ||
            !Arm64VerifyKseg0HardwarePage(phys_ttbr1, "TTBR1 root final"))
        {
            ERR("active TTBR1 root KSEG0 mapping lost during handoff finalization\n");
            return;
        }
    } else if (el == 2) {
        UINT64 sctlr2;
        tcr = TCR_EL2_RSVD | (ips << 16) |
              TCR_T0SZ(va_bits) | TCR_SHARED_INNER | TCR_ORGN_WBWA | TCR_IRGN_WBWA | TCR_TG0_4K;

        /* Disable MMU for safe reprogramming at EL2 */
        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr2));
        sctlr2 &= ~(1ULL << 0);
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr2) : "memory");
        ARM64_ISB();

        ARM64_DSB_ISH();
        __asm__ volatile("msr ttbr0_el2, %0" :: "r" (phys_ttbr0) : "memory");
        __asm__ volatile("msr tcr_el2, %0"   :: "r" (tcr) : "memory");
        __asm__ volatile("msr mair_el2, %0"  :: "r" (MEMORY_ATTRIBUTES) : "memory");
        ARM64_ISB();

        TLBI_ALLE2IS();
        ARM64_DSB_ISH();
        ARM64_ISB();

        __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr2));
        sctlr2 |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr2) : "memory");
        ARM64_ISB();
    } else if (el == 3) {
        UINT64 sctlr3;
        tcr = TCR_EL3_RSVD | (ips << 16) |
              TCR_T0SZ(va_bits) | TCR_SHARED_INNER | TCR_ORGN_WBWA | TCR_IRGN_WBWA | TCR_TG0_4K;

        __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr3));
        sctlr3 &= ~(1ULL << 0);
        __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr3) : "memory");
        ARM64_ISB();

        ARM64_DSB_ISH();
        __asm__ volatile("msr ttbr0_el3, %0" :: "r" (phys_ttbr0) : "memory");
        __asm__ volatile("msr tcr_el3, %0"   :: "r" (tcr) : "memory");
        __asm__ volatile("msr mair_el3, %0"  :: "r" (MEMORY_ATTRIBUTES) : "memory");
        ARM64_ISB();

        TLBI_ALLE3IS();
        ARM64_DSB_ISH();
        ARM64_ISB();

        __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr3));
        sctlr3 |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
        __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr3) : "memory");
        ARM64_ISB();
    }

    mmu_enabled = TRUE;
    identity_mapping_enabled = TRUE;

    TRACE("ARM64: Kernel handoff MMU configuration complete\n");
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

BOOLEAN
Arm64MapUserSharedDataPage(ULONGLONG VirtualAddress,
                           ULONGLONG PhysicalAddress,
                           ULONG Attributes)
{
    ULONGLONG l0_idx = (VirtualAddress >> 39) & 0x1FFULL;
    ULONGLONG l1_idx = (VirtualAddress >> 30) & 0x1FFULL;
    ULONGLONG l2_idx = (VirtualAddress >> 21) & 0x1FFULL;
    ULONGLONG l3_idx = (VirtualAddress >> 12) & 0x1FFULL;
    UINT64 attrs = PTE_BLOCK_MEMTYPE(ARM64_MEM_ATTR_NORMAL_WB) |
                   PTE_BLOCK_INNER_SHARE |
                   PTE_BLOCK_AF |
                   PTE_BLOCK_PXN |
                   PTE_BLOCK_UXN;

    UNREFERENCED_PARAMETER(Attributes);

    if (l0_idx != ARM64_KUSER_SHARED_L0_INDEX)
    {
        TRACE("ARM64: Arm64MapUserSharedDataPage invalid L0 index %llu\n",
              (unsigned long long)l0_idx);
        return FALSE;
    }

    if (!DESC_VALID(arm64_kernel_l0_table[l0_idx]))
    {
        pte_write(&arm64_kernel_l0_table[l0_idx],
                  (phys_from_ptr(arm64_kuser_l1_table) | PTE_TABLE_ATTRS));
    }

    if (!DESC_VALID(arm64_kuser_l1_table[l1_idx]))
    {
        pte_write(&arm64_kuser_l1_table[l1_idx],
                  (phys_from_ptr(arm64_kuser_l2_table) | PTE_TABLE_ATTRS));
    }

    if (!DESC_VALID(arm64_kuser_l2_table[l2_idx]))
    {
        pte_write(&arm64_kuser_l2_table[l2_idx],
                  (phys_from_ptr(arm64_kuser_l3_table) | PTE_TABLE_ATTRS));
    }

    UINT64 desc = (PhysicalAddress & ~0xFFFULL) |
                  PTE_TYPE_VALID |
                  PTE_TYPE_PAGE |
                  attrs;

    pte_replace_break_before_make(&arm64_kuser_l3_table[l3_idx],
                                  desc,
                                  VirtualAddress,
                                  PAGE_SIZE);

    tlbi_va_entry(VirtualAddress, PAGE_SIZE);
    ARM64_DSB_ISH();
    ARM64_ISB();

    TRACE("ARM64: KUSER shared page mapped VA=0x%llx -> PA=0x%llx\n",
          (unsigned long long)VirtualAddress,
          (unsigned long long)PhysicalAddress);

    return TRUE;
}

/*
 * Unmap a virtual range.
 * - Requires 4KiB alignment.
 * - Only supports:
 *   * TTBR0 identity region, or
 *   * Kernel KSEG0 region (L0 indices within ARM64_KSEG0_L0_INDEX..+ARM64_KERNEL_L1_TABLES).
 * - Does not support partial unmap of 1GiB/2MiB blocks.
 */
BOOLEAN Arm64UnmapVirtualMemory(ULONGLONG VirtualAddress, ULONGLONG Size)
{
    UINT64 va = VirtualAddress;
    UINT64 end = VirtualAddress + Size;

    TRACE("ARM64: Unmap VA=0x%016llx, Size=0x%016llx\n", VirtualAddress, Size);

    ensure_page_tables_initialized();

    if (((va | Size) & (PAGE_SIZE - 1)) != 0)
    {
        TRACE("ARM64: Unmap requires 4KiB alignment\n");
        return FALSE;
    }

    while (va < end)
    {
        UINT64 l0_idx = (va >> 39) & 0x1FF;
        UINT64 l1_idx = (va >> 30) & 0x1FF;
        UINT64 l2_idx = (va >> 21) & 0x1FF;
        UINT64 l3_idx = (va >> 12) & 0x1FF;
        BOOLEAN kernel_va = (va >= ARM64_SYSTEM_RANGE_BASE);
        UINT64 *l1_table;

        if (kernel_va)
        {
            if (l0_idx < ARM64_KSEG0_L0_INDEX ||
                l0_idx >= (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES))
            {
                TRACE("ARM64: Unmap VA 0x%llx outside kernel space\n", va);
                return FALSE;
            }
            l1_table = arm64_kernel_l1_tables[l0_idx - ARM64_KSEG0_L0_INDEX];
        }
        else
        {
            if (l0_idx >= ARM64_USER_L1_TABLES)
            {
                TRACE("ARM64: Unmap VA 0x%llx outside user static range\n", va);
                return FALSE;
            }
            l1_table = arm64_l1_page_tables[l0_idx];
        }

        UINT64 l1_entry = l1_table[l1_idx];
        if (DESC_IS_BLOCK(l1_entry))
        {
            if ((va & ARM64_BLOCK_MASK_1G) != 0 || (end - va) < ARM64_BLOCK_SIZE_1G)
            {
                TRACE("ARM64: Cannot partially unmap 1GiB block at VA 0x%llx\n", va);
                return FALSE;
            }
            pte_replace_break_before_make(&l1_table[l1_idx],
                                          0,
                                          va,
                                          ARM64_BLOCK_SIZE_1G);
            va += ARM64_BLOCK_SIZE_1G;
            continue;
        }

        if (!DESC_IS_TABLE(l1_entry))
        {
            TRACE("ARM64: No mapping found at L1 for VA 0x%llx\n", va);
            return FALSE;
        }

        UINT64 *l2_table = (UINT64 *)PA_TO_VA(l1_entry & PTE_ADDR_MASK);
        UINT64 l2_entry = l2_table[l2_idx];
        if (DESC_IS_BLOCK(l2_entry))
        {
            if ((va & ARM64_BLOCK_MASK_2M) != 0 || (end - va) < ARM64_BLOCK_SIZE_2M)
            {
                TRACE("ARM64: Cannot partially unmap 2MiB block at VA 0x%llx\n", va);
                return FALSE;
            }
            pte_replace_break_before_make(&l2_table[l2_idx],
                                          0,
                                          va,
                                          ARM64_BLOCK_SIZE_2M);
            va += ARM64_BLOCK_SIZE_2M;
            continue;
        }

        if (!DESC_IS_TABLE(l2_entry))
        {
            TRACE("ARM64: No mapping found at L2 for VA 0x%llx\n", va);
            return FALSE;
        }

        UINT64 *l3_table = (UINT64 *)PA_TO_VA(l2_entry & PTE_ADDR_MASK);
        UINT64 pte = l3_table[l3_idx];
        if (!DESC_IS_PAGE(pte))
        {
            TRACE("ARM64: No 4KiB mapping at VA 0x%llx\n", va);
            return FALSE;
        }

        pte_replace_break_before_make(&l3_table[l3_idx],
                                      0,
                                      va,
                                      PAGE_SIZE);
        va += PAGE_SIZE;
    }

    return TRUE;
}

/* ---------- Address translation helpers ---------- */

/*
 * Translate a VA to PA based on the loader's static mappings.
 * - If MMU is disabled, returns VA.
 * - For TTBR0 identity range: returns VA.
 * - For kernel KSEG0 region (4 pooled L0 slots): walks L1/L2/L3.
 * - For other VAs (PFN DB, paged pool, hyperspace, etc.): returns VA unchanged.
 *   (Early boot code should not rely on those translations here.)
 */
ULONGLONG Arm64GetPhysicalAddress(ULONGLONG VirtualAddress)
{
    UINT64 va = VirtualAddress;

    if (!mmu_enabled)
        return va;

    /* Fast path: identity for TTBR0 space */
    if (va < ARM64_SYSTEM_RANGE_BASE)
        return va;

    /* Full walk for TTBR1: L0 -> L1 -> (L2 -> L3) */
    UINT64 l0 = (va >> 39) & 0x1FF;
    UINT64 entry = arm64_kernel_l0_table[l0];
    UINT64 *l1tbl;

    if (!DESC_IS_TABLE(entry))
        return va;

    if (l0 >= ARM64_KSEG0_L0_INDEX && l0 < (ARM64_KSEG0_L0_INDEX + ARM64_KERNEL_L1_TABLES))
        l1tbl = arm64_kernel_l1_tables[l0 - ARM64_KSEG0_L0_INDEX];
    else
        l1tbl = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);

    UINT64 l1 = (va >> 30) & 0x1FF;
    UINT64 pte1 = l1tbl[l1];
    if (DESC_IS_BLOCK(pte1)) {
        return (pte1 & PTE_BLOCK_ADDR_MASK_1G) | (va & 0x3FFFFFFFULL); /* 1GiB */
    }
    if (!DESC_IS_TABLE(pte1)) return va;

    UINT64 *l2tbl = (UINT64 *)PA_TO_VA(pte1 & PTE_ADDR_MASK);
    UINT64 l2 = (va >> 21) & 0x1FF;
    UINT64 pte2 = l2tbl[l2];
    if (DESC_IS_BLOCK(pte2)) {
        return (pte2 & PTE_BLOCK_ADDR_MASK_2M) | (va & 0x1FFFFFULL);     /* 2MiB */
    }
    if (!DESC_IS_TABLE(pte2)) return va;

    UINT64 *l3tbl = (UINT64 *)PA_TO_VA(pte2 & PTE_ADDR_MASK);
    UINT64 l3 = (va >> 12) & 0x1FF;
    UINT64 pte3 = l3tbl[l3];
    if (DESC_IS_PAGE(pte3)) {
        return (pte3 & PTE_ADDR_MASK) | (va & 0xFFFULL);           /* 4KiB */
    }
    return va;
}

BOOLEAN Arm64IsMMUEnabled(VOID)
{
    return mmu_enabled;
}

VOID Arm64DisableMMU(VOID)
{
    ULONGLONG sctlr;

    if (!mmu_enabled) return;

    TRACE("ARM64: Disabling MMU\n");

    Arm64FlushDataCacheAll();
    Arm64InvalidateInstructionCacheAll();
    ARM64_DSB_ISH();
    ARM64_ISB();

    {
        int el = get_effective_el();
        BOOLEAN el12 = use_el12_registers();

        if (el == 1 || el12) {
            if (el12) __asm__ volatile("mrs %0, " SCTLR_EL12_SYSREG : "=r"(sctlr));
            else      __asm__ volatile("mrs %0, sctlr_el1"          : "=r"(sctlr));
            sctlr &= ~(SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I);
            if (el12)
                __asm__ volatile("msr " SCTLR_EL12_SYSREG ", %0" :: "r"(sctlr) : "memory");
            else
                __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
        } else if (el == 2) {
            __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sctlr));
            sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
            __asm__ volatile("msr sctlr_el2, %0" :: "r"(sctlr) : "memory");
        } else if (el == 3) {
            __asm__ volatile("mrs %0, sctlr_el3" : "=r"(sctlr));
            sctlr &= ~((1ULL << 0) | (1ULL << 2) | (1ULL << 12));
            __asm__ volatile("msr sctlr_el3, %0" :: "r"(sctlr) : "memory");
        }
    }
    ARM64_ISB();

    mmu_enabled = FALSE;

    TRACE("ARM64: MMU disabled\n");
}

/* ---------- Attribute query ---------- */

ULONG Arm64GetMemoryAttributes(ULONGLONG Address)
{
    ULONG MemoryMapSize;
    FREELDR_MEMORY_DESCRIPTOR* MemoryMap;
    ULONG i;

    MemoryMap = UefiMemGetMemoryMap(&MemoryMapSize);
    if (MemoryMap) {
        for (i = 0; i < MemoryMapSize; i++) {
            UINT64 start = (UINT64)MemoryMap[i].BasePage * PAGE_SIZE;
            UINT64 end   = start + (UINT64)MemoryMap[i].PageCount * PAGE_SIZE;
            if (Address >= start && Address < end) {
                switch (MemoryMap[i].MemoryType) {
                    case LoaderFirmwarePermanent:
                    case LoaderFirmwareTemporary:
                        return ARM64_MEM_ATTR_DEVICE_nGnRnE;
                    default:
                        return ARM64_MEM_ATTR_NORMAL_WB;
                }
            }
        }
    }
    return ARM64_MEM_ATTR_NORMAL_WB;
}

/* ---------- TLB range flush ---------- */

VOID Arm64FlushTlbRange(ULONGLONG VirtualAddress, ULONGLONG Size)
{
    ULONGLONG end = VirtualAddress + Size;

    if (Size == 0)
        return;

    tlbi_va_range(VirtualAddress, end);
    ARM64_DSB_ISH();
    ARM64_ISB();
}

static VOID debug_dump_static_mapping(UINT64 va)
{
    BOOLEAN kernel = (va >= ARM64_SYSTEM_RANGE_BASE);
    UINT64 *l0_table = kernel ? arm64_kernel_l0_table : arm64_l0_page_table;
    UINT64 l0_idx = (va >> 39) & 0x1FF;
    UINT64 l1_idx = (va >> 30) & 0x1FF;
    UINT64 l2_idx = (va >> 21) & 0x1FF;
    UINT64 l3_idx = (va >> 12) & 0x1FF;
    UINT64 entry;

    /* Enhanced UART debug for critical handoff debugging */
    UartPuts("ARM64: Page walk for VA 0x");
    UartPutHex64(va);
    UartPuts(" (");
    UartPuts(kernel ? "kernel" : "user");
    UartPuts("):\n");

    UartPuts("  L0 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l0_table);
    UartPuts(", idx=");
    UartPutHex32(l0_idx);

    entry = l0_table[l0_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);
    UartPuts("\n");

    if (!DESC_VALID(entry)) {
        UartPuts("  L0 entry INVALID - translation fault!\n");
        TRACE("ARM64: debug map VA=0x%llx L0[%llx] invalid (kernel=%d)\n",
              (unsigned long long)va, (unsigned long long)l0_idx, kernel);
        return;
    }
    if (!DESC_IS_TABLE(entry)) {
        UartPuts("  L0 entry is leaf (not table) - unexpected\n");
        TRACE("ARM64: debug map VA=0x%llx L0[%llx]=0x%llx leaf (kernel=%d)\n",
              (unsigned long long)va, (unsigned long long)l0_idx,
              (unsigned long long)entry, kernel);
        return;
    }

    UINT64 *l1_table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);
    UartPuts("  L1 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l1_table);
    UartPuts(", idx=");
    UartPutHex32(l1_idx);

    entry = l1_table[l1_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);
    UartPuts("\n");

    if (!DESC_VALID(entry)) {
        UartPuts("  L1 entry INVALID - translation fault!\n");
        TRACE("ARM64: debug map VA=0x%llx L1[%llx] invalid\n",
              (unsigned long long)va, (unsigned long long)l1_idx);
        return;
    }
    if (DESC_IS_BLOCK(entry)) {
        UartPuts("  L1 block mapping -> PA 0x");
        UartPutHex64(entry & PTE_BLOCK_ADDR_MASK_1G);
        UartPuts("\n");
        TRACE("ARM64: debug map VA=0x%llx -> block L1[%llx]=0x%llx\n",
              (unsigned long long)va, (unsigned long long)l1_idx,
              (unsigned long long)entry);
        return;
    }
    if (!DESC_IS_TABLE(entry)) {
        UartPuts("  L1 entry unexpected type\n");
        TRACE("ARM64: debug map VA=0x%llx L1[%llx]=0x%llx unexpected\n",
              (unsigned long long)va, (unsigned long long)l1_idx,
              (unsigned long long)entry);
        return;
    }

    UINT64 *l2_table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);
    UartPuts("  L2 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l2_table);
    UartPuts(", idx=");
    UartPutHex32(l2_idx);

    entry = l2_table[l2_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);
    UartPuts("\n");

    if (!DESC_VALID(entry)) {
        UartPuts("  L2 entry INVALID - translation fault!\n");
        TRACE("ARM64: debug map VA=0x%llx L2[%llx] invalid\n",
              (unsigned long long)va, (unsigned long long)l2_idx);
        return;
    }
    if (DESC_IS_BLOCK(entry)) {
        UartPuts("  L2 block mapping -> PA 0x");
        UartPutHex64(entry & PTE_BLOCK_ADDR_MASK_2M);
        UartPuts("\n");
        TRACE("ARM64: debug map VA=0x%llx -> block L2[%llx]=0x%llx\n",
              (unsigned long long)va, (unsigned long long)l2_idx,
              (unsigned long long)entry);
        return;
    }
    if (!DESC_IS_TABLE(entry)) {
        UartPuts("  L2 entry unexpected type\n");
        TRACE("ARM64: debug map VA=0x%llx L2[%llx]=0x%llx unexpected\n",
              (unsigned long long)va, (unsigned long long)l2_idx,
              (unsigned long long)entry);
        return;
    }

    UINT64 *l3_table = (UINT64 *)PA_TO_VA(entry & PTE_ADDR_MASK);
    UartPuts("  L3 table @ 0x");
    UartPutHex64((UINT64)(uintptr_t)l3_table);
    UartPuts(", idx=");
    UartPutHex32(l3_idx);

    entry = l3_table[l3_idx];
    UartPuts(", entry=0x");
    UartPutHex64(entry);

    if (DESC_VALID(entry)) {
        UartPuts(" -> PA 0x");
        UartPutHex64(entry & PTE_ADDR_MASK);
    } else {
        UartPuts(" INVALID");
    }
    UartPuts("\n");

    TRACE("ARM64: debug map VA=0x%llx L3[%llx]=0x%llx\n",
          (unsigned long long)va, (unsigned long long)l3_idx,
          (unsigned long long)entry);
}

VOID Arm64DebugDumpMapping(UINT64 VirtualAddress)
{
    debug_dump_static_mapping(VirtualAddress);
}

/*
 * Arm64ClearIdentityMappings - Clean up identity mappings before kernel handoff
 *
 * FreeLoader creates identity mappings in TTBR0's L0 entries (indices 0-3 for the
 * first 4TB of physical address space) to allow code to run while switching page
 * tables. These mappings occupy user-space virtual address range (0x0000...).
 *
 * The NT kernel expects user-space to be completely unmapped for a new process.
 * If these identity mappings remain, they can:
 * 1. Clash with kernel user-mode address allocations
 * 2. Cause security issues (kernel accessible from user space)
 * 3. Confuse the memory manager's PTE tracking
 *
 * This function clears the TTBR0 L0 entries (user-space page table root) to ensure
 * a clean slate for the kernel. Called just before jumping to the kernel entry point.
 *
 * Note: The kernel code (executing in TTBR1 space) continues to run from kernel
 * virtual addresses (0xFFFF...) which are unaffected by clearing TTBR0.
 */
VOID Arm64ClearIdentityMappings(VOID)
{
    ULONG i;

    if (!page_tables_initialized || !mmu_enabled)
    {
        return;
    }

    if (!arm64_l0_page_table)
    {
        return;
    }


    /*
     * Clear all L0 entries in TTBR0's page table.
     * This removes all user-space identity mappings.
     *
     * ARM64_USER_L1_TABLES defines how many L0 slots we use for user-space
     * identity mappings (typically 4, covering 4TB).
     */
    for (i = 0; i < ARM64_USER_L1_TABLES; i++)
    {
        if (arm64_l0_page_table[i] != 0)
        {
            arm64_l0_page_table[i] = 0;
        }
    }

    /*
     * Memory barriers and TLB invalidation.
     *
     * DSB ISHST: Ensure L0 writes are visible to table walkers
     * TLBI VMALLE1IS: Invalidate all TLB entries for EL1 (both TTBR0 and TTBR1)
     * DSB ISH: Wait for TLB invalidation to complete
     * ISB: Synchronize instruction stream
     */
    ARM64_DSB_ISHST();
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    ARM64_DSB_ISH();
    ARM64_ISB();

    identity_mapping_enabled = FALSE;

}
