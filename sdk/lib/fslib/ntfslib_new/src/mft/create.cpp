/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Create files and directories through the shared NTFS core
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static BOOLEAN
IsFileNameCharacterValid(_In_ WCHAR Character)
{
    if (Character < 0x20)
        return FALSE;

    switch (Character)
    {
        case L'"':
        case L'*':
        case L'/':
        case L':':
        case L'<':
        case L'>':
        case L'?':
        case L'\\':
        case L'|':
            return FALSE;
        default:
            return TRUE;
    }
}

NTSTATUS
NtfsValidateComponentName(
    _In_reads_(NameLength) PWCHAR Name,
    _In_ ULONG NameLength)
{
    if (!Name || NameLength == 0)
        return STATUS_OBJECT_NAME_INVALID;
    if (NameLength > NTFS_MAX_FILE_NAME_LENGTH)
        return STATUS_NAME_TOO_LONG;
    if ((NameLength == 1 && Name[0] == L'.') ||
        (NameLength == 2 &&
         Name[0] == L'.' &&
         Name[1] == L'.'))
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    for (ULONG Index = 0;
         Index < NameLength;
         Index++)
    {
        if (!IsFileNameCharacterValid(Name[Index]))
            return STATUS_OBJECT_NAME_INVALID;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MasterFileTable::CreateFile(
    _Inout_ PWCHAR Query,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _Out_ PFileRecord* File)
{
    Directory ParentIndex(DiskVolume);
    PFileRecord Parent = NULL;
    PFileRecord NewFile = NULL;
    PFileNameEx FileName = NULL;
    PWCHAR Name;
    ULONG NameLength;
    ULONGLONG ExistingReference;
    ULONGLONG FileReference;
    BOOLEAN RecordPublished = FALSE;
    NTSTATUS RollbackStatus;
    NTSTATUS Status;

    if (!File)
        return STATUS_INVALID_PARAMETER;
    *File = NULL;
    if (!Query || !DiskVolume)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if ((FileAttributes &
         ~NTFS_CREATE_MUTABLE_ATTRIBUTES) != 0 ||
        ((FileAttributes & FILE_PERM_NORMAL) &&
         FileAttributes != FILE_PERM_NORMAL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = SplitAndResolveParent(
        Query,
        TRUE,
        &Parent,
        &Name,
        &NameLength);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = ParentIndex.FindNextFile(
        Parent,
        Name,
        &ExistingReference);
    if (NT_SUCCESS(Status))
    {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Done;
    }
    if (Status != STATUS_NOT_FOUND)
        goto Done;

    /* Windows marks only new ordinary files as unarchived content. */
    if (FileAttributes == 0 && !IsDirectory)
        FileAttributes = FILE_PERM_ARCHIVE;
    Status = AllocateBaseFileRecord(
        IsDirectory,
        &NewFile);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = NewFile->InitializeNewFileRecord(
        Parent,
        Name,
        NameLength,
        IsDirectory,
        FileAttributes,
        &FileName);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    Status = WriteFileRecordToMFT(NewFile);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    FileReference =
        MakeFileReference(NewFile->Header);
    Status = ParentIndex.AddFileToDirectory(
        Parent,
        FileReference,
        FileName);
    if (!NT_SUCCESS(Status))
        goto Rollback;
    RecordPublished = TRUE;

    *File = NewFile;
    NewFile = NULL;
    Status = STATUS_SUCCESS;
    goto Done;

Rollback:
    if (!RecordPublished && NewFile)
    {
        RollbackStatus =
            DeallocateBaseFileRecord(NewFile);
        if (!NT_SUCCESS(RollbackStatus))
        {
            DPRINT1(
                "Unable to roll back MFT record %lu "
                "after create failure 0x%lx "
                "(rollback 0x%lx).\n",
                NewFile->Header->MFTRecordNumber,
                Status,
                RollbackStatus);
        }
    }

Done:
    delete NewFile;
    delete Parent;
    return Status;
}
