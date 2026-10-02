/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/ppc/tlb.c
 * PURPOSE:     Windows NT PowerPC translation invalidation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "hardware.h"

/* Ranges above this size flush every translation. */
#define MI_PPC_TLB_RANGE_LIMIT 64

VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    UNREFERENCED_PARAMETER(Scope);
    KiPpcFlushAllTranslations();
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG_PTR Address = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
    SIZE_T Pages;

    UNREFERENCED_PARAMETER(Scope);
    if (Size == 0)
        return;

    if ((Size >= (SIZE_T)PAGE_SIZE * MI_PPC_TLB_RANGE_LIMIT) || (Size > ~(ULONG_PTR)BaseAddress))
    {
        KiPpcFlushAllTranslations();
        return;
    }

    Pages = (Size + ((ULONG_PTR)BaseAddress & (PAGE_SIZE - 1)) + PAGE_SIZE - 1) >> PAGE_SHIFT;
    for (; Pages != 0; Pages--, Address += PAGE_SIZE)
        KiPpcFlushTranslation((PVOID)Address);
}

VOID
MiArchInvalidateTlbSingle(PVOID VirtualAddress, MI_TLB_SCOPE Scope)
{
    MiArchInvalidateTlbRange(VirtualAddress, PAGE_SIZE, Scope);
}

VOID
MiArchTlbInvalidate(ULONG64 VirtualAddress, ULONG64 PageCount, BOOLEAN AllProcessors)
{
    MI_TLB_SCOPE Scope = AllProcessors ? MiTlbAllProcessors : MiTlbLocal;

    if (PageCount > ((SIZE_T)-1 >> PAGE_SHIFT))
        MiArchInvalidateTlbAll(Scope);
    else
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)VirtualAddress, (SIZE_T)(PageCount << PAGE_SHIFT), Scope);
}

VOID
MiArchTlbInvalidateAll(BOOLEAN AllProcessors)
{
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
}
