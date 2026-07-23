/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file and directory information
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

static VOID
NtfsFillBasicInformation(_In_ const NTFS3G_ROS_FILE_INFORMATION *Source,
                         _Out_ PFILE_BASIC_INFORMATION Destination)
{
    Destination->CreationTime.QuadPart = Source->CreationTime;
    Destination->LastAccessTime.QuadPart = Source->LastAccessTime;
    Destination->LastWriteTime.QuadPart = Source->LastWriteTime;
    Destination->ChangeTime.QuadPart = Source->ChangeTime;
    Destination->FileAttributes = Source->Attributes ?
                                  Source->Attributes : FILE_ATTRIBUTE_NORMAL;
}

static NTSTATUS
NtfsQueryFileInformation(_In_ PFILE_OBJECT FileObject,
                         _In_ FILE_INFORMATION_CLASS InformationClass,
                         _Out_writes_bytes_(Length) PVOID Buffer,
                         _In_ ULONG Length,
                         _Out_ PULONG BytesWritten)
{
    PFileContextBlock File = FileObject->FsContext;
    *BytesWritten = 0;
    if (!File)
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
            Network->FileAttributes = File->Information.Attributes ?
                                      File->Information.Attributes :
                                      FILE_ATTRIBUTE_NORMAL;
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
            AttributeTag->FileAttributes = File->Information.Attributes;
            AttributeTag->ReparseTag = 0;
            *BytesWritten = sizeof(*AttributeTag);
            return STATUS_SUCCESS;
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
    if (!Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);
    Status = NtfsQueryFileInformation(IrpSp->FileObject,
                                     IrpSp->Parameters.QueryFile.FileInformationClass,
                                     Buffer,
                                     IrpSp->Parameters.QueryFile.Length,
                                     &BytesWritten);
    return NtfsCompleteRequest(Irp, Status, BytesWritten);
}

NTSTATUS
NTAPI
NtfsFsdSetInformation(_In_ PDEVICE_OBJECT DeviceObject,
                      _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return NtfsCompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);
}

static NTSTATUS
NtfsSetDirectoryPattern(_Inout_ PFileContextBlock File,
                        _In_ PCUNICODE_STRING Pattern)
{
    PWCHAR Buffer;

    if (File->DirectoryPattern.Buffer) {
        ExFreePoolWithTag(File->DirectoryPattern.Buffer, TAG_NTFS);
        RtlZeroMemory(&File->DirectoryPattern, sizeof(File->DirectoryPattern));
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
    RtlCopyMemory(Buffer, Pattern->Buffer, Pattern->Length);
    Buffer[Pattern->Length / sizeof(WCHAR)] = UNICODE_NULL;
    File->DirectoryPattern.Buffer = Buffer;
    File->DirectoryPattern.Length = Pattern->Length;
    File->DirectoryPattern.MaximumLength = Pattern->Length + sizeof(WCHAR);
    return STATUS_SUCCESS;
}

static BOOLEAN
NtfsDirectoryNameMatches(_In_ PFileContextBlock File,
                         _In_ const NTFS3G_ROS_DIRECTORY_ENTRY *Entry)
{
    UNICODE_STRING Name;

    if (!File->DirectoryPattern.Length)
        return TRUE;
    Name.Buffer = (PWCHAR)Entry->FileName;
    Name.Length = Entry->FileNameLength * sizeof(WCHAR);
    Name.MaximumLength = Name.Length;
    return FsRtlIsNameInExpression(&File->DirectoryPattern,
                                   &Name,
                                   TRUE,
                                   NULL);
}

static ULONG
NtfsDirectoryEntrySize(_In_ FILE_INFORMATION_CLASS InformationClass,
                       _In_ ULONG NameLength)
{
    ULONG Size;

    switch (InformationClass) {
        case FileDirectoryInformation:
            Size = FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName);
            break;
        case FileFullDirectoryInformation:
            Size = FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName);
            break;
        case FileBothDirectoryInformation:
            Size = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);
            break;
        case FileNamesInformation:
            Size = FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName);
            break;
        default:
            return 0;
    }
    return ALIGN_UP_BY(Size + NameLength, sizeof(ULONGLONG));
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
        Directory->FileAttributes = Source->Attributes ?
                                    Source->Attributes : FILE_ATTRIBUTE_NORMAL;
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
    PFileContextBlock File = IrpSp->FileObject->FsContext;
    FILE_INFORMATION_CLASS InformationClass;
    NTFS3G_ROS_DIRECTORY_ENTRY Entry;
    PUCHAR Buffer = GetBuffer(Irp);
    ULONG BufferLength = IrpSp->Parameters.QueryDirectory.Length;
    ULONG BytesWritten = 0;
    PULONG PreviousNextEntryOffset = NULL;
    BOOLEAN Restart;
    BOOLEAN ReturnSingle;
    NTSTATUS Status;
    int Result = 0;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (IrpSp->MinorFunction != IRP_MN_QUERY_DIRECTORY)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (!File || !(File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY))
        return NtfsCompleteRequest(Irp, STATUS_NOT_A_DIRECTORY, 0);
    if (!Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);

    InformationClass = IrpSp->Parameters.QueryDirectory.FileInformationClass;
    if (!NtfsDirectoryEntrySize(InformationClass, 0))
        return NtfsCompleteRequest(Irp, STATUS_INVALID_INFO_CLASS, 0);

    Restart = (IrpSp->Flags & SL_RESTART_SCAN) != 0;
    ReturnSingle = (IrpSp->Flags & SL_RETURN_SINGLE_ENTRY) != 0;
    if ((!File->DirectoryQueryStarted || Restart) &&
        IrpSp->Parameters.QueryDirectory.FileName) {
        Status = NtfsSetDirectoryPattern(
            File, IrpSp->Parameters.QueryDirectory.FileName);
        if (!NT_SUCCESS(Status))
            return NtfsCompleteRequest(Irp, Status, 0);
    }
    if (Restart) {
        Result = Ntfs3gRosRestartDirectory(File->File);
        if (Result < 0)
            return NtfsCompleteRequest(Irp,
                                       Ntfs3gRosStatusFromError(-Result),
                                       0);
    }
    File->DirectoryQueryStarted = TRUE;

    while (!ReturnSingle || !BytesWritten) {
        uint64_t Position = Ntfs3gRosGetDirectoryPosition(File->File);
        ULONG EntrySize;
        PULONG NextEntryOffset;

        Result = Ntfs3gRosReadDirectory(File->File, &Entry);
        if (Result <= 0)
            break;
        if (!NtfsDirectoryNameMatches(File, &Entry))
            continue;

        EntrySize = NtfsDirectoryEntrySize(
            InformationClass, Entry.FileNameLength * sizeof(WCHAR));
        if (EntrySize > BufferLength - BytesWritten) {
            Ntfs3gRosSetDirectoryPosition(File->File, Position);
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
