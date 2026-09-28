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

static BOOLEAN
IsShortNameCharacter(_In_ WCHAR Character)
{
    if ((Character >= L'A' && Character <= L'Z') ||
        (Character >= L'a' && Character <= L'z') ||
        (Character >= L'0' && Character <= L'9'))
    {
        return TRUE;
    }

    switch (Character)
    {
        case L'!':
        case L'#':
        case L'$':
        case L'%':
        case L'&':
        case L'\'':
        case L'(':
        case L')':
        case L'-':
        case L'@':
        case L'^':
        case L'_':
        case L'`':
        case L'{':
        case L'}':
        case L'~':
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOLEAN
IsLegalShortName(
    _In_reads_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength)
{
    ULONG Dot = MAXULONG;

    if (NameLength == 0 || NameLength > 12)
        return FALSE;

    for (ULONG Index = 0; Index < NameLength; Index++)
    {
        if (Name[Index] == L'.')
        {
            if (Dot != MAXULONG)
                return FALSE;
            Dot = Index;
        }
        else if (!IsShortNameCharacter(Name[Index]))
        {
            return FALSE;
        }
    }

    if (Dot == MAXULONG)
        return NameLength <= 8;
    return Dot >= 1 && Dot <= 8 &&
           NameLength - Dot - 1 >= 1 &&
           NameLength - Dot - 1 <= 3;
}

static ULONG
CopyShortNameCharacters(
    _In_reads_(Length) PCWSTR Source,
    _In_ ULONG Length,
    _Out_ PWCHAR Target,
    _In_ ULONG Capacity)
{
    ULONG Count = 0;

    for (ULONG Index = 0; Index < Length && Count < Capacity; Index++)
    {
        WCHAR Character = Source[Index];

        if (Character == L' ' || Character == L'.')
            continue;
        if (Character >= L'a' && Character <= L'z')
            Character = (WCHAR)(Character - (L'a' - L'A'));
        else if (!IsShortNameCharacter(Character))
            Character = L'_';
        Target[Count++] = Character;
    }
    return Count;
}

static NTSTATUS
GenerateShortName(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord Parent,
    _In_reads_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _Out_ PWCHAR ShortName,
    _Out_ PULONG ShortNameLength)
{
    WCHAR Base[6];
    WCHAR Extension[3];
    ULONG Start = 0;
    ULONG LastDot = MAXULONG;
    ULONG BaseLength;
    ULONG ExtensionLength = 0;

    *ShortNameLength = 0;

    while (Start < NameLength && Name[Start] == L'.')
        Start++;
    for (ULONG Index = Start; Index < NameLength; Index++)
    {
        if (Name[Index] == L'.')
            LastDot = Index;
    }

    BaseLength = CopyShortNameCharacters(Name + Start,
                                         (LastDot == MAXULONG ? NameLength : LastDot) - Start,
                                         Base,
                                         RTL_NUMBER_OF(Base));
    if (LastDot != MAXULONG)
    {
        ExtensionLength = CopyShortNameCharacters(Name + LastDot + 1,
                                                  NameLength - LastDot - 1,
                                                  Extension,
                                                  RTL_NUMBER_OF(Extension));
    }
    if (BaseLength == 0)
        return STATUS_NOT_FOUND;

    for (ULONG Tail = 1; Tail <= 999999; Tail++)
    {
        Directory Index(DiskVolume);
        WCHAR Digits[7];
        ULONG DigitCount = 0;
        ULONG Value = Tail;
        ULONG Keep;
        ULONG Length = 0;
        ULONGLONG Reference;
        NTSTATUS Status;

        while (Value)
        {
            Digits[DigitCount++] = (WCHAR)(L'0' + Value % 10);
            Value /= 10;
        }

        Keep = BaseLength < 7 - DigitCount ? BaseLength : 7 - DigitCount;
        for (ULONG Index2 = 0; Index2 < Keep; Index2++)
            ShortName[Length++] = Base[Index2];
        ShortName[Length++] = L'~';
        while (DigitCount)
            ShortName[Length++] = Digits[--DigitCount];
        if (ExtensionLength)
        {
            ShortName[Length++] = L'.';
            for (ULONG Index2 = 0; Index2 < ExtensionLength; Index2++)
                ShortName[Length++] = Extension[Index2];
        }
        ShortName[Length] = L'\0';

        Status = Index.FindNextFile(Parent, ShortName, &Reference);
        if (Status == STATUS_NOT_FOUND)
        {
            *ShortNameLength = Length;
            return STATUS_SUCCESS;
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_OBJECT_NAME_COLLISION;
}

NTSTATUS
MasterFileTable::CreateFile(
    _Inout_ PWCHAR Query,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _Out_ PFileRecord* File)
{
    PFileRecord Parent = NULL;
    PWCHAR Name;
    ULONG NameLength;
    NTSTATUS Status;

    if (!File)
        return STATUS_INVALID_PARAMETER;
    *File = NULL;
    if (!Query || !DiskVolume)
        return STATUS_INVALID_PARAMETER;

    Status = SplitAndResolveParent(
        Query,
        TRUE,
        &Parent,
        &Name,
        &NameLength);
    if (NT_SUCCESS(Status))
    {
        Status = CreateFileInDirectory(
            Parent,
            Name,
            NameLength,
            IsDirectory,
            FileAttributes,
            FALSE,
            File);
    }
    delete Parent;
    return Status;
}

NTSTATUS
MasterFileTable::CreateFileInDirectory(
    _In_ PFileRecord Parent,
    _In_reads_(NameLength) PWCHAR Name,
    _In_ ULONG NameLength,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _In_ BOOLEAN NameKnownMissing,
    _Out_ PFileRecord* File)
{
    Directory ParentIndex(DiskVolume);
    PFileRecord NewFile = NULL;
    PFileNameEx FileName = NULL;
    PFileNameEx ShortValue = NULL;
    WCHAR ShortName[13];
    ULONG ShortNameLength = 0;
    ULONGLONG ExistingReference;
    ULONGLONG FileReference;
    BOOLEAN RecordPublished = FALSE;
    NTSTATUS RollbackStatus;
    NTSTATUS Status;

    if (!File)
        return STATUS_INVALID_PARAMETER;
    *File = NULL;
    if (!DiskVolume || !Parent ||
        !Parent->Header ||
        !(Parent->Header->Flags & FR_IS_DIRECTORY))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if ((FileAttributes &
         ~NTFS_CREATE_MUTABLE_ATTRIBUTES) != 0 ||
        ((FileAttributes & FILE_PERM_NORMAL) &&
         FileAttributes != FILE_PERM_NORMAL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfsValidateComponentName(Name,
                                       NameLength);
    if (!NT_SUCCESS(Status))
        goto Done;


    if (!NameKnownMissing)
    {
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
    }

    if (DiskVolume->Generate8dot3Names &&
        !IsLegalShortName(Name, NameLength))
    {
        Status = GenerateShortName(DiskVolume,
                                   Parent,
                                   Name,
                                   NameLength,
                                   ShortName,
                                   &ShortNameLength);
        if (Status == STATUS_NOT_FOUND)
            ShortNameLength = 0;
        else if (!NT_SUCCESS(Status))
            goto Done;
    }

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

    if (ShortNameLength)
    {
        PAttribute ShortAttribute;
        PAttribute NameAttribute;
        PFileNameEx NameValue;
        ULONG Offset = 0;

        FileName->NameType = NAME_TYPE_WIN32;
        Status = InsertFileNameLink(NewFile,
                                    FileName,
                                    MakeFileReference(Parent->Header),
                                    ShortName,
                                    ShortNameLength,
                                    &ShortAttribute,
                                    &ShortValue);
        if (!NT_SUCCESS(Status))
            goto Rollback;
        ShortValue->NameType = NAME_TYPE_DOS;
        NewFile->Header->HardLinkCount = 2;

        FileName = NULL;
        ShortValue = NULL;
        while (NT_SUCCESS(Status = EnumerateFileNames(NewFile,
                                                       &Offset,
                                                       &NameAttribute,
                                                       &NameValue)) &&
               NameAttribute)
        {
            if (NameValue->NameType == NAME_TYPE_DOS)
                ShortValue = NameValue;
            else
                FileName = NameValue;
        }
        if (!NT_SUCCESS(Status))
            goto Rollback;
        if (!FileName || !ShortValue)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Rollback;
        }
    }

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

    if (ShortValue)
    {
        Directory ShortIndex(DiskVolume);

        Status = ShortIndex.AddFileToDirectory(
            Parent,
            FileReference,
            ShortValue);
        if (!NT_SUCCESS(Status))
        {
            Directory UndoIndex(DiskVolume);
            UNICODE_STRING LongName = NtfsMakeCountedUnicodeString(
                FileName->Name,
                (USHORT)(FileName->NameLength * sizeof(WCHAR)));

            RollbackStatus = UndoIndex.RemoveFileFromDirectory(
                Parent,
                FileReference,
                &LongName);
            if (!NT_SUCCESS(RollbackStatus))
            {
                RecordPublished = TRUE;
                goto Done;
            }
            goto Rollback;
        }
    }
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
    return Status;
}
