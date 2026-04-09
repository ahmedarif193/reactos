/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Synchronous block-device helpers
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NtfslxReadDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ LONGLONG StartingOffset,
    _In_ ULONG Length,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ BOOLEAN Override)
{
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    ULONGLONG RealReadOffset;
    ULONG RealLength;
    PUCHAR ReadBuffer;
    BOOLEAN AllocatedBuffer;

    ReadBuffer = Buffer;
    AllocatedBuffer = FALSE;
    RealReadOffset = (ULONGLONG)StartingOffset;
    RealLength = Length;

    if ((RealReadOffset % SectorSize) != 0 || (RealLength % SectorSize) != 0)
    {
        RealReadOffset = ROUND_DOWN(RealReadOffset, SectorSize);
        RealLength = ROUND_UP(Length + (ULONG)(StartingOffset - RealReadOffset), SectorSize);

        ReadBuffer = ExAllocatePoolWithTag(NonPagedPool, RealLength, NTFSLX_TAG);
        if (ReadBuffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        AllocatedBuffer = TRUE;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Offset.QuadPart = RealReadOffset;
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
                                       DeviceObject,
                                       ReadBuffer,
                                       RealLength,
                                       &Offset,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL)
    {
        if (AllocatedBuffer)
        {
            ExFreePoolWithTag(ReadBuffer, NTFSLX_TAG);
        }

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
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (AllocatedBuffer)
    {
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(Buffer,
                          ReadBuffer + (ULONG)(StartingOffset - RealReadOffset),
                          Length);
        }

        ExFreePoolWithTag(ReadBuffer, NTFSLX_TAG);
    }

    return Status;
}

NTSTATUS
NtfslxDeviceIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(*OutputBufferSize, *OutputBufferSize) PVOID OutputBuffer,
    _Inout_opt_ PULONG OutputBufferSize,
    _In_ BOOLEAN Override)
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
                                        OutputBufferSize ? *OutputBufferSize : 0,
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
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (OutputBufferSize != NULL)
    {
        *OutputBufferSize = (ULONG)IoStatus.Information;
    }

    return Status;
}
