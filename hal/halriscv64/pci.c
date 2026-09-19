/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Firmware-configured QEMU virt PCI host and device PMAs
 */

#include <ntifs.h>
#include <reactos/riscv64/fdtlib.h>
#include "halp.h"

#define RISCV_PCI_MAX_RANGES 8
#define RISCV_PCI_MAX_INTX_ROUTES 64
#define RISCV_PCI_CONFIG_SIZE 0x1000UL
#define RISCV_PCI_BUS_SIZE 0x100000UL
#define RISCV_PHYSICAL_LIMIT 0x0100000000000000ULL

typedef struct _RISCV_PCI_RANGE
{
    ULONG64 BusAddress;
    ULONG64 PhysicalAddress;
    ULONG64 Size;
    ULONG AddressSpace;
} RISCV_PCI_RANGE;

typedef struct _RISCV_PCI_INTX_ROUTE
{
    ULONG Address[3];
    ULONG Pin;
    ULONG Source;
} RISCV_PCI_INTX_ROUTE;

typedef struct _RISCV_PCI_HOST
{
    ULONG64 ConfigAddress;
    ULONG64 ConfigSize;
    PUCHAR ConfigMapping;
    ULONG Segment;
    ULONG FirstBus;
    ULONG LastBus;
    ULONG RangeCount;
    RISCV_PCI_RANGE Ranges[RISCV_PCI_MAX_RANGES];
    ULONG InterruptMask[4];
    ULONG InterruptRouteCount;
    RISCV_PCI_INTX_ROUTE InterruptRoutes[RISCV_PCI_MAX_INTX_ROUTES];
    BOOLEAN DmaCoherent;
    KSPIN_LOCK Lock;
} RISCV_PCI_HOST;

static RISCV_PCI_HOST HalpRiscvPciHost;
static BOOLEAN HalpRiscvPciHostPresent;

static BOOLEAN
HalpRiscvRangeContains(ULONG64 Base, ULONG64 Size, ULONG64 Address, ULONG64 Length)
{
    return Length && Address >= Base && Length <= Size && Address - Base <= Size - Length;
}

static BOOLEAN
HalpRiscvDeviceRangeValid(ULONG64 Address, ULONG64 Size)
{
    return Size && !(Address & (PAGE_SIZE - 1)) && !(Size & (PAGE_SIZE - 1)) &&
           Address < RISCV_PHYSICAL_LIMIT && Size <= RISCV_PHYSICAL_LIMIT - Address;
}

static BOOLEAN
HalpRiscvRangesOverlap(ULONG64 Base1, ULONG64 Size1, ULONG64 Base2, ULONG64 Size2)
{
    return Base1 <= Base2 ? Base2 - Base1 < Size1 : Base1 - Base2 < Size2;
}

