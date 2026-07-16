/* Adapter from the portable recording model to the current GUI views. */

#include "profiler_legacy_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _RPERF_LEGACY_PROGRESS_STATE
{
    RPERF_SESSION_PROGRESS Callback;
    PVOID Context;
    ULONGLONG PhaseSpan;
    ULONGLONG Total;
} RPERF_LEGACY_PROGRESS_STATE;

static BOOL
RperfLegacyContinue(HANDLE CancelEvent)
{
    DWORD Wait;

    if (CancelEvent == NULL)
        return TRUE;
    Wait = WaitForSingleObject(CancelEvent, 0);
    if (Wait == WAIT_TIMEOUT)
        return TRUE;
    if (Wait == WAIT_OBJECT_0)
        SetLastError(ERROR_CANCELLED);
    else if (Wait != WAIT_FAILED)
        SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
}

static VOID
RperfLegacyReportPhase(RPERF_LEGACY_PROGRESS_STATE *State,
                       ULONG Phase,
                       ULONGLONG Completed,
                       ULONGLONG Total)
{
    ULONGLONG Scaled;

    if (State == NULL || State->Callback == NULL)
        return;
    if (Total == 0 || Completed >= Total)
        Scaled = State->PhaseSpan;
    else
        Scaled = (ULONGLONG)(((double)Completed *
                              (double)State->PhaseSpan) /
                             (double)Total);
    State->Callback(State->Context,
                    (ULONGLONG)Phase * State->PhaseSpan + Scaled,
                    State->Total);
}

static VOID CALLBACK
RperfLegacyAnalysisProgress(PVOID Context,
                            ULONGLONG Completed,
                            ULONGLONG Total)
{
    RPERF_LEGACY_PROGRESS_STATE *State = Context;
    ULONGLONG Scaled;

    if (State == NULL || State->Callback == NULL)
        return;
    if (Total == 0 || Completed >= Total)
        Scaled = State->PhaseSpan * 2;
    else
        Scaled = (ULONGLONG)(((double)Completed *
                              (double)(State->PhaseSpan * 2)) /
                             (double)Total);
    State->Callback(State->Context,
                    State->PhaseSpan * 4 + Scaled,
                    State->Total);
}

static int __cdecl
RperfLegacyCompareAddresses(const void *Left,
                            const void *Right)
{
    ULONGLONG A = *(const ULONGLONG *)Left;
    ULONGLONG B = *(const ULONGLONG *)Right;
    return A < B ? -1 : A > B ? 1 : 0;
}

static VOID
RperfLegacyCopyAnsi(PSTR Destination,
                    SIZE_T DestinationCount,
                    PCSTR Source)
{
    SIZE_T Length;

    if (DestinationCount == 0)
        return;
    if (Source == NULL)
    {
        Destination[0] = ANSI_NULL;
        return;
    }
    Length = strlen(Source);
    if (Length >= DestinationCount)
        Length = DestinationCount - 1;
    CopyMemory(Destination, Source, Length);
    Destination[Length] = ANSI_NULL;
}

static PCWSTR
RperfLegacyBaseName(PCWSTR Path)
{
    PCWSTR Slash, Backslash;

    if (Path == NULL)
        return L"<unknown>";
    Slash = wcsrchr(Path, L'/');
    Backslash = wcsrchr(Path, L'\\');
    if (Slash == NULL || (Backslash != NULL && Backslash > Slash))
        Slash = Backslash;
    return Slash != NULL ? Slash + 1 : Path;
}

static VOID
RperfLegacyModuleName(PSTR Destination,
                      SIZE_T DestinationCount,
                      PCWSTR Path)
{
    PCWSTR Name = RperfLegacyBaseName(Path);
    INT Result;

    if (DestinationCount == 0)
        return;
    Result = WideCharToMultiByte(CP_UTF8, 0, Name, -1,
                                 Destination, (INT)DestinationCount,
                                 NULL, NULL);
    if (Result == 0)
    {
        WideCharToMultiByte(CP_ACP, 0, Name, -1,
                            Destination, (INT)DestinationCount,
                            NULL, NULL);
    }
    Destination[DestinationCount - 1] = ANSI_NULL;
}

static const RPERF_MODULE *
RperfLegacyModuleForAddress(const RPERF_RECORDING *Recording,
                            ULONGLONG Address)
{
    SIZE_T Index;
    const RPERF_MODULE *Best = NULL;

    for (Index = 0; Index < Recording->ModuleCount; ++Index)
    {
        const RPERF_MODULE *Module = &Recording->Modules[Index];
        if (Address < Module->Base ||
            (Module->Size != 0 && Address - Module->Base >= Module->Size))
            continue;
        if (Best == NULL || Module->Base > Best->Base)
            Best = Module;
    }
    return Best;
}

