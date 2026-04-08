/*
 * PROJECT:         ReactOS PCI Bus Driver
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            drivers/bus/pci/intrconn.c
 * PURPOSE:         MSI/MSI-X interrupt programming and connection
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include "pci.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/*
 * PciProgramMsiMessages
 *
 * Programs the MSI capability registers of a PCI device with the given
 * message address and data values. Handles both 32-bit and 64-bit MSI
 * capability layouts.
 *
 * MSI Capability layout:
 *   +0x00: Cap ID (8) | Next (8) | Message Control (16)
 *   +0x04: Message Address (lower 32 bits)
 *   +0x08: Message Address (upper 32, if 64-bit) or Message Data (if 32-bit)
 *   +0x0C: Message Data (if 64-bit)
 *   +0x10: Mask Bits (if per-vector masking, 64-bit)
 *   +0x0C: Mask Bits (if per-vector masking, 32-bit)
 */
NTSTATUS
PciProgramMsiMessages(
    _In_ PPCI_DEVICE Device,
    _In_ PHYSICAL_ADDRESS MessageAddress,
    _In_ USHORT MessageData,
    _In_ ULONG MessageCount)
{
    ULONG CapOffset;
    USHORT MsiControl;
    ULONG AddressLo;
    ULONG AddressHi;
    USHORT MultipleMessageEnable;

    if (Device->MsiCapability == 0)
    {
        DPRINT1("PciProgramMsiMessages: device %04X:%04X has no MSI capability\n",
                Device->PciConfig.VendorID, Device->PciConfig.DeviceID);
        return STATUS_NOT_SUPPORTED;
    }

    CapOffset = Device->MsiCapability;

    /* Read current MSI control */
    PciReadDeviceConfig(Device, &MsiControl, CapOffset + PCI_MSI_FLAGS, sizeof(USHORT));

    /* Compute Multiple Message Enable field (bits 6:4) from requested count */
    MultipleMessageEnable = 0;
    if (MessageCount > 1)
    {
        ULONG Shift = 0;
        ULONG Count = MessageCount;
        while (Count > 1)
        {
            Count >>= 1;
            Shift++;
        }
        if (Shift > 5) Shift = 5; /* Max 32 messages = shift 5 */
        MultipleMessageEnable = (USHORT)(Shift << 4);
    }

    /* Set Multiple Message Enable, preserve other bits */
    MsiControl = (MsiControl & ~(USHORT)PCI_MSI_FLAGS_QSIZE) | MultipleMessageEnable;

    /* Write Message Address (lower 32 bits) */
    AddressLo = MessageAddress.LowPart;
    PciWriteDeviceConfig(Device, &AddressLo, CapOffset + PCI_MSI_ADDRESS_LO, sizeof(ULONG));

    if (MsiControl & PCI_MSI_FLAGS_64BIT)
    {
        /* 64-bit capable: write upper address then data */
        AddressHi = MessageAddress.HighPart;
        PciWriteDeviceConfig(Device, &AddressHi, CapOffset + PCI_MSI_ADDRESS_HI, sizeof(ULONG));
        PciWriteDeviceConfig(Device, &MessageData, CapOffset + PCI_MSI_DATA_64, sizeof(USHORT));
    }
    else
    {
        /* 32-bit: data register is at offset 8 */
        PciWriteDeviceConfig(Device, &MessageData, CapOffset + PCI_MSI_DATA_32, sizeof(USHORT));
    }

    /* Write updated control with message count */
    PciWriteDeviceConfig(Device, &MsiControl, CapOffset + PCI_MSI_FLAGS, sizeof(USHORT));

    DPRINT("PciProgramMsiMessages: device %04X:%04X MSI programmed addr=0x%I64x data=0x%04X count=%lu\n",
           Device->PciConfig.VendorID, Device->PciConfig.DeviceID,
           MessageAddress.QuadPart, MessageData, MessageCount);

    return STATUS_SUCCESS;
}

/*
 * PciEnableMsi
 *
 * Enables MSI for a device by setting the MSI Enable bit in the MSI
 * Message Control register, and disables legacy INTx by setting bit 10
 * in the PCI Command register.
 */