BOOLEAN
HalpRiscvInitializePci(const VOID *DeviceTree, SIZE_T DeviceTreeSize)
{
    RISCV_FDT Fdt;
    ULONG Root, Soc, Parent, Node, Length, Value, Index, Other, Entry, Phandle, Source;
    const VOID *Property;
    RISCV_PCI_HOST *Host = &HalpRiscvPciHost;

    if (!RiscvFdtOpen(DeviceTree, DeviceTreeSize, &Fdt))
        return FALSE;
    Root = RiscvFdtRootNode(&Fdt);
    Property = RiscvFdtGetProperty(&Fdt, Root, "compatible", &Length);
    if (!RiscvFdtStringListContains(Property, Length, "riscv-virtio"))
        return TRUE;

    /* This PMA contract is for QEMU virt, not all generic ECAM hardware.
     * Its /soc has an identity translation to the CPU physical address space. */
    if (!RiscvFdtFindNode(&Fdt, "/soc", &Soc, &Parent) || Parent != Root)
        return FALSE;
    Property = RiscvFdtGetProperty(&Fdt, Soc, "ranges", &Length);
    if (!Property || Length ||
        !RiscvFdtReadU32(&Fdt, Soc, "#address-cells", &Value) || Value != 2 ||
        !RiscvFdtReadU32(&Fdt, Soc, "#size-cells", &Value) || Value != 2)
        return FALSE;

    for (Node = RiscvFdtFirstChild(&Fdt, Soc); Node != RISCV_FDT_NO_NODE; Node = RiscvFdtNextSibling(&Fdt, Node))
    {
        Property = RiscvFdtGetProperty(&Fdt, Node, "compatible", &Length);
        if (!RiscvFdtStringListContains(Property, Length, "pci-host-ecam-generic"))
            continue;
        Property = RiscvFdtGetProperty(&Fdt, Node, "status", &Length);
        if (Property && !RiscvFdtStringListContains(Property, Length, "okay") &&
            !RiscvFdtStringListContains(Property, Length, "ok"))
            continue;
        if (HalpRiscvPciHostPresent)
            return FALSE;

        /* QEMU virt marks this host coherent. A different board may still
         * enumerate PCI, but our direct DMA adapter must reject it. */
        Property = RiscvFdtGetProperty(&Fdt, Node, "dma-coherent", &Length);
        if (Property && Length != 0)
            return FALSE;
        Host->DmaCoherent = (Property != NULL);

        if (!RiscvFdtReadU32(&Fdt, Node, "#address-cells", &Value) || Value != 3 ||
            !RiscvFdtReadU32(&Fdt, Node, "#size-cells", &Value) || Value != 2 ||
            !RiscvFdtReadU32(&Fdt, Node, "#interrupt-cells", &Value) || Value != 1)
            return FALSE;

        Property = RiscvFdtGetProperty(&Fdt, Node, "interrupt-map-mask", &Length);
        if (!Property || Length != 4 * sizeof(ULONG))
            return FALSE;
        for (Index = 0; Index < 4; ++Index)
            Host->InterruptMask[Index] = RiscvFdtReadBigEndian32((const UCHAR *)Property + Index * sizeof(ULONG));
        Property = RiscvFdtGetProperty(&Fdt, Node, "interrupt-map", &Length);
        if (!Property || !Length || Length % (6 * sizeof(ULONG)) ||
            Length / (6 * sizeof(ULONG)) > RISCV_PCI_MAX_INTX_ROUTES)
            return FALSE;
        Host->InterruptRouteCount = Length / (6 * sizeof(ULONG));
        for (Entry = 0; Entry < Host->InterruptRouteCount; ++Entry)
        {
            RISCV_PCI_INTX_ROUTE *Route = &Host->InterruptRoutes[Entry];
            const UCHAR *Cells = (const UCHAR *)Property + Entry * 6 * sizeof(ULONG);

            for (Index = 0; Index < 3; ++Index)
                Route->Address[Index] = RiscvFdtReadBigEndian32(Cells + Index * sizeof(ULONG));
            Route->Pin = RiscvFdtReadBigEndian32(Cells + 3 * sizeof(ULONG));
            Phandle = RiscvFdtReadBigEndian32(Cells + 4 * sizeof(ULONG));
            Source = RiscvFdtReadBigEndian32(Cells + 5 * sizeof(ULONG));
            if (Route->Pin < 1 || Route->Pin > 4 ||
                !HalpRiscvPlicHasSource(Phandle, Source))
                return FALSE;
            Route->Source = Source;
        }
        Property = RiscvFdtGetProperty(&Fdt, Node, "reg", &Length);
        if (!Property || Length != 4 * sizeof(ULONG) ||
            !RiscvFdtReadCells(Property, Length, 0, 2, &Host->ConfigAddress) ||
            !RiscvFdtReadCells(Property, Length, 2, 2, &Host->ConfigSize) ||
            !HalpRiscvDeviceRangeValid(Host->ConfigAddress, Host->ConfigSize) ||
            (Host->ConfigAddress & (RISCV_PCI_BUS_SIZE - 1)))
            return FALSE;

        Host->FirstBus = 0;
        Host->LastBus = 255;
        Property = RiscvFdtGetProperty(&Fdt, Node, "bus-range", &Length);
        if (Property)
        {
            if (Length != 2 * sizeof(ULONG))
                return FALSE;
            Host->FirstBus = RiscvFdtReadBigEndian32(Property);
            Host->LastBus = RiscvFdtReadBigEndian32((const UCHAR *)Property + sizeof(ULONG));
        }
        if (Host->FirstBus > Host->LastBus || Host->LastBus > 255 ||
            Host->ConfigSize < (ULONG64)(Host->LastBus - Host->FirstBus + 1) * RISCV_PCI_BUS_SIZE)
            return FALSE;
        /* Only map the buses the firmware assigned, not unused reg padding. */
        Host->ConfigSize = (ULONG64)(Host->LastBus - Host->FirstBus + 1) * RISCV_PCI_BUS_SIZE;

        Property = RiscvFdtGetProperty(&Fdt, Node, "linux,pci-domain", &Length);
        if (Property)
        {
            if (Length != sizeof(ULONG))
                return FALSE;
            Host->Segment = RiscvFdtReadBigEndian32(Property);
            if (Host->Segment > MAXUSHORT)
                return FALSE;
        }

        Property = RiscvFdtGetProperty(&Fdt, Node, "ranges", &Length);
        if (!Property || !Length || Length % (7 * sizeof(ULONG)) ||
            Length / (7 * sizeof(ULONG)) > RISCV_PCI_MAX_RANGES)
            return FALSE;
        Host->RangeCount = Length / (7 * sizeof(ULONG));
        for (Index = 0; Index < Host->RangeCount; ++Index)
        {
            RISCV_PCI_RANGE *Range = &Host->Ranges[Index];
            ULONG Flags = RiscvFdtReadBigEndian32((const UCHAR *)Property + Index * 7 * sizeof(ULONG));
            ULONG Space = (Flags >> 24) & 3;

            if ((Flags & ~0x43000000UL) || Space == 0 ||
                !RiscvFdtReadCells(Property, Length, Index * 7 + 1, 2, &Range->BusAddress) ||
                !RiscvFdtReadCells(Property, Length, Index * 7 + 3, 2, &Range->PhysicalAddress) ||
                !RiscvFdtReadCells(Property, Length, Index * 7 + 5, 2, &Range->Size) ||
                !HalpRiscvDeviceRangeValid(Range->PhysicalAddress, Range->Size) ||
                Range->Size - 1 > MAXULONGLONG - Range->BusAddress ||
                (Space != 3 && (Range->BusAddress > MAXULONG || Range->Size - 1 > MAXULONG - Range->BusAddress)) ||
                HalpRiscvRangesOverlap(Range->PhysicalAddress, Range->Size, Host->ConfigAddress, Host->ConfigSize))
                return FALSE;
            Range->AddressSpace = (Space == 1);
            for (Other = 0; Other < Index; ++Other)
            {
                RISCV_PCI_RANGE *Previous = &Host->Ranges[Other];

                if (HalpRiscvRangesOverlap(Range->PhysicalAddress, Range->Size, Previous->PhysicalAddress, Previous->Size) ||
                    (Range->AddressSpace == Previous->AddressSpace &&
                     HalpRiscvRangesOverlap(Range->BusAddress, Range->Size, Previous->BusAddress, Previous->Size)))
                    return FALSE;
            }
        }
        HalpRiscvPciHostPresent = TRUE;
    }
    return TRUE;
}

