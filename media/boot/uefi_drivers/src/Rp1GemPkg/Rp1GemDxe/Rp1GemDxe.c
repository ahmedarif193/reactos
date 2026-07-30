/** @file
  Simple Network Protocol driver for the Raspberry Pi 5 RP1 Cadence GEM.

  The firmware carries the whole EDK2 network stack but no driver for the
  board's own Ethernet MAC, so nothing can bind above SNP. This driver fills
  that gap for network boot: it finds the RP1 PCIe endpoint, brings up the GEM
  and its BCM54210E PHY, and drives both rings by polling, which is all the
  SNP contract asks for.

  Copyright (c) 2026, ReactOS Project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Rp1GemDxe.h"

STATIC EFI_HANDLE  mImageHandle;

/* ------------------------------------------------------------------ */
/* Register access                                                     */
/* ------------------------------------------------------------------ */

STATIC
UINT32
GemRead32 (
  IN RP1_GEM_DEVICE  *Device,
  IN UINT64           Offset
  )
{
  UINT32  Value;

  Value = 0;
  Device->PciIo->Mem.Read (
                       Device->PciIo,
                       EfiPciIoWidthUint32,
                       Device->BarIndex,
                       Device->RegisterOffset + Offset,
                       1,
                       &Value
                       );
  return Value;
}

STATIC
VOID
GemWrite32 (
  IN RP1_GEM_DEVICE  *Device,
  IN UINT64           Offset,
  IN UINT32           Value
  )
{
  Device->PciIo->Mem.Write (
                       Device->PciIo,
                       EfiPciIoWidthUint32,
                       Device->BarIndex,
                       Device->RegisterOffset + Offset,
                       1,
                       &Value
                       );
}

/* ------------------------------------------------------------------ */
/* MDIO and PHY                                                        */
/* ------------------------------------------------------------------ */

STATIC
BOOLEAN
GemWaitMdioIdle (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINTN  Retry;

  for (Retry = 0; Retry < 10000; Retry++) {
    if ((GemRead32 (Device, MACB_NSR) & MACB_NSR_IDLE) != 0) {
      return TRUE;
    }

    gBS->Stall (10);
  }

  return FALSE;
}

