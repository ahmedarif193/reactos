/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file close handling
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

NTSTATUS
NTAPI
NtfsFsdClose(_In_ PDEVICE_OBJECT DeviceObject,
             _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File;

    UNREFERENCED_PARAMETER(DeviceObject);
    File = FileObject ? FileObject->FsContext : NULL;
    if (File) {
        FileObject->FsContext = NULL;
        FileObject->SectionObjectPointer = NULL;
        Ntfs3gRosCloseFile(File->File);
        if (File->DirectoryPattern.Buffer)
            ExFreePoolWithTag(File->DirectoryPattern.Buffer, TAG_NTFS);
        if (File->FileName.Buffer)
            ExFreePoolWithTag(File->FileName.Buffer, TAG_NTFS);
        ExDeleteResourceLite(&File->MainResource);
        ExDeleteResourceLite(&File->PagingIoResource);
        FsRtlUninitializeFileLock(&File->FileLock);
        ExFreePoolWithTag(File, TAG_NTFS);
    }
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}
