/*
 * PROJECT:     ReactOS system profiling
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Versioned kernel/user profiling transport ABI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _REACTOS_ROSPROF_H_
#define _REACTOS_ROSPROF_H_

#include <stddef.h>

#ifndef CTL_CODE
#include <winioctl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This ABI is little-endian and uses fixed-width Windows integer types only.
 * Every variable record is eight-byte sized and aligned. Unknown record types
 * and newer per-record versions are skipped using ROSPROF_RECORD_HEADER.Size.
 * Offsets are byte offsets from the beginning of their containing structure;
 * zero denotes an absent optional payload. No wire structure contains a
 * pointer, handle, SIZE_T, BOOLEAN, or compiler-sized enum.
 */

#define ROSPROF_ABI_VERSION_MAJOR                 1
#define ROSPROF_ABI_VERSION_MINOR                 0
#define ROSPROF_BYTE_ORDER_LITTLE_ENDIAN          1
#define ROSPROF_RECORD_ALIGNMENT                  8

#define ROSPROF_NT_DEVICE_NAME                    L"\\Device\\RosProf"
#define ROSPROF_DOS_DEVICE_NAME                   L"\\DosDevices\\RosProf"
#define ROSPROF_WIN32_DEVICE_NAME                 L"\\\\.\\RosProf"

/* Customer-assigned device type and function range. */
#define FILE_DEVICE_ROSPROF                       0x00008337
#define ROSPROF_IOCTL_BASE                        0x800
#define ROSPROF_CTL_CODE(Function, Access) \
    CTL_CODE(FILE_DEVICE_ROSPROF, \
             ROSPROF_IOCTL_BASE + (Function), \
             METHOD_BUFFERED, \
             (Access))

/* OUT: ROSPROF_CAPABILITIES_V1. */
#define IOCTL_ROSPROF_QUERY_CAPABILITIES \
    ROSPROF_CTL_CODE(0x00, FILE_READ_ACCESS)
