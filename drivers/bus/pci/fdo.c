/*
 * PROJECT:         ReactOS PCI bus driver
 * FILE:            fdo.c
 * PURPOSE:         PCI device object dispatch routines
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 * UPDATE HISTORY:
 *      10-09-2001  CSH  Created
 */

#include "pci.h"

#define NDEBUG
#include <debug.h>

/*** PRIVATE *****************************************************************/

static NTSTATUS
FdoLocateChildDevice(
    PPCI_DEVICE *Device,
    PFDO_DEVICE_EXTENSION DeviceExtension,
    ULONG BusNumber,
    PCI_SLOT_NUMBER SlotNumber,
    PPCI_COMMON_CONFIG PciConfig)
{
    PLIST_ENTRY CurrentEntry;
    PPCI_DEVICE CurrentDevice;

    DPRINT("Called\n");

    CurrentEntry = DeviceExtension->DeviceListHead.Flink;
    while (CurrentEntry != &DeviceExtension->DeviceListHead)
    {
        CurrentDevice = CONTAINING_RECORD(CurrentEntry, PCI_DEVICE, ListEntry);

        /* If both vendor ID and device ID match, it is the same device */
        if ((CurrentDevice->BusNumber == BusNumber) &&
            (PciConfig->VendorID == CurrentDevice->PciConfig.VendorID) &&
            (PciConfig->DeviceID == CurrentDevice->PciConfig.DeviceID) &&
            (SlotNumber.u.AsULONG == CurrentDevice->SlotNumber.u.AsULONG))
        {
            *Device = CurrentDevice;
            DPRINT("Done\n");
            return STATUS_SUCCESS;
        }

        CurrentEntry = CurrentEntry->Flink;
    }

    *Device = NULL;
    DPRINT("Done\n");
    return STATUS_UNSUCCESSFUL;
}

