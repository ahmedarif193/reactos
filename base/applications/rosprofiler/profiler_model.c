/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Backend-neutral immutable recording model
 */

#include "profiler_model.h"

#include <reactos/rperf.h>

#include <stdlib.h>
#include <string.h>

ULONG
RperfNativeArchitecture(VOID)
{
#if defined(_M_ARM64)
    return RPERF_ARCH_ARM64;
#elif defined(_M_AMD64)
    return RPERF_ARCH_AMD64;
#elif defined(_M_ARM)
    return RPERF_ARCH_ARM;
#elif defined(_M_IX86)
    return RPERF_ARCH_X86;
#else
    return RPERF_ARCH_UNKNOWN;
#endif
}

static BOOL
RperfResize(PVOID *Buffer,
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
    if (NewCapacity < Required || NewCapacity > ((SIZE_T)-1) / ElementSize)
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

static PWSTR
RperfDuplicateWide(PCWSTR Text,
                   ULONG MaximumBytes)
{
    SIZE_T Characters;
    SIZE_T Bytes;
    PWSTR Copy;

    if (Text == NULL)
        return NULL;
    Characters = wcslen(Text) + 1;
    if (Characters > ((SIZE_T)-1) / sizeof(WCHAR))
        return NULL;
    Bytes = Characters * sizeof(WCHAR);
    if (Bytes > MaximumBytes)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Copy = HeapAlloc(GetProcessHeap(), 0, Bytes);
    if (Copy != NULL)
        CopyMemory(Copy, Text, Bytes);
    return Copy;
}

static PSTR
RperfDuplicateAnsi(PCSTR Text,
                   ULONG MaximumBytes)
{
    SIZE_T Bytes;
    PSTR Copy;

    if (Text == NULL)
        return NULL;
    Bytes = strlen(Text) + 1;
    if (Bytes > MaximumBytes)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Copy = HeapAlloc(GetProcessHeap(), 0, Bytes);
    if (Copy != NULL)
        CopyMemory(Copy, Text, Bytes);
    return Copy;
}

VOID
RperfDefaultCaptureLimits(RPERF_CAPTURE_LIMITS *Limits)
{
    if (Limits == NULL)
        return;
    ZeroMemory(Limits, sizeof(*Limits));
    Limits->MaxFileBytes = 16ULL * 1024 * 1024 * 1024;
    Limits->MaxRecords = 4000000;
    Limits->MaxSamples = 1000000;
    Limits->MaxSymbols = 524288;
    Limits->MaxModules = 65536;
    Limits->MaxThreads = 65536;
    Limits->MaxFrames = 256;
    Limits->MaxRecordBytes = 1024 * 1024;
    Limits->MaxStringBytes = 1024 * 1024;
    Limits->MaxDurationMs = 7 * 24 * 60 * 60 * 1000;
}

VOID
RperfInitializeFilter(RPERF_FILTER *Filter)
{
    if (Filter == NULL)
        return;
    ZeroMemory(Filter, sizeof(*Filter));
    Filter->EndNs = (ULONGLONG)-1;
    Filter->Cpu = RPERF_MODEL_ALL_CPUS;
    Filter->EventId = RPERF_MODEL_ALL_EVENTS;
    Filter->ModuleId = RPERF_MODEL_ALL_MODULES;
    Filter->IncludeTruncated = TRUE;
    Filter->IncludeLoss = TRUE;
}

RPERF_RECORDING *
RperfRecordingCreate(const RPERF_CAPTURE_LIMITS *Limits)
{
    RPERF_RECORDING *Recording;

    Recording = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                          sizeof(*Recording));
    if (Recording == NULL)
        return NULL;
    Recording->References = 1;
    if (Limits != NULL)
        Recording->Limits = *Limits;
    else
        RperfDefaultCaptureLimits(&Recording->Limits);
    Recording->Info.AddressWidth = sizeof(PVOID);
    Recording->Info.Metric = RperfMetricSnapshotPopulation;
    return Recording;
}

VOID
RperfRecordingAddRef(RPERF_RECORDING *Recording)
{
    if (Recording != NULL)
        InterlockedIncrement(&Recording->References);
}

