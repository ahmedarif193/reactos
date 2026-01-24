/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Debug support functions and statistics tracking
 * COPYRIGHT:   Copyright 2018 Mark Jansen (mark.jansen@reactos.org)
 *              Copyright 2024 ReactOS Team - Enhanced debug logging
 *              Copyright 2026 Ahmed ARIF (arif.ing@outlook.com) - NDIS 6.x port
 */

#include "e1000.h"
#include <ntstrsafe.h>

#include <debug.h>

/* ============================================================================
 * Global Debug Variables
 * ============================================================================ */

#if DBG
/* Global debug statistics */
E1000_DEBUG_STATS DebugStats = {0};
#endif /* DBG */


/* ============================================================================
 * OID to String Conversion
 * ============================================================================ */

const char* Oid2Str(IN NDIS_OID Oid)
{
#if DBG
    switch (Oid)
    {
#define RETURN_X(x) case x: return #x;
        /* Required Object IDs (OIDs) */
        RETURN_X(OID_GEN_SUPPORTED_LIST);
        RETURN_X(OID_GEN_HARDWARE_STATUS);
        RETURN_X(OID_GEN_MEDIA_SUPPORTED);
        RETURN_X(OID_GEN_MEDIA_IN_USE);
        RETURN_X(OID_GEN_MAXIMUM_LOOKAHEAD);
        RETURN_X(OID_GEN_MAXIMUM_FRAME_SIZE);
        RETURN_X(OID_GEN_LINK_SPEED);
        RETURN_X(OID_GEN_TRANSMIT_BUFFER_SPACE);
        RETURN_X(OID_GEN_RECEIVE_BUFFER_SPACE);
        RETURN_X(OID_GEN_TRANSMIT_BLOCK_SIZE);
        RETURN_X(OID_GEN_RECEIVE_BLOCK_SIZE);
        RETURN_X(OID_GEN_VENDOR_ID);
        RETURN_X(OID_GEN_VENDOR_DESCRIPTION);
        RETURN_X(OID_GEN_CURRENT_PACKET_FILTER);
        RETURN_X(OID_GEN_CURRENT_LOOKAHEAD);
        RETURN_X(OID_GEN_DRIVER_VERSION);
        RETURN_X(OID_GEN_MAXIMUM_TOTAL_SIZE);
        RETURN_X(OID_GEN_PROTOCOL_OPTIONS);
        RETURN_X(OID_GEN_MAC_OPTIONS);
        RETURN_X(OID_GEN_MEDIA_CONNECT_STATUS);
        RETURN_X(OID_GEN_MAXIMUM_SEND_PACKETS);
        RETURN_X(OID_GEN_VENDOR_DRIVER_VERSION);
        RETURN_X(OID_GEN_SUPPORTED_GUIDS);
        RETURN_X(OID_GEN_NETWORK_LAYER_ADDRESSES);
        RETURN_X(OID_GEN_TRANSPORT_HEADER_OFFSET);
        RETURN_X(OID_GEN_MACHINE_NAME);
        RETURN_X(OID_GEN_RNDIS_CONFIG_PARAMETER);
        RETURN_X(OID_GEN_VLAN_ID);

        /* Optional OIDs */
        RETURN_X(OID_GEN_MEDIA_CAPABILITIES);
        RETURN_X(OID_GEN_PHYSICAL_MEDIUM);

        /* Required statistics OIDs */
        RETURN_X(OID_GEN_XMIT_OK);
        RETURN_X(OID_GEN_RCV_OK);
        RETURN_X(OID_GEN_XMIT_ERROR);
        RETURN_X(OID_GEN_RCV_ERROR);
        RETURN_X(OID_GEN_RCV_NO_BUFFER);

        /* Optional statistics OIDs */
        RETURN_X(OID_GEN_DIRECTED_BYTES_XMIT);
        RETURN_X(OID_GEN_DIRECTED_FRAMES_XMIT);
        RETURN_X(OID_GEN_MULTICAST_BYTES_XMIT);
        RETURN_X(OID_GEN_MULTICAST_FRAMES_XMIT);
        RETURN_X(OID_GEN_BROADCAST_BYTES_XMIT);
        RETURN_X(OID_GEN_BROADCAST_FRAMES_XMIT);
        RETURN_X(OID_GEN_DIRECTED_BYTES_RCV);
        RETURN_X(OID_GEN_DIRECTED_FRAMES_RCV);
        RETURN_X(OID_GEN_MULTICAST_BYTES_RCV);
        RETURN_X(OID_GEN_MULTICAST_FRAMES_RCV);
        RETURN_X(OID_GEN_BROADCAST_BYTES_RCV);
        RETURN_X(OID_GEN_BROADCAST_FRAMES_RCV);
        RETURN_X(OID_GEN_RCV_CRC_ERROR);
        RETURN_X(OID_GEN_TRANSMIT_QUEUE_LENGTH);
        RETURN_X(OID_GEN_GET_TIME_CAPS);
        RETURN_X(OID_GEN_GET_NETCARD_TIME);
        RETURN_X(OID_GEN_NETCARD_LOAD);
        RETURN_X(OID_GEN_DEVICE_PROFILE);
        RETURN_X(OID_GEN_INIT_TIME_MS);
        RETURN_X(OID_GEN_RESET_COUNTS);
        RETURN_X(OID_GEN_MEDIA_SENSE_COUNTS);
        RETURN_X(OID_GEN_FRIENDLY_NAME);
        RETURN_X(OID_GEN_MINIPORT_INFO);
        RETURN_X(OID_GEN_RESET_VERIFY_PARAMETERS);

        /* IEEE 802.3 (Ethernet) OIDs */
        RETURN_X(OID_802_3_PERMANENT_ADDRESS);
        RETURN_X(OID_802_3_CURRENT_ADDRESS);
        RETURN_X(OID_802_3_MULTICAST_LIST);
        RETURN_X(OID_802_3_MAXIMUM_LIST_SIZE);
        RETURN_X(OID_802_3_MAC_OPTIONS);
        RETURN_X(OID_802_3_RCV_ERROR_ALIGNMENT);
        RETURN_X(OID_802_3_XMIT_ONE_COLLISION);
        RETURN_X(OID_802_3_XMIT_MORE_COLLISIONS);
        RETURN_X(OID_802_3_XMIT_DEFERRED);
        RETURN_X(OID_802_3_XMIT_MAX_COLLISIONS);
        RETURN_X(OID_802_3_RCV_OVERRUN);
        RETURN_X(OID_802_3_XMIT_UNDERRUN);
        RETURN_X(OID_802_3_XMIT_HEARTBEAT_FAILURE);
        RETURN_X(OID_802_3_XMIT_TIMES_CRS_LOST);
        RETURN_X(OID_802_3_XMIT_LATE_COLLISIONS);

        /* IEEE 802.11 (WLAN) OIDs */
        RETURN_X(OID_802_11_BSSID);
        RETURN_X(OID_802_11_SSID);
        RETURN_X(OID_802_11_NETWORK_TYPES_SUPPORTED);
        RETURN_X(OID_802_11_NETWORK_TYPE_IN_USE);
        RETURN_X(OID_802_11_TX_POWER_LEVEL);
        RETURN_X(OID_802_11_RSSI);
        RETURN_X(OID_802_11_RSSI_TRIGGER);
        RETURN_X(OID_802_11_INFRASTRUCTURE_MODE);
        RETURN_X(OID_802_11_FRAGMENTATION_THRESHOLD);
        RETURN_X(OID_802_11_RTS_THRESHOLD);
        RETURN_X(OID_802_11_NUMBER_OF_ANTENNAS);
        RETURN_X(OID_802_11_RX_ANTENNA_SELECTED);
        RETURN_X(OID_802_11_TX_ANTENNA_SELECTED);
        RETURN_X(OID_802_11_SUPPORTED_RATES);
        RETURN_X(OID_802_11_DESIRED_RATES);
        RETURN_X(OID_802_11_CONFIGURATION);
        RETURN_X(OID_802_11_STATISTICS);
        RETURN_X(OID_802_11_ADD_WEP);
        RETURN_X(OID_802_11_REMOVE_WEP);
        RETURN_X(OID_802_11_DISASSOCIATE);
        RETURN_X(OID_802_11_POWER_MODE);
        RETURN_X(OID_802_11_BSSID_LIST);
        RETURN_X(OID_802_11_AUTHENTICATION_MODE);
        RETURN_X(OID_802_11_PRIVACY_FILTER);
        RETURN_X(OID_802_11_BSSID_LIST_SCAN);
        RETURN_X(OID_802_11_WEP_STATUS);
        RETURN_X(OID_802_11_RELOAD_DEFAULTS);

        /* Power Management OIDs */
        RETURN_X(OID_PNP_CAPABILITIES);
        RETURN_X(OID_PNP_SET_POWER);
        RETURN_X(OID_PNP_QUERY_POWER);
        RETURN_X(OID_PNP_ENABLE_WAKE_UP);

        /* OID_GEN_MINIPORT_INFO constants */
        RETURN_X(NDIS_MINIPORT_BUS_MASTER);
        RETURN_X(NDIS_MINIPORT_WDM_DRIVER);
        RETURN_X(NDIS_MINIPORT_SG_LIST);
        RETURN_X(NDIS_MINIPORT_SUPPORTS_MEDIA_QUERY);
        RETURN_X(NDIS_MINIPORT_INDICATES_PACKETS);
        RETURN_X(NDIS_MINIPORT_IGNORE_PACKET_QUEUE);
        RETURN_X(NDIS_MINIPORT_IGNORE_REQUEST_QUEUE);
        RETURN_X(NDIS_MINIPORT_IGNORE_TOKEN_RING_ERRORS);
        RETURN_X(NDIS_MINIPORT_INTERMEDIATE_DRIVER);
        RETURN_X(NDIS_MINIPORT_IS_NDIS_5);
        RETURN_X(NDIS_MINIPORT_IS_CO);
        RETURN_X(NDIS_MINIPORT_DESERIALIZE);
        RETURN_X(NDIS_MINIPORT_REQUIRES_MEDIA_POLLING);
        RETURN_X(NDIS_MINIPORT_SUPPORTS_MEDIA_SENSE);
        RETURN_X(NDIS_MINIPORT_NETBOOT_CARD);
        RETURN_X(NDIS_MINIPORT_PM_SUPPORTED);
        RETURN_X(NDIS_MINIPORT_SUPPORTS_MAC_ADDRESS_OVERWRITE);
        RETURN_X(NDIS_MINIPORT_USES_SAFE_BUFFER_APIS);
        RETURN_X(NDIS_MINIPORT_HIDDEN);
        RETURN_X(NDIS_MINIPORT_SWENUM);
        RETURN_X(NDIS_MINIPORT_SURPRISE_REMOVE_OK);
        RETURN_X(NDIS_MINIPORT_NO_HALT_ON_SUSPEND);
        RETURN_X(NDIS_MINIPORT_HARDWARE_DEVICE);
        RETURN_X(NDIS_MINIPORT_SUPPORTS_CANCEL_SEND_PACKETS);
        RETURN_X(NDIS_MINIPORT_64BITS_DMA);
    default:
        return "<UNKNOWN>";
    }
#else
    UNREFERENCED_PARAMETER(Oid);
    return "!DBG";
#endif
}


