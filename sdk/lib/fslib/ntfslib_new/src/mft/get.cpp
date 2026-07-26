/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define MFT_BITMAP_CHUNK_SIZE 0x10000
#define NTFS_FILE_REFERENCE_ORDINAL_MASK \
    ((ULONGLONG)0x0000FFFFFFFFFFFFULL)

static BOOLEAN
IsFileRecordHeaderValid(_In_ PFileRecord File,
                        _In_ ULONG FileRecordSize,
                        _In_ ULONG BytesPerSector)
{
    PFileRecordHeader Header = File->Header;
    ULONG UsaSize;

    if (Header->AllocatedSize > FileRecordSize ||
        Header->AllocatedSize < sizeof(FileRecordHeader) ||
        Header->ActualSize > Header->AllocatedSize ||
        Header->ActualSize < sizeof(FileRecordHeader) ||
        Header->AttributeOffset < sizeof(FileRecordHeader) ||
        (Header->AttributeOffset & 7) != 0 ||
        Header->AllocatedSize < BytesPerSector ||
        (Header->AllocatedSize % BytesPerSector) != 0 ||
        Header->Header.SizeOfUpdateSequence !=
            Header->AllocatedSize / BytesPerSector + 1)
    {
        return FALSE;
    }

    UsaSize = Header->Header.SizeOfUpdateSequence * sizeof(USHORT);
    return Header->Header.UpdateSequenceOffset <= Header->ActualSize &&
           UsaSize <= Header->ActualSize - Header->Header.UpdateSequenceOffset;
}

static BOOLEAN
AreFileRecordAttributesValid(_In_ PFileRecord File)
{
    PAttribute Attribute;
    ULONG DataOffset, MinimumSize, Offset, Remaining;

    Offset = File->Header->AttributeOffset;
    while (Offset < File->Header->ActualSize)
    {
        Remaining = File->Header->ActualSize - Offset;
        if (Remaining < sizeof(ULONG))
            return FALSE;

        Attribute = reinterpret_cast<PAttribute>(File->Data + Offset);
        if (Attribute->AttributeType == TypeAttributeEndMarker)
            return TRUE;

        MinimumSize = Attribute->IsNonResident
            ? ((Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
                ? 0x48
                : 0x40)
            : 0x18;
        if (Remaining < MinimumSize ||
            Attribute->Length < MinimumSize ||
            (Attribute->Length & 7) != 0 ||
            Attribute->Length > Remaining ||
            Attribute->NameOffset > Attribute->Length ||
            Attribute->NameLength * sizeof(WCHAR) >
                Attribute->Length - Attribute->NameOffset)
        {
            return FALSE;
        }

        if (Attribute->IsNonResident)
        {
            DataOffset = Attribute->NonResident.DataRunsOffset;
            if (DataOffset < MinimumSize || DataOffset >= Attribute->Length)
                return FALSE;
        }
        else
        {
            DataOffset = Attribute->Resident.DataOffset;
            if (DataOffset < MinimumSize ||
                DataOffset > Attribute->Length ||
                Attribute->Resident.DataLength >
                    Attribute->Length - DataOffset)
            {
                return FALSE;
            }
        }

        Offset += Attribute->Length;
    }

    return FALSE;
}

// NOTE: This function will search $MFTMirr automatically if needed.
NTSTATUS
MasterFileTable::GetFileRecord(_In_   ULONG FileRecordNumber,
                               _Out_  PFileRecord* File)
{
    NTSTATUS Status;
    ULONG BytesToRead;
    BOOLEAN IsRecordInUse;

    *File = NULL;
    *File = new(PagedPool, TAG_MFT) FileRecord(DiskVolume, FileRecordSize);
    if (!*File)
        return STATUS_INSUFFICIENT_RESOURCES;

    BytesToRead = FileRecordSize;

    if (FileRecordNumber == _MFT)
    {
        // The $MFT file is calculated from the MFT LCN.
        Status = DiskVolume->ReadVolume(MFTDiskOffset,
                                        FileRecordSize,
                                        (*File)->Data);
        goto FileCheck;
    }

    else if (FileRecordNumber == _MFTMirr)
    {
        /*
         * MFT record 1 describes the $MFTMirr file. The bytes at
         * MFTMirrDiskOffset begin with a mirror of MFT record 0, so reading
         * that location as record 1 silently gives us $MFT's runlist.
         */
        Status = MFTDataAttr
            ? MFTFile->CopyData(MFTDataAttr,
                                (*File)->Data,
                                &BytesToRead,
                                FileRecordOffset(FileRecordNumber))
            : STATUS_INVALID_DEVICE_STATE;
        goto FileCheck;
    }

    /* All other files are found by querying the MFT file or if that fails
     * and the file is in $MFTMirr, the MFTMirr file.
     */

    // If the file record is not in use, fail.
    Status = IsFileRecordNumberInUse(FileRecordNumber,
                                     &IsRecordInUse);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to determine if file record number is in use!\n");
        goto Failed;
    }

    if (!IsRecordInUse)
    {
        DPRINT1("File record is not in use!\n");
        Status = STATUS_NOT_FOUND;
        goto Failed;
    }

    // Grab file from $MFT, reusing the cached $DATA attribute.
    Status = MFTDataAttr ?
             MFTFile->CopyData(MFTDataAttr,
                               (*File)->Data,
                               &BytesToRead,
                               FileRecordOffset(FileRecordNumber))
             : STATUS_NOT_FOUND;

FileCheck:
    /* Reject unreadable records, invalid signatures, malformed headers, and
     * unsafe update-sequence windows before applying fixups.
     */
    if (!NT_SUCCESS(Status) ||
        RtlCompareMemory((*File)->Header->Header.TypeID, "FILE", 4) != 4 ||
        !IsFileRecordHeaderValid(*File,
                                 FileRecordSize,
                                 DiskVolume->BytesPerSector))
    {
        DPRINT1("Failed to get file %ld from MFT!\n", FileRecordNumber);
        delete *File;
        *File = NULL;
        if (FileRecordNumber != _MFT && FileRecordNumber != _MFTMirr)
            return GetFileRecordFromMFTMirr(FileRecordNumber, File);
        else
            return STATUS_NOT_FOUND;
    }

    // Apply fixup for the file.
    Status = (*File)->ApplyFixup();

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("File corruption detected!\n");
        goto Failed;
    }

    if (!AreFileRecordAttributesValid(*File))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failed;
    }

    return Status;

