/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Backend-neutral immutable recording model
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windows.h>

#define RPERF_MODEL_VERSION 2
#define RPERF_MODEL_MAX_FRAMES 256
#define RPERF_MODEL_ALL_CPUS MAXDWORD
#define RPERF_MODEL_ALL_EVENTS MAXDWORD
#define RPERF_MODEL_ALL_MODULES ((ULONGLONG)-1)
#define RPERF_MODEL_INVALID_ID ((ULONGLONG)-1)

typedef enum _RPERF_BACKEND_KIND
{
    RperfBackendIntrusive = 1,
    RperfBackendFake,
    RperfBackendKernel,
    RperfBackendEtw
} RPERF_BACKEND_KIND;

typedef enum _RPERF_METRIC_KIND
{
    RperfMetricSnapshotPopulation = 1,
    RperfMetricCpuSamples,
    RperfMetricEventWeight,
    RperfMetricWallTime,
    RperfMetricOffCpuTime
} RPERF_METRIC_KIND;

typedef enum _RPERF_COMPLETION_REASON
{
    RperfCompletionIncomplete = 0,
    RperfCompletionDuration,
    RperfCompletionUserStop,
    RperfCompletionTargetExit,
    RperfCompletionError
} RPERF_COMPLETION_REASON;

typedef enum _RPERF_THREAD_STATE_KIND
{
    RperfThreadStateUnknown = 0,
    RperfThreadStateInitialized,
    RperfThreadStateReady,
    RperfThreadStateRunning,
    RperfThreadStateStandby,
    RperfThreadStateTerminated,
    RperfThreadStateWaiting,
    RperfThreadStateTransition,
    RperfThreadStateDeferredReady
} RPERF_THREAD_STATE_KIND;

typedef enum _RPERF_RECORD_KIND
{
    RperfRecordSessionInfo = 1,
    RperfRecordProcessStart,
    RperfRecordProcessEnd,
    RperfRecordThreadStart,
    RperfRecordThreadEnd,
    RperfRecordImageLoad,
    RperfRecordImageUnload,
    RperfRecordSample,
    RperfRecordLost,
    RperfRecordContextSwitch,
    RperfRecordWakeup,
    RperfRecordPmu,
    RperfRecordSecurity,
    RperfRecordClockSync,
    RperfRecordSessionEnd
} RPERF_RECORD_KIND;

typedef enum _RPERF_CONTEXT_KIND
{
    RperfContextUnknown = 0,
    RperfContextUser = 1,
    RperfContextKernel = 2,
    RperfContextTransition = 3
} RPERF_CONTEXT_KIND;

typedef enum _RPERF_RESOLUTION_KIND
{
    RperfResolutionUnknown = 0,
    RperfResolutionAddress = 1,
    RperfResolutionFunction = 2,
    RperfResolutionSource = 3,
    RperfResolutionInline = 4
} RPERF_RESOLUTION_KIND;

typedef enum _RPERF_SYMBOL_SOURCE_KIND
{
    RperfSymbolSourceUnknown = 0,
    RperfSymbolSourcePdb,
    RperfSymbolSourceRosSym,
    RperfSymbolSourceDwarf,
    RperfSymbolSourceCoff,
    RperfSymbolSourceExport,
    RperfSymbolSourceModuleOffset
} RPERF_SYMBOL_SOURCE_KIND;

typedef enum _RPERF_SYMBOL_STATUS_KIND
{
    RperfSymbolStatusUnattempted = 0,
    RperfSymbolStatusResolved,
    RperfSymbolStatusImageMissing,
    RperfSymbolStatusIdentityMismatch,
    RperfSymbolStatusSymbolsMissing,
    RperfSymbolStatusLoadError
} RPERF_SYMBOL_STATUS_KIND;

typedef enum _RPERF_LOSS_REASON
{
    RperfLossUnknown = 0,
    RperfLossBufferFull,
    RperfLossAllocation,
    RperfLossRecursion,
    RperfLossDisabledEvent,
    RperfLossUnsafeUnwind,
    RperfLossSequenceGap,
    RperfLossUserspaceSnapshot
} RPERF_LOSS_REASON;

#define RPERF_MODEL_RECORD_FLAG_TRUNCATED       0x00000001
#define RPERF_MODEL_RECORD_FLAG_UNWIND_FAILED   0x00000002
#define RPERF_MODEL_RECORD_FLAG_INCOMPLETE      0x00000004
#define RPERF_MODEL_RECORD_FLAG_FILTERED        0x00000008
#define RPERF_MODEL_RECORD_FLAG_REDACTED        0x00000010
#define RPERF_MODEL_RECORD_FLAG_SYNTHETIC       0x00000020
#define RPERF_MODEL_RECORD_FLAG_STATE_KNOWN     0x00000040
#define RPERF_MODEL_RECORD_FLAG_WAITING         0x00000080
#define RPERF_MODEL_RECORD_FLAG_WAIT_REASON_SHIFT 16
#define RPERF_MODEL_RECORD_FLAG_WAIT_REASON_MASK 0x00ff0000

