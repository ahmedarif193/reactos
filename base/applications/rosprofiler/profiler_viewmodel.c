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

static VOID
RperfTimelineInsertValue(ULONGLONG *Values,
                         SIZE_T *Count,
                         SIZE_T Capacity,
                         BOOL *Truncated,
                         ULONGLONG Value)
{
    SIZE_T Low = 0, High = *Count;

    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;

        if (Values[Middle] < Value)
            Low = Middle + 1;
        else
            High = Middle;
    }
    if (Low < *Count && Values[Low] == Value)
        return;
    if (*Count == Capacity)
    {
        *Truncated = TRUE;
        if (Low == Capacity)
            return;
        MoveMemory(&Values[Low + 1],
                   &Values[Low],
                   (Capacity - Low - 1) * sizeof(*Values));
    }
    else
    {
        MoveMemory(&Values[Low + 1],
                   &Values[Low],
                   (*Count - Low) * sizeof(*Values));
        (*Count)++;
    }
    Values[Low] = Value;
}

static int __cdecl
RperfCompareTimelineEvents(const void *Left,
                           const void *Right)
{
    const RPERF_RECORD *A = *(const RPERF_RECORD * const *)Left;
    const RPERF_RECORD *B = *(const RPERF_RECORD * const *)Right;

    if (A->Header.TimestampNs < B->Header.TimestampNs)
        return -1;
    if (A->Header.TimestampNs > B->Header.TimestampNs)
        return 1;
    if (A->Header.Sequence < B->Header.Sequence)
        return -1;
    if (A->Header.Sequence > B->Header.Sequence)
        return 1;
    return A->Header.Kind < B->Header.Kind ? -1 :
           A->Header.Kind > B->Header.Kind ? 1 : 0;
}

static ULONGLONG
RperfTimelineThreadKey(ULONGLONG ThreadKey,
                       ULONG ThreadId)
{
    if (ThreadKey != 0)
        return ThreadKey;
    if (ThreadId != 0)
        return (1ULL << 63) | ThreadId;
    return 0;
}

ULONGLONG
RperfTimelineFindProcessKey(const RPERF_RECORDING *Recording,
                            ULONG ProcessId)
{
    SIZE_T Index;

    if (Recording == NULL || ProcessId == 0)
        return 0;
    for (Index = 0; Index < Recording->RecordCount; Index++)
    {
        const RPERF_RECORD *Record = &Recording->Records[Index];

        if (Record->Header.ProcessId == ProcessId &&
            Record->Header.ProcessKey != 0)
        {
            return Record->Header.ProcessKey;
        }
        if (Record->Header.Kind == RperfRecordContextSwitch ||
            Record->Header.Kind == RperfRecordWakeup)
        {
            const RPERF_SCHEDULER_RECORD *Scheduler =
                &Record->Data.Scheduler;

            if (Scheduler->OldProcessId == ProcessId &&
                Scheduler->OldProcessKey != 0)
            {
                return Scheduler->OldProcessKey;
            }
            if (Scheduler->NewProcessId == ProcessId &&
                Scheduler->NewProcessKey != 0)
            {
                return Scheduler->NewProcessKey;
            }
        }
    }
    return 0;
}

static BOOL
RperfTimelineContinue(HANDLE CancelEvent,
                      SIZE_T Index)
{
    if ((Index & 4095) != 0 || CancelEvent == NULL)
        return TRUE;
    if (WaitForSingleObject(CancelEvent, 0) == WAIT_TIMEOUT)
        return TRUE;
    SetLastError(ERROR_CANCELLED);
    return FALSE;
}

static BOOL
RperfTimelineBaseMatches(const RPERF_RECORD *Record,
                         const RPERF_FILTER *Filter)
{
    RPERF_FILTER Base = *Filter;

    Base.Enabled &= ~(RPERF_FILTER_PROCESS | RPERF_FILTER_THREAD);
    return RperfRecordMatchesFilter(NULL, Record, &Base);
}

static BOOL
RperfTimelineSideMatches(const RPERF_FILTER *Filter,
                         ULONGLONG ProcessKey,
                         ULONGLONG ThreadKey)
{
    if ((Filter->Enabled & RPERF_FILTER_PROCESS) != 0 &&
        ProcessKey != Filter->ProcessKey)
        return FALSE;
    if ((Filter->Enabled & RPERF_FILTER_THREAD) != 0 &&
        ThreadKey != Filter->ThreadKey)
        return FALSE;
    return TRUE;
}