Failed:
    delete *File;
    *File = NULL;
    return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
}

NTSTATUS
MasterFileTable::ReadFileRecord(
    _In_ ULONGLONG RequestedFileReference,
    _Out_ PULONGLONG ReturnedFileReference,
    _Out_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    PAttribute BitmapAttribute;
    PFileRecord File = NULL;
    PUCHAR Bitmap = NULL;
    ULONGLONG BitmapDataLength;
    ULONGLONG Candidate;
    ULONGLONG MftDataLength;
    ULONGLONG RecordCount;
    ULONGLONG ByteIndex;
    ULONGLONG ChunkStart;
    ULONGLONG FoundRecord = 0;
    ULONG Capacity;
    ULONG Length;
    ULONG SearchLength;
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    if (!ReturnedFileReference ||
        !BufferLength ||
        (!Buffer && *BufferLength != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Capacity = *BufferLength;
    *BufferLength = FileRecordSize;
    if (!Buffer || Capacity < FileRecordSize)
        return STATUS_BUFFER_TOO_SMALL;
    if (!MFTFile || !MFTDataAttr ||
        !MFTDataAttr->IsNonResident ||
        FileRecordSize == 0)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    MftDataLength =
        MFTDataAttr->NonResident.DataSize;
    if (MftDataLength < FileRecordSize ||
        MftDataLength % FileRecordSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    RecordCount = MftDataLength / FileRecordSize;

    Candidate =
        RequestedFileReference &
        NTFS_FILE_REFERENCE_ORDINAL_MASK;
    if (Candidate >= RecordCount)
        Candidate = RecordCount - 1;
    if (Candidate > MAXULONG)
        Candidate = MAXULONG;

    BitmapAttribute =
        MFTFile->GetAttribute(TypeBitmap, NULL);
    if (!BitmapAttribute)
        return STATUS_FILE_CORRUPT_ERROR;
    BitmapDataLength =
        GetAttributeDataSize(BitmapAttribute);
    if ((Candidate >> 3) >= BitmapDataLength)
        return STATUS_FILE_CORRUPT_ERROR;

    Bitmap =
        new(PagedPool, TAG_MFT)
            UCHAR[MFT_BITMAP_CHUNK_SIZE];
    if (!Bitmap)
        return STATUS_INSUFFICIENT_RESOURCES;

    ByteIndex = Candidate >> 3;
    for (;;)
    {
        ChunkStart =
            ByteIndex -
            (ByteIndex % MFT_BITMAP_CHUNK_SIZE);
        SearchLength =
            (ULONG)(ByteIndex - ChunkStart + 1);
        Length = SearchLength;
        Status = MFTFile->CopyData(
            BitmapAttribute,
            Bitmap,
            &Length,
            ChunkStart);
        if (!NT_SUCCESS(Status) || Length != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }

        while (SearchLength != 0)
        {
            UCHAR Value;
            ULONG Bit;

            SearchLength--;
            Value = Bitmap[SearchLength];
            if (SearchLength ==
                (ULONG)(ByteIndex - ChunkStart))
            {
                Value &= (UCHAR)(
                    (1u << ((Candidate & 7) + 1)) -
                    1u);
            }
            if (Value == 0)
                continue;

            for (Bit = 8; Bit != 0; Bit--)
            {
                if (Value & (1u << (Bit - 1)))
                {
                    FoundRecord =
                        (ChunkStart + SearchLength) * 8 +
                        Bit - 1;
                    Found = TRUE;
                    break;
                }
            }
            if (Found)
                break;
        }
        if (Found)
            break;
        if (ChunkStart == 0)
        {
            Status = STATUS_NOT_FOUND;
            goto Done;
        }

        Candidate = ChunkStart * 8 - 1;
        ByteIndex = Candidate >> 3;
    }

    Status = GetFileRecord((ULONG)FoundRecord,
                           &File);
    if (!NT_SUCCESS(Status))
        goto Done;

    RtlCopyMemory(Buffer, File->Data,
                  FileRecordSize);
    *ReturnedFileReference = FoundRecord;
    *BufferLength = FileRecordSize;
    Status = STATUS_SUCCESS;

Done:
    delete File;
    delete[] Bitmap;
    return Status;
}

NTSTATUS
MasterFileTable::GetFileRecordFromMFTMirr(_In_   ULONG FileRecordNumber,
                                          _Out_  PFileRecord* File)
{
    NTSTATUS Status;
    BOOLEAN IsRecordInUse;
    ULONG BytesToRead;

    *File = NULL;

    if (!IsFileRecordInMFTMirr(FileRecordNumber))
        return STATUS_NOT_FOUND;

    if (FileRecordNumber == _MFT ||
        FileRecordNumber == _MFTMirr)
    {
        // Logic for finding MFT and MFTMirr is in GetFileRecord()
        return GetFileRecord(FileRecordNumber, File);
    }

    // If the file record is not in use, fail.
    Status = IsFileRecordNumberInUse(FileRecordNumber,
                                     &IsRecordInUse);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to determine if file record number is in use!\n");
        return Status;
    }

    if (!IsRecordInUse)
    {
        DPRINT1("File record is not in use!\n");
        return STATUS_NOT_FOUND;
    }

    // File is in MFTMirr. Let's try to get it from there.
    *File = new(PagedPool, TAG_MFT) FileRecord(DiskVolume, FileRecordSize);
    if (!*File)
        return STATUS_INSUFFICIENT_RESOURCES;

    BytesToRead = FileRecordSize;

    // Grab file from $MFTMirr, reusing the cached $DATA attribute.
    Status = MFTMirrDataAttr ?
             MFTMirrFile->CopyData(MFTMirrDataAttr,
                                   (*File)->Data,
                                   &BytesToRead,
                                   FileRecordOffset(FileRecordNumber))
             : STATUS_NOT_FOUND;

    if (!NT_SUCCESS(Status) ||
        RtlCompareMemory((*File)->Header->Header.TypeID, "FILE", 4) != 4 ||
        !IsFileRecordHeaderValid(*File,
                                 FileRecordSize,
                                 DiskVolume->BytesPerSector))
    {
        DPRINT1("Failed to get file %ld from MFT Mirror!\n", FileRecordNumber);
        goto Failed;
    }

    // Apply fixup for the file.
    Status = (*File)->ApplyFixup();

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to apply fixup!\n");
        goto Failed;
    }

    if (!AreFileRecordAttributesValid(*File))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failed;
    }

    return Status;

Failed:
    delete *File;
    *File = NULL;
    return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
}

