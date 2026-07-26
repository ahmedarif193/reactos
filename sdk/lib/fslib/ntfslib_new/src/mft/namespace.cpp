/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Delete, rename, and hard-link namespace operations
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

/*
 * The multi-record sequences below are ordered so every intermediate
 * on-disk state stays readable and favors recoverable leaks over
 * cross-links: names disappear before records, records are blanked
 * before their clusters return to $Bitmap. Crash atomicity across a
 * whole operation still requires the future LFS transaction work.
 */

static NTSTATUS
AppendFilteredRuns(
    _Inout_ PDataRun* Head,
    _Inout_ PDataRun* Tail,
    _In_opt_ PDataRun Source)
{
    for (PDataRun Run = Source;
         Run;
         Run = Run->NextRun)
    {
        PDataRun Copy;

        if (Run->Length == 0)
            return STATUS_FILE_CORRUPT_ERROR;
        if (Run->IsSparse)
            continue;
        if (Run->LCN >
            ~(ULONGLONG)0 - Run->Length)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Copy =
            new(PagedPool, TAG_DATA_RUN)
                DataRun();
        if (!Copy)
            return STATUS_INSUFFICIENT_RESOURCES;
        Copy->NextRun = NULL;
        Copy->LCN = Run->LCN;
        Copy->Length = Run->Length;
        Copy->IsSparse = FALSE;
        if (*Tail)
            (*Tail)->NextRun = Copy;
        else
            *Head = Copy;
        *Tail = Copy;
    }
    return STATUS_SUCCESS;
}

/*
 * Copies a $FILE_NAME's name into a caller-owned buffer and returns a
 * counted string pointing into it. Used to preserve a DOS/WIN32 alias
 * name across the record edits that would otherwise invalidate it.
 */
static UNICODE_STRING
CaptureAlias(
    _In_ PFileNameEx AliasValue,
    _Out_ PWCHAR AliasNameBuffer)
{
    ULONG Bytes = AliasValue->NameLength * sizeof(WCHAR);

    RtlCopyMemory(AliasNameBuffer, AliasValue->Name, Bytes);
    AliasNameBuffer[AliasValue->NameLength] = 0;
    return NtfsMakeCountedUnicodeString(AliasNameBuffer, Bytes);
}

/*
 * Walks the resident $FILE_NAME attributes of one base record. Offset
 * starts at zero and advances across calls; a NULL attribute result
 * means the walk is complete.
 */
