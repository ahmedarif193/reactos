/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Backend-independent symbol provider and bounded cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_symbolizer.h"

#include <string.h>

typedef struct _RPERF_SYMBOL_CACHE_ENTRY
{
    ULONGLONG ModuleId;
    ULONGLONG Address;
    BOOL Valid;
    BOOL Resolved;
    RPERF_SYMBOL_RESULT Result;
} RPERF_SYMBOL_CACHE_ENTRY;

typedef struct _RPERF_CACHED_PROVIDER
{
    RPERF_SYMBOL_PROVIDER_OPS Inner;
    PVOID InnerContext;
    RPERF_SYMBOL_CACHE_ENTRY *Entries;
    SIZE_T Count;
    SIZE_T Capacity;
    SIZE_T Maximum;
    SIZE_T NextReplacement;
} RPERF_CACHED_PROVIDER;

static BOOL
RperfCachedResolve(PVOID Opaque,
                   const RPERF_MODULE *Module,
                   ULONGLONG Address,
                   RPERF_SYMBOL_RESULT *Result)
{
    RPERF_CACHED_PROVIDER *Cache = Opaque;
    ULONGLONG ModuleId = Module != NULL ? Module->Id : RPERF_MODEL_INVALID_ID;
    SIZE_T Index, Slot;
    BOOL Resolved;

    for (Index = 0; Index < Cache->Count; ++Index)
    {
        if (Cache->Entries[Index].Valid &&
            Cache->Entries[Index].ModuleId == ModuleId &&
            Cache->Entries[Index].Address == Address)
        {
            if (Cache->Entries[Index].Resolved)
                *Result = Cache->Entries[Index].Result;
            else
                SetLastError(ERROR_NOT_FOUND);
            return Cache->Entries[Index].Resolved;
        }
    }
    ZeroMemory(Result, sizeof(*Result));
    Resolved = Cache->Inner.Resolve(Cache->InnerContext,
                                    Module, Address, Result);
    if (Cache->Maximum == 0)
        return Resolved;
    if (Cache->Count < Cache->Maximum)
    {
        if (Cache->Count == Cache->Capacity)
        {
            SIZE_T NewCapacity = Cache->Capacity != 0 ?
                                 Cache->Capacity * 2 : 256;
            PVOID NewEntries;
            if (NewCapacity > Cache->Maximum)
                NewCapacity = Cache->Maximum;
            if (NewCapacity > ((SIZE_T)-1) / sizeof(*Cache->Entries))
                return Resolved;
            if (Cache->Entries != NULL)
                NewEntries = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                         Cache->Entries,
                                         NewCapacity * sizeof(*Cache->Entries));
            else
                NewEntries = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       NewCapacity * sizeof(*Cache->Entries));
            if (NewEntries == NULL)
                return Resolved; /* cache failure never hides a symbol result */
            Cache->Entries = NewEntries;
            Cache->Capacity = NewCapacity;
        }
        Slot = Cache->Count++;
    }
    else
    {
        Slot = Cache->NextReplacement++ % Cache->Maximum;
    }
    Cache->Entries[Slot].Valid = TRUE;
    Cache->Entries[Slot].ModuleId = ModuleId;
    Cache->Entries[Slot].Address = Address;
    Cache->Entries[Slot].Resolved = Resolved;
    if (Resolved)
        Cache->Entries[Slot].Result = *Result;
    return Resolved;
}

static VOID
RperfCachedDestroy(PVOID Opaque)
{
    RPERF_CACHED_PROVIDER *Cache = Opaque;
    if (Cache->Inner.Destroy != NULL)
        Cache->Inner.Destroy(Cache->InnerContext);
    if (Cache->Entries != NULL)
        HeapFree(GetProcessHeap(), 0, Cache->Entries);
    HeapFree(GetProcessHeap(), 0, Cache);
}

static BOOL
RperfCachedQuerySummary(PVOID Opaque,
                        RPERF_SYMBOLIZATION_SUMMARY *Summary)
{
    RPERF_CACHED_PROVIDER *Cache = Opaque;

    if (Cache->Inner.QuerySummary == NULL)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    return Cache->Inner.QuerySummary(Cache->InnerContext, Summary);
}

static const RPERF_SYMBOL_PROVIDER_OPS RperfCachedOps =
{
    RperfCachedResolve,
    RperfCachedQuerySummary,
    RperfCachedDestroy
};

static RPERF_MODEL_SYMBOL *
RperfFindMutableSymbol(const RPERF_RECORDING *Recording,
                       ULONGLONG Address)
{
    SIZE_T Index;
    for (Index = 0; Index < Recording->SymbolCount; ++Index)
    {
        if (Recording->Symbols[Index].Address == Address)
            return &((RPERF_RECORDING *)Recording)->Symbols[Index];
    }
    return NULL;
}

