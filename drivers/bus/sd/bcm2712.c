/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Raspberry Pi 5 BCM2712 SDHCI hooks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bcm2712.h"

#define NDEBUG
#include <debug.h>

#define BCM2712_SD_HOST_PHYS             0x1000FFF000ULL
#define BCM2712_SD_CFG_PHYS              0x1000FFF400ULL
#define BCM2712_SD_CFG_LENGTH            0x200
#define BCM2712_GIO_AON_PHYS             0x107D517C00ULL
#define BCM2712_GIO_AON_LENGTH           0x40

#define BRCMSTB_GIO_DATA                 0x04
#define BRCMSTB_GIO_IODIR                0x08

#define BCM2712_SD_IOVDD_SEL_GPIO        3
#define BCM2712_SD_PWR_ON_GPIO           4

#define SDIO_CFG_CQ_CAPABILITY           0x4C
#define SDIO_CFG_CQ_CAPABILITY_FMUL_SHIFT 12
#define SDIO_CFG_SD_PIN_SEL              0x44
#define SDIO_CFG_SD_PIN_SEL_MASK         0x3
#define SDIO_CFG_SD_PIN_SEL_SD           0x2
#define SDIO_CFG_SD_PIN_SEL_MMC          0x1
#define SDIO_CFG_MAX_50MHZ_MODE          0x1AC
#define SDIO_CFG_MAX_50MHZ_MODE_STRAP_OVERRIDE 0x80000000
#define SDIO_CFG_MAX_50MHZ_MODE_ENABLE   0x1

typedef struct _SDBUS_BCM2712_EXTENSION
{
    SDBUS_HARDWARE_EXTENSION Hardware;
    PVOID CfgBase;
    ULONG CfgLength;
    PVOID GioAonBase;
    ULONG GioAonLength;
} SDBUS_BCM2712_EXTENSION, *PSDBUS_BCM2712_EXTENSION;

static VOID
SdBusBcm2712Release(
    _In_ PFDO_EXTENSION FdoExtension);

static VOID
SdBusBcm2712InitializeController(
    _In_ PFDO_EXTENSION FdoExtension);

static VOID
SdBusBcm2712SelectPins(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN MmcTiming);

static BOOLEAN
SdBusBcm2712CanVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension);

static const SDBUS_HARDWARE_OPS SdBusBcm2712Ops =
{
    SdBusBcm2712Release,
    SdBusBcm2712InitializeController,
    SdBusBcm2712SelectPins,
    SdBusBcm2712CanVoltageSwitch
};

static ULONG
SdBusBcm2712ReadCfg32(
    _In_ PSDBUS_BCM2712_EXTENSION BcmExtension,
    _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)BcmExtension->CfgBase + Register));
}

static VOID
SdBusBcm2712WriteCfg32(
    _In_ PSDBUS_BCM2712_EXTENSION BcmExtension,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)BcmExtension->CfgBase + Register), Value);
}

static ULONG
SdBusBcm2712ReadGioAon32(
    _In_ PSDBUS_BCM2712_EXTENSION BcmExtension,
    _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)BcmExtension->GioAonBase + Register));
}

static VOID
SdBusBcm2712WriteGioAon32(
    _In_ PSDBUS_BCM2712_EXTENSION BcmExtension,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)BcmExtension->GioAonBase + Register), Value);
}

static VOID
SdBusBcm2712SetGioAonOutput(
    _In_ PSDBUS_BCM2712_EXTENSION BcmExtension,
    _In_ ULONG Gpio,
    _In_ BOOLEAN High)
{
    ULONG Bit;
    ULONG Reg;

    Bit = 1u << Gpio;

    Reg = SdBusBcm2712ReadGioAon32(BcmExtension, BRCMSTB_GIO_DATA);
    if (High)
    {
        Reg |= Bit;
    }
    else
    {
        Reg &= ~Bit;
    }
    SdBusBcm2712WriteGioAon32(BcmExtension, BRCMSTB_GIO_DATA, Reg);

    Reg = SdBusBcm2712ReadGioAon32(BcmExtension, BRCMSTB_GIO_IODIR);
    Reg &= ~Bit;
    SdBusBcm2712WriteGioAon32(BcmExtension, BRCMSTB_GIO_IODIR, Reg);
}

