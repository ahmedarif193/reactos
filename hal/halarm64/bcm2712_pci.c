/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2712 PCIe config-space backend
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Broadcom BCM2712 (Raspberry Pi 5) indirect PCI
 *              configuration space access via CFG_INDEX / CFG_DATA.
 *
 *              The BCM2712 does NOT use standard ECAM.  Each of its three
 *              PCIe root complexes uses a pair of MMIO registers:
 *
 *                 CFG_INDEX  (base + 0x9000)  — write Bus/Dev/Func here
 *                 CFG_DATA   (base + 0x8000)  — 4 KB window into the
 *                                               selected device's config
 *
 *              Bus 0 (root port) config is at the RC base directly.
 *
 *              Reference: WoR edk2-platforms, rpi5-dev branch
 *                Silicon/Broadcom/Bcm27xx/Library/
 *                  Bcm2712PciSegmentLib/PciSegmentLib.c
 *                Silicon/Broadcom/Bcm27xx/Include/IndustryStandard/
 *                  Bcm2712.h, Bcm2712Pcie.h
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * Include the same HAL headers as halarm64.c — this file is compiled
 * as part of the HAL, not as a standalone driver.
 */
#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <reactos/hal/acpi_pci.h>
#include <reactos/drivers/acpi/acpi.h>
#include <halacpi.h>
#include <hal.h>
#include "bcm2712_pci.h"

#define NDEBUG
#include <debug.h>

/* ACPI _PRT callback from halarm64.c — used for InterruptLine patching */
extern PHAL_ACPI_PCI_ROUTE_QUERY HalpArm64PciRouteQueryCallback;

/* ================================================================== */
/*  Per-controller runtime state                                      */
/* ================================================================== */

typedef struct _BCM2712_PCIE_RC
{
    PHYSICAL_ADDRESS PhysBase;   /* Controller physical address          */
    PVOID            VirtBase;   /* MmMapIoSpace mapping                 */
    BOOLEAN          Mapped;     /* TRUE after successful MmMapIoSpace   */
    BOOLEAN          LinkUp;     /* TRUE if PHY link is active           */
} BCM2712_PCIE_RC, *PBCM2712_PCIE_RC;

static BCM2712_PCIE_RC Bcm2712RcState[BCM2712_PCIE_RC_COUNT];
BOOLEAN                Bcm2712Detected  = FALSE;
static BOOLEAN         Bcm2712Initialized = FALSE;
static KSPIN_LOCK      Bcm2712ConfigLock;

/* Physical base addresses — indexed by segment number. */
static const ULONGLONG Bcm2712RcBases[BCM2712_PCIE_RC_COUNT] = {
    BCM2712_PCIE0_RC_BASE,
    BCM2712_PCIE1_RC_BASE,
    BCM2712_PCIE2_RC_BASE,
};

/* ================================================================== */
/*  Internal helpers                                                  */
/* ================================================================== */

/**
 * Read a 32-bit register from a mapped RC register block.
 */
FORCEINLINE
ULONG
Bcm2712Read32(
    _In_ PVOID RcBase,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)RcBase + Offset));
}

/**
 * Write a 32-bit register in a mapped RC register block.
 */
FORCEINLINE
VOID
Bcm2712Write32(
    _In_ PVOID RcBase,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)RcBase + Offset), Value);
}

/**
 * Check PCIe link status for one controller.
 */
static
BOOLEAN
Bcm2712IsLinkUp(
    _In_ PVOID RcBase)
{
    ULONG Status;

    Status = Bcm2712Read32(RcBase, PCIE_MISC_PCIE_STATUS);
    return (Status & (PCIE_MISC_PCIE_STATUS_PHYLINKUP |
                      PCIE_MISC_PCIE_STATUS_DL_ACTIVE)) != 0;
}

/**
 * Compute the MMIO address for a config space access.
 *
 * For bus 0 (root port):
 *   address = RcBase + Register
 *
 * For bus > 0 (downstream devices):
 *   1. Write (Bus << 20 | Dev << 15 | Func << 12) to CFG_INDEX
 *   2. address = RcBase + CFG_DATA + Register
 *
 * Returns NULL if the target is known-unreachable (e.g. bus 0 dev > 0,
 * or bus 1 dev > 0 — BCM2712 is a single-root-port bridge).
 */
