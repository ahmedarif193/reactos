/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Interrupt handlers
 * COPYRIGHT:   2013 Cameron Gutman (cameron.gutman@reactos.org)
 *              2018 Mark Jansen (mark.jansen@reactos.org)
 *              2019 Victor Perevertkin (victor.perevertkin@reactos.org)
 *              2024 ReactOS Team - Checksum offload, MSI-X queue support, larger rings
 */

#include "nic.h"

#include <debug.h>


/* ============================================================================
 * Interrupt Service Routine (ISR)
 * ============================================================================ */

VOID
NTAPI
MiniportISR(
    OUT PBOOLEAN InterruptRecognized,
    OUT PBOOLEAN QueueMiniportHandleInterrupt,
    IN NDIS_HANDLE MiniportAdapterContext)
{
    ULONG Value;
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    /* Reading the interrupt acknowledges them */
    E1000ReadUlong(Adapter, E1000_REG_ICR, &Value);

    /* Log raw ICR value */
    E1000_INT_DBG(("ISR: raw ICR=0x%08x mask=0x%08x\n", Value, Adapter->InterruptMask));

    Value &= Adapter->InterruptMask;
    _InterlockedOr(&Adapter->InterruptPending, Value);

    if (Value)
    {
        *InterruptRecognized = TRUE;
        /* Mark the events pending service */
        *QueueMiniportHandleInterrupt = TRUE;

        E1000_STAT_INC(Interrupts);
        E1000_LOG_INTERRUPT(Value);

#if DBG
        KeQueryTickCount(&DebugStats.LastInterruptTime);
#endif
    }
    else
    {
        /* This is not ours. */
        *InterruptRecognized = FALSE;
        *QueueMiniportHandleInterrupt = FALSE;

        E1000_STAT_INC32(SpuriousInterrupts);
        E1000_INT_DBG(("ISR: spurious interrupt (masked value=0)\n"));
    }
}


/* ============================================================================
 * Deferred Procedure Call (DPC) - Handle Interrupt
 * ============================================================================ */

