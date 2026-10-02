/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UCSI KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "classlibrary.h"
#include <UcmucsiCx.h>

#define UCSI_TAG 'iscU'
#define UCSI_MAX_CONNECTORS 127
#define UCSI_COMMAND_TIMEOUT_MS 1000
#define UCSI_NOTIFICATIONS 0xDA65
#define UCSI_RESET_ATTEMPTS 3

typedef struct _UCSI_COLLECTION_CONTEXT
{
    ULONG Count;
    ULONGLONG Ids[UCSI_MAX_CONNECTORS];
} UCSI_COLLECTION_CONTEXT, *PUCSI_COLLECTION_CONTEXT;

typedef struct _UCSI_PPM_CONTEXT
{
    WDFDEVICE Device;
    UCMUCSIPPM Handle;
    ULONG ConnectorCount;
    BOOLEAN UsbDeviceControllerEnabled;
    WDFQUEUE Queue;
    KSPIN_LOCK Lock;
    UCSI_DATA_BLOCK Block;
    BOOLEAN Started;
    BOOLEAN Stopping;
    PKTHREAD Thread;
    KEVENT Wake;
    KEVENT Notified;
    UCSI_GET_CAPABILITY_IN Capability;
} UCSI_PPM_CONTEXT, *PUCSI_PPM_CONTEXT;

typedef struct _UCSI_DEVICE_CONTEXT
{
    UCMUCSIPPM Ppm;
} UCSI_DEVICE_CONTEXT, *PUCSI_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UCSI_COLLECTION_CONTEXT, UcsiGetCollectionContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UCSI_PPM_CONTEXT, UcsiGetPpmContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UCSI_DEVICE_CONTEXT, UcsiGetDeviceContext)

static
NTSTATUS
NTAPI
UcsiEvtWdmIrpDispatch(
    _In_ WDFDEVICE Device,
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ ULONG Code,
    _In_ WDFCONTEXT DriverContext,
    _Inout_ PIRP Irp,
    _In_ WDFCONTEXT DispatchContext)
{
    PUCSI_DEVICE_CONTEXT DeviceContext = UcsiGetDeviceContext(Device);
    PUCSI_PPM_CONTEXT Ppm;

    UNREFERENCED_PARAMETER(MajorFunction);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(DriverContext);

    if ((Code == IOCTL_UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK || Code == IOCTL_UCMUCSI_PPM_GET_UCSI_DATA_BLOCK) &&
        Irp->RequestorMode == KernelMode &&
        DeviceContext != NULL &&
        DeviceContext->Ppm != NULL)
    {
        Ppm = UcsiGetPpmContext(DeviceContext->Ppm);
        if (Ppm->Queue != NULL)
            return WdfDeviceWdmDispatchIrpToIoQueue(Device, Irp, Ppm->Queue, WDF_DISPATCH_IRP_TO_IO_QUEUE_NO_FLAGS);
    }

    return WdfDeviceWdmDispatchIrp(Device, Irp, DispatchContext);
}

static
NTSTATUS
UcsiReadBlock(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _Out_ PUCSI_DATA_BLOCK Block)
{
    UCMUCSI_PPM_GET_UCSI_DATA_BLOCK_OUT_PARAMS Output;
    UCMUCSI_PPM_GET_UCSI_DATA_BLOCK_IN_PARAMS Input;
    NTSTATUS Status;

    Input.PpmObject = Ppm->Handle;
    RtlZeroMemory(&Output, sizeof(Output));
    Status = WdfCxSendIoctlToDevice(Ppm->Device, IOCTL_UCMUCSI_PPM_GET_UCSI_DATA_BLOCK, &Input, sizeof(Input), &Output, sizeof(Output));
    *Block = Output.UcmUcsiDataBlock;
    return Status;
}

static
NTSTATUS
UcsiWriteBlock(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _In_ ULONG64 Control)
{
    UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK_IN_PARAMS Input;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.PpmObject = Ppm->Handle;
    Input.UcmUcsiDataBlock.Control.AsUInt64 = Control;
    return WdfCxSendIoctlToDevice(Ppm->Device, IOCTL_UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK, &Input, sizeof(Input), NULL, 0);
}

