/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Video memory segment ownership core
 */

#include "vidmm_core.h"

#include <debug.h>

static BOOLEAN
Dxgmms2VidMmAlignUp(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment,
    _Out_ PULONGLONG Result)
{
    ULONGLONG Aligned;

    *Result = 0;
    if (Alignment == 0)
        Alignment = 1;
    if ((Alignment & (Alignment - 1)) != 0)
        return FALSE;
    if (Value > MAXULONGLONG - (Alignment - 1))
        return FALSE;
    Aligned = (Value + Alignment - 1) & ~(Alignment - 1);
    *Result = Aligned;
    return TRUE;
}

static PDXGMMS2_VIDMM_SEGMENT
Dxgmms2VidMmSegment(
    _In_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentIndex)
{
    if (!Core->Started || SegmentIndex >= Core->SegmentCount)
        return NULL;
    if (!Core->Segments[SegmentIndex].Valid)
        return NULL;
    return &Core->Segments[SegmentIndex];
}

static ULONGLONG
Dxgmms2VidMmPlacementLimit(
    _In_ PDXGMMS2_VIDMM_SEGMENT Segment)
{
    if (Segment->CommitLimit != 0 && Segment->CommitLimit < Segment->Size)
        return Segment->CommitLimit;
    return Segment->Size;
}

VOID
Dxgmms2VidMmCoreInitialize(
    _Out_ PDXGMMS2_VIDMM_CORE Core,
    _In_reads_(PoolCount) PDXGMMS2_VIDMM_RANGE RangePool,
    _In_ ULONG PoolCount)
{
    ULONG Index;

    RtlZeroMemory(Core, sizeof(*Core));
    InitializeListHead(&Core->FreeRangeList);
    Core->RangePool = RangePool;
    Core->RangePoolCount = PoolCount;
    if (RangePool == NULL)
        return;
    RtlZeroMemory(RangePool, PoolCount * sizeof(DXGMMS2_VIDMM_RANGE));
    for (Index = 0; Index < PoolCount; ++Index)
        InsertTailList(&Core->FreeRangeList, &RangePool[Index].Entry);
}

NTSTATUS
Dxgmms2VidMmCoreStart(
    _Inout_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentCount)
{
    ULONG Index;

    if (SegmentCount == 0 || SegmentCount > DXGMMS2_VIDMM_MAX_SEGMENTS)
        return STATUS_INVALID_PARAMETER;
    if (Core->Started)
        return STATUS_INVALID_DEVICE_STATE;
    if (Core->RangePool == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Index < SegmentCount; ++Index)
    {
        RtlZeroMemory(&Core->Segments[Index], sizeof(Core->Segments[Index]));
        InitializeListHead(&Core->Segments[Index].RangeList);
    }
    Core->SegmentCount = SegmentCount;
    Core->Started = TRUE;
    return STATUS_SUCCESS;
}

VOID
Dxgmms2VidMmCoreStop(
    _Inout_ PDXGMMS2_VIDMM_CORE Core)
{
    ULONG Index;

    if (!Core->Started)
        return;
    for (Index = 0; Index < Core->SegmentCount; ++Index)
    {
        PDXGMMS2_VIDMM_SEGMENT Segment = &Core->Segments[Index];

        while (!IsListEmpty(&Segment->RangeList))
        {
            PLIST_ENTRY Entry = RemoveHeadList(&Segment->RangeList);
            PDXGMMS2_VIDMM_RANGE Range = CONTAINING_RECORD(Entry, DXGMMS2_VIDMM_RANGE, Entry);

            RtlZeroMemory(Range, sizeof(*Range));
            InsertTailList(&Core->FreeRangeList, &Range->Entry);
        }
        Segment->UsedSize = 0;
        Segment->BumpOffset = 0;
        Segment->RangeCount = 0;
        Segment->Valid = FALSE;
    }
    Core->LiveRangeCount = 0;
    Core->SegmentCount = 0;
    Core->Started = FALSE;
}

