/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60thunk_rx.c
 * PURPOSE:     NDIS 6 -> NDIS 5 receive thunk + status indication.
 *
 *              When an NDIS 6 miniport (e1000e, virtio-net 6.x, ...) calls
 *              NdisMIndicateReceiveNetBufferLists to push received frames up
 *              the stack, the bound legacy NDIS 5 protocol (tcpip.sys) is
 *              expecting NDIS_PACKETs not NBLs. This file translates each
 *              incoming NB into a wrapper NDIS_PACKET pulled from a per-
 *              adapter pool, indicates them to the legacy protocol via the
 *              existing MiniIndicateReceivePacket machinery, and tracks the
 *              outstanding refcount per NBL so the NBL can be returned to
 *              the miniport once all wrappers are released.
 *
 *              Status indications follow the same pattern: when the
 *              miniport calls NdisMIndicateStatusEx, we translate the NDIS 6
 *              NDIS_STATUS_INDICATION (e.g. NDIS_STATUS_LINK_STATE) into
 *              a legacy NDIS_STATUS code (NDIS_STATUS_MEDIA_CONNECT/
 *              MEDIA_DISCONNECT), update the cached GeneralAttrs in the
 *              adapter extension so subsequent OID queries reflect the new
 *              state, and walk the protocol list calling each protocol's
 *              StatusHandler/StatusCompleteHandler.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS 5↔6
 *              datapath thunking work (Phase 3 of the bridge plan).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

/* MiniIndicateReceivePacket lives in the legacy library (miniport.c) but
 * has no public header. Forward-declare so we can call it from the RX
 * indication path. */
extern VOID NTAPI
MiniIndicateReceivePacket(
    IN NDIS_HANDLE   MiniportAdapterHandle,
    IN PPNDIS_PACKET PacketArray,
    IN UINT          NumberOfPackets);

/* ============================================================================
 *  Ndis6RxBuildLegacyPacket — wrap a NET_BUFFER in a legacy NDIS_PACKET.
 *
 *  The wrapper is allocated from Ext->RxLegacyPacketPool, with a single
 *  NDIS_BUFFER chained on that re-uses the NB's CurrentMdl directly (no
 *  copy). The original NBL pointer is stashed in Reserved[0] so the
 *  return path can find it; the Ext is stashed in Reserved[1]. The legacy
 *  WrapperReserved[0] starts at zero and is incremented by the existing
 *  protocol-bind logic in MiniIndicateReceivePacket.
 *
 *  Returns NULL on allocation failure (caller drops the NB).
 * ============================================================================ */

