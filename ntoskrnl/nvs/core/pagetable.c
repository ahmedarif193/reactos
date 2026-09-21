/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/core/pagetable.c
 * PURPOSE:     Architecture-neutral page-table management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

FORCEINLINE
ULONG
MiPtIndex(const MI_ARCH_DESCRIPTOR *Arch, ULONG64 VirtualAddress, LONG Level)
{
    return (ULONG)((VirtualAddress >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask);
}

static
PMI_PTE
MiPtWalk(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ BOOLEAN Create,
    _In_ ULONG TargetLevel,
    _In_ ULONG AllocationFlags,
    _Out_opt_ PULONG TableFrame)
{
    PMI_SYSTEM System = Space->System;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    ULONG Frame = Space->RootFrame;
    LONG Level;

    if (TargetLevel >= Arch->PagingLevels)
        return NULL;

    for (Level = Arch->PagingLevels - 1; Level > (LONG)TargetLevel; Level--)
    {
        ULONG Index = MiPtIndex(Arch, VirtualAddress, Level);
        PMI_PTE Slot = (PMI_PTE)MiArchMapFrame(Frame) + Index;
        MI_PTE Entry = MiArchPteRead(Slot);

        if (MiArchPteIsValid(Entry) && MiArchPteIsBlock(Entry, (ULONG)Level))
            return NULL;

        if (!MiArchPteIsValid(Entry))
        {
            ULONG Table;
            KIRQL OldIrql;

            if (!Create || Entry != 0)
                return NULL;

            Table = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED | AllocationFlags);
            if (Table == MI_FRAME_INVALID)
            {
                Entry = MiArchPteRead(Slot);
                if (!MiArchPteIsValid(Entry))
                    return NULL;
            }
            else
            {
                MiPfnInitializePage(&System->Pfn, Table,
                                    ((ULONG64)Frame << PAGE_SHIFT) + (ULONG64)Index * sizeof(MI_PTE),
                                    Frame, 0, MI_PFN_FLAG_PAGE_TABLE);
                System->Pfn.Pfn[Table].PageTableOwner = Space;

                OldIrql = MiPfnLock(&System->Pfn, Frame);
                Entry = MiArchPteRead(Slot);
                if (!MiArchPteIsValid(Entry))
                {
                    Entry = MiArchPteMakeTable(Table, Space->IsSystem ? 0 : MI_LEAF_USER);
                    MI_ATOMIC_ADD32(&System->Pfn.Pfn[Frame].UsedEntries, 1);
                    MI_ATOMIC_ADD64(&Space->PageTablePages, 1);
                    MiArchPteWrite(Slot, Entry);
                    Table = MI_FRAME_INVALID;
                }
                MiPfnUnlock(&System->Pfn, Frame, OldIrql);

                if (Table != MI_FRAME_INVALID)
                    MiPfnShareDecrement(&System->Pfn, Table, TRUE);
            }

            if (MiArchPteIsBlock(Entry, (ULONG)Level))
                return NULL;
        }

        Frame = (ULONG)MiArchPteFrame(Entry);
    }

    if (TableFrame != NULL)
        *TableFrame = Frame;

    return (PMI_PTE)MiArchMapFrame(Frame) + MiPtIndex(Arch, VirtualAddress, TargetLevel);
}

BOOLEAN
MiPtTranslate(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Out_ PULONG64 PhysicalAddress,
    _Out_opt_ PMI_PTE LeafPte)
{
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;
    ULONG64 Frame = Space->RootFrame;
    LONG Level;

    *PhysicalAddress = 0;

    for (Level = Arch->PagingLevels - 1; Level >= 0; Level--)
    {
        PMI_PTE Slot = (PMI_PTE)MiArchMapFrame(Frame) + MiPtIndex(Arch, VirtualAddress, Level);
        MI_PTE Entry = MiArchPteRead(Slot);
        ULONG64 Span;

        if (!MiArchPteIsValid(Entry) || (Level == 0 && !MiArchPteIsLeafDescriptor(Entry)))
            return FALSE;

        if (Level != 0 && !MiArchPteIsBlock(Entry, (ULONG)Level))
        {
            Frame = MiArchPteFrame(Entry);
            continue;
        }

        Span = 1ULL << Arch->Level[Level].Shift;
        *PhysicalAddress = ((MiArchPteFrame(Entry) << PAGE_SHIFT) & ~(Span - 1)) | (VirtualAddress & (Span - 1));

        if (LeafPte != NULL)
            *LeafPte = Entry;

        return TRUE;
    }

    return FALSE;
}