/* IN: ROSPROF_CONFIG_V1, OUT: ROSPROF_SESSION_INFO_V1. */
#define IOCTL_ROSPROF_CONFIGURE \
    ROSPROF_CTL_CODE(0x01, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
/* IN: ROSPROF_SESSION_COMMAND_V1. */
#define IOCTL_ROSPROF_START \
    ROSPROF_CTL_CODE(0x02, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
/* IN: ROSPROF_SESSION_COMMAND_V1. */
#define IOCTL_ROSPROF_STOP \
    ROSPROF_CTL_CODE(0x03, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
/* IN: ROSPROF_SESSION_QUERY_V1, OUT: ROSPROF_SESSION_STATUS_V1. */
#define IOCTL_ROSPROF_QUERY_STATUS \
    ROSPROF_CTL_CODE(0x04, FILE_READ_ACCESS)
/* IN: ROSPROF_SESSION_QUERY_V1, OUT: ROSPROF_ACCESS_INFO_V1. */
#define IOCTL_ROSPROF_QUERY_ACCESS \
    ROSPROF_CTL_CODE(0x05, FILE_READ_ACCESS)
/* IN: ROSPROF_SESSION_COMMAND_V1. Only valid after the stream is stopped. */
#define IOCTL_ROSPROF_RESET \
    ROSPROF_CTL_CODE(0x06, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
/* IN: ROSPROF_PMU_ENUM_REQUEST_V1, OUT: ROSPROF_PMU_EVENT_LIST_V1. */
#define IOCTL_ROSPROF_ENUM_PMU_EVENTS \
    ROSPROF_CTL_CODE(0x07, FILE_READ_ACCESS)

/* Generic structure flags. */
#define ROSPROF_STRUCT_FLAG_NONE                  0x00000000UL

/* Capability bits. */
#define ROSPROF_CAP_TIMER_SAMPLE                  0x0000000000000001ULL
#define ROSPROF_CAP_PMU_SAMPLE                    0x0000000000000002ULL
#define ROSPROF_CAP_USER_STACK                    0x0000000000000004ULL
#define ROSPROF_CAP_KERNEL_STACK                  0x0000000000000008ULL
#define ROSPROF_CAP_PROCESS_LIFECYCLE             0x0000000000000010ULL
#define ROSPROF_CAP_THREAD_LIFECYCLE              0x0000000000000020ULL
#define ROSPROF_CAP_IMAGE_LIFECYCLE               0x0000000000000040ULL
#define ROSPROF_CAP_CONTEXT_SWITCH                0x0000000000000080ULL
#define ROSPROF_CAP_SCHED_WAKEUP                  0x0000000000000100ULL
#define ROSPROF_CAP_PER_CPU_BUFFER                0x0000000000000200ULL
#define ROSPROF_CAP_LOSS_RECORDS                  0x0000000000000400ULL
#define ROSPROF_CAP_BATCH_READ                    0x0000000000000800ULL
#define ROSPROF_CAP_CANCELLABLE_READ              0x0000000000001000ULL
#define ROSPROF_CAP_NONBLOCKING_READ              0x0000000000002000ULL
#define ROSPROF_CAP_ADDRESS_REDACTION             0x0000000000004000ULL
#define ROSPROF_CAP_CLOCK_SYNC                     0x0000000000008000ULL
#define ROSPROF_CAP_SYSTEM_WIDE                   0x0000000000010000ULL
#define ROSPROF_CAP_PROCESS_SCOPED                0x0000000000020000ULL
#define ROSPROF_CAP_THREAD_SCOPED                 0x0000000000040000ULL

/* Source selection bits. */
#define ROSPROF_SOURCE_TIMER                      0x0000000000000001ULL
#define ROSPROF_SOURCE_PMU                        0x0000000000000002ULL
#define ROSPROF_SOURCE_PROCESS                    0x0000000000000004ULL
#define ROSPROF_SOURCE_THREAD                     0x0000000000000008ULL
#define ROSPROF_SOURCE_IMAGE                      0x0000000000000010ULL
#define ROSPROF_SOURCE_CONTEXT_SWITCH             0x0000000000000020ULL
#define ROSPROF_SOURCE_SCHED_WAKEUP               0x0000000000000040ULL
#define ROSPROF_SOURCE_CLOCK_SYNC                 0x0000000000000080ULL

/* Architecture values. */
#define ROSPROF_ARCH_UNKNOWN                      0UL
#define ROSPROF_ARCH_X86                          1UL
#define ROSPROF_ARCH_AMD64                        2UL
#define ROSPROF_ARCH_ARM                          3UL
#define ROSPROF_ARCH_ARM64                        4UL

/* Timestamp domains. */
#define ROSPROF_CLOCK_UNSPECIFIED                 0UL
#define ROSPROF_CLOCK_INTERRUPT_TIME              1UL
#define ROSPROF_CLOCK_PERFORMANCE_COUNTER         2UL

/* Session configuration flags. */
#define ROSPROF_CONFIG_FLAG_SYSTEM_WIDE           0x0000000000000001ULL
#define ROSPROF_CONFIG_FLAG_INHERIT_CHILDREN      0x0000000000000002ULL
#define ROSPROF_CONFIG_FLAG_NONBLOCKING_READ      0x0000000000000004ULL
#define ROSPROF_CONFIG_FLAG_PER_CPU_BUFFER        0x0000000000000008ULL
#define ROSPROF_CONFIG_FLAG_DROP_NEW              0x0000000000000010ULL
#define ROSPROF_CONFIG_FLAG_INCLUDE_IDLE          0x0000000000000020ULL
#define ROSPROF_CONFIG_FLAG_EXCLUDE_PROFILER      0x0000000000000040ULL

/* Stack selection and result flags. */
#define ROSPROF_STACK_USER                        0x00000001UL
#define ROSPROF_STACK_KERNEL                      0x00000002UL
#define ROSPROF_STACK_INCLUDE_IP                  0x00000004UL
#define ROSPROF_STACK_TRUNCATE_ALLOWED            0x00000008UL
#define ROSPROF_STACK_WOW64                       0x00000010UL

/* Per-sample result flags (ROSPROF_SAMPLE_RECORD_V1.SampleFlags). */
#define ROSPROF_SAMPLE_FLAG_CHAIN_TRUNCATED       0x00000001UL
#define ROSPROF_SAMPLE_FLAG_CHAIN_STOPPED         0x00000002UL
#define ROSPROF_SAMPLE_FLAG_USER_BOUNDARY         0x00000004UL

/* Security modes requested at configuration time. */
#define ROSPROF_SECURITY_DEFAULT                  0UL
#define ROSPROF_SECURITY_STRICT                   1UL
#define ROSPROF_SECURITY_REDACT_KERNEL            2UL
#define ROSPROF_SECURITY_REDACT_ALL_ADDRESSES     3UL

/* Effective security and policy bits. */
#define ROSPROF_SECURITY_ADMIN_DEVICE_ACL         0x0000000000000001ULL
#define ROSPROF_SECURITY_DEBUG_PRIVILEGE          0x0000000000000002ULL
#define ROSPROF_SECURITY_TARGET_OWNER             0x0000000000000004ULL
#define ROSPROF_SECURITY_SYSTEM_WIDE_PRIVILEGE    0x0000000000000008ULL
#define ROSPROF_SECURITY_KERNEL_STACK_PRIVILEGE   0x0000000000000010ULL
#define ROSPROF_SECURITY_KERNEL_ADDRESS_REDACTED  0x0000000000000020ULL
#define ROSPROF_SECURITY_USER_ADDRESS_REDACTED    0x0000000000000040ULL
#define ROSPROF_SECURITY_AUDITED                  0x0000000000000080ULL
#define ROSPROF_SECURITY_SECURE_ZERO              0x0000000000000100ULL

#define ROSPROF_PRINCIPAL_UNKNOWN                 0UL
#define ROSPROF_PRINCIPAL_STANDARD_USER           1UL
#define ROSPROF_PRINCIPAL_TARGET_OWNER            2UL
#define ROSPROF_PRINCIPAL_ADMINISTRATOR           3UL
#define ROSPROF_PRINCIPAL_SYSTEM                  4UL

/* Session states. */
#define ROSPROF_SESSION_NEW                       0UL
#define ROSPROF_SESSION_CONFIGURED                1UL
#define ROSPROF_SESSION_RUNNING                   2UL
#define ROSPROF_SESSION_STOPPING                  3UL
#define ROSPROF_SESSION_STOPPED                   4UL
#define ROSPROF_SESSION_FAULTED                   5UL

/* Stop and lifecycle reasons. */
#define ROSPROF_REASON_NONE                       0UL
#define ROSPROF_REASON_REQUESTED                  1UL
#define ROSPROF_REASON_TARGET_EXIT                2UL
#define ROSPROF_REASON_BUFFER_LIMIT               3UL
#define ROSPROF_REASON_SECURITY_REVOKED           4UL
#define ROSPROF_REASON_DEVICE_REMOVED             5UL
#define ROSPROF_REASON_INTERNAL_ERROR              6UL

/* Read batch constants and flags. */
#define ROSPROF_READ_MAGIC                        0x31425052UL /* "RPB1" */
#define ROSPROF_READ_VERSION                      1
#define ROSPROF_READ_FLAG_MORE_DATA               0x00000001UL
#define ROSPROF_READ_FLAG_END_OF_STREAM           0x00000002UL
#define ROSPROF_READ_FLAG_LOSS_PENDING            0x00000004UL
#define ROSPROF_READ_FLAG_NONBLOCKING_EMPTY       0x00000008UL

/* Record header flags. */
#define ROSPROF_RECORD_FLAG_COMMITTED             0x00000001UL
#define ROSPROF_RECORD_FLAG_TRUNCATED             0x00000002UL
#define ROSPROF_RECORD_FLAG_REDACTED              0x00000004UL
#define ROSPROF_RECORD_FLAG_USER                  0x00000008UL
#define ROSPROF_RECORD_FLAG_KERNEL                0x00000010UL
#define ROSPROF_RECORD_FLAG_WOW64                 0x00000020UL
#define ROSPROF_RECORD_FLAG_SYNTHETIC             0x00000040UL

/* Record types. Values below 64 have a corresponding record-mask bit. */
#define ROSPROF_RECORD_SESSION                    1
#define ROSPROF_RECORD_SAMPLE                     2
#define ROSPROF_RECORD_LOSS                       3
#define ROSPROF_RECORD_PROCESS                    4
#define ROSPROF_RECORD_THREAD                     5
#define ROSPROF_RECORD_IMAGE                      6
#define ROSPROF_RECORD_CONTEXT_SWITCH             7
#define ROSPROF_RECORD_SCHED_WAKEUP               8
#define ROSPROF_RECORD_PMU                        9
#define ROSPROF_RECORD_SECURITY                   10
#define ROSPROF_RECORD_CLOCK_SYNC                 11
#define ROSPROF_RECORD_MASK(Type)                 (1ULL << (Type))

#define ROSPROF_SESSION_EVENT_BEGIN               1UL
#define ROSPROF_SESSION_EVENT_END                 2UL
#define ROSPROF_SESSION_EVENT_PAUSE               3UL
#define ROSPROF_SESSION_EVENT_RESUME              4UL

#define ROSPROF_LIFECYCLE_START                   1UL
#define ROSPROF_LIFECYCLE_END                     2UL
#define ROSPROF_IMAGE_LOAD                        1UL
#define ROSPROF_IMAGE_UNLOAD                      2UL

/* Loss reasons and scopes. */
#define ROSPROF_LOSS_RING_FULL                    1UL
#define ROSPROF_LOSS_ALLOCATION                   2UL
#define ROSPROF_LOSS_STACK_WALK                   3UL
#define ROSPROF_LOSS_PMU_OVERFLOW                 4UL
#define ROSPROF_LOSS_INTERRUPT_PRESSURE           5UL
#define ROSPROF_LOSS_THROTTLED                    6UL
#define ROSPROF_LOSS_SECURITY                     7UL
#define ROSPROF_LOSS_CORRUPT_RECORD               8UL
#define ROSPROF_LOSS_SCOPE_SESSION                0UL
#define ROSPROF_LOSS_SCOPE_PROCESSOR              1UL
#define ROSPROF_LOSS_SCOPE_PROCESS                2UL
#define ROSPROF_LOSS_SCOPE_THREAD                 3UL

/* Scheduler state and wake flags use stable profiler-local numeric values. */
#define ROSPROF_THREAD_STATE_UNKNOWN              0UL
#define ROSPROF_THREAD_STATE_INITIALIZED          1UL
#define ROSPROF_THREAD_STATE_READY                2UL
#define ROSPROF_THREAD_STATE_RUNNING              3UL
#define ROSPROF_THREAD_STATE_STANDBY              4UL
#define ROSPROF_THREAD_STATE_TERMINATED           5UL
#define ROSPROF_THREAD_STATE_WAITING              6UL
#define ROSPROF_THREAD_STATE_TRANSITION           7UL
#define ROSPROF_THREAD_STATE_DEFERRED_READY       8UL
#define ROSPROF_SCHED_FLAG_PREEMPTED              0x00000001UL
#define ROSPROF_SCHED_FLAG_VOLUNTARY              0x00000002UL
#define ROSPROF_SCHED_FLAG_MIGRATED               0x00000004UL
#define ROSPROF_WAKE_FLAG_CROSS_CPU               0x00000001UL
#define ROSPROF_WAKE_FLAG_OBJECT_REDACTED         0x00000002UL

/* Image identity flags. The upper 16 bits contain IMAGE_FILE_MACHINE. */
#define ROSPROF_IMAGE_FLAG_SYSTEM                 0x00000001UL
#define ROSPROF_IMAGE_FLAG_BUILD_ID_RSDS          0x00000002UL
#define ROSPROF_IMAGE_MACHINE_SHIFT               16
#define ROSPROF_IMAGE_MACHINE_MASK                0xFFFF0000UL
#define ROSPROF_IMAGE_MACHINE(Flags) \
    (((Flags) & ROSPROF_IMAGE_MACHINE_MASK) >> ROSPROF_IMAGE_MACHINE_SHIFT)

/* PMU event kinds, standard events, and flags. */
#define ROSPROF_PMU_KIND_HARDWARE                 1UL
#define ROSPROF_PMU_KIND_RAW                      2UL
#define ROSPROF_PMU_EVENT_CPU_CYCLES              1UL
#define ROSPROF_PMU_EVENT_INSTRUCTIONS            2UL
#define ROSPROF_PMU_EVENT_CACHE_REFERENCES        3UL
#define ROSPROF_PMU_EVENT_CACHE_MISSES            4UL
#define ROSPROF_PMU_EVENT_BRANCHES                5UL
#define ROSPROF_PMU_EVENT_BRANCH_MISSES           6UL
#define ROSPROF_PMU_FLAG_USER                     0x00000001UL
#define ROSPROF_PMU_FLAG_KERNEL                   0x00000002UL
#define ROSPROF_PMU_FLAG_EDGE                     0x00000004UL
#define ROSPROF_PMU_FLAG_INVERT                   0x00000008UL
#define ROSPROF_PMU_FLAG_PRECISE                  0x00000010UL

/* Variable UTF-8 payloads are byte strings and are not NUL-terminated. */

#include <pshpack1.h>

typedef struct _ROSPROF_STRUCT_HEADER
{
    ULONG Size;
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG Flags;
    ULONG Reserved;
} ROSPROF_STRUCT_HEADER, *PROSPROF_STRUCT_HEADER;

typedef struct _ROSPROF_CAPABILITIES_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG Capabilities;
    ULONGLONG SupportedSources;
    ULONGLONG SupportedRecordTypes;
    ULONGLONG SecurityCapabilities;
    ULONGLONG TimestampFrequency;
    ULONG Architecture;
    ULONG PointerWidth;
    ULONG ProcessorCount;
    ULONG ClockType;
    ULONG MinimumPeriod100ns;
    ULONG MaximumPeriod100ns;
    ULONG MinimumRingBytes;
    ULONG MaximumRingBytes;
    ULONG MaximumRecordBytes;
    ULONG MaximumStackDepth;
    ULONG MaximumPmuEvents;
    ULONG RecordAlignment;
    ULONG ReadHeaderSize;
    ULONG Reserved[5];
} ROSPROF_CAPABILITIES_V1, *PROSPROF_CAPABILITIES_V1;

typedef struct _ROSPROF_PMU_EVENT_CONFIG_V1
{
    ULONG EventId;
    ULONG EventKind;
    ULONGLONG EventCode;
    ULONGLONG SamplePeriod;
    ULONG Flags;
    ULONG Reserved;
} ROSPROF_PMU_EVENT_CONFIG_V1, *PROSPROF_PMU_EVENT_CONFIG_V1;

typedef struct _ROSPROF_CONFIG_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG Sources;
    ULONGLONG RecordTypes;
    ULONGLONG ConfigFlags;
    ULONGLONG SamplePeriod100ns;
    ULONG TargetProcessId;
    ULONG TargetThreadId;
    ULONG RingSizeBytes;
    ULONG ReadWatermarkBytes;
    USHORT MaximumStackDepth;
    USHORT Reserved16;
    ULONG StackFlags;
    ULONG PmuEventCount;
    ULONG PmuEventsOffset;
    ULONG CpuMaskBytes;
    ULONG CpuMaskOffset;
    ULONG SecurityMode;
    ULONG Reserved[7];
} ROSPROF_CONFIG_V1, *PROSPROF_CONFIG_V1;

typedef struct _ROSPROF_SESSION_INFO_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG SessionId;
    ULONGLONG ConfigGeneration;
    ULONGLONG AcceptedSources;
    ULONGLONG AcceptedRecordTypes;
    ULONGLONG ActualPeriod100ns;
    ULONGLONG TimestampFrequency;
    ULONGLONG EffectiveSecurity;
    ULONG RingCapacityBytes;
    ULONG ReadWatermarkBytes;
    USHORT MaximumStackDepth;
    USHORT Reserved16;
    ULONG StackFlags;
    ULONG State;
    ULONG Reserved32;
    ULONGLONG Reserved[4];
} ROSPROF_SESSION_INFO_V1, *PROSPROF_SESSION_INFO_V1;

typedef struct _ROSPROF_SESSION_QUERY_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG SessionId;
    ULONGLONG ConfigGeneration;
} ROSPROF_SESSION_QUERY_V1, *PROSPROF_SESSION_QUERY_V1;

