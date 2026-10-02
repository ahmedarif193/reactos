/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C connector system software interface class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMUCSIPPMREQUESTS_H_
#define _UCMUCSIPPMREQUESTS_H_

#ifndef WDF_EXTERN_C
  #ifdef __cplusplus
    #define WDF_EXTERN_C       extern "C"
    #define WDF_EXTERN_C_START extern "C" {
    #define WDF_EXTERN_C_END   }
  #else
    #define WDF_EXTERN_C
    #define WDF_EXTERN_C_START
    #define WDF_EXTERN_C_END
  #endif
#endif

WDF_EXTERN_C_START

#define IOCTL_UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK \
    CTL_CODE(FILE_DEVICE_UCMUCSI, \
             0x0, \
             METHOD_BUFFERED, \
             FILE_WRITE_ACCESS)

#define IOCTL_UCMUCSI_PPM_GET_UCSI_DATA_BLOCK \
    CTL_CODE(FILE_DEVICE_UCMUCSI, \
             0x1, \
             METHOD_BUFFERED, \
             FILE_READ_ACCESS)

typedef enum _UCMUCSI_PPM_IOCTL {
    _IOCTL_UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK = IOCTL_UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK,
    _IOCTL_UCMUCSI_PPM_GET_UCSI_DATA_BLOCK = IOCTL_UCMUCSI_PPM_GET_UCSI_DATA_BLOCK,
} UCMUCSI_PPM_IOCTL;

#pragma warning(push)
#pragma warning(disable:4201)
#include <pshpack1.h>

typedef struct _UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK_IN_PARAMS
{
    UCMUCSIPPM PpmObject;
    UCSI_DATA_BLOCK UcmUcsiDataBlock;
} UCMUCSI_PPM_SEND_UCSI_DATA_BLOCK_IN_PARAMS, *PUCMUCSI_PPM_SEND_UCSI_DATA_BLOCK_IN_PARAMS;

typedef struct _UCMUCSI_PPM_GET_UCSI_DATA_BLOCK_IN_PARAMS
{
    UCMUCSIPPM PpmObject;
} UCMUCSI_PPM_GET_UCSI_DATA_BLOCK_IN_PARAMS, *PUCMUCSI_PPM_GET_UCSI_DATA_BLOCK_IN_PARAMS;

typedef struct _UCMUCSI_PPM_GET_UCSI_DATA_BLOCK_OUT_PARAMS
{
    UCSI_DATA_BLOCK UcmUcsiDataBlock;
} UCMUCSI_PPM_GET_UCSI_DATA_BLOCK_OUT_PARAMS, *PUCMUCSI_PPM_GET_UCSI_DATA_BLOCK_OUT_PARAMS;

#include <poppack.h>
#pragma warning(pop)

WDF_EXTERN_C_END

#endif
