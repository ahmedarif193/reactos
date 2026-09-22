/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/pagefile/pagefile.c
 * PURPOSE:     Paging file allocation and management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mm.h>

NTSTATUS
MiPageFileInitialize(
    _Out_ PMI_PAGEFILE PageFile,
    _In_ PMI_PAGEFILE_OPS Ops,
    _In_opt_ PVOID Context,
    _In_ ULONG64 SlotCount)
{
    SIZE_T Words = (SIZE_T)((SlotCount + 63) / 64);

    RtlZeroMemory(PageFile, sizeof(*PageFile));
    PageFile->Bitmap = MI_ALLOCATE(Words * sizeof(ULONG64));
    if (PageFile->Bitmap == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(PageFile->Bitmap, Words * sizeof(ULONG64));
    PageFile->Bitmap[0] = 1;
    PageFile->SlotsInUse = 0;
    PageFile->SlotCount = SlotCount;
    PageFile->Ops = *Ops;
    PageFile->Context = Context;
    MI_SPIN_INIT(&PageFile->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
MiPageFileExtend(
    _Inout_ PMI_PAGEFILE PageFile,
    _In_ ULONG64 SlotCount)
{
    SIZE_T Words = (SIZE_T)((SlotCount + 63) / 64);
    PULONG64 Bitmap;
    PULONG64 Old;
    KIRQL OldIrql;

    Bitmap = MI_ALLOCATE(Words * sizeof(ULONG64));
    if (Bitmap == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Bitmap, Words * sizeof(ULONG64));

    MI_SPIN_ACQUIRE(&PageFile->Lock, &OldIrql);

    if (SlotCount <= PageFile->SlotCount)
    {
        MI_SPIN_RELEASE(&PageFile->Lock, OldIrql);
        MI_FREE(Bitmap);
        return STATUS_SUCCESS;
    }

    RtlCopyMemory(Bitmap, PageFile->Bitmap, (SIZE_T)((PageFile->SlotCount + 63) / 64) * sizeof(ULONG64));
    Old = PageFile->Bitmap;
    PageFile->Bitmap = Bitmap;
    PageFile->SlotCount = SlotCount;

    MI_SPIN_RELEASE(&PageFile->Lock, OldIrql);

    MI_FREE(Old);
    return STATUS_SUCCESS;
}

VOID
MiPageFileUninitialize(
    _Inout_ PMI_PAGEFILE PageFile)
{
    if (PageFile->Bitmap != NULL)
        MI_FREE(PageFile->Bitmap);

    PageFile->Bitmap = NULL;
}

ULONG64
MiPageFileReserveSlot(
    _Inout_ PMI_PAGEFILE PageFile)
{
    ULONG64 Slot = 0;
    ULONG64 Scanned;
    KIRQL OldIrql;

    MI_SPIN_ACQUIRE(&PageFile->Lock, &OldIrql);

    for (Scanned = 0; Scanned < PageFile->SlotCount; Scanned++)
    {
        ULONG64 Candidate = (PageFile->Hint + Scanned) % PageFile->SlotCount;

        if ((PageFile->Bitmap[Candidate >> 6] >> (Candidate & 63)) & 1)
            continue;

        PageFile->Bitmap[Candidate >> 6] |= (ULONG64)1 << (Candidate & 63);
        PageFile->SlotsInUse++;
        PageFile->Hint = Candidate + 1;
        Slot = Candidate;
        break;
    }

    MI_SPIN_RELEASE(&PageFile->Lock, OldIrql);
    return Slot;
}

VOID
MiPageFileReleaseSlot(
    _Inout_ PMI_PAGEFILE PageFile,
    _In_ ULONG64 Slot)
{
    KIRQL OldIrql;

    if (Slot == 0 || Slot >= PageFile->SlotCount)
        return;

    MI_SPIN_ACQUIRE(&PageFile->Lock, &OldIrql);
    MI_ASSERT((PageFile->Bitmap[Slot >> 6] >> (Slot & 63)) & 1);
    PageFile->Bitmap[Slot >> 6] &= ~((ULONG64)1 << (Slot & 63));
    PageFile->SlotsInUse--;
    if (Slot < PageFile->Hint)
        PageFile->Hint = Slot;
    MI_SPIN_RELEASE(&PageFile->Lock, OldIrql);
}
