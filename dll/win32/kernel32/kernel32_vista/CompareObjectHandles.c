/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     CompareObjectHandles
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/process.c
 */

#include "k32_vista.h"

BOOL
WINAPI
CompareObjectHandles(
    _In_ HANDLE FirstObjectHandle,
    _In_ HANDLE SecondObjectHandle)
{
    NTSTATUS Status;

    Status = NtCompareObjects(FirstObjectHandle, SecondObjectHandle);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }

    return TRUE;
}
