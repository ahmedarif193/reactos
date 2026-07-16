/* Bounded userspace baseline for profilers whose live streams lack rundown. */
#pragma once

#include "profiler_recorder.h"

typedef BOOL (CALLBACK *RPERF_BASELINE_ID_CALLBACK)(PVOID Context,
                                                    BOOL Thread,
                                                    ULONG NumericId,
                                                    ULONGLONG StableId);

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
