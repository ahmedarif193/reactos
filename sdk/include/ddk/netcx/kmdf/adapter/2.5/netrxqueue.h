/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx 2.5 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NETRXQUEUE_2_5_H_
#define _NETRXQUEUE_2_5_H_

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

#include <netpacketqueue.h>

struct _NETRXQUEUE_INIT;
typedef struct _NETRXQUEUE_INIT NETRXQUEUE_INIT;

typedef struct _NET_RXQUEUE_BUFFER_LAYOUT_HINT
{

    ULONG MinimumBackfillSize;

    ULONG L3HeaderAlignment;

} NET_RXQUEUE_BUFFER_LAYOUT_HINT;

inline
void
NET_RX_QUEUE_CONFIG_INIT_EXECUTION_CONTEXT(
    _Out_ NET_PACKET_QUEUE_CONFIG * Config,
    _In_ NETEXECUTIONCONTEXT ExecutionContext,
    _In_  PFN_PACKET_QUEUE_ADVANCE EvtAdvance,
    _In_  PFN_PACKET_QUEUE_CANCEL EvtCancel
)
{
    NET_PACKET_QUEUE_CONFIG_INIT(Config, EvtAdvance, NULL, EvtCancel);
    Config->ExecutionContext = ExecutionContext;
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_NETRXQUEUECREATE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _Inout_
    NETRXQUEUE_INIT* NetRxQueueInit,
    _In_opt_
    WDF_OBJECT_ATTRIBUTES* RxQueueAttributes,
    _In_
    NET_PACKET_QUEUE_CONFIG* Configuration,
    _Out_
    NETPACKETQUEUE* PacketQueue
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
NetRxQueueCreate(
    _Inout_
    NETRXQUEUE_INIT* NetRxQueueInit,
    _In_opt_
    WDF_OBJECT_ATTRIBUTES* RxQueueAttributes,
    _In_
    NET_PACKET_QUEUE_CONFIG* Configuration,
    _Out_
    NETPACKETQUEUE* PacketQueue
    )
{
    return ((PFN_NETRXQUEUECREATE) NetFunctions[NetRxQueueCreateTableIndex])(NetDriverGlobals, NetRxQueueInit, RxQueueAttributes, Configuration, PacketQueue);
}

typedef
_IRQL_requires_max_(HIGH_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETRXQUEUENOTIFYMORERECEIVEDPACKETSAVAILABLE)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETPACKETQUEUE PacketQueue
    );

_IRQL_requires_max_(HIGH_LEVEL)
FORCEINLINE
void
NetRxQueueNotifyMoreReceivedPacketsAvailable(
    _In_
    NETPACKETQUEUE PacketQueue
    )
{
    ((PFN_NETRXQUEUENOTIFYMORERECEIVEDPACKETSAVAILABLE) NetFunctions[NetRxQueueNotifyMoreReceivedPacketsAvailableTableIndex])(NetDriverGlobals, PacketQueue);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_NETRXQUEUEINITGETQUEUEID)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETRXQUEUE_INIT* NetRxQueueInit
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
ULONG
NetRxQueueInitGetQueueId(
    _In_
    NETRXQUEUE_INIT* NetRxQueueInit
    )
{
    return ((PFN_NETRXQUEUEINITGETQUEUEID) NetFunctions[NetRxQueueInitGetQueueIdTableIndex])(NetDriverGlobals, NetRxQueueInit);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
const NET_RING_COLLECTION *
(NTAPI *PFN_NETRXQUEUEGETRINGCOLLECTION)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETPACKETQUEUE PacketQueue
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
const NET_RING_COLLECTION *
NetRxQueueGetRingCollection(
    _In_
    NETPACKETQUEUE PacketQueue
    )
{
    return ((PFN_NETRXQUEUEGETRINGCOLLECTION) NetFunctions[NetRxQueueGetRingCollectionTableIndex])(NetDriverGlobals, PacketQueue);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
void
(NTAPI *PFN_NETRXQUEUEGETEXTENSION)(
    _In_
    PNET_DRIVER_GLOBALS DriverGlobals,
    _In_
    NETPACKETQUEUE PacketQueue,
    _In_
    CONST NET_EXTENSION_QUERY* Query,
    _Out_
    NET_EXTENSION* Extension
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
void
NetRxQueueGetExtension(
    _In_
    NETPACKETQUEUE PacketQueue,
    _In_
    CONST NET_EXTENSION_QUERY* Query,
    _Out_
    NET_EXTENSION* Extension
    )
{
    ((PFN_NETRXQUEUEGETEXTENSION) NetFunctions[NetRxQueueGetExtensionTableIndex])(NetDriverGlobals, PacketQueue, Query, Extension);
}

WDF_EXTERN_C_END

#endif
