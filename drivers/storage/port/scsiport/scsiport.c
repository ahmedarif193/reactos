/*
 * PROJECT:     ReactOS Storage Stack / SCSIPORT storage port library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main and exported functions
 * COPYRIGHT:   Eric Kohl (eric.kohl@reactos.org)
 *              Aleksey Bragin (aleksey@reactos.org)
 *              2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include "scsiport.h"

#define NDEBUG
#include <debug.h>

#ifndef FREELDR_VERBOSE_SCSIPORT
#define FREELDR_VERBOSE_SCSIPORT 0
#endif

#if FREELDR_VERBOSE_SCSIPORT
#define SCSIPORT_TRACE(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, __VA_ARGS__)
#else
#define SCSIPORT_TRACE(...) ((void)0)
#endif

ULONG InternalDebugLevel = 0x03;
static LONG ScsiPortWarnedNoAccessRanges = 0;
static const CHAR ScsiPortDriverExtensionKey = 0;

/* Maximum number of consecutive buses returning SP_RETURN_NOT_FOUND before stopping scan */
#define MAX_CONSECUTIVE_NOT_FOUND 4

#undef ScsiPortMoveMemory

/* GLOBALS *******************************************************************/

static BOOLEAN
SpiGetPciConfigData(IN PDRIVER_OBJECT DriverObject,
                    IN PDEVICE_OBJECT DeviceObject,
                    IN struct _HW_INITIALIZATION_DATA *HwInitializationData,
                    IN OUT PPORT_CONFIGURATION_INFORMATION PortConfig,
                    IN PUNICODE_STRING RegistryPath,
                    IN ULONG BusNumber,
                    IN OUT PPCI_SLOT_NUMBER NextSlotNumber);

static
BOOLEAN
SpiIsPciBusInRange(
    _In_ ULONG BusNumber)
{
    ULONG MinBus, MaxBus;

    if (!HalQueryPciBusRange(&MinBus, &MaxBus))
    {
        /* HAL has not advertised a clamp yet; fall back to a legacy scan. */
        return TRUE;
    }

    if (BusNumber < MinBus || BusNumber > MaxBus)
    {
        DPRINT1("SCSIPORT: Skipping PCI probe on bus %lu; firmware range is [%lu-%lu].\n",
                BusNumber,
                MinBus,
                MaxBus);
        return FALSE;
    }

    return TRUE;
}

/* Clamp a PCI bus number to the HAL-advertised [Min..Max] range, if available. */
static
VOID
SpiClampPciBusToHalRange(
    _Inout_ PULONG BusNumber)
{
    ULONG MinBus, MaxBus;

    if (!HalQueryPciBusRange(&MinBus, &MaxBus))
    {
        /* No clamp available; keep legacy behavior. */
        return;
    }

    if (*BusNumber < MinBus || *BusNumber > MaxBus)
    {
        DPRINT1("SCSIPORT: Clamping PCI BusNumber %lu to firmware minimum %lu (max=%lu)\n",
                *BusNumber, MinBus, MaxBus);
        *BusNumber = MinBus;
    }
}

static NTSTATUS NTAPI
ScsiPortCreateClose(IN PDEVICE_OBJECT DeviceObject,
            IN PIRP Irp);

static VOID NTAPI
ScsiPortIoTimer(PDEVICE_OBJECT DeviceObject,
        PVOID Context);

VOID NTAPI
SpiMiniportTimerDpc(IN struct _KDPC *Dpc,
                    IN PVOID DeviceObject,
                    IN PVOID SystemArgument1,
                    IN PVOID SystemArgument2);

static NTSTATUS
SpiCreatePortConfig(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                    PHW_INITIALIZATION_DATA HwInitData,
                    PCONFIGURATION_INFO InternalConfigInfo,
                    PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                    BOOLEAN FirstCall);

NTSTATUS NTAPI
SpQueryDeviceCallout(IN PVOID  Context,
                     IN PUNICODE_STRING  PathName,
                     IN INTERFACE_TYPE  BusType,
                     IN ULONG  BusNumber,
                     IN PKEY_VALUE_FULL_INFORMATION  *BusInformation,
                     IN CONFIGURATION_TYPE  ControllerType,
                     IN ULONG  ControllerNumber,
                     IN PKEY_VALUE_FULL_INFORMATION  *ControllerInformation,
                     IN CONFIGURATION_TYPE  PeripheralType,
                     IN ULONG  PeripheralNumber,
                     IN PKEY_VALUE_FULL_INFORMATION  *PeripheralInformation);

VOID
SpiParseDeviceInfo(IN PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                   IN HANDLE Key,
                   IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                   IN PCONFIGURATION_INFO InternalConfigInfo,
                   IN PUCHAR Buffer);

static VOID
SpiInitializePortDeviceExtension(
    _Inout_ PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PHW_INITIALIZATION_DATA HwInitializationData);

static BOOLEAN
SppHandleInterrupt(
    _In_ PSCSI_PORT_INTERRUPT_CONTEXT Context,
    _In_ ULONG MessageId);

VOID
SpiResourceToConfig(IN PHW_INITIALIZATION_DATA HwInitializationData,
                    IN PCM_FULL_RESOURCE_DESCRIPTOR ResourceDescriptor,
                    IN PPORT_CONFIGURATION_INFORMATION PortConfig);

static PCM_RESOURCE_LIST
SpiConfigToResource(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                    PPORT_CONFIGURATION_INFORMATION PortConfig);

NTSTATUS
SpiEnsureAdapterObject(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                       PPORT_CONFIGURATION_INFORMATION ConfigInfo);

NTSTATUS
SpiAllocateCommonBuffer(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                        PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                        ULONG NonCachedSize);

static VOID
SpiInitializePortDeviceExtension(
    _Inout_ PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PHW_INITIALIZATION_DATA HwInitializationData)
{
    DeviceExtension->Common.DeviceObject = DeviceObject;
    DeviceExtension->Common.IsFDO = TRUE;

    DeviceExtension->InterruptObjects = NULL;
    DeviceExtension->InterruptContext = NULL;
    DeviceExtension->InterruptMessageInfo = NULL;
    DeviceExtension->InterruptMessageBased = FALSE;
    DeviceExtension->InterruptCount = 0;
    DeviceExtension->FirmwareSupportsPnp =
        (DeviceExtension->DriverExtension != NULL) ?
        DeviceExtension->DriverExtension->FirmwareSupportsPnp : FALSE;

    DeviceExtension->HwInitialize = HwInitializationData->HwInitialize;
    DeviceExtension->HwStartIo = HwInitializationData->HwStartIo;
    DeviceExtension->HwInterrupt = HwInitializationData->HwInterrupt;
    DeviceExtension->HwResetBus = HwInitializationData->HwResetBus;
    DeviceExtension->HwDmaStarted = HwInitializationData->HwDmaStarted;

    DeviceExtension->MiniPortExtensionSize = HwInitializationData->DeviceExtensionSize;
    DeviceExtension->LunExtensionSize =
        ALIGN_UP(HwInitializationData->SpecificLuExtensionSize, INT64);
    DeviceExtension->SrbExtensionSize =
        ALIGN_UP(HwInitializationData->SrbExtensionSize, INT64);

    DeviceExtension->MaxLunCount = SCSI_MAXIMUM_LOGICAL_UNITS;
    DeviceExtension->RequestsNumber = 16;

    KeInitializeSpinLock(&DeviceExtension->IrqLock);
    KeInitializeSpinLock(&DeviceExtension->SpinLock);

    IoInitializeDpcRequest(DeviceObject, ScsiPortDpcForIsr);

    DeviceExtension->TimerCount = -1;
    IoInitializeTimer(DeviceObject,
                      ScsiPortIoTimer,
                      DeviceExtension);

    KeInitializeTimer(&DeviceExtension->MiniportTimer);
    KeInitializeDpc(&DeviceExtension->MiniportTimerDpc,
                    SpiMiniportTimerDpc,
                    DeviceObject);
}

NTHALAPI ULONG NTAPI HalGetBusData(BUS_DATA_TYPE, ULONG, ULONG, PVOID, ULONG);
NTHALAPI ULONG NTAPI HalGetInterruptVector(INTERFACE_TYPE, ULONG, ULONG, ULONG, PKIRQL, PKAFFINITY);
NTHALAPI NTSTATUS NTAPI HalAssignSlotResources(PUNICODE_STRING, PUNICODE_STRING, PDRIVER_OBJECT, PDEVICE_OBJECT, INTERFACE_TYPE, ULONG, ULONG, PCM_RESOURCE_LIST *);

/* FUNCTIONS *****************************************************************/

/**********************************************************************
 * NAME                         EXPORTED
 *  DriverEntry
 *
 * DESCRIPTION
 *  This function initializes the driver.
 *
 * RUN LEVEL
 *  PASSIVE_LEVEL
 *
 * ARGUMENTS
 *  DriverObject
 *      System allocated Driver Object for this driver.
 *
 *  RegistryPath
 *      Name of registry driver service key.
 *
 * RETURN VALUE
 *  Status.
 */

NTSTATUS NTAPI
DriverEntry(IN PDRIVER_OBJECT DriverObject,
        IN PUNICODE_STRING RegistryPath)
{
    DPRINT1("SCSIPORT DriverEntry: DriverObject=%p RegistryPath=%wZ\n",
            DriverObject,
            RegistryPath);
    return STATUS_SUCCESS;
}

VOID
NTAPI
ScsiPortUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    // no-op
}

NTSTATUS
NTAPI
ScsiPortDispatchPnp(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    if (((PSCSI_PORT_COMMON_EXTENSION)DeviceObject->DeviceExtension)->IsFDO)
    {
        return FdoDispatchPnp(DeviceObject, Irp);
    }
    else
    {
        return PdoDispatchPnp(DeviceObject, Irp);
    }
}

NTSTATUS
NTAPI
ScsiPortAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PSCSI_PORT_DRIVER_EXTENSION DriverExtension;
    PHW_INITIALIZATION_DATA HwInitData;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT PortDeviceObject = NULL;
    NTSTATUS Status;
    ULONG DeviceExtensionSize;

    DriverExtension = IoGetDriverObjectExtension(DriverObject,
                                                 (PVOID)&ScsiPortDriverExtensionKey);

    if (DriverExtension == NULL || !DriverExtension->HwInitDataValid)
    {
        SCSIPORT_TRACE("[SCSIPORT] ScsiPortAddDevice: missing miniport initialization data\n");
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (DriverExtension->IsLegacyDriver)
    {
        SCSIPORT_TRACE("[SCSIPORT] ScsiPortAddDevice: legacy miniport detected, deferring to legacy path\n");
        return STATUS_SUCCESS;
    }

    HwInitData = &DriverExtension->HwInitializationData;
    DeviceExtensionSize = sizeof(SCSI_PORT_DEVICE_EXTENSION) + HwInitData->DeviceExtensionSize;

    Status = IoCreateDevice(DriverObject,
                            DeviceExtensionSize,
                            NULL,
                            FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &PortDeviceObject);

    if (!NT_SUCCESS(Status))
    {
        SCSIPORT_TRACE("[SCSIPORT] ScsiPortAddDevice: IoCreateDevice failed %lx\n",
                   Status);
        return Status;
    }

    PortDeviceObject->Flags |= DO_DIRECT_IO;
    PortDeviceObject->AlignmentRequirement = FILE_WORD_ALIGNMENT;

    DeviceExtension = PortDeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, DeviceExtensionSize);

    DeviceExtension->Length = DeviceExtensionSize;
    DeviceExtension->DriverExtension = DriverExtension;
    DeviceExtension->PhysicalDeviceObject = PhysicalDeviceObject;
    DeviceExtension->PortNumber = SP_UNINITIALIZED_VALUE;
    DeviceExtension->DeviceName.Length = 0;
    DeviceExtension->DeviceName.MaximumLength = 0;
    DeviceExtension->DeviceName.Buffer = NULL;
    DeviceExtension->InterfaceName.Length = 0;
    DeviceExtension->InterfaceName.MaximumLength = 0;
    DeviceExtension->InterfaceName.Buffer = NULL;

    SpiInitializePortDeviceExtension(DeviceExtension, PortDeviceObject, HwInitData);

    DeviceExtension->Common.LowerDevice =
        IoAttachDeviceToDeviceStack(PortDeviceObject, PhysicalDeviceObject);

    if (DeviceExtension->Common.LowerDevice == NULL)
    {
        SCSIPORT_TRACE("[SCSIPORT] ScsiPortAddDevice: IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(PortDeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    SCSIPORT_TRACE("[SCSIPORT] ScsiPortAddDevice: created FDO %p for PDO %p\n",
               PortDeviceObject,
               PhysicalDeviceObject);

    PortDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}


/**********************************************************************
 * NAME                         EXPORTED
 *  ScsiDebugPrint
 *
 * DESCRIPTION
 *  Prints debugging messages.
 *
 * RUN LEVEL
 *  PASSIVE_LEVEL
 *
 * ARGUMENTS
 *  DebugPrintLevel
 *      Debug level of the given message.
 *
 *  DebugMessage
 *      Pointer to printf()-compatible format string.
 *
 *  ...
        Additional output data (see printf()).
 *
 * RETURN VALUE
 *  None.
 *
 * @implemented
 */

VOID
ScsiDebugPrint(IN ULONG DebugPrintLevel,
           IN PCHAR DebugMessage,
           ...)
{
    char Buffer[256];
    va_list ap;

    if (DebugPrintLevel > InternalDebugLevel)
        return;

    va_start(ap, DebugMessage);
    vsprintf(Buffer, DebugMessage, ap);
    va_end(ap);

    DbgPrint(Buffer);
}

static VOID
SpiLogPortError(
    _In_ PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR PathId,
    _In_ UCHAR TargetId,
    _In_ UCHAR Lun,
    _In_ ULONG ErrorCode,
    _In_ ULONG UniqueId)
{
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        DPRINT1("[scsiport] (deferred) error log code=0x%lx P/T/L=%u/%u/%u Unique=0x%lx IRQL=%lu\n",
                ErrorCode,
                PathId,
                TargetId,
                Lun,
                UniqueId,
                KeGetCurrentIrql());
        return;
    }

    if (Srb != NULL)
    {
        PathId = Srb->PathId;
        TargetId = Srb->TargetId;
        Lun = Srb->Lun;
    }

    ScsiPortLogError(DeviceExtension->MiniPortDeviceExtension,
                     Srb,
                     PathId,
                     TargetId,
                     Lun,
                     ErrorCode,
                     UniqueId);
}

/* An internal helper function for ScsiPortCompleteRequest */
VOID
NTAPI
SpiCompleteRequest(IN PVOID HwDeviceExtension,
                   IN PSCSI_REQUEST_BLOCK_INFO SrbInfo,
                   IN UCHAR SrbStatus)
{
    PSCSI_REQUEST_BLOCK Srb;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;

    /* Get current SRB */
    Srb = SrbInfo->Srb;
    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    /* Return if there is no SRB or it is not active */
    if (!Srb || !(Srb->SrbFlags & SRB_FLAGS_IS_ACTIVE)) return;

    /* Set status */
    Srb->SrbStatus = SrbStatus;

    {
        UCHAR coreStatus = SRB_STATUS(SrbStatus);
        BOOLEAN successLike = (coreStatus == SRB_STATUS_SUCCESS) ||
                              (coreStatus == SRB_STATUS_DATA_OVERRUN);

        if (!successLike)
        {
            Srb->DataTransferLength = 0;
            SpiLogPortError(DeviceExtension,
                            Srb,
                            Srb->PathId,
                            Srb->TargetId,
                            Srb->Lun,
                            IO_ERR_CONTROLLER_ERROR,
                            SrbInfo->SequenceNumber ? SrbInfo->SequenceNumber : Srb->QueueTag);
        }
    }

    /* Notify */
    ScsiPortNotification(RequestComplete,
                         HwDeviceExtension,
                         Srb);
}

/*
 * @implemented
 */
VOID NTAPI
ScsiPortCompleteRequest(IN PVOID HwDeviceExtension,
                        IN UCHAR PathId,
                        IN UCHAR TargetId,
                        IN UCHAR Lun,
                        IN UCHAR SrbStatus)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PSCSI_PORT_LUN_EXTENSION LunExtension;
    PSCSI_REQUEST_BLOCK_INFO SrbInfo;
    PLIST_ENTRY ListEntry;

    DPRINT("ScsiPortCompleteRequest() called\n");

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    /* Go through all buses */
    for (UINT8 pathId = 0; pathId < DeviceExtension->NumberOfBuses; pathId++)
    {
        PSCSI_BUS_INFO bus = &DeviceExtension->Buses[pathId];

        /* Go through all logical units */
        for (PLIST_ENTRY lunEntry = bus->LunsListHead.Flink;
             lunEntry != &bus->LunsListHead;
             lunEntry = lunEntry->Flink)
        {
            LunExtension = CONTAINING_RECORD(lunEntry, SCSI_PORT_LUN_EXTENSION, LunEntry);

            /* Now match what caller asked with what we are at now */
            if ((PathId == SP_UNTAGGED || PathId == LunExtension->PathId) &&
                (TargetId == SP_UNTAGGED || TargetId == LunExtension->TargetId) &&
                (Lun == SP_UNTAGGED || Lun == LunExtension->Lun))
            {
                /* Yes, that's what caller asked for. Complete abort requests */
                if (LunExtension->CompletedAbortRequests)
                {
                    /* TODO: Save SrbStatus in this request */
                    DPRINT1("Completing abort request without setting SrbStatus!\n");

                    /* Issue a notification request */
                    ScsiPortNotification(RequestComplete,
                                         HwDeviceExtension,
                                         LunExtension->CompletedAbortRequests);
                }

                /* Complete the request using our helper */
                SpiCompleteRequest(HwDeviceExtension,
                                   &LunExtension->SrbInfo,
                                   SrbStatus);

                /* Go through the queue and complete everything there too */
                ListEntry = LunExtension->SrbInfo.Requests.Flink;
                while (ListEntry != &LunExtension->SrbInfo.Requests)
                {
                    /* Get the actual SRB info entry */
                    SrbInfo = CONTAINING_RECORD(ListEntry,
                                                SCSI_REQUEST_BLOCK_INFO,
                                                Requests);

                    /* Complete it */
                    SpiCompleteRequest(HwDeviceExtension,
                                       SrbInfo,
                                       SrbStatus);

                    /* Advance to the next request in queue */
                    ListEntry = SrbInfo->Requests.Flink;
                }
            }
        }
    }
}

