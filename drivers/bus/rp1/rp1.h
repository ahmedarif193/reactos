/*
 * PROJECT:     ReactOS RP1 Bus Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * FILE:        drivers/bus/rp1/rp1.h
 * PURPOSE:     RP1 southbridge bus driver header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdm.h>

/* RP1 PCI identity */
#define RP1_VENDOR_ID       0x1DE4
#define RP1_DEVICE_ID       0x0001

/*
 * BCM2712 PCIE2 outbound window used by current RPi5 firmware.
 * Keep bus-relative PCI addresses separate from CPU-visible physical
 * addresses; firmware currently reports the child window without the
 * translation ReactOS needs to map it.
 */
#define BCM2712_PCIE2_PCI_MEM_BASE  0xC0000000ULL
#define BCM2712_PCIE2_CPU_MEM_BASE  0x1F00000000ULL

/* Pool allocation tag: 'RP1B' */
#define RP1_TAG             'B1PR'

/*
 * RP1 BAR1 peripheral map.
 * BAR1 is a 4 MB region containing all RP1 on-chip peripherals.
 */
#define RP1_BAR1_SIZE           0x00400000  /* 4 MB */

/* xHCI (DWC3 USB 3.0) controller offsets within BAR1 */
#define RP1_XHCI0_OFFSET        0x00200000
#define RP1_XHCI0_SIZE          0x00100000

#define RP1_XHCI1_OFFSET        0x00300000
#define RP1_XHCI1_SIZE          0x00100000

#define RP1_XHCI_PCI_PROGIF     0x30  /* xHCI */
/* Synthetic config-space device IDs, not external PnP hardware IDs. */
#define RP1_XHCI_PCI_DEVICE_ID_BASE 0x0100

/* RP1 clock controller (within BAR1) */
#define RP1_CLOCKS_OFFSET       0x00018000

/*
 * USB clock register offsets (relative to RP1_CLOCKS_OFFSET).
 * Each clock has CTRL, DIV_INT, and SEL registers.
 */
#define RP1_CLK_USBH0_MICROFRAME_CTRL  0x0F4
#define RP1_CLK_USBH0_MICROFRAME_DIV   0x0F8
#define RP1_CLK_USBH0_MICROFRAME_SEL   0x100

#define RP1_CLK_USBH1_MICROFRAME_CTRL  0x104
#define RP1_CLK_USBH1_MICROFRAME_DIV   0x108
#define RP1_CLK_USBH1_MICROFRAME_SEL   0x110

#define RP1_CLK_USBH0_SUSPEND_CTRL     0x114
#define RP1_CLK_USBH0_SUSPEND_DIV      0x118
#define RP1_CLK_USBH0_SUSPEND_SEL      0x120

#define RP1_CLK_USBH1_SUSPEND_CTRL     0x124
#define RP1_CLK_USBH1_SUSPEND_DIV      0x128
#define RP1_CLK_USBH1_SUSPEND_SEL      0x130

/* Clock control register bits */
#define RP1_CLK_CTRL_ENABLE             (1u << 11)

/* ------------------------------------------------------------------ */
/*  DWC3 (Synopsys DesignWare USB3) register offsets and bits          */
/*  Offsets are relative to the DWC3 block base (e.g. BAR1+0x200000)  */
/* ------------------------------------------------------------------ */

/* Global registers (offset from DWC3 base) */
#define DWC3_GSBUSCFG0              0xC100
#define DWC3_GCTL                   0xC110  /* Global Control */
#define DWC3_GSTS                   0xC118  /* Global Status (RO) */
#define DWC3_GSNPSID               0xC120  /* Synopsys ID (RO) */
#define DWC3_GHWPARAMS0            0xC140  /* Hardware Params 0 (RO) */
#define DWC3_GHWPARAMS3            0xC14C  /* Hardware Params 3 (RO) */
#define DWC3_GUSB2PHYCFG(n)       (0xC200 + ((n) * 4))  /* USB2 PHY Config */
#define DWC3_GUSB3PIPECTL(n)      (0xC2C0 + ((n) * 4))  /* USB3 Pipe Control */

/* GCTL bits */
#define DWC3_GCTL_PRTCAPDIR_MASK    (3u << 12)
#define DWC3_GCTL_PRTCAP_HOST       (1u << 12)
#define DWC3_GCTL_PRTCAP_DEVICE     (2u << 12)
#define DWC3_GCTL_U2RSTECN          (1u << 16)

