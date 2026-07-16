/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Legacy text import and shared .rperf v2 model adapter
 */

#include "profiler_codec.h"
#include "rosprofiler.h"

#include <reactos/rperf.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RPERF_CODEC_TIMESTAMP_FREQUENCY 1000000000ULL
#define RPERF_CODEC_CHUNK_TARGET (1024U * 1024U)
#define RPERF_CODEC_RECOVERY_SCAN (64ULL * 1024 * 1024)
#define RPERF_CODEC_MAX_CHUNK_TYPE RPERF_CHUNK_FOOTER

typedef struct _RPERF_SDK_FILE
{
    HANDLE File;
    HANDLE CancelEvent;
    ULONGLONG PhysicalSize;
    ULONGLONG HighWaterMark;
    DWORD Error;
} RPERF_SDK_FILE;

typedef struct _RPERF_CHUNK_BUILDER
{
    UCHAR *Data;
    ULONG Size;
    ULONG Capacity;
    ULONGLONG RecordCount;
    ULONGLONG FirstTimestamp;
    ULONGLONG LastTimestamp;
} RPERF_CHUNK_BUILDER;

typedef struct _RPERF_SOURCE_MAP
{
    ULONG EventId;
    ULONG SourceId;
    ULONG SourceKind;
    ULONGLONG Period;
} RPERF_SOURCE_MAP;

typedef struct _RPERF_MODULE_MAP
{
    ULONGLONG ModelId;
    ULONG DiskId;
} RPERF_MODULE_MAP;

typedef struct _RPERF_V2_OUTPUT
{
    RPERF_WRITER Writer;
    RPERF_CHUNK_BUILDER Builders[RPERF_CODEC_MAX_CHUNK_TYPE + 1];
    ULONG ChunkTarget;
    ULONGLONG NextSequence;
    RPERF_SOURCE_MAP *Sources;
    SIZE_T SourceCount;
    RPERF_MODULE_MAP *Modules;
    SIZE_T ModuleCount;
} RPERF_V2_OUTPUT;

typedef struct _RPERF_STRING_VALUE
{
    ULONG Id;
    PSTR Text;
} RPERF_STRING_VALUE;

typedef struct _RPERF_ID_LIFETIME
{
    ULONG NumericId;
    ULONGLONG StableId;
    ULONGLONG StartNs;
    ULONGLONG EndNs;
} RPERF_ID_LIFETIME;

typedef struct _RPERF_ALIAS_SYMBOL
{
    RPERF_MODEL_SYMBOL Symbol;
} RPERF_ALIAS_SYMBOL;

typedef struct _RPERF_V2_INPUT
{
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_RECORDING *Recording;
    RPERF_FILE_HEADER_V2 FileHeader;
    RPERF_STRING_VALUE *Strings;
    SIZE_T StringCount;
    SIZE_T StringCapacity;
    RPERF_SOURCE_MAP *Sources;
    SIZE_T SourceCount;
    SIZE_T SourceCapacity;
    RPERF_ID_LIFETIME *Processes;
    SIZE_T ProcessCount;
    SIZE_T ProcessCapacity;
    RPERF_ID_LIFETIME *Threads;
    SIZE_T ThreadCount;
    SIZE_T ThreadCapacity;
    RPERF_ALIAS_SYMBOL *Aliases;
    SIZE_T AliasCount;
    SIZE_T AliasCapacity;
    ULONGLONG DiskRecordLimit;
    BOOL SessionSeen;
    BOOL FooterSeen;
    BOOL Incomplete;
    BOOL Recovered;
    BOOL TimerSource;
    BOOL PmuSource;
} RPERF_V2_INPUT;

typedef enum _RPERF_LOAD_PASS
{
    RperfLoadStrings = 1,
    RperfLoadMetadata,
    RperfLoadEvents
} RPERF_LOAD_PASS;

static BOOL
RperfCodecCancelled(HANDLE Event)
{
    return Event != NULL && WaitForSingleObject(Event, 0) == WAIT_OBJECT_0;
}

static DWORD
RperfCodecStatusError(RPERF_STATUS Status)
{
    switch (Status)
    {
        case RPERF_E_INVALID_ARGUMENT: return ERROR_INVALID_PARAMETER;
        case RPERF_E_IO: return ERROR_READ_FAULT;
        case RPERF_E_TRUNCATED: return ERROR_HANDLE_EOF;
        case RPERF_E_BAD_MAGIC:
        case RPERF_E_BAD_SIZE:
        case RPERF_E_CHECKSUM:
        case RPERF_E_CORRUPT: return ERROR_BAD_FORMAT;
        case RPERF_E_BAD_VERSION:
        case RPERF_E_BAD_ENDIAN: return ERROR_REVISION_MISMATCH;
        case RPERF_E_OVERFLOW: return ERROR_ARITHMETIC_OVERFLOW;
        case RPERF_E_UNSUPPORTED: return ERROR_NOT_SUPPORTED;
        case RPERF_E_STATE: return ERROR_INVALID_STATE;
        case RPERF_E_LIMIT: return ERROR_BUFFER_OVERFLOW;
        case RPERF_E_NOT_FOUND: return ERROR_NOT_FOUND;
        default: return ERROR_GEN_FAILURE;
    }
}

static BOOL
RperfCodecFailStatus(RPERF_STATUS Status,
                     const RPERF_SDK_FILE *File)
{
    DWORD Error = File != NULL ? File->Error : ERROR_SUCCESS;
    if (Error == ERROR_SUCCESS)
        Error = RperfCodecStatusError(Status);
    SetLastError(Error);
    return FALSE;
}

static BOOL
RperfCodecResize(PVOID *Buffer,
                 SIZE_T ElementSize,
                 SIZE_T *Capacity,
                 SIZE_T Required,
                 ULONGLONG Maximum)
{
    SIZE_T NewCapacity;
    PVOID NewBuffer;

    if (Required <= *Capacity)
        return TRUE;
    if ((ULONGLONG)Required > Maximum ||
        Required > ((SIZE_T)-1) / ElementSize)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    NewCapacity = *Capacity != 0 ? *Capacity : 64;
    while (NewCapacity < Required)
    {
        if (NewCapacity > ((SIZE_T)-1) / 2)
        {
            NewCapacity = Required;
            break;
        }
        NewCapacity *= 2;
    }
    if ((ULONGLONG)NewCapacity > Maximum)
        NewCapacity = (SIZE_T)Maximum;
    if (NewCapacity < Required ||
        NewCapacity > ((SIZE_T)-1) / ElementSize)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    if (*Buffer != NULL)
        NewBuffer = HeapReAlloc(GetProcessHeap(), 0, *Buffer,
                                NewCapacity * ElementSize);
    else
        NewBuffer = HeapAlloc(GetProcessHeap(), 0,
                              NewCapacity * ElementSize);
    if (NewBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    *Buffer = NewBuffer;
    *Capacity = NewCapacity;
    return TRUE;
}

static ULONGLONG
RperfCodecTimestampNs(ULONGLONG Value,
                      ULONGLONG Frequency)
{
    ULONGLONG Whole, Remainder;

    if (Frequency == 0 || Frequency == RPERF_CODEC_TIMESTAMP_FREQUENCY)
        return Value;
    Whole = Value / Frequency;
    Remainder = Value % Frequency;
    if (Whole > (ULONGLONG)-1 / RPERF_CODEC_TIMESTAMP_FREQUENCY)
        return (ULONGLONG)-1;
    return Whole * RPERF_CODEC_TIMESTAMP_FREQUENCY +
           (Remainder * RPERF_CODEC_TIMESTAMP_FREQUENCY) / Frequency;
}

static RPERF_STATUS
RperfSdkReadAt(PVOID Context,
               uint64_t Offset,
               PVOID Buffer,
               uint32_t BytesToRead,
               uint32_t *BytesRead)
{
    RPERF_SDK_FILE *File = Context;
    LARGE_INTEGER Position;
    DWORD Read = 0;

    *BytesRead = 0;
    if (RperfCodecCancelled(File->CancelEvent))
    {
        File->Error = ERROR_CANCELLED;
        return RPERF_E_IO;
    }
    if (Offset > 0x7fffffffffffffffULL)
        return RPERF_E_OVERFLOW;
    Position.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(File->File, Position, NULL, FILE_BEGIN) ||
        !ReadFile(File->File, Buffer, BytesToRead, &Read, NULL))
    {
        File->Error = GetLastError();
        return RPERF_E_IO;
    }
    *BytesRead = Read;
    return RPERF_OK;
}

static RPERF_STATUS
RperfSdkWriteAt(PVOID Context,
                uint64_t Offset,
                const void *Buffer,
                uint32_t BytesToWrite,
                uint32_t *BytesWritten)
{
    RPERF_SDK_FILE *File = Context;
    LARGE_INTEGER Position;
    DWORD Written = 0;

    *BytesWritten = 0;
    if (RperfCodecCancelled(File->CancelEvent))
    {
        File->Error = ERROR_CANCELLED;
        return RPERF_E_IO;
    }
    if (Offset > 0x7fffffffffffffffULL)
        return RPERF_E_OVERFLOW;
    Position.QuadPart = (LONGLONG)Offset;
    if (!SetFilePointerEx(File->File, Position, NULL, FILE_BEGIN) ||
        !WriteFile(File->File, Buffer, BytesToWrite, &Written, NULL))
    {
        File->Error = GetLastError();
        return RPERF_E_IO;
    }
    *BytesWritten = Written;
    if (Offset + Written > File->HighWaterMark)
        File->HighWaterMark = Offset + Written;
    return RPERF_OK;
}

static PSTR
RperfWideToUtf8(PCWSTR Text,
                ULONG MaximumBytes,
                ULONG *Length)
{
    int Required;
    PSTR Result;

    *Length = 0;
    if (Text == NULL || *Text == UNICODE_NULL)
        return NULL;
    Required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                   Text, -1, NULL, 0, NULL, NULL);
    if (Required <= 1 || (ULONG)Required - 1 > MaximumBytes)
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return NULL;
    }
    Result = HeapAlloc(GetProcessHeap(), 0, Required);
    if (Result == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                             Text, -1, Result, Required, NULL, NULL))
    {
        HeapFree(GetProcessHeap(), 0, Result);
        return NULL;
    }
    *Length = (ULONG)Required - 1;
    return Result;
}

static PWSTR
RperfUtf8ToWide(PCSTR Text,
                ULONG Length,
                ULONG MaximumBytes)
{
    int Required;
    SIZE_T Bytes;
    PWSTR Result;

    if (Text == NULL || Length == 0)
        return NULL;
    Required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   Text, Length, NULL, 0);
    if (Required <= 0 ||
        (SIZE_T)Required + 1 > ((SIZE_T)-1) / sizeof(WCHAR))
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return NULL;
    }
    Bytes = ((SIZE_T)Required + 1) * sizeof(WCHAR);
    if (Bytes > MaximumBytes)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Result = HeapAlloc(GetProcessHeap(), 0, Bytes);
    if (Result == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             Text, Length, Result, Required))
    {
        HeapFree(GetProcessHeap(), 0, Result);
        return NULL;
    }
    Result[Required] = UNICODE_NULL;
    return Result;
}

static PSTR
RperfDuplicateSanitizedAnsi(PCSTR Text,
                            ULONG MaximumBytes)
{
    SIZE_T Length, Index;
    PSTR Result;

    if (Text == NULL)
        Text = "";
    Length = strlen(Text);
    if (Length >= MaximumBytes)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Result = HeapAlloc(GetProcessHeap(), 0, Length + 1);
    if (Result == NULL)
        return NULL;
    for (Index = 0; Index < Length; ++Index)
    {
        CHAR Character = Text[Index];
        Result[Index] = (Character == '\t' || Character == '\r' ||
                         Character == '\n') ? ' ' : Character;
    }
    Result[Length] = ANSI_NULL;
    return Result;
}

static BOOL
RperfFindLegacyModule(const RPERF_RECORDING *Recording,
                      ULONGLONG Base,
                      ULONGLONG *Id)
{
    SIZE_T Index;

    for (Index = 0; Index < Recording->ModuleCount; ++Index)
    {
        if (Recording->Modules[Index].Base == Base)
        {
            *Id = Recording->Modules[Index].Id;
            return TRUE;
        }
    }
    return FALSE;
}

RPERF_RECORDING *
RperfRecordingFromLegacySession(const RPERF_SESSION *Legacy,
                                const RPERF_CAPTURE_LIMITS *Limits)
{
    RPERF_RECORDING *Recording;
    SIZE_T Index, FrameIndex;
    ULONGLONG Sequence = 1;

    Recording = RperfRecordingCreate(Limits);
    if (Recording == NULL)
        return NULL;
    Recording->Info.Backend = Legacy->Backend != 0 ?
                              Legacy->Backend : RperfBackendIntrusive;
    Recording->Info.Metric = RperfMetricSnapshotPopulation;
    Recording->Info.ProducerArchitecture = RperfNativeArchitecture();
    Recording->Info.AddressWidth = sizeof(PVOID) * 8;
    Recording->Info.IntervalMs = Legacy->IntervalMs;
    Recording->Info.RequestedDurationMs = Legacy->RequestedDurationMs;
    Recording->Info.EndTimeNs = Legacy->ElapsedUs <= (ULONGLONG)-1 / 1000 ?
                                Legacy->ElapsedUs * 1000 : (ULONGLONG)-1;
    Recording->Info.Complete = Legacy->LogComplete;
    Recording->Info.CompletionReason = Legacy->CompletionReason;
    Recording->Info.CompletionError = Legacy->CaptureError;
    if (!RperfRecordingSetTargetName(Recording, Legacy->ProcessName) ||
        !RperfRecordingSetSourcePath(Recording, Legacy->SourcePath))
        goto Failure;

    for (Index = 0; Index < Legacy->ModuleCount; ++Index)
    {
        const RPERF_CAPTURE_MODULE *Old = &Legacy->Modules[Index];
        RPERF_MODULE Module;

        ZeroMemory(&Module, sizeof(Module));
        Module.Id = Index + 1;
        Module.ProcessKey = Legacy->ProcessId;
        Module.Base = Old->Base;
        Module.Size = Old->Size;
        Module.Architecture = Old->Architecture;
        Module.Flags = Old->Flags;
        Module.TimeDateStamp = Old->TimeDateStamp;
        Module.Checksum = Old->Checksum;
        CopyMemory(Module.DebugId, Old->DebugId,
                   sizeof(Module.DebugId));
        Module.DebugAge = Old->DebugAge;
        Module.Path = (PWSTR)Old->Path;
        if (!RperfRecordingAddModule(Recording, &Module))
            goto Failure;
        if (Recording->Info.ProducerArchitecture == 0 &&
            Module.Architecture != 0)
            Recording->Info.ProducerArchitecture = Module.Architecture;
    }

    for (Index = 0; Index < Legacy->SymbolCount; ++Index)
    {
        const RPERF_SYMBOL *Old = &Legacy->Symbols[Index];
        RPERF_MODEL_SYMBOL Symbol;
        ULONGLONG ModuleId = RPERF_MODEL_INVALID_ID;

        if (Old->ModuleBase != 0 &&
            !RperfFindLegacyModule(Recording, Old->ModuleBase, &ModuleId))
        {
            RPERF_MODULE Module;
            WCHAR Name[64];

            ZeroMemory(&Module, sizeof(Module));
            Module.Id = Recording->ModuleCount + 1;
            Module.ProcessKey = Legacy->ProcessId;
            Module.Base = Old->ModuleBase;
            ModuleId = Module.Id;
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     Old->Module, -1,
                                     Name, ARRAYSIZE(Name)))
            {
                MultiByteToWideChar(CP_ACP, 0, Old->Module, -1,
                                    Name, ARRAYSIZE(Name));
            }
            Name[ARRAYSIZE(Name) - 1] = UNICODE_NULL;
            Module.Path = Name;
            if (!RperfRecordingAddModule(Recording, &Module))
                goto Failure;
        }
        ZeroMemory(&Symbol, sizeof(Symbol));
        Symbol.Address = Old->Address;
        Symbol.FunctionAddress = Old->FunctionAddress;
        Symbol.ModuleId = ModuleId;
        Symbol.RelativeAddress = Old->ModuleBase != 0 ?
                                 Old->Address - Old->ModuleBase : Old->Address;
        Symbol.Resolution = Old->Name[0] != ANSI_NULL &&
                            (Old->Status == RperfSymbolStatusResolved ||
                             (Old->Status ==
                                  RperfSymbolStatusUnattempted &&
                              strcmp(Old->Name, "<unknown>") != 0)) ?
                            RperfResolutionFunction : RperfResolutionAddress;
        Symbol.Source = Old->Source;
        Symbol.Status = Old->Status;
        Symbol.Name = (PSTR)Old->Name;
        Symbol.ModuleName = (PSTR)Old->Module;
        if (!RperfRecordingAddSymbol(Recording, &Symbol))
            goto Failure;
    }

    for (Index = 0; Index < Legacy->SampleCount; ++Index)
    {
        const RPERF_SAMPLE *Old = &Legacy->Samples[Index];
        RPERF_RECORD Record;

        ZeroMemory(&Record, sizeof(Record));
        Record.Header.Kind = RperfRecordSample;
        Record.Header.Sequence = Sequence++;
        Record.Header.TimestampNs = Old->TimeUs <= (ULONGLONG)-1 / 1000 ?
                                    Old->TimeUs * 1000 : (ULONGLONG)-1;
        Record.Header.ProcessKey = Old->ProcessId;
        Record.Header.ThreadKey = Old->ThreadId;
        Record.Header.ProcessId = Old->ProcessId;
        Record.Header.ThreadId = Old->ThreadId;
        Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
        if (Old->Flags & RPERF_SAMPLE_TRUNCATED)
            Record.Header.Flags |= RPERF_MODEL_RECORD_FLAG_TRUNCATED;
        if (Old->Flags & RPERF_SAMPLE_UNWIND_FAILED)
            Record.Header.Flags |= RPERF_MODEL_RECORD_FLAG_UNWIND_FAILED;
        Record.Data.Sample.Weight = 1;
        Record.Data.Sample.Period = Legacy->IntervalMs * 1000000ULL;
        Record.Data.Sample.Depth = Old->Depth;
        for (FrameIndex = 0; FrameIndex < Old->Depth; ++FrameIndex)
        {
            const RPERF_SYMBOL *OldSymbol =
                RperfFindSymbol(Legacy, Old->Frames[FrameIndex]);
            RPERF_FRAME *Frame = &Record.Data.Sample.Frames[FrameIndex];

            Frame->Address = Old->Frames[FrameIndex];
            Frame->FunctionAddress = Frame->Address;
            Frame->Context = RperfContextUser;
            Frame->Resolution = RperfResolutionAddress;
            Frame->ModuleId = RPERF_MODEL_INVALID_ID;
            if (OldSymbol != NULL)
            {
                Frame->FunctionAddress = OldSymbol->FunctionAddress;
                Frame->Resolution = OldSymbol->Name[0] != ANSI_NULL ?
                                    RperfResolutionFunction :
                                    RperfResolutionAddress;
                if (OldSymbol->ModuleBase != 0)
                    RperfFindLegacyModule(Recording,
                                          OldSymbol->ModuleBase,
                                          &Frame->ModuleId);
            }
        }
        if (!RperfRecordingAddRecord(Recording, &Record))
            goto Failure;
    }
    if (Legacy->MissedThreads >
        (ULONGLONG)-1 - (ULONGLONG)Legacy->SampleCount)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        goto Failure;
    }
    Recording->Counters = Legacy->Counters;
    if (Recording->Counters.AttemptedSamples <
        (ULONGLONG)Legacy->SampleCount + Legacy->MissedThreads)
    {
        Recording->Counters.AttemptedSamples =
            (ULONGLONG)Legacy->SampleCount + Legacy->MissedThreads;
    }
    Recording->Counters.SuccessfulSamples = Legacy->SampleCount;
    if (Recording->Counters.FailedSamples < Legacy->MissedThreads)
        Recording->Counters.FailedSamples = Legacy->MissedThreads;
    Recording->Counters.TruncatedSamples = Legacy->TruncatedStacks;
    Recording->Counters.MissedCadenceTicks = Legacy->MissedTicks;
    Recording->Counters.LostRecords = Legacy->LostSamples;
    if (!RperfRecordingFreeze(Recording))
        goto Failure;
    return Recording;

Failure:
    RperfRecordingRelease(Recording);
    return NULL;
}

