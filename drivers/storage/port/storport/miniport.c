/*
 * PROJECT:     ReactOS Storport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Miniport interface code
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS ******************************************************************/

static
NTSTATUS
InitializeConfiguration(
    _In_ PPORT_CONFIGURATION_INFORMATION PortConfig,
    _In_ PHW_INITIALIZATION_DATA InitData,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber)
{
    PCONFIGURATION_INFORMATION ConfigInfo;
    ULONG i;

    DPRINT("InitializeConfiguration(%p %p %lu %lu)\n",
            PortConfig, InitData, BusNumber, SlotNumber);

    /* Get the configurration information */
    ConfigInfo = IoGetConfigurationInformation();

    /* Initialize the port configuration */
    RtlZeroMemory(PortConfig,
                  sizeof(PORT_CONFIGURATION_INFORMATION));

    PortConfig->Length = sizeof(PORT_CONFIGURATION_INFORMATION);
    PortConfig->SystemIoBusNumber = BusNumber;
    PortConfig->SlotNumber = SlotNumber;
    PortConfig->AdapterInterfaceType = InitData->AdapterInterfaceType;

    PortConfig->MaximumTransferLength = SP_UNINITIALIZED_VALUE;
    PortConfig->DmaChannel = SP_UNINITIALIZED_VALUE;
    PortConfig->DmaPort = SP_UNINITIALIZED_VALUE;

    PortConfig->InterruptMode = LevelSensitive;

    PortConfig->Master = TRUE;
    PortConfig->AtdiskPrimaryClaimed = ConfigInfo->AtDiskPrimaryAddressClaimed;
    PortConfig->AtdiskSecondaryClaimed = ConfigInfo->AtDiskSecondaryAddressClaimed;
    PortConfig->Dma32BitAddresses = TRUE;
    PortConfig->DemandMode = FALSE;
    PortConfig->MapBuffers = InitData->MapBuffers;

    PortConfig->NeedPhysicalAddresses = TRUE;
    PortConfig->TaggedQueuing = TRUE;
    PortConfig->AutoRequestSense = TRUE;
    PortConfig->MultipleRequestPerLu = TRUE;
    PortConfig->ReceiveEvent = InitData->ReceiveEvent;
    PortConfig->RealModeInitialized = FALSE;
    PortConfig->BufferAccessScsiPortControlled = TRUE;
    PortConfig->MaximumNumberOfTargets = SCSI_MAXIMUM_TARGETS_PER_BUS;

    PortConfig->SpecificLuExtensionSize = InitData->SpecificLuExtensionSize;
    PortConfig->SrbExtensionSize = InitData->SrbExtensionSize;
    PortConfig->MaximumNumberOfLogicalUnits = SCSI_MAXIMUM_LOGICAL_UNITS;
    PortConfig->WmiDataProvider = TRUE;

    PortConfig->NumberOfAccessRanges = InitData->NumberOfAccessRanges;
    DPRINT("NumberOfAccessRanges: %lu\n", PortConfig->NumberOfAccessRanges);
    if (PortConfig->NumberOfAccessRanges != 0)
    {
        PortConfig->AccessRanges = ExAllocatePoolWithTag(NonPagedPool,
                                                         PortConfig->NumberOfAccessRanges * sizeof(ACCESS_RANGE),
                                                         TAG_ACCRESS_RANGE);
        if (PortConfig->AccessRanges == NULL)
            return STATUS_NO_MEMORY;

        RtlZeroMemory(PortConfig->AccessRanges,
                      PortConfig->NumberOfAccessRanges * sizeof(ACCESS_RANGE));
    }

    for (i = 0; i < RTL_NUMBER_OF(PortConfig->InitiatorBusId); i++)
    {
        PortConfig->InitiatorBusId[i] = (CCHAR)SP_UNINITIALIZED_VALUE;
    }

    return STATUS_SUCCESS;
}


