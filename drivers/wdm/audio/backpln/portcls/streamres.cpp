/*
 * PROJECT:     LiberNT Port Class
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stream resource registration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.hpp"

#define TAG_PORTCLASS_STREAMRES 'RScP'

typedef struct _PC_STREAM_RESOURCE
{
    LIST_ENTRY ListEntry;
    ULONG Signature;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PCSTREAMRESOURCE_DESCRIPTOR Descriptor;
} PC_STREAM_RESOURCE, *PPC_STREAM_RESOURCE;

static LIST_ENTRY PcStreamResourceList = {&PcStreamResourceList, &PcStreamResourceList};
static KSPIN_LOCK PcStreamResourceLock;

NTSTATUS
NTAPI
PcAddStreamResource(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    IN PVOID ResourceSet,
    IN PPCSTREAMRESOURCE_DESCRIPTOR ResourceDescriptor,
    OUT PCSTREAMRESOURCE *ResourceHandle)
{
    PPC_STREAM_RESOURCE Resource;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(ResourceSet);

    if (ResourceHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    *ResourceHandle = NULL;

    if ((PhysicalDeviceObject == NULL) || (ResourceDescriptor == NULL) ||
        (ResourceDescriptor->Size < sizeof(PCSTREAMRESOURCE_DESCRIPTOR)) ||
        (ResourceDescriptor->Flags != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (ResourceDescriptor->Type)
    {
        case ePcStreamResourceInterrupt:
            if (ResourceDescriptor->Resource.Interrupt.Generic == NULL)
                return STATUS_INVALID_PARAMETER;
            break;

        case ePcStreamResourceThread:
            if (ResourceDescriptor->Resource.Thread == NULL)
                return STATUS_INVALID_PARAMETER;
            break;

        case ePcStreamResourceSet:
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    Resource = (PPC_STREAM_RESOURCE)AllocateItem(NonPagedPool, sizeof(*Resource), TAG_PORTCLASS_STREAMRES);
    if (Resource == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Resource->Signature = TAG_PORTCLASS_STREAMRES;
    Resource->PhysicalDeviceObject = PhysicalDeviceObject;
    Resource->Descriptor = *ResourceDescriptor;
    if (ResourceDescriptor->Type == ePcStreamResourceThread)
        ObReferenceObject(ResourceDescriptor->Resource.Thread);

    KeAcquireSpinLock(&PcStreamResourceLock, &OldIrql);
    InsertTailList(&PcStreamResourceList, &Resource->ListEntry);
    KeReleaseSpinLock(&PcStreamResourceLock, OldIrql);

    *ResourceHandle = (PCSTREAMRESOURCE)Resource;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PcRemoveStreamResource(
    IN PCSTREAMRESOURCE ResourceHandle)
{
    PPC_STREAM_RESOURCE Resource = (PPC_STREAM_RESOURCE)ResourceHandle;
    KIRQL OldIrql;

    if ((Resource == NULL) || (Resource->Signature != TAG_PORTCLASS_STREAMRES))
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&PcStreamResourceLock, &OldIrql);
    RemoveEntryList(&Resource->ListEntry);
    Resource->Signature = 0;
    KeReleaseSpinLock(&PcStreamResourceLock, OldIrql);

    if (Resource->Descriptor.Type == ePcStreamResourceThread)
        ObDereferenceObject(Resource->Descriptor.Resource.Thread);

    FreeItem(Resource, TAG_PORTCLASS_STREAMRES);
    return STATUS_SUCCESS;
}