static PNDIS_PACKET
Ndis6RxBuildLegacyPacket(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNET_BUFFER_LIST   Nbl,
    _In_ PNET_BUFFER        Nb,
    _In_ BOOLEAN            ResourcesFlag)
{
    PNDIS_PACKET Packet     = NULL;
    PNDIS_BUFFER FirstBuffer = NULL;
    PNDIS_BUFFER PrevBuffer = NULL;
    NDIS_STATUS  Status;
    PMDL         CurrentMdl;
    ULONG        DataOffset;
    ULONG        DataLength;
    ULONG        Remaining;
    ULONG        MdlSegmentSize;
    ULONG        MdlSegmentOffset;
    ULONG        ChainedMdls = 0;
    /* Note: RxLegacyBufferPool is always NULL because NdisAllocateBufferPool
     * is a no-op stub in the legacy library — NdisAllocateBuffer ignores
     * the handle and just calls IoAllocateMdl. Only the packet pool needs
     * to be non-NULL. */
    if (Ext->RxLegacyPacketPool == NULL)
        return NULL;

    CurrentMdl = NET_BUFFER_CURRENT_MDL(Nb);
    DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(Nb);
    DataLength = NET_BUFFER_DATA_LENGTH(Nb);

    if (CurrentMdl == NULL || DataLength == 0)
        return NULL;

    NdisAllocatePacket(&Status, &Packet, Ext->RxLegacyPacketPool);
    if (Status != NDIS_STATUS_SUCCESS || Packet == NULL)
        return NULL;

    /* A2: walk the NET_BUFFER MDL chain and chain a PNDIS_BUFFER per MDL.
     * The first MDL starts at NET_BUFFER_CURRENT_MDL_OFFSET; subsequent
     * MDLs start at offset 0. The total of all segments must equal
     * DataLength — we stop when Remaining hits 0. Works for single-MDL
     * NBs (e1000e) AND multi-MDL NBs (jumbo frames, virtio-net, etc.). */
    Remaining        = DataLength;
    MdlSegmentOffset = DataOffset;
    while (CurrentMdl != NULL && Remaining > 0)
    {
        ULONG MdlSize = MmGetMdlByteCount(CurrentMdl);
        PNDIS_BUFFER NdisBuffer = NULL;

        /* Clamp the MDL segment to what's available in this MDL AND
         * what's remaining in the total payload. */
        if (MdlSegmentOffset >= MdlSize)
        {
            /* Offset puts us past this MDL — shouldn't normally happen
             * for first MDL (CurrentMdl is CURRENT_MDL by definition),
             * but defensively skip to the next MDL and try again. */
            MdlSegmentOffset = 0;
            CurrentMdl = CurrentMdl->Next;
            continue;
        }
        MdlSegmentSize = MdlSize - MdlSegmentOffset;
        if (MdlSegmentSize > Remaining)
            MdlSegmentSize = Remaining;

        NdisAllocateBuffer(&Status,
                           &NdisBuffer,
                           Ext->RxLegacyBufferPool,
                           (PUCHAR)MmGetMdlVirtualAddress(CurrentMdl) + MdlSegmentOffset,
                           MdlSegmentSize);
        if (Status != NDIS_STATUS_SUCCESS || NdisBuffer == NULL)
        {
            /* Tear down the partial chain we built before this failure,
             * then free the packet itself. */
            {
                PNDIS_BUFFER toFree = FirstBuffer;
                while (toFree != NULL)
                {
                    PNDIS_BUFFER next = NDIS_BUFFER_LINKAGE(toFree);
                    NdisFreeBuffer(toFree);
                    toFree = next;
                }
            }
            NdisFreePacket(Packet);
            return NULL;
        }

        if (FirstBuffer == NULL)
        {
            FirstBuffer = NdisBuffer;
        }
        else
        {
            /* Chain after the previous buffer — legacy NDIS_BUFFER is just
             * an MDL so NDIS_BUFFER_LINKAGE is the MDL Next pointer. */
            NDIS_BUFFER_LINKAGE(PrevBuffer) = NdisBuffer;
        }
        PrevBuffer = NdisBuffer;
        ChainedMdls++;

        Remaining       -= MdlSegmentSize;
        MdlSegmentOffset = 0;              /* second and later MDLs start at 0 */
        CurrentMdl       = CurrentMdl->Next;
    }

    if (FirstBuffer == NULL || Remaining > 0)
    {
        {
            PNDIS_BUFFER toFree = FirstBuffer;
            while (toFree != NULL)
            {
                PNDIS_BUFFER next = NDIS_BUFFER_LINKAGE(toFree);
                NdisFreeBuffer(toFree);
                toFree = next;
            }
        }
        NdisFreePacket(Packet);
        return NULL;
    }

    NdisChainBufferAtFront(Packet, FirstBuffer);

    /* Stash NBL backptr + Ext so Ndis6RxReturnLegacyPacket can find them. */
    Packet->Reserved[0] = (ULONG_PTR)Nbl;
    Packet->Reserved[1] = (ULONG_PTR)Ext;

    /* B2: translate RX offload result from NBL info to legacy packet info.
     * NDIS_TCP_IP_CHECKSUM_PACKET_INFO.Receive and
     * NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO.Receive share the same
     * bit layout — raw PVOID copy carries it across. */
    {
        PVOID ChecksumValue =
            NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo);
        if (ChecksumValue != NULL)
            NDIS_PER_PACKET_INFO_FROM_PACKET(Packet, TcpIpChecksumPacketInfo) = ChecksumValue;
    }

    /* D1: translate RX VLAN tag. Same union-over-PVOID layout in
     * both NDIS 5 IEEE_8021Q_INFO and NDIS 6 Ieee8021QNetBufferListInfo. */
    {
        PVOID VlanValue =
            NET_BUFFER_LIST_INFO(Nbl, Ieee8021QNetBufferListInfo);
        if (VlanValue != NULL)
            NDIS_PER_PACKET_INFO_FROM_PACKET(Packet, Ieee8021QInfo) = VlanValue;
    }

    /* Initial refcount of zero — protocols increment as they hold the
     * packet across async work. */
    Packet->WrapperReserved[0] = 0;

    NDIS_SET_PACKET_STATUS(Packet,
        ResourcesFlag ? NDIS_STATUS_RESOURCES : NDIS_STATUS_SUCCESS);
    NDIS_SET_PACKET_HEADER_SIZE(Packet, 14);  /* Ethernet II */

    return Packet;
}

