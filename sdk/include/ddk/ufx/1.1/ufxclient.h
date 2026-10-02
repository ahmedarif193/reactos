/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB function class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ufxbase.h>
#include <ufxproprietarycharger.h>
#include <usbfnioctl.h>
#include <usbfnattach.h>

typedef
_Function_class_(EVT_UFX_DEVICE_HOST_CONNECT)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EVT_UFX_DEVICE_HOST_CONNECT (
    _In_ UFXDEVICE
    );

typedef EVT_UFX_DEVICE_HOST_CONNECT *PFN_UFX_DEVICE_HOST_CONNECT;

typedef
_Function_class_(EVT_UFX_DEVICE_HOST_DISCONNECT)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EVT_UFX_DEVICE_HOST_DISCONNECT (
    _In_ UFXDEVICE
    );

typedef EVT_UFX_DEVICE_HOST_DISCONNECT *PFN_UFX_DEVICE_HOST_DISCONNECT;

typedef
_Function_class_(EVT_UFX_DEVICE_ADDRESSED)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EVT_UFX_DEVICE_ADDRESSED (
    _In_ UFXDEVICE,
    _In_ USHORT
    );

typedef EVT_UFX_DEVICE_ADDRESSED *PFN_UFX_DEVICE_ADDRESSED;

typedef
_Function_class_(EVT_UFX_DEVICE_ENDPOINT_ADD)
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
EVT_UFX_DEVICE_ENDPOINT_ADD (
    _In_ UFXDEVICE,
    _In_ const PUSB_ENDPOINT_DESCRIPTOR,
    _Inout_ PUFXENDPOINT_INIT
    );

typedef EVT_UFX_DEVICE_ENDPOINT_ADD *PFN_UFX_DEVICE_ENDPOINT_ADD;

typedef
_Function_class_(EVT_UFX_DEVICE_DEFAULT_ENDPOINT_ADD)
_IRQL_requires_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_DEFAULT_ENDPOINT_ADD (
    _In_ UFXDEVICE,
    _In_ USHORT,
    _Inout_ PUFXENDPOINT_INIT
    );

typedef EVT_UFX_DEVICE_DEFAULT_ENDPOINT_ADD *PFN_UFX_DEVICE_DEFAULT_ENDPOINT_ADD;

typedef
_Function_class_(EVT_UFX_DEVICE_USB_STATE_CHANGE)
_IRQL_requires_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_USB_STATE_CHANGE (
    _In_ UFXDEVICE,
    _In_ USBFN_DEVICE_STATE
    );

typedef EVT_UFX_DEVICE_USB_STATE_CHANGE *PFN_UFX_DEVICE_USB_STATE_CHANGE;

typedef
_Function_class_(EVT_UFX_DEVICE_PORT_CHANGE)
_IRQL_requires_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_PORT_CHANGE (
    _In_ UFXDEVICE,
    _In_ USBFN_PORT_TYPE
    );

typedef EVT_UFX_DEVICE_PORT_CHANGE *PFN_UFX_DEVICE_PORT_CHANGE;

typedef
_Function_class_(EVT_UFX_DEVICE_PORT_DETECT)
_IRQL_requires_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_PORT_DETECT (
    _In_ UFXDEVICE
    );

typedef EVT_UFX_DEVICE_PORT_DETECT *PFN_UFX_DEVICE_PORT_DETECT;

typedef
_Function_class_(EVT_UFX_DEVICE_REMOTE_WAKEUP_SIGNAL)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_REMOTE_WAKEUP_SIGNAL (
    _In_ UFXDEVICE
    );

typedef EVT_UFX_DEVICE_REMOTE_WAKEUP_SIGNAL *PFN_UFX_DEVICE_REMOTE_WAKEUP_SIGNAL;

typedef
_Function_class_(EVT_UFX_DEVICE_CONTROLLER_RESET)
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_CONTROLLER_RESET (
    _In_ UFXDEVICE,
    _In_opt_ PUFX_HARDWARE_FAILURE_CONTEXT
    );

typedef EVT_UFX_DEVICE_CONTROLLER_RESET *PFN_UFX_DEVICE_CONTROLLER_RESET;

