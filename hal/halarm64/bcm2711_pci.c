/*
 * PROJECT:     ReactOS HAL — ARM64 BCM2711 PCIe host + config backend
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Broadcom BCM2711 (Raspberry Pi 4 / CM4) brcmstb PCIe root
 *              complex: controller bring-up + link training (the firmware
 *              leaves the CM4 link down) and indirect PCI configuration
 *              space access via CFG_INDEX / CFG_DATA.
 *
 *              Config-access + InterruptLine patching modelled on
 *              bcm2712_pci.c.  Bring-up sequence reimplemented from the
 *              register documentation (Bcm2711.h) and the public Linux
 *              pcie-brcmstb bring-up description; no GPL source copied.
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
#include "halext.h"
#include "bcm2711_pci.h"

#define NDEBUG
#include <debug.h>

/* ACPI _PRT callback from halarm64.c — used for InterruptLine patching */
extern PHAL_ACPI_PCI_ROUTE_QUERY HalpArm64PciRouteQueryCallback;

/* ================================================================== */
/*  Single-controller runtime state                                   */
/* ================================================================== */

typedef struct _BCM2711_PCIE_RC
{
    PVOID    VirtBase;
    BOOLEAN  Mapped;
    BOOLEAN  Valid;
    BOOLEAN  LinkUp;
    UCHAR    PrimaryBus;
    UCHAR    SecondaryBus;
    UCHAR    SubordinateBus;
} BCM2711_PCIE_RC;

static BCM2711_PCIE_RC Bcm2711Rc;
BOOLEAN                Bcm2711Detected   = FALSE;
static BOOLEAN         Bcm2711Initialized = FALSE;
static KSPIN_LOCK      Bcm2711ConfigLock;

/* ================================================================== */
/*  Register helpers                                                  */
/* ================================================================== */

FORCEINLINE
ULONG
Bcm2711Read32(
    _In_ PVOID RcBase,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)RcBase + Offset));
}

FORCEINLINE
VOID
Bcm2711Write32(
    _In_ PVOID RcBase,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)RcBase + Offset), Value);
}

static
ULONG
Bcm2711Log2(
    _In_ ULONGLONG Value)
{
    ULONG Log2 = 0;

    while (Value > 1)
    {
        Value >>= 1;
        Log2++;
    }

    return Log2;
}

/*
 * Encode an inbound-window size for RC_BARn_CONFIG_LO / SCB size fields.
 * 4KB..32KB use the 0x1C-based range; 64KB..32GB use ilog2(size) - 15.
 */
static
ULONG
Bcm2711EncodeWindowSize(
    _In_ ULONGLONG Size)
{
    ULONG Log2;

    if (Size == 0 || (Size & (Size - 1)) != 0)
        return 0;

    Log2 = Bcm2711Log2(Size);

    if (Log2 >= 12 && Log2 <= 15)
        return (Log2 - 12) + 0x1C;

    if (Log2 >= 16 && Log2 <= 36)
        return Log2 - 15;

    return 0;
}

/* ================================================================== */
/*  Controller bring-up / link training                               */
/* ================================================================== */

static
BOOLEAN
Bcm2711IsLinkUp(
    _In_ PVOID RcBase)
{
    ULONG Mask = PCIE_MISC_PCIE_STATUS_RC_MODE |
                 PCIE_MISC_PCIE_STATUS_PHYLINKUP | PCIE_MISC_PCIE_STATUS_DL_ACTIVE;
    ULONG Status = Bcm2711Read32(RcBase, PCIE_MISC_PCIE_STATUS);

    return (Status & Mask) == Mask;
}

