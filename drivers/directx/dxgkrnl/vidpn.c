/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN (Video Present Network) topology management
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * This file implements the VidPN manager subsystem for dxgkrnl.  It provides
 * the interface tables that miniport drivers (particularly DOD drivers like
 * KMDOD and viogpudo) use during mode enumeration and commitment:
 *
 *   DxgkCbQueryVidPnInterface    -> DXGK_VIDPN_INTERFACE
 *     pfnGetTopology             -> DXGK_VIDPNTOPOLOGY_INTERFACE
 *     pfnAcquireSourceModeSet    -> DXGK_VIDPNSOURCEMODESET_INTERFACE
 *     pfnAcquireTargetModeSet    -> DXGK_VIDPNTARGETMODESET_INTERFACE
 *
 *   DxgkCbQueryMonitorInterface  -> Monitor interface with
 *     pfnAcquireMonitorSourceModeSet -> DXGK_MONITORSOURCEMODESET_INTERFACE
 *
 * Multi-monitor support:
 *   - Up to DXGKP_MAX_SOURCES sources and DXGKP_MAX_TARGETS targets
 *   - Multiple paths in topology (clone, extend, single)
 *   - Per-source and per-target mode sets with pinning
 *   - VidPN cloning for miniport negotiation
 *   - Hot-plug detection and VidPN rebuild
 *   - VidPN source ownership tracking
 *
 * Memory ordering notes (x86-64):
 *   All VidPN operations run at PASSIVE_LEVEL under serialisation provided
 *   by the calling DDI path (IsSupportedVidPn, EnumVidPnCofuncModality,
 *   CommitVidPn are all PASSIVE_LEVEL and serialised by dxgkrnl's dispatch).
 *   No additional memory barriers are needed beyond the implicit TSO ordering
 *   provided by x86-64.
 */

#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidpn.h"
#include "present.h"
#include "pnp.h"
#include "hotplug_work_core.h"
#include <reactos/dwmframe.h>

/* ========================================================================
 * Forward declarations for all interface functions
 * ====================================================================== */

/* Topology interface */
static NTSTATUS APIENTRY VidPnTopology_GetNumPaths(D3DKMDT_HVIDPNTOPOLOGY, SIZE_T*);
static NTSTATUS APIENTRY VidPnTopology_GetNumPathsFromSource(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, SIZE_T*);
static NTSTATUS APIENTRY VidPnTopology_EnumPathTargetsFromSource(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_VIDPN_PRESENT_PATH_INDEX, D3DDDI_VIDEO_PRESENT_TARGET_ID*);
static NTSTATUS APIENTRY VidPnTopology_GetPathSourceFromTarget(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DDDI_VIDEO_PRESENT_SOURCE_ID*);
static NTSTATUS APIENTRY VidPnTopology_AcquirePathInfo(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DDDI_VIDEO_PRESENT_TARGET_ID, CONST D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_AcquireFirstPathInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_AcquireNextPathInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH*, CONST D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_UpdatePathSupportInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH*);
static NTSTATUS APIENTRY VidPnTopology_ReleasePathInfo(D3DKMDT_HVIDPNTOPOLOGY, CONST D3DKMDT_VIDPN_PRESENT_PATH*);
static NTSTATUS APIENTRY VidPnTopology_CreateNewPathInfo(D3DKMDT_HVIDPNTOPOLOGY, D3DKMDT_VIDPN_PRESENT_PATH**);
static NTSTATUS APIENTRY VidPnTopology_AddPath(D3DKMDT_HVIDPNTOPOLOGY, D3DKMDT_VIDPN_PRESENT_PATH*);
static NTSTATUS APIENTRY VidPnTopology_RemovePath(D3DKMDT_HVIDPNTOPOLOGY, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DDDI_VIDEO_PRESENT_TARGET_ID);

/* Source mode set interface */
static NTSTATUS APIENTRY VidPnSourceModeSet_GetNumModes(D3DKMDT_HVIDPNSOURCEMODESET, CONST SIZE_T*);
static NTSTATUS APIENTRY VidPnSourceModeSet_AcquireFirstModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_AcquireNextModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE*, CONST D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_AcquirePinnedModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_ReleaseModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, CONST D3DKMDT_VIDPN_SOURCE_MODE*);
static NTSTATUS APIENTRY VidPnSourceModeSet_CreateNewModeInfo(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDPN_SOURCE_MODE**);
static NTSTATUS APIENTRY VidPnSourceModeSet_AddMode(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDPN_SOURCE_MODE*);
static NTSTATUS APIENTRY VidPnSourceModeSet_PinMode(D3DKMDT_HVIDPNSOURCEMODESET, D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID);

/* Target mode set interface */
static NTSTATUS APIENTRY VidPnTargetModeSet_GetNumModes(D3DKMDT_HVIDPNTARGETMODESET, CONST SIZE_T*);
static NTSTATUS APIENTRY VidPnTargetModeSet_AcquireFirstModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_AcquireNextModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE*, CONST D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_AcquirePinnedModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_ReleaseModeInfo(D3DKMDT_HVIDPNTARGETMODESET, CONST D3DKMDT_VIDPN_TARGET_MODE*);
static NTSTATUS APIENTRY VidPnTargetModeSet_CreateNewModeInfo(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDPN_TARGET_MODE**);
static NTSTATUS APIENTRY VidPnTargetModeSet_AddMode(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDPN_TARGET_MODE*);
static NTSTATUS APIENTRY VidPnTargetModeSet_PinMode(D3DKMDT_HVIDPNTARGETMODESET, D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID);

/* Monitor source mode set interface */
static NTSTATUS APIENTRY MonitorSourceModeSet_GetNumModes(D3DKMDT_HMONITORSOURCEMODESET, SIZE_T*);
static NTSTATUS APIENTRY MonitorSourceModeSet_AcquirePreferredModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_AcquireFirstModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_AcquireNextModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE*, CONST D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_CreateNewModeInfo(D3DKMDT_HMONITORSOURCEMODESET, D3DKMDT_MONITOR_SOURCE_MODE**);
static NTSTATUS APIENTRY MonitorSourceModeSet_AddMode(D3DKMDT_HMONITORSOURCEMODESET, D3DKMDT_MONITOR_SOURCE_MODE*);
static NTSTATUS APIENTRY MonitorSourceModeSet_ReleaseModeInfo(D3DKMDT_HMONITORSOURCEMODESET, CONST D3DKMDT_MONITOR_SOURCE_MODE*);

/* Top-level VidPN interface */
static NTSTATUS APIENTRY VidPn_GetTopology(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTOPOLOGY*, CONST DXGK_VIDPNTOPOLOGY_INTERFACE**);
static NTSTATUS APIENTRY VidPn_AcquireSourceModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_HVIDPNSOURCEMODESET*, CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_ReleaseSourceModeSet(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNSOURCEMODESET);
static NTSTATUS APIENTRY VidPn_CreateNewSourceModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_HVIDPNSOURCEMODESET*, CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_AssignSourceModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, D3DKMDT_HVIDPNSOURCEMODESET);
static NTSTATUS APIENTRY VidPn_AssignMultisamplingMethodSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_SOURCE_ID, CONST D3DDDI_MULTISAMPLINGMETHOD*);
static NTSTATUS APIENTRY VidPn_AcquireTargetModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HVIDPNTARGETMODESET*, CONST DXGK_VIDPNTARGETMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_ReleaseTargetModeSet(D3DKMDT_HVIDPN, D3DKMDT_HVIDPNTARGETMODESET);
static NTSTATUS APIENTRY VidPn_CreateNewTargetModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HVIDPNTARGETMODESET*, CONST DXGK_VIDPNTARGETMODESET_INTERFACE**);
static NTSTATUS APIENTRY VidPn_AssignTargetModeSet(D3DKMDT_HVIDPN, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HVIDPNTARGETMODESET);

/* Monitor interface */
static NTSTATUS APIENTRY Monitor_AcquireMonitorSourceModeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HMONITORSOURCEMODESET*, CONST DXGK_MONITORSOURCEMODESET_INTERFACE**);
static NTSTATUS APIENTRY Monitor_ReleaseMonitorSourceModeSet(D3DKMDT_ADAPTER, D3DKMDT_HMONITORSOURCEMODESET);
static NTSTATUS APIENTRY Monitor_GetMonitorFrequencyRangeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HMONITORFREQUENCYRANGESET*, CONST DXGK_MONITORFREQUENCYRANGESET_INTERFACE**);
static NTSTATUS APIENTRY Monitor_GetMonitorDescriptorSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HMONITORDESCRIPTORSET*, CONST DXGK_MONITORDESCRIPTORSET_INTERFACE**);
static NTSTATUS APIENTRY Monitor_GetAdditionalMonitorModeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, UINT*, DXGK_TARGETMODE_DETAIL_TIMING**);
static NTSTATUS APIENTRY Monitor_ReleaseAdditionalMonitorModeSet(D3DKMDT_ADAPTER, D3DDDI_VIDEO_PRESENT_TARGET_ID, CONST DXGK_TARGETMODE_DETAIL_TIMING*);
static VOID DxgkpDestroySharedPrimaryLocked(PDXGKRNL_ADAPTER);

/* ========================================================================
 * Static interface tables
 * ====================================================================== */

static CONST DXGK_VIDPNTOPOLOGY_INTERFACE g_VidPnTopologyInterface =
{
    VidPnTopology_GetNumPaths,
    VidPnTopology_GetNumPathsFromSource,
    VidPnTopology_EnumPathTargetsFromSource,
    VidPnTopology_GetPathSourceFromTarget,
    VidPnTopology_AcquirePathInfo,
    VidPnTopology_AcquireFirstPathInfo,
    VidPnTopology_AcquireNextPathInfo,
    VidPnTopology_UpdatePathSupportInfo,
    VidPnTopology_ReleasePathInfo,
    VidPnTopology_CreateNewPathInfo,
    VidPnTopology_AddPath,
    VidPnTopology_RemovePath,
};

static CONST DXGK_VIDPNSOURCEMODESET_INTERFACE g_VidPnSourceModeSetInterface =
{
    VidPnSourceModeSet_GetNumModes,
    VidPnSourceModeSet_AcquireFirstModeInfo,
    VidPnSourceModeSet_AcquireNextModeInfo,
    VidPnSourceModeSet_AcquirePinnedModeInfo,
    VidPnSourceModeSet_ReleaseModeInfo,
    VidPnSourceModeSet_CreateNewModeInfo,
    VidPnSourceModeSet_AddMode,
    VidPnSourceModeSet_PinMode,
};

static CONST DXGK_VIDPNTARGETMODESET_INTERFACE g_VidPnTargetModeSetInterface =
{
    VidPnTargetModeSet_GetNumModes,
    VidPnTargetModeSet_AcquireFirstModeInfo,
    VidPnTargetModeSet_AcquireNextModeInfo,
    VidPnTargetModeSet_AcquirePinnedModeInfo,
    VidPnTargetModeSet_ReleaseModeInfo,
    VidPnTargetModeSet_CreateNewModeInfo,
    VidPnTargetModeSet_AddMode,
    VidPnTargetModeSet_PinMode,
};

static CONST DXGK_MONITORSOURCEMODESET_INTERFACE g_MonitorSourceModeSetInterface =
{
    MonitorSourceModeSet_GetNumModes,
    MonitorSourceModeSet_AcquirePreferredModeInfo,
    MonitorSourceModeSet_AcquireFirstModeInfo,
    MonitorSourceModeSet_AcquireNextModeInfo,
    MonitorSourceModeSet_CreateNewModeInfo,
    MonitorSourceModeSet_AddMode,
    MonitorSourceModeSet_ReleaseModeInfo,
};

static CONST DXGK_VIDPN_INTERFACE g_VidPnInterfaceV1 =
{
    DXGK_VIDPN_INTERFACE_VERSION_V1,
    VidPn_GetTopology,
    VidPn_AcquireSourceModeSet,
    VidPn_ReleaseSourceModeSet,
    VidPn_CreateNewSourceModeSet,
    VidPn_AssignSourceModeSet,
    VidPn_AssignMultisamplingMethodSet,
    VidPn_AcquireTargetModeSet,
    VidPn_ReleaseTargetModeSet,
    VidPn_CreateNewTargetModeSet,
    VidPn_AssignTargetModeSet,
};

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
/*
 * WDDM 2.9 added interface version V2 without changing the
 * DXGK_VIDPN_INTERFACE layout or its callback signatures.  Keep a distinct
 * table because a miniport is allowed to validate that Version matches the
 * version it requested.
 */
static CONST DXGK_VIDPN_INTERFACE g_VidPnInterfaceV2 =
{
    DXGK_VIDPN_INTERFACE_VERSION_V2,
    VidPn_GetTopology,
    VidPn_AcquireSourceModeSet,
    VidPn_ReleaseSourceModeSet,
    VidPn_CreateNewSourceModeSet,
    VidPn_AssignSourceModeSet,
    VidPn_AssignMultisamplingMethodSet,
    VidPn_AcquireTargetModeSet,
    VidPn_ReleaseTargetModeSet,
    VidPn_CreateNewTargetModeSet,
    VidPn_AssignTargetModeSet,
};
#endif

NTSTATUS
DxgkVidPnResolveTargetForSource(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID *VidPnTargetId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T Index;
    NTSTATUS Status = STATUS_GRAPHICS_SOURCE_NOT_IN_TOPOLOGY;

    if (Adapter == NULL || VidPnTargetId == NULL)
        return STATUS_INVALID_PARAMETER;

    *VidPnTargetId = D3DDDI_ID_UNINITIALIZED;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE || VidPnSourceId >= VidPn->NumSources)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }
    else
    {
        for (Index = 0; Index < VidPn->NumPaths; ++Index)
        {
            if (VidPn->Paths[Index].VidPnSourceId != VidPnSourceId)
                continue;
            *VidPnTargetId = VidPn->Paths[Index].VidPnTargetId;
            Status = STATUS_SUCCESS;
            break;
        }
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    return Status;
}

static CONST DXGK_MONITOR_INTERFACE g_MonitorInterfaceV1 =
{
    DXGK_MONITOR_INTERFACE_VERSION_V1,
    Monitor_AcquireMonitorSourceModeSet,
    Monitor_ReleaseMonitorSourceModeSet,
    Monitor_GetMonitorFrequencyRangeSet,
    Monitor_GetMonitorDescriptorSet,
};

static CONST DXGK_MONITOR_INTERFACE_V2 g_MonitorInterfaceV2 =
{
    DXGK_MONITOR_INTERFACE_VERSION_V2,
    Monitor_AcquireMonitorSourceModeSet,
    Monitor_ReleaseMonitorSourceModeSet,
    Monitor_GetMonitorFrequencyRangeSet,
    Monitor_GetMonitorDescriptorSet,
    Monitor_GetAdditionalMonitorModeSet,
    Monitor_ReleaseAdditionalMonitorModeSet,
};

/* ========================================================================
 * VidPN source ownership tracking
 *
 * A source identifier is only meaningful within one adapter.  Keep one
 * state object for each adapter that currently has at least one owner rather
 * than aliasing source zero from every adapter through one global array.
 * The state object does not need a separate adapter reference: every stored
 * owner is a live device, and each live device pins its adapter.  Empty state
 * objects are detached and freed before the last owner can disappear.
 * ====================================================================== */
typedef struct _DXGKP_SOURCE_OWNER_ADAPTER_STATE
{
    LIST_ENTRY Entry;
    PDXGKRNL_ADAPTER Adapter;
    DXGKP_VIDPN_SOURCE_OWNER Owners[DXGKP_MAX_SOURCES];
} DXGKP_SOURCE_OWNER_ADAPTER_STATE, *PDXGKP_SOURCE_OWNER_ADAPTER_STATE;

#define DXGKP_MAX_SOURCE_OWNER_OPERATIONS 4096U
#define DXGKP_STDALLOC_MAX_PRIVATE_SIZE   (64U * 1024U)
#define DXGKP_SOURCE_OWNER_FLAG_ALLOW_OUTPUT_DUPLICATION 0x00000001U
#define DXGKP_SOURCE_OWNER_FLAG_DISABLE_DWM_VIRTUAL_MODE 0x00000002U
#define DXGKP_SOURCE_OWNER_FLAG_USE_NT_HANDLES           0x00000004U
#define DXGKP_SOURCE_OWNER_FLAG_VALID_MASK               0x00000007U
#define TAG_DXGK_SOURCE_OWNER 'OxgD'

static LIST_ENTRY g_SourceOwnerAdapterList;
static FAST_MUTEX g_SourceOwnerMutex;
static volatile LONG g_SourceOwnerState = 0;

static VOID
DxgkpEnsureSourceOwnerMutex(VOID)
{
    LONG State = InterlockedCompareExchange(&g_SourceOwnerState, 1, 0);

    if (State == 0)
    {
        ExInitializeFastMutex(&g_SourceOwnerMutex);
        InitializeListHead(&g_SourceOwnerAdapterList);
        InterlockedExchange(&g_SourceOwnerState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_SourceOwnerState, 2, 2) != 2)
        YieldProcessor();
}

static PDXGKP_SOURCE_OWNER_ADAPTER_STATE
DxgkpFindSourceOwnerAdapterLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PLIST_ENTRY Entry;

    for (Entry = g_SourceOwnerAdapterList.Flink; Entry != &g_SourceOwnerAdapterList; Entry = Entry->Flink)
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State = CONTAINING_RECORD(Entry, DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);

        if (State->Adapter == Adapter)
            return State;
    }

    return NULL;
}

static BOOLEAN
DxgkpSourceOwnerStateIsEmpty(
    _In_reads_(DXGKP_MAX_SOURCES) CONST DXGKP_VIDPN_SOURCE_OWNER *Owners)
{
    ULONG Index;

    for (Index = 0; Index < DXGKP_MAX_SOURCES; ++Index)
    {
        if (Owners[Index].OwnerDevice != NULL)
            return FALSE;
    }

    return TRUE;
}

