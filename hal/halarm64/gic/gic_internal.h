/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/gic/gic_internal.h
 * PURPOSE:         ARM GIC (Generic Interrupt Controller) Internal Definitions
 *
 * DESCRIPTION:
 *   Internal header file for GIC support on ARM64. Contains register definitions,
 *   helper macros, and function declarations shared across the GIC implementation
 *   files (gicv2.c, gicv3.c, gic_redist.c, gic_its.c, gic_common.c, gic_init.c).
 *
 *   This file is not intended for external use - only GIC implementation files
 *   should include it.
 *
 * REFERENCES:
 *   - ARM GIC Architecture Specification (IHI 0069)
 *   - ARM GICv3/v4 Specification (IHI 0069)
 *   - ARM ITS Specification (part of GICv3)
 */

#ifndef _GIC_INTERNAL_H_
#define _GIC_INTERNAL_H_

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <halacpi_arm64.h>
#include <debug.h>

/*
 * ============================================================================
 * GIC Default Physical Addresses (QEMU virt platform)
 * ============================================================================
 */
#define HAL_ARM64_GICD_BASE_DEFAULT         0x08000000ULL
#define HAL_ARM64_GICC_BASE_DEFAULT         0x08010000ULL
#define HAL_ARM64_GICR_BASE_DEFAULT         0x080A0000ULL
#define HAL_ARM64_GICR_STRIDE_DEFAULT       0x20000ULL
#define HAL_ARM64_GICR_SGI_OFFSET_DEFAULT   0x10000ULL
#define HAL_ARM64_KSEG0_BASE                0xFFFF800000000000ULL

/*
 * ============================================================================
 * GIC Distributor (GICD) Register Offsets
 * ============================================================================
 */
#define GICD_CTLR           0x000
#define GICD_TYPER          0x004
#define GICD_IIDR           0x008
#define GICD_IGROUPR        0x080
#define GICD_ISENABLER      0x100
#define GICD_ICENABLER      0x180
#define GICD_ISPENDR        0x200
#define GICD_ICPENDR        0x280
#define GICD_ISACTIVER      0x300
#define GICD_ICACTIVER      0x380
#define GICD_IPRIORITYR     0x400
#define GICD_ITARGETSR      0x800
#define GICD_ICFGR          0xC00
#define GICD_SGIR           0xF00
#define GICD_PIDR2          0xFE8
#define GICD_IROUTER        0x6100

/*
 * GICD_CTLR bit definitions
 */
#define GICD_CTLR_ENABLE_GRP0       (1u << 0)
#define GICD_CTLR_ENABLE_GRP1_NS    (1u << 1)
#define GICD_CTLR_ENABLE_GRP1_S     (1u << 2)
#define GICD_CTLR_ARE_NS            (1u << 4)
#define GICD_CTLR_ARE_S             (1u << 5)
#define GICD_CTLR_DS                (1u << 6)

/*
 * GICD_TYPER bit definitions
 */
#define GICD_TYPER_ITLINES_MASK     0x1F
#define GICD_TYPER_CPU_NUM_SHIFT    5
#define GICD_TYPER_CPU_NUM_MASK     0x7
#define GICD_TYPER_SECURITY_EXT     (1u << 10)

/*
 * ============================================================================
 * GIC CPU Interface (GICC) Register Offsets (GICv2 Legacy)
 * ============================================================================
 */
#define GICC_CTLR           0x000
#define GICC_PMR            0x004
#define GICC_BPR            0x008
#define GICC_IAR            0x00C
#define GICC_EOIR           0x010
#define GICC_RPR            0x014
#define GICC_HPPIR          0x018
#define GICC_AIAR           0x020
#define GICC_AEOIR          0x024
#define GICC_IIDR           0x0FC

/*
 * GICC_CTLR bit definitions
 */
#define GICC_CTLR_ENABLE_GRP0       (1u << 0)
#define GICC_CTLR_ENABLE_GRP1       (1u << 1)
#define GICC_CTLR_EOI_MODE_NS       (1u << 9)
#define GICC_CTLR_EOI_MODE_S        (1u << 10)

/*
 * ============================================================================
 * GIC Redistributor (GICR) Register Offsets (GICv3+)
 * ============================================================================
 */
/* RD_base frame */
#define GICR_CTLR           0x000
#define GICR_IIDR           0x004
#define GICR_TYPER          0x008
#define GICR_WAKER          0x014
#define GICR_PROPBASER      0x070
#define GICR_PENDBASER      0x078

/* SGI_base frame (offset from GICR base) */
#define GICR_IGROUPR0       0x080
#define GICR_ISENABLER0     0x100
#define GICR_ICENABLER0     0x180
#define GICR_ISPENDR0       0x200
#define GICR_ICPENDR0       0x280
#define GICR_ISACTIVER0     0x300
#define GICR_ICACTIVER0     0x380
#define GICR_IPRIORITYR     0x400
#define GICR_ICFGR0         0xC00   /* SGIs - read-only */
#define GICR_ICFGR1         0xC04   /* PPIs - read/write */

/*
 * GICR_TYPER bit definitions
 */
#define GICR_TYPER_LAST             (1ULL << 4)
#define GICR_TYPER_AFFINITY_SHIFT   32

/*
 * GICR_WAKER bit definitions
 */
#define GICR_WAKER_PROCESSOR_SLEEP  (1u << 0)
#define GICR_WAKER_CHILDREN_ASLEEP  (1u << 2)

/*
 * GICR LPI configuration registers
 */
#define GICR_PROPBASER_ADDR_MASK    0x0000FFFFFFFFF000ULL
#define GICR_PROPBASER_IDBITS_MASK  0x1FULL
#define GICR_PENDBASER_ADDR_MASK    0x0000FFFFFFFFF000ULL

/*
 * ============================================================================
 * GIC ITS (Interrupt Translation Service) Register Offsets
 * ============================================================================
 */
#define GITS_CTLR           0x0000
#define GITS_IIDR           0x0004
#define GITS_TYPER          0x0008
#define GITS_CBASER         0x0080
#define GITS_CWRITER        0x0088
#define GITS_CREADR         0x0090
#define GITS_BASER          0x0100
#define GITS_TRANSLATER     0x10000

#define HAL_ARM64_GITS_BASER_COUNT  8

/*
 * GITS_TYPER bit definitions
 */
#define GITS_TYPER_ITT_ENTRY_SIZE_SHIFT     4
#define GITS_TYPER_ITT_ENTRY_SIZE_MASK      0xFULL
#define GITS_TYPER_IDBITS_SHIFT             8
#define GITS_TYPER_IDBITS_MASK              0x1FULL
#define GITS_TYPER_DEVBITS_SHIFT            13
#define GITS_TYPER_DEVBITS_MASK             0x1FULL

/*
 * GITS_BASER bit definitions
 */
#define GITS_BASER_VALID            (1ULL << 63)
#define GITS_BASER_TYPE_SHIFT       56
#define GITS_BASER_TYPE_MASK        0x7ULL
#define GITS_BASER_ENTRY_SHIFT      48
#define GITS_BASER_ENTRY_MASK       0x1FULL
#define GITS_BASER_PAGE_SHIFT       8
#define GITS_BASER_PAGE_MASK        0x3ULL
#define GITS_BASER_SIZE_MASK        0xFFULL
#define GITS_BASER_ADDR_MASK        0x0000FFFFFFFFF000ULL

#define GITS_BASER_TYPE_DEVICE      1
#define GITS_BASER_TYPE_COLLECTION  2

/*
 * GITS_CBASER bit definitions
 */
#define GITS_CBASER_VALID           (1ULL << 63)
#define GITS_CBASER_SIZE_MASK       0xFFULL
#define GITS_CBASER_ADDR_MASK       0x0000FFFFFFFFF000ULL

/*
 * ITS command opcodes
 */
#define GITS_CMD_SYNC       0x05
#define GITS_CMD_MAPD       0x08
#define GITS_CMD_MAPC       0x09
#define GITS_CMD_MAPTI      0x0A
#define GITS_CMD_MAPI       0x0B
#define GITS_CMD_INV        0x0C
#define GITS_CMD_INVALL     0x0D
#define GITS_CMD_MOVALL     0x0E
#define GITS_CMD_DISCARD    0x0F
#define GITS_CMD_CLEAR      0x04

/*
 * GICv4 ITS command opcodes (Virtual LPI support)
 */
#define GITS_CMD_VMAPP      0x2A    /* Map vPE to ITS */
#define GITS_CMD_VMAPTI     0x2B    /* Map virtual interrupt to LPI */
#define GITS_CMD_VMOVI      0x2C    /* Move virtual interrupt between vPEs */
#define GITS_CMD_VMOVP      0x2D    /* Move vPE to different redistributor */
#define GITS_CMD_VSYNC      0x2E    /* Sync virtual PE */
#define GITS_CMD_VINVALL    0x2F    /* Invalidate all VLPIs for a vPE */
#define GITS_CMD_VSGI       0x30    /* Virtual SGI command (GICv4.1) */

