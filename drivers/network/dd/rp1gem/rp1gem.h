/*
 * PROJECT:     ReactOS RP1 Ethernet Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * FILE:        drivers/network/dd/rp1gem/rp1gem.h
 * PURPOSE:     Raspberry Pi 5 RP1 Cadence GEM NDIS 6.20 miniport header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ifdef.h>
#include <ipifcons.h>

#define NDEBUG
#include <reactos/debug.h>

#ifndef ETH_LENGTH_OF_ADDRESS
#define ETH_LENGTH_OF_ADDRESS 6
#endif

#ifndef NDIS_LINK_SPEED_UNKNOWN
#define NDIS_LINK_SPEED_UNKNOWN 0
#endif

#ifndef NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2
#define NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2 2
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE
#define NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE 0x00000001
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK
#define NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK 0x00000004
#endif

#ifndef NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER
#define NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER 0x00000040
#endif

#define RP1GEM_TAG 'G1PR'
#define RP1GEM_DRIVER_VERSION 0x0100
#define RP1GEM_PHY_ADDRESS 1
#define RP1GEM_MAX_MULTICAST 32
#define RP1GEM_MTU 1500
#define RP1GEM_FRAME_SIZE 1514
#define RP1GEM_BUFFER_SIZE 1536
#define RP1GEM_RX_RING_SIZE 64
#define RP1GEM_TX_RING_SIZE 64
#define RP1GEM_RX_BUDGET 32
#define RP1GEM_LINK_SPEED_10M 10000000ULL
#define RP1GEM_LINK_SPEED_100M 100000000ULL
#define RP1GEM_LINK_SPEED_1G 1000000000ULL
#define RP1GEM_MAC_OPTIONS (NDIS_MAC_OPTION_TRANSFERS_NOT_PEND | \
                            NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | \
                            NDIS_MAC_OPTION_NO_LOOPBACK)
#define RP1GEM_SUPPORTED_FILTERS (NDIS_PACKET_TYPE_DIRECTED | \
                                  NDIS_PACKET_TYPE_MULTICAST | \
                                  NDIS_PACKET_TYPE_ALL_MULTICAST | \
                                  NDIS_PACKET_TYPE_BROADCAST | \
                                  NDIS_PACKET_TYPE_PROMISCUOUS)

/* RP1 BAR1 sideband registers used by the Raspberry Pi 5 PHY reset GPIO. */
#define RP1GEM_ETH_OFFSET 0x00100000
#define RP1GEM_PCIE_APBS_OFFSET 0x00108000
#define RP1GEM_PCIE_APBS_LENGTH 0x00001000
#define RP1GEM_PCIE_APBS_REG_SET 0x00000800
#define RP1GEM_MSIX_CFG(_Irq) (0x00000008 + (sizeof(ULONG) * (_Irq)))
#define RP1GEM_MSIX_CFG_ENABLE (1u << 0)
#define RP1GEM_MSIX_CFG_IACK (1u << 2)
#define RP1GEM_MSIX_CFG_IACK_EN (1u << 3)
#define RP1GEM_INT_ETH 6u
#define RP1GEM_GPIO_OFFSET 0x000d0000
#define RP1GEM_RIO_OFFSET 0x000e0000
#define RP1GEM_PADS_OFFSET 0x000f0000
#define RP1GEM_GPIO_WINDOW_LENGTH 0x0000c000

#define RP1GEM_PHY_RESET_GPIO 32
#define RP1GEM_PHY_RESET_BANK_MIN 28
#define RP1GEM_PHY_RESET_PIN (RP1GEM_PHY_RESET_GPIO - RP1GEM_PHY_RESET_BANK_MIN)
#define RP1GEM_PHY_RESET_BIT (1u << RP1GEM_PHY_RESET_PIN)
#define RP1GEM_PHY_RESET_BANK_OFFSET 0x4000
#define RP1GEM_PHY_RESET_GPIO_CTRL (RP1GEM_PHY_RESET_BANK_OFFSET + \
                                    (RP1GEM_PHY_RESET_PIN * sizeof(ULONG) * 2) + \
                                    RP1_GPIO_CTRL)
