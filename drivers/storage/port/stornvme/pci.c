/*
 * PROJECT:     ReactOS Storport NVMe miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Adapter discovery, resources and Storport entry points
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "stornvme.h"

/*
 * Firmware or the bus driver can leave MSI/MSI-X armed before any interrupt
 * is connected; the controller then fires messages into an unregistered
 * vector during the polled bring-up. Mask message generation at the device
 * until the service routines are live.
 */
VOID
NvmeMaskDeviceInterrupts(_In_ PNVME_DEVICE_EXTENSION Device, _In_ BOOLEAN Mask)
{
    UCHAR Config[256];
    ULONG CapOffset;
    ULONG Guard;

    if (StorPortGetBusData(Device, PCIConfiguration, Device->SystemIoBusNumber,
                           Device->SlotNumber, Config, sizeof(Config)) < 64)
        return;
    if ((Config[0x06] & 0x10) == 0)
        return;

    CapOffset = Config[0x34] & 0xFC;
    for (Guard = 0; CapOffset >= 0x40 && CapOffset <= 0xFC - 4 && Guard < 48; Guard++)
    {
        UCHAR CapId = Config[CapOffset];
        USHORT MessageControl = (USHORT)(Config[CapOffset + 2] | (Config[CapOffset + 3] << 8));

        if (CapId == 0x05 && (MessageControl & 0x0100))
        {
            ULONG MaskBits = Mask ? 0xFFFFFFFF : 0;
            ULONG MaskOffset = CapOffset + ((MessageControl & 0x0080) ? 0x10 : 0x0C);

            StorPortSetBusDataByOffset(Device, PCIConfiguration, Device->SystemIoBusNumber,
                                       Device->SlotNumber, &MaskBits, MaskOffset, sizeof(MaskBits));
        }
        else if (CapId == 0x11)
        {
            USHORT NewControl = Mask ? (MessageControl | 0x4000) : (MessageControl & ~0x4000);

            if (NewControl != MessageControl)
            {
                StorPortSetBusDataByOffset(Device, PCIConfiguration, Device->SystemIoBusNumber,
                                           Device->SlotNumber, &NewControl, CapOffset + 2, sizeof(NewControl));
            }
        }
        CapOffset = Config[CapOffset + 1] & 0xFC;
    }
}

static
VOID
NvmeReportPcieLink(_In_ PNVME_DEVICE_EXTENSION Device)
{
    UCHAR Config[256];
    ULONG CapOffset;
    ULONG Guard;

    if (StorPortGetBusData(Device, PCIConfiguration, Device->SystemIoBusNumber,
                           Device->SlotNumber, Config, sizeof(Config)) < 64 ||
        (Config[0x06] & 0x10) == 0)
    {
        return;
    }
    CapOffset = Config[0x34] & 0xFC;
    for (Guard = 0; CapOffset >= 0x40 && CapOffset <= 0xFC - 4 && Guard < 48; Guard++)
    {
        if (Config[CapOffset] == 0x10)
        {
            USHORT LinkStatus = (USHORT)(Config[CapOffset + 0x12] | (Config[CapOffset + 0x13] << 8));

            DPRINT1("stornvme: PCIe link x%u Gen%u\n", (LinkStatus >> 4) & 0x3F, LinkStatus & 0xF);
            return;
        }
        CapOffset = Config[CapOffset + 1] & 0xFC;
    }
}

