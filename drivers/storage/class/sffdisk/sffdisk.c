/*
 * PROJECT:     ReactOS SD/MMC Storage Class Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DriverEntry, AddDevice, PnP, Power, Create/Close dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include <initguid.h>

#include "sffdisk.h"
#include <mountmgr.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, SffdiskAddDevice)
#pragma alloc_text(PAGE, SffdiskPnp)
#pragma alloc_text(PAGE, SffdiskStartDevice)
#endif

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
SffdiskCreatePhysicalDriveLink(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension);

static
NTSTATUS
SffdiskOpenBusInterface(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension);

static
VOID
SffdiskCloseBusInterface(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension);

static
VOID
SffdiskBlockBusRequests(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension);

static
VOID
SffdiskUnblockBusRequests(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension);

static
VOID
SffdiskStopDevice(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension);

static
NTSTATUS
SffdiskAdjustDeviceUsage(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_ DEVICE_USAGE_NOTIFICATION_TYPE Type,
    _In_ BOOLEAN InPath)
{
    PULONG Counter;
    BOOLEAN AffectsPowerPageability;
    KIRQL OldIrql;
    NTSTATUS Status;

    switch (Type)
    {
        case DeviceUsageTypePaging:
            Counter = &DeviceExtension->PagingPathCount;
            AffectsPowerPageability = TRUE;
            break;

        case DeviceUsageTypeHibernation:
            Counter = &DeviceExtension->HibernationPathCount;
            AffectsPowerPageability = TRUE;
            break;

        case DeviceUsageTypeDumpFile:
            Counter = &DeviceExtension->DumpPathCount;
            AffectsPowerPageability = TRUE;
            break;

        case DeviceUsageTypeBoot:
            Counter = &DeviceExtension->BootPathCount;
            AffectsPowerPageability = FALSE;
            break;

        case DeviceUsageTypePostDisplay:
            Counter = &DeviceExtension->PostDisplayPathCount;
            AffectsPowerPageability = FALSE;
            break;

        case DeviceUsageTypeGuestAssigned:
            Counter = &DeviceExtension->GuestAssignedPathCount;
            AffectsPowerPageability = FALSE;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    Status = STATUS_SUCCESS;
    KeAcquireSpinLock(&DeviceExtension->UsageLock, &OldIrql);
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

    if (NT_SUCCESS(Status) && AffectsPowerPageability)
    {
        if (DeviceExtension->PagingPathCount != 0 ||
            DeviceExtension->HibernationPathCount != 0 ||
            DeviceExtension->DumpPathCount != 0)
        {
            DeviceExtension->Self->Flags &= ~DO_POWER_PAGABLE;
        }
        else if (DeviceExtension->PowerPagable &&
                 !(DeviceExtension->Self->Flags & DO_POWER_INRUSH))
        {
            DeviceExtension->Self->Flags |= DO_POWER_PAGABLE;
        }
    }
    KeReleaseSpinLock(&DeviceExtension->UsageLock, OldIrql);

    return Status;
}

static
BOOLEAN
SffdiskHasActiveDeviceUsage(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;
    BOOLEAN Active;

    KeAcquireSpinLock(&DeviceExtension->UsageLock, &OldIrql);
    Active = DeviceExtension->PagingPathCount != 0 ||
             DeviceExtension->HibernationPathCount != 0 ||
             DeviceExtension->DumpPathCount != 0 ||
             DeviceExtension->BootPathCount != 0 ||
             DeviceExtension->PostDisplayPathCount != 0 ||
             DeviceExtension->GuestAssignedPathCount != 0;
    KeReleaseSpinLock(&DeviceExtension->UsageLock, OldIrql);

    return Active;
}

static
NTSTATUS
SffdiskReserveDiskNumber(
    _Inout_ PULONG DiskCount,
    _Out_ PULONG DiskNumber)
{
    volatile LONG *AtomicDiskCount;
    LONG Observed;
    LONG Previous;
    ULONG Next;

    AtomicDiskCount = (volatile LONG *)DiskCount;
    do
    {
        Observed = InterlockedCompareExchange(AtomicDiskCount, 0, 0);
        if ((ULONG)Observed == MAXULONG)
        {
            return STATUS_INTEGER_OVERFLOW;
        }

        Next = (ULONG)Observed + 1;
        Previous = InterlockedCompareExchange(AtomicDiskCount,
                                              (LONG)Next,
                                              Observed);
    } while (Previous != Observed);

    *DiskNumber = (ULONG)Observed;
    return STATUS_SUCCESS;
}

/**
 * @brief Completion routine that signals the event when the lower driver
 *        finishes processing a forwarded IRP.
 *
 * @param DeviceObject The device object (unused).
 * @param Irp The IRP being completed (unused).
 * @param Context Pointer to a KEVENT to be signaled.
 *
 * @return STATUS_MORE_PROCESSING_REQUIRED to prevent further completion.
 */
static
NTSTATUS
NTAPI
SffdiskForwardIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
SffdiskReleaseRemoveLockCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;

    UNREFERENCED_PARAMETER(DeviceObject);

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)Context;
    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
SffdiskForwardIrpWithRemoveLock(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN PowerIrp)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SffdiskReleaseRemoveLockCompletion,
                           DeviceExtension,
                           TRUE,
                           TRUE,
                           TRUE);

    if (PowerIrp)
    {
        return PoCallDriver(DeviceExtension->LowerDevice, Irp);
    }
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

/**
 * @brief Sends an IRP down the stack and waits for the lower driver to complete it.
 *
 * @param DeviceObject Our FDO.
 * @param Irp The IRP to forward.
 *
 * @return Status from the lower driver.
 */
static
NTSTATUS
SffdiskForwardIrpSynchronous(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    KEVENT Event;
    NTSTATUS Status;

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SffdiskForwardIrpCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    Status = Irp->IoStatus.Status;

    return Status;
}

/**
 * @brief Queries a property from the SD bus driver using the bus interface.
 *
 * @param DeviceExtension Our device extension.
 * @param Property The SDBUS_PROPERTY to query.
 * @param Buffer Output buffer for the property value.
 * @param Length Size of the output buffer in bytes.
 *
 * @return NTSTATUS from SdBusSubmitRequest.
 */
