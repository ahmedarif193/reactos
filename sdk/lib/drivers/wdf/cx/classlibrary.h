/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Common KMDF class-library registration helpers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include "wdf.h"
#include <fxldr.h>
#include "wdfcx.h"
#include <reshub.h>

#define WDFCX_TAG 'xCdW'

#define WDFCX_CALL(_Index, _Type) ((_Type)WdfFunctions[_Index])

#define WdfCxDeviceInitAllocate(_Globals, _DeviceInit) \
    WDFCX_CALL(WdfCxDeviceInitAllocateTableIndex, PFN_WDFCXDEVICEINITALLOCATE)( \
        (_Globals), (_DeviceInit))

#define WdfCxDeviceInitSetRequestAttributes(_Globals, _CxInit, _Attributes) \
    WDFCX_CALL(WdfCxDeviceInitSetRequestAttributesTableIndex, PFN_WDFCXDEVICEINITSETREQUESTATTRIBUTES)( \
        (_Globals), (_CxInit), (_Attributes))

#define WdfCxDeviceInitSetFileObjectConfig(_Globals, _CxInit, _Config, _Attributes) \
    WDFCX_CALL(WdfCxDeviceInitSetFileObjectConfigTableIndex, PFN_WDFCXDEVICEINITSETFILEOBJECTCONFIG)( \
        (_Globals), (_CxInit), (_Config), (_Attributes))

#define WdfCxDeviceInitSetIoInCallerContextCallback(_Globals, _CxInit, _Callback) \
    WDFCX_CALL(WdfCxDeviceInitSetIoInCallerContextCallbackTableIndex, PFN_WDFCXDEVICEINITSETIOINCALLERCONTEXTCALLBACK)( \
        (_Globals), (_CxInit), (_Callback))

#define WdfCxDeviceInitSetPnpPowerEventCallbacks(_Globals, _CxInit, _Callbacks) \
    WDFCX_CALL(WdfCxDeviceInitSetPnpPowerEventCallbacksTableIndex, PFN_WDFCXDEVICEINITSETPNPPOWEREVENTCALLBACKS)( \
        (_Globals), (_CxInit), (_Callbacks))

#define WdfCxDeviceInitAllocateContext(_Globals, _DeviceInit, _Attributes, _Context) \
    WDFCX_CALL(WdfCxDeviceInitAllocateContextTableIndex, PFN_WDFCXDEVICEINITALLOCATECONTEXT)( \
        (_Globals), (_DeviceInit), (_Attributes), (_Context))

typedef VOID (NTAPI *PFN_WDFCX_CLIENT_DEVICEINITSETIOTYPE)(PWDF_DRIVER_GLOBALS, PWDFDEVICE_INIT, WDF_DEVICE_IO_TYPE);
typedef VOID (NTAPI *PFN_WDFCX_CLIENT_DEVICEINITSETEXCLUSIVE)(PWDF_DRIVER_GLOBALS, PWDFDEVICE_INIT, BOOLEAN);
typedef VOID (NTAPI *PFN_WDFCX_CLIENT_DEVICEINITSETDEVICETYPE)(PWDF_DRIVER_GLOBALS, PWDFDEVICE_INIT, DEVICE_TYPE);
typedef VOID (NTAPI *PFN_WDFCX_CLIENT_DEVICEINITSETCHARACTERISTICS)(PWDF_DRIVER_GLOBALS, PWDFDEVICE_INIT, ULONG, BOOLEAN);

#define WdfCxClientDeviceInitSetIoType(_ClientGlobals, _DeviceInit, _IoType) \
    WDFCX_CALL(WdfDeviceInitSetIoTypeTableIndex, PFN_WDFCX_CLIENT_DEVICEINITSETIOTYPE)( \
        (_ClientGlobals), (_DeviceInit), (_IoType))

