/*
 * PROJECT:     ReactOS Universal Serial Bus Bulk Enhanced Host Controller Interface
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/usb/usbccgp/fdo.c
 * PURPOSE:     USB  device driver.
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 *              Cameron Gutman
 */

#include "usbccgp.h"

#define NDEBUG
#include <debug.h>

static VOID
USBCCGP_FreeUnicodeString(
    IN OUT PUNICODE_STRING String,
    IN BOOLEAN Tagged)
{
    if (!String || !String->Buffer)
        return;

    if (String->Length == 0 && String->MaximumLength == sizeof(WCHAR))
    {
        String->Buffer = NULL;
        String->MaximumLength = 0;
        return;
    }

    if (Tagged)
        ExFreePoolWithTag(String->Buffer, USBCCPG_TAG);
    else
        ExFreePool(String->Buffer);

    String->Buffer = NULL;
    String->Length = 0;
    String->MaximumLength = 0;
}

static VOID
USBCCGP_FreeFunctionDescriptors(
    IN OUT PFDO_DEVICE_EXTENSION FDODeviceExtension)
{
    ULONG Index;
    BOOLEAN Tagged;

    if (!FDODeviceExtension->FunctionDescriptor)
        return;

    Tagged = FDODeviceExtension->FunctionDescriptorOwned ? TRUE : FALSE;

    for (Index = 0; Index < FDODeviceExtension->FunctionDescriptorCount; Index++)
    {
        if (FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList)
        {
            if (Tagged)
                ExFreePoolWithTag(FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList, USBCCPG_TAG);
            else
                ExFreePool(FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList);
            FDODeviceExtension->FunctionDescriptor[Index].InterfaceDescriptorList = NULL;
        }

        USBCCGP_FreeUnicodeString(&FDODeviceExtension->FunctionDescriptor[Index].HardwareId, Tagged);
        USBCCGP_FreeUnicodeString(&FDODeviceExtension->FunctionDescriptor[Index].CompatibleId, Tagged);
        USBCCGP_FreeUnicodeString(&FDODeviceExtension->FunctionDescriptor[Index].FunctionDescription, Tagged);
    }

    if (Tagged)
        ExFreePoolWithTag(FDODeviceExtension->FunctionDescriptor, USBCCPG_TAG);
    else
        ExFreePool(FDODeviceExtension->FunctionDescriptor);

    FDODeviceExtension->FunctionDescriptor = NULL;
    FDODeviceExtension->FunctionDescriptorCount = 0;
    FDODeviceExtension->FunctionDescriptorOwned = FALSE;
}

static VOID
USBCCGP_FreeInterfaceList(
    IN OUT PFDO_DEVICE_EXTENSION FDODeviceExtension)
{
    ULONG Index;

    if (!FDODeviceExtension->InterfaceList)
        return;

    for (Index = 0; Index < FDODeviceExtension->InterfaceListCount; Index++)
    {
        if (FDODeviceExtension->InterfaceList[Index].Interface)
        {
            FreeItem(FDODeviceExtension->InterfaceList[Index].Interface);
            FDODeviceExtension->InterfaceList[Index].Interface = NULL;
        }
    }

    FreeItem(FDODeviceExtension->InterfaceList);
    FDODeviceExtension->InterfaceList = NULL;
    FDODeviceExtension->InterfaceListCount = 0;
    FDODeviceExtension->ConfigurationHandle = NULL;
}

static VOID
USBCCGP_DrainResetCycleQueue(
    IN OUT PFDO_DEVICE_EXTENSION FDODeviceExtension,
    IN PLIST_ENTRY ListHead,
    IN NTSTATUS Status)
{
    LIST_ENTRY TempList;
    PLIST_ENTRY Entry;
    PIRP ListIrp;
    KIRQL CancelIrql;

    InitializeListHead(&TempList);

    IoAcquireCancelSpinLock(&CancelIrql);
    KeAcquireSpinLockAtDpcLevel(&FDODeviceExtension->Lock);

    while (!IsListEmpty(ListHead))
    {
        Entry = RemoveHeadList(ListHead);
        ListIrp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        ListIrp->Tail.Overlay.DriverContext[0] = NULL;
        IoSetCancelRoutine(ListIrp, NULL);
        InsertTailList(&TempList, Entry);
    }

    KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->Lock);
    IoReleaseCancelSpinLock(CancelIrql);

    while (!IsListEmpty(&TempList))
    {
        Entry = RemoveHeadList(&TempList);
        ListIrp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        ListIrp->IoStatus.Status = Status;
        IoCompleteRequest(ListIrp, IO_NO_INCREMENT);
    }
}

