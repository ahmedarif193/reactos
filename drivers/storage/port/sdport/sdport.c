/*
 * PROJECT:     ReactOS SD Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SD port driver main file - PnP, initialization, and IRP dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES *******************************************************************/

#include "sdport.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

SDPORT_INITIALIZATION_DATA SdPortMiniportInitData;
PDRIVER_OBJECT SdPortDriverObject = NULL;
ULONG SdPortNumber = 0;

/* FORWARD DECLARATIONS *******************************************************/

static NTSTATUS NTAPI SdPortDispatchCreate(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
static NTSTATUS NTAPI SdPortDispatchClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
static VOID NTAPI SdPortUnload(_In_ PDRIVER_OBJECT DriverObject);

static NTSTATUS SdPortPnpStartDevice(_In_ PSDPORT_FDO_EXTENSION FdoExtension, _In_ PIRP Irp);
static NTSTATUS SdPortPnpRemoveDevice(_In_ PSDPORT_FDO_EXTENSION FdoExtension, _In_ PIRP Irp);
static NTSTATUS SdPortPnpQueryRemove(_In_ PSDPORT_FDO_EXTENSION FdoExtension, _In_ PIRP Irp);
static NTSTATUS SdPortPnpCancelRemove(_In_ PSDPORT_FDO_EXTENSION FdoExtension, _In_ PIRP Irp);
static NTSTATUS SdPortPnpSurpriseRemoval(_In_ PSDPORT_FDO_EXTENSION FdoExtension, _In_ PIRP Irp);
static NTSTATUS SdPortForwardIrpSynchronous(_In_ PSDPORT_FDO_EXTENSION FdoExtension, _In_ PIRP Irp);
static NTSTATUS SdPortForwardPowerIrpSynchronous(_In_ PSDPORT_FDO_EXTENSION FdoExtension, _In_ PIRP Irp);
static NTSTATUS NTAPI SdPortForwardIrpCompletion(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PVOID Context);

static BOOLEAN NTAPI SdPortInterruptService(_In_ PKINTERRUPT Interrupt, _In_ PVOID Context);
static VOID NTAPI SdPortInterruptDpc(_In_ PKDPC Dpc, _In_opt_ PVOID DeferredContext, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2);
static VOID NTAPI SdPortCardChangeDpc(_In_ PKDPC Dpc, _In_opt_ PVOID DeferredContext, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2);
static VOID NTAPI SdPortCardChangeWorker(_In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Parameter);

static VOID SdPortCleanupDevice(_In_ PSDPORT_FDO_EXTENSION FdoExtension);
static NTSTATUS SdPortInitializeSlots(_In_ PSDPORT_FDO_EXTENSION FdoExtension);

/* FUNCTIONS ******************************************************************/

/**
 * @brief Initialize the SD port driver from a miniport's DriverEntry.
 *
 * Exported function called by the SDHCI miniport driver from its DriverEntry.
 * Saves the miniport callback table and sets up the driver dispatch routines
 * so that PnP can instantiate the FDO and start the device.
 *
 * @param[in] DriverObject        The driver object created by the I/O manager.
 * @param[in] RegistryPath        Registry path for driver parameters.
 * @param[in] InitializationData  Miniport callback table and private extension size.
 *
 * @return STATUS_SUCCESS on success, STATUS_INVALID_PARAMETER if the
 *         initialization data is NULL or missing required callbacks,
 *         or STATUS_REVISION_MISMATCH if the structure size is too small.
 */
NTSTATUS
NTAPI
SdPortInitialize(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PSDPORT_INITIALIZATION_DATA InitializationData)
{
    DPRINT1("SdPortInitialize(%p %wZ %p)\n",
           DriverObject, RegistryPath, InitializationData);

    /* Validate the initialization data */
    if (InitializationData == NULL)
    {
        DPRINT1("SdPortInitialize: InitializationData is NULL\n");
        return STATUS_INVALID_PARAMETER;
    }

    {
        ULONG LegacyMinSize =
            FIELD_OFFSET(SDPORT_INITIALIZATION_DATA, PoFxPowerControlCallback);

        if (InitializationData->StructureSize < LegacyMinSize)
        {
            DPRINT1("SdPortInitialize: StructureSize too small "
                    "(%lu < legacy min %lu)\n",
                    InitializationData->StructureSize,
                    LegacyMinSize);
            return STATUS_REVISION_MISMATCH;
        }
    }

    if (InitializationData->Initialize == NULL ||
        InitializationData->IssueRequest == NULL ||
        InitializationData->Interrupt == NULL)
    {
        DPRINT1("SdPortInitialize: Required callback is NULL "
                "(Initialize=%p IssueRequest=%p Interrupt=%p)\n",
                InitializationData->Initialize,
                InitializationData->IssueRequest,
                InitializationData->Interrupt);
        return STATUS_INVALID_PARAMETER;
    }

    SdPortDriverObject = DriverObject;
    RtlZeroMemory(&SdPortMiniportInitData, sizeof(SDPORT_INITIALIZATION_DATA));
    {
        ULONG CopyBytes = InitializationData->StructureSize;
        ULONG LegacyEndOffset =
            FIELD_OFFSET(SDPORT_INITIALIZATION_DATA, PoFxPowerControlCallback);

        if (CopyBytes > sizeof(SDPORT_INITIALIZATION_DATA))
        {
            CopyBytes = sizeof(SDPORT_INITIALIZATION_DATA);
        }

        if (CopyBytes < sizeof(SDPORT_INITIALIZATION_DATA))
        {
            ULONG LegacyPrivExtOffset = LegacyEndOffset;
            ULONG LegacyPrivExtValue = 0;

            RtlCopyMemory(&SdPortMiniportInitData,
                          InitializationData,
                          LegacyEndOffset);

            if (CopyBytes >= LegacyPrivExtOffset + sizeof(ULONG))
            {
                RtlCopyMemory(&LegacyPrivExtValue,
                              (PUCHAR)InitializationData + LegacyPrivExtOffset,
                              sizeof(ULONG));
            }
            SdPortMiniportInitData.PrivateExtensionSize = LegacyPrivExtValue;
        }
        else
        {
            RtlCopyMemory(&SdPortMiniportInitData, InitializationData, CopyBytes);
        }
    }
    SdPortMiniportInitData.StructureSize = sizeof(SDPORT_INITIALIZATION_DATA);

    /* Set up the driver dispatch routines */
    DriverObject->DriverExtension->AddDevice = SdPortAddDevice;
    DriverObject->DriverUnload = SdPortUnload;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = SdPortDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SdPortDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_PNP] = SdPortDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SdPortDispatchPower;

    return STATUS_SUCCESS;
}