static
NTSTATUS
NTAPI
UcsiDeviceInitInitialize(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(DriverGlobals);

    if (DeviceInit == NULL)
        return STATUS_INVALID_PARAMETER;

    return WdfCxDeviceInitAllocate(WdfDriverGlobals, DeviceInit) != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
NTAPI
UcsiDeviceInitialize(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE WdfDevice,
    _In_ PUCMUCSI_DEVICE_CONFIG Config)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (WdfDevice == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, UCSI_DEVICE_CONTEXT);
    Status = WdfObjectAllocateContext(WdfDevice, &Attributes, NULL);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    return WdfDeviceConfigureWdmIrpDispatchCallback(WdfDevice,
                                                    WdfGetDriver(),
                                                    IRP_MJ_DEVICE_CONTROL,
                                                    UcsiEvtWdmIrpDispatch,
                                                    NULL);
}

static
NTSTATUS
NTAPI
UcsiConnectorCollectionCreate(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE WdfDevice,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _Out_ UCMUCSI_CONNECTOR_COLLECTION *ConnectorCollection)
{
    WDF_OBJECT_ATTRIBUTES ObjectAttributes, ContextAttributes;
    WDFOBJECT Object;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (WdfDevice == NULL || ConnectorCollection == NULL)
        return STATUS_INVALID_PARAMETER;

    *ConnectorCollection = NULL;
    if (Attributes != NULL)
        ObjectAttributes = *Attributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);
    if (ObjectAttributes.ParentObject == NULL)
        ObjectAttributes.ParentObject = WdfDevice;

    Status = WdfObjectCreate(&ObjectAttributes, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&ContextAttributes, UCSI_COLLECTION_CONTEXT);
    Status = WdfObjectAllocateContext(Object, &ContextAttributes, NULL);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Object);
        return Status;
    }

    *ConnectorCollection = (UCMUCSI_CONNECTOR_COLLECTION)Object;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UcsiConnectorCollectionAddConnector(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMUCSI_CONNECTOR_COLLECTION ConnectorCollection,
    _In_ PUCMUCSI_CONNECTOR_INFO ConnectorInfo)
{
    PUCSI_COLLECTION_CONTEXT Context;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (ConnectorCollection == NULL || ConnectorInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = UcsiGetCollectionContext(ConnectorCollection);
    if (Context->Count == UCSI_MAX_CONNECTORS)
        return STATUS_INSUFFICIENT_RESOURCES;

    Context->Ids[Context->Count++] = ConnectorInfo->ConnectorId;
    return STATUS_SUCCESS;
}

static
VOID
UcsiStopEngine(
    _In_ PUCSI_PPM_CONTEXT Ppm);

static
VOID
NTAPI
UcsiEvtPpmCleanup(
    _In_ WDFOBJECT Object)
{
    PUCSI_PPM_CONTEXT Ppm = UcsiGetPpmContext(Object);
    PUCSI_DEVICE_CONTEXT DeviceContext;

    UcsiStopEngine(Ppm);
    DeviceContext = UcsiGetDeviceContext(Ppm->Device);
    if (DeviceContext != NULL && DeviceContext->Ppm == Ppm->Handle)
        DeviceContext->Ppm = NULL;
}

static
NTSTATUS
NTAPI
UcsiPpmCreate(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE WdfDevice,
    _In_ PUCMUCSI_PPM_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _Out_ UCMUCSIPPM *PpmObject)
{
    WDF_OBJECT_ATTRIBUTES ObjectAttributes, ContextAttributes;
    PUCSI_DEVICE_CONTEXT DeviceContext;
    PUCSI_PPM_CONTEXT Ppm;
    WDFOBJECT Object;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (WdfDevice == NULL || Config == NULL || PpmObject == NULL)
        return STATUS_INVALID_PARAMETER;

    *PpmObject = NULL;
    DeviceContext = UcsiGetDeviceContext(WdfDevice);
    if (DeviceContext == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    if (Attributes != NULL)
        ObjectAttributes = *Attributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);
    if (ObjectAttributes.ParentObject == NULL)
        ObjectAttributes.ParentObject = WdfDevice;

    Status = WdfObjectCreate(&ObjectAttributes, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&ContextAttributes, UCSI_PPM_CONTEXT);
    ContextAttributes.EvtCleanupCallback = UcsiEvtPpmCleanup;
    Status = WdfObjectAllocateContext(Object, &ContextAttributes, (PVOID *)&Ppm);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Object);
        return Status;
    }

    Ppm->Device = WdfDevice;
    Ppm->Handle = (UCMUCSIPPM)Object;
    Ppm->UsbDeviceControllerEnabled = Config->UsbDeviceControllerEnabled;
    if (Config->ConnectorCollectionHandle != NULL)
        Ppm->ConnectorCount = UcsiGetCollectionContext(Config->ConnectorCollectionHandle)->Count;
    KeInitializeSpinLock(&Ppm->Lock);
    KeInitializeEvent(&Ppm->Wake, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Ppm->Notified, SynchronizationEvent, FALSE);
    DeviceContext->Ppm = Ppm->Handle;
    *PpmObject = Ppm->Handle;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
