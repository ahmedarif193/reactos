/** @file
  Simple Network Protocol driver for the Raspberry Pi 5 RP1 Cadence GEM.

  The RP1 southbridge is a PCIe endpoint whose BAR window exposes the SoC
  peripherals; the Ethernet MAC sits at a fixed offset inside it. Register
  definitions follow the Cadence MACB/GEM programming model, matching the
  ReactOS rp1gem NDIS miniport this driver shares its bring-up sequence with.

  Copyright (c) 2026, ReactOS Project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RP1_GEM_DXE_H_
#define RP1_GEM_DXE_H_

#include <Uefi.h>
#include <IndustryStandard/Pci.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/PciIo.h>
#include <Protocol/DevicePath.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

/* Spelled out here so the driver needs nothing from NetworkPkg. */
#ifndef NET_ETHER_ADDR_LEN
#define NET_ETHER_ADDR_LEN  6
#endif
#ifndef NET_IFTYPE_ETHERNET
#define NET_IFTYPE_ETHERNET  0x01
#endif

#define RP1_PCI_VENDOR_ID       0x1DE4
#define RP1_GEM_BAR_OFFSET      0x00100000  /* ETH0 inside the RP1 window. */
#define RP1_GEM_REGISTER_LENGTH 0x4000
#define RP1_PERIPHERAL_MIN_SIZE 0x00200000  /* Reject small unrelated BARs. */

#define RP1_GEM_SIGNATURE       SIGNATURE_32 ('R', 'P', '1', 'G')

/*
 * The platform hands out very little bus-master memory, so keep the rings
 * small and allocate every packet buffer on its own.
 */
#define RP1_GEM_RX_RING_SIZE    128
#define RP1_GEM_TX_RING_SIZE    32
#define RP1_GEM_BUFFER_SIZE     1536
#define RP1_GEM_FRAME_SIZE      1514
#define RP1_GEM_MTU             1500
#define RP1_GEM_MAX_MCAST       16
#define RP1_GEM_RECYCLE_SIZE    (RP1_GEM_TX_RING_SIZE * 2)

/* Cadence MACB/GEM registers. */
#define MACB_NCR                0x0000
#define MACB_NCFGR              0x0004
#define MACB_NSR                0x0008
#define GEM_DMACFG              0x0010
#define MACB_TSR                0x0014
#define MACB_RBQP               0x0018
#define MACB_TBQP               0x001C
#define MACB_RSR                0x0020
#define MACB_ISR                0x0024
#define MACB_IER                0x0028
#define MACB_IDR                0x002C
#define MACB_IMR                0x0030
#define MACB_MAN                0x0034
#define GEM_HRB                 0x0080
#define GEM_HRT                 0x0084
#define GEM_SA1B                0x0088
#define GEM_SA1T                0x008C
#define GEM_AMP                 0x0054
#define GEM_INTMOD              0x005C
#define GEM_DCFG1               0x0280
#define GEM_DCFG6               0x0294
#define MACB_TBQPH              0x04C8
#define MACB_RBQPH              0x04D4

#define MACB_NCR_RE             BIT2
#define MACB_NCR_TE             BIT3
#define MACB_NCR_MPE            BIT4
#define MACB_NCR_CLRSTAT        BIT5
#define MACB_NCR_TSTART         BIT9
#define MACB_NSR_IDLE           BIT2

#define MACB_NCFGR_SPD          BIT0
#define MACB_NCFGR_FD           BIT1
#define MACB_NCFGR_CAF          BIT4
#define MACB_NCFGR_NBC          BIT5
#define MACB_NCFGR_MTI          BIT6
#define MACB_NCFGR_BIG          BIT8
#define GEM_NCFGR_GBE           BIT10
#define MACB_NCFGR_DRFCS        BIT17
#define GEM_NCFGR_CLK_DIV96     (5u << 18)
#define GEM_NCFGR_DBW128        (2u << 21)
#define GEM_NCFGR_RXCOEN        BIT24

#define GEM_DMACFG_FBLDO_INCR16 16u
#define GEM_DMACFG_FBLDO_MASK   0x0000001Fu
#define GEM_DMACFG_ENDIA_DESC   BIT6
#define GEM_DMACFG_ENDIA_PKT    BIT7
#define GEM_DMACFG_RXBMS_FULL   (3u << 8)
#define GEM_DMACFG_TXPBMS       BIT10
#define GEM_DMACFG_RXBS(Size)   (((Size) / 64u) << 16)
#define GEM_DMACFG_RXBS_MASK    (0xFFu << 16)
#define GEM_DMACFG_ADDR64       BIT30
#define GEM_DCFG6_DAW64         BIT23

