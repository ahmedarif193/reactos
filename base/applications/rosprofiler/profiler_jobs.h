/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Cancellable parse, symbolization, and analysis jobs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_analysis.h"
#include "profiler_codec.h"
#include "profiler_legacy_bridge.h"
#include "profiler_symbolizer.h"
#include "profiler_viewmodel.h"

typedef enum _RPERF_JOB_KIND
{
    RperfJobOpen = 1,
    RperfJobSymbolize,
    RperfJobAnalyze,
    RperfJobPrepareLegacy
} RPERF_JOB_KIND;

typedef struct _RPERF_JOB RPERF_JOB;

typedef VOID (CALLBACK *RPERF_JOB_PROGRESS)(PVOID Context,
                                            ULONGLONG Generation,
                                            RPERF_JOB_KIND Kind,
                                            ULONGLONG Completed,
                                            ULONGLONG Total);
typedef VOID (CALLBACK *RPERF_JOB_COMPLETE)(PVOID Context,
                                            ULONGLONG Generation,
                                            RPERF_JOB_KIND Kind,
                                            DWORD Status);

RPERF_JOB *RperfJobStartOpen(ULONGLONG Generation,
                             PCWSTR Path,
                             const RPERF_CAPTURE_LIMITS *Limits,
                             RPERF_JOB_PROGRESS Progress,
                             RPERF_JOB_COMPLETE Complete,
                             PVOID Context);
RPERF_JOB *RperfJobStartSymbolize(ULONGLONG Generation,
                                  RPERF_RECORDING *Recording,
                                  RPERF_SYMBOL_PROVIDER *Provider,
                                  RPERF_JOB_PROGRESS Progress,
                                  RPERF_JOB_COMPLETE Complete,
                                  PVOID Context);
RPERF_JOB *RperfJobStartAnalysis(ULONGLONG Generation,
                                 RPERF_RECORDING *Recording,
                                 const RPERF_FILTER *Filter,
                                 RPERF_JOB_PROGRESS Progress,
                                 RPERF_JOB_COMPLETE Complete,
                                 PVOID Context);
RPERF_JOB *RperfJobStartPrepareLegacy(ULONGLONG Generation,
                                      RPERF_RECORDING *Recording,
                                      PCWSTR SourcePath,
                                      SIZE_T TimelineBucketCount,
                                      RPERF_JOB_PROGRESS Progress,
                                      RPERF_JOB_COMPLETE Complete,
                                      PVOID Context);
BOOL RperfJobCancel(RPERF_JOB *Job);
BOOL RperfJobJoin(RPERF_JOB *Job, DWORD TimeoutMilliseconds);
DWORD RperfJobGetStatus(const RPERF_JOB *Job);
RPERF_RECORDING *RperfJobTakeRecording(RPERF_JOB *Job);
RPERF_ANALYSIS *RperfJobTakeAnalysis(RPERF_JOB *Job);
RPERF_SESSION *RperfJobTakeLegacySession(RPERF_JOB *Job);
RPERF_TIMELINE_VIEW *RperfJobTakeTimelineView(RPERF_JOB *Job);
VOID RperfJobDestroy(RPERF_JOB *Job);