static VOID
USBCCGP_FdoCleanup(
    IN PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    ULONG Index;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    USBCCGP_DrainResetCycleQueue(FDODeviceExtension, &FDODeviceExtension->ResetPortListHead, STATUS_CANCELLED);
    USBCCGP_DrainResetCycleQueue(FDODeviceExtension, &FDODeviceExtension->CyclePortListHead, STATUS_CANCELLED);

    if (FDODeviceExtension->ChildPDO)
    {
        for (Index = 0; Index < FDODeviceExtension->FunctionDescriptorCount; Index++)
        {
            if (FDODeviceExtension->ChildPDO[Index])
            {
                IoDeleteDevice(FDODeviceExtension->ChildPDO[Index]);
                FDODeviceExtension->ChildPDO[Index] = NULL;
            }
        }

        FreeItem(FDODeviceExtension->ChildPDO);
        FDODeviceExtension->ChildPDO = NULL;
    }

    USBCCGP_FreeFunctionDescriptors(FDODeviceExtension);
    USBCCGP_FreeInterfaceList(FDODeviceExtension);

    /* Dereference bus interface if we acquired a reference */
    if (FDODeviceExtension->BusInterfaceReferenced)
    {
        if (FDODeviceExtension->BusInterface.InterfaceDereference)
        {
            FDODeviceExtension->BusInterface.InterfaceDereference(FDODeviceExtension->BusInterface.Context);
        }
    }

    RtlZeroMemory(&FDODeviceExtension->BusInterface, sizeof(FDODeviceExtension->BusInterface));
    FDODeviceExtension->BusInterfaceReferenced = FALSE;

    if (FDODeviceExtension->ConfigurationDescriptor)
    {
        FreeItem(FDODeviceExtension->ConfigurationDescriptor);
        FDODeviceExtension->ConfigurationDescriptor = NULL;
    }

    if (FDODeviceExtension->DeviceDescriptor)
    {
        FreeItem(FDODeviceExtension->DeviceDescriptor);
        FDODeviceExtension->DeviceDescriptor = NULL;
    }
}

VOID
NTAPI
USBCCGP_CancelResetCycleIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    KIRQL OldLevel;
    PLIST_ENTRY ListHead;

    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&FDODeviceExtension->Lock, &OldLevel);
    ListHead = (PLIST_ENTRY)Irp->Tail.Overlay.DriverContext[0];
    if (ListHead)
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        Irp->Tail.Overlay.DriverContext[0] = NULL;
    }
    KeReleaseSpinLock(&FDODeviceExtension->Lock, OldLevel);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

NTSTATUS
NTAPI
FDO_QueryCapabilitiesCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    /* Set event */
    KeSetEvent((PRKEVENT)Context, 0, FALSE);

    /* Completion is done in the HidClassFDO_QueryCapabilities routine */
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
FDO_QueryCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PDEVICE_CAPABILITIES Capabilities)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* Init event */
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /* Now allocate the irp */
    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        /* No memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Get next stack location */
    IoStack = IoGetNextIrpStackLocation(Irp);

    /* Init stack location */
    IoStack->MajorFunction = IRP_MJ_PNP;
    IoStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
    IoStack->Parameters.DeviceCapabilities.Capabilities = Capabilities;

    /* Set completion routine */
    IoSetCompletionRoutine(Irp,
                           FDO_QueryCapabilitiesCompletionRoutine,
                           (PVOID)&Event,
                           TRUE,
                           TRUE,
                           TRUE);

    /* Init capabilities */
    RtlZeroMemory(Capabilities, sizeof(DEVICE_CAPABILITIES));
    Capabilities->Size = sizeof(DEVICE_CAPABILITIES);
    Capabilities->Version = 1; // FIXME hardcoded constant
    Capabilities->Address = MAXULONG;
    Capabilities->UINumber = MAXULONG;

    /* Pnp irps have default completion code */
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    /* Call lower device */
    Status = IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        /* Wait for completion */
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    /* Get status */
    Status = Irp->IoStatus.Status;

    /* Complete request */
    IoFreeIrp(Irp);

    /* Done */
    return Status;
}