#define GEM_AMP_AR2R_MAX_PIPE(V)   ((UINT32)(V) << 0)
#define GEM_AMP_AW2W_MAX_PIPE(V)   ((UINT32)(V) << 8)
#define GEM_AMP_AW2B_FILL          BIT16
#define GEM_AMP_AR2R_MAX_PIPE_MASK (0xFFu << 0)
#define GEM_AMP_AW2W_MAX_PIPE_MASK (0xFFu << 8)
#define RP1_GEM_AXI_MAX_PIPE       8u

#define MACB_TSR_UBR            BIT0
#define MACB_TSR_RLE            BIT2
#define MACB_TSR_BEX            BIT4
#define MACB_TSR_COMP           BIT5
#define MACB_TSR_UND            BIT6
#define MACB_TSR_ALL            (MACB_TSR_UBR | MACB_TSR_RLE | MACB_TSR_BEX | \
                                 MACB_TSR_COMP | MACB_TSR_UND)

#define MACB_RSR_BNA            BIT0
#define MACB_RSR_REC            BIT1
#define MACB_RSR_OVR            BIT2
#define MACB_RSR_ALL            (MACB_RSR_BNA | MACB_RSR_REC | MACB_RSR_OVR)

#define MACB_INT_ALL            0xFFFFFFFFu

/* Receive descriptor word 0 flags, word 1 status. */
#define MACB_RX_USED            BIT0
#define MACB_RX_WRAP            BIT1
#define MACB_RX_FRMLEN_MASK     0x00001FFFu
#define MACB_RX_SOF             BIT14
#define MACB_RX_EOF             BIT15

/* Transmit descriptor word 1. */
#define MACB_TX_LAST            BIT15
#define MACB_TX_WRAP            BIT30
#define MACB_TX_USED            BIT31
#define MACB_TX_ERROR_MASK      (BIT27 | BIT28 | BIT29)

#define MACB_MAN_VALUE(Sof, Rw, Phy, Reg, Code, Data) \
  ((((UINT32)(Sof)  & 0x3)  << 30) | \
   (((UINT32)(Rw)   & 0x3)  << 28) | \
   (((UINT32)(Phy)  & 0x1F) << 23) | \
   (((UINT32)(Reg)  & 0x1F) << 18) | \
   (((UINT32)(Code) & 0x3)  << 16) | \
   ((UINT32)(Data) & 0xFFFF))

#define MACB_MAN_SOF_C22        1
#define MACB_MAN_RW_WRITE       1
#define MACB_MAN_RW_READ        2
#define MACB_MAN_CODE           2

/* Clause 22 PHY registers. */
#define MII_BMCR                0
#define MII_BMSR                1
#define MII_PHYSID1             2
#define MII_PHYSID2             3
#define MII_ADVERTISE           4
#define MII_LPA                 5
#define MII_CTRL1000            9
#define MII_STAT1000            10
#define MII_MMD_CTRL            13
#define MII_MMD_DATA            14

#define MII_MMD_CTRL_ADDR       0x0000
#define MII_MMD_CTRL_NOINCR     0x4000
#define MDIO_MMD_AN             7
#define MDIO_AN_EEE_ADV         60
#define MDIO_EEE_100TX          0x0002
#define MDIO_EEE_1000T          0x0004

#define BMCR_ANRESTART          0x0200
#define BMCR_PDOWN              0x0800
#define BMCR_ANENABLE           0x1000
#define BMCR_RESET              0x8000
#define BMSR_LSTATUS            0x0004
#define BMSR_ANEGCOMPLETE       0x0020

#define ADVERTISE_CSMA          0x0001
#define ADVERTISE_10HALF        0x0020
#define ADVERTISE_10FULL        0x0040
#define ADVERTISE_100HALF       0x0080
#define ADVERTISE_100FULL       0x0100
#define ADVERTISE_PAUSE_CAP     0x0400
#define ADVERTISE_PAUSE_ASYM    0x0800
#define ADVERTISE_ALL           (ADVERTISE_10HALF | ADVERTISE_10FULL | \
                                 ADVERTISE_100HALF | ADVERTISE_100FULL)