NTSTATUS
MasterFileTable::EnumerateFileNames(
    _In_ PFileRecord File,
    _Inout_ PULONG Offset,
    _Out_ PAttribute* Attribute,
    _Out_ PFileNameEx* FileName)
{
    PFileRecordHeader Header = File->Header;
    PUCHAR Data = File->Data;

    *Attribute = NULL;
    *FileName = NULL;

    if (*Offset == 0)
        *Offset = Header->AttributeOffset;
    if (Header->AttributeOffset <
            sizeof(FileRecordHeader) ||
        Header->ActualSize >
            Header->AllocatedSize ||
        Header->AllocatedSize >
            File->RecordBufferSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    while (*Offset + 0x18 <= Header->ActualSize)
    {
        PAttribute Candidate =
            reinterpret_cast<PAttribute>(
                Data + *Offset);

        if (Candidate->AttributeType ==
            TypeAttributeEndMarker)
        {
            return STATUS_SUCCESS;
        }
        if (Candidate->Length < 0x18 ||
            (Candidate->Length &
             (sizeof(ULONGLONG) - 1)) != 0 ||
            Candidate->Length >
                Header->ActualSize - *Offset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (Candidate->AttributeType >
            (ULONG)TypeFileName)
        {
            return STATUS_SUCCESS;
        }
        if (Candidate->AttributeType ==
                (ULONG)TypeFileName &&
            !Candidate->IsNonResident)
        {
            PFileNameEx Value;

            if (Candidate->Resident.DataOffset <
                    0x18 ||
                Candidate->Resident.DataOffset >
                    Candidate->Length ||
                Candidate->Resident.DataLength >
                    Candidate->Length -
                    Candidate->
                        Resident.DataOffset ||
                Candidate->Resident.DataLength <
                    FIELD_OFFSET(FileNameEx,
                                 Name))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            Value =
                reinterpret_cast<PFileNameEx>(
                    GetResidentDataPointer(
                        Candidate));
            if (Value->NameLength *
                    sizeof(WCHAR) >
                Candidate->Resident.DataLength -
                    FIELD_OFFSET(FileNameEx,
                                 Name))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            *Offset += Candidate->Length;
            *Attribute = Candidate;
            *FileName = Value;
            return STATUS_SUCCESS;
        }
        *Offset += Candidate->Length;
    }
    return STATUS_FILE_CORRUPT_ERROR;
}

/*
 * Finds the resident $FILE_NAME carrying exactly this parent and name,
 * and reports any DOS/WIN32 alias forming a pair with it.
 */
NTSTATUS
MasterFileTable::FindFileNamePair(
    _In_ PFileRecord File,
    _In_ ULONGLONG ParentReference,
    _In_ PUNICODE_STRING Name,
    _Out_ PAttribute* NameAttribute,
    _Out_ PFileNameEx* NameValue,
    _Out_ PAttribute* AliasAttribute,
    _Out_ PFileNameEx* AliasValue)
{
    ULONG Offset = 0;
    NTSTATUS Status;

    *NameAttribute = NULL;
    *NameValue = NULL;
    *AliasAttribute = NULL;
    *AliasValue = NULL;

    for (;;)
    {
        PAttribute Attribute;
        PFileNameEx Value;
        UNICODE_STRING Candidate;
        LONG Comparison;

        Status = EnumerateFileNames(
            File,
            &Offset,
            &Attribute,
            &Value);
        if (!NT_SUCCESS(Status))
            return Status;
        if (!Attribute)
            break;
        if (Value->ParentFileReference !=
            ParentReference)
        {
            continue;
        }

        Candidate =
            NtfsMakeCountedUnicodeString(
                Value->Name,
                Value->NameLength *
                    sizeof(WCHAR));
        Status = DiskVolume->CompareFileNames(
            Name,
            &Candidate,
            &Comparison);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Comparison == 0 &&
            !*NameAttribute)
        {
            *NameAttribute = Attribute;
            *NameValue = Value;
        }
        else if (!*AliasAttribute)
        {
            *AliasAttribute = Attribute;
            *AliasValue = Value;
        }
    }

    if (!*NameAttribute)
        return STATUS_NOT_FOUND;

    /*
     * Only a DOS/WIN32 sibling of the removed name forms a dying pair;
     * an unrelated POSIX name in the same directory is a hard link.
     */
    if (*AliasAttribute)
    {
        UCHAR NameType = (*NameValue)->NameType;
        UCHAR AliasType =
            (*AliasValue)->NameType;

        if (!((NameType == NAME_TYPE_WIN32 &&
               AliasType == NAME_TYPE_DOS) ||
              (NameType == NAME_TYPE_DOS &&
               AliasType == NAME_TYPE_WIN32)))
        {
            *AliasAttribute = NULL;
            *AliasValue = NULL;
        }
    }
    return STATUS_SUCCESS;
}

/*
 * Removes one directory name from a file record, together with its
 * DOS/WIN32 alias when one exists. The alias is re-found after the primary
 * removal because that removal compacts the record and invalidates the
 * earlier pointer. Link-count adjustment stays with the caller.
 */
NTSTATUS
MasterFileTable::RemoveFileNameFromRecord(
    _In_ PFileRecord File,
    _In_ ULONGLONG ParentReference,
    _In_ PUNICODE_STRING Name)
{
    PAttribute NameAttribute;
    PAttribute AliasAttribute;
    PFileNameEx NameValue;
    PFileNameEx AliasValue;
    WCHAR AliasName[NTFS_MAX_FILE_NAME_LENGTH + 1];
    UNICODE_STRING AliasString;
    BOOLEAN HasAlias;
    NTSTATUS Status;

    Status = FindFileNamePair(
        File,
        ParentReference,
        Name,
        &NameAttribute,
        &NameValue,
        &AliasAttribute,
        &AliasValue);
    if (!NT_SUCCESS(Status))
        return Status;

    HasAlias = AliasAttribute != NULL;
    if (HasAlias)
        AliasString = CaptureAlias(AliasValue, AliasName);

    Status = File->RemoveAttributeRecord(NameAttribute);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!HasAlias)
        return STATUS_SUCCESS;

    Status = FindFileNamePair(
        File,
        ParentReference,
        &AliasString,
        &NameAttribute,
        &NameValue,
        &AliasAttribute,
        &AliasValue);
    if (!NT_SUCCESS(Status))
        return Status;
    return File->RemoveAttributeRecord(NameAttribute);
}

/*
 * Publishes an additional $FILE_NAME on a record: a new POSIX name copying
 * the duplicated header fields (timestamps, sizes, flags) from an existing
 * name, reparented to NewParentReference. The caller raises the link count,
 * commits the record, and adds the matching index entry.
 */