static BOOL
RperfTimelineSchedulerMatches(const RPERF_RECORD *Record,
                              const RPERF_FILTER *Filter)
{
    const RPERF_SCHEDULER_RECORD *Scheduler = &Record->Data.Scheduler;

    if (!RperfTimelineBaseMatches(Record, Filter))
        return FALSE;
    return RperfTimelineSideMatches(Filter,
                                    Scheduler->OldProcessKey,
                                    RperfTimelineThreadKey(
                                        Scheduler->OldThreadKey,
                                        Scheduler->OldThreadId)) ||
           RperfTimelineSideMatches(Filter,
                                    Scheduler->NewProcessKey,
                                    RperfTimelineThreadKey(
                                        Scheduler->NewThreadKey,
                                        Scheduler->NewThreadId));
}

static BOOL
RperfTimelineEventMatches(const RPERF_RECORDING *Recording,
                          const RPERF_RECORD *Record,
                          const RPERF_FILTER *Filter)
{
    if (Record->Header.Kind == RperfRecordContextSwitch ||
        Record->Header.Kind == RperfRecordWakeup)
    {
        return RperfTimelineSchedulerMatches(Record, Filter);
    }
    if (Record->Header.Kind == RperfRecordLost)
        return RperfTimelineBaseMatches(Record, Filter);
    return RperfRecordMatchesFilter(Recording, Record, Filter);
}

static BOOL
RperfTimelineIsEvent(ULONG Kind)
{
    return Kind == RperfRecordContextSwitch ||
           Kind == RperfRecordWakeup ||
           Kind == RperfRecordThreadStart ||
           Kind == RperfRecordThreadEnd ||
           Kind == RperfRecordLost;
}

static SIZE_T
RperfFindLane(const RPERF_TIMELINE_VIEW *View,
              ULONGLONG ThreadKey)
{
    SIZE_T Low = 0, High = View->LaneCount;
    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (View->Lanes[Middle].ThreadKey < ThreadKey)
            Low = Middle + 1;
        else
            High = Middle;
    }
    return Low < View->LaneCount &&
           View->Lanes[Low].ThreadKey == ThreadKey ?
           Low : (SIZE_T)-1;
}

static SIZE_T
RperfFindCpu(const RPERF_TIMELINE_VIEW *View,
             ULONG Cpu)
{
    SIZE_T Low = 0, High = View->CpuCount;

    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (View->CpuIds[Middle] < Cpu)
            Low = Middle + 1;
        else
            High = Middle;
    }
    return Low < View->CpuCount && View->CpuIds[Low] == Cpu ?
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

static VOID
RperfTimelineAddU64(ULONGLONG *Value,
                    ULONGLONG Addend)
{
    *Value = Addend > (ULONGLONG)-1 - *Value ?
             (ULONGLONG)-1 : *Value + Addend;
}

static VOID
RperfTimelineIncrementU32(ULONG *Value)
{
    if (*Value != MAXDWORD)
        (*Value)++;
}

static VOID
RperfTimelineUpdateCpu(RPERF_TIMELINE_CELL *Cell,
                       ULONG Cpu)
{
    if (Cpu == RPERF_MODEL_ALL_CPUS)
        return;
    if ((Cell->Flags & RPERF_TIMELINE_CELL_MIXED_CPU) != 0)
        return;
    if (Cell->Cpu == RPERF_MODEL_ALL_CPUS)
        Cell->Cpu = Cpu;
    else if (Cell->Cpu != Cpu)
    {
        Cell->Cpu = RPERF_MODEL_ALL_CPUS;
        Cell->Flags |= RPERF_TIMELINE_CELL_MIXED_CPU;
    }
}

static VOID
RperfTimelineUpdateWaitReason(RPERF_TIMELINE_CELL *Cell,
                              ULONG Reason)
{
    if ((Cell->Flags & RPERF_TIMELINE_CELL_MIXED_REASON) != 0)
        return;
    if ((Cell->Flags & RPERF_TIMELINE_CELL_WAIT_REASON_KNOWN) == 0)
    {
        Cell->WaitReason = Reason;
        Cell->Flags |= RPERF_TIMELINE_CELL_WAIT_REASON_KNOWN;
    }
    else if (Cell->WaitReason != Reason)
    {
        Cell->WaitReason = 0;
        Cell->Flags |= RPERF_TIMELINE_CELL_MIXED_REASON;
    }
}

