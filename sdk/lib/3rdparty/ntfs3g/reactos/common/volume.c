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
#include "inode.h"
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

    Native = ntfs_device_mount(Device,
                               Operations->Write ? 0 : NTFS_MNT_RDONLY);
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
    ntfs_volume_get_free_space(Native);
    if (Native->free_clusters >= 0)
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
    if (!Result)
        free(Volume);
    Ntfs3gRosHostRelease();
    errno = Error;
    return Result ? -Error : 0;
}

int
Ntfs3gRosFlushVolume(NTFS3G_ROS_VOLUME *Volume)
{
    ntfs_volume *Native;
    int Error = 0;

    if (!Volume) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    Native = Volume->Native;
    if (Native->vol_ni &&
        (NInoDirty(Native->vol_ni) ||
         NInoAttrListDirty(Native->vol_ni)) &&
        ntfs_inode_sync(Native->vol_ni) &&
        !Error)
        Error = errno;
    if (Native->secure_ni &&
        (NInoDirty(Native->secure_ni) ||
         NInoAttrListDirty(Native->secure_ni)) &&
        ntfs_inode_sync(Native->secure_ni) &&
        !Error)
        Error = errno;
    if (Native->lcnbmp_ni &&
        (NInoDirty(Native->lcnbmp_ni) ||
         NInoAttrListDirty(Native->lcnbmp_ni)) &&
        ntfs_inode_sync(Native->lcnbmp_ni) &&
        !Error)
        Error = errno;
    if (Native->mft_ni &&
        (NInoDirty(Native->mft_ni) ||
         NInoAttrListDirty(Native->mft_ni)) &&
        ntfs_inode_sync(Native->mft_ni) &&
        !Error)
        Error = errno;
    if (Native->mftmirr_ni &&
        (NInoDirty(Native->mftmirr_ni) ||
         NInoAttrListDirty(Native->mftmirr_ni)) &&
        ntfs_inode_sync(Native->mftmirr_ni) &&
        !Error)
        Error = errno;
    if (ntfs_device_sync(Native->dev) && !Error)
        Error = errno;
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
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
    if (!Volume)
        return 0;
    if (Volume->Native->free_clusters >= 0)
        return Volume->Native->free_clusters;
    return Volume->FreeClusterCount;
}

uint64_t
Ntfs3gRosGetVolumeSerialNumber(const NTFS3G_ROS_VOLUME *Volume)
{
    return Volume ? Volume->SerialNumber : 0;
}

int
Ntfs3gRosReadVolumeSerialNumber(NTFS3G_ROS_VOLUME *Volume,
                                uint64_t *SerialNumber)
{
    NTFS_BOOT_SECTOR BootSector;
    int Error = 0;

    if (!Volume || !SerialNumber) {
        errno = EINVAL;
        return -EINVAL;
    }

    Ntfs3gRosHostAcquire();
    if (ntfs_pread(Volume->Native->dev,
                   0,
                   sizeof(BootSector),
                   &BootSector) != sizeof(BootSector)) {
        Error = errno ? errno : EIO;
    } else {
        *SerialNumber =
            le64_to_cpu(BootSector.volume_serial_number);
    }
    Ntfs3gRosHostRelease();
    errno = Error;
    return Error ? -Error : 0;
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

int
Ntfs3gRosSetVolumeNameUtf16(NTFS3G_ROS_VOLUME *Volume,
                            const uint16_t *Name,
                            size_t NameLength)
{
    ntfschar *NativeName;
    size_t Index;
    int Result;
    int Error;

    if (!Volume || (!Name && NameLength) ||
        NameLength > 0x100 / sizeof(ntfschar)) {
        errno = NameLength > 0x100 / sizeof(ntfschar) ?
            ERANGE : EINVAL;
        return -errno;
    }

    /*
     * ntfs_volume_rename updates both $VOLUME_NAME and the in-memory UTF-8
     * label.  Its conversion path historically examines twice the supplied
     * character count after converting that count to bytes, so retain a
     * zero-filled guard of the same size after the actual label.
     */
    NativeName = calloc(NameLength ? NameLength * 2 : 1,
                        sizeof(*NativeName));
    if (!NativeName) {
        errno = ENOMEM;
        return -ENOMEM;
    }
    for (Index = 0; Index < NameLength; ++Index)
        NativeName[Index] = cpu_to_le16(Name[Index]);

    Ntfs3gRosHostAcquire();
    Result = ntfs_volume_rename(
        Volume->Native, NativeName, (int)NameLength);
    Error = Result ? errno : 0;
    if (!Result && ntfs_inode_sync(Volume->Native->vol_ni)) {
        Error = errno ? errno : EIO;
        Result = -1;
    }
    Ntfs3gRosHostRelease();
    free(NativeName);
    errno = Error;
    return Result ? -Error : 0;
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
