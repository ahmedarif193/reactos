/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Transactional ownership boundary between workers and the GUI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_jobs.h"

typedef struct _RPERF_SESSION_CONTROLLER
{
    CRITICAL_SECTION Lock;
    ULONGLONG Generation;
    RPERF_JOB *ActiveJob;
    BOOL CancelRequested;
    RPERF_RECORDING *Recording;
    RPERF_ANALYSIS *Analysis;
    RPERF_SESSION *PreparedSession;
} RPERF_SESSION_CONTROLLER;

VOID RperfControllerInitialize(RPERF_SESSION_CONTROLLER *Controller);
VOID RperfControllerDestroy(RPERF_SESSION_CONTROLLER *Controller);
ULONGLONG RperfControllerNextGeneration(RPERF_SESSION_CONTROLLER *Controller);
BOOL RperfControllerBeginOpen(RPERF_SESSION_CONTROLLER *Controller,
                              PCWSTR Path,
                              const RPERF_CAPTURE_LIMITS *Limits,
                              RPERF_JOB_PROGRESS Progress,
                              RPERF_JOB_COMPLETE Complete,
                              PVOID Context,
                              ULONGLONG *Generation);
BOOL RperfControllerBeginSymbolize(RPERF_SESSION_CONTROLLER *Controller,
                                   RPERF_SYMBOL_PROVIDER *Provider,
                                   RPERF_JOB_PROGRESS Progress,
                                   RPERF_JOB_COMPLETE Complete,
                                   PVOID Context,
                                   ULONGLONG *Generation);
BOOL RperfControllerBeginFilter(RPERF_SESSION_CONTROLLER *Controller,
                                const RPERF_FILTER *Filter,
                                RPERF_JOB_PROGRESS Progress,
                                RPERF_JOB_COMPLETE Complete,
                                PVOID Context,
                                ULONGLONG *Generation);
BOOL RperfControllerBeginPrepareLegacy(RPERF_SESSION_CONTROLLER *Controller,
                                       PCWSTR SourcePath,
                                       RPERF_JOB_PROGRESS Progress,
                                       RPERF_JOB_COMPLETE Complete,
                                       PVOID Context,
                                       ULONGLONG *Generation);
BOOL RperfControllerCommitCompleted(RPERF_SESSION_CONTROLLER *Controller,
                                    ULONGLONG Generation);
VOID RperfControllerCancel(RPERF_SESSION_CONTROLLER *Controller);
RPERF_RECORDING *
RperfControllerAcquireRecording(RPERF_SESSION_CONTROLLER *Controller);
RPERF_ANALYSIS *
RperfControllerAcquireAnalysis(RPERF_SESSION_CONTROLLER *Controller);
RPERF_SESSION *
RperfControllerTakePreparedSession(RPERF_SESSION_CONTROLLER *Controller);
