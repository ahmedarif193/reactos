/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Transactional ownership boundary between workers and the GUI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_controller.h"

static VOID
RperfControllerFreePreparedSession(RPERF_SESSION *Session)
{
    if (Session == NULL)
        return;
    RperfSessionClear(Session);
    HeapFree(GetProcessHeap(), 0, Session);
}

VOID
RperfControllerInitialize(RPERF_SESSION_CONTROLLER *Controller)
{
    ZeroMemory(Controller, sizeof(*Controller));
    InitializeCriticalSection(&Controller->Lock);
}

VOID
RperfControllerCancel(RPERF_SESSION_CONTROLLER *Controller)
{
    RPERF_JOB *Job;
    if (Controller == NULL)
        return;
    EnterCriticalSection(&Controller->Lock);
    Job = Controller->ActiveJob;
    if (Job != NULL)
    {
        Controller->CancelRequested = TRUE;
        RperfJobCancel(Job);
    }
    LeaveCriticalSection(&Controller->Lock);
}

VOID
RperfControllerDestroy(RPERF_SESSION_CONTROLLER *Controller)
{
    if (Controller == NULL)
        return;
    RperfControllerCancel(Controller);
    if (Controller->ActiveJob != NULL)
        RperfJobDestroy(Controller->ActiveJob);
    if (Controller->Analysis != NULL)
        RperfAnalysisRelease(Controller->Analysis);
    if (Controller->Recording != NULL)
        RperfRecordingRelease(Controller->Recording);
    RperfControllerFreePreparedSession(Controller->PreparedSession);
    if (Controller->PreparedTimeline != NULL)
        RperfTimelineViewDestroy(Controller->PreparedTimeline);
    DeleteCriticalSection(&Controller->Lock);
    ZeroMemory(Controller, sizeof(*Controller));
}

ULONGLONG
RperfControllerNextGeneration(RPERF_SESSION_CONTROLLER *Controller)
{
    ULONGLONG Generation;
    EnterCriticalSection(&Controller->Lock);
    Generation = ++Controller->Generation;
    if (Generation == 0)
        Generation = ++Controller->Generation;
    LeaveCriticalSection(&Controller->Lock);
    return Generation;
}

static BOOL
RperfControllerCanStartJob(RPERF_SESSION_CONTROLLER *Controller)
{
    BOOL Available;

    EnterCriticalSection(&Controller->Lock);
    Available = Controller->ActiveJob == NULL;
    LeaveCriticalSection(&Controller->Lock);
    if (!Available)
        SetLastError(ERROR_BUSY);
    return Available;
}

