/*
 * PROJECT:     ReactOS Storport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Storport helper functions
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
 */

/* INCLUDES *******************************************************************/

#include "precomp.h"

#define NDEBUG
#include <debug.h>


/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
ForwardIrpAndForget(
    _In_ PDEVICE_OBJECT LowerDevice,
    _In_ PIRP Irp)
{
    ASSERT(LowerDevice);

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(LowerDevice, Irp);
}


INTERFACE_TYPE
GetBusInterface(
    PDEVICE_OBJECT DeviceObject)
{
    GUID Guid;
    ULONG Length;
    NTSTATUS Status;

    Status = IoGetDeviceProperty(DeviceObject,
                                 DevicePropertyBusTypeGuid,
                                 sizeof(Guid),
                                 &Guid,
                                 &Length);
    if (!NT_SUCCESS(Status))
        return InterfaceTypeUndefined;

    if (RtlCompareMemory(&Guid, &GUID_BUS_TYPE_PCMCIA, sizeof(GUID)) == sizeof(GUID))
        return PCMCIABus;
    else if (RtlCompareMemory(&Guid, &GUID_BUS_TYPE_PCI, sizeof(GUID)) == sizeof(GUID))
        return PCIBus;
    else if (RtlCompareMemory(&Guid, &GUID_BUS_TYPE_ISAPNP, sizeof(GUID)) == sizeof(GUID))
        return PNPISABus;

    return InterfaceTypeUndefined;
}


static
ULONG
GetResourceListSize(
    PCM_RESOURCE_LIST ResourceList)
{
    PCM_FULL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Size;

    DPRINT("GetResourceListSize(%p)\n", ResourceList);

    Size = sizeof(CM_RESOURCE_LIST);
    if (ResourceList->Count == 0)
    {
        DPRINT("Size: 0x%lx (%u)\n", Size, Size);
        return Size;
    }

    DPRINT("ResourceList->Count: %lu\n", ResourceList->Count);

    Descriptor = &ResourceList->List[0];

    DPRINT("PartialResourceList->Count: %lu\n", Descriptor->PartialResourceList.Count);

    /* Add the size of the partial descriptors */
    if (Descriptor->PartialResourceList.Count > 1)
        Size += (Descriptor->PartialResourceList.Count - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    DPRINT("Size: 0x%lx (%u)\n", Size, Size);
    return Size;
}


PCM_RESOURCE_LIST
CopyResourceList(
    POOL_TYPE PoolType,
    PCM_RESOURCE_LIST Source)
{
    PCM_RESOURCE_LIST Destination;
    ULONG Size;

    DPRINT("CopyResourceList(%lu %p)\n",
            PoolType, Source);

    /* Get the size of the resource list */
    Size = GetResourceListSize(Source);

    /* Allocate a new buffer */
    Destination = ExAllocatePoolWithTag(PoolType,
                                        Size,
                                        TAG_RESOURCE_LIST);
    if (Destination == NULL)
        return NULL;

    /* Copy the resource list */
    RtlCopyMemory(Destination,
                  Source,
                  Size);

    return Destination;
}


NTSTATUS
QueryBusInterface(
    PDEVICE_OBJECT DeviceObject,
    PGUID Guid,
    USHORT Size,
    USHORT Version,
    PBUS_INTERFACE_STANDARD Interface,
    PVOID InterfaceSpecificData)
{
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;

    /*
     * This function performs synchronous I/O with infinite wait, which requires
     * PASSIVE_LEVEL IRQL. PnP interface queries should normally be at PASSIVE_LEVEL.
     * We check IRQL before proceeding to avoid assertion failures in KeWaitForSingleObject.
     */
    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
    {
        DPRINT1("QueryBusInterface: ERROR - Called at IRQL %u, requires PASSIVE_LEVEL for synchronous I/O\n",
                KeGetCurrentIrql());
        return STATUS_UNSUCCESSFUL;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       DeviceObject,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Stack = IoGetNextIrpStackLocation(Irp);

    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = Guid;
    Stack->Parameters.QueryInterface.Size = Size;
    Stack->Parameters.QueryInterface.Version = Version;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = InterfaceSpecificData;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

        Status=IoStatus.Status;
    }

    return Status;
}


BOOLEAN
TranslateResourceListAddress(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    INTERFACE_TYPE BusType,
    ULONG SystemIoBusNumber,
    STOR_PHYSICAL_ADDRESS IoAddress,
    ULONG NumberOfBytes,
    BOOLEAN InIoSpace,
    PPHYSICAL_ADDRESS TranslatedAddress)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptorA, FullDescriptorT;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptorA, PartialDescriptorT;
    INT i, j;

    DPRINT("TranslateResourceListAddress(%p)\n", DeviceExtension);

    FullDescriptorA = DeviceExtension->AllocatedResources->List;
    FullDescriptorT = DeviceExtension->TranslatedResources->List;
    for (i = 0; i < DeviceExtension->AllocatedResources->Count; i++)
    {
        for (j = 0; j < FullDescriptorA->PartialResourceList.Count; j++)
        {
            PartialDescriptorA = FullDescriptorA->PartialResourceList.PartialDescriptors + j;
            PartialDescriptorT = FullDescriptorT->PartialResourceList.PartialDescriptors + j;

            switch (PartialDescriptorA->Type)
            {
                case CmResourceTypePort:
                    DPRINT("Port: 0x%I64x (0x%lx)\n",
                            PartialDescriptorA->u.Port.Start.QuadPart,
                            PartialDescriptorA->u.Port.Length);
                    if (InIoSpace &&
                        IoAddress.QuadPart >= PartialDescriptorA->u.Port.Start.QuadPart &&
                        IoAddress.QuadPart + NumberOfBytes <= PartialDescriptorA->u.Port.Start.QuadPart + PartialDescriptorA->u.Port.Length)
                    {
                        TranslatedAddress->QuadPart = PartialDescriptorT->u.Port.Start.QuadPart +
                                                      (IoAddress.QuadPart - PartialDescriptorA->u.Port.Start.QuadPart);
                        return TRUE;
                    }
                    break;

                case CmResourceTypeMemory:
                    DPRINT("Memory: 0x%I64x (0x%lx)\n",
                            PartialDescriptorA->u.Memory.Start.QuadPart,
                            PartialDescriptorA->u.Memory.Length);
                    if (!InIoSpace &&
                        IoAddress.QuadPart >= PartialDescriptorA->u.Memory.Start.QuadPart &&
                        IoAddress.QuadPart + NumberOfBytes <= PartialDescriptorA->u.Memory.Start.QuadPart + PartialDescriptorA->u.Memory.Length)
                    {
                        TranslatedAddress->QuadPart = PartialDescriptorT->u.Memory.Start.QuadPart +
                                                      (IoAddress.QuadPart - PartialDescriptorA->u.Memory.Start.QuadPart);
                        return TRUE;
                    }
                    break;
            }
        }

        /* Advance to next CM_FULL_RESOURCE_DESCRIPTOR block in memory. */
        FullDescriptorA = (PCM_FULL_RESOURCE_DESCRIPTOR)(FullDescriptorA->PartialResourceList.PartialDescriptors +
                                                         FullDescriptorA->PartialResourceList.Count);

        FullDescriptorT = (PCM_FULL_RESOURCE_DESCRIPTOR)(FullDescriptorT->PartialResourceList.PartialDescriptors +
                                                         FullDescriptorT->PartialResourceList.Count);
    }

    return FALSE;
}