typedef struct _ROSPROF_SESSION_COMMAND_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG SessionId;
    ULONGLONG ConfigGeneration;
    ULONG CommandFlags;
    ULONG Reason;
} ROSPROF_SESSION_COMMAND_V1, *PROSPROF_SESSION_COMMAND_V1;

typedef struct _ROSPROF_RING_STATUS_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG SessionId;
    ULONGLONG ProducerSequence;
    ULONGLONG ConsumerSequence;
    ULONGLONG ProducedRecords;
    ULONGLONG ConsumedRecords;
    ULONGLONG LostRecords;
    ULONGLONG LostBytes;
    ULONGLONG PendingLossRecords;
    ULONG CapacityBytes;
    ULONG UsedBytes;
    ULONG HighWatermarkBytes;
    ULONG NextRecordBytes;
    ULONG Flags;
    ULONG Reserved;
} ROSPROF_RING_STATUS_V1, *PROSPROF_RING_STATUS_V1;

typedef struct _ROSPROF_SESSION_STATUS_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG SessionId;
    ULONGLONG ConfigGeneration;
    ULONG State;
    LONG FinalStatus;
    ULONG StopReason;
    ULONG ActiveProcessorCount;
    ULONGLONG StartTimestamp;
    ULONGLONG StopTimestamp;
    ULONGLONG Samples;
    ULONGLONG ContextSwitches;
    ULONGLONG Wakeups;
    ULONGLONG LifecycleRecords;
    ROSPROF_RING_STATUS_V1 Ring;
    /* Samples deliberately not recorded by configuration (owner exclusion,
     * affinity, source mismatch), as opposed to unexpected loss. */
    ULONGLONG FilteredSamples;
    ULONGLONG Reserved[3];
} ROSPROF_SESSION_STATUS_V1, *PROSPROF_SESSION_STATUS_V1;