static NTSTATUS
FdoEnumerateDevices(
    PDEVICE_OBJECT DeviceObject)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PCI_COMMON_CONFIG PciConfig;
    PPCI_DEVICE Device;
    PCI_SLOT_NUMBER SlotNumber;
    ULONG DeviceNumber;
    ULONG FunctionNumber;
    ULONG Bus;
    ULONG Size;
    NTSTATUS Status;

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DPRINT("PCI: FdoEnumerateDevices called (bus %lu-%lu)\n",
           DeviceExtension->BusRangeStart, DeviceExtension->BusRangeEnd);

    DeviceExtension->DeviceListCount = 0;

    /* Enumerate devices across the root's bus window */
    for (Bus = DeviceExtension->BusRangeStart; Bus <= DeviceExtension->BusRangeEnd; Bus++)
    {
        if (!PciIsBusInRange(DeviceExtension, Bus))
        {
            continue;
        }

        /*
         * Early Bus Termination Optimization:
         *
         * Per PCI specification, if device 0 function 0 does not exist on a bus,
         * the entire bus is empty. This is because PCI-to-PCI bridges that create
         * subordinate buses must be enumerated starting from device 0.
         *
         * By checking device 0 function 0 first and skipping empty buses, we
         * reduce the scan to only populated buses, typically around 64 reads total.
         */
        SlotNumber.u.AsULONG = 0;
        RtlZeroMemory(&PciConfig, sizeof(PCI_COMMON_CONFIG));

        Size = HalGetBusData(PCIConfiguration,
                             Bus,
                             SlotNumber.u.AsULONG,
                             &PciConfig,
                             PCI_COMMON_HDR_LENGTH);

        if (Size != PCI_COMMON_HDR_LENGTH ||
            PciConfig.VendorID == PCI_INVALID_VENDORID ||
            PciConfig.VendorID == 0)
        {
            /*
             * No device at slot 0 function 0.
             *
             * NOTE: The PCI specification states that if device 0 does not exist,
             * the entire bus is empty. However, some hypervisors (like VirtualBox)
             * do NOT follow this convention and may have devices starting at
             * non-zero device numbers. We cannot use the early bus termination
             * optimization in these cases.
             *
             * For compatibility, we only skip the bus if we're scanning a
             * secondary/subordinate bus (bus > 0). For bus 0 (root bus), we
             * must scan all device numbers because the root complex may have
             * integrated devices at any slot.
             */
            if (Bus > 0)
            {
                DPRINT("Bus %lu: No device at slot 0, skipping entire bus\n", Bus);
                continue;
            }
            DPRINT1("PCI: Bus %lu slot 0 empty (Size=%lu VendorID=0x%04hx), but scanning root bus anyway\n",
                   Bus, Size, PciConfig.VendorID);
        }

        /* Bus has at least one device, enumerate all devices on this bus */
        for (DeviceNumber = 0; DeviceNumber < PCI_MAX_DEVICES; DeviceNumber++)
        {
            SlotNumber.u.bits.DeviceNumber = DeviceNumber;
            for (FunctionNumber = 0; FunctionNumber < PCI_MAX_FUNCTION; FunctionNumber++)
            {
                SlotNumber.u.bits.FunctionNumber = FunctionNumber;

                /*
                 * For device 0 function 0, we already have the config data from
                 * the early bus termination check above. Reuse it to avoid a
                 * redundant config space read.
                 */
                if (DeviceNumber == 0 && FunctionNumber == 0)
                {
                    /* PciConfig already populated from the bus check above */
                }
                else
                {
                    RtlZeroMemory(&PciConfig,
                                  sizeof(PCI_COMMON_CONFIG));

                    Size = HalGetBusData(PCIConfiguration,
                                         Bus,
                                         SlotNumber.u.AsULONG,
                                         &PciConfig,
                                         PCI_COMMON_HDR_LENGTH);
                }

                if (Size != PCI_COMMON_HDR_LENGTH ||
                    PciConfig.VendorID == PCI_INVALID_VENDORID ||
                    PciConfig.VendorID == 0)
                {
                    if (FunctionNumber == 0)
                    {
                        /* No device at this slot, try next device number */
                        break;
                    }
                    else
                    {
                        /* No function at this number, try next function */
                        continue;
                    }
                }

                DPRINT1("PCI: Found Bus %1lu  Device %2lu  Func %1lu  VenID 0x%04hx  DevID 0x%04hx\n",
                       Bus,
                       DeviceNumber,
                       FunctionNumber,
                       PciConfig.VendorID,
                       PciConfig.DeviceID);

                Status = FdoLocateChildDevice(&Device, DeviceExtension, Bus, SlotNumber, &PciConfig);
                if (!NT_SUCCESS(Status))
                {
                    Device = ExAllocatePoolWithTag(NonPagedPool, sizeof(PCI_DEVICE), TAG_PCI);
                    if (!Device)
                    {
                        /* FIXME: Cleanup resources for already discovered devices */
                        return STATUS_INSUFFICIENT_RESOURCES;
                    }

                    RtlZeroMemory(Device,
                                  sizeof(PCI_DEVICE));

                    Device->BusNumber = Bus;

                    if (PciIsDeviceDebugging(Bus, SlotNumber))
                    {
                        Device->IsDebuggingDevice = TRUE;

                        /*
                         * ReactOS-specific: apply a hack
                         * to prevent driver installation for the debugging device.
                         * NOTE: Nothing to do for IEEE 1394 devices; NT5.1 and NT5.2
                         * support IEEE 1394 debugging.
                         *
                         * FIXME: We should set the device problem code
                         * CM_PROB_USED_BY_DEBUGGER instead.
                         */
                        if (PciConfig.BaseClass != PCI_CLASS_SERIAL_BUS_CTLR ||
                            PciConfig.SubClass != PCI_SUBCLASS_SB_IEEE1394)
                        {
                            PciConfig.VendorID = 0xDEAD;
                            PciConfig.DeviceID = 0xBEEF;
                        }
                    }

                    RtlCopyMemory(&Device->SlotNumber,
                                  &SlotNumber,
                                  sizeof(PCI_SLOT_NUMBER));

                    RtlCopyMemory(&Device->PciConfig,
                                  &PciConfig,
                                  sizeof(PCI_COMMON_CONFIG));

                    ExInterlockedInsertTailList(
                        &DeviceExtension->DeviceListHead,
                        &Device->ListEntry,
                        &DeviceExtension->DeviceListLock);
                }

                DeviceExtension->DeviceListCount++;

                /* Skip to next device if the current one is not a multifunction device */
                if ((FunctionNumber == 0) &&
                    ((PciConfig.HeaderType & 0x80) == 0))
                {
                    break;
                }
            }
        }
    }

    DPRINT("Done\n");

    return STATUS_SUCCESS;
}


