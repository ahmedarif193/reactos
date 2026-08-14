/*
 * PROJECT:     ReactOS Universal Serial Bus Human Interface Device Driver
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/hid/hidclass/hidclass.c
 * PURPOSE:     HID Class Driver
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

static LPWSTR ClientIdentificationAddress = L"HIDCLASS";
static ULONG HidClassDeviceNumber = 0;

NTSTATUS
NTAPI
DllInitialize(
    IN PUNICODE_STRING RegistryPath)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DllUnload(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClassAddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    WCHAR CharDeviceName[64];
    NTSTATUS Status;
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT NewDeviceObject;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    ULONG DeviceExtensionSize;
    PHIDCLASS_DRIVER_EXTENSION DriverExtension;

    /* increment device number */
    InterlockedIncrement((PLONG)&HidClassDeviceNumber);

    /* construct device name */
    _swprintf(CharDeviceName, L"\\Device\\_HID%08x", HidClassDeviceNumber);

    /* initialize device name */
    RtlInitUnicodeString(&DeviceName, CharDeviceName);

    /* get driver object extension */
    DriverExtension = IoGetDriverObjectExtension(DriverObject, ClientIdentificationAddress);
    if (!DriverExtension)
    {
        /* device removed */
        ASSERT(FALSE);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* calculate device extension size */
    DeviceExtensionSize = sizeof(HIDCLASS_FDO_EXTENSION) + DriverExtension->DeviceExtensionSize;

    /* now create the device */
    Status = IoCreateDevice(DriverObject, DeviceExtensionSize, &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &NewDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* failed to create device object */
        ASSERT(FALSE);
        return Status;
    }

    /* get device extension */
    FDODeviceExtension = NewDeviceObject->DeviceExtension;

    /* zero device extension */
    RtlZeroMemory(FDODeviceExtension, sizeof(HIDCLASS_FDO_EXTENSION));

    /* initialize device extension */
    FDODeviceExtension->Common.IsFDO = TRUE;
    FDODeviceExtension->Common.DriverExtension = DriverExtension;
    FDODeviceExtension->Common.HidDeviceExtension.PhysicalDeviceObject = PhysicalDeviceObject;
    FDODeviceExtension->Common.HidDeviceExtension.MiniDeviceExtension = (PVOID)((ULONG_PTR)FDODeviceExtension + sizeof(HIDCLASS_FDO_EXTENSION));
    FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject = IoAttachDeviceToDeviceStack(NewDeviceObject, PhysicalDeviceObject);
    if (FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject == NULL)
    {
        /* no PDO */
        IoDeleteDevice(NewDeviceObject);
        DPRINT1("[HIDCLASS] failed to attach to device stack\n");
        return STATUS_DEVICE_REMOVED;
    }

    /* sanity check */
    ASSERT(FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject);

    /* increment stack size */
    NewDeviceObject->StackSize++;

    /* init device object */
    NewDeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    NewDeviceObject->Flags  &= ~DO_DEVICE_INITIALIZING;

    /* now call driver provided add device routine */
    ASSERT(DriverExtension->AddDevice != 0);
    Status = DriverExtension->AddDevice(DriverObject, NewDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* failed */
        DPRINT1("HIDCLASS: AddDevice failed with %x\n", Status);
        IoDetachDevice(FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject);
        IoDeleteDevice(NewDeviceObject);
        return Status;
    }

    /* succeeded */
    return Status;
}

VOID
NTAPI
HidClassDriverUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED;
}

