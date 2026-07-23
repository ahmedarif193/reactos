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
        case ENOTDIR:
            return STATUS_NOT_A_DIRECTORY;
        case EISDIR:
            return STATUS_FILE_IS_A_DIRECTORY;
        case ENAMETOOLONG:
            return STATUS_NAME_TOO_LONG;
        case EILSEQ:
            return STATUS_OBJECT_NAME_INVALID;
        case EINVAL:
            return STATUS_INVALID_PARAMETER;
        case EACCES:
        case EROFS:
            return STATUS_MEDIA_WRITE_PROTECTED;
        case EOPNOTSUPP:
            return STATUS_NOT_SUPPORTED;
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
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    ByteOffset.QuadPart = Offset;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
                                       Context->DeviceObject,
                                       Context->ReadBuffer,
                                       Length,
                                       &ByteOffset,
                                       &Event,
                                       &IoStatus);
    if (!Irp)
        return ENOMEM;
    Status = Ntfs3gSubmitSynchronousIrp(Context->DeviceObject, Irp,
                                       &Event, &IoStatus);
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
                                   PAGE_SIZE - SectorOffset);
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

static void
Ntfs3gKernelClose(void *OpaqueContext)
{
    NTFS3G_KERNEL_DEVICE *Context = OpaqueContext;

    MmFreeContiguousMemory(Context->ReadBuffer);
    ObDereferenceObject(Context->DeviceObject);
    Ntfs3gRosHostFree(Context);
}

static const NTFS3G_ROS_DEVICE_OPERATIONS Ntfs3gKernelDeviceOperations = {
    Ntfs3gKernelRead,
    Ntfs3gKernelClose
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
    if (!ReadOnly)
        return STATUS_NOT_SUPPORTED;
    if (!InterlockedCompareExchange(&Ntfs3gKernelRuntimeInitialized, 1, 1))
        return STATUS_INVALID_DEVICE_STATE;

    Context = Ntfs3gRosHostAllocate(sizeof(*Context));
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    HighestAddress.QuadPart = -1;
    Context->ReadBuffer = MmAllocateContiguousMemory(PAGE_SIZE, HighestAddress);
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

    Result = Ntfs3gRosMount(Context, &Ntfs3gKernelDeviceOperations,
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