/*
 * @implemented
 */
VOID NTAPI
ScsiPortFlushDma(IN PVOID HwDeviceExtension)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    PSCSI_REQUEST_BLOCK Srb;
    PSCSI_PORT_LUN_EXTENSION LunExtension;
    PSCSI_REQUEST_BLOCK_INFO SrbInfo;
    ULONG TransferLength;
    ULONG Index;
    BOOLEAN WriteToDevice;

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    if (DeviceExtension->AdapterObject == NULL ||
        DeviceExtension->Common.DeviceObject == NULL)
    {
        return;
    }

    Irp = DeviceExtension->Common.DeviceObject->CurrentIrp;
    if (Irp == NULL)
    {
        return;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    if (IoStack == NULL || IoStack->MajorFunction != IRP_MJ_SCSI)
    {
        return;
    }

    Srb = IoStack->Parameters.Scsi.Srb;
    if (Srb == NULL || Irp->MdlAddress == NULL)
    {
        return;
    }

    LunExtension = IoStack->DeviceObject->DeviceExtension;
    if (LunExtension == NULL || LunExtension->Common.IsFDO)
    {
        return;
    }

    SrbInfo = SpiGetSrbData(DeviceExtension, LunExtension, Srb->QueueTag);
    if (SrbInfo == NULL)
    {
        return;
    }

    if (!DeviceExtension->MapRegisters)
    {
        if (!(SrbInfo->DmaFlags & SCSI_PORT_DMA_FLAG_MAPPED) ||
            SrbInfo->DmaTransferLength == 0 ||
            DeviceExtension->MapRegisterBase == NULL)
        {
            return;
        }

        WriteToDevice = (SrbInfo->DmaFlags & SCSI_PORT_DMA_FLAG_WRITE_OUT) != 0;

        IoFlushAdapterBuffers(DeviceExtension->AdapterObject,
                              Irp->MdlAddress,
                              DeviceExtension->MapRegisterBase,
                              SrbInfo->DmaLogicalAddress,
                              SrbInfo->DmaTransferLength,
                              WriteToDevice);
        if (SrbInfo->DmaTransferLength != 0)
        {
            ULONG Remaining;
            ULONG Delta = SrbInfo->DmaTransferLength;

            if (SrbInfo->DmaBytesFlushed < Srb->DataTransferLength)
            {
                Remaining = Srb->DataTransferLength - SrbInfo->DmaBytesFlushed;
            }
            else
            {
                Remaining = 0;
            }

            if (Delta > Remaining)
            {
                Delta = Remaining;
            }

            SrbInfo->DmaBytesFlushed += Delta;
        }

        SrbInfo->DmaCurrentOffset += SrbInfo->DmaTransferLength;
        SrbInfo->DmaLogicalAddress = NULL;
        SrbInfo->DmaTransferLength = 0;
        SrbInfo->DmaFlags = 0;
        return;
    }

    if (SrbInfo->BaseOfMapRegister == NULL ||
        SrbInfo->ScatterGather == NULL ||
        SrbInfo->NumberOfMapRegisters == 0)
    {
        return;
    }

    TransferLength = 0;
    for (Index = 0;
         Index < SrbInfo->NumberOfMapRegisters && TransferLength < Srb->DataTransferLength;
         Index++)
    {
        TransferLength += SrbInfo->ScatterGather[Index].Length;
    }

    if (TransferLength == 0)
    {
        return;
    }

    WriteToDevice = (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) ? TRUE : FALSE;

    IoFlushAdapterBuffers(DeviceExtension->AdapterObject,
                          Irp->MdlAddress,
                          SrbInfo->BaseOfMapRegister,
                          Srb->DataBuffer,
                          TransferLength,
                          WriteToDevice);
    if (TransferLength != 0)
    {
        ULONG Remaining;

        if (SrbInfo->DmaBytesFlushed < Srb->DataTransferLength)
        {
            Remaining = Srb->DataTransferLength - SrbInfo->DmaBytesFlushed;
        }
        else
        {
            Remaining = 0;
        }

        if (Remaining > TransferLength)
        {
            Remaining = TransferLength;
        }

        SrbInfo->DmaBytesFlushed += Remaining;
    }

    SrbInfo->DmaCurrentOffset += TransferLength;
    SrbInfo->DmaFlags = 0;
    SrbInfo->DmaTransferLength = 0;
    SrbInfo->DmaLogicalAddress = NULL;
}


/*
 * @implemented
 */
VOID NTAPI
ScsiPortFreeDeviceBase(IN PVOID HwDeviceExtension,
               IN PVOID MappedAddress)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PMAPPED_ADDRESS NextMa, LastMa;

    //DPRINT("ScsiPortFreeDeviceBase() called\n");

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    /* Initialize our pointers */
    NextMa = DeviceExtension->MappedAddressList;
    LastMa = NextMa;

    while (NextMa)
    {
        if (NextMa->MappedAddress == MappedAddress)
        {
            /* Unmap it first */
            MmUnmapIoSpace(MappedAddress, NextMa->NumberOfBytes);

            /* Remove it from the list */
            if (NextMa == DeviceExtension->MappedAddressList)
            {
                /* Remove the first entry */
                DeviceExtension->MappedAddressList = NextMa->NextMappedAddress;
            }
            else
            {
                LastMa->NextMappedAddress = NextMa->NextMappedAddress;
            }

            /* Free the resources and quit */
            ExFreePool(NextMa);

            return;
        }
        else
        {
            LastMa = NextMa;
            NextMa = NextMa->NextMappedAddress;
        }
    }
}


/*
 * @implemented
 */
ULONG NTAPI
ScsiPortGetBusData(IN PVOID DeviceExtension,
           IN ULONG BusDataType,
           IN ULONG SystemIoBusNumber,
           IN ULONG SlotNumber,
           IN PVOID Buffer,
           IN ULONG Length)
{
    DPRINT("ScsiPortGetBusData()\n");

    if (BusDataType == PCIConfiguration)
    {
        ULONG MinBus, MaxBus;

        if (HalQueryPciBusRange(&MinBus, &MaxBus) &&
            (SystemIoBusNumber < MinBus || SystemIoBusNumber > MaxBus))
        {
            return 0;
        }
    }

    if (Length)
    {
        /* If Length is non-zero, just forward the call to
        HalGetBusData() function */
        return HalGetBusData(BusDataType,
                             SystemIoBusNumber,
                             SlotNumber,
                             Buffer,
                             Length);
    }

    /* We have a more complex case here */
    UNIMPLEMENTED;
    return 0;
}

/*
 * @implemented
 */
ULONG NTAPI
ScsiPortSetBusDataByOffset(IN PVOID DeviceExtension,
                           IN ULONG BusDataType,
                           IN ULONG SystemIoBusNumber,
                           IN ULONG SlotNumber,
                           IN PVOID Buffer,
                           IN ULONG Offset,
                           IN ULONG Length)
{
    DPRINT("ScsiPortSetBusDataByOffset()\n");

    if (BusDataType == PCIConfiguration)
    {
        ULONG MinBus, MaxBus;

        if (HalQueryPciBusRange(&MinBus, &MaxBus) &&
            (SystemIoBusNumber < MinBus || SystemIoBusNumber > MaxBus))
        {
            return 0;
        }
    }

    return HalSetBusDataByOffset(BusDataType,
                                 SystemIoBusNumber,
                                 SlotNumber,
                                 Buffer,
                                 Offset,
                                 Length);
}

/*
 * @implemented
 */
PVOID NTAPI
ScsiPortGetDeviceBase(IN PVOID HwDeviceExtension,
              IN INTERFACE_TYPE BusType,
              IN ULONG SystemIoBusNumber,
              IN SCSI_PHYSICAL_ADDRESS IoAddress,
              IN ULONG NumberOfBytes,
              IN BOOLEAN InIoSpace)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PHYSICAL_ADDRESS TranslatedAddress;
    PMAPPED_ADDRESS DeviceBase;
    ULONG AddressSpace;
    PVOID MappedAddress;

    //DPRINT ("ScsiPortGetDeviceBase() called\n");

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    AddressSpace = (ULONG)InIoSpace;
    if (HalTranslateBusAddress(BusType,
                               SystemIoBusNumber,
                               IoAddress,
                               &AddressSpace,
                               &TranslatedAddress) == FALSE)
    {
        return NULL;
    }

    /* i/o space */
    if (AddressSpace != 0)
        return((PVOID)(ULONG_PTR)TranslatedAddress.QuadPart);

    MappedAddress = MmMapIoSpace(TranslatedAddress,
                                 NumberOfBytes,
                                 FALSE);

    if (MappedAddress == NULL)
    {
        return NULL;
    }

    DeviceBase = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(MAPPED_ADDRESS),
                                       TAG_SCSIPORT);

    if (DeviceBase == NULL)
    {
        MmUnmapIoSpace(MappedAddress, NumberOfBytes);
        return NULL;
    }

    DeviceBase->MappedAddress = MappedAddress;
    DeviceBase->NumberOfBytes = NumberOfBytes;
    DeviceBase->IoAddress = IoAddress;
    DeviceBase->BusNumber = SystemIoBusNumber;

    /* Link it to the Device Extension list */
    DeviceBase->NextMappedAddress = DeviceExtension->MappedAddressList;
    DeviceExtension->MappedAddressList = DeviceBase;

    return MappedAddress;
}

/*
 * @unimplemented
 */
PVOID NTAPI
ScsiPortGetLogicalUnit(IN PVOID HwDeviceExtension,
               IN UCHAR PathId,
               IN UCHAR TargetId,
               IN UCHAR Lun)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PSCSI_PORT_LUN_EXTENSION LunExtension;

    DPRINT("ScsiPortGetLogicalUnit() called\n");

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    /* Check the extension size */
    if (!DeviceExtension->LunExtensionSize)
    {
        /* They didn't want one */
        return NULL;
    }

    LunExtension = GetLunByPath(DeviceExtension, PathId, TargetId, Lun);

    /* Check that the logical unit exists */
    if (!LunExtension)
    {
        /* Nope, return NULL */
        return NULL;
    }

    /* Return the logical unit miniport extension */
    return (LunExtension + 1);
}


/*
 * @implemented
 */
SCSI_PHYSICAL_ADDRESS NTAPI
ScsiPortGetPhysicalAddress(IN PVOID HwDeviceExtension,
                           IN PSCSI_REQUEST_BLOCK Srb OPTIONAL,
                           IN PVOID VirtualAddress,
                           OUT ULONG *Length)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    SCSI_PHYSICAL_ADDRESS PhysicalAddress;
    SIZE_T BufferLength = 0;
    ULONG_PTR Offset;
    PSCSI_SG_ADDRESS SGList;
    PSCSI_REQUEST_BLOCK_INFO SrbInfo;

    DPRINT("ScsiPortGetPhysicalAddress(%p %p %p %p)\n",
        HwDeviceExtension, Srb, VirtualAddress, Length);

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    if (Srb == NULL || Srb->SenseInfoBuffer == VirtualAddress)
    {
        /* Simply look it up in the allocated common buffer */
        Offset = (PUCHAR)VirtualAddress - (PUCHAR)DeviceExtension->SrbExtensionBuffer;

        BufferLength = DeviceExtension->CommonBufferLength - Offset;
        PhysicalAddress.QuadPart = DeviceExtension->PhysicalAddress.QuadPart + Offset;
    }
    else if (DeviceExtension->MapRegisters)
    {
        PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Srb->OriginalRequest);
        PSCSI_PORT_LUN_EXTENSION LunExtension = IoStack->DeviceObject->DeviceExtension;
        ASSERT(LunExtension && !LunExtension->Common.IsFDO);

        /* Scatter-gather list must be used */
        SrbInfo = SpiGetSrbData(DeviceExtension, LunExtension, Srb->QueueTag);

        SGList = SrbInfo->ScatterGather;

        /* Find needed item in the SG list */
        Offset = (PCHAR)VirtualAddress - (PCHAR)Srb->DataBuffer;
        while (Offset >= SGList->Length)
        {
            Offset -= SGList->Length;
            SGList++;
        }

        /* We're done, store length and physical address */
        BufferLength = SGList->Length - Offset;
        PhysicalAddress.QuadPart = SGList->PhysicalAddress.QuadPart + Offset;
    }
    else
    {
        /* Nothing */
        PhysicalAddress.QuadPart = (LONGLONG)SP_UNINITIALIZED_VALUE;
    }

    *Length = (ULONG)BufferLength;
    return PhysicalAddress;
}


/*
 * @implemented
 */
PSCSI_REQUEST_BLOCK NTAPI
ScsiPortGetSrb(IN PVOID HwDeviceExtension,
           IN UCHAR PathId,
           IN UCHAR TargetId,
           IN UCHAR Lun,
           IN LONG QueueTag)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PSCSI_PORT_LUN_EXTENSION LunExtension;
    PSCSI_REQUEST_BLOCK_INFO SrbInfo;
    UCHAR Tag;

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    LunExtension = GetLunByPath(DeviceExtension, PathId, TargetId, Lun);
    if (LunExtension == NULL)
    {
        return NULL;
    }

    Tag = (QueueTag < 0) ? SP_UNTAGGED : (UCHAR)QueueTag;

    SrbInfo = SpiGetSrbData(DeviceExtension, LunExtension, Tag);
    if (SrbInfo == NULL)
    {
        return NULL;
    }

    return SrbInfo->Srb;
}


NTSTATUS
SpiEnsureAdapterObject(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                       PPORT_CONFIGURATION_INFORMATION ConfigInfo)
{
    DEVICE_DESCRIPTION DeviceDescription;
    ULONG MapRegistersCount;
    BOOLEAN NeedDma;

    if (DeviceExtension->AdapterObject)
        return STATUS_SUCCESS;

    NeedDma = ConfigInfo->Master ||
        ConfigInfo->DmaChannel != SP_UNINITIALIZED_VALUE ||
        ConfigInfo->DmaChannel2 != SP_UNINITIALIZED_VALUE;

    if (!NeedDma)
        return STATUS_SUCCESS;

    RtlZeroMemory(&DeviceDescription, sizeof(DeviceDescription));

    DeviceDescription.Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDescription.Master = ConfigInfo->Master;
    DeviceDescription.ScatterGather = ConfigInfo->ScatterGather;
    DeviceDescription.DemandMode = ConfigInfo->DemandMode;
    DeviceDescription.Dma32BitAddresses = ConfigInfo->Dma32BitAddresses;
    DeviceDescription.Dma64BitAddresses = ConfigInfo->Dma64BitAddresses != 0;
    DeviceDescription.BusNumber = ConfigInfo->SystemIoBusNumber;
    DeviceDescription.DmaChannel = ConfigInfo->DmaChannel;
    DeviceDescription.InterfaceType = ConfigInfo->AdapterInterfaceType;
    DeviceDescription.DmaWidth = ConfigInfo->DmaWidth;
    DeviceDescription.DmaSpeed = ConfigInfo->DmaSpeed;
    DeviceDescription.MaximumLength = ConfigInfo->MaximumTransferLength;
    DeviceDescription.DmaPort = ConfigInfo->DmaPort;

    DeviceExtension->AdapterObject =
        HalGetAdapter(&DeviceDescription, &MapRegistersCount);

    if (DeviceExtension->AdapterObject == NULL)
    {
        DPRINT1("HalGetAdapter failed for bus %lu\n", ConfigInfo->SystemIoBusNumber);
        ConfigInfo->Master = FALSE;
        ConfigInfo->DmaChannel = SP_UNINITIALIZED_VALUE;
        ConfigInfo->DmaChannel2 = SP_UNINITIALIZED_VALUE;
        ConfigInfo->DmaPort = 0;
        ConfigInfo->DmaPort2 = 0;
        return STATUS_SUCCESS;
    }

    if (DeviceExtension->PortCapabilities.MaximumPhysicalPages == 0)
    {
        if (ConfigInfo->NumberOfPhysicalBreaks != 0 &&
            MapRegistersCount > ConfigInfo->NumberOfPhysicalBreaks)
        {
            DeviceExtension->PortCapabilities.MaximumPhysicalPages =
                ConfigInfo->NumberOfPhysicalBreaks;
        }
        else
        {
            DeviceExtension->PortCapabilities.MaximumPhysicalPages = MapRegistersCount;
        }
    }

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
PVOID NTAPI
ScsiPortGetUncachedExtension(IN PVOID HwDeviceExtension,
                 IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                 IN ULONG NumberOfBytes)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;

    DPRINT("ScsiPortGetUncachedExtension(%p %p %lu)\n",
        HwDeviceExtension, ConfigInfo, NumberOfBytes);

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    /* Check for allocated common DMA buffer */
    if (DeviceExtension->SrbExtensionBuffer != NULL)
    {
        DPRINT1("The HBA has already got a common DMA buffer!\n");
        return NULL;
    }

    Status = SpiEnsureAdapterObject(DeviceExtension, ConfigInfo);
    if (!NT_SUCCESS(Status))
        return NULL;

    /* Update auto request sense feature */
    DeviceExtension->SupportsAutoSense = ConfigInfo->AutoRequestSense;

    /* Update Srb extension size */
    if (DeviceExtension->SrbExtensionSize != ConfigInfo->SrbExtensionSize)
        DeviceExtension->SrbExtensionSize = ConfigInfo->SrbExtensionSize;

    /* Update Srb extension alloc flag */
    if (ConfigInfo->AutoRequestSense || DeviceExtension->SrbExtensionSize)
        DeviceExtension->NeedSrbExtensionAlloc = TRUE;

    /* Allocate a common DMA buffer */
    Status = SpiAllocateCommonBuffer(DeviceExtension, ConfigInfo, NumberOfBytes);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SpiAllocateCommonBuffer() failed with Status = 0x%08X!\n", Status);
        return NULL;
    }

    return DeviceExtension->NonCachedExtension;
}

