/*
 * PROJECT:     ReactOS Win32 Base API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Control Flow Guard dynamic target registration
 */

#include "k32_vista.h"

NTSTATUS NTAPI RtlSetCfgTargetValidity(PVOID Base, SIZE_T Size, ULONG Count, PCFG_CALL_TARGET_INFO Targets);

BOOL
WINAPI
SetProcessValidCallTargets(
    _In_ HANDLE hProcess,
    _In_ PVOID VirtualAddress,
    _In_ SIZE_T RegionSize,
    _In_ ULONG NumberOfOffsets,
    _Inout_updates_(NumberOfOffsets) PCFG_CALL_TARGET_INFO OffsetInformation)
{
    NTSTATUS Status;

    if (hProcess != GetCurrentProcess())
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    Status = RtlSetCfgTargetValidity(VirtualAddress, RegionSize, NumberOfOffsets, OffsetInformation);
    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
