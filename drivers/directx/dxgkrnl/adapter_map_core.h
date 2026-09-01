/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Testable adapter MapMemory range and owner validation
 */

#ifndef _DXGK_ADAPTER_MAP_CORE_H_
#define _DXGK_ADAPTER_MAP_CORE_H_

#include <ntddk.h>

BOOLEAN
DxgkAdapterMapRangeAssigned(
    _In_opt_ PCM_RESOURCE_LIST TranslatedResources,
    _In_ PHYSICAL_ADDRESS TranslatedAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN InIoSpace);

BOOLEAN
DxgkAdapterMapOwnerMatches(
    _In_ PVOID OwnerAdapter,
    _In_opt_ PVOID OwnerProcess,
    _In_ PVOID CallerAdapter,
    _In_opt_ PVOID CallerProcess);

#endif /* _DXGK_ADAPTER_MAP_CORE_H_ */
