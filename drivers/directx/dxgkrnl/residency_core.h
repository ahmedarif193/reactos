/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Residency budgets, per-device references and offer/reclaim
 *
 * Residency decides what the GPU can reach.  Getting the accounting wrong
 * either evicts memory a running packet is reading, or never evicts and wedges
 * on a full segment.  No dxgkrnl or miniport types.
 */

#ifndef _DXGK_RESIDENCY_CORE_H_
#define _DXGK_RESIDENCY_CORE_H_

#include <ntddk.h>

/* --- budgets --------------------------------------------------------- */

typedef struct _DXGK_RESIDENCY_BUDGET
{
    ULONGLONG Budget;             /* what this process may keep resident */
    ULONGLONG Reservation;        /* what it is guaranteed */
    ULONGLONG CurrentUsage;
    BOOLEAN   Initialized;
} DXGK_RESIDENCY_BUDGET, *PDXGK_RESIDENCY_BUDGET;

NTSTATUS DxgkResidencyCoreBudgetInitialize(_Out_ PDXGK_RESIDENCY_BUDGET Budget, _In_ ULONGLONG BudgetBytes, _In_ ULONGLONG ReservationBytes);
NTSTATUS DxgkResidencyCoreBudgetCharge(_Inout_ PDXGK_RESIDENCY_BUDGET Budget, _In_ ULONGLONG Bytes);
NTSTATUS DxgkResidencyCoreBudgetRelease(_Inout_ PDXGK_RESIDENCY_BUDGET Budget, _In_ ULONGLONG Bytes);
BOOLEAN DxgkResidencyCoreBudgetIsOver(_In_ const DXGK_RESIDENCY_BUDGET *Budget);
/* Bytes the caller must free to get back inside budget; 0 when within it. */
ULONGLONG DxgkResidencyCoreBudgetTrimTarget(_In_ const DXGK_RESIDENCY_BUDGET *Budget);
/* Usage below the reservation is protected from trimming under pressure. */
BOOLEAN DxgkResidencyCoreBudgetIsProtected(_In_ const DXGK_RESIDENCY_BUDGET *Budget);

/* --- per-device residency references ---------------------------------- */

#define DXGK_RESIDENCY_MAX_DEVICES  8

typedef struct _DXGK_RESIDENCY_REFS
{
    ULONGLONG DeviceCookies[DXGK_RESIDENCY_MAX_DEVICES];
    ULONG     DeviceCounts[DXGK_RESIDENCY_MAX_DEVICES];
    ULONG     DeviceCount;
    ULONG     TotalCount;
    /* A created-resident allocation carries one reference for its creating
     * device that no explicit MakeResident ever took. */
    BOOLEAN   ImplicitReferenceHeld;
    ULONGLONG ImplicitDeviceCookie;
    /* Active submissions pin placement regardless of residency references. */
    ULONG     SubmissionPinCount;
} DXGK_RESIDENCY_REFS, *PDXGK_RESIDENCY_REFS;

VOID DxgkResidencyCoreRefsInitialize(_Out_ PDXGK_RESIDENCY_REFS Refs, _In_ ULONGLONG CreatingDeviceCookie, _In_ BOOLEAN CreatedResident);
NTSTATUS DxgkResidencyCoreAcquire(_Inout_ PDXGK_RESIDENCY_REFS Refs, _In_ ULONGLONG DeviceCookie);
NTSTATUS DxgkResidencyCoreRelease(_Inout_ PDXGK_RESIDENCY_REFS Refs, _In_ ULONGLONG DeviceCookie);
ULONG DxgkResidencyCoreQueryDevice(_In_ const DXGK_RESIDENCY_REFS *Refs, _In_ ULONGLONG DeviceCookie);
ULONG DxgkResidencyCoreReleaseAllForDevice(_Inout_ PDXGK_RESIDENCY_REFS Refs, _In_ ULONGLONG DeviceCookie);
VOID DxgkResidencyCorePinSubmission(_Inout_ PDXGK_RESIDENCY_REFS Refs);
VOID DxgkResidencyCoreUnpinSubmission(_Inout_ PDXGK_RESIDENCY_REFS Refs);
/* Evictable only when nothing references it and no submission holds it. */
BOOLEAN DxgkResidencyCoreIsEvictable(_In_ const DXGK_RESIDENCY_REFS *Refs);

/* --- callback-spanning residency transactions ------------------------- */

/*
 * The owner slot is deliberately pointer-sized: a caller may use the address
 * of its stack transaction as a unique token.  TryAcquire/Release are atomic
 * so tests and lock-free observers see one exact owner transition.
 */
BOOLEAN
DxgkResidencyCoreTransactionTryAcquire(
    _Inout_ PVOID volatile *OwnerSlot,
    _In_ PVOID Owner);

BOOLEAN
DxgkResidencyCoreTransactionRelease(
    _Inout_ PVOID volatile *OwnerSlot,
    _In_ PVOID Owner);

/*
 * Validate a duplicate-collapsed Evict entry and decide whether this request
 * owns the zero-reference transition.  A different device's or request's
 * reference must keep the physical placement intact.
 */
NTSTATUS
DxgkResidencyCorePlanEvict(
    _In_ LONG DeviceReferences,
    _In_ LONG TotalReferences,
    _In_ ULONG RequestReferences,
    _In_ BOOLEAN Resident,
    _In_ BOOLEAN EvictOnlyIfNecessary,
    _Out_ PBOOLEAN PhysicalEvictionRequired,
    _Out_ PBOOLEAN TrimCandidate);

BOOLEAN
DxgkResidencyCoreShouldRollbackPlacement(
    _In_ BOOLEAN PlacementOwned,
    _In_ BOOLEAN OwnedReferencesReachedZero);

/* --- offer / reclaim -------------------------------------------------- */

typedef enum _DXGK_OFFER_PRIORITY
{
    DxgkOfferPriorityNone      = 0,
    DxgkOfferPriorityLow       = 1,
    DxgkOfferPriorityNormal    = 2,
    DxgkOfferPriorityHigh      = 3,
    DxgkOfferPriorityAuto      = 4
} DXGK_OFFER_PRIORITY;

typedef struct _DXGK_OFFER_STATE
{
    DXGK_OFFER_PRIORITY Priority;
    BOOLEAN Offered;
    BOOLEAN ContentDiscarded;
} DXGK_OFFER_STATE, *PDXGK_OFFER_STATE;

VOID DxgkOfferCoreInitialize(_Out_ PDXGK_OFFER_STATE State);
NTSTATUS DxgkOfferCoreOffer(_Inout_ PDXGK_OFFER_STATE State, _In_ DXGK_OFFER_PRIORITY Priority);
/* Reclaim reports whether the content survived; a discarded surface must be
 * regenerated by the client rather than read. */
NTSTATUS DxgkOfferCoreReclaim(_Inout_ PDXGK_OFFER_STATE State, _Out_ PBOOLEAN Discarded);
/* The memory manager may discard offered content at any time. */
BOOLEAN DxgkOfferCoreDiscard(_Inout_ PDXGK_OFFER_STATE State);
/* Offered content is evicted before anything still in use, lowest first. */
BOOLEAN DxgkOfferCoreOutranksAsVictim(_In_ const DXGK_OFFER_STATE *Candidate, _In_ const DXGK_OFFER_STATE *Incumbent);

#endif /* _DXGK_RESIDENCY_CORE_H_ */