static BOOL
RperfLoadV1(PCWSTR Path,
            const RPERF_CAPTURE_LIMITS *Limits,
            RPERF_RECORDING **Recording)
{
    RPERF_SESSION Legacy;
    RPERF_RECORDING *Converted;

    RperfSessionInitialize(&Legacy);
    if (!RperfLoadLog(&Legacy, Path))
        return FALSE;
    Converted = RperfRecordingFromLegacySession(&Legacy, Limits);
    RperfSessionClear(&Legacy);
    if (Converted == NULL)
        return FALSE;
    *Recording = Converted;
    return TRUE;
}

static BOOL
RperfSaveV1(PCWSTR Path,
            const RPERF_RECORDING *Recording,
            HANDLE CancelEvent,
            RPERF_CODEC_PROGRESS Progress,
            PVOID ProgressContext)
{
    FILE *File;
    PSTR Name = NULL;
    ULONG NameLength = 0;
    SIZE_T Index, FrameIndex;
    ULONG ProcessId = 0;
    BOOL Result = FALSE;

    File = _wfopen(Path, L"wt");
    if (File == NULL)
        return FALSE;
    Name = RperfWideToUtf8(Recording->Info.TargetName,
                           Recording->Limits.MaxStringBytes,
                           &NameLength);
    if (Name == NULL)
        Name = RperfDuplicateSanitizedAnsi("<unknown>",
                                           Recording->Limits.MaxStringBytes);
    if (Name == NULL)
        goto Cleanup;
    {
        PSTR Character;
        for (Character = Name; *Character != ANSI_NULL; ++Character)
        {
            if (*Character == '\t' || *Character == '\r' ||
                *Character == '\n')
                *Character = ' ';
        }
    }
    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        if (Recording->Records[Index].Header.ProcessId != 0)
        {
            ProcessId = Recording->Records[Index].Header.ProcessId;
            break;
        }
    }
    if (fprintf(File, "RPERF\t1\n") < 0 ||
        fprintf(File, "p\t%lu\t%s\n", ProcessId, Name) < 0 ||
        fprintf(File, "c\t%lu\t%lu\t%s\n",
                Recording->Info.IntervalMs,
                Recording->Info.RequestedDurationMs,
                Recording->Info.Backend == RperfBackendKernel ?
                    "kernel-on-cpu-samples" :
                Recording->Info.Backend == RperfBackendEtw ?
                    "etw-sampled-profile" :
                Recording->Info.Backend == RperfBackendFake ?
                    "synthetic-contract-test" :
                    "wall-clock-all-threads") < 0)
        goto Cleanup;

    for (Index = 0; Index < Recording->SymbolCount; ++Index)
    {
        const RPERF_MODEL_SYMBOL *Symbol = &Recording->Symbols[Index];
        const RPERF_MODULE *Module =
            RperfRecordingFindModule(Recording, Symbol->ModuleId);
        PSTR ModuleName = RperfDuplicateSanitizedAnsi(
            Symbol->ModuleName != NULL ? Symbol->ModuleName : "<unknown>",
            Recording->Limits.MaxStringBytes);
        PSTR SymbolName = RperfDuplicateSanitizedAnsi(
            Symbol->Name != NULL ? Symbol->Name : "<unresolved>",
            Recording->Limits.MaxStringBytes);
        ULONGLONG Base = Module != NULL ? Module->Base : 0;

        if (ModuleName == NULL || SymbolName == NULL ||
            fprintf(File,
                    "y\t%I64x\t%I64x\t%I64x\t%I64x\t%s\t%s\n",
                    Symbol->Address, Symbol->FunctionAddress, Base,
                    Symbol->Address >= Symbol->FunctionAddress ?
                    Symbol->Address - Symbol->FunctionAddress : 0,
                    ModuleName != NULL ? ModuleName : "<unknown>",
                    SymbolName != NULL ? SymbolName : "<unresolved>") < 0)
        {
            if (ModuleName != NULL)
                HeapFree(GetProcessHeap(), 0, ModuleName);
            if (SymbolName != NULL)
                HeapFree(GetProcessHeap(), 0, SymbolName);
            goto Cleanup;
        }
        HeapFree(GetProcessHeap(), 0, ModuleName);
        HeapFree(GetProcessHeap(), 0, SymbolName);
    }

    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        const RPERF_RECORD *Record = &Recording->Records[Index];
        USHORT Depth;
        ULONG LegacyFlags = 0;

        if (Record->Header.Kind != RperfRecordSample)
            continue;
        if ((Index & 1023) == 0)
        {
            if (RperfCodecCancelled(CancelEvent))
            {
                SetLastError(ERROR_CANCELLED);
                goto Cleanup;
            }
            if (Progress != NULL)
                Progress(ProgressContext, Index, Recording->RecordCount);
        }
        Depth = Record->Data.Sample.Depth > RPERF_MAX_FRAMES ?
                RPERF_MAX_FRAMES : Record->Data.Sample.Depth;
        if (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_TRUNCATED)
            LegacyFlags |= RPERF_SAMPLE_TRUNCATED;
        if (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_UNWIND_FAILED)
            LegacyFlags |= RPERF_SAMPLE_UNWIND_FAILED;
        if (fprintf(File, "s\t%I64u\t%lu\t%lu\t%lu\t%u",
                    Record->Header.TimestampNs / 1000,
                    Record->Header.ProcessId,
                    Record->Header.ThreadId,
                    LegacyFlags,
                    Depth) < 0)
            goto Cleanup;
        for (FrameIndex = 0; FrameIndex < Depth; ++FrameIndex)
        {
            if (fprintf(File, "\t%I64x",
                        Record->Data.Sample.Frames[FrameIndex].Address) < 0)
                goto Cleanup;
        }
        if (fprintf(File, "\n") < 0)
            goto Cleanup;
    }
    if (fprintf(File,
                "e\t%I64u\t0\t0\t%I64u\t%I64u\t%I64u\t%I64u\t%u\t%lu\n",
                Recording->Info.EndTimeNs / 1000,
                Recording->Counters.FailedSamples,
                Recording->Counters.MissedCadenceTicks,
                Recording->Counters.TruncatedSamples,
                Recording->Counters.LostRecords,
                Recording->Info.CompletionReason,
                Recording->Info.CompletionError) < 0 ||
        fflush(File) != 0)
        goto Cleanup;
    Result = TRUE;

Cleanup:
    if (Name != NULL)
        HeapFree(GetProcessHeap(), 0, Name);
    if (fclose(File) != 0)
        Result = FALSE;
    if (!Result && GetLastError() == ERROR_SUCCESS)
        SetLastError(ERROR_WRITE_FAULT);
    return Result;
}

static ULONG
RperfSdkRecordSize(ULONG HeaderSize,
                   SIZE_T PayloadBytes)
{
    uint32_t Size;
    RPERF_STATUS Status;

    if (PayloadBytes > MAXDWORD)
        return 0;
    Status = RperfCalculateRecordSize(HeaderSize,
                                      (ULONG)PayloadBytes,
                                      &Size);
    if (Status != RPERF_OK)
    {
        SetLastError(RperfCodecStatusError(Status));
        return 0;
    }
    return Size;
}

static BOOL
RperfFlushChunkType(RPERF_V2_OUTPUT *Output,
                    USHORT Type)
{
    RPERF_CHUNK_BUILDER *Builder;
    RPERF_CHUNK_DESCRIPTOR Descriptor;
    RPERF_STATUS Status;

    if (Type == 0 || Type > RPERF_CODEC_MAX_CHUNK_TYPE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Builder = &Output->Builders[Type];
    if (Builder->Size == 0)
        return TRUE;
    ZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.Size = sizeof(Descriptor);
    Descriptor.Type = Type;
    Descriptor.Version = 1;
    Descriptor.CompressionAlgorithm = RPERF_COMPRESSION_NONE;
    Descriptor.StoredSize = Builder->Size;
    Descriptor.UncompressedSize = Builder->Size;
    Descriptor.RecordCount = Builder->RecordCount;
    Descriptor.FirstTimestamp = Builder->FirstTimestamp;
    Descriptor.LastTimestamp = Builder->LastTimestamp;
    Status = RperfWriterWriteChunk(&Output->Writer,
                                   &Descriptor,
                                   Builder->Data);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status,
            (const RPERF_SDK_FILE *)Output->Writer.Context);
    Builder->Size = 0;
    Builder->RecordCount = 0;
    Builder->FirstTimestamp = 0;
    Builder->LastTimestamp = 0;
    return TRUE;
}

static BOOL
RperfAppendChunkRecord(RPERF_V2_OUTPUT *Output,
                       USHORT ChunkType,
                       const UCHAR *Record,
                       ULONG RecordSize)
{
    RPERF_CHUNK_BUILDER *Builder;
    RPERF_STATUS Status;
    ULONGLONG Timestamp;
    ULONG Required, Capacity;
    PVOID NewBuffer;

    if (ChunkType == 0 || ChunkType > RPERF_CODEC_MAX_CHUNK_TYPE ||
        ChunkType == RPERF_CHUNK_FOOTER || Record == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Status = RperfValidateRecord(Record,
                                 RecordSize,
                                 Output->Writer.MaximumChunkBytes);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    if (RecordSize > Output->Writer.MaximumChunkBytes)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    Builder = &Output->Builders[ChunkType];
    if (Builder->Size != 0 &&
        (RecordSize > Output->ChunkTarget -
                      min(Output->ChunkTarget, Builder->Size)))
    {
        if (!RperfFlushChunkType(Output, ChunkType))
            return FALSE;
    }
    if (Builder->Size > MAXDWORD - RecordSize)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }
    Required = Builder->Size + RecordSize;
    if (Required > Builder->Capacity)
    {
        Capacity = Builder->Capacity != 0 ? Builder->Capacity : 4096;
        while (Capacity < Required)
        {
            ULONG Next = Capacity <= Output->ChunkTarget / 2 ?
                         Capacity * 2 : Required;
            if (Next < Capacity || Next > Output->Writer.MaximumChunkBytes)
                Next = Required;
            Capacity = Next;
        }
        if (Capacity > Output->Writer.MaximumChunkBytes)
            Capacity = Required;
        if (Capacity < Required)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        if (Builder->Data != NULL)
            NewBuffer = HeapReAlloc(GetProcessHeap(), 0,
                                    Builder->Data, Capacity);
        else
            NewBuffer = HeapAlloc(GetProcessHeap(), 0, Capacity);
        if (NewBuffer == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        Builder->Data = NewBuffer;
        Builder->Capacity = Capacity;
    }
    CopyMemory(Builder->Data + Builder->Size, Record, RecordSize);
    Builder->Size += RecordSize;
    Timestamp = RperfLoadLe64(Record +
                              offsetof(RPERF_RECORD_HEADER_V1, Timestamp));
    if (Builder->RecordCount == 0)
    {
        Builder->FirstTimestamp = Timestamp;
        Builder->LastTimestamp = Timestamp;
    }
    else
    {
        if (Timestamp < Builder->FirstTimestamp)
            Builder->FirstTimestamp = Timestamp;
        if (Timestamp > Builder->LastTimestamp)
            Builder->LastTimestamp = Timestamp;
    }
    Builder->RecordCount++;
    return TRUE;
}

static VOID
RperfDestroyV2Output(RPERF_V2_OUTPUT *Output)
{
    ULONG Type;

    for (Type = 0; Type <= RPERF_CODEC_MAX_CHUNK_TYPE; ++Type)
    {
        if (Output->Builders[Type].Data != NULL)
            HeapFree(GetProcessHeap(), 0, Output->Builders[Type].Data);
    }
    if (Output->Sources != NULL)
        HeapFree(GetProcessHeap(), 0, Output->Sources);
    if (Output->Modules != NULL)
        HeapFree(GetProcessHeap(), 0, Output->Modules);
    ZeroMemory(Output, sizeof(*Output));
}

static int __cdecl
RperfCompareUlong(const void *Left,
                  const void *Right)
{
    ULONG A = *(const ULONG *)Left;
    ULONG B = *(const ULONG *)Right;

    return A < B ? -1 : A > B ? 1 : 0;
}

static int __cdecl
RperfCompareModuleMap(const void *Left,
                      const void *Right)
{
    const RPERF_MODULE_MAP *A = Left;
    const RPERF_MODULE_MAP *B = Right;

    return A->ModelId < B->ModelId ? -1 :
           A->ModelId > B->ModelId ? 1 : 0;
}

static BOOL
RperfCollectOutputMaps(RPERF_V2_OUTPUT *Output,
                       const RPERF_RECORDING *Recording)
{
    ULONG *Events;
    SIZE_T EventCount = 0, Index, Unique;

    Events = Recording->RecordCount != 0 ?
             HeapAlloc(GetProcessHeap(), 0,
                       Recording->RecordCount * sizeof(*Events)) : NULL;
    if (Recording->RecordCount != 0 && Events == NULL)
        return FALSE;
    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        const RPERF_RECORD *Record = &Recording->Records[Index];
        if (Record->Header.Kind == RperfRecordSample ||
            Record->Header.Kind == RperfRecordPmu)
            Events[EventCount++] = Record->Header.EventId;
    }
    if (EventCount == 0)
    {
        if (Events == NULL)
            Events = HeapAlloc(GetProcessHeap(), 0, sizeof(*Events));
        if (Events == NULL)
            return FALSE;
        Events[EventCount++] = 0;
    }
    qsort(Events, EventCount, sizeof(*Events), RperfCompareUlong);
    Unique = 1;
    for (Index = 1; Index < EventCount; ++Index)
    {
        if (Events[Index] != Events[Unique - 1])
            Events[Unique++] = Events[Index];
    }
    if (Unique > MAXDWORD)
    {
        HeapFree(GetProcessHeap(), 0, Events);
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    Output->Sources = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                Unique * sizeof(*Output->Sources));
    if (Output->Sources == NULL)
    {
        HeapFree(GetProcessHeap(), 0, Events);
        return FALSE;
    }
    Output->SourceCount = Unique;
    for (Index = 0; Index < Unique; ++Index)
    {
        Output->Sources[Index].EventId = Events[Index];
        Output->Sources[Index].SourceId = (ULONG)Index + 1;
        Output->Sources[Index].SourceKind =
            Recording->Info.Metric == RperfMetricEventWeight ?
            RPERF_SOURCE_PMU : RPERF_SOURCE_TIMER;
        Output->Sources[Index].Period =
            Recording->Info.IntervalMs * 1000000ULL;
    }
    HeapFree(GetProcessHeap(), 0, Events);

    if (Recording->ModuleCount != 0)
    {
        if (Recording->ModuleCount > MAXDWORD ||
            Recording->ModuleCount > ((SIZE_T)-1) /
                                     sizeof(*Output->Modules))
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        Output->Modules = HeapAlloc(GetProcessHeap(), 0,
            Recording->ModuleCount * sizeof(*Output->Modules));
        if (Output->Modules == NULL)
            return FALSE;
        Output->ModuleCount = Recording->ModuleCount;
        for (Index = 0; Index < Recording->ModuleCount; ++Index)
        {
            Output->Modules[Index].ModelId = Recording->Modules[Index].Id;
            Output->Modules[Index].DiskId = (ULONG)Index + 1;
        }
        qsort(Output->Modules, Output->ModuleCount,
              sizeof(*Output->Modules), RperfCompareModuleMap);
        for (Index = 1; Index < Output->ModuleCount; ++Index)
        {
            if (Output->Modules[Index - 1].ModelId ==
                Output->Modules[Index].ModelId)
            {
                SetLastError(ERROR_BAD_FORMAT);
                return FALSE;
            }
        }
    }
    return TRUE;
}

static ULONG
RperfOutputSourceId(const RPERF_V2_OUTPUT *Output,
                    ULONG EventId)
{
    SIZE_T Low = 0, High = Output->SourceCount;

    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (Output->Sources[Middle].EventId < EventId)
            Low = Middle + 1;
        else
            High = Middle;
    }
    if (Low < Output->SourceCount &&
        Output->Sources[Low].EventId == EventId)
        return Output->Sources[Low].SourceId;
    return 0;
}

static ULONG
RperfOutputModuleId(const RPERF_V2_OUTPUT *Output,
                    ULONGLONG ModelId)
{
    SIZE_T Low = 0, High = Output->ModuleCount;

    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (Output->Modules[Middle].ModelId < ModelId)
            Low = Middle + 1;
        else
            High = Middle;
    }
    if (Low < Output->ModuleCount &&
        Output->Modules[Low].ModelId == ModelId)
        return Output->Modules[Low].DiskId;
    return 0;
}

static ULONG
RperfOutputModuleIndex(const RPERF_RECORDING *Recording,
                       ULONGLONG ModelId)
{
    SIZE_T Index;

    for (Index = 0; Index < Recording->ModuleCount; ++Index)
    {
        if (Recording->Modules[Index].Id == ModelId)
            return (ULONG)Index;
    }
    return MAXDWORD;
}

static BOOL
RperfAppendStringRecord(RPERF_V2_OUTPUT *Output,
                        PCSTR Text,
                        ULONG Length,
                        ULONG MaximumRecordBytes,
                        ULONG *NextStringId,
                        ULONG *StringId)
{
    RPERF_STRING_RECORD_V1 *Record;
    ULONG Size;
    RPERF_STATUS Status;
    BOOL Result;

    *StringId = 0;
    if (Text == NULL || Length == 0)
        return TRUE;
    if (*NextStringId == MAXDWORD ||
        Length > RPERF_MAX_STRING_BYTES)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    Size = RperfSdkRecordSize(sizeof(*Record), Length);
    if (Size == 0 || Size > MaximumRecordBytes)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    Record = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Record == NULL)
        return FALSE;
    Status = RperfInitializeRecord(Record, Size,
                                   RPERF_RECORD_STRING, 1, 0,
                                   sizeof(*Record),
                                   Output->NextSequence, 0);
    if (Status != RPERF_OK)
    {
        HeapFree(GetProcessHeap(), 0, Record);
        return RperfCodecFailStatus(Status, NULL);
    }
    (*NextStringId)++;
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_STRING_RECORD_V1, StringId),
                   *NextStringId);
    RperfStoreLe16((UCHAR *)Record +
                   offsetof(RPERF_STRING_RECORD_V1, Encoding),
                   RPERF_STRING_UTF8);
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_STRING_RECORD_V1, DataOffset),
                   sizeof(*Record));
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_STRING_RECORD_V1, DataBytes),
                   Length);
    CopyMemory((UCHAR *)Record + sizeof(*Record), Text, Length);
    Result = RperfAppendChunkRecord(Output, RPERF_CHUNK_STRING_TABLE,
                                    (const UCHAR *)Record, Size);
    HeapFree(GetProcessHeap(), 0, Record);
    if (!Result)
        return FALSE;
    Output->NextSequence++;
    *StringId = *NextStringId;
    return TRUE;
}

static ULONG
RperfOutputRecordFlags(const RPERF_RECORD *Record)
{
    ULONG Flags = 0;

    if (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_TRUNCATED)
        Flags |= RPERF_RECORD_FLAG_TRUNCATED;
    if (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_REDACTED)
        Flags |= RPERF_RECORD_FLAG_REDACTED;
    if (Record->Header.Flags & (RPERF_MODEL_RECORD_FLAG_INCOMPLETE |
                                RPERF_MODEL_RECORD_FLAG_SYNTHETIC))
        Flags |= RPERF_RECORD_FLAG_SYNTHETIC;
    return Flags;
}

static ULONG
RperfOutputStopReason(ULONG CompletionReason)
{
    switch ((RPERF_COMPLETION_REASON)CompletionReason)
    {
        case RperfCompletionDuration: return RPERF_STOP_LIMIT;
        case RperfCompletionUserStop: return RPERF_STOP_REQUESTED;
        case RperfCompletionTargetExit: return RPERF_STOP_TARGET_EXIT;
        case RperfCompletionError: return RPERF_STOP_INTERNAL_ERROR;
        case RperfCompletionIncomplete:
        default: return RPERF_STOP_NONE;
    }
}

static ULONG
RperfInputCompletionReason(ULONG StopReason)
{
    switch (StopReason)
    {
        case RPERF_STOP_LIMIT: return RperfCompletionDuration;
        case RPERF_STOP_REQUESTED: return RperfCompletionUserStop;
        case RPERF_STOP_TARGET_EXIT: return RperfCompletionTargetExit;
        case RPERF_STOP_NONE: return RperfCompletionIncomplete;
        default: return RperfCompletionError;
    }
}

