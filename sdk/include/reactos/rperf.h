/*
 * PROJECT:     ReactOS system profiling
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Durable .rperf v2 recording format and streaming codec
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _REACTOS_RPERF_H_
#define _REACTOS_RPERF_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * .rperf v2 is a little-endian, pointer-free disk format. All reserved bytes
 * and alignment padding are zero. File, chunk, and record structures have
 * fixed layouts; variable data is referenced by byte offsets relative to the
 * start of its containing record. Unknown chunks and records are skipped by
 * their declared sizes. Writers preserve caller order and never generate IDs,
 * timestamps, or metadata, which makes identical input byte-for-byte stable.
 * In v1 records, a present variable payload begins at HeaderSize; an absent
 * payload has offset and length/count zero. Size includes zero alignment pad.
 *
 * Chunks are framed as:
 *
 *   RPERF_CHUNK_HEADER_V1
 *   StoredSize bytes of payload
 *   zero padding to RPERF_ALIGNMENT
 *   RPERF_CHUNK_TRAILER_V1
 *
 * HeaderCrc32 covers HeaderSize bytes with HeaderCrc32 zeroed. PayloadCrc32
 * covers the stored payload only, before decompression. The trailer repeats
 * the sequence, header checksum, and stored size to reject torn writes. The
 * core codec writes compression NONE and returns RPERF_E_UNSUPPORTED for other
 * algorithms, while still allowing such chunks to be skipped and checksummed.
 */

#define RPERF_VERSION_MAJOR                       2U
#define RPERF_VERSION_MINOR                       0U
#define RPERF_ENDIAN_MARKER                       UINT32_C(0x01020304)
#define RPERF_ALIGNMENT                           8U
#define RPERF_FILE_HEADER_SIZE                    128U
#define RPERF_CHUNK_HEADER_SIZE                   80U
#define RPERF_CHUNK_TRAILER_SIZE                  32U
#define RPERF_RECORD_HEADER_SIZE                  32U

#define RPERF_FILE_MAGIC_BYTES                    "RPERF2\0\0"
#define RPERF_CHUNK_MAGIC                         UINT32_C(0x4b4e4843) /* CHNK */
#define RPERF_TRAILER_MAGIC                       UINT32_C(0x444e4543) /* CEND */

#define RPERF_DEFAULT_MAX_CHUNK_BYTES             (64U * 1024U * 1024U)
#define RPERF_HARD_MAX_CHUNK_BYTES                (1024U * 1024U * 1024U)
#define RPERF_DEFAULT_MAX_RECORD_BYTES            (16U * 1024U * 1024U)
#define RPERF_HARD_MAX_RECORD_BYTES               (1024U * 1024U * 1024U)
#define RPERF_MAX_HEADER_BYTES                    4096U
#define RPERF_MAX_STRING_BYTES                    (1024U * 1024U)
#define RPERF_MAX_CALLCHAIN_DEPTH                 4096U

/* Stable codec status values. */
typedef int32_t RPERF_STATUS;

#define RPERF_OK                                  ((RPERF_STATUS)0)
#define RPERF_S_END_OF_FILE                       ((RPERF_STATUS)1)
#define RPERF_S_RECOVERED                         ((RPERF_STATUS)2)
#define RPERF_S_MORE_DATA                         ((RPERF_STATUS)3)
#define RPERF_E_INVALID_ARGUMENT                  ((RPERF_STATUS)-1)
#define RPERF_E_IO                                ((RPERF_STATUS)-2)
#define RPERF_E_TRUNCATED                         ((RPERF_STATUS)-3)
#define RPERF_E_BAD_MAGIC                         ((RPERF_STATUS)-4)
#define RPERF_E_BAD_VERSION                       ((RPERF_STATUS)-5)
#define RPERF_E_BAD_ENDIAN                        ((RPERF_STATUS)-6)
#define RPERF_E_BAD_SIZE                          ((RPERF_STATUS)-7)
#define RPERF_E_OVERFLOW                          ((RPERF_STATUS)-8)
#define RPERF_E_CHECKSUM                          ((RPERF_STATUS)-9)
#define RPERF_E_UNSUPPORTED                       ((RPERF_STATUS)-10)
#define RPERF_E_STATE                             ((RPERF_STATUS)-11)
#define RPERF_E_LIMIT                             ((RPERF_STATUS)-12)
#define RPERF_E_CORRUPT                           ((RPERF_STATUS)-13)
#define RPERF_E_NOT_FOUND                         ((RPERF_STATUS)-14)

