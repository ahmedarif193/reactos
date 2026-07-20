/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Stable circular present-queue removal core
 */

#include "present_queue_core.h"

VOID DxgkPresentLimitCoreInitialize(_Out_ PDXGK_PRESENT_LIMIT_CORE State, _In_ ULONG DefaultLimit)
{
    ASSERT(State != NULL);
    ASSERT(DefaultLimit != 0);
    KeInitializeSpinLock(&State->Lock);
    State->Limit = DefaultLimit;
    State->Reserved = 0;
}

NTSTATUS DxgkPresentLimitCoreSet(_Inout_ PDXGK_PRESENT_LIMIT_CORE State, _In_ ULONG RequestedLimit, _In_ ULONG DefaultLimit, _In_ ULONG MaximumLimit)
{
    KIRQL OldIrql;
    ULONG NewLimit;

    if (State == NULL || DefaultLimit == 0 || MaximumLimit < DefaultLimit)
        return STATUS_INVALID_PARAMETER;
    NewLimit = RequestedLimit != 0 ? RequestedLimit : DefaultLimit;
    if (NewLimit > MaximumLimit)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&State->Lock, &OldIrql);
    State->Limit = NewLimit;
    KeReleaseSpinLock(&State->Lock, OldIrql);
    return STATUS_SUCCESS;
}

BOOLEAN DxgkPresentLimitCoreTryReserve(_Inout_ PDXGK_PRESENT_LIMIT_CORE State)
{
    BOOLEAN Reserved = FALSE;
    KIRQL OldIrql;

    if (State == NULL)
        return FALSE;
    KeAcquireSpinLock(&State->Lock, &OldIrql);
    if (State->Limit != 0 && State->Reserved < State->Limit)
    {
        State->Reserved++;
        Reserved = TRUE;
    }
    KeReleaseSpinLock(&State->Lock, OldIrql);
    return Reserved;
}

VOID DxgkPresentLimitCoreRelease(_Inout_ PDXGK_PRESENT_LIMIT_CORE State)
{
    KIRQL OldIrql;

    if (State == NULL)
        return;
    KeAcquireSpinLock(&State->Lock, &OldIrql);
    ASSERT(State->Reserved != 0);
    if (State->Reserved != 0)
        State->Reserved--;
    KeReleaseSpinLock(&State->Lock, OldIrql);
}

BOOLEAN DxgkPresentLimitCoreIsReached(_Inout_ PDXGK_PRESENT_LIMIT_CORE State)
{
    BOOLEAN Reached;
    KIRQL OldIrql;

    if (State == NULL)
        return FALSE;
    KeAcquireSpinLock(&State->Lock, &OldIrql);
    Reached = State->Limit != 0 && State->Reserved >= State->Limit;
    KeReleaseSpinLock(&State->Lock, OldIrql);
    return Reached;
}

ULONG DxgkPresentLimitCoreGetLimit(_Inout_ PDXGK_PRESENT_LIMIT_CORE State)
{
    KIRQL OldIrql;
    ULONG Limit;

    if (State == NULL)
        return 0;
    KeAcquireSpinLock(&State->Lock, &OldIrql);
    Limit = State->Limit;
    KeReleaseSpinLock(&State->Lock, OldIrql);
    return Limit;
}

ULONG DxgkPresentLimitCoreGetReserved(_Inout_ PDXGK_PRESENT_LIMIT_CORE State)
{
    KIRQL OldIrql;
    ULONG Reserved;

    if (State == NULL)
        return 0;
    KeAcquireSpinLock(&State->Lock, &OldIrql);
    Reserved = State->Reserved;
    KeReleaseSpinLock(&State->Lock, OldIrql);
    return Reserved;
}

BOOLEAN
DxgkPresentQueueCoreRemove(
    _Inout_ PKSPIN_LOCK Lock,
    _Inout_ PVOID Entries,
    _In_ SIZE_T EntrySize,
    _In_ ULONG Capacity,
    _Inout_ PULONG Head,
    _Inout_ PULONG Tail,
    _Inout_ PULONG Count,
    _In_ PDXGK_PRESENT_QUEUE_MATCH Match,
    _In_opt_ PVOID MatchContext,
    _Out_ PVOID RemovedEntry)
{
    PUCHAR EntryBytes = Entries;
    KIRQL OldIrql;
    ULONG Offset;
    ULONG Shift;

    if (Lock == NULL || Entries == NULL || EntrySize == 0 || Capacity == 0 || Head == NULL || Tail == NULL || Count == NULL || Match == NULL || RemovedEntry == NULL)
        return FALSE;

    KeAcquireSpinLock(Lock, &OldIrql);
    if (*Head >= Capacity || *Tail >= Capacity || *Count > Capacity || Capacity > MAXULONG_PTR / EntrySize)
    {
        KeReleaseSpinLock(Lock, OldIrql);
        return FALSE;
    }
    for (Offset = 0; Offset < *Count; ++Offset)
    {
        ULONG Index = (*Head + Offset) % Capacity;
        PVOID Entry = EntryBytes + ((SIZE_T)Index * EntrySize);

        if (!Match(Entry, MatchContext))
            continue;
        RtlCopyMemory(RemovedEntry, Entry, EntrySize);
        for (Shift = Offset; Shift + 1 < *Count; ++Shift)
        {
            ULONG Destination = (*Head + Shift) % Capacity;
            ULONG Source = (*Head + Shift + 1) % Capacity;

            RtlCopyMemory(EntryBytes + ((SIZE_T)Destination * EntrySize), EntryBytes + ((SIZE_T)Source * EntrySize), EntrySize);
        }
        *Tail = (*Tail + Capacity - 1) % Capacity;
        RtlZeroMemory(EntryBytes + ((SIZE_T)*Tail * EntrySize), EntrySize);
        (*Count)--;
        KeReleaseSpinLock(Lock, OldIrql);
        return TRUE;
    }
    KeReleaseSpinLock(Lock, OldIrql);
    return FALSE;
}
