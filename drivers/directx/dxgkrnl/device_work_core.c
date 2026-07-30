/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Generic per-owner accepted-work ledger
 */

#include "device_work_core.h"

static NTSTATUS DxgkpDeviceWorkQueryTerminalLocked(_In_ PDXGK_DEVICE_WORK_LEDGER Ledger)
{
    NTSTATUS Status;

    if (Ledger->Terminal.Destroying != NULL && InterlockedCompareExchange(Ledger->Terminal.Destroying, 0, 0) != 0)
        return NT_SUCCESS(Ledger->Terminal.TerminalStatus) ? STATUS_DEVICE_REMOVED : Ledger->Terminal.TerminalStatus;
    if (Ledger->Terminal.ExecutionState != NULL && InterlockedCompareExchange(Ledger->Terminal.ExecutionState, 0, 0) != Ledger->Terminal.ActiveExecutionState)
        return NT_SUCCESS(Ledger->Terminal.TerminalStatus) ? STATUS_DEVICE_REMOVED : Ledger->Terminal.TerminalStatus;
    if (Ledger->Terminal.Query == NULL)
        return STATUS_SUCCESS;
    Status = Ledger->Terminal.Query(Ledger->Terminal.QueryContext);
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
}

static NTSTATUS DxgkpDeviceWorkWait(_Inout_ PDXGK_DEVICE_WORK_LEDGER Ledger, _In_ ULONGLONG TargetSequence, _Inout_opt_ PKEVENT ArmedEvent)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    if (TargetSequence > Ledger->LastAssignedSequence)
    {
        KeReleaseSpinLock(&Ledger->Lock, OldIrql);
        return STATUS_INVALID_PARAMETER;
    }
    for (;;)
    {
        PDXGK_DEVICE_WORK_ITEM HeadItem = NULL;

        Status = DxgkpDeviceWorkQueryTerminalLocked(Ledger);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseSpinLock(&Ledger->Lock, OldIrql);
            return Status;
        }
        if (!IsListEmpty(&Ledger->OutstandingList))
            HeadItem = CONTAINING_RECORD(Ledger->OutstandingList.Flink, DXGK_DEVICE_WORK_ITEM, ListEntry);
        if (HeadItem == NULL || HeadItem->Sequence > TargetSequence)
        {
            KeReleaseSpinLock(&Ledger->Lock, OldIrql);
            return STATUS_SUCCESS;
        }
        KeClearEvent(&Ledger->ProgressEvent);
        if (ArmedEvent != NULL)
            KeSetEvent(ArmedEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Ledger->Lock, OldIrql);
        Status = KeWaitForSingleObject(&Ledger->ProgressEvent, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    }
}

VOID DxgkDeviceWorkCoreInitializeLedger(_Out_ PDXGK_DEVICE_WORK_LEDGER Ledger, _In_opt_ const DXGK_DEVICE_WORK_TERMINAL_STATE *Terminal)
{
    RtlZeroMemory(Ledger, sizeof(*Ledger));
    KeInitializeSpinLock(&Ledger->Lock);
    InitializeListHead(&Ledger->OutstandingList);
    KeInitializeEvent(&Ledger->ProgressEvent, NotificationEvent, FALSE);
    if (Terminal != NULL)
        Ledger->Terminal = *Terminal;
}

VOID DxgkDeviceWorkCoreInitializeItem(_Out_ PDXGK_DEVICE_WORK_ITEM Item, _In_ PDXGK_DEVICE_WORK_LEDGER Ledger)
{
    RtlZeroMemory(Item, sizeof(*Item));
    InitializeListHead(&Item->ListEntry);
    Item->Ledger = Ledger;
    Item->State = DxgkDeviceWorkItemDormant;
}

NTSTATUS DxgkDeviceWorkCoreActivate(_Inout_ PDXGK_DEVICE_WORK_ITEM Item)
{
    PDXGK_DEVICE_WORK_LEDGER Ledger;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Item == NULL || Item->Ledger == NULL)
        return STATUS_INVALID_PARAMETER;
    Ledger = Item->Ledger;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    if (Item->State == DxgkDeviceWorkItemActive)
    {
        Status = STATUS_SUCCESS;
        goto Exit;
    }
    if (Item->State != DxgkDeviceWorkItemDormant)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Status = DxgkpDeviceWorkQueryTerminalLocked(Ledger);
    if (!NT_SUCCESS(Status))
        goto Exit;
    if (Ledger->LastAssignedSequence == MAXULONGLONG)
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Exit;
    }
    Item->Sequence = ++Ledger->LastAssignedSequence;
    InsertTailList(&Ledger->OutstandingList, &Item->ListEntry);
    Item->State = DxgkDeviceWorkItemActive;
    Status = STATUS_SUCCESS;