/* ============================================================================
 *  Ndis6RxFreeLegacyPacket — return a wrapper NDIS_PACKET to the pool.
 *
 *  Frees the NDIS_BUFFER and the NDIS_PACKET. Does NOT touch the underlying
 *  NB MDL — that belongs to the original NBL.
 * ============================================================================ */

static VOID
Ndis6RxFreeLegacyPacket(
    _In_ PNDIS_PACKET Packet)
{
    PNDIS_BUFFER NdisBuffer;

    /* A2: walk the chain — there may be more than one NDIS_BUFFER if the
     * original NET_BUFFER spanned multiple MDLs. NdisUnchainBufferAtFront
     * pops the head; NDIS_BUFFER_LINKAGE lets us walk the rest. */
    NdisUnchainBufferAtFront(Packet, &NdisBuffer);
    while (NdisBuffer != NULL)
    {
        PNDIS_BUFFER Next = NDIS_BUFFER_LINKAGE(NdisBuffer);
        NDIS_BUFFER_LINKAGE(NdisBuffer) = NULL;
        NdisFreeBuffer(NdisBuffer);
        NdisBuffer = Next;
    }

    NdisFreePacket(Packet);
}

/* ============================================================================
 *  Ndis6RxReturnLegacyPacket — called from miniport.c:NdisReturnPackets via
 *  the IsNdis6 gate when a legacy protocol releases a held wrapper packet.
 *
 *  Decrements the per-NBL refcount stored in NBL->ChildRefCount; when
 *  it hits zero, returns the entire NBL to the miniport via its
 *  ReturnNetBufferListsHandler.
 * ============================================================================ */

VOID
Ndis6RxReturnLegacyPacket(
    _In_ PNDIS_PACKET Packet)
{
    PNET_BUFFER_LIST    Nbl;
    PNDIS6_ADAPTER_EXT  Ext;
    ULONG_PTR           RemainingRefs;

    if (Packet == NULL)
        return;

    Nbl = (PNET_BUFFER_LIST)Packet->Reserved[0];
    Ext = (PNDIS6_ADAPTER_EXT)Packet->Reserved[1];

    Ndis6RxFreeLegacyPacket(Packet);

    if (Nbl == NULL || Ext == NULL || Ext->Adapter == NULL)
        return;

    /* ChildRefCount is NDIS-owned. MiniportReserved has only two entries and
     * belongs to the miniport, so using MiniportReserved[2] corrupted the
     * following NET_BUFFER_LIST fields on 64-bit builds. */
    RemainingRefs = (ULONG_PTR)InterlockedDecrement(&Nbl->ChildRefCount);

    if (RemainingRefs == 0)
    {
        /* All wrappers released. Hand the NBL back to the miniport via
         * the filter chain so any attached filter's ReturnNBL handler
         * runs in TX (top→bottom) order before the miniport sees it. */
        Ndis6FilterDispatchReturn(Ext->Adapter, Nbl, 0);
    }
}

/* ============================================================================
 *  NdisMIndicateReceiveNetBufferLists — driver-side receive entry point.
 *
 *  The NDIS 6 miniport calls this when its RX DPC has packets to indicate.
 *  We walk the chain, build wrapper NDIS_PACKETs, count them per NBL, and
 *  call MiniIndicateReceivePacket for each batch. Replaces the no-op stub
 *  that lived in 60stubs.c.
 * ============================================================================ */

