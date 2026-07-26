/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

NTSTATUS
FileRecord::CopyData(_In_    AttributeType Type,
                     _In_    PWSTR Name,
                     _In_    PUCHAR Buffer,
                     _Inout_ PULONG Length,
                     _In_    ULONGLONG Offset)
{
    PAttribute Attr = GetAttribute(Type, Name);
    BOOLEAN WofHandled;
    NTSTATUS Status;

    if (!Attr)
        return STATUS_NOT_FOUND;

    if (Type == TypeData && !Name)
    {
        Status = TryCopyWofCompressedData(Attr,
                                          Buffer,
                                          Length,
                                          Offset,
                                          &WofHandled);
        if (!NT_SUCCESS(Status) || WofHandled)
            return Status;
    }

    // Basic guard: resident data must have valid DataOffset/length window
    if (!Attr->IsNonResident)
    {
        const ULONG dataStart = Attr->Resident.DataOffset;
        const ULONG dataEnd = dataStart + Attr->Resident.DataLength;
        if (dataStart < 0x18 || dataEnd > Attr->Length)
            return STATUS_FILE_CORRUPT_ERROR;
    }
    return CopyData(Attr, Buffer, Length, Offset);
}

NTSTATUS
FileRecord::ReadAttributeAlloc(
    _In_ PAttribute Attr,
    _Outptr_result_bytebuffer_(*Length) PUCHAR* Buffer,
    _Out_ PULONG Length)
{
    ULONGLONG AttributeSize;
    ULONG BytesRemaining;
    NTSTATUS Status;

    *Buffer = NULL;
    *Length = 0;
    AttributeSize = GetAttributeDataSize(Attr);
    if (AttributeSize == 0)
        return STATUS_SUCCESS;

    if (AttributeSize > MAXULONG)
        return STATUS_FILE_TOO_LARGE;

    *Buffer = new(NonPagedPool) UCHAR[(ULONG)AttributeSize];
    if (!*Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    BytesRemaining = (ULONG)AttributeSize;
    Status = CopyData(Attr, *Buffer, &BytesRemaining, 0);
    if (!NT_SUCCESS(Status) || BytesRemaining != 0)
    {
        delete[] *Buffer;
        *Buffer = NULL;
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    *Length = (ULONG)AttributeSize;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::CopyData(_In_    PAttribute Attr,
                     _In_    PUCHAR Buffer,
                     _Inout_ PULONG Length,
                     _In_    ULONGLONG Offset)
{
    NTSTATUS Status;
    ULONG BytesToRead, BytesRead, BytesFromRuns;
    PDataRun Head, CurrentDR;

    ASSERT(Buffer);
    ASSERT(Attr);

    if (*Length == 0)
    {
        // If we aren't reading anything, don't read anything.
        return STATUS_SUCCESS;
    }

    if (!(Attr->IsNonResident)) // Attribute is resident.
    {
        if (Offset >= Attr->Resident.DataLength)
        {
            // Don't read past the file data.
            DPRINT1("Offset is greater than or equal to the data size!\n");
            DPRINT1("Offset: %ld, Data Size: %ld\n", Offset, Attr->Resident.DataLength);
            return STATUS_END_OF_FILE;
        }

        // Determine number of bytes we need to copy.
        BytesToRead = min((Attr->Resident.DataLength - Offset), (*Length));

        // Copy attribute data into buffer.
        RtlCopyMemory(Buffer,
                      GetResidentDataPointer(Attr) + Offset,
                      BytesToRead);

        BytesRead = BytesToRead;
    }

    else // Attribute is nonresident.
    {
        /* Encrypted streams require EFS key handling that is not implemented. */
        if (Attr->Flags & ATTR_ENCRYPTED)
            return STATUS_NOT_IMPLEMENTED;

        if (Attr->Flags & ATTR_COMPRESSION_MASK)
        {
            if ((Attr->Flags & ATTR_COMPRESSION_MASK) != ATTR_COMPRESSED)
                return STATUS_NOT_IMPLEMENTED;
            return CopyCompressedData(Attr, Buffer, Length, Offset);
        }

        if (Offset >= Attr->NonResident.DataSize)
        {
            // Don't read past the file data.
            DPRINT1("Offset >= DataSize! (%ld >= %ld)\n", Offset, Attr->NonResident.DataSize);
            return STATUS_END_OF_FILE;
        }

        // Determine number of bytes we need to copy.
        BytesToRead = min((Attr->NonResident.DataSize - Offset), (*Length));

        BytesFromRuns = Offset < Attr->NonResident.InitalizedDataSize
            ? (ULONG)min((ULONGLONG)BytesToRead,
                         Attr->NonResident.InitalizedDataSize - Offset)
            : 0;

        /* Runs belong to the per-record cache, so no cleanup happens on
         * any path below.
         */
        Head = NULL;
        if (BytesFromRuns != 0)
        {
            Head = GetCachedDataRuns(Attr);
            if (!Head)
                return STATUS_FILE_CORRUPT_ERROR;
        }
        CurrentDR = Head;
        BytesRead = 0;

        while (CurrentDR && BytesRead < BytesFromRuns)
        {
            ULONGLONG BytesInRun = GetRunSize(CurrentDR);

            if (Offset >= BytesInRun)
            {
                Offset -= BytesInRun;
            }

            else
            {
                ULONGLONG BytesAvailable = BytesInRun - Offset;
                ULONG BytesRemaining = BytesFromRuns - BytesRead;
                ULONG Chunk = BytesAvailable < BytesRemaining
                    ? (ULONG)BytesAvailable
                    : BytesRemaining;

                if (CurrentDR->IsSparse)
                {
                    RtlZeroMemory(Buffer + BytesRead, Chunk);
                }
                else
                {
                    Status = DiskVolume->ReadVolume(
                        GetOffset(CurrentDR->LCN) + Offset,
                        Chunk,
                        Buffer + BytesRead);
                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("Failed to read attribute contents!\n");
                        return Status;
                    }
                }

                BytesRead += Chunk;
                Offset = 0;
            }

            CurrentDR = CurrentDR->NextRun;
        }

        if (BytesRead != BytesFromRuns)
        {
            DPRINT1("Failed to copy file data!\n");
            return STATUS_NOT_FOUND;
        }

        if (BytesRead < BytesToRead)
        {
            RtlZeroMemory(Buffer + BytesRead, BytesToRead - BytesRead);
            BytesRead = BytesToRead;
        }
    }

    // Adjust length for caller
    *Length -= BytesRead;

    return STATUS_SUCCESS;
}
