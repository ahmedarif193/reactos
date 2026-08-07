/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Process and thread cycle time queries
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/process.c
 */

#include <k32.h>

BOOL
WINAPI
QueryThreadCycleTime(IN HANDLE ThreadHandle,
                     OUT PULONG64 CycleTime)
{
    THREAD_CYCLE_TIME_INFORMATION CycleInfo;
    NTSTATUS Status;

    Status = NtQueryInformationThread(ThreadHandle, ThreadCycleTime, &CycleInfo, sizeof(CycleInfo), NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    *CycleTime = CycleInfo.AccumulatedCycles;
    return TRUE;
}

BOOL
WINAPI
QueryProcessCycleTime(IN HANDLE ProcessHandle,
                      OUT PULONG64 CycleTime)
{
    PROCESS_CYCLE_TIME_INFORMATION CycleInfo;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessCycleTime, &CycleInfo, sizeof(CycleInfo), NULL);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    *CycleTime = CycleInfo.AccumulatedCycles;
    return TRUE;
}