/*
 * ============================================================================
 * GICv3.1/v4 Extended Register Definitions
 * ============================================================================
 */

/*
 * GICD_PIDR2 - Distributor Peripheral ID 2 (for version detection)
 */
#define GICD_PIDR2_ARCHREV_MASK     0xF0
#define GICD_PIDR2_ARCHREV_SHIFT    4
#define GICD_PIDR2_ARCHREV_GICv3    0x03
#define GICD_PIDR2_ARCHREV_GICv4    0x04

/*
 * GICD_TYPER bit definitions (extended for GICv3.1/v4)
 */
#define GICD_TYPER_ESPI             (1u << 8)   /* Extended SPI range (GICv3.1+) */
#define GICD_TYPER_LPIS             (1u << 17)  /* LPIs supported */
#define GICD_TYPER_MBIS             (1u << 16)  /* Message-based SPIs (GICv3.1+) */
#define GICD_TYPER_RSS              (1u << 26)  /* Range selector support (GICv3.1+) */
#define GICD_TYPER_NUM_LPIS_SHIFT   11
#define GICD_TYPER_NUM_LPIS_MASK    0x1F
#define GICD_TYPER_ESPI_RANGE_SHIFT 27
#define GICD_TYPER_ESPI_RANGE_MASK  0x1F

/*
 * GITS_TYPER bit definitions (extended for GICv4)
 */
#define GITS_TYPER_PHYSICAL         (1ULL << 0)   /* Physical LPIs supported */
#define GITS_TYPER_VIRTUAL          (1ULL << 1)   /* Virtual LPIs supported (GICv4) */
#define GITS_TYPER_VMOVP            (1ULL << 26)  /* Single VMOVP command capable */
#define GITS_TYPER_PTA              (1ULL << 19)  /* Physical target addresses */
#define GITS_TYPER_SVPET_SHIFT      17
#define GITS_TYPER_SVPET_MASK       0x3ULL
#define GITS_TYPER_VPEID_BITS_SHIFT 22
#define GITS_TYPER_VPEID_BITS_MASK  0x1FULL

/*
 * GITS_BASER type definitions (extended for GICv4)
 */
#define GITS_BASER_TYPE_VCPU        0x2         /* Virtual CPU table (GICv4) */
#define GITS_BASER_TYPE_VPE         0x2         /* vPE table (alias) */
#define GITS_BASER_INDIRECT         (1ULL << 62)

/*
 * GICR VLPI Register Offsets (VLPI_base frame at offset 0x20000)
 */
#define GICR_VLPI_OFFSET            0x20000     /* Offset to VLPI_base frame */
#define GICR_VPROPBASER             0x70        /* Virtual LPI Property Table Base */
#define GICR_VPENDBASER             0x78        /* Virtual LPI Pending Table Base */

/*
 * GICR_VPROPBASER bit definitions (GICv4)
 */
#define GICR_VPROPBASER_VALID           (1ULL << 63)
#define GICR_VPROPBASER_IDBITS_MASK     0x1FULL
#define GICR_VPROPBASER_ADDR_MASK       0x0000FFFFFFFFF000ULL
#define GICR_VPROPBASER_SHAREABILITY_MASK   (0x3ULL << 10)
#define GICR_VPROPBASER_INNER_CACHEABILITY_MASK (0x7ULL << 7)
#define GICR_VPROPBASER_OUTER_CACHEABILITY_MASK (0x7ULL << 56)

/* GICv4.1 specific VPROPBASER fields */
#define GICR_VPROPBASER_4_1_VALID       (1ULL << 0)
#define GICR_VPROPBASER_4_1_INDIRECT    (1ULL << 1)
#define GICR_VPROPBASER_4_1_PAGE_SIZE_MASK (0x3ULL << 2)
#define GICR_VPROPBASER_4_1_PAGE_SIZE   (0x3ULL << 2)
#define GICR_VPROPBASER_4_1_ENTRY_SIZE  (0x1FULL << 4)
#define GICR_VPROPBASER_4_1_SIZE        (0xFFULL << 9)
#define GICR_VPROPBASER_4_1_ADDR        (0xFFFFFFFFFFULL << 12)
#define GICR_VPROPBASER_4_1_Z           (1ULL << 59)

/*
 * GICR_VPENDBASER bit definitions (GICv4)
 */
#define GICR_VPENDBASER_Valid           (1ULL << 63)
#define GICR_VPENDBASER_PendingLast     (1ULL << 61)
#define GICR_VPENDBASER_Dirty           (1ULL << 60)
#define GICR_VPENDBASER_ADDR_MASK       0x0000FFFFFFFFF000ULL
#define GICR_VPENDBASER_VPEID_MASK      0xFFFFULL
#define GICR_VPENDBASER_SHAREABILITY_MASK   (0x3ULL << 10)
#define GICR_VPENDBASER_INNER_CACHEABILITY_MASK (0x7ULL << 7)
#define GICR_VPENDBASER_OUTER_CACHEABILITY_MASK (0x7ULL << 56)

/* GICv4.1 specific VPENDBASER fields */
#define GICR_VPENDBASER_4_1_VGRP0EN     (1ULL << 62)
#define GICR_VPENDBASER_4_1_VGRP1EN     (1ULL << 61)
#define GICR_VPENDBASER_4_1_DB          (1ULL << 60)

/*
 * GICR_TYPER extended bit definitions for GICv4
 */
#define GICR_TYPER_RVPEID               (1ULL << 7)   /* RVPEID support (GICv4.1) */
#define GICR_TYPER_COMMON_LPI_AFF_SHIFT 24
#define GICR_TYPER_COMMON_LPI_AFF_MASK  0x3ULL

/*
 * Maximum vPE ID bits and count
 */
#define HALP_GIC_MAX_VPEID_BITS         16
#define HALP_GIC_MAX_VPEID              (1 << HALP_GIC_MAX_VPEID_BITS)

/*
 * GIC page size definitions (for ITS tables)
 */
#define GIC_PAGE_SIZE_4K                0
#define GIC_PAGE_SIZE_16K               1
#define GIC_PAGE_SIZE_64K               2

/*
 * GITS Level 1 entry size for indirect tables
 */
#define GITS_LVL1_ENTRY_SIZE            8

/*
 * ============================================================================
 * GIC MSI Frame (GICv2m) Definitions
 * ============================================================================
 */
#define HAL_ARM64_GICV2M_SETSPI     0x40

/*
 * ============================================================================
 * LPI (Locality-specific Peripheral Interrupt) Definitions
 * ============================================================================
 *
 * Following Linux's approach in drivers/irqchip/irq-gic-v3-its.c:
 * - lpi_id_bits determines the maximum number of LPIs supported
 * - PROPBASE size is 2^lpi_id_bits bytes (one configuration byte per LPI)
 * - PENDBASE size is 2^lpi_id_bits / 8 bytes (one bit per LPI)
 * - Both tables must be 64KB aligned
 */
#define HAL_ARM64_LPI_BASE              8192u
#define HAL_ARM64_LPI_COUNT             8192u   /* Increased from 1024 for dynamic allocation */
#define HAL_ARM64_LPI_PROP_ENABLED      0x01u
#define HAL_ARM64_LPI_PROP_GROUP1       0x02u
#define HAL_ARM64_LPI_PROP_PRIO_DEFAULT 0xA0u

/*
 * Maximum LPI ID bits supported (determines LPI table sizes)
 * Linux uses lpi_id_bits which is typically 14-16 bits
 * We use 13 bits = 8192 LPIs by default, can be increased
 */
#define HAL_ARM64_LPI_ID_BITS_DEFAULT   13
#define HAL_ARM64_LPI_ID_BITS_MAX       16      /* 64K LPIs max */

/*
 * ============================================================================
 * SGI (Software Generated Interrupt) Definitions
 * ============================================================================
 */
#define HAL_ARM64_SGI_IPI   0
#define HAL_ARM64_SGI_APC   1
#define HAL_ARM64_SGI_DPC   2

/*
 * ============================================================================
 * ITS Command Queue Definitions
 * ============================================================================
 *
 * Following Linux's ITS_CMD_QUEUE_SZ = SZ_64K (64KB command queue)
 * Each command is 32 bytes (256 bits), giving 2048 entries
 */