NTSTATUS
Dxgmms2VidMmCoreSetSegment(
    _Inout_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentIndex,
    _In_ const DXGMMS2_VIDMM_SEGMENT_DESC_V1 *Desc)
{
    PDXGMMS2_VIDMM_SEGMENT Segment;

    if (!Core->Started || SegmentIndex >= Core->SegmentCount)
        return STATUS_INVALID_PARAMETER;
    if (Desc->Size == 0)
        return STATUS_INVALID_PARAMETER;
    if (Desc->CommitLimit != 0 && Desc->CommitLimit > Desc->Size)
        return STATUS_INVALID_PARAMETER;
    Segment = &Core->Segments[SegmentIndex];
    /* Redescribing a segment that already holds placements would invalidate
     * every offset dxgkrnl handed out from it. */
    if (Segment->Valid && Segment->RangeCount != 0)
        return STATUS_INVALID_DEVICE_STATE;
    Segment->Size = Desc->Size;
    Segment->CommitLimit = Desc->CommitLimit;
    Segment->Flags = Desc->Flags;
    Segment->UsedSize = 0;
    Segment->BumpOffset = 0;
    Segment->Valid = TRUE;
    return STATUS_SUCCESS;
}

/*
 * Dxgmms2VidMmCoreReserve
 *
 * Virgin space first — the high-water cursor never hands back a retired
 * offset, which some GPUs require (recycled GPU VA wedges the V3D vertex
 * pipe) — then first-fit over the gaps once the tail is exhausted.
 */
NTSTATUS
Dxgmms2VidMmCoreReserve(
    _Inout_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentIndex,
    _In_ const DXGMMS2_VIDMM_RESERVE_INFO_V1 *Info,
    _Out_ PULONGLONG OutOffset)
{
    PDXGMMS2_VIDMM_SEGMENT Segment;
    PDXGMMS2_VIDMM_RANGE Range;
    PLIST_ENTRY Entry;
    ULONGLONG Limit;
    ULONGLONG AlignedSize;
    ULONGLONG Candidate = 0;
    ULONGLONG Aligned;
    BOOLEAN Found = FALSE;

    *OutOffset = 0;
    Segment = Dxgmms2VidMmSegment(Core, SegmentIndex);
    if (Segment == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size == 0 || Info->OwnerCookie == 0)
        return STATUS_INVALID_PARAMETER;
    if (!Dxgmms2VidMmAlignUp(Info->Size, Info->Alignment, &AlignedSize))
        return STATUS_INVALID_PARAMETER;

    Limit = Dxgmms2VidMmPlacementLimit(Segment);
    if (AlignedSize > Limit)
        return STATUS_NO_MEMORY;
    if (Segment->UsedSize > Limit || AlignedSize > Limit - Segment->UsedSize)
        return STATUS_NO_MEMORY;
    /*
     * An exhausted range pool means no segment can take another placement
     * right now.  Report it as a space failure so the caller's eviction pass
     * is the recovery — evicting anywhere returns a record here — but say so
     * out loud, because "no memory" from a full metadata pool is a different
     * condition from a full segment and should not be diagnosed as one.
     */
    if (IsListEmpty(&Core->FreeRangeList))
    {
        DPRINT1("Dxgmms2VidMmCoreReserve: placement-record pool exhausted (%lu live)\n", Core->LiveRangeCount);
        return STATUS_NO_MEMORY;
    }

    if (Dxgmms2VidMmAlignUp(Segment->BumpOffset, Info->Alignment, &Aligned) &&
        Aligned <= Limit && AlignedSize <= Limit - Aligned)
    {
        Candidate = Aligned;
        Segment->BumpOffset = Aligned + AlignedSize;
        Found = TRUE;
    }

    if (!Found)
    {
        for (Entry = Segment->RangeList.Flink; Entry != &Segment->RangeList; Entry = Entry->Flink)
        {
            PDXGMMS2_VIDMM_RANGE Existing = CONTAINING_RECORD(Entry, DXGMMS2_VIDMM_RANGE, Entry);

            if (!Dxgmms2VidMmAlignUp(Candidate, Info->Alignment, &Aligned))
                return STATUS_INTEGER_OVERFLOW;
            if (Aligned <= Existing->Offset && AlignedSize <= Existing->Offset - Aligned)
            {
                Candidate = Aligned;
                Found = TRUE;
                break;
            }
            if (Existing->Offset > Limit || Existing->AlignedSize > Limit - Existing->Offset)
                return STATUS_DATA_ERROR;
            if (Existing->Offset + Existing->AlignedSize > Candidate)
                Candidate = Existing->Offset + Existing->AlignedSize;
        }
    }

    if (!Found)
    {
        if (Dxgmms2VidMmAlignUp(Candidate, Info->Alignment, &Aligned) &&
            Aligned <= Limit && AlignedSize <= Limit - Aligned)
        {
            Candidate = Aligned;
            Found = TRUE;
        }
    }

    if (!Found)
        return STATUS_NO_MEMORY;

    Range = CONTAINING_RECORD(RemoveHeadList(&Core->FreeRangeList), DXGMMS2_VIDMM_RANGE, Entry);
    Range->Offset = Candidate;
    Range->AlignedSize = AlignedSize;
    Range->OwnerCookie = Info->OwnerCookie;
    Range->SegmentIndex = SegmentIndex;
    Range->Priority = Info->Priority;
    Range->Flags = Info->Flags;

    /* Keep the list sorted; append is the common case so scan from the tail. */
    {
        PLIST_ENTRY InsertBefore = &Segment->RangeList;

        for (Entry = Segment->RangeList.Blink; Entry != &Segment->RangeList; Entry = Entry->Blink)
        {
            PDXGMMS2_VIDMM_RANGE Existing = CONTAINING_RECORD(Entry, DXGMMS2_VIDMM_RANGE, Entry);

            if (Existing->Offset <= Candidate)
            {
                InsertBefore = Entry->Flink;
                break;
            }
        }
        InsertTailList(InsertBefore, &Range->Entry);
    }

    Segment->UsedSize += AlignedSize;
    Segment->RangeCount++;
    Core->LiveRangeCount++;
    *OutOffset = Candidate;
    return STATUS_SUCCESS;
}

