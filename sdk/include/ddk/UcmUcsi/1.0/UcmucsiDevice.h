/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C connector system software interface class extension client interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _UCMUCSIDEVICE_H_
#define _UCMUCSIDEVICE_H_

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

typedef struct _UCMUCSI_DEVICE_CONFIG
{
    ULONG Size;
} UCMUCSI_DEVICE_CONFIG, *PUCMUCSI_DEVICE_CONFIG;

VOID
FORCEINLINE
UCMUCSI_DEVICE_CONFIG_INIT (
    _Out_ PUCMUCSI_DEVICE_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(*Config));
    Config->Size = sizeof(*Config);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_UCMUCSIDEVICEINITINITIALIZE)(
    _In_
    PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_
    PWDFDEVICE_INIT DeviceInit
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
UcmUcsiDeviceInitInitialize(
    _In_
    PWDFDEVICE_INIT DeviceInit
    )
{
    return ((PFN_UCMUCSIDEVICEINITINITIALIZE) UcmucsiFunctions[UcmUcsiDeviceInitInitializeTableIndex])(UcmucsiDriverGlobals, DeviceInit);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_UCMUCSIDEVICEINITIALIZE)(
    _In_
    PUCMUCSI_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE WdfDevice,
    _In_
    PUCMUCSI_DEVICE_CONFIG Config
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
UcmUcsiDeviceInitialize(
    _In_
    WDFDEVICE WdfDevice,
    _In_
    PUCMUCSI_DEVICE_CONFIG Config
    )
{
    return ((PFN_UCMUCSIDEVICEINITIALIZE) UcmucsiFunctions[UcmUcsiDeviceInitializeTableIndex])(UcmucsiDriverGlobals, WdfDevice, Config);
}

WDF_EXTERN_C_END

#endif
