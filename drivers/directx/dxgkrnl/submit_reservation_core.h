/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Submit-DMA reservation drain state transitions
 */

#ifndef _DXGK_SUBMIT_RESERVATION_CORE_H_
#define _DXGK_SUBMIT_RESERVATION_CORE_H_

#include <ntddk.h>

typedef enum _DXGK_SUBMISSION_ACCOUNTING_STATE
{
    DxgkSubmissionUnaccounted = 0,
    DxgkSubmissionPrecharged = 1,
    DxgkSubmissionCommitted = 2,
    DxgkSubmissionReleased = 3
} DXGK_SUBMISSION_ACCOUNTING_STATE;

typedef struct _DXGK_SUBMISSION_ACCOUNTING
{
    volatile LONG State;
} DXGK_SUBMISSION_ACCOUNTING, *PDXGK_SUBMISSION_ACCOUNTING;

/* The caller must serialize every transition and observation with the same
 * lock that protects its drained event.  The Boolean output tells the caller
 * whether this transition must change that event while still holding the
 * lock. */
BOOLEAN DxgkSubmitReservationCoreTryAcquireLocked(_Inout_ volatile LONG *ActiveReservations, _In_ BOOLEAN Stopping, _Out_ PBOOLEAN ClearDrainedEvent);
BOOLEAN DxgkSubmitReservationCoreReleaseLocked(_Inout_ volatile LONG *ActiveReservations, _Out_ PBOOLEAN SetDrainedEvent);
BOOLEAN DxgkSubmitReservationCoreIsDrainedLocked(_In_ volatile const LONG *ActiveReservations);

/* Precharge and commit are serialized by the caller's submission-admission
 * lock.  Release is an exactly-once terminal transition and may run without
 * that lock; a concurrent admission may reject conservatively but cannot
 * exceed either limit. */
VOID DxgkSubmissionAccountingInitialize(_Out_ PDXGK_SUBMISSION_ACCOUNTING Accounting);
BOOLEAN DxgkSubmissionAccountingTryPrechargeLocked(_Inout_ PDXGK_SUBMISSION_ACCOUNTING Accounting, _Inout_ volatile LONG *DeviceCount, _Inout_ volatile LONG *ProcessCount, _In_ LONG DeviceLimit, _In_ LONG ProcessLimit);
BOOLEAN DxgkSubmissionAccountingCommitLocked(_Inout_ PDXGK_SUBMISSION_ACCOUNTING Accounting, _Inout_ volatile LONG *DeviceCount, _Inout_ volatile LONG *ProcessCount);
BOOLEAN DxgkSubmissionAccountingRelease(_Inout_ PDXGK_SUBMISSION_ACCOUNTING Accounting, _Inout_ volatile LONG *DeviceCount, _Inout_ volatile LONG *ProcessCount);
DXGK_SUBMISSION_ACCOUNTING_STATE DxgkSubmissionAccountingGetState(_In_ const DXGK_SUBMISSION_ACCOUNTING *Accounting);
BOOLEAN DxgkSubmissionResidencyPinTryAcquire(_Inout_ volatile LONG *PinCount);
BOOLEAN DxgkSubmissionResidencyPinRelease(_Inout_ volatile LONG *PinCount);
BOOLEAN DxgkSubmissionResidencyPinIsHeld(_In_ volatile const LONG *PinCount);

#endif /* _DXGK_SUBMIT_RESERVATION_CORE_H_ */
