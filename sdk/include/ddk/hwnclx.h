/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hardware notification class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __HWNCLX_H__
#define __HWNCLX_H__

#ifdef __cplusplus
extern "C" {
#endif

#define HWN_CLIENT_VERSION             0x1
#define HWN_DEVICE_INFORMATION_VERSION 0x1

typedef struct _CLIENT_DEVICE_INFORMATION
{
    USHORT Version;
    USHORT Size;
    USHORT TotalHwNs;
} CLIENT_DEVICE_INFORMATION, *PCLIENT_DEVICE_INFORMATION;

#define HWN_EXPORT __stdcall

typedef NTSTATUS (HWN_EXPORT HWN_CLIENT_INITIALIZE_DEVICE)(
    _In_ WDFDEVICE Device,
    _In_ PVOID Context,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated);
typedef HWN_CLIENT_INITIALIZE_DEVICE *PHWN_CLIENT_INITIALIZE_DEVICE;

typedef NTSTATUS (HWN_EXPORT HWN_CLIENT_UNINITIALIZE_DEVICE)(
    _In_ WDFDEVICE Device,
    _In_ PVOID Context);
typedef HWN_CLIENT_UNINITIALIZE_DEVICE *PHWN_CLIENT_UNINITIALIZE_DEVICE;

typedef NTSTATUS (HWN_EXPORT HWN_CLIENT_QUERY_DEVICE_INFORMATION)(
    _In_ PVOID Context,
    _Out_ PCLIENT_DEVICE_INFORMATION Information);
typedef HWN_CLIENT_QUERY_DEVICE_INFORMATION *PHWN_CLIENT_QUERY_DEVICE_INFORMATION;

typedef NTSTATUS (HWN_EXPORT HWN_CLIENT_START_DEVICE)(
    _In_ PVOID Context);
typedef HWN_CLIENT_START_DEVICE *PHWN_CLIENT_START_DEVICE;

typedef NTSTATUS (HWN_EXPORT HWN_CLIENT_STOP_DEVICE)(
    _In_ PVOID Context);
typedef HWN_CLIENT_STOP_DEVICE *PHWN_CLIENT_STOP_DEVICE;

typedef NTSTATUS (HWN_EXPORT HWN_CLIENT_GET_STATE)(
    _In_ PVOID Context,
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_ PULONG BytesRead);
typedef HWN_CLIENT_GET_STATE *PHWN_CLIENT_GET_STATE;

typedef NTSTATUS (HWN_EXPORT HWN_CLIENT_SET_STATE)(
    _In_ PVOID Context,
    _In_reads_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG BytesWritten);
typedef HWN_CLIENT_SET_STATE *PHWN_CLIENT_SET_STATE;

typedef struct _HWN_CLIENT_REGISTRATION_PACKET
{
    USHORT Version;
    USHORT Size;
    ULONG DeviceContextSize;
    ULONG Reserved;
    PHWN_CLIENT_INITIALIZE_DEVICE ClientInitializeDevice;
    PHWN_CLIENT_UNINITIALIZE_DEVICE ClientUnInitializeDevice;
    PHWN_CLIENT_QUERY_DEVICE_INFORMATION ClientQueryDeviceInformation;
    PHWN_CLIENT_START_DEVICE ClientStartDevice;
    PHWN_CLIENT_STOP_DEVICE ClientStopDevice;
    PHWN_CLIENT_SET_STATE ClientSetHwNState;
    PHWN_CLIENT_GET_STATE ClientGetHwNState;
} HWN_CLIENT_REGISTRATION_PACKET, *PHWN_CLIENT_REGISTRATION_PACKET;

typedef NTSTATUS (HWN_EXPORT HWN_CLX_REGISTER_CLIENT)(
    _In_ WDFDRIVER Driver,
    _Inout_ PHWN_CLIENT_REGISTRATION_PACKET RegistrationPacket,
    _In_ PUNICODE_STRING RegistryPath);
typedef HWN_CLX_REGISTER_CLIENT *PHWN_CLX_REGISTER_CLIENT;

typedef NTSTATUS (HWN_EXPORT HWN_CLX_UNREGISTER_CLIENT)(
    _In_ WDFDRIVER Driver);
typedef HWN_CLX_UNREGISTER_CLIENT *PHWN_CLX_UNREGISTER_CLIENT;

typedef NTSTATUS (HWN_EXPORT HWN_CLX_PROCESS_ADD_DEVICE_PRE_DEVICE_CREATE)(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit,
    _Out_ PWDF_OBJECT_ATTRIBUTES FdoAttributes);
typedef HWN_CLX_PROCESS_ADD_DEVICE_PRE_DEVICE_CREATE *PHWN_CLX_PROCESS_ADD_DEVICE_PRE_DEVICE_CREATE;

typedef NTSTATUS (HWN_EXPORT HWN_CLX_PROCESS_ADD_DEVICE_POST_DEVICE_CREATE)(
    _In_ WDFDRIVER Driver,
    _In_ WDFDEVICE Device,
    _In_ LPGUID DeviceGuid);
typedef HWN_CLX_PROCESS_ADD_DEVICE_POST_DEVICE_CREATE *PHWN_CLX_PROCESS_ADD_DEVICE_POST_DEVICE_CREATE;

typedef enum _HWN_CLX_EXPORT_INDEX
{
    RegisterClientIndex = 0,
    UnregisterClientIndex,
    AddDevicePreDeviceCreateIndex,
    AddDevicePostDeviceCreateIndex,
    HwNExportLastExportIndex
} HWN_CLX_EXPORT_INDEX, *PHWN_CLX_EXPORT_INDEX;

#define HWN_CLX_TOTAL_EXPORTS (HwNExportLastExportIndex)

typedef VOID (*PHWN_CLX_EXPORTED_INTERFACES)(VOID);

extern PHWN_CLX_EXPORTED_INTERFACES HwNClxExportedInterfaces[HWN_CLX_TOTAL_EXPORTS];

FORCEINLINE
NTSTATUS
HwNRegisterClient(
    _In_ WDFDRIVER Driver,
    _Inout_ PHWN_CLIENT_REGISTRATION_PACKET RegistrationPacket,
    _In_ PUNICODE_STRING RegistryPath)
{
    return ((PHWN_CLX_REGISTER_CLIENT)HwNClxExportedInterfaces[RegisterClientIndex])(
        Driver, RegistrationPacket, RegistryPath);
}

FORCEINLINE
NTSTATUS
HwNUnregisterClient(
    _In_ WDFDRIVER Driver)
{
    return ((PHWN_CLX_UNREGISTER_CLIENT)HwNClxExportedInterfaces[UnregisterClientIndex])(Driver);
}

FORCEINLINE
NTSTATUS
HwNProcessAddDevicePreDeviceCreate(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit,
    _Out_ PWDF_OBJECT_ATTRIBUTES FdoAttributes)
{
    return ((PHWN_CLX_PROCESS_ADD_DEVICE_PRE_DEVICE_CREATE)
        HwNClxExportedInterfaces[AddDevicePreDeviceCreateIndex])(Driver, DeviceInit, FdoAttributes);
}

FORCEINLINE
NTSTATUS
HwNProcessAddDevicePostDeviceCreate(
    _In_ WDFDRIVER Driver,
    _In_ WDFDEVICE Device,
    _In_ LPGUID DeviceGuid)
{
    return ((PHWN_CLX_PROCESS_ADD_DEVICE_POST_DEVICE_CREATE)
        HwNClxExportedInterfaces[AddDevicePostDeviceCreateIndex])(Driver, Device, DeviceGuid);
}

#ifdef __cplusplus
}
#endif

#endif
