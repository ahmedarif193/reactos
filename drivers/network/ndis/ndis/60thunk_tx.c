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

    /* Track the wrapper on the in-flight list so HaltEx can wait for
     * outstanding sends to drain before tearing down the adapter.
     * Phase 4 will add the actual drain-on-halt; for now we just
     * maintain the list. We use MiniportReserved[3] as the LIST_ENTRY
     * link to keep MiniportReserved[0..2] free for the original
     * packet pointer + future per-NBL state. */
    {
        PLIST_ENTRY link = (PLIST_ENTRY)&Nbl->MiniportReserved[3];
        link->Flink = link->Blink = NULL;

        KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
        InsertTailList(&Ext->InFlightNblsTx, link);
        KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
    }

    DbgPrint("NDIS6-TX: Ndis6TxSendPacket Adapter=%p Packet=%p Nbl=%p Len=%u\n",
             Adapter, Packet, Nbl, TotalLength);

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
        DbgPrint("NDIS6-TX: TerminalSend Ext=%p DriverBlock=%p Handler=%p — DROPPING\n",
                 Ext, Ext ? Ext->DriverBlock : NULL,
                 (Ext && Ext->DriverBlock) ? Ext->DriverBlock->Characteristics.SendNetBufferListsHandler : NULL);
        return;
    }

    DbgPrint("NDIS6-TX: TerminalSend → driver Nbl=%p\n", NetBufferList);
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

    /* Detach from the in-flight list. The link is at MiniportReserved[3]. */
    link = (PLIST_ENTRY)&NetBufferList->MiniportReserved[3];
    if (link->Flink != NULL && link->Blink != NULL)
    {
        KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
        RemoveEntryList(link);
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

/* EOF */