static BOOL
RperfLegacyCollectAddresses(const RPERF_RECORDING *Recording,
                            HANDLE CancelEvent,
                            RPERF_LEGACY_PROGRESS_STATE *Progress,
                            ULONGLONG **Addresses,
                            SIZE_T *AddressCount)
{
    SIZE_T Index, Count = 0, SlotCount = 1, Slot;
    ULONGLONG *Values = NULL, *Slots = NULL;

    while (SlotCount < RPERF_MAX_SYMBOLS * 2)
        SlotCount *= 2;
    Slots = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                      SlotCount * sizeof(*Slots));
    if (Slots == NULL)
        return FALSE;

    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        const RPERF_RECORD *Record = &Recording->Records[Index];
        USHORT FrameIndex, Depth;

        if ((Index & 0xff) == 0 && !RperfLegacyContinue(CancelEvent))
            goto Failure;
        if ((Index & 0x3fff) == 0)
            RperfLegacyReportPhase(Progress, 2, Index,
                                   Recording->RecordCount);

        if (Record->Header.Kind != RperfRecordSample)
            continue;
        Depth = min(Record->Data.Sample.Depth, RPERF_MAX_FRAMES);
        for (FrameIndex = 0; FrameIndex < Depth; ++FrameIndex)
        {
            ULONGLONG Address =
                Record->Data.Sample.Frames[FrameIndex].Address;
            ULONGLONG Hash;

            if (Address == 0)
                continue;
            Hash = Address;
            Hash ^= Hash >> 33;
            Hash *= 0xff51afd7ed558ccdULL;
            Hash ^= Hash >> 33;
            Slot = (SIZE_T)(Hash ^ (Hash >> 32)) & (SlotCount - 1);
            while (Slots[Slot] != 0 && Slots[Slot] != Address)
                Slot = (Slot + 1) & (SlotCount - 1);
            if (Slots[Slot] == Address)
                continue;
            if (Count == RPERF_MAX_SYMBOLS)
            {
                SetLastError(ERROR_BUFFER_OVERFLOW);
                goto Failure;
            }
            Slots[Slot] = Address;
            Count++;
        }
    }
    if (!RperfLegacyContinue(CancelEvent))
        goto Failure;
    RperfLegacyReportPhase(Progress, 2, Recording->RecordCount,
                           Recording->RecordCount);
    if (Count == 0)
    {
        SetLastError(ERROR_NO_DATA);
        goto Failure;
    }
    Values = HeapAlloc(GetProcessHeap(), 0, Count * sizeof(*Values));
    if (Values == NULL)
        goto Failure;
    Count = 0;
    for (Slot = 0; Slot < SlotCount; ++Slot)
    {
        if (Slots[Slot] != 0)
            Values[Count++] = Slots[Slot];
    }
    HeapFree(GetProcessHeap(), 0, Slots);
    Slots = NULL;
    if (!RperfLegacyContinue(CancelEvent))
        goto Failure;
    qsort(Values, Count, sizeof(*Values), RperfLegacyCompareAddresses);
    if (!RperfLegacyContinue(CancelEvent))
        goto Failure;
    *Addresses = Values;
    *AddressCount = Count;
    return TRUE;

Failure:
    if (Slots != NULL)
        HeapFree(GetProcessHeap(), 0, Slots);
    if (Values != NULL)
        HeapFree(GetProcessHeap(), 0, Values);
    return FALSE;
}

