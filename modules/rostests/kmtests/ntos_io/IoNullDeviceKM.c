/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite IoGetDeviceObjectPointer API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(IoNullDeviceKM)
{
    UNICODE_STRING Name;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    PDEVICE_OBJECT Related;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\Device\\Null");
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    ok(FileObject != NULL, "file object NULL\n");
    ok(DeviceObject != NULL, "device object NULL\n");

    if (FileObject != NULL && DeviceObject != NULL)
    {
        ok_eq_pointer(FileObject->DeviceObject, DeviceObject);

        Related = IoGetRelatedDeviceObject(FileObject);
        ok(Related != NULL, "related device NULL\n");

        ok_eq_uint(DeviceObject->Type, IO_TYPE_DEVICE);
    }

    if (FileObject != NULL)
        ObDereferenceObject(FileObject);

    RtlInitUnicodeString(&Name, L"\\Device\\KmtNoSuchDevice");
    FileObject = NULL;
    DeviceObject = NULL;
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &DeviceObject);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
}
