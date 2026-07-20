/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Submit-DMA reservation drain state transitions
 */

#include "submit_reservation_core.h"

BOOLEAN DxgkSubmitReservationCoreTryAcquireLocked(_Inout_ volatile LONG *ActiveReservations, _In_ BOOLEAN Stopping, _Out_ PBOOLEAN ClearDrainedEvent)
{
    if (ActiveReservations == NULL || ClearDrainedEvent == NULL)
        return FALSE;
    *ClearDrainedEvent = FALSE;
    if (Stopping || *ActiveReservations < 0 || *ActiveReservations == MAXLONG)
        return FALSE;
    *ClearDrainedEvent = *ActiveReservations == 0;
    (*ActiveReservations)++;
    return TRUE;
}

BOOLEAN DxgkSubmitReservationCoreReleaseLocked(_Inout_ volatile LONG *ActiveReservations, _Out_ PBOOLEAN SetDrainedEvent)
{
    if (ActiveReservations == NULL || SetDrainedEvent == NULL)
        return FALSE;
    *SetDrainedEvent = FALSE;
    if (*ActiveReservations <= 0)
        return FALSE;
    (*ActiveReservations)--;
    *SetDrainedEvent = *ActiveReservations == 0;
    return TRUE;
}

BOOLEAN DxgkSubmitReservationCoreIsDrainedLocked(_In_ volatile const LONG *ActiveReservations)
{
    return ActiveReservations != NULL && *ActiveReservations == 0;
}

VOID DxgkSubmissionAccountingInitialize(_Out_ PDXGK_SUBMISSION_ACCOUNTING Accounting)
{
    if (Accounting != NULL)
        Accounting->State = DxgkSubmissionUnaccounted;
}

BOOLEAN DxgkSubmissionAccountingTryPrechargeLocked(_Inout_ PDXGK_SUBMISSION_ACCOUNTING Accounting, _Inout_ volatile LONG *DeviceCount, _Inout_ volatile LONG *ProcessCount, _In_ LONG DeviceLimit, _In_ LONG ProcessLimit)
{
    LONG CurrentDevice;
    LONG CurrentProcess;

    if (Accounting == NULL || DeviceCount == NULL || ProcessCount == NULL || DeviceLimit <= 0 || ProcessLimit <= 0)
        return FALSE;
    if (InterlockedCompareExchange(&Accounting->State, DxgkSubmissionUnaccounted, DxgkSubmissionUnaccounted) != DxgkSubmissionUnaccounted)
        return FALSE;
    CurrentDevice = InterlockedCompareExchange(DeviceCount, 0, 0);
    CurrentProcess = InterlockedCompareExchange(ProcessCount, 0, 0);
    if (CurrentDevice < 0 || CurrentProcess < 0 || CurrentDevice >= DeviceLimit || CurrentProcess >= ProcessLimit)
        return FALSE;
    InterlockedIncrement(DeviceCount);
    InterlockedIncrement(ProcessCount);
    InterlockedExchange(&Accounting->State, DxgkSubmissionPrecharged);
    return TRUE;
}

BOOLEAN DxgkSubmissionAccountingCommitLocked(_Inout_ PDXGK_SUBMISSION_ACCOUNTING Accounting, _Inout_ volatile LONG *DeviceCount, _Inout_ volatile LONG *ProcessCount)
{
    LONG PreviousState;

    if (Accounting == NULL || DeviceCount == NULL || ProcessCount == NULL)
        return FALSE;
    PreviousState = InterlockedCompareExchange(&Accounting->State, DxgkSubmissionCommitted, DxgkSubmissionPrecharged);
    if (PreviousState == DxgkSubmissionPrecharged)
        return TRUE;
    if (PreviousState != DxgkSubmissionUnaccounted || InterlockedCompareExchange(&Accounting->State, DxgkSubmissionCommitted, DxgkSubmissionUnaccounted) != DxgkSubmissionUnaccounted)
        return FALSE;
    InterlockedIncrement(DeviceCount);
    InterlockedIncrement(ProcessCount);
    return TRUE;
}

BOOLEAN DxgkSubmissionAccountingRelease(_Inout_ PDXGK_SUBMISSION_ACCOUNTING Accounting, _Inout_ volatile LONG *DeviceCount, _Inout_ volatile LONG *ProcessCount)
{
    LONG DeviceRemaining;
    LONG PreviousState;
    LONG ProcessRemaining;

    if (Accounting == NULL || DeviceCount == NULL || ProcessCount == NULL)
        return FALSE;
    for (;;)
    {
        PreviousState = InterlockedCompareExchange(&Accounting->State, DxgkSubmissionUnaccounted, DxgkSubmissionUnaccounted);
        if (PreviousState == DxgkSubmissionReleased)
            return FALSE;
        if (InterlockedCompareExchange(&Accounting->State, DxgkSubmissionReleased, PreviousState) == PreviousState)
            break;
    }
    if (PreviousState == DxgkSubmissionUnaccounted)
        return TRUE;
    if (PreviousState != DxgkSubmissionPrecharged && PreviousState != DxgkSubmissionCommitted)
        return FALSE;
    ProcessRemaining = InterlockedDecrement(ProcessCount);
    DeviceRemaining = InterlockedDecrement(DeviceCount);
    ASSERT(ProcessRemaining >= 0);
    ASSERT(DeviceRemaining >= 0);
    return TRUE;
}

DXGK_SUBMISSION_ACCOUNTING_STATE DxgkSubmissionAccountingGetState(_In_ const DXGK_SUBMISSION_ACCOUNTING *Accounting)
{
    if (Accounting == NULL)
        return DxgkSubmissionReleased;
    return (DXGK_SUBMISSION_ACCOUNTING_STATE)InterlockedCompareExchange((volatile LONG *)&Accounting->State, 0, 0);
}

BOOLEAN DxgkSubmissionResidencyPinTryAcquire(_Inout_ volatile LONG *PinCount)
{
    LONG Count;

    if (PinCount == NULL)
        return FALSE;
    for (;;)
    {
        Count = InterlockedCompareExchange(PinCount, 0, 0);
        if (Count < 0 || Count == MAXLONG)
            return FALSE;
        if (InterlockedCompareExchange(PinCount, Count + 1, Count) == Count)
            return TRUE;
    }
}

BOOLEAN DxgkSubmissionResidencyPinRelease(_Inout_ volatile LONG *PinCount)
{
    LONG Count;

    if (PinCount == NULL)
        return FALSE;
    for (;;)
    {
        Count = InterlockedCompareExchange(PinCount, 0, 0);
        if (Count <= 0)
            return FALSE;
        if (InterlockedCompareExchange(PinCount, Count - 1, Count) == Count)
            return TRUE;
    }
}

BOOLEAN DxgkSubmissionResidencyPinIsHeld(_In_ volatile const LONG *PinCount)
{
    return PinCount != NULL && InterlockedCompareExchange((volatile LONG *)PinCount, 0, 0) != 0;
}