VOID
RperfRecordingRelease(RPERF_RECORDING *Recording)
{
    SIZE_T Index;

    if (Recording == NULL || InterlockedDecrement(&Recording->References) != 0)
        return;
    if (Recording->Info.TargetName != NULL)
        HeapFree(GetProcessHeap(), 0, Recording->Info.TargetName);
    if (Recording->Info.SourcePath != NULL)
        HeapFree(GetProcessHeap(), 0, Recording->Info.SourcePath);
    for (Index = 0; Index < Recording->ModuleCount; ++Index)
    {
        if (Recording->Modules[Index].Path != NULL)
            HeapFree(GetProcessHeap(), 0, Recording->Modules[Index].Path);
    }
    for (Index = 0; Index < Recording->SymbolCount; ++Index)
    {
        RPERF_MODEL_SYMBOL *Symbol = &Recording->Symbols[Index];
        if (Symbol->Name != NULL)
            HeapFree(GetProcessHeap(), 0, Symbol->Name);
        if (Symbol->ModuleName != NULL)
            HeapFree(GetProcessHeap(), 0, Symbol->ModuleName);
        if (Symbol->SourceFile != NULL)
            HeapFree(GetProcessHeap(), 0, Symbol->SourceFile);
    }
    if (Recording->Records != NULL)
        HeapFree(GetProcessHeap(), 0, Recording->Records);
    if (Recording->Modules != NULL)
        HeapFree(GetProcessHeap(), 0, Recording->Modules);
    if (Recording->Symbols != NULL)
        HeapFree(GetProcessHeap(), 0, Recording->Symbols);
    HeapFree(GetProcessHeap(), 0, Recording);
}

