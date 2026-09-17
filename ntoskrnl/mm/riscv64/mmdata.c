/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Native RV64 PTE templates and protection translation data
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>

/* MM must supply a real writable kernel alias, not a dummy data page. */
KUSER_SHARED_DATA *SharedUserData;

/* Templates only: PFN assignment, ownership and publication remain MM work.
 * Intermediate table descriptors must not have leaf R/W/X or reserved A/D/U.
 * Initial data leaves are writable, non-executable, and not global. */
MMPDE ValidKernelPde = {{PTE_VALID}};
MMPDE ValidKernelPdeLocal = {{PTE_VALID}};
MMPPE ValidKernelPpe = {{PTE_VALID}};
MMPTE ValidKernelPte = {{PTE_VALID | PTE_READWRITE | PTE_ACCESSED | PTE_DIRTY}};
MMPTE ValidKernelPteLocal = {{PTE_VALID | PTE_READWRITE | PTE_ACCESSED | PTE_DIRTY}};
MMPTE DemandZeroPte = {{(ULONG64)MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS}};
MMPTE MmDecommittedPte = {{(ULONG64)MM_DECOMMIT << MM_PTE_SOFTWARE_PROTECTION_BITS}};
MMPTE PrototypePte = {{((ULONG64)MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS) |
                       MI_RISCV_PTE_PROTOTYPE | (MI_RISCV_PTE_LOOKUP_NEEDED << 32)}};

/* Hardware access bits only. The caller must reject unsupported cache modes
 * and retain guard/no-access states as invalid software entries. */
#define RISCV_ACCESS_ROW \
    0, PTE_READONLY, PTE_EXECUTE, PTE_EXECUTE_READ, PTE_READWRITE, \
    PTE_WRITECOPY, PTE_EXECUTE_READWRITE, PTE_EXECUTE_WRITECOPY
const ULONG_PTR MmProtectToPteMask[32] =
{
    RISCV_ACCESS_ROW,
    RISCV_ACCESS_ROW,
    RISCV_ACCESS_ROW,
    RISCV_ACCESS_ROW
};

#define RISCV_PROTECTION_ROW(Modifier) \
    PAGE_NOACCESS, PAGE_READONLY | (Modifier), PAGE_EXECUTE | (Modifier), \
    PAGE_EXECUTE_READ | (Modifier), PAGE_READWRITE | (Modifier), \
    PAGE_WRITECOPY | (Modifier), PAGE_EXECUTE_READWRITE | (Modifier), \
    PAGE_EXECUTE_WRITECOPY | (Modifier)
const ULONG MmProtectToValue[32] =
{
    RISCV_PROTECTION_ROW(0),
    RISCV_PROTECTION_ROW(PAGE_NOCACHE),
    RISCV_PROTECTION_ROW(PAGE_GUARD),
    RISCV_PROTECTION_ROW(PAGE_WRITECOMBINE)
};
