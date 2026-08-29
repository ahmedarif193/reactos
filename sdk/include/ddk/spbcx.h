/*
 * PROJECT:     ReactOS Simple Peripheral Bus class extension
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Public SPBCx version 1 client interface
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#ifndef WDFAPI
#error Include wdf.h before spbcx.h
#endif

#include <spb.h>

#ifdef __cplusplus
struct SPBTARGET__ : WDFFILEOBJECT__ { };
typedef SPBTARGET__ *SPBTARGET;
struct SPBREQUEST__ : WDFREQUEST__ { };
typedef SPBREQUEST__ *SPBREQUEST;
#else
DECLARE_HANDLE(SPBTARGET);
DECLARE_HANDLE(SPBREQUEST);
#endif

WDF_EXTERN_C_START

typedef VOID (*SPBFUNC)(VOID);
extern SPBFUNC SpbFunctions[];
typedef struct _WDF_DRIVER_GLOBALS SPB_DRIVER_GLOBALS, *PSPB_DRIVER_GLOBALS;
typedef struct WDFDEVICE_INIT WDFDEVICE_INIT;
extern PSPB_DRIVER_GLOBALS SpbDriverGlobals;

typedef enum _SPBFUNCENUM
{
    SpbDeviceInitConfigTableIndex = 0,
    SpbDeviceInitializeTableIndex,
    SpbControllerSetIoOtherCallbackTableIndex,
    SpbControllerSetRequestAttributesTableIndex,
    SpbControllerSetTargetAttributesTableIndex,
    SpbTargetGetConnectionParametersTableIndex,
    SpbTargetGetFileObjectTableIndex,
    SpbRequestGetTargetTableIndex,
    SpbRequestGetControllerTableIndex,
    SpbRequestGetParametersTableIndex,
    SpbRequestGetTransferParametersTableIndex,
    SpbRequestCompleteTableIndex,
    SpbRequestCaptureIoOtherTransferListTableIndex,
    SpbFunctionTableNumEntries
} SPBFUNCENUM;

typedef enum _SPB_REQUEST_TYPE
{
    SpbRequestTypeUndefined = 0,
    SpbRequestTypeRead,
    SpbRequestTypeWrite,
    SpbRequestTypeSequence,
    SpbRequestTypeLockController,
    SpbRequestTypeUnlockController,
    SpbRequestTypeLockConnection,
    SpbRequestTypeUnlockConnection,
    SpbRequestTypeOther,
    SpbRequestTypeMax
} SPB_REQUEST_TYPE, *PSPB_REQUEST_TYPE;

typedef enum _SPB_REQUEST_SEQUENCE_POSITION
{
    SpbRequestSequencePositionInvalid = 0,
    SpbRequestSequencePositionSingle,
    SpbRequestSequencePositionFirst,
    SpbRequestSequencePositionContinue,
    SpbRequestSequencePositionLast,
    SpbRequestSequencePositionMax
} SPB_REQUEST_SEQUENCE_POSITION, *PSPB_REQUEST_SEQUENCE_POSITION;

typedef NTSTATUS EVT_SPB_TARGET_CONNECT(WDFDEVICE, SPBTARGET);
typedef EVT_SPB_TARGET_CONNECT *PFN_SPB_TARGET_CONNECT;
typedef VOID EVT_SPB_TARGET_DISCONNECT(WDFDEVICE, SPBTARGET);
typedef EVT_SPB_TARGET_DISCONNECT *PFN_SPB_TARGET_DISCONNECT;
typedef VOID EVT_SPB_CONTROLLER_LOCK(WDFDEVICE, SPBTARGET, SPBREQUEST);
typedef EVT_SPB_CONTROLLER_LOCK *PFN_SPB_CONTROLLER_LOCK;
typedef VOID EVT_SPB_CONTROLLER_UNLOCK(WDFDEVICE, SPBTARGET, SPBREQUEST);
typedef EVT_SPB_CONTROLLER_UNLOCK *PFN_SPB_CONTROLLER_UNLOCK;
typedef VOID EVT_SPB_CONTROLLER_READ(WDFDEVICE, SPBTARGET, SPBREQUEST, size_t);
typedef EVT_SPB_CONTROLLER_READ *PFN_SPB_CONTROLLER_READ;
typedef VOID EVT_SPB_CONTROLLER_WRITE(WDFDEVICE, SPBTARGET, SPBREQUEST, size_t);
typedef EVT_SPB_CONTROLLER_WRITE *PFN_SPB_CONTROLLER_WRITE;
typedef VOID EVT_SPB_CONTROLLER_SEQUENCE(WDFDEVICE, SPBTARGET, SPBREQUEST, ULONG);
typedef EVT_SPB_CONTROLLER_SEQUENCE *PFN_SPB_CONTROLLER_SEQUENCE;
typedef VOID EVT_SPB_CONTROLLER_OTHER(WDFDEVICE, SPBTARGET, SPBREQUEST, size_t, size_t, ULONG);
typedef EVT_SPB_CONTROLLER_OTHER *PFN_SPB_CONTROLLER_OTHER;

typedef struct _SPB_CONTROLLER_CONFIG
{
    ULONG Size;
    WDF_IO_QUEUE_DISPATCH_TYPE ControllerDispatchType;
    WDF_TRI_STATE PowerManaged;
    PFN_SPB_TARGET_CONNECT EvtSpbTargetConnect;
    PFN_SPB_TARGET_DISCONNECT EvtSpbTargetDisconnect;
    PFN_SPB_CONTROLLER_LOCK EvtSpbControllerLock;
    PFN_SPB_CONTROLLER_UNLOCK EvtSpbControllerUnlock;
    PFN_SPB_CONTROLLER_READ EvtSpbIoRead;
    PFN_SPB_CONTROLLER_WRITE EvtSpbIoWrite;
    PFN_SPB_CONTROLLER_SEQUENCE EvtSpbIoSequence;
} SPB_CONTROLLER_CONFIG, *PSPB_CONTROLLER_CONFIG;

FORCEINLINE VOID SPB_CONTROLLER_CONFIG_INIT(PSPB_CONTROLLER_CONFIG Config)
{
    RtlZeroMemory(Config, sizeof(*Config));
    Config->Size = sizeof(*Config);
    Config->ControllerDispatchType = WdfIoQueueDispatchSequential;
    Config->PowerManaged = WdfUseDefault;
}

typedef struct _SPB_CONNECTION_PARAMETERS
{
    USHORT Size;
    PCWSTR ConnectionTag;
    PVOID ConnectionParameters;
} SPB_CONNECTION_PARAMETERS, *PSPB_CONNECTION_PARAMETERS;

FORCEINLINE VOID SPB_CONNECTION_PARAMETERS_INIT(PSPB_CONNECTION_PARAMETERS Parameters)
{
    RtlZeroMemory(Parameters, sizeof(*Parameters));
    Parameters->Size = sizeof(*Parameters);
}

typedef struct _SPB_REQUEST_PARAMETERS
{
    USHORT Size;
    SPB_REQUEST_TYPE Type;
    SPB_REQUEST_SEQUENCE_POSITION Position;
    SPB_TRANSFER_DIRECTION PreviousTransferDirection;
    size_t Length;
    ULONG SequenceTransferCount;
} SPB_REQUEST_PARAMETERS, *PSPB_REQUEST_PARAMETERS;

FORCEINLINE VOID SPB_REQUEST_PARAMETERS_INIT(PSPB_REQUEST_PARAMETERS Parameters)
{
    RtlZeroMemory(Parameters, sizeof(*Parameters));
    Parameters->Size = sizeof(*Parameters);
}

typedef struct SPB_TRANSFER_DESCRIPTOR
{
    USHORT Size;
    SPB_TRANSFER_DIRECTION Direction;
    size_t TransferLength;
    ULONG DelayInUs;
} SPB_TRANSFER_DESCRIPTOR, *PSPB_TRANSFER_DESCRIPTOR;

FORCEINLINE VOID SPB_TRANSFER_DESCRIPTOR_INIT(PSPB_TRANSFER_DESCRIPTOR Descriptor)
{
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Size = sizeof(*Descriptor);
}

typedef NTSTATUS (WDFAPI *PFN_SPBDEVICEINITCONFIG)(PSPB_DRIVER_GLOBALS, WDFDEVICE_INIT *);
typedef NTSTATUS (WDFAPI *PFN_SPBDEVICEINITIALIZE)(PSPB_DRIVER_GLOBALS, WDFDEVICE, PSPB_CONTROLLER_CONFIG);
typedef VOID (WDFAPI *PFN_SPBCONTROLLERSETIOOTHERCALLBACK)(PSPB_DRIVER_GLOBALS, WDFDEVICE,
                                                           PFN_SPB_CONTROLLER_OTHER, PFN_WDF_IO_IN_CALLER_CONTEXT);
typedef VOID (WDFAPI *PFN_SPBCONTROLLERSETREQUESTATTRIBUTES)(PSPB_DRIVER_GLOBALS, WDFDEVICE, PWDF_OBJECT_ATTRIBUTES);
typedef VOID (WDFAPI *PFN_SPBCONTROLLERSETTARGETATTRIBUTES)(PSPB_DRIVER_GLOBALS, WDFDEVICE, PWDF_OBJECT_ATTRIBUTES);
typedef VOID (WDFAPI *PFN_SPBTARGETGETCONNECTIONPARAMETERS)(PSPB_DRIVER_GLOBALS, SPBTARGET, PSPB_CONNECTION_PARAMETERS);
typedef WDFFILEOBJECT (WDFAPI *PFN_SPBTARGETGETFILEOBJECT)(PSPB_DRIVER_GLOBALS, SPBTARGET);
typedef SPBTARGET (WDFAPI *PFN_SPBREQUESTGETTARGET)(PSPB_DRIVER_GLOBALS, SPBREQUEST);
typedef WDFDEVICE (WDFAPI *PFN_SPBREQUESTGETCONTROLLER)(PSPB_DRIVER_GLOBALS, SPBREQUEST);
typedef VOID (WDFAPI *PFN_SPBREQUESTGETPARAMETERS)(PSPB_DRIVER_GLOBALS, SPBREQUEST, PSPB_REQUEST_PARAMETERS);
typedef VOID (WDFAPI *PFN_SPBREQUESTGETTRANSFERPARAMETERS)(PSPB_DRIVER_GLOBALS, SPBREQUEST, ULONG,
                                                           PSPB_TRANSFER_DESCRIPTOR, PMDL *);
typedef VOID (WDFAPI *PFN_SPBREQUESTCOMPLETE)(PSPB_DRIVER_GLOBALS, SPBREQUEST, NTSTATUS);
typedef NTSTATUS (WDFAPI *PFN_SPBREQUESTCAPTUREIOOTHERTRANSFERLIST)(PSPB_DRIVER_GLOBALS, SPBREQUEST);

FORCEINLINE NTSTATUS SpbDeviceInitConfig(WDFDEVICE_INIT *DeviceInit)
{
    return ((PFN_SPBDEVICEINITCONFIG)SpbFunctions[SpbDeviceInitConfigTableIndex])(SpbDriverGlobals, DeviceInit);
}
FORCEINLINE NTSTATUS SpbDeviceInitialize(WDFDEVICE Device, PSPB_CONTROLLER_CONFIG Config)
{
    return ((PFN_SPBDEVICEINITIALIZE)SpbFunctions[SpbDeviceInitializeTableIndex])(SpbDriverGlobals, Device, Config);
}
FORCEINLINE VOID SpbControllerSetIoOtherCallback(WDFDEVICE Device, PFN_SPB_CONTROLLER_OTHER Callback,
                                                 PFN_WDF_IO_IN_CALLER_CONTEXT CallerContext)
{
    ((PFN_SPBCONTROLLERSETIOOTHERCALLBACK)SpbFunctions[SpbControllerSetIoOtherCallbackTableIndex])(
        SpbDriverGlobals, Device, Callback, CallerContext);
}
FORCEINLINE VOID SpbControllerSetRequestAttributes(WDFDEVICE Device, PWDF_OBJECT_ATTRIBUTES Attributes)
{
    ((PFN_SPBCONTROLLERSETREQUESTATTRIBUTES)SpbFunctions[SpbControllerSetRequestAttributesTableIndex])(
        SpbDriverGlobals, Device, Attributes);
}
FORCEINLINE VOID SpbControllerSetTargetAttributes(WDFDEVICE Device, PWDF_OBJECT_ATTRIBUTES Attributes)
{
    ((PFN_SPBCONTROLLERSETTARGETATTRIBUTES)SpbFunctions[SpbControllerSetTargetAttributesTableIndex])(
        SpbDriverGlobals, Device, Attributes);
}
FORCEINLINE VOID SpbTargetGetConnectionParameters(SPBTARGET Target, PSPB_CONNECTION_PARAMETERS Parameters)
{
    ((PFN_SPBTARGETGETCONNECTIONPARAMETERS)SpbFunctions[SpbTargetGetConnectionParametersTableIndex])(
        SpbDriverGlobals, Target, Parameters);
}
FORCEINLINE WDFFILEOBJECT SpbTargetGetFileObject(SPBTARGET Target)
{
    return ((PFN_SPBTARGETGETFILEOBJECT)SpbFunctions[SpbTargetGetFileObjectTableIndex])(SpbDriverGlobals, Target);
}
FORCEINLINE SPBTARGET SpbRequestGetTarget(SPBREQUEST Request)
{
    return ((PFN_SPBREQUESTGETTARGET)SpbFunctions[SpbRequestGetTargetTableIndex])(SpbDriverGlobals, Request);
}
FORCEINLINE WDFDEVICE SpbRequestGetController(SPBREQUEST Request)
{
    return ((PFN_SPBREQUESTGETCONTROLLER)SpbFunctions[SpbRequestGetControllerTableIndex])(SpbDriverGlobals, Request);
}
FORCEINLINE VOID SpbRequestGetParameters(SPBREQUEST Request, PSPB_REQUEST_PARAMETERS Parameters)
{
    ((PFN_SPBREQUESTGETPARAMETERS)SpbFunctions[SpbRequestGetParametersTableIndex])(SpbDriverGlobals, Request, Parameters);
}
FORCEINLINE VOID SpbRequestGetTransferParameters(SPBREQUEST Request, ULONG Index,
                                                  PSPB_TRANSFER_DESCRIPTOR Descriptor, PMDL *Mdl)
{
    ((PFN_SPBREQUESTGETTRANSFERPARAMETERS)SpbFunctions[SpbRequestGetTransferParametersTableIndex])(
        SpbDriverGlobals, Request, Index, Descriptor, Mdl);
}
FORCEINLINE VOID SpbRequestComplete(SPBREQUEST Request, NTSTATUS Status)
{
    ((PFN_SPBREQUESTCOMPLETE)SpbFunctions[SpbRequestCompleteTableIndex])(SpbDriverGlobals, Request, Status);
}
FORCEINLINE NTSTATUS SpbRequestCaptureIoOtherTransferList(SPBREQUEST Request)
{
    return ((PFN_SPBREQUESTCAPTUREIOOTHERTRANSFERLIST)SpbFunctions[SpbRequestCaptureIoOtherTransferListTableIndex])(
        SpbDriverGlobals, Request);
}

WDF_EXTERN_C_END