typedef
_Function_class_(EVT_UFX_DEVICE_TEST_MODE_SET)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EVT_UFX_DEVICE_TEST_MODE_SET (
    _In_ UFXDEVICE,
    _In_ ULONG
    );

typedef EVT_UFX_DEVICE_TEST_MODE_SET *PFN_UFX_DEVICE_TEST_MODE_SET;

typedef
_Function_class_(EVT_UFX_DEVICE_TESTHOOK)
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
EVT_UFX_DEVICE_TESTHOOK (
    _In_ UFXDEVICE,
    _In_ PVOID,
    _In_ size_t,
    _Out_ PVOID,
    _In_ size_t
    );

typedef EVT_UFX_DEVICE_TESTHOOK *PFN_UFX_DEVICE_TESTHOOK;

typedef
_Function_class_(EVT_UFX_DEVICE_SUPER_SPEED_POWER_FEATURE)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EVT_UFX_DEVICE_SUPER_SPEED_POWER_FEATURE (
    _In_ UFXDEVICE,
    _In_ USHORT,
    _In_ BOOLEAN
    );

typedef EVT_UFX_DEVICE_SUPER_SPEED_POWER_FEATURE *PFN_UFX_DEVICE_SUPER_SPEED_POWER_FEATURE;

typedef
_Function_class_(EVT_UFX_DEVICE_DETECT_PROPRIETARY_CHARGER)
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_PROPRIETARY_CHARGER_DETECT (
    _In_ UFXDEVICE
    );

typedef EVT_UFX_DEVICE_PROPRIETARY_CHARGER_DETECT *PFN_UFX_DEVICE_PROPRIETARY_CHARGER_DETECT;

typedef
_Function_class_(EVT_UFX_DEVICE_PROPRIETARY_CHARGER_SET_PROPERTY)
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
EVT_UFX_DEVICE_PROPRIETARY_CHARGER_SET_PROPERTY (
    _In_ UFXDEVICE,
    _In_ WDFREQUEST
    );

typedef EVT_UFX_DEVICE_PROPRIETARY_CHARGER_SET_PROPERTY *PFN_UFX_DEVICE_PROPRIETARY_CHARGER_SET_PROPERTY;

typedef
_Function_class_(EVT_UFX_DEVICE_PROPRIETARY_CHARGER_RESET)
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
EVT_UFX_DEVICE_PROPRIETARY_CHARGER_RESET (
    _In_ UFXDEVICE
    );

typedef EVT_UFX_DEVICE_PROPRIETARY_CHARGER_RESET *PFN_UFX_DEVICE_PROPRIETARY_CHARGER_RESET;

typedef struct _UFX_DEVICE_CALLBACKS {
    ULONG Size;
    PFN_UFX_DEVICE_HOST_CONNECT EvtDeviceHostConnect;
    PFN_UFX_DEVICE_HOST_DISCONNECT EvtDeviceHostDisconnect;
    PFN_UFX_DEVICE_ADDRESSED EvtDeviceAddressed;
    PFN_UFX_DEVICE_ENDPOINT_ADD EvtDeviceEndpointAdd;
    PFN_UFX_DEVICE_DEFAULT_ENDPOINT_ADD EvtDeviceDefaultEndpointAdd;
    PFN_UFX_DEVICE_USB_STATE_CHANGE EvtDeviceUsbStateChange;
    PFN_UFX_DEVICE_PORT_CHANGE EvtDevicePortChange;
    PFN_UFX_DEVICE_PORT_DETECT EvtDevicePortDetect;
    PFN_UFX_DEVICE_REMOTE_WAKEUP_SIGNAL EvtDeviceRemoteWakeupSignal;
    PFN_UFX_DEVICE_CONTROLLER_RESET EvtDeviceControllerReset;
    PFN_UFX_DEVICE_TEST_MODE_SET EvtDeviceTestModeSet;
    PFN_UFX_DEVICE_TESTHOOK EvtDeviceTestHook;
    PFN_UFX_DEVICE_SUPER_SPEED_POWER_FEATURE EvtDeviceSuperSpeedPowerFeature;
    PFN_UFX_DEVICE_PROPRIETARY_CHARGER_DETECT EvtDeviceProprietaryChargerDetect;
    PFN_UFX_DEVICE_PROPRIETARY_CHARGER_SET_PROPERTY EvtDeviceProprietaryChargerSetProperty;
    PFN_UFX_DEVICE_PROPRIETARY_CHARGER_RESET EvtDeviceProprietaryChargerReset;
} UFX_DEVICE_CALLBACKS, *PUFX_DEVICE_CALLBACKS;

