/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Exactly-once tracked GPU-work terminal state machine
 */

#ifndef _DXGK_TRACKED_WORK_CORE_H_
#define _DXGK_TRACKED_WORK_CORE_H_

#include <ntddk.h>

typedef enum _DXGK_TRACKED_WORK_STATE
{
    DxgkTrackedWorkPrepared = 0,
    DxgkTrackedWorkCommitted = 1,
    DxgkTrackedWorkRetired = 2,
    DxgkTrackedWorkCancelled = 3
} DXGK_TRACKED_WORK_STATE;

/* These callbacks execute with Core->Lock held at DISPATCH_LEVEL. They must
 * be nonblocking, nonpaged, non-reentrant, and must not acquire a lock above
 * the tracked-work core in the caller's lock hierarchy. */
typedef VOID (NTAPI *PDXGK_TRACKED_WORK_ADJUST_IN_FLIGHT)(_In_opt_ PVOID Context, _In_ LONG Delta);
typedef VOID (NTAPI *PDXGK_TRACKED_WORK_PUBLISH_SIGNAL)(_In_opt_ PVOID Context);
typedef VOID (NTAPI *PDXGK_TRACKED_WORK_COMPLETE_DEVICE_WORK)(_In_opt_ PVOID Context);

typedef struct _DXGK_TRACKED_WORK_CALLBACKS
{
    PDXGK_TRACKED_WORK_ADJUST_IN_FLIGHT AdjustInFlight;
    PDXGK_TRACKED_WORK_PUBLISH_SIGNAL PublishSignal;
    PDXGK_TRACKED_WORK_COMPLETE_DEVICE_WORK CompleteDeviceWork;
} DXGK_TRACKED_WORK_CALLBACKS, *PDXGK_TRACKED_WORK_CALLBACKS;

typedef struct _DXGK_TRACKED_WORK_CORE
{
    KSPIN_LOCK Lock;
    DXGK_TRACKED_WORK_CALLBACKS Callbacks;
    PVOID CallbackContext;
    volatile LONG State;
    BOOLEAN DeviceWorkOwned;
    BOOLEAN ExternalCleanupOwned;
} DXGK_TRACKED_WORK_CORE, *PDXGK_TRACKED_WORK_CORE;

VOID DxgkTrackedWorkCoreInitialize(_Out_ PDXGK_TRACKED_WORK_CORE Core, _In_opt_ const DXGK_TRACKED_WORK_CALLBACKS *Callbacks, _In_opt_ PVOID CallbackContext, _In_ BOOLEAN DeviceWorkOwned);
BOOLEAN DxgkTrackedWorkCoreClaimDeviceWork(_Inout_ PDXGK_TRACKED_WORK_CORE Core);
BOOLEAN DxgkTrackedWorkCoreClaimExternalCleanup(_Inout_ PDXGK_TRACKED_WORK_CORE Core);
BOOLEAN DxgkTrackedWorkCoreCommit(_Inout_ PDXGK_TRACKED_WORK_CORE Core, _In_ BOOLEAN CompletionAlreadyReached, _Out_ PBOOLEAN RetiredNow);
BOOLEAN DxgkTrackedWorkCoreRetire(_Inout_ PDXGK_TRACKED_WORK_CORE Core);
BOOLEAN DxgkTrackedWorkCoreCancel(_Inout_ PDXGK_TRACKED_WORK_CORE Core);
DXGK_TRACKED_WORK_STATE DxgkTrackedWorkCoreGetState(_Inout_ PDXGK_TRACKED_WORK_CORE Core);
BOOLEAN DxgkTrackedWorkCoreOwnsDeviceWork(_Inout_ PDXGK_TRACKED_WORK_CORE Core);
BOOLEAN DxgkTrackedWorkCoreOwnsExternalCleanup(_Inout_ PDXGK_TRACKED_WORK_CORE Core);

#endif /* _DXGK_TRACKED_WORK_CORE_H_ */
