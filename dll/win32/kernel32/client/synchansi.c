/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/kernel32/client/synchansi.c
 * PURPOSE:         Synchronization functions (ANSI)
 */

/* INCLUDES ******************************************************************/

#include <k32.h>

/* FUNCTIONS ****************************************************************/

/*
 * @implemented
 */
HANDLE
WINAPI
CreateWaitableTimerA(IN LPSECURITY_ATTRIBUTES lpTimerAttributes OPTIONAL,
                     IN BOOL bManualReset,
                     IN LPCSTR lpTimerName OPTIONAL)
{
    ConvertWin32AnsiObjectApiToUnicodeApi(WaitableTimer, lpTimerName, lpTimerAttributes, bManualReset);
}

HANDLE
WINAPI
CreateWaitableTimerExA(IN LPSECURITY_ATTRIBUTES lpTimerAttributes OPTIONAL,
                       IN LPCSTR lpTimerName OPTIONAL,
                       IN DWORD dwFlags,
                       IN DWORD dwDesiredAccess)
{
    NTSTATUS Status;
    PUNICODE_STRING UnicodeCache;
    ANSI_STRING AnsiName;

    if (!lpTimerName)
        return CreateWaitableTimerExW(lpTimerAttributes, NULL, dwFlags, dwDesiredAccess);

    UnicodeCache = &NtCurrentTeb()->StaticUnicodeString;
    RtlInitAnsiString(&AnsiName, lpTimerName);
    Status = RtlAnsiStringToUnicodeString(UnicodeCache, &AnsiName, FALSE);
    if (NT_SUCCESS(Status))
        return CreateWaitableTimerExW(lpTimerAttributes, UnicodeCache->Buffer, dwFlags, dwDesiredAccess);

    if (Status == STATUS_BUFFER_OVERFLOW)
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
    else
        BaseSetLastNTError(Status);

    return NULL;
}

/*
 * @implemented
 */
HANDLE
WINAPI
OpenWaitableTimerA(IN DWORD dwDesiredAccess,
                   IN BOOL bInheritHandle,
                   IN LPCSTR lpTimerName)
{
    ConvertOpenWin32AnsiObjectApiToUnicodeApi(WaitableTimer, dwDesiredAccess, bInheritHandle, lpTimerName);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateSemaphoreA(IN LPSECURITY_ATTRIBUTES lpSemaphoreAttributes  OPTIONAL,
                 IN LONG lInitialCount,
                 IN LONG lMaximumCount,
                 IN LPCSTR lpName  OPTIONAL)
{
    ConvertWin32AnsiObjectApiToUnicodeApi(Semaphore, lpName, lpSemaphoreAttributes, lInitialCount, lMaximumCount);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenSemaphoreA(IN DWORD dwDesiredAccess,
               IN BOOL bInheritHandle,
               IN LPCSTR lpName)
{
    ConvertOpenWin32AnsiObjectApiToUnicodeApi(Semaphore, dwDesiredAccess, bInheritHandle, lpName);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenMutexA(IN DWORD dwDesiredAccess,
           IN BOOL bInheritHandle,
           IN LPCSTR lpName)
{
    ConvertOpenWin32AnsiObjectApiToUnicodeApi(Mutex, dwDesiredAccess, bInheritHandle, lpName);
}

/* EOF */