VOID
NTAPI
NdisMIndicateReceiveNetBufferLists(
    _In_ NDIS_HANDLE      NdisMiniportHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG            NumberOfNetBufferLists,
    _In_ ULONG            ReceiveFlags)
{
    PLOGICAL_ADAPTER  Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNET_BUFFER_LIST  CurrentNbl;
    PNET_BUFFER_LIST  NextNbl;
    if (Adapter == NULL || !Adapter->IsNdis6 || NetBufferLists == NULL)
        return;

    /* Phase 8: walk the chain bottom→top — bottommost filter sees the
     * RX first, then up through each filter, finally to the terminal
     * which wraps NBs in legacy NDIS_PACKETs and indicates to bound
     * NDIS 5 protocols.
     *
     * The driver may indicate a chain of NBLs in one call. Filters can
     * reorder/drop independently, so we split the chain so each NBL
     * walks the filter stack on its own. */
    UNREFERENCED_PARAMETER(NextNbl);
    UNREFERENCED_PARAMETER(CurrentNbl);
    Ndis6FilterDispatchReceive(Adapter, NetBufferLists, PortNumber, NumberOfNetBufferLists, ReceiveFlags);
}

/* ============================================================================
 *  Ndis6FilterTerminalReceive — top-of-chain RX handler. Called by
 *  Ndis6FilterDispatchReceive when no filters are attached, and by
 *  NdisFIndicateReceiveNetBufferLists when the topmost filter's call
 *  reaches the head of the chain. Wraps each NB in a legacy NDIS_PACKET
 *  and indicates them to the bound NDIS 5 protocols.
 * ============================================================================ */

