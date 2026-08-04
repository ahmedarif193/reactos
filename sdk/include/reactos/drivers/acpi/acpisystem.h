/*
 * PROJECT:         ReactOS ACPI system services interface
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Kernel access to ACPI system description tables
 */

#pragma once

/* {4B4B1E0F-4EBF-46A7-92A5-DB27D3B9B6A1} */
DEFINE_GUID(GUID_ACPI_SYSTEM_INTERFACE,
    0x4B4B1E0F, 0x4EBF, 0x46A7,
    0x92, 0xA5, 0xDB, 0x27, 0xD3, 0xB9, 0xB6, 0xA1);

#define IOCTL_ACPI_GET_SYSTEM_TABLE CTL_CODE(FILE_DEVICE_ACPI, 0x11, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _ACPI_GET_SYSTEM_TABLE_INPUT
{
    CHAR Signature[4];
    ULONG Instance;
} ACPI_GET_SYSTEM_TABLE_INPUT, *PACPI_GET_SYSTEM_TABLE_INPUT;
