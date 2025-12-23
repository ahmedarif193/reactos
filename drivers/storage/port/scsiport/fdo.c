/*
 * PROJECT:     ReactOS Storage Stack / SCSIPORT storage port library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Adapter device object (FDO) support routines
 * COPYRIGHT:   Eric Kohl (eric.kohl@reactos.org)
 *              Aleksey Bragin (aleksey@reactos.org)
 *              2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include "scsiport.h"

#define NDEBUG
#include <debug.h>

typedef NTSTATUS (NTAPI *PFN_IO_CONNECT_INTERRUPT_EX)(PIO_CONNECT_INTERRUPT_PARAMETERS Parameters);
typedef VOID (NTAPI *PFN_IO_DISCONNECT_INTERRUPT_EX)(PIO_DISCONNECT_INTERRUPT_PARAMETERS Parameters);

static PFN_IO_CONNECT_INTERRUPT_EX SppIoConnectInterruptExPointer = NULL;
static PFN_IO_DISCONNECT_INTERRUPT_EX SppIoDisconnectInterruptExPointer = NULL;
static BOOLEAN SppInterruptApiInitialized = FALSE;

static
VOID
SppEnsureInterruptApi(VOID)
{
    if (SppInterruptApiInitialized)
        return;

    UNICODE_STRING routineName;

    RtlInitUnicodeString(&routineName, L"IoConnectInterruptEx");
    SppIoConnectInterruptExPointer =
        (PFN_IO_CONNECT_INTERRUPT_EX)MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"IoDisconnectInterruptEx");
    SppIoDisconnectInterruptExPointer =
        (PFN_IO_DISCONNECT_INTERRUPT_EX)MmGetSystemRoutineAddress(&routineName);

    SppInterruptApiInitialized = TRUE;
}

static
VOID
FdoDumpResourceList(
    _In_ PCSTR ListName,
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    if (!ResourceList)
    {
        DPRINT1("%s: (null)\n", ListName);
        return;
    }

    for (ULONG i = 0; i < ResourceList->Count; i++)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR full = &ResourceList->List[i];
        DPRINT1("%s[%lu]: Interface %lu Bus %lu Count %lu\n",
                ListName,
                i,
                full->InterfaceType,
                full->BusNumber,
                full->PartialResourceList.Count);

        for (ULONG j = 0; j < full->PartialResourceList.Count; j++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR partial =
                &full->PartialResourceList.PartialDescriptors[j];

            switch (partial->Type)
            {
                case CmResourceTypePort:
                    DPRINT1("  PORT  start=0x%I64x len=0x%lx flags=0x%x\n",
                            partial->u.Port.Start.QuadPart,
                            partial->u.Port.Length,
                            partial->Flags);
                    break;
                case CmResourceTypeMemory:
                    DPRINT1("  MEM   start=0x%I64x len=0x%lx flags=0x%x\n",
                            partial->u.Memory.Start.QuadPart,
                            partial->u.Memory.Length,
                            partial->Flags);
                    break;
                case CmResourceTypeInterrupt:
                    DPRINT1("  IRQ   level=%lu vector=%lu affinity=0x%I64x flags=0x%x\n",
                            partial->u.Interrupt.Level,
                            partial->u.Interrupt.Vector,
                            partial->u.Interrupt.Affinity,
                            partial->Flags);
                    break;
                case CmResourceTypeDma:
                    DPRINT1("  DMA   channel=%lu port=%lu\n",
                            partial->u.Dma.Channel,
                            partial->u.Dma.Port);
                    break;
                default:
                    DPRINT1("  TYPE %u raw[0]=0x%I64x raw[1]=0x%I64x\n",
                            partial->Type,
                            partial->u.DevicePrivate.Data[0],
                            partial->u.DevicePrivate.Data[1]);
                    break;
            }
        }
    }
}


static
NTSTATUS
FdoStartDeviceWithResources(
    _Inout_ PSCSI_PORT_DEVICE_EXTENSION PortExtension,
    _In_opt_ PCM_RESOURCE_LIST RawResources,
    _In_opt_ PCM_RESOURCE_LIST TranslatedResources)
{
    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[SCSIPORT] FdoStartDeviceWithResources enter PortExt=%p Raw=%p Xlat=%p\n",
               PortExtension,
               RawResources,
               TranslatedResources);
    PHW_INITIALIZATION_DATA hwInit;
    CONFIGURATION_INFO configInfo;
    PPORT_CONFIGURATION_INFORMATION portConfig = NULL;
    SIZE_T headerSize, rangesBytes, tailBytes, allocSize;
    PUCHAR base;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG result;
    BOOLEAN again = FALSE;
    UCHAR parseBuffer[512];

    UNREFERENCED_PARAMETER(RawResources);

    ASSERT(PortExtension->DriverExtension != NULL);
    hwInit = &PortExtension->DriverExtension->HwInitializationData;

    PortExtension->InterruptMessageBased = FALSE;
    PortExtension->InterruptCount = 0;

    if (TranslatedResources)
    {
        for (ULONG resIndex = 0; resIndex < TranslatedResources->Count; resIndex++)
        {
            PCM_FULL_RESOURCE_DESCRIPTOR fullDescriptor = &TranslatedResources->List[resIndex];

            for (ULONG partIndex = 0;
                 partIndex < fullDescriptor->PartialResourceList.Count;
                 partIndex++)
            {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR partial =
                    &fullDescriptor->PartialResourceList.PartialDescriptors[partIndex];

                if (partial->Type != CmResourceTypeInterrupt)
                    continue;

                if (partial->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                {
                    USHORT messageCount = 1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
                    messageCount = partial->u.MessageInterrupt.Raw.MessageCount;
#endif
                    if (messageCount == 0)
                        messageCount = 1;

                    PortExtension->InterruptMessageBased = TRUE;
                    PortExtension->InterruptCount = messageCount;
                    goto InterruptScanDone;
                }
            }
        }
    }

InterruptScanDone:

    RtlZeroMemory(&configInfo, sizeof(configInfo));
    configInfo.LastAdapterNumber = SP_UNINITIALIZED_VALUE;

    if (TranslatedResources && TranslatedResources->Count > 0)
    {
        configInfo.BusNumber = TranslatedResources->List[0].BusNumber;
    }

    if (hwInit->NumberOfAccessRanges != 0)
    {
        SIZE_T accessBytes = (SIZE_T)hwInit->NumberOfAccessRanges * sizeof(ACCESS_RANGE);
        configInfo.AccessRanges = ExAllocatePoolZero(PagedPool, accessBytes, TAG_SCSIPORT);
        if (!configInfo.AccessRanges)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    headerSize = ALIGN_UP(sizeof(PORT_CONFIGURATION_INFORMATION), sizeof(LONGLONG));
    rangesBytes = (SIZE_T)hwInit->NumberOfAccessRanges * sizeof(ACCESS_RANGE);
    tailBytes = (rangesBytes != 0) ? ALIGN_UP(rangesBytes, sizeof(LONGLONG)) : 0;
    allocSize = headerSize + tailBytes;

    portConfig = ExAllocatePoolZero(NonPagedPool, allocSize, TAG_SCSIPORT);
    if (!portConfig)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    base = (PUCHAR)portConfig + headerSize;
    portConfig->AccessRanges = (ACCESS_RANGE (*)[])base;

    portConfig->Length = sizeof(PORT_CONFIGURATION_INFORMATION);
    portConfig->AdapterInterfaceType = hwInit->AdapterInterfaceType;
    portConfig->InterruptMode = Latched;
    portConfig->InterruptMode2 = Latched;
    portConfig->MaximumTransferLength = SP_UNINITIALIZED_VALUE;
    portConfig->NumberOfPhysicalBreaks = 17;
    portConfig->DmaChannel = SP_UNINITIALIZED_VALUE;
    portConfig->DmaPort = SP_UNINITIALIZED_VALUE;
    portConfig->DmaChannel2 = SP_UNINITIALIZED_VALUE;
    portConfig->DmaPort2 = SP_UNINITIALIZED_VALUE;
    portConfig->NumberOfAccessRanges = hwInit->NumberOfAccessRanges;
    portConfig->MaximumNumberOfTargets = SCSI_MAXIMUM_TARGETS;
    portConfig->SpecificLuExtensionSize = PortExtension->LunExtensionSize;
    portConfig->SrbExtensionSize = PortExtension->SrbExtensionSize;
    portConfig->NeedPhysicalAddresses = hwInit->NeedPhysicalAddresses;
    portConfig->MapBuffers = hwInit->MapBuffers;
    portConfig->AutoRequestSense = hwInit->AutoRequestSense;
    portConfig->ReceiveEvent = hwInit->ReceiveEvent;
    portConfig->TaggedQueuing = hwInit->TaggedQueuing;
    portConfig->MultipleRequestPerLu = hwInit->MultipleRequestPerLu;

    for (ULONG i = 0; i < RTL_NUMBER_OF(portConfig->InitiatorBusId); i++)
    {
        portConfig->InitiatorBusId[i] = (CCHAR)SP_UNINITIALIZED_VALUE;
    }

    PortExtension->PortConfig = portConfig;

    if (TranslatedResources && TranslatedResources->Count > 0)
    {
        portConfig->AdapterInterfaceType = TranslatedResources->List[0].InterfaceType;
        portConfig->SystemIoBusNumber = TranslatedResources->List[0].BusNumber;

        for (ULONG index = 0; index < TranslatedResources->Count; index++)
        {
            SpiResourceToConfig(hwInit, &TranslatedResources->List[index], portConfig);
        }
    }
    else
    {
        portConfig->SystemIoBusNumber = configInfo.BusNumber;
    }

    SpiInitOpenKeys(&configInfo, PortExtension->DriverExtension);

    if (configInfo.DeviceKey)
    {
        RtlZeroMemory(parseBuffer, sizeof(parseBuffer));
        SpiParseDeviceInfo(PortExtension,
                           configInfo.DeviceKey,
                           portConfig,
                           &configInfo,
                           parseBuffer);
    }

    if (configInfo.ServiceKey && configInfo.ServiceKey != configInfo.DeviceKey)
    {
        RtlZeroMemory(parseBuffer, sizeof(parseBuffer));
        SpiParseDeviceInfo(PortExtension,
                           configInfo.ServiceKey,
                           portConfig,
                           &configInfo,
                           parseBuffer);
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[SCSIPORT] FdoStartDevice: calling HwFindAdapter Interface=%lu Bus=%lu AccessRanges=%lu\n",
               portConfig->AdapterInterfaceType,
               portConfig->SystemIoBusNumber,
               portConfig->NumberOfAccessRanges);

    result = hwInit->HwFindAdapter(&PortExtension->MiniPortDeviceExtension,
                                   PortExtension->DriverExtension->HwContext,
                                   NULL,
                                   configInfo.Parameter,
                                   portConfig,
                                   &again);

    PCSTR resultText;
    switch (result)
    {
        case SP_RETURN_NOT_FOUND:
            resultText = "SP_RETURN_NOT_FOUND";
            break;
        case SP_RETURN_ERROR:
            resultText = "SP_RETURN_ERROR";
            break;
        case SP_RETURN_BAD_CONFIG:
            resultText = "SP_RETURN_BAD_CONFIG";
            break;
        case SP_RETURN_FOUND:
            resultText = "SP_RETURN_FOUND";
            break;
        default:
            resultText = "SP_RETURN_*UNKNOWN*";
            break;
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[SCSIPORT] FdoStartDevice: HwFindAdapter result=%lu (%s) again=%lu\n",
               result,
               resultText,
               again);

    if (PortExtension->MapRegisterBase)
    {
        ExFreePool(PortExtension->MapRegisterBase);
        PortExtension->MapRegisterBase = NULL;
    }

    if (result != SP_RETURN_FOUND)
    {
        status = (result == SP_RETURN_NOT_FOUND) ? STATUS_DEVICE_DOES_NOT_EXIST
                                                 : STATUS_INTERNAL_ERROR;
        goto Cleanup;
    }

    if (!PortExtension->NonCachedExtension &&
        portConfig->SrbExtensionSize != PortExtension->SrbExtensionSize)
    {
        PortExtension->SrbExtensionSize = ALIGN_UP(portConfig->SrbExtensionSize, INT64);
    }

    if (portConfig->SpecificLuExtensionSize != PortExtension->LunExtensionSize)
    {
        PortExtension->LunExtensionSize = portConfig->SpecificLuExtensionSize;
    }

    if (PortExtension->PortNumber == SP_UNINITIALIZED_VALUE)
    {
        LONG nextPort = InterlockedIncrement(&PortExtension->DriverExtension->NextPortNumber) - 1;
        PortExtension->PortNumber = (ULONG)nextPort;
    }

    if (portConfig->MaximumNumberOfTargets > SCSI_MAXIMUM_TARGETS_PER_BUS)
    {
        PortExtension->MaxTargedIds = SCSI_MAXIMUM_TARGETS_PER_BUS;
    }
    else
    {
        PortExtension->MaxTargedIds = portConfig->MaximumNumberOfTargets;
    }

    PortExtension->NumberOfBuses = portConfig->NumberOfBuses ? portConfig->NumberOfBuses : 1;
    portConfig->NumberOfBuses = PortExtension->NumberOfBuses;
    PortExtension->CachesData = portConfig->CachesData;
    PortExtension->ReceiveEvent = portConfig->ReceiveEvent;
    PortExtension->SupportsTaggedQueuing = portConfig->TaggedQueuing;
    PortExtension->MultipleReqsPerLun = portConfig->MultipleRequestPerLu;

    if (PortExtension->Buses)
    {
        ExFreePoolWithTag(PortExtension->Buses, TAG_SCSIPORT);
        PortExtension->Buses = NULL;
    }

    SIZE_T busConfigSize = PortExtension->NumberOfBuses * sizeof(*PortExtension->Buses);
    PortExtension->Buses = ExAllocatePoolZero(NonPagedPool, busConfigSize, TAG_SCSIPORT);
    if (!PortExtension->Buses)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    for (UINT8 pathId = 0; pathId < PortExtension->NumberOfBuses; pathId++)
    {
        PortExtension->Buses[pathId].BusIdentifier = portConfig->InitiatorBusId[pathId];
        InitializeListHead(&PortExtension->Buses[pathId].LunsListHead);
    }

    if (configInfo.DisableMultipleLun)
    {
        PortExtension->MultipleReqsPerLun = portConfig->MultipleRequestPerLu = FALSE;
    }

    if (configInfo.DisableTaggedQueueing)
    {
        PortExtension->SupportsTaggedQueuing = portConfig->TaggedQueuing = FALSE;
    }

    PortExtension->NeedSrbDataAlloc =
        (PortExtension->SupportsTaggedQueuing || PortExtension->MultipleReqsPerLun);

    PortExtension->MapBuffers = portConfig->MapBuffers;
    PortExtension->PortCapabilities.AdapterUsesPio = portConfig->MapBuffers;

    if (portConfig->DmaChannel != SP_UNINITIALIZED_VALUE || portConfig->Master)
    {
        status = SpiEnsureAdapterObject(PortExtension, portConfig);
        if (!NT_SUCCESS(status))
        {
            goto Cleanup;
        }
    }

    if (PortExtension->SrbExtensionBuffer == NULL &&
        (PortExtension->SrbExtensionSize != 0 || portConfig->AutoRequestSense))
    {
        PortExtension->SupportsAutoSense = portConfig->AutoRequestSense;
        PortExtension->NeedSrbExtensionAlloc = TRUE;

        status = SpiAllocateCommonBuffer(PortExtension, portConfig, 0);
        if (!NT_SUCCESS(status))
        {
            goto Cleanup;
        }
    }

    if (PortExtension->NeedSrbDataAlloc)
    {
        ULONG count = PortExtension->SrbDataCount != 0 ? PortExtension->SrbDataCount
                                                      : PortExtension->RequestsNumber * 2;
        PSCSI_REQUEST_BLOCK_INFO srbData = ExAllocatePoolWithTag(NonPagedPool,
                                                                 count * sizeof(SCSI_REQUEST_BLOCK_INFO),
                                                                 TAG_SCSIPORT);
        if (!srbData)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlZeroMemory(srbData, count * sizeof(SCSI_REQUEST_BLOCK_INFO));

        PortExtension->SrbInfo = srbData;
        PortExtension->FreeSrbInfo = srbData;
        PortExtension->SrbDataCount = count;

        while (count > 0)
        {
            srbData->Requests.Flink = (PLIST_ENTRY)(srbData + 1);
            srbData++;
            count--;
        }

        srbData--;
        srbData->Requests.Flink = NULL;
    }

    RtlZeroMemory(&PortExtension->PortCapabilities, sizeof(PortExtension->PortCapabilities));
    PortExtension->PortCapabilities.Length = sizeof(IO_SCSI_CAPABILITIES);
    PortExtension->PortCapabilities.MaximumTransferLength = portConfig->MaximumTransferLength;

    if (portConfig->ReceiveEvent)
    {
        PortExtension->PortCapabilities.SupportedAsynchronousEvents |= SRBEV_SCSI_ASYNC_NOTIFICATION;
    }

    PortExtension->PortCapabilities.TaggedQueuing = PortExtension->SupportsTaggedQueuing;
    PortExtension->PortCapabilities.AdapterScansDown = portConfig->AdapterScansDown;

    if (portConfig->AlignmentMask > PortExtension->Common.DeviceObject->AlignmentRequirement)
    {
        PortExtension->Common.DeviceObject->AlignmentRequirement = portConfig->AlignmentMask;
    }

    PortExtension->PortCapabilities.AlignmentMask = PortExtension->Common.DeviceObject->AlignmentRequirement;

    if (PortExtension->PortCapabilities.MaximumPhysicalPages == 0)
    {
        PortExtension->PortCapabilities.MaximumPhysicalPages =
            BYTES_TO_PAGES(PortExtension->PortCapabilities.MaximumTransferLength);

        if (portConfig->NumberOfPhysicalBreaks < PortExtension->PortCapabilities.MaximumPhysicalPages)
        {
            PortExtension->PortCapabilities.MaximumPhysicalPages = portConfig->NumberOfPhysicalBreaks;
        }
    }

    status = FdoCallHWInitialize(PortExtension);
    if (!NT_SUCCESS(status))
    {
        goto Cleanup;
    }

    status = FdoStartAdapter(PortExtension);
    if (!NT_SUCCESS(status))
    {
        goto Cleanup;
    }

    FdoScanAdapter(PortExtension);

    if (PortExtension->PhysicalDeviceObject != NULL)
    {
        IoInvalidateDeviceRelations(PortExtension->PhysicalDeviceObject, BusRelations);
    }

Cleanup:
    if (!NT_SUCCESS(status))
    {
        FdoRemoveAdapter(PortExtension, FALSE);
    }

    if (configInfo.Parameter)
    {
        ExFreePool(configInfo.Parameter);
    }

    if (configInfo.AccessRanges)
    {
        ExFreePool(configInfo.AccessRanges);
    }

    if (configInfo.DeviceKey)
        ZwClose(configInfo.DeviceKey);

    if (configInfo.ServiceKey && configInfo.ServiceKey != configInfo.DeviceKey)
        ZwClose(configInfo.ServiceKey);

    if (configInfo.BusKey)
        ZwClose(configInfo.BusKey);

    if (!NT_SUCCESS(status))
    {
        PortExtension->PortConfig = NULL;
    }

    return status;
}


static
NTSTATUS
FdoSendInquiry(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpStack;
    KEVENT Event;
    KIRQL Irql;
    PIRP Irp;
    NTSTATUS Status;
    PINQUIRYDATA InquiryBuffer;
    PSENSE_DATA SenseBuffer;
    BOOLEAN KeepTrying = TRUE;
    ULONG RetryCount = 0;
    SCSI_REQUEST_BLOCK Srb;
    PCDB Cdb;

    PSCSI_PORT_LUN_EXTENSION LunExtension = DeviceObject->DeviceExtension;
    PSCSI_PORT_DEVICE_EXTENSION DeviceExtension =
        LunExtension->Common.LowerDevice->DeviceExtension;

    DPRINT1("FdoSendInquiry: Path %u Target %u Lun %u\n",
            LunExtension->PathId,
            LunExtension->TargetId,
            LunExtension->Lun);

    InquiryBuffer = ExAllocatePoolWithTag(NonPagedPool, INQUIRYDATABUFFERSIZE, TAG_SCSIPORT);
    if (InquiryBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    SenseBuffer = ExAllocatePoolWithTag(NonPagedPool, SENSE_BUFFER_SIZE, TAG_SCSIPORT);
    if (SenseBuffer == NULL)
    {
        ExFreePoolWithTag(InquiryBuffer, TAG_SCSIPORT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    while (KeepTrying)
    {
        /* Initialize event for waiting */
        KeInitializeEvent(&Event,
                          NotificationEvent,
                          FALSE);

        /* Create an IRP */
        Irp = IoBuildDeviceIoControlRequest(IOCTL_SCSI_EXECUTE_IN,
                                            DeviceObject,
                                            NULL,
                                            0,
                                            InquiryBuffer,
                                            INQUIRYDATABUFFERSIZE,
                                            TRUE,
                                            &Event,
                                            &IoStatusBlock);
        if (Irp == NULL)
        {
            DPRINT("IoBuildDeviceIoControlRequest() failed\n");

            /* Quit the loop */
            Status = STATUS_INSUFFICIENT_RESOURCES;
            KeepTrying = FALSE;
            continue;
        }

        /* Prepare SRB */
        RtlZeroMemory(&Srb, sizeof(SCSI_REQUEST_BLOCK));

        Srb.Length = sizeof(SCSI_REQUEST_BLOCK);
        Srb.OriginalRequest = Irp;
        Srb.PathId = LunExtension->PathId;
        Srb.TargetId = LunExtension->TargetId;
        Srb.Lun = LunExtension->Lun;
        Srb.Function = SRB_FUNCTION_EXECUTE_SCSI;
        Srb.SrbFlags = SRB_FLAGS_DATA_IN | SRB_FLAGS_DISABLE_SYNCH_TRANSFER;
        Srb.TimeOutValue = 4;
        Srb.CdbLength = 6;

        Srb.SenseInfoBuffer = SenseBuffer;
        Srb.SenseInfoBufferLength = SENSE_BUFFER_SIZE;

        Srb.DataBuffer = InquiryBuffer;
        Srb.DataTransferLength = INQUIRYDATABUFFERSIZE;

        /* Attach Srb to the Irp */
        IrpStack = IoGetNextIrpStackLocation (Irp);
        IrpStack->Parameters.Scsi.Srb = &Srb;

        /* Fill in CDB */
        Cdb = (PCDB)Srb.Cdb;
        Cdb->CDB6INQUIRY.OperationCode = SCSIOP_INQUIRY;
        Cdb->CDB6INQUIRY.LogicalUnitNumber = LunExtension->Lun;
        Cdb->CDB6INQUIRY.AllocationLength = INQUIRYDATABUFFERSIZE;

        /* Call the driver */
        Status = IoCallDriver(DeviceObject, Irp);

        /* Wait for it to complete */
        if (Status == STATUS_PENDING)
        {
            DPRINT("FdoSendInquiry(): Waiting for the driver to process request...\n");
            KeWaitForSingleObject(&Event,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = IoStatusBlock.Status;
        }

        DPRINT1("FdoSendInquiry: SRB completed with status 0x%08X (SrbStatus 0x%02X)\n",
                Status,
                Srb.SrbStatus);

        if (SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_SUCCESS)
        {
            /* All fine, copy data over */
            RtlCopyMemory(&LunExtension->InquiryData,
                          InquiryBuffer,
                          INQUIRYDATABUFFERSIZE);

            CHAR vendorId[9] = {0};
            CHAR productId[17] = {0};
            RtlCopyMemory(vendorId, LunExtension->InquiryData.VendorId, 8);
            RtlCopyMemory(productId, LunExtension->InquiryData.ProductId, 16);

            DPRINT1("FdoSendInquiry: DeviceType=%u Removable=%u Vendor='%s' Product='%s'\n",
                    LunExtension->InquiryData.DeviceType,
                    LunExtension->InquiryData.RemovableMedia,
                    vendorId,
                    productId);

            /* Quit the loop */
            Status = STATUS_SUCCESS;
            KeepTrying = FALSE;
            continue;
        }

        UCHAR senseKey = 0;
        UCHAR asc = 0;
        UCHAR ascq = 0;

        if (Srb.SrbStatus & SRB_STATUS_AUTOSENSE_VALID)
        {
            senseKey = SenseBuffer->SenseKey;
            asc = SenseBuffer->AdditionalSenseCode;
            ascq = SenseBuffer->AdditionalSenseCodeQualifier;
        }

        DPRINT1("FdoSendInquiry: Inquiry failed SrbStatus 0x%02X SenseKey 0x%02X ASC 0x%02X ASCQ 0x%02X\n",
                Srb.SrbStatus,
                senseKey,
                asc,
                ascq);

        /* Check if the queue is frozen */
        if (Srb.SrbStatus & SRB_STATUS_QUEUE_FROZEN)
        {
            /* Something weird happened, deal with it (unfreeze the queue) */
            KeepTrying = FALSE;

            DPRINT("FdoSendInquiry(): the queue is frozen at TargetId %d\n", Srb.TargetId);

            /* Clear frozen flag */
            LunExtension->Flags &= ~LUNEX_FROZEN_QUEUE;

            /* Acquire the spinlock */
            KeAcquireSpinLock(&DeviceExtension->SpinLock, &Irql);

            /* Process the request. SpiGetNextRequestFromLun will unlock for us */
            SpiGetNextRequestFromLun(DeviceExtension, LunExtension, &Irql);
        }

        /* Check if data overrun happened */
        if (SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_DATA_OVERRUN)
        {
            DPRINT("Data overrun at TargetId %d\n", LunExtension->TargetId);

            /* Nothing dramatic, just copy data, but limiting the size */
            RtlCopyMemory(&LunExtension->InquiryData,
                            InquiryBuffer,
                            (Srb.DataTransferLength > INQUIRYDATABUFFERSIZE) ?
                            INQUIRYDATABUFFERSIZE : Srb.DataTransferLength);

            /* Quit the loop */
            Status = STATUS_SUCCESS;
            KeepTrying = FALSE;
        }
        else if ((Srb.SrbStatus & SRB_STATUS_AUTOSENSE_VALID) &&
                 SenseBuffer->SenseKey == SCSI_SENSE_ILLEGAL_REQUEST)
        {
            /* LUN is not valid, but some device responds there.
                Mark it as invalid anyway */

            /* Quit the loop */
            Status = STATUS_INVALID_DEVICE_REQUEST;
            KeepTrying = FALSE;
        }
        else
        {
            /* Retry a couple of times if no timeout happened */
            if ((RetryCount < 2) &&
                (SRB_STATUS(Srb.SrbStatus) != SRB_STATUS_NO_DEVICE) &&
                (SRB_STATUS(Srb.SrbStatus) != SRB_STATUS_SELECTION_TIMEOUT))
            {
                RetryCount++;
                KeepTrying = TRUE;
            }
            else
            {
                /* That's all, quit the loop */
                KeepTrying = FALSE;

                /* Set status according to SRB status */
                if (SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_BAD_FUNCTION ||
                    SRB_STATUS(Srb.SrbStatus) == SRB_STATUS_BAD_SRB_BLOCK_LENGTH)
                {
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                }
                else
                {
                    Status = STATUS_IO_DEVICE_ERROR;
                }
            }
        }
    }

    /* Free buffers */
    ExFreePoolWithTag(InquiryBuffer, TAG_SCSIPORT);
    ExFreePoolWithTag(SenseBuffer, TAG_SCSIPORT);

    DPRINT("FdoSendInquiry() done with Status 0x%08X\n", Status);

    return Status;
}

