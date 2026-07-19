/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Intrusive and deterministic fake recorder backends
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_recorder_internal.h"
#include "profiler_codec.h"
#include "rosprofiler.h"

#include <reactos/rperf.h>

typedef struct _RPERF_INTRUSIVE_STATE
{
    RPERF_SESSION Legacy;
    RPERF_CAPTURE_CONFIGURATION Config;
    PWSTR TargetName;
    PWSTR OutputPath;
    RPERF_RECORDING *Recording;
} RPERF_INTRUSIVE_STATE;

typedef struct _RPERF_FAKE_STATE
{
    RPERF_CAPTURE_CONFIGURATION Config;
    HANDLE StopEvent;
    HANDLE Worker;
    RPERF_RECORDING *Recording;
    DWORD Error;
} RPERF_FAKE_STATE;

static PWSTR
RperfRecorderDuplicate(PCWSTR Text)
{
    SIZE_T Bytes;
    PWSTR Result;
    if (Text == NULL)
        return NULL;
    Bytes = (wcslen(Text) + 1) * sizeof(WCHAR);
    Result = HeapAlloc(GetProcessHeap(), 0, Bytes);
    if (Result != NULL)
        CopyMemory(Result, Text, Bytes);
    return Result;
}

BOOL
RperfIntrusiveQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Capabilities)
{
    Capabilities->Available = TRUE;
    Capabilities->Features = RPERF_CAP_TIMER | RPERF_CAP_USER_STACKS |
                             RPERF_CAP_STACK_WALK;
    Capabilities->MinimumIntervalUs = 1000;
    Capabilities->MaximumStackDepth = RPERF_MAX_FRAMES;
    Capabilities->AbiVersion = 1;
    lstrcpyW(Capabilities->Description,
             L"Intrusive userspace all-thread wall-clock snapshots.");
    return TRUE;
}

static BOOL
RperfIntrusiveStart(PVOID Opaque)
{
    RPERF_INTRUSIVE_STATE *State = Opaque;
    ULONG IntervalMs = (State->Config.IntervalUs + 999) / 1000;
    return RperfCaptureStart(&State->Legacy,
                             State->Config.ProcessId,
                             State->TargetName,
                             IntervalMs,
                             State->Config.DurationMs,
                             State->OutputPath,
                             State->Config.LegacyNotifyWindow);
}

static BOOL
RperfIntrusiveStop(PVOID Opaque)
{
    RPERF_INTRUSIVE_STATE *State = Opaque;
    RperfCaptureStop(&State->Legacy);
    return TRUE;
}

