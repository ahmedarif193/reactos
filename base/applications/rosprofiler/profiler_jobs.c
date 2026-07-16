/* Cancellable background parse, symbolization, and analysis jobs. */

#include "profiler_jobs.h"

struct _RPERF_JOB
{
    ULONGLONG Generation;
    RPERF_JOB_KIND Kind;
    HANDLE Thread;
    HANDLE CancelEvent;
    volatile LONG Joined;
    DWORD Status;
    PWSTR Path;
    RPERF_CAPTURE_LIMITS Limits;
    RPERF_FILTER Filter;
    RPERF_RECORDING *Input;
    RPERF_RECORDING *RecordingResult;
    RPERF_ANALYSIS *AnalysisResult;
    RPERF_SESSION *LegacyResult;
    RPERF_SYMBOL_PROVIDER *Provider;
    RPERF_JOB_PROGRESS Progress;
    RPERF_JOB_COMPLETE Complete;
    PVOID Context;
};

static VOID CALLBACK
RperfJobCodecProgress(PVOID Opaque,
                      ULONGLONG Completed,
                      ULONGLONG Total)
{
    RPERF_JOB *Job = Opaque;
    if (Job->Progress != NULL)
        Job->Progress(Job->Context, Job->Generation, Job->Kind,
                      Completed, Total);
}

static VOID CALLBACK
RperfJobSymbolProgress(PVOID Opaque,
                       SIZE_T Completed,
                       SIZE_T Total)
{
    RperfJobCodecProgress(Opaque, Completed, Total);
}

static VOID CALLBACK
RperfJobSessionProgress(PVOID Opaque,
                        ULONGLONG Completed,
                        ULONGLONG Total)
{
    RperfJobCodecProgress(Opaque, Completed, Total);
}

static DWORD WINAPI
RperfJobWorker(PVOID Opaque)
{
    RPERF_JOB *Job = Opaque;
    BOOL Result = FALSE;

    if (Job->Kind == RperfJobOpen)
    {
        Result = RperfCodecLoad(Job->Path,
                                RperfCodecAuto,
                                &Job->Limits,
                                Job->CancelEvent,
                                RperfJobCodecProgress,
                                Job,
                                &Job->RecordingResult,
                                NULL);
    }
    else if (Job->Kind == RperfJobSymbolize)
    {
        Result = RperfSymbolizeRecording(Job->Input,
                                         Job->Provider,
                                         Job->CancelEvent,
                                         RperfJobSymbolProgress,
                                         Job,
                                         &Job->RecordingResult);
    }
    else if (Job->Kind == RperfJobAnalyze)
    {
        if (Job->Progress != NULL)
            Job->Progress(Job->Context, Job->Generation, Job->Kind,
                          0, Job->Input->RecordCount);
        Job->AnalysisResult = RperfAnalysisBuild(Job->Input,
                                                 &Job->Filter,
                                                 Job->CancelEvent);
        Result = Job->AnalysisResult != NULL;
        if (Result && Job->Progress != NULL)
            Job->Progress(Job->Context, Job->Generation, Job->Kind,
                          Job->Input->RecordCount,
                          Job->Input->RecordCount);
    }
    else if (Job->Kind == RperfJobPrepareLegacy)
    {
        Job->LegacyResult = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      sizeof(*Job->LegacyResult));
        if (Job->LegacyResult == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        }
        else
        {
            RperfSessionInitialize(Job->LegacyResult);
            Result = RperfLegacySessionFromRecordingEx(
                         Job->Input,
                         Job->Path,
                         Job->CancelEvent,
                         RperfJobSessionProgress,
                         Job,
                         Job->LegacyResult);
            if (!Result)
            {
                RperfSessionClear(Job->LegacyResult);
                HeapFree(GetProcessHeap(), 0, Job->LegacyResult);
                Job->LegacyResult = NULL;
            }
        }
    }
    Job->Status = Result ? ERROR_SUCCESS : GetLastError();
    if (!Result && Job->Status == ERROR_SUCCESS)
        Job->Status = ERROR_GEN_FAILURE;
    if (Job->Complete != NULL)
        Job->Complete(Job->Context, Job->Generation,
                      Job->Kind, Job->Status);
    return Job->Status;
}