NTSTATUS
NTAPI
HidClass_Create(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;
    PHIDCLASS_FILEOP_CONTEXT Context;

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
         //
         // only supported for PDO
         //
         Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
         IoCompleteRequest(Irp, IO_NO_INCREMENT);
         return STATUS_UNSUCCESSFUL;
    }

    //
    // must be a PDO
    //
    ASSERT(CommonDeviceExtension->IsFDO == FALSE);

    //
    // get device extension
    //
    PDODeviceExtension = DeviceObject->DeviceExtension;

    //
    // get stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT("ShareAccess %x\n", IoStack->Parameters.Create.ShareAccess);
    DPRINT("Options %x\n", IoStack->Parameters.Create.Options);
    DPRINT("DesiredAccess %x\n", IoStack->Parameters.Create.SecurityContext->DesiredAccess);

    //
    // allocate context
    //
    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(HIDCLASS_FILEOP_CONTEXT), HIDCLASS_TAG);
    if (!Context)
    {
        //
        // no memory
        //
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // init context
    //
    RtlZeroMemory(Context, sizeof(HIDCLASS_FILEOP_CONTEXT));
    Context->DeviceExtension = PDODeviceExtension;
    KeInitializeSpinLock(&Context->Lock);
    InitializeListHead(&Context->ReadPendingIrpListHead);
    InitializeListHead(&Context->IrpCompletedListHead);
    KeInitializeEvent(&Context->IrpReadComplete, NotificationEvent, FALSE);

    //
    // store context
    //
    ASSERT(IoStack->FileObject);
    IoStack->FileObject->FsContext = Context;

    //
    // done
    //
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static VOID
HidClass_CancelPendingReads(
    IN PHIDCLASS_FILEOP_CONTEXT Context)
{
    PIRP StackIrps[8];
    PIRP *Irps = StackIrps;
    ULONG Count = 0;
    ULONG Index = 0;
    KIRQL OldLevel;
    PLIST_ENTRY Entry;

    KeAcquireSpinLock(&Context->Lock, &OldLevel);

    for (Entry = Context->ReadPendingIrpListHead.Flink;
         Entry != &Context->ReadPendingIrpListHead;
         Entry = Entry->Flink)
    {
        Count++;
    }

    KeReleaseSpinLock(&Context->Lock, OldLevel);

    if (!Count)
        return;

    if (Count > sizeof(StackIrps) / sizeof(StackIrps[0]))
    {
        Irps = ExAllocatePoolWithTag(NonPagedPool,
                                     Count * sizeof(PIRP),
                                     HIDCLASS_TAG);
        if (!Irps)
        {
            Irps = StackIrps;
            Count = sizeof(StackIrps) / sizeof(StackIrps[0]);
        }
    }

    KeAcquireSpinLock(&Context->Lock, &OldLevel);

    for (Entry = Context->ReadPendingIrpListHead.Flink;
         Entry != &Context->ReadPendingIrpListHead && Index < Count;
         Entry = Entry->Flink)
    {
        Irps[Index++] = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
    }

    KeReleaseSpinLock(&Context->Lock, OldLevel);

    while (Index--)
        IoCancelIrp(Irps[Index]);

    if (Irps != StackIrps)
        ExFreePoolWithTag(Irps, HIDCLASS_TAG);
}

static VOID
HidClass_StopPendingReads(
    IN PHIDCLASS_FILEOP_CONTEXT Context)
{
    BOOLEAN IsRequestPending;
    KIRQL OldLevel;

    KeAcquireSpinLock(&Context->Lock, &OldLevel);
    Context->StopInProgress = TRUE;
    IsRequestPending = !IsListEmpty(&Context->ReadPendingIrpListHead);
    KeReleaseSpinLock(&Context->Lock, OldLevel);

    if (IsRequestPending)
    {
        HidClass_CancelPendingReads(Context);

        DPRINT1("[HIDCLASS] Waiting for read irp completion...\n");
        KeWaitForSingleObject(&Context->IrpReadComplete,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}

static VOID
NTAPI
HidClass_ReadCancel(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIRP NewIrp;

    UNREFERENCED_PARAMETER(DeviceObject);

    NewIrp = Irp->Tail.Overlay.DriverContext[0];
    Irp->Tail.Overlay.DriverContext[0] = NULL;
    Irp->Tail.Overlay.DriverContext[1] = NULL;

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    if (NewIrp)
        IoCancelIrp(NewIrp);
}

static VOID
HidClass_FreeReadIrp(
    IN PIRP Irp,
    IN PHIDCLASS_IRP_CONTEXT IrpContext)
{
    ExFreePoolWithTag(IrpContext->InputReportBuffer, HIDCLASS_TAG);
    ExFreePoolWithTag(IrpContext, HIDCLASS_TAG);
    IoFreeIrp(Irp);
}

NTSTATUS
NTAPI
HidClass_Close(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHIDCLASS_FILEOP_CONTEXT IrpContext;
    KIRQL OldLevel;
    PLIST_ENTRY Entry;
    PIRP ListIrp;

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // is it a FDO request
    //
    if (CommonDeviceExtension->IsFDO)
    {
        //
        // how did the request get there
        //
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    //
    // get stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    //
    // sanity checks
    //
    ASSERT(IoStack->FileObject);
    ASSERT(IoStack->FileObject->FsContext);

    //
    // get irp context
    //
    IrpContext = IoStack->FileObject->FsContext;
    ASSERT(IrpContext);

    HidClass_StopPendingReads(IrpContext);

    //
    // acquire lock
    //
    KeAcquireSpinLock(&IrpContext->Lock, &OldLevel);

    //
    // sanity check
    //
    ASSERT(IsListEmpty(&IrpContext->ReadPendingIrpListHead));

    //
    // now free all irps
    //
    while (!IsListEmpty(&IrpContext->IrpCompletedListHead))
    {
        //
        // remove head irp
        //
        Entry = RemoveHeadList(&IrpContext->IrpCompletedListHead);

        //
        // get irp
        //
        ListIrp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        //
        // free the irp
        //
        IoFreeIrp(ListIrp);
    }

    //
    // release lock
    //
    KeReleaseSpinLock(&IrpContext->Lock, OldLevel);

    //
    // remove context
    //
    IoStack->FileObject->FsContext = NULL;

    //
    // free context
    //
    ExFreePoolWithTag(IrpContext, HIDCLASS_TAG);

    //
    // complete request
    //
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClass_Cleanup(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHIDCLASS_FILEOP_CONTEXT IrpContext;

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IrpContext = IoStack->FileObject ? IoStack->FileObject->FsContext : NULL;

    if (IrpContext)
        HidClass_StopPendingReads(IrpContext);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClass_ReadCompleteIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Ctx)
{
    PHIDCLASS_IRP_CONTEXT IrpContext;
    KIRQL OldLevel;
    PUCHAR Address;
    ULONG Offset;
    ULONG CopyLength;
    ULONG ReadLength;
    PIO_STACK_LOCATION OriginalIoStack;
    PHIDP_COLLECTION_DESC CollectionDescription;
    PHIDP_REPORT_IDS ReportDescription;
    BOOLEAN IsEmpty;
    KIRQL CancelIrql;
    NTSTATUS OriginalStatus;
    ULONG_PTR OriginalInformation;

    //
    // get irp context
    //
    IrpContext = Ctx;

    DPRINT("HidClass_ReadCompleteIrp Irql %lu\n", KeGetCurrentIrql());
    DPRINT("HidClass_ReadCompleteIrp Status %lx\n", Irp->IoStatus.Status);
    DPRINT("HidClass_ReadCompleteIrp Length %lu\n", Irp->IoStatus.Information);
    DPRINT("HidClass_ReadCompleteIrp Irp %p\n", Irp);
    DPRINT("HidClass_ReadCompleteIrp InputReportBuffer %p\n", IrpContext->InputReportBuffer);
    DPRINT("HidClass_ReadCompleteIrp InputReportBufferLength %li\n", IrpContext->InputReportBufferLength);
    DPRINT("HidClass_ReadCompleteIrp OriginalIrp %p\n", IrpContext->OriginalIrp);

    IoAcquireCancelSpinLock(&CancelIrql);
    IoSetCancelRoutine(IrpContext->OriginalIrp, NULL);
    IrpContext->OriginalIrp->Tail.Overlay.DriverContext[0] = NULL;
    IrpContext->OriginalIrp->Tail.Overlay.DriverContext[1] = NULL;
    IoReleaseCancelSpinLock(CancelIrql);

    OriginalStatus = Irp->IoStatus.Status;
    OriginalInformation = Irp->IoStatus.Information;

    //
    // copy result
    //
    if (NT_SUCCESS(Irp->IoStatus.Status) && Irp->IoStatus.Information)
    {
        //
        // get address
        //
        if (IrpContext->OriginalIrp->MdlAddress)
        {
            Address = MmGetSystemAddressForMdlSafe(IrpContext->OriginalIrp->MdlAddress,
                                                   NormalPagePriority);
        }
        else
        {
            Address = IrpContext->OriginalIrp->AssociatedIrp.SystemBuffer;
        }

        if (Address)
        {
            //
            // reports may have a report id prepended
            //
            Offset = 0;

            //
            // get collection description
            //
            CollectionDescription = HidClassPDO_GetCollectionDescription(&IrpContext->FileOp->DeviceExtension->Common.DeviceDescription,
                                                                         IrpContext->FileOp->DeviceExtension->CollectionNumber);
            ASSERT(CollectionDescription);

            //
            // get report description
            //
            ReportDescription = HidClassPDO_GetReportDescription(&IrpContext->FileOp->DeviceExtension->Common.DeviceDescription,
                                                                 IrpContext->FileOp->DeviceExtension->CollectionNumber);
            ASSERT(ReportDescription);

            if (CollectionDescription && ReportDescription)
            {
                //
                // calculate offset
                //
                ASSERT(CollectionDescription->InputLength >= ReportDescription->InputLength);
                Offset = CollectionDescription->InputLength - ReportDescription->InputLength;
            }

            //
            // copy result
            //
            OriginalIoStack = IoGetCurrentIrpStackLocation(IrpContext->OriginalIrp);
            ReadLength = OriginalIoStack->Parameters.Read.Length;

            if (Offset >= ReadLength)
            {
                OriginalStatus = STATUS_BUFFER_TOO_SMALL;
                OriginalInformation = 0;
            }
            else
            {
                CopyLength = IrpContext->InputReportBufferLength;
                if (CopyLength > Irp->IoStatus.Information)
                    CopyLength = (ULONG)Irp->IoStatus.Information;
                if (CopyLength > ReadLength - Offset)
                    CopyLength = ReadLength - Offset;

                RtlCopyMemory(&Address[Offset], IrpContext->InputReportBuffer, CopyLength);
                OriginalInformation = Offset + CopyLength;
            }
        }
        else
        {
            OriginalStatus = STATUS_INVALID_USER_BUFFER;
            OriginalInformation = 0;
        }
    }

    //
    // copy result status
    //
    IrpContext->OriginalIrp->IoStatus.Status = OriginalStatus;
    IrpContext->OriginalIrp->IoStatus.Information = OriginalInformation;

    //
    // free input report buffer
    //
    ExFreePoolWithTag(IrpContext->InputReportBuffer, HIDCLASS_TAG);

    //
    // remove us from pending list
    //
    KeAcquireSpinLock(&IrpContext->FileOp->Lock, &OldLevel);

    //
    // remove from pending list
    //
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);

    //
    // is list empty
    //
    IsEmpty = IsListEmpty(&IrpContext->FileOp->ReadPendingIrpListHead);

    //
    // insert into completed list
    //
    InsertTailList(&IrpContext->FileOp->IrpCompletedListHead, &Irp->Tail.Overlay.ListEntry);

    //
    // release lock
    //
    KeReleaseSpinLock(&IrpContext->FileOp->Lock, OldLevel);

    //
    // complete original request
    //
    IoCompleteRequest(IrpContext->OriginalIrp, IO_NO_INCREMENT);


    DPRINT("StopInProgress %x IsEmpty %x\n", IrpContext->FileOp->StopInProgress, IsEmpty);
    if (IrpContext->FileOp->StopInProgress && IsEmpty)
    {
        //
        // last pending irp
        //
        DPRINT1("[HIDCLASS] LastPendingTransfer Signalling\n");
        KeSetEvent(&IrpContext->FileOp->IrpReadComplete, 0, FALSE);
    }

    //
    // free irp context
    //
    ExFreePoolWithTag(IrpContext, HIDCLASS_TAG);

    //
    // done
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}

PIRP
HidClass_GetIrp(
    IN PHIDCLASS_FILEOP_CONTEXT Context)
{
   KIRQL OldLevel;
   PIRP Irp = NULL;
   PLIST_ENTRY ListEntry;

    //
    // acquire lock
    //
    KeAcquireSpinLock(&Context->Lock, &OldLevel);

    //
    // is list empty?
    //
    if (!IsListEmpty(&Context->IrpCompletedListHead))
    {
        //
        // grab first entry
        //
        ListEntry = RemoveHeadList(&Context->IrpCompletedListHead);

        //
        // get irp
        //
        Irp = CONTAINING_RECORD(ListEntry, IRP, Tail.Overlay.ListEntry);
    }

    //
    // release lock
    //
    KeReleaseSpinLock(&Context->Lock, OldLevel);

    //
    // done
    //
    return Irp;
}

NTSTATUS
HidClass_BuildIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP RequestIrp,
    IN PHIDCLASS_FILEOP_CONTEXT Context,
    IN ULONG DeviceIoControlCode,
    IN ULONG BufferLength,
    OUT PIRP *OutIrp,
    OUT PHIDCLASS_IRP_CONTEXT *OutIrpContext)
{
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_IRP_CONTEXT IrpContext;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;
    PHIDP_COLLECTION_DESC CollectionDescription;
    PHIDP_REPORT_IDS ReportDescription;

    //
    // get an irp from fresh list
    //
    Irp = HidClass_GetIrp(Context);
    if (!Irp)
    {
        //
        // build new irp
        //
        Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
        if (!Irp)
        {
            //
            // no memory
            //
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
    {
        //
        // re-use irp
        //
        IoReuseIrp(Irp, STATUS_SUCCESS);
    }

    //
    // allocate completion context
    //
    IrpContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(HIDCLASS_IRP_CONTEXT), HIDCLASS_TAG);
    if (!IrpContext)
    {
        //
        // no memory
        //
        IoFreeIrp(Irp);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // get device extension
    //
    PDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);

    //
    // init irp context
    //
    RtlZeroMemory(IrpContext, sizeof(HIDCLASS_IRP_CONTEXT));
    IrpContext->OriginalIrp = RequestIrp;
    IrpContext->FileOp = Context;

    //
    // get collection description
    //
    CollectionDescription = HidClassPDO_GetCollectionDescription(&IrpContext->FileOp->DeviceExtension->Common.DeviceDescription,
                                                                 IrpContext->FileOp->DeviceExtension->CollectionNumber);
    ASSERT(CollectionDescription);

    //
    // get report description
    //
    ReportDescription = HidClassPDO_GetReportDescription(&IrpContext->FileOp->DeviceExtension->Common.DeviceDescription,
                                                         IrpContext->FileOp->DeviceExtension->CollectionNumber);
    ASSERT(ReportDescription);

    //
    // sanity check
    //
    ASSERT(CollectionDescription->InputLength >= ReportDescription->InputLength);

    if (Context->StopInProgress)
    {
         //
         // stop in progress
         //
         DPRINT1("[HIDCLASS] Stop In Progress\n");
         Irp->IoStatus.Status = STATUS_CANCELLED;
         IoFreeIrp(Irp);
         ExFreePoolWithTag(IrpContext, HIDCLASS_TAG);
         return STATUS_CANCELLED;

    }

    //
    // store report length
    //
    IrpContext->InputReportBufferLength = ReportDescription->InputLength;

    //
    // allocate buffer
    //
    IrpContext->InputReportBuffer = ExAllocatePoolWithTag(NonPagedPool, IrpContext->InputReportBufferLength, HIDCLASS_TAG);
    if (!IrpContext->InputReportBuffer)
    {
        //
        // no memory
        //
        IoFreeIrp(Irp);
        ExFreePoolWithTag(IrpContext, HIDCLASS_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // get stack location
    //
    IoStack = IoGetNextIrpStackLocation(Irp);

    //
    // init stack location
    //
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = DeviceIoControlCode;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = IrpContext->InputReportBufferLength;
    IoStack->Parameters.DeviceIoControl.InputBufferLength = 0;
    IoStack->Parameters.DeviceIoControl.Type3InputBuffer = NULL;
    Irp->UserBuffer = IrpContext->InputReportBuffer;
    IoStack->DeviceObject = DeviceObject;

    //
    // store result
    //
    *OutIrp = Irp;
    *OutIrpContext = IrpContext;

    //
    // done
    //
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HidClass_Read(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_FILEOP_CONTEXT Context;
    KIRQL OldLevel;
    NTSTATUS Status;
    PIRP NewIrp;
    PHIDCLASS_IRP_CONTEXT NewIrpContext;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    KIRQL CancelIrql;

    //
    // get current stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(CommonDeviceExtension->IsFDO == FALSE);

    //
    // sanity check
    //
    ASSERT(IoStack->FileObject);
    ASSERT(IoStack->FileObject->FsContext);

    //
    // get context
    //
    Context = IoStack->FileObject->FsContext;
    ASSERT(Context);

    //
    // Polled HID miniports are not implemented here. Do not assert in
    // checked builds; fail reads explicitly until a real poll path exists.
    //
    if (Context->DeviceExtension->Common.DriverExtension->DevicesArePolled)
    {
        DPRINT1("[HIDCLASS] ReadDispatch: polled device not supported via IRP path\n");
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_NOT_SUPPORTED;
    }

    if (Context->StopInProgress)
    {
        //
        // stop in progress
        //
        DPRINT1("[HIDCLASS] Stop In Progress\n");
        Irp->IoStatus.Status = STATUS_CANCELLED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_CANCELLED;
    }

    //
    // build irp request
    //
    Status = HidClass_BuildIrp(DeviceObject,
                               Irp,
                               Context,
                               IOCTL_HID_READ_REPORT,
                               IoStack->Parameters.Read.Length,
                               &NewIrp,
                               &NewIrpContext);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed
        //
        DPRINT1("HidClass_BuildIrp failed with %x\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    //
    // acquire lock
    //
    KeAcquireSpinLock(&Context->Lock, &OldLevel);

    if (Context->StopInProgress)
    {
        KeReleaseSpinLock(&Context->Lock, OldLevel);

        IoAcquireCancelSpinLock(&CancelIrql);
        IoSetCancelRoutine(Irp, NULL);
        Irp->Tail.Overlay.DriverContext[0] = NULL;
        Irp->Tail.Overlay.DriverContext[1] = NULL;
        IoReleaseCancelSpinLock(CancelIrql);

        HidClass_FreeReadIrp(NewIrp, NewIrpContext);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_CANCELLED;
    }

    //
    // insert irp into pending list
    //
    InsertTailList(&Context->ReadPendingIrpListHead, &NewIrp->Tail.Overlay.ListEntry);

    //
    // set completion routine
    //
    IoSetCompletionRoutine(NewIrp, HidClass_ReadCompleteIrp, NewIrpContext, TRUE, TRUE, TRUE);

    //
    // make next location current
    //
    IoSetNextIrpStackLocation(NewIrp);

    IoAcquireCancelSpinLock(&CancelIrql);
    if (Irp->Cancel)
    {
        IoReleaseCancelSpinLock(CancelIrql);
        RemoveEntryList(&NewIrp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Context->Lock, OldLevel);

        HidClass_FreeReadIrp(NewIrp, NewIrpContext);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_CANCELLED;
    }

    Irp->Tail.Overlay.DriverContext[0] = NewIrp;
    Irp->Tail.Overlay.DriverContext[1] = NewIrpContext;
    IoSetCancelRoutine(Irp, HidClass_ReadCancel);
    IoReleaseCancelSpinLock(CancelIrql);

    //
    // release spin lock
    //
    KeReleaseSpinLock(&Context->Lock, OldLevel);

    //
    // mark irp pending
    //
    IoMarkIrpPending(Irp);

    //
    // let's dispatch the request
    //
    ASSERT(Context->DeviceExtension);
    Status = Context->DeviceExtension->Common.DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL](Context->DeviceExtension->FDODeviceObject, NewIrp);

    //
    // complete
    //
    return STATUS_PENDING;
}

typedef struct _HIDCLASS_SYNC_IRP_CONTEXT
{
    KEVENT Event;
    BOOLEAN Completed;
} HIDCLASS_SYNC_IRP_CONTEXT, *PHIDCLASS_SYNC_IRP_CONTEXT;

static NTSTATUS
NTAPI
HidClass_SyncIrpCompletion(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PHIDCLASS_SYNC_IRP_CONTEXT SyncContext = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    SyncContext->Completed = TRUE;
    KeSetEvent(&SyncContext->Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
HidClass_DispatchMiniDriverRequest(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension,
    IN ULONG IoControlCode,
    IN PHID_XFER_PACKET XferPacket)
{
    PDEVICE_OBJECT FDODeviceObject;
    PHIDCLASS_COMMON_DEVICE_EXTENSION FDOCommonExtension;
    HIDCLASS_SYNC_IRP_CONTEXT SyncContext;
    PIO_STACK_LOCATION IoStack;
    PIRP SubIrp;
    NTSTATUS Status;

    ASSERT(PDODeviceExtension);
    ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);
    ASSERT(PDODeviceExtension->FDODeviceObject);

    FDODeviceObject = PDODeviceExtension->FDODeviceObject;
    FDOCommonExtension = FDODeviceObject->DeviceExtension;

    ASSERT(FDOCommonExtension->IsFDO);
    ASSERT(FDOCommonExtension->DriverExtension);
    ASSERT(FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL]);

    SubIrp = IoAllocateIrp(FDODeviceObject->StackSize, FALSE);
    if (!SubIrp)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(&SyncContext, sizeof(SyncContext));
    KeInitializeEvent(&SyncContext.Event, NotificationEvent, FALSE);

    SubIrp->UserBuffer = XferPacket;

    IoStack = IoGetNextIrpStackLocation(SubIrp);
    RtlZeroMemory(IoStack, sizeof(*IoStack));
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    IoStack->Parameters.DeviceIoControl.InputBufferLength = XferPacket->reportBufferLen;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = XferPacket->reportBufferLen;
    IoStack->DeviceObject = FDODeviceObject;

    IoSetCompletionRoutine(SubIrp,
                           HidClass_SyncIrpCompletion,
                           &SyncContext,
                           TRUE,
                           TRUE,
                           TRUE);

    IoSetNextIrpStackLocation(SubIrp);
    Status = FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL](FDODeviceObject, SubIrp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&SyncContext.Event, Executive, KernelMode, FALSE, NULL);
    }

    if (SyncContext.Completed)
        Status = SubIrp->IoStatus.Status;

    IoFreeIrp(SubIrp);
    return Status;
}

static NTSTATUS
HidClass_DispatchMiniDriverStringRequest(
    IN PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension,
    IN ULONG IoControlCode,
    IN ULONG StringId,
    OUT PVOID StringBuffer,
    IN ULONG StringBufferLength,
    OUT PULONG_PTR Information)
{
    PDEVICE_OBJECT FDODeviceObject;
    PHIDCLASS_COMMON_DEVICE_EXTENSION FDOCommonExtension;
    HIDCLASS_SYNC_IRP_CONTEXT SyncContext;
    PIO_STACK_LOCATION IoStack;
    PIRP SubIrp;
    NTSTATUS Status;

    ASSERT(PDODeviceExtension);
    ASSERT(PDODeviceExtension->Common.IsFDO == FALSE);
    ASSERT(PDODeviceExtension->FDODeviceObject);

    if (Information)
        *Information = 0;

    FDODeviceObject = PDODeviceExtension->FDODeviceObject;
    FDOCommonExtension = FDODeviceObject->DeviceExtension;

    ASSERT(FDOCommonExtension->IsFDO);
    ASSERT(FDOCommonExtension->DriverExtension);
    ASSERT(FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL]);

    SubIrp = IoAllocateIrp(FDODeviceObject->StackSize, FALSE);
    if (!SubIrp)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(&SyncContext, sizeof(SyncContext));
    KeInitializeEvent(&SyncContext.Event, NotificationEvent, FALSE);

    SubIrp->UserBuffer = StringBuffer;

    IoStack = IoGetNextIrpStackLocation(SubIrp);
    RtlZeroMemory(IoStack, sizeof(*IoStack));
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    IoStack->Parameters.DeviceIoControl.InputBufferLength = sizeof(ULONG);
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = StringBufferLength;
    IoStack->Parameters.DeviceIoControl.Type3InputBuffer = UlongToPtr(StringId);
    IoStack->DeviceObject = FDODeviceObject;

    IoSetCompletionRoutine(SubIrp,
                           HidClass_SyncIrpCompletion,
                           &SyncContext,
                           TRUE,
                           TRUE,
                           TRUE);

    IoSetNextIrpStackLocation(SubIrp);
    Status = FDOCommonExtension->DriverExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL](FDODeviceObject, SubIrp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&SyncContext.Event, Executive, KernelMode, FALSE, NULL);
    }

    if (SyncContext.Completed)
    {
        Status = SubIrp->IoStatus.Status;
        if (Information)
            *Information = SubIrp->IoStatus.Information;
    }

    IoFreeIrp(SubIrp);
    return Status;
}

NTSTATUS
NTAPI
HidClass_Write(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    HID_XFER_PACKET XferPacket;
    NTSTATUS Status;
    ULONG Length;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Length = IoStack->Parameters.Write.Length;
    if (Length < 1)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&XferPacket, sizeof(XferPacket));
    XferPacket.reportBufferLen = Length;
    XferPacket.reportBuffer = Irp->UserBuffer;
    XferPacket.reportId = XferPacket.reportBuffer[0];

    CommonDeviceExtension = DeviceObject->DeviceExtension;
    if (CommonDeviceExtension->IsFDO)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    PDODeviceExtension = DeviceObject->DeviceExtension;
    Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                IOCTL_HID_WRITE_REPORT,
                                                &XferPacket);
    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = XferPacket.reportBufferLen;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
HidClass_DeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    PHID_COLLECTION_INFORMATION CollectionInformation;
    PHIDP_COLLECTION_DESC CollectionDescription;
    PHIDCLASS_PDO_DEVICE_EXTENSION PDODeviceExtension;
    NTSTATUS Status;

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // only PDO are supported
    //
    if (CommonDeviceExtension->IsFDO)
    {
        //
        // invalid request
        //
        DPRINT1("[HIDCLASS] DeviceControl Irp for FDO arrived\n");
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER_1;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER_1;
    }

    ASSERT(CommonDeviceExtension->IsFDO == FALSE);

    //
    // get pdo device extension
    //
    PDODeviceExtension = DeviceObject->DeviceExtension;

    //
    // get stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_HID_GET_COLLECTION_INFORMATION:
        {
            ULONG CompositeLength;

            //
            // check if output buffer is big enough
            //
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_COLLECTION_INFORMATION))
            {
                //
                // invalid buffer size
                //
                Irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_BUFFER_SIZE;
            }

            //
            // get output buffer
            //
            CollectionInformation = Irp->AssociatedIrp.SystemBuffer;
            ASSERT(CollectionInformation);

            //
            // get collection description
            //
            CollectionDescription = HidClassPDO_GetCollectionDescription(&CommonDeviceExtension->DeviceDescription,
                                                                         PDODeviceExtension->CollectionNumber);
            ASSERT(CollectionDescription);

            //
            // init result buffer
            //
            CollectionInformation->DescriptorSize = CollectionDescription->PreparsedDataLength;
            /* Kernel HID clients pass this buffer directly to HidP_* routines,
             * so they also need the private parser context after the public KDR. */
            if (Irp->RequestorMode == KernelMode && HidP_GetCompositePreparsedDataSize(CollectionDescription->PreparsedData, CollectionDescription->PreparsedDataLength, &CompositeLength))
                CollectionInformation->DescriptorSize = CompositeLength;
            CollectionInformation->Polled = CommonDeviceExtension->DriverExtension->DevicesArePolled;
            CollectionInformation->VendorID = CommonDeviceExtension->Attributes.VendorID;
            CollectionInformation->ProductID = CommonDeviceExtension->Attributes.ProductID;
            CollectionInformation->VersionNumber = CommonDeviceExtension->Attributes.VersionNumber;

            //
            // complete request
            //
            Irp->IoStatus.Information = sizeof(HID_COLLECTION_INFORMATION);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_HID_GET_COLLECTION_DESCRIPTOR:
        {
            ULONG CopyLength;
            ULONG CompositeLength;

            //
            // get collection description
            //
            CollectionDescription = HidClassPDO_GetCollectionDescription(&CommonDeviceExtension->DeviceDescription,
                                                                         PDODeviceExtension->CollectionNumber);
            ASSERT(CollectionDescription);

            CopyLength = CollectionDescription->PreparsedDataLength;
            if (HidP_GetCompositePreparsedDataSize(CollectionDescription->PreparsedData, CollectionDescription->PreparsedDataLength, &CompositeLength) && IoStack->Parameters.DeviceIoControl.OutputBufferLength >= CompositeLength)
                CopyLength = CompositeLength;

            //
            // check if output buffer is big enough
            //
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < CollectionDescription->PreparsedDataLength)
            {
                //
                // invalid buffer size
                //
                Irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_BUFFER_SIZE;
            }

            //
            // copy result
            //
            ASSERT(Irp->UserBuffer);
            Status = STATUS_SUCCESS;

            if (Irp->RequestorMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForWrite(Irp->UserBuffer,
                                  CopyLength,
                                  sizeof(UCHAR));
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
            }

            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            RtlCopyMemory(Irp->UserBuffer,
                          CollectionDescription->PreparsedData,
                          CopyLength);

            //
            // complete request
            //
            Irp->IoStatus.Information = CopyLength;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        case IOCTL_HID_GET_FEATURE:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;
            PUCHAR ReportBuffer;

            ReportBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!ReportBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ReportBuffer[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.OutputBufferLength < ReportDescription->FeatureLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->FeatureLength;
            XferPacket.reportBuffer = ReportBuffer;
            XferPacket.reportId = ReportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_GET_FEATURE,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_SET_FEATURE:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;

            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < 1)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }
            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ((PUCHAR)Irp->AssociatedIrp.SystemBuffer)[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.InputBufferLength < ReportDescription->FeatureLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->FeatureLength;
            XferPacket.reportBuffer = Irp->AssociatedIrp.SystemBuffer;
            XferPacket.reportId = XferPacket.reportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_SET_FEATURE,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_GET_INPUT_REPORT:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;
            PUCHAR ReportBuffer;

            ReportBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!ReportBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ReportBuffer[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.OutputBufferLength < ReportDescription->InputLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->InputLength;
            XferPacket.reportBuffer = ReportBuffer;
            XferPacket.reportId = ReportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_GET_INPUT_REPORT,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_SET_OUTPUT_REPORT:
        {
            HID_XFER_PACKET XferPacket;
            NTSTATUS Status;
            PHIDP_REPORT_IDS ReportDescription;

            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < 1)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }
            ReportDescription = HidClassPDO_GetReportDescriptionByReportID(&PDODeviceExtension->Common.DeviceDescription, ((PUCHAR)Irp->AssociatedIrp.SystemBuffer)[0]);
            if (!ReportDescription || IoStack->Parameters.DeviceIoControl.InputBufferLength < ReportDescription->OutputLength)
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(&XferPacket, sizeof(XferPacket));
            XferPacket.reportBufferLen = ReportDescription->OutputLength;
            XferPacket.reportBuffer = Irp->AssociatedIrp.SystemBuffer;
            XferPacket.reportId = XferPacket.reportBuffer[0];

            Status = HidClass_DispatchMiniDriverRequest(PDODeviceExtension,
                                                        IOCTL_HID_SET_OUTPUT_REPORT,
                                                        &XferPacket);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = XferPacket.reportBufferLen;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IOCTL_HID_GET_MANUFACTURER_STRING:
        case IOCTL_HID_GET_PRODUCT_STRING:
        case IOCTL_HID_GET_SERIALNUMBER_STRING:
        case IOCTL_HID_GET_INDEXED_STRING:
        {
            ULONG StringId;
            ULONG MiniDriverIoControlCode = IOCTL_HID_GET_STRING;
            PVOID StringBuffer;
            ULONG_PTR Information = 0;

            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength == 0)
            {
                Irp->IoStatus.Status = STATUS_INVALID_BUFFER_SIZE;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_BUFFER_SIZE;
            }

            StringBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
            if (!StringBuffer)
            {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_HID_GET_MANUFACTURER_STRING:
                    StringId = HID_STRING_ID_IMANUFACTURER;
                    break;

                case IOCTL_HID_GET_PRODUCT_STRING:
                    StringId = HID_STRING_ID_IPRODUCT;
                    break;

                case IOCTL_HID_GET_SERIALNUMBER_STRING:
                    StringId = HID_STRING_ID_ISERIALNUMBER;
                    break;

                default:
                    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG) ||
                        !Irp->AssociatedIrp.SystemBuffer)
                    {
                        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                        IoCompleteRequest(Irp, IO_NO_INCREMENT);
                        return STATUS_INVALID_PARAMETER;
                    }

                    StringId = *(PULONG)Irp->AssociatedIrp.SystemBuffer;
                    MiniDriverIoControlCode = IOCTL_HID_GET_INDEXED_STRING;
                    break;
            }

            Status = HidClass_DispatchMiniDriverStringRequest(PDODeviceExtension,
                                                              MiniDriverIoControlCode,
                                                              StringId,
                                                              StringBuffer,
                                                              IoStack->Parameters.DeviceIoControl.OutputBufferLength,
                                                              &Information);
            Irp->IoStatus.Status = Status;
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Information;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        default:
        {
            DPRINT1("[HIDCLASS] DeviceControl IoControlCode 0x%x not implemented\n", IoStack->Parameters.DeviceIoControl.IoControlCode);
            Irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_IMPLEMENTED;
        }
    }
}