NTSTATUS
MasterFileTable::InsertFileNameLink(
    _In_ PFileRecord File,
    _In_ const FileNameEx* Source,
    _In_ ULONGLONG ParentReference,
    _In_reads_(NameLength) PWCHAR Name,
    _In_ ULONG NameLength,
    _Out_ PAttribute* NewAttribute,
    _Out_ PFileNameEx* NewValue)
{
    PUCHAR NewData;
    PFileNameEx Value;
    ULONG NewDataLength;
    NTSTATUS Status;

    *NewAttribute = NULL;
    *NewValue = NULL;

    NewDataLength =
        FIELD_OFFSET(FileNameEx, Name) +
        NameLength * sizeof(WCHAR);
    NewData =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[NewDataLength];
    if (!NewData)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(NewData, NewDataLength);
    Value = reinterpret_cast<PFileNameEx>(NewData);
    RtlCopyMemory(
        Value,
        Source,
        FIELD_OFFSET(FileNameEx, NameLength));
    Value->ParentFileReference = ParentReference;
    Value->NameLength = (UCHAR)NameLength;
    Value->NameType = NAME_TYPE_POSIX;
    RtlCopyMemory(Value->Name,
                  Name,
                  NameLength * sizeof(WCHAR));

    PAttribute Inserted;

    Status = File->InsertResidentAttribute(
        TypeFileName,
        NULL,
        &Inserted);
    if (NT_SUCCESS(Status))
    {
        Status = File->ReplaceResidentData(
            Inserted,
            NewData,
            NewDataLength);
    }
    delete[] NewData;
    if (!NT_SUCCESS(Status))
        return Status;
    Inserted->Resident.IndexedFlag = 1;
    *NewAttribute = Inserted;
    *NewValue =
        reinterpret_cast<PFileNameEx>(
            GetResidentDataPointer(Inserted));
    return STATUS_SUCCESS;
}

NTSTATUS
MasterFileTable::LookupParentAndChild(
    _Inout_ PWCHAR Query,
    _Out_ PFileRecord* Parent,
    _Out_ PFileRecord* Child,
    _Out_ PULONGLONG ChildReference,
    _Out_ PWCHAR* Name,
    _Out_ PULONG NameLength)
{
    Directory ParentIndex(DiskVolume);
    NTSTATUS Status;

    *Child = NULL;
    *ChildReference = 0;

    /* Existing-name lookup: accept any component within the length bound. */
    Status = SplitAndResolveParent(
        Query,
        FALSE,
        Parent,
        Name,
        NameLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ParentIndex.FindNextFile(
        *Parent,
        *Name,
        ChildReference);
    if (!NT_SUCCESS(Status))
        goto Fail;
    if (GetFRNFromFileRef(*ChildReference) >
        MAXULONG)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Fail;
    }

    Status = GetFileRecord(
        (ULONG)GetFRNFromFileRef(
            *ChildReference),
        Child);
    if (!NT_SUCCESS(Status))
        goto Fail;
    if (!(*Child)->Header ||
        !((*Child)->Header->Flags &
          FR_IN_USE) ||
        (*Child)->Header->BaseFileRecord != 0 ||
        (*Child)->Header->SequenceNumber !=
            GetSequenceFromFileRef(
                *ChildReference))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Fail;
    }
    if ((*Child)->Header->MFTRecordNumber <=
        NTFS_LAST_RESERVED_FILE_RECORD)
    {
        Status = STATUS_ACCESS_DENIED;
        goto Fail;
    }
    return STATUS_SUCCESS;

Fail:
    delete *Child;
    delete *Parent;
    *Parent = NULL;
    *Child = NULL;
    return Status;
}

/*
 * Gathers every physical cluster still owned by a dying file: each
 * VCN-zero nonresident attribute contributes its complete composite
 * run list, and a nonresident $ATTRIBUTE_LIST contributes its own.
 */
