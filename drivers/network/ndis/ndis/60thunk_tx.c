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
 *              from Ext->TxWrapperNblPool, stash the original packet pointer
 *              in MiniportReserved[0] for the send-complete callback to
 *              recover, track the wrapper on Ext->InFlightNblsTx so HaltEx
 *              can drain pending sends, and hand the NBL to the miniport's
 *              SendNetBufferListsHandler.
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
    PNET_BUFFER_LIST    Nbl;
    PNDIS_BUFFER        FirstBuffer;
    UINT                TotalLength;
    KIRQL               OldIrql;

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

    /* Stash the original NDIS_PACKET in MiniportReserved[0] so the
     * send-complete callback can recover it. NDIS contracts say
     * MiniportReserved is owned by NDIS (which is us, the bridge),
     * not the miniport — the miniport touches its own ScratchPad and
     * the per-NBL Context block. */
    Nbl->MiniportReserved[0] = Packet;

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
     * Use MiniportReserved[3] as the LIST_ENTRY link to keep
     * MiniportReserved[0..2] free for the original packet pointer +
     * future per-NBL state. A1: also increment TxInFlightCount and
     * clear the drain event so HaltEx will actually wait. */
    {
        PLIST_ENTRY link = (PLIST_ENTRY)&Nbl->MiniportReserved[3];
        link->Flink = link->Blink = NULL;

        KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
        InsertTailList(&Ext->InFlightNblsTx, link);
        InterlockedIncrement(&Ext->TxInFlightCount);
        KeClearEvent(&Ext->TxDrainEvent);
        KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
    }

    /* Phase 8: route through the filter chain. If no filters are
     * attached, Ndis6FilterDispatchSend short-circuits straight to
     * Ndis6FilterTerminalSend, which calls the miniport's
     * SendNetBufferListsHandler. With filters attached, each filter's
     * SendNetBufferListsHandler runs in turn before the miniport. */
    Ndis6FilterDispatchSend(Adapter, Nbl);

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
    _In_ PNET_BUFFER_LIST NetBufferList)
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

    Ext->DriverBlock->Characteristics.SendNetBufferListsHandler(
        Ext->MiniportAdapterContext,
        NetBufferList,
        0,                      /* PortNumber */
        0);                     /* SendFlags */
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
    PNDIS_PACKET       Packet;
    NDIS_STATUS        Status;
    PLIST_ENTRY        link;
    KIRQL              OldIrql;

    UNREFERENCED_PARAMETER(SendCompleteFlags);

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    Packet = (PNDIS_PACKET)NetBufferList->MiniportReserved[0];
    Status = NET_BUFFER_LIST_STATUS(NetBufferList);

    /* Detach from the in-flight list. The link is at MiniportReserved[3].
     * A1: decrement TxInFlightCount; if it hits zero, signal the drain
     * event so HaltEx can finish waiting. */
    link = (PLIST_ENTRY)&NetBufferList->MiniportReserved[3];
    if (link->Flink != NULL && link->Blink != NULL)
    {
        LONG NewCount;
        KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
        RemoveEntryList(link);
        NewCount = InterlockedDecrement(&Ext->TxInFlightCount);
        if (NewCount == 0)
            KeSetEvent(&Ext->TxDrainEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
    }

    /* Free the wrapper NBL. The MDL chain it pointed at is owned by
     * the original legacy NDIS_PACKET and stays alive until
     * MiniSendComplete returns. */
    NdisFreeNetBufferList(NetBufferList);

    /* Wake up the legacy protocol via the existing MiniSendComplete
     * machinery. MiniSendComplete walks back to the binding (stored
     * in Packet->Reserved[1] by the legacy ProSend at protocol.c:508)
     * and calls its SendCompleteHandler. */
    if (Packet != NULL)
        MiniSendComplete(Adapter, Packet, Status);
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
    KIRQL               OldIrql;

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
        PLIST_ENTRY      link;

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

        Nbl->MiniportReserved[0] = Packet;
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

        link = (PLIST_ENTRY)&Nbl->MiniportReserved[3];
        link->Flink = link->Blink = NULL;
        KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
        InsertTailList(&Ext->InFlightNblsTx, link);
        InterlockedIncrement(&Ext->TxInFlightCount);
        KeClearEvent(&Ext->TxDrainEvent);
        KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);

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
        Ndis6FilterDispatchSend(Adapter, HeadNbl);
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
NTAPI
NdisMCancelSendNetBufferLists(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PVOID       CancelId)
{
    PLOGICAL_ADAPTER    Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
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
        /* The LIST_ENTRY link is at MiniportReserved[3] — back-compute the
         * enclosing NBL. sizeof(PVOID) is the stride of MiniportReserved. */
        PNET_BUFFER_LIST Nbl = (PNET_BUFFER_LIST)
            ((PUCHAR)entry - offsetof(NET_BUFFER_LIST, MiniportReserved) -
             3 * sizeof(PVOID));

        if (NET_BUFFER_LIST_INFO(Nbl, NetBufferListCancelId) == CancelId)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SEND_ABORTED;
        }
    }
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

VOID
NTAPI
NdisCancelSendNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PVOID       CancelId)
{
    /* The binding handle the bridge hands to native NDIS 6 protocols
     * doubles as the adapter pointer (see NdisOpenAdapterEx). Forward
     * to the miniport-cancellation path. */
    NdisMCancelSendNetBufferLists(NdisBindingHandle, CancelId);
}

/* EOF */
