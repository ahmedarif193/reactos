/*
 * PROJECT:     ReactOS WinUSB
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     WinUSB entry points reporting an unsupported device stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winusb.h"

typedef struct _WINUSB_SETUP_PACKET
{
    UCHAR RequestType;
    UCHAR Request;
    USHORT Value;
    USHORT Index;
    USHORT Length;
} WINUSB_SETUP_PACKET, *PWINUSB_SETUP_PACKET;

static BOOL WinUsbUnsupported(VOID)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_Initialize(HANDLE DeviceHandle, PWINUSB_INTERFACE_HANDLE InterfaceHandle)
{
    if (InterfaceHandle != NULL)
        *InterfaceHandle = NULL;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_GetAssociatedInterface(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                          UCHAR AssociatedInterfaceIndex,
                                          PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle)
{
    if (AssociatedInterfaceHandle != NULL)
        *AssociatedInterfaceHandle = NULL;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_ControlTransfer(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                   WINUSB_SETUP_PACKET SetupPacket,
                                   PUCHAR Buffer,
                                   ULONG BufferLength,
                                   PULONG LengthTransferred,
                                   LPOVERLAPPED Overlapped)
{
    if (LengthTransferred != NULL)
        *LengthTransferred = 0;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_ReadPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                            UCHAR PipeID,
                            PUCHAR Buffer,
                            ULONG BufferLength,
                            PULONG LengthTransferred,
                            LPOVERLAPPED Overlapped)
{
    if (LengthTransferred != NULL)
        *LengthTransferred = 0;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_WritePipe(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                             UCHAR PipeID,
                             PUCHAR Buffer,
                             ULONG BufferLength,
                             PULONG LengthTransferred,
                             LPOVERLAPPED Overlapped)
{
    if (LengthTransferred != NULL)
        *LengthTransferred = 0;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_ResetPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_AbortPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_FlushPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle, UCHAR PipeID)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_SetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                              UCHAR SettingNumber)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_GetCurrentAlternateSetting(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                              PUCHAR SettingNumber)
{
    if (SettingNumber != NULL)
        *SettingNumber = 0;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_GetOverlappedResult(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                       LPOVERLAPPED Overlapped,
                                       LPDWORD NumberOfBytesTransferred,
                                       BOOL Wait)
{
    if (NumberOfBytesTransferred != NULL)
        *NumberOfBytesTransferred = 0;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_QueryDeviceInformation(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                          ULONG InformationType,
                                          PULONG BufferLength,
                                          PVOID Buffer)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_QueryInterfaceSettings(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                          UCHAR AlternateInterfaceNumber,
                                          PVOID UsbAltInterfaceDescriptor)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_QueryPipe(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                             UCHAR AlternateInterfaceNumber,
                             UCHAR PipeIndex,
                             PVOID PipeInformation)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_GetDescriptor(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                 UCHAR DescriptorType,
                                 UCHAR Index,
                                 USHORT LanguageID,
                                 PUCHAR Buffer,
                                 ULONG BufferLength,
                                 PULONG LengthTransferred)
{
    if (LengthTransferred != NULL)
        *LengthTransferred = 0;
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_GetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                 UCHAR PipeID,
                                 ULONG PolicyType,
                                 PULONG ValueLength,
                                 PVOID Value)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_SetPipePolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                 UCHAR PipeID,
                                 ULONG PolicyType,
                                 ULONG ValueLength,
                                 PVOID Value)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_GetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                  ULONG PolicyType,
                                  PULONG ValueLength,
                                  PVOID Value)
{
    return WinUsbUnsupported();
}

BOOL WINAPI WinUsb_SetPowerPolicy(WINUSB_INTERFACE_HANDLE InterfaceHandle,
                                  ULONG PolicyType,
                                  ULONG ValueLength,
                                  PVOID Value)
{
    return WinUsbUnsupported();
}
