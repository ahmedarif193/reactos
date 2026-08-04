/*
 * PROJECT:     ReactOS TPM 2.0 Diagnostic Utility
 * LICENSE:     GPL-2.0-or-later
 */

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <wchar.h>
#include <initguid.h>
#include <reactos/drivers/tpm2.h>

#define TPM2DIAG_RESPONSE_SIZE 65536
#define TPM2DIAG_HEADER_SIZE 10

static
ULONG
Tpm2DiagReadBigEndian32(
    _In_reads_(4) const BYTE *Buffer)
{
    return ((ULONG)Buffer[0] << 24) | ((ULONG)Buffer[1] << 16) | ((ULONG)Buffer[2] << 8) | Buffer[3];
}

static
HANDLE
Tpm2DiagOpenDevice(VOID)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DetailData = NULL;
    HDEVINFO DeviceInfo;
    DWORD RequiredSize = 0;
    HANDLE Device = INVALID_HANDLE_VALUE;

    DeviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_REACTOS_TPM2, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DeviceInfo == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    ZeroMemory(&InterfaceData, sizeof(InterfaceData));
    InterfaceData.cbSize = sizeof(InterfaceData);
    if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, &GUID_DEVINTERFACE_REACTOS_TPM2, 0, &InterfaceData))
        goto Cleanup;
    SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, NULL, 0, &RequiredSize, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || RequiredSize < sizeof(*DetailData))
        goto Cleanup;
    DetailData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize);
    if (!DetailData)
        goto Cleanup;
    DetailData->cbSize = sizeof(*DetailData);
    if (!SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, DetailData, RequiredSize, NULL, NULL))
        goto Cleanup;
    Device = CreateFileW(DetailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

Cleanup:
    if (DetailData)
        HeapFree(GetProcessHeap(), 0, DetailData);
    SetupDiDestroyDeviceInfoList(DeviceInfo);
    return Device;
}

static
VOID
Tpm2DiagPrintManufacturer(
    _In_ ULONG Manufacturer)
{
    WCHAR Text[5];

    Text[0] = (WCHAR)((Manufacturer >> 24) & 0xFF);
    Text[1] = (WCHAR)((Manufacturer >> 16) & 0xFF);
    Text[2] = (WCHAR)((Manufacturer >> 8) & 0xFF);
    Text[3] = (WCHAR)(Manufacturer & 0xFF);
    Text[4] = UNICODE_NULL;
    wprintf(L"Manufacturer:  %ls (0x%08lx)\n", Text, Manufacturer);
}

static
BOOL
Tpm2DiagInfo(
    _In_ HANDLE Device)
{
    TPM2_DEVICE_INFORMATION Information;
    DWORD BytesReturned;

    ZeroMemory(&Information, sizeof(Information));
    if (!DeviceIoControl(Device, IOCTL_TPM2_QUERY_INFORMATION, NULL, 0, &Information, sizeof(Information), &BytesReturned, NULL))
    {
        wprintf(L"TPM2 query failed: %lu\n", GetLastError());
        return FALSE;
    }
    if (BytesReturned < sizeof(Information) || Information.Version != TPM2_DRIVER_INTERFACE_VERSION)
    {
        wprintf(L"TPM2 returned an invalid information block.\n");
        return FALSE;
    }
    wprintf(L"Interface:     %ls\n", Information.InterfaceType == TPM2_INTERFACE_CRB ? L"CRB" : Information.InterfaceType == TPM2_INTERFACE_TIS ? L"TIS/FIFO" : L"unknown");
    wprintf(L"Start method: %lu\n", Information.StartMethod);
    wprintf(L"Command max:  %lu bytes\n", Information.MaximumCommandSize);
    wprintf(L"Response max: %lu bytes\n", Information.MaximumResponseSize);
    Tpm2DiagPrintManufacturer(Information.Manufacturer);
    wprintf(L"Firmware:      %08lx.%08lx\n", Information.FirmwareVersion1, Information.FirmwareVersion2);
    return TRUE;
}

