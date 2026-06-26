/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     802.3 <-> 802.11 data-path frame translation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

/* RFC 1042 / 802.1H SNAP constants. */
#define SNAP_LLC_DSAP       0xAA
#define SNAP_LLC_SSAP       0xAA
#define SNAP_LLC_CONTROL    0x03

/* Per-NBL context accessor (pool created with ContextSize == sizeof). */
#define NWIFI_NBL_CTX(_nbl) \
    ((PNWIFI_NBL_CONTEXT)NET_BUFFER_LIST_CONTEXT_DATA_START(_nbl))

/* ===========================================================================
 *  Small buffer helpers
 * ===========================================================================
 */

/* Linearise a NET_BUFFER (which may span several MDLs) into a contiguous
 * buffer.  Returns bytes copied. */
static
ULONG
NwifiLinearizeNb(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(MaxLength) PUCHAR Dest,
    _In_ ULONG MaxLength)
{
    PMDL Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG Copied = 0;

    if (Remaining > MaxLength)
    {
        Remaining = MaxLength;
    }

    while (Mdl != NULL && Remaining > 0)
    {
        PUCHAR Va = MmGetSystemAddressForMdlSafe(Mdl, LowPagePriority);
        ULONG MdlLength = MmGetMdlByteCount(Mdl);
        ULONG Chunk;

        if (Va == NULL)
        {
            break;
        }
        if (Offset >= MdlLength)
        {
            Offset -= MdlLength;
            Mdl = Mdl->Next;
            continue;
        }

        Chunk = MdlLength - Offset;
        if (Chunk > Remaining)
        {
            Chunk = Remaining;
        }
        RtlCopyMemory(Dest + Copied, Va + Offset, Chunk);
        Copied += Chunk;
        Remaining -= Chunk;
        Offset = 0;
        Mdl = Mdl->Next;
    }

    return Copied;
}

/* Build a single-buffer NBL around a freshly allocated frame buffer; the
 * per-NBL context records what the free path must release. */
static
PNET_BUFFER_LIST
NwifiBuildNbl(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_HANDLE Pool,
    _In_ PUCHAR FrameBuffer,
    _In_ ULONG FrameLength,
    _In_opt_ PNET_BUFFER_LIST OriginalNbl)
{
    PMDL Mdl;
    PNET_BUFFER_LIST Nbl;
    PNWIFI_NBL_CONTEXT Ctx;

    Mdl = NdisAllocateMdl(gNwifi.MiniportDriverHandle, FrameBuffer, FrameLength);
    if (Mdl == NULL)
    {
        return NULL;
    }
    MmBuildMdlForNonPagedPool(Mdl);

    Nbl = NdisAllocateNetBufferAndNetBufferList(Pool, 0, 0, Mdl, 0, FrameLength);
    if (Nbl == NULL)
    {
        NdisFreeMdl(Mdl);
        return NULL;
    }

    Ctx = NWIFI_NBL_CTX(Nbl);
    Ctx->Adapter = Adapter;
    Ctx->OriginalNbl = OriginalNbl;
    Ctx->DataBuffer = FrameBuffer;
    Ctx->DataLength = FrameLength;
    Ctx->Mdl = Mdl;

    return Nbl;
}

static
VOID
NwifiFreeBuiltNbl(
    _In_ PNET_BUFFER_LIST Nbl)
{
    PNWIFI_NBL_CONTEXT Ctx = NWIFI_NBL_CTX(Nbl);
    PUCHAR Buffer = Ctx->DataBuffer;
    PMDL Mdl = Ctx->Mdl;

    NdisFreeNetBufferList(Nbl);
    if (Mdl != NULL)
    {
        NdisFreeMdl(Mdl);
    }
    if (Buffer != NULL)
    {
        NwifiFree(Buffer);
    }
}

/* Does this EtherType use the 802.1H bridge-tunnel SNAP OUI (00-00-F8)? */
static
__inline
BOOLEAN
NwifiUsesBridgeTunnel(
    _In_ USHORT EtherType)
{
    return (EtherType == ETHERTYPE_AARP || EtherType == ETHERTYPE_IPX);
}