static
ULONG
NTAPI
NvmeFindAdapter(_In_ PVOID DeviceExtension,
                _In_ PVOID HwContext,
                _In_ PVOID BusInformation,
                _In_ PCHAR ArgumentString,
                _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
                _Out_ PBOOLEAN Again)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;
    PACCESS_RANGE AccessRange;
    STOR_PHYSICAL_ADDRESS Physical;
    PUCHAR Uncached;
    ULONG UncachedSize;
    ULONG CapLow;
    ULONG CapHigh;
    ULONG QueueCapacity;
    ULONG IoEntries;
    ULONG SqBytes;
    ULONG CqBytes;
    ULONG Processors;
    ULONG Length;
    ULONG Offset;
    ULONG Index;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Again = FALSE;

    AccessRange = &(*ConfigInfo->AccessRanges)[0];
    if (AccessRange->RangeLength == 0 || !AccessRange->RangeInMemory)
        return SP_RETURN_NOT_FOUND;

    Device->SystemIoBusNumber = ConfigInfo->SystemIoBusNumber;
    Device->SlotNumber = ConfigInfo->SlotNumber;
    NvmeMaskDeviceInterrupts(Device, TRUE);
    NvmeReportPcieLink(Device);

    Device->Bar0 = (PUCHAR)StorPortGetDeviceBase(Device,
                                                 ConfigInfo->AdapterInterfaceType,
                                                 ConfigInfo->SystemIoBusNumber,
                                                 AccessRange->RangeStart,
                                                 AccessRange->RangeLength,
                                                 FALSE);
    if (Device->Bar0 == NULL)
        return SP_RETURN_ERROR;

    CapLow = NvmeReadRegister(Device, NVME_REG_CAP);
    CapHigh = NvmeReadRegister(Device, NVME_REG_CAP + 4);
    Device->DoorbellStride = 4 << NVME_CAP_HIGH_DSTRD(CapHigh);
    if ((NVME_CAP_HIGH_CSS(CapHigh) & 1) == 0 ||
        NVME_CAP_HIGH_MPSMIN(CapHigh) > PAGE_SHIFT - 12 ||
        NVME_CAP_HIGH_MPSMAX(CapHigh) < PAGE_SHIFT - 12)
    {
        DPRINT1("stornvme: controller does not support the NVM command set with %lu-byte pages\n", PAGE_SIZE);
        return SP_RETURN_ERROR;
    }

    /* CAP.TO is in 500 ms units; keep a floor for controllers reporting 0. */
    Device->TimeoutMilliseconds = NVME_CAP_LOW_TO(CapLow) * 500;
    if (Device->TimeoutMilliseconds < 1000)
        Device->TimeoutMilliseconds = 1000;
    if (Device->TimeoutMilliseconds > 30000)
        Device->TimeoutMilliseconds = 30000;

    QueueCapacity = NVME_CAP_LOW_MQES(CapLow) + 1;
    if (QueueCapacity < 2)
    {
        DPRINT1("stornvme: controller queue capacity is too small\n");
        return SP_RETURN_ERROR;
    }
    IoEntries = min(NVME_IO_QUEUE_ENTRIES, QueueCapacity);
    Device->MaxQueueEntries = IoEntries;

    /* One I/O queue pair per processor, bounded by the doorbell window. */
    Processors = KeQueryActiveProcessorCount(NULL);
    if (Processors == 0)
        Processors = 1;
    Device->IoQueueCount = min(Processors, NVME_MAX_IO_QUEUES);
    while (Device->IoQueueCount > 1 &&
           AccessRange->RangeLength < NVME_CQ_DOORBELL(Device->DoorbellStride, Device->IoQueueCount) + sizeof(ULONG))
    {
        Device->IoQueueCount--;
    }
    if (AccessRange->RangeLength < NVME_CQ_DOORBELL(Device->DoorbellStride, 1) + sizeof(ULONG))
    {
        DPRINT1("stornvme: BAR0 is too small for queue doorbells\n");
        return SP_RETURN_ERROR;
    }

    SqBytes = (ULONG)ROUND_TO_PAGES(IoEntries * sizeof(NVME_COMMAND));
    CqBytes = (ULONG)ROUND_TO_PAGES(IoEntries * sizeof(NVME_COMPLETION));
    UncachedSize = PAGE_SIZE +                                  /* admin SQ */
                   PAGE_SIZE +                                  /* admin CQ */
                   Device->IoQueueCount * (SqBytes + CqBytes) +
                   PAGE_SIZE +                                  /* identify/scratch */
                   PAGE_SIZE;                                   /* SMART log */
    Uncached = (PUCHAR)StorPortGetUncachedExtension(Device, ConfigInfo, UncachedSize);
    if (Uncached == NULL)
        return SP_RETURN_ERROR;
    RtlZeroMemory(Uncached, UncachedSize);
    Device->UncachedBase = Uncached;
    Device->UncachedSize = UncachedSize;
    Physical = StorPortGetPhysicalAddress(Device, NULL, Uncached, &Length);
    Device->UncachedPhysical = (ULONGLONG)Physical.QuadPart;
    if (Device->UncachedPhysical == 0 || (Device->UncachedPhysical & (PAGE_SIZE - 1)) != 0)
        return SP_RETURN_ERROR;

    Offset = 0;
    Device->AdminQueue.Sq = (PNVME_COMMAND)(Uncached + Offset);
    Device->AdminQueue.SqPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Device->AdminQueue.Cq = (PNVME_COMPLETION)(Uncached + Offset);
    Device->AdminQueue.CqPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    for (Index = 0; Index < Device->IoQueueCount; Index++)
    {
        PNVME_QUEUE Queue = &Device->IoQueues[Index];

        Queue->Sq = (PNVME_COMMAND)(Uncached + Offset);
        Queue->SqPhysical = Device->UncachedPhysical + Offset;
        Offset += SqBytes;
        Queue->Cq = (PNVME_COMPLETION)(Uncached + Offset);
        Queue->CqPhysical = Device->UncachedPhysical + Offset;
        Offset += CqBytes;
        Queue->Entries = IoEntries;
    }
    Device->IdentifyBuffer = Uncached + Offset;
    Device->IdentifyPhysical = Device->UncachedPhysical + Offset;
    Offset += PAGE_SIZE;
    Device->SmartLog = (PNVME_SMART_LOG)(Uncached + Offset);
    Device->SmartLogPhysical = Device->UncachedPhysical + Offset;

    Device->AdminQueue.QueueId = 0;
    Device->AdminQueue.Vector = 0;
    Device->AdminQueue.Entries = min(NVME_ADMIN_QUEUE_ENTRIES, QueueCapacity);
    Device->AdminQueue.Phase = 1;
    Device->AdminQueue.SqDoorbell = NVME_SQ_DOORBELL(Device->DoorbellStride, 0);
    Device->AdminQueue.CqDoorbell = NVME_CQ_DOORBELL(Device->DoorbellStride, 0);

    if (!NvmeStartController(Device, TRUE))
        return SP_RETURN_ERROR;

    ConfigInfo->MaximumTransferLength = Device->MaximumTransferLength;
    ConfigInfo->NumberOfPhysicalBreaks = ((Device->MaximumTransferLength + PAGE_SIZE - 1) / PAGE_SIZE) + 1;
    ConfigInfo->AlignmentMask = 0x3;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = Device->VolatileWriteCache;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = NVME_MAX_NAMESPACES;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->Dma64BitAddresses = SCSI_DMA64_SYSTEM_SUPPORTED;
    ConfigInfo->HwMSInterruptRoutine = NvmeHwMSInterrupt;
    ConfigInfo->InterruptSynchronizationMode = InterruptSynchronizeAll;

    return SP_RETURN_FOUND;
}

