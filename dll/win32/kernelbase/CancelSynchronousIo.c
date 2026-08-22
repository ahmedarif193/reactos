/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synchronous I/O cancellation wrapper
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winternl.h>

#include "wine/kernelbase.h"

BOOL
WINAPI
CancelSynchronousIo(
    _In_ HANDLE Thread)
{
    IO_STATUS_BLOCK IoStatusBlock;

    return set_ntstatus(NtCancelSynchronousIoFile(Thread, NULL, &IoStatusBlock));
}
