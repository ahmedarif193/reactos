/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     HID-I2C descriptor test driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "Hidi2cTest.h"

KMT_MESSAGE_HANDLER TestHidi2cDescriptor;

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    PAGED_CODE();

    *DeviceName = L"Hidi2cDescriptor";
    *Flags = TESTENTRY_NO_EXCLUSIVE_DEVICE;
    KmtRegisterMessageHandler(IOCTL_TEST_HIDI2C_DESCRIPTOR,
                              NULL,
                              TestHidi2cDescriptor);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    PAGED_CODE();
}
