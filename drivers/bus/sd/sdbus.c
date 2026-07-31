/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver entry, AddDevice, and dispatch routing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"

#define NDEBUG
#include <debug.h>

/* Forward declarations */

static NTSTATUS NTAPI
SdBusDispatchPnpImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

static NTSTATUS NTAPI
SdBusDispatchPowerImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

static NTSTATUS NTAPI
SdBusDispatchInternalDeviceControlImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

static NTSTATUS NTAPI
SdBusDispatchCreateCloseImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

static
NTSTATUS
NTAPI
SdBusReleaseFdoRemoveLockCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PVOID Context)
{
    PFDO_EXTENSION FdoExtension;

    UNREFERENCED_PARAMETER(DeviceObject);

    FdoExtension = (PFDO_EXTENSION)Context;
    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }
    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

VOID
SdBusInitializeDeviceUsage(
    _Inout_ PSDBUS_COMMON_EXTENSION CommonExtension,
    _In_ BOOLEAN PowerPagable)
{
    KeInitializeSpinLock(&CommonExtension->UsageLock);
    CommonExtension->PowerPagable = PowerPagable;
}

NTSTATUS
SdBusAdjustDeviceUsage(
    _Inout_ PSDBUS_COMMON_EXTENSION CommonExtension,
    _In_ DEVICE_USAGE_NOTIFICATION_TYPE Type,
    _In_ BOOLEAN InPath)
{
    PULONG Counter;
    BOOLEAN AffectsPaging;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (Type)
    {
        case DeviceUsageTypePaging:
            Counter = &CommonExtension->PagingPathCount;
            AffectsPaging = TRUE;
            break;

        case DeviceUsageTypeHibernation:
            Counter = &CommonExtension->HibernationPathCount;
            AffectsPaging = TRUE;
            break;

        case DeviceUsageTypeDumpFile:
            Counter = &CommonExtension->DumpPathCount;
            AffectsPaging = TRUE;
            break;

        case DeviceUsageTypeBoot:
            Counter = &CommonExtension->BootPathCount;
            AffectsPaging = FALSE;
            break;

        case DeviceUsageTypePostDisplay:
            Counter = &CommonExtension->PostDisplayPathCount;
            AffectsPaging = FALSE;
            break;

        case DeviceUsageTypeGuestAssigned:
            Counter = &CommonExtension->GuestAssignedPathCount;
            AffectsPaging = FALSE;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&CommonExtension->UsageLock, &OldIrql);
    if (InPath)
    {
        if (*Counter == MAXULONG)
        {
            Status = STATUS_INTEGER_OVERFLOW;
        }
        else
        {
            (*Counter)++;
        }
    }
    else if (*Counter == 0)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else
    {
        (*Counter)--;
    }

    if (NT_SUCCESS(Status) && AffectsPaging)
    {
        if (CommonExtension->PagingPathCount != 0 ||
            CommonExtension->HibernationPathCount != 0 ||
            CommonExtension->DumpPathCount != 0)
        {
            CommonExtension->Self->Flags &= ~DO_POWER_PAGABLE;
        }
        else if (CommonExtension->PowerPagable &&
                 !(CommonExtension->Self->Flags & DO_POWER_INRUSH))
        {
            CommonExtension->Self->Flags |= DO_POWER_PAGABLE;
        }
    }
    KeReleaseSpinLock(&CommonExtension->UsageLock, OldIrql);

    return Status;
}

BOOLEAN
SdBusHasActiveDeviceUsage(
    _Inout_ PSDBUS_COMMON_EXTENSION CommonExtension)
{
    KIRQL OldIrql;
    BOOLEAN Active;

    KeAcquireSpinLock(&CommonExtension->UsageLock, &OldIrql);
    Active = CommonExtension->PagingPathCount != 0 ||
             CommonExtension->HibernationPathCount != 0 ||
             CommonExtension->DumpPathCount != 0 ||
             CommonExtension->BootPathCount != 0 ||
             CommonExtension->PostDisplayPathCount != 0 ||
             CommonExtension->GuestAssignedPathCount != 0;
    KeReleaseSpinLock(&CommonExtension->UsageLock, OldIrql);

    return Active;
}

