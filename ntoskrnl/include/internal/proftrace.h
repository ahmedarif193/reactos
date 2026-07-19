/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Private profiling trace engine contract
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * Private kernel contract for the ReactOS profiling trace engine.
 *
 * This is deliberately not a user ABI.  The public control-device bridge must
 * translate the versioned public structures into these structures instead of
 * exposing kernel pointers or the in-kernel ring representation.
 */

#define KPROF_TRACE_CONFIG_VERSION          1
#define KPROF_TRACE_CAPABILITIES_VERSION    1
#define KPROF_TRACE_STATS_VERSION           1

#define KPROF_TRACE_MAX_SESSIONS            8
#define KPROF_TRACE_MIN_RING_SIZE           (16 * 1024)
#define KPROF_TRACE_MAX_RING_SIZE           (4 * 1024 * 1024)
#define KPROF_TRACE_MAX_SESSION_RING_BYTES  (64ULL * 1024 * 1024)

#define KPROF_TRACE_FLAG_SAMPLE             0x00000001
#define KPROF_TRACE_FLAG_PROCESS            0x00000002
#define KPROF_TRACE_FLAG_THREAD             0x00000004
#define KPROF_TRACE_FLAG_IMAGE              0x00000008
#define KPROF_TRACE_FLAG_CONTEXT_SWITCH     0x00000010
#define KPROF_TRACE_FLAG_CALLCHAIN          0x00000020
#define KPROF_TRACE_FLAG_PMU                0x00000040
#define KPROF_TRACE_FLAG_SCHED_WAKEUP        0x00000080
#define KPROF_TRACE_FLAG_CLOCK_SYNC          0x00000100
#define KPROF_TRACE_FLAG_EXCLUDE_OWNER       0x00000200
#define KPROF_TRACE_FLAG_SCHEDULER          \
    (KPROF_TRACE_FLAG_CONTEXT_SWITCH | KPROF_TRACE_FLAG_SCHED_WAKEUP)

#define KPROF_TRACE_SUPPORTED_FLAGS         \
    (KPROF_TRACE_FLAG_SAMPLE | KPROF_TRACE_FLAG_PROCESS | \
     KPROF_TRACE_FLAG_THREAD | KPROF_TRACE_FLAG_IMAGE | \
     KPROF_TRACE_FLAG_CONTEXT_SWITCH | KPROF_TRACE_FLAG_SCHED_WAKEUP | \
     KPROF_TRACE_FLAG_CLOCK_SYNC | KPROF_TRACE_FLAG_EXCLUDE_OWNER | \
     KPROF_TRACE_FLAG_CALLCHAIN)

#define KPROF_TRACE_CAP_TIMER               0x00000001
#define KPROF_TRACE_CAP_PROCESS             0x00000002
#define KPROF_TRACE_CAP_THREAD              0x00000004
#define KPROF_TRACE_CAP_IMAGE               0x00000008
#define KPROF_TRACE_CAP_SCHEDULER           0x00000010
#define KPROF_TRACE_CAP_PER_CPU_RING        0x00000020
#define KPROF_TRACE_CAP_LOSS_RECORDS        0x00000040
#define KPROF_TRACE_CAP_KERNEL_CHAIN        0x00000080

/* Bounded interrupt-time call chains appended after the fixed record. */
#define KPROF_TRACE_MAX_FRAMES              24
#define KPROF_TRACE_MAX_RECORD_SIZE         (sizeof(KPROF_TRACE_RECORD) + KPROF_TRACE_MAX_FRAMES * sizeof(ULONGLONG))

#define KPROF_TRACE_CHAIN_TRUNCATED         0x00000001
#define KPROF_TRACE_CHAIN_STOPPED           0x00000002
#define KPROF_TRACE_CHAIN_USER_BOUNDARY     0x00000004

