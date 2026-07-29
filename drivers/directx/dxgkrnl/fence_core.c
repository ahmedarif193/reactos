/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU fence ordering, monitored fences and periodic fences
 */

#include "fence_core.h"

/* --- 32-bit submission fence identity -------------------------------- */

BOOLEAN
DxgkFenceCoreReached(
    _In_ ULONG CompletedFence,
    _In_ ULONG TargetFence)
{
    return ((LONG)(CompletedFence - TargetFence) >= 0);
}

LONG
DxgkFenceCoreDistance(
    _In_ ULONG Earlier,
    _In_ ULONG Later)
{
    return (LONG)(Later - Earlier);
}

BOOLEAN
DxgkFenceCoreAdvance(
    _Inout_ volatile LONG *Watermark,
    _In_ ULONG Reported)
{
    LONG Current;

    for (;;)
    {
        Current = InterlockedCompareExchange(Watermark, 0, 0);
        if (DxgkFenceCoreReached((ULONG)Current, Reported))
            return FALSE;
        if (InterlockedCompareExchange(Watermark, (LONG)Reported, Current) == Current)
            return TRUE;
    }
}

ULONG
DxgkFenceCoreAllocate(
    _Inout_ volatile LONG *NextFenceId)
{
    ULONG Fence;

    /* Zero means "no fence", so the sequence skips it on wrap. */
    do
    {
        Fence = (ULONG)InterlockedIncrement(NextFenceId);
    } while (Fence == 0);
    return Fence;
}

/* --- monitored fences ------------------------------------------------- */

