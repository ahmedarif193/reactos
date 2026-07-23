/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file read handling
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

NTSTATUS
NTAPI
NtfsFsdRead(_In_ PDEVICE_OBJECT DeviceObject,
            _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File = FileObject->FsContext;
    LARGE_INTEGER ByteOffset = IrpSp->Parameters.Read.ByteOffset;
    ULONG Length = IrpSp->Parameters.Read.Length;
    PVOID Buffer;
    size_t BytesRead;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!File)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)
        return NtfsCompleteRequest(Irp, STATUS_FILE_IS_A_DIRECTORY, 0);
    if (ByteOffset.HighPart == -1 &&
        ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION)
        ByteOffset = FileObject->CurrentByteOffset;
    if (ByteOffset.QuadPart < 0)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);

    Buffer = GetBuffer(Irp);
    if (Length && !Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    if (!Length)
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    {
        int Result = Ntfs3gRosReadFileAt(File->File,
                                         (uint64_t)ByteOffset.QuadPart,
                                         Buffer,
                                         Length,
                                         &BytesRead);
        if (Result < 0) {
            Status = Ntfs3gRosStatusFromError(-Result);
            return NtfsCompleteRequest(Irp, Status, 0);
        }
    }
    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
        FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + BytesRead;
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, BytesRead);
}