#if DBG
/* ============================================================================
 * Interrupt Cause Register to String Conversion
 * ============================================================================ */

const char* E1000_IcrToString(IN ULONG Icr, OUT PCHAR Buffer, IN ULONG BufferSize)
{
    PCHAR p = Buffer;
    ULONG remaining = BufferSize;
    int written;

    if (Buffer == NULL || BufferSize < 32)
        return "BUFFER_TOO_SMALL";

    *p = '\0';

    if (Icr == 0)
    {
        return "NONE";
    }

#define APPEND_FLAG(flag, name) \
    if (Icr & (flag)) { \
        if (NT_SUCCESS(RtlStringCchPrintfA(p, remaining, "%s ", name))) { \
            written = (LONG)strlen(p); \
            p += written; \
            remaining -= written; \
        } \
    }

    APPEND_FLAG(E1000_IMS_TXDW, "TXDW");
    APPEND_FLAG(E1000_IMS_TXQE, "TXQE");
    APPEND_FLAG(E1000_IMS_LSC, "LSC");
    APPEND_FLAG(E1000_IMS_RXSEQ, "RXSEQ");
    APPEND_FLAG(E1000_IMS_RXDMT0, "RXDMT0");
    APPEND_FLAG(E1000_IMS_RXO, "RXO");
    APPEND_FLAG(E1000_IMS_RXT0, "RXT0");
    APPEND_FLAG(E1000_IMS_MDAC, "MDAC");
    APPEND_FLAG(E1000_IMS_PHYINT, "PHYINT");
    APPEND_FLAG(E1000_IMS_TXD_LOW, "TXD_LOW");
    APPEND_FLAG(E1000_IMS_SRPD, "SRPD");
    APPEND_FLAG(E1000_IMS_RXQ0, "RXQ0");
    APPEND_FLAG(E1000_IMS_RXQ1, "RXQ1");
    APPEND_FLAG(E1000_IMS_TXQ0, "TXQ0");
    APPEND_FLAG(E1000_IMS_TXQ1, "TXQ1");
    APPEND_FLAG(E1000_IMS_OTHER, "OTHER");

#undef APPEND_FLAG

    return Buffer;
}