PMI_PTE
MiPtLookup(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Out_opt_ PULONG TableFrame)
{
    return MiPtWalk(Space, VirtualAddress, FALSE, 0, 0, TableFrame);
}

PMI_PTE
MiPtEnsure(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Out_ PULONG TableFrame)
{
    return MiPtWalk(Space, VirtualAddress, TRUE, 0, 0, TableFrame);
}

PMI_PTE
MiPtLookupLevel(PMI_ADDRESS_SPACE Space, ULONG64 VirtualAddress, ULONG Level, PULONG TableFrame)
{
    return MiPtWalk(Space, VirtualAddress, FALSE, Level, 0, TableFrame);
}

PMI_PTE
MiPtEnsureLevel(PMI_ADDRESS_SPACE Space, ULONG64 VirtualAddress, ULONG Level, PULONG TableFrame)
{
    return MiPtWalk(Space, VirtualAddress, TRUE, Level, 0, TableFrame);
}

ULONG64
MiPtSlotAddress(
    _In_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ ULONG64 VirtualAddress)
{
    UNREFERENCED_PARAMETER(VirtualAddress);
    return ((ULONG64)TableFrame << PAGE_SHIFT) + ((ULONG_PTR)Slot & (PAGE_SIZE - 1));
}

BOOLEAN
MiPtVirtualAddressFromSlot(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 SlotAddress,
    _Out_ PULONG64 VirtualAddress)
{
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;
    PMI_PFN_DATABASE Db = &Space->System->Pfn;
    ULONG64 Va = 0;
    ULONG Level;

    for (Level = 0; Level < Arch->PagingLevels; Level++)
    {
        ULONG64 Frame = SlotAddress >> Arch->PageShift;

        Va |= ((SlotAddress & (Arch->PageSize - 1)) / Arch->PteBytes) << Arch->Level[Level].Shift;

        if (Frame == Space->RootFrame)
        {
            if (Level != (ULONG)Arch->PagingLevels - 1)
                return FALSE;

            if (Space->IsSystem)
                Va |= ~((1ULL << Arch->VirtualAddressBits) - 1);

            *VirtualAddress = Va;
            return TRUE;
        }

        if (Frame >= Db->FrameCount || !(MI_PFN_FLAGS(&Db->Pfn[Frame]) & MI_PFN_FLAG_PAGE_TABLE))
            return FALSE;

        SlotAddress = Db->Pfn[Frame].PteAddress;
    }

    return FALSE;
}

ULONG64
MiPtNextTableBoundary(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    ULONG64 Span = 1ULL << Space->System->Arch->Level[1].Shift;

    return (VirtualAddress & ~(Span - 1)) + Span;
}

static
VOID
MiPtReleaseTable(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG TableFrame,
    _In_ ULONG64 VirtualAddress)
{
    PMI_SYSTEM System = Space->System;

    while (TableFrame != Space->RootFrame)
    {
        PMI_PFN Table = &System->Pfn.Pfn[TableFrame];
        ULONG Parent = Table->PteFrame;
        PMI_PTE ParentSlot;
        ULONG i;

        if (MI_ATOMIC_READ32(&Table->UsedEntries) != 0 || (MI_PFN_FLAGS(Table) & MI_PFN_FLAG_PINNED))
            return;

        for (i = 0; i < MI_EMPTY_TABLE_CACHE_SIZE; i++)
        {
            if (Space->EmptyTable[i].Present && Space->EmptyTable[i].Frame == TableFrame)
                Space->EmptyTable[i].Present = FALSE;
        }
        ParentSlot = (PMI_PTE)((PUCHAR)MiArchMapFrame(Parent) + (Table->PteAddress & (PAGE_SIZE - 1)));
        MiArchPteWrite(ParentSlot, 0);
        MI_ATOMIC_ADD64(&Space->PageTablePages, -1);
        MI_ATOMIC_ADD32(&System->Pfn.Pfn[Parent].UsedEntries, -1);
        if (Space->TlbBatch != NULL)
        {
            MiTlbBatchAdd(Space, VirtualAddress, TableFrame, NULL, NULL, TRUE);
        }
        else
        {
            MiArchTlbInvalidate(VirtualAddress, 1, TRUE);
            MiPfnShareDecrement(&System->Pfn, TableFrame, TRUE);
        }
        TableFrame = Parent;
    }
}

