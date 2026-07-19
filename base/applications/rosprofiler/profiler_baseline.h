/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Bounded userspace baseline for live streams without rundown
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_recorder.h"

typedef BOOL (CALLBACK *RPERF_BASELINE_ID_CALLBACK)(PVOID Context,
                                                    BOOL Thread,
                                                    ULONG NumericId,
                                                    ULONGLONG StableId,
                                                    ULONGLONG CreationTime100ns,
                                                    BOOL CreationTimeValid);

typedef struct _RPERF_BASELINE_RESULT
{
    BOOL Partial;
    DWORD Status;
    ULONG Processes;
    ULONG Threads;
    ULONG Modules;
} RPERF_BASELINE_RESULT;

BOOL RperfCaptureBaseline(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                          RPERF_RECORDING *Recording,
                          ULONGLONG TimestampNs,
                          ULONGLONG *NextSequence,
                          RPERF_BASELINE_ID_CALLBACK IdCallback,
                          PVOID IdContext,
                          RPERF_BASELINE_RESULT *Result);

BOOL RperfCaptureSystemModuleBaseline(RPERF_RECORDING *Recording,
                                      ULONGLONG TimestampNs,
                                      ULONGLONG *NextSequence,
                                      RPERF_BASELINE_RESULT *Result);
