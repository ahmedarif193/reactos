/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/vm/execwrite.c
 * PURPOSE:     Executable-memory write tracking range management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

#define MI_VAD_START(v) ((v)->Node.StartingVpn << PAGE_SHIFT)
#define MI_VAD_END(v) (((v)->Node.EndingVpn + 1) << PAGE_SHIFT)

VOID
MiArmExecutableWriteRangeLocked(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Va = Start & ~((ULONG64)PAGE_SIZE - 1);
    ULONG64 TableSpan = 1ULL << (Space->System->Arch->Level[0].Shift +
                                Space->System->Arch->Level[0].IndexBits);

    while (Va < End)
    {
        PMI_VAD Vad = MiVadLocate(Space, Va);
        ULONG64 VadEnd;
        ULONG TableFrame;
        PMI_PTE Slot;
        MI_PTE Pte;
        KIRQL OldIrql;

        if (Vad == NULL)
        {
            Va += PAGE_SIZE;
            continue;
        }

        VadEnd = MI_VAD_END(Vad);
        if (VadEnd > End)
            VadEnd = End;
        if (Vad->EcCode || Vad->Type == MiVadLarge)
        {
            Va = VadEnd;
            continue;
        }

        while (Va < VadEnd)
        {
            Slot = MiPtLookup(Space, Va, &TableFrame);
            if (Slot == NULL)
            {
                ULONG64 Step = TableSpan - (Va & (TableSpan - 1));

                if (Step >= VadEnd - Va)
                    Va = VadEnd;
                else
                    Va += Step;
                continue;
            }

            OldIrql = MiPfnLock(&Space->System->Pfn, TableFrame);
            Pte = MiArchPteRead(Slot);
            if (MiArchPteIsLeafDescriptor(Pte) && MiArchPteIsWritable(Pte) &&
                MiArchPteIsExecutable(Pte, TRUE) && MiArchPteIsDirty(Pte))
            {
                MiArchPteWrite(Slot, MiArchPteSetDirty(Pte, FALSE));
                MiArchTlbInvalidate(Va, 1, TRUE);
            }
            MiPfnUnlock(&Space->System->Pfn, TableFrame, OldIrql);
            Va += PAGE_SIZE;
        }
    }
}

NTSTATUS
MiSetExecutableWriteTracking(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ BOOLEAN Enable)
{
    PMI_VAD_NODE Node;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    Space->TrackExecutableWrites = Enable;
    if (Enable)
    {
        for (Node = MiVadFirst(&Space->VadRoot); Node != NULL; Node = MiVadNext(Node))
        {
            PMI_VAD Vad = CONTAINING_RECORD(Node, MI_VAD, Node);

            if (!Vad->EcCode)
                MiArmExecutableWriteRangeLocked(Space, MI_VAD_START(Vad), MI_VAD_END(Vad));
        }
    }
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
MiResetExecutableWriteTracking(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size)
{
    ULONG64 Start;
    ULONG64 End;
    ULONG64 Va;

    if (Size == 0 || Base > ~(ULONG64)0 - Size ||
        Base + Size > ~(ULONG64)0 - (PAGE_SIZE - 1))
        return STATUS_INVALID_PARAMETER;

    if (Base < Space->LowestVa || Base + Size - 1 > Space->HighestVa)
        return STATUS_INVALID_PARAMETER;
    Start = Base & ~((ULONG64)PAGE_SIZE - 1);
    End = (Base + Size + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1);

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    if (!Space->TrackExecutableWrites)
    {
        MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
        return STATUS_NOT_SUPPORTED;
    }

    for (Va = Start; Va < End; )
    {
        PMI_VAD Vad = MiVadLocate(Space, Va);

        if (Vad == NULL)
        {
            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
            return STATUS_MEMORY_NOT_ALLOCATED;
        }

        Va = MI_VAD_END(Vad);
        if (Va > End)
            Va = End;
    }

    MiArmExecutableWriteRangeLocked(Space, Start, End);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return STATUS_SUCCESS;
}