static
PVOID
Bcm2712ComputeConfigAddress(
    _In_ PVOID RcBase,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Register)
{
    /*
     * Bus 0: only device 0, function 0 exists (the root port itself).
     * The config registers are at the RC base directly.
     */
    if (Bus == 0)
    {
        if (Device > 0 || Function > 0)
            return NULL;

        return (PVOID)((PUCHAR)RcBase + Register);
    }

    /*
     * Bus 1: only one device (the directly-attached endpoint, e.g. RP1).
     * Multiple functions are allowed (RP1 may expose multiple).
     */
    if (Bus == 1 && Device > 0)
        return NULL;

    /*
     * For bus > 0: program CFG_INDEX with the B/D/F address, then
     * access through the 4 KB CFG_DATA window.
     */
    Bcm2712Write32(RcBase,
                   PCIE_EXT_CFG_INDEX,
                   (Bus << 20) | (Device << 15) | (Function << 12));

    return (PVOID)((PUCHAR)RcBase + PCIE_EXT_CFG_DATA + Register);
}

/**
 * Perform a byte-granularity read or write to/from a config address.
 *
 * Uses natural-width MMIO accesses where alignment permits,
 * falling back to byte accesses for unaligned or odd-sized transfers.
 */
static
VOID
Bcm2712ConfigTransfer(
    _In_ BOOLEAN Write,
    _In_ PVOID ConfigAddr,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Src;
    PUCHAR Dst;
    ULONG Remaining;
    ULONG ChunkSize;

    if (Write)
    {
        Src = (PUCHAR)Buffer;
        Dst = (PUCHAR)ConfigAddr;
    }
    else
    {
        Src = (PUCHAR)ConfigAddr;
        Dst = (PUCHAR)Buffer;
    }

    Remaining = Length;
    while (Remaining > 0)
    {
        ULONG_PTR Alignment = (ULONG_PTR)(Write ? Dst : Src) & 3;

        if (Remaining >= 4 && Alignment == 0)
        {
            /* 32-bit aligned transfer */
            if (Write)
                WRITE_REGISTER_ULONG((PULONG)Dst, *(PULONG)Src);
            else
                *(PULONG)Dst = READ_REGISTER_ULONG((PULONG)Src);
            ChunkSize = 4;
        }
        else if (Remaining >= 2 && (Alignment & 1) == 0)
        {
            /* 16-bit aligned transfer */
            if (Write)
                WRITE_REGISTER_USHORT((PUSHORT)Dst, *(PUSHORT)Src);
            else
                *(PUSHORT)Dst = READ_REGISTER_USHORT((PUSHORT)Src);
            ChunkSize = 2;
        }
        else
        {
            /* Byte transfer */
            if (Write)
                WRITE_REGISTER_UCHAR(Dst, *Src);
            else
                *Dst = READ_REGISTER_UCHAR(Src);
            ChunkSize = 1;
        }

        Src += ChunkSize;
        Dst += ChunkSize;
        Remaining -= ChunkSize;
    }
}

/* ================================================================== */
/*  Public interface                                                   */
/* ================================================================== */

BOOLEAN
Bcm2712PciProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /*
     * Detect the platform by checking the ACPI FADT OEM ID.
     * The RPi5 EDK2 firmware uses "RPIFDN" as OEM ID and "RPI5" as
     * OEM Table ID.  We check both to avoid false positives on other
     * Broadcom-based systems.
     *
     * Use HalAcpiGetTable to retrieve the already-cached FADT.
     */
    PDESCRIPTION_HEADER Fadt;

    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (!Fadt)
    {
        DPRINT1("[BCM2712] No FADT table, cannot probe platform\n");
        return FALSE;
    }

    /*
     * Compare OEM ID (6 bytes, may not be null-terminated) and
     * OEM Table ID (first 4 bytes of the 8-byte field).
     */
    if (RtlCompareMemory(Fadt->OEMID,
                         BCM2712_ACPI_OEM_ID,
                         6) != 6)
    {
        DPRINT("[BCM2712] OEM ID mismatch, not a BCM2712 platform\n");
        return FALSE;
    }

    if (RtlCompareMemory(Fadt->OEMTableID,
                         BCM2712_ACPI_OEM_TABLE_ID,
                         4) != 4)
    {
        DPRINT("[BCM2712] OEM Table ID mismatch\n");
        return FALSE;
    }

    DPRINT1("[BCM2712] Platform detected: %.*s / %.*s\n",
            6, Fadt->OEMID,
            8, Fadt->OEMTableID);

    Bcm2712Detected = TRUE;
    return TRUE;
}

