/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2712 PCIe config-space backend
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Broadcom BCM2712 (Raspberry Pi 5) indirect PCI
 *              configuration space access via CFG_INDEX / CFG_DATA.
 *
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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
    BOOLEAN          Valid;      /* TRUE if firmware initialized the RC  */
    BOOLEAN          LinkUp;     /* TRUE if PHY link is active           */
    BOOLEAN          BusNumbersValid;
    UCHAR            PrimaryBus;
    UCHAR            SecondaryBus;
    UCHAR            SubordinateBus;
} BCM2712_PCIE_RC, *PBCM2712_PCIE_RC;

static BCM2712_PCIE_RC Bcm2712RcState[BCM2712_PCIE_RC_COUNT];
BOOLEAN                Bcm2712Detected  = FALSE;
static BOOLEAN         Bcm2712Initialized = FALSE;
static KSPIN_LOCK      Bcm2712ConfigLock;

/* Physical base addresses — indexed by hardware controller number. */
static const ULONGLONG Bcm2712RcBases[BCM2712_PCIE_RC_COUNT] = {
    BCM2712_PCIE0_RC_BASE,
    BCM2712_PCIE1_RC_BASE,
    BCM2712_PCIE2_RC_BASE,
};

#define BCM2712_PCI_BRIDGE_BUS_NUMBERS 0x18

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

static
ULONG
Bcm2712EncodeInboundWindowSize(
    _In_ ULONGLONG Size)
{
    ULONG Log2;

    if (Size == 0 || (Size & (Size - 1)) != 0)
        return 0;

    Log2 = 0;
    while (Size > 1)
    {
        Size >>= 1;
        Log2++;
    }

    if (Log2 >= 12 && Log2 <= 15)
        return (Log2 - 12) + 0x1C;

    if (Log2 >= 16 && Log2 <= 36)
        return Log2 - 15;

    return 0;
}

static
VOID
Bcm2712ProgramInboundWindow(
    _In_ PVOID RcBase,
    _In_ ULONG BarLoOffset,
    _In_ ULONG UbusLoOffset,
    _In_ ULONGLONG PciBase,
    _In_ ULONGLONG CpuBase,
    _In_ ULONGLONG Size)
{
    ULONG EncodedSize;
    ULONG BarLo;
    ULONG UbusLo;

    EncodedSize = Bcm2712EncodeInboundWindowSize(Size);
    if (EncodedSize == 0)
    {
        Bcm2712Write32(RcBase, BarLoOffset, 0);
        Bcm2712Write32(RcBase, BarLoOffset + 4, 0);
        Bcm2712Write32(RcBase, UbusLoOffset, 0);
        Bcm2712Write32(RcBase, UbusLoOffset + 4, 0);
        return;
    }

    BarLo = (ULONG)PciBase & ~PCIE_MISC_RC_BAR_CONFIG_LO_SIZE_MASK;
    BarLo |= EncodedSize & PCIE_MISC_RC_BAR_CONFIG_LO_SIZE_MASK;

    UbusLo = ((ULONG)CpuBase & ~0xFFFUL) | PCIE_MISC_UBUS_BAR_REMAP_ACCESS_EN;

    Bcm2712Write32(RcBase, BarLoOffset, BarLo);
    Bcm2712Write32(RcBase, BarLoOffset + 4, (ULONG)(PciBase >> 32));
    Bcm2712Write32(RcBase, UbusLoOffset, UbusLo);
    Bcm2712Write32(RcBase, UbusLoOffset + 4, (ULONG)(CpuBase >> 32));
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
    ULONG Mask = PCIE_MISC_PCIE_STATUS_PHYLINKUP | PCIE_MISC_PCIE_STATUS_DL_ACTIVE;

    Status = Bcm2712Read32(RcBase, PCIE_MISC_PCIE_STATUS);
    return (Status & Mask) == Mask;
}

