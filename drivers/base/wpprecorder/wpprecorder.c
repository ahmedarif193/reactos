/*
 * PROJECT:     LiberNT WPP auto-logger stub
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     No-op WppRecorder exports so Win8+ inbox drivers (usbser.sys
 *              and friends) resolve their WPP tracing imports.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define WPP_RECORDER_LOG_TAG 'LppW'

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

VOID
NTAPI
imp_WppRecorderReplay(
    _In_opt_ PVOID WppControlBlock,
    _In_ TRACEHANDLE WppTraceHandle,
    _In_ ULONG EnableFlags,
    _In_ UCHAR EnableLevel)
{
    UNREFERENCED_PARAMETER(WppControlBlock);
    UNREFERENCED_PARAMETER(WppTraceHandle);
    UNREFERENCED_PARAMETER(EnableFlags);
    UNREFERENCED_PARAMETER(EnableLevel);
}

VOID
NTAPI
imp_WppRecorderConfigure(
    _In_ PVOID WppCb,
    _In_ PVOID ConfigureParams)
{
    UNREFERENCED_PARAMETER(WppCb);
    UNREFERENCED_PARAMETER(ConfigureParams);
}

NTSTATUS
NTAPI
imp_WppRecorderLogCreate(
    _In_ PVOID WppCb,
    _In_ PVOID CreateParams,
    _Out_ PVOID *RecorderLog)
{
    PULONG Log;

    UNREFERENCED_PARAMETER(WppCb);

    if (RecorderLog == NULL)
        return STATUS_INVALID_PARAMETER;
    *RecorderLog = NULL;
    if (CreateParams == NULL)
        return STATUS_INVALID_PARAMETER;

    Log = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Log), WPP_RECORDER_LOG_TAG);
    if (Log == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Log = WPP_RECORDER_LOG_TAG;
    *RecorderLog = Log;
    return STATUS_SUCCESS;
}

VOID
NTAPI
imp_WppRecorderLogDelete(
    _In_ PVOID WppCb,
    _In_ PVOID RecorderLog)
{
    PULONG Log = RecorderLog;

    UNREFERENCED_PARAMETER(WppCb);

    if (Log == NULL || *Log != WPP_RECORDER_LOG_TAG)
        return;

    *Log = 0;
    ExFreePoolWithTag(Log, WPP_RECORDER_LOG_TAG);
}

PVOID
NTAPI
imp_WppRecorderLogGetDefault(
    _In_ PVOID WppCb)
{
    UNREFERENCED_PARAMETER(WppCb);
    return NULL;
}

BOOLEAN
NTAPI
imp_WppRecorderIsDefaultLogAvailable(
    _In_ PVOID WppCb)
{
    UNREFERENCED_PARAMETER(WppCb);
    return FALSE;
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