NTSTATUS
SpiAllocateCommonBuffer(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                        PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                        ULONG NonCachedSize)
{
    PVOID *SrbExtension, CommonBuffer;
    ULONG CommonBufferLength, BufSize;
    PHYSICAL_ADDRESS physicalAddress;

    /* If size is 0, set it to 16 */
    if (!DeviceExtension->SrbExtensionSize)
        DeviceExtension->SrbExtensionSize = 16;

    /* Calculate size */
    BufSize = DeviceExtension->SrbExtensionSize;

    /* Add autosense data size if needed */
    if (DeviceExtension->SupportsAutoSense)
        BufSize += sizeof(SENSE_DATA);


    /* Round it */
    BufSize = (BufSize + sizeof(LONGLONG) - 1) & ~(sizeof(LONGLONG) - 1);

    /* Sum up into the total common buffer length, and round it to page size */
    CommonBufferLength =
        ROUND_TO_PAGES(NonCachedSize + BufSize * DeviceExtension->RequestsNumber);

    DeviceExtension->CommonBufferFromMm = FALSE;
    DeviceExtension->CommonBufferCacheType = MmNonCached;

    /* Allocate it */
    if (!DeviceExtension->AdapterObject)
    {
        /* From nonpaged pool if there is no DMA */
        CommonBuffer = ExAllocatePoolWithTag(NonPagedPool, CommonBufferLength, TAG_SCSIPORT);
    }
    else
    {
        /* Perform a full request since we have a DMA adapter*/
        CommonBuffer = HalAllocateCommonBuffer(DeviceExtension->AdapterObject,
            CommonBufferLength,
            &DeviceExtension->PhysicalAddress,
            FALSE );

        if (CommonBuffer &&
            ConfigInfo &&
            ConfigInfo->Dma32BitAddresses &&
            !ConfigInfo->Dma64BitAddresses &&
            DeviceExtension->PhysicalAddress.HighPart != 0)
        {
            PHYSICAL_ADDRESS lowestAddress;
            PHYSICAL_ADDRESS highestAddress;
            PHYSICAL_ADDRESS alignment;

            DPRINT1("SpiAllocateCommonBuffer: DMA32-only adapter got PA=0x%I64x, retrying <=4GB\n",
                    DeviceExtension->PhysicalAddress.QuadPart);

            physicalAddress = DeviceExtension->PhysicalAddress;
            HalFreeCommonBuffer(DeviceExtension->AdapterObject,
                                CommonBufferLength,
                                physicalAddress,
                                CommonBuffer,
                                FALSE);

            CommonBuffer = NULL;
            DeviceExtension->PhysicalAddress.QuadPart = 0;

            lowestAddress.QuadPart = 0;
            highestAddress.QuadPart = 0x00000000FFFFFFFFULL;
            alignment.QuadPart = 0;

            CommonBuffer = MmAllocateContiguousMemorySpecifyCache(CommonBufferLength,
                                                                  lowestAddress,
                                                                  highestAddress,
                                                                  alignment,
                                                                  MmNonCached);
            if (CommonBuffer)
            {
                DeviceExtension->PhysicalAddress = MmGetPhysicalAddress(CommonBuffer);
                DeviceExtension->CommonBufferFromMm = TRUE;
                DeviceExtension->CommonBufferCacheType = MmNonCached;
            }
        }
    }

    /* Fail in case of error */
    if (!CommonBuffer)
    {
        SpiLogPortError(DeviceExtension,
                        NULL,
                        SP_UNTAGGED,
                        SP_UNTAGGED,
                        SP_UNTAGGED,
                        IO_ERR_INSUFFICIENT_RESOURCES,
                        'CMON');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Zero it */
    RtlZeroMemory(CommonBuffer, CommonBufferLength);

    /* Store its size in Device Extension */
    DeviceExtension->CommonBufferLength = CommonBufferLength;

    /* SrbExtension buffer is located at the beginning of the buffer */
    DeviceExtension->SrbExtensionBuffer = CommonBuffer;

    /* Non-cached extension buffer is located at the end of
       the common buffer */
    if (NonCachedSize)
    {
        CommonBufferLength -=  NonCachedSize;
        DeviceExtension->NonCachedExtension = (PUCHAR)CommonBuffer + CommonBufferLength;
    }
    else
    {
        DeviceExtension->NonCachedExtension = NULL;
    }

    if (DeviceExtension->NeedSrbExtensionAlloc)
    {
        /* Look up how many SRB data structures we need */
        DeviceExtension->SrbDataCount = CommonBufferLength / BufSize;

        /* Initialize the free SRB extensions list */
        SrbExtension = (PVOID *)CommonBuffer;
        DeviceExtension->FreeSrbExtensions = SrbExtension;

        /* Fill the remaining pointers (if we have more than 1 SRB) */
        while (CommonBufferLength >= 2 * BufSize)
        {
            *SrbExtension = (PVOID*)((PCHAR)SrbExtension + BufSize);
            SrbExtension = *SrbExtension;

            CommonBufferLength -= BufSize;
        }
    }

    return STATUS_SUCCESS;
}



/*
 * @implemented
 */
PVOID NTAPI
ScsiPortGetVirtualAddress(IN PVOID HwDeviceExtension,
                          IN SCSI_PHYSICAL_ADDRESS PhysicalAddress)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    ULONG Offset;

    DPRINT("ScsiPortGetVirtualAddress(%p %I64x)\n",
        HwDeviceExtension, PhysicalAddress.QuadPart);

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    if (DeviceExtension->PhysicalAddress.QuadPart > PhysicalAddress.QuadPart)
        return NULL;

    Offset = (ULONG)(PhysicalAddress.QuadPart - DeviceExtension->PhysicalAddress.QuadPart);

    if (Offset >= DeviceExtension->CommonBufferLength)
        return NULL;

    return (PVOID)((ULONG_PTR)DeviceExtension->SrbExtensionBuffer + Offset);
}

/**********************************************************************
 * NAME                         EXPORTED
 *  ScsiPortInitialize
 *
 * DESCRIPTION
 *  Initializes SCSI port driver specific data.
 *
 * RUN LEVEL
 *  PASSIVE_LEVEL
 *
 * ARGUMENTS
 *  Argument1
 *      Pointer to the miniport driver's driver object.
 *
 *  Argument2
 *      Pointer to the miniport driver's registry path.
 *
 *  HwInitializationData
 *      Pointer to port driver specific configuration data.
 *
 *  HwContext
        Miniport driver specific context.
 *
 * RETURN VALUE
 *  Status.
 *
 * @implemented
 */

ULONG NTAPI
ScsiPortInitialize(
    IN PVOID Argument1,
    IN PVOID Argument2,
    IN struct _HW_INITIALIZATION_DATA *HwInitializationData,
    IN PVOID HwContext)
{
    PDRIVER_OBJECT DriverObject = (PDRIVER_OBJECT)Argument1;
    PUNICODE_STRING RegistryPath = (PUNICODE_STRING)Argument2;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension = NULL;
    PCONFIGURATION_INFORMATION SystemConfig;
    PPORT_CONFIGURATION_INFORMATION PortConfig = NULL;
    CONFIGURATION_INFO ConfigInfo;
    ULONG DeviceExtensionSize;
    SIZE_T PortConfigSize;
    BOOLEAN Again;
    BOOLEAN DeviceFound = FALSE;
    BOOLEAN FirstConfigCall = TRUE;
    ULONG Result;
    NTSTATUS Status;
    PCI_SLOT_NUMBER SlotNumber;

    PDEVICE_OBJECT PortDeviceObject;
    UNICODE_STRING DeviceName;
    PIO_SCSI_CAPABILITIES PortCapabilities;

    PCM_RESOURCE_LIST ResourceList = NULL;

    SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize enter: AdapterInterfaceType=%lu\n",
                   HwInitializationData->AdapterInterfaceType);

    /* Check params for validity */
    if ((HwInitializationData->HwInitialize == NULL) ||
        (HwInitializationData->HwStartIo == NULL) ||
        (HwInitializationData->HwInterrupt == NULL) ||
        (HwInitializationData->HwFindAdapter == NULL) ||
        (HwInitializationData->HwResetBus == NULL))
    {
        return STATUS_REVISION_MISMATCH;
    }

    PSCSI_PORT_DRIVER_EXTENSION driverExtension;

    // ScsiPortInitialize may be called multiple times by the same driver
    driverExtension = IoGetDriverObjectExtension(DriverObject,
                                                 (PVOID)&ScsiPortDriverExtensionKey);

    if (!driverExtension)
    {
        Status = IoAllocateDriverObjectExtension(DriverObject,
                                                 (PVOID)&ScsiPortDriverExtensionKey,
                                                 sizeof(SCSI_PORT_DRIVER_EXTENSION),
                                                 (PVOID *)&driverExtension);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to allocate the driver extension! Status 0x%x\n", Status);
            return Status;
        }

        RtlZeroMemory(driverExtension, sizeof(SCSI_PORT_DRIVER_EXTENSION));
        driverExtension->NextPortNumber = IoGetConfigurationInformation()->ScsiPortCount;
    }

    // set up the driver extension
    if (driverExtension->RegistryPath.Buffer == NULL ||
        driverExtension->RegistryPath.MaximumLength < RegistryPath->MaximumLength)
    {
        if (driverExtension->RegistryPath.Buffer != NULL)
        {
            ExFreePoolWithTag(driverExtension->RegistryPath.Buffer, TAG_SCSIPORT);
            driverExtension->RegistryPath.Buffer = NULL;
            driverExtension->RegistryPath.Length = 0;
        }

        driverExtension->RegistryPath.Buffer =
            ExAllocatePoolWithTag(PagedPool, RegistryPath->MaximumLength, TAG_SCSIPORT);
        driverExtension->RegistryPath.MaximumLength = RegistryPath->MaximumLength;
    }

    if (driverExtension->RegistryPath.Buffer != NULL)
    {
        RtlCopyUnicodeString(&driverExtension->RegistryPath, RegistryPath);
    }

    driverExtension->DriverObject = DriverObject;

    driverExtension->HwInitializationData = *HwInitializationData;
    driverExtension->HwContext = HwContext;
    driverExtension->HwInitDataValid = TRUE;

    /* Set handlers */
    DriverObject->DriverUnload = ScsiPortUnload;
    DriverObject->DriverStartIo = ScsiPortStartIo;
    DriverObject->DriverExtension->AddDevice = ScsiPortAddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = ScsiPortCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = ScsiPortCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ScsiPortDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SCSI] = ScsiPortDispatchScsi;
    DriverObject->MajorFunction[IRP_MJ_PNP] = ScsiPortDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = ScsiPortDispatchPower;

    /* Obtain configuration information */
    SystemConfig = IoGetConfigurationInformation();

    /* Zero the internal configuration info structure */
    RtlZeroMemory(&ConfigInfo, sizeof(CONFIGURATION_INFO));

    /* Zero starting slot number */
    SlotNumber.u.AsULONG = 0;

    PortDeviceObject = NULL;

    /* Allocate space for access ranges */
    if (HwInitializationData->NumberOfAccessRanges)
    {
        ConfigInfo.AccessRanges =
            ExAllocatePoolWithTag(PagedPool,
            HwInitializationData->NumberOfAccessRanges * sizeof(ACCESS_RANGE), TAG_SCSIPORT);

        /* Fail if failed */
        if (ConfigInfo.AccessRanges == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Open registry keys and fill the driverExtension */
    SpiInitOpenKeys(&ConfigInfo, driverExtension);

    SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: IsLegacyDriver=%u\n",
                   driverExtension->IsLegacyDriver);

    if (!driverExtension->IsLegacyDriver)
    {
        SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: treating driver as PnP-only, skipping legacy detection\n");
        Status = STATUS_SUCCESS;
        goto Cleanup;
    }

#if !SCSIPORT_LEGACY_DETECTION
    Status = STATUS_SUCCESS;
    goto Cleanup;
#endif

    /* Last adapter number = not known */
    ConfigInfo.LastAdapterNumber = SP_UNINITIALIZED_VALUE;

    /* Counter to prevent infinite loops on non-existent PCI buses */
    ULONG ConsecutiveNotFoundCount = 0;

    /* Calculate sizes of DeviceExtension and PortConfig */
    DeviceExtensionSize = sizeof(SCSI_PORT_DEVICE_EXTENSION) +
        HwInitializationData->DeviceExtensionSize;

    DPRINT("AdapterInterfaceType: %lu\n",
           HwInitializationData->AdapterInterfaceType);

    while (TRUE)
    {
        WCHAR NameBuffer[27];
        /* Create a unicode device name */
        swprintf(NameBuffer,
                 L"\\Device\\ScsiPort%lu",
                 SystemConfig->ScsiPortCount);
        if (!RtlCreateUnicodeString(&DeviceName, NameBuffer))
        {
            DPRINT1("Failed to allocate memory for device name!\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            PortDeviceObject = NULL;
            SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: RtlCreateUnicodeString failed Port=%lu\n",
                       SystemConfig->ScsiPortCount);
            break;
        }

        DPRINT("Creating device: %wZ\n", &DeviceName);

        /* Create the port device */
        Status = IoCreateDevice(DriverObject,
                                DeviceExtensionSize,
                                &DeviceName,
                                FILE_DEVICE_CONTROLLER,
                                FILE_DEVICE_SECURE_OPEN,
                                FALSE,
                                &PortDeviceObject);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IoCreateDevice call failed! (Status 0x%lX)\n", Status);
            PortDeviceObject = NULL;
            SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: IoCreateDevice failed Status=0x%08lx\n",
                       Status);
            break;
        }

        /* Set the buffering strategy here... */
        PortDeviceObject->Flags |= DO_DIRECT_IO;
        PortDeviceObject->AlignmentRequirement = FILE_WORD_ALIGNMENT; /* FIXME: Is this really needed? */

        /* Fill Device Extension */
        DeviceExtension = PortDeviceObject->DeviceExtension;
        RtlZeroMemory(DeviceExtension, DeviceExtensionSize);
        DeviceExtension->Length = DeviceExtensionSize;
        DeviceExtension->DriverExtension = driverExtension;
        DeviceExtension->PhysicalDeviceObject = NULL;
        DeviceExtension->PortNumber = SystemConfig->ScsiPortCount;
        DeviceExtension->DeviceName = DeviceName;

        SpiInitializePortDeviceExtension(DeviceExtension,
                                         PortDeviceObject,
                                         HwInitializationData);

