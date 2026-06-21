/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2711 PCIe host + config backend
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Header for Broadcom BCM2711 (Raspberry Pi 4 / CM4) brcmstb
 *              PCIe root-complex bring-up (link training) and indirect
 *              configuration-space access via CFG_INDEX / CFG_DATA.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/* ------------------------------------------------------------------ */
/*  RC register block + memory windows (pftf PcdBcm27xxPci*)          */
/* ------------------------------------------------------------------ */
#define BCM2711_PCIE_RC_BASE        0x00000000FD500000ULL
#define BCM2711_PCIE_RC_LENGTH      0x9310ULL

#define BCM2711_PCIE_CPU_MMIO_BASE  0x0000000600000000ULL  /* CPU view  */
#define BCM2711_PCIE_PCI_MMIO_BASE  0x00000000F8000000ULL  /* PCI view  */
#define BCM2711_PCIE_MMIO_LEN       0x0000000004000000ULL  /* 64 MB     */

/*
 * Inbound DMA window.  The VL805 / bcm2711 PCIe is limited to the first
 * 3GB (pftf Pci.asl _DMA 0..0xbfffffff).  This board is 1GB, so a single
 * 1GB identity window at CPU base 0 covers all of RAM.
 */
#define BCM2711_PCIE_DMA_PCI_BASE   0x0000000000000000ULL
#define BCM2711_PCIE_DMA_CPU_BASE   0x0000000000000000ULL
#define BCM2711_PCIE_DMA_SIZE       0x0000000040000000ULL  /* 1 GB      */

/* ------------------------------------------------------------------ */
/*  Indirect config-space windows (offsets from RC base)              */
/* ------------------------------------------------------------------ */
#define PCIE_EXT_CFG_INDEX          0x9000
#define PCIE_EXT_CFG_DATA           0x8000

/* ------------------------------------------------------------------ */
/*  Bridge reset / PERST                                              */
/* ------------------------------------------------------------------ */
#define PCIE_RGR1_SW_INIT_1                  0x9210
#define PCIE_RGR1_SW_INIT_1_INIT_MASK        0x00000002  /* bridge reset (bit1) */
#define PCIE_RGR1_SW_INIT_1_PERST_MASK       0x00000001  /* PERST#       (bit0) */

/* ------------------------------------------------------------------ */
/*  MISC control                                                      */
/* ------------------------------------------------------------------ */
#define PCIE_MISC_MISC_CTRL                  0x4008
#define PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN    0x00001000
#define PCIE_MISC_MISC_CTRL_CFG_READ_UR      0x00002000
#define PCIE_MISC_MISC_CTRL_MAX_BURST_MASK   0x00300000
#define PCIE_MISC_MISC_CTRL_RCB_MPS_MODE     0x00000400
#define PCIE_MISC_MISC_CTRL_RCB_64B_MODE     0x00000080
#define PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK   0xF8000000
#define PCIE_MISC_MISC_CTRL_SCB0_SIZE_SHIFT  27
#define BCM2711_BURST_SIZE_128               0

/* ------------------------------------------------------------------ */
/*  SerDes power                                                      */
/* ------------------------------------------------------------------ */
#define PCIE_MISC_HARD_PCIE_HARD_DEBUG               0x4204
#define PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ   0x08000000

/* ------------------------------------------------------------------ */
/*  Link status                                                       */
/* ------------------------------------------------------------------ */
#define PCIE_MISC_PCIE_STATUS               0x4068
#define PCIE_MISC_PCIE_STATUS_PHYLINKUP     0x00000010
#define PCIE_MISC_PCIE_STATUS_DL_ACTIVE     0x00000020
#define PCIE_MISC_PCIE_STATUS_RC_MODE       0x00000080

/* ------------------------------------------------------------------ */
/*  Outbound (CPU->PCIe) memory window 0                              */
/* ------------------------------------------------------------------ */
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO         0x400C
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI         0x4010
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT 0x4070
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI    0x4080
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI   0x4084

/* ------------------------------------------------------------------ */
/*  Inbound RC_BAR2 (main dma-ranges)                                 */
/* ------------------------------------------------------------------ */
#define PCIE_MISC_RC_BAR2_CONFIG_LO             0x4034
#define PCIE_MISC_RC_BAR2_CONFIG_HI             0x4038
#define PCIE_MISC_RC_BAR2_CONFIG_LO_SIZE_MASK   0x0000001F

/* Root-port vendor reg — BAR2 endian mode (cleared = little-endian) */
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1                   0x0188
#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_BAR2_MASK  0x0000000C

/* Standard PCI bridge bus-number register (root-port cfg @ RC base) */
#define BCM2711_PCI_BRIDGE_BUS_NUMBERS      0x18

/* ------------------------------------------------------------------ */
/*  ACPI OEM identification for RPi4/CM4 firmware                     */
/* ------------------------------------------------------------------ */
#define BCM2711_ACPI_OEM_ID         "RPIFDN"
#define BCM2711_ACPI_OEM_TABLE_ID   "RPI4"

extern BOOLEAN Bcm2711Detected;

BOOLEAN
Bcm2711PciProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

NTSTATUS
Bcm2711PciInit(VOID);

BOOLEAN
Bcm2711PciAccessConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length);