#define WdfCxClientDeviceInitSetExclusive(_ClientGlobals, _DeviceInit, _Exclusive) \
    WDFCX_CALL(WdfDeviceInitSetExclusiveTableIndex, PFN_WDFCX_CLIENT_DEVICEINITSETEXCLUSIVE)( \
        (_ClientGlobals), (_DeviceInit), (_Exclusive))

#define WdfCxClientDeviceInitSetDeviceType(_ClientGlobals, _DeviceInit, _DeviceType) \
    WDFCX_CALL(WdfDeviceInitSetDeviceTypeTableIndex, PFN_WDFCX_CLIENT_DEVICEINITSETDEVICETYPE)( \
        (_ClientGlobals), (_DeviceInit), (_DeviceType))

#define WdfCxClientDeviceInitSetCharacteristics(_ClientGlobals, _DeviceInit, _Characteristics, _OrIn) \
    WDFCX_CALL(WdfDeviceInitSetCharacteristicsTableIndex, PFN_WDFCX_CLIENT_DEVICEINITSETCHARACTERISTICS)( \
        (_ClientGlobals), (_DeviceInit), (_Characteristics), (_OrIn))

FORCEINLINE
BOOLEAN
WdfCxParseConnectionId(
    _In_ PCUNICODE_STRING FileName,
    _Out_ PLARGE_INTEGER ConnectionId)
{
    ULONGLONG Value = 0;
    USHORT Index = 0;
    USHORT Digits = 0;
    USHORT Length;

    ConnectionId->QuadPart = 0;
    if (FileName == NULL || FileName->Buffer == NULL)
        return FALSE;

    Length = FileName->Length / sizeof(WCHAR);
    if (Length >= 1 && FileName->Buffer[0] == L'\\')
        Index++;

    while (Index < Length)
    {
        WCHAR Character = FileName->Buffer[Index++];
        UCHAR Digit;

        if (Character >= L'0' && Character <= L'9')
            Digit = (UCHAR)(Character - L'0');
        else if (Character >= L'a' && Character <= L'f')
            Digit = (UCHAR)(Character - L'a' + 10);
        else if (Character >= L'A' && Character <= L'F')
            Digit = (UCHAR)(Character - L'A' + 10);
        else
            return FALSE;

        if (++Digits > 16)
            return FALSE;
        Value = (Value << 4) | Digit;
    }

    if (Digits != 16 || Value == 0)
        return FALSE;

    ConnectionId->QuadPart = (LONGLONG)Value;
    return TRUE;
}