static BOOL
RperfLegacyPopulateSymbols(const RPERF_RECORDING *Recording,
                           HANDLE CancelEvent,
                           RPERF_LEGACY_PROGRESS_STATE *Progress,
                           RPERF_SESSION *Session)
{
    ULONGLONG *Addresses = NULL;
    SIZE_T Count = 0, Index;

    if (!RperfLegacyCollectAddresses(Recording,
                                     CancelEvent,
                                     Progress,
                                     &Addresses,
                                     &Count))
        return FALSE;
    Session->Symbols = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                 Count * sizeof(*Session->Symbols));
    if (Session->Symbols == NULL)
    {
        HeapFree(GetProcessHeap(), 0, Addresses);
        return FALSE;
    }
    Session->SymbolCapacity = Count;
    Session->SymbolCount = Count;
    Session->SymbolsSorted = TRUE;
    for (Index = 0; Index < Count; ++Index)
    {
        ULONGLONG Address = Addresses[Index];
        const RPERF_MODEL_SYMBOL *ModelSymbol =
            RperfRecordingFindSymbol(Recording, Address);
        const RPERF_MODULE *Module = NULL;
        RPERF_SYMBOL *Symbol = &Session->Symbols[Index];

        if ((Index & 0xff) == 0 && !RperfLegacyContinue(CancelEvent))
            goto Failure;
        if ((Index & 0x3fff) == 0)
            RperfLegacyReportPhase(Progress, 3, Index, Count);

        if (ModelSymbol != NULL)
            Module = RperfRecordingFindModule(Recording,
                                              ModelSymbol->ModuleId);
        if (Module == NULL)
            Module = RperfLegacyModuleForAddress(Recording, Address);
        Symbol->Address = Address;
        Symbol->FunctionAddress = ModelSymbol != NULL &&
                                  ModelSymbol->FunctionAddress != 0 &&
                                  ModelSymbol->FunctionAddress <= Address ?
                                  ModelSymbol->FunctionAddress : Address;
        Symbol->ModuleBase = Module != NULL ? Module->Base : 0;
        Symbol->Displacement = Address - Symbol->FunctionAddress;
        if (ModelSymbol != NULL && ModelSymbol->ModuleName != NULL)
        {
            RperfLegacyCopyAnsi(Symbol->Module,
                                ARRAYSIZE(Symbol->Module),
                                ModelSymbol->ModuleName);
        }
        else if (Module != NULL)
        {
            RperfLegacyModuleName(Symbol->Module,
                                  ARRAYSIZE(Symbol->Module),
                                  Module->Path);
        }
        else
        {
            RperfLegacyCopyAnsi(Symbol->Module,
                                ARRAYSIZE(Symbol->Module),
                                "<unknown>");
        }
        if (ModelSymbol != NULL && ModelSymbol->Name != NULL &&
            ModelSymbol->Name[0] != ANSI_NULL)
        {
            RperfLegacyCopyAnsi(Symbol->Name,
                                ARRAYSIZE(Symbol->Name),
                                ModelSymbol->Name);
            Symbol->Source = ModelSymbol->Source;
            Symbol->Status = ModelSymbol->Status;
        }
        else
        {
            ULONGLONG Relative = Module != NULL && Address >= Module->Base ?
                                 Address - Module->Base : Address;
            _snprintf(Symbol->Name, ARRAYSIZE(Symbol->Name),
                      "+0x%I64x", Relative);
            Symbol->Name[ARRAYSIZE(Symbol->Name) - 1] = ANSI_NULL;
            Symbol->Source = RperfSymbolSourceModuleOffset;
            Symbol->Status = RperfSymbolStatusSymbolsMissing;
        }
    }
    if (!RperfLegacyContinue(CancelEvent))
        goto Failure;
    RperfLegacyReportPhase(Progress, 3, Count, Count);
    HeapFree(GetProcessHeap(), 0, Addresses);
    return TRUE;

Failure:
    HeapFree(GetProcessHeap(), 0, Addresses);
    return FALSE;
}

