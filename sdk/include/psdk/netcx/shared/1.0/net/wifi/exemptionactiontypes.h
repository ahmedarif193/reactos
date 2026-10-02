/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx shared Wi-Fi data path definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#pragma region Desktop Family or OneCore Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)

EXTERN_C_START

#pragma warning(push)
#pragma warning(default:4820)

typedef struct _NET_PACKET_WIFI_EXEMPTION_ACTION
{
    UINT8 ExemptionAction;

} NET_PACKET_WIFI_EXEMPTION_ACTION;

C_ASSERT(sizeof(NET_PACKET_WIFI_EXEMPTION_ACTION) == sizeof(UINT8));

#pragma warning(pop)

EXTERN_C_END

#define NET_PACKET_EXTENSION_WIFI_EXEMPTION_ACTION_NAME L"ms_packet_wifiexemptionaction"
#define NET_PACKET_EXTENSION_WIFI_EXEMPTION_ACTION_VERSION_1 1U

#endif
#pragma endregion
