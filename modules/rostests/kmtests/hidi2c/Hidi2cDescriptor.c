/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     HID-I2C device descriptor validation tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "hidi2c.h"
#include "Hidi2cTest.h"

static const HIDI2C_DEVICE_DESCRIPTOR ValidDescriptor =
{
    sizeof(HIDI2C_DEVICE_DESCRIPTOR),
    0x0100,
    128,
    1,
    2,
    64,
    3,
    64,
    4,
    5,
    0x04F3,
    0,
    0,
    0
};

#define CHECK_INVALID_FIELD(Field, Value) \
    do \
    { \
        Descriptor = ValidDescriptor; \
        Descriptor.Field = (Value); \
        Status = Hidi2cValidateDeviceDescriptor(&Descriptor); \
        ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR); \
    } while (0)

static
VOID
TestDescriptorValidation(VOID)
{
    HIDI2C_DEVICE_DESCRIPTOR Descriptor;
    USHORT *Registers[5];
    NTSTATUS Status;
    ULONG Index, OtherIndex;

    Descriptor = ValidDescriptor;
    Status = Hidi2cValidateDeviceDescriptor(&Descriptor);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Descriptor.OutputRegister = 0;
    Descriptor.MaxOutputLength = 0;
    Status = Hidi2cValidateDeviceDescriptor(&Descriptor);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Descriptor = ValidDescriptor;
    Descriptor.ReportDescriptorLength = MAXUSHORT;
    Descriptor.MaxInputLength = MAXUSHORT;
    Descriptor.MaxOutputLength = MAXUSHORT;
    Status = Hidi2cValidateDeviceDescriptor(&Descriptor);
    ok_eq_hex(Status, STATUS_SUCCESS);

    CHECK_INVALID_FIELD(HidDescriptorLength,
                        sizeof(HIDI2C_DEVICE_DESCRIPTOR) - 1);
    CHECK_INVALID_FIELD(BcdVersion, 0x0101);
    CHECK_INVALID_FIELD(ReportDescriptorLength, 0);
    CHECK_INVALID_FIELD(ReportDescriptorRegister, 0);
    CHECK_INVALID_FIELD(InputRegister, 0);
    CHECK_INVALID_FIELD(MaxInputLength, 0);
    CHECK_INVALID_FIELD(MaxInputLength, 1);
    CHECK_INVALID_FIELD(CommandRegister, 0);
    CHECK_INVALID_FIELD(DataRegister, 0);
    CHECK_INVALID_FIELD(VendorId, 0);

    Descriptor = ValidDescriptor;
    Registers[0] = &Descriptor.ReportDescriptorRegister;
    Registers[1] = &Descriptor.InputRegister;
    Registers[2] = &Descriptor.OutputRegister;
    Registers[3] = &Descriptor.CommandRegister;
    Registers[4] = &Descriptor.DataRegister;
    for (Index = 0; Index < RTL_NUMBER_OF(Registers); Index++)
    {
        for (OtherIndex = Index + 1;
             OtherIndex < RTL_NUMBER_OF(Registers);
             OtherIndex++)
        {
            Descriptor = ValidDescriptor;
            *Registers[OtherIndex] = *Registers[Index];
            Status = Hidi2cValidateDeviceDescriptor(&Descriptor);
            ok_eq_hex(Status, STATUS_DEVICE_CONFIGURATION_ERROR);
        }
    }
}

NTSTATUS
TestHidi2cDescriptor(
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
    NT_VERIFY(ControlCode == IOCTL_TEST_HIDI2C_DESCRIPTOR);

    TestDescriptorValidation();
    return STATUS_SUCCESS;
}
