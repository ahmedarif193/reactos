/*
 * PROJECT:     ReactOS KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode CPU-set information query
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>

BOOL
WINAPI
GetSystemCpuSetInformation(
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnedLength) PSYSTEM_CPU_SET_INFORMATION Information,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnedLength,
    _In_opt_ HANDLE Process,
    _Reserved_ ULONG Flags)
{
    NTSTATUS Status;

    *ReturnedLength = 0;
    if (Flags)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Status = NtQuerySystemInformationEx(SystemCpuSetInformation, &Process, sizeof(Process), Information, BufferLength, ReturnedLength);
    if (!NT_SUCCESS(Status))
    {
        RtlSetLastWin32Error(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}
