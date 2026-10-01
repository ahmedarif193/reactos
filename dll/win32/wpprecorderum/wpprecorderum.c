/*
 * PROJECT:     LiberNT WPP auto-logger (user mode)
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     No-op WppRecorderUM exports for UMDF drivers built with WPP auto-logging
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

VOID
WINAPI
WppAutoLogStart(
    _In_opt_ PVOID WppControlBlock,
    _In_opt_ PVOID DriverObject,
    _In_opt_ PVOID RegistryPath)
{
    UNREFERENCED_PARAMETER(WppControlBlock);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
}

VOID
WINAPI
WppAutoLogStop(
    _In_opt_ PVOID WppControlBlock)
{
    UNREFERENCED_PARAMETER(WppControlBlock);
}

VOID
__cdecl
WppAutoLogTrace(
    _In_opt_ PVOID AutoLogContext,
    _In_ UCHAR MessageLevel,
    _In_ ULONG MessageFlags,
    _In_opt_ PVOID MessageGuid,
    _In_ USHORT MessageNumber,
    ...)
{
    UNREFERENCED_PARAMETER(AutoLogContext);
    UNREFERENCED_PARAMETER(MessageLevel);
    UNREFERENCED_PARAMETER(MessageFlags);
    UNREFERENCED_PARAMETER(MessageGuid);
    UNREFERENCED_PARAMETER(MessageNumber);
}
