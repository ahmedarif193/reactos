/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static
NTSTATUS
AddKeyToBothDirInfo(_In_     PBTreeKey Key,
                    _In_opt_ PBTreeKey ShortNameKey,
                    _In_     BOOLEAN IsLastEntry,
                    _Inout_  PFILE_BOTH_DIR_INFORMATION Buffer,
                    _Inout_  PULONG BufferLength,
                    _Out_    PULONG EntryLength = NULL)
{
    PFileNameEx FileNameData;
    ULONG EntrySize;

    // Set the file name data pointer
    FileNameData = GetFileName(Key);
    EntrySize = ALIGN_UP_BY(FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName) + GetWStrLength(FileNameData->NameLength), sizeof(ULONGLONG));

    if (*BufferLength < EntrySize)
        return STATUS_BUFFER_OVERFLOW;

    Buffer->FileIndex = 0; // Undefined for NTFS
    Buffer->CreationTime.QuadPart = FileNameData->CreationTime;
    Buffer->LastAccessTime.QuadPart = FileNameData->LastAccessTime;
    Buffer->LastWriteTime.QuadPart = FileNameData->LastWriteTime;
    Buffer->ChangeTime.QuadPart = FileNameData->ChangeTime;
    Buffer->EndOfFile.QuadPart = FileNameData->DataSize;
    Buffer->AllocationSize.QuadPart = FileNameData->AllocatedSize;
    Buffer->FileAttributes = FileNameData->Flags;
    Buffer->FileNameLength = GetWStrLength(FileNameData->NameLength);
    Buffer->EaSize = FileNameData->Extended.EAInfo.PackedEASize;
    RtlCopyMemory(Buffer->FileName,
                  FileNameData->Name,
                  GetWStrLength(FileNameData->NameLength));

    // Mark file as folder if it is a directory
    if (FileNameData->Flags & FN_DIRECTORY)
        Buffer->FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

    // Let's get the short name
    RtlZeroMemory(Buffer->ShortName, MAX_SHORTNAME_LENGTH * sizeof(WCHAR));
    Buffer->ShortNameLength = 0;

    if (ShortNameKey)
    {
        FileNameData = GetFileName(ShortNameKey);
        Buffer->ShortNameLength = GetWStrLength(FileNameData->NameLength);
        RtlCopyMemory(Buffer->ShortName,
                      FileNameData->Name,
                      GetWStrLength(FileNameData->NameLength));

    }

    /* Set the entry size.
     * Note: Entries in the buffer must be aligned to 8-byte boundaries
     * See: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/270df317-9ba5-4ccb-ba00-8d22be139bc5
     */
    *BufferLength -= EntrySize;

    // Set next entry offset
    if (IsLastEntry)
        Buffer->NextEntryOffset = 0;
    else
        Buffer->NextEntryOffset = EntrySize;

    if (EntryLength)
        *EntryLength = EntrySize;

    return STATUS_SUCCESS;
}

BOOLEAN
Directory::IsEligibleForFileDir(PBTreeKey Key,
                                PUNICODE_STRING FileNameFilter)
{
    // Is this a dummy key?
    if (IsLastEntry(Key))
        return FALSE;

    // Does this match the file name filter?
    if (FileNameFilter
        && !DoesFileNameMatch(FileNameFilter, Key))
        return FALSE;

    // Is this a super hidden metadata file?
    if (GetFRNFromFileRef(FileRef(Key)) <= NTFS_LAST_RESERVED_FILE_RECORD
        && !DiskVolume->ShowMetadataFiles)
        return FALSE;

    // Is this a duplicated short name?
    if (Key->Flags & DIR_KEY_8DOT3)
        return FALSE;

    return TRUE;
}

