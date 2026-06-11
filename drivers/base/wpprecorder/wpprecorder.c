/*
 * PROJECT:     ReactOS WPP auto-logger stub
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     No-op WppRecorder exports so Win8+ inbox drivers (usbser.sys
 *              and friends) resolve their WPP tracing imports.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

VOID
NTAPI
WppAutoLogStart(
    _In_opt_ PVOID WppControlBlock,
    _In_opt_ PDRIVER_OBJECT DriverObject,
    _In_opt_ PCUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(WppControlBlock);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
}

VOID
NTAPI
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

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    return STATUS_SUCCESS;
}
