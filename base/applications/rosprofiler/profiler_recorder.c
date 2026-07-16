/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Pluggable recorder coordinator
 */

#include "profiler_recorder_internal.h"

VOID
RperfInitializeCaptureConfiguration(RPERF_CAPTURE_CONFIGURATION *Config)
{
    if (Config == NULL)
        return;
    ZeroMemory(Config, sizeof(*Config));
    Config->Backend = RperfBackendIntrusive;
    Config->Scope = RperfScopeProcess;
    Config->IntervalUs = 10000;
    Config->IncludeUser = TRUE;
    RperfDefaultCaptureLimits(&Config->Limits);
    Config->Limits.MaxFrames = 64;
}

BOOL
RperfRecorderQueryCapabilities(RPERF_BACKEND_KIND Backend,
                               RPERF_RECORDER_CAPABILITIES *Capabilities)
{
    if (Capabilities == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(Capabilities, sizeof(*Capabilities));
    switch (Backend)
    {
        case RperfBackendIntrusive:
            return RperfIntrusiveQueryCapabilities(Capabilities);
        case RperfBackendFake:
            return RperfFakeQueryCapabilities(Capabilities);
        case RperfBackendKernel:
            return RperfKernelQueryCapabilities(Capabilities);
        case RperfBackendEtw:
            return RperfEtwQueryCapabilities(Capabilities);
        default:
            Capabilities->Status = ERROR_NOT_SUPPORTED;
            lstrcpyW(Capabilities->Description, L"Unknown recorder backend.");
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
    }
}

RPERF_BACKEND_KIND
RperfRecorderPreferredBackend(VOID)
{
    RPERF_RECORDER_CAPABILITIES Capabilities;
    if (RperfRecorderQueryCapabilities(RperfBackendKernel, &Capabilities) &&
        Capabilities.Available)
        return RperfBackendKernel;
    if (RperfRecorderQueryCapabilities(RperfBackendEtw, &Capabilities) &&
        Capabilities.Available)
        return RperfBackendEtw;
    return RperfBackendIntrusive;
}

BOOL
RperfRecorderValidateConfiguration(
    const RPERF_CAPTURE_CONFIGURATION *Configuration,
    RPERF_RECORDER_CAPABILITIES *Capabilities)
{
    RPERF_RECORDER_CAPABILITIES Local;
    RPERF_RECORDER_CAPABILITIES *Result = Capabilities != NULL ?
                                          Capabilities : &Local;

    if (Configuration == NULL ||
        Configuration->Limits.MaxFrames == 0 ||
        Configuration->Limits.MaxFrames > RPERF_MODEL_MAX_FRAMES ||
        Configuration->Limits.MaxRecords == 0 ||
        Configuration->Limits.MaxSamples == 0 ||
        (!Configuration->IncludeUser && !Configuration->IncludeKernel) ||
        Configuration->DurationMs > Configuration->Limits.MaxDurationMs ||
        Configuration->IntervalUs == 0 ||
        Configuration->Scope < RperfScopeProcess ||
        Configuration->Scope > RperfScopeSystem)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!RperfRecorderQueryCapabilities(Configuration->Backend, Result))
        return FALSE;
    if (!Result->Available)
    {
        SetLastError(Result->Status != ERROR_SUCCESS ?
                     Result->Status : ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (Configuration->IntervalUs < Result->MinimumIntervalUs ||
        Result->MaximumStackDepth == 0 ||
        (Configuration->EventId != 0 &&
         (Result->Features & RPERF_CAP_PMU) == 0) ||
        (Configuration->IncludeKernel &&
         (Result->Features & RPERF_CAP_KERNEL_STACKS) == 0) ||
        (Configuration->Scope == RperfScopeProcessTree &&
         (Result->Features & RPERF_CAP_PROCESS_TREE) == 0) ||
        (Configuration->Scope == RperfScopeSystem &&
         (Result->Features & RPERF_CAP_SYSTEM_WIDE) == 0) ||
        (Configuration->Scope == RperfScopeSelectedThreads &&
         (Result->Features & RPERF_CAP_THREAD_SCOPE) == 0) ||
        (Configuration->FollowChildren &&
         (Result->Features & RPERF_CAP_PROCESS_TREE) == 0))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if ((Configuration->Scope == RperfScopeProcess ||
         Configuration->Scope == RperfScopeProcessTree) &&
        Configuration->ProcessId == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (Configuration->Scope == RperfScopeSelectedThreads &&
        (Configuration->ThreadIds == NULL || Configuration->ThreadCount == 0))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL
RperfRecorderCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                    RPERF_RECORDER **Recorder)
{
    RPERF_RECORDER *Result;
    const RPERF_RECORDER_OPS *Ops = NULL;
    PVOID State = NULL;
    BOOL Created = FALSE;

    if (Recorder == NULL ||
        !RperfRecorderValidateConfiguration(Configuration, NULL))
        return FALSE;
    *Recorder = NULL;
    switch (Configuration->Backend)
    {
        case RperfBackendIntrusive:
            Created = RperfIntrusiveCreate(Configuration, &Ops, &State);
            break;
        case RperfBackendFake:
            Created = RperfFakeCreate(Configuration, &Ops, &State);
            break;
        case RperfBackendKernel:
            Created = RperfKernelCreate(Configuration, &Ops, &State);
            break;
        case RperfBackendEtw:
            Created = RperfEtwCreate(Configuration, &Ops, &State);
            break;
        default:
            SetLastError(ERROR_NOT_SUPPORTED);
            break;
    }
    if (!Created)
        return FALSE;
    Result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Result));
    if (Result == NULL)
    {
        Ops->Destroy(State);
        return FALSE;
    }
    Result->Ops = Ops;
    Result->BackendState = State;
    Result->State = RperfRecorderCreated;
    *Recorder = Result;
    return TRUE;
}

BOOL
RperfRecorderStart(RPERF_RECORDER *Recorder)
{
    if (Recorder == NULL ||
        InterlockedCompareExchange(&Recorder->State,
                                   RperfRecorderRunning,
                                   RperfRecorderCreated) !=
        RperfRecorderCreated)
    {
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    if (!Recorder->Ops->Start(Recorder->BackendState))
    {
        InterlockedExchange(&Recorder->State, RperfRecorderFailed);
        return FALSE;
    }
    return TRUE;
}

BOOL
RperfRecorderRequestStop(RPERF_RECORDER *Recorder)
{
    LONG State;
    if (Recorder == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    State = InterlockedCompareExchange(&Recorder->State,
                                       RperfRecorderStopping,
                                       RperfRecorderRunning);
    if (State == RperfRecorderStopped || State == RperfRecorderStopping)
        return TRUE;
    if (State != RperfRecorderRunning)
    {
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    if (!Recorder->Ops->RequestStop(Recorder->BackendState))
    {
        InterlockedExchange(&Recorder->State, RperfRecorderFailed);
        return FALSE;
    }
    return TRUE;
}

BOOL
RperfRecorderJoin(RPERF_RECORDER *Recorder,
                  DWORD TimeoutMilliseconds)
{
    LONG State;
    if (Recorder == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    State = InterlockedCompareExchange(&Recorder->State, 0, 0);
    if (State == RperfRecorderStopped)
        return TRUE;
    if (State != RperfRecorderRunning && State != RperfRecorderStopping)
    {
        SetLastError(ERROR_INVALID_STATE);
        return FALSE;
    }
    if (!Recorder->Ops->Join(Recorder->BackendState, TimeoutMilliseconds))
        return FALSE;
    InterlockedExchange(&Recorder->State, RperfRecorderStopped);
    return TRUE;
}

RPERF_RECORDER_STATE
RperfRecorderGetState(const RPERF_RECORDER *Recorder)
{
    if (Recorder == NULL)
        return RperfRecorderFailed;
    return (RPERF_RECORDER_STATE)
        InterlockedCompareExchange((volatile LONG *)&Recorder->State, 0, 0);
}

BOOL
RperfRecorderGetCounters(const RPERF_RECORDER *Recorder,
                         RPERF_CAPTURE_COUNTERS *Counters)
{
    if (Recorder == NULL || Counters == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return Recorder->Ops->GetCounters(Recorder->BackendState, Counters);
}

RPERF_RECORDING *
RperfRecorderTakeRecording(RPERF_RECORDER *Recorder)
{
    if (Recorder == NULL || RperfRecorderGetState(Recorder) != RperfRecorderStopped)
    {
        SetLastError(ERROR_INVALID_STATE);
        return NULL;
    }
    return Recorder->Ops->TakeRecording(Recorder->BackendState);
}

VOID
RperfRecorderDestroy(RPERF_RECORDER *Recorder)
{
    if (Recorder == NULL)
        return;
    if (RperfRecorderGetState(Recorder) == RperfRecorderRunning)
        RperfRecorderRequestStop(Recorder);
    if (RperfRecorderGetState(Recorder) == RperfRecorderStopping)
        RperfRecorderJoin(Recorder, INFINITE);
    Recorder->Ops->Destroy(Recorder->BackendState);
    HeapFree(GetProcessHeap(), 0, Recorder);
}