CreatePortConfig:

        SCSIPORT_TRACE("[SCSIPORT] CreatePortConfig: BusNumber=%lu InterfaceType=%lu NumberOfAccessRanges=%lu FirstCall=%u\n",
                       ConfigInfo.BusNumber,
                       HwInitializationData->AdapterInterfaceType,
                       (ULONG)HwInitializationData->NumberOfAccessRanges,
                       FirstConfigCall);

        /* Allocate and initialize port configuration info */
        {
            SIZE_T ConfigHeaderSize;
            SIZE_T RangesBytes;
            SIZE_T TailBytes;

            ConfigHeaderSize = ALIGN_UP(sizeof(PORT_CONFIGURATION_INFORMATION), sizeof(LONGLONG));
            RangesBytes = (SIZE_T)HwInitializationData->NumberOfAccessRanges * sizeof(ACCESS_RANGE);

            TailBytes = (RangesBytes != 0)
                            ? ALIGN_UP(RangesBytes, sizeof(LONGLONG))
                            : 0;

            PortConfigSize = ConfigHeaderSize + TailBytes;
        }

        DeviceExtension->PortConfig =
            ExAllocatePoolWithTag(NonPagedPool, PortConfigSize, TAG_SCSIPORT);

        /* Fail if failed */
        if (DeviceExtension->PortConfig == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: ExAllocatePoolWithTag failed Size=%zu\n",
                       PortConfigSize);
            break;
        }

        RtlZeroMemory(DeviceExtension->PortConfig, PortConfigSize);

        Status = SpiCreatePortConfig(DeviceExtension,
                                     HwInitializationData,
                                     &ConfigInfo,
                                     DeviceExtension->PortConfig,
                                     FirstConfigCall);

        if (!NT_SUCCESS(Status))
        {
            SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: SpiCreatePortConfig failed Status=0x%08lx Bus=%lu Interface=%lu\n",
                       Status,
                       ConfigInfo.BusNumber,
                       HwInitializationData->AdapterInterfaceType);
            break;
        }

        SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: PortConfig ready Bus=%lu Interface=%lu Ranges=%lu FirstCall=%u\n",
                       ConfigInfo.BusNumber,
                       DeviceExtension->PortConfig->AdapterInterfaceType,
                       (ULONG)HwInitializationData->NumberOfAccessRanges,
                       FirstConfigCall);

        PortConfig = DeviceExtension->PortConfig;

        SCSIPORT_TRACE("[SCSIPORT] SpiCreatePortConfig: BusNumber=%lu InterfaceType=%lu NumRanges=%lu\n",
                       PortConfig->SystemIoBusNumber,
                       PortConfig->AdapterInterfaceType,
                       (ULONG)HwInitializationData->NumberOfAccessRanges);

        /* Copy extension sizes into the PortConfig */
        PortConfig->SpecificLuExtensionSize = DeviceExtension->LunExtensionSize;
        PortConfig->SrbExtensionSize = DeviceExtension->SrbExtensionSize;

        /* Initialize Access ranges */
        if (HwInitializationData->NumberOfAccessRanges != 0)
        {
            PACCESS_RANGE Ranges;
            SIZE_T RangesBytes;
            PUCHAR Base;

            Base = (PUCHAR)PortConfig;
            Base += ALIGN_UP(sizeof(PORT_CONFIGURATION_INFORMATION), sizeof(LONGLONG));

            RangesBytes = (SIZE_T)HwInitializationData->NumberOfAccessRanges * sizeof(ACCESS_RANGE);
            Ranges = (PACCESS_RANGE)Base;
            *((PVOID *)&PortConfig->AccessRanges) = Ranges;

            /* Copy the data */
            if (ConfigInfo.AccessRanges != NULL && RangesBytes != 0)
            {
                RtlCopyMemory(Ranges, ConfigInfo.AccessRanges, RangesBytes);
            }

            PACCESS_RANGE accessArray = *PortConfig->AccessRanges;
            for (ULONG rangeIndex = 0;
                 rangeIndex < HwInitializationData->NumberOfAccessRanges;
                 rangeIndex++)
            {
                PACCESS_RANGE range = &accessArray[rangeIndex];
                DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                           DPFLTR_INFO_LEVEL,
                           "[scsiport] AccessRange[%lu]: start=0x%I64x len=0x%lx inMem=%lu\n",
                           rangeIndex,
                           range->RangeStart.QuadPart,
                           range->RangeLength,
                           range->RangeInMemory);
            }

            BOOLEAN HasValidRange = FALSE;
            for (ULONG rangeIndex = 0;
                 rangeIndex < HwInitializationData->NumberOfAccessRanges;
                 rangeIndex++)
            {
                PACCESS_RANGE range = &accessArray[rangeIndex];
                if (range->RangeLength != 0)
                {
                    HasValidRange = TRUE;
                    break;
                }
            }

            if (!HasValidRange && HwInitializationData->AdapterInterfaceType == PCIBus)
            {
                DPRINT1("No access ranges discovered from registry, trying PCI fallback\n");
                /* Clamp bogus bus numbers to the firmware-advertised range */
                SpiClampPciBusToHalRange(&PortConfig->SystemIoBusNumber);
                ConfigInfo.BusNumber = PortConfig->SystemIoBusNumber;

                if (!SpiGetPciConfigData(DriverObject,
                                         PortDeviceObject,
                                         HwInitializationData,
                                         PortConfig,
                                         RegistryPath,
                                         PortConfig->SystemIoBusNumber,
                                         &SlotNumber))
                {
                    DPRINT1("SpiGetPciConfigData did not find matching device\n");
                }
                else
                {
                    accessArray = *PortConfig->AccessRanges;
                    for (ULONG rangeIndex = 0;
                         rangeIndex < HwInitializationData->NumberOfAccessRanges;
                         rangeIndex++)
                    {
                        PACCESS_RANGE range = &accessArray[rangeIndex];
                        if (range->RangeLength == 0)
                            continue;

                    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                               DPFLTR_INFO_LEVEL,
                               "[scsiport] (PCI) AccessRange[%lu]: start=0x%I64x len=0x%lx inMem=%lu\n",
                               rangeIndex,
                               range->RangeStart.QuadPart,
                               range->RangeLength,
                               range->RangeInMemory);
                        HasValidRange = TRUE;
                    }
                }
            }

            if (!HasValidRange)
            {
                if (InterlockedCompareExchange(&ScsiPortWarnedNoAccessRanges, 1, 0) == 0)
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                               DPFLTR_WARNING_LEVEL,
                               "[scsiport] Adapter %p has no valid access ranges; continuing\n",
                               DeviceExtension);
                }
            }
        }

        /* Search for matching PCI device */
        if ((HwInitializationData->AdapterInterfaceType == PCIBus) &&
            (HwInitializationData->VendorIdLength > 0) &&
            (HwInitializationData->VendorId != NULL) &&
            (HwInitializationData->DeviceIdLength > 0) &&
            (HwInitializationData->DeviceId != NULL))
        {
            PortConfig->BusInterruptLevel = 0;

            /* Get PCI device data */
            DPRINT(
                "VendorId '%.*s'  DeviceId '%.*s'\n", HwInitializationData->VendorIdLength,
                HwInitializationData->VendorId, HwInitializationData->DeviceIdLength,
                HwInitializationData->DeviceId);

            /* Ensure we don't walk invalid bus numbers forever */
            SpiClampPciBusToHalRange(&ConfigInfo.BusNumber);

            if (!SpiGetPciConfigData(
                    DriverObject, PortDeviceObject, HwInitializationData, PortConfig, RegistryPath,
                    ConfigInfo.BusNumber, &SlotNumber))
            {
                /* Continue to the next bus, nothing here */
                ConfigInfo.BusNumber++;
                DeviceExtension->PortConfig = NULL;
                ExFreePool(PortConfig);
                Again = FALSE;
                goto CreatePortConfig;
            }

            if (!PortConfig->BusInterruptLevel)
            {
                /* Bypass this slot, because no interrupt was assigned */
                DeviceExtension->PortConfig = NULL;
                ExFreePool(PortConfig);
                goto CreatePortConfig;
            }
        }
        else
        {
            DPRINT("Non-pci bus\n");
        }

        /* Note: HwFindAdapter is called once for each bus */
        Again = FALSE;
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                   "[scsiport] Calling HwFindAdapter=%p Bus=%lu CfgBus=%lu If=%u Ranges=%lu Legacy=%u\n",
                   HwInitializationData->HwFindAdapter,
                   PortConfig->SystemIoBusNumber,
                   ConfigInfo.BusNumber,
                   HwInitializationData->AdapterInterfaceType,
                   (ULONG)HwInitializationData->NumberOfAccessRanges,
                   driverExtension->IsLegacyDriver);
        Result = (HwInitializationData->HwFindAdapter)(
            &DeviceExtension->MiniPortDeviceExtension, HwContext, 0, /* BusInformation */
            ConfigInfo.Parameter,                                    /* ArgumentString */
            PortConfig, &Again);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                   "[scsiport] HwFindAdapter result=%lu Again=%lu Bus=%lu If=%u\n",
                   Result,
                   Again,
                   PortConfig->SystemIoBusNumber,
                   HwInitializationData->AdapterInterfaceType);

        /* Free MapRegisterBase, it's not needed anymore */
        if (DeviceExtension->MapRegisterBase != NULL)
        {
            ExFreePool(DeviceExtension->MapRegisterBase);
            DeviceExtension->MapRegisterBase = NULL;
        }

        /* If result is nothing good... */
        if (Result != SP_RETURN_FOUND)
        {
            SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: HwFindAdapter returned %lu (Bus=%lu)\n",
                       Result,
                       PortConfig->SystemIoBusNumber);

            if (Result == SP_RETURN_NOT_FOUND)
            {
                SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: no adapter on Bus=%lu, continuing scan\n",
                           ConfigInfo.BusNumber);

                DeviceExtension->PortConfig = NULL;
                ExFreePool(PortConfig);

                /* For PCI we probe successive bus numbers, otherwise stop here */
                if (HwInitializationData->AdapterInterfaceType == PCIBus)
                {
                    /* Increment consecutive not-found counter */
                    ConsecutiveNotFoundCount++;

                    DbgPrintEx(DPFLTR_DEFAULT_ID,
                               DPFLTR_ERROR_LEVEL,
                               "[SCSIPORT] ScsiPortInitialize: ConsecutiveNotFoundCount=%lu (max=%lu)\n",
                               ConsecutiveNotFoundCount,
                               (ULONG)MAX_CONSECUTIVE_NOT_FOUND);

                    /* Break if too many consecutive not-found results */
                    if (ConsecutiveNotFoundCount >= MAX_CONSECUTIVE_NOT_FOUND)
                    {
                        SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: stopping bus scan after %lu consecutive not-found buses\n",
                                   ConsecutiveNotFoundCount);
                        break;
                    }

                    ConfigInfo.BusNumber++;
                    Again = FALSE;
                    goto CreatePortConfig;
                }

                break;
            }

            /* Otherwise, break */
            Status = STATUS_INTERNAL_ERROR;
            break;
        }

        SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: adapter found (Bus=%lu Vector=%lu)\n",
                   PortConfig->SystemIoBusNumber,
                   PortConfig->BusInterruptVector);

        /* Reset consecutive not-found counter on successful adapter detection */
        ConsecutiveNotFoundCount = 0;

        /* If the SRB extension size was updated */
        if (!DeviceExtension->NonCachedExtension &&
            (PortConfig->SrbExtensionSize != DeviceExtension->SrbExtensionSize))
        {
            /* Set it (rounding to LONGLONG again) */
            DeviceExtension->SrbExtensionSize = ALIGN_UP(PortConfig->SrbExtensionSize, INT64);
        }

        /* The same with LUN extension size */
        if (PortConfig->SpecificLuExtensionSize != DeviceExtension->LunExtensionSize)
            DeviceExtension->LunExtensionSize = PortConfig->SpecificLuExtensionSize;

        /* Construct a resource list */
        ResourceList = SpiConfigToResource(DeviceExtension, PortConfig);

#if SCSIPORT_LEGACY_DETECTION
        PDEVICE_OBJECT LowerPDO = NULL;

        Status = IoReportDetectedDevice(DriverObject,
                                        HwInitializationData->AdapterInterfaceType,
                                        ConfigInfo.BusNumber,
                                        PortConfig->SlotNumber,
                                        ResourceList,
                                        NULL,
                                        TRUE,
                                        &LowerPDO);

        if (!NT_SUCCESS(Status))
        {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_ERROR_LEVEL,
                       "[scsiport] IoReportDetectedDevice failed: Status=0x%08lx Bus=%lu Slot=%lu\n",
                       Status,
                       ConfigInfo.BusNumber,
                       PortConfig->SlotNumber);
            break;
        }

        DeviceExtension->Common.LowerDevice = IoAttachDeviceToDeviceStack(PortDeviceObject, LowerPDO);

        ASSERT(DeviceExtension->Common.LowerDevice);

        PortDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

        if (ResourceList)
        {
            ExFreePoolWithTag(ResourceList, TAG_SCSIPORT);
        }
#else
        if (ResourceList)
        {
            ExFreePoolWithTag(ResourceList, TAG_SCSIPORT);
            ResourceList = NULL;
        }

        PortDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
        DeviceExtension->Common.LowerDevice = NULL;
#endif

        /* Copy all stuff which we ever need from PortConfig to the DeviceExtension */
        if (PortConfig->MaximumNumberOfTargets > SCSI_MAXIMUM_TARGETS_PER_BUS)
            DeviceExtension->MaxTargedIds = SCSI_MAXIMUM_TARGETS_PER_BUS;
        else
            DeviceExtension->MaxTargedIds = PortConfig->MaximumNumberOfTargets;

        DeviceExtension->NumberOfBuses = PortConfig->NumberOfBuses;
        DeviceExtension->CachesData = PortConfig->CachesData;
        DeviceExtension->ReceiveEvent = PortConfig->ReceiveEvent;
        DeviceExtension->SupportsTaggedQueuing = PortConfig->TaggedQueuing;
        DeviceExtension->MultipleReqsPerLun = PortConfig->MultipleRequestPerLu;

        /* Initialize bus scanning information */
        size_t BusConfigSize = DeviceExtension->NumberOfBuses * sizeof(*DeviceExtension->Buses);
        DeviceExtension->Buses = ExAllocatePoolZero(NonPagedPool, BusConfigSize, TAG_SCSIPORT);
        if (!DeviceExtension->Buses)
        {
            DPRINT1("Out of resources!\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: SpiEnsureAdapterObject failed Status=0x%08lx\n",
                       Status);
            break;
        }

        // initialize bus data
        for (UINT8 pathId = 0; pathId < DeviceExtension->NumberOfBuses; pathId++)
        {
            DeviceExtension->Buses[pathId].BusIdentifier =
                DeviceExtension->PortConfig->InitiatorBusId[pathId];
            InitializeListHead(&DeviceExtension->Buses[pathId].LunsListHead);
        }

        /* If something was disabled via registry - apply it */
        if (ConfigInfo.DisableMultipleLun)
            DeviceExtension->MultipleReqsPerLun = PortConfig->MultipleRequestPerLu = FALSE;

        if (ConfigInfo.DisableTaggedQueueing)
            DeviceExtension->SupportsTaggedQueuing = PortConfig->MultipleRequestPerLu = FALSE;

        /* Check if we need to alloc SRB data */
        if (DeviceExtension->SupportsTaggedQueuing || DeviceExtension->MultipleReqsPerLun)
        {
            DeviceExtension->NeedSrbDataAlloc = TRUE;
        }
        else
        {
            DeviceExtension->NeedSrbDataAlloc = FALSE;
        }

        /* Get a pointer to the port capabilities */
        PortCapabilities = &DeviceExtension->PortCapabilities;

        /* Copy one field there */
        DeviceExtension->MapBuffers = PortConfig->MapBuffers;
        PortCapabilities->AdapterUsesPio = PortConfig->MapBuffers;

        if (PortConfig->DmaChannel != SP_UNINITIALIZED_VALUE || PortConfig->Master)
        {
            Status = SpiEnsureAdapterObject(DeviceExtension, PortConfig);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Failed to acquire DMA adapter (Status 0x%08X)\n", Status);
                break;
            }
        }

        if (DeviceExtension->SrbExtensionBuffer == NULL &&
            (DeviceExtension->SrbExtensionSize != 0 || PortConfig->AutoRequestSense))
        {
            DeviceExtension->SupportsAutoSense = PortConfig->AutoRequestSense;
            DeviceExtension->NeedSrbExtensionAlloc = TRUE;

            /* Allocate common buffer */
            Status = SpiAllocateCommonBuffer(DeviceExtension, PortConfig, 0);

            /* Check for failure */
            if (!NT_SUCCESS(Status))
            {
                SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: SpiAllocateCommonBuffer failed Status=0x%08lx\n",
                           Status);
                break;
            }
        }

        /* Allocate SrbData, if needed */
        if (DeviceExtension->NeedSrbDataAlloc)
        {
            ULONG Count;
            PSCSI_REQUEST_BLOCK_INFO SrbData;

            if (DeviceExtension->SrbDataCount != 0)
                Count = DeviceExtension->SrbDataCount;
            else
                Count = DeviceExtension->RequestsNumber * 2;

            /* Allocate the data */
            SrbData = ExAllocatePoolWithTag(
                NonPagedPool, Count * sizeof(SCSI_REQUEST_BLOCK_INFO), TAG_SCSIPORT);
            if (SrbData == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(SrbData, Count * sizeof(SCSI_REQUEST_BLOCK_INFO));

            DeviceExtension->SrbInfo = SrbData;
            DeviceExtension->FreeSrbInfo = SrbData;
            DeviceExtension->SrbDataCount = Count;

            /* Link it to the list */
            while (Count > 0)
            {
                SrbData->Requests.Flink = (PLIST_ENTRY)(SrbData + 1);
                SrbData++;
                Count--;
            }

            /* Mark the last entry of the list */
            SrbData--;
            SrbData->Requests.Flink = NULL;
        }

        /* Initialize port capabilities */
        PortCapabilities = &DeviceExtension->PortCapabilities;
        PortCapabilities->Length = sizeof(IO_SCSI_CAPABILITIES);
        PortCapabilities->MaximumTransferLength = PortConfig->MaximumTransferLength;

        if (PortConfig->ReceiveEvent)
            PortCapabilities->SupportedAsynchronousEvents |= SRBEV_SCSI_ASYNC_NOTIFICATION;

        PortCapabilities->TaggedQueuing = DeviceExtension->SupportsTaggedQueuing;
        PortCapabilities->AdapterScansDown = PortConfig->AdapterScansDown;

        if (PortConfig->AlignmentMask > PortDeviceObject->AlignmentRequirement)
            PortDeviceObject->AlignmentRequirement = PortConfig->AlignmentMask;

        PortCapabilities->AlignmentMask = PortDeviceObject->AlignmentRequirement;

        if (PortCapabilities->MaximumPhysicalPages == 0)
        {
            PortCapabilities->MaximumPhysicalPages =
                BYTES_TO_PAGES(PortCapabilities->MaximumTransferLength);

            /* Apply miniport's limits */
            if (PortConfig->NumberOfPhysicalBreaks < PortCapabilities->MaximumPhysicalPages)
            {
                PortCapabilities->MaximumPhysicalPages = PortConfig->NumberOfPhysicalBreaks;
            }
        }

        FdoCallHWInitialize(DeviceExtension);

        Status = FdoStartAdapter(DeviceExtension);

        if (!NT_SUCCESS(Status))
        {
            SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: FdoStartAdapter failed Status=0x%08lx Port=%lu Bus=%lu Interface=%lu\n",
                       Status,
                       DeviceExtension->PortNumber,
                       ConfigInfo.BusNumber,
                       DeviceExtension->PortConfig->AdapterInterfaceType);
            break;
        }

        SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize: Adapter started Port=%lu Bus=%lu Targets=%lu\n",
                   DeviceExtension->PortNumber,
                   ConfigInfo.BusNumber,
                   PortConfig->MaximumNumberOfTargets);

        FdoScanAdapter(DeviceExtension);

        FirstConfigCall = FALSE;

        /* Increase adapter number and bus number respectively */
        ConfigInfo.AdapterNumber++;

        if (!Again)
            ConfigInfo.BusNumber++;

        DPRINT("  Bus: %lu\n", ConfigInfo.BusNumber);

        DeviceFound = TRUE;
    }

Cleanup:

    /* Clean up the mess */
    if (!NT_SUCCESS(Status) && PortDeviceObject)
    {
        FdoRemoveAdapter(DeviceExtension, TRUE);
    }

    /* Close registry keys */
    if (ConfigInfo.ServiceKey != NULL)
        ZwClose(ConfigInfo.ServiceKey);

    if (ConfigInfo.DeviceKey != NULL)
        ZwClose(ConfigInfo.DeviceKey);

    if (ConfigInfo.BusKey != NULL)
        ZwClose(ConfigInfo.BusKey);

    if (ConfigInfo.AccessRanges != NULL)
        ExFreePool(ConfigInfo.AccessRanges);

    if (ConfigInfo.Parameter != NULL)
        ExFreePool(ConfigInfo.Parameter);

    SCSIPORT_TRACE("[SCSIPORT] ScsiPortInitialize exit: Status=0x%08lx DeviceFound=%d\n",
               Status,
               DeviceFound);

    return (DeviceFound == FALSE) ? Status : STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID NTAPI