// Utilizes MFTMirr to find an attribute if the MFT version doesn't have it.
NTSTATUS
MasterFileTable::GetFileAttributeFromFileRecordNumber(_In_  AttributeType Type,
                                                      _In_  PWSTR Name,
                                                      _In_  ULONG FileRecordNumber,
                                                      _Out_ PFileRecord* TargetFile,
                                                      _Out_ PAttribute* TargetAttribute)
{
    NTSTATUS Status;

    *TargetFile = NULL;
    *TargetAttribute = NULL;

    // Grab the file using GetFileRecord().
    Status = GetFileRecord(FileRecordNumber, TargetFile);
    if (!NT_SUCCESS(Status))
        return Status;

    *TargetAttribute = (*TargetFile)->GetAttribute(Type, Name);

    if (!*TargetAttribute)
    {
        // Free current target file.
        delete *TargetFile;
        *TargetFile = NULL;

        // Try getting the attribute using the file from MFTMirr.
        Status = GetFileRecordFromMFTMirr(FileRecordNumber, TargetFile);
        if (!NT_SUCCESS(Status))
            return Status;

        *TargetAttribute = (*TargetFile)->GetAttribute(Type, Name);
        if (!*TargetAttribute)
        {
            // We still don't have the attribute.
            delete *TargetFile;
            *TargetFile = NULL;
            return STATUS_NOT_FOUND;
        }
    }

    return Status;
}

