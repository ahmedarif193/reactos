/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     NT-compatible filesystem driver shell for staged NTFS porting
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

NTFSLX_GLOBAL_DATA NtfslxGlobalData;

static FAST_IO_DISPATCH NtfslxFastIoDispatch;

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

    /*
     * Install a minimal fast-I/O dispatch table. Without it NtWriteFile /
     * NtReadFile assert on writes to cached files. We use the generic
     * FsRtlCopyRead / FsRtlCopyWrite helpers which go through CcCopyRead /
     * CcCopyWrite on the shared cache map set up by NtfslxCacheRuntimeAttach.
     * FastIoCheckIfPossible stays NULL — the kernel's fallback is "not
     * possible, use the IRP path", which is what we want when Cc can't
     * satisfy the request (we then end up in IRP_MJ_READ / IRP_MJ_WRITE).
     * AcquireFileForNtCreateSection / ReleaseFileForNtCreateSection also
     * stay NULL so the kernel's default handler acquires FcbHeader.Resource
     * directly — pointing them at FsRtlAcquireFileExclusive / FsRtlReleaseFile
     * would recurse because those are the default handlers themselves.
     */
    RtlZeroMemory(&NtfslxFastIoDispatch, sizeof(NtfslxFastIoDispatch));
    NtfslxFastIoDispatch.SizeOfFastIoDispatch = sizeof(FAST_IO_DISPATCH);
    NtfslxFastIoDispatch.FastIoRead = FsRtlCopyRead;
    NtfslxFastIoDispatch.FastIoWrite = FsRtlCopyWrite;
    DriverObject->FastIoDispatch = &NtfslxFastIoDispatch;

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
