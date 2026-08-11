/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60thunk_tx.c
 * PURPOSE:     NDIS 5 -> NDIS 6 send thunk for legacy protocols on
 *              NDIS 6 miniports.
 *
 *              When tcpip.sys (or any other legacy NDIS 5 protocol) calls
 *              NdisSend on an adapter that is actually an NDIS 6 miniport
 *              (e1000e, virtio-net 6.x, ...), the legacy proSendPacketToMiniport
 *              path would dereference Adapter->NdisMiniportBlock.DriverHandle
 *              which is NULL on NDIS 6 adapters and crash. The IsNdis6 gate
 *              in protocol.c forwards into Ndis6TxSendPacket here instead.
 *
 *              We wrap the legacy NDIS_PACKET (a chain of NDIS_BUFFER, which
 *              are themselves PMDLs) in a freshly allocated NET_BUFFER_LIST
 *              from Ext->TxWrapperNblPool, store bridge-owned state in
 *              Nbl->NdisReserved[1], track the wrapper on Ext->InFlightNblsTx
 *              so HaltEx can drain pending sends, and hand the NBL to the
 *              miniport's SendNetBufferListsHandler.
 *
 *              When the miniport completes the send by calling
 *              NdisMSendNetBufferListsComplete, we walk back to the original
 *              NDIS_PACKET, remove the wrapper from the in-flight list, free
 *              the wrapper NBL, and call MiniSendComplete to signal the
 *              legacy protocol's SendCompleteHandler.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS 5↔6
 *              datapath thunking work (Phase 3 of the bridge plan).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

/* MiniSendComplete is declared in miniport.h (pulled in via ndis6_internal.h)
 * and is the canonical way to wake up a legacy NDIS 5 protocol's
 * SendCompleteHandler. */

#define NDIS6_TX_WRAPPER_TAG 'wTNn'

typedef struct _NDIS6_TX_WRAPPER_CONTEXT
{
    LIST_ENTRY       Link;
    PNET_BUFFER_LIST Nbl;
    PNDIS_PACKET     Packet;
    ULONG_PTR        BindingHandle;
} NDIS6_TX_WRAPPER_CONTEXT, *PNDIS6_TX_WRAPPER_CONTEXT;

BOOLEAN
Ndis6ReferenceNativeTransmit(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;
    BOOLEAN Referenced = FALSE;

    if (Ext == NULL)
        return FALSE;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    if (Ext->TxAccepting)
    {
        if (Ext->TxInFlightCount++ == 0)
            KeClearEvent(&Ext->TxDrainEvent);
        Referenced = TRUE;
    }
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);

    return Referenced;
}