static NTSTATUS
DxgkpApplySourceOwnerOperation(
    _Inout_updates_(DXGKP_MAX_SOURCES) DXGKP_VIDPN_SOURCE_OWNER *Owners,
    _In_ PDXGKRNL_DEVICE OwnerDevice,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_ D3DKMT_VIDPNSOURCEOWNER_TYPE RequestedType,
    _In_ UINT OwnerFlags)
{
    PDXGKP_VIDPN_SOURCE_OWNER Current = &Owners[SourceId];

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_UNOWNED)
    {
        if (Current->OwnerDevice == OwnerDevice)
        {
            Current->OwnerDevice = NULL;
            Current->OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
            Current->OwnerFlags = 0;
        }
        return STATUS_SUCCESS;
    }

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_SHARED)
    {
        if (Current->OwnerDevice == OwnerDevice && (Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
            return STATUS_INVALID_PARAMETER;
        return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    }

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || RequestedType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI)
    {
        if (Current->OwnerDevice == NULL || Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_SHARED)
        {
            Current->OwnerDevice = OwnerDevice;
            Current->OwnerType = RequestedType;
            Current->OwnerFlags = OwnerFlags;
            return STATUS_SUCCESS;
        }
        if (Current->OwnerDevice == OwnerDevice && Current->OwnerType == RequestedType)
        {
            Current->OwnerFlags = OwnerFlags;
            return STATUS_SUCCESS;
        }
        if (Current->OwnerDevice == OwnerDevice)
            return STATUS_INVALID_PARAMETER;
        return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    }

    if (Current->OwnerDevice == NULL)
    {
        Current->OwnerDevice = OwnerDevice;
        Current->OwnerType = D3DKMT_VIDPNSOURCEOWNER_EMULATED;
        Current->OwnerFlags = OwnerFlags;
        return STATUS_SUCCESS;
    }
    if (Current->OwnerDevice == OwnerDevice && Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EMULATED)
    {
        Current->OwnerFlags = OwnerFlags;
        return STATUS_SUCCESS;
    }
    if (Current->OwnerDevice == OwnerDevice && (Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
        return STATUS_INVALID_PARAMETER;
    return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
}

/* ========================================================================
 * DxgkpDeviceOwnsVidPnSource
 *
 * Returns TRUE if hDevice currently has real primary ownership of
 * VidPnSourceId (SHARED, EXCLUSIVE, or EXCLUSIVEGDI).  EMULATED ownership is
 * deliberately excluded: it reserves gamma control but has no real primary
 * ownership.  The present path uses this to decide whether a present may scan
 * out to the primary.
 * This mirrors Windows, where the desktop compositor owns the primary and an
 * ordinary app present is composited into the app's window rather than
 * overwriting the live desktop.  Without it, any process presenting an
 * arbitrary surface to source 0 would paint directly over the desktop.
 * ====================================================================== */
BOOLEAN
DxgkpDeviceOwnsVidPnSource(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device;
    BOOLEAN Owns = FALSE;

    if (hDevice == 0 || VidPnSourceId >= DXGKP_MAX_SOURCES)
        return FALSE;

    Device = DxgkLookupDeviceByHandle(hDevice, &Adapter);
    if (Device == NULL)
        return FALSE;
    if (Adapter == NULL)
    {
        DxgkDereferenceDevice(Device);
        return FALSE;
    }

    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        ExReleaseFastMutex(&g_SourceOwnerMutex);
        DxgkDereferenceDevice(Device);
        return FALSE;
    }
    State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
    if (State != NULL && State->Owners[VidPnSourceId].OwnerDevice == Device)
        Owns = (BOOLEAN)(State->Owners[VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_SHARED || State->Owners[VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || State->Owners[VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI);
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    DxgkDereferenceDevice(Device);

    return Owns;
}

VOID
DxgkVidPnCleanupDeviceOwners(
    _In_ PDXGKRNL_DEVICE Device)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE StateToFree = NULL;
    ULONG Index;

    if (Device == NULL || Device->Adapter == NULL || Device->Handle == 0)
        return;
    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    State = DxgkpFindSourceOwnerAdapterLocked(Device->Adapter);
    if (State != NULL)
    {
        for (Index = 0; Index < DXGKP_MAX_SOURCES; ++Index)
        {
            if (State->Owners[Index].OwnerDevice == Device)
            {
                State->Owners[Index].OwnerDevice = NULL;
                State->Owners[Index].OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
                State->Owners[Index].OwnerFlags = 0;
            }
        }
        if (DxgkpSourceOwnerStateIsEmpty(State->Owners))
        {
            RemoveEntryList(&State->Entry);
            StateToFree = State;
        }
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    if (StateToFree != NULL)
    {
        RtlZeroMemory(StateToFree->Owners, sizeof(StateToFree->Owners));
        ExFreePoolWithTag(StateToFree, TAG_DXGK_SOURCE_OWNER);
    }
}

NTSTATUS
DxgkVidPnReleaseProcessOwners(
    _In_ PEPROCESS Process)
{
    LIST_ENTRY FreeList;
    PLIST_ENTRY Entry;

    PAGED_CODE();
    if (Process == NULL)
        return STATUS_INVALID_PARAMETER;
    InitializeListHead(&FreeList);
    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    Entry = g_SourceOwnerAdapterList.Flink;
    while (Entry != &g_SourceOwnerAdapterList)
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State = CONTAINING_RECORD(Entry, DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);
        PLIST_ENTRY Next = Entry->Flink;
        ULONG Index;

        for (Index = 0; Index < DXGKP_MAX_SOURCES; ++Index)
        {
            if (State->Owners[Index].OwnerDevice != NULL && State->Owners[Index].OwnerDevice->OwnerProcess == Process)
            {
                State->Owners[Index].OwnerDevice = NULL;
                State->Owners[Index].OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
                State->Owners[Index].OwnerFlags = 0;
            }
        }
        if (DxgkpSourceOwnerStateIsEmpty(State->Owners))
        {
            RemoveEntryList(&State->Entry);
            InsertTailList(&FreeList, &State->Entry);
        }
        Entry = Next;
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    while (!IsListEmpty(&FreeList))
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State = CONTAINING_RECORD(RemoveHeadList(&FreeList), DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);

        RtlZeroMemory(State->Owners, sizeof(State->Owners));
        ExFreePoolWithTag(State, TAG_DXGK_SOURCE_OWNER);
    }
    return STATUS_SUCCESS;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
NTSTATUS
DxgkVidPnQueryExclusiveOwnership(
    _In_ HANDLE ProcessId,
    _In_ CONST LUID *QueryAdapterLuid,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID QueryVidPnSourceId,
    _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID *ResultVidPnSourceId,
    _Out_ LUID *ResultAdapterLuid,
    _Out_ D3DKMT_VIDPNSOURCEOWNER_TYPE *OwnerType)
{
    PLIST_ENTRY Entry;
    PDXGKP_VIDPN_SOURCE_OWNER Owner;
    PEPROCESS Process = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (QueryAdapterLuid == NULL ||
        ResultVidPnSourceId == NULL ||
        ResultAdapterLuid == NULL ||
        OwnerType == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *ResultVidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    RtlZeroMemory(ResultAdapterLuid, sizeof(*ResultAdapterLuid));
    *OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;

    if (ProcessId == NULL)
        return STATUS_INVALID_HANDLE;

    Status = PsLookupProcessByProcessId(ProcessId, &Process);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * QueryVidPnExclusiveOwnership does not turn a yieldable SHARED owner into
     * an exclusive one.  Native reports only EXCLUSIVE, EXCLUSIVEGDI, and
     * EMULATED here; no match is a successful query with the defaults above.
     *
     * Device teardown removes an owner while holding this same mutex before
     * the final device reference is released, so OwnerDevice and its adapter
     * remain stable for the duration of the lookup.
     */
    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    for (Entry = g_SourceOwnerAdapterList.Flink;
         Entry != &g_SourceOwnerAdapterList;
         Entry = Entry->Flink)
    {
        PDXGKP_SOURCE_OWNER_ADAPTER_STATE State =
            CONTAINING_RECORD(
                Entry,
                DXGKP_SOURCE_OWNER_ADAPTER_STATE,
                Entry);

        if (State->Adapter != NULL &&
            State->Adapter->AdapterLuid.LowPart ==
                QueryAdapterLuid->LowPart &&
            State->Adapter->AdapterLuid.HighPart ==
                QueryAdapterLuid->HighPart &&
            QueryVidPnSourceId < DXGKP_MAX_SOURCES)
        {
            Owner = &State->Owners[QueryVidPnSourceId];
            if (Owner->OwnerDevice != NULL &&
                Owner->OwnerDevice->OwnerProcess == Process &&
                InterlockedCompareExchange(
                    &Owner->OwnerDevice->Destroying, 0, 0) == 0 &&
                (Owner->OwnerType ==
                     D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE ||
                 Owner->OwnerType ==
                     D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI ||
                 Owner->OwnerType ==
                     D3DKMT_VIDPNSOURCEOWNER_EMULATED))
            {
                *ResultVidPnSourceId = QueryVidPnSourceId;
                *ResultAdapterLuid = State->Adapter->AdapterLuid;
                *OwnerType = Owner->OwnerType;
            }
            break;
        }
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    ObDereferenceObject(Process);
    return STATUS_SUCCESS;
}
#endif

/* ========================================================================
 * Handle validation helpers
 * ====================================================================== */

static PDXGKP_VIDPN
DxgkpVidPnFromHandle(
    _In_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKP_VIDPN VidPn = (PDXGKP_VIDPN)hVidPn;

    if (VidPn == NULL)
        return NULL;

    if (VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
    {
        DXGKRNL_ERR("DxgkpVidPnFromHandle: bad signature 0x%08lX (expected 0x%08lX) handle=%p\n",
                     VidPn->Signature, DXGKP_VIDPN_SIGNATURE, hVidPn);
        return NULL;
    }

    return VidPn;
}

BOOLEAN
DxgkVidPnReference(
    _In_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKP_VIDPN VidPn = (PDXGKP_VIDPN)hVidPn;
    LONG References;

    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
        return FALSE;
    for (;;)
    {
        References = InterlockedCompareExchange(&VidPn->RefCount, 0, 0);
        if (References <= 0 || References == MAXLONG)
            return FALSE;
        if (InterlockedCompareExchange(&VidPn->RefCount, References + 1, References) == References)
            return TRUE;
    }
}

static PDXGKP_VIDPN
DxgkpTopologyFromHandle(
    _In_ D3DKMDT_HVIDPNTOPOLOGY hTopology)
{
    return DxgkpVidPnFromHandle((D3DKMDT_HVIDPN)hTopology);
}

static PDXGKP_VIDPN_SOURCE_MODESET
DxgkpSourceModeSetFromHandle(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET hModeSet)
{
    return (PDXGKP_VIDPN_SOURCE_MODESET)hModeSet;
}

static PDXGKP_VIDPN_TARGET_MODESET
DxgkpTargetModeSetFromHandle(
    _In_ D3DKMDT_HVIDPNTARGETMODESET hModeSet)
{
    return (PDXGKP_VIDPN_TARGET_MODESET)hModeSet;
}

static PDXGKP_MONITOR_SOURCE_MODESET
DxgkpMonitorModeSetFromHandle(
    _In_ D3DKMDT_HMONITORSOURCEMODESET hModeSet)
{
    return (PDXGKP_MONITOR_SOURCE_MODESET)hModeSet;
}

/* ========================================================================
 * Default mode table
 * ====================================================================== */

typedef struct _DXGKP_DEFAULT_MODE
{
    UINT Width;
    UINT Height;
} DXGKP_DEFAULT_MODE;

static CONST DXGKP_DEFAULT_MODE g_DefaultModes[] =
{
    {  800,  600 },
    { 1024,  768 },
    { 1152,  864 },
    { 1280,  720 },
    { 1280,  800 },
    { 1280, 1024 },
    { 1366,  768 },
    { 1440,  900 },
    { 1600,  900 },
    { 1600, 1200 },
    { 1680, 1050 },
    { 1920, 1080 },
    { 1920, 1200 },
};

#define DXGKP_NUM_DEFAULT_MODES \
    (sizeof(g_DefaultModes) / sizeof(g_DefaultModes[0]))

/* ========================================================================
 * Mode normalization and comparison helpers
 * ====================================================================== */

static VOID
DxgkpNormalizeSourceMode(
    _Inout_ D3DKMDT_VIDPN_SOURCE_MODE *Mode)
{
    if ((Mode->Format.Graphics.VisibleRegionSize.cx == 0 ||
         Mode->Format.Graphics.VisibleRegionSize.cy == 0) &&
        Mode->Format.Graphics.PrimSurfSize.cx != 0 &&
        Mode->Format.Graphics.PrimSurfSize.cy != 0)
    {
        Mode->Format.Graphics.VisibleRegionSize = Mode->Format.Graphics.PrimSurfSize;
    }
}

static BOOLEAN
DxgkpAreEquivalentSourceModes(
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *Left,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE *Right)
{
    return (Left->Type == Right->Type &&
            Left->Format.Graphics.PrimSurfSize.cx == Right->Format.Graphics.PrimSurfSize.cx &&
            Left->Format.Graphics.PrimSurfSize.cy == Right->Format.Graphics.PrimSurfSize.cy &&
            Left->Format.Graphics.VisibleRegionSize.cx == Right->Format.Graphics.VisibleRegionSize.cx &&
            Left->Format.Graphics.VisibleRegionSize.cy == Right->Format.Graphics.VisibleRegionSize.cy &&
            Left->Format.Graphics.Stride == Right->Format.Graphics.Stride &&
            Left->Format.Graphics.PixelFormat == Right->Format.Graphics.PixelFormat &&
            Left->Format.Graphics.ColorBasis == Right->Format.Graphics.ColorBasis &&
            Left->Format.Graphics.PixelValueAccessMode == Right->Format.Graphics.PixelValueAccessMode);
}

static VOID
DxgkpNormalizeTargetMode(
    _Inout_ D3DKMDT_VIDPN_TARGET_MODE *Mode)
{
    if ((Mode->VideoSignalInfo.ActiveSize.cx == 0 ||
         Mode->VideoSignalInfo.ActiveSize.cy == 0) &&
        Mode->VideoSignalInfo.TotalSize.cx != 0 &&
        Mode->VideoSignalInfo.TotalSize.cy != 0)
    {
        Mode->VideoSignalInfo.ActiveSize = Mode->VideoSignalInfo.TotalSize;
    }

    if ((Mode->VideoSignalInfo.TotalSize.cx == 0 ||
         Mode->VideoSignalInfo.TotalSize.cy == 0) &&
        Mode->VideoSignalInfo.ActiveSize.cx != 0 &&
        Mode->VideoSignalInfo.ActiveSize.cy != 0)
    {
        Mode->VideoSignalInfo.TotalSize = Mode->VideoSignalInfo.ActiveSize;
    }
}

static BOOLEAN
DxgkpAreEquivalentTargetModes(
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *Left,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE *Right)
{
    return (Left->VideoSignalInfo.VideoStandard == Right->VideoSignalInfo.VideoStandard &&
            Left->VideoSignalInfo.TotalSize.cx == Right->VideoSignalInfo.TotalSize.cx &&
            Left->VideoSignalInfo.TotalSize.cy == Right->VideoSignalInfo.TotalSize.cy &&
            Left->VideoSignalInfo.ActiveSize.cx == Right->VideoSignalInfo.ActiveSize.cx &&
            Left->VideoSignalInfo.ActiveSize.cy == Right->VideoSignalInfo.ActiveSize.cy &&
            Left->VideoSignalInfo.VSyncFreq.Numerator == Right->VideoSignalInfo.VSyncFreq.Numerator &&
            Left->VideoSignalInfo.VSyncFreq.Denominator == Right->VideoSignalInfo.VSyncFreq.Denominator &&
            Left->VideoSignalInfo.HSyncFreq.Numerator == Right->VideoSignalInfo.HSyncFreq.Numerator &&
            Left->VideoSignalInfo.HSyncFreq.Denominator == Right->VideoSignalInfo.HSyncFreq.Denominator &&
            Left->VideoSignalInfo.PixelRate == Right->VideoSignalInfo.PixelRate &&
            Left->VideoSignalInfo.ScanLineOrdering == Right->VideoSignalInfo.ScanLineOrdering);
}

static VOID
DxgkpNormalizeMonitorMode(
    _Inout_ D3DKMDT_MONITOR_SOURCE_MODE *Mode)
{
    if ((Mode->VideoSignalInfo.ActiveSize.cx == 0 ||
         Mode->VideoSignalInfo.ActiveSize.cy == 0) &&
        Mode->VideoSignalInfo.TotalSize.cx != 0 &&
        Mode->VideoSignalInfo.TotalSize.cy != 0)
    {
        Mode->VideoSignalInfo.ActiveSize = Mode->VideoSignalInfo.TotalSize;
    }

    if ((Mode->VideoSignalInfo.TotalSize.cx == 0 ||
         Mode->VideoSignalInfo.TotalSize.cy == 0) &&
        Mode->VideoSignalInfo.ActiveSize.cx != 0 &&
        Mode->VideoSignalInfo.ActiveSize.cy != 0)
    {
        Mode->VideoSignalInfo.TotalSize = Mode->VideoSignalInfo.ActiveSize;
    }
}

static BOOLEAN
DxgkpAreEquivalentMonitorModes(
    _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE *Left,
    _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE *Right)
{
    return (Left->VideoSignalInfo.VideoStandard == Right->VideoSignalInfo.VideoStandard &&
            Left->VideoSignalInfo.TotalSize.cx == Right->VideoSignalInfo.TotalSize.cx &&
            Left->VideoSignalInfo.TotalSize.cy == Right->VideoSignalInfo.TotalSize.cy &&
            Left->VideoSignalInfo.ActiveSize.cx == Right->VideoSignalInfo.ActiveSize.cx &&
            Left->VideoSignalInfo.ActiveSize.cy == Right->VideoSignalInfo.ActiveSize.cy &&
            Left->VideoSignalInfo.VSyncFreq.Numerator == Right->VideoSignalInfo.VSyncFreq.Numerator &&
            Left->VideoSignalInfo.VSyncFreq.Denominator == Right->VideoSignalInfo.VSyncFreq.Denominator &&
            Left->VideoSignalInfo.HSyncFreq.Numerator == Right->VideoSignalInfo.HSyncFreq.Numerator &&
            Left->VideoSignalInfo.HSyncFreq.Denominator == Right->VideoSignalInfo.HSyncFreq.Denominator &&
            Left->VideoSignalInfo.PixelRate == Right->VideoSignalInfo.PixelRate &&
            Left->VideoSignalInfo.ScanLineOrdering == Right->VideoSignalInfo.ScanLineOrdering &&
            Left->ColorBasis == Right->ColorBasis &&
            Left->ColorCoeffDynamicRanges.FirstChannel == Right->ColorCoeffDynamicRanges.FirstChannel &&
            Left->ColorCoeffDynamicRanges.SecondChannel == Right->ColorCoeffDynamicRanges.SecondChannel &&
            Left->ColorCoeffDynamicRanges.ThirdChannel == Right->ColorCoeffDynamicRanges.ThirdChannel &&
            Left->ColorCoeffDynamicRanges.FourthChannel == Right->ColorCoeffDynamicRanges.FourthChannel &&
            Left->Origin == Right->Origin);
}

/* ========================================================================
 * Default mode population helpers
 * ====================================================================== */

static VOID
DxgkpPopulateDefaultSourceMode(
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE *Mode,
    _In_  UINT                       Id,
    _In_  UINT                       Width,
    _In_  UINT                       Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Id   = Id;
    Mode->Type = D3DKMDT_RMT_GRAPHICS;

    Mode->Format.Graphics.PrimSurfSize.cx      = Width;
    Mode->Format.Graphics.PrimSurfSize.cy      = Height;
    Mode->Format.Graphics.VisibleRegionSize.cx = Width;
    Mode->Format.Graphics.VisibleRegionSize.cy = Height;
    Mode->Format.Graphics.Stride               = Width * 4;
    Mode->Format.Graphics.PixelFormat          = D3DDDIFMT_A8R8G8B8;
    /*
     * sRGB, not scRGB.  scRGB is the linear wide-gamut basis defined for 16
     * bits of float per channel; naming it on an 8-bit integer A8R8G8B8
     * surface describes a mode that cannot exist, and dxgkrnl's own monitor
     * modes declare sRGB, so the path claimed one basis at the source and
     * another at the monitor.  A miniport that checks the pinned source mode
     * refuses the whole VidPN over it.
     */
    Mode->Format.Graphics.ColorBasis           = D3DKMDT_CB_SRGB;
    Mode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
}

/*
 * Fill a video signal with VESA CVT reduced-blanking timings.
 *
 * These modes used to be written with TotalSize == ActiveSize and a pixel
 * rate of Width*Height*Refresh -- a raster with no blanking at all, which no
 * display pipeline can generate.  A full miniport validates the signal it is
 * asked to drive, and this Intel part refuses the whole VidPN over it, so
 * nothing was ever committed and the boot ended in VIDEO_DRIVER_INIT_FAILURE.
 * The synthetic list has to describe real rasters even when it is only a
 * starting point for DxgkDdiEnumVidPnCofuncModality to prune.
 *
 * CVT-RB v1: a fixed 160-pixel horizontal blanking, and just enough vertical
 * blanking lines to cover the 460 us the standard requires.  For 1920x1080 at
 * 60 Hz that gives 2080x1111 and 138.65 MHz, which is the published CVT-RB
 * timing for that mode.
 */
#define DXGKP_CVT_RB_H_BLANK        160u
#define DXGKP_CVT_RB_MIN_V_BLANK_US 460u

static VOID
DxgkpFillCvtReducedBlankingSignal(
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal,
    _In_  UINT                       Width,
    _In_  UINT                       Height,
    _In_  UINT                       RefreshHz)
{
    ULONGLONG TotalWidth;
    ULONGLONG TotalHeight;
    ULONGLONG BlankLines;
    ULONGLONG Denominator;
    ULONGLONG PixelRate;

    RtlZeroMemory(Signal, sizeof(*Signal));
    if (Width == 0 || Height == 0 || RefreshHz == 0)
        return;

    TotalWidth = (ULONGLONG)Width + DXGKP_CVT_RB_H_BLANK;

    /*
     * Lines of vertical blanking, rounded up:
     *     ceil(MinVBlankUs * Height * Refresh / (1000000 - MinVBlankUs * Refresh))
     * which is the CVT estimate  ceil(MinVBlank / HPeriodEst)  with the line
     * period left as a ratio so no rounding creeps in before the division.
     */
    Denominator = 1000000ULL - (ULONGLONG)DXGKP_CVT_RB_MIN_V_BLANK_US * RefreshHz;
    if ((LONGLONG)Denominator <= 0)
    {
        /* Refresh so high the blanking interval would not fit; fall back to
         * the CVT floor of front porch + sync + back porch. */
        BlankLines = 10;
    }
    else
    {
        ULONGLONG Numerator =
            (ULONGLONG)DXGKP_CVT_RB_MIN_V_BLANK_US * Height * RefreshHz;

        BlankLines = (Numerator + Denominator - 1) / Denominator;
        if (BlankLines < 10)
            BlankLines = 10;
    }
    TotalHeight = (ULONGLONG)Height + BlankLines;
    PixelRate = TotalWidth * TotalHeight * RefreshHz;

    Signal->VideoStandard      = D3DKMDT_VSS_OTHER;
    Signal->TotalSize.cx       = (LONG)TotalWidth;
    Signal->TotalSize.cy       = (LONG)TotalHeight;
    Signal->ActiveSize.cx      = (LONG)Width;
    Signal->ActiveSize.cy      = (LONG)Height;
    Signal->VSyncFreq.Numerator   = (UINT)RefreshHz;
    Signal->VSyncFreq.Denominator = 1;
    /* Exact rather than rounded: the line rate is the pixel rate over one
     * whole line, and both fit a rational. */
    Signal->HSyncFreq.Numerator   = (UINT)PixelRate;
    Signal->HSyncFreq.Denominator = (UINT)TotalWidth;
    Signal->PixelRate          = (SIZE_T)PixelRate;
    Signal->ScanLineOrdering   = D3DDDI_VSSLO_PROGRESSIVE;
}

static VOID
DxgkpPopulateDefaultTargetMode(
    _Out_ D3DKMDT_VIDPN_TARGET_MODE *Mode,
    _In_  UINT                       Id,
    _In_  UINT                       Width,
    _In_  UINT                       Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Id = Id;
    DxgkpFillCvtReducedBlankingSignal(&Mode->VideoSignalInfo, Width, Height, 60);
    Mode->Preference = D3DKMDT_MP_NOTPREFERRED;
}

static VOID
DxgkpPopulateDefaultMonitorMode(
    _Out_ D3DKMDT_MONITOR_SOURCE_MODE *Mode,
    _In_  UINT                         Id,
    _In_  UINT                         Width,
    _In_  UINT                         Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Id = Id;
    /* Same raster the target mode describes -- a monitor mode that claimed no
     * blanking would contradict the target it is meant to be driven by. */
    DxgkpFillCvtReducedBlankingSignal(&Mode->VideoSignalInfo, Width, Height, 60);

    Mode->ColorBasis = D3DKMDT_CB_SRGB;
    Mode->ColorCoeffDynamicRanges.FirstChannel  = 8;
    Mode->ColorCoeffDynamicRanges.SecondChannel = 8;
    Mode->ColorCoeffDynamicRanges.ThirdChannel  = 8;
    Mode->ColorCoeffDynamicRanges.FourthChannel = 0;
    Mode->Origin     = D3DKMDT_MCO_DRIVER;
    Mode->Preference = D3DKMDT_MP_PREFERRED;
}

static VOID
DxgkpPopulateDefaultPath(
    _Out_ D3DKMDT_VIDPN_PRESENT_PATH *Path,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    RtlZeroMemory(Path, sizeof(*Path));
    Path->VidPnSourceId   = SourceId;
    Path->VidPnTargetId   = TargetId;
    Path->ImportanceOrdinal = D3DKMDT_VPPI_PRIMARY;

    Path->ContentTransformation.Scaling  = D3DKMDT_VPPS_IDENTITY;
    Path->ContentTransformation.ScalingSupport.Identity = 1;
    Path->ContentTransformation.ScalingSupport.Centered = 1;
    Path->ContentTransformation.ScalingSupport.Stretched = 1;

    Path->ContentTransformation.Rotation = D3DKMDT_VPPR_IDENTITY;
    Path->ContentTransformation.RotationSupport.Identity = 1;
    Path->ContentTransformation.RotationSupport.Rotate90 = 0;
    Path->ContentTransformation.RotationSupport.Rotate180 = 0;
    Path->ContentTransformation.RotationSupport.Rotate270 = 0;

    Path->GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
    Path->GammaRamp.DataSize = 0;

    /* Same reasoning as the source mode, and the dynamic ranges right below
     * say 8 bits per channel -- which scRGB never is. */
    Path->VidPnTargetColorBasis = D3DKMDT_CB_SRGB;
    Path->VidPnTargetColorCoeffDynamicRanges.FirstChannel  = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.SecondChannel = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.ThirdChannel  = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.FourthChannel = 0;

    Path->Content = D3DKMDT_VPPC_GRAPHICS;

    /*
     * "No copy protection" is a value of its own, not the zero the rest of the
     * struct was cleared to.  D3DKMDT_VPPMT_UNINITIALIZED is what RtlZeroMemory
     * leaves behind, and it is the one field on this path that still said
     * nothing -- everything else names a real enumerator.  Declare the support
     * bit too, so what the path asks for is also what it advertises.
     */
    Path->CopyProtection.CopyProtectionType = D3DKMDT_VPPMT_NOPROTECTION;
    Path->CopyProtection.APSTriggerBits = 0;
    Path->CopyProtection.CopyProtectionSupport.NoProtection = 1;
}

/* ========================================================================
 * Mode set allocation/deallocation helpers
 * ====================================================================== */

static PDXGKP_VIDPN_SOURCE_MODESET
DxgkpAllocateSourceModeSet(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    ModeSet = (PDXGKP_VIDPN_SOURCE_MODESET)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN_SOURCE_MODESET), TAG_DXGK_MODESET);
    if (ModeSet == NULL)
        return NULL;

    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
    ModeSet->Owner = VidPn;
    ModeSet->SourceId = SourceId;
    ModeSet->PinnedModeId = (UINT)-1;
    ModeSet->NextModeId = 0;

    return ModeSet;
}

/*
 * A VidPN target id is the miniport's child uid, which is whatever the driver
 * chose -- this Intel part reports 49 for its connected output while declaring
 * ten children.  The per-target arrays are positional, so an id is not an
 * index and must be looked up.  Treating the two as the same number rejected
 * every adapter whose uids are not 0..NumTargets-1.
 */
ULONG
DxgkVidPnTargetIndexFromId(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    ULONG Index;

    if (VidPn == NULL)
        return MAXULONG;
    for (Index = 0; Index < VidPn->NumTargets && Index < DXGKP_MAX_TARGETS; Index++)
    {
        if (VidPn->TargetModeSets[Index] != NULL &&
            VidPn->TargetModeSets[Index]->TargetId == TargetId)
        {
            return Index;
        }
    }
    return MAXULONG;
}

static PDXGKP_VIDPN_TARGET_MODESET
DxgkpAllocateTargetModeSet(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    ModeSet = (PDXGKP_VIDPN_TARGET_MODESET)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN_TARGET_MODESET), TAG_DXGK_MODESET);
    if (ModeSet == NULL)
        return NULL;

    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
    ModeSet->Owner = VidPn;
    ModeSet->TargetId = TargetId;
    ModeSet->PinnedModeId = (UINT)-1;
    ModeSet->NextModeId = 0;

    return ModeSet;
}

static PDXGKP_MONITOR_SOURCE_MODESET
DxgkpAllocateMonitorModeSet(
    _In_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    ModeSet = (PDXGKP_MONITOR_SOURCE_MODESET)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_MONITOR_SOURCE_MODESET), TAG_DXGK_MODESET);
    if (ModeSet == NULL)
        return NULL;

    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
    ModeSet->Owner = VidPn;
    ModeSet->TargetId = TargetId;
    ModeSet->NextModeId = 0;

    return ModeSet;
}

static VOID
DxgkpPopulateDefaultModes(
    _In_ PDXGKP_VIDPN_SOURCE_MODESET SrcSet,
    _In_ PDXGKP_VIDPN_TARGET_MODESET TgtSet,
    _In_ PDXGKP_MONITOR_SOURCE_MODESET MonSet)
{
    UINT i;
    UINT NumModes = (UINT)DXGKP_NUM_DEFAULT_MODES;

    if (NumModes > DXGKP_MAX_MODES)
        NumModes = DXGKP_MAX_MODES;

    if (SrcSet != NULL)
    {
        for (i = 0; i < NumModes; i++)
        {
            DxgkpPopulateDefaultSourceMode(
                &SrcSet->Modes[i], i,
                g_DefaultModes[i].Width,
                g_DefaultModes[i].Height);
        }
        SrcSet->NumModes = NumModes;
        SrcSet->NextModeId = NumModes;
    }

    if (TgtSet != NULL)
    {
        for (i = 0; i < NumModes; i++)
        {
            DxgkpPopulateDefaultTargetMode(
                &TgtSet->Modes[i], i,
                g_DefaultModes[i].Width,
                g_DefaultModes[i].Height);
        }
        TgtSet->NumModes = NumModes;
        TgtSet->NextModeId = NumModes;
    }

    if (MonSet != NULL)
    {
        for (i = 0; i < NumModes; i++)
        {
            DxgkpPopulateDefaultMonitorMode(
                &MonSet->Modes[i], i,
                g_DefaultModes[i].Width,
                g_DefaultModes[i].Height);
        }
        if (NumModes > 0)
            MonSet->Modes[NumModes - 1].Preference = D3DKMDT_MP_PREFERRED;
        MonSet->NumModes = NumModes;
        MonSet->NextModeId = NumModes;
    }
}

/* ========================================================================
 * VidPN object lifecycle
 * ====================================================================== */

NTSTATUS
DxgkVidPnCreateForAdapter(
    _In_  PDXGKRNL_ADAPTER  Adapter,
    _Out_ D3DKMDT_HVIDPN   *phVidPn)
{
    PDXGKP_VIDPN VidPn = NULL;
    ULONG NumSources, NumTargets;
    ULONG i, ModePairCount, PathCount;
    BOOLEAN SeedDefaultTopology;

    PAGED_CODE();

    if (phVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *phVidPn = NULL;

    DxgkpEnsureSourceOwnerMutex();

    VidPn = (PDXGKP_VIDPN)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN), TAG_DXGK_VIDPN);
    if (VidPn == NULL)
    {
        DXGKRNL_ERR("DxgkVidPnCreateForAdapter: failed to allocate VidPN\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(VidPn, sizeof(*VidPn));
    VidPn->Signature = DXGKP_VIDPN_SIGNATURE;
    VidPn->RefCount  = 1;
    VidPn->Adapter   = Adapter;

    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetUids[DXGKP_MAX_TARGETS];

    /* Determine source/target counts from adapter. */
    NumSources = (Adapter->NumberOfVideoPresentSources > 0)
                 ? Adapter->NumberOfVideoPresentSources : 1;
    NumTargets = (Adapter->NumberOfChildren > 0)
                 ? Adapter->NumberOfChildren : 1;

    if (NumSources > DXGKP_MAX_SOURCES) NumSources = DXGKP_MAX_SOURCES;
    if (NumTargets > DXGKP_MAX_TARGETS) NumTargets = DXGKP_MAX_TARGETS;

    VidPn->NumSources = NumSources;
    VidPn->NumTargets = NumTargets;

    /*
     * Record which child uid each positional target stands for.  The miniport
     * identifies its outputs by uid, so the topology must carry uids, while
     * the arrays here stay positional; DxgkVidPnTargetIndexFromId bridges the two.
     */
    {
        KIRQL ChildIrql;
        PLIST_ENTRY ChildEntry;
        ULONG Slot = 0;

        for (Slot = 0; Slot < NumTargets; Slot++)
            TargetUids[Slot] = Slot;
        Slot = 0;
        KeAcquireSpinLock(&Adapter->ChildListLock, &ChildIrql);
        for (ChildEntry = Adapter->ChildListHead.Flink;
             ChildEntry != &Adapter->ChildListHead && Slot < NumTargets;
             ChildEntry = ChildEntry->Flink)
        {
            PDXGK_CHILD_PDO_EXTENSION Child =
                CONTAINING_RECORD(ChildEntry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

            if (!Child->Present ||
                Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            {
                continue;
            }
            TargetUids[Slot++] = Child->Descriptor.ChildUid;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, ChildIrql);
    }

    /* Allocate mode sets for each source and target. */
    for (i = 0; i < NumSources; i++)
    {
        VidPn->SourceModeSets[i] = DxgkpAllocateSourceModeSet(VidPn, i);
        if (VidPn->SourceModeSets[i] == NULL)
            goto Fail;
    }

    for (i = 0; i < NumTargets; i++)
    {
        VidPn->TargetModeSets[i] = DxgkpAllocateTargetModeSet(VidPn, TargetUids[i]);
        if (VidPn->TargetModeSets[i] == NULL)
            goto Fail;

        VidPn->MonitorModeSets[i] = DxgkpAllocateMonitorModeSet(VidPn, TargetUids[i]);
        if (VidPn->MonitorModeSets[i] == NULL)
            goto Fail;
    }

    /*
     * A full WDDM miniport's source and child counts describe capacities, not
     * source-to-target wiring.  Start it with the contractually valid empty
     * topology; child-status/hot-plug discovery will add only paths that are
     * actually connected.  Display-only and BasicDisplay adapters need their
     * pre-seeded path during early boot because they provide the desktop
     * framebuffer before child discovery runs.
     */
    SeedDefaultTopology =
        Adapter->MiniportContext != NULL &&
        (Adapter->MiniportContext->IsDisplayOnlyDriver ||
         Adapter->MiniportContext->IsBasicDisplayFallback);
    ModePairCount = (NumSources < NumTargets) ? NumSources : NumTargets;
    if (ModePairCount > DXGKP_MAX_PATHS)
        ModePairCount = DXGKP_MAX_PATHS;
    PathCount = SeedDefaultTopology ? ModePairCount : 0;

    for (i = 0; i < ModePairCount; i++)
    {
        if (SeedDefaultTopology)
            DxgkpPopulateDefaultPath(&VidPn->Paths[i], i, i);
        DxgkpPopulateDefaultModes(
            VidPn->SourceModeSets[i],
            VidPn->TargetModeSets[i],
            VidPn->MonitorModeSets[i]);
    }
    VidPn->NumPaths = PathCount;

    *phVidPn = (D3DKMDT_HVIDPN)VidPn;

    DXGKRNL_TRACE("DxgkVidPnCreateForAdapter: created VidPN %p for adapter %p "
                  "(%lu sources, %lu targets, %lu paths)\n",
                  VidPn, Adapter, NumSources, NumTargets, PathCount);

    return STATUS_SUCCESS;

Fail:
    /* Clean up partially allocated mode sets. */
    for (i = 0; i < DXGKP_MAX_SOURCES; i++)
    {
        if (VidPn->SourceModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->SourceModeSets[i], TAG_DXGK_MODESET);
            VidPn->SourceModeSets[i] = NULL;
        }
    }
    for (i = 0; i < DXGKP_MAX_TARGETS; i++)
    {
        if (VidPn->TargetModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->TargetModeSets[i], TAG_DXGK_MODESET);
            VidPn->TargetModeSets[i] = NULL;
        }
        if (VidPn->MonitorModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->MonitorModeSets[i], TAG_DXGK_MODESET);
            VidPn->MonitorModeSets[i] = NULL;
        }
    }
    ExFreePoolWithTag(VidPn, TAG_DXGK_VIDPN);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
DxgkVidPnClone(
    _In_  D3DKMDT_HVIDPN  hSourceVidPn,
    _Out_ D3DKMDT_HVIDPN *phClonedVidPn)
{
    PDXGKP_VIDPN Source, Clone;
    ULONG i;

    PAGED_CODE();

    if (phClonedVidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *phClonedVidPn = NULL;

    Source = DxgkpVidPnFromHandle(hSourceVidPn);
    if (Source == NULL)
        return STATUS_INVALID_PARAMETER;

    Clone = (PDXGKP_VIDPN)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_VIDPN), TAG_DXGK_VIDPN);
    if (Clone == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Clone, sizeof(*Clone));
    Clone->Signature  = DXGKP_VIDPN_SIGNATURE;
    Clone->RefCount   = 1;
    Clone->Adapter    = Source->Adapter;
    Clone->NumSources = Source->NumSources;
    Clone->NumTargets = Source->NumTargets;
    Clone->NumPaths   = Source->NumPaths;

    /* Copy paths. */
    RtlCopyMemory(Clone->Paths, Source->Paths,
                   Source->NumPaths * sizeof(D3DKMDT_VIDPN_PRESENT_PATH));

    /* Deep-copy source mode sets. */
    for (i = 0; i < Source->NumSources; i++)
    {
        if (Source->SourceModeSets[i] != NULL)
        {
            Clone->SourceModeSets[i] = DxgkpAllocateSourceModeSet(Clone, i);
            if (Clone->SourceModeSets[i] == NULL)
                goto CloneFail;
            RtlCopyMemory(Clone->SourceModeSets[i], Source->SourceModeSets[i],
                           sizeof(DXGKP_VIDPN_SOURCE_MODESET));
            Clone->SourceModeSets[i]->Owner = Clone;
        }
    }

    /* Deep-copy target mode sets and monitor mode sets. */
    for (i = 0; i < Source->NumTargets; i++)
    {
        if (Source->TargetModeSets[i] != NULL)
        {
            Clone->TargetModeSets[i] = DxgkpAllocateTargetModeSet(Clone, i);
            if (Clone->TargetModeSets[i] == NULL)
                goto CloneFail;
            RtlCopyMemory(Clone->TargetModeSets[i], Source->TargetModeSets[i],
                           sizeof(DXGKP_VIDPN_TARGET_MODESET));
            Clone->TargetModeSets[i]->Owner = Clone;
        }
        if (Source->MonitorModeSets[i] != NULL)
        {
            Clone->MonitorModeSets[i] = DxgkpAllocateMonitorModeSet(Clone, i);
            if (Clone->MonitorModeSets[i] == NULL)
                goto CloneFail;
            RtlCopyMemory(Clone->MonitorModeSets[i], Source->MonitorModeSets[i],
                           sizeof(DXGKP_MONITOR_SOURCE_MODESET));
            Clone->MonitorModeSets[i]->Owner = Clone;
        }
    }

    *phClonedVidPn = (D3DKMDT_HVIDPN)Clone;
    DXGKRNL_TRACE("DxgkVidPnClone: cloned %p -> %p\n", Source, Clone);
    return STATUS_SUCCESS;

CloneFail:
    DxgkVidPnDestroy((D3DKMDT_HVIDPN)Clone);
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID
DxgkVidPnDestroy(
    _In_ D3DKMDT_HVIDPN hVidPn)
{
    PDXGKP_VIDPN VidPn;
    ULONG i;

    if (hVidPn == NULL)
        return;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return;

    if (InterlockedDecrement(&VidPn->RefCount) != 0)
        return;

    DXGKRNL_TRACE("DxgkVidPnDestroy: freeing VidPN %p\n", VidPn);

    VidPn->Signature = 0;

    for (i = 0; i < DXGKP_MAX_SOURCES; i++)
    {
        if (VidPn->SourceModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->SourceModeSets[i], TAG_DXGK_MODESET);
            VidPn->SourceModeSets[i] = NULL;
        }
    }
    for (i = 0; i < DXGKP_MAX_TARGETS; i++)
    {
        if (VidPn->TargetModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->TargetModeSets[i], TAG_DXGK_MODESET);
            VidPn->TargetModeSets[i] = NULL;
        }
        if (VidPn->MonitorModeSets[i] != NULL)
        {
            ExFreePoolWithTag(VidPn->MonitorModeSets[i], TAG_DXGK_MODESET);
            VidPn->MonitorModeSets[i] = NULL;
        }
    }

    ExFreePoolWithTag(VidPn, TAG_DXGK_VIDPN);
}