static BOOL
RperfAppendSessionRecord(RPERF_V2_OUTPUT *Output,
                         const RPERF_RECORDING *Recording,
                         ULONG TargetStringId,
                         ULONG SourceStringId)
{
    UCHAR Bytes[sizeof(RPERF_SESSION_RECORD_V1)];
    SYSTEM_INFO SystemInfo;
    ULONGLONG SourceMask = 0, RecordMask = 0;
    SIZE_T Index;
    RPERF_STATUS Status;
    ULONG CaptureStatus = Recording->Info.CompletionError;

    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_SESSION,
                                   RPERF_SESSION_RECORD_VERSION_METADATA, 0,
                                   sizeof(Bytes),
                                   Output->NextSequence, 0);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    CopyMemory(Bytes + offsetof(RPERF_SESSION_RECORD_V1, SessionId),
               &Recording->Info.SessionId, sizeof(GUID));
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1,
                            CaptureStartTimestamp),
                   Recording->Info.StartTimeNs);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1,
                            CaptureEndTimestamp),
                   Recording->Info.EndTimeNs);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1,
                            TimestampFrequency),
                   RPERF_CODEC_TIMESTAMP_FREQUENCY);
    for (Index = 0; Index < Output->SourceCount; ++Index)
    {
        if (Output->Sources[Index].SourceKind < 64)
            SourceMask |= 1ULL << Output->Sources[Index].SourceKind;
    }
    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        ULONG Type = 0;
        switch (Recording->Records[Index].Header.Kind)
        {
            case RperfRecordProcessStart:
            case RperfRecordProcessEnd: Type = RPERF_RECORD_PROCESS; break;
            case RperfRecordThreadStart:
            case RperfRecordThreadEnd: Type = RPERF_RECORD_THREAD; break;
            case RperfRecordImageLoad:
            case RperfRecordImageUnload: Type = RPERF_RECORD_IMAGE; break;
            case RperfRecordSample: Type = RPERF_RECORD_SAMPLE; break;
            case RperfRecordContextSwitch:
            case RperfRecordWakeup: Type = RPERF_RECORD_SCHEDULER; break;
            case RperfRecordLost: Type = RPERF_RECORD_LOSS; break;
            case RperfRecordClockSync: Type = RPERF_RECORD_CLOCK_SYNC; break;
            default: break;
        }
        if (Type < 64)
            RecordMask |= 1ULL << Type;
    }
    RperfStoreLe64(Bytes + offsetof(RPERF_SESSION_RECORD_V1, Sources),
                   SourceMask);
    RperfStoreLe64(Bytes + offsetof(RPERF_SESSION_RECORD_V1, RecordTypes),
                   RecordMask);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1, Architecture),
                   Recording->Info.ProducerArchitecture);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1, PointerWidth),
                   Recording->Info.AddressWidth);
    GetSystemInfo(&SystemInfo);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1, ProcessorCount),
                   SystemInfo.dwNumberOfProcessors);
    RperfStoreLe32(Bytes + offsetof(RPERF_SESSION_RECORD_V1, ClockType),
                   Recording->Info.ClockId);
    if (!Recording->Info.Complete && CaptureStatus == ERROR_SUCCESS)
        CaptureStatus = ERROR_HANDLE_EOF;
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1, CaptureStatus),
                   CaptureStatus);
    RperfStoreLe32(Bytes + offsetof(RPERF_SESSION_RECORD_V1, StopReason),
                   RperfOutputStopReason(
                       Recording->Info.CompletionReason));
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1, HostNameStringId),
                   TargetStringId);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SESSION_RECORD_V1, CommandLineStringId),
                   SourceStringId);
    RperfStoreLe64(Bytes + offsetof(RPERF_SESSION_RECORD_V1, Reserved),
                   ((ULONGLONG)Recording->Info.Backend &
                    RPERF_SESSION_META_BACKEND_MASK) |
                   (((ULONGLONG)Recording->Info.Metric <<
                     RPERF_SESSION_META_METRIC_SHIFT) &
                    RPERF_SESSION_META_METRIC_MASK));
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_SESSION,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendSourceRecords(RPERF_V2_OUTPUT *Output)
{
    SIZE_T Index;

    for (Index = 0; Index < Output->SourceCount; ++Index)
    {
        const RPERF_SOURCE_MAP *Source = &Output->Sources[Index];
        UCHAR Bytes[sizeof(RPERF_SOURCE_RECORD_V1)];
        RPERF_STATUS Status;

        Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                       RPERF_RECORD_SOURCE, 1, 0,
                                       sizeof(Bytes),
                                       Output->NextSequence, 0);
        if (Status != RPERF_OK)
            return RperfCodecFailStatus(Status, NULL);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SOURCE_RECORD_V1, SourceId),
                       Source->SourceId);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SOURCE_RECORD_V1, SourceKind),
                       Source->SourceKind);
        RperfStoreLe64(Bytes +
                       offsetof(RPERF_SOURCE_RECORD_V1, ScaleNumerator), 1);
        RperfStoreLe64(Bytes +
                       offsetof(RPERF_SOURCE_RECORD_V1, ScaleDenominator), 1);
        RperfStoreLe64(Bytes +
                       offsetof(RPERF_SOURCE_RECORD_V1, MinimumPeriod),
                       Source->Period);
        RperfStoreLe64(Bytes +
                       offsetof(RPERF_SOURCE_RECORD_V1, MaximumPeriod),
                       Source->Period);
        RperfStoreLe64(Bytes +
                       offsetof(RPERF_SOURCE_RECORD_V1, EventCode),
                       Source->EventId);
        if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_SOURCE_TABLE,
                                    Bytes, sizeof(Bytes)))
            return FALSE;
        Output->NextSequence++;
    }
    return TRUE;
}

static BOOL
RperfDebugIdPresent(const RPERF_MODULE *Module)
{
    ULONG Index;

    if (Module->DebugAge != 0)
        return TRUE;
    for (Index = 0; Index < sizeof(Module->DebugId); ++Index)
    {
        if (Module->DebugId[Index] != 0)
            return TRUE;
    }
    return FALSE;
}

static BOOL
RperfAppendModuleRecords(RPERF_V2_OUTPUT *Output,
                         const RPERF_RECORDING *Recording,
                         const ULONG *PathStringIds)
{
    SIZE_T Index;

    for (Index = 0; Index < Recording->ModuleCount; ++Index)
    {
        const RPERF_MODULE *Module = &Recording->Modules[Index];
        RPERF_MODULE_RECORD_V1 *Record;
        ULONG DiskId = RperfOutputModuleId(Output, Module->Id);
        ULONG DebugBytes = RperfDebugIdPresent(Module) ? 20 : 0;
        ULONG Size = RperfSdkRecordSize(sizeof(*Record), DebugBytes);
        RPERF_STATUS Status;
        BOOL Result;

        if (DiskId == 0 || Size == 0 ||
            Size > Recording->Limits.MaxRecordBytes)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        Record = HeapAlloc(GetProcessHeap(), 0, Size);
        if (Record == NULL)
            return FALSE;
        Status = RperfInitializeRecord(Record, Size,
                                       RPERF_RECORD_MODULE, 1, 0,
                                       sizeof(*Record),
                                       Output->NextSequence, 0);
        if (Status != RPERF_OK)
        {
            HeapFree(GetProcessHeap(), 0, Record);
            return RperfCodecFailStatus(Status, NULL);
        }
        RperfStoreLe32((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, ModuleId), DiskId);
        if (Module->ProcessKey <= MAXDWORD)
        {
            RperfStoreLe32((UCHAR *)Record +
                           offsetof(RPERF_MODULE_RECORD_V1, ProcessId),
                           (ULONG)Module->ProcessKey);
        }
        RperfStoreLe64((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, UniqueProcessKey),
                       Module->ProcessKey);
        RperfStoreLe64((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, ImageBase),
                       Module->Base);
        RperfStoreLe64((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, ImageSize),
                       Module->Size);
        RperfStoreLe32((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, PathStringId),
                       PathStringIds != NULL ? PathStringIds[Index] : 0);
        RperfStoreLe32((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, ModuleFlags),
                       (Module->Flags & ~RPERF_MODULE_FLAG_ARCH_MASK) |
                       (Module->Architecture &
                        RPERF_MODULE_FLAG_ARCH_MASK));
        RperfStoreLe32((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, Checksum),
                       Module->Checksum);
        RperfStoreLe32((UCHAR *)Record +
                       offsetof(RPERF_MODULE_RECORD_V1, TimeDateStamp),
                       Module->TimeDateStamp);
        if (DebugBytes != 0)
        {
            RperfStoreLe32((UCHAR *)Record +
                           offsetof(RPERF_MODULE_RECORD_V1, DebugIdOffset),
                           sizeof(*Record));
            RperfStoreLe32((UCHAR *)Record +
                           offsetof(RPERF_MODULE_RECORD_V1, DebugIdBytes),
                           DebugBytes);
            CopyMemory((UCHAR *)Record + sizeof(*Record),
                       Module->DebugId, sizeof(Module->DebugId));
            RperfStoreLe32((UCHAR *)Record + sizeof(*Record) +
                           sizeof(Module->DebugId), Module->DebugAge);
        }
        Result = RperfAppendChunkRecord(Output, RPERF_CHUNK_MODULE_TABLE,
                                        (const UCHAR *)Record, Size);
        HeapFree(GetProcessHeap(), 0, Record);
        if (!Result)
            return FALSE;
        Output->NextSequence++;
    }
    return TRUE;
}

static BOOL
RperfAppendSymbolRecords(RPERF_V2_OUTPUT *Output,
                         const RPERF_RECORDING *Recording,
                         const ULONG *NameStringIds,
                         const ULONG *FileStringIds)
{
    SIZE_T Index;

    for (Index = 0; Index < Recording->SymbolCount; ++Index)
    {
        const RPERF_MODEL_SYMBOL *Symbol = &Recording->Symbols[Index];
        UCHAR Bytes[sizeof(RPERF_SYMBOL_RECORD_V1)];
        ULONGLONG Address = Symbol->FunctionAddress != 0 ?
                            Symbol->FunctionAddress : Symbol->Address;
        ULONG ModuleId = RperfOutputModuleId(Output, Symbol->ModuleId);
        RPERF_STATUS Status;

        if (Index >= MAXDWORD)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                       RPERF_RECORD_SYMBOL, 1, 0,
                                       sizeof(Bytes),
                                       Output->NextSequence, 0);
        if (Status != RPERF_OK)
            return RperfCodecFailStatus(Status, NULL);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SYMBOL_RECORD_V1, SymbolId),
                       (ULONG)Index + 1);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SYMBOL_RECORD_V1, ModuleId), ModuleId);
        RperfStoreLe64(Bytes + offsetof(RPERF_SYMBOL_RECORD_V1, Address),
                       Address);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SYMBOL_RECORD_V1, NameStringId),
                       NameStringIds != NULL ? NameStringIds[Index] : 0);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SYMBOL_RECORD_V1, FileStringId),
                       FileStringIds != NULL ? FileStringIds[Index] : 0);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SYMBOL_RECORD_V1, LineNumber),
                       Symbol->SourceLine);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_SYMBOL_RECORD_V1, SymbolFlags),
                       (Symbol->Resolution &
                        RPERF_SYMBOL_FLAG_RESOLUTION_MASK) |
                       ((Symbol->Source <<
                         RPERF_SYMBOL_FLAG_SOURCE_SHIFT) &
                        RPERF_SYMBOL_FLAG_SOURCE_MASK) |
                       ((Symbol->Status <<
                         RPERF_SYMBOL_FLAG_STATUS_SHIFT) &
                        RPERF_SYMBOL_FLAG_STATUS_MASK));
        if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_SYMBOL_TABLE,
                                    Bytes, sizeof(Bytes)))
            return FALSE;
        Output->NextSequence++;
    }
    return TRUE;
}

static BOOL
RperfAppendProcessEvent(RPERF_V2_OUTPUT *Output,
                        const RPERF_RECORD *Source)
{
    UCHAR Bytes[sizeof(RPERF_PROCESS_RECORD_V1)];
    RPERF_STATUS Status;

    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_PROCESS, 1,
                                   RperfOutputRecordFlags(Source),
                                   sizeof(Bytes), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    RperfStoreLe32(Bytes + offsetof(RPERF_PROCESS_RECORD_V1, Event),
                   Source->Header.Kind == RperfRecordProcessEnd ?
                   RPERF_LIFECYCLE_END : RPERF_LIFECYCLE_START);
    RperfStoreLe32(Bytes + offsetof(RPERF_PROCESS_RECORD_V1, ProcessId),
                   Source->Header.ProcessId);
    if (Source->Data.Lifecycle.ParentId <= MAXDWORD)
    {
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_PROCESS_RECORD_V1, ParentProcessId),
                       (ULONG)Source->Data.Lifecycle.ParentId);
    }
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_PROCESS_RECORD_V1, UniqueProcessKey),
                   Source->Header.ProcessKey != 0 ?
                   Source->Header.ProcessKey :
                   Source->Data.Lifecycle.ObjectId);
    RperfStoreLe32(Bytes + offsetof(RPERF_PROCESS_RECORD_V1, ExitStatus),
                   Source->Data.Lifecycle.ExitStatus);
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_PROCESS,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendThreadEvent(RPERF_V2_OUTPUT *Output,
                       const RPERF_RECORD *Source)
{
    UCHAR Bytes[sizeof(RPERF_THREAD_RECORD_V1)];
    RPERF_STATUS Status;

    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_THREAD, 1,
                                   RperfOutputRecordFlags(Source),
                                   sizeof(Bytes), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    RperfStoreLe32(Bytes + offsetof(RPERF_THREAD_RECORD_V1, Event),
                   Source->Header.Kind == RperfRecordThreadEnd ?
                   RPERF_LIFECYCLE_END : RPERF_LIFECYCLE_START);
    RperfStoreLe32(Bytes + offsetof(RPERF_THREAD_RECORD_V1, ProcessId),
                   Source->Header.ProcessId);
    RperfStoreLe32(Bytes + offsetof(RPERF_THREAD_RECORD_V1, ThreadId),
                   Source->Header.ThreadId);
    RperfStoreLe32(Bytes + offsetof(RPERF_THREAD_RECORD_V1, ExitStatus),
                   Source->Data.Lifecycle.ExitStatus);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_THREAD_RECORD_V1, UniqueThreadKey),
                   Source->Header.ThreadKey != 0 ?
                   Source->Header.ThreadKey :
                   Source->Data.Lifecycle.ObjectId);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_THREAD_RECORD_V1, UniqueProcessKey),
                   Source->Header.ProcessKey);
    RperfStoreLe64(Bytes + offsetof(RPERF_THREAD_RECORD_V1, StartAddress),
                   Source->Data.Lifecycle.ImageBase);
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_THREAD,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendImageEvent(RPERF_V2_OUTPUT *Output,
                      const RPERF_RECORDING *Recording,
                      const RPERF_RECORD *Source,
                      const ULONG *PathStringIds)
{
    UCHAR Bytes[sizeof(RPERF_IMAGE_RECORD_V1)];
    ULONG ModuleId = RperfOutputModuleId(Output,
                                         Source->Data.Lifecycle.ModuleId);
    ULONG ModuleIndex = RperfOutputModuleIndex(
        Recording, Source->Data.Lifecycle.ModuleId);
    const RPERF_MODULE *Module = ModuleIndex != MAXDWORD ?
                                 &Recording->Modules[ModuleIndex] : NULL;
    RPERF_STATUS Status;

    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_IMAGE, 1,
                                   RperfOutputRecordFlags(Source),
                                   sizeof(Bytes), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    RperfStoreLe32(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, Event),
                   Source->Header.Kind == RperfRecordImageUnload ?
                   RPERF_IMAGE_UNLOAD : RPERF_IMAGE_LOAD);
    RperfStoreLe32(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ProcessId),
                   Source->Header.ProcessId);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_IMAGE_RECORD_V1, UniqueProcessKey),
                   Source->Header.ProcessKey);
    RperfStoreLe64(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ImageBase),
                   Source->Data.Lifecycle.ImageBase);
    RperfStoreLe64(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ImageSize),
                   Source->Data.Lifecycle.ImageSize);
    RperfStoreLe32(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ImageId),
                   ModuleId);
    RperfStoreLe32(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ModuleId),
                   ModuleId);
    if (ModuleIndex != MAXDWORD && PathStringIds != NULL)
    {
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_IMAGE_RECORD_V1, PathStringId),
                       PathStringIds[ModuleIndex]);
    }
    if (Module != NULL)
    {
        RperfStoreLe32(Bytes + offsetof(RPERF_IMAGE_RECORD_V1, Checksum),
                       Module->Checksum);
        RperfStoreLe32(Bytes +
                       offsetof(RPERF_IMAGE_RECORD_V1, TimeDateStamp),
                       Module->TimeDateStamp);
    }
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_IMAGE,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendSampleEvent(RPERF_V2_OUTPUT *Output,
                       const RPERF_RECORDING *Recording,
                       const RPERF_RECORD *Source)
{
    RPERF_SAMPLE_RECORD_V1 *Record;
    ULONG Count = Source->Data.Sample.Depth;
    ULONG Size, Index, UserDepth = 0, KernelDepth;
    ULONG Flags = RperfOutputRecordFlags(Source);
    ULONG SampleFlags = 0;
    BOOL KernelSeen = FALSE;
    RPERF_STATUS Status;
    BOOL Result;

    if (Count == 0 || Count > RPERF_MAX_CALLCHAIN_DEPTH ||
        Count > Recording->Limits.MaxFrames ||
        Count > (MAXDWORD - sizeof(*Record)) / sizeof(ULONGLONG))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    for (Index = 0; Index < Count; ++Index)
    {
        RPERF_CONTEXT_KIND Context =
            Source->Data.Sample.Frames[Index].Context;
        if (Context == RperfContextKernel)
            KernelSeen = TRUE;
        else if (Context == RperfContextUser && KernelSeen)
        {
            SetLastError(ERROR_BAD_FORMAT);
            return FALSE;
        }
        if (!KernelSeen)
            UserDepth++;
    }
    KernelDepth = Count - UserDepth;
    if (UserDepth != 0)
        Flags |= RPERF_RECORD_FLAG_USER;
    if (KernelDepth != 0)
        Flags |= RPERF_RECORD_FLAG_KERNEL;
    Size = RperfSdkRecordSize(sizeof(*Record),
                              Count * sizeof(ULONGLONG));
    if (Size == 0 || Size > Recording->Limits.MaxRecordBytes)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    Record = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Record == NULL)
        return FALSE;
    Status = RperfInitializeRecord(Record, Size,
                                   RPERF_RECORD_SAMPLE, 1, Flags,
                                   sizeof(*Record), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
    {
        HeapFree(GetProcessHeap(), 0, Record);
        return RperfCodecFailStatus(Status, NULL);
    }
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, ProcessId),
                   Source->Header.ProcessId);
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, ThreadId),
                   Source->Header.ThreadId);
    RperfStoreLe16((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, ProcessorNumber),
                   Source->Header.Cpu <= 0xffff ?
                   (USHORT)Source->Header.Cpu : 0xffff);
    RperfStoreLe16((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, UserDepth),
                   (USHORT)UserDepth);
    RperfStoreLe16((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, KernelDepth),
                   (USHORT)KernelDepth);
    RperfStoreLe16((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, CallchainEncoding),
                   RPERF_CALLCHAIN_ADDRESS64);
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, SourceId),
                   RperfOutputSourceId(Output, Source->Header.EventId));
    if (Source->Header.Flags & RPERF_MODEL_RECORD_FLAG_TRUNCATED)
        SampleFlags |= RPERF_SAMPLE_FLAG_STACK_TRUNCATED;
    if (Source->Header.Flags & RPERF_MODEL_RECORD_FLAG_UNWIND_FAILED)
        SampleFlags |= RPERF_SAMPLE_FLAG_UNWIND_FAILED;
    if (Source->Header.Flags & RPERF_MODEL_RECORD_FLAG_INCOMPLETE)
        SampleFlags |= RPERF_SAMPLE_FLAG_INCOMPLETE;
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, SampleFlags),
                   SampleFlags);
    RperfStoreLe64((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, Weight),
                   Source->Data.Sample.Weight != 0 ?
                   Source->Data.Sample.Weight : 1);
    RperfStoreLe64((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, InstructionPointer),
                   Source->Data.Sample.Frames[0].Address);
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, CallchainOffset),
                   sizeof(*Record));
    RperfStoreLe32((UCHAR *)Record +
                   offsetof(RPERF_SAMPLE_RECORD_V1, CallchainCount), Count);
    for (Index = 0; Index < Count; ++Index)
    {
        RperfStoreLe64((UCHAR *)Record + sizeof(*Record) +
                       Index * sizeof(ULONGLONG),
                       Source->Data.Sample.Frames[Index].Address);
    }
    Result = RperfAppendChunkRecord(Output, RPERF_CHUNK_SAMPLE,
                                    (const UCHAR *)Record, Size);
    HeapFree(GetProcessHeap(), 0, Record);
    if (!Result)
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendSchedulerEvent(RPERF_V2_OUTPUT *Output,
                          const RPERF_RECORD *Source)
{
    UCHAR Bytes[sizeof(RPERF_SCHEDULER_RECORD_V1)];
    RPERF_STATUS Status;
    ULONG Event;

    Event = Source->Header.Kind == RperfRecordWakeup ?
            RPERF_SCHED_WAKEUP : RPERF_SCHED_CONTEXT_SWITCH;
    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_SCHEDULER, 1,
                                   RperfOutputRecordFlags(Source),
                                   sizeof(Bytes), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    RperfStoreLe32(Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, Event), Event);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, SchedulerFlags),
                   Source->Data.Scheduler.Flags);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, OldProcessId),
                   Source->Data.Scheduler.OldProcessId);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, OldThreadId),
                   Source->Data.Scheduler.OldThreadId);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, NewProcessId),
                   Source->Data.Scheduler.NewProcessId);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, NewThreadId),
                   Source->Data.Scheduler.NewThreadId);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, OldThreadState),
                   Source->Data.Scheduler.State);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, OldWaitReason),
                   Source->Data.Scheduler.Reason);
    RperfStoreLe16(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, SourceProcessor),
                   Source->Header.Cpu <= 0xffff ?
                   (USHORT)Source->Header.Cpu : 0xffff);
    RperfStoreLe16(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, TargetProcessor),
                   Source->Data.Scheduler.TargetCpu <= 0xffff ?
                   (USHORT)Source->Data.Scheduler.TargetCpu : 0xffff);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_SCHEDULER_RECORD_V1, WaitDuration),
                   Source->Data.Scheduler.DurationNs);
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_SCHEDULER,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static ULONG
RperfOutputLossReason(RPERF_LOSS_REASON Reason)
{
    switch (Reason)
    {
        case RperfLossBufferFull: return RPERF_LOSS_RING_FULL;
        case RperfLossAllocation: return RPERF_LOSS_ALLOCATION;
        case RperfLossUnsafeUnwind: return RPERF_LOSS_STACK_WALK;
        case RperfLossDisabledEvent: return RPERF_LOSS_THROTTLED;
        case RperfLossSequenceGap: return RPERF_LOSS_GAP_IN_INPUT;
        default: return RPERF_LOSS_CORRUPT_RECORD;
    }
}

