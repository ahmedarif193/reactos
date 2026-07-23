/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Win32 host callbacks for the shared NTFS-3G core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winioctl.h>

#include <errno.h>
#include <stdint.h>

#include "host.h"
#include "ntfs3g_ros.h"

typedef struct _NTFS3G_USER_DEVICE
{
    HANDLE Handle;
} NTFS3G_USER_DEVICE;

static INIT_ONCE Ntfs3gUserRuntimeOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION Ntfs3gUserRuntimeLock;

static BOOL CALLBACK
Ntfs3gUserInitializeRuntime(PINIT_ONCE Once,
                            PVOID Parameter,
                            PVOID *Context)
{
    (void)Once;
    (void)Parameter;
    (void)Context;
    InitializeCriticalSection(&Ntfs3gUserRuntimeLock);
    return TRUE;
}

static int
Ntfs3gUserEnsureRuntime(void)
{
    return InitOnceExecuteOnce(&Ntfs3gUserRuntimeOnce,
                               Ntfs3gUserInitializeRuntime,
                               NULL,
                               NULL) ? 0 : -1;
}

static int
Ntfs3gUserErrorToErrno(DWORD Error)
{
    switch (Error) {
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return EACCES;
        case ERROR_HANDLE_EOF:
            return 0;
        case ERROR_INVALID_HANDLE:
        case ERROR_INVALID_PARAMETER:
            return EINVAL;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return ENOMEM;
        case ERROR_WRITE_PROTECT:
            return EROFS;
        default:
            return EIO;
    }
}

void *
Ntfs3gRosHostAllocate(size_t Size)
{
    return HeapAlloc(GetProcessHeap(), 0, Size);
}

void
Ntfs3gRosHostFree(void *Buffer)
{
    if (Buffer)
        HeapFree(GetProcessHeap(), 0, Buffer);
}

void
Ntfs3gRosHostAcquire(void)
{
    EnterCriticalSection(&Ntfs3gUserRuntimeLock);
}

void
Ntfs3gRosHostRelease(void)
{
    LeaveCriticalSection(&Ntfs3gUserRuntimeLock);
}

int64_t
Ntfs3gRosHostGetTime(void)
{
    ULARGE_INTEGER Time;
    FILETIME FileTime;

    GetSystemTimeAsFileTime(&FileTime);
    Time.LowPart = FileTime.dwLowDateTime;
    Time.HighPart = FileTime.dwHighDateTime;
    return (int64_t)(Time.QuadPart / 10000000ULL - 11644473600ULL);
}

void
Ntfs3gRosHostLog(int IsError,
                 const char *Message)
{
    (void)IsError;
    OutputDebugStringA("NTFS3G: ");
    OutputDebugStringA(Message);
}

static int
Ntfs3gUserRead(void *OpaqueContext,
               uint64_t Offset,
               void *Buffer,
               uint32_t Length,
               uint32_t *BytesRead)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;
    LARGE_INTEGER Position;
    DWORD Done;

    if (Offset > INT64_MAX)
        return EINVAL;
    Position.QuadPart = Offset;
    if (!SetFilePointerEx(Context->Handle, Position, NULL, FILE_BEGIN))
        return Ntfs3gUserErrorToErrno(GetLastError());
    if (!ReadFile(Context->Handle, Buffer, Length, &Done, NULL))
        return Ntfs3gUserErrorToErrno(GetLastError());
    *BytesRead = Done;
    return 0;
}

static void
Ntfs3gUserClose(void *OpaqueContext)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;

    CloseHandle(Context->Handle);
    HeapFree(GetProcessHeap(), 0, Context);
}

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gUserDeviceOperations = {
    Ntfs3gUserRead,
    Ntfs3gUserClose
};

int
Ntfs3gRosMountHandle(void *Handle,
                     int ReadOnly,
                     NTFS3G_ROS_VOLUME **Volume)
{
    NTFS3G_USER_DEVICE *Context;
    GET_LENGTH_INFORMATION Length;
    DISK_GEOMETRY Geometry;
    LARGE_INTEGER FileSize;
    uint64_t DeviceLength = 0;
    uint32_t SectorSize = 512;
    DWORD Returned;
    HANDLE Duplicate;
    int Result;
    int Error;

    if (!Handle || !Volume || !ReadOnly) {
        Error = !ReadOnly ? EOPNOTSUPP : EINVAL;
        errno = Error;
        return -Error;
    }
    if (Ntfs3gUserEnsureRuntime()) {
        errno = ENOMEM;
        return -ENOMEM;
    }
    if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)Handle,
                         GetCurrentProcess(), &Duplicate, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        Error = Ntfs3gUserErrorToErrno(GetLastError());
        errno = Error;
        return -Error;
    }

    Context = HeapAlloc(GetProcessHeap(), 0, sizeof(*Context));
    if (!Context) {
        CloseHandle(Duplicate);
        errno = ENOMEM;
        return -ENOMEM;
    }
    Context->Handle = Duplicate;

    if (DeviceIoControl(Duplicate, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                        &Geometry, sizeof(Geometry), &Returned, NULL))
        SectorSize = Geometry.BytesPerSector;
    if (DeviceIoControl(Duplicate, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                        &Length, sizeof(Length), &Returned, NULL))
        DeviceLength = Length.Length.QuadPart;
    else if (GetFileSizeEx(Duplicate, &FileSize))
        DeviceLength = FileSize.QuadPart;

    Result = Ntfs3gRosMount(Context, &Ntfs3gUserDeviceOperations,
                            DeviceLength, SectorSize, Volume);
    Error = Result < 0 ? -Result : 0;
    errno = Error;
    return Result;
}
