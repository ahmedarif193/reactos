/*
 * PROJECT:     FreeLoader
 * PURPOSE:     In-memory disk I/O glue for FatFs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 */

#include <stdint.h>
#include <string.h>
#include "diskio.h"
#include "ramdisk_fatfs.h"

static int
FatFsGetDevice(PRAMDISK_FATFS_DEVICE Device)
{
    return RamDiskFatFsGetDevice(Device);
}

DSTATUS disk_status(BYTE pdrv)
{
    RAMDISK_FATFS_DEVICE Device;

    if (pdrv != 0)
        return STA_NOINIT;

    if (!FatFsGetDevice(&Device) || Device.Base == NULL || Device.BytesPerSector == 0)
        return STA_NOINIT;

    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count)
{
    RAMDISK_FATFS_DEVICE Device;
    uint64_t Offset;
    uint64_t Length;

    if (!buff || count == 0)
        return RES_PARERR;

    if (pdrv != 0)
        return RES_PARERR;

    if (!FatFsGetDevice(&Device))
        return RES_NOTRDY;

    Offset = (uint64_t)sector * Device.BytesPerSector;
    Length = (uint64_t)count * Device.BytesPerSector;

    if ((Offset + Length) > Device.Size)
        return RES_PARERR;

    memcpy(buff, (const uint8_t *)Device.Base + Offset, (size_t)Length);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count)
{
    RAMDISK_FATFS_DEVICE Device;
    uint64_t Offset;
    uint64_t Length;

    if (!buff || count == 0)
        return RES_PARERR;

    if (pdrv != 0)
        return RES_PARERR;

    if (!FatFsGetDevice(&Device))
        return RES_NOTRDY;

    Offset = (uint64_t)sector * Device.BytesPerSector;
    Length = (uint64_t)count * Device.BytesPerSector;

    if ((Offset + Length) > Device.Size)
        return RES_PARERR;

    memcpy((uint8_t *)Device.Base + Offset, buff, (size_t)Length);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff)
{
    RAMDISK_FATFS_DEVICE Device;

    if (pdrv != 0)
        return RES_PARERR;

    if (!FatFsGetDevice(&Device))
        return RES_NOTRDY;

    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            if (buff)
            {
                *(DWORD*)buff = (DWORD)(Device.Size / Device.BytesPerSector);
                return RES_OK;
            }
            return RES_PARERR;

        case GET_SECTOR_SIZE:
            if (buff)
            {
                *(WORD*)buff = (WORD)Device.BytesPerSector;
                return RES_OK;
            }
            return RES_PARERR;

        case GET_BLOCK_SIZE:
            if (buff)
            {
                *(DWORD*)buff = 1;
                return RES_OK;
            }
            return RES_PARERR;

        default:
            return RES_PARERR;
    }
}

#if _USE_ISDIO
DRESULT disk_ioctl_isd(BYTE pdrv, BYTE cmd, void* buff)
{
    (void)pdrv;
    (void)cmd;
    (void)buff;
    return RES_PARERR;
}
#endif
