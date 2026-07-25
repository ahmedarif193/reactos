/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file and directory information
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

static ULONG
NtfsNormalizedAttributes(
    _In_ const NTFS3G_ROS_FILE_INFORMATION *Information)
{
    return Information->Attributes ?
           Information->Attributes : FILE_ATTRIBUTE_NORMAL;
}

static VOID
NtfsFillBasicInformation(_In_ const NTFS3G_ROS_FILE_INFORMATION *Source,
                         _Out_ PFILE_BASIC_INFORMATION Destination)
{
    Destination->CreationTime.QuadPart = Source->CreationTime;
    Destination->LastAccessTime.QuadPart = Source->LastAccessTime;
    Destination->LastWriteTime.QuadPart = Source->LastWriteTime;
    Destination->ChangeTime.QuadPart = Source->ChangeTime;
    Destination->FileAttributes = NtfsNormalizedAttributes(Source);
}

static ULONG
NtfsQueryEaSize(_In_ PFILE_OBJECT FileObject)
{
    PFileContextBlock File = FileObject->FsContext;
    PHandleContextBlock Handle = FileObject->FsContext2;
    NTFS3G_ROS_FILE *CoreFile;
    size_t Length = 0;

    if (!File || !Handle || File->IsVolume)
        return 0;
    CoreFile = File->File ?
        File->File : Handle->DirectoryFile;
    if (!CoreFile ||
        Ntfs3gRosGetExtendedAttributes(
            CoreFile, NULL, 0, &Length) < 0 ||
        Length > MAXULONG)
        return 0;
    return (ULONG)Length;
}

