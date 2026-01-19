/*
 * ndis6.h
 *
 * NDIS 6.x Types and Definitions - Supplementary Header
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
 * This header provides supplementary NDIS 6.x type definitions.
 * The main ndis.h contains all NDIS 6.x types when NDIS_SUPPORT_NDIS6
 * is true. This header is provided for:
 *
 * 1. Documentation purposes - clearly shows what constitutes NDIS 6.x API
 * 2. Convenience macros for NDIS 6.x development
 * 3. Additional helper definitions
 *
 * All NDIS 6.x types (NET_BUFFER_LIST, NDIS_OID_REQUEST,
 * NDIS_MINIPORT_DRIVER_CHARACTERISTICS, etc.) are now defined in ndis.h.
 */

#ifndef _NDIS6_H_
#define _NDIS6_H_

#pragma once

/* Ensure ndis.h is included first for all types */
#ifndef _NDIS_
#include <ndis.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Mark that NDIS 6.x support header has been included */
#define _NDIS6_SUPPORT_INCLUDED_ 1

#if NDIS_SUPPORT_NDIS6

/*
 * NDIS 6.x types available when NDIS_SUPPORT_NDIS6 is set (defined in ndis.h):
 *
 * Buffer types:
 *   NET_BUFFER, PNET_BUFFER
 *   NET_BUFFER_LIST, PNET_BUFFER_LIST
 *   NET_BUFFER_LIST_CONTEXT, PNET_BUFFER_LIST_CONTEXT
 *
 * Request types:
 *   NDIS_OID_REQUEST, PNDIS_OID_REQUEST
 *
 * Parameter types:
 *   NDIS_MINIPORT_INIT_PARAMETERS
 *   NDIS_MINIPORT_PAUSE_PARAMETERS
 *   NDIS_MINIPORT_RESTART_PARAMETERS
 *   NET_DEVICE_PNP_EVENT
 *
 * Attribute types:
 *   NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES
 *   NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES
 *   NDIS_MINIPORT_ADAPTER_ATTRIBUTES (union)
 *
 * Status types:
 *   NDIS_STATUS_INDICATION
 *   NDIS_LINK_STATE
 *
 * Handler types:
 *   MINIPORT_INITIALIZE, MINIPORT_HALT
 *   MINIPORT_PAUSE, MINIPORT_RESTART
 *   MINIPORT_OID_REQUEST, MINIPORT_CANCEL_OID_REQUEST
 *   MINIPORT_SEND_NET_BUFFER_LISTS
 *   MINIPORT_RETURN_NET_BUFFER_LISTS
 *   MINIPORT_CANCEL_SEND
 *   MINIPORT_CHECK_FOR_HANG, MINIPORT_RESET
 *   MINIPORT_DEVICE_PNP_EVENT_NOTIFY
 *   MINIPORT_SHUTDOWN, MINIPORT_UNLOAD
 *
 * Interrupt handler types:
 *   MINIPORT_ISR (MINIPORT_INTERRUPT_HANDLER)
 *   MINIPORT_INTERRUPT_DPC
 *   MINIPORT_ENABLE_INTERRUPT, MINIPORT_DISABLE_INTERRUPT
 *   MINIPORT_SYNCHRONIZE_INTERRUPT
 *
 * Structures:
 *   NDIS_MINIPORT_DRIVER_CHARACTERISTICS
 *   NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS
 *   NDIS_SG_DMA_DESCRIPTION
 *   NET_BUFFER_LIST_POOL_PARAMETERS
 *
 * Enumerations:
 *   NDIS_HALT_ACTION, NDIS_SHUTDOWN_ACTION
 *   NDIS_INTERRUPT_TYPE
 *   NDIS_MEDIA_CONNECT_STATE, NDIS_MEDIA_DUPLEX_STATE
 *   NDIS_SUPPORTED_PAUSE_FUNCTIONS
 *
 * APIs:
 *   NdisRegisterMiniportDriver, NdisDeregisterMiniportDriver
 *   NdisMSetMiniportAttributes
 *   NdisMRegisterInterruptEx, NdisMDeregisterInterruptEx
 *   NdisMSynchronizeWithInterruptEx
 *   NdisMRegisterScatterGatherDma, NdisMDeregisterScatterGatherDma
 *   NdisAllocateNetBufferListPool, NdisFreeNetBufferListPool
 *   NdisAllocateNetBufferList, NdisFreeNetBufferList
 *   NdisMSendNetBufferListsComplete, NdisMIndicateReceiveNetBufferLists
 *   NdisMOidRequestComplete, NdisMIndicateStatusEx
 *   NdisMPauseComplete, NdisMRestartComplete
 */