/* ========================================================================
 * Hot-plug detection support
 * ====================================================================== */

typedef struct _DXGKP_HOTPLUG_MONITOR_SNAPSHOT
{
    BOOLEAN Connected;
    BOOLEAN EdidValid;
    ULONG ChildUid;
    ULONG64 ChildStateGeneration;
    LONG64 ChildEnumerationEpoch;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    UCHAR Edid[128];
} DXGKP_HOTPLUG_MONITOR_SNAPSHOT, *PDXGKP_HOTPLUG_MONITOR_SNAPSHOT;

/*
 * Bind the positional target slots to the miniport's child uids.
 *
 * The VidPn is created inside DxgkAdapterStart, before PnP has asked the
 * miniport for its child relations, so DxgkVidPnCreateForAdapter can only
 * seed the slots with the placeholders 0..NumTargets-1.  The miniport names
 * its outputs by uid and the topology carries uids, so while the placeholders
 * stand every DxgkVidPnTargetIndexFromId lookup misses and no path can be
 * built for a real output.  This is where the real uids become known.
 *
 * The binding is stable: a slot already naming a live output keeps it, and
 * only outputs that own no slot take the free ones.  Re-keying the slots from
 * list order on every rebuild would move one output's mode sets onto another
 * whenever a child appeared or disappeared.
 */
static VOID
DxgkpBindVidPnTargetsToChildren(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PDXGKP_VIDPN VidPn)
{
    BOOLEAN SlotBound[DXGKP_MAX_TARGETS];
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG NumTargets;
    ULONG Slot;
    LONG64 Epoch;

    PAGED_CODE();

    NumTargets = VidPn->NumTargets;
    if (NumTargets > DXGKP_MAX_TARGETS)
        NumTargets = DXGKP_MAX_TARGETS;
    for (Slot = 0; Slot < NumTargets; Slot++)
        SlotBound[Slot] = FALSE;

    /* VidPnMutex before ChildListLock, the order the rebuild already uses. */
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    Epoch = Adapter->ChildEnumerationEpoch;
    if (!NT_SUCCESS(DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, Epoch)))
    {
        /* Mid-enumeration: the list is not the live set yet.  The rebuild
         * retries, and the binding runs again against the published list. */
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        return;
    }

    /* Pass 1: every slot that already names a live output keeps it. */
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);
        ULONG Index;

        if (!Child->Present || Child->EnumerationEpoch != Epoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            continue;
        Index = DxgkVidPnTargetIndexFromId(VidPn, Child->Descriptor.ChildUid);
        if (Index != MAXULONG && Index < NumTargets)
            SlotBound[Index] = TRUE;
    }

    /* Pass 2: the outputs that own no slot take the ones still free. */
    Slot = 0;
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->EnumerationEpoch != Epoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            continue;
        if (DxgkVidPnTargetIndexFromId(VidPn, Child->Descriptor.ChildUid) != MAXULONG)
            continue;
        while (Slot < NumTargets && SlotBound[Slot])
            Slot++;
        if (Slot >= NumTargets)
        {
            /* More outputs than this VidPn has slots for.  The remaining ones
             * stay unreachable; DxgkpSnapshotHotPlugMonitor refuses to drive
             * an output that owns no target rather than driving the wrong one. */
            DXGKRNL_WARN("DxgkpBindVidPnTargetsToChildren: child uid %lu has no free "
                         "target slot among the %lu this VidPN describes\n",
                         Child->Descriptor.ChildUid, VidPn->NumTargets);
            break;
        }
        if (VidPn->TargetModeSets[Slot] != NULL)
            VidPn->TargetModeSets[Slot]->TargetId = Child->Descriptor.ChildUid;
        if (VidPn->MonitorModeSets[Slot] != NULL)
            VidPn->MonitorModeSets[Slot]->TargetId = Child->Descriptor.ChildUid;
        SlotBound[Slot] = TRUE;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
}

static NTSTATUS
DxgkpSnapshotHotPlugMonitor(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_VIDPN VidPn,
    _In_ LONG64 ExpectedGeneration,
    _Out_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    PDXGK_CHILD_PDO_EXTENSION ConnectedChild = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG ConnectedCount = 0;

    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    if (InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0) != ExpectedGeneration)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        return STATUS_RETRY;
    }
    Snapshot->ChildEnumerationEpoch = Adapter->ChildEnumerationEpoch;
    if (!NT_SUCCESS(DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, Snapshot->ChildEnumerationEpoch)))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        return STATUS_DEVICE_NOT_READY;
    }
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->EnumerationEpoch != Snapshot->ChildEnumerationEpoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput)
            continue;
        if (!Child->Connected)
            continue;
        ConnectedCount++;
        /*
         * Prefer an output that reported an EDID: on a part with several
         * connectors the one with a monitor behind it is the one whose modes
         * the topology should be built from.  Otherwise keep the first
         * connected output rather than the last, so the choice is stable
         * across rebuilds instead of depending on child list order.
         */
        if (ConnectedChild == NULL ||
            (!ConnectedChild->EdidValid && Child->EdidValid))
        {
            ConnectedChild = Child;
        }
    }
    if (ConnectedCount >= 1)
    {
        Snapshot->Connected = TRUE;
        Snapshot->ChildUid = ConnectedChild->Descriptor.ChildUid;
        Snapshot->ChildStateGeneration = ConnectedChild->StateGeneration;
        Snapshot->EdidValid = ConnectedChild->EdidValid;
        if (Snapshot->EdidValid)
            RtlCopyMemory(Snapshot->Edid, ConnectedChild->Edid, sizeof(Snapshot->Edid));
    }
    if (InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0) != ExpectedGeneration)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        return STATUS_RETRY;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    if (ConnectedCount > 1)
    {
        /*
         * Several outputs are connected.  Refusing the rebuild here used to
         * abandon the whole VidPn: no topology was built, so no mode was ever
         * committed, so win32k could not create a primary surface and the boot
         * ended in VIDEO_DRIVER_INIT_FAILURE.  A single-source topology over
         * the chosen output is a correct VidPn and drives the display; it is
         * only incomplete in that the remaining outputs stay dark.
         *
         * TODO: build a multi-path topology so every connected output gets a
         * source.  DXGKP_HOTPLUG_MONITOR_SNAPSHOT holds one child, so that
         * needs the snapshot to become a list first.
         */
        static LONG DxgkpMultiOutputReported;

        if (InterlockedCompareExchange(&DxgkpMultiOutputReported, 1, 0) == 0)
        {
            DXGKRNL_WARN("DxgkpSnapshotHotPlugMonitor: %lu connected outputs, "
                         "driving child uid %lu (edid=%u); the others stay dark\n",
                         ConnectedCount,
                         Snapshot->ChildUid,
                         (UINT)Snapshot->EdidValid);
        }
    }
    /* Nothing connected is a complete answer for any adapter; the single
     * source this implementation drives only matters once a path exists. */
    if (!Snapshot->Connected)
        return STATUS_SUCCESS;
    /*
     * The rebuild drives one connected child through video present source 0,
     * which is what DxgkpPopulateDefaultPath below builds.  How many sources
     * the adapter declares in total is irrelevant to that: every real display
     * adapter reports several (this Intel part reports three), and demanding
     * exactly one rejected every one of them with STATUS_NOT_SUPPORTED, so no
     * VidPN was ever committed and the adapter kept a zero-sized mode.
     */
    if (VidPn->NumSources == 0)
    {
        DXGKRNL_ERR("SNAPSHOT: vidpn %p describes no video present source; "
                    "the rebuild has nothing to attach target %lu to\n",
                    VidPn, Snapshot->ChildUid);
        return STATUS_NOT_SUPPORTED;
    }
    /*
     * The target id handed to the topology is the miniport's child uid; the
     * positional arrays are reached through DxgkVidPnTargetIndexFromId.  Accepting
     * only uids below NumTargets rejected this adapter outright, because it
     * numbers its connected output 49 while declaring ten children.
     */
    Snapshot->TargetId = Snapshot->ChildUid;
    if (DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId) == MAXULONG)
    {
        DXGKRNL_ERR("DxgkpSnapshotHotPlugMonitor: connected child uid %lu has no target "
                    "among the %lu this VidPn describes\n",
                    Snapshot->ChildUid, VidPn->NumTargets);
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpHotPlugSnapshotCurrentLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot,
    _In_ LONG64 ExpectedGeneration,
    _Outptr_result_maybenull_ PDXGK_CHILD_PDO_EXTENSION *MatchingChild)
{
    PLIST_ENTRY Entry;
    ULONG ConnectedCount = 0;

    *MatchingChild = NULL;
    if (InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0) != ExpectedGeneration)
        return FALSE;
    if (!NT_SUCCESS(DxgkHotPlugWorkCoreValidateEnumerationLocked(&Adapter->ChildEnumerationEpoch, &Adapter->ChildRelationsEnumerated, Snapshot->ChildEnumerationEpoch)))
        return FALSE;
    for (Entry = Adapter->ChildListHead.Flink; Entry != &Adapter->ChildListHead; Entry = Entry->Flink)
    {
        PDXGK_CHILD_PDO_EXTENSION Child = CONTAINING_RECORD(Entry, DXGK_CHILD_PDO_EXTENSION, ListEntry);

        if (!Child->Present || Child->EnumerationEpoch != Snapshot->ChildEnumerationEpoch || Child->Descriptor.ChildDeviceType != TypeVideoOutput || !Child->Connected)
            continue;
        ConnectedCount++;
        if (Snapshot->Connected && Child->Descriptor.ChildUid == Snapshot->ChildUid && Child->StateGeneration == Snapshot->ChildStateGeneration)
            *MatchingChild = Child;
    }
    if (!Snapshot->Connected)
        return ConnectedCount == 0;
    /*
     * The snapshot is still current when the output it chose is still present,
     * still connected and unchanged.  Demanding that no *other* output be
     * connected contradicted DxgkpSnapshotHotPlugMonitor, which deliberately
     * drives one of several connected outputs rather than abandoning the
     * VidPn: on a part with two live connectors the snapshot succeeded, this
     * re-check then failed it, and the rebuild retried until it retired --
     * without ever committing a path.
     */
    return *MatchingChild != NULL;
}

static NTSTATUS
DxgkpRefreshHotPlugEdid(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    PDXGKDDI_QUERY_DEVICE_DESCRIPTOR QueryDeviceDescriptor;
    DXGK_DEVICE_DESCRIPTOR Descriptor;
    NTSTATUS Status;

    if (!Snapshot->Connected || Snapshot->EdidValid)
        return STATUS_SUCCESS;
    QueryDeviceDescriptor = DXGK_CB(Adapter, DxgkDdiQueryDeviceDescriptor);
    if (QueryDeviceDescriptor == NULL)
        return STATUS_SUCCESS;
    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.DescriptorOffset = 0;
    Descriptor.DescriptorLength = sizeof(Snapshot->Edid);
    Descriptor.DescriptorBuffer = Snapshot->Edid;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = QueryDeviceDescriptor(Adapter->MiniportDeviceContext, Snapshot->ChildUid, &Descriptor);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (NT_SUCCESS(Status))
    {
        Snapshot->EdidValid = TRUE;
        return STATUS_SUCCESS;
    }
    if (Status == STATUS_MONITOR_NO_DESCRIPTOR || Status == STATUS_NOT_SUPPORTED)
        return STATUS_SUCCESS;
    DXGKRNL_WARN("DxgkpRefreshHotPlugEdid: ChildUid %lu descriptor query failed 0x%08lX; retaining existing modes\n", Snapshot->ChildUid, Status);
    return STATUS_SUCCESS;
}

static NTSTATUS DxgkpVidPnRebuildForHotPlugGeneration(_In_ PDXGKRNL_ADAPTER Adapter, _In_ LONG64 ExpectedGeneration);


static VOID
NTAPI
DxgkpHotPlugRebuildWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_ADAPTER Adapter = Context;
    LONG64 ObservedGeneration;
    LONG64 RetryGeneration = 0;
    ULONG RetryCount = 0;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();
    for (;;)
    {
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        {
            (VOID)DxgkHotPlugWorkCoreCompleteLocked(&Adapter->HotPlugGeneration, &Adapter->HotPlugWorkActive, Adapter->HotPlugGeneration, FALSE);
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            break;
        }
        ObservedGeneration = Adapter->HotPlugGeneration;
        if (RetryGeneration != ObservedGeneration)
        {
            RetryGeneration = ObservedGeneration;
            RetryCount = 0;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        Status = DxgkpVidPnRebuildForHotPlugGeneration(Adapter, ObservedGeneration);
        KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
        if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        {
            (VOID)DxgkHotPlugWorkCoreCompleteLocked(&Adapter->HotPlugGeneration, &Adapter->HotPlugWorkActive, Adapter->HotPlugGeneration, FALSE);
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            break;
        }
        if (Adapter->HotPlugGeneration != ObservedGeneration)
        {
            RetryCount = 0;
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            continue;
        }
        /* The adapter is started and not running down (checked above), so a
         * refused miniport call means another thread still holds the KMD
         * exclusively -- the tail of the start that queued this rebuild --
         * which is a wait, not a removal. */
        if (Status == STATUS_DELETE_PENDING)
            Status = STATUS_DEVICE_BUSY;
        if (DxgkHotPlugWorkCoreShouldRetry(Status, RetryCount))
        {
            LARGE_INTEGER Delay;
            ULONG DelayMs = DxgkHotPlugWorkCoreRetryDelayMs(RetryCount);

            RetryCount++;
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            Delay.QuadPart = -(LONGLONG)DelayMs * 10000;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            continue;
        }
        if (!NT_SUCCESS(Status))
            DXGKRNL_ERR("DxgkpHotPlugRebuildWorker: adapter %p generation %I64d rebuild retired after %lu retries with 0x%08lX\n", Adapter, ObservedGeneration, RetryCount, Status);
        if (!DxgkHotPlugWorkCoreCompleteLocked(&Adapter->HotPlugGeneration, &Adapter->HotPlugWorkActive, ObservedGeneration, TRUE))
        {
            KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
            continue;
        }
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        break;
    }
    ExReleaseRundownProtection(&Adapter->RundownRef);
}

