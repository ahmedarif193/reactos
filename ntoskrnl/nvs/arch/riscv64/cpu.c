/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/riscv64/cpu.c
 * PURPOSE:     RISC-V 64-bit memory manager processor operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>
#include "hardware.h"

BOOLEAN MiRiscvPbmtEnabled;

VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    const RISCV64_LOADER_BLOCK *RiscvBlock = &LoaderBlock->u.Riscv64;

    /* The backend reaches frames through the loader's direct map and adopts
     * its KSEG0 mappings; both windows are fixed by the loader contract. */
    if (RiscvBlock->Kseg0Base != RISCV64_LOADER_KSEG0_BASE ||
        RiscvBlock->DirectMapBase != RISCV64_LOADER_DIRECT_MAP_BASE ||
        RiscvBlock->PhysicalLimit != RISCV64_LOADER_PHYSICAL_LIMIT ||
        RiscvBlock->HighestMappedPhysicalAddress >= RiscvBlock->PhysicalLimit)
    {
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x52564C59, RiscvBlock->Kseg0Base, RiscvBlock->DirectMapBase,
                     RiscvBlock->HighestMappedPhysicalAddress);
    }

    MmSystemRangeStart = (PVOID)(ULONG_PTR)MiArchDescribe()->SystemAddressStart;
    MiRiscvPbmtEnabled = (KiRiscvQueryFeatureFlags() & KI_RISCV_FEATURE_SVPBMT) != 0;
}

PVOID
MiArchMapFrame(_In_ ULONG64 Frame)
{
    MI_ASSERT(Frame < (RISCV64_LOADER_PHYSICAL_LIMIT >> PAGE_SHIFT));
    return MI_RISCV_PFN_TO_VA(Frame);
}

PVOID
MiArchDebugMapFrame(_In_ ULONG64 Frame)
{
    if (Frame >= (RISCV64_LOADER_PHYSICAL_LIMIT >> PAGE_SHIFT))
        return NULL;
    return MiArchMapFrame(Frame);
}

VOID
MiArchUnmapFrame(_In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

MI_PTE
MiArchPteRead(_In_ PMI_PTE Slot)
{
    return __atomic_load_n(Slot, __ATOMIC_ACQUIRE);
}

/* Sv39 orders only leaf entries for an address-specific SFENCE.VMA. Once a
 * table is unlinked or replaced, every hart must drop its cached walks
 * before the table page can be reused. */
static
VOID
MiRiscvCheckTableUnlink(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    if (MiRiscvPteIsTable(Old) && Old != New)
        MiArchInvalidateTlbAll(MiTlbAllProcessors);
}

VOID
MiArchPteWrite(_Inout_ PMI_PTE Slot, _In_ MI_PTE Value)
{
    MI_PTE Old = __atomic_exchange_n(Slot, Value, __ATOMIC_ACQ_REL);

    MiRiscvCheckTableUnlink(Old, Value);
}

BOOLEAN
MiArchPteCompareExchange(_Inout_ PMI_PTE Slot, _In_ MI_PTE Expected, _In_ MI_PTE Value)
{
    MI_PTE Old = Expected;
    BOOLEAN Swapped = (BOOLEAN)__atomic_compare_exchange_n(Slot, &Old, Value, FALSE,
                                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);

    if (Swapped)
        MiRiscvCheckTableUnlink(Expected, Value);

    return Swapped;
}

ULONG64
MiArchBootRootFrame(VOID)
{
    ULONG64 Satp;

    __asm__ __volatile__("csrr %0, satp" : "=r"(Satp));
    return Satp & RISCV64_LOADER_SATP_PPN_MASK;
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

    /* Drop the loader's identity map; its tables stay loader data. */
    for (Index = 0; Index < MI_RISCV_ROOT_KERNEL_INDEX; Index++)
        __atomic_store_n(&Root[Index], 0ULL, __ATOMIC_RELEASE);

    MiArchInvalidateTlbAll(MiTlbAllProcessors);
    MiArchUnmapFrame(Root);
}