/* Checksums and compression algorithms. */
#define RPERF_CHECKSUM_NONE                       0U
#define RPERF_CHECKSUM_CRC32_IEEE                 1U
#define RPERF_COMPRESSION_NONE                    0U
#define RPERF_COMPRESSION_ZSTD                    1U
#define RPERF_COMPRESSION_LZ4                     2U

/* File flags. */
#define RPERF_FILE_FLAG_STREAMING                 UINT32_C(0x00000001)
#define RPERF_FILE_FLAG_FINALIZED                 UINT32_C(0x00000002)
#define RPERF_FILE_FLAG_HAS_INDEX                 UINT32_C(0x00000004)
#define RPERF_FILE_FLAG_RECOVERED                 UINT32_C(0x00000008)
#define RPERF_FILE_FLAG_REDACTED                  UINT32_C(0x00000010)

/* Writer and reader options. */
#define RPERF_WRITER_FLAG_STREAMING               UINT32_C(0x00000001)
#define RPERF_READER_FLAG_ALLOW_NEWER_MINOR       UINT32_C(0x00000001)
#define RPERF_READER_FLAG_REQUIRE_FINALIZED       UINT32_C(0x00000002)
#define RPERF_READER_FLAG_REQUIRE_ZERO_PADDING    UINT32_C(0x00000004)

/* Chunk flags. */
#define RPERF_CHUNK_FLAG_CRITICAL                 UINT32_C(0x00000001)
#define RPERF_CHUNK_FLAG_PARTIAL                  UINT32_C(0x00000002)
#define RPERF_CHUNK_FLAG_REDACTED                 UINT32_C(0x00000004)

/* Chunk types. */
#define RPERF_CHUNK_SESSION                       1U
#define RPERF_CHUNK_SOURCE_TABLE                  2U
#define RPERF_CHUNK_PROCESS                       3U
#define RPERF_CHUNK_THREAD                        4U
#define RPERF_CHUNK_IMAGE                         5U
#define RPERF_CHUNK_SAMPLE                        6U
#define RPERF_CHUNK_SCHEDULER                     7U
#define RPERF_CHUNK_COUNTER                       8U
#define RPERF_CHUNK_LOSS                          9U
#define RPERF_CHUNK_CLOCK_SYNC                    10U
#define RPERF_CHUNK_STRING_TABLE                  11U
#define RPERF_CHUNK_SYMBOL_TABLE                  12U
#define RPERF_CHUNK_MODULE_TABLE                  13U
#define RPERF_CHUNK_INDEX                         14U
#define RPERF_CHUNK_FOOTER                        15U

/* Record flags and types. */
#define RPERF_RECORD_FLAG_TRUNCATED               UINT32_C(0x00000001)
#define RPERF_RECORD_FLAG_REDACTED                UINT32_C(0x00000002)
#define RPERF_RECORD_FLAG_SYNTHETIC               UINT32_C(0x00000004)
#define RPERF_RECORD_FLAG_KERNEL                  UINT32_C(0x00000008)
#define RPERF_RECORD_FLAG_USER                    UINT32_C(0x00000010)

#define RPERF_SAMPLE_FLAG_STACK_TRUNCATED         UINT32_C(0x00000001)
#define RPERF_SAMPLE_FLAG_UNWIND_FAILED           UINT32_C(0x00000002)
#define RPERF_SAMPLE_FLAG_INCOMPLETE              UINT32_C(0x00000004)
#define RPERF_SAMPLE_FLAG_WOW64                   UINT32_C(0x00000008)
#define RPERF_SAMPLE_FLAG_PRECISE_IP              UINT32_C(0x00000010)

#define RPERF_RECORD_SESSION                      1U
#define RPERF_RECORD_SOURCE                       2U
#define RPERF_RECORD_PROCESS                      3U
#define RPERF_RECORD_THREAD                       4U
#define RPERF_RECORD_IMAGE                        5U
#define RPERF_RECORD_SAMPLE                       6U
#define RPERF_RECORD_SCHEDULER                    7U
#define RPERF_RECORD_COUNTER                      8U
#define RPERF_RECORD_LOSS                         9U
#define RPERF_RECORD_CLOCK_SYNC                   10U
#define RPERF_RECORD_STRING                       11U
#define RPERF_RECORD_SYMBOL                       12U
#define RPERF_RECORD_MODULE                       13U
#define RPERF_RECORD_INDEX                        14U
#define RPERF_RECORD_FOOTER                       15U

