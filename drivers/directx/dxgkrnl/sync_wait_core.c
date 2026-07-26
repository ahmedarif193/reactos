/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Nonpaged monitored-fence CPU wait registry core
 */

#include "sync_wait_core.h"

#define DXGK_SYNC_WAIT_STATE_INITIAL 0
#define DXGK_SYNC_WAIT_STATE_REGISTERED 1
#define DXGK_SYNC_WAIT_STATE_COMPLETED 2

static BOOLEAN
DxgkpSyncWaitConditionSatisfiedLocked(
    _In_ PDXGK_SYNC_WAIT_CORE_REQUEST Request)
{
    ULONG Index;

    for (Index = 0; Index < Request->TargetCount; ++Index)
    {
        BOOLEAN Reached = (UINT64)*Request->Targets[Index].FenceValue >= Request->Targets[Index].TargetValue;

        if (Request->WaitAny && Reached)
            return TRUE;
        if (!Request->WaitAny && !Reached)
            return FALSE;
    }
    return Request->WaitAny ? FALSE : TRUE;
}

static VOID
DxgkpSyncWaitCompleteList(
    _Inout_ PLIST_ENTRY CompletionList)
{
    while (!IsListEmpty(CompletionList))
    {
        PDXGK_SYNC_WAIT_CORE_REQUEST Request = CONTAINING_RECORD(RemoveHeadList(CompletionList), DXGK_SYNC_WAIT_CORE_REQUEST, RegistryEntry);

        InitializeListHead(&Request->RegistryEntry);
        Request->CompletionRoutine(Request, Request->CompletionStatus, Request->CompletionContext);
    }
}

static VOID
DxgkpSyncWaitMoveCompletedLocked(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REQUEST Request,
    _In_ NTSTATUS Status,
    _Inout_ PLIST_ENTRY CompletionList)
{
    ASSERT(Request->State == DXGK_SYNC_WAIT_STATE_REGISTERED);
    RemoveEntryList(&Request->RegistryEntry);
    Request->State = DXGK_SYNC_WAIT_STATE_COMPLETED;
    Request->CompletionStatus = Status;
    InsertTailList(CompletionList, &Request->RegistryEntry);
}

static VOID
DxgkpSyncWaitCollectSatisfiedLocked(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _Inout_ PLIST_ENTRY CompletionList)
{
    PLIST_ENTRY Entry = Registry->RequestList.Flink;

    while (Entry != &Registry->RequestList)
    {
        PDXGK_SYNC_WAIT_CORE_REQUEST Request = CONTAINING_RECORD(Entry, DXGK_SYNC_WAIT_CORE_REQUEST, RegistryEntry);
        PLIST_ENTRY Next = Entry->Flink;

        if (DxgkpSyncWaitConditionSatisfiedLocked(Request))
            DxgkpSyncWaitMoveCompletedLocked(Request, STATUS_SUCCESS, CompletionList);
        Entry = Next;
    }
}

VOID
DxgkSyncWaitCoreInitializeRegistry(
    _Out_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry)
{
    RtlZeroMemory(Registry, sizeof(*Registry));
    KeInitializeSpinLock(&Registry->Lock);
    InitializeListHead(&Registry->RequestList);
    Registry->ShutdownStatus = STATUS_DEVICE_REMOVED;
}

VOID
DxgkSyncWaitCoreInitializeRequest(
    _Out_ PDXGK_SYNC_WAIT_CORE_REQUEST Request,
    _In_reads_(TargetCount) PDXGK_SYNC_WAIT_CORE_TARGET Targets,
    _In_ ULONG TargetCount,
    _In_ BOOLEAN WaitAny,
    _In_opt_ PDXGK_SYNC_WAIT_CORE_ADMISSION AdmissionRoutine,
    _In_ PDXGK_SYNC_WAIT_CORE_COMPLETION CompletionRoutine,
    _In_opt_ PVOID CompletionContext)
{
    RtlZeroMemory(Request, sizeof(*Request));
    InitializeListHead(&Request->RegistryEntry);
    Request->WaitAny = WaitAny;
    Request->TargetCount = TargetCount;
    Request->Targets = Targets;
    Request->AdmissionRoutine = AdmissionRoutine;
    Request->CompletionRoutine = CompletionRoutine;
    Request->CompletionContext = CompletionContext;
}

NTSTATUS
DxgkSyncWaitCoreRegister(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _Inout_ PDXGK_SYNC_WAIT_CORE_REQUEST Request)
{
    KIRQL OldIrql;
    BOOLEAN CompleteNow;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Registry == NULL || Request == NULL || Request->Targets == NULL || Request->TargetCount == 0 || Request->CompletionRoutine == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Registry->Lock, &OldIrql);
    if (Registry->ShuttingDown)
    {
        Status = Registry->ShutdownStatus;
        CompleteNow = FALSE;
    }
    else if (Request->AdmissionRoutine != NULL && !NT_SUCCESS(Status = Request->AdmissionRoutine(Request, Request->CompletionContext)))
    {
        CompleteNow = FALSE;
    }
    else
    {
        ASSERT(Request->State == DXGK_SYNC_WAIT_STATE_INITIAL);
        CompleteNow = DxgkpSyncWaitConditionSatisfiedLocked(Request);
        Request->State = CompleteNow ? DXGK_SYNC_WAIT_STATE_COMPLETED : DXGK_SYNC_WAIT_STATE_REGISTERED;
        Request->CompletionStatus = STATUS_SUCCESS;
        if (!CompleteNow)
            InsertTailList(&Registry->RequestList, &Request->RegistryEntry);
    }
    KeReleaseSpinLock(&Registry->Lock, OldIrql);
    if (CompleteNow)
        Request->CompletionRoutine(Request, STATUS_SUCCESS, Request->CompletionContext);
    return Status;
}