/* GUSB2PHYCFG bits */
#define DWC3_GUSB2PHYCFG_PHYIF      (1u << 3)   /* 0=8bit UTMI, 1=16bit */
#define DWC3_GUSB2PHYCFG_SUSPHY     (1u << 6)   /* Suspend PHY */
#define DWC3_GUSB2PHYCFG_ENBLSLPM   (1u << 8)   /* Enable sleep mode */
#define DWC3_GUSB2PHYCFG_USBTRDTIM_MASK (0xFu << 10)
#define DWC3_GUSB2PHYCFG_USBTRDTIM_8BIT (9u << 10)

/* GUSB3PIPECTL bits */
#define DWC3_GUSB3PIPECTL_SUSPHY    (1u << 17)  /* Suspend SS PHY */
#define DWC3_GUSB3PIPECTL_UX_EXIT_PX (1u << 27) /* Known PHY bug */

#define DWC3_GFLADJ                   0xC630
#define DWC3_GFLADJ_30MHZ_SDBND_SEL   (1u << 7)
#define DWC3_GFLADJ_30MHZ_MASK        0x3F

/* Maximum number of child devices the bus driver exposes */
#define RP1_MAX_CHILDREN        2

/* Child device indices */
#define RP1_CHILD_XHCI0         0
#define RP1_CHILD_XHCI1         1

/*
 * Child device descriptor.
 * Describes one peripheral carved out of the RP1 BAR1 space.
 */
typedef struct _RP1_CHILD_DESCRIPTOR
{
    ULONG  Offset;      /* Offset from BAR1 base */
    ULONG  Length;       /* MMIO region size */
    PCWSTR InstanceId;   /* Unique instance identifier */
} RP1_CHILD_DESCRIPTOR, *PRP1_CHILD_DESCRIPTOR;

/*
 * Common device extension header.
 * First field distinguishes FDO from PDO in shared dispatch routines.
 */
typedef struct _RP1_COMMON_EXTENSION
{
    BOOLEAN IsFdo;
} RP1_COMMON_EXTENSION, *PRP1_COMMON_EXTENSION;

/*
 * FDO device extension.
 * Attached to the functional device object that sits on the RP1 PCI PDO.
 */
typedef struct _RP1_FDO_EXTENSION
{
    RP1_COMMON_EXTENSION Common;

    /* Lower device object (PCI PDO) */
    PDEVICE_OBJECT LowerDevice;

    /* Physical device object we are attached to */
    PDEVICE_OBJECT PhysicalDevice;

    /* BAR1 bus-relative PCI address and CPU-visible physical address */
    PHYSICAL_ADDRESS Bar1BusAddress;
    PHYSICAL_ADDRESS Bar1CpuAddress;

    /* BAR1 mapped virtual address */
    PVOID Bar1Virtual;

    /* BAR1 length (should be RP1_BAR1_SIZE) */
    ULONG Bar1Length;

    /* Interrupt level/vector from PCI resources */
    ULONG InterruptLevel;
    ULONG InterruptVector;
    KAFFINITY InterruptAffinity;

    /* Child PDO array */
    PDEVICE_OBJECT ChildPdo[RP1_MAX_CHILDREN];
    BOOLEAN ChildPresent[RP1_MAX_CHILDREN];
    ULONG ChildCount;

    /* Set when START_DEVICE has completed successfully */
    BOOLEAN Started;

} RP1_FDO_EXTENSION, *PRP1_FDO_EXTENSION;

/*
 * PDO device extension.
 * Attached to each child physical device object (one per xHCI controller).
 */
typedef struct _RP1_PDO_EXTENSION
{
    RP1_COMMON_EXTENSION Common;

    /* This PDO */
    PDEVICE_OBJECT Self;

    /* Back pointer to the parent FDO */
    PDEVICE_OBJECT ParentFdo;

    /* Which child this PDO represents (RP1_CHILD_XHCI0 or RP1_CHILD_XHCI1) */
    ULONG ChildIndex;

    /* MMIO region: bus-relative and CPU-visible address for this child */
    PHYSICAL_ADDRESS MmioBusAddress;
    PHYSICAL_ADDRESS MmioPhysical;
    ULONG MmioLength;

    /* Interrupt from parent (shared with PCI device) */
    ULONG InterruptLevel;
    ULONG InterruptVector;
    KAFFINITY InterruptAffinity;

    /* Mutable fields in the synthetic PCI config view exposed to USBPORT */
    USHORT PciCommand;
    UCHAR CacheLineSize;
    UCHAR LatencyTimer;
    UCHAR InterruptLine;

} RP1_PDO_EXTENSION, *PRP1_PDO_EXTENSION;

/* Static child descriptor table */
extern const RP1_CHILD_DESCRIPTOR Rp1Children[RP1_MAX_CHILDREN];

/* FDO dispatch */
NTSTATUS
Rp1FdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/* PDO dispatch */
NTSTATUS
Rp1PdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);