#define KPROF_TRACE_RECORD_FLAG_KERNEL      0x00000001
#define KPROF_TRACE_RECORD_FLAG_USER        0x00000002
#define KPROF_TRACE_RECORD_FLAG_SYSTEM      0x00000004
#define KPROF_TRACE_RECORD_FLAG_CREATE      0x00000008
#define KPROF_TRACE_RECORD_FLAG_DESTROY     0x00000010
#define KPROF_TRACE_RECORD_FLAG_READY       0x00000020
#define KPROF_TRACE_RECORD_FLAG_TRUNCATED   0x00000040

#define KPROF_TRACE_SCHED_FLAG_PREEMPTED    0x00000001
#define KPROF_TRACE_SCHED_FLAG_VOLUNTARY    0x00000002
#define KPROF_TRACE_SCHED_FLAG_MIGRATED     0x00000004

#define KPROF_TRACE_IMAGE_META_SYSTEM       0x00000001
#define KPROF_TRACE_IMAGE_META_RSDS         0x00000002
#define KPROF_TRACE_IMAGE_META_TRUNCATED    0x00000004
#define KPROF_TRACE_IMAGE_BUILD_ID_BYTES    20
#define KPROF_TRACE_MAX_IMAGE_PATH_BYTES    (128 * 1024)

#define KPROF_TRACE_LOSS_RING_FULL          1
#define KPROF_TRACE_LOSS_OVERSIZE           2

typedef enum _KPROF_TRACE_SCOPE
{
    KprofTraceScopeSystem = 0,
    KprofTraceScopeProcess = 1
} KPROF_TRACE_SCOPE;

typedef enum _KPROF_TRACE_RECORD_TYPE
{
    KprofTraceRecordInvalid = 0,
    KprofTraceRecordSample = 1,
    KprofTraceRecordProcess = 2,
    KprofTraceRecordThread = 3,
    KprofTraceRecordImage = 4,
    KprofTraceRecordScheduler = 5,
    KprofTraceRecordLost = 6,
    KprofTraceRecordSession = 7,
    KprofTraceRecordSecurity = 8,
    KprofTraceRecordClockSync = 9
} KPROF_TRACE_RECORD_TYPE;

typedef struct _KPROF_TRACE_CONFIG
{
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG Scope;
    ULONG Source;
    ULONG Interval;
    ULONG RingSize;
    ULONG MaximumStackDepth;
    KAFFINITY Affinity;
    PEPROCESS TargetProcess;
} KPROF_TRACE_CONFIG, *PKPROF_TRACE_CONFIG;

typedef struct _KPROF_TRACE_CAPABILITIES
{
    ULONG Version;
    ULONG Size;
    ULONG Capabilities;
    ULONG UnsupportedFlags;
    ULONG MaximumSessions;
    ULONG MaximumProcessors;
    ULONG MinimumRingSize;
    ULONG MaximumRingSize;
    ULONG TimerSource;
    ULONG TimerInterval;
} KPROF_TRACE_CAPABILITIES, *PKPROF_TRACE_CAPABILITIES;

typedef struct _KPROF_TRACE_RECORD_HEADER
{
    USHORT Type;
    USHORT Size;
    ULONG Flags;
    ULONG Processor;
    ULONG Reserved;
    ULONGLONG Timestamp;
    ULONGLONG Sequence;
    ULONGLONG ProcessId;
    ULONGLONG ThreadId;
    ULONGLONG ProcessKey;
    ULONGLONG ThreadKey;
} KPROF_TRACE_RECORD_HEADER, *PKPROF_TRACE_RECORD_HEADER;

