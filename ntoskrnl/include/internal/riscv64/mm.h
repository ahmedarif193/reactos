/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RV64 memory-manager definitions: the Sv39 address space, the
 * hardware page-table entry format and the architecture helpers shared by
 * the kernel and the NVS riscv64 backend (ntoskrnl/nvs/arch/riscv64). */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define _MI_PAGING_LEVELS 3
#define _MI_HAS_NO_EXECUTE 1

/* All Sv39 levels share the page-table entry format. */
typedef MMPTE MMPDE, *PMMPDE;
typedef MMPTE MMPPE, *PMMPPE;

/* Sv39 canonical halves. Keep a 64 KiB guard at the top of the user half. */
#define MI_USER_PROBE_ADDRESS           ((PVOID)0x0000003FFFFF0000ULL)
#define MI_DEFAULT_SYSTEM_RANGE_START   ((PVOID)0xFFFFFFC000000000ULL)
#define MI_HIGHEST_SYSTEM_ADDRESS       ((PVOID)0xFFFFFFFFFFFFFFFFULL)
#ifndef MM_LOWEST_USER_ADDRESS
#define MM_LOWEST_USER_ADDRESS          ((PVOID)MM_ALLOCATION_GRANULARITY)
#endif
#define MM_HIGHEST_VAD_ADDRESS          ((PVOID)((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (16 * PAGE_SIZE)))
/* NT's fixed user view of KUSER_SHARED_DATA. */
#define MM_SHARED_USER_DATA_VA          0x7FFE0000UL
/* The 64-bit NT ZeroBits count limit is independent of the Sv39 VA limit. */
#define MI_MAX_ZERO_BITS                53

/* WOW64 compatibility limits; no 32-bit guest ABI is implemented. */
#define MM_HIGHEST_USER_ADDRESS_WOW64   0x7FFEFFFF
#define MM_SYSTEM_RANGE_START_WOW64     0x80000000

/* Private MmAccessFault flags, never raw scause. Trap entry must derive
 * PRESENT from the stabilized translation and WRITE/EXECUTE from the cause. */
#define MI_RISCV_FAULT_PRESENT 0x01
#define MI_RISCV_FAULT_WRITE   0x02
#define MI_RISCV_FAULT_EXECUTE 0x10
#define MI_IS_NOT_PRESENT_FAULT(Code) (!BooleanFlagOn((Code), MI_RISCV_FAULT_PRESENT))
#define MI_IS_WRITE_ACCESS(Code) BooleanFlagOn((Code), MI_RISCV_FAULT_WRITE)
#define MI_IS_INSTRUCTION_FETCH(Code) BooleanFlagOn((Code), MI_RISCV_FAULT_EXECUTE)

/* Sv39 page-table entry. RSW bit 8 marks copy-on-write leaves. PBMT is only
 * meaningful when the platform implements and enables Svpbmt. */
#define MI_RISCV_PTE_VALID       0x001ULL
#define MI_RISCV_PTE_READ        0x002ULL
#define MI_RISCV_PTE_WRITE       0x004ULL
#define MI_RISCV_PTE_EXECUTE     0x008ULL
#define MI_RISCV_PTE_OWNER       0x010ULL
#define MI_RISCV_PTE_GLOBAL      0x020ULL
#define MI_RISCV_PTE_ACCESSED    0x040ULL
#define MI_RISCV_PTE_DIRTY       0x080ULL
#define MI_RISCV_PTE_COPYONWRITE 0x100ULL
#define MI_RISCV_PTE_LEAF_MASK   (MI_RISCV_PTE_READ | MI_RISCV_PTE_WRITE | MI_RISCV_PTE_EXECUTE)
#define MI_RISCV_PTE_PFN_MASK    0x003FFFFFFFFFFC00ULL
#define MI_RISCV_PTE_PFN_SHIFT   10
#define MI_RISCV_PTE_RESERVED    0x1FC0000000000000ULL
#define MI_RISCV_PTE_PBMT_MASK   0x6000000000000000ULL
#define MI_RISCV_PTE_PBMT_NC     0x2000000000000000ULL
#define MI_RISCV_PTE_PBMT_IO     0x4000000000000000ULL
#define MI_RISCV_PTE_NAPOT       0x8000000000000000ULL
#define MI_RISCV_PFN_MAX         0x00000FFFFFFFFFFFULL

/* The loader's direct map reaches every RAM page and page table. */
#define MI_RISCV_PFN_TO_VA(Pfn) \
    ((PVOID)(ULONG_PTR)(RISCV64_LOADER_DIRECT_MAP_BASE + ((ULONG64)(Pfn) << PAGE_SHIFT)))

FORCEINLINE
BOOLEAN
MiRiscvIsCanonicalAddress(_In_ ULONG_PTR Address)
{
    return ((ULONG_PTR)((LONG64)(Address << 25) >> 25) == Address);
}

/* Read-only walk of the live Sv39 tables through the direct map. */
typedef struct _MI_RISCV_PAGE_WALK
{
    PMMPTE Entry;
    MMPTE Value;
    PFN_NUMBER TablePageFrame;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Level;
    BOOLEAN Global;
} MI_RISCV_PAGE_WALK, *PMI_RISCV_PAGE_WALK;

VOID NTAPI MiRiscvInitializePageTableAccess(_In_ PFN_NUMBER HighestTableFrame);

NTSTATUS
NTAPI
MiRiscvWalkPageTables(
    _In_ PFN_NUMBER RootPageFrame,
    _In_ ULONG_PTR VirtualAddress,
    _Out_ PMI_RISCV_PAGE_WALK Result);

NTSTATUS NTAPI MiRiscvWalkCurrentPageTables(_In_ PVOID Address, _Out_ PMI_RISCV_PAGE_WALK Result);

#ifdef __cplusplus
}
#endif
