/*
 * PROJECT:     ReactOS exFAT filesystem driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     File and raw-volume reads and writes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "exfat.h"

#define NDEBUG
#include <debug.h>

static NTSTATUS
ExFatReadVolume(
    PEXFAT_VCB Vcb,
    PIRP Irp,
    PVOID Buffer,
    ULONG Length,
    LARGE_INTEGER Offset)
{
    ULONGLONG VolumeLength = Vcb->SectorCount * Vcb->BytesPerSector;

    if (Offset.QuadPart < 0 || (ULONGLONG)Offset.QuadPart >= VolumeLength)
        return STATUS_END_OF_FILE;
    if (Length > VolumeLength - Offset.QuadPart)
        Length = (ULONG)(VolumeLength - Offset.QuadPart);

    if (!Length)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(ExFatReadWriteDevice(Vcb->StorageDevice,
                                         IRP_MJ_READ,
                                         Buffer,
                                         Length,
                                         &Offset,
                                         FALSE)))
    {
        return STATUS_IO_DEVICE_ERROR;
    }

    Irp->IoStatus.Information = Length;
    return STATUS_SUCCESS;
}

NTSTATUS
ExFatRead(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_VCB Vcb;
    PEXFAT_FCB Fcb;
    PEXFAT_CCB Ccb;
    LARGE_INTEGER Offset = Stack->Parameters.Read.ByteOffset;
    ULONG Length = Stack->Parameters.Read.Length;
    UINT BytesRead = 0;
    PVOID Buffer;
    FRESULT Result;
    NTSTATUS Status;
    BOOLEAN PagingIo = !!(Irp->Flags & IRP_PAGING_IO);

    if (DeviceObject == ExFatGlobalData->DeviceObject || !FileObject)
        return STATUS_INVALID_DEVICE_REQUEST;
    Vcb = DeviceObject->DeviceExtension;
    Fcb = FileObject->FsContext;
    Ccb = FileObject->FsContext2;
    if (!Fcb || !Ccb)
        return STATUS_INVALID_HANDLE;
    if (Fcb->IsDirectory)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (!Length)
        return STATUS_SUCCESS;

    if (Offset.LowPart == FILE_USE_FILE_POINTER_POSITION && Offset.HighPart == -1)
        Offset = FileObject->CurrentByteOffset;
    if (Offset.QuadPart < 0)
        return STATUS_INVALID_PARAMETER;

    Status = ExFatLockUserBuffer(Irp, Length, IoWriteAccess);
    if (!NT_SUCCESS(Status))
        return Status;
    Buffer = ExFatGetUserBuffer(Irp, PagingIo);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (Fcb->IsVolume)
    {
        Status = ExFatReadVolume(Vcb, Irp, Buffer, Length, Offset);
    }
    else
    {
        if (!Ccb->HandleOpen || Ccb->IsDirectory)
            return STATUS_FILE_CLOSED;
        if ((ULONGLONG)Offset.QuadPart >= (ULONGLONG)Fcb->Header.FileSize.QuadPart)
            return STATUS_END_OF_FILE;
        if (Length > (ULONGLONG)Fcb->Header.FileSize.QuadPart - Offset.QuadPart)
            Length = (ULONG)((ULONGLONG)Fcb->Header.FileSize.QuadPart - Offset.QuadPart);
        if (!(Irp->Flags & IRP_PAGING_IO) &&
            !FsRtlCheckLockForReadAccess(&Fcb->FileLock, Irp))
        {
            return STATUS_FILE_LOCK_CONFLICT;
        }

        ExAcquireResourceSharedLite(PagingIo ? &Fcb->PagingIoResource : &Fcb->MainResource,
                                    TRUE);
        ExFatAcquireFatFs(Vcb);
        Result = f_lseek(&Ccb->Handle.File, (FSIZE_t)Offset.QuadPart);
        if (Result == FR_OK)
            Result = f_read(&Ccb->Handle.File, Buffer, Length, &BytesRead);
        ExFatReleaseFatFs(Vcb);
        ExReleaseResourceLite(PagingIo ? &Fcb->PagingIoResource : &Fcb->MainResource);

        Status = ExFatMapResult(Result);
        if (NT_SUCCESS(Status))
            Irp->IoStatus.Information = BytesRead;
    }

    if (NT_SUCCESS(Status) && !PagingIo && (FileObject->Flags & FO_SYNCHRONOUS_IO))
        FileObject->CurrentByteOffset.QuadPart = Offset.QuadPart + Irp->IoStatus.Information;
    return Status;
}

static NTSTATUS
ExFatWriteVolume(
    PEXFAT_VCB Vcb,
    PFILE_OBJECT FileObject,
    PIRP Irp,
    PVOID Buffer,
    ULONG Length,
    LARGE_INTEGER Offset)
{
    ULONGLONG VolumeLength = Vcb->SectorCount * Vcb->BytesPerSector;

    if (Vcb->ReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (!Vcb->Locked || Vcb->LockOwner != FileObject)
        return STATUS_ACCESS_DENIED;
    if (Offset.QuadPart < 0 ||
        (ULONGLONG)Offset.QuadPart > VolumeLength ||
        Length > VolumeLength - Offset.QuadPart)
    {
        return STATUS_END_OF_FILE;
    }

    if (!Length)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(ExFatReadWriteDevice(Vcb->StorageDevice,
                                         IRP_MJ_WRITE,
                                         Buffer,
                                         Length,
                                         &Offset,
                                         FALSE)))
    {
        return STATUS_IO_DEVICE_ERROR;
    }

    Irp->IoStatus.Information = Length;
    return STATUS_SUCCESS;
}

NTSTATUS
ExFatWrite(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = Stack->FileObject;
    PEXFAT_VCB Vcb;
    PEXFAT_FCB Fcb;
    PEXFAT_CCB Ccb;
    LARGE_INTEGER Offset = Stack->Parameters.Write.ByteOffset;
    ULONG Length = Stack->Parameters.Write.Length;
    UINT BytesWritten = 0;
    ULONG ClusterSize;
    PVOID Buffer;
    FRESULT Result;
    NTSTATUS Status;
    BOOLEAN PagingIo = !!(Irp->Flags & IRP_PAGING_IO);

    if (DeviceObject == ExFatGlobalData->DeviceObject || !FileObject)
        return STATUS_INVALID_DEVICE_REQUEST;
    Vcb = DeviceObject->DeviceExtension;
    Fcb = FileObject->FsContext;
    Ccb = FileObject->FsContext2;
    if (!Fcb || !Ccb)
        return STATUS_INVALID_HANDLE;
    if (Fcb->IsDirectory)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (Vcb->ReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;

    if (Offset.LowPart == FILE_WRITE_TO_END_OF_FILE && Offset.HighPart == -1)
        Offset = Fcb->Header.FileSize;
    else if (Offset.LowPart == FILE_USE_FILE_POINTER_POSITION && Offset.HighPart == -1)
        Offset = FileObject->CurrentByteOffset;
    if (Offset.QuadPart < 0 || (ULONGLONG)Offset.QuadPart + Length < (ULONGLONG)Offset.QuadPart)
        return STATUS_INVALID_PARAMETER;
    if (!Length)
        return STATUS_SUCCESS;

    Status = ExFatLockUserBuffer(Irp, Length, IoReadAccess);
    if (!NT_SUCCESS(Status))
        return Status;
    Buffer = ExFatGetUserBuffer(Irp, PagingIo);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (Fcb->IsVolume)
    {
        Status = ExFatWriteVolume(Vcb, FileObject, Irp, Buffer, Length, Offset);
    }
    else
    {
        if (!Ccb->HandleOpen || Ccb->IsDirectory)
            return STATUS_FILE_CLOSED;
        if (!PagingIo && !ExFatIsWriteAccess(Ccb->DesiredAccess))
            return STATUS_ACCESS_DENIED;
        if (!PagingIo && !FsRtlCheckLockForWriteAccess(&Fcb->FileLock, Irp))
            return STATUS_FILE_LOCK_CONFLICT;
        if (PagingIo && (ULONGLONG)Offset.QuadPart >= (ULONGLONG)Fcb->Header.FileSize.QuadPart)
            return STATUS_SUCCESS;
        if (PagingIo && Length > (ULONGLONG)Fcb->Header.FileSize.QuadPart - Offset.QuadPart)
            Length = (ULONG)((ULONGLONG)Fcb->Header.FileSize.QuadPart - Offset.QuadPart);

        ExAcquireResourceExclusiveLite(PagingIo ? &Fcb->PagingIoResource : &Fcb->MainResource,
                                       TRUE);
        ExFatAcquireFatFs(Vcb);
        Result = f_lseek(&Ccb->Handle.File, (FSIZE_t)Offset.QuadPart);
        if (Result == FR_OK)
            Result = f_write(&Ccb->Handle.File, Buffer, Length, &BytesWritten);
        if (Result == FR_OK &&
            ((Stack->Flags & SL_WRITE_THROUGH) || (FileObject->Flags & FO_WRITE_THROUGH)))
        {
            Result = f_sync(&Ccb->Handle.File);
        }
        if (Result == FR_OK)
        {
            Fcb->Header.FileSize.QuadPart = f_size(&Ccb->Handle.File);
            Fcb->Header.ValidDataLength = Fcb->Header.FileSize;
            ClusterSize = Vcb->FileSystem.csize * Vcb->BytesPerSector;
            Fcb->Header.AllocationSize.QuadPart = ExFatRoundUp(f_size(&Ccb->Handle.File),
                                                               ClusterSize);
        }
        ExFatReleaseFatFs(Vcb);
        ExReleaseResourceLite(PagingIo ? &Fcb->PagingIoResource : &Fcb->MainResource);

        Status = ExFatMapResult(Result);
        if (NT_SUCCESS(Status))
        {
            Irp->IoStatus.Information = BytesWritten;
            FileObject->Flags |= FO_FILE_MODIFIED;
        }
    }

    if (NT_SUCCESS(Status) && !PagingIo && (FileObject->Flags & FO_SYNCHRONOUS_IO))
        FileObject->CurrentByteOffset.QuadPart = Offset.QuadPart + Irp->IoStatus.Information;
    return Status;
}