#define HAL_ARM64_ITS_CMD_ENTRY_SIZE    32      /* 256 bits per command */
#define HAL_ARM64_ITS_CMD_QUEUE_SIZE    (64 * 1024)   /* 64KB total */
#define HAL_ARM64_ITS_CMD_QUEUE_PAGES   ((HAL_ARM64_ITS_CMD_QUEUE_SIZE) / PAGE_SIZE)
#define HAL_ARM64_ITS_CMD_QUEUE_ENTRIES ((HAL_ARM64_ITS_CMD_QUEUE_SIZE) / HAL_ARM64_ITS_CMD_ENTRY_SIZE)

/*
 * ITT (Interrupt Translation Table) alignment requirement
 * Following Linux's ITS_ITT_ALIGN = SZ_256
 */
#define HAL_ARM64_ITS_ITT_ALIGN         256

/*
 * Maximum number of ITS nodes supported
 * Multi-socket systems can have one ITS per socket
 */
#define HALP_GIC_MAX_ITS_NODES          8

/*
 * ============================================================================
 * GIC Version and Capabilities Information
 * ============================================================================
 *
 * Tracks detected GIC version and capabilities for Windows 11 ARM64 compatibility.
 * This structure is populated during GIC initialization by reading GICD_PIDR2,
 * GICD_TYPER, and GICR_TYPER registers.
 */
typedef struct _HALP_GIC_VERSION_INFO
{
    /* GIC architecture version (3 or 4, from GICD_PIDR2) */
    ULONG Architecture;

    /* GICv3.1 features */
    BOOLEAN HasExtendedSpiRange;    /* ESPI: Extended SPI range (up to 1020 SPIs) */
    BOOLEAN HasMessageBasedSpi;     /* MBIS: Message-based SPIs */
    BOOLEAN HasRangeSelector;       /* RSS: Range selector support */
    ULONG ExtendedSpiStart;         /* First Extended SPI INTID (4096) */
    ULONG ExtendedSpiCount;         /* Number of Extended SPIs */

    /* GICv4 features (Virtual LPI / Direct Injection) */
    BOOLEAN HasVlpis;               /* GICR_TYPER.VLPIS: Virtual LPIs supported */
    BOOLEAN HasDirectLpi;           /* GICR_TYPER.DirectLPI: Direct LPI injection */
    BOOLEAN HasRvpeid;              /* GICR_TYPER.RVPEID: GICv4.1 RVPEID support */
    BOOLEAN HasItsVirtual;          /* GITS_TYPER.Virtual: ITS supports VLPIs */
    ULONG VpeidBits;                /* Number of vPE ID bits supported */
    ULONG MaxVpeid;                 /* Maximum vPE ID (2^VpeidBits - 1) */

    /* Interrupt limits */
    ULONG MaxSpiId;                 /* Maximum SPI INTID (normally 1019) */
    ULONG MaxLpiId;                 /* Maximum LPI INTID */
    ULONG TotalInterruptLines;      /* Total interrupt lines from GICD_TYPER */

    /* Initialization state */
    BOOLEAN Initialized;
} HALP_GIC_VERSION_INFO, *PHALP_GIC_VERSION_INFO;

/*
 * ============================================================================
 * Virtual Processing Element (vPE) Structure for GICv4
 * ============================================================================
 *
 * Following Linux's struct its_vpe in drivers/irqchip/irq-gic-v3-its.c
 * Each vPE represents a virtual CPU within a virtual machine and contains:
 * - Virtual Pending Table (VPT) for VLPI pending state
 * - vPE ID for ITS command targeting
 * - Doorbell LPI for VM scheduling notifications
 */
typedef struct _HALP_GIC_VPE
{
    /* vPE identification */
    ULONG VpeId;                    /* Unique vPE identifier (0 to MaxVpeid-1) */
    ULONG VmId;                     /* Virtual Machine ID (for HAL tracking) */
    ULONG VpIndex;                  /* Virtual Processor index within VM */

    /* Virtual Pending Table (VPT) */
    PVOID VptBase;                  /* Virtual Pending Table virtual address */
    PHYSICAL_ADDRESS VptPa;         /* Virtual Pending Table physical address */
    SIZE_T VptSize;                 /* VPT size in bytes */
    PVOID VptRaw;                   /* Raw allocation for freeing */

    /* vPE configuration */
    ULONG DoorbellLpi;              /* Doorbell LPI for scheduling notifications */
    ULONG CollectionId;             /* Target collection (CPU) when scheduled */

    /* vPE residency tracking */
    volatile LONG ResidentCpu;      /* CPU where vPE is currently resident (-1 if none) */
    volatile LONG VmappCount;       /* Reference count for VMAPP mappings */
    BOOLEAN Resident;               /* TRUE if currently scheduled on a CPU */

    /* Linked list for VM's vPE list */
    struct _HALP_GIC_VPE *Next;

    /* Statistics */
    ULONG VlpiCount;                /* Number of VLPIs mapped to this vPE */
    ULONG ScheduleCount;            /* Number of times scheduled */
    ULONG DescheduleCount;          /* Number of times descheduled */
} HALP_GIC_VPE, *PHALP_GIC_VPE;

/*
 * ============================================================================
 * Virtual Machine Structure for GICv4
 * ============================================================================
 *
 * Tracks per-VM state for GICv4 virtual interrupt injection.
 */
typedef struct _HALP_GIC_VM
{
    /* VM identification */
    ULONG VmId;                     /* Unique VM identifier */

    /* Virtual property table (shared by all vPEs in VM) */
    PVOID VpropBase;                /* Virtual LPI property table virtual address */
    PHYSICAL_ADDRESS VpropPa;       /* Virtual LPI property table physical address */
    SIZE_T VpropSize;               /* Property table size in bytes */
    PVOID VpropRaw;                 /* Raw allocation for freeing */

    /* vPE management */
    PHALP_GIC_VPE VpeList;          /* List of vPEs in this VM */
    ULONG VpeCount;                 /* Number of vPEs */
    KSPIN_LOCK VpeLock;             /* Lock protecting vPE list */

    /* VLPI allocation */
    RTL_BITMAP VlpiBitmap;          /* Bitmap for VLPI allocation */
    PULONG VlpiBitmapBuffer;        /* Bitmap storage */
    ULONG VlpiCount;                /* Total VLPIs available */
    ULONG VlpiAllocated;            /* VLPIs currently allocated */

    /* Linked list for global VM list */
    struct _HALP_GIC_VM *Next;
    struct _HALP_GIC_VM *Prev;

    /* Initialization state */
    volatile LONG InitState;        /* 0=pending, 1=initializing, 2=done, -1=failed */
} HALP_GIC_VM, *PHALP_GIC_VM;

/*
 * ============================================================================
 * VLPI (Virtual LPI) Mapping Entry
 * ============================================================================
 *
 * Tracks a virtual interrupt mapping for a vPE.
 */
typedef struct _HALP_GIC_VLPI_ENTRY
{
    ULONG VirtIntId;                /* Virtual interrupt ID within VM */
    ULONG PhysLpi;                  /* Physical LPI backing this VLPI */
    ULONG EventId;                  /* Event ID in device ITT */
    ULONG DeviceId;                 /* Device ID for ITS */
    PHALP_GIC_VPE Vpe;              /* Target vPE */
    BOOLEAN DoorbellEnabled;        /* Doorbell interrupt enabled */
    BOOLEAN Enabled;                /* VLPI enabled */
} HALP_GIC_VLPI_ENTRY, *PHALP_GIC_VLPI_ENTRY;

/*
 * ============================================================================
 * ITS Node Structure - One per ITS in the system
 * ============================================================================
 *
 * Following Linux's struct its_node in drivers/irqchip/irq-gic-v3-its.c
 * Each ITS node maintains:
 * - Command queue for issuing ITS commands
 * - Device table (BASER[0]) mapping DeviceIDs to ITTs
 * - Collection table (BASER[1]) mapping CollectionIDs to redistributors
 * - ITT entry size from GITS_TYPER
 */
