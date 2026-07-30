/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the filesystem controls
 * COPYRIGHT:   Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

 #include "ntfspch.h"

#ifndef FSCTL_DELETE_EXTERNAL_BACKING
#define FSCTL_DELETE_EXTERNAL_BACKING \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 197, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#endif

 static
 BOOLEAN
 NtfsIsIrpTopLevel (_In_ PIRP Irp)
 {
     PAGED_CODE();

     if (!IoGetTopLevelIrp())
     {
         IoSetTopLevelIrp(Irp);
         return TRUE;
     }

     return FALSE;
 }

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdFlushBuffers)
#endif

/* FUNCTIONS ****************************************************************/

static
NTSTATUS
NtfsGetReparsePoint(_Inout_ PIRP Irp,
                    _In_ PIO_STACK_LOCATION IrpSp)
{
    PFileContextBlock FileCB;
    ULONG BufferLength;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;
    if (!IrpSp->FileObject ||
        !IrpSp->FileObject->FsContext)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileCB = (PFileContextBlock)IrpSp->FileObject->FsContext;
    if (!FileCB->FileRec)
        return STATUS_INVALID_PARAMETER;

    BufferLength =
        IrpSp->Parameters.FileSystemControl.OutputBufferLength;
    if (BufferLength != 0 && !Irp->AssociatedIrp.SystemBuffer)
        return STATUS_INVALID_USER_BUFFER;

    Status = NtfsFileRecordReadReparsePoint(
        FileCB->FileRec,
        (PUCHAR)Irp->AssociatedIrp.SystemBuffer,
        &BufferLength);
    if (NT_SUCCESS(Status))
    {
        ((PReparsePointEx)Irp->AssociatedIrp.SystemBuffer)->Padding = 0;
        Irp->IoStatus.Information = BufferLength;
    }
    return Status;
}

static
NTSTATUS
NtfsUpdateReparsePoint(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ BOOLEAN Delete)
{
    PFileContextBlock FileCB;
    PFILE_OBJECT FileObject;
    PVolumeContextBlock VolCB;
    PUCHAR Input;
    ULONG InputLength;
    ULONG ReparseTag = 0;
    BOOLEAN ResourceAcquired = FALSE;
    NTSTATUS Status;

    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!FileObject || !FileCB || !FileCB->FileRec ||
        !VolCB || !VolCB->DiskVolume)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (FileCB->RequestedType != TypeData ||
        FileCB->RequestedStream)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Irp->RequestorMode == UserMode &&
        !(FileCB->DesiredAccess &
          (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES)))
    {
        return STATUS_ACCESS_DENIED;
    }
    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (IrpSp->Parameters.FileSystemControl.OutputBufferLength != 0)
        return STATUS_INVALID_PARAMETER;

    InputLength =
        IrpSp->Parameters.FileSystemControl.InputBufferLength;
    Input = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    if (!Input && InputLength != 0)
        return STATUS_INVALID_USER_BUFFER;

    if (!Delete && Input &&
        InputLength >= sizeof(ReparseTag))
    {
        RtlCopyMemory(&ReparseTag,
                      Input,
                      sizeof(ReparseTag));
        if (ReparseTag ==
                NTFS_IO_REPARSE_TAG_SYMLINK &&
            Irp->RequestorMode == UserMode &&
            !SeSinglePrivilegeCheck(
                RtlConvertLongToLuid(
                    SE_CREATE_SYMBOLIC_LINK_PRIVILEGE),
                UserMode))
        {
            return STATUS_ACCESS_DENIED;
        }
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &FileCB->MainResource,
        TRUE);
    ResourceAcquired = TRUE;
    Status = Delete
        ? NtfsFileRecordDeleteReparsePoint(
            FileCB->FileRec,
            Input,
            InputLength)
        : NtfsFileRecordSetReparsePoint(
            FileCB->FileRec,
            Input,
            InputLength);
    if (NT_SUCCESS(Status))
        FileObject->Flags |= FO_FILE_MODIFIED;

    if (ResourceAcquired)
    {
        ExReleaseResourceLite(&FileCB->MainResource);
        KeLeaveCriticalRegion();
    }
    return Status;
}

static
NTSTATUS
NtfsDeleteExternalBacking(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PFILE_OBJECT FileObject;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    NTSTATUS Status;

    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!FileObject || !FileCB || !FileCB->FileRec ||
        !VolCB || !VolCB->DiskVolume)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (FileCB->RequestedType != TypeData ||
        FileCB->RequestedStream)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (IrpSp->Parameters.FileSystemControl.InputBufferLength != 0 ||
        IrpSp->Parameters.FileSystemControl.OutputBufferLength != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Irp->RequestorMode == UserMode &&
        !(FileCB->DesiredAccess &
          (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES)))
    {
        return STATUS_ACCESS_DENIED;
    }
    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
        return STATUS_MEDIA_WRITE_PROTECTED;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &FileCB->MainResource,
        TRUE);
    Status = NtfsFileRecordDeleteExternalBacking(
        FileCB->FileRec);
    if (NT_SUCCESS(Status))
    {
        NtfsRefreshFileSizes(FileCB,
                             FileObject);
        FileObject->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;
    }
    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();
    return Status;
}

