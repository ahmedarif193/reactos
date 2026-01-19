/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Checksum offload configuration
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements checksum offload configuration:
 *   - E1000InitializeOffloadCapabilities
 *   - E1000SetOffloadParameters
 */

#include "e1000.h"

/* ============================================================================
 * E1000InitializeOffloadCapabilities - Report hardware offload capabilities
 *
 * Fills in the NDIS_OFFLOAD structure with E1000 capabilities.
 * ============================================================================ */

NDIS_STATUS
E1000InitializeOffloadCapabilities(
    _In_ PE1000_ADAPTER Adapter,
    _Out_ PNDIS_OFFLOAD OffloadCapabilities
    )
{
    NdisZeroMemory(OffloadCapabilities, sizeof(NDIS_OFFLOAD));

    /* Set header */
    OffloadCapabilities->Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
    OffloadCapabilities->Header.Revision = NDIS_OFFLOAD_REVISION_3;
    OffloadCapabilities->Header.Size = sizeof(NDIS_OFFLOAD);

    /*
     * E1000 checksum offload capabilities:
     *   - IPv4 header checksum (TX and RX)
     *   - TCP checksum over IPv4 (TX and RX)
     *   - UDP checksum over IPv4 (TX and RX)
     *   - TCPv6 checksum (82574L only)
     *   - UDPv6 checksum (82574L only)
     */

    /* IPv4 TX Checksum */
    OffloadCapabilities->Checksum.IPv4Transmit.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    OffloadCapabilities->Checksum.IPv4Transmit.IpOptionsSupported = TRUE;
    OffloadCapabilities->Checksum.IPv4Transmit.TcpOptionsSupported = TRUE;
    OffloadCapabilities->Checksum.IPv4Transmit.TcpChecksum = TRUE;
    OffloadCapabilities->Checksum.IPv4Transmit.UdpChecksum = TRUE;
    OffloadCapabilities->Checksum.IPv4Transmit.IpChecksum = TRUE;

    /* IPv4 RX Checksum */
    OffloadCapabilities->Checksum.IPv4Receive.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    OffloadCapabilities->Checksum.IPv4Receive.IpOptionsSupported = TRUE;
    OffloadCapabilities->Checksum.IPv4Receive.TcpOptionsSupported = TRUE;
    OffloadCapabilities->Checksum.IPv4Receive.TcpChecksum = TRUE;
    OffloadCapabilities->Checksum.IPv4Receive.UdpChecksum = TRUE;
    OffloadCapabilities->Checksum.IPv4Receive.IpChecksum = TRUE;

    /* IPv6 TX Checksum (82574L PCIe only) */
    if (Adapter->IsPCIe)
    {
        OffloadCapabilities->Checksum.IPv6Transmit.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
        OffloadCapabilities->Checksum.IPv6Transmit.IpExtensionHeadersSupported = FALSE;
        OffloadCapabilities->Checksum.IPv6Transmit.TcpOptionsSupported = TRUE;
        OffloadCapabilities->Checksum.IPv6Transmit.TcpChecksum = TRUE;
        OffloadCapabilities->Checksum.IPv6Transmit.UdpChecksum = TRUE;

        OffloadCapabilities->Checksum.IPv6Receive.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
        OffloadCapabilities->Checksum.IPv6Receive.IpExtensionHeadersSupported = FALSE;
        OffloadCapabilities->Checksum.IPv6Receive.TcpOptionsSupported = TRUE;
        OffloadCapabilities->Checksum.IPv6Receive.TcpChecksum = TRUE;
        OffloadCapabilities->Checksum.IPv6Receive.UdpChecksum = TRUE;
    }

    /*
     * Large Send Offload (LSO) - Not supported in initial implementation
     * Can be added later for better performance with large transfers.
     */
    OffloadCapabilities->LsoV1.IPv4.Encapsulation = NDIS_ENCAPSULATION_NOT_SUPPORTED;
    OffloadCapabilities->LsoV2.IPv4.Encapsulation = NDIS_ENCAPSULATION_NOT_SUPPORTED;
    OffloadCapabilities->LsoV2.IPv6.Encapsulation = NDIS_ENCAPSULATION_NOT_SUPPORTED;

    /* IPsec offload not supported */
    /* RSC (Receive Segment Coalescing) not supported */

    /* Set default enabled state */
    Adapter->ChecksumOffload.TxIpChecksumEnabled = TRUE;
    Adapter->ChecksumOffload.TxTcpChecksumEnabled = TRUE;
    Adapter->ChecksumOffload.TxUdpChecksumEnabled = TRUE;
    Adapter->ChecksumOffload.RxIpChecksumEnabled = TRUE;
    Adapter->ChecksumOffload.RxTcpChecksumEnabled = TRUE;
    Adapter->ChecksumOffload.RxUdpChecksumEnabled = TRUE;

    DbgPrint("E1000: Offload capabilities initialized (IPv4 TX/RX checksum enabled)\n");

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000SetOffloadParameters - Configure offload settings
 *
 * Called when the OS wants to change offload parameters.
 * ============================================================================ */

NDIS_STATUS
E1000SetOffloadParameters(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_OFFLOAD_PARAMETERS OffloadParams
    )
{
    ULONG RxcsumValue = 0;

    /* Validate header */
    if (OffloadParams->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        OffloadParams->Header.Size < sizeof(NDIS_OFFLOAD_PARAMETERS))
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Process IPv4 checksum settings */
    switch (OffloadParams->IPv4Checksum)
    {
        case NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED:
            Adapter->ChecksumOffload.TxIpChecksumEnabled = FALSE;
            Adapter->ChecksumOffload.RxIpChecksumEnabled = FALSE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED:
            Adapter->ChecksumOffload.TxIpChecksumEnabled = TRUE;
            Adapter->ChecksumOffload.RxIpChecksumEnabled = FALSE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED:
            Adapter->ChecksumOffload.TxIpChecksumEnabled = FALSE;
            Adapter->ChecksumOffload.RxIpChecksumEnabled = TRUE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED:
            Adapter->ChecksumOffload.TxIpChecksumEnabled = TRUE;
            Adapter->ChecksumOffload.RxIpChecksumEnabled = TRUE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_NO_CHANGE:
        default:
            /* Keep current settings */
            break;
    }

    /* Process TCP checksum settings */
    switch (OffloadParams->TCPIPv4Checksum)
    {
        case NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED:
            Adapter->ChecksumOffload.TxTcpChecksumEnabled = FALSE;
            Adapter->ChecksumOffload.RxTcpChecksumEnabled = FALSE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED:
            Adapter->ChecksumOffload.TxTcpChecksumEnabled = TRUE;
            Adapter->ChecksumOffload.RxTcpChecksumEnabled = FALSE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED:
            Adapter->ChecksumOffload.TxTcpChecksumEnabled = FALSE;
            Adapter->ChecksumOffload.RxTcpChecksumEnabled = TRUE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED:
            Adapter->ChecksumOffload.TxTcpChecksumEnabled = TRUE;
            Adapter->ChecksumOffload.RxTcpChecksumEnabled = TRUE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_NO_CHANGE:
        default:
            break;
    }

    /* Process UDP checksum settings */
    switch (OffloadParams->UDPIPv4Checksum)
    {
        case NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED:
            Adapter->ChecksumOffload.TxUdpChecksumEnabled = FALSE;
            Adapter->ChecksumOffload.RxUdpChecksumEnabled = FALSE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED:
            Adapter->ChecksumOffload.TxUdpChecksumEnabled = TRUE;
            Adapter->ChecksumOffload.RxUdpChecksumEnabled = FALSE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED:
            Adapter->ChecksumOffload.TxUdpChecksumEnabled = FALSE;
            Adapter->ChecksumOffload.RxUdpChecksumEnabled = TRUE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED:
            Adapter->ChecksumOffload.TxUdpChecksumEnabled = TRUE;
            Adapter->ChecksumOffload.RxUdpChecksumEnabled = TRUE;
            break;

        case NDIS_OFFLOAD_PARAMETERS_NO_CHANGE:
        default:
            break;
    }

    /* Configure hardware RX checksum offload */
    if (Adapter->IoBase != NULL)
    {
        /* RXCSUM register configuration */
        RxcsumValue = E1000_READ_REG(Adapter, E1000_REG_RXCSUM);

        if (Adapter->ChecksumOffload.RxIpChecksumEnabled ||
            Adapter->ChecksumOffload.RxTcpChecksumEnabled ||
            Adapter->ChecksumOffload.RxUdpChecksumEnabled)
        {
            /* Enable IP checksum offload */
            RxcsumValue |= E1000_RXCSUM_IPOFL;

            /* Enable TCP/UDP checksum offload */
            RxcsumValue |= E1000_RXCSUM_TUOFL;

            /* For 82574L, enable IPv6 checksum offload */
            if (Adapter->IsPCIe)
            {
                RxcsumValue |= E1000_RXCSUM_IPV6OFL;
            }
        }
        else
        {
            /* Disable all RX checksum offload */
            RxcsumValue &= ~(E1000_RXCSUM_IPOFL | E1000_RXCSUM_TUOFL | E1000_RXCSUM_IPV6OFL);
        }

        E1000_WRITE_REG(Adapter, E1000_REG_RXCSUM, RxcsumValue);
    }

    DbgPrint("E1000: Offload parameters updated - IP TX=%d RX=%d, TCP TX=%d RX=%d, UDP TX=%d RX=%d\n",
             Adapter->ChecksumOffload.TxIpChecksumEnabled,
             Adapter->ChecksumOffload.RxIpChecksumEnabled,
             Adapter->ChecksumOffload.TxTcpChecksumEnabled,
             Adapter->ChecksumOffload.RxTcpChecksumEnabled,
             Adapter->ChecksumOffload.TxUdpChecksumEnabled,
             Adapter->ChecksumOffload.RxUdpChecksumEnabled);

    return NDIS_STATUS_SUCCESS;
}