typedef struct _ROSPROF_ACCESS_INFO_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONGLONG SessionId;
    ULONGLONG PolicyFlags;
    ULONGLONG RequestedSources;
    ULONGLONG GrantedSources;
    ULONGLONG RequestedRecordTypes;
    ULONGLONG GrantedRecordTypes;
    ULONG RequestedStackFlags;
    ULONG GrantedStackFlags;
    ULONG RedactionMode;
    ULONG PrincipalClass;
    LONG AccessStatus;
    ULONG Reserved32;
    ULONGLONG Reserved[5];
} ROSPROF_ACCESS_INFO_V1, *PROSPROF_ACCESS_INFO_V1;

typedef struct _ROSPROF_PMU_ENUM_REQUEST_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONG FirstEventIndex;
    ULONG MaximumEventCount;
} ROSPROF_PMU_ENUM_REQUEST_V1, *PROSPROF_PMU_ENUM_REQUEST_V1;

typedef struct _ROSPROF_PMU_EVENT_DESCRIPTOR_V1
{
    ULONG Size;
    ULONG EventId;
    ULONG EventKind;
    ULONG Flags;
    ULONGLONG EventCode;
    ULONGLONG MinimumPeriod;
    ULONGLONG MaximumPeriod;
    ULONG NameOffset;
    ULONG NameBytes;
    ULONG DescriptionOffset;
    ULONG DescriptionBytes;
    ULONG Reserved[2];
} ROSPROF_PMU_EVENT_DESCRIPTOR_V1, *PROSPROF_PMU_EVENT_DESCRIPTOR_V1;