/* ============================================================================
 * Debug Initialization
 * ============================================================================ */

VOID E1000_InitDebug(VOID)
{
    /* Clear all debug statistics */
    RtlZeroMemory(&DebugStats, sizeof(DebugStats));

    /* Record initialization time */
    KeQueryTickCount(&DebugStats.InitTime);

    DPRINT1("E1000: Debug subsystem initialized\n");
}


/* ============================================================================
 * Reset Debug Statistics
 * ============================================================================ */

VOID E1000_ResetDebugStats(VOID)
{
    LARGE_INTEGER InitTime = DebugStats.InitTime;

    RtlZeroMemory(&DebugStats, sizeof(DebugStats));

    /* Preserve initialization time */
    DebugStats.InitTime = InitTime;

    DPRINT("E1000: Debug statistics reset\n");
}


/* ============================================================================
 * Dump Driver State
 * ============================================================================ */

VOID E1000_DumpDriverState(IN PVOID AdapterContext)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)AdapterContext;
    ULONG TdtReg, TdhReg, RdtReg, RdhReg;

    if (Adapter == NULL)
    {
        DPRINT("E1000: Cannot dump state - NULL adapter\n");
        return;
    }

    DPRINT1("\n");
    DPRINT1("============================================================\n");
    DPRINT("E1000 Driver State Dump (NDIS 6.x)\n");
    DPRINT1("============================================================\n");

    /* Basic adapter info */
    DPRINT1("Adapter Handle:     %p\n", Adapter->MiniportAdapterHandle);
    DPRINT1("Vendor/Device ID:   %04x:%04x\n", Adapter->VendorId, Adapter->DeviceId);
    DPRINT1("Subsystem:          %04x:%04x\n", Adapter->SubsystemVendorId, Adapter->SubsystemId);
    DPRINT1("Is PCIe:            %s\n", Adapter->IsPCIe ? "Yes" : "No");

    /* MAC address */
    DPRINT1("MAC Address:        %02x:%02x:%02x:%02x:%02x:%02x\n",
             Adapter->PermanentMacAddress[0], Adapter->PermanentMacAddress[1],
             Adapter->PermanentMacAddress[2], Adapter->PermanentMacAddress[3],
             Adapter->PermanentMacAddress[4], Adapter->PermanentMacAddress[5]);

    /* Link status */
    DPRINT1("Media State:        %s\n",
             Adapter->MediaState == MediaConnectStateConnected ? "Connected" : "Disconnected");
    DPRINT1("Link Speed:         %I64u bps\n", Adapter->LinkSpeed);

    /* Memory mapping */
    DPRINT1("IoBase:             %p (PA: 0x%I64x, Len: %u)\n",
             Adapter->IoBase, Adapter->IoAddress.QuadPart, Adapter->IoLength);
    DPRINT1("IoPort:             0x%x (Len: %u)\n",
             Adapter->IoPortAddress, Adapter->IoPortLength);

    /* Interrupt info */
    DPRINT("Interrupt:          Vector=%u Level=%u Shared=%s\n",
             Adapter->InterruptVector, Adapter->InterruptLevel,
             Adapter->InterruptShared ? "Yes" : "No");
    DPRINT("Interrupt Mask:     0x%08x\n", Adapter->InterruptMask);

    /* TX state - read hardware registers */
    if (Adapter->IoBase)
    {
        TdhReg = E1000_READ_REG(Adapter, E1000_REG_TDH);
        TdtReg = E1000_READ_REG(Adapter, E1000_REG_TDT);
        RdhReg = E1000_READ_REG(Adapter, E1000_REG_RDH);
        RdtReg = E1000_READ_REG(Adapter, E1000_REG_RDT);
    }
    else
    {
        TdhReg = TdtReg = RdhReg = RdtReg = 0;
    }

    DPRINT1("\nTransmit State (Queue Count: %u):\n", Adapter->TxQueueCount);
    if (Adapter->TxQueueCount > 0)
    {
        PE1000_TX_QUEUE TxQueue = &Adapter->TxQueues[0];
        DPRINT1("  Queue 0:\n");
        DPRINT("    Descriptors:    %p\n", TxQueue->Descriptors);
        DPRINT1("    Head/Tail:      %u / %u\n", TxQueue->Head, TxQueue->Tail);
        DPRINT1("    HW Head/Tail:   %u / %u\n", TdhReg, TdtReg);
    }

    DPRINT1("\nReceive State (Queue Count: %u):\n", Adapter->RxQueueCount);
    if (Adapter->RxQueueCount > 0)
    {
        PE1000_RX_QUEUE RxQueue = &Adapter->RxQueues[0];
        DPRINT1("  Queue 0:\n");
        DPRINT("    Descriptors:    %p\n", RxQueue->Descriptors);
        DPRINT1("    Head/Tail:      %u / %u\n", RxQueue->Head, RxQueue->Tail);
        DPRINT1("    HW Head/Tail:   %u / %u\n", RdhReg, RdtReg);
    }

    /* Checksum offload */
    DPRINT1("\nChecksum Offload:\n");
    DPRINT1("  TX IP Enabled:    %s\n", Adapter->ChecksumOffload.TxIpChecksumEnabled ? "Yes" : "No");
    DPRINT1("  RX IP Enabled:    %s\n", Adapter->ChecksumOffload.RxIpChecksumEnabled ? "Yes" : "No");

    /* Power state */
    DPRINT1("\nPower Management:\n");
    DPRINT1("  Current State:    D%d\n", Adapter->CurrentPowerState);
    DPRINT("  Wake on Magic:    %s\n", Adapter->WakeOnMagicPacket ? "Yes" : "No");
    DPRINT1("  Wake on Link:     %s\n", Adapter->WakeOnLinkChange ? "Yes" : "No");

    /* Packet filter */
    DPRINT("\nPacket Filter:      0x%08x\n", Adapter->PacketFilter);
    DPRINT1("Flags:              0x%08x\n", Adapter->Flags);

    DPRINT1("============================================================\n\n");
}


