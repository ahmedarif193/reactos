/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     NT-compatible filesystem driver shell for staged NTFS porting
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

NTFSLX_GLOBAL_DATA NtfslxGlobalData;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(NTFSLX_DEVICE_NAME);
    PDEVICE_OBJECT DeviceObject;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlZeroMemory(&NtfslxGlobalData, sizeof(NtfslxGlobalData));

    Status = IoCreateDevice(DriverObject,
                            sizeof(NTFSLX_DEVICE_EXTENSION),
                            &DeviceName,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateDevice failed with status %lx\n", Status);
        return Status;
    }

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    DeviceExtension->Signature = NTFSLX_TAG;
    DeviceExtension->Kind = NtfslxDeviceKindControl;
    DeviceExtension->DeviceObject = DeviceObject;

    Status = ExInitializeResourceLite(&DeviceExtension->Resource);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ExInitializeResourceLite failed with status %lx\n", Status);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    NtfslxGlobalData.DriverObject = DriverObject;
    NtfslxGlobalData.ControlDeviceObject = DeviceObject;

    NtfslxInitializeFunctionPointers(DriverObject);
    DriverObject->DriverUnload = NULL;

    DeviceObject->Flags |= DO_DIRECT_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    IoRegisterFileSystem(DeviceObject);
    ObReferenceObject(DeviceObject);

    return STATUS_SUCCESS;
}

VOID
NtfslxInitializeFunctionPointers(
    _In_ PDRIVER_OBJECT DriverObject)
{
    ULONG Index;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
    {
        DriverObject->MajorFunction[Index] = NtfslxFsdDispatch;
    }
}