static BOOL
RperfIntrusiveJoin(PVOID Opaque,
                   DWORD Timeout)
{
    RPERF_INTRUSIVE_STATE *State = Opaque;
    DWORD Wait;

    if (State->Legacy.WorkerThread == NULL)
    {
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    Wait = WaitForSingleObject(State->Legacy.WorkerThread, Timeout);
    if (Wait == WAIT_TIMEOUT)
    {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    if (Wait != WAIT_OBJECT_0)
        return FALSE;
    RperfCaptureWait(&State->Legacy);
    if (State->Legacy.SampleCount == 0)
    {
        SetLastError(State->Legacy.CaptureError != ERROR_SUCCESS ?
                     State->Legacy.CaptureError : ERROR_NO_DATA);
        return FALSE;
    }
    State->Recording = RperfRecordingFromLegacySession(
        &State->Legacy, &State->Config.Limits);
    return State->Recording != NULL;
}

static BOOL
RperfIntrusiveCounters(PVOID Opaque,
                       RPERF_CAPTURE_COUNTERS *Counters)
{
    RPERF_INTRUSIVE_STATE *State = Opaque;
    ZeroMemory(Counters, sizeof(*Counters));
    if (State->Recording != NULL)
        *Counters = State->Recording->Counters;
    else
        *Counters = State->Legacy.Counters;
    return TRUE;
}

static RPERF_RECORDING *
RperfIntrusiveTake(PVOID Opaque)
{
    RPERF_INTRUSIVE_STATE *State = Opaque;
    if (State->Recording != NULL)
        RperfRecordingAddRef(State->Recording);
    return State->Recording;
}

static VOID
RperfIntrusiveDestroy(PVOID Opaque)
{
    RPERF_INTRUSIVE_STATE *State = Opaque;
    RperfSessionClear(&State->Legacy);
    if (State->Recording != NULL)
        RperfRecordingRelease(State->Recording);
    if (State->TargetName != NULL)
        HeapFree(GetProcessHeap(), 0, State->TargetName);
    if (State->OutputPath != NULL)
        HeapFree(GetProcessHeap(), 0, State->OutputPath);
    HeapFree(GetProcessHeap(), 0, State);
}

static const RPERF_RECORDER_OPS RperfIntrusiveOps =
{
    RperfIntrusiveStart,
    RperfIntrusiveStop,
    RperfIntrusiveJoin,
    RperfIntrusiveCounters,
    RperfIntrusiveTake,
    RperfIntrusiveDestroy
};

BOOL
RperfIntrusiveCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                     const RPERF_RECORDER_OPS **Ops,
                     PVOID *Opaque)
{
    RPERF_INTRUSIVE_STATE *State;
    if (Configuration->OutputPath == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    State = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*State));
    if (State == NULL)
        return FALSE;
    State->Config = *Configuration;
    State->TargetName = RperfRecorderDuplicate(Configuration->TargetName);
    State->OutputPath = RperfRecorderDuplicate(Configuration->OutputPath);
    if (State->OutputPath == NULL ||
        (Configuration->TargetName != NULL && State->TargetName == NULL))
    {
        RperfIntrusiveDestroy(State);
        return FALSE;
    }
    RperfSessionInitialize(&State->Legacy);
    *Ops = &RperfIntrusiveOps;
    *Opaque = State;
    return TRUE;
}

BOOL
RperfFakeQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Capabilities)
{
    Capabilities->Available = TRUE;
    Capabilities->Features = RPERF_CAP_TIMER | RPERF_CAP_USER_STACKS |
                             RPERF_CAP_KERNEL_STACKS | RPERF_CAP_PMU |
                             RPERF_CAP_PROCESS_EVENTS |
                             RPERF_CAP_THREAD_EVENTS |
                             RPERF_CAP_IMAGE_EVENTS |
                             RPERF_CAP_SCHEDULER_EVENTS |
                             RPERF_CAP_LOSS_ACCOUNTING |
                             RPERF_CAP_PROCESS_TREE |
                             RPERF_CAP_SYSTEM_WIDE |
                             RPERF_CAP_STACK_WALK;
    Capabilities->MinimumIntervalUs = 1;
    Capabilities->MaximumStackDepth = RPERF_MODEL_MAX_FRAMES;
    Capabilities->AbiVersion = RPERF_MODEL_VERSION;
    lstrcpyW(Capabilities->Description,
             L"Deterministic synthetic recorder for contract tests.");
    return TRUE;
}

