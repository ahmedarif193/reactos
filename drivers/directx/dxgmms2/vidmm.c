/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Typed video memory ownership interface
 *
 * The typed wrapper around the segment ownership core.  Every entry point
 * takes VidMmLock, calls into the core, and releases before returning:
 * dxgkrnl never observes a partially updated ledger, and no dxgmms2 lock is
 * ever held across a dxgkrnl or miniport call.
 */

#include "dxgmms2_private.h"

#define NDEBUG
#include <debug.h>

static PDXGMMS2_ADAPTER_CONTEXT
Dxgmms2VidMmContext(_In_ DXGMMS2_VIDMM_HANDLE VidMm)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = (PDXGMMS2_ADAPTER_CONTEXT)VidMm;

    if (Context == NULL || Context->Signature != DXGMMS2_ADAPTER_SIGNATURE)
        return NULL;
    return Context;
}

static NTSTATUS
NTAPI
Dxgmms2VidMmStart(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _In_ ULONG SegmentCount)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Status = Dxgmms2VidMmCoreStart(&Context->VidMmCore, SegmentCount);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2VidMmSetSegment(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _In_ ULONG SegmentIndex,
    _In_ const DXGMMS2_VIDMM_SEGMENT_DESC_V1 *Desc)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (Desc == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Status = Dxgmms2VidMmCoreSetSegment(&Context->VidMmCore, SegmentIndex, Desc);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2VidMmReservePlacement(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _In_ ULONG SegmentIndex,
    _In_ const DXGMMS2_VIDMM_RESERVE_INFO_V1 *Info,
    _Out_ PULONGLONG OutOffset)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (OutOffset == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutOffset = 0;
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (Info == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Status = Dxgmms2VidMmCoreReserve(&Context->VidMmCore, SegmentIndex, Info, OutOffset);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2VidMmReleasePlacement(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _In_ ULONG SegmentIndex,
    _In_ ULONGLONG OwnerCookie)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Status = Dxgmms2VidMmCoreRelease(&Context->VidMmCore, SegmentIndex, OwnerCookie);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2VidMmSetPlacementState(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _In_ ULONG SegmentIndex,
    _In_ ULONGLONG OwnerCookie,
    _In_ LONG Priority,
    _In_ ULONG Flags)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Status = Dxgmms2VidMmCoreSetRangeState(&Context->VidMmCore, SegmentIndex, OwnerCookie, Priority, Flags);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2VidMmQuerySegmentStatus(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _In_ ULONG SegmentIndex,
    _Inout_ DXGMMS2_VIDMM_SEGMENT_STATUS_V1 *Status)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;
    NTSTATUS QueryStatus;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (Status == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    QueryStatus = Dxgmms2VidMmCoreQuerySegment(&Context->VidMmCore, SegmentIndex, Status);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return QueryStatus;
}

static BOOLEAN
NTAPI
Dxgmms2VidMmFindEvictionCandidate(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _In_ ULONG SegmentIndex,
    _In_ ULONG ExcludeFlags,
    _Out_ PULONGLONG OutOwnerCookie)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;
    BOOLEAN Found;

    if (OutOwnerCookie == NULL)
        return FALSE;
    *OutOwnerCookie = 0;
    if (Context == NULL)
        return FALSE;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Found = Dxgmms2VidMmCoreFindEvictionCandidate(&Context->VidMmCore, SegmentIndex, ExcludeFlags, OutOwnerCookie);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return Found;
}

static NTSTATUS
NTAPI
Dxgmms2VidMmReleaseAllPlacements(
    _In_ DXGMMS2_VIDMM_HANDLE VidMm,
    _Out_writes_to_(Capacity, *Count) PULONGLONG OwnerCookies,
    _In_ ULONG Capacity,
    _Out_ PULONG Count)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2VidMmContext(VidMm);
    KIRQL OldIrql;

    if (Count == NULL)
        return STATUS_INVALID_PARAMETER;
    *Count = 0;
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (OwnerCookies == NULL || Capacity == 0)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    *Count = Dxgmms2VidMmCoreReleaseAll(&Context->VidMmCore, OwnerCookies, Capacity);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Dxgmms2QueryVidMmInterface(
    _In_ DXGMMS2_ADAPTER_HANDLE Adapter,
    _Inout_ DXGMMS2_VIDMM_INTERFACE_V1 *VidMmInterface)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;

    if (VidMmInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    if (VidMmInterface->Size != DXGMMS2_VIDMM_INTERFACE_V1_SIZE ||
        VidMmInterface->Version != DXGMMS2_VIDMM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;

    VidMmInterface->VidMmHandle = (DXGMMS2_VIDMM_HANDLE)Context;
    VidMmInterface->Start = Dxgmms2VidMmStart;
    VidMmInterface->SetSegment = Dxgmms2VidMmSetSegment;
    VidMmInterface->ReservePlacement = Dxgmms2VidMmReservePlacement;
    VidMmInterface->ReleasePlacement = Dxgmms2VidMmReleasePlacement;
    VidMmInterface->SetPlacementState = Dxgmms2VidMmSetPlacementState;
    VidMmInterface->QuerySegmentStatus = Dxgmms2VidMmQuerySegmentStatus;
    VidMmInterface->FindEvictionCandidate = Dxgmms2VidMmFindEvictionCandidate;
    VidMmInterface->ReleaseAllPlacements = Dxgmms2VidMmReleaseAllPlacements;

    Dxgmms2DereferenceAdapterContext(Context);
    return STATUS_SUCCESS;
}

NTSTATUS
Dxgmms2VidMmInitializeContext(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context)
{
    PDXGMMS2_VIDMM_RANGE Pool;

    KeInitializeSpinLock(&Context->VidMmLock);
    Pool = ExAllocatePoolWithTag(NonPagedPool,
                                 DXGMMS2_VIDMM_MAX_RANGES * sizeof(DXGMMS2_VIDMM_RANGE),
                                 'MMXD');
    if (Pool == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Context->VidMmRangePool = Pool;
    Dxgmms2VidMmCoreInitialize(&Context->VidMmCore, Pool, DXGMMS2_VIDMM_MAX_RANGES);
    return STATUS_SUCCESS;
}

VOID
Dxgmms2VidMmTeardownContext(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context)
{
    KIRQL OldIrql;
    PVOID Pool;

    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Dxgmms2VidMmCoreStop(&Context->VidMmCore);
    Pool = Context->VidMmRangePool;
    Context->VidMmRangePool = NULL;
    RtlZeroMemory(&Context->VidMmCore, sizeof(Context->VidMmCore));
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    if (Pool != NULL)
        ExFreePoolWithTag(Pool, 'MMXD');
}

NTSTATUS
Dxgmms2VidMmStartAdapter(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context,
    _In_ ULONG SegmentCount)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (SegmentCount == 0)
        SegmentCount = 1;
    /* Silently clamping would leave the segments past the bound unowned and
     * fail later at publish time with a misleading error. */
    if (SegmentCount > DXGMMS2_VIDMM_MAX_SEGMENTS)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Status = Dxgmms2VidMmCoreStart(&Context->VidMmCore, SegmentCount);
    if (Status == STATUS_INVALID_DEVICE_STATE)
        Status = STATUS_SUCCESS;
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
    return Status;
}

VOID
Dxgmms2VidMmStopAdapter(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->VidMmLock, &OldIrql);
    Dxgmms2VidMmCoreStop(&Context->VidMmCore);
    KeReleaseSpinLock(&Context->VidMmLock, OldIrql);
}

/* EOF */