UcsiPpmSetUcsiCommandRequestQueue(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMUCSIPPM PpmObject,
    _In_ WDFQUEUE PpmRequestQueue)
{
    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PpmObject != NULL)
        UcsiGetPpmContext(PpmObject)->Queue = PpmRequestQueue;
}

static
BOOLEAN
UcsiWaitNotification(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _Out_ PUCSI_DATA_BLOCK Block)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    KIRQL Irql;

    Timeout.QuadPart = -10000LL * UCSI_COMMAND_TIMEOUT_MS;
    Status = KeWaitForSingleObject(&Ppm->Notified, Executive, KernelMode, FALSE, &Timeout);
    KeAcquireSpinLock(&Ppm->Lock, &Irql);
    *Block = Ppm->Block;
    KeReleaseSpinLock(&Ppm->Lock, Irql);
    return Status == STATUS_SUCCESS && !Ppm->Stopping;
}

static
NTSTATUS
UcsiWaitCompletion(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _Out_ PUCSI_DATA_BLOCK Result)
{
    do
    {
        if (!UcsiWaitNotification(Ppm, Result))
            return STATUS_IO_TIMEOUT;
    } while (!Result->CCI.CommandCompletedIndicator);

    return STATUS_SUCCESS;
}

static
NTSTATUS
UcsiAcknowledge(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _In_ BOOLEAN AcknowledgeChange)
{
    UCSI_DATA_BLOCK Block;
    NTSTATUS Status;

    KeClearEvent(&Ppm->Notified);
    Status = UcsiWriteBlock(Ppm, UcsiCommandAckCcCi | ((AcknowledgeChange ? 3ULL : 2ULL) << 16));
    if (!NT_SUCCESS(Status))
        return Status;

    do
    {
        if (!UcsiWaitNotification(Ppm, &Block))
            return STATUS_IO_TIMEOUT;
    } while (!Block.CCI.AcknowledgeCommandIndicator);

    return STATUS_SUCCESS;
}

