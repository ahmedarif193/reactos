/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/ppc/pte.c
 * PURPOSE:     Windows NT PowerPC page-table entry encoding
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include "hardware.h"

/* Classic PowerPC pages have no execute permission of their own: every
 * readable page is executable, and write implies read. */
static const MI_PTE MiPpcAccess[8] =
{
    0,
    0,
    0,
    0,
    MI_PPC_PTE_WRITE,
    MI_PPC_PTE_COPYONWRITE,
    MI_PPC_PTE_WRITE,
    MI_PPC_PTE_COPYONWRITE
};

MI_PTE
MiArchPteMakeLeaf(
    _In_ ULONG64 Frame,
    _In_ ULONG Protection,
    _In_ ULONG Flags)
{
    MI_PTE Pte;

    if (!MI_PROT_IS_ACCESSIBLE(Protection) || (Protection & MI_PROT_GUARD))
        return 0;

    Pte = MI_PPC_PTE_VALID | MI_PPC_PTE_ACCESSED | MiPpcAccess[Protection & MI_PROT_ACCESS_MASK] | ((Frame << MI_PPC_PTE_FRAME_SHIFT) & MI_PPC_PTE_FRAME_MASK);
    if (Flags & MI_LEAF_USER)
        Pte |= MI_PPC_PTE_OWNER;
    if ((Pte & MI_PPC_PTE_WRITE) && (Flags & MI_LEAF_DIRTY))
        Pte |= MI_PPC_PTE_DIRTY;
    if (Flags & (MI_LEAF_NOCACHE | MI_LEAF_DEVICE))
        Pte |= MI_PPC_PTE_CACHEDISABLE;
    else if (Flags & MI_LEAF_WRITECOMBINE)
        Pte |= MI_PPC_PTE_WRITETHROUGH;
    return Pte;
}

MI_PTE
MiArchPteMakeTable(
    _In_ ULONG64 Frame,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    return MI_PPC_PTE_VALID | MI_PPC_PTE_WRITE | ((Frame << MI_PPC_PTE_FRAME_SHIFT) & MI_PPC_PTE_FRAME_MASK);
}

BOOLEAN
MiArchPteIsBlock(_In_ MI_PTE Pte, _In_ ULONG Level)
{
    /* No large pages. */
    UNREFERENCED_PARAMETER(Pte);
    UNREFERENCED_PARAMETER(Level);
    return FALSE;
}

MI_PTE
MiArchPteMakeBlock(_In_ ULONG64 Frame, _In_ ULONG Protection, _In_ ULONG Flags)
{
    return MiArchPteMakeLeaf(Frame, Protection, Flags);
}

BOOLEAN
MiArchPteIsValid(_In_ MI_PTE Pte)
{
    return (Pte & MI_PPC_PTE_VALID) != 0;
}

MI_PTE
MiArchPteBlockToPage(MI_PTE Pte, ULONG64 Frame)
{
    return (Pte & ~(MI_PPC_PTE_FRAME_MASK | MI_PPC_PTE_LARGE)) | ((Frame << MI_PPC_PTE_FRAME_SHIFT) & MI_PPC_PTE_FRAME_MASK);
}

MI_PTE
MiArchPteWithCache(MI_PTE Pte, ULONG Flags)
{
    MI_PTE Mask = MI_PPC_PTE_CACHEDISABLE | MI_PPC_PTE_WRITETHROUGH;

    return (Pte & ~Mask) | (MiArchPteMakeLeaf(0, MI_PROT_READWRITE, Flags) & Mask);
}

ULONG64
MiArchPteFrame(_In_ MI_PTE Pte)
{
    return (Pte & MI_PPC_PTE_FRAME_MASK) >> MI_PPC_PTE_FRAME_SHIFT;
}

BOOLEAN
MiArchPteIsWritable(_In_ MI_PTE Pte)
{
    return MiArchPteIsValid(Pte) && (Pte & MI_PPC_PTE_WRITE) != 0;
}

