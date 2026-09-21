/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm64/cpu.c
 * PURPOSE:     ARM64 memory manager processor operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <ntoskrnl.h>
#include <nvs/include/miarch.h>
#include <nvs/include/mienv.h>
#include <nvs/include/mipte.h>
#include "archdef.h"

#define MiArm64Dsb(scope)  __asm__ __volatile__("dsb " #scope ::: "memory")
#define MiArm64Isb()       __asm__ __volatile__("isb" ::: "memory")

VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
}

PVOID
MiArchMapFrame(
    _In_ ULONG64 Frame)
{
    return (PVOID)MI_ARM64_PFN_TO_VA(Frame);
}

VOID
MiArchUnmapFrame(
    _In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

MI_PTE
MiArchPteRead(
    _In_ PMI_PTE Slot)
{
    return __atomic_load_n((volatile ULONG64 *)Slot, __ATOMIC_ACQUIRE);
}

VOID
MiArchPteWrite(
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Value)
{
    MI_PTE Old;

    if (!(Value & 1ULL))
    {
        __atomic_store_n((volatile ULONG64 *)Slot, Value, __ATOMIC_RELEASE);
        return;
    }

    Old = __atomic_load_n((volatile ULONG64 *)Slot, __ATOMIC_RELAXED);
    if ((Old & 1ULL) && MiArchPteNeedsBreak(Old, Value))
    {
        __atomic_store_n((volatile ULONG64 *)Slot, 0ULL, __ATOMIC_RELEASE);
        MiArchInvalidateTlbAll(MiTlbAllProcessors);
    }

    __atomic_store_n((volatile ULONG64 *)Slot, Value, __ATOMIC_RELEASE);

    if (!MiArchPteIsUser(Value))
    {
        MiArm64Dsb(ishst);
        MiArm64Isb();
    }
}

BOOLEAN
MiArchPteCompareExchange(
    _Inout_ PMI_PTE Slot,
    _In_ MI_PTE Expected,
    _In_ MI_PTE Value)
{
    BOOLEAN Swapped = (BOOLEAN)__atomic_compare_exchange_n((volatile ULONG64 *)Slot, &Expected, Value, 0,
                                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);

    if (Swapped && (Value & 1ULL) && !MiArchPteIsUser(Value))
    {
        MiArm64Dsb(ishst);
        MiArm64Isb();
    }

    return Swapped;
}

ULONG64
MiArchBootRootFrame(VOID)
{
    ULONG64 Ttbr1;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    return (Ttbr1 & 0x0000FFFFFFFFF000ULL) >> PAGE_SHIFT;
}

VOID
MiArchBootClearUserHalf(
    _In_ ULONG64 RootFrame)
{
    PMI_PTE Root = (PMI_PTE)MI_ARM64_PFN_TO_VA(RootFrame);
    ULONG Index;

    for (Index = 0; Index < 256; Index++)
        __atomic_store_n((volatile ULONG64 *)&Root[Index], 0ULL, __ATOMIC_RELEASE);

    MiArm64Dsb(ishst);
    MiArchInvalidateTlbAll(MiTlbAllProcessors);
}