VOID
Ndis6FilterTerminalReceive(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG            NumberOfNetBufferLists,
    _In_ ULONG            ReceiveFlags)
{
    PNDIS6_ADAPTER_EXT  Ext;
    PNET_BUFFER_LIST    CurrentNbl;
    PNET_BUFFER_LIST    NextNbl;
    BOOLEAN             ResourcesFlag;

    if (Adapter == NULL || !Adapter->IsNdis6 || NetBufferLists == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    /* If a native NDIS 6 protocol is bound, deliver the NBLs (still
     * miniport-owned) straight to its ReceiveNetBufferListsHandler instead of
     * the legacy NDIS_PACKET wrap. Indicate outside the lock since the handler
     * may re-enter NDIS (e.g. NdisReturnNetBufferLists); only a single native
     * binding is supported — multi-binding fan-out would need NBL clones. */
    {
#define NDIS6_RX_BINDING_SNAPSHOT_MAX 8
        struct
        {
            PNDIS6_PROTOCOL_BINDING                   Binding;
            PROTOCOL_RECEIVE_NET_BUFFER_LISTS_HANDLER ReceiveHandler;
            NDIS_HANDLE                               Context;
        } Snapshot[NDIS6_RX_BINDING_SNAPSHOT_MAX];
        UINT  SnapCount = 0;
        UINT  i;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
        {
            PLIST_ENTRY entry;
            for (entry = Ext->ProtocolBindingList.Flink;
                 entry != &Ext->ProtocolBindingList &&
                     SnapCount < NDIS6_RX_BINDING_SNAPSHOT_MAX;
                 entry = entry->Flink)
            {
                PNDIS6_PROTOCOL_BINDING Binding =
                    CONTAINING_RECORD(entry, NDIS6_PROTOCOL_BINDING, AdapterLink);
                if (Binding->DriverBlock != NULL &&
                    Binding->DriverBlock->Characteristics.ReceiveNetBufferListsHandler != NULL &&
                    Ndis6ReferenceProtocolBinding(Binding))
                {
                    Snapshot[SnapCount].Binding        = Binding;
                    Snapshot[SnapCount].ReceiveHandler =
                        Binding->DriverBlock->Characteristics.ReceiveNetBufferListsHandler;
                    Snapshot[SnapCount].Context        = Binding->ProtocolBindingContext;
                    SnapCount++;
                }
            }
        }
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);

        if (SnapCount > 0)
        {
            for (i = 0; i < SnapCount; i++)
            {
                Snapshot[i].ReceiveHandler(
                    Snapshot[i].Context,
                    NetBufferLists,
                    PortNumber,
                    NumberOfNetBufferLists,
                    ReceiveFlags);
                Ndis6DereferenceProtocolBinding(Snapshot[i].Binding);
            }
            return;
        }
    }

    ResourcesFlag = (ReceiveFlags & NDIS_RECEIVE_FLAGS_RESOURCES) != 0;

    if (ResourcesFlag)
    {
        PNDIS_PACKET BatchArray[16];
        UINT         BatchCount = 0;
        UINT         j;

        for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            PNET_BUFFER Nb;

            for (Nb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
            {
                PNDIS_PACKET LegacyPacket = Ndis6RxBuildLegacyPacket(Ext, CurrentNbl, Nb, TRUE);
                if (LegacyPacket == NULL)
                    continue;
                BatchArray[BatchCount++] = LegacyPacket;
                if (BatchCount == ARRAYSIZE(BatchArray))
                {
                    MiniIndicateReceivePacket((NDIS_HANDLE)Adapter, BatchArray, BatchCount);
                    for (j = 0; j < BatchCount; j++)
                        Ndis6RxFreeLegacyPacket(BatchArray[j]);
                    BatchCount = 0;
                }
            }
        }

        if (BatchCount != 0)
        {
            MiniIndicateReceivePacket((NDIS_HANDLE)Adapter, BatchArray, BatchCount);
            for (j = 0; j < BatchCount; j++)
                Ndis6RxFreeLegacyPacket(BatchArray[j]);
        }

        /* The miniport retains ownership when RESOURCES is set and reclaims
         * the original NBL chain as soon as its indication call returns.
         * NDIS must never invoke MiniportReturnNetBufferLists for this path. */
        return;
    }

    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        PNET_BUFFER  Nb;
        PNDIS_PACKET PacketArray[16];
        UINT         PacketCount = 0;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        for (Nb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
             Nb != NULL && PacketCount < ARRAYSIZE(PacketArray);
             Nb = NET_BUFFER_NEXT_NB(Nb))
        {
            PNDIS_PACKET LegacyPacket =
                Ndis6RxBuildLegacyPacket(Ext, CurrentNbl, Nb, ResourcesFlag);
            if (LegacyPacket != NULL)
                PacketArray[PacketCount++] = LegacyPacket;
        }

        if (PacketCount == 0)
        {
            /* Nothing to indicate — return the NBL immediately via the
             * filter chain (TX direction). */
            if (!ResourcesFlag)
                Ndis6FilterDispatchReturn(Adapter, CurrentNbl, 0);
            continue;
        }

        /* Stash the per-NBL outstanding refcount BEFORE indicating, so
         * Ndis6RxReturnLegacyPacket can decrement it as wrappers come back. */
        InterlockedExchange(&CurrentNbl->ChildRefCount, (LONG)PacketCount);

        /* Push the wrapper packets up the legacy protocol chain. The
         * existing MiniIndicateReceivePacket increments WrapperReserved[0]
         * for each protocol that holds the packet. Packets that no
         * protocol holds (refcount stays 0) need to be freed immediately
         * and counted as one fewer outstanding NBL ref. */
        MiniIndicateReceivePacket((NDIS_HANDLE)Adapter, PacketArray, PacketCount);

        /* B3: RESOURCES path — the miniport needs the NBL back before
         * we return, and protocols are contractually required to copy
         * the data inline (they cannot hold the wrapper). So we free
         * every wrapper immediately WITHOUT touching the per-NBL
         * refcount, then return the NBL once through the filter chain.
         * This is a fast path that skips the refcount bookkeeping. */
        if (ResourcesFlag)
        {
            UINT j;
            for (j = 0; j < PacketCount; j++)
            {
                /* Direct free — does not touch the NBL refcount. */
                Ndis6RxFreeLegacyPacket(PacketArray[j]);
            }
            Ndis6FilterDispatchReturn(Adapter, CurrentNbl, 0);
            continue;
        }

        /* Normal path: walk the array; for unheld wrappers (refcount==0),
         * free now and decrement the per-NBL refcount. For held wrappers,
         * the protocol will eventually call NdisReturnPackets which routes
         * through the IsNdis6 gate to Ndis6RxReturnLegacyPacket. */
        {
            UINT j;
            for (j = 0; j < PacketCount; j++)
            {
                if (PacketArray[j]->WrapperReserved[0] == 0)
                {
                    /* No protocol held it — free wrapper and drop the
                     * outstanding-NBL ref. Last decrement returns NBL. */
                    Ndis6RxReturnLegacyPacket(PacketArray[j]);
                }
            }
        }
    }
}