static NTSTATUS
FdoQueryBusRelations(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION PdoDeviceExtension = NULL;
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_RELATIONS Relations;
    PLIST_ENTRY CurrentEntry;
    PPCI_DEVICE Device;
    NTSTATUS Status;
    BOOLEAN ErrorOccurred;
    NTSTATUS ErrorStatus;
    ULONG Size;
    ULONG i;

    UNREFERENCED_PARAMETER(IrpSp);

    DPRINT1("PCI: FdoQueryBusRelations called\n");

    ErrorStatus = STATUS_INSUFFICIENT_RESOURCES;

    Status = STATUS_SUCCESS;

    ErrorOccurred = FALSE;

    /* Acquire the global PCI lock to serialize bus enumeration */
    KeEnterCriticalRegion();
    KeWaitForSingleObject(&PciGlobalLock, Executive, KernelMode, FALSE, NULL);

    FdoEnumerateDevices(DeviceObject);

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DPRINT1("PCI: FdoQueryBusRelations enumerated %lu devices\n",
            DeviceExtension->DeviceListCount);

    if (Irp->IoStatus.Information)
    {
        /* FIXME: Another bus driver has already created a DEVICE_RELATIONS
                  structure so we must merge this structure with our own */
        DPRINT1("FIXME: leaking old bus relations\n");
    }

    Size = sizeof(DEVICE_RELATIONS) +
           sizeof(Relations->Objects) * (DeviceExtension->DeviceListCount - 1);
    Relations = ExAllocatePoolWithTag(PagedPool, Size, TAG_PCI);
    if (!Relations)
        return STATUS_INSUFFICIENT_RESOURCES;

    Relations->Count = DeviceExtension->DeviceListCount;

    i = 0;
    CurrentEntry = DeviceExtension->DeviceListHead.Flink;
    while (CurrentEntry != &DeviceExtension->DeviceListHead)
    {
        Device = CONTAINING_RECORD(CurrentEntry, PCI_DEVICE, ListEntry);

        PdoDeviceExtension = NULL;

        if (!Device->Pdo)
        {
            /* Create a physical device object for the
               device as it does not already have one */
            Status = IoCreateDevice(DeviceObject->DriverObject,
                                    sizeof(PDO_DEVICE_EXTENSION),
                                    NULL,
                                    FILE_DEVICE_CONTROLLER,
                                    FILE_AUTOGENERATED_DEVICE_NAME,
                                    FALSE,
                                    &Device->Pdo);
            if (!NT_SUCCESS(Status))
            {
                DPRINT("IoCreateDevice() failed with status 0x%X\n", Status);
                ErrorStatus = Status;
                ErrorOccurred = TRUE;
                break;
            }

            Device->Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

            //Device->Pdo->Flags |= DO_POWER_PAGABLE;

            PdoDeviceExtension = (PPDO_DEVICE_EXTENSION)Device->Pdo->DeviceExtension;

            RtlZeroMemory(PdoDeviceExtension, sizeof(PDO_DEVICE_EXTENSION));

            IoInitializeRemoveLock(&PdoDeviceExtension->RemoveLock, TAG_PCI, 0, 0);

            PdoDeviceExtension->Common.IsFDO = FALSE;

            PdoDeviceExtension->Common.DeviceObject = Device->Pdo;

            PdoDeviceExtension->Common.DevicePowerState = PowerDeviceD0;

            PdoDeviceExtension->Fdo = DeviceObject;

            PdoDeviceExtension->PciDevice = Device;

            /* Initialize Wait/Wake support */
            KeInitializeSpinLock(&PdoDeviceExtension->WaitWakeSpinLock);
            PdoDeviceExtension->WaitWakeState = 0;

            /* Add Device ID string */
            Status = PciCreateDeviceIDString(&PdoDeviceExtension->DeviceID, Device);
            if (!NT_SUCCESS(Status))
            {
                ErrorStatus = Status;
                ErrorOccurred = TRUE;
                break;
            }

            DPRINT1("PCI: Created PDO for DeviceID: %S\n",
                   PdoDeviceExtension->DeviceID.Buffer);

            /* Add Instance ID string */
            Status = PciCreateInstanceIDString(&PdoDeviceExtension->InstanceID, Device);
            if (!NT_SUCCESS(Status))
            {
                ErrorStatus = Status;
                ErrorOccurred = TRUE;
                break;
            }

            /* Add Hardware IDs string */
            Status = PciCreateHardwareIDsString(&PdoDeviceExtension->HardwareIDs, Device);
            if (!NT_SUCCESS(Status))
            {
                ErrorStatus = Status;
                ErrorOccurred = TRUE;
                break;
            }

            /* Add Compatible IDs string */
            Status = PciCreateCompatibleIDsString(&PdoDeviceExtension->CompatibleIDs, Device);
            if (!NT_SUCCESS(Status))
            {
                ErrorStatus = Status;
                ErrorOccurred = TRUE;
                break;
            }

             /* Add device description string */
            Status = PciCreateDeviceDescriptionString(&PdoDeviceExtension->DeviceDescription, Device);
            if (!NT_SUCCESS(Status))
            {
                ErrorStatus = Status;
                ErrorOccurred = TRUE;
                break;
            }

            /* Add device location string */
            Status = PciCreateDeviceLocationString(&PdoDeviceExtension->DeviceLocation, Device);
            if (!NT_SUCCESS(Status))
            {
                ErrorStatus = Status;
                ErrorOccurred = TRUE;
                break;
            }
        }

        /* Reference the physical device object. The PnP manager
           will dereference it again when it is no longer needed */
        ObReferenceObject(Device->Pdo);

        Relations->Objects[i] = Device->Pdo;

        i++;

        CurrentEntry = CurrentEntry->Flink;
    }

    /* Release the global PCI lock */
    KeSetEvent(&PciGlobalLock, IO_NO_INCREMENT, FALSE);
    KeLeaveCriticalRegion();

    if (ErrorOccurred)
    {
        /* FIXME: Cleanup all new PDOs created in this call. Please give me SEH!!! ;-) */
        /* FIXME: Should IoAttachDeviceToDeviceStack() be undone? */
        if (PdoDeviceExtension)
        {
            RtlFreeUnicodeString(&PdoDeviceExtension->DeviceID);
            RtlFreeUnicodeString(&PdoDeviceExtension->InstanceID);
            RtlFreeUnicodeString(&PdoDeviceExtension->HardwareIDs);
            RtlFreeUnicodeString(&PdoDeviceExtension->CompatibleIDs);
            RtlFreeUnicodeString(&PdoDeviceExtension->DeviceDescription);
            RtlFreeUnicodeString(&PdoDeviceExtension->DeviceLocation);
        }

        ExFreePoolWithTag(Relations, TAG_PCI);
        return ErrorStatus;
    }

    Irp->IoStatus.Information = (ULONG_PTR)Relations;

    DPRINT("Done\n");

    return Status;
}