NTSTATUS
MasterFileTable::CollectReleasableRuns(
    _In_ PFileRecord File,
    _Out_ PDataRun* Runs)
{
    PAttribute ListAttribute;
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    NTSTATUS Status;

    *Runs = NULL;

    ListAttribute = File->GetAttribute(
        TypeAttributeList,
        NULL);
    if (ListAttribute)
    {
        ULONG Offset = 0;

        if (ListAttribute->IsNonResident)
        {
            Status = AppendFilteredRuns(
                &Head,
                &Tail,
                File->GetCachedDataRuns(
                    ListAttribute));
            if (!NT_SUCCESS(Status))
                goto Fail;
        }
        Status = File->LoadAttributeList();
        if (!NT_SUCCESS(Status))
            goto Fail;

        while (Offset +
               FIELD_OFFSET(AttributeListEx,
                            Padding) <=
               File->AttributeListLength)
        {
            PAttributeListEx Entry =
                reinterpret_cast<
                    PAttributeListEx>(
                        File->
                            AttributeListData +
                        Offset);
            PFileRecord Owner;
            PAttribute Attribute;
            WCHAR EntryName[
                NTFS_MAX_FILE_NAME_LENGTH + 1];

            if (Entry->RecordLength <
                    FIELD_OFFSET(AttributeListEx,
                                 Padding) ||
                (Entry->RecordLength &
                 (sizeof(ULONGLONG) - 1)) != 0 ||
                Entry->RecordLength >
                    File->AttributeListLength -
                    Offset ||
                Entry->NameOffset +
                    Entry->NameLength *
                        sizeof(WCHAR) >
                    Entry->RecordLength)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Fail;
            }

            /*
             * The list's own runs were captured above; a self-entry in
             * the list must not add them twice.
             */
            if (Entry->FirstVCN != 0 ||
                Entry->Type ==
                    (UINT32)TypeAttributeList)
            {
                Offset += Entry->RecordLength;
                continue;
            }

            Owner =
                GetFRNFromFileRef(
                    Entry->BaseFileRef) ==
                File->Header->MFTRecordNumber
                ? File
                : File->GetExtentRecord(
                      Entry->BaseFileRef);
            if (!Owner)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Fail;
            }
            if (Entry->NameLength != 0)
            {
                RtlCopyMemory(
                    EntryName,
                    reinterpret_cast<PUCHAR>(
                        Entry) +
                        Entry->NameOffset,
                    Entry->NameLength *
                        sizeof(WCHAR));
            }
            EntryName[Entry->NameLength] = 0;

            Attribute =
                Owner->FindAttributeInRecord(
                    (AttributeType)Entry->Type,
                    Entry->NameLength != 0
                        ? EntryName
                        : NULL,
                    &Entry->AttributeId);
            if (!Attribute)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Fail;
            }
            if (Attribute->IsNonResident)
            {
                Status = AppendFilteredRuns(
                    &Head,
                    &Tail,
                    File->GetCachedDataRuns(
                        Attribute));
                if (!NT_SUCCESS(Status))
                    goto Fail;
            }
            Offset += Entry->RecordLength;
        }
    }
    else
    {
        ULONG Offset =
            File->Header->AttributeOffset;

        while (Offset + 0x18 <=
               File->Header->ActualSize)
        {
            PAttribute Attribute =
                reinterpret_cast<PAttribute>(
                    File->Data + Offset);

            if (Attribute->AttributeType ==
                TypeAttributeEndMarker)
            {
                break;
            }
            if (Attribute->Length < 0x18 ||
                (Attribute->Length &
                 (sizeof(ULONGLONG) - 1)) != 0 ||
                Attribute->Length >
                    File->Header->ActualSize -
                    Offset)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Fail;
            }
            if (Attribute->IsNonResident)
            {
                Status = AppendFilteredRuns(
                    &Head,
                    &Tail,
                    File->GetCachedDataRuns(
                        Attribute));
                if (!NT_SUCCESS(Status))
                    goto Fail;
            }
            Offset += Attribute->Length;
        }
    }

    *Runs = Head;
    return STATUS_SUCCESS;

Fail:
    FreeDataRun(Head);
    return Status;
}

/*
 * Gathers the distinct extension-record references a file's $ATTRIBUTE_LIST
 * points at (its own base record excluded), for teardown. A file with no
 * attribute list yields an empty set.
 */
