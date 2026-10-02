/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio class extension 1.1 interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _ACXGLOBALS_H_
#define _ACXGLOBALS_H_

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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ACX_DRIVER_GLOBALS {
    ULONG Reserved;
} ACX_DRIVER_GLOBALS, *PACX_DRIVER_GLOBALS;

#ifdef __cplusplus
}
#endif

WDF_EXTERN_C_END

#endif