static BOOL
RperfControllerInstallJob(RPERF_SESSION_CONTROLLER *Controller,
                          RPERF_JOB *Job,
                          ULONGLONG Generation)
{
    if (Job == NULL)
        return FALSE;
    EnterCriticalSection(&Controller->Lock);
    if (Controller->ActiveJob != NULL)
    {
        LeaveCriticalSection(&Controller->Lock);
        RperfJobCancel(Job);
        RperfJobDestroy(Job);
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    Controller->ActiveJob = Job;
    Controller->Generation = Generation;
    Controller->CancelRequested = FALSE;
    LeaveCriticalSection(&Controller->Lock);
    return TRUE;
}

BOOL
RperfControllerBeginOpen(RPERF_SESSION_CONTROLLER *Controller,
                         PCWSTR Path,
                         const RPERF_CAPTURE_LIMITS *Limits,
                         RPERF_JOB_PROGRESS Progress,
                         RPERF_JOB_COMPLETE Complete,
                         PVOID Context,
                         ULONGLONG *Generation)
{
    ULONGLONG Value;
    RPERF_JOB *Job;

    if (!RperfControllerCanStartJob(Controller))
        return FALSE;
    Value = RperfControllerNextGeneration(Controller);
    Job = RperfJobStartOpen(Value, Path, Limits,
                            Progress, Complete, Context);
    if (!RperfControllerInstallJob(Controller, Job, Value))
        return FALSE;
    if (Generation != NULL)
        *Generation = Value;
    return TRUE;
}

BOOL
RperfControllerBeginSymbolize(RPERF_SESSION_CONTROLLER *Controller,
                              RPERF_SYMBOL_PROVIDER *Provider,
                              RPERF_JOB_PROGRESS Progress,
                              RPERF_JOB_COMPLETE Complete,
                              PVOID Context,
                              ULONGLONG *Generation)
{
    ULONGLONG Value;
    RPERF_RECORDING *Recording;
    RPERF_JOB *Job;

    if (!RperfControllerCanStartJob(Controller))
        return FALSE;
    Recording = RperfControllerAcquireRecording(Controller);
    if (Recording == NULL)
        return FALSE;
    Value = RperfControllerNextGeneration(Controller);
    Job = RperfJobStartSymbolize(Value, Recording, Provider,
                                 Progress, Complete, Context);
    RperfRecordingRelease(Recording);
    if (!RperfControllerInstallJob(Controller, Job, Value))
        return FALSE;
    if (Generation != NULL)
        *Generation = Value;
    return TRUE;
}

BOOL
RperfControllerBeginFilter(RPERF_SESSION_CONTROLLER *Controller,
                           const RPERF_FILTER *Filter,
                           RPERF_JOB_PROGRESS Progress,
                           RPERF_JOB_COMPLETE Complete,
                           PVOID Context,
                           ULONGLONG *Generation)
{
    ULONGLONG Value;
    RPERF_RECORDING *Recording;
    RPERF_JOB *Job;

    if (!RperfControllerCanStartJob(Controller))
        return FALSE;
    Recording = RperfControllerAcquireRecording(Controller);
    if (Recording == NULL)
        return FALSE;
    Value = RperfControllerNextGeneration(Controller);
    Job = RperfJobStartAnalysis(Value, Recording, Filter,
                                Progress, Complete, Context);
    RperfRecordingRelease(Recording);
    if (!RperfControllerInstallJob(Controller, Job, Value))
        return FALSE;
    if (Generation != NULL)
        *Generation = Value;
    return TRUE;
}

BOOL
RperfControllerBeginPrepareLegacy(RPERF_SESSION_CONTROLLER *Controller,
                                  PCWSTR SourcePath,
                                  SIZE_T TimelineBucketCount,
                                  RPERF_JOB_PROGRESS Progress,
                                  RPERF_JOB_COMPLETE Complete,
                                  PVOID Context,
                                  ULONGLONG *Generation)
{
    ULONGLONG Value;
    RPERF_RECORDING *Recording;
    RPERF_JOB *Job;

    if (!RperfControllerCanStartJob(Controller))
        return FALSE;
    Recording = RperfControllerAcquireRecording(Controller);
    if (Recording == NULL)
        return FALSE;
    Value = RperfControllerNextGeneration(Controller);
    Job = RperfJobStartPrepareLegacy(Value,
                                     Recording,
                                     SourcePath,
                                     TimelineBucketCount,
                                     Progress,
                                     Complete,
                                     Context);
    RperfRecordingRelease(Recording);
    if (!RperfControllerInstallJob(Controller, Job, Value))
        return FALSE;
    if (Generation != NULL)
        *Generation = Value;
    return TRUE;
}

BOOL
RperfControllerCommitCompleted(RPERF_SESSION_CONTROLLER *Controller,
                               ULONGLONG Generation)
{
    RPERF_JOB *Job;
    RPERF_RECORDING *Recording;
    RPERF_ANALYSIS *Analysis;
    RPERF_SESSION *PreparedSession;
    RPERF_TIMELINE_VIEW *PreparedTimeline;
    DWORD JobStatus;

    EnterCriticalSection(&Controller->Lock);
    if (Generation != Controller->Generation || Controller->ActiveJob == NULL)
    {
        LeaveCriticalSection(&Controller->Lock);
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    Job = Controller->ActiveJob;
    LeaveCriticalSection(&Controller->Lock);
    if (!RperfJobJoin(Job, INFINITE))
        return FALSE;
    JobStatus = RperfJobGetStatus(Job);
    Recording = JobStatus == ERROR_SUCCESS ?
                RperfJobTakeRecording(Job) : NULL;
    Analysis = JobStatus == ERROR_SUCCESS ?
               RperfJobTakeAnalysis(Job) : NULL;
    PreparedSession = JobStatus == ERROR_SUCCESS ?
                      RperfJobTakeLegacySession(Job) : NULL;
    PreparedTimeline = JobStatus == ERROR_SUCCESS ?
                       RperfJobTakeTimelineView(Job) : NULL;
    EnterCriticalSection(&Controller->Lock);
    if (Generation != Controller->Generation ||
        Job != Controller->ActiveJob)
    {
        LeaveCriticalSection(&Controller->Lock);
        if (Recording != NULL) RperfRecordingRelease(Recording);
        if (Analysis != NULL) RperfAnalysisRelease(Analysis);
        RperfControllerFreePreparedSession(PreparedSession);
        RperfTimelineViewDestroy(PreparedTimeline);
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    if (Controller->CancelRequested)
    {
        if (Recording != NULL)
        {
            RperfRecordingRelease(Recording);
            Recording = NULL;
        }
        if (Analysis != NULL)
        {
            RperfAnalysisRelease(Analysis);
            Analysis = NULL;
        }
        RperfControllerFreePreparedSession(PreparedSession);
        PreparedSession = NULL;
        RperfTimelineViewDestroy(PreparedTimeline);
        PreparedTimeline = NULL;
        JobStatus = ERROR_CANCELLED;
    }
    Controller->ActiveJob = NULL;
    Controller->CancelRequested = FALSE;
    if (Recording != NULL)
    {
        if (Controller->Recording != NULL)
            RperfRecordingRelease(Controller->Recording);
        Controller->Recording = Recording;
        if (Controller->Analysis != NULL)
        {
            RperfAnalysisRelease(Controller->Analysis);
            Controller->Analysis = NULL;
        }
        RperfControllerFreePreparedSession(Controller->PreparedSession);
        Controller->PreparedSession = NULL;
        RperfTimelineViewDestroy(Controller->PreparedTimeline);
        Controller->PreparedTimeline = NULL;
    }
    if (Analysis != NULL)
    {
        if (Controller->Analysis != NULL)
            RperfAnalysisRelease(Controller->Analysis);
        Controller->Analysis = Analysis;
    }
    if (PreparedSession != NULL)
    {
        RperfControllerFreePreparedSession(Controller->PreparedSession);
        Controller->PreparedSession = PreparedSession;
    }
    if (PreparedTimeline != NULL)
    {
        RperfTimelineViewDestroy(Controller->PreparedTimeline);
        Controller->PreparedTimeline = PreparedTimeline;
    }
    LeaveCriticalSection(&Controller->Lock);
    RperfJobDestroy(Job);
    if (JobStatus != ERROR_SUCCESS)
    {
        SetLastError(JobStatus);
        return FALSE;
    }
    return TRUE;
}

RPERF_RECORDING *
RperfControllerAcquireRecording(RPERF_SESSION_CONTROLLER *Controller)
{
    RPERF_RECORDING *Recording;
    EnterCriticalSection(&Controller->Lock);
    Recording = Controller->Recording;
    if (Recording != NULL)
        RperfRecordingAddRef(Recording);
    LeaveCriticalSection(&Controller->Lock);
    return Recording;
}

RPERF_ANALYSIS *
RperfControllerAcquireAnalysis(RPERF_SESSION_CONTROLLER *Controller)
{
    RPERF_ANALYSIS *Analysis;
    EnterCriticalSection(&Controller->Lock);
    Analysis = Controller->Analysis;
    if (Analysis != NULL)
        RperfAnalysisAddRef(Analysis);
    LeaveCriticalSection(&Controller->Lock);
    return Analysis;
}

RPERF_SESSION *
RperfControllerTakePreparedSession(RPERF_SESSION_CONTROLLER *Controller)
{
    RPERF_SESSION *Session;

    EnterCriticalSection(&Controller->Lock);
    Session = Controller->PreparedSession;
    Controller->PreparedSession = NULL;
    LeaveCriticalSection(&Controller->Lock);
    return Session;
}

RPERF_TIMELINE_VIEW *
RperfControllerTakePreparedTimeline(RPERF_SESSION_CONTROLLER *Controller)
{
    RPERF_TIMELINE_VIEW *Timeline;

    EnterCriticalSection(&Controller->Lock);
    Timeline = Controller->PreparedTimeline;
    Controller->PreparedTimeline = NULL;
    LeaveCriticalSection(&Controller->Lock);
    return Timeline;
}
