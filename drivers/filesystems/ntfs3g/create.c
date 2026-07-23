/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file create and open handling
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

static NTSTATUS
NtfsBuildFileName(_In_ PFILE_OBJECT FileObject,
                  _Out_ PUNICODE_STRING FileName)
{
    PFileContextBlock Related = NULL;
    UNICODE_STRING Suffix = FileObject->FileName;
    ULONG Length;
    BOOLEAN AddSeparator = FALSE;

    RtlZeroMemory(FileName, sizeof(*FileName));
    if (FileObject->RelatedFileObject &&
        (!Suffix.Length || Suffix.Buffer[0] != L'\\')) {
        Related = FileObject->RelatedFileObject->FsContext;
        if (!Related)
            return STATUS_INVALID_PARAMETER;
        if (Related->FileName.Length && Suffix.Length &&
            Related->FileName.Buffer[Related->FileName.Length / sizeof(WCHAR) - 1] != L'\\')
            AddSeparator = TRUE;
    }

    Length = Suffix.Length + (Related ? Related->FileName.Length : 0) +
             (AddSeparator ? sizeof(WCHAR) : 0);
    if (Length > MAXUSHORT - sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    FileName->Buffer = ExAllocatePoolWithTag(PagedPool,
                                              Length + sizeof(WCHAR),
                                              TAG_NTFS);
    if (!FileName->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    FileName->Length = 0;
    FileName->MaximumLength = (USHORT)(Length + sizeof(WCHAR));
    if (Related) {
        RtlCopyMemory(FileName->Buffer,
                      Related->FileName.Buffer,
                      Related->FileName.Length);
        FileName->Length = Related->FileName.Length;
    }
    if (AddSeparator) {
        FileName->Buffer[FileName->Length / sizeof(WCHAR)] = L'\\';
        FileName->Length += sizeof(WCHAR);
    }
    if (Suffix.Length) {
        RtlCopyMemory((PUCHAR)FileName->Buffer + FileName->Length,
                      Suffix.Buffer,
                      Suffix.Length);
        FileName->Length += Suffix.Length;
    }
    FileName->Buffer[FileName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static BOOLEAN
NtfsRequestsWriteAccess(_In_ ACCESS_MASK DesiredAccess,
                        _In_ ULONG CreateOptions)
{
    const ACCESS_MASK WriteAccess = FILE_WRITE_DATA | FILE_APPEND_DATA |
                                    FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                    DELETE | WRITE_DAC | WRITE_OWNER |
                                    GENERIC_WRITE | GENERIC_ALL;

    return (DesiredAccess & WriteAccess) != 0 ||
           (CreateOptions & FILE_DELETE_ON_CLOSE) != 0;
}

NTSTATUS
NTAPI
NtfsFsdCreate(_In_ PDEVICE_OBJECT DeviceObject,
              _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PVolumeContextBlock Volume;
    PFileContextBlock File = NULL;
    NTFS3G_ROS_FILE *CoreFile = NULL;
    NTFS3G_ROS_FILE_INFORMATION Information;
    UNICODE_STRING FileName;
    ACCESS_MASK DesiredAccess;
    ULONG CreateOptions;
    UCHAR Disposition;
    NTSTATUS Status;
    int Result;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);

    Volume = DeviceObject->DeviceExtension;
    CreateOptions = GetCreateOptions(IrpSp->Parameters.Create.Options);
    Disposition = GetDisposition(IrpSp->Parameters.Create.Options);
    DesiredAccess = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;

    if ((CreateOptions & FILE_OPEN_BY_FILE_ID) ||
        NtfsRequestsWriteAccess(DesiredAccess, CreateOptions))
        return NtfsCompleteRequest(Irp, STATUS_MEDIA_WRITE_PROTECTED, 0);

    Status = NtfsBuildFileName(FileObject, &FileName);
    if (!NT_SUCCESS(Status))
        return NtfsCompleteRequest(Irp, Status, 0);

    Status = Ntfs3gRosOpenUnicodeFile(Volume->Volume, &FileName, &CoreFile);
    if (!NT_SUCCESS(Status)) {
        ExFreePoolWithTag(FileName.Buffer, TAG_NTFS);
        if ((Status == STATUS_OBJECT_NAME_NOT_FOUND ||
             Status == STATUS_OBJECT_PATH_NOT_FOUND) &&
            (Disposition == FILE_CREATE || Disposition == FILE_OPEN_IF ||
             Disposition == FILE_OVERWRITE_IF ||
             Disposition == FILE_SUPERSEDE))
            Status = STATUS_MEDIA_WRITE_PROTECTED;
        return NtfsCompleteRequest(Irp, Status, FILE_DOES_NOT_EXIST);
    }

    if (Disposition == FILE_CREATE) {
        Status = STATUS_OBJECT_NAME_COLLISION;
        goto Failure;
    }
    if (Disposition == FILE_SUPERSEDE || Disposition == FILE_OVERWRITE ||
        Disposition == FILE_OVERWRITE_IF) {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Failure;
    }
    Result = Ntfs3gRosGetFileInformation(CoreFile, &Information);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
        goto Failure;
    }
    if ((CreateOptions & FILE_DIRECTORY_FILE) &&
        !(Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)) {
        Status = STATUS_NOT_A_DIRECTORY;
        goto Failure;
    }
    if ((CreateOptions & FILE_NON_DIRECTORY_FILE) &&
        (Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)) {
        Status = STATUS_FILE_IS_A_DIRECTORY;
        goto Failure;
    }

    File = ExAllocatePoolWithTag(NonPagedPool, sizeof(*File), TAG_NTFS);
    if (!File) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    RtlZeroMemory(File, sizeof(*File));
    FsRtlInitializeFileLock(&File->FileLock, NULL, NULL);
    Status = ExInitializeResourceLite(&File->MainResource);
    if (!NT_SUCCESS(Status)) {
        FsRtlUninitializeFileLock(&File->FileLock);
        ExFreePoolWithTag(File, TAG_NTFS);
        File = NULL;
        goto Failure;
    }
    Status = ExInitializeResourceLite(&File->PagingIoResource);
    if (!NT_SUCCESS(Status)) {
        ExDeleteResourceLite(&File->MainResource);
        FsRtlUninitializeFileLock(&File->FileLock);
        ExFreePoolWithTag(File, TAG_NTFS);
        File = NULL;
        goto Failure;
    }
    ExInitializeFastMutex(&File->HeaderMutex);
    FsRtlSetupAdvancedHeader(&File->CommonFCBHeader, &File->HeaderMutex);
    File->CommonFCBHeader.NodeTypeCode = NTFS3G_FCB_NODE_TYPE;
    File->CommonFCBHeader.NodeByteSize = sizeof(*File);
    File->CommonFCBHeader.Resource = &File->MainResource;
    File->CommonFCBHeader.PagingIoResource = &File->PagingIoResource;
    File->CommonFCBHeader.IsFastIoPossible = FastIoIsNotPossible;
    File->CommonFCBHeader.AllocationSize.QuadPart = Information.AllocationSize;
    File->CommonFCBHeader.FileSize.QuadPart = Information.FileSize;
    File->CommonFCBHeader.ValidDataLength.QuadPart = Information.FileSize;
    File->File = CoreFile;
    File->Information = Information;
    File->FileName = FileName;
    File->CreateOptions = CreateOptions;
    File->DesiredAccess = DesiredAccess;

    FileObject->FsContext = File;
    FileObject->SectionObjectPointer = &File->SectionObjectPointers;
    FileObject->Vpb = DeviceObject->Vpb;
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);

Failure:
    if (File)
        ExFreePoolWithTag(File, TAG_NTFS);
    Ntfs3gRosCloseFile(CoreFile);
    ExFreePoolWithTag(FileName.Buffer, TAG_NTFS);
    return NtfsCompleteRequest(Irp, Status,
                               Status == STATUS_OBJECT_NAME_COLLISION ?
                               FILE_EXISTS : 0);
}
