/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/pte.c
 * PURPOSE:     AMD64 page-table entry operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "hardware.h"

static
BOOLEAN
MiAmd64PteHas(MI_PTE Pte, MI_PTE Bits)
{
    MI_PTE Required = MI_AMD64_PTE_PRESENT | Bits;
    return (Pte & Required) == Required;
}

MI_PTE
MiArchPteMakeLeaf(
    _In_ ULONG64 Frame,
    _In_ ULONG Protection,
    _In_ ULONG Flags)
{
    static const MI_PTE Access[8] =
    {
        0, MI_AMD64_PTE_NX, 0, 0,
        MI_AMD64_PTE_NX | MI_AMD64_PTE_WRITABLE,
        MI_AMD64_PTE_NX | MI_AMD64_PTE_COPY,
        MI_AMD64_PTE_WRITABLE, MI_AMD64_PTE_COPY
    };
    MI_PTE Attributes;

    if (!MI_PROT_IS_ACCESSIBLE(Protection) || (Protection & MI_PROT_GUARD))
        return 0;
    Attributes = Access[Protection & MI_PROT_ACCESS_MASK];
    Attributes |= MI_AMD64_PTE_PRESENT | MI_AMD64_PTE_ACCESSED;
    if (Flags & MI_LEAF_USER)
        Attributes |= MI_AMD64_PTE_USER;
    else if (Flags & MI_LEAF_GLOBAL)
        Attributes |= MI_AMD64_PTE_GLOBAL;
    if ((Flags & (MI_LEAF_DEVICE | MI_LEAF_NOCACHE)) || (Protection & MI_PROT_NOCACHE))
        Attributes |= MI_AMD64_PTE_PCD | MI_AMD64_PTE_PWT;
    else if (Flags & MI_LEAF_WRITECOMBINE)
        Attributes |= MI_AMD64_PTE_PWT;
    if (Attributes & MI_AMD64_PTE_WRITABLE)
    {
        if (Flags & (MI_LEAF_DIRTY | MI_LEAF_HARDWARE_DIRTY))
            Attributes |= MI_AMD64_PTE_WRITE;
        if (Flags & MI_LEAF_DIRTY)
            Attributes |= MI_AMD64_PTE_DIRTY;
    }
    return ((Frame << MI_ARCH_PAGE_SHIFT) & MI_AMD64_PTE_FRAME) | Attributes;
}

MI_PTE
MiArchPteMakeTable(
    _In_ ULONG64 Frame,
    _In_ ULONG Flags)
{
    MI_PTE Attributes = MI_AMD64_PTE_PRESENT | MI_AMD64_PTE_WRITE | MI_AMD64_PTE_ACCESSED;
    return ((Frame << MI_ARCH_PAGE_SHIFT) & MI_AMD64_PTE_FRAME) | Attributes |
           ((Flags & MI_LEAF_USER) ? MI_AMD64_PTE_USER : 0);
}

BOOLEAN
MiArchPteIsBlock(_In_ MI_PTE Pte, _In_ ULONG Level)
{
    return (Level == 1 || Level == 2) && MiAmd64PteHas(Pte, MI_AMD64_PTE_LARGE);
}

MI_PTE
MiArchPteMakeBlock(_In_ ULONG64 Frame, _In_ ULONG Protection, _In_ ULONG Flags)
{
    MI_PTE Leaf = MiArchPteMakeLeaf(Frame, Protection, Flags);
    return Leaf ? Leaf | MI_AMD64_PTE_LARGE : 0;
}

BOOLEAN
MiArchPteIsValid(_In_ MI_PTE Pte)
{
    return MiAmd64PteHas(Pte, 0);
}

ULONG64
MiArchPteFrame(_In_ MI_PTE Pte)
{
    return (Pte & MI_AMD64_PTE_FRAME) >> MI_ARCH_PAGE_SHIFT;
}