NTSTATUS
PciEnableMsi(
    _In_ PPCI_DEVICE Device)
{
    USHORT MsiControl;
    USHORT Command;
    ULONG CapOffset;

    if (Device->MsiCapability == 0)
        return STATUS_NOT_SUPPORTED;

    CapOffset = Device->MsiCapability;

    /* Read and set MSI Enable (bit 0 of Message Control) */
    PciReadDeviceConfig(Device, &MsiControl, CapOffset + PCI_MSI_FLAGS, sizeof(USHORT));
    MsiControl |= PCI_MSI_FLAGS_ENABLE;
    PciWriteDeviceConfig(Device, &MsiControl, CapOffset + PCI_MSI_FLAGS, sizeof(USHORT));

    /* Disable legacy INTx: set bit 10 of PCI Command register */
    PciReadDeviceConfig(Device, &Command, PCI_COMMAND, sizeof(USHORT));
    Command |= PCI_COMMAND_INTX_DISABLE;
    PciWriteDeviceConfig(Device, &Command, PCI_COMMAND, sizeof(USHORT));

    Device->MsiAllocated = TRUE;

    DPRINT("PciEnableMsi: MSI enabled for device %04X:%04X\n",
           Device->PciConfig.VendorID, Device->PciConfig.DeviceID);

    return STATUS_SUCCESS;
}

/*
 * PciDisableMsi
 *
 * Disables MSI for a device by clearing the MSI Enable bit, and
 * re-enables legacy INTx by clearing bit 10 of PCI Command.
 */
NTSTATUS
PciDisableMsi(
    _In_ PPCI_DEVICE Device)
{
    USHORT MsiControl;
    USHORT Command;
    ULONG CapOffset;

    if (Device->MsiCapability == 0)
        return STATUS_NOT_SUPPORTED;

    CapOffset = Device->MsiCapability;

    /* Clear MSI Enable bit */
    PciReadDeviceConfig(Device, &MsiControl, CapOffset + PCI_MSI_FLAGS, sizeof(USHORT));
    MsiControl &= ~(USHORT)PCI_MSI_FLAGS_ENABLE;
    PciWriteDeviceConfig(Device, &MsiControl, CapOffset + PCI_MSI_FLAGS, sizeof(USHORT));

    /* Re-enable legacy INTx: clear bit 10 of PCI Command register */
    PciReadDeviceConfig(Device, &Command, PCI_COMMAND, sizeof(USHORT));
    Command &= ~(USHORT)PCI_COMMAND_INTX_DISABLE;
    PciWriteDeviceConfig(Device, &Command, PCI_COMMAND, sizeof(USHORT));

    Device->MsiAllocated = FALSE;
    Device->MsiMessageCount = 0;

    DPRINT("PciDisableMsi: MSI disabled for device %04X:%04X\n",
           Device->PciConfig.VendorID, Device->PciConfig.DeviceID);

    return STATUS_SUCCESS;
}

/*
 * PciConnectLegacyInterrupt
 *
 * Marks a PCI device as having its legacy INTx interrupt connected.
 * The actual IoConnectInterruptEx call is handled by the function driver;
 * this tracks the connection state in pci.sys's device structure.
 *
 */
NTSTATUS
PciConnectLegacyInterrupt(
    _In_ PPCI_DEVICE Device)
{
    UCHAR InterruptLine;
    UCHAR InterruptPin;

    InterruptLine = Device->PciConfig.u.type0.InterruptLine;
    InterruptPin = Device->PciConfig.u.type0.InterruptPin;

    if (InterruptPin == 0)
    {
        DPRINT("PciConnectLegacyInterrupt: device %04X:%04X has no interrupt pin\n",
               Device->PciConfig.VendorID, Device->PciConfig.DeviceID);
        return STATUS_SUCCESS;
    }

    Device->InterruptConnected = TRUE;

    DPRINT1("PciConnectLegacyInterrupt: device %04X:%04X INT%c line=%u connected\n",
            Device->PciConfig.VendorID, Device->PciConfig.DeviceID,
            'A' + InterruptPin - 1, InterruptLine);

    return STATUS_SUCCESS;
}

/*
 * PciDisconnectInterrupt
 *
 * Marks a PCI device as having its interrupt disconnected, regardless
 * of whether it was using MSI or legacy INTx.
 *
 */
VOID
PciDisconnectInterrupt(
    _In_ PPCI_DEVICE Device)
{
    if (!Device->InterruptConnected && !Device->MsiAllocated)
        return;

    if (Device->MsiAllocated)
    {
        PciDisableMsi(Device);
    }

    Device->InterruptConnected = FALSE;

    DPRINT("PciDisconnectInterrupt: device %04X:%04X interrupt disconnected\n",
           Device->PciConfig.VendorID, Device->PciConfig.DeviceID);
}

/* EOF */