VOID
Ndis6DereferenceTransmit(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    ASSERT(Ext->TxInFlightCount > 0);
    if (--Ext->TxInFlightCount == 0)
        KeSetEvent(&Ext->TxDrainEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

static BOOLEAN
Ndis6TrackTransmitWrapper(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNDIS6_TX_WRAPPER_CONTEXT TxContext)
{
    KIRQL OldIrql;
    BOOLEAN Tracked = FALSE;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    if (Ext->TxAccepting)
    {
        InsertTailList(&Ext->InFlightNblsTx, &TxContext->Link);
        if (Ext->TxInFlightCount++ == 0)
            KeClearEvent(&Ext->TxDrainEvent);
        Tracked = TRUE;
    }
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);

    return Tracked;
}

static VOID
Ndis6UntrackTransmitWrapper(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNDIS6_TX_WRAPPER_CONTEXT TxContext)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    RemoveEntryList(&TxContext->Link);
    TxContext->Link.Flink = NULL;
    TxContext->Link.Blink = NULL;
    ASSERT(Ext->TxInFlightCount > 0);
    if (--Ext->TxInFlightCount == 0)
        KeSetEvent(&Ext->TxDrainEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

/* ============================================================================
 *  Ndis6TxSendPacket — wraps an NDIS_PACKET in an NBL and sends it to the
 *  NDIS 6 miniport. Called from protocol.c:proSendPacketToMiniport via the
 *  IsNdis6 gate.
 *
 *  Returns:
 *    NDIS_STATUS_PENDING — the wrapped NBL has been handed off; the legacy
 *      protocol will eventually be woken via MiniSendComplete.
 *    NDIS_STATUS_RESOURCES — wrapper allocation failed; the protocol should
 *      treat the packet as failed (legacy ProSend handles this case).
 *    NDIS_STATUS_FAILURE — invalid arguments or driver has no send handler.
 * ============================================================================ */

NDIS_STATUS
Ndis6TxSendPacket(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_PACKET     Packet)
{
    PNDIS6_ADAPTER_EXT  Ext;
    PNDIS6_TX_WRAPPER_CONTEXT TxContext;
    PNET_BUFFER_LIST    Nbl;
    PNDIS_BUFFER        FirstBuffer;
    UINT                TotalLength;

    if (Adapter == NULL || Packet == NULL)
        return NDIS_STATUS_FAILURE;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->TxWrapperNblPool == NULL ||
        Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        return NDIS_STATUS_FAILURE;
    }

    /* Walk the legacy NDIS_PACKET to get the head NDIS_BUFFER (PMDL) and
     * the total payload length. NDIS_BUFFER is just an MDL, so the chain
     * is already in the format NET_BUFFER expects. */
    NdisQueryPacket(Packet, NULL, NULL, &FirstBuffer, &TotalLength);
    if (FirstBuffer == NULL || TotalLength == 0)
        return NDIS_STATUS_FAILURE;

    /* Allocate the wrapper NBL from our per-adapter pool. The NB is
     * embedded in the NBL allocation (pool was created with
     * fAllocateNetBuffer = TRUE in 60adapter.c). DataOffset = 0 because
     * NDIS_BUFFER MDLs always start at the data, and DataLength is the
     * full packet payload. */
    Nbl = NdisAllocateNetBufferAndNetBufferList(
        Ext->TxWrapperNblPool,
        0,                      /* ContextSize */
        0,                      /* ContextBackFill */
        (PMDL)FirstBuffer,      /* MdlChain */
        0,                      /* DataOffset */
        TotalLength);

    if (Nbl == NULL)
        return NDIS_STATUS_RESOURCES;

    TxContext = ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(*TxContext),
                                      NDIS6_TX_WRAPPER_TAG);
    if (TxContext == NULL)
    {
        NdisFreeNetBufferList(Nbl);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(TxContext, sizeof(*TxContext));
    TxContext->Nbl = Nbl;
    TxContext->Packet = Packet;
    TxContext->BindingHandle = Packet->Reserved[1];
    Nbl->NdisReserved[1] = TxContext;

    /* B2: copy the legacy packet's checksum/LSO offload info onto the
     * wrapper NBL's NetBufferListInfo[] so the miniport sees the same
     * offload requests whether the send came from a legacy protocol
     * or a native NDIS 6 protocol. The bit layouts of
     * NDIS_TCP_IP_CHECKSUM_PACKET_INFO.Transmit and
     * NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO.Transmit are identical
     * for the bottom 5 bits (IPv4/IPv6/TCP/UDP/IP header), so a straight
     * PVOID copy does the job. Same for RX. */
    {
        PVOID ChecksumValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
            Packet, TcpIpChecksumPacketInfo);
        if (ChecksumValue != NULL)
            NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) = ChecksumValue;

        /* Large-send / LSO — same pattern, same layout. */
        {
            PVOID LsoValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, TcpLargeSendPacketInfo);
            if (LsoValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, TcpLargeSendNetBufferListInfo) = LsoValue;
        }

        /* D1: VLAN tag. Legacy IEEE_8021Q_INFO and NBL
         * Ieee8021QNetBufferListInfo share the same union-over-PVOID
         * layout (TCI + canonical format bits). Copy through. */
        {
            PVOID VlanValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, Ieee8021QInfo);
            if (VlanValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, Ieee8021QNetBufferListInfo) = VlanValue;
        }
    }

    /* Track the wrapper on the in-flight list so HaltEx can wait for
     * outstanding sends to drain before tearing down the adapter.
     * A1: also increment TxInFlightCount and clear the drain event so
     * HaltEx will actually wait. */
    if (!Ndis6TrackTransmitWrapper(Ext, TxContext))
    {
        Nbl->NdisReserved[1] = NULL;
        ExFreePoolWithTag(TxContext, NDIS6_TX_WRAPPER_TAG);
        NdisFreeNetBufferList(Nbl);
        return NDIS_STATUS_PAUSED;
    }

    /* Phase 8: route through the filter chain. If no filters are
     * attached, Ndis6FilterDispatchSend short-circuits straight to
     * Ndis6FilterTerminalSend, which calls the miniport's
     * SendNetBufferListsHandler. With filters attached, each filter's
     * SendNetBufferListsHandler runs in turn before the miniport. */
    Ndis6FilterDispatchSend(Adapter, Nbl, 0, 0);

    /* Either way the call is asynchronous from the legacy protocol's
     * perspective — the SendCompleteHandler will eventually fire via
     * MiniSendComplete. */
    return NDIS_STATUS_PENDING;
}