NTSTATUS
NTAPI
HidClass_InternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    Irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HidClass_Power(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    if (CommonDeviceExtension->IsFDO)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        return HidClassFDO_DispatchRequest(DeviceObject, Irp);
    }
    else
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
}

NTSTATUS
NTAPI
HidClass_PnP(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;

    //
    // get common device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // check type of device object
    //
    if (CommonDeviceExtension->IsFDO)
    {
        //
        // handle request
        //
        return HidClassFDO_PnP(DeviceObject, Irp);
    }
    else
    {
        //
        // handle request
        //
        return HidClassPDO_PnP(DeviceObject, Irp);
    }
}

NTSTATUS
NTAPI
HidClass_DispatchDefault(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;

    //
    // get common device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // FIXME: support PDO
    //
    ASSERT(CommonDeviceExtension->IsFDO == TRUE);

    //
    // skip current irp stack location
    //
    IoSkipCurrentIrpStackLocation(Irp);

    //
    // dispatch to lower device object
    //
    return IoCallDriver(CommonDeviceExtension->HidDeviceExtension.NextDeviceObject, Irp);
}

NTSTATUS
NTAPI
HidClassDispatch(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;

    //
    // get current stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DPRINT("[HIDCLASS] Dispatch Major %x Minor %x\n", IoStack->MajorFunction, IoStack->MinorFunction);

    //
    // dispatch request based on major function
    //
    switch (IoStack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            return HidClass_Create(DeviceObject, Irp);
        case IRP_MJ_CLEANUP:
            return HidClass_Cleanup(DeviceObject, Irp);
        case IRP_MJ_CLOSE:
            return HidClass_Close(DeviceObject, Irp);
        case IRP_MJ_READ:
            return HidClass_Read(DeviceObject, Irp);
        case IRP_MJ_WRITE:
            return HidClass_Write(DeviceObject, Irp);
        case IRP_MJ_DEVICE_CONTROL:
            return HidClass_DeviceControl(DeviceObject, Irp);
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
           return HidClass_InternalDeviceControl(DeviceObject, Irp);
        case IRP_MJ_POWER:
            return HidClass_Power(DeviceObject, Irp);
        case IRP_MJ_PNP:
            return HidClass_PnP(DeviceObject, Irp);
        default:
            return HidClass_DispatchDefault(DeviceObject, Irp);
    }
}

