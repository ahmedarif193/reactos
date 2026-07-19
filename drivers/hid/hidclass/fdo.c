/*
 * PROJECT:     ReactOS Universal Serial Bus Human Interface Device Driver
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/hid/hidclass/fdo.c
 * PURPOSE:     HID Class Driver
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
HidClassFDO_QueryCapabilitiesCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    //
    // set event
    //
    KeSetEvent(Context, 0, FALSE);

    //
    // completion is done in the HidClassFDO_QueryCapabilities routine
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
HidClassFDO_QueryCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PDEVICE_CAPABILITIES Capabilities)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;

    //
    // get device extension
    //
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // init event
    //
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    //
    // now allocate the irp
    //
    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        //
        // no memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // get next stack location
    //
    IoStack = IoGetNextIrpStackLocation(Irp);

    //
    // init stack location
    //
    IoStack->MajorFunction = IRP_MJ_PNP;
    IoStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
    IoStack->Parameters.DeviceCapabilities.Capabilities = Capabilities;

    //
    // set completion routine
    //
    IoSetCompletionRoutine(Irp, HidClassFDO_QueryCapabilitiesCompletionRoutine, &Event, TRUE, TRUE, TRUE);

    //
    // init capabilities
    //
    RtlZeroMemory(Capabilities, sizeof(DEVICE_CAPABILITIES));
    Capabilities->Size = sizeof(DEVICE_CAPABILITIES);
    Capabilities->Version = 1; // FIXME hardcoded constant
    Capabilities->Address = MAXULONG;
    Capabilities->UINumber = MAXULONG;

    //
    // pnp irps have default completion code
    //
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    //
    // call lower  device
    //
    Status = IoCallDriver(FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        //
        // wait for completion
        //
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    //
    // get status
    //
    Status = Irp->IoStatus.Status;

    //
    // complete request
    //
    IoFreeIrp(Irp);

    //
    // done
    //
    return Status;
}

NTSTATUS
NTAPI
HidClassFDO_DispatchRequestSynchronousCompletion(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    //
    // signal event
    //
    KeSetEvent(Context, 0, FALSE);

    //
    // done
    //
    return STATUS_MORE_PROCESSING_REQUIRED;
}


NTSTATUS
HidClassFDO_DispatchRequestSynchronous(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    KEVENT Event;
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;

    //
    // init event
    //
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    //
    // set completion routine
    //
    IoSetCompletionRoutine(Irp, HidClassFDO_DispatchRequestSynchronousCompletion, &Event, TRUE, TRUE, TRUE);

    ASSERT(Irp->CurrentLocation > 0);
    //
    // create stack location
    //
    IoSetNextIrpStackLocation(Irp);

    //
    // get next stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    //
    // store device object
    //
    IoStack->DeviceObject = DeviceObject;

    //
    // sanity check
    //
    ASSERT(CommonDeviceExtension->DriverExtension->MajorFunction[IoStack->MajorFunction] != NULL);

    //
    // call minidriver (hidusb)
    //
    Status = CommonDeviceExtension->DriverExtension->MajorFunction[IoStack->MajorFunction](DeviceObject, Irp);

    //
    // wait for the request to finish
    //
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

        //
        // update status
        //
        Status = Irp->IoStatus.Status;
    }

    //
    // done
    //
    return Status;
}

NTSTATUS
HidClassFDO_DispatchRequest(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_COMMON_DEVICE_EXTENSION CommonDeviceExtension;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;

    //
    // get device extension
    //
    CommonDeviceExtension = DeviceObject->DeviceExtension;

    ASSERT(Irp->CurrentLocation > 0);

    //
    // create stack location
    //
    IoSetNextIrpStackLocation(Irp);

    //
    // get next stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    //
    // store device object
    //
    IoStack->DeviceObject = DeviceObject;

    //
    // sanity check
    //
    ASSERT(CommonDeviceExtension->DriverExtension->MajorFunction[IoStack->MajorFunction] != NULL);

    //
    // call driver
    //
    Status = CommonDeviceExtension->DriverExtension->MajorFunction[IoStack->MajorFunction](DeviceObject, Irp);

    //
    // done
    //
    return Status;
}

NTSTATUS
HidClassFDO_GetDescriptors(
    IN PDEVICE_OBJECT DeviceObject)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    //
    // get device extension
    //
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // let's allocate irp
    //
    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        //
        // no memory
        //
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
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_HID_GET_DEVICE_DESCRIPTOR;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(HID_DESCRIPTOR);
    IoStack->Parameters.DeviceIoControl.InputBufferLength = 0;
    IoStack->Parameters.DeviceIoControl.Type3InputBuffer = NULL;
    Irp->UserBuffer = &FDODeviceExtension->HidDescriptor;

    //
    // send request
    //
    Status = HidClassFDO_DispatchRequestSynchronous(DeviceObject, Irp);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed to get device descriptor
        //
        DPRINT1("[HIDCLASS] IOCTL_HID_GET_DEVICE_DESCRIPTOR failed with %x\n", Status);
        IoFreeIrp(Irp);
        return Status;
    }

    //
    // let's get device attributes
    //
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_HID_GET_DEVICE_ATTRIBUTES;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(HID_DEVICE_ATTRIBUTES);
    Irp->UserBuffer = &FDODeviceExtension->Common.Attributes;

    //
    // send request
    //
    Status = HidClassFDO_DispatchRequestSynchronous(DeviceObject, Irp);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed to get device descriptor
        //
        DPRINT1("[HIDCLASS] IOCTL_HID_GET_DEVICE_ATTRIBUTES failed with %x\n", Status);
        IoFreeIrp(Irp);
        return Status;
    }

    //
    // sanity checks
    //
    ASSERT(FDODeviceExtension->HidDescriptor.bLength == sizeof(HID_DESCRIPTOR));
    ASSERT(FDODeviceExtension->HidDescriptor.bNumDescriptors > 0);
    ASSERT(FDODeviceExtension->HidDescriptor.DescriptorList[0].wReportLength > 0);
    ASSERT(FDODeviceExtension->HidDescriptor.DescriptorList[0].bReportType == HID_REPORT_DESCRIPTOR_TYPE);

    //
    // now allocate space for the report descriptor
    //
    FDODeviceExtension->ReportDescriptor = ExAllocatePoolWithTag(NonPagedPool,
                                                                 FDODeviceExtension->HidDescriptor.DescriptorList[0].wReportLength,
                                                                 HIDCLASS_TAG);
    if (!FDODeviceExtension->ReportDescriptor)
    {
        //
        // not enough memory
        //
        IoFreeIrp(Irp);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // init stack location
    //
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_HID_GET_REPORT_DESCRIPTOR;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = FDODeviceExtension->HidDescriptor.DescriptorList[0].wReportLength;
    Irp->UserBuffer = FDODeviceExtension->ReportDescriptor;

    //
    // send request
    //
    Status = HidClassFDO_DispatchRequestSynchronous(DeviceObject, Irp);
    if (!NT_SUCCESS(Status))
    {
        //
        // failed to get device descriptor
        //
        DPRINT1("[HIDCLASS] IOCTL_HID_GET_REPORT_DESCRIPTOR failed with %x\n", Status);
        IoFreeIrp(Irp);
        return Status;
    }

    //
    // completed successfully
    //
    IoFreeIrp(Irp);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidClassFDO_SendReportRequest(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG IoControlCode,
    IN OUT PHID_XFER_PACKET XferPacket)
{
    PIO_STACK_LOCATION IoStack;
    PIRP Irp;
    NTSTATUS Status;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->UserBuffer = XferPacket;
    IoStack = IoGetNextIrpStackLocation(Irp);
    RtlZeroMemory(IoStack, sizeof(*IoStack));
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    IoStack->Parameters.DeviceIoControl.InputBufferLength =
        XferPacket->reportBufferLen;
    IoStack->Parameters.DeviceIoControl.OutputBufferLength =
        XferPacket->reportBufferLen;

    Status = HidClassFDO_DispatchRequestSynchronous(DeviceObject, Irp);
    IoFreeIrp(Irp);
    return Status;
}

static
BOOLEAN
HidClassFDO_HasPrecisionTouchpad(
    IN PHIDP_DEVICE_DESC DeviceDescription)
{
    ULONG Index;

    for (Index = 0; Index < DeviceDescription->CollectionDescLength; Index++)
    {
        if (DeviceDescription->CollectionDesc[Index].UsagePage ==
                HID_USAGE_PAGE_DIGITIZER &&
            DeviceDescription->CollectionDesc[Index].Usage ==
                HID_USAGE_DIGITIZER_TOUCH_PAD)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
NTSTATUS
HidClassFDO_ConfigurePrecisionTouchpad(
    IN PDEVICE_OBJECT DeviceObject)
{
    PHIDCLASS_FDO_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PHIDP_DEVICE_DESC DeviceDescription =
        &DeviceExtension->Common.DeviceDescription;
    PHIDP_COLLECTION_DESC Collection;
    PHIDP_REPORT_IDS ReportDescription = NULL;
    HIDP_VALUE_CAPS ModeCaps;
    HID_XFER_PACKET XferPacket;
    PUCHAR ReportBuffer;
    USHORT CapsLength;
    NTSTATUS Status;
    ULONG Index;

    if (!HidClassFDO_HasPrecisionTouchpad(DeviceDescription))
        return STATUS_NOT_FOUND;

    Collection = NULL;
    for (Index = 0; Index < DeviceDescription->CollectionDescLength; Index++)
    {
        if (DeviceDescription->CollectionDesc[Index].UsagePage ==
                HID_USAGE_PAGE_DIGITIZER &&
            DeviceDescription->CollectionDesc[Index].Usage ==
                HID_USAGE_DIGITIZER_DEVICE_CONFIGURATION)
        {
            Collection = &DeviceDescription->CollectionDesc[Index];
            break;
        }
    }
    if (Collection == NULL)
        return STATUS_NOT_SUPPORTED;

    CapsLength = 1;
    Status = HidP_GetSpecificValueCaps(HidP_Feature,
                                       HID_USAGE_PAGE_DIGITIZER,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_DIGITIZER_DEVICE_MODE,
                                       &ModeCaps,
                                       &CapsLength,
                                       Collection->PreparsedData);
    if (Status != HIDP_STATUS_SUCCESS || CapsLength != 1 ||
        ModeCaps.ReportID == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    for (Index = 0; Index < DeviceDescription->ReportIDsLength; Index++)
    {
        if (DeviceDescription->ReportIDs[Index].CollectionNumber ==
                Collection->CollectionNumber &&
            DeviceDescription->ReportIDs[Index].ReportID ==
                ModeCaps.ReportID &&
            DeviceDescription->ReportIDs[Index].FeatureLength != 0)
        {
            ReportDescription = &DeviceDescription->ReportIDs[Index];
            break;
        }
    }
    if (ReportDescription == NULL)
        return STATUS_NOT_SUPPORTED;

    ReportBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         ReportDescription->FeatureLength,
                                         HIDCLASS_TAG);
    if (ReportBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ReportBuffer, ReportDescription->FeatureLength);
    ReportBuffer[0] = ModeCaps.ReportID;
    Status = HidP_SetUsageValue(HidP_Feature,
                                HID_USAGE_PAGE_DIGITIZER,
                                ModeCaps.LinkCollection,
                                HID_USAGE_DIGITIZER_DEVICE_MODE,
                                3,
                                Collection->PreparsedData,
                                (PCHAR)ReportBuffer,
                                ReportDescription->FeatureLength);
    if (Status == HIDP_STATUS_SUCCESS)
    {
        RtlZeroMemory(&XferPacket, sizeof(XferPacket));
        XferPacket.reportBuffer = ReportBuffer;
        XferPacket.reportBufferLen = ReportDescription->FeatureLength;
        XferPacket.reportId = ModeCaps.ReportID;
        Status = HidClassFDO_SendReportRequest(DeviceObject,
                                               IOCTL_HID_SET_FEATURE,
                                               &XferPacket);
    }

    ExFreePoolWithTag(ReportBuffer, HIDCLASS_TAG);
    return Status;
}


NTSTATUS
HidClassFDO_StartDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    NTSTATUS Status;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;

    //
    // get device extension
    //
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // query capabilities
    //
    Status = HidClassFDO_QueryCapabilities(DeviceObject, &FDODeviceExtension->Capabilities);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[HIDCLASS] Failed to retrieve capabilities %x\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    //
    // let's start the lower device too
    //
    IoSkipCurrentIrpStackLocation(Irp);
    Status = HidClassFDO_DispatchRequestSynchronous(DeviceObject, Irp);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[HIDCLASS] Failed to start lower device with %x\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    //
    // let's get the descriptors
    //
    Status = HidClassFDO_GetDescriptors(DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[HIDCLASS] Failed to retrieve the descriptors %x\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    //
    // now get the the collection description
    //
    Status = HidP_GetCollectionDescription(FDODeviceExtension->ReportDescriptor, FDODeviceExtension->HidDescriptor.DescriptorList[0].wReportLength, NonPagedPool, &FDODeviceExtension->Common.DeviceDescription);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[HIDCLASS] Failed to retrieve the collection description %x\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    Status = HidClassFDO_ConfigurePrecisionTouchpad(DeviceObject);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
    {
        DPRINT1("[HIDCLASS] Precision Touchpad input mode failed with %x\n",
                Status);
    }

    //
    // complete request
    //
    Status = STATUS_SUCCESS;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
HidClassFDO_RemoveDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    NTSTATUS Status;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;

    //
    // get device extension
    //
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* FIXME cleanup */

    //
    // dispatch to minidriver
    //
    IoSkipCurrentIrpStackLocation(Irp);
    Status = HidClassFDO_DispatchRequestSynchronous(DeviceObject, Irp);

    //
    // complete request
    //
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    //
    // detach and delete device
    //
    IoDetachDevice(FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject);
    IoDeleteDevice(DeviceObject);

    return Status;
}