static
VOID
Bcm2711ProgramOutboundWindow(
    _In_ PVOID RcBase,
    _In_ ULONGLONG CpuBase,
    _In_ ULONGLONG PciBase,
    _In_ ULONGLONG Size)
{
    ULONGLONG CpuBaseMb  = CpuBase >> 20;
    ULONGLONG LimitMb    = (CpuBase + Size - 1) >> 20;
    ULONG BaseLimit;

    Bcm2711Write32(RcBase, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO, (ULONG)PciBase);
    Bcm2711Write32(RcBase, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI, (ULONG)(PciBase >> 32));

    BaseLimit = (((ULONG)CpuBaseMb & 0xFFF) << 4) |
                (((ULONG)LimitMb   & 0xFFF) << 20);
    Bcm2711Write32(RcBase, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT, BaseLimit);

    Bcm2711Write32(RcBase, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI,
                   (ULONG)(CpuBaseMb >> 12) & 0xFF);
    Bcm2711Write32(RcBase, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI,
                   (ULONG)(LimitMb >> 12) & 0xFF);
}

/*
 * Bring the brcmstb PCIe root complex up and train the link.
 *
 * Sequence (reimplemented from Bcm2711.h register map + the public Linux
 * pcie-brcmstb flow): assert bridge reset + PERST, release bridge reset,
 * power up the SerDes, program MISC_CTRL + inbound/outbound windows + bus
 * numbers, release PERST, then poll for PHYLINKUP + DL_ACTIVE.
 */
static
VOID
Bcm2711BringUpLink(
    _In_ PVOID RcBase)
{
    ULONG Value;
    ULONG SizeCode;
    ULONG Attempt;

    /* 1. Assert bridge reset and PERST. */
    Value = Bcm2711Read32(RcBase, PCIE_RGR1_SW_INIT_1);
    Value |= PCIE_RGR1_SW_INIT_1_INIT_MASK | PCIE_RGR1_SW_INIT_1_PERST_MASK;
    Bcm2711Write32(RcBase, PCIE_RGR1_SW_INIT_1, Value);

    KeStallExecutionProcessor(200);

    /* 2. Release the bridge reset (keep PERST asserted for now). */
    Value = Bcm2711Read32(RcBase, PCIE_RGR1_SW_INIT_1);
    Value &= ~PCIE_RGR1_SW_INIT_1_INIT_MASK;
    Bcm2711Write32(RcBase, PCIE_RGR1_SW_INIT_1, Value);

    /* 3. Power up the SerDes (clear IDDQ) and let it settle. */
    Value = Bcm2711Read32(RcBase, PCIE_MISC_HARD_PCIE_HARD_DEBUG);
    Value &= ~PCIE_MISC_HARD_PCIE_HARD_DEBUG_SERDES_IDDQ;
    Bcm2711Write32(RcBase, PCIE_MISC_HARD_PCIE_HARD_DEBUG, Value);

    KeStallExecutionProcessor(200);

    /* 4. MISC_CTRL: enable SCB access, UR-on-bad-cfg, RCB modes, 128B burst,
     *    and the SCB0 size to match the inbound DMA window. */
    SizeCode = Bcm2711EncodeWindowSize(BCM2711_PCIE_DMA_SIZE);
    Value = Bcm2711Read32(RcBase, PCIE_MISC_MISC_CTRL);
    Value |= PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN |
             PCIE_MISC_MISC_CTRL_CFG_READ_UR |
             PCIE_MISC_MISC_CTRL_RCB_MPS_MODE |
             PCIE_MISC_MISC_CTRL_RCB_64B_MODE;
    Value &= ~PCIE_MISC_MISC_CTRL_MAX_BURST_MASK;
    Value |= (BCM2711_BURST_SIZE_128 << 20) & PCIE_MISC_MISC_CTRL_MAX_BURST_MASK;
    Value &= ~PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK;
    Value |= (SizeCode << PCIE_MISC_MISC_CTRL_SCB0_SIZE_SHIFT) &
             PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK;
    Bcm2711Write32(RcBase, PCIE_MISC_MISC_CTRL, Value);

    /* 5. Inbound window (RC_BAR2 = main dma-ranges), little-endian BAR2. */
    Bcm2711Write32(RcBase, PCIE_MISC_RC_BAR2_CONFIG_LO,
                   ((ULONG)BCM2711_PCIE_DMA_PCI_BASE &
                    ~PCIE_MISC_RC_BAR2_CONFIG_LO_SIZE_MASK) |
                   (SizeCode & PCIE_MISC_RC_BAR2_CONFIG_LO_SIZE_MASK));
    Bcm2711Write32(RcBase, PCIE_MISC_RC_BAR2_CONFIG_HI,
                   (ULONG)(BCM2711_PCIE_DMA_PCI_BASE >> 32));

    Value = Bcm2711Read32(RcBase, PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1);
    Value &= ~PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1_ENDIAN_BAR2_MASK;
    Bcm2711Write32(RcBase, PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1, Value);

    /* 6. Outbound CPU->PCIe memory window. */
    Bcm2711ProgramOutboundWindow(RcBase,
                                 BCM2711_PCIE_CPU_MMIO_BASE,
                                 BCM2711_PCIE_PCI_MMIO_BASE,
                                 BCM2711_PCIE_MMIO_LEN);

    /* 7. Bus numbers: primary 0, secondary 1, subordinate 1. */
    Value = Bcm2711Read32(RcBase, BCM2711_PCI_BRIDGE_BUS_NUMBERS);
    Value &= 0xFF000000;
    Value |= (0x00) | (0x01 << 8) | (0x01 << 16);
    Bcm2711Write32(RcBase, BCM2711_PCI_BRIDGE_BUS_NUMBERS, Value);

    /* 8. Release PERST so the endpoint comes out of reset. */
    Value = Bcm2711Read32(RcBase, PCIE_RGR1_SW_INIT_1);
    Value &= ~PCIE_RGR1_SW_INIT_1_PERST_MASK;
    Bcm2711Write32(RcBase, PCIE_RGR1_SW_INIT_1, Value);

    /* 9. Poll for link up (PHYLINKUP + DL_ACTIVE), ~100 ms total. */
    for (Attempt = 0; Attempt < 100; Attempt++)
    {
        if (Bcm2711IsLinkUp(RcBase))
        {
            Bcm2711Rc.LinkUp = TRUE;
            break;
        }

        KeStallExecutionProcessor(1000);
    }

    DPRINT1("[BCM2711] PCIe link %s (status 0x%08lx)\n",
            Bcm2711Rc.LinkUp ? "UP" : "DOWN",
            Bcm2711Read32(RcBase, PCIE_MISC_PCIE_STATUS));
}