NTSTATUS
FDO_DeviceRelations(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    ULONG DeviceCount = 0;
    ULONG Index;
    PDEVICE_RELATIONS DeviceRelations;
    PIO_STACK_LOCATION IoStack;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    /* Get current irp stack location */
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    /* Check if relation type is BusRelations */
    if (IoStack->Parameters.QueryDeviceRelations.Type != BusRelations)
    {
        /* FDO always only handles bus relations */
        return STATUS_SUCCESS;
    }

    /* Go through array and count device objects */
    for(Index = 0; Index < FDODeviceExtension->FunctionDescriptorCount; Index++)
    {
        if (FDODeviceExtension->ChildPDO[Index])
        {
            /* Child pdo */
            DeviceCount++;
        }
    }

    /* Allocate device relations */
    DeviceRelations = (PDEVICE_RELATIONS)AllocateItem(PagedPool,
                                                      sizeof(DEVICE_RELATIONS) + (DeviceCount > 1 ? (DeviceCount-1) * sizeof(PDEVICE_OBJECT) : 0));
    if (!DeviceRelations)
    {
        /* No memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Add device objects */
    for(Index = 0; Index < FDODeviceExtension->FunctionDescriptorCount; Index++)
    {
        if (FDODeviceExtension->ChildPDO[Index])
        {
            /* Store child pdo */
            DeviceRelations->Objects[DeviceRelations->Count] = FDODeviceExtension->ChildPDO[Index];

            /* Add reference */
            ObReferenceObject(FDODeviceExtension->ChildPDO[Index]);

            /* Increment count */
            DeviceRelations->Count++;
        }
    }

    /* Store result */
    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;
    Irp->IoStatus.Status = STATUS_SUCCESS;

    /* Request completed successfully */
    return STATUS_SUCCESS;
}

NTSTATUS
FDO_CreateChildPdo(
    IN PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT PDODeviceObject;
    PPDO_DEVICE_EXTENSION PDODeviceExtension;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    ULONG Index;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* Lets create array for the child PDO */
    FDODeviceExtension->ChildPDO = AllocateItem(NonPagedPool,
                                                sizeof(PDEVICE_OBJECT) * FDODeviceExtension->FunctionDescriptorCount);
    if (!FDODeviceExtension->ChildPDO)
    {
        /* No memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Create pdo for each function */
    for (Index = 0; Index < FDODeviceExtension->FunctionDescriptorCount; Index++)
    {
        if (FDODeviceExtension->FunctionDescriptor[Index].NumberOfInterfaces == 0)
        {
            // Ignore invalid devices
            DPRINT("[USBCCGP] Found descriptor with 0 interfaces\n");
            continue;
        }

        /* Create the PDO */
        Status = IoCreateDevice(FDODeviceExtension->DriverObject,
                                sizeof(PDO_DEVICE_EXTENSION),
                                NULL,
                                FILE_DEVICE_USB,
                                FILE_AUTOGENERATED_DEVICE_NAME,
                                FALSE,
                                &PDODeviceObject);
        if (!NT_SUCCESS(Status))
        {
            /* Failed to create device object */
            DPRINT1("IoCreateDevice failed with %x\n", Status);
            return Status;
        }

        /* Store in array */
        FDODeviceExtension->ChildPDO[Index] = PDODeviceObject;

        /* Get device extension */
        PDODeviceExtension = (PPDO_DEVICE_EXTENSION)PDODeviceObject->DeviceExtension;
        RtlZeroMemory(PDODeviceExtension, sizeof(PDO_DEVICE_EXTENSION));

        /* Init device extension */
        PDODeviceExtension->Common.IsFDO = FALSE;
        PDODeviceExtension->FunctionDescriptor = &FDODeviceExtension->FunctionDescriptor[Index];
        PDODeviceExtension->NextDeviceObject = DeviceObject;
        PDODeviceExtension->FunctionIndex = Index;
        PDODeviceExtension->FDODeviceExtension = FDODeviceExtension;
        PDODeviceExtension->InterfaceList = FDODeviceExtension->InterfaceList;
        PDODeviceExtension->InterfaceListCount = FDODeviceExtension->InterfaceListCount;
        PDODeviceExtension->ConfigurationHandle = FDODeviceExtension->ConfigurationHandle;
        PDODeviceExtension->ConfigurationDescriptor = FDODeviceExtension->ConfigurationDescriptor;
        RtlCopyMemory(&PDODeviceExtension->Capabilities, &FDODeviceExtension->Capabilities, sizeof(DEVICE_CAPABILITIES));
        RtlCopyMemory(&PDODeviceExtension->DeviceDescriptor, FDODeviceExtension->DeviceDescriptor, sizeof(USB_DEVICE_DESCRIPTOR));

        /* Patch the stack size */
        PDODeviceObject->StackSize = DeviceObject->StackSize + 1;

        /* Set device flags */
        PDODeviceObject->Flags |= DO_DIRECT_IO | DO_MAP_IO_BUFFER;

        /* Device is initialized */
        PDODeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    }

    /* Done */
    return STATUS_SUCCESS;
}

NTSTATUS
FDO_StartDevice(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    NTSTATUS Status;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    BOOLEAN InterfaceReferenced = FALSE;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    DPRINT("[USBCCGP] FDO_StartDevice: starting lower device\n");

    /* First start lower device */
    if (IoForwardIrpSynchronously(FDODeviceExtension->NextDeviceObject, Irp))
    {
        Status = Irp->IoStatus.Status;
    }
    else
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(Status))
    {
        /* Failed to start lower device */
        DPRINT1("FDO_StartDevice lower device failed to start with %x\n", Status);
        USBCCGP_FdoCleanup(DeviceObject);
        return Status;
    }

    DPRINT("[USBCCGP] FDO_StartDevice: lower device started, getting descriptors\n");

    /* Get descriptors */
    Status = USBCCGP_GetDescriptors(DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* Failed to start lower device */
        DPRINT1("FDO_StartDevice failed to get descriptors with %x\n", Status);
        USBCCGP_FdoCleanup(DeviceObject);
        return Status;
    }

    DPRINT("[USBCCGP] FDO_StartDevice: descriptors obtained, querying capabilities\n");

    /* Get capabilities */
    Status = FDO_QueryCapabilities(DeviceObject,
                                   &FDODeviceExtension->Capabilities);
    if (!NT_SUCCESS(Status))
    {
        /* Failed to start lower device */
        DPRINT1("FDO_StartDevice failed to get capabilities with %x\n", Status);
        USBCCGP_FdoCleanup(DeviceObject);
        return Status;
    }

    DPRINT("[USBCCGP] FDO_StartDevice: capabilities obtained, selecting configuration\n");

    /* Now select the configuration */
    Status = USBCCGP_SelectConfiguration(DeviceObject, FDODeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        /* Failed to select interface */
        DPRINT1("FDO_StartDevice failed to select configuration with %x\n", Status);
        USBCCGP_FdoCleanup(DeviceObject);
        return Status;
    }

    DPRINT("[USBCCGP] FDO_StartDevice: configuration selected, querying bus interface\n");

    /* Query bus interface */
    Status = USBCCGP_QueryInterface(FDODeviceExtension->NextDeviceObject,
                                    &FDODeviceExtension->BusInterface);
    if (NT_SUCCESS(Status) &&
        FDODeviceExtension->BusInterface.InterfaceReference)
    {
        FDODeviceExtension->BusInterface.InterfaceReference(FDODeviceExtension->BusInterface.Context);
        InterfaceReferenced = TRUE;
    }
    FDODeviceExtension->BusInterfaceReferenced = InterfaceReferenced;

    DPRINT("[USBCCGP] FDO_StartDevice: bus interface queried, enumerating functions\n");

    /* Now enumerate the functions */
    Status = USBCCGP_EnumerateFunctions(DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* Failed to enumerate functions */
        DPRINT1("Failed to enumerate functions with %x\n", Status);
        USBCCGP_FdoCleanup(DeviceObject);
        return Status;
    }

    /* Sanity checks */
    ASSERT(FDODeviceExtension->FunctionDescriptorCount);
    ASSERT(FDODeviceExtension->FunctionDescriptor);
    DumpFunctionDescriptor(FDODeviceExtension->FunctionDescriptor,
                           FDODeviceExtension->FunctionDescriptorCount);

    /* Now create the pdo */
    Status = FDO_CreateChildPdo(DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* Failed */
        DPRINT1("FDO_CreateChildPdo failed with %x\n", Status);
        USBCCGP_FdoCleanup(DeviceObject);
        return Status;
    }

    /* Inform pnp manager of new device objects */
    IoInvalidateDeviceRelations(FDODeviceExtension->PhysicalDeviceObject,
                                BusRelations);

    /* Done */
    DPRINT("[USBCCGP] FDO initialized successfully\n");
    return Status;
}

NTSTATUS
FDO_CloseConfiguration(
    IN PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PURB Urb;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PUSBD_INTERFACE_LIST_ENTRY TempList;
    ULONG ListSize;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* Nothing to do if we're not configured */
    if (FDODeviceExtension->ConfigurationDescriptor == NULL ||
        FDODeviceExtension->InterfaceList == NULL)
    {
        return STATUS_SUCCESS;
    }

    /*
     * USBD_CreateConfigurationRequestEx overwrites InterfaceList[i].Interface
     * with pointers into the URB it creates. We must use a temporary copy so
     * the real Interface pointers (separately allocated) are not clobbered.
     * Otherwise USBCCGP_FreeInterfaceList would try to free URB-internal
     * pointers, causing misaligned-pointer / double-free pool corruption.
     */
    ListSize = sizeof(USBD_INTERFACE_LIST_ENTRY) * (FDODeviceExtension->InterfaceListCount + 1);
    TempList = AllocateItem(NonPagedPool, ListSize);
    if (!TempList)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(TempList, FDODeviceExtension->InterfaceList, ListSize);

    /* Now allocate the urb */
    Urb = USBD_CreateConfigurationRequestEx(FDODeviceExtension->ConfigurationDescriptor,
                                            TempList);
    FreeItem(TempList);
    if (!Urb)
    {
        /* No memory */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Clear configuration descriptor to make it an unconfigure request */
    Urb->UrbSelectConfiguration.ConfigurationDescriptor = NULL;

    /* Submit urb */
    Status = USBCCGP_SyncUrbRequest(FDODeviceExtension->NextDeviceObject, Urb);
    if (!NT_SUCCESS(Status))
    {
        /* Failed to set configuration */
        DPRINT1("USBCCGP_SyncUrbRequest failed to unconfigure device %x\n", Status);
    }

    ExFreePool(Urb);
    return Status;
}


NTSTATUS
FDO_HandlePnp(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);


    /* Get stack location */
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DPRINT("[USBCCGP] PnP Minor %x\n", IoStack->MinorFunction);
    switch(IoStack->MinorFunction)
    {
        case IRP_MN_REMOVE_DEVICE:
        {
            /* Unconfigure device */
            DPRINT("[USBCCGP] FDO IRP_MN_REMOVE\n");
            FDO_CloseConfiguration(DeviceObject);
            USBCCGP_FdoCleanup(DeviceObject);

            /* Forward remove IRP down the stack */
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);

            /* Detach from the device stack */
            IoDetachDevice(FDODeviceExtension->NextDeviceObject);

            /* Delete the device object */
            IoDeleteDevice(DeviceObject);

            /* The lower driver owns IRP completion; do not touch it again. */
            return Status;
        }
        case IRP_MN_SURPRISE_REMOVAL:
        {
            FDO_CloseConfiguration(DeviceObject);
            USBCCGP_FdoCleanup(DeviceObject);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
        }
        case IRP_MN_START_DEVICE:
        {
            /* Start the device */
            Status = FDO_StartDevice(DeviceObject, Irp);
            break;
        }
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            /* Handle device relations */
            Status = FDO_DeviceRelations(DeviceObject, Irp);
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            /* Forward irp to next device object */
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
        }
        case IRP_MN_QUERY_CAPABILITIES:
        {
            /* Copy capabilities */
            RtlCopyMemory(IoStack->Parameters.DeviceCapabilities.Capabilities,
                          &FDODeviceExtension->Capabilities,
                          sizeof(DEVICE_CAPABILITIES));
            Status = STATUS_UNSUCCESSFUL;

            if (IoForwardIrpSynchronously(FDODeviceExtension->NextDeviceObject, Irp))
            {
                Status = Irp->IoStatus.Status;
                if (NT_SUCCESS(Status))
                {
                    IoStack->Parameters.DeviceCapabilities.Capabilities->SurpriseRemovalOK = TRUE;
                }
            }
            break;
       }
        case IRP_MN_STOP_DEVICE:
        {
            /* Close configuration and clean up resources */
            FDO_CloseConfiguration(DeviceObject);
            USBCCGP_FdoCleanup(DeviceObject);

            /* Forward stop IRP to lower driver */
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
        }
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        {
            /* Sure */
            Irp->IoStatus.Status = STATUS_SUCCESS;

            /* Forward irp to next device object */
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
        }
       default:
       {
            /* Forward irp to next device object */
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
       }

    }

    /* Complete request */
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
FDO_HandleResetCyclePort(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;
    PLIST_ENTRY ListHead;
    PUCHAR ResetActive;
    KIRQL OldLevel;
    KIRQL CancelIrql;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* Get stack location */
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DPRINT("FDO_HandleResetCyclePort IOCTL %x\n", IoStack->Parameters.DeviceIoControl.IoControlCode);

    if (IoStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_RESET_PORT)
    {
        /* Use reset port list */
        ListHead = &FDODeviceExtension->ResetPortListHead;
        ResetActive = &FDODeviceExtension->ResetPortActive;
    }
    else
    {
        /* Use cycle port list */
        ListHead = &FDODeviceExtension->CyclePortListHead;
        ResetActive = &FDODeviceExtension->CyclePortActive;
    }

    /* Acquire lock */
    KeAcquireSpinLock(&FDODeviceExtension->Lock, &OldLevel);

    if (*ResetActive)
    {
        KeReleaseSpinLock(&FDODeviceExtension->Lock, OldLevel);

        IoAcquireCancelSpinLock(&CancelIrql);
        KeAcquireSpinLockAtDpcLevel(&FDODeviceExtension->Lock);

        if (Irp->Cancel)
        {
            KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->Lock);
            IoReleaseCancelSpinLock(CancelIrql);
            Irp->IoStatus.Status = STATUS_CANCELLED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_CANCELLED;
        }

        /* Insert into pending list */
        Irp->Tail.Overlay.DriverContext[0] = ListHead;
        InsertTailList(ListHead, &Irp->Tail.Overlay.ListEntry);
        IoSetCancelRoutine(Irp, USBCCGP_CancelResetCycleIrp);

        /* Mark irp pending */
        IoMarkIrpPending(Irp);
        Status = STATUS_PENDING;

        KeReleaseSpinLockFromDpcLevel(&FDODeviceExtension->Lock);
        IoReleaseCancelSpinLock(CancelIrql);
    }
    else
    {
        /* Mark reset active */
        *ResetActive = TRUE;

        /* Release lock */
        KeReleaseSpinLock(&FDODeviceExtension->Lock, OldLevel);

        /* Forward request synchronized */
        NT_VERIFY(IoForwardIrpSynchronously(FDODeviceExtension->NextDeviceObject, Irp));

        /* Capture actual completion status from lower driver */
        Status = Irp->IoStatus.Status;

        /* Reacquire lock */
        KeAcquireSpinLock(&FDODeviceExtension->Lock, &OldLevel);

        /* Mark reset as completed */
        *ResetActive = FALSE;

        /* Release lock */
        KeReleaseSpinLock(&FDODeviceExtension->Lock, OldLevel);

        /* Complete pending irps with the actual status */
        USBCCGP_DrainResetCycleQueue(FDODeviceExtension, ListHead, Status);
    }

    return Status;
}



