/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Residency budgets, per-device references and offer/reclaim
 */

#include "residency_core.h"

/* --- budgets --------------------------------------------------------- */

NTSTATUS
DxgkResidencyCoreBudgetInitialize(
    _Out_ PDXGK_RESIDENCY_BUDGET Budget,
    _In_ ULONGLONG BudgetBytes,
    _In_ ULONGLONG ReservationBytes)
{
    RtlZeroMemory(Budget, sizeof(*Budget));
    /* A reservation the budget cannot honour is a contradiction: the process
     * would be guaranteed memory it is simultaneously forbidden to keep. */
    if (ReservationBytes > BudgetBytes)
        return STATUS_INVALID_PARAMETER;
    Budget->Budget = BudgetBytes;
    Budget->Reservation = ReservationBytes;
    Budget->Initialized = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreBudgetCharge(
    _Inout_ PDXGK_RESIDENCY_BUDGET Budget,
    _In_ ULONGLONG Bytes)
{
    if (!Budget->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if (Bytes == 0)
        return STATUS_INVALID_PARAMETER;
    if (Budget->CurrentUsage > MAXULONGLONG - Bytes)
        return STATUS_INTEGER_OVERFLOW;
    /*
     * Charging past the budget is allowed and is exactly what makes a process
     * over-budget: the memory manager reacts by trimming rather than by
     * failing the residency request outright.
     */
    Budget->CurrentUsage += Bytes;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreBudgetRelease(
    _Inout_ PDXGK_RESIDENCY_BUDGET Budget,
    _In_ ULONGLONG Bytes)
{
    if (!Budget->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    /* Releasing more than was charged would make usage wrap to a huge value
     * and the process would look permanently over budget. */
    if (Bytes > Budget->CurrentUsage)
        return STATUS_INVALID_PARAMETER;
    Budget->CurrentUsage -= Bytes;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkResidencyCoreBudgetIsOver(
    _In_ const DXGK_RESIDENCY_BUDGET *Budget)
{
    if (!Budget->Initialized)
        return FALSE;
    return Budget->CurrentUsage > Budget->Budget;
}

ULONGLONG
DxgkResidencyCoreBudgetTrimTarget(
    _In_ const DXGK_RESIDENCY_BUDGET *Budget)
{
    if (!Budget->Initialized || Budget->CurrentUsage <= Budget->Budget)
        return 0;
    return Budget->CurrentUsage - Budget->Budget;
}

BOOLEAN
DxgkResidencyCoreBudgetIsProtected(
    _In_ const DXGK_RESIDENCY_BUDGET *Budget)
{
    if (!Budget->Initialized)
        return FALSE;
    return Budget->CurrentUsage <= Budget->Reservation;
}

/* --- per-device residency references ---------------------------------- */

static ULONG
DxgkResidencyCoreFindDevice(
    _In_ const DXGK_RESIDENCY_REFS *Refs,
    _In_ ULONGLONG DeviceCookie)
{
    ULONG Index;

    for (Index = 0; Index < Refs->DeviceCount; ++Index)
    {
        if (Refs->DeviceCookies[Index] == DeviceCookie)
            return Index;
    }
    return DXGK_RESIDENCY_MAX_DEVICES;
}

VOID
DxgkResidencyCoreRefsInitialize(
    _Out_ PDXGK_RESIDENCY_REFS Refs,
    _In_ ULONGLONG CreatingDeviceCookie,
    _In_ BOOLEAN CreatedResident)
{
    RtlZeroMemory(Refs, sizeof(*Refs));
    if (!CreatedResident)
        return;
    /*
     * A created-resident allocation is resident without anyone having called
     * MakeResident, so it owes one implicit reference to its creating device.
     * It is a flag rather than a list entry because the creating device is
     * only known after the allocation object exists.
     */
    Refs->ImplicitReferenceHeld = TRUE;
    Refs->ImplicitDeviceCookie = CreatingDeviceCookie;
    Refs->TotalCount = 1;
}

NTSTATUS
DxgkResidencyCoreAcquire(
    _Inout_ PDXGK_RESIDENCY_REFS Refs,
    _In_ ULONGLONG DeviceCookie)
{
    ULONG Index;

    if (DeviceCookie == 0)
        return STATUS_INVALID_PARAMETER;
    Index = DxgkResidencyCoreFindDevice(Refs, DeviceCookie);
    if (Index == DXGK_RESIDENCY_MAX_DEVICES)
    {
        if (Refs->DeviceCount >= DXGK_RESIDENCY_MAX_DEVICES)
            return STATUS_INSUFFICIENT_RESOURCES;
        Index = Refs->DeviceCount++;
        Refs->DeviceCookies[Index] = DeviceCookie;
        Refs->DeviceCounts[Index] = 0;
    }
    Refs->DeviceCounts[Index]++;
    Refs->TotalCount++;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreRelease(
    _Inout_ PDXGK_RESIDENCY_REFS Refs,
    _In_ ULONGLONG DeviceCookie)
{
    ULONG Index;

    if (DeviceCookie == 0)
        return STATUS_INVALID_PARAMETER;
    Index = DxgkResidencyCoreFindDevice(Refs, DeviceCookie);
    if (Index == DXGK_RESIDENCY_MAX_DEVICES || Refs->DeviceCounts[Index] == 0)
    {
        /*
         * No explicit reference from this device: the implicit one taken at
         * create time is consumed here, so a device that created a resident
         * allocation can evict it exactly once without a matching acquire.
         */
        if (Refs->ImplicitReferenceHeld && Refs->ImplicitDeviceCookie == DeviceCookie)
        {
            Refs->ImplicitReferenceHeld = FALSE;
            Refs->TotalCount--;
            return STATUS_SUCCESS;
        }
        return STATUS_INVALID_PARAMETER;
    }
    Refs->DeviceCounts[Index]--;
    Refs->TotalCount--;
    return STATUS_SUCCESS;
}

ULONG
DxgkResidencyCoreQueryDevice(
    _In_ const DXGK_RESIDENCY_REFS *Refs,
    _In_ ULONGLONG DeviceCookie)
{
    ULONG Index = DxgkResidencyCoreFindDevice(Refs, DeviceCookie);
    ULONG Count = (Index == DXGK_RESIDENCY_MAX_DEVICES) ? 0 : Refs->DeviceCounts[Index];

    if (Refs->ImplicitReferenceHeld && Refs->ImplicitDeviceCookie == DeviceCookie)
        Count++;
    return Count;
}

ULONG
DxgkResidencyCoreReleaseAllForDevice(
    _Inout_ PDXGK_RESIDENCY_REFS Refs,
    _In_ ULONGLONG DeviceCookie)
{
    ULONG Index = DxgkResidencyCoreFindDevice(Refs, DeviceCookie);
    ULONG Released = 0;

    if (Index != DXGK_RESIDENCY_MAX_DEVICES)
    {
        Released = Refs->DeviceCounts[Index];
        Refs->DeviceCounts[Index] = 0;
        Refs->TotalCount -= Released;
    }
    /* A device going away takes its implicit reference with it, or the
     * allocation stays resident forever with no owner to release it. */
    if (Refs->ImplicitReferenceHeld && Refs->ImplicitDeviceCookie == DeviceCookie)
    {
        Refs->ImplicitReferenceHeld = FALSE;
        Refs->TotalCount--;
        Released++;
    }
    return Released;
}

VOID
DxgkResidencyCorePinSubmission(
    _Inout_ PDXGK_RESIDENCY_REFS Refs)
{
    Refs->SubmissionPinCount++;
}

VOID
DxgkResidencyCoreUnpinSubmission(
    _Inout_ PDXGK_RESIDENCY_REFS Refs)
{
    if (Refs->SubmissionPinCount != 0)
        Refs->SubmissionPinCount--;
}

BOOLEAN
DxgkResidencyCoreIsEvictable(
    _In_ const DXGK_RESIDENCY_REFS *Refs)
{
    return Refs->TotalCount == 0 && Refs->SubmissionPinCount == 0;
}

/* --- offer / reclaim -------------------------------------------------- */

VOID
DxgkOfferCoreInitialize(
    _Out_ PDXGK_OFFER_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
    State->Priority = DxgkOfferPriorityNone;
}

NTSTATUS
DxgkOfferCoreOffer(
    _Inout_ PDXGK_OFFER_STATE State,
    _In_ DXGK_OFFER_PRIORITY Priority)
{
    if (Priority == DxgkOfferPriorityNone || Priority > DxgkOfferPriorityAuto)
        return STATUS_INVALID_PARAMETER;
    /* Offering twice without reclaiming loses track of which offer the
     * content belongs to. */
    if (State->Offered)
        return STATUS_INVALID_DEVICE_STATE;
    State->Offered = TRUE;
    State->Priority = Priority;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkOfferCoreReclaim(
    _Inout_ PDXGK_OFFER_STATE State,
    _Out_ PBOOLEAN Discarded)
{
    *Discarded = FALSE;
    if (!State->Offered)
        return STATUS_INVALID_DEVICE_STATE;
    *Discarded = State->ContentDiscarded;
    State->Offered = FALSE;
    State->Priority = DxgkOfferPriorityNone;
    State->ContentDiscarded = FALSE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkOfferCoreDiscard(
    _Inout_ PDXGK_OFFER_STATE State)
{
    /* Only offered content may be discarded; anything else is still in use. */
    if (!State->Offered)
        return FALSE;
    State->ContentDiscarded = TRUE;
    return TRUE;
}

BOOLEAN
DxgkOfferCoreOutranksAsVictim(
    _In_ const DXGK_OFFER_STATE *Candidate,
    _In_ const DXGK_OFFER_STATE *Incumbent)
{
    if (Candidate->Offered != Incumbent->Offered)
        return Candidate->Offered;
    if (!Candidate->Offered)
        return FALSE;
    return Candidate->Priority < Incumbent->Priority;
}

/* EOF */