static
NTSTATUS
NtfsSetSparse(_In_ PDEVICE_OBJECT VolumeDeviceObject,
              _Inout_ PIRP Irp,
              _In_ PIO_STACK_LOCATION IrpSp)
{
    PFILE_OBJECT FileObject;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    ULONG InputLength;
    BOOLEAN SetSparse = TRUE;
    NTSTATUS Status;

    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!FileObject || !FileCB || !FileCB->FileRec ||
        !VolCB || !VolCB->DiskVolume)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (FileCB->RequestedType != TypeData ||
        (NtfsFileRecordGetHeader(FileCB->FileRec)->
             Flags & FR_IS_DIRECTORY))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!(FileCB->DesiredAccess &
          (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES)))
    {
        return STATUS_ACCESS_DENIED;
    }
    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (IrpSp->Parameters.FileSystemControl.
            OutputBufferLength != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InputLength =
        IrpSp->Parameters.FileSystemControl.
            InputBufferLength;
    if (InputLength != 0)
    {
        if (InputLength <
                sizeof(FILE_SET_SPARSE_BUFFER) ||
            !Irp->AssociatedIrp.SystemBuffer)
        {
            return STATUS_INVALID_PARAMETER;
        }
        SetSparse =
            ((PFILE_SET_SPARSE_BUFFER)
                Irp->AssociatedIrp.SystemBuffer)->
                    SetSparse != FALSE;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &FileCB->MainResource,
        TRUE);
    Status = NtfsFileRecordSetSparse(
        FileCB->FileRec,
        FileCB->RequestedType,
        FileCB->RequestedStream,
        SetSparse);
    if (NT_SUCCESS(Status))
    {
        NtfsRefreshFileSizes(FileCB,
                             FileObject);
        FileObject->Flags |= FO_FILE_MODIFIED;
    }
    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();
    return Status;
}

static
NTSTATUS
NtfsSetZeroData(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                _Inout_ PIRP Irp,
                _In_ PIO_STACK_LOCATION IrpSp)
{
    PFILE_ZERO_DATA_INFORMATION Input;
    PFILE_OBJECT FileObject;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    IO_STATUS_BLOCK CacheStatus;
    NTSTATUS Status;

    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!FileObject || !FileCB || !FileCB->FileRec ||
        !VolCB || !VolCB->DiskVolume)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (FileCB->RequestedType != TypeData ||
        (NtfsFileRecordGetHeader(FileCB->FileRec)->
             Flags & FR_IS_DIRECTORY))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!(FileCB->DesiredAccess &
          (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES)))
    {
        return STATUS_ACCESS_DENIED;
    }
    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (IrpSp->Parameters.FileSystemControl.
            InputBufferLength <
            sizeof(FILE_ZERO_DATA_INFORMATION) ||
        IrpSp->Parameters.FileSystemControl.
            OutputBufferLength != 0 ||
        !Irp->AssociatedIrp.SystemBuffer)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Input = (PFILE_ZERO_DATA_INFORMATION)
        Irp->AssociatedIrp.SystemBuffer;
    if (Input->FileOffset.QuadPart < 0 ||
        Input->BeyondFinalZero.QuadPart < 0 ||
        Input->FileOffset.QuadPart >
            Input->BeyondFinalZero.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &FileCB->MainResource,
        TRUE);

    /*
     * The shared core writes the volume directly. Drain and invalidate any
     * cached pages first so a later lazy write cannot restore old data.
     */
    if (CcIsFileCached(FileObject))
    {
        CcFlushCache(FileObject->SectionObjectPointer,
                     NULL,
                     0,
                     &CacheStatus);
        if (!NT_SUCCESS(CacheStatus.Status))
        {
            Status = CacheStatus.Status;
            goto Done;
        }
        if (!CcPurgeCacheSection(
                FileObject->SectionObjectPointer,
                NULL,
                0,
                FALSE))
        {
            Status = STATUS_USER_MAPPED_FILE;
            goto Done;
        }
    }

    Status = NtfsFileRecordSetZeroData(
        FileCB->FileRec,
        FileCB->RequestedType,
        FileCB->RequestedStream,
        (ULONGLONG)Input->FileOffset.QuadPart,
        (ULONGLONG)Input->
            BeyondFinalZero.QuadPart);
    if (NT_SUCCESS(Status))
    {
        NtfsRefreshFileSizes(FileCB,
                             FileObject);
        FileObject->Flags |= FO_FILE_MODIFIED;
    }

Done:
    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();
    return Status;
}