static
VOID
AssignResourcesToConfiguration(
    _Inout_ PMINIPORT Miniport,
    _In_ PCM_RESOURCE_LIST RawResourceList,
    _In_opt_ PCM_RESOURCE_LIST TranslatedResourceList,
    _In_ ULONG NumberOfAccessRanges)
{
    PPORT_CONFIGURATION_INFORMATION PortConfiguration;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PACCESS_RANGE AccessRange;
    INT i, j;
    ULONG RangeNumber = 0, Interrupt = 0, Dma = 0;

    PortConfiguration = &Miniport->PortConfig;

    for (ULONG idx = 0; idx < RTL_NUMBER_OF(Miniport->SystemInterruptValid); idx++)
    {
        Miniport->SystemInterruptVector[idx] = 0;
        Miniport->SystemInterruptIrql[idx] = 0;
        Miniport->SystemInterruptAffinity[idx] = 0;
        Miniport->SystemInterruptValid[idx] = FALSE;
        Miniport->FirmwareInterruptLine[idx] = 0;
    }

    DPRINT("AssignResourceToConfiguration(%p %p %lu)\n",
            PortConfiguration, RawResourceList, NumberOfAccessRanges);

    FullDescriptor = &RawResourceList->List[0];
    for (i = 0; i < RawResourceList->Count; i++)
    {
        PartialResourceList = &FullDescriptor->PartialResourceList;

        for (j = 0; j < PartialResourceList->Count; j++)
        {
            PartialDescriptor = &PartialResourceList->PartialDescriptors[j];

            switch (PartialDescriptor->Type)
            {
                case CmResourceTypePort:
                    DPRINT("Port: 0x%I64x (0x%lx)\n",
                            PartialDescriptor->u.Port.Start.QuadPart,
                            PartialDescriptor->u.Port.Length);
                    if (RangeNumber < NumberOfAccessRanges)
                    {
                        AccessRange = &((*(PortConfiguration->AccessRanges))[RangeNumber]);
                        AccessRange->RangeStart = PartialDescriptor->u.Port.Start;
                        AccessRange->RangeLength = PartialDescriptor->u.Port.Length;
                        AccessRange->RangeInMemory = FALSE;
                        RangeNumber++;
                    }
                    break;

                case CmResourceTypeMemory:
                    DPRINT("Memory: 0x%I64x (0x%lx)\n",
                            PartialDescriptor->u.Memory.Start.QuadPart,
                            PartialDescriptor->u.Memory.Length);
                    if (RangeNumber < NumberOfAccessRanges)
                    {
                        AccessRange = &((*(PortConfiguration->AccessRanges))[RangeNumber]);
                        AccessRange->RangeStart = PartialDescriptor->u.Memory.Start;
                        AccessRange->RangeLength = PartialDescriptor->u.Memory.Length;
                        AccessRange->RangeInMemory = TRUE;
                        RangeNumber++;
                    }
                    break;

                case CmResourceTypeInterrupt:
                {
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR TranslatedInterrupt = NULL;
                    ULONG firmwareLevel, firmwareVector;
                    ULONG systemLevel, systemVector;
                    KAFFINITY systemAffinity;
                    ULONG flags;

                    DPRINT("Interrupt: Level %lu  Vector %lu\n",
                            PartialDescriptor->u.Interrupt.Level,
                            PartialDescriptor->u.Interrupt.Vector);

                    firmwareLevel = PartialDescriptor->u.Interrupt.Level;
                    firmwareVector = PartialDescriptor->u.Interrupt.Vector;
                    systemLevel = firmwareLevel;
                    systemVector = firmwareVector;
                    systemAffinity = PartialDescriptor->u.Interrupt.Affinity;
                    flags = PartialDescriptor->Flags;

                    if (TranslatedResourceList)
                    {
                        ULONG found = 0;
                        PCM_FULL_RESOURCE_DESCRIPTOR TranslatedFull = &TranslatedResourceList->List[0];

                        for (INT ti = 0; ti < (INT)TranslatedResourceList->Count && !TranslatedInterrupt; ti++)
                        {
                            PCM_PARTIAL_RESOURCE_LIST TranslatedPartialList = &TranslatedFull->PartialResourceList;

                            for (INT tj = 0; tj < (INT)TranslatedPartialList->Count; tj++)
                            {
                                PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate = &TranslatedPartialList->PartialDescriptors[tj];

                                if (Candidate->Type != CmResourceTypeInterrupt)
                                    continue;

                                if (found == Interrupt)
                                {
                                    TranslatedInterrupt = Candidate;
                                    break;
                                }

                                found++;
                            }

                            TranslatedFull = (PCM_FULL_RESOURCE_DESCRIPTOR)(TranslatedFull->PartialResourceList.PartialDescriptors +
                                                                               TranslatedFull->PartialResourceList.Count);
                        }
                    }

                    if (TranslatedInterrupt)
                    {
                        systemLevel = TranslatedInterrupt->u.Interrupt.Level;
                        systemVector = TranslatedInterrupt->u.Interrupt.Vector;
                        if (TranslatedInterrupt->u.Interrupt.Affinity != 0)
                            systemAffinity = TranslatedInterrupt->u.Interrupt.Affinity;
                        flags = TranslatedInterrupt->Flags;
                    }

                    if (Interrupt < RTL_NUMBER_OF(Miniport->FirmwareInterruptLine))
                    {
                        Miniport->FirmwareInterruptLine[Interrupt] = firmwareLevel;
                        Miniport->SystemInterruptVector[Interrupt] = systemVector;
                        Miniport->SystemInterruptIrql[Interrupt] =
                            (systemLevel <= HIGH_LEVEL) ? (KIRQL)systemLevel : DISPATCH_LEVEL;
                        Miniport->SystemInterruptAffinity[Interrupt] = systemAffinity;
                        Miniport->SystemInterruptValid[Interrupt] = TRUE;

                        if (TranslatedInterrupt &&
                            ((systemLevel != firmwareLevel) || (systemVector != firmwareVector)))
                        {
                            DPRINT1("Storport: Interrupt %lu GSI %lu -> vector %lu (IRQL %u affinity %p)\n",
                                    Interrupt,
                                    firmwareLevel,
                                    systemVector,
                                    Miniport->SystemInterruptIrql[Interrupt],
                                    (PVOID)(ULONG_PTR)systemAffinity);
                        }
                    }

                    if (Interrupt == 0)
                    {
                        PortConfiguration->BusInterruptLevel = systemLevel;
                        PortConfiguration->BusInterruptVector = systemVector;
                        PortConfiguration->InterruptMode =
                            ((flags & CM_RESOURCE_INTERRUPT_LEVEL_LATCHED_BITS) == CM_RESOURCE_INTERRUPT_LATCHED)
                                ? Latched
                                : LevelSensitive;
                    }
                    else if (Interrupt == 1)
                    {
                        PortConfiguration->BusInterruptLevel2 = systemLevel;
                        PortConfiguration->BusInterruptVector2 = systemVector;
                        PortConfiguration->InterruptMode2 =
                            ((flags & CM_RESOURCE_INTERRUPT_LEVEL_LATCHED_BITS) == CM_RESOURCE_INTERRUPT_LATCHED)
                                ? Latched
                                : LevelSensitive;
                    }

                    Interrupt++;
                    break;
                }

                case CmResourceTypeDma:
                    DPRINT("Dma: Channel: %lu  Port: %lu\n",
                            PartialDescriptor->u.Dma.Channel,
                            PartialDescriptor->u.Dma.Port);
                    if (Dma == 0)
                    {
                        PortConfiguration->DmaChannel = PartialDescriptor->u.Dma.Channel;
                        PortConfiguration->DmaPort = PartialDescriptor->u.Dma.Port;

                        if (PartialDescriptor->Flags & CM_RESOURCE_DMA_8)
                            PortConfiguration->DmaWidth = Width8Bits;
                        else if ((PartialDescriptor->Flags & CM_RESOURCE_DMA_16) ||
                                 (PartialDescriptor->Flags & CM_RESOURCE_DMA_8_AND_16))
                            PortConfiguration->DmaWidth = Width16Bits;
                        else if (PartialDescriptor->Flags & CM_RESOURCE_DMA_32)
                            PortConfiguration->DmaWidth = Width32Bits;
                    }
                    else if (Dma == 1)
                    {
                        PortConfiguration->DmaChannel2 = PartialDescriptor->u.Dma.Channel;
                        PortConfiguration->DmaPort2 = PartialDescriptor->u.Dma.Port;

                        if (PartialDescriptor->Flags & CM_RESOURCE_DMA_8)
                            PortConfiguration->DmaWidth2 = Width8Bits;
                        else if ((PartialDescriptor->Flags & CM_RESOURCE_DMA_16) ||
                                 (PartialDescriptor->Flags & CM_RESOURCE_DMA_8_AND_16))
                            PortConfiguration->DmaWidth2 = Width16Bits;
                        else if (PartialDescriptor->Flags & CM_RESOURCE_DMA_32)
                            PortConfiguration->DmaWidth2 = Width32Bits;
                    }
                    Dma++;
                    break;

                default:
                    DPRINT("Other: %u\n", PartialDescriptor->Type);
                    break;
            }
        }

        /* Advance to next CM_FULL_RESOURCE_DESCRIPTOR block in memory. */
        FullDescriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)(FullDescriptor->PartialResourceList.PartialDescriptors +
                                                        FullDescriptor->PartialResourceList.Count);
    }
}