NTSTATUS
MasterFileTable::CollectDistinctExtentReferences(
    _In_ PFileRecord File,
    _Out_ PULONGLONG ExtentReferences,
    _In_ ULONG Capacity,
    _Out_ PULONG ExtentCount)
{
    ULONG Offset = 0;
    NTSTATUS Status;

    *ExtentCount = 0;
    if (!File->GetAttribute(TypeAttributeList, NULL))
        return STATUS_SUCCESS;
    Status = File->LoadAttributeList();
    if (!NT_SUCCESS(Status))
        return Status;

    while (Offset +
           FIELD_OFFSET(AttributeListEx, Padding) <=
           File->AttributeListLength)
    {
        PAttributeListEx Entry =
            reinterpret_cast<PAttributeListEx>(
                File->AttributeListData + Offset);
        BOOLEAN Known = FALSE;

        if (Entry->RecordLength <
                FIELD_OFFSET(AttributeListEx, Padding) ||
            Entry->RecordLength >
                File->AttributeListLength - Offset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        Offset += Entry->RecordLength;

        if (GetFRNFromFileRef(Entry->BaseFileRef) ==
            File->Header->MFTRecordNumber)
        {
            continue;
        }
        for (ULONG Index = 0;
             Index < *ExtentCount;
             Index++)
        {
            if (ExtentReferences[Index] ==
                Entry->BaseFileRef)
            {
                Known = TRUE;
                break;
            }
        }
        if (Known)
            continue;
        if (*ExtentCount == Capacity)
            return STATUS_FILE_CORRUPT_ERROR;
        ExtentReferences[(*ExtentCount)++] =
            Entry->BaseFileRef;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MasterFileTable::DeleteFile(
    _Inout_ PWCHAR Query,
    _In_ BOOLEAN RemoveDirectory)
{
    PFileRecord Parent = NULL;
    PFileRecord Child = NULL;
    PAttribute NameAttribute;
    PAttribute AliasAttribute;
    PFileNameEx NameValue;
    PFileNameEx AliasValue;
    PDataRun ReleaseRuns = NULL;
    PWCHAR Name;
    ULONGLONG ChildReference;
    ULONGLONG ParentReference;
    ULONGLONG ExtentReferences[64];
    ULONG ExtentCount = 0;
    ULONG NameLength;
    ULONG RemovedLinks;
    UNICODE_STRING NameString;
    WCHAR AliasName[
        NTFS_MAX_FILE_NAME_LENGTH + 1];
    UNICODE_STRING AliasString = {};
    BOOLEAN IsDirectory;
    NTSTATUS Status;

    if (!Query || !DiskVolume)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;

    Status = LookupParentAndChild(
        Query,
        &Parent,
        &Child,
        &ChildReference,
        &Name,
        &NameLength);
    if (!NT_SUCCESS(Status))
        return Status;
    ParentReference =
        MakeFileReference(Parent->Header);

    IsDirectory =
        !!(Child->Header->Flags &
           FR_IS_DIRECTORY);
    if (IsDirectory != !!RemoveDirectory)
    {
        Status = IsDirectory
            ? STATUS_FILE_IS_A_DIRECTORY
            : STATUS_NOT_A_DIRECTORY;
        goto Done;
    }
    if (IsDirectory)
    {
        Directory Enumerator(DiskVolume);
        NtfsDirectoryEntry Entry;

        Status = Enumerator.LoadDirectory(
            Child);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = Enumerator.GetNextEntry(
            TRUE,
            &Entry);
        if (NT_SUCCESS(Status))
        {
            Status =
                STATUS_DIRECTORY_NOT_EMPTY;
            goto Done;
        }
        if (Status != STATUS_NO_MORE_FILES)
            goto Done;
    }

    NameString = NtfsMakeCountedUnicodeString(
        Name,
        NameLength * sizeof(WCHAR));
    Status = FindFileNamePair(
        Child,
        ParentReference,
        &NameString,
        &NameAttribute,
        &NameValue,
        &AliasAttribute,
        &AliasValue);
    if (!NT_SUCCESS(Status))
        goto Done;
    RemovedLinks = 1;
    if (AliasAttribute)
    {
        AliasString = CaptureAlias(AliasValue, AliasName);
        RemovedLinks = 2;
    }

    {
        Directory ParentIndex(DiskVolume);

        Status =
            ParentIndex.RemoveFileFromDirectory(
                Parent,
                ChildReference,
                &NameString);
        if (!NT_SUCCESS(Status))
            goto Done;
        if (AliasAttribute)
        {
            Status =
                ParentIndex.
                    RemoveFileFromDirectory(
                        Parent,
                        ChildReference,
                        &AliasString);
            if (!NT_SUCCESS(Status))
                goto Done;
        }
    }

    if (Child->Header->HardLinkCount >
        RemovedLinks)
    {
        /* Other links survive: drop only these names. */
        Status = RemoveFileNameFromRecord(
            Child,
            ParentReference,
            &NameString);
        if (!NT_SUCCESS(Status))
            goto Done;
        Child->Header->HardLinkCount =
            (USHORT)(
                Child->Header->HardLinkCount -
                RemovedLinks);
        Status = Child->StampChangeTime();
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = WriteFileRecordToMFT(Child);
        goto Done;
    }

    /*
     * Last name: the record set and its allocation go away. Runs are
     * captured while the records are still alive, records are blanked
     * next, and only then do the clusters return to $Bitmap.
     */
    Status = CollectReleasableRuns(
        Child,
        &ReleaseRuns);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = CollectDistinctExtentReferences(
        Child,
        ExtentReferences,
        RTL_NUMBER_OF(ExtentReferences),
        &ExtentCount);
    if (!NT_SUCCESS(Status))
        goto Done;
    for (ULONG Index = 0;
         Index < ExtentCount;
         Index++)
    {
        PFileRecord Extent =
            Child->GetExtentRecord(
                ExtentReferences[Index]);

        if (!Extent)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = DeallocateExtensionFileRecord(
            Extent);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    Status = DeallocateBaseFileRecord(Child);
    if (!NT_SUCCESS(Status))
        goto Done;

    if (ReleaseRuns)
    {
        NTSTATUS ReleaseStatus =
            DiskVolume->ReleaseClusters(
                ReleaseRuns);
        if (!NT_SUCCESS(ReleaseStatus))
        {
            DPRINT1(
                "Deleted file's clusters were "
                "not all released: 0x%lx.\n",
                ReleaseStatus);
        }
    }

Done:
    FreeDataRun(ReleaseRuns);
    delete Child;
    delete Parent;
    return Status;
}

NTSTATUS
MasterFileTable::CreateHardLink(
    _Inout_ PWCHAR ExistingQuery,
    _Inout_ PWCHAR NewQuery)
{
    Directory NewParentIndex(DiskVolume);
    PFileRecord OldParent = NULL;
    PFileRecord Child = NULL;
    PFileRecord NewParent = NULL;
    PAttribute NameAttribute;
    PAttribute NewAttribute;
    PFileNameEx SourceValue;
    PFileNameEx NewValue;
    PWCHAR OldName;
    PWCHAR NewName;
    ULONGLONG ChildReference;
    ULONGLONG ExistingReference;
    ULONGLONG NewParentReference;
    ULONG OldNameLength;
    ULONG NewNameLength;
    ULONG WalkOffset = 0;
    UNICODE_STRING NameString;
    NTSTATUS Status;

    if (!ExistingQuery || !NewQuery ||
        !DiskVolume)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;

    Status = LookupParentAndChild(
        ExistingQuery,
        &OldParent,
        &Child,
        &ChildReference,
        &OldName,
        &OldNameLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Child->Header->Flags &
        FR_IS_DIRECTORY)
    {
        Status = STATUS_FILE_IS_A_DIRECTORY;
        goto Done;
    }

    Status = SplitAndResolveParent(
        NewQuery,
        TRUE,
        &NewParent,
        &NewName,
        &NewNameLength);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (NewParent->Header->MFTRecordNumber ==
        OldParent->Header->MFTRecordNumber)
    {
        /*
         * One directory, one record object: two loaded copies would
         * desynchronize as soon as either side commits.
         */
        delete NewParent;
        NewParent = OldParent;
    }
    NewParentReference =
        MakeFileReference(NewParent->Header);

    Status = NewParentIndex.FindNextFile(
        NewParent,
        NewName,
        &ExistingReference);
    if (NT_SUCCESS(Status))
    {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Done;
    }
    if (Status != STATUS_NOT_FOUND)
        goto Done;

    /* Copy the duplicated information from any existing name. */
    Status = EnumerateFileNames(
        Child,
        &WalkOffset,
        &NameAttribute,
        &SourceValue);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (!NameAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Status = InsertFileNameLink(
        Child,
        SourceValue,
        NewParentReference,
        NewName,
        NewNameLength,
        &NewAttribute,
        &NewValue);
    if (!NT_SUCCESS(Status))
        goto Done;
    Child->Header->HardLinkCount++;
    Status = Child->StampChangeTime();
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = WriteFileRecordToMFT(Child);
    if (!NT_SUCCESS(Status))
        goto Done;

    NameString = NtfsMakeCountedUnicodeString(
        NewValue->Name,
        NewNameLength * sizeof(WCHAR));
    Status = NewParentIndex.AddFileToDirectory(
        NewParent,
        ChildReference,
        NewValue);
    if (!NT_SUCCESS(Status))
    {
        /* Unpublish the link the record already carries. */
        NTSTATUS RollbackStatus =
            RemoveFileNameFromRecord(
                Child,
                NewParentReference,
                &NameString);
        if (NT_SUCCESS(RollbackStatus))
        {
            Child->Header->HardLinkCount--;
            RollbackStatus =
                WriteFileRecordToMFT(Child);
        }
        if (!NT_SUCCESS(RollbackStatus))
        {
            DPRINT1(
                "Hard-link rollback failed: "
                "0x%lx.\n",
                RollbackStatus);
        }
    }

Done:
    if (NewParent != OldParent)
        delete NewParent;
    delete Child;
    delete OldParent;
    return Status;
}

/*
 * Splits a path into its final component and parent directory and resolves
 * the parent. ValidateName applies new-name character rules (creation,
 * rename, and link destinations); an existing-name lookup instead accepts
 * any component within the length bound. On success Parent is caller-owned.
 */
NTSTATUS
MasterFileTable::SplitAndResolveParent(
    _Inout_ PWCHAR Query,
    _In_ BOOLEAN ValidateName,
    _Out_ PFileRecord* Parent,
    _Out_ PWCHAR* Name,
    _Out_ PULONG NameLength)
{
    SIZE_T QueryCharacters;
    ULONG NameOffset;
    WCHAR SavedCharacter;
    NTSTATUS Status;

    *Parent = NULL;
    *Name = NULL;
    *NameLength = 0;

    QueryCharacters = NtfsWcsLen(Query);
    while (QueryCharacters != 0 &&
           Query[QueryCharacters - 1] == L'\\')
    {
        QueryCharacters--;
    }
    if (QueryCharacters == 0 ||
        QueryCharacters > MAXULONG)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }
    Query[QueryCharacters] = L'\0';

    NameOffset = (ULONG)QueryCharacters;
    while (NameOffset != 0 &&
           Query[NameOffset - 1] != L'\\')
    {
        NameOffset--;
    }
    *Name = Query + NameOffset;
    *NameLength =
        (ULONG)QueryCharacters - NameOffset;
    if (ValidateName)
    {
        Status = NtfsValidateComponentName(
            *Name,
            *NameLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else if (*NameLength == 0 ||
             *NameLength > NTFS_MAX_FILE_NAME_LENGTH)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    if (NameOffset == 0)
    {
        Status = GetFileRecord(_Root, Parent);
    }
    else
    {
        SavedCharacter = Query[NameOffset - 1];
        Query[NameOffset - 1] = L'\0';
        Status = Query[0] == L'\0'
            ? GetFileRecord(_Root, Parent)
            : GetFileRecordFromQuery(
                  Query,
                  Parent);
        Query[NameOffset - 1] = SavedCharacter;
    }
    if (!NT_SUCCESS(Status))
        return Status;
    if (!*Parent ||
        !((*Parent)->Header->Flags &
          FR_IS_DIRECTORY))
    {
        delete *Parent;
        *Parent = NULL;
        return STATUS_NOT_A_DIRECTORY;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MasterFileTable::RenameFile(
    _Inout_ PWCHAR OldQuery,
    _Inout_ PWCHAR NewQuery)
{
    Directory OldParentIndex(DiskVolume);
    Directory NewParentIndex(DiskVolume);
    PFileRecord OldParent = NULL;
    PFileRecord Child = NULL;
    PFileRecord NewParent = NULL;
    PAttribute NameAttribute;
    PAttribute AliasAttribute;
    PAttribute NewAttribute;
    PFileNameEx NameValue;
    PFileNameEx AliasValue;
    PFileNameEx NewValue;
    PWCHAR OldName;
    PWCHAR NewName;
    ULONGLONG ChildReference;
    ULONGLONG ExistingReference;
    ULONGLONG OldParentReference;
    ULONGLONG NewParentReference;
    ULONG OldNameLength;
    ULONG NewNameLength;
    ULONG RemovedLinks = 1;
    UNICODE_STRING OldNameString;
    UNICODE_STRING NewNameString;
    WCHAR AliasName[
        NTFS_MAX_FILE_NAME_LENGTH + 1];
    UNICODE_STRING AliasString = {};
    BOOLEAN CaseOnly;
    BOOLEAN NewEntryAdded = FALSE;
    NTSTATUS Status;

    if (!OldQuery || !NewQuery || !DiskVolume)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;

    Status = LookupParentAndChild(
        OldQuery,
        &OldParent,
        &Child,
        &ChildReference,
        &OldName,
        &OldNameLength);
    if (!NT_SUCCESS(Status))
        return Status;
    OldParentReference =
        MakeFileReference(OldParent->Header);

    Status = SplitAndResolveParent(
        NewQuery,
        TRUE,
        &NewParent,
        &NewName,
        &NewNameLength);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (NewParent->Header->MFTRecordNumber ==
        OldParent->Header->MFTRecordNumber)
    {
        /*
         * One directory, one record object: two loaded copies would
         * desynchronize as soon as either side commits.
         */
        delete NewParent;
        NewParent = OldParent;
    }
    NewParentReference =
        MakeFileReference(NewParent->Header);

    OldNameString =
        NtfsMakeCountedUnicodeString(
            OldName,
            OldNameLength * sizeof(WCHAR));
    NewNameString =
        NtfsMakeCountedUnicodeString(
            NewName,
            NewNameLength * sizeof(WCHAR));
    if (NewParentReference ==
        OldParentReference)
    {
        LONG Comparison;

        Status = DiskVolume->CompareFileNames(
            &OldNameString,
            &NewNameString,
            &Comparison);
        if (!NT_SUCCESS(Status))
            goto Done;
        CaseOnly = Comparison == 0;
    }
    else
    {
        CaseOnly = FALSE;
    }
    if (CaseOnly &&
        OldNameLength == NewNameLength &&
        RtlCompareMemory(
            OldName,
            NewName,
            NewNameLength * sizeof(WCHAR)) ==
            NewNameLength * sizeof(WCHAR))
    {
        Status = STATUS_SUCCESS;
        goto Done;
    }

    Status = NewParentIndex.FindNextFile(
        NewParent,
        NewName,
        &ExistingReference);
    if (NT_SUCCESS(Status))
    {
        if (!(CaseOnly &&
              ExistingReference ==
                  ChildReference))
        {
            Status =
                STATUS_OBJECT_NAME_COLLISION;
            goto Done;
        }
    }
    else if (Status != STATUS_NOT_FOUND)
    {
        goto Done;
    }

    Status = FindFileNamePair(
        Child,
        OldParentReference,
        &OldNameString,
        &NameAttribute,
        &NameValue,
        &AliasAttribute,
        &AliasValue);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (AliasAttribute)
    {
        AliasString = CaptureAlias(AliasValue, AliasName);
        RemovedLinks = 2;
    }

    if (CaseOnly)
    {
        /*
         * The colliding old entry must leave the index before the new
         * spelling can enter it. This touches only the parent index, so
         * the source name below stays valid.
         */
        Status =
            OldParentIndex.
                RemoveFileFromDirectory(
                    OldParent,
                    ChildReference,
                    &OldNameString);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    Status = InsertFileNameLink(
        Child,
        NameValue,
        NewParentReference,
        NewName,
        NewNameLength,
        &NewAttribute,
        &NewValue);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = WriteFileRecordToMFT(Child);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = NewParentIndex.AddFileToDirectory(
        NewParent,
        ChildReference,
        NewValue);
    if (!NT_SUCCESS(Status))
        goto Rollback;
    NewEntryAdded = TRUE;

    if (!CaseOnly)
    {
        Status =
            OldParentIndex.
                RemoveFileFromDirectory(
                    OldParent,
                    ChildReference,
                    &OldNameString);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }
    if (AliasAttribute)
    {
        Status =
            OldParentIndex.
                RemoveFileFromDirectory(
                    OldParent,
                    ChildReference,
                    &AliasString);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }

    /* Drop the old on-record names and settle the link count. */
    Status = RemoveFileNameFromRecord(
        Child,
        OldParentReference,
        &OldNameString);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (RemovedLinks > 1)
    {
        Child->Header->HardLinkCount =
            (USHORT)(
                Child->Header->HardLinkCount -
                (RemovedLinks - 1));
    }
    Status = Child->StampChangeTime();
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = WriteFileRecordToMFT(Child);
    goto Done;

Rollback:
    {
        NTSTATUS RollbackStatus = Status;

        if (NewEntryAdded)
        {
            (void)NewParentIndex.
                RemoveFileFromDirectory(
                    NewParent,
                    ChildReference,
                    &NewNameString);
        }
        if (NT_SUCCESS(RemoveFileNameFromRecord(
                Child,
                NewParentReference,
                &NewNameString)))
        {
            (void)WriteFileRecordToMFT(Child);
        }
        if (CaseOnly)
        {
            /* Republish the original spelling. */
            PAttribute OriginalAttribute;
            PFileNameEx OriginalValue;
            PAttribute OriginalAlias;
            PFileNameEx OriginalAliasValue;

            if (NT_SUCCESS(FindFileNamePair(
                    Child,
                    OldParentReference,
                    &OldNameString,
                    &OriginalAttribute,
                    &OriginalValue,
                    &OriginalAlias,
                    &OriginalAliasValue)))
            {
                (void)OldParentIndex.
                    AddFileToDirectory(
                        OldParent,
                        ChildReference,
                        OriginalValue);
            }
        }
        Status = RollbackStatus;
    }

Done:
    if (NewParent != OldParent)
        delete NewParent;
    delete Child;
    delete OldParent;
    return Status;
}
