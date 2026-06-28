/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDIO transport: CMD52/CMD53, backplane window, RAM write
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

PVOID
CywAllocate(
    _In_ ULONG Size)
{
    return ExAllocatePoolZero(NonPagedPool, Size, CYW_TAG);
}

VOID
CywFree(
    _In_ PVOID Buffer)
{
    if (Buffer != NULL)
    {
        ExFreePoolWithTag(Buffer, CYW_TAG);
    }
}

NTSTATUS
CywSdioOpen(
    _In_ PCYW_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = SdBusOpenInterface(Adapter->Pdo,
                                &Adapter->SdBus,
                                sizeof(SDBUS_INTERFACE_STANDARD),
                                SDBUS_INTERFACE_VERSION);
    if (NT_SUCCESS(Status))
    {
        Adapter->SdBusOpened = TRUE;
    }

    return Status;
}

VOID
CywSdioClose(
    _In_ PCYW_ADAPTER Adapter)
{
    if (Adapter->SdBusOpened && Adapter->SdBus.InterfaceDereference != NULL)
    {
        Adapter->SdBus.InterfaceDereference(Adapter->SdBus.Context);
    }
    Adapter->SdBusOpened = FALSE;
}

NTSTATUS
CywSdioReadByte(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Value)
{
    SDBUS_REQUEST_PACKET Packet;
    NTSTATUS Status;

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_DIRECT);
    Packet.Parameters.IoDirect.Function = Function;
    Packet.Parameters.IoDirect.Write = FALSE;
    Packet.Parameters.IoDirect.RawMode = FALSE;
    Packet.Parameters.IoDirect.Address = Address;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
    if (NT_SUCCESS(Status) && Value != NULL)
    {
        *Value = Packet.Parameters.IoDirect.DataOut;
    }

    return Status;
}

NTSTATUS
CywSdioWriteByte(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ UCHAR Value)
{
    SDBUS_REQUEST_PACKET Packet;

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_DIRECT);
    Packet.Parameters.IoDirect.Function = Function;
    Packet.Parameters.IoDirect.Write = TRUE;
    Packet.Parameters.IoDirect.RawMode = FALSE;
    Packet.Parameters.IoDirect.Address = Address;
    Packet.Parameters.IoDirect.DataIn = Value;

    return SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
}

static
NTSTATUS
CywSdioRw(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ BOOLEAN Increment,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN BlockMode,
    _In_ ULONG BlockSize)
{
    SDBUS_REQUEST_PACKET Packet;
    PMDL Mdl;
    NTSTATUS Status;

    Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);
    if (Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    MmBuildMdlForNonPagedPool(Mdl);

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_EXTENDED);
    Packet.Parameters.IoExtended.Function = Function;
    Packet.Parameters.IoExtended.Write = Write;
    Packet.Parameters.IoExtended.BlockMode = BlockMode;
    Packet.Parameters.IoExtended.Increment = Increment;
    Packet.Parameters.IoExtended.Address = Address;
    Packet.Parameters.IoExtended.BlockCount = BlockMode ? (Length / BlockSize) : Length;
    Packet.Parameters.IoExtended.BlockSize = BlockSize;
    Packet.Parameters.IoExtended.Mdl = Mdl;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);

    IoFreeMdl(Mdl);
    return Status;
}

NTSTATUS
CywSdioReadBytes(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    return CywSdioRw(Adapter, Function, FALSE, TRUE, Address, Buffer, Length, FALSE, 0);
}

NTSTATUS
CywSdioReadBlocks(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _Out_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG BlockSize)
{
    return CywSdioRw(Adapter, Function, FALSE, TRUE, Address, Buffer, Length, TRUE, BlockSize);
}

NTSTATUS
CywSdioWriteBytes(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    return CywSdioRw(Adapter, Function, TRUE, TRUE, Address, Buffer, Length, FALSE, 0);
}

NTSTATUS
CywSdioWriteBlocks(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONG BlockSize)
{
    return CywSdioRw(Adapter, Function, TRUE, TRUE, Address, Buffer, Length, TRUE, BlockSize);
}

