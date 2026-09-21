/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm64/tlbops.h
 * PURPOSE:     ARM64 translation lookaside buffer operation definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#ifdef MM_HOST_TEST
VOID MiArm64TlbStoreBarrier(VOID);
VOID MiArm64TlbCompleteBarrier(VOID);
VOID MiArm64TlbInstructionBarrier(VOID);
VOID MiArm64TlbPage(ULONG64 Page, MI_TLB_SCOPE Scope);
VOID MiArm64TlbAll(MI_TLB_SCOPE Scope);
#else
FORCEINLINE
VOID MiArm64TlbStoreBarrier(VOID)
{
    __asm__ __volatile__("dsb ishst" ::: "memory");
}

FORCEINLINE
VOID MiArm64TlbCompleteBarrier(VOID)
{
    __asm__ __volatile__("dsb ish" ::: "memory");
}

FORCEINLINE
VOID MiArm64TlbInstructionBarrier(VOID)
{
    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID MiArm64TlbPage(ULONG64 Page, MI_TLB_SCOPE Scope)
{
    if (Scope == MiTlbAllProcessors)
        __asm__ __volatile__("tlbi vaae1is, %0" :: "r"(Page) : "memory");
    else
        __asm__ __volatile__("tlbi vaae1, %0" :: "r"(Page) : "memory");
}

FORCEINLINE
VOID MiArm64TlbAll(MI_TLB_SCOPE Scope)
{
    if (Scope == MiTlbAllProcessors)
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    else
        __asm__ __volatile__("tlbi vmalle1" ::: "memory");
}
#endif
