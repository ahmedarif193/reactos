/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60filter.c
 * PURPOSE:     NDIS 6 filter driver datapath chain walk.
 *
 *              Filters attach to an adapter via NdisFRegisterFilterDriver
 *              + AttachHandler. Once attached, every TX, send-complete,
 *              receive, and return-NBL operation must walk the per-adapter
 *              filter chain in order:
 *
 *                  TX:           protocol -> top filter -> ... -> bottom filter -> miniport
 *                  Send-Cmpl:    miniport -> bottom -> ... -> top -> protocol
 *                  RX:           miniport -> bottom -> ... -> top -> protocol
 *                  Return-NBL:   protocol -> top -> ... -> bottom -> miniport
 *
 *              The bridge models the chain as Adapter->NDIS6_ADAPTER_EXT->
 *              FilterModuleList. List head = topmost filter (closest to
 *              the protocol), list tail = bottommost (closest to the
 *              miniport). Each filter sees the bridge as "the layer above
 *              and below"; the bridge multiplexes all NdisF* helper calls
 *              into a chain walk.
 *
 *              The handle the bridge passes to the filter at AttachHandler
 *              time is (NDIS_HANDLE)PNDIS6_FILTER_MODULE. Filters round-
 *              trip that handle in every NdisF* helper call so we can
 *              find their position in the chain.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS 5↔6
 *              filter framework work (Phase 8 of the bridge plan).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

/* MiniIndicateReceivePacket forward decl — same as 60thunk_rx.c. */
extern VOID NTAPI
MiniIndicateReceivePacket(
    IN NDIS_HANDLE   MiniportAdapterHandle,
    IN PPNDIS_PACKET PacketArray,
    IN UINT          NumberOfPackets);

/* ============================================================================
 *  Chain helpers
 * ============================================================================ */

/* Returns TRUE if the per-adapter filter chain has at least one entry. */
static BOOLEAN
Ndis6FilterChainHasEntries(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;
    BOOLEAN Empty;

    if (Ext == NULL)
        return FALSE;

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    Empty = IsListEmpty(&Ext->FilterModuleList);
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

    return !Empty;
}

/* Get the topmost filter module on this adapter (head of list).
 * Returns NULL if no filters are attached. */
static PNDIS6_FILTER_MODULE
Ndis6FilterChainTop(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;
    PNDIS6_FILTER_MODULE Module = NULL;

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    if (!IsListEmpty(&Ext->FilterModuleList))
    {
        Module = CONTAINING_RECORD(Ext->FilterModuleList.Flink,
                                   NDIS6_FILTER_MODULE, ListEntry);
    }
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    return Module;
}

/* Get the bottommost filter module on this adapter (tail of list).
 * Returns NULL if no filters are attached. */
static PNDIS6_FILTER_MODULE
Ndis6FilterChainBottom(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;
    PNDIS6_FILTER_MODULE Module = NULL;

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    if (!IsListEmpty(&Ext->FilterModuleList))
    {
        Module = CONTAINING_RECORD(Ext->FilterModuleList.Blink,
                                   NDIS6_FILTER_MODULE, ListEntry);
    }
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    return Module;
}

/* Walk one step downward in the chain (TX direction). Returns the next
 * filter, or NULL if Module is at the tail. */
static PNDIS6_FILTER_MODULE
Ndis6FilterChainNextDown(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;
    PNDIS6_FILTER_MODULE Next = NULL;

    if (Module == NULL || Module->Adapter == NULL)
        return NULL;

    Ext = NDIS6_EXT(Module->Adapter);
    if (Ext == NULL)
        return NULL;

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    if (Module->ListEntry.Flink != &Ext->FilterModuleList)
    {
        Next = CONTAINING_RECORD(Module->ListEntry.Flink,
                                 NDIS6_FILTER_MODULE, ListEntry);
    }
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    return Next;
}

/* Walk one step upward in the chain (RX/return direction). Returns the
 * previous filter, or NULL if Module is at the head. */
static PNDIS6_FILTER_MODULE
Ndis6FilterChainNextUp(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;
    PNDIS6_FILTER_MODULE Prev = NULL;

    if (Module == NULL || Module->Adapter == NULL)
        return NULL;

    Ext = NDIS6_EXT(Module->Adapter);
    if (Ext == NULL)
        return NULL;

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    if (Module->ListEntry.Blink != &Ext->FilterModuleList)
    {
        Prev = CONTAINING_RECORD(Module->ListEntry.Blink,
                                 NDIS6_FILTER_MODULE, ListEntry);
    }
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    return Prev;
}

/* ============================================================================
 *  Bridge dispatch entry points — called by the bridge's TX/RX glue
 *  (60thunk_tx.c, 60thunk_rx.c) to inject NBLs into the filter chain.
 *  If no filters are attached the bridge calls the terminal handler
 *  directly; the chain walk only happens when at least one filter is
 *  installed.
 * ============================================================================ */

