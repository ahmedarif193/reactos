/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PnP driver whose AddDevice fails after deleting its only device object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

static WCHAR ServicePath[260];
static volatile LONG UnloadCalled;

static VOID NTAPI TestUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    InterlockedExchange(&UnloadCalled, 1);
}

static NTSTATUS NTAPI TestAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT Fdo;
    ULONG UnloadDuringAddDevice;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Pdo);

    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    IoDeleteDevice(Fdo);

    UnloadDuringAddDevice = (ULONG)UnloadCalled;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, L"UnloadDuringAddDevice", REG_DWORD,
                          &UnloadDuringAddDevice, sizeof(UnloadDuringAddDevice));
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    if (RegistryPath->Length >= sizeof(ServicePath))
        return STATUS_NAME_TOO_LONG;
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    DriverObject->DriverUnload = TestUnload;
    DriverObject->DriverExtension->AddDevice = TestAddDevice;
    return STATUS_SUCCESS;
}