#define RPERF_FILTER_TIME                 0x00000001
#define RPERF_FILTER_PROCESS              0x00000002
#define RPERF_FILTER_THREAD               0x00000004
#define RPERF_FILTER_CPU                  0x00000008
#define RPERF_FILTER_EVENT                0x00000010
#define RPERF_FILTER_CONTEXT              0x00000020
#define RPERF_FILTER_MODULE               0x00000040
#define RPERF_FILTER_RESOLUTION           0x00000080
#define RPERF_FILTER_TRUNCATION           0x00000100
#define RPERF_FILTER_LOSS                 0x00000200

typedef struct _RPERF_CAPTURE_LIMITS
{
    ULONGLONG MaxFileBytes;
    ULONGLONG MaxRecords;
    ULONGLONG MaxSamples;
    ULONGLONG MaxSymbols;
    ULONGLONG MaxModules;
    ULONG MaxThreads;
    ULONG MaxFrames;
    ULONG MaxRecordBytes;
    ULONG MaxStringBytes;
    ULONG MaxDurationMs;
} RPERF_CAPTURE_LIMITS;

typedef struct _RPERF_CAPTURE_COUNTERS
{
    ULONGLONG AttemptedSamples;
    ULONGLONG SuccessfulSamples;
    ULONGLONG FailedSamples;
    ULONGLONG SkippedSamples;
    ULONGLONG TruncatedSamples;
    ULONGLONG ThreadOpenFailures;
    ULONGLONG ThreadOwnershipFailures;
    ULONGLONG SuspendFailures;
    ULONGLONG ContextFailures;
    ULONGLONG UnwindFailures;
    ULONGLONG ResumeFailures;
    ULONGLONG MissedCadenceTicks;
    ULONGLONG LostRecords;
    ULONGLONG LostWeight;
    ULONGLONG SchemaSkips;
    ULONGLONG MalformedRecords;
    ULONGLONG BytesWritten;
} RPERF_CAPTURE_COUNTERS;

typedef struct _RPERF_RECORD_HEADER
{
    RPERF_RECORD_KIND Kind;
    ULONG Flags;
    ULONGLONG TimestampNs;
    ULONGLONG Sequence;
    ULONGLONG ProcessKey;
    ULONGLONG ThreadKey;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG Cpu;
    ULONG EventId;
} RPERF_RECORD_HEADER;

typedef struct _RPERF_FRAME
{
    ULONGLONG Address;
    ULONGLONG FunctionAddress;
    ULONGLONG ModuleId;
    RPERF_CONTEXT_KIND Context;
    RPERF_RESOLUTION_KIND Resolution;
} RPERF_FRAME;

typedef struct _RPERF_SAMPLE_RECORD
{
    ULONGLONG Weight;
    ULONGLONG Period;
    USHORT Depth;
    RPERF_FRAME Frames[RPERF_MODEL_MAX_FRAMES]; /* leaf first */
} RPERF_SAMPLE_RECORD;

typedef struct _RPERF_LOST_RECORD
{
    RPERF_LOSS_REASON Reason;
    ULONGLONG FirstSequence;
    ULONGLONG LastSequence;
    ULONGLONG Count;
    ULONGLONG Weight;
} RPERF_LOST_RECORD;

typedef struct _RPERF_LIFECYCLE_RECORD
{
    ULONGLONG ObjectId;
    ULONGLONG ParentId;
    ULONGLONG ModuleId;
    ULONGLONG ImageBase;
    ULONGLONG ImageSize;
    ULONG ExitStatus;
} RPERF_LIFECYCLE_RECORD;

typedef struct _RPERF_SCHEDULER_RECORD
{
    ULONGLONG OldProcessKey;
    ULONGLONG OldThreadKey;
    ULONGLONG NewProcessKey;
    ULONGLONG NewThreadKey;
    ULONG OldProcessId;
    ULONG OldThreadId;
    ULONG NewProcessId;
    ULONG NewThreadId;
    ULONG State;
    ULONG Reason;
    ULONG TargetCpu;
    ULONG Flags;
    ULONGLONG DurationNs;
} RPERF_SCHEDULER_RECORD;

typedef struct _RPERF_CLOCK_RECORD
{
    ULONGLONG SystemTime100ns;
    ULONGLONG PerformanceCounter;
    ULONGLONG PerformanceFrequency;
    ULONGLONG InterruptTime100ns;
    ULONG Flags;
} RPERF_CLOCK_RECORD;

typedef struct _RPERF_RECORD
{
    RPERF_RECORD_HEADER Header;
    union
    {
        RPERF_SAMPLE_RECORD Sample;
        RPERF_LOST_RECORD Lost;
        RPERF_LIFECYCLE_RECORD Lifecycle;
        RPERF_SCHEDULER_RECORD Scheduler;
        RPERF_CLOCK_RECORD Clock;
    } Data;
} RPERF_RECORD;