NTSTATUS
Bcm2712PciInit(VOID)
{
    ULONG Index;

    if (!Bcm2712Detected)
        return STATUS_NOT_SUPPORTED;

    if (Bcm2712Initialized)
        return STATUS_SUCCESS;

    KeInitializeSpinLock(&Bcm2712ConfigLock);
    RtlZeroMemory(Bcm2712RcState, sizeof(Bcm2712RcState));

    for (Index = 0; Index < BCM2712_PCIE_RC_COUNT; Index++)
    {
        PHYSICAL_ADDRESS PhysAddr;
        PVOID VirtAddr;

        PhysAddr.QuadPart = Bcm2712RcBases[Index];

        /*
         * Map the full RC register block.
         * Use HalpMapPhysicalMemory64 which works during both Phase 0
         * (via KSEG0/identity mapping from UEFI page tables) and Phase 1+
         * (via MmMapIoSpace).  The firmware already initialized the
         * controller, brought up the link, and configured windows.
         */
        VirtAddr = HalpMapPhysicalMemory64(PhysAddr,
                       (PFN_COUNT)((BCM2712_PCIE_RC_LENGTH + PAGE_SIZE - 1) >> PAGE_SHIFT));
        if (!VirtAddr)
        {
            DPRINT1("[BCM2712] Failed to map PCIE%lu at %I64x\n",
                    Index, PhysAddr.QuadPart);
            continue;
        }

        Bcm2712RcState[Index].PhysBase = PhysAddr;
        Bcm2712RcState[Index].VirtBase = VirtAddr;
        Bcm2712RcState[Index].Mapped = TRUE;

        /*
         * Validate controller is alive.  The firmware only initializes
         * controllers it uses (PCIE2 for RP1).  Uninitialized controllers
         * read garbage from all registers.
         *
         * Check: SCB access enable bit in MISC_CTRL must be set, AND
         * root port Vendor ID must be a valid Broadcom ID (0x14e4).
         */
        {
            ULONG MiscCtrl = Bcm2712Read32(VirtAddr, PCIE_MISC_MISC_CTRL);
            ULONG VendorDev = READ_REGISTER_ULONG((PULONG)VirtAddr);
            USHORT VendorId = (USHORT)(VendorDev & 0xFFFF);
            BOOLEAN ScrValid = (MiscCtrl & PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN) != 0;
            BOOLEAN VidValid = (VendorId == 0x14e4); /* Broadcom */

            if (!ScrValid || !VidValid)
            {
                /* Controller not initialized by firmware — skip it */
                Bcm2712RcState[Index].LinkUp = FALSE;
                DPRINT1("[BCM2712] PCIE%lu: not initialized by firmware "
                        "(MiscCtrl=0x%08lx VID=0x%04x) — skipped\n",
                        Index, MiscCtrl, VendorId);
                continue;
            }

            Bcm2712RcState[Index].LinkUp = Bcm2712IsLinkUp(VirtAddr);

            if (Bcm2712RcState[Index].LinkUp)
            {
                /* Dump outbound window config for address translation debugging */
                ULONG MemWinLo = Bcm2712Read32(VirtAddr, 0x400C); /* CPU_2_PCIE_MEM_WIN0_LO */
                ULONG MemWinHi = Bcm2712Read32(VirtAddr, 0x4010); /* CPU_2_PCIE_MEM_WIN0_HI */
                ULONG BaseLimitReg = Bcm2712Read32(VirtAddr, 0x4070); /* BASE_LIMIT */
                ULONG BaseHi = Bcm2712Read32(VirtAddr, 0x4080); /* BASE_HI */
                ULONG LimitHi = Bcm2712Read32(VirtAddr, 0x4084); /* LIMIT_HI */
                DPRINT1("[BCM2712] PCIE%lu: outbound MEM_WIN0: LO=0x%08lx HI=0x%08lx "
                        "BASE_LIMIT=0x%08lx BASE_HI=0x%02lx LIMIT_HI=0x%02lx\n",
                        Index, MemWinLo, MemWinHi,
                        BaseLimitReg, BaseHi, LimitHi);
                DPRINT1("[BCM2712] PCIE%lu: link UP, root port VID:DID=%04x:%04x\n",
                        Index,
                        VendorId,
                        (USHORT)((VendorDev >> 16) & 0xFFFF));

                /*
                 * Configure PCIe inbound DMA window to cover full RAM.
                 *
                 * On RPi5, ALL physical RAM is above 4GB (starting at ~0x140000000).
                 * The RP1 xHCI needs to DMA to DCBAA/command ring/event ring which
                 * reside in this high memory. Without a properly configured inbound
                 * window, DMA writes from RP1 to CPU memory silently fail (xHCI
                 * event ring stays all zeros → command timeouts).
                 *
                 * Program inbound window 2 (RC_BAR2) for identity mapping:
                 *   PCI address 0 → CPU address 0, size = 64GB
                 * This covers all possible physical RAM locations.
                 *
                 * Register layout (from Linux pcie-brcmstb driver):
                 *   RC_BAR2_CONFIG_LO (0x4034) = encoded_size | flags
                 *   RC_BAR2_CONFIG_HI (0x4038) = upper 32 bits of BAR base (0)
                 *   UBUS_BAR2_CONFIG_REMAP_LO (0x40B4) = CPU addr low | access_en
                 *   UBUS_BAR2_CONFIG_REMAP_HI (0x40B8) = CPU addr high
                 */
                {
                    /* Bar 2: offset = 0x402C + 8*(2-1) = 0x4034 */
                    #define RC_BAR2_CONFIG_LO  0x4034
                    #define RC_BAR2_CONFIG_HI  0x4038
                    /* UBUS Bar 2: offset = 0x40AC + 8*(2-1) = 0x40B4 */
                    #define UBUS_BAR2_REMAP_LO 0x40B4
                    #define UBUS_BAR2_REMAP_HI 0x40B8

                    /* 64GB window: ilog2(64GB) = 36, encode = 36 - 15 = 21 */
                    ULONG EncodedSize = 21;
                    ULONG Bar2Lo, Bar2Hi, UbusLo, UbusHi;

                    /* Read current config */
                    Bar2Lo = Bcm2712Read32(VirtAddr, RC_BAR2_CONFIG_LO);
                    Bar2Hi = Bcm2712Read32(VirtAddr, RC_BAR2_CONFIG_HI);
                    UbusLo = Bcm2712Read32(VirtAddr, UBUS_BAR2_REMAP_LO);
                    UbusHi = Bcm2712Read32(VirtAddr, UBUS_BAR2_REMAP_HI);

                    DPRINT1("[BCM2712] PCIE%lu: inbound BEFORE: BAR2_LO=0x%08lx BAR2_HI=0x%08lx "
                            "UBUS_LO=0x%08lx UBUS_HI=0x%08lx\n",
                            Index, Bar2Lo, Bar2Hi, UbusLo, UbusHi);

                    /*
                     * Program BAR2 for RP1 DMA translation.
                     *
                     * The RP1's internal bus masters (xHCI, Ethernet, DMA) add
                     * 0x10_00000000 to AXI addresses before sending PCIe TLPs.
                     * (This is the RP1 ATU's fixed outbound offset.)
                     *
                     * Set BAR2 PCI base = 0x10_00000000 so inbound TLPs from
                     * RP1 at PCI addr (CPU_PA + 0x10G) get translated to CPU_PA.
                     *
                     * dma-ranges: PCI 0x10_00000000 → CPU 0x00000000, size 64GB
                     */
                    Bar2Lo = (Bar2Lo & ~0x1F) | (EncodedSize & 0x1F);
                    Bar2Hi = 0; /* PCI BAR base address high = 0 (identity map) */
                    Bcm2712Write32(VirtAddr, RC_BAR2_CONFIG_LO, Bar2Lo);
                    Bcm2712Write32(VirtAddr, RC_BAR2_CONFIG_HI, Bar2Hi);

                    /* UBUS remap: CPU address = 0 (maps PCI base to CPU 0), access enable = 1 */
                    Bcm2712Write32(VirtAddr, UBUS_BAR2_REMAP_LO, 0x1); /* bit 0 = access enable */
                    Bcm2712Write32(VirtAddr, UBUS_BAR2_REMAP_HI, 0x0); /* CPU addr high = 0 */

                    /* Read back to verify */
                    Bar2Lo = Bcm2712Read32(VirtAddr, RC_BAR2_CONFIG_LO);
                    Bar2Hi = Bcm2712Read32(VirtAddr, RC_BAR2_CONFIG_HI);
                    UbusLo = Bcm2712Read32(VirtAddr, UBUS_BAR2_REMAP_LO);
                    UbusHi = Bcm2712Read32(VirtAddr, UBUS_BAR2_REMAP_HI);

                    DPRINT1("[BCM2712] PCIE%lu: inbound AFTER: BAR2_LO=0x%08lx BAR2_HI=0x%08lx "
                            "UBUS_LO=0x%08lx UBUS_HI=0x%08lx (64GB window)\n",
                            Index, Bar2Lo, Bar2Hi, UbusLo, UbusHi);
                }
            }
            else
            {
                DPRINT1("[BCM2712] PCIE%lu: initialized but link DOWN\n",
                        Index);
            }
        }
    }

    Bcm2712Initialized = TRUE;

    DPRINT1("[BCM2712] PCI config-space backend initialized "
            "(%lu controllers mapped)\n", BCM2712_PCIE_RC_COUNT);
    return STATUS_SUCCESS;
}