static
NTSTATUS
SffdiskGetProperty(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_ SDBUS_PROPERTY Property,
    _Out_ PVOID Buffer,
    _In_ ULONG Length)
{
    SDBUS_REQUEST_PACKET Srb;

    SD_INIT_GET_PROPERTY(&Srb, Property, Buffer, Length);

    return SdBusSubmitRequest(DeviceExtension->BusInterface.Context, &Srb);
}

static
NTSTATUS
SffdiskOpenBusInterface(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    SDBUS_INTERFACE_PARAMETERS InterfaceParams;
    KIRQL OldIrql;
    BOOLEAN InterfaceOpen;
    NTSTATUS Status;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    InterfaceOpen = DeviceExtension->InterfaceOpen;
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    if (InterfaceOpen)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&DeviceExtension->BusInterface,
                  sizeof(DeviceExtension->BusInterface));

    Status = SdBusOpenInterface(DeviceExtension->PhysicalDevice,
                                &DeviceExtension->BusInterface,
                                sizeof(SDBUS_INTERFACE_STANDARD),
                                SDBUS_INTERFACE_VERSION);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskOpenBusInterface: SdBusOpenInterface failed (0x%08lx)\n",
                Status);
        return Status;
    }

    RtlZeroMemory(&InterfaceParams, sizeof(InterfaceParams));
    InterfaceParams.Size = sizeof(SDBUS_INTERFACE_PARAMETERS);
    InterfaceParams.TargetObject = DeviceExtension->LowerDevice;

    Status = DeviceExtension->BusInterface.InitializeInterface(
                 DeviceExtension->BusInterface.Context,
                 &InterfaceParams);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskOpenBusInterface: InitializeInterface failed (0x%08lx)\n",
                Status);
        DeviceExtension->BusInterface.InterfaceDereference(
            DeviceExtension->BusInterface.Context);
        RtlZeroMemory(&DeviceExtension->BusInterface,
                      sizeof(DeviceExtension->BusInterface));
        return Status;
    }

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    DeviceExtension->InterfaceOpen = TRUE;
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    return STATUS_SUCCESS;
}

static
VOID
SffdiskCloseBusInterface(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PVOID InterfaceContext;
    KIRQL OldIrql;

    InterfaceDereference = NULL;
    InterfaceContext = NULL;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    if (DeviceExtension->InterfaceOpen)
    {
        InterfaceDereference = DeviceExtension->BusInterface.InterfaceDereference;
        InterfaceContext = DeviceExtension->BusInterface.Context;
        DeviceExtension->InterfaceOpen = FALSE;
    }
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    if (InterfaceDereference != NULL)
    {
        InterfaceDereference(InterfaceContext);
        RtlZeroMemory(&DeviceExtension->BusInterface,
                      sizeof(DeviceExtension->BusInterface));
    }
}

