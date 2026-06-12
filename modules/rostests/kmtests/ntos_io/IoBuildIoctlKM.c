/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite IoBuildDeviceIoControlRequest API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(IoBuildIoctlKM)
{
    UNICODE_STRING Name;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    UCHAR OutBuffer[8];

    RtlInitUnicodeString(&Name, L"\\Device\\Null");
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    RtlZeroMemory(&IoStatus, sizeof(IoStatus));
    IoStatus.Status = STATUS_PENDING;

    Irp = IoBuildDeviceIoControlRequest(CTL_CODE(FILE_DEVICE_NULL, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS), DeviceObject, NULL, 0, OutBuffer, sizeof(OutBuffer), FALSE, &Event, &IoStatus);
    ok(Irp != NULL, "IoBuildDeviceIoControlRequest failed\n");
    if (Irp != NULL)
    {
        IoStack = IoGetNextIrpStackLocation(Irp);
        ok_eq_uint(IoStack->MajorFunction, IRP_MJ_DEVICE_CONTROL);
        ok_eq_pointer(IoStack->DeviceObject, NULL);

        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatus.Status;
        }
        ok_eq_hex(Status, STATUS_INVALID_DEVICE_REQUEST);
    }

    ObDereferenceObject(FileObject);
}
