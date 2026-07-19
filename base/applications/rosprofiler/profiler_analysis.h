/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Metric-aware recording analysis
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "profiler_model.h"

typedef struct _RPERF_FUNCTION_KEY
{
    ULONGLONG ModuleId;
    ULONGLONG Address;
} RPERF_FUNCTION_KEY;

typedef struct _RPERF_FUNCTION_COST
{
    RPERF_FUNCTION_KEY Key;
    ULONGLONG SelfWeight;
    ULONGLONG InclusiveWeight;
    ULONGLONG SelfSamples;
    ULONGLONG InclusiveSamples;
    const RPERF_MODEL_SYMBOL *Symbol;
} RPERF_FUNCTION_COST;

typedef struct _RPERF_CALL_NODE
{
    RPERF_FUNCTION_KEY Key;
    ULONGLONG Weight;
    ULONGLONG Samples;
    SIZE_T Parent;
    SIZE_T FirstChild;
    SIZE_T NextSibling;
    USHORT Depth;
} RPERF_CALL_NODE;

typedef struct _RPERF_CALL_EDGE
{
    RPERF_FUNCTION_KEY Caller;
    RPERF_FUNCTION_KEY Callee;
    ULONGLONG Weight;
    ULONGLONG Samples;
} RPERF_CALL_EDGE;

typedef struct _RPERF_ANALYSIS
{
    volatile LONG References;
    RPERF_RECORDING *Recording;
    RPERF_FILTER Filter;
    RPERF_METRIC_KIND Metric;
    ULONGLONG TotalWeight;
    ULONGLONG TotalSamples;
    ULONGLONG MatchedLostRecords;
    ULONGLONG MatchedLostWeight;
    RPERF_FUNCTION_COST *Functions;
    SIZE_T FunctionCount;
    SIZE_T FunctionCapacity;
    RPERF_CALL_NODE *TopDown;
    SIZE_T TopDownCount;
    SIZE_T TopDownCapacity;
    RPERF_CALL_NODE *BottomUp;
    SIZE_T BottomUpCount;
    SIZE_T BottomUpCapacity;
    RPERF_CALL_EDGE *Edges;
    SIZE_T EdgeCount;
    SIZE_T EdgeCapacity;
} RPERF_ANALYSIS;

RPERF_ANALYSIS *RperfAnalysisBuild(RPERF_RECORDING *Recording,
                                   const RPERF_FILTER *Filter,
                                   HANDLE CancelEvent);
VOID RperfAnalysisAddRef(RPERF_ANALYSIS *Analysis);
VOID RperfAnalysisRelease(RPERF_ANALYSIS *Analysis);
BOOL RperfFunctionKeyEqual(const RPERF_FUNCTION_KEY *Left,
                           const RPERF_FUNCTION_KEY *Right);
const RPERF_FUNCTION_COST *
RperfAnalysisFindFunction(const RPERF_ANALYSIS *Analysis,
                          const RPERF_FUNCTION_KEY *Key);
