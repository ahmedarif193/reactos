/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     HID-I2C transfer completion validation tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "hidi2c.h"
#include "Hidi2cTest.h"

static
VOID
TestTransferResult(VOID)
{
    NTSTATUS Status;

    Status = Hidi2cValidateTransferResult(STATUS_SUCCESS, 6, 6);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = Hidi2cValidateTransferResult(STATUS_SUCCESS, 5, 6);
    ok_eq_hex(Status, STATUS_DEVICE_PROTOCOL_ERROR);

    Status = Hidi2cValidateTransferResult(STATUS_SUCCESS, 7, 6);
    ok_eq_hex(Status, STATUS_DEVICE_PROTOCOL_ERROR);

    Status = Hidi2cValidateTransferResult(STATUS_IO_TIMEOUT, 0, 6);
    ok_eq_hex(Status, STATUS_IO_TIMEOUT);

    Status = Hidi2cValidateTransferResult(STATUS_CANCELLED, 6, 6);
    ok_eq_hex(Status, STATUS_CANCELLED);
}

NTSTATUS
TestHidi2cTransfer(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    PAGED_CODE();
    NT_VERIFY(ControlCode == IOCTL_TEST_HIDI2C_TRANSFER);

    TestTransferResult();
    return STATUS_SUCCESS;
}