NTSTATUS
DxgkSyncWaitCorePublishBatch(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _In_reads_(UpdateCount) PDXGK_SYNC_WAIT_CORE_UPDATE Updates,
    _In_ ULONG UpdateCount,
    _In_ BOOLEAN AllowFenceRewind,
    _In_opt_ PDXGK_SYNC_WAIT_CORE_PUBLISH_ADMISSION AdmissionRoutine,
    _In_opt_ PVOID AdmissionContext)
{
    LIST_ENTRY CompletionList;
    KIRQL OldIrql;
    ULONG Index;

    if (Registry == NULL || Updates == NULL || UpdateCount == 0)
        return STATUS_INVALID_PARAMETER;
    InitializeListHead(&CompletionList);
    KeAcquireSpinLock(&Registry->Lock, &OldIrql);
    if (Registry->ShuttingDown)
    {
        NTSTATUS Status = Registry->ShutdownStatus;

        KeReleaseSpinLock(&Registry->Lock, OldIrql);
        return Status;
    }
    if (AdmissionRoutine != NULL)
    {
        NTSTATUS Status = AdmissionRoutine(AdmissionContext);

        if (!NT_SUCCESS(Status))
        {
            KeReleaseSpinLock(&Registry->Lock, OldIrql);
            return Status;
        }
    }
    /*
     * A monitored fence only moves forward unless the caller says otherwise.
     * Refuse the request rather than dropping it: this used to skip the update
     * and still report success, so a caller that signalled backwards was told
     * its signal had taken effect while the value never moved -- and it has no
     * way to find out, because the fence reads back as whatever it already was.
     * Windows returns STATUS_INVALID_PARAMETER here, and that is measurably
     * what a caller needs to see.
     *
     * Checked across the whole batch before anything is written, because a
     * batch that is refused must not have applied part of itself.
     */
    if (!AllowFenceRewind)
    {
        for (Index = 0; Index < UpdateCount; ++Index)
        {
            if (Updates[Index].NewValue <= (UINT64)*Updates[Index].FenceValue)
            {
                KeReleaseSpinLock(&Registry->Lock, OldIrql);
                return STATUS_INVALID_PARAMETER;
            }
        }
    }

    for (Index = 0; Index < UpdateCount; ++Index)
    {
        InterlockedExchange64(Updates[Index].FenceValue, (LONG64)Updates[Index].NewValue);
        if (Updates[Index].PublishedValue != NULL)
            InterlockedExchange64((volatile LONG64 *)Updates[Index].PublishedValue, (LONG64)Updates[Index].NewValue);
    }
    KeMemoryBarrier();
    DxgkpSyncWaitCollectSatisfiedLocked(Registry, &CompletionList);
    KeReleaseSpinLock(&Registry->Lock, OldIrql);
    DxgkpSyncWaitCompleteList(&CompletionList);
    return STATUS_SUCCESS;
}

VOID
DxgkSyncWaitCoreCancelObject(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _In_ PVOID Object,
    _In_ NTSTATUS Status)
{
    LIST_ENTRY CompletionList;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Registry == NULL || Object == NULL)
        return;
    InitializeListHead(&CompletionList);
    KeAcquireSpinLock(&Registry->Lock, &OldIrql);
    Entry = Registry->RequestList.Flink;
    while (Entry != &Registry->RequestList)
    {
        PDXGK_SYNC_WAIT_CORE_REQUEST Request = CONTAINING_RECORD(Entry, DXGK_SYNC_WAIT_CORE_REQUEST, RegistryEntry);
        PLIST_ENTRY Next = Entry->Flink;
        ULONG Index;

        for (Index = 0; Index < Request->TargetCount; ++Index)
        {
            if (Request->Targets[Index].Object == Object)
            {
                DxgkpSyncWaitMoveCompletedLocked(Request, Status, &CompletionList);
                break;
            }
        }
        Entry = Next;
    }
    KeReleaseSpinLock(&Registry->Lock, OldIrql);
    DxgkpSyncWaitCompleteList(&CompletionList);
}

VOID
DxgkSyncWaitCoreCancelAll(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _In_ NTSTATUS Status,
    _In_ BOOLEAN ShutDown)
{
    LIST_ENTRY CompletionList;
    KIRQL OldIrql;

    if (Registry == NULL)
        return;
    InitializeListHead(&CompletionList);
    KeAcquireSpinLock(&Registry->Lock, &OldIrql);
    if (ShutDown && !Registry->ShuttingDown)
    {
        Registry->ShuttingDown = TRUE;
        Registry->ShutdownStatus = Status;
    }
    while (!IsListEmpty(&Registry->RequestList))
    {
        PDXGK_SYNC_WAIT_CORE_REQUEST Request = CONTAINING_RECORD(Registry->RequestList.Flink, DXGK_SYNC_WAIT_CORE_REQUEST, RegistryEntry);

        DxgkpSyncWaitMoveCompletedLocked(Request, Status, &CompletionList);
    }
    KeReleaseSpinLock(&Registry->Lock, OldIrql);
    DxgkpSyncWaitCompleteList(&CompletionList);
}

BOOLEAN
DxgkSyncWaitCoreIsEmpty(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry)
{
    KIRQL OldIrql;
    BOOLEAN Empty;

    if (Registry == NULL)
        return TRUE;
    KeAcquireSpinLock(&Registry->Lock, &OldIrql);
    Empty = IsListEmpty(&Registry->RequestList);
    KeReleaseSpinLock(&Registry->Lock, OldIrql);
    return Empty;
}