/* ============================================================================
 *  Ndis6FilterTerminalReturn — bottom-of-chain return-NBL handler. Called
 *  by Ndis6FilterDispatchReturn when no filters are attached, and by
 *  NdisFReturnNetBufferLists when the bottommost filter's call falls off
 *  the end of the chain. Hands the NBL back to the miniport's
 *  ReturnNetBufferListsHandler.
 * ============================================================================ */

VOID
Ndis6FilterTerminalReturn(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG            ReturnFlags)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.ReturnNetBufferListsHandler == NULL)
    {
        return;
    }

    NET_BUFFER_LIST_NEXT_NBL(NetBufferList) = NULL;
    Ext->DriverBlock->Characteristics.ReturnNetBufferListsHandler(
        Ext->MiniportAdapterContext, NetBufferList, ReturnFlags);
}

/* ============================================================================
 *  NdisMIndicateStatusEx — driver-side status indication entry point.
 *
 *  Translates the NDIS 6 NDIS_STATUS_INDICATION (NDIS_STATUS_LINK_STATE,
 *  NDIS_STATUS_MEDIA_CONNECT, etc.) into a legacy NDIS_STATUS, updates the
 *  cached GeneralAttrs in Ext (so subsequent cached OID queries reflect the
 *  new state — fixes the boot-time spurious MEDIA_DISCONNECT line), and
 *  walks the bound-protocol list calling each protocol's StatusHandler +
 *  StatusCompleteHandler.
 *
 *  Replaces the no-op stub that lived in 60stubs.c.
 * ============================================================================ */

