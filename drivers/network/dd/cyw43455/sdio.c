/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SDIO transport: CMD52/CMD53, backplane window, RAM write
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

#define CYW_SDIO_MAX_FUNCTION          7
#define CYW_SDIO_MAX_ADDRESS           0x1FFFFUL
#define CYW_SDIO_MAX_COUNT             512
#define CYW_SDIO_MAX_BLOCK_SIZE        0x0FFF

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

    if (Adapter == NULL || Adapter->Pdo == NULL || Adapter->SdBusOpened)
    {
        return STATUS_INVALID_PARAMETER;
    }

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
    RtlZeroMemory(&Adapter->SdBus, sizeof(Adapter->SdBus));
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

    if (Adapter == NULL || !Adapter->SdBusOpened ||
        Adapter->SdBus.Context == NULL || Value == NULL ||
        Function > CYW_SDIO_MAX_FUNCTION || Address > CYW_SDIO_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_DIRECT);
    Packet.Parameters.IoDirect.Function = Function;
    Packet.Parameters.IoDirect.Write = FALSE;
    Packet.Parameters.IoDirect.RawMode = FALSE;
    Packet.Parameters.IoDirect.Address = Address;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
    if (NT_SUCCESS(Status))
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

    if (Adapter == NULL || !Adapter->SdBusOpened ||
        Adapter->SdBus.Context == NULL ||
        Function > CYW_SDIO_MAX_FUNCTION || Address > CYW_SDIO_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_DIRECT);
    Packet.Parameters.IoDirect.Function = Function;
    Packet.Parameters.IoDirect.Write = TRUE;
    Packet.Parameters.IoDirect.RawMode = FALSE;
    Packet.Parameters.IoDirect.Address = Address;
    Packet.Parameters.IoDirect.DataIn = Value;

    return SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);
}

NTSTATUS
CywRegisterDmaBuf(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Buffer,
    _In_ ULONG Size)
{
    PCYW_DMA_BUF Buf;
    SIZE_T MdlSize;
    ULONG PageCount;

    if (Adapter == NULL || Buffer == NULL || Size == 0 ||
        Adapter->DmaBufCount >= CYW_DMA_BUF_COUNT ||
        (ULONG_PTR)Buffer > MAXULONG_PTR - (Size - 1))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Buf = &Adapter->DmaBufs[Adapter->DmaBufCount];

    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Buffer, Size);
    MdlSize = sizeof(MDL) + sizeof(PFN_NUMBER) * (SIZE_T)PageCount;
    if (MdlSize > MAXULONG)
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    Buf->Mdl = CywAllocate((ULONG)MdlSize);
    if (Buf->Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Buf->Buffer = Buffer;
    Buf->Size = Size;
    Adapter->DmaBufCount++;
    return STATUS_SUCCESS;
}

VOID
CywFreeDmaBufs(
    _In_ PCYW_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < Adapter->DmaBufCount; i++)
    {
        CywFree(Adapter->DmaBufs[i].Mdl);
        Adapter->DmaBufs[i].Mdl = NULL;
        Adapter->DmaBufs[i].Buffer = NULL;
        Adapter->DmaBufs[i].Size = 0;
    }
    Adapter->DmaBufCount = 0;
}

/* Transfers overwhelmingly target one of the persistent adapter buffers, each
 * of which is serialized by its owning lock. Those reuse a preallocated MDL,
 * re-pointed at the requested range, instead of allocating one per command. */
