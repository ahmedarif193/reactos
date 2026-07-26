/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN topology, mode sets and the flip queue
 */

#include "display_core.h"

/* --- VidPN topology --------------------------------------------------- */

VOID
DxgkVidPnCoreTopologyInitialize(
    _Out_ PDXGK_VIDPN_TOPOLOGY Topology)
{
    RtlZeroMemory(Topology, sizeof(*Topology));
}

BOOLEAN
DxgkVidPnCoreFindPath(
    _In_ const DXGK_VIDPN_TOPOLOGY *Topology,
    _In_ ULONG SourceId,
    _In_ ULONG TargetId)
{
    ULONG Index;

    for (Index = 0; Index < Topology->PathCount; ++Index)
    {
        const DXGK_VIDPN_PATH *Path = &Topology->Paths[Index];

        if (Path->InUse && Path->SourceId == SourceId && Path->TargetId == TargetId)
            return TRUE;
    }
    return FALSE;
}

BOOLEAN
DxgkVidPnCoreTargetIsDriven(
    _In_ const DXGK_VIDPN_TOPOLOGY *Topology,
    _In_ ULONG TargetId,
    _Out_ PULONG SourceId)
{
    ULONG Index;

    *SourceId = 0;
    for (Index = 0; Index < Topology->PathCount; ++Index)
    {
        const DXGK_VIDPN_PATH *Path = &Topology->Paths[Index];

        if (Path->InUse && Path->TargetId == TargetId)
        {
            *SourceId = Path->SourceId;
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS
DxgkVidPnCoreAddPath(
    _Inout_ PDXGK_VIDPN_TOPOLOGY Topology,
    _In_ ULONG SourceId,
    _In_ ULONG TargetId)
{
    ULONG ExistingSource;

    if (Topology->PathCount >= DXGK_VIDPN_CORE_MAX_PATHS)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (DxgkVidPnCoreFindPath(Topology, SourceId, TargetId))
        return STATUS_OBJECT_NAME_COLLISION;
    /*
     * A monitor takes its picture from exactly one source.  Two sources on one
     * target is not a clone, it is two writers to one scanout.  (One source to
     * many targets is clone mode and is allowed.)
     */
    if (DxgkVidPnCoreTargetIsDriven(Topology, TargetId, &ExistingSource))
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;

    Topology->Paths[Topology->PathCount].SourceId = SourceId;
    Topology->Paths[Topology->PathCount].TargetId = TargetId;
    Topology->Paths[Topology->PathCount].InUse = TRUE;
    Topology->PathCount++;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidPnCoreRemovePath(
    _Inout_ PDXGK_VIDPN_TOPOLOGY Topology,
    _In_ ULONG SourceId,
    _In_ ULONG TargetId)
{
    ULONG Index;

    for (Index = 0; Index < Topology->PathCount; ++Index)
    {
        DXGK_VIDPN_PATH *Path = &Topology->Paths[Index];

        if (!Path->InUse || Path->SourceId != SourceId || Path->TargetId != TargetId)
            continue;
        /* Compact so PathCount stays the number of live paths. */
        if (Index + 1 < Topology->PathCount)
            RtlMoveMemory(&Topology->Paths[Index], &Topology->Paths[Index + 1],
                          (Topology->PathCount - Index - 1) * sizeof(DXGK_VIDPN_PATH));
        Topology->PathCount--;
        RtlZeroMemory(&Topology->Paths[Topology->PathCount], sizeof(DXGK_VIDPN_PATH));
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

ULONG
DxgkVidPnCoreCountTargetsForSource(
    _In_ const DXGK_VIDPN_TOPOLOGY *Topology,
    _In_ ULONG SourceId)
{
    ULONG Count = 0;
    ULONG Index;

    for (Index = 0; Index < Topology->PathCount; ++Index)
    {
        if (Topology->Paths[Index].InUse && Topology->Paths[Index].SourceId == SourceId)
            Count++;
    }
    return Count;
}

NTSTATUS
DxgkVidPnCoreValidateTopology(
    _In_ const DXGK_VIDPN_TOPOLOGY *Topology,
    _In_ ULONG SourceCount,
    _In_ ULONG TargetCount)
{
    ULONG Index;

    if (Topology->PathCount > DXGK_VIDPN_CORE_MAX_PATHS)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    for (Index = 0; Index < Topology->PathCount; ++Index)
    {
        const DXGK_VIDPN_PATH *Path = &Topology->Paths[Index];

        if (!Path->InUse)
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        if (Path->SourceId >= SourceCount)
            return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
        if (Path->TargetId >= TargetCount)
            return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }
    return STATUS_SUCCESS;
}

/* --- mode sets -------------------------------------------------------- */

VOID
DxgkVidPnCoreModeSetInitialize(
    _Out_ PDXGK_VIDPN_MODESET ModeSet)
{
    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
    ModeSet->PinnedIndex = DXGK_VIDPN_CORE_NO_PIN;
}

NTSTATUS
DxgkVidPnCoreModeValid(
    _In_ const DXGK_VIDPN_MODE *Mode)
{
    if (Mode->Width == 0 || Mode->Height == 0)
        return STATUS_INVALID_PARAMETER;
    if (Mode->BitsPerPixel == 0)
        return STATUS_INVALID_PARAMETER;
    /* A zero denominator is a division by zero the moment anyone computes the
     * refresh rate; a zero numerator is a mode that never scans out. */
    if (Mode->RefreshRateDenominator == 0 || Mode->RefreshRateNumerator == 0)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidPnCoreAddMode(
    _Inout_ PDXGK_VIDPN_MODESET ModeSet,
    _In_ const DXGK_VIDPN_MODE *Mode)
{
    NTSTATUS Status = DxgkVidPnCoreModeValid(Mode);

    if (!NT_SUCCESS(Status))
        return Status;
    if (ModeSet->ModeCount >= DXGK_VIDPN_CORE_MAX_MODES)
        return STATUS_INSUFFICIENT_RESOURCES;
    ModeSet->Modes[ModeSet->ModeCount] = *Mode;
    ModeSet->ModeCount++;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidPnCorePinMode(
    _Inout_ PDXGK_VIDPN_MODESET ModeSet,
    _In_ ULONG ModeIndex)
{
    if (ModeIndex >= ModeSet->ModeCount)
        return STATUS_INVALID_PARAMETER;
    /* Pinning over an existing pin would silently change the committed mode. */
    if (ModeSet->PinnedIndex != DXGK_VIDPN_CORE_NO_PIN)
        return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
    ModeSet->PinnedIndex = ModeIndex;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidPnCoreUnpinMode(
    _Inout_ PDXGK_VIDPN_MODESET ModeSet)
{
    if (ModeSet->PinnedIndex == DXGK_VIDPN_CORE_NO_PIN)
        return STATUS_GRAPHICS_MODE_NOT_PINNED;
    ModeSet->PinnedIndex = DXGK_VIDPN_CORE_NO_PIN;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkVidPnCoreGetPinnedMode(
    _In_ const DXGK_VIDPN_MODESET *ModeSet,
    _Out_ PDXGK_VIDPN_MODE Mode)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    if (ModeSet->PinnedIndex == DXGK_VIDPN_CORE_NO_PIN || ModeSet->PinnedIndex >= ModeSet->ModeCount)
        return FALSE;
    *Mode = ModeSet->Modes[ModeSet->PinnedIndex];
    return TRUE;
}

/* --- flip queue ------------------------------------------------------- */

VOID
DxgkFlipCoreInitialize(
    _Out_ PDXGK_FLIP_QUEUE Queue,
    _In_ ULONG SourceId)
{
    RtlZeroMemory(Queue, sizeof(*Queue));
    Queue->SourceId = SourceId;
}

VOID
DxgkFlipCoreSetSourceOwnership(
    _Inout_ PDXGK_FLIP_QUEUE Queue,
    _In_ BOOLEAN Owned)
{
    Queue->OwnsSource = Owned;
}

NTSTATUS
DxgkFlipCoreQueue(
    _Inout_ PDXGK_FLIP_QUEUE Queue,
    _In_ ULONGLONG AllocationCookie,
    _In_ DXGK_FLIP_INTERVAL Interval)
{
    ULONG Slot;

    if (AllocationCookie == 0)
        return STATUS_INVALID_PARAMETER;
    if (Interval >= DxgkFlipIntervalMax)
        return STATUS_INVALID_PARAMETER;
    /* Scanning out requires owning the source; otherwise one client would
     * display over another's output. */
    if (!Queue->OwnsSource)
        return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    if (Queue->Count >= DXGK_FLIP_CORE_MAX_QUEUED)
        return STATUS_GRAPHICS_TOO_MANY_REFERENCES;

    Slot = (Queue->HeadIndex + Queue->Count) % DXGK_FLIP_CORE_MAX_QUEUED;
    Queue->Entries[Slot].AllocationCookie = AllocationCookie;
    Queue->Entries[Slot].Interval = Interval;
    /* An immediate flip is visible at the next vsync boundary the queue is
     * examined; an interval of N waits N vsyncs. */
    Queue->Entries[Slot].VSyncsRemaining =
        (Interval == DxgkFlipIntervalImmediate) ? 0 : (ULONG)Interval;
    Queue->Count++;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkFlipCoreNotifyVSync(
    _Inout_ PDXGK_FLIP_QUEUE Queue,
    _Out_ PULONGLONG PresentedCookie)
{
    DXGK_FLIP_ENTRY *Head;

    *PresentedCookie = 0;
    if (Queue->Count == 0)
        return FALSE;
    Head = &Queue->Entries[Queue->HeadIndex];
    if (Head->VSyncsRemaining != 0)
    {
        Head->VSyncsRemaining--;
        return FALSE;
    }
    Queue->ScannedOutCookie = Head->AllocationCookie;
    *PresentedCookie = Head->AllocationCookie;
    RtlZeroMemory(Head, sizeof(*Head));
    Queue->HeadIndex = (Queue->HeadIndex + 1) % DXGK_FLIP_CORE_MAX_QUEUED;
    Queue->Count--;
    return TRUE;
}

ULONG
DxgkFlipCoreQueuedCount(
    _In_ const DXGK_FLIP_QUEUE *Queue)
{
    return Queue->Count;
}

/* EOF */