VOID
MiPtFlushEmpty(PMI_ADDRESS_SPACE Space)
{
    ULONG i;

    for (i = 0; i < MI_EMPTY_TABLE_CACHE_SIZE; i++)
    {
        PMI_EMPTY_TABLE Entry = &Space->EmptyTable[i];

        if (Entry->Present)
        {
            Entry->Present = FALSE;
            MiPtReleaseTable(Space, Entry->Frame, Entry->Va);
        }
    }
}

VOID
MiPtCacheEmpty(PMI_ADDRESS_SPACE Space, ULONG TableFrame, ULONG64 VirtualAddress)
{
    PMI_PFN Table = &Space->System->Pfn.Pfn[TableFrame];
    PMI_EMPTY_TABLE Entry;
    ULONG i, Free = MI_EMPTY_TABLE_CACHE_SIZE;

    if (MI_ATOMIC_READ32(&Table->UsedEntries) != 0 || (MI_PFN_FLAGS(Table) & MI_PFN_FLAG_PINNED))
        return;
    for (i = 0; i < MI_EMPTY_TABLE_CACHE_SIZE; i++)
    {
        Entry = &Space->EmptyTable[i];
        if (Entry->Present && Entry->Frame == TableFrame)
        {
            Entry->Va = VirtualAddress;
            return;
        }
        if (!Entry->Present && Free == MI_EMPTY_TABLE_CACHE_SIZE)
            Free = i;
    }
    if (Free == MI_EMPTY_TABLE_CACHE_SIZE)
    {
        Free = Space->EmptyTableNext++ % MI_EMPTY_TABLE_CACHE_SIZE;
        Entry = &Space->EmptyTable[Free];
        Entry->Present = FALSE;
        MiPtReleaseTable(Space, Entry->Frame, Entry->Va);
    }
    Entry = &Space->EmptyTable[Free];
    Entry->Frame = TableFrame;
    Entry->Va = VirtualAddress;
    Entry->Present = TRUE;
}

VOID
MiPtPruneEmpty(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    PMI_PFN_DATABASE Db = &Space->System->Pfn;
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;
    ULONG Frame = Space->RootFrame;
    LONG Level;

    for (Level = Arch->PagingLevels - 1; Level >= 1; Level--)
    {
        PMI_PTE Slot = (PMI_PTE)MiArchMapFrame(Frame) + MiPtIndex(Arch, VirtualAddress, Level);
        MI_PTE Pte = MiArchPteRead(Slot);
        ULONG Child;

        if (!MiArchPteIsValid(Pte))
            break;
        if (MiArchPteIsBlock(Pte, (ULONG)Level))
            return;

        Child = (ULONG)MiArchPteFrame(Pte);
        if (Child >= Db->FrameCount || Db->Pfn[Child].PteFrame != Frame ||
            !(MI_PFN_FLAGS(&Db->Pfn[Child]) & MI_PFN_FLAG_PAGE_TABLE))
        {
            return;
        }
        Frame = Child;
    }

    MiPtReleaseTable(Space, Frame, VirtualAddress);
}

VOID
MiPtWrite(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ MI_PTE Value)
{
    PMI_PFN Table = &Space->System->Pfn.Pfn[TableFrame];
    MI_PTE Old = MiArchPteRead(Slot);

    MiArchPteWrite(Slot, Value);

    if (MI_PFN_FLAGS(Table) & MI_PFN_FLAG_PINNED)
        return;

    if (Old == 0 && Value != 0)
    {
        MI_ATOMIC_ADD32(&Table->UsedEntries, 1);
    }
    else if (Old != 0 && Value == 0)
    {
        if (MI_ATOMIC_ADD32(&Table->UsedEntries, -1) - 1 == 0 &&
            (Space->TlbBatch == NULL || !Space->TlbBatch->DeferTables))
            MiPtReleaseTable(Space, TableFrame, VirtualAddress);
    }
}