typedef struct _ROSPROF_PMU_EVENT_LIST_V1
{
    ROSPROF_STRUCT_HEADER Header;
    ULONG TotalEventCount;
    ULONG ReturnedEventCount;
    ULONG EventsOffset;
    ULONG EventsBytes;
    ULONG NextEventIndex;
    ULONG Reserved[3];
} ROSPROF_PMU_EVENT_LIST_V1, *PROSPROF_PMU_EVENT_LIST_V1;

/*
 * ReadFile returns one complete batch. The device uses direct I/O. A read of
 * fewer than sizeof(ROSPROF_READ_BATCH_V1) bytes fails with
 * STATUS_BUFFER_TOO_SMALL. Records never straddle batches. When the next
 * record does not fit, a header-only batch sets MORE_DATA and RequiredSize.
 * Blocking reads pend while a running session is empty and are cancellable.
 * Nonblocking reads return a header-only NONBLOCKING_EMPTY batch. Once a
 * stopped session is drained, one END_OF_STREAM batch is returned; subsequent
 * reads complete with STATUS_END_OF_FILE. Exactly one reader may consume a
 * session. Closing the file cancels reads, stops capture, and securely frees
 * the per-handle session and ring.
 */
typedef struct _ROSPROF_READ_BATCH_V1
{
    ULONG Magic;
    USHORT HeaderSize;
    USHORT Version;
    ULONG Flags;
    ULONG RecordAlignment;
    ULONGLONG SessionId;
    ULONGLONG ConfigGeneration;
    ULONGLONG FirstSequence;
    ULONGLONG LastSequence;
    ULONG RecordCount;
    ULONG RecordsOffset;
    ULONG RecordsBytes;
    ULONG RequiredSize;
    ULONGLONG LostSinceLastRead;
    ULONGLONG Reserved;
} ROSPROF_READ_BATCH_V1, *PROSPROF_READ_BATCH_V1;