BOOLEAN
NTAPI
HalpRiscvIsDeviceMemory(PHYSICAL_ADDRESS Address, SIZE_T Length)
{
    RISCV_PCI_HOST *Host = &HalpRiscvPciHost;
    ULONG Index;

    if (HalpRiscvRtcIsDeviceMemory(Address, Length) ||
        HalpRiscvPlicIsDeviceMemory(Address, Length))
        return TRUE;
    if (!HalpRiscvPciHostPresent || Address.QuadPart < 0)
        return FALSE;
    if (HalpRiscvRangeContains(Host->ConfigAddress, Host->ConfigSize, Address.QuadPart, Length))
        return TRUE;
    for (Index = 0; Index < Host->RangeCount; ++Index)
    {
        RISCV_PCI_RANGE *Range = &Host->Ranges[Index];

        if (HalpRiscvRangeContains(Range->PhysicalAddress, Range->Size, Address.QuadPart, Length))
            return TRUE;
    }
    return FALSE;
}

BOOLEAN
HalpRiscvMapPciConfig(VOID)
{
    PHYSICAL_ADDRESS Address;

    if (!HalpRiscvPciHostPresent)
        return TRUE;
    Address.QuadPart = HalpRiscvPciHost.ConfigAddress;
    HalpRiscvPciHost.ConfigMapping = MmMapIoSpace(Address, HalpRiscvPciHost.ConfigSize, MmNonCached);
    return HalpRiscvPciHost.ConfigMapping != NULL;
}

