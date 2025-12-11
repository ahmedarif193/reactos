/*
 * PROJECT:         ReactOS Kernel – ARM64 bring-up scaffolding
 * LICENSE:         GPL-2.0-or-later
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include <usbioctl.h>

static VOID
PrintProtocols(const USB_PROTOCOLS *Protocols)
{
    printf("    Protocols:");
    if (Protocols->Usb110) printf(" USB1.1");
    if (Protocols->Usb200) printf(" USB2.0");
    if (Protocols->Usb300) printf(" USB3.x");
    if (!Protocols->Usb110 && !Protocols->Usb200 && !Protocols->Usb300)
        printf(" (none)");
    printf("\n");
}

static VOID
PrintFlags(const USB_NODE_CONNECTION_INFORMATION_EX_V2_FLAGS *Flags)
{
    printf("    Flags:");
    if (Flags->DeviceIsOperatingAtSuperSpeedOrHigher)
        printf(" OperatingAtSS+");
    if (Flags->DeviceIsSuperSpeedCapableOrHigher)
        printf(" SSCapable+");
    if (Flags->DeviceIsOperatingAtSuperSpeedPlusOrHigher)
        printf(" OperatingAtSSP+");
    if (Flags->DeviceIsSuperSpeedPlusCapableOrHigher)
        printf(" SSPCapable+");
    if (!Flags->ul)
        printf(" (none)");
    printf("\n");
}

static const char *
SpeedToString(UCHAR Speed)
{
    switch (Speed)
    {
        case 0: return "Unknown";
        case 1: return "LowSpeed";
        case 2: return "FullSpeed";
        case 3: return "HighSpeed";
        case 4: return "SuperSpeed";
        default: return "Reserved";
    }
}

static const char *
ConnectionStatusToString(USB_CONNECTION_STATUS Status)
{
    switch (Status)
    {
        case NoDeviceConnected: return "NoDevice";
        case DeviceConnected: return "Connected";
        case DeviceFailedEnumeration: return "FailedEnumeration";
        case DeviceGeneralFailure: return "GeneralFailure";
        case DeviceCausedOvercurrent: return "Overcurrent";
        case DeviceNotEnoughPower: return "NotEnoughPower";
        case DeviceNotEnoughBandwidth: return "NotEnoughBandwidth";
        case DeviceHubNestedTooDeeply: return "HubNestedTooDeeply";
        case DeviceInLegacyHub: return "InLegacyHub";
        case DeviceEnumerating: return "Enumerating";
        case DeviceReset: return "Reset";
        default: return "UnknownStatus";
    }
}

static BOOL
QueryConnectionInfoEx(
    _In_ HANDLE HubHandle,
    _In_ ULONG ConnectionIndex,
    _Out_ PUSB_NODE_CONNECTION_INFORMATION_EX *OutInfo)
{
    PUSB_NODE_CONNECTION_INFORMATION_EX Info;
    DWORD BytesReturned;
    BOOL Result;
    ULONG PipeCount = 32;
    ULONG Size;

    *OutInfo = NULL;

    Size = sizeof(USB_NODE_CONNECTION_INFORMATION_EX) +
           PipeCount * sizeof(USB_PIPE_INFO);

    Info = (PUSB_NODE_CONNECTION_INFORMATION_EX)HeapAlloc(GetProcessHeap(),
                                                          HEAP_ZERO_MEMORY,
                                                          Size);
    if (!Info)
        return FALSE;

    Info->ConnectionIndex = ConnectionIndex;

    Result = DeviceIoControl(HubHandle,
                             IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                             Info,
                             Size,
                             Info,
                             Size,
                             &BytesReturned,
                             NULL);
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, Info);
        return FALSE;
    }

    *OutInfo = Info;
    return TRUE;
}

static BOOL
QueryConnectionInfoExV2(
    _In_ HANDLE HubHandle,
    _In_ ULONG ConnectionIndex,
    _Out_ PUSB_NODE_CONNECTION_INFORMATION_EX_V2 Info)
{
    DWORD BytesReturned;
    BOOL Result;

    ZeroMemory(Info, sizeof(*Info));
    Info->ConnectionIndex = ConnectionIndex;
    Info->Length = sizeof(*Info);
    Info->SupportedUsbProtocols.ul = 0;
    Info->Flags.ul = 0;

    Result = DeviceIoControl(HubHandle,
                             IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2,
                             Info,
                             sizeof(*Info),
                             Info,
                             sizeof(*Info),
                             &BytesReturned,
                             NULL);
    return Result;
}

static VOID
ShowPortInfo(
    _In_ HANDLE HubHandle,
    _In_ ULONG PortIndex)
{
    USB_NODE_CONNECTION_INFORMATION_EX_V2 InfoV2;
    PUSB_NODE_CONNECTION_INFORMATION_EX InfoEx = NULL;
    BOOL HasV2;

    HasV2 = QueryConnectionInfoExV2(HubHandle, PortIndex, &InfoV2);
    if (!QueryConnectionInfoEx(HubHandle, PortIndex, &InfoEx))
    {
        if (!HasV2)
        {
            printf("  Port %lu: unable to query connection information (error %lu)\n",
                   PortIndex,
                   GetLastError());
        }
        else
        {
            printf("  Port %lu: connection info v2 only\n", PortIndex);
            PrintProtocols(&InfoV2.SupportedUsbProtocols);
            PrintFlags(&InfoV2.Flags);
        }
        return;
    }

    printf("  Port %lu: %s, %s",
           PortIndex,
           ConnectionStatusToString(InfoEx->ConnectionStatus),
           SpeedToString(InfoEx->Speed));

    if (InfoEx->ConnectionStatus == DeviceConnected)
    {
        USHORT bcdUSB = InfoEx->DeviceDescriptor.bcdUSB;
        printf(", bcdUSB %x.%02x",
               bcdUSB >> 8,
               bcdUSB & 0xFF);
        if (InfoEx->DeviceIsHub)
            printf(", Hub");
    }
    printf("\n");

    if (HasV2)
    {
        PrintProtocols(&InfoV2.SupportedUsbProtocols);
        PrintFlags(&InfoV2.Flags);
    }

    HeapFree(GetProcessHeap(), 0, InfoEx);
}

static BOOL
OpenRootHubFromController(
    _In_ HANDLE ControllerHandle,
    _Out_ HANDLE *RootHubHandle)
{
    PUSB_ROOT_HUB_NAME RootHubName;
    DWORD BytesReturned;
    BOOL Result;
    ULONG Size;
    WCHAR DevicePath[MAX_PATH];

    *RootHubHandle = INVALID_HANDLE_VALUE;

    Size = sizeof(USB_ROOT_HUB_NAME) + 256 * sizeof(WCHAR);
    RootHubName = (PUSB_ROOT_HUB_NAME)HeapAlloc(GetProcessHeap(),
                                                HEAP_ZERO_MEMORY,
                                                Size);
    if (!RootHubName)
        return FALSE;

    Result = DeviceIoControl(ControllerHandle,
                             IOCTL_USB_GET_ROOT_HUB_NAME,
                             RootHubName,
                             Size,
                             RootHubName,
                             Size,
                             &BytesReturned,
                             NULL);
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, RootHubName);
        return FALSE;
    }

    if (RootHubName->ActualLength < sizeof(ULONG) + sizeof(WCHAR))
    {
        HeapFree(GetProcessHeap(), 0, RootHubName);
        return FALSE;
    }

    _snwprintf(DevicePath,
               sizeof(DevicePath) / sizeof(DevicePath[0]),
               L"\\\\.\\%s",
               RootHubName->RootHubName);
    DevicePath[(sizeof(DevicePath) / sizeof(DevicePath[0])) - 1] = L'\0';

    HeapFree(GetProcessHeap(), 0, RootHubName);

    *RootHubHandle = CreateFileW(DevicePath,
                                 GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 0,
                                 NULL);
    return (*RootHubHandle != INVALID_HANDLE_VALUE);
}

static BOOL
QueryHubNodeInfo(
    _In_ HANDLE HubHandle,
    _Out_ PUSB_NODE_INFORMATION NodeInfo)
{
    DWORD BytesReturned;

    ZeroMemory(NodeInfo, sizeof(*NodeInfo));
    NodeInfo->NodeType = UsbHub;

    return DeviceIoControl(HubHandle,
                           IOCTL_USB_GET_NODE_INFORMATION,
                           NodeInfo,
                           sizeof(*NodeInfo),
                           NodeInfo,
                           sizeof(*NodeInfo),
                           &BytesReturned,
                           NULL);
}

static VOID
EnumerateRootHub(
    _In_ HANDLE HubHandle)
{
    USB_NODE_INFORMATION NodeInfo;
    UCHAR Port;

    if (!QueryHubNodeInfo(HubHandle, &NodeInfo))
    {
        printf("  Failed to query root hub information (error %lu)\n",
               GetLastError());
        return;
    }

    printf("  Root hub: %u ports, %s-powered\n",
           NodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts,
           NodeInfo.u.HubInformation.HubIsBusPowered ? "bus" : "self");

    for (Port = 1; Port <= NodeInfo.u.HubInformation.HubDescriptor.bNumberOfPorts; Port++)
    {
        ShowPortInfo(HubHandle, Port);
    }
}

static VOID
EnumerateControllers(VOID)
{
    WCHAR DevicePath[32];
    ULONG Index;
    BOOL FoundAny = FALSE;

    for (Index = 0; Index < 32; Index++)
    {
        HANDLE ControllerHandle;
        HANDLE RootHubHandle;

        _snwprintf(DevicePath,
                   sizeof(DevicePath) / sizeof(DevicePath[0]),
                   L"\\\\.\\HCD%lu",
                   Index);
        DevicePath[(sizeof(DevicePath) / sizeof(DevicePath[0])) - 1] = L'\0';

        ControllerHandle = CreateFileW(DevicePath,
                                       GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL,
                                       OPEN_EXISTING,
                                       0,
                                       NULL);
        if (ControllerHandle == INVALID_HANDLE_VALUE)
            continue;

        FoundAny = TRUE;
        printf("Host controller %lu (%S):\n", Index, DevicePath);

        if (OpenRootHubFromController(ControllerHandle, &RootHubHandle))
        {
            EnumerateRootHub(RootHubHandle);
            CloseHandle(RootHubHandle);
        }
        else
        {
            printf("  Failed to open root hub (error %lu)\n", GetLastError());
        }

        CloseHandle(ControllerHandle);
        printf("\n");
    }

    if (!FoundAny)
        fprintf(stderr, "usb3info: no host controllers found (HCD0..HCD31)\n");
}

static VOID
PrintUsage(VOID)
{
    fprintf(stderr,
            "Usage:\n"
            "  usb3info               Enumerate host controllers and root-hub ports\n"
            "  usb3info <hub> <port>  Query a specific hub path and 1-based port\n");
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 1)
    {
        EnumerateControllers();
        return 0;
    }
    else if (argc == 3)
    {
        HANDLE HubHandle;
        ULONG PortIndex;

        PortIndex = (ULONG)_wtoi(argv[2]);
        if (PortIndex == 0)
        {
            fprintf(stderr, "usb3info: invalid port index '%S'\n", argv[2]);
            return 1;
        }

        HubHandle = CreateFileW(argv[1],
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                0,
                                NULL);
        if (HubHandle == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "usb3info: failed to open '%S' (error %lu)\n",
                    argv[1],
                    GetLastError());
            return 1;
        }

        printf("Hub '%S':\n", argv[1]);
        ShowPortInfo(HubHandle, PortIndex);
        CloseHandle(HubHandle);
        return 0;
    }

    PrintUsage();
    return 1;
}