STATIC
EFI_STATUS
PhyRead (
  IN  RP1_GEM_DEVICE  *Device,
  IN  UINT8            Phy,
  IN  UINT8            Register,
  OUT UINT16          *Value
  )
{
  if (!GemWaitMdioIdle (Device)) {
    return EFI_TIMEOUT;
  }

  GemWrite32 (
    Device,
    MACB_MAN,
    MACB_MAN_VALUE (MACB_MAN_SOF_C22, MACB_MAN_RW_READ, Phy, Register, MACB_MAN_CODE, 0)
    );

  if (!GemWaitMdioIdle (Device)) {
    return EFI_TIMEOUT;
  }

  *Value = (UINT16)GemRead32 (Device, MACB_MAN);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
PhyWrite (
  IN RP1_GEM_DEVICE  *Device,
  IN UINT8            Phy,
  IN UINT8            Register,
  IN UINT16           Value
  )
{
  if (!GemWaitMdioIdle (Device)) {
    return EFI_TIMEOUT;
  }

  GemWrite32 (
    Device,
    MACB_MAN,
    MACB_MAN_VALUE (MACB_MAN_SOF_C22, MACB_MAN_RW_WRITE, Phy, Register, MACB_MAN_CODE, Value)
    );

  return GemWaitMdioIdle (Device) ? EFI_SUCCESS : EFI_TIMEOUT;
}

STATIC
EFI_STATUS
PhyShadowRead (
  IN  RP1_GEM_DEVICE  *Device,
  IN  UINT16           Shadow,
  OUT UINT16          *Value
  )
{
  EFI_STATUS  Status;

  Status = PhyWrite (Device, Device->PhyAddress, MII_BCM54XX_SHD, MII_BCM54XX_SHD_VAL (Shadow));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PhyRead (Device, Device->PhyAddress, MII_BCM54XX_SHD, Value);
  if (!EFI_ERROR (Status)) {
    *Value = MII_BCM54XX_SHD_DATA (*Value);
  }

  return Status;
}

STATIC
EFI_STATUS
PhyShadowWrite (
  IN RP1_GEM_DEVICE  *Device,
  IN UINT16           Shadow,
  IN UINT16           Value
  )
{
  return PhyWrite (
           Device,
           Device->PhyAddress,
           MII_BCM54XX_SHD,
           MII_BCM54XX_SHD_WRITE | MII_BCM54XX_SHD_VAL (Shadow) | MII_BCM54XX_SHD_DATA (Value)
           );
}

STATIC
EFI_STATUS
PhyAuxctlRead (
  IN  RP1_GEM_DEVICE  *Device,
  IN  UINT16           Register,
  OUT UINT16          *Value
  )
{
  EFI_STATUS  Status;

  Status = PhyWrite (
             Device,
             Device->PhyAddress,
             MII_BCM54XX_AUX_CTL,
             MII_BCM54XX_AUXCTL_SHDWSEL_MASK |
             (UINT16)(Register << MII_BCM54XX_AUXCTL_SHDWSEL_READ_SHIFT)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return PhyRead (Device, Device->PhyAddress, MII_BCM54XX_AUX_CTL, Value);
}

STATIC
EFI_STATUS
PhyAuxctlWrite (
  IN RP1_GEM_DEVICE  *Device,
  IN UINT16           Register,
  IN UINT16           Value
  )
{
  return PhyWrite (Device, Device->PhyAddress, MII_BCM54XX_AUX_CTL, Register | Value);
}

STATIC
EFI_STATUS
PhyMmdWrite (
  IN RP1_GEM_DEVICE  *Device,
  IN UINT8            Devad,
  IN UINT16           Register,
  IN UINT16           Value
  )
{
  EFI_STATUS  Status;

  Status = PhyWrite (Device, Device->PhyAddress, MII_MMD_CTRL, MII_MMD_CTRL_ADDR | Devad);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PhyWrite (Device, Device->PhyAddress, MII_MMD_DATA, Register);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PhyWrite (Device, Device->PhyAddress, MII_MMD_CTRL, MII_MMD_CTRL_NOINCR | Devad);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return PhyWrite (Device, Device->PhyAddress, MII_MMD_DATA, Value);
}

STATIC
EFI_STATUS
PhyMmdRead (
  IN  RP1_GEM_DEVICE  *Device,
  IN  UINT8            Devad,
  IN  UINT16           Register,
  OUT UINT16          *Value
  )
{
  EFI_STATUS  Status;

  Status = PhyWrite (Device, Device->PhyAddress, MII_MMD_CTRL, MII_MMD_CTRL_ADDR | Devad);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PhyWrite (Device, Device->PhyAddress, MII_MMD_DATA, Register);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PhyWrite (Device, Device->PhyAddress, MII_MMD_CTRL, MII_MMD_CTRL_NOINCR | Devad);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return PhyRead (Device, Device->PhyAddress, MII_MMD_DATA, Value);
}

STATIC
BOOLEAN
PhyIdIsValid (
  IN UINT32  PhyId
  )
{
  return (BOOLEAN)(PhyId != 0 && PhyId != 0xFFFFFFFFu);
}

STATIC
VOID
PhyProbe (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT16  Id1;
  UINT16  Id2;
  UINT8   Address;
  UINT8   Candidate;

  Device->PhyValid   = FALSE;
  Device->PhyAddress = RP1_GEM_PHY_ADDRESS;

  for (Candidate = 0; Candidate < 33; Candidate++) {
    /* Try the address the Pi 5 wires up before scanning the whole bus. */
    Address = (Candidate == 0) ? RP1_GEM_PHY_ADDRESS : (UINT8)(Candidate - 1);
    if ((Candidate != 0) && (Address == RP1_GEM_PHY_ADDRESS)) {
      continue;
    }

    Id1 = 0;
    Id2 = 0;
    if (EFI_ERROR (PhyRead (Device, Address, MII_PHYSID1, &Id1)) ||
        EFI_ERROR (PhyRead (Device, Address, MII_PHYSID2, &Id2)))
    {
      continue;
    }

    if (PhyIdIsValid (((UINT32)Id1 << 16) | Id2)) {
      Device->PhyAddress = Address;
      Device->PhyId      = ((UINT32)Id1 << 16) | Id2;
      Device->PhyValid   = TRUE;
      DEBUG ((DEBUG_INFO, "Rp1Gem: PHY%u id %08x\n", Address, Device->PhyId));
      return;
    }
  }

  DEBUG ((DEBUG_ERROR, "Rp1Gem: no PHY found on the MDIO bus\n"));
}

/**
  Apply the board-specific BCM54210E setup and restart autonegotiation.

  The Pi 5 wires the GEM to the PHY in rgmii-id mode, so the PHY has to supply
  both the RX skew and the TX clock delay; its broken EEE advertisement also
  has to go, or gigabit links renegotiate in a loop.
**/
STATIC
VOID
PhyConfigure (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT16  Value;

  if (!Device->PhyValid) {
    return;
  }

  if ((Device->PhyId & RP1_GEM_PHY_ID_MASK) == RP1_GEM_PHY_ID_BCM54210E) {
    if (!EFI_ERROR (PhyAuxctlRead (Device, MII_BCM54XX_AUXCTL_SHDWSEL_MISC, &Value))) {
      Value |= MII_BCM54XX_AUXCTL_MISC_WREN | MII_BCM54XX_AUXCTL_SHDWSEL_MISC_RGMII_SKEW_EN;
      PhyAuxctlWrite (Device, MII_BCM54XX_AUXCTL_SHDWSEL_MISC, Value);
    }

    if (!EFI_ERROR (PhyShadowRead (Device, BCM54810_SHD_CLK_CTL, &Value))) {
      PhyShadowWrite (Device, BCM54810_SHD_CLK_CTL, Value | BCM54810_SHD_CLK_CTL_GTXCLK_EN);
    }

    if (!EFI_ERROR (PhyShadowRead (Device, BCM54XX_SHD_SCR3, &Value))) {
      PhyShadowWrite (Device, BCM54XX_SHD_SCR3, Value & ~BCM54XX_SHD_SCR3_DLLAPD_DIS);
    }

    if (!EFI_ERROR (PhyShadowRead (Device, BCM54XX_SHD_APD, &Value))) {
      PhyShadowWrite (Device, BCM54XX_SHD_APD, Value | BCM54XX_SHD_APD_EN);
    }

    PhyAuxctlWrite (
      Device,
      MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL,
      MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA | MII_BCM54XX_AUXCTL_ACTL_TX_6DB
      );
    PhyAuxctlWrite (
      Device,
      MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL,
      MII_BCM54XX_AUXCTL_ACTL_TX_6DB
      );

    if (!EFI_ERROR (PhyMmdRead (Device, MDIO_MMD_AN, MDIO_AN_EEE_ADV, &Value))) {
      PhyMmdWrite (
        Device,
        MDIO_MMD_AN,
        MDIO_AN_EEE_ADV,
        Value & ~(MDIO_EEE_100TX | MDIO_EEE_1000T)
        );
    }
  }

  PhyWrite (
    Device,
    Device->PhyAddress,
    MII_ADVERTISE,
    ADVERTISE_CSMA | ADVERTISE_ALL | ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM
    );
  PhyWrite (Device, Device->PhyAddress, MII_CTRL1000, ADVERTISE_1000FULL);
  PhyWrite (Device, Device->PhyAddress, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
}

/* ------------------------------------------------------------------ */
/* MAC configuration                                                   */
/* ------------------------------------------------------------------ */

STATIC
UINT32
GemBuildNcfgr (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Ncfgr;

  Ncfgr = GEM_NCFGR_CLK_DIV96 | GEM_NCFGR_DBW128 | MACB_NCFGR_DRFCS |
          MACB_NCFGR_BIG | GEM_NCFGR_RXCOEN;

  if ((Device->Mode.ReceiveFilterSetting & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS) != 0) {
    Ncfgr |= MACB_NCFGR_CAF;
  }

  if ((Device->Mode.ReceiveFilterSetting & EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST) == 0) {
    Ncfgr |= MACB_NCFGR_NBC;
  }

  if ((Device->Mode.ReceiveFilterSetting &
       (EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST |
        EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST)) != 0)
  {
    Ncfgr |= MACB_NCFGR_MTI;
  }

  if (Device->LinkUp) {
    if (Device->FullDuplex) {
      Ncfgr |= MACB_NCFGR_FD;
    }

    if (Device->LinkSpeed == 1000) {
      Ncfgr |= GEM_NCFGR_GBE;
    } else if (Device->LinkSpeed == 100) {
      Ncfgr |= MACB_NCFGR_SPD;
    }
  }

  return Ncfgr;
}

STATIC
VOID
GemApplyLinkState (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Ncr;

  GemWrite32 (Device, MACB_NCFGR, GemBuildNcfgr (Device));

  Ncr = GemRead32 (Device, MACB_NCR);
  if (Device->LinkUp && (Device->Mode.State == EfiSimpleNetworkInitialized)) {
    Ncr |= MACB_NCR_RE | MACB_NCR_TE;
  } else {
    Ncr &= ~(MACB_NCR_RE | MACB_NCR_TE | MACB_NCR_TSTART);
  }

  GemWrite32 (Device, MACB_NCR, Ncr);
}

STATIC
VOID
GemWriteMacAddress (
  IN RP1_GEM_DEVICE  *Device
  )
{
  CONST UINT8  *Mac;

  Mac = Device->Mode.CurrentAddress.Addr;
  GemWrite32 (
    Device,
    GEM_SA1B,
    (UINT32)Mac[0] | ((UINT32)Mac[1] << 8) | ((UINT32)Mac[2] << 16) | ((UINT32)Mac[3] << 24)
    );
  GemWrite32 (Device, GEM_SA1T, (UINT32)Mac[4] | ((UINT32)Mac[5] << 8));
}

STATIC
VOID
GemReadMacAddress (
  IN RP1_GEM_DEVICE  *Device
  )
{
  STATIC CONST UINT8  FallbackMac[NET_ETHER_ADDR_LEN] = { 0x02, 0x00, 0x00, 0x27, 0x12, 0x01 };
  UINT32              Low;
  UINT32              High;
  UINT8               Mac[NET_ETHER_ADDR_LEN];

  Low  = GemRead32 (Device, GEM_SA1B);
  High = GemRead32 (Device, GEM_SA1T);

  Mac[0] = (UINT8)(Low & 0xFF);
  Mac[1] = (UINT8)((Low >> 8) & 0xFF);
  Mac[2] = (UINT8)((Low >> 16) & 0xFF);
  Mac[3] = (UINT8)((Low >> 24) & 0xFF);
  Mac[4] = (UINT8)(High & 0xFF);
  Mac[5] = (UINT8)((High >> 8) & 0xFF);

  /* A multicast bit or an all-zero address means firmware left it unset. */
  if (((Mac[0] & 0x01) != 0) ||
      ((Mac[0] | Mac[1] | Mac[2] | Mac[3] | Mac[4] | Mac[5]) == 0))
  {
    DEBUG ((DEBUG_WARN, "Rp1Gem: MAC registers unset, using a local address\n"));
    CopyMem (Mac, FallbackMac, sizeof (Mac));
  }

  CopyMem (Device->Mode.PermanentAddress.Addr, Mac, sizeof (Mac));
  CopyMem (Device->Mode.CurrentAddress.Addr, Mac, sizeof (Mac));

  DEBUG ((
    DEBUG_INFO,
    "Rp1Gem: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
    Mac[0],
    Mac[1],
    Mac[2],
    Mac[3],
    Mac[4],
    Mac[5]
    ));
}

/**
  Read the PHY link state and push any change into the MAC.

  @retval TRUE   The link state changed.
**/
STATIC
BOOLEAN
GemRefreshLink (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT16   Bmcr;
  UINT16   Bmsr;
  UINT16   Lpa;
  UINT16   Stat1000;
  BOOLEAN  WasUp;
  UINT64   OldSpeed;
  BOOLEAN  OldDuplex;

  WasUp     = Device->LinkUp;
  OldSpeed  = Device->LinkSpeed;
  OldDuplex = Device->FullDuplex;

  Bmcr = 0;
  Bmsr = 0;
  if (!Device->PhyValid ||
      EFI_ERROR (PhyRead (Device, Device->PhyAddress, MII_BMCR, &Bmcr)) ||
      ((Bmcr & BMCR_ANRESTART) != 0) ||
      EFI_ERROR (PhyRead (Device, Device->PhyAddress, MII_BMSR, &Bmsr)) ||
      EFI_ERROR (PhyRead (Device, Device->PhyAddress, MII_BMSR, &Bmsr)) ||
      ((Bmsr & BMSR_LSTATUS) == 0))
  {
    Device->LinkUp     = FALSE;
    Device->LinkSpeed  = 0;
    Device->FullDuplex = FALSE;
  } else {
    Lpa      = 0;
    Stat1000 = 0;
    PhyRead (Device, Device->PhyAddress, MII_LPA, &Lpa);
    PhyRead (Device, Device->PhyAddress, MII_STAT1000, &Stat1000);

    Device->LinkUp     = TRUE;
    Device->LinkSpeed  = 10;
    Device->FullDuplex = FALSE;

    if ((Stat1000 & LPA_1000FULL) != 0) {
      Device->LinkSpeed  = 1000;
      Device->FullDuplex = TRUE;
    } else if ((Stat1000 & LPA_1000HALF) != 0) {
      Device->LinkSpeed = 1000;
    } else if ((Lpa & ADVERTISE_100FULL) != 0) {
      Device->LinkSpeed  = 100;
      Device->FullDuplex = TRUE;
    } else if ((Lpa & ADVERTISE_100HALF) != 0) {
      Device->LinkSpeed = 100;
    } else if ((Lpa & ADVERTISE_10FULL) != 0) {
      Device->FullDuplex = TRUE;
    }
  }

  Device->Mode.MediaPresent = Device->LinkUp;

  if ((WasUp != Device->LinkUp) ||
      (OldSpeed != Device->LinkSpeed) ||
      (OldDuplex != Device->FullDuplex))
  {
    DEBUG ((
      DEBUG_INFO,
      "Rp1Gem: link %a %u Mbps %a duplex\n",
      Device->LinkUp ? "up" : "down",
      (UINT32)Device->LinkSpeed,
      Device->FullDuplex ? "full" : "half"
      ));
    GemApplyLinkState (Device);
    return TRUE;
  }

  return FALSE;
}

/* ------------------------------------------------------------------ */
/* DMA rings                                                           */
/* ------------------------------------------------------------------ */

STATIC
EFI_STATUS
GemAllocateCommonBuffer (
  IN  RP1_GEM_DEVICE  *Device,
  IN  UINTN            Size,
  OUT VOID           **HostAddress,
  OUT UINT64          *DeviceAddress,
  OUT VOID           **Mapping
  )
{
  EFI_STATUS            Status;
  UINTN                 Bytes;
  EFI_PHYSICAL_ADDRESS  Address;

  /*
   * Write-combining is only a hint; platforms that cannot honour it answer
   * with an error rather than downgrading, so fall back to plain memory.
   */
  Status = Device->PciIo->AllocateBuffer (
                            Device->PciIo,
                            AllocateAnyPages,
                            EfiBootServicesData,
                            EFI_SIZE_TO_PAGES (Size),
                            HostAddress,
                            EFI_PCI_IO_ATTRIBUTE_MEMORY_WRITE_COMBINE
                            );
  if (EFI_ERROR (Status)) {
    Status = Device->PciIo->AllocateBuffer (
                              Device->PciIo,
                              AllocateAnyPages,
                              EfiBootServicesData,
                              EFI_SIZE_TO_PAGES (Size),
                              HostAddress,
                              0
                              );
  }

  if (EFI_ERROR (Status)) {
    /* MNP retries forever; one report is enough to diagnose the failure. */
    STATIC BOOLEAN  Reported = FALSE;

    if (!Reported) {
      Reported = TRUE;
      Print (L"Rp1Gem: AllocateBuffer(%u bytes) - %r\n", (UINT32)Size, Status);
    }

    return Status;
  }

  ZeroMem (*HostAddress, Size);

  Bytes  = Size;
  Status = Device->PciIo->Map (
                            Device->PciIo,
                            EfiPciIoOperationBusMasterCommonBuffer,
                            *HostAddress,
                            &Bytes,
                            &Address,
                            Mapping
                            );
  if (EFI_ERROR (Status) || (Bytes != Size)) {
    Print (
      L"Rp1Gem: Map(%u bytes) - %r, mapped %u\n",
      (UINT32)Size,
      Status,
      (UINT32)Bytes
      );
    Device->PciIo->FreeBuffer (Device->PciIo, EFI_SIZE_TO_PAGES (Size), *HostAddress);
    *HostAddress = NULL;
    return EFI_ERROR (Status) ? Status : EFI_OUT_OF_RESOURCES;
  }

  *DeviceAddress = (UINT64)Address;
  return EFI_SUCCESS;
}

STATIC
VOID
GemFreeCommonBuffer (
  IN RP1_GEM_DEVICE  *Device,
  IN UINTN            Size,
  IN VOID            *HostAddress,
  IN VOID            *Mapping
  )
{
  if (Mapping != NULL) {
    Device->PciIo->Unmap (Device->PciIo, Mapping);
  }

  if (HostAddress != NULL) {
    Device->PciIo->FreeBuffer (Device->PciIo, EFI_SIZE_TO_PAGES (Size), HostAddress);
  }
}

STATIC
VOID
GemRearmRxDescriptor (
  IN RP1_GEM_DEVICE  *Device,
  IN UINT32           Index
  )
{
  UINT32  Flags;

  Flags = (Index == (RP1_GEM_RX_RING_SIZE - 1)) ? MACB_RX_WRAP : 0;

  Device->RxRing[Index].Control     = 0;
  Device->RxRing[Index].AddressHigh = (UINT32)(Device->RxSlots[Index].DeviceAddress >> 32);
  MemoryFence ();
  Device->RxRing[Index].Address = (UINT32)Device->RxSlots[Index].DeviceAddress | Flags;
}

STATIC
VOID
GemFreeRings (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Index;

  for (Index = 0; Index < RP1_GEM_RX_RING_SIZE; Index++) {
    GemFreeCommonBuffer (
      Device,
      RP1_GEM_BUFFER_SIZE,
      Device->RxSlots[Index].Buffer,
      Device->RxSlots[Index].Mapping
      );
    Device->RxSlots[Index].Buffer  = NULL;
    Device->RxSlots[Index].Mapping = NULL;
  }

  for (Index = 0; Index < RP1_GEM_TX_RING_SIZE; Index++) {
    GemFreeCommonBuffer (
      Device,
      RP1_GEM_BUFFER_SIZE,
      Device->TxSlots[Index].Buffer,
      Device->TxSlots[Index].Mapping
      );
    Device->TxSlots[Index].Buffer  = NULL;
    Device->TxSlots[Index].Mapping = NULL;
  }

  GemFreeCommonBuffer (
    Device,
    RP1_GEM_RX_RING_SIZE * sizeof (RP1_GEM_DESCRIPTOR),
    Device->RxRing,
    Device->RxRingMapping
    );
  Device->RxRing        = NULL;
  Device->RxRingMapping = NULL;

  GemFreeCommonBuffer (
    Device,
    RP1_GEM_TX_RING_SIZE * sizeof (RP1_GEM_DESCRIPTOR),
    Device->TxRing,
    Device->TxRingMapping
    );
  Device->TxRing        = NULL;
  Device->TxRingMapping = NULL;
}

STATIC
VOID
GemResetRings (
  IN RP1_GEM_DEVICE  *Device
  );

STATIC
EFI_STATUS
GemAllocateRings (
  IN RP1_GEM_DEVICE  *Device
  )
{
  EFI_STATUS  Status;
  UINT32      Index;

  Status = GemAllocateCommonBuffer (
             Device,
             RP1_GEM_RX_RING_SIZE * sizeof (RP1_GEM_DESCRIPTOR),
             (VOID **)&Device->RxRing,
             &Device->RxRingDevice,
             &Device->RxRingMapping
             );
  if (EFI_ERROR (Status)) {
    goto Failed;
  }

  Status = GemAllocateCommonBuffer (
             Device,
             RP1_GEM_TX_RING_SIZE * sizeof (RP1_GEM_DESCRIPTOR),
             (VOID **)&Device->TxRing,
             &Device->TxRingDevice,
             &Device->TxRingMapping
             );
  if (EFI_ERROR (Status)) {
    goto Failed;
  }

  for (Index = 0; Index < RP1_GEM_RX_RING_SIZE; Index++) {
    Status = GemAllocateCommonBuffer (
               Device,
               RP1_GEM_BUFFER_SIZE,
               &Device->RxSlots[Index].Buffer,
               &Device->RxSlots[Index].DeviceAddress,
               &Device->RxSlots[Index].Mapping
               );
    if (EFI_ERROR (Status)) {
      goto Failed;
    }
  }

  for (Index = 0; Index < RP1_GEM_TX_RING_SIZE; Index++) {
    Status = GemAllocateCommonBuffer (
               Device,
               RP1_GEM_BUFFER_SIZE,
               &Device->TxSlots[Index].Buffer,
               &Device->TxSlots[Index].DeviceAddress,
               &Device->TxSlots[Index].Mapping
               );
    if (EFI_ERROR (Status)) {
      goto Failed;
    }
  }

  GemResetRings (Device);
  return EFI_SUCCESS;

Failed:
  GemFreeRings (Device);
  return Status;
}

/*
 * Put both rings back into their post-reset state. Kept separate from the
 * allocation because SNP.Initialize runs at the caller's raised TPL, where the
 * platform refuses bus-master allocations.
 */
STATIC
VOID
GemResetRings (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Index;
  UINT32  Control;

  for (Index = 0; Index < RP1_GEM_RX_RING_SIZE; Index++) {
    GemRearmRxDescriptor (Device, Index);
  }

  for (Index = 0; Index < RP1_GEM_TX_RING_SIZE; Index++) {
    /* USED means "owned by software": the MAC must not transmit these yet. */
    Control = MACB_TX_USED;
    if (Index == (RP1_GEM_TX_RING_SIZE - 1)) {
      Control |= MACB_TX_WRAP;
    }

    Device->TxRing[Index].AddressHigh = (UINT32)(Device->TxSlots[Index].DeviceAddress >> 32);
    Device->TxRing[Index].Address     = (UINT32)Device->TxSlots[Index].DeviceAddress;
    Device->TxRing[Index].Control     = Control;
  }

  Device->RxTail      = 0;
  Device->TxHead      = 0;
  Device->TxTail      = 0;
  Device->RecycleHead = 0;
  Device->RecycleTail = 0;
}

STATIC
VOID
GemStopHardware (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Ncr;

  GemWrite32 (Device, MACB_IDR, MACB_INT_ALL);
  Ncr  = GemRead32 (Device, MACB_NCR);
  Ncr &= ~(MACB_NCR_RE | MACB_NCR_TE | MACB_NCR_TSTART);
  GemWrite32 (Device, MACB_NCR, Ncr);
  GemWrite32 (Device, MACB_TSR, MACB_TSR_ALL);
  GemWrite32 (Device, MACB_RSR, MACB_RSR_ALL);
}

STATIC
EFI_STATUS
GemStartHardware (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Value;

  if ((GemRead32 (Device, GEM_DCFG6) & GEM_DCFG6_DAW64) == 0) {
    DEBUG ((DEBUG_ERROR, "Rp1Gem: controller lacks 64-bit descriptor support\n"));
    return EFI_UNSUPPORTED;
  }

  GemStopHardware (Device);

  Value  = GemRead32 (Device, GEM_DMACFG);
  Value &= ~(GEM_DMACFG_FBLDO_MASK | GEM_DMACFG_RXBS_MASK |
             GEM_DMACFG_ENDIA_DESC | GEM_DMACFG_ENDIA_PKT);
  Value |= GEM_DMACFG_FBLDO_INCR16 | GEM_DMACFG_RXBMS_FULL | GEM_DMACFG_TXPBMS |
           GEM_DMACFG_RXBS (RP1_GEM_BUFFER_SIZE) | GEM_DMACFG_ADDR64;
  GemWrite32 (Device, GEM_DMACFG, Value);

  Value  = GemRead32 (Device, GEM_AMP);
  Value &= ~(GEM_AMP_AR2R_MAX_PIPE_MASK | GEM_AMP_AW2W_MAX_PIPE_MASK | GEM_AMP_AW2B_FILL);
  Value |= GEM_AMP_AR2R_MAX_PIPE (RP1_GEM_AXI_MAX_PIPE) |
           GEM_AMP_AW2W_MAX_PIPE (RP1_GEM_AXI_MAX_PIPE) |
           GEM_AMP_AW2B_FILL;
  GemWrite32 (Device, GEM_AMP, Value);

  GemWrite32 (Device, GEM_INTMOD, 0);
  GemWrite32 (Device, MACB_RBQPH, (UINT32)(Device->RxRingDevice >> 32));
  GemWrite32 (Device, MACB_TBQPH, (UINT32)(Device->TxRingDevice >> 32));
  GemWrite32 (Device, MACB_RBQP, (UINT32)Device->RxRingDevice);
  GemWrite32 (Device, MACB_TBQP, (UINT32)Device->TxRingDevice);
  GemWrite32 (Device, MACB_TSR, MACB_TSR_ALL);
  GemWrite32 (Device, MACB_RSR, MACB_RSR_ALL);
  GemWrite32 (Device, MACB_IDR, MACB_INT_ALL);

  /* Multicast hash: accept everything, the SNP filter decides above us. */
  GemWrite32 (Device, GEM_HRB, 0xFFFFFFFFu);
  GemWrite32 (Device, GEM_HRT, 0xFFFFFFFFu);

  Value  = GemRead32 (Device, MACB_NCR);
  Value &= ~(MACB_NCR_RE | MACB_NCR_TE | MACB_NCR_TSTART);
  Value |= MACB_NCR_MPE;
  GemWrite32 (Device, MACB_NCR, Value);

  GemWriteMacAddress (Device);
  return EFI_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Simple Network Protocol                                             */
/* ------------------------------------------------------------------ */

STATIC
EFI_STATUS
EFIAPI
Rp1GemStart (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  RP1_GEM_DEVICE  *Device;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkStopped) {
    return EFI_ALREADY_STARTED;
  }

  Device->Mode.State = EfiSimpleNetworkStarted;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemStop (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  RP1_GEM_DEVICE  *Device;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Device->Mode.State == EfiSimpleNetworkInitialized) {
    This->Shutdown (This);
  }

  Device->Mode.State = EfiSimpleNetworkStopped;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemInitialize (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                         ExtraRxBufferSize  OPTIONAL,
  IN UINTN                         ExtraTxBufferSize  OPTIONAL
  )
{
  RP1_GEM_DEVICE  *Device;
  EFI_STATUS      Status;
  UINTN           Attempt;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Device->Mode.State == EfiSimpleNetworkInitialized) {
    return EFI_SUCCESS;
  }

  if ((ExtraRxBufferSize != 0) || (ExtraTxBufferSize != 0)) {
    Print (
      L"Rp1Gem: extra buffers requested (rx=%u tx=%u)\n",
      (UINT32)ExtraRxBufferSize,
      (UINT32)ExtraTxBufferSize
      );
    return EFI_UNSUPPORTED;
  }

  GemResetRings (Device);

  Status = GemStartHardware (Device);
  if (EFI_ERROR (Status)) {
    Print (L"Rp1Gem: hardware start failed - %r\n", Status);
    return Status;
  }

  PhyProbe (Device);
  PhyConfigure (Device);

  Device->Mode.State                 = EfiSimpleNetworkInitialized;
  Device->Mode.ReceiveFilterSetting  = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
                                       EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST;

  /*
   * Autonegotiation with this PHY needs a few seconds. Waiting here keeps the
   * first DHCP attempt from running before the link exists; callers still see
   * MediaPresent update later if it comes up after the timeout.
   */
  for (Attempt = 0; Attempt < 60; Attempt++) {
    if (GemRefreshLink (Device) && Device->LinkUp) {
      break;
    }

    if (Device->LinkUp) {
      break;
    }

    gBS->Stall (100 * 1000);
  }

  GemApplyLinkState (Device);
  DEBUG ((
    DEBUG_INFO,
    "Rp1Gem: initialized, link %a\n",
    Device->LinkUp ? "up" : "down"
    ));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemReset (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                       ExtendedVerification
  )
{
  RP1_GEM_DEVICE  *Device;
  UINT32          Index;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  GemStopHardware (Device);

  for (Index = 0; Index < RP1_GEM_RX_RING_SIZE; Index++) {
    GemRearmRxDescriptor (Device, Index);
  }

  for (Index = 0; Index < RP1_GEM_TX_RING_SIZE; Index++) {
    Device->TxRing[Index].Control = MACB_TX_USED |
                                    ((Index == (RP1_GEM_TX_RING_SIZE - 1)) ? MACB_TX_WRAP : 0);
  }

  Device->RxTail      = 0;
  Device->TxHead      = 0;
  Device->TxTail      = 0;
  Device->RecycleHead = 0;
  Device->RecycleTail = 0;

  GemStartHardware (Device);
  GemRefreshLink (Device);
  GemApplyLinkState (Device);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemShutdown (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  RP1_GEM_DEVICE  *Device;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  /* The rings outlive Shutdown; they belong to the device, not the session. */
  GemStopHardware (Device);

  Device->Mode.State                = EfiSimpleNetworkStarted;
  Device->Mode.ReceiveFilterSetting = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemReceiveFilters (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINT32                        Enable,
  IN UINT32                        Disable,
  IN BOOLEAN                       ResetMCastFilter,
  IN UINTN                         MCastFilterCnt     OPTIONAL,
  IN EFI_MAC_ADDRESS              *MCastFilter        OPTIONAL
  )
{
  RP1_GEM_DEVICE  *Device;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  if (((Enable | Disable) & ~Device->Mode.ReceiveFilterMask) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  Device->Mode.ReceiveFilterSetting =
    (Device->Mode.ReceiveFilterSetting | Enable) & ~Disable;

  if (ResetMCastFilter) {
    Device->Mode.MCastFilterCount = 0;
  } else if ((MCastFilterCnt != 0) && (MCastFilter != NULL)) {
    if (MCastFilterCnt > Device->Mode.MaxMCastFilterCount) {
      return EFI_INVALID_PARAMETER;
    }

    Device->Mode.MCastFilterCount = (UINT32)MCastFilterCnt;
    CopyMem (
      Device->Mode.MCastFilter,
      MCastFilter,
      MCastFilterCnt * sizeof (EFI_MAC_ADDRESS)
      );
  }

  GemApplyLinkState (Device);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemStationAddress (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                       Reset,
  IN EFI_MAC_ADDRESS              *New  OPTIONAL
  )
{
  RP1_GEM_DEVICE  *Device;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  if (Reset) {
    CopyMem (
      &Device->Mode.CurrentAddress,
      &Device->Mode.PermanentAddress,
      sizeof (EFI_MAC_ADDRESS)
      );
  } else {
    if (New == NULL) {
      return EFI_INVALID_PARAMETER;
    }

    CopyMem (&Device->Mode.CurrentAddress, New, sizeof (EFI_MAC_ADDRESS));
  }

  GemWriteMacAddress (Device);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemStatistics (
  IN     EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN     BOOLEAN                       Reset,
  IN OUT UINTN                        *StatisticsSize  OPTIONAL,
  OUT    EFI_NETWORK_STATISTICS       *StatisticsTable OPTIONAL
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemMCastIpToMac (
  IN  EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN  BOOLEAN                       IPv6,
  IN  EFI_IP_ADDRESS               *Ip,
  OUT EFI_MAC_ADDRESS              *Mac
  )
{
  if ((This == NULL) || (Ip == NULL) || (Mac == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Mac, sizeof (EFI_MAC_ADDRESS));

  if (IPv6) {
    Mac->Addr[0] = 0x33;
    Mac->Addr[1] = 0x33;
    Mac->Addr[2] = Ip->v6.Addr[12];
    Mac->Addr[3] = Ip->v6.Addr[13];
    Mac->Addr[4] = Ip->v6.Addr[14];
    Mac->Addr[5] = Ip->v6.Addr[15];
  } else {
    Mac->Addr[0] = 0x01;
    Mac->Addr[1] = 0x00;
    Mac->Addr[2] = 0x5E;
    Mac->Addr[3] = (UINT8)(Ip->v4.Addr[1] & 0x7F);
    Mac->Addr[4] = Ip->v4.Addr[2];
    Mac->Addr[5] = Ip->v4.Addr[3];
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemNvData (
  IN     EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN     BOOLEAN                       ReadWrite,
  IN     UINTN                         Offset,
  IN     UINTN                         BufferSize,
  IN OUT VOID                         *Buffer
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Reap finished transmits so their buffers can be handed back to the caller.
**/
STATIC
VOID
GemReapTransmits (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Control;

  while (Device->TxTail != Device->TxHead) {
    Control = Device->TxRing[Device->TxTail].Control;
    if ((Control & MACB_TX_USED) == 0) {
      break;
    }

    if (((Device->RecycleHead + 1) % RP1_GEM_RECYCLE_SIZE) == Device->RecycleTail) {
      break;
    }

    /*
     * SNP.GetStatus must return the address the caller passed to Transmit;
     * MNP identifies its own buffers by that pointer and leaks any it does
     * not recognise.
     */
    Device->Recycle[Device->RecycleHead] = Device->TxSlots[Device->TxTail].CallerBuffer;
    Device->RecycleHead                  = (Device->RecycleHead + 1) % RP1_GEM_RECYCLE_SIZE;
    Device->TxTail                       = (Device->TxTail + 1) % RP1_GEM_TX_RING_SIZE;
  }
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemGetStatus (
  IN  EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT UINT32                       *InterruptStatus  OPTIONAL,
  OUT VOID                        **TxBuf            OPTIONAL
  )
{
  RP1_GEM_DEVICE  *Device;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  GemReapTransmits (Device);

  /* MDIO reads are slow, so only re-read the link every so often. */
  Device->LinkPollTick++;
  if ((Device->LinkPollTick % 256) == 0) {
    GemRefreshLink (Device);
  }

  if (InterruptStatus != NULL) {
    *InterruptStatus = 0;
    if (Device->RecycleHead != Device->RecycleTail) {
      *InterruptStatus |= EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT;
    }

    if ((Device->RxRing[Device->RxTail].Address & MACB_RX_USED) != 0) {
      *InterruptStatus |= EFI_SIMPLE_NETWORK_RECEIVE_INTERRUPT;
    }
  }

  if (TxBuf != NULL) {
    if (Device->RecycleHead == Device->RecycleTail) {
      *TxBuf = NULL;
    } else {
      *TxBuf              = Device->Recycle[Device->RecycleTail];
      Device->RecycleTail = (Device->RecycleTail + 1) % RP1_GEM_RECYCLE_SIZE;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemTransmit (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                         HeaderSize,
  IN UINTN                         BufferSize,
  IN VOID                         *Buffer,
  IN EFI_MAC_ADDRESS              *SrcAddr   OPTIONAL,
  IN EFI_MAC_ADDRESS              *DestAddr  OPTIONAL,
  IN UINT16                       *Protocol  OPTIONAL
  )
{
  RP1_GEM_DEVICE  *Device;
  UINT8           *Frame;
  UINT32           Index;
  UINT32           Next;
  UINT16           Type;

  if ((This == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  if (BufferSize > RP1_GEM_BUFFER_SIZE) {
    return EFI_BUFFER_TOO_SMALL;
  }

  if (HeaderSize != 0) {
    if ((HeaderSize != Device->Mode.MediaHeaderSize) ||
        (DestAddr == NULL) || (Protocol == NULL) ||
        (BufferSize < HeaderSize))
    {
      return EFI_INVALID_PARAMETER;
    }
  }

  GemReapTransmits (Device);

  Index = Device->TxHead;
  Next  = (Index + 1) % RP1_GEM_TX_RING_SIZE;
  if ((Next == Device->TxTail) ||
      ((Device->TxRing[Index].Control & MACB_TX_USED) == 0))
  {
    return EFI_NOT_READY;
  }

  Frame = Device->TxSlots[Index].Buffer;
  CopyMem (Frame, Buffer, BufferSize);

  if (HeaderSize != 0) {
    CopyMem (Frame, DestAddr, NET_ETHER_ADDR_LEN);
    if (SrcAddr != NULL) {
      CopyMem (Frame + NET_ETHER_ADDR_LEN, SrcAddr, NET_ETHER_ADDR_LEN);
    } else {
      CopyMem (Frame + NET_ETHER_ADDR_LEN, &Device->Mode.CurrentAddress, NET_ETHER_ADDR_LEN);
    }

    Type                                = *Protocol;
    Frame[2 * NET_ETHER_ADDR_LEN]       = (UINT8)(Type >> 8);
    Frame[(2 * NET_ETHER_ADDR_LEN) + 1] = (UINT8)(Type & 0xFF);
  }

  /* Runt frames are padded by the MAC, but the descriptor length must be sane. */
  if (BufferSize < (NET_ETHER_ADDR_LEN * 2)) {
    return EFI_INVALID_PARAMETER;
  }

  Device->TxSlots[Index].CallerBuffer = Buffer;

  Device->TxRing[Index].AddressHigh = (UINT32)(Device->TxSlots[Index].DeviceAddress >> 32);
  Device->TxRing[Index].Address     = (UINT32)Device->TxSlots[Index].DeviceAddress;
  MemoryFence ();
  Device->TxRing[Index].Control = (UINT32)BufferSize | MACB_TX_LAST |
                                  ((Index == (RP1_GEM_TX_RING_SIZE - 1)) ? MACB_TX_WRAP : 0);
  MemoryFence ();

  Device->TxHead = Next;
  GemWrite32 (Device, MACB_NCR, GemRead32 (Device, MACB_NCR) | MACB_NCR_TSTART);
  return EFI_SUCCESS;
}

/*
 * The GEM stops fetching descriptors once it runs out of free ones and latches
 * BNA/OVR. Software has to rebuild the ring and bounce RE to restart it,
 * otherwise receive stays dead for the rest of the boot.
 */
STATIC
VOID
GemRecoverReceiver (
  IN RP1_GEM_DEVICE  *Device
  )
{
  UINT32  Rsr;
  UINT32  Ncr;
  UINT32  Index;

  Rsr = GemRead32 (Device, MACB_RSR);
  if ((Rsr & (MACB_RSR_BNA | MACB_RSR_OVR)) == 0) {
    return;
  }

  Ncr = GemRead32 (Device, MACB_NCR);
  GemWrite32 (Device, MACB_NCR, Ncr & ~MACB_NCR_RE);

  for (Index = 0; Index < RP1_GEM_RX_RING_SIZE; Index++) {
    GemRearmRxDescriptor (Device, Index);
  }

  Device->RxTail = 0;
  MemoryFence ();

  GemWrite32 (Device, MACB_RBQPH, (UINT32)(Device->RxRingDevice >> 32));
  GemWrite32 (Device, MACB_RBQP, (UINT32)Device->RxRingDevice);
  GemWrite32 (Device, MACB_RSR, MACB_RSR_ALL);
  GemWrite32 (Device, MACB_NCR, Ncr | MACB_NCR_RE);
}

STATIC
EFI_STATUS
EFIAPI
Rp1GemReceive (
  IN     EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT    UINTN                        *HeaderSize  OPTIONAL,
  IN OUT UINTN                        *BufferSize,
  OUT    VOID                         *Buffer,
  OUT    EFI_MAC_ADDRESS              *SrcAddr     OPTIONAL,
  OUT    EFI_MAC_ADDRESS              *DestAddr    OPTIONAL,
  OUT    UINT16                       *Protocol    OPTIONAL
  )
{
  RP1_GEM_DEVICE  *Device;
  UINT8           *Frame;
  UINT32           Index;
  UINT32           Length;
  UINT32           Status;

  if ((This == NULL) || (BufferSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Device = RP1_GEM_FROM_SNP (This);
  if (Device->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_STARTED;
  }

  Index = Device->RxTail;
  if ((Device->RxRing[Index].Address & MACB_RX_USED) == 0) {
    GemRecoverReceiver (Device);
    return EFI_NOT_READY;
  }

  Status = Device->RxRing[Index].Control;
  Length = Status & MACB_RX_FRMLEN_MASK;

  /* Frames split across descriptors cannot happen with full-length buffers. */
  if (((Status & (MACB_RX_SOF | MACB_RX_EOF)) != (MACB_RX_SOF | MACB_RX_EOF)) ||
      (Length == 0) || (Length > RP1_GEM_BUFFER_SIZE))
  {
    GemRearmRxDescriptor (Device, Index);
    Device->RxTail = (Index + 1) % RP1_GEM_RX_RING_SIZE;
    GemWrite32 (Device, MACB_RSR, MACB_RSR_ALL);
    return EFI_NOT_READY;
  }

  if (*BufferSize < Length) {
    *BufferSize = Length;
    return EFI_BUFFER_TOO_SMALL;
  }

  Frame = Device->RxSlots[Index].Buffer;
  CopyMem (Buffer, Frame, Length);
  *BufferSize = Length;

  if (HeaderSize != NULL) {
    *HeaderSize = Device->Mode.MediaHeaderSize;
  }

  if (DestAddr != NULL) {
    ZeroMem (DestAddr, sizeof (EFI_MAC_ADDRESS));
    CopyMem (DestAddr, Frame, NET_ETHER_ADDR_LEN);
  }

  if (SrcAddr != NULL) {
    ZeroMem (SrcAddr, sizeof (EFI_MAC_ADDRESS));
    CopyMem (SrcAddr, Frame + NET_ETHER_ADDR_LEN, NET_ETHER_ADDR_LEN);
  }

  if (Protocol != NULL) {
    *Protocol = (UINT16)((Frame[2 * NET_ETHER_ADDR_LEN] << 8) |
                         Frame[(2 * NET_ETHER_ADDR_LEN) + 1]);
  }

  GemRearmRxDescriptor (Device, Index);
  Device->RxTail = (Index + 1) % RP1_GEM_RX_RING_SIZE;
  GemWrite32 (Device, MACB_RSR, MACB_RSR_REC);
  return EFI_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Discovery and installation                                          */
/* ------------------------------------------------------------------ */

STATIC CONST EFI_SIMPLE_NETWORK_PROTOCOL  mSnpTemplate = {
  EFI_SIMPLE_NETWORK_PROTOCOL_REVISION,
  Rp1GemStart,
  Rp1GemStop,
  Rp1GemInitialize,
  Rp1GemReset,
  Rp1GemShutdown,
  Rp1GemReceiveFilters,
  Rp1GemStationAddress,
  Rp1GemStatistics,
  Rp1GemMCastIpToMac,
  Rp1GemNvData,
  Rp1GemGetStatus,
  Rp1GemTransmit,
  Rp1GemReceive,
  NULL,
  NULL
};

STATIC CONST EFI_GUID  mRp1GemDevicePathGuid = {
  0x3f5c2a18, 0x91d4, 0x4f6b, { 0xa2, 0x7e, 0x8c, 0x1d, 0x5b, 0x60, 0x74, 0x93 }
};

/**
  Find the BAR that exposes the RP1 peripheral window.

  @retval EFI_SUCCESS  BarIndex names a window large enough to hold the GEM.
**/
STATIC
EFI_STATUS
Rp1FindPeripheralBar (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT8                *BarIndex
  )
{
  EFI_STATUS                         Status;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Descriptor;
  UINT8                              Index;

  for (Index = 0; Index < PCI_MAX_BAR; Index++) {
    Descriptor = NULL;
    Status     = PciIo->GetBarAttributes (PciIo, Index, NULL, (VOID **)&Descriptor);
    if (EFI_ERROR (Status) || (Descriptor == NULL)) {
      continue;
    }

    if ((Descriptor->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR) &&
        (Descriptor->ResType == ACPI_ADDRESS_SPACE_TYPE_MEM) &&
        (Descriptor->AddrLen >= (RP1_GEM_BAR_OFFSET + RP1_GEM_REGISTER_LENGTH)) &&
        (Descriptor->AddrLen >= RP1_PERIPHERAL_MIN_SIZE))
    {
      DEBUG ((
        DEBUG_INFO,
        "Rp1Gem: BAR%u base %lx length %lx\n",
        Index,
        Descriptor->AddrRangeMin,
        Descriptor->AddrLen
        ));
      *BarIndex = Index;
      FreePool (Descriptor);
      return EFI_SUCCESS;
    }

    FreePool (Descriptor);
  }

  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
Rp1GemInstall (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS       Status;
  RP1_GEM_DEVICE  *Device;
  UINT8            BarIndex;
  UINT64           Supports;

  Status = Rp1FindPeripheralBar (PciIo, &BarIndex);
  if (EFI_ERROR (Status)) {
    Print (L"Rp1Gem: no peripheral BAR on the RP1 device\n");
    return Status;
  }

  Print (L"Rp1Gem: using BAR%u for the peripheral window\n", BarIndex);

  Device = AllocateZeroPool (sizeof (RP1_GEM_DEVICE));
  if (Device == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Device->Signature      = RP1_GEM_SIGNATURE;
  Device->PciIo          = PciIo;
  Device->BarIndex       = BarIndex;
  Device->RegisterOffset = RP1_GEM_BAR_OFFSET;
  CopyMem (&Device->Snp, &mSnpTemplate, sizeof (EFI_SIMPLE_NETWORK_PROTOCOL));
  Device->Snp.Mode = &Device->Mode;

  Supports = 0;
  Status   = PciIo->Attributes (PciIo, EfiPciIoAttributeOperationSupported, 0, &Supports);
  if (!EFI_ERROR (Status)) {
    PciIo->Attributes (
             PciIo,
             EfiPciIoAttributeOperationEnable,
             Supports & (EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE),
             NULL
             );
  }

  /*
   * Confirm the block answers before driving it. A GEM that is held in reset
   * or unclocked reads back as all-ones, and writing it would wedge the boot.
   */
  if (GemRead32 (Device, GEM_DCFG1) == 0xFFFFFFFFu) {
    Print (L"Rp1Gem: GEM block is not responding\n");
    FreePool (Device);
    return EFI_DEVICE_ERROR;
  }

  /*
   * Claim the DMA rings here rather than from SNP.Initialize: MNP calls that
   * entry point with the TPL raised, and the platform cannot satisfy
   * bus-master allocations above TPL_APPLICATION.
   */
  Status = GemAllocateRings (Device);
  if (EFI_ERROR (Status)) {
    Print (L"Rp1Gem: ring allocation failed - %r\n", Status);
    FreePool (Device);
    return Status;
  }

  Device->Mode.State               = EfiSimpleNetworkStopped;
  Device->Mode.HwAddressSize       = NET_ETHER_ADDR_LEN;
  Device->Mode.MediaHeaderSize     = 14;
  Device->Mode.MaxPacketSize       = RP1_GEM_MTU;
  Device->Mode.NvRamSize           = 0;
  Device->Mode.NvRamAccessSize     = 0;
  Device->Mode.ReceiveFilterMask   = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
                                     EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST |
                                     EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST |
                                     EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS |
                                     EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST;
  Device->Mode.ReceiveFilterSetting  = 0;
  Device->Mode.MaxMCastFilterCount   = MAX_MCAST_FILTER_CNT;
  Device->Mode.MCastFilterCount      = 0;
  Device->Mode.IfType                = NET_IFTYPE_ETHERNET;
  Device->Mode.MacAddressChangeable  = TRUE;
  Device->Mode.MultipleTxSupported   = FALSE;
  Device->Mode.MediaPresentSupported = TRUE;
  Device->Mode.MediaPresent          = FALSE;
  SetMem (&Device->Mode.BroadcastAddress, sizeof (EFI_MAC_ADDRESS), 0);
  SetMem (&Device->Mode.BroadcastAddress, NET_ETHER_ADDR_LEN, 0xFF);

  GemStopHardware (Device);
  GemReadMacAddress (Device);

  Device->DevicePath = AllocateZeroPool (sizeof (RP1_GEM_DEVICE_PATH));
  if (Device->DevicePath == NULL) {
    GemFreeRings (Device);
    FreePool (Device);
    return EFI_OUT_OF_RESOURCES;
  }

  Device->DevicePath->Vendor.Header.Type    = HARDWARE_DEVICE_PATH;
  Device->DevicePath->Vendor.Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&Device->DevicePath->Vendor.Header, sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Device->DevicePath->Vendor.Guid, &mRp1GemDevicePathGuid);

  Device->DevicePath->MacAddress.Header.Type    = MESSAGING_DEVICE_PATH;
  Device->DevicePath->MacAddress.Header.SubType = MSG_MAC_ADDR_DP;
  SetDevicePathNodeLength (&Device->DevicePath->MacAddress.Header, sizeof (MAC_ADDR_DEVICE_PATH));
  CopyMem (
    &Device->DevicePath->MacAddress.MacAddress,
    &Device->Mode.PermanentAddress,
    sizeof (EFI_MAC_ADDRESS)
    );
  Device->DevicePath->MacAddress.IfType = NET_IFTYPE_ETHERNET;
  SetDevicePathEndNode (&Device->DevicePath->End);

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Device->Handle,
                  &gEfiSimpleNetworkProtocolGuid,
                  &Device->Snp,
                  &gEfiDevicePathProtocolGuid,
                  Device->DevicePath,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    Print (L"Rp1Gem: cannot install SNP - %r\n", Status);
    GemFreeRings (Device);
    FreePool (Device->DevicePath);
    FreePool (Device);
    return Status;
  }

  Print (
    L"Rp1Gem: SNP ready, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
    Device->Mode.PermanentAddress.Addr[0],
    Device->Mode.PermanentAddress.Addr[1],
    Device->Mode.PermanentAddress.Addr[2],
    Device->Mode.PermanentAddress.Addr[3],
    Device->Mode.PermanentAddress.Addr[4],
    Device->Mode.PermanentAddress.Addr[5]
    );
  return EFI_SUCCESS;
}

/**
  Entry point: attach to the first RP1 endpoint that exposes a GEM.

  The RP1 is a fixed on-board device, so the driver installs its own handle
  instead of joining the driver model. That keeps it clear of whatever
  platform driver already manages the RP1 controller handle.
**/
EFI_STATUS
EFIAPI
Rp1GemDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_HANDLE           *Handles;
  UINTN                 HandleCount;
  UINTN                 Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT32                PciId;
  UINTN                 Installed;

  mImageHandle = ImageHandle;
  Handles      = NULL;
  HandleCount  = 0;
  Installed    = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Rp1Gem: no PCI I/O handles - %r\n", Status));
    return EFI_SUCCESS;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
    if (EFI_ERROR (Status)) {
      continue;
    }

    PciId  = 0;
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, 1, &PciId);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if ((UINT16)PciId != RP1_PCI_VENDOR_ID) {
      continue;
    }

    Print (L"Rp1Gem: RP1 endpoint %04x:%04x\n", PciId & 0xFFFF, PciId >> 16);
    Status = Rp1GemInstall (PciIo);
    if (!EFI_ERROR (Status)) {
      Installed++;
      break;
    }

    Print (L"Rp1Gem: attach failed - %r\n", Status);
  }

  FreePool (Handles);

  if (Installed == 0) {
    Print (L"Rp1Gem: no RP1 Ethernet controller attached (%u PCI devices)\n",
           (UINT32)HandleCount);
  }

  return EFI_SUCCESS;
}
