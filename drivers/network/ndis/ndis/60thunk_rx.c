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

#define NDIS6_RX_PARENT_CONTEXT_TAG   'pRxN'
#define NDIS6_RX_PARENT_CONTEXT_MAGIC 0x4E785250

typedef struct _NDIS6_RX_PARENT_CONTEXT
{
    ULONG Magic;
    volatile LONG References;
    PNET_BUFFER_LIST NetBufferList;
    PLOGICAL_ADAPTER Adapter;
} NDIS6_RX_PARENT_CONTEXT, *PNDIS6_RX_PARENT_CONTEXT;

#define NDIS6_RX_NATIVE_CONTEXT_TAG   'nRxN'
#define NDIS6_RX_NATIVE_CONTEXT_MAGIC 0x4E78524E

typedef struct _NDIS6_RX_NATIVE_CONTEXT
{
    ULONG Magic;
    PNDIS6_PROTOCOL_BINDING Binding;
    PNDIS6_RX_PARENT_CONTEXT Parent;
} NDIS6_RX_NATIVE_CONTEXT, *PNDIS6_RX_NATIVE_CONTEXT;

static PNDIS6_RX_PARENT_CONTEXT
Ndis6RxAllocateParentContext(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    PNDIS6_RX_PARENT_CONTEXT Context;

    Context = (PNDIS6_RX_PARENT_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), NDIS6_RX_PARENT_CONTEXT_TAG);
    if (Context == NULL)
        return NULL;

    if (!Ndis6ReferenceAdapterLifecycle(Adapter))
    {
        ExFreePoolWithTag(Context, NDIS6_RX_PARENT_CONTEXT_TAG);
        return NULL;
    }

    Context->Magic = NDIS6_RX_PARENT_CONTEXT_MAGIC;
    Context->References = 1; /* construction/indication guard */
    Context->NetBufferList = NetBufferList;
    Context->Adapter = Adapter;
    return Context;
}

static VOID
Ndis6RxReleaseParentContext(
    _In_ PNDIS6_RX_PARENT_CONTEXT Context,
    _In_ ULONG ReturnFlags)
{
    PNET_BUFFER_LIST NetBufferList;
    PLOGICAL_ADAPTER Adapter;
    LONG RemainingReferences;

    if (Context == NULL || Context->Magic != NDIS6_RX_PARENT_CONTEXT_MAGIC)
        return;

    RemainingReferences = InterlockedDecrement(&Context->References);
    ASSERT(RemainingReferences >= 0);
    if (RemainingReferences != 0)
        return;

    NetBufferList = Context->NetBufferList;
    Adapter = Context->Adapter;
    Context->Magic = 0;
    ExFreePoolWithTag(Context, NDIS6_RX_PARENT_CONTEXT_TAG);

    Ndis6FilterDispatchReturn(Adapter, NetBufferList, ReturnFlags);
    Ndis6DereferenceAdapterLifecycle(Adapter);
}

static PNET_BUFFER_LIST
Ndis6RxAllocateNativeClone(
    _In_ PNDIS6_PROTOCOL_BINDING Binding,
    _In_ PNDIS6_RX_PARENT_CONTEXT Parent)
{
    PNDIS6_RX_NATIVE_CONTEXT Context;
    PNET_BUFFER_LIST Clone;

    Clone = NdisAllocateCloneNetBufferList(Parent->NetBufferList, NULL, NULL, 0);
    if (Clone == NULL)
        return NULL;

    Context = (PNDIS6_RX_NATIVE_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*Context), NDIS6_RX_NATIVE_CONTEXT_TAG);
    if (Context == NULL)
    {
        NdisFreeCloneNetBufferList(Clone, 0);
        return NULL;
    }

    Context->Magic = NDIS6_RX_NATIVE_CONTEXT_MAGIC;
    Context->Binding = Binding;
    Context->Parent = Parent;
    Clone->NdisReserved[0] = Context;
    NET_BUFFER_LIST_NEXT_NBL(Clone) = NULL;
    NdisCopyReceiveNetBufferListInfo(Clone, Parent->NetBufferList);
    InterlockedIncrement(&Parent->References);
    return Clone;
}