/* Scans all SCSI buses */
VOID
FdoScanAdapter(
    _In_ PSCSI_PORT_DEVICE_EXTENSION PortExtension)
{
    NTSTATUS status;
    UINT32 totalLUNs = PortExtension->TotalLUCount;

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[SCSIPORT] FdoScanAdapter entry: Port=%lu Buses=%lu Targets=%lu MaxLuns=%lu MaxLogicalUnits=%lu BusId0=%ld\n",
               PortExtension->PortNumber,
               (ULONG)PortExtension->NumberOfBuses,
               PortExtension->PortConfig ? (ULONG)PortExtension->PortConfig->MaximumNumberOfTargets : 0UL,
               (ULONG)PortExtension->MaxLunCount,
               PortExtension->PortConfig ? (ULONG)PortExtension->PortConfig->MaximumNumberOfLogicalUnits : 0UL,
               PortExtension->Buses ? (LONG)PortExtension->Buses[0].BusIdentifier : -1L);

    /* Scan all buses */
    for (UINT8 pathId = 0; pathId < PortExtension->NumberOfBuses; pathId++)
    {
        DPRINT("    Scanning bus/pathID %u\n", pathId);

        /* Get pointer to the scan information */
        PSCSI_BUS_INFO currentBus = &PortExtension->Buses[pathId];

        /* And send INQUIRY to every target */
        for (UINT8 targetId = 0;
             targetId < PortExtension->PortConfig->MaximumNumberOfTargets;
             targetId++)
        {
            BOOLEAN targetFound = FALSE;

            /* TODO: Support scan bottom-up */

            /* Skip if it's the same address */
            if (currentBus->BusIdentifier != (UCHAR)SP_UNINITIALIZED_VALUE &&
                targetId == currentBus->BusIdentifier)
                continue;

            /* Scan all logical units */
            for (UINT8 lun = 0; lun < PortExtension->MaxLunCount; lun++)
            {
                PSCSI_PORT_LUN_EXTENSION lunExt;

                /* Skip invalid lun values */
                if (lun >= PortExtension->PortConfig->MaximumNumberOfLogicalUnits)
                    continue;

                // try to find an existing device
                lunExt = GetLunByPath(PortExtension,
                                      pathId,
                                      targetId,
                                      lun);

                if (lunExt)
                {
                    // check if the device still exists
                    status = FdoSendInquiry(lunExt->Common.DeviceObject);
                    if (!NT_SUCCESS(status))
                    {
                        // remove the device
                        UNIMPLEMENTED;
                        __debugbreak();
                    }

                    if (lunExt->InquiryData.DeviceTypeQualifier == DEVICE_QUALIFIER_NOT_SUPPORTED)
                    {
                        // remove the device
                        UNIMPLEMENTED;
                        __debugbreak();
                    }

                    /* Decide whether we are continuing or not */
                    if (status == STATUS_INVALID_DEVICE_REQUEST)
                        continue;
                    else
                        break;
                }

                // create a new LUN device
                PDEVICE_OBJECT lunPDO = PdoCreateLunDevice(PortExtension);
                if (!lunPDO)
                {
                    continue;
                }

                lunExt = lunPDO->DeviceExtension;

                lunExt->PathId = pathId;
                lunExt->TargetId = targetId;
                lunExt->Lun = lun;

                DPRINT("Add PDO to list: PDO: %p, FDOExt: %p, PDOExt: %p\n", lunPDO, PortExtension, lunExt);

                /* Set flag to prevent race conditions */
                lunExt->Flags |= SCSI_PORT_SCAN_IN_PROGRESS;

                /* Finally send the inquiry command */
                status = FdoSendInquiry(lunPDO);

                if (NT_SUCCESS(status))
                {
                    /* Let's see if we really found a device */
                    PINQUIRYDATA InquiryData = &lunExt->InquiryData;

                    /* Check if this device is unsupported */
                    if (InquiryData->DeviceTypeQualifier == DEVICE_QUALIFIER_NOT_SUPPORTED)
                    {
                        IoDeleteDevice(lunPDO);
                        continue;
                    }

                    /* Clear the "in scan" flag */
                    lunExt->Flags &= ~SCSI_PORT_SCAN_IN_PROGRESS;

                    DPRINT1("Found device of type %d at controller %d bus %d tid %d lun %d, PDO: %p\n",
                        InquiryData->DeviceType, PortExtension->PortNumber, pathId, targetId, lun, lunPDO);

                    InsertTailList(&currentBus->LunsListHead, &lunExt->LunEntry);

                    totalLUNs++;
                    currentBus->LogicalUnitsCount++;
                    targetFound = TRUE;
                }
                else
                {
                    /* Decide whether we are continuing or not */
                    if (status == STATUS_INVALID_DEVICE_REQUEST)
                        continue;
                    else
                        break;
                }
            }

            if (targetFound)
            {
                currentBus->TargetsCount++;
            }
        }
    }

    PortExtension->TotalLUCount = totalLUNs;
}

