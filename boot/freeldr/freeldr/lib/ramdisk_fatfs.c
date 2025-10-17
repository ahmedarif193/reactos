/*
 * PROJECT:     FreeLoader
 * PURPOSE:     Helpers for FatFs in-memory backing store
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 */

#include <string.h>
#include "ramdisk_fatfs.h"

static RAMDISK_FATFS_DEVICE g_FatFsDevice = { NULL, 0, 0 };

void
RamDiskFatFsAttach(
    void *Base,
    uint64_t Size,
    uint32_t BytesPerSector)
{
    g_FatFsDevice.Base = Base;
    g_FatFsDevice.Size = Size;
    g_FatFsDevice.BytesPerSector = BytesPerSector;
}

void
RamDiskFatFsDetach(void)
{
    memset(&g_FatFsDevice, 0, sizeof(g_FatFsDevice));
}

int
RamDiskFatFsGetDevice(
    PRAMDISK_FATFS_DEVICE Device)
{
    if (!g_FatFsDevice.Base || g_FatFsDevice.BytesPerSector == 0)
        return 0;

    if (Device)
        *Device = g_FatFsDevice;

    return 1;
}