static BOOL
RperfTimelineStateIsReady(ULONG State)
{
    return State == RperfThreadStateReady ||
           State == RperfThreadStateStandby ||
           State == RperfThreadStateTransition ||
           State == RperfThreadStateDeferredReady;
}

static RPERF_TIMELINE_CELL *
RperfTimelineMutableCell(RPERF_TIMELINE_VIEW *View,
                         SIZE_T Lane,
                         ULONGLONG Timestamp)
{
    SIZE_T Bucket;

    if (Lane >= View->LaneCount)
        return NULL;
    Bucket = RperfTimelineBucket(View, Timestamp);
    return &View->Cells[Lane * View->BucketCount + Bucket];
}

typedef enum _RPERF_TIMELINE_THREAD_STATE
{
    RperfTimelineStateUnknown = 0,
    RperfTimelineStateRunning,
    RperfTimelineStateReady,
    RperfTimelineStateWaiting
} RPERF_TIMELINE_THREAD_STATE;

typedef struct _RPERF_TIMELINE_STATE_SPAN
{
    RPERF_TIMELINE_THREAD_STATE State;
    ULONGLONG StartNs;
    ULONG Cpu;
    ULONG Reason;
} RPERF_TIMELINE_STATE_SPAN;

static VOID
RperfTimelineAddDuration(RPERF_TIMELINE_VIEW *View,
                         SIZE_T Lane,
                         ULONGLONG StartNs,
                         ULONGLONG EndNs,
                         RPERF_TIMELINE_THREAD_STATE State,
                         ULONG Cpu,
                         ULONG Reason)
{
    SIZE_T First, Last, Bucket, CpuIndex = (SIZE_T)-1;

    if (Lane >= View->LaneCount || EndNs <= StartNs ||
        EndNs <= View->StartNs || StartNs >= View->EndNs)
        return;
    if (StartNs < View->StartNs)
        StartNs = View->StartNs;
    if (EndNs > View->EndNs)
        EndNs = View->EndNs;
    First = RperfTimelineBucket(View, StartNs);
    Last = RperfTimelineBucket(View, EndNs - 1);
    if ((State == RperfTimelineStateRunning ||
         State == RperfTimelineStateReady) &&
        Cpu != RPERF_MODEL_ALL_CPUS)
    {
        CpuIndex = RperfFindCpu(View, Cpu);
    }
    for (Bucket = First; Bucket <= Last; Bucket++)
    {
        ULONGLONG CellStart = View->StartNs +
                              Bucket * View->BucketWidthNs;
        ULONGLONG CellEnd = Bucket + 1 == View->BucketCount ?
                            View->EndNs :
                            CellStart + View->BucketWidthNs;
        ULONGLONG OverlapStart = max(StartNs, CellStart);
        ULONGLONG OverlapEnd = min(EndNs, CellEnd);
        ULONGLONG Duration;
        RPERF_TIMELINE_CELL *Cell;
        RPERF_TIMELINE_CELL *CpuCell;

        if (OverlapEnd <= OverlapStart)
            continue;
        Duration = OverlapEnd - OverlapStart;
        Cell = &View->Cells[Lane * View->BucketCount + Bucket];
        RperfTimelineUpdateCpu(Cell, Cpu);
        if (State == RperfTimelineStateRunning)
            RperfTimelineAddU64(&Cell->RunningNs, Duration);
        else if (State == RperfTimelineStateReady)
            RperfTimelineAddU64(&Cell->ReadyNs, Duration);
        else if (State == RperfTimelineStateWaiting)
        {
            RperfTimelineAddU64(&Cell->WaitingNs, Duration);
            RperfTimelineUpdateWaitReason(Cell, Reason);
        }

        CpuCell = CpuIndex != (SIZE_T)-1 ?
                  &View->CpuCells[CpuIndex * View->BucketCount + Bucket] :
                  NULL;
        if (CpuCell != NULL)
        {
            if (State == RperfTimelineStateRunning)
                RperfTimelineAddU64(&CpuCell->RunningNs, Duration);
            else
                RperfTimelineAddU64(&CpuCell->ReadyNs, Duration);
        }
    }
}