static
NTSTATUS
UcsiFinishCommand(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _In_ BOOLEAN AcknowledgeChange,
    _Out_ PUCSI_DATA_BLOCK Result)
{
    NTSTATUS Status;

    Status = UcsiWaitCompletion(Ppm, Result);
    if (NT_SUCCESS(Status))
        Status = UcsiAcknowledge(Ppm, AcknowledgeChange);
    if (!NT_SUCCESS(Status))
        return Status;

    return UCSI_CMD_SUCCEEDED(Result->CCI) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static
NTSTATUS
UcsiCommand(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _In_ ULONG64 Control,
    _In_ BOOLEAN AcknowledgeChange,
    _Out_ PUCSI_DATA_BLOCK Result)
{
    NTSTATUS Status;

    KeClearEvent(&Ppm->Notified);
    Status = UcsiWriteBlock(Ppm, Control);
    if (!NT_SUCCESS(Status))
        return Status;

    return UcsiFinishCommand(Ppm, AcknowledgeChange, Result);
}

static
NTSTATUS
UcsiConnectorChanged(
    _In_ PUCSI_PPM_CONTEXT Ppm,
    _In_ ULONG Connector)
{
    PUCSI_GET_CONNECTOR_STATUS_IN ConnectorStatus;
    ULONG64 Commands[3];
    UCSI_DATA_BLOCK Block;
    ULONG Count = 0, Index;
    BOOLEAN Contract;
    NTSTATUS Status;

    KeClearEvent(&Ppm->Notified);
    Status = UcsiWriteBlock(Ppm, UcsiCommandGetConnectorStatus | ((ULONG64)Connector << 16));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UcsiWaitCompletion(Ppm, &Block);
    if (!NT_SUCCESS(Status))
        return Status;

    ConnectorStatus = &Block.MessageIn.ConnectorStatus;
    Contract = UCSI_CMD_SUCCEEDED(Block.CCI) &&
               ConnectorStatus->ConnectStatus &&
               ConnectorStatus->PowerOperationMode == UcsiPowerOperationModePd;
    if (Contract)
    {
        if (Ppm->Capability.bmOptionalFeatures.PdoDetailsAvailable)
        {
            Commands[Count++] = UcsiCommandGetPdos |
                                ((ULONG64)Connector << 16) |
                                ((ULONG64)(ConnectorStatus->PowerDirection == UcsiPowerDirectionConsumer) << 23) |
                                (3ULL << 32) |
                                (1ULL << 34);
        }

        if (Ppm->Capability.bmOptionalFeatures.AlternateModeDetailsAvailable)
        {
            Commands[Count++] = UcsiCommandGetAlternateModes |
                                ((ULONG64)UcsiGetAlternateModesRecipientSop << 16) |
                                ((ULONG64)Connector << 24) |
                                (1ULL << 40);
        }

        if (ConnectorStatus->ConnectorPartnerType == UcsiConnectorPartnerTypeDfp)
            Commands[Count++] = UcsiCommandSetUor | ((ULONG64)Connector << 16) | (1ULL << 23);
    }

    Status = UcsiAcknowledge(Ppm, Count == 0);
    if (NT_SUCCESS(Status) && !UCSI_CMD_SUCCEEDED(Block.CCI))
        Status = STATUS_UNSUCCESSFUL;
    for (Index = 0; Index < Count && NT_SUCCESS(Status); Index++)
        Status = UcsiCommand(Ppm, Commands[Index], Index + 1 == Count, &Block);
    return Status;
}

static
NTSTATUS
UcsiInitializePpm(
    _In_ PUCSI_PPM_CONTEXT Ppm)
{
    UCSI_DATA_BLOCK Block;
    ULONG Notifications;
    NTSTATUS Status;
    ULONG Connector;

    Status = UcsiFinishCommand(Ppm, FALSE, &Block);
    if (NT_SUCCESS(Status))
        Status = UcsiCommand(Ppm, UcsiCommandGetCapability, FALSE, &Block);
    if (!NT_SUCCESS(Status))
        return Status;

    Ppm->Capability = Block.MessageIn.Capability;
    for (Connector = 1; Connector <= Ppm->ConnectorCount; Connector++)
    {
        Status = UcsiCommand(Ppm, UcsiCommandGetConnectorCapability | ((ULONG64)Connector << 16), FALSE, &Block);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    for (Connector = 1; Connector <= Ppm->ConnectorCount; Connector++)
    {
        Status = UcsiCommand(Ppm, UcsiCommandGetConnectorStatus | ((ULONG64)Connector << 16), FALSE, &Block);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Notifications = UCSI_NOTIFICATIONS;
    if (Ppm->Capability.bmOptionalFeatures.ExternalSupplyNotificationSupported)
        Notifications |= 1 << 1;
    if (Ppm->Capability.bmOptionalFeatures.PdResetNotificationSupported)
        Notifications |= 1 << 7;
    if (Ppm->Capability.bmOptionalFeatures.AlternateModeDetailsAvailable)
        Notifications |= 1 << 8;

    return UcsiCommand(Ppm, UcsiCommandSetNotificationEnable | ((ULONG64)Notifications << 16), FALSE, &Block);
}

static
VOID
NTAPI
UcsiEngineThread(
    _In_ PVOID Parameter)
{
    PUCSI_PPM_CONTEXT Ppm = Parameter;
    ULONG Connector;
    KIRQL Irql;

    if (!NT_SUCCESS(UcsiInitializePpm(Ppm)) && !Ppm->Stopping)
        WdfDeviceSetFailed(Ppm->Device, WdfDeviceFailedNoRestart);

    while (!Ppm->Stopping)
    {
        KeWaitForSingleObject(&Ppm->Wake, Executive, KernelMode, FALSE, NULL);
        if (Ppm->Stopping)
            break;

        KeAcquireSpinLock(&Ppm->Lock, &Irql);
        Connector = Ppm->Block.CCI.ConnectorChangeIndicator;
        KeReleaseSpinLock(&Ppm->Lock, Irql);
        if (Connector != 0 && !NT_SUCCESS(UcsiConnectorChanged(Ppm, Connector)) && !Ppm->Stopping)
        {
            WdfDeviceSetFailed(Ppm->Device, WdfDeviceFailedNoRestart);
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
UcsiStopEngine(
    _In_ PUCSI_PPM_CONTEXT Ppm)
{
    PKTHREAD Thread = Ppm->Thread;

    if (Thread == NULL)
        return;

    Ppm->Stopping = TRUE;
    KeSetEvent(&Ppm->Notified, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Ppm->Wake, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(Thread);
    Ppm->Thread = NULL;
    Ppm->Started = FALSE;
    Ppm->Stopping = FALSE;
}

static
NTSTATUS
NTAPI
UcsiPpmStart(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMUCSIPPM PpmObject)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PUCSI_PPM_CONTEXT Ppm;
    UCSI_DATA_BLOCK Block;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    HANDLE Handle;
    ULONG Attempt;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PpmObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Ppm = UcsiGetPpmContext(PpmObject);
    if (Ppm->Queue == NULL || Ppm->Started)
        return STATUS_INVALID_DEVICE_STATE;

    KeClearEvent(&Ppm->Notified);
    Status = UcsiWriteBlock(Ppm, UcsiCommandPpmReset);
    if (!NT_SUCCESS(Status))
        return Status;

    Timeout.QuadPart = -10000LL * UCSI_COMMAND_TIMEOUT_MS;
    for (Attempt = 0; ; Attempt++)
    {
        KeWaitForSingleObject(&Ppm->Notified, Executive, KernelMode, FALSE, &Timeout);
        Status = UcsiReadBlock(Ppm, &Block);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Block.CCI.ResetCompletedIndicator)
            break;
        if (Attempt == UCSI_RESET_ATTEMPTS)
            return STATUS_IO_TIMEOUT;
    }

    KeClearEvent(&Ppm->Notified);
    KeClearEvent(&Ppm->Wake);
    Status = UcsiWriteBlock(Ppm, UcsiCommandSetNotificationEnable | (1ULL << 16));
    if (!NT_SUCCESS(Status))
        return Status;

    Ppm->Started = TRUE;
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, UcsiEngineThread, Ppm);
    if (!NT_SUCCESS(Status))
    {
        Ppm->Started = FALSE;
        return Status;
    }

    ObReferenceObjectByHandle(Handle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID *)&Ppm->Thread, NULL);
    ZwClose(Handle);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
UcsiPpmStop(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMUCSIPPM PpmObject)
{
    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PpmObject != NULL)
        UcsiStopEngine(UcsiGetPpmContext(PpmObject));
}

static
VOID
NTAPI
UcsiPpmNotification(
    _In_ PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMUCSIPPM PpmObject,
    _In_ PUCSI_DATA_BLOCK DataBlock)
{
    PUCSI_PPM_CONTEXT Ppm;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PpmObject == NULL || DataBlock == NULL)
        return;

    Ppm = UcsiGetPpmContext(PpmObject);
    KeAcquireSpinLock(&Ppm->Lock, &Irql);
    Ppm->Block = *DataBlock;
    KeReleaseSpinLock(&Ppm->Lock, Irql);
    KeSetEvent(&Ppm->Notified, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Ppm->Wake, IO_NO_INCREMENT, FALSE);
}

static PVOID UcsiFunctions[UcmucsiFunctionTableNumEntries] =
{
    UcsiDeviceInitInitialize,
    UcsiDeviceInitialize,
    UcsiConnectorCollectionCreate,
    UcsiConnectorCollectionAddConnector,
    UcsiPpmCreate,
    UcsiPpmSetUcsiCommandRequestQueue,
    UcsiPpmStart,
    UcsiPpmStop,
    UcsiPpmNotification
};

static
NTSTATUS
NTAPI
UcsiLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo, ClientGlobals, (PVOID const *)UcsiFunctions, RTL_NUMBER_OF(UcsiFunctions), 1);
}

static
VOID
NTAPI
UcsiLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO UcsiLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 0, 0},
    NULL,
    NULL,
    UcsiLibraryBindClient,
    UcsiLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject, RegistryPath, L"\\Device\\UcmUcsiCx", &UcsiLibraryInfo);
}