#define RP1GEM_PHY_RESET_RIO_BANK RP1GEM_PHY_RESET_BANK_OFFSET
#define RP1GEM_PHY_RESET_PAD_CTRL (0x4004 + (RP1GEM_PHY_RESET_PIN * sizeof(ULONG)))

#define RP1_SET_OFFSET 0x2000
#define RP1_CLR_OFFSET 0x3000

#define RP1_GPIO_CTRL 0x0004
#define RP1_GPIO_CTRL_FUNCSEL_MASK 0x0000001f
#define RP1_GPIO_CTRL_OUTOVER_MASK 0x00003000
#define RP1_GPIO_CTRL_OEOVER_MASK 0x0000c000
#define RP1_GPIO_CTRL_INOVER_MASK 0x00030000
#define RP1_FSEL_GPIO 0x05

#define RP1_RIO_OUT 0x00
#define RP1_RIO_OE 0x04

#define RP1_PAD_PULL_MASK 0x0000000c
#define RP1_PAD_IN_ENABLE_MASK 0x00000040
#define RP1_PAD_OUT_DISABLE_MASK 0x00000080

/* Cadence MACB/GEM register offsets used by the RP1 GEM probe path. */
#define MACB_NCR 0x0000
#define MACB_NCFGR 0x0004
#define MACB_NSR 0x0008
#define GEM_DMACFG 0x0010
#define GEM_AMP 0x0054
#define GEM_INTMOD 0x005c
#define MACB_TSR 0x0014
#define MACB_RBQP 0x0018
#define MACB_TBQP 0x001c
#define MACB_RSR 0x0020
#define MACB_ISR 0x0024
#define MACB_IER 0x0028
#define MACB_IDR 0x002c
#define MACB_IMR 0x0030
#define MACB_MAN 0x0034
#define MACB_MID 0x00fc
#define MACB_TBQPH 0x04c8
#define MACB_RBQPH 0x04d4
#define GEM_SA1B 0x0088
#define GEM_SA1T 0x008c
#define GEM_DCFG1 0x0280
#define GEM_DCFG2 0x0284
#define GEM_DCFG6 0x0294
#define GEM_OCTTXL 0x0100
#define GEM_TXCNT 0x0108
#define GEM_TXBCCNT 0x010c
#define GEM_OCTRXL 0x0150
#define GEM_RXCNT 0x0158
#define GEM_RXBROADCNT 0x015c
#define GEM_RXFCSCNT 0x0190
#define GEM_RXRESERRCNT 0x01a0
#define GEM_RXORCNT 0x01a4

#define MACB_NCR_RE (1u << 2)
#define MACB_NCR_TE (1u << 3)
#define MACB_NCR_MPE (1u << 4)
#define MACB_NCR_CLRSTAT (1u << 5)
#define MACB_NCR_TSTART (1u << 9)
#define MACB_NSR_IDLE (1u << 2)

#define MACB_NCFGR_SPD (1u << 0)
#define MACB_NCFGR_FD (1u << 1)
#define MACB_NCFGR_CAF (1u << 4)
#define MACB_NCFGR_BIG (1u << 8)
#define GEM_NCFGR_GBE (1u << 10)
#define MACB_NCFGR_DRFCS (1u << 17)
#define GEM_NCFGR_CLK_MASK (7u << 18)
#define GEM_NCFGR_CLK_DIV96 (5u << 18)
#define GEM_NCFGR_DBW128 (2u << 21)

#define GEM_DMACFG_FBLDO_INCR16 16u
#define GEM_DMACFG_FBLDO_MASK 0x0000001f
#define GEM_DMACFG_ENDIA_DESC (1u << 6)
#define GEM_DMACFG_ENDIA_PKT (1u << 7)
#define GEM_DMACFG_RXBMS_FULL (3u << 8)
#define GEM_DMACFG_TXPBMS (1u << 10)
#define GEM_DMACFG_TXCOEN (1u << 11)
#define GEM_DMACFG_RXBS(_Size) (((_Size) / 64u) << 16)
#define GEM_DMACFG_RXBS_MASK (0xffu << 16)
#define GEM_DMACFG_ADDR64 (1u << 30)
#define GEM_DCFG1_IRQCOR (1u << 23)
#define GEM_DCFG6_DAW64 (1u << 23)