#define RPERF_LIFECYCLE_START                     1U
#define RPERF_LIFECYCLE_END                       2U
#define RPERF_IMAGE_LOAD                          1U
#define RPERF_IMAGE_UNLOAD                        2U
#define RPERF_SCHED_CONTEXT_SWITCH                1U
#define RPERF_SCHED_READY                         2U
#define RPERF_SCHED_WAKEUP                        3U

#define RPERF_SOURCE_TIMER                        1U
#define RPERF_SOURCE_PMU                          2U
#define RPERF_SOURCE_COUNTER                      3U
#define RPERF_SOURCE_SCHEDULER                    4U
#define RPERF_SOURCE_LIFECYCLE                    5U

#define RPERF_STRING_UTF8                         1U
#define RPERF_CALLCHAIN_ADDRESS64                 1U

/* Session architecture and timestamp domain values. */
#define RPERF_ARCH_UNKNOWN                        0U
#define RPERF_ARCH_X86                            1U
#define RPERF_ARCH_AMD64                          2U
#define RPERF_ARCH_ARM                            3U
#define RPERF_ARCH_ARM64                          4U

/* RPERF_MODULE_RECORD_V1.ModuleFlags. */
#define RPERF_MODULE_FLAG_ARCH_MASK               0x000000ffU
#define RPERF_MODULE_FLAG_EMBEDDED_ROSSYM          0x00000100U

/* RPERF_SYMBOL_RECORD_V1.SymbolFlags. */
#define RPERF_SYMBOL_FLAG_RESOLUTION_MASK          0x000000ffU
#define RPERF_SYMBOL_FLAG_SOURCE_SHIFT             8U
#define RPERF_SYMBOL_FLAG_SOURCE_MASK              0x0000ff00U
#define RPERF_SYMBOL_FLAG_STATUS_SHIFT             16U
#define RPERF_SYMBOL_FLAG_STATUS_MASK              0x00ff0000U

/* Session record v2 uses RPERF_SESSION_RECORD_V1.Reserved[0]. */
#define RPERF_SESSION_RECORD_VERSION_METADATA     2U
#define RPERF_SESSION_META_BACKEND_MASK            0x00000000ffffffffULL
#define RPERF_SESSION_META_METRIC_SHIFT            32U
#define RPERF_SESSION_META_METRIC_MASK             0xffffffff00000000ULL
#define RPERF_CLOCK_UNSPECIFIED                   0U
#define RPERF_CLOCK_INTERRUPT_TIME                1U
#define RPERF_CLOCK_PERFORMANCE_COUNTER           2U

/* Capture stop reasons. */
#define RPERF_STOP_NONE                           0U
#define RPERF_STOP_REQUESTED                      1U
#define RPERF_STOP_TARGET_EXIT                    2U
#define RPERF_STOP_LIMIT                          3U
#define RPERF_STOP_SECURITY                       4U
#define RPERF_STOP_DEVICE_REMOVED                 5U
#define RPERF_STOP_INTERNAL_ERROR                 6U

/* Loss reasons retain the rosprof.h numeric meanings where they overlap. */
#define RPERF_LOSS_RING_FULL                      1U
#define RPERF_LOSS_ALLOCATION                     2U
#define RPERF_LOSS_STACK_WALK                     3U
#define RPERF_LOSS_PMU_OVERFLOW                   4U
#define RPERF_LOSS_INTERRUPT_PRESSURE             5U
#define RPERF_LOSS_THROTTLED                      6U
#define RPERF_LOSS_SECURITY                       7U
#define RPERF_LOSS_CORRUPT_RECORD                 8U
#define RPERF_LOSS_GAP_IN_INPUT                   9U
#define RPERF_LOSS_SCOPE_SESSION                  0U
#define RPERF_LOSS_SCOPE_PROCESSOR                1U
#define RPERF_LOSS_SCOPE_PROCESS                  2U
#define RPERF_LOSS_SCOPE_THREAD                   3U

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#pragma pack(push, 1)
#endif