static VOID
RperfTimelineCloseState(RPERF_TIMELINE_VIEW *View,
                        RPERF_TIMELINE_STATE_SPAN *States,
                        SIZE_T Lane,
                        ULONGLONG Timestamp)
{
    RPERF_TIMELINE_STATE_SPAN *State;

    if (Lane >= View->LaneCount)
        return;
    State = &States[Lane];
    if (State->State != RperfTimelineStateUnknown)
    {
        RperfTimelineAddDuration(View, Lane, State->StartNs, Timestamp,
                                 State->State, State->Cpu, State->Reason);
    }
    State->State = RperfTimelineStateUnknown;
    State->StartNs = Timestamp;
    State->Cpu = RPERF_MODEL_ALL_CPUS;
    State->Reason = 0;
}

static VOID
RperfTimelineStartState(RPERF_TIMELINE_STATE_SPAN *States,
                        SIZE_T Lane,
                        ULONGLONG Timestamp,
                        RPERF_TIMELINE_THREAD_STATE State,
                        ULONG Cpu,
                        ULONG Reason)
{
    States[Lane].State = State;
    States[Lane].StartNs = Timestamp;
    States[Lane].Cpu = Cpu;
    States[Lane].Reason = Reason;
}

static VOID
RperfTimelineSetLaneIdentity(RPERF_TIMELINE_VIEW *View,
                             ULONGLONG ThreadKey,
                             ULONGLONG ProcessKey,
                             ULONG ProcessId,
                             ULONG ThreadId)
{
    SIZE_T Lane = RperfFindLane(View, ThreadKey);

    if (Lane == (SIZE_T)-1)
        return;
    if (View->Lanes[Lane].ProcessKey == 0)
        View->Lanes[Lane].ProcessKey = ProcessKey;
    if (View->Lanes[Lane].ProcessId == 0)
        View->Lanes[Lane].ProcessId = ProcessId;
    if (View->Lanes[Lane].ThreadId == 0)
        View->Lanes[Lane].ThreadId = ThreadId;
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
    ULONGLONG *Keys = NULL;
    ULONGLONG *Cpus = NULL;
    const RPERF_RECORD **Events = NULL;
    RPERF_TIMELINE_STATE_SPAN *States = NULL;
    SIZE_T KeyCount = 0, CpuKeyCount = 0, EventCount = 0;
    SIZE_T EventCapacity, RecordIndex, Cells, CellIndex;
    BOOL LanesTruncated = FALSE, CpusTruncated = FALSE;

    if (Recording == NULL || !Recording->Frozen ||
        MaximumLanes == 0 || BucketCount == 0)
        return NULL;
    if (Filter != NULL)
        Effective = *Filter;
    else
        RperfInitializeFilter(&Effective);
    if (MaximumLanes > ((SIZE_T)-1) / sizeof(*Keys))
        return NULL;
    if (Recording->RecordCount != 0)
    {
        Keys = HeapAlloc(GetProcessHeap(), 0,
                         MaximumLanes * sizeof(*Keys));
        Cpus = HeapAlloc(GetProcessHeap(), 0,
                         MaximumLanes * sizeof(*Cpus));
        if (Keys == NULL || Cpus == NULL)
        {
            if (Keys != NULL)
                HeapFree(GetProcessHeap(), 0, Keys);
            if (Cpus != NULL)
                HeapFree(GetProcessHeap(), 0, Cpus);
            return NULL;
        }
    }
    for (RecordIndex = 0; RecordIndex < Recording->RecordCount; ++RecordIndex)
    {
        const RPERF_RECORD *Record = &Recording->Records[RecordIndex];
        ULONGLONG Key;
        BOOL EventMatch;

        if (!RperfTimelineContinue(CancelEvent, RecordIndex))
        {
            HeapFree(GetProcessHeap(), 0, Keys);
            HeapFree(GetProcessHeap(), 0, Cpus);
            return NULL;
        }
        EventMatch = RperfTimelineIsEvent(Record->Header.Kind) &&
                     RperfTimelineEventMatches(Recording,
                                               Record,
                                               &Effective);
        if (EventMatch)
            EventCount++;
        if (Record->Header.Kind == RperfRecordContextSwitch ||
            Record->Header.Kind == RperfRecordWakeup)
        {
            const RPERF_SCHEDULER_RECORD *Scheduler =
                &Record->Data.Scheduler;
            ULONGLONG OldKey, NewKey;
            BOOL OldMatches, NewMatches;

            if (!EventMatch)
                continue;
            OldKey = RperfTimelineThreadKey(Scheduler->OldThreadKey,
                                            Scheduler->OldThreadId);
            NewKey = RperfTimelineThreadKey(Scheduler->NewThreadKey,
                                            Scheduler->NewThreadId);
            OldMatches = OldKey != 0 &&
                         RperfTimelineSideMatches(&Effective,
                                                  Scheduler->OldProcessKey,
                                                  OldKey);
            NewMatches = NewKey != 0 &&
                         RperfTimelineSideMatches(&Effective,
                                                  Scheduler->NewProcessKey,
                                                  NewKey);
            if (OldMatches)
            {
                RperfTimelineInsertValue(Keys, &KeyCount, MaximumLanes,
                                         &LanesTruncated, OldKey);
            }
            if (NewMatches)
            {
                RperfTimelineInsertValue(Keys, &KeyCount, MaximumLanes,
                                         &LanesTruncated, NewKey);
            }
            if (Record->Header.Kind == RperfRecordContextSwitch &&
                NewMatches && Record->Header.Cpu != RPERF_MODEL_ALL_CPUS)
            {
                RperfTimelineInsertValue(Cpus, &CpuKeyCount, MaximumLanes,
                                         &CpusTruncated,
                                         Record->Header.Cpu);
            }
            if (((Record->Header.Kind == RperfRecordContextSwitch &&
                  OldMatches &&
                  RperfTimelineStateIsReady(Scheduler->State)) ||
                 (Record->Header.Kind == RperfRecordWakeup && NewMatches)) &&
                Scheduler->TargetCpu != RPERF_MODEL_ALL_CPUS)
            {
                RperfTimelineInsertValue(Cpus, &CpuKeyCount, MaximumLanes,
                                         &CpusTruncated,
                                         Scheduler->TargetCpu);
            }
            continue;
        }
        if (!RperfRecordMatchesFilter(Recording, Record, &Effective))
            continue;
        if (Record->Header.Kind == RperfRecordSample ||
            Record->Header.Kind == RperfRecordThreadStart ||
            Record->Header.Kind == RperfRecordThreadEnd)
        {
            Key = RperfTimelineThreadKey(Record->Header.ThreadKey,
                                         Record->Header.ThreadId);
            if (Key != 0)
            {
                RperfTimelineInsertValue(Keys, &KeyCount, MaximumLanes,
                                         &LanesTruncated, Key);
            }
        }
        if (Record->Header.Cpu != RPERF_MODEL_ALL_CPUS)
        {
            RperfTimelineInsertValue(Cpus, &CpuKeyCount, MaximumLanes,
                                     &CpusTruncated, Record->Header.Cpu);
        }
    }
    View = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*View));
    if (View == NULL)
    {
        HeapFree(GetProcessHeap(), 0, Keys);
        HeapFree(GetProcessHeap(), 0, Cpus);
        return NULL;
    }
    View->Recording = Recording;
    RperfRecordingAddRef(Recording);
    View->Filter = Effective;
    View->LanesTruncated = LanesTruncated;
    View->LaneCount = KeyCount;
    View->BucketCount = BucketCount;
    if (View->LaneCount != 0)
    {
        View->Lanes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                View->LaneCount * sizeof(*View->Lanes));
        if (View->Lanes == NULL)
            goto Failure;
        for (RecordIndex = 0; RecordIndex < View->LaneCount; RecordIndex++)
            View->Lanes[RecordIndex].ThreadKey = Keys[RecordIndex];
    }
    HeapFree(GetProcessHeap(), 0, Keys);
    Keys = NULL;

    View->CpusTruncated = CpusTruncated;
    View->CpuCount = CpuKeyCount;
    if (View->CpuCount != 0)
    {
        View->CpuIds = HeapAlloc(GetProcessHeap(), 0,
                                 View->CpuCount * sizeof(*View->CpuIds));
        if (View->CpuIds == NULL)
            goto Failure;
        for (RecordIndex = 0; RecordIndex < View->CpuCount; RecordIndex++)
            View->CpuIds[RecordIndex] = (ULONG)Cpus[RecordIndex];
    }
    HeapFree(GetProcessHeap(), 0, Cpus);
    Cpus = NULL;

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
    {
        for (RecordIndex = 0; RecordIndex < Recording->RecordCount;
             RecordIndex++)
        {
            ULONGLONG Timestamp =
                Recording->Records[RecordIndex].Header.TimestampNs;
            if (RecordIndex == 0 || Timestamp < View->StartNs)
                View->StartNs = Timestamp;
            if (RecordIndex == 0 || Timestamp > View->EndNs)
                View->EndNs = Timestamp;
        }
        if (View->EndNs <= View->StartNs)
            View->EndNs = View->StartNs + 1;
    }
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
    if (View->CpuCount != 0 &&
        BucketCount > ((SIZE_T)-1) / View->CpuCount)
        goto Failure;
    Cells = View->CpuCount * BucketCount;
    if (Cells > 16000000 || Cells > ((SIZE_T)-1) / sizeof(*View->CpuCells))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        goto Failure;
    }
    View->CpuCells = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                               Cells * sizeof(*View->CpuCells));
    if (View->CpuCells == NULL && Cells != 0)
        goto Failure;
    View->LossCells = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                BucketCount * sizeof(*View->LossCells));
    if (View->LossCells == NULL)
        goto Failure;
    for (CellIndex = 0;
         CellIndex < View->LaneCount * BucketCount;
         CellIndex++)
    {
        View->Cells[CellIndex].Cpu = RPERF_MODEL_ALL_CPUS;
    }
    EventCapacity = EventCount;
    if (EventCapacity > ((SIZE_T)-1) / sizeof(*Events))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        goto Failure;
    }
    if (EventCapacity != 0)
    {
        Events = HeapAlloc(GetProcessHeap(), 0,
                           EventCapacity * sizeof(*Events));
        if (Events == NULL)
            goto Failure;
    }
    EventCount = 0;
    for (RecordIndex = 0; RecordIndex < Recording->RecordCount; ++RecordIndex)
    {
        const RPERF_RECORD *Record = &Recording->Records[RecordIndex];
        BOOL EventMatch;
        SIZE_T Lane, Bucket;
        RPERF_TIMELINE_CELL *Cell;

        if (!RperfTimelineContinue(CancelEvent, RecordIndex))
            goto Failure;
        EventMatch = RperfTimelineIsEvent(Record->Header.Kind) &&
                     RperfTimelineEventMatches(Recording,
                                               Record,
                                               &Effective);
        if (EventMatch)
        {
            if (EventCount == EventCapacity)
            {
                SetLastError(ERROR_BUFFER_OVERFLOW);
                goto Failure;
            }
            Events[EventCount++] = Record;
        }
        if (Record->Header.Kind == RperfRecordContextSwitch ||
            Record->Header.Kind == RperfRecordWakeup)
        {
            const RPERF_SCHEDULER_RECORD *Scheduler =
                &Record->Data.Scheduler;
            ULONGLONG OldKey = RperfTimelineThreadKey(
                Scheduler->OldThreadKey, Scheduler->OldThreadId);
            ULONGLONG NewKey = RperfTimelineThreadKey(
                Scheduler->NewThreadKey, Scheduler->NewThreadId);

            if (!EventMatch)
                continue;
            RperfTimelineSetLaneIdentity(
                View, OldKey, Scheduler->OldProcessKey,
                Scheduler->OldProcessId, Scheduler->OldThreadId);
            RperfTimelineSetLaneIdentity(
                View, NewKey, Scheduler->NewProcessKey,
                Scheduler->NewProcessId, Scheduler->NewThreadId);
            continue;
        }
        if (!RperfRecordMatchesFilter(Recording, Record, &Effective))
            continue;
        if (Record->Header.Kind == RperfRecordSample ||
            Record->Header.Kind == RperfRecordThreadStart ||
            Record->Header.Kind == RperfRecordThreadEnd)
        {
            RperfTimelineSetLaneIdentity(
                View,
                RperfTimelineThreadKey(Record->Header.ThreadKey,
                                       Record->Header.ThreadId),
                Record->Header.ProcessKey,
                Record->Header.ProcessId,
                Record->Header.ThreadId);
        }
        if (Record->Header.Kind == RperfRecordSample)
        {
            RPERF_TIMELINE_CELL *CpuCell = NULL;
            ULONGLONG Key;
            SIZE_T CpuIndex;

            Key = RperfTimelineThreadKey(Record->Header.ThreadKey,
                                         Record->Header.ThreadId);
            Lane = RperfFindLane(View, Key);
            if (Lane == (SIZE_T)-1)
                continue;
            Bucket = RperfTimelineBucket(View, Record->Header.TimestampNs);
            Cell = &View->Cells[Lane * BucketCount + Bucket];
            RperfTimelineAddU64(&Cell->Samples, 1);
            RperfTimelineAddU64(&Cell->Weight,
                                Record->Data.Sample.Weight);
            Cell->Flags |= Record->Header.Flags;
            RperfTimelineUpdateCpu(Cell, Record->Header.Cpu);
            CpuIndex = RperfFindCpu(View, Record->Header.Cpu);
            if (CpuIndex != (SIZE_T)-1)
            {
                CpuCell = &View->CpuCells[
                    CpuIndex * View->BucketCount + Bucket];
            }
            if (CpuCell != NULL)
            {
                RperfTimelineAddU64(&CpuCell->Samples, 1);
                RperfTimelineAddU64(&CpuCell->Weight,
                                    Record->Data.Sample.Weight);
                CpuCell->Flags |= Record->Header.Flags;
            }
        }
    }

    if (EventCount > 1)
    {
        qsort(Events, EventCount, sizeof(*Events),
              RperfCompareTimelineEvents);
    }
    if (View->LaneCount != 0)
    {
        States = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           View->LaneCount * sizeof(*States));
        if (States == NULL)
            goto Failure;
        for (CellIndex = 0; CellIndex < View->LaneCount; CellIndex++)
            States[CellIndex].Cpu = RPERF_MODEL_ALL_CPUS;
    }

    for (RecordIndex = 0; RecordIndex < EventCount; RecordIndex++)
    {
        const RPERF_RECORD *Record = Events[RecordIndex];
        ULONGLONG Timestamp = Record->Header.TimestampNs;

        if (!RperfTimelineContinue(CancelEvent, RecordIndex))
            goto Failure;
        if (Record->Header.Kind == RperfRecordLost)
        {
            ULONGLONG Count = Record->Data.Lost.Count != 0 ?
                              Record->Data.Lost.Count : 1;
            SIZE_T Bucket = RperfTimelineBucket(View, Timestamp);

            for (CellIndex = 0;
                 CellIndex < View->LaneCount;
                 CellIndex++)
            {
                RperfTimelineCloseState(View,
                                        States,
                                        CellIndex,
                                        Timestamp);
            }
            RperfTimelineAddU64(&View->GlobalLoss, Count);
            RperfTimelineAddU64(&View->LossCells[Bucket], Count);
            continue;
        }
        if (Record->Header.Kind == RperfRecordThreadStart ||
            Record->Header.Kind == RperfRecordThreadEnd)
        {
            ULONGLONG Key = RperfTimelineThreadKey(
                Record->Header.ThreadKey, Record->Header.ThreadId);
            SIZE_T Lane = RperfFindLane(View, Key);
            RPERF_TIMELINE_CELL *Cell =
                RperfTimelineMutableCell(View, Lane, Timestamp);

            View->HasLifecycle = TRUE;
            if (Cell != NULL)
            {
                if (Record->Header.Kind == RperfRecordThreadStart)
                    RperfTimelineIncrementU32(&Cell->ThreadStarts);
                else
                {
                    RperfTimelineIncrementU32(&Cell->ThreadEnds);
                    RperfTimelineCloseState(View, States, Lane, Timestamp);
                }
            }
            continue;
        }
        if (Record->Header.Kind == RperfRecordWakeup)
        {
            const RPERF_SCHEDULER_RECORD *Scheduler =
                &Record->Data.Scheduler;
            ULONGLONG Key = RperfTimelineThreadKey(
                Scheduler->NewThreadKey, Scheduler->NewThreadId);
            SIZE_T Lane = RperfFindLane(View, Key);
            RPERF_TIMELINE_CELL *Cell =
                RperfTimelineMutableCell(View, Lane, Timestamp);

            View->HasScheduler = TRUE;
            RperfTimelineAddU64(&View->WakeupCount, 1);
            if (Cell != NULL)
            {
                RperfTimelineIncrementU32(&Cell->Wakeups);
                if (States[Lane].State == RperfTimelineStateWaiting)
                    RperfTimelineCloseState(View, States, Lane, Timestamp);
                if (States[Lane].State != RperfTimelineStateRunning &&
                    States[Lane].State != RperfTimelineStateReady)
                {
                    RperfTimelineStartState(
                        States, Lane, Timestamp, RperfTimelineStateReady,
                        Scheduler->TargetCpu, 0);
                }
            }
            continue;
        }
        if (Record->Header.Kind == RperfRecordContextSwitch)
        {
            const RPERF_SCHEDULER_RECORD *Scheduler =
                &Record->Data.Scheduler;
            ULONGLONG OldKey = RperfTimelineThreadKey(
                Scheduler->OldThreadKey, Scheduler->OldThreadId);
            ULONGLONG NewKey = RperfTimelineThreadKey(
                Scheduler->NewThreadKey, Scheduler->NewThreadId);
            SIZE_T OldLane = RperfFindLane(View, OldKey);
            SIZE_T NewLane = RperfFindLane(View, NewKey);
            RPERF_TIMELINE_CELL *Cell;

            View->HasScheduler = TRUE;
            RperfTimelineAddU64(&View->ContextSwitchCount, 1);
            if (OldLane != (SIZE_T)-1)
            {
                Cell = RperfTimelineMutableCell(View, OldLane, Timestamp);
                if (Cell != NULL)
                {
                    RperfTimelineIncrementU32(&Cell->ContextSwitches);
                }
                RperfTimelineCloseState(View, States, OldLane, Timestamp);
                if (Scheduler->State == RperfThreadStateWaiting)
                {
                    RperfTimelineStartState(
                        States, OldLane, Timestamp,
                        RperfTimelineStateWaiting,
                        RPERF_MODEL_ALL_CPUS, Scheduler->Reason);
                }
                else if (RperfTimelineStateIsReady(Scheduler->State))
                {
                    RperfTimelineStartState(
                        States, OldLane, Timestamp,
                        RperfTimelineStateReady,
                        Scheduler->TargetCpu, 0);
                }
            }
            if (NewLane != (SIZE_T)-1)
            {
                Cell = RperfTimelineMutableCell(View, NewLane, Timestamp);
                if (Cell != NULL)
                {
                    RperfTimelineIncrementU32(&Cell->ContextSwitches);
                }
                RperfTimelineCloseState(View, States, NewLane, Timestamp);
                RperfTimelineStartState(
                    States, NewLane, Timestamp,
                    RperfTimelineStateRunning,
                    Record->Header.Cpu, 0);
            }
        }
    }

    for (CellIndex = 0; CellIndex < View->LaneCount; CellIndex++)
        RperfTimelineCloseState(View, States, CellIndex, View->EndNs);
    if (States != NULL)
        HeapFree(GetProcessHeap(), 0, States);
    if (Events != NULL)
        HeapFree(GetProcessHeap(), 0, Events);
    return View;

