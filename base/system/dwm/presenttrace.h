/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#pragma once
#include <reactos/dwmpresenttracecore.h>
extern DPT_BANK g_DwmPresentTrace;
void DwmPresentTraceSetIcd(PFNWGLCONTROLPRESENTATIONTRACEROS Control);
void DwmPresentTraceFrame(DPT_SCOPE Scope, ULONGLONG DrawStart,
                          ULONGLONG DrawEnd, ULONGLONG PresentEnd);

enum DWM_GPU_WAIT_PHASE
{
    DWM_WAIT_END,
    DWM_WAIT_FLUSH,
    DWM_WAIT_QUERY,
    DWM_WAIT_REMOVED,
    DWM_WAIT_SLEEP,
    DWM_WAIT_TOTAL,
    DWM_WAIT_PHASE_COUNT
};

typedef struct _DWM_GPU_WAIT_TIMING
{
    ULONGLONG Ticks[DWM_WAIT_PHASE_COUNT];
    ULONG Polls, Sleeps;
    ULONGLONG MaxSleep;
} DWM_GPU_WAIT_TIMING;

void DwmPresentTraceGpuWait(DPT_SCOPE Scope, DWM_GPU_WAIT_TIMING *Timing, BOOL Complete);