BOOLEAN
MiArchPteIsWritable(_In_ MI_PTE Pte)
{
    return MiAmd64PteHas(Pte, MI_AMD64_PTE_WRITABLE);
}

BOOLEAN
MiArchPteIsCopyOnWrite(_In_ MI_PTE Pte)
{
    return MiAmd64PteHas(Pte, MI_AMD64_PTE_COPY);
}

ULONG
MiArchPteLeafFlags(_In_ MI_PTE Pte)
{
    if (MiAmd64PteHas(Pte, MI_AMD64_PTE_PCD))
        return MI_LEAF_NOCACHE;

    if (MiAmd64PteHas(Pte, MI_AMD64_PTE_PWT))
        return MI_LEAF_WRITECOMBINE;

    return 0;
}

BOOLEAN
MiArchPteIsDirty(_In_ MI_PTE Pte)
{
    return MiAmd64PteHas(Pte, MI_AMD64_PTE_DIRTY);
}

BOOLEAN
MiArchPteIsAccessed(_In_ MI_PTE Pte)
{
    return MiAmd64PteHas(Pte, MI_AMD64_PTE_ACCESSED);
}

BOOLEAN
MiArchPteIsLeafDescriptor(_In_ MI_PTE Pte)
{
    return MiAmd64PteHas(Pte, 0);
}

BOOLEAN
MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress)
{
    return VirtualAddress - MI_AMD64_SELF_BASE < MI_AMD64_SELF_BYTES;
}

BOOLEAN
MiArchPteIsUser(_In_ MI_PTE Pte)
{
    return MiAmd64PteHas(Pte, MI_AMD64_PTE_USER);
}

BOOLEAN
MiArchPteIsExecutable(_In_ MI_PTE Pte, _In_ BOOLEAN UserMode)
{
    UNREFERENCED_PARAMETER(UserMode);
    return (Pte & (MI_AMD64_PTE_PRESENT | MI_AMD64_PTE_NX)) == MI_AMD64_PTE_PRESENT;
}

MI_PTE
MiArchPteSetDirty(_In_ MI_PTE Pte, _In_ BOOLEAN Dirty)
{
    if (!MiAmd64PteHas(Pte, MI_AMD64_PTE_WRITABLE))
        return Pte;
    Pte &= ~(MI_AMD64_PTE_WRITE | MI_AMD64_PTE_DIRTY);
    return Pte | (Dirty ? MI_AMD64_PTE_WRITE | MI_AMD64_PTE_DIRTY : 0);
}

MI_PTE
MiArchPteSetAccessed(_In_ MI_PTE Pte, _In_ BOOLEAN Accessed)
{
    if (!MiAmd64PteHas(Pte, 0))
        return Pte;
    return (Pte & ~MI_AMD64_PTE_ACCESSED) | (Accessed ? MI_AMD64_PTE_ACCESSED : 0);
}

BOOLEAN
MiArchPteNeedsBreak(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    UNREFERENCED_PARAMETER(Old);
    UNREFERENCED_PARAMETER(New);
    return FALSE;
}

VOID
MiArchInitializeProcessRoot(_In_ ULONG64 SystemRootFrame, _In_ ULONG64 ProcessRootFrame)
{
    PMI_PTE System = MiArchMapFrame(SystemRootFrame);
    PMI_PTE Process = MiArchMapFrame(ProcessRootFrame);
    ULONG Index;

    MI_ASSERT(SystemRootFrame != ProcessRootFrame);
    for (Index = 0; Index < 512; Index++)
    {
        MI_PTE Value = 0;
        if (Index == MI_AMD64_SELF_INDEX)
            Value = MiArchPteMakeTable(ProcessRootFrame, 0);
        else if (Index >= 256 && Index != MI_AMD64_HYPER_INDEX)
            Value = MiArchPteRead(&System[Index]);
        MiArchPteWrite(&Process[Index], Value);
    }
    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);
}
