/*++

Copyright (c) Microsoft Corporation

Module Name:

    FxCxDeviceInit.cpp

Abstract:
    Internals for WDFCXDEVICE_INIT

Author:



Environment:

    Both kernel and user mode

Revision History:



--*/

#include "coreprivshared.hpp"

extern "C" {
// #include "FxCxDeviceInit.tmh"
}

WDFCXDEVICE_INIT::WDFCXDEVICE_INIT()
{
    InitializeListHead(&ListEntry);

    ClientDriverGlobals = NULL;
    CxDriverGlobals = NULL;
    PreprocessInfo = NULL;
    IoInCallerContextCallback = NULL;
    RtlZeroMemory(&RequestAttributes, sizeof(RequestAttributes));
    RtlZeroMemory(&FileObject, sizeof(FileObject));
    FileObject.AutoForwardCleanupClose = WdfUseDefault;
    RtlZeroMemory(&PnpPowerCallbacks, sizeof(PnpPowerCallbacks));
    CxDeviceInfo = NULL;
}

WDFCXDEVICE_INIT::~WDFCXDEVICE_INIT()
{
    ASSERT(IsListEmpty(&ListEntry));

    if (PreprocessInfo != NULL) {
        delete PreprocessInfo;
    }
}

_Must_inspect_result_
PWDFCXDEVICE_INIT
WDFCXDEVICE_INIT::_AllocateCxDeviceInit(
    __in PWDFDEVICE_INIT DeviceInit
    )
{
    PFX_DRIVER_GLOBALS  fxDriverGlobals;
    PWDFCXDEVICE_INIT   init;

    fxDriverGlobals = DeviceInit->DriverGlobals;

    init = new(fxDriverGlobals) WDFCXDEVICE_INIT();
    if (init == NULL) {
        DoTraceLevelMessage(fxDriverGlobals, TRACE_LEVEL_ERROR, TRACINGDEVICE,
                        "WDFDRIVER 0x%p  couldn't allocate WDFCXDEVICE_INIT",
                        DeviceInit->Driver);
        return NULL;
    }

    DeviceInit->AddCxDeviceInit(init);

    return init;
}

static
FxDevice*
FxCxGetDeviceFromHandle(
    __in WDFDEVICE Device
    )
{
    FxDevice* device;

    FxObjectHandleGetPtr(NULL,
                         Device,
                         FX_TYPE_DEVICE,
                         reinterpret_cast<PVOID*>(&device));
    return device;
}

static
FxCxDeviceInfo*
FxCxGetDeviceInfoFromEntry(
    __in PLIST_ENTRY Entry
    )
{
    return CONTAINING_RECORD(Entry, FxCxDeviceInfo, ListEntry);
}

static
VOID
FxCxSaveFirstError(
    __inout NTSTATUS* FinalStatus,
    __in NTSTATUS Status
    )
{
    if (NT_SUCCESS(*FinalStatus) && !NT_SUCCESS(Status)) {
        *FinalStatus = Status;
    }
}

static
VOID
FxCxCleanupPrepareHardware(
    __in FxDevice* FxDeviceObject,
    __in PLIST_ENTRY StopEntry,
    __in WDFDEVICE Device,
    __in WDFCMRESLIST ResourcesRaw,
    __in WDFCMRESLIST ResourcesTranslated
    )
{
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;

    for (entry = FxDeviceObject->m_CxDeviceInfoListHead.Flink;
         entry != StopEntry;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePrePrepareHardware != NULL &&
            callbacks->EvtCxDevicePrePrepareHardwareFailedCleanup != NULL) {
            callbacks->EvtCxDevicePrePrepareHardwareFailedCleanup(
                Device,
                ResourcesRaw,
                ResourcesTranslated);
        }
    }
}