static
PMDL
CywAcquireMdl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Owned)
{
    PMDL Mdl;
    ULONG i;
    ULONG_PTR BufferAddress;

    if (Owned == NULL)
    {
        return NULL;
    }
    *Owned = FALSE;

    if (Adapter == NULL || Buffer == NULL || Length == 0 ||
        (ULONG_PTR)Buffer > MAXULONG_PTR - (Length - 1))
    {
        return NULL;
    }

    BufferAddress = (ULONG_PTR)Buffer;
    for (i = 0; i < Adapter->DmaBufCount; i++)
    {
        PCYW_DMA_BUF Buf = &Adapter->DmaBufs[i];
        ULONG_PTR RegisteredAddress = (ULONG_PTR)Buf->Buffer;
        SIZE_T Offset;

        if (BufferAddress >= RegisteredAddress)
        {
            Offset = BufferAddress - RegisteredAddress;
            if (Offset <= Buf->Size && Length <= Buf->Size - Offset)
            {
                Mdl = Buf->Mdl;
                MmInitializeMdl(Mdl, Buffer, Length);
                MmBuildMdlForNonPagedPool(Mdl);
                return Mdl;
            }
        }
    }

    Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);
    if (Mdl != NULL)
    {
        MmBuildMdlForNonPagedPool(Mdl);
    }
    *Owned = TRUE;
    return Mdl;
}

