/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Adapt the portable recording model to the GUI views
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
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
