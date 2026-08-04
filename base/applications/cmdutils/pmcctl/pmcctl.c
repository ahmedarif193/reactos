/*
 * PROJECT:     ReactOS Intel PMC Diagnostic Utility
 * LICENSE:     GPL-2.0-or-later
 */

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <initguid.h>
#include <reactos/drivers/intelpmc.h>

static
HANDLE
PmcCtlOpenDevice(VOID)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DetailData = NULL;
    HDEVINFO DeviceInfo;
    DWORD RequiredSize = 0;
    HANDLE Device = INVALID_HANDLE_VALUE;

    DeviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_REACTOS_INTEL_PMC, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DeviceInfo == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    ZeroMemory(&InterfaceData, sizeof(InterfaceData));
    InterfaceData.cbSize = sizeof(InterfaceData);
    if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, &GUID_DEVINTERFACE_REACTOS_INTEL_PMC, 0, &InterfaceData))
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
    Device = CreateFileW(DetailData->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

Cleanup:
    if (DetailData)
        HeapFree(GetProcessHeap(), 0, DetailData);
    SetupDiDestroyDeviceInfoList(DeviceInfo);
    return Device;
}

static
BOOL
PmcCtlQuery(
    _In_ HANDLE Device,
    _Out_ PINTELPMC_INFORMATION Information)
{
    DWORD BytesReturned;

    ZeroMemory(Information, sizeof(*Information));
    if (!DeviceIoControl(Device, IOCTL_INTELPMC_QUERY_INFORMATION, NULL, 0, Information, sizeof(*Information), &BytesReturned, NULL))
        return FALSE;
    return BytesReturned == sizeof(*Information) && Information->Version == INTELPMC_INTERFACE_VERSION;
}

static
VOID
PmcCtlPrintStatus(
    _In_ PINTELPMC_INFORMATION Information)
{
    ULONGLONG Microseconds;
    ULONG Index;

    Microseconds = (ULONGLONG)Information->SlpS0Residency * 122;
    wprintf(L"CPU:          Intel family 6 model 0x%02lx, %lu active processors (SMP)\n", Information->ProcessorModel, Information->ActiveProcessors);
    wprintf(L"PMC base:     0x%I64x (%ls)\n", Information->PhysicalBase, (Information->Flags & INTELPMC_FLAG_LPIT_BASE) ? L"LPIT" : L"platform default");
    wprintf(L"SLP_S0:       %lu ticks (~%I64u ms)\n", Information->SlpS0Residency, Microseconds / 1000);
    wprintf(L"PM_CFG:       0x%08lx (%ls)\n", Information->PmConfiguration, (Information->Flags & INTELPMC_FLAG_READ_DISABLED) ? L"PMC reads disabled" : L"PMC reads enabled");
    wprintf(L"LTR_IGNORE:   0x%08lx\n", Information->LtrIgnore);
    wprintf(L"LPM:          enable=0x%08lx priority=0x%08lx sleep-prepared=%ls\n", Information->LpmEnable, Information->LpmPriority, (Information->Flags & INTELPMC_FLAG_SLEEP_PREPARED) ? L"yes" : L"no");
    for (Index = 0; Index < INTELPMC_LPM_MODE_COUNT; Index++)
    {
        Microseconds = (ULONGLONG)Information->LpmResidency[Index] * 61 / 2;
        wprintf(L"LPM%lu:         %lu ticks (~%I64u us)\n", Index, Information->LpmResidency[Index], Microseconds);
    }
    for (Index = 0; Index < INTELPMC_LPM_MAP_COUNT; Index++)
        wprintf(L"Map %lu:        status=0x%08lx live=0x%08lx\n", Index, Information->LpmStatus[Index], Information->LpmLiveStatus[Index]);
    wprintf(L"Power gates:  ");
    for (Index = 0; Index < INTELPMC_PPFEAR_COUNT; Index++)
        wprintf(L"%02x%ls", Information->PowerGatingStatus[Index], Index + 1 == INTELPMC_PPFEAR_COUNT ? L"\n" : L" ");
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) WCHAR **argv)
{
    INTELPMC_INFORMATION Information;
    HANDLE Device;
    BOOL Success;

    if (argc != 1 && (argc != 2 || _wcsicmp(argv[1], L"status") != 0))
    {
        wprintf(L"Usage: pmcctl [status]\n");
        return 2;
    }
    Device = PmcCtlOpenDevice();
    if (Device == INVALID_HANDLE_VALUE)
    {
        wprintf(L"Intel PMC device is unavailable: %lu\n", GetLastError());
        return 1;
    }
    Success = PmcCtlQuery(Device, &Information);
    if (Success)
        PmcCtlPrintStatus(&Information);
    else
        wprintf(L"Intel PMC query failed: %lu\n", GetLastError());
    CloseHandle(Device);
    return Success ? 0 : 1;
}
