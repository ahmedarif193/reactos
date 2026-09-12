/*
 * PROJECT:     ReactOS Universal Serial Bus Bulk Storage Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     USB block storage device driver.
 * COPYRIGHT:   2005-2006 James Tabor
 *              2011-2012 Michael Martin (michael.martin@reactos.org)
 *              2011-2013 Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "usbstor.h"

#define NDEBUG
#include <debug.h>


VOID
USBSTOR_QueueInitialize(
    PFDO_DEVICE_EXTENSION FDODeviceExtension)
{
    ASSERT(FDODeviceExtension->Common.IsFDO);
    KeInitializeSpinLock(&FDODeviceExtension->IrpListLock);
    InitializeListHead(&FDODeviceExtension->IrpListHead);
    KeInitializeEvent(&FDODeviceExtension->NoPendingRequests, NotificationEvent, TRUE);
}

VOID
NTAPI
USBSTOR_Cancel(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PPDO_DEVICE_EXTENSION PDODeviceExtension = DeviceObject->DeviceExtension;

    /* IoCancelIrp passes the PDO from the current SCSI stack location. */
    ASSERT(!PDODeviceExtension->Common.IsFDO);
    DeviceObject = PDODeviceExtension->LowerDeviceObject;
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT_IRQL_EQUAL(DISPATCH_LEVEL);
    ASSERT(FDODeviceExtension->Common.IsFDO);

    KeAcquireSpinLockAtDpcLevel(&FDODeviceExtension->IrpListLock);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->IrpListLock);

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb->SrbStatus = SRB_STATUS_ABORTED;

    USBSTOR_QueueTerminateRequest(DeviceObject, Irp, FALSE);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    USBSTOR_QueueNextRequest(DeviceObject);
}

BOOLEAN
USBSTOR_QueueAddIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    KIRQL OldLevel;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    BOOLEAN Queued;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PSCSI_REQUEST_BLOCK Request = IoStack->Parameters.Scsi.Srb;

    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);
    IoMarkIrpPending(Irp);

    /* Publish queued IRPs and their cancellation routine under both locks. */
    IoAcquireCancelSpinLock(&OldLevel);
    if (Irp->Cancel)
    {
        IoReleaseCancelSpinLock(OldLevel);
        Request->SrbStatus = SRB_STATUS_ABORTED;
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return TRUE;
    }

    KeAcquireSpinLockAtDpcLevel(&FDODeviceExtension->IrpListLock);
    Queued = FDODeviceExtension->IrpPendingCount != 0 ||
             (FDODeviceExtension->Flags & (USBSTOR_FDO_FLAGS_IRP_LIST_FREEZE |
                                           USBSTOR_FDO_FLAGS_DEVICE_RESETTING |
                                           USBSTOR_FDO_FLAGS_RESET_REQUIRED)) != 0;
    if (Queued)
    {
        InsertTailList(&FDODeviceExtension->IrpListHead, &Irp->Tail.Overlay.ListEntry);
        IoSetCancelRoutine(Irp, USBSTOR_Cancel);
    }
    else
    {
        ASSERT(FDODeviceExtension->ActiveSrb == NULL);
        FDODeviceExtension->ActiveSrb = Request;
        /* StartIo checks cancellation before submitting the request. Until
         * then, cancellation must not free the reserved active packet. */
        IoSetCancelRoutine(Irp, NULL);
    }

    FDODeviceExtension->IrpPendingCount++;
    KeClearEvent(&FDODeviceExtension->NoPendingRequests);
    KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->IrpListLock);
    IoReleaseCancelSpinLock(OldLevel);
    if (Queued)
        USBSTOR_QueueNextRequest(DeviceObject);
    return Queued;
}

