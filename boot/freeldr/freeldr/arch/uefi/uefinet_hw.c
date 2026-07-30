/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     LattePanda Mu RTL8168 preparation for UEFI network boot
 */

#include <uefildr.h>
#include <PciIo.h>
#include <SimpleNetwork.h>

#include "uefinetp.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#if defined(_M_IX86) || defined(_M_AMD64)

#define RTL8168_VENDOR_ID 0x10ec
#define RTL8168_DEVICE_ID 0x8168

#define RTL8168_MAC_REGISTER 0x00
#define RTL8168_9346CR       0x50
#define RTL8168_EEM_NORMAL   0x00
#define RTL8168_EEM_WRITE    0xc0

static EFI_GUID EfiPciIoGuid = EFI_PCI_IO_PROTOCOL_GUID;
static const UINT8 LattePandaFallbackMac[6] =
    {0x02, 0xde, 0xad, 0x10, 0xec, 0x68};

typedef struct _RTL_REGISTER_IO
{
    EFI_PCI_IO_PROTOCOL_ACCESS *Access;
    UINT8 BarIndex;
} RTL_REGISTER_IO;

static BOOLEAN
UefiMacIsZero(
    _In_reads_(Length) const UINT8 *Address,
    _In_ UINTN Length)
{
    UINTN Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Address[Index] != 0)
            return FALSE;
    }

    return TRUE;
}

static EFI_STATUS
UefiRtlReadMac(
    _In_ EFI_PCI_IO_PROTOCOL *PciIo,
    _Out_writes_(6) UINT8 *Address,
    _Out_ RTL_REGISTER_IO *Registers)
{
    EFI_STATUS Status;
    UINT8 BarIndex;

    for (BarIndex = 0; BarIndex < 6; BarIndex++)
    {
        RtlZeroMemory(Address, 6);
        Status = PciIo->Io.Read(
            PciIo,
            EfiPciIoWidthUint8,
            BarIndex,
            RTL8168_MAC_REGISTER,
            6,
            Address);
        if (!EFI_ERROR(Status))
        {
            Registers->Access = &PciIo->Io;
            Registers->BarIndex = BarIndex;
            return EFI_SUCCESS;
        }
    }

    for (BarIndex = 0; BarIndex < 6; BarIndex++)
    {
        RtlZeroMemory(Address, 6);
        Status = PciIo->Mem.Read(
            PciIo,
            EfiPciIoWidthUint8,
            BarIndex,
            RTL8168_MAC_REGISTER,
            6,
            Address);
        if (!EFI_ERROR(Status))
        {
            Registers->Access = &PciIo->Mem;
            Registers->BarIndex = BarIndex;
            return EFI_SUCCESS;
        }
    }

    return Status;
}

static EFI_STATUS
UefiRtlWriteRegister(
    _In_ EFI_PCI_IO_PROTOCOL *PciIo,
    _In_ RTL_REGISTER_IO *Registers,
    _In_ UINT64 Offset,
    _In_ UINTN Length,
    _In_reads_bytes_(Length) VOID *Buffer)
{
    return Registers->Access->Write(
        PciIo,
        EfiPciIoWidthUint8,
        Registers->BarIndex,
        Offset,
        Length,
        Buffer);
}

static EFI_STATUS
UefiRtlProgramMac(
    _In_ EFI_PCI_IO_PROTOCOL *PciIo,
    _In_ RTL_REGISTER_IO *Registers)
{
    EFI_STATUS Status;
    EFI_STATUS RestoreStatus;
    UINT8 RegisterValue;
    UINT8 ReadBack[sizeof(LattePandaFallbackMac)];
    UINTN Index;

    RegisterValue = RTL8168_EEM_WRITE;
    Status = UefiRtlWriteRegister(
        PciIo, Registers, RTL8168_9346CR, 1, &RegisterValue);
    if (EFI_ERROR(Status))
        return Status;

    Status = UefiRtlWriteRegister(
        PciIo,
        Registers,
        RTL8168_MAC_REGISTER,
        sizeof(LattePandaFallbackMac),
        (VOID *)LattePandaFallbackMac);

    RegisterValue = RTL8168_EEM_NORMAL;
    RestoreStatus = UefiRtlWriteRegister(
        PciIo, Registers, RTL8168_9346CR, 1, &RegisterValue);

    if (EFI_ERROR(Status))
        return Status;
    if (EFI_ERROR(RestoreStatus))
        return RestoreStatus;

    RtlZeroMemory(ReadBack, sizeof(ReadBack));
    Status = Registers->Access->Read(
        PciIo,
        EfiPciIoWidthUint8,
        Registers->BarIndex,
        RTL8168_MAC_REGISTER,
        sizeof(ReadBack),
        ReadBack);
    if (EFI_ERROR(Status))
        return Status;

    for (Index = 0; Index < sizeof(ReadBack); Index++)
    {
        if (ReadBack[Index] != LattePandaFallbackMac[Index])
            return EFI_DEVICE_ERROR;
    }

    return EFI_SUCCESS;
}

