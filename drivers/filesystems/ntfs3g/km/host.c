/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel callbacks for the shared NTFS-3G core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ntdddisk.h>

#include <errno.h>
#include <stdint.h>

#undef STATUS_NOT_FOUND
#include "host.h"
#include "ntfs3g_ros_km.h"

#define NTFS3G_POOL_TAG 'G3TN'
#define NTFS3G_IO_BUFFER_SIZE (256 * 1024)

typedef struct _NTFS3G_KERNEL_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PVOID ReadBuffer;
    uint32_t SectorSize;
} NTFS3G_KERNEL_DEVICE;

static ERESOURCE Ntfs3gKernelRuntimeLock;
static LONG Ntfs3gKernelRuntimeInitialized;

NTSTATUS
Ntfs3gRosStatusFromError(int Error)
{
    switch (Error) {
        case ENOMEM:
            return STATUS_INSUFFICIENT_RESOURCES;
        case ENOENT:
            return STATUS_OBJECT_NAME_NOT_FOUND;
        case EEXIST:
            return STATUS_OBJECT_NAME_COLLISION;
        case EXDEV:
            return STATUS_NOT_SAME_DEVICE;
        case ENOTDIR:
            return STATUS_NOT_A_DIRECTORY;
        case EISDIR:
            return STATUS_FILE_IS_A_DIRECTORY;
        case ENOTEMPTY:
            return STATUS_DIRECTORY_NOT_EMPTY;
        case ENAMETOOLONG:
            return STATUS_NAME_TOO_LONG;
        case ENODATA:
            return STATUS_NO_EAS_ON_FILE;
        case ERANGE:
            return STATUS_BUFFER_TOO_SMALL;
        case ENOSPC:
            return STATUS_DISK_FULL;
#ifdef EFBIG
        case EFBIG:
            return STATUS_FILE_TOO_LARGE;
#endif
        case EILSEQ:
            return STATUS_OBJECT_NAME_INVALID;
        case EINVAL:
            return STATUS_INVALID_PARAMETER;
        case EACCES:
        case EROFS:
            return STATUS_MEDIA_WRITE_PROTECTED;
        case EOPNOTSUPP:
            return STATUS_NOT_SUPPORTED;
        case EBUSY:
        case EPERM:
            return STATUS_CANNOT_DELETE;
        case EIO:
            return STATUS_IO_DEVICE_ERROR;
        case ENODEV:
            return STATUS_NO_SUCH_DEVICE;
        default:
            return STATUS_UNRECOGNIZED_VOLUME;
    }
}

static int
Ntfs3gStatusToErrno(NTSTATUS Status)
{
    if (Status == STATUS_INSUFFICIENT_RESOURCES)
        return ENOMEM;
    if (Status == STATUS_ACCESS_DENIED ||
        Status == STATUS_MEDIA_WRITE_PROTECTED)
        return EROFS;
    if (Status == STATUS_INVALID_PARAMETER)
        return EINVAL;
    return EIO;
}

void *
Ntfs3gRosHostAllocate(size_t Size)
{
    return ExAllocatePoolWithTag(PagedPool, Size, NTFS3G_POOL_TAG);
}

void
Ntfs3gRosHostFree(void *Buffer)
{
    if (Buffer)
        ExFreePoolWithTag(Buffer, NTFS3G_POOL_TAG);
}

void
Ntfs3gRosHostAcquire(void)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Ntfs3gKernelRuntimeLock, TRUE);
}

void
Ntfs3gRosHostRelease(void)
{
    ExReleaseResourceLite(&Ntfs3gKernelRuntimeLock);
    KeLeaveCriticalRegion();
}

int64_t
Ntfs3gRosHostGetTime(void)
{
    LARGE_INTEGER SystemTime;
    ULONG Seconds;

    KeQuerySystemTime(&SystemTime);
    if (!RtlTimeToSecondsSince1970(&SystemTime, &Seconds))
        return 0;
    return Seconds;
}

void
Ntfs3gRosHostLog(int IsError,
                 const char *Message)
{
    DbgPrintEx(DPFLTR_NTFS_ID,
               IsError ? DPFLTR_ERROR_LEVEL : DPFLTR_INFO_LEVEL,
               "NTFS3G: %s",
               Message);
}