VOID
NTAPI
MiniportHandleInterrupt(
    IN NDIS_HANDLE MiniportAdapterContext)
{
    ULONG InterruptPending;
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    volatile PE1000_TRANSMIT_DESCRIPTOR TransmitDescriptor;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    InterruptPending = _InterlockedExchange(&Adapter->InterruptPending, 0);

    E1000_INT_DBG(("HandleInterrupt: pending=0x%08x\n", InterruptPending));

    /* ========================================================================
     * Link State Changed
     * ======================================================================== */
    if (InterruptPending & E1000_IMS_LSC)
    {
        ULONG Status;
        ULONG OldMediaState = Adapter->MediaState;
        ULONG OldLinkSpeed = Adapter->LinkSpeedMbps;

        InterruptPending &= ~E1000_IMS_LSC;
        NDIS_DbgPrint(MAX_TRACE, ("Link status changed!.\n"));

        E1000_STAT_INC32(LinkInterrupts);

        NICUpdateLinkStatus(Adapter);

        /* Log link state change details */
        E1000_LINK_DBG(("Link change: %s -> %s, speed: %u -> %u Mbps\n",
                        OldMediaState == NdisMediaStateConnected ? "Connected" : "Disconnected",
                        Adapter->MediaState == NdisMediaStateConnected ? "Connected" : "Disconnected",
                        OldLinkSpeed, Adapter->LinkSpeedMbps));

        Status = Adapter->MediaState == NdisMediaStateConnected ? NDIS_STATUS_MEDIA_CONNECT : NDIS_STATUS_MEDIA_DISCONNECT;

        NdisMIndicateStatus(Adapter->AdapterHandle, Status, NULL, 0);
        NdisMIndicateStatusComplete(Adapter->AdapterHandle);
    }

    /* ========================================================================
     * PHY Interrupt (some devices signal link changes this way)
     * ======================================================================== */
    if (InterruptPending & E1000_IMS_PHYINT)
    {
        InterruptPending &= ~E1000_IMS_PHYINT;
        NDIS_DbgPrint(MAX_TRACE, ("PHY interrupt - updating link status.\n"));

        E1000_INT_DBG(("PHY interrupt received\n"));
        E1000_STAT_INC32(OtherInterrupts);

        NICUpdateLinkStatus(Adapter);
    }

    /* ========================================================================
     * Receive Overrun
     * ======================================================================== */
    if (InterruptPending & E1000_IMS_RXO)
    {
        InterruptPending &= ~E1000_IMS_RXO;
        NDIS_DbgPrint(MID_TRACE, ("Receive overrun detected!\n"));

        E1000_RX_DBG(("RX OVERRUN detected!\n"));
        E1000_STAT_INC32(RxNoBuffer);

        Adapter->Statistics.RxNoBuffer++;
    }

    /* ========================================================================
     * Receive Interrupts - including MSI-X queue interrupts for 82574L
     * ======================================================================== */
    if (InterruptPending & (E1000_IMS_RXDMT0 | E1000_IMS_RXT0 | E1000_IMS_RXQ0 | E1000_IMS_RXQ1))
    {
        volatile PE1000_RECEIVE_DESCRIPTOR ReceiveDescriptor;
        PETH_HEADER EthHeader;
        ULONG BufferOffset;
        BOOLEAN bGotAny = FALSE;
        ULONG RxDescHead, RxDescTail, CurrRxDesc;
        ULONG PacketsReceived = 0;
        ULONG BytesReceived = 0;
        ULONG ErrorCount = 0;

        /* Log which RX interrupt sources fired */
        E1000_INT_DBG(("RX interrupt: RXDMT0=%d RXT0=%d RXQ0=%d RXQ1=%d\n",
                       !!(InterruptPending & E1000_IMS_RXDMT0),
                       !!(InterruptPending & E1000_IMS_RXT0),
                       !!(InterruptPending & E1000_IMS_RXQ0),
                       !!(InterruptPending & E1000_IMS_RXQ1)));

        E1000_STAT_INC32(RxInterrupts);

        /* Clear out these interrupts */
        InterruptPending &= ~(E1000_IMS_RXDMT0 | E1000_IMS_RXT0 | E1000_IMS_RXQ0 | E1000_IMS_RXQ1);

        NdisAcquireSpinLock(&Adapter->RxLock);

        E1000ReadUlong(Adapter, E1000_REG_RDH, &RxDescHead);
        E1000ReadUlong(Adapter, E1000_REG_RDT, &RxDescTail);

        E1000_LOG_RX_RING(RxDescHead, RxDescTail);

        while (((RxDescTail + 1) % NUM_RECEIVE_DESCRIPTORS) != RxDescHead)
        {
            CurrRxDesc = (RxDescTail + 1) % NUM_RECEIVE_DESCRIPTORS;
            BufferOffset = CurrRxDesc * Adapter->ReceiveBufferEntrySize;
            ReceiveDescriptor = Adapter->ReceiveDescriptors + CurrRxDesc;

            E1000_ASSERT_RX_DESC_INDEX(CurrRxDesc);
            E1000_STAT_INC(RxAttempts);

            /* Check if the hardware have released this descriptor (DD - Descriptor Done) */
            if (!(ReceiveDescriptor->Status & E1000_RDESC_STATUS_DD))
            {
                /* No need to check descriptors after the first unfinished one */
                E1000_RX_DBG(("RX desc %u: not ready (no DD)\n", CurrRxDesc));
                break;
            }

            /* Log receive descriptor details */
            E1000_LOG_RX_PACKET(DebugStats.RxSuccess, ReceiveDescriptor->Length,
                                CurrRxDesc, ReceiveDescriptor->Status,
                                ReceiveDescriptor->Errors);

            /* Check for receive errors */
            if (ReceiveDescriptor->Errors)
            {
                NDIS_DbgPrint(MID_TRACE, ("Receive error: 0x%02x\n", ReceiveDescriptor->Errors));
                Adapter->Statistics.RxErrors++;
                ErrorCount++;

                E1000_RX_DBG(("RX desc %u: ERROR 0x%02x [%s%s%s%s%s%s]\n",
                              CurrRxDesc, ReceiveDescriptor->Errors,
                              (ReceiveDescriptor->Errors & E1000_RDESC_ERR_CE) ? "CRC " : "",
                              (ReceiveDescriptor->Errors & E1000_RDESC_ERR_SE) ? "SEQ " : "",
                              (ReceiveDescriptor->Errors & E1000_RDESC_ERR_SEQ) ? "SYMBOL " : "",
                              (ReceiveDescriptor->Errors & E1000_RDESC_ERR_CXE) ? "CARRIER " : "",
                              (ReceiveDescriptor->Errors & E1000_RDESC_ERR_IPE) ? "IP_CSUM " : "",
                              (ReceiveDescriptor->Errors & E1000_RDESC_ERR_TCPE) ? "TCP_CSUM " : ""));

                E1000_STAT_INC(RxFailed);

                /* Track specific error types */
                if (ReceiveDescriptor->Errors & E1000_RDESC_ERR_CE)
                {
                    Adapter->Statistics.RxCrcErrors++;
                    E1000_STAT_INC32(RxCrcErrors);
                }
                if (ReceiveDescriptor->Errors & E1000_RDESC_ERR_SE)
                {
                    Adapter->Statistics.RxAlignErrors++;
                    E1000_STAT_INC32(RxAlignErrors);
                }
                if (ReceiveDescriptor->Errors & E1000_RDESC_ERR_IPE)
                {
                    NDIS_DbgPrint(MID_TRACE, ("IP checksum error detected by hardware\n"));
                    E1000_CSUM_DBG(("RX IP checksum ERROR on desc %u\n", CurrRxDesc));
                    E1000_STAT_INC32(RxChecksumBad);
                }
                if (ReceiveDescriptor->Errors & E1000_RDESC_ERR_TCPE)
                {
                    NDIS_DbgPrint(MID_TRACE, ("TCP/UDP checksum error detected by hardware\n"));
                    E1000_CSUM_DBG(("RX TCP/UDP checksum ERROR on desc %u\n", CurrRxDesc));
                    E1000_STAT_INC32(RxChecksumBad);
                }

                /* Give the descriptor back and continue */
                ReceiveDescriptor->Status = 0;
                RxDescTail = CurrRxDesc;
                continue;
            }

            /* Check for End of Packet - we don't support multi-descriptor packets yet */
            if (!(ReceiveDescriptor->Status & E1000_RDESC_STATUS_EOP))
            {
                NDIS_DbgPrint(MIN_TRACE, ("Multi-descriptor packet not supported\n"));
                E1000_RX_DBG(("RX desc %u: multi-descriptor packet (no EOP), dropping\n", CurrRxDesc));
                E1000_STAT_INC32(RxMultiDesc);
                E1000_STAT_INC(RxDropped);
                ReceiveDescriptor->Status = 0;
                RxDescTail = CurrRxDesc;
                continue;
            }

            /* Make sure the receive indications are enabled */
            if (!Adapter->PacketFilter)
            {
                E1000_RX_DBG(("RX desc %u: packet filter disabled, dropping\n", CurrRxDesc));
                E1000_STAT_INC(RxDropped);
                goto NextReceiveDescriptor;
            }

            if (ReceiveDescriptor->Length != 0 && ReceiveDescriptor->Address != 0)
            {
                EthHeader = (PETH_HEADER)(Adapter->ReceiveBuffer + BufferOffset);

                /*
                 * Hardware checksum offload status is available in the descriptor:
                 * - E1000_RDESC_STATUS_IPCS: IP checksum was calculated
                 * - E1000_RDESC_STATUS_TCPCS: TCP/UDP checksum was calculated
                 * - E1000_RDESC_STATUS_IXSM: Ignore checksum indication
                 *
                 * Errors are indicated by:
                 * - E1000_RDESC_ERR_IPE: IP checksum error
                 * - E1000_RDESC_ERR_TCPE: TCP/UDP checksum error
                 *
                 * We handle checksum errors above, so packets here have valid checksums
                 * if those status bits are set.
                 */

                /* Log checksum offload status */
                if (Adapter->ChecksumOffload.RxChecksumEnabled)
                {
                    if (ReceiveDescriptor->Status & E1000_RDESC_STATUS_IXSM)
                    {
                        E1000_CSUM_DBG(("RX desc %u: checksum ignored (IXSM)\n", CurrRxDesc));
                        E1000_STAT_INC32(RxChecksumNone);
                    }
                    else
                    {
                        if (ReceiveDescriptor->Status & E1000_RDESC_STATUS_IPCS)
                        {
                            E1000_CSUM_DBG(("RX desc %u: IP checksum OK\n", CurrRxDesc));
                            E1000_STAT_INC32(RxChecksumGood);
                        }
                        if (ReceiveDescriptor->Status & E1000_RDESC_STATUS_TCPCS)
                        {
                            E1000_CSUM_DBG(("RX desc %u: TCP/UDP checksum OK\n", CurrRxDesc));
                        }
                    }
                }

                E1000_RX_DBG(("RX desc %u: indicating packet len=%u dst=%02x:%02x:%02x:%02x:%02x:%02x\n",
                              CurrRxDesc, ReceiveDescriptor->Length,
                              EthHeader->Destination[0], EthHeader->Destination[1],
                              EthHeader->Destination[2], EthHeader->Destination[3],
                              EthHeader->Destination[4], EthHeader->Destination[5]));

                NdisMEthIndicateReceive(Adapter->AdapterHandle,
                                        NULL,
                                        (PCHAR)EthHeader,
                                        sizeof(ETH_HEADER),
                                        (PCHAR)(EthHeader + 1),
                                        ReceiveDescriptor->Length - sizeof(ETH_HEADER),
                                        ReceiveDescriptor->Length - sizeof(ETH_HEADER));

                bGotAny = TRUE;
                PacketsReceived++;
                BytesReceived += ReceiveDescriptor->Length;

                E1000_STAT_INC(RxSuccess);
                E1000_STAT_ADD(RxBytes, ReceiveDescriptor->Length);
            }
            else
            {
                NDIS_DbgPrint(MIN_TRACE, ("Got a NULL descriptor"));
                E1000_RX_DBG(("RX desc %u: NULL descriptor (len=%u addr=0x%I64x)\n",
                              CurrRxDesc, ReceiveDescriptor->Length, ReceiveDescriptor->Address));
                E1000_STAT_INC(RxFailed);
            }

NextReceiveDescriptor:
            /* Give the descriptor back */
            ReceiveDescriptor->Status = 0;

            RxDescTail = CurrRxDesc;
        }

        /* Update statistics */
        Adapter->Statistics.RxPackets += PacketsReceived;
        Adapter->Statistics.RxBytes += BytesReceived;

        NdisReleaseSpinLock(&Adapter->RxLock);

        if (bGotAny)
        {
            /* Write back new tail value */
            E1000WriteUlong(Adapter, E1000_REG_RDT, RxDescTail);

            NDIS_DbgPrint(MAX_TRACE, ("Rx done (RDH: %u, RDT: %u, Packets: %u, Bytes: %u)\n",
                                      RxDescHead, RxDescTail, PacketsReceived, BytesReceived));

            E1000_RX_DBG(("RX complete: %u packets, %u bytes, %u errors, RDT=%u\n",
                          PacketsReceived, BytesReceived, ErrorCount, RxDescTail));

#if DBG
            KeQueryTickCount(&DebugStats.LastRxTime);
#endif

            NdisMEthIndicateReceiveComplete(Adapter->AdapterHandle);
        }
        else
        {
            E1000_RX_DBG(("RX interrupt: no packets ready\n"));
        }
    }

    /* ========================================================================
     * Transmit Interrupts - including MSI-X queue interrupts for 82574L
     * ======================================================================== */
    if (InterruptPending & (E1000_IMS_TXD_LOW | E1000_IMS_TXDW | E1000_IMS_TXQE | E1000_IMS_TXQ0 | E1000_IMS_TXQ1))
    {
        /* Increased array size for larger descriptor rings (256) */
        PNDIS_PACKET AckPackets[64] = {0};
        ULONG NumPackets = 0, i;
        ULONG PacketsCompleted = 0;
        ULONG DescriptorsFreed = 0;

        /* Log which TX interrupt sources fired */
        E1000_INT_DBG(("TX interrupt: TXD_LOW=%d TXDW=%d TXQE=%d TXQ0=%d TXQ1=%d\n",
                       !!(InterruptPending & E1000_IMS_TXD_LOW),
                       !!(InterruptPending & E1000_IMS_TXDW),
                       !!(InterruptPending & E1000_IMS_TXQE),
                       !!(InterruptPending & E1000_IMS_TXQ0),
                       !!(InterruptPending & E1000_IMS_TXQ1)));

        E1000_STAT_INC32(TxInterrupts);

        /* Clear out these interrupts */
        InterruptPending &= ~(E1000_IMS_TXD_LOW | E1000_IMS_TXDW | E1000_IMS_TXQE | E1000_IMS_TXQ0 | E1000_IMS_TXQ1);

        NdisAcquireSpinLock(&Adapter->TxLock);

        E1000_TX_DBG(("TX completion: current=%u last=%u full=%d\n",
                      Adapter->CurrentTxDesc, Adapter->LastTxDesc, Adapter->TxFull));

        while ((Adapter->TxFull || Adapter->LastTxDesc != Adapter->CurrentTxDesc) && NumPackets < ARRAYSIZE(AckPackets))
        {
            TransmitDescriptor = Adapter->TransmitDescriptors + Adapter->LastTxDesc;

            if (TransmitDescriptor->Status & E1000_TDESC_STATUS_DD)
            {
                E1000_LOG_TX_COMPLETE(Adapter->LastTxDesc, TransmitDescriptor->Status);

                if (Adapter->TransmitPackets[Adapter->LastTxDesc])
                {
                    AckPackets[NumPackets++] = Adapter->TransmitPackets[Adapter->LastTxDesc];
                    Adapter->TransmitPackets[Adapter->LastTxDesc] = NULL;
                    PacketsCompleted++;

                    E1000_TX_DBG(("TX desc %u: complete, packet queued for ack\n", Adapter->LastTxDesc));
                    E1000_STAT_INC(TxSuccess);
                }
                else
                {
                    E1000_TX_DBG(("TX desc %u: complete (SG fragment, no packet)\n", Adapter->LastTxDesc));
                }

                TransmitDescriptor->Status = 0;
                DescriptorsFreed++;

                Adapter->LastTxDesc = (Adapter->LastTxDesc + 1) % NUM_TRANSMIT_DESCRIPTORS;
                Adapter->TxFull = FALSE;
            }
            else
            {
                E1000_TX_DBG(("TX desc %u: not complete (status=0x%02x)\n",
                              Adapter->LastTxDesc, TransmitDescriptor->Status));
                break;
            }
        }

        /* Update statistics */
        Adapter->Statistics.TxPackets += PacketsCompleted;

#if DBG
        /* Update descriptor usage tracking */
        DebugStats.TxDescriptorsUsed = NUM_TRANSMIT_DESCRIPTORS - NICGetFreeTxDescriptors(Adapter);
#endif

        NdisReleaseSpinLock(&Adapter->TxLock);

        if (NumPackets)
        {
            NDIS_DbgPrint(MAX_TRACE, ("Tx: (TDH: %u, TDT: %u)\n", Adapter->CurrentTxDesc, Adapter->LastTxDesc));
            NDIS_DbgPrint(MAX_TRACE, ("Tx Done: %u packets to ack\n", NumPackets));

            E1000_TX_DBG(("TX complete: %u packets, %u descriptors freed, last=%u\n",
                          NumPackets, DescriptorsFreed, Adapter->LastTxDesc));

            for (i = 0; i < NumPackets; ++i)
            {
                NdisMSendComplete(Adapter->AdapterHandle, AckPackets[i], NDIS_STATUS_SUCCESS);
            }
        }
        else
        {
            E1000_TX_DBG(("TX interrupt: no completions\n"));
        }
    }

    /* ========================================================================
     * Handle any unprocessed interrupts
     * ======================================================================== */
    if (InterruptPending != 0)
    {
        NDIS_DbgPrint(MID_TRACE, ("Unhandled interrupt bits: 0x%08x\n", InterruptPending));

        E1000_INT_DBG(("Unhandled interrupt bits: 0x%08x\n", InterruptPending));
        E1000_STAT_INC32(UnhandledInterrupts);

        /* Track other interrupt types */
        E1000_STAT_INC32(OtherInterrupts);
    }
}