NTSTATUS
SffdiskAcquireBusRequest(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PFILE_OBJECT FileObject)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    for (;;)
    {
        KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
        if (DeviceExtension->BusRequestsBlocked ||
            !DeviceExtension->InterfaceOpen)
        {
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_DEVICE_NOT_READY;
        }

        if (DeviceExtension->OutstandingBusRequests == MAXULONG)
        {
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_INTEGER_OVERFLOW;
        }

        if (DeviceExtension->ChannelOwner == NULL ||
            DeviceExtension->ChannelOwner == FileObject)
        {
            if (DeviceExtension->OutstandingBusRequests++ == 0)
            {
                KeClearEvent(&DeviceExtension->BusRequestsDrained);
            }
            KeClearEvent(&DeviceExtension->ChannelAvailable);
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_SUCCESS;
        }
        KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

        Status = KeWaitForSingleObject(&DeviceExtension->ChannelAvailable,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
}

VOID
SffdiskReleaseBusRequest(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    ASSERT(DeviceExtension->OutstandingBusRequests != 0);
    if (--DeviceExtension->OutstandingBusRequests == 0)
    {
        KeSetEvent(&DeviceExtension->BusRequestsDrained,
                   IO_NO_INCREMENT,
                   FALSE);
        if (DeviceExtension->ChannelOwner == NULL)
        {
            KeSetEvent(&DeviceExtension->ChannelAvailable,
                       IO_NO_INCREMENT,
                       FALSE);
        }
    }
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
}

NTSTATUS
SffdiskLockChannel(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_ PFILE_OBJECT FileObject)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (FileObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (;;)
    {
        KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
        if (DeviceExtension->BusRequestsBlocked ||
            !DeviceExtension->InterfaceOpen)
        {
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_DEVICE_NOT_READY;
        }

        if (DeviceExtension->ChannelOwner == FileObject)
        {
            if (DeviceExtension->ChannelLockDepth == MAXULONG)
            {
                KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
                return STATUS_INTEGER_OVERFLOW;
            }

            DeviceExtension->ChannelLockDepth++;
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_SUCCESS;
        }

        if (DeviceExtension->ChannelOwner == NULL &&
            DeviceExtension->OutstandingBusRequests == 0)
        {
            DeviceExtension->ChannelOwner = FileObject;
            DeviceExtension->ChannelLockDepth = 1;
            KeClearEvent(&DeviceExtension->ChannelAvailable);
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_SUCCESS;
        }
        KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

        Status = KeWaitForSingleObject(&DeviceExtension->ChannelAvailable,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
}

NTSTATUS
SffdiskUnlockChannel(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_ PFILE_OBJECT FileObject)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    if (FileObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (;;)
    {
        KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
        if (DeviceExtension->ChannelOwner != FileObject ||
            DeviceExtension->ChannelLockDepth == 0)
        {
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_INVALID_DEVICE_STATE;
        }

        if (DeviceExtension->ChannelLockDepth > 1)
        {
            DeviceExtension->ChannelLockDepth--;
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_SUCCESS;
        }

        if (DeviceExtension->OutstandingBusRequests == 0)
        {
            DeviceExtension->ChannelOwner = NULL;
            DeviceExtension->ChannelLockDepth = 0;
            KeSetEvent(&DeviceExtension->ChannelAvailable,
                       IO_NO_INCREMENT,
                       FALSE);
            KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
            return STATUS_SUCCESS;
        }
        KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

        Status = KeWaitForSingleObject(&DeviceExtension->BusRequestsDrained,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
}

VOID
SffdiskReleaseChannelForFile(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PFILE_OBJECT FileObject)
{
    KIRQL OldIrql;

    if (FileObject == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    if (DeviceExtension->ChannelOwner == FileObject)
    {
        DeviceExtension->ChannelOwner = NULL;
        DeviceExtension->ChannelLockDepth = 0;
        if (DeviceExtension->OutstandingBusRequests == 0)
        {
            KeSetEvent(&DeviceExtension->ChannelAvailable,
                       IO_NO_INCREMENT,
                       FALSE);
        }
    }
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
}

static
VOID
SffdiskBlockBusRequests(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;
    BOOLEAN WaitForDrain;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    DeviceExtension->BusRequestsBlocked = TRUE;
    DeviceExtension->ChannelOwner = NULL;
    DeviceExtension->ChannelLockDepth = 0;
    KeSetEvent(&DeviceExtension->ChannelAvailable,
               IO_NO_INCREMENT,
               FALSE);
    WaitForDrain = (DeviceExtension->OutstandingBusRequests != 0);
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);

    if (WaitForDrain)
    {
        KeWaitForSingleObject(&DeviceExtension->BusRequestsDrained,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
}

static
VOID
SffdiskUnblockBusRequests(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->BusRequestLock, &OldIrql);
    ASSERT(DeviceExtension->InterfaceOpen);
    DeviceExtension->BusRequestsBlocked = FALSE;
    if (DeviceExtension->OutstandingBusRequests == 0)
    {
        KeSetEvent(&DeviceExtension->ChannelAvailable,
                   IO_NO_INCREMENT,
                   FALSE);
    }
    KeReleaseSpinLock(&DeviceExtension->BusRequestLock, OldIrql);
}

/**
 * @brief Populates DiskGeometry from TotalSectors and BytesPerSector.
 *
 * Uses a simplified CHS geometry suitable for SD/eMMC media.
 *
 * @param DeviceExtension Our device extension with TotalSectors already set.
 */
static
VOID
SffdiskComputeGeometry(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    ULONGLONG TotalSectors = DeviceExtension->TotalSectors;
    ULONG SectorsPerTrack;
    ULONG TracksPerCylinder;
    ULONGLONG Cylinders;

    /*
     * Use a standard geometry convention:
     *   - SectorsPerTrack   = 63
     *   - TracksPerCylinder = 255
     *   - Cylinders         = TotalSectors / (63 * 255)
     *
     * This matches what Windows reports for USB flash drives and SD cards.
     */
    SectorsPerTrack = 63;
    TracksPerCylinder = 255;
    Cylinders = TotalSectors / ((ULONGLONG)SectorsPerTrack * TracksPerCylinder);

    if (Cylinders == 0)
    {
        Cylinders = 1;
    }

    DeviceExtension->DiskGeometry.Cylinders.QuadPart = (LONGLONG)Cylinders;
    DeviceExtension->DiskGeometry.MediaType =
        DeviceExtension->Removable ? RemovableMedia : FixedMedia;
    DeviceExtension->DiskGeometry.TracksPerCylinder = TracksPerCylinder;
    DeviceExtension->DiskGeometry.SectorsPerTrack = SectorsPerTrack;
    DeviceExtension->DiskGeometry.BytesPerSector = DeviceExtension->BytesPerSector;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/**
 * @brief Opens the SD bus interface, queries card properties, and initializes
 *        the device extension with card-specific information.
 *
 * @param DeviceExtension Our device extension.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
SffdiskStartDevice(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;
    SDBUS_FUNCTION_TYPE FunctionType;
    BOOLEAN WriteProtected;
    BOOLEAN HighCapacity;
    SDPROP_MEDIA_STATE MediaState;
    ULONG MediaChangeCount;
    ULONG BusCardType;

    PAGED_CODE();

    SffdiskBlockBusRequests(DeviceExtension);
    DeviceExtension->Present = FALSE;
    DeviceExtension->MediaPresent = FALSE;
    DeviceExtension->TotalSectors = 0;
    DeviceExtension->CardType = SdCardTypeUnknown;
    DeviceExtension->CidValid = FALSE;

    Status = SffdiskOpenBusInterface(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /*
     * Step 3: Query the function type to determine card type.
     */
    FunctionType = SDBUS_FUNCTION_TYPE_UNKNOWN;
    Status = SffdiskGetProperty(DeviceExtension,
                                SDP_FUNCTION_TYPE,
                                &FunctionType,
                                sizeof(FunctionType));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskStartDevice: Failed to get SDP_FUNCTION_TYPE (0x%08lx)\n", Status);
        goto CleanupInterface;
    }
    if (!IsTypeMemory(FunctionType))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto CleanupInterface;
    }

    BusCardType = SdCardTypeUnknown;
    Status = SffdiskGetProperty(DeviceExtension,
                                SDP_ROS_CARD_TYPE,
                                &BusCardType,
                                sizeof(BusCardType));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskStartDevice: Failed to get SDP_ROS_CARD_TYPE (0x%08lx)\n",
                Status);
        goto CleanupInterface;
    }
    switch ((SD_CARD_TYPE)BusCardType)
    {
        case SdCardTypeSdV1:
        case SdCardTypeSdV2:
        case SdCardTypeSdhc:
        case SdCardTypeSdxc:
        case SdCardTypeCombo:
        case SdCardTypeMmc:
        case SdCardTypeEmmc:
            DeviceExtension->CardType = (SD_CARD_TYPE)BusCardType;
            break;

        default:
            Status = STATUS_DEVICE_DATA_ERROR;
            goto CleanupInterface;
    }

    if (DeviceExtension->CardType == SdCardTypeEmmc)
    {
        DeviceExtension->Removable = FALSE;
        DeviceExtension->RemovabilityKnown = TRUE;
    }
    else if (!DeviceExtension->RemovabilityKnown)
    {
        /* QUERY_CAPABILITIES normally precedes start. Keep a conservative
         * protocol fallback for stacks that start without querying it. */
        DeviceExtension->Removable = TRUE;
    }

    /*
     * Step 4: Query write-protect status.
     */
    WriteProtected = FALSE;
    Status = SffdiskGetProperty(DeviceExtension,
                                SDP_WRITE_PROTECTED,
                                &WriteProtected,
                                sizeof(WriteProtected));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskStartDevice: Failed to get SDP_WRITE_PROTECTED (0x%08lx)\n",
                Status);
        goto CleanupInterface;
    }
    DeviceExtension->WriteProtected = WriteProtected;

    /*
     * Step 5: Query high-capacity support to determine addressing mode.
     */
    HighCapacity = FALSE;
    Status = SffdiskGetProperty(DeviceExtension,
                                SDP_HIGH_CAPACITY_SUPPORTED,
                                &HighCapacity,
                                sizeof(HighCapacity));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskStartDevice: Failed to get SDP_HIGH_CAPACITY_SUPPORTED (0x%08lx)\n",
                Status);
        goto CleanupInterface;
    }
    DeviceExtension->HighCapacity = HighCapacity;

    /*
     * Refine card type based on high-capacity flag for SD memory.
     */
    if (FunctionType == SDBUS_FUNCTION_TYPE_SD_MEMORY)
    {
        if (DeviceExtension->HighCapacity)
        {
            DeviceExtension->CardType = SdCardTypeSdhc;
        }
        else
        {
            DeviceExtension->CardType = SdCardTypeSdV2;
        }
    }

    /*
     * eMMC and SDHC/SDXC always use block addressing.
     */
    if ((DeviceExtension->CardType == SdCardTypeSdhc ||
         DeviceExtension->CardType == SdCardTypeSdxc ||
         DeviceExtension->CardType == SdCardTypeEmmc) &&
        !DeviceExtension->HighCapacity)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto CleanupInterface;
    }
    if ((DeviceExtension->CardType == SdCardTypeSdV1 ||
         DeviceExtension->CardType == SdCardTypeSdV2) &&
        DeviceExtension->HighCapacity)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto CleanupInterface;
    }

    /*
     * Step 6: Query media state.
     */
    MediaState = SDPMS_NO_MEDIA;
    Status = SffdiskGetProperty(DeviceExtension,
                                SDP_MEDIA_STATE,
                                &MediaState,
                                sizeof(MediaState));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskStartDevice: Failed to get SDP_MEDIA_STATE (0x%08lx)\n",
                Status);
        goto CleanupInterface;
    }
    if (MediaState != SDPMS_NO_MEDIA && MediaState != SDPMS_MEDIA_INSERTED)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
        goto CleanupInterface;
    }
    DeviceExtension->MediaPresent = (MediaState == SDPMS_MEDIA_INSERTED);
    if (!DeviceExtension->MediaPresent)
    {
        Status = STATUS_NO_MEDIA_IN_DEVICE;
        goto CleanupInterface;
    }

    /*
     * Step 7: Query media change count.
     */
    MediaChangeCount = 0;
    Status = SffdiskGetProperty(DeviceExtension,
                                SDP_MEDIA_CHANGECOUNT,
                                &MediaChangeCount,
                                sizeof(MediaChangeCount));
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->MediaChangeCount = MediaChangeCount;
    }

    {
        SDBUS_REQUEST_PACKET Srb;

        SD_INIT_REQUEST_PACKET(&Srb, SDRF_DEVICE_COMMAND);
        Srb.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_SEND_CID;
        Srb.Parameters.DeviceCommand.CmdDesc.CmdClass = SDCC_STANDARD;
        Srb.Parameters.DeviceCommand.CmdDesc.TransferDirection = SDTD_UNSPECIFIED;
        Srb.Parameters.DeviceCommand.CmdDesc.TransferType = SDTT_CMD_ONLY;
        Srb.Parameters.DeviceCommand.CmdDesc.ResponseType = SDRT_2;
        Srb.Parameters.DeviceCommand.Argument = 0;
        Srb.Parameters.DeviceCommand.Mdl = NULL;
        Srb.Parameters.DeviceCommand.Length = 0;

        Status = SdBusSubmitRequest(DeviceExtension->BusInterface.Context,
                                    &Srb);
        if (NT_SUCCESS(Status))
        {
            PULONG Resp = Srb.ResponseData.AsULONG;
            PSD_CID Cid = &DeviceExtension->CardId;

            RtlZeroMemory(Cid, sizeof(SD_CID));

            Cid->ManufacturerId = (UCHAR)((Resp[3] >> 16) & 0xFF);
            Cid->OemId = (USHORT)(Resp[3] & 0xFFFF);

            Cid->ProductName[0] = (UCHAR)((Resp[2] >> 24) & 0xFF);
            Cid->ProductName[1] = (UCHAR)((Resp[2] >> 16) & 0xFF);
            Cid->ProductName[2] = (UCHAR)((Resp[2] >> 8) & 0xFF);
            Cid->ProductName[3] = (UCHAR)(Resp[2] & 0xFF);
            Cid->ProductName[4] = (UCHAR)((Resp[1] >> 24) & 0xFF);

            Cid->ProductRevision = (UCHAR)((Resp[1] >> 16) & 0xFF);
            Cid->ProductSerialNumber =
                ((Resp[1] & 0xFFFF) << 16) | ((Resp[0] >> 16) & 0xFFFF);
            Cid->ManufacturingDate = (USHORT)((Resp[0] >> 4) & 0xFFF);
            Cid->Crc7 = (UCHAR)((Resp[0] << 1) & 0xFE);

            DeviceExtension->CidValid = TRUE;
        }
        else
        {
            DPRINT1("SffdiskStartDevice: CMD10 (SEND_CID) failed (0x%08lx)\n",
                    Status);
            DeviceExtension->CidValid = FALSE;
        }
    }

    DeviceExtension->BytesPerSector = SD_DEFAULT_SECTOR_SIZE;

    {
        ULONGLONG TotalSectors = 0;

        Status = SffdiskGetProperty(DeviceExtension,
                                    SDP_TOTAL_SECTORS,
                                    &TotalSectors,
                                    sizeof(TotalSectors));
        if (NT_SUCCESS(Status) && TotalSectors > 0)
        {
            DeviceExtension->TotalSectors = TotalSectors;
        }
    }

    /*
     * Fallback: read CSD via CMD9 if the bus driver did not
     * provide total sectors through the property.
     */
    if (DeviceExtension->TotalSectors == 0)
    {
        SDBUS_REQUEST_PACKET Srb;

        SD_INIT_REQUEST_PACKET(&Srb, SDRF_DEVICE_COMMAND);
        Srb.Parameters.DeviceCommand.CmdDesc.Cmd = SDCMD_SEND_CSD;
        Srb.Parameters.DeviceCommand.CmdDesc.CmdClass = SDCC_STANDARD;
        Srb.Parameters.DeviceCommand.CmdDesc.TransferDirection = SDTD_UNSPECIFIED;
        Srb.Parameters.DeviceCommand.CmdDesc.TransferType = SDTT_CMD_ONLY;
        Srb.Parameters.DeviceCommand.CmdDesc.ResponseType = SDRT_2;
        Srb.Parameters.DeviceCommand.Argument = 0;
        Srb.Parameters.DeviceCommand.Mdl = NULL;
        Srb.Parameters.DeviceCommand.Length = 0;

        Status = SdBusSubmitRequest(DeviceExtension->BusInterface.Context,
                                    &Srb);
        if (NT_SUCCESS(Status))
        {
            /*
             * R2 response: 128-bit CSD packed MSB-first in
             * ResponseData.AsULONG[3..0]. Parse the CSD structure
             * version from the top 2 bits of the first word.
             *
             * Note: SDHCI response registers contain CSD[127:8]
             * (hardware strips CRC and end bit). Bits map as:
             *   Response[3] bits [23:0]  = CSD[127:104]
             *   Response[2] bits [31:0]  = CSD[103:72]
             *   Response[1] bits [31:0]  = CSD[71:40]
             *   Response[0] bits [31:0]  = CSD[39:8]
             */
            UCHAR CsdStructure = (UCHAR)((Srb.ResponseData.AsULONG[3] >> 22) & 0x03);

            if (CsdStructure == 0)
            {
                /* CSD Version 1.0 (SDSC) */
                ULONG CSize;
                ULONG CSizeMult;
                ULONG ReadBlLen;

                /* READ_BL_LEN [83:80] -> Response[2] bits [11:8] */
                ReadBlLen = (Srb.ResponseData.AsULONG[2] >> 8) & 0x0F;

                /* C_SIZE [73:62] -> Response[2] bits [1:0] : Response[1] bits [31:22] */
                CSize = ((Srb.ResponseData.AsULONG[2] & 0x03) << 10) |
                        ((Srb.ResponseData.AsULONG[1] >> 22) & 0x3FF);

                /* C_SIZE_MULT [49:47] -> Response[1] bits [9:7] */
                CSizeMult = (Srb.ResponseData.AsULONG[1] >> 7) & 0x07;

                DeviceExtension->TotalSectors =
                    ((ULONGLONG)(CSize + 1) *
                     (1ULL << (CSizeMult + 2)) *
                     (1ULL << ReadBlLen)) / SD_DEFAULT_SECTOR_SIZE;
            }
            else if (CsdStructure == 1)
            {
                /* CSD Version 2.0 (SDHC/SDXC) */
                ULONG CSize;

                /* C_SIZE [69:48] -> Response[1] bits [29:8] (22 bits) */
                CSize = (Srb.ResponseData.AsULONG[1] >> 8) & 0x3FFFFF;

                DeviceExtension->TotalSectors =
                    (ULONGLONG)(CSize + 1) * 1024ULL;
            }
        }
        else
        {
            DPRINT1("SffdiskStartDevice: CMD9 failed (0x%08lx)\n", Status);
        }
    }

    if (DeviceExtension->TotalSectors == 0)
    {
        DPRINT1("SffdiskStartDevice: Unable to determine card capacity\n");
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto CleanupInterface;
    }

    /*
     */
    SffdiskComputeGeometry(DeviceExtension);

    DeviceExtension->DevicePowerState = PowerDeviceD0;
    DeviceExtension->Present = TRUE;
    DeviceExtension->SurpriseRemoved = FALSE;

    /*
     *
     */
    if (DeviceExtension->DiskInterfaceRegistered &&
        !DeviceExtension->DiskInterfaceEnabled)
    {
        Status = IoSetDeviceInterfaceState(&DeviceExtension->DiskInterfaceName,
                                           TRUE);
        if (NT_SUCCESS(Status))
        {
            DeviceExtension->DiskInterfaceEnabled = TRUE;
            DPRINT1("SffdiskStartDevice: Enabled disk interface %wZ\n",
                    &DeviceExtension->DiskInterfaceName);
        }
        else
        {
            DPRINT1("SffdiskStartDevice: IoSetDeviceInterfaceState(disk) failed 0x%08lx\n",
                    Status);
            goto CleanupInterface;
        }
    }

    /* Match disk.sys' public naming contract. Opening PhysicalDriveN reaches
       the top of the attached stack, including partmgr's performance and
       storage-property handlers. */
    Status = SffdiskCreatePhysicalDriveLink(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        goto CleanupInterface;
    }

    SffdiskUnblockBusRequests(DeviceExtension);

    DPRINT1("SffdiskStartDevice: Card type %d, %I64u sectors, %s, %s\n",
           DeviceExtension->CardType,
           DeviceExtension->TotalSectors,
           DeviceExtension->HighCapacity ? "HighCapacity" : "Standard",
           DeviceExtension->WriteProtected ? "WriteProtected" : "Writable");

    return STATUS_SUCCESS;