VOID
Ndis6FilterDispatchSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Top;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ndis6FilterChainHasEntries(Ext))
    {
        /* No filters — go straight to the miniport. */
        Ndis6FilterTerminalSend(Adapter, NetBufferList);
        return;
    }

    Top = Ndis6FilterChainTop(Ext);
    if (Top == NULL || Top->DriverBlock == NULL ||
        Top->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        /* Filter doesn't implement send — skip it. The chain walk inside
         * NdisFSendNetBufferLists handles further filters; for the
         * topmost being inert we just skip directly to the miniport. */
        Ndis6FilterTerminalSend(Adapter, NetBufferList);
        return;
    }

    Top->DriverBlock->Characteristics.SendNetBufferListsHandler(
        Top->FilterModuleContext,
        NetBufferList,
        0,                  /* PortNumber */
        0);                 /* SendFlags */
}

VOID
Ndis6FilterDispatchSendComplete(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG            SendCompleteFlags)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Bottom;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ndis6FilterChainHasEntries(Ext))
    {
        Ndis6FilterTerminalSendComplete(Adapter, NetBufferList, SendCompleteFlags);
        return;
    }

    Bottom = Ndis6FilterChainBottom(Ext);
    if (Bottom == NULL || Bottom->DriverBlock == NULL ||
        Bottom->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler == NULL)
    {
        Ndis6FilterTerminalSendComplete(Adapter, NetBufferList, SendCompleteFlags);
        return;
    }

    Bottom->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(
        Bottom->FilterModuleContext,
        NetBufferList,
        SendCompleteFlags);
}

VOID
Ndis6FilterDispatchReceive(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferLists,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             NumberOfNetBufferLists,
    _In_ ULONG             ReceiveFlags)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Bottom;

    if (Adapter == NULL || NetBufferLists == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ndis6FilterChainHasEntries(Ext))
    {
        Ndis6FilterTerminalReceive(Adapter, NetBufferLists, PortNumber,
                                   NumberOfNetBufferLists, ReceiveFlags);
        return;
    }

    Bottom = Ndis6FilterChainBottom(Ext);
    if (Bottom == NULL || Bottom->DriverBlock == NULL ||
        Bottom->DriverBlock->Characteristics.ReceiveNetBufferListsHandler == NULL)
    {
        Ndis6FilterTerminalReceive(Adapter, NetBufferLists, PortNumber,
                                   NumberOfNetBufferLists, ReceiveFlags);
        return;
    }

    Bottom->DriverBlock->Characteristics.ReceiveNetBufferListsHandler(
        Bottom->FilterModuleContext,
        NetBufferLists,
        PortNumber,
        NumberOfNetBufferLists,
        ReceiveFlags);
}

VOID
Ndis6FilterDispatchReturn(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             ReturnFlags)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Top;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ndis6FilterChainHasEntries(Ext))
    {
        Ndis6FilterTerminalReturn(Adapter, NetBufferList, ReturnFlags);
        return;
    }

    Top = Ndis6FilterChainTop(Ext);
    if (Top == NULL || Top->DriverBlock == NULL ||
        Top->DriverBlock->Characteristics.ReturnNetBufferListsHandler == NULL)
    {
        Ndis6FilterTerminalReturn(Adapter, NetBufferList, ReturnFlags);
        return;
    }

    Top->DriverBlock->Characteristics.ReturnNetBufferListsHandler(
        Top->FilterModuleContext,
        NetBufferList,
        ReturnFlags);
}

/* ============================================================================
 *  NdisF* helper API — what filters call to push NBLs through the chain
 *
 *  In each helper, NdisFilterHandle is the (NDIS_HANDLE)Module pointer the
 *  bridge gave the filter at AttachHandler time. The filter rounds-trips
 *  it back so we can locate the filter's position in the chain and
 *  forward to the next adjacent filter (or to the terminal handler).
 * ============================================================================ */

VOID
NTAPI
NdisFSendNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             SendFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    if (Module == NULL || Module->Adapter == NULL || NetBufferList == NULL)
        return;

    Next = Ndis6FilterChainNextDown(Module);
    if (Next != NULL && Next->DriverBlock != NULL &&
        Next->DriverBlock->Characteristics.SendNetBufferListsHandler != NULL)
    {
        Next->DriverBlock->Characteristics.SendNetBufferListsHandler(
            Next->FilterModuleContext,
            NetBufferList,
            PortNumber,
            SendFlags);
        return;
    }

    /* End of chain — hand off to the bridge's terminal send (which calls
     * the miniport's SendNetBufferListsHandler). */
    Ndis6FilterTerminalSend(Module->Adapter, NetBufferList);
}