BOOLEAN
HalpRiscvGetPciResource(PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource)
{
    if (!HalpRiscvPciHost.ConfigMapping)
        return FALSE;
    Resource->Type = CmResourceTypeMemory;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Resource->u.Memory.Start.QuadPart = HalpRiscvPciHost.ConfigAddress;
    Resource->u.Memory.Length = (ULONG)HalpRiscvPciHost.ConfigSize;
    return TRUE;
}

BOOLEAN
NTAPI
HalpRiscvGetPciBusRange(PULONG FirstBus, PULONG LastBus)
{
    if (!HalpRiscvPciHostPresent || !FirstBus || !LastBus)
        return FALSE;
    *FirstBus = HalpRiscvPciHost.FirstBus;
    *LastBus = HalpRiscvPciHost.LastBus;
    return TRUE;
}

BOOLEAN
HalpRiscvPciDmaCoherent(VOID)
{
    return HalpRiscvPciHostPresent && HalpRiscvPciHost.DmaCoherent;
}

BOOLEAN
NTAPI
HalQueryPciRoutedInterrupt(ULONG Segment, ULONG Bus, ULONG Device,
                            ULONG Function, ULONG Pin, PULONG Gsi)
{
    RISCV_PCI_HOST *Host = &HalpRiscvPciHost;
    ULONG Address[3], Index;

    if (!Gsi || !HalpRiscvPciHostPresent || Segment != Host->Segment ||
        Bus < Host->FirstBus || Bus > Host->LastBus || Device > 31 ||
        Function > 7 || Pin < 1 || Pin > 4)
        return FALSE;

    Address[0] = (Bus << 16) | (Device << 11) | (Function << 8);
    Address[1] = Address[2] = 0;
    for (Index = 0; Index < Host->InterruptRouteCount; ++Index)
    {
        const RISCV_PCI_INTX_ROUTE *Route = &Host->InterruptRoutes[Index];

        if (((Address[0] ^ Route->Address[0]) & Host->InterruptMask[0]) == 0 &&
            ((Address[1] ^ Route->Address[1]) & Host->InterruptMask[1]) == 0 &&
            ((Address[2] ^ Route->Address[2]) & Host->InterruptMask[2]) == 0 &&
            ((Pin ^ Route->Pin) & Host->InterruptMask[3]) == 0)
        {
            *Gsi = Route->Source;
            return TRUE;
        }
    }
    return FALSE;
}