NTSTATUS
SdBusBcm2712MapResources(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PHYSICAL_ADDRESS HostPhysicalAddress,
    _In_ PCM_PARTIAL_RESOURCE_LIST PartialList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PSDBUS_BCM2712_EXTENSION BcmExtension;
    PHYSICAL_ADDRESS CfgPhysicalAddress;
    PHYSICAL_ADDRESS GioAonPhysicalAddress;
    ULONG CfgLength;
    ULONG GioAonLength;
    ULONG i;

    if (HostPhysicalAddress.QuadPart != BCM2712_SD_HOST_PHYS)
    {
        return STATUS_SUCCESS;
    }

    CfgPhysicalAddress.QuadPart = 0;
    GioAonPhysicalAddress.QuadPart = 0;
    CfgLength = 0;
    GioAonLength = 0;

    for (i = 0; i < PartialList->Count; i++)
    {
        Descriptor = &PartialList->PartialDescriptors[i];
        if (Descriptor->Type == CmResourceTypeMemory &&
            Descriptor->u.Memory.Start.QuadPart == BCM2712_SD_CFG_PHYS &&
            Descriptor->u.Memory.Length >= BCM2712_SD_CFG_LENGTH)
        {
            CfgPhysicalAddress = Descriptor->u.Memory.Start;
            CfgLength = Descriptor->u.Memory.Length;
            continue;
        }

        if (Descriptor->Type == CmResourceTypeMemory &&
            Descriptor->u.Memory.Start.QuadPart == BCM2712_GIO_AON_PHYS &&
            Descriptor->u.Memory.Length >= BCM2712_GIO_AON_LENGTH)
        {
            GioAonPhysicalAddress = Descriptor->u.Memory.Start;
            GioAonLength = Descriptor->u.Memory.Length;
        }
    }

    if (CfgLength == 0)
    {
        DPRINT1("SDHCI: BCM2712 CFG resource missing\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (GioAonLength == 0)
    {
        DPRINT1("SDHCI: BCM2712 GIO AON resource missing\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    BcmExtension = ExAllocatePoolZero(NonPagedPool,
                                      sizeof(*BcmExtension),
                                      TAG_SDBUS);
    if (BcmExtension == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    BcmExtension->CfgBase = MmMapIoSpace(CfgPhysicalAddress,
                                         CfgLength,
                                         MmNonCached);
    if (BcmExtension->CfgBase == NULL)
    {
        ExFreePoolWithTag(BcmExtension, TAG_SDBUS);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    BcmExtension->GioAonBase = MmMapIoSpace(GioAonPhysicalAddress,
                                            GioAonLength,
                                            MmNonCached);
    if (BcmExtension->GioAonBase == NULL)
    {
        MmUnmapIoSpace(BcmExtension->CfgBase, CfgLength);
        ExFreePoolWithTag(BcmExtension, TAG_SDBUS);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    BcmExtension->Hardware.Ops = &SdBusBcm2712Ops;
    BcmExtension->CfgLength = CfgLength;
    BcmExtension->GioAonLength = GioAonLength;
    FdoExtension->HardwareExtension = BcmExtension;

    DPRINT1("SDHCI: BCM2712 CFG resource at 0x%I64x length 0x%lx\n",
            CfgPhysicalAddress.QuadPart,
            CfgLength);
    DPRINT1("SDHCI: BCM2712 GIO AON resource at 0x%I64x length 0x%lx\n",
            GioAonPhysicalAddress.QuadPart,
            GioAonLength);

    return STATUS_SUCCESS;
}

static VOID
SdBusBcm2712Release(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_BCM2712_EXTENSION BcmExtension;

    BcmExtension = FdoExtension->HardwareExtension;
    if (BcmExtension == NULL)
    {
        return;
    }

    if (BcmExtension->CfgBase != NULL)
    {
        MmUnmapIoSpace(BcmExtension->CfgBase, BcmExtension->CfgLength);
        BcmExtension->CfgBase = NULL;
    }

    if (BcmExtension->GioAonBase != NULL)
    {
        MmUnmapIoSpace(BcmExtension->GioAonBase, BcmExtension->GioAonLength);
        BcmExtension->GioAonBase = NULL;
    }

    FdoExtension->HardwareExtension = NULL;
    ExFreePoolWithTag(BcmExtension, TAG_SDBUS);
}

static VOID
SdBusBcm2712InitializeController(
    _In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_BCM2712_EXTENSION BcmExtension;
    ULONG BaseClockMhz;
    ULONG Reg;
    LARGE_INTEGER Delay;

    BcmExtension = FdoExtension->HardwareExtension;
    if (BcmExtension == NULL)
    {
        return;
    }

    SdBusBcm2712SelectPins(FdoExtension, FALSE);
    SdBusBcm2712SetGioAonOutput(BcmExtension, BCM2712_SD_IOVDD_SEL_GPIO, FALSE);
    SdBusBcm2712SetGioAonOutput(BcmExtension, BCM2712_SD_PWR_ON_GPIO, TRUE);

    Delay.QuadPart = -50000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    Reg = SdBusBcm2712ReadCfg32(BcmExtension, SDIO_CFG_MAX_50MHZ_MODE);
    Reg &= ~SDIO_CFG_MAX_50MHZ_MODE_ENABLE;
    Reg |= SDIO_CFG_MAX_50MHZ_MODE_STRAP_OVERRIDE;
    SdBusBcm2712WriteCfg32(BcmExtension, SDIO_CFG_MAX_50MHZ_MODE, Reg);

    BaseClockMhz = FdoExtension->MaxClockFrequency / 1000;
    if (BaseClockMhz == 0)
    {
        BaseClockMhz = 1;
    }

    Reg = (3 << SDIO_CFG_CQ_CAPABILITY_FMUL_SHIFT) | BaseClockMhz;
    SdBusBcm2712WriteCfg32(BcmExtension, SDIO_CFG_CQ_CAPABILITY, Reg);

    DPRINT1("SDHCI: BCM2712 CFG initialized (base clock %lu MHz)\n",
            BaseClockMhz);
    DPRINT1("SDHCI: BCM2712 SD slot power enabled at 3.3V\n");
}

static VOID
SdBusBcm2712SelectPins(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ BOOLEAN MmcTiming)
{
    PSDBUS_BCM2712_EXTENSION BcmExtension;
    ULONG Reg;

    BcmExtension = FdoExtension->HardwareExtension;
    if (BcmExtension == NULL)
    {
        return;
    }

    Reg = SdBusBcm2712ReadCfg32(BcmExtension, SDIO_CFG_SD_PIN_SEL);
    Reg &= ~SDIO_CFG_SD_PIN_SEL_MASK;
    Reg |= MmcTiming ? SDIO_CFG_SD_PIN_SEL_MMC : SDIO_CFG_SD_PIN_SEL_SD;
    SdBusBcm2712WriteCfg32(BcmExtension, SDIO_CFG_SD_PIN_SEL, Reg);
}

static BOOLEAN
SdBusBcm2712CanVoltageSwitch(
    _In_ PFDO_EXTENSION FdoExtension)
{
    UNREFERENCED_PARAMETER(FdoExtension);

    /*
     * The Pi 5 microSD slot has an external vqmmc GPIO regulator. Until this
     * driver owns that regulator, do not request S18R and then switch only the
     * SDHCI host side to 1.8V.
     */
    return FALSE;
}