VOID
UefiLattePandaPrepareNic(VOID)
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles = NULL;
    UINTN HandleCount = 0;
    UINTN Index;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &EfiPciIoGuid, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status))
        return;

    for (Index = 0; Index < HandleCount; Index++)
    {
        EFI_PCI_IO_PROTOCOL *PciIo = NULL;
        UINT32 PciId = 0;
        UINT16 VendorId;
        UINT16 DeviceId;
        UINT64 SupportedAttributes;
        UINT8 MacAddress[6];
        RTL_REGISTER_IO Registers;

        Status = GlobalSystemTable->BootServices->HandleProtocol(
            Handles[Index], &EfiPciIoGuid, (VOID **)&PciIo);
        if (EFI_ERROR(Status) || !PciIo)
            continue;

        Status = PciIo->Pci.Read(
            PciIo, EfiPciIoWidthUint32, 0, 1, &PciId);
        if (EFI_ERROR(Status))
            continue;

        VendorId = (UINT16)PciId;
        DeviceId = (UINT16)(PciId >> 16);
        if (VendorId != RTL8168_VENDOR_ID || DeviceId != RTL8168_DEVICE_ID)
            continue;


        SupportedAttributes = 0;
        Status = PciIo->Attributes(
            PciIo,
            EfiPciIoAttributeOperationSupported,
            0,
            &SupportedAttributes);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI RTL8168: cannot query PCI attributes (Status %llx)\n",
                  (unsigned long long)Status);
            continue;
        }

        Status = PciIo->Attributes(
            PciIo,
            EfiPciIoAttributeOperationEnable,
            SupportedAttributes & EFI_PCI_DEVICE_ENABLE,
            NULL);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI RTL8168: cannot enable PCI device (Status %llx)\n",
                  (unsigned long long)Status);
            continue;
        }

        RtlZeroMemory(&Registers, sizeof(Registers));
        Status = UefiRtlReadMac(PciIo, MacAddress, &Registers);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI RTL8168: cannot read MAC registers (Status %llx)\n",
                  (unsigned long long)Status);
            continue;
        }

        if (UefiMacIsZero(MacAddress, sizeof(MacAddress)))
        {
            Status = UefiRtlProgramMac(PciIo, &Registers);
            if (EFI_ERROR(Status))
            {
                TRACE("UEFI RTL8168: cannot program fallback MAC (Status %llx)\n",
                      (unsigned long long)Status);
                continue;
            }

            TRACE("UEFI RTL8168: programmed fallback MAC 02:de:ad:10:ec:68\n");
        }
    }

    GlobalSystemTable->BootServices->FreePool(Handles);
}

VOID
UefiLattePandaFixMac(
    _In_ EFI_SIMPLE_NETWORK_PROTOCOL *Snp)
{
    EFI_STATUS Status;
    EFI_MAC_ADDRESS Address;
    UINTN AddressLength;

    if (!Snp || !Snp->Mode)
    {
        return;
    }

    AddressLength = Snp->Mode->HwAddressSize;
    if (AddressLength > sizeof(Snp->Mode->CurrentAddress.Addr))
        AddressLength = sizeof(Snp->Mode->CurrentAddress.Addr);


    if (!UefiMacIsZero(Snp->Mode->CurrentAddress.Addr, AddressLength))
    {
        return;
    }

    RtlZeroMemory(&Address, sizeof(Address));
    RtlCopyMemory(
        Address.Addr,
        LattePandaFallbackMac,
        sizeof(LattePandaFallbackMac));

    Status = Snp->StationAddress(Snp, FALSE, &Address);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI RTL8168: StationAddress failed (Status %llx)\n",
              (unsigned long long)Status);
        return;
    }

    TRACE("UEFI RTL8168: SNP fallback MAC applied\n");
}

#else

/*
 * Boards whose NIC needs no pre-UNDI repair. The Raspberry Pi 5 GEM driver
 * reports the MAC the firmware programmed, so both hooks are no-ops.
 */
VOID
UefiLattePandaPrepareNic(VOID)
{
}

VOID
UefiLattePandaFixMac(
    _In_ EFI_SIMPLE_NETWORK_PROTOCOL *Snp)
{
    UNREFERENCED_PARAMETER(Snp);
}

#endif /* defined(_M_IX86) || defined(_M_AMD64) */
