/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/cpu.c
 * PURPOSE:     AMD64 memory manager processor operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>
#include "hardware.h"
#include "tlb.h"

PVOID
MiArchMapFrame(_In_ ULONG64 Frame)
{
    ASSERT(Frame < (MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT));
    return (PVOID)(ULONG_PTR)(MI_AMD64_DIRECT_BASE + (Frame << PAGE_SHIFT));
}

VOID
MiArchUnmapFrame(_In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

PVOID
MiArchDebugMapFrame(_In_ ULONG64 Frame)
{
    if (Frame >= (MI_AMD64_DIRECT_BYTES >> PAGE_SHIFT))
        return NULL;
    return MiArchMapFrame(Frame);
}

MI_PTE
MiArchPteRead(_In_ PMI_PTE Slot)
{
    return __atomic_load_n(Slot, __ATOMIC_ACQUIRE);
}

VOID
MiArchPteWrite(_Inout_ PMI_PTE Slot, _In_ MI_PTE Value)
{
    __atomic_store_n(Slot, Value, __ATOMIC_RELEASE);
}

BOOLEAN
MiArchPteCompareExchange(_Inout_ PMI_PTE Slot, _In_ MI_PTE Expected, _In_ MI_PTE Value)
{
    return (BOOLEAN)__atomic_compare_exchange_n(Slot, &Expected, Value, FALSE,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

ULONG64
MiArchBootRootFrame(VOID)
{
    return (__readcr3() & MI_AMD64_PTE_FRAME) >> MI_ARCH_PAGE_SHIFT;
}

ULONG64
MiArchDebugRootFrame(_In_ ULONG64 VirtualAddress)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    return MiArchBootRootFrame();
}

VOID
MiArchBootClearUserHalf(_In_ ULONG64 RootFrame)
{
    PMI_PTE Root = MiArchMapFrame(RootFrame);
    ULONG Index;

    for (Index = 0; Index < 256; Index++)
        MiArchPteWrite(&Root[Index], 0);
    MiArchTlbInvalidateAll(TRUE);
    DbgPrint("NVS AMD64: INVPCID=%u, processors=%u, per-target TLB mailboxes\n",
             !!(MiAmd64TlbCapabilities() & MI_AMD64_TLB_INVPCID), KeNumberProcessors);
}
