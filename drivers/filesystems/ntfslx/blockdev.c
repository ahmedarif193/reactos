/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Synchronous block-device helpers
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

/*
 * Signal our private event when the downstream disk IRP completes. Registered
 * as the IRP completion routine for NtfslxReadDisk so that synchronous paging
 * I/O (which runs at APC_LEVEL with kernel APCs disabled) can still wake up:
 * IoBuildSynchronousFsdRequest's native completion path uses a kernel APC,
 * which never delivers at APC_LEVEL and leads to an indefinite wait.
 */
static
NTSTATUS
NTAPI
NtfslxReadDiskCompletion(
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
NtfslxReadDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ LONGLONG StartingOffset,
    _In_ ULONG Length,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ BOOLEAN Override)
{
    PIO_STACK_LOCATION Stack;
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
    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ,
                                        DeviceObject,
                                        ReadBuffer,
                                        RealLength,
                                        &Offset,
                                        NULL);
    if (Irp == NULL)
    {
        if (AllocatedBuffer)
        {
            ExFreePoolWithTag(ReadBuffer, NTFSLX_TAG);
        }

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->UserIosb = &Irp->IoStatus;
    Irp->Flags |= IRP_SYNCHRONOUS_PAGING_IO;

    IoSetCompletionRoutine(Irp,
                           NtfslxReadDiskCompletion,
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
     * short-circuiting the I/O manager's normal teardown. We are
     * responsible for unlocking and freeing the MDL we built, and the
     * IRP itself.
     */
    if (Irp->MdlAddress != NULL)
    {
        MmUnlockPages(Irp->MdlAddress);
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NULL;
    }
    IoFreeIrp(Irp);

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
