/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Exactly-once tracked GPU-work terminal state machine
 */

#include "tracked_work_core.h"

static VOID DxgkpTrackedWorkAdjustInFlightLocked(_In_ PDXGK_TRACKED_WORK_CORE Core, _In_ LONG Delta)
{
    if (Core->Callbacks.AdjustInFlight != NULL)
        Core->Callbacks.AdjustInFlight(Core->CallbackContext, Delta);
}

static VOID DxgkpTrackedWorkCompleteLocked(_In_ PDXGK_TRACKED_WORK_CORE Core)
{
    if (Core->DeviceWorkOwned && Core->Callbacks.CompleteDeviceWork != NULL)
        Core->Callbacks.CompleteDeviceWork(Core->CallbackContext);
}

VOID DxgkTrackedWorkCoreInitialize(_Out_ PDXGK_TRACKED_WORK_CORE Core, _In_opt_ const DXGK_TRACKED_WORK_CALLBACKS *Callbacks, _In_opt_ PVOID CallbackContext, _In_ BOOLEAN DeviceWorkOwned)
{
    RtlZeroMemory(Core, sizeof(*Core));
    KeInitializeSpinLock(&Core->Lock);
    if (Callbacks != NULL)
        Core->Callbacks = *Callbacks;
    Core->CallbackContext = CallbackContext;
    Core->State = DxgkTrackedWorkPrepared;
    Core->DeviceWorkOwned = DeviceWorkOwned;
}

BOOLEAN DxgkTrackedWorkCoreClaimDeviceWork(_Inout_ PDXGK_TRACKED_WORK_CORE Core)
{
    KIRQL OldIrql;
    BOOLEAN Claimed;

    if (Core == NULL)
        return FALSE;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    Claimed = Core->State == DxgkTrackedWorkPrepared;
    if (Claimed)
        Core->DeviceWorkOwned = TRUE;
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return Claimed;
}

BOOLEAN DxgkTrackedWorkCoreClaimExternalCleanup(_Inout_ PDXGK_TRACKED_WORK_CORE Core)
{
    KIRQL OldIrql;
    BOOLEAN Claimed;

    if (Core == NULL)
        return FALSE;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    Claimed = Core->State == DxgkTrackedWorkPrepared;
    if (Claimed)
        Core->ExternalCleanupOwned = TRUE;
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return Claimed;
}

BOOLEAN DxgkTrackedWorkCoreCommit(_Inout_ PDXGK_TRACKED_WORK_CORE Core, _In_ BOOLEAN CompletionAlreadyReached, _Out_ PBOOLEAN RetiredNow)
{
    KIRQL OldIrql;
    BOOLEAN Committed = FALSE;

    if (RetiredNow == NULL)
        return FALSE;
    *RetiredNow = FALSE;
    if (Core == NULL)
        return FALSE;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    if (Core->State == DxgkTrackedWorkPrepared)
    {
        Core->ExternalCleanupOwned = TRUE;
        Core->State = CompletionAlreadyReached ? DxgkTrackedWorkRetired : DxgkTrackedWorkCommitted;
        DxgkpTrackedWorkAdjustInFlightLocked(Core, 1);
        if (CompletionAlreadyReached)
        {
            DxgkpTrackedWorkAdjustInFlightLocked(Core, -1);
            if (Core->Callbacks.PublishSignal != NULL)
                Core->Callbacks.PublishSignal(Core->CallbackContext);
            DxgkpTrackedWorkCompleteLocked(Core);
            *RetiredNow = TRUE;
        }
        Committed = TRUE;
    }
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return Committed;
}

BOOLEAN DxgkTrackedWorkCoreRetire(_Inout_ PDXGK_TRACKED_WORK_CORE Core)
{
    KIRQL OldIrql;
    BOOLEAN Retired = FALSE;

    if (Core == NULL)
        return FALSE;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    if (Core->State == DxgkTrackedWorkCommitted)
    {
        Core->State = DxgkTrackedWorkRetired;
        DxgkpTrackedWorkAdjustInFlightLocked(Core, -1);
        if (Core->Callbacks.PublishSignal != NULL)
            Core->Callbacks.PublishSignal(Core->CallbackContext);
        DxgkpTrackedWorkCompleteLocked(Core);
        Retired = TRUE;
    }
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return Retired;
}

BOOLEAN DxgkTrackedWorkCoreCancel(_Inout_ PDXGK_TRACKED_WORK_CORE Core)
{
    KIRQL OldIrql;
    BOOLEAN Cancelled = FALSE;

    if (Core == NULL)
        return FALSE;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    if (Core->State == DxgkTrackedWorkPrepared || Core->State == DxgkTrackedWorkCommitted)
    {
        DXGK_TRACKED_WORK_STATE PreviousState = (DXGK_TRACKED_WORK_STATE)Core->State;

        Core->State = DxgkTrackedWorkCancelled;
        if (PreviousState == DxgkTrackedWorkCommitted)
            DxgkpTrackedWorkAdjustInFlightLocked(Core, -1);
        DxgkpTrackedWorkCompleteLocked(Core);
        Cancelled = TRUE;
    }
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return Cancelled;
}

DXGK_TRACKED_WORK_STATE DxgkTrackedWorkCoreGetState(_Inout_ PDXGK_TRACKED_WORK_CORE Core)
{
    DXGK_TRACKED_WORK_STATE State;
    KIRQL OldIrql;

    if (Core == NULL)
        return DxgkTrackedWorkCancelled;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    State = (DXGK_TRACKED_WORK_STATE)Core->State;
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return State;
}

BOOLEAN DxgkTrackedWorkCoreOwnsDeviceWork(_Inout_ PDXGK_TRACKED_WORK_CORE Core)
{
    KIRQL OldIrql;
    BOOLEAN Owned;

    if (Core == NULL)
        return FALSE;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    Owned = Core->DeviceWorkOwned;
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return Owned;
}

BOOLEAN DxgkTrackedWorkCoreOwnsExternalCleanup(_Inout_ PDXGK_TRACKED_WORK_CORE Core)
{
    KIRQL OldIrql;
    BOOLEAN Owned;

    if (Core == NULL)
        return FALSE;
    KeAcquireSpinLock(&Core->Lock, &OldIrql);
    Owned = Core->ExternalCleanupOwned;
    KeReleaseSpinLock(&Core->Lock, OldIrql);
    return Owned;
}
