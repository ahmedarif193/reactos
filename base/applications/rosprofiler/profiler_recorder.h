/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Pluggable recorder contract
 */

#pragma once

#include "profiler_model.h"

#define RPERF_CAP_TIMER             0x00000001
#define RPERF_CAP_USER_STACKS       0x00000002
#define RPERF_CAP_KERNEL_STACKS     0x00000004
#define RPERF_CAP_PMU               0x00000008
#define RPERF_CAP_PROCESS_EVENTS    0x00000010
#define RPERF_CAP_THREAD_EVENTS     0x00000020
#define RPERF_CAP_IMAGE_EVENTS      0x00000040
#define RPERF_CAP_SCHEDULER_EVENTS  0x00000080
#define RPERF_CAP_LOSS_ACCOUNTING   0x00000100
#define RPERF_CAP_PROCESS_TREE      0x00000200
#define RPERF_CAP_SYSTEM_WIDE       0x00000400
#define RPERF_CAP_STACK_WALK        0x00000800
#define RPERF_CAP_THREAD_SCOPE      0x00001000

typedef enum _RPERF_CAPTURE_SCOPE
{
    RperfScopeProcess = 1,
    RperfScopeProcessTree,
    RperfScopeSelectedThreads,
    RperfScopeSystem
} RPERF_CAPTURE_SCOPE;

typedef enum _RPERF_RECORDER_STATE
{
    RperfRecorderCreated = 0,
    RperfRecorderRunning,
    RperfRecorderStopping,
    RperfRecorderStopped,
    RperfRecorderFailed
} RPERF_RECORDER_STATE;

typedef struct _RPERF_RECORDER_CAPABILITIES
{
    BOOL Available;
    BOOL RequiresPrivilege;
    ULONG Features;
    ULONG MinimumIntervalUs;
    ULONG MaximumStackDepth;
    ULONG AbiVersion;
    DWORD Status;
    WCHAR Description[256];
} RPERF_RECORDER_CAPABILITIES;

typedef struct _RPERF_CAPTURE_CONFIGURATION
{
    RPERF_BACKEND_KIND Backend;
    RPERF_CAPTURE_SCOPE Scope;
    ULONG ProcessId;
    const ULONG *ThreadIds;
    SIZE_T ThreadCount;
    ULONG IntervalUs;
    ULONGLONG Period;
    ULONG DurationMs;
    ULONG EventId;
    BOOL IncludeUser;
    BOOL IncludeKernel;
    BOOL FollowChildren;
    BOOL AllowExplicitFallback;
    PCWSTR TargetName;
    PCWSTR OutputPath;
    HWND LegacyNotifyWindow;
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_RECORD_SINK RecordSink;
    PVOID RecordSinkContext;
} RPERF_CAPTURE_CONFIGURATION;

typedef struct _RPERF_RECORDER RPERF_RECORDER;

VOID RperfInitializeCaptureConfiguration(RPERF_CAPTURE_CONFIGURATION *Config);
BOOL RperfRecorderQueryCapabilities(RPERF_BACKEND_KIND Backend,
                                    RPERF_RECORDER_CAPABILITIES *Capabilities);
RPERF_BACKEND_KIND RperfRecorderPreferredBackend(VOID);
BOOL RperfRecorderValidateConfiguration(
    const RPERF_CAPTURE_CONFIGURATION *Configuration,
    RPERF_RECORDER_CAPABILITIES *Capabilities);
BOOL RperfRecorderCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                         RPERF_RECORDER **Recorder);
BOOL RperfRecorderStart(RPERF_RECORDER *Recorder);
BOOL RperfRecorderRequestStop(RPERF_RECORDER *Recorder);
BOOL RperfRecorderJoin(RPERF_RECORDER *Recorder,
                       DWORD TimeoutMilliseconds);
RPERF_RECORDER_STATE RperfRecorderGetState(const RPERF_RECORDER *Recorder);
BOOL RperfRecorderGetCounters(const RPERF_RECORDER *Recorder,
                              RPERF_CAPTURE_COUNTERS *Counters);
RPERF_RECORDING *RperfRecorderTakeRecording(RPERF_RECORDER *Recorder);
VOID RperfRecorderDestroy(RPERF_RECORDER *Recorder);
