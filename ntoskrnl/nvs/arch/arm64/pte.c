/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/arm64/pte.c
 * PURPOSE:     ARM64 page-table entry operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>

#define A64_VALID          (1ULL << 0)
#define A64_TYPE_PAGE      (1ULL << 1)
#define A64_ATTR_SHIFT     2
#define A64_ATTR_NORMAL    (4ULL << A64_ATTR_SHIFT)
#define A64_ATTR_DEVICE    (1ULL << A64_ATTR_SHIFT)
#define A64_ATTR_NOCACHE   (2ULL << A64_ATTR_SHIFT)
#define A64_ATTR_WC        (3ULL << A64_ATTR_SHIFT)
#define A64_AP_USER        (1ULL << 6)
#define A64_AP_READONLY    (1ULL << 7)
#define A64_SH_INNER       (3ULL << 8)
#define A64_AF             (1ULL << 10)
#define A64_NG             (1ULL << 11)
#define A64_DBM            (1ULL << 51)
#define A64_PXN            (1ULL << 53)
#define A64_UXN            (1ULL << 54)
#define A64_SW_WRITE       (1ULL << 55)
#define A64_SW_COPY        (1ULL << 56)
#define A64_FRAME_MASK     0x0000FFFFFFFFF000ULL
#define A64_TABLE_ATTRIBUTES (A64_VALID | A64_TYPE_PAGE | A64_SH_INNER | A64_AF | A64_NG | A64_PXN | A64_UXN | \
                              A64_SW_WRITE)
#define A64_SELF_MAP_INDEX   493
#define A64_HYPERSPACE_INDEX 494

C_ASSERT(A64_TABLE_ATTRIBUTES == 0x00E0000000000F03ULL);

MI_PTE
MiArchPteMakeLeaf(
    _In_ ULONG64 Frame,
    _In_ ULONG Protection,
    _In_ ULONG Flags)
{
    MI_PTE Pte = A64_VALID | A64_TYPE_PAGE | A64_AF | ((Frame << 12) & A64_FRAME_MASK);

    if (Flags & MI_LEAF_DEVICE)
        Pte |= A64_ATTR_DEVICE;
    else if ((Flags & MI_LEAF_NOCACHE) || (Protection & MI_PROT_NOCACHE))
        Pte |= A64_ATTR_NOCACHE | A64_SH_INNER;
    else if (Flags & MI_LEAF_WRITECOMBINE)
        Pte |= A64_ATTR_WC | A64_SH_INNER;
    else
        Pte |= A64_ATTR_NORMAL | A64_SH_INNER;

    if (Flags & MI_LEAF_USER)
        Pte |= A64_AP_USER | A64_NG;
    else if (!(Flags & MI_LEAF_GLOBAL))
        Pte |= A64_NG;

    if (MI_PROT_IS_EXECUTE(Protection))
        Pte |= (Flags & MI_LEAF_USER) ? A64_PXN : A64_UXN;
    else
        Pte |= A64_PXN | A64_UXN;

    if (MI_PROT_IS_WRITABLE(Protection))
    {
        Pte |= A64_SW_WRITE;
        if (Flags & MI_LEAF_HARDWARE_DIRTY)
            Pte |= A64_DBM;
        if (!(Flags & MI_LEAF_DIRTY))
            Pte |= A64_AP_READONLY;
    }
    else
    {
        Pte |= A64_AP_READONLY;
        if (MI_PROT_IS_COPY(Protection))
            Pte |= A64_SW_COPY;
    }

    return Pte;
}

MI_PTE
MiArchPteMakeTable(
    _In_ ULONG64 Frame,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    return A64_TABLE_ATTRIBUTES | ((Frame << 12) & A64_FRAME_MASK);
}

BOOLEAN
MiArchPteIsBlock(_In_ MI_PTE Pte, _In_ ULONG Level)
{
    return Level != 0 && (Pte & (A64_VALID | A64_TYPE_PAGE)) == A64_VALID;
}

MI_PTE
MiArchPteMakeBlock(_In_ ULONG64 Frame, _In_ ULONG Protection, _In_ ULONG Flags)
{
    return MiArchPteMakeLeaf(Frame, Protection, Flags) & ~A64_TYPE_PAGE;
}