NTSTATUS
FxCxInvokeDevicePrepareHardware(
    __in WDFDEVICE Device,
    __in WDFCMRESLIST ResourcesRaw,
    __in WDFCMRESLIST ResourcesTranslated,
    __in_opt PFN_WDF_DEVICE_PREPARE_HARDWARE ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePrePrepareHardware != NULL) {
            status = callbacks->EvtCxDevicePrePrepareHardware(
                Device,
                ResourcesRaw,
                ResourcesTranslated);
            if (!NT_SUCCESS(status)) {
                FxCxCleanupPrepareHardware(deviceObject,
                                           entry,
                                           Device,
                                           ResourcesRaw,
                                           ResourcesTranslated);
                return status;
            }
        }
    }

    status = ClientCallback != NULL
        ? ClientCallback(Device, ResourcesRaw, ResourcesTranslated)
        : STATUS_SUCCESS;
    if (!NT_SUCCESS(status)) {
        return status;
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostPrepareHardware != NULL) {
            callbackStatus = callbacks->EvtCxDevicePostPrepareHardware(
                Device,
                ResourcesRaw,
                ResourcesTranslated);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

NTSTATUS
FxCxInvokeDeviceReleaseHardware(
    __in WDFDEVICE Device,
    __in WDFCMRESLIST ResourcesTranslated,
    __in_opt PFN_WDF_DEVICE_RELEASE_HARDWARE ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);
    status = STATUS_SUCCESS;

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreReleaseHardware != NULL) {
            callbackStatus = callbacks->EvtCxDevicePreReleaseHardware(
                Device,
                ResourcesTranslated);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    if (ClientCallback != NULL) {
        callbackStatus = ClientCallback(Device, ResourcesTranslated);
        FxCxSaveFirstError(&status, callbackStatus);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostReleaseHardware != NULL) {
            callbackStatus = callbacks->EvtCxDevicePostReleaseHardware(
                Device,
                ResourcesTranslated);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

static
VOID
FxCxCleanupD0Entry(
    __in FxDevice* FxDeviceObject,
    __in PLIST_ENTRY StopEntry,
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;

    for (entry = FxDeviceObject->m_CxDeviceInfoListHead.Flink;
         entry != StopEntry;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreD0Entry != NULL &&
            callbacks->EvtCxDevicePreD0EntryFailedCleanup != NULL) {
            callbacks->EvtCxDevicePreD0EntryFailedCleanup(Device,
                                                          PreviousState);
        }
    }
}

NTSTATUS
FxCxInvokeDeviceD0Entry(
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE PreviousState,
    __in_opt PFN_WDF_DEVICE_D0_ENTRY ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreD0Entry != NULL) {
            status = callbacks->EvtCxDevicePreD0Entry(Device, PreviousState);
            if (!NT_SUCCESS(status)) {
                FxCxCleanupD0Entry(deviceObject,
                                   entry,
                                   Device,
                                   PreviousState);
                return status;
            }
        }
    }

    status = ClientCallback != NULL
        ? ClientCallback(Device, PreviousState)
        : STATUS_SUCCESS;
    if (!NT_SUCCESS(status)) {
        FxCxCleanupD0Entry(deviceObject,
                           &deviceObject->m_CxDeviceInfoListHead,
                           Device,
                           PreviousState);
        return status;
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostD0Entry != NULL) {
            callbackStatus = callbacks->EvtCxDevicePostD0Entry(
                Device,
                PreviousState);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

NTSTATUS
FxCxInvokeDeviceD0Exit(
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE TargetState,
    __in_opt PFN_WDF_DEVICE_D0_EXIT ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);
    status = STATUS_SUCCESS;

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreD0Exit != NULL) {
            callbackStatus = callbacks->EvtCxDevicePreD0Exit(
                Device,
                TargetState);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    if (ClientCallback != NULL) {
        callbackStatus = ClientCallback(Device, TargetState);
        FxCxSaveFirstError(&status, callbackStatus);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostD0Exit != NULL) {
            callbackStatus = callbacks->EvtCxDevicePostD0Exit(
                Device,
                TargetState);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

static
VOID
FxCxCleanupSelfManagedIoInit(
    __in FxDevice* FxDeviceObject,
    __in PLIST_ENTRY StopEntry,
    __in WDFDEVICE Device
    )
{
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;

    for (entry = FxDeviceObject->m_CxDeviceInfoListHead.Flink;
         entry != StopEntry;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreSelfManagedIoInit != NULL &&
            callbacks->EvtCxDevicePreSelfManagedIoInitFailedCleanup != NULL) {
            callbacks->EvtCxDevicePreSelfManagedIoInitFailedCleanup(Device);
        }
    }
}

NTSTATUS
FxCxInvokeDeviceSelfManagedIoInit(
    __in WDFDEVICE Device,
    __in_opt PFN_WDF_DEVICE_SELF_MANAGED_IO_INIT ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreSelfManagedIoInit != NULL) {
            status = callbacks->EvtCxDevicePreSelfManagedIoInit(Device);
            if (!NT_SUCCESS(status)) {
                FxCxCleanupSelfManagedIoInit(deviceObject, entry, Device);
                return status;
            }
        }
    }

    status = ClientCallback != NULL
        ? ClientCallback(Device)
        : STATUS_SUCCESS;
    if (!NT_SUCCESS(status)) {
        FxCxCleanupSelfManagedIoInit(
            deviceObject,
            &deviceObject->m_CxDeviceInfoListHead,
            Device);
        return status;
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostSelfManagedIoInit != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePostSelfManagedIoInit(Device);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

NTSTATUS
FxCxInvokeDeviceSelfManagedIoSuspend(
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE TargetState,
    __in_opt PFN_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);
    status = STATUS_SUCCESS;

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreSelfManagedIoSuspendEx != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePreSelfManagedIoSuspendEx(Device,
                                                                TargetState);
            FxCxSaveFirstError(&status, callbackStatus);
        } else if (callbacks->EvtCxDevicePreSelfManagedIoSuspend != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePreSelfManagedIoSuspend(Device);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    if (ClientCallback != NULL) {
        callbackStatus = ClientCallback(Device);
        FxCxSaveFirstError(&status, callbackStatus);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostSelfManagedIoSuspendEx != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePostSelfManagedIoSuspendEx(Device,
                                                                 TargetState);
            FxCxSaveFirstError(&status, callbackStatus);
        } else if (callbacks->EvtCxDevicePostSelfManagedIoSuspend != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePostSelfManagedIoSuspend(Device);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

static
VOID
FxCxCleanupSelfManagedIoRestart(
    __in FxDevice* FxDeviceObject,
    __in PLIST_ENTRY StopEntry,
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;

    for (entry = FxDeviceObject->m_CxDeviceInfoListHead.Flink;
         entry != StopEntry;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreSelfManagedIoRestartEx != NULL &&
            callbacks->EvtCxDevicePreSelfManagedIoRestartExFailedCleanup !=
                NULL) {
            callbacks->EvtCxDevicePreSelfManagedIoRestartExFailedCleanup(
                Device,
                PreviousState);
        } else if (callbacks->EvtCxDevicePreSelfManagedIoRestart != NULL &&
                   callbacks->
                       EvtCxDevicePreSelfManagedIoRestartFailedCleanup !=
                           NULL) {
            callbacks->EvtCxDevicePreSelfManagedIoRestartFailedCleanup(Device);
        }
    }
}

NTSTATUS
FxCxInvokeDeviceSelfManagedIoRestart(
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE PreviousState,
    __in_opt PFN_WDF_DEVICE_SELF_MANAGED_IO_RESTART ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreSelfManagedIoRestartEx != NULL) {
            status = callbacks->EvtCxDevicePreSelfManagedIoRestartEx(
                Device,
                PreviousState);
        } else if (callbacks->EvtCxDevicePreSelfManagedIoRestart != NULL) {
            status = callbacks->EvtCxDevicePreSelfManagedIoRestart(Device);
        } else {
            status = STATUS_SUCCESS;
        }

        if (!NT_SUCCESS(status)) {
            FxCxCleanupSelfManagedIoRestart(deviceObject,
                                            entry,
                                            Device,
                                            PreviousState);
            return status;
        }
    }

    status = ClientCallback != NULL
        ? ClientCallback(Device)
        : STATUS_SUCCESS;
    if (!NT_SUCCESS(status)) {
        FxCxCleanupSelfManagedIoRestart(
            deviceObject,
            &deviceObject->m_CxDeviceInfoListHead,
            Device,
            PreviousState);
        return status;
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostSelfManagedIoRestartEx != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePostSelfManagedIoRestartEx(
                    Device,
                    PreviousState);
            FxCxSaveFirstError(&status, callbackStatus);
        } else if (callbacks->EvtCxDevicePostSelfManagedIoRestart != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePostSelfManagedIoRestart(Device);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

VOID
FxCxInvokeDeviceSurpriseRemoval(
    __in WDFDEVICE Device,
    __in_opt PFN_WDF_DEVICE_SURPRISE_REMOVAL ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreSurpriseRemoval != NULL) {
            callbacks->EvtCxDevicePreSurpriseRemoval(Device);
        }
    }

    if (ClientCallback != NULL) {
        ClientCallback(Device);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostSurpriseRemoval != NULL) {
            callbacks->EvtCxDevicePostSurpriseRemoval(Device);
        }
    }
}