VOID
FORCEINLINE
UFX_DEVICE_CALLBACKS_INIT (
    _Out_ PUFX_DEVICE_CALLBACKS Callbacks
    )
{
    RtlZeroMemory(Callbacks, sizeof(UFX_DEVICE_CALLBACKS));
    Callbacks->Size = sizeof(UFX_DEVICE_CALLBACKS);
}

VOID
FORCEINLINE
UFX_DEVICE_CAPABILITIES_INIT (
    _Out_ PUFX_DEVICE_CAPABILITIES Capabilities
    )
{
    RtlZeroMemory(Capabilities, sizeof(UFX_DEVICE_CAPABILITIES));
    Capabilities->Size = sizeof(UFX_DEVICE_CAPABILITIES);
    Capabilities->InEndpointBitmap = 0xFFFF;
    Capabilities->OutEndpointBitmap = 0xFFFF;
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
UFX_FDO_INIT (
    _In_ PUFX_GLOBALS,
    _In_ WDFDRIVER,
    _Inout_ PWDFDEVICE_INIT,
    _Inout_ PWDF_OBJECT_ATTRIBUTES
    );

typedef UFX_FDO_INIT *PFN_UFX_FDO_INIT;

_Must_inspect_result_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FORCEINLINE
UfxFdoInit (
    _In_ WDFDRIVER WdfDriver,
    _Inout_ PWDFDEVICE_INIT DeviceInit,
    _Inout_ PWDF_OBJECT_ATTRIBUTES FdoAttributes
    )
{
    return((PFN_UFX_FDO_INIT)
           UfxClassFunctions[UfxFdoInitIndex])(UfxGlobals,
                                               WdfDriver,
                                               DeviceInit,
                                               FdoAttributes);
}

typedef
_Must_inspect_result_
_IRQL_requires_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
UFX_DEVICE_CREATE (
    _In_ PUFX_GLOBALS,
    _In_ WDFDEVICE,
    _In_ PUFX_DEVICE_CALLBACKS,
    _In_ PUFX_DEVICE_CAPABILITIES,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES,
    _Out_ UFXDEVICE*
    );

typedef UFX_DEVICE_CREATE *PFN_UFX_DEVICE_CREATE;

_Must_inspect_result_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FORCEINLINE
UfxDeviceCreate (
    _In_ WDFDEVICE WdfDevice,
    _In_ PUFX_DEVICE_CALLBACKS Callbacks,
    _In_ PUFX_DEVICE_CAPABILITIES Capabilities,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _Out_ UFXDEVICE* UfxDevice
    )
{
    return ((PFN_UFX_DEVICE_CREATE)
            UfxClassFunctions[UfxDeviceCreateIndex])(UfxGlobals,
                                                     WdfDevice,
                                                     Callbacks,
                                                     Capabilities,
                                                     Attributes,
                                                     UfxDevice);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_EVENT_COMPLETE (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ NTSTATUS
    );

typedef UFX_DEVICE_EVENT_COMPLETE *PFN_UFX_DEVICE_EVENT_COMPLETE;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceEventComplete (
    _In_ UFXDEVICE UfxDevice,
    _In_ NTSTATUS Status
    )
{
    ((PFN_UFX_DEVICE_EVENT_COMPLETE)
     UfxClassFunctions[UfxDeviceEventCompleteIndex])(UfxGlobals,
                                                     UfxDevice,
                                                     Status);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_HARDWARE_READY (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE
    );

typedef UFX_DEVICE_NOTIFY_HARDWARE_READY *PFN_UFX_DEVICE_NOTIFY_HARDWARE_READY;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifyHardwareReady (
    _In_ UFXDEVICE UfxDevice
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_HARDWARE_READY)
     UfxClassFunctions[UfxDeviceNotifyHardwareReadyIndex])(UfxGlobals,
                                                           UfxDevice);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_ATTACH (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE
    );

typedef UFX_DEVICE_NOTIFY_ATTACH *PFN_UFX_DEVICE_NOTIFY_ATTACH;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifyAttach (
    _In_ UFXDEVICE UfxDevice
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_ATTACH)
     UfxClassFunctions[UfxDeviceNotifyAttachIndex])(UfxGlobals,
                                                    UfxDevice);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_DETACH (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE
    );

typedef UFX_DEVICE_NOTIFY_DETACH *PFN_UFX_DEVICE_NOTIFY_DETACH;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifyDetach (
    _In_ UFXDEVICE UfxDevice
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_DETACH)
     UfxClassFunctions[UfxDeviceNotifyDetachIndex])(UfxGlobals,
                                                    UfxDevice);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_SUSPEND (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE
    );

