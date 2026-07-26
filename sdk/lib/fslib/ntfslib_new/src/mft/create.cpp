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

extern "C" {
extern long NtfsIoReadCount;
extern long NtfsIoWriteCount;
extern long long NtfsIoReadTicks;
extern long long NtfsIoWriteTicks;
}

static long NtfsLibCreates = 0;
static unsigned long long NtfsLibResolve = 0;
static unsigned long long NtfsLibCollide = 0;
static unsigned long long NtfsLibAlloc = 0;
static unsigned long long NtfsLibInit = 0;
static unsigned long long NtfsLibWrite = 0;
static unsigned long long NtfsLibIndex = 0;

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
    unsigned long long LT0 = 0, LT1 = 0, LT2 = 0, LT3 = 0, LT4 = 0, LT5 = 0, LT6 = 0;
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

    LT0 = NtfsQueryTicks();
    Status = SplitAndResolveParent(
        Query,
        TRUE,
        &Parent,
        &Name,
        &NameLength);
    LT1 = NtfsQueryTicks();
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
    LT2 = NtfsQueryTicks();
    if (Status != STATUS_NOT_FOUND)
        goto Done;

    /* Windows marks only new ordinary files as unarchived content. */
    if (FileAttributes == 0 && !IsDirectory)
        FileAttributes = FILE_PERM_ARCHIVE;
    Status = AllocateBaseFileRecord(
        IsDirectory,
        &NewFile);
    LT3 = NtfsQueryTicks();
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = NewFile->InitializeNewFileRecord(
        Parent,
        Name,
        NameLength,
        IsDirectory,
        FileAttributes,
        &FileName);
    LT4 = NtfsQueryTicks();
    if (!NT_SUCCESS(Status))
        goto Rollback;

    Status = WriteFileRecordToMFT(NewFile);
    LT5 = NtfsQueryTicks();
    if (!NT_SUCCESS(Status))
        goto Rollback;

    FileReference =
        MakeFileReference(NewFile->Header);
    Status = ParentIndex.AddFileToDirectory(
        Parent,
        FileReference,
        FileName);
    LT6 = NtfsQueryTicks();
    if (!NT_SUCCESS(Status))
        goto Rollback;
    RecordPublished = TRUE;

    NtfsLibResolve += LT1 - LT0;
    NtfsLibCollide += LT2 - LT1;
    NtfsLibAlloc += LT3 - LT2;
    NtfsLibInit += LT4 - LT3;
    NtfsLibWrite += LT5 - LT4;
    NtfsLibIndex += LT6 - LT5;
    if ((++NtfsLibCreates & 0x1F) == 0)
    {
        DPRINT1("LIBACCT n=%ld res=%I64u col=%I64u alloc=%I64u init=%I64u wr=%I64u idx=%I64u rd(%ld)=%I64d wrio(%ld)=%I64d\n",
                NtfsLibCreates, NtfsLibResolve, NtfsLibCollide,
                NtfsLibAlloc, NtfsLibInit, NtfsLibWrite, NtfsLibIndex,
                NtfsIoReadCount, NtfsIoReadTicks,
                NtfsIoWriteCount, NtfsIoWriteTicks);
    }


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
