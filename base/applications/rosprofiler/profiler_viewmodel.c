/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Scalable immutable hot-table and timeline view models
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_viewmodel.h"

#include <stdlib.h>

static ULONG RperfHotSortColumn;
static BOOL RperfHotSortAscending;

static int __cdecl
RperfHotCompare(const void *Left,
                const void *Right)
{
    const RPERF_HOT_ROW *A = Left;
    const RPERF_HOT_ROW *B = Right;
    ULONGLONG ValueA, ValueB;
    int Result;

    switch (RperfHotSortColumn)
    {
        case 0: ValueA = A->SelfWeight; ValueB = B->SelfWeight; break;
        case 1: ValueA = A->InclusiveWeight; ValueB = B->InclusiveWeight; break;
        case 2: ValueA = A->SelfSamples; ValueB = B->SelfSamples; break;
        case 3: ValueA = A->InclusiveSamples; ValueB = B->InclusiveSamples; break;
        default: ValueA = A->FunctionIndex; ValueB = B->FunctionIndex; break;
    }
    Result = ValueA < ValueB ? -1 : ValueA > ValueB ? 1 : 0;
    return RperfHotSortAscending ? Result : -Result;
}

RPERF_HOT_VIEW *
RperfHotViewCreate(RPERF_ANALYSIS *Analysis)
{
    RPERF_HOT_VIEW *View;
    SIZE_T Index;
    if (Analysis == NULL ||
        Analysis->FunctionCount > ((SIZE_T)-1) / sizeof(*View->Rows))
        return NULL;
    View = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*View));
    if (View == NULL)
        return NULL;
    View->Rows = HeapAlloc(GetProcessHeap(), 0,
                           Analysis->FunctionCount * sizeof(*View->Rows));
    if (View->Rows == NULL && Analysis->FunctionCount != 0)
    {
        HeapFree(GetProcessHeap(), 0, View);
        return NULL;
    }
    View->Analysis = Analysis;
    RperfAnalysisAddRef(Analysis);
    View->RowCount = Analysis->FunctionCount;
    for (Index = 0; Index < View->RowCount; ++Index)
    {
        View->Rows[Index].FunctionIndex = Index;
        View->Rows[Index].SelfWeight = Analysis->Functions[Index].SelfWeight;
        View->Rows[Index].InclusiveWeight =
            Analysis->Functions[Index].InclusiveWeight;
        View->Rows[Index].SelfSamples = Analysis->Functions[Index].SelfSamples;
        View->Rows[Index].InclusiveSamples =
            Analysis->Functions[Index].InclusiveSamples;
    }
    RperfHotViewSort(View, 1, FALSE);
    return View;
}

VOID
RperfHotViewSort(RPERF_HOT_VIEW *View,
                 ULONG Column,
                 BOOL Ascending)
{
    if (View == NULL || View->RowCount < 2)
        return;
    RperfHotSortColumn = Column;
    RperfHotSortAscending = Ascending;
    qsort(View->Rows, View->RowCount, sizeof(*View->Rows), RperfHotCompare);
}

VOID
RperfHotViewDestroy(RPERF_HOT_VIEW *View)
{
    if (View == NULL)
        return;
    if (View->Analysis != NULL)
        RperfAnalysisRelease(View->Analysis);
    if (View->Rows != NULL)
        HeapFree(GetProcessHeap(), 0, View->Rows);
    HeapFree(GetProcessHeap(), 0, View);
}

static int __cdecl
RperfCompareU64(const void *Left,
                const void *Right)
{
    ULONGLONG A = *(const ULONGLONG *)Left;
    ULONGLONG B = *(const ULONGLONG *)Right;
    return A < B ? -1 : A > B ? 1 : 0;
}

static SIZE_T
RperfFindLane(const RPERF_TIMELINE_VIEW *View,
              ULONGLONG ThreadKey)
{
    SIZE_T Low = 0, High = View->LaneCount;
    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (View->ThreadKeys[Middle] < ThreadKey)
            Low = Middle + 1;
        else
            High = Middle;
    }
    return Low < View->LaneCount && View->ThreadKeys[Low] == ThreadKey ?
           Low : (SIZE_T)-1;
}

static SIZE_T
RperfTimelineBucket(const RPERF_TIMELINE_VIEW *View,
                    ULONGLONG Timestamp)
{
    ULONGLONG Delta;
    SIZE_T Bucket;
    if (Timestamp <= View->StartNs)
        return 0;
    if (Timestamp >= View->EndNs)
        return View->BucketCount - 1;
    Delta = Timestamp - View->StartNs;
    Bucket = (SIZE_T)(Delta / View->BucketWidthNs);
    return Bucket < View->BucketCount ? Bucket : View->BucketCount - 1;
}