static PSTR
RperfSymbolizerDuplicate(PCSTR Text,
                         ULONG MaximumBytes)
{
    SIZE_T Length;
    PSTR Copy;

    if (Text == NULL)
        return NULL;
    Length = strlen(Text);
    if (Length >= MaximumBytes || Length == (SIZE_T)-1)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return NULL;
    }
    Copy = HeapAlloc(GetProcessHeap(), 0, Length + 1);
    if (Copy != NULL)
        CopyMemory(Copy, Text, Length + 1);
    return Copy;
}

static BOOL
RperfReplaceSymbol(RPERF_RECORDING *Recording,
                   RPERF_MODEL_SYMBOL *Symbol,
                   const RPERF_MODULE *Module,
                   ULONGLONG Address,
                   const RPERF_SYMBOL_RESULT *Resolved)
{
    PSTR Name = NULL, ModuleName = NULL, SourceFile = NULL;

    Name = RperfSymbolizerDuplicate(Resolved->Name,
                                    Recording->Limits.MaxStringBytes);
    ModuleName = RperfSymbolizerDuplicate(
        Resolved->ModuleName, Recording->Limits.MaxStringBytes);
    SourceFile = RperfSymbolizerDuplicate(
        Resolved->SourceFile, Recording->Limits.MaxStringBytes);
    if ((Resolved->Name[0] != ANSI_NULL && Name == NULL) ||
        (Resolved->ModuleName[0] != ANSI_NULL && ModuleName == NULL) ||
        (Resolved->SourceFile[0] != ANSI_NULL && SourceFile == NULL))
    {
        if (Name != NULL) HeapFree(GetProcessHeap(), 0, Name);
        if (ModuleName != NULL) HeapFree(GetProcessHeap(), 0, ModuleName);
        if (SourceFile != NULL) HeapFree(GetProcessHeap(), 0, SourceFile);
        return FALSE;
    }
    if (Symbol->Name != NULL)
        HeapFree(GetProcessHeap(), 0, Symbol->Name);
    if (Symbol->ModuleName != NULL)
        HeapFree(GetProcessHeap(), 0, Symbol->ModuleName);
    if (Symbol->SourceFile != NULL)
        HeapFree(GetProcessHeap(), 0, Symbol->SourceFile);
    Symbol->Address = Address;
    Symbol->FunctionAddress = Resolved->FunctionAddress != 0 ?
                              Resolved->FunctionAddress : Address;
    Symbol->RelativeAddress = Module != NULL && Address >= Module->Base ?
                              Address - Module->Base : Address;
    Symbol->Resolution = Resolved->Resolution;
    Symbol->Source = Resolved->Source;
    Symbol->Status = Resolved->Status;
    Symbol->Name = Name;
    Symbol->ModuleName = ModuleName;
    Symbol->SourceFile = SourceFile;
    Symbol->SourceLine = Resolved->SourceLine;
    return TRUE;
}

RPERF_SYMBOL_PROVIDER *
RperfCreateCachedSymbolProvider(const RPERF_SYMBOL_PROVIDER_OPS *Ops,
                                PVOID Context,
                                SIZE_T MaximumEntries)
{
    RPERF_SYMBOL_PROVIDER *Provider;
    RPERF_CACHED_PROVIDER *Cache;
    if (Ops == NULL || Ops->Resolve == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    Provider = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         sizeof(*Provider));
    Cache = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Cache));
    if (Provider == NULL || Cache == NULL)
    {
        if (Provider != NULL)
            HeapFree(GetProcessHeap(), 0, Provider);
        if (Cache != NULL)
            HeapFree(GetProcessHeap(), 0, Cache);
        return NULL;
    }
    Cache->Inner = *Ops;
    Cache->InnerContext = Context;
    Cache->Maximum = MaximumEntries;
    Provider->Ops = &RperfCachedOps;
    Provider->Context = Cache;
    return Provider;
}

VOID
RperfDestroySymbolProvider(RPERF_SYMBOL_PROVIDER *Provider)
{
    if (Provider == NULL)
        return;
    if (Provider->Ops != NULL && Provider->Ops->Destroy != NULL)
        Provider->Ops->Destroy(Provider->Context);
    HeapFree(GetProcessHeap(), 0, Provider);
}