NTSTATUS
Dxgmms2VidMmCoreRelease(
    _Inout_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentIndex,
    _In_ ULONGLONG OwnerCookie)
{
    PDXGMMS2_VIDMM_SEGMENT Segment;
    PLIST_ENTRY Entry;

    Segment = Dxgmms2VidMmSegment(Core, SegmentIndex);
    if (Segment == NULL || OwnerCookie == 0)
        return STATUS_INVALID_PARAMETER;
    for (Entry = Segment->RangeList.Flink; Entry != &Segment->RangeList; Entry = Entry->Flink)
    {
        PDXGMMS2_VIDMM_RANGE Range = CONTAINING_RECORD(Entry, DXGMMS2_VIDMM_RANGE, Entry);

        if (Range->OwnerCookie != OwnerCookie)
            continue;
        RemoveEntryList(&Range->Entry);
        ASSERT(Segment->UsedSize >= Range->AlignedSize);
        Segment->UsedSize -= Range->AlignedSize;
        Segment->RangeCount--;
        Core->LiveRangeCount--;
        RtlZeroMemory(Range, sizeof(*Range));
        InsertTailList(&Core->FreeRangeList, &Range->Entry);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS
Dxgmms2VidMmCoreSetRangeState(
    _Inout_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentIndex,
    _In_ ULONGLONG OwnerCookie,
    _In_ LONG Priority,
    _In_ ULONG Flags)
{
    PDXGMMS2_VIDMM_SEGMENT Segment;
    PLIST_ENTRY Entry;

    Segment = Dxgmms2VidMmSegment(Core, SegmentIndex);
    if (Segment == NULL || OwnerCookie == 0)
        return STATUS_INVALID_PARAMETER;
    for (Entry = Segment->RangeList.Flink; Entry != &Segment->RangeList; Entry = Entry->Flink)
    {
        PDXGMMS2_VIDMM_RANGE Range = CONTAINING_RECORD(Entry, DXGMMS2_VIDMM_RANGE, Entry);

        if (Range->OwnerCookie != OwnerCookie)
            continue;
        Range->Priority = Priority;
        Range->Flags = Flags;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS
Dxgmms2VidMmCoreQuerySegment(
    _In_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentIndex,
    _Inout_ DXGMMS2_VIDMM_SEGMENT_STATUS_V1 *Status)
{
    PDXGMMS2_VIDMM_SEGMENT Segment = Dxgmms2VidMmSegment((PDXGMMS2_VIDMM_CORE)Core, SegmentIndex);

    if (Segment == NULL)
        return STATUS_INVALID_PARAMETER;
    Status->Size = Segment->Size;
    Status->CommitLimit = Dxgmms2VidMmPlacementLimit(Segment);
    Status->UsedSize = Segment->UsedSize;
    Status->BumpOffset = Segment->BumpOffset;
    Status->PlacementCount = Segment->RangeCount;
    Status->Flags = Segment->Flags;
    return STATUS_SUCCESS;
}

/*
 * Lowest-priority resident placement, skipping anything the caller has
 * marked as pinned.  Offered content is the preferred victim.
 */
BOOLEAN
Dxgmms2VidMmCoreFindEvictionCandidate(
    _In_ PDXGMMS2_VIDMM_CORE Core,
    _In_ ULONG SegmentIndex,
    _In_ ULONG ExcludeFlags,
    _Out_ PULONGLONG OutOwnerCookie)
{
    PDXGMMS2_VIDMM_SEGMENT Segment = Dxgmms2VidMmSegment((PDXGMMS2_VIDMM_CORE)Core, SegmentIndex);
    PDXGMMS2_VIDMM_RANGE Victim = NULL;
    PLIST_ENTRY Entry;

    *OutOwnerCookie = 0;
    if (Segment == NULL)
        return FALSE;
    for (Entry = Segment->RangeList.Flink; Entry != &Segment->RangeList; Entry = Entry->Flink)
    {
        PDXGMMS2_VIDMM_RANGE Range = CONTAINING_RECORD(Entry, DXGMMS2_VIDMM_RANGE, Entry);
        BOOLEAN RangeOffered;
        BOOLEAN VictimOffered;

        if ((Range->Flags & ExcludeFlags) != 0)
            continue;
        RangeOffered = ((Range->Flags & DXGMMS2_VIDMM_RANGE_OFFERED) != 0);
        if (Victim == NULL)
        {
            Victim = Range;
            continue;
        }
        VictimOffered = ((Victim->Flags & DXGMMS2_VIDMM_RANGE_OFFERED) != 0);
        if ((RangeOffered && !VictimOffered) ||
            (RangeOffered == VictimOffered && Range->Priority < Victim->Priority))
            Victim = Range;
    }
    if (Victim == NULL)
        return FALSE;
    *OutOwnerCookie = Victim->OwnerCookie;
    return TRUE;
}

ULONG
Dxgmms2VidMmCoreReleaseAll(
    _Inout_ PDXGMMS2_VIDMM_CORE Core,
    _Out_writes_to_(Capacity, return) PULONGLONG OwnerCookies,
    _In_ ULONG Capacity)
{
    ULONG Count = 0;
    ULONG Index;

    if (!Core->Started)
        return 0;
    for (Index = 0; Index < Core->SegmentCount && Count < Capacity; ++Index)
    {
        PDXGMMS2_VIDMM_SEGMENT Segment = &Core->Segments[Index];

        while (Count < Capacity && !IsListEmpty(&Segment->RangeList))
        {
            PLIST_ENTRY Entry = RemoveHeadList(&Segment->RangeList);
            PDXGMMS2_VIDMM_RANGE Range = CONTAINING_RECORD(Entry, DXGMMS2_VIDMM_RANGE, Entry);

            OwnerCookies[Count++] = Range->OwnerCookie;
            ASSERT(Segment->UsedSize >= Range->AlignedSize);
            Segment->UsedSize -= Range->AlignedSize;
            Segment->RangeCount--;
            Core->LiveRangeCount--;
            RtlZeroMemory(Range, sizeof(*Range));
            InsertTailList(&Core->FreeRangeList, &Range->Entry);
        }
    }
    return Count;
}

/* EOF */
