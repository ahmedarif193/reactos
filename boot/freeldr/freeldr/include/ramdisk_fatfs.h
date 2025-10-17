/*
 * PROJECT:     FreeLoader
 * PURPOSE:     Helpers for FatFs in-memory backing store
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RAMDISK_FATFS_DEVICE
{
    void *Base;
    uint64_t Size;
    uint32_t BytesPerSector;
} RAMDISK_FATFS_DEVICE, *PRAMDISK_FATFS_DEVICE;

void
RamDiskFatFsAttach(
    void *Base,
    uint64_t Size,
    uint32_t BytesPerSector);

void
RamDiskFatFsDetach(void);

int
RamDiskFatFsGetDevice(
    PRAMDISK_FATFS_DEVICE Device);

#ifdef __cplusplus
}
#endif
