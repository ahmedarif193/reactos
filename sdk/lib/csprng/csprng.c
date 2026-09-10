/*
 * PROJECT:     ReactOS CSPRNG Library
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Kernel-seeded process random number generator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ksecioctl.h>
#include <csprng.h>
#include <mbedtls/chacha20.h>

#define CSPRNG_SEED_SIZE (32 + 12)
#define CSPRNG_BLOCK_SIZE 1024

static RTL_SRWLOCK CsprngLock;
static BOOLEAN CsprngInitialized;
static UCHAR CsprngSeed[CSPRNG_SEED_SIZE];

static
BOOLEAN
CsprngInitialize(VOID)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KsecDD");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock = {0};
    HANDLE DeviceHandle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&DeviceHandle, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }

    Status = NtDeviceIoControlFile(DeviceHandle, NULL, NULL, NULL, &IoStatusBlock, IOCTL_KSEC_RANDOM_FILL_BUFFER, NULL, 0, CsprngSeed, sizeof(CsprngSeed));
    NtClose(DeviceHandle);
    if (Status != STATUS_SUCCESS || IoStatusBlock.Status != STATUS_SUCCESS || IoStatusBlock.Information != sizeof(CsprngSeed))
    {
        RtlSecureZeroMemory(CsprngSeed, sizeof(CsprngSeed));
        return FALSE;
    }

    CsprngInitialized = TRUE;
    return TRUE;
}

BOOLEAN
NTAPI
RosCsprngFill(
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ SIZE_T Length)
{
    UCHAR Block[CSPRNG_BLOCK_SIZE];
    PUCHAR Destination = Buffer;
    SIZE_T Count;
    BOOLEAN Success;

    if (Length == 0)
    {
        return TRUE;
    }

    if ((Buffer == NULL) || (Length > MAXULONG))
    {
        return FALSE;
    }

    while (Length)
    {
        RtlAcquireSRWLockExclusive(&CsprngLock);
        Success = CsprngInitialized || CsprngInitialize();
        if (Success)
        {
            RtlZeroMemory(Block, sizeof(Block));
            Success = mbedtls_chacha20_crypt(CsprngSeed, CsprngSeed + 32, 0, sizeof(Block), Block, Block) == 0;
            if (Success)
            {
                /* Reserve a fresh key and nonce; never expose them as output. */
                RtlCopyMemory(CsprngSeed, Block, sizeof(CsprngSeed));
            }
        }
        RtlReleaseSRWLockExclusive(&CsprngLock);
        if (!Success)
        {
            RtlSecureZeroMemory(Block, sizeof(Block));
            return FALSE;
        }

        /* Copy after unlocking so a caller buffer fault cannot strand the lock. */
        Count = min(Length, sizeof(Block) - sizeof(CsprngSeed));
        RtlCopyMemory(Destination, Block + sizeof(CsprngSeed), Count);
        RtlSecureZeroMemory(Block, sizeof(Block));
        Destination += Count;
        Length -= Count;
    }

    return TRUE;
}