typedef struct _RPERF_FILE_HEADER_V2
{
    uint8_t Magic[8];
    uint16_t VersionMajor;
    uint16_t VersionMinor;
    uint32_t EndianMarker;
    uint32_t HeaderSize;
    uint32_t Flags;
    uint32_t ChecksumAlgorithm;
    uint32_t CompressionAlgorithm;
    uint8_t SessionId[16];
    uint64_t CreatedSystemTime100ns;
    uint64_t TimestampFrequency;
    uint64_t FirstChunkOffset;
    uint64_t FileSize;
    uint64_t ChunkCount;
    uint64_t IndexChunkOffset;
    uint64_t FooterChunkOffset;
    uint32_t HeaderCrc32;
    uint32_t Reserved[5];
} RPERF_FILE_HEADER_V2;

typedef struct _RPERF_CHUNK_HEADER_V1
{
    uint32_t Magic;
    uint16_t Type;
    uint16_t Version;
    uint32_t HeaderSize;
    uint32_t Flags;
    uint32_t CompressionAlgorithm;
    uint32_t ChecksumAlgorithm;
    uint64_t Sequence;
    uint64_t StoredSize;
    uint64_t UncompressedSize;
    uint64_t RecordCount;
    uint64_t FirstTimestamp;
    uint64_t LastTimestamp;
    uint32_t HeaderCrc32;
    uint32_t Reserved;
} RPERF_CHUNK_HEADER_V1;

typedef struct _RPERF_CHUNK_TRAILER_V1
{
    uint32_t Magic;
    uint32_t Size;
    uint64_t Sequence;
    uint32_t PayloadCrc32;
    uint32_t HeaderCrc32;
    uint64_t StoredSize;
} RPERF_CHUNK_TRAILER_V1;

typedef struct _RPERF_RECORD_HEADER_V1
{
    uint32_t Size;
    uint16_t Type;
    uint16_t Version;
    uint32_t Flags;
    uint32_t HeaderSize;
    uint64_t Sequence;
    uint64_t Timestamp;
} RPERF_RECORD_HEADER_V1;

typedef struct _RPERF_SESSION_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint8_t SessionId[16];
    uint64_t CaptureStartSystemTime100ns;
    uint64_t CaptureStartTimestamp;
    uint64_t CaptureEndTimestamp;
    uint64_t TimestampFrequency;
    uint64_t Sources;       /* Bit N means source kind N is present. */
    uint64_t RecordTypes;   /* Bit N means record type N is present. */
    uint32_t OsVersionMajor;
    uint32_t OsVersionMinor;
    uint32_t OsBuildNumber;
    uint32_t Architecture;
    uint32_t PointerWidth;
    uint32_t ProcessorCount;
    uint32_t ClockType;
    uint32_t SecurityFlags;
    int32_t CaptureStatus;
    uint32_t StopReason;
    uint32_t HostNameStringId;
    uint32_t CommandLineStringId;
    uint64_t Reserved[2];
} RPERF_SESSION_RECORD_V1;

typedef struct _RPERF_SOURCE_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t SourceId;
    uint32_t SourceKind;
    uint32_t SourceFlags;
    uint32_t NameStringId;
    uint32_t DescriptionStringId;
    uint32_t UnitStringId;
    uint64_t ScaleNumerator;
    uint64_t ScaleDenominator;
    uint64_t MinimumPeriod;
    uint64_t MaximumPeriod;
    uint64_t EventCode;
    uint64_t Reserved[2];
} RPERF_SOURCE_RECORD_V1;

typedef struct _RPERF_PROCESS_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t Event;
    uint32_t ProcessId;
    uint32_t ParentProcessId;
    uint32_t SessionId;
    uint64_t UniqueProcessKey;
    int32_t ExitStatus;
    uint32_t ProcessFlags;
    uint32_t ImagePathStringId;
    uint32_t CommandLineStringId;
    uint64_t CreateSystemTime100ns;
} RPERF_PROCESS_RECORD_V1;

typedef struct _RPERF_THREAD_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t Event;
    uint32_t ProcessId;
    uint32_t ThreadId;
    int32_t ExitStatus;
    uint64_t UniqueThreadKey;
    uint64_t UniqueProcessKey;
    uint64_t StartAddress;
    int32_t BasePriority;
    uint32_t NameStringId;
} RPERF_THREAD_RECORD_V1;