/* ============================================================================
 * Dump Debug Statistics
 * ============================================================================ */

VOID E1000_DumpStatistics(VOID)
{
    LARGE_INTEGER Now;
    ULONG64 Uptime;

    KeQueryTickCount(&Now);
    Uptime = (Now.QuadPart - DebugStats.InitTime.QuadPart);

    DPRINT1("\n");
    DPRINT1("============================================================\n");
    DPRINT("E1000 Debug Statistics\n");
    DPRINT1("============================================================\n");

    DPRINT1("Uptime (ticks):     %I64u\n", Uptime);

    DPRINT("\nTransmit Statistics:\n");
    DPRINT("  TX Attempts:      %I64d\n", DebugStats.TxAttempts);
    DPRINT("  TX Success:       %I64d\n", DebugStats.TxSuccess);
    DPRINT("  TX Failed:        %I64d\n", DebugStats.TxFailed);
    DPRINT("  TX Bytes:         %I64d\n", DebugStats.TxBytes);
    DPRINT("  TX Dropped:       %I64d\n", DebugStats.TxDropped);
    DPRINT("  TX Ring Full:     %u\n", DebugStats.TxRingFull);
    DPRINT("  TX Batch Calls:   %u\n", DebugStats.TxBatchCount);
    DPRINT("  TX Single Calls:  %u\n", DebugStats.TxSingleCount);
    DPRINT("  TX Max Desc Used: %u\n", DebugStats.TxMaxDescriptorsUsed);

    DPRINT("\nReceive Statistics:\n");
    DPRINT("  RX Attempts:      %I64d\n", DebugStats.RxAttempts);
    DPRINT("  RX Success:       %I64d\n", DebugStats.RxSuccess);
    DPRINT("  RX Failed:        %I64d\n", DebugStats.RxFailed);
    DPRINT("  RX Bytes:         %I64d\n", DebugStats.RxBytes);
    DPRINT("  RX Dropped:       %I64d\n", DebugStats.RxDropped);
    DPRINT("  RX No Buffer:     %u\n", DebugStats.RxNoBuffer);
    DPRINT("  RX Checksum Good: %u\n", DebugStats.RxChecksumGood);
    DPRINT("  RX Checksum Bad:  %u\n", DebugStats.RxChecksumBad);
    DPRINT("  RX CRC Errors:    %u\n", DebugStats.RxCrcErrors);
    DPRINT("  RX Align Errors:  %u\n", DebugStats.RxAlignErrors);
    DPRINT("  RX Multi-Desc:    %u\n", DebugStats.RxMultiDesc);

    DPRINT("\nInterrupt Statistics:\n");
    DPRINT("  Total Interrupts: %I64d\n", DebugStats.Interrupts);
    DPRINT("  Spurious:         %u\n", DebugStats.SpuriousInterrupts);
    DPRINT("  TX Interrupts:    %u\n", DebugStats.TxInterrupts);
    DPRINT("  RX Interrupts:    %u\n", DebugStats.RxInterrupts);
    DPRINT("  Link Interrupts:  %u\n", DebugStats.LinkInterrupts);
    DPRINT("  Other:            %u\n", DebugStats.OtherInterrupts);
    DPRINT("  Unhandled:        %u\n", DebugStats.UnhandledInterrupts);

    DPRINT("\nInitialization Statistics:\n");
    DPRINT("  Init Attempts:    %u\n", DebugStats.InitAttempts);
    DPRINT("  Init Success:     %u\n", DebugStats.InitSuccess);
    DPRINT("  Init Failed:      %u\n", DebugStats.InitFailed);
    DPRINT("  Reset Count:      %u\n", DebugStats.ResetCount);

    DPRINT1("\nPower Management:\n");
    DPRINT("  Transitions:      %u\n", DebugStats.PowerTransitions);
    DPRINT("  Wake Events:      %u\n", DebugStats.WakeEvents);

    DPRINT1("============================================================\n\n");
}