static
BOOLEAN
NTAPI
NvmeHwInitialize(_In_ PVOID DeviceExtension)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;
    MESSAGE_INTERRUPT_INFORMATION InterruptInfo;
    NVME_LOCK Lock;
    ULONG MessageId;

    if (!Device->ControllerStarted)
        return FALSE;

    Device->MessageCount = 0;
    for (MessageId = 0; MessageId < NVME_MAX_IO_QUEUES + 1; MessageId++)
    {
        RtlZeroMemory(&InterruptInfo, sizeof(InterruptInfo));
        if (StorPortGetMSIInfo(Device, MessageId, &InterruptInfo) != STOR_STATUS_SUCCESS)
            break;
        Device->MessageCount++;
    }
    Device->MessageInterrupts = Device->MessageCount != 0;

    StorPortInitializeDpc(Device, &Device->AdminQueue.Dpc, NvmeAdminDpc);
    for (MessageId = 0; MessageId < Device->IoQueueCount; MessageId++)
        StorPortInitializeDpc(Device, &Device->IoQueues[MessageId].Dpc, NvmeIoQueueDpc);
    Device->InterruptsLive = TRUE;

    if (!NvmeCreateIoQueues(Device))
        return FALSE;

    NvmeWriteRegister(Device, NVME_REG_INTMC, 0xFFFFFFFF);
    NvmeMaskDeviceInterrupts(Device, FALSE);

    NvmeAcquireLock(Device, 0, &Lock);
    NvmeArmAerLocked(Device);
    NvmeKickSmartLocked(Device);
    NvmeReleaseLock(Device, &Lock);
    return TRUE;
}