CleanupInterface:
    SffdiskStopDevice(DeviceExtension);
    return Status;
}

/** Create the well-known raw-disk name used by storage-management clients. */
static
NTSTATUS
SffdiskCreatePhysicalDriveLink(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    WCHAR LinkNameBuffer[64];
    UNICODE_STRING LinkName;
    NTSTATUS Status;

    if (DeviceExtension->PhysicalDriveLinkCreated)
    {
        return STATUS_SUCCESS;
    }

    if (DeviceExtension->DeviceName.Buffer == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    swprintf(LinkNameBuffer,
             RTL_NUMBER_OF(LinkNameBuffer),
             L"\\DosDevices\\PhysicalDrive%lu",
             DeviceExtension->DiskNumber);
    RtlInitUnicodeString(&LinkName, LinkNameBuffer);

    Status = IoCreateSymbolicLink(&LinkName, &DeviceExtension->DeviceName);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->PhysicalDriveLinkCreated = TRUE;
        DPRINT1("Sffdisk: Linked %wZ to %wZ\n",
                &LinkName, &DeviceExtension->DeviceName);
    }
    else
    {
        DPRINT1("Sffdisk: IoCreateSymbolicLink(%wZ) failed 0x%08lx\n",
                &LinkName, Status);
    }

    return Status;
}