BOOLEAN
Directory::IsEligibleForFileDir(PIndexEntry Entry,
                                PUNICODE_STRING FileNameFilter)
{
    PFileNameEx FileNameData;

    if (Entry->Flags & INDEX_ENTRY_END)
        return FALSE;

    FileNameData = (PFileNameEx)Entry->IndexStream;
    if (FileNameFilter &&
        !DoesFileNameMatch(FileNameFilter, Entry))
    {
        return FALSE;
    }

    if (GetFRNFromFileRef(Entry->Data.Directory.IndexedFile) <=
            NTFS_LAST_RESERVED_FILE_RECORD &&
        !DiskVolume->ShowMetadataFiles)
    {
        return FALSE;
    }

    return FileNameData->NameType != NAME_TYPE_DOS;
}

NTSTATUS
Directory::LoadDirectoryForEnumeration(_In_ PFileRecord File)
{
    PIndexRootEx IndexRoot;
    ULONG IndexRecordSize;
    NTSTATUS Status;

    if (!File || !(File->Header->Flags & FR_IS_DIRECTORY))
        return STATUS_NOT_FOUND;

    EnumerationFile = File;
    EnumerationRoot = File->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(L"$I30"));
    EnumerationAllocation = File->GetAttribute(
        TypeIndexAllocation,
        const_cast<PWSTR>(L"$I30"));
    EnumerationBitmap = File->GetAttribute(
        TypeBitmap,
        const_cast<PWSTR>(L"$I30"));

    if (!EnumerationRoot ||
        EnumerationRoot->IsNonResident ||
        EnumerationRoot->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) + sizeof(IndexNodeHeader))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failed;
    }

    IndexRoot =
        (PIndexRootEx)GetResidentDataPointer(EnumerationRoot);
    IndexRecordSize = BytesPerIndexRecord(DiskVolume);
    if (IndexRecordSize == 0 ||
        IndexRoot->AttributeType != TypeFileName ||
        IndexRoot->CollationRule != ATTRDEF_COLLATION_FILENAME ||
        IndexRoot->BytesPerIndexRec != IndexRecordSize)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failed;
    }

    DirectEnumeration = TRUE;
    Status = ResetDirectEnumeration();
    if (!NT_SUCCESS(Status))
        goto Failed;
    return STATUS_SUCCESS;

Failed:
    EnumerationFile = NULL;
    EnumerationRoot = NULL;
    EnumerationAllocation = NULL;
    EnumerationBitmap = NULL;
    EnumerationDepth = 0;
    EnumerationLoadedDepth = -1;
    EnumerationLoadedVCN = ~(ULONGLONG)0;
    DirectEnumeration = FALSE;
    return Status;
}

NTSTATUS
Directory::ResetDirectEnumeration()
{
    PIndexNodeHeader Header;
    ULONG HeaderBytes;
    NTSTATUS Status;

    if (!DirectEnumeration || !EnumerationFile || !EnumerationRoot)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(EnumerationStack, sizeof(EnumerationStack));
    EnumerationStack[0].EntryOffset = MAXULONG;
    EnumerationStack[0].IsRoot = TRUE;
    EnumerationDepth = 1;
    EnumerationLoadedDepth = -1;
    EnumerationLoadedVCN = ~(ULONGLONG)0;

    Status = LoadDirectNode(0, &Header, &HeaderBytes);
    if (!NT_SUCCESS(Status))
        EnumerationDepth = 0;
    return Status;
}