NTSTATUS
HidClassFDO_CopyDeviceRelations(
    IN PDEVICE_OBJECT DeviceObject,
    OUT PDEVICE_RELATIONS *OutRelations)
{
    PDEVICE_RELATIONS DeviceRelations;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    ULONG Index;

    //
    // get device extension
    //
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // allocate result
    //
    DeviceRelations = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(DEVICE_RELATIONS) + (FDODeviceExtension->DeviceRelations->Count - 1) * sizeof(PDEVICE_OBJECT),
                                            HIDCLASS_TAG);
    if (!DeviceRelations)
    {
        //
        // no memory
        //
        *OutRelations = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // copy device objects
    //
    for (Index = 0; Index < FDODeviceExtension->DeviceRelations->Count; Index++)
    {
        //
        // reference pdo
        //
        ObReferenceObject(FDODeviceExtension->DeviceRelations->Objects[Index]);

        //
        // store object
        //
        DeviceRelations->Objects[Index] = FDODeviceExtension->DeviceRelations->Objects[Index];
    }

    //
    // set object count
    //
    DeviceRelations->Count = FDODeviceExtension->DeviceRelations->Count;

    //
    // store result
    //
    *OutRelations = DeviceRelations;
    return STATUS_SUCCESS;
}

NTSTATUS
HidClassFDO_DeviceRelations(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PDEVICE_RELATIONS DeviceRelations;

    //
    // get device extension
    //
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // get current irp stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    //
    // check relations type
    //
    if (IoStack->Parameters.QueryDeviceRelations.Type != BusRelations)
    {
        //
        // only bus relations are handled
        //
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(FDODeviceExtension->Common.HidDeviceExtension.NextDeviceObject, Irp);
    }

    if (FDODeviceExtension->DeviceRelations == NULL)
    {
        //
        // time to create the pdos
        //
        Status = HidClassPDO_CreatePDO(DeviceObject, &FDODeviceExtension->DeviceRelations);
        if (!NT_SUCCESS(Status))
        {
            //
            // failed
            //
            DPRINT1("[HIDCLASS] HidClassPDO_CreatePDO failed with %x\n", Status);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
        //
        // sanity check
        //
        ASSERT(FDODeviceExtension->DeviceRelations->Count > 0);
    }

    //
    // now copy device relations
    //
    Status = HidClassFDO_CopyDeviceRelations(DeviceObject, &DeviceRelations);
    //
    // store result
    //
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;

    //
    // complete request
    //
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
HidClassFDO_PnP(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;
    NTSTATUS Status;

    //
    // get device extension
    //
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    //
    // get current irp stack location
    //
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
             return HidClassFDO_StartDevice(DeviceObject, Irp);
        }
        case IRP_MN_REMOVE_DEVICE:
        {
             return HidClassFDO_RemoveDevice(DeviceObject, Irp);
        }
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
             return HidClassFDO_DeviceRelations(DeviceObject, Irp);
        }
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        {
            //
            // set status to success and fall through
            //
            Irp->IoStatus.Status = STATUS_SUCCESS;
        }
        default:
        {
            //
            // dispatch to mini driver
            //
           IoCopyCurrentIrpStackLocationToNext(Irp);
           Status = HidClassFDO_DispatchRequest(DeviceObject, Irp);
           return Status;
        }
    }
}