typedef struct _RPERF_IMAGE_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t Event;
    uint32_t ProcessId;
    uint64_t UniqueProcessKey;
    uint64_t ImageBase;
    uint64_t ImageSize;
    uint32_t ImageId;
    uint32_t PathStringId;
    uint32_t ModuleId;
    uint32_t ImageFlags;
    uint32_t Checksum;
    uint32_t TimeDateStamp;
    uint32_t BuildIdOffset;
    uint32_t BuildIdBytes;
} RPERF_IMAGE_RECORD_V1;

/*
 * CallchainOffset points to CallchainCount little-endian uint64_t addresses.
 * UserDepth leaf-to-root user frames come first, followed by KernelDepth
 * leaf-to-root kernel frames. Count must equal UserDepth + KernelDepth.
 */
typedef struct _RPERF_SAMPLE_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t ProcessId;
    uint32_t ThreadId;
    uint16_t ProcessorNumber;
    uint16_t UserDepth;
    uint16_t KernelDepth;
    uint16_t CallchainEncoding;
    uint32_t SourceId;
    uint32_t SampleFlags;
    uint64_t Weight;
    uint64_t InstructionPointer;
    uint64_t StackPointer;
    uint32_t CallchainOffset;
    uint32_t CallchainCount;
} RPERF_SAMPLE_RECORD_V1;

typedef struct _RPERF_SCHEDULER_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t Event;
    uint32_t SchedulerFlags;
    uint32_t OldProcessId;
    uint32_t OldThreadId;
    uint32_t NewProcessId;
    uint32_t NewThreadId;
    uint32_t OldThreadState;
    uint32_t OldWaitReason;
    uint16_t OldPriority;
    uint16_t NewPriority;
    uint16_t SourceProcessor;
    uint16_t TargetProcessor;
    uint64_t WaitDuration;
} RPERF_SCHEDULER_RECORD_V1;

typedef struct _RPERF_COUNTER_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t ProcessId;
    uint32_t ThreadId;
    uint32_t ProcessorNumber;
    uint32_t SourceId;
    uint64_t Value;
    uint64_t Period;
    uint64_t InstructionPointer;
    uint32_t CounterFlags;
    uint32_t Reserved;
} RPERF_COUNTER_RECORD_V1;

typedef struct _RPERF_LOSS_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t Reason;
    uint32_t Scope;
    uint64_t LostRecords;
    uint64_t LostBytes;
    uint64_t FirstLostSequence;
    uint64_t LastLostSequence;
    uint64_t FirstLostTimestamp;
    uint64_t LastLostTimestamp;
} RPERF_LOSS_RECORD_V1;

typedef struct _RPERF_CLOCK_SYNC_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint64_t SystemTime100ns;
    uint64_t PerformanceCounter;
    uint64_t PerformanceFrequency;
    uint64_t InterruptTime100ns;
    uint32_t ClockFlags;
    uint32_t Reserved;
} RPERF_CLOCK_SYNC_RECORD_V1;

typedef struct _RPERF_STRING_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t StringId;
    uint16_t Encoding;
    uint16_t StringFlags;
    uint32_t DataOffset;
    uint32_t DataBytes;
} RPERF_STRING_RECORD_V1;

/* UTF-8 string payloads contain DataBytes bytes without a trailing NUL. */

typedef struct _RPERF_SYMBOL_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t SymbolId;
    uint32_t ModuleId;
    uint64_t Address;
    uint64_t Size;
    uint32_t NameStringId;
    uint32_t FileStringId;
    uint32_t LineNumber;
    uint32_t SymbolFlags;
} RPERF_SYMBOL_RECORD_V1;

typedef struct _RPERF_MODULE_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t ModuleId;
    uint32_t ProcessId;
    uint64_t UniqueProcessKey;
    uint64_t ImageBase;
    uint64_t ImageSize;
    uint32_t PathStringId;
    uint32_t ModuleFlags;
    uint32_t Checksum;
    uint32_t TimeDateStamp;
    uint32_t DebugIdOffset;
    uint32_t DebugIdBytes;
    uint64_t Reserved;
} RPERF_MODULE_RECORD_V1;

typedef struct _RPERF_INDEX_ENTRY_V1
{
    uint64_t ChunkSequence;
    uint64_t ChunkOffset;
    uint16_t ChunkType;
    uint16_t ChunkVersion;
    uint32_t ChunkFlags;
    uint64_t StoredSize;
    uint64_t UncompressedSize;
    uint64_t RecordCount;
    uint64_t FirstTimestamp;
    uint64_t LastTimestamp;
} RPERF_INDEX_ENTRY_V1;