VOID
DxgkVidPnInitializeHotPlugWorker(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter != NULL)
        ExInitializeWorkItem(&Adapter->HotPlugWorkItem, DxgkpHotPlugRebuildWorker, Adapter);
}

NTSTATUS
DxgkVidPnQueueHotPlugRebuild(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    KIRQL OldIrql;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        return STATUS_DELETE_PENDING;
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    (VOID)DxgkHotPlugWorkCorePublishLocked(&Adapter->HotPlugGeneration);
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    if (!ExAcquireRundownProtection(&Adapter->RundownRef))
        return STATUS_DELETE_PENDING;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
    {
        ExReleaseRundownProtection(&Adapter->RundownRef);
        return STATUS_DELETE_PENDING;
    }
    KeAcquireSpinLock(&Adapter->ChildListLock, &OldIrql);
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        ExReleaseRundownProtection(&Adapter->RundownRef);
        return STATUS_DELETE_PENDING;
    }
    if (!DxgkHotPlugWorkCoreTryActivateLocked(&Adapter->HotPlugWorkActive))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
        ExReleaseRundownProtection(&Adapter->RundownRef);
        return STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, OldIrql);
    ExQueueWorkItem(&Adapter->HotPlugWorkItem, DelayedWorkQueue);
    return STATUS_SUCCESS;
}

static BOOLEAN
DxgkpParseEdidPreferredSignal(
    _In_reads_bytes_(128) CONST UCHAR *Edid,
    _Out_ D3DKMDT_VIDEO_SIGNAL_INFO *Signal)
{
    CONST UCHAR *Timing = &Edid[54];
    ULONG Checksum = 0;
    ULONG HActive;
    ULONG HBlank;
    ULONG VActive;
    ULONG VBlank;
    ULONG HTotal;
    ULONG VTotal;
    ULONG PixelClock;
    ULONG Index;

    if (Edid[0] != 0x00 || Edid[1] != 0xff || Edid[2] != 0xff || Edid[3] != 0xff || Edid[4] != 0xff || Edid[5] != 0xff || Edid[6] != 0xff || Edid[7] != 0x00)
        return FALSE;
    for (Index = 0; Index < 128; ++Index)
        Checksum += Edid[Index];
    if ((Checksum & 0xff) != 0)
        return FALSE;
    PixelClock = ((ULONG)Timing[1] << 8 | Timing[0]) * 10000UL;
    HActive = Timing[2] | ((ULONG)(Timing[4] & 0xf0) << 4);
    HBlank = Timing[3] | ((ULONG)(Timing[4] & 0x0f) << 8);
    VActive = Timing[5] | ((ULONG)(Timing[7] & 0xf0) << 4);
    VBlank = Timing[6] | ((ULONG)(Timing[7] & 0x0f) << 8);
    HTotal = HActive + HBlank;
    VTotal = VActive + VBlank;
    if (PixelClock == 0 || HActive == 0 || VActive == 0 || HTotal <= HActive || VTotal <= VActive || HActive > 16384 || VActive > 16384)
        return FALSE;
    RtlZeroMemory(Signal, sizeof(*Signal));
    Signal->VideoStandard = D3DKMDT_VSS_OTHER;
    Signal->TotalSize.cx = HTotal;
    Signal->TotalSize.cy = VTotal;
    Signal->ActiveSize.cx = HActive;
    Signal->ActiveSize.cy = VActive;
    Signal->VSyncFreq.Numerator = PixelClock;
    Signal->VSyncFreq.Denominator = HTotal * VTotal;
    Signal->HSyncFreq.Numerator = PixelClock;
    Signal->HSyncFreq.Denominator = HTotal;
    Signal->PixelRate = PixelClock;
    Signal->ScanLineOrdering = (Timing[17] & 0x80) != 0 ? D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST : D3DDDI_VSSLO_PROGRESSIVE;
    return TRUE;
}

static NTSTATUS
DxgkpAddEdidPreferredModes(
    _Inout_ PDXGKP_VIDPN VidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_reads_bytes_(128) CONST UCHAR *Edid)
{
    PDXGKP_VIDPN_SOURCE_MODESET SourceSet = VidPn->SourceModeSets[0];
    ULONG TargetIndex = DxgkVidPnTargetIndexFromId(VidPn, TargetId);
    PDXGKP_VIDPN_TARGET_MODESET TargetSet =
        (TargetIndex != MAXULONG) ? VidPn->TargetModeSets[TargetIndex] : NULL;
    PDXGKP_MONITOR_SOURCE_MODESET MonitorSet =
        (TargetIndex != MAXULONG) ? VidPn->MonitorModeSets[TargetIndex] : NULL;
    D3DKMDT_VIDEO_SIGNAL_INFO Signal;
    D3DKMDT_VIDPN_SOURCE_MODE SourceMode;
    D3DKMDT_VIDPN_TARGET_MODE TargetMode;
    D3DKMDT_MONITOR_SOURCE_MODE MonitorMode;
    SIZE_T Index;
    BOOLEAN Found;

    if (SourceSet == NULL || TargetSet == NULL || MonitorSet == NULL)
        return STATUS_GRAPHICS_INVALID_VIDPN;
    if (!DxgkpParseEdidPreferredSignal(Edid, &Signal))
        return STATUS_SUCCESS;
    for (Index = 0; Index < TargetSet->NumModes; ++Index)
        TargetSet->Modes[Index].Preference = D3DKMDT_MP_NOTPREFERRED;
    for (Index = 0; Index < MonitorSet->NumModes; ++Index)
        MonitorSet->Modes[Index].Preference = D3DKMDT_MP_NOTPREFERRED;
    DxgkpPopulateDefaultSourceMode(&SourceMode, SourceSet->NextModeId, Signal.ActiveSize.cx, Signal.ActiveSize.cy);
    Found = FALSE;
    for (Index = 0; Index < SourceSet->NumModes; ++Index)
    {
        if (!DxgkpAreEquivalentSourceModes(&SourceSet->Modes[Index], &SourceMode))
            continue;
        SourceSet->PinnedModeId = SourceSet->Modes[Index].Id;
        Found = TRUE;
        break;
    }
    if (!Found)
    {
        if (SourceSet->NumModes >= DXGKP_MAX_MODES)
            return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;
        SourceMode.Id = SourceSet->NextModeId++;
        SourceSet->Modes[SourceSet->NumModes++] = SourceMode;
        SourceSet->PinnedModeId = SourceMode.Id;
    }
    RtlZeroMemory(&TargetMode, sizeof(TargetMode));
    TargetMode.Id = TargetSet->NextModeId;
    TargetMode.VideoSignalInfo = Signal;
    TargetMode.Preference = D3DKMDT_MP_PREFERRED;
    Found = FALSE;
    for (Index = 0; Index < TargetSet->NumModes; ++Index)
    {
        if (!DxgkpAreEquivalentTargetModes(&TargetSet->Modes[Index], &TargetMode))
            continue;
        TargetSet->Modes[Index].Preference = D3DKMDT_MP_PREFERRED;
        TargetSet->PinnedModeId = TargetSet->Modes[Index].Id;
        Found = TRUE;
        break;
    }
    if (!Found)
    {
        if (TargetSet->NumModes >= DXGKP_MAX_MODES)
            return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;
        TargetMode.Id = TargetSet->NextModeId++;
        TargetSet->Modes[TargetSet->NumModes++] = TargetMode;
        TargetSet->PinnedModeId = TargetMode.Id;
    }
    RtlZeroMemory(&MonitorMode, sizeof(MonitorMode));
    MonitorMode.Id = MonitorSet->NextModeId;
    MonitorMode.VideoSignalInfo = Signal;
    MonitorMode.ColorBasis = D3DKMDT_CB_SRGB;
    MonitorMode.ColorCoeffDynamicRanges.FirstChannel = 8;
    MonitorMode.ColorCoeffDynamicRanges.SecondChannel = 8;
    MonitorMode.ColorCoeffDynamicRanges.ThirdChannel = 8;
    MonitorMode.Origin = D3DKMDT_MCO_MONITORDESCRIPTOR;
    MonitorMode.Preference = D3DKMDT_MP_PREFERRED;
    Found = FALSE;
    for (Index = 0; Index < MonitorSet->NumModes; ++Index)
    {
        if (!DxgkpAreEquivalentMonitorModes(&MonitorSet->Modes[Index], &MonitorMode))
            continue;
        MonitorSet->Modes[Index].Preference = D3DKMDT_MP_PREFERRED;
        Found = TRUE;
        break;
    }
    if (!Found)
    {
        if (MonitorSet->NumModes >= DXGKP_MAX_MODES)
            return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;
        MonitorMode.Id = MonitorSet->NextModeId++;
        MonitorSet->Modes[MonitorSet->NumModes++] = MonitorMode;
    }
    return STATUS_SUCCESS;
}

/*
 * Prepare the candidate for the miniport's recommendation.
 *
 * The topology is emptied and the monitor's modes are made available, but no
 * path is built here.  DxgkDdiRecommendFunctionalVidPn and
 * DxgkDdiRecommendVidPnTopology are DDIs the driver *populates*, and Windows
 * hands them a VidPN it has just created and left empty:
 * VIDPN_MGR::RecommendFunctionalVidPn calls VIDPN_MGR::CreateClientVidPn and
 * passes that fresh object as hRecommendedFunctionalVidPn, then validates what
 * came back with DMMVIDPN::IsFunctional.
 *
 * Seeding a path first made the normal driver answer impossible: asked to
 * recommend a configuration for the connected output, the driver adds a path
 * for that output, VidPnTopology_AddPath refuses it because the same target is
 * already in the topology, the driver fails its own DDI, and the rebuild dies
 * carrying the driver's error.  Nothing then ever adds a path, so the VidPN a
 * full miniport starts with -- the contractually empty one from
 * DxgkVidPnCreateForAdapter -- stays empty for the life of the adapter.
 *
 * DxgkpSeedDefaultHotPlugPath builds the OS's own path afterwards, and only if
 * the driver produced none.
 */
static NTSTATUS
DxgkpBuildHotPlugCandidate(
    _Inout_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    RtlZeroMemory(VidPn->Paths, sizeof(VidPn->Paths));
    VidPn->NumPaths = 0;
    if (!Snapshot->Connected)
        return STATUS_SUCCESS;
    if (DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId) == MAXULONG ||
        VidPn->NumSources == 0)
        return STATUS_NOT_SUPPORTED;
    if (Snapshot->EdidValid)
        return DxgkpAddEdidPreferredModes(VidPn, Snapshot->TargetId, Snapshot->Edid);
    return STATUS_SUCCESS;
}

/*
 * Wire the connected output to a source when the miniport recommended nothing.
 *
 * Which source drives which target is the OS's decision in WDDM, not the
 * driver's: Windows builds the topology on its own side through
 * VIDPN_MGR::AddPathToVidPnTopology, whose callers are OS topology
 * constructors (BTL_TOPOLOGY_CONSTRUCTOR::_AddExternalPathsToTopology,
 * ::_AddSecondaryPathToTopology, CDS_JOURNAL::_ExtendTopology), and only
 * *asks* the driver for an opinion.
 * A driver that answers STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN, or
 * that publishes neither recommendation DDI, has no opinion, and the caller's
 * own path stands.  Source 0 is the one this implementation drives; see
 * DxgkpSnapshotHotPlugMonitor.
 */
static NTSTATUS
DxgkpSeedDefaultHotPlugPath(
    _Inout_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    if (!Snapshot->Connected || VidPn->NumPaths != 0)
        return STATUS_SUCCESS;
    if (DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId) == MAXULONG ||
        VidPn->NumSources == 0)
        return STATUS_NOT_SUPPORTED;
    DxgkpPopulateDefaultPath(&VidPn->Paths[0], 0, Snapshot->TargetId);
    VidPn->NumPaths = 1;
    return STATUS_SUCCESS;
}

/*
 * Check what the miniport built into our VidPN.
 *
 * Only the invariants the rest of dxgkrnl relies on: every path names a source
 * this adapter declares and a target that resolves to one of its mode-set
 * slots, and no target is driven twice.  How *many* paths there are is not one
 * of them.  Refusing anything past a single path threw away the answer of any
 * driver that recommended a real multi-output configuration -- this Intel part
 * declares three sources and ten outputs -- and left the topology empty, which
 * is the one state that cannot be committed at all.  Several paths sharing a
 * source is clone mode and is legal.
 */
static NTSTATUS
DxgkpValidateRecommendedTopology(
    _In_ PDXGKP_VIDPN VidPn)
{
    SIZE_T Index;
    SIZE_T Other;

    if (VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
        return STATUS_GRAPHICS_INVALID_VIDPN;
    if (VidPn->NumPaths > DXGKP_MAX_PATHS)
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    for (Index = 0; Index < VidPn->NumPaths; Index++)
    {
        if (VidPn->Paths[Index].VidPnSourceId >= VidPn->NumSources ||
            DxgkVidPnTargetIndexFromId(VidPn, VidPn->Paths[Index].VidPnTargetId) == MAXULONG)
        {
            DXGKRNL_ERR("DxgkpValidateRecommendedTopology: recommended path %Iu names "
                        "source %u target %u, which this VidPN (%lu sources, %lu "
                        "targets) does not describe\n",
                        Index,
                        VidPn->Paths[Index].VidPnSourceId,
                        VidPn->Paths[Index].VidPnTargetId,
                        VidPn->NumSources, VidPn->NumTargets);
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }
        for (Other = 0; Other < Index; Other++)
        {
            if (VidPn->Paths[Other].VidPnTargetId != VidPn->Paths[Index].VidPnTargetId)
                continue;
            DXGKRNL_ERR("DxgkpValidateRecommendedTopology: recommended paths %Iu and "
                        "%Iu both drive target %u\n",
                        Other, Index, VidPn->Paths[Index].VidPnTargetId);
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }
    }
    return STATUS_SUCCESS;
}

/*
 * Ask the miniport for a topology when no functional VidPn could be had.
 *
 * These are two different questions and Windows asks both.  A functional VidPn
 * is a complete configuration -- sources, targets, paths and modes.  A topology
 * is only the wiring: which source drives which target.  A driver that cannot
 * produce the former may still know the latter, and on a display-only or
 * headless adapter that is the usual case.
 *
 * The reason code tells the driver *why* it is being asked, which changes what
 * a sensible answer is: at initialization with no last-known-good configuration
 * to fall back on, a driver should offer something rather than nothing.
 *
 * A refusal is not a failure.  STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY is
 * the driver saying it has no opinion, and the caller's own default path stands.
 */
/*
 * Tell the miniport that an *already active* path's attributes changed.
 *
 * This is not the same event as committing a VidPn, and that is why it is a
 * separate DDI.  A commit says "this is the new configuration"; this says "the
 * configuration is unchanged, but the rotation or scaling on a path you are
 * already driving is different now".  A driver that only ever saw commits would
 * keep scanning out with the previous transformation, which is visible on
 * screen and reported by nothing.
 *
 * Only called when something actually differs -- announcing an unchanged path
 * would have a driver reprogram hardware for no reason on every commit.
 */
static VOID
DxgkpNotifyActivePathChanged(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *OldPath,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH *NewPath)
{
    PDXGKDDI_UPDATE_ACTIVE_VIDPN_PRESENT_PATH UpdatePath =
        DXGK_CB_FULL(Adapter, DxgkDdiUpdateActiveVidPnPresentPath);
    DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH UpdateArgs;
    NTSTATUS Status;

    if (UpdatePath == NULL)
        return;
    if (OldPath->VidPnSourceId != NewPath->VidPnSourceId ||
        OldPath->VidPnTargetId != NewPath->VidPnTargetId)
    {
        return;
    }
    if (OldPath->ContentTransformation.Scaling == NewPath->ContentTransformation.Scaling &&
        OldPath->ContentTransformation.Rotation == NewPath->ContentTransformation.Rotation &&
        OldPath->Content == NewPath->Content)
    {
        return;
    }

    RtlZeroMemory(&UpdateArgs, sizeof(UpdateArgs));
    UpdateArgs.VidPnPresentPathInfo = *NewPath;

    if (!DxgkAcquireKmdCall(Adapter))
        return;
    _SEH2_TRY
    {
        Status = UpdatePath(Adapter->MiniportDeviceContext, &UpdateArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    /*
     * Reported, not propagated.  The configuration is already committed and the
     * driver is already scanning it out; failing the commit here would roll back
     * a display that is working, to fix an attribute that is merely stale.
     */
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("UpdateActiveVidPnPresentPath refused 0x%08lX for source %u target %u\n",
                     Status, NewPath->VidPnSourceId, NewPath->VidPnTargetId);
    }
}

static NTSTATUS
DxgkpRecommendTopologyFallback(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot,
    _In_ DXGK_RECOMMENDVIDPNTOPOLOGY_REASON Reason)
{
    PDXGKDDI_RECOMMEND_VIDPN_TOPOLOGY RecommendTopology =
        DXGK_CB_FULL(Adapter, DxgkDdiRecommendVidPnTopology);
    DXGKARG_RECOMMENDVIDPNTOPOLOGY TopologyArgs;
    NTSTATUS Status;

    if (RecommendTopology == NULL || !Snapshot->Connected)
        return STATUS_SUCCESS;

    RtlZeroMemory(&TopologyArgs, sizeof(TopologyArgs));
    TopologyArgs.hVidPn = (D3DKMDT_HVIDPN)VidPn;
    TopologyArgs.VidPnSourceId = 0;
    TopologyArgs.RequestReason = Reason;
    TopologyArgs.hFallbackTopology = NULL;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = RecommendTopology(Adapter->MiniportDeviceContext, &TopologyArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    if (Status == STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY)
    {
        DXGKRNL_TRACE("RecommendVidPnTopology has no opinion for target %u\n", Snapshot->TargetId);
        return STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkDdiRecommendVidPnTopology failed 0x%08lX for target %u "
                    "(reason %d); the rebuild has no topology to commit\n",
                    Status, Snapshot->TargetId, Reason);
        return Status;
    }

    /* The driver wrote into our VidPn.  Whatever it built has to still be a
     * VidPn this adapter can drive, or the negotiation continues on something
     * malformed and fails much later with a less useful status. */
    return DxgkpValidateRecommendedTopology(VidPn);
}

static NTSTATUS
DxgkpRecommendHotPlugCandidate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKP_VIDPN VidPn,
    _In_ PDXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot)
{
    PDXGKDDI_RECOMMEND_FUNCTIONAL_VIDPN RecommendFunctionalVidPn = DXGK_CB(Adapter, DxgkDdiRecommendFunctionalVidPn);
    DXGKARG_RECOMMENDFUNCTIONALVIDPN RecommendArgs;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId = Snapshot->TargetId;
    NTSTATUS Status;

    /*
     * Offer the driver its chance to add monitor modes first.  A monitor's mode
     * set starts from what its EDID advertises, and a driver often knows modes
     * the EDID does not list -- scaled, interlaced, or panel-native timings it
     * can drive.  Windows asks before building the functional VidPn for exactly
     * that reason, so a driver that only implements this DDI is not silently
     * limited to its EDID.  A refusal costs nothing: the EDID modes stand.
     */
    if (Snapshot->Connected &&
        DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId) != MAXULONG &&
        DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId) != MAXULONG &&
        VidPn->MonitorModeSets[DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId)] != NULL &&
        DXGK_CB(Adapter, DxgkDdiRecommendMonitorModes) != NULL)
    {
        DXGKARG_RECOMMENDMONITORMODES MonitorArgs;
        NTSTATUS MonitorStatus;

        RtlZeroMemory(&MonitorArgs, sizeof(MonitorArgs));
        MonitorArgs.VideoPresentTargetId = Snapshot->TargetId;
        MonitorArgs.hMonitorSourceModeSet =
            (D3DKMDT_HMONITORSOURCEMODESET)
                VidPn->MonitorModeSets[DxgkVidPnTargetIndexFromId(VidPn, Snapshot->TargetId)];
        MonitorArgs.pMonitorSourceModeSetInterface = &g_MonitorSourceModeSetInterface;
        if (DxgkAcquireKmdCall(Adapter))
        {
            _SEH2_TRY
            {
                MonitorStatus = DXGK_CB(Adapter, DxgkDdiRecommendMonitorModes)(Adapter->MiniportDeviceContext, &MonitorArgs);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                MonitorStatus = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            DxgkReleaseKmdCall(Adapter);
            if (!NT_SUCCESS(MonitorStatus))
                DXGKRNL_TRACE("RecommendMonitorModes declined 0x%08lX for target %u\n",
                              MonitorStatus, Snapshot->TargetId);
        }
    }

    if (RecommendFunctionalVidPn == NULL)
        return DxgkpRecommendTopologyFallback(Adapter, VidPn, Snapshot,
                                              DXGK_RVT_INITIALIZATION_NOLKG);
    RtlZeroMemory(&RecommendArgs, sizeof(RecommendArgs));
    RecommendArgs.NumberOfVidPnTargets = Snapshot->Connected ? 1 : 0;
    RecommendArgs.pVidPnTargetPrioritizationVector = Snapshot->Connected ? &TargetId : NULL;
    RecommendArgs.hRecommendedFunctionalVidPn = (D3DKMDT_HVIDPN)VidPn;
    RecommendArgs.RequestReason = DXGK_RFVR_HOTKEY;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = RecommendFunctionalVidPn(Adapter->MiniportDeviceContext, &RecommendArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (Status == STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN)
    {
        /* No complete configuration on offer.  The driver may still know the
         * wiring, which is a smaller question and often answerable when the
         * larger one is not. */
        return DxgkpRecommendTopologyFallback(Adapter, VidPn, Snapshot,
                                              DXGK_RVT_AUGMENTATION_NOLKG);
    }
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("DxgkDdiRecommendFunctionalVidPn failed 0x%08lX for target %u; "
                    "the rebuild has no topology to commit\n",
                    Status, Snapshot->TargetId);
        return Status;
    }
    return DxgkpValidateRecommendedTopology(VidPn);
}

