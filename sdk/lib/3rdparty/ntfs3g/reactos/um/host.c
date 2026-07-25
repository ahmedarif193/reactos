/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Win32 host callbacks for the shared NTFS-3G core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifdef _WIN32

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

static int
Ntfs3gUserWrite(void *OpaqueContext,
                uint64_t Offset,
                const void *Buffer,
                uint32_t Length,
                uint32_t *BytesWritten)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;
    LARGE_INTEGER Position;
    DWORD Done;

    if (Offset > INT64_MAX)
        return EINVAL;
    Position.QuadPart = Offset;
    if (!SetFilePointerEx(Context->Handle, Position, NULL, FILE_BEGIN))
        return Ntfs3gUserErrorToErrno(GetLastError());
    if (!WriteFile(Context->Handle, Buffer, Length, &Done, NULL))
        return Ntfs3gUserErrorToErrno(GetLastError());
    *BytesWritten = Done;
    return 0;
}

static int
Ntfs3gUserSync(void *OpaqueContext)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;

    return FlushFileBuffers(Context->Handle) ?
        0 : Ntfs3gUserErrorToErrno(GetLastError());
}

static void
Ntfs3gUserClose(void *OpaqueContext)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;

    CloseHandle(Context->Handle);
    Ntfs3gRosHostFree(Context);
}

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gUserReadOnlyOperations = {
    Ntfs3gUserRead,
    Ntfs3gUserClose,
    NULL,
    NULL
};

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gUserReadWriteOperations = {
    Ntfs3gUserRead,
    Ntfs3gUserClose,
    Ntfs3gUserWrite,
    Ntfs3gUserSync
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

    if (!Handle || !Volume) {
        errno = EINVAL;
        return -EINVAL;
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

    Context = Ntfs3gRosHostAllocate(sizeof(*Context));
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

    Result = Ntfs3gRosMount(Context,
                            ReadOnly ? &Ntfs3gUserReadOnlyOperations :
                                       &Ntfs3gUserReadWriteOperations,
                            DeviceLength, SectorSize, Volume);
    Error = Result < 0 ? -Result : 0;
    errno = Error;
    return Result;
}

int
Ntfs3gRosMountPath(const char *Path,
                   int ReadOnly,
                   NTFS3G_ROS_VOLUME **Volume)
{
    HANDLE Handle;
    int Result;
    int Error;

    if (!Path || !Volume) {
        errno = EINVAL;
        return -EINVAL;
    }

    Handle = CreateFileA(Path,
                         GENERIC_READ | (ReadOnly ? 0 : GENERIC_WRITE),
                         FILE_SHARE_READ,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE) {
        Error = Ntfs3gUserErrorToErrno(GetLastError());
        errno = Error;
        return -Error;
    }

    Result = Ntfs3gRosMountHandle(Handle, ReadOnly, Volume);
    Error = Result < 0 ? -Result : 0;
    CloseHandle(Handle);
    errno = Error;
    return Result;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ntfs3g_ros.h"

typedef struct _NTFS3G_USER_DEVICE
{
    int FileDescriptor;
} NTFS3G_USER_DEVICE;

static pthread_mutex_t Ntfs3gUserRuntimeLock = PTHREAD_MUTEX_INITIALIZER;

void *
Ntfs3gRosHostAllocate(size_t Size)
{
    return malloc(Size);
}

void
Ntfs3gRosHostFree(void *Buffer)
{
    free(Buffer);
}

void
Ntfs3gRosHostAcquire(void)
{
    pthread_mutex_lock(&Ntfs3gUserRuntimeLock);
}

void
Ntfs3gRosHostRelease(void)
{
    pthread_mutex_unlock(&Ntfs3gUserRuntimeLock);
}

int64_t
Ntfs3gRosHostGetTime(void)
{
    return (int64_t)time(NULL);
}

void
Ntfs3gRosHostLog(int IsError,
                 const char *Message)
{
    fputs(Message, IsError ? stderr : stdout);
}

static int
Ntfs3gUserRead(void *OpaqueContext,
               uint64_t Offset,
               void *Buffer,
               uint32_t Length,
               uint32_t *BytesRead)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;
    ssize_t Result;

    if (Offset > INT64_MAX)
        return EINVAL;
    Result = pread(Context->FileDescriptor, Buffer, Length, (off_t)Offset);
    if (Result < 0)
        return errno;
    *BytesRead = (uint32_t)Result;
    return 0;
}

static int
Ntfs3gUserWrite(void *OpaqueContext,
                uint64_t Offset,
                const void *Buffer,
                uint32_t Length,
                uint32_t *BytesWritten)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;
    ssize_t Result;

    if (Offset > INT64_MAX)
        return EINVAL;
    Result = pwrite(Context->FileDescriptor, Buffer, Length, (off_t)Offset);
    if (Result < 0)
        return errno;
    *BytesWritten = (uint32_t)Result;
    return 0;
}

static int
Ntfs3gUserSync(void *OpaqueContext)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;

    return fsync(Context->FileDescriptor) ? errno : 0;
}

static void
Ntfs3gUserClose(void *OpaqueContext)
{
    NTFS3G_USER_DEVICE *Context = OpaqueContext;

    close(Context->FileDescriptor);
    free(Context);
}

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gUserReadOnlyOperations = {
    Ntfs3gUserRead,
    Ntfs3gUserClose,
    NULL,
    NULL
};

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gUserReadWriteOperations = {
    Ntfs3gUserRead,
    Ntfs3gUserClose,
    Ntfs3gUserWrite,
    Ntfs3gUserSync
};

int
Ntfs3gRosMountHandle(void *Handle,
                     int ReadOnly,
                     NTFS3G_ROS_VOLUME **Volume)
{
    NTFS3G_USER_DEVICE *Context;
    struct stat Status;
    int FileDescriptor;
    int Result;
    int Error;

    if (!Volume || (intptr_t)Handle < 0) {
        errno = EINVAL;
        return -EINVAL;
    }

    FileDescriptor = dup((int)(intptr_t)Handle);
    if (FileDescriptor < 0)
        return -errno;
    if (fstat(FileDescriptor, &Status)) {
        Error = errno;
        close(FileDescriptor);
        errno = Error;
        return -Error;
    }

    Context = malloc(sizeof(*Context));
    if (!Context) {
        close(FileDescriptor);
        errno = ENOMEM;
        return -ENOMEM;
    }
    Context->FileDescriptor = FileDescriptor;

    Result = Ntfs3gRosMount(Context,
                            ReadOnly ? &Ntfs3gUserReadOnlyOperations :
                                       &Ntfs3gUserReadWriteOperations,
                            (uint64_t)Status.st_size, 512, Volume);
    Error = Result < 0 ? -Result : 0;
    errno = Error;
    return Result;
}

int
Ntfs3gRosMountPath(const char *Path,
                   int ReadOnly,
                   NTFS3G_ROS_VOLUME **Volume)
{
    int FileDescriptor;
    int Result;
    int Error;

    if (!Path || !Volume) {
        errno = EINVAL;
        return -EINVAL;
    }

    FileDescriptor = open(Path, ReadOnly ? O_RDONLY : O_RDWR);
    if (FileDescriptor < 0)
        return -errno;
    Result = Ntfs3gRosMountHandle((void *)(intptr_t)FileDescriptor,
                                  ReadOnly, Volume);
    Error = Result < 0 ? -Result : 0;
    close(FileDescriptor);
    errno = Error;
    return Result;
}

#endif