NTSTATUS
MiniportInitialize(
    _In_ PMINIPORT Miniport,
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PHW_INITIALIZATION_DATA InitData)
{
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
    ULONG Size;
    NTSTATUS Status;

    DPRINT("MiniportInitialize(%p %p %p)\n",
            Miniport, DeviceExtension, InitData);

    Miniport->DeviceExtension = DeviceExtension;
    Miniport->InitData = InitData;

    /* Calculate the miniport device extension size */
    Size = sizeof(MINIPORT_DEVICE_EXTENSION) +
           Miniport->InitData->DeviceExtensionSize;

    /* Allocate and initialize the miniport device extension */
    MiniportExtension = ExAllocatePoolWithTag(NonPagedPool,
                                              Size,
                                              TAG_MINIPORT_DATA);
    if (MiniportExtension == NULL)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(MiniportExtension, Size);
    RtlZeroMemory(Miniport->SystemInterruptVector, sizeof(Miniport->SystemInterruptVector));
    RtlZeroMemory(Miniport->SystemInterruptIrql, sizeof(Miniport->SystemInterruptIrql));
    RtlZeroMemory(Miniport->SystemInterruptAffinity, sizeof(Miniport->SystemInterruptAffinity));
    RtlZeroMemory(Miniport->SystemInterruptValid, sizeof(Miniport->SystemInterruptValid));
    RtlZeroMemory(Miniport->FirmwareInterruptLine, sizeof(Miniport->FirmwareInterruptLine));

    MiniportExtension->Miniport = Miniport;
    Miniport->MiniportExtension = MiniportExtension;

    /* Initialize the port configuration */
    Status = InitializeConfiguration(&Miniport->PortConfig,
                                     InitData,
                                     DeviceExtension->BusNumber,
                                     DeviceExtension->SlotNumber);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Assign the resources to the port configuration */
    AssignResourcesToConfiguration(Miniport,
                                   DeviceExtension->AllocatedResources,
                                   DeviceExtension->TranslatedResources,
                                   InitData->NumberOfAccessRanges);

    return STATUS_SUCCESS;
}