typedef struct _HALP_GIC_ITS_NODE
{
    /* Physical and virtual base addresses */
    PHYSICAL_ADDRESS PhysicalBase;
    ULONG_PTR VirtualBase;

    /* ITS identification */
    ULONG ItsId;                    /* From ACPI MADT */
    ULONG ListNumber;               /* Position in its_list_map (for GICv4 VMOVP) */

    /* Capabilities from GITS_TYPER */
    ULONGLONG Typer;
    ULONG DeviceIdBits;             /* Number of DeviceID bits (1-32) */
    ULONG EventIdBits;              /* Number of EventID bits (1-32) */
    ULONG IttEntrySize;             /* ITT entry size in bytes */
    ULONG MaxDeviceId;              /* Maximum supported DeviceID */
    BOOLEAN HasVlpis;               /* GITS_TYPER.VLPIS */
    BOOLEAN HasVmovp;               /* GITS_TYPER.VMOVP - single VMOVP capable */

    /* Command queue */
    PVOID CmdQueueBase;             /* Virtual address of command queue */
    PHYSICAL_ADDRESS CmdQueuePa;    /* Physical address for GITS_CBASER */
    ULONG CmdWriteIndex;            /* Next write position (entry index) */
    ULONG CmdQueueEntries;          /* Total entries in queue */
    BOOLEAN CmdQueueNeedsFlush;     /* Cache flush required for commands */
    KSPIN_LOCK CmdLock;             /* Lock for command queue access */

    /* Device table (BASER type 0x1) */
    PVOID DeviceTableBase;
    PHYSICAL_ADDRESS DeviceTablePa;
    SIZE_T DeviceTableSize;
    ULONG DeviceTableEntries;
    ULONG DeviceTableEntrySize;
    BOOLEAN DeviceTableFlat;        /* TRUE = flat, FALSE = 2-level */
    PVOID DeviceTableRaw;           /* Raw allocation for freeing */

    /* Collection table (BASER type 0x4) */
    PVOID CollectionTableBase;
    PHYSICAL_ADDRESS CollectionTablePa;
    SIZE_T CollectionTableSize;
    ULONG CollectionTableEntries;
    PVOID CollectionTableRaw;       /* Raw allocation for freeing */

    /* Collections (one per CPU) */
    BOOLEAN CollectionMapped[MAXIMUM_PROCESSORS];
    ULONGLONG CollectionTarget[MAXIMUM_PROCESSORS];

    /* Device tracking - hash table */
    struct _HALP_ARM64_ITS_DEVICE **DeviceBuckets;
    ULONG DeviceBucketCount;
    ULONG DeviceCount;
    KSPIN_LOCK DeviceLock;

    /* Initialization state */
    volatile LONG InitState;        /* 0=pending, 1=initializing, 2=done, -1=failed */
    BOOLEAN Enabled;

    /* GICv4 vPE Table (BASER type 0x2) */
    PVOID VpeTableBase;
    PHYSICAL_ADDRESS VpeTablePa;
    SIZE_T VpeTableSize;
    ULONG VpeTableEntries;
    PVOID VpeTableRaw;              /* Raw allocation for freeing */
    BOOLEAN VpeTableIndirect;       /* TRUE if using 2-level table */

    /* GICv4.1 specific */
    ULONG VlpiRedistOffset;         /* Offset to VLPI redistributor frame */
    BOOLEAN IsGicv4_1;              /* TRUE if GICv4.1 capable */
} HALP_GIC_ITS_NODE, *PHALP_GIC_ITS_NODE;

/*
 * ============================================================================
 * ITS Device Tracking Structure
 * ============================================================================
 *
 * Following Linux's struct its_device in drivers/irqchip/irq-gic-v3-its.c
 * Each device (identified by RequesterId/DeviceID) has:
 * - An ITT (Interrupt Translation Table) mapping EventIDs to LPIs
 * - A reference to the ITS node it's attached to
 */
typedef struct _HALP_ARM64_ITS_DEVICE
{
    struct _HALP_ARM64_ITS_DEVICE *Next;  /* Hash chain */
    PHALP_GIC_ITS_NODE ItsNode;           /* Owning ITS node */
    USHORT RequesterId;                   /* PCI BDF as DeviceID */
    volatile LONG InitState;              /* 0=pending, 1=init, 2=done, -1=fail */

    /* ITT (Interrupt Translation Table) */
    ULONG IttEntries;                     /* Number of entries (power of 2) */
    PVOID IttVa;                          /* Virtual address */
    PHYSICAL_ADDRESS IttPa;               /* Physical address for MAPD */
    SIZE_T IttSize;                       /* Size in bytes */

    /* Event ID tracking */
    ULONG AllocatedEvents;                /* Number of events allocated */
    ULONG MaxEvents;                      /* Maximum events for this device */

    /* Per-event to LPI mapping */
    PULONG EventToLpi;                    /* EventID -> LPI mapping array */
    PULONG EventToCollection;             /* EventID -> Collection mapping */
} HALP_ARM64_ITS_DEVICE, *PHALP_ARM64_ITS_DEVICE;

/*
 * ============================================================================
 * LPI Allocator Structure
 * ============================================================================
 *
 * Dynamic LPI allocation using a bitmap, following Linux's approach.
 * LPIs start at 8192 and can extend to 8192 + LPI_COUNT - 1.
 */
typedef struct _HALP_LPI_ALLOCATOR
{
    RTL_BITMAP Bitmap;
    PULONG BitmapBuffer;
    ULONG TotalLpis;
    ULONG AllocatedLpis;
    KSPIN_LOCK Lock;
} HALP_LPI_ALLOCATOR, *PHALP_LPI_ALLOCATOR;

/*
 * ============================================================================
 * MSI Allocation Result Structure
 * ============================================================================
 */
typedef struct _HALP_MSI_INFO
{
    ULONG Lpi;                      /* Allocated LPI number (INTID) */
    PHYSICAL_ADDRESS MsiAddress;    /* GITS_TRANSLATER address to write to */
    ULONG MsiData;                  /* EventID value to write */
    PHALP_ARM64_ITS_DEVICE Device;  /* Associated device */
    PHALP_GIC_ITS_NODE ItsNode;     /* ITS node handling this MSI */
    ULONG EventId;                  /* Event ID within device */
    ULONG CollectionId;             /* Target CPU/collection */
} HALP_MSI_INFO, *PHALP_MSI_INFO;

/*
 * ============================================================================
 * GICv3 Redistributor Region Tracking (Multi-Region Support)
 * ============================================================================
 *
 * ARM64 systems can have multiple GICR (redistributor) regions in ACPI MADT:
 * - Multi-socket systems may have separate regions per socket
 * - Non-contiguous memory layouts require multiple region tracking
 * - Windows 11 ARM64 properly parses ALL GICR entries from MADT
 *
 * This structure tracks each redistributor region discovered from ACPI.
 * Following Linux's approach in drivers/irqchip/irq-gic-v3.c:
 *   struct redist_region {
 *       void __iomem *redist_base;
 *       phys_addr_t phys_base;
 *       bool single_redist;
 *   };
 */

/*
 * Maximum number of redistributor regions supported.
 * Linux uses dynamic allocation; we use a fixed array for simplicity.
 * 8 regions should be sufficient for most multi-socket ARM64 systems.
 */
#define HALP_GIC_MAX_REDIST_REGIONS     8

/*
 * GICR_TYPER bit definitions for redistributor discovery
 */
#define HALP_GICR_TYPER_PLPIS           (1ULL << 0)   /* Physical LPIs supported */
#define HALP_GICR_TYPER_VLPIS           (1ULL << 1)   /* Virtual LPIs supported */
#define HALP_GICR_TYPER_DIRTY           (1ULL << 2)   /* GICR_VPENDBASER.Dirty supported */
#define HALP_GICR_TYPER_DIRECTLPI       (1ULL << 3)   /* Direct LPI injection */
#define HALP_GICR_TYPER_LAST            (1ULL << 4)   /* Last redistributor in region */
#define HALP_GICR_TYPER_RVPEID          (1ULL << 7)   /* RVPEID support */
#define HALP_GICR_TYPER_AFFINITY_MASK   0xFFFFFFFF00000000ULL  /* Affinity in bits [63:32] */
#define HALP_GICR_TYPER_AFFINITY_SHIFT  32

/*
 * Redistributor frame sizes (following Linux's gic_data.redist_stride logic):
 * - Without VLPI: 64KB RD_base + 64KB SGI_base = 128KB
 * - With VLPI:    64KB RD_base + 64KB SGI_base + 64KB VLPI_base + 64KB reserved = 256KB
 */
#define HALP_GICR_FRAME_SIZE_BASE       (64 * 1024)   /* 64KB per frame */
#define HALP_GICR_STRIDE_NO_VLPI        (2 * HALP_GICR_FRAME_SIZE_BASE)   /* 128KB */
#define HALP_GICR_STRIDE_WITH_VLPI      (4 * HALP_GICR_FRAME_SIZE_BASE)   /* 256KB */

/*
 * HALP_GIC_REDIST_REGION - Describes a single redistributor memory region
 *
 * Following Linux's redist_region structure, each region contains one or more
 * redistributor frames. The frames within a region are contiguous in memory.
 */
typedef struct _HALP_GIC_REDIST_REGION
{
    PHYSICAL_ADDRESS PhysicalBase;  /* Physical base address of region */
    ULONG_PTR VirtualBase;          /* Virtual address (after mapping) */
    ULONG Length;                   /* Region length in bytes */
    BOOLEAN SingleRedist;           /* TRUE if from GICC entry (single CPU) */
    BOOLEAN Mapped;                 /* TRUE if VirtualBase is valid */
} HALP_GIC_REDIST_REGION, *PHALP_GIC_REDIST_REGION;