NTSTATUS
FDO_HandleInternalDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* Get stack location */
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    if (IoStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_RESET_PORT ||
        IoStack->Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_CYCLE_PORT)
    {
        /* Handle reset / cycle ports */
        Status = FDO_HandleResetCyclePort(DeviceObject, Irp);
        DPRINT("FDO_HandleResetCyclePort Status %x\n", Status);
        if (Status != STATUS_PENDING)
        {
            /* Complete request */
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
        }
        return Status;
    }

    /* Forward and forget request */
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
}

NTSTATUS
FDO_HandleSystemControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    /* Get device extension */
    FDODeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* Forward and forget request */
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
}

NTSTATUS
FDO_Dispatch(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PFDO_DEVICE_EXTENSION FDODeviceExtension;

    /* Get device extension */
    FDODeviceExtension = DeviceObject->DeviceExtension;
    ASSERT(FDODeviceExtension->Common.IsFDO);

    /* Get stack location */
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    switch(IoStack->MajorFunction)
    {
        case IRP_MJ_PNP:
            return FDO_HandlePnp(DeviceObject, Irp);
        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            return FDO_HandleInternalDeviceControl(DeviceObject, Irp);
        case IRP_MJ_POWER:
            PoStartNextPowerIrp(Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(FDODeviceExtension->NextDeviceObject, Irp);
        case IRP_MJ_SYSTEM_CONTROL:
            return FDO_HandleSystemControl(DeviceObject, Irp);
        default:
            DPRINT1("[USBCCGP] FDO_Dispatch: unhandled major function %x\n", IoStack->MajorFunction);
            Status = Irp->IoStatus.Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
    }

}