static
BOOL
Tpm2DiagSubmit(
    _In_ HANDLE Device,
    _In_reads_bytes_(CommandLength) const BYTE *Command,
    _In_ DWORD CommandLength,
    _Outptr_result_bytebuffer_(*ResponseLength) BYTE **Response,
    _Out_ PDWORD ResponseLength)
{
    BYTE *Buffer;
    BOOL Result;

    *Response = NULL;
    *ResponseLength = 0;
    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, TPM2DIAG_RESPONSE_SIZE);
    if (!Buffer)
        return FALSE;
    CopyMemory(Buffer, Command, CommandLength);
    Result = DeviceIoControl(Device, IOCTL_TPM2_SUBMIT_COMMAND, Buffer, CommandLength, Buffer, TPM2DIAG_RESPONSE_SIZE, ResponseLength, NULL);
    if (!Result)
    {
        wprintf(L"TPM2 command failed: %lu\n", GetLastError());
        HeapFree(GetProcessHeap(), 0, Buffer);
        return FALSE;
    }
    if (*ResponseLength < TPM2DIAG_HEADER_SIZE || Tpm2DiagReadBigEndian32(Buffer + 2) != *ResponseLength)
    {
        wprintf(L"TPM2 returned an invalid response.\n");
        HeapFree(GetProcessHeap(), 0, Buffer);
        return FALSE;
    }
    *Response = Buffer;
    return TRUE;
}

static
BOOL
Tpm2DiagGetRandom(
    _In_ HANDLE Device,
    _In_ ULONG Requested)
{
    BYTE Command[12] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x7B, 0, 0};
    BYTE *Response;
    DWORD ResponseLength;
    ULONG TpmResult;
    ULONG RandomLength;
    ULONG Index;

    if (Requested == 0 || Requested > 64)
    {
        wprintf(L"Random byte count must be between 1 and 64.\n");
        return FALSE;
    }
    Command[10] = (BYTE)(Requested >> 8);
    Command[11] = (BYTE)Requested;
    if (!Tpm2DiagSubmit(Device, Command, sizeof(Command), &Response, &ResponseLength))
        return FALSE;
    TpmResult = Tpm2DiagReadBigEndian32(Response + 6);
    if (TpmResult != 0)
    {
        wprintf(L"TPM2_GetRandom returned 0x%08lx\n", TpmResult);
        HeapFree(GetProcessHeap(), 0, Response);
        return FALSE;
    }
    if (ResponseLength < 12)
    {
        HeapFree(GetProcessHeap(), 0, Response);
        return FALSE;
    }
    RandomLength = ((ULONG)Response[10] << 8) | Response[11];
    if (RandomLength > ResponseLength - 12)
    {
        HeapFree(GetProcessHeap(), 0, Response);
        return FALSE;
    }
    wprintf(L"Random (%lu bytes): ", RandomLength);
    for (Index = 0; Index < RandomLength; Index++)
        wprintf(L"%02x", Response[12 + Index]);
    wprintf(L"\n");
    HeapFree(GetProcessHeap(), 0, Response);
    return TRUE;
}

static
BOOL
Tpm2DiagSelfTest(
    _In_ HANDLE Device,
    _In_ BOOL FullTest)
{
    BYTE Command[11] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x01, 0x43, 0};
    BYTE *Response;
    DWORD ResponseLength;
    ULONG TpmResult;

    Command[10] = FullTest ? 1 : 0;
    if (!Tpm2DiagSubmit(Device, Command, sizeof(Command), &Response, &ResponseLength))
        return FALSE;
    TpmResult = Tpm2DiagReadBigEndian32(Response + 6);
    HeapFree(GetProcessHeap(), 0, Response);
    wprintf(L"TPM2_SelfTest returned 0x%08lx\n", TpmResult);
    return TpmResult == 0;
}

