/*-----------------------------------------------------------------------/
/  Low level disk interface module include file   (C)ChaN, 2014          /
/-----------------------------------------------------------------------*/

#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define _USE_WRITE 1
#define _USE_IOCTL 1

typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint32_t UINT;

typedef BYTE DSTATUS;

typedef enum
{
    RES_OK = 0,
    RES_ERROR,
    RES_WRPRT,
    RES_NOTRDY,
    RES_PARERR
} DRESULT;

DSTATUS disk_initialize(BYTE pdrv);
DSTATUS disk_status(BYTE pdrv);
DRESULT disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);

/* Disk Status Bits (DSTATUS) */
#define STA_NOINIT   0x01
#define STA_NODISK   0x02
#define STA_PROTECT  0x04

/* Command Functions */
#define CTRL_SYNC           0
#define GET_SECTOR_COUNT    1
#define GET_SECTOR_SIZE     2
#define GET_BLOCK_SIZE      3

#if _USE_ISDIO
DRESULT disk_ioctl_isd(BYTE pdrv, BYTE cmd, void* buff);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _DISKIO_DEFINED */