VOID
NTAPI
NdisMIndicateStatusEx(
    _In_ NDIS_HANDLE             NdisMiniportHandle,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PLOGICAL_ADAPTER   Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS        LegacyStatus = 0;
    PVOID              LegacyBuffer = NULL;
    UINT               LegacyBufferSize = 0;
    PLIST_ENTRY        Entry;
    KIRQL              OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6 || StatusIndication == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    /* Hand native NDIS 6 protocols the raw NDIS_STATUS_INDICATION via
     * StatusHandlerEx before the legacy translation switch below, which drops
     * several codes (all NDIS_STATUS_DOT11_*) and returns early. Call the
     * handlers outside the lock so they can re-enter NDIS. */
    {
#define NDIS6_STATUSEX_SNAPSHOT_MAX 8
        struct
        {
            PNDIS6_PROTOCOL_BINDING Binding;
            NDIS_HANDLE                Context;
            PROTOCOL_STATUS_EX_HANDLER StatusHandlerEx;
        } Snapshot[NDIS6_STATUSEX_SNAPSHOT_MAX];
        UINT  SnapCount = 0;
        UINT  i;
        KIRQL NativeIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &NativeIrql);
        {
            PLIST_ENTRY entry;
            for (entry = Ext->ProtocolBindingList.Flink;
                 entry != &Ext->ProtocolBindingList &&
                     SnapCount < NDIS6_STATUSEX_SNAPSHOT_MAX;
                 entry = entry->Flink)
            {
                PNDIS6_PROTOCOL_BINDING Binding =
                    CONTAINING_RECORD(entry, NDIS6_PROTOCOL_BINDING, AdapterLink);
                if (Binding->DriverBlock != NULL &&
                    Binding->DriverBlock->Characteristics.StatusHandlerEx != NULL &&
                    Ndis6ReferenceProtocolBinding(Binding))
                {
                    Snapshot[SnapCount].Binding = Binding;
                    Snapshot[SnapCount].Context =
                        Binding->ProtocolBindingContext;
                    Snapshot[SnapCount].StatusHandlerEx =
                        Binding->DriverBlock->Characteristics.StatusHandlerEx;
                    SnapCount++;
                }
            }
        }
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, NativeIrql);

        for (i = 0; i < SnapCount; i++)
        {
            Snapshot[i].StatusHandlerEx(
                Snapshot[i].Context,
                StatusIndication);
            Ndis6DereferenceProtocolBinding(Snapshot[i].Binding);
        }
    }

    /* Translate the NDIS 6 status code to a legacy NDIS 5 code and update
     * the cache so cached OID queries serve fresh values. */
    switch (StatusIndication->StatusCode)
    {
    case NDIS_STATUS_LINK_STATE:
    {
        PNDIS_LINK_STATE LinkState =
            (PNDIS_LINK_STATE)StatusIndication->StatusBuffer;
        if (LinkState != NULL &&
            StatusIndication->StatusBufferSize >= sizeof(NDIS_LINK_STATE))
        {
            DbgPrint("NDIS6: LINK_STATE adapter=%p connect=%d rx=%llu tx=%llu\n", Adapter, LinkState->MediaConnectState, LinkState->RcvLinkSpeed, LinkState->XmitLinkSpeed);
            Ext->GeneralAttrs.MediaConnectState = LinkState->MediaConnectState;
            Ext->GeneralAttrs.MediaDuplexState  = LinkState->MediaDuplexState;
            Ext->GeneralAttrs.RcvLinkSpeed      = LinkState->RcvLinkSpeed;
            Ext->GeneralAttrs.XmitLinkSpeed     = LinkState->XmitLinkSpeed;

            LegacyStatus = (LinkState->MediaConnectState == MediaConnectStateConnected)
                ? NDIS_STATUS_MEDIA_CONNECT
                : NDIS_STATUS_MEDIA_DISCONNECT;
        }
        break;
    }
    case NDIS_STATUS_MEDIA_CONNECT:
    case NDIS_STATUS_MEDIA_DISCONNECT:
        /* Identical legacy code — pass through. */
        LegacyStatus = StatusIndication->StatusCode;
        Ext->GeneralAttrs.MediaConnectState =
            (StatusIndication->StatusCode == NDIS_STATUS_MEDIA_CONNECT)
                ? MediaConnectStateConnected
                : MediaConnectStateDisconnected;
        break;
    case NDIS_STATUS_RESET_START:
    case NDIS_STATUS_RESET_END:
        LegacyStatus = StatusIndication->StatusCode;
        break;

    case NDIS_STATUS_OPER_STATUS:
        /* Operational status changed (e.g., admin shutdown / restore).
         * Map to legacy media-state codes since legacy NDIS 5 doesn't
         * carry a separate "operational status" notion. */
        LegacyStatus = NDIS_STATUS_MEDIA_CONNECT;
        break;

    case NDIS_STATUS_LINK_SPEED_CHANGE:
        /* New link speed in StatusBuffer (ULONG64). Update the cache so
         * subsequent OID_GEN_LINK_SPEED queries reflect the new rate.
         * Don't fan out to legacy protocols — there's no legacy parallel
         * to "link speed change" status. */
        if (StatusIndication->StatusBuffer != NULL &&
            StatusIndication->StatusBufferSize >= sizeof(ULONG64))
        {
            ULONG64 NewSpeed = *(ULONG64*)StatusIndication->StatusBuffer;
            Ext->GeneralAttrs.RcvLinkSpeed  = NewSpeed;
            Ext->GeneralAttrs.XmitLinkSpeed = NewSpeed;
        }
        return;

    case NDIS_STATUS_NETWORK_CHANGE:
        /* Generic network reconfiguration. Forward as media-connect to
         * trigger ARP cache invalidation in tcpip. */
        LegacyStatus = NDIS_STATUS_MEDIA_CONNECT;
        break;

    case NDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG:
        /* Offload reconfigured — silently drop, the bridge doesn't
         * advertise offloads to legacy protocols. */
        return;

    case NDIS_STATUS_PACKET_FILTER:
        /* Driver changed effective packet filter — legacy protocols
         * don't care (they drive the filter via OID_GEN_CURRENT_PACKET_FILTER
         * which is round-tripped via Ndis6OidForward). Drop. */
        return;

    case NDIS_STATUS_MEDIA_SPECIFIC_INDICATION:
    case NDIS_STATUS_MEDIA_SPECIFIC_INDICATION_EX:
        /* WWAN / WLAN driver-specific indications. No legacy parallel;
         * forwarding as NDIS_STATUS_MEDIA_SPECIFIC_INDICATION keeps the
         * bit pattern consistent so protocols that DO care can decode it. */
        LegacyStatus = NDIS_STATUS_MEDIA_SPECIFIC_INDICATION;
        LegacyBuffer = StatusIndication->StatusBuffer;
        LegacyBufferSize = StatusIndication->StatusBufferSize;
        break;

    case NDIS_STATUS_DOT11_SCAN_CONFIRM:
    case NDIS_STATUS_DOT11_MPDU_MAX_LENGTH_CHANGED:
    case NDIS_STATUS_DOT11_ASSOCIATION_START:
    case NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION:
    case NDIS_STATUS_DOT11_CONNECTION_START:
    case NDIS_STATUS_DOT11_CONNECTION_COMPLETION:
    case NDIS_STATUS_DOT11_ROAMING_START:
    case NDIS_STATUS_DOT11_ROAMING_COMPLETION:
    case NDIS_STATUS_DOT11_DISASSOCIATION:
        /* 802.11 WLAN indications — no legacy WLAN stack in ReactOS, drop. */
        return;

    default:
        /* Unknown indication — drop with debug log. */
        DbgPrint("NDIS6: unhandled status indication 0x%08lx\n",
                 StatusIndication->StatusCode);
        return;
    }

    if (LegacyStatus == 0)
        return;

    /* Snapshot the protocol bindings under the lock, then call the
     * StatusHandlers OUTSIDE the lock. Holding NdisMiniportBlock.Lock
     * across protocol callbacks is a recipe for deadlock — protocols
     * commonly call back into NDIS from their status handlers (e.g.
     * tcpip queries OIDs from inside ProtocolStatus when the link
     * comes up), and the OID forward path takes its own waiter lock.
     *
     * The snapshot is bounded to 16 bindings — adapters with more than
     * that bound at once are extremely rare, and we silently truncate
     * (each binding still gets the indication on the next change). */
    {
#define NDIS6_STATUS_SNAPSHOT_MAX  16
        struct
        {
            PADAPTER_BINDING     Binding;
            PVOID                Context;
            STATUS_HANDLER       StatusHandler;
            STATUS_COMPLETE_HANDLER StatusCompleteHandler;
        } Snapshot[NDIS6_STATUS_SNAPSHOT_MAX];
        UINT SnapCount = 0;
        UINT i;

        KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
        for (Entry = Adapter->ProtocolListHead.Flink;
             Entry != &Adapter->ProtocolListHead && SnapCount < NDIS6_STATUS_SNAPSHOT_MAX;
             Entry = Entry->Flink)
        {
            PADAPTER_BINDING Binding =
                CONTAINING_RECORD(Entry, ADAPTER_BINDING, AdapterListEntry);

            if (Binding->ProtocolBinding == NULL)
                continue;

            if (!NdisReferenceAdapterBinding(Binding))
                continue;

            Snapshot[SnapCount].Binding = Binding;
            Snapshot[SnapCount].Context =
                Binding->NdisOpenBlock.ProtocolBindingContext;
            Snapshot[SnapCount].StatusHandler =
                Binding->ProtocolBinding->Chars.StatusHandler;
            Snapshot[SnapCount].StatusCompleteHandler =
                Binding->ProtocolBinding->Chars.StatusCompleteHandler;
            SnapCount++;
        }
        KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

        for (i = 0; i < SnapCount; i++)
        {
            if (Snapshot[i].StatusHandler != NULL)
            {
                Snapshot[i].StatusHandler(
                    Snapshot[i].Context,
                    LegacyStatus,
                    LegacyBuffer,
                    LegacyBufferSize);
            }
            if (Snapshot[i].StatusCompleteHandler != NULL)
            {
                Snapshot[i].StatusCompleteHandler(Snapshot[i].Context);
            }
            NdisDereferenceAdapterBinding(Snapshot[i].Binding);
        }
    }
}

/* EOF */
