/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx 2.3 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NETWAKESOURCE_2_3_H_
#define _NETWAKESOURCE_2_3_H_

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

typedef struct _NET_WAKE_SOURCE_BITMAP_PARAMETERS
{
    ULONG Size;
    ULONG Id;
    UCHAR const * Pattern;
    SIZE_T PatternSize;
    UCHAR const * Mask;
    SIZE_T MaskSize;
} NET_WAKE_SOURCE_BITMAP_PARAMETERS;

inline
void
NET_WAKE_SOURCE_BITMAP_PARAMETERS_INIT(
    _Out_ NET_WAKE_SOURCE_BITMAP_PARAMETERS * Parameters
)
{
    RtlZeroMemory(Parameters, sizeof(*Parameters));
    Parameters->Size = sizeof(*Parameters);
}

typedef struct _NET_WAKE_SOURCE_MEDIA_CHANGE_PARAMETERS
{
    ULONG Size;
    BOOLEAN MediaConnect;
    BOOLEAN MediaDisconnect;
} NET_WAKE_SOURCE_MEDIA_CHANGE_PARAMETERS;

inline
void
NET_WAKE_SOURCE_MEDIA_CHANGE_PARAMETERS_INIT(
    _Out_ NET_WAKE_SOURCE_MEDIA_CHANGE_PARAMETERS * Parameters
)
{
    RtlZeroMemory(Parameters, sizeof(*Parameters));
    Parameters->Size = sizeof(*Parameters);
}

typedef enum _NET_WAKE_SOURCE_TYPE {
    NetWakeSourceTypeBitmapPattern = 1,
    NetWakeSourceTypeMagicPacket,
    NetWakeSourceTypeMediaChange,
    NetWakeSourceTypePacketFilterMatch,
    NetWakeSourceTypeEapolPacket,
} NET_WAKE_SOURCE_TYPE;

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
NET_WAKE_SOURCE_TYPE
(NTAPI *PFN_NETWAKESOURCEGETTYPE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETWAKESOURCE WakeSource
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
NET_WAKE_SOURCE_TYPE
NetWakeSourceGetType(
    _In_
    NETWAKESOURCE WakeSource
    )
{
    return ((PFN_NETWAKESOURCEGETTYPE) NetFunctions[NetWakeSourceGetTypeTableIndex])(NetDriverGlobals, WakeSource);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
NETADAPTER
(NTAPI *PFN_NETWAKESOURCEGETADAPTER)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETWAKESOURCE WakeSource
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
NETADAPTER
NetWakeSourceGetAdapter(
    _In_
    NETWAKESOURCE WakeSource
    )
{
    return ((PFN_NETWAKESOURCEGETADAPTER) NetFunctions[NetWakeSourceGetAdapterTableIndex])(NetDriverGlobals, WakeSource);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETWAKESOURCEGETBITMAPPARAMETERS)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETWAKESOURCE WakeSource,
    _Inout_
    NET_WAKE_SOURCE_BITMAP_PARAMETERS* Parameters
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetWakeSourceGetBitmapParameters(
    _In_
    NETWAKESOURCE WakeSource,
    _Inout_
    NET_WAKE_SOURCE_BITMAP_PARAMETERS* Parameters
    )
{
    ((PFN_NETWAKESOURCEGETBITMAPPARAMETERS) NetFunctions[NetWakeSourceGetBitmapParametersTableIndex])(NetDriverGlobals, WakeSource, Parameters);
}

typedef
_IRQL_requires_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETWAKESOURCEGETMEDIACHANGEPARAMETERS)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETWAKESOURCE WakeSource,
    _Inout_
    NET_WAKE_SOURCE_MEDIA_CHANGE_PARAMETERS* Parameters
    );

_IRQL_requires_(PASSIVE_LEVEL)
FORCEINLINE
void
NetWakeSourceGetMediaChangeParameters(
    _In_
    NETWAKESOURCE WakeSource,
    _Inout_
    NET_WAKE_SOURCE_MEDIA_CHANGE_PARAMETERS* Parameters
    )
{
    ((PFN_NETWAKESOURCEGETMEDIACHANGEPARAMETERS) NetFunctions[NetWakeSourceGetMediaChangeParametersTableIndex])(NetDriverGlobals, WakeSource, Parameters);
}

WDF_EXTERN_C_END

#endif