static NTSTATUS
NtfsQueryFileInformation(_In_ PFILE_OBJECT FileObject,
                         _In_ FILE_INFORMATION_CLASS InformationClass,
                         _Out_writes_bytes_(Length) PVOID Buffer,
                         _In_ ULONG Length,
                         _Out_ PULONG BytesWritten)
{
    PFileContextBlock File = FileObject->FsContext;
    PHandleContextBlock Handle = FileObject->FsContext2;
    *BytesWritten = 0;
    if (!File || !Handle)
        return STATUS_INVALID_PARAMETER;

    switch (InformationClass) {
        case FileBasicInformation:
            if (Length < sizeof(FILE_BASIC_INFORMATION))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Buffer, sizeof(FILE_BASIC_INFORMATION));
            NtfsFillBasicInformation(&File->Information, Buffer);
            *BytesWritten = sizeof(FILE_BASIC_INFORMATION);
            return STATUS_SUCCESS;

        case FileStandardInformation:
        {
            PFILE_STANDARD_INFORMATION Standard = Buffer;

            if (Length < sizeof(*Standard))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Standard, sizeof(*Standard));
            Standard->AllocationSize.QuadPart = File->Information.AllocationSize;
            Standard->EndOfFile.QuadPart = File->Information.FileSize;
            Standard->NumberOfLinks = File->Information.LinkCount;
            Standard->DeletePending = File->DeletePending;
            Standard->Directory =
                (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY) != 0;
            *BytesWritten = sizeof(*Standard);
            return STATUS_SUCCESS;
        }

        case FileInternalInformation:
        {
            PFILE_INTERNAL_INFORMATION Internal = Buffer;

            if (Length < sizeof(*Internal))
                return STATUS_BUFFER_TOO_SMALL;
            Internal->IndexNumber.QuadPart = File->Information.FileId;
            *BytesWritten = sizeof(*Internal);
            return STATUS_SUCCESS;
        }

        case FileNameInformation:
        case FileNormalizedNameInformation:
        {
            PFILE_NAME_INFORMATION Name = Buffer;
            ULONG HeaderSize = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
            ULONG CopyLength;

            if (Length < HeaderSize)
                return STATUS_BUFFER_TOO_SMALL;
            Name->FileNameLength = File->FileName.Length;
            CopyLength = min(File->FileName.Length, Length - HeaderSize);
            RtlCopyMemory(Name->FileName, File->FileName.Buffer, CopyLength);
            *BytesWritten = HeaderSize + CopyLength;
            return CopyLength == File->FileName.Length ?
                   STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
        }

        case FileNetworkOpenInformation:
        {
            PFILE_NETWORK_OPEN_INFORMATION Network = Buffer;

            if (Length < sizeof(*Network))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Network, sizeof(*Network));
            Network->CreationTime.QuadPart = File->Information.CreationTime;
            Network->LastAccessTime.QuadPart = File->Information.LastAccessTime;
            Network->LastWriteTime.QuadPart = File->Information.LastWriteTime;
            Network->ChangeTime.QuadPart = File->Information.ChangeTime;
            Network->AllocationSize.QuadPart = File->Information.AllocationSize;
            Network->EndOfFile.QuadPart = File->Information.FileSize;
            Network->FileAttributes =
                NtfsNormalizedAttributes(&File->Information);
            *BytesWritten = sizeof(*Network);
            return STATUS_SUCCESS;
        }

        case FilePositionInformation:
        {
            PFILE_POSITION_INFORMATION Position = Buffer;

            if (Length < sizeof(*Position))
                return STATUS_BUFFER_TOO_SMALL;
            Position->CurrentByteOffset = FileObject->CurrentByteOffset;
            *BytesWritten = sizeof(*Position);
            return STATUS_SUCCESS;
        }

        case FileAttributeTagInformation:
        {
            PFILE_ATTRIBUTE_TAG_INFORMATION AttributeTag = Buffer;

            if (Length < sizeof(*AttributeTag))
                return STATUS_BUFFER_TOO_SMALL;
            AttributeTag->FileAttributes =
                NtfsNormalizedAttributes(&File->Information);
            AttributeTag->ReparseTag = 0;
            *BytesWritten = sizeof(*AttributeTag);
            return STATUS_SUCCESS;
        }

        case FileEaInformation:
        {
            PFILE_EA_INFORMATION Ea = Buffer;

            if (Length < sizeof(*Ea))
                return STATUS_BUFFER_TOO_SMALL;
            Ea->EaSize = NtfsQueryEaSize(FileObject);
            *BytesWritten = sizeof(*Ea);
            return STATUS_SUCCESS;
        }

        case FileAccessInformation:
        {
            PFILE_ACCESS_INFORMATION Access = Buffer;

            if (Length < sizeof(*Access))
                return STATUS_BUFFER_TOO_SMALL;
            Access->AccessFlags = Handle->DesiredAccess;
            *BytesWritten = sizeof(*Access);
            return STATUS_SUCCESS;
        }

        case FileModeInformation:
        {
            PFILE_MODE_INFORMATION Mode = Buffer;

            if (Length < sizeof(*Mode))
                return STATUS_BUFFER_TOO_SMALL;
            Mode->Mode = Handle->CreateOptions;
            *BytesWritten = sizeof(*Mode);
            return STATUS_SUCCESS;
        }

        case FileAlignmentInformation:
        {
            PFILE_ALIGNMENT_INFORMATION Alignment = Buffer;

            if (Length < sizeof(*Alignment))
                return STATUS_BUFFER_TOO_SMALL;
            Alignment->AlignmentRequirement =
                File->Volume->StorageDevice->
                    AlignmentRequirement;
            *BytesWritten = sizeof(*Alignment);
            return STATUS_SUCCESS;
        }

        case FileStreamInformation:
        {
            static const WCHAR StreamName[] = L"::$DATA";
            PFILE_STREAM_INFORMATION Stream = Buffer;
            ULONG NameLength =
                sizeof(StreamName) - sizeof(UNICODE_NULL);
            ULONG Required =
                FIELD_OFFSET(FILE_STREAM_INFORMATION, StreamName) +
                NameLength;

            if (File->Information.Attributes &
                NTFS3G_ROS_FILE_DIRECTORY)
                return STATUS_SUCCESS;
            if (Length < Required)
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Stream, Required);
            Stream->StreamNameLength = NameLength;
            Stream->StreamSize.QuadPart =
                File->Information.FileSize;
            Stream->StreamAllocationSize.QuadPart =
                File->Information.AllocationSize;
            RtlCopyMemory(
                Stream->StreamName, StreamName, NameLength);
            *BytesWritten = Required;
            return STATUS_SUCCESS;
        }

        case FileCompressionInformation:
        {
            PFILE_COMPRESSION_INFORMATION Compression = Buffer;

            if (Length < sizeof(*Compression))
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(Compression, sizeof(*Compression));
            Compression->CompressedFileSize.QuadPart =
                File->Information.AllocationSize;
            if (File->Information.Attributes &
                FILE_ATTRIBUTE_COMPRESSED)
                Compression->CompressionFormat =
                    COMPRESSION_FORMAT_DEFAULT;
            *BytesWritten = sizeof(*Compression);
            return STATUS_SUCCESS;
        }

        case FileAllInformation:
        {
            PFILE_ALL_INFORMATION All = Buffer;
            ULONG FixedLength =
                FIELD_OFFSET(FILE_ALL_INFORMATION,
                             NameInformation.FileName);
            ULONG CopyLength;

            if (Length < FixedLength)
                return STATUS_BUFFER_TOO_SMALL;
            RtlZeroMemory(All, FixedLength);
            NtfsFillBasicInformation(
                &File->Information,
                &All->BasicInformation);
            All->StandardInformation.AllocationSize.QuadPart =
                File->Information.AllocationSize;
            All->StandardInformation.EndOfFile.QuadPart =
                File->Information.FileSize;
            All->StandardInformation.NumberOfLinks =
                File->Information.LinkCount;
            All->StandardInformation.DeletePending =
                File->DeletePending;
            All->StandardInformation.Directory =
                BooleanFlagOn(
                    File->Information.Attributes,
                    NTFS3G_ROS_FILE_DIRECTORY);
            All->InternalInformation.IndexNumber.QuadPart =
                File->Information.FileId;
            All->EaInformation.EaSize =
                NtfsQueryEaSize(FileObject);
            All->AccessInformation.AccessFlags =
                Handle->DesiredAccess;
            All->PositionInformation.CurrentByteOffset =
                FileObject->CurrentByteOffset;
            All->ModeInformation.Mode =
                Handle->CreateOptions;
            All->AlignmentInformation.AlignmentRequirement =
                File->Volume->StorageDevice->
                    AlignmentRequirement;
            All->NameInformation.FileNameLength =
                File->FileName.Length;
            CopyLength = min(
                (ULONG)File->FileName.Length,
                Length - FixedLength);
            RtlCopyMemory(
                All->NameInformation.FileName,
                File->FileName.Buffer,
                CopyLength);
            *BytesWritten = FixedLength + CopyLength;
            return CopyLength == File->FileName.Length ?
                STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
        }

        default:
            return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS
NTAPI
NtfsFsdQueryInformation(_In_ PDEVICE_OBJECT DeviceObject,
                        _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer = GetBuffer(Irp);
    ULONG BytesWritten;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    /*
     * MM keeps file objects referenced after IRP_MJ_CLEANUP while an image or
     * data section exists.  The object manager can still query the file name
     * during process creation, so information queries remain valid until
     * IRP_MJ_CLOSE tears down FsContext.
     */
    if (!IrpSp->FileObject || !IrpSp->FileObject->FsContext)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (!Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(
        &((PFileContextBlock)
          IrpSp->FileObject->FsContext)->MainResource,
        TRUE);
    if (((PFileContextBlock)
         IrpSp->FileObject->FsContext)->Volume->
            ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
        BytesWritten = 0;
    } else {
        Status = NtfsQueryFileInformation(
            IrpSp->FileObject,
            IrpSp->Parameters.QueryFile.FileInformationClass,
            Buffer,
            IrpSp->Parameters.QueryFile.Length,
            &BytesWritten);
    }
    ExReleaseResourceLite(
        &((PFileContextBlock)
          IrpSp->FileObject->FsContext)->MainResource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(Irp, Status, BytesWritten);
}

static NTSTATUS
NtfsSetBasicInformation(
    _Inout_ PFILE_OBJECT FileObject,
    _Inout_ PFileContextBlock File,
    _Inout_ PHandleContextBlock Handle,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PFILE_BASIC_INFORMATION Basic = Buffer;
    NTFS3G_ROS_BASIC_INFORMATION Information;
    NTFS3G_ROS_FILE *CoreFile;
    NTSTATUS Status;
    int Result;

    UNREFERENCED_PARAMETER(FileObject);
    if (Length < sizeof(*Basic))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!(Handle->DesiredAccess & FILE_WRITE_ATTRIBUTES))
        return STATUS_ACCESS_DENIED;

    RtlZeroMemory(&Information, sizeof(Information));
    if (Basic->CreationTime.QuadPart &&
        Basic->CreationTime.QuadPart != -1) {
        Information.CreationTime = Basic->CreationTime.QuadPart;
        Information.ValidFields |=
            NTFS3G_ROS_BASIC_CREATION_TIME;
    }
    if (Basic->LastAccessTime.QuadPart &&
        Basic->LastAccessTime.QuadPart != -1) {
        Information.LastAccessTime = Basic->LastAccessTime.QuadPart;
        Information.ValidFields |=
            NTFS3G_ROS_BASIC_LAST_ACCESS_TIME;
    }
    if (Basic->LastWriteTime.QuadPart &&
        Basic->LastWriteTime.QuadPart != -1) {
        Information.LastWriteTime = Basic->LastWriteTime.QuadPart;
        Information.ValidFields |=
            NTFS3G_ROS_BASIC_LAST_WRITE_TIME;
    }
    if (Basic->ChangeTime.QuadPart &&
        Basic->ChangeTime.QuadPart != -1) {
        Information.ChangeTime = Basic->ChangeTime.QuadPart;
        Information.ValidFields |=
            NTFS3G_ROS_BASIC_CHANGE_TIME;
    }
    if (Basic->FileAttributes) {
        if (Basic->FileAttributes &
            ~FILE_ATTRIBUTE_VALID_SET_FLAGS)
            return STATUS_INVALID_PARAMETER;
        Information.Attributes =
            Basic->FileAttributes & ~FILE_ATTRIBUTE_NORMAL;
        Information.ValidFields |=
            NTFS3G_ROS_BASIC_ATTRIBUTES;
    }
    if (!Information.ValidFields)
        return STATUS_SUCCESS;

    CoreFile = File->File ?
        File->File : Handle->DirectoryFile;
    if (!CoreFile)
        return STATUS_INVALID_PARAMETER;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
    if (File->Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
        goto Complete;
    }
    Result = Ntfs3gRosSetBasicInformation(
        CoreFile, &Information);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
        goto Complete;
    }
    Result = Ntfs3gRosGetFileInformation(
        CoreFile, &File->Information);
    Status = Result < 0 ?
        Ntfs3gRosStatusFromError(-Result) : STATUS_SUCCESS;

Complete:
    ExReleaseResourceLite(&File->MainResource);
    KeLeaveCriticalRegion();
    return Status;
}

static NTSTATUS
NtfsSetDispositionInformation(
    _Inout_ PFILE_OBJECT FileObject,
    _Inout_ PFileContextBlock File,
    _Inout_ PHandleContextBlock Handle,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PFILE_DISPOSITION_INFORMATION Disposition = Buffer;
    NTFS3G_ROS_FILE *CoreFile;
    NTSTATUS Status = STATUS_SUCCESS;
    int Result;

    if (Length < sizeof(*Disposition))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!(Handle->DesiredAccess & DELETE))
        return STATUS_ACCESS_DENIED;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
    if (File->Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
    } else if (Disposition->DeleteFile) {
        if (!File->FileName.Length ||
            (File->FileName.Length == sizeof(WCHAR) &&
             File->FileName.Buffer[0] == L'\\')) {
            Status = STATUS_CANNOT_DELETE;
        } else if (File->Information.Attributes & FILE_ATTRIBUTE_READONLY) {
            Status = STATUS_CANNOT_DELETE;
        } else if (!MmFlushImageSection(&File->SectionObjectPointers,
                                        MmFlushForDelete)) {
            Status = STATUS_CANNOT_DELETE;
        } else {
            CoreFile = File->File ?
                File->File : Handle->DirectoryFile;
            if (!CoreFile) {
                Status = STATUS_INVALID_PARAMETER;
            } else {
                Result = Ntfs3gRosCanDeleteFile(CoreFile);
                if (Result < 0)
                    Status = Ntfs3gRosStatusFromError(-Result);
            }
        }
    }
    if (NT_SUCCESS(Status)) {
        File->DeletePending = Disposition->DeleteFile;
        FileObject->DeletePending = Disposition->DeleteFile;
    }
    ExReleaseResourceLite(&File->MainResource);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