/* ===========================================================================
 *  Transmit: 802.3 -> 802.11 (ToDS), attach DOT11_EXTSTA_SEND_CONTEXT
 * ===========================================================================
 */

/* Translate one 802.3 NET_BUFFER into an 802.11 ToDS data frame with an
 * RFC 1042 SNAP header.  Returns a new TX-pool NBL, or NULL on failure. */
static
PNET_BUFFER_LIST
NwifiTranslateTxFrame(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ PNET_BUFFER_LIST OriginalNbl)
{
    UCHAR Eth[ETH_MAX_FRAME];
    ULONG EthLength;
    ULONG PayloadLength;
    ULONG FrameLength;
    PUCHAR Frame;
    PIEEE80211_MAC_HEADER MacHeader;
    PRFC1042_SNAP_HEADER Snap;
    PUCHAR Payload;
    USHORT EtherType;
    USHORT SeqCtl;
    PNET_BUFFER_LIST Nbl;

    EthLength = NET_BUFFER_DATA_LENGTH(NetBuffer);
    if (EthLength < ETH_HEADER_LEN || EthLength > ETH_MAX_FRAME)
    {
        return NULL;
    }

    if (NwifiLinearizeNb(NetBuffer, Eth, EthLength) != EthLength)
    {
        return NULL;
    }

    PayloadLength = EthLength - ETH_HEADER_LEN;
    EtherType = (USHORT)((Eth[ETH_TYPE_OFFSET] << 8) | Eth[ETH_TYPE_OFFSET + 1]);

    /* New 802.11 frame: MAC header + SNAP + payload (Ethernet addrs/type dropped). */
    FrameLength = IEEE80211_MAC_HEADER_LEN + RFC1042_SNAP_HEADER_LEN + PayloadLength;
    Frame = (PUCHAR)NwifiAllocate(FrameLength);
    if (Frame == NULL)
    {
        return NULL;
    }

    MacHeader = (PIEEE80211_MAC_HEADER)Frame;
    MacHeader->FrameControl[0] = IEEE80211_FC0_VERSION_0 |
                                 IEEE80211_FC0_TYPE_DATA |
                                 IEEE80211_FC0_SUBTYPE_DATA;
    MacHeader->FrameControl[1] = IEEE80211_FC1_DIR_TODS;    /* infra uplink */
    MacHeader->DurationId[0] = 0;
    MacHeader->DurationId[1] = 0;
    /* ToDS: A1=BSSID, A2=SA(our MAC), A3=DA(eth dst). */
    RtlCopyMemory(MacHeader->Address1, Adapter->Bssid, IEEE80211_ADDR_LEN);
    RtlCopyMemory(MacHeader->Address2, &Eth[ETH_ADDR_LEN], IEEE80211_ADDR_LEN);
    RtlCopyMemory(MacHeader->Address3, &Eth[0], IEEE80211_ADDR_LEN);

    /* 12-bit sequence number in the high bits, fragment number 0. */
    SeqCtl = (USHORT)((InterlockedIncrement(&Adapter->SequenceNumber) & 0x0FFF) << 4);
    MacHeader->SequenceControl[0] = (UCHAR)(SeqCtl & 0xFF);
    MacHeader->SequenceControl[1] = (UCHAR)(SeqCtl >> 8);

    /* SNAP header carrying the original EtherType. */
    Snap = (PRFC1042_SNAP_HEADER)(Frame + IEEE80211_MAC_HEADER_LEN);
    Snap->Dsap = SNAP_LLC_DSAP;
    Snap->Ssap = SNAP_LLC_SSAP;
    Snap->Control = SNAP_LLC_CONTROL;
    Snap->Oui[0] = 0x00;
    Snap->Oui[1] = 0x00;
    Snap->Oui[2] = NwifiUsesBridgeTunnel(EtherType) ? 0xF8 : 0x00;
    Snap->EtherType[0] = Eth[ETH_TYPE_OFFSET];
    Snap->EtherType[1] = Eth[ETH_TYPE_OFFSET + 1];

    Payload = Frame + IEEE80211_MAC_HEADER_LEN + RFC1042_SNAP_HEADER_LEN;
    RtlCopyMemory(Payload, &Eth[ETH_HEADER_LEN], PayloadLength);

    Nbl = NwifiBuildNbl(Adapter, Adapter->TxNblPool, Frame, FrameLength, OriginalNbl);
    if (Nbl == NULL)
    {
        NwifiFree(Frame);
        return NULL;
    }

    /*
     * A Native-802.11 miniport expects a DOT11_EXTSTA_SEND_CONTEXT in the
     * NBL's MediaSpecificInformation slot; keep a back-pointer in
     * ProtocolReserved[0] so the send-complete path can free it.
     */
    {
        PDOT11_EXTSTA_SEND_CONTEXT SendCtx =
            (PDOT11_EXTSTA_SEND_CONTEXT)NwifiAllocate(sizeof(DOT11_EXTSTA_SEND_CONTEXT));
        if (SendCtx == NULL)
        {
            NwifiFreeBuiltNbl(Nbl);
            return NULL;
        }
        SendCtx->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        SendCtx->Header.Revision = DOT11_EXTSTA_SEND_CONTEXT_REVISION_1;
        SendCtx->Header.Size = sizeof(DOT11_EXTSTA_SEND_CONTEXT);
        SendCtx->usExemptionActionType = 0;     /* open network: no exemption */
        SendCtx->uPhyId = 0;                     /* let the miniport choose */
        SendCtx->uDelayedSleepValue = 0;
        SendCtx->pvMediaSpecificInfo = NULL;
        SendCtx->uSendFlags = 0;

        NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = SendCtx;
        Nbl->ProtocolReserved[0] = SendCtx;
    }

    return Nbl;
}