#define GEM_AMP_AR2R_MAX_PIPE(_Value) ((ULONG)(_Value) << 0)
#define GEM_AMP_AW2W_MAX_PIPE(_Value) ((ULONG)(_Value) << 8)
#define GEM_AMP_AW2B_FILL (1u << 16)
#define GEM_AMP_AR2R_MAX_PIPE_MASK (0xffu << 0)
#define GEM_AMP_AW2W_MAX_PIPE_MASK (0xffu << 8)
#define GEM_AMP_AW2B_FILL_MASK (1u << 16)
#define RP1GEM_AXI_MAX_PIPE 8u

#define GEM_INTMOD_RX(_Value) ((ULONG)(_Value) << 0)
#define GEM_INTMOD_TX(_Value) ((ULONG)(_Value) << 16)
#define RP1GEM_INTMOD_50US 62u

#define MACB_TSR_UBR (1u << 0)
#define MACB_TSR_RLE (1u << 2)
#define MACB_TSR_BEX (1u << 4)
#define MACB_TSR_COMP (1u << 5)
#define MACB_TSR_UND (1u << 6)
#define MACB_TSR_ALL (MACB_TSR_UBR | MACB_TSR_RLE | MACB_TSR_BEX | \
                      MACB_TSR_COMP | MACB_TSR_UND)

#define MACB_RSR_BNA (1u << 0)
#define MACB_RSR_REC (1u << 1)
#define MACB_RSR_OVR (1u << 2)
#define MACB_RSR_ALL (MACB_RSR_BNA | MACB_RSR_REC | MACB_RSR_OVR)

#define MACB_INT_RCOMP (1u << 1)
#define MACB_INT_RXUBR (1u << 2)
#define MACB_INT_TXUBR (1u << 3)
#define MACB_INT_TUND (1u << 4)
#define MACB_INT_RLE (1u << 5)
#define MACB_INT_TXERR (1u << 6)
#define MACB_INT_TCOMP (1u << 7)
#define MACB_INT_ROVR (1u << 10)
#define MACB_INT_HRESP (1u << 11)
#define MACB_INT_ALL 0xffffffff
#define RP1GEM_INT_MASK (MACB_INT_RCOMP | MACB_INT_RXUBR | MACB_INT_TXUBR | \
                         MACB_INT_TUND | MACB_INT_RLE | MACB_INT_TXERR | \
                         MACB_INT_TCOMP | MACB_INT_ROVR | MACB_INT_HRESP)

#define MACB_RX_USED (1u << 0)
#define MACB_RX_WRAP (1u << 1)
#define MACB_RX_FRMLEN_MASK 0x0fff
#define MACB_RX_SOF (1u << 14)
#define MACB_RX_EOF (1u << 15)

#define MACB_TX_LAST (1u << 15)
#define MACB_TX_WRAP (1u << 30)
#define MACB_TX_USED (1u << 31)
#define MACB_TX_ERROR_MASK ((1u << 27) | (1u << 28) | (1u << 29))

#define MACB_MAN_VALUE(_Sof, _Rw, _Phy, _Reg, _Code, _Data) \
    ((((ULONG)(_Sof) & 0x3) << 30) | \
     (((ULONG)(_Rw) & 0x3) << 28) | \
     (((ULONG)(_Phy) & 0x1f) << 23) | \
     (((ULONG)(_Reg) & 0x1f) << 18) | \
     (((ULONG)(_Code) & 0x3) << 16) | \
     ((ULONG)(_Data) & 0xffff))

#define MACB_MAN_SOF_C22 1
#define MACB_MAN_RW_WRITE 1
#define MACB_MAN_RW_READ 2
#define MACB_MAN_CODE 2

#define MII_BMCR 0
#define MII_BMSR 1
#define MII_ADVERTISE 4
#define MII_LPA 5
#define MII_EXPANSION 6
#define MII_CTRL1000 9
#define MII_STAT1000 10
#define MII_MMD_CTRL 13
#define MII_MMD_DATA 14
#define MII_ESTATUS 15