Exit:
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
    return Status;
}

VOID DxgkDeviceWorkCoreComplete(_Inout_opt_ PDXGK_DEVICE_WORK_ITEM Item)
{
    PDXGK_DEVICE_WORK_LEDGER Ledger;
    KIRQL OldIrql;

    if (Item == NULL || Item->Ledger == NULL)
        return;
    Ledger = Item->Ledger;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    if (Item->State == DxgkDeviceWorkItemActive)
    {
        RemoveEntryList(&Item->ListEntry);
        InitializeListHead(&Item->ListEntry);
        Item->State = DxgkDeviceWorkItemCompleted;
        KeSetEvent(&Ledger->ProgressEvent, IO_NO_INCREMENT, FALSE);
    }
    else if (Item->State == DxgkDeviceWorkItemDormant)
    {
        Item->State = DxgkDeviceWorkItemCompleted;
    }
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
}

VOID DxgkDeviceWorkCoreNotifyStateChange(_Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger)
{
    KIRQL OldIrql;

    if (Ledger == NULL)
        return;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    KeSetEvent(&Ledger->ProgressEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
}

VOID DxgkDeviceWorkCoreTransitionTerminal(_Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger, _Inout_opt_ volatile LONG *State, _In_ LONG Value)
{
    KIRQL OldIrql;

    if (Ledger == NULL || State == NULL)
        return;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    InterlockedExchange(State, Value);
    KeSetEvent(&Ledger->ProgressEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
}

BOOLEAN
DxgkDeviceWorkCoreTryTransitionTerminal(
    _Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger,
    _Inout_opt_ volatile LONG *State,
    _In_ LONG ExpectedValue,
    _In_ LONG NewValue)
{
    KIRQL OldIrql;
    BOOLEAN Transitioned = FALSE;

    if (Ledger == NULL || State == NULL)
        return FALSE;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    if (InterlockedCompareExchange(State, NewValue, ExpectedValue) ==
        ExpectedValue)
    {
        KeSetEvent(&Ledger->ProgressEvent, IO_NO_INCREMENT, FALSE);
        Transitioned = TRUE;
    }
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
    return Transitioned;
}

NTSTATUS DxgkDeviceWorkCoreCaptureSnapshot(_Inout_ PDXGK_DEVICE_WORK_LEDGER Ledger, _Out_ PDXGK_DEVICE_WORK_SNAPSHOT Snapshot)
{
    KIRQL OldIrql;

    if (Ledger == NULL || Snapshot == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    Snapshot->Ledger = Ledger;
    Snapshot->TargetSequence = Ledger->LastAssignedSequence;
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS DxgkDeviceWorkCoreWaitForSnapshot(_Inout_ PDXGK_DEVICE_WORK_LEDGER Ledger, _In_ const DXGK_DEVICE_WORK_SNAPSHOT *Snapshot, _Inout_opt_ PKEVENT ArmedEvent)
{
    if (Ledger == NULL || Snapshot == NULL || Snapshot->Ledger != Ledger)
        return STATUS_INVALID_PARAMETER;
    return DxgkpDeviceWorkWait(Ledger, Snapshot->TargetSequence, ArmedEvent);
}

NTSTATUS DxgkDeviceWorkCoreWaitForIdle(_Inout_ PDXGK_DEVICE_WORK_LEDGER Ledger, _Inout_opt_ PKEVENT ArmedEvent)
{
    DXGK_DEVICE_WORK_SNAPSHOT Snapshot;
    KIRQL OldIrql;

    PAGED_CODE();
    if (Ledger == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    Snapshot.Ledger = Ledger;
    Snapshot.TargetSequence = Ledger->LastAssignedSequence;
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
    return DxgkpDeviceWorkWait(Ledger, Snapshot.TargetSequence, ArmedEvent);
}

BOOLEAN DxgkDeviceWorkCoreIsEmpty(_Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger)
{
    KIRQL OldIrql;
    BOOLEAN Empty;

    if (Ledger == NULL)
        return TRUE;
    KeAcquireSpinLock(&Ledger->Lock, &OldIrql);
    Empty = IsListEmpty(&Ledger->OutstandingList);
    KeReleaseSpinLock(&Ledger->Lock, OldIrql);
    return Empty;
}
