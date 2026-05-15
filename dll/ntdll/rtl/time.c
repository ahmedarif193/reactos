/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     NTDLL time query wrappers
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

BOOL
WINAPI
RtlQueryPerformanceCounter(PLARGE_INTEGER Counter)
{
    NTSTATUS Status;

    if (!Counter)
        return FALSE;

    Status = NtQueryPerformanceCounter(Counter, NULL);
    return NT_SUCCESS(Status);
}

BOOL
WINAPI
RtlQueryPerformanceFrequency(PLARGE_INTEGER Frequency)
{
    LARGE_INTEGER Counter;
    NTSTATUS Status;

    if (!Frequency)
        return FALSE;

    Status = NtQueryPerformanceCounter(&Counter, Frequency);
    return NT_SUCCESS(Status);
}

VOID
WINAPI
RtlQuerySystemTime(PLARGE_INTEGER SystemTime)
{
    if (SystemTime)
        NtQuerySystemTime(SystemTime);
}

VOID
WINAPI
RtlSystemTimeToTimeFields(const LARGE_INTEGER *SystemTime,
                          PTIME_FIELDS TimeFields)
{
    if (!SystemTime || !TimeFields)
        return;

    RtlTimeToTimeFields((PLARGE_INTEGER)SystemTime, TimeFields);
}
