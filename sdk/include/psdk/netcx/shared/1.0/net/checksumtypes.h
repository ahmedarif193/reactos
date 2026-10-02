/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx shared data path definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#pragma region Desktop Family or OneCore Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)

EXTERN_C_START

typedef enum _NET_PACKET_TX_CHECKSUM_ACTION
{
    NetPacketTxChecksumActionPassthrough = 0,
    NetPacketTxChecksumActionRequired = 2,
} NET_PACKET_TX_CHECKSUM_ACTION;

typedef enum _NET_PACKET_RX_CHECKSUM_EVALUATION
{
    NetPacketRxChecksumEvaluationNotChecked = 0,
    NetPacketRxChecksumEvaluationValid = 1,
    NetPacketRxChecksumEvaluationInvalid = 2,
} NET_PACKET_RX_CHECKSUM_EVALUATION;

#pragma warning(push)
#pragma warning(default:4820)

typedef struct _NET_PACKET_CHECKSUM
{

    UINT8
        Layer2 : 2;

    UINT8
        Layer3 : 2;

    UINT8
        Layer4 : 2;

    UINT8
        Reserved : 2;

} NET_PACKET_CHECKSUM;

C_ASSERT(sizeof(NET_PACKET_CHECKSUM) == 1);

#pragma warning(pop)

EXTERN_C_END

#define NET_PACKET_EXTENSION_CHECKSUM_NAME L"ms_packet_checksum"
#define NET_PACKET_EXTENSION_CHECKSUM_VERSION_1 1U

#endif
#pragma endregion