static RPERF_JOB *
RperfJobAllocate(ULONGLONG Generation,
                 RPERF_JOB_KIND Kind,
                 RPERF_JOB_PROGRESS Progress,
                 RPERF_JOB_COMPLETE Complete,
                 PVOID Context)
{
    RPERF_JOB *Job = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                               sizeof(*Job));
    if (Job == NULL)
        return NULL;
    Job->Generation = Generation;
    Job->Kind = Kind;
    Job->Progress = Progress;
    Job->Complete = Complete;
    Job->Context = Context;
    Job->CancelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Job->CancelEvent == NULL)
    {
        HeapFree(GetProcessHeap(), 0, Job);
        return NULL;
    }
    return Job;
}

static BOOL
RperfJobLaunch(RPERF_JOB *Job)
{
    Job->Thread = CreateThread(NULL, 0, RperfJobWorker, Job, 0, NULL);
    return Job->Thread != NULL;
}

RPERF_JOB *
RperfJobStartOpen(ULONGLONG Generation,
                  PCWSTR Path,
                  const RPERF_CAPTURE_LIMITS *Limits,
                  RPERF_JOB_PROGRESS Progress,
                  RPERF_JOB_COMPLETE Complete,
                  PVOID Context)
{
    RPERF_JOB *Job;
    SIZE_T Bytes;

    if (Path == NULL)
        return NULL;
    Job = RperfJobAllocate(Generation, RperfJobOpen,
                           Progress, Complete, Context);
    if (Job == NULL)
        return NULL;
    Bytes = (wcslen(Path) + 1) * sizeof(WCHAR);
    Job->Path = HeapAlloc(GetProcessHeap(), 0, Bytes);
    if (Job->Path == NULL)
        goto Failure;
    CopyMemory(Job->Path, Path, Bytes);
    if (Limits != NULL)
        Job->Limits = *Limits;
    else
        RperfDefaultCaptureLimits(&Job->Limits);
    if (!RperfJobLaunch(Job))
        goto Failure;
    return Job;
Failure:
    RperfJobDestroy(Job);
    return NULL;
}

RPERF_JOB *
RperfJobStartSymbolize(ULONGLONG Generation,
                       RPERF_RECORDING *Recording,
                       RPERF_SYMBOL_PROVIDER *Provider,
                       RPERF_JOB_PROGRESS Progress,
                       RPERF_JOB_COMPLETE Complete,
                       PVOID Context)
{
    RPERF_JOB *Job;
    if (Recording == NULL || Provider == NULL)
        return NULL;
    Job = RperfJobAllocate(Generation, RperfJobSymbolize,
                           Progress, Complete, Context);
    if (Job == NULL)
        return NULL;
    Job->Input = Recording;
    RperfRecordingAddRef(Recording);
    Job->Provider = Provider;
    if (!RperfJobLaunch(Job))
    {
        RperfJobDestroy(Job);
        return NULL;
    }
    return Job;
}

RPERF_JOB *
RperfJobStartAnalysis(ULONGLONG Generation,
                      RPERF_RECORDING *Recording,
                      const RPERF_FILTER *Filter,
                      RPERF_JOB_PROGRESS Progress,
                      RPERF_JOB_COMPLETE Complete,
                      PVOID Context)
{
    RPERF_JOB *Job;
    if (Recording == NULL)
        return NULL;
    Job = RperfJobAllocate(Generation, RperfJobAnalyze,
                           Progress, Complete, Context);
    if (Job == NULL)
        return NULL;
    Job->Input = Recording;
    RperfRecordingAddRef(Recording);
    if (Filter != NULL)
        Job->Filter = *Filter;
    else
        RperfInitializeFilter(&Job->Filter);
    if (!RperfJobLaunch(Job))
    {
        RperfJobDestroy(Job);
        return NULL;
    }
    return Job;
}

