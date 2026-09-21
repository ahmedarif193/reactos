/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/ws/trim.c
 * PURPOSE:     Working-set trimming and page reclamation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

static
BOOLEAN
MiTrimPage(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ BOOLEAN Aggressive)
{
    PMI_SYSTEM System = Space->System;
    MI_PTE Pte = MiArchPteRead(Slot);
    PMI_PFN Entry;
    ULONG Frame;

    if (!MiArchPteIsValid(Pte))
        return FALSE;

    Frame = (ULONG)MiArchPteFrame(Pte);
    Entry = &System->Pfn.Pfn[Frame];

    if (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PAGE_TABLE)
        return FALSE;

    if (!Aggressive && MiArchPteIsAccessed(Pte))
    {
        MiArchPteWrite(Slot, MiArchPteSetAccessed(Pte, FALSE));
        MiArchTlbInvalidate(VirtualAddress, 1, TRUE);
        return FALSE;
    }

    if (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE)
        return MiTrimPrototypePage(Space, Vad, VirtualAddress, Slot, TableFrame);

    UNREFERENCED_PARAMETER(TableFrame);

    for (;;)
    {
        MI_PTE Transition = MiSoftMake(MiSoftTransition, MiSoftProtection(Entry->OriginalPte), Frame);

        if (MiArchPteCompareExchange(Slot, Pte, Transition))
            break;

        Pte = MiArchPteRead(Slot);
    }

    MiArchTlbInvalidate(VirtualAddress, 1, TRUE);

    if (MiArchPteIsDirty(Pte))
        MiPfnSetModified(&System->Pfn, Frame);

    MiPfnShareDecrement(&System->Pfn, Frame, FALSE);
    MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
    return TRUE;
}

ULONG
MiTrimAddressSpace(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG PageTarget,
    _In_ BOOLEAN Aggressive)
{
    ULONG Trimmed = 0;
    ULONG64 Origin;
    ULONG Pass;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    MiPtFlushEmpty(Space);
    Origin = Space->TrimCursor;

    for (Pass = 0; Pass < 2 && Trimmed < PageTarget; Pass++)
    {
        ULONG64 Limit = (Pass == 0) ? ~0ULL : Origin;
        PMI_VAD_NODE Node;

        for (Node = MiVadFirst(&Space->VadRoot); Node != NULL && Trimmed < PageTarget; Node = MiVadNext(Node))
        {
            PMI_VAD Vad = CONTAINING_RECORD(Node, MI_VAD, Node);
            ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
            ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

            if (End <= Space->TrimCursor || MI_VAD_IS_DIRECT(Vad))
                continue;

            if (Va >= Limit)
                break;

            if (Va < Space->TrimCursor)
                Va = Space->TrimCursor;

            if (End > Limit)
                End = Limit;

            while (Va < End && Trimmed < PageTarget)
            {
                ULONG TableFrame;
                PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);

                if (Slot == NULL)
                {
                    Va = MiPtNextTableBoundary(Space, Va);
                    continue;
                }

                if (MiTrimPage(Space, Vad, Va, Slot, TableFrame, Aggressive))
                    Trimmed++;

                Va += PAGE_SIZE;
            }

            Space->TrimCursor = (Va < End) ? Va : End;
        }

        if (Trimmed < PageTarget)
        {
            if (Pass == 0)
                Space->TrimCursor = 0;
            else
                Space->TrimCursor = Origin;
        }
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Trimmed;
}

ULONG
MiWriteModifiedPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG MaximumPages)
{
    PMI_PAGEFILE PageFile = System->PageFile;
    ULONG Written = 0;
    ULONG Attempts = 0;

    while (Written < MaximumPages && Attempts++ < MaximumPages)
    {
        ULONG Frame = MiPfnTakeModified(&System->Pfn);
        PMI_PFN Entry;
        ULONG64 Slot;
        BOOLEAN Reused;
        NTSTATUS Status;
        PVOID Mapping;

        if (Frame == MI_FRAME_INVALID)
            break;

        Entry = &System->Pfn.Pfn[Frame];

        if ((MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE) && MiSoftKind(Entry->OriginalPte) == MiSoftSubsection)
        {
            MiWritePrototypePage(System, Frame);
            Written++;
            continue;
        }

        if (PageFile == NULL)
        {
            MiPfnWriteComplete(&System->Pfn, Frame, 0, FALSE);
            continue;
        }

        Reused = (BOOLEAN)(MiSoftKind(Entry->OriginalPte) == MiSoftPageFile);
        Slot = Reused ? MiSoftValue(Entry->OriginalPte) : MiPageFileReserveSlot(PageFile);
        if (Slot == 0)
        {
            MiPfnWriteComplete(&System->Pfn, Frame, 0, FALSE);
            break;
        }

        Mapping = MiArchMapFrame(Frame);
        Status = PageFile->Ops.Write(PageFile->Context, Slot, Mapping);
        MiArchUnmapFrame(Mapping);

        if (!NT_SUCCESS(Status))
        {
            if (MiPfnWriteComplete(&System->Pfn, Frame, 0, FALSE) || !Reused)
                MiPageFileReleaseSlot(PageFile, Slot);

            break;
        }

        MI_ATOMIC_ADD64(&PageFile->PagesWritten, 1);
        if (MiPfnWriteComplete(&System->Pfn, Frame,
                               MiSoftMake(MiSoftPageFile, MiSoftProtection(Entry->OriginalPte), Slot), TRUE))
        {
            MiPageFileReleaseSlot(PageFile, Slot);
        }

        Written++;
    }

    return Written;
}
