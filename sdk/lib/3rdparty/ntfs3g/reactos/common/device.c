/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared read-only NTFS-3G device implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>

#include "device.h"
#include "reactos_device.h"

typedef struct _NTFS3G_ROS_DEVICE
{
    void *Context;
    NTFS3G_ROS_DEVICE_OPERATIONS Operations;
    int64_t Position;
    uint64_t Length;
    uint32_t SectorSize;
} NTFS3G_ROS_DEVICE;

static int
Ntfs3gRosDeviceOpen(struct ntfs_device *Device,
                    int Flags)
{
    if (NDevOpen(Device)) {
        errno = EBUSY;
        return -1;
    }
    if (Flags & O_RDWR) {
        errno = EROFS;
        return -1;
    }

    NDevSetBlock(Device);
    NDevSetReadOnly(Device);
    NDevSetOpen(Device);
    return 0;
}

static void
Ntfs3gRosReleaseDeviceContext(NTFS3G_ROS_DEVICE *Context)
{
    if (!Context)
        return;
    Context->Operations.Close(Context->Context);
    free(Context);
}

static int
Ntfs3gRosDeviceClose(struct ntfs_device *Device)
{
    if (!NDevOpen(Device)) {
        errno = EBADF;
        return -1;
    }

    NDevClearOpen(Device);
    Ntfs3gRosReleaseDeviceContext(Device->d_private);
    Device->d_private = NULL;
    return 0;
}

static int64_t
Ntfs3gRosDeviceSeek(struct ntfs_device *Device,
                    int64_t Offset,
                    int Origin)
{
    NTFS3G_ROS_DEVICE *Context = Device->d_private;
    int64_t Position;

    switch (Origin) {
        case SEEK_SET:
            Position = Offset;
            break;
        case SEEK_CUR:
            Position = Context->Position + Offset;
            break;
        case SEEK_END:
            if (Context->Length > INT64_MAX) {
                errno = EOVERFLOW;
                return -1;
            }
            Position = (int64_t)Context->Length + Offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    if (Position < 0) {
        errno = EINVAL;
        return -1;
    }
    Context->Position = Position;
    return Position;
}

static int64_t
Ntfs3gRosDevicePread(struct ntfs_device *Device,
                     void *Buffer,
                     int64_t Count,
                     int64_t Offset)
{
    NTFS3G_ROS_DEVICE *Context = Device->d_private;
    uint64_t Total = 0;

    if (Count < 0 || Offset < 0) {
        errno = EINVAL;
        return -1;
    }

    while (Count) {
        uint32_t Chunk = Count > UINT32_MAX ? UINT32_MAX : (uint32_t)Count;
        uint32_t Done = 0;
        int Error;

        Error = Context->Operations.Read(Context->Context,
                                         (uint64_t)Offset + Total,
                                         (char *)Buffer + Total,
                                         Chunk,
                                         &Done);
        if (Error) {
            errno = Error;
            return Total ? (int64_t)Total : -1;
        }
        Total += Done;
        Count -= Done;
        if (Done != Chunk)
            break;
    }
    return (int64_t)Total;
}

static int64_t
Ntfs3gRosDeviceRead(struct ntfs_device *Device,
                    void *Buffer,
                    int64_t Count)
{
    NTFS3G_ROS_DEVICE *Context = Device->d_private;
    int64_t Result = Ntfs3gRosDevicePread(Device, Buffer, Count,
                                          Context->Position);

    if (Result > 0)
        Context->Position += Result;
    return Result;
}

static int64_t
Ntfs3gRosDeviceWrite(struct ntfs_device *Device,
                     const void *Buffer,
                     int64_t Count)
{
    (void)Device;
    (void)Buffer;
    (void)Count;
    errno = EROFS;
    return -1;
}

static int64_t
Ntfs3gRosDevicePwrite(struct ntfs_device *Device,
                      const void *Buffer,
                      int64_t Count,
                      int64_t Offset)
{
    (void)Offset;
    return Ntfs3gRosDeviceWrite(Device, Buffer, Count);
}

static int
Ntfs3gRosDeviceSync(struct ntfs_device *Device)
{
    (void)Device;
    return 0;
}

static int
Ntfs3gRosDeviceStat(struct ntfs_device *Device,
                    struct stat *Stat)
{
    (void)Device;
    (void)Stat;
    errno = EOPNOTSUPP;
    return -1;
}

static int
Ntfs3gRosDeviceIoctl(struct ntfs_device *Device,
                     unsigned long Request,
                     void *Argument)
{
    NTFS3G_ROS_DEVICE *Context = Device->d_private;

    switch (Request) {
        case BLKSSZGET:
            *(int *)Argument = Context->SectorSize;
            return 0;
        case BLKGETSIZE64:
            *(uint64_t *)Argument = Context->Length;
            return 0;
        case BLKBSZSET:
            return 0;
        default:
            errno = ENOTTY;
            return -1;
    }
}

static struct ntfs_device_operations Ntfs3gRosDeviceOperations = {
    Ntfs3gRosDeviceOpen,
    Ntfs3gRosDeviceClose,
    Ntfs3gRosDeviceSeek,
    Ntfs3gRosDeviceRead,
    Ntfs3gRosDeviceWrite,
    Ntfs3gRosDevicePread,
    Ntfs3gRosDevicePwrite,
    Ntfs3gRosDeviceSync,
    Ntfs3gRosDeviceStat,
    Ntfs3gRosDeviceIoctl
};

struct ntfs_device *
Ntfs3gRosCreateDevice(void *HostContext,
                     const NTFS3G_ROS_DEVICE_OPERATIONS *Operations,
                     uint64_t Length,
                     uint32_t SectorSize)
{
    NTFS3G_ROS_DEVICE *Context;
    struct ntfs_device *Device;

    Context = calloc(1, sizeof(*Context));
    if (!Context) {
        Operations->Close(HostContext);
        return NULL;
    }
    Context->Context = HostContext;
    Context->Operations = *Operations;
    Context->Length = Length;
    Context->SectorSize = SectorSize;

    Device = ntfs_device_alloc("reactos-ntfs3g", 0,
                               &Ntfs3gRosDeviceOperations, Context);
    if (!Device) {
        Ntfs3gRosReleaseDeviceContext(Context);
        return NULL;
    }
    return Device;
}

void
Ntfs3gRosDestroyDevice(struct ntfs_device *Device)
{
    if (!Device)
        return;
    if (NDevOpen(Device))
        Device->d_ops->close(Device);
    else if (Device->d_private) {
        Ntfs3gRosReleaseDeviceContext(Device->d_private);
        Device->d_private = NULL;
    }
    ntfs_device_free(Device);
}