#define MII_MMD_CTRL_ADDR 0x0000
#define MII_MMD_CTRL_NOINCR 0x4000

#define MDIO_MMD_AN 7
#define MDIO_AN_EEE_ADV 60
#define MDIO_EEE_100TX 0x0002
#define MDIO_EEE_1000T 0x0004

#define BMCR_ANRESTART 0x0200
#define BMCR_ISOLATE 0x0400
#define BMCR_PDOWN 0x0800
#define BMCR_ANENABLE 0x1000
#define BMSR_LSTATUS 0x0004
#define BMSR_ANEGCOMPLETE 0x0020
#define ADVERTISE_CSMA 0x0001
#define ADVERTISE_10HALF 0x0020
#define ADVERTISE_10FULL 0x0040
#define ADVERTISE_100HALF 0x0080
#define ADVERTISE_100FULL 0x0100
#define ADVERTISE_PAUSE_CAP 0x0400
#define ADVERTISE_PAUSE_ASYM 0x0800
#define ADVERTISE_ALL (ADVERTISE_10HALF | ADVERTISE_10FULL | \
                       ADVERTISE_100HALF | ADVERTISE_100FULL)
#define ADVERTISE_1000HALF 0x0100
#define ADVERTISE_1000FULL 0x0200
#define LPA_1000HALF 0x0400
#define LPA_1000FULL 0x0800
#define LPA_1000REMRXOK 0x1000
#define LPA_1000LOCALRXOK 0x2000
#define LPA_1000MSRES 0x4000
#define LPA_1000MSFAIL 0x8000

#define RP1GEM_PHY_ID_MODEL_MASK 0xfffffff0
#define RP1GEM_PHY_ID_BCM54210E 0x600d84a0

#define MII_BCM54XX_AUX_CTL 0x18
#define MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL 0x00
#define MII_BCM54XX_AUXCTL_ACTL_TX_6DB 0x0400
#define MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA 0x0800
#define MII_BCM54XX_AUXCTL_SHDWSEL_MISC 0x07
#define MII_BCM54XX_AUXCTL_SHDWSEL_MISC_RGMII_SKEW_EN 0x0100
#define MII_BCM54XX_AUXCTL_MISC_WREN 0x8000
#define MII_BCM54XX_AUXCTL_SHDWSEL_READ_SHIFT 12
#define MII_BCM54XX_AUXCTL_SHDWSEL_MASK 0x0007

#define MII_BCM54XX_SHD 0x1c
#define MII_BCM54XX_SHD_WRITE 0x8000
#define MII_BCM54XX_SHD_VAL(_Shadow) (((_Shadow) & 0x1f) << 10)
#define MII_BCM54XX_SHD_DATA(_Value) ((_Value) & 0x03ff)
#define BCM54XX_SHD_SCR3 0x05
#define BCM54XX_SHD_SCR3_DLLAPD_DIS 0x0002
#define BCM54XX_SHD_APD 0x0a
#define BCM54XX_SHD_APD_EN 0x0020
#define BCM54810_SHD_CLK_CTL 0x03
#define BCM54810_SHD_CLK_CTL_GTXCLK_EN (1u << 9)

#define BCM2712_MBOX_PHYS 0x107C013880ULL
#define BCM2712_MBOX_LENGTH 0x40
#define BCM2712_MBOX_MAIL0_RD 0x00
#define BCM2712_MBOX_MAIL0_STA 0x18
#define BCM2712_MBOX_MAIL1_WRT 0x20
#define BCM2712_MBOX_MAIL1_STA 0x38
#define BCM2712_MBOX_STATUS_FULL (1u << 31)
#define BCM2712_MBOX_STATUS_EMPTY (1u << 30)
#define BCM2712_MBOX_PROPERTY_CHANNEL 8
#define BCM2712_MBOX_PROPERTY_SUCCESS 0x80000000
#define BCM2712_MBOX_TAG_RESPONSE 0x80000000
#define BCM2712_MBOX_GET_BOARD_MAC 0x00010003
#define BCM2712_MBOX_GET_ETHERNET_MAC 0x00030082