ScsiPortIoMapTransfer(IN PVOID HwDeviceExtension,
                      IN PSCSI_REQUEST_BLOCK Srb,
                      IN PVOID LogicalAddress,
                      IN ULONG Length)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    PSCSI_PORT_LUN_EXTENSION LunExtension;
    PSCSI_REQUEST_BLOCK_INFO SrbInfo;
    PSCSI_PHYSICAL_ADDRESS PhysicalAddress;
    ULONG RemainingLength;
    ULONG MapLength;

    if (Srb == NULL || Length == 0)
    {
        return;
    }

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    Irp = Srb->OriginalRequest;
    if (Irp == NULL || Irp->MdlAddress == NULL)
    {
        return;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    if (IoStack == NULL || IoStack->MajorFunction != IRP_MJ_SCSI)
    {
        return;
    }

    LunExtension = IoStack->DeviceObject->DeviceExtension;
    if (LunExtension == NULL || LunExtension->Common.IsFDO)
    {
        return;
    }

    SrbInfo = SpiGetSrbData(DeviceExtension, LunExtension, Srb->QueueTag);
    if (SrbInfo == NULL)
    {
        return;
    }

    PhysicalAddress = (PSCSI_PHYSICAL_ADDRESS)LogicalAddress;

    RemainingLength = (Srb->DataTransferLength > SrbInfo->DmaCurrentOffset)
                          ? (Srb->DataTransferLength - SrbInfo->DmaCurrentOffset)
                          : 0;
    if (RemainingLength == 0)
    {
        return;
    }

    MapLength = (Length < RemainingLength) ? Length : RemainingLength;

    if (DeviceExtension->MapRegisters)
    {
        PUCHAR RequestVa;
        SCSI_PHYSICAL_ADDRESS SegmentAddress;

        if (PhysicalAddress == NULL)
        {
            return;
        }

        RequestVa = (PUCHAR)Srb->DataBuffer + SrbInfo->DmaCurrentOffset;
        SegmentAddress = ScsiPortGetPhysicalAddress(HwDeviceExtension,
                                                    Srb,
                                                    RequestVa,
                                                    &MapLength);

        if (MapLength == 0)
        {
            return;
        }

        PhysicalAddress->QuadPart = SegmentAddress.QuadPart;

        SrbInfo->DmaLogicalAddress = RequestVa;
        SrbInfo->DmaTransferLength = MapLength;
        SrbInfo->DmaFlags = SCSI_PORT_DMA_FLAG_MAPPED;
        if (Srb->SrbFlags & SRB_FLAGS_DATA_OUT)
        {
            SrbInfo->DmaFlags |= SCSI_PORT_DMA_FLAG_WRITE_OUT;
        }

        SrbInfo->DmaWindowCount++;
        SrbInfo->DmaBytesMapped += MapLength;

        return;
    }

    if (DeviceExtension->AdapterObject == NULL ||
        DeviceExtension->MapRegisterBase == NULL)
    {
        return;
    }

    if (SrbInfo->DmaFlags & SCSI_PORT_DMA_FLAG_MAPPED)
    {
        BOOLEAN PendingWrite = (SrbInfo->DmaFlags & SCSI_PORT_DMA_FLAG_WRITE_OUT) != 0;

        IoFlushAdapterBuffers(DeviceExtension->AdapterObject,
                              Irp->MdlAddress,
                              DeviceExtension->MapRegisterBase,
                              SrbInfo->DmaLogicalAddress,
                              SrbInfo->DmaTransferLength,
                              PendingWrite);

        if (SrbInfo->DmaTransferLength != 0)
        {
            ULONG Remaining;
            ULONG Delta = SrbInfo->DmaTransferLength;

            if (SrbInfo->DmaBytesFlushed < Srb->DataTransferLength)
            {
                Remaining = Srb->DataTransferLength - SrbInfo->DmaBytesFlushed;
            }
            else
            {
                Remaining = 0;
            }

            if (Delta > Remaining)
            {
                Delta = Remaining;
            }

            SrbInfo->DmaBytesFlushed += Delta;
        }

        SrbInfo->DmaCurrentOffset += SrbInfo->DmaTransferLength;
        SrbInfo->DmaLogicalAddress = NULL;
        SrbInfo->DmaTransferLength = 0;
        SrbInfo->DmaFlags = 0;

        RemainingLength = (Srb->DataTransferLength > SrbInfo->DmaCurrentOffset)
                              ? (Srb->DataTransferLength - SrbInfo->DmaCurrentOffset)
                              : 0;
        if (RemainingLength == 0)
        {
            return;
        }

        if (MapLength > RemainingLength)
        {
            MapLength = RemainingLength;
        }
    }

    {
        PUCHAR BaseVa;
        SIZE_T BufferOffset;
        PUCHAR CurrentVa;
        BOOLEAN WriteToDevice;
        ULONG IoLength;
        PHYSICAL_ADDRESS SegmentAddress;

        BaseVa = (PUCHAR)(SrbInfo->DataOffset ? SrbInfo->DataOffset
                                              : MmGetMdlVirtualAddress(Irp->MdlAddress));
        if (BaseVa == NULL)
        {
            return;
        }

        BufferOffset = (PUCHAR)Srb->DataBuffer - BaseVa;
        CurrentVa = BaseVa + BufferOffset + SrbInfo->DmaCurrentOffset;

        WriteToDevice = (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) ? TRUE : FALSE;

        IoLength = MapLength;
        SegmentAddress = IoMapTransfer(DeviceExtension->AdapterObject,
                                       Irp->MdlAddress,
                                       DeviceExtension->MapRegisterBase,
                                       CurrentVa,
                                       &IoLength,
                                       WriteToDevice);

        if (IoLength == 0)
        {
            return;
        }

        if (PhysicalAddress != NULL)
        {
            PhysicalAddress->QuadPart = SegmentAddress.QuadPart;
        }

        SrbInfo->DmaLogicalAddress = CurrentVa;
        SrbInfo->DmaTransferLength = IoLength;
        SrbInfo->DmaFlags = SCSI_PORT_DMA_FLAG_MAPPED;
        if (WriteToDevice)
        {
            SrbInfo->DmaFlags |= SCSI_PORT_DMA_FLAG_WRITE_OUT;
        }

        SrbInfo->DmaWindowCount++;
        SrbInfo->DmaBytesMapped += IoLength;

        if (DeviceExtension->HwDmaStarted != NULL)
        {
            DeviceExtension->HwDmaStarted(DeviceExtension->MiniPortDeviceExtension);
        }
    }
}

/*
 * @unimplemented
 */
VOID NTAPI
ScsiPortLogError(IN PVOID HwDeviceExtension,
                 IN PSCSI_REQUEST_BLOCK Srb OPTIONAL,
                 IN UCHAR PathId,
                 IN UCHAR TargetId,
                 IN UCHAR Lun,
                 IN ULONG ErrorCode,
                 IN ULONG UniqueId)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PIO_ERROR_LOG_PACKET Packet;
    ULONG AddressKey;

    DeviceExtension = CONTAINING_RECORD(HwDeviceExtension,
                                        SCSI_PORT_DEVICE_EXTENSION,
                                        MiniPortDeviceExtension);

    if (DeviceExtension->Common.DeviceObject == NULL)
    {
        return;
    }

    Packet = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(DeviceExtension->Common.DeviceObject,
                                                           (UCHAR)sizeof(IO_ERROR_LOG_PACKET));

    if (Packet == NULL)
    {
        return;
    }

    RtlZeroMemory(Packet, sizeof(*Packet));

    Packet->ErrorCode = ErrorCode;
    Packet->UniqueErrorValue = UniqueId;
    Packet->FinalStatus = STATUS_UNSUCCESSFUL;
    Packet->SequenceNumber = 0;
    Packet->MajorFunctionCode = IRP_MJ_SCSI;
    Packet->IoControlCode = 0;
    Packet->RetryCount = 0;

    AddressKey = ((ULONG)PathId << 16) | ((ULONG)TargetId << 8) | Lun;
    Packet->DumpDataSize = sizeof(AddressKey);
    Packet->DumpData[0] = AddressKey;

    IoWriteErrorLogEntry(Packet);
}

/*
 * @implemented
 */
VOID NTAPI
ScsiPortMoveMemory(OUT PVOID Destination,
           IN PVOID Source,
           IN ULONG Length)
{
  RtlMoveMemory(Destination,
        Source,
        Length);
}


/*
 * @implemented
 */
VOID
ScsiPortNotification(IN SCSI_NOTIFICATION_TYPE NotificationType, IN PVOID HwDeviceExtension, ...)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    va_list ap;

    DPRINT("ScsiPortNotification() called\n");

    DeviceExtension =
        CONTAINING_RECORD(HwDeviceExtension, SCSI_PORT_DEVICE_EXTENSION, MiniPortDeviceExtension);

    DPRINT("DeviceExtension %p\n", DeviceExtension);

    va_start(ap, HwDeviceExtension);

    switch (NotificationType)
    {
        case RequestComplete:
        {
            PSCSI_REQUEST_BLOCK Srb;
            PSCSI_REQUEST_BLOCK_INFO SrbData;

            Srb = (PSCSI_REQUEST_BLOCK)va_arg(ap, PSCSI_REQUEST_BLOCK);

            DPRINT("Notify: RequestComplete (Srb %p)\n", Srb);

            /* Make sure Srb is alright */
            ASSERT(Srb->SrbStatus != SRB_STATUS_PENDING);
            ASSERT(
                Srb->Function != SRB_FUNCTION_EXECUTE_SCSI ||
                Srb->SrbStatus != SRB_STATUS_SUCCESS || Srb->ScsiStatus == SCSISTAT_GOOD);

            if (!(Srb->SrbFlags & SRB_FLAGS_IS_ACTIVE))
            {
                /* It's been already completed */
                va_end(ap);
                return;
            }

            /* It's not active anymore */
            Srb->SrbFlags &= ~SRB_FLAGS_IS_ACTIVE;

            if (Srb->Function == SRB_FUNCTION_ABORT_COMMAND)
            {
                /* TODO: Treat it specially */
                ASSERT(FALSE);
            }
            else
            {
                PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Srb->OriginalRequest);
                PSCSI_PORT_LUN_EXTENSION LunExtension = IoStack->DeviceObject->DeviceExtension;
                ASSERT(LunExtension && !LunExtension->Common.IsFDO);

                /* Get the SRB data */
                SrbData = SpiGetSrbData(DeviceExtension, LunExtension, Srb->QueueTag);

                /* Make sure there are no CompletedRequests and there is a Srb */
                ASSERT(SrbData->CompletedRequests == NULL && SrbData->Srb != NULL);

                /* If it's a read/write request, make sure it has data inside it */
                if ((Srb->SrbStatus == SRB_STATUS_SUCCESS) &&
                    ((Srb->Cdb[0] == SCSIOP_READ) || (Srb->Cdb[0] == SCSIOP_WRITE)))
                {
                    ASSERT(Srb->DataTransferLength);
                }

                SrbData->CompletedRequests = DeviceExtension->InterruptData.CompletedRequests;
                DeviceExtension->InterruptData.CompletedRequests = SrbData;
            }
        }
        break;

        case NextRequest:
            DPRINT("Notify: NextRequest\n");
            DeviceExtension->InterruptData.Flags |= SCSI_PORT_NEXT_REQUEST_READY;
            break;

        case NextLuRequest:
        {
            UCHAR PathId;
            UCHAR TargetId;
            UCHAR Lun;
            PSCSI_PORT_LUN_EXTENSION LunExtension;

            PathId = (UCHAR)va_arg(ap, int);
            TargetId = (UCHAR)va_arg(ap, int);
            Lun = (UCHAR)va_arg(ap, int);

            DPRINT(
                "Notify: NextLuRequest(PathId %u  TargetId %u  Lun %u)\n", PathId, TargetId, Lun);

            /* Mark it in the flags field */
            DeviceExtension->InterruptData.Flags |= SCSI_PORT_NEXT_REQUEST_READY;

            /* Get the LUN extension */
            LunExtension = GetLunByPath(DeviceExtension, PathId, TargetId, Lun);

            /* If returned LunExtension is NULL, break out */
            if (!LunExtension)
                break;

            /* This request should not be processed if */
            if ((LunExtension->ReadyLun) || (LunExtension->SrbInfo.Srb))
            {
                /* Nothing to do here */
                break;
            }

            /* Add this LUN to the list */
            LunExtension->ReadyLun = DeviceExtension->InterruptData.ReadyLun;
            DeviceExtension->InterruptData.ReadyLun = LunExtension;
        }
        break;

        case ResetDetected:
            DPRINT("Notify: ResetDetected\n");
            /* Add RESET flags */
            DeviceExtension->InterruptData.Flags |= SCSI_PORT_RESET | SCSI_PORT_RESET_REPORTED;
            SpiLogPortError(DeviceExtension,
                            NULL,
                            SP_UNTAGGED,
                            SP_UNTAGGED,
                            SP_UNTAGGED,
                            IO_ERR_RESET,
                            'RSET');
            break;

        case CallDisableInterrupts:
            DeviceExtension->InterruptData.Flags |= SCSI_PORT_DISABLE_INTERRUPTS;
            break;

        case CallEnableInterrupts:
            DeviceExtension->InterruptData.Flags &= ~SCSI_PORT_DISABLE_INTERRUPTS;
            break;

        case RequestTimerCall:
            DPRINT("Notify: RequestTimerCall\n");
            DeviceExtension->InterruptData.Flags |= SCSI_PORT_TIMER_NEEDED;
            DeviceExtension->InterruptData.HwScsiTimer = (PHW_TIMER)va_arg(ap, PHW_TIMER);
            DeviceExtension->InterruptData.MiniportTimerValue = (ULONG)va_arg(ap, ULONG);
            break;

        case BusChangeDetected:
            DPRINT1("UNIMPLEMENTED SCSI Notification called: BusChangeDetected!\n");
            break;

        default:
            DPRINT1("Unsupported notification from WMI: %lu\n", NotificationType);
            break;
    }

    va_end(ap);

    /* Request a DPC after we're done with the interrupt */
    DeviceExtension->InterruptData.Flags |= SCSI_PORT_NOTIFICATION_NEEDED;
}

/*
 * @implemented
 */
BOOLEAN NTAPI
ScsiPortValidateRange(IN PVOID HwDeviceExtension,
              IN INTERFACE_TYPE BusType,
              IN ULONG SystemIoBusNumber,
              IN SCSI_PHYSICAL_ADDRESS IoAddress,
              IN ULONG NumberOfBytes,
              IN BOOLEAN InIoSpace)
{
  DPRINT("ScsiPortValidateRange()\n");
  return(TRUE);
}


/* INTERNAL FUNCTIONS ********************************************************/