/* ============================================================================
 *  Ndis6FilterTerminalSend — bottom-of-chain TX handler. Called by
 *  Ndis6FilterDispatchSend (when no filters are attached) and by
 *  NdisFSendNetBufferLists (when the bottommost filter's call falls
 *  off the end of the chain). Hands the NBL to the miniport.
 * ============================================================================ */

VOID
Ndis6FilterTerminalSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        return;
    }

    Ext->DriverBlock->Characteristics.SendNetBufferListsHandler(Ext->MiniportAdapterContext, NetBufferList, PortNumber, SendFlags);
}

/* ============================================================================
 *  NdisMSendNetBufferListsComplete — driver-side completion callback.
 *
 *  Called by the NDIS 6 miniport (or its TX completion DPC) when one or more
 *  wrapper NBLs have been transmitted. Walks the chain, recovers the original
 *  NDIS_PACKET pointer from each NBL, removes from the in-flight list, frees
 *  the wrapper NBL, and calls MiniSendComplete to signal the legacy protocol.
 *
 *  This replaces the no-op stub that lived in 60stubs.c.
 * ============================================================================ */

VOID
NTAPI
NdisMSendNetBufferListsComplete(
    _In_ NDIS_HANDLE      NdisMiniportHandle,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG            SendCompleteFlags)
{
    PLOGICAL_ADAPTER  Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNET_BUFFER_LIST  CurrentNbl;
    PNET_BUFFER_LIST  NextNbl;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    /* Phase 8: walk the chain in reverse order — bottommost filter sees
     * the completion first, then up through each filter, finally to
     * Ndis6FilterTerminalSendComplete which routes to MiniSendComplete.
     *
     * The driver may complete a chain of NBLs in one call. We split the
     * chain so each NBL gets its own walk through the filter stack —
     * filters are allowed to reorder, drop, or hold NBLs independently. */
    for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        Ndis6FilterDispatchSendComplete(Adapter, CurrentNbl, SendCompleteFlags);
    }
}

/* ============================================================================
 *  Ndis6FilterTerminalSendComplete — top-of-chain TX completion handler.
 *  Called by Ndis6FilterDispatchSendComplete when the chain is empty,
 *  and by NdisFSendNetBufferListsComplete when the topmost filter's
 *  call falls off the head of the chain. Recovers the original
 *  NDIS_PACKET from the wrapper NBL, frees the wrapper, and signals
 *  the legacy protocol via MiniSendComplete.
 * ============================================================================ */

VOID
Ndis6FilterTerminalSendComplete(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG            SendCompleteFlags)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_TX_WRAPPER_CONTEXT TxContext;
    PNDIS_PACKET       Packet;
    ULONG_PTR          BindingHandle;
    NDIS_STATUS        Status;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    /* Native sends carry the referenced binding in NdisReserved[1] until
     * completion. The binding and adapter TX references remain held across
     * the protocol callback, so close/halt cannot free either owner here. */
    {
        PNDIS6_PROTOCOL_BINDING Binding =
            (PNDIS6_PROTOCOL_BINDING)NetBufferList->SourceHandle;

        if (Binding != NULL && NetBufferList->NdisReserved[1] == Binding)
        {
            NetBufferList->NdisReserved[1] = NULL;
            NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
            if (Binding->DriverBlock != NULL && Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, NetBufferList, SendCompleteFlags);
            Ndis6DereferenceTransmit(Ext);
            Ndis6DereferenceProtocolBinding(Binding);
            return;
        }
    }

    /* Legacy bridge wrapper completion (tcpip.sys path). */
    TxContext = (PNDIS6_TX_WRAPPER_CONTEXT)NetBufferList->NdisReserved[1];
    Packet = TxContext ? TxContext->Packet : NULL;
    BindingHandle = TxContext ? TxContext->BindingHandle : 0;
    Status = NET_BUFFER_LIST_STATUS(NetBufferList);

    /* Detach from the in-flight list. A1: decrement TxInFlightCount; if it
     * hits zero, signal the drain event so HaltEx can finish waiting. */
    if (TxContext != NULL &&
        TxContext->Link.Flink != NULL &&
        TxContext->Link.Blink != NULL)
        Ndis6UntrackTransmitWrapper(Ext, TxContext);

    /* Free the wrapper NBL. The MDL chain it pointed at is owned by
     * the original legacy NDIS_PACKET and stays alive until
     * MiniSendComplete returns. */
    NetBufferList->NdisReserved[1] = NULL;
    NdisFreeNetBufferList(NetBufferList);

    /* Wake up the legacy protocol via the existing MiniSendComplete
     * machinery. Restore Packet->Reserved[1] because the legacy packet
     * is owned by tcpip and may have been reused internally while the
     * NDIS 6 miniport held the wrapper NBL. */
    if (Packet != NULL && BindingHandle != 0)
    {
        Packet->Reserved[1] = BindingHandle;
        MiniSendComplete(Adapter, Packet, Status);
    }
    else
        DbgPrint("NDIS6-TX: dropping send completion with missing binding packet=%p\n",
                 Packet);

    if (TxContext != NULL)
        ExFreePoolWithTag(TxContext, NDIS6_TX_WRAPPER_TAG);
}