typedef struct _RP1GEM_DMA_DESCRIPTOR
{
    ULONG Address;
    ULONG Control;
    ULONG AddressHigh;
    ULONG Reserved;
} RP1GEM_DMA_DESCRIPTOR, *PRP1GEM_DMA_DESCRIPTOR;

typedef struct _RP1GEM_RX_BUFFER
{
    PVOID VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
    PMDL Mdl;
    PNET_BUFFER_LIST NetBufferList;
    BOOLEAN Indicated;
} RP1GEM_RX_BUFFER, *PRP1GEM_RX_BUFFER;

typedef struct _RP1GEM_TX_BUFFER
{
    PVOID VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
    PNET_BUFFER_LIST NetBufferList;
    ULONG Length;
} RP1GEM_TX_BUFFER, *PRP1GEM_TX_BUFFER;

typedef struct _RP1GEM_ADAPTER
{
    NDIS_HANDLE MiniportHandle;
    PDEVICE_OBJECT PhysicalDeviceObject;

    PVOID RegisterBase;
    ULONG RegisterLength;
    PHYSICAL_ADDRESS RegisterPhysical;
    PVOID Rp1ApbsBase;
    ULONG Rp1ApbsLength;

    ULONG InterruptVector;
    ULONG InterruptLevel;
    KAFFINITY InterruptAffinity;
    NDIS_HANDLE InterruptHandle;
    volatile LONG InterruptPending;

    NDIS_HANDLE RxNblPool;
    NDIS_SPIN_LOCK RxLock;
    NDIS_SPIN_LOCK TxLock;

    PRP1GEM_DMA_DESCRIPTOR RxRing;
    NDIS_PHYSICAL_ADDRESS RxRingPhysical;
    ULONG RxRingLength;
    PVOID RxBufferArea;
    NDIS_PHYSICAL_ADDRESS RxBufferPhysical;
    ULONG RxBufferAreaLength;
    RP1GEM_RX_BUFFER RxBuffers[RP1GEM_RX_RING_SIZE];
    ULONG RxTail;

    PRP1GEM_DMA_DESCRIPTOR TxRing;
    NDIS_PHYSICAL_ADDRESS TxRingPhysical;
    ULONG TxRingLength;
    PVOID TxBufferArea;
    NDIS_PHYSICAL_ADDRESS TxBufferPhysical;
    ULONG TxBufferAreaLength;
    RP1GEM_TX_BUFFER TxBuffers[RP1GEM_TX_RING_SIZE];
    ULONG TxHead;
    ULONG TxTail;
    ULONG TxFree;

    UCHAR PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR MulticastList[RP1GEM_MAX_MULTICAST][ETH_LENGTH_OF_ADDRESS];
    ULONG MulticastCount;
    ULONG PacketFilter;
    ULONG Lookahead;
    ULONG64 LinkSpeed;
    NDIS_MEDIA_CONNECT_STATE MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE MediaDuplexState;
    NDIS_MINIPORT_TIMER LinkTimer;
    BOOLEAN LinkTimerInitialized;
    ULONG LinkPollCount;
    UCHAR PhyAddress;
    USHORT PhyId1;
    USHORT PhyId2;
    BOOLEAN Dma64Bit;
    BOOLEAN InterruptClearOnWrite;
    BOOLEAN DatapathReady;
    ULONG64 TxPackets;
    ULONG64 RxPackets;
    ULONG64 TxBytes;
    ULONG64 RxBytes;
    ULONG64 TxErrors;
    ULONG64 RxErrors;
    ULONG64 RxNoBuffer;
    ULONG InterruptDpcCount;
    ULONG InterruptIsrCount;
    ULONG InterruptRecognizedCount;
    ULONG InterruptLastRawIsr;
    ULONG InterruptLastPending;
    ULONG SpuriousInterruptCount;
} RP1GEM_ADAPTER, *PRP1GEM_ADAPTER;

typedef struct _RP1GEM_MAILBOX_PROPERTY
{
    ULONG Size;
    ULONG Code;
    ULONG Tag;
    ULONG ValueSize;
    ULONG RequestResponse;
    ULONG Value[2];
    ULONG EndTag;
} RP1GEM_MAILBOX_PROPERTY, *PRP1GEM_MAILBOX_PROPERTY;