NTSTATUS
MiniportFindAdapter(
    _In_ PMINIPORT Miniport)
{
    BOOLEAN Reserved = FALSE;
    ULONG Result;
    NTSTATUS Status;

    DPRINT("MiniportFindAdapter(%p)\n", Miniport);

    /* Call the miniport HwFindAdapter routine */
    Result = Miniport->InitData->HwFindAdapter(&Miniport->MiniportExtension->HwDeviceExtension,
                                               NULL,
                                               NULL,
                                               NULL,
                                               &Miniport->PortConfig,
                                               &Reserved);
    DPRINT("HwFindAdapter() returned %lu\n", Result);

    /* Convert the result to a status code */
    switch (Result)
    {
        case SP_RETURN_NOT_FOUND:
            DPRINT("SP_RETURN_NOT_FOUND\n");
            Status = STATUS_NOT_FOUND;
            break;

        case SP_RETURN_FOUND:
            DPRINT("SP_RETURN_FOUND\n");
            Status = STATUS_SUCCESS;
            break;

        case SP_RETURN_ERROR:
            DPRINT1("SP_RETURN_ERROR\n");
            Status = STATUS_ADAPTER_HARDWARE_ERROR;
            break;

        case SP_RETURN_BAD_CONFIG:
            DPRINT("SP_RETURN_BAD_CONFIG\n");
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;

        default:
            DPRINT("Unknown result: %lu\n", Result);
            Status = STATUS_INTERNAL_ERROR;
            break;
    }

    return Status;
}


NTSTATUS
MiniportHwInitialize(
    _In_ PMINIPORT Miniport)
{
    BOOLEAN Result;

    DPRINT("MiniportHwInitialize(%p)\n", Miniport);

    /* Call the miniport HwInitialize routine */
    Result = Miniport->InitData->HwInitialize(&Miniport->MiniportExtension->HwDeviceExtension);
    DPRINT("HwInitialize() returned %u\n", Result);

    return Result ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}


BOOLEAN
MiniportHwInterrupt(
    _In_ PMINIPORT Miniport)
{
    BOOLEAN Result;

    Result = Miniport->InitData->HwInterrupt(&Miniport->MiniportExtension->HwDeviceExtension);

    return Result;
}


BOOLEAN
MiniportStartIo(
    _In_ PMINIPORT Miniport,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    BOOLEAN Result;

    Result = Miniport->InitData->HwStartIo(&Miniport->MiniportExtension->HwDeviceExtension, Srb);

    return Result;
}

/* EOF */
