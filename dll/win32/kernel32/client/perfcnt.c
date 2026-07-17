/*
 * COPYRIGHT:            See COPYING in the top level directory
 * PROJECT:              ReactOS Win32 Base API
 * FILE:                 dll/win32/kernel32/client/perfcnt.c
 * PURPOSE:              Performance Counter
 * PROGRAMMER:           Eric Kohl
 */

/* INCLUDES *******************************************************************/

#include <k32.h>

#define NDEBUG
#include <debug.h>

#ifdef _M_ARM64
#define K32_ARM64_QPC_BYPASS_CONFIGURATION 0x0001
#endif

/* FUNCTIONS ******************************************************************/

/*
 * @implemented
 */
BOOL
WINAPI
QueryPerformanceCounter(OUT PLARGE_INTEGER lpPerformanceCount)
{
    LARGE_INTEGER Frequency;
    NTSTATUS Status;

#ifdef _M_ARM64
    if ((*(volatile USHORT *)&SharedUserData->QpcData == K32_ARM64_QPC_BYPASS_CONFIGURATION) &&
        RtlQueryPerformanceCounter(lpPerformanceCount))
    {
        return TRUE;
    }
#endif

    Status = NtQueryPerformanceCounter(lpPerformanceCount, &Frequency);
    if (Frequency.QuadPart == 0) Status = STATUS_NOT_IMPLEMENTED;

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
QueryPerformanceFrequency(OUT PLARGE_INTEGER lpFrequency)
{
    LARGE_INTEGER Count;
    NTSTATUS Status;

#ifdef _M_ARM64
    if ((*(volatile USHORT *)&SharedUserData->QpcData == K32_ARM64_QPC_BYPASS_CONFIGURATION) &&
        RtlQueryPerformanceFrequency(lpFrequency) &&
        (lpFrequency->QuadPart != 0))
    {
        return TRUE;
    }
#endif

    Status = NtQueryPerformanceCounter(&Count, lpFrequency);
    if (lpFrequency->QuadPart == 0) Status = STATUS_NOT_IMPLEMENTED;

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}

/* EOF */