typedef struct _RPERF_INDEX_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint32_t EntryCount;
    uint32_t EntrySize;
    uint32_t EntriesOffset;
    uint32_t Reserved;
} RPERF_INDEX_RECORD_V1;

typedef struct _RPERF_FOOTER_RECORD_V1
{
    RPERF_RECORD_HEADER_V1 Header;
    uint64_t FileSize;
    uint64_t ChunkCount;
    uint64_t LastChunkOffset;
    uint64_t IndexChunkOffset;
    uint64_t TotalRecordCount;
    uint64_t TotalLostRecords;
    int32_t FinalStatus;
    uint32_t FooterFlags;
    uint64_t Reserved;
} RPERF_FOOTER_RECORD_V1;

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#pragma pack(pop)
#endif

#define RPERF_JOIN2_(Left, Right) Left##Right
#define RPERF_JOIN_(Left, Right) RPERF_JOIN2_(Left, Right)
#if defined(__cplusplus) && (__cplusplus >= 201103L)
#define RPERF_STATIC_ASSERT(Expression) static_assert((Expression), #Expression)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define RPERF_STATIC_ASSERT(Expression) _Static_assert((Expression), #Expression)
#else
#define RPERF_STATIC_ASSERT(Expression) \
    typedef char RPERF_JOIN_(RperfStaticAssert_, __LINE__)[(Expression) ? 1 : -1]
#endif

RPERF_STATIC_ASSERT(sizeof(uint8_t) == 1);
RPERF_STATIC_ASSERT(sizeof(uint16_t) == 2);
RPERF_STATIC_ASSERT(sizeof(uint32_t) == 4);
RPERF_STATIC_ASSERT(sizeof(uint64_t) == 8);
RPERF_STATIC_ASSERT(sizeof(RPERF_FILE_HEADER_V2) == RPERF_FILE_HEADER_SIZE);
RPERF_STATIC_ASSERT(sizeof(RPERF_CHUNK_HEADER_V1) == RPERF_CHUNK_HEADER_SIZE);
RPERF_STATIC_ASSERT(sizeof(RPERF_CHUNK_TRAILER_V1) == RPERF_CHUNK_TRAILER_SIZE);
RPERF_STATIC_ASSERT(sizeof(RPERF_RECORD_HEADER_V1) == RPERF_RECORD_HEADER_SIZE);
RPERF_STATIC_ASSERT(offsetof(RPERF_CHUNK_HEADER_V1, HeaderCrc32) == 72);
RPERF_STATIC_ASSERT(offsetof(RPERF_FILE_HEADER_V2, HeaderCrc32) == 104);
RPERF_STATIC_ASSERT(sizeof(RPERF_SESSION_RECORD_V1) == 160);
RPERF_STATIC_ASSERT(sizeof(RPERF_SOURCE_RECORD_V1) == 112);
RPERF_STATIC_ASSERT(sizeof(RPERF_PROCESS_RECORD_V1) == 80);
RPERF_STATIC_ASSERT(sizeof(RPERF_THREAD_RECORD_V1) == 80);
RPERF_STATIC_ASSERT(sizeof(RPERF_IMAGE_RECORD_V1) == 96);
RPERF_STATIC_ASSERT(sizeof(RPERF_SAMPLE_RECORD_V1) == 88);
RPERF_STATIC_ASSERT(sizeof(RPERF_SCHEDULER_RECORD_V1) == 80);
RPERF_STATIC_ASSERT(sizeof(RPERF_COUNTER_RECORD_V1) == 80);
RPERF_STATIC_ASSERT(sizeof(RPERF_LOSS_RECORD_V1) == 88);
RPERF_STATIC_ASSERT(sizeof(RPERF_CLOCK_SYNC_RECORD_V1) == 72);
RPERF_STATIC_ASSERT(sizeof(RPERF_STRING_RECORD_V1) == 48);
RPERF_STATIC_ASSERT(sizeof(RPERF_SYMBOL_RECORD_V1) == 72);
RPERF_STATIC_ASSERT(sizeof(RPERF_MODULE_RECORD_V1) == 96);
RPERF_STATIC_ASSERT(sizeof(RPERF_INDEX_ENTRY_V1) == 64);
RPERF_STATIC_ASSERT(sizeof(RPERF_INDEX_RECORD_V1) == 48);
RPERF_STATIC_ASSERT(sizeof(RPERF_FOOTER_RECORD_V1) == 96);

