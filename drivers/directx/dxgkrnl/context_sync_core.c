/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Scheduler-ordered synchronization object state core
 */

#include "context_sync_core.h"

static BOOLEAN
DxgkpContextSyncIsWait(
    _In_ DXGK_CONTEXT_SYNC_OPERATION Operation)
{
    return Operation == DxgkContextSyncOperationLegacyWait || Operation == DxgkContextSyncOperationWait2;
}

static BOOLEAN
DxgkpContextSyncIsSignal(
    _In_ DXGK_CONTEXT_SYNC_OPERATION Operation)
{
    return Operation == DxgkContextSyncOperationLegacySignal || Operation == DxgkContextSyncOperationSignal2;
}

static BOOLEAN
DxgkpContextSyncIsSecondGeneration(
    _In_ DXGK_CONTEXT_SYNC_OPERATION Operation)
{
    return Operation == DxgkContextSyncOperationWait2 || Operation == DxgkContextSyncOperationSignal2;
}

static ULONG
DxgkpContextSyncDuplicateCount(
    _In_reads_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG ObjectCount,
    _In_ ULONG Index)
{
    ULONG Candidate;
    ULONG Count = 0;

    for (Candidate = 0; Candidate < ObjectCount; ++Candidate)
    {
        if (Objects[Candidate].Identity == Objects[Index].Identity)
            ++Count;
    }
    return Count;
}

static BOOLEAN
DxgkpContextSyncIsFirstOccurrence(
    _In_reads_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG Index)
{
    ULONG Candidate;

    for (Candidate = 0; Candidate < Index; ++Candidate)
    {
        if (Objects[Candidate].Identity == Objects[Index].Identity)
            return FALSE;
    }
    return TRUE;
}

VOID
DxgkContextSyncCoreInitializeRetention(
    _Out_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention,
    _In_ PDXGK_CONTEXT_SYNC_CORE_RELEASE_ROUTINE ReleaseRoutine,
    _In_opt_ PVOID ReleaseContext)
{
    RtlZeroMemory(Retention, sizeof(*Retention));
    Retention->ReleaseRoutine = ReleaseRoutine;
    Retention->ReleaseContext = ReleaseContext;
}

NTSTATUS
DxgkContextSyncCoreAddReference(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention,
    _In_ PVOID Object,
    _In_ DXGK_CONTEXT_SYNC_REFERENCE_KIND Kind)
{
    ULONG Index;

    if (Retention == NULL || Retention->ReleaseRoutine == NULL || Object == NULL || Kind <= DxgkContextSyncReferenceInvalid || Kind > DxgkContextSyncReferenceEvent)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Retention->ReleaseClaimed, 0, 0) != 0)
        return STATUS_DELETE_PENDING;
    Index = Retention->ReferenceCount;
    if (Index >= RTL_NUMBER_OF(Retention->References))
        return STATUS_INSUFFICIENT_RESOURCES;
    Retention->References[Index].Object = Object;
    Retention->References[Index].Kind = Kind;
    Retention->ReferenceCount = Index + 1;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkContextSyncCoreClaimRelease(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention)
{
    return Retention != NULL && InterlockedCompareExchange(&Retention->ReleaseClaimed, 1, 0) == 0;
}

VOID
DxgkContextSyncCoreReleaseClaimed(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention)
{
    ULONG Index;

    if (Retention == NULL || InterlockedCompareExchange(&Retention->ReleaseClaimed, 0, 0) == 0)
        return;
    Index = (ULONG)InterlockedExchange(&Retention->ReferenceCount, 0);
    while (Index != 0)
    {
        PDXGK_CONTEXT_SYNC_CORE_REFERENCE Reference = &Retention->References[--Index];

        if (Reference->Object != NULL)
            Retention->ReleaseRoutine(Reference->Object, Reference->Kind, Retention->ReleaseContext);
        Reference->Object = NULL;
        Reference->Kind = DxgkContextSyncReferenceInvalid;
    }
}

VOID
DxgkContextSyncCoreRelease(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention)
{
    if (DxgkContextSyncCoreClaimRelease(Retention))
        DxgkContextSyncCoreReleaseClaimed(Retention);
}

/*
 * A monitored fence carries its own value per object: one call signals fence A
 * to 7 and fence B to 12.  The single-value form is the legacy contract, where
 * every object in the batch shares one value, so the array simply overrides it
 * per index when present.
 */
static UINT64
DxgkpContextSyncValueAt(
    _In_ UINT64 FenceValue,
    _In_reads_opt_(Index + 1) CONST UINT64 *FenceValueArray,
    _In_ ULONG Index)
{
    return FenceValueArray != NULL ? FenceValueArray[Index] : FenceValue;
}

