/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx shared Wi-Fi data path definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#pragma region Desktop Family or OneCore Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)

#include <net/wifi/exemptionactiontypes.h>

EXTERN_C_START

inline
NET_PACKET_WIFI_EXEMPTION_ACTION *
WifiExtensionGetExemptionAction(
    NET_EXTENSION const * Extension,
    UINT32 Index
)
{
    return (NET_PACKET_WIFI_EXEMPTION_ACTION *) NetExtensionGetData(Extension, Index);
}

EXTERN_C_END

#endif
#pragma endregion