static PNET_BUFFER_LIST
Ndis6RxAllocateResourcesClone(
    _In_ PNET_BUFFER_LIST Original)
{
    PNET_BUFFER_LIST Clone;

    Clone = NdisAllocateCloneNetBufferList(Original, NULL, NULL, 0);
    if (Clone != NULL)
    {
        NET_BUFFER_LIST_NEXT_NBL(Clone) = NULL;
        NdisCopyReceiveNetBufferListInfo(Clone, Original);
    }

    return Clone;
}

BOOLEAN
Ndis6RxReturnNativeNetBufferList(
    _In_ PNDIS6_PROTOCOL_BINDING Binding,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG ReturnFlags)
{
    PNDIS6_RX_NATIVE_CONTEXT Context;
    PNDIS6_RX_PARENT_CONTEXT Parent;

    if (Binding == NULL || NetBufferList == NULL)
        return FALSE;

    Context = (PNDIS6_RX_NATIVE_CONTEXT)NetBufferList->NdisReserved[0];
    if (Context == NULL ||
        Context->Magic != NDIS6_RX_NATIVE_CONTEXT_MAGIC ||
        Context->Binding != Binding ||
        Context->Parent == NULL ||
        Context->Parent->Adapter != Binding->Adapter)
    {
        return FALSE;
    }

    Parent = Context->Parent;
    NetBufferList->NdisReserved[0] = NULL;
    Context->Magic = 0;
    ExFreePoolWithTag(Context, NDIS6_RX_NATIVE_CONTEXT_TAG);
    NdisFreeCloneNetBufferList(NetBufferList, 0);
    Ndis6DereferenceProtocolBinding(Binding);
    Ndis6RxReleaseParentContext(Parent, ReturnFlags);

    return TRUE;
}

/* ============================================================================
 *  Ndis6RxBuildLegacyPacket — wrap a NET_BUFFER in a legacy NDIS_PACKET.
 *
 *  The wrapper is allocated from Ext->RxLegacyPacketPool, with a single
 *  NDIS_BUFFER chained on that re-uses the NB's CurrentMdl directly (no
 *  copy). Reserved[0] carries the private per-NBL return context and
 *  Reserved[1] carries the indicating adapter. The legacy WrapperReserved[0]
 *  starts at zero and is initialized by MiniIndicateReceivePacket.
 *
 *  Returns NULL on allocation failure (caller drops the NB).
 * ============================================================================ */

static PNDIS_PACKET
Ndis6RxBuildLegacyPacket(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNET_BUFFER_LIST   Nbl,
    _In_ PNET_BUFFER        Nb,
    _In_ BOOLEAN            ResourcesFlag,
    _In_opt_ PNDIS6_RX_PARENT_CONTEXT ReturnContext)
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
        PVOID SegmentAddress;

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

        SegmentAddress =
            (PUCHAR)MmGetMdlVirtualAddress(CurrentMdl) + MdlSegmentOffset;

        /* Preserve the source MDL's locked-page description. Rebuilding this
         * range with MmBuildMdlForNonPagedPool would incorrectly assume that
         * every NDIS 6 receive MDL describes ordinary nonpaged pool. */
        NdisBuffer = IoAllocateMdl(SegmentAddress,
                                   MdlSegmentSize,
                                   FALSE,
                                   FALSE,
                                   NULL);
        if (NdisBuffer == NULL)
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
        IoBuildPartialMdl(CurrentMdl,
                          NdisBuffer,
                          SegmentAddress,
                          MdlSegmentSize);
        NdisBuffer->Next = NULL;

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

    /* Reserved[1] is the indicating adapter in the legacy packet ABI;
     * MiniIndicateReceivePacket writes the same value before indication.
     * Reserved[0] points to the bridge-owned return context. */
    Packet->Reserved[0] = (ULONG_PTR)ReturnContext;
    Packet->Reserved[1] = (ULONG_PTR)Ext->Adapter;

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
 *  Drops one wrapper reference in the private per-NBL return context. The
 *  final release returns the original NBL to the miniport.
 * ============================================================================ */

