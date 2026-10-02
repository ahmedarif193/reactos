/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PReP PCI configuration and bus address translation
 *
 * The Raven host bridge implements type 1 configuration cycles through ports
 * 0xCF8/0xCFC. PCI memory appears at PciMemoryPhysicalBase; PCI and ISA ports
 * are reached with the port routines.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static KSPIN_LOCK HalpPpcPciLock;

BOOLEAN
NTAPI
HalpPpcGetPciBusRange(_Out_ PULONG FirstBus, _Out_ PULONG LastBus)
{
    *FirstBus = 0;
    *LastBus = 0;
    return TRUE;
}

BOOLEAN
HalpPpcGetPciResource(_Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource)
{
    UNREFERENCED_PARAMETER(Resource);
    return FALSE;
}

BOOLEAN
HalpPpcPciDmaCoherent(VOID)
{
    /* The 604 snoops PCI bus-master traffic. */
    return TRUE;
}

NTSTATUS
NTAPI
HalQueryPciRoutedInterrupt(ULONG Segment, ULONG Bus, ULONG Device, ULONG Function, ULONG Pin, PULONG Gsi)
{
    UNREFERENCED_PARAMETER(Segment);
    UNREFERENCED_PARAMETER(Bus);
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Function);
    UNREFERENCED_PARAMETER(Pin);
    UNREFERENCED_PARAMETER(Gsi);
    /* The PCI driver uses the interrupt line register. */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalQueryPciMsiSupport(ULONG Segment, UCHAR Bus, PBOOLEAN Supported, PULONG OscStatusFlags, PULONG OscControlGranted, PUSHORT EffectiveSegment, PULONG OscMaskedControls)
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
    return STATUS_NOT_SUPPORTED;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(INTERFACE_TYPE InterfaceType, ULONG BusNumber, PHYSICAL_ADDRESS BusAddress, PULONG AddressSpace, PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(BusNumber);

    if (!AddressSpace || !TranslatedAddress || (*AddressSpace > 1) || (BusAddress.QuadPart < 0))
        return FALSE;
    if (InterfaceType == Internal)
    {
        *TranslatedAddress = BusAddress;
        return TRUE;
    }
    if (*AddressSpace == 1)
    {
        /* Ports stay ports: the port routines add the I/O window. */
        if (BusAddress.QuadPart > 0xFFFFFF)
            return FALSE;
        *TranslatedAddress = BusAddress;
        return TRUE;
    }
    if (BusAddress.QuadPart >= 0x3F000000)
        return FALSE;
    TranslatedAddress->QuadPart = HalpPpcPciMemoryBase + BusAddress.QuadPart;
    return TRUE;
}

static
ULONG
HalpPpcAccessPciConfig(
    _In_ BOOLEAN Write,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PCI_SLOT_NUMBER Slot;
    PUCHAR Bytes = Buffer;
    KIRQL OldIrql;
    ULONG Done;

    Slot.u.AsULONG = SlotNumber;
    if ((BusNumber != 0) || (Offset >= 256))
        return 0;
    if (Length > 256 - Offset)
        Length = 256 - Offset;

    KeAcquireSpinLock(&HalpPpcPciLock, &OldIrql);
    for (Done = 0; Done < Length; Done++)
    {
        ULONG Register = Offset + Done;
        ULONG Address = 0x80000000UL | (BusNumber << 16) | (Slot.u.bits.DeviceNumber << 11) | (Slot.u.bits.FunctionNumber << 8) | (Register & 0xFC);

        WRITE_PORT_ULONG((PULONG)PCI_CONFIG_ADDRESS, Address);
        if (Write)
            WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)(PCI_CONFIG_DATA + (Register & 3)), Bytes[Done]);
        else
            Bytes[Done] = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)(PCI_CONFIG_DATA + (Register & 3)));
    }
    KeReleaseSpinLock(&HalpPpcPciLock, OldIrql);

    /* An empty slot reads as all ones. */
    if (!Write && (Offset == 0) && (Length >= 2) && (*(PUSHORT)Buffer == 0xFFFF))
        return 2;
    return Length;
}

ULONG
NTAPI
HalGetBusDataByOffset(BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, PVOID Buffer, ULONG Offset, ULONG Length)
{
    if (BusDataType != PCIConfiguration)
        return 0;
    return HalpPpcAccessPciConfig(FALSE, BusNumber, SlotNumber, Buffer, Offset, Length);
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
    return HalpPpcAccessPciConfig(TRUE, BusNumber, SlotNumber, Buffer, Offset, Length);
}

ULONG
NTAPI
HalSetBusData(BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, PVOID Buffer, ULONG Length)
{
    return HalSetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0, Length);
}