typedef struct _ROSPROF_RECORD_HEADER
{
    ULONG Size;
    USHORT Type;
    USHORT Version;
    ULONG Flags;
    USHORT HeaderSize;
    USHORT ProcessorNumber;
    ULONGLONG Sequence;
    ULONGLONG Timestamp;
    ULONG ProcessId;
    ULONG ThreadId;
    /* Opaque lifetime keys remain stable across numeric ID reuse and do not
     * disclose kernel object addresses. */
    ULONGLONG UniqueProcessKey;
    ULONGLONG UniqueThreadKey;
} ROSPROF_RECORD_HEADER, *PROSPROF_RECORD_HEADER;

typedef struct _ROSPROF_SESSION_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG Event;
    ULONG Reason;
    LONG Status;
    ULONG State;
    ULONGLONG Sources;
    ULONGLONG ConfigGeneration;
    ULONGLONG LostRecords;
} ROSPROF_SESSION_RECORD_V1, *PROSPROF_SESSION_RECORD_V1;

/* Frames are ULONGLONG addresses, leaf first: user frames then kernel frames. */
typedef struct _ROSPROF_SAMPLE_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONGLONG Weight;
    ULONG Source;
    USHORT UserDepth;
    USHORT KernelDepth;
    ULONG FramesOffset;
    ULONG FramesBytes;
    ULONGLONG InstructionPointer;
    ULONGLONG StackPointer;
    ULONG PmuEventId;
    ULONG SampleFlags;
} ROSPROF_SAMPLE_RECORD_V1, *PROSPROF_SAMPLE_RECORD_V1;