NTSTATUS
DxgkMonitoredFenceCoreInitialize(
    _Out_ PDXGK_MONITORED_FENCE Fence,
    _In_ ULONGLONG InitialValue,
    _In_ ULONG Flags)
{
    RtlZeroMemory(Fence, sizeof(*Fence));
    if ((Flags & ~DXGK_MONITORED_FENCE_FLAG_VALID_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    /* A fence that can neither be signalled nor waited on carries no
     * information and is a caller error, not a degenerate-but-legal object. */
    if ((Flags & DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL) != 0 &&
        (Flags & DXGK_MONITORED_FENCE_FLAG_NO_WAIT) != 0)
        return STATUS_INVALID_PARAMETER;
    Fence->CurrentValue = InitialValue;
    Fence->LastSignalledValue = InitialValue;
    Fence->Flags = Flags;
    Fence->Initialized = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkMonitoredFenceCoreSignal(
    _Inout_ PDXGK_MONITORED_FENCE Fence,
    _In_ ULONGLONG Value)
{
    if (!Fence->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Fence->Flags & DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL) != 0)
        return STATUS_NOT_SUPPORTED;
    /*
     * Monitored fence values are monotonic by contract.  Accepting a regress
     * would un-satisfy waits that already resolved, so it is refused and the
     * published value is left alone.
     */
    if (Value < Fence->CurrentValue)
        return STATUS_INVALID_PARAMETER;
    Fence->CurrentValue = Value;
    Fence->LastSignalledValue = Value;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkMonitoredFenceCoreIsSatisfied(
    _In_ const DXGK_MONITORED_FENCE *Fence,
    _In_ ULONGLONG WaitValue)
{
    if (!Fence->Initialized)
        return FALSE;
    return Fence->CurrentValue >= WaitValue;
}

NTSTATUS
DxgkMonitoredFenceCoreCanWait(
    _In_ const DXGK_MONITORED_FENCE *Fence)
{
    if (!Fence->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Fence->Flags & DXGK_MONITORED_FENCE_FLAG_NO_WAIT) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkMonitoredFenceCoreCanSignal(
    _In_ const DXGK_MONITORED_FENCE *Fence)
{
    if (!Fence->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Fence->Flags & DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

/* --- periodic monitored-fence notification lifetime ----------------- */

VOID
DxgkPeriodicNotificationCoreInitialize(
    _Out_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification)
{
    RtlZeroMemory(Notification, sizeof(*Notification));
}

NTSTATUS
DxgkPeriodicNotificationCoreBeginCreate(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG NotificationId)
{
    if (Notification == NULL || NotificationId == 0)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Notification->State,
                                   DxgkPeriodicNotificationCreating,
                                   DxgkPeriodicNotificationNone) !=
        DxgkPeriodicNotificationNone)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    Notification->VidPnTargetId = VidPnTargetId;
    Notification->NotificationId = NotificationId;
    Notification->NotificationHandle = NULL;
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkPeriodicNotificationCoreCompleteCreate(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification,
    _In_ PVOID NotificationHandle)
{
    if (Notification == NULL || NotificationHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Notification->State,
                                   DxgkPeriodicNotificationCreating,
                                   DxgkPeriodicNotificationCreating) !=
        DxgkPeriodicNotificationCreating)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Notification->NotificationHandle = NotificationHandle;
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Notification->State,
                                   DxgkPeriodicNotificationActive,
                                   DxgkPeriodicNotificationCreating) !=
        DxgkPeriodicNotificationCreating)
    {
        Notification->NotificationHandle = NULL;
        return STATUS_INVALID_DEVICE_STATE;
    }
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPeriodicNotificationCoreCancelCreate(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification)
{
    if (Notification == NULL ||
        InterlockedCompareExchange(&Notification->State,
                                   DxgkPeriodicNotificationNone,
                                   DxgkPeriodicNotificationCreating) !=
        DxgkPeriodicNotificationCreating)
    {
        return FALSE;
    }
    Notification->NotificationHandle = NULL;
    Notification->NotificationId = 0;
    Notification->VidPnTargetId = 0;
    return TRUE;
}

BOOLEAN
DxgkPeriodicNotificationCoreMatches(
    _In_ const DXGK_PERIODIC_NOTIFICATION_CORE *Notification,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG NotificationId)
{
    if (Notification == NULL || NotificationId == 0)
        return FALSE;
    if (InterlockedCompareExchange(
            (volatile LONG *)&Notification->State,
            DxgkPeriodicNotificationActive,
            DxgkPeriodicNotificationActive) !=
        DxgkPeriodicNotificationActive)
    {
        return FALSE;
    }
    return Notification->VidPnTargetId == VidPnTargetId &&
           Notification->NotificationId == NotificationId;
}

BOOLEAN
DxgkPeriodicNotificationCoreClaimDestroy(
    _Inout_ PDXGK_PERIODIC_NOTIFICATION_CORE Notification,
    _Out_ PVOID *NotificationHandle)
{
    PVOID Handle;

    if (NotificationHandle == NULL)
        return FALSE;
    *NotificationHandle = NULL;
    if (Notification == NULL ||
        InterlockedCompareExchange(&Notification->State,
                                   DxgkPeriodicNotificationDestroyed,
                                   DxgkPeriodicNotificationActive) !=
        DxgkPeriodicNotificationActive)
    {
        return FALSE;
    }
    KeMemoryBarrier();
    Handle = Notification->NotificationHandle;
    ASSERT(Handle != NULL);
    Notification->NotificationHandle = NULL;
    *NotificationHandle = Handle;
    return Handle != NULL;
}

/* --- periodic interrupt ISR-to-DPC handoff ---------------------------- */

VOID
DxgkPeriodicInterruptCoreInitialize(
    _Out_ PDXGK_PERIODIC_INTERRUPT_CORE Core)
{
    RtlZeroMemory(Core, sizeof(*Core));
}

VOID
DxgkPeriodicInterruptCoreEnableLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core)
{
    RtlZeroMemory(Core->Entries, sizeof(Core->Entries));
    Core->DpcActive = FALSE;
    Core->OverflowCount = 0;
    InterlockedExchange(
        &Core->State,
        DxgkPeriodicInterruptAccepting);
}

VOID
DxgkPeriodicInterruptCoreDisableLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core)
{
    InterlockedExchange(
        &Core->State,
        DxgkPeriodicInterruptDisabled);
    Core->DpcActive = FALSE;
    RtlZeroMemory(Core->Entries, sizeof(Core->Entries));
}

static NTSTATUS
DxgkpPeriodicInterruptCoreOverflowLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core,
    _Out_ PBOOLEAN QueueDpc,
    _In_ NTSTATUS Status)
{
    if (Core->OverflowCount != (ULONGLONG)-1)
        ++Core->OverflowCount;
    InterlockedExchange(
        &Core->State,
        DxgkPeriodicInterruptOverflowed);
    if (!Core->DpcActive)
    {
        Core->DpcActive = TRUE;
        *QueueDpc = TRUE;
    }
    return Status;
}

