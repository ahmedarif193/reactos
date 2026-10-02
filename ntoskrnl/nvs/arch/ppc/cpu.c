/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/ppc/cpu.c
 * PURPOSE:     Windows NT PowerPC frame access and entry updates
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/nt/mint.h>
#include "hardware.h"

/* 64-bit entries are updated with interrupts off: the real-mode reload runs
 * on this processor and reads only the low word, which a single store
 * publishes last. */

VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    const PPC_LOADER_BLOCK *PpcBlock = &LoaderBlock->u.PowerPC;

    if ((PpcBlock->Kseg0Base != PPC_LOADER_KSEG0_BASE) || (PpcBlock->Kseg0Size != PPC_LOADER_KSEG0_SIZE) || (PpcBlock->HighestMappedPhysicalAddress >= PPC_LOADER_KSEG0_SIZE))
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x50504331, PpcBlock->Kseg0Base, PpcBlock->Kseg0Size, PpcBlock->HighestMappedPhysicalAddress);

    MmSystemRangeStart = (PVOID)(ULONG_PTR)MiArchDescribe()->SystemAddressStart;
}

NTSTATUS
MiArchSetFrameCache(ULONG Frame, ULONG Flags)
{
    UNREFERENCED_PARAMETER(Frame);
    return Flags == 0 ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

ULONG64
MiArchBootFrameAlias(ULONG Frame)
{
    return Frame < MI_PPC_KSEG0_PAGES ? PPC_LOADER_KSEG0_BASE + ((ULONG64)Frame << PAGE_SHIFT) : 0;
}

VOID
MiArchWriteBootPte(PMI_PTE Slot, MI_PTE Value)
{
    KIRQL OldIrql;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    MiArchPteWrite(Slot, Value);
    MiArchTlbInvalidateAll(FALSE);
    KeLowerIrql(OldIrql);
}

PVOID
MiArchMapFrame(_In_ ULONG64 Frame)
{
    MI_ASSERT(Frame < MI_PPC_KSEG0_PAGES);
    return MI_PPC_PFN_TO_VA(Frame);
}

VOID
MiArchUnmapFrame(_In_ PVOID Mapping)
{
    UNREFERENCED_PARAMETER(Mapping);
}

PVOID
MiArchDebugMapFrame(_In_ ULONG64 Frame)
{
    if (Frame >= MI_PPC_KSEG0_PAGES)
        return NULL;
    return MiArchMapFrame(Frame);
}

/* KSEG0 is a block address translation of physical memory with no page
 * tables behind it, so frames reached through MiArchMapFrame translate
 * arithmetically. It behaves as cached, writable, dirty kernel memory. */
BOOLEAN
MiArchTranslateWindow(
    _In_ ULONG64 VirtualAddress,
    _Out_ PULONG64 PhysicalAddress,
    _Out_opt_ PMI_PTE LeafPte)
{
    if (VirtualAddress < PPC_LOADER_KSEG0_BASE || VirtualAddress - PPC_LOADER_KSEG0_BASE >= PPC_LOADER_KSEG0_SIZE)
        return FALSE;

    *PhysicalAddress = VirtualAddress - PPC_LOADER_KSEG0_BASE;
    if (LeafPte != NULL)
        *LeafPte = MiArchPteMakeLeaf(*PhysicalAddress >> PAGE_SHIFT, MI_PROT_READWRITE, MI_LEAF_DIRTY | MI_LEAF_GLOBAL);
    return TRUE;
}

MI_PTE
MiArchPteRead(_In_ PMI_PTE Slot)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    MI_PTE Value = *(volatile MI_PTE *)Slot;

    KeRestoreInterrupts(Interrupts);
    return Value;
}

static
VOID
MiPpcStorePte(_Inout_ PMI_PTE Slot, _In_ MI_PTE Value)
{
    volatile ULONG *Words = (volatile ULONG *)Slot;

    /* Invalidate the low word first so the reload never pairs a new high
     * word with a stale valid low word, then publish the low word last. */
    Words[0] = 0;
    __asm__ __volatile__("eieio" ::: "memory");
    Words[1] = (ULONG)(Value >> 32);
    __asm__ __volatile__("eieio" ::: "memory");
    Words[0] = (ULONG)Value;
    __asm__ __volatile__("sync" ::: "memory");
}

/* A new executable entry for a frame needs the instruction cache made
 * coherent with the data written through another alias. */
static
VOID
MiPpcCheckInstructionSync(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    if (!MiArchPteIsValid(New) || ((Old & MI_PPC_PTE_FRAME_MASK) == (New & MI_PPC_PTE_FRAME_MASK) && MiArchPteIsValid(Old)))
        return;
    if (MiArchPteFrame(New) < MI_PPC_KSEG0_PAGES)
        KeSweepICache(MI_PPC_PFN_TO_VA(MiArchPteFrame(New)), PAGE_SIZE);
}

VOID
MiArchPteWrite(_Inout_ PMI_PTE Slot, _In_ MI_PTE Value)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    MI_PTE Old = *(volatile MI_PTE *)Slot;

    MiPpcStorePte(Slot, Value);
    KeRestoreInterrupts(Interrupts);
    MiPpcCheckInstructionSync(Old, Value);
}

BOOLEAN
MiArchPteCompareExchange(_Inout_ PMI_PTE Slot, _In_ MI_PTE Expected, _In_ MI_PTE Value)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    BOOLEAN Swapped = FALSE;

    if (*(volatile MI_PTE *)Slot == Expected)
    {
        MiPpcStorePte(Slot, Value);
        Swapped = TRUE;
    }
    KeRestoreInterrupts(Interrupts);
    if (Swapped)
        MiPpcCheckInstructionSync(Expected, Value);
    return Swapped;
}

ULONG64
MiArchBootRootFrame(VOID)
{
    return KeGetPcr()->PageTableRoot >> PAGE_SHIFT;
}

/* The reload walks the PCR root, which follows every address-space switch,
 * including the debugger's. */
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

    for (Index = 0; Index < MI_PPC_ROOT_KERNEL_INDEX; Index++)
        MiArchPteWrite(&Root[Index], 0);

    MiArchInvalidateTlbAll(MiTlbAllProcessors);
    MiArchUnmapFrame(Root);
}
