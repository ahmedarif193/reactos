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
USBSTOR_CancelIo(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT_IRQL_EQUAL(DISPATCH_LEVEL);
    ASSERT(FDODeviceExtension->Common.IsFDO);

    // this IRP isn't in our list here
    // now release the cancel lock
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;

    USBSTOR_QueueTerminateRequest(DeviceObject, Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    USBSTOR_QueueNextRequest(DeviceObject);
}

VOID
NTAPI
USBSTOR_Cancel(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT_IRQL_EQUAL(DISPATCH_LEVEL);
    ASSERT(FDODeviceExtension->Common.IsFDO);

    KeAcquireSpinLockAtDpcLevel(&FDODeviceExtension->IrpListLock);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->IrpListLock);

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;

    USBSTOR_QueueTerminateRequest(DeviceObject, Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    USBSTOR_QueueNextRequest(DeviceObject);
}

BOOLEAN
USBSTOR_QueueAddIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PDRIVER_CANCEL OldDriverCancel;
    KIRQL OldLevel;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    BOOLEAN IrpListFreeze;
    BOOLEAN SrbProcessing;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PSCSI_REQUEST_BLOCK Request = (PSCSI_REQUEST_BLOCK)IoStack->Parameters.Others.Argument1;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    IoMarkIrpPending(Irp);

    KeAcquireSpinLock(&FDODeviceExtension->IrpListLock, &OldLevel);

    SrbProcessing = (FDODeviceExtension->IrpPendingCount != 0);
    ASSERT(!SrbProcessing || FDODeviceExtension->ActiveSrb != NULL);
    ASSERT(SrbProcessing || FDODeviceExtension->ActiveSrb == NULL);

    if (SrbProcessing)
    {
        // add irp to queue
        InsertTailList(&FDODeviceExtension->IrpListHead, &Irp->Tail.Overlay.ListEntry);
    }
    else
    {
        FDODeviceExtension->ActiveSrb = Request;
    }

    FDODeviceExtension->IrpPendingCount++;
    KeClearEvent(&FDODeviceExtension->NoPendingRequests);

    // check if queue is freezed
    IrpListFreeze = BooleanFlagOn(FDODeviceExtension->Flags, USBSTOR_FDO_FLAGS_IRP_LIST_FREEZE);

    KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);

    // synchronize with cancellations by holding the cancel lock
    IoAcquireCancelSpinLock(&Irp->CancelIrql);

    if (SrbProcessing)
    {
        OldDriverCancel = IoSetCancelRoutine(Irp, USBSTOR_Cancel);
    }
    else
    {
        OldDriverCancel = IoSetCancelRoutine(Irp, USBSTOR_CancelIo);
    }

    // check if the irp has already been cancelled
    if (Irp->Cancel && OldDriverCancel == NULL)
    {
        // cancel irp
        Irp->CancelRoutine(DeviceObject, Irp);
        return FALSE;
    }

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    // if list is freezed, dont start this packet
    DPRINT("IrpListFreeze: %lu IrpPendingCount %lu\n", IrpListFreeze, FDODeviceExtension->IrpPendingCount);

    return (IrpListFreeze || SrbProcessing);
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
    IN PIRP Irp)
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

    FDODeviceExtension->IrpPendingCount--;

    // check if this was our current active SRB
    if (FDODeviceExtension->ActiveSrb == Request)
    {
        // indicate processing is completed
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
USBSTOR_QueueNextRequest(
    IN PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    PSCSI_REQUEST_BLOCK Request;
    PLIST_ENTRY Entry;
    KIRQL OldLevel;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    KeAcquireSpinLock(&FDODeviceExtension->IrpListLock, &OldLevel);

    DPRINT("USBSTOR_QueueNextRequest: ActiveSrb=%p Flags=0x%lx PendingCount=%lu\n",
           FDODeviceExtension->ActiveSrb, FDODeviceExtension->Flags, FDODeviceExtension->IrpPendingCount);

    // check first if there's already a request pending or the queue is frozen
    if (FDODeviceExtension->ActiveSrb != NULL ||
        BooleanFlagOn(FDODeviceExtension->Flags, USBSTOR_FDO_FLAGS_IRP_LIST_FREEZE))
    {
        DPRINT("USBSTOR_QueueNextRequest: skipping (ActiveSrb or frozen)\n");
        // no work to do yet
        KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);
        return;
    }

    // is there an irp pending
    if (IsListEmpty(&FDODeviceExtension->IrpListHead))
    {
        DPRINT("USBSTOR_QueueNextRequest: no IRP in list, calling IoStartNextPacket\n");
        KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);
        IoStartNextPacket(DeviceObject, TRUE);
        return;
    }

    Entry = RemoveHeadList(&FDODeviceExtension->IrpListHead);
    Irp = (PIRP)CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Request = (PSCSI_REQUEST_BLOCK)IoStack->Parameters.Others.Argument1;
    ASSERT(Request);

    FDODeviceExtension->ActiveSrb = Request;
    KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);

    DPRINT("USBSTOR_QueueNextRequest: starting Irp=%p Srb=%p\n", Irp, Request);

    // start next packet
    IoStartPacket(DeviceObject, Irp, &Request->QueueSortKey, USBSTOR_CancelIo);
    IoStartNextPacket(DeviceObject, TRUE);
}

VOID
USBSTOR_QueueRelease(
    IN PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PIRP Irp;
    KIRQL OldLevel;
    PIO_STACK_LOCATION IoStack;
    PSCSI_REQUEST_BLOCK Request;
    PLIST_ENTRY Entry;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    KeAcquireSpinLock(&FDODeviceExtension->IrpListLock, &OldLevel);

    // clear freezed status
    FDODeviceExtension->Flags &= ~USBSTOR_FDO_FLAGS_IRP_LIST_FREEZE;

    // grab newest irp
    if (IsListEmpty(&FDODeviceExtension->IrpListHead))
    {
        FDODeviceExtension->ActiveSrb = NULL;
        KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);
        return;
    }

    Entry = RemoveHeadList(&FDODeviceExtension->IrpListHead);
    Irp = (PIRP)CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Request = (PSCSI_REQUEST_BLOCK)IoStack->Parameters.Others.Argument1;
    FDODeviceExtension->ActiveSrb = Request;

    KeReleaseSpinLock(&FDODeviceExtension->IrpListLock, OldLevel);

    IoStartPacket(DeviceObject,
                  Irp,
                  &Request->QueueSortKey,
                  USBSTOR_CancelIo);
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

        USBSTOR_QueueTerminateRequest(DeviceObject, Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        USBSTOR_QueueNextRequest(DeviceObject);
        return;
    }

    IoReleaseCancelSpinLock(OldLevel);

    KeAcquireSpinLock(&FDODeviceExtension->CommonLock, &OldLevel);
    ResetInProgress = BooleanFlagOn(FDODeviceExtension->Flags, USBSTOR_FDO_FLAGS_DEVICE_RESETTING);
    KeReleaseSpinLock(&FDODeviceExtension->CommonLock, OldLevel);

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
        USBSTOR_QueueTerminateRequest(DeviceObject, Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        USBSTOR_QueueNextRequest(DeviceObject);
        return;
    }

    USBSTOR_HandleExecuteSCSI(IoStack->DeviceObject, Irp);

    // FIXME: handle error
}
