/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Memory Management Internal Definitions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#pragma once

//
// ARM64 Memory Layout
// Based on standard AArch64 48-bit virtual address space
//

// Kernel base address - standard ARM64 kernel space
#define KERNEL_BASE               0xFFFF000000000000ULL
#define MM_SYSTEM_RANGE_START     ((PVOID)KERNEL_BASE)

// User space limit (47-bit user space)
#define USER_SHARED_DATA          0x0000800000000000ULL
#define MM_HIGHEST_USER_ADDRESS   ((PVOID)0x00007FFFFFFFFFFFULL)
#define MM_USER_PROBE_ADDRESS     ((PVOID)0x00007FFFFFF00000ULL)
#define MM_SYSTEM_SPACE_START     MM_SYSTEM_RANGE_START

// Page directory/table definitions for ARM64 4-level paging
#define MM_HIGHEST_VAD_ADDRESS    ((PVOID)0x000007FFFFFFFFFFULL)

// ARM64 uses 4-level page tables (PGD, PUD, PMD, PTE)
#define PTE_PER_PAGE              512
#define PDE_PER_PAGE              512
#define PPE_PER_PAGE              512
#define PXE_PER_PAGE              512

// Page table indices
#define MiGetPxeIndex(va)         ((((ULONG64)(va)) >> 39) & 0x1FF)
#define MiGetPpeIndex(va)         ((((ULONG64)(va)) >> 30) & 0x1FF)
#define MiGetPdeIndex(va)         ((((ULONG64)(va)) >> 21) & 0x1FF)
#define MiGetPteIndex(va)         ((((ULONG64)(va)) >> 12) & 0x1FF)

// Page table entry types are defined in ndk/arm64/mmtypes.h
// Just reference them here

// Page directory entry types (same structure as PTE on ARM64)
#ifndef _MMPDE_DEFINED
#define _MMPDE_DEFINED
typedef MMPTE MMPDE;
typedef PMMPTE PMMPDE;
#endif

#ifndef _MMPPE_DEFINED
#define _MMPPE_DEFINED
typedef MMPTE MMPPE;
typedef PMMPTE PMMPPE;
#endif

#ifndef _MMPXE_DEFINED
#define _MMPXE_DEFINED
typedef MMPTE MMPXE;
typedef PMMPTE PMMPXE;
#endif

// ARM64 TLB flush macros
#define KeFlushCurrentTb() \
    __asm__ __volatile__("dsb sy; tlbi vmalle1; dsb sy; isb" ::: "memory")

#define KeFlushProcessTb() \
    __asm__ __volatile__("dsb sy; tlbi vmalle1; dsb sy; isb" ::: "memory")

/* MI_MAKE_PROTOTYPE_PTE - ARM64 version takes 2 parameters */
#define MI_MAKE_PROTOTYPE_PTE(x, y) do { \
    (x)->u.Long = 0; \
    (x)->u.Proto.Prototype = 1; \
    (x)->u.Proto.ProtoAddress = (ULONG_PTR)(y); \
} while(0)

/* Special IRQL value for memory management critical sections */
#define MM_NOIRQL 0x100

/* EOF */