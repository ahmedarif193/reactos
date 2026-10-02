/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB function attach detection interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

DEFINE_GUID(GUID_USBFN_INTERFACE_ATTACH,
0x3934502d, 0xe301, 0x4542, 0xbd, 0xbe, 0xb9, 0xa3, 0x59, 0xdc, 0x3a, 0xbb);

typedef enum _USBFN_ATTACH_ACTION {
    UsbfnPortDetected = 0,
    UsbfnPortDetectedNoCad,
    UsbfnProceedWithAttach,
    UsbfnIgnoreAttach,
    UsbfnDetectProprietaryCharger,
    UsbfnHwBasedChargerDetection,
} USBFN_ATTACH_ACTION, *PUSBFN_ATTACH_ACTION;

typedef struct _USBFN_ON_ATTACH {
    USBFN_PORT_TYPE PortType;
    USBFN_ATTACH_ACTION AttachAction;
} USBFN_ON_ATTACH, *PUSBFN_ON_ATTACH;

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
USBFN_GET_ATTACH_ACTION (
    _In_ PVOID Context,
    _Out_ PUSBFN_ON_ATTACH OnAttach
    );

typedef USBFN_GET_ATTACH_ACTION *PFN_USBFN_GET_ATTACH_ACTION;

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
USBFN_GET_ATTACH_ACTION_ABORT (
    _In_ PVOID Context
    );

typedef USBFN_GET_ATTACH_ACTION_ABORT *PFN_USBFN_GET_ATTACH_ACTION_ABORT;

typedef
NTSTATUS
_IRQL_requires_max_(PASSIVE_LEVEL)
USBFN_SET_DEVICE_STATE (
    _In_ PVOID Context,
    _In_ USBFN_DEVICE_STATE DeviceState,
    _In_ USBFN_BUS_SPEED BusSpeed
    );

typedef USBFN_SET_DEVICE_STATE *PFN_USBFN_SET_DEVICE_STATE;

typedef struct _USBFN_INTERFACE_ATTACH {
    INTERFACE                            InterfaceHeader;

    PFN_USBFN_GET_ATTACH_ACTION         GetAttachAction;
    PFN_USBFN_GET_ATTACH_ACTION_ABORT   GetAttachActionAbortOperation;
    PFN_USBFN_SET_DEVICE_STATE          SetDeviceState;
} USBFN_INTERFACE_ATTACH, *PUSBFN_INTERFACE_ATTACH;
