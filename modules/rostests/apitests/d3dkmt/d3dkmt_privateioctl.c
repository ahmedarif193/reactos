/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private dxgkrnl IOCTL user-mode isolation tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"
#include <winioctl.h>

#define IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY 0x23003F
#define IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY 0x230043
#define IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY 0x230047
#define IOCTL_DXGKRNL_GET_UNINIT_ENTRY 0x23004B
#define IOCTL_DXGKRNL_EXCHANGE_INTERFACE CTL_CODE(0x23, 0x200, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETDISPLAYMODELIST CTL_CODE(0x23, 0x105, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYRESOURCEINFO CTL_CODE(0x23, 0x134, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENRESOURCE CTL_CODE(0x23, 0x135, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_PRESENT CTL_CODE(0x23, 0x141, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DXGKRNL_INTERFACE_VERSION_1 1
#define PRIVATE_INTERFACE_BUFFER_SIZE 4096

typedef struct _PRIVATE_INTERFACE_EXCHANGE_IN
{
    ULONG Version;
    ULONG Size;
} PRIVATE_INTERFACE_EXCHANGE_IN;

static void
TestResolverIoctl(
    HANDLE Device,
    DWORD IoControlCode,
    const char *Name)
{
    ULONG_PTR OutputCanary;
    ULONG_PTR ExpectedCanary;
    DWORD BytesReturned;
    DWORD Error;
    BOOL Success;

    OutputCanary = (ULONG_PTR)0xA55AA55AUL;
    ExpectedCanary = OutputCanary;
    BytesReturned = 0xFFFFFFFF;
    SetLastError(ERROR_SUCCESS);
    Success = DeviceIoControl(Device, IoControlCode, NULL, 0, &OutputCanary, sizeof(OutputCanary), &BytesReturned, NULL);
    Error = GetLastError();

    ok(!Success, "%s must not succeed from user-mode DEVICE_CONTROL (error %lu, bytes %lu)\n", Name, Error, BytesReturned);
    ok(OutputCanary == ExpectedCanary, "%s changed the output canary from %p to %p\n", Name, (PVOID)ExpectedCanary, (PVOID)OutputCanary);
}

static void
TestExchangeInterfaceIoctl(HANDLE Device)
{
    PRIVATE_INTERFACE_EXCHANGE_IN Input;
    UCHAR OutputCanary[PRIVATE_INTERFACE_BUFFER_SIZE];
    UCHAR ExpectedCanary[PRIVATE_INTERFACE_BUFFER_SIZE];
    DWORD BytesReturned;
    DWORD Error;
    BOOL Success;

    Input.Version = DXGKRNL_INTERFACE_VERSION_1;
    Input.Size = sizeof(OutputCanary);
    memset(OutputCanary, 0xA5, sizeof(OutputCanary));
    memcpy(ExpectedCanary, OutputCanary, sizeof(ExpectedCanary));
    BytesReturned = 0xFFFFFFFF;
    SetLastError(ERROR_SUCCESS);
    Success = DeviceIoControl(Device, IOCTL_DXGKRNL_EXCHANGE_INTERFACE, &Input, sizeof(Input), OutputCanary, sizeof(OutputCanary), &BytesReturned, NULL);
    Error = GetLastError();

    ok(!Success, "IOCTL_DXGKRNL_EXCHANGE_INTERFACE must not succeed from user-mode DEVICE_CONTROL (error %lu, bytes %lu)\n", Error, BytesReturned);
    ok(memcmp(OutputCanary, ExpectedCanary, sizeof(OutputCanary)) == 0, "IOCTL_DXGKRNL_EXCHANGE_INTERFACE changed the output canary\n");
}