/* ============================================================================
 *  B1: Ndis6TxSendPackets — batch send entry
 *
 *  Called from protocol.c:NDIS_SendPackets when a legacy protocol hands
 *  us an array of NDIS_PACKETs. Wrap each in an NBL, chain them via
 *  NET_BUFFER_LIST_NEXT_NBL, and hand the whole chain to the miniport's
 *  SendNetBufferListsHandler in ONE call. Saves the per-packet call
 *  overhead and lets the driver batch descriptor fills on its TX ring.
 *
 *  Packets that fail to wrap are completed immediately via MiniSendComplete
 *  with NDIS_STATUS_RESOURCES; the caller sees the Private.Flags status
 *  and won't wait for them.
 * ============================================================================ */

ULONG
Ndis6TxSendPackets(
    _In_                         PLOGICAL_ADAPTER Adapter,
    _In_reads_(NumberOfPackets)  PPNDIS_PACKET    PacketArray,
    _In_                         UINT             NumberOfPackets)
{
    PNDIS6_ADAPTER_EXT  Ext;
    PNET_BUFFER_LIST    HeadNbl = NULL;
    PNET_BUFFER_LIST    TailNbl = NULL;
    ULONG               Wrapped = 0;
    UINT                i;

    if (Adapter == NULL || PacketArray == NULL || NumberOfPackets == 0)
        return 0;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->TxWrapperNblPool == NULL ||
        Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        /* Complete everything with FAILURE so the caller doesn't hang. */
        for (i = 0; i < NumberOfPackets; i++)
            MiniSendComplete(Adapter, PacketArray[i], NDIS_STATUS_FAILURE);
        return 0;
    }

    for (i = 0; i < NumberOfPackets; i++)
    {
        PNDIS_PACKET     Packet = PacketArray[i];
        PNDIS_BUFFER     FirstBuffer;
        UINT             TotalLength;
        PNET_BUFFER_LIST Nbl;
        PNDIS6_TX_WRAPPER_CONTEXT TxContext;

        if (Packet == NULL)
            continue;

        NdisQueryPacket(Packet, NULL, NULL, &FirstBuffer, &TotalLength);
        if (FirstBuffer == NULL || TotalLength == 0)
        {
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_FAILURE);
            continue;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(
            Ext->TxWrapperNblPool,
            0, 0, (PMDL)FirstBuffer, 0, TotalLength);
        if (Nbl == NULL)
        {
            /* Out of wrapper NBLs — drop with RESOURCES so the caller
             * can retry. */
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_RESOURCES);
            continue;
        }

        TxContext = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(*TxContext),
                                          NDIS6_TX_WRAPPER_TAG);
        if (TxContext == NULL)
        {
            NdisFreeNetBufferList(Nbl);
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_RESOURCES);
            continue;
        }

        RtlZeroMemory(TxContext, sizeof(*TxContext));
        TxContext->Nbl = Nbl;
        TxContext->Packet = Packet;
        TxContext->BindingHandle = Packet->Reserved[1];
        Nbl->NdisReserved[1] = TxContext;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        /* B2 + D1: carry offload request + VLAN info across. */
        {
            PVOID ChecksumValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, TcpIpChecksumPacketInfo);
            if (ChecksumValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) = ChecksumValue;
        }
        {
            PVOID LsoValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, TcpLargeSendPacketInfo);
            if (LsoValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, TcpLargeSendNetBufferListInfo) = LsoValue;
        }
        {
            PVOID VlanValue = NDIS_PER_PACKET_INFO_FROM_PACKET(
                Packet, Ieee8021QInfo);
            if (VlanValue != NULL)
                NET_BUFFER_LIST_INFO(Nbl, Ieee8021QNetBufferListInfo) = VlanValue;
        }

        if (!Ndis6TrackTransmitWrapper(Ext, TxContext))
        {
            Nbl->NdisReserved[1] = NULL;
            ExFreePoolWithTag(TxContext, NDIS6_TX_WRAPPER_TAG);
            NdisFreeNetBufferList(Nbl);
            MiniSendComplete(Adapter, Packet, NDIS_STATUS_PAUSED);
            continue;
        }

        /* Chain onto the batch. */
        if (HeadNbl == NULL)
        {
            HeadNbl = Nbl;
            TailNbl = Nbl;
        }
        else
        {
            NET_BUFFER_LIST_NEXT_NBL(TailNbl) = Nbl;
            TailNbl = Nbl;
        }
        Wrapped++;
    }

    if (HeadNbl != NULL)
    {
        /* Route through the filter chain. With no filters attached, this
         * goes straight to Ndis6FilterTerminalSend which hands the chain
         * to SendNetBufferListsHandler in one call. */
        Ndis6FilterDispatchSend(Adapter, HeadNbl, 0, 0);
    }

    return Wrapped;
}