NTSTATUS
Directory::LoadDirectNode(_In_ ULONG Depth,
                          _Out_ PIndexNodeHeader* Header,
                          _Out_ PULONG HeaderBytes)
{
    const ULONGLONG MaximumValue = ~(ULONGLONG)0;
    DirectEnumerationFrame* Frame;
    ULONG IndexRecordSize;

    if (!Header || !HeaderBytes || Depth >= EnumerationDepth)
        return STATUS_INVALID_PARAMETER;

    Frame = &EnumerationStack[Depth];
    if (Frame->IsRoot)
    {
        PIndexRootEx IndexRoot =
            (PIndexRootEx)GetResidentDataPointer(EnumerationRoot);

        *Header = &IndexRoot->Header;
        *HeaderBytes =
            EnumerationRoot->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header);
    }
    else
    {
        PIndexBuffer NodeBuffer;
        ULONGLONG AllocationUnit;
        ULONGLONG AllocationOffset;
        ULONGLONG IndexRecordNumber;
        ULONGLONG BitmapLength;
        ULONG BitmapBytesRemaining = sizeof(UCHAR);
        ULONG BytesRemaining;
        UCHAR BitmapMask;
        UCHAR BitmapValue;
        NTSTATUS Status;

        if (!EnumerationAllocation ||
            !EnumerationAllocation->IsNonResident ||
            !EnumerationBitmap)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        IndexRecordSize = BytesPerIndexRecord(DiskVolume);
        AllocationUnit = IndexRecordSize < BytesPerCluster(DiskVolume)
            ? DiskVolume->BytesPerSector
            : BytesPerCluster(DiskVolume);
        if (IndexRecordSize == 0 ||
            AllocationUnit == 0 ||
            Frame->VCN > MaximumValue / AllocationUnit)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        AllocationOffset = Frame->VCN * AllocationUnit;
        if (AllocationOffset % IndexRecordSize != 0)
            return STATUS_FILE_CORRUPT_ERROR;

        IndexRecordNumber = AllocationOffset / IndexRecordSize;

        if (!DiskVolume->IndexWorkBuffer ||
            DiskVolume->IndexWorkBufferSize < IndexRecordSize)
        {
            delete[] DiskVolume->IndexWorkBuffer;
            DiskVolume->IndexWorkBuffer =
                new(PagedPool, TAG_NTFS) UCHAR[IndexRecordSize];
            DiskVolume->IndexWorkBufferSize =
                DiskVolume->IndexWorkBuffer ? IndexRecordSize : 0;
            DiskVolume->IndexWorkBufferValid = FALSE;
        }
        if (!DiskVolume->IndexWorkBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        if (EnumerationLoadedDepth != (LONG)Depth ||
            EnumerationLoadedVCN != Frame->VCN)
        {
            DiskVolume->IndexWorkBufferValid = FALSE;
            BitmapLength =
                GetAttributeDataSize(EnumerationBitmap);
            if ((IndexRecordNumber >> 3) >= BitmapLength)
                return STATUS_FILE_CORRUPT_ERROR;

            BitmapMask =
                (UCHAR)(1 << (IndexRecordNumber & 7));
            Status = EnumerationFile->CopyData(
                EnumerationBitmap,
                &BitmapValue,
                &BitmapBytesRemaining,
                IndexRecordNumber >> 3);
            if (!NT_SUCCESS(Status) ||
                BitmapBytesRemaining != 0 ||
                !(BitmapValue & BitmapMask))
            {
                return NT_SUCCESS(Status)
                    ? STATUS_FILE_CORRUPT_ERROR
                    : Status;
            }

            BytesRemaining = IndexRecordSize;
            Status = EnumerationFile->CopyData(
                EnumerationAllocation,
                DiskVolume->IndexWorkBuffer,
                &BytesRemaining,
                AllocationOffset);
            if (!NT_SUCCESS(Status) || BytesRemaining != 0)
            {
                return NT_SUCCESS(Status)
                    ? STATUS_END_OF_FILE
                    : Status;
            }

            NodeBuffer =
                (PIndexBuffer)DiskVolume->IndexWorkBuffer;
            Status = NtfsApplyFixup(
                &NodeBuffer->RecordHeader,
                IndexRecordSize,
                DiskVolume->BytesPerSector);
            if (!NT_SUCCESS(Status) ||
                RtlCompareMemory(
                    NodeBuffer->RecordHeader.TypeID,
                    "INDX",
                    4) != 4 ||
                NodeBuffer->VCN != Frame->VCN)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            EnumerationLoadedDepth = (LONG)Depth;
            EnumerationLoadedVCN = Frame->VCN;
        }

        NodeBuffer = (PIndexBuffer)DiskVolume->IndexWorkBuffer;
        *Header = &NodeBuffer->IndexHeader;
        *HeaderBytes =
            IndexRecordSize -
            FIELD_OFFSET(IndexBuffer, IndexHeader);
    }

    if (*HeaderBytes < sizeof(IndexNodeHeader) ||
        (*Header)->IndexOffset < sizeof(IndexNodeHeader) ||
        (*Header)->IndexOffset > (*Header)->TotalIndexSize ||
        (*Header)->TotalIndexSize > *HeaderBytes)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Frame->EntryOffset == MAXULONG)
        Frame->EntryOffset = (*Header)->IndexOffset;
    if (Frame->EntryOffset < (*Header)->IndexOffset ||
        Frame->EntryOffset >= (*Header)->TotalIndexSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Directory::GetNextDirectEntry(_Out_ PIndexEntry* Entry)
{
    if (!Entry)
        return STATUS_INVALID_PARAMETER;
    *Entry = NULL;

    while (EnumerationDepth != 0)
    {
        DirectEnumerationFrame* Frame =
            &EnumerationStack[EnumerationDepth - 1];
        PIndexNodeHeader Header;
        PIndexEntry Current;
        ULONG HeaderBytes;
        ULONG Remaining;
        NTSTATUS Status;

        Status = LoadDirectNode(
            EnumerationDepth - 1,
            &Header,
            &HeaderBytes);
        if (!NT_SUCCESS(Status))
            return Status;

        Remaining =
            Header->TotalIndexSize - Frame->EntryOffset;
        Current = (PIndexEntry)(
            (PUCHAR)Header + Frame->EntryOffset);
        if (!NtfsIsDirectoryIndexEntryValid(
                Current,
                Remaining))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if ((Current->Flags & INDEX_ENTRY_NODE) &&
            !Frame->ChildVisited)
        {
            ULONGLONG ChildVCN = *GetSubnodeVCN(Current);

            Frame->ChildVisited = TRUE;
            if (EnumerationDepth ==
                RTL_NUMBER_OF(EnumerationStack))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            for (ULONG Index = 0;
                 Index < EnumerationDepth;
                 Index++)
            {
                if (!EnumerationStack[Index].IsRoot &&
                    EnumerationStack[Index].VCN == ChildVCN)
                {
                    return STATUS_FILE_CORRUPT_ERROR;
                }
            }

            EnumerationStack[EnumerationDepth].VCN =
                ChildVCN;
            EnumerationStack[EnumerationDepth].EntryOffset =
                MAXULONG;
            EnumerationStack[EnumerationDepth].IsRoot =
                FALSE;
            EnumerationStack[EnumerationDepth].ChildVisited =
                FALSE;
            EnumerationDepth++;
            continue;
        }

        if (Current->Flags & INDEX_ENTRY_END)
        {
            EnumerationDepth--;
            EnumerationLoadedDepth = -1;
            EnumerationLoadedVCN = ~(ULONGLONG)0;
            continue;
        }

        if (Current->EntryLength >
            Header->TotalIndexSize - Frame->EntryOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        Frame->EntryOffset += Current->EntryLength;
        Frame->ChildVisited = FALSE;
        *Entry = Current;
        return STATUS_SUCCESS;
    }

    return STATUS_NO_MORE_FILES;
}

NTSTATUS
Directory::FindDirectShortName(
    _In_ ULONGLONG FileReference,
    _Out_ PWCHAR ShortName,
    _Out_ PUCHAR ShortNameLength)
{
    DirectEnumerationFrame SavedStack[
        RTL_NUMBER_OF(EnumerationStack)];
    ULONG SavedDepth = EnumerationDepth;
    NTSTATUS Status;

    if (!ShortName || !ShortNameLength)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(
        SavedStack,
        EnumerationStack,
        sizeof(SavedStack));
    *ShortNameLength = 0;
    RtlZeroMemory(
        ShortName,
        (MAX_SHORTNAME_LENGTH + 1) * sizeof(WCHAR));

    Status = ResetDirectEnumeration();
    if (NT_SUCCESS(Status))
    {
        PIndexEntry Entry;

        while (NT_SUCCESS(
            Status = GetNextDirectEntry(&Entry)))
        {
            PFileNameEx FileNameData =
                (PFileNameEx)Entry->IndexStream;

            if (Entry->Data.Directory.IndexedFile ==
                    FileReference &&
                FileNameData->NameType == NAME_TYPE_DOS)
            {
                if (FileNameData->NameLength >
                    MAX_SHORTNAME_LENGTH)
                {
                    Status =
                        STATUS_FILE_CORRUPT_ERROR;
                    break;
                }

                *ShortNameLength = (UCHAR)
                    GetWStrLength(
                        FileNameData->NameLength);
                RtlCopyMemory(
                    ShortName,
                    FileNameData->Name,
                    *ShortNameLength);
                Status = STATUS_SUCCESS;
                break;
            }
        }
        if (Status == STATUS_NO_MORE_FILES)
            Status = STATUS_SUCCESS;
    }

    RtlCopyMemory(
        EnumerationStack,
        SavedStack,
        sizeof(SavedStack));
    EnumerationDepth = SavedDepth;
    EnumerationLoadedDepth = -1;
    EnumerationLoadedVCN = ~(ULONGLONG)0;
    return Status;
}

NTSTATUS
Directory::GetFileBothDirInfoDirect(
    _In_ BOOLEAN ReturnSingleEntry,
    _In_ BOOLEAN RestartScan,
    _In_ PUNICODE_STRING FileNameFilter,
    _Inout_ PFILE_BOTH_DIR_INFORMATION Buffer,
    _Inout_ PULONG BufferLength)
{
    PFILE_BOTH_DIR_INFORMATION PreviousBuffer = NULL;
    ULONG EntrySize = 0;
    ULONG TotalBufferLength = *BufferLength;
    BOOLEAN SkipToResume = FALSE;
    NTSTATUS Status;

    if (RestartScan)
    {
        Status = ResetDirectEnumeration();
        if (!NT_SUCCESS(Status))
            return Status;
        HasResumeName = FALSE;
    }
    else if (HasResumeName)
    {
        /*
         * Where the previous query stopped is recorded as a node and an offset
         * inside it, and both stop describing that entry as soon as the
         * directory is edited. Deleting what was just enumerated is ordinary
         * ("del *.txt"), and it shifts every following entry, so continue from
         * the last name handed out and let the index's own ordering say what
         * comes next.
         */
        Status = ResetDirectEnumeration();
        if (!NT_SUCCESS(Status))
            return Status;
        SkipToResume = TRUE;
    }
    else if (EnumerationDepth == 0)
    {
        return STATUS_NO_MORE_FILES;
    }

    if (FileNameFilter)
    {
        Status = DiskVolume->UpcaseWideString(
            FileNameFilter->Buffer,
            FileNameFilter->Length / sizeof(WCHAR));
        if (!NT_SUCCESS(Status))
            return Status;
    }

    /*
     * The volume scratch may have served another lookup since the previous
     * query IRP. Force the first child node to be reloaded, then retain it
     * while this call walks adjacent entries.
     */
    EnumerationLoadedDepth = -1;
    EnumerationLoadedVCN = ~(ULONGLONG)0;

    for (;;)
    {
        PIndexEntry IndexEntry;

        Status = GetNextDirectEntry(&IndexEntry);
        if (Status == STATUS_NO_MORE_FILES)
        {
            if (TotalBufferLength == *BufferLength)
                return STATUS_NO_MORE_FILES;
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
            break;

        if (SkipToResume)
        {
            PFileNameEx EntryName = (PFileNameEx)IndexEntry->IndexStream;
            UNICODE_STRING EntryString;
            UNICODE_STRING ResumeString;
            LONG CompareResult;

            EntryString.Buffer = EntryName->Name;
            EntryString.Length = (USHORT)(EntryName->NameLength * sizeof(WCHAR));
            EntryString.MaximumLength = EntryString.Length;
            ResumeString.Buffer = ResumeName;
            ResumeString.Length = (USHORT)(ResumeNameLength * sizeof(WCHAR));
            ResumeString.MaximumLength = ResumeString.Length;

            Status = DiskVolume->CompareFileNames(&EntryString, &ResumeString, &CompareResult);
            if (!NT_SUCCESS(Status))
                break;
            if (CompareResult <= 0)
                continue;
            SkipToResume = FALSE;
        }

        if (IsEligibleForFileDir(
                IndexEntry,
                FileNameFilter))
        {
            BTreeKey Key = {};
            PFileNameEx EmittedName = (PFileNameEx)IndexEntry->IndexStream;
            BOOLEAN FindShortName =
                EmittedName->NameType == NAME_TYPE_WIN32;

            Key.Entry = IndexEntry;
            Status = AddKeyToBothDirInfo(
                &Key,
                NULL,
                FALSE,
                Buffer,
                BufferLength,
                &EntrySize);
            if (Status == STATUS_BUFFER_OVERFLOW)
            {
                DirectEnumerationFrame* Frame;

                if (EnumerationDepth == 0)
                    return STATUS_FILE_CORRUPT_ERROR;
                Frame =
                    &EnumerationStack[EnumerationDepth - 1];
                if (Frame->EntryOffset <
                    IndexEntry->EntryLength)
                {
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                Frame->EntryOffset -=
                    IndexEntry->EntryLength;
                Frame->ChildVisited =
                    !!(IndexEntry->Flags & INDEX_ENTRY_NODE);
                Status = STATUS_SUCCESS;
                break;
            }
            if (!NT_SUCCESS(Status))
                break;

            if (FindShortName)
            {
                UCHAR ShortNameLength;

                Status = FindDirectShortName(
                    IndexEntry->Data.Directory.IndexedFile,
                    Buffer->ShortName,
                    &ShortNameLength);
                if (!NT_SUCCESS(Status))
                    break;
                Buffer->ShortNameLength =
                    ShortNameLength;
            }

            ResumeNameLength = (UCHAR)min(EmittedName->NameLength, NTFS_MAX_FILE_NAME_LENGTH);
            RtlCopyMemory(ResumeName, EmittedName->Name, ResumeNameLength * sizeof(WCHAR));
            HasResumeName = TRUE;

            if (ReturnSingleEntry)
            {
                Buffer->NextEntryOffset = 0;
                break;
            }

            PreviousBuffer = Buffer;
            Buffer = (PFILE_BOTH_DIR_INFORMATION)(
                (ULONG_PTR)Buffer + EntrySize);
        }
    }

    if (PreviousBuffer)
        PreviousBuffer->NextEntryOffset = 0;
    return Status;
}

NTSTATUS
Directory::GetNextEntry(_In_ BOOLEAN RestartScan,
                        _Out_ PNtfsDirectoryEntry Entry)
{
    PBTreeKey Key;
    PBTreeKey ShortNameKey;
    PFileNameEx FileNameData;

    if (!Entry)
        return STATUS_INVALID_PARAMETER;

    if (RestartScan)
        ResetCurrentKey();

    while (CurrentKey && !IsEligibleForFileDir(CurrentKey, NULL))
        CurrentKey = GetNextKey(CurrentKey);

    if (!CurrentKey || IsEndOfNode(CurrentKey))
        return STATUS_NO_MORE_FILES;

    Key = CurrentKey;
    CurrentKey = GetNextKey(CurrentKey);
    FileNameData = GetFileName(Key);
    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->FileReference = FileRef(Key);
    Entry->CreationTime = FileNameData->CreationTime;
    Entry->LastAccessTime = FileNameData->LastAccessTime;
    Entry->LastWriteTime = FileNameData->LastWriteTime;
    Entry->ChangeTime = FileNameData->ChangeTime;
    Entry->EndOfFile = FileNameData->DataSize;
    Entry->AllocationSize = FileNameData->AllocatedSize;
    Entry->FileAttributes = FileNameData->Flags;
    if (FileNameData->Flags & FILE_PERM_REPARSE_PT)
    {
        Entry->EaSize = 0;
        Entry->ReparseTag =
            FileNameData->Extended.ReparseTag;
    }
    else
    {
        Entry->EaSize =
            FileNameData->Extended.EAInfo.PackedEASize;
        Entry->ReparseTag = 0;
    }
    Entry->NameLength = FileNameData->NameLength;
    RtlCopyMemory(Entry->Name,
                  FileNameData->Name,
                  FileNameData->NameLength * sizeof(WCHAR));
    Entry->Name[Entry->NameLength] = L'\0';

    ShortNameKey = GetShortNameKey(Key);
    if (ShortNameKey)
    {
        FileNameData = GetFileName(ShortNameKey);
        if (FileNameData->NameLength > MAX_SHORTNAME_LENGTH)
            return STATUS_FILE_CORRUPT_ERROR;

        Entry->ShortNameLength = FileNameData->NameLength;
        RtlCopyMemory(Entry->ShortName,
                      FileNameData->Name,
                      FileNameData->NameLength * sizeof(WCHAR));
        Entry->ShortName[Entry->ShortNameLength] = L'\0';
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Directory::GetFileBothDirInfo(_In_    BOOLEAN ReturnSingleEntry,
                              _In_    BOOLEAN RestartScan,
                              _In_    PUNICODE_STRING FileNameFilter,
                              _Inout_ PFILE_BOTH_DIR_INFORMATION Buffer,
                              _Inout_ PULONG BufferLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG EntrySize, TotalBufferLength;
    PFILE_BOTH_DIR_INFORMATION PreviousBuffer;

    if (DirectEnumeration)
    {
        return GetFileBothDirInfoDirect(
            ReturnSingleEntry,
            RestartScan,
            FileNameFilter,
            Buffer,
            BufferLength);
    }

    EntrySize = 0;
    PreviousBuffer = NULL;

    if (!CurrentKey ||
        IsEndOfNode(CurrentKey))
    {
        // We reached the end of the directory listing.
        return STATUS_NO_MORE_FILES;
    }

    // Restart scan if requested.
    if (RestartScan)
        ResetCurrentKey();

    if (FileNameFilter)
    {
        Status = DiskVolume->UpcaseWideString(
            FileNameFilter->Buffer,
            FileNameFilter->Length / sizeof(WCHAR));
        if (!NT_SUCCESS(Status))
            return Status;
    }

    TotalBufferLength = *BufferLength;

    while (CurrentKey)
    {
        if (IsEligibleForFileDir(CurrentKey,
                                 FileNameFilter))
        {
            // Add key to buffer
            Status = AddKeyToBothDirInfo(CurrentKey,
                                         GetShortNameKey(CurrentKey),
                                         FALSE,
                                         Buffer,
                                         BufferLength,
                                         &EntrySize);

            if (Status == STATUS_BUFFER_OVERFLOW)
            {
                /* Writing this key will lead to a buffer overflow.
                 * Terminate the last entry and return STATUS_SUCCESS.
                 */
                Status = STATUS_SUCCESS;
                goto done;
            }

            if (!NT_SUCCESS(Status))
            {
                // Some other error.
                DPRINT1("Failed to add key to buffer!\n");
                goto done;
            }

            if (ReturnSingleEntry)
            {
                Buffer->NextEntryOffset = 0;
                CurrentKey = GetNextKey(CurrentKey);
                break;
            }

            // Adjust buffer
            PreviousBuffer = Buffer;
            Buffer = (PFILE_BOTH_DIR_INFORMATION)((ULONG_PTR)Buffer + EntrySize);
        }

        CurrentKey = GetNextKey(CurrentKey);

        if (!CurrentKey)
        {
            if (TotalBufferLength == *BufferLength)
            {
                /* Traversal reached the end without emitting an eligible
                 * entry. This is the definitive empty-result test because
                 * filtered and metadata entries may all have been skipped.
                 */
                return STATUS_NO_MORE_FILES;
            }

            goto done;
        }
    }

done:
    // Go back to previous entry and terminate it.
    if (PreviousBuffer)
        PreviousBuffer->NextEntryOffset = 0;

    return Status;
}