NTSTATUS
DxgkPeriodicInterruptCoreEnqueueLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core,
    _In_ ULONG VidPnTargetId,
    _In_ ULONG NotificationId,
    _In_ ULONGLONG NotificationCount,
    _Out_ PBOOLEAN QueueDpc)
{
    PDXGK_PERIODIC_INTERRUPT_CORE_ENTRY FreeEntry = NULL;
    ULONG Index;
    LONG State;

    if (Core == NULL || QueueDpc == NULL ||
        NotificationId == 0 || NotificationCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *QueueDpc = FALSE;
    State = InterlockedCompareExchange(
        &Core->State,
        DxgkPeriodicInterruptDisabled,
        DxgkPeriodicInterruptDisabled);
    if (State == DxgkPeriodicInterruptDisabled)
        return STATUS_DEVICE_NOT_READY;
    if (State == DxgkPeriodicInterruptOverflowed)
        return STATUS_BUFFER_OVERFLOW;

    for (Index = 0;
         Index < DXGK_PERIODIC_INTERRUPT_CORE_CAPACITY;
         ++Index)
    {
        PDXGK_PERIODIC_INTERRUPT_CORE_ENTRY Candidate =
            &Core->Entries[Index];

        if (!Candidate->InUse)
        {
            if (FreeEntry == NULL)
                FreeEntry = Candidate;
            continue;
        }
        if (Candidate->NotificationId != NotificationId)
            continue;
        if (Candidate->VidPnTargetId != VidPnTargetId)
        {
            return DxgkpPeriodicInterruptCoreOverflowLocked(
                Core,
                QueueDpc,
                STATUS_INVALID_PARAMETER);
        }
        if (NotificationCount >
            (ULONGLONG)-1 - Candidate->PendingCount)
        {
            return DxgkpPeriodicInterruptCoreOverflowLocked(
                Core,
                QueueDpc,
                STATUS_INTEGER_OVERFLOW);
        }
        Candidate->PendingCount += NotificationCount;
        if (!Core->DpcActive)
        {
            Core->DpcActive = TRUE;
            *QueueDpc = TRUE;
        }
        return STATUS_SUCCESS;
    }

    if (FreeEntry == NULL)
    {
        return DxgkpPeriodicInterruptCoreOverflowLocked(
            Core,
            QueueDpc,
            STATUS_BUFFER_OVERFLOW);
    }

    FreeEntry->VidPnTargetId = VidPnTargetId;
    FreeEntry->NotificationId = NotificationId;
    FreeEntry->PendingCount = NotificationCount;
    KeMemoryBarrier();
    FreeEntry->InUse = TRUE;
    if (!Core->DpcActive)
    {
        Core->DpcActive = TRUE;
        *QueueDpc = TRUE;
    }
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPeriodicInterruptCoreDequeueLocked(
    _Inout_ PDXGK_PERIODIC_INTERRUPT_CORE Core,
    _Out_ PDXGK_PERIODIC_INTERRUPT_CORE_ENTRY Entry)
{
    ULONG Index;

    if (Core == NULL || Entry == NULL)
        return FALSE;
    RtlZeroMemory(Entry, sizeof(*Entry));
    if (InterlockedCompareExchange(
            &Core->State,
            DxgkPeriodicInterruptDisabled,
            DxgkPeriodicInterruptDisabled) !=
        DxgkPeriodicInterruptAccepting)
    {
        /*
         * Overflow means at least one count was lost or malformed.  Do not
         * publish the retained subset as though the stream were complete.
         * The sticky state remains for the adapter DPC to report/recover.
         */
        Core->DpcActive = FALSE;
        RtlZeroMemory(Core->Entries, sizeof(Core->Entries));
        return FALSE;
    }

    for (Index = 0;
         Index < DXGK_PERIODIC_INTERRUPT_CORE_CAPACITY;
         ++Index)
    {
        PDXGK_PERIODIC_INTERRUPT_CORE_ENTRY Candidate =
            &Core->Entries[Index];

        if (!Candidate->InUse)
            continue;
        *Entry = *Candidate;
        RtlZeroMemory(Candidate, sizeof(*Candidate));
        return TRUE;
    }

    /*
     * Exit handshake: clear DpcActive only while the ISR-visible table is
     * proven empty under the shared interrupt lock.  A later ISR observes
     * FALSE, changes it to TRUE, and queues a fresh DPC; an earlier ISR was
     * already visible to this scan and is drained by this invocation.
     */
    Core->DpcActive = FALSE;
    return FALSE;
}

/* EOF */