/**
 * @brief Create and attach an FDO for the SD host controller.
 *
 * Called by the PnP manager for each physical device object (PDO) that
 * matches this driver. Creates the functional device object, initializes
 * the FDO extension, and attaches to the device stack.
 *
 * @param[in] DriverObject  Pointer to the SD bus driver object.
 * @param[in] PhysicalDeviceObject  Pointer to the PDO created by the parent bus.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
NTAPI
SdBusAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT Fdo;
    PFDO_EXTENSION FdoExtension;
    NTSTATUS Status;

    DPRINT1("SdBusAddDevice: PhysicalDeviceObject %p\n", PhysicalDeviceObject);

    if (PhysicalDeviceObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Create the functional device object */
    Status = IoCreateDevice(DriverObject,
                            sizeof(FDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusAddDevice: IoCreateDevice failed (0x%08lx)\n", Status);
        return Status;
    }

    FdoExtension = (PFDO_EXTENSION)Fdo->DeviceExtension;
    RtlZeroMemory(FdoExtension, sizeof(FDO_EXTENSION));

    /* Initialize common header */
    FdoExtension->Common.Self = Fdo;
    FdoExtension->Common.IsFdo = TRUE;
    FdoExtension->Common.DeviceState = SdBusDeviceStateStopped;
    FdoExtension->Common.DevicePowerState = PowerDeviceD0;

    /* Store the PDO */
    FdoExtension->PhysicalDevice = PhysicalDeviceObject;
    FdoExtension->BusInterfaceType = InterfaceTypeUndefined;
    FdoExtension->BusNumber = 0;
    FdoExtension->CurrentBusWidth = 1;
    FdoExtension->RequestsBlocked = 1;

    /* Attach to the device stack */
    FdoExtension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo,
                                                             PhysicalDeviceObject);
    if (FdoExtension->LowerDevice == NULL)
    {
        DPRINT1("SdBusAddDevice: IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Initialize the spin lock */
    KeInitializeSpinLock(&FdoExtension->Lock);

    /* Initialize the command serialization mutex */
    ExInitializeFastMutex(&FdoExtension->CommandMutex);

    /* Initialize the child PDO list */
    InitializeListHead(&FdoExtension->ChildPdoList);
    FdoExtension->ChildPdoCount = 0;

    /* Initialize the remove lock */
    IoInitializeRemoveLock(&FdoExtension->RemoveLock, TAG_SDBUS, 0, 0);

    /* Initialize command/data completion event and DPC */
    KeInitializeEvent(&FdoExtension->CommandEvent, SynchronizationEvent, FALSE);
    KeInitializeDpc(&FdoExtension->CommandDpc,
                    SdBusCommandCompletionDpc,
                    FdoExtension);

    /* Initialize the card-detect DPC */
    KeInitializeDpc(&FdoExtension->CardDetectDpc,
                    SdBusCardDetectDpc,
                    FdoExtension);

    /* Propagate flags from the lower device */
    Fdo->Flags |= FdoExtension->LowerDevice->Flags &
                  (DO_DIRECT_IO | DO_BUFFERED_IO);
    if (FdoExtension->LowerDevice->Flags & DO_POWER_INRUSH)
    {
        Fdo->Flags |= DO_POWER_INRUSH;
    }
    else if (FdoExtension->LowerDevice->Flags & DO_POWER_PAGABLE)
    {
        Fdo->Flags |= DO_POWER_PAGABLE;
    }
    SdBusInitializeDeviceUsage(&FdoExtension->Common,
                               (Fdo->Flags & DO_POWER_PAGABLE) != 0);
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT1("SdBusAddDevice: FDO %p attached to %p\n",
           Fdo, FdoExtension->LowerDevice);

    return STATUS_SUCCESS;
}

/**
 * @brief Route IRP_MJ_PNP to the FDO or PDO PnP handler.
 *
 * Inspects the device extension to determine whether the target device
 * object is an FDO or PDO and dispatches accordingly.
 *
 * @param[in]     DeviceObject  Pointer to the target device object.
 * @param[in,out] Irp           Pointer to the PnP I/O request packet.
 *
 * @return NTSTATUS from the FDO or PDO PnP handler.
 */
static NTSTATUS
NTAPI
SdBusDispatchPnpImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSDBUS_COMMON_EXTENSION CommonExtension;

    CommonExtension = (PSDBUS_COMMON_EXTENSION)DeviceObject->DeviceExtension;

    if (CommonExtension->IsFdo)
    {
        return SdBusFdoPnp(DeviceObject, Irp);
    }
    else
    {
        return SdBusPdoPnp(DeviceObject, Irp);
    }
}