/* The reload refuses a store miss unless the entry carries the write bit,
 * and user pages keep that protection in the hashed table. Kernel pages
 * cannot: once loaded they are supervisor-writable (see KiPpcHashReload),
 * so this reports the protection the entry asks for. */
BOOLEAN
MiArchPteIsHardwareWritable(_In_ MI_PTE Pte)
{
    return MiArchPteIsWritable(Pte);
}

BOOLEAN
MiArchPteIsCopyOnWrite(_In_ MI_PTE Pte)
{
    return MiArchPteIsValid(Pte) && (Pte & MI_PPC_PTE_COPYONWRITE) != 0;
}

ULONG
MiArchPteLeafFlags(_In_ MI_PTE Pte)
{
    if (Pte & MI_PPC_PTE_CACHEDISABLE)
        return MI_LEAF_NOCACHE;
    if (Pte & MI_PPC_PTE_WRITETHROUGH)
        return MI_LEAF_WRITECOMBINE;
    return 0;
}

BOOLEAN
MiArchPteIsDirty(_In_ MI_PTE Pte)
{
    return MiArchPteIsValid(Pte) && ((Pte & (MI_PPC_PTE_WRITE | MI_PPC_PTE_DIRTY)) == (MI_PPC_PTE_WRITE | MI_PPC_PTE_DIRTY));
}

BOOLEAN
MiArchPteIsAccessed(_In_ MI_PTE Pte)
{
    return MiArchPteIsValid(Pte) && (Pte & MI_PPC_PTE_ACCESSED) != 0;
}

BOOLEAN
MiArchPteIsLeafDescriptor(_In_ MI_PTE Pte)
{
    /* Only leaf-level slots are asked. */
    return MiArchPteIsValid(Pte);
}

BOOLEAN
MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress)
{
    /* Page tables are reached through KSEG0, not a recursive mapping. */
    UNREFERENCED_PARAMETER(VirtualAddress);
    return FALSE;
}

BOOLEAN
MiArchPteIsUser(_In_ MI_PTE Pte)
{
    return MiArchPteIsValid(Pte) && (Pte & MI_PPC_PTE_OWNER) != 0;
}

BOOLEAN
MiArchPteIsExecutable(_In_ MI_PTE Pte, _In_ BOOLEAN UserMode)
{
    return MiArchPteIsValid(Pte) && (!UserMode || (Pte & MI_PPC_PTE_OWNER));
}

MI_PTE
MiArchPteSetDirty(_In_ MI_PTE Pte, _In_ BOOLEAN Dirty)
{
    if (!MiArchPteIsValid(Pte) || !(Pte & MI_PPC_PTE_WRITE))
        return Pte;
    return Dirty ? (Pte | MI_PPC_PTE_DIRTY) : (Pte & ~MI_PPC_PTE_DIRTY);
}

MI_PTE
MiArchPteSetAccessed(_In_ MI_PTE Pte, _In_ BOOLEAN Accessed)
{
    if (!MiArchPteIsValid(Pte))
        return Pte;
    return Accessed ? (Pte | MI_PPC_PTE_ACCESSED) : (Pte & ~MI_PPC_PTE_ACCESSED);
}

BOOLEAN
MiArchPteNeedsBreak(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    /* A valid entry may be replaced in place; the caller invalidates the
     * translation (hashed table and TLB) afterwards. */
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

    /* The system root owns a table for both supervisor slots, so sharing
     * the root entries shares all kernel mappings. */
    for (Index = 0; Index < MI_PPC_ROOT_ENTRIES; Index++)
    {
        MI_PTE Value = (Index >= MI_PPC_ROOT_KERNEL_INDEX) ? MiArchPteRead(&System[Index]) : 0;

        MiArchPteWrite(&Process[Index], Value);
    }

    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);

    /* The new address space's VSIDs derive from its root frame. Drop any
     * translation a previous owner of that frame left behind. */
    KiPpcFlushAllTranslations();
}