static
VOID
SffdiskDeletePhysicalDriveLink(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    WCHAR LinkNameBuffer[64];
    UNICODE_STRING LinkName;

    if (!DeviceExtension->PhysicalDriveLinkCreated)
    {
        return;
    }

    swprintf(LinkNameBuffer,
             RTL_NUMBER_OF(LinkNameBuffer),
             L"\\DosDevices\\PhysicalDrive%lu",
             DeviceExtension->DiskNumber);
    RtlInitUnicodeString(&LinkName, LinkNameBuffer);
    IoDeleteSymbolicLink(&LinkName);
    DeviceExtension->PhysicalDriveLinkCreated = FALSE;
}

static
VOID
SffdiskStopDevice(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    SffdiskBlockBusRequests(DeviceExtension);
    DeviceExtension->Present = FALSE;
    DeviceExtension->MediaPresent = FALSE;

    if (DeviceExtension->DiskInterfaceEnabled)
    {
        IoSetDeviceInterfaceState(&DeviceExtension->DiskInterfaceName, FALSE);
        DeviceExtension->DiskInterfaceEnabled = FALSE;
    }

    SffdiskDeletePhysicalDriveLink(DeviceExtension);

    SffdiskCloseBusInterface(DeviceExtension);
}

static
VOID
SffdiskCleanupDevice(
    _Inout_ PSFFDISK_DEVICE_EXTENSION DeviceExtension)
{
    SffdiskStopDevice(DeviceExtension);

    if (DeviceExtension->HarddiskDirectory != NULL)
    {
        ZwMakeTemporaryObject(DeviceExtension->HarddiskDirectory);
        ZwClose(DeviceExtension->HarddiskDirectory);
        DeviceExtension->HarddiskDirectory = NULL;
    }

    /* Free the device name buffer */
    if (DeviceExtension->DeviceName.Buffer != NULL)
    {
        ExFreePoolWithTag(DeviceExtension->DeviceName.Buffer, TAG_SFFDISK);
        RtlInitUnicodeString(&DeviceExtension->DeviceName, NULL);
    }

    if (DeviceExtension->DiskInterfaceRegistered)
    {
        RtlFreeUnicodeString(&DeviceExtension->DiskInterfaceName);
        RtlInitUnicodeString(&DeviceExtension->DiskInterfaceName, NULL);
        DeviceExtension->DiskInterfaceRegistered = FALSE;
    }
}