VOID
SpiResourceToConfig(IN PHW_INITIALIZATION_DATA HwInitializationData,
                    IN PCM_FULL_RESOURCE_DESCRIPTOR ResourceDescriptor,
                    IN PPORT_CONFIGURATION_INFORMATION PortConfig)
{
    PACCESS_RANGE AccessRange;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialData;
    ULONG RangeNumber;
    ULONG Index;
    ULONG Interrupt = 0;
    ULONG Dma = 0;

    RangeNumber = 0;

    /* Loop through all entries */
    for (Index = 0; Index < ResourceDescriptor->PartialResourceList.Count; Index++)
    {
        PartialData = &ResourceDescriptor->PartialResourceList.PartialDescriptors[Index];

        switch (PartialData->Type)
        {
        case CmResourceTypePort:
            /* Copy access ranges */
            if (RangeNumber < HwInitializationData->NumberOfAccessRanges)
            {
                AccessRange = &((*(PortConfig->AccessRanges))[RangeNumber]);

                AccessRange->RangeStart = PartialData->u.Port.Start;
                AccessRange->RangeLength = PartialData->u.Port.Length;

                AccessRange->RangeInMemory = FALSE;
                RangeNumber++;
            }
            break;

        case CmResourceTypeMemory:
            /* Copy access ranges */
            if (RangeNumber < HwInitializationData->NumberOfAccessRanges)
            {
                AccessRange = &((*(PortConfig->AccessRanges))[RangeNumber]);

                AccessRange->RangeStart = PartialData->u.Memory.Start;
                AccessRange->RangeLength = PartialData->u.Memory.Length;

                AccessRange->RangeInMemory = TRUE;
                RangeNumber++;
            }
            break;

        case CmResourceTypeInterrupt:

            if (PartialData->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
            {
                /* Handled separately by the PnP path */
                continue;
            }

            if (Interrupt == 0)
            {
                /* Copy interrupt data */
                PortConfig->BusInterruptLevel = PartialData->u.Interrupt.Level;
                PortConfig->BusInterruptVector = PartialData->u.Interrupt.Vector;

                /* Set interrupt mode accordingly to the resource */
                if ((PartialData->Flags & CM_RESOURCE_INTERRUPT_LEVEL_LATCHED_BITS) == CM_RESOURCE_INTERRUPT_LATCHED)
                    PortConfig->InterruptMode = Latched;
                else
                    PortConfig->InterruptMode = LevelSensitive;
            }
            else if (Interrupt == 1)
            {
                /* Copy interrupt data */
                PortConfig->BusInterruptLevel2 = PartialData->u.Interrupt.Level;
                PortConfig->BusInterruptVector2 = PartialData->u.Interrupt.Vector;

                /* Set interrupt mode accordingly to the resource */
                if ((PartialData->Flags & CM_RESOURCE_INTERRUPT_LEVEL_LATCHED_BITS) == CM_RESOURCE_INTERRUPT_LATCHED)
                    PortConfig->InterruptMode2 = Latched;
                else
                    PortConfig->InterruptMode2 = LevelSensitive;
            }

            Interrupt++;
            break;

        case CmResourceTypeDma:

            if (Dma == 0)
            {
                PortConfig->DmaChannel = PartialData->u.Dma.Channel;
                PortConfig->DmaPort = PartialData->u.Dma.Port;

                if (PartialData->Flags & CM_RESOURCE_DMA_8)
                    PortConfig->DmaWidth = Width8Bits;
                else if ((PartialData->Flags & CM_RESOURCE_DMA_16) ||
                         (PartialData->Flags & CM_RESOURCE_DMA_8_AND_16)) //???
                    PortConfig->DmaWidth = Width16Bits;
                else if (PartialData->Flags & CM_RESOURCE_DMA_32)
                    PortConfig->DmaWidth = Width32Bits;
            }
            else if (Dma == 1)
            {
                PortConfig->DmaChannel2 = PartialData->u.Dma.Channel;
                PortConfig->DmaPort2 = PartialData->u.Dma.Port;

                if (PartialData->Flags & CM_RESOURCE_DMA_8)
                    PortConfig->DmaWidth2 = Width8Bits;
                else if ((PartialData->Flags & CM_RESOURCE_DMA_16) ||
                         (PartialData->Flags & CM_RESOURCE_DMA_8_AND_16)) //???
                    PortConfig->DmaWidth2 = Width16Bits;
                else if (PartialData->Flags & CM_RESOURCE_DMA_32)
                    PortConfig->DmaWidth2 = Width32Bits;
            }
            break;
        }
    }
}

static PCM_RESOURCE_LIST
SpiConfigToResource(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                    PPORT_CONFIGURATION_INFORMATION PortConfig)
{
    PCONFIGURATION_INFORMATION ConfigInfo;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR ResourceDescriptor;
    PACCESS_RANGE AccessRange;
    ULONG ListLength = 0, i, FullSize;
    ULONG Interrupt, Dma;

    /* Get current Atdisk usage from the system */
    ConfigInfo = IoGetConfigurationInformation();

    if (PortConfig->AtdiskPrimaryClaimed)
        ConfigInfo->AtDiskPrimaryAddressClaimed = TRUE;

    if (PortConfig->AtdiskSecondaryClaimed)
        ConfigInfo->AtDiskSecondaryAddressClaimed = TRUE;

    /* Do we use DMA? */
    if (PortConfig->DmaChannel != SP_UNINITIALIZED_VALUE ||
        PortConfig->DmaPort != SP_UNINITIALIZED_VALUE)
    {
        Dma = 1;

        if (PortConfig->DmaChannel2 != SP_UNINITIALIZED_VALUE ||
            PortConfig->DmaPort2 != SP_UNINITIALIZED_VALUE)
            Dma++;
    }
    else
    {
        Dma = 0;
    }
    ListLength += Dma;

    /* How many interrupts to we have? */
    Interrupt = DeviceExtension->InterruptCount;
    ListLength += Interrupt;

    /* How many access ranges do we use? */
    AccessRange = &((*(PortConfig->AccessRanges))[0]);
    for (i = 0; i < PortConfig->NumberOfAccessRanges; i++)
    {
        if (AccessRange->RangeLength != 0)
            ListLength++;

        AccessRange++;
    }

    /* Allocate the resource list, since we know its size now */
    FullSize = sizeof(CM_RESOURCE_LIST) + (ListLength - 1) *
        sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    ResourceList = (PCM_RESOURCE_LIST)ExAllocatePoolWithTag(PagedPool, FullSize, TAG_SCSIPORT);

    if (!ResourceList)
        return NULL;

    /* Zero it */
    RtlZeroMemory(ResourceList, FullSize);

    /* Initialize it */
    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = PortConfig->AdapterInterfaceType;
    ResourceList->List[0].BusNumber = PortConfig->SystemIoBusNumber;
    ResourceList->List[0].PartialResourceList.Count = ListLength;
    ResourceDescriptor = ResourceList->List[0].PartialResourceList.PartialDescriptors;

    /* Copy access ranges array over */
    for (i = 0; i < PortConfig->NumberOfAccessRanges; i++)
    {
        AccessRange = &((*(PortConfig->AccessRanges))[i]);

        /* If the range is empty - skip it */
        if (AccessRange->RangeLength == 0)
            continue;

        if (AccessRange->RangeInMemory)
        {
            ResourceDescriptor->Type = CmResourceTypeMemory;
            ResourceDescriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        }
        else
        {
            ResourceDescriptor->Type = CmResourceTypePort;
            ResourceDescriptor->Flags = CM_RESOURCE_PORT_IO;
        }

        ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;

        ResourceDescriptor->u.Memory.Start = AccessRange->RangeStart;
        ResourceDescriptor->u.Memory.Length = AccessRange->RangeLength;

        ResourceDescriptor++;
    }

    /* If we use interrupt(s), copy them */
    while (Interrupt)
    {
        ResourceDescriptor->Type = CmResourceTypeInterrupt;

        if (PortConfig->AdapterInterfaceType == MicroChannel ||
            ((Interrupt == 2) ? PortConfig->InterruptMode2 : PortConfig->InterruptMode) == LevelSensitive)
        {
            ResourceDescriptor->ShareDisposition = CmResourceShareShared;
            ResourceDescriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        }
        else
        {
            ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
            ResourceDescriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
        }

        ResourceDescriptor->u.Interrupt.Level = (Interrupt == 2) ? PortConfig->BusInterruptLevel2 : PortConfig->BusInterruptLevel;
        ResourceDescriptor->u.Interrupt.Vector = (Interrupt == 2) ? PortConfig->BusInterruptVector2 : PortConfig->BusInterruptVector;
        ResourceDescriptor->u.Interrupt.Affinity = 0;

        ResourceDescriptor++;
        Interrupt--;
    }

    /* Copy DMA data */
    while (Dma)
    {
        ResourceDescriptor->Type = CmResourceTypeDma;
        ResourceDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        ResourceDescriptor->u.Dma.Channel = (Dma == 2) ? PortConfig->DmaChannel2 : PortConfig->DmaChannel;
        ResourceDescriptor->u.Dma.Port = (Dma == 2) ? PortConfig->DmaPort2 : PortConfig->DmaPort;
        ResourceDescriptor->Flags = 0;

        if (((Dma == 2) ? PortConfig->DmaWidth2 : PortConfig->DmaWidth) == Width8Bits)
            ResourceDescriptor->Flags |= CM_RESOURCE_DMA_8;
        else if (((Dma == 2) ? PortConfig->DmaWidth2 : PortConfig->DmaWidth) == Width16Bits)
            ResourceDescriptor->Flags |= CM_RESOURCE_DMA_16;
        else
            ResourceDescriptor->Flags |= CM_RESOURCE_DMA_32;

        if (((Dma == 2) ? PortConfig->DmaChannel2 : PortConfig->DmaChannel) == SP_UNINITIALIZED_VALUE)
            ResourceDescriptor->u.Dma.Channel = 0;

        if (((Dma == 2) ? PortConfig->DmaPort2 : PortConfig->DmaPort) == SP_UNINITIALIZED_VALUE)
            ResourceDescriptor->u.Dma.Port = 0;

        ResourceDescriptor++;
        Dma--;
    }

    return ResourceList;
}


static BOOLEAN
SpiGetPciConfigData(IN PDRIVER_OBJECT DriverObject,
                    IN PDEVICE_OBJECT DeviceObject,
                    IN struct _HW_INITIALIZATION_DATA *HwInitializationData,
                    IN OUT PPORT_CONFIGURATION_INFORMATION PortConfig,
                    IN PUNICODE_STRING RegistryPath,
                    IN ULONG BusNumber,
                    IN OUT PPCI_SLOT_NUMBER NextSlotNumber)
{
    PCI_COMMON_CONFIG PciConfig;
    PCI_SLOT_NUMBER SlotNumber;
    ULONG DataSize;
    ULONG DeviceNumber;
    ULONG FunctionNumber;
    CHAR VendorIdString[8];
    CHAR DeviceIdString[8];
    UNICODE_STRING UnicodeStr;
    PCM_RESOURCE_LIST ResourceList = NULL;
    NTSTATUS Status;

    DPRINT ("SpiGetPciConfiguration() called\n");

    if (!SpiIsPciBusInRange(BusNumber))
    {
        return FALSE;
    }

    SlotNumber.u.AsULONG = 0;

    /* Loop through all devices */
    for (DeviceNumber = NextSlotNumber->u.bits.DeviceNumber; DeviceNumber < PCI_MAX_DEVICES; DeviceNumber++)
    {
        SlotNumber.u.bits.DeviceNumber = DeviceNumber;

        /* Loop through all functions */
        for (FunctionNumber = NextSlotNumber->u.bits.FunctionNumber; FunctionNumber < PCI_MAX_FUNCTION; FunctionNumber++)
        {
            SlotNumber.u.bits.FunctionNumber = FunctionNumber;

            /* Get PCI config bytes */
            DataSize = HalGetBusData(PCIConfiguration,
                                     BusNumber,
                                     SlotNumber.u.AsULONG,
                                     &PciConfig,
                                     sizeof(ULONG));

            /* If result of HalGetBusData is 0, then the bus is wrong */
            if (DataSize == 0)
                return FALSE;

            /* Check if result is PCI_INVALID_VENDORID or too small */
            if ((DataSize < sizeof(ULONG)) ||
                (PciConfig.VendorID == PCI_INVALID_VENDORID))
            {
                /* Continue to try the next function */
                continue;
            }

            sprintf (VendorIdString, "%04hx", PciConfig.VendorID);
            sprintf (DeviceIdString, "%04hx", PciConfig.DeviceID);

            if (_strnicmp(VendorIdString, HwInitializationData->VendorId, HwInitializationData->VendorIdLength) ||
                _strnicmp(DeviceIdString, HwInitializationData->DeviceId, HwInitializationData->DeviceIdLength))
            {
                /* It is not our device */
                continue;
            }

            DPRINT("Found device 0x%04hx 0x%04hx at %1lu %2lu %1lu\n",
                   PciConfig.VendorID,
                   PciConfig.DeviceID,
                   BusNumber,
                   SlotNumber.u.bits.DeviceNumber,
                   SlotNumber.u.bits.FunctionNumber);


            RtlInitUnicodeString(&UnicodeStr, L"ScsiAdapter");
            Status = HalAssignSlotResources(RegistryPath,
                                            &UnicodeStr,
                                            DriverObject,
                                            DeviceObject,
                                            PCIBus,
                                            BusNumber,
                                            SlotNumber.u.AsULONG,
                                            &ResourceList);

            if (!NT_SUCCESS(Status))
                break;

            /* Create configuration information */
            SpiResourceToConfig(HwInitializationData,
                                ResourceList->List,
                                PortConfig);

            /* Free the resource list */
            ExFreePool(ResourceList);

            /* Set dev & fn numbers */
            NextSlotNumber->u.bits.DeviceNumber = DeviceNumber;
            NextSlotNumber->u.bits.FunctionNumber = FunctionNumber + 1;

            /* Save the slot number */
            PortConfig->SlotNumber = SlotNumber.u.AsULONG;

            return TRUE;
        }
       NextSlotNumber->u.bits.FunctionNumber = 0;
    }

    NextSlotNumber->u.bits.DeviceNumber = 0;
    DPRINT ("No device found\n");

    return FALSE;
}



/**********************************************************************
 * NAME                         INTERNAL
 *  ScsiPortCreateClose
 *
 * DESCRIPTION
 *  Answer requests for Create/Close calls: a null operation.
 *
 * RUN LEVEL
 *  PASSIVE_LEVEL
 *
 * ARGUMENTS
 *  DeviceObject
 *      Pointer to a device object.
 *
 *  Irp
 *      Pointer to an IRP.
 *
 * RETURN VALUE
 *  Status.
 */

static NTSTATUS NTAPI
ScsiPortCreateClose(IN PDEVICE_OBJECT DeviceObject,
                   IN PIRP Irp)
{
    DPRINT("ScsiPortCreateClose()\n");

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

IO_ALLOCATION_ACTION
NTAPI
SpiAdapterControl(PDEVICE_OBJECT DeviceObject,
                  PIRP Irp,
                  PVOID MapRegisterBase,
                  PVOID Context)
{
    PSCSI_REQUEST_BLOCK Srb;
    PSCSI_SG_ADDRESS ScatterGatherList;
    KIRQL CurrentIrql;
    PIO_STACK_LOCATION IrpStack;
    ULONG TotalLength = 0;
    PSCSI_REQUEST_BLOCK_INFO SrbInfo;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PUCHAR DataVA;
    BOOLEAN WriteToDevice;

    /* Get pointers to SrbInfo and DeviceExtension */
    SrbInfo = (PSCSI_REQUEST_BLOCK_INFO)Context;
    DeviceExtension = DeviceObject->DeviceExtension;

    /* Get pointer to SRB */
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    Srb = (PSCSI_REQUEST_BLOCK)IrpStack->Parameters.Others.Argument1;

    /* Depending on the map registers number, we allocate
       either from NonPagedPool, or from our static list */
    if (SrbInfo->NumberOfMapRegisters > MAX_SG_LIST)
    {
        SrbInfo->ScatterGather = ExAllocatePoolWithTag(
            NonPagedPool, SrbInfo->NumberOfMapRegisters * sizeof(SCSI_SG_ADDRESS), TAG_SCSIPORT);

        if (SrbInfo->ScatterGather == NULL)
            ASSERT(FALSE);

        Srb->SrbFlags |= SRB_FLAGS_SGLIST_FROM_POOL;
    }
    else
    {
        SrbInfo->ScatterGather = SrbInfo->ScatterGatherList;
    }

    /* Use chosen SG list source */
    ScatterGatherList = SrbInfo->ScatterGather;

    /* Save map registers base */
    SrbInfo->BaseOfMapRegister = MapRegisterBase;

    /* Determine WriteToDevice flag */
    WriteToDevice = Srb->SrbFlags & SRB_FLAGS_DATA_OUT ? TRUE : FALSE;

    /* Get virtual address of the data buffer */
    DataVA = (PUCHAR)MmGetMdlVirtualAddress(Irp->MdlAddress) +
                ((PCHAR)Srb->DataBuffer - SrbInfo->DataOffset);

    /* Build the actual SG list */
    while (TotalLength < Srb->DataTransferLength)
    {
        if (!ScatterGatherList)
            break;

        ScatterGatherList->Length = Srb->DataTransferLength - TotalLength;
        ScatterGatherList->PhysicalAddress = IoMapTransfer(DeviceExtension->AdapterObject,
                                                           Irp->MdlAddress,
                                                           MapRegisterBase,
                                                           DataVA + TotalLength,
                                                           &ScatterGatherList->Length,
                                                           WriteToDevice);

        TotalLength += ScatterGatherList->Length;
        ScatterGatherList++;
    }

    /* Schedule an active request */
    InterlockedIncrement(&DeviceExtension->ActiveRequestCounter );
    KeAcquireSpinLock(&DeviceExtension->SpinLock, &CurrentIrql);
    PKINTERRUPT primaryInterrupt = SppGetPrimaryInterruptObject(DeviceExtension);

    if (primaryInterrupt)
    {
        KeSynchronizeExecution(primaryInterrupt,
                                ScsiPortStartPacket,
                                DeviceObject);
    }
    else
    {
        ScsiPortStartPacket(DeviceObject);
    }
    KeReleaseSpinLock(&DeviceExtension->SpinLock, CurrentIrql);

    return DeallocateObjectKeepRegisters;
}

BOOLEAN
NTAPI
ScsiPortIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    UNREFERENCED_PARAMETER(Interrupt);

    return SppHandleInterrupt((PSCSI_PORT_INTERRUPT_CONTEXT)ServiceContext, (ULONG)-1);
}

static BOOLEAN
SppHandleInterrupt(
    _In_ PSCSI_PORT_INTERRUPT_CONTEXT Context,
    _In_ ULONG MessageId)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension = Context->DeviceExtension;

    Context->MessageId = MessageId;

    DPRINT("ScsiPortIsr() called!\n");

    /* If interrupts are disabled - we don't expect any */
    if (DeviceExtension->InterruptData.Flags & SCSI_PORT_DISABLE_INTERRUPTS)
        return FALSE;

    /* Call miniport's HwInterrupt routine */
    if (DeviceExtension->HwInterrupt(&DeviceExtension->MiniPortDeviceExtension) == FALSE)
    {
        /* This interrupt doesn't belong to us */
        return FALSE;
    }

    /* If flag of notification is set - queue a DPC */
    if (DeviceExtension->InterruptData.Flags & SCSI_PORT_NOTIFICATION_NEEDED)
    {
        IoRequestDpc(DeviceExtension->Common.DeviceObject,
                     DeviceExtension->CurrentIrp,
                     DeviceExtension);
    }

    return TRUE;
}

BOOLEAN
NTAPI
SppMessageInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(Interrupt);

    return SppHandleInterrupt((PSCSI_PORT_INTERRUPT_CONTEXT)ServiceContext, MessageId);
}

BOOLEAN
NTAPI
SppMessageFallbackService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    UNREFERENCED_PARAMETER(Interrupt);

    return SppHandleInterrupt((PSCSI_PORT_INTERRUPT_CONTEXT)ServiceContext, (ULONG)-1);
}

BOOLEAN
NTAPI
SpiProcessTimeout(PVOID ServiceContext)
{
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)ServiceContext;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    ULONG Bus;

    DPRINT("SpiProcessTimeout() entered\n");

    DeviceExtension->TimerCount = -1;

    if (DeviceExtension->InterruptData.Flags & SCSI_PORT_RESET)
    {
        DeviceExtension->InterruptData.Flags &= ~SCSI_PORT_RESET;

        if (DeviceExtension->InterruptData.Flags & SCSI_PORT_RESET_REQUEST)
        {
            DeviceExtension->InterruptData.Flags &=  ~SCSI_PORT_RESET_REQUEST;
            ScsiPortStartPacket(ServiceContext);
        }

        return FALSE;
    }
    else
    {
        DPRINT("Resetting the bus\n");

        for (Bus = 0; Bus < DeviceExtension->NumberOfBuses; Bus++)
        {
            BOOLEAN result = DeviceExtension->HwResetBus(DeviceExtension->MiniPortDeviceExtension, Bus);
            UNREFERENCED_PARAMETER(result);

            /* Reset flags and set reset timeout to 4 seconds */
            DeviceExtension->InterruptData.Flags |= SCSI_PORT_RESET;
            DeviceExtension->TimerCount = 4;
        }

        /* If miniport requested - request a dpc for it */
        if (DeviceExtension->InterruptData.Flags & SCSI_PORT_NOTIFICATION_NEEDED)
            IoRequestDpc(DeviceExtension->Common.DeviceObject, NULL, NULL);
    }

    return TRUE;
}


BOOLEAN
NTAPI
SpiResetBus(PVOID ServiceContext)
{
    PRESETBUS_PARAMS ResetParams = (PRESETBUS_PARAMS)ServiceContext;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;

    /* Perform the bus reset */
    DeviceExtension = (PSCSI_PORT_DEVICE_EXTENSION)ResetParams->DeviceExtension;
    BOOLEAN result = DeviceExtension->HwResetBus(DeviceExtension->MiniPortDeviceExtension,
                                                 ResetParams->PathId);
    UNREFERENCED_PARAMETER(result);

    /* Set flags and start the timer */
    DeviceExtension->InterruptData.Flags |= SCSI_PORT_RESET;
    DeviceExtension->TimerCount = 4;

    /* If miniport requested - give him a DPC */
    if (DeviceExtension->InterruptData.Flags & SCSI_PORT_NOTIFICATION_NEEDED)
        IoRequestDpc(DeviceExtension->Common.DeviceObject, NULL, NULL);

    return TRUE;
}

