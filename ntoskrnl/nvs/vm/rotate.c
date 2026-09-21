/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/vm/rotate.c
 * PURPOSE:     Rotatable physical view operations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

static
NTSTATUS
MiRotateLookupLocked(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Size,
    _Out_ PMI_VAD *Rotate)
{
    PMI_VAD Vad = MiVadLocate(Space, VirtualAddress);

    if (Vad == NULL || Vad->Type != MiVadRotate || VirtualAddress + Size < VirtualAddress ||
        VirtualAddress + Size > ((Vad->Node.EndingVpn + 1) << PAGE_SHIFT))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    if (!Vad->MemCommit || Vad->RotateFrames == NULL)
        return STATUS_INVALID_PAGE_PROTECTION;

    *Rotate = Vad;
    return STATUS_SUCCESS;
}

VOID
MiRotateReleaseLocked(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad)
{
    ULONG64 Start = Vad->Node.StartingVpn << PAGE_SHIFT;
    ULONG64 Pages = Vad->Node.EndingVpn - Vad->Node.StartingVpn + 1;
    ULONG64 i;

    for (i = 0; i < Pages; i++)
    {
        ULONG64 Va = Start + (i << PAGE_SHIFT);
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);

        if (Slot != NULL && MiArchPteRead(Slot) != 0)
            MiPtWrite(Space, Va, Slot, TableFrame, 0);
    }

    MiArchTlbInvalidateAll(TRUE);

    for (i = 0; i < Pages; i++)
    {
        if (Vad->RotateFrames != NULL && Vad->RotateFrames[i] != 0)
            MiPfnShareDecrement(&Space->System->Pfn, (ULONG)Vad->RotateFrames[i], TRUE);

        MiPtPruneEmpty(Space, Start + (i << PAGE_SHIFT));
    }

    MiReturnCommit(Space, Vad->CommitCharge);
    MiVadRemove(&Space->VadRoot, &Vad->Node);

    if (Vad->RotateFrames != NULL)
        MI_FREE(Vad->RotateFrames);

    MI_FREE(Vad);
}

NTSTATUS
MiRotatePopulate(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 BaseAddress,
    _In_ ULONG64 RegionSize)
{
    ULONG64 Pages = RegionSize >> PAGE_SHIFT;
    NTSTATUS Status = STATUS_SUCCESS;
    PMI_FRAME_NUMBER Frames;
    PMI_VAD Vad;
    ULONG64 i;

    Frames = MI_ALLOCATE(Pages * sizeof(*Frames));
    if (Frames == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Frames, Pages * sizeof(*Frames));
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Vad = MiVadLocate(Space, BaseAddress);
    if (Vad == NULL || Vad->Type != MiVadRotate || Vad->RotateFrames != NULL ||
        (Vad->Node.StartingVpn << PAGE_SHIFT) != BaseAddress ||
        ((Vad->Node.EndingVpn + 1) << PAGE_SHIFT) != BaseAddress + RegionSize)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        MI_FREE(Frames);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    Vad->RotateFrames = Frames;

    for (i = 0; i < Pages; i++)
    {
        ULONG64 Va = BaseAddress + (i << PAGE_SHIFT);
        ULONG TableFrame;
        PMI_PTE Slot = MiPtEnsure(Space, Va, &TableFrame);
        ULONG Frame;

        if (Slot == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        Frame = MiPfnAllocatePage(&Space->System->Pfn, MI_ALLOCATE_ZEROED);
        if (Frame == MI_FRAME_INVALID)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        MiPfnInitializePage(&Space->System->Pfn, Frame, MiPtSlotAddress(Slot, TableFrame, Va), TableFrame,
                            MiSoftMake(MiSoftDemandZero, Vad->Protection, 0), 0);
        MiPtWrite(Space, Va, Slot, TableFrame,
                  MiArchPteMakeLeaf(Frame, Vad->Protection, MI_LEAF_USER | MI_LEAF_DIRTY));
        Frames[i] = Frame;
    }

    if (!NT_SUCCESS(Status))
        MiRotateReleaseLocked(Space, Vad);

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

NTSTATUS
MiRotateQuery(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Size,
    _Out_ PMI_FRAME_NUMBER Mapped,
    _Out_ PMI_FRAME_NUMBER Regular)
{
    ULONG64 Pages = Size >> PAGE_SHIFT;
    ULONG64 First, i;
    NTSTATUS Status;
    PMI_VAD Vad;

    MI_RW_ACQUIRE_SHARED(&Space->Lock);

    Status = MiRotateLookupLocked(Space, VirtualAddress, Size, &Vad);
    if (NT_SUCCESS(Status))
    {
        First = (VirtualAddress >> PAGE_SHIFT) - Vad->Node.StartingVpn;

        for (i = 0; i < Pages; i++)
        {
            PMI_PTE Slot = MiPtLookup(Space, VirtualAddress + (i << PAGE_SHIFT), NULL);
            MI_PTE Pte = (Slot != NULL) ? MiArchPteRead(Slot) : 0;

            Mapped[i] = MiArchPteIsValid(Pte) ? (MI_FRAME_NUMBER)MiArchPteFrame(Pte) : 0;
            Regular[i] = Vad->RotateFrames[First + i];
        }
    }

    MI_RW_RELEASE_SHARED(&Space->Lock);
    return Status;
}

NTSTATUS
MiRotateApply(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Size,
    _In_opt_ const MI_FRAME_NUMBER *Frames,
    _In_ ULONG LeafFlags)
{
    ULONG64 Pages = Size >> PAGE_SHIFT;
    ULONG64 First, i;
    NTSTATUS Status;
    PMI_VAD Vad;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Status = MiRotateLookupLocked(Space, VirtualAddress, Size, &Vad);
    if (!NT_SUCCESS(Status))
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return Status;
    }

    First = (VirtualAddress >> PAGE_SHIFT) - Vad->Node.StartingVpn;

    for (i = 0; i < Pages; i++)
    {
        ULONG64 Va = VirtualAddress + (i << PAGE_SHIFT);
        ULONG TableFrame;
        PMI_PTE Slot = MiPtLookup(Space, Va, &TableFrame);

        if (Slot != NULL && MiArchPteRead(Slot) != 0)
            MiPtWrite(Space, Va, Slot, TableFrame, 0);
    }

    MiArchTlbInvalidateAll(TRUE);

    for (i = 0; i < Pages; i++)
    {
        ULONG64 Va = VirtualAddress + (i << PAGE_SHIFT);
        MI_FRAME_NUMBER Frame = (Frames != NULL) ? Frames[i] : Vad->RotateFrames[First + i];
        ULONG TableFrame;
        PMI_PTE Slot = MiPtEnsure(Space, Va, &TableFrame);

        if (Slot == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        MiPtWrite(Space, Va, Slot, TableFrame,
                  MiArchPteMakeLeaf(Frame, Vad->Protection,
                                    MI_LEAF_USER | MI_LEAF_DIRTY | ((Frames != NULL) ? LeafFlags : 0)));
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}