typedef RPERF_STATUS
(*RPERF_READ_AT_CALLBACK)(void *Context,
                          uint64_t Offset,
                          void *Buffer,
                          uint32_t BytesToRead,
                          uint32_t *BytesRead);

typedef RPERF_STATUS
(*RPERF_WRITE_AT_CALLBACK)(void *Context,
                           uint64_t Offset,
                           const void *Buffer,
                           uint32_t BytesToWrite,
                           uint32_t *BytesWritten);

/*
 * Callbacks may complete partially but must make progress when returning
 * RPERF_OK. A read callback reports physical end-of-file as RPERF_OK with
 * *BytesRead == 0. Offsets are absolute, so non-streaming writers can patch
 * the finalized file header; STREAMING writers receive monotonically
 * increasing offsets only. Readers require random ReadAt access and may
 * revisit footer bytes or probe offsets during recovery.
 *
 * Writer, reader, iterator, callback context, and backing storage are owned
 * by the caller. The codec allocates nothing and has no destroy operation.
 * Options, descriptors, and write buffers are consumed synchronously and are
 * not retained. Iterator data is borrowed: each returned record pointer stays
 * valid only while that data remains unchanged and alive.
 *
 * A writer is terminal after Finalize or a sticky I/O error. A reader permits
 * only one active chunk: drain it with ReadChunkData and FinishChunk, or call
 * SkipChunk, before NextChunk. Recover abandons an active/corrupt chunk and
 * scans for the next fully checksummed frame.
 */

typedef struct _RPERF_WRITER_OPTIONS
{
    uint32_t Size;
    uint32_t Flags;
    uint8_t SessionId[16];
    uint64_t CreatedSystemTime100ns;
    uint64_t TimestampFrequency;
    uint32_t MaximumChunkBytes;
    uint32_t FileFlags; /* RECOVERED and REDACTED only. */
} RPERF_WRITER_OPTIONS;

typedef struct _RPERF_CHUNK_DESCRIPTOR
{
    uint32_t Size;
    uint16_t Type;
    uint16_t Version;
    uint32_t Flags;
    uint32_t CompressionAlgorithm;
    uint64_t StoredSize;
    uint64_t UncompressedSize;
    uint64_t RecordCount;
    uint64_t FirstTimestamp;
    uint64_t LastTimestamp;
} RPERF_CHUNK_DESCRIPTOR;

typedef struct _RPERF_READER_OPTIONS
{
    uint32_t Size;
    uint32_t Flags;
    uint32_t MaximumChunkBytes;
    uint32_t MaximumRecordBytes;
    uint64_t FileSizeLimit;
} RPERF_READER_OPTIONS;

typedef struct _RPERF_WRITER
{
    RPERF_WRITE_AT_CALLBACK WriteAt;
    void *Context;
    RPERF_FILE_HEADER_V2 Header;
    RPERF_CHUNK_HEADER_V1 ActiveChunk;
    uint64_t Offset;
    uint64_t ActiveChunkOffset;
    uint64_t LastChunkOffset;
    uint64_t ChunkCount;
    uint64_t TotalRecordCount;
    uint64_t IndexChunkOffset;
    uint64_t ActivePayloadRemaining;
    uint32_t ActivePayloadCrc32;
    uint32_t MaximumChunkBytes;
    uint32_t Flags;
    uint32_t Active;
    RPERF_STATUS Status;
} RPERF_WRITER;

typedef struct _RPERF_READER
{
    RPERF_READ_AT_CALLBACK ReadAt;
    void *Context;
    RPERF_FILE_HEADER_V2 Header;
    RPERF_CHUNK_HEADER_V1 ActiveChunk;
    uint64_t NextChunkOffset;
    uint64_t NextChunkSequence; /* Zero only while accepting recovery gap. */
    uint64_t ActiveChunkOffset;
    uint64_t ActivePayloadOffset;
    uint64_t ActivePayloadRemaining;
    uint64_t FileSizeLimit;
    uint64_t TotalRecordCount;
    uint32_t ActivePayloadCrc32;
    uint32_t MaximumChunkBytes;
    uint32_t MaximumRecordBytes;
    uint32_t Flags;
    uint32_t Active;
    uint32_t FooterSeen;
    uint32_t RecoveryUsed;
    RPERF_STATUS Status;
} RPERF_READER;