static
NTSTATUS
CywSdioRw(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN BlockMode,
    _In_ ULONG BlockSize)
{
    SDBUS_REQUEST_PACKET Packet;
    PMDL Mdl;
    BOOLEAN OwnedMdl;
    NTSTATUS Status;
    ULONG Count;
    BOOLEAN Increment;

    if (Adapter == NULL || !Adapter->SdBusOpened ||
        Adapter->SdBus.Context == NULL || Buffer == NULL || Length == 0 ||
        Function > CYW_SDIO_MAX_FUNCTION || Address > CYW_SDIO_MAX_ADDRESS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (BlockMode)
    {
        if (BlockSize == 0 || BlockSize > CYW_SDIO_MAX_BLOCK_SIZE ||
            (Length % BlockSize) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Count = Length / BlockSize;
    }
    else
    {
        if (BlockSize != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Count = Length;
    }

    if (Count == 0 || Count > CYW_SDIO_MAX_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Function 2 is a receive FIFO; reads must keep the CMD53 address fixed. */
    Increment = !(Function == CYW_SDIO_FUNC_RADIO && !Write);
    if (Increment && Length - 1 > CYW_SDIO_MAX_ADDRESS - Address)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mdl = CywAcquireMdl(Adapter, Buffer, Length, &OwnedMdl);
    if (Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_EXTENDED);
    Packet.Parameters.IoExtended.Function = Function;
    Packet.Parameters.IoExtended.Write = Write;
    Packet.Parameters.IoExtended.BlockMode = BlockMode;
    Packet.Parameters.IoExtended.Increment = Increment;
    Packet.Parameters.IoExtended.Address = Address;
    Packet.Parameters.IoExtended.BlockCount = Count;
    Packet.Parameters.IoExtended.BlockSize = BlockSize;
    Packet.Parameters.IoExtended.Mdl = Mdl;

    Status = SdBusSubmitRequest(Adapter->SdBus.Context, &Packet);

    if (OwnedMdl)
    {
        IoFreeMdl(Mdl);
    }
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
    return CywSdioRw(Adapter, Function, FALSE, Address, Buffer, Length, FALSE, 0);
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
    return CywSdioRw(Adapter, Function, FALSE, Address, Buffer, Length, TRUE, BlockSize);
}

NTSTATUS
CywSdioWriteBytes(
    _In_ PCYW_ADAPTER Adapter,
    _In_ UCHAR Function,
    _In_ ULONG Address,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    return CywSdioRw(Adapter, Function, TRUE, Address, Buffer, Length, FALSE, 0);
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
    return CywSdioRw(Adapter, Function, TRUE, Address, Buffer, Length, TRUE, BlockSize);
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
    LARGE_INTEGER Delay;

    if (Adapter == NULL || Function == 0 ||
        Function > CYW_SDIO_MAX_FUNCTION)
    {
        return STATUS_INVALID_PARAMETER;
    }

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
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (Ready & (1u << Function))
        {
            return STATUS_SUCCESS;
        }
        Delay.QuadPart = -10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    Enable &= (UCHAR)~(1u << Function);
    (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                           SDIO_CCCR_IOEx, Enable);
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
    UCHAR OldLow;
    UCHAR OldHigh;
    UCHAR VerifyLow;
    UCHAR VerifyHigh;

    if (Adapter == NULL || Function > CYW_SDIO_MAX_FUNCTION ||
        BlockSize == 0 || BlockSize > CYW_SDIO_MAX_BLOCK_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                             Fbr + 0x10, &OldLow);
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                                 Fbr + 0x11, &OldHigh);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, Fbr + 0x10, (UCHAR)(BlockSize & 0xFF));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS, Fbr + 0x11, (UCHAR)((BlockSize >> 8) & 0xFF));
    if (!NT_SUCCESS(Status))
    {
        (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                               Fbr + 0x10, OldLow);
        return Status;
    }

    Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                             Fbr + 0x10, &VerifyLow);
    if (NT_SUCCESS(Status))
    {
        Status = CywSdioReadByte(Adapter, CYW_SDIO_FUNC_BUS,
                                 Fbr + 0x11, &VerifyHigh);
    }
    if (!NT_SUCCESS(Status) || VerifyLow != (UCHAR)BlockSize ||
        VerifyHigh != (UCHAR)(BlockSize >> 8))
    {
        (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                               Fbr + 0x10, OldLow);
        (VOID)CywSdioWriteByte(Adapter, CYW_SDIO_FUNC_BUS,
                               Fbr + 0x11, OldHigh);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
CywBackplaneSetWindowLocked(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    ULONG Window = Address & SBSDIO_SBWINDOW_MASK;
    NTSTATUS Status;

    if (Window == Adapter->CurrentBackplaneWindow)
    {
        return STATUS_SUCCESS;
    }

    /* A partial programming failure makes the old cached value untrustworthy. */
    Adapter->CurrentBackplaneWindow = MAXULONG;

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
CywBackplaneSetWindow(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address)
{
    NTSTATUS Status;

    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    Status = CywBackplaneSetWindowLocked(Adapter, Address);
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
}

NTSTATUS
CywBackplaneReadl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value)
{
    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return CywBackplaneReadlSc(Adapter, Address, Value,
                               Adapter->ControlBuffer);
}

NTSTATUS
CywBackplaneWritel(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    if (Adapter == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return CywBackplaneWritelSc(Adapter, Address, Value,
                                Adapter->ControlBuffer);
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

    if (Adapter == NULL || Value == NULL || Scratch == NULL ||
        (Address & SBSDIO_SB_OFT_ADDR_MASK) >
            SBSDIO_SB_OFT_ADDR_LIMIT - sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    Status = CywBackplaneSetWindowLocked(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
        return Status;
    }

    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    Status = CywSdioReadBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE, Offset, Scratch, 4);
    if (NT_SUCCESS(Status) && Value != NULL)
    {
        *Value = ((PULONG)Scratch)[0];
    }
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
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

    if (Adapter == NULL || Scratch == NULL ||
        (Address & SBSDIO_SB_OFT_ADDR_MASK) >
            SBSDIO_SB_OFT_ADDR_LIMIT - sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    Status = CywBackplaneSetWindowLocked(Adapter, Address);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
        return Status;
    }

    ((PULONG)Scratch)[0] = Value;
    Offset = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;
    Status = CywSdioWriteBytes(Adapter, CYW_SDIO_FUNC_BACKPLANE,
                               Offset, Scratch, 4);
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
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

    if (Adapter == NULL || (Buffer == NULL && Length != 0) ||
        (Length != 0 && Address > MAXULONG - (Length - 1)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive,
                          KernelMode, FALSE, NULL);
    while (Length > 0)
    {
        Status = CywBackplaneSetWindowLocked(Adapter, Address);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
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
            /* A count of 512 is encoded as zero in CMD53, allowing each
             * 32-KiB backplane window to be written in one request. */
            if (Blocks > CYW_SDIO_MAX_COUNT)
            {
                Blocks = CYW_SDIO_MAX_COUNT;
            }
            Transfer = Blocks * CYW_F1_BLOCKSIZE;
            Status = CywSdioRw(Adapter, CYW_SDIO_FUNC_BACKPLANE, TRUE,
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
            KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
            return Status;
        }

        Address += Transfer;
        Buffer += Transfer;
        Length -= Transfer;
    }

    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return STATUS_SUCCESS;
}