RPERF_JOB *
RperfJobStartPrepareLegacy(ULONGLONG Generation,
                           RPERF_RECORDING *Recording,
                           PCWSTR SourcePath,
                           RPERF_JOB_PROGRESS Progress,
                           RPERF_JOB_COMPLETE Complete,
                           PVOID Context)
{
    RPERF_JOB *Job;
    SIZE_T Bytes;

    if (Recording == NULL)
        return NULL;
    Job = RperfJobAllocate(Generation, RperfJobPrepareLegacy,
                           Progress, Complete, Context);
    if (Job == NULL)
        return NULL;
    Job->Input = Recording;
    RperfRecordingAddRef(Recording);
    if (SourcePath != NULL)
    {
        Bytes = (wcslen(SourcePath) + 1) * sizeof(WCHAR);
        Job->Path = HeapAlloc(GetProcessHeap(), 0, Bytes);
        if (Job->Path == NULL)
            goto Failure;
        CopyMemory(Job->Path, SourcePath, Bytes);
    }
    if (!RperfJobLaunch(Job))
        goto Failure;
    return Job;

Failure:
    RperfJobDestroy(Job);
    return NULL;
}

BOOL
RperfJobCancel(RPERF_JOB *Job)
{
    return Job != NULL && SetEvent(Job->CancelEvent);
}

BOOL
RperfJobJoin(RPERF_JOB *Job,
             DWORD Timeout)
{
    DWORD Wait;
    if (Job == NULL || Job->Thread == NULL)
        return FALSE;
    if (InterlockedCompareExchange(&Job->Joined, 0, 0) != 0)
        return TRUE;
    Wait = WaitForSingleObject(Job->Thread, Timeout);
    if (Wait == WAIT_TIMEOUT)
    {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    if (Wait != WAIT_OBJECT_0)
        return FALSE;
    InterlockedExchange(&Job->Joined, 1);
    return TRUE;
}

DWORD
RperfJobGetStatus(const RPERF_JOB *Job)
{
    if (Job == NULL ||
        InterlockedCompareExchange((volatile LONG *)&Job->Joined, 0, 0) == 0)
    {
        return ERROR_IO_PENDING;
    }
    return Job->Status;
}

RPERF_RECORDING *
RperfJobTakeRecording(RPERF_JOB *Job)
{
    RPERF_RECORDING *Result;
    if (Job == NULL || InterlockedCompareExchange(&Job->Joined, 0, 0) == 0)
        return NULL;
    Result = Job->RecordingResult;
    Job->RecordingResult = NULL;
    return Result;
}

RPERF_ANALYSIS *
RperfJobTakeAnalysis(RPERF_JOB *Job)
{
    RPERF_ANALYSIS *Result;
    if (Job == NULL || InterlockedCompareExchange(&Job->Joined, 0, 0) == 0)
        return NULL;
    Result = Job->AnalysisResult;
    Job->AnalysisResult = NULL;
    return Result;
}

RPERF_SESSION *
RperfJobTakeLegacySession(RPERF_JOB *Job)
{
    RPERF_SESSION *Result;
    if (Job == NULL || InterlockedCompareExchange(&Job->Joined, 0, 0) == 0)
        return NULL;
    Result = Job->LegacyResult;
    Job->LegacyResult = NULL;
    return Result;
}

VOID
RperfJobDestroy(RPERF_JOB *Job)
{
    if (Job == NULL)
        return;
    if (Job->Thread != NULL &&
        InterlockedCompareExchange(&Job->Joined, 0, 0) == 0)
    {
        RperfJobCancel(Job);
        WaitForSingleObject(Job->Thread, INFINITE);
    }
    if (Job->Thread != NULL)
        CloseHandle(Job->Thread);
    if (Job->CancelEvent != NULL)
        CloseHandle(Job->CancelEvent);
    if (Job->Path != NULL)
        HeapFree(GetProcessHeap(), 0, Job->Path);
    if (Job->Input != NULL)
        RperfRecordingRelease(Job->Input);
    if (Job->RecordingResult != NULL)
        RperfRecordingRelease(Job->RecordingResult);
    if (Job->AnalysisResult != NULL)
        RperfAnalysisRelease(Job->AnalysisResult);
    if (Job->LegacyResult != NULL)
    {
        RperfSessionClear(Job->LegacyResult);
        HeapFree(GetProcessHeap(), 0, Job->LegacyResult);
    }
    HeapFree(GetProcessHeap(), 0, Job);
}
