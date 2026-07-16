/* Adapter from the portable recording model to the current GUI views. */
#pragma once

#include "profiler_model.h"
#include "rosprofiler.h"

BOOL RperfLegacySessionFromRecording(const RPERF_RECORDING *Recording,
                                     PCWSTR SourcePath,
                                     RPERF_SESSION *Session);
BOOL RperfLegacySessionFromRecordingEx(const RPERF_RECORDING *Recording,
                                       PCWSTR SourcePath,
                                       HANDLE CancelEvent,
                                       RPERF_SESSION_PROGRESS Progress,
                                       PVOID ProgressContext,
                                       RPERF_SESSION *Session);