/* ================================================================== */
/*  Config-space addressing                                           */
/* ================================================================== */

static
PVOID
Bcm2711ComputeConfigAddress(
    _In_ PVOID RcBase,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Register)
{
    /* Root port (primary bus): only dev0/fn0, registers at the RC base. */
    if (Bus == Bcm2711Rc.PrimaryBus)
    {
        if (Device > 0 || Function > 0)
            return NULL;

        return (PVOID)((PUCHAR)RcBase + Register);
    }

    if (!Bcm2711Rc.LinkUp)
        return NULL;

    /* Directly-attached endpoint bus has only device 0 behind the port. */
    if (Bus == Bcm2711Rc.SecondaryBus && Device > 0)
        return NULL;

    Bcm2711Write32(RcBase,
                   PCIE_EXT_CFG_INDEX,
                   (Bus << 20) | (Device << 15) | (Function << 12));

    return (PVOID)((PUCHAR)RcBase + PCIE_EXT_CFG_DATA + Register);
}

static
VOID
Bcm2711ConfigTransfer(
    _In_ BOOLEAN Write,
    _In_ PVOID ConfigAddr,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Src = Write ? (PUCHAR)Buffer : (PUCHAR)ConfigAddr;
    PUCHAR Dst = Write ? (PUCHAR)ConfigAddr : (PUCHAR)Buffer;
    ULONG Remaining = Length;

    while (Remaining > 0)
    {
        ULONG_PTR Alignment = (ULONG_PTR)(Write ? Dst : Src) & 3;
        ULONG ChunkSize;

        if (Remaining >= 4 && Alignment == 0)
        {
            if (Write)
                WRITE_REGISTER_ULONG((PULONG)Dst, *(PULONG)Src);
            else
                *(PULONG)Dst = READ_REGISTER_ULONG((PULONG)Src);
            ChunkSize = 4;
        }
        else if (Remaining >= 2 && (Alignment & 1) == 0)
        {
            if (Write)
                WRITE_REGISTER_USHORT((PUSHORT)Dst, *(PUSHORT)Src);
            else
                *(PUSHORT)Dst = READ_REGISTER_USHORT((PUSHORT)Src);
            ChunkSize = 2;
        }
        else
        {
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
/*  Public interface                                                  */
/* ================================================================== */

static const HAL_ARM64_PCI_CONFIG_BACKEND Bcm2711PciConfigBackend =
{
    "BCM2711 brcmstb CFG_INDEX/CFG_DATA",
    Bcm2711PciAccessConfigSpace,
};

BOOLEAN
Bcm2711PciProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PDESCRIPTION_HEADER Fadt;

    if (Bcm2711Detected)
        return TRUE;

    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (!Fadt)
        return FALSE;

    if (RtlCompareMemory(Fadt->OEMID, BCM2711_ACPI_OEM_ID, 6) != 6)
        return FALSE;

    if (RtlCompareMemory(Fadt->OEMTableID, BCM2711_ACPI_OEM_TABLE_ID, 4) != 4)
        return FALSE;

    Bcm2711Detected = TRUE;

    HalpArm64RegisterPciConfigBackend(&Bcm2711PciConfigBackend);

    return TRUE;
}

NTSTATUS
Bcm2711PciInit(VOID)
{
    PHYSICAL_ADDRESS PhysAddr;
    PVOID VirtAddr;

    if (!Bcm2711Detected)
        return STATUS_NOT_SUPPORTED;

    if (Bcm2711Initialized)
        return STATUS_SUCCESS;

    KeInitializeSpinLock(&Bcm2711ConfigLock);
    RtlZeroMemory(&Bcm2711Rc, sizeof(Bcm2711Rc));

    PhysAddr.QuadPart = BCM2711_PCIE_RC_BASE;
    VirtAddr = HalpMapPhysicalMemory64(PhysAddr,
                   (PFN_COUNT)((BCM2711_PCIE_RC_LENGTH + PAGE_SIZE - 1) >> PAGE_SHIFT));
    if (!VirtAddr)
    {
        DPRINT1("[BCM2711] Failed to map PCIe RC at %I64x\n", PhysAddr.QuadPart);
        Bcm2711Initialized = TRUE;
        return STATUS_UNSUCCESSFUL;
    }

    Bcm2711Rc.VirtBase = VirtAddr;
    Bcm2711Rc.Mapped = TRUE;
    Bcm2711Rc.Valid = TRUE;
    Bcm2711Rc.PrimaryBus = 0;
    Bcm2711Rc.SecondaryBus = 1;
    Bcm2711Rc.SubordinateBus = 1;
    Bcm2711Rc.LinkUp = FALSE;

    if (Bcm2711IsLinkUp(VirtAddr))
    {
        Bcm2711Rc.LinkUp = TRUE;
        DPRINT1("[BCM2711] PCIe link already trained by firmware (status 0x%08lx)\n",
                Bcm2711Read32(VirtAddr, PCIE_MISC_PCIE_STATUS));
    }
    else
    {
        Bcm2711BringUpLink(VirtAddr);
    }

    Bcm2711Initialized = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
Bcm2711PciAccessConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PVOID ConfigAddr;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Segment);

    if (!Bcm2711Detected)
        return FALSE;

    if (!Bcm2711Initialized)
    {
        NTSTATUS Status = Bcm2711PciInit();
        if (!NT_SUCCESS(Status))
            return FALSE;
    }

    if (!Bcm2711Rc.Valid)
        return FALSE;

    if (Bus > 0xFF || Offset >= 0x1000 ||
        (ULONGLONG)Offset + Length > 0x1000 || Length == 0)
    {
        return FALSE;
    }

    /* Buses outside this single root complex: absent. */
    if (Bus != Bcm2711Rc.PrimaryBus &&
        (Bus < Bcm2711Rc.SecondaryBus || Bus > Bcm2711Rc.SubordinateBus))
    {
        if (!Write)
            RtlFillMemory(Buffer, Length, 0xFF);
        return TRUE;
    }

    /* Link down and accessing a downstream bus: absent device. */
    if (Bus != Bcm2711Rc.PrimaryBus && !Bcm2711Rc.LinkUp)
    {
        if (!Write)
            RtlFillMemory(Buffer, Length, 0xFF);
        return TRUE;
    }

    KeAcquireSpinLock(&Bcm2711ConfigLock, &OldIrql);

    ConfigAddr = Bcm2711ComputeConfigAddress(Bcm2711Rc.VirtBase,
                                             Bus,
                                             Slot.u.bits.DeviceNumber,
                                             Slot.u.bits.FunctionNumber,
                                             Offset);
    if (!ConfigAddr)
    {
        KeReleaseSpinLock(&Bcm2711ConfigLock, OldIrql);
        if (!Write)
            RtlFillMemory(Buffer, Length, 0xFF);
        return TRUE;
    }

    Bcm2711ConfigTransfer(Write, ConfigAddr, Buffer, Length);

    KeReleaseSpinLock(&Bcm2711ConfigLock, OldIrql);

    /*
     * InterruptLine patching: bcm2711 firmware leaves the PCI InterruptLine
     * register unprogrammed.  The ACPI _PRT carries the real GSI; patch the
     * read buffer so pci.sys sees a valid line without common-code changes.
     */
    if (!Write && Offset <= 0x3C && (Offset + Length) > 0x3D &&
        HalpArm64PciRouteQueryCallback)
    {
        PUCHAR ByteBuffer = (PUCHAR)Buffer;
        ULONG LineOffset = 0x3C - Offset;
        ULONG PinOffset = 0x3D - Offset;
        UCHAR InterruptLine = ByteBuffer[LineOffset];
        UCHAR InterruptPin = ByteBuffer[PinOffset];

        if (InterruptPin != 0 && InterruptPin <= 4 &&
            (InterruptLine == 0 || InterruptLine == 0xFF))
        {
            ULONG Gsi = 0;
            UCHAR Polarity = 0;
            UCHAR TriggerMode = 0;
            UCHAR QueryPin = InterruptPin;
            UCHAR QueryDev = (UCHAR)Slot.u.bits.DeviceNumber;
            UCHAR QueryBus = (UCHAR)Bus;
            USHORT QuerySeg;
            BOOLEAN Found = FALSE;

            if (Bus > 0)
            {
                QueryPin = (UCHAR)(((Slot.u.bits.DeviceNumber + InterruptPin - 1) % 4) + 1);
                QueryBus = 0;
                QueryDev = 0;
            }

            for (QuerySeg = 0; QuerySeg < 2 && !Found; QuerySeg++)
            {
                Found = HalpArm64PciRouteQueryCallback(
                            QuerySeg, QueryBus, QueryDev, 0, QueryPin,
                            &Gsi, &Polarity, &TriggerMode);
            }

            if (Found)
                ByteBuffer[LineOffset] = (Gsi <= 0xFF) ? (UCHAR)Gsi : 0xFE;
        }
    }

    return TRUE;
}