BOOLEAN
Bcm2712PciAccessConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PBCM2712_PCIE_RC Rc;
    PVOID ConfigAddr;
    KIRQL OldIrql;

    /* Not our platform */
    if (!Bcm2712Detected)
        return FALSE;

    /* Lazy init: MmMapIoSpace is not available during Phase 0 */
    if (!Bcm2712Initialized)
    {
        NTSTATUS Status = Bcm2712PciInit();
        if (!NT_SUCCESS(Status))
            return FALSE;
    }

    /*
     * Resolve ACPI segment to the correct BCM2712 controller.
     *
     * The RPi5 firmware assigns ACPI segment numbers that don't
     * necessarily match the hardware controller index.  For example,
     * the firmware may report SEG=1 for the RP1 root bridge, but
     * RP1 is physically on PCIE2 (controller index 2).
     *
     * Strategy: if the segment maps directly to a valid link-up
     * controller, use it.  Otherwise, scan all controllers for one
     * with an active link.
     */
    Rc = NULL;
    if (Segment < BCM2712_PCIE_RC_COUNT &&
        Bcm2712RcState[Segment].Mapped &&
        Bcm2712RcState[Segment].LinkUp)
    {
        Rc = &Bcm2712RcState[Segment];
    }
    else
    {
        ULONG Index;
        for (Index = 0; Index < BCM2712_PCIE_RC_COUNT; Index++)
        {
            if (Bcm2712RcState[Index].Mapped &&
                Bcm2712RcState[Index].LinkUp)
            {
                Rc = &Bcm2712RcState[Index];
                break;
            }
        }
    }

    if (!Rc)
        return FALSE;

    /* Validate offset + length within 4 KB config space */
    if (Bus > 0xFF || Offset >= 0x1000 ||
        (ULONGLONG)Offset + Length > 0x1000 || Length == 0)
    {
        return FALSE;
    }

    /*
     * If link is down and we're accessing bus > 0, return all-ones
     * (standard PCI behavior for absent devices).  Root port (bus 0)
     * is always accessible regardless of link state.
     */
    if (Bus > 0 && !Rc->LinkUp)
    {
        if (!Write)
            RtlFillMemory(Buffer, Length, 0xFF);
        return TRUE;
    }

    /*
     * Serialize config access.  The CFG_INDEX register is shared —
     * concurrent accesses to different B/D/F on the same controller
     * would race on the index.  The lock is per-system, not per-RC,
     * to match the WoR firmware's approach (TPL_HIGH_LEVEL lock).
     */
    KeAcquireSpinLock(&Bcm2712ConfigLock, &OldIrql);

    ConfigAddr = Bcm2712ComputeConfigAddress(
        Rc->VirtBase,
        Bus,
        Slot.u.bits.DeviceNumber,
        Slot.u.bits.FunctionNumber,
        Offset);

    if (!ConfigAddr)
    {
        /* Unreachable target (e.g. bus 0, device > 0) */
        KeReleaseSpinLock(&Bcm2712ConfigLock, OldIrql);
        if (!Write)
            RtlFillMemory(Buffer, Length, 0xFF);
        return TRUE;
    }

    Bcm2712ConfigTransfer(Write, ConfigAddr, Buffer, Length);

    KeReleaseSpinLock(&Bcm2712ConfigLock, OldIrql);

    /*
     * ARM64 InterruptLine patching.
     *
     * BCM2712 firmware does not program the PCI InterruptLine register
     * (offset 0x3C).  The ACPI _PRT table has the correct GSI mapping.
     * Patch InterruptLine in the read buffer so the PCI driver sees a
     * valid interrupt assignment without any changes to common code.
     *
     * This is standard practice on ARM64 — Windows HAL does the same.
     */
    if (!Write && Offset <= 0x3C && (Offset + Length) > 0x3D)
    {
        PUCHAR ByteBuffer = (PUCHAR)Buffer;
        ULONG LineOffset = 0x3C - Offset;
        ULONG PinOffset  = 0x3D - Offset;
        UCHAR InterruptLine = ByteBuffer[LineOffset];
        UCHAR InterruptPin  = ByteBuffer[PinOffset];

        if (InterruptPin != 0 && InterruptPin <= 4 &&
            (InterruptLine == 0 || InterruptLine == 0xFF))
        {
            if (HalpArm64PciRouteQueryCallback)
            {
                ULONG Gsi = 0;
                UCHAR Polarity = 0;
                UCHAR TriggerMode = 0;
                BOOLEAN Found = FALSE;
                USHORT QuerySeg;

                /*
                 * When segment is HALP_ACPI_SEGMENT_ANY (0xFFFF), try
                 * all known segments.  The ACPI _PRT entries are keyed
                 * by segment number, and callers often pass 0xFFFF.
                 */
                /*
                 * Query the _PRT for this device's interrupt.
                 * If the device is behind a bridge (bus > 0), the _PRT only
                 * covers bus 0.  Apply PCI interrupt swizzling:
                 *   swizzled_pin = ((device + pin - 1) % 4) + 1
                 * Then query bus 0 device 0 with the swizzled pin.
                 */
                {
                    UCHAR QueryBus = (UCHAR)Bus;
                    UCHAR QueryDev = (UCHAR)Slot.u.bits.DeviceNumber;
                    UCHAR QueryPin = InterruptPin;

                    if (Bus > 0)
                    {
                        /* Swizzle through bridge to bus 0 */
                        QueryPin = (UCHAR)(((Slot.u.bits.DeviceNumber + InterruptPin - 1) % 4) + 1);
                        QueryBus = 0;
                        QueryDev = 0; /* Root port is always dev 0 */
                    }

                    if (Segment != 0xFFFF)
                    {
                        Found = HalpArm64PciRouteQueryCallback(
                                    Segment, QueryBus, QueryDev,
                                    0, /* Function */
                                    QueryPin,
                                    &Gsi, &Polarity, &TriggerMode);
                    }
                    else
                    {
                        for (QuerySeg = 0; QuerySeg <= BCM2712_PCIE_RC_COUNT && !Found; QuerySeg++)
                        {
                            Found = HalpArm64PciRouteQueryCallback(
                                        QuerySeg, QueryBus, QueryDev,
                                        0, QueryPin,
                                        &Gsi, &Polarity, &TriggerMode);
                        }
                    }
                }

                if (Found)
                {
                    UCHAR PatchedLine = (Gsi <= 0xFF) ? (UCHAR)Gsi : 0xFE;
                    ByteBuffer[LineOffset] = PatchedLine;
                    DPRINT1("[BCM2712] Patched InterruptLine: Pin=%u GSI=%lu -> Line=0x%02X "
                            "(Bus %lu Dev %lu Func %lu)\n",
                            InterruptPin, Gsi, PatchedLine,
                            Bus, Slot.u.bits.DeviceNumber, Slot.u.bits.FunctionNumber);
                }
            }
        }
    }

    return TRUE;
}