NTSTATUS
DxgkContextSyncCoreValidate(
    _In_ DXGK_CONTEXT_SYNC_OPERATION Operation,
    _In_reads_opt_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG ObjectCount,
    _In_ ULONG SignalFlags,
    _In_ UINT64 PayloadValue,
    _In_reads_opt_(ObjectCount) CONST UINT64 *PayloadValueArray,
    _In_ BOOLEAN EventReferenced)
{
    BOOLEAN FencePresent = FALSE;
    BOOLEAN IsWait = DxgkpContextSyncIsWait(Operation);
    BOOLEAN IsSignal = DxgkpContextSyncIsSignal(Operation);
    BOOLEAN IsSecondGeneration = DxgkpContextSyncIsSecondGeneration(Operation);
    ULONG Index;

    if (!IsWait && !IsSignal)
        return STATUS_INVALID_PARAMETER;
    if (IsWait && SignalFlags != 0)
        return STATUS_INVALID_PARAMETER;
    if (Operation == DxgkContextSyncOperationLegacySignal && (SignalFlags & ~DXGK_CONTEXT_SYNC_SIGNAL_AT_SUBMISSION) != 0)
        return STATUS_INVALID_PARAMETER;
    if (Operation == DxgkContextSyncOperationSignal2 && (SignalFlags & ~DXGK_CONTEXT_SYNC_SIGNAL2_FLAGS_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if ((SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0)
    {
        if (Operation != DxgkContextSyncOperationSignal2 || ObjectCount != 0 || !EventReferenced || (SignalFlags & DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND) != 0)
            return STATUS_INVALID_PARAMETER;
        return STATUS_SUCCESS;
    }
    if (EventReferenced)
        return STATUS_INVALID_PARAMETER;
    if (Objects == NULL || ObjectCount == 0 || ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        PDXGK_CONTEXT_SYNC_CORE_OBJECT Object = &Objects[Index];

        if (Object->Identity == NULL || Object->Destroying == NULL || Object->FenceValue == NULL || Object->StateEvent == NULL)
            return STATUS_INVALID_PARAMETER;
        if (InterlockedCompareExchange(Object->Destroying, 0, 0) != 0)
            return STATUS_DELETE_PENDING;
        if (IsWait && (Object->ObjectFlags & DXGK_CONTEXT_SYNC_OBJECT_NO_WAIT) != 0)
            return STATUS_ACCESS_DENIED;
        if (IsSignal && (Object->ObjectFlags & DXGK_CONTEXT_SYNC_OBJECT_NO_SIGNAL) != 0)
            return STATUS_ACCESS_DENIED;
        switch (Object->Type)
        {
            case D3DDDI_SYNCHRONIZATION_MUTEX:
                if (Object->MutexOwned == NULL)
                    return STATUS_INVALID_PARAMETER;
                break;
            case D3DDDI_SEMAPHORE:
                if (Object->SemaphoreCount == NULL || Object->SemaphoreLimit == 0)
                    return STATUS_INVALID_PARAMETER;
                break;
            case D3DDDI_FENCE:
                if (!IsSecondGeneration || ObjectCount != 1)
                    return STATUS_INVALID_PARAMETER;
                FencePresent = TRUE;
                break;
            case D3DDDI_CPU_NOTIFICATION:
                if (IsWait || Object->NotificationEvent == NULL || (Object->ObjectFlags & DXGK_CONTEXT_SYNC_OBJECT_SIGNAL_BY_KMD) != 0)
                    return STATUS_INVALID_PARAMETER;
                break;
            case DXGK_CONTEXT_SYNC_TYPE_MONITORED_FENCE:
                /*
                 * A monitored fence carries its own target value, so unlike a
                 * legacy fence it can appear alongside others in one batch --
                 * that is the whole point of the GPU-side entry points.  It
                 * does require the per-object array: with a single shared value
                 * there would be nothing to distinguish the objects.
                 */
                if (!IsSecondGeneration || PayloadValueArray == NULL)
                    return STATUS_INVALID_PARAMETER;
                FencePresent = TRUE;
                break;
            default:
                return STATUS_NOT_SUPPORTED;
        }
    }
    if (!FencePresent && PayloadValueArray == NULL && PayloadValue != 0)
        return STATUS_INVALID_PARAMETER;
    if ((SignalFlags & DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND) != 0 && !FencePresent)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkContextSyncCoreExecuteWait(
    _In_reads_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG ObjectCount,
    _In_ UINT64 FenceValue,
    _In_reads_opt_(ObjectCount) CONST UINT64 *FenceValueArray)
{
    ULONG Index;

    if (Objects == NULL || ObjectCount == 0 || ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        PDXGK_CONTEXT_SYNC_CORE_OBJECT Object = &Objects[Index];
        ULONG Demand;

        if (!DxgkpContextSyncIsFirstOccurrence(Objects, Index))
            continue;
        Demand = DxgkpContextSyncDuplicateCount(Objects, ObjectCount, Index);
        if (Object->Type == D3DDDI_SYNCHRONIZATION_MUTEX && (InterlockedCompareExchange(Object->MutexOwned, 0, 0) != 0 || Demand != 1))
            return STATUS_PENDING;
        if (Object->Type == D3DDDI_SEMAPHORE && (UINT64)InterlockedCompareExchange64(Object->SemaphoreCount, 0, 0) < Demand)
            return STATUS_PENDING;
        if ((Object->Type == D3DDDI_FENCE || Object->Type == DXGK_CONTEXT_SYNC_TYPE_MONITORED_FENCE) &&
            (UINT64)InterlockedCompareExchange64(Object->FenceValue, 0, 0) <
                DxgkpContextSyncValueAt(FenceValue, FenceValueArray, Index))
            return STATUS_PENDING;
    }
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        PDXGK_CONTEXT_SYNC_CORE_OBJECT Object = &Objects[Index];
        ULONG Demand;

        if (!DxgkpContextSyncIsFirstOccurrence(Objects, Index))
            continue;
        Demand = DxgkpContextSyncDuplicateCount(Objects, ObjectCount, Index);
        if (Object->Type == D3DDDI_SYNCHRONIZATION_MUTEX)
        {
            InterlockedExchange(Object->MutexOwned, 1);
            InterlockedExchange64(Object->FenceValue, 0);
            KeClearEvent(Object->StateEvent);
        }
        else if (Object->Type == D3DDDI_SEMAPHORE)
        {
            LONG64 NewCount = InterlockedCompareExchange64(Object->SemaphoreCount, 0, 0) - Demand;

            InterlockedExchange64(Object->SemaphoreCount, NewCount);
            InterlockedExchange64(Object->FenceValue, NewCount);
            if (NewCount == 0)
                KeClearEvent(Object->StateEvent);
        }
    }
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkContextSyncCoreExecuteSignal(
    _In_reads_opt_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG ObjectCount,
    _In_ ULONG SignalFlags,
    _In_ UINT64 FenceValue,
    _In_reads_opt_(ObjectCount) CONST UINT64 *FenceValueArray,
    _In_opt_ PKEVENT EnqueueEvent)
{
    ULONG Index;

    if ((SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0)
    {
        if (ObjectCount != 0 || EnqueueEvent == NULL)
            return STATUS_INVALID_PARAMETER;
        KeSetEvent(EnqueueEvent, IO_NO_INCREMENT, FALSE);
        return STATUS_SUCCESS;
    }
    if (Objects == NULL || ObjectCount == 0 || ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED || EnqueueEvent != NULL)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        PDXGK_CONTEXT_SYNC_CORE_OBJECT Object = &Objects[Index];
        ULONG Demand;

        if (!DxgkpContextSyncIsFirstOccurrence(Objects, Index))
            continue;
        if (Object->Type != D3DDDI_SEMAPHORE)
            continue;
        Demand = DxgkpContextSyncDuplicateCount(Objects, ObjectCount, Index);
        if ((UINT64)InterlockedCompareExchange64(Object->SemaphoreCount, 0, 0) > (UINT64)Object->SemaphoreLimit || Demand > Object->SemaphoreLimit || (UINT64)InterlockedCompareExchange64(Object->SemaphoreCount, 0, 0) > (UINT64)Object->SemaphoreLimit - Demand)
            return STATUS_SEMAPHORE_LIMIT_EXCEEDED;
    }
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        PDXGK_CONTEXT_SYNC_CORE_OBJECT Object = &Objects[Index];
        ULONG Demand;

        if (!DxgkpContextSyncIsFirstOccurrence(Objects, Index))
            continue;
        Demand = DxgkpContextSyncDuplicateCount(Objects, ObjectCount, Index);
        if (Object->Type == D3DDDI_SYNCHRONIZATION_MUTEX)
        {
            InterlockedExchange(Object->MutexOwned, 0);
            InterlockedExchange64(Object->FenceValue, 1);
            KeSetEvent(Object->StateEvent, IO_NO_INCREMENT, FALSE);
        }
        else if (Object->Type == D3DDDI_SEMAPHORE)
        {
            LONG64 NewCount = InterlockedCompareExchange64(Object->SemaphoreCount, 0, 0) + Demand;

            InterlockedExchange64(Object->SemaphoreCount, NewCount);
            InterlockedExchange64(Object->FenceValue, NewCount);
            KeSetEvent(Object->StateEvent, IO_NO_INCREMENT, FALSE);
        }
        else if (Object->Type == D3DDDI_FENCE)
        {
            UINT64 CurrentValue = (UINT64)InterlockedCompareExchange64(Object->FenceValue, 0, 0);
            UINT64 TargetValue = DxgkpContextSyncValueAt(FenceValue, FenceValueArray, Index);

            if ((SignalFlags & DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND) != 0 || TargetValue > CurrentValue)
                InterlockedExchange64(Object->FenceValue, (LONG64)TargetValue);
            KeSetEvent(Object->StateEvent, IO_NO_INCREMENT, FALSE);
        }
        else if (Object->Type == D3DDDI_CPU_NOTIFICATION)
        {
            KeSetEvent(Object->NotificationEvent, IO_NO_INCREMENT, FALSE);
            KeSetEvent(Object->StateEvent, IO_NO_INCREMENT, FALSE);
        }
    }
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}
