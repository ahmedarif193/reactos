/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Native $REPARSE_POINT creation, replacement, and deletion
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static BOOLEAN
IsMicrosoftReparseTag(_In_ UINT32 ReparseTag)
{
    return !!(ReparseTag & ((UINT32)0x80000000));
}

static void
CopyReparseGuid(_In_ const UCHAR* Buffer,
                _Out_ GUID* ReparseGuid)
{
    RtlCopyMemory(
        ReparseGuid,
        Buffer + sizeof(ReparsePointEx),
        sizeof(*ReparseGuid));
}

static BOOLEAN
ReparseGuidsEqual(_In_ const GUID* Left,
                  _In_ const GUID* Right)
{
    return RtlCompareMemory(Left,
                            Right,
                            sizeof(*Left)) ==
           sizeof(*Left);
}

static NTSTATUS
CheckDirectoryIsEmpty(_In_ PVolume DiskVolume,
                      _In_ PFileRecord File)
{
    NtfsDirectoryEntry Entry;
    Directory DirectoryView(DiskVolume);
    NTSTATUS Status;

    Status = DirectoryView.LoadDirectory(File);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DirectoryView.GetNextEntry(TRUE, &Entry);
    if (NT_SUCCESS(Status))
        return STATUS_DIRECTORY_NOT_EMPTY;
    return Status == STATUS_NO_MORE_FILES
        ? STATUS_SUCCESS
        : Status;
}