typedef struct _ROSPROF_LOSS_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG Reason;
    ULONG Scope;
    ULONGLONG LostRecords;
    ULONGLONG LostBytes;
    ULONGLONG FirstLostSequence;
    ULONGLONG LastLostSequence;
    ULONGLONG FirstLostTimestamp;
    ULONGLONG LastLostTimestamp;
} ROSPROF_LOSS_RECORD_V1, *PROSPROF_LOSS_RECORD_V1;

typedef struct _ROSPROF_PROCESS_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG Event;
    ULONG ParentProcessId;
    LONG ExitStatus;
    ULONG SessionId;
    ULONGLONG UniqueProcessKey;
    ULONGLONG ImageBase;
    ULONG ImageNameOffset;
    ULONG ImageNameBytes;
    ULONG CommandLineOffset;
    ULONG CommandLineBytes;
} ROSPROF_PROCESS_RECORD_V1, *PROSPROF_PROCESS_RECORD_V1;

typedef struct _ROSPROF_THREAD_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG Event;
    LONG ExitStatus;
    ULONGLONG UniqueThreadKey;
    ULONGLONG UniqueProcessKey;
    ULONGLONG StartAddress;
    LONG BasePriority;
    ULONG Reserved;
} ROSPROF_THREAD_RECORD_V1, *PROSPROF_THREAD_RECORD_V1;

typedef struct _ROSPROF_IMAGE_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG Event;
    ULONG ImageFlags;
    ULONGLONG ImageBase;
    ULONGLONG ImageSize;
    ULONGLONG ImageKey;
    ULONG PathOffset;
    ULONG PathBytes;
    ULONG BuildIdOffset;
    ULONG BuildIdBytes;
    ULONG Checksum;
    ULONG TimeDateStamp;
} ROSPROF_IMAGE_RECORD_V1, *PROSPROF_IMAGE_RECORD_V1;

typedef struct _ROSPROF_CONTEXT_SWITCH_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG OldProcessId;
    ULONG OldThreadId;
    ULONG NewProcessId;
    ULONG NewThreadId;
    ULONGLONG OldProcessKey;
    ULONGLONG OldThreadKey;
    ULONGLONG NewProcessKey;
    ULONGLONG NewThreadKey;
    ULONG OldThreadState;
    ULONG OldWaitReason;
    USHORT OldPriority;
    USHORT NewPriority;
    USHORT NewProcessorNumber;
    USHORT SchedulerFlags;
    ULONGLONG WaitDuration;
} ROSPROF_CONTEXT_SWITCH_RECORD_V1, *PROSPROF_CONTEXT_SWITCH_RECORD_V1;

typedef struct _ROSPROF_SCHED_WAKEUP_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG TargetProcessId;
    ULONG TargetThreadId;
    ULONG WakerProcessId;
    ULONG WakerThreadId;
    ULONGLONG TargetProcessKey;
    ULONGLONG TargetThreadKey;
    ULONGLONG WakerProcessKey;
    ULONGLONG WakerThreadKey;
    USHORT TargetProcessorNumber;
    USHORT SourceProcessorNumber;
    ULONG WakeFlags;
    ULONGLONG WaitObject;
    ULONGLONG WakeLatency;
} ROSPROF_SCHED_WAKEUP_RECORD_V1, *PROSPROF_SCHED_WAKEUP_RECORD_V1;

typedef struct _ROSPROF_PMU_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG EventId;
    ULONG CounterIndex;
    ULONGLONG CounterValue;
    ULONGLONG SamplePeriod;
    ULONGLONG InstructionPointer;
    ULONG PmuFlags;
    ULONG Reserved;
} ROSPROF_PMU_RECORD_V1, *PROSPROF_PMU_RECORD_V1;

typedef struct _ROSPROF_SECURITY_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONG Event;
    ULONG PrincipalClass;
    ULONGLONG RequestedFeatures;
    ULONGLONG GrantedFeatures;
    ULONGLONG DeniedFeatures;
    ULONG RedactionMode;
    LONG Status;
} ROSPROF_SECURITY_RECORD_V1, *PROSPROF_SECURITY_RECORD_V1;

