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
    EntrySize = ALIGN_UP_BY(sizeof(FILE_BOTH_DIR_INFORMATION) + GetWStrLength(FileNameData->NameLength),
                            sizeof(ULONG));

    if (*BufferLength < EntrySize)
    {
        // We will overrun the buffer if we continue
        DPRINT1("Unable to add key to buffer: too small!\n");
        return STATUS_BUFFER_OVERFLOW;
    }

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
