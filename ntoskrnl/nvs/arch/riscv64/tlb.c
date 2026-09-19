/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/riscv64/tlb.c
 * PURPOSE:     RISC-V 64-bit translation cache maintenance
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "hardware.h"

/* Ranges above this size flush the whole translation cache. */
#define MI_RISCV_TLB_RANGE_LIMIT 64

static
VOID
MiRiscvFlushRemote(VOID)
{
    /* The boot hart is the only processor; remote shootdown is added with
     * the secondary harts. */
    if (KeNumberProcessors > 1)
        KeFlushEntireTb(TRUE, TRUE);
}

VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    __asm__ __volatile__("sfence.vma zero, zero" ::: "memory");

    if (Scope == MiTlbAllProcessors)
        MiRiscvFlushRemote();
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG_PTR Address = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
    SIZE_T Pages;

    if (Size == 0)
        return;

    if (Size >= (SIZE_T)PAGE_SIZE * MI_RISCV_TLB_RANGE_LIMIT || Size > ~(ULONG_PTR)BaseAddress)
    {
        MiArchInvalidateTlbAll(Scope);
        return;
    }

    Pages = (Size + ((ULONG_PTR)BaseAddress & (PAGE_SIZE - 1)) + PAGE_SIZE - 1) >> PAGE_SHIFT;

    /* SFENCE.VMA orders this hart's earlier page-table stores itself. */
    for (; Pages != 0; Pages--, Address += PAGE_SIZE)
        __asm__ __volatile__("sfence.vma %0, zero" :: "r"(Address) : "memory");

    if (Scope == MiTlbAllProcessors)
        MiRiscvFlushRemote();
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
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)VirtualAddress,
                                 (SIZE_T)(PageCount << PAGE_SHIFT), Scope);
}

VOID
MiArchTlbInvalidateAll(BOOLEAN AllProcessors)
{
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
}
