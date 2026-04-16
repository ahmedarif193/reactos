/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Synchronous disk write helper
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NtfslxWriteDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ LONGLONG StartingOffset,
    _In_ ULONG Length,
    _In_ ULONG SectorSize,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ BOOLEAN Override)
{
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    ULONGLONG RealWriteOffset;
    ULONG RealLength;
    PUCHAR WriteBuffer;
    BOOLEAN AllocatedBuffer;

    if (DeviceObject == NULL || Buffer == NULL || SectorSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NtfslxIsPowerOfTwo(SectorSize))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    if (StartingOffset < 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    WriteBuffer = Buffer;
    AllocatedBuffer = FALSE;
    RealWriteOffset = (ULONGLONG)StartingOffset;
    RealLength = Length;

    if ((RealWriteOffset % SectorSize) != 0 || (RealLength % SectorSize) != 0)
    {
        ULONGLONG AlignedStart;
        ULONGLONG AlignedEnd;

        AlignedStart = ROUND_DOWN(RealWriteOffset, SectorSize);
        AlignedEnd = ROUND_UP(RealWriteOffset + Length, SectorSize);
        if (AlignedEnd < AlignedStart || (AlignedEnd - AlignedStart) > MAXULONG)
        {
            return STATUS_INVALID_BUFFER_SIZE;
        }

        RealLength = (ULONG)(AlignedEnd - AlignedStart);
        WriteBuffer = ExAllocatePoolWithTag(NonPagedPool, RealLength, NTFSLX_TAG);
        if (WriteBuffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Read-modify-write: read existing sector data first */
        Status = NtfslxReadDisk(DeviceObject,
                                (LONGLONG)AlignedStart,
                                RealLength,
                                SectorSize,
                                WriteBuffer,
                                Override);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(WriteBuffer, NTFSLX_TAG);
            return Status;
        }

        RtlCopyMemory(WriteBuffer + (ULONG)(RealWriteOffset - AlignedStart),
                      Buffer,
                      Length);
        RealWriteOffset = AlignedStart;
        AllocatedBuffer = TRUE;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Offset.QuadPart = (LONGLONG)RealWriteOffset;
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE,
                                       DeviceObject,
                                       WriteBuffer,
                                       RealLength,
                                       &Offset,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL)
    {
        if (AllocatedBuffer)
        {
            ExFreePoolWithTag(WriteBuffer, NTFSLX_TAG);
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
        ExFreePoolWithTag(WriteBuffer, NTFSLX_TAG);
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (IoStatus.Information != RealLength)
    {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}
