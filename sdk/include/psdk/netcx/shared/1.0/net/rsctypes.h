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

#pragma warning(push)
#pragma warning(default:4820)
#pragma warning(disable:4201)

typedef struct _NET_PACKET_RSC
{
    union {
        struct {
            UINT16
                CoalescedSegmentCount;

            UINT16
                DuplicateAckCount;
        } TCP;
        struct {
            UINT16
                CoalescedSegmentCount;

            UINT16
                CoalescedSegmentSize;
        } UDP;
    } DUMMYUNIONNAME;
} NET_PACKET_RSC;

C_ASSERT(sizeof(NET_PACKET_RSC) == 4);

typedef struct _NET_PACKET_RSC_TIMESTAMP
{
    union {
        struct {
            UINT32
                RscTcpTimestampDelta;
        } TCP;
    } DUMMYUNIONNAME;
} NET_PACKET_RSC_TIMESTAMP;

C_ASSERT(sizeof(NET_PACKET_RSC_TIMESTAMP) == 4);

#pragma warning(pop)

EXTERN_C_END

#define NET_PACKET_EXTENSION_RSC_NAME L"ms_packet_rsc"
#define NET_PACKET_EXTENSION_RSC_VERSION_1 1U
#define NET_PACKET_EXTENSION_RSC_VERSION_2 2U

#define NET_PACKET_EXTENSION_RSC_TIMESTAMP_NAME L"ms_packet_rsc_timestamp"
#define NET_PACKET_EXTENSION_RSC_TIMESTAMP_VERSION_1 1U
#define NET_PACKET_EXTENSION_RSC_TIMESTAMP_VERSION_1_SIZE sizeof(NET_PACKET_RSC_TIMESTAMP)

#endif
#pragma endregion