#define ADVERTISE_1000FULL      0x0200
#define LPA_1000HALF            0x0400
#define LPA_1000FULL            0x0800

/* Broadcom BCM54210E shadow/auxiliary access. */
#define RP1_GEM_PHY_ADDRESS     1
#define RP1_GEM_PHY_ID_MASK     0xFFFFFFF0u
#define RP1_GEM_PHY_ID_BCM54210E 0x600D84A0u

#define MII_BCM54XX_AUX_CTL                      0x18
#define MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL        0x00
#define MII_BCM54XX_AUXCTL_ACTL_TX_6DB           0x0400
#define MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA        0x0800
#define MII_BCM54XX_AUXCTL_SHDWSEL_MISC          0x07
#define MII_BCM54XX_AUXCTL_SHDWSEL_MISC_RGMII_SKEW_EN 0x0100
#define MII_BCM54XX_AUXCTL_MISC_WREN             0x8000
#define MII_BCM54XX_AUXCTL_SHDWSEL_READ_SHIFT    12
#define MII_BCM54XX_AUXCTL_SHDWSEL_MASK          0x0007

#define MII_BCM54XX_SHD                 0x1C
#define MII_BCM54XX_SHD_WRITE           0x8000
#define MII_BCM54XX_SHD_VAL(Shadow)     (((Shadow) & 0x1F) << 10)
#define MII_BCM54XX_SHD_DATA(Value)     ((Value) & 0x03FF)
#define BCM54XX_SHD_SCR3                0x05
#define BCM54XX_SHD_SCR3_DLLAPD_DIS     0x0002
#define BCM54XX_SHD_APD                 0x0A
#define BCM54XX_SHD_APD_EN              0x0020
#define BCM54810_SHD_CLK_CTL            0x03
#define BCM54810_SHD_CLK_CTL_GTXCLK_EN  BIT9

typedef struct {
  UINT32    Address;
  UINT32    Control;
  UINT32    AddressHigh;
  UINT32    Reserved;
} RP1_GEM_DESCRIPTOR;

typedef struct {
  VOID     *Buffer;
  UINT64    DeviceAddress;
  VOID     *Mapping;
  /* TX only: the address SNP.GetStatus must hand back to the caller. */
  VOID     *CallerBuffer;
} RP1_GEM_SLOT;

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  MAC_ADDR_DEVICE_PATH        MacAddress;
  EFI_DEVICE_PATH_PROTOCOL    End;
} RP1_GEM_DEVICE_PATH;

typedef struct {
  UINT32                         Signature;
  EFI_HANDLE                     Handle;
  EFI_SIMPLE_NETWORK_PROTOCOL    Snp;
  EFI_SIMPLE_NETWORK_MODE        Mode;
  RP1_GEM_DEVICE_PATH           *DevicePath;

  EFI_PCI_IO_PROTOCOL           *PciIo;
  UINT8                          BarIndex;
  UINT64                         RegisterOffset;

  RP1_GEM_DESCRIPTOR            *RxRing;
  VOID                          *RxRingMapping;
  UINT64                         RxRingDevice;
  RP1_GEM_SLOT                   RxSlots[RP1_GEM_RX_RING_SIZE];
  UINT32                         RxTail;

  RP1_GEM_DESCRIPTOR            *TxRing;
  VOID                          *TxRingMapping;
  UINT64                         TxRingDevice;
  RP1_GEM_SLOT                   TxSlots[RP1_GEM_TX_RING_SIZE];
  UINT32                         TxHead;
  UINT32                         TxTail;

  VOID                          *Recycle[RP1_GEM_RECYCLE_SIZE];
  UINT32                         RecycleHead;
  UINT32                         RecycleTail;

  UINT8                          PhyAddress;
  BOOLEAN                        PhyValid;
  UINT32                         PhyId;
  UINT64                         LinkSpeed;
  BOOLEAN                        FullDuplex;
  BOOLEAN                        LinkUp;
  UINTN                          LinkPollTick;
} RP1_GEM_DEVICE;

#define RP1_GEM_FROM_SNP(a)  CR (a, RP1_GEM_DEVICE, Snp, RP1_GEM_SIGNATURE)

#endif /* RP1_GEM_DXE_H_ */
