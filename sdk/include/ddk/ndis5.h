/*
 * ndis5.h
 *
 * NDIS 5.1 Legacy Types and Definitions - Supplementary Header
 *
 * This file is part of the ReactOS DDK package.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * This header provides supplementary NDIS 5.1 legacy type definitions.
 * The main ndis.h already contains all NDIS 5.x types when NDIS_LEGACY_DRIVER
 * is true. This header is provided for:
 *
 * 1. Documentation purposes - clearly shows what constitutes NDIS 5.x API
 * 2. Direct inclusion by drivers that want explicit NDIS 5.x support
 * 3. Future clean separation if ndis.h is refactored
 *
 * When NDIS_LEGACY_DRIVER is already set (via ndis.h), this header adds
 * only convenience macros and doesn't duplicate type definitions.
 */

#ifndef _NDIS5_H_
#define _NDIS5_H_

#pragma once

/* Ensure ndis.h is included first for base types */
#ifndef _NDIS_
#include <ndis.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When included after ndis.h with NDIS_LEGACY_DRIVER=1, all NDIS 5.x
 * types are already defined. This section provides compatibility
 * markers and any supplementary definitions.
 */

/* Mark that NDIS 5.x support header has been included */
#define _NDIS5_SUPPORT_INCLUDED_ 1

#if NDIS_LEGACY_DRIVER

/*
 * NDIS 5.x types available when NDIS_LEGACY_DRIVER is set:
 *
 * Packet types:
 *   NDIS_PACKET, PNDIS_PACKET, PPNDIS_PACKET
 *   NDIS_PACKET_PRIVATE, PNDIS_PACKET_PRIVATE
 *   NDIS_PACKET_POOL, PNDIS_PACKET_POOL
 *   NDIS_PACKET_STACK, PNDIS_PACKET_STACK
 *   NDIS_PACKET_OOB_DATA, PNDIS_PACKET_OOB_DATA
 *   NDIS_PACKET_EXTENSION, PNDIS_PACKET_EXTENSION
 *   NDIS_PACKET_8021Q_INFO, PNDIS_PACKET_8021Q_INFO
 *
 * Request types:
 *   NDIS_REQUEST, PNDIS_REQUEST
 *   NDIS_REQUEST_TYPE, PNDIS_REQUEST_TYPE
 *
 * WAN types:
 *   NDIS_WAN_PACKET, PNDIS_WAN_PACKET
 *
 * Handler types (in NDIS_MINIPORT_CHARACTERISTICS):
 *   W_SEND_HANDLER, W_SEND_PACKETS_HANDLER
 *   W_TRANSFER_DATA_HANDLER, W_RETURN_PACKET_HANDLER
 *   W_ALLOCATE_COMPLETE_HANDLER
 *   W_CO_SEND_PACKETS_HANDLER, W_CO_REQUEST_HANDLER
 *
 * Protocol handler types:
 *   CO_SEND_COMPLETE_HANDLER, CO_RECEIVE_PACKET_HANDLER
 *   CO_REQUEST_HANDLER, CO_REQUEST_COMPLETE_HANDLER
 *   SEND_COMPLETE_HANDLER, TRANSFER_DATA_COMPLETE_HANDLER
 *   REQUEST_COMPLETE_HANDLER, RECEIVE_PACKET_HANDLER
 *
 * Structures:
 *   NDIS_MINIPORT_CHARACTERISTICS (versions 30, 40, 50, 51)
 *   NDIS_MINIPORT_INTERRUPT
 *   NDIS_CALL_MANAGER_CHARACTERISTICS
 *   NDIS_CLIENT_CHARACTERISTICS
 *   NDIS_PROTOCOL_CHARACTERISTICS
 *
 * APIs:
 *   NdisAllocatePacket, NdisFreePacket
 *   NdisAllocatePacketPool, NdisFreePacketPool
 *   NdisQueryPacket, NdisReinitializePacket
 *   NdisMSendComplete, NdisMIndicateReceivePacket
 *   NdisSend, NdisSendPackets, NdisTransferData
 *   NdisReturnPackets, NdisRequest
 */

/* ============================================================================
 * Convenience macros for NDIS 5.x packet manipulation
 * ============================================================================ */

/*
 * NDIS_INIT_PROTOCOL_RESERVED - Initialize protocol reserved area in packet
 */
#ifndef NDIS_INIT_PROTOCOL_RESERVED
#define NDIS_INIT_PROTOCOL_RESERVED(_Packet, _Size) \
    NdisZeroMemory((_Packet)->ProtocolReserved, (_Size))
#endif

/*
 * NDIS_GET_PACKET_CANCEL_ID - Get the cancel ID from a packet
 */
#ifndef NDIS_GET_PACKET_CANCEL_ID
#define NDIS_GET_PACKET_CANCEL_ID(_Packet) \
    NDIS_PER_PACKET_INFO_FROM_PACKET((_Packet), PacketCancelId)
#endif

/*
 * NDIS_SET_PACKET_CANCEL_ID - Set the cancel ID for a packet
 */
#ifndef NDIS_SET_PACKET_CANCEL_ID
#define NDIS_SET_PACKET_CANCEL_ID(_Packet, _CancelId) \
    NDIS_PER_PACKET_INFO_FROM_PACKET((_Packet), PacketCancelId) = (_CancelId)
#endif

/*
 * NDIS_GET_ORIGINAL_PACKET - Get the original packet pointer
 */
#ifndef NDIS_GET_ORIGINAL_PACKET
#define NDIS_GET_ORIGINAL_PACKET(_Packet) \
    ((PNDIS_PACKET)NDIS_PER_PACKET_INFO_FROM_PACKET((_Packet), OriginalPacketInfo))
#endif

/*
 * NDIS_SET_ORIGINAL_PACKET - Set the original packet pointer
 */
#ifndef NDIS_SET_ORIGINAL_PACKET
#define NDIS_SET_ORIGINAL_PACKET(_Packet, _OrigPacket) \
    NDIS_PER_PACKET_INFO_FROM_PACKET((_Packet), OriginalPacketInfo) = (_OrigPacket)
#endif

#else /* !NDIS_LEGACY_DRIVER */

/*
 * NDIS 6.x build - legacy types are not available.
 * Define marker to indicate NDIS 5.x is not supported in this build.
 */
#define _NDIS5_TYPES_NOT_AVAILABLE_ 1

/*
 * For NDIS 6.x drivers that need to compile legacy code conditionally,
 * these macros can be used to stub out NDIS 5.x specific code.
 */
#define NDIS5_ONLY_CODE(code) /* Stubbed out in NDIS 6.x builds */

#endif /* NDIS_LEGACY_DRIVER */

#ifdef __cplusplus
}
#endif

#endif /* _NDIS5_H_ */

/* EOF */