static
VOID
FxCxCleanupD0EntryPostHardwareEnabled(
    __in FxDevice* FxDeviceObject,
    __in PLIST_ENTRY StopEntry,
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;

    for (entry = FxDeviceObject->m_CxDeviceInfoListHead.Flink;
         entry != StopEntry;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreD0EntryPostHardwareEnabled != NULL &&
            callbacks->EvtCxDevicePreD0EntryPostHardwareEnabledFailedCleanup !=
                NULL) {
            callbacks->EvtCxDevicePreD0EntryPostHardwareEnabledFailedCleanup(
                Device,
                PreviousState);
        }
    }
}

NTSTATUS
FxCxInvokeDeviceD0EntryPostHardwareEnabled(
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE PreviousState
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreD0EntryPostHardwareEnabled != NULL) {
            status = callbacks->EvtCxDevicePreD0EntryPostHardwareEnabled(
                Device,
                PreviousState);
            if (!NT_SUCCESS(status)) {
                FxCxCleanupD0EntryPostHardwareEnabled(deviceObject,
                                                      entry,
                                                      Device,
                                                      PreviousState);
                return status;
            }
        }
    }

    status = STATUS_SUCCESS;
    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostD0EntryPostHardwareEnabled != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePostD0EntryPostHardwareEnabled(
                    Device,
                    PreviousState);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