NTSTATUS
NTAPI
HidRegisterMinidriver(
    IN PHID_MINIDRIVER_REGISTRATION MinidriverRegistration)
{
    NTSTATUS Status;
    PHIDCLASS_DRIVER_EXTENSION DriverExtension;

    /* check if the version matches */
    if (MinidriverRegistration->Revision > HID_REVISION)
    {
        /* revision mismatch */
        ASSERT(FALSE);
        return STATUS_REVISION_MISMATCH;
    }

    /* now allocate the driver object extension */
    Status = IoAllocateDriverObjectExtension(MinidriverRegistration->DriverObject,
                                             ClientIdentificationAddress,
                                             sizeof(HIDCLASS_DRIVER_EXTENSION),
                                             (PVOID *)&DriverExtension);
    if (!NT_SUCCESS(Status))
    {
        /* failed to allocate driver extension */
        ASSERT(FALSE);
        return Status;
    }

    /* zero driver extension */
    RtlZeroMemory(DriverExtension, sizeof(HIDCLASS_DRIVER_EXTENSION));

    /* init driver extension */
    DriverExtension->DriverObject = MinidriverRegistration->DriverObject;
    DriverExtension->DeviceExtensionSize = MinidriverRegistration->DeviceExtensionSize;
    DriverExtension->DevicesArePolled = MinidriverRegistration->DevicesArePolled;
    DriverExtension->AddDevice = MinidriverRegistration->DriverObject->DriverExtension->AddDevice;
    DriverExtension->DriverUnload = MinidriverRegistration->DriverObject->DriverUnload;

    /* copy driver dispatch routines */
    RtlCopyMemory(DriverExtension->MajorFunction,
                  MinidriverRegistration->DriverObject->MajorFunction,
                  sizeof(PDRIVER_DISPATCH) * (IRP_MJ_MAXIMUM_FUNCTION + 1));

    /* initialize lock */
    KeInitializeSpinLock(&DriverExtension->Lock);

    /* now replace dispatch routines */
    DriverExtension->DriverObject->DriverExtension->AddDevice = HidClassAddDevice;
    DriverExtension->DriverObject->DriverUnload = HidClassDriverUnload;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_CREATE] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_CLEANUP] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_CLOSE] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_READ] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_WRITE] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_POWER] = HidClassDispatch;
    DriverExtension->DriverObject->MajorFunction[IRP_MJ_PNP] = HidClassDispatch;

    /* done */
    return STATUS_SUCCESS;
}
