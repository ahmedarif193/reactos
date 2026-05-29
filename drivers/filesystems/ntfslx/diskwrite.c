/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Disk write helper.
 *
 *              Uses an async IRP + custom completion routine signalled
 *              via KEVENT (mirroring what NtfslxReadDisk does in
 *              blockdev.c) so the wait is safe even when the caller
 *              holds a FAST_MUTEX or KGUARDED_MUTEX.
 *              IoBuildSynchronousFsdRequest's native completion path
 *              uses a kernel APC into the originating thread, which is
 *              NEVER delivered while either FAST_MUTEX or
 *              KGUARDED_MUTEX is held (both call KeEnterCriticalRegion,
 *              which disables special kernel APCs). After T1.5.2 the
 *              production allocator code no longer holds MftAllocLock
 *              across the disk writes, so this APC-safe property is
 *              defence-in-depth — the disk-IO helpers must never
 *              regress to APC-based completion regardless of whether
 *              the caller is currently under a guarded lock.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
NTAPI
NtfslxWriteDiskCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

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
    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_WRITE,
                                        DeviceObject,
                                        WriteBuffer,
                                        RealLength,
                                        &Offset,
                                        NULL);
    if (Irp == NULL)
    {
        if (AllocatedBuffer)
        {
            ExFreePoolWithTag(WriteBuffer, NTFSLX_TAG);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->UserIosb = &Irp->IoStatus;
    Irp->Flags |= IRP_SYNCHRONOUS_PAGING_IO;

    IoSetCompletionRoutine(Irp,
                           NtfslxWriteDiskCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    if (Override)
    {
        Stack = IoGetNextIrpStackLocation(Irp);
        Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    }

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
    Status = Irp->IoStatus.Status;

    /*
     * Our completion routine returned STATUS_MORE_PROCESSING_REQUIRED,
     * short-circuiting the I/O manager's normal teardown. Unlock and
     * free the MDL we built, and the IRP itself.
     */
    if (Irp->MdlAddress != NULL)
    {
        MmUnlockPages(Irp->MdlAddress);
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NULL;
    }

    if (NT_SUCCESS(Status) && Irp->IoStatus.Information != RealLength)
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    IoFreeIrp(Irp);

    if (AllocatedBuffer)
    {
        ExFreePoolWithTag(WriteBuffer, NTFSLX_TAG);
    }

    return Status;
}