/**
 * Compute the MMIO address for a config space access.
 *
 * For the primary bus (root port):
 *   address = RcBase + Register
 *
 * For downstream buses:
 *   1. Write (Bus << 20 | Dev << 15 | Func << 12) to CFG_INDEX
 *   2. address = RcBase + CFG_DATA + Register
 *
 * Returns NULL if the target is known-unreachable.
 */
static
PVOID
Bcm2712ComputeConfigAddress(
    _In_ PBCM2712_PCIE_RC Rc,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Register)
{
    PVOID RcBase = Rc->VirtBase;

    /*
     * Primary bus: only device 0, function 0 exists (the root port itself).
     * The config registers are at the RC base directly.
     */
    if (Bus == Rc->PrimaryBus)
    {
        if (Device > 0 || Function > 0)
            return NULL;

        return (PVOID)((PUCHAR)RcBase + Register);
    }

    if (!Rc->LinkUp)
        return NULL;

    if (Rc->BusNumbersValid &&
        (Bus < Rc->SecondaryBus || Bus > Rc->SubordinateBus))
    {
        return NULL;
    }

    /*
     * The directly-attached endpoint bus only has device 0 behind this
     * single-root-port bridge.  Subordinate buses behind a downstream
     * bridge are addressed normally through CFG_INDEX.
     */
    if (Bus == Rc->SecondaryBus && Device > 0)
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

static
BOOLEAN
Bcm2712AnyRcValid(VOID)
{
    ULONG Index;

    for (Index = 0; Index < BCM2712_PCIE_RC_COUNT; Index++)
    {
        if (Bcm2712RcState[Index].Valid)
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
Bcm2712RcContainsBus(
    _In_ PBCM2712_PCIE_RC Rc,
    _In_ ULONG Bus)
{
    if (!Rc->Valid)
        return FALSE;

    if (!Rc->BusNumbersValid)
        return (Bus == Rc->PrimaryBus ||
                (Rc->LinkUp && Bus == Rc->SecondaryBus));

    return (Bus == Rc->PrimaryBus ||
            (Bus >= Rc->SecondaryBus && Bus <= Rc->SubordinateBus));
}

static
PBCM2712_PCIE_RC
Bcm2712FindRcForConfigSpace(
    _In_ USHORT Segment,
    _In_ ULONG Bus)
{
    ULONG Index;

    /*
     * Firmware segment numbers may not match the hardware controller index on
     * Raspberry Pi 5.  Prefer a segment match only when that controller owns
     * the requested bus, then fall back to the live controller whose bridge
     * bus-number registers contain the request.
     */
    if (Segment < BCM2712_PCIE_RC_COUNT &&
        Bcm2712RcContainsBus(&Bcm2712RcState[Segment], Bus))
    {
        return &Bcm2712RcState[Segment];
    }

    for (Index = 0; Index < BCM2712_PCIE_RC_COUNT; Index++)
    {
        if (Bcm2712RcState[Index].LinkUp &&
            Bcm2712RcContainsBus(&Bcm2712RcState[Index], Bus))
            return &Bcm2712RcState[Index];
    }

    for (Index = 0; Index < BCM2712_PCIE_RC_COUNT; Index++)
    {
        if (Bcm2712RcContainsBus(&Bcm2712RcState[Index], Bus))
            return &Bcm2712RcState[Index];
    }

    return NULL;
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
                continue;
            }

            {
                ULONG BusNumbers;

                BusNumbers = Bcm2712Read32(VirtAddr, BCM2712_PCI_BRIDGE_BUS_NUMBERS);

                Bcm2712RcState[Index].Valid = TRUE;
                Bcm2712RcState[Index].PrimaryBus = (UCHAR)(BusNumbers & 0xFF);
                Bcm2712RcState[Index].SecondaryBus = (UCHAR)((BusNumbers >> 8) & 0xFF);
                Bcm2712RcState[Index].SubordinateBus = (UCHAR)((BusNumbers >> 16) & 0xFF);
                Bcm2712RcState[Index].BusNumbersValid =
                    (Bcm2712RcState[Index].SecondaryBus != 0 ||
                     Bcm2712RcState[Index].SubordinateBus != 0);

                if (!Bcm2712RcState[Index].BusNumbersValid)
                {
                    Bcm2712RcState[Index].PrimaryBus = 0;
                    Bcm2712RcState[Index].SecondaryBus = 1;
                    Bcm2712RcState[Index].SubordinateBus = 1;
                }
            }

            Bcm2712RcState[Index].LinkUp = Bcm2712IsLinkUp(VirtAddr);

            DPRINT("[BCM2712] PCIE%lu: bus %u-%u-%u, link %s\n",
                   Index,
                   Bcm2712RcState[Index].PrimaryBus,
                   Bcm2712RcState[Index].SecondaryBus,
                   Bcm2712RcState[Index].SubordinateBus,
                   Bcm2712RcState[Index].LinkUp ? "UP" : "DOWN");

            if (Bcm2712RcState[Index].LinkUp && Index == 2)
            {
                /* RP1-only inbound translations: program solely on PCIE2 (the RP1 x4 link). */
                Bcm2712ProgramInboundWindow(VirtAddr,
                                            RC_BAR1_CONFIG_LO,
                                            UBUS_BAR1_REMAP_LO,
                                            BCM2712_RP1_DMA_PCI_BASE,
                                            BCM2712_RP1_DMA_CPU_BASE,
                                            BCM2712_RP1_DMA_SIZE);

                /* RP1 MSI writes target MIP0 through this 4 KiB inbound window. */
                Bcm2712ProgramInboundWindow(VirtAddr,
                                            RC_BAR_CONFIG_LO(BCM2712_PCIE2_MIP0_BAR),
                                            UBUS_BAR_REMAP_LO(BCM2712_PCIE2_MIP0_BAR),
                                            BCM2712_PCIE2_MIP0_PCI_BASE,
                                            BCM2712_PCIE2_MIP0_CPU_BASE,
                                            BCM2712_PCIE2_MIP0_SIZE);
            }
        }
    }

    Bcm2712Initialized = TRUE;

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

    /* Validate offset + length within 4 KB config space */
    if (Bus > 0xFF || Offset >= 0x1000 ||
        (ULONGLONG)Offset + Length > 0x1000 || Length == 0)
    {
        return FALSE;
    }

    Rc = Bcm2712FindRcForConfigSpace(Segment, Bus);

    if (!Rc)
    {
        if (Bcm2712AnyRcValid())
        {
            if (!Write)
                RtlFillMemory(Buffer, Length, 0xFF);
            return TRUE;
        }

        return FALSE;
    }

    /*
     * If link is down and we're accessing bus > 0, return all-ones
     * (standard PCI behavior for absent devices).  Root port (bus 0)
     * is always accessible regardless of link state.
     */
    if (Bus != Rc->PrimaryBus && !Rc->LinkUp)
    {
        if (!Write)
            RtlFillMemory(Buffer, Length, 0xFF);
        return TRUE;
    }

    /*
     * Serialize config access.  A controller has one CFG_INDEX selector, so
     * racing selector writes would target the wrong B/D/F.
     */
    KeAcquireSpinLock(&Bcm2712ConfigLock, &OldIrql);

    ConfigAddr = Bcm2712ComputeConfigAddress(
        Rc,
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
     * Keep this platform-specific fix in the HAL path instead of the common
     * PCI bus driver.
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
                        /*
                         * Segment 0xFFFF is a wildcard from the config-space
                         * caller.  ACPI _PRT entries are keyed by firmware
                         * _SEG values, not by BCM2712 hardware RC indices.
                         */
                        for (QuerySeg = 0; QuerySeg < BCM2712_PCIE_RC_COUNT && !Found; QuerySeg++)
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
                }
            }
        }
    }

    return TRUE;
}