static NTSTATUS
Ntfs3gSubmitSynchronousIrp(PDEVICE_OBJECT DeviceObject,
                           PIRP Irp,
                           PKEVENT Event,
                           PIO_STATUS_BLOCK IoStatus)
{
    NTSTATUS Status;

    IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus->Status;
    } else if (NT_SUCCESS(Status)) {
        Status = IoStatus->Status;
    }
    return Status;
}

static NTSTATUS
NTAPI
Ntfs3gOwnedIrpCompletion(PDEVICE_OBJECT DeviceObject,
                         PIRP Irp,
                         PVOID Context)
{
    PKEVENT Event = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * Cache Manager can issue paging writes while holding a guarded mutex.
 * Such a caller cannot receive the completion APC used by
 * IoBuildSynchronousFsdRequest.  Stop completion in our own routine, signal
 * an event directly, and release the IRP after the lower stack is finished.
 */
static NTSTATUS
Ntfs3gSubmitOwnedIrp(PDEVICE_OBJECT DeviceObject,
                     PIRP Irp,
                     PIO_STATUS_BLOCK IoStatus)
{
    PMDL Mdl;
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp,
                           Ntfs3gOwnedIrpCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);
    IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING || !KeReadStateEvent(&Event))
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    *IoStatus = Irp->IoStatus;
    Status = IoStatus->Status;
    if ((Irp->Flags & (IRP_BUFFERED_IO | IRP_INPUT_OPERATION)) ==
        (IRP_BUFFERED_IO | IRP_INPUT_OPERATION) &&
        !NT_ERROR(Status) &&
        Irp->UserBuffer &&
        Irp->AssociatedIrp.SystemBuffer) {
        RtlCopyMemory(Irp->UserBuffer,
                      Irp->AssociatedIrp.SystemBuffer,
                      IoStatus->Information);
    }
    if ((Irp->Flags & IRP_DEALLOCATE_BUFFER) &&
        Irp->AssociatedIrp.SystemBuffer)
        ExFreePool(Irp->AssociatedIrp.SystemBuffer);
    while ((Mdl = Irp->MdlAddress)) {
        Irp->MdlAddress = Mdl->Next;
        /*
         * Returning STATUS_MORE_PROCESSING_REQUIRED prevents
         * IofCompleteRequest from reaching its normal MDL-unlock pass.
         * We own the stopped IRP, so mirror that cleanup before freeing
         * each MDL.
         */
        if (Mdl->MdlFlags & MDL_PAGES_LOCKED)
            MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }
    IoFreeIrp(Irp);
    return Status;
}

static NTSTATUS
Ntfs3gDeviceControl(PDEVICE_OBJECT DeviceObject,
                    ULONG ControlCode,
                    void *OutputBuffer,
                    ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(ControlCode, DeviceObject, NULL, 0,
                                        OutputBuffer, OutputLength, FALSE,
                                        &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    return Ntfs3gSubmitSynchronousIrp(DeviceObject, Irp, &Event, &IoStatus);
}

static int
Ntfs3gKernelReadChunk(NTFS3G_KERNEL_DEVICE *Context,
                      uint64_t Offset,
                      uint32_t Length,
                      uint32_t *BytesRead)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER ByteOffset;
    PIRP Irp;
    NTSTATUS Status;

    ByteOffset.QuadPart = Offset;
    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ,
                                        Context->DeviceObject,
                                        Context->ReadBuffer,
                                        Length,
                                        &ByteOffset,
                                        &IoStatus);
    if (!Irp)
        return ENOMEM;
    Status = Ntfs3gSubmitOwnedIrp(Context->DeviceObject, Irp, &IoStatus);
    if (!NT_SUCCESS(Status))
        return Ntfs3gStatusToErrno(Status);
    *BytesRead = (uint32_t)IoStatus.Information;
    return 0;
}

