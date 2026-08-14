/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite device stack API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(IoStackKM)
{
    PDEVICE_OBJECT Lower = NULL;
    PDEVICE_OBJECT Upper = NULL;
    PDEVICE_OBJECT Attached;
    PDEVICE_OBJECT Result;
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Lower);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Upper);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(Lower);
        return;
    }

    /* Dynamically created devices are ready for stack operations now. */
    Lower->Flags &= ~DO_DEVICE_INITIALIZING;
    Upper->Flags &= ~DO_DEVICE_INITIALIZING;

    Attached = IoGetAttachedDeviceReference(Lower);
    ok_eq_pointer(Attached, Lower);
    ObDereferenceObject(Attached);

    Result = IoAttachDeviceToDeviceStack(Upper, Lower);
    ok_eq_pointer(Result, Lower);
    if (!Result)
    {
        IoDeleteDevice(Upper);
        IoDeleteDevice(Lower);
        return;
    }

    Attached = IoGetAttachedDeviceReference(Lower);
    ok_eq_pointer(Attached, Upper);
    ObDereferenceObject(Attached);

    IoDetachDevice(Lower);

    Attached = IoGetAttachedDeviceReference(Lower);
    ok_eq_pointer(Attached, Lower);
    ObDereferenceObject(Attached);

    IoDeleteDevice(Upper);
    IoDeleteDevice(Lower);
}
