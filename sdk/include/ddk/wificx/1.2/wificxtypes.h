/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WiFiCx 1.2 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WIFICXTYPES_H_
#define _WIFICXTYPES_H_

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

DECLARE_HANDLE( WIFIREQUEST );
DECLARE_HANDLE( WIFIDIRECTDEVICE );
DECLARE_HANDLE( WIFIPOWEROFFLOAD );
DECLARE_HANDLE( WIFIWAKESOURCE );

struct _WIFIDIRECT_DEVICE_INIT;
typedef struct _WIFIDIRECT_DEVICE_INIT WIFIDIRECT_DEVICE_INIT;

typedef struct _WIFI_DRIVER_GLOBALS
{
    ULONG Unused;
} WIFI_DRIVER_GLOBALS, *PWIFI_DRIVER_GLOBALS;

typedef VOID (*WIFIFUNC) (VOID);
extern WIFIFUNC WifiFunctions[];

WDF_EXTERN_C_END

#endif
