/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     MBBCx 1.0 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _MBBCXTYPES_H_
#define _MBBCXTYPES_H_

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

DECLARE_HANDLE( MBBREQUEST );

typedef ULONG DSS_SESSION_ID;

typedef struct _MBB_DRIVER_GLOBALS
{
    ULONG Unused;
} MBB_DRIVER_GLOBALS, *PMBB_DRIVER_GLOBALS;

typedef VOID (*MBBFUNC) (VOID);
extern MBBFUNC MbbFunctions[];

WDF_EXTERN_C_END

#endif