VOID
NTAPI
NwifiMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl = NetBufferLists;
    ULONG DownFlags = 0;
    BOOLEAN Running;

    UNREFERENCED_PARAMETER(PortNumber);

    if (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL)
    {
        DownFlags |= NDIS_SEND_FLAGS_DISPATCH_LEVEL;
    }

    NdisAcquireSpinLock(&Adapter->DataLock);
    Running = (Adapter->State == NwifiStateRunning);
    NdisReleaseSpinLock(&Adapter->DataLock);

    while (Nbl != NULL)
    {
        PNET_BUFFER_LIST NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        PNET_BUFFER NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        BOOLEAN Failed = TRUE;

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        /* Only single-NET_BUFFER NBLs are translated (TCP/IP sends one frame
         * per NBL); multi-NB NBLs are failed, not silently dropped. */
        if (Running && NetBuffer != NULL && NET_BUFFER_NEXT_NB(NetBuffer) == NULL &&
            Adapter->Associated)
        {
            PNET_BUFFER_LIST DownNbl =
                NwifiTranslateTxFrame(Adapter, NetBuffer, Nbl);
            if (DownNbl != NULL)
            {
                DownNbl->SourceHandle = NULL;   /* protocol-originated send */
                InterlockedIncrement(&Adapter->PendingSends);
                NdisSendNetBufferLists(Adapter->BindingHandle, DownNbl,
                                       NDIS_DEFAULT_PORT_NUMBER, DownFlags);
                Failed = FALSE;
            }
        }

        if (Failed)
        {
            /*
             * Paused => NDIS_STATUS_PAUSED (NDIS re-sends after restart).
             * Otherwise fail permanently - never RESOURCES, which would make
             * TCP/IP retry against a link that cannot carry the frame.
             */
            NET_BUFFER_LIST_STATUS(Nbl) =
                Running ? NDIS_STATUS_FAILURE : NDIS_STATUS_PAUSED;
            Adapter->TxError++;
            NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Nbl,
                (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL)
                    ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
        }

        Nbl = NextNbl;
    }
}

/* Free the translated down-NBL + its send context, and complete the matching
 * upper NBL back to TCP/IP. */