VOID
USBSTOR_QueueWaitForPendingRequests(
    IN PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    KeWaitForSingleObject(&FDODeviceExtension->NoPendingRequests,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
}

VOID
USBSTOR_QueueTerminateRequest(
    IN PDEVICE_OBJECT FDODeviceObject,
    IN PIRP Irp,
    IN BOOLEAN ResetDevice)
{
    KIRQL OldLevel;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PSCSI_REQUEST_BLOCK Request = (PSCSI_REQUEST_BLOCK)IoStack->Parameters.Others.Argument1;

    DPRINT("USBSTOR_QueueTerminateRequest: Irp=%p Srb=%p IoStatus=0x%lx\n",
           Irp, Request, Irp->IoStatus.Status);

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)FDODeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    KeAcquireSpinLock(&FDODeviceExtension->IrpListLock, &OldLevel);

    ASSERT(FDODeviceExtension->IrpPendingCount != 0);
    FDODeviceExtension->IrpPendingCount--;
    if (ResetDevice)
        FDODeviceExtension->Flags |= USBSTOR_FDO_FLAGS_DEVICE_RESETTING;

    // check if this was our current active SRB
    if (FDODeviceExtension->ActiveSrb == Request)
    {
        /* Only the active request is submitted to the I/O manager. Release
         * its empty device queue before publishing ActiveSrb == NULL, so a
         * concurrent retry cannot get stuck behind the completed packet. */
        ASSERT(IsListEmpty(&FDODeviceObject->DeviceQueue.DeviceListHead));
        IoStartNextPacket(FDODeviceObject, FALSE);
        FDODeviceExtension->ActiveSrb = NULL;
    }

    // Set the event if nothing else is pending
    if (FDODeviceExtension->IrpPendingCount == 0 &&
        FDODeviceExtension->ActiveSrb == NULL)
    {
        KeSetEvent(&FDODeviceExtension->NoPendingRequests, IO_NO_INCREMENT, FALSE);
    }

    KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);
}

VOID
USBSTOR_QueueEndReset(
    IN PDEVICE_OBJECT DeviceObject,
    IN NTSTATUS Status)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = DeviceObject->DeviceExtension;
    LIST_ENTRY FailedIrps;
    PLIST_ENTRY Entry;
    PIRP Irp;
    PSCSI_REQUEST_BLOCK Request;
    KIRQL OldLevel;

    InitializeListHead(&FailedIrps);
    IoAcquireCancelSpinLock(&OldLevel);
    KeAcquireSpinLockAtDpcLevel(&FDODeviceExtension->IrpListLock);
    ASSERT(FDODeviceExtension->ActiveSrb == NULL);
    FDODeviceExtension->Flags &= ~USBSTOR_FDO_FLAGS_DEVICE_RESETTING;
    if (NT_SUCCESS(Status))
    {
        FDODeviceExtension->Flags &= ~USBSTOR_FDO_FLAGS_RESET_REQUIRED;
    }
    else
    {
        /* A failed class reset or clear-stall leaves the BOT transport out
         * of sync. Complete waiting requests without sending another CBW;
         * a later request must retry recovery before using the bulk pipes. */
        FDODeviceExtension->Flags |= USBSTOR_FDO_FLAGS_RESET_REQUIRED;
        while (!IsListEmpty(&FDODeviceExtension->IrpListHead))
        {
            Entry = RemoveHeadList(&FDODeviceExtension->IrpListHead);
            Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
            IoSetCancelRoutine(Irp, NULL);
            InsertTailList(&FailedIrps, Entry);
            ASSERT(FDODeviceExtension->IrpPendingCount != 0);
            FDODeviceExtension->IrpPendingCount--;
        }
        if (FDODeviceExtension->IrpPendingCount == 0)
            KeSetEvent(&FDODeviceExtension->NoPendingRequests, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->IrpListLock);
    IoReleaseCancelSpinLock(OldLevel);

    while (!IsListEmpty(&FailedIrps))
    {
        Entry = RemoveHeadList(&FailedIrps);
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        Request = IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb;
        Request->SrbStatus = SRB_STATUS_INTERNAL_ERROR;
        Request->InternalStatus = Status;
        Request->DataTransferLength = 0;
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    USBSTOR_QueueNextRequest(DeviceObject);
}

VOID
USBSTOR_QueueNextRequest(
    IN PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = DeviceObject->DeviceExtension;
    PIRP Irp;
    PSCSI_REQUEST_BLOCK Request;
    PLIST_ENTRY Entry;
    KIRQL OldLevel;

    ASSERT(FDODeviceExtension->Common.IsFDO);
    IoAcquireCancelSpinLock(&OldLevel);
    KeAcquireSpinLockAtDpcLevel(&FDODeviceExtension->IrpListLock);

    if (FDODeviceExtension->ActiveSrb != NULL ||
        (FDODeviceExtension->Flags & (USBSTOR_FDO_FLAGS_IRP_LIST_FREEZE |
                                      USBSTOR_FDO_FLAGS_DEVICE_RESETTING)) != 0 ||
        IsListEmpty(&FDODeviceExtension->IrpListHead))
    {
        KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->IrpListLock);
        IoReleaseCancelSpinLock(OldLevel);
        return;
    }

    if (FDODeviceExtension->Flags & USBSTOR_FDO_FLAGS_RESET_REQUIRED)
    {
        FDODeviceExtension->Flags |= USBSTOR_FDO_FLAGS_DEVICE_RESETTING;
        KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->IrpListLock);
        IoReleaseCancelSpinLock(OldLevel);
        USBSTOR_QueueResetDevice(FDODeviceExtension);
        return;
    }

    Entry = RemoveHeadList(&FDODeviceExtension->IrpListHead);
    Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
    Request = IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb;
    ASSERT(Request);
    IoSetCancelRoutine(Irp, NULL);
    FDODeviceExtension->ActiveSrb = Request;
    KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->IrpListLock);
    IoReleaseCancelSpinLock(OldLevel);

    IoStartPacket(DeviceObject, Irp, &Request->QueueSortKey, NULL);
}