static int
Ntfs3gKernelRead(void *OpaqueContext,
                 uint64_t Offset,
                 void *Buffer,
                 uint32_t Length,
                 uint32_t *BytesRead)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;
    uint8_t *Destination = Buffer;

    if (Offset > INT64_MAX || Length > INT64_MAX - Offset)
        return EINVAL;

    *BytesRead = 0;
    while (*BytesRead < Length) {
        uint64_t Position = Offset + *BytesRead;
        uint32_t SectorOffset = (uint32_t)(Position & (Context->SectorSize - 1));
        uint32_t ChunkLength = min(Length - *BytesRead,
                                   NTFS3G_IO_BUFFER_SIZE - SectorOffset);
        uint32_t ReadLength = ALIGN_UP_BY(SectorOffset + ChunkLength,
                                          Context->SectorSize);
        uint32_t ChunkRead;
        uint32_t CopyLength;
        int Error;

        Error = Ntfs3gKernelReadChunk(Context, Position - SectorOffset,
                                      ReadLength, &ChunkRead);
        if (Error)
            return Error;
        if (ChunkRead <= SectorOffset)
            break;
        CopyLength = min(ChunkLength, ChunkRead - SectorOffset);
        RtlCopyMemory(Destination + *BytesRead,
                      (uint8_t *)Context->ReadBuffer + SectorOffset,
                      CopyLength);
        *BytesRead += CopyLength;
        if (CopyLength != ChunkLength)
            break;
    }
    return 0;
}

static int
Ntfs3gKernelWriteChunk(NTFS3G_KERNEL_DEVICE *Context,
                       uint64_t Offset,
                       uint32_t Length)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER ByteOffset;
    PIRP Irp;
    NTSTATUS Status;

    ByteOffset.QuadPart = Offset;
    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_WRITE,
                                        Context->DeviceObject,
                                        Context->ReadBuffer,
                                        Length,
                                        &ByteOffset,
                                        &IoStatus);
    if (!Irp)
        return ENOMEM;
    Status = Ntfs3gSubmitOwnedIrp(Context->DeviceObject, Irp, &IoStatus);
    if (!NT_SUCCESS(Status))
        return Ntfs3gStatusToErrno(Status);
    return IoStatus.Information == Length ? 0 : EIO;
}

static int
Ntfs3gKernelWrite(void *OpaqueContext,
                  uint64_t Offset,
                  const void *Buffer,
                  uint32_t Length,
                  uint32_t *BytesWritten)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;
    const uint8_t *Source = Buffer;

    if (Offset > INT64_MAX || Length > INT64_MAX - Offset)
        return EINVAL;

    *BytesWritten = 0;
    while (*BytesWritten < Length) {
        uint64_t Position = Offset + *BytesWritten;
        uint32_t SectorOffset = (uint32_t)(Position & (Context->SectorSize - 1));
        uint32_t ChunkLength = min(Length - *BytesWritten,
                                   NTFS3G_IO_BUFFER_SIZE - SectorOffset);
        uint32_t WriteLength = ALIGN_UP_BY(SectorOffset + ChunkLength,
                                           Context->SectorSize);
        int Error;

        if (SectorOffset || ChunkLength != WriteLength) {
            uint32_t ReadLength;

            Error = Ntfs3gKernelReadChunk(Context, Position - SectorOffset,
                                          WriteLength, &ReadLength);
            if (Error)
                return Error;
            if (ReadLength != WriteLength)
                return EIO;
        }
        RtlCopyMemory((uint8_t *)Context->ReadBuffer + SectorOffset,
                      Source + *BytesWritten,
                      ChunkLength);
        Error = Ntfs3gKernelWriteChunk(Context, Position - SectorOffset,
                                       WriteLength);
        if (Error)
            return Error;
        *BytesWritten += ChunkLength;
    }
    return 0;
}

static int
Ntfs3gKernelSync(void *OpaqueContext)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    NTSTATUS Status;

    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_FLUSH_BUFFERS,
                                        Context->DeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        &IoStatus);
    if (!Irp)
        return ENOMEM;
    Status = Ntfs3gSubmitOwnedIrp(Context->DeviceObject, Irp, &IoStatus);
    return NT_SUCCESS(Status) ? 0 : Ntfs3gStatusToErrno(Status);
}

