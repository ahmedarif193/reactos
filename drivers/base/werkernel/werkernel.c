/*
 * PROJECT:     LiberNT WER kernel support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Live kernel report exports for drivers linked with lkmdtel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

NTSTATUS
NTAPI
WerLiveKernelCreateReport(
    _In_ PCWSTR ReportType,
    _In_ PVOID ReportParameters,
    _Out_ PHANDLE ReportHandle)
{
    UNREFERENCED_PARAMETER(ReportType);
    UNREFERENCED_PARAMETER(ReportParameters);

    if (ReportHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    *ReportHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
WerLiveKernelOpenDumpFile(
    _In_ HANDLE ReportHandle,
    _Out_ PHANDLE FileHandle)
{
    UNREFERENCED_PARAMETER(ReportHandle);

    if (FileHandle != NULL)
        *FileHandle = NULL;
    return STATUS_INVALID_HANDLE;
}

NTSTATUS
NTAPI
WerLiveKernelSubmitReport(
    _In_ HANDLE ReportHandle,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(ReportHandle);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_INVALID_HANDLE;
}

NTSTATUS
NTAPI
WerLiveKernelCancelReport(
    _In_ HANDLE ReportHandle)
{
    UNREFERENCED_PARAMETER(ReportHandle);
    return STATUS_INVALID_HANDLE;
}

NTSTATUS
NTAPI
WerLiveKernelCloseHandle(
    _In_ HANDLE Handle)
{
    UNREFERENCED_PARAMETER(Handle);
    return STATUS_INVALID_HANDLE;
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
