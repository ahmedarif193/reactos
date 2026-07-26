/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Initialize newly allocated NTFS file records
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

NTSTATUS
FileRecord::InitializeNewFileRecord(
    _In_ FileRecord* Parent,
    _In_reads_(NameLength) PWSTR Name,
    _In_ ULONG NameLength,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _Out_ PFileNameEx* CreatedName)
{
    const ULONG StandardInformationV1Length =
        FIELD_OFFSET(StandardInformationEx, OwnerId);
    PAttribute Attribute;
    PAttribute ParentSecurity;
    PFileNameEx FileName;
    PStandardInformationEx Standard;
    PIndexRootEx IndexRoot;
    PIndexEntry EndEntry;
    PUCHAR FileNameData = NULL;
    ULONG FileNameDataLength;
    ULONG NameBytes;
    ULONG SecurityLength;
    ULONG RootDataLength;
    ULONG NormalizedAttributes;
    ULONGLONG CurrentTime;
    ULONGLONG ParentReference;
    NTSTATUS Status;

    if (!CreatedName)
        return STATUS_INVALID_PARAMETER;
    *CreatedName = NULL;
    if (!DiskVolume || !Header || !Data ||
        !Parent || !Parent->Header || !Parent->Data ||
        !Name || NameLength == 0 ||
        NameLength > NTFS_MAX_FILE_NAME_LENGTH ||
        !(Parent->Header->Flags & FR_IS_DIRECTORY) ||
        Header->BaseFileRecord != 0 ||
        !(Header->Flags & FR_IN_USE) ||
        !!(Header->Flags & FR_IS_DIRECTORY) !=
            !!IsDirectory ||
        (FileAttributes &
         ~NTFS_CREATE_MUTABLE_ATTRIBUTES) != 0 ||
        ((FileAttributes & FILE_PERM_NORMAL) &&
         FileAttributes != FILE_PERM_NORMAL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Parent->Header->SequenceNumber == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /* FILE_PERM_NORMAL is the "no other bits" marker; it stores as zero. */
    NormalizedAttributes =
        FileAttributes == FILE_PERM_NORMAL
            ? 0
            : FileAttributes;

    NameBytes = NameLength * sizeof(WCHAR);
    if (NameBytes >
        MAXULONG - FIELD_OFFSET(FileNameEx, Name))
    {
        return STATUS_NAME_TOO_LONG;
    }
    FileNameDataLength =
        FIELD_OFFSET(FileNameEx, Name) +
        NameBytes;
    FileNameData =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[FileNameDataLength];
    if (!FileNameData)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(FileNameData,
                  FileNameDataLength);

    Status = NtfsQuerySystemTime(&CurrentTime);
    if (!NT_SUCCESS(Status))
        goto Done;
    ParentReference =
        MakeFileReference(Parent->Header);

    Status = InsertResidentAttribute(
        TypeStandardInformation,
        NULL,
        &Attribute);
    if (!NT_SUCCESS(Status))
        goto Done;
    Standard =
        reinterpret_cast<PStandardInformationEx>(
            FileNameData);
    Standard->CreationTime = CurrentTime;
    Standard->LastWriteTime = CurrentTime;
    Standard->ChangeTime = CurrentTime;
    Standard->LastAccessTime = CurrentTime;
    Standard->FilePermissions = NormalizedAttributes;
    Status = ReplaceResidentData(
        Attribute,
        FileNameData,
        StandardInformationV1Length);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = InsertResidentAttribute(
        TypeFileName,
        NULL,
        &Attribute);
    if (!NT_SUCCESS(Status))
        goto Done;
    FileName =
        reinterpret_cast<PFileNameEx>(
            FileNameData);
    RtlZeroMemory(FileNameData,
                  FileNameDataLength);
    FileName->ParentFileReference =
        ParentReference;
    FileName->CreationTime = CurrentTime;
    FileName->LastWriteTime = CurrentTime;
    FileName->ChangeTime = CurrentTime;
    FileName->LastAccessTime = CurrentTime;
    FileName->Flags =
        NormalizedAttributes |
        (IsDirectory ? FN_DIRECTORY : 0);
    FileName->NameLength = (UCHAR)NameLength;
    FileName->NameType = NAME_TYPE_POSIX;
    RtlCopyMemory(FileName->Name,
                  Name,
                  NameBytes);
    Status = ReplaceResidentData(
        Attribute,
        FileNameData,
        FileNameDataLength);
    if (!NT_SUCCESS(Status))
        goto Done;
    Attribute->Resident.IndexedFlag = 1;

    /*
     * Legacy NTFS volumes keep a per-file descriptor. Preserve a compact
     * resident parent descriptor when one is present; NTFS 3.x SecurityId
     * inheritance and nonresident legacy descriptors are handled by the
     * dedicated $Secure mutation work.
     */
    ParentSecurity = Parent->GetAttribute(
        TypeSecurityDescriptor,
        NULL);
    if (ParentSecurity &&
        !ParentSecurity->IsNonResident &&
        NT_SUCCESS(Parent->
            ValidateResidentAttributeForUpdate(
                ParentSecurity,
                &SecurityLength)) &&
        SecurityLength != 0)
    {
        Status = InsertResidentAttribute(
            TypeSecurityDescriptor,
            NULL,
            &Attribute);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = ReplaceResidentData(
            Attribute,
            reinterpret_cast<PUCHAR>(
                GetResidentDataPointer(
                    ParentSecurity)),
            SecurityLength);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    if (IsDirectory)
    {
        RootDataLength =
            FIELD_OFFSET(IndexRootEx, Header) +
            sizeof(IndexNodeHeader) +
            ALIGN_UP_BY(
                FIELD_OFFSET(IndexEntry,
                             IndexStream),
                sizeof(ULONGLONG));
        if (RootDataLength >
            FileNameDataLength)
        {
            delete[] FileNameData;
            FileNameData =
                new(PagedPool, TAG_FILE_RECORD)
                    UCHAR[RootDataLength];
            if (!FileNameData)
            {
                Status =
                    STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }
        }
        RtlZeroMemory(FileNameData,
                      RootDataLength);
        IndexRoot =
            reinterpret_cast<PIndexRootEx>(
                FileNameData);
        IndexRoot->AttributeType =
            TypeFileName;
        IndexRoot->CollationRule =
            ATTRDEF_COLLATION_FILENAME;
        IndexRoot->BytesPerIndexRec =
            BytesPerIndexRecord(DiskVolume);
        IndexRoot->ClusPerIndexRec =
            (UCHAR)
                DiskVolume->ClustersPerIndexRecord;
        IndexRoot->Header.IndexOffset =
            sizeof(IndexNodeHeader);
        IndexRoot->Header.TotalIndexSize =
            RootDataLength -
            FIELD_OFFSET(IndexRootEx, Header);
        IndexRoot->Header.AllocatedSize =
            IndexRoot->Header.TotalIndexSize;
        IndexRoot->Header.Flags = 0;

        EndEntry =
            reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(
                    &IndexRoot->Header) +
                IndexRoot->Header.IndexOffset);
        EndEntry->EntryLength =
            (USHORT)ALIGN_UP_BY(
                FIELD_OFFSET(IndexEntry,
                             IndexStream),
                sizeof(ULONGLONG));
        EndEntry->Flags = INDEX_ENTRY_END;

        Status = InsertResidentAttribute(
            TypeIndexRoot,
            const_cast<PWSTR>(NtfsI30Name),
            &Attribute);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = ReplaceResidentData(
            Attribute,
            FileNameData,
            RootDataLength);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    else
    {
        Status = InsertResidentAttribute(
            TypeData,
            NULL,
            &Attribute);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    Header->HardLinkCount = 1;
    Attribute = GetAttribute(
        TypeFileName,
        NULL);
    if (!Attribute ||
        Attribute->IsNonResident ||
        Attribute->Resident.DataLength !=
            FileNameDataLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    *CreatedName =
        reinterpret_cast<PFileNameEx>(
            GetResidentDataPointer(Attribute));
    Status = STATUS_SUCCESS;

Done:
    delete[] FileNameData;
    return Status;
}