static void
TestD3dkmtIoctlDenied(
    HANDLE Device,
    DWORD IoControlCode,
    const void *Input,
    DWORD InputSize,
    const char *Name)
{
    UCHAR OutputCanary[PRIVATE_INTERFACE_BUFFER_SIZE];
    UCHAR ExpectedCanary[PRIVATE_INTERFACE_BUFFER_SIZE];
    DWORD BytesReturned;
    DWORD Error;
    BOOL Success;

    ok(InputSize <= sizeof(OutputCanary), "%s input is too large for the isolation canary (%lu)\n", Name, InputSize);
    if (InputSize > sizeof(OutputCanary))
        return;
    memset(OutputCanary, 0xA5, sizeof(OutputCanary));
    memcpy(ExpectedCanary, OutputCanary, sizeof(ExpectedCanary));
    BytesReturned = 0xFFFFFFFF;
    SetLastError(ERROR_SUCCESS);
    Success = DeviceIoControl(Device, IoControlCode, (PVOID)Input, InputSize, OutputCanary, InputSize, &BytesReturned, NULL);
    Error = GetLastError();

    ok(!Success, "%s must not bypass the win32k D3DKMT capture boundary (error %lu, bytes %lu)\n", Name, Error, BytesReturned);
    ok(Error == ERROR_ACCESS_DENIED, "%s returned error %lu instead of ERROR_ACCESS_DENIED\n", Name, Error);
    ok(BytesReturned == 0, "%s returned %lu output bytes on denial\n", Name, BytesReturned);
    ok(memcmp(OutputCanary, ExpectedCanary, sizeof(OutputCanary)) == 0, "%s changed the output canary\n", Name);
}

START_TEST(privateioctl)
{
    D3DKMT_GETDISPLAYMODELIST ModeList;
    D3DKMT_QUERYRESOURCEINFO ResourceInfo;
    D3DKMT_OPENRESOURCE OpenResource;
    D3DKMT_PRESENT Present;
    HANDLE Device;

    Device = CreateFileW(L"\\\\.\\DxgKrnl", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Device == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open \\\\.\\DxgKrnl for private IOCTL isolation tests (error %lu)\n", GetLastError());
        return;
    }

    TestResolverIoctl(Device, IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY, "IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY (0x23003F)");
    TestResolverIoctl(Device, IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY, "IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY (0x230043)");
    TestResolverIoctl(Device, IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY, "IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY (0x230047)");
    TestResolverIoctl(Device, IOCTL_DXGKRNL_GET_UNINIT_ENTRY, "IOCTL_DXGKRNL_GET_UNINIT_ENTRY (0x23004B)");
    TestExchangeInterfaceIoctl(Device);
    memset(&ModeList, 0, sizeof(ModeList));
    ModeList.pModeList = (D3DKMT_DISPLAYMODE *)(ULONG_PTR)-4096;
    ModeList.ModeCount = 1;
    TestD3dkmtIoctlDenied(Device, IOCTL_D3DKMT_GETDISPLAYMODELIST, &ModeList, sizeof(ModeList), "IOCTL_D3DKMT_GETDISPLAYMODELIST");
    memset(&ResourceInfo, 0, sizeof(ResourceInfo));
    ResourceInfo.pPrivateRuntimeData = (PVOID)(ULONG_PTR)-4096;
    ResourceInfo.PrivateRuntimeDataSize = sizeof(ULONG);
    TestD3dkmtIoctlDenied(Device, IOCTL_D3DKMT_QUERYRESOURCEINFO, &ResourceInfo, sizeof(ResourceInfo), "IOCTL_D3DKMT_QUERYRESOURCEINFO");
    memset(&OpenResource, 0, sizeof(OpenResource));
    OpenResource.NumAllocations = 1;
    OpenResource.pOpenAllocationInfo2 = (D3DDDI_OPENALLOCATIONINFO2 *)(ULONG_PTR)-4096;
    OpenResource.pPrivateRuntimeData = (PVOID)(ULONG_PTR)-4096;
    OpenResource.PrivateRuntimeDataSize = sizeof(ULONG);
    TestD3dkmtIoctlDenied(Device, IOCTL_D3DKMT_OPENRESOURCE, &OpenResource, sizeof(OpenResource), "IOCTL_D3DKMT_OPENRESOURCE");
    memset(&Present, 0, sizeof(Present));
    Present.Flags.ColorFill = 1;
    TestD3dkmtIoctlDenied(Device, IOCTL_D3DKMT_PRESENT, &Present, sizeof(Present), "IOCTL_D3DKMT_PRESENT");
    CloseHandle(Device);
}
