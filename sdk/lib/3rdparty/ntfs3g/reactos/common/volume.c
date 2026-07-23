/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared NTFS-3G volume interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include "cache.h"
#include "device.h"
#include "host.h"
#include "layout.h"
#include "ntfs3g_ros.h"
#include "reactos_device.h"
#include "reactos_volume.h"
#include "unistr.h"
#include "volume.h"

int
Ntfs3gRosMount(void *DeviceContext,
               const NTFS3G_ROS_DEVICE_OPERATIONS *Operations,
               uint64_t DeviceLength,
               uint32_t SectorSize,
               NTFS3G_ROS_VOLUME **Volume)
{
    NTFS3G_ROS_VOLUME *HostVolume;
    NTFS_BOOT_SECTOR BootSector;
    struct ntfs_device *Device;
    ntfs_volume *Native;
    int Error;

    if (!DeviceContext || !Operations || !Operations->Read ||
        !Operations->Close || !Volume || !DeviceLength ||
        SectorSize < 256 || SectorSize > 4096 ||
        (SectorSize & (SectorSize - 1))) {
        if (DeviceContext && Operations && Operations->Close)
            Operations->Close(DeviceContext);
        errno = EINVAL;
        return -EINVAL;
    }

    *Volume = NULL;
    Ntfs3gRosHostAcquire();
    Device = Ntfs3gRosCreateDevice(DeviceContext, Operations, DeviceLength,
                                   SectorSize);
    if (!Device)
        goto error;

    Native = ntfs_device_mount(Device, NTFS_MNT_RDONLY);
    if (!Native) {
        Ntfs3gRosDestroyDevice(Device);
        goto error;
    }

    ntfs_create_lru_caches(Native);
    if (ntfs_set_ignore_case(Native)) {
        Error = errno;
        ntfs_umount(Native, TRUE);
        errno = Error;
        goto error;
    }
    ntfs_set_shown_files(Native, TRUE, TRUE, FALSE);

    HostVolume = malloc(sizeof(*HostVolume));
    if (!HostVolume) {
        Error = errno;
        ntfs_umount(Native, TRUE);
        errno = Error;
        goto error;
    }
    HostVolume->Native = Native;
    HostVolume->DeviceLength = DeviceLength;
    HostVolume->FreeClusterCount = 0;
    HostVolume->SerialNumber = 0;
    Error = errno;
    if (!ntfs_volume_get_free_space(Native) && Native->free_clusters >= 0)
        HostVolume->FreeClusterCount = Native->free_clusters;
    if (ntfs_pread(Device, 0, sizeof(BootSector), &BootSector) ==
        sizeof(BootSector))
        HostVolume->SerialNumber = le64_to_cpu(BootSector.volume_serial_number);
    errno = Error;
    *Volume = HostVolume;
    Ntfs3gRosHostRelease();
    errno = 0;
    return 0;

error:
    Error = errno;
    Ntfs3gRosHostRelease();
    errno = Error;
    return -Error;
}

int
Ntfs3gRosUnmount(NTFS3G_ROS_VOLUME *Volume)
{
    int Error;
    int Result;

    if (!Volume) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    Result = ntfs_umount(Volume->Native, TRUE);
    Error = Result ? errno : 0;
    free(Volume);
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result ? -Error : 0;
}

uint64_t
Ntfs3gRosGetVolumeSize(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->DeviceLength : 0;
}

uint32_t
Ntfs3gRosGetBytesPerSector(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->Native->sector_size : 0;
}

uint32_t
Ntfs3gRosGetSectorsPerCluster(const NTFS3G_ROS_VOLUME *Volume)
{
    if (!Volume || !Volume->Native->sector_size)
        return 0;
    return Volume->Native->cluster_size / Volume->Native->sector_size;
}

uint64_t
Ntfs3gRosGetClusterCount(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->Native->nr_clusters : 0;
}

uint64_t
Ntfs3gRosGetFreeClusterCount(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->FreeClusterCount : 0;
}

uint64_t
Ntfs3gRosGetVolumeSerialNumber(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->SerialNumber : 0;
}

const char *
Ntfs3gRosGetVolumeName(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->Native->vol_name : NULL;
}

int
Ntfs3gRosGetVolumeNameUtf16(const NTFS3G_ROS_VOLUME *Volume,
                            uint16_t *Buffer,
                            size_t BufferLength,
                            size_t *NameLength)
{
    ntfschar *Name = NULL;
    size_t Index;
    int Length;
    int Error;

    if (!Volume || !Buffer || !NameLength) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    Length = ntfs_mbstoucs(Volume->Native->vol_name, &Name);
    Error = Length < 0 ? errno : 0;
    if (Length >= 0 && (size_t)Length >= BufferLength) {
        Length = -1;
        Error = ENAMETOOLONG;
    }
    if (Length >= 0) {
        for (Index = 0; Index < (size_t)Length; ++Index)
            Buffer[Index] = le16_to_cpu(Name[Index]);
        Buffer[Length] = 0;
        *NameLength = Length;
    }
    free(Name);
    Ntfs3gRosHostRelease();
    errno = Error;
    return Length < 0 ? -Error : 0;
}

uint8_t
Ntfs3gRosGetMajorVersion(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->Native->major_ver : 0;
}

uint8_t
Ntfs3gRosGetMinorVersion(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->Native->minor_ver : 0;
}

int
Ntfs3gRosIsReadOnly(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? NVolReadOnly(Volume->Native) : 1;
}

void *
Ntfs3gRosGetNativeVolume(NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->Native : NULL;
}
