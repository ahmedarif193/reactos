/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hardware notification KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "classlibrary.h"
#include <initguid.h>
#include <hwn.h>
#include <hwnclx.h>

typedef struct _HWNCLX_DRIVER_CONTEXT
{
    HWN_CLIENT_REGISTRATION_PACKET Packet;
    BOOLEAN Registered;
} HWNCLX_DRIVER_CONTEXT, *PHWNCLX_DRIVER_CONTEXT;

typedef struct _HWNCLX_DEVICE_CONTEXT
{
    WDFDRIVER Driver;
    PHWN_CLIENT_REGISTRATION_PACKET Packet;
    WDFQUEUE Queue;
    PVOID ClientContext;
    CLIENT_DEVICE_INFORMATION Information;
    BOOLEAN Initialized;
    BOOLEAN Prepared;
    BOOLEAN Started;
} HWNCLX_DEVICE_CONTEXT, *PHWNCLX_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(HWNCLX_DRIVER_CONTEXT, HwNCxGetDriverContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(HWNCLX_DEVICE_CONTEXT, HwNCxGetDeviceContext)

static
NTSTATUS
NTAPI
HwNCxEvtPrePrepareHardware(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PHWNCLX_DEVICE_CONTEXT Device = HwNCxGetDeviceContext(DeviceHandle);
    NTSTATUS Status;

    if (Device == NULL || !Device->Initialized)
        return STATUS_INVALID_DEVICE_STATE;

    if (Device->Packet->ClientInitializeDevice != NULL)
    {
        Status = Device->Packet->ClientInitializeDevice(DeviceHandle,
                                                        Device->ClientContext,
                                                        ResourcesRaw,
                                                        ResourcesTranslated);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Device->Prepared = TRUE;

    RtlZeroMemory(&Device->Information, sizeof(Device->Information));
    Device->Information.Version = HWN_DEVICE_INFORMATION_VERSION;
    Device->Information.Size = sizeof(Device->Information);
    if (Device->Packet->ClientQueryDeviceInformation != NULL)
    {
        Status = Device->Packet->ClientQueryDeviceInformation(Device->ClientContext,
                                                              &Device->Information);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HwNCxEvtPostReleaseHardware(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PHWNCLX_DEVICE_CONTEXT Device = HwNCxGetDeviceContext(DeviceHandle);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    if (Device == NULL || !Device->Prepared)
        return STATUS_SUCCESS;

    Device->Prepared = FALSE;
    if (Device->Packet->ClientUnInitializeDevice != NULL)
        return Device->Packet->ClientUnInitializeDevice(DeviceHandle, Device->ClientContext);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HwNCxEvtPreD0Entry(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PHWNCLX_DEVICE_CONTEXT Device = HwNCxGetDeviceContext(DeviceHandle);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);

    if (Device == NULL || !Device->Prepared)
        return STATUS_INVALID_DEVICE_STATE;

    if (Device->Packet->ClientStartDevice != NULL)
    {
        Status = Device->Packet->ClientStartDevice(Device->ClientContext);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Device->Started = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HwNCxEvtPostD0Exit(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PHWNCLX_DEVICE_CONTEXT Device = HwNCxGetDeviceContext(DeviceHandle);

    UNREFERENCED_PARAMETER(TargetState);

    if (Device == NULL || !Device->Started)
        return STATUS_SUCCESS;

    Device->Started = FALSE;
    if (Device->Packet->ClientStopDevice != NULL)
        return Device->Packet->ClientStopDevice(Device->ClientContext);

    return STATUS_SUCCESS;
}

static
VOID
NTAPI
HwNCxEvtDeviceCleanup(
    _In_ WDFOBJECT Object)
{
    PHWNCLX_DEVICE_CONTEXT Device = HwNCxGetDeviceContext(Object);

    if (Device == NULL)
        return;

    if (Device->ClientContext != NULL)
    {
        ExFreePoolWithTag(Device->ClientContext, WDFCX_TAG);
        Device->ClientContext = NULL;
    }
}

static
VOID
NTAPI
HwNCxEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(Queue);
    PHWNCLX_DEVICE_CONTEXT Device = HwNCxGetDeviceContext(DeviceHandle);
    PVOID InputBuffer = NULL;
    PVOID OutputBuffer = NULL;
    ULONG Bytes = 0;
    NTSTATUS Status;

    if (Device == NULL || !Device->Started)
    {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return;
    }
    if (InputBufferLength > MAXULONG || OutputBufferLength > MAXULONG)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    switch (IoControlCode)
    {
        case IOCTL_HWN_SET_STATE:
            if (Device->Packet->ClientSetHwNState == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            Status = WdfRequestRetrieveInputBuffer(Request, HWN_HEADER_SIZE, &InputBuffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            Status = Device->Packet->ClientSetHwNState(Device->ClientContext,
                                                       InputBuffer,
                                                       (ULONG)InputBufferLength,
                                                       &Bytes);
            break;

        case IOCTL_HWN_GET_STATE:
            if (Device->Packet->ClientGetHwNState == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            Status = WdfRequestRetrieveOutputBuffer(Request, HWN_HEADER_SIZE, &OutputBuffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            if (InputBufferLength != 0)
            {
                Status = WdfRequestRetrieveInputBuffer(Request, 0, &InputBuffer, NULL);
                if (!NT_SUCCESS(Status))
                    break;
            }
            Status = Device->Packet->ClientGetHwNState(Device->ClientContext,
                                                       OutputBuffer,
                                                       (ULONG)OutputBufferLength,
                                                       InputBuffer,
                                                       (ULONG)InputBufferLength,
                                                       &Bytes);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    WdfRequestCompleteWithInformation(Request, Status, NT_SUCCESS(Status) ? Bytes : 0);
}

static
NTSTATUS
HWN_EXPORT
HwNCxDdiRegisterClient(
    _In_ WDFDRIVER Driver,
    _Inout_ PHWN_CLIENT_REGISTRATION_PACKET Packet,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    PHWNCLX_DRIVER_CONTEXT Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    if (Driver == NULL || Packet == NULL ||
        Packet->Version != HWN_CLIENT_VERSION ||
        Packet->Size < sizeof(HWN_CLIENT_REGISTRATION_PACKET))
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, HWNCLX_DRIVER_CONTEXT);
    Status = WdfObjectAllocateContext(Driver, &Attributes, (PVOID *)&Context);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
    {
        Context = HwNCxGetDriverContext(Driver);
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    if (Context->Registered)
        return STATUS_INVALID_DEVICE_STATE;

    RtlCopyMemory(&Context->Packet, Packet, sizeof(HWN_CLIENT_REGISTRATION_PACKET));
    Context->Registered = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HWN_EXPORT
HwNCxDdiUnregisterClient(
    _In_ WDFDRIVER Driver)
{
    PHWNCLX_DRIVER_CONTEXT Context;

    if (Driver == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = HwNCxGetDriverContext(Driver);
    if (Context == NULL || !Context->Registered)
        return STATUS_INVALID_DEVICE_STATE;

    Context->Registered = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HWN_EXPORT
HwNCxDdiProcessAddDevicePreDeviceCreate(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit,
    _Out_ PWDF_OBJECT_ATTRIBUTES FdoAttributes)
{
    PHWNCLX_DRIVER_CONTEXT Context;
    PWDFCXDEVICE_INIT CxInit;
    WDFCX_PNPPOWER_EVENT_CALLBACKS PnpCallbacks;

    if (Driver == NULL || DeviceInit == NULL || FdoAttributes == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = HwNCxGetDriverContext(Driver);
    if (Context == NULL || !Context->Registered)
        return STATUS_INVALID_DEVICE_STATE;

    CxInit = WdfCxDeviceInitAllocate(WdfDriverGlobals, DeviceInit);
    if (CxInit == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&PnpCallbacks, sizeof(PnpCallbacks));
    PnpCallbacks.Size = sizeof(PnpCallbacks);
    PnpCallbacks.EvtCxDevicePrePrepareHardware = HwNCxEvtPrePrepareHardware;
    PnpCallbacks.EvtCxDevicePostReleaseHardware = HwNCxEvtPostReleaseHardware;
    PnpCallbacks.EvtCxDevicePreD0Entry = HwNCxEvtPreD0Entry;
    PnpCallbacks.EvtCxDevicePostD0Exit = HwNCxEvtPostD0Exit;
    WdfCxDeviceInitSetPnpPowerEventCallbacks(WdfDriverGlobals, CxInit, &PnpCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(FdoAttributes, HWNCLX_DEVICE_CONTEXT);
    FdoAttributes->EvtCleanupCallback = HwNCxEvtDeviceCleanup;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HWN_EXPORT
HwNCxDdiProcessAddDevicePostDeviceCreate(
    _In_ WDFDRIVER Driver,
    _In_ WDFDEVICE DeviceHandle,
    _In_ LPGUID DeviceGuid)
{
    PHWNCLX_DRIVER_CONTEXT DriverContext;
    PHWNCLX_DEVICE_CONTEXT Device;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    NTSTATUS Status;

    if (Driver == NULL || DeviceHandle == NULL || DeviceGuid == NULL)
        return STATUS_INVALID_PARAMETER;

    DriverContext = HwNCxGetDriverContext(Driver);
    Device = HwNCxGetDeviceContext(DeviceHandle);
    if (DriverContext == NULL || !DriverContext->Registered || Device == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    Device->Driver = Driver;
    Device->Packet = &DriverContext->Packet;

    if (Device->Packet->DeviceContextSize != 0)
    {
        Device->ClientContext = ExAllocatePoolWithTag(NonPagedPool,
                                                      Device->Packet->DeviceContextSize,
                                                      WDFCX_TAG);
        if (Device->ClientContext == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Device->ClientContext, Device->Packet->DeviceContextSize);
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchSequential);
    QueueConfig.EvtIoDeviceControl = HwNCxEvtIoDeviceControl;
    Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->Queue);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WdfDeviceCreateDeviceInterface(DeviceHandle, DeviceGuid, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Device->Initialized = TRUE;
    return STATUS_SUCCESS;
}

static PVOID HwNCxFunctions[HWN_CLX_TOTAL_EXPORTS] =
{
    HwNCxDdiRegisterClient,
    HwNCxDdiUnregisterClient,
    HwNCxDdiProcessAddDevicePreDeviceCreate,
    HwNCxDdiProcessAddDevicePostDeviceCreate
};

static
NTSTATUS
NTAPI
HwNCxLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo,
                           ClientGlobals,
                           HwNCxFunctions,
                           RTL_NUMBER_OF(HwNCxFunctions),
                           1);
}

static
VOID
NTAPI
HwNCxLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO HwNCxLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 0, 0},
    NULL,
    NULL,
    HwNCxLibraryBindClient,
    HwNCxLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject,
                                RegistryPath,
                                L"\\Device\\mshwnclx",
                                &HwNCxLibraryInfo);
}