/*
 * Global redistributor region tracking arrays
 */
extern HALP_GIC_REDIST_REGION HalpGicRedistRegions[HALP_GIC_MAX_REDIST_REGIONS];
extern ULONG HalpGicRedistRegionCount;

/*
 * Redistributor stride (0 = auto-detect from GICR_TYPER.VLPIS)
 * If non-zero, use this fixed stride for all regions
 */
extern ULONG HalpGicRedistStride;

/*
 * VLPI and DirectLPI capability flags (discovered during redistributor scan)
 */
extern BOOLEAN HalpGicRedistHasVlpis;
extern BOOLEAN HalpGicRedistHasDirectLpi;

/*
 * ============================================================================
 * Global State Variables (exported from gic_common.c / gic_init.c)
 * ============================================================================
 */

/* GIC base addresses */
extern ULONGLONG HalpGicdBase;
extern ULONGLONG HalpGiccBase;
extern ULONGLONG HalpGicrRegionBase;      /* Legacy: first region base (for compatibility) */
extern ULONGLONG HalpGicrRegionLength;    /* Legacy: first region length */
extern ULONG HalpGicrStride;              /* Legacy: redistributor stride */
extern ULONG HalpGicrSgiOffset;
extern ULONG_PTR HalpGicrCpuBase[MAXIMUM_PROCESSORS];

/* GIC ITS state - legacy single ITS (for backward compatibility) */
extern ULONGLONG HalpGicItsBase;
extern ULONG HalpGicItsId;
extern BOOLEAN HalpGicItsPresent;
extern BOOLEAN HalpGicItsEnabled;
extern KSPIN_LOCK HalpGicItsLock;

/* Multi-ITS support (following Linux's its_nodes list) */
extern HALP_GIC_ITS_NODE HalpGicItsNodes[HALP_GIC_MAX_ITS_NODES];
extern ULONG HalpGicItsNodeCount;
extern ULONG HalpGicItsListMap;         /* Bitmap of used ITS list numbers */

/* LPI allocator */
extern HALP_LPI_ALLOCATOR HalpGicLpiAllocator;

/* GIC MSI (v2m) state */
extern ULONGLONG HalpGicMsiFrameBase;
extern USHORT HalpGicMsiSpiBase;
extern USHORT HalpGicMsiSpiCount;
extern ULONG HalpGicMsiFlags;
extern BOOLEAN HalpGicMsiPresent;

/* GIC detection state */
extern BOOLEAN HalpGicUseSysRegs;
extern BOOLEAN HalpForceSysRegs;
extern BOOLEAN HalpForceLegacyGic;
extern ULONG HalpGicArchRev;
extern BOOLEAN HalpGicParsedMadt;
extern BOOLEAN HalpGicInterfaceSelected;
extern BOOLEAN HalpGiccPresent;

/* GIC version and capability information */
extern HALP_GIC_VERSION_INFO HalpGicVersionInfo;

/*
 * GICv4 VLPI / vPE state
 */
extern BOOLEAN HalpGicHasVlpis;             /* System supports VLPIs */
extern BOOLEAN HalpGicHasDirectLpi;         /* System supports direct LPI injection */
extern BOOLEAN HalpGicIsGicv4_1;            /* System is GICv4.1 capable */

/* vPE ID allocator */
extern RTL_BITMAP HalpGicVpeidBitmap;
extern PULONG HalpGicVpeidBitmapBuffer;
extern ULONG HalpGicVpeidCount;
extern ULONG HalpGicVpeidAllocated;
extern KSPIN_LOCK HalpGicVpeidLock;

/* VM tracking */
extern PHALP_GIC_VM HalpGicVmList;
extern ULONG HalpGicVmCount;
extern KSPIN_LOCK HalpGicVmLock;

/* vPE tracking (indexed by vPE ID) */
extern PHALP_GIC_VPE *HalpGicVpeTable;
extern ULONG HalpGicVpeTableSize;

/* LPI state */
extern ULONG HalpGicLpiCount;
extern UCHAR *HalpGicLpiConfig;
extern PHYSICAL_ADDRESS HalpGicLpiConfigPa;
extern SIZE_T HalpGicLpiConfigSize;
extern UCHAR *HalpGicLpiPending[MAXIMUM_PROCESSORS];
extern PHYSICAL_ADDRESS HalpGicLpiPendingPa[MAXIMUM_PROCESSORS];

/* ITS state */
extern BOOLEAN HalpGicItsInitialized;
extern BOOLEAN HalpGicItsInitFailed;
extern volatile LONG HalpGicItsInitState;
extern ULONG HalpGicItsDeviceIdBits;
extern ULONG HalpGicItsEventIdBits;
extern ULONG HalpGicItsEventIdLimit;
extern ULONG HalpGicItsLpiIdBits;
extern ULONG HalpGicItsIttEntrySize;
extern ULONG HalpGicItsDeviceTableEntries;
extern ULONG HalpGicItsCollectionEntries;
extern PHALP_ARM64_ITS_DEVICE *HalpGicItsDeviceBuckets;
extern ULONG HalpGicItsDeviceBucketCount;
extern BOOLEAN HalpGicItsCollectionMapped[MAXIMUM_PROCESSORS];

/* Active interrupt tracking */
extern ULONG HalpArm64ActiveIntId[MAXIMUM_PROCESSORS];

/* Default interrupt affinity */
extern KAFFINITY HalpDefaultInterruptAffinity;

/* Memory mapping mode */
extern BOOLEAN HalpUseIdentityMapping;

/*
 * ============================================================================
 * IRQ Affinity Tracking State (GICv3 Dynamic Affinity Routing)
 * ============================================================================
 */

/* Maximum number of SPIs we track (INTIDs 32-1019 = 988 SPIs) */
#define HALP_GIC_MAX_SPI_COUNT 988

/* Array tracking target CPU for each SPI (indexed by INTID - 32) */
extern ULONG HalpGicSpiAffinityTarget[HALP_GIC_MAX_SPI_COUNT];

/* Array storing MPIDR values for each CPU (indexed by CPU number) */
extern ULONGLONG HalpGicCpuMpidr[MAXIMUM_PROCESSORS];

/* Spinlock protecting affinity updates */
extern KSPIN_LOCK HalpGicAffinityLock;

/* Flag indicating if affinity tracking is initialized */
extern BOOLEAN HalpGicAffinityInitialized;

/* Number of online CPUs (for round-robin distribution) */
extern volatile LONG HalpGicOnlineCpuCount;

/*
 * ============================================================================
 * MMIO Helper Functions (defined in gic_common.c)
 * ============================================================================
 */

FORCEINLINE
volatile ULONG *
HalpMmio(
    _In_ ULONG_PTR Base,
    _In_ ULONG Offset)
{
    return (volatile ULONG *)(Base + Offset);
}

FORCEINLINE
ULONGLONG
HalpMmioRead64(
    _In_ ULONG_PTR Base,
    _In_ ULONG Offset)
{
    volatile ULONG *Ptr = HalpMmio(Base, Offset);
    ULONGLONG Low = Ptr[0];
    ULONGLONG High = Ptr[1];
    return Low | (High << 32);
}

FORCEINLINE
VOID
HalpMmioWrite64(
    _In_ ULONG_PTR Base,
    _In_ ULONG Offset,
    _In_ ULONGLONG Value)
{
    volatile ULONG *Ptr = HalpMmio(Base, Offset);
    Ptr[0] = (ULONG)(Value & 0xFFFFFFFFu);
    Ptr[1] = (ULONG)(Value >> 32);
}

/*
 * ============================================================================
 * GIC Base Address Helper Functions (defined in gic_common.c)
 * ============================================================================
 */

FORCEINLINE
ULONG_PTR
HalpGicrBase(
    _In_ ULONG Cpu)
{
    if (Cpu < RTL_NUMBER_OF(HalpGicrCpuBase) && HalpGicrCpuBase[Cpu])
        return HalpGicrCpuBase[Cpu];

    return (ULONG_PTR)(HalpGicrRegionBase + ((ULONG_PTR)Cpu * HalpGicrStride));
}

FORCEINLINE
ULONG_PTR
HalpGicrSgiBase(
    _In_ ULONG Cpu)
{
    return HalpGicrBase(Cpu) + HalpGicrSgiOffset;
}

/*
 * ============================================================================
 * ARM64 System Register Helpers (defined in gic_common.c / gicv3.c)
 * ============================================================================
 */

FORCEINLINE
ULONGLONG
HalpReadMidr(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, midr_el1" : "=r"(v));
    return v;
}

FORCEINLINE
ULONGLONG
HalpReadMpidr(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(v));
    return v;
}

FORCEINLINE
ULONGLONG
HalpReadPfr0(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(v));
    return v;
}