typedef struct _ROSPROF_CLOCK_SYNC_RECORD_V1
{
    ROSPROF_RECORD_HEADER Header;
    ULONGLONG SystemTime100ns;
    ULONGLONG PerformanceCounter;
    ULONGLONG PerformanceFrequency;
    ULONGLONG InterruptTime100ns;
    ULONG ClockFlags;
    ULONG Reserved;
} ROSPROF_CLOCK_SYNC_RECORD_V1, *PROSPROF_CLOCK_SYNC_RECORD_V1;

#include <poppack.h>

#define ROSPROF_JOIN2_(Left, Right) Left##Right
#define ROSPROF_JOIN_(Left, Right) ROSPROF_JOIN2_(Left, Right)
#if defined(__cplusplus) && (__cplusplus >= 201103L)
#define ROSPROF_STATIC_ASSERT(Expression) static_assert((Expression), #Expression)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define ROSPROF_STATIC_ASSERT(Expression) _Static_assert((Expression), #Expression)
#else
#define ROSPROF_STATIC_ASSERT(Expression) \
    typedef char ROSPROF_JOIN_(RosProfStaticAssert_, __LINE__)[(Expression) ? 1 : -1]
#endif

ROSPROF_STATIC_ASSERT(sizeof(UCHAR) == 1);
ROSPROF_STATIC_ASSERT(sizeof(USHORT) == 2);
ROSPROF_STATIC_ASSERT(sizeof(ULONG) == 4);
ROSPROF_STATIC_ASSERT(sizeof(ULONGLONG) == 8);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_STRUCT_HEADER) == 16);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_CAPABILITIES_V1) == 128);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_PMU_EVENT_CONFIG_V1) == 32);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_CONFIG_V1) == 120);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_SESSION_INFO_V1) == 128);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_SESSION_QUERY_V1) == 32);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_SESSION_COMMAND_V1) == 40);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_RING_STATUS_V1) == 104);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_ACCESS_INFO_V1) == 128);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_PMU_EVENT_DESCRIPTOR_V1) == 64);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_PMU_EVENT_LIST_V1) == 48);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_READ_BATCH_V1) == 80);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_RECORD_HEADER) == 56);
ROSPROF_STATIC_ASSERT(offsetof(ROSPROF_RECORD_HEADER, Sequence) == 16);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_SESSION_RECORD_V1) == 96);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_SAMPLE_RECORD_V1) == 104);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_LOSS_RECORD_V1) == 112);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_PROCESS_RECORD_V1) == 104);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_THREAD_RECORD_V1) == 96);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_IMAGE_RECORD_V1) == 112);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_CONTEXT_SWITCH_RECORD_V1) == 128);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_SCHED_WAKEUP_RECORD_V1) == 128);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_PMU_RECORD_V1) == 96);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_SECURITY_RECORD_V1) == 96);
ROSPROF_STATIC_ASSERT(sizeof(ROSPROF_CLOCK_SYNC_RECORD_V1) == 96);

static __inline ULONG
RosProfAlignUpU32(ULONG Value)
{
    if (Value > (ULONG)-1 - (ROSPROF_RECORD_ALIGNMENT - 1))
        return 0;
    return (Value + (ROSPROF_RECORD_ALIGNMENT - 1)) &
           ~(ROSPROF_RECORD_ALIGNMENT - 1);
}

static __inline int
RosProfIsAlignedU32(ULONG Value)
{
    return (Value & (ROSPROF_RECORD_ALIGNMENT - 1)) == 0;
}

static __inline int
RosProfRangeValidU32(ULONG TotalSize, ULONG Offset, ULONG ByteCount)
{
    if (ByteCount == 0)
        return Offset == 0 || Offset <= TotalSize;
    if (Offset == 0 || Offset > TotalSize)
        return 0;
    return ByteCount <= TotalSize - Offset;
}

static __inline int
RosProfRecordHeaderValid(const ROSPROF_RECORD_HEADER *Header,
                         ULONG AvailableBytes)
{
    return Header != NULL &&
           AvailableBytes >= sizeof(*Header) &&
           Header->HeaderSize >= sizeof(*Header) &&
           Header->HeaderSize <= Header->Size &&
           Header->Size <= AvailableBytes &&
           RosProfIsAlignedU32(Header->Size);
}

#ifdef __cplusplus
}
#endif

#endif /* _REACTOS_ROSPROF_H_ */