typedef struct _RPERF_RECORD_ITERATOR
{
    const uint8_t *Data;
    uint32_t Bytes;
    uint32_t Offset;
    uint32_t MaximumRecordBytes;
} RPERF_RECORD_ITERATOR;

uint16_t
RperfLoadLe16(const void *Buffer);

uint32_t
RperfLoadLe32(const void *Buffer);

uint64_t
RperfLoadLe64(const void *Buffer);

void
RperfStoreLe16(void *Buffer, uint16_t Value);

void
RperfStoreLe32(void *Buffer, uint32_t Value);

void
RperfStoreLe64(void *Buffer, uint64_t Value);

uint32_t
RperfCrc32(uint32_t InitialCrc, const void *Buffer, size_t BufferSize);

const char *
RperfStatusString(RPERF_STATUS Status);

/* Returns HeaderSize + PayloadBytes rounded up to the disk alignment. */
RPERF_STATUS
RperfCalculateRecordSize(uint32_t HeaderSize,
                         uint32_t PayloadBytes,
                         uint32_t *RecordSize);

RPERF_STATUS
RperfInitializeRecord(void *Record,
                      uint32_t RecordSize,
                      uint16_t Type,
                      uint16_t Version,
                      uint32_t Flags,
                      uint32_t HeaderSize,
                      uint64_t Sequence,
                      uint64_t Timestamp);

RPERF_STATUS
RperfValidateFileHeader(const void *Buffer,
                        size_t BufferSize,
                        uint32_t ReaderFlags,
                        RPERF_FILE_HEADER_V2 *Header);

RPERF_STATUS
RperfValidateRecord(const void *Record,
                    uint32_t AvailableBytes,
                    uint32_t MaximumRecordBytes);

RPERF_STATUS
RperfWriterInitialize(RPERF_WRITER *Writer,
                      RPERF_WRITE_AT_CALLBACK WriteAt,
                      void *Context,
                      const RPERF_WRITER_OPTIONS *Options);

RPERF_STATUS
RperfWriterBeginChunk(RPERF_WRITER *Writer,
                      const RPERF_CHUNK_DESCRIPTOR *Descriptor);

RPERF_STATUS
RperfWriterWriteChunkData(RPERF_WRITER *Writer,
                          const void *Buffer,
                          uint32_t BufferSize);

RPERF_STATUS
RperfWriterEndChunk(RPERF_WRITER *Writer);

RPERF_STATUS
RperfWriterWriteChunk(RPERF_WRITER *Writer,
                      const RPERF_CHUNK_DESCRIPTOR *Descriptor,
                      const void *Payload);

RPERF_STATUS
RperfWriterFinalize(RPERF_WRITER *Writer,
                    int32_t FinalStatus,
                    uint32_t FooterFlags,
                    uint64_t TotalLostRecords);

RPERF_STATUS
RperfReaderInitialize(RPERF_READER *Reader,
                      RPERF_READ_AT_CALLBACK ReadAt,
                      void *Context,
                      const RPERF_READER_OPTIONS *Options);

RPERF_STATUS
RperfReaderNextChunk(RPERF_READER *Reader,
                     RPERF_CHUNK_HEADER_V1 *ChunkHeader);

/* Reads stored payload bytes; compressed chunks are not decompressed. */
RPERF_STATUS
RperfReaderReadChunkData(RPERF_READER *Reader,
                         void *Buffer,
                         uint32_t BufferSize,
                         uint32_t *BytesRead);

RPERF_STATUS
RperfReaderFinishChunk(RPERF_READER *Reader);

RPERF_STATUS
RperfReaderSkipChunk(RPERF_READER *Reader);

RPERF_STATUS
RperfReaderRecover(RPERF_READER *Reader,
                   uint64_t MaximumScanBytes,
                   uint64_t *BytesSkipped);

void
RperfRecordIteratorInitialize(RPERF_RECORD_ITERATOR *Iterator,
                              const void *Data,
                              uint32_t DataBytes,
                              uint32_t MaximumRecordBytes);

/* Multi-byte fields in iterator results remain little-endian disk bytes. */
RPERF_STATUS
RperfRecordIteratorNext(RPERF_RECORD_ITERATOR *Iterator,
                        const RPERF_RECORD_HEADER_V1 **Record);

#ifdef __cplusplus
}
#endif

#endif /* _REACTOS_RPERF_H_ */
