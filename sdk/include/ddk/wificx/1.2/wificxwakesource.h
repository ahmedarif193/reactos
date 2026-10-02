/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WiFiCx 1.2 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WIFICXWAKESOURCE_H_
#define _WIFICXWAKESOURCE_H_

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

typedef enum _WIFI_WAKE_SOURCE_TYPE
{
    WifiWakeSourceTypeNloDiscovery = 1,
    WifiWakeSourceTypeApAssociationLost,
    WifiWakeSourceTypeGtkHandshakeError,
    WifiWakeSourceTypeFourWayHandshakeRequest,
    WifiWakeSourceTypeIncomingActionFrame,
    WifiWakeSourceTypeClientDriverDiagnostic,
} WIFI_WAKE_SOURCE_TYPE;

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
WIFI_WAKE_SOURCE_TYPE
(NTAPI *PFN_WIFIWAKESOURCEGETTYPE)(
    _In_
    PWIFI_DRIVER_GLOBALS DriverGlobals,
    _In_
    WIFIWAKESOURCE WakeSource
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
WIFI_WAKE_SOURCE_TYPE
WifiWakeSourceGetType(
    _In_
    WIFIWAKESOURCE WakeSource
    )
{
    return ((PFN_WIFIWAKESOURCEGETTYPE) WifiFunctions[WifiWakeSourceGetTypeTableIndex])(WifiDriverGlobals, WakeSource);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
NETADAPTER
(NTAPI *PFN_WIFIWAKESOURCEGETADAPTER)(
    _In_
    PWIFI_DRIVER_GLOBALS DriverGlobals,
    _In_
    WIFIWAKESOURCE WakeSource
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
NETADAPTER
WifiWakeSourceGetAdapter(
    _In_
    WIFIWAKESOURCE WakeSource
    )
{
    return ((PFN_WIFIWAKESOURCEGETADAPTER) WifiFunctions[WifiWakeSourceGetAdapterTableIndex])(WifiDriverGlobals, WakeSource);
}

WDF_EXTERN_C_END

#endif
