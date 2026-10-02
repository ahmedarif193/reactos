/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C connector system software interface class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMUCSIGLOBALS_H_
#define _UCMUCSIGLOBALS_H_

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

typedef struct _UCMUCSI_DRIVER_GLOBALS {
    ULONG Reserved;
} UCMUCSI_DRIVER_GLOBALS, *PUCMUCSI_DRIVER_GLOBALS;

WDF_EXTERN_C_END

#endif