NtfsResizeFile(_Inout_ PFILE_OBJECT FileObject,
               _Inout_ PFileContextBlock File,
               _In_ PLARGE_INTEGER NewSize,
               _In_ BOOLEAN AllocationOnly)
{
    IO_STATUS_BLOCK CacheStatus;
    LARGE_INTEGER OldSize;
    BOOLEAN Shrinking;
    NTSTATUS Status = STATUS_SUCCESS;
    int Result;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
    if (File->Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
        goto Complete;
    }
    OldSize = File->CommonFCBHeader.FileSize;
    if (AllocationOnly &&
        NewSize->QuadPart >= OldSize.QuadPart)
        goto Complete;

    Shrinking = NewSize->QuadPart < OldSize.QuadPart;
    if (Shrinking &&
        !MmCanFileBeTruncated(&File->SectionObjectPointers, NewSize)) {
        Status = STATUS_USER_MAPPED_FILE;
        goto Complete;
    }

    if (Shrinking &&
        (File->SectionObjectPointers.SharedCacheMap ||
         File->SectionObjectPointers.DataSectionObject)) {
        CcFlushCache(&File->SectionObjectPointers, NULL, 0, &CacheStatus);
        if (!NT_SUCCESS(CacheStatus.Status)) {
            Status = CacheStatus.Status;
            goto Complete;
        }
    }

    if (Shrinking &&
        (File->SectionObjectPointers.SharedCacheMap ||
         File->SectionObjectPointers.DataSectionObject) &&
        !CcPurgeCacheSection(&File->SectionObjectPointers,
                             NULL,
                             0,
                             FALSE)) {
        Status = STATUS_USER_MAPPED_FILE;
        goto Complete;
    }

    ExAcquireResourceExclusiveLite(&File->PagingIoResource, TRUE);
    Result = Ntfs3gRosSetFileSize(File->File, NewSize->QuadPart);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
    } else {
        Result = Ntfs3gRosGetFileInformation(File->File,
                                             &File->Information);
        Status = Result < 0 ?
            Ntfs3gRosStatusFromError(-Result) : STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status)) {
        File->CommonFCBHeader.AllocationSize.QuadPart =
            File->Information.AllocationSize;
        File->CommonFCBHeader.FileSize.QuadPart =
            File->Information.FileSize;
        File->CommonFCBHeader.ValidDataLength.QuadPart =
            File->Information.FileSize;
        if (File->SectionObjectPointers.SharedCacheMap) {
            CcSetFileSizes(
                FileObject,
                (PCC_FILE_SIZES)&File->CommonFCBHeader.AllocationSize);
        }
        FileObject->Flags |= FO_FILE_MODIFIED | FO_FILE_SIZE_CHANGED;
    }
    ExReleaseResourceLite(&File->PagingIoResource);

Complete:
    ExReleaseResourceLite(&File->MainResource);
    KeLeaveCriticalRegion();
    return Status;
}