NTSTATUS
GetResourceListInterrupt(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    PULONG Vector,
    PKIRQL Irql,
    KINTERRUPT_MODE *InterruptMode,
    PBOOLEAN ShareVector,
    PKAFFINITY Affinity)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PMINIPORT Miniport;
    ULONG interruptIndex = 0;
    INT i, j;

    DPRINT("GetResourceListInterrupt(%p)\n",
            DeviceExtension);

    Miniport = &DeviceExtension->Miniport;

    FullDescriptor = DeviceExtension->TranslatedResources->List;
    for (i = 0; i < DeviceExtension->TranslatedResources->Count; i++)
    {
        for (j = 0; j < FullDescriptor->PartialResourceList.Count; j++)
        {
            PartialDescriptor = FullDescriptor->PartialResourceList.PartialDescriptors + j;

            switch (PartialDescriptor->Type)
            {
                case CmResourceTypeInterrupt:
                    DPRINT("Interrupt: Level %lu  Vector %lu\n",
                            PartialDescriptor->u.Interrupt.Level,
                            PartialDescriptor->u.Interrupt.Vector);

                    if ((interruptIndex < RTL_NUMBER_OF(Miniport->SystemInterruptValid)) &&
                        Miniport->SystemInterruptValid[interruptIndex])
                    {
                        *Vector = Miniport->SystemInterruptVector[interruptIndex];
                        *Irql = Miniport->SystemInterruptIrql[interruptIndex];
                        if (*Irql == 0)
                        {
                            *Irql = (KIRQL)PartialDescriptor->u.Interrupt.Level;
                        }
                        *Affinity = Miniport->SystemInterruptAffinity[interruptIndex];
                        if (*Affinity == 0)
                        {
                            *Affinity = PartialDescriptor->u.Interrupt.Affinity;
                        }
                    }
                    else
                    {
                        *Vector = PartialDescriptor->u.Interrupt.Vector;
                        *Irql = (KIRQL)PartialDescriptor->u.Interrupt.Level;
                        *Affinity = PartialDescriptor->u.Interrupt.Affinity;
                    }

                    *InterruptMode = (PartialDescriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive;
                    *ShareVector = (PartialDescriptor->ShareDisposition == CmResourceShareShared) ? TRUE : FALSE;

                    interruptIndex++;
                    return STATUS_SUCCESS;
            }
        }

        /* Advance to next CM_FULL_RESOURCE_DESCRIPTOR block in memory. */
        FullDescriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)(FullDescriptor->PartialResourceList.PartialDescriptors +
                                                        FullDescriptor->PartialResourceList.Count);
    }

    return STATUS_NOT_FOUND;
}


NTSTATUS
AllocateAddressMapping(
    PMAPPED_ADDRESS *MappedAddressList,
    STOR_PHYSICAL_ADDRESS IoAddress,
    PVOID MappedAddress,
    ULONG NumberOfBytes,
    ULONG BusNumber)
{
    PMAPPED_ADDRESS Mapping;

    DPRINT("AllocateAddressMapping()\n");

    Mapping = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(MAPPED_ADDRESS),
                                    TAG_ADDRESS_MAPPING);
    if (Mapping == NULL)
    {
        DPRINT("No memory!\n");
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Mapping, sizeof(MAPPED_ADDRESS));

    Mapping->NextMappedAddress = *MappedAddressList;
    *MappedAddressList = Mapping;

    Mapping->IoAddress = IoAddress;
    Mapping->MappedAddress = MappedAddress;
    Mapping->NumberOfBytes = NumberOfBytes;
    Mapping->BusNumber = BusNumber;

    return STATUS_SUCCESS;
}

/* EOF */
