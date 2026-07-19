/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Deterministic filtered exports and cross-session comparison
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_analysis.h"
#include "profiler_codec.h"

typedef enum _RPERF_EXPORT_KIND
{
    RperfExportFolded = 1,
    RperfExportCsv,
    RperfExportRawV2
} RPERF_EXPORT_KIND;

typedef struct _RPERF_COMPARISON_ENTRY
{
    RPERF_FUNCTION_KEY Key;
    ULONGLONG BaselineSelf;
    ULONGLONG BaselineInclusive;
    ULONGLONG CandidateSelf;
    ULONGLONG CandidateInclusive;
    LONGLONG SelfDeltaPpm;
    LONGLONG InclusiveDeltaPpm;
} RPERF_COMPARISON_ENTRY;

typedef struct _RPERF_COMPARISON
{
    RPERF_ANALYSIS *Baseline;
    RPERF_ANALYSIS *Candidate;
    RPERF_COMPARISON_ENTRY *Entries;
    SIZE_T EntryCount;
} RPERF_COMPARISON;

BOOL RperfExportAnalysis(PCWSTR Path,
                         RPERF_EXPORT_KIND Kind,
                         const RPERF_ANALYSIS *Analysis,
                         HANDLE CancelEvent);
RPERF_COMPARISON *RperfCompareAnalyses(RPERF_ANALYSIS *Baseline,
                                       RPERF_ANALYSIS *Candidate,
                                       HANDLE CancelEvent);
VOID RperfComparisonDestroy(RPERF_COMPARISON *Comparison);