static void
Ntfs3gKernelClose(void *OpaqueContext)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;

    MmFreeContiguousMemory(Context->ReadBuffer);
    ObDereferenceObject(Context->DeviceObject);
    Ntfs3gRosHostFree(Context);
}

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gKernelReadOnlyOperations = {
    Ntfs3gKernelRead,
    Ntfs3gKernelClose,
    NULL,
    NULL
};

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gKernelReadWriteOperations = {
    Ntfs3gKernelRead,
    Ntfs3gKernelClose,
    Ntfs3gKernelWrite,
    Ntfs3gKernelSync
};

NTSTATUS
Ntfs3gRosInitializeKernelLibrary(void)
{
    NTSTATUS Status;

    Status = ExInitializeResourceLite(&Ntfs3gKernelRuntimeLock);
    if (!NT_SUCCESS(Status))
        return Status;
    InterlockedExchange(&Ntfs3gKernelRuntimeInitialized, 1);
    return STATUS_SUCCESS;
}

void
Ntfs3gRosUninitializeKernelLibrary(void)
{
    if (InterlockedExchange(&Ntfs3gKernelRuntimeInitialized, 0))
        ExDeleteResourceLite(&Ntfs3gKernelRuntimeLock);
}

NTSTATUS
Ntfs3gRosMountDevice(PDEVICE_OBJECT DeviceObject,
                     int ReadOnly,
                     PNTFS3G_ROS_KM_VOLUME *Volume)
{
    NTFS3G_KERNEL_DEVICE *Context;
    GET_LENGTH_INFORMATION Length;
    DISK_GEOMETRY Geometry;
    PHYSICAL_ADDRESS HighestAddress;
    uint64_t DeviceLength = 0;
    uint32_t SectorSize = 512;
    int Result;

    if (!DeviceObject || !Volume)
        return STATUS_INVALID_PARAMETER;
    if (!InterlockedCompareExchange(&Ntfs3gKernelRuntimeInitialized, 1, 1))
        return STATUS_INVALID_DEVICE_STATE;

    Context = Ntfs3gRosHostAllocate(sizeof(*Context));
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    HighestAddress.QuadPart = -1;
    Context->ReadBuffer = MmAllocateContiguousMemory(NTFS3G_IO_BUFFER_SIZE,
                                                     HighestAddress);
    if (!Context->ReadBuffer) {
        Ntfs3gRosHostFree(Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Context->DeviceObject = DeviceObject;
    ObReferenceObject(DeviceObject);

    if (NT_SUCCESS(Ntfs3gDeviceControl(DeviceObject,
                                       IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                       &Geometry, sizeof(Geometry))))
        SectorSize = Geometry.BytesPerSector;
    if (NT_SUCCESS(Ntfs3gDeviceControl(DeviceObject,
                                       IOCTL_DISK_GET_LENGTH_INFO,
                                       &Length, sizeof(Length))))
        DeviceLength = Length.Length.QuadPart;
    Context->SectorSize = SectorSize;

    Result = Ntfs3gRosMount(Context,
                            ReadOnly ? &Ntfs3gKernelReadOnlyOperations :
                                       &Ntfs3gKernelReadWriteOperations,
                            DeviceLength, SectorSize, Volume);
    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    return STATUS_SUCCESS;
}

NTSTATUS
Ntfs3gRosUnmountDevice(PNTFS3G_ROS_KM_VOLUME Volume)
{
    int Result = Ntfs3gRosUnmount(Volume);

    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    return STATUS_SUCCESS;
}

NTSTATUS
Ntfs3gRosOpenUnicodeFile(PNTFS3G_ROS_KM_VOLUME Volume,
                         PCUNICODE_STRING Path,
                         NTFS3G_ROS_FILE **File)
{
    int Result;

    if (!Volume || !Path || !File)
        return STATUS_INVALID_PARAMETER;
    Result = Ntfs3gRosOpenFileUtf16(Volume,
                                    (const uint16_t *)Path->Buffer,
                                    Path->Length / sizeof(WCHAR),
                                    File);
    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    return STATUS_SUCCESS;
}