VOID
Ndis6RxReturnLegacyPacket(
    _In_ PNDIS_PACKET Packet)
{
    PNDIS6_RX_PARENT_CONTEXT ReturnContext;
    PLOGICAL_ADAPTER Adapter;
    ULONG ReturnFlags;

    if (Packet == NULL)
        return;

    ReturnContext = (PNDIS6_RX_PARENT_CONTEXT)Packet->Reserved[0];
    Adapter = (PLOGICAL_ADAPTER)Packet->Reserved[1];

    Ndis6RxFreeLegacyPacket(Packet);

    if (ReturnContext == NULL ||
        ReturnContext->Magic != NDIS6_RX_PARENT_CONTEXT_MAGIC ||
        Adapter == NULL ||
        !Adapter->IsNdis6 ||
        ReturnContext->Adapter != Adapter)
    {
        return;
    }

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    ReturnFlags = KeGetCurrentIrql() == DISPATCH_LEVEL ?
        NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0;
    Ndis6RxReleaseParentContext(ReturnContext, ReturnFlags);
}

static VOID
Ndis6RxIndicateLegacyBatch(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_reads_(PacketCount) PPNDIS_PACKET PacketArray,
    _In_ UINT PacketCount)
{
    ASSERT(PacketCount != 0);
    MiniIndicateReceivePacket((NDIS_HANDLE)Adapter, PacketArray, PacketCount);

    /* MiniIndicateReceivePacket leaves one NDIS 6 bridge-owner reference on
     * every non-RESOURCES wrapper. Drop it through the same locked return
     * path used by protocols. This prevents a concurrent protocol return
     * from freeing a wrapper while this batch still holds its pointer. */
    NdisReturnPackets(PacketArray, PacketCount);
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

typedef struct _NDIS6_RX_BINDING_SNAPSHOT_ENTRY
{
    PNDIS6_PROTOCOL_BINDING Binding;
    PROTOCOL_RECEIVE_NET_BUFFER_LISTS_HANDLER ReceiveHandler;
    NDIS_HANDLE Context;
} NDIS6_RX_BINDING_SNAPSHOT_ENTRY, *PNDIS6_RX_BINDING_SNAPSHOT_ENTRY;

static PNDIS6_RX_BINDING_SNAPSHOT_ENTRY
Ndis6RxSnapshotNativeBindings(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _Out_ PSIZE_T SnapshotCount)
{
    PNDIS6_RX_BINDING_SNAPSHOT_ENTRY Snapshot = NULL;
    SIZE_T Capacity = 0;
    SIZE_T Count = 0;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    *SnapshotCount = 0;
    KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
    for (Entry = Ext->ProtocolBindingList.Flink;
         Entry != &Ext->ProtocolBindingList;
         Entry = Entry->Flink)
    {
        PNDIS6_PROTOCOL_BINDING Binding =
            CONTAINING_RECORD(Entry, NDIS6_PROTOCOL_BINDING, AdapterLink);
        if (Binding->DriverBlock != NULL &&
            InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_RUNNING, NDIS6_PROTOCOL_STATE_RUNNING) ==
                NDIS6_PROTOCOL_STATE_RUNNING &&
            Binding->DriverBlock->Characteristics.ReceiveNetBufferListsHandler != NULL)
        {
            Capacity++;
        }
    }
    KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);

    if (Capacity == 0 ||
        Capacity > MAXULONG_PTR / sizeof(*Snapshot))
    {
        return NULL;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, Capacity * sizeof(*Snapshot), 'bRxN');
    if (Snapshot == NULL)
        return NULL;

    KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
    for (Entry = Ext->ProtocolBindingList.Flink;
         Entry != &Ext->ProtocolBindingList && Count < Capacity;
         Entry = Entry->Flink)
    {
        PNDIS6_PROTOCOL_BINDING Binding =
            CONTAINING_RECORD(Entry, NDIS6_PROTOCOL_BINDING, AdapterLink);
        if (Binding->DriverBlock != NULL &&
            InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_RUNNING, NDIS6_PROTOCOL_STATE_RUNNING) ==
                NDIS6_PROTOCOL_STATE_RUNNING &&
            Binding->DriverBlock->Characteristics.ReceiveNetBufferListsHandler != NULL &&
            Ndis6ReferenceProtocolBinding(Binding))
        {
            Snapshot[Count].Binding = Binding;
            Snapshot[Count].ReceiveHandler =
                Binding->DriverBlock->Characteristics.ReceiveNetBufferListsHandler;
            Snapshot[Count].Context = Binding->ProtocolBindingContext;
            Count++;
        }
    }
    KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);

    if (Count == 0)
    {
        ExFreePoolWithTag(Snapshot, 'bRxN');
        return NULL;
    }

    *SnapshotCount = Count;
    return Snapshot;
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
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_RX_BINDING_SNAPSHOT_ENTRY Snapshot;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST NextNbl;
    SIZE_T SnapshotCount;
    SIZE_T i;
    BOOLEAN ResourcesFlag;
    ULONG ImmediateReturnFlags;

    if (Adapter == NULL || !Adapter->IsNdis6 || NetBufferLists == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    ResourcesFlag = (ReceiveFlags & NDIS_RECEIVE_FLAGS_RESOURCES) != 0;
    ImmediateReturnFlags =
        (ReceiveFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL) != 0 ?
        NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0;
    Snapshot = Ndis6RxSnapshotNativeBindings(Ext, &SnapshotCount);

    if (ResourcesFlag)
    {
        PNDIS_PACKET BatchArray[16];
        UINT BatchCount = 0;
        UINT j;

        for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            PNET_BUFFER Nb;

            for (i = 0; i < SnapshotCount; i++)
            {
                PNET_BUFFER_LIST Clone =
                    Ndis6RxAllocateResourcesClone(CurrentNbl);
                if (Clone == NULL)
                    continue;

                Snapshot[i].ReceiveHandler(Snapshot[i].Context, Clone, PortNumber, 1, ReceiveFlags);
                NdisFreeCloneNetBufferList(Clone, 0);
            }

            for (Nb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
            {
                PNDIS_PACKET LegacyPacket = Ndis6RxBuildLegacyPacket(Ext, CurrentNbl, Nb, TRUE, NULL);
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
        goto CleanupSnapshot;
    }

    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        PNET_BUFFER Nb;
        PNET_BUFFER NextNb;
        PNDIS6_RX_PARENT_CONTEXT ReturnContext;
        PNDIS_PACKET PacketArray[16];
        UINT PacketCount = 0;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        ReturnContext = Ndis6RxAllocateParentContext(Adapter, CurrentNbl);
        if (ReturnContext == NULL)
        {
            Ndis6FilterDispatchReturn(Adapter, CurrentNbl, ImmediateReturnFlags);
            continue;
        }

        /* Every native binding gets an independent clone so protocol-reserved
         * fields and return ownership cannot collide. The binding snapshot
         * reference covers the callback; a second reference is transferred
         * to each clone that can be retained after the callback returns. */
        for (i = 0; i < SnapshotCount; i++)
        {
            PNET_BUFFER_LIST Clone;

            if (!Ndis6ReferenceProtocolBinding(Snapshot[i].Binding))
                continue;
            Clone = Ndis6RxAllocateNativeClone(Snapshot[i].Binding, ReturnContext);
            if (Clone == NULL)
            {
                Ndis6DereferenceProtocolBinding(Snapshot[i].Binding);
                continue;
            }

            Snapshot[i].ReceiveHandler(Snapshot[i].Context, Clone, PortNumber, 1, ReceiveFlags);
        }

        for (Nb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
             Nb != NULL;
             Nb = NextNb)
        {
            PNDIS_PACKET LegacyPacket =
                Ndis6RxBuildLegacyPacket(Ext, CurrentNbl, Nb, FALSE, ReturnContext);

            NextNb = NET_BUFFER_NEXT_NB(Nb);
            if (LegacyPacket == NULL)
                continue;

            InterlockedIncrement(&ReturnContext->References);
            PacketArray[PacketCount++] = LegacyPacket;
            if (PacketCount == ARRAYSIZE(PacketArray))
            {
                Ndis6RxIndicateLegacyBatch(Adapter, PacketArray, PacketCount);
                PacketCount = 0;
            }
        }

        if (PacketCount != 0)
            Ndis6RxIndicateLegacyBatch(Adapter, PacketArray, PacketCount);

        /* Drop the construction guard after every callback has acquired its
         * own child/wrapper reference. The final protocol return releases the
         * original miniport NBL through the filter return path. */
        Ndis6RxReleaseParentContext(ReturnContext, ImmediateReturnFlags);
    }

CleanupSnapshot:
    for (i = 0; i < SnapshotCount; i++)
        Ndis6DereferenceProtocolBinding(Snapshot[i].Binding);
    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, 'bRxN');
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
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;

    if (Adapter == NULL || !Adapter->IsNdis6 || StatusIndication == NULL)
        return;

    Ndis6FilterDispatchStatus(Adapter, StatusIndication);
}