static BOOL
RperfAppendLossEvent(RPERF_V2_OUTPUT *Output,
                     const RPERF_RECORD *Source)
{
    UCHAR Bytes[sizeof(RPERF_LOSS_RECORD_V1)];
    RPERF_STATUS Status;

    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_LOSS, 1,
                                   RperfOutputRecordFlags(Source),
                                   sizeof(Bytes), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    RperfStoreLe32(Bytes + offsetof(RPERF_LOSS_RECORD_V1, Reason),
                   RperfOutputLossReason(Source->Data.Lost.Reason));
    RperfStoreLe64(Bytes + offsetof(RPERF_LOSS_RECORD_V1, LostRecords),
                   Source->Data.Lost.Count);
    RperfStoreLe64(Bytes + offsetof(RPERF_LOSS_RECORD_V1, LostBytes),
                   Source->Data.Lost.Weight);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_LOSS_RECORD_V1, FirstLostSequence),
                   Source->Data.Lost.FirstSequence);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_LOSS_RECORD_V1, LastLostSequence),
                   Source->Data.Lost.LastSequence);
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_LOSS,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendClockEvent(RPERF_V2_OUTPUT *Output,
                      const RPERF_RECORD *Source)
{
    UCHAR Bytes[sizeof(RPERF_CLOCK_SYNC_RECORD_V1)];
    RPERF_STATUS Status;

    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_CLOCK_SYNC, 1,
                                   RperfOutputRecordFlags(Source),
                                   sizeof(Bytes), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_CLOCK_SYNC_RECORD_V1, SystemTime100ns),
                   Source->Data.Clock.SystemTime100ns);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_CLOCK_SYNC_RECORD_V1, PerformanceCounter),
                   Source->Data.Clock.PerformanceCounter);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_CLOCK_SYNC_RECORD_V1,
                            PerformanceFrequency),
                   Source->Data.Clock.PerformanceFrequency);
    RperfStoreLe64(Bytes +
                   offsetof(RPERF_CLOCK_SYNC_RECORD_V1,
                            InterruptTime100ns),
                   Source->Data.Clock.InterruptTime100ns);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_CLOCK_SYNC_RECORD_V1, ClockFlags),
                   Source->Data.Clock.Flags);
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_CLOCK_SYNC,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendCounterEvent(RPERF_V2_OUTPUT *Output,
                        const RPERF_RECORD *Source)
{
    UCHAR Bytes[sizeof(RPERF_COUNTER_RECORD_V1)];
    RPERF_STATUS Status;

    Status = RperfInitializeRecord(Bytes, sizeof(Bytes),
                                   RPERF_RECORD_COUNTER, 1,
                                   RperfOutputRecordFlags(Source),
                                   sizeof(Bytes), Output->NextSequence,
                                   Source->Header.TimestampNs);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, NULL);
    RperfStoreLe32(Bytes + offsetof(RPERF_COUNTER_RECORD_V1, ProcessId),
                   Source->Header.ProcessId);
    RperfStoreLe32(Bytes + offsetof(RPERF_COUNTER_RECORD_V1, ThreadId),
                   Source->Header.ThreadId);
    RperfStoreLe32(Bytes +
                   offsetof(RPERF_COUNTER_RECORD_V1, ProcessorNumber),
                   Source->Header.Cpu);
    RperfStoreLe32(Bytes + offsetof(RPERF_COUNTER_RECORD_V1, SourceId),
                   RperfOutputSourceId(Output, Source->Header.EventId));
    RperfStoreLe64(Bytes + offsetof(RPERF_COUNTER_RECORD_V1, Value),
                   Source->Data.Sample.Weight);
    RperfStoreLe64(Bytes + offsetof(RPERF_COUNTER_RECORD_V1, Period),
                   Source->Data.Sample.Period);
    if (Source->Data.Sample.Depth != 0)
    {
        RperfStoreLe64(Bytes +
                       offsetof(RPERF_COUNTER_RECORD_V1,
                                InstructionPointer),
                       Source->Data.Sample.Frames[0].Address);
    }
    if (!RperfAppendChunkRecord(Output, RPERF_CHUNK_COUNTER,
                                Bytes, sizeof(Bytes)))
        return FALSE;
    Output->NextSequence++;
    return TRUE;
}

static BOOL
RperfAppendEventRecords(RPERF_V2_OUTPUT *Output,
                        const RPERF_RECORDING *Recording,
                        const ULONG *PathStringIds,
                        BOOL ImagesOnly,
                        HANDLE CancelEvent,
                        RPERF_CODEC_PROGRESS Progress,
                        PVOID ProgressContext)
{
    SIZE_T Index;

    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        const RPERF_RECORD *Record = &Recording->Records[Index];
        BOOL Result = TRUE;

        if ((Index & 1023) == 0)
        {
            if (RperfCodecCancelled(CancelEvent))
            {
                SetLastError(ERROR_CANCELLED);
                return FALSE;
            }
            if (Progress != NULL)
                Progress(ProgressContext, Index, Recording->RecordCount);
        }
        if (ImagesOnly && Record->Header.Kind != RperfRecordImageLoad && Record->Header.Kind != RperfRecordImageUnload)
            continue;
        switch (Record->Header.Kind)
        {
            case RperfRecordProcessStart:
            case RperfRecordProcessEnd:
                Result = RperfAppendProcessEvent(Output, Record);
                break;
            case RperfRecordThreadStart:
            case RperfRecordThreadEnd:
                Result = RperfAppendThreadEvent(Output, Record);
                break;
            case RperfRecordImageLoad:
            case RperfRecordImageUnload:
                Result = RperfAppendImageEvent(Output, Recording, Record,
                                               PathStringIds);
                break;
            case RperfRecordSample:
                Result = RperfAppendSampleEvent(Output, Recording, Record);
                break;
            case RperfRecordContextSwitch:
            case RperfRecordWakeup:
                Result = RperfAppendSchedulerEvent(Output, Record);
                break;
            case RperfRecordPmu:
                Result = RperfAppendCounterEvent(Output, Record);
                break;
            case RperfRecordLost:
                Result = RperfAppendLossEvent(Output, Record);
                break;
            case RperfRecordClockSync:
                Result = RperfAppendClockEvent(Output, Record);
                break;
            case RperfRecordSessionInfo:
            case RperfRecordSecurity:
            case RperfRecordSessionEnd:
                break;
            default:
                SetLastError(ERROR_BAD_FORMAT);
                return FALSE;
        }
        if (!Result)
            return FALSE;
        if (Output->NextSequence == 0)
        {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
RperfV2WriteTail(RPERF_SDK_FILE *File,
                 RPERF_V2_OUTPUT *Output,
                 const RPERF_RECORDING *Recording,
                 BOOL ImagesOnly,
                 HANDLE CancelEvent,
                 RPERF_CODEC_PROGRESS Progress,
                 PVOID ProgressContext);

static BOOL
RperfSaveV2(PCWSTR Path,
            const RPERF_RECORDING *Recording,
            HANDLE CancelEvent,
            RPERF_CODEC_PROGRESS Progress,
            PVOID ProgressContext)
{
    RPERF_SDK_FILE File;
    RPERF_V2_OUTPUT Output;
    RPERF_WRITER_OPTIONS Options;
    FILETIME SystemTime;
    ULARGE_INTEGER Created;
    RPERF_STATUS Status;
    BOOL Result = FALSE;

    ZeroMemory(&File, sizeof(File));
    ZeroMemory(&Output, sizeof(Output));
    File.File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                            NULL);
    if (File.File == INVALID_HANDLE_VALUE)
        return FALSE;
    File.CancelEvent = CancelEvent;

    if (Recording->Limits.MaxRecordBytes == 0 ||
        Recording->Limits.MaxRecordBytes > RPERF_HARD_MAX_RECORD_BYTES)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        goto Cleanup;
    }
    ZeroMemory(&Options, sizeof(Options));
    Options.Size = sizeof(Options);
    CopyMemory(Options.SessionId, &Recording->Info.SessionId,
               sizeof(Options.SessionId));
    GetSystemTimeAsFileTime(&SystemTime);
    Created.LowPart = SystemTime.dwLowDateTime;
    Created.HighPart = SystemTime.dwHighDateTime;
    Options.CreatedSystemTime100ns = Created.QuadPart;
    Options.TimestampFrequency = RPERF_CODEC_TIMESTAMP_FREQUENCY;
    Options.MaximumChunkBytes = max(RPERF_CODEC_CHUNK_TARGET,
                                    Recording->Limits.MaxRecordBytes);
    Status = RperfWriterInitialize(&Output.Writer, RperfSdkWriteAt,
                                   &File, &Options);
    if (Status != RPERF_OK)
    {
        RperfCodecFailStatus(Status, &File);
        goto Cleanup;
    }
    Output.ChunkTarget = min(Options.MaximumChunkBytes,
                             RPERF_CODEC_CHUNK_TARGET);
    Output.NextSequence = 1;
    if (!RperfCollectOutputMaps(&Output, Recording))
        goto Cleanup;

    Result = RperfV2WriteTail(&File, &Output, Recording, FALSE, CancelEvent, Progress, ProgressContext);

Cleanup:
    RperfDestroyV2Output(&Output);
    if (!CloseHandle(File.File) && Result)
        Result = FALSE;
    if (!Result && GetLastError() == ERROR_SUCCESS)
        SetLastError(File.Error != ERROR_SUCCESS ?
                     File.Error : ERROR_WRITE_FAULT);
    return Result;
}

static BOOL
RperfV2WriteTail(RPERF_SDK_FILE *File,
                 RPERF_V2_OUTPUT *Output,
                 const RPERF_RECORDING *Recording,
                 BOOL ImagesOnly,
                 HANDLE CancelEvent,
                 RPERF_CODEC_PROGRESS Progress,
                 PVOID ProgressContext)
{
    ULONG *ModulePathIds = NULL;
    ULONG *SymbolNameIds = NULL;
    ULONG *SymbolFileIds = NULL;
    ULONG TargetStringId = 0, SourceStringId = 0, NextStringId = 0;
    SIZE_T Index;
    RPERF_STATUS Status;
    BOOL Result = FALSE;

    if (Recording->ModuleCount != 0)
    {
        if (Recording->ModuleCount > ((SIZE_T)-1) / sizeof(ULONG))
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            goto Cleanup;
        }
        ModulePathIds = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            Recording->ModuleCount * sizeof(*ModulePathIds));
        if (ModulePathIds == NULL)
            goto Cleanup;
    }
    if (Recording->SymbolCount != 0)
    {
        if (Recording->SymbolCount > ((SIZE_T)-1) / sizeof(ULONG))
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            goto Cleanup;
        }
        SymbolNameIds = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            Recording->SymbolCount * sizeof(*SymbolNameIds));
        SymbolFileIds = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            Recording->SymbolCount * sizeof(*SymbolFileIds));
        if (SymbolNameIds == NULL || SymbolFileIds == NULL)
            goto Cleanup;
    }

    if (Recording->Info.TargetName != NULL &&
        *Recording->Info.TargetName != UNICODE_NULL)
    {
        PSTR Utf8;
        ULONG Length;

        Utf8 = RperfWideToUtf8(Recording->Info.TargetName,
                               Recording->Limits.MaxStringBytes, &Length);
        if (Utf8 == NULL ||
            !RperfAppendStringRecord(Output, Utf8, Length,
                                     Recording->Limits.MaxRecordBytes,
                                     &NextStringId, &TargetStringId))
        {
            if (Utf8 != NULL)
                HeapFree(GetProcessHeap(), 0, Utf8);
            goto Cleanup;
        }
        HeapFree(GetProcessHeap(), 0, Utf8);
    }
    if (Recording->Info.SourcePath != NULL &&
        *Recording->Info.SourcePath != UNICODE_NULL)
    {
        PSTR Utf8;
        ULONG Length;

        Utf8 = RperfWideToUtf8(Recording->Info.SourcePath,
                               Recording->Limits.MaxStringBytes, &Length);
        if (Utf8 == NULL ||
            !RperfAppendStringRecord(Output, Utf8, Length,
                                     Recording->Limits.MaxRecordBytes,
                                     &NextStringId, &SourceStringId))
        {
            if (Utf8 != NULL)
                HeapFree(GetProcessHeap(), 0, Utf8);
            goto Cleanup;
        }
        HeapFree(GetProcessHeap(), 0, Utf8);
    }
    for (Index = 0; Index < Recording->ModuleCount; ++Index)
    {
        const RPERF_MODULE *Module = &Recording->Modules[Index];
        if (Module->Path != NULL && *Module->Path != UNICODE_NULL)
        {
            PSTR Utf8;
            ULONG Length;

            Utf8 = RperfWideToUtf8(Module->Path,
                                   Recording->Limits.MaxStringBytes,
                                   &Length);
            if (Utf8 == NULL ||
                !RperfAppendStringRecord(Output, Utf8, Length,
                                         Recording->Limits.MaxRecordBytes,
                                         &NextStringId,
                                         &ModulePathIds[Index]))
            {
                if (Utf8 != NULL)
                    HeapFree(GetProcessHeap(), 0, Utf8);
                goto Cleanup;
            }
            HeapFree(GetProcessHeap(), 0, Utf8);
        }
    }
    for (Index = 0; Index < Recording->SymbolCount; ++Index)
    {
        const RPERF_MODEL_SYMBOL *Symbol = &Recording->Symbols[Index];
        SIZE_T Length;

        if (Symbol->Name != NULL && *Symbol->Name != ANSI_NULL)
        {
            Length = strlen(Symbol->Name);
            if (Length > MAXDWORD ||
                !RperfAppendStringRecord(Output, Symbol->Name,
                                         (ULONG)Length,
                                         Recording->Limits.MaxRecordBytes,
                                         &NextStringId,
                                         &SymbolNameIds[Index]))
                goto Cleanup;
        }
        if (Symbol->SourceFile != NULL &&
            *Symbol->SourceFile != ANSI_NULL)
        {
            Length = strlen(Symbol->SourceFile);
            if (Length > MAXDWORD ||
                !RperfAppendStringRecord(Output, Symbol->SourceFile,
                                         (ULONG)Length,
                                         Recording->Limits.MaxRecordBytes,
                                         &NextStringId,
                                         &SymbolFileIds[Index]))
                goto Cleanup;
        }
    }

    if (!RperfAppendSessionRecord(Output, Recording,
                                  TargetStringId, SourceStringId) ||
        !RperfAppendSourceRecords(Output) ||
        !RperfFlushChunkType(Output, RPERF_CHUNK_SESSION) ||
        !RperfFlushChunkType(Output, RPERF_CHUNK_SOURCE_TABLE) ||
        !RperfFlushChunkType(Output, RPERF_CHUNK_STRING_TABLE) ||
        !RperfAppendModuleRecords(Output, Recording, ModulePathIds) ||
        !RperfAppendSymbolRecords(Output, Recording,
                                  SymbolNameIds, SymbolFileIds) ||
        !RperfFlushChunkType(Output, RPERF_CHUNK_MODULE_TABLE) ||
        !RperfFlushChunkType(Output, RPERF_CHUNK_SYMBOL_TABLE) ||
        !RperfAppendEventRecords(Output, Recording, ModulePathIds, ImagesOnly,
                                 CancelEvent, Progress, ProgressContext))
        goto Cleanup;
    for (Index = RPERF_CHUNK_PROCESS;
         Index <= RPERF_CHUNK_CLOCK_SYNC;
         ++Index)
    {
        if (!RperfFlushChunkType(Output, (USHORT)Index))
            goto Cleanup;
    }
    if (RperfCodecCancelled(CancelEvent))
    {
        SetLastError(ERROR_CANCELLED);
        goto Cleanup;
    }
    Status = RperfWriterFinalize(&Output->Writer,
                                 (int32_t)(Recording->Info.CompletionError != 0 ?
                                     Recording->Info.CompletionError :
                                     !Recording->Info.Complete ?
                                     ERROR_HANDLE_EOF : ERROR_SUCCESS),
                                 0,
                                 Recording->Counters.LostRecords);
    if (Status != RPERF_OK)
    {
        RperfCodecFailStatus(Status, File);
        goto Cleanup;
    }
    if (!FlushFileBuffers(File->File))
        goto Cleanup;
    if (Progress != NULL)
        Progress(ProgressContext, Recording->RecordCount,
                 Recording->RecordCount);
    Result = TRUE;

Cleanup:
    if (ModulePathIds != NULL)
        HeapFree(GetProcessHeap(), 0, ModulePathIds);
    if (SymbolNameIds != NULL)
        HeapFree(GetProcessHeap(), 0, SymbolNameIds);
    if (SymbolFileIds != NULL)
        HeapFree(GetProcessHeap(), 0, SymbolFileIds);
    return Result;
}

struct _RPERF_CODEC_STREAM
{
    RPERF_SDK_FILE File;
    RPERF_V2_OUTPUT Output;
    RPERF_RECORDING LimitsShim;
    BOOL Failed;
    DWORD Error;
    ULONGLONG StreamedRecords;
};

static VOID
RperfCodecStreamFail(RPERF_CODEC_STREAM *Stream)
{
    if (Stream->Failed) return;
    Stream->Failed = TRUE;
    Stream->Error = GetLastError() != ERROR_SUCCESS ? GetLastError() : ERROR_WRITE_FAULT;
}

