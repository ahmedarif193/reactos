/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Pure adapter-start role policy shared with kernel tests
 */

#pragma once

#include <ntdef.h>

typedef enum _DXGK_ADAPTER_START_ROLE
{
    DxgkAdapterStartDisplayOnly = 0,
    DxgkAdapterStartFullDisplay,
    DxgkAdapterStartRenderOnly
} DXGK_ADAPTER_START_ROLE;

DXGK_ADAPTER_START_ROLE
DxgkAdapterStartClassifyRole(
    _In_ BOOLEAN IsDisplayOnlyDriver,
    _In_ ULONG NumberOfVideoPresentSources);

BOOLEAN
DxgkAdapterStartRoleRequiresScheduler(
    _In_ DXGK_ADAPTER_START_ROLE Role);

BOOLEAN
DxgkAdapterStartRoleRequiresDisplayPipeline(
    _In_ DXGK_ADAPTER_START_ROLE Role);

BOOLEAN
DxgkAdapterStartRoleHasValidCounts(
    _In_ DXGK_ADAPTER_START_ROLE Role,
    _In_ ULONG NumberOfVideoPresentSources,
    _In_ ULONG NodeCount,
    _In_ ULONG MaximumNodeCount);