Failure:
    if (States != NULL)
        HeapFree(GetProcessHeap(), 0, States);
    if (Events != NULL)
        HeapFree(GetProcessHeap(), 0, Events);
    if (Keys != NULL)
        HeapFree(GetProcessHeap(), 0, Keys);
    if (Cpus != NULL)
        HeapFree(GetProcessHeap(), 0, Cpus);
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

const RPERF_TIMELINE_CELL *
RperfTimelineCpuCell(const RPERF_TIMELINE_VIEW *View,
                     SIZE_T Cpu,
                     SIZE_T Bucket)
{
    if (View == NULL || Cpu >= View->CpuCount ||
        Bucket >= View->BucketCount)
        return NULL;
    return &View->CpuCells[Cpu * View->BucketCount + Bucket];
}

VOID
RperfTimelineViewDestroy(RPERF_TIMELINE_VIEW *View)
{
    if (View == NULL)
        return;
    if (View->Recording != NULL)
        RperfRecordingRelease(View->Recording);
    if (View->Lanes != NULL)
        HeapFree(GetProcessHeap(), 0, View->Lanes);
    if (View->Cells != NULL)
        HeapFree(GetProcessHeap(), 0, View->Cells);
    if (View->CpuIds != NULL)
        HeapFree(GetProcessHeap(), 0, View->CpuIds);
    if (View->CpuCells != NULL)
        HeapFree(GetProcessHeap(), 0, View->CpuCells);
    if (View->LossCells != NULL)
        HeapFree(GetProcessHeap(), 0, View->LossCells);
    HeapFree(GetProcessHeap(), 0, View);
}
