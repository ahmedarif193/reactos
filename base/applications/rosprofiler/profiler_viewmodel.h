/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Scalable immutable hot-table and timeline view models
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_analysis.h"

#define RPERF_TIMELINE_DEFAULT_MAX_LANES 64

typedef struct _RPERF_HOT_ROW
{
    SIZE_T FunctionIndex;
    ULONGLONG SelfWeight;
    ULONGLONG InclusiveWeight;
    ULONGLONG SelfSamples;
    ULONGLONG InclusiveSamples;
} RPERF_HOT_ROW;

typedef struct _RPERF_HOT_VIEW
{
    RPERF_ANALYSIS *Analysis;
    RPERF_HOT_ROW *Rows;
    SIZE_T RowCount;
} RPERF_HOT_VIEW;

typedef struct _RPERF_TIMELINE_CELL
{
    ULONGLONG Samples;
    ULONGLONG Weight;
    ULONGLONG Loss;
    ULONGLONG RunningNs;
    ULONGLONG ReadyNs;
    ULONGLONG WaitingNs;
    ULONG ContextSwitches;
    ULONG Wakeups;
    ULONG ThreadStarts;
    ULONG ThreadEnds;
    ULONG Cpu;
    ULONG WaitReason;
    ULONG Flags;
} RPERF_TIMELINE_CELL;

typedef struct _RPERF_TIMELINE_LANE
{
    ULONGLONG ThreadKey;
    ULONGLONG ProcessKey;
    ULONG ProcessId;
    ULONG ThreadId;
} RPERF_TIMELINE_LANE;

#define RPERF_TIMELINE_CELL_MIXED_CPU 0x20000000
#define RPERF_TIMELINE_CELL_MIXED_REASON 0x10000000
#define RPERF_TIMELINE_CELL_WAIT_REASON_KNOWN 0x08000000

typedef struct _RPERF_TIMELINE_VIEW
{
    RPERF_RECORDING *Recording;
    RPERF_FILTER Filter;
    ULONGLONG StartNs;
    ULONGLONG EndNs;
    ULONGLONG BucketWidthNs;
    RPERF_TIMELINE_LANE *Lanes;
    SIZE_T LaneCount;
    SIZE_T BucketCount;
    RPERF_TIMELINE_CELL *Cells; /* lane-major */
    ULONG *CpuIds;
    SIZE_T CpuCount;
    RPERF_TIMELINE_CELL *CpuCells; /* CPU-major */
    ULONGLONG *LossCells;
    ULONGLONG GlobalLoss;
    ULONGLONG ContextSwitchCount;
    ULONGLONG WakeupCount;
    BOOL HasScheduler;
    BOOL HasLifecycle;
    BOOL LanesTruncated;
    BOOL CpusTruncated;
} RPERF_TIMELINE_VIEW;

RPERF_HOT_VIEW *RperfHotViewCreate(RPERF_ANALYSIS *Analysis);
VOID RperfHotViewSort(RPERF_HOT_VIEW *View,
                      ULONG Column,
                      BOOL Ascending);
VOID RperfHotViewDestroy(RPERF_HOT_VIEW *View);
RPERF_TIMELINE_VIEW *
RperfTimelineViewCreate(RPERF_RECORDING *Recording,
                        const RPERF_FILTER *Filter,
                        SIZE_T MaximumLanes,
                        SIZE_T BucketCount,
                        HANDLE CancelEvent);
ULONGLONG
RperfTimelineFindProcessKey(const RPERF_RECORDING *Recording,
                            ULONG ProcessId);
const RPERF_TIMELINE_CELL *
RperfTimelineCell(const RPERF_TIMELINE_VIEW *View,
                  SIZE_T Lane,
                  SIZE_T Bucket);
const RPERF_TIMELINE_CELL *
RperfTimelineCpuCell(const RPERF_TIMELINE_VIEW *View,
                     SIZE_T Cpu,
                     SIZE_T Bucket);
VOID RperfTimelineViewDestroy(RPERF_TIMELINE_VIEW *View);