/**
 * @brief Standard driver entry point for the SD port driver.
 *
 * For a port driver, the miniport calls SdPortInitialize from its own
 * DriverEntry, so this function is minimal and returns success immediately.
 *
 * @param[in] DriverObject  The driver object created by the I/O manager.
 * @param[in] RegistryPath  Registry path for driver parameters.
 *
 * @return STATUS_SUCCESS always.
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    DPRINT1("SDPORT DriverEntry: DriverObject=%p RegistryPath=%wZ\n",
           DriverObject, RegistryPath);

    return STATUS_SUCCESS;
}

/**
 * @brief PnP AddDevice routine for the SD port driver.
 *
 * Creates the FDO and attaches it to the device stack. Allocates the
 * miniport private extension at the end of the FDO extension, initializes
 * per-slot structures, interrupt DPC, and card change work item.
 *
 * @param[in] DriverObject          The driver object for the SD port driver.
 * @param[in] PhysicalDeviceObject  The PDO created by the parent bus driver.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code on failure.
 */
NTSTATUS
NTAPI
SdPortAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PSDPORT_FDO_EXTENSION FdoExtension;
    PDEVICE_OBJECT Fdo = NULL;
    WCHAR NameBuffer[80];
    UNICODE_STRING DeviceName;
    ULONG ExtensionSize;
    NTSTATUS Status;
    UCHAR Index;

    DPRINT1("SdPortAddDevice(%p %p)\n", DriverObject, PhysicalDeviceObject);

    /* Build the device name */
    _snwprintf(NameBuffer, RTL_NUMBER_OF(NameBuffer),
               L"\\Device\\SdPort%lu", SdPortNumber);
    NameBuffer[RTL_NUMBER_OF(NameBuffer) - 1] = UNICODE_NULL;
    RtlInitUnicodeString(&DeviceName, NameBuffer);
    SdPortNumber++;

    /*
     * Allocate the FDO extension plus room for the miniport private data.
     * The miniport private extension follows directly after the FDO extension.
     */
    ExtensionSize = sizeof(SDPORT_FDO_EXTENSION) +
                    SdPortMiniportInitData.PrivateExtensionSize;

    Status = IoCreateDevice(DriverObject,
                            ExtensionSize,
                            &DeviceName,
                            FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortAddDevice: IoCreateDevice failed (0x%08lx)\n", Status);
        return Status;
    }

    Fdo->Flags |= DO_DIRECT_IO;
    Fdo->Flags |= DO_POWER_PAGABLE;

    /* Initialize the FDO extension */
    FdoExtension = (PSDPORT_FDO_EXTENSION)Fdo->DeviceExtension;
    RtlZeroMemory(FdoExtension, ExtensionSize);

    FdoExtension->DeviceObject = Fdo;
    FdoExtension->PhysicalDeviceObject = PhysicalDeviceObject;
    FdoExtension->DeviceState = SdPortDeviceStateNotStarted;

    IoInitializeRemoveLock(&FdoExtension->RemoveLock, SDPORT_TAG_FDO, 0, 0);

    /* Copy the miniport callbacks */
    RtlCopyMemory(&FdoExtension->MiniportInitData,
                   &SdPortMiniportInitData,
                   sizeof(SDPORT_INITIALIZATION_DATA));

    /* Set up the miniport private extension pointer */
    FdoExtension->MiniportPrivateSize = SdPortMiniportInitData.PrivateExtensionSize;
    if (FdoExtension->MiniportPrivateSize > 0)
    {
        FdoExtension->MiniportPrivateExtension =
            (PUCHAR)FdoExtension + sizeof(SDPORT_FDO_EXTENSION);
    }

    /* Initialize per-slot structures */
    for (Index = 0; Index < SDPORT_MAX_SLOTS; Index++)
    {
        PSDPORT_SLOT_EXTENSION Slot = &FdoExtension->Slots[Index];

        Slot->SlotIndex = Index;
        Slot->FdoExtension = FdoExtension;
        KeInitializeEvent(&Slot->RequestEvent,
                          SynchronizationEvent,
                          FALSE);
        Slot->State = SdPortRequestStateIdle;

        KeInitializeSpinLock(&Slot->RequestLock);
        InitializeListHead(&Slot->PendingRequests);
        Slot->ActiveRequest = NULL;

        Slot->UsePortPio = FALSE;
    }

    /* Initialize the interrupt DPC */
    KeInitializeDpc(&FdoExtension->InterruptDpc,
                    SdPortInterruptDpc,
                    FdoExtension);

    /* Initialize the card change DPC */
    KeInitializeDpc(&FdoExtension->CardChangeDpc,
                    SdPortCardChangeDpc,
                    FdoExtension);
    FdoExtension->CardChangeWorkItemQueued = 0;
    FdoExtension->CardChangePending = 0;

    /* Attach to the device stack */
    FdoExtension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo,
                                                             PhysicalDeviceObject);
    if (FdoExtension->LowerDevice == NULL)
    {
        DPRINT1("SdPortAddDevice: IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    FdoExtension->CardChangeWorkItem = IoAllocateWorkItem(Fdo);
    if (FdoExtension->CardChangeWorkItem == NULL)
    {
        DPRINT1("SdPortAddDevice: IoAllocateWorkItem failed\n");
        IoDetachDevice(FdoExtension->LowerDevice);
        IoDeleteDevice(Fdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    DPRINT1("SdPortAddDevice: Created FDO %p, extension %p\n", Fdo, FdoExtension);
    return STATUS_SUCCESS;
}

/**
 * @brief Dispatch routine for IRP_MJ_CREATE requests.
 *
 * Completes the IRP with STATUS_SUCCESS, allowing handles to be opened
 * to the SD port device.
 *
 * @param[in] DeviceObject  The SD port FDO device object.
 * @param[in] Irp           The create IRP to process.
 *
 * @return STATUS_SUCCESS always.
 */
static
NTSTATUS
NTAPI
SdPortDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = FILE_OPENED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Dispatch routine for IRP_MJ_CLOSE requests.
 *
 * Completes the IRP with STATUS_SUCCESS, allowing handles to the SD port
 * device to be closed.
 *
 * @param[in] DeviceObject  The SD port FDO device object.
 * @param[in] Irp           The close IRP to process.
 *
 * @return STATUS_SUCCESS always.
 */
static
NTSTATUS
NTAPI
SdPortDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/**
 * @brief Driver unload routine for the SD port driver.
 *
 * Called when the driver is being unloaded from memory.
 *
 * @param[in] DriverObject  The driver object being unloaded.
 */
static
VOID
NTAPI
SdPortUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DPRINT1("SdPortUnload(%p)\n", DriverObject);
}

/**
 * @brief Dispatch routine for IRP_MJ_PNP requests.
 *
 * Acquires the remove lock and dispatches PnP minor function codes to the
 * appropriate handler. Unhandled PnP IRPs are passed down the device stack.
 *
 * @param[in] DeviceObject  The SD port FDO device object.
 * @param[in] Irp           The PnP IRP to process.
 *
 * @return STATUS_SUCCESS or an NTSTATUS error code from the PnP handler.
 */
NTSTATUS
NTAPI
SdPortDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PSDPORT_FDO_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IrpStack;
    NTSTATUS Status;

    FdoExtension = (PSDPORT_FDO_EXTENSION)DeviceObject->DeviceExtension;
    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT1("SdPortDispatchPnp: MinorFunction=0x%02x\n",
           IrpStack->MinorFunction);

    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = SdPortPnpStartDevice(FdoExtension, Irp);
            break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            Status = SdPortPnpQueryRemove(FdoExtension, Irp);
            break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = SdPortPnpCancelRemove(FdoExtension, Irp);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            Status = SdPortPnpSurpriseRemoval(FdoExtension, Irp);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = SdPortPnpRemoveDevice(FdoExtension, Irp);
            /* Remove lock released inside SdPortPnpRemoveDevice */
            return Status;

        case IRP_MN_STOP_DEVICE:
            SdPortCleanupDevice(FdoExtension);
            FdoExtension->DeviceState = SdPortDeviceStateStopped;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
            FdoExtension->DeviceState = SdPortDeviceStateStopPending;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
            return Status;

        case IRP_MN_CANCEL_STOP_DEVICE:
            FdoExtension->DeviceState = SdPortDeviceStateStarted;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
            return Status;

        default:
            /* Pass unhandled PnP IRPs down the stack */
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
            return Status;
    }

    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    return Status;
}

