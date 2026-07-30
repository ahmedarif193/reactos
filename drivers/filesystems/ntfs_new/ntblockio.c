/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfspch.h"

typedef struct _NTFS_VOLUME_IO_CONTEXT
{
    PFILE_OBJECT FileObject;
    LARGE_INTEGER ByteOffset;
} NTFS_VOLUME_IO_CONTEXT, *PNTFS_VOLUME_IO_CONTEXT;

static
NTSTATUS
NTAPI
NtfsVolumeIoCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PVOID Context)
{
    PNTFS_VOLUME_IO_CONTEXT IoContext =
        (PNTFS_VOLUME_IO_CONTEXT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Irp->IoStatus.Status) &&
        (IoContext->FileObject->Flags & FO_SYNCHRONOUS_IO))
    {
        IoContext->FileObject->CurrentByteOffset.QuadPart =
            IoContext->ByteOffset.QuadPart +
            Irp->IoStatus.Information;
    }

    ExFreePoolWithTag(IoContext, TAG_NTFS);
    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsForwardVolumeIo(
    _In_ PVolumeContextBlock VolCB,
    _In_ PFileContextBlock FileCB,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN Write)
{
    PIO_STACK_LOCATION IrpSp;
    PIO_STACK_LOCATION NextIrpSp;
    PFILE_OBJECT FileObject;
    PNTFS_VOLUME_IO_CONTEXT IoContext;
    LARGE_INTEGER ByteOffset;
    PVOID Buffer;
    PMDL Mdl;
    ULONG Length;
    NTSTATUS Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpSp->FileObject;
    ByteOffset = Write ? IrpSp->Parameters.Write.ByteOffset
                       : IrpSp->Parameters.Read.ByteOffset;
    Length = Write ? IrpSp->Parameters.Write.Length
                   : IrpSp->Parameters.Read.Length;
    Irp->IoStatus.Information = 0;

    if (!VolCB || !FileCB || !FileCB->IsVolumeOpen ||
        !FileObject || !VolCB->StorageDevice ||
        !VolCB->BytesPerSector)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if (FileCB->CleanupComplete)
    {
        Status = STATUS_FILE_CLOSED;
        goto Complete;
    }
    if (Write)
    {
        if (!(FileCB->DesiredAccess &
              (FILE_WRITE_DATA | FILE_APPEND_DATA)))
        {
            Status = STATUS_ACCESS_DENIED;
            goto Complete;
        }
        if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
        {
            Status = STATUS_MEDIA_WRITE_PROTECTED;
            goto Complete;
        }

        ExAcquireFastMutex(&VolCB->VolumeStateMutex);
        if (VolCB->Dismounted)
            Status = STATUS_VOLUME_DISMOUNTED;
        else if (VolCB->Dismounting)
            Status = STATUS_DEVICE_BUSY;
        else if (VolCB->VolumeLockOwner != FileObject)
            Status = STATUS_ACCESS_DENIED;
        else
            Status = STATUS_SUCCESS;
        ExReleaseFastMutex(&VolCB->VolumeStateMutex);
        if (!NT_SUCCESS(Status))
            goto Complete;
    }
    else if (!(FileCB->DesiredAccess & FILE_READ_DATA))
    {
        Status = STATUS_ACCESS_DENIED;
        goto Complete;
    }

    if (ByteOffset.QuadPart < 0 ||
        (ByteOffset.QuadPart % VolCB->BytesPerSector) != 0 ||
        (Length % VolCB->BytesPerSector) != 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if (Length == 0)
    {
        Status = STATUS_SUCCESS;
        goto Complete;
    }

    if (Irp->MdlAddress)
    {
        if (Irp->MdlAddress->Next ||
            MmGetMdlByteCount(Irp->MdlAddress) < Length)
        {
            Status = STATUS_INVALID_USER_BUFFER;
            goto Complete;
        }
    }
    else
    {
        Buffer = Irp->AssociatedIrp.SystemBuffer
            ? Irp->AssociatedIrp.SystemBuffer
            : Irp->UserBuffer;
        if (!Buffer)
        {
            Status = STATUS_INVALID_USER_BUFFER;
            goto Complete;
        }

        Mdl = IoAllocateMdl(Buffer,
                            Length,
                            FALSE,
                            FALSE,
                            Irp);
        if (!Mdl)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Complete;
        }

        Status = STATUS_SUCCESS;
        _SEH2_TRY
        {
            MmProbeAndLockPages(
                Mdl,
                Irp->AssociatedIrp.SystemBuffer
                    ? KernelMode
                    : Irp->RequestorMode,
                Write ? IoReadAccess : IoWriteAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            Irp->MdlAddress = NULL;
            IoFreeMdl(Mdl);
            goto Complete;
        }
    }

    IoContext = (PNTFS_VOLUME_IO_CONTEXT)
        ExAllocatePoolZero(NonPagedPool,
                           sizeof(*IoContext),
                           TAG_NTFS);
    if (!IoContext)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Complete;
    }
    IoContext->FileObject = FileObject;
    IoContext->ByteOffset = ByteOffset;

    IoCopyCurrentIrpStackLocationToNext(Irp);
    NextIrpSp = IoGetNextIrpStackLocation(Irp);
    NextIrpSp->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    NextIrpSp->FileObject = NULL;
    IoSetCompletionRoutine(Irp,
                           NtfsVolumeIoCompletion,
                           IoContext,
                           TRUE,
                           TRUE,
                           TRUE);
    return IoCallDriver(VolCB->StorageDevice, Irp);

Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

//TODO: This shouldn't really be needed. Honestly there's something wrong with this.
NTSTATUS
NTAPI
ByPasscompletion (
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);

    if (!NT_SUCCESS( Irp->IoStatus.Status )) {

        Irp->IoStatus.Information = 0;
    }

    KeSetEvent( (KEVENT*)Context, 0, FALSE );
    Irp->IoStatus.Status = STATUS_SUCCESS;
    return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS
ReadDisk(_In_    PDEVICE_OBJECT DeviceToRead,
         _In_    ULONGLONG Offset,
         _In_    ULONG Length,
         _Inout_ PUCHAR Buffer)
{
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    IO_STATUS_BLOCK Iosb;

    PAGED_CODE();

    //  Initialize the event we're going to use
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    //  Build the IRP for the operation
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
                                       DeviceToRead,
                                       Buffer,
                                       Length,
                                       (PLARGE_INTEGER) &Offset,
                                       &Event,
                                       &Iosb);

    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    SetFlag(IoGetNextIrpStackLocation(Irp)->Flags, SL_OVERRIDE_VERIFY_VOLUME);


    //TODO: There's something SERIOUSLY wrong with completetion.
    IoSetCompletionRoutine( Irp,
                            ByPasscompletion,
                            &Event,
                            TRUE,
                            TRUE,
                            TRUE );

    //  Call the device to do the read and wait for it to finish.
    Status = IoCallDriver(DeviceToRead, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        // Status = Iosb.Status; ???
    }

    NT_ASSERT(Status != STATUS_VERIFY_REQUIRED);

    /*  Special case this error code because this probably means we used
     *  the wrong sector size and we want to reject STATUS_WRONG_VOLUME.
     */
    if (Status == STATUS_INVALID_PARAMETER)
        return Status;

    //  And return to our caller.
    return Status;
}