VOID
Ndis6FilterTerminalStatus(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
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
        typedef struct _NDIS6_STATUSEX_SNAPSHOT_ENTRY
        {
            PNDIS6_PROTOCOL_BINDING Binding;
            NDIS_HANDLE                Context;
            PROTOCOL_STATUS_EX_HANDLER StatusHandlerEx;
        } NDIS6_STATUSEX_SNAPSHOT_ENTRY, *PNDIS6_STATUSEX_SNAPSHOT_ENTRY;
        PNDIS6_STATUSEX_SNAPSHOT_ENTRY Snapshot = NULL;
        SIZE_T SnapshotCapacity = 0;
        SIZE_T SnapCount = 0;
        SIZE_T i;
        KIRQL NativeIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &NativeIrql);
        {
            PLIST_ENTRY entry;
            for (entry = Ext->ProtocolBindingList.Flink;
                 entry != &Ext->ProtocolBindingList;
                 entry = entry->Flink)
            {
                PNDIS6_PROTOCOL_BINDING Binding =
                    CONTAINING_RECORD(entry, NDIS6_PROTOCOL_BINDING, AdapterLink);
                if (Binding->DriverBlock != NULL &&
                    Binding->DriverBlock->Characteristics.StatusHandlerEx != NULL)
                {
                    SnapshotCapacity++;
                }
            }
        }
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, NativeIrql);

        if (SnapshotCapacity != 0 &&
            SnapshotCapacity <= MAXULONG_PTR / sizeof(*Snapshot))
        {
            Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), 'xSNn');
        }

        if (SnapshotCapacity != 0 && Snapshot == NULL)
        {
            DbgPrint("NDIS6: unable to allocate native status binding snapshot\n");
        }
        else if (Snapshot != NULL)
        {
            PLIST_ENTRY entry;

            KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &NativeIrql);
            for (entry = Ext->ProtocolBindingList.Flink;
                 entry != &Ext->ProtocolBindingList && SnapCount < SnapshotCapacity;
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
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, NativeIrql);
        }

        for (i = 0; i < SnapCount; i++)
        {
            Snapshot[i].StatusHandlerEx(
                Snapshot[i].Context,
                StatusIndication);
            Ndis6DereferenceProtocolBinding(Snapshot[i].Binding);
        }

        if (Snapshot != NULL)
            ExFreePoolWithTag(Snapshot, 'xSNn');
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
     * comes up), and the OID forward path takes its own waiter lock. */
    {
        typedef struct _NDIS6_STATUS_SNAPSHOT_ENTRY
        {
            PADAPTER_BINDING     Binding;
            PVOID                Context;
            STATUS_HANDLER       StatusHandler;
            STATUS_COMPLETE_HANDLER StatusCompleteHandler;
        } NDIS6_STATUS_SNAPSHOT_ENTRY, *PNDIS6_STATUS_SNAPSHOT_ENTRY;
        PNDIS6_STATUS_SNAPSHOT_ENTRY Snapshot = NULL;
        SIZE_T SnapshotCapacity = 0;
        SIZE_T SnapCount = 0;
        SIZE_T i;

        KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
        for (Entry = Adapter->ProtocolListHead.Flink;
             Entry != &Adapter->ProtocolListHead;
             Entry = Entry->Flink)
        {
            PADAPTER_BINDING Binding =
                CONTAINING_RECORD(Entry, ADAPTER_BINDING, AdapterListEntry);

            if (Binding->ProtocolBinding != NULL)
                SnapshotCapacity++;
        }
        KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

        if (SnapshotCapacity == 0)
            return;

        if (SnapshotCapacity > MAXULONG_PTR / sizeof(*Snapshot))
        {
            DbgPrint("NDIS6: status binding snapshot size overflow\n");
            return;
        }

        Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), 'sSNn');
        if (Snapshot == NULL)
        {
            DbgPrint("NDIS6: unable to allocate status binding snapshot\n");
            return;
        }

        KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
        for (Entry = Adapter->ProtocolListHead.Flink;
             Entry != &Adapter->ProtocolListHead && SnapCount < SnapshotCapacity;
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

        ExFreePoolWithTag(Snapshot, 'sSNn');
    }
}

/* EOF */