static
INT
Tpm2DiagHexDigit(
    _In_ WCHAR Character)
{
    if (Character >= L'0' && Character <= L'9')
        return Character - L'0';
    if (Character >= L'a' && Character <= L'f')
        return Character - L'a' + 10;
    if (Character >= L'A' && Character <= L'F')
        return Character - L'A' + 10;
    return -1;
}

static
BOOL
Tpm2DiagRaw(
    _In_ HANDLE Device,
    _In_ PCWSTR HexCommand)
{
    BYTE *Command;
    BYTE *Response;
    SIZE_T CharacterCount = wcslen(HexCommand);
    DWORD CommandLength;
    DWORD ResponseLength;
    DWORD Index;
    INT High;
    INT Low;

    if (CharacterCount < TPM2DIAG_HEADER_SIZE * 2 || (CharacterCount & 1) != 0 || CharacterCount / 2 > TPM2DIAG_RESPONSE_SIZE)
    {
        wprintf(L"Raw command must be an even-length hex TPM packet.\n");
        return FALSE;
    }
    CommandLength = (DWORD)(CharacterCount / 2);
    Command = HeapAlloc(GetProcessHeap(), 0, CommandLength);
    if (!Command)
        return FALSE;
    for (Index = 0; Index < CommandLength; Index++)
    {
        High = Tpm2DiagHexDigit(HexCommand[Index * 2]);
        Low = Tpm2DiagHexDigit(HexCommand[Index * 2 + 1]);
        if (High < 0 || Low < 0)
        {
            HeapFree(GetProcessHeap(), 0, Command);
            wprintf(L"Raw command contains a non-hex character.\n");
            return FALSE;
        }
        Command[Index] = (BYTE)((High << 4) | Low);
    }
    if (!Tpm2DiagSubmit(Device, Command, CommandLength, &Response, &ResponseLength))
    {
        HeapFree(GetProcessHeap(), 0, Command);
        return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, Command);
    wprintf(L"Response: ");
    for (Index = 0; Index < ResponseLength; Index++)
        wprintf(L"%02x", Response[Index]);
    wprintf(L"\n");
    HeapFree(GetProcessHeap(), 0, Response);
    return TRUE;
}

static
VOID
Tpm2DiagUsage(VOID)
{
    wprintf(L"Usage: tpm2diag [info | probe | random <1-64> | selftest [full] | raw <hex>]\n");
}

INT
wmain(
    _In_ INT argc,
    _In_reads_(argc) WCHAR **argv)
{
    HANDLE Device;
    BOOL Result = FALSE;
    ULONG Requested;

    Device = Tpm2DiagOpenDevice();
    if (Device == INVALID_HANDLE_VALUE)
    {
        wprintf(L"TPM 2.0 device is unavailable (error %lu).\n", GetLastError());
        return 1;
    }
    if (argc == 1 || _wcsicmp(argv[1], L"info") == 0)
        Result = Tpm2DiagInfo(Device);
    else if (_wcsicmp(argv[1], L"probe") == 0)
        Result = Tpm2DiagInfo(Device) && Tpm2DiagGetRandom(Device, 16);
    else if (_wcsicmp(argv[1], L"random") == 0 && argc == 3)
    {
        Requested = wcstoul(argv[2], NULL, 0);
        Result = Tpm2DiagGetRandom(Device, Requested);
    }
    else if (_wcsicmp(argv[1], L"selftest") == 0 && argc <= 3)
        Result = Tpm2DiagSelfTest(Device, argc == 3 && _wcsicmp(argv[2], L"full") == 0);
    else if (_wcsicmp(argv[1], L"raw") == 0 && argc == 3)
        Result = Tpm2DiagRaw(Device, argv[2]);
    else
        Tpm2DiagUsage();
    CloseHandle(Device);
    return Result ? 0 : 1;
}