static DWORD WINAPI
RperfFakeWorker(PVOID Opaque)
{
    RPERF_FAKE_STATE *State = Opaque;
    RPERF_RECORDING *Recording;
    RPERF_MODULE Module;
    RPERF_MODEL_SYMBOL Symbols[3];
    ULONGLONG Sequence = 1;
    ULONG Index, Count;

    Recording = RperfRecordingCreate(&State->Config.Limits);
    if (Recording == NULL)
        goto Failure;
    RperfRecordingSetSink(Recording, State->Config.RecordSink, State->Config.RecordSinkContext);
    Recording->Info.Backend = RperfBackendFake;
    Recording->Info.Metric = RperfMetricCpuSamples;
    Recording->Info.ProducerArchitecture = RperfNativeArchitecture();
    Recording->Info.AddressWidth = sizeof(PVOID) * 8;
    Recording->Info.IntervalMs = (State->Config.IntervalUs + 999) / 1000;
    Recording->Info.RequestedDurationMs = State->Config.DurationMs;
    if (!RperfRecordingSetTargetName(Recording,
                                     State->Config.TargetName != NULL ?
                                     State->Config.TargetName : L"fake-target"))
        goto Failure;
    ZeroMemory(&Module, sizeof(Module));
    Module.Id = 1;
    Module.ProcessKey = State->Config.ProcessId;
    Module.Base = 0x10000000;
    Module.Size = 0x10000;
    Module.Architecture = Recording->Info.ProducerArchitecture;
    Module.TimeDateStamp = 0x5a17c0de;
    Module.Checksum = 0x12345678;
    Module.DebugId[0] = 0x42;
    Module.DebugId[15] = 0xa5;
    Module.DebugAge = 7;
    Module.Path = L"fake-target.exe";
    if (!RperfRecordingAddModule(Recording, &Module))
        goto Failure;
    ZeroMemory(Symbols, sizeof(Symbols));
    Symbols[0].Address = Symbols[0].FunctionAddress = 0x10001000;
    Symbols[0].ModuleId = 1;
    Symbols[0].RelativeAddress = 0x1000;
    Symbols[0].Resolution = RperfResolutionFunction;
    Symbols[0].Source = RperfSymbolSourceRosSym;
    Symbols[0].Status = RperfSymbolStatusResolved;
    Symbols[0].Name = "fake_leaf";
    Symbols[1] = Symbols[0];
    Symbols[1].Address = Symbols[1].FunctionAddress = 0x10002000;
    Symbols[1].RelativeAddress = 0x2000;
    Symbols[1].Name = "fake_worker";
    Symbols[2] = Symbols[0];
    Symbols[2].Address = Symbols[2].FunctionAddress = 0x10003000;
    Symbols[2].RelativeAddress = 0x3000;
    Symbols[2].Name = "fake_root";
    for (Index = 0; Index < ARRAYSIZE(Symbols); ++Index)
    {
        if (!RperfRecordingAddSymbol(Recording, &Symbols[Index]))
            goto Failure;
    }
    Count = State->Config.DurationMs != 0 ?
            max(1, (State->Config.DurationMs * 1000) /
                   State->Config.IntervalUs) : 100;
    if (Count > State->Config.Limits.MaxSamples)
        Count = (ULONG)State->Config.Limits.MaxSamples;
    for (Index = 0; Index < Count; ++Index)
    {
        RPERF_RECORD Record;
        if (WaitForSingleObject(State->StopEvent, 0) == WAIT_OBJECT_0)
            break;
        ZeroMemory(&Record, sizeof(Record));
        Record.Header.Kind = RperfRecordSample;
        Record.Header.Sequence = Sequence++;
        Record.Header.TimestampNs =
            (ULONGLONG)Index * State->Config.IntervalUs * 1000;
        Record.Header.ProcessId = State->Config.ProcessId;
        Record.Header.ThreadId = 100 + (Index & 1);
        Record.Header.ProcessKey = State->Config.ProcessId;
        Record.Header.ThreadKey = Record.Header.ThreadId;
        Record.Header.Cpu = Index & 3;
        Record.Header.EventId = State->Config.EventId;
        Record.Data.Sample.Weight = 1;
        Record.Data.Sample.Period = State->Config.Period;
        Record.Data.Sample.Depth = 3;
        Record.Data.Sample.Frames[0].Address = 0x10001000;
        Record.Data.Sample.Frames[1].Address = 0x10002000;
        Record.Data.Sample.Frames[2].Address = 0x10003000;
        {
            USHORT Frame;
            for (Frame = 0; Frame < 3; ++Frame)
            {
                Record.Data.Sample.Frames[Frame].FunctionAddress =
                    Record.Data.Sample.Frames[Frame].Address;
                Record.Data.Sample.Frames[Frame].ModuleId = 1;
                Record.Data.Sample.Frames[Frame].Context = RperfContextUser;
                Record.Data.Sample.Frames[Frame].Resolution =
                    RperfResolutionFunction;
            }
        }
        if (!RperfRecordingAddRecord(Recording, &Record))
            goto Failure;
    }
    Recording->Info.EndTimeNs =
        (ULONGLONG)Index * State->Config.IntervalUs * 1000;
    Recording->Info.Complete = TRUE;
    Recording->Info.CompletionReason =
        WaitForSingleObject(State->StopEvent, 0) == WAIT_OBJECT_0 ? 2 : 1;
    Recording->Counters.AttemptedSamples = Recording->Counters.SuccessfulSamples;
    if (!RperfRecordingFreeze(Recording))
        goto Failure;
    State->Recording = Recording;
    return ERROR_SUCCESS;

Failure:
    State->Error = GetLastError();
    if (Recording != NULL)
        RperfRecordingRelease(Recording);
    return State->Error != ERROR_SUCCESS ? State->Error : ERROR_GEN_FAILURE;
}