/* ============================================================================
 *  A5: NdisMCancelSendNetBufferLists / NdisCancelSendNetBufferLists
 *
 *  The caller (protocol or miniport) tags in-flight sends with a
 *  CancelId via NDIS_SET_NET_BUFFER_LIST_CANCEL_ID; this call walks
 *  the in-flight list and marks matching NBLs with NDIS_STATUS_SEND_ABORTED
 *  so the driver's send-completion path sees the status and drops them.
 *  We DO NOT forcibly complete the NBL here because the driver still
 *  owns the MDLs; we just leave a marker and let the driver's own DPC
 *  finish the job.
 * ============================================================================ */

VOID
Ndis6FilterTerminalCancelSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PVOID       CancelId)
{
    PNDIS6_ADAPTER_EXT  Ext;
    PLIST_ENTRY         entry;
    KIRQL               OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    /* Forward to the driver's CancelSendHandler if it has one. The driver
     * is responsible for actually cancelling; the bridge just delivers
     * the notification. */
    if (Ext->DriverBlock != NULL &&
        Ext->DriverBlock->Characteristics.CancelSendHandler != NULL &&
        Ext->MiniportAdapterContext != NULL)
    {
        Ext->DriverBlock->Characteristics.CancelSendHandler(
            Ext->MiniportAdapterContext, CancelId);
    }

    /* Also walk the bridge's in-flight list and mark any wrapper NBL
     * whose original NDIS_PACKET carries the matching CancelId. The
     * driver's completion path will see NDIS_STATUS_SEND_ABORTED on
     * the NBL and route the cancellation back to the protocol. */
    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    for (entry = Ext->InFlightNblsTx.Flink;
         entry != &Ext->InFlightNblsTx;
         entry = entry->Flink)
    {
        PNDIS6_TX_WRAPPER_CONTEXT TxContext =
            CONTAINING_RECORD(entry, NDIS6_TX_WRAPPER_CONTEXT, Link);
        PNET_BUFFER_LIST Nbl = TxContext->Nbl;

        if (Nbl != NULL &&
            NET_BUFFER_LIST_INFO(Nbl, NetBufferListCancelId) == CancelId)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;
        }
    }
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

VOID
NTAPI
NdisMCancelSendNetBufferLists(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PVOID CancelId)
{
    Ndis6FilterTerminalCancelSend((PLOGICAL_ADAPTER)NdisMiniportHandle, CancelId);
}

VOID
NTAPI
NdisCancelSendNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PVOID       CancelId)
{
    PNDIS6_PROTOCOL_BINDING Binding = NdisBindingHandle;
    PLOGICAL_ADAPTER Adapter;

    if (!Ndis6ReferenceProtocolBinding(Binding))
        return;

    Adapter = Binding->Adapter;
    if (Adapter != NULL && Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL)
        Ndis6FilterDispatchCancelSend(Adapter, CancelId);

    Ndis6DereferenceProtocolBinding(Binding);
}

/* EOF */
