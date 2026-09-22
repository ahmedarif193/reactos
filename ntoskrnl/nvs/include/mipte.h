/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/mipte.h
 * PURPOSE:     Page-table structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define MI_PROT_NONE              0x00
#define MI_PROT_READONLY          0x01
#define MI_PROT_EXECUTE           0x02
#define MI_PROT_EXECUTE_READ      0x03
#define MI_PROT_READWRITE         0x04
#define MI_PROT_WRITECOPY         0x05
#define MI_PROT_EXECUTE_READWRITE 0x06
#define MI_PROT_EXECUTE_WRITECOPY 0x07
#define MI_PROT_ACCESS_MASK       0x07
#define MI_PROT_NOCACHE           0x08
#define MI_PROT_GUARD             0x10
#define MI_PROT_NOACCESS          0x18
#define MI_PROT_MASK              0x1F

#define MI_PROT_IS_WRITABLE(p)    (((p) & MI_PROT_ACCESS_MASK) == MI_PROT_READWRITE || \
                                   ((p) & MI_PROT_ACCESS_MASK) == MI_PROT_EXECUTE_READWRITE)
#define MI_PROT_IS_COPY(p)        (((p) & MI_PROT_ACCESS_MASK) == MI_PROT_WRITECOPY || \
                                   ((p) & MI_PROT_ACCESS_MASK) == MI_PROT_EXECUTE_WRITECOPY)
#define MI_PROT_IS_EXECUTE(p)     (((p) & MI_PROT_ACCESS_MASK) == MI_PROT_EXECUTE || \
                                   ((p) & MI_PROT_ACCESS_MASK) == MI_PROT_EXECUTE_READ || \
                                   ((p) & MI_PROT_ACCESS_MASK) == MI_PROT_EXECUTE_READWRITE || \
                                   ((p) & MI_PROT_ACCESS_MASK) == MI_PROT_EXECUTE_WRITECOPY)
#define MI_PROT_IS_ACCESSIBLE(p)  (((p) & MI_PROT_ACCESS_MASK) != MI_PROT_NONE && \
                                   ((p) & MI_PROT_NOACCESS) != MI_PROT_NOACCESS)
#define MI_PROT_IS_READABLE(p)    (MI_PROT_IS_ACCESSIBLE(p) && \
                                   ((p) & MI_PROT_ACCESS_MASK) != MI_PROT_EXECUTE)

#define MI_LEAF_USER              0x01
#define MI_LEAF_GLOBAL            0x02
#define MI_LEAF_DIRTY             0x04
#define MI_LEAF_NOCACHE           0x08
#define MI_LEAF_WRITECOMBINE      0x10
#define MI_LEAF_DEVICE            0x20
#define MI_LEAF_HARDWARE_DIRTY    0x40
#define MI_LEAF_PFN_CACHE         0x80 /* Resolve MDL cache attributes per physical page. */
#define MI_LEAF_CACHE_MASK        (MI_LEAF_NOCACHE | MI_LEAF_WRITECOMBINE | MI_LEAF_DEVICE)

typedef ULONG64 MI_PTE, *PMI_PTE;

#define MI_SOFT_KIND_SHIFT        1
#define MI_SOFT_KIND_MASK         0x7ULL
#define MI_SOFT_PROT_SHIFT        4
#define MI_SOFT_PROT_MASK         0x1FULL
#define MI_SOFT_FILE_SHIFT        9
#define MI_SOFT_FILE_MASK         0x7ULL
#define MI_SOFT_VALUE_SHIFT       12

typedef enum _MI_SOFT_KIND
{
    MiSoftNone = 0,
    MiSoftDemandZero,
    MiSoftTransition,
    MiSoftPageFile,
    MiSoftPrototype,
    MiSoftSubsection,
    MiSoftDecommitted,
    MiSoftResident
} MI_SOFT_KIND;

FORCEINLINE
MI_PTE
MiSoftMake(_In_ MI_SOFT_KIND Kind, _In_ ULONG Protection, _In_ ULONG64 Value)
{
    return ((ULONG64)Kind << MI_SOFT_KIND_SHIFT) |
           (((ULONG64)Protection & MI_SOFT_PROT_MASK) << MI_SOFT_PROT_SHIFT) |
           (Value << MI_SOFT_VALUE_SHIFT);
}

FORCEINLINE
MI_SOFT_KIND
MiSoftKind(_In_ MI_PTE Pte)
{
    return (MI_SOFT_KIND)((Pte >> MI_SOFT_KIND_SHIFT) & MI_SOFT_KIND_MASK);
}

