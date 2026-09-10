/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     WDI miniport driver registration.
 */
#include "ndis6_internal.h"

NDIS_STATUS
NdisMRegisterWdiMiniportDriver(
    PDRIVER_OBJECT DriverObject,
    PCUNICODE_STRING RegistryPath,
    NDIS_HANDLE DriverContext,
    PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics,
    PNDIS_MINIPORT_DRIVER_WDI_CHARACTERISTICS WdiCharacteristics,
    PNDIS_HANDLE DriverHandle)
{
    if (DriverHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    *DriverHandle = NULL;
    if (WdiCharacteristics == NULL || WdiCharacteristics->WdiVersion < WDI_VERSION_1_0 || WdiCharacteristics->WdiVersion > WDI_VERSION_LATEST)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Native WDI consumes the revision-one layout independently of the
     * advertised Header fields. Callback validity is an adapter-lifecycle
     * concern; SetOptions must run with the original driver context. */
    return Ndis6RegisterMiniportDriverInternal(DriverObject, (PUNICODE_STRING)RegistryPath, DriverContext, Characteristics, NULL, WdiCharacteristics, DriverHandle);
}

VOID
NdisMDeregisterWdiMiniportDriver(NDIS_HANDLE DriverHandle)
{
    NdisMDeregisterMiniportDriver(DriverHandle);
}