BOOL
RperfLegacySessionFromRecordingEx(const RPERF_RECORDING *Recording,
                                  PCWSTR SourcePath,
                                  HANDLE CancelEvent,
                                  RPERF_SESSION_PROGRESS Progress,
                                  PVOID ProgressContext,
                                  RPERF_SESSION *Session)
{
    RPERF_SESSION Loaded;
    SIZE_T Index, SampleCount = 0, SampleIndex = 0;
    ULONGLONG StartNs, EndNs;
    RPERF_LEGACY_PROGRESS_STATE ProgressState;

    if (Recording == NULL || !Recording->Frozen || Session == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Recording->RecordCount > ((ULONGLONG)-1) / 6)
    {
        SetLastError(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }
    ZeroMemory(&ProgressState, sizeof(ProgressState));
    ProgressState.Callback = Progress;
    ProgressState.Context = ProgressContext;
    ProgressState.PhaseSpan = Recording->RecordCount;
    ProgressState.Total = (ULONGLONG)Recording->RecordCount * 6;
    if (!RperfLegacyContinue(CancelEvent))
        return FALSE;
    RperfLegacyReportPhase(&ProgressState, 0, 0,
                           Recording->RecordCount);
    RperfSessionInitialize(&Loaded);
    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        if ((Index & 0xff) == 0 && !RperfLegacyContinue(CancelEvent))
            goto Failure;
        if ((Index & 0x3fff) == 0)
            RperfLegacyReportPhase(&ProgressState, 0, Index,
                                   Recording->RecordCount);
        if (Recording->Records[Index].Header.Kind == RperfRecordSample)
            SampleCount++;
    }
    RperfLegacyReportPhase(&ProgressState, 0, Recording->RecordCount,
                           Recording->RecordCount);
    if (SampleCount == 0 || SampleCount > RPERF_MAX_SAMPLES)
    {
        SetLastError(SampleCount == 0 ? ERROR_NO_DATA :
                     ERROR_BUFFER_OVERFLOW);
        goto Failure;
    }
    Loaded.Samples = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                               SampleCount * sizeof(*Loaded.Samples));
    if (Loaded.Samples == NULL)
        goto Failure;
    Loaded.SampleCapacity = SampleCount;
    Loaded.SampleCount = SampleCount;
    Loaded.Backend = Recording->Info.Backend;
    Loaded.IntervalMs = Recording->Info.IntervalMs;
    Loaded.RequestedDurationMs = Recording->Info.RequestedDurationMs;
    Loaded.CompletionReason = Recording->Info.CompletionReason;
    Loaded.CaptureError = Recording->Info.CompletionError;
    Loaded.LogComplete = Recording->Info.Complete;
    Loaded.Counters = Recording->Counters;
    Loaded.MissedThreads = Recording->Counters.FailedSamples;
    Loaded.MissedTicks = Recording->Counters.MissedCadenceTicks;
    Loaded.TruncatedStacks = Recording->Counters.TruncatedSamples;
    Loaded.LostSamples = Recording->Counters.LostRecords;
    if (Recording->Info.TargetName != NULL)
        lstrcpynW(Loaded.ProcessName, Recording->Info.TargetName,
                  ARRAYSIZE(Loaded.ProcessName));
    if (SourcePath != NULL)
        lstrcpynW(Loaded.SourcePath, SourcePath,
                  ARRAYSIZE(Loaded.SourcePath));
    else if (Recording->Info.SourcePath != NULL)
        lstrcpynW(Loaded.SourcePath, Recording->Info.SourcePath,
                  ARRAYSIZE(Loaded.SourcePath));
    StartNs = Recording->Info.StartTimeNs;
    EndNs = Recording->Info.EndTimeNs;
    Loaded.ElapsedUs = EndNs >= StartNs ?
                       (EndNs - StartNs) / 1000 : EndNs / 1000;
    for (Index = 0; Index < Recording->RecordCount; ++Index)
    {
        const RPERF_RECORD *Record = &Recording->Records[Index];
        RPERF_SAMPLE *Sample;
        USHORT FrameIndex;

        if ((Index & 0xff) == 0 && !RperfLegacyContinue(CancelEvent))
            goto Failure;
        if ((Index & 0x3fff) == 0)
            RperfLegacyReportPhase(&ProgressState, 1, Index,
                                   Recording->RecordCount);

        if (Record->Header.Kind != RperfRecordSample)
            continue;
        Sample = &Loaded.Samples[SampleIndex++];
        Sample->TimeUs = Record->Header.TimestampNs >= StartNs ?
                         (Record->Header.TimestampNs - StartNs) / 1000 :
                         Record->Header.TimestampNs / 1000;
        Sample->ProcessId = Record->Header.ProcessId;
        Sample->ThreadId = Record->Header.ThreadId;
        Sample->Depth = min(Record->Data.Sample.Depth,
                            RPERF_MAX_FRAMES);
        if (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_TRUNCATED ||
            Record->Data.Sample.Depth > RPERF_MAX_FRAMES)
            Sample->Flags |= RPERF_SAMPLE_TRUNCATED;
        if (Record->Header.Flags & RPERF_MODEL_RECORD_FLAG_UNWIND_FAILED)
            Sample->Flags |= RPERF_SAMPLE_UNWIND_FAILED;
        /* Recorder backends interrupt only threads that were on a CPU, so
         * their samples always carry a known running state. */
        Sample->Flags |= RPERF_SAMPLE_STATE_KNOWN;
        if (Loaded.StateTaggedSamples != (ULONGLONG)-1) Loaded.StateTaggedSamples++;
        for (FrameIndex = 0; FrameIndex < Sample->Depth; ++FrameIndex)
            Sample->Frames[FrameIndex] =
                Record->Data.Sample.Frames[FrameIndex].Address;
        if (Loaded.ProcessId == 0 && Sample->ProcessId != 0)
            Loaded.ProcessId = Sample->ProcessId;
        if (Sample->TimeUs > Loaded.ElapsedUs)
            Loaded.ElapsedUs = Sample->TimeUs;
    }
    RperfLegacyReportPhase(&ProgressState, 1, Recording->RecordCount,
                           Recording->RecordCount);
    if (!RperfLegacyPopulateSymbols(Recording,
                                    CancelEvent,
                                    &ProgressState,
                                    &Loaded) ||
        !RperfBuildFilteredAnalysisEx(&Loaded, 0, 0, (ULONGLONG)-1, 0, CancelEvent, RperfLegacyAnalysisProgress, &ProgressState))
        goto Failure;
    RperfSessionClear(Session);
    *Session = Loaded;
    return TRUE;

Failure:
    RperfSessionClear(&Loaded);
    return FALSE;
}

BOOL
RperfLegacySessionFromRecording(const RPERF_RECORDING *Recording,
                                PCWSTR SourcePath,
                                RPERF_SESSION *Session)
{
    return RperfLegacySessionFromRecordingEx(Recording,
                                              SourcePath,
                                              NULL,
                                              NULL,
                                              NULL,
                                              Session);
}
