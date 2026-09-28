/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Write-watch tracking for private allocations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <nvs/include/mm.h>

#define MI_VAD_START(v) ((v)->Node.StartingVpn << PAGE_SHIFT)
#define MI_VAD_END(v) (((v)->Node.EndingVpn + 1) << PAGE_SHIFT)
#define MI_PAGE_ALIGN_DOWN(a)   ((a) & ~((ULONG64)PAGE_SIZE - 1))
#define MI_PAGE_ALIGN_UP(a)     (((a) + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1))

static
SIZE_T
MiWriteWatchBytes(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    return (SIZE_T)((((End - Start) >> PAGE_SHIFT) + 7) / 8);
}

NTSTATUS
MiWriteWatchAttach(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad)
{
    SIZE_T Bytes = MiWriteWatchBytes(MI_VAD_START(Vad), MI_VAD_END(Vad));

    Vad->WriteWatchBits = MI_ALLOCATE(Bytes);
    if (Vad->WriteWatchBits == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vad->WriteWatchBits, Bytes);
    Vad->WriteWatchBase = MI_VAD_START(Vad);
    Space->HasWriteWatch = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
MiWriteWatchDuplicate(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad)
{
    PUCHAR Source = Vad->WriteWatchBits;
    SIZE_T Bytes;

    if (Source == NULL)
        return STATUS_SUCCESS;

    Bytes = MiWriteWatchBytes(Vad->WriteWatchBase, MI_VAD_END(Vad));
    Vad->WriteWatchBits = MI_ALLOCATE(Bytes);
    if (Vad->WriteWatchBits == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Vad->WriteWatchBits, Source, Bytes);
    Space->HasWriteWatch = TRUE;
    return STATUS_SUCCESS;
}

VOID
MiWriteWatchRelease(
    _Inout_ PMI_VAD Vad)
{
    if (Vad->WriteWatchBits != NULL)
    {
        MI_FREE(Vad->WriteWatchBits);
        Vad->WriteWatchBits = NULL;
    }
}

VOID
MiWriteWatchNoteWrite(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    ULONG64 Page;
    PMI_VAD Vad;

    MI_RW_ACQUIRE_SHARED(&Space->Lock);
    Vad = MiVadLocate(Space, VirtualAddress);
    if (Vad != NULL && Vad->WriteWatchBits != NULL)
    {
        Page = (MI_PAGE_ALIGN_DOWN(VirtualAddress) - Vad->WriteWatchBase) >> PAGE_SHIFT;
        MI_ATOMIC_OR8(&Vad->WriteWatchBits[Page / 8], 1u << (Page % 8));
    }
    MI_RW_RELEASE_SHARED(&Space->Lock);
}

static
BOOLEAN
MiWriteWatchTestPage(
    _In_ PMI_VAD Vad,
    _In_ ULONG64 Va)
{
    ULONG64 Page = (Va - Vad->WriteWatchBase) >> PAGE_SHIFT;

    return (BOOLEAN)((MI_ATOMIC_READ8(&Vad->WriteWatchBits[Page / 8]) >> (Page % 8)) & 1);
}

static
VOID
MiWriteWatchResetLocked(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Va;
    ULONG64 Page;
    ULONG TableFrame;
    PMI_PTE Slot;
    MI_PTE Pte;
    KIRQL OldIrql;

    for (Va = Start; Va < End; Va += PAGE_SIZE)
    {
        Page = (Va - Vad->WriteWatchBase) >> PAGE_SHIFT;
        MI_ATOMIC_AND8(&Vad->WriteWatchBits[Page / 8], ~(1u << (Page % 8)));

        Slot = MiPtLookup(Space, Va, &TableFrame);
        if (Slot == NULL)
            continue;

        OldIrql = MiPfnLock(&Space->System->Pfn, TableFrame);
        Pte = MiArchPteRead(Slot);
        if (MiArchPteIsLeafDescriptor(Pte) && MiArchPteIsWritable(Pte) && MiArchPteIsDirty(Pte))
        {
            MiArchPteWrite(Slot, MiArchPteSetDirty(Pte, FALSE));
            MiArchTlbInvalidate(Va, 1, TRUE);
        }
        MiPfnUnlock(&Space->System->Pfn, TableFrame, OldIrql);
    }
}

static
NTSTATUS
MiWriteWatchRange(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _Out_ PULONG64 Start,
    _Out_ PULONG64 End,
    _Out_ PMI_VAD *Vad)
{
    if (Size == 0 || Base > ~(ULONG64)0 - Size || Base + Size > ~(ULONG64)0 - (PAGE_SIZE - 1))
        return STATUS_INVALID_PARAMETER;

    *Start = MI_PAGE_ALIGN_DOWN(Base);
    *End = MI_PAGE_ALIGN_UP(Base + Size);
    *Vad = MiVadLocate(Space, *Start);
    if (*Vad == NULL || (*Vad)->WriteWatchBits == NULL || *End > MI_VAD_END(*Vad))
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

NTSTATUS
MiGetWriteWatch(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _In_ BOOLEAN Reset,
    _Out_ PULONG64 Addresses,
    _Inout_ PULONG64 Count)
{
    ULONG64 Start, End, Va;
    ULONG64 Found = 0;
    PMI_VAD Vad;
    NTSTATUS Status;

    if (*Count == 0)
        return STATUS_INVALID_PARAMETER;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Status = MiWriteWatchRange(Space, Base, Size, &Start, &End, &Vad);
    if (NT_SUCCESS(Status))
    {
        for (Va = Start; Found < *Count && Va < End; Va += PAGE_SIZE)
        {
            if (MiWriteWatchTestPage(Vad, Va))
                Addresses[Found++] = Va;
        }

        if (Reset)
            MiWriteWatchResetLocked(Space, Vad, Start, Va);

        *Count = Found;
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

NTSTATUS
MiResetWriteWatch(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size)
{
    ULONG64 Start, End;
    PMI_VAD Vad;
    NTSTATUS Status;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Status = MiWriteWatchRange(Space, Base, Size, &Start, &End, &Vad);
    if (NT_SUCCESS(Status))
        MiWriteWatchResetLocked(Space, Vad, Start, End);

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}
