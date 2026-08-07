/*
 * PROJECT:     ReactOS CSPRNG Library
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     User-mode interface to the kernel random number generator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ksecioctl.h>
#include <csprng.h>

static HANDLE CsprngDeviceHandle;

static
NTSTATUS
CsprngOpenDevice(VOID)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KsecDD");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE DeviceHandle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&DeviceHandle, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (InterlockedCompareExchangePointer(&CsprngDeviceHandle, DeviceHandle, NULL) != NULL)
    {
        NtClose(DeviceHandle);
    }

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
RosCsprngFill(
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ SIZE_T Length)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    if (Length == 0)
    {
        return TRUE;
    }

    if ((Buffer == NULL) || (Length > MAXULONG))
    {
        return FALSE;
    }

    if (CsprngDeviceHandle == NULL)
    {
        Status = CsprngOpenDevice();
        if (!NT_SUCCESS(Status))
        {
            return FALSE;
        }
    }

    Status = NtDeviceIoControlFile(CsprngDeviceHandle, NULL, NULL, NULL, &IoStatusBlock, IOCTL_KSEC_RANDOM_FILL_BUFFER, NULL, 0, Buffer, (ULONG)Length);
    return NT_SUCCESS(Status) && (IoStatusBlock.Information == Length);
}