/* ============================================================================
 * NDIS 6.x Convenience Macros
 * ============================================================================ */

/*
 * NET_BUFFER_LIST macros (ensure they're defined)
 */
#ifndef NET_BUFFER_LIST_FIRST_NB
#define NET_BUFFER_LIST_FIRST_NB(_NBL)          ((_NBL)->FirstNetBuffer)
#endif

#ifndef NET_BUFFER_LIST_NEXT_NBL
#define NET_BUFFER_LIST_NEXT_NBL(_NBL)          ((_NBL)->Next)
#endif

#ifndef NET_BUFFER_LIST_STATUS
#define NET_BUFFER_LIST_STATUS(_NBL)            ((_NBL)->Status)
#endif

#ifndef NET_BUFFER_LIST_INFO
#define NET_BUFFER_LIST_INFO(_NBL, _Id)         ((_NBL)->NetBufferListInfo[(_Id)])
#endif

/*
 * NET_BUFFER macros (ensure they're defined)
 */
#ifndef NET_BUFFER_NEXT_NB
#define NET_BUFFER_NEXT_NB(_NB)                 ((_NB)->Next)
#endif

#ifndef NET_BUFFER_FIRST_MDL
#define NET_BUFFER_FIRST_MDL(_NB)               ((_NB)->MdlChain)
#endif

#ifndef NET_BUFFER_CURRENT_MDL
#define NET_BUFFER_CURRENT_MDL(_NB)             ((_NB)->CurrentMdl)
#endif

#ifndef NET_BUFFER_CURRENT_MDL_OFFSET
#define NET_BUFFER_CURRENT_MDL_OFFSET(_NB)      ((_NB)->CurrentMdlOffset)
#endif

#ifndef NET_BUFFER_DATA_LENGTH
#define NET_BUFFER_DATA_LENGTH(_NB)             ((_NB)->DataLength)
#endif

#ifndef NET_BUFFER_DATA_OFFSET
#define NET_BUFFER_DATA_OFFSET(_NB)             ((_NB)->DataOffset)
#endif

/*
 * NDIS_INIT_OBJECT_HEADER - Initialize NDIS object header
 */
#ifndef NDIS_INIT_OBJECT_HEADER
#define NDIS_INIT_OBJECT_HEADER(_Header, _Type, _Revision, _Size) \
    do { \
        (_Header)->Type = (_Type); \
        (_Header)->Revision = (_Revision); \
        (_Header)->Size = (_Size); \
    } while (0)
#endif

/*
 * Check dispatch level macros
 */
#ifndef NDIS_TEST_SEND_FLAG
#define NDIS_TEST_SEND_FLAG(_Flags, _Flag) (((_Flags) & (_Flag)) != 0)
#endif

#ifndef NDIS_TEST_SEND_AT_DISPATCH_LEVEL
#define NDIS_TEST_SEND_AT_DISPATCH_LEVEL(_Flags) \
    NDIS_TEST_SEND_FLAG((_Flags), NDIS_SEND_FLAGS_DISPATCH_LEVEL)
#endif

#ifndef NDIS_TEST_RECEIVE_FLAG
#define NDIS_TEST_RECEIVE_FLAG(_Flags, _Flag) (((_Flags) & (_Flag)) != 0)
#endif

#ifndef NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL
#define NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(_Flags) \
    NDIS_TEST_RECEIVE_FLAG((_Flags), NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL)
#endif

#ifndef NDIS_TEST_RECEIVE_CANNOT_PEND
#define NDIS_TEST_RECEIVE_CANNOT_PEND(_Flags) \
    NDIS_TEST_RECEIVE_FLAG((_Flags), NDIS_RECEIVE_FLAGS_RESOURCES)
#endif

/*
 * NDIS_SIZEOF macros for variable-sized structures
 */
#ifndef NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1
#define NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_MINIPORT_DRIVER_CHARACTERISTICS, CancelOidRequestHandler)
#endif

#ifndef NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1
#define NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES, InterfaceType)
#endif

#else /* !NDIS_SUPPORT_NDIS6 */

/*
 * NDIS 5.x build - NDIS 6.x types are not available.
 * Define marker to indicate NDIS 6.x is not supported in this build.
 */
#define _NDIS6_TYPES_NOT_AVAILABLE_ 1

/*
 * For drivers that need to compile NDIS 6.x code conditionally,
 * these macros can be used to stub out NDIS 6.x specific code.
 */
#define NDIS6_ONLY_CODE(code) /* Stubbed out in NDIS 5.x builds */

#endif /* NDIS_SUPPORT_NDIS6 */

#ifdef __cplusplus
}
#endif

#endif /* _NDIS6_H_ */

/* EOF */