/**
 * @brief      Calls HwInitialize routine of the miniport and sets up interrupts
 *             Should be called inside ScsiPortInitialize (for legacy drivers)
 *             or inside IRP_MN_START_DEVICE for pnp drivers
 *
 * @param[in]  DeviceExtension  The device extension
 *
 * @return     NTSTATUS of the operation
 */
NTSTATUS
FdoCallHWInitialize(
    _In_ PSCSI_PORT_DEVICE_EXTENSION DeviceExtension)
{
    PPORT_CONFIGURATION_INFORMATION PortConfig = DeviceExtension->PortConfig;
    NTSTATUS Status;
    KIRQL OldIrql;

    SppEnsureInterruptApi();
    const BOOLEAN hasInterruptEx =
        (SppIoConnectInterruptExPointer != NULL &&
         SppIoDisconnectInterruptExPointer != NULL);

    /* Deal with interrupts */
    if (DeviceExtension->HwInterrupt == NULL)
    {
        DeviceExtension->InterruptCount = 0;
    }
    else if (DeviceExtension->InterruptMessageBased)
    {
        if (!hasInterruptEx)
        {
            DPRINT1("Message-based interrupts requested but IoConnectInterruptEx is unavailable\n");
            return STATUS_NOT_SUPPORTED;
        }

        IO_CONNECT_INTERRUPT_PARAMETERS connectParameters;

        if (DeviceExtension->InterruptContext != NULL)
        {
            ExFreePoolWithTag(DeviceExtension->InterruptContext, TAG_SCSIPORT);
            DeviceExtension->InterruptContext = NULL;
        }

        DeviceExtension->InterruptContext =
            ExAllocatePoolZero(NonPagedPool,
                               sizeof(SCSI_PORT_INTERRUPT_CONTEXT),
                               TAG_SCSIPORT);
        if (DeviceExtension->InterruptContext == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        DeviceExtension->InterruptContext[0].DeviceExtension = DeviceExtension;
        DeviceExtension->InterruptContext[0].MessageId = (ULONG)-1;

        RtlZeroMemory(&connectParameters, sizeof(connectParameters));
        connectParameters.Version = CONNECT_MESSAGE_BASED;
        connectParameters.MessageBased.PhysicalDeviceObject =
            DeviceExtension->PhysicalDeviceObject ?
                DeviceExtension->PhysicalDeviceObject :
                DeviceExtension->Common.DeviceObject;
        connectParameters.MessageBased.ConnectionContext.InterruptMessageTable =
            &DeviceExtension->InterruptMessageInfo;
        connectParameters.MessageBased.MessageServiceRoutine = SppMessageInterruptService;
        connectParameters.MessageBased.ServiceContext = &DeviceExtension->InterruptContext[0];
        connectParameters.MessageBased.SpinLock = &DeviceExtension->IrqLock;
        connectParameters.MessageBased.SynchronizeIrql = DISPATCH_LEVEL;
        connectParameters.MessageBased.FloatingSave = FALSE;
        connectParameters.MessageBased.FallBackServiceRoutine = SppMessageFallbackService;

        Status = SppIoConnectInterruptExPointer(&connectParameters);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(DeviceExtension->InterruptContext, TAG_SCSIPORT);
            DeviceExtension->InterruptContext = NULL;
            return Status;
        }

        DeviceExtension->InterruptCount = DeviceExtension->InterruptMessageInfo->MessageCount;
        if (DeviceExtension->InterruptCount == 0)
            DeviceExtension->InterruptCount = 1;

        if (DeviceExtension->InterruptObjects != NULL)
        {
            ExFreePoolWithTag(DeviceExtension->InterruptObjects, TAG_SCSIPORT);
            DeviceExtension->InterruptObjects = NULL;
        }

        DeviceExtension->InterruptObjects =
            ExAllocatePoolZero(NonPagedPool,
                               sizeof(PKINTERRUPT) * DeviceExtension->InterruptCount,
                               TAG_SCSIPORT);
        if (DeviceExtension->InterruptObjects == NULL)
        {
            IO_DISCONNECT_INTERRUPT_PARAMETERS disconnectParameters = {0};
            disconnectParameters.Version = CONNECT_MESSAGE_BASED;
            disconnectParameters.ConnectionContext.InterruptMessageTable =
                DeviceExtension->InterruptMessageInfo;
            SppIoDisconnectInterruptExPointer(&disconnectParameters);
            DeviceExtension->InterruptMessageInfo = NULL;
            ExFreePoolWithTag(DeviceExtension->InterruptContext, TAG_SCSIPORT);
            DeviceExtension->InterruptContext = NULL;
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        for (ULONG index = 0; index < DeviceExtension->InterruptCount; index++)
        {
            DeviceExtension->InterruptObjects[index] =
                DeviceExtension->InterruptMessageInfo->MessageInfo[index].InterruptObject;
        }
    }
    else
    {
        ULONG lineInterruptCount = 0;
        ULONG interruptVector[2];
        KIRQL dirql[2];
        KAFFINITY affinity[2];
        KIRQL maxDirql;

        if (PortConfig->BusInterruptLevel != 0 || PortConfig->BusInterruptVector != 0)
            lineInterruptCount++;
        if (PortConfig->BusInterruptLevel2 != 0 || PortConfig->BusInterruptVector2 != 0)
            lineInterruptCount++;

        if (lineInterruptCount == 0)
        {
            DeviceExtension->InterruptCount = 0;

            DPRINT1("No line-based interrupts were described\n");
            return STATUS_INVALID_DEVICE_STATE;
        }
        else
        {
            DeviceExtension->InterruptCount = lineInterruptCount;

            if (DeviceExtension->InterruptObjects != NULL)
            {
                ExFreePoolWithTag(DeviceExtension->InterruptObjects, TAG_SCSIPORT);
                DeviceExtension->InterruptObjects = NULL;
            }

            if (DeviceExtension->InterruptContext != NULL)
            {
                ExFreePoolWithTag(DeviceExtension->InterruptContext, TAG_SCSIPORT);
                DeviceExtension->InterruptContext = NULL;
            }

            DeviceExtension->InterruptObjects =
                ExAllocatePoolZero(NonPagedPool,
                                   sizeof(PKINTERRUPT) * DeviceExtension->InterruptCount,
                                   TAG_SCSIPORT);
            if (DeviceExtension->InterruptObjects == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            DeviceExtension->InterruptContext =
                ExAllocatePoolZero(NonPagedPool,
                                   sizeof(SCSI_PORT_INTERRUPT_CONTEXT) * DeviceExtension->InterruptCount,
                                   TAG_SCSIPORT);
            if (DeviceExtension->InterruptContext == NULL)
            {
                ExFreePoolWithTag(DeviceExtension->InterruptObjects, TAG_SCSIPORT);
                DeviceExtension->InterruptObjects = NULL;
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            for (ULONG idx = 0; idx < DeviceExtension->InterruptCount; idx++)
            {
                ULONG level = (idx == 0) ? PortConfig->BusInterruptLevel
                                         : PortConfig->BusInterruptLevel2;
                ULONG vector = (idx == 0) ? PortConfig->BusInterruptVector
                                          : PortConfig->BusInterruptVector2;

                DeviceExtension->InterruptContext[idx].DeviceExtension = DeviceExtension;
                DeviceExtension->InterruptContext[idx].MessageId = idx;

                interruptVector[idx] = HalGetInterruptVector(
                    PortConfig->AdapterInterfaceType,
                    PortConfig->SystemIoBusNumber,
                    level,
                    vector,
                    &dirql[idx],
                    &affinity[idx]);

                DbgPrintEx(DPFLTR_DEFAULT_ID,
                           DPFLTR_ERROR_LEVEL,
                           "[SCSIPORT] Interrupt[%lu]: level=%lu vector=%lu -> systemVector=%lu dirql=%lu affinity=%p\n",
                           idx,
                           level,
                           vector,
                           interruptVector[idx],
                           dirql[idx],
                           (PVOID)(ULONG_PTR)affinity[idx]);
            }

            maxDirql = dirql[0];
            for (ULONG idx = 1; idx < DeviceExtension->InterruptCount; idx++)
            {
                if (dirql[idx] > maxDirql)
                    maxDirql = dirql[idx];
            }

            for (ULONG idx = 0; idx < DeviceExtension->InterruptCount; idx++)
            {
                BOOLEAN interruptShareable;
                KINTERRUPT_MODE interruptMode =
                    (idx == 0) ? PortConfig->InterruptMode : PortConfig->InterruptMode2;
                PKINTERRUPT interruptObject = NULL;
                IO_CONNECT_INTERRUPT_PARAMETERS connectParameters;

                interruptShareable =
                    (PortConfig->AdapterInterfaceType == MicroChannel) ||
                    (interruptMode == LevelSensitive);

                if (hasInterruptEx)
                {
                    RtlZeroMemory(&connectParameters, sizeof(connectParameters));
                    connectParameters.Version = CONNECT_FULLY_SPECIFIED;
                    connectParameters.FullySpecified.PhysicalDeviceObject =
                        DeviceExtension->PhysicalDeviceObject ?
                            DeviceExtension->PhysicalDeviceObject :
                            DeviceExtension->Common.DeviceObject;
                    connectParameters.FullySpecified.InterruptObject = &interruptObject;
                    connectParameters.FullySpecified.ServiceRoutine = ScsiPortIsr;
                    connectParameters.FullySpecified.ServiceContext =
                        &DeviceExtension->InterruptContext[idx];
                    connectParameters.FullySpecified.SpinLock = &DeviceExtension->IrqLock;
                    connectParameters.FullySpecified.SynchronizeIrql = maxDirql;
                    connectParameters.FullySpecified.FloatingSave = FALSE;
                    connectParameters.FullySpecified.ShareVector = interruptShareable;
                    connectParameters.FullySpecified.Vector = interruptVector[idx];
                    connectParameters.FullySpecified.Irql = dirql[idx];
                    connectParameters.FullySpecified.InterruptMode = interruptMode;
                    connectParameters.FullySpecified.ProcessorEnableMask = affinity[idx];
                    connectParameters.FullySpecified.Group = 0;

                    Status = SppIoConnectInterruptExPointer(&connectParameters);
                    if (!NT_SUCCESS(Status))
                    {
                        IO_DISCONNECT_INTERRUPT_PARAMETERS disconnectParameters = {0};

                        while (idx-- > 0)
                        {
                            disconnectParameters.Version = CONNECT_FULLY_SPECIFIED;
                            disconnectParameters.ConnectionContext.InterruptObject =
                                DeviceExtension->InterruptObjects[idx];
                            SppIoDisconnectInterruptExPointer(&disconnectParameters);
                        }

                        ExFreePoolWithTag(DeviceExtension->InterruptObjects, TAG_SCSIPORT);
                        DeviceExtension->InterruptObjects = NULL;
                        ExFreePoolWithTag(DeviceExtension->InterruptContext, TAG_SCSIPORT);
                        DeviceExtension->InterruptContext = NULL;
                        return Status;
                    }

                    DeviceExtension->InterruptObjects[idx] = interruptObject;
                }
                else
                {
                    Status = IoConnectInterrupt(&DeviceExtension->InterruptObjects[idx],
                                                ScsiPortIsr,
                                                &DeviceExtension->InterruptContext[idx],
                                                &DeviceExtension->IrqLock,
                                                interruptVector[idx],
                                                dirql[idx],
                                                maxDirql,
                                                interruptMode,
                                                interruptShareable,
                                                affinity[idx],
                                                FALSE);

                    if (!NT_SUCCESS(Status))
                    {
                        while (idx-- > 0)
                        {
                            IoDisconnectInterrupt(DeviceExtension->InterruptObjects[idx]);
                        }

                        ExFreePoolWithTag(DeviceExtension->InterruptObjects, TAG_SCSIPORT);
                        DeviceExtension->InterruptObjects = NULL;
                        ExFreePoolWithTag(DeviceExtension->InterruptContext, TAG_SCSIPORT);
                        DeviceExtension->InterruptContext = NULL;
                        return Status;
                    }
                }
            }
        }
    }

    /* Save IoAddress (from access ranges) */
    if (PortConfig->NumberOfAccessRanges != 0)
    {
        DeviceExtension->IoAddress = ((*(PortConfig->AccessRanges))[0]).RangeStart.LowPart;

        DPRINT("Io Address %x\n", DeviceExtension->IoAddress);
    }

    /* Set flag that it's allowed to disconnect during this command */
    DeviceExtension->Flags |= SCSI_PORT_DISCONNECT_ALLOWED;

    /* Initialize counter of active requests (-1 means there are none) */
    DeviceExtension->ActiveRequestCounter = -1;

    /* Analyze what we have about DMA */
    if (DeviceExtension->AdapterObject != NULL && PortConfig->Master &&
        PortConfig->NeedPhysicalAddresses)
    {
        DeviceExtension->MapRegisters = TRUE;
    }
    else
    {
        DeviceExtension->MapRegisters = FALSE;
    }

    /* Call HwInitialize at DISPATCH_LEVEL */
    #if DBG
    {
        KIRQL cur = KeGetCurrentIrql();
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL,
                   "[SCSIPORT] FdoStartAdapter: raising IRQL to DISPATCH_LEVEL (current=%lu) before HwInitialize\n",
                   (ULONG)cur);
    }
    #endif
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    PKINTERRUPT primaryInterrupt = SppGetPrimaryInterruptObject(DeviceExtension);

    if (primaryInterrupt == NULL ||
        !KeSynchronizeExecution(primaryInterrupt,
                                 DeviceExtension->HwInitialize,
                                 DeviceExtension->MiniPortDeviceExtension))
    {
        DPRINT1("HwInitialize() failed!\n");
        KeLowerIrql(OldIrql);
        #if DBG
        {
            KIRQL cur2 = KeGetCurrentIrql();
            if (cur2 != OldIrql)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                           "[SCSIPORT] FdoStartAdapter: IRQL after failure lower is %lu, expected %lu\n",
                           (ULONG)cur2, (ULONG)OldIrql);
            }
        }
        #endif
        return STATUS_ADAPTER_HARDWARE_ERROR;
    }

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[SCSIPORT] HwInitialize completed successfully\n");

    /* Check if a notification is needed */
    if (DeviceExtension->InterruptData.Flags & SCSI_PORT_NOTIFICATION_NEEDED)
    {
        /* Call DPC right away, because we're already at DISPATCH_LEVEL */
        ScsiPortDpcForIsr(NULL, DeviceExtension->Common.DeviceObject, NULL, NULL);
    }

    /* Lower irql back to what it was */
    KeLowerIrql(OldIrql);
    #if DBG
    {
        KIRQL cur3 = KeGetCurrentIrql();
        if (cur3 != OldIrql)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[SCSIPORT] FdoStartAdapter: IRQL after lower is %lu, expected %lu\n",
                       (ULONG)cur3, (ULONG)OldIrql);
        }
    }
    #endif

    return STATUS_SUCCESS;
}