//    ScsiPortIoTimer
//  DESCRIPTION:
//    This function handles timeouts and other time delayed processing
//
//  RUN LEVEL:
//
//  ARGUMENTS:
//    IN  PDEVICE_OBJECT  DeviceObject  Device object registered with timer
//    IN  PVOID           Context       the Controller extension for the
//                                      controller the device is on
//
static VOID NTAPI
ScsiPortIoTimer(PDEVICE_OBJECT DeviceObject,
                PVOID Context)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;
    PSCSI_PORT_LUN_EXTENSION LunExtension;
    PIRP Irp;

    DPRINT("ScsiPortIoTimer()\n");

    DeviceExtension = (PSCSI_PORT_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    /* Protect with the spinlock */
    KeAcquireSpinLockAtDpcLevel(&DeviceExtension->SpinLock);

    /* Check timeouts */
    if (DeviceExtension->TimerCount > 0)
    {
        /* Decrease the timeout counter */
        DeviceExtension->TimerCount--;

        if (DeviceExtension->TimerCount == 0)
        {
            /* Timeout, process it */
            PKINTERRUPT interruptObject = SppGetPrimaryInterruptObject(DeviceExtension);

            if (interruptObject &&
                KeSynchronizeExecution(interruptObject,
                                       SpiProcessTimeout,
                                       DeviceExtension->Common.DeviceObject))
            {
                DPRINT("Error happened during processing timeout, but nothing critical\n");
            }
        }

        KeReleaseSpinLockFromDpcLevel(&DeviceExtension->SpinLock);

        /* We should exit now, since timeout is processed */
        return;
    }

    /* Per-Lun scanning of timeouts is needed... */
    for (UINT8 pathId = 0; pathId < DeviceExtension->NumberOfBuses; pathId++)
    {
        PSCSI_BUS_INFO bus = &DeviceExtension->Buses[pathId];

        for (PLIST_ENTRY lunEntry = bus->LunsListHead.Flink;
             lunEntry != &bus->LunsListHead;
             lunEntry = lunEntry->Flink)
        {
            LunExtension = CONTAINING_RECORD(lunEntry, SCSI_PORT_LUN_EXTENSION, LunEntry);

            if (LunExtension->Flags & LUNEX_BUSY)
            {
                if (!(LunExtension->Flags &
                    (LUNEX_NEED_REQUEST_SENSE | LUNEX_FROZEN_QUEUE)))
                {
                    DPRINT("Retrying busy request\n");

                    /* Clear flags, and retry busy request */
                    LunExtension->Flags &= ~(LUNEX_BUSY | LUNEX_FULL_QUEUE);
                    Irp = LunExtension->BusyRequest;

                    /* Clearing busy request */
                    LunExtension->BusyRequest = NULL;

                    KeReleaseSpinLockFromDpcLevel(&DeviceExtension->SpinLock);

                    IoStartPacket(DeviceObject, Irp, (PULONG)NULL, NULL);

                    KeAcquireSpinLockAtDpcLevel(&DeviceExtension->SpinLock);
                }
            }
            else if (LunExtension->RequestTimeout == 0)
            {
                RESETBUS_PARAMS ResetParams;

                LunExtension->RequestTimeout = -1;

                DPRINT("Request timed out, resetting bus\n");

                SpiLogPortError(DeviceExtension,
                                NULL,
                                LunExtension->PathId,
                                LunExtension->TargetId,
                                LunExtension->Lun,
                                IO_ERR_TIMEOUT,
                                'LTO ');

                /* Pass params to the bus reset routine */
                ResetParams.PathId = LunExtension->PathId;
                ResetParams.DeviceExtension = DeviceExtension;

                PKINTERRUPT interruptObject = SppGetPrimaryInterruptObject(DeviceExtension);

                if (!interruptObject ||
                    !KeSynchronizeExecution(interruptObject,
                                            SpiResetBus,
                                            &ResetParams))
                {
                    DPRINT1("Reset failed\n");
                }
                else
                {
                    SpiLogPortError(DeviceExtension,
                                    NULL,
                                    LunExtension->PathId,
                                    SP_UNTAGGED,
                                    SP_UNTAGGED,
                                    IO_ERR_RESET,
                                    'RBUS');
                }
            }
            else if (LunExtension->RequestTimeout > 0)
            {
                /* Decrement the timeout counter */
                LunExtension->RequestTimeout--;
            }
        }
    }

    /* Release the spinlock */
    KeReleaseSpinLockFromDpcLevel(&DeviceExtension->SpinLock);
}

VOID
NTAPI
SpiMiniportTimerDpc(IN struct _KDPC *Dpc,
                    IN PVOID DeviceObject,
                    IN PVOID SystemArgument1,
                    IN PVOID SystemArgument2)
{
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension;

    DPRINT("Miniport timer DPC\n");

    DeviceExtension = ((PDEVICE_OBJECT)DeviceObject)->DeviceExtension;

    /* Acquire the spinlock */
    KeAcquireSpinLockAtDpcLevel(&DeviceExtension->SpinLock);

    /* Call the timer routine */
    if (DeviceExtension->HwScsiTimer != NULL)
    {
        DeviceExtension->HwScsiTimer(&DeviceExtension->MiniPortDeviceExtension);
    }

    /* Release the spinlock */
    KeReleaseSpinLockFromDpcLevel(&DeviceExtension->SpinLock);

    if (DeviceExtension->InterruptData.Flags & SCSI_PORT_NOTIFICATION_NEEDED)
    {
        ScsiPortDpcForIsr(NULL,
                          DeviceExtension->Common.DeviceObject,
                          NULL,
                          NULL);
    }
}

static NTSTATUS
SpiCreatePortConfig(PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                    PHW_INITIALIZATION_DATA HwInitData,
                    PCONFIGURATION_INFO InternalConfigInfo,
                    PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                    BOOLEAN ZeroStruct)
{
    UNICODE_STRING UnicodeString;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PCONFIGURATION_INFORMATION DdkConfigInformation;
    HANDLE RootKey, Key;
    BOOLEAN Found;
    WCHAR DeviceBuffer[16];
    WCHAR StrBuffer[512];
    ULONG Bus;
    NTSTATUS Status;
    PSCSI_PORT_DRIVER_EXTENSION DriverExtension;
    PCWSTR ServicePath;

    DriverExtension = IoGetDriverObjectExtension(DeviceExtension->Common.DeviceObject->DriverObject,
                                                 HwInitData->HwInitialize);

    ServicePath = (DriverExtension != NULL && DriverExtension->RegistryPath.Buffer != NULL)
                      ? DriverExtension->RegistryPath.Buffer
                      : L"<unknown>";

    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_INFO_LEVEL,
               "[scsiport] SpiCreatePortConfig(Adapter=%lu LastAdapter=%lu Bus=%lu InterfaceType=%lu Ranges=%lu ZeroStruct=%d ServiceKey=%p DeviceKey=%p ServicePath=%ws)\n",
               InternalConfigInfo->AdapterNumber,
               InternalConfigInfo->LastAdapterNumber,
               InternalConfigInfo->BusNumber,
               HwInitData->AdapterInterfaceType,
               HwInitData->NumberOfAccessRanges,
               ZeroStruct,
               InternalConfigInfo->ServiceKey,
               InternalConfigInfo->DeviceKey,
               ServicePath);

    /* Zero out the struct if told so */
    if (ZeroStruct)
    {
        /* First zero the portconfig */
        RtlZeroMemory(ConfigInfo, sizeof(PORT_CONFIGURATION_INFORMATION));

        /* Then access ranges */
        RtlZeroMemory(InternalConfigInfo->AccessRanges,
                      HwInitData->NumberOfAccessRanges * sizeof(ACCESS_RANGE));

        /* Initialize the struct */
        ConfigInfo->Length = sizeof(PORT_CONFIGURATION_INFORMATION);
        ConfigInfo->AdapterInterfaceType = HwInitData->AdapterInterfaceType;
        ConfigInfo->InterruptMode = Latched;
        ConfigInfo->DmaChannel = SP_UNINITIALIZED_VALUE;
        ConfigInfo->DmaPort = SP_UNINITIALIZED_VALUE;
        ConfigInfo->DmaChannel2 = SP_UNINITIALIZED_VALUE;
        ConfigInfo->DmaPort2 = SP_UNINITIALIZED_VALUE;
        ConfigInfo->MaximumTransferLength = SP_UNINITIALIZED_VALUE;
        ConfigInfo->NumberOfAccessRanges = HwInitData->NumberOfAccessRanges;
        ConfigInfo->MaximumNumberOfTargets = SCSI_MAXIMUM_TARGETS; // NOTE: Using legacy value.
        ConfigInfo->MaximumNumberOfLogicalUnits = SCSI_MAXIMUM_LOGICAL_UNITS;

        /* Store parameters */
        ConfigInfo->NeedPhysicalAddresses = HwInitData->NeedPhysicalAddresses;
        ConfigInfo->MapBuffers = HwInitData->MapBuffers;
        ConfigInfo->AutoRequestSense = HwInitData->AutoRequestSense;
        ConfigInfo->ReceiveEvent = HwInitData->ReceiveEvent;
        ConfigInfo->TaggedQueuing = HwInitData->TaggedQueuing;
        ConfigInfo->MultipleRequestPerLu = HwInitData->MultipleRequestPerLu;

        /* Get the disk usage */
        DdkConfigInformation = IoGetConfigurationInformation();
        ConfigInfo->AtdiskPrimaryClaimed = DdkConfigInformation->AtDiskPrimaryAddressClaimed;
        ConfigInfo->AtdiskSecondaryClaimed = DdkConfigInformation->AtDiskSecondaryAddressClaimed;

        /* Initiator bus id is not set */
        for (Bus = 0; Bus < RTL_NUMBER_OF(ConfigInfo->InitiatorBusId); Bus++)
        {
            ConfigInfo->InitiatorBusId[Bus] = (CCHAR)SP_UNINITIALIZED_VALUE;
        }
    }

    ConfigInfo->NumberOfPhysicalBreaks = 17;

    /* Clear this information */
    InternalConfigInfo->DisableTaggedQueueing = FALSE;
    InternalConfigInfo->DisableMultipleLun = FALSE;

    /* Store Bus Number */
    ConfigInfo->SystemIoBusNumber = InternalConfigInfo->BusNumber;

    /*
     * Fast path for legacy (non-PCI) miniports on ACPI systems:
     * Do not rely on legacy HARDWARE\DESCRIPTION registry layouts that
     * are absent on ACPI/Q35. Proceed directly to HwFindAdapter and let
     * the miniport probe fixed ISA compatibility ranges.
     */
    if ((ConfigInfo->AdapterInterfaceType != PCIBus))
    {
        PSCSI_PORT_DRIVER_EXTENSION DriverExtensionLocal;
        DriverExtensionLocal = IoGetDriverObjectExtension(DeviceExtension->Common.DeviceObject->DriverObject,
                                                          HwInitData->HwInitialize);
        if (DriverExtensionLocal && DriverExtensionLocal->FirmwareSupportsPnp)
        {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_INFO_LEVEL,
                       "[scsiport] ACPI detected; skipping legacy registry scan for non-PCI miniport.\n");
            return STATUS_SUCCESS;
        }
    }

TryNextAd:

    if (ConfigInfo->AdapterInterfaceType == Internal)
    {
        /* Open registry key for HW database */
        InitializeObjectAttributes(&ObjectAttributes,
                                   DeviceExtension->Common.DeviceObject->DriverObject->HardwareDatabase,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);

        Status = ZwOpenKey(&RootKey,
                           KEY_READ,
                           &ObjectAttributes);

        if (NT_SUCCESS(Status))
        {
            /* Create name for it */
            swprintf(StrBuffer, L"ScsiAdapter\\%lu",
                InternalConfigInfo->AdapterNumber);

            RtlInitUnicodeString(&UnicodeString, StrBuffer);

            /* Open device key */
            InitializeObjectAttributes(&ObjectAttributes,
                                       &UnicodeString,
                                       OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                       RootKey,
                                       NULL);

            Status = ZwOpenKey(&Key,
                               KEY_READ,
                               &ObjectAttributes);

            ZwClose(RootKey);

            if (NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                           DPFLTR_INFO_LEVEL,
                           "[scsiport] Found hardware database entry %ws for Adapter %lu\n",
                           StrBuffer,
                           InternalConfigInfo->AdapterNumber);

                if (InternalConfigInfo->LastAdapterNumber != InternalConfigInfo->AdapterNumber)
                {
                    DPRINT("Hardware info found at %S\n", StrBuffer);

                    /* Parse it */
                    SpiParseDeviceInfo(DeviceExtension,
                                       Key,
                                       ConfigInfo,
                                       InternalConfigInfo,
                                       (PUCHAR)StrBuffer);

                     InternalConfigInfo->BusNumber = 0;
                }
                else
                {
                    /* Try the next adapter */
                    InternalConfigInfo->AdapterNumber++;
                    goto TryNextAd;
                }
            }
            else
            {
                /* Info was not found, exit */
                DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                           DPFLTR_INFO_LEVEL,
                           "[scsiport] ZwOpenKey failed for hardware database entry %ws Status=0x%08lx\n",
                           StrBuffer,
                           Status);
                DPRINT("ZwOpenKey() failed with Status=0x%08X\n", Status);
                return STATUS_DEVICE_DOES_NOT_EXIST;
            }
        }
        else
        {
            DPRINT1("ZwOpenKey() failed with Status=0x%08X\n", Status);
            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_INFO_LEVEL,
                       "[scsiport] ZwOpenKey hardware database failed Status=0x%08lx\n",
                       Status);
        }
    }

    /* Look at device params */
    Key = NULL;
    if (InternalConfigInfo->Parameter)
    {
        ExFreePool(InternalConfigInfo->Parameter);
        InternalConfigInfo->Parameter = NULL;
    }

    if (InternalConfigInfo->ServiceKey != NULL)
    {
        swprintf(DeviceBuffer, L"Device%lu", InternalConfigInfo->AdapterNumber);
        RtlInitUnicodeString(&UnicodeString, DeviceBuffer);

        /* Open the service key */
        InitializeObjectAttributes(&ObjectAttributes,
                                   &UnicodeString,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   InternalConfigInfo->ServiceKey,
                                   NULL);

        Status = ZwOpenKey(&Key,
                           KEY_READ,
                           &ObjectAttributes);

        if (NT_SUCCESS(Status))
        {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_INFO_LEVEL,
                   "[scsiport] Opened Parameters\\%wZ for Adapter %lu (Bus=%lu)\n",
                   &UnicodeString,
                   InternalConfigInfo->AdapterNumber,
                   InternalConfigInfo->BusNumber);
        }
        else
        {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_INFO_LEVEL,
                       "[scsiport] ZwOpenKey failed for Parameters\\%wZ (Adapter %lu) Status=0x%08lx\n",
                       &UnicodeString,
                       InternalConfigInfo->AdapterNumber,
                       Status);
        }
    }

    /* Parse device key */
    if (InternalConfigInfo->DeviceKey != NULL)
    {
        SpiParseDeviceInfo(DeviceExtension,
                           InternalConfigInfo->DeviceKey,
                           ConfigInfo,
                           InternalConfigInfo,
                           (PUCHAR)StrBuffer);
    }

    /* Then parse hw info */
    if (Key != NULL)
    {
        if (InternalConfigInfo->LastAdapterNumber != InternalConfigInfo->AdapterNumber)
        {
            SpiParseDeviceInfo(DeviceExtension,
                               Key,
                               ConfigInfo,
                               InternalConfigInfo,
                               (PUCHAR)StrBuffer);

            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_INFO_LEVEL,
                       "[scsiport] Parsed Parameters\\%wZ BusNumber=%lu AccessRanges=%lu\n",
                       &UnicodeString,
                       InternalConfigInfo->BusNumber,
                       ConfigInfo->NumberOfAccessRanges);

            /* Close the key */
            ZwClose(Key);
        }
        else
        {
            /* Adapter not found, go try the next one */
            InternalConfigInfo->AdapterNumber++;

            /* Close the key */
            ZwClose(Key);

            goto TryNextAd;
        }
    }

    /* Update the last adapter number */
    InternalConfigInfo->LastAdapterNumber = InternalConfigInfo->AdapterNumber;

    /* Do we have this kind of bus at all? */
    Found = FALSE;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_INFO_LEVEL,
               "[scsiport] IoQueryDeviceDescription(InterfaceType=%lu, Bus=%lu)\n",
               HwInitData->AdapterInterfaceType,
               InternalConfigInfo->BusNumber);

    Status = IoQueryDeviceDescription(&HwInitData->AdapterInterfaceType,
                                      &InternalConfigInfo->BusNumber,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL,
                                      SpQueryDeviceCallout,
                                      &Found);

    /* This bus was not found */
    if (!Found)
    {
        INTERFACE_TYPE InterfaceType = Eisa;

        if (HwInitData->AdapterInterfaceType == Isa)
        {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_INFO_LEVEL,
                       "[scsiport] IoQueryDeviceDescription retry (InterfaceType=%lu, Bus=%lu)\n",
                       InterfaceType,
                       InternalConfigInfo->BusNumber);

            Status = IoQueryDeviceDescription(&InterfaceType,
                                              &InternalConfigInfo->BusNumber,
                                              NULL,
                                              NULL,
                                              NULL,
                                              NULL,
                                              SpQueryDeviceCallout,
                                              &Found);

            if (Found)
                return STATUS_SUCCESS;

            return STATUS_DEVICE_DOES_NOT_EXIST;
        }

        /*
         * On ACPI/Uefi boots the legacy HAL callbacks may not expose these
         * controllers. Allow the miniport to proceed even when the query fails.
         */
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_INFO_LEVEL,
                   "[scsiport] IoQueryDeviceDescription returned no match (InterfaceType=%lu Bus=%lu) – continuing\n",
                   HwInitData->AdapterInterfaceType,
                   InternalConfigInfo->BusNumber);
        return STATUS_SUCCESS;
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_INFO_LEVEL,
               "[scsiport] Adapter %lu finalized: Bus=%lu InterruptLevel=%lu/%lu AccessRanges=%lu\n",
               InternalConfigInfo->AdapterNumber,
               ConfigInfo->SystemIoBusNumber,
               ConfigInfo->BusInterruptLevel,
               ConfigInfo->BusInterruptLevel2,
               ConfigInfo->NumberOfAccessRanges);

    return STATUS_SUCCESS;
}

