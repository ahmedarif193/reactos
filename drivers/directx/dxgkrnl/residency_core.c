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
    _In_ ULONGLONG MaximumBytes,
    _In_ ULONGLONG ReservationBytes)
{
    RtlZeroMemory(Budget, sizeof(*Budget));
    if (BudgetBytes > MaximumBytes || ReservationBytes > MaximumBytes)
        return STATUS_INVALID_PARAMETER;
    Budget->Budget = BudgetBytes;
    Budget->Maximum = MaximumBytes;
    Budget->Reservation = ReservationBytes;
    Budget->Initialized = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreBudgetSetLimits(
    _Inout_ PDXGK_RESIDENCY_BUDGET Budget,
    _In_ ULONGLONG BudgetBytes,
    _In_ ULONGLONG MaximumBytes)
{
    if (!Budget->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if (BudgetBytes > MaximumBytes ||
        Budget->Reservation > MaximumBytes ||
        Budget->CurrentUsage > MaximumBytes)
        return STATUS_INVALID_PARAMETER;
    Budget->Budget = BudgetBytes;
    Budget->Maximum = MaximumBytes;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreBudgetSetReservation(
    _Inout_ PDXGK_RESIDENCY_BUDGET Budget,
    _In_ ULONGLONG ReservationBytes)
{
    if (!Budget->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if (ReservationBytes > Budget->Maximum)
        return STATUS_INVALID_PARAMETER;
    Budget->Reservation = ReservationBytes;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreBudgetPlanCharge(
    _In_ const DXGK_RESIDENCY_BUDGET *Budget,
    _In_ ULONGLONG Bytes,
    _In_ BOOLEAN CantTrimFurther,
    _Out_ PULONGLONG NumBytesToTrim)
{
    ULONGLONG EffectiveBudget;
    ULONGLONG NewUsage;

    if (NumBytesToTrim == NULL)
        return STATUS_INVALID_PARAMETER;
    *NumBytesToTrim = 0;
    if (Budget == NULL || !Budget->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    if (Bytes == 0)
        return STATUS_INVALID_PARAMETER;
    if (Budget->CurrentUsage > MAXULONGLONG - Bytes)
        return STATUS_INTEGER_OVERFLOW;

    NewUsage = Budget->CurrentUsage + Bytes;
    EffectiveBudget = CantTrimFurther
                        ? Budget->Maximum
                        : max(Budget->Budget, Budget->Reservation);
    if (NewUsage > EffectiveBudget)
    {
        *NumBytesToTrim = NewUsage - EffectiveBudget;
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreBudgetTryCharge(
    _Inout_ PDXGK_RESIDENCY_BUDGET Budget,
    _In_ ULONGLONG Bytes,
    _In_ BOOLEAN CantTrimFurther,
    _Out_ PULONGLONG NumBytesToTrim)
{
    NTSTATUS Status;

    Status = DxgkResidencyCoreBudgetPlanCharge(Budget,
                                               Bytes,
                                               CantTrimFurther,
                                               NumBytesToTrim);
    if (NT_SUCCESS(Status))
        Budget->CurrentUsage += Bytes;
    return Status;
}

NTSTATUS
DxgkResidencyCoreBudgetCharge(
    _Inout_ PDXGK_RESIDENCY_BUDGET Budget,
    _In_ ULONGLONG Bytes)
{
    ULONGLONG NumBytesToTrim;

    return DxgkResidencyCoreBudgetTryCharge(Budget,
                                            Bytes,
                                            TRUE,
                                            &NumBytesToTrim);
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

/* --- process-exit admission ------------------------------------------ */

VOID
DxgkResidencyCoreProcessAdmissionInitialize(
    _Out_ PDXGK_RESIDENCY_PROCESS_ADMISSION Admission)
{
    RtlZeroMemory(Admission, sizeof(*Admission));
}

BOOLEAN
DxgkResidencyCoreProcessAdmissionTryEnter(
    _Inout_ PDXGK_RESIDENCY_PROCESS_ADMISSION Admission,
    _In_ BOOLEAN ProcessAlreadyExiting)
{
    if (Admission == NULL)
        return FALSE;
    if (ProcessAlreadyExiting)
    {
        Admission->Exiting = TRUE;
        return FALSE;
    }
    if (Admission->Exiting || Admission->ActiveEntrants == MAXULONG)
    {
        return FALSE;
    }
    Admission->ActiveEntrants++;
    return TRUE;
}

VOID
DxgkResidencyCoreProcessAdmissionMarkExiting(
    _Inout_ PDXGK_RESIDENCY_PROCESS_ADMISSION Admission)
{
    if (Admission != NULL)
        Admission->Exiting = TRUE;
}

BOOLEAN
DxgkResidencyCoreProcessAdmissionCanPublish(
    _In_ const DXGK_RESIDENCY_PROCESS_ADMISSION *Admission)
{
    return Admission != NULL &&
           Admission->ActiveEntrants != 0 &&
           !Admission->Exiting;
}

BOOLEAN
DxgkResidencyCoreProcessAdmissionLeave(
    _Inout_ PDXGK_RESIDENCY_PROCESS_ADMISSION Admission)
{
    if (Admission == NULL || Admission->ActiveEntrants == 0)
        return FALSE;
    Admission->ActiveEntrants--;
    return Admission->ActiveEntrants == 0;
}

/* --- per-process shared-placement charges ---------------------------- */

VOID
DxgkResidencyCoreProcessChargeInitialize(
    _Out_ PDXGK_RESIDENCY_PROCESS_CHARGE_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
}

NTSTATUS
DxgkResidencyCoreProcessChargePlanAcquire(
    _In_ const DXGK_RESIDENCY_PROCESS_CHARGE_STATE *State,
    _In_ ULONG ReferenceCount,
    _Out_ PBOOLEAN RequiresBudgetCharge)
{
    if (RequiresBudgetCharge == NULL)
        return STATUS_INVALID_PARAMETER;
    *RequiresBudgetCharge = FALSE;
    if (State == NULL || ReferenceCount == 0)
        return STATUS_INVALID_PARAMETER;
    if ((State->ReferenceCount == 0) != !State->BudgetCharged)
        return STATUS_INVALID_DEVICE_STATE;
    if (ReferenceCount > MAXULONG - State->ReferenceCount)
        return STATUS_INTEGER_OVERFLOW;
    *RequiresBudgetCharge = State->ReferenceCount == 0;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreProcessChargeCommitAcquire(
    _Inout_ PDXGK_RESIDENCY_PROCESS_CHARGE_STATE State,
    _In_ ULONG ReferenceCount,
    _In_ BOOLEAN BudgetChargeCommitted)
{
    BOOLEAN RequiresBudgetCharge;
    NTSTATUS Status;

    Status = DxgkResidencyCoreProcessChargePlanAcquire(
                 State,
                 ReferenceCount,
                 &RequiresBudgetCharge);
    if (!NT_SUCCESS(Status))
        return Status;
    if (RequiresBudgetCharge != BudgetChargeCommitted)
        return STATUS_INVALID_DEVICE_STATE;
    State->ReferenceCount += ReferenceCount;
    if (RequiresBudgetCharge)
        State->BudgetCharged = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkResidencyCoreProcessChargeRelease(
    _Inout_ PDXGK_RESIDENCY_PROCESS_CHARGE_STATE State,
    _In_ ULONG ReferenceCount,
    _Out_ PBOOLEAN ReleaseBudgetCharge)
{
    if (ReleaseBudgetCharge == NULL)
        return STATUS_INVALID_PARAMETER;
    *ReleaseBudgetCharge = FALSE;
    if (State == NULL || ReferenceCount == 0)
        return STATUS_INVALID_PARAMETER;
    if (State->ReferenceCount == 0 ||
        !State->BudgetCharged ||
        ReferenceCount > State->ReferenceCount)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    State->ReferenceCount -= ReferenceCount;
    if (State->ReferenceCount == 0)
    {
        State->BudgetCharged = FALSE;
        *ReleaseBudgetCharge = TRUE;
    }
    return STATUS_SUCCESS;
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

/* --- callback-spanning residency transactions ------------------------- */

BOOLEAN
DxgkResidencyCoreTransactionTryAcquire(
    _Inout_ PVOID volatile *OwnerSlot,
    _In_ PVOID Owner)
{
    if (OwnerSlot == NULL || Owner == NULL)
        return FALSE;
    return InterlockedCompareExchangePointer(OwnerSlot, Owner, NULL) == NULL;
}

BOOLEAN
DxgkResidencyCoreTransactionRelease(
    _Inout_ PVOID volatile *OwnerSlot,
    _In_ PVOID Owner)
{
    if (OwnerSlot == NULL || Owner == NULL)
        return FALSE;
    return InterlockedCompareExchangePointer(OwnerSlot, NULL, Owner) == Owner;
}

NTSTATUS
DxgkResidencyCorePlanEvict(
    _In_ LONG DeviceReferences,
    _In_ LONG TotalReferences,
    _In_ ULONG RequestReferences,
    _In_ BOOLEAN Resident,
    _In_ BOOLEAN EvictOnlyIfNecessary,
    _Out_ PBOOLEAN PhysicalEvictionRequired,
    _Out_ PBOOLEAN TrimCandidate)
{
    if (PhysicalEvictionRequired == NULL || TrimCandidate == NULL)
        return STATUS_INVALID_PARAMETER;
    *PhysicalEvictionRequired = FALSE;
    *TrimCandidate = FALSE;
    if (RequestReferences == 0 ||
        RequestReferences > MAXLONG ||
        DeviceReferences < 0 ||
        TotalReferences < 0 ||
        DeviceReferences < (LONG)RequestReferences ||
        TotalReferences < (LONG)RequestReferences)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Resident && TotalReferences == (LONG)RequestReferences)
    {
        if (EvictOnlyIfNecessary)
            *TrimCandidate = TRUE;
        else
            *PhysicalEvictionRequired = TRUE;
    }
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkResidencyCoreShouldRollbackPlacement(
    _In_ BOOLEAN PlacementOwned,
    _In_ BOOLEAN OwnedReferencesReachedZero)
{
    return PlacementOwned && OwnedReferencesReachedZero;
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