NTSTATUS
FdoRemoveAdapter(
    _Inout_ PSCSI_PORT_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN DeleteDeviceObject)
{
    PDEVICE_OBJECT deviceObject = DeviceExtension->Common.DeviceObject;

    SppEnsureInterruptApi();
    const BOOLEAN hasInterruptEx = (SppIoDisconnectInterruptExPointer != NULL);

    if (deviceObject)
    {
        IoStopTimer(deviceObject);
    }

    // release device interface
    if (DeviceExtension->InterfaceName.Buffer)
    {
        NTSTATUS status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
        UNREFERENCED_PARAMETER(status);

        RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
        RtlInitUnicodeString(&DeviceExtension->InterfaceName, NULL);
    }

    // remove the dos device link if we created one
    if (DeviceExtension->DeviceName.Buffer && DeviceExtension->DeviceName.Length != 0)
    {
        WCHAR dosNameBuffer[12];
        UNICODE_STRING dosDeviceName;

        swprintf(dosNameBuffer, L"\\??\\Scsi%lu:", DeviceExtension->PortNumber);
        RtlInitUnicodeString(&dosDeviceName, dosNameBuffer);

        IoDeleteSymbolicLink(&dosDeviceName); // ignore the result
    }

    // decrease the port count
    if (DeviceExtension->DeviceStarted)
    {
        PCONFIGURATION_INFORMATION sysConfig = IoGetConfigurationInformation();
        sysConfig->ScsiPortCount--;
        DeviceExtension->DeviceStarted = FALSE;
    }

    // disconnect the interrupts
    if (DeviceExtension->InterruptMessageInfo != NULL)
    {
        if (hasInterruptEx)
        {
            IO_DISCONNECT_INTERRUPT_PARAMETERS disconnectParameters = {0};

            disconnectParameters.Version = CONNECT_MESSAGE_BASED;
            disconnectParameters.ConnectionContext.InterruptMessageTable =
                DeviceExtension->InterruptMessageInfo;

            SppIoDisconnectInterruptExPointer(&disconnectParameters);

            DeviceExtension->InterruptMessageInfo = NULL;
        }
    }
    else if (DeviceExtension->InterruptObjects != NULL)
    {
        if (hasInterruptEx)
        {
            IO_DISCONNECT_INTERRUPT_PARAMETERS disconnectParameters = {0};

            disconnectParameters.Version = CONNECT_FULLY_SPECIFIED;

            for (ULONG idx = 0; idx < DeviceExtension->InterruptCount; idx++)
            {
                PKINTERRUPT interruptObject = DeviceExtension->InterruptObjects[idx];

                if (!interruptObject)
                    continue;

                disconnectParameters.ConnectionContext.InterruptObject = interruptObject;
                SppIoDisconnectInterruptExPointer(&disconnectParameters);
            }
        }
        else
        {
            for (ULONG idx = 0; idx < DeviceExtension->InterruptCount; idx++)
            {
                PKINTERRUPT interruptObject = DeviceExtension->InterruptObjects[idx];

                if (!interruptObject)
                    continue;

                IoDisconnectInterrupt(interruptObject);
            }
        }
    }

    DeviceExtension->InterruptCount = 0;

    if (DeviceExtension->InterruptObjects)
    {
        ExFreePoolWithTag(DeviceExtension->InterruptObjects, TAG_SCSIPORT);
        DeviceExtension->InterruptObjects = NULL;
    }

    if (DeviceExtension->InterruptContext)
    {
        ExFreePoolWithTag(DeviceExtension->InterruptContext, TAG_SCSIPORT);
        DeviceExtension->InterruptContext = NULL;
    }

    DeviceExtension->InterruptMessageBased = FALSE;

    // FIXME: delete LUNs
    if (DeviceExtension->Buses)
    {
        for (UINT8 pathId = 0; pathId < DeviceExtension->NumberOfBuses; pathId++)
        {
            PSCSI_BUS_INFO bus = &DeviceExtension->Buses[pathId];
            if (bus->RegistryMapKey)
            {
                ZwDeleteKey(bus->RegistryMapKey);
                ZwClose(bus->RegistryMapKey);
                bus->RegistryMapKey = NULL;
            }
        }

        ExFreePoolWithTag(DeviceExtension->Buses, TAG_SCSIPORT);
        DeviceExtension->Buses = NULL;
    }

    /* Free PortConfig */
    if (DeviceExtension->PortConfig)
    {
        ExFreePoolWithTag(DeviceExtension->PortConfig, TAG_SCSIPORT);
        DeviceExtension->PortConfig = NULL;
    }

    /* Free common buffer (if it exists) */
    if (DeviceExtension->SrbExtensionBuffer != NULL && DeviceExtension->CommonBufferLength != 0)
    {
        if (DeviceExtension->CommonBufferFromMm)
        {
            MmFreeContiguousMemorySpecifyCache(DeviceExtension->SrbExtensionBuffer,
                                               DeviceExtension->CommonBufferLength,
                                               DeviceExtension->CommonBufferCacheType);
        }
        else if (!DeviceExtension->AdapterObject)
        {
            ExFreePoolWithTag(DeviceExtension->SrbExtensionBuffer, TAG_SCSIPORT);
        }
        else
        {
            HalFreeCommonBuffer(DeviceExtension->AdapterObject,
                                DeviceExtension->CommonBufferLength,
                                DeviceExtension->PhysicalAddress,
                                DeviceExtension->SrbExtensionBuffer,
                                FALSE);
        }

        DeviceExtension->SrbExtensionBuffer = NULL;
        DeviceExtension->CommonBufferLength = 0;
        DeviceExtension->NonCachedExtension = NULL;
        DeviceExtension->CommonBufferFromMm = FALSE;
        DeviceExtension->CommonBufferCacheType = MmNonCached;
    }

    /* Free SRB info */
    if (DeviceExtension->SrbInfo != NULL)
    {
        ExFreePoolWithTag(DeviceExtension->SrbInfo, TAG_SCSIPORT);
        DeviceExtension->SrbInfo = NULL;
        DeviceExtension->FreeSrbInfo = NULL;
        DeviceExtension->SrbDataCount = 0;
    }

    /* Unmap mapped addresses */
    while (DeviceExtension->MappedAddressList != NULL)
    {
        MmUnmapIoSpace(DeviceExtension->MappedAddressList->MappedAddress,
                       DeviceExtension->MappedAddressList->NumberOfBytes);

        PVOID ptr = DeviceExtension->MappedAddressList;
        DeviceExtension->MappedAddressList = DeviceExtension->MappedAddressList->NextMappedAddress;

        ExFreePoolWithTag(ptr, TAG_SCSIPORT);
    }

    if (DeleteDeviceObject && deviceObject)
    {
        if (DeviceExtension->Common.LowerDevice)
        {
            IoDetachDevice(DeviceExtension->Common.LowerDevice);
            DeviceExtension->Common.LowerDevice = NULL;
        }

        IoDeleteDevice(deviceObject);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FdoStartAdapter(
    _In_ PSCSI_PORT_DEVICE_EXTENSION PortExtension)
{
    WCHAR dosNameBuffer[12];
    UNICODE_STRING dosDeviceName;
    NTSTATUS status;

    DPRINT1("FdoStartAdapter: Port %u Buses %u Requests %u\n",
            PortExtension->PortNumber,
            PortExtension->NumberOfBuses,
            PortExtension->RequestsNumber);

    // Start our timer
    IoStartTimer(PortExtension->Common.DeviceObject);

    // Create the DOS device link only for legacy-named devices
    if (PortExtension->DeviceName.Buffer && PortExtension->DeviceName.Length != 0)
    {
        swprintf(dosNameBuffer, L"\\??\\Scsi%u:", PortExtension->PortNumber);
        RtlInitUnicodeString(&dosDeviceName, dosNameBuffer);
        status = IoCreateSymbolicLink(&dosDeviceName, &PortExtension->DeviceName);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    // start building a device map
    RegistryInitAdapterKey(PortExtension);

    // increase the port count
    PCONFIGURATION_INFORMATION sysConfig = IoGetConfigurationInformation();
    sysConfig->ScsiPortCount++;

    // Register and enable the device interface
    PDEVICE_OBJECT pdo = IoGetDeviceAttachmentBaseRef(PortExtension->Common.DeviceObject);
    if (pdo == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    status = IoRegisterDeviceInterface(pdo,
                                       &StoragePortClassGuid,
                                       NULL,
                                       &PortExtension->InterfaceName);
    ObDereferenceObject(pdo);
    DPRINT("IoRegisterDeviceInterface status: %x, InterfaceName: %wZ\n",
        status, &PortExtension->InterfaceName);

    if (NT_SUCCESS(status))
    {
        NTSTATUS interfaceStatus = IoSetDeviceInterfaceState(&PortExtension->InterfaceName, TRUE);
        UNREFERENCED_PARAMETER(interfaceStatus);
    }

    PortExtension->DeviceStarted = TRUE;

    DPRINT1("FdoStartAdapter: Port %u started successfully\n",
            PortExtension->PortNumber);

    return STATUS_SUCCESS;
}

static
NTSTATUS
FdoHandleDeviceRelations(
    _In_ PSCSI_PORT_DEVICE_EXTENSION PortExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_TRACE_LEVEL,
               "[scsiport] FDO QueryDeviceRelations type=%u TotalLUs=%u\n",
               ioStack->Parameters.QueryDeviceRelations.Type,
               PortExtension->TotalLUCount);

    // FDO always only handles bus relations
    if (ioStack->Parameters.QueryDeviceRelations.Type == BusRelations)
    {
        FdoScanAdapter(PortExtension);
        DPRINT("Found %u PD objects, FDOExt: %p\n", PortExtension->TotalLUCount, PortExtension);

        // check that no filter driver has messed up this
        ASSERT(Irp->IoStatus.Information == 0);

        PDEVICE_RELATIONS deviceRelations =
            ExAllocatePoolWithTag(PagedPool,
                                  (sizeof(DEVICE_RELATIONS) +
                                   sizeof(PDEVICE_OBJECT) * (PortExtension->TotalLUCount - 1)),
                                  TAG_SCSIPORT);

        if (!deviceRelations)
        {
            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        deviceRelations->Count = 0;

        for (UINT8 pathId = 0; pathId < PortExtension->NumberOfBuses; pathId++)
        {
            PSCSI_BUS_INFO bus = &PortExtension->Buses[pathId];

            for (PLIST_ENTRY lunEntry = bus->LunsListHead.Flink;
                 lunEntry != &bus->LunsListHead;
                 lunEntry = lunEntry->Flink)
            {
                PSCSI_PORT_LUN_EXTENSION lunExt =
                    CONTAINING_RECORD(lunEntry, SCSI_PORT_LUN_EXTENSION, LunEntry);

                deviceRelations->Objects[deviceRelations->Count++] = lunExt->Common.DeviceObject;
                ObReferenceObject(lunExt->Common.DeviceObject);
            }
        }

        ASSERT(deviceRelations->Count == PortExtension->TotalLUCount);

        Irp->IoStatus.Information = (ULONG_PTR)deviceRelations;
        Irp->IoStatus.Status = STATUS_SUCCESS;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(PortExtension->Common.LowerDevice, Irp);
}

static
NTSTATUS
FdoHandleQueryCompatibleId(
    _Inout_ PZZWSTR* PwIds)
{
    static WCHAR GenScsiAdapterId[] = L"GEN_SCSIADAPTER";
    PWCHAR Ids = *PwIds, NewIds;
    ULONG Length = 0;

    if (Ids)
    {
        /* Calculate the length of existing MULTI_SZ value line by line */
        while (*Ids)
        {
            Ids += wcslen(Ids) + 1;
        }
        Length = Ids - *PwIds;
        Ids = *PwIds;
    }

    /* New MULTI_SZ with added identifier and finalizing zeros */
    NewIds = ExAllocatePoolZero(PagedPool,
                                Length * sizeof(WCHAR) + sizeof(GenScsiAdapterId) + sizeof(UNICODE_NULL),
                                TAG_SCSIPORT);
    if (!NewIds)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Length)
    {
        RtlCopyMemory(NewIds, Ids, Length * sizeof(WCHAR));
    }
    RtlCopyMemory(&NewIds[Length], GenScsiAdapterId, sizeof(GenScsiAdapterId));

    /* Finally replace identifiers */
    if (Ids)
    {
        ExFreePool(Ids);
    }
    *PwIds = NewIds;

    return STATUS_SUCCESS;
}

NTSTATUS
FdoDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PSCSI_PORT_DEVICE_EXTENSION portExt = DeviceObject->DeviceExtension;
    NTSTATUS status;

    ASSERT(portExt->Common.IsFDO);

    DPRINT("FDO PnP request %s\n", GetIRPMinorFunctionString(ioStack->MinorFunction));
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_TRACE_LEVEL,
               "[scsiport] FDO PnP minor=%u\n",
               ioStack->MinorFunction);

    switch (ioStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            PCM_RESOURCE_LIST rawResources = ioStack->Parameters.StartDevice.AllocatedResources;
            PCM_RESOURCE_LIST translatedResources =
                ioStack->Parameters.StartDevice.AllocatedResourcesTranslated;

            DPRINT1("FDO START_DEVICE for %p raw=%p translated=%p\n",
                    DeviceObject,
                    rawResources,
                    translatedResources);

            FdoDumpResourceList("  RAW ", rawResources);
            FdoDumpResourceList("  XLAT", translatedResources);

            status = IoForwardIrpSynchronously(portExt->Common.LowerDevice, Irp);
            if (NT_SUCCESS(status))
            {
                status = FdoStartDeviceWithResources(portExt,
                                                     rawResources,
                                                     translatedResources);
            }
            Irp->IoStatus.Information = 0;
            DPRINT1("FDO START_DEVICE complete with status 0x%08X\n", status);
            break;
        }
        case IRP_MN_STOP_DEVICE:
        {
            status = IoForwardIrpSynchronously(portExt->Common.LowerDevice, Irp);
            if (NT_SUCCESS(status))
            {
                FdoRemoveAdapter(portExt, FALSE);
            }

            Irp->IoStatus.Information = 0;
            DPRINT1("FDO STOP_DEVICE complete with status 0x%08X\n", status);
            break;
        }
        case IRP_MN_SURPRISE_REMOVAL:
        {
            DPRINT1("FDO SURPRISE_REMOVAL for %p\n", DeviceObject);
            FdoRemoveAdapter(portExt, FALSE);

            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(portExt->Common.LowerDevice, Irp);
        }
        case IRP_MN_REMOVE_DEVICE:
        {
            DPRINT1("FDO REMOVE_DEVICE for %p\n", DeviceObject);

            IoSkipCurrentIrpStackLocation(Irp);
            status = IoCallDriver(portExt->Common.LowerDevice, Irp);

            FdoRemoveAdapter(portExt, TRUE);

            return status;
        }
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            return FdoHandleDeviceRelations(portExt, Irp);
        }
        case IRP_MN_QUERY_ID:
        {
            if (ioStack->Parameters.QueryId.IdType == BusQueryCompatibleIDs)
            {
                Irp->IoStatus.Information = 0;
                IoForwardIrpSynchronously(portExt->Common.LowerDevice, Irp);
                status = FdoHandleQueryCompatibleId((PZZWSTR*)&Irp->IoStatus.Information);
                break;
            }
            // otherwise fall through the default case
        }
        default:
        {
            // forward irp to next device object
            IoCopyCurrentIrpStackLocationToNext(Irp);
            return IoCallDriver(portExt->Common.LowerDevice, Irp);
        }
    }

    if (status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return status;
}