/**
 * @brief Route IRP_MJ_POWER to the FDO or PDO power handler.
 *
 * Inspects the device extension to determine whether the target device
 * object is an FDO or PDO and dispatches accordingly.
 *
 * @param[in]     DeviceObject  Pointer to the target device object.
 * @param[in,out] Irp           Pointer to the power I/O request packet.
 *
 * @return NTSTATUS from the FDO or PDO power handler.
 */
static NTSTATUS
NTAPI
SdBusDispatchPowerImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSDBUS_COMMON_EXTENSION CommonExtension;

    CommonExtension = (PSDBUS_COMMON_EXTENSION)DeviceObject->DeviceExtension;

    if (CommonExtension->IsFdo)
    {
        return SdBusFdoPower(DeviceObject, Irp);
    }
    else
    {
        return SdBusPdoPower(DeviceObject, Irp);
    }
}

/**
 * @brief Route IRP_MJ_INTERNAL_DEVICE_CONTROL to the PDO handler or pass down.
 *
 * PDOs handle internal IOCTLs (SD bus request packets). For the FDO the
 * IRP is forwarded to the lower driver unchanged.
 *
 * @param[in]     DeviceObject  Pointer to the target device object.
 * @param[in,out] Irp           Pointer to the internal IOCTL IRP.
 *
 * @return NTSTATUS from the PDO handler or the lower driver.
 */
static NTSTATUS
NTAPI
SdBusDispatchInternalDeviceControlImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSDBUS_COMMON_EXTENSION CommonExtension;
    PFDO_EXTENSION FdoExtension;
    NTSTATUS Status;

    CommonExtension = (PSDBUS_COMMON_EXTENSION)DeviceObject->DeviceExtension;

    if (!CommonExtension->IsFdo)
    {
        return SdBusPdoInternalDeviceControl(DeviceObject, Irp);
    }

    FdoExtension = (PFDO_EXTENSION)DeviceObject->DeviceExtension;
    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* The FDO does not handle the request, but it must remain attached until
       the lower stack finishes with the IRP. */
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SdBusReleaseFdoRemoveLockCompletion,
                           FdoExtension,
                           TRUE,
                           TRUE,
                           TRUE);
    return IoCallDriver(FdoExtension->LowerDevice, Irp);
}

/**
 * @brief Handle IRP_MJ_CREATE and IRP_MJ_CLOSE requests.
 *
 * Always succeeds, allowing handles to be opened and closed on the
 * SD bus device objects.
 *
 * @param[in]     DeviceObject  Pointer to the target device object (unused).
 * @param[in,out] Irp           Pointer to the create/close IRP.
 *
 * @return STATUS_SUCCESS always.
 */
static NTSTATUS
NTAPI
SdBusDispatchCreateCloseImpl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSDBUS_COMMON_EXTENSION CommonExtension;
    PIO_STACK_LOCATION IoStack;
    PIO_REMOVE_LOCK RemoveLock;
    NTSTATUS Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Status = STATUS_SUCCESS;

    if (IoStack->MajorFunction == IRP_MJ_CREATE)
    {
        CommonExtension = (PSDBUS_COMMON_EXTENSION)DeviceObject->DeviceExtension;
        RemoveLock = CommonExtension->IsFdo
                         ? &((PFDO_EXTENSION)CommonExtension)->RemoveLock
                         : &((PPDO_EXTENSION)CommonExtension)->RemoveLock;
        Status = IoAcquireRemoveLock(RemoveLock, Irp);
        if (NT_SUCCESS(Status))
        {
            IoReleaseRemoveLock(RemoveLock, Irp);
        }
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/**
 * @brief SD bus driver entry point.
 *
 * Initializes the driver dispatch table and registers the AddDevice
 * callback with the PnP manager.
 *
 * @param[in] DriverObject  Pointer to the driver object created by the I/O manager.
 * @param[in] RegistryPath  Pointer to the driver's registry service key (unused).
 *
 * @return STATUS_SUCCESS always.
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DPRINT1("SD Bus Driver: DriverEntry\n");

    /* Set up the dispatch table */
    DriverObject->MajorFunction[IRP_MJ_PNP] = SdBusDispatchPnpImpl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SdBusDispatchPowerImpl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SdBusDispatchInternalDeviceControlImpl;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = SdBusDispatchCreateCloseImpl;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SdBusDispatchCreateCloseImpl;

    /* Set AddDevice handler */
    DriverObject->DriverExtension->AddDevice = SdBusAddDevice;

    return STATUS_SUCCESS;
}