VOID
SpiParseDeviceInfo(IN PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
                   IN HANDLE Key,
                   IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                   IN PCONFIGURATION_INFO InternalConfigInfo,
                   IN PUCHAR Buffer)
{
    PKEY_VALUE_FULL_INFORMATION KeyValueInformation;
    PCM_FULL_RESOURCE_DESCRIPTOR FullResource;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PCM_SCSI_DEVICE_DATA ScsiDeviceData;
    ULONG Length, Count, Dma = 0, Interrupt = 0;
    ULONG Index = 0, RangeCount = 0;
    UNICODE_STRING UnicodeString;
    ANSI_STRING AnsiString;
    NTSTATUS Status = STATUS_SUCCESS;


    KeyValueInformation = (PKEY_VALUE_FULL_INFORMATION) Buffer;

    /* Loop through all values in the device node */
    while(TRUE)
    {
        Status = ZwEnumerateValueKey(Key,
                                     Index,
                                     KeyValueFullInformation,
                                     Buffer,
                                     512,
                                     &Length);

        if (!NT_SUCCESS(Status))
            return;

        Index++;

        /* Length for DWORD is ok? */
        if (KeyValueInformation->Type == REG_DWORD &&
            KeyValueInformation->DataLength != sizeof(ULONG))
        {
            continue;
        }

        /* Get MaximumLogicalUnit */
        if (_wcsnicmp(KeyValueInformation->Name, L"MaximumLogicalUnit",
            KeyValueInformation->NameLength/2) == 0)
        {

            if (KeyValueInformation->Type != REG_DWORD)
            {
                DPRINT("Bad data type for MaximumLogicalUnit\n");
                continue;
            }

            DeviceExtension->MaxLunCount = *((PUCHAR)
                (Buffer + KeyValueInformation->DataOffset));

            /* Check / reset if needed */
            if (DeviceExtension->MaxLunCount > SCSI_MAXIMUM_LOGICAL_UNITS)
                DeviceExtension->MaxLunCount = SCSI_MAXIMUM_LOGICAL_UNITS;

            DPRINT("MaximumLogicalUnit = %d\n", DeviceExtension->MaxLunCount);
        }

        /* Get InitiatorTargetId */
        if (_wcsnicmp(KeyValueInformation->Name, L"InitiatorTargetId",
            KeyValueInformation->NameLength / 2) == 0)
        {

            if (KeyValueInformation->Type != REG_DWORD)
            {
                DPRINT("Bad data type for InitiatorTargetId\n");
                continue;
            }

            ConfigInfo->InitiatorBusId[0] = *((PUCHAR)
                (Buffer + KeyValueInformation->DataOffset));

            /* Check / reset if needed */
            if (ConfigInfo->InitiatorBusId[0] > ConfigInfo->MaximumNumberOfTargets - 1)
                ConfigInfo->InitiatorBusId[0] = (CCHAR)SP_UNINITIALIZED_VALUE;

            DPRINT("InitiatorTargetId = %d\n", ConfigInfo->InitiatorBusId[0]);
        }

        /* Get ScsiDebug */
        if (_wcsnicmp(KeyValueInformation->Name, L"ScsiDebug",
            KeyValueInformation->NameLength/2) == 0)
        {
            DPRINT("ScsiDebug key not supported\n");
        }

        /* Check for a breakpoint */
        if (_wcsnicmp(KeyValueInformation->Name, L"BreakPointOnEntry",
            KeyValueInformation->NameLength/2) == 0)
        {
            DPRINT1("Breakpoint on entry requested!\n");
            DbgBreakPoint();
        }

        /* Get DisableSynchronousTransfers */
        if (_wcsnicmp(KeyValueInformation->Name, L"DisableSynchronousTransfers",
            KeyValueInformation->NameLength/2) == 0)
        {
            DeviceExtension->SrbFlags |= SRB_FLAGS_DISABLE_SYNCH_TRANSFER;
            DPRINT("Synch transfers disabled\n");
        }

        /* Get DisableDisconnects */
        if (_wcsnicmp(KeyValueInformation->Name, L"DisableDisconnects",
            KeyValueInformation->NameLength/2) == 0)
        {
            DeviceExtension->SrbFlags |= SRB_FLAGS_DISABLE_DISCONNECT;
            DPRINT("Disconnects disabled\n");
        }

        /* Get DisableTaggedQueuing */
        if (_wcsnicmp(KeyValueInformation->Name, L"DisableTaggedQueuing",
            KeyValueInformation->NameLength/2) == 0)
        {
            InternalConfigInfo->DisableTaggedQueueing = TRUE;
            DPRINT("Tagged queueing disabled\n");
        }

        /* Get DisableMultipleRequests */
        if (_wcsnicmp(KeyValueInformation->Name, L"DisableMultipleRequests",
            KeyValueInformation->NameLength/2) == 0)
        {
            InternalConfigInfo->DisableMultipleLun = TRUE;
            DPRINT("Multiple requests disabled\n");
        }

        /* Get DriverParameters */
        if (_wcsnicmp(KeyValueInformation->Name, L"DriverParameters",
            KeyValueInformation->NameLength/2) == 0)
        {
            /* Skip if nothing */
            if (KeyValueInformation->DataLength == 0)
                continue;

            /* If there was something previously allocated - free it */
            if (InternalConfigInfo->Parameter != NULL)
                ExFreePool(InternalConfigInfo->Parameter);

            /* Allocate it */
            InternalConfigInfo->Parameter = ExAllocatePoolWithTag(NonPagedPool,
                                                           KeyValueInformation->DataLength, TAG_SCSIPORT);

            if (InternalConfigInfo->Parameter != NULL)
            {
                if (KeyValueInformation->Type != REG_SZ)
                {
                    /* Just copy */
                    RtlCopyMemory(
                        InternalConfigInfo->Parameter,
                        (PCCHAR)KeyValueInformation + KeyValueInformation->DataOffset,
                        KeyValueInformation->DataLength);
                }
                else
                {
                    /* If it's a unicode string, convert it to ansi */
                    UnicodeString.Length = (USHORT)KeyValueInformation->DataLength;
                    UnicodeString.MaximumLength = (USHORT)KeyValueInformation->DataLength;
                    UnicodeString.Buffer =
                        (PWSTR)((PCCHAR)KeyValueInformation + KeyValueInformation->DataOffset);

                    AnsiString.Length = 0;
                    AnsiString.MaximumLength = (USHORT)KeyValueInformation->DataLength;
                    AnsiString.Buffer = (PCHAR)InternalConfigInfo->Parameter;

                    Status = RtlUnicodeStringToAnsiString(&AnsiString,
                                                          &UnicodeString,
                                                          FALSE);

                    /* In case of error, free the allocated space */
                    if (!NT_SUCCESS(Status))
                    {
                        ExFreePool(InternalConfigInfo->Parameter);
                        InternalConfigInfo->Parameter = NULL;
                    }

                }
            }

            DPRINT("Found driver parameter\n");
        }

        /* Get MaximumSGList */
        if (_wcsnicmp(KeyValueInformation->Name, L"MaximumSGList",
            KeyValueInformation->NameLength/2) == 0)
        {
            if (KeyValueInformation->Type != REG_DWORD)
            {
                DPRINT("Bad data type for MaximumSGList\n");
                continue;
            }

            ConfigInfo->NumberOfPhysicalBreaks = *((PUCHAR)(Buffer + KeyValueInformation->DataOffset));

            /* Check / fix */
            if (ConfigInfo->NumberOfPhysicalBreaks > SCSI_MAXIMUM_PHYSICAL_BREAKS)
            {
                ConfigInfo->NumberOfPhysicalBreaks = SCSI_MAXIMUM_PHYSICAL_BREAKS;
            }
            else if (ConfigInfo->NumberOfPhysicalBreaks < SCSI_MINIMUM_PHYSICAL_BREAKS)
            {
                ConfigInfo->NumberOfPhysicalBreaks = SCSI_MINIMUM_PHYSICAL_BREAKS;
            }

            DPRINT("MaximumSGList = %d\n", ConfigInfo->NumberOfPhysicalBreaks);
        }

        /* Get NumberOfRequests */
        if (_wcsnicmp(KeyValueInformation->Name, L"NumberOfRequests",
            KeyValueInformation->NameLength/2) == 0)
        {
            if (KeyValueInformation->Type != REG_DWORD)
            {
                DPRINT("NumberOfRequests has wrong data type\n");
                continue;
            }

            DeviceExtension->RequestsNumber = *((PUCHAR)(Buffer + KeyValueInformation->DataOffset));

            /* Check / fix */
            if (DeviceExtension->RequestsNumber < 16)
            {
                DeviceExtension->RequestsNumber = 16;
            }
            else if (DeviceExtension->RequestsNumber > 512)
            {
                DeviceExtension->RequestsNumber = 512;
            }

            DPRINT("Number Of Requests = %d\n", DeviceExtension->RequestsNumber);
        }

        /* Get resource list */
        if (_wcsnicmp(KeyValueInformation->Name, L"ResourceList",
                KeyValueInformation->NameLength/2) == 0 ||
            _wcsnicmp(KeyValueInformation->Name, L"Configuration Data",
                KeyValueInformation->NameLength/2) == 0 )
        {
            if (KeyValueInformation->Type != REG_FULL_RESOURCE_DESCRIPTOR ||
                KeyValueInformation->DataLength < sizeof(REG_FULL_RESOURCE_DESCRIPTOR))
            {
                DPRINT("Bad data type for ResourceList\n");
                continue;
            }
            else
            {
                DPRINT("Found ResourceList\n");
            }

            FullResource = (PCM_FULL_RESOURCE_DESCRIPTOR)(Buffer + KeyValueInformation->DataOffset);

            /* Copy some info from it */
            InternalConfigInfo->BusNumber = FullResource->BusNumber;
            ConfigInfo->SystemIoBusNumber = FullResource->BusNumber;

            DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                       DPFLTR_INFO_LEVEL,
                       "[scsiport] ResourceList Bus=%lu Count=%lu Interface=%lu\n",
                       FullResource->BusNumber,
                       FullResource->PartialResourceList.Count,
                       ConfigInfo->AdapterInterfaceType);

            /* Loop through it */
            for (Count = 0; Count < FullResource->PartialResourceList.Count; Count++)
            {
                /* Get partial descriptor */
                PartialDescriptor =
                    &FullResource->PartialResourceList.PartialDescriptors[Count];

                /* Check datalength */
                if ((ULONG)((PCHAR)(PartialDescriptor + 1) -
                    (PCHAR)FullResource) > KeyValueInformation->DataLength)
                {
                    DPRINT("Resource data is of incorrect size\n");
                    break;
                }

                switch (PartialDescriptor->Type)
                {
                case CmResourceTypePort:
                    if (RangeCount >= ConfigInfo->NumberOfAccessRanges)
                    {
                        DPRINT("Too many access ranges\n");
                        continue;
                    }

                    InternalConfigInfo->AccessRanges[RangeCount].RangeInMemory = FALSE;
                    InternalConfigInfo->AccessRanges[RangeCount].RangeStart = PartialDescriptor->u.Port.Start;
                    InternalConfigInfo->AccessRanges[RangeCount].RangeLength = PartialDescriptor->u.Port.Length;

                    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                               DPFLTR_INFO_LEVEL,
                               "[scsiport] AccessRange[%lu] IO Start=0x%I64x Length=0x%lx\n",
                               RangeCount,
                               PartialDescriptor->u.Port.Start.QuadPart,
                               PartialDescriptor->u.Port.Length);

                    RangeCount++;

                    break;

                case CmResourceTypeMemory:
                    if (RangeCount >= ConfigInfo->NumberOfAccessRanges)
                    {
                        DPRINT("Too many access ranges\n");
                        continue;
                    }

                    InternalConfigInfo->AccessRanges[RangeCount].RangeInMemory = TRUE;
                    InternalConfigInfo->AccessRanges[RangeCount].RangeStart = PartialDescriptor->u.Memory.Start;
                    InternalConfigInfo->AccessRanges[RangeCount].RangeLength = PartialDescriptor->u.Memory.Length;

                    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                               DPFLTR_INFO_LEVEL,
                               "[scsiport] AccessRange[%lu] MEM Start=0x%I64x Length=0x%lx\n",
                               RangeCount,
                               PartialDescriptor->u.Memory.Start.QuadPart,
                               PartialDescriptor->u.Memory.Length);

                    RangeCount++;

                    break;

                case CmResourceTypeInterrupt:

                    if (Interrupt == 0)
                    {
                        ConfigInfo->BusInterruptLevel =
                            PartialDescriptor->u.Interrupt.Level;

                        ConfigInfo->BusInterruptVector =
                            PartialDescriptor->u.Interrupt.Vector;

                        ConfigInfo->InterruptMode = (PartialDescriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive;

                        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                                   DPFLTR_INFO_LEVEL,
                                   "[scsiport] Interrupt[0] Level=%lu Vector=%lu Flags=0x%lx\n",
                                   PartialDescriptor->u.Interrupt.Level,
                                   PartialDescriptor->u.Interrupt.Vector,
                                   PartialDescriptor->Flags);
                    }
                    else if (Interrupt == 1)
                    {
                        ConfigInfo->BusInterruptLevel2 =
                        PartialDescriptor->u.Interrupt.Level;

                        ConfigInfo->BusInterruptVector2 =
                        PartialDescriptor->u.Interrupt.Vector;

                        ConfigInfo->InterruptMode2 = (PartialDescriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive;

                        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                                   DPFLTR_INFO_LEVEL,
                                   "[scsiport] Interrupt[1] Level=%lu Vector=%lu Flags=0x%lx\n",
                                   PartialDescriptor->u.Interrupt.Level,
                                   PartialDescriptor->u.Interrupt.Vector,
                                   PartialDescriptor->Flags);
                    }

                    Interrupt++;
                    break;

                case CmResourceTypeDma:

                    if (Dma == 0)
                    {
                        ConfigInfo->DmaChannel = PartialDescriptor->u.Dma.Channel;
                        ConfigInfo->DmaPort = PartialDescriptor->u.Dma.Port;

                        if (PartialDescriptor->Flags & CM_RESOURCE_DMA_8)
                            ConfigInfo->DmaWidth = Width8Bits;
                        else if ((PartialDescriptor->Flags & CM_RESOURCE_DMA_16) ||
                                 (PartialDescriptor->Flags & CM_RESOURCE_DMA_8_AND_16)) //???
                            ConfigInfo->DmaWidth = Width16Bits;
                        else if (PartialDescriptor->Flags & CM_RESOURCE_DMA_32)
                            ConfigInfo->DmaWidth = Width32Bits;

                        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                                   DPFLTR_INFO_LEVEL,
                                   "[scsiport] DMA[0] Channel=%lu Port=0x%lx Flags=0x%lx Width=%lu\n",
                                   PartialDescriptor->u.Dma.Channel,
                                   PartialDescriptor->u.Dma.Port,
                                   PartialDescriptor->Flags,
                                   ConfigInfo->DmaWidth);
                    }
                    else if (Dma == 1)
                    {
                        ConfigInfo->DmaChannel2 = PartialDescriptor->u.Dma.Channel;
                        ConfigInfo->DmaPort2 = PartialDescriptor->u.Dma.Port;

                        if (PartialDescriptor->Flags & CM_RESOURCE_DMA_8)
                            ConfigInfo->DmaWidth2 = Width8Bits;
                        else if ((PartialDescriptor->Flags & CM_RESOURCE_DMA_16) ||
                                 (PartialDescriptor->Flags & CM_RESOURCE_DMA_8_AND_16)) //???
                            ConfigInfo->DmaWidth2 = Width16Bits;
                        else if (PartialDescriptor->Flags & CM_RESOURCE_DMA_32)
                            ConfigInfo->DmaWidth2 = Width32Bits;

                        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                                   DPFLTR_INFO_LEVEL,
                                   "[scsiport] DMA[1] Channel=%lu Port=0x%lx Flags=0x%lx Width=%lu\n",
                                   PartialDescriptor->u.Dma.Channel,
                                   PartialDescriptor->u.Dma.Port,
                                   PartialDescriptor->Flags,
                                   ConfigInfo->DmaWidth2);
                    }

                    Dma++;
                    break;

                case CmResourceTypeDeviceSpecific:
                    if (PartialDescriptor->u.DeviceSpecificData.DataSize <
                        sizeof(CM_SCSI_DEVICE_DATA) ||
                        (PCHAR) (PartialDescriptor + 1) - (PCHAR)FullResource +
                        PartialDescriptor->u.DeviceSpecificData.DataSize >
                        KeyValueInformation->DataLength)
                    {
                        DPRINT("Resource data length is incorrect");
                        break;
                    }

                    /* Set only one field from it */
                    ScsiDeviceData = (PCM_SCSI_DEVICE_DATA) (PartialDescriptor+1);
                    ConfigInfo->InitiatorBusId[0] = ScsiDeviceData->HostIdentifier;
                    break;
                }
            }
        }
    }
}

NTSTATUS
NTAPI
SpQueryDeviceCallout(IN PVOID  Context,
                     IN PUNICODE_STRING  PathName,
                     IN INTERFACE_TYPE  BusType,
                     IN ULONG  BusNumber,
                     IN PKEY_VALUE_FULL_INFORMATION  *BusInformation,
                     IN CONFIGURATION_TYPE  ControllerType,
                     IN ULONG  ControllerNumber,
                     IN PKEY_VALUE_FULL_INFORMATION  *ControllerInformation,
                     IN CONFIGURATION_TYPE  PeripheralType,
                     IN ULONG  PeripheralNumber,
                     IN PKEY_VALUE_FULL_INFORMATION  *PeripheralInformation)
{
    PBOOLEAN Found = (PBOOLEAN)Context;
    /* We just set our Found variable to TRUE */

    *Found = TRUE;
    return STATUS_SUCCESS;
}

IO_ALLOCATION_ACTION
NTAPI
ScsiPortAllocateAdapterChannel(IN PDEVICE_OBJECT DeviceObject,
                               IN PIRP Irp,
                               IN PVOID MapRegisterBase,
                               IN PVOID Context)
{
    KIRQL Irql;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    /* Guard access with the spinlock */
    KeAcquireSpinLock(&DeviceExtension->SpinLock, &Irql);

    /* Save MapRegisterBase we've got here */
    DeviceExtension->MapRegisterBase = MapRegisterBase;

    /* Start pending request */
    PKINTERRUPT interruptObject = SppGetPrimaryInterruptObject(DeviceExtension);

    if (interruptObject)
    {
        KeSynchronizeExecution(interruptObject,
                               ScsiPortStartPacket,
                               DeviceObject);
    }
    else
    {
        ScsiPortStartPacket(DeviceObject);
    }

    /* Release spinlock we took */
    KeReleaseSpinLock(&DeviceExtension->SpinLock, Irql);

    return KeepObject;
}

#undef ScsiPortConvertPhysicalAddressToUlong
/*
 * @implemented
 */
ULONG NTAPI
ScsiPortConvertPhysicalAddressToUlong(IN SCSI_PHYSICAL_ADDRESS Address)
{
  DPRINT("ScsiPortConvertPhysicalAddressToUlong()\n");
  return(Address.u.LowPart);
}