NTSTATUS
FxCxInvokeDeviceD0ExitPreHardwareDisabled(
    __in WDFDEVICE Device,
    __in WDF_POWER_DEVICE_STATE TargetState
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_PNPPOWER_EVENT_CALLBACKS callbacks;
    NTSTATUS status;
    NTSTATUS callbackStatus;

    deviceObject = FxCxGetDeviceFromHandle(Device);
    status = STATUS_SUCCESS;

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePreD0ExitPreHardwareDisabled != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePreD0ExitPreHardwareDisabled(
                    Device,
                    TargetState);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks = &FxCxGetDeviceInfoFromEntry(entry)->PnpPowerCallbacks;
        if (callbacks->EvtCxDevicePostD0ExitPreHardwareDisabled != NULL) {
            callbackStatus =
                callbacks->EvtCxDevicePostD0ExitPreHardwareDisabled(
                    Device,
                    TargetState);
            FxCxSaveFirstError(&status, callbackStatus);
        }
    }

    return status;
}

VOID
FxCxInvokeDeviceDisarmWakeFromS0(
    __in WDFDEVICE Device,
    __in_opt PFN_WDF_DEVICE_DISARM_WAKE_FROM_S0 ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_POWER_POLICY_EVENT_CALLBACKS callbacks;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePreDisarmWakeFromS0 != NULL) {
            callbacks->EvtCxDevicePreDisarmWakeFromS0(Device);
        }
    }

    if (ClientCallback != NULL) {
        ClientCallback(Device);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePostDisarmWakeFromS0 != NULL) {
            callbacks->EvtCxDevicePostDisarmWakeFromS0(Device);
        }
    }
}

VOID
FxCxInvokeDeviceDisarmWakeFromSx(
    __in WDFDEVICE Device,
    __in_opt PFN_WDF_DEVICE_DISARM_WAKE_FROM_SX ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_POWER_POLICY_EVENT_CALLBACKS callbacks;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePreDisarmWakeFromSx != NULL) {
            callbacks->EvtCxDevicePreDisarmWakeFromSx(Device);
        }
    }

    if (ClientCallback != NULL) {
        ClientCallback(Device);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePostDisarmWakeFromSx != NULL) {
            callbacks->EvtCxDevicePostDisarmWakeFromSx(Device);
        }
    }
}

VOID
FxCxInvokeDeviceWakeFromS0Triggered(
    __in WDFDEVICE Device,
    __in_opt PFN_WDF_DEVICE_WAKE_FROM_S0_TRIGGERED ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_POWER_POLICY_EVENT_CALLBACKS callbacks;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePreWakeFromS0Triggered != NULL) {
            callbacks->EvtCxDevicePreWakeFromS0Triggered(Device);
        }
    }

    if (ClientCallback != NULL) {
        ClientCallback(Device);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePostWakeFromS0Triggered != NULL) {
            callbacks->EvtCxDevicePostWakeFromS0Triggered(Device);
        }
    }
}

VOID
FxCxInvokeDeviceWakeFromSxTriggered(
    __in WDFDEVICE Device,
    __in_opt PFN_WDF_DEVICE_WAKE_FROM_SX_TRIGGERED ClientCallback
    )
{
    FxDevice* deviceObject;
    PLIST_ENTRY entry;
    PWDFCX_POWER_POLICY_EVENT_CALLBACKS callbacks;

    deviceObject = FxCxGetDeviceFromHandle(Device);

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePreWakeFromSxTriggered != NULL) {
            callbacks->EvtCxDevicePreWakeFromSxTriggered(Device);
        }
    }

    if (ClientCallback != NULL) {
        ClientCallback(Device);
    }

    for (entry = deviceObject->m_CxDeviceInfoListHead.Flink;
         entry != &deviceObject->m_CxDeviceInfoListHead;
         entry = entry->Flink) {
        callbacks =
            &FxCxGetDeviceInfoFromEntry(entry)->PowerPolicyCallbacks;
        if (callbacks->EvtCxDevicePostWakeFromSxTriggered != NULL) {
            callbacks->EvtCxDevicePostWakeFromSxTriggered(Device);
        }
    }
}