/* ============================================================================
 * Dump TX Descriptor Ring
 * ============================================================================ */

VOID E1000_DumpTxRing(IN PVOID AdapterContext)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)AdapterContext;
    PE1000_TX_QUEUE TxQueue;
    volatile PE1000_TRANSMIT_DESCRIPTOR Desc;
    ULONG i;
    ULONG TdhReg, TdtReg;

    if (Adapter == NULL || Adapter->TxQueueCount == 0)
    {
        DPRINT("E1000: Cannot dump TX ring - NULL adapter or no queues\n");
        return;
    }

    TxQueue = &Adapter->TxQueues[0];
    if (TxQueue->Descriptors == NULL)
    {
        DPRINT("E1000: Cannot dump TX ring - NULL descriptors\n");
        return;
    }

    /* Read hardware registers */
    if (Adapter->IoBase)
    {
        TdhReg = E1000_READ_REG(Adapter, E1000_REG_TDH);
        TdtReg = E1000_READ_REG(Adapter, E1000_REG_TDT);
    }
    else
    {
        TdhReg = TdtReg = 0;
    }

    DPRINT1("\n");
    DPRINT1("============================================================\n");
    DPRINT("E1000 TX Descriptor Ring Dump (NDIS 6.x)\n");
    DPRINT1("============================================================\n");
    DPRINT("Ring Base:    %p\n", TxQueue->Descriptors);
    DPRINT1("SW Head:      %u\n", TxQueue->Head);
    DPRINT1("SW Tail:      %u\n", TxQueue->Tail);
    DPRINT1("HW Head:      %u\n", TdhReg);
    DPRINT1("HW Tail:      %u\n", TdtReg);
    DPRINT1("Count:        %u\n", TxQueue->Count);
    DPRINT("\nDescriptor Details (showing active range):\n");
    DPRINT1("Idx  Address          Length Cmd  Stat\n");
    DPRINT1("---- ---------------- ------ ---- ----\n");

    /* Show descriptors around the current position */
    for (i = 0; i < TxQueue->Count && i < E1000_NUM_TX_DESC; i++)
    {
        Desc = TxQueue->Descriptors + i;

        /* Only print non-empty descriptors or those near head/tail */
        if (Desc->Address != 0 || Desc->Status != 0 ||
            i == TxQueue->Head || i == TxQueue->Tail ||
            i == TdhReg || i == TdtReg)
        {
            DPRINT1("%3u%s %016I64x %5u  %02x   %02x\n",
                     i,
                     (i == TxQueue->Head) ? "H" :
                     (i == TxQueue->Tail) ? "T" :
                     (i == TdhReg) ? "h" :
                     (i == TdtReg) ? "t" : " ",
                     Desc->Address,
                     Desc->Length,
                     Desc->Command,
                     Desc->Status);
        }
    }

    DPRINT1("============================================================\n\n");
}