BOOLEAN
MiArchPteIsValid(_In_ MI_PTE Pte)
{
    return (Pte & A64_VALID) != 0;
}

ULONG64
MiArchPteFrame(_In_ MI_PTE Pte)
{
    return (Pte & A64_FRAME_MASK) >> 12;
}

BOOLEAN
MiArchPteIsWritable(_In_ MI_PTE Pte)
{
    return (Pte & A64_SW_WRITE) != 0;
}

BOOLEAN
MiArchPteIsCopyOnWrite(_In_ MI_PTE Pte)
{
    return (Pte & A64_SW_COPY) != 0;
}

ULONG
MiArchPteLeafFlags(_In_ MI_PTE Pte)
{
    switch (Pte & (7ULL << A64_ATTR_SHIFT))
    {
        case A64_ATTR_DEVICE:
            return MI_LEAF_DEVICE;
        case A64_ATTR_NOCACHE:
            return MI_LEAF_NOCACHE;
        case A64_ATTR_WC:
            return MI_LEAF_WRITECOMBINE;
        default:
            return 0;
    }
}

BOOLEAN
MiArchPteIsDirty(_In_ MI_PTE Pte)
{
    return (Pte & A64_SW_WRITE) != 0 && (Pte & A64_AP_READONLY) == 0;
}

BOOLEAN
MiArchPteIsAccessed(_In_ MI_PTE Pte)
{
    return (Pte & A64_AF) != 0;
}

BOOLEAN
MiArchPteIsLeafDescriptor(_In_ MI_PTE Pte)
{
    return (Pte & (A64_VALID | A64_TYPE_PAGE)) == (A64_VALID | A64_TYPE_PAGE);
}

BOOLEAN
MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress)
{
    return VirtualAddress >= 0xFFFFF68000000000ULL && VirtualAddress <= 0xFFFFF6FFFFFFFFFFULL;
}

BOOLEAN
MiArchPteIsUser(_In_ MI_PTE Pte)
{
    return (Pte & A64_AP_USER) != 0;
}

BOOLEAN
MiArchPteIsExecutable(_In_ MI_PTE Pte, _In_ BOOLEAN UserMode)
{
    return (Pte & (UserMode ? A64_UXN : A64_PXN)) == 0;
}

MI_PTE
MiArchPteSetDirty(_In_ MI_PTE Pte, _In_ BOOLEAN Dirty)
{
    if (!(Pte & A64_SW_WRITE))
        return Pte;

    return Dirty ? (Pte & ~A64_AP_READONLY) : (Pte | A64_AP_READONLY);
}

MI_PTE
MiArchPteSetAccessed(_In_ MI_PTE Pte, _In_ BOOLEAN Accessed)
{
    return Accessed ? (Pte | A64_AF) : (Pte & ~A64_AF);
}

BOOLEAN
MiArchPteNeedsBreak(_In_ MI_PTE Old, _In_ MI_PTE New)
{
    const MI_PTE Sensitive = A64_FRAME_MASK | (7ULL << A64_ATTR_SHIFT) | A64_SH_INNER | A64_NG | A64_TYPE_PAGE;

    if (!(Old & A64_VALID) || !(New & A64_VALID))
        return FALSE;

    return ((Old ^ New) & Sensitive) != 0;
}

VOID
MiArchInitializeProcessRoot(_In_ ULONG64 SystemRootFrame, _In_ ULONG64 ProcessRootFrame)
{
    PMI_PTE System = MiArchMapFrame(SystemRootFrame);
    PMI_PTE Process = MiArchMapFrame(ProcessRootFrame);
    ULONG Index;

    for (Index = 256; Index < 512; Index++)
    {
        if (Index != A64_SELF_MAP_INDEX && Index != A64_HYPERSPACE_INDEX)
            Process[Index] = System[Index];
    }

    Process[A64_SELF_MAP_INDEX] = MiArchPteMakeTable(ProcessRootFrame, 0);

    MiArchUnmapFrame(Process);
    MiArchUnmapFrame(System);
}
