/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB function class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usbfnbase.h>

DECLARE_HANDLE(UFXDEVICE);
DECLARE_HANDLE(UFXENDPOINT);

typedef struct _UFXENDPOINT_INIT *PUFXENDPOINT_INIT;

typedef enum _UFX_CLASS_FUNCTIONS {
    UfxFdoInitIndex = 0,
    UfxDeviceCreateIndex,
    UfxDeviceEventCompleteIndex,
    UfxDeviceNotifyHardwareReadyIndex,
    UfxDeviceNotifyAttachIndex,
    UfxDeviceNotifyDetachIndex,
    UfxDeviceNotifySuspendIndex,
    UfxDeviceNotifyResumeIndex,
    UfxDeviceNotifyResetIndex,
    UfxDevicePortDetectCompleteIndex,
    UfxDeviceProprietaryChargerDetectCompleteIndex,
    UfxDeviceNotifyHardwareFailureIndex,
    UfxDeviceIoControlIndex,
    UfxDeviceIoInternalControlIndex,
    UfxEndpointCreateIndex,
    UfxEndpointInitSetEventCallbacksIndex,
    UfxEndpointNotifySetupIndex,
    UfxEndpointGetTransferQueueIndex,
    UfxEndpointGetCommandQueueIndex,
    UfxDevicePortDetectCompleteExIndex,
    UfxDeviceNotifyFinalExitIndex,
    UfxFunctionsMax
} UFX_CLASS_FUNCTIONS;

typedef void (*PFN_UFX_CLASS)(void);
extern PFN_UFX_CLASS UfxClassFunctions[UfxFunctionsMax];

typedef struct _UFX_GLOBALS {

    ULONG Size;
    PWDF_DRIVER_GLOBALS WdfDriverGlobals;
} UFX_GLOBALS, *PUFX_GLOBALS;

typedef struct _UFX_DEVICE_CAPABILITIES {
    ULONG Size;
    USB_DEVICE_SPEED MaxSpeed;

    ULONG RemoteWakeSignalDelay;
    BOOLEAN PdcpSupported;

    USHORT InEndpointBitmap;
    USHORT OutEndpointBitmap;
    BOOLEAN SharesConnectors;
    ULONG GroupId;
} UFX_DEVICE_CAPABILITIES, *PUFX_DEVICE_CAPABILITIES;

typedef struct _UFX_HARDWARE_FAILURE_CONTEXT {
    ULONG Size;
    ULONG ExceptionCode;
    UCHAR Data[1];
} UFX_HARDWARE_FAILURE_CONTEXT, *PUFX_HARDWARE_FAILURE_CONTEXT;

typedef enum _USBFN_ACTION {
    UsbfnActionNone = 0,
    UsbfnActionNoCad,
    UsbfnActionDetectProprietaryCharger
} USBFN_ACTION, *PUSBFN_ACTION;

extern PUFX_GLOBALS UfxGlobals;

#define IOCTL_INTERNAL_USBFN_DESCRIPTOR_UPDATE             CTL_CODE(FILE_DEVICE_BUS_EXTENDER,  \
                                                                    0x100,                     \
                                                                    METHOD_IN_DIRECT,          \
                                                                    FILE_READ_DATA |           \
                                                                    FILE_WRITE_DATA)
