/*
 * PROJECT:     ReactOS SD/MMC Storage Class Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IRP_MJ_READ and IRP_MJ_WRITE dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "sffdisk.h"

#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
SffdiskMapBusStatus(
    _In_ NTSTATUS BusStatus)
{
    if (NT_SUCCESS(BusStatus))
    {
        return BusStatus;
    }

    switch (BusStatus)
    {
        case STATUS_IO_TIMEOUT:
        case STATUS_SD_CMD_TIMEOUT:
        case STATUS_SD_DATA_TIMEOUT:
            return STATUS_DEVICE_DATA_ERROR;

        case STATUS_DEVICE_NOT_CONNECTED:
        case STATUS_SD_CARD_NOT_DETECTED:
        case STATUS_SD_CARD_REMOVED:
            return STATUS_NO_MEDIA_IN_DEVICE;

        case STATUS_SD_CMD_CRC_ERROR:
        case STATUS_SD_DATA_CRC_ERROR:
            return STATUS_CRC_ERROR;

        case STATUS_SD_WRITE_PROTECTED:
            return STATUS_MEDIA_WRITE_PROTECTED;

        default:
            return BusStatus;
    }
}

/**
 * @brief Perform a single read or write transfer to the SD/eMMC card.
 *
 * Submits the appropriate SD command (CMD17/CMD18 for reads, CMD24/CMD25
 * for writes) via the SD bus interface.
 *
 * @param[in] DeviceExtension  Pointer to the sffdisk device extension.
 * @param[in] StartSector      First sector to read or write.
 * @param[in] SectorCount      Number of sectors to transfer.
 * @param[in] Mdl              MDL describing the data buffer.
 * @param[in] TransferLength   Total bytes to transfer.
 * @param[in] IsWrite          TRUE for a write operation, FALSE for a read.
 *
 * @return NTSTATUS from the bus driver.
 */
static
NTSTATUS
SffdiskPerformTransfer(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount,
    _In_ PMDL Mdl,
    _In_ ULONG TransferLength,
    _In_ BOOLEAN IsWrite)
{
    SDBUS_REQUEST_PACKET Srb;
    ULONG Argument;

    SD_INIT_REQUEST_PACKET(&Srb, SDRF_DEVICE_COMMAND);

    /*
     * Set up the command descriptor based on transfer direction and block count.
     *
     * Read:
     *   CMD17 (READ_SINGLE_BLOCK)   - for 1 sector
     *   CMD18 (READ_MULTIPLE_BLOCK) - for >1 sectors
     *
     * Write:
     *   CMD24 (WRITE_BLOCK)           - for 1 sector
     *   CMD25 (WRITE_MULTIPLE_BLOCK)  - for >1 sectors
     */
    if (IsWrite)
    {
        if (SectorCount == 1)
        {
            Srb.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_WRITE_BLOCK;
            Srb.Parameters.DeviceCommand.CmdDesc.TransferType = SDTT_SINGLE_BLOCK;
        }
        else
        {
            Srb.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_WRITE_MULTIPLE_BLOCK;
            Srb.Parameters.DeviceCommand.CmdDesc.TransferType = SDTT_MULTI_BLOCK;
        }
        Srb.Parameters.DeviceCommand.CmdDesc.TransferDirection = SDTD_WRITE;
    }
    else
    {
        if (SectorCount == 1)
        {
            Srb.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_READ_SINGLE_BLOCK;
            Srb.Parameters.DeviceCommand.CmdDesc.TransferType = SDTT_SINGLE_BLOCK;
        }
        else
        {
            Srb.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_READ_MULTIPLE_BLOCK;
            Srb.Parameters.DeviceCommand.CmdDesc.TransferType = SDTT_MULTI_BLOCK;
        }
        Srb.Parameters.DeviceCommand.CmdDesc.TransferDirection = SDTD_READ;
    }

    Srb.Parameters.DeviceCommand.CmdDesc.CmdClass = SDCC_STANDARD;
    Srb.Parameters.DeviceCommand.CmdDesc.ResponseType = SDRT_1;

    /*
     * Address argument:
     *   - SDHC/SDXC/eMMC (HighCapacity): block address (sector number)
     *   - SDSC (standard capacity): byte address (sector number * 512)
     */
    if (DeviceExtension->HighCapacity)
    {
        Argument = (ULONG)StartSector;
    }
    else
    {
        Argument = (ULONG)(StartSector * DeviceExtension->BytesPerSector);
    }

    Srb.Parameters.DeviceCommand.Argument = Argument;
    Srb.Parameters.DeviceCommand.Mdl = Mdl;
    Srb.Parameters.DeviceCommand.Length = TransferLength;

    return SffdiskMapBusStatus(
        SdBusSubmitRequest(DeviceExtension->BusInterface.Context, &Srb));
}

