#include "partmgr.h"

static NTSTATUS NTAPI
PartMgrForwardCompletion(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    PFDO_EXTENSION Extension = Context;
    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);
    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
NTAPI
ForwardIrpAndForget(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    // this part of a structure is identical in both FDO and PDO
    PFDO_EXTENSION Extension = DeviceObject->DeviceExtension;
    BOOLEAN Power = IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_POWER;
    NTSTATUS Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return PartMgrFailIrp(Irp, Status);

    /* The dispatch lease protects setup; this lease survives STATUS_PENDING. */
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, PartMgrForwardCompletion, Extension, TRUE, TRUE, TRUE);
    if (Power)
        return PoCallDriver(Extension->LowerDevice, Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

NTSTATUS
IssueSyncIoControlRequest(
    _In_ UINT32 IoControlCode,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _In_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ BOOLEAN InternalDeviceIoControl)
{
    PIRP Irp;
    IO_STATUS_BLOCK IoStatusBlock;
    PKEVENT Event;
    NTSTATUS Status;
    PAGED_CODE();

    /* Allocate a non-paged event */
    Event = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Event), TAG_PARTMGR);
    if (!Event)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize it */
    KeInitializeEvent(Event, NotificationEvent, FALSE);

    /* Build the IRP */
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        InputBuffer,
                                        InputBufferLength,
                                        OutputBuffer,
                                        OutputBufferLength,
                                        InternalDeviceIoControl,
                                        Event,
                                        &IoStatusBlock);
    if (!Irp)
    {
        /* Fail, free the event */
        ExFreePoolWithTag(Event, TAG_PARTMGR);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Call the driver and check if it's pending */
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        /* Wait on the driver */
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    /* Free the event and return the Status */
    ExFreePoolWithTag(Event, TAG_PARTMGR);
    return Status;
}
