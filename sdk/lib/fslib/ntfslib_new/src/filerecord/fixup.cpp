/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

/* NOTE: For more information, see: https://flatcap.github.io/linux-ntfs/ntfs/concepts/fixup.html
 *
 * Abbreviations used
 *     USA: Update Sequence Array
 *     USN: Update Sequence Number
 */

static NTSTATUS
ValidateFixup(_In_ PNTFSRecordHeader Header,
              _In_ ULONG RecordSize,
              _In_ ULONG BytesPerSector)
{
    ULONG UsaSize = Header->SizeOfUpdateSequence * sizeof(USHORT);

    if (RecordSize < BytesPerSector ||
        (RecordSize % BytesPerSector) != 0 ||
        Header->SizeOfUpdateSequence != RecordSize / BytesPerSector + 1 ||
        Header->UpdateSequenceOffset > RecordSize ||
        UsaSize > RecordSize - Header->UpdateSequenceOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfsApplyFixup(_Inout_ PNTFSRecordHeader Header,
               _In_ ULONG RecordSize,
               _In_ ULONG BytesPerSector)
{
    NTSTATUS Status = ValidateFixup(Header, RecordSize, BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;

    PUSHORT Usa = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) + Header->UpdateSequenceOffset);
    PUSHORT SectorTail = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) +
        BytesPerSector - sizeof(USHORT));

    for (ULONG Index = 1;
         Index < Header->SizeOfUpdateSequence;
         Index++)
    {
        if (*SectorTail != Usa[0])
        {
            DPRINT1("Update-sequence mismatch: %u read, %u expected.\n",
                    *SectorTail,
                    Usa[0]);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        *SectorTail = Usa[Index];
        SectorTail = reinterpret_cast<PUSHORT>(
            reinterpret_cast<PUCHAR>(SectorTail) + BytesPerSector);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtfsCommitFixup(_Inout_ PNTFSRecordHeader Header,
                _In_ ULONG RecordSize,
                _In_ ULONG BytesPerSector)
{
    NTSTATUS Status = ValidateFixup(Header, RecordSize, BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;

    PUSHORT Usa = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) + Header->UpdateSequenceOffset);
    PUSHORT SectorTail = reinterpret_cast<PUSHORT>(
        reinterpret_cast<PUCHAR>(Header) +
        BytesPerSector - sizeof(USHORT));

    Usa[0]++;
    if (Usa[0] == 0)
        Usa[0]++;

    for (ULONG Index = 1;
         Index < Header->SizeOfUpdateSequence;
         Index++)
    {
        Usa[Index] = *SectorTail;
        *SectorTail = Usa[0];
        SectorTail = reinterpret_cast<PUSHORT>(
            reinterpret_cast<PUCHAR>(SectorTail) + BytesPerSector);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::CommitFixup()
{
    return NtfsCommitFixup(&Header->Header,
                           Header->AllocatedSize,
                           DiskVolume->BytesPerSector);
}

NTSTATUS
FileRecord::ApplyFixup()
{
    return NtfsApplyFixup(&Header->Header,
                          Header->AllocatedSize,
                          DiskVolume->BytesPerSector);
}