static BOOL
RperfCodecStreamEnsureSource(RPERF_CODEC_STREAM *Stream, ULONG EventId)
{
    RPERF_V2_OUTPUT *Output = &Stream->Output;
    RPERF_SOURCE_MAP *Grown;
    SIZE_T Index, Position;

    for (Position = 0; Position < Output->SourceCount; Position++)
    {
        if (Output->Sources[Position].EventId == EventId) return TRUE;
        if (Output->Sources[Position].EventId > EventId) break;
    }
    if (Output->SourceCount >= MAXDWORD) return FALSE;
    if (Output->Sources != NULL)
        Grown = HeapReAlloc(GetProcessHeap(), 0, Output->Sources, (Output->SourceCount + 1) * sizeof(*Grown));
    else
        Grown = HeapAlloc(GetProcessHeap(), 0, sizeof(*Grown));
    if (Grown == NULL) return FALSE;
    Output->Sources = Grown;
    for (Index = Output->SourceCount; Index > Position; Index--)
        Output->Sources[Index] = Output->Sources[Index - 1];
    ZeroMemory(&Output->Sources[Position], sizeof(Output->Sources[Position]));
    Output->Sources[Position].EventId = EventId;
    Output->SourceCount++;
    /* The array stays sorted by EventId for the append-time binary search;
     * the id itself is frozen at first sight because already-flushed event
     * chunks reference it.  Uniqueness is all the loader requires. */
    Output->Sources[Position].SourceId = (ULONG)Output->SourceCount;
    return TRUE;
}

RPERF_CODEC_STREAM *
RperfCodecStreamOpen(PCWSTR Path,
                     const RPERF_CAPTURE_LIMITS *Limits)
{
    RPERF_CODEC_STREAM *Stream;
    RPERF_WRITER_OPTIONS Options;
    FILETIME SystemTime;
    ULARGE_INTEGER Created;
    RPERF_STATUS Status;

    if (Path == NULL || Limits == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (Limits->MaxRecordBytes == 0 || Limits->MaxRecordBytes > RPERF_HARD_MAX_RECORD_BYTES)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Stream = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Stream));
    if (Stream == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    Stream->LimitsShim.Limits = *Limits;
    Stream->File.File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (Stream->File.File == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, Stream);
        return NULL;
    }
    ZeroMemory(&Options, sizeof(Options));
    Options.Size = sizeof(Options);
    GetSystemTimeAsFileTime(&SystemTime);
    Created.LowPart = SystemTime.dwLowDateTime;
    Created.HighPart = SystemTime.dwHighDateTime;
    Options.CreatedSystemTime100ns = Created.QuadPart;
    Options.TimestampFrequency = RPERF_CODEC_TIMESTAMP_FREQUENCY;
    Options.MaximumChunkBytes = max(RPERF_CODEC_CHUNK_TARGET, Limits->MaxRecordBytes);
    Status = RperfWriterInitialize(&Stream->Output.Writer, RperfSdkWriteAt, &Stream->File, &Options);
    if (Status != RPERF_OK)
    {
        RperfCodecFailStatus(Status, &Stream->File);
        CloseHandle(Stream->File.File);
        HeapFree(GetProcessHeap(), 0, Stream);
        return NULL;
    }
    Stream->Output.ChunkTarget = min(Options.MaximumChunkBytes, RPERF_CODEC_CHUNK_TARGET);
    Stream->Output.NextSequence = 1;
    return Stream;
}

VOID CALLBACK
RperfCodecStreamSink(PVOID Context,
                     const RPERF_RECORD *Record)
{
    RPERF_CODEC_STREAM *Stream = Context;
    BOOL Result = TRUE;

    if (Stream == NULL || Record == NULL || Stream->Failed) return;
    switch (Record->Header.Kind)
    {
        case RperfRecordProcessStart:
        case RperfRecordProcessEnd:
            Result = RperfAppendProcessEvent(&Stream->Output, Record);
            break;
        case RperfRecordThreadStart:
        case RperfRecordThreadEnd:
            Result = RperfAppendThreadEvent(&Stream->Output, Record);
            break;
        case RperfRecordSample:
            Result = RperfCodecStreamEnsureSource(Stream, Record->Header.EventId);
            if (Result) Result = RperfAppendSampleEvent(&Stream->Output, &Stream->LimitsShim, Record);
            break;
        case RperfRecordContextSwitch:
        case RperfRecordWakeup:
            Result = RperfAppendSchedulerEvent(&Stream->Output, Record);
            break;
        case RperfRecordPmu:
            Result = RperfCodecStreamEnsureSource(Stream, Record->Header.EventId);
            if (Result) Result = RperfAppendCounterEvent(&Stream->Output, Record);
            break;
        case RperfRecordLost:
            Result = RperfAppendLossEvent(&Stream->Output, Record);
            break;
        case RperfRecordClockSync:
            Result = RperfAppendClockEvent(&Stream->Output, Record);
            break;
        default:
            /* Image, session, and security records need tables that only
             * exist once the recording is complete; they are written at
             * finalization from the final recording. */
            return;
    }
    if (!Result)
    {
        RperfCodecStreamFail(Stream);
        return;
    }
    Stream->StreamedRecords++;
}

BOOL
RperfCodecStreamFinalize(RPERF_CODEC_STREAM *Stream,
                         const RPERF_RECORDING *Recording)
{
    SIZE_T Index;
    BOOL Result = FALSE;

    if (Stream == NULL || Recording == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Stream->Failed)
    {
        SetLastError(Stream->Error);
        goto Cleanup;
    }
    /* Source kind and period become known only once the recording is
     * complete; the table chunk has not been flushed yet, so refresh the
     * entries before they are emitted. */
    for (Index = 0; Index < Stream->Output.SourceCount; Index++)
    {
        Stream->Output.Sources[Index].SourceKind = Recording->Info.Metric == RperfMetricEventWeight ? RPERF_SOURCE_PMU : RPERF_SOURCE_TIMER;
        Stream->Output.Sources[Index].Period = Recording->Info.IntervalMs * 1000000ULL;
    }
    if (Stream->Output.SourceCount == 0 && !RperfCodecStreamEnsureSource(Stream, 0))
        goto Cleanup;
    /* The module map is only referenced by the tables and image events
     * written now, so it can be built from the final recording; the source
     * map must NOT be rebuilt because flushed sample chunks already
     * reference the incrementally assigned ids. */
    if (Recording->ModuleCount != 0)
    {
        if (Recording->ModuleCount > MAXDWORD || Recording->ModuleCount > ((SIZE_T)-1) / sizeof(*Stream->Output.Modules))
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            goto Cleanup;
        }
        Stream->Output.Modules = HeapAlloc(GetProcessHeap(), 0, Recording->ModuleCount * sizeof(*Stream->Output.Modules));
        if (Stream->Output.Modules == NULL) goto Cleanup;
        Stream->Output.ModuleCount = Recording->ModuleCount;
        for (Index = 0; Index < Recording->ModuleCount; Index++)
        {
            Stream->Output.Modules[Index].ModelId = Recording->Modules[Index].Id;
            Stream->Output.Modules[Index].DiskId = (ULONG)Index + 1;
        }
        qsort(Stream->Output.Modules, Stream->Output.ModuleCount, sizeof(*Stream->Output.Modules), RperfCompareModuleMap);
    }
    Result = RperfV2WriteTail(&Stream->File, &Stream->Output, Recording, TRUE, NULL, NULL, NULL);

Cleanup:
    RperfDestroyV2Output(&Stream->Output);
    if (!CloseHandle(Stream->File.File) && Result)
        Result = FALSE;
    if (!Result && GetLastError() == ERROR_SUCCESS)
        SetLastError(Stream->File.Error != ERROR_SUCCESS ? Stream->File.Error : ERROR_WRITE_FAULT);
    HeapFree(GetProcessHeap(), 0, Stream);
    return Result;
}

VOID
RperfCodecStreamAbort(RPERF_CODEC_STREAM *Stream)
{
    if (Stream == NULL) return;
    RperfDestroyV2Output(&Stream->Output);
    CloseHandle(Stream->File.File);
    HeapFree(GetProcessHeap(), 0, Stream);
}

static VOID
RperfDestroyV2Input(RPERF_V2_INPUT *Input)
{
    SIZE_T Index;

    for (Index = 0; Index < Input->StringCount; ++Index)
    {
        if (Input->Strings[Index].Text != NULL)
            HeapFree(GetProcessHeap(), 0, Input->Strings[Index].Text);
    }
    if (Input->Strings != NULL)
        HeapFree(GetProcessHeap(), 0, Input->Strings);
    if (Input->Sources != NULL)
        HeapFree(GetProcessHeap(), 0, Input->Sources);
    if (Input->Processes != NULL)
        HeapFree(GetProcessHeap(), 0, Input->Processes);
    if (Input->Threads != NULL)
        HeapFree(GetProcessHeap(), 0, Input->Threads);
    if (Input->Aliases != NULL)
        HeapFree(GetProcessHeap(), 0, Input->Aliases);
    if (Input->Recording != NULL)
        RperfRecordingRelease(Input->Recording);
    ZeroMemory(Input, sizeof(*Input));
}

static int __cdecl
RperfCompareStringValue(const void *Left,
                        const void *Right)
{
    const RPERF_STRING_VALUE *A = Left;
    const RPERF_STRING_VALUE *B = Right;

    return A->Id < B->Id ? -1 : A->Id > B->Id ? 1 : 0;
}

static int __cdecl
RperfCompareSourceId(const void *Left,
                     const void *Right)
{
    const RPERF_SOURCE_MAP *A = Left;
    const RPERF_SOURCE_MAP *B = Right;

    return A->SourceId < B->SourceId ? -1 :
           A->SourceId > B->SourceId ? 1 : 0;
}

static int __cdecl
RperfCompareModelSymbolAddress(const void *Left,
                               const void *Right)
{
    const RPERF_MODEL_SYMBOL *A = Left;
    const RPERF_MODEL_SYMBOL *B = Right;

    if (A->Address < B->Address)
        return -1;
    if (A->Address > B->Address)
        return 1;
    if (A->ModuleId < B->ModuleId)
        return -1;
    return A->ModuleId > B->ModuleId ? 1 : 0;
}

static int __cdecl
RperfCompareAliasAddress(const void *Left,
                         const void *Right)
{
    const RPERF_ALIAS_SYMBOL *A = Left;
    const RPERF_ALIAS_SYMBOL *B = Right;

    return RperfCompareModelSymbolAddress(&A->Symbol, &B->Symbol);
}

static int __cdecl
RperfCompareModelRecordSequence(const void *Left,
                                const void *Right)
{
    const RPERF_RECORD *A = Left;
    const RPERF_RECORD *B = Right;

    if (A->Header.Sequence < B->Header.Sequence)
        return -1;
    if (A->Header.Sequence > B->Header.Sequence)
        return 1;
    if (A->Header.TimestampNs < B->Header.TimestampNs)
        return -1;
    if (A->Header.TimestampNs > B->Header.TimestampNs)
        return 1;
    return A->Header.Kind < B->Header.Kind ? -1 :
           A->Header.Kind > B->Header.Kind ? 1 : 0;
}

static PCSTR
RperfInputString(const RPERF_V2_INPUT *Input,
                 ULONG Id)
{
    SIZE_T Low = 0, High = Input->StringCount;

    if (Id == 0)
        return NULL;
    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (Input->Strings[Middle].Id < Id)
            Low = Middle + 1;
        else
            High = Middle;
    }
    if (Low < Input->StringCount && Input->Strings[Low].Id == Id)
        return Input->Strings[Low].Text;
    return NULL;
}

static const RPERF_SOURCE_MAP *
RperfInputSource(const RPERF_V2_INPUT *Input,
                 ULONG SourceId)
{
    SIZE_T Low = 0, High = Input->SourceCount;

    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (Input->Sources[Middle].SourceId < SourceId)
            Low = Middle + 1;
        else
            High = Middle;
    }
    if (Low < Input->SourceCount &&
        Input->Sources[Low].SourceId == SourceId)
        return &Input->Sources[Low];
    return NULL;
}

static BOOL
RperfAddInputString(RPERF_V2_INPUT *Input,
                    ULONG Id,
                    PCSTR Text,
                    ULONG Length)
{
    PSTR Copy;

    if (Id == 0 ||
        Length > Input->Limits.MaxStringBytes ||
        memchr(Text, ANSI_NULL, Length) != NULL)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (!RperfCodecResize((PVOID *)&Input->Strings,
                          sizeof(*Input->Strings),
                          &Input->StringCapacity,
                          Input->StringCount + 1,
                          Input->DiskRecordLimit))
        return FALSE;
    Copy = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Length + 1);
    if (Copy == NULL)
        return FALSE;
    CopyMemory(Copy, Text, Length);
    Copy[Length] = ANSI_NULL;
    Input->Strings[Input->StringCount].Id = Id;
    Input->Strings[Input->StringCount].Text = Copy;
    Input->StringCount++;
    return TRUE;
}

static BOOL
RperfAddInputSource(RPERF_V2_INPUT *Input,
                    ULONG SourceId,
                    ULONG SourceKind,
                    ULONGLONG EventCode,
                    ULONGLONG Period)
{
    RPERF_SOURCE_MAP *Source;

    if (SourceId == 0)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (!RperfCodecResize((PVOID *)&Input->Sources,
                          sizeof(*Input->Sources),
                          &Input->SourceCapacity,
                          Input->SourceCount + 1,
                          Input->DiskRecordLimit))
        return FALSE;
    Source = &Input->Sources[Input->SourceCount++];
    ZeroMemory(Source, sizeof(*Source));
    Source->SourceId = SourceId;
    Source->SourceKind = SourceKind;
    Source->EventId = EventCode <= MAXDWORD ?
                      (ULONG)EventCode : SourceId;
    Source->Period = Period;
    if (SourceKind == RPERF_SOURCE_TIMER)
        Input->TimerSource = TRUE;
    else if (SourceKind == RPERF_SOURCE_PMU)
        Input->PmuSource = TRUE;
    return TRUE;
}

static BOOL
RperfAddLifetime(RPERF_ID_LIFETIME **Lifetimes,
                 SIZE_T *Count,
                 SIZE_T *Capacity,
                 ULONGLONG Maximum,
                 ULONG NumericId,
                 ULONGLONG StableId,
                 ULONGLONG Timestamp,
                 BOOL Start)
{
    SIZE_T Index;

    if (!Start)
    {
        for (Index = *Count; Index != 0; --Index)
        {
            RPERF_ID_LIFETIME *Lifetime = &(*Lifetimes)[Index - 1];
            if (Lifetime->NumericId == NumericId &&
                Lifetime->EndNs == 0 &&
                (StableId == 0 || Lifetime->StableId == StableId))
            {
                Lifetime->EndNs = Timestamp;
                return TRUE;
            }
        }
    }
    if (!RperfCodecResize((PVOID *)Lifetimes, sizeof(**Lifetimes),
                          Capacity, *Count + 1, Maximum))
        return FALSE;
    (*Lifetimes)[*Count].NumericId = NumericId;
    (*Lifetimes)[*Count].StableId = StableId != 0 ? StableId : NumericId;
    (*Lifetimes)[*Count].StartNs = Start ? Timestamp : 0;
    (*Lifetimes)[*Count].EndNs = Start ? 0 : Timestamp;
    (*Count)++;
    return TRUE;
}

static ULONGLONG
RperfLookupLifetime(const RPERF_ID_LIFETIME *Lifetimes,
                    SIZE_T Count,
                    ULONG NumericId,
                    ULONGLONG Timestamp)
{
    SIZE_T Index;
    ULONGLONG Result = NumericId, BestStart = 0;

    for (Index = 0; Index < Count; ++Index)
    {
        const RPERF_ID_LIFETIME *Lifetime = &Lifetimes[Index];
        if (Lifetime->NumericId != NumericId ||
            Lifetime->StartNs > Timestamp ||
            (Lifetime->EndNs != 0 && Timestamp > Lifetime->EndNs))
            continue;
        if (Result == NumericId || Lifetime->StartNs >= BestStart)
        {
            Result = Lifetime->StableId;
            BestStart = Lifetime->StartNs;
        }
    }
    return Result;
}

static ULONG
RperfInputModelFlags(ULONG DiskFlags)
{
    ULONG Flags = 0;

    if (DiskFlags & RPERF_RECORD_FLAG_TRUNCATED)
        Flags |= RPERF_MODEL_RECORD_FLAG_TRUNCATED;
    if (DiskFlags & RPERF_RECORD_FLAG_REDACTED)
        Flags |= RPERF_MODEL_RECORD_FLAG_REDACTED;
    if (DiskFlags & RPERF_RECORD_FLAG_SYNTHETIC)
        Flags |= RPERF_MODEL_RECORD_FLAG_SYNTHETIC;
    return Flags;
}

static ULONG
RperfInputSampleFlags(ULONG SampleFlags)
{
    ULONG Flags = 0;

    if (SampleFlags & RPERF_SAMPLE_FLAG_STACK_TRUNCATED)
        Flags |= RPERF_MODEL_RECORD_FLAG_TRUNCATED;
    if (SampleFlags & RPERF_SAMPLE_FLAG_UNWIND_FAILED)
        Flags |= RPERF_MODEL_RECORD_FLAG_UNWIND_FAILED;
    if (SampleFlags & RPERF_SAMPLE_FLAG_INCOMPLETE)
        Flags |= RPERF_MODEL_RECORD_FLAG_INCOMPLETE;
    return Flags;
}

static VOID
RperfInputCommonHeader(const RPERF_V2_INPUT *Input,
                       const UCHAR *DiskRecord,
                       RPERF_RECORD_HEADER *Header)
{
    ZeroMemory(Header, sizeof(*Header));
    Header->Flags = RperfInputModelFlags(
        RperfLoadLe32(DiskRecord +
                      offsetof(RPERF_RECORD_HEADER_V1, Flags)));
    Header->TimestampNs = RperfCodecTimestampNs(
        RperfLoadLe64(DiskRecord +
                      offsetof(RPERF_RECORD_HEADER_V1, Timestamp)),
        Input->FileHeader.TimestampFrequency);
    Header->Sequence = RperfLoadLe64(
        DiskRecord + offsetof(RPERF_RECORD_HEADER_V1, Sequence));
    Header->Cpu = RPERF_MODEL_ALL_CPUS;
}

static BOOL
RperfParseStringRecord(RPERF_V2_INPUT *Input,
                       const UCHAR *Bytes)
{
    ULONG Id, Offset, Length;
    USHORT Encoding;

    Id = RperfLoadLe32(Bytes +
                       offsetof(RPERF_STRING_RECORD_V1, StringId));
    Encoding = RperfLoadLe16(Bytes +
        offsetof(RPERF_STRING_RECORD_V1, Encoding));
    Offset = RperfLoadLe32(Bytes +
        offsetof(RPERF_STRING_RECORD_V1, DataOffset));
    Length = RperfLoadLe32(Bytes +
        offsetof(RPERF_STRING_RECORD_V1, DataBytes));
    if (Encoding != RPERF_STRING_UTF8)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return RperfAddInputString(Input, Id, (PCSTR)Bytes + Offset, Length);
}

