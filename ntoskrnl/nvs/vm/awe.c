/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/vm/awe.c
 * PURPOSE:     Address Windowing Extensions memory operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

typedef struct _MI_AWE_PAGE
{
    MI_VAD_NODE Node;
    ULONG64 Va;
    BOOLEAN Pending;
} MI_AWE_PAGE, *PMI_AWE_PAGE;

typedef struct _MI_AWE_MAPPING
{
    MI_VAD_NODE Node;
    PMI_AWE_PAGE Page;
    PMI_PTE Slot;
    ULONG Table;
} MI_AWE_MAPPING, *PMI_AWE_MAPPING;

static PMI_AWE_PAGE
MiAweFindPage(PMI_ADDRESS_SPACE Space, MI_FRAME_NUMBER Frame)
{
    PMI_VAD_NODE Node = MiVadFind(&Space->AweRoot, Frame);
    return Node != NULL ? CONTAINING_RECORD(Node, MI_AWE_PAGE, Node) : NULL;
}

static VOID
MiAweDeletePage(PMI_ADDRESS_SPACE Space, PMI_AWE_PAGE Page)
{
    ULONG Frame = (ULONG)Page->Node.StartingVpn;

    MI_ASSERT(Page->Va == 0);
    MiVadRemove(&Space->AweRoot, &Page->Node);
    MI_FREE(Page);
    MiPfnShareDecrement(&Space->System->Pfn, Frame, TRUE);
    MI_ATOMIC_ADD64(&Space->AwePages, -1);
    MI_ATOMIC_ADD64(&Space->System->AwePages, -1);
}

static BOOLEAN
MiAweClearMapping(PMI_ADDRESS_SPACE Space, ULONG64 Va)
{
    ULONG Table;
    PMI_PTE Slot = MiPtLookup(Space, Va, &Table);
    MI_PTE Pte = Slot != NULL ? MiArchPteRead(Slot) : 0;
    PMI_AWE_PAGE Page;

    if (!MiArchPteIsValid(Pte))
        return FALSE;
    Page = MiAweFindPage(Space, (MI_FRAME_NUMBER)MiArchPteFrame(Pte));
    MI_ASSERT(Page != NULL && Page->Va == Va);
    Page->Va = 0;
    MiArchPteWrite(Slot, 0);
    MI_ATOMIC_ADD32(&Space->System->Pfn.Pfn[Table].UsedEntries, -1);
    return TRUE;
}

NTSTATUS
MiAweAllocatePages(PMI_ADDRESS_SPACE Space, PULONG Count, PMI_FRAME_NUMBER Frames)
{
    ULONG Wanted = *Count, Done = 0;
    NTSTATUS Status = STATUS_NO_MEMORY;

    *Count = 0;
    if (Wanted == 0 || Frames == NULL || Space->IsSystem)
        return STATUS_INVALID_PARAMETER;
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    while (Done < Wanted)
    {
        PMI_AWE_PAGE Page = MI_ALLOCATE(sizeof(*Page));
        ULONG Frame;

        if (Page == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        Frame = MiPfnAllocatePage(&Space->System->Pfn, MI_ALLOCATE_ZEROED);
        if (Frame == MI_FRAME_INVALID)
        {
            MI_FREE(Page);
            break;
        }
        RtlZeroMemory(Page, sizeof(*Page));
        Page->Node.StartingVpn = Page->Node.EndingVpn = Frame;
        if (!MiVadInsert(&Space->AweRoot, &Page->Node))
        {
            MiPfnShareDecrement(&Space->System->Pfn, Frame, TRUE);
            MI_FREE(Page);
            Status = STATUS_CONFLICTING_ADDRESSES;
            break;
        }
        Frames[Done++] = Frame;
    }
    MI_ATOMIC_ADD64(&Space->AwePages, Done);
    MI_ATOMIC_ADD64(&Space->System->AwePages, Done);
    *Count = Done;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Done != 0 ? STATUS_SUCCESS : Status;
}

NTSTATUS
MiAweMapPages(PMI_ADDRESS_SPACE Space, ULONG64 Base, const ULONG64 *Addresses,
              ULONG Count, const MI_FRAME_NUMBER *Frames)
{
    PMI_AWE_MAPPING Map;
    MI_VAD_ROOT Batch;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i, Prepared = 0;
    BOOLEAN Changed = FALSE;
    SIZE_T Bytes = (SIZE_T)Count * sizeof(*Map);

    if (Count == 0 || Space->IsSystem || Bytes / sizeof(*Map) != Count)
        return STATUS_INVALID_PARAMETER;
    if (Addresses == NULL && ((Base & (PAGE_SIZE - 1)) || Base < Space->LowestVa ||
        Base > Space->HighestVa || ((ULONG64)Count << PAGE_SHIFT) - 1 > Space->HighestVa - Base))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Map = MI_ALLOCATE(Bytes);
    if (Map == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Map, Bytes);
    MiVadRootInitialize(&Batch, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    if (Addresses == NULL)
    {
        PMI_VAD Vad = MiVadLocate(Space, Base);
        if (Vad == NULL || Vad->Type != MiVadAwe ||
            (Base >> PAGE_SHIFT) + Count - 1 > Vad->Node.EndingVpn)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto Done;
        }
    }
    for (i = 0; i < Count; i++)
    {
        ULONG64 Va = Addresses != NULL ? Addresses[i] : Base + ((ULONG64)i << PAGE_SHIFT);
        PMI_VAD Vad = MiVadLocate(Space, Va);

        if ((Va & (PAGE_SIZE - 1)) || Vad == NULL || Vad->Type != MiVadAwe)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto Done;
        }
        Map[i].Node.StartingVpn = Map[i].Node.EndingVpn = Va >> PAGE_SHIFT;
        if (!MiVadInsert(&Batch, &Map[i].Node))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Done;
        }
        Prepared++;
        if (Frames != NULL && (Frames[i] != 0 || Addresses == NULL))
        {
            PMI_AWE_PAGE Page = MiAweFindPage(Space, Frames[i]);
            if (Page == NULL || Page->Pending)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Done;
            }
            Page->Pending = TRUE;
            Map[i].Page = Page;
        }
    }
    for (i = 0; i < Count; i++)
    {
        PMI_AWE_PAGE Page = Map[i].Page;
        ULONG64 Va = Map[i].Node.StartingVpn << PAGE_SHIFT;

        if (Page != NULL && Page->Va != 0 && MiVadFind(&Batch, Page->Va >> PAGE_SHIFT) == NULL)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto Done;
        }
        Map[i].Slot = Page != NULL ? MiPtEnsure(Space, Va, &Map[i].Table)
                                   : MiPtLookup(Space, Va, &Map[i].Table);
        if (Page != NULL && Map[i].Slot == NULL)
        {
            Status = STATUS_NO_MEMORY;
            goto Done;
        }
    }
    for (i = 0; i < Count; i++)
        Changed |= MiAweClearMapping(Space, Map[i].Node.StartingVpn << PAGE_SHIFT);
    if (Changed)
        MiArchTlbInvalidateAll(TRUE);
    for (i = 0; i < Count; i++)
    {
        PMI_AWE_PAGE Page = Map[i].Page;
        ULONG64 Va = Map[i].Node.StartingVpn << PAGE_SHIFT;

        if (Page != NULL)
        {
            MiPtWrite(Space, Va, Map[i].Slot, Map[i].Table,
                      MiArchPteMakeLeaf(Page->Node.StartingVpn, MI_PROT_READWRITE, MI_LEAF_USER | MI_LEAF_DIRTY));
            Page->Va = Va;
        }
    }
