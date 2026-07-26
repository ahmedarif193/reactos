/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Query and update $STANDARD_INFORMATION
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_MUTABLE_BASIC_ATTRIBUTES \
    (FILE_PERM_READONLY | FILE_PERM_HIDDEN | FILE_PERM_SYSTEM | \
     FILE_PERM_ARCHIVE | FILE_PERM_NORMAL | FILE_PERM_TEMP | \
     FILE_PERM_OFFLINE | FILE_PERM_NOT_INDXED)

NTSTATUS
FileRecord::GetStandardInformationForUpdate(
    _Out_ PAttribute* Attribute,
    _Out_ PStandardInformationEx* Standard)
{
    PAttribute Target;
    ULONG DataLength;
    NTSTATUS Status;

    if (!Attribute || !Standard)
        return STATUS_INVALID_PARAMETER;
    *Attribute = NULL;
    *Standard = NULL;

    Target = GetAttribute(TypeStandardInformation,
                          NULL);
    if (!Target)
        return STATUS_FILE_CORRUPT_ERROR;

    /*
     * $STANDARD_INFORMATION is always resident and unnamed. NTFS 1.x stores
     * a 0x30-byte value while NTFS 3.x extends it to 0x48 bytes; the common
     * timestamp/attribute prefix ends at FilePermissions.
     */
    Status = ValidateResidentAttributeForUpdate(
        Target,
        &DataLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Target->AttributeType !=
            TypeStandardInformation ||
        Target->NameLength != 0 ||
        Target->Flags != 0 ||
        DataLength <
            FIELD_OFFSET(StandardInformationEx,
                         FilePermissions) +
                sizeof(ULONG))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *Attribute = Target;
    *Standard =
        reinterpret_cast<PStandardInformationEx>(
            GetResidentDataPointer(Target));
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::GetBasicInformation(
    _Out_ PNtfsFileBasicInformation Information)
{
    PAttribute Attribute;
    PStandardInformationEx Standard;
    NTSTATUS Status;

    if (!Information)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Information, sizeof(*Information));

    Status = GetStandardInformationForUpdate(
        &Attribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(Attribute);

    Information->Fields = NTFS_BASIC_INFO_ALL_FIELDS;
    Information->CreationTime =
        Standard->CreationTime;
    Information->LastAccessTime =
        Standard->LastAccessTime;
    Information->LastWriteTime =
        Standard->LastWriteTime;
    Information->ChangeTime =
        Standard->ChangeTime;
    Information->FileAttributes =
        Standard->FilePermissions;
    if (Header->Flags & FR_IS_DIRECTORY)
    {
        Information->FileAttributes |=
            FILE_ATTRIBUTE_DIRECTORY;
    }
    if (Information->FileAttributes == 0)
    {
        Information->FileAttributes =
            FILE_PERM_NORMAL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::SetAutomaticTimestampMask(
    _In_ UINT32 Fields)
{
    if (Fields & ~NTFS_AUTOMATIC_TIMESTAMP_FIELDS)
        return STATUS_INVALID_PARAMETER;
    AutomaticTimestampMask = Fields;
    return STATUS_SUCCESS;
}

UINT32
FileRecord::GetAutomaticTimestampMask() const
{
    return AutomaticTimestampMask;
}

BOOLEAN
FileRecord::HasAutomaticTimestampUpdate(
    _In_ UINT32 Fields) const
{
    return Header &&
           Header->BaseFileRecord == 0 &&
           Header->MFTRecordNumber >
               NTFS_LAST_RESERVED_FILE_RECORD &&
           (Fields &
            AutomaticTimestampMask &
            NTFS_AUTOMATIC_TIMESTAMP_FIELDS) != 0;
}

NTSTATUS
FileRecord::PrepareAutomaticTimestamps(
    _In_ UINT32 Fields,
    _Out_opt_ PBOOLEAN Changed)
{
    PAttribute Attribute;
    PStandardInformationEx Standard;
    ULONGLONG CurrentTime;
    UINT32 EffectiveFields;
    NTSTATUS Status;

    if (Changed)
        *Changed = FALSE;
    if (Fields & ~NTFS_AUTOMATIC_TIMESTAMP_FIELDS)
        return STATUS_INVALID_PARAMETER;
    if (!HasAutomaticTimestampUpdate(Fields))
        return STATUS_SUCCESS;

    EffectiveFields =
        Fields & AutomaticTimestampMask;
    Status = GetStandardInformationForUpdate(
        &Attribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(Attribute);

    Status = NtfsQuerySystemTime(&CurrentTime);
    if (!NT_SUCCESS(Status))
        return Status;

    if (EffectiveFields &
        NTFS_BASIC_INFO_LAST_ACCESS_TIME)
    {
        Standard->LastAccessTime = CurrentTime;
    }
    if (EffectiveFields &
        NTFS_BASIC_INFO_LAST_WRITE_TIME)
    {
        Standard->LastWriteTime = CurrentTime;
    }
    if (EffectiveFields &
        NTFS_BASIC_INFO_CHANGE_TIME)
    {
        Standard->ChangeTime = CurrentTime;
    }
    if (Changed)
        *Changed = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::UpdateAutomaticTimestamps(
    _In_ UINT32 Fields)
{
    PUCHAR RecordBackup;
    BOOLEAN Changed;
    NTSTATUS Status;

    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (Fields & ~NTFS_AUTOMATIC_TIMESTAMP_FIELDS)
        return STATUS_INVALID_PARAMETER;
    if (!HasAutomaticTimestampUpdate(Fields))
        return STATUS_SUCCESS;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    Status = PrepareAutomaticTimestamps(
        Fields,
        &Changed);
    if (NT_SUCCESS(Status) && Changed)
    {
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(this);
    }
    if (!NT_SUCCESS(Status))
    {
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::TouchDirectory()
{
    PAttribute Attribute;
    PStandardInformationEx Standard;
    PUCHAR RecordBackup;
    ULONGLONG CurrentTime;
    NTSTATUS Status;

    if (!DiskVolume || !Header ||
        !(Header->Flags & FR_IS_DIRECTORY))
    {
        return STATUS_NOT_A_DIRECTORY;
    }
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;

    Status = GetStandardInformationForUpdate(
        &Attribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(Attribute);

    Status = NtfsQuerySystemTime(&CurrentTime);
    if (!NT_SUCCESS(Status))
        return Status;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    Standard->LastWriteTime = CurrentTime;
    Standard->ChangeTime = CurrentTime;
    Status = DiskVolume->MFT->
        WriteFileRecordToMFT(this);
    if (!NT_SUCCESS(Status))
    {
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

    delete[] RecordBackup;
    return Status;
}

/*
 * Stamps $STANDARD_INFORMATION's change time in memory. The caller commits
 * the record; this only refreshes the timestamp inside a larger mutation.
 */
NTSTATUS
FileRecord::StampChangeTime()
{
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    ULONGLONG CurrentTime;
    NTSTATUS Status;

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);

    Status = NtfsQuerySystemTime(&CurrentTime);
    if (!NT_SUCCESS(Status))
        return Status;
    Standard->ChangeTime = CurrentTime;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::SetBasicInformation(
    _In_ const NtfsFileBasicInformation* Information)
{
    PAttribute Attribute;
    PStandardInformationEx Standard;
    PUCHAR RecordBackup;
    ULONG RequestedAttributes;
    ULONG StructuralMask;
    NTSTATUS Status;

    if (!Information || !DiskVolume)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (Information->Fields &
        ~NTFS_BASIC_INFO_ALL_FIELDS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Information->Fields == 0)
        return STATUS_SUCCESS;

    Status = GetStandardInformationForUpdate(
        &Attribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(Attribute);

    if (Information->Fields &
        NTFS_BASIC_INFO_FILE_ATTRIBUTES)
    {
        RequestedAttributes =
            Information->FileAttributes;
        if ((RequestedAttributes &
                FILE_ATTRIBUTE_DIRECTORY) &&
            !(Header->Flags & FR_IS_DIRECTORY))
        {
            return STATUS_INVALID_PARAMETER;
        }
        if ((RequestedAttributes &
                FILE_PERM_NORMAL) &&
            RequestedAttributes !=
                FILE_PERM_NORMAL)
        {
            return STATUS_INVALID_PARAMETER;
        }

        /*
         * Basic information may replace user-settable flags. Structural
         * flags (directory, sparse, compressed, reparse, encrypted, and
         * future bits) are maintained by their dedicated NTFS operations.
         * Accept an already-present structural bit for query/modify/write
         * round trips, but never create one here.
         */
        StructuralMask =
            ~(NTFS_MUTABLE_BASIC_ATTRIBUTES |
              FILE_ATTRIBUTE_DIRECTORY);
        if ((RequestedAttributes &
             StructuralMask) &
            ~Standard->FilePermissions)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    if (Information->Fields &
        NTFS_BASIC_INFO_CREATION_TIME)
    {
        Standard->CreationTime =
            Information->CreationTime;
    }
    if (Information->Fields &
        NTFS_BASIC_INFO_LAST_ACCESS_TIME)
    {
        Standard->LastAccessTime =
            Information->LastAccessTime;
    }
    if (Information->Fields &
        NTFS_BASIC_INFO_LAST_WRITE_TIME)
    {
        Standard->LastWriteTime =
            Information->LastWriteTime;
    }
    if (Information->Fields &
        NTFS_BASIC_INFO_CHANGE_TIME)
    {
        Standard->ChangeTime =
            Information->ChangeTime;
    }
    if (Information->Fields &
        NTFS_BASIC_INFO_FILE_ATTRIBUTES)
    {
        Standard->FilePermissions =
            (Standard->FilePermissions &
             ~NTFS_MUTABLE_BASIC_ATTRIBUTES) |
            (Information->FileAttributes &
             NTFS_MUTABLE_BASIC_ATTRIBUTES);
    }

    if ((Information->Fields &
         NTFS_BASIC_INFO_FILE_ATTRIBUTES) &&
        !(Information->Fields &
          NTFS_BASIC_INFO_CHANGE_TIME))
    {
        Status = PrepareAutomaticTimestamps(
            NTFS_BASIC_INFO_CHANGE_TIME,
            NULL);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = DiskVolume->MFT->
        WriteFileRecordToMFT(this);
    if (NT_SUCCESS(Status))
    {
        delete[] RecordBackup;
        return STATUS_SUCCESS;
    }

Restore:
    RtlCopyMemory(Data,
                  RecordBackup,
                  RecordBufferSize);
    Header =
        reinterpret_cast<PFileRecordHeader>(Data);
    ClearDataRunCache();

    delete[] RecordBackup;
    return Status;
}
