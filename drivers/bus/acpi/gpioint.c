/*
 * PROJECT:     ReactOS ACPI bus driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Secondary interrupt (GpioInt) vector allocation through the HAL
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntifs.h>
#include <ndk/halfuncs.h>

NTSTATUS
BuspAllocateSecondaryGsiv(
    _In_ PDEVICE_OBJECT Controller,
    _Out_ PULONG Gsiv)
{
    WCHAR Buffer[96];
    UNICODE_STRING Name;
    ANSI_STRING OwnerName;
    ULONG Length = 0;
    NTSTATUS Status;

    *Gsiv = 0;
    if (HalAllocateGsivForSecondaryInterrupt == NULL)
        return STATUS_NOT_SUPPORTED;
    Status = IoGetDeviceProperty(Controller, DevicePropertyPhysicalDeviceObjectName, sizeof(Buffer), Buffer, &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    Name.Buffer = Buffer;
    Name.Length = (USHORT)(Length >= sizeof(WCHAR) ? Length - sizeof(WCHAR) : 0);
    Name.MaximumLength = sizeof(Buffer);
    Status = RtlUnicodeStringToAnsiString(&OwnerName, &Name, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = HalAllocateGsivForSecondaryInterrupt(OwnerName.Buffer, OwnerName.Length, Gsiv);
    RtlFreeAnsiString(&OwnerName);
    return Status;
}
