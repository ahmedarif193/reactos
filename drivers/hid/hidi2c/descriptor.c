/*
 * PROJECT:     ReactOS HID over I2C minidriver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HID-I2C device descriptor validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "hidi2c.h"

NTSTATUS
Hidi2cValidateDeviceDescriptor(
    _In_ const HIDI2C_DEVICE_DESCRIPTOR *Descriptor)
{
    const USHORT Registers[] =
    {
        Descriptor->ReportDescriptorRegister,
        Descriptor->InputRegister,
        Descriptor->OutputRegister,
        Descriptor->CommandRegister,
        Descriptor->DataRegister
    };
    ULONG Index, OtherIndex;

    if (Descriptor->HidDescriptorLength != sizeof(*Descriptor) ||
        Descriptor->BcdVersion != 0x0100 ||
        Descriptor->ReportDescriptorLength == 0 ||
        Descriptor->ReportDescriptorRegister == 0 ||
        Descriptor->InputRegister == 0 ||
        Descriptor->MaxInputLength < sizeof(USHORT) ||
        Descriptor->CommandRegister == 0 ||
        Descriptor->DataRegister == 0 ||
        Descriptor->VendorId == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Registers); Index++)
    {
        if (Registers[Index] == 0)
            continue;

        for (OtherIndex = Index + 1;
             OtherIndex < RTL_NUMBER_OF(Registers);
             OtherIndex++)
        {
            if (Registers[Index] == Registers[OtherIndex])
                return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    return STATUS_SUCCESS;
}