FORCEINLINE
ULONGLONG
HalpReadCntfrq(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

FORCEINLINE
ULONGLONG
HalpReadCntpct(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

/*
 * ============================================================================
 * GICv3 System Register Accessors (defined in gicv3.c)
 * ============================================================================
 */

FORCEINLINE
unsigned int
HalpReadIccSre(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, icc_sre_el1" : "=r"(v));
    return (unsigned int)v;
}

FORCEINLINE
VOID
HalpWriteIccSre(
    _In_ unsigned int v)
{
    __asm__ __volatile__("msr icc_sre_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE
unsigned int
HalpReadIccIar1(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, icc_iar1_el1" : "=r"(v));
    return (unsigned int)(v & 0xFFFFFFu);
}

FORCEINLINE
VOID
HalpWriteIccEoir1(
    _In_ unsigned int id)
{
    __asm__ __volatile__("msr icc_eoir1_el1, %0; isb" :: "r"((ULONGLONG)id) : "memory");
}

FORCEINLINE
VOID
HalpWriteIccPmr(
    _In_ unsigned int v)
{
    __asm__ __volatile__("msr icc_pmr_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE
VOID
HalpWriteIccBpr1(
    _In_ unsigned int v)
{
    __asm__ __volatile__("msr icc_bpr1_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE
VOID
HalpWriteIccIgrpen1(
    _In_ unsigned int v)
{
    __asm__ __volatile__("msr icc_igrpen1_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE
unsigned int
HalpReadIccPmr(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, icc_pmr_el1" : "=r"(v));
    return (unsigned int)v;
}

FORCEINLINE
unsigned int
HalpReadIccIgrpen1(VOID)
{
    ULONGLONG v;
    __asm__ __volatile__("mrs %0, icc_igrpen1_el1" : "=r"(v));
    return (unsigned int)v;
}

FORCEINLINE
VOID
HalpWriteIccSgi1r(
    _In_ ULONGLONG v)
{
    __asm__ __volatile__("msr icc_sgi1r_el1, %0; isb" :: "r"(v) : "memory");
}

/*
 * ============================================================================
 * MPIDR / Affinity Helpers (defined in gic_common.c)
 * ============================================================================
 */

FORCEINLINE
ULONG
HalpArm64AffinityFromMpidr(
    _In_ ULONGLONG Mpidr)
{
    ULONG Aff0 = (ULONG)(Mpidr & 0xFF);
    ULONG Aff1 = (ULONG)((Mpidr >> 8) & 0xFF);
    ULONG Aff2 = (ULONG)((Mpidr >> 16) & 0xFF);
    ULONG Aff3 = (ULONG)((Mpidr >> 32) & 0xFF);
    return Aff0 | (Aff1 << 8) | (Aff2 << 16) | (Aff3 << 24);
}

FORCEINLINE
ULONGLONG
HalpArm64IrouterFromMpidr(
    _In_ ULONGLONG Mpidr)
{
    ULONG Aff = HalpArm64AffinityFromMpidr(Mpidr);
    ULONGLONG Aff0 = (ULONGLONG)(Aff & 0xFF);
    ULONGLONG Aff1 = (ULONGLONG)((Aff >> 8) & 0xFF);
    ULONGLONG Aff2 = (ULONGLONG)((Aff >> 16) & 0xFF);
    ULONGLONG Aff3 = (ULONGLONG)((Aff >> 24) & 0xFF);
    return (Aff0) | (Aff1 << 8) | (Aff2 << 16) | (Aff3 << 32);
}

/*
 * ============================================================================
 * Cache Maintenance Helpers (defined in gic_common.c)
 * ============================================================================
 */

ULONG
HalpArm64GetCacheLineSize(VOID);

VOID
HalpArm64CleanDcacheRange(
    _In_ PVOID Base,
    _In_ SIZE_T Length);

VOID
HalpArm64CleanInvalidateDcacheRange(
    _In_ PVOID Base,
    _In_ SIZE_T Length);

/*
 * ============================================================================
 * GIC Initialization Functions (defined in gic_init.c)
 * ============================================================================
 */

VOID
HalpArm64DiscoverGicFromMadt(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
HalpArm64SelectGicInterface(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
HalpInitGicDistributor(
    _In_ ULONG GicVersion,
    _In_ BOOLEAN UseSysRegs);

/*
 * ============================================================================
 * GICv2 Functions (defined in gicv2.c)
 * ============================================================================
 */

VOID
HalpInitGicv2CpuInterface(VOID);

VOID
HalpInitGicv2SgiPpi(VOID);

ULONG
HalpGicv2AcknowledgeInterrupt(VOID);

VOID
HalpGicv2EndInterrupt(
    _In_ ULONG IntId);

VOID
HalpInitGicv2SpiTargets(
    _In_ ULONG Lines);

/*
 * ============================================================================
 * GICv3 Functions (defined in gicv3.c)
 * ============================================================================
 */

VOID
HalpInitGicv3CpuInterface(VOID);

VOID
HalpInitGicv3SpiRouting(
    _In_ ULONG Lines);

ULONG
HalpGicv3AcknowledgeInterrupt(VOID);

VOID
HalpGicv3EndInterrupt(
    _In_ ULONG IntId);

VOID
HalpGicv3SendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId);

/*
 * ============================================================================
 * GICv3 Dynamic Affinity Routing Functions (defined in gicv3.c)
 * ============================================================================
 */

/*
 * HalpGicv3InitAffinityTracking - Initialize affinity tracking state
 *
 * Initializes the affinity tracking structures including:
 * - Spinlock for thread-safe updates
 * - CPU MPIDR array
 * - Default affinity for all SPIs (round-robin across CPUs)
 *
 * Called during GIC initialization.
 */
VOID
HalpGicv3InitAffinityTracking(VOID);

/*
 * HalpGicv3RegisterCpu - Register a CPU's MPIDR for affinity routing
 *
 * Stores the MPIDR value for a CPU so it can be used when setting
 * interrupt affinity. Should be called when each CPU comes online.
 *
 * Parameters:
 *   CpuIndex - Logical CPU index
 *   Mpidr    - MPIDR value for the CPU
 */
VOID
HalpGicv3RegisterCpu(
    _In_ ULONG CpuIndex,
    _In_ ULONGLONG Mpidr);

/*
 * HalpArm64SetGicAffinity - Set affinity for a GIC interrupt
 *
 * Routes an SPI to the specified target CPU by programming GICD_IROUTER.
 * This is the main function for dynamic IRQ affinity routing.
 *
 * Parameters:
 *   InterruptId - GIC interrupt ID (must be SPI: 32-1019)
 *   TargetCpu   - Target CPU index
 *
 * Returns:
 *   STATUS_SUCCESS on success
 *   STATUS_INVALID_PARAMETER if InterruptId is not an SPI
 *   STATUS_INVALID_PARAMETER if TargetCpu is out of range or not online
 */
NTSTATUS
HalpArm64SetGicAffinity(
    _In_ ULONG InterruptId,
    _In_ ULONG TargetCpu);

/*
 * HalpArm64GetGicAffinity - Get current affinity for a GIC interrupt
 *
 * Returns the target CPU for an SPI.
 *
 * Parameters:
 *   InterruptId - GIC interrupt ID (must be SPI: 32-1019)
 *
 * Returns:
 *   Target CPU index, or (ULONG)-1 if invalid interrupt ID
 */
ULONG
HalpArm64GetGicAffinity(
    _In_ ULONG InterruptId);

/*
 * HalpGicv3MigrateCpuIrqs - Migrate IRQs away from a CPU
 *
 * Called when a CPU goes offline. Migrates all SPIs currently
 * routed to the specified CPU to CPU 0.
 *
 * Parameters:
 *   CpuIndex - CPU that is going offline
 */
VOID
HalpGicv3MigrateCpuIrqs(
    _In_ ULONG CpuIndex);

/*
 * HalpGicv3SetSpiAffinityRoundRobin - Distribute SPIs across CPUs
 *
 * Distributes all SPIs across available CPUs using round-robin
 * for load balancing.
 *
 * Parameters:
 *   Lines - Total number of interrupt lines in the GIC
 */
VOID
HalpGicv3SetSpiAffinityRoundRobin(
    _In_ ULONG Lines);

/*
 * ============================================================================
 * Redistributor Functions (defined in gic_redist.c)
 * ============================================================================
 */

/*
 * HalpArm64InitRedistRegions - Initialize redistributor region array
 *
 * Called during GIC initialization to populate the HalpGicRedistRegions
 * array from ACPI MADT GICR entries. This must be called before any
 * redistributor discovery or initialization.
 */
VOID
HalpArm64InitRedistRegions(VOID);

/*
 * HalpArm64MapRedistRegions - Map redistributor regions into virtual memory
 *
 * Maps all discovered redistributor regions. Called after MM is available.
 */
VOID
HalpArm64MapRedistRegions(VOID);

/*
 * HalpArm64GetRedistStrideForTyper - Calculate redistributor stride from TYPER
 *
 * Given a GICR_TYPER value, returns the stride to the next redistributor:
 * - 128KB (2 * 64KB) for standard redistributors
 * - 256KB (4 * 64KB) for redistributors with VLPI support
 *
 * This follows Linux's gic_iterate_rdists() logic.
 *
 * Parameters:
 *   Typer - GICR_TYPER register value
 *
 * Returns:
 *   Stride in bytes to next redistributor frame
 */
ULONG
HalpArm64GetRedistStrideForTyper(
    _In_ ULONGLONG Typer);

/*
 * HalpArm64FindGicrForMpidr - Find the Redistributor for a specific CPU
 *
 * Searches ALL redistributor regions to find the Redistributor frame
 * matching the specified MPIDR value. This is the multi-region version
 * that properly handles:
 * - Multiple GICR memory regions (multi-socket systems)
 * - Non-contiguous redistributor memory layouts
 * - Variable stride based on VLPI support
 * - GICR_TYPER.Last bit for region boundary detection
 *
 * Parameters:
 *   Mpidr - The MPIDR value of the target CPU
 *
 * Returns:
 *   Base address of the matching Redistributor, or 0 if not found.
 */
ULONG_PTR
HalpArm64FindGicrForMpidr(
    _In_ ULONGLONG Mpidr);

/*
 * HalpArm64FindGicrInRegion - Find redistributor in a specific region
 *
 * Helper function that searches a single region for a matching redistributor.
 *
 * Parameters:
 *   Region    - Pointer to the redistributor region to search
 *   Mpidr     - The MPIDR value to match
 *   OutOffset - If found, receives the offset within the region
 *
 * Returns:
 *   TRUE if found, FALSE otherwise
 */
BOOLEAN
HalpArm64FindGicrInRegion(
    _In_ PHALP_GIC_REDIST_REGION Region,
    _In_ ULONGLONG Mpidr,
    _Out_opt_ PULONG OutOffset);

/*
 * HalpInitGicRedistributor - Initialize the Redistributor for a CPU
 *
 * Performs redistributor wake-up and SGI/PPI bank configuration.
 * Uses multi-region discovery if CPU's redistributor base is not cached.
 *
 * Parameters:
 *   Cpu - The processor number to initialize
 */
VOID
HalpInitGicRedistributor(
    _In_ ULONG Cpu);

/*
 * HalpArm64EnableCpuInterface - Enable the CPU interface for the current CPU
 */
VOID
HalpArm64EnableCpuInterface(VOID);

/*
 * ============================================================================
 * ITS Functions (defined in gic_its.c)
 * ============================================================================
 */

/*
 * Multi-ITS node management
 */
BOOLEAN
HalpGicItsInitAllNodes(VOID);

BOOLEAN
HalpGicItsProbeNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode);

PHALP_GIC_ITS_NODE
HalpGicItsSelectNodeForDevice(
    _In_ ULONG DeviceId);

/*
 * Legacy single-ITS initialization (backward compatibility)
 */
BOOLEAN
HalpGicItsInitialize(VOID);

/*
 * LPI allocator functions
 */
BOOLEAN
HalpGicItsInitLpiAllocator(VOID);

ULONG
HalpGicItsAllocateLpi(
    _In_ ULONG Count);

VOID
HalpGicItsFreeLpi(
    _In_ ULONG LpiBase,
    _In_ ULONG Count);

/*
 * Per-CPU LPI table programming
 */
BOOLEAN
HalpGicItsProgramCpuTables(
    _In_ ULONG Cpu);

BOOLEAN
HalpGicItsEnsureCollection(
    _In_ ULONG Cpu);

BOOLEAN
HalpGicItsEnsureCollectionOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG Cpu);

/*
 * Device management
 */
PHALP_ARM64_ITS_DEVICE
HalpGicItsGetDevice(
    _In_ USHORT RequesterId);

PHALP_ARM64_ITS_DEVICE
HalpGicItsGetDeviceOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ USHORT RequesterId);

PHALP_ARM64_ITS_DEVICE
HalpGicItsCreateDevice(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG NrEvents);

VOID
HalpGicItsFreeDevice(
    _Inout_ PHALP_ARM64_ITS_DEVICE Device);

/*
 * LPI configuration
 */
VOID
HalpGicItsEnableLpi(
    _In_ ULONG Lpi);

VOID
HalpGicItsDisableLpi(
    _In_ ULONG Lpi);

VOID
HalpGicItsSetLpiPriority(
    _In_ ULONG Lpi,
    _In_ UCHAR Priority);

/*
 * ITS command functions - Single ITS (legacy)
 */
BOOLEAN
HalpGicItsSendMapd(
    _In_ ULONG DeviceId,
    _In_ ULONG IttEntries,
    _In_ ULONGLONG IttPa);

BOOLEAN
HalpGicItsSendMapc(
    _In_ ULONG CollectionId,
    _In_ ULONGLONG TargetAddress);

BOOLEAN
HalpGicItsSendMapti(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG PhysId,
    _In_ ULONG CollectionId,
    _In_ ULONGLONG TargetAddress);

/*
 * ITS command functions - Multi-ITS (new)
 */
BOOLEAN
HalpGicItsSendMapdOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG IttEntries,
    _In_ ULONGLONG IttPa,
    _In_ BOOLEAN Valid);

BOOLEAN
HalpGicItsSendMapcOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG CollectionId,
    _In_ ULONGLONG TargetAddress,
    _In_ BOOLEAN Valid);

BOOLEAN
HalpGicItsSendMaptiOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG PhysId,
    _In_ ULONG CollectionId);

BOOLEAN
HalpGicItsSendInvOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId);

BOOLEAN
HalpGicItsSendDiscardOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId);

BOOLEAN
HalpGicItsSendSyncOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONGLONG TargetAddress);

/*
 * MSI allocation API
 */
NTSTATUS
HalpGicItsAllocateMsi(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG TargetCpu,
    _Out_ PULONG Lpi,
    _Out_ PPHYSICAL_ADDRESS MsiAddress,
    _Out_ PULONG MsiData);

NTSTATUS
HalpGicItsFreeMsi(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId);

NTSTATUS
HalpGicItsGetMsiInfo(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _Out_ PHALP_MSI_INFO MsiInfo);

/*
 * MSI affinity management
 */
NTSTATUS
HalpGicItsSetMsiAffinity(
    _In_ ULONG Lpi,
    _In_ ULONG TargetCpu);

/*
 * ============================================================================
 * GIC Version Detection Functions (defined in gic_init.c)
 * ============================================================================
 */

/*
 * HalpGicDetectVersion - Detect GIC version and capabilities
 *
 * Reads GICD_PIDR2, GICD_TYPER, and GICR_TYPER to populate
 * the HalpGicVersionInfo structure with detected capabilities.
 *
 * Must be called after GIC base addresses are known (after MADT parsing).
 */
VOID
HalpGicDetectVersion(VOID);

/*
 * HalpGicHasVlpiSupport - Check if VLPI support is available
 *
 * Returns TRUE if all required components for GICv4 VLPIs are present:
 * - GICR_TYPER.VLPIS set on all redistributors
 * - GITS_TYPER.Virtual set on at least one ITS
 */
BOOLEAN
HalpGicHasVlpiSupport(VOID);

/*
 * HalpGicHasExtendedSpi - Check if extended SPI range is available
 *
 * Returns TRUE if GICv3.1+ extended SPI range (up to 1020 SPIs) is supported.
 */
BOOLEAN
HalpGicHasExtendedSpi(VOID);

/*
 * ============================================================================
 * GICv4 vPE Management Functions (defined in gic_its.c)
 * ============================================================================
 */

/*
 * HalpGicItsInitVpeSupport - Initialize GICv4 vPE support
 *
 * Allocates vPE ID bitmap and initializes vPE tracking structures.
 * Must be called after ITS initialization if GICv4 is supported.
 *
 * Returns TRUE on success, FALSE if vPE support is not available.
 */
BOOLEAN
HalpGicItsInitVpeSupport(VOID);

/*
 * HalpGicItsAllocateVpe - Allocate a vPE for a virtual processor
 *
 * Allocates a vPE ID, creates the Virtual Pending Table, and
 * maps the vPE on all ITS nodes via VMAPP commands.
 *
 * Parameters:
 *   VmId     - Virtual Machine ID
 *   VpIndex  - Virtual Processor index within VM
 *   VpeOut   - Receives pointer to allocated vPE structure
 *
 * Returns:
 *   STATUS_SUCCESS on success
 *   STATUS_INSUFFICIENT_RESOURCES if out of vPE IDs or memory
 *   STATUS_NOT_SUPPORTED if GICv4 is not available
 */
NTSTATUS
HalpGicItsAllocateVpe(
    _In_ ULONG VmId,
    _In_ ULONG VpIndex,
    _Out_ PHALP_GIC_VPE *VpeOut);

/*
 * HalpGicItsFreeVpe - Free a vPE
 *
 * Unmaps the vPE from all ITS nodes and frees resources.
 *
 * Parameters:
 *   Vpe - Pointer to vPE to free
 */
VOID
HalpGicItsFreeVpe(
    _In_ PHALP_GIC_VPE Vpe);

/*
 * HalpGicItsScheduleVpe - Schedule a vPE on a CPU
 *
 * Makes a vPE resident on the specified CPU by programming
 * GICR_VPENDBASER. VLPIs for this vPE will be delivered
 * to the CPU after this call.
 *
 * Parameters:
 *   Vpe      - Pointer to vPE to schedule
 *   TargetCpu - CPU to schedule the vPE on
 *
 * Returns:
 *   STATUS_SUCCESS on success
 *   STATUS_INVALID_PARAMETER if parameters are invalid
 */
NTSTATUS
HalpGicItsScheduleVpe(
    _In_ PHALP_GIC_VPE Vpe,
    _In_ ULONG TargetCpu);

/*
 * HalpGicItsDescheduleVpe - Deschedule a vPE from its current CPU
 *
 * Makes a vPE non-resident. A doorbell interrupt will be
 * generated if there are pending VLPIs for this vPE.
 *
 * Parameters:
 *   Vpe - Pointer to vPE to deschedule
 *
 * Returns:
 *   STATUS_SUCCESS on success
 */
NTSTATUS
HalpGicItsDescheduleVpe(
    _In_ PHALP_GIC_VPE Vpe);

/*
 * ============================================================================
 * GICv4 VLPI Functions (defined in gic_its.c)
 * ============================================================================
 */

/*
 * HalpGicItsMapVlpi - Map a virtual interrupt to an LPI
 *
 * Creates a VLPI mapping using the VMAPTI command. The virtual
 * interrupt will be delivered to the vPE when the device triggers
 * the corresponding event.
 *
 * Parameters:
 *   Vpe       - Target vPE
 *   DeviceId  - Device ID
 *   EventId   - Event ID within device
 *   VirtIntId - Virtual interrupt ID within VM
 *   Enabled   - TRUE to enable the VLPI immediately
 *
 * Returns:
 *   STATUS_SUCCESS on success
 */
NTSTATUS
HalpGicItsMapVlpi(
    _In_ PHALP_GIC_VPE Vpe,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ ULONG VirtIntId,
    _In_ BOOLEAN Enabled);

/*
 * HalpGicItsUnmapVlpi - Unmap a virtual interrupt
 *
 * Removes a VLPI mapping and frees associated resources.
 *
 * Parameters:
 *   Vpe      - Target vPE
 *   DeviceId - Device ID
 *   EventId  - Event ID within device
 *
 * Returns:
 *   STATUS_SUCCESS on success
 */
NTSTATUS
HalpGicItsUnmapVlpi(
    _In_ PHALP_GIC_VPE Vpe,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId);

/*
 * HalpGicItsMoveVlpi - Move a VLPI to a different vPE
 *
 * Uses VMOVI command to move a virtual interrupt to a different vPE.
 *
 * Parameters:
 *   DeviceId  - Device ID
 *   EventId   - Event ID within device
 *   TargetVpe - New target vPE
 *
 * Returns:
 *   STATUS_SUCCESS on success
 */
NTSTATUS
HalpGicItsMoveVlpi(
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ PHALP_GIC_VPE TargetVpe);

/*
 * HalpGicItsGetVpeId - Get the vPE ID from a vPE pointer
 *
 * Returns the vPE ID for use by callers that don't have access to the
 * full HALP_GIC_VPE structure definition.
 */
ULONG
HalpGicItsGetVpeId(
    _In_ PHALP_GIC_VPE Vpe);

/*
 * ============================================================================
 * GICv4 ITS Command Functions (defined in gic_its.c)
 * ============================================================================
 */

/*
 * HalpGicItsSendVmappOnNode - Send VMAPP command to map vPE
 */
BOOLEAN
HalpGicItsSendVmappOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ PHALP_GIC_VPE Vpe,
    _In_ ULONG CollectionId,
    _In_ BOOLEAN Valid);

/*
 * HalpGicItsSendVmaptiOnNode - Send VMAPTI command to map virtual interrupt
 */
BOOLEAN
HalpGicItsSendVmaptiOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ PHALP_GIC_VPE Vpe,
    _In_ ULONG VirtIntId,
    _In_ BOOLEAN DoorbellEnabled);

