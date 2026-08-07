/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     GetOverlappedResultEx
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/file.c
 */

#include "k32_vista.h"

BOOL
WINAPI
GetOverlappedResultEx(
    _In_ HANDLE FileHandle,
    _In_ LPOVERLAPPED Overlapped,
    _Out_ LPDWORD BytesTransferred,
    _In_ DWORD Milliseconds,
    _In_ BOOL Alertable)
{
    NTSTATUS Status;
    DWORD WaitStatus;

    Status = (NTSTATUS)Overlapped->Internal;
    MemoryBarrier();
    if (Status == STATUS_PENDING)
    {
        if (Milliseconds == 0)
        {
            SetLastError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }

        WaitStatus = WaitForSingleObjectEx(Overlapped->hEvent ? Overlapped->hEvent : FileHandle, Milliseconds, Alertable);
        if (WaitStatus == WAIT_FAILED)
            return FALSE;
        if (WaitStatus != WAIT_OBJECT_0)
        {
            SetLastError(WaitStatus);
            return FALSE;
        }

        Status = (NTSTATUS)Overlapped->Internal;
        if (Status == STATUS_PENDING)
            Status = STATUS_SUCCESS;
    }

    *BytesTransferred = (DWORD)Overlapped->InternalHigh;
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}
