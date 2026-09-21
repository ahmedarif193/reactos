/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm64/tlb.c
 * PURPOSE:     ARM64 translation lookaside buffer maintenance
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "tlbops.h"

VOID
MiArchInvalidateTlbSingle(PVOID VirtualAddress, MI_TLB_SCOPE Scope)
{
    ULONG64 Page = (ULONG64)(ULONG_PTR)VirtualAddress >> PAGE_SHIFT;

    MiArm64TlbStoreBarrier();
    MiArm64TlbPage(Page, Scope);
    MiArm64TlbCompleteBarrier();
    MiArm64TlbInstructionBarrier();
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG_PTR Address = (ULONG_PTR)BaseAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
    SIZE_T Pages;

    if (Size == 0)
        return;

    if (Size >= (SIZE_T)PAGE_SIZE * 64 || Size > ~(ULONG_PTR)BaseAddress)
    {
        MiArchInvalidateTlbAll(Scope);
        return;
    }

    Pages = (Size + ((ULONG_PTR)BaseAddress & (PAGE_SIZE - 1)) + PAGE_SIZE - 1) >> PAGE_SHIFT;
    MiArm64TlbStoreBarrier();

    for (; Pages != 0; Pages--, Address += PAGE_SIZE)
        MiArm64TlbPage((ULONG64)Address >> PAGE_SHIFT, Scope);

    MiArm64TlbCompleteBarrier();
    MiArm64TlbInstructionBarrier();
}

VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    MiArm64TlbStoreBarrier();
    MiArm64TlbAll(Scope);
    MiArm64TlbCompleteBarrier();
    MiArm64TlbInstructionBarrier();
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