static BOOL
RperfParseSessionRecord(RPERF_V2_INPUT *Input,
                        const UCHAR *Bytes)
{
    ULONG StringId, CaptureStatus;
    ULONGLONG ProducerMetadata;
    PCSTR Target;
    PWSTR WideTarget = NULL;

    if (Input->SessionSeen)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Input->SessionSeen = TRUE;
    CopyMemory(&Input->Recording->Info.SessionId,
               Bytes + offsetof(RPERF_SESSION_RECORD_V1, SessionId),
               sizeof(GUID));
    Input->Recording->Info.StartTimeNs = RperfCodecTimestampNs(
        RperfLoadLe64(Bytes +
            offsetof(RPERF_SESSION_RECORD_V1, CaptureStartTimestamp)),
        Input->FileHeader.TimestampFrequency);
    Input->Recording->Info.EndTimeNs = RperfCodecTimestampNs(
        RperfLoadLe64(Bytes +
            offsetof(RPERF_SESSION_RECORD_V1, CaptureEndTimestamp)),
        Input->FileHeader.TimestampFrequency);
    Input->Recording->Info.ProducerArchitecture = RperfLoadLe32(
        Bytes + offsetof(RPERF_SESSION_RECORD_V1, Architecture));
    Input->Recording->Info.AddressWidth = RperfLoadLe32(
        Bytes + offsetof(RPERF_SESSION_RECORD_V1, PointerWidth));
    Input->Recording->Info.ClockId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SESSION_RECORD_V1, ClockType));
    Input->Recording->Info.ClockFrequency = RperfLoadLe64(
        Bytes + offsetof(RPERF_SESSION_RECORD_V1, TimestampFrequency));
    ProducerMetadata = RperfLoadLe64(
        Bytes + offsetof(RPERF_SESSION_RECORD_V1, Reserved));
    Input->Recording->Info.Backend = (RPERF_BACKEND_KIND)
        (ProducerMetadata & RPERF_SESSION_META_BACKEND_MASK);
    Input->Recording->Info.Metric = (RPERF_METRIC_KIND)
        ((ProducerMetadata & RPERF_SESSION_META_METRIC_MASK) >>
         RPERF_SESSION_META_METRIC_SHIFT);
    if (Input->Recording->Info.Backend < RperfBackendIntrusive ||
        Input->Recording->Info.Backend > RperfBackendEtw)
        Input->Recording->Info.Backend = 0;
    if (Input->Recording->Info.Metric < RperfMetricSnapshotPopulation ||
        Input->Recording->Info.Metric > RperfMetricOffCpuTime)
        Input->Recording->Info.Metric = 0;
    Input->Recording->Info.CompletionReason = RperfInputCompletionReason(
        RperfLoadLe32(
            Bytes + offsetof(RPERF_SESSION_RECORD_V1, StopReason)));
    CaptureStatus = RperfLoadLe32(
        Bytes + offsetof(RPERF_SESSION_RECORD_V1, CaptureStatus));
    Input->Recording->Info.CompletionError = CaptureStatus;
    StringId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SESSION_RECORD_V1, HostNameStringId));
    Target = RperfInputString(Input, StringId);
    if (StringId != 0 && Target == NULL)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (Target != NULL)
    {
        SIZE_T Length = strlen(Target);
        if (Length > MAXDWORD)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        WideTarget = RperfUtf8ToWide(Target, (ULONG)Length,
                                     Input->Limits.MaxStringBytes);
        if (WideTarget == NULL ||
            !RperfRecordingSetTargetName(Input->Recording, WideTarget))
        {
            if (WideTarget != NULL)
                HeapFree(GetProcessHeap(), 0, WideTarget);
            return FALSE;
        }
        HeapFree(GetProcessHeap(), 0, WideTarget);
    }
    return TRUE;
}

static BOOL
RperfParseSourceRecord(RPERF_V2_INPUT *Input,
                       const UCHAR *Bytes)
{
    return RperfAddInputSource(Input,
        RperfLoadLe32(Bytes + offsetof(RPERF_SOURCE_RECORD_V1, SourceId)),
        RperfLoadLe32(Bytes + offsetof(RPERF_SOURCE_RECORD_V1, SourceKind)),
        RperfLoadLe64(Bytes + offsetof(RPERF_SOURCE_RECORD_V1, EventCode)),
        RperfLoadLe64(Bytes +
                      offsetof(RPERF_SOURCE_RECORD_V1, MinimumPeriod)));
}

static PSTR
RperfModuleNameUtf8(const RPERF_MODULE *Module,
                    ULONG MaximumBytes)
{
    PSTR Full, Name, Copy;
    ULONG Length;
    SIZE_T NameLength;

    if (Module == NULL || Module->Path == NULL)
        return NULL;
    Full = RperfWideToUtf8(Module->Path, MaximumBytes, &Length);
    if (Full == NULL)
        return NULL;
    Name = strrchr(Full, '\\');
    if (Name == NULL)
        Name = strrchr(Full, '/');
    Name = Name != NULL ? Name + 1 : Full;
    NameLength = strlen(Name);
    Copy = HeapAlloc(GetProcessHeap(), 0, NameLength + 1);
    if (Copy != NULL)
        CopyMemory(Copy, Name, NameLength + 1);
    HeapFree(GetProcessHeap(), 0, Full);
    return Copy;
}

static BOOL
RperfParseModuleRecord(RPERF_V2_INPUT *Input,
                       const UCHAR *Bytes)
{
    RPERF_MODULE Module;
    ULONG PathId, DebugOffset, DebugBytes;
    PCSTR Path;
    PWSTR WidePath = NULL;

    ZeroMemory(&Module, sizeof(Module));
    Module.Id = RperfLoadLe32(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, ModuleId));
    if (Module.Id == 0 ||
        RperfRecordingFindModule(Input->Recording, Module.Id) != NULL)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Module.ProcessKey = RperfLoadLe64(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, UniqueProcessKey));
    Module.Base = RperfLoadLe64(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, ImageBase));
    Module.Size = RperfLoadLe64(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, ImageSize));
    Module.Flags = RperfLoadLe32(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, ModuleFlags));
    Module.Architecture = Module.Flags & RPERF_MODULE_FLAG_ARCH_MASK;
    if (Module.Architecture == RPERF_ARCH_UNKNOWN)
        Module.Architecture = Input->Recording->Info.ProducerArchitecture;
    Module.Checksum = RperfLoadLe32(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, Checksum));
    Module.TimeDateStamp = RperfLoadLe32(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, TimeDateStamp));
    PathId = RperfLoadLe32(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, PathStringId));
    Path = RperfInputString(Input, PathId);
    if (PathId != 0 && Path == NULL)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (Path != NULL)
    {
        SIZE_T Length = strlen(Path);
        if (Length > MAXDWORD)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        WidePath = RperfUtf8ToWide(Path, (ULONG)Length,
                                   Input->Limits.MaxStringBytes);
        if (WidePath == NULL)
            return FALSE;
        Module.Path = WidePath;
    }
    DebugOffset = RperfLoadLe32(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, DebugIdOffset));
    DebugBytes = RperfLoadLe32(
        Bytes + offsetof(RPERF_MODULE_RECORD_V1, DebugIdBytes));
    if (DebugBytes >= sizeof(Module.DebugId))
        CopyMemory(Module.DebugId, Bytes + DebugOffset,
                   sizeof(Module.DebugId));
    if (DebugBytes >= sizeof(Module.DebugId) + sizeof(ULONG))
        Module.DebugAge = RperfLoadLe32(Bytes + DebugOffset +
                                       sizeof(Module.DebugId));
    if (!RperfRecordingAddModule(Input->Recording, &Module))
    {
        if (WidePath != NULL)
            HeapFree(GetProcessHeap(), 0, WidePath);
        return FALSE;
    }
    if (WidePath != NULL)
        HeapFree(GetProcessHeap(), 0, WidePath);
    return TRUE;
}

static BOOL
RperfParseSymbolRecord(RPERF_V2_INPUT *Input,
                       const UCHAR *Bytes)
{
    RPERF_MODEL_SYMBOL Symbol;
    const RPERF_MODULE *Module;
    ULONG NameId, FileId, Flags;
    PCSTR Name, File;
    PSTR ModuleName = NULL;

    ZeroMemory(&Symbol, sizeof(Symbol));
    Symbol.ModuleId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SYMBOL_RECORD_V1, ModuleId));
    Symbol.Address = RperfLoadLe64(
        Bytes + offsetof(RPERF_SYMBOL_RECORD_V1, Address));
    Symbol.FunctionAddress = Symbol.Address;
    NameId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SYMBOL_RECORD_V1, NameStringId));
    FileId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SYMBOL_RECORD_V1, FileStringId));
    Name = RperfInputString(Input, NameId);
    File = RperfInputString(Input, FileId);
    if ((NameId != 0 && Name == NULL) || (FileId != 0 && File == NULL))
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Symbol.Name = (PSTR)Name;
    Symbol.SourceFile = (PSTR)File;
    Symbol.SourceLine = RperfLoadLe32(
        Bytes + offsetof(RPERF_SYMBOL_RECORD_V1, LineNumber));
    Flags = RperfLoadLe32(
        Bytes + offsetof(RPERF_SYMBOL_RECORD_V1, SymbolFlags));
    Symbol.Resolution =
        (Flags & RPERF_SYMBOL_FLAG_RESOLUTION_MASK) >=
            RperfResolutionAddress &&
        (Flags & RPERF_SYMBOL_FLAG_RESOLUTION_MASK) <=
            RperfResolutionInline ?
        (RPERF_RESOLUTION_KIND)(Flags &
                               RPERF_SYMBOL_FLAG_RESOLUTION_MASK) :
                        File != NULL ? RperfResolutionSource :
                        Name != NULL ? RperfResolutionFunction :
                        RperfResolutionAddress;
    Symbol.Source = (RPERF_SYMBOL_SOURCE_KIND)
        ((Flags & RPERF_SYMBOL_FLAG_SOURCE_MASK) >>
         RPERF_SYMBOL_FLAG_SOURCE_SHIFT);
    Symbol.Status = (RPERF_SYMBOL_STATUS_KIND)
        ((Flags & RPERF_SYMBOL_FLAG_STATUS_MASK) >>
         RPERF_SYMBOL_FLAG_STATUS_SHIFT);
    if (Symbol.Source > RperfSymbolSourceModuleOffset)
        Symbol.Source = RperfSymbolSourceUnknown;
    if (Symbol.Status > RperfSymbolStatusLoadError)
        Symbol.Status = RperfSymbolStatusUnattempted;
    Module = RperfRecordingFindModule(Input->Recording, Symbol.ModuleId);
    if (Module != NULL)
    {
        Symbol.RelativeAddress = Symbol.Address >= Module->Base ?
                                 Symbol.Address - Module->Base :
                                 Symbol.Address;
        ModuleName = RperfModuleNameUtf8(Module,
                                         Input->Limits.MaxStringBytes);
        Symbol.ModuleName = ModuleName;
    }
    if (!RperfRecordingAddSymbol(Input->Recording, &Symbol))
    {
        if (ModuleName != NULL)
            HeapFree(GetProcessHeap(), 0, ModuleName);
        return FALSE;
    }
    if (ModuleName != NULL)
        HeapFree(GetProcessHeap(), 0, ModuleName);
    return TRUE;
}

static BOOL
RperfIndexProcessLifetime(RPERF_V2_INPUT *Input,
                          const UCHAR *Bytes)
{
    ULONG Event = RperfLoadLe32(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, Event));
    ULONG ProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, ProcessId));
    ULONGLONG StableId = RperfLoadLe64(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, UniqueProcessKey));
    ULONGLONG Timestamp = RperfCodecTimestampNs(
        RperfLoadLe64(Bytes + offsetof(RPERF_RECORD_HEADER_V1, Timestamp)),
        Input->FileHeader.TimestampFrequency);

    if (Event != RPERF_LIFECYCLE_START && Event != RPERF_LIFECYCLE_END)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    return RperfAddLifetime(&Input->Processes,
                            &Input->ProcessCount,
                            &Input->ProcessCapacity,
                            Input->Limits.MaxRecords,
                            ProcessId, StableId, Timestamp,
                            Event == RPERF_LIFECYCLE_START);
}

static BOOL
RperfIndexThreadLifetime(RPERF_V2_INPUT *Input,
                         const UCHAR *Bytes)
{
    ULONG Event = RperfLoadLe32(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, Event));
    ULONG ThreadId = RperfLoadLe32(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, ThreadId));
    ULONGLONG StableId = RperfLoadLe64(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, UniqueThreadKey));
    ULONGLONG Timestamp = RperfCodecTimestampNs(
        RperfLoadLe64(Bytes + offsetof(RPERF_RECORD_HEADER_V1, Timestamp)),
        Input->FileHeader.TimestampFrequency);

    if (Event != RPERF_LIFECYCLE_START && Event != RPERF_LIFECYCLE_END)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    return RperfAddLifetime(&Input->Threads,
                            &Input->ThreadCount,
                            &Input->ThreadCapacity,
                            Input->Limits.MaxRecords,
                            ThreadId, StableId, Timestamp,
                            Event == RPERF_LIFECYCLE_START);
}

static const RPERF_MODULE *
RperfInputModuleForAddress(const RPERF_V2_INPUT *Input,
                           ULONGLONG ProcessKey,
                           ULONGLONG Address)
{
    const RPERF_MODULE *Best = NULL;
    SIZE_T Index;

    for (Index = 0; Index < Input->Recording->ModuleCount; ++Index)
    {
        const RPERF_MODULE *Module = &Input->Recording->Modules[Index];
        BOOL Contains;

        if (Module->ProcessKey != 0 && ProcessKey != 0 &&
            Module->ProcessKey != ProcessKey)
            continue;
        Contains = Address >= Module->Base &&
                   (Module->Size == 0 ||
                    Address - Module->Base < Module->Size);
        if (Contains && (Best == NULL || Module->Base > Best->Base))
            Best = Module;
    }
    return Best;
}

static const RPERF_MODEL_SYMBOL *
RperfInputBaseSymbol(const RPERF_V2_INPUT *Input,
                     ULONGLONG ModuleId,
                     ULONGLONG Address)
{
    SIZE_T Low = 0, High = Input->Recording->SymbolCount, Index;
    const RPERF_MODULE *Module =
        RperfRecordingFindModule(Input->Recording, ModuleId);

    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (Input->Recording->Symbols[Middle].Address <= Address)
            Low = Middle + 1;
        else
            High = Middle;
    }
    Index = Low;
    while (Index != 0)
    {
        const RPERF_MODEL_SYMBOL *Symbol =
            &Input->Recording->Symbols[--Index];
        if (Module != NULL && Symbol->Address < Module->Base)
            break;
        if (ModuleId == RPERF_MODEL_INVALID_ID || ModuleId == 0 ||
            Symbol->ModuleId == ModuleId)
            return Symbol;
    }
    return NULL;
}

static BOOL
RperfRememberAlias(RPERF_V2_INPUT *Input,
                   ULONGLONG Address,
                   const RPERF_MODEL_SYMBOL *Base)
{
    RPERF_ALIAS_SYMBOL *Alias;
    ULONGLONG Maximum;

    if (Base == NULL || Address == Base->Address)
        return TRUE;
    Maximum = Input->Limits.MaxSymbols > Input->Recording->SymbolCount ?
              Input->Limits.MaxSymbols - Input->Recording->SymbolCount : 0;
    if ((ULONGLONG)Input->AliasCount >= Maximum)
        return TRUE;
    if (!RperfCodecResize((PVOID *)&Input->Aliases,
                          sizeof(*Input->Aliases),
                          &Input->AliasCapacity,
                          Input->AliasCount + 1,
                          Maximum))
        return FALSE;
    Alias = &Input->Aliases[Input->AliasCount++];
    ZeroMemory(Alias, sizeof(*Alias));
    Alias->Symbol = *Base;
    Alias->Symbol.Address = Address;
    Alias->Symbol.FunctionAddress = Base->Address;
    if (Base->RelativeAddress <= (ULONGLONG)-1 -
                                 (Address - Base->Address))
    {
        Alias->Symbol.RelativeAddress =
            Base->RelativeAddress + (Address - Base->Address);
    }
    return TRUE;
}

static BOOL
RperfPopulateInputFrame(RPERF_V2_INPUT *Input,
                        ULONGLONG ProcessKey,
                        ULONGLONG Address,
                        RPERF_CONTEXT_KIND Context,
                        RPERF_FRAME *Frame)
{
    const RPERF_MODULE *Module;
    const RPERF_MODEL_SYMBOL *Symbol;

    ZeroMemory(Frame, sizeof(*Frame));
    Frame->Address = Address;
    Frame->FunctionAddress = Address;
    Frame->ModuleId = RPERF_MODEL_INVALID_ID;
    Frame->Context = Context;
    Frame->Resolution = RperfResolutionAddress;
    Module = RperfInputModuleForAddress(Input, ProcessKey, Address);
    if (Module != NULL)
        Frame->ModuleId = Module->Id;
    Symbol = RperfInputBaseSymbol(Input, Frame->ModuleId, Address);
    if (Symbol != NULL)
    {
        Frame->FunctionAddress = Symbol->Address;
        Frame->ModuleId = Symbol->ModuleId;
        Frame->Resolution = Symbol->Resolution;
        if (!RperfRememberAlias(Input, Address, Symbol))
            return FALSE;
    }
    return TRUE;
}