static BOOL
RperfReplaceWide(PWSTR *Destination,
                 PCWSTR Source,
                 ULONG MaximumBytes)
{
    PWSTR Copy = RperfDuplicateWide(Source, MaximumBytes);

    if (Source != NULL && Copy == NULL)
    {
        if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (*Destination != NULL)
        HeapFree(GetProcessHeap(), 0, *Destination);
    *Destination = Copy;
    return TRUE;
}

BOOL
RperfRecordingSetTargetName(RPERF_RECORDING *Recording,
                            PCWSTR Name)
{
    if (Recording == NULL || Recording->Frozen)
    {
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    return RperfReplaceWide(&Recording->Info.TargetName,
                            Name,
                            Recording->Limits.MaxStringBytes);
}

BOOL
RperfRecordingSetSourcePath(RPERF_RECORDING *Recording,
                            PCWSTR Path)
{
    if (Recording == NULL || Recording->Frozen)
    {
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    return RperfReplaceWide(&Recording->Info.SourcePath,
                            Path,
                            Recording->Limits.MaxStringBytes);
}

BOOL
RperfRecordingAddModule(RPERF_RECORDING *Recording,
                        const RPERF_MODULE *Module)
{
    RPERF_MODULE Copy;

    if (Recording == NULL || Module == NULL || Recording->Frozen ||
        Module->Id == RPERF_MODEL_INVALID_ID)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!RperfResize((PVOID *)&Recording->Modules,
                     sizeof(*Recording->Modules),
                     &Recording->ModuleCapacity,
                     Recording->ModuleCount + 1,
                     Recording->Limits.MaxModules))
        return FALSE;
    Copy = *Module;
    Copy.Path = RperfDuplicateWide(Module->Path,
                                   Recording->Limits.MaxStringBytes);
    if (Module->Path != NULL && Copy.Path == NULL)
    {
        if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    Recording->Modules[Recording->ModuleCount++] = Copy;
    return TRUE;
}

BOOL
RperfRecordingAddSymbol(RPERF_RECORDING *Recording,
                        const RPERF_MODEL_SYMBOL *Symbol)
{
    RPERF_MODEL_SYMBOL Copy;

    if (Recording == NULL || Symbol == NULL || Recording->Frozen ||
        Symbol->Address == 0 || Symbol->FunctionAddress > Symbol->Address)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!RperfResize((PVOID *)&Recording->Symbols,
                     sizeof(*Recording->Symbols),
                     &Recording->SymbolCapacity,
                     Recording->SymbolCount + 1,
                     Recording->Limits.MaxSymbols))
        return FALSE;
    Copy = *Symbol;
    Copy.Name = RperfDuplicateAnsi(Symbol->Name,
                                   Recording->Limits.MaxStringBytes);
    Copy.ModuleName = RperfDuplicateAnsi(Symbol->ModuleName,
                                         Recording->Limits.MaxStringBytes);
    Copy.SourceFile = RperfDuplicateAnsi(Symbol->SourceFile,
                                         Recording->Limits.MaxStringBytes);
    if ((Symbol->Name != NULL && Copy.Name == NULL) ||
        (Symbol->ModuleName != NULL && Copy.ModuleName == NULL) ||
        (Symbol->SourceFile != NULL && Copy.SourceFile == NULL))
    {
        if (Copy.Name != NULL)
            HeapFree(GetProcessHeap(), 0, Copy.Name);
        if (Copy.ModuleName != NULL)
            HeapFree(GetProcessHeap(), 0, Copy.ModuleName);
        if (Copy.SourceFile != NULL)
            HeapFree(GetProcessHeap(), 0, Copy.SourceFile);
        if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    Recording->Symbols[Recording->SymbolCount++] = Copy;
    return TRUE;
}

BOOL
RperfRecordingAddRecord(RPERF_RECORDING *Recording,
                        const RPERF_RECORD *Record)
{
    if (Recording == NULL || Record == NULL || Recording->Frozen ||
        Record->Header.Kind < RperfRecordSessionInfo ||
        Record->Header.Kind > RperfRecordSessionEnd ||
        (Record->Header.Kind == RperfRecordSample &&
         (Record->Data.Sample.Depth == 0 ||
          Record->Data.Sample.Depth > Recording->Limits.MaxFrames ||
          Record->Data.Sample.Depth > RPERF_MODEL_MAX_FRAMES)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Record->Header.Kind == RperfRecordSample &&
        Recording->Counters.SuccessfulSamples >= Recording->Limits.MaxSamples)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    if (!RperfResize((PVOID *)&Recording->Records,
                     sizeof(*Recording->Records),
                     &Recording->RecordCapacity,
                     Recording->RecordCount + 1,
                     Recording->Limits.MaxRecords))
        return FALSE;
    Recording->Records[Recording->RecordCount++] = *Record;
    if (Record->Header.Kind == RperfRecordSample)
    {
        Recording->Counters.SuccessfulSamples++;
        if (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_TRUNCATED)
            Recording->Counters.TruncatedSamples++;
    }
    else if (Record->Header.Kind == RperfRecordLost)
    {
        Recording->Counters.LostRecords += Record->Data.Lost.Count;
        Recording->Counters.LostWeight += Record->Data.Lost.Weight;
    }
    if (Recording->Sink != NULL) Recording->Sink(Recording->SinkContext, Record);
    return TRUE;
}

VOID
RperfRecordingSetSink(RPERF_RECORDING *Recording,
                      RPERF_RECORD_SINK Sink,
                      PVOID SinkContext)
{
    if (Recording == NULL) return;
    Recording->Sink = Sink;
    Recording->SinkContext = SinkContext;
}

static int __cdecl
RperfCompareSymbols(const void *Left,
                    const void *Right)
{
    const RPERF_MODEL_SYMBOL *A = Left;
    const RPERF_MODEL_SYMBOL *B = Right;
    if (A->Address < B->Address)
        return -1;
    if (A->Address > B->Address)
        return 1;
    return 0;
}

BOOL
RperfRecordingFreeze(RPERF_RECORDING *Recording)
{
    SIZE_T Index;
    ULONGLONG Previous = 0;

    if (Recording == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Recording->Frozen)
        return TRUE;
    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        const RPERF_RECORD *Record = &Recording->Records[Index];
        if (Index != 0 && Record->Header.Sequence <= Previous)
        {
            SetLastError(ERROR_BAD_FORMAT);
            return FALSE;
        }
        Previous = Record->Header.Sequence;
    }
    if (Recording->SymbolCount != 0)
    {
        qsort(Recording->Symbols,
              Recording->SymbolCount,
              sizeof(*Recording->Symbols),
              RperfCompareSymbols);
    }
    Recording->Frozen = TRUE;
    return TRUE;
}

const RPERF_MODULE *
RperfRecordingFindModule(const RPERF_RECORDING *Recording,
                         ULONGLONG ModuleId)
{
    SIZE_T Index;
    if (Recording == NULL)
        return NULL;
    for (Index = 0; Index < Recording->ModuleCount; ++Index)
    {
        if (Recording->Modules[Index].Id == ModuleId)
            return &Recording->Modules[Index];
    }
    return NULL;
}

const RPERF_MODEL_SYMBOL *
RperfRecordingFindSymbol(const RPERF_RECORDING *Recording,
                         ULONGLONG Address)
{
    SIZE_T Low = 0, High;
    if (Recording == NULL || !Recording->Frozen)
        return NULL;
    High = Recording->SymbolCount;
    while (Low < High)
    {
        SIZE_T Middle = Low + (High - Low) / 2;
        if (Recording->Symbols[Middle].Address < Address)
            Low = Middle + 1;
        else
            High = Middle;
    }
    if (Low < Recording->SymbolCount &&
        Recording->Symbols[Low].Address == Address)
        return &Recording->Symbols[Low];
    return NULL;
}

BOOL
RperfRecordMatchesFilter(const RPERF_RECORDING *Recording,
                         const RPERF_RECORD *Record,
                         const RPERF_FILTER *Filter)
{
    USHORT Index;
    BOOL Match;

    UNREFERENCED_PARAMETER(Recording);
    if (Record == NULL || Filter == NULL)
        return FALSE;
    if (Record->Header.Kind == RperfRecordLost && !Filter->IncludeLoss)
        return FALSE;
    if ((Filter->Enabled & RPERF_FILTER_TIME) != 0 &&
        (Record->Header.TimestampNs < Filter->StartNs ||
         Record->Header.TimestampNs > Filter->EndNs))
        return FALSE;
    if ((Filter->Enabled & RPERF_FILTER_PROCESS) != 0 &&
        Record->Header.ProcessKey != Filter->ProcessKey)
        return FALSE;
    if ((Filter->Enabled & RPERF_FILTER_THREAD) != 0 &&
        Record->Header.ThreadKey != Filter->ThreadKey)
        return FALSE;
    if ((Filter->Enabled & RPERF_FILTER_CPU) != 0 &&
        Record->Header.Cpu != Filter->Cpu)
        return FALSE;
    if ((Filter->Enabled & RPERF_FILTER_EVENT) != 0 &&
        Record->Header.EventId != Filter->EventId)
        return FALSE;
    if ((Filter->Enabled & RPERF_FILTER_TRUNCATION) != 0 &&
        !Filter->IncludeTruncated &&
        (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_TRUNCATED) != 0)
        return FALSE;
    if (Record->Header.Kind != RperfRecordSample)
        return TRUE;

    if ((Filter->Enabled & (RPERF_FILTER_CONTEXT |
                            RPERF_FILTER_MODULE |
                            RPERF_FILTER_RESOLUTION)) == 0)
        return TRUE;
    for (Index = 0; Index < Record->Data.Sample.Depth; ++Index)
    {
        const RPERF_FRAME *Frame = &Record->Data.Sample.Frames[Index];
        Match = TRUE;
        if ((Filter->Enabled & RPERF_FILTER_CONTEXT) != 0 &&
            (Filter->ContextMask & (1UL << Frame->Context)) == 0)
            Match = FALSE;
        if ((Filter->Enabled & RPERF_FILTER_MODULE) != 0 &&
            Frame->ModuleId != Filter->ModuleId)
            Match = FALSE;
        if ((Filter->Enabled & RPERF_FILTER_RESOLUTION) != 0 &&
            (Filter->ResolutionMask & (1UL << Frame->Resolution)) == 0)
            Match = FALSE;
        if (Match)
            return TRUE;
    }
    return FALSE;
}

static RPERF_RECORDING *
RperfRecordingCloneInternal(const RPERF_RECORDING *Source,
                            BOOL Freeze)
{
    RPERF_RECORDING *Clone;
    SIZE_T Index;

    if (Source == NULL)
        return NULL;
    Clone = RperfRecordingCreate(&Source->Limits);
    if (Clone == NULL)
        return NULL;
    Clone->Info = Source->Info;
    Clone->Info.TargetName = NULL;
    Clone->Info.SourcePath = NULL;
    Clone->Counters = Source->Counters;
    if (!RperfRecordingSetTargetName(Clone, Source->Info.TargetName) ||
        !RperfRecordingSetSourcePath(Clone, Source->Info.SourcePath))
        goto Failure;
    for (Index = 0; Index < Source->ModuleCount; ++Index)
    {
        if (!RperfRecordingAddModule(Clone, &Source->Modules[Index]))
            goto Failure;
    }
    for (Index = 0; Index < Source->SymbolCount; ++Index)
    {
        if (!RperfRecordingAddSymbol(Clone, &Source->Symbols[Index]))
            goto Failure;
    }
    for (Index = 0; Index < Source->RecordCount; ++Index)
    {
        if (!RperfRecordingAddRecord(Clone, &Source->Records[Index]))
            goto Failure;
    }
    Clone->Counters = Source->Counters;
    if (Freeze && !RperfRecordingFreeze(Clone))
        goto Failure;
    return Clone;

Failure:
    RperfRecordingRelease(Clone);
    return NULL;
}

RPERF_RECORDING *
RperfRecordingClone(const RPERF_RECORDING *Source)
{
    return RperfRecordingCloneInternal(Source,
                                       Source != NULL && Source->Frozen);
}

RPERF_RECORDING *
RperfRecordingCloneMutable(const RPERF_RECORDING *Source)
{
    return RperfRecordingCloneInternal(Source, FALSE);
}
