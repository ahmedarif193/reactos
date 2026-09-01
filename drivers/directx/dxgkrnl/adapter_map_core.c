/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Testable adapter MapMemory range and owner validation
 */

#include "adapter_map_core.h"
#include <reactos/drivers/cmreslist.h>

BOOLEAN
DxgkAdapterMapRangeAssigned(
    _In_opt_ PCM_RESOURCE_LIST TranslatedResources,
    _In_ PHYSICAL_ADDRESS TranslatedAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN InIoSpace)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    ULONGLONG RequestStart;
    ULONGLONG RequestEnd;
    ULONG FullIndex;

    if (TranslatedResources == NULL || Length == 0 || TranslatedAddress.QuadPart < 0)
        return FALSE;

    RequestStart = (ULONGLONG)TranslatedAddress.QuadPart;
    if (RequestStart > MAXULONGLONG - Length)
        return FALSE;
    RequestEnd = RequestStart + Length;

    FullDescriptor = &TranslatedResources->List[0];
    for (FullIndex = 0; FullIndex < TranslatedResources->Count; ++FullIndex)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
        ULONG PartialIndex;

        PartialDescriptor = &FullDescriptor->PartialResourceList.PartialDescriptors[0];
        for (PartialIndex = 0; PartialIndex < FullDescriptor->PartialResourceList.Count; ++PartialIndex)
        {
            ULONGLONG ResourceStart = 0;
            ULONGLONG ResourceLength = 0;
            ULONGLONG ResourceEnd;
            BOOLEAN MatchingSpace = FALSE;

            if (PartialDescriptor->Type == CmResourceTypePort)
            {
                BOOLEAN PortIoSpace = (PartialDescriptor->Flags & CM_RESOURCE_PORT_IO) != 0;

                MatchingSpace = (InIoSpace == PortIoSpace);
                ResourceStart = (ULONGLONG)PartialDescriptor->u.Port.Start.QuadPart;
                ResourceLength = PartialDescriptor->u.Port.Length;
            }
            else if (!InIoSpace && PartialDescriptor->Type == CmResourceTypeMemory)
            {
                MatchingSpace = TRUE;
                ResourceStart = (ULONGLONG)PartialDescriptor->u.Memory.Start.QuadPart;
                ResourceLength = PartialDescriptor->u.Memory.Length;
            }
            else if (!InIoSpace && PartialDescriptor->Type == CmResourceTypeMemoryLarge)
            {
                MatchingSpace = TRUE;
                ResourceStart = (ULONGLONG)PartialDescriptor->u.Memory.Start.QuadPart;
                if ((PartialDescriptor->Flags & CM_RESOURCE_MEMORY_LARGE) == CM_RESOURCE_MEMORY_LARGE_40)
                    ResourceLength = (ULONGLONG)PartialDescriptor->u.Memory40.Length40 << 8;
                else if ((PartialDescriptor->Flags & CM_RESOURCE_MEMORY_LARGE) == CM_RESOURCE_MEMORY_LARGE_48)
                    ResourceLength = (ULONGLONG)PartialDescriptor->u.Memory48.Length48 << 16;
                else if ((PartialDescriptor->Flags & CM_RESOURCE_MEMORY_LARGE) == CM_RESOURCE_MEMORY_LARGE_64)
                    ResourceLength = (ULONGLONG)PartialDescriptor->u.Memory64.Length64 << 32;
                else
                    MatchingSpace = FALSE;
            }

            if (MatchingSpace && ResourceLength != 0 && ResourceStart <= MAXULONGLONG - ResourceLength)
            {
                ResourceEnd = ResourceStart + ResourceLength;
                if (RequestStart >= ResourceStart && RequestEnd <= ResourceEnd)
                    return TRUE;
            }

            PartialDescriptor = CmiGetNextPartialDescriptor(PartialDescriptor);
        }

        FullDescriptor = CmiGetNextResourceDescriptor(FullDescriptor);
    }

    return FALSE;
}

BOOLEAN
DxgkAdapterMapOwnerMatches(
    _In_ PVOID OwnerAdapter,
    _In_opt_ PVOID OwnerProcess,
    _In_ PVOID CallerAdapter,
    _In_opt_ PVOID CallerProcess)
{
    if (OwnerAdapter == NULL || CallerAdapter == NULL || OwnerAdapter != CallerAdapter)
        return FALSE;

    return OwnerProcess == NULL || OwnerProcess == CallerProcess;
}