/*
 * HalpGicItsSendVmoviOnNode - Send VMOVI command to move virtual interrupt
 */
BOOLEAN
HalpGicItsSendVmoviOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ ULONG DeviceId,
    _In_ ULONG EventId,
    _In_ PHALP_GIC_VPE Vpe,
    _In_ BOOLEAN DoorbellEnabled);

/*
 * HalpGicItsSendVmovpOnNode - Send VMOVP command to move vPE
 */
BOOLEAN
HalpGicItsSendVmovpOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ PHALP_GIC_VPE Vpe,
    _In_ ULONG CollectionId,
    _In_ USHORT SeqNum,
    _In_ USHORT ItsList);

/*
 * HalpGicItsSendVinvallOnNode - Send VINVALL command
 */
BOOLEAN
HalpGicItsSendVinvallOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ PHALP_GIC_VPE Vpe);

/*
 * HalpGicItsSendVsyncOnNode - Send VSYNC command
 */
BOOLEAN
HalpGicItsSendVsyncOnNode(
    _Inout_ PHALP_GIC_ITS_NODE ItsNode,
    _In_ PHALP_GIC_VPE Vpe);

/*
 * ============================================================================
 * Direct Injection Functions (defined in gic_redist.c)
 * ============================================================================
 */

/*
 * HalpGicrProgramVpropbaser - Program GICR_VPROPBASER for a CPU
 *
 * Sets up the virtual LPI property table for GICv4.
 *
 * Parameters:
 *   Cpu - Target CPU
 *
 * Returns:
 *   TRUE on success, FALSE on failure
 */