static NTSTATUS
FdoStartDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PCM_RESOURCE_LIST AllocatedResources;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR ResourceDescriptor;
    ULONG FoundBusNumber = FALSE;
    ULONG i;

    DeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    DPRINT1("PCI: FdoStartDevice called\n");

    AllocatedResources = IoGetCurrentIrpStackLocation(Irp)->Parameters.StartDevice.AllocatedResources;
    if (!AllocatedResources)
    {
        DPRINT("No allocated resources sent to driver\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (AllocatedResources->Count < 1)
    {
        DPRINT("Not enough allocated resources sent to driver\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (AllocatedResources->List[0].PartialResourceList.Version != 1 ||
        AllocatedResources->List[0].PartialResourceList.Revision != 1)
        return STATUS_REVISION_MISMATCH;

    /* By default, use the bus number in the resource list header */
    DeviceExtension->BusNumber = AllocatedResources->List[0].BusNumber;
    DeviceExtension->BusSegment = 0;
    DeviceExtension->BusRangeStart = DeviceExtension->BusNumber;
    DeviceExtension->BusRangeEnd = DeviceExtension->BusNumber;

    for (i = 0; i < AllocatedResources->List[0].PartialResourceList.Count; i++)
    {
        ResourceDescriptor = &AllocatedResources->List[0].PartialResourceList.PartialDescriptors[i];
        switch (ResourceDescriptor->Type)
        {
            case CmResourceTypeBusNumber:
                if (FoundBusNumber || ResourceDescriptor->u.BusNumber.Length < 1)
                    return STATUS_INVALID_PARAMETER;

                /* Use this one instead */
                ASSERT(AllocatedResources->List[0].BusNumber == ResourceDescriptor->u.BusNumber.Start);
                DeviceExtension->BusNumber = ResourceDescriptor->u.BusNumber.Start;
                DeviceExtension->BusRangeStart = ResourceDescriptor->u.BusNumber.Start;
                {
                    ULONG length = ResourceDescriptor->u.BusNumber.Length;
                    ULONG endBus = ResourceDescriptor->u.BusNumber.Start;

                    if (length > 0)
                    {
                        endBus = ResourceDescriptor->u.BusNumber.Start + length - 1;
                        if (endBus < ResourceDescriptor->u.BusNumber.Start)
                            endBus = ResourceDescriptor->u.BusNumber.Start;
                        if (endBus > 0xFF)
                            endBus = 0xFF;
                    }

                    DeviceExtension->BusRangeEnd = endBus;
                }
                DPRINT("Found bus number resource: %lu-%lu\n",
                       DeviceExtension->BusRangeStart,
                       DeviceExtension->BusRangeEnd);
               FoundBusNumber = TRUE;
               break;

            default:
                DPRINT("Unknown resource descriptor type 0x%x\n",
                       ResourceDescriptor->Type);
        }
    }

    /*
     * MSI is architecturally supported on all x86 ACPI platforms.
     * Per Windows pci.sys behavior, the PCI driver determines MSI support
     * internally — no HAL exports needed. _OSC evaluation (when implemented)
     * can refine this per root bridge.
     */
    DeviceExtension->MsiSupported = PciMsiEnabledByPolicy;

    InitializeListHead(&DeviceExtension->DeviceListHead);
    KeInitializeSpinLock(&DeviceExtension->DeviceListLock);
    DeviceExtension->DeviceListCount = 0;

    InitializeListHead(&DeviceExtension->ArbiterListHead);
    PciCreateFdoArbiters(DeviceExtension);

    /* Initialize power management S-to-D mapping */
    DeviceExtension->SystemPowerState = PowerSystemWorking;
    DeviceExtension->StoD_PowerMapping[PowerSystemWorking] = PowerDeviceD0;
    DeviceExtension->StoD_PowerMapping[PowerSystemSleeping1] = PowerDeviceD3;
    DeviceExtension->StoD_PowerMapping[PowerSystemSleeping2] = PowerDeviceD3;
    DeviceExtension->StoD_PowerMapping[PowerSystemSleeping3] = PowerDeviceD3;
    DeviceExtension->StoD_PowerMapping[PowerSystemHibernate] = PowerDeviceD3;
    DeviceExtension->StoD_PowerMapping[PowerSystemShutdown] = PowerDeviceD3;
    KeInitializeSpinLock(&DeviceExtension->WaitWakeSpinLock);

    ExInterlockedInsertTailList(
        &DriverExtension->BusListHead,
        &DeviceExtension->ListEntry,
        &DriverExtension->BusListLock);

  Irp->IoStatus.Information = 0;

  return STATUS_SUCCESS;
}


/*** PUBLIC ******************************************************************/

NTSTATUS
FdoPnpControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
/*
 * FUNCTION: Handle Plug and Play IRPs for the PCI device object
 * ARGUMENTS:
 *     DeviceObject = Pointer to functional device object of the PCI driver
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status = Irp->IoStatus.Status;

    DPRINT("Called\n");

    DeviceExtension = DeviceObject->DeviceExtension;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            Status = STATUS_UNSUCCESSFUL;
            if (IoForwardIrpSynchronously(DeviceExtension->Ldo, Irp))
            {
                Status = Irp->IoStatus.Status;
            }
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IrpSp->Parameters.QueryDeviceRelations.Type != BusRelations)
                break;

            Status = FdoQueryBusRelations(DeviceObject, Irp, IrpSp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            /* Succeed query - don't change state yet */
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_START_DEVICE:
            DPRINT("IRP_MN_START_DEVICE received\n");

            /* Begin transition to Started before forwarding */
            Status = PciBeginStateTransition(DeviceExtension, PciStarted);
            if (!NT_SUCCESS(Status))
            {
                IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            Status = STATUS_UNSUCCESSFUL;
            if (IoForwardIrpSynchronously(DeviceExtension->Ldo, Irp))
            {
                Status = Irp->IoStatus.Status;
                if (NT_SUCCESS(Status))
                {
                    Status = FdoStartDevice(DeviceObject, Irp);
                }
            }

            if (NT_SUCCESS(Status))
            {
                PciCommitStateTransition(DeviceExtension, PciStarted);
            }
            else
            {
                PciCancelStateTransition(DeviceExtension, PciStarted);
            }

            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
            /* Begin transition to Stopped to indicate willingness to stop */
            Status = PciBeginStateTransition(DeviceExtension, PciStopped);
            if (!NT_SUCCESS(Status))
            {
                IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
            /* Restore previous state since stop was cancelled */
            PciCancelStateTransition(DeviceExtension, PciStopped);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
            /* Commit the stop transition that was begun in QUERY_STOP */
            PciCommitStateTransition(DeviceExtension, PciStopped);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            PciBeginStateTransition(DeviceExtension, PciSurpriseRemoved);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            break;

        case IRP_MN_REMOVE_DEVICE:
            PciBeginStateTransition(DeviceExtension, PciDeleted);
            PciDestroyFdoArbiters(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->Ldo, Irp);
            IoDetachDevice(DeviceExtension->Ldo);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_CAPABILITIES:
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            /* Don't print the warning, too much noise */
            break;

        default:
            DPRINT1("Unknown PNP minor function 0x%x\n", IrpSp->MinorFunction);
            break;
    }

    Irp->IoStatus.Status = Status;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(DeviceExtension->Ldo, Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

    DPRINT("Leaving. Status 0x%lx\n", Status);

    return Status;
}


static VOID NTAPI
FdoDevicePowerComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR MinorFunction,
    _In_ POWER_STATE PowerState,
    _In_opt_ PVOID Context,
    _In_ PIO_STATUS_BLOCK IoStatus)
{
    PIRP SystemIrp = (PIRP)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(PowerState);

    /* Complete the original system power IRP */
    SystemIrp->IoStatus.Status = IoStatus->Status;
    PoStartNextPowerIrp(SystemIrp);
    IoCompleteRequest(SystemIrp, IO_NO_INCREMENT);
}

static IO_COMPLETION_ROUTINE FdoSystemPowerComplete;
static NTSTATUS
NTAPI
FdoSystemPowerComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PFDO_DEVICE_EXTENSION DeviceExtension = (PFDO_DEVICE_EXTENSION)Context;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    SYSTEM_POWER_STATE SystemState;
    DEVICE_POWER_STATE DeviceState;
    POWER_STATE PowerState;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        PoStartNextPowerIrp(Irp);
        return STATUS_SUCCESS;
    }

    SystemState = IrpSp->Parameters.Power.State.SystemState;
    DeviceState = DeviceExtension->StoD_PowerMapping[SystemState];

    /* Request corresponding device power IRP */
    PowerState.DeviceState = DeviceState;
    Status = PoRequestPowerIrp(DeviceExtension->Common.DeviceObject,
                                IRP_MN_SET_POWER,
                                PowerState,
                                FdoDevicePowerComplete,
                                Irp,
                                NULL);

    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
FdoPowerControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
/*
 * FUNCTION: Handle power management IRPs for the PCI device object
 * ARGUMENTS:
 *     DeviceObject = Pointer to functional device object of the PCI driver
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PFDO_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;

    DPRINT("Called\n");

    DeviceExtension = DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_SET_POWER:
            if (IrpSp->Parameters.Power.Type == SystemPowerState)
            {
                DPRINT("FDO IRP_MN_SET_POWER: System state S%d\n",
                       IrpSp->Parameters.Power.State.SystemState - PowerSystemWorking);

                /* System power: forward down, then request device power on completion */
                DeviceExtension->SystemPowerState = IrpSp->Parameters.Power.State.SystemState;
                IoMarkIrpPending(Irp);
                IoCopyCurrentIrpStackLocationToNext(Irp);
                IoSetCompletionRoutine(Irp, FdoSystemPowerComplete, DeviceExtension, TRUE, TRUE, TRUE);
                PoCallDriver(DeviceExtension->Ldo, Irp);
                return STATUS_PENDING;
            }
            else
            {
                DPRINT("FDO IRP_MN_SET_POWER: Device state D%d\n",
                       IrpSp->Parameters.Power.State.DeviceState - PowerDeviceD0);

                /* Device power: just forward down */
                PoStartNextPowerIrp(Irp);
                IoSkipCurrentIrpStackLocation(Irp);
                return PoCallDriver(DeviceExtension->Ldo, Irp);
            }

        case IRP_MN_QUERY_POWER:
            DPRINT("FDO IRP_MN_QUERY_POWER: Type %d\n",
                   IrpSp->Parameters.Power.Type);
            /* Always succeed */
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(DeviceExtension->Ldo, Irp);

        case IRP_MN_WAIT_WAKE:
            DPRINT("FDO IRP_MN_WAIT_WAKE\n");
            PoStartNextPowerIrp(Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(DeviceExtension->Ldo, Irp);

        default:
            DPRINT("FDO power IRP minor 0x%x\n", IrpSp->MinorFunction);
            PoStartNextPowerIrp(Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            return PoCallDriver(DeviceExtension->Ldo, Irp);
    }
}

/* EOF */