// You might notice Carl this looks exactly like ReadDisk, So let's go over WHY...
NTSTATUS
WriteDisk(_In_    PDEVICE_OBJECT DeviceToWrite,
          _In_    ULONGLONG Offset,
          _In_    ULONG Length,
          _In_    PUCHAR Buffer)
{
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    IO_STATUS_BLOCK Iosb;

    //This code can be paged, required for pretty much everything in this driver.
    PAGED_CODE();

    //  Initialize an event which will be used to STALL THE OS UNTIL THE OPERATION COMPLETES
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /* let's build an IO request, the Irp Representing the request buffer. */
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE, //we ARE writing
                                       DeviceToWrite, // this IS the devce
                                       Buffer, // This is the bufffer
                                       Length, /// how many bytes
                                       (PLARGE_INTEGER) &Offset, //offset on disk
                                       &Event, //event in question
                                       &Iosb); //status check

    if (Irp == NULL) //if an IO request cant be allocated
        return STATUS_INSUFFICIENT_RESOURCES;

    SetFlag(IoGetNextIrpStackLocation( Irp )->Flags, SL_OVERRIDE_VERIFY_VOLUME); // override this because it causes problems
    //TODO: There's something SERIOUSLY wrong with completetion.
    IoSetCompletionRoutine( Irp,
                            ByPasscompletion,
                            &Event,
                            TRUE,
                            TRUE,
                            TRUE );

    //  Call the device to do the write and wait for it to finish.
    Status = IoCallDriver(DeviceToWrite, Irp); // DO DE WRITE

    if (Status == STATUS_PENDING)
    {
        // Infinitely stall the OS until this kernel mode executive event completes
        (VOID)KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, (PLARGE_INTEGER)NULL);
       // Status = Iosb.Status; ???
    }

    NT_ASSERT(Status != STATUS_VERIFY_REQUIRED);

    /*  Special case this error code because this probably means we used
     *  the wrong sector size and we want to reject STATUS_WRONG_VOLUME.
     */
    if (Status == STATUS_INVALID_PARAMETER)
        return Status;

    //  And return to our caller.
    return Status;
}

NTSTATUS
DeviceIoControl(_In_    PDEVICE_OBJECT DeviceObject,
                _In_    ULONG ControlCode,
                _In_    PVOID InputBuffer,
                _In_    ULONG InputBufferSize,
                _Inout_ PVOID OutputBuffer,
                _Inout_ PULONG OutputBufferSize,
                _In_    BOOLEAN Override)
{
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(ControlCode,
                                        DeviceObject,
                                        InputBuffer,
                                        InputBufferSize,
                                        OutputBuffer,
                                        (OutputBufferSize) ? *OutputBufferSize : 0,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Override)
    {
        Stack = IoGetNextIrpStackLocation(Irp);
        Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (OutputBufferSize)
    {
        *OutputBufferSize = IoStatus.Information;
    }

    return Status;
}
