/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Synchronize duplicated $FILE_NAME stream sizes
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_INDEX_HEADER_LARGE 1

extern const WCHAR NtfsI30Name[] = L"$I30";

static BOOLEAN
FileNameKeysMatch(_In_ PIndexEntry Entry,
                  _In_ PFileNameEx TargetName,
                  _In_ ULONGLONG FileReference)
{
    PFileNameEx IndexedName;
    ULONG NameBytes;

    if ((Entry->Flags & INDEX_ENTRY_END) ||
        Entry->Data.Directory.IndexedFile != FileReference)
    {
        return FALSE;
    }

    IndexedName =
        reinterpret_cast<PFileNameEx>(Entry->IndexStream);
    if (IndexedName->ParentFileReference !=
            TargetName->ParentFileReference ||
        IndexedName->NameLength != TargetName->NameLength ||
        IndexedName->NameType != TargetName->NameType)
    {
        return FALSE;
    }

    NameBytes =
        TargetName->NameLength * sizeof(WCHAR);
    return NameBytes == 0 ||
           RtlCompareMemory(IndexedName->Name,
                            TargetName->Name,
                            NameBytes) == NameBytes;
}

static NTSTATUS
UpdateIndexNodeInformation(
    _Inout_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _In_ PFileNameEx TargetName,
    _In_ ULONGLONG FileReference,
    _In_ UINT32 Fields,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ USHORT PackedEaSize,
    _In_ ULONG ReparseTag,
    _In_ ULONG StorageFlags)
{
    PIndexEntry Entry;
    PFileNameEx MatchedName = NULL;
    ULONG_PTR End;
    BOOLEAN FoundEnd = FALSE;

    if (Fields == 0 ||
        (Fields & ~NTFS_FILE_NAME_UPDATE_ALL) != 0 ||
        (StorageFlags &
         ~(FILE_PERM_SPARSE |
           FILE_PERM_COMPRESSED)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Header ||
        HeaderBytes < sizeof(*Header) ||
        Header->IndexOffset < sizeof(*Header) ||
        Header->IndexOffset > Header->TotalIndexSize ||
        Header->TotalIndexSize > Header->AllocatedSize ||
        Header->AllocatedSize > HeaderBytes ||
        (Header->Flags & ~NTFS_INDEX_HEADER_LARGE) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Entry = reinterpret_cast<PIndexEntry>(
        reinterpret_cast<PUCHAR>(Header) +
            Header->IndexOffset);
    End = reinterpret_cast<ULONG_PTR>(Header) +
          Header->TotalIndexSize;

    while (reinterpret_cast<ULONG_PTR>(Entry) < End)
    {
        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                (ULONG)(End -
                    reinterpret_cast<ULONG_PTR>(Entry))))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->Flags & INDEX_ENTRY_END)
        {
            if (reinterpret_cast<ULONG_PTR>(Entry) +
                    Entry->EntryLength != End)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            FoundEnd = TRUE;
            break;
        }

        if (FileNameKeysMatch(Entry,
                              TargetName,
                              FileReference))
        {
            PFileNameEx IndexedName =
                reinterpret_cast<PFileNameEx>(
                    Entry->IndexStream);

            if (MatchedName)
                return STATUS_FILE_CORRUPT_ERROR;
            MatchedName = IndexedName;
        }

        Entry = reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Entry) +
                Entry->EntryLength);
    }

    if (!FoundEnd)
        return STATUS_FILE_CORRUPT_ERROR;
    if (!MatchedName)
        return STATUS_NOT_FOUND;

    if (Fields & NTFS_FILE_NAME_UPDATE_SIZES)
    {
        MatchedName->AllocatedSize = AllocatedSize;
        MatchedName->DataSize = DataSize;
    }
    if (Fields & NTFS_FILE_NAME_UPDATE_REPARSE_TAG)
    {
        if (ReparseTag != 0)
        {
            MatchedName->Flags |=
                FILE_PERM_REPARSE_PT;
            MatchedName->Extended.ReparseTag =
                ReparseTag;
        }
        else
        {
            MatchedName->Flags &=
                ~FILE_PERM_REPARSE_PT;
            MatchedName->Extended.EAInfo.PackedEASize =
                PackedEaSize;
            MatchedName->Extended.EAInfo.Padding = 0;
        }
    }
    if ((Fields & NTFS_FILE_NAME_UPDATE_EA_SIZE) &&
        !(MatchedName->Flags & FILE_PERM_REPARSE_PT))
    {
        MatchedName->Extended.EAInfo.PackedEASize =
            PackedEaSize;
        MatchedName->Extended.EAInfo.Padding = 0;
    }
    if (Fields & NTFS_FILE_NAME_UPDATE_ARCHIVE)
        MatchedName->Flags |= FILE_PERM_ARCHIVE;
    if (Fields & NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS)
    {
        MatchedName->Flags =
            (MatchedName->Flags &
             ~(FILE_PERM_SPARSE |
               FILE_PERM_COMPRESSED)) |
            StorageFlags;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::UpdateParentIndexEntry(
    _In_ PFileNameEx TargetName,
    _In_ ULONGLONG FileReference,
    _In_ UINT32 Fields,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ USHORT PackedEaSize,
    _In_ ULONG ReparseTag,
    _In_ ULONG StorageFlags)
{
    PFileRecord Parent = NULL;
    PFileRecord IndexRootOwner;
    PAttribute IndexRootAttribute;
    PAttribute IndexAllocationAttribute;
    PAttribute BitmapAttribute;
    PIndexRootEx IndexRoot;
    PUCHAR Bitmap = NULL;
    PUCHAR IndexBufferData = NULL;
    ULONGLONG ParentRecordNumber;
    ULONGLONG AllocationLength;
    ULONGLONG AllocationUnit;
    ULONG BitmapLength = 0;
    ULONG IndexRecordSize;
    USHORT ParentSequence;
    NTSTATUS Status;

    ParentRecordNumber =
        GetFRNFromFileRef(
            TargetName->ParentFileReference);
    ParentSequence = (USHORT)(
        TargetName->ParentFileReference >> 48);
    if (ParentRecordNumber > MAXULONG)
        return STATUS_FILE_CORRUPT_ERROR;

    Status = DiskVolume->MFT->GetFileRecord(
        (ULONG)ParentRecordNumber,
        &Parent);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Parent ||
        !(Parent->Header->Flags & FR_IS_DIRECTORY) ||
        (ParentSequence != 0 &&
         Parent->Header->SequenceNumber != ParentSequence))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexRootAttribute = Parent->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(NtfsI30Name));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->Resident.DataOffset < 0x18 ||
        IndexRootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) +
                sizeof(IndexNodeHeader) ||
        IndexRootAttribute->Resident.DataOffset >
            IndexRootAttribute->Length ||
        IndexRootAttribute->Resident.DataLength >
            IndexRootAttribute->Length -
                IndexRootAttribute->Resident.DataOffset)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexRoot = reinterpret_cast<PIndexRootEx>(
        GetResidentDataPointer(IndexRootAttribute));
    IndexRecordSize = IndexRoot->BytesPerIndexRec;
    if (IndexRoot->AttributeType != TypeFileName ||
        IndexRoot->CollationRule !=
            ATTRDEF_COLLATION_FILENAME ||
        IndexRecordSize !=
            BytesPerIndexRecord(DiskVolume) ||
        IndexRecordSize < sizeof(IndexBuffer) ||
        IndexRecordSize %
            DiskVolume->BytesPerSector != 0 ||
        IndexRoot->ClusPerIndexRec !=
            (UCHAR)DiskVolume->ClustersPerIndexRecord)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Status = UpdateIndexNodeInformation(
        &IndexRoot->Header,
        IndexRootAttribute->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        TargetName,
        FileReference,
        Fields,
        AllocatedSize,
        DataSize,
        PackedEaSize,
        ReparseTag,
        StorageFlags);
    if (NT_SUCCESS(Status))
    {
        IndexRootOwner =
            Parent->GetAttributeOwner(IndexRootAttribute);
        if (!IndexRootOwner)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = DiskVolume->MFT->WriteFileRecordToMFT(
            IndexRootOwner);
        goto Done;
    }
    if (Status != STATUS_NOT_FOUND ||
        !(IndexRoot->Header.Flags &
          NTFS_INDEX_HEADER_LARGE))
    {
        if (Status == STATUS_NOT_FOUND)
            Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexAllocationAttribute = Parent->GetAttribute(
        TypeIndexAllocation,
        const_cast<PWSTR>(NtfsI30Name));
    BitmapAttribute = Parent->GetAttribute(
        TypeBitmap,
        const_cast<PWSTR>(NtfsI30Name));
    if (!IndexAllocationAttribute ||
        !IndexAllocationAttribute->IsNonResident ||
        !BitmapAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Status = Parent->ReadAttributeAlloc(BitmapAttribute,
                                        &Bitmap,
                                        &BitmapLength);
    if (!NT_SUCCESS(Status) ||
        !Bitmap ||
        BitmapLength == 0)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    AllocationLength =
        GetAttributeDataSize(IndexAllocationAttribute);
    AllocationUnit =
        IndexRecordSize < BytesPerCluster(DiskVolume)
            ? DiskVolume->BytesPerSector
            : BytesPerCluster(DiskVolume);
    if (AllocationUnit == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexBufferData =
        new(PagedPool, TAG_NTFS) UCHAR[IndexRecordSize];
    if (!IndexBufferData)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    for (ULONGLONG RecordNumber = 0;
         RecordNumber < (ULONGLONG)BitmapLength * 8;
         RecordNumber++)
    {
        PIndexBuffer NodeBuffer;
        LARGE_INTEGER WriteOffset;
        ULONGLONG AllocationOffset;
        ULONGLONG ExpectedVCN;
        ULONG BytesRemaining;
        ULONG WriteLength;
        UCHAR Mask =
            (UCHAR)(1u << (RecordNumber & 7));

        if (!(Bitmap[RecordNumber >> 3] & Mask))
            continue;
        if (RecordNumber >
            ~(ULONGLONG)0 / IndexRecordSize)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        AllocationOffset =
            RecordNumber * IndexRecordSize;
        if (AllocationOffset > AllocationLength ||
            IndexRecordSize >
                AllocationLength - AllocationOffset ||
            AllocationOffset % AllocationUnit != 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        ExpectedVCN =
            AllocationOffset / AllocationUnit;

        BytesRemaining = IndexRecordSize;
        Status = Parent->CopyData(
            IndexAllocationAttribute,
            IndexBufferData,
            &BytesRemaining,
            AllocationOffset);
        if (!NT_SUCCESS(Status) ||
            BytesRemaining != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }

        NodeBuffer =
            reinterpret_cast<PIndexBuffer>(
                IndexBufferData);
        Status = NtfsApplyFixup(
            &NodeBuffer->RecordHeader,
            IndexRecordSize,
            DiskVolume->BytesPerSector);
        if (!NT_SUCCESS(Status) ||
            RtlCompareMemory(
                NodeBuffer->RecordHeader.TypeID,
                "INDX",
                4) != 4 ||
            NodeBuffer->VCN != ExpectedVCN)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        Status = UpdateIndexNodeInformation(
            &NodeBuffer->IndexHeader,
            IndexRecordSize -
                FIELD_OFFSET(IndexBuffer, IndexHeader),
            TargetName,
            FileReference,
            Fields,
            AllocatedSize,
            DataSize,
            PackedEaSize,
            ReparseTag,
            StorageFlags);
        if (Status == STATUS_NOT_FOUND)
            continue;
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = NtfsCommitFixup(
            &NodeBuffer->RecordHeader,
            IndexRecordSize,
            DiskVolume->BytesPerSector);
        if (!NT_SUCCESS(Status))
            goto Done;

        WriteOffset.QuadPart =
            (LONGLONG)AllocationOffset;
        WriteLength = IndexRecordSize;
        Status = Parent->WriteFileData(
            TypeIndexAllocation,
            const_cast<PWSTR>(NtfsI30Name),
            IndexBufferData,
            &WriteLength,
            &WriteOffset);
        if (NT_SUCCESS(Status) &&
            WriteLength != IndexRecordSize)
        {
            Status = STATUS_END_OF_FILE;
        }
        goto Done;
    }

    Status = STATUS_FILE_CORRUPT_ERROR;

Done:
    delete[] IndexBufferData;
    delete[] Bitmap;
    delete Parent;
    return Status;
}

NTSTATUS
FileRecord::SynchronizeFileNameSizes(
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize)
{
    return SynchronizeFileNameInformation(
        NTFS_FILE_NAME_UPDATE_SIZES,
        AllocatedSize,
        DataSize,
        0,
        0,
        0);
}

NTSTATUS
FileRecord::SynchronizeFileNameEaSize(
    _In_ USHORT PackedEaSize)
{
    return SynchronizeFileNameInformation(
        NTFS_FILE_NAME_UPDATE_EA_SIZE,
        0,
        0,
        PackedEaSize,
        0,
        0);
}

NTSTATUS
FileRecord::SynchronizeFileNameInformation(
    _In_ UINT32 Fields,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ USHORT PackedEaSize,
    _In_ ULONG ReparseTag,
    _In_ ULONG StorageFlags)
{
    ULONGLONG FileReference;
    ULONG Offset;
    NTSTATUS Status;

    if (Fields == 0 ||
        (Fields & ~NTFS_FILE_NAME_UPDATE_ALL) != 0 ||
        (StorageFlags &
         ~(FILE_PERM_SPARSE |
           FILE_PERM_COMPRESSED)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Data || !Header)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    FileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        Header->MFTRecordNumber;
    Offset = Header->AttributeOffset;

    while (Offset < Header->ActualSize)
    {
        PAttribute Attribute;
        ULONG Remaining = Header->ActualSize - Offset;

        if (Remaining < sizeof(ULONG))
            return STATUS_FILE_CORRUPT_ERROR;
        Attribute =
            reinterpret_cast<PAttribute>(Data + Offset);
        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            break;
        }
        if (Attribute->Length < 0x18 ||
            (Attribute->Length &
             (sizeof(ULONGLONG) - 1)) != 0 ||
            Attribute->Length > Remaining)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->AttributeType == TypeFileName)
        {
            PFileNameEx FileName;
            ULONG NameBytes;

            if (Attribute->IsNonResident ||
                Attribute->Resident.DataOffset < 0x18 ||
                Attribute->Resident.DataOffset >
                    Attribute->Length ||
                Attribute->Resident.DataLength <
                    FIELD_OFFSET(FileNameEx, Name) ||
                Attribute->Resident.DataLength >
                    Attribute->Length -
                        Attribute->Resident.DataOffset)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            FileName = reinterpret_cast<PFileNameEx>(
                GetResidentDataPointer(Attribute));
            NameBytes =
                FileName->NameLength * sizeof(WCHAR);
            if (NameBytes >
                Attribute->Resident.DataLength -
                    FIELD_OFFSET(FileNameEx, Name))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            Status = UpdateParentIndexEntry(
                FileName,
                FileReference,
                Fields,
                AllocatedSize,
                DataSize,
                PackedEaSize,
                ReparseTag,
                StorageFlags);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        Offset += Attribute->Length;
    }

    return UpdateFileNameInformation(
        Fields,
        AllocatedSize,
        DataSize,
        PackedEaSize,
        ReparseTag,
        StorageFlags);
}