static NTSTATUS
DxgkpRecoverFailedHotPlugRollback(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
    DXGKRNL_ERR("DxgkpRecoverFailedHotPlugRollback: private TDR recovery is quarantined; failing adapter closed\n");
    DxgkBeginAdapterRundown(Adapter);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
DxgkpVidPnRebuildForHotPlugGeneration(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ LONG64 ExpectedGeneration)
{
    DXGKP_HOTPLUG_MONITOR_SNAPSHOT Snapshot;
    D3DKMDT_HVIDPN OldVidPn = NULL;
    D3DKMDT_HVIDPN Candidate = NULL;
    D3DKMDT_HVIDPN DetachedVidPn = NULL;
    PDXGKP_VIDPN CandidateObject;
    DXGKP_DISPLAY_COMMIT_RESULT CommitResult;
    DXGKP_DISPLAY_COMMIT_RESULT RollbackResult;
    ULONG OldCommittedWidth;
    ULONG OldCommittedHeight;
    D3DKMDT_VIDPN_PRESENT_PATH OldPath;
    D3DKMDT_VIDPN_PRESENT_PATH NewPath;
    BOOLEAN PathsComparable = FALSE;
    BOOLEAN KmdTransaction = FALSE;
    BOOLEAN RecoveryRequired = FALSE;
    PDXGK_CHILD_PDO_EXTENSION MatchingChild = NULL;
    KIRQL ChildOldIrql;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    /* WDDM 2.3 replaced DxgkDdiCommitVidPn with DxgkDdiSetTimingsFromVidPn;
     * a miniport that implements either can take the rebuilt topology. */
    if (DXGK_CB(Adapter, DxgkDdiCommitVidPn) == NULL &&
        DXGK_CB_FULL(Adapter, DxgkDdiSetTimingsFromVidPn) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    DxgkpBeginSharedSurfaceMutationLocked(Adapter);
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    KmdTransaction = TRUE;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    OldVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
    if (!DxgkVidPnReference(OldVidPn))
        OldVidPn = NULL;
    OldCommittedWidth = Adapter->CommittedWidth;
    OldCommittedHeight = Adapter->CommittedHeight;
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    if (OldVidPn == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }
    DxgkpBindVidPnTargetsToChildren(Adapter, (PDXGKP_VIDPN)OldVidPn);
    Status = DxgkpSnapshotHotPlugMonitor(Adapter, (PDXGKP_VIDPN)OldVidPn, ExpectedGeneration, &Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkpRefreshHotPlugEdid(Adapter, &Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkVidPnClone(OldVidPn, &Candidate);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    CandidateObject = (PDXGKP_VIDPN)Candidate;
    Status = DxgkpBuildHotPlugCandidate(CandidateObject, &Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkpRecommendHotPlugCandidate(Adapter, CandidateObject, &Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    /* Only now, once the driver has had the empty topology it is entitled to
     * and declined to fill it, does dxgkrnl wire the output itself. */
    Status = DxgkpSeedDefaultHotPlugPath(CandidateObject, &Snapshot);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    KeAcquireSpinLock(&Adapter->ChildListLock, &ChildOldIrql);
    if (!DxgkpHotPlugSnapshotCurrentLocked(Adapter, &Snapshot, ExpectedGeneration, &MatchingChild))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
        /* STATUS_RETRY is a success code, so the !NT_SUCCESS test this used to
         * carry never fired: the candidate was committed to the miniport from
         * a snapshot that had just been proved stale, and only the second
         * re-check below undid it with a full rollback mode set. */
        Status = STATUS_RETRY;
        goto Cleanup;
    }
    KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
    Status = DxgkpDisplayCommitVidPnCandidate(Adapter, Candidate, &CommitResult);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&Adapter->ChildListLock, &ChildOldIrql);
    MatchingChild = NULL;
    if ((D3DKMDT_HVIDPN)Adapter->VidPn != OldVidPn || !DxgkpHotPlugSnapshotCurrentLocked(Adapter, &Snapshot, ExpectedGeneration, &MatchingChild))
    {
        KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        Status = DxgkpDisplayCommitVidPnCandidate(Adapter, OldVidPn, &RollbackResult);
        if (NT_SUCCESS(Status))
            Status = STATUS_RETRY;
        else
        {
            NTSTATUS RollbackStatus = Status;

            (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
            Adapter->VidPnCommitted = FALSE;
            KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
            InterlockedExchange(&Adapter->TdrOwnershipUncertain, 1);
            RecoveryRequired = TRUE;
            Status = RollbackStatus;
        }
        goto Cleanup;
    }
    DetachedVidPn = (D3DKMDT_HVIDPN)Adapter->VidPn;
    /* Capture the outgoing path before it is replaced, so the comparison below
     * has something to compare against. */
    {
        PDXGKP_VIDPN OutgoingVidPn = (PDXGKP_VIDPN)Adapter->VidPn;

        if (OutgoingVidPn != NULL && OutgoingVidPn->NumPaths == 1 &&
            CandidateObject != NULL && CandidateObject->NumPaths == 1)
        {
            OldPath = OutgoingVidPn->Paths[0];
            NewPath = CandidateObject->Paths[0];
            PathsComparable = TRUE;
        }
    }
    Adapter->VidPn = Candidate;
    Adapter->CommittedWidth = CommitResult.CommittedWidth;
    Adapter->CommittedHeight = CommitResult.CommittedHeight;
    Adapter->VidPnCommitted = CommitResult.VidPnCommitted;
    if (Snapshot.Connected && Snapshot.EdidValid && MatchingChild != NULL)
    {
        RtlCopyMemory(MatchingChild->Edid, Snapshot.Edid, sizeof(MatchingChild->Edid));
        MatchingChild->EdidValid = TRUE;
    }
    Candidate = NULL;
    KeReleaseSpinLock(&Adapter->ChildListLock, ChildOldIrql);
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    if (CommitResult.CommittedWidth != OldCommittedWidth || CommitResult.CommittedHeight != OldCommittedHeight || !Snapshot.Connected)
        DxgkpDestroySharedPrimaryLocked(Adapter);
    /* After the new VidPn is published and the VidPn mutex is dropped, but
     * while the KMD transaction still holds the miniport. */
    if (PathsComparable)
        DxgkpNotifyActivePathChanged(Adapter, &OldPath, &NewPath);
    DXGKRNL_TRACE("DxgkVidPnRebuildForHotPlug: atomically published %p replacing %p connected=%u target=%u mode=%ux%u\n", Adapter->VidPn, DetachedVidPn, Snapshot.Connected, Snapshot.TargetId, CommitResult.CommittedWidth, CommitResult.CommittedHeight);
    Status = STATUS_SUCCESS;

Cleanup:
    if (KmdTransaction)
    {
        DxgkEndKmdTransaction(Adapter);
        KmdTransaction = FALSE;
    }
    if (RecoveryRequired)
    {
        NTSTATUS RecoveryStatus = DxgkpRecoverFailedHotPlugRollback(Adapter);

        Status = NT_SUCCESS(RecoveryStatus) ? STATUS_RETRY : RecoveryStatus;
    }
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    if (DetachedVidPn != NULL)
        DxgkVidPnDestroy(DetachedVidPn);
    if (OldVidPn != NULL)
        DxgkVidPnDestroy(OldVidPn);
    if (Candidate != NULL)
        DxgkVidPnDestroy(Candidate);
    return Status;
}

NTSTATUS
DxgkVidPnRebuildForHotPlug(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG64 ExpectedGeneration;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    ExpectedGeneration = InterlockedCompareExchange64(&Adapter->HotPlugGeneration, 0, 0);
    Status = DxgkpVidPnRebuildForHotPlugGeneration(Adapter, ExpectedGeneration);
    if (Status == STATUS_RETRY)
        Status = DxgkVidPnQueueHotPlugRebuild(Adapter);
    return Status;
}

/* ========================================================================
 * DxgkCbQueryVidPnInterface
 * ====================================================================== */

NTSTATUS
APIENTRY
DxgkCbQueryVidPnInterface(
    IN_CONST_D3DKMDT_HVIDPN hVidPn,
    IN_CONST_DXGK_VIDPN_INTERFACE_VERSION VidPnInterfaceVersion,
    DEREF_OUT_CONST_PPDXGK_VIDPN_INTERFACE ppVidPnInterface)
{
    PDXGKP_VIDPN VidPn;

    DXGKRNL_TRACE("DxgkCbQueryVidPnInterface: hVidPn=%p version=%d\n",
                  hVidPn, VidPnInterfaceVersion);

    if (ppVidPnInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppVidPnInterface = NULL;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
    {
        DXGKRNL_ERR("DxgkCbQueryVidPnInterface: invalid hVidPn %p\n", hVidPn);
        return STATUS_GRAPHICS_INVALID_VIDPN;
    }

    switch (VidPnInterfaceVersion)
    {
        case DXGK_VIDPN_INTERFACE_VERSION_V1:
            *ppVidPnInterface = &g_VidPnInterfaceV1;
            return STATUS_SUCCESS;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
        case DXGK_VIDPN_INTERFACE_VERSION_V2:
            *ppVidPnInterface = &g_VidPnInterfaceV2;
            return STATUS_SUCCESS;
#endif

        default:
            DXGKRNL_WARN("DxgkCbQueryVidPnInterface: unsupported version %d\n",
                         VidPnInterfaceVersion);
            return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
APIENTRY
DxgkCbQueryMonitorInterface(
    IN_CONST_HANDLE hAdapter,
    IN_CONST_DXGK_MONITOR_INTERFACE_VERSION MonitorInterfaceVersion,
    DEREF_OUT_CONST_PPDXGK_MONITOR_INTERFACE ppMonitorInterface)
{
    DXGKRNL_TRACE("DxgkCbQueryMonitorInterface: hAdapter=%p version=%u\n",
                  hAdapter, MonitorInterfaceVersion);

    if (ppMonitorInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppMonitorInterface = NULL;

    if (hAdapter == NULL)
    {
        DXGKRNL_ERR("DxgkCbQueryMonitorInterface: NULL hAdapter\n");
        return STATUS_INVALID_PARAMETER;
    }

    switch (MonitorInterfaceVersion)
    {
        case DXGK_MONITOR_INTERFACE_VERSION_V1:
            *ppMonitorInterface = &g_MonitorInterfaceV1;
            return STATUS_SUCCESS;

        case DXGK_MONITOR_INTERFACE_VERSION_V2:
            *ppMonitorInterface =
                (CONST DXGK_MONITOR_INTERFACE *)&g_MonitorInterfaceV2;
            return STATUS_SUCCESS;

        default:
            DXGKRNL_WARN("DxgkCbQueryMonitorInterface: unsupported version %u\n",
                         MonitorInterfaceVersion);
            return STATUS_NOT_SUPPORTED;
    }
}

/* ========================================================================
 * Top-level VidPN interface (DXGK_VIDPN_INTERFACE) implementations
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPn_GetTopology(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY*                     phVidPnTopology,
    _Out_ CONST DXGK_VIDPNTOPOLOGY_INTERFACE**        ppVidPnTopologyInterface)
{
    PDXGKP_VIDPN VidPn;

    if (phVidPnTopology == NULL || ppVidPnTopologyInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *phVidPnTopology           = (D3DKMDT_HVIDPNTOPOLOGY)VidPn;
    *ppVidPnTopologyInterface  = &g_VidPnTopologyInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AcquireSourceModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID               VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                phVidPnSourceModeSet,
    _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**   ppVidPnSourceModeSetInterface)
{
    PDXGKP_VIDPN VidPn;

    if (phVidPnSourceModeSet == NULL || ppVidPnSourceModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources || VidPn->SourceModeSets[VidPnSourceId] == NULL)
    {
        DXGKRNL_WARN("VidPn_AcquireSourceModeSet: invalid source %u (max %lu)\n",
                     VidPnSourceId, VidPn->NumSources);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    *phVidPnSourceModeSet          = (D3DKMDT_HVIDPNSOURCEMODESET)VidPn->SourceModeSets[VidPnSourceId];
    *ppVidPnSourceModeSetInterface = &g_VidPnSourceModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_ReleaseSourceModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet)
{
    UNREFERENCED_PARAMETER(hVidPn);
    UNREFERENCED_PARAMETER(hVidPnSourceModeSet);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_CreateNewSourceModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID               VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                phVidPnSourceModeSet,
    _Out_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE**   ppVidPnSourceModeSetInterface)
{
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (phVidPnSourceModeSet == NULL || ppVidPnSourceModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    /* Clear existing mode set for this source (reuse allocation). */
    ModeSet = VidPn->SourceModeSets[VidPnSourceId];
    if (ModeSet == NULL)
    {
        ModeSet = DxgkpAllocateSourceModeSet(VidPn, VidPnSourceId);
        if (ModeSet == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        VidPn->SourceModeSets[VidPnSourceId] = ModeSet;
    }
    else
    {
        ModeSet->NumModes     = 0;
        ModeSet->PinnedModeId = (UINT)-1;
        ModeSet->NextModeId   = 0;
    }
    VidPn->NewSourceModeValid = FALSE;

    *phVidPnSourceModeSet          = (D3DKMDT_HVIDPNSOURCEMODESET)ModeSet;
    *ppVidPnSourceModeSetInterface = &g_VidPnSourceModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AssignSourceModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet)
{
    PDXGKP_VIDPN VidPn;

    UNREFERENCED_PARAMETER(hVidPnSourceModeSet);

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    /* In-place model: assignment is a no-op. */
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AssignMultisamplingMethodSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ CONST D3DDDI_MULTISAMPLINGMETHOD*            pMultisamplingMethod)
{
    PDXGKP_VIDPN VidPn;
    UNREFERENCED_PARAMETER(pMultisamplingMethod);

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= VidPn->NumSources)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AcquireTargetModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                phVidPnTargetModeSet,
    _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE**   ppVidPnTargetModeSetInterface)
{
    PDXGKP_VIDPN VidPn;
    ULONG TargetIndex;

    if (phVidPnTargetModeSet == NULL || ppVidPnTargetModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    TargetIndex = DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId);
    if (TargetIndex == MAXULONG || VidPn->TargetModeSets[TargetIndex] == NULL)
    {
        DXGKRNL_WARN("VidPn_AcquireTargetModeSet: invalid target %u (max %lu)\n",
                     VidPnTargetId, VidPn->NumTargets);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }

    *phVidPnTargetModeSet          = (D3DKMDT_HVIDPNTARGETMODESET)VidPn->TargetModeSets[TargetIndex];
    *ppVidPnTargetModeSetInterface = &g_VidPnTargetModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_ReleaseTargetModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet)
{
    UNREFERENCED_PARAMETER(hVidPn);
    UNREFERENCED_PARAMETER(hVidPnTargetModeSet);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_CreateNewTargetModeSet(
    _In_  D3DKMDT_HVIDPN                              hVidPn,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                phVidPnTargetModeSet,
    _Out_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE**   ppVidPnTargetModeSetInterface)
{
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (phVidPnTargetModeSet == NULL || ppVidPnTargetModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId) == MAXULONG)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    ModeSet = (DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId) != MAXULONG)
                  ? VidPn->TargetModeSets[DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId)] : NULL;
    if (ModeSet == NULL)
    {
        ModeSet = DxgkpAllocateTargetModeSet(VidPn, VidPnTargetId);
        if (ModeSet == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        VidPn->TargetModeSets[DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId)] = ModeSet;
    }
    else
    {
        ModeSet->NumModes     = 0;
        ModeSet->PinnedModeId = (UINT)-1;
        ModeSet->NextModeId   = 0;
    }
    VidPn->NewTargetModeValid = FALSE;

    *phVidPnTargetModeSet          = (D3DKMDT_HVIDPNTARGETMODESET)ModeSet;
    *ppVidPnTargetModeSetInterface = &g_VidPnTargetModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPn_AssignTargetModeSet(
    _In_ D3DKMDT_HVIDPN                               hVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID                VidPnTargetId,
    _In_ D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet)
{
    PDXGKP_VIDPN VidPn;

    UNREFERENCED_PARAMETER(hVidPnTargetModeSet);

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (DxgkVidPnTargetIndexFromId(VidPn, VidPnTargetId) == MAXULONG)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    return STATUS_SUCCESS;
}

/* ========================================================================
 * Topology interface (DXGK_VIDPNTOPOLOGY_INTERFACE) implementations
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPnTopology_GetNumPaths(
    _In_  D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology,
    _Out_ SIZE_T*                pNumPaths)
{
    PDXGKP_VIDPN VidPn;

    if (pNumPaths == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    *pNumPaths = VidPn->NumPaths;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_GetNumPathsFromSource(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
    _Out_ SIZE_T*                               pNumPathsFromSource)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T Count = 0;
    SIZE_T i;

    if (pNumPathsFromSource == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId)
            Count++;
    }

    *pNumPathsFromSource = Count;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_EnumPathTargetsFromSource(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
    _In_  D3DKMDT_VIDPN_PRESENT_PATH_INDEX      PathIndex,
    _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID*       pVidPnTargetId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i, MatchIndex = 0;

    if (pVidPnTargetId == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId)
        {
            if (MatchIndex == PathIndex)
            {
                *pVidPnTargetId = VidPn->Paths[i].VidPnTargetId;
                return STATUS_SUCCESS;
            }
            MatchIndex++;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
}

static NTSTATUS APIENTRY
VidPnTopology_GetPathSourceFromTarget(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID        VidPnTargetId,
    _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID*       pVidPnSourceId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    if (pVidPnSourceId == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnTargetId == VidPnTargetId)
        {
            *pVidPnSourceId = VidPn->Paths[i].VidPnSourceId;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_TARGET_NOT_IN_TOPOLOGY;
}

static NTSTATUS APIENTRY
VidPnTopology_AcquirePathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID        VidPnSourceId,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID        VidPnTargetId,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    if (ppVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == VidPnTargetId)
        {
            *ppVidPnPresentPathInfo = &VidPn->Paths[i];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

static NTSTATUS APIENTRY
VidPnTopology_AcquireFirstPathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppFirstVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;

    if (ppFirstVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPn->NumPaths == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstVidPnPresentPathInfo = &VidPn->Paths[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_AcquireNextPathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _In_  CONST D3DKMDT_VIDPN_PRESENT_PATH*    pVidPnPresentPathInfo,
    _Out_ CONST D3DKMDT_VIDPN_PRESENT_PATH**   ppNextVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    if (ppNextVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL || pVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Find current path by pointer. */
    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (&VidPn->Paths[i] == pVidPnPresentPathInfo)
        {
            if (i + 1 < VidPn->NumPaths)
            {
                *ppNextVidPnPresentPathInfo = &VidPn->Paths[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    /* Fallback: match by source+target IDs. */
    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == pVidPnPresentPathInfo->VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == pVidPnPresentPathInfo->VidPnTargetId)
        {
            if (i + 1 < VidPn->NumPaths)
            {
                *ppNextVidPnPresentPathInfo = &VidPn->Paths[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
VidPnTopology_UpdatePathSupportInfo(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH*     pVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL || pVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Find the path and update its support info. */
    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == pVidPnPresentPathInfo->VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == pVidPnPresentPathInfo->VidPnTargetId)
        {
            VidPn->Paths[i].ContentTransformation = pVidPnPresentPathInfo->ContentTransformation;
            VidPn->Paths[i].VidPnTargetColorBasis = pVidPnPresentPathInfo->VidPnTargetColorBasis;
            VidPn->Paths[i].VidPnTargetColorCoeffDynamicRanges =
                pVidPnPresentPathInfo->VidPnTargetColorCoeffDynamicRanges;
            VidPn->Paths[i].Content = pVidPnPresentPathInfo->Content;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

static NTSTATUS APIENTRY
VidPnTopology_ReleasePathInfo(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ CONST D3DKMDT_VIDPN_PRESENT_PATH*     pVidPnPresentPathInfo)
{
    UNREFERENCED_PARAMETER(hVidPnTopology);
    UNREFERENCED_PARAMETER(pVidPnPresentPathInfo);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_CreateNewPathInfo(
    _In_  D3DKMDT_HVIDPNTOPOLOGY               hVidPnTopology,
    _Out_ D3DKMDT_VIDPN_PRESENT_PATH**         ppNewVidPnPresentPathInfo)
{
    PDXGKP_VIDPN VidPn;

    if (ppNewVidPnPresentPathInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewVidPnPresentPathInfo = NULL;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&VidPn->NewPath, sizeof(VidPn->NewPath));
    VidPn->NewPathValid = TRUE;
    *ppNewVidPnPresentPathInfo = &VidPn->NewPath;

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_AddPath(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH*           pVidPnPresentPath)
{
    PDXGKP_VIDPN VidPn;

    if (pVidPnPresentPath == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPn->NumPaths >= DXGKP_MAX_PATHS)
    {
        DXGKRNL_WARN("VidPnTopology_AddPath: topology full (%Iu paths)\n",
                     VidPn->NumPaths);
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    }

    /* Validate source/target IDs. */
    if (pVidPnPresentPath->VidPnSourceId >= VidPn->NumSources ||
        DxgkVidPnTargetIndexFromId(VidPn, pVidPnPresentPath->VidPnTargetId) == MAXULONG)
    {
        DXGKRNL_WARN("VidPnTopology_AddPath: src=%u tgt=%u out of range\n",
                     pVidPnPresentPath->VidPnSourceId,
                     pVidPnPresentPath->VidPnTargetId);
        return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
    }

    /* Check for duplicate target (each target can only appear once). */
    {
        SIZE_T i;
        for (i = 0; i < VidPn->NumPaths; i++)
        {
            if (VidPn->Paths[i].VidPnTargetId == pVidPnPresentPath->VidPnTargetId)
            {
                DXGKRNL_WARN("VidPnTopology_AddPath: target %u already in topology\n",
                             pVidPnPresentPath->VidPnTargetId);
                return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
            }
        }
    }

    RtlCopyMemory(&VidPn->Paths[VidPn->NumPaths], pVidPnPresentPath,
                   sizeof(D3DKMDT_VIDPN_PRESENT_PATH));
    VidPn->NumPaths++;
    VidPn->NewPathValid = FALSE;

    DXGKRNL_TRACE("VidPnTopology_AddPath: added path src=%u tgt=%u (now %Iu paths)\n",
                  pVidPnPresentPath->VidPnSourceId,
                  pVidPnPresentPath->VidPnTargetId,
                  VidPn->NumPaths);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTopology_RemovePath(
    _In_ D3DKMDT_HVIDPNTOPOLOGY                hVidPnTopology,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID         VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID         VidPnTargetId)
{
    PDXGKP_VIDPN VidPn;
    SIZE_T i;

    VidPn = DxgkpTopologyFromHandle(hVidPnTopology);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < VidPn->NumPaths; i++)
    {
        if (VidPn->Paths[i].VidPnSourceId == VidPnSourceId &&
            VidPn->Paths[i].VidPnTargetId == VidPnTargetId)
        {
            /* Shift remaining paths down. */
            if (i + 1 < VidPn->NumPaths)
            {
                RtlMoveMemory(&VidPn->Paths[i], &VidPn->Paths[i + 1],
                               (VidPn->NumPaths - i - 1) * sizeof(D3DKMDT_VIDPN_PRESENT_PATH));
            }
            VidPn->NumPaths--;
            DXGKRNL_TRACE("VidPnTopology_RemovePath: removed src=%u tgt=%u (now %Iu paths)\n",
                          VidPnSourceId, VidPnTargetId, VidPn->NumPaths);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
}

/* ========================================================================
 * Source mode set interface (DXGK_VIDPNSOURCEMODESET_INTERFACE)
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPnSourceModeSet_GetNumModes(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET           hVidPnSourceModeSet,
    _Out_ CONST SIZE_T*                         pNumModes)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (pNumModes == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *(SIZE_T*)pNumModes = ModeSet->NumModes;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AcquireFirstModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**            ppFirstVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (ppFirstVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->NumModes == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstVidPnSourceModeInfo = &ModeSet->Modes[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AcquireNextModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _In_  CONST D3DKMDT_VIDPN_SOURCE_MODE*             pVidPnSourceModeInfo,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**            ppNextVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppNextVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL || pVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Find by pointer first. */
    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (&ModeSet->Modes[i] == pVidPnSourceModeInfo)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextVidPnSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    /* Fallback: match by ID. */
    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == pVidPnSourceModeInfo->Id)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextVidPnSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AcquirePinnedModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _Out_ CONST D3DKMDT_VIDPN_SOURCE_MODE**            ppPinnedVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppPinnedVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppPinnedVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->PinnedModeId == (UINT)-1)
        return STATUS_SUCCESS;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == ModeSet->PinnedModeId)
        {
            *ppPinnedVidPnSourceModeInfo = &ModeSet->Modes[i];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_ReleaseModeInfo(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
    _In_ CONST D3DKMDT_VIDPN_SOURCE_MODE*              pVidPnSourceModeInfo)
{
    UNREFERENCED_PARAMETER(hVidPnSourceModeSet);
    UNREFERENCED_PARAMETER(pVidPnSourceModeInfo);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_CreateNewModeInfo(
    _In_  D3DKMDT_HVIDPNSOURCEMODESET                  hVidPnSourceModeSet,
    _Out_ D3DKMDT_VIDPN_SOURCE_MODE**                  ppNewVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;

    if (ppNewVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewVidPnSourceModeInfo = NULL;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL || ModeSet->Owner == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&ModeSet->Owner->NewSourceMode, sizeof(D3DKMDT_VIDPN_SOURCE_MODE));
    ModeSet->Owner->NewSourceMode.Id = ModeSet->NextModeId;
    ModeSet->Owner->NewSourceModeValid = TRUE;

    *ppNewVidPnSourceModeInfo = &ModeSet->Owner->NewSourceMode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_AddMode(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
    _In_ D3DKMDT_VIDPN_SOURCE_MODE*                    pVidPnSourceModeInfo)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    D3DKMDT_VIDPN_SOURCE_MODE NewMode;
    SIZE_T i;

    if (pVidPnSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&NewMode, pVidPnSourceModeInfo, sizeof(NewMode));
    DxgkpNormalizeSourceMode(&NewMode);

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (DxgkpAreEquivalentSourceModes(&ModeSet->Modes[i], &NewMode))
        {
            if (ModeSet->Owner)
                ModeSet->Owner->NewSourceModeValid = FALSE;
            return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
        }
    }

    if (ModeSet->NumModes >= DXGKP_MAX_MODES)
        return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;

    RtlCopyMemory(&ModeSet->Modes[ModeSet->NumModes], &NewMode, sizeof(NewMode));
    ModeSet->Modes[ModeSet->NumModes].Id = ModeSet->NextModeId;
    ModeSet->NextModeId++;
    ModeSet->NumModes++;

    if (ModeSet->Owner)
        ModeSet->Owner->NewSourceModeValid = FALSE;

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnSourceModeSet_PinMode(
    _In_ D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet,
    _In_ D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID          VidPnSourceModeId)
{
    PDXGKP_VIDPN_SOURCE_MODESET ModeSet;
    SIZE_T i;

    ModeSet = DxgkpSourceModeSetFromHandle(hVidPnSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == VidPnSourceModeId)
        {
            ModeSet->PinnedModeId = VidPnSourceModeId;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
}

/* ========================================================================
 * Target mode set interface (DXGK_VIDPNTARGETMODESET_INTERFACE)
 * ====================================================================== */

static NTSTATUS APIENTRY
VidPnTargetModeSet_GetNumModes(
    _In_  D3DKMDT_HVIDPNTARGETMODESET           hVidPnTargetModeSet,
    _Out_ CONST SIZE_T*                         pNumModes)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (pNumModes == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *(SIZE_T*)pNumModes = ModeSet->NumModes;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AcquireFirstModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**            ppFirstVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (ppFirstVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->NumModes == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstVidPnTargetModeInfo = &ModeSet->Modes[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AcquireNextModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _In_  CONST D3DKMDT_VIDPN_TARGET_MODE*             pVidPnTargetModeInfo,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**            ppNextVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    SIZE_T i;

    if (ppNextVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL || pVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (&ModeSet->Modes[i] == pVidPnTargetModeInfo)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextVidPnTargetModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    /* Fallback: match by ID. */
    if ((ULONG_PTR)pVidPnTargetModeInfo >= (ULONG_PTR)MmSystemRangeStart)
    {
        for (i = 0; i < ModeSet->NumModes; i++)
        {
            if (ModeSet->Modes[i].Id == pVidPnTargetModeInfo->Id)
            {
                if (i + 1 < ModeSet->NumModes)
                {
                    *ppNextVidPnTargetModeInfo = &ModeSet->Modes[i + 1];
                    return STATUS_SUCCESS;
                }
                return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
            }
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AcquirePinnedModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _Out_ CONST D3DKMDT_VIDPN_TARGET_MODE**            ppPinnedVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    SIZE_T i;

    if (ppPinnedVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppPinnedVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->PinnedModeId == (UINT)-1)
        return STATUS_SUCCESS;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == ModeSet->PinnedModeId)
        {
            *ppPinnedVidPnTargetModeInfo = &ModeSet->Modes[i];
            return STATUS_SUCCESS;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_ReleaseModeInfo(
    _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
    _In_ CONST D3DKMDT_VIDPN_TARGET_MODE*              pVidPnTargetModeInfo)
{
    UNREFERENCED_PARAMETER(hVidPnTargetModeSet);
    UNREFERENCED_PARAMETER(pVidPnTargetModeInfo);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_CreateNewModeInfo(
    _In_  D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet,
    _Out_ D3DKMDT_VIDPN_TARGET_MODE**                  ppNewVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;

    if (ppNewVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewVidPnTargetModeInfo = NULL;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL || ModeSet->Owner == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&ModeSet->Owner->NewTargetMode, sizeof(D3DKMDT_VIDPN_TARGET_MODE));
    ModeSet->Owner->NewTargetMode.Id = ModeSet->NextModeId;
    ModeSet->Owner->NewTargetModeValid = TRUE;

    *ppNewVidPnTargetModeInfo = &ModeSet->Owner->NewTargetMode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_AddMode(
    _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
    _In_ D3DKMDT_VIDPN_TARGET_MODE*                    pVidPnTargetModeInfo)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    D3DKMDT_VIDPN_TARGET_MODE NewMode;
    SIZE_T i;

    if (pVidPnTargetModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&NewMode, pVidPnTargetModeInfo, sizeof(NewMode));
    DxgkpNormalizeTargetMode(&NewMode);

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (DxgkpAreEquivalentTargetModes(&ModeSet->Modes[i], &NewMode))
        {
            if (ModeSet->Owner)
                ModeSet->Owner->NewTargetModeValid = FALSE;
            return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
        }
    }

    if (ModeSet->NumModes >= DXGKP_MAX_MODES)
        return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;

    RtlCopyMemory(&ModeSet->Modes[ModeSet->NumModes], &NewMode, sizeof(NewMode));
    ModeSet->Modes[ModeSet->NumModes].Id = ModeSet->NextModeId;
    ModeSet->NextModeId++;
    ModeSet->NumModes++;

    if (ModeSet->Owner)
        ModeSet->Owner->NewTargetModeValid = FALSE;

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
VidPnTargetModeSet_PinMode(
    _In_ D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet,
    _In_ D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID          VidPnTargetModeId)
{
    PDXGKP_VIDPN_TARGET_MODESET ModeSet;
    SIZE_T i;

    ModeSet = DxgkpTargetModeSetFromHandle(hVidPnTargetModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == VidPnTargetModeId)
        {
            ModeSet->PinnedModeId = VidPnTargetModeId;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET_MODE;
}

/* ========================================================================
 * Monitor source mode set interface (DXGK_MONITORSOURCEMODESET_INTERFACE)
 * ====================================================================== */

static NTSTATUS APIENTRY
MonitorSourceModeSet_GetNumModes(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ SIZE_T*                                    pNumModes)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    if (pNumModes == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *pNumModes = ModeSet->NumModes;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AcquirePreferredModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppFirstMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppFirstMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Preference == D3DKMDT_MP_PREFERRED)
        {
            *ppFirstMonitorSourceModeInfo = &ModeSet->Modes[i];
            return STATUS_SUCCESS;
        }
    }

    if (ModeSet->NumModes > 0)
    {
        *ppFirstMonitorSourceModeInfo = &ModeSet->Modes[ModeSet->NumModes - 1];
        return STATUS_SUCCESS;
    }

    return STATUS_GRAPHICS_DATASET_IS_EMPTY;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AcquireFirstModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppFirstMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    if (ppFirstMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppFirstMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ModeSet->NumModes == 0)
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;

    *ppFirstMonitorSourceModeInfo = &ModeSet->Modes[0];
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AcquireNextModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _In_  CONST D3DKMDT_MONITOR_SOURCE_MODE*         pMonitorSourceModeInfo,
    _Out_ CONST D3DKMDT_MONITOR_SOURCE_MODE**        ppNextMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;
    SIZE_T i;

    if (ppNextMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNextMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL || pMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (&ModeSet->Modes[i] == pMonitorSourceModeInfo)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextMonitorSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (ModeSet->Modes[i].Id == pMonitorSourceModeInfo->Id)
        {
            if (i + 1 < ModeSet->NumModes)
            {
                *ppNextMonitorSourceModeInfo = &ModeSet->Modes[i + 1];
                return STATUS_SUCCESS;
            }
            return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
        }
    }

    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_CreateNewModeInfo(
    _In_  D3DKMDT_HMONITORSOURCEMODESET              hMonitorSourceModeSet,
    _Out_ D3DKMDT_MONITOR_SOURCE_MODE**              ppNewMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    if (ppNewMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    *ppNewMonitorSourceModeInfo = NULL;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    {
        static D3DKMDT_MONITOR_SOURCE_MODE s_ScratchMonitorMode;
        RtlZeroMemory(&s_ScratchMonitorMode, sizeof(s_ScratchMonitorMode));
        s_ScratchMonitorMode.Id = ModeSet->NextModeId;
        *ppNewMonitorSourceModeInfo = &s_ScratchMonitorMode;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_AddMode(
    _In_ D3DKMDT_HMONITORSOURCEMODESET               hMonitorSourceModeSet,
    _In_ D3DKMDT_MONITOR_SOURCE_MODE*                pMonitorSourceModeInfo)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;
    D3DKMDT_MONITOR_SOURCE_MODE NewMode;
    SIZE_T i;

    if (pMonitorSourceModeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&NewMode, pMonitorSourceModeInfo, sizeof(NewMode));
    DxgkpNormalizeMonitorMode(&NewMode);

    for (i = 0; i < ModeSet->NumModes; i++)
    {
        if (DxgkpAreEquivalentMonitorModes(&ModeSet->Modes[i], &NewMode))
            return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
    }

    if (ModeSet->NumModes >= DXGKP_MAX_MODES)
        return STATUS_GRAPHICS_RESOURCES_NOT_RELATED;

    RtlCopyMemory(&ModeSet->Modes[ModeSet->NumModes], &NewMode, sizeof(NewMode));
    ModeSet->Modes[ModeSet->NumModes].Id = ModeSet->NextModeId;
    ModeSet->NextModeId++;
    ModeSet->NumModes++;

    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
MonitorSourceModeSet_ReleaseModeInfo(
    _In_ D3DKMDT_HMONITORSOURCEMODESET               hMonitorSourceModeSet,
    _In_ CONST D3DKMDT_MONITOR_SOURCE_MODE*          pMonitorSourceModeInfo)
{
    UNREFERENCED_PARAMETER(hMonitorSourceModeSet);
    UNREFERENCED_PARAMETER(pMonitorSourceModeInfo);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Monitor interface implementations
 * ====================================================================== */

static NTSTATUS APIENTRY
Monitor_AcquireMonitorSourceModeSet(
    _In_  D3DKMDT_ADAPTER                              hAdapter,
    _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID               VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORSOURCEMODESET*               phMonitorSourceModeSet,
    _Out_ CONST DXGK_MONITORSOURCEMODESET_INTERFACE**  ppMonitorSourceModeSetInterface)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKP_VIDPN     VidPn;

    if (phMonitorSourceModeSet == NULL || ppMonitorSourceModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    *phMonitorSourceModeSet = NULL;
    *ppMonitorSourceModeSetInterface = NULL;

    Adapter = (PDXGKRNL_ADAPTER)hAdapter;
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (!DxgkVidPnReference((D3DKMDT_HVIDPN)VidPn))
    {
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);

    if (DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId) == MAXULONG ||
        DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId) == MAXULONG ||
        VidPn->MonitorModeSets[DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId)] == NULL)
    {
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)VidPn);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }

    *phMonitorSourceModeSet = (D3DKMDT_HMONITORSOURCEMODESET)VidPn->MonitorModeSets[DxgkVidPnTargetIndexFromId(VidPn, VideoPresentTargetId)];
    *ppMonitorSourceModeSetInterface = &g_MonitorSourceModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
Monitor_ReleaseMonitorSourceModeSet(
    _In_ D3DKMDT_ADAPTER                               hAdapter,
    _In_ D3DKMDT_HMONITORSOURCEMODESET                 hMonitorSourceModeSet)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);

    if (ModeSet == NULL || ModeSet->Owner == NULL || ModeSet->Owner->Adapter != (PDXGKRNL_ADAPTER)hAdapter)
        return STATUS_INVALID_PARAMETER;
    DxgkVidPnDestroy((D3DKMDT_HVIDPN)ModeSet->Owner);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
Monitor_GetMonitorFrequencyRangeSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORFREQUENCYRANGESET *phMonitorFrequencyRangeSet,
    _Out_ CONST DXGK_MONITORFREQUENCYRANGESET_INTERFACE
        **ppMonitorFrequencyRangeSetInterface)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(VideoPresentTargetId);

    if (phMonitorFrequencyRangeSet == NULL ||
        ppMonitorFrequencyRangeSetInterface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *phMonitorFrequencyRangeSet = NULL;
    *ppMonitorFrequencyRangeSetInterface = NULL;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS APIENTRY
Monitor_GetMonitorDescriptorSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORDESCRIPTORSET *phMonitorDescriptorSet,
    _Out_ CONST DXGK_MONITORDESCRIPTORSET_INTERFACE
        **ppMonitorDescriptorSetInterface)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(VideoPresentTargetId);

    if (phMonitorDescriptorSet == NULL ||
        ppMonitorDescriptorSetInterface == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *phMonitorDescriptorSet = NULL;
    *ppMonitorDescriptorSetInterface = NULL;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS APIENTRY
Monitor_GetAdditionalMonitorModeSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ UINT *pNumberModes,
    _Out_ DXGK_TARGETMODE_DETAIL_TIMING **ppAdditionalModesSet)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(VideoPresentTargetId);

    if (pNumberModes == NULL || ppAdditionalModesSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *pNumberModes = 0;
    *ppAdditionalModesSet = NULL;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS APIENTRY
Monitor_ReleaseAdditionalMonitorModeSet(
    _In_ D3DKMDT_ADAPTER hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _In_ CONST DXGK_TARGETMODE_DETAIL_TIMING *pAdditionalModesSet)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(VideoPresentTargetId);

    if (pAdditionalModesSet == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

/* ========================================================================
 * D3DKMT API implementations
 * ====================================================================== */

static VOID
DxgkpDestroySharedPrimaryLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKVMM_RESOURCE Resource = NULL;

    if (Adapter == NULL)
        return;

    DxgkpBeginSharedSurfaceMutationLocked(Adapter);

    if (Adapter->SharedPrimaryAllocationHandle != NULL)
    {
        /*
         * Display-only adapters point Adapter->ShadowFb at this allocation's
         * CPU VA. Shared-surface rundown above drained every retained snapshot,
         * so clear the allocation-backed alias before destroying its mapping.
         */
        if (Adapter->MiniportContext != NULL &&
            Adapter->MiniportContext->IsDisplayOnlyDriver &&
            !Adapter->ShadowFbPoolOwned)
        {
            Adapter->ShadowFb = NULL;
            Adapter->ShadowFbPitch = 0;
            Adapter->ShadowFbSize = 0;
            Adapter->ShadowFbPoolOwned = FALSE;
        }

        DxgkVidMmDestroyAllocation(Adapter, Adapter->SharedPrimaryAllocationHandle);
        Adapter->SharedPrimaryAllocationHandle = NULL;
    }

    if (Adapter->SharedPrimaryResourceHandle != 0)
    {
        if (NT_SUCCESS(DxgkVidMmReferenceResource(Adapter->SharedPrimaryResourceHandle, FALSE, NULL, &Resource)))
        {
            if (Resource->Adapter != Adapter)
            {
                DxgkVidMmDereferenceResource(Resource);
            }
            else
            {
                DxgkVidMmDereferenceResource(Resource);
                DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
            }
            Resource = NULL;
        }
        Adapter->SharedPrimaryResourceHandle = 0;
    }

    Adapter->SharedPrimaryGlobalShareHandle = 0;
    Adapter->SharedPrimaryVidPnSourceId = 0;
    Adapter->SharedPrimaryWidth = 0;
    Adapter->SharedPrimaryHeight = 0;
    Adapter->SharedPrimaryFormat = 0;
    Adapter->SharedPrimaryIsGopBacked = FALSE;

    if (Adapter->SharedShadowAllocationHandle != NULL)
    {
        /*
         * Full-WDDM adapters reuse Adapter->ShadowFb as the live CPU VA for
         * the shadow allocation. Rundown makes clearing it here generation-safe.
         */
        if (Adapter->MiniportContext != NULL && !Adapter->MiniportContext->IsDisplayOnlyDriver && !Adapter->ShadowFbPoolOwned)
        {
            Adapter->ShadowFb = NULL;
            Adapter->ShadowFbPitch = 0;
            Adapter->ShadowFbSize = 0;
            Adapter->ShadowFbPoolOwned = FALSE;
        }

        DxgkVidMmDestroyAllocation(Adapter, Adapter->SharedShadowAllocationHandle);
        Adapter->SharedShadowAllocationHandle = NULL;
    }

    if (Adapter->SharedShadowResourceHandle != 0)
    {
        if (NT_SUCCESS(DxgkVidMmReferenceResource(Adapter->SharedShadowResourceHandle, FALSE, NULL, &Resource)))
        {
            if (Resource->Adapter != Adapter)
            {
                DxgkVidMmDereferenceResource(Resource);
            }
            else
            {
                DxgkVidMmDereferenceResource(Resource);
                DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
            }
            Resource = NULL;
        }
        Adapter->SharedShadowResourceHandle = 0;
    }

    Adapter->SharedShadowGlobalShareHandle = 0;
    Adapter->SharedShadowWidth = 0;
    Adapter->SharedShadowHeight = 0;
    Adapter->SharedShadowPitch = 0;
    Adapter->SharedShadowFormat = 0;
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
}

NTSTATUS
DxgkCreateRedirectionSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _Inout_ PDXGK_REDIRECTION_SURFACE_CREATE Create)
{
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA QueryArgs;
    D3DKMDT_GDISURFACEDATA SurfaceData;
    DWM_DX_SHARED_SURFACE_INFO RuntimeInfo;
    DXGK_ALLOCATIONINFO AllocInfo;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    HANDLE AllocationHandle = NULL;
    HANDLE MiniportResourceHandle = NULL;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PVOID AllocationPrivateData = NULL;
    PVOID ResourcePrivateData = NULL;
    UINT AllocationPrivateDataSize = 0;
    UINT ResourcePrivateDataSize = 0;
    PVOID CpuAddress = NULL;
    ULONGLONG RequiredBytes;
    NTSTATUS Status;

    if (Adapter == NULL || (Device != NULL && Device->Adapter != Adapter) ||
        Create == NULL ||
        Create->StructSize != sizeof(*Create) || Create->Flags != 0 ||
        Create->Width == 0 || Create->Height == 0 ||
        Create->Format != DWM_DX_FORMAT_B8G8R8A8_UNORM ||
        Create->Width > MAXULONG / sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RequiredBytes = (ULONGLONG)Create->Width * sizeof(ULONG) * Create->Height;
    if (RequiredBytes > MAXULONG)
        return STATUS_INTEGER_OVERFLOW;

    Create->Pitch = 0;
    Create->AllocationBytes = 0;
    Create->AllocationHandle = 0;
    Create->ResourceHandle = 0;
    Create->GlobalShare = 0;
    Create->CpuAddress = 0;

    if (Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&SurfaceData, sizeof(SurfaceData));
    SurfaceData.Width = Create->Width;
    SurfaceData.Height = Create->Height;
    SurfaceData.Format = D3DDDIFMT_X8R8G8B8;
    SurfaceData.Type = D3DKMDT_GDISURFACE_TEXTURE;
    SurfaceData.Pitch = Create->Width * sizeof(ULONG);

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.StandardAllocationType = DXGK_STDALLOCATION_GDISURFACE;
    QueryArgs.pCreateGdiSurfaceData = &SurfaceData;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext, &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationPrivateDataSize = QueryArgs.AllocationPrivateDriverDataSize;
    ResourcePrivateDataSize = QueryArgs.ResourcePrivateDriverDataSize;
    if (AllocationPrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE ||
        ResourcePrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AllocationPrivateDataSize != 0)
    {
        AllocationPrivateData = ExAllocatePoolWithTag(
            PagedPool, AllocationPrivateDataSize, TAG_DXGK_DISPLAY);
        if (AllocationPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }
    if (ResourcePrivateDataSize != 0)
    {
        ResourcePrivateData = ExAllocatePoolWithTag(
            PagedPool, ResourcePrivateDataSize, TAG_DXGK_DISPLAY);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    QueryArgs.pAllocationPrivateDriverData = AllocationPrivateData;
    QueryArgs.AllocationPrivateDriverDataSize = AllocationPrivateDataSize;
    QueryArgs.pResourcePrivateDriverData = ResourcePrivateData;
    QueryArgs.ResourcePrivateDriverDataSize = ResourcePrivateDataSize;
    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext, &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (QueryArgs.AllocationPrivateDriverDataSize != AllocationPrivateDataSize ||
        QueryArgs.ResourcePrivateDriverDataSize != ResourcePrivateDataSize ||
        SurfaceData.Pitch < Create->Width * sizeof(ULONG) ||
        SurfaceData.Pitch > MAXULONG / Create->Height)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(&AllocInfo, sizeof(AllocInfo));
    AllocInfo.pPrivateDriverData = AllocationPrivateData;
    AllocInfo.PrivateDriverDataSize = AllocationPrivateDataSize;
    AllocInfo.Size = (SIZE_T)SurfaceData.Pitch * SurfaceData.Height;

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = 1;
    Status = DxgkVidMmCreateAllocation(
        Adapter, Device, &AllocInfo, ResourcePrivateData,
        ResourcePrivateDataSize, NULL, CreateFlags,
        &AllocationHandle, &MiniportResourceHandle);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkVidMmReferenceAllocation(
        AllocationHandle, Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    /*
     * A CDD redirection bitmap is a pageable GDI texture, not a permanently
     * resident CPU-visible primary.  Keep its authoritative contents in the
     * VidMm system backing while GDI and the software compositor access it;
     * a later GPU consumer can make the allocation resident through the
     * normal paging path.  Besides matching the native type-1 allocation,
     * this avoids pinning every window surface in the scan-out segment.
     */
    if (Allocation->Resident)
    {
        Status = DxgkVidMmEvict(Allocation);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    RtlZeroMemory(&RuntimeInfo, sizeof(RuntimeInfo));
    RuntimeInfo.Magic = DWM_DX_SURFACE_INFO_MAGIC;
    RuntimeInfo.Version = DWM_DX_SURFACE_INFO_VERSION;
    RuntimeInfo.Width = Create->Width;
    RuntimeInfo.Height = Create->Height;
    RuntimeInfo.Pitch = SurfaceData.Pitch;
    RuntimeInfo.Format = Create->Format;
    Resource = DxgkVidMmCreateResourceWrapper(
        Adapter, Device, MiniportResourceHandle, 0, TRUE,
        &RuntimeInfo, sizeof(RuntimeInfo),
        ResourcePrivateData, ResourcePrivateDataSize);
    if (Resource == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = DxgkVidMmAttachAllocationToResource(Resource, Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Allocation->MiniportResourceHandle = NULL;
    Allocation->DestroyMiniportResource = FALSE;

    /* CDD paints its kernel-owned redirection bitmap through CpuAddress.  A
     * user DWM/ICD resource is instead a GPU texture opened by global share;
     * production KMDs legitimately describe that texture as non-CPU-visible. */
    if (Device == NULL)
    {
        Status = DxgkVidMmMapAllocationCpu(Allocation, &CpuAddress);
        if (!NT_SUCCESS(Status) || CpuAddress == NULL)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }
    }

    Create->Pitch = SurfaceData.Pitch;
    Create->AllocationBytes = AllocInfo.Size;
    Create->AllocationHandle = (ULONGLONG)(ULONG_PTR)AllocationHandle;
    Create->ResourceHandle = Resource->Handle;
    Create->GlobalShare = Resource->GlobalShareHandle;
    Create->CpuAddress = (ULONGLONG)(ULONG_PTR)CpuAddress;
    Status = STATUS_SUCCESS;

Cleanup:
    if (Allocation != NULL)
        DxgkVidMmDereferenceAllocation(Allocation);
    if (!NT_SUCCESS(Status))
    {
        if (Resource != NULL)
        {
            (VOID)DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
            Resource = NULL;
            AllocationHandle = NULL;
        }
        else if (AllocationHandle != NULL)
        {
            (VOID)DxgkVidMmDestroyAllocation(Adapter, AllocationHandle);
        }
    }
    if (AllocationPrivateData != NULL)
        ExFreePoolWithTag(AllocationPrivateData, TAG_DXGK_DISPLAY);
    if (ResourcePrivateData != NULL)
        ExFreePoolWithTag(ResourcePrivateData, TAG_DXGK_DISPLAY);
    return Status;
}

NTSTATUS
DxgkDestroyRedirectionSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ CONST DXGK_REDIRECTION_SURFACE_DESTROY *Destroy)
{
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    NTSTATUS Status;

    if (Adapter == NULL || Destroy == NULL ||
        Destroy->StructSize != sizeof(*Destroy) || Destroy->Flags != 0 ||
        Destroy->AllocationHandle == 0 || Destroy->ResourceHandle == 0 ||
        Destroy->GlobalShare == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkVidMmReferenceResource(
        Destroy->ResourceHandle, FALSE, NULL, &Resource);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;
    Status = DxgkVidMmReferenceAllocation(
        (HANDLE)(ULONG_PTR)Destroy->AllocationHandle,
        Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
    {
        DxgkVidMmDereferenceResource(Resource);
        return STATUS_INVALID_PARAMETER;
    }
    if (Resource->Adapter != Adapter ||
        Resource->GlobalShareHandle != Destroy->GlobalShare ||
        Resource->AllocationCount != 1 || Allocation->Resource != Resource)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                    Executive, KernelMode, FALSE, NULL);
        InterlockedExchangePointer(
            (PVOID volatile *)&Allocation->RedirectionSurfaceHandle,
            NULL);
        RtlZeroMemory(Allocation->RedirectionSubmittedFenceId,
                      sizeof(Allocation->RedirectionSubmittedFenceId));
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        Status = STATUS_SUCCESS;
    }
    DxgkVidMmDereferenceAllocation(Allocation);
    if (NT_SUCCESS(Status))
        Status = DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
    DxgkVidMmDereferenceResource(Resource);
    return Status;
}

static NTSTATUS
DxgkpEnsureSharedShadowSurfaceLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA QueryArgs;
    D3DKMDT_SHADOWSURFACEDATA SurfaceData;
    DXGK_ALLOCATIONINFO AllocInfo;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    HANDLE AllocationHandle = NULL;
    HANDLE MiniportResourceHandle = NULL;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PVOID AllocationPrivateData = NULL;
    PVOID ResourcePrivateData = NULL;
    UINT AllocationPrivateDataSize = 0;
    UINT ResourcePrivateDataSize = 0;
    PVOID ShadowVa = NULL;
    BOOLEAN StartTimer = FALSE;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Adapter->SharedShadowAllocationHandle != NULL &&
        Adapter->SharedShadowWidth == Adapter->CommittedWidth &&
        Adapter->SharedShadowHeight == Adapter->CommittedHeight)
    {
        return STATUS_SUCCESS;
    }

    if (DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData) == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&SurfaceData, sizeof(SurfaceData));
    SurfaceData.Width = Adapter->CommittedWidth;
    SurfaceData.Height = Adapter->CommittedHeight;
    SurfaceData.Format = D3DDDIFMT_X8R8G8B8;
    SurfaceData.Pitch = Adapter->CommittedWidth * 4;

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.StandardAllocationType = DXGK_STDALLOCATION_SHADOWSURFACE;
    QueryArgs.pCreateShadowSurfaceData = &SurfaceData;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationPrivateDataSize = QueryArgs.AllocationPrivateDriverDataSize;
    ResourcePrivateDataSize = QueryArgs.ResourcePrivateDriverDataSize;
    if (AllocationPrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE ||
        ResourcePrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AllocationPrivateDataSize != 0)
    {
        AllocationPrivateData = ExAllocatePoolWithTag(PagedPool,
                                                      AllocationPrivateDataSize,
                                                      TAG_DXGK_DISPLAY);
        if (AllocationPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    if (ResourcePrivateDataSize != 0)
    {
        ResourcePrivateData = ExAllocatePoolWithTag(PagedPool,
                                                    ResourcePrivateDataSize,
                                                    TAG_DXGK_DISPLAY);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    QueryArgs.pAllocationPrivateDriverData = AllocationPrivateData;
    QueryArgs.AllocationPrivateDriverDataSize = AllocationPrivateDataSize;
    QueryArgs.pResourcePrivateDriverData = ResourcePrivateData;
    QueryArgs.ResourcePrivateDriverDataSize = ResourcePrivateDataSize;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (QueryArgs.AllocationPrivateDriverDataSize != AllocationPrivateDataSize ||
        QueryArgs.ResourcePrivateDriverDataSize != ResourcePrivateDataSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(&AllocInfo, sizeof(AllocInfo));
    AllocInfo.pPrivateDriverData = AllocationPrivateData;
    AllocInfo.PrivateDriverDataSize = AllocationPrivateDataSize;
    AllocInfo.Size = max((SIZE_T)SurfaceData.Pitch * SurfaceData.Height,
                         (SIZE_T)SurfaceData.Width * SurfaceData.Height * 4);

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = 1;

    DXGKRNL_TRACE("DxgkpEnsureSharedShadowSurface: calling CreateAllocation "
                  "Pitch=%u PrivDataSize=%u ResPrivDataSize=%u\n",
                  SurfaceData.Pitch,
                  AllocationPrivateDataSize,
                  ResourcePrivateDataSize);

    Status = DxgkVidMmCreateAllocation(Adapter, NULL, &AllocInfo, ResourcePrivateData, ResourcePrivateDataSize, NULL, CreateFlags, &AllocationHandle, &MiniportResourceHandle);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkVidMmReferenceAllocation(AllocationHandle, Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /*
     * CDD writes the shadow through its CPU mapping, then uses it as the GPU
     * blit source. Keep its placement resident: an aperture mapping exposes
     * the same cached backing pages to both CPU and GPU.
     */
    if (!Allocation->Resident)
    {
        Status = DxgkVidMmMakeResident(Allocation, Adapter);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Resource = DxgkVidMmCreateResourceWrapper(Adapter, NULL, MiniportResourceHandle, 0, TRUE, NULL, 0, ResourcePrivateData, ResourcePrivateDataSize);
    if (Resource == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = DxgkVidMmAttachAllocationToResource(Resource, Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (!Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        Status = DxgkVidMmMapAllocationCpu(Allocation, &ShadowVa);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (ShadowVa == NULL)
        {
            Status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }
    }

    DxgkpBeginSharedSurfaceMutationLocked(Adapter);
    Adapter->SharedShadowResourceHandle = Resource->Handle;
    Adapter->SharedShadowGlobalShareHandle = Resource->GlobalShareHandle;
    Adapter->SharedShadowAllocationHandle = AllocationHandle;
    Adapter->SharedShadowWidth = SurfaceData.Width;
    Adapter->SharedShadowHeight = SurfaceData.Height;
    Adapter->SharedShadowPitch = SurfaceData.Pitch;
    Adapter->SharedShadowFormat = SurfaceData.Format;

    /*
     * For full WDDM adapters using the DOD present path:
     * Set ShadowFb to the shadow allocation's CPU VA so that the
     * present timer can copy CDD's draws to the display via
     * DxgkDdiPresentDisplayOnly.
     */
    if (ShadowVa != NULL)
    {
        Adapter->ShadowFb = ShadowVa;
        Adapter->ShadowFbPitch = SurfaceData.Pitch;
        Adapter->ShadowFbSize = AllocInfo.Size;
        Adapter->ShadowFbPoolOwned = FALSE;
        StartTimer = Adapter->VidPnCommitted;
        DXGKRNL_TRACE("DxgkpEnsureSharedShadowSurface: ShadowFb=%p pitch=%u (%Iu bytes) for DOD present path\n", ShadowVa, SurfaceData.Pitch, AllocInfo.Size);
    }
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
    if (StartTimer)
        DxgkpStartPresentTimer(Adapter);

    DXGKRNL_TRACE("DxgkpEnsureSharedShadowSurface: created %ux%u pitch=%u "
                  "ResHandle=0x%X GlobalShare=0x%X AllocHandle=%p\n",
                  SurfaceData.Width,
                  SurfaceData.Height,
                  SurfaceData.Pitch,
                  Resource->Handle,
                  Resource->GlobalShareHandle,
                  AllocationHandle);

    Status = STATUS_SUCCESS;

Cleanup:
    if (Allocation != NULL)
    {
        DxgkVidMmDereferenceAllocation(Allocation);
        Allocation = NULL;
    }

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedShadowSurface: FAILED status=0x%08lX\n",
                     Status);
        if (AllocationHandle != NULL)
            DxgkVidMmDestroyAllocation(Adapter, AllocationHandle);
        if (Resource != NULL)
            DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
    }

    if (AllocationPrivateData != NULL)
        ExFreePoolWithTag(AllocationPrivateData, TAG_DXGK_DISPLAY);
    if (ResourcePrivateData != NULL)
        ExFreePoolWithTag(ResourcePrivateData, TAG_DXGK_DISPLAY);

    UNREFERENCED_PARAMETER(VidPnSourceId);
    return Status;
}

static NTSTATUS
DxgkpEnsureSharedPrimaryLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA QueryArgs;
    D3DKMDT_SHAREDPRIMARYSURFACEDATA SurfaceData;
    DXGK_ALLOCATIONINFO AllocInfo;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    HANDLE AllocationHandle = NULL;
    HANDLE MiniportResourceHandle = NULL;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PVOID AllocationPrivateData = NULL;
    PVOID ResourcePrivateData = NULL;
    UINT AllocationPrivateDataSize = 0;
    UINT ResourcePrivateDataSize = 0;
    PVOID CpuVa = NULL;
    BOOLEAN StartTimer = FALSE;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * An invalid source is a pure query failure.  Reject it before touching
     * the adapter-owned primary/shadow pair: CDD may still have the current
     * source mapped as its GDI surface.
     */
    if (VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
        return STATUS_INVALID_PARAMETER;

    DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: Adapter=%p Committed=%ux%u "
                  "ExistingAlloc=%p VidPnSourceId=%u\n",
                  Adapter,
                  Adapter->CommittedWidth, Adapter->CommittedHeight,
                  Adapter->SharedPrimaryAllocationHandle,
                  VidPnSourceId);

    if (Adapter->CommittedWidth == 0 || Adapter->CommittedHeight == 0)
    {
        Status = DxgkpDisplayCommitVidPnWhileSharedPrimaryLocked(Adapter);
        if (!NT_SUCCESS(Status))
            return Status;
        /*
         * A commit over an empty topology succeeds without establishing a
         * mode, so success here does not mean there is a geometry to build a
         * primary from.  Asking the miniport for a 0x0 standard allocation
         * makes it answer with whatever it likes -- this Intel part returns
         * STATUS_UNSUCCESSFUL -- and that status is what win32k reports
         * instead of the reason there is no mode.  Say it plainly.
         */
        if (Adapter->CommittedWidth == 0 || Adapter->CommittedHeight == 0)
        {
            DXGKRNL_WARN("DxgkpEnsureSharedPrimary: no VidPN has been committed, "
                         "so the adapter has no mode to build a primary from\n");
            return STATUS_GRAPHICS_INVALID_VIDPN_TOPOLOGY;
        }
    }

    if (Adapter->SharedPrimaryAllocationHandle != NULL &&
        Adapter->SharedPrimaryWidth == Adapter->CommittedWidth &&
        Adapter->SharedPrimaryHeight == Adapter->CommittedHeight &&
        Adapter->SharedPrimaryVidPnSourceId == VidPnSourceId)
    {
        return STATUS_SUCCESS;
    }

    DxgkpDestroySharedPrimaryLocked(Adapter);

    if (DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData) == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&SurfaceData, sizeof(SurfaceData));
    SurfaceData.Width = Adapter->CommittedWidth;
    SurfaceData.Height = Adapter->CommittedHeight;
    SurfaceData.Format = D3DDDIFMT_X8R8G8B8;
    SurfaceData.RefreshRate.Numerator = 60;
    SurfaceData.RefreshRate.Denominator = 1;
    SurfaceData.VidPnSourceId = VidPnSourceId;

    RtlZeroMemory(&QueryArgs, sizeof(QueryArgs));
    QueryArgs.StandardAllocationType = DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE;
    QueryArgs.pCreateSharedPrimarySurfaceData = &SurfaceData;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationPrivateDataSize = QueryArgs.AllocationPrivateDriverDataSize;
    ResourcePrivateDataSize = QueryArgs.ResourcePrivateDriverDataSize;
    if (AllocationPrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE ||
        ResourcePrivateDataSize > DXGKP_STDALLOC_MAX_PRIVATE_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (AllocationPrivateDataSize != 0)
    {
        AllocationPrivateData = ExAllocatePoolWithTag(PagedPool,
                                                      AllocationPrivateDataSize,
                                                      TAG_DXGK_DISPLAY);
        if (AllocationPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    if (ResourcePrivateDataSize != 0)
    {
        ResourcePrivateData = ExAllocatePoolWithTag(PagedPool,
                                                    ResourcePrivateDataSize,
                                                    TAG_DXGK_DISPLAY);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
    }

    QueryArgs.pAllocationPrivateDriverData = AllocationPrivateData;
    QueryArgs.AllocationPrivateDriverDataSize = AllocationPrivateDataSize;
    QueryArgs.pResourcePrivateDriverData = ResourcePrivateData;
    QueryArgs.ResourcePrivateDriverDataSize = ResourcePrivateDataSize;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetStandardAllocationDriverData)(
        Adapter->MiniportDeviceContext,
        &QueryArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (QueryArgs.AllocationPrivateDriverDataSize != AllocationPrivateDataSize ||
        QueryArgs.ResourcePrivateDriverDataSize != ResourcePrivateDataSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    RtlZeroMemory(&AllocInfo, sizeof(AllocInfo));
    AllocInfo.pPrivateDriverData = AllocationPrivateData;
    AllocInfo.PrivateDriverDataSize = AllocationPrivateDataSize;
    /* Set the allocation size from the surface dimensions.
     * The shared primary is always 32bpp XRGB. */
    AllocInfo.Size = (SIZE_T)SurfaceData.Width * SurfaceData.Height * 4;

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = 1;

    DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: calling CreateAllocation "
                  "PrivDataSize=%u ResPrivDataSize=%u\n",
                  AllocationPrivateDataSize, ResourcePrivateDataSize);

    Status = DxgkVidMmCreateAllocation(Adapter, NULL, &AllocInfo, ResourcePrivateData, ResourcePrivateDataSize, NULL, CreateFlags, &AllocationHandle, &MiniportResourceHandle);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedPrimary: CreateAllocation failed 0x%08lX\n",
                     Status);
        goto Cleanup;
    }

    Status = DxgkVidMmReferenceAllocation(AllocationHandle, Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /*
     * The shared primary is the retirement destination and may also be
     * CPU-mapped by the display path. Pin it before any mapping is published;
     * allocation destruction drains this lifetime reference.
     */
    if (!Allocation->Resident)
    {
        Status = DxgkVidMmMakeResident(Allocation, Adapter);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    Status = DxgkVidMmAcquireDeviceResidencyReference(Allocation, NULL);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Resource = DxgkVidMmCreateResourceWrapper(Adapter, NULL, MiniportResourceHandle, 0, TRUE, NULL, 0, ResourcePrivateData, ResourcePrivateDataSize);
    if (Resource == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = DxgkVidMmAttachAllocationToResource(Resource, Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        Status = DxgkVidMmMapAllocationCpu(Allocation, &CpuVa);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (CpuVa == NULL)
        {
            Status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }
    }

    /*
     * Attach system memory backing to the GPU resource via aperture mapping.
     * This triggers BuildPagingBuffer(MAP_APERTURE_SEGMENT) → miniport's
     * MapApertureSegment → AttachBacking → GPU can read the pages.
     */
    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedPrimary: aperture map failed 0x%08lX\n",
                     Status);
        goto Cleanup;
    }

    DxgkpBeginSharedSurfaceMutationLocked(Adapter);
    Adapter->SharedPrimaryResourceHandle = Resource->Handle;
    Adapter->SharedPrimaryGlobalShareHandle = Resource->GlobalShareHandle;
    Adapter->SharedPrimaryAllocationHandle = AllocationHandle;
    Adapter->SharedPrimaryVidPnSourceId = VidPnSourceId;
    Adapter->SharedPrimaryWidth = SurfaceData.Width;
    Adapter->SharedPrimaryHeight = SurfaceData.Height;
    Adapter->SharedPrimaryFormat = SurfaceData.Format;
    Adapter->SharedPrimaryIsGopBacked = FALSE;
    if (CpuVa != NULL)
    {
        Adapter->ShadowFb = CpuVa;
        Adapter->ShadowFbPitch = SurfaceData.Width * 4;
        Adapter->ShadowFbSize = AllocInfo.Size;
        Adapter->ShadowFbPoolOwned = FALSE;
        StartTimer = Adapter->VidPnCommitted;
        DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: ShadowFb set to %p pitch=%u (%Iu bytes)\n", CpuVa, SurfaceData.Width * 4, AllocInfo.Size);
    }
    DxgkpEndSharedSurfaceMutationLocked(Adapter);
    if (StartTimer)
        DxgkpStartPresentTimer(Adapter);

    /*
     * Shared-primary creation only prepares the backing allocation.
     * Full-WDDM scanout programming still happens in DxgkSetDisplayMode
     * and via the present/retire refresh path.
     */

    DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: created %ux%u "
                  "ResHandle=0x%X GlobalShare=0x%X AllocHandle=%p\n",
                  SurfaceData.Width, SurfaceData.Height,
                  Resource->Handle, Resource->GlobalShareHandle,
                  AllocationHandle);

    Status = STATUS_SUCCESS;

Cleanup:
    if (Allocation != NULL)
    {
        DxgkVidMmDereferenceAllocation(Allocation);
        Allocation = NULL;
    }

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpEnsureSharedPrimary: FAILED status=0x%08lX\n", Status);
        if (Adapter->SharedPrimaryAllocationHandle != NULL || Adapter->SharedPrimaryResourceHandle != 0)
        {
            DxgkpDestroySharedPrimaryLocked(Adapter);
        }
        else
        {
            if (AllocationHandle != NULL)
                DxgkVidMmDestroyAllocation(Adapter, AllocationHandle);
            if (Resource != NULL)
                DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
        }
    }

    if (AllocationPrivateData != NULL)
        ExFreePoolWithTag(AllocationPrivateData, TAG_DXGK_DISPLAY);
    if (ResourcePrivateData != NULL)
        ExFreePoolWithTag(ResourcePrivateData, TAG_DXGK_DISPLAY);

    return Status;
}

NTSTATUS
DxgkpEnsureSharedDisplaySurfaces(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        return STATUS_NOT_SUPPORTED;
    }

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, VidPnSourceId);
    if (NT_SUCCESS(Status))
    {
        Status = DxgkpEnsureSharedShadowSurfaceLocked(Adapter,
                                                      VidPnSourceId);
    }
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    return Status;
}

VOID
DxgkDestroySharedPrimary(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    DxgkpDestroySharedPrimaryLocked(Adapter);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
}

NTSTATUS
NTAPI
DxgkSetDisplayMode(
    _In_ D3DKMT_SETDISPLAYMODE *pSetDisplayMode)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKP_VIDPN VidPn;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKARG_SETVIDPNSOURCEADDRESS SetSourceAddress = NULL;
    LARGE_INTEGER PrimaryAddress;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pSetDisplayMode == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Look up the device to get the adapter. */
    Device = DxgkLookupDeviceByHandle(pSetDisplayMode->hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
    {
        DXGKRNL_WARN("DxgkSetDisplayMode: invalid device handle 0x%X\n",
                     pSetDisplayMode->hDevice);
        return STATUS_INVALID_PARAMETER;
    }

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
    {
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        DXGKRNL_WARN("DxgkSetDisplayMode: no VidPN on adapter\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, 0);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)pSetDisplayMode->hPrimaryAllocation, Adapter, Device, &Allocation);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    PrimaryAddress = DxgkVidMmGetAllocationPrimaryAddress(Allocation);

    SetSourceAddress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*SetSourceAddress), TAG_DXGK_VIDPN);
    if (SetSourceAddress == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    RtlZeroMemory(SetSourceAddress, sizeof(*SetSourceAddress));
    SetSourceAddress->VidPnSourceId = 0;
    SetSourceAddress->hAllocation = Allocation->MiniportHandle;
    SetSourceAddress->PrimaryAddress = PrimaryAddress;
    SetSourceAddress->PrimarySegment = Allocation->SegmentId;
    SetSourceAddress->Flags.ModeChange = 1;

    DXGKRNL_TRACE("DxgkSetDisplayMode: device=0x%X alloc=0x%X seg=%u addr=0x%I64x\n",
                  pSetDisplayMode->hDevice,
                  pSetDisplayMode->hPrimaryAllocation,
                  Allocation->SegmentId,
                  PrimaryAddress.QuadPart);

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress)(Adapter->MiniportDeviceContext, SetSourceAddress);
    DxgkReleaseKmdCall(Adapter);
    ExFreePoolWithTag(SetSourceAddress, TAG_DXGK_VIDPN);
    SetSourceAddress = NULL;
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL && DxgkAcquireKmdCall(Adapter))
    {
        DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
        Visibility.VidPnSourceId = 0;
        Visibility.Visible = TRUE;
        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(Adapter->MiniportDeviceContext, &Visibility);
        DxgkReleaseKmdCall(Adapter);
    }

Cleanup:
    if (SetSourceAddress != NULL)
        ExFreePoolWithTag(SetSourceAddress, TAG_DXGK_VIDPN);
    if (Allocation != NULL)
        DxgkVidMmDereferenceAllocation(Allocation);
    if (Device != NULL)
        DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkGetSharedPrimaryHandle(
    _Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE *pGetSharedPrimaryHandle)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (pGetSharedPrimaryHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    DXGKRNL_TRACE("DxgkGetSharedPrimaryHandle: hAdapter=0x%X VidPnSourceId=%u\n",
                  pGetSharedPrimaryHandle->hAdapter,
                  pGetSharedPrimaryHandle->VidPnSourceId);

    Adapter = DxgkLookupAdapterByHandle(pGetSharedPrimaryHandle->hAdapter);
    if (Adapter == NULL)
    {
        DXGKRNL_WARN("DxgkGetSharedPrimaryHandle: invalid adapter handle 0x%X\n",
                     pGetSharedPrimaryHandle->hAdapter);
        return STATUS_INVALID_PARAMETER;
    }

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, pGetSharedPrimaryHandle->VidPnSourceId);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
        goto Cleanup;
    }

    pGetSharedPrimaryHandle->hSharedPrimary = Adapter->SharedPrimaryGlobalShareHandle;

    DXGKRNL_TRACE("DxgkGetSharedPrimaryHandle: returning h=0x%X "
                  "(AllocHandle=%p ResHandle=0x%X Width=%u Height=%u)\n",
                  Adapter->SharedPrimaryGlobalShareHandle,
                  Adapter->SharedPrimaryAllocationHandle,
                  Adapter->SharedPrimaryResourceHandle,
                  Adapter->SharedPrimaryWidth,
                  Adapter->SharedPrimaryHeight);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);

Cleanup:
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
NTAPI
DxgkGetShadowSurface(
    _Inout_ DXGKMT_GETSHADOWSURFACE *pGetShadowSurface)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (pGetShadowSurface == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pGetShadowSurface->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpEnsureSharedPrimaryLocked(Adapter, pGetShadowSurface->VidPnSourceId);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
        goto Cleanup;
    }

    Status = DxgkpEnsureSharedShadowSurfaceLocked(Adapter, pGetShadowSurface->VidPnSourceId);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
        goto Cleanup;
    }

    pGetShadowSurface->hShadowSurface = Adapter->SharedShadowGlobalShareHandle;
    pGetShadowSurface->Width = Adapter->SharedShadowWidth;
    pGetShadowSurface->Height = Adapter->SharedShadowHeight;
    pGetShadowSurface->Pitch = Adapter->SharedShadowPitch;
    pGetShadowSurface->Format = Adapter->SharedShadowFormat;

    DXGKRNL_TRACE("DxgkGetShadowSurface: returning h=0x%X "
                  "(AllocHandle=%p ResHandle=0x%X Width=%u Height=%u Pitch=%u)\n",
                  Adapter->SharedShadowGlobalShareHandle,
                  Adapter->SharedShadowAllocationHandle,
                  Adapter->SharedShadowResourceHandle,
                  Adapter->SharedShadowWidth,
                  Adapter->SharedShadowHeight,
                  Adapter->SharedShadowPitch);
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);

Cleanup:
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
NTAPI
DxgkQueryResourceInfo(
    _Inout_ D3DKMT_QUERYRESOURCEINFO *pQueryResourceInfo)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    UINT AllocationCount = 0;
    UINT TotalPrivateDriverDataSize = 0;
    UINT RuntimeCapacity;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pQueryResourceInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    RuntimeCapacity = pQueryResourceInfo->PrivateRuntimeDataSize;

    Device = DxgkLookupDeviceByHandle(pQueryResourceInfo->hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceResource(pQueryResourceInfo->hGlobalShare, TRUE, NULL, &Resource);
    if (!NT_SUCCESS(Status) || Resource->Adapter != Adapter)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }

    Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations, &AllocationCount, &TotalPrivateDriverDataSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    pQueryResourceInfo->PrivateRuntimeDataSize = Resource->PrivateRuntimeDataSize;
    pQueryResourceInfo->TotalPrivateDriverDataSize = TotalPrivateDriverDataSize;
    pQueryResourceInfo->ResourcePrivateDriverDataSize = Resource->ResourcePrivateDriverDataSize;
    pQueryResourceInfo->NumAllocations = AllocationCount;
    if (Resource->PrivateRuntimeDataSize != 0 && pQueryResourceInfo->pPrivateRuntimeData != NULL)
    {
        if (RuntimeCapacity < Resource->PrivateRuntimeDataSize)
            Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            _SEH2_TRY
            {
                RtlCopyMemory(pQueryResourceInfo->pPrivateRuntimeData, Resource->PrivateRuntimeData, Resource->PrivateRuntimeDataSize);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
    }

Cleanup:
    if (Allocations != NULL)
        DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pOpenResource)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    PDXGKVMM_RESOURCE OpenedResource = NULL;
    PHANDLE OpenedAllocationHandles = NULL;
    PVOID RuntimeCopy = NULL;
    PVOID ResourcePrivateCopy = NULL;
    PVOID TotalPrivateCopy = NULL;
    UINT AllocationCount = 0;
    UINT TotalPrivateSize = 0;
    UINT AllocationCapacity;
    UINT RuntimeCapacity;
    UINT ResourcePrivateCapacity;
    UINT TotalPrivateCapacity;
    UINT Offset;
    UINT Index;
    BOOLEAN BufferTooSmall = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pOpenResource == NULL)
        return STATUS_INVALID_PARAMETER;

    AllocationCapacity = pOpenResource->NumAllocations;
    RuntimeCapacity = pOpenResource->PrivateRuntimeDataSize;
    ResourcePrivateCapacity = pOpenResource->ResourcePrivateDriverDataSize;
    TotalPrivateCapacity = pOpenResource->TotalPrivateDriverDataBufferSize;

    Device = DxgkLookupDeviceByHandle(pOpenResource->hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceResource(pOpenResource->hGlobalShare, TRUE, NULL, &Resource);
    if (!NT_SUCCESS(Status) || Resource->Adapter != Adapter)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }

    Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations, &AllocationCount, &TotalPrivateSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    pOpenResource->NumAllocations = AllocationCount;
    pOpenResource->PrivateRuntimeDataSize = Resource->PrivateRuntimeDataSize;
    pOpenResource->ResourcePrivateDriverDataSize = Resource->ResourcePrivateDriverDataSize;
    pOpenResource->TotalPrivateDriverDataBufferSize = TotalPrivateSize;
    if (AllocationCapacity < AllocationCount || pOpenResource->pOpenAllocationInfo == NULL)
        BufferTooSmall = TRUE;
    if (Resource->PrivateRuntimeDataSize != 0 && (pOpenResource->pPrivateRuntimeData == NULL || RuntimeCapacity < Resource->PrivateRuntimeDataSize))
        BufferTooSmall = TRUE;
    if (Resource->ResourcePrivateDriverDataSize != 0 && (pOpenResource->pResourcePrivateDriverData == NULL || ResourcePrivateCapacity < Resource->ResourcePrivateDriverDataSize))
        BufferTooSmall = TRUE;
    if (TotalPrivateSize != 0 && (pOpenResource->pTotalPrivateDriverDataBuffer == NULL || TotalPrivateCapacity < TotalPrivateSize))
        BufferTooSmall = TRUE;
    if (BufferTooSmall)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }

    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*OpenedAllocationHandles))
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Cleanup;
    }
    OpenedAllocationHandles = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*OpenedAllocationHandles), TAG_VIDMM_RESOURCE);
    if (OpenedAllocationHandles == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(OpenedAllocationHandles, (SIZE_T)AllocationCount * sizeof(*OpenedAllocationHandles));
    if (Resource->PrivateRuntimeDataSize != 0)
    {
        RuntimeCopy = ExAllocatePoolWithTag(NonPagedPool, Resource->PrivateRuntimeDataSize, TAG_VIDMM_RESOURCE);
        if (RuntimeCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(RuntimeCopy, Resource->PrivateRuntimeData, Resource->PrivateRuntimeDataSize);
    }
    if (Resource->ResourcePrivateDriverDataSize != 0)
    {
        ResourcePrivateCopy = ExAllocatePoolWithTag(NonPagedPool, Resource->ResourcePrivateDriverDataSize, TAG_VIDMM_RESOURCE);
        if (ResourcePrivateCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(ResourcePrivateCopy, Resource->ResourcePrivateDriverData, Resource->ResourcePrivateDriverDataSize);
    }
    if (TotalPrivateSize != 0)
    {
        TotalPrivateCopy = ExAllocatePoolWithTag(NonPagedPool, TotalPrivateSize, TAG_VIDMM_RESOURCE);
        if (TotalPrivateCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Offset = 0;
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            if (Allocations[Index]->PrivateDriverDataSize != 0)
                RtlCopyMemory((PUCHAR)TotalPrivateCopy + Offset, Allocations[Index]->PrivateDriverData, Allocations[Index]->PrivateDriverDataSize);
            Offset += Allocations[Index]->PrivateDriverDataSize;
        }
        ASSERT(Offset == TotalPrivateSize);
    }

    Status = DxgkVidMmCreateOpenResource(Device, Resource, Allocations, AllocationCount, ResourcePrivateCopy, Resource->ResourcePrivateDriverDataSize, TotalPrivateCopy, TotalPrivateSize, &OpenedResource, OpenedAllocationHandles);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    _SEH2_TRY
    {
        if (Resource->PrivateRuntimeDataSize != 0)
            RtlCopyMemory(pOpenResource->pPrivateRuntimeData, RuntimeCopy, Resource->PrivateRuntimeDataSize);
        if (Resource->ResourcePrivateDriverDataSize != 0)
            RtlCopyMemory(pOpenResource->pResourcePrivateDriverData, ResourcePrivateCopy, Resource->ResourcePrivateDriverDataSize);
        if (TotalPrivateSize != 0)
            RtlCopyMemory(pOpenResource->pTotalPrivateDriverDataBuffer, TotalPrivateCopy, TotalPrivateSize);
        Offset = 0;
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            pOpenResource->pOpenAllocationInfo[Index].hAllocation = (D3DKMT_HANDLE)(ULONG_PTR)OpenedAllocationHandles[Index];
            pOpenResource->pOpenAllocationInfo[Index].pPrivateDriverData = Allocations[Index]->PrivateDriverDataSize != 0 ? (PVOID)((PUCHAR)pOpenResource->pTotalPrivateDriverDataBuffer + Offset) : NULL;
            pOpenResource->pOpenAllocationInfo[Index].PrivateDriverDataSize = Allocations[Index]->PrivateDriverDataSize;
            Offset += Allocations[Index]->PrivateDriverDataSize;
        }
        pOpenResource->hResource = OpenedResource->Handle;
        pOpenResource->NumAllocations = AllocationCount;
        pOpenResource->PrivateRuntimeDataSize = Resource->PrivateRuntimeDataSize;
        pOpenResource->ResourcePrivateDriverDataSize = Resource->ResourcePrivateDriverDataSize;
        pOpenResource->TotalPrivateDriverDataBufferSize = TotalPrivateSize;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

Cleanup:
    if (!NT_SUCCESS(Status) && OpenedResource != NULL)
        DxgkpVidMmDestroyResourceWrapper(Adapter, OpenedResource);
    if (TotalPrivateCopy != NULL)
        ExFreePoolWithTag(TotalPrivateCopy, TAG_VIDMM_RESOURCE);
    if (ResourcePrivateCopy != NULL)
        ExFreePoolWithTag(ResourcePrivateCopy, TAG_VIDMM_RESOURCE);
    if (RuntimeCopy != NULL)
        ExFreePoolWithTag(RuntimeCopy, TAG_VIDMM_RESOURCE);
    if (OpenedAllocationHandles != NULL)
        ExFreePoolWithTag(OpenedAllocationHandles, TAG_VIDMM_RESOURCE);
    if (Allocations != NULL)
        DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}

typedef struct _DXGKP_DEFAULT_DISPLAY_MODE
{
    UINT Width;
    UINT Height;
} DXGKP_DEFAULT_DISPLAY_MODE;

static const DXGKP_DEFAULT_DISPLAY_MODE DxgkpDefaultDisplayModes[] =
{
    { 800, 600 },
    { 1024, 768 },
    { 1280, 720 },
    { 1280, 768 },
    { 1920, 1080 }
};

static VOID
DxgkpInitializeDisplayMode(
    _Out_ D3DKMT_DISPLAYMODE *Mode,
    _In_ UINT Width,
    _In_ UINT Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Width = Width;
    Mode->Height = Height;
    Mode->Format = D3DDDIFMT_X8R8G8B8;
    Mode->IntegerRefreshRate = 60;
    Mode->RefreshRate.Numerator = 60000;
    Mode->RefreshRate.Denominator = 1000;
    Mode->ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    Mode->DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
    Mode->DisplayFixedOutput = 0;
}

NTSTATUS
DxgkVidPnQueryCurrentDisplayMode(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ D3DKMT_CURRENTDISPLAYMODE *CurrentMode)
{
    D3DKMT_DISPLAYMODE Mode;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_SOURCE_MODESET SourceSet;
    PDXGKP_VIDPN_TARGET_MODESET TargetSet = NULL;
    CONST D3DKMDT_VIDPN_SOURCE_MODE *SourceMode = NULL;
    CONST D3DKMDT_VIDPN_TARGET_MODE *TargetMode = NULL;
    CONST D3DKMDT_VIDPN_PRESENT_PATH *Path = NULL;
    SIZE_T Index;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    PAGED_CODE();

    if (Adapter == NULL || CurrentMode == NULL)
        return STATUS_INVALID_PARAMETER;

    SourceId = CurrentMode->VidPnSourceId;
    if (SourceId >= Adapter->NumberOfVideoPresentSources)
        return STATUS_INVALID_PARAMETER;

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (!Adapter->VidPnCommitted ||
        Adapter->CommittedWidth == 0 ||
        Adapter->CommittedHeight == 0 ||
        VidPn == NULL ||
        VidPn->Signature != DXGKP_VIDPN_SIGNATURE ||
        SourceId >= VidPn->NumSources)
    {
        goto Cleanup;
    }

    for (Index = 0; Index < VidPn->NumPaths; ++Index)
    {
        if (VidPn->Paths[Index].VidPnSourceId == SourceId)
        {
            Path = &VidPn->Paths[Index];
            break;
        }
    }
    if (Path == NULL)
        goto Cleanup;

    SourceSet = VidPn->SourceModeSets[SourceId];
    if (SourceSet == NULL || SourceSet->PinnedModeId == (UINT)-1)
        goto Cleanup;
    for (Index = 0; Index < SourceSet->NumModes; ++Index)
    {
        if (SourceSet->Modes[Index].Id == SourceSet->PinnedModeId)
        {
            SourceMode = &SourceSet->Modes[Index];
            break;
        }
    }
    if (SourceMode == NULL || SourceMode->Type != D3DKMDT_RMT_GRAPHICS)
        goto Cleanup;

    DxgkpInitializeDisplayMode(&Mode,
                               SourceMode->Format.Graphics.PrimSurfSize.cx,
                               SourceMode->Format.Graphics.PrimSurfSize.cy);
    if (Mode.Width == 0 || Mode.Height == 0)
    {
        Mode.Width = Adapter->CommittedWidth;
        Mode.Height = Adapter->CommittedHeight;
    }
    if (SourceMode->Format.Graphics.PixelFormat != D3DDDIFMT_UNKNOWN)
        Mode.Format = SourceMode->Format.Graphics.PixelFormat;

    if (DxgkVidPnTargetIndexFromId(VidPn, Path->VidPnTargetId) != MAXULONG)
        TargetSet = (DxgkVidPnTargetIndexFromId(VidPn, Path->VidPnTargetId) != MAXULONG)
                        ? VidPn->TargetModeSets[DxgkVidPnTargetIndexFromId(VidPn, Path->VidPnTargetId)] : NULL;
    if (TargetSet != NULL && TargetSet->PinnedModeId != (UINT)-1)
    {
        for (Index = 0; Index < TargetSet->NumModes; ++Index)
        {
            if (TargetSet->Modes[Index].Id == TargetSet->PinnedModeId)
            {
                TargetMode = &TargetSet->Modes[Index];
                break;
            }
        }
    }
    if (TargetMode != NULL &&
        TargetMode->VideoSignalInfo.VSyncFreq.Numerator != 0 &&
        TargetMode->VideoSignalInfo.VSyncFreq.Denominator != 0)
    {
        Mode.RefreshRate = TargetMode->VideoSignalInfo.VSyncFreq;
        Mode.IntegerRefreshRate = (UINT)(((ULONGLONG)Mode.RefreshRate.Numerator +
                                          (Mode.RefreshRate.Denominator / 2)) /
                                         Mode.RefreshRate.Denominator);
        Mode.ScanLineOrdering = TargetMode->VideoSignalInfo.ScanLineOrdering;
    }

    switch (Path->ContentTransformation.Rotation)
    {
        case D3DKMDT_VPPR_ROTATE90:
            Mode.DisplayOrientation = D3DDDI_ROTATION_90;
            break;
        case D3DKMDT_VPPR_ROTATE180:
            Mode.DisplayOrientation = D3DDDI_ROTATION_180;
            break;
        case D3DKMDT_VPPR_ROTATE270:
            Mode.DisplayOrientation = D3DDDI_ROTATION_270;
            break;
        default:
            Mode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
            break;
    }

    CurrentMode->DisplayMode = Mode;
    Status = STATUS_SUCCESS;

Cleanup:
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
    return Status;
}

static NTSTATUS
DxgkpReturnDefaultDisplayModeList(
    _Inout_ D3DKMT_GETDISPLAYMODELIST *pGetDisplayModeList)
{
    UINT i;

    if (pGetDisplayModeList->pModeList == NULL ||
        pGetDisplayModeList->ModeCount == 0)
    {
        pGetDisplayModeList->ModeCount =
            RTL_NUMBER_OF(DxgkpDefaultDisplayModes);
        return STATUS_SUCCESS;
    }

    if (pGetDisplayModeList->ModeCount <
        RTL_NUMBER_OF(DxgkpDefaultDisplayModes))
    {
        pGetDisplayModeList->ModeCount =
            RTL_NUMBER_OF(DxgkpDefaultDisplayModes);
        return STATUS_BUFFER_TOO_SMALL;
    }

    _SEH2_TRY
    {
        for (i = 0; i < RTL_NUMBER_OF(DxgkpDefaultDisplayModes); i++)
        {
            DxgkpInitializeDisplayMode(&pGetDisplayModeList->pModeList[i],
                                       DxgkpDefaultDisplayModes[i].Width,
                                       DxgkpDefaultDisplayModes[i].Height);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        return _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    pGetDisplayModeList->ModeCount =
        RTL_NUMBER_OF(DxgkpDefaultDisplayModes);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkGetDisplayModeList(
    _Inout_ D3DKMT_GETDISPLAYMODELIST *pGetDisplayModeList)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKP_VIDPN VidPn = NULL;
    PDXGKP_VIDPN_SOURCE_MODESET SrcSet;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT NumModes, i;

    PAGED_CODE();

    if (pGetDisplayModeList == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pGetDisplayModeList->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pGetDisplayModeList->VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    (VOID)KeWaitForSingleObject(&Adapter->VidPnMutex, Executive, KernelMode, FALSE, NULL);
    if (Adapter->CommittedWidth != 0 && Adapter->CommittedHeight != 0)
    {
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        Status = DxgkpReturnDefaultDisplayModeList(pGetDisplayModeList);
        goto Cleanup;
    }
    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (!DxgkVidPnReference((D3DKMDT_HVIDPN)VidPn))
    {
        VidPn = NULL;
        KeReleaseMutex(&Adapter->VidPnMutex, FALSE);
        Status = DxgkpReturnDefaultDisplayModeList(pGetDisplayModeList);
        goto Cleanup;
    }
    KeReleaseMutex(&Adapter->VidPnMutex, FALSE);

    if (pGetDisplayModeList->VidPnSourceId >= VidPn->NumSources)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    SrcSet = VidPn->SourceModeSets[pGetDisplayModeList->VidPnSourceId];
    if (SrcSet == NULL)
    {
        Status = DxgkpReturnDefaultDisplayModeList(pGetDisplayModeList);
        goto Cleanup;
    }

    NumModes = (UINT)SrcSet->NumModes;
    if (NumModes < RTL_NUMBER_OF(DxgkpDefaultDisplayModes))
    {
        Status = DxgkpReturnDefaultDisplayModeList(pGetDisplayModeList);
        goto Cleanup;
    }

    /* Pass 1: caller wants the mode count only. */
    if (pGetDisplayModeList->pModeList == NULL || pGetDisplayModeList->ModeCount == 0)
    {
        pGetDisplayModeList->ModeCount = NumModes;
        goto Cleanup;
    }

    if (pGetDisplayModeList->ModeCount < NumModes)
    {
        pGetDisplayModeList->ModeCount = NumModes;
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }

    _SEH2_TRY
    {
        for (i = 0; i < NumModes; i++)
        {
            D3DKMT_DISPLAYMODE *pOut = &pGetDisplayModeList->pModeList[i];
            const D3DKMDT_VIDPN_SOURCE_MODE *pSrc = &SrcSet->Modes[i];

            DxgkpInitializeDisplayMode(pOut, pSrc->Format.Graphics.PrimSurfSize.cx, pSrc->Format.Graphics.PrimSurfSize.cy);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
        pGetDisplayModeList->ModeCount = NumModes;

Cleanup:
    if (VidPn != NULL)
        DxgkVidPnDestroy((D3DKMDT_HVIDPN)VidPn);
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
NTAPI
DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner,
    _In_ UINT OwnerFlags,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE StateToFree = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKRNL_ADAPTER Adapter = NULL;
    D3DKMT_VIDPNSOURCEOWNER_TYPE *Types = NULL;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID *SourceIds = NULL;
    DXGKP_VIDPN_SOURCE_OWNER StagedOwners[DXGKP_MAX_SOURCES];
    BOOLEAN SeenSources[DXGKP_MAX_SOURCES];
    BOOLEAN HasExclusiveGdi = FALSE;
    BOOLEAN ReleaseAll;
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T TypesSize = 0;
    SIZE_T SourceIdsSize = 0;
    ULONG i;

    PAGED_CODE();

    if (pSetVidPnSourceOwner == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pSetVidPnSourceOwner->VidPnSourceCount > DXGKP_MAX_SOURCE_OWNER_OPERATIONS)
        return STATUS_INVALID_PARAMETER;
    if (pSetVidPnSourceOwner->VidPnSourceCount != 0 && (pSetVidPnSourceOwner->pType == NULL || pSetVidPnSourceOwner->pVidPnSourceId == NULL))
        return STATUS_INVALID_PARAMETER;
    /* A zero count is a release-all request; the arrays are then ignored, not
     * policed.  Windows 11 accepts it with non-NULL pointers still supplied. */
    if ((OwnerFlags & ~DXGKP_SOURCE_OWNER_FLAG_VALID_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if ((OwnerFlags & DXGKP_SOURCE_OWNER_FLAG_ALLOW_OUTPUT_DUPLICATION) != 0)
        return STATUS_NOT_SUPPORTED;
    if ((OwnerFlags & DXGKP_SOURCE_OWNER_FLAG_DISABLE_DWM_VIRTUAL_MODE) != 0)
        return STATUS_NOT_SUPPORTED;
    if ((OwnerFlags & DXGKP_SOURCE_OWNER_FLAG_USE_NT_HANDLES) != 0)
        return STATUS_NOT_SUPPORTED;

    Device = DxgkLookupDeviceByHandle(pSetVidPnSourceOwner->hDevice, &Adapter);
    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter == NULL)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INVALID_HANDLE;
    }
    if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    ReleaseAll = pSetVidPnSourceOwner->VidPnSourceCount == 0;
    if (!ReleaseAll)
    {
        TypesSize = (SIZE_T)pSetVidPnSourceOwner->VidPnSourceCount * sizeof(*Types);
        SourceIdsSize = (SIZE_T)pSetVidPnSourceOwner->VidPnSourceCount * sizeof(*SourceIds);
        Status = DxgkpCaptureUserBuffer(pSetVidPnSourceOwner->pType, TypesSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, (PVOID *)&Types);
        if (NT_SUCCESS(Status))
            Status = DxgkpCaptureUserBuffer(pSetVidPnSourceOwner->pVidPnSourceId, SourceIdsSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, (PVOID *)&SourceIds);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        for (i = 0; i < pSetVidPnSourceOwner->VidPnSourceCount; ++i)
        {
            if (SourceIds[i] >= DXGKP_MAX_SOURCES || SourceIds[i] >= Adapter->NumberOfVideoPresentSources)
            {
                /* Windows 11 rejects an out-of-range source in this array with
                 * the generic status, not the graphics-specific one. */
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            if (Types[i] < D3DKMT_VIDPNSOURCEOWNER_UNOWNED || Types[i] > D3DKMT_VIDPNSOURCEOWNER_EMULATED)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            if (Types[i] == D3DKMT_VIDPNSOURCEOWNER_SHARED && Device->Flags.LegacyMode)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            if (Types[i] == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI)
                HasExclusiveGdi = TRUE;
        }

        if (HasExclusiveGdi)
        {
            if (!Device->Flags.LegacyMode || Adapter->NumberOfVideoPresentSources > DXGKP_MAX_SOURCES || pSetVidPnSourceOwner->VidPnSourceCount != Adapter->NumberOfVideoPresentSources)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            RtlZeroMemory(SeenSources, sizeof(SeenSources));
            for (i = 0; i < pSetVidPnSourceOwner->VidPnSourceCount; ++i)
            {
                if (Types[i] != D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI || SeenSources[SourceIds[i]])
                {
                    Status = STATUS_INVALID_PARAMETER;
                    goto Cleanup;
                }
                SeenSources[SourceIds[i]] = TRUE;
            }
        }
    }

    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Unlock;
    }
    State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
    if (State != NULL)
        RtlCopyMemory(StagedOwners, State->Owners, sizeof(StagedOwners));
    else
        RtlZeroMemory(StagedOwners, sizeof(StagedOwners));

    if (ReleaseAll)
    {
        for (i = 0; i < DXGKP_MAX_SOURCES; ++i)
        {
            if (StagedOwners[i].OwnerDevice == Device)
            {
                StagedOwners[i].OwnerDevice = NULL;
                StagedOwners[i].OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
                StagedOwners[i].OwnerFlags = 0;
            }
        }
    }
    else
    {
        for (i = 0; i < pSetVidPnSourceOwner->VidPnSourceCount; ++i)
        {
            Status = DxgkpApplySourceOwnerOperation(StagedOwners, Device, SourceIds[i], Types[i], OwnerFlags);
            if (!NT_SUCCESS(Status))
                goto Unlock;
        }
    }

    if (DxgkpSourceOwnerStateIsEmpty(StagedOwners))
    {
        if (State != NULL)
        {
            RemoveEntryList(&State->Entry);
            StateToFree = State;
        }
    }
    else
    {
        if (State == NULL)
        {
            State = ExAllocatePoolWithTag(NonPagedPool, sizeof(*State), TAG_DXGK_SOURCE_OWNER);
            if (State == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Unlock;
            }
            RtlZeroMemory(State, sizeof(*State));
            State->Adapter = Adapter;
            InsertTailList(&g_SourceOwnerAdapterList, &State->Entry);
        }
        RtlCopyMemory(State->Owners, StagedOwners, sizeof(State->Owners));
    }

Unlock:
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    if (StateToFree != NULL)
    {
        RtlZeroMemory(StateToFree->Owners, sizeof(StateToFree->Owners));
        ExFreePoolWithTag(StateToFree, TAG_DXGK_SOURCE_OWNER);
    }

Cleanup:
    if (SourceIds != NULL)
        ExFreePoolWithTag(SourceIds, TAG_DXGK_CAPTURE);
    if (Types != NULL)
        ExFreePoolWithTag(Types, TAG_DXGK_CAPTURE);
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkpSetVidPnSourceOwnerWithAccessMode(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    return DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(pSetVidPnSourceOwner, 0, EmbeddedBufferMode);
}

NTSTATUS
NTAPI
DxgkSetVidPnSourceOwner(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner)
{
    return DxgkpSetVidPnSourceOwnerWithAccessMode(pSetVidPnSourceOwner, KernelMode);
}

BOOLEAN
NTAPI
DxgkpIsAnyVidPnSourceExclusivelyOwned(VOID)
{
    PLIST_ENTRY Entry;
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    BOOLEAN Owned = FALSE;
    ULONG i;

    PAGED_CODE();

    DxgkpEnsureSourceOwnerMutex();
    ExAcquireFastMutex(&g_SourceOwnerMutex);
    for (Entry = g_SourceOwnerAdapterList.Flink; Entry != &g_SourceOwnerAdapterList && !Owned; Entry = Entry->Flink)
    {
        State = CONTAINING_RECORD(Entry, DXGKP_SOURCE_OWNER_ADAPTER_STATE, Entry);
        for (i = 0; i < DXGKP_MAX_SOURCES; ++i)
        {
            if (State->Owners[i].OwnerDevice != NULL &&
                (State->Owners[i].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE ||
                 State->Owners[i].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
            {
                Owned = TRUE;
                break;
            }
        }
    }
    ExReleaseFastMutex(&g_SourceOwnerMutex);
    return Owned;
}

NTSTATUS
NTAPI
DxgkCheckVidPnExclusiveOwnership(
    _In_ CONST D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *pCheckVidPnExclusiveOwnership)
{
    PDXGKP_SOURCE_OWNER_ADAPTER_STATE State;
    PDXGKRNL_ADAPTER Adapter = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pCheckVidPnExclusiveOwnership == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pCheckVidPnExclusiveOwnership->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->State != DxgkAdapterStateStarted)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    if (pCheckVidPnExclusiveOwnership->VidPnSourceId >= DXGKP_MAX_SOURCES || pCheckVidPnExclusiveOwnership->VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
        goto Cleanup;
    }

    DxgkpEnsureSourceOwnerMutex();

    ExAcquireFastMutex(&g_SourceOwnerMutex);
    State = DxgkpFindSourceOwnerAdapterLocked(Adapter);
    if (State != NULL && State->Owners[pCheckVidPnExclusiveOwnership->VidPnSourceId].OwnerDevice != NULL && (State->Owners[pCheckVidPnExclusiveOwnership->VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE || State->Owners[pCheckVidPnExclusiveOwnership->VidPnSourceId].OwnerType == D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI))
        Status = STATUS_GRAPHICS_PRESENT_OCCLUDED;
    ExReleaseFastMutex(&g_SourceOwnerMutex);

Cleanup:
    DxgkDereferenceAdapter(Adapter);
    return Status;
}