typedef UFX_DEVICE_NOTIFY_SUSPEND *PFN_UFX_DEVICE_NOTIFY_SUSPEND;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifySuspend (
    _In_ UFXDEVICE UfxDevice
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_SUSPEND)
     UfxClassFunctions[UfxDeviceNotifySuspendIndex])(UfxGlobals,
                                                     UfxDevice);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_RESUME (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE
    );

typedef UFX_DEVICE_NOTIFY_RESUME *PFN_UFX_DEVICE_NOTIFY_RESUME;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifyResume (
    _In_ UFXDEVICE UfxDevice
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_RESUME)
     UfxClassFunctions[UfxDeviceNotifyResumeIndex])(UfxGlobals,
                                                    UfxDevice);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_RESET (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ USB_DEVICE_SPEED
    );

typedef UFX_DEVICE_NOTIFY_RESET *PFN_UFX_DEVICE_NOTIFY_RESET;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifyReset (
    _In_ UFXDEVICE UfxDevice,
    _In_ USB_DEVICE_SPEED DeviceSpeed
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_RESET)
     UfxClassFunctions[UfxDeviceNotifyResetIndex])(UfxGlobals,
                                                   UfxDevice,
                                                   DeviceSpeed);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_PORT_DETECT_COMPLETE (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ USBFN_PORT_TYPE
    );