BOOL
RperfQuerySymbolProviderSummary(RPERF_SYMBOL_PROVIDER *Provider,
                                RPERF_SYMBOLIZATION_SUMMARY *Summary)
{
    if (Provider == NULL || Provider->Ops == NULL ||
        Provider->Ops->QuerySummary == NULL || Summary == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(Summary, sizeof(*Summary));
    return Provider->Ops->QuerySummary(Provider->Context, Summary);
}

BOOL
RperfSymbolizeRecording(const RPERF_RECORDING *Source,
                        RPERF_SYMBOL_PROVIDER *Provider,
                        HANDLE CancelEvent,
                        RPERF_SYMBOL_PROGRESS Progress,
                        PVOID ProgressContext,
                        RPERF_RECORDING **Output)
{
    RPERF_RECORDING *Result;
    SIZE_T RecordIndex;

    if (Source == NULL || !Source->Frozen || Provider == NULL ||
        Provider->Ops == NULL || Provider->Ops->Resolve == NULL ||
        Output == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *Output = NULL;
    Result = RperfRecordingCloneMutable(Source);
    if (Result == NULL)
        return FALSE;
    for (RecordIndex = 0; RecordIndex < Result->RecordCount; ++RecordIndex)
    {
        RPERF_RECORD *Record = &Result->Records[RecordIndex];
        USHORT FrameIndex;
        if ((RecordIndex & 1023) == 0)
        {
            if (CancelEvent != NULL &&
                WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
            {
                SetLastError(ERROR_CANCELLED);
                goto Failure;
            }
            if (Progress != NULL)
                Progress(ProgressContext, RecordIndex, Result->RecordCount);
        }
        if (Record->Header.Kind != RperfRecordSample)
            continue;
        for (FrameIndex = 0;
             FrameIndex < Record->Data.Sample.Depth;
             ++FrameIndex)
        {
            RPERF_FRAME *Frame = &Record->Data.Sample.Frames[FrameIndex];
            RPERF_MODEL_SYMBOL *Existing;
            const RPERF_MODULE *Module;
            RPERF_SYMBOL_RESULT Resolved;
            RPERF_MODEL_SYMBOL Symbol;

            Existing = RperfFindMutableSymbol(Result, Frame->Address);
            if (Existing != NULL &&
                Existing->Resolution >= RperfResolutionSource &&
                Existing->Status == RperfSymbolStatusResolved)
            {
                Frame->FunctionAddress = Existing->FunctionAddress;
                Frame->ModuleId = Existing->ModuleId;
                Frame->Resolution = Existing->Resolution;
                continue;
            }
            Module = RperfRecordingFindModule(Result, Frame->ModuleId);
            ZeroMemory(&Resolved, sizeof(Resolved));
            if (!Provider->Ops->Resolve(Provider->Context,
                                        Module,
                                        Frame->Address,
                                        &Resolved))
            {
                if (Existing != NULL)
                {
                    Frame->FunctionAddress = Existing->FunctionAddress;
                    Frame->ModuleId = Existing->ModuleId;
                    Frame->Resolution = Existing->Resolution;
                }
                continue;
            }
            if (Existing != NULL)
            {
                BOOL Better = Resolved.Resolution > Existing->Resolution ||
                    (Resolved.Resolution == Existing->Resolution &&
                     Resolved.Status == RperfSymbolStatusResolved &&
                     (Existing->Status != RperfSymbolStatusResolved ||
                      Existing->Source == RperfSymbolSourceUnknown));

                if (Better && !RperfReplaceSymbol(Result, Existing, Module,
                                                   Frame->Address,
                                                   &Resolved))
                    goto Failure;
                Frame->FunctionAddress = Existing->FunctionAddress;
                Frame->ModuleId = Existing->ModuleId;
                Frame->Resolution = Existing->Resolution;
                continue;
            }
            ZeroMemory(&Symbol, sizeof(Symbol));
            Symbol.Address = Frame->Address;
            Symbol.FunctionAddress = Resolved.FunctionAddress != 0 ?
                                     Resolved.FunctionAddress : Frame->Address;
            Symbol.ModuleId = Frame->ModuleId;
            Symbol.RelativeAddress = Module != NULL &&
                                     Frame->Address >= Module->Base ?
                                     Frame->Address - Module->Base :
                                     Frame->Address;
            Symbol.Resolution = Resolved.Resolution;
            Symbol.Source = Resolved.Source;
            Symbol.Status = Resolved.Status;
            Symbol.Name = Resolved.Name;
            Symbol.ModuleName = Resolved.ModuleName;
            Symbol.SourceFile = Resolved.SourceFile;
            Symbol.SourceLine = Resolved.SourceLine;
            if (!RperfRecordingAddSymbol(Result, &Symbol))
                goto Failure;
            Frame->FunctionAddress = Symbol.FunctionAddress;
            Frame->Resolution = Symbol.Resolution;
        }
    }
    if (!RperfRecordingFreeze(Result))
        goto Failure;
    if (Progress != NULL)
        Progress(ProgressContext, Result->RecordCount, Result->RecordCount);
    *Output = Result;
    return TRUE;

Failure:
    RperfRecordingRelease(Result);
    return FALSE;
}