NTSTATUS
MiPtPinRange(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Length)
{
    ULONG64 End = VirtualAddress + Length;
    ULONG64 Va = VirtualAddress;

    while (Va < End)
    {
        ULONG TableFrame;

        if (MiPtEnsure(Space, Va, &TableFrame) == NULL)
            return STATUS_NO_MEMORY;

        MI_ATOMIC_OR8(&Space->System->Pfn.Pfn[TableFrame].Flags, MI_PFN_FLAG_PINNED);
        Va = MiPtNextTableBoundary(Space, Va);
    }

    return STATUS_SUCCESS;
}

/* System PTE reservations may originate at DISPATCH_LEVEL. Their owner
 * serializes population with a spin lock; do not enter standby reclamation
 * (which can acquire address-space locks) while allocating these tables. */
NTSTATUS
MiPtPinSystemRange(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Length)
{
    ULONG64 End = VirtualAddress + Length;
    ULONG64 Va = VirtualAddress;

    while (Va < End)
    {
        ULONG TableFrame;

        if (MiPtWalk(Space, Va, TRUE, 0, MI_ALLOCATE_NO_RECLAIM, &TableFrame) == NULL)
            return STATUS_NO_MEMORY;

        MI_ATOMIC_OR8(&Space->System->Pfn.Pfn[TableFrame].Flags, MI_PFN_FLAG_PINNED);
        Va = MiPtNextTableBoundary(Space, Va);
    }

    return STATUS_SUCCESS;
}

VOID
MiPtUnpinRange(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Length)
{
    ULONG64 End = VirtualAddress + Length;
    ULONG64 Va = VirtualAddress;

    while (Va < End)
    {
        ULONG TableFrame;

        if (MiPtLookup(Space, Va, &TableFrame) != NULL)
        {
            MI_ATOMIC_AND8(&Space->System->Pfn.Pfn[TableFrame].Flags, (UCHAR)~MI_PFN_FLAG_PINNED);
            MiPtReleaseTable(Space, TableFrame, Va);
        }

        Va = MiPtNextTableBoundary(Space, Va);
    }
}

static
ULONG
MiPtCheckTable(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG Frame,
    _In_ LONG Level,
    _Inout_ PULONG64 Tables)
{
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;
    PMI_PTE Table = MiArchMapFrame(Frame);
    ULONG Entries = (ULONG)Arch->Level[Level].EntryCount;
    ULONG Errors = 0;
    LONG Used = 0;
    ULONG Index;

    for (Index = 0; Index < Entries; Index++)
    {
        MI_PTE Entry = MiArchPteRead(&Table[Index]);

        if (Entry == 0)
            continue;

        if (Level == 0)
        {
            Used++;
            continue;
        }

        if (!MiArchPteIsValid(Entry))
        {
            if (Level == Arch->LargePageLevel && MiSoftKind(Entry) == MiSoftResident &&
                MiSoftValue(Entry) < Space->System->Pfn.FrameCount)
            {
                Used++;
                continue;
            }
            Errors++;
            continue;
        }

        if (MiArchPteIsBlock(Entry, (ULONG)Level))
        {
            Used++;
            continue;
        }

        if (MiArchPteFrame(Entry) >= Space->System->Pfn.FrameCount)
            continue;

        {
            ULONG Child = (ULONG)MiArchPteFrame(Entry);
            PMI_PFN ChildPfn = &Space->System->Pfn.Pfn[Child];

            if (ChildPfn->PteFrame == Frame)
            {
                Used++;
                (*Tables)++;
                if (!(MI_PFN_FLAGS(ChildPfn) & MI_PFN_FLAG_PAGE_TABLE) || ChildPfn->State != MiPageActive)
                    Errors++;
                Errors += MiPtCheckTable(Space, Child, Level - 1, Tables);
            }
        }
    }

    if (!(MI_PFN_FLAGS(&Space->System->Pfn.Pfn[Frame]) & MI_PFN_FLAG_PINNED) &&
        Used != MI_ATOMIC_READ32(&Space->System->Pfn.Pfn[Frame].UsedEntries))
    {
        Errors++;
    }

    return Errors;
}

ULONG
MiPtCheck(
    _In_ PMI_ADDRESS_SPACE Space)
{
    ULONG64 Tables = 0;
    ULONG Errors = MiPtCheckTable(Space, Space->RootFrame, Space->System->Arch->PagingLevels - 1, &Tables);

    if ((LONG64)Tables != MI_ATOMIC_READ64(&Space->PageTablePages))
        Errors++;

    return Errors;
}
