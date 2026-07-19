/*
 * PROJECT:     ReactOS Intel LPSS / DesignWare I2C driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver-wide definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <reactos/drivers/spb/spb.h>

#define DW_DRIVER_TAG 'wD2I'

typedef struct _DW_I2C_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    PUCHAR Registers;
    ULONG RegistersLength;
    PHYSICAL_ADDRESS PhysicalBase;
    ULONG ControllerClockHz;
    ULONG TxFifoDepth;
    ULONG RxFifoDepth;
    ULONG Segment;
    ULONG BusNumber;
    ULONG Address;
    UNICODE_STRING InterfaceName;
    KSEMAPHORE TransferSemaphore;
    BOOLEAN InterfaceRegistered;
    BOOLEAN InterfaceEnabled;
    BOOLEAN Started;
    volatile LONG ControllerExternallyLocked;
    DEVICE_POWER_STATE DevicePowerState;
} DW_I2C_DEVICE_EXTENSION, *PDW_I2C_DEVICE_EXTENSION;

NTSTATUS
DwInitializeHardware(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension);

VOID
DwQuiesceHardware(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
DwExecuteTransferList(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PREACTOS_SPB_TRANSFER_LIST TransferList,
    _Out_ PULONG_PTR Information);
