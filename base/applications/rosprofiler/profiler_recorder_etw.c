/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Documented Windows ETW sampled-profile recorder backend
 */

#include "profiler_recorder_internal.h"
#include "profiler_pe.h"

#include <wmistr.h>
#include <evntrace.h>
#include <evntprov.h>
#include <reactos/rperf.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef EVENT_TRACE_SYSTEM_LOGGER_MODE
#define EVENT_TRACE_SYSTEM_LOGGER_MODE 0x02000000
#endif
#ifndef PROCESS_TRACE_MODE_REAL_TIME
#define PROCESS_TRACE_MODE_REAL_TIME 0x00000100
#endif
#ifndef PROCESS_TRACE_MODE_EVENT_RECORD
#define PROCESS_TRACE_MODE_EVENT_RECORD 0x10000000
#endif
#ifndef EVENT_HEADER_EXT_TYPE_STACK_TRACE32
#define EVENT_HEADER_EXT_TYPE_STACK_TRACE32 5
#define EVENT_HEADER_EXT_TYPE_STACK_TRACE64 6
#endif

#define RPERF_ETW_PROFILE_OPCODE 46
#define RPERF_ETW_MAX_EXTENDED_ITEMS 64
#define RPERF_ETW_MAX_PROFILE_SOURCE_BYTES (1024U * 1024U)

typedef ULONG (WINAPI *RPERF_START_TRACE_W)(PTRACEHANDLE, LPCWSTR,
                                            PEVENT_TRACE_PROPERTIES);
typedef ULONG (WINAPI *RPERF_CONTROL_TRACE_W)(TRACEHANDLE, LPCWSTR,
                                              PEVENT_TRACE_PROPERTIES, ULONG);
typedef TRACEHANDLE (WINAPI *RPERF_OPEN_TRACE_W)(PEVENT_TRACE_LOGFILEW);
typedef ULONG (WINAPI *RPERF_PROCESS_TRACE)(PTRACEHANDLE, ULONG,
                                            LPFILETIME, LPFILETIME);
typedef ULONG (WINAPI *RPERF_CLOSE_TRACE)(TRACEHANDLE);
typedef ULONG (WINAPI *RPERF_TRACE_SET_INFORMATION)(TRACEHANDLE,
                                                    TRACE_INFO_CLASS,
                                                    PVOID, ULONG);
typedef ULONG (WINAPI *RPERF_TRACE_QUERY_INFORMATION)(TRACEHANDLE,
                                                      TRACE_INFO_CLASS,
                                                      PVOID, ULONG,
                                                      PULONG);

/* Documented EVENT_RECORD layout, kept local because this SDK only forwards it. */
typedef struct _RPERF_ETW_EVENT_DESCRIPTOR
{
    USHORT Id;
    UCHAR Version;
    UCHAR Channel;
    UCHAR Level;
    UCHAR Opcode;
    USHORT Task;
    ULONGLONG Keyword;
} RPERF_ETW_EVENT_DESCRIPTOR;

typedef struct _RPERF_ETW_EVENT_HEADER
{
    USHORT Size;
    USHORT HeaderType;
    USHORT Flags;
    USHORT EventProperty;
    ULONG ThreadId;
    ULONG ProcessId;
    LARGE_INTEGER TimeStamp;
    GUID ProviderId;
    RPERF_ETW_EVENT_DESCRIPTOR EventDescriptor;
    union
    {
        struct
        {
            ULONG KernelTime;
            ULONG UserTime;
        } Times;
        ULONGLONG ProcessorTime;
    } Time;
    GUID ActivityId;
} RPERF_ETW_EVENT_HEADER;

typedef struct _RPERF_ETW_BUFFER_CONTEXT
{
    union
    {
        struct
        {
            UCHAR ProcessorNumber;
            UCHAR Alignment;
        } Legacy;
        USHORT ProcessorIndex;
    } Processor;
    USHORT LoggerId;
} RPERF_ETW_BUFFER_CONTEXT;

typedef struct _RPERF_ETW_EXTENDED_ITEM
{
    USHORT Reserved1;
    USHORT ExtType;
    USHORT Linkage;
    USHORT DataSize;
    ULONGLONG DataPtr;
} RPERF_ETW_EXTENDED_ITEM;

typedef struct _RPERF_ETW_EVENT_RECORD
{
    RPERF_ETW_EVENT_HEADER EventHeader;
    RPERF_ETW_BUFFER_CONTEXT BufferContext;
    USHORT ExtendedDataCount;
    USHORT UserDataLength;
    RPERF_ETW_EXTENDED_ITEM *ExtendedData;
    PVOID UserData;
    PVOID UserContext;
} RPERF_ETW_EVENT_RECORD;

typedef struct _RPERF_ETW_ID
{
    ULONG Numeric;
    ULONGLONG Stable;
} RPERF_ETW_ID;

typedef struct _RPERF_ETW_STATE
{
    RPERF_CAPTURE_CONFIGURATION Config;
    HMODULE Advapi;
    RPERF_START_TRACE_W StartTraceW;
    RPERF_CONTROL_TRACE_W ControlTraceW;
    RPERF_OPEN_TRACE_W OpenTraceW;
    RPERF_PROCESS_TRACE ProcessTrace;
    RPERF_CLOSE_TRACE CloseTrace;
    RPERF_TRACE_SET_INFORMATION TraceSetInformation;
    RPERF_TRACE_QUERY_INFORMATION TraceQueryInformation;
    TRACEHANDLE SessionHandle;
    TRACEHANDLE ConsumerHandle;
    PEVENT_TRACE_PROPERTIES Properties;
    ULONG PropertiesBytes;
    EVENT_TRACE_LOGFILEW Log;
    WCHAR SessionName[96];
    HANDLE Worker;
    HANDLE DurationWorker;
    HANDLE StopEvent;
    volatile LONG StopIssued;
    volatile LONG DurationExpired;
    volatile LONG TargetExited;
    volatile LONG UserStopRequested;
    TRACE_PROFILE_INTERVAL PreviousInterval;
    TRACE_PROFILE_INTERVAL ActiveInterval;
    BOOL PreviousIntervalValid;
    BOOL IntervalChanged;
    RPERF_RECORDING *Recording;
    RPERF_ETW_ID *Processes;
    SIZE_T ProcessCount;
    SIZE_T ProcessCapacity;
    RPERF_ETW_ID *Threads;
    SIZE_T ThreadCount;
    SIZE_T ThreadCapacity;
    ULONGLONG NextSequence;
    ULONGLONG Frequency;
    ULONG PointerSize;
    RPERF_BASELINE_RESULT Baseline;
    DWORD Error;
} RPERF_ETW_STATE;

static const GUID RperfEtwSystemTraceGuid =
    {0x9e814aad, 0x3204, 0x11d2,
     {0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39}};