/* DISPATCH FUNCTIONS *********************************************************/

/**
 * @brief Creates the functional device object (FDO) and attaches it to the
 *        device stack for an SD memory card.
 *
 * @param DriverObject Our driver object.
 * @param PhysicalDeviceObject The PDO created by the bus driver.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code.
 */
NTSTATUS
NTAPI
SffdiskAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    PCONFIGURATION_INFORMATION ConfigInfo;
    ULONG DiskNumber;
    WCHAR DirNameBuf[64];
    WCHAR DevNameBuf[64];
    UNICODE_STRING DirName;
    UNICODE_STRING DevName;
    OBJECT_ATTRIBUTES ObjAttrs;
    HANDLE DirHandle = NULL;

    PAGED_CODE();

    /*
     * Step 1: Claim the next disk number and create the
     * \\Device\\HarddiskN directory object (like disk.sys does).
     */
    ConfigInfo = IoGetConfigurationInformation();
    Status = SffdiskReserveDiskNumber(&ConfigInfo->DiskCount, &DiskNumber);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    swprintf(DirNameBuf,
             RTL_NUMBER_OF(DirNameBuf),
             L"\\Device\\Harddisk%lu",
             DiskNumber);
    RtlInitUnicodeString(&DirName, DirNameBuf);

    InitializeObjectAttributes(&ObjAttrs,
                               &DirName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_PERMANENT,
                               NULL,
                               NULL);

    Status = ZwCreateDirectoryObject(&DirHandle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjAttrs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskAddDevice: ZwCreateDirectoryObject(%wZ) failed 0x%08lx\n",
                &DirName, Status);
        return Status;
    }

    DPRINT1("SffdiskAddDevice: Created %wZ (Disk%lu)\n", &DirName, DiskNumber);

    /*
     * Step 2: Create the FDO with a proper device name
     * \\Device\\HarddiskN\\DRN (same convention as disk.sys).
     */
    swprintf(DevNameBuf,
             RTL_NUMBER_OF(DevNameBuf),
             L"\\Device\\Harddisk%lu\\DR%lu",
             DiskNumber,
             DiskNumber);
    RtlInitUnicodeString(&DevName, DevNameBuf);

    Status = IoCreateDevice(DriverObject,
                            sizeof(SFFDISK_DEVICE_EXTENSION),
                            &DevName,
                            FILE_DEVICE_DISK,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SffdiskAddDevice: IoCreateDevice failed (0x%08lx)\n", Status);
        ZwMakeTemporaryObject(DirHandle);
        ZwClose(DirHandle);
        return Status;
    }

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(SFFDISK_DEVICE_EXTENSION));

    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    DeviceExtension->DiskNumber = DiskNumber;
    DeviceExtension->HarddiskDirectory = DirHandle;

    /* Save a copy of the device name for symlink creation later */
    DeviceExtension->DeviceName.MaximumLength = sizeof(DevNameBuf);
    DeviceExtension->DeviceName.Buffer = ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DevNameBuf), TAG_SFFDISK);
    if (DeviceExtension->DeviceName.Buffer == NULL)
    {
        IoDeleteDevice(DeviceObject);
        ZwMakeTemporaryObject(DirHandle);
        ZwClose(DirHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyUnicodeString(&DeviceExtension->DeviceName, &DevName);

    /* Initialize the remove lock */
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, TAG_SFFDISK, 0, 0);
    KeInitializeSpinLock(&DeviceExtension->UsageLock);
    KeInitializeSpinLock(&DeviceExtension->BusRequestLock);
    KeInitializeEvent(&DeviceExtension->BusRequestsDrained,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&DeviceExtension->ChannelAvailable,
                      NotificationEvent,
                      TRUE);
    DeviceExtension->BusRequestsBlocked = TRUE;

    /* Attach to the device stack */
    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject,
                                                               PhysicalDeviceObject);
    if (DeviceExtension->LowerDevice == NULL)
    {
        DPRINT1("SffdiskAddDevice: IoAttachDeviceToDeviceStack failed\n");
        if (DeviceExtension->DeviceName.Buffer)
            ExFreePoolWithTag(DeviceExtension->DeviceName.Buffer, TAG_SFFDISK);
        IoDeleteDevice(DeviceObject);
        ZwMakeTemporaryObject(DirHandle);
        ZwClose(DirHandle);
        return STATUS_NO_SUCH_DEVICE;
    }

    /*
     * Set up device flags:
     * - Direct I/O for read/write operations
     * - Power pageable to allow paging path power IRPs
     */
    DeviceObject->Flags |= DO_DIRECT_IO;
    if (DeviceExtension->LowerDevice->Flags & DO_POWER_INRUSH)
    {
        DeviceObject->Flags |= DO_POWER_INRUSH;
    }
    else if (DeviceExtension->LowerDevice->Flags & DO_POWER_PAGABLE)
    {
        DeviceObject->Flags |= DO_POWER_PAGABLE;
    }
    DeviceExtension->PowerPagable =
        (DeviceObject->Flags & DO_POWER_PAGABLE) != 0;

    /* Match the alignment requirement of the lower device */
    DeviceObject->AlignmentRequirement = DeviceExtension->LowerDevice->AlignmentRequirement;

    DeviceExtension->DevicePowerState = PowerDeviceD0;

    {
        UNICODE_STRING InterfaceName;
        NTSTATUS IfStatus;

        RtlInitUnicodeString(&InterfaceName, NULL);

        IfStatus = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                             &GUID_DEVINTERFACE_DISK,
                                             NULL,
                                             &InterfaceName);
        if (NT_SUCCESS(IfStatus))
        {
            DeviceExtension->DiskInterfaceName = InterfaceName;
            DeviceExtension->DiskInterfaceRegistered = TRUE;
            DPRINT1("SffdiskAddDevice: Registered disk interface %wZ\n",
                    &InterfaceName);
        }
        else
        {
            DPRINT1("SffdiskAddDevice: IoRegisterDeviceInterface(disk) failed 0x%08lx\n",
                    IfStatus);
            IoDetachDevice(DeviceExtension->LowerDevice);
            ExFreePoolWithTag(DeviceExtension->DeviceName.Buffer, TAG_SFFDISK);
            DeviceExtension->DeviceName.Buffer = NULL;
            IoDeleteDevice(DeviceObject);
            ZwMakeTemporaryObject(DirHandle);
            ZwClose(DirHandle);
            return IfStatus;
        }
    }

    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT1("SffdiskAddDevice: FDO %p (%wZ) attached to PDO %p\n",
            DeviceObject, &DeviceExtension->DeviceName, PhysicalDeviceObject);

    return STATUS_SUCCESS;
}

