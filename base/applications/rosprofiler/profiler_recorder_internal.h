/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal recorder implementation contract
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_recorder.h"
#include "profiler_baseline.h"
#include <reactos/rosprof.h>

typedef struct _RPERF_RECORDER_OPS
{
    BOOL (*Start)(PVOID State);
    BOOL (*RequestStop)(PVOID State);
    BOOL (*Join)(PVOID State, DWORD TimeoutMilliseconds);
    BOOL (*GetCounters)(PVOID State, RPERF_CAPTURE_COUNTERS *Counters);
    RPERF_RECORDING *(*TakeRecording)(PVOID State);
    VOID (*Destroy)(PVOID State);
} RPERF_RECORDER_OPS;

struct _RPERF_RECORDER
{
    const RPERF_RECORDER_OPS *Ops;
    PVOID BackendState;
    volatile LONG State;
};

BOOL RperfIntrusiveQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Capabilities);
BOOL RperfIntrusiveCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                          const RPERF_RECORDER_OPS **Ops,
                          PVOID *State);
BOOL RperfFakeQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Capabilities);
BOOL RperfFakeCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                     const RPERF_RECORDER_OPS **Ops,
                     PVOID *State);
BOOL RperfKernelQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Capabilities);
BOOL RperfKernelCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                       const RPERF_RECORDER_OPS **Ops,
                       PVOID *State);
BOOL RperfKernelDecodeImageRecord(const ROSPROF_RECORD_HEADER *Header,
                                  ULONG MaximumStringBytes,
                                  ULONG FallbackArchitecture,
                                  ULONGLONG ProcessKey,
                                  RPERF_MODULE *Module,
                                  PWSTR *Path);
BOOL RperfKernelSequenceAfter(ULONGLONG Sequence,
                              ULONGLONG Previous);
BOOL RperfEtwQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Capabilities);
BOOL RperfEtwCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                    const RPERF_RECORDER_OPS **Ops,
                    PVOID *State);
BOOL RperfEtwDecodeLifecycleIds(const VOID *Data,
                                USHORT DataLength,
                                UCHAR Version,
                                ULONG PointerSize,
                                BOOL Thread,
                                ULONG *ProcessId,
                                ULONG *ThreadId);
BOOL RperfEtwDecodeImageRecord(const VOID *Data,
                               USHORT DataLength,
                               ULONG PointerSize,
                               ULONG Architecture,
                               UCHAR Version,
                               ULONG MaximumStringBytes,
                               ULONGLONG ProcessKey,
                               RPERF_MODULE *Module,
                               PWSTR *Path);
