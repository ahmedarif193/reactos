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
Volume::LoadAttributeDefinitions()
{
    NTSTATUS Status;
    PFileRecord AttrDefFile;
    PAttribute DataAttr;
    PAttrDefEntry TableEntry;
    ULONG AttrDefDataSize, EntryCount, MaxIndex;
    PUCHAR Buffer = NULL;

    Status = MFT->GetFileAttributeFromFileRecordNumber(TypeData,
                                                       NULL,
                                                       _AttrDef,
                                                       &AttrDefFile,
                                                       &DataAttr);

    if (!NT_SUCCESS(Status))
        return Status;

    Status = AttrDefFile->ReadAttributeAlloc(DataAttr,
                                             &Buffer,
                                             &AttrDefDataSize);
    if (!NT_SUCCESS(Status))
        goto Done;

    MaxIndex = AttrDefDataSize / sizeof(AttrDefEntry);
    TableEntry = (PAttrDefEntry)Buffer;
    EntryCount = 0;
    while (EntryCount < MaxIndex &&
           TableEntry[EntryCount].AttributeType != 0)
    {
        ULONG LabelLength;
        for (LabelLength = 0;
             LabelLength < RTL_NUMBER_OF(TableEntry[EntryCount].Label) &&
             TableEntry[EntryCount].Label[LabelLength] != L'\0';
             LabelLength++)
        {
        }

        if (LabelLength == RTL_NUMBER_OF(TableEntry[EntryCount].Label))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        EntryCount++;
    }

    if (EntryCount == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    AttrDefCache = new(PagedPool, TAG_NTFS) AttrDefCacheEntry[EntryCount];
    if (!AttrDefCache)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    AttrDefCacheCount = EntryCount;
    for (ULONG Index = 0; Index < EntryCount; Index++)
    {
        AttrDefCache[Index].Type =
            AttributeType(TableEntry[Index].AttributeType);
        RtlCopyMemory(AttrDefCache[Index].Label,
                      TableEntry[Index].Label,
                      sizeof(AttrDefCache[Index].Label));
    }
    Status = STATUS_SUCCESS;

Done:
    delete AttrDefFile;
    if (Buffer)
        delete[] Buffer;
    return Status;
}

NTSTATUS
Volume::GetAttributeTypeFromName(_In_  PWSTR AttributeTypeName,
                                 _Out_ AttributeType* Type)
{
    NTSTATUS Status;
    ULONG NameLength, NameCompareLength;

    if (!AttributeTypeName || !Type)
        return STATUS_INVALID_PARAMETER;

    if (!AttrDefCache)
    {
        Status = LoadAttributeDefinitions();
        if (!NT_SUCCESS(Status))
            return Status;
    }

    NameLength = NtfsWcsLen(AttributeTypeName);
    if (NameLength >= RTL_NUMBER_OF(AttrDefCache[0].Label))
        return STATUS_NOT_FOUND;

    Status = UpcaseWideString(AttributeTypeName, NameLength);
    if (!NT_SUCCESS(Status))
        return Status;

    NameCompareLength = NameLength * sizeof(WCHAR);
    for (ULONG Index = 0; Index < AttrDefCacheCount; Index++)
    {
        if (NtfsWcsLen(AttrDefCache[Index].Label) == NameLength &&
            RtlCompareMemory(AttrDefCache[Index].Label,
                             AttributeTypeName,
                             NameCompareLength) == NameCompareLength)
        {
            *Type = AttrDefCache[Index].Type;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

/* Allocates a NUL-terminated copy of the first Length characters of Source. */
static PWSTR
CopyStreamName(_In_ PCWSTR Source,
               _In_ ULONG Length)
{
    PWSTR StreamName = new(NonPagedPool) WCHAR[Length + 1];

    if (!StreamName)
        return NULL;
    if (Length != 0)
        RtlCopyMemory(StreamName, Source, Length * sizeof(WCHAR));
    StreamName[Length] = L'\0';
    return StreamName;
}

NTSTATUS
Volume::GetADSPreference(_In_  PUNICODE_STRING FileName,
                         _Out_ AttributeType* RequestedType,
                         _Out_ PWSTR* RequestedStream)
{
    NTSTATUS Status;
    ULONG CharacterCount;
    ULONG FirstColon = MAXULONG;
    ULONG SecondColon = MAXULONG;
    ULONG StreamLength;
    ULONG TypeLength;
    PWSTR TypeName = NULL;

    if (!FileName || !RequestedType || !RequestedStream)
        return STATUS_INVALID_PARAMETER;

    *RequestedType = TypeData;
    *RequestedStream = NULL;

    if ((FileName->Length & (sizeof(WCHAR) - 1)) != 0 ||
        FileName->MaximumLength < FileName->Length ||
        (!FileName->Buffer && FileName->Length != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    CharacterCount = FileName->Length / sizeof(WCHAR);

    /*
     * UNICODE_STRING buffers are counted and need not have a trailing NUL.
     * Locate at most two ADS separators without reading or modifying bytes
     * outside the declared path. A stream is valid only on the final path
     * component.
     */
    for (ULONG Index = 0; Index < CharacterCount; Index++)
    {
        WCHAR Character = FileName->Buffer[Index];

        if (Character == L'\0')
            return STATUS_OBJECT_NAME_INVALID;
        if (Character == L'\\')
        {
            if (FirstColon != MAXULONG)
                return STATUS_OBJECT_NAME_INVALID;
            continue;
        }
        if (Character != L':')
            continue;

        if (FirstColon == MAXULONG)
            FirstColon = Index;
        else if (SecondColon == MAXULONG)
            SecondColon = Index;
        else
            return STATUS_OBJECT_NAME_INVALID;
    }

    if (FirstColon == MAXULONG)
        return STATUS_SUCCESS;
    if (FirstColon == 0 ||
        FileName->Buffer[FirstColon - 1] == L'\\')
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    if (SecondColon == MAXULONG)
    {
        StreamLength = CharacterCount - FirstColon - 1;
    }
    else
    {
        StreamLength = SecondColon - FirstColon - 1;
        TypeLength = CharacterCount - SecondColon - 1;
        if (TypeLength == 0)
            return STATUS_OBJECT_NAME_INVALID;

        TypeName = CopyStreamName(
            FileName->Buffer + SecondColon + 1,
            TypeLength);
        if (!TypeName)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = GetAttributeTypeFromName(TypeName,
                                          RequestedType);
        delete[] TypeName;
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to find ADS attribute type!\n");
            return Status == STATUS_NOT_FOUND
                ? STATUS_OBJECT_NAME_INVALID
                : Status;
        }
    }

    if (StreamLength != 0)
    {
        *RequestedStream = CopyStreamName(
            FileName->Buffer + FirstColon + 1,
            StreamLength);
        if (!*RequestedStream)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}
