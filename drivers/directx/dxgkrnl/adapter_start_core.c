/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Pure adapter-start role policy shared with kernel tests
 */

#include "adapter_start_core.h"

DXGK_ADAPTER_START_ROLE
DxgkAdapterStartClassifyRole(
    _In_ BOOLEAN IsDisplayOnlyDriver,
    _In_ ULONG NumberOfVideoPresentSources)
{
    if (IsDisplayOnlyDriver)
        return DxgkAdapterStartDisplayOnly;
    return NumberOfVideoPresentSources == 0 ? DxgkAdapterStartRenderOnly : DxgkAdapterStartFullDisplay;
}

BOOLEAN
DxgkAdapterStartRoleRequiresScheduler(
    _In_ DXGK_ADAPTER_START_ROLE Role)
{
    return Role == DxgkAdapterStartFullDisplay || Role == DxgkAdapterStartRenderOnly;
}

BOOLEAN
DxgkAdapterStartRoleRequiresDisplayPipeline(
    _In_ DXGK_ADAPTER_START_ROLE Role)
{
    return Role == DxgkAdapterStartDisplayOnly || Role == DxgkAdapterStartFullDisplay;
}

BOOLEAN
DxgkAdapterStartRoleHasValidCounts(
    _In_ DXGK_ADAPTER_START_ROLE Role,
    _In_ ULONG NumberOfVideoPresentSources,
    _In_ ULONG NodeCount,
    _In_ ULONG MaximumNodeCount)
{
    if (MaximumNodeCount == 0 || NodeCount > MaximumNodeCount)
        return FALSE;
    if (Role == DxgkAdapterStartDisplayOnly)
        return NumberOfVideoPresentSources != 0 && NodeCount == 0;
    if (Role == DxgkAdapterStartFullDisplay)
        return NumberOfVideoPresentSources != 0 && NodeCount != 0;
    if (Role == DxgkAdapterStartRenderOnly)
        return NumberOfVideoPresentSources == 0 && NodeCount != 0;
    return FALSE;
}