typedef struct _KPROF_TRACE_RECORD
{
    KPROF_TRACE_RECORD_HEADER Header;
    union
    {
        struct
        {
            ULONGLONG ProgramCounter;
            ULONG Source;
            ULONG Interval;
            ULONG FrameCount;
            ULONG ChainFlags;
        } Sample;
        struct
        {
            ULONGLONG SubjectProcessId;
            ULONGLONG SubjectThreadId;
            ULONGLONG ParentProcessId;
            ULONGLONG SubjectProcessKey;
            ULONGLONG SubjectThreadKey;
        } Lifecycle;
        struct
        {
            ULONGLONG ImageBase;
            ULONGLONG ImageSize;
            ULONGLONG ImageKey;
            ULONGLONG MetadataId;
        } Image;
        struct
        {
            ULONGLONG OldProcessId;
            ULONGLONG OldThreadId;
            ULONGLONG NewProcessId;
            ULONGLONG NewThreadId;
            ULONGLONG OldProcessKey;
            ULONGLONG OldThreadKey;
            ULONGLONG NewProcessKey;
            ULONGLONG NewThreadKey;
            ULONG Reason;
            UCHAR OldState;
            UCHAR OldPriority;
            UCHAR NewPriority;
            UCHAR SchedulerFlags;
            USHORT SourceProcessor;
            USHORT TargetProcessor;
            ULONG Reserved;
        } Scheduler;
        struct
        {
            ULONGLONG FirstSequence;
            ULONGLONG LastSequence;
            ULONGLONG Count;
            ULONGLONG FirstTimestamp;
            ULONGLONG LastTimestamp;
            ULONG Reason;
            ULONG Reserved;
        } Lost;
        struct
        {
            ULONG Event;
            ULONG Reason;
            LONG Status;
            ULONG State;
            ULONGLONG Sources;
            ULONGLONG ConfigGeneration;
            ULONGLONG LostRecords;
        } Session;
        struct
        {
            ULONGLONG RequestedFeatures;
            ULONGLONG GrantedFeatures;
            ULONGLONG DeniedFeatures;
            ULONG RedactionMode;
            ULONG PrincipalClass;
            LONG Status;
            ULONG Reserved;
        } Security;
        struct
        {
            ULONGLONG SystemTime100ns;
            ULONGLONG PerformanceCounter;
            ULONGLONG PerformanceFrequency;
            ULONGLONG InterruptTime100ns;
            ULONG ClockFlags;
            ULONG Reserved;
        } ClockSync;
    } Data;
} KPROF_TRACE_RECORD, *PKPROF_TRACE_RECORD;

typedef struct _KPROF_TRACE_IMAGE_METADATA
{
    ULONG Flags;
    ULONG PathBytes;
    ULONG BuildIdBytes;
    ULONG Checksum;
    ULONG TimeDateStamp;
    USHORT Machine;
    USHORT Reserved;
    UCHAR BuildId[KPROF_TRACE_IMAGE_BUILD_ID_BYTES];
} KPROF_TRACE_IMAGE_METADATA, *PKPROF_TRACE_IMAGE_METADATA;

typedef struct _KPROF_TRACE_CPU_STATS
{
    ULONGLONG ProducedRecords;
    ULONGLONG LostRecords;
    ULONGLONG PendingLostRecords;
    ULONGLONG BytesAvailable;
    ULONGLONG HighWatermarkBytes;
} KPROF_TRACE_CPU_STATS, *PKPROF_TRACE_CPU_STATS;

typedef struct _KPROF_TRACE_STATS
{
    ULONG Version;
    ULONG Size;
    ULONG State;
    ULONG ProcessorCount;
    ULONGLONG ProducedRecords;
    ULONGLONG LostRecords;
    ULONGLONG PendingLostRecords;
    ULONGLONG BytesAvailable;
    ULONGLONG HighWatermarkBytes;
    ULONGLONG StartTimestamp;
    ULONGLONG StopTimestamp;
    ULONGLONG Samples;
    ULONGLONG ContextSwitches;
    ULONGLONG Wakeups;
    ULONGLONG LifecycleRecords;
    ULONGLONG FilteredSamples;
    ULONG TargetExited;
} KPROF_TRACE_STATS, *PKPROF_TRACE_STATS;

typedef struct _KPROF_TRACE_SESSION KPROF_TRACE_SESSION, *PKPROF_TRACE_SESSION;

VOID
NTAPI
KprofTraceQueryCapabilities(
    _Out_ PKPROF_TRACE_CAPABILITIES Capabilities);