BOOLEAN
HalpGicrProgramVpropbaser(
    _In_ ULONG Cpu);

/*
 * HalpGicrProgramVpendbaser - Program GICR_VPENDBASER for vPE residency
 *
 * Makes a vPE resident on a CPU by programming GICR_VPENDBASER
 * with the vPE's pending table address.
 *
 * Parameters:
 *   Cpu - Target CPU
 *   Vpe - vPE to make resident (NULL to clear residency)
 *
 * Returns:
 *   TRUE on success, FALSE on failure
 */
BOOLEAN
HalpGicrProgramVpendbaser(
    _In_ ULONG Cpu,
    _In_opt_ PHALP_GIC_VPE Vpe);

/*
 * HalpGicrGetVlpiBase - Get VLPI frame base address for a CPU
 *
 * Returns the virtual address of the VLPI_base frame
 * (offset 0x20000 from redistributor base).
 *
 * Parameters:
 *   Cpu - CPU number
 *
 * Returns:
 *   VLPI frame base address, or 0 if not available
 */
ULONG_PTR
HalpGicrGetVlpiBase(
    _In_ ULONG Cpu);

/*
 * ============================================================================
 * Common GIC Functions (defined in gic_common.c)
 * ============================================================================
 */

VOID
NTAPI
HalpArm64ProgramGicTrigger(
    _In_ ULONG IntId,
    _In_ BOOLEAN EdgeTriggered);

VOID
HalpArm64SendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId);

ULONG
HalpGicAcknowledgeInterrupt(VOID);

VOID
HalpGicEndInterrupt(
    _In_ ULONG IntId);

VOID
HalpGicEnableInterrupt(
    _In_ ULONG IntId);

VOID
HalpGicDisableInterrupt(
    _In_ ULONG IntId);

VOID
HalpGicSetInterruptPriority(
    _In_ ULONG IntId,
    _In_ ULONG Priority);

/*
 * ============================================================================
 * Memory Allocation Helpers (defined in gic_its.c)
 * ============================================================================
 */

PVOID
HalpGicItsAllocAligned(
    _In_ SIZE_T Size,
    _In_ SIZE_T Alignment,
    _Out_ PPHYSICAL_ADDRESS Physical,
    _Out_opt_ PVOID *RawOut);

/*
 * ============================================================================
 * Memory Allocation Tag
 * ============================================================================
 */
#ifndef TAG_HAL
#define TAG_HAL     ' laH'
#endif

#endif /* _GIC_INTERNAL_H_ */