typedef struct _RPERF_MODULE
{
    ULONGLONG Id;
    ULONGLONG ProcessKey;
    ULONGLONG Base;
    ULONGLONG Size;
    ULONG Architecture;
    ULONG Flags;
    ULONG TimeDateStamp;
    ULONG Checksum;
    UCHAR DebugId[16];
    ULONG DebugAge;
    PWSTR Path;
} RPERF_MODULE;

typedef struct _RPERF_MODEL_SYMBOL
{
    ULONGLONG Address;
    ULONGLONG FunctionAddress;
    ULONGLONG ModuleId;
    ULONGLONG RelativeAddress;
    RPERF_RESOLUTION_KIND Resolution;
    RPERF_SYMBOL_SOURCE_KIND Source;
    RPERF_SYMBOL_STATUS_KIND Status;
    PSTR Name;
    PSTR ModuleName;
    PSTR SourceFile;
    ULONG SourceLine;
} RPERF_MODEL_SYMBOL;

typedef struct _RPERF_RECORDING_INFO
{
    GUID SessionId;
    RPERF_BACKEND_KIND Backend;
    RPERF_METRIC_KIND Metric;
    ULONG ProducerArchitecture;
    ULONG AddressWidth;
    ULONG ClockId;
    ULONGLONG ClockFrequency;
    ULONGLONG StartTimeNs;
    ULONGLONG EndTimeNs;
    ULONG IntervalMs;
    ULONG RequestedDurationMs;
    RPERF_COMPLETION_REASON CompletionReason;
    ULONG CompletionError;
    BOOL Complete;
    PWSTR TargetName;
    PWSTR SourcePath;
} RPERF_RECORDING_INFO;

typedef VOID (CALLBACK *RPERF_RECORD_SINK)(PVOID Context,
                                           const RPERF_RECORD *Record);

typedef struct _RPERF_RECORDING
{
    volatile LONG References;
    BOOL Frozen;
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_RECORDING_INFO Info;
    RPERF_CAPTURE_COUNTERS Counters;
    RPERF_RECORD *Records;
    SIZE_T RecordCount;
    SIZE_T RecordCapacity;
    RPERF_MODULE *Modules;
    SIZE_T ModuleCount;
    SIZE_T ModuleCapacity;
    RPERF_MODEL_SYMBOL *Symbols;
    SIZE_T SymbolCount;
    SIZE_T SymbolCapacity;
    RPERF_RECORD_SINK Sink;
    PVOID SinkContext;
} RPERF_RECORDING;

typedef struct _RPERF_FILTER
{
    ULONG Enabled;
    ULONGLONG StartNs;
    ULONGLONG EndNs;
    ULONGLONG ProcessKey;
    ULONGLONG ThreadKey;
    ULONG Cpu;
    ULONG EventId;
    ULONG ContextMask;
    ULONGLONG ModuleId;
    ULONG ResolutionMask;
    BOOL IncludeTruncated;
    BOOL IncludeLoss;
} RPERF_FILTER;

ULONG RperfNativeArchitecture(VOID);
VOID RperfDefaultCaptureLimits(RPERF_CAPTURE_LIMITS *Limits);
VOID RperfInitializeFilter(RPERF_FILTER *Filter);
RPERF_RECORDING *RperfRecordingCreate(const RPERF_CAPTURE_LIMITS *Limits);
RPERF_RECORDING *RperfRecordingClone(const RPERF_RECORDING *Source);
RPERF_RECORDING *RperfRecordingCloneMutable(const RPERF_RECORDING *Source);
VOID RperfRecordingAddRef(RPERF_RECORDING *Recording);
VOID RperfRecordingRelease(RPERF_RECORDING *Recording);
BOOL RperfRecordingSetTargetName(RPERF_RECORDING *Recording, PCWSTR Name);
BOOL RperfRecordingSetSourcePath(RPERF_RECORDING *Recording, PCWSTR Path);
BOOL RperfRecordingAddModule(RPERF_RECORDING *Recording,
                             const RPERF_MODULE *Module);
BOOL RperfRecordingAddSymbol(RPERF_RECORDING *Recording,
                             const RPERF_MODEL_SYMBOL *Symbol);
BOOL RperfRecordingAddRecord(RPERF_RECORDING *Recording,
                             const RPERF_RECORD *Record);
VOID RperfRecordingSetSink(RPERF_RECORDING *Recording,
                           RPERF_RECORD_SINK Sink,
                           PVOID SinkContext);
BOOL RperfRecordingFreeze(RPERF_RECORDING *Recording);
const RPERF_MODULE *RperfRecordingFindModule(const RPERF_RECORDING *Recording,
                                             ULONGLONG ModuleId);
const RPERF_MODEL_SYMBOL *
RperfRecordingFindSymbol(const RPERF_RECORDING *Recording,
                         ULONGLONG Address);
BOOL RperfRecordMatchesFilter(const RPERF_RECORDING *Recording,
                              const RPERF_RECORD *Record,
                              const RPERF_FILTER *Filter);