static
NTSTATUS
WdfCxQueryConnectionProperties(
    _In_ LARGE_INTEGER ConnectionId,
    _Outptr_ PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER *Properties)
{
    UNICODE_STRING HubName = RTL_CONSTANT_STRING(RESOURCE_HUB_DEVICE_NAME);
    RH_QUERY_CONNECTION_PROPERTIES_INPUT_BUFFER Input;
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Output;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    ULONG OutputLength;
    NTSTATUS Status;

    *Properties = NULL;

    Status = IoGetDeviceObjectPointer(&HubName,
                                      FILE_READ_DATA | FILE_WRITE_DATA,
                                      &FileObject,
                                      &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    OutputLength = FIELD_OFFSET(RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER, ConnectionProperties) + 256;
    for (;;)
    {
        Output = ExAllocatePoolWithTag(NonPagedPool, OutputLength, WDFCX_TAG);
        if (Output == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlZeroMemory(&Input, sizeof(Input));
        Input.Version = RH_QUERY_CONNECTION_PROPERTIES_INPUT_VERSION;
        Input.QueryType = ConnectionIdType;
        Input.u.ConnectionId = ConnectionId;

        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Irp = IoBuildDeviceIoControlRequest(IOCTL_RH_QUERY_CONNECTION_PROPERTIES,
                                            DeviceObject,
                                            &Input,
                                            sizeof(Input),
                                            Output,
                                            OutputLength,
                                            FALSE,
                                            &Event,
                                            &IoStatus);
        if (Irp == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            ExFreePoolWithTag(Output, WDFCX_TAG);
            break;
        }

        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatus.Status;
        }

        if (NT_SUCCESS(Status))
        {
            *Properties = Output;
            break;
        }

        if (Status == STATUS_BUFFER_TOO_SMALL &&
            IoStatus.Information >= FIELD_OFFSET(RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER, ConnectionProperties) &&
            Output->PropertiesLength != 0)
        {
            OutputLength = FIELD_OFFSET(RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER, ConnectionProperties) +
                           Output->PropertiesLength;
            ExFreePoolWithTag(Output, WDFCX_TAG);
            continue;
        }

        ExFreePoolWithTag(Output, WDFCX_TAG);
        break;
    }

    ObDereferenceObject(FileObject);
    return Status;
}

static
NTSTATUS
WdfCxBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals,
    _In_reads_(FunctionCount) PVOID const *Functions,
    _In_ ULONG FunctionCount,
    _In_ ULONG ClassMajorVersion)
{
    if (ClassBindInfo == NULL || ClientGlobals == NULL ||
        ClassBindInfo->Size < sizeof(WDF_CLASS_BIND_INFO))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ClassBindInfo->Version.Major != ClassMajorVersion ||
        ClassBindInfo->FunctionTable == NULL ||
        ClassBindInfo->FunctionTableCount > FunctionCount)
    {
        return STATUS_REVISION_MISMATCH;
    }

    RtlCopyMemory(ClassBindInfo->FunctionTable,
                  Functions,
                  ClassBindInfo->FunctionTableCount * sizeof(Functions[0]));

    if (ClassBindInfo->ClassBindInfo != NULL)
        *(PVOID *)ClassBindInfo->ClassBindInfo = *ClientGlobals;

    return STATUS_SUCCESS;
}

static
VOID
WdfCxUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo)
{
    if (ClassBindInfo == NULL)
        return;

    if (ClassBindInfo->FunctionTable != NULL)
    {
        RtlZeroMemory(ClassBindInfo->FunctionTable,
                      ClassBindInfo->FunctionTableCount * sizeof(PVOID));
    }

    if (ClassBindInfo->ClassBindInfo != NULL)
        *(PVOID *)ClassBindInfo->ClassBindInfo = NULL;
}

static
VOID
NTAPI
WdfCxEvtDriverUnload(
    _In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);
}

static
NTSTATUS
WdfCxRegisterLibrary(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PCWSTR DeviceName,
    _In_ PWDF_CLASS_LIBRARY_INFO ClassLibraryInfo)
{
    DECLARE_CONST_UNICODE_STRING(Sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    WDF_DRIVER_CONFIG DriverConfig;
    PWDFDEVICE_INIT DeviceInit;
    UNICODE_STRING ObjectName;
    WDFDRIVER Driver;
    WDFDEVICE Device;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&DriverConfig, NULL);
    DriverConfig.DriverInitFlags = WdfDriverInitNonPnpDriver;
    DriverConfig.EvtDriverUnload = WdfCxEvtDriverUnload;
    DriverConfig.DriverPoolTag = WDFCX_TAG;

    Status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES,
                             &DriverConfig,
                             &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&ObjectName, DeviceName);
    DeviceInit = WdfControlDeviceInitAllocate(Driver, &Sddl);
    if (DeviceInit == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = WdfDeviceInitAssignName(DeviceInit, &ObjectName);
    if (NT_SUCCESS(Status))
    {
        Status = WdfDeviceCreate(&DeviceInit, WDF_NO_OBJECT_ATTRIBUTES, &Device);
    }

    if (!NT_SUCCESS(Status))
    {
        if (DeviceInit != NULL)
            WdfDeviceInitFree(DeviceInit);
        return Status;
    }

    WdfControlFinishInitializing(Device);

    return WdfRegisterClassLibrary(ClassLibraryInfo, RegistryPath, &ObjectName);
}