NTSTATUS
MasterFileTable::GetFileRecordFromQuery(_In_ PWCHAR Query,
                                        _Out_ PFileRecord* File,
                                        _In_ BOOLEAN ReturnReparse,
                                        _In_ BOOLEAN OpenFinalReparsePoint,
                                        _Out_opt_ PULONG RemainingNameLength)
{
    NTSTATUS Status;
    PWCHAR NextElement;
    PWCHAR NextSeparator;
    PWCHAR QueryElementPtr;
    Directory CurrentDirectory(DiskVolume);
    PFileRecord CurrentFile;
    ULONGLONG CurrentFileReference;
    ULONGLONG CurrentFRN;
    BOOLEAN HasRemainingName;

    *File = NULL;
    if (RemainingNameLength)
        *RemainingNameLength = 0;

    if (!Query ||
        (ReturnReparse &&
         !RemainingNameLength))
    {
        DPRINT1("GetFileRecordFromQuery() requires a non-NULL query.\n");
        return STATUS_INVALID_PARAMETER;
    }

    // Let's start with the root directory, which is hardcoded.
    if (IsRootFile(Query))
        return GetFileRecord(_Root, File);

    /* Every other file will be inside of the root directory.
     * Parse the query to find the file record.
     */
    Status = GetFileRecord(_Root, &CurrentFile);
    if(!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to get root directory file!\n");
        return STATUS_NOT_FOUND;
    }

    QueryElementPtr = Query;
    while (*QueryElementPtr == L'\\')
        QueryElementPtr++;

    /* A path consisting only of separators denotes the root directory. */
    if (*QueryElementPtr == L'\0')
    {
        *File = CurrentFile;
        return STATUS_SUCCESS;
    }

    while (*QueryElementPtr)
    {
        // Find the directory pointed to by the path
        Status = CurrentDirectory.FindNextFile(CurrentFile,
                                               QueryElementPtr,
                                               &CurrentFileReference);
        if (!NT_SUCCESS(Status))
        {
            delete CurrentFile;
            return Status;
        }

        CurrentFRN = GetFRNFromFileRef(CurrentFileReference);
        delete CurrentFile;
        CurrentFile = NULL;

        if (CurrentFRN > MAXULONG)
            return STATUS_FILE_TOO_LARGE;

        Status = GetFileRecord((ULONG)CurrentFRN,
                               &CurrentFile);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to find file at MFT ID %ld!\n", CurrentFRN);
            return Status;
        }

        if (GetSequenceFromFileRef(CurrentFileReference) != 0 &&
            CurrentFile->Header->SequenceNumber !=
                GetSequenceFromFileRef(CurrentFileReference))
        {
            delete CurrentFile;
            return STATUS_FILE_CORRUPT_ERROR;
        }

        NextSeparator =
            NtfsWcsChr(QueryElementPtr, L'\\');
        NextElement = NextSeparator;
        if (NextElement)
        {
            while (*NextElement == L'\\')
                NextElement++;
        }
        HasRemainingName =
            NextElement && *NextElement != L'\0';

        if (ReturnReparse &&
            CurrentFile->GetAttribute(
                TypeReparsePoint,
                NULL) &&
            (HasRemainingName ||
             !OpenFinalReparsePoint))
        {
            SIZE_T RemainingCharacters =
                NextSeparator
                ? NtfsWcsLen(NextSeparator)
                : 0;

            if (RemainingCharacters >
                MAXULONG / sizeof(WCHAR))
            {
                delete CurrentFile;
                return STATUS_NAME_TOO_LONG;
            }
            *RemainingNameLength =
                (ULONG)(RemainingCharacters *
                        sizeof(WCHAR));
            *File = CurrentFile;
            return STATUS_REPARSE;
        }

        if (NextSeparator)
        {
            /* Ignore trailing and repeated path separators. */
            if (!HasRemainingName)
                break;

            if (!(CurrentFile->Header->Flags & FR_IS_DIRECTORY))
            {
                delete CurrentFile;
                return STATUS_NOT_FOUND;
            }

            QueryElementPtr = NextElement;
        }

        else
        {
            break;
        }
    }

    // Current File should now point to the queried file.
    *File = CurrentFile;
    return STATUS_SUCCESS;
}