static
NTSTATUS
NtfsQueryAllocatedRanges(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    FILE_ALLOCATED_RANGE_BUFFER Query;
    PFILE_ALLOCATED_RANGE_BUFFER Output;
    PNtfsAllocatedRange Ranges = NULL;
    PFILE_OBJECT FileObject;
    PFileContextBlock FileCB;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG Capacity;
    ULONG Count;
    NTSTATUS Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;
    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    if (!FileObject || !FileCB || !FileCB->FileRec)
        return STATUS_INVALID_PARAMETER;
    if (FileCB->RequestedType != TypeData ||
        (NtfsFileRecordGetHeader(FileCB->FileRec)->
             Flags & FR_IS_DIRECTORY))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!(FileCB->DesiredAccess & FILE_READ_DATA))
        return STATUS_ACCESS_DENIED;

    InputLength =
        IrpSp->Parameters.FileSystemControl.
            InputBufferLength;
    OutputLength =
        IrpSp->Parameters.FileSystemControl.
            OutputBufferLength;
    Output = (PFILE_ALLOCATED_RANGE_BUFFER)
        GetUserBuffer(Irp);
    if (InputLength <
            sizeof(FILE_ALLOCATED_RANGE_BUFFER) ||
        !IrpSp->Parameters.FileSystemControl.
            Type3InputBuffer)
    {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY
    {
        if (Irp->RequestorMode != KernelMode)
        {
            ProbeForRead(
                IrpSp->Parameters.FileSystemControl.
                    Type3InputBuffer,
                sizeof(FILE_ALLOCATED_RANGE_BUFFER),
                sizeof(UCHAR));
            if (OutputLength != 0)
            {
                ProbeForWrite(Output,
                              OutputLength,
                              sizeof(UCHAR));
            }
        }
        else if (OutputLength != 0 && !Output)
        {
            Status = STATUS_INVALID_PARAMETER;
        }

        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(
                &Query,
                IrpSp->Parameters.FileSystemControl.
                    Type3InputBuffer,
                sizeof(Query));
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        return Status;

    if (Query.FileOffset.QuadPart < 0 ||
        Query.Length.QuadPart < 0 ||
        Query.Length.QuadPart >
            MAXLONGLONG -
                Query.FileOffset.QuadPart)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Query.Length.QuadPart != 0 &&
        OutputLength <
            sizeof(FILE_ALLOCATED_RANGE_BUFFER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Capacity =
        OutputLength /
        sizeof(FILE_ALLOCATED_RANGE_BUFFER);
    if (Capacity != 0)
    {
        Ranges = (PNtfsAllocatedRange)
            ExAllocatePoolWithTag(
                PagedPool,
                Capacity * sizeof(*Ranges),
                TAG_NTFS);
        if (!Ranges)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    Count = Capacity;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(
        &FileCB->MainResource,
        TRUE);
    Status = NtfsFileRecordQueryAllocatedRanges(
        FileCB->FileRec,
        FileCB->RequestedType,
        FileCB->RequestedStream,
        (ULONGLONG)Query.FileOffset.QuadPart,
        (ULONGLONG)Query.Length.QuadPart,
        Ranges,
        &Count);
    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();

    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        _SEH2_TRY
        {
            ULONG Index;

            for (Index = 0; Index < Count; Index++)
            {
                Output[Index].FileOffset.QuadPart =
                    (LONGLONG)Ranges[Index].FileOffset;
                Output[Index].Length.QuadPart =
                    (LONGLONG)Ranges[Index].Length;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            Count = 0;
        }
        _SEH2_END;
    }

    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        Irp->IoStatus.Information =
            Count * sizeof(FILE_ALLOCATED_RANGE_BUFFER);
    }
    if (Ranges)
        ExFreePoolWithTag(Ranges, TAG_NTFS);
    return Status;
}

static
NTSTATUS
NtfsGetRetrievalPointers(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    STARTING_VCN_INPUT_BUFFER Input;
    PRETRIEVAL_POINTERS_BUFFER Output;
    PNtfsRetrievalExtent Extents = NULL;
    PFILE_OBJECT FileObject;
    PFileContextBlock FileCB;
    AttributeType RequestedType;
    PWSTR RequestedStream;
    ULONGLONG ReturnedStartingVcn;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG HeaderLength;
    ULONG Capacity;
    ULONG Count;
    NTSTATUS Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;
    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    if (!FileObject || !FileCB || !FileCB->FileRec)
        return STATUS_INVALID_PARAMETER;

    InputLength =
        IrpSp->Parameters.FileSystemControl.
            InputBufferLength;
    OutputLength =
        IrpSp->Parameters.FileSystemControl.
            OutputBufferLength;
    Output = (PRETRIEVAL_POINTERS_BUFFER)
        GetUserBuffer(Irp);
    if (InputLength <
            sizeof(STARTING_VCN_INPUT_BUFFER) ||
        !IrpSp->Parameters.FileSystemControl.
            Type3InputBuffer)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputLength <
        sizeof(RETRIEVAL_POINTERS_BUFFER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    _SEH2_TRY
    {
        if (Irp->RequestorMode != KernelMode)
        {
            ProbeForRead(
                IrpSp->Parameters.FileSystemControl.
                    Type3InputBuffer,
                sizeof(STARTING_VCN_INPUT_BUFFER),
                sizeof(UCHAR));
            ProbeForWrite(Output,
                          OutputLength,
                          sizeof(UCHAR));
        }
        else if (!Output)
        {
            Status = STATUS_INVALID_PARAMETER;
        }

        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(
                &Input,
                IrpSp->Parameters.FileSystemControl.
                    Type3InputBuffer,
                sizeof(Input));
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Input.StartingVcn.QuadPart < 0)
        return STATUS_INVALID_PARAMETER;

    HeaderLength = FIELD_OFFSET(
        RETRIEVAL_POINTERS_BUFFER,
        Extents);
    Capacity =
        (OutputLength - HeaderLength) /
        sizeof(Output->Extents[0]);
    Extents = (PNtfsRetrievalExtent)
        ExAllocatePoolWithTag(
            PagedPool,
            Capacity * sizeof(*Extents),
            TAG_NTFS);
    if (!Extents)
        return STATUS_INSUFFICIENT_RESOURCES;

    RequestedType = FileCB->RequestedType;
    RequestedStream = FileCB->RequestedStream;
    if ((NtfsFileRecordGetHeader(FileCB->FileRec)->
             Flags & FR_IS_DIRECTORY) &&
        !RequestedStream)
    {
        RequestedType = TypeIndexAllocation;
        RequestedStream = L"$I30";
    }

    Count = Capacity;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(
        &FileCB->MainResource,
        TRUE);
    Status = NtfsFileRecordQueryRetrievalPointers(
        FileCB->FileRec,
        RequestedType,
        RequestedStream,
        (ULONGLONG)Input.StartingVcn.QuadPart,
        &ReturnedStartingVcn,
        Extents,
        &Count);
    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();
    if (Status == STATUS_NOT_FOUND &&
        RequestedType == TypeIndexAllocation)
    {
        Status = STATUS_END_OF_FILE;
    }

    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        ULONG Index;

        if (ReturnedStartingVcn > MAXLONGLONG)
        {
            Status = STATUS_FILE_TOO_LARGE;
            Count = 0;
            goto Done;
        }
        for (Index = 0; Index < Count; Index++)
        {
            if (Extents[Index].NextVcn >
                MAXLONGLONG)
            {
                Status = STATUS_FILE_TOO_LARGE;
                Count = 0;
                goto Done;
            }
        }

        _SEH2_TRY
        {
            Output->ExtentCount = Count;
            Output->StartingVcn.QuadPart =
                (LONGLONG)ReturnedStartingVcn;
            for (Index = 0; Index < Count; Index++)
            {
                Output->Extents[Index].
                    NextVcn.QuadPart =
                    (LONGLONG)
                        Extents[Index].NextVcn;
                Output->Extents[Index].
                    Lcn.QuadPart =
                    Extents[Index].Lcn;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            Count = 0;
        }
        _SEH2_END;
    }

Done:
    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        Irp->IoStatus.Information =
            HeaderLength +
            Count * sizeof(Output->Extents[0]);
    }
    ExFreePoolWithTag(Extents, TAG_NTFS);
    return Status;
}

static
NTSTATUS
NtfsGetVolumeBitmap(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    STARTING_LCN_INPUT_BUFFER Input;
    PVOLUME_BITMAP_BUFFER Output;
    PVolumeContextBlock VolCB;
    PUCHAR Bitmap = NULL;
    ULONGLONG ReturnedStartingLcn;
    ULONGLONG BitmapSize;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG HeaderLength;
    ULONG Capacity;
    ULONG Length;
    NTSTATUS Status = STATUS_SUCCESS;

    Irp->IoStatus.Information = 0;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!VolCB || !VolCB->DiskVolume ||
        !IrpSp->FileObject ||
        IrpSp->FileObject->FileName.Length != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InputLength =
        IrpSp->Parameters.FileSystemControl.
            InputBufferLength;
    OutputLength =
        IrpSp->Parameters.FileSystemControl.
            OutputBufferLength;
    Output = (PVOLUME_BITMAP_BUFFER)
        GetUserBuffer(Irp);
    if (InputLength <
            sizeof(STARTING_LCN_INPUT_BUFFER) ||
        !IrpSp->Parameters.FileSystemControl.
            Type3InputBuffer)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputLength < sizeof(VOLUME_BITMAP_BUFFER))
        return STATUS_BUFFER_TOO_SMALL;

    _SEH2_TRY
    {
        if (Irp->RequestorMode != KernelMode)
        {
            ProbeForRead(
                IrpSp->Parameters.FileSystemControl.
                    Type3InputBuffer,
                sizeof(STARTING_LCN_INPUT_BUFFER),
                sizeof(UCHAR));
            ProbeForWrite(Output,
                          OutputLength,
                          sizeof(UCHAR));
        }
        else if (!Output)
        {
            Status = STATUS_INVALID_PARAMETER;
        }

        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(
                &Input,
                IrpSp->Parameters.FileSystemControl.
                    Type3InputBuffer,
                sizeof(Input));
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Input.StartingLcn.QuadPart < 0)
        return STATUS_INVALID_PARAMETER;

    HeaderLength = FIELD_OFFSET(
        VOLUME_BITMAP_BUFFER,
        Buffer);
    Capacity = OutputLength - HeaderLength;
    Bitmap = (PUCHAR)ExAllocatePoolWithTag(
        PagedPool,
        Capacity,
        TAG_NTFS);
    if (!Bitmap)
        return STATUS_INSUFFICIENT_RESOURCES;

    Length = Capacity;
    Status = NtfsVolumeReadBitmap(
        VolCB->DiskVolume,
        (ULONGLONG)Input.StartingLcn.QuadPart,
        &ReturnedStartingLcn,
        &BitmapSize,
        Bitmap,
        &Length);
    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        _SEH2_TRY
        {
            Output->StartingLcn.QuadPart =
                (LONGLONG)ReturnedStartingLcn;
            Output->BitmapSize.QuadPart =
                (LONGLONG)BitmapSize;
            RtlCopyMemory(Output->Buffer,
                          Bitmap,
                          Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            Length = 0;
        }
        _SEH2_END;
    }

    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        Irp->IoStatus.Information =
            HeaderLength + Length;
    }
    ExFreePoolWithTag(Bitmap, TAG_NTFS);
    return Status;
}