/* ============================================================================
 * Dump RX Descriptor Ring
 * ============================================================================ */

VOID E1000_DumpRxRing(IN PVOID AdapterContext)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)AdapterContext;
    PE1000_RX_QUEUE RxQueue;
    volatile PE1000_RECEIVE_DESCRIPTOR Desc;
    ULONG i;
    ULONG RdhReg, RdtReg;

    if (Adapter == NULL || Adapter->RxQueueCount == 0)
    {
        DPRINT("E1000: Cannot dump RX ring - NULL adapter or no queues\n");
        return;
    }

    RxQueue = &Adapter->RxQueues[0];
    if (RxQueue->Descriptors == NULL)
    {
        DPRINT("E1000: Cannot dump RX ring - NULL descriptors\n");
        return;
    }

    /* Read hardware registers */
    if (Adapter->IoBase)
    {
        RdhReg = E1000_READ_REG(Adapter, E1000_REG_RDH);
        RdtReg = E1000_READ_REG(Adapter, E1000_REG_RDT);
    }
    else
    {
        RdhReg = RdtReg = 0;
    }

    DPRINT1("\n");
    DPRINT1("============================================================\n");
    DPRINT("E1000 RX Descriptor Ring Dump (NDIS 6.x)\n");
    DPRINT1("============================================================\n");
    DPRINT("Ring Base:    %p\n", RxQueue->Descriptors);
    DPRINT1("SW Head:      %u\n", RxQueue->Head);
    DPRINT1("SW Tail:      %u\n", RxQueue->Tail);
    DPRINT1("HW Head:      %u\n", RdhReg);
    DPRINT1("HW Tail:      %u\n", RdtReg);
    DPRINT1("Count:        %u\n", RxQueue->Count);
    DPRINT("\nDescriptor Details (showing descriptors with status):\n");
    DPRINT1("Idx  Address          Length Status Errors\n");
    DPRINT1("---- ---------------- ------ ------ ------\n");

    /* Show descriptors with status set */
    for (i = 0; i < RxQueue->Count && i < E1000_NUM_RX_DESC; i++)
    {
        Desc = RxQueue->Descriptors + i;

        /* Only print descriptors with status or near head/tail */
        if (Desc->Status != 0 || i == RdhReg || i == RdtReg ||
            i == RxQueue->Head || i == RxQueue->Tail)
        {
            DPRINT1("%3u%s %016I64x %5u    %02x     %02x\n",
                     i,
                     (i == RxQueue->Head) ? "H" :
                     (i == RxQueue->Tail) ? "T" :
                     (i == RdhReg) ? "h" :
                     (i == RdtReg) ? "t" : " ",
                     Desc->Address,
                     Desc->Length,
                     Desc->Status,
                     Desc->Errors);
        }
    }

    DPRINT1("============================================================\n\n");
}

#else /* !DBG */

/* Release build stubs */
VOID E1000_InitDebug(VOID) {}
VOID E1000_ResetDebugStats(VOID) {}
VOID E1000_DumpDriverState(IN PVOID AdapterContext) { UNREFERENCED_PARAMETER(AdapterContext); }
VOID E1000_DumpStatistics(VOID) {}
VOID E1000_DumpTxRing(IN PVOID AdapterContext) { UNREFERENCED_PARAMETER(AdapterContext); }
VOID E1000_DumpRxRing(IN PVOID AdapterContext) { UNREFERENCED_PARAMETER(AdapterContext); }
const char* E1000_IcrToString(IN ULONG Icr, OUT PCHAR Buffer, IN ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(Icr);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferSize);
    return "";
}

#endif /* DBG */