static BOOL
RperfParseProcessEvent(RPERF_V2_INPUT *Input,
                       const UCHAR *Bytes)
{
    RPERF_RECORD Record;
    ULONG Event;

    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Event = RperfLoadLe32(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, Event));
    if (Event != RPERF_LIFECYCLE_START && Event != RPERF_LIFECYCLE_END)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Record.Header.Kind = Event == RPERF_LIFECYCLE_START ?
                         RperfRecordProcessStart : RperfRecordProcessEnd;
    Record.Header.ProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, ProcessId));
    Record.Header.ProcessKey = RperfLoadLe64(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, UniqueProcessKey));
    if (Record.Header.ProcessKey == 0)
        Record.Header.ProcessKey = Record.Header.ProcessId;
    Record.Data.Lifecycle.ObjectId = Record.Header.ProcessKey;
    Record.Data.Lifecycle.ParentId = RperfLoadLe32(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, ParentProcessId));
    Record.Data.Lifecycle.ExitStatus = RperfLoadLe32(
        Bytes + offsetof(RPERF_PROCESS_RECORD_V1, ExitStatus));
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static BOOL
RperfParseThreadEvent(RPERF_V2_INPUT *Input,
                      const UCHAR *Bytes)
{
    RPERF_RECORD Record;
    ULONG Event;

    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Event = RperfLoadLe32(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, Event));
    if (Event != RPERF_LIFECYCLE_START && Event != RPERF_LIFECYCLE_END)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Record.Header.Kind = Event == RPERF_LIFECYCLE_START ?
                         RperfRecordThreadStart : RperfRecordThreadEnd;
    Record.Header.ProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, ProcessId));
    Record.Header.ThreadId = RperfLoadLe32(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, ThreadId));
    Record.Header.ProcessKey = RperfLoadLe64(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, UniqueProcessKey));
    if (Record.Header.ProcessKey == 0)
        Record.Header.ProcessKey = RperfLookupLifetime(
            Input->Processes, Input->ProcessCount,
            Record.Header.ProcessId, Record.Header.TimestampNs);
    Record.Header.ThreadKey = RperfLoadLe64(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, UniqueThreadKey));
    if (Record.Header.ThreadKey == 0)
        Record.Header.ThreadKey = Record.Header.ThreadId;
    Record.Data.Lifecycle.ObjectId = Record.Header.ThreadKey;
    Record.Data.Lifecycle.ParentId = Record.Header.ProcessKey;
    Record.Data.Lifecycle.ImageBase = RperfLoadLe64(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, StartAddress));
    Record.Data.Lifecycle.ExitStatus = RperfLoadLe32(
        Bytes + offsetof(RPERF_THREAD_RECORD_V1, ExitStatus));
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static BOOL
RperfParseImageEvent(RPERF_V2_INPUT *Input,
                     const UCHAR *Bytes)
{
    RPERF_RECORD Record;
    ULONG Event;

    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Event = RperfLoadLe32(
        Bytes + offsetof(RPERF_IMAGE_RECORD_V1, Event));
    if (Event != RPERF_IMAGE_LOAD && Event != RPERF_IMAGE_UNLOAD)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Record.Header.Kind = Event == RPERF_IMAGE_LOAD ?
                         RperfRecordImageLoad : RperfRecordImageUnload;
    Record.Header.ProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ProcessId));
    Record.Header.ProcessKey = RperfLoadLe64(
        Bytes + offsetof(RPERF_IMAGE_RECORD_V1, UniqueProcessKey));
    if (Record.Header.ProcessKey == 0)
        Record.Header.ProcessKey = RperfLookupLifetime(
            Input->Processes, Input->ProcessCount,
            Record.Header.ProcessId, Record.Header.TimestampNs);
    Record.Data.Lifecycle.ModuleId = RperfLoadLe32(
        Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ModuleId));
    Record.Data.Lifecycle.ObjectId =
        Record.Data.Lifecycle.ModuleId;
    Record.Data.Lifecycle.ImageBase = RperfLoadLe64(
        Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ImageBase));
    Record.Data.Lifecycle.ImageSize = RperfLoadLe64(
        Bytes + offsetof(RPERF_IMAGE_RECORD_V1, ImageSize));
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static BOOL
RperfParseSampleEvent(RPERF_V2_INPUT *Input,
                      const UCHAR *Bytes)
{
    RPERF_RECORD Record;
    const RPERF_SOURCE_MAP *Source;
    ULONG Offset, Count, SourceId, SampleFlags, Index;
    USHORT UserDepth, KernelDepth, Processor;
    ULONGLONG InstructionPointer;

    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Record.Header.Kind = RperfRecordSample;
    Record.Header.ProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, ProcessId));
    Record.Header.ThreadId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, ThreadId));
    Record.Header.ProcessKey = RperfLookupLifetime(
        Input->Processes, Input->ProcessCount,
        Record.Header.ProcessId, Record.Header.TimestampNs);
    Record.Header.ThreadKey = RperfLookupLifetime(
        Input->Threads, Input->ThreadCount,
        Record.Header.ThreadId, Record.Header.TimestampNs);
    Processor = RperfLoadLe16(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, ProcessorNumber));
    Record.Header.Cpu = Processor == 0xffff ?
                        RPERF_MODEL_ALL_CPUS : Processor;
    SourceId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, SourceId));
    Source = RperfInputSource(Input, SourceId);
    Record.Header.EventId = Source != NULL ? Source->EventId : SourceId;
    SampleFlags = RperfLoadLe32(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, SampleFlags));
    Record.Header.Flags = RperfInputModelFlags(
        RperfLoadLe32(Bytes +
                      offsetof(RPERF_RECORD_HEADER_V1, Flags))) |
        RperfInputSampleFlags(SampleFlags);
    Record.Data.Sample.Weight = RperfLoadLe64(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, Weight));
    if (Record.Data.Sample.Weight == 0)
        Record.Data.Sample.Weight = 1;
    Record.Data.Sample.Period = Source != NULL ? Source->Period : 0;
    Offset = RperfLoadLe32(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, CallchainOffset));
    Count = RperfLoadLe32(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, CallchainCount));
    UserDepth = RperfLoadLe16(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, UserDepth));
    KernelDepth = RperfLoadLe16(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, KernelDepth));
    InstructionPointer = RperfLoadLe64(
        Bytes + offsetof(RPERF_SAMPLE_RECORD_V1, InstructionPointer));
    if (Count == 0 && InstructionPointer != 0)
    {
        Count = 1;
        UserDepth = (RperfLoadLe32(Bytes +
            offsetof(RPERF_RECORD_HEADER_V1, Flags)) &
            RPERF_RECORD_FLAG_KERNEL) == 0;
        KernelDepth = 1 - UserDepth;
    }
    if (Count == 0 || Count > Input->Limits.MaxFrames ||
        Count > RPERF_MODEL_MAX_FRAMES ||
        Count != (ULONG)UserDepth + KernelDepth)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    Record.Data.Sample.Depth = (USHORT)Count;
    for (Index = 0; Index < Count; ++Index)
    {
        ULONGLONG Address = Offset != 0 ?
            RperfLoadLe64(Bytes + Offset + Index * sizeof(ULONGLONG)) :
            InstructionPointer;
        RPERF_CONTEXT_KIND Context = Index < UserDepth ?
            RperfContextUser : RperfContextKernel;
        if (!RperfPopulateInputFrame(Input, Record.Header.ProcessKey,
                                     Address, Context,
                                     &Record.Data.Sample.Frames[Index]))
            return FALSE;
    }
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static BOOL
RperfParseSchedulerEvent(RPERF_V2_INPUT *Input,
                         const UCHAR *Bytes)
{
    RPERF_RECORD Record;
    ULONG Event;

    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Event = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, Event));
    if (Event == RPERF_SCHED_CONTEXT_SWITCH)
        Record.Header.Kind = RperfRecordContextSwitch;
    else if (Event == RPERF_SCHED_READY || Event == RPERF_SCHED_WAKEUP)
        Record.Header.Kind = RperfRecordWakeup;
    else
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Record.Data.Scheduler.Flags = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, SchedulerFlags));
    Record.Data.Scheduler.OldProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, OldProcessId));
    Record.Data.Scheduler.OldThreadId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, OldThreadId));
    Record.Data.Scheduler.NewProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, NewProcessId));
    Record.Data.Scheduler.NewThreadId = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, NewThreadId));
    Record.Data.Scheduler.OldProcessKey = RperfLookupLifetime(
        Input->Processes, Input->ProcessCount,
        Record.Data.Scheduler.OldProcessId, Record.Header.TimestampNs);
    Record.Data.Scheduler.OldThreadKey = RperfLookupLifetime(
        Input->Threads, Input->ThreadCount,
        Record.Data.Scheduler.OldThreadId, Record.Header.TimestampNs);
    Record.Data.Scheduler.NewProcessKey = RperfLookupLifetime(
        Input->Processes, Input->ProcessCount,
        Record.Data.Scheduler.NewProcessId, Record.Header.TimestampNs);
    Record.Data.Scheduler.NewThreadKey = RperfLookupLifetime(
        Input->Threads, Input->ThreadCount,
        Record.Data.Scheduler.NewThreadId, Record.Header.TimestampNs);
    Record.Data.Scheduler.State = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, OldThreadState));
    Record.Data.Scheduler.Reason = RperfLoadLe32(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, OldWaitReason));
    Record.Header.Cpu = RperfLoadLe16(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, SourceProcessor));
    if (Record.Header.Cpu == 0xffff)
        Record.Header.Cpu = RPERF_MODEL_ALL_CPUS;
    Record.Data.Scheduler.TargetCpu = RperfLoadLe16(
        Bytes + offsetof(RPERF_SCHEDULER_RECORD_V1, TargetProcessor));
    Record.Data.Scheduler.DurationNs = RperfCodecTimestampNs(
        RperfLoadLe64(Bytes +
                      offsetof(RPERF_SCHEDULER_RECORD_V1, WaitDuration)),
        Input->FileHeader.TimestampFrequency);
    Record.Header.ProcessId = Record.Data.Scheduler.NewProcessId;
    Record.Header.ThreadId = Record.Data.Scheduler.NewThreadId;
    Record.Header.ProcessKey = Record.Data.Scheduler.NewProcessKey;
    Record.Header.ThreadKey = Record.Data.Scheduler.NewThreadKey;
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static BOOL
RperfParseCounterEvent(RPERF_V2_INPUT *Input,
                       const UCHAR *Bytes)
{
    RPERF_RECORD Record;
    const RPERF_SOURCE_MAP *Source;
    ULONG SourceId;
    ULONGLONG Address;

    Address = RperfLoadLe64(
        Bytes + offsetof(RPERF_COUNTER_RECORD_V1, InstructionPointer));
    if (Address == 0)
        return TRUE;
    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Record.Header.Kind = RperfRecordSample;
    Record.Header.ProcessId = RperfLoadLe32(
        Bytes + offsetof(RPERF_COUNTER_RECORD_V1, ProcessId));
    Record.Header.ThreadId = RperfLoadLe32(
        Bytes + offsetof(RPERF_COUNTER_RECORD_V1, ThreadId));
    Record.Header.ProcessKey = RperfLookupLifetime(
        Input->Processes, Input->ProcessCount,
        Record.Header.ProcessId, Record.Header.TimestampNs);
    Record.Header.ThreadKey = RperfLookupLifetime(
        Input->Threads, Input->ThreadCount,
        Record.Header.ThreadId, Record.Header.TimestampNs);
    Record.Header.Cpu = RperfLoadLe32(
        Bytes + offsetof(RPERF_COUNTER_RECORD_V1, ProcessorNumber));
    SourceId = RperfLoadLe32(
        Bytes + offsetof(RPERF_COUNTER_RECORD_V1, SourceId));
    Source = RperfInputSource(Input, SourceId);
    Record.Header.EventId = Source != NULL ? Source->EventId : SourceId;
    Record.Data.Sample.Weight = RperfLoadLe64(
        Bytes + offsetof(RPERF_COUNTER_RECORD_V1, Value));
    Record.Data.Sample.Period = RperfLoadLe64(
        Bytes + offsetof(RPERF_COUNTER_RECORD_V1, Period));
    Record.Data.Sample.Depth = 1;
    if (!RperfPopulateInputFrame(Input, Record.Header.ProcessKey, Address,
                                 RperfContextUnknown,
                                 &Record.Data.Sample.Frames[0]))
        return FALSE;
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static RPERF_LOSS_REASON
RperfInputLossReason(ULONG Reason)
{
    switch (Reason)
    {
        case RPERF_LOSS_RING_FULL: return RperfLossBufferFull;
        case RPERF_LOSS_ALLOCATION: return RperfLossAllocation;
        case RPERF_LOSS_STACK_WALK: return RperfLossUnsafeUnwind;
        case RPERF_LOSS_THROTTLED: return RperfLossDisabledEvent;
        case RPERF_LOSS_GAP_IN_INPUT: return RperfLossSequenceGap;
        default: return RperfLossUnknown;
    }
}

static BOOL
RperfParseLossEvent(RPERF_V2_INPUT *Input,
                    const UCHAR *Bytes)
{
    RPERF_RECORD Record;

    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Record.Header.Kind = RperfRecordLost;
    Record.Data.Lost.Reason = RperfInputLossReason(RperfLoadLe32(
        Bytes + offsetof(RPERF_LOSS_RECORD_V1, Reason)));
    Record.Data.Lost.Count = RperfLoadLe64(
        Bytes + offsetof(RPERF_LOSS_RECORD_V1, LostRecords));
    Record.Data.Lost.Weight = RperfLoadLe64(
        Bytes + offsetof(RPERF_LOSS_RECORD_V1, LostBytes));
    Record.Data.Lost.FirstSequence = RperfLoadLe64(
        Bytes + offsetof(RPERF_LOSS_RECORD_V1, FirstLostSequence));
    Record.Data.Lost.LastSequence = RperfLoadLe64(
        Bytes + offsetof(RPERF_LOSS_RECORD_V1, LastLostSequence));
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static BOOL
RperfParseClockEvent(RPERF_V2_INPUT *Input,
                     const UCHAR *Bytes)
{
    RPERF_RECORD Record;

    ZeroMemory(&Record, sizeof(Record));
    RperfInputCommonHeader(Input, Bytes, &Record.Header);
    Record.Header.Kind = RperfRecordClockSync;
    Record.Data.Clock.SystemTime100ns = RperfLoadLe64(
        Bytes + offsetof(RPERF_CLOCK_SYNC_RECORD_V1, SystemTime100ns));
    Record.Data.Clock.PerformanceCounter = RperfLoadLe64(
        Bytes + offsetof(RPERF_CLOCK_SYNC_RECORD_V1, PerformanceCounter));
    Record.Data.Clock.PerformanceFrequency = RperfLoadLe64(
        Bytes + offsetof(RPERF_CLOCK_SYNC_RECORD_V1,
                         PerformanceFrequency));
    Record.Data.Clock.InterruptTime100ns = RperfLoadLe64(
        Bytes + offsetof(RPERF_CLOCK_SYNC_RECORD_V1,
                         InterruptTime100ns));
    Record.Data.Clock.Flags = RperfLoadLe32(
        Bytes + offsetof(RPERF_CLOCK_SYNC_RECORD_V1, ClockFlags));
    return RperfRecordingAddRecord(Input->Recording, &Record);
}

static BOOL
RperfParseFooterRecord(RPERF_V2_INPUT *Input,
                       const UCHAR *Bytes)
{
    ULONG FinalStatus;

    Input->FooterSeen = TRUE;
    FinalStatus = RperfLoadLe32(
        Bytes + offsetof(RPERF_FOOTER_RECORD_V1, FinalStatus));
    if (Input->Recording->Info.CompletionError == ERROR_SUCCESS &&
        FinalStatus != 0)
        Input->Recording->Info.CompletionError = FinalStatus;
    return TRUE;
}

static USHORT
RperfExpectedRecordType(USHORT ChunkType)
{
    switch (ChunkType)
    {
        case RPERF_CHUNK_SESSION: return RPERF_RECORD_SESSION;
        case RPERF_CHUNK_SOURCE_TABLE: return RPERF_RECORD_SOURCE;
        case RPERF_CHUNK_PROCESS: return RPERF_RECORD_PROCESS;
        case RPERF_CHUNK_THREAD: return RPERF_RECORD_THREAD;
        case RPERF_CHUNK_IMAGE: return RPERF_RECORD_IMAGE;
        case RPERF_CHUNK_SAMPLE: return RPERF_RECORD_SAMPLE;
        case RPERF_CHUNK_SCHEDULER: return RPERF_RECORD_SCHEDULER;
        case RPERF_CHUNK_COUNTER: return RPERF_RECORD_COUNTER;
        case RPERF_CHUNK_LOSS: return RPERF_RECORD_LOSS;
        case RPERF_CHUNK_CLOCK_SYNC: return RPERF_RECORD_CLOCK_SYNC;
        case RPERF_CHUNK_STRING_TABLE: return RPERF_RECORD_STRING;
        case RPERF_CHUNK_SYMBOL_TABLE: return RPERF_RECORD_SYMBOL;
        case RPERF_CHUNK_MODULE_TABLE: return RPERF_RECORD_MODULE;
        case RPERF_CHUNK_INDEX: return RPERF_RECORD_INDEX;
        case RPERF_CHUNK_FOOTER: return RPERF_RECORD_FOOTER;
        default: return 0;
    }
}

static BOOL
RperfPassUsesChunk(RPERF_LOAD_PASS Pass,
                   USHORT ChunkType)
{
    if (Pass == RperfLoadStrings)
        return ChunkType == RPERF_CHUNK_STRING_TABLE;
    if (Pass == RperfLoadMetadata)
    {
        return ChunkType == RPERF_CHUNK_SESSION ||
               ChunkType == RPERF_CHUNK_SOURCE_TABLE ||
               ChunkType == RPERF_CHUNK_PROCESS ||
               ChunkType == RPERF_CHUNK_THREAD ||
               ChunkType == RPERF_CHUNK_MODULE_TABLE ||
               ChunkType == RPERF_CHUNK_SYMBOL_TABLE ||
               ChunkType == RPERF_CHUNK_FOOTER;
    }
    return ChunkType == RPERF_CHUNK_PROCESS ||
           ChunkType == RPERF_CHUNK_THREAD ||
           ChunkType == RPERF_CHUNK_IMAGE ||
           ChunkType == RPERF_CHUNK_SAMPLE ||
           ChunkType == RPERF_CHUNK_SCHEDULER ||
           ChunkType == RPERF_CHUNK_COUNTER ||
           ChunkType == RPERF_CHUNK_LOSS ||
           ChunkType == RPERF_CHUNK_CLOCK_SYNC;
}

static BOOL
RperfParseChunkPass(RPERF_V2_INPUT *Input,
                    RPERF_LOAD_PASS Pass,
                    const RPERF_CHUNK_HEADER_V1 *Chunk,
                    const UCHAR *Payload,
                    ULONG PayloadBytes)
{
    RPERF_RECORD_ITERATOR Iterator;
    const RPERF_RECORD_HEADER_V1 *Header;
    RPERF_STATUS Status;
    ULONGLONG Count = 0;
    USHORT Expected = RperfExpectedRecordType(Chunk->Type);

    if (!RperfPassUsesChunk(Pass, Chunk->Type))
        return TRUE;
    RperfRecordIteratorInitialize(&Iterator, Payload, PayloadBytes,
                                  Input->Limits.MaxRecordBytes);
    for (;;)
    {
        const UCHAR *Bytes;
        USHORT Type, Version;
        BOOL Result = TRUE;

        Status = RperfRecordIteratorNext(&Iterator, &Header);
        if (Status == RPERF_S_END_OF_FILE)
            break;
        if (Status != RPERF_OK)
            return RperfCodecFailStatus(Status, NULL);
        Count++;
        Bytes = (const UCHAR *)Header;
        Type = RperfLoadLe16(Bytes +
                             offsetof(RPERF_RECORD_HEADER_V1, Type));
        Version = RperfLoadLe16(Bytes +
                                offsetof(RPERF_RECORD_HEADER_V1, Version));
        if (Expected != 0 && Type != Expected)
        {
            SetLastError(ERROR_BAD_FORMAT);
            return FALSE;
        }
        if (Version != 1 &&
            !(Type == RPERF_RECORD_SESSION &&
              Version == RPERF_SESSION_RECORD_VERSION_METADATA))
        {
            if (Chunk->Flags & RPERF_CHUNK_FLAG_CRITICAL)
            {
                SetLastError(ERROR_NOT_SUPPORTED);
                return FALSE;
            }
            continue;
        }
        if (Pass == RperfLoadStrings)
            Result = RperfParseStringRecord(Input, Bytes);
        else if (Pass == RperfLoadMetadata)
        {
            switch (Type)
            {
                case RPERF_RECORD_SESSION:
                    Result = RperfParseSessionRecord(Input, Bytes);
                    break;
                case RPERF_RECORD_SOURCE:
                    Result = RperfParseSourceRecord(Input, Bytes);
                    break;
                case RPERF_RECORD_PROCESS:
                    Result = RperfIndexProcessLifetime(Input, Bytes);
                    break;
                case RPERF_RECORD_THREAD:
                    Result = RperfIndexThreadLifetime(Input, Bytes);
                    break;
                case RPERF_RECORD_MODULE:
                    Result = RperfParseModuleRecord(Input, Bytes);
                    break;
                case RPERF_RECORD_SYMBOL:
                    Result = RperfParseSymbolRecord(Input, Bytes);
                    break;
                case RPERF_RECORD_FOOTER:
                    Result = RperfParseFooterRecord(Input, Bytes);
                    break;
                default:
                    break;
            }
        }
        else
        {
            switch (Type)
            {
                case RPERF_RECORD_PROCESS:
                    Result = RperfParseProcessEvent(Input, Bytes);
                    break;
                case RPERF_RECORD_THREAD:
                    Result = RperfParseThreadEvent(Input, Bytes);
                    break;
                case RPERF_RECORD_IMAGE:
                    Result = RperfParseImageEvent(Input, Bytes);
                    break;
                case RPERF_RECORD_SAMPLE:
                    Result = RperfParseSampleEvent(Input, Bytes);
                    break;
                case RPERF_RECORD_SCHEDULER:
                    Result = RperfParseSchedulerEvent(Input, Bytes);
                    break;
                case RPERF_RECORD_COUNTER:
                    Result = RperfParseCounterEvent(Input, Bytes);
                    break;
                case RPERF_RECORD_LOSS:
                    Result = RperfParseLossEvent(Input, Bytes);
                    break;
                case RPERF_RECORD_CLOCK_SYNC:
                    Result = RperfParseClockEvent(Input, Bytes);
                    break;
                default:
                    break;
            }
        }
        if (!Result)
            return FALSE;
    }
    if (Count != Chunk->RecordCount)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfReaderFailureAtTail(const RPERF_READER *Reader,
                         const RPERF_V2_INPUT *Input)
{
    ULONGLONG End, Aligned;

    if (Input->FileHeader.Flags & RPERF_FILE_FLAG_FINALIZED)
        return FALSE;
    if (!Reader->Active)
        return Reader->NextChunkOffset < Reader->FileSizeLimit &&
               Reader->FileSizeLimit - Reader->NextChunkOffset <
               RPERF_CHUNK_HEADER_SIZE;
    if (Reader->ActiveChunkOffset > (ULONGLONG)-1 -
                                    Reader->ActiveChunk.HeaderSize ||
        Reader->ActiveChunkOffset + Reader->ActiveChunk.HeaderSize >
            (ULONGLONG)-1 - Reader->ActiveChunk.StoredSize)
        return TRUE;
    End = Reader->ActiveChunkOffset + Reader->ActiveChunk.HeaderSize +
          Reader->ActiveChunk.StoredSize;
    if (End > (ULONGLONG)-1 - (RPERF_ALIGNMENT - 1))
        return TRUE;
    Aligned = (End + RPERF_ALIGNMENT - 1) & ~(RPERF_ALIGNMENT - 1);
    if (Aligned > (ULONGLONG)-1 - RPERF_CHUNK_TRAILER_SIZE)
        return TRUE;
    End = Aligned + RPERF_CHUNK_TRAILER_SIZE;
    return End >= Reader->FileSizeLimit;
}

static BOOL
RperfHandleReaderFailure(RPERF_READER *Reader,
                         RPERF_SDK_FILE *File,
                         RPERF_V2_INPUT *Input,
                         RPERF_STATUS Failure,
                         BOOL Critical,
                         BOOL *ContinueWalking)
{
    BOOL Tail = RperfReaderFailureAtTail(Reader, Input);
    ULONGLONG Remaining, Scan, Skipped = 0;
    RPERF_STATUS Status;

    *ContinueWalking = FALSE;
    if (File->Error == ERROR_CANCELLED)
    {
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    if (!Critical)
    {
        ULONGLONG Offset = Reader->Active ?
                           Reader->ActiveChunkOffset :
                           Reader->NextChunkOffset;
        Remaining = Offset < Reader->FileSizeLimit ?
                    Reader->FileSizeLimit - Offset : 0;
        Scan = min(Remaining, RPERF_CODEC_RECOVERY_SCAN);
        if (Scan >= RPERF_ALIGNMENT)
        {
            Status = RperfReaderRecover(Reader, Scan, &Skipped);
            if (Status == RPERF_S_RECOVERED)
            {
                Input->Recovered = TRUE;
                Input->Incomplete = TRUE;
                *ContinueWalking = TRUE;
                return TRUE;
            }
        }
    }
    if (Tail)
    {
        Input->Incomplete = TRUE;
        return TRUE;
    }
    return RperfCodecFailStatus(Failure, File);
}

static BOOL
RperfWalkV2(RPERF_SDK_FILE *File,
            RPERF_V2_INPUT *Input,
            RPERF_LOAD_PASS Pass)
{
    RPERF_READER Reader;
    RPERF_READER_OPTIONS Options;
    RPERF_CHUNK_HEADER_V1 Chunk;
    ULONGLONG DiskRecords = 0;
    RPERF_STATUS Status;

    ZeroMemory(&Options, sizeof(Options));
    Options.Size = sizeof(Options);
    Options.Flags = RPERF_READER_FLAG_ALLOW_NEWER_MINOR |
                    RPERF_READER_FLAG_REQUIRE_ZERO_PADDING;
    Options.MaximumChunkBytes = RPERF_DEFAULT_MAX_CHUNK_BYTES;
    if (Input->Limits.MaxRecordBytes > Options.MaximumChunkBytes)
        Options.MaximumChunkBytes = Input->Limits.MaxRecordBytes;
    Options.MaximumRecordBytes = Input->Limits.MaxRecordBytes;
    Options.FileSizeLimit = File->PhysicalSize;
    File->Error = ERROR_SUCCESS;
    Status = RperfReaderInitialize(&Reader, RperfSdkReadAt, File, &Options);
    if (Status != RPERF_OK)
        return RperfCodecFailStatus(Status, File);
    if (Pass == RperfLoadStrings)
    {
        Input->FileHeader = Reader.Header;
        CopyMemory(&Input->Recording->Info.SessionId,
                   Reader.Header.SessionId, sizeof(GUID));
    }

    for (;;)
    {
        UCHAR *Payload = NULL;
        ULONG PayloadBytes = 0;
        BOOL ContinueWalking;
        BOOL Critical;

        Status = RperfReaderNextChunk(&Reader, &Chunk);
        if (Status == RPERF_S_END_OF_FILE)
            break;
        if (Status != RPERF_OK)
        {
            if (!RperfHandleReaderFailure(&Reader, File, Input, Status,
                                          FALSE, &ContinueWalking))
                return FALSE;
            if (ContinueWalking)
                continue;
            break;
        }
        Critical = (Chunk.Flags & RPERF_CHUNK_FLAG_CRITICAL) != 0;
        if (Chunk.RecordCount > Input->DiskRecordLimit -
                                min(Input->DiskRecordLimit, DiskRecords))
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        DiskRecords += Chunk.RecordCount;
        if (Chunk.CompressionAlgorithm != RPERF_COMPRESSION_NONE)
        {
            if (Critical)
            {
                SetLastError(ERROR_NOT_SUPPORTED);
                return FALSE;
            }
            Status = RperfReaderSkipChunk(&Reader);
            if (Status != RPERF_OK)
            {
                if (!RperfHandleReaderFailure(&Reader, File, Input, Status,
                                              Critical,
                                              &ContinueWalking))
                    return FALSE;
                if (ContinueWalking)
                    continue;
                break;
            }
            continue;
        }
        if (Chunk.StoredSize > MAXDWORD)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        PayloadBytes = (ULONG)Chunk.StoredSize;
        if (PayloadBytes != 0)
        {
            ULONG Done = 0;
            Payload = HeapAlloc(GetProcessHeap(), 0, PayloadBytes);
            if (Payload == NULL)
                return FALSE;
            while (Done < PayloadBytes)
            {
                uint32_t Read = 0;
                Status = RperfReaderReadChunkData(&Reader,
                                                  Payload + Done,
                                                  PayloadBytes - Done,
                                                  &Read);
                if (Status != RPERF_OK || Read == 0)
                    break;
                Done += Read;
            }
            if (Done != PayloadBytes)
            {
                if (Status == RPERF_OK)
                    Status = RPERF_E_TRUNCATED;
                HeapFree(GetProcessHeap(), 0, Payload);
                if (!RperfHandleReaderFailure(&Reader, File, Input, Status,
                                              Critical,
                                              &ContinueWalking))
                    return FALSE;
                if (ContinueWalking)
                    continue;
                break;
            }
        }
        Status = RperfReaderFinishChunk(&Reader);
        if (Status != RPERF_OK)
        {
            if (Payload != NULL)
                HeapFree(GetProcessHeap(), 0, Payload);
            if (!RperfHandleReaderFailure(&Reader, File, Input, Status,
                                          Critical, &ContinueWalking))
                return FALSE;
            if (ContinueWalking)
                continue;
            break;
        }
        if (RperfExpectedRecordType(Chunk.Type) != 0 &&
            Chunk.Version != 1)
        {
            if (Payload != NULL)
                HeapFree(GetProcessHeap(), 0, Payload);
            if (Critical)
            {
                SetLastError(ERROR_NOT_SUPPORTED);
                return FALSE;
            }
            continue;
        }
        if (!RperfParseChunkPass(Input, Pass, &Chunk,
                                 Payload, PayloadBytes))
        {
            if (Payload != NULL)
                HeapFree(GetProcessHeap(), 0, Payload);
            return FALSE;
        }
        if (Payload != NULL)
            HeapFree(GetProcessHeap(), 0, Payload);
    }
    return TRUE;
}

static BOOL
RperfFinalizeStringTable(RPERF_V2_INPUT *Input)
{
    SIZE_T Index;

    if (Input->StringCount == 0)
        return TRUE;
    qsort(Input->Strings, Input->StringCount,
          sizeof(*Input->Strings), RperfCompareStringValue);
    for (Index = 1; Index < Input->StringCount; ++Index)
    {
        if (Input->Strings[Index - 1].Id == Input->Strings[Index].Id)
        {
            SetLastError(ERROR_BAD_FORMAT);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
RperfFinalizeMetadata(RPERF_V2_INPUT *Input)
{
    SIZE_T Index;

    if (!Input->SessionSeen)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (Input->SourceCount != 0)
    {
        qsort(Input->Sources, Input->SourceCount,
              sizeof(*Input->Sources), RperfCompareSourceId);
        for (Index = 1; Index < Input->SourceCount; ++Index)
        {
            if (Input->Sources[Index - 1].SourceId ==
                Input->Sources[Index].SourceId)
            {
                SetLastError(ERROR_BAD_FORMAT);
                return FALSE;
            }
        }
    }
    if (Input->Recording->SymbolCount != 0)
    {
        qsort(Input->Recording->Symbols,
              Input->Recording->SymbolCount,
              sizeof(*Input->Recording->Symbols),
              RperfCompareModelSymbolAddress);
    }
    Input->Recording->Info.Metric = Input->PmuSource ?
        RperfMetricEventWeight : RperfMetricCpuSamples;
    return TRUE;
}

static BOOL
RperfApplyAliases(RPERF_V2_INPUT *Input)
{
    SIZE_T Index;
    SIZE_T BaseSymbolCount = Input->Recording->SymbolCount;
    ULONGLONG PreviousAddress = RPERF_MODEL_INVALID_ID;

    if (Input->AliasCount == 0)
        return TRUE;
    qsort(Input->Aliases, Input->AliasCount,
          sizeof(*Input->Aliases), RperfCompareAliasAddress);
    for (Index = 0; Index < Input->AliasCount; ++Index)
    {
        const RPERF_MODEL_SYMBOL *Alias = &Input->Aliases[Index].Symbol;
        SIZE_T Low = 0, High = BaseSymbolCount;

        if (Alias->Address == PreviousAddress)
            continue;
        PreviousAddress = Alias->Address;
        while (Low < High)
        {
            SIZE_T Middle = Low + (High - Low) / 2;
            if (Input->Recording->Symbols[Middle].Address < Alias->Address)
                Low = Middle + 1;
            else
                High = Middle;
        }
        if (Low < BaseSymbolCount &&
            Input->Recording->Symbols[Low].Address == Alias->Address)
            continue;
        if (!RperfRecordingAddSymbol(Input->Recording, Alias))
            return FALSE;
    }
    return TRUE;
}

static BOOL
RperfFinalizeLoadedRecording(RPERF_V2_INPUT *Input,
                             PCWSTR Path)
{
    RPERF_RECORDING *Recording = Input->Recording;
    SIZE_T Index;
    ULONGLONG Previous = 0, MaximumSequence = 0;
    BOOL ContainerComplete;

    if (!RperfApplyAliases(Input))
        return FALSE;
    if (Recording->RecordCount != 0)
    {
        qsort(Recording->Records, Recording->RecordCount,
              sizeof(*Recording->Records),
              RperfCompareModelRecordSequence);
        for (Index = 0; Index < Recording->RecordCount; ++Index)
        {
            RPERF_RECORD *Record = &Recording->Records[Index];
            if (Index != 0 && Record->Header.Sequence == Previous)
            {
                SetLastError(ERROR_BAD_FORMAT);
                return FALSE;
            }
            Previous = Record->Header.Sequence;
            MaximumSequence = max(MaximumSequence,
                                  Record->Header.Sequence);
            if (Recording->Info.StartTimeNs == 0 ||
                Record->Header.TimestampNs < Recording->Info.StartTimeNs)
                Recording->Info.StartTimeNs = Record->Header.TimestampNs;
            if (Record->Header.TimestampNs > Recording->Info.EndTimeNs)
                Recording->Info.EndTimeNs = Record->Header.TimestampNs;
        }
    }
    if (Input->Recovered)
    {
        RPERF_RECORD Loss;

        if (MaximumSequence == (ULONGLONG)-1)
        {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return FALSE;
        }
        ZeroMemory(&Loss, sizeof(Loss));
        Loss.Header.Kind = RperfRecordLost;
        Loss.Header.Flags = RPERF_MODEL_RECORD_FLAG_INCOMPLETE;
        Loss.Header.Sequence = MaximumSequence + 1;
        Loss.Header.TimestampNs = Recording->Info.EndTimeNs;
        Loss.Header.Cpu = RPERF_MODEL_ALL_CPUS;
        Loss.Data.Lost.Reason = RperfLossSequenceGap;
        Loss.Data.Lost.Count = 1;
        if (!RperfRecordingAddRecord(Recording, &Loss))
            return FALSE;
    }
    ContainerComplete =
        (Input->FileHeader.Flags & RPERF_FILE_FLAG_FINALIZED) != 0 ||
        Input->FooterSeen;
    Recording->Info.Complete = ContainerComplete &&
                               !Input->Incomplete &&
                               !Input->Recovered &&
                               Recording->Info.CompletionReason !=
                                   RperfCompletionIncomplete;
    if (!Recording->Info.Complete &&
        Recording->Info.CompletionError == ERROR_SUCCESS)
        Recording->Info.CompletionError = ERROR_HANDLE_EOF;
    if (Recording->Info.Backend == 0)
        Recording->Info.Backend = RperfBackendKernel;
    if (Recording->Info.Metric == 0)
        Recording->Info.Metric = RperfMetricCpuSamples;
    Recording->Counters.AttemptedSamples =
        Recording->Counters.SuccessfulSamples +
        Recording->Counters.LostRecords;
    if (!RperfRecordingSetSourcePath(Recording, Path) ||
        !RperfRecordingFreeze(Recording))
        return FALSE;
    return TRUE;
}

static BOOL
RperfLoadV2(PCWSTR Path,
            const RPERF_CAPTURE_LIMITS *Limits,
            HANDLE CancelEvent,
            RPERF_CODEC_PROGRESS Progress,
            PVOID ProgressContext,
            RPERF_RECORDING **Recording)
{
    RPERF_SDK_FILE File;
    RPERF_V2_INPUT Input;
    LARGE_INTEGER FileSize;
    ULONGLONG Limit;
    BOOL Result = FALSE;

    ZeroMemory(&File, sizeof(File));
    ZeroMemory(&Input, sizeof(Input));
    Input.Limits = *Limits;
    File.File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File.File == INVALID_HANDLE_VALUE)
        return FALSE;
    File.CancelEvent = CancelEvent;
    if (!GetFileSizeEx(File.File, &FileSize) || FileSize.QuadPart < 0)
        goto Cleanup;
    File.PhysicalSize = (ULONGLONG)FileSize.QuadPart;
    if (File.PhysicalSize > Limits->MaxFileBytes ||
        File.PhysicalSize < RPERF_FILE_HEADER_SIZE)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        goto Cleanup;
    }
    Input.Recording = RperfRecordingCreate(Limits);
    if (Input.Recording == NULL)
        goto Cleanup;
    Limit = Limits->MaxRecords;
    if (Limit <= (ULONGLONG)-1 - Limits->MaxSymbols * 3ULL)
        Limit += Limits->MaxSymbols * 3ULL;
    else
        Limit = (ULONGLONG)-1;
    if (Limit <= (ULONGLONG)-1 - Limits->MaxModules * 2ULL)
        Limit += Limits->MaxModules * 2ULL;
    else
        Limit = (ULONGLONG)-1;
    Input.DiskRecordLimit = Limit <= (ULONGLONG)-1 - 4096 ?
                            Limit + 4096 : (ULONGLONG)-1;

    if (!RperfWalkV2(&File, &Input, RperfLoadStrings) ||
        !RperfFinalizeStringTable(&Input))
        goto Cleanup;
    if (Progress != NULL)
        Progress(ProgressContext, 1, 3);
    if (!RperfWalkV2(&File, &Input, RperfLoadMetadata) ||
        !RperfFinalizeMetadata(&Input))
        goto Cleanup;
    if (Progress != NULL)
        Progress(ProgressContext, 2, 3);
    if (!RperfWalkV2(&File, &Input, RperfLoadEvents) ||
        !RperfFinalizeLoadedRecording(&Input, Path))
        goto Cleanup;
    if (Progress != NULL)
        Progress(ProgressContext, 3, 3);
    *Recording = Input.Recording;
    Input.Recording = NULL;
    Result = TRUE;

Cleanup:
    RperfDestroyV2Input(&Input);
    CloseHandle(File.File);
    return Result;
}

static BOOL
RperfEffectiveLimits(const RPERF_CAPTURE_LIMITS *Requested,
                     RPERF_CAPTURE_LIMITS *Effective)
{
    if (Requested != NULL)
        *Effective = *Requested;
    else
        RperfDefaultCaptureLimits(Effective);
    if (Effective->MaxFileBytes < RPERF_FILE_HEADER_SIZE ||
        Effective->MaxRecords == 0 ||
        Effective->MaxSamples == 0 ||
        Effective->MaxSymbols == 0 ||
        Effective->MaxModules == 0 ||
        Effective->MaxFrames == 0 ||
        Effective->MaxFrames > RPERF_MODEL_MAX_FRAMES ||
        Effective->MaxRecordBytes < RPERF_RECORD_HEADER_SIZE ||
        Effective->MaxRecordBytes > RPERF_HARD_MAX_RECORD_BYTES ||
        Effective->MaxStringBytes == 0 ||
        Effective->MaxStringBytes > RPERF_MAX_STRING_BYTES)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfProbeFormat(PCWSTR Path,
                 RPERF_CODEC_FORMAT *Format)
{
    HANDLE File;
    UCHAR Probe[8];
    DWORD Read = 0;

    *Format = RperfCodecAuto;
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!ReadFile(File, Probe, sizeof(Probe), &Read, NULL))
    {
        DWORD Error = GetLastError();
        CloseHandle(File);
        SetLastError(Error);
        return FALSE;
    }
    CloseHandle(File);
    if (Read == sizeof(Probe) &&
        memcmp(Probe, RPERF_FILE_MAGIC_BYTES, sizeof(Probe)) == 0)
        *Format = RperfCodecV2Binary;
    else if (Read >= 7 && memcmp(Probe, "RPERF\t1", 7) == 0)
        *Format = RperfCodecV1Text;
    else
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    return TRUE;
}

BOOL
RperfCodecLoad(PCWSTR Path,
               RPERF_CODEC_FORMAT RequestedFormat,
               const RPERF_CAPTURE_LIMITS *Limits,
               HANDLE CancelEvent,
               RPERF_CODEC_PROGRESS Progress,
               PVOID ProgressContext,
               RPERF_RECORDING **Recording,
               RPERF_CODEC_FORMAT *DetectedFormat)
{
    RPERF_CAPTURE_LIMITS Effective;
    RPERF_CODEC_FORMAT Detected;
    BOOL Result;

    if (Recording == NULL || Path == NULL || *Path == UNICODE_NULL ||
        RequestedFormat < RperfCodecAuto ||
        RequestedFormat > RperfCodecV2Binary)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *Recording = NULL;
    if (DetectedFormat != NULL)
        *DetectedFormat = RperfCodecAuto;
    if (!RperfEffectiveLimits(Limits, &Effective) ||
        !RperfProbeFormat(Path, &Detected))
        return FALSE;
    if (RequestedFormat != RperfCodecAuto && RequestedFormat != Detected)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (RperfCodecCancelled(CancelEvent))
    {
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    if (Detected == RperfCodecV1Text)
        Result = RperfLoadV1(Path, &Effective, Recording);
    else
        Result = RperfLoadV2(Path, &Effective, CancelEvent,
                             Progress, ProgressContext, Recording);
    if (!Result)
        return FALSE;
    if (RperfCodecCancelled(CancelEvent))
    {
        RperfRecordingRelease(*Recording);
        *Recording = NULL;
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    if (DetectedFormat != NULL)
        *DetectedFormat = Detected;
    return TRUE;
}

static PWSTR
RperfTemporaryPath(PCWSTR Path)
{
    SIZE_T Characters = wcslen(Path);
    PWSTR Temporary;
    int Result;

    if (Characters > ((SIZE_T)-1) - 64)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Temporary = HeapAlloc(GetProcessHeap(), 0,
                          (Characters + 64) * sizeof(WCHAR));
    if (Temporary == NULL)
        return NULL;
    Result = _snwprintf(Temporary, Characters + 63,
                        L"%s.tmp-%lu-%lu", Path,
                        GetCurrentProcessId(), GetTickCount());
    Temporary[Characters + 63] = UNICODE_NULL;
    if (Result < 0)
    {
        HeapFree(GetProcessHeap(), 0, Temporary);
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    return Temporary;
}

BOOL
RperfCodecSave(PCWSTR Path,
               RPERF_CODEC_FORMAT Format,
               const RPERF_RECORDING *Recording,
               HANDLE CancelEvent,
               RPERF_CODEC_PROGRESS Progress,
               PVOID ProgressContext)
{
    PWSTR Temporary;
    DWORD Error;
    BOOL Result;

    if (Path == NULL || *Path == UNICODE_NULL || Recording == NULL ||
        (Format != RperfCodecAuto &&
         Format != RperfCodecV1Text &&
         Format != RperfCodecV2Binary))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Format == RperfCodecAuto)
        Format = RperfCodecV2Binary;
    if (RperfCodecCancelled(CancelEvent))
    {
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    Temporary = RperfTemporaryPath(Path);
    if (Temporary == NULL)
        return FALSE;
    DeleteFileW(Temporary);
    SetLastError(ERROR_SUCCESS);
    if (Format == RperfCodecV1Text)
        Result = RperfSaveV1(Temporary, Recording, CancelEvent,
                             Progress, ProgressContext);
    else
        Result = RperfSaveV2(Temporary, Recording, CancelEvent,
                             Progress, ProgressContext);
    if (Result)
    {
        Result = MoveFileExW(Temporary, Path,
                             MOVEFILE_REPLACE_EXISTING |
                             MOVEFILE_WRITE_THROUGH);
    }
    Error = Result ? ERROR_SUCCESS : GetLastError();
    if (!Result)
        DeleteFileW(Temporary);
    HeapFree(GetProcessHeap(), 0, Temporary);
    if (!Result)
        SetLastError(Error != ERROR_SUCCESS ? Error : ERROR_WRITE_FAULT);
    return Result;
}