VOID
NwifiSendCompleteFromLower(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags)
{
    PNET_BUFFER_LIST Nbl = NetBufferLists;
    ULONG UpFlags = 0;

    if (SendCompleteFlags & NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL)
    {
        UpFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    }

    while (Nbl != NULL)
    {
        PNET_BUFFER_LIST NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        PNWIFI_NBL_CONTEXT Ctx = NWIFI_NBL_CTX(Nbl);
        PNET_BUFFER_LIST OriginalNbl = Ctx->OriginalNbl;
        NDIS_STATUS Status = NET_BUFFER_LIST_STATUS(Nbl);
        PDOT11_EXTSTA_SEND_CONTEXT SendCtx =
            (PDOT11_EXTSTA_SEND_CONTEXT)Nbl->ProtocolReserved[0];

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (Status == NDIS_STATUS_SUCCESS)
        {
            Adapter->TxOk++;
            Adapter->TxBytes += Ctx->DataLength;
        }
        else
        {
            Adapter->TxError++;
        }

        /* Free the per-send dot11 context and the translated frame/NBL. */
        if (SendCtx != NULL)
        {
            NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = NULL;
            Nbl->ProtocolReserved[0] = NULL;
            NwifiFree(SendCtx);
        }
        NwifiFreeBuiltNbl(Nbl);

        /* Complete the original upper-edge NBL back to TCP/IP. */
        if (OriginalNbl != NULL)
        {
            NET_BUFFER_LIST_STATUS(OriginalNbl) = Status;
            NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                            OriginalNbl, UpFlags);
        }

        InterlockedDecrement(&Adapter->PendingSends);
        Nbl = NextNbl;
    }
}

/* ===========================================================================
 *  Raw L2 TX inject (used by the supplicant to send EAPOL-Key frames)
 *
 *  Build an 802.11 ToDS data frame around a raw (dst, EtherType, payload)
 *  triple and send it down, optionally marked "send unencrypted".
 * ===========================================================================
 */