static
BOOLEAN
NTAPI
NvmeHwResetBus(_In_ PVOID DeviceExtension, _In_ ULONG PathId)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;

    UNREFERENCED_PARAMETER(PathId);
    return NvmeResetController(Device);
}

static
SCSI_ADAPTER_CONTROL_STATUS
NTAPI
NvmeHwAdapterControl(_In_ PVOID DeviceExtension,
                     _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
                     _In_ PVOID Parameters)
{
    PNVME_DEVICE_EXTENSION Device = (PNVME_DEVICE_EXTENSION)DeviceExtension;

    switch (ControlType)
    {
        case ScsiQuerySupportedControlTypes:
        {
            PSCSI_SUPPORTED_CONTROL_TYPE_LIST List = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;

            if (ScsiQuerySupportedControlTypes < List->MaxControlType)
                List->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
            if (ScsiStopAdapter < List->MaxControlType)
                List->SupportedTypeList[ScsiStopAdapter] = TRUE;
            if (ScsiRestartAdapter < List->MaxControlType)
                List->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            return ScsiAdapterControlSuccess;
        }

        case ScsiStopAdapter:
            /* An orderly shutdown flushes the volatile cache with it. */
            NvmeShutdownController(Device);
            return ScsiAdapterControlSuccess;

        case ScsiRestartAdapter:
        {
            NVME_LOCK Lock;

            if (!NvmeStartController(Device, FALSE) || !NvmeCreateIoQueues(Device))
                return ScsiAdapterControlUnsuccessful;
            NvmeWriteRegister(Device, NVME_REG_INTMC, 0xFFFFFFFF);
            NvmeAcquireLock(Device, 0, &Lock);
            NvmeArmAerLocked(Device);
            NvmeKickSmartLocked(Device);
            NvmeReleaseLock(Device, &Lock);
            return ScsiAdapterControlSuccess;
        }

        default:
            return ScsiAdapterControlUnsuccessful;
    }
}

ULONG
NTAPI
DriverEntry(_In_ PVOID DriverObject, _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = PCIBus;
    InitData.HwInitialize = NvmeHwInitialize;
    InitData.HwStartIo = NvmeHwStartIo;
    InitData.HwInterrupt = NvmeHwInterrupt;
    InitData.HwFindAdapter = NvmeFindAdapter;
    InitData.HwResetBus = NvmeHwResetBus;
    InitData.HwAdapterControl = NvmeHwAdapterControl;
    InitData.DeviceExtensionSize = sizeof(NVME_DEVICE_EXTENSION);
    InitData.SrbExtensionSize = sizeof(NVME_SRB_EXTENSION);
    InitData.NumberOfAccessRanges = 1;
    InitData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    InitData.NeedPhysicalAddresses = TRUE;
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;

    return StorPortInitialize(DriverObject, RegistryPath, &InitData, NULL);
}
