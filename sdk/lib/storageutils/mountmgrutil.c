/*
 * PROJECT:     ReactOS Storage Utilities
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared Mount Manager helpers for drive-letter assignment/removal.
 */

#include "mountmgrutil.h"

#include <winbase.h>
#include <winioctl.h>
#include <strsafe.h>
#include <mountmgr.h>
#include <wctype.h>

static HANDLE
StorageUtilOpenMountManager(VOID)
{
    return CreateFileW(MOUNTMGR_DOS_DEVICE_NAME,
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
}

BOOL
StorageUtilAssignDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _In_ WCHAR DriveLetter)
{
    WCHAR DosDeviceName[30];
    ULONG DosDeviceNameLength;
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    PMOUNTMGR_CREATE_POINT_INPUT InputBuffer;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL || DriveLetter == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    StringCchPrintfW(DosDeviceName, ARRAYSIZE(DosDeviceName), L"\\DosDevices\\%c:", towupper(DriveLetter));
    DosDeviceNameLength = (ULONG)(wcslen(DosDeviceName) * sizeof(WCHAR));
    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));

    InputBufferLength = DosDeviceNameLength + DeviceNameLength + sizeof(MOUNTMGR_CREATE_POINT_INPUT);
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    if (InputBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->SymbolicLinkNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT);
    InputBuffer->SymbolicLinkNameLength = DosDeviceNameLength;
    InputBuffer->DeviceNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT) + DosDeviceNameLength;
    InputBuffer->DeviceNameLength = DeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->SymbolicLinkNameOffset,
               DosDeviceName,
               DosDeviceNameLength);
    CopyMemory((PBYTE)InputBuffer + InputBuffer->DeviceNameOffset,
               DeviceName,
               DeviceNameLength);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_CREATE_POINT,
                              InputBuffer,
                              InputBufferLength,
                              NULL,
                              0,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    return Success;
}

BOOL
StorageUtilAssignNextDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _Out_ PWCHAR DriveLetter)
{
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    PMOUNTMGR_DRIVE_LETTER_TARGET InputBuffer;
    MOUNTMGR_DRIVE_LETTER_INFORMATION LetterInfo;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL || DriveLetter == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *DriveLetter = UNICODE_NULL;
    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));
    InputBufferLength = DeviceNameLength + FIELD_OFFSET(MOUNTMGR_DRIVE_LETTER_TARGET, DeviceName);
    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    if (InputBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->DeviceNameLength = DeviceNameLength;
    CopyMemory(InputBuffer->DeviceName, DeviceName, DeviceNameLength);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        return FALSE;
    }

    ZeroMemory(&LetterInfo, sizeof(LetterInfo));
    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_NEXT_DRIVE_LETTER,
                              InputBuffer,
                              InputBufferLength,
                              &LetterInfo,
                              sizeof(LetterInfo),
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    if (!Success)
        return FALSE;

    if (LetterInfo.DriveLetterWasAssigned)
        *DriveLetter = LetterInfo.CurrentDriveLetter;

    return TRUE;
}

BOOL
StorageUtilGetDriveLetter(
    _In_z_ PCWSTR DeviceName,
    _Out_ PWCHAR DriveLetter)
{
    ULONG DeviceNameLength;
    ULONG InputBufferLength;
    ULONG OutputBufferLength;
    PMOUNTMGR_MOUNT_POINT InputBuffer;
    PMOUNTMGR_MOUNT_POINTS OutputBuffer;
    PMOUNTMGR_MOUNT_POINT Point;
    PWCHAR LinkName;
    ULONG LinkChars;
    ULONG Index;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DeviceName == NULL || DriveLetter == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *DriveLetter = UNICODE_NULL;
    DeviceNameLength = (ULONG)(wcslen(DeviceName) * sizeof(WCHAR));
    InputBufferLength = sizeof(MOUNTMGR_MOUNT_POINT) + DeviceNameLength;
    OutputBufferLength = 0x1000;

    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    OutputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, OutputBufferLength);
    if (InputBuffer == NULL || OutputBuffer == NULL)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->DeviceNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    InputBuffer->DeviceNameLength = (USHORT)DeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->DeviceNameOffset, DeviceName, DeviceNameLength);

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_QUERY_POINTS,
                              InputBuffer,
                              InputBufferLength,
                              OutputBuffer,
                              OutputBufferLength,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);

    if (Success)
    {
        for (Index = 0; Index < OutputBuffer->NumberOfMountPoints; Index++)
        {
            Point = &OutputBuffer->MountPoints[Index];
            LinkName = (PWCHAR)((PBYTE)OutputBuffer + Point->SymbolicLinkNameOffset);
            LinkChars = Point->SymbolicLinkNameLength / sizeof(WCHAR);

            /* Mount manager writes drive letters exactly as \DosDevices\X: */
            if ((LinkChars == 14) && (wcsncmp(LinkName, L"\\DosDevices\\", 12) == 0) && (LinkName[13] == L':'))
            {
                *DriveLetter = towupper(LinkName[12]);
                break;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, InputBuffer);
    HeapFree(GetProcessHeap(), 0, OutputBuffer);
    return Success;
}

BOOL
StorageUtilDeleteDriveLetter(
    _In_ WCHAR DriveLetter)
{
    PMOUNTMGR_MOUNT_POINT InputBuffer;
    PMOUNTMGR_MOUNT_POINTS OutputBuffer;
    WCHAR DosDeviceName[30];
    ULONG DosDeviceNameLength;
    ULONG InputBufferLength;
    ULONG OutputBufferLength;
    HANDLE MountMgrHandle;
    DWORD BytesReturned;
    BOOL Success;

    if (DriveLetter == UNICODE_NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    StringCchPrintfW(DosDeviceName, ARRAYSIZE(DosDeviceName), L"\\DosDevices\\%c:", towupper(DriveLetter));
    DosDeviceNameLength = (ULONG)(wcslen(DosDeviceName) * sizeof(WCHAR));
    InputBufferLength = sizeof(MOUNTMGR_MOUNT_POINT) + DosDeviceNameLength;
    OutputBufferLength = 0x1000;

    InputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InputBufferLength);
    OutputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, OutputBufferLength);
    if (InputBuffer == NULL || OutputBuffer == NULL)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    InputBuffer->SymbolicLinkNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    InputBuffer->SymbolicLinkNameLength = DosDeviceNameLength;
    CopyMemory((PBYTE)InputBuffer + InputBuffer->SymbolicLinkNameOffset,
               DosDeviceName,
               DosDeviceNameLength);
    OutputBuffer->Size = OutputBufferLength;

    MountMgrHandle = StorageUtilOpenMountManager();
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        HeapFree(GetProcessHeap(), 0, InputBuffer);
        HeapFree(GetProcessHeap(), 0, OutputBuffer);
        return FALSE;
    }

    BytesReturned = 0;
    Success = DeviceIoControl(MountMgrHandle,
                              IOCTL_MOUNTMGR_DELETE_POINTS,
                              InputBuffer,
                              InputBufferLength,
                              OutputBuffer,
                              OutputBufferLength,
                              &BytesReturned,
                              NULL);
    CloseHandle(MountMgrHandle);
    HeapFree(GetProcessHeap(), 0, InputBuffer);
    HeapFree(GetProcessHeap(), 0, OutputBuffer);
    return Success;
}