NDIS_STATUS
NwifiInjectL2Frame(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_reads_bytes_(6) PUCHAR DestMac,
    _In_ USHORT EtherType,
    _In_reads_bytes_(PayloadLength) PUCHAR Payload,
    _In_ ULONG PayloadLength,
    _In_ BOOLEAN SendUnencrypted)
{
    ULONG FrameLength;
    PUCHAR Frame;
    PIEEE80211_MAC_HEADER MacHeader;
    PRFC1042_SNAP_HEADER Snap;
    USHORT SeqCtl;
    PNET_BUFFER_LIST Nbl;
    PDOT11_EXTSTA_SEND_CONTEXT SendCtx;

    if (PayloadLength == 0 || PayloadLength > ETH_MTU)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    FrameLength = IEEE80211_MAC_HEADER_LEN + RFC1042_SNAP_HEADER_LEN + PayloadLength;
    Frame = (PUCHAR)NwifiAllocate(FrameLength);
    if (Frame == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    MacHeader = (PIEEE80211_MAC_HEADER)Frame;
    MacHeader->FrameControl[0] = IEEE80211_FC0_VERSION_0 |
                                 IEEE80211_FC0_TYPE_DATA |
                                 IEEE80211_FC0_SUBTYPE_DATA;
    MacHeader->FrameControl[1] = IEEE80211_FC1_DIR_TODS;
    MacHeader->DurationId[0] = 0;
    MacHeader->DurationId[1] = 0;
    /* ToDS: A1=BSSID, A2=SA(our MAC), A3=DA. */
    RtlCopyMemory(MacHeader->Address1, Adapter->Bssid, IEEE80211_ADDR_LEN);
    RtlCopyMemory(MacHeader->Address2, Adapter->MacAddress, IEEE80211_ADDR_LEN);
    RtlCopyMemory(MacHeader->Address3, DestMac, IEEE80211_ADDR_LEN);

    SeqCtl = (USHORT)((InterlockedIncrement(&Adapter->SequenceNumber) & 0x0FFF) << 4);
    MacHeader->SequenceControl[0] = (UCHAR)(SeqCtl & 0xFF);
    MacHeader->SequenceControl[1] = (UCHAR)(SeqCtl >> 8);

    Snap = (PRFC1042_SNAP_HEADER)(Frame + IEEE80211_MAC_HEADER_LEN);
    Snap->Dsap = SNAP_LLC_DSAP;
    Snap->Ssap = SNAP_LLC_SSAP;
    Snap->Control = SNAP_LLC_CONTROL;
    Snap->Oui[0] = 0x00;
    Snap->Oui[1] = 0x00;
    Snap->Oui[2] = NwifiUsesBridgeTunnel(EtherType) ? 0xF8 : 0x00;
    Snap->EtherType[0] = (UCHAR)(EtherType >> 8);
    Snap->EtherType[1] = (UCHAR)(EtherType & 0xFF);

    RtlCopyMemory(Frame + IEEE80211_MAC_HEADER_LEN + RFC1042_SNAP_HEADER_LEN,
                  Payload, PayloadLength);

    Nbl = NwifiBuildNbl(Adapter, Adapter->TxNblPool, Frame, FrameLength, NULL);
    if (Nbl == NULL)
    {
        NwifiFree(Frame);
        return NDIS_STATUS_RESOURCES;
    }

    SendCtx = (PDOT11_EXTSTA_SEND_CONTEXT)NwifiAllocate(sizeof(DOT11_EXTSTA_SEND_CONTEXT));
    if (SendCtx == NULL)
    {
        NwifiFreeBuiltNbl(Nbl);
        return NDIS_STATUS_RESOURCES;
    }
    SendCtx->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    SendCtx->Header.Revision = DOT11_EXTSTA_SEND_CONTEXT_REVISION_1;
    SendCtx->Header.Size = sizeof(DOT11_EXTSTA_SEND_CONTEXT);
    /* EAPOL must always be sent in the clear during the handshake. */
    SendCtx->usExemptionActionType =
        SendUnencrypted ? DOT11_EXEMPT_ALWAYS : DOT11_EXEMPT_NO_EXEMPTION;
    SendCtx->uPhyId = 0;
    SendCtx->uDelayedSleepValue = 0;
    SendCtx->pvMediaSpecificInfo = NULL;
    SendCtx->uSendFlags = 0;

    NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = SendCtx;
    Nbl->ProtocolReserved[0] = SendCtx;
    Nbl->SourceHandle = NULL;       /* protocol-originated send */

    InterlockedIncrement(&Adapter->PendingSends);
    NdisSendNetBufferLists(Adapter->BindingHandle, Nbl,
                           NDIS_DEFAULT_PORT_NUMBER, 0);
    return NDIS_STATUS_SUCCESS;
}

/* ===========================================================================
 *  Receive: 802.11 -> 802.3, strip DOT11_EXTSTA_RECV_CONTEXT
 * ===========================================================================
 */

/* Translate one received (linearised) 802.11 data frame into a fresh 802.3
 * frame.  Returns a new RX-pool NBL, or NULL if not translatable. */
static
PNET_BUFFER_LIST
NwifiTranslateRxFrame(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_reads_bytes_(InLength) PUCHAR In,
    _In_ ULONG InLength)
{
    PIEEE80211_MAC_HEADER MacHeader;
    PRFC1042_SNAP_HEADER Snap;
    PUCHAR Payload;
    ULONG PayloadLength;
    ULONG EthLength;
    PUCHAR Eth;
    UCHAR Da[IEEE80211_ADDR_LEN];
    UCHAR Sa[IEEE80211_ADDR_LEN];
    UCHAR Type;
    UCHAR Dir;
    PNET_BUFFER_LIST Nbl;

    if (InLength < IEEE80211_MAC_HEADER_LEN + RFC1042_SNAP_HEADER_LEN)
    {
        return NULL;
    }

    MacHeader = (PIEEE80211_MAC_HEADER)In;
    Type = MacHeader->FrameControl[0] & IEEE80211_FC0_TYPE_MASK;
    Dir = MacHeader->FrameControl[1] & IEEE80211_FC1_DIR_MASK;

    if (Type != IEEE80211_FC0_TYPE_DATA)
    {
        return NULL;
    }
    if ((MacHeader->FrameControl[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
        IEEE80211_FC0_SUBTYPE_DATA)
    {
        /* TODO: QoS data frames */
        return NULL;
    }

    /* Recover the 802.3 dst/src from the address fields per the DS bits.
     * FromDS (infra downlink): A1=DA, A2=BSSID, A3=SA. */
    switch (Dir)
    {
        case IEEE80211_FC1_DIR_FROMDS:
            RtlCopyMemory(Da, MacHeader->Address1, IEEE80211_ADDR_LEN);
            RtlCopyMemory(Sa, MacHeader->Address3, IEEE80211_ADDR_LEN);
            break;
        case IEEE80211_FC1_DIR_NODS:    /* IBSS / direct */
            RtlCopyMemory(Da, MacHeader->Address1, IEEE80211_ADDR_LEN);
            RtlCopyMemory(Sa, MacHeader->Address2, IEEE80211_ADDR_LEN);
            break;
        default:
            /* ToDS or WDS frames are not destined for our STA. */
            return NULL;
    }

    Snap = (PRFC1042_SNAP_HEADER)(In + IEEE80211_MAC_HEADER_LEN);
    /* Verify it is an RFC-1042 / 802.1H encapsulated frame. */
    if (Snap->Dsap != SNAP_LLC_DSAP || Snap->Ssap != SNAP_LLC_SSAP ||
        Snap->Control != SNAP_LLC_CONTROL)
    {
        return NULL;
    }

    Payload = In + IEEE80211_MAC_HEADER_LEN + RFC1042_SNAP_HEADER_LEN;
    PayloadLength = InLength - IEEE80211_MAC_HEADER_LEN - RFC1042_SNAP_HEADER_LEN;
    if (PayloadLength > ETH_MTU)
    {
        return NULL;
    }

    EthLength = ETH_HEADER_LEN + PayloadLength;
    Eth = (PUCHAR)NwifiAllocate(EthLength);
    if (Eth == NULL)
    {
        return NULL;
    }

    /* Rebuild the 802.3 header: dst | src | EtherType (from the SNAP). */
    RtlCopyMemory(&Eth[0], Da, ETH_ADDR_LEN);
    RtlCopyMemory(&Eth[ETH_ADDR_LEN], Sa, ETH_ADDR_LEN);
    Eth[ETH_TYPE_OFFSET] = Snap->EtherType[0];
    Eth[ETH_TYPE_OFFSET + 1] = Snap->EtherType[1];
    RtlCopyMemory(&Eth[ETH_HEADER_LEN], Payload, PayloadLength);

    Nbl = NwifiBuildNbl(Adapter, Adapter->RxNblPool, Eth, EthLength, NULL);
    if (Nbl == NULL)
    {
        NwifiFree(Eth);
        return NULL;
    }
    Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
    return Nbl;
}

/* Translate each indicated 802.11 data frame to 802.3 and indicate it up.
 * The data is copied, so the lower NBLs can be returned immediately. */
VOID
NwifiReceiveFromLower(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags)
{
    PNET_BUFFER_LIST Nbl = NetBufferLists;
    BOOLEAN AtDispatch = (ReceiveFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL) ? TRUE : FALSE;
    BOOLEAN Running;
    ULONG ReturnFlags = 0;

    UNREFERENCED_PARAMETER(NumberOfNetBufferLists);

    if (AtDispatch)
    {
        ReturnFlags |= NDIS_RETURN_FLAGS_DISPATCH_LEVEL;
    }

    NdisAcquireSpinLock(&Adapter->DataLock);
    Running = (Adapter->State == NwifiStateRunning);
    NdisReleaseSpinLock(&Adapter->DataLock);

    while (Nbl != NULL)
    {
        PNET_BUFFER_LIST NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        PNET_BUFFER NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);

        /* The DOT11_EXTSTA_RECV_CONTEXT in MediaSpecificInformation is
         * ignored; RSSI/rate are not needed for the data path. */

        for (; NetBuffer != NULL; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            UCHAR Frame[NWIFI_MAX_80211_FRAME];
            ULONG FrameLength = NET_BUFFER_DATA_LENGTH(NetBuffer);
            PNET_BUFFER_LIST UpNbl;

            if (FrameLength == 0 || FrameLength > sizeof(Frame))
            {
                Adapter->RxError++;
                continue;
            }
            if (NwifiLinearizeNb(NetBuffer, Frame, FrameLength) != FrameLength)
            {
                Adapter->RxError++;
                continue;
            }

            UpNbl = NwifiTranslateRxFrame(Adapter, Frame, FrameLength);
            if (UpNbl == NULL)
            {
                /* Not a translatable data frame (mgmt/ctrl/QoS/foreign). */
                continue;
            }

            /* EAPOL-Key frames are consumed by the supplicant and must not
             * be indicated up to TCP/IP. */
            {
                PNWIFI_NBL_CONTEXT UpCtx = NWIFI_NBL_CTX(UpNbl);
                if (NwifiMsmRxData(Adapter, UpCtx->DataBuffer, UpCtx->DataLength))
                {
                    NwifiFreeBuiltNbl(UpNbl);
                    continue;
                }
            }

            if (!Running)
            {
                NwifiFreeBuiltNbl(UpNbl);
                continue;
            }

            Adapter->RxOk++;
            Adapter->RxBytes += NWIFI_NBL_CTX(UpNbl)->DataLength;
            InterlockedIncrement(&Adapter->PendingReceives);

            /* Indicate with RESOURCES so NDIS returns the NBL synchronously;
             * the copy is then freed immediately in ReturnNetBufferLists. */
            NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                               UpNbl, NDIS_DEFAULT_PORT_NUMBER, 1,
                                               ReceiveFlags |
                                               NDIS_RECEIVE_FLAGS_RESOURCES);
            /* With RESOURCES set, the NBL is owned by us again on return. */
            NwifiMiniportReturnNetBufferLists(Adapter->MiniportAdapterHandle,
                                              UpNbl, ReturnFlags);
        }

        Nbl = NextNbl;
    }

    /* With RESOURCES the lower NBL data is only valid during this call and
     * there is nothing to return. */
    if ((ReceiveFlags & NDIS_RECEIVE_FLAGS_RESOURCES) == 0)
    {
        NdisReturnNetBufferLists(Adapter->BindingHandle, NetBufferLists, ReturnFlags);
    }
}

/* Reclaim an up-NBL after TCP/IP is done with it: frees the 802.3 copy,
 * MDL and NBL. */
VOID
NTAPI
NwifiMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl = NetBufferLists;

    UNREFERENCED_PARAMETER(ReturnFlags);

    while (Nbl != NULL)
    {
        PNET_BUFFER_LIST NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        NwifiFreeBuiltNbl(Nbl);
        InterlockedDecrement(&Adapter->PendingReceives);

        Nbl = NextNbl;
    }
}

/* ===========================================================================
 *  Link-state indication to TCP/IP
 * ===========================================================================
 */
VOID
NwifiIndicateLinkState(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ BOOLEAN Connected)
{
    NDIS_LINK_STATE LinkState;
    NDIS_STATUS_INDICATION StatusIndication;

    if (Adapter->MiniportAdapterHandle == NULL)
    {
        return;
    }

    Adapter->MediaConnected = Connected;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState =
        Connected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    if (Connected)
    {
        LinkState.XmitLinkSpeed = NWIFI_DEFAULT_LINK_SPEED;
        LinkState.RcvLinkSpeed = NWIFI_DEFAULT_LINK_SPEED;
    }
    else
    {
        LinkState.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        LinkState.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

    RtlZeroMemory(&StatusIndication, sizeof(StatusIndication));
    StatusIndication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    StatusIndication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    StatusIndication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    StatusIndication.SourceHandle = Adapter->MiniportAdapterHandle;
    StatusIndication.StatusCode = NDIS_STATUS_LINK_STATE;
    StatusIndication.StatusBuffer = &LinkState;
    StatusIndication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &StatusIndication);
}