static
NTSTATUS
NtfsGetNtfsVolumeData(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PVolumeContextBlock VolCB;
    PNTFS_VOLUME_DATA_BUFFER Output;
    PNTFS_EXTENDED_VOLUME_DATA Extended;
    NtfsVolumeInformation Information;
    ULONG OutputLength;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!VolCB || !VolCB->DiskVolume ||
        !IrpSp->FileObject)
    {
        return STATUS_INVALID_PARAMETER;
    }

    OutputLength =
        IrpSp->Parameters.FileSystemControl.
            OutputBufferLength;
    if (OutputLength <
        sizeof(NTFS_VOLUME_DATA_BUFFER))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    Output = (PNTFS_VOLUME_DATA_BUFFER)
        Irp->AssociatedIrp.SystemBuffer;
    if (!Output)
        return STATUS_INVALID_USER_BUFFER;

    Status = NtfsVolumeQueryInformation(
        VolCB->DiskVolume,
        &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information.NumberSectors > MAXLONGLONG ||
        Information.TotalClusters > MAXLONGLONG ||
        Information.FreeClusters > MAXLONGLONG ||
        Information.TotalReserved > MAXLONGLONG ||
        Information.MftValidDataLength > MAXLONGLONG ||
        Information.MftStartLcn > MAXLONGLONG ||
        Information.Mft2StartLcn > MAXLONGLONG ||
        Information.MftZoneStart > MAXLONGLONG ||
        Information.MftZoneEnd > MAXLONGLONG)
    {
        return STATUS_FILE_TOO_LARGE;
    }

    RtlZeroMemory(Output, sizeof(*Output));
    Output->VolumeSerialNumber.QuadPart =
        (LONGLONG)Information.VolumeSerialNumber;
    Output->NumberSectors.QuadPart =
        (LONGLONG)Information.NumberSectors;
    Output->TotalClusters.QuadPart =
        (LONGLONG)Information.TotalClusters;
    Output->FreeClusters.QuadPart =
        (LONGLONG)Information.FreeClusters;
    Output->TotalReserved.QuadPart =
        (LONGLONG)Information.TotalReserved;
    Output->BytesPerSector =
        Information.BytesPerSector;
    Output->BytesPerCluster =
        Information.BytesPerCluster;
    Output->BytesPerFileRecordSegment =
        Information.BytesPerFileRecordSegment;
    Output->ClustersPerFileRecordSegment =
        Information.ClustersPerFileRecordSegment;
    Output->MftValidDataLength.QuadPart =
        (LONGLONG)Information.MftValidDataLength;
    Output->MftStartLcn.QuadPart =
        (LONGLONG)Information.MftStartLcn;
    Output->Mft2StartLcn.QuadPart =
        (LONGLONG)Information.Mft2StartLcn;
    Output->MftZoneStart.QuadPart =
        (LONGLONG)Information.MftZoneStart;
    Output->MftZoneEnd.QuadPart =
        (LONGLONG)Information.MftZoneEnd;
    Irp->IoStatus.Information = sizeof(*Output);

    if (OutputLength >=
        sizeof(*Output) + sizeof(*Extended))
    {
        Extended = (PNTFS_EXTENDED_VOLUME_DATA)
            ((PUCHAR)Output + sizeof(*Output));
        RtlZeroMemory(Extended, sizeof(*Extended));
        Extended->ByteCount = sizeof(*Extended);
        Extended->MajorVersion =
            Information.MajorVersion;
        Extended->MinorVersion =
            Information.MinorVersion;
        Irp->IoStatus.Information +=
            sizeof(*Extended);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsGetNtfsFileRecord(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    NTFS_FILE_RECORD_INPUT_BUFFER Input;
    PNTFS_FILE_RECORD_OUTPUT_BUFFER Output;
    PVolumeContextBlock VolCB;
    PNtfsMasterFileTable Mft;
    ULONGLONG ReturnedFileReference;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG HeaderLength;
    ULONG RecordLength;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!VolCB || !VolCB->DiskVolume ||
        !IrpSp->FileObject ||
        IrpSp->FileObject->FileName.Length != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InputLength =
        IrpSp->Parameters.FileSystemControl.
            InputBufferLength;
    OutputLength =
        IrpSp->Parameters.FileSystemControl.
            OutputBufferLength;
    Output = (PNTFS_FILE_RECORD_OUTPUT_BUFFER)
        Irp->AssociatedIrp.SystemBuffer;
    if (InputLength < sizeof(Input))
        return STATUS_INVALID_PARAMETER;
    if (!Output)
        return STATUS_INVALID_USER_BUFFER;

    RtlCopyMemory(&Input, Output, sizeof(Input));
    Mft = NtfsVolumeGetMft(VolCB->DiskVolume);
    if (!Mft)
        return STATUS_INVALID_DEVICE_STATE;

    RecordLength = 0;
    Status = NtfsMasterFileTableReadFileRecord(
        Mft,
        (ULONGLONG)Input.FileReferenceNumber.QuadPart,
        &ReturnedFileReference,
        NULL,
        &RecordLength);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;

    HeaderLength = FIELD_OFFSET(
        NTFS_FILE_RECORD_OUTPUT_BUFFER,
        FileRecordBuffer);
    if (OutputLength < HeaderLength ||
        RecordLength > OutputLength - HeaderLength)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = NtfsMasterFileTableReadFileRecord(
        Mft,
        (ULONGLONG)Input.FileReferenceNumber.QuadPart,
        &ReturnedFileReference,
        Output->FileRecordBuffer,
        &RecordLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Output->FileReferenceNumber.QuadPart =
        (LONGLONG)ReturnedFileReference;
    Output->FileRecordLength = RecordLength;
    Irp->IoStatus.Information =
        HeaderLength + RecordLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsFlushVolume(
    _In_ PVolumeContextBlock VolCB,
    _In_ BOOLEAN Purge)
{
    PStreamContextBlock *Streams = NULL;
    PLIST_ENTRY Entry;
    ULONG StreamCount = 0;
    ULONG Captured = 0;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS DiskStatus;

    ExAcquireFastMutex(&VolCB->StreamListMutex);
    for (Entry = VolCB->StreamList.Flink;
         Entry != &VolCB->StreamList;
         Entry = Entry->Flink)
    {
        StreamCount++;
    }
    ExReleaseFastMutex(&VolCB->StreamListMutex);

    if (StreamCount)
    {
        Streams = (PStreamContextBlock*)ExAllocatePoolWithTag(
            PagedPool,
            StreamCount * sizeof(*Streams),
            TAG_NTFS);
        if (!Streams)
            return STATUS_INSUFFICIENT_RESOURCES;

        ExAcquireFastMutex(&VolCB->StreamListMutex);
        for (Entry = VolCB->StreamList.Flink;
             Entry != &VolCB->StreamList && Captured < StreamCount;
             Entry = Entry->Flink)
        {
            PStreamContextBlock StreamCB =
                CONTAINING_RECORD(Entry,
                                  StreamContextBlock,
                                  ListEntry);

            StreamCB->ReferenceCount++;
            Streams[Captured++] = StreamCB;
        }
        ExReleaseFastMutex(&VolCB->StreamListMutex);
    }

    for (Index = 0; Index < Captured; Index++)
    {
        IO_STATUS_BLOCK IoStatus;

        CcFlushCache(&Streams[Index]->SectionObjectPointers,
                     NULL,
                     0,
                     &IoStatus);
        if (!NT_SUCCESS(IoStatus.Status) &&
            NT_SUCCESS(Status))
        {
            Status = IoStatus.Status;
        }

        if (Purge &&
            !CcPurgeCacheSection(
                &Streams[Index]->SectionObjectPointers,
                NULL,
                0,
                TRUE) &&
            NT_SUCCESS(Status))
        {
            Status = STATUS_USER_MAPPED_FILE;
        }

        NtfsDereferenceStreamContext(VolCB,
                                     Streams[Index]);
    }

    if (Streams)
        ExFreePoolWithTag(Streams, TAG_NTFS);

    DiskStatus = NtfsDiskFlushKm();
    if (!NT_SUCCESS(DiskStatus) && NT_SUCCESS(Status))
        Status = DiskStatus;
    return Status;
}

static
NTSTATUS
NtfsLockOrUnlockVolume(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ BOOLEAN Lock)
{
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    PVolumeContextBlock VolCB =
        (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (!FileCB || !FileCB->IsVolumeOpen)
        return STATUS_INVALID_PARAMETER;

    if (Lock)
    {
        FsRtlNotifyVolumeEvent(FileObject,
                               FSRTL_VOLUME_LOCK);

        ExAcquireFastMutex(&VolCB->VolumeStateMutex);
        if (VolCB->Dismounted)
            Status = STATUS_VOLUME_DISMOUNTED;
        else if (VolCB->VolumeLockOwner ||
                 VolCB->OpenHandleCount != 1)
            Status = STATUS_ACCESS_DENIED;
        else
        {
            VolCB->VolumeLockOwner = FileObject;
            Status = STATUS_SUCCESS;
        }
        ExReleaseFastMutex(&VolCB->VolumeStateMutex);

        if (NT_SUCCESS(Status))
            Status = NtfsFlushVolume(VolCB, FALSE);

        ExAcquireFastMutex(&VolCB->VolumeStateMutex);
        if (NT_SUCCESS(Status))
        {
            IoAcquireVpbSpinLock(&OldIrql);
            VolumeDeviceObject->Vpb->Flags |=
                VPB_LOCKED | VPB_DIRECT_WRITES_ALLOWED;
            IoReleaseVpbSpinLock(OldIrql);
        }
        else if (VolCB->VolumeLockOwner == FileObject)
        {
            VolCB->VolumeLockOwner = NULL;
        }
        ExReleaseFastMutex(&VolCB->VolumeStateMutex);

        if (!NT_SUCCESS(Status))
        {
            FsRtlNotifyVolumeEvent(
                FileObject,
                FSRTL_VOLUME_LOCK_FAILED);
        }
        return Status;
    }

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->VolumeLockOwner != FileObject)
    {
        Status = STATUS_NOT_LOCKED;
    }
    else
    {
        VolCB->VolumeLockOwner = NULL;
        IoAcquireVpbSpinLock(&OldIrql);
        VolumeDeviceObject->Vpb->Flags &= ~VPB_LOCKED;
        if (!VolCB->Dismounted)
        {
            VolumeDeviceObject->Vpb->Flags &=
                ~VPB_DIRECT_WRITES_ALLOWED;
        }
        IoReleaseVpbSpinLock(OldIrql);
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);

    if (NT_SUCCESS(Status))
    {
        FsRtlNotifyVolumeEvent(FileObject,
                               FSRTL_VOLUME_UNLOCK);
    }
    return Status;
}

static
NTSTATUS
NtfsDismountVolume(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    PVolumeContextBlock VolCB =
        (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (!FileCB || !FileCB->IsVolumeOpen)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->Dismounted)
        Status = STATUS_VOLUME_DISMOUNTED;
    else if (VolCB->Dismounting)
        Status = STATUS_DEVICE_BUSY;
    else
    {
        VolCB->Dismounting = TRUE;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);
    if (!NT_SUCCESS(Status))
        return Status;

    FsRtlNotifyVolumeEvent(FileObject,
                           FSRTL_VOLUME_DISMOUNT);

    /*
     * Drain metadata operations that crossed the state check before the
     * dismount flag was published. Cached paging writes remain able to run
     * while CcFlushCache below commits them.
     */
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&VolCB->MetadataResource,
                                   TRUE);
    ExReleaseResourceLite(&VolCB->MetadataResource);
    KeLeaveCriticalRegion();

    Status = NtfsFlushVolume(VolCB, TRUE);

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (NT_SUCCESS(Status))
    {
        VolCB->Dismounted = TRUE;
        IoAcquireVpbSpinLock(&OldIrql);
        VolumeDeviceObject->Vpb->Flags &= ~VPB_MOUNTED;
        VolumeDeviceObject->Vpb->Flags |=
            VPB_DIRECT_WRITES_ALLOWED;
        IoReleaseVpbSpinLock(OldIrql);
    }
    VolCB->Dismounting = FALSE;
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);

    if (!NT_SUCCESS(Status))
    {
        FsRtlNotifyVolumeEvent(
            FileObject,
            FSRTL_VOLUME_DISMOUNT_FAILED);
    }
    return Status;
}

/* INCOMPLETE */
_Function_class_(IRP_MJ_FILE_SYSTEM_CONTROL)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdFileSystemControl(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                         _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handles FSCTL requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-file-system-control
     */
    NTSTATUS Status;
    BOOLEAN TopLevel;

    PAGED_CODE();
    FsRtlEnterFileSystem();
    TopLevel = NtfsIsIrpTopLevel(Irp);
    /* SEH TRY? */
    PIO_STACK_LOCATION IrpSp;

    UNREFERENCED_PARAMETER(VolumeDeviceObject);
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Irp->IoStatus.Information = 0;

    if ((IrpSp->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) &&
        (IrpSp->MinorFunction == IRP_MN_USER_FS_REQUEST) &&
        (IrpSp->Parameters.FileSystemControl.FsControlCode == FSCTL_INVALIDATE_VOLUMES))
    {
        Irp->IoStatus.Status = STATUS_UNRECOGNIZED_VOLUME;
        Status = STATUS_UNRECOGNIZED_VOLUME;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    else
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Status = STATUS_UNRECOGNIZED_VOLUME;

        switch (IrpSp->MinorFunction)
        {
            case IRP_MN_USER_FS_REQUEST:
                switch (IrpSp->Parameters.FileSystemControl.FsControlCode)
                {
                    case FSCTL_LOCK_VOLUME:
                        Irp->IoStatus.Status =
                            NtfsLockOrUnlockVolume(
                                VolumeDeviceObject,
                                IrpSp,
                                TRUE);
                        break;

                    case FSCTL_UNLOCK_VOLUME:
                        Irp->IoStatus.Status =
                            NtfsLockOrUnlockVolume(
                                VolumeDeviceObject,
                                IrpSp,
                                FALSE);
                        break;

                    case FSCTL_DISMOUNT_VOLUME:
                        Irp->IoStatus.Status =
                            NtfsDismountVolume(
                                VolumeDeviceObject,
                                IrpSp);
                        break;

                    case FSCTL_GET_REPARSE_POINT:
                        Irp->IoStatus.Status =
                            NtfsGetReparsePoint(Irp, IrpSp);
                        break;

                    case FSCTL_SET_REPARSE_POINT:
                        Irp->IoStatus.Status =
                            NtfsUpdateReparsePoint(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp,
                                FALSE);
                        break;

                    case FSCTL_DELETE_REPARSE_POINT:
                        Irp->IoStatus.Status =
                            NtfsUpdateReparsePoint(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp,
                                TRUE);
                        break;

                    case FSCTL_DELETE_EXTERNAL_BACKING:
                        Irp->IoStatus.Status =
                            NtfsDeleteExternalBacking(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp);
                        break;

                    case FSCTL_SET_SPARSE:
                        Irp->IoStatus.Status =
                            NtfsSetSparse(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp);
                        break;

                    case FSCTL_SET_ZERO_DATA:
                        Irp->IoStatus.Status =
                            NtfsSetZeroData(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp);
                        break;

                    case FSCTL_QUERY_ALLOCATED_RANGES:
                        Irp->IoStatus.Status =
                            NtfsQueryAllocatedRanges(
                                Irp,
                                IrpSp);
                        break;

                    case FSCTL_GET_RETRIEVAL_POINTERS:
                        Irp->IoStatus.Status =
                            NtfsGetRetrievalPointers(
                                Irp,
                                IrpSp);
                        break;

                    case FSCTL_GET_VOLUME_BITMAP:
                        Irp->IoStatus.Status =
                            NtfsGetVolumeBitmap(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp);
                        break;

                    case FSCTL_GET_NTFS_VOLUME_DATA:
                        Irp->IoStatus.Status =
                            NtfsGetNtfsVolumeData(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp);
                        break;

                    case FSCTL_GET_NTFS_FILE_RECORD:
                        Irp->IoStatus.Status =
                            NtfsGetNtfsFileRecord(
                                VolumeDeviceObject,
                                Irp,
                                IrpSp);
                        break;

                    default:
                        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
                        break;
                }
                Status = Irp->IoStatus.Status;
                break;

            case IRP_MN_MOUNT_VOLUME:
                Irp->IoStatus.Status = NtfsMountVolume(IrpSp->Parameters.MountVolume.DeviceObject,
                                                       IrpSp->Parameters.MountVolume.Vpb,
                                                       IrpSp->DeviceObject);
                Status = Irp->IoStatus.Status;
                break;
            case IRP_MN_VERIFY_VOLUME:
                Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            default:
                Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    if (TopLevel) { IoSetTopLevelIrp(NULL); }
    FsRtlExitFileSystem();

    return Status;
}

_Function_class_(IRP_MJ_FLUSH_BUFFERS)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdFlushBuffers(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                    _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Write all changes from buffer to disk.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-flush-buffers
     */
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!FileCB)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    /* Hold the file still so no new data lands while its pages are written. */
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FileCB->MainResource, TRUE);

    if (FileObject->PrivateCacheMap)
    {
        CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, &IoStatus);
        Status = IoStatus.Status;
    }

    /* Metadata the library is holding back has to reach the disk too. */
    if (NT_SUCCESS(Status))
        Status = NtfsDiskFlushKm();

    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();

Complete:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