typedef UFX_DEVICE_PORT_DETECT_COMPLETE *PFN_UFX_DEVICE_PORT_DETECT_COMPLETE;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDevicePortDetectComplete (
    _In_ UFXDEVICE UfxDevice,
    _In_ USBFN_PORT_TYPE PortType
    )
{
    ((PFN_UFX_DEVICE_PORT_DETECT_COMPLETE)
     UfxClassFunctions[UfxDevicePortDetectCompleteIndex])(UfxGlobals,
                                                          UfxDevice,
                                                          PortType);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_PORT_DETECT_COMPLETE_EX (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ USBFN_PORT_TYPE,
    _In_ USBFN_ACTION
    );

typedef UFX_DEVICE_PORT_DETECT_COMPLETE_EX *PFN_UFX_DEVICE_PORT_DETECT_COMPLETE_EX;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDevicePortDetectCompleteEx (
    _In_ UFXDEVICE UfxDevice,
    _In_ USBFN_PORT_TYPE PortType,
    _In_ USBFN_ACTION Action
    )
{
    ((PFN_UFX_DEVICE_PORT_DETECT_COMPLETE_EX)
     UfxClassFunctions[UfxDevicePortDetectCompleteExIndex])(UfxGlobals,
                                                            UfxDevice,
                                                            PortType,
                                                            Action);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
UFX_DEVICE_PROPRIETARY_CHARGER_DETECT_COMPLETE (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ PUFX_PROPRIETARY_CHARGER
    );

typedef UFX_DEVICE_PROPRIETARY_CHARGER_DETECT_COMPLETE *PFN_UFX_DEVICE_PROPRIETARY_CHARGER_DETECT_COMPLETE;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
FORCEINLINE
UfxDeviceProprietaryChargerDetectComplete (
    _In_ UFXDEVICE UfxDevice,
    _In_ PUFX_PROPRIETARY_CHARGER DetectedCharger
    )
{
    ((PFN_UFX_DEVICE_PROPRIETARY_CHARGER_DETECT_COMPLETE)
     UfxClassFunctions[UfxDeviceProprietaryChargerDetectCompleteIndex])(UfxGlobals,
                                                                        UfxDevice,
                                                                        DetectedCharger);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_HARDWARE_FAILURE (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ PUFX_HARDWARE_FAILURE_CONTEXT
    );

typedef UFX_DEVICE_NOTIFY_HARDWARE_FAILURE *PFN_UFX_DEVICE_NOTIFY_HARDWARE_FAILURE;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifyHardwareFailure (
    _In_ UFXDEVICE UfxDevice,
    _In_opt_ PUFX_HARDWARE_FAILURE_CONTEXT HardwareFailureContext
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_HARDWARE_FAILURE)
     UfxClassFunctions[UfxDeviceNotifyHardwareFailureIndex])(UfxGlobals,
                                                             UfxDevice,
                                                             HardwareFailureContext);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
UFX_DEVICE_IO_CONTROL (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ WDFREQUEST,
    _In_ size_t,
    _In_ size_t,
    _In_ ULONG
    );

typedef UFX_DEVICE_IO_CONTROL *PFN_UFX_DEVICE_IO_CONTROL;

_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
FORCEINLINE
UfxDeviceIoControl (
    _In_ UFXDEVICE UfxDevice,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    return ((PFN_UFX_DEVICE_IO_CONTROL)
            UfxClassFunctions[UfxDeviceIoControlIndex])(UfxGlobals,
                                                        UfxDevice,
                                                        Request,
                                                        OutputBufferLength,
                                                        InputBufferLength,
                                                        IoControlCode);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
UFX_DEVICE_IO_INTERNAL_CONTROL (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _In_ WDFREQUEST,
    _In_ size_t,
    _In_ size_t,
    _In_ ULONG
    );

typedef UFX_DEVICE_IO_INTERNAL_CONTROL *PFN_UFX_DEVICE_IO_INTERNAL_CONTROL;

_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
FORCEINLINE
UfxDeviceIoInternalControl (
    _In_ UFXDEVICE UfxDevice,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    return ((PFN_UFX_DEVICE_IO_INTERNAL_CONTROL)
            UfxClassFunctions[UfxDeviceIoInternalControlIndex])(UfxGlobals,
                                                                UfxDevice,
                                                                Request,
                                                                OutputBufferLength,
                                                                InputBufferLength,
                                                                IoControlCode);
}

typedef struct _UFX_ENDPOINT_CALLBACKS {
    ULONG Size;
} UFX_ENDPOINT_CALLBACKS, *PUFX_ENDPOINT_CALLBACKS;

VOID
FORCEINLINE
UFX_ENDPOINT_CALLBACKS_INIT (
    _Out_ PUFX_ENDPOINT_CALLBACKS Callbacks
    )
{
    RtlZeroMemory(Callbacks, sizeof(UFX_ENDPOINT_CALLBACKS));
    Callbacks->Size = sizeof(UFX_ENDPOINT_CALLBACKS);
}

typedef
_Must_inspect_result_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
UFX_ENDPOINT_CREATE (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE,
    _Inout_ PUFXENDPOINT_INIT,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES,
    _In_ PWDF_IO_QUEUE_CONFIG,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES,
    _In_ PWDF_IO_QUEUE_CONFIG,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES,
    _Out_ UFXENDPOINT*
    );

typedef UFX_ENDPOINT_CREATE *PFN_UFX_ENDPOINT_CREATE;

_Must_inspect_result_
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
FORCEINLINE
UfxEndpointCreate (
    _In_ UFXDEVICE UfxDevice,
    _Inout_ PUFXENDPOINT_INIT EndpointInit,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_ PWDF_IO_QUEUE_CONFIG TransferQueueConfig,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES TransferQueueAttributes,
    _In_ PWDF_IO_QUEUE_CONFIG CommandQueueConfig,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES CommandQueueAttributes,
    _Out_ UFXENDPOINT* UfxEndpoint
    )
{
    return ((PFN_UFX_ENDPOINT_CREATE)
            UfxClassFunctions[UfxEndpointCreateIndex])(UfxGlobals,
                                                       UfxDevice,
                                                       EndpointInit,
                                                       Attributes,
                                                       TransferQueueConfig,
                                                       TransferQueueAttributes,
                                                       CommandQueueConfig,
                                                       CommandQueueAttributes,
                                                       UfxEndpoint);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
VOID
UFX_ENDPOINT_INIT_SET_EVENT_CALLBACKS (
    _In_ PUFX_GLOBALS,
    _Inout_ PUFXENDPOINT_INIT EndpointInit,
    _In_ PUFX_ENDPOINT_CALLBACKS Callbacks
    );

typedef UFX_ENDPOINT_INIT_SET_EVENT_CALLBACKS *PFN_UFX_ENDPOINT_INIT_SET_CALLBACKS;

_IRQL_requires_(PASSIVE_LEVEL)
VOID
FORCEINLINE
UfxEndpointInitSetEventCallbacks (
    _Inout_ PUFXENDPOINT_INIT EndpointInit,
    _In_ PUFX_ENDPOINT_CALLBACKS Callbacks
    )
{
    ((PFN_UFX_ENDPOINT_INIT_SET_CALLBACKS)
     UfxClassFunctions[UfxEndpointInitSetEventCallbacksIndex])(UfxGlobals,
                                                               EndpointInit,
                                                               Callbacks);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_ENDPOINT_NOTIFY_SETUP (
    _In_ PUFX_GLOBALS,
    _In_ UFXENDPOINT UfxEndpoint,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET SetupInfo
    );

typedef UFX_ENDPOINT_NOTIFY_SETUP *PFN_UFX_ENDPOINT_NOTIFY_SETUP;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxEndpointNotifySetup (
    _In_ UFXENDPOINT UfxEndpoint,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET SetupInfo
    )
{
    ((PFN_UFX_ENDPOINT_NOTIFY_SETUP)
     UfxClassFunctions[UfxEndpointNotifySetupIndex])(UfxGlobals,
                                                     UfxEndpoint,
                                                     SetupInfo);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFQUEUE
UFX_ENDPOINT_GET_TRANSFER_QUEUE (
    _In_ PUFX_GLOBALS,
    _In_ UFXENDPOINT
    );

typedef UFX_ENDPOINT_GET_TRANSFER_QUEUE *PFN_UFX_ENDPOINT_GET_TRANSFER_QUEUE;

_IRQL_requires_max_(DISPATCH_LEVEL)
WDFQUEUE
FORCEINLINE
UfxEndpointGetTransferQueue (
    _In_ UFXENDPOINT UfxEndpoint
    )
{
    return  ((PFN_UFX_ENDPOINT_GET_TRANSFER_QUEUE)
             UfxClassFunctions[UfxEndpointGetTransferQueueIndex])(UfxGlobals,
                                                                  UfxEndpoint);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFQUEUE
UFX_ENDPOINT_GET_COMMAND_QUEUE (
    _In_ PUFX_GLOBALS,
    _In_ UFXENDPOINT
    );

typedef UFX_ENDPOINT_GET_COMMAND_QUEUE *PFN_UFX_ENDPOINT_GET_COMMAND_QUEUE;

_IRQL_requires_max_(DISPATCH_LEVEL)
WDFQUEUE
FORCEINLINE
UfxEndpointGetCommandQueue (
    _In_ UFXENDPOINT UfxEndpoint
    )
{
    return  ((PFN_UFX_ENDPOINT_GET_COMMAND_QUEUE)
             UfxClassFunctions[UfxEndpointGetCommandQueueIndex])(UfxGlobals,
                                                                 UfxEndpoint);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
UFX_DEVICE_NOTIFY_FINAL_EXIT (
    _In_ PUFX_GLOBALS,
    _In_ UFXDEVICE
    );

typedef UFX_DEVICE_NOTIFY_FINAL_EXIT *PFN_UFX_DEVICE_NOTIFY_FINAL_EXIT;

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
FORCEINLINE
UfxDeviceNotifyFinalExit (
    _In_ UFXDEVICE UfxDevice
    )
{
    ((PFN_UFX_DEVICE_NOTIFY_FINAL_EXIT)
     UfxClassFunctions[UfxDeviceNotifyFinalExitIndex])(UfxGlobals,
                                                       UfxDevice);
}