VOID
USBSTOR_QueueRelease(
    IN PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension = DeviceObject->DeviceExtension;
    KIRQL OldLevel;

    ASSERT(FDODeviceExtension->Common.IsFDO);
    KeAcquireSpinLock(&FDODeviceExtension->IrpListLock, &OldLevel);
    FDODeviceExtension->Flags &= ~USBSTOR_FDO_FLAGS_IRP_LIST_FREEZE;
    KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);
    USBSTOR_QueueNextRequest(DeviceObject);
}

VOID
NTAPI
USBSTOR_StartIo(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PSCSI_REQUEST_BLOCK Request;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PPDO_DEVICE_EXTENSION PDODeviceExtension;
    KIRQL OldLevel;
    BOOLEAN ResetInProgress;

    DPRINT("USBSTOR_StartIo: DevObj=%p Irp=%p\n", DeviceObject, Irp);

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    IoAcquireCancelSpinLock(&OldLevel);

    IoSetCancelRoutine(Irp, NULL);

    // check if the irp has been cancelled
    if (Irp->Cancel)
    {
        IoReleaseCancelSpinLock(OldLevel);

        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoGetCurrentIrpStackLocation(Irp)->Parameters.Scsi.Srb->SrbStatus = SRB_STATUS_ABORTED;

        USBSTOR_QueueTerminateRequest(DeviceObject, Irp, FALSE);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        USBSTOR_QueueNextRequest(DeviceObject);
        return;
    }

    IoReleaseCancelSpinLock(OldLevel);

    KeAcquireSpinLock(&FDODeviceExtension->IrpListLock, &OldLevel);
    ResetInProgress = BooleanFlagOn(FDODeviceExtension->Flags, USBSTOR_FDO_FLAGS_DEVICE_RESETTING);
    KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    PDODeviceExtension = (PPDO_DEVICE_EXTENSION)IoStack->DeviceObject->DeviceExtension;
    Request = IoStack->Parameters.Scsi.Srb;
    ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);

    if (ResetInProgress)
    {
        // hard reset is in progress
        Request->SrbStatus = SRB_STATUS_NO_DEVICE;
        Request->DataTransferLength = 0;
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_DEVICE_DOES_NOT_EXIST;
        USBSTOR_QueueTerminateRequest(DeviceObject, Irp, FALSE);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        USBSTOR_QueueNextRequest(DeviceObject);
        return;
    }

    USBSTOR_HandleExecuteSCSI(IoStack->DeviceObject, Irp);

    // FIXME: handle error
}