FORCEINLINE
BOOLEAN
MiSoftIsUnbacked(_In_ MI_PTE Pte)
{
    MI_PTE Unprotected = Pte & ~(MI_SOFT_PROT_MASK << MI_SOFT_PROT_SHIFT);

    return Pte == 0 || Unprotected == ((MI_PTE)MiSoftDemandZero << MI_SOFT_KIND_SHIFT) ||
           Unprotected == ((MI_PTE)MiSoftDecommitted << MI_SOFT_KIND_SHIFT);
}

FORCEINLINE
ULONG
MiSoftProtection(_In_ MI_PTE Pte)
{
    return (ULONG)((Pte >> MI_SOFT_PROT_SHIFT) & MI_SOFT_PROT_MASK);
}

FORCEINLINE
ULONG64
MiSoftValue(_In_ MI_PTE Pte)
{
    return Pte >> MI_SOFT_VALUE_SHIFT;
}

FORCEINLINE
ULONG
MiSoftFile(_In_ MI_PTE Pte)
{
    return (ULONG)((Pte >> MI_SOFT_FILE_SHIFT) & MI_SOFT_FILE_MASK);
}

FORCEINLINE
MI_PTE
MiSoftWithProtection(_In_ MI_PTE Pte, _In_ ULONG Protection)
{
    return (Pte & ~(MI_SOFT_PROT_MASK << MI_SOFT_PROT_SHIFT)) |
           (((ULONG64)Protection & MI_SOFT_PROT_MASK) << MI_SOFT_PROT_SHIFT);
}

MI_PTE MiArchPteMakeLeaf(_In_ ULONG64 Frame, _In_ ULONG Protection, _In_ ULONG Flags);
MI_PTE MiArchPteMakeTable(_In_ ULONG64 Frame, _In_ ULONG Flags);
BOOLEAN MiArchPteIsValid(_In_ MI_PTE Pte);
BOOLEAN MiArchPteIsBlock(_In_ MI_PTE Pte, _In_ ULONG Level);
MI_PTE MiArchPteMakeBlock(_In_ ULONG64 Frame, _In_ ULONG Protection, _In_ ULONG Flags);
ULONG64 MiArchPteFrame(_In_ MI_PTE Pte);
BOOLEAN MiArchPteIsWritable(_In_ MI_PTE Pte);
BOOLEAN MiArchPteIsCopyOnWrite(_In_ MI_PTE Pte);
ULONG MiArchPteLeafFlags(_In_ MI_PTE Pte);
BOOLEAN MiArchPteIsDirty(_In_ MI_PTE Pte);
BOOLEAN MiArchPteIsAccessed(_In_ MI_PTE Pte);
BOOLEAN MiArchPteIsUser(_In_ MI_PTE Pte);
BOOLEAN MiArchPteIsLeafDescriptor(_In_ MI_PTE Pte);
BOOLEAN MiArchIsSelfMapAddress(_In_ ULONG64 VirtualAddress);
BOOLEAN MiArchPteIsExecutable(_In_ MI_PTE Pte, _In_ BOOLEAN UserMode);
MI_PTE MiArchPteSetDirty(_In_ MI_PTE Pte, _In_ BOOLEAN Dirty);
MI_PTE MiArchPteSetAccessed(_In_ MI_PTE Pte, _In_ BOOLEAN Accessed);
BOOLEAN MiArchPteNeedsBreak(_In_ MI_PTE Old, _In_ MI_PTE New);
VOID MiArchInitializeProcessRoot(_In_ ULONG64 SystemRootFrame, _In_ ULONG64 ProcessRootFrame);

ULONG64 MiArchBootRootFrame(VOID);
VOID MiArchBootClearUserHalf(_In_ ULONG64 RootFrame);
PVOID MiArchMapFrame(_In_ ULONG64 Frame);
VOID MiArchUnmapFrame(_In_ PVOID Mapping);
MI_PTE MiArchPteRead(_In_ PMI_PTE Slot);
VOID MiArchPteWrite(_Inout_ PMI_PTE Slot, _In_ MI_PTE Value);
BOOLEAN MiArchPteCompareExchange(_Inout_ PMI_PTE Slot, _In_ MI_PTE Expected, _In_ MI_PTE Value);
VOID MiArchTlbInvalidate(_In_ ULONG64 VirtualAddress, _In_ ULONG64 PageCount, _In_ BOOLEAN AllProcessors);
VOID MiArchTlbInvalidateAll(_In_ BOOLEAN AllProcessors);