static NTSTATUS
GetPackedEaSize(_In_ PFileRecord File,
                _Out_ PUSHORT PackedEaSize,
                _Out_opt_ PBOOLEAN HasExtendedAttributes)
{
    EAInformationEx Information;
    ULONG Length = 0;
    NTSTATUS Status;

    if (!File || !PackedEaSize)
        return STATUS_INVALID_PARAMETER;
    *PackedEaSize = 0;
    if (HasExtendedAttributes)
        *HasExtendedAttributes = FALSE;

    Status = File->ReadExtendedAttributes(
        NULL,
        &Length,
        &Information);
    if (Status == STATUS_NO_EAS_ON_FILE)
        return STATUS_SUCCESS;
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;

    *PackedEaSize = Information.PackedEASize;
    if (HasExtendedAttributes)
        *HasExtendedAttributes = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::SetReparsePoint(
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength)
{
    NTSTATUS Status;

    Status = NtfsValidateReparseBuffer(Buffer,
                                       BufferLength,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    return UpdateReparsePoint(Buffer,
                              BufferLength,
                              FALSE);
}

NTSTATUS
FileRecord::DeleteReparsePoint(
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength)
{
    ReparsePointEx Header;
    ULONG ExpectedLength;
    NTSTATUS Status;

    if (!Buffer ||
        BufferLength < sizeof(Header))
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    RtlCopyMemory(&Header, Buffer, sizeof(Header));
    if (Header.ReparseType <= 1)
        return STATUS_IO_REPARSE_TAG_INVALID;
    if (Header.ReparseDataLength != 0)
        return STATUS_IO_REPARSE_DATA_INVALID;

    ExpectedLength = IsMicrosoftReparseTag(
        Header.ReparseType)
        ? sizeof(ReparsePointEx)
        : sizeof(ThirdPartyReparsePointEx);
    if (BufferLength != ExpectedLength)
        return STATUS_IO_REPARSE_DATA_INVALID;

    Status = NtfsValidateReparseBuffer(Buffer,
                                       BufferLength,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    return UpdateReparsePoint(Buffer,
                              BufferLength,
                              TRUE);
}

NTSTATUS
FileRecord::UpdateReparsePoint(
    _In_opt_ const UCHAR* Buffer,
    _In_ ULONG BufferLength,
    _In_ BOOLEAN Delete)
{
    PDataRun OldRuns = NULL;
    PAttribute DataAttribute;
    PAttribute ReparseAttribute;
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    PUCHAR ExistingData = NULL;
    PUCHAR FinalData = NULL;
    PUCHAR RecordBackup = NULL;
    ReparsePointEx ExistingHeader;
    ReparsePointEx RequestedHeader;
    GUID ExistingGuid = {};
    GUID RequestedGuid = {};
    ULONGLONG ExistingLength64;
    ULONGLONG DataLength;
    ULONG ExistingLength = 0;
    USHORT PackedEaSize = 0;
    BOOLEAN HasExtendedAttributes = FALSE;
    BOOLEAN IsDirectory;
    BOOLEAN Promote = FALSE;
    BOOLEAN Committed = FALSE;
    UINT32 FileNameFields;
    NTSTATUS Status;

    if (!Buffer || BufferLength < sizeof(ReparsePointEx))
        return STATUS_INVALID_PARAMETER;
    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (FindAttributeInRecord(TypeAttributeList,
                              NULL,
                              NULL))
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = NtfsValidateReparseBuffer(
        Buffer,
        BufferLength,
        &RequestedHeader);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!IsMicrosoftReparseTag(
            RequestedHeader.ReparseType))
    {
        CopyReparseGuid(Buffer, &RequestedGuid);
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);

    ReparseAttribute =
        FindAttributeInRecord(TypeReparsePoint,
                              NULL,
                              NULL);
    if (!!ReparseAttribute !=
        !!(Standard->FilePermissions &
           FILE_PERM_REPARSE_PT))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (ReparseAttribute)
    {
        if (ReparseAttribute->NameLength != 0 ||
            ReparseAttribute->Flags != 0 ||
            GetAttributeOwner(ReparseAttribute) != this ||
            (ReparseAttribute->IsNonResident &&
             ReparseAttribute->NonResident.FirstVCN != 0))
        {
            return STATUS_NOT_IMPLEMENTED;
        }

        ExistingLength64 =
            GetAttributeDataSize(ReparseAttribute);
        if (ExistingLength64 <
                sizeof(ReparsePointEx) ||
            ExistingLength64 >
                NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
        {
            return STATUS_IO_REPARSE_DATA_INVALID;
        }
        ExistingLength = (ULONG)ExistingLength64;
        ExistingData =
            new(PagedPool, TAG_NTFS)
                UCHAR[ExistingLength];
        if (!ExistingData)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = ReadReparsePoint(ExistingData,
                                  &ExistingLength);
        if (!NT_SUCCESS(Status))
            goto Done;
        RtlCopyMemory(&ExistingHeader,
                      ExistingData,
                      sizeof(ExistingHeader));
        if (!IsMicrosoftReparseTag(
                ExistingHeader.ReparseType))
        {
            CopyReparseGuid(ExistingData,
                            &ExistingGuid);
        }

        if (ExistingHeader.ReparseType !=
            RequestedHeader.ReparseType)
        {
            Status = STATUS_IO_REPARSE_TAG_MISMATCH;
            goto Done;
        }
        if (!IsMicrosoftReparseTag(
                ExistingHeader.ReparseType) &&
            !ReparseGuidsEqual(&ExistingGuid,
                               &RequestedGuid))
        {
            Status =
                STATUS_REPARSE_ATTRIBUTE_CONFLICT;
            goto Done;
        }
    }
    else if (Delete)
    {
        Status = STATUS_NOT_A_REPARSE_POINT;
        goto Done;
    }

    IsDirectory = !!(Header->Flags & FR_IS_DIRECTORY);
    if (!Delete)
    {
        if (RequestedHeader.ReparseType ==
                NTFS_IO_REPARSE_TAG_MOUNT_POINT &&
            !IsDirectory)
        {
            Status = STATUS_NOT_A_DIRECTORY;
            goto Done;
        }

        if (IsDirectory)
        {
            Status = CheckDirectoryIsEmpty(
                DiskVolume,
                this);
            if (!NT_SUCCESS(Status))
                goto Done;
        }
        else if (RequestedHeader.ReparseType ==
                 NTFS_IO_REPARSE_TAG_SYMLINK)
        {
            DataAttribute = GetAttribute(TypeData,
                                         NULL);
            if (!DataAttribute)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            DataLength =
                GetAttributeDataSize(DataAttribute);
            if (DataLength != 0)
            {
                Status =
                    STATUS_IO_REPARSE_DATA_INVALID;
                goto Done;
            }
        }
    }

    Status = GetPackedEaSize(this,
                             &PackedEaSize,
                             &HasExtendedAttributes);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (!Delete && !ReparseAttribute &&
        HasExtendedAttributes)
    {
        Status = STATUS_EAS_NOT_SUPPORTED;
        goto Done;
    }

    if (ReparseAttribute &&
        ReparseAttribute->IsNonResident)
    {
        OldRuns =
            FindNonResidentData(ReparseAttribute);
        if (!OldRuns)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    if (ReparseAttribute)
    {
        Status = RemoveAttributeRecord(
            ReparseAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
        ReparseAttribute = NULL;
    }

    if (!Delete)
    {
        FinalData =
            new(PagedPool, TAG_NTFS)
                UCHAR[BufferLength];
        if (!FinalData)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Restore;
        }
        RtlCopyMemory(FinalData,
                      Buffer,
                      BufferLength);
        reinterpret_cast<PReparsePointEx>(
            FinalData)->Padding = 0;

        Status = InsertResidentAttribute(
            TypeReparsePoint,
            NULL,
            &ReparseAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Status = ReplaceResidentData(
            ReparseAttribute,
            FinalData,
            BufferLength);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            Promote = TRUE;
            Status = STATUS_SUCCESS;
        }
        else if (!NT_SUCCESS(Status))
        {
            goto Restore;
        }
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    UNREFERENCED_PARAMETER(StandardAttribute);

    if (Delete)
    {
        Standard->FilePermissions &=
            ~FILE_PERM_REPARSE_PT;
    }
    else
    {
        Standard->FilePermissions |=
            FILE_PERM_REPARSE_PT;
    }
    FileNameFields =
        NTFS_FILE_NAME_UPDATE_REPARSE_TAG;
    if (Delete)
        FileNameFields |=
            NTFS_FILE_NAME_UPDATE_EA_SIZE;
    if (!IsDirectory)
    {
        Standard->FilePermissions |=
            FILE_PERM_ARCHIVE;
        FileNameFields |=
            NTFS_FILE_NAME_UPDATE_ARCHIVE;
    }

    Status = SynchronizeFileNameInformation(
        FileNameFields,
        0,
        0,
        PackedEaSize,
        Delete ? 0 : RequestedHeader.ReparseType,
        0);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;

    if (Promote)
    {
        Status = PromoteResidentData(
            ReparseAttribute,
            FinalData,
            BufferLength,
            0,
            BufferLength,
            BufferLength);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Committed = TRUE;
    }
    else
    {
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(this);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Committed = TRUE;
    }

    if (OldRuns)
        Status = DiskVolume->ReleaseClusters(OldRuns);
    goto Done;

Restore:
    if (!Committed)
    {
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

Done:
    if (Committed)
        InvalidateWofCompression();
    FreeDataRun(OldRuns);
    delete[] RecordBackup;
    delete[] FinalData;
    delete[] ExistingData;
    return Status;
}