static NTSTATUS
NtfsGetRenameTargetName(
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PFILE_RENAME_INFORMATION Rename,
    _Out_ PUNICODE_STRING TargetName)
{
    UNICODE_STRING SuppliedName;
    USHORT Index;

    if (IrpSp->Parameters.SetFile.FileObject)
        SuppliedName = IrpSp->Parameters.SetFile.FileObject->FileName;
    else {
        if (Rename->FileNameLength > MAXUSHORT)
            return STATUS_NAME_TOO_LONG;
        SuppliedName.Buffer = Rename->FileName;
        SuppliedName.Length = (USHORT)Rename->FileNameLength;
        SuppliedName.MaximumLength = SuppliedName.Length;
    }
    if (!SuppliedName.Buffer || !SuppliedName.Length ||
        (SuppliedName.Length & (sizeof(WCHAR) - 1)))
        return STATUS_OBJECT_NAME_INVALID;

    Index = SuppliedName.Length / sizeof(WCHAR);
    if (SuppliedName.Buffer[Index - 1] == L'\\' ||
        SuppliedName.Buffer[Index - 1] == L'/')
        return STATUS_OBJECT_NAME_INVALID;
    while (Index &&
           SuppliedName.Buffer[Index - 1] != L'\\' &&
           SuppliedName.Buffer[Index - 1] != L'/')
        Index--;

    TargetName->Buffer = SuppliedName.Buffer + Index;
    TargetName->Length =
        SuppliedName.Length - Index * sizeof(WCHAR);
    TargetName->MaximumLength = TargetName->Length;
    if (!TargetName->Length ||
        TargetName->Length >
            NTFS3G_ROS_MAX_NAME_LENGTH * sizeof(WCHAR) ||
        FsRtlDoesNameContainWildCards(TargetName))
        return STATUS_OBJECT_NAME_INVALID;
    if ((TargetName->Length == sizeof(WCHAR) &&
         TargetName->Buffer[0] == L'.') ||
        (TargetName->Length == 2 * sizeof(WCHAR) &&
         TargetName->Buffer[0] == L'.' &&
         TargetName->Buffer[1] == L'.'))
        return STATUS_OBJECT_NAME_INVALID;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsBuildRenameTargetPath(
    _In_ PFileContextBlock Source,
    _In_opt_ PFileContextBlock TargetDirectory,
    _In_ PCUNICODE_STRING TargetName,
    _Out_ PUNICODE_STRING TargetPath)
{
    UNICODE_STRING Parent;
    USHORT ParentCharacters;
    USHORT TotalLength;
    BOOLEAN AddSeparator;

    if (TargetDirectory) {
        Parent = TargetDirectory->FileName;
    } else {
        Parent = Source->FileName;
        ParentCharacters = Parent.Length / sizeof(WCHAR);
        while (ParentCharacters &&
               Parent.Buffer[ParentCharacters - 1] != L'\\' &&
               Parent.Buffer[ParentCharacters - 1] != L'/')
            ParentCharacters--;
        if (!ParentCharacters)
            return STATUS_OBJECT_PATH_INVALID;
        while (ParentCharacters > 1 &&
               (Parent.Buffer[ParentCharacters - 1] == L'\\' ||
                Parent.Buffer[ParentCharacters - 1] == L'/'))
            ParentCharacters--;
        Parent.Length = ParentCharacters * sizeof(WCHAR);
        Parent.MaximumLength = Parent.Length;
    }

    AddSeparator =
        Parent.Length &&
        Parent.Buffer[Parent.Length / sizeof(WCHAR) - 1] != L'\\';
    if (Parent.Length >
        MAXUSHORT - TargetName->Length -
        (AddSeparator ? sizeof(WCHAR) : 0) - sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;
    TotalLength = Parent.Length + TargetName->Length +
                  (AddSeparator ? sizeof(WCHAR) : 0);
    TargetPath->Buffer = ExAllocatePoolWithTag(
        PagedPool, TotalLength + sizeof(WCHAR), TAG_NTFS);
    if (!TargetPath->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    TargetPath->Length = 0;
    TargetPath->MaximumLength = TotalLength + sizeof(WCHAR);
    if (Parent.Length) {
        RtlCopyMemory(TargetPath->Buffer,
                      Parent.Buffer,
                      Parent.Length);
        TargetPath->Length = Parent.Length;
    }
    if (AddSeparator) {
        TargetPath->Buffer[
            TargetPath->Length / sizeof(WCHAR)] = L'\\';
        TargetPath->Length += sizeof(WCHAR);
    }
    RtlCopyMemory(
        (PUCHAR)TargetPath->Buffer + TargetPath->Length,
        TargetName->Buffer,
        TargetName->Length);
    TargetPath->Length += TargetName->Length;
    TargetPath->Buffer[
        TargetPath->Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static BOOLEAN
NtfsIsDescendantPath(_In_ PCUNICODE_STRING Parent,
                     _In_ PCUNICODE_STRING Candidate)
{
    UNICODE_STRING Prefix;

    if (Candidate->Length <= Parent->Length ||
        Candidate->Buffer[Parent->Length / sizeof(WCHAR)] != L'\\')
        return FALSE;
    Prefix = *Candidate;
    Prefix.Length = Parent->Length;
    Prefix.MaximumLength = Prefix.Length;
    return RtlEqualUnicodeString(Parent, &Prefix, TRUE);
}

static NTSTATUS
NtfsPurgeRenameFcb(_Inout_ PFileContextBlock File)
{
    IO_STATUS_BLOCK CacheStatus;

    if (!MmFlushImageSection(&File->SectionObjectPointers,
                             MmFlushForDelete))
        return STATUS_SHARING_VIOLATION;

    CacheStatus.Status = STATUS_SUCCESS;
    CacheStatus.Information = 0;
    CcFlushCache(&File->SectionObjectPointers,
                 NULL,
                 0,
                 &CacheStatus);
    if (!NT_SUCCESS(CacheStatus.Status))
        return CacheStatus.Status;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&File->PagingIoResource, TRUE);
    ExReleaseResourceLite(&File->PagingIoResource);
    KeLeaveCriticalRegion();
    if (!CcPurgeCacheSection(&File->SectionObjectPointers,
                             NULL,
                             0,
                             FALSE))
        return STATUS_SHARING_VIOLATION;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsSetRenameInformation(
    _Inout_ PFILE_OBJECT FileObject,
    _Inout_ PFileContextBlock File,
    _Inout_ PHandleContextBlock Handle,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PFILE_RENAME_INFORMATION Rename = Buffer;
    PFILE_OBJECT TargetFileObject =
        IrpSp->Parameters.SetFile.FileObject;
    PFileContextBlock TargetDirectory = NULL;
    PHandleContextBlock TargetHandle = NULL;
    PVolumeContextBlock Volume = File->Volume;
    PFileContextBlock Candidate;
    PFileContextBlock RetireFile = NULL;
    NTFS3G_ROS_FILE *CoreFile;
    NTFS3G_ROS_FILE *CoreTargetDirectory = NULL;
    UNICODE_STRING TargetName;
    UNICODE_STRING TargetPath;
    PLIST_ENTRY Entry;
    BOOLEAN ReplaceIfExists =
        IrpSp->Parameters.SetFile.ReplaceIfExists;
    NTSTATUS Status;
    int Result;

    RtlZeroMemory(&TargetName, sizeof(TargetName));
    RtlZeroMemory(&TargetPath, sizeof(TargetPath));
    if (Length < FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) ||
        Rename->FileNameLength >
            Length - FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!(Handle->DesiredAccess & DELETE))
        return STATUS_ACCESS_DENIED;
    if (File->DeletePending)
        return STATUS_DELETE_PENDING;
    if (!File->FileName.Length ||
        (File->FileName.Length == sizeof(WCHAR) &&
         File->FileName.Buffer[0] == L'\\'))
        return STATUS_INVALID_PARAMETER;

    Status = NtfsGetRenameTargetName(IrpSp, Rename, &TargetName);
    if (!NT_SUCCESS(Status))
        return Status;

    if (TargetFileObject) {
        TargetDirectory = TargetFileObject->FsContext;
        TargetHandle = TargetFileObject->FsContext2;
        if (!TargetDirectory || !TargetHandle ||
            TargetHandle->CleanupComplete ||
            TargetDirectory->Volume != Volume)
            return STATUS_INVALID_PARAMETER;
        if (!(TargetDirectory->Information.Attributes &
              NTFS3G_ROS_FILE_DIRECTORY) ||
            !TargetHandle->DirectoryFile)
            return STATUS_NOT_A_DIRECTORY;
        CoreTargetDirectory = TargetHandle->DirectoryFile;
    }

    Status = NtfsBuildRenameTargetPath(
        File, TargetDirectory, &TargetName, &TargetPath);
    if (!NT_SUCCESS(Status))
        return Status;
    if (RtlEqualUnicodeString(&File->FileName,
                              &TargetPath,
                              FALSE)) {
        ExFreePoolWithTag(TargetPath.Buffer, TAG_NTFS);
        return STATUS_SUCCESS;
    }
    if ((File->Information.Attributes &
         NTFS3G_ROS_FILE_DIRECTORY) &&
        NtfsIsDescendantPath(&File->FileName, &TargetPath)) {
        ExFreePoolWithTag(TargetPath.Buffer, TAG_NTFS);
        return STATUS_ACCESS_DENIED;
    }

    CoreFile = File->File ? File->File : Handle->DirectoryFile;
    if (!CoreFile) {
        ExFreePoolWithTag(TargetPath.Buffer, TAG_NTFS);
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Volume->FcbListResource, TRUE);
    ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
    if (Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
        goto Complete;
    }
    if (File->DeletePending) {
        Status = STATUS_DELETE_PENDING;
        goto Complete;
    }
    if ((File->Information.Attributes &
         NTFS3G_ROS_FILE_DIRECTORY) &&
        File->OpenHandleCount > 1) {
        Status = STATUS_SHARING_VIOLATION;
        goto Complete;
    }

ScanFcbs:
    for (Entry = Volume->FcbListHead.Flink;
         Entry != &Volume->FcbListHead;
         Entry = Entry->Flink) {
        Candidate = CONTAINING_RECORD(
            Entry, FileContextBlock, ListEntry);
        if (Candidate == File)
            continue;
        if (RtlEqualUnicodeString(
                &Candidate->FileName, &TargetPath, TRUE)) {
            if (!ReplaceIfExists) {
                Status = STATUS_OBJECT_NAME_COLLISION;
                goto Complete;
            }
            if (Candidate->OpenHandleCount ||
                Candidate->DeletePending) {
                Status = STATUS_SHARING_VIOLATION;
                goto Complete;
            }
            NtfsReferenceFcb(Candidate);
            RetireFile = Candidate;
            break;
        }
        if ((File->Information.Attributes &
             NTFS3G_ROS_FILE_DIRECTORY) &&
            NtfsIsDescendantPath(&File->FileName,
                                 &Candidate->FileName)) {
            if (Candidate->OpenHandleCount ||
                Candidate->DeletePending) {
                Status = STATUS_ACCESS_DENIED;
                goto Complete;
            }
            NtfsReferenceFcb(Candidate);
            RetireFile = Candidate;
            break;
        }
    }

    if (RetireFile) {
        /*
         * Closed cached destinations and descendants can outlive their user
         * handles.  Flush and purge one without the FCB-list or source-file
         * resource held, retire its stale NTFS-3G wrapper, and rescan.
         */
        ExReleaseResourceLite(&File->MainResource);
        ExReleaseResourceLite(&Volume->FcbListResource);
        KeLeaveCriticalRegion();

        Status = NtfsPurgeRenameFcb(RetireFile);

        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(
            &Volume->FcbListResource, TRUE);
        ExAcquireResourceExclusiveLite(
            &File->MainResource, TRUE);
        if (!NT_SUCCESS(Status))
            goto Complete;
        if (File->DeletePending) {
            Status = STATUS_DELETE_PENDING;
            goto Complete;
        }
        if (RetireFile->OpenHandleCount ||
            RetireFile->DeletePending ||
            RetireFile->SectionObjectPointers.SharedCacheMap ||
            RetireFile->SectionObjectPointers.DataSectionObject ||
            RetireFile->SectionObjectPointers.ImageSectionObject ||
            RetireFile->ReferenceCount != 1) {
            Status = STATUS_SHARING_VIOLATION;
            goto Complete;
        }
        if (!RtlEqualUnicodeString(
                &RetireFile->FileName,
                &TargetPath,
                TRUE) &&
            !((File->Information.Attributes &
               NTFS3G_ROS_FILE_DIRECTORY) &&
              NtfsIsDescendantPath(
                  &File->FileName,
                  &RetireFile->FileName))) {
            NtfsDereferenceFcb(RetireFile);
            RetireFile = NULL;
            goto ScanFcbs;
        }

        Candidate = RetireFile;
        RetireFile = NULL;
        NtfsDereferenceFcb(Candidate);
        goto ScanFcbs;
    }

    Result = Ntfs3gRosRenameFileUtf16(
        CoreFile,
        CoreTargetDirectory,
        (const uint16_t *)TargetName.Buffer,
        TargetName.Length / sizeof(WCHAR),
        ReplaceIfExists);
    if (Result < 0) {
        DbgPrintEx(DPFLTR_NTFS_ID,
                   DPFLTR_ERROR_LEVEL,
                   "NTFS3G: rename %wZ -> %wZ failed: %d\n",
                   &File->FileName,
                   &TargetPath,
                   -Result);
        Status = Ntfs3gRosStatusFromError(-Result);
        goto Complete;
    }

    if (File->FileName.Buffer)
        ExFreePoolWithTag(File->FileName.Buffer, TAG_NTFS);
    File->FileName = TargetPath;
    RtlZeroMemory(&TargetPath, sizeof(TargetPath));
    Result = Ntfs3gRosGetFileInformation(
        CoreFile, &File->Information);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
        goto Complete;
    }
    FileObject->Flags |= FO_FILE_MODIFIED;
    Status = STATUS_SUCCESS;

Complete:
    if (RetireFile)
        NtfsDereferenceFcb(RetireFile);
    ExReleaseResourceLite(&File->MainResource);
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    if (TargetPath.Buffer)
        ExFreePoolWithTag(TargetPath.Buffer, TAG_NTFS);
    return Status;
}

NTSTATUS
NTAPI
NtfsFsdSetInformation(_In_ PDEVICE_OBJECT DeviceObject,
                      _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File = FileObject->FsContext;
    PHandleContextBlock Handle = FileObject->FsContext2;
    FILE_INFORMATION_CLASS InformationClass =
        IrpSp->Parameters.SetFile.FileInformationClass;
    ULONG Length = IrpSp->Parameters.SetFile.Length;
    PVOID Buffer = GetBuffer(Irp);
    LARGE_INTEGER NewSize;
    BOOLEAN AllocationOnly;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!File || !Handle)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (Handle->CleanupComplete)
        return NtfsCompleteRequest(Irp, STATUS_FILE_CLOSED, 0);
    if (File->Volume->ShutdownStarted)
        return NtfsCompleteRequest(Irp, STATUS_SYSTEM_SHUTDOWN, 0);
    if (!Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);
    if (File->IsVolume &&
        InformationClass != FilePositionInformation)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    switch (InformationClass) {
        case FileBasicInformation:
            Status = NtfsSetBasicInformation(FileObject,
                                             File,
                                             Handle,
                                             Buffer,
                                             Length);
            return NtfsCompleteRequest(Irp, Status, 0);

        case FilePositionInformation:
            if (Length < sizeof(FILE_POSITION_INFORMATION))
                return NtfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            NewSize =
                ((PFILE_POSITION_INFORMATION)Buffer)->CurrentByteOffset;
            if (NewSize.QuadPart < 0)
                return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
            FileObject->CurrentByteOffset = NewSize;
            return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

        case FileAllocationInformation:
            if (!(Handle->DesiredAccess & FILE_WRITE_DATA))
                return NtfsCompleteRequest(Irp, STATUS_ACCESS_DENIED, 0);
            if (Length < sizeof(FILE_ALLOCATION_INFORMATION))
                return NtfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            NewSize =
                ((PFILE_ALLOCATION_INFORMATION)Buffer)->AllocationSize;
            if (NewSize.QuadPart < 0)
                return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
            AllocationOnly = TRUE;
            break;

        case FileEndOfFileInformation:
            if (!(Handle->DesiredAccess & FILE_WRITE_DATA))
                return NtfsCompleteRequest(Irp, STATUS_ACCESS_DENIED, 0);
            if (Length < sizeof(FILE_END_OF_FILE_INFORMATION))
                return NtfsCompleteRequest(Irp, STATUS_INFO_LENGTH_MISMATCH, 0);
            NewSize =
                ((PFILE_END_OF_FILE_INFORMATION)Buffer)->EndOfFile;
            if (NewSize.QuadPart < 0)
                return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
            AllocationOnly = FALSE;
            break;

        case FileDispositionInformation:
            Status = NtfsSetDispositionInformation(FileObject,
                                                   File,
                                                   Handle,
                                                   Buffer,
                                                   Length);
            return NtfsCompleteRequest(Irp, Status, 0);

        case FileRenameInformation:
            Status = NtfsSetRenameInformation(FileObject,
                                              File,
                                              Handle,
                                              IrpSp,
                                              Buffer,
                                              Length);
            return NtfsCompleteRequest(Irp, Status, 0);

        default:
            return NtfsCompleteRequest(Irp, STATUS_INVALID_INFO_CLASS, 0);
    }

    if (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)
        return NtfsCompleteRequest(Irp, STATUS_FILE_IS_A_DIRECTORY, 0);
    Status = NtfsResizeFile(FileObject,
                            File,
                            &NewSize,
                            AllocationOnly);
    return NtfsCompleteRequest(Irp, Status, 0);
}

static NTSTATUS
NtfsSetDirectoryPattern(_Inout_ PHandleContextBlock Handle,
                        _In_ PCUNICODE_STRING Pattern)
{
    PWCHAR Buffer;
    NTSTATUS Status;

    if (Handle->DirectoryPattern.Buffer) {
        ExFreePoolWithTag(Handle->DirectoryPattern.Buffer, TAG_NTFS);
        RtlZeroMemory(&Handle->DirectoryPattern,
                      sizeof(Handle->DirectoryPattern));
    }
    if (!Pattern || !Pattern->Length)
        return STATUS_SUCCESS;
    if (Pattern->Length > MAXUSHORT - sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    Buffer = ExAllocatePoolWithTag(PagedPool,
                                   Pattern->Length + sizeof(WCHAR),
                                   TAG_NTFS);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    Handle->DirectoryPattern.Buffer = Buffer;
    Handle->DirectoryPattern.Length = Pattern->Length;
    Handle->DirectoryPattern.MaximumLength =
        Pattern->Length + sizeof(WCHAR);
    Status = RtlUpcaseUnicodeString(&Handle->DirectoryPattern,
                                    Pattern,
                                    FALSE);
    if (!NT_SUCCESS(Status)) {
        ExFreePoolWithTag(Buffer, TAG_NTFS);
        RtlZeroMemory(&Handle->DirectoryPattern,
                      sizeof(Handle->DirectoryPattern));
        return Status;
    }
    Buffer[Pattern->Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static BOOLEAN
NtfsDirectoryNameMatches(_In_ PHandleContextBlock Handle,
                         _In_ const NTFS3G_ROS_DIRECTORY_ENTRY *Entry)
{
    UNICODE_STRING Name;

    if (!Handle->DirectoryPattern.Length)
        return TRUE;
    Name.Buffer = (PWCHAR)Entry->FileName;
    Name.Length = Entry->FileNameLength * sizeof(WCHAR);
    Name.MaximumLength = Name.Length;
    return FsRtlIsNameInExpression(&Handle->DirectoryPattern,
                                   &Name,
                                   TRUE,
                                   NULL);
}

static ULONG
NtfsDirectoryEntryHeaderSize(
    _In_ FILE_INFORMATION_CLASS InformationClass)
{
    switch (InformationClass) {
        case FileDirectoryInformation:
            return FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName);
        case FileFullDirectoryInformation:
            return FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName);
        case FileBothDirectoryInformation:
            return FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);
        case FileNamesInformation:
            return FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName);
        default:
            return 0;
    }
}

static VOID
NtfsFillDirectoryEntry(_Out_ PVOID Buffer,
                       _In_ FILE_INFORMATION_CLASS InformationClass,
                       _In_ const NTFS3G_ROS_DIRECTORY_ENTRY *Entry)
{
    const NTFS3G_ROS_FILE_INFORMATION *Source = &Entry->Information;
    ULONG NameLength = Entry->FileNameLength * sizeof(WCHAR);
    PWCHAR FileName;

    if (InformationClass == FileNamesInformation) {
        PFILE_NAMES_INFORMATION Names = Buffer;

        Names->FileIndex = (ULONG)Source->FileId;
        Names->FileNameLength = NameLength;
        FileName = Names->FileName;
    } else {
        PFILE_DIRECTORY_INFORMATION Directory = Buffer;

        Directory->FileIndex = (ULONG)Source->FileId;
        Directory->CreationTime.QuadPart = Source->CreationTime;
        Directory->LastAccessTime.QuadPart = Source->LastAccessTime;
        Directory->LastWriteTime.QuadPart = Source->LastWriteTime;
        Directory->ChangeTime.QuadPart = Source->ChangeTime;
        Directory->EndOfFile.QuadPart = Source->FileSize;
        Directory->AllocationSize.QuadPart = Source->AllocationSize;
        Directory->FileAttributes = NtfsNormalizedAttributes(Source);
        Directory->FileNameLength = NameLength;

        if (InformationClass == FileDirectoryInformation) {
            FileName = Directory->FileName;
        } else if (InformationClass == FileFullDirectoryInformation) {
            PFILE_FULL_DIR_INFORMATION Full = Buffer;

            Full->EaSize = 0;
            FileName = Full->FileName;
        } else {
            PFILE_BOTH_DIR_INFORMATION Both = Buffer;

            Both->EaSize = 0;
            Both->ShortNameLength = 0;
            RtlZeroMemory(Both->ShortName, sizeof(Both->ShortName));
            FileName = Both->FileName;
        }
    }
    RtlCopyMemory(FileName, Entry->FileName, NameLength);
}

NTSTATUS
NTAPI
NtfsFsdDirectoryControl(_In_ PDEVICE_OBJECT DeviceObject,
                        _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File = FileObject ? FileObject->FsContext : NULL;
    PHandleContextBlock Handle =
        FileObject ? FileObject->FsContext2 : NULL;
    FILE_INFORMATION_CLASS InformationClass;
    NTFS3G_ROS_DIRECTORY_ENTRY Entry;
    PUCHAR Buffer = GetBuffer(Irp);
    ULONG BufferLength = IrpSp->Parameters.QueryDirectory.Length;
    ULONG EntryHeaderSize;
    ULONG BytesWritten = 0;
    PULONG PreviousNextEntryOffset = NULL;
    BOOLEAN Restart;
    BOOLEAN ReturnSingle;
    NTSTATUS Status;
    int Result = 0;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (IrpSp->MinorFunction != IRP_MN_QUERY_DIRECTORY)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (!File || !Handle ||
        !(File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY))
        return NtfsCompleteRequest(Irp, STATUS_NOT_A_DIRECTORY, 0);
    if (Handle->CleanupComplete || !Handle->DirectoryFile)
        return NtfsCompleteRequest(Irp, STATUS_FILE_CLOSED, 0);
    if (!(Handle->DesiredAccess & FILE_LIST_DIRECTORY))
        return NtfsCompleteRequest(Irp, STATUS_ACCESS_DENIED, 0);
    if (!Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);

    InformationClass = IrpSp->Parameters.QueryDirectory.FileInformationClass;
    EntryHeaderSize = NtfsDirectoryEntryHeaderSize(InformationClass);
    if (!EntryHeaderSize)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_INFO_CLASS, 0);

    Restart = (IrpSp->Flags & SL_RESTART_SCAN) != 0;
    ReturnSingle = (IrpSp->Flags & SL_RETURN_SINGLE_ENTRY) != 0;
    if ((!Handle->DirectoryQueryStarted || Restart) &&
        IrpSp->Parameters.QueryDirectory.FileName) {
        Status = NtfsSetDirectoryPattern(
            Handle, IrpSp->Parameters.QueryDirectory.FileName);
        if (!NT_SUCCESS(Status))
            return NtfsCompleteRequest(Irp, Status, 0);
    }
    if (Restart) {
        Result = Ntfs3gRosRestartDirectory(Handle->DirectoryFile);
        if (Result < 0)
            return NtfsCompleteRequest(Irp,
                                       Ntfs3gRosStatusFromError(-Result),
                                       0);
    }
    Handle->DirectoryQueryStarted = TRUE;

    while (!ReturnSingle || !BytesWritten) {
        uint64_t Position =
            Ntfs3gRosGetDirectoryPosition(Handle->DirectoryFile);
        ULONG EntrySize;
        PULONG NextEntryOffset;

        Result = Ntfs3gRosReadDirectory(Handle->DirectoryFile, &Entry);
        if (Result <= 0)
            break;
        if (!NtfsDirectoryNameMatches(Handle, &Entry))
            continue;

        EntrySize = ALIGN_UP_BY(
            EntryHeaderSize + Entry.FileNameLength * sizeof(WCHAR),
            sizeof(ULONGLONG));
        if (EntrySize > BufferLength - BytesWritten) {
            Ntfs3gRosSetDirectoryPosition(Handle->DirectoryFile, Position);
            if (!BytesWritten)
                return NtfsCompleteRequest(Irp, STATUS_BUFFER_OVERFLOW, 0);
            break;
        }

        RtlZeroMemory(Buffer + BytesWritten, EntrySize);
        NextEntryOffset = (PULONG)(Buffer + BytesWritten);
        NtfsFillDirectoryEntry(Buffer + BytesWritten,
                               InformationClass,
                               &Entry);
        if (PreviousNextEntryOffset)
            *PreviousNextEntryOffset =
                (ULONG)((PUCHAR)NextEntryOffset -
                        (PUCHAR)PreviousNextEntryOffset);
        PreviousNextEntryOffset = NextEntryOffset;
        BytesWritten += EntrySize;
    }

    if (BytesWritten)
        Status = STATUS_SUCCESS;
    else if (Result < 0)
        Status = Ntfs3gRosStatusFromError(-Result);
    else
        Status = STATUS_NO_MORE_FILES;
    return NtfsCompleteRequest(Irp, Status, BytesWritten);
}