NTSTATUS
NTAPI
KprofTraceCreateSession(
    _In_ const KPROF_TRACE_CONFIG *Config,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Outptr_ PKPROF_TRACE_SESSION *Session);

NTSTATUS
NTAPI
KprofTraceStartSession(
    _Inout_ PKPROF_TRACE_SESSION Session);

NTSTATUS
NTAPI
KprofTraceStopSession(
    _Inout_ PKPROF_TRACE_SESSION Session);

NTSTATUS
NTAPI
KprofTraceDrain(
    _Inout_ PKPROF_TRACE_SESSION Session,
    _In_ ULONG Processor,
    _Out_writes_bytes_to_(BufferSize, *BytesWritten) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten,
    _Out_opt_ PKPROF_TRACE_CPU_STATS CpuStats);

NTSTATUS
NTAPI
KprofTracePeek(
    _In_ PKPROF_TRACE_SESSION Session,
    _In_ ULONG Processor,
    _Out_writes_bytes_(Capacity) PKPROF_TRACE_RECORD Record,
    _In_ ULONG Capacity,
    _Out_opt_ PKPROF_TRACE_CPU_STATS CpuStats);

NTSTATUS
NTAPI
KprofTraceWaitForData(
    _In_ PKPROF_TRACE_SESSION Session,
    _In_opt_ PKEVENT CancelEvent);

NTSTATUS
NTAPI
KprofTraceQueryStats(
    _In_ PKPROF_TRACE_SESSION Session,
    _Out_ PKPROF_TRACE_STATS Stats);

NTSTATUS
NTAPI
KprofTraceQueryImageMetadata(
    _In_ PKPROF_TRACE_SESSION Session,
    _In_ ULONGLONG MetadataId,
    _Out_ PKPROF_TRACE_IMAGE_METADATA Metadata,
    _Out_writes_bytes_to_opt_(PathCapacity, *PathBytes) PVOID Path,
    _In_ ULONG PathCapacity,
    _Out_ PULONG PathBytes);

NTSTATUS
NTAPI
KprofTraceDestroySession(
    _Inout_ PKPROF_TRACE_SESSION Session);

VOID
NTAPI
KprofTraceSampleEvent(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KPROFILE_SOURCE Source);

VOID
NTAPI
KprofTraceProcessEvent(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Create);

VOID
NTAPI
KprofTraceThreadEvent(
    _In_ PETHREAD Thread,
    _In_ BOOLEAN Create);

VOID
NTAPI
KprofTraceImageEvent(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ImageBase,
    _In_ SIZE_T ImageSize,
    _In_ BOOLEAN Load,
    _In_ BOOLEAN SystemImage,
    _In_opt_ PCUNICODE_STRING ImageName);

VOID
NTAPI
KprofTraceSchedulerSwitch(
    _In_opt_ PKTHREAD OldThread,
    _In_opt_ PKTHREAD NewThread,
    _In_ ULONG Reason);

VOID
NTAPI
KprofTraceSchedulerReady(
    _In_ PKTHREAD Thread,
    _In_ ULONG TargetProcessor);

NTSTATUS
NTAPI
KprofTraceEmitSessionRecord(
    _Inout_ PKPROF_TRACE_SESSION Session,
    _In_ ULONG Event,
    _In_ ULONG Reason,
    _In_ NTSTATUS Status,
    _In_ ULONGLONG Sources,
    _In_ ULONGLONG ConfigGeneration);

NTSTATUS
NTAPI
KprofTraceEmitSecurityRecord(
    _Inout_ PKPROF_TRACE_SESSION Session,
    _In_ ULONGLONG RequestedFeatures,
    _In_ ULONGLONG GrantedFeatures,
    _In_ ULONG RedactionMode,
    _In_ ULONG PrincipalClass,
    _In_ NTSTATUS Status);

NTSTATUS
NTAPI
KprofTraceEmitClockSyncRecord(
    _Inout_ PKPROF_TRACE_SESSION Session);

BOOLEAN
NTAPI
RosprofInitialize(VOID);