static BOOL
RperfFakeStart(PVOID Opaque)
{
    RPERF_FAKE_STATE *State = Opaque;
    State->Worker = CreateThread(NULL, 0, RperfFakeWorker, State, 0, NULL);
    return State->Worker != NULL;
}

static BOOL
RperfFakeStop(PVOID Opaque)
{
    RPERF_FAKE_STATE *State = Opaque;
    return SetEvent(State->StopEvent);
}

static BOOL
RperfFakeJoin(PVOID Opaque,
              DWORD Timeout)
{
    RPERF_FAKE_STATE *State = Opaque;
    DWORD Wait = WaitForSingleObject(State->Worker, Timeout);
    if (Wait == WAIT_TIMEOUT)
        SetLastError(ERROR_TIMEOUT);
    if (Wait != WAIT_OBJECT_0)
        return FALSE;
    if (State->Recording == NULL)
    {
        SetLastError(State->Error != ERROR_SUCCESS ?
                     State->Error : ERROR_GEN_FAILURE);
        return FALSE;
    }
    return TRUE;
}

static BOOL
RperfFakeCounters(PVOID Opaque,
                  RPERF_CAPTURE_COUNTERS *Counters)
{
    RPERF_FAKE_STATE *State = Opaque;
    ZeroMemory(Counters, sizeof(*Counters));
    if (State->Recording != NULL)
        *Counters = State->Recording->Counters;
    return TRUE;
}

static RPERF_RECORDING *
RperfFakeTake(PVOID Opaque)
{
    RPERF_FAKE_STATE *State = Opaque;
    if (State->Recording != NULL)
        RperfRecordingAddRef(State->Recording);
    return State->Recording;
}

static VOID
RperfFakeDestroy(PVOID Opaque)
{
    RPERF_FAKE_STATE *State = Opaque;
    if (State->Worker != NULL)
        CloseHandle(State->Worker);
    if (State->StopEvent != NULL)
        CloseHandle(State->StopEvent);
    if (State->Recording != NULL)
        RperfRecordingRelease(State->Recording);
    HeapFree(GetProcessHeap(), 0, State);
}

static const RPERF_RECORDER_OPS RperfFakeOps =
{
    RperfFakeStart,
    RperfFakeStop,
    RperfFakeJoin,
    RperfFakeCounters,
    RperfFakeTake,
    RperfFakeDestroy
};

BOOL
RperfFakeCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                const RPERF_RECORDER_OPS **Ops,
                PVOID *Opaque)
{
    RPERF_FAKE_STATE *State;
    State = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*State));
    if (State == NULL)
        return FALSE;
    State->Config = *Configuration;
    State->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (State->StopEvent == NULL)
    {
        HeapFree(GetProcessHeap(), 0, State);
        return FALSE;
    }
    *Ops = &RperfFakeOps;
    *Opaque = State;
    return TRUE;
}