BOOLEAN
NTAPI
HalQueryPciMsiSupport(ULONG Segment, UCHAR Bus, PBOOLEAN Supported,
                       PULONG OscStatusFlags, PULONG OscControlGranted,
                       PUSHORT EffectiveSegment, PULONG OscMaskedControls)
{
    UNREFERENCED_PARAMETER(Bus);
    if (Supported)
        *Supported = FALSE;
    if (OscStatusFlags)
        *OscStatusFlags = 0;
    if (OscControlGranted)
        *OscControlGranted = 0;
    if (EffectiveSegment)
        *EffectiveSegment = (USHORT)Segment;
    if (OscMaskedControls)
        *OscMaskedControls = 0;
    /* The supervisor PLIC has no MSI receiver on this QEMU virt machine. */
    return FALSE;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(INTERFACE_TYPE InterfaceType, ULONG BusNumber, PHYSICAL_ADDRESS BusAddress, PULONG AddressSpace, PPHYSICAL_ADDRESS TranslatedAddress)
{
    RISCV_PCI_HOST *Host = &HalpRiscvPciHost;
    ULONG Index;

    if (!AddressSpace || !TranslatedAddress || *AddressSpace > 1 || BusAddress.QuadPart < 0)
        return FALSE;
    if (InterfaceType == Internal && BusNumber == 0 && *AddressSpace == 0 && (ULONG64)BusAddress.QuadPart < RISCV_PHYSICAL_LIMIT)
    {
        *TranslatedAddress = BusAddress;
        return TRUE;
    }
    if (InterfaceType != PCIBus || !HalpRiscvPciHostPresent ||
        (BusNumber >> 8) != Host->Segment || (BusNumber & 255) < Host->FirstBus || (BusNumber & 255) > Host->LastBus)
        return FALSE;
    for (Index = 0; Index < Host->RangeCount; ++Index)
    {
        RISCV_PCI_RANGE *Range = &Host->Ranges[Index];

        if (*AddressSpace == Range->AddressSpace && HalpRiscvRangeContains(Range->BusAddress, Range->Size, BusAddress.QuadPart, 1))
        {
            TranslatedAddress->QuadPart = Range->PhysicalAddress + (BusAddress.QuadPart - Range->BusAddress);
            *AddressSpace = 0; /* PCI I/O is also CPU MMIO on this platform. */
            return TRUE;
        }
    }
    return FALSE;
}

static ULONG
HalpRiscvAccessPciConfig(BOOLEAN Write, ULONG BusNumber, ULONG SlotNumber, PVOID Buffer, ULONG Offset, ULONG Length)
{
    RISCV_PCI_HOST *Host = &HalpRiscvPciHost;
    PCI_SLOT_NUMBER Slot;
    PUCHAR Bytes = Buffer;
    volatile UCHAR *Config;
    KIRQL OldIrql;
    ULONG Done = 0;

    Slot.u.AsULONG = SlotNumber;
    if (!Buffer || !Length || !Host->ConfigMapping || Slot.u.bits.Reserved ||
        Offset >= RISCV_PCI_CONFIG_SIZE || (BusNumber >> 8) != Host->Segment ||
        (BusNumber & 255) < Host->FirstBus || (BusNumber & 255) > Host->LastBus)
        return 0;
    Length = min(Length, RISCV_PCI_CONFIG_SIZE - Offset);
    Config = Host->ConfigMapping + ((BusNumber & 255) - Host->FirstBus) * RISCV_PCI_BUS_SIZE +
             (Slot.u.bits.DeviceNumber << 15) + (Slot.u.bits.FunctionNumber << 12);
    OldIrql = KeAcquireSpinLockRaiseToDpc(&Host->Lock);
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");

    /* The legacy HAL API must not reconfigure a PCI bridge's common header.
     * The PCI bus driver's eventual raw configuration interface is separate. */
    if (Write && Offset < 256 && (Config[FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType)] & 0x7f) == PCI_BRIDGE_TYPE)
    {
        KeReleaseSpinLock(&Host->Lock, OldIrql);
        return 0;
    }
    Config += Offset;
    while (Done < Length)
    {
        ULONG Width = 1, Value = 0, Index;

        if (!((Offset + Done) & 3) && Length - Done >= 4)
            Width = 4;
        else if (!((Offset + Done) & 1) && Length - Done >= 2)
            Width = 2;
        if (Write)
        {
            for (Index = 0; Index < Width; ++Index)
                Value |= (ULONG)Bytes[Done + Index] << (Index * 8);
            if (Width == 4)
                *(volatile ULONG *)(Config + Done) = Value;
            else if (Width == 2)
                *(volatile USHORT *)(Config + Done) = (USHORT)Value;
            else
                Config[Done] = (UCHAR)Value;
        }
        else
        {
            if (Width == 4)
                Value = *(volatile ULONG *)(Config + Done);
            else if (Width == 2)
                Value = *(volatile USHORT *)(Config + Done);
            else
                Value = Config[Done];
            for (Index = 0; Index < Width; ++Index)
                Bytes[Done + Index] = (UCHAR)(Value >> (Index * 8));
        }
        Done += Width;
    }
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    KeReleaseSpinLock(&Host->Lock, OldIrql);
    return Done;
}

ULONG
NTAPI
HalGetBusDataByOffset(BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, PVOID Buffer, ULONG Offset, ULONG Length)
{
    if (BusDataType != PCIConfiguration)
        return 0;
    return HalpRiscvAccessPciConfig(FALSE, BusNumber, SlotNumber, Buffer, Offset, Length);
}

ULONG
NTAPI
HalGetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, PVOID Buffer, ULONG Length)
{
    return HalGetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0, Length);
}

ULONG
NTAPI
HalSetBusDataByOffset(BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, PVOID Buffer, ULONG Offset, ULONG Length)
{
    if (BusDataType != PCIConfiguration)
        return 0;
    return HalpRiscvAccessPciConfig(TRUE, BusNumber, SlotNumber, Buffer, Offset, Length);
}

ULONG
NTAPI
HalSetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, PVOID Buffer, ULONG Length)
{
    return HalSetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0, Length);
}