NTSTATUS
CywSdioEnableFunction(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function)
{
    NTSTATUS Status;
    UCHAR Enable;
    UCHAR Ready;
    ULONG Retry;

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_IOEx, &Enable);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Enable |= (1u << Function);
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_IOEx, Enable);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Retry = 0; Retry < 500; Retry++)
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS, SDIO_CCCR_IORx, &Ready);
        if (NT_SUCCESS(Status) && (Ready & (1u << Function)))
        {
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(1000);
    }

    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
CywSdioSetBlockSize(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG BlockSize)
{
    ULONG Fbr = (ULONG)Function * 0x100;
    NTSTATUS Status;

    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, Fbr + 0x10, (UCHAR)(BlockSize & 0xFF));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, Fbr + 0x11, (UCHAR)((BlockSize >> 8) & 0xFF));
}

NTSTATUS
CywBackplaneSetWindow(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    ULONG Window = Address & SBSDIO_SBWINDOW_MASK;
    NTSTATUS Status;

    if (Window == Adapter->CurrentBackplaneWindow)
    {
        return STATUS_SUCCESS;
    }

    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                              SBSDIO_FUNC1_SBADDRLOW, (UCHAR)((Window >> 8) & 0x80));
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                  SBSDIO_FUNC1_SBADDRMID, (UCHAR)((Window >> 16) & 0xFF));
    }
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                  SBSDIO_FUNC1_SBADDRHIGH, (UCHAR)((Window >> 24) & 0xFF));
    }

    if (NT_SUCCESS(Status))
    {
        Adapter->CurrentBackplaneWindow = Window;
    }
    return Status;
}

NTSTATUS
CywBackplaneReadl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value)
{
    NTSTATUS Status;
    ULONG Offset;

    Status = CywBackplaneSetWindow(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE, Offset, Adapter->ControlBuffer, 4);
    if (NT_SUCCESS(Status) && Value != NULL)
    {
        *Value = ((PULONG)Adapter->ControlBuffer)[0];
    }
    return Status;
}

NTSTATUS
CywBackplaneWritel(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    NTSTATUS Status;
    ULONG Offset;

    Status = CywBackplaneSetWindow(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ((PULONG)Adapter->ControlBuffer)[0] = Value;
    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    return CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE, Offset, Adapter->ControlBuffer, 4);
}

NTSTATUS
CywBackplaneReadlSc(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value,
    _Inout_ PUCHAR Scratch)
{
    NTSTATUS Status;
    ULONG Offset;

    Status = CywBackplaneSetWindow(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE, Offset, Scratch, 4);
    if (NT_SUCCESS(Status) && Value != NULL)
    {
        *Value = ((PULONG)Scratch)[0];
    }
    return Status;
}

NTSTATUS
CywBackplaneWritelSc(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value,
    _Inout_ PUCHAR Scratch)
{
    NTSTATUS Status;
    ULONG Offset;

    Status = CywBackplaneSetWindow(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ((PULONG)Scratch)[0] = Value;
    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    return CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE, Offset, Scratch, 4);
}

NTSTATUS
CywRamWrite(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    NTSTATUS Status;
    ULONG WindowOffset;
    ULONG Chunk;
    ULONG Transfer;

    while (Length > 0)
    {
        Status = CywBackplaneSetWindow(Adapter, Address);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        WindowOffset = Address & SBSDIO_SB_OFT_ADDR_MASK;
        Chunk = SBSDIO_SB_OFT_ADDR_LIMIT - WindowOffset;
        if (Chunk > Length)
        {
            Chunk = Length;
        }

        Transfer = Chunk;
        if (Transfer >= CYW_F1_BLOCKSIZE)
        {
            ULONG Blocks = Transfer / CYW_F1_BLOCKSIZE;
            if (Blocks > 64)
            {
                Blocks = 64;
            }
            Transfer = Blocks * CYW_F1_BLOCKSIZE;
            Status = CywSdioRw(Adapter, CYW_SDIO_FUNC_BACKPLANE, TRUE, TRUE,
                               WindowOffset | SBSDIO_SB_ACCESS_2_4B_FLAG,
                               Buffer, Transfer, TRUE, CYW_F1_BLOCKSIZE);
        }
        else
        {
            Status = CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                                       WindowOffset | SBSDIO_SB_ACCESS_2_4B_FLAG,
                                       Buffer, Transfer);
        }
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Address += Transfer;
        Buffer += Transfer;
        Length -= Transfer;
    }

    return STATUS_SUCCESS;
}