/**
 * @brief Handles IRP_MJ_PNP requests for the SD disk device.
 *
 * @param DeviceObject Our FDO.
 * @param Irp The PnP IRP to handle.
 *
 * @return NTSTATUS from PnP processing.
 */
NTSTATUS
NTAPI
SffdiskPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    PAGED_CODE();

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
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
        case IRP_MN_START_DEVICE:
        {
            /* Forward the start IRP down the stack first */
            Status = SffdiskForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status))
            {
                /* Now initialize our device */
                Status = SffdiskStartDevice(DeviceExtension);
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_QUERY_STOP_DEVICE:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SffdiskForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }

        case IRP_MN_QUERY_REMOVE_DEVICE:
        {
            NTSTATUS RestoreStatus;

            SffdiskBlockBusRequests(DeviceExtension);
            DeviceExtension->Present = FALSE;
            SffdiskCloseBusInterface(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Status = SffdiskForwardIrpSynchronous(DeviceObject, Irp);
            if (!NT_SUCCESS(Status))
            {
                RestoreStatus = SffdiskOpenBusInterface(DeviceExtension);
                if (NT_SUCCESS(RestoreStatus))
                {
                    DeviceExtension->Present = TRUE;
                    SffdiskUnblockBusRequests(DeviceExtension);
                }
                else
                {
                    DPRINT1("SffdiskPnp: failed to reopen interface after rejected query-remove (0x%08lx)\n",
                            RestoreStatus);
                }
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_CANCEL_STOP_DEVICE:
        {
            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SffdiskForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        {
            NTSTATUS RestoreStatus;

            Irp->IoStatus.Status = STATUS_SUCCESS;
            Status = SffdiskForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status))
            {
                RestoreStatus = SffdiskOpenBusInterface(DeviceExtension);
                if (NT_SUCCESS(RestoreStatus))
                {
                    DeviceExtension->Present = TRUE;
                    SffdiskUnblockBusRequests(DeviceExtension);
                }
                else
                {
                    Status = RestoreStatus;
                }
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_STOP_DEVICE:
        {
            SffdiskStopDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SffdiskForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }

        case IRP_MN_SURPRISE_REMOVAL:
        {
            DeviceExtension->Present = FALSE;
            DeviceExtension->MediaPresent = FALSE;
            DeviceExtension->SurpriseRemoved = TRUE;
            DeviceObject->Flags |= DO_VERIFY_VOLUME;
            SffdiskStopDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            return SffdiskForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            PDEVICE_OBJECT LowerDevice = DeviceExtension->LowerDevice;

            SffdiskStopDevice(DeviceExtension);

            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            SffdiskCleanupDevice(DeviceExtension);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(LowerDevice, Irp);

            IoDetachDevice(LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities;

            /* Forward to the lower driver first */
            Status = SffdiskForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status))
            {
                Capabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;

                /* The SD bus owns slot-wiring policy. Cache its answer instead
                 * of replacing an embedded-slot result with a protocol guess. */
                DeviceExtension->Removable = Capabilities->Removable;
                DeviceExtension->RemovabilityKnown = TRUE;
                if (DeviceExtension->BytesPerSector != 0 &&
                    DeviceExtension->TotalSectors != 0)
                {
                    SffdiskComputeGeometry(DeviceExtension);
                }
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
        {
            DEVICE_USAGE_NOTIFICATION_TYPE Type;
            BOOLEAN InPath;

            Type = IrpSp->Parameters.UsageNotification.Type;
            InPath = IrpSp->Parameters.UsageNotification.InPath;
            Status = SffdiskAdjustDeviceUsage(DeviceExtension, Type, InPath);
            if (NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = STATUS_SUCCESS;
                Status = SffdiskForwardIrpSynchronous(DeviceObject, Irp);
                if (!NT_SUCCESS(Status))
                {
                    (VOID)SffdiskAdjustDeviceUsage(DeviceExtension,
                                                   Type,
                                                   !InPath);
                }
                else
                {
                    IoInvalidateDeviceState(DeviceExtension->PhysicalDevice);
                }
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        {
            Status = SffdiskForwardIrpSynchronous(DeviceObject, Irp);
            if (NT_SUCCESS(Status) &&
                SffdiskHasActiveDeviceUsage(DeviceExtension))
            {
                Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
            }

            Irp->IoStatus.Status = Status;
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        default:
        {
            /* Pass all other PnP IRPs down the stack */
            return SffdiskForwardIrpWithRemoveLock(DeviceExtension, Irp, FALSE);
        }
    }
}

static
NTSTATUS
NTAPI
SffdiskPowerUpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;

    UNREFERENCED_PARAMETER(Context);

    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    if (NT_SUCCESS(Irp->IoStatus.Status))
    {
        DeviceExtension->DevicePowerState =
            IrpSp->Parameters.Power.State.DeviceState;

        PoSetPowerState(DeviceObject,
                        IrpSp->Parameters.Power.Type,
                        IrpSp->Parameters.Power.State);
    }

    PoStartNextPowerIrp(Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SffdiskPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    if (IrpSp->MinorFunction == IRP_MN_SET_POWER &&
        IrpSp->Parameters.Power.Type == DevicePowerState)
    {
        DEVICE_POWER_STATE NewState = IrpSp->Parameters.Power.State.DeviceState;

        if (NewState == PowerDeviceD0)
        {
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp,
                                   SffdiskPowerUpCompletion,
                                   NULL,
                                   TRUE,
                                   TRUE,
                                   TRUE);
            return PoCallDriver(DeviceExtension->LowerDevice, Irp);
        }
        else
        {
            PoSetPowerState(DeviceObject,
                            IrpSp->Parameters.Power.Type,
                            IrpSp->Parameters.Power.State);
            DeviceExtension->DevicePowerState = NewState;
        }
    }

    PoStartNextPowerIrp(Irp);
    return SffdiskForwardIrpWithRemoveLock(DeviceExtension, Irp, TRUE);
}

/**
 * @brief Handles per-handle create, cleanup, and close lifecycle.
 *
 * @param DeviceObject Our FDO (unused).
 * @param Irp The create/close IRP to complete.
 *
 * Cleanup and close release any raw-command channel reservation owned by the
 * handle.
 */
NTSTATUS
NTAPI
SffdiskCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Status = STATUS_SUCCESS;

    switch (IrpSp->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (NT_SUCCESS(Status))
            {
                IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            }
            break;

        case IRP_MJ_CLEANUP:
            SffdiskReleaseChannelForFile(DeviceExtension, IrpSp->FileObject);
            break;

        case IRP_MJ_CLOSE:
            SffdiskReleaseChannelForFile(DeviceExtension, IrpSp->FileObject);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

/**
 * @brief Handles IRP_MJ_FLUSH_BUFFERS.
 *
 * The SD driver has no write cache, so flushes always succeed immediately.
 * The filesystem relies on this IRP being handled to commit dirty data
 * to the volume.
 *
 * @param DeviceObject Our FDO (unused).
 * @param Irp The flush IRP to complete.
 *
 * @return STATUS_SUCCESS always.
 */
NTSTATUS
NTAPI
SffdiskFlushBuffers(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSFFDISK_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DeviceExtension = (PSFFDISK_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

/* DRIVER ENTRY ***************************************************************/

/**
 * @brief Entry point for sffdisk.sys. Sets up dispatch routines.
 *
 * @param DriverObject Our driver object.
 * @param RegistryPath Path to our service registry key (unused).
 *
 * @return STATUS_SUCCESS.
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_PNP] = SffdiskPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SffdiskPower;
    DriverObject->MajorFunction[IRP_MJ_READ] = SffdiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = SffdiskReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SffdiskDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = SffdiskCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = SffdiskCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SffdiskCreateClose;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = SffdiskFlushBuffers;
    DriverObject->DriverExtension->AddDevice = SffdiskAddDevice;

    return STATUS_SUCCESS;
}