RPERF_TIMELINE_VIEW *
RperfTimelineViewCreate(RPERF_RECORDING *Recording,
                        const RPERF_FILTER *Filter,
                        SIZE_T MaximumLanes,
                        SIZE_T BucketCount,
                        HANDLE CancelEvent)
{
    RPERF_TIMELINE_VIEW *View;
    RPERF_FILTER Effective;
    ULONGLONG *Keys;
    SIZE_T KeyCount = 0, RecordIndex, Unique, Cells;

    if (Recording == NULL || !Recording->Frozen ||
        MaximumLanes == 0 || BucketCount == 0)
        return NULL;
    if (Filter != NULL)
        Effective = *Filter;
    else
        RperfInitializeFilter(&Effective);
    if (Recording->RecordCount > ((SIZE_T)-1) / sizeof(*Keys))
        return NULL;
    Keys = HeapAlloc(GetProcessHeap(), 0,
                     Recording->RecordCount * sizeof(*Keys));
    if (Keys == NULL && Recording->RecordCount != 0)
        return NULL;
    for (RecordIndex = 0; RecordIndex < Recording->RecordCount; ++RecordIndex)
    {
        const RPERF_RECORD *Record = &Recording->Records[RecordIndex];
        if ((RecordIndex & 4095) == 0 && CancelEvent != NULL &&
            WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
        {
            HeapFree(GetProcessHeap(), 0, Keys);
            SetLastError(ERROR_CANCELLED);
            return NULL;
        }
        if (Record->Header.ThreadKey != 0 &&
            RperfRecordMatchesFilter(Recording, Record, &Effective))
            Keys[KeyCount++] = Record->Header.ThreadKey;
    }
    qsort(Keys, KeyCount, sizeof(*Keys), RperfCompareU64);
    Unique = 0;
    for (RecordIndex = 0; RecordIndex < KeyCount; ++RecordIndex)
    {
        if (RecordIndex == 0 || Keys[RecordIndex] != Keys[RecordIndex - 1])
            Keys[Unique++] = Keys[RecordIndex];
    }
    View = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*View));
    if (View == NULL)
    {
        HeapFree(GetProcessHeap(), 0, Keys);
        return NULL;
    }
    View->Recording = Recording;
    RperfRecordingAddRef(Recording);
    View->Filter = Effective;
    View->LanesTruncated = Unique > MaximumLanes;
    View->LaneCount = min(Unique, MaximumLanes);
    View->BucketCount = BucketCount;
    View->ThreadKeys = Keys;
    if (Effective.Enabled & RPERF_FILTER_TIME)
    {
        View->StartNs = Effective.StartNs;
        View->EndNs = Effective.EndNs;
    }
    else
    {
        View->StartNs = Recording->Info.StartTimeNs;
        View->EndNs = Recording->Info.EndTimeNs;
    }
    if (View->EndNs <= View->StartNs)
        View->EndNs = View->StartNs + 1;
    View->BucketWidthNs =
        (View->EndNs - View->StartNs + BucketCount - 1) / BucketCount;
    if (View->BucketWidthNs == 0)
        View->BucketWidthNs = 1;
    if (View->LaneCount != 0 && BucketCount > ((SIZE_T)-1) / View->LaneCount)
        goto Failure;
    Cells = View->LaneCount * BucketCount;
    if (Cells > 16000000 || Cells > ((SIZE_T)-1) / sizeof(*View->Cells))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        goto Failure;
    }
    View->Cells = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            Cells * sizeof(*View->Cells));
    if (View->Cells == NULL && Cells != 0)
        goto Failure;
    for (RecordIndex = 0; RecordIndex < Recording->RecordCount; ++RecordIndex)
    {
        const RPERF_RECORD *Record = &Recording->Records[RecordIndex];
        SIZE_T Lane, Bucket;
        RPERF_TIMELINE_CELL *Cell;
        if (!RperfRecordMatchesFilter(Recording, Record, &Effective))
            continue;
        if (Record->Header.Kind == RperfRecordLost)
        {
            View->GlobalLoss += Record->Data.Lost.Count;
            continue;
        }
        Lane = RperfFindLane(View, Record->Header.ThreadKey);
        if (Lane == (SIZE_T)-1)
            continue;
        Bucket = RperfTimelineBucket(View, Record->Header.TimestampNs);
        Cell = &View->Cells[Lane * BucketCount + Bucket];
        if (Record->Header.Kind == RperfRecordSample)
        {
            Cell->Samples++;
            Cell->Weight += Record->Data.Sample.Weight;
            Cell->Flags |= Record->Header.Flags;
        }
        else if (Record->Header.Kind == RperfRecordContextSwitch ||
                 Record->Header.Kind == RperfRecordWakeup)
        {
            Cell->Weight += Record->Data.Scheduler.DurationNs;
            Cell->Flags |= 0x80000000;
        }
    }
    return View;

Failure:
    RperfTimelineViewDestroy(View);
    return NULL;
}

const RPERF_TIMELINE_CELL *
RperfTimelineCell(const RPERF_TIMELINE_VIEW *View,
                  SIZE_T Lane,
                  SIZE_T Bucket)
{
    if (View == NULL || Lane >= View->LaneCount ||
        Bucket >= View->BucketCount)
        return NULL;
    return &View->Cells[Lane * View->BucketCount + Bucket];
}

VOID
RperfTimelineViewDestroy(RPERF_TIMELINE_VIEW *View)
{
    if (View == NULL)
        return;
    if (View->Recording != NULL)
        RperfRecordingRelease(View->Recording);
    if (View->ThreadKeys != NULL)
        HeapFree(GetProcessHeap(), 0, View->ThreadKeys);
    if (View->Cells != NULL)
        HeapFree(GetProcessHeap(), 0, View->Cells);
    HeapFree(GetProcessHeap(), 0, View);
}