static
NTSTATUS
SffdiskTransferWindow(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_ PMDL MasterMdl,
    _In_ PVOID MasterVa,
    _In_ ULONG MasterLength,
    _In_ ULONG WindowOffset,
    _In_ ULONG WindowLength,
    _In_ ULONGLONG StartSector,
    _In_ ULONG SectorCount,
    _In_ BOOLEAN IsWrite)
{
    PMDL TransferMdl;
    PMDL PartialMdl;
    NTSTATUS Status;

    TransferMdl = MasterMdl;
    PartialMdl = NULL;

    if (WindowOffset != 0 || WindowLength != MasterLength)
    {
        PartialMdl = IoAllocateMdl((PUCHAR)MasterVa + WindowOffset,
                                   WindowLength,
                                   FALSE,
                                   FALSE,
                                   NULL);
        if (PartialMdl == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        IoBuildPartialMdl(MasterMdl,
                          PartialMdl,
                          (PUCHAR)MasterVa + WindowOffset,
                          WindowLength);
        TransferMdl = PartialMdl;
    }

    Status = SffdiskPerformTransfer(DeviceExtension,
                                    StartSector,
                                    SectorCount,
                                    TransferMdl,
                                    WindowLength,
                                    IsWrite);

    if (PartialMdl != NULL)
    {
        IoFreeMdl(PartialMdl);
    }

    return Status;
}

/* DISPATCH FUNCTION **********************************************************/

/**
 * @brief Handle IRP_MJ_READ and IRP_MJ_WRITE for the SD disk device.
 *
 * Validates alignment and size requirements, then performs the transfer
 * by submitting SD bus commands. Large transfers are split into chunks
 * of SFFDISK_MAX_TRANSFER_SIZE.
 *
 * @param[in]     DeviceObject  Pointer to the device object (our FDO).
 * @param[in,out] Irp           Pointer to the read/write I/O request packet.
 *
 * @return STATUS_SUCCESS on success, or an appropriate NTSTATUS error code.
 */
NTSTATUS
NTAPI
SffdiskReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;
    ULONGLONG ByteOffset;
    ULONG TransferLength;
    ULONGLONG StartSector;
    ULONG SectorCount;
    BOOLEAN IsWrite;
    PMDL Mdl;
    PVOID MdlVa;
    ULONG BytesDone;
    ULONG ChunkBytes;
    ULONG ChunkSectors;
    ULONGLONG CurrentSector;

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    DPRINT("SffdiskReadWrite: %s Offset=0x%I64x Length=%lu\n",
           (IrpSp->MajorFunction == IRP_MJ_WRITE) ? "WRITE" : "READ",
           (ULONGLONG)IrpSp->Parameters.Read.ByteOffset.QuadPart,
           IrpSp->Parameters.Read.Length);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IsWrite = (IrpSp->MajorFunction == IRP_MJ_WRITE);

    if (!DeviceExtension->Present)
    {
        Status = STATUS_NO_MEDIA_IN_DEVICE;
        goto Complete;
    }

    if ((DeviceObject->Flags & DO_VERIFY_VOLUME) &&
        !(IrpSp->Flags & SL_OVERRIDE_VERIFY_VOLUME))
    {
        Status = STATUS_VERIFY_REQUIRED;
        goto Complete;
    }

    if (!DeviceExtension->MediaPresent)
    {
        DeviceObject->Flags |= DO_VERIFY_VOLUME;
        Status = STATUS_NO_MEDIA_IN_DEVICE;
        goto Complete;
    }

    /*
     * Check write protection for write requests.
     */
    if (IsWrite && DeviceExtension->WriteProtected)
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Complete;
    }

    ByteOffset = (ULONGLONG)IrpSp->Parameters.Read.ByteOffset.QuadPart;
    TransferLength = IrpSp->Parameters.Read.Length;

    /*
     * Zero-length transfers succeed immediately.
     */
    if (TransferLength == 0)
    {
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        goto Complete;
    }

    /*
     * Validate alignment: both offset and length must be sector-aligned.
     */
    if ((ByteOffset % DeviceExtension->BytesPerSector) != 0 ||
        (TransferLength % DeviceExtension->BytesPerSector) != 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    StartSector = ByteOffset / DeviceExtension->BytesPerSector;
    SectorCount = TransferLength / DeviceExtension->BytesPerSector;

    /*
     * Validate that the transfer does not exceed the card capacity.
     */
    if (StartSector + SectorCount > DeviceExtension->TotalSectors)
    {
        Status = STATUS_NONEXISTENT_SECTOR;
        goto Complete;
    }

    if (StartSector >= 0x100000000ULL ||
        (StartSector + SectorCount) > 0x100000000ULL)
    {
        Status = STATUS_NONEXISTENT_SECTOR;
        goto Complete;
    }

    /*
     * Get the MDL provided by the I/O manager (we are a DO_DIRECT_IO device).
     * Use MmGetMdlVirtualAddress for IoBuildPartialMdl — it requires the
     * address to be within the MDL's original VA range, NOT the system mapping.
     */
    Mdl = Irp->MdlAddress;
    if (Mdl == NULL)
    {
        DPRINT1("SffdiskReadWrite: MDL is NULL for %lu byte %s!\n",
                TransferLength, IsWrite ? "write" : "read");
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    MdlVa = MmGetMdlVirtualAddress(Mdl);

    /*
     * Perform the transfer in chunks to stay within the maximum transfer size.
     * For each chunk, we create a partial MDL and submit the SD command.
     */
    BytesDone = 0;
    CurrentSector = StartSector;
    Status = STATUS_SUCCESS;

    DPRINT("SffdiskReadWrite: Starting chunked transfer, Sectors=%lu MdlVa=%p\n",
           SectorCount, MdlVa);

    while (BytesDone < TransferLength)
    {
        ULONG RemainingBytes = TransferLength - BytesDone;

        ChunkBytes = (RemainingBytes > SFFDISK_MAX_TRANSFER_SIZE)
                         ? SFFDISK_MAX_TRANSFER_SIZE
                         : RemainingBytes;
        ChunkSectors = ChunkBytes / DeviceExtension->BytesPerSector;

        DPRINT("SffdiskReadWrite: Chunk sector=%I64u count=%lu bytes=%lu\n",
               CurrentSector, ChunkSectors, ChunkBytes);

        Status = SffdiskTransferWindow(DeviceExtension,
                                       Mdl,
                                       MdlVa,
                                       TransferLength,
                                       BytesDone,
                                       ChunkBytes,
                                       CurrentSector,
                                       ChunkSectors,
                                       IsWrite);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SffdiskReadWrite: Transfer failed at sector %I64u (0x%08lx)\n",
                    CurrentSector, Status);
            break;
        }

        BytesDone += ChunkBytes;
        CurrentSector += ChunkSectors;
    }

    Irp->IoStatus.Information = BytesDone;

    DPRINT("SffdiskReadWrite: Done %lu bytes, Status=0x%08lx\n", BytesDone, Status);

Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, (NT_SUCCESS(Status) ? IO_DISK_INCREMENT : IO_NO_INCREMENT));
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

    return Status;
}