Done:
    for (i = 0; i < Prepared; i++)
    {
        if (Map[i].Page != NULL)
            Map[i].Page->Pending = FALSE;
        MiPtPruneEmpty(Space, Map[i].Node.StartingVpn << PAGE_SHIFT);
    }
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    MI_FREE(Map);
    return Status;
}

NTSTATUS
MiAweFreePages(PMI_ADDRESS_SPACE Space, PULONG Count, const MI_FRAME_NUMBER *Frames)
{
    ULONG Wanted = *Count, Done = 0, i;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Changed = FALSE;

    *Count = 0;
    if (Wanted == 0 || Frames == NULL)
        return STATUS_INVALID_PARAMETER;
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    while (Done < Wanted)
    {
        PMI_AWE_PAGE Page = MiAweFindPage(Space, Frames[Done]);
        if (Page == NULL || Page->Pending)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        Page->Pending = TRUE;
        Done++;
    }
    for (i = 0; i < Done; i++)
    {
        PMI_AWE_PAGE Page = MiAweFindPage(Space, Frames[i]);
        ULONG64 Va = Page->Va;

        if (Va != 0)
        {
            Changed |= MiAweClearMapping(Space, Va);
            Page->Va = Va;
        }
    }
    if (Changed)
        MiArchTlbInvalidateAll(TRUE);
    for (i = 0; i < Done; i++)
    {
        PMI_AWE_PAGE Page = MiAweFindPage(Space, Frames[i]);
        if (Page->Va != 0)
            MiPtPruneEmpty(Space, Page->Va);
        Page->Va = 0;
        MiAweDeletePage(Space, Page);
    }
    *Count = Done;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

VOID
MiAweReleaseWindowLocked(PMI_ADDRESS_SPACE Space, PMI_VAD Vad)
{
    ULONG64 Start = Vad->Node.StartingVpn << PAGE_SHIFT;
    ULONG64 End = (Vad->Node.EndingVpn + 1) << PAGE_SHIFT;
    ULONG64 Va = Start;
    BOOLEAN Changed = FALSE;

    while (Va < End)
    {
        if (MiPtLookup(Space, Va, NULL) == NULL)
        {
            Va = MiPtNextTableBoundary(Space, Va);
            continue;
        }
        Changed |= MiAweClearMapping(Space, Va);
        Va += PAGE_SIZE;
    }
    if (Changed)
        MiArchTlbInvalidate(Start, (End - Start) >> PAGE_SHIFT, TRUE);
    for (Va = Start; Va < End; Va = MiPtNextTableBoundary(Space, Va))
        MiPtPruneEmpty(Space, Va);
    MiVadRemove(&Space->VadRoot, &Vad->Node);
    MI_FREE(Vad);
}

VOID
MiAweDestroyPagesLocked(PMI_ADDRESS_SPACE Space)
{
    PMI_VAD_NODE Node;
    while ((Node = MiVadFirst(&Space->AweRoot)) != NULL)
        MiAweDeletePage(Space, CONTAINING_RECORD(Node, MI_AWE_PAGE, Node));
}
