/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared SCSI helpers for crash-dump storage providers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Every dump-capable storage provider (SCSIPORT, STORPORT, USBSTOR) builds
 * the same WRITE(10)/WRITE(16) command and applies the same sector-size
 * contract. Keep that logic here so the providers cannot drift apart.
 *
 * The includer supplies the SCSI definitions (CDB, SCSIOP_*) through whichever
 * of scsi.h / storport.h its miniport model uses -- those two headers conflict,
 * so this one must not pick either.
 */

#pragma once

/*
 * The dump writer transfers at most one page per request and addresses the
 * volume in whole sectors, so the sector size must be a power of two within
 * [512, PAGE_SIZE].
 */
FORCEINLINE BOOLEAN RosDumpIsValidSectorSize(_In_ ULONG BytesPerSector)
{
    return (BytesPerSector >= 512) && (BytesPerSector <= PAGE_SIZE) && ((BytesPerSector & (BytesPerSector - 1)) == 0);
}

/*
 * Build a WRITE command for BlockCount blocks at BlockAddress and return its
 * CDB length. WRITE(16) is used only when the LBA exceeds a 32-bit field.
 */
FORCEINLINE UCHAR RosDumpBuildWriteCdb(_Out_ PCDB Cdb, _In_ ULONG64 BlockAddress, _In_ ULONG BlockCount)
{
    if (BlockAddress <= MAXULONG)
    {
        ULONG LowPart = (ULONG)BlockAddress;

        Cdb->CDB10.OperationCode = SCSIOP_WRITE;
        Cdb->CDB10.LogicalBlockByte0 = ((PFOUR_BYTE)&LowPart)->Byte3;
        Cdb->CDB10.LogicalBlockByte1 = ((PFOUR_BYTE)&LowPart)->Byte2;
        Cdb->CDB10.LogicalBlockByte2 = ((PFOUR_BYTE)&LowPart)->Byte1;
        Cdb->CDB10.LogicalBlockByte3 = ((PFOUR_BYTE)&LowPart)->Byte0;
        Cdb->CDB10.TransferBlocksMsb = ((PFOUR_BYTE)&BlockCount)->Byte1;
        Cdb->CDB10.TransferBlocksLsb = ((PFOUR_BYTE)&BlockCount)->Byte0;
        return 10;
    }

    Cdb->CDB16.OperationCode = SCSIOP_WRITE16;
    REVERSE_BYTES_QUAD(&Cdb->CDB16.LogicalBlock, &BlockAddress);
    REVERSE_BYTES(&Cdb->CDB16.TransferLength, &BlockCount);
    return 16;
}
