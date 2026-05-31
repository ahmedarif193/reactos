/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2712 PCIe config-space backend
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Header for Broadcom BCM2712 (Raspberry Pi 5) indirect
 *              PCI configuration space access via CFG_INDEX / CFG_DATA
 *              registers.  Designed to be detachable into a standalone
 *              rpipx4.sys driver later.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/* ------------------------------------------------------------------ */
/*  BCM2712 PCIe root-complex controller base addresses (CPU view)    */
/* ------------------------------------------------------------------ */
#define BCM2712_PCIE0_RC_BASE       0x1000100000ULL  /* M.2 HAT slot        */
#define BCM2712_PCIE1_RC_BASE       0x1000110000ULL  /* FPC connector       */
#define BCM2712_PCIE2_RC_BASE       0x1000120000ULL  /* RP1 southbridge x4  */

#define BCM2712_PCIE_RC_LENGTH      0x9310ULL        /* Register block size */
#define BCM2712_PCIE_RC_COUNT       3

/* ------------------------------------------------------------------ */
/*  Indirect config-space registers (offsets from RC base)            */
/* ------------------------------------------------------------------ */
#define PCIE_EXT_CFG_DATA           0x8000  /* 4 KB data window         */
#define PCIE_EXT_CFG_INDEX          0x9000  /* B/D/F selector           */

/* ------------------------------------------------------------------ */
/*  Link status register                                              */
/* ------------------------------------------------------------------ */
#define PCIE_MISC_PCIE_STATUS               0x4068
#define PCIE_MISC_PCIE_STATUS_PHYLINKUP     0x00000010  /* Bit 4  */
#define PCIE_MISC_PCIE_STATUS_DL_ACTIVE     0x00000020  /* Bit 5  */

/* ------------------------------------------------------------------ */
/*  Misc control — needed to verify SCB access is enabled             */
/* ------------------------------------------------------------------ */
#define PCIE_MISC_MISC_CTRL                 0x4008
#define PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN   0x00001000  /* Bit 12 */
#define PCIE_MISC_MISC_CTRL_CFG_READ_UR     0x00002000  /* Bit 13 */

/* ------------------------------------------------------------------ */
/*  Inbound DMA windows (RC_BARn + UBUS remap) - RP1 DMA translation   */
/*  BCM2712 BARn layout is 0x402C + 8 * (n - 1).                    */
/* ------------------------------------------------------------------ */
#define RC_BAR1_CONFIG_LO   0x402C  /* PCI base[31:0] | size_code */
#define UBUS_BAR1_REMAP_LO  0x40AC  /* CPU base[31:0] | access_en */

#define PCIE_MISC_RC_BAR_CONFIG_LO_SIZE_MASK      0x0000001F
#define PCIE_MISC_UBUS_BAR_REMAP_ACCESS_EN        0x00000001

#define BCM2712_RP1_DMA_PCI_BASE                  0x1000000000ULL
#define BCM2712_RP1_DMA_CPU_BASE                  0x0000000000ULL
#define BCM2712_RP1_DMA_SIZE                      0x1000000000ULL

/* ------------------------------------------------------------------ */
/*  ACPI OEM identification for RPi5 firmware                         */
/* ------------------------------------------------------------------ */
#define BCM2712_ACPI_OEM_ID         "RPIFDN"
#define BCM2712_ACPI_OEM_TABLE_ID   "RPI5"

/* ------------------------------------------------------------------ */
/*  Platform detection flag — set by Bcm2712PciProbe(), read by HAL   */
/* ------------------------------------------------------------------ */
extern BOOLEAN Bcm2712Detected;

/* ------------------------------------------------------------------ */
/*  Public interface — called from HalpArm64AccessPciConfigSpace      */
/* ------------------------------------------------------------------ */

/**
 * Bcm2712PciProbe - Detect whether we are running on a BCM2712 platform.
 *
 * Checks ACPI OEM identification strings from the cached FADT.
 * Must be called once during HAL Phase-0 init, after ACPI tables are cached.
 *
 * @param LoaderBlock   Boot loader parameter block (Phase 0), or NULL (Phase 1).
 * @return  TRUE if this is a BCM2712 / RPi5 platform.
 */
BOOLEAN
Bcm2712PciProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

/**
 * Bcm2712PciInit - Map the RC register blocks for all three controllers.
 *
 * Called once after Bcm2712PciProbe() returns TRUE.  Maps the MMIO
 * regions and verifies link status on each port.
 *
 * @return  STATUS_SUCCESS or an appropriate error code.
 */
NTSTATUS
Bcm2712PciInit(VOID);

/**
 * Bcm2712PciAccessConfigSpace - Read/write PCI config space.
 *
 * Uses the BCM2712 indirect CFG_INDEX / CFG_DATA mechanism.
 * Drop-in replacement for the ECAM path when MCFG is absent.
 *
 * @param Write     TRUE for a write, FALSE for a read.
 * @param Segment   ACPI PCI segment hint.  BCM2712 bus-number registers
 *                  select the exact root complex when firmware segment
 *                  numbering does not match the hardware index.
 * @param Bus       PCI bus number (0-255).
 * @param Slot      PCI slot (device + function).
 * @param Buffer    Caller buffer.
 * @param Offset    Byte offset into config space (0-4095).
 * @param Length    Byte count (1, 2, or 4 for natural access; arbitrary
 *                  for memcpy-style callers).
 *
 * @return  TRUE if the access was handled by the BCM2712 backend.  Absent
 *          devices and buses return PCI all-ones.  FALSE means the backend
 *          is unavailable or the request is invalid.
 */
BOOLEAN
Bcm2712PciAccessConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length);
