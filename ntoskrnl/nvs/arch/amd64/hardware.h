/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/hardware.h
 * PURPOSE:     AMD64 memory management hardware definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "archdef.h"

#define MI_AMD64_PTE_PRESENT  (1ULL << 0)
#define MI_AMD64_PTE_WRITE    (1ULL << 1)
#define MI_AMD64_PTE_USER     (1ULL << 2)
#define MI_AMD64_PTE_PWT      (1ULL << 3)
#define MI_AMD64_PTE_PCD      (1ULL << 4)
#define MI_AMD64_PTE_ACCESSED (1ULL << 5)
#define MI_AMD64_PTE_DIRTY    (1ULL << 6)
#define MI_AMD64_PTE_LARGE    (1ULL << 7)
#define MI_AMD64_PTE_GLOBAL   (1ULL << 8)
#define MI_AMD64_PTE_COPY     (1ULL << 9)
#define MI_AMD64_PTE_WRITABLE (1ULL << 11)
#define MI_AMD64_PTE_NX       (1ULL << 63)
#define MI_AMD64_PTE_FRAME    0x000FFFFFFFFFF000ULL
#define MI_AMD64_SELF_BASE   (0xFFFF000000000000ULL | ((ULONG64)MI_AMD64_SELF_INDEX << 39))
#define MI_AMD64_SELF_BYTES  (1ULL << 39)

FORCEINLINE
ULONG64
MiAmd64SelfMapSlotAddress(ULONG64 Address, ULONG Level)
{
    ULONG Index;
    ULONG Shift = MI_ARCH_PAGE_SHIFT + 9 * Level;
    ULONG64 Base = MI_AMD64_SELF_BASE;

    MI_ASSERT(Level < MI_ARCH_PAGING_LEVELS);
    for (Index = 1; Index <= Level; Index++)
        Base |= (ULONG64)MI_AMD64_SELF_INDEX << (39 - 9 * Index);
    return Base + (((Address >> Shift) & ((1ULL << (48 - Shift)) - 1)) << 3);
}

FORCEINLINE
BOOLEAN
MiAmd64CanonicalAddress(ULONG64 Address)
{
    return Address < (1ULL << 47) || Address >= 0xFFFF800000000000ULL;
}