static const GUID RperfEtwPerfInfoGuid =
    {0xce1dbfb4, 0x137e, 0x4da6,
     {0x87, 0xb0, 0x3f, 0x59, 0xaa, 0x10, 0x2c, 0xbc}};
static const GUID RperfEtwProcessGuid =
    {0x3d6fa8d0, 0xfe05, 0x11d0,
     {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};
static const GUID RperfEtwThreadGuid =
    {0x3d6fa8d1, 0xfe05, 0x11d0,
     {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};
static const GUID RperfEtwImageGuid =
    {0x2cb15d1d, 0x5fc1, 0x11d2,
     {0xab, 0xe1, 0x00, 0xa0, 0xc9, 0x11, 0xf5, 0x18}};

static BOOL RperfEtwStopSession(RPERF_ETW_STATE *State);

static BOOL
RperfEtwConfigureInterval(RPERF_ETW_STATE *State)
{
    TRACE_PROFILE_INTERVAL Queried;
    ULONG Returned = 0, Status;

    ZeroMemory(&State->PreviousInterval,
               sizeof(State->PreviousInterval));
    Status = State->TraceQueryInformation(
        0, TraceSampledProfileIntervalInfo,
        &State->PreviousInterval, sizeof(State->PreviousInterval),
        &Returned);
    if (Status != ERROR_SUCCESS || Returned < sizeof(State->PreviousInterval))
    {
        SetLastError(Status != ERROR_SUCCESS ? Status : ERROR_BAD_LENGTH);
        return FALSE;
    }
    State->PreviousIntervalValid = TRUE;
    ZeroMemory(&State->ActiveInterval, sizeof(State->ActiveInterval));
    State->ActiveInterval.Source = 0;
    State->ActiveInterval.Interval = State->Config.IntervalUs * 10;
    Status = State->TraceSetInformation(
        0, TraceSampledProfileIntervalInfo,
        &State->ActiveInterval, sizeof(State->ActiveInterval));
    if (Status != ERROR_SUCCESS)
    {
        SetLastError(Status);
        return FALSE;
    }
    State->IntervalChanged = TRUE;
    ZeroMemory(&Queried, sizeof(Queried));
    Returned = 0;
    Status = State->TraceQueryInformation(
        0, TraceSampledProfileIntervalInfo,
        &Queried, sizeof(Queried),
        &Returned);
    if (Status != ERROR_SUCCESS || Returned < sizeof(Queried) ||
        Queried.Source != State->ActiveInterval.Source ||
        Queried.Interval != State->ActiveInterval.Interval)
    {
        SetLastError(Status != ERROR_SUCCESS ? Status : ERROR_BAD_FORMAT);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfEtwRestoreInterval(RPERF_ETW_STATE *State)
{
    TRACE_PROFILE_INTERVAL Current;
    ULONG Returned = 0, Status;

    if (!State->IntervalChanged || !State->PreviousIntervalValid)
        return TRUE;
    ZeroMemory(&Current, sizeof(Current));
    Status = State->TraceQueryInformation(
        0, TraceSampledProfileIntervalInfo,
        &Current, sizeof(Current), &Returned);
    if (Status != ERROR_SUCCESS || Returned < sizeof(Current))
    {
        SetLastError(Status != ERROR_SUCCESS ? Status : ERROR_BAD_LENGTH);
        return FALSE;
    }
    if (Current.Source != State->ActiveInterval.Source ||
        Current.Interval != State->ActiveInterval.Interval)
    {
        /* Another controller changed the global interval; do not clobber it. */
        State->IntervalChanged = FALSE;
        return TRUE;
    }
    Status = State->TraceSetInformation(
        0, TraceSampledProfileIntervalInfo,
        &State->PreviousInterval, sizeof(State->PreviousInterval));
    if (Status != ERROR_SUCCESS)
    {
        SetLastError(Status);
        return FALSE;
    }
    State->IntervalChanged = FALSE;
    return TRUE;
}

static BOOL
RperfEtwGuidEqual(const GUID *Left,
                  const GUID *Right)
{
    return memcmp(Left, Right, sizeof(*Left)) == 0;
}

static HMODULE
RperfEtwLoadAdvapi(VOID)
{
    WCHAR Path[MAX_PATH];
    UINT Length = GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    if (Length == 0 || Length >= ARRAYSIZE(Path) - 14)
        return NULL;
    if (Path[Length - 1] != L'\\')
        Path[Length++] = L'\\';
    lstrcpyW(Path + Length, L"advapi32.dll");
    return LoadLibraryW(Path);
}

static BOOL
RperfEtwResolve(RPERF_ETW_STATE *State)
{
    State->Advapi = RperfEtwLoadAdvapi();
    if (State->Advapi == NULL)
        return FALSE;
#define RPERF_ETW_RESOLVE(Member, Name) \
    State->Member = (PVOID)GetProcAddress(State->Advapi, Name)
    RPERF_ETW_RESOLVE(StartTraceW, "StartTraceW");
    RPERF_ETW_RESOLVE(ControlTraceW, "ControlTraceW");
    RPERF_ETW_RESOLVE(OpenTraceW, "OpenTraceW");
    RPERF_ETW_RESOLVE(ProcessTrace, "ProcessTrace");
    RPERF_ETW_RESOLVE(CloseTrace, "CloseTrace");
    RPERF_ETW_RESOLVE(TraceSetInformation, "TraceSetInformation");
    RPERF_ETW_RESOLVE(TraceQueryInformation, "TraceQueryInformation");
#undef RPERF_ETW_RESOLVE
    if (State->StartTraceW == NULL || State->ControlTraceW == NULL ||
        State->OpenTraceW == NULL || State->ProcessTrace == NULL ||
        State->CloseTrace == NULL || State->TraceSetInformation == NULL ||
        State->TraceQueryInformation == NULL)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfEtwQueryTimerSource(RPERF_ETW_STATE *State,
                         PULONG MinimumInterval100ns)
{
    PUCHAR Buffer = NULL;
    ULONG Required = 0, Status, Offset = 0;
    BOOL Found = FALSE;

    *MinimumInterval100ns = 0;
    Status = State->TraceQueryInformation(0,
                                          TraceProfileSourceListInfo,
                                          NULL, 0, &Required);
    if (Status != ERROR_INSUFFICIENT_BUFFER &&
        Status != ERROR_MORE_DATA &&
        Status != ERROR_BAD_LENGTH &&
        Status != ERROR_SUCCESS)
    {
        SetLastError(Status);
        return FALSE;
    }
    if (Required < offsetof(PROFILE_SOURCE_INFO, Description) +
                   sizeof(WCHAR) ||
        Required > RPERF_ETW_MAX_PROFILE_SOURCE_BYTES)
    {
        SetLastError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required);
    if (Buffer == NULL)
        return FALSE;
    Status = State->TraceQueryInformation(0,
                                          TraceProfileSourceListInfo,
                                          Buffer, Required, &Required);
    if (Status != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        SetLastError(Status);
        return FALSE;
    }
    while (Offset < Required)
    {
        PROFILE_SOURCE_INFO *Source;
        ULONG Remaining = Required - Offset;
        ULONG EntryBytes;

        if (Remaining < offsetof(PROFILE_SOURCE_INFO, Description) +
                        sizeof(WCHAR))
            goto Malformed;
        Source = (PROFILE_SOURCE_INFO *)(Buffer + Offset);
        EntryBytes = Source->NextEntryOffset != 0 ?
                     Source->NextEntryOffset : Remaining;
        if (EntryBytes < offsetof(PROFILE_SOURCE_INFO, Description) +
                         sizeof(WCHAR) ||
            EntryBytes > Remaining ||
            (Source->NextEntryOffset != 0 &&
             (Source->NextEntryOffset & (sizeof(ULONG) - 1)) != 0) ||
            wmemchr(Source->Description, UNICODE_NULL,
                    (EntryBytes -
                     offsetof(PROFILE_SOURCE_INFO, Description)) /
                    sizeof(WCHAR)) == NULL)
            goto Malformed;
        if (Source->Source == 0)
        {
            Found = TRUE;
            *MinimumInterval100ns = Source->MinInterval;
            break;
        }
        if (Source->NextEntryOffset == 0)
            break;
        Offset += Source->NextEntryOffset;
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    if (!Found)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return TRUE;

Malformed:
    HeapFree(GetProcessHeap(), 0, Buffer);
    SetLastError(ERROR_BAD_FORMAT);
    return FALSE;
}

BOOL
RperfEtwQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Capabilities)
{
    RPERF_ETW_STATE State;
    ULONG MinimumInterval100ns;
    ZeroMemory(&State, sizeof(State));
    if (!RperfEtwResolve(&State))
    {
        Capabilities->Available = FALSE;
        Capabilities->Status = GetLastError();
        lstrcpyW(Capabilities->Description,
                 L"Documented ETW sampled-profile APIs are unavailable on "
                 L"this operating system.");
        if (State.Advapi != NULL)
            FreeLibrary(State.Advapi);
        return TRUE;
    }
    if (!RperfEtwQueryTimerSource(&State, &MinimumInterval100ns))
    {
        Capabilities->Available = FALSE;
        Capabilities->RequiresPrivilege = GetLastError() == ERROR_ACCESS_DENIED;
        Capabilities->Status = GetLastError();
        lstrcpyW(Capabilities->Description,
                 L"ETW profile-time sampling is not available through the "
                 L"documented profile-source query.");
        FreeLibrary(State.Advapi);
        return TRUE;
    }
    Capabilities->Available = TRUE;
    Capabilities->RequiresPrivilege = TRUE;
    Capabilities->Features = RPERF_CAP_TIMER | RPERF_CAP_USER_STACKS |
                             RPERF_CAP_KERNEL_STACKS |
                             RPERF_CAP_PROCESS_EVENTS |
                             RPERF_CAP_THREAD_EVENTS |
                             RPERF_CAP_IMAGE_EVENTS |
                             RPERF_CAP_SYSTEM_WIDE |
                             RPERF_CAP_STACK_WALK;
    Capabilities->MinimumIntervalUs =
        max(1, (MinimumInterval100ns + 9) / 10);
    Capabilities->MaximumStackDepth = RPERF_MODEL_MAX_FRAMES;
    Capabilities->AbiVersion = 1;
    Capabilities->Status = ERROR_SUCCESS;
    lstrcpyW(Capabilities->Description,
             L"Windows documented ETW profile-time sampling and kernel "
             L"events; PMU sampling is intentionally not advertised.");
    FreeLibrary(State.Advapi);
    return TRUE;
}

static BOOL
RperfEtwGrowIds(RPERF_ETW_ID **Ids,
                SIZE_T *Capacity,
                SIZE_T Required)
{
    SIZE_T NewCapacity = *Capacity != 0 ? *Capacity * 2 : 64;
    PVOID NewBuffer;
    if (Required <= *Capacity)
        return TRUE;
    if (NewCapacity < Required)
        NewCapacity = Required;
    if (NewCapacity > ((SIZE_T)-1) / sizeof(**Ids))
        return FALSE;
    if (*Ids != NULL)
        NewBuffer = HeapReAlloc(GetProcessHeap(), 0, *Ids,
                                NewCapacity * sizeof(**Ids));
    else
        NewBuffer = HeapAlloc(GetProcessHeap(), 0,
                              NewCapacity * sizeof(**Ids));
    if (NewBuffer == NULL)
        return FALSE;
    *Ids = NewBuffer;
    *Capacity = NewCapacity;
    return TRUE;
}

static ULONGLONG
RperfEtwFindId(const RPERF_ETW_ID *Ids,
               SIZE_T Count,
               ULONG Numeric)
{
    SIZE_T Index;
    for (Index = Count; Index != 0; --Index)
    {
        if (Ids[Index - 1].Numeric == Numeric)
            return Ids[Index - 1].Stable;
    }
    return Numeric;
}

static BOOL
RperfEtwSetId(RPERF_ETW_ID **Ids,
              SIZE_T *Count,
              SIZE_T *Capacity,
              ULONG Numeric,
              ULONGLONG Stable)
{
    SIZE_T Index;
    for (Index = 0; Index < *Count; ++Index)
    {
        if ((*Ids)[Index].Numeric == Numeric)
        {
            (*Ids)[Index].Stable = Stable;
            return TRUE;
        }
    }
    if (!RperfEtwGrowIds(Ids, Capacity, *Count + 1))
        return FALSE;
    (*Ids)[*Count].Numeric = Numeric;
    (*Ids)[*Count].Stable = Stable;
    (*Count)++;
    return TRUE;
}

static BOOL CALLBACK
RperfEtwBaselineId(PVOID Opaque,
                   BOOL Thread,
                   ULONG NumericId,
                   ULONGLONG StableId)
{
    RPERF_ETW_STATE *State = Opaque;

    if (Thread)
    {
        return RperfEtwSetId(&State->Threads, &State->ThreadCount,
                             &State->ThreadCapacity,
                             NumericId, StableId);
    }
    return RperfEtwSetId(&State->Processes, &State->ProcessCount,
                         &State->ProcessCapacity,
                         NumericId, StableId);
}

static ULONGLONG
RperfEtwTimestampNs(const RPERF_ETW_STATE *State,
                    ULONGLONG Timestamp)
{
    ULONGLONG Whole, Remainder;
    if (State->Frequency == 0)
        return Timestamp;
    Whole = Timestamp / State->Frequency;
    Remainder = Timestamp % State->Frequency;
    if (Whole > (ULONGLONG)-1 / 1000000000ULL)
        return (ULONGLONG)-1;
    return Whole * 1000000000ULL +
           (Remainder * 1000000000ULL) / State->Frequency;
}

static BOOL
RperfEtwEventSelected(const RPERF_ETW_STATE *State,
                      const RPERF_ETW_EVENT_RECORD *Event)
{
    return State->Config.Scope == RperfScopeSystem ||
           Event->EventHeader.ProcessId == State->Config.ProcessId;
}

static BOOL
RperfEtwEventEnvelopeValid(RPERF_ETW_STATE *State,
                           const RPERF_ETW_EVENT_RECORD *Event)
{
    if (Event->EventHeader.Size < sizeof(Event->EventHeader) ||
        Event->ExtendedDataCount > RPERF_ETW_MAX_EXTENDED_ITEMS ||
        (Event->ExtendedDataCount != 0 && Event->ExtendedData == NULL) ||
        (Event->UserDataLength != 0 && Event->UserData == NULL) ||
        (Event->ExtendedData != NULL &&
         (ULONG_PTR)Event->ExtendedData > (ULONG_PTR)-1 -
             (SIZE_T)Event->ExtendedDataCount * sizeof(*Event->ExtendedData)) ||
        (Event->UserData != NULL &&
         (ULONG_PTR)Event->UserData > (ULONG_PTR)-1 -
             Event->UserDataLength))
    {
        State->Recording->Counters.MalformedRecords++;
        return FALSE;
    }
    return TRUE;
}

static const RPERF_MODULE *
RperfEtwModuleForAddress(const RPERF_ETW_STATE *State,
                         ULONGLONG ProcessKey,
                         ULONGLONG Address)
{
    const RPERF_MODULE *Best = NULL;
    SIZE_T Index;

    for (Index = 0; Index < State->Recording->ModuleCount; ++Index)
    {
        const RPERF_MODULE *Module = &State->Recording->Modules[Index];
        if (Module->ProcessKey != 0 && ProcessKey != 0 &&
            Module->ProcessKey != ProcessKey)
            continue;
        if (Address >= Module->Base &&
            (Module->Size == 0 || Address - Module->Base < Module->Size) &&
            (Best == NULL || Module->Base > Best->Base))
            Best = Module;
    }
    return Best;
}

static const RPERF_MODULE *
RperfEtwModuleAtBase(const RPERF_ETW_STATE *State,
                     ULONGLONG ProcessKey,
                     ULONGLONG Base)
{
    SIZE_T Index;

    for (Index = State->Recording->ModuleCount; Index != 0; --Index)
    {
        const RPERF_MODULE *Module = &State->Recording->Modules[Index - 1];
        if (Module->Base == Base &&
            (ProcessKey == 0 || Module->ProcessKey == ProcessKey))
            return Module;
    }
    return NULL;
}

static VOID
RperfEtwInitializeFrame(RPERF_ETW_STATE *State,
                        ULONGLONG ProcessKey,
                        ULONGLONG Address,
                        RPERF_FRAME *Frame)
{
    const RPERF_MODULE *Module =
        RperfEtwModuleForAddress(State, ProcessKey, Address);

    ZeroMemory(Frame, sizeof(*Frame));
    Frame->Address = Address;
    Frame->FunctionAddress = Address;
    Frame->ModuleId = Module != NULL ? Module->Id : RPERF_MODEL_INVALID_ID;
    Frame->Context = RperfContextUnknown;
    Frame->Resolution = RperfResolutionAddress;
}

static VOID
RperfEtwInitializeRecord(RPERF_ETW_STATE *State,
                         const RPERF_ETW_EVENT_RECORD *Event,
                         RPERF_RECORD *Record)
{
    ZeroMemory(Record, sizeof(*Record));
    Record->Header.Sequence = State->NextSequence++;
    Record->Header.TimestampNs =
        RperfEtwTimestampNs(State, Event->EventHeader.TimeStamp.QuadPart);
    Record->Header.ProcessId = Event->EventHeader.ProcessId;
    Record->Header.ThreadId = Event->EventHeader.ThreadId;
    Record->Header.ProcessKey =
        RperfEtwFindId(State->Processes, State->ProcessCount,
                       Record->Header.ProcessId);
    Record->Header.ThreadKey =
        RperfEtwFindId(State->Threads, State->ThreadCount,
                       Record->Header.ThreadId);
    Record->Header.Cpu = Event->BufferContext.Processor.Legacy.ProcessorNumber;
}

static ULONGLONG
RperfEtwReadPointer(const UCHAR *Data,
                    ULONG Available,
                    ULONG PointerSize)
{
    ULONGLONG Value = 0;
    if ((PointerSize != 4 && PointerSize != 8) || Available < PointerSize)
        return 0;
    CopyMemory(&Value, Data, PointerSize);
    return Value;
}

static BOOL
RperfEtwAddSample(RPERF_ETW_STATE *State,
                  const RPERF_ETW_EVENT_RECORD *Event)
{
    RPERF_RECORD Record;
    ULONG ExtendedIndex;
    USHORT Depth = 0;

    if (State->Config.Scope != RperfScopeSystem &&
        Event->EventHeader.ProcessId != State->Config.ProcessId)
        return TRUE;
    State->Recording->Counters.AttemptedSamples++;
    RperfEtwInitializeRecord(State, Event, &Record);
    Record.Header.Kind = RperfRecordSample;
    Record.Header.EventId = 0;
    Record.Data.Sample.Weight = 1;
    Record.Data.Sample.Period = (ULONGLONG)State->Config.IntervalUs * 1000;
    for (ExtendedIndex = 0;
         ExtendedIndex < Event->ExtendedDataCount;
         ++ExtendedIndex)
    {
        const RPERF_ETW_EXTENDED_ITEM *Item =
            &Event->ExtendedData[ExtendedIndex];
        const UCHAR *Data = (const UCHAR *)(ULONG_PTR)Item->DataPtr;
        ULONG AddressSize, Count, Index;

        if (Item->ExtType == EVENT_HEADER_EXT_TYPE_STACK_TRACE32)
            AddressSize = 4;
        else if (Item->ExtType == EVENT_HEADER_EXT_TYPE_STACK_TRACE64)
            AddressSize = 8;
        else
            continue;
        if (Data == NULL ||
            (ULONG_PTR)Data > (ULONG_PTR)-1 - Item->DataSize ||
            Item->DataSize < sizeof(ULONGLONG) ||
            (Item->DataSize - sizeof(ULONGLONG)) % AddressSize != 0)
        {
            State->Recording->Counters.SchemaSkips++;
            continue;
        }
        Data += sizeof(ULONGLONG); /* MatchId */
        Count = (Item->DataSize - sizeof(ULONGLONG)) / AddressSize;
        if (Count > State->Config.Limits.MaxFrames)
        {
            Count = State->Config.Limits.MaxFrames;
            Record.Header.Flags |= RPERF_MODEL_RECORD_FLAG_TRUNCATED;
        }
        for (Index = 0; Index < Count; ++Index)
        {
            ULONGLONG Address = RperfEtwReadPointer(
                Data + Index * AddressSize, AddressSize, AddressSize);
            if (Address == 0)
                continue;
            RperfEtwInitializeFrame(State, Record.Header.ProcessKey,
                                    Address,
                                    &Record.Data.Sample.Frames[Depth++]);
        }
        break;
    }
    if (Depth == 0 && Event->UserData != NULL)
    {
        ULONGLONG Address = RperfEtwReadPointer(Event->UserData,
                                                Event->UserDataLength,
                                                State->PointerSize);
        if (Address != 0)
        {
            RperfEtwInitializeFrame(State, Record.Header.ProcessKey,
                                    Address,
                                    &Record.Data.Sample.Frames[0]);
            Depth = 1;
        }
    }
    if (Depth == 0)
    {
        State->Recording->Counters.FailedSamples++;
        State->Recording->Counters.UnwindFailures++;
        State->Recording->Counters.SchemaSkips++;
        return TRUE;
    }
    Record.Data.Sample.Depth = Depth;
    return RperfRecordingAddRecord(State->Recording, &Record);
}

static BOOL
RperfEtwAddLifecycle(RPERF_ETW_STATE *State,
                     const RPERF_ETW_EVENT_RECORD *Event,
                     RPERF_RECORD_KIND StartKind,
                     RPERF_RECORD_KIND EndKind,
                     BOOL Thread)
{
    RPERF_RECORD Record;
    BOOL Ending = Event->EventHeader.EventDescriptor.Opcode ==
                  EVENT_TRACE_TYPE_END ||
                  Event->EventHeader.EventDescriptor.Opcode ==
                  EVENT_TRACE_TYPE_DC_END;
    ULONGLONG Stable;
    ULONGLONG Existing;
    ULONG Numeric;

    RperfEtwInitializeRecord(State, Event, &Record);
    Record.Header.Kind = Ending ? EndKind : StartKind;
    Numeric = Thread ? Event->EventHeader.ThreadId :
                       Event->EventHeader.ProcessId;
    Existing = Thread ? Record.Header.ThreadKey : Record.Header.ProcessKey;
    if (Ending ||
        Event->EventHeader.EventDescriptor.Opcode == EVENT_TRACE_TYPE_DC_START)
        Stable = Existing;
    else
        Stable = ((ULONGLONG)Numeric << 32) ^
                 Record.Header.TimestampNs ^ Record.Header.Sequence;
    if (!Ending)
    {
        if (Thread)
        {
            if (!RperfEtwSetId(&State->Threads, &State->ThreadCount,
                               &State->ThreadCapacity, Numeric, Stable))
                return FALSE;
            Record.Header.ThreadKey = Stable;
        }
        else
        {
            if (!RperfEtwSetId(&State->Processes, &State->ProcessCount,
                               &State->ProcessCapacity, Numeric, Stable))
                return FALSE;
            Record.Header.ProcessKey = Stable;
        }
    }
    Record.Data.Lifecycle.ObjectId = Stable;
    if (Thread)
        Record.Data.Lifecycle.ParentId = Record.Header.ProcessKey;
    return RperfRecordingAddRecord(State->Recording, &Record);
}

BOOL
RperfEtwDecodeImageRecord(const VOID *OpaqueData,
                          USHORT DataLength,
                          ULONG PointerSize,
                          ULONG Architecture,
                          UCHAR Version,
                          ULONG MaximumStringBytes,
                          ULONGLONG ProcessKey,
                          RPERF_MODULE *Module,
                          PWSTR *Path)
{
    const UCHAR *Data = OpaqueData;
    ULONG Length = DataLength;
    ULONG Offset;
    SIZE_T Characters, Index;
    PWSTR Copy;

    if (Module == NULL || Path == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(Module, sizeof(*Module));
    *Path = NULL;
    if (Data == NULL || (PointerSize != 4 && PointerSize != 8) ||
        Architecture == RPERF_ARCH_UNKNOWN)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Length < PointerSize * 2)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Module->ProcessKey = ProcessKey;
    Module->Base = RperfEtwReadPointer(Data, Length, PointerSize);
    Module->Size = RperfEtwReadPointer(Data + PointerSize,
                                       Length - PointerSize,
                                       PointerSize);
    Module->Architecture = Architecture;
    if (Version >= 2)
    {
        ULONG Fixed = PointerSize * 3 + 8 * sizeof(ULONG);
        ULONG ChecksumOffset = PointerSize * 2 + sizeof(ULONG);

        if (Length < Fixed)
        {
            SetLastError(ERROR_BAD_FORMAT);
            return FALSE;
        }
        CopyMemory(&Module->Checksum,
                   Data + ChecksumOffset,
                   sizeof(Module->Checksum));
        CopyMemory(&Module->TimeDateStamp,
                   Data + ChecksumOffset + sizeof(ULONG),
                   sizeof(Module->TimeDateStamp));
        Offset = Fixed;
    }
    else
    {
        Offset = PointerSize * 2 + sizeof(ULONG);
        if (Length < Offset)
        {
            SetLastError(ERROR_BAD_FORMAT);
            return FALSE;
        }
    }
    Characters = 0;
    while (Offset + (Characters + 1) * sizeof(WCHAR) <= Length)
    {
        WCHAR Character;
        CopyMemory(&Character,
                   Data + Offset + Characters * sizeof(WCHAR),
                   sizeof(Character));
        if (Character == UNICODE_NULL)
            break;
        Characters++;
    }
    if (Offset + (Characters + 1) * sizeof(WCHAR) > Length ||
        (Characters + 1) * sizeof(WCHAR) > MaximumStringBytes)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Copy = HeapAlloc(GetProcessHeap(), 0,
                     (Characters + 1) * sizeof(WCHAR));
    if (Copy == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    for (Index = 0; Index < Characters; ++Index)
    {
        CopyMemory(&Copy[Index], Data + Offset + Index * sizeof(WCHAR),
                   sizeof(WCHAR));
    }
    Copy[Characters] = UNICODE_NULL;
    Module->Path = Copy;
    *Path = Copy;
    return TRUE;
}

static BOOL
RperfEtwAddImage(RPERF_ETW_STATE *State,
                 const RPERF_ETW_EVENT_RECORD *Event)
{
    RPERF_RECORD Record;
    RPERF_MODULE Module, DecodedModule;
    const RPERF_MODULE *Existing;
    ULONGLONG Base, Size, Id;
    ULONG Checksum = 0, TimeDateStamp = 0;
    PWSTR Path = NULL;
    BOOL MetadataAvailable;
    BOOL Unload = Event->EventHeader.EventDescriptor.Opcode ==
                  EVENT_TRACE_TYPE_END;

    if (Event->UserData == NULL ||
        Event->UserDataLength < State->PointerSize * 2)
    {
        State->Recording->Counters.SchemaSkips++;
        return TRUE;
    }
    Base = RperfEtwReadPointer(Event->UserData,
                               Event->UserDataLength,
                               State->PointerSize);
    Size = RperfEtwReadPointer((const UCHAR *)Event->UserData +
                               State->PointerSize,
                               Event->UserDataLength - State->PointerSize,
                               State->PointerSize);
    if (Base == 0 || Size == 0)
    {
        State->Recording->Counters.SchemaSkips++;
        return TRUE;
    }
    RperfEtwInitializeRecord(State, Event, &Record);
    MetadataAvailable = RperfEtwDecodeImageRecord(
                            Event->UserData,
                            Event->UserDataLength,
                            State->PointerSize,
                            State->Recording->Info.ProducerArchitecture,
                            Event->EventHeader.EventDescriptor.Version,
                            State->Config.Limits.MaxStringBytes,
                            Record.Header.ProcessKey,
                            &DecodedModule,
                            &Path);
    if (!MetadataAvailable)
    {
        if (GetLastError() == ERROR_NOT_ENOUGH_MEMORY)
            return FALSE;
        State->Recording->Counters.SchemaSkips++;
    }
    else
    {
        Checksum = DecodedModule.Checksum;
        TimeDateStamp = DecodedModule.TimeDateStamp;
    }
    Existing = RperfEtwModuleAtBase(State, Record.Header.ProcessKey, Base);
    Id = Existing != NULL ? Existing->Id :
         Base ^ (Record.Header.ProcessKey << 1) ^
         (Unload ? 0 : Record.Header.TimestampNs);
    if (!Unload && Existing == NULL && Base != 0 && Size != 0)
    {
        ZeroMemory(&Module, sizeof(Module));
        if (MetadataAvailable)
            Module = DecodedModule;
        Module.Id = Id;
        Module.ProcessKey = Record.Header.ProcessKey;
        Module.Base = Base;
        Module.Size = Size;
        Module.Architecture =
            State->Recording->Info.ProducerArchitecture;
        Module.TimeDateStamp = TimeDateStamp;
        Module.Checksum = Checksum;
        Module.Path = Path;
        if (Path != NULL)
        {
            RPERF_MODULE Enriched = Module;
            if (RperfEnrichModuleFromImage(&Enriched, Path))
            {
                Enriched.Id = Module.Id;
                Enriched.ProcessKey = Module.ProcessKey;
                Enriched.Base = Module.Base;
                Module = Enriched;
            }
        }
        if (!RperfRecordingAddModule(State->Recording, &Module))
        {
            if (Path != NULL)
                HeapFree(GetProcessHeap(), 0, Path);
            return FALSE;
        }
    }
    Record.Header.Kind = Unload ? RperfRecordImageUnload :
                                  RperfRecordImageLoad;
    Record.Data.Lifecycle.ObjectId = Id;
    Record.Data.Lifecycle.ModuleId = Id;
    Record.Data.Lifecycle.ImageBase = Base;
    Record.Data.Lifecycle.ImageSize = Size;
    if (Path != NULL)
        HeapFree(GetProcessHeap(), 0, Path);
    return RperfRecordingAddRecord(State->Recording, &Record);
}

static VOID WINAPI
RperfEtwEventCallback(PEVENT_RECORD Opaque)
{
    const RPERF_ETW_EVENT_RECORD *Event =
        (const RPERF_ETW_EVENT_RECORD *)Opaque;
    RPERF_ETW_STATE *State;
    BOOL Result = TRUE;

    if (Event == NULL)
        return;
    State = Event->UserContext;
    if (State == NULL || State->Error != ERROR_SUCCESS)
        return;
    if (!RperfEtwEventEnvelopeValid(State, Event))
        return;
    if (RperfEtwGuidEqual(&Event->EventHeader.ProviderId,
                          &RperfEtwPerfInfoGuid) &&
        Event->EventHeader.EventDescriptor.Opcode ==
            RPERF_ETW_PROFILE_OPCODE)
    {
        Result = RperfEtwAddSample(State, Event);
    }
    else if (RperfEtwGuidEqual(&Event->EventHeader.ProviderId,
                               &RperfEtwProcessGuid))
    {
        if (!RperfEtwEventSelected(State, Event))
            return;
        Result = RperfEtwAddLifecycle(State, Event,
                                      RperfRecordProcessStart,
                                      RperfRecordProcessEnd,
                                      FALSE);
        if (Result && State->Config.Scope != RperfScopeSystem &&
            Event->EventHeader.ProcessId == State->Config.ProcessId &&
            (Event->EventHeader.EventDescriptor.Opcode ==
             EVENT_TRACE_TYPE_END ||
             Event->EventHeader.EventDescriptor.Opcode ==
             EVENT_TRACE_TYPE_DC_END))
        {
            InterlockedExchange(&State->TargetExited, 1);
            SetEvent(State->StopEvent);
        }
    }
    else if (RperfEtwGuidEqual(&Event->EventHeader.ProviderId,
                               &RperfEtwThreadGuid))
    {
        if (!RperfEtwEventSelected(State, Event))
            return;
        Result = RperfEtwAddLifecycle(State, Event,
                                      RperfRecordThreadStart,
                                      RperfRecordThreadEnd,
                                      TRUE);
    }
    else if (RperfEtwGuidEqual(&Event->EventHeader.ProviderId,
                               &RperfEtwImageGuid))
    {
        if (!RperfEtwEventSelected(State, Event))
            return;
        Result = RperfEtwAddImage(State, Event);
    }
    if (!Result)
    {
        State->Error = GetLastError();
        if (State->Error == ERROR_SUCCESS)
            State->Error = ERROR_BAD_FORMAT;
        SetEvent(State->StopEvent);
    }
}

static BOOL
RperfEtwStopSession(RPERF_ETW_STATE *State)
{
    ULONG Status;
    SetEvent(State->StopEvent);
    if (InterlockedCompareExchange(&State->StopIssued, 1, 0) != 0)
        return TRUE;
    Status = State->ControlTraceW(State->SessionHandle,
                                  State->SessionName,
                                  State->Properties,
                                  EVENT_TRACE_CONTROL_STOP);
    if (Status != ERROR_SUCCESS && Status != ERROR_WMI_INSTANCE_NOT_FOUND)
    {
        SetLastError(Status);
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI
RperfEtwDurationThread(PVOID Opaque)
{
    RPERF_ETW_STATE *State = Opaque;
    if (WaitForSingleObject(State->StopEvent,
                            State->Config.DurationMs != 0 ?
                            State->Config.DurationMs : INFINITE) ==
        WAIT_TIMEOUT)
        InterlockedExchange(&State->DurationExpired, 1);
    RperfEtwStopSession(State);
    return ERROR_SUCCESS;
}

static DWORD WINAPI
RperfEtwWorker(PVOID Opaque)
{
    RPERF_ETW_STATE *State = Opaque;
    ULONG Status = State->ProcessTrace(&State->ConsumerHandle,
                                       1, NULL, NULL);
    if (Status != ERROR_SUCCESS && Status != ERROR_CANCELLED)
        State->Error = Status;
    return Status;
}

static VOID
RperfEtwAbortStart(RPERF_ETW_STATE *State)
{
    SetEvent(State->StopEvent);
    if (State->SessionHandle != 0 &&
        InterlockedCompareExchange(&State->StopIssued, 0, 0) == 0)
        RperfEtwStopSession(State);
    if (State->ConsumerHandle != 0 &&
        State->ConsumerHandle != INVALID_PROCESSTRACE_HANDLE)
    {
        State->CloseTrace(State->ConsumerHandle);
        State->ConsumerHandle = INVALID_PROCESSTRACE_HANDLE;
    }
    if (State->Worker != NULL)
        WaitForSingleObject(State->Worker, INFINITE);
    if (State->DurationWorker != NULL)
        WaitForSingleObject(State->DurationWorker, INFINITE);
    RperfEtwRestoreInterval(State);
}

static BOOL
RperfEtwStart(PVOID Opaque)
{
    RPERF_ETW_STATE *State = Opaque;
    CLASSIC_EVENT_ID StackEvent;
    ULONG Status;

    if (!RperfEtwConfigureInterval(State))
    {
        DWORD Error = GetLastError();
        RperfEtwRestoreInterval(State);
        SetLastError(Error);
        return FALSE;
    }
    Status = State->StartTraceW(&State->SessionHandle,
                                State->SessionName,
                                State->Properties);
    if (Status != ERROR_SUCCESS)
    {
        RperfEtwRestoreInterval(State);
        SetLastError(Status);
        return FALSE;
    }
    ZeroMemory(&StackEvent, sizeof(StackEvent));
    StackEvent.EventGuid = RperfEtwPerfInfoGuid;
    StackEvent.Type = RPERF_ETW_PROFILE_OPCODE;
    Status = State->TraceSetInformation(State->SessionHandle,
                                        TraceStackTracingInfo,
                                        &StackEvent,
                                        sizeof(StackEvent));
    if (Status != ERROR_SUCCESS)
        goto Failure;

    if (State->Config.Scope != RperfScopeSystem &&
        State->Config.ProcessId != 0)
    {
        if (!RperfCaptureBaseline(&State->Config,
                                  State->Recording,
                                  0,
                                  &State->NextSequence,
                                  RperfEtwBaselineId,
                                  State,
                                  &State->Baseline))
        {
            Status = GetLastError();
            goto Failure;
        }
        if (State->Baseline.Partial)
        {
            RPERF_RECORD Loss;
            ZeroMemory(&Loss, sizeof(Loss));
            Loss.Header.Kind = RperfRecordLost;
            Loss.Header.Flags = RPERF_MODEL_RECORD_FLAG_SYNTHETIC;
            Loss.Header.Sequence = State->NextSequence++;
            Loss.Header.Cpu = RPERF_MODEL_ALL_CPUS;
            Loss.Data.Lost.Reason = RperfLossUserspaceSnapshot;
            Loss.Data.Lost.Count = 1;
            if (!RperfRecordingAddRecord(State->Recording, &Loss))
            {
                Status = GetLastError();
                goto Failure;
            }
        }
    }

    ZeroMemory(&State->Log, sizeof(State->Log));
    State->Log.LoggerName = State->SessionName;
    State->Log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME |
                                  PROCESS_TRACE_MODE_EVENT_RECORD;
    State->Log.EventRecordCallback = RperfEtwEventCallback;
    State->Log.Context = State;
    State->ConsumerHandle = State->OpenTraceW(&State->Log);
    if (State->ConsumerHandle == INVALID_PROCESSTRACE_HANDLE)
    {
        Status = GetLastError();
        goto Failure;
    }
    State->PointerSize = State->Log.LogfileHeader.PointerSize;
    if (State->PointerSize != 4 && State->PointerSize != 8)
        State->PointerSize = sizeof(PVOID);
    State->Recording->Info.AddressWidth = State->PointerSize * 8;
    State->Worker = CreateThread(NULL, 0, RperfEtwWorker, State, 0, NULL);
    if (State->Worker == NULL)
    {
        Status = GetLastError();
        goto Failure;
    }
    State->DurationWorker = CreateThread(NULL, 0,
                                         RperfEtwDurationThread,
                                         State, 0, NULL);
    if (State->DurationWorker == NULL)
    {
        Status = GetLastError();
        goto Failure;
    }
    return TRUE;

Failure:
    RperfEtwAbortStart(State);
    SetLastError(Status);
    return FALSE;
}

static BOOL
RperfEtwStop(PVOID Opaque)
{
    RPERF_ETW_STATE *State = Opaque;
    InterlockedExchange(&State->UserStopRequested, 1);
    return SetEvent(State->StopEvent);
}

static BOOL
RperfEtwJoin(PVOID Opaque,
             DWORD Timeout)
{
    RPERF_ETW_STATE *State = Opaque;
    DWORD Wait;

    Wait = WaitForSingleObject(State->Worker, Timeout);
    if (Wait == WAIT_TIMEOUT)
    {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    if (Wait != WAIT_OBJECT_0)
        return FALSE;
    SetEvent(State->StopEvent);
    if (State->DurationWorker != NULL)
        WaitForSingleObject(State->DurationWorker, INFINITE);
    if (State->ConsumerHandle != 0 &&
        State->ConsumerHandle != INVALID_PROCESSTRACE_HANDLE)
    {
        State->CloseTrace(State->ConsumerHandle);
        State->ConsumerHandle = 0;
    }
    State->SessionHandle = 0;
    if (!RperfEtwRestoreInterval(State) &&
        State->Error == ERROR_SUCCESS)
        State->Error = GetLastError();
    if (State->Error != ERROR_SUCCESS)
    {
        State->Recording->Info.Complete = FALSE;
        State->Recording->Info.CompletionReason = RperfCompletionError;
        State->Recording->Info.CompletionError = State->Error;
        SetLastError(State->Error);
        return FALSE;
    }
    if (State->Log.EventsLost != 0)
    {
        RPERF_RECORD Lost;
        ZeroMemory(&Lost, sizeof(Lost));
        Lost.Header.Kind = RperfRecordLost;
        Lost.Header.Sequence = State->NextSequence++;
        Lost.Header.Cpu = RPERF_MODEL_ALL_CPUS;
        Lost.Data.Lost.Reason = RperfLossBufferFull;
        Lost.Data.Lost.Count = State->Log.EventsLost;
        if (!RperfRecordingAddRecord(State->Recording, &Lost))
            return FALSE;
    }
    State->Recording->Info.EndTimeNs =
        State->Recording->RecordCount != 0 ?
        State->Recording->Records[State->Recording->RecordCount - 1].
            Header.TimestampNs : 0;
    State->Recording->Info.Complete = TRUE;
    State->Recording->Info.CompletionError = ERROR_SUCCESS;
    if (InterlockedCompareExchange(&State->DurationExpired, 0, 0) != 0)
        State->Recording->Info.CompletionReason = RperfCompletionDuration;
    else if (InterlockedCompareExchange(&State->TargetExited, 0, 0) != 0)
        State->Recording->Info.CompletionReason = RperfCompletionTargetExit;
    else if (InterlockedCompareExchange(&State->UserStopRequested, 0, 0) != 0)
        State->Recording->Info.CompletionReason = RperfCompletionUserStop;
    else
    {
        State->Recording->Info.Complete = FALSE;
        State->Recording->Info.CompletionReason = RperfCompletionIncomplete;
        State->Recording->Info.CompletionError = ERROR_HANDLE_EOF;
    }
    if (!RperfRecordingFreeze(State->Recording))
        return FALSE;
    return TRUE;
}

static BOOL
RperfEtwCounters(PVOID Opaque,
                 RPERF_CAPTURE_COUNTERS *Counters)
{
    RPERF_ETW_STATE *State = Opaque;
    *Counters = State->Recording->Counters;
    return TRUE;
}

static RPERF_RECORDING *
RperfEtwTake(PVOID Opaque)
{
    RPERF_ETW_STATE *State = Opaque;
    RperfRecordingAddRef(State->Recording);
    return State->Recording;
}

static VOID
RperfEtwDestroy(PVOID Opaque)
{
    RPERF_ETW_STATE *State = Opaque;
    if (State->SessionHandle != 0 && State->ControlTraceW != NULL)
        RperfEtwStopSession(State);
    if (State->ConsumerHandle != 0 &&
        State->ConsumerHandle != INVALID_PROCESSTRACE_HANDLE &&
        State->CloseTrace != NULL)
        State->CloseTrace(State->ConsumerHandle);
    if (State->Worker != NULL)
        CloseHandle(State->Worker);
    if (State->DurationWorker != NULL)
        CloseHandle(State->DurationWorker);
    if (State->StopEvent != NULL)
        CloseHandle(State->StopEvent);
    if (State->Recording != NULL)
        RperfRecordingRelease(State->Recording);
    if (State->Processes != NULL)
        HeapFree(GetProcessHeap(), 0, State->Processes);
    if (State->Threads != NULL)
        HeapFree(GetProcessHeap(), 0, State->Threads);
    if (State->Properties != NULL)
        HeapFree(GetProcessHeap(), 0, State->Properties);
    if (State->TraceQueryInformation != NULL &&
        State->TraceSetInformation != NULL)
        RperfEtwRestoreInterval(State);
    if (State->Advapi != NULL)
        FreeLibrary(State->Advapi);
    HeapFree(GetProcessHeap(), 0, State);
}

static const RPERF_RECORDER_OPS RperfEtwOps =
{
    RperfEtwStart,
    RperfEtwStop,
    RperfEtwJoin,
    RperfEtwCounters,
    RperfEtwTake,
    RperfEtwDestroy
};

BOOL
RperfEtwCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
               const RPERF_RECORDER_OPS **Ops,
               PVOID *Opaque)
{
    RPERF_ETW_STATE *State;
    LARGE_INTEGER Frequency;
    SIZE_T NameBytes;

    if (Configuration->EventId != 0 ||
        Configuration->Scope == RperfScopeSelectedThreads ||
        Configuration->Scope == RperfScopeProcessTree ||
        Configuration->FollowChildren ||
        Configuration->IntervalUs > MAXDWORD / 10)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    State = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*State));
    if (State == NULL)
        return FALSE;
    State->SessionHandle = 0;
    State->ConsumerHandle = INVALID_PROCESSTRACE_HANDLE;
    State->Config = *Configuration;
    State->NextSequence = 1;
    if (!RperfEtwResolve(State))
        goto Failure;
    State->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (State->StopEvent == NULL)
        goto Failure;
    if (!QueryPerformanceFrequency(&Frequency) || Frequency.QuadPart <= 0)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        goto Failure;
    }
    State->Frequency = Frequency.QuadPart;
    _snwprintf(State->SessionName, ARRAYSIZE(State->SessionName),
               L"RosProfiler-%lu-%08lx",
               GetCurrentProcessId(), GetTickCount());
    State->SessionName[ARRAYSIZE(State->SessionName) - 1] = UNICODE_NULL;
    NameBytes = (wcslen(State->SessionName) + 1) * sizeof(WCHAR);
    State->PropertiesBytes = sizeof(EVENT_TRACE_PROPERTIES) + (ULONG)NameBytes;
    State->Properties = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                  State->PropertiesBytes);
    if (State->Properties == NULL)
        goto Failure;
    State->Properties->Wnode.BufferSize = State->PropertiesBytes;
    State->Properties->Wnode.Guid = RperfEtwSystemTraceGuid;
    State->Properties->Wnode.ClientContext = 1; /* QPC timestamps */
    State->Properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    State->Properties->BufferSize = 64;
    State->Properties->MinimumBuffers = 4;
    State->Properties->MaximumBuffers = 64;
    State->Properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE |
                                     EVENT_TRACE_SYSTEM_LOGGER_MODE;
    State->Properties->EnableFlags = EVENT_TRACE_FLAG_PROCESS |
                                     EVENT_TRACE_FLAG_THREAD |
                                     EVENT_TRACE_FLAG_IMAGE_LOAD |
                                     EVENT_TRACE_FLAG_PROFILE;
    State->Properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    CopyMemory((PUCHAR)State->Properties +
               State->Properties->LoggerNameOffset,
               State->SessionName,
               NameBytes);
    State->Recording = RperfRecordingCreate(&Configuration->Limits);
    if (State->Recording == NULL)
        goto Failure;
    RperfRecordingSetSink(State->Recording, Configuration->RecordSink, Configuration->RecordSinkContext);
    State->Recording->Info.Backend = RperfBackendEtw;
    State->Recording->Info.Metric = RperfMetricCpuSamples;
    State->Recording->Info.ProducerArchitecture = RperfNativeArchitecture();
    State->Recording->Info.ClockId = 2;
    State->Recording->Info.ClockFrequency = State->Frequency;
    State->Recording->Info.AddressWidth = sizeof(PVOID) * 8;
    State->Recording->Info.IntervalMs =
        (Configuration->IntervalUs + 999) / 1000;
    State->Recording->Info.RequestedDurationMs = Configuration->DurationMs;
    if (!RperfRecordingSetTargetName(State->Recording,
                                     Configuration->TargetName))
        goto Failure;
    *Ops = &RperfEtwOps;
    *Opaque = State;
    return TRUE;

Failure:
    RperfEtwDestroy(State);
    return FALSE;
}