/**
 * @brief Handle IRP_MN_START_DEVICE for the SD port FDO.
 *
 * Passes the IRP down first so the bus driver can assign resources, then
 * maps memory resources, connects the interrupt, calls the miniport
 * Initialize callback, and initializes all controller slots.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 * @param[in] Irp           The IRP_MN_START_DEVICE IRP.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code on failure.
 */
static
NTSTATUS
SdPortPnpStartDevice(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack;
    PCM_RESOURCE_LIST AllocatedResources;
    PCM_RESOURCE_LIST TranslatedResources;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;
    NTSTATUS Status;

    DPRINT1("SdPortPnpStartDevice(%p %p)\n", FdoExtension, Irp);

    /* Pass the IRP down first */
    Status = SdPortForwardIrpSynchronous(FdoExtension, Irp);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortPnpStartDevice: Lower driver failed START (0x%08lx)\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    AllocatedResources = IrpStack->Parameters.StartDevice.AllocatedResources;
    TranslatedResources = IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated;

    if (TranslatedResources == NULL || AllocatedResources == NULL)
    {
        DPRINT1("SdPortPnpStartDevice: No resources allocated\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* Parse the translated resource list for memory and interrupt */
    FdoExtension->MappedMemory = FALSE;
    FdoExtension->InterruptConnected = FALSE;

    for (Index = 0;
         Index < TranslatedResources->List[0].PartialResourceList.Count;
         Index++)
    {
        Descriptor = &TranslatedResources->List[0].PartialResourceList.PartialDescriptors[Index];

        switch (Descriptor->Type)
        {
            case CmResourceTypeMemory:
            {
                /* Map the SDHCI register space */
                if (!FdoExtension->MappedMemory)
                {
                    FdoExtension->MappedPhysicalBase = Descriptor->u.Memory.Start;
                    FdoExtension->MappedLength = Descriptor->u.Memory.Length;
                    FdoExtension->MappedVirtualBase =
                        MmMapIoSpace(Descriptor->u.Memory.Start,
                                     Descriptor->u.Memory.Length,
                                     MmNonCached);

                    if (FdoExtension->MappedVirtualBase == NULL)
                    {
                        DPRINT1("SdPortPnpStartDevice: MmMapIoSpace failed\n");
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        goto Cleanup;
                    }

                    FdoExtension->MappedMemory = TRUE;
                    DPRINT1("SdPortPnpStartDevice: Mapped 0x%I64x -> %p (%lu bytes)\n",
                           Descriptor->u.Memory.Start.QuadPart,
                           FdoExtension->MappedVirtualBase,
                           FdoExtension->MappedLength);
                }
                break;
            }

            case CmResourceTypeInterrupt:
            {
                if (!FdoExtension->InterruptConnected)
                {
                    FdoExtension->InterruptVector = Descriptor->u.Interrupt.Vector;
                    FdoExtension->InterruptLevel = (KIRQL)Descriptor->u.Interrupt.Level;
                    FdoExtension->InterruptAffinity = Descriptor->u.Interrupt.Affinity;
                    FdoExtension->InterruptMode =
                        (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                        Latched : LevelSensitive;

                    DPRINT1("SdPortPnpStartDevice: Interrupt vector=%lu level=%u\n",
                           FdoExtension->InterruptVector,
                           FdoExtension->InterruptLevel);
                }
                break;
            }
        }
    }

    if (!FdoExtension->MappedMemory)
    {
        DPRINT1("SdPortPnpStartDevice: No memory resource found\n");
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Cleanup;
    }

    /* Call the miniport Initialize callback */
    Status = FdoExtension->MiniportInitData.Initialize(
                FdoExtension->MiniportPrivateExtension,
                FdoExtension->MappedPhysicalBase,
                FdoExtension->MappedVirtualBase,
                FdoExtension->MappedLength,
                FALSE /* CrashdumpMode */);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortPnpStartDevice: Miniport Initialize failed (0x%08lx)\n", Status);
        goto Cleanup;
    }

    /* Connect the interrupt */
    Status = IoConnectInterrupt(&FdoExtension->InterruptObject,
                                SdPortInterruptService,
                                FdoExtension,
                                NULL,
                                FdoExtension->InterruptVector,
                                FdoExtension->InterruptLevel,
                                FdoExtension->InterruptLevel,
                                FdoExtension->InterruptMode,
                                TRUE, /* ShareVector */
                                FdoExtension->InterruptAffinity,
                                FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortPnpStartDevice: IoConnectInterrupt failed (0x%08lx)\n", Status);
        goto Cleanup;
    }
    FdoExtension->InterruptConnected = TRUE;

    /* Query the miniport for slot count and initialize each slot */
    Status = SdPortInitializeSlots(FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdPortPnpStartDevice: SdPortInitializeSlots failed (0x%08lx)\n", Status);
        goto Cleanup;
    }

    FdoExtension->DeviceState = SdPortDeviceStateStarted;

    /* Enable card insertion/removal interrupts on all slots */
    if (FdoExtension->MiniportInitData.ToggleEvents != NULL)
    {
        FdoExtension->MiniportInitData.ToggleEvents(
            FdoExtension->MiniportPrivateExtension,
            SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL,
            TRUE);
    }

    Status = STATUS_SUCCESS;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;

Cleanup:
    SdPortCleanupDevice(FdoExtension);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/**
 * @brief Handle IRP_MN_REMOVE_DEVICE for the SD port FDO.
 *
 * Cleans up hardware resources, passes the IRP to the lower driver, waits
 * for all pending I/O via IoReleaseRemoveLockAndWait, then detaches and
 * deletes the FDO.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 * @param[in] Irp           The IRP_MN_REMOVE_DEVICE IRP.
 *
 * @return The NTSTATUS code returned by the lower driver.
 */
static
NTSTATUS
SdPortPnpRemoveDevice(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    PDEVICE_OBJECT LowerDevice;
    NTSTATUS Status;

    DPRINT1("SdPortPnpRemoveDevice(%p %p)\n", FdoExtension, Irp);

    FdoExtension->DeviceState = SdPortDeviceStateRemoved;

    /* Clean up hardware resources and miniport state */
    SdPortCleanupDevice(FdoExtension);

    /* Pass the IRP to the lower driver */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    LowerDevice = FdoExtension->LowerDevice;
    Status = IoCallDriver(LowerDevice, Irp);

    /* Wait for pending I/O and release the remove lock */
    IoReleaseRemoveLockAndWait(&FdoExtension->RemoveLock, Irp);

    if (FdoExtension->CardChangeWorkItem != NULL)
    {
        IoFreeWorkItem(FdoExtension->CardChangeWorkItem);
        FdoExtension->CardChangeWorkItem = NULL;
    }

    /* Detach and delete our device */
    IoDetachDevice(LowerDevice);
    IoDeleteDevice(FdoExtension->DeviceObject);

    return Status;
}

/**
 * @brief Handle IRP_MN_QUERY_REMOVE_DEVICE for the SD port FDO.
 *
 * Transitions the device to the remove-pending state and passes the IRP
 * down the device stack.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 * @param[in] Irp           The IRP_MN_QUERY_REMOVE_DEVICE IRP.
 *
 * @return The NTSTATUS code returned by the lower driver.
 */
static
NTSTATUS
SdPortPnpQueryRemove(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    NTSTATUS Status;

    FdoExtension->DeviceState = SdPortDeviceStateRemovePending;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
    return Status;
}

/**
 * @brief Handle IRP_MN_CANCEL_REMOVE_DEVICE for the SD port FDO.
 *
 * Restores the device to the started state and passes the IRP down
 * the device stack.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 * @param[in] Irp           The IRP_MN_CANCEL_REMOVE_DEVICE IRP.
 *
 * @return The NTSTATUS code returned by the lower driver.
 */
static
NTSTATUS
SdPortPnpCancelRemove(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    NTSTATUS Status;

    FdoExtension->DeviceState = SdPortDeviceStateStarted;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
    return Status;
}

/**
 * @brief Handle IRP_MN_SURPRISE_REMOVAL for the SD port FDO.
 *
 * Marks all slots as card-removed, transitions the device state, and
 * passes the IRP down the device stack.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 * @param[in] Irp           The IRP_MN_SURPRISE_REMOVAL IRP.
 *
 * @return The NTSTATUS code returned by the lower driver.
 */
static
NTSTATUS
SdPortPnpSurpriseRemoval(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    NTSTATUS Status;

    FdoExtension->DeviceState = SdPortDeviceStateSurpriseRemoval;

    /* Mark all slots as card removed */
    for (UCHAR i = 0; i < FdoExtension->SlotCount; i++)
    {
        FdoExtension->Slots[i].CardPresent = FALSE;
        FdoExtension->Slots[i].Initialized = FALSE;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
    return Status;
}

/**
 * @brief Completion routine for synchronous IRP forwarding.
 *
 * Signals the KEVENT passed as the Context parameter so that the thread
 * waiting in SdPortForwardIrpSynchronous can resume.
 *
 * @param[in] DeviceObject  The device object (unused).
 * @param[in] Irp           The completed IRP (unused).
 * @param[in] Context       Pointer to a KEVENT to be signaled.
 *
 * @return STATUS_MORE_PROCESSING_REQUIRED to prevent further completion.
 */
static
NTSTATUS
NTAPI
SdPortForwardIrpCompletion(
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

/**
 * @brief Forward an IRP to the lower driver and wait for completion.
 *
 * Copies the current IRP stack location, sets a completion routine that
 * signals a local event, sends the IRP down, and waits if the lower
 * driver returned STATUS_PENDING.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 * @param[in] Irp           The IRP to forward synchronously.
 *
 * @return The final NTSTATUS from the lower driver.
 */
static
NTSTATUS
SdPortForwardIrpSynchronous(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SdPortForwardIrpCompletion,
                           &Event,
                           TRUE, TRUE, TRUE);

    Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
SdPortForwardPowerIrpSynchronous(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           SdPortForwardIrpCompletion,
                           &Event,
                           TRUE, TRUE, TRUE);

    Status = PoCallDriver(FdoExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

/**
 * @brief Clean up all hardware resources and miniport state.
 *
 * Disconnects the interrupt, frees ADMA descriptor buffers for all slots,
 * calls the miniport Cleanup callback, and unmaps the register space.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 */
static
VOID
SdPortCleanupDevice(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension)
{
    UCHAR Index;

    DPRINT1("SdPortCleanupDevice(%p)\n", FdoExtension);

    /* Disconnect the interrupt */
    if (FdoExtension->InterruptConnected && FdoExtension->InterruptObject != NULL)
    {
        IoDisconnectInterrupt(FdoExtension->InterruptObject);
        FdoExtension->InterruptObject = NULL;
        FdoExtension->InterruptConnected = FALSE;
    }

    /* Free ADMA descriptors for each slot */
    for (Index = 0; Index < FdoExtension->SlotCount; Index++)
    {
        SdPortFreeAdmaDescriptors(&FdoExtension->Slots[Index]);
    }

    /* Call the miniport Cleanup callback */
    if (FdoExtension->MiniportInitData.Cleanup != NULL &&
        FdoExtension->MiniportPrivateExtension != NULL)
    {
        FdoExtension->MiniportInitData.Cleanup(
            FdoExtension->MiniportPrivateExtension);
    }

    /* Unmap the register space */
    if (FdoExtension->MappedMemory && FdoExtension->MappedVirtualBase != NULL)
    {
        MmUnmapIoSpace(FdoExtension->MappedVirtualBase,
                        FdoExtension->MappedLength);
        FdoExtension->MappedVirtualBase = NULL;
        FdoExtension->MappedMemory = FALSE;
    }
}

/**
 * @brief Query slot count and initialize all controller slots.
 *
 * Queries the miniport for the number of slots, retrieves capabilities for
 * each slot, allocates ADMA2 descriptors if supported, checks card detect
 * state, and attempts card initialization for any inserted cards.
 *
 * @param[in] FdoExtension  Pointer to the FDO device extension.
 *
 * @return STATUS_SUCCESS on success, or an NTSTATUS error code if the
 *         slot count query fails or returns an invalid value.
 */
static
NTSTATUS
SdPortInitializeSlots(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension)
{
    UCHAR SlotCount = 0;
    UCHAR Index;
    PSDPORT_SLOT_EXTENSION SlotExtension;
    NTSTATUS Status;

    if (FdoExtension->MiniportInitData.GetSlotCount != NULL)
    {
        Status = FdoExtension->MiniportInitData.GetSlotCount(
                    FdoExtension->MiniportPrivateExtension,
                    &SlotCount);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("SdPortInitializeSlots: GetSlotCount failed (0x%08lx)\n",
                    Status);
            return Status;
        }
    }
    else
    {
        SlotCount = 1;
    }

    if (SlotCount == 0 || SlotCount > SDPORT_MAX_SLOTS)
    {
        DPRINT1("SdPortInitializeSlots: Invalid slot count %u\n", SlotCount);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    FdoExtension->SlotCount = SlotCount;
    DPRINT1("SdPortInitializeSlots: Controller has %u slot(s)\n", SlotCount);

    for (Index = 0; Index < SlotCount; Index++)
    {
        SlotExtension = &FdoExtension->Slots[Index];
        SlotExtension->SlotIndex = Index;
        SlotExtension->FdoExtension = FdoExtension;

        if (FdoExtension->MiniportInitData.GetSlotCapabilities != NULL)
        {
            FdoExtension->MiniportInitData.GetSlotCapabilities(
                FdoExtension->MiniportPrivateExtension,
                Index,
                &SlotExtension->Capabilities);
        }

        DPRINT1("SdPortInitializeSlots: Slot %u: BaseClock=%lu kHz, "
               "ADMA2=%u, HS=%u\n",
               Index,
               SlotExtension->Capabilities.BaseClockFrequencyKhz,
               SlotExtension->Capabilities.Adma2Supported,
               SlotExtension->Capabilities.HighSpeedSupported);

        /* Allocate ADMA2 descriptors if supported */
        if (SlotExtension->Capabilities.Adma2Supported)
        {
            Status = SdPortAllocateAdmaDescriptors(SlotExtension);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdPortInitializeSlots: ADMA alloc failed for slot %u\n", Index);
                /* Fall back to PIO, not a fatal error */
            }
        }

        if (FdoExtension->MiniportInitData.GetCardDetectState != NULL)
        {
            SlotExtension->CardPresent =
                FdoExtension->MiniportInitData.GetCardDetectState(
                    FdoExtension->MiniportPrivateExtension);
        }
        else
        {
            SlotExtension->CardPresent = TRUE;
        }

        if (SlotExtension->CardPresent)
        {
            DPRINT1("SdPortInitializeSlots: Card detected in slot %u\n", Index);
            Status = SdPortInitializeCard(FdoExtension, SlotExtension);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("SdPortInitializeSlots: Card init failed for slot %u (0x%08lx)\n",
                        Index, Status);
                SlotExtension->Initialized = FALSE;
                /* Non-fatal: the card may be inserted later or be unsupported */
            }
        }
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Interrupt service routine for the SD host controller.
 *
 * Calls into the miniport Interrupt callback to determine the source of the
 * interrupt. Accumulates events/errors into the slot extension using
 * interlocked operations and schedules the interrupt DPC or card change
 * DPC as appropriate.
 *
 * @param[in] Interrupt  Pointer to the interrupt object (unused).
 * @param[in] Context    Pointer to the SDPORT_FDO_EXTENSION for this controller.
 *
 * @return TRUE if the interrupt was handled, FALSE otherwise.
 */
static
BOOLEAN
NTAPI
SdPortInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID Context)
{
    PSDPORT_FDO_EXTENSION FdoExtension = (PSDPORT_FDO_EXTENSION)Context;
    ULONG Events = 0;
    ULONG Errors = 0;
    BOOLEAN NotifyCardChange = FALSE;
    BOOLEAN NotifySdioInterrupt = FALSE;
    BOOLEAN NotifyTuning = FALSE;
    BOOLEAN Handled;

    UNREFERENCED_PARAMETER(Interrupt);

    if (FdoExtension->DeviceState != SdPortDeviceStateStarted)
    {
        return FALSE;
    }

    /* Call the miniport ISR to determine what happened */
    Handled = FdoExtension->MiniportInitData.Interrupt(
                  FdoExtension->MiniportPrivateExtension,
                  &Events,
                  &Errors,
                  &NotifyCardChange,
                  &NotifySdioInterrupt,
                  &NotifyTuning);

    if (!Handled)
    {
        return FALSE;
    }

    if (Events != 0 || Errors != 0)
    {
        UCHAR TargetSlot = 0;
        UCHAR Index;

        for (Index = 0; Index < FdoExtension->SlotCount; Index++)
        {
            if (FdoExtension->Slots[Index].CurrentRequest != NULL)
            {
                TargetSlot = Index;
                break;
            }
        }

        if (FdoExtension->SlotCount > 0)
        {
            InterlockedOr(&FdoExtension->Slots[TargetSlot].LastEvents,
                          (LONG)Events);
            InterlockedOr(&FdoExtension->Slots[TargetSlot].LastErrors,
                          (LONG)Errors);
        }
        KeInsertQueueDpc(&FdoExtension->InterruptDpc, NULL, NULL);
    }

    /* Schedule card change DPC if requested */
    if (NotifyCardChange)
    {
        KeInsertQueueDpc(&FdoExtension->CardChangeDpc, NULL, NULL);
    }

    return TRUE;
}

static
NTSTATUS
SdPortErrorsToStatus(
    _In_ ULONG Errors)
{
    if (Errors & (SDHCI_INT_CMD_TIMEOUT >> 16))
    {
        return STATUS_SD_CMD_TIMEOUT;
    }
    if (Errors & (SDHCI_INT_CMD_CRC >> 16))
    {
        return STATUS_SD_CMD_CRC_ERROR;
    }
    if (Errors & (SDHCI_INT_DATA_TIMEOUT >> 16))
    {
        return STATUS_SD_DATA_TIMEOUT;
    }
    if (Errors & (SDHCI_INT_DATA_CRC >> 16))
    {
        return STATUS_SD_DATA_CRC_ERROR;
    }
    if (Errors & (SDHCI_INT_ADMA >> 16))
    {
        return STATUS_SD_ADMA_ERROR;
    }
    return STATUS_SD_IO_ERROR;
}

static
BOOLEAN
SdPortPumpManagedPio(
    _In_ PSDPORT_FDO_EXTENSION FdoExtension,
    _In_ PSDPORT_SLOT_EXTENSION SlotExtension,
    _In_ PSDPORT_REQUEST Request,
    _In_ ULONG Events)
{
    PVOID BufferVa;
    PUCHAR BlockPtr;
    ULONG WordsPerBlock;
    ULONG BlockSize;
    ULONG i;
    ULONG Word;

    if (!SlotExtension->UsePortPio || Request == NULL)
    {
        return FALSE;
    }

    BlockSize = Request->PioBytesPerBlock;
    if (BlockSize == 0 || FdoExtension->MappedVirtualBase == NULL)
    {
        return FALSE;
    }

    WordsPerBlock = BlockSize / sizeof(ULONG);

    if (Request->DataMdl != NULL)
    {
        BufferVa = MmGetMdlVirtualAddress(Request->DataMdl);
    }
    else
    {
        BufferVa = Request->DataBuffer;
    }

    if (BufferVa == NULL)
    {
        return FALSE;
    }

    BlockPtr = (PUCHAR)BufferVa + Request->PioBytesDone;

    if (Events & SDHCI_INT_BUFFER_READ_READY)
    {
        for (i = 0; i < WordsPerBlock; i++)
        {
            Word = READ_REGISTER_ULONG(
                (PULONG)((PUCHAR)FdoExtension->MappedVirtualBase +
                         SDHCI_BUFFER_DATA_PORT));
            RtlCopyMemory(BlockPtr + i * sizeof(ULONG), &Word, sizeof(ULONG));
        }
        Request->PioBytesDone += BlockSize;
    }
    else if (Events & SDHCI_INT_BUFFER_WRITE_READY)
    {
        for (i = 0; i < WordsPerBlock; i++)
        {
            RtlCopyMemory(&Word, BlockPtr + i * sizeof(ULONG), sizeof(ULONG));
            WRITE_REGISTER_ULONG(
                (PULONG)((PUCHAR)FdoExtension->MappedVirtualBase +
                         SDHCI_BUFFER_DATA_PORT),
                Word);
        }
        Request->PioBytesDone += BlockSize;
    }
    else
    {
        return FALSE;
    }

    Request->BytesTransferred = Request->PioBytesDone;
    return (Request->PioBytesDone >=
            (ULONG)(Request->BlockSize * Request->BlockCount));
}

/**
 * @brief DPC routine for SD command/transfer completion.
 *
 *
 */
static
VOID
NTAPI
SdPortInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PSDPORT_FDO_EXTENSION FdoExtension = (PSDPORT_FDO_EXTENSION)DeferredContext;
    UCHAR SlotIndex;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (FdoExtension == NULL || FdoExtension->SlotCount == 0)
    {
        return;
    }

    for (SlotIndex = 0; SlotIndex < FdoExtension->SlotCount; SlotIndex++)
    {
        PSDPORT_SLOT_EXTENSION SlotExtension = &FdoExtension->Slots[SlotIndex];
        PSDPORT_REQUEST Request;
        ULONG Events;
        ULONG Errors;
        BOOLEAN SignalCompletion = FALSE;

        Events = (ULONG)InterlockedExchange(&SlotExtension->LastEvents, 0);
        Errors = (ULONG)InterlockedExchange(&SlotExtension->LastErrors, 0);

        if (Events == 0 && Errors == 0)
        {
            continue;
        }

        Request = SlotExtension->CurrentRequest;

        if (FdoExtension->MiniportInitData.RequestDpc != NULL && Request != NULL)
        {
            FdoExtension->MiniportInitData.RequestDpc(
                FdoExtension->MiniportPrivateExtension,
                Request,
                Events,
                Errors);
        }

        SdPortPumpManagedPio(FdoExtension,
                             SlotExtension,
                             Request,
                             Events);

        if (Errors != 0)
        {
            SlotExtension->State = SdPortRequestStateErrorRecovery;
            SlotExtension->RequestStatus = SdPortErrorsToStatus(Errors);
            SignalCompletion = TRUE;
        }
        else
        {
            switch (SlotExtension->State)
            {
                case SdPortRequestStateCommand:
                    if (Events & SDHCI_INT_CMD_COMPLETE)
                    {
                        if (SlotExtension->RequestHasData)
                        {
                            SlotExtension->State = SdPortRequestStateData;
                        }
                        else if (Request != NULL &&
                                 Request->Command.ResponseType == SDRT_1B)
                        {
                            SlotExtension->State = SdPortRequestStateBusyEnd;
                            SlotExtension->RequestStatus = STATUS_SUCCESS;
                            SignalCompletion = TRUE;
                        }
                        else
                        {
                            SlotExtension->State = SdPortRequestStateCompleted;
                            SlotExtension->RequestStatus = STATUS_SUCCESS;
                            SignalCompletion = TRUE;
                        }
                    }

                    if (SlotExtension->State != SdPortRequestStateData)
                    {
                        break;
                    }

                case SdPortRequestStateData:
                    if (Events & SDHCI_INT_XFER_COMPLETE)
                    {
                        SlotExtension->State = SdPortRequestStateCompleted;
                        SlotExtension->RequestStatus = STATUS_SUCCESS;
                        SignalCompletion = TRUE;
                    }
                    break;

                case SdPortRequestStateBusyEnd:
                    if (Events & SDHCI_INT_XFER_COMPLETE)
                    {
                        SlotExtension->State = SdPortRequestStateCompleted;
                    }
                    break;

                case SdPortRequestStateIdle:
                case SdPortRequestStateResponse:
                case SdPortRequestStateErrorRecovery:
                case SdPortRequestStateCompleted:
                default:
                    if (SlotExtension->RequestHasData)
                    {
                        if (Events & SDHCI_INT_XFER_COMPLETE)
                        {
                            SlotExtension->RequestStatus = STATUS_SUCCESS;
                            SignalCompletion = TRUE;
                        }
                    }
                    else if (Events & SDHCI_INT_CMD_COMPLETE)
                    {
                        SlotExtension->RequestStatus = STATUS_SUCCESS;
                        SignalCompletion = TRUE;
                    }
                    break;
            }
        }

        if (SignalCompletion)
        {
            KeSetEvent(&SlotExtension->RequestEvent, IO_NO_INCREMENT, FALSE);

            if (Request != NULL && Request->CompletionRoutine != NULL)
            {
                KIRQL OldIrql;
                PSDPORT_REQUEST Next = NULL;

                PSDPORT_REQUEST_COMPLETION_ROUTINE Routine = Request->CompletionRoutine;
                PVOID Ctx = Request->CompletionContext;

                Request->Status = SlotExtension->RequestStatus;

                KeAcquireSpinLock(&SlotExtension->RequestLock, &OldIrql);
                SlotExtension->ActiveRequest = NULL;
                if (!IsListEmpty(&SlotExtension->PendingRequests))
                {
                    PLIST_ENTRY Entry =
                        RemoveHeadList(&SlotExtension->PendingRequests);
                    Next = CONTAINING_RECORD(Entry, SDPORT_REQUEST, QueueLink);
                    SlotExtension->ActiveRequest = Next;
                }
                else
                {
                    SlotExtension->CurrentRequest = NULL;
                    SlotExtension->RequestHasData = FALSE;
                    SlotExtension->State = SdPortRequestStateIdle;
                }
                KeReleaseSpinLock(&SlotExtension->RequestLock, OldIrql);

                Routine(FdoExtension, Request, Ctx);

                if (Next != NULL)
                {
                    SlotExtension->CurrentRequest = Next;
                    SlotExtension->RequestHasData =
                        (Next->Command.TransferType != SDTT_CMD_ONLY &&
                         Next->Command.TransferType != SDTT_UNSPECIFIED);
                    SlotExtension->State = SdPortRequestStateCommand;
                    Next->State = SdPortRequestStateCommand;
                    Next->PioBytesDone = 0;
                    Next->PioBytesPerBlock = Next->BlockSize;

                    Next->Status = FdoExtension->MiniportInitData.IssueRequest(
                        FdoExtension->MiniportPrivateExtension,
                        Next);
                    if (!NT_SUCCESS(Next->Status))
                    {
                        PSDPORT_REQUEST_COMPLETION_ROUTINE NextRoutine =
                            Next->CompletionRoutine;
                        PVOID NextCtx = Next->CompletionContext;

                        KeAcquireSpinLock(&SlotExtension->RequestLock, &OldIrql);
                        SlotExtension->ActiveRequest = NULL;
                        SlotExtension->CurrentRequest = NULL;
                        KeReleaseSpinLock(&SlotExtension->RequestLock, OldIrql);

                        if (NextRoutine != NULL)
                        {
                            NextRoutine(FdoExtension, Next, NextCtx);
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief DPC routine for card insertion/removal notification.
 *
 * Queues a work item to handle card initialization or teardown at
 * PASSIVE_LEVEL. Uses InterlockedCompareExchange to prevent double-queueing
 * from concurrent DPC invocations.
 *
 * @param[in]     Dpc              Pointer to the DPC object (unused).
 * @param[in,out] DeferredContext  Pointer to the SDPORT_FDO_EXTENSION.
 * @param[in]     SystemArgument1  System argument (unused).
 * @param[in]     SystemArgument2  System argument (unused).
 */
static
VOID
NTAPI
SdPortCardChangeDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PSDPORT_FDO_EXTENSION FdoExtension = (PSDPORT_FDO_EXTENSION)DeferredContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (FdoExtension == NULL)
    {
        return;
    }

    InterlockedExchange(&FdoExtension->CardChangePending, 1);

    if (FdoExtension->CardChangeWorkItem == NULL)
    {
        return;
    }

    if (InterlockedCompareExchange(&FdoExtension->CardChangeWorkItemQueued,
                                    1, 0) == 0)
    {
        Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock,
                                     FdoExtension->CardChangeWorkItem);
        if (!NT_SUCCESS(Status))
        {
            InterlockedExchange(&FdoExtension->CardChangeWorkItemQueued, 0);
            return;
        }

        IoQueueWorkItem(FdoExtension->CardChangeWorkItem,
                        SdPortCardChangeWorker,
                        DelayedWorkQueue,
                        FdoExtension);
    }
}

/**
 * @brief Work item routine for card insertion/removal at PASSIVE_LEVEL.
 *
 * Iterates over all slots, checks the card detect state via the miniport,
 * and initializes newly inserted cards or tears down removed cards by
 * clearing their CID, CSD, SCR, and RCA.
 *
 * @param[in] DeviceObject  Device object that owns the work item (unused).
 * @param[in] Parameter     Pointer to the SDPORT_FDO_EXTENSION.
 */
static
VOID
NTAPI
SdPortCardChangeWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Parameter)
{
    PSDPORT_FDO_EXTENSION FdoExtension = (PSDPORT_FDO_EXTENSION)Parameter;
    PSDPORT_SLOT_EXTENSION SlotExtension;
    BOOLEAN CardPresent;
    UCHAR Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    for (;;)
    {
        InterlockedExchange(&FdoExtension->CardChangePending, 0);

        for (Index = 0; Index < FdoExtension->SlotCount; Index++)
        {
            SlotExtension = &FdoExtension->Slots[Index];

            if (FdoExtension->MiniportInitData.GetCardDetectState != NULL)
            {
                CardPresent = FdoExtension->MiniportInitData.GetCardDetectState(
                                  FdoExtension->MiniportPrivateExtension);
            }
            else
            {
                CardPresent = SlotExtension->CardPresent;
            }

            if (CardPresent && !SlotExtension->CardPresent)
            {
                /* Card was just inserted */
                DPRINT1("SdPortCardChangeWorker: Card inserted in slot %u\n", Index);
                SlotExtension->CardPresent = TRUE;

                Status = SdPortInitializeCard(FdoExtension, SlotExtension);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("SdPortCardChangeWorker: Card init failed (0x%08lx)\n", Status);
                    SlotExtension->Initialized = FALSE;
                }
            }
            else if (!CardPresent && SlotExtension->CardPresent)
            {
                /* Card was removed */
                DPRINT1("SdPortCardChangeWorker: Card removed from slot %u\n", Index);
                SlotExtension->CardPresent = FALSE;
                SlotExtension->Initialized = FALSE;
                SlotExtension->CardType = SdCardTypeUnknown;
                SlotExtension->Rca = 0;
                RtlZeroMemory(&SlotExtension->Cid, sizeof(SD_CID));
                RtlZeroMemory(&SlotExtension->Csd, sizeof(SD_CSD));
                RtlZeroMemory(&SlotExtension->Scr, sizeof(SD_SCR));
            }
        }

        if (InterlockedCompareExchange(&FdoExtension->CardChangePending, 0, 0) != 0)
        {
            continue;
        }

        InterlockedExchange(&FdoExtension->CardChangeWorkItemQueued, 0);
        if (InterlockedCompareExchange(&FdoExtension->CardChangePending, 0, 0) == 0)
        {
            break;
        }

        if (InterlockedCompareExchange(&FdoExtension->CardChangeWorkItemQueued,
                                       1, 0) != 0)
        {
            break;
        }
    }

    IoReleaseRemoveLock(&FdoExtension->RemoveLock, FdoExtension->CardChangeWorkItem);
}

/**
 * @brief Dispatch routine for IRP_MJ_POWER requests.
 *
 * Handles IRP_MN_SET_POWER by calling the miniport SaveContext or
 * RestoreContext callbacks for device power state transitions.
 * All power IRPs are passed down the device stack.
 *
 * @param[in] DeviceObject  The SD port FDO device object.
 * @param[in] Irp           The power IRP to process.
 *
 * @return The NTSTATUS code returned by PoCallDriver.
 */
NTSTATUS
NTAPI
SdPortDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PSDPORT_FDO_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IrpStack;
    NTSTATUS Status;

    FdoExtension = (PSDPORT_FDO_EXTENSION)DeviceObject->DeviceExtension;
    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    DPRINT1("SdPortDispatchPower: MinorFunction=0x%02x\n",
           IrpStack->MinorFunction);

    /* Acquire remove lock to prevent removal during power IRP processing */
    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_SET_POWER:
        {
            if (IrpStack->Parameters.Power.Type == DevicePowerState)
            {
                DEVICE_POWER_STATE PowerState =
                    IrpStack->Parameters.Power.State.DeviceState;

                if (PowerState >= PowerDeviceD1)
                {
                    /* Save context before sleep */
                    if (FdoExtension->MiniportInitData.SaveContext != NULL)
                    {
                        FdoExtension->MiniportInitData.SaveContext(
                            FdoExtension->MiniportPrivateExtension);
                    }
                }

                Status = SdPortForwardPowerIrpSynchronous(FdoExtension, Irp);
                if (NT_SUCCESS(Status) &&
                    PowerState == PowerDeviceD0 &&
                    FdoExtension->MiniportInitData.RestoreContext != NULL)
                {
                    Status = FdoExtension->MiniportInitData.RestoreContext(
                        FdoExtension->MiniportPrivateExtension);
                }

                PoStartNextPowerIrp(Irp);
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
                return Status;
            }
            break;
        }
    }

    /* Pass the power IRP down the stack */
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(FdoExtension->LowerDevice, Irp);

    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    return Status;
}