VOID
NTAPI
NdisFSendNetBufferListsComplete(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             SendCompleteFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Prev;

    if (Module == NULL || Module->Adapter == NULL || NetBufferList == NULL)
        return;

    Prev = Ndis6FilterChainNextUp(Module);
    if (Prev != NULL && Prev->DriverBlock != NULL &&
        Prev->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
    {
        Prev->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(
            Prev->FilterModuleContext,
            NetBufferList,
            SendCompleteFlags);
        return;
    }

    /* Top of chain — terminal send-complete: routes to MiniSendComplete
     * which wakes the legacy NDIS 5 protocol's SendCompleteHandler. */
    Ndis6FilterTerminalSendComplete(Module->Adapter, NetBufferList, SendCompleteFlags);
}

VOID
NTAPI
NdisFIndicateReceiveNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             NumberOfNetBufferLists,
    _In_ ULONG             ReceiveFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Prev;

    if (Module == NULL || Module->Adapter == NULL || NetBufferList == NULL)
        return;

    Prev = Ndis6FilterChainNextUp(Module);
    if (Prev != NULL && Prev->DriverBlock != NULL &&
        Prev->DriverBlock->Characteristics.ReceiveNetBufferListsHandler != NULL)
    {
        Prev->DriverBlock->Characteristics.ReceiveNetBufferListsHandler(
            Prev->FilterModuleContext,
            NetBufferList,
            PortNumber,
            NumberOfNetBufferLists,
            ReceiveFlags);
        return;
    }

    /* Top of chain — indicate to legacy protocols via the RX terminal. */
    Ndis6FilterTerminalReceive(Module->Adapter, NetBufferList, PortNumber,
                               NumberOfNetBufferLists, ReceiveFlags);
}

VOID
NTAPI
NdisFReturnNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             ReturnFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;

    if (Module == NULL || Module->Adapter == NULL || NetBufferList == NULL)
        return;

    Next = Ndis6FilterChainNextDown(Module);
    if (Next != NULL && Next->DriverBlock != NULL &&
        Next->DriverBlock->Characteristics.ReturnNetBufferListsHandler != NULL)
    {
        Next->DriverBlock->Characteristics.ReturnNetBufferListsHandler(
            Next->FilterModuleContext,
            NetBufferList,
            ReturnFlags);
        return;
    }

    /* Bottom of chain — return NBL to the miniport. */
    Ndis6FilterTerminalReturn(Module->Adapter, NetBufferList, ReturnFlags);
}

NDIS_STATUS
NTAPI
NdisFSetAttributes(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_opt_ NDIS_HANDLE   FilterModuleContext,
    _In_opt_ PVOID         AttributeData)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;

    UNREFERENCED_PARAMETER(AttributeData);

    if (Module == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Module->FilterModuleContext = FilterModuleContext;
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  Filter OID forward
 *
 *  Ndis6FilterDispatchOidRequest — entry point from 60oid.c when an OID
 *  request needs to walk the filter chain on the way to the miniport.
 *  Calls the topmost filter's OidRequestHandler; the filter does its
 *  inspect/modify work and calls NdisFOidRequest to push the OID down
 *  to the next filter (or to the miniport at the bottom).
 *
 *  Synchronous only — if any filter returns PENDING the bridge currently
 *  demotes to NOT_SUPPORTED. Async filter OID completion needs per-OID
 *  context tracking that we'd add when a real consumer demands it.
 * ============================================================================ */

NDIS_STATUS
Ndis6FilterDispatchOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Top;

    if (Adapter == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ndis6FilterChainHasEntries(Ext))
    {
        /* No filters — straight to the miniport. */
        return Ndis6FilterTerminalOidRequest(Adapter, OidRequest);
    }

    Top = Ndis6FilterChainTop(Ext);
    if (Top == NULL || Top->DriverBlock == NULL ||
        Top->DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        return Ndis6FilterTerminalOidRequest(Adapter, OidRequest);
    }

    return Top->DriverBlock->Characteristics.OidRequestHandler(
        Top->FilterModuleContext, OidRequest);
}

NDIS_STATUS
Ndis6FilterTerminalOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    return Ext->DriverBlock->Characteristics.OidRequestHandler(
        Ext->MiniportAdapterContext, OidRequest);
}

NDIS_STATUS
NTAPI
NdisFOidRequest(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;

    if (Module == NULL || Module->Adapter == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Next = Ndis6FilterChainNextDown(Module);
    if (Next != NULL && Next->DriverBlock != NULL &&
        Next->DriverBlock->Characteristics.OidRequestHandler != NULL)
    {
        return Next->DriverBlock->Characteristics.OidRequestHandler(
            Next->FilterModuleContext, OidRequest);
    }

    return Ndis6FilterTerminalOidRequest(Module->Adapter, OidRequest);
}

VOID
NTAPI
NdisFOidRequestComplete(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Prev;

    if (Module == NULL || Module->Adapter == NULL || OidRequest == NULL)
        return;

    Prev = Ndis6FilterChainNextUp(Module);
    if (Prev != NULL && Prev->DriverBlock != NULL &&
        Prev->DriverBlock->Characteristics.OidRequestCompleteHandler != NULL)
    {
        Prev->DriverBlock->Characteristics.OidRequestCompleteHandler(
            Prev->FilterModuleContext, OidRequest, Status);
        return;
    }

    /* Top of chain — completion needs to reach the original waiter or
     * the legacy NdisRequest caller. The bridge's NdisMOidRequestComplete
     * already handles the legacy case via OidRequest->RequestId pointing
     * at NDIS6_OID_WAITER. Forward there. */
    NdisMOidRequestComplete((NDIS_HANDLE)Module->Adapter, OidRequest, Status);
}

/* EOF */
