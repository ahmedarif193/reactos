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
static NTSTATUS APIENTRY MonitorSourceModeSet_GetNumModes(D3DKMDT_HMONITORSOURCEMODESET, CONST SIZE_T*);
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
static NTSTATUS APIENTRY Monitor_AcquireMonitorSourceModeSet(HANDLE, D3DDDI_VIDEO_PRESENT_TARGET_ID, D3DKMDT_HMONITORSOURCEMODESET*, CONST DXGK_MONITORSOURCEMODESET_INTERFACE**);
static NTSTATUS APIENTRY Monitor_ReleaseMonitorSourceModeSet(HANDLE, D3DKMDT_HMONITORSOURCEMODESET);

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

static CONST DXGK_VIDPN_INTERFACE g_VidPnInterface =
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

typedef struct _DXGKP_MONITOR_INTERFACE
{
    DXGK_MONITOR_INTERFACE_VERSION                  Version;

    NTSTATUS (APIENTRY *pfnAcquireMonitorSourceModeSet)(
        _In_  HANDLE                                        hAdapter,
        _In_  D3DDDI_VIDEO_PRESENT_TARGET_ID                VideoPresentTargetId,
        _Out_ D3DKMDT_HMONITORSOURCEMODESET*                phMonitorSourceModeSet,
        _Out_ CONST DXGK_MONITORSOURCEMODESET_INTERFACE**   ppMonitorSourceModeSetInterface);

    NTSTATUS (APIENTRY *pfnReleaseMonitorSourceModeSet)(
        _In_ HANDLE                                         hAdapter,
        _In_ D3DKMDT_HMONITORSOURCEMODESET                  hMonitorSourceModeSet);

    PVOID                                                  pfnGetMonitorFrequencyRangeSet;
    PVOID                                                  pfnGetMonitorDescriptorSet;
} DXGKP_MONITOR_INTERFACE;

static CONST DXGKP_MONITOR_INTERFACE g_MonitorInterface =
{
    DXGK_MONITOR_INTERFACE_VERSION_V1,
    Monitor_AcquireMonitorSourceModeSet,
    Monitor_ReleaseMonitorSourceModeSet,
    NULL,
    NULL,
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
    _In_ D3DKMT_VIDPNSOURCEOWNER_TYPE RequestedType)
{
    PDXGKP_VIDPN_SOURCE_OWNER Current = &Owners[SourceId];

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_UNOWNED)
    {
        if (Current->OwnerDevice == OwnerDevice)
        {
            Current->OwnerDevice = NULL;
            Current->OwnerType = D3DKMT_VIDPNSOURCEOWNER_UNOWNED;
        }
        return STATUS_SUCCESS;
    }

    if (RequestedType == D3DKMT_VIDPNSOURCEOWNER_SHARED)
    {
        if (Current->OwnerDevice == NULL)
        {
            Current->OwnerDevice = OwnerDevice;
            Current->OwnerType = RequestedType;
            return STATUS_SUCCESS;
        }
        if (Current->OwnerDevice == OwnerDevice && Current->OwnerType == RequestedType)
            return STATUS_SUCCESS;
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
            return STATUS_SUCCESS;
        }
        if (Current->OwnerDevice == OwnerDevice && Current->OwnerType == RequestedType)
            return STATUS_SUCCESS;
        if (Current->OwnerDevice == OwnerDevice)
            return STATUS_INVALID_PARAMETER;
        return STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;
    }

    if (Current->OwnerDevice == NULL)
    {
        Current->OwnerDevice = OwnerDevice;
        Current->OwnerType = D3DKMT_VIDPNSOURCEOWNER_EMULATED;
        return STATUS_SUCCESS;
    }
    if (Current->OwnerDevice == OwnerDevice && Current->OwnerType == D3DKMT_VIDPNSOURCEOWNER_EMULATED)
        return STATUS_SUCCESS;
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
        ExFreePoolWithTag(StateToFree, TAG_DXGK_SOURCE_OWNER);
}

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
    Mode->Format.Graphics.ColorBasis           = D3DKMDT_CB_SCRGB;
    Mode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;
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

    Mode->VideoSignalInfo.VideoStandard      = D3DKMDT_VSS_OTHER;
    Mode->VideoSignalInfo.TotalSize.cx       = Width;
    Mode->VideoSignalInfo.TotalSize.cy       = Height;
    Mode->VideoSignalInfo.ActiveSize.cx      = Width;
    Mode->VideoSignalInfo.ActiveSize.cy      = Height;
    Mode->VideoSignalInfo.VSyncFreq.Numerator   = 60;
    Mode->VideoSignalInfo.VSyncFreq.Denominator = 1;
    Mode->VideoSignalInfo.HSyncFreq.Numerator   = (UINT)((ULONGLONG)Height * 60);
    Mode->VideoSignalInfo.HSyncFreq.Denominator = 1;
    Mode->VideoSignalInfo.PixelRate          = (SIZE_T)Width * Height * 60;
    Mode->VideoSignalInfo.ScanLineOrdering   = D3DDDI_VSSLO_PROGRESSIVE;

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

    Mode->VideoSignalInfo.VideoStandard      = D3DKMDT_VSS_OTHER;
    Mode->VideoSignalInfo.TotalSize.cx       = Width;
    Mode->VideoSignalInfo.TotalSize.cy       = Height;
    Mode->VideoSignalInfo.ActiveSize.cx      = Width;
    Mode->VideoSignalInfo.ActiveSize.cy      = Height;
    Mode->VideoSignalInfo.VSyncFreq.Numerator   = 60;
    Mode->VideoSignalInfo.VSyncFreq.Denominator = 1;
    Mode->VideoSignalInfo.HSyncFreq.Numerator   = (UINT)((ULONGLONG)Height * 60);
    Mode->VideoSignalInfo.HSyncFreq.Denominator = 1;
    Mode->VideoSignalInfo.PixelRate          = (SIZE_T)Width * Height * 60;
    Mode->VideoSignalInfo.ScanLineOrdering   = D3DDDI_VSSLO_PROGRESSIVE;

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

    Path->VidPnTargetColorBasis = D3DKMDT_CB_SCRGB;
    Path->VidPnTargetColorCoeffDynamicRanges.FirstChannel  = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.SecondChannel = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.ThirdChannel  = 8;
    Path->VidPnTargetColorCoeffDynamicRanges.FourthChannel = 0;

    Path->Content = D3DKMDT_VPPC_GRAPHICS;
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
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    ModeSet = (PDXGKP_MONITOR_SOURCE_MODESET)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(DXGKP_MONITOR_SOURCE_MODESET), TAG_DXGK_MODESET);
    if (ModeSet == NULL)
        return NULL;

    RtlZeroMemory(ModeSet, sizeof(*ModeSet));
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
    PDXGKP_VIDPN VidPn;
    ULONG NumSources, NumTargets;
    ULONG i, PathCount;

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

    /* Determine source/target counts from adapter. */
    NumSources = (Adapter->NumberOfVideoPresentSources > 0)
                 ? Adapter->NumberOfVideoPresentSources : 1;
    NumTargets = (Adapter->NumberOfChildren > 0)
                 ? Adapter->NumberOfChildren : 1;

    if (NumSources > DXGKP_MAX_SOURCES) NumSources = DXGKP_MAX_SOURCES;
    if (NumTargets > DXGKP_MAX_TARGETS) NumTargets = DXGKP_MAX_TARGETS;

    VidPn->NumSources = NumSources;
    VidPn->NumTargets = NumTargets;

    /* Allocate mode sets for each source and target. */
    for (i = 0; i < NumSources; i++)
    {
        VidPn->SourceModeSets[i] = DxgkpAllocateSourceModeSet(VidPn, i);
        if (VidPn->SourceModeSets[i] == NULL)
            goto Fail;
    }

    for (i = 0; i < NumTargets; i++)
    {
        VidPn->TargetModeSets[i] = DxgkpAllocateTargetModeSet(VidPn, i);
        if (VidPn->TargetModeSets[i] == NULL)
            goto Fail;

        VidPn->MonitorModeSets[i] = DxgkpAllocateMonitorModeSet(i);
        if (VidPn->MonitorModeSets[i] == NULL)
            goto Fail;
    }

    /* Populate default modes for each source/target pair. */
    PathCount = (NumSources < NumTargets) ? NumSources : NumTargets;
    if (PathCount > DXGKP_MAX_PATHS) PathCount = DXGKP_MAX_PATHS;

    for (i = 0; i < PathCount; i++)
    {
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
            Clone->MonitorModeSets[i] = DxgkpAllocateMonitorModeSet(i);
            if (Clone->MonitorModeSets[i] == NULL)
                goto CloneFail;
            RtlCopyMemory(Clone->MonitorModeSets[i], Source->MonitorModeSets[i],
                           sizeof(DXGKP_MONITOR_SOURCE_MODESET));
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

NTSTATUS
DxgkVidPnRebuildForHotPlug(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * For now, the existing VidPN is valid (it was created at adapter start).
     * A full implementation would:
     *   1. Query each child's connection status
     *   2. Read EDID from connected monitors
     *   3. Rebuild mode sets based on EDID data
     *   4. Call DxgkDdiRecommendFunctionalVidPn
     *   5. CommitVidPn with the new topology
     *
     * For the current DOD path, the existing VidPN remains valid.
     */
    DXGKRNL_TRACE("DxgkVidPnRebuildForHotPlug: adapter %p (topology preserved)\n",
                  Adapter);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkCbQueryVidPnInterface
 * ====================================================================== */

NTSTATUS
APIENTRY
DxgkCbQueryVidPnInterface(
    _In_  D3DKMDT_HVIDPN                       hVidPn,
    _In_  DXGK_VIDPN_INTERFACE_VERSION         VidPnInterfaceVersion,
    _Out_ CONST DXGK_VIDPN_INTERFACE**         ppVidPnInterface)
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
        return STATUS_INVALID_PARAMETER;
    }

    if (VidPnInterfaceVersion != DXGK_VIDPN_INTERFACE_VERSION_V1)
    {
        DXGKRNL_WARN("DxgkCbQueryVidPnInterface: unsupported version %d, "
                     "returning V1\n", VidPnInterfaceVersion);
    }

    *ppVidPnInterface = &g_VidPnInterface;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
DxgkCbQueryMonitorInterface(
    _In_  HANDLE                               hAdapter,
    _In_  UINT                                 MonitorInterfaceVersion,
    _Out_ PVOID*                               ppMonitorInterface)
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

    *ppMonitorInterface = (PVOID)&g_MonitorInterface;
    return STATUS_SUCCESS;
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

    if (phVidPnTargetModeSet == NULL || ppVidPnTargetModeSetInterface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidPn = DxgkpVidPnFromHandle(hVidPn);
    if (VidPn == NULL)
        return STATUS_INVALID_PARAMETER;

    if (VidPnTargetId >= VidPn->NumTargets || VidPn->TargetModeSets[VidPnTargetId] == NULL)
    {
        DXGKRNL_WARN("VidPn_AcquireTargetModeSet: invalid target %u (max %lu)\n",
                     VidPnTargetId, VidPn->NumTargets);
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }

    *phVidPnTargetModeSet          = (D3DKMDT_HVIDPNTARGETMODESET)VidPn->TargetModeSets[VidPnTargetId];
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

    if (VidPnTargetId >= VidPn->NumTargets)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    ModeSet = VidPn->TargetModeSets[VidPnTargetId];
    if (ModeSet == NULL)
    {
        ModeSet = DxgkpAllocateTargetModeSet(VidPn, VidPnTargetId);
        if (ModeSet == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        VidPn->TargetModeSets[VidPnTargetId] = ModeSet;
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

    if (VidPnTargetId >= VidPn->NumTargets)
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
        pVidPnPresentPath->VidPnTargetId >= VidPn->NumTargets)
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
    if ((ULONG_PTR)pVidPnTargetModeInfo >= 0xFFFF800000000000ULL)
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
    _Out_ CONST SIZE_T*                              pNumModes)
{
    PDXGKP_MONITOR_SOURCE_MODESET ModeSet;

    if (pNumModes == NULL)
        return STATUS_INVALID_PARAMETER;

    ModeSet = DxgkpMonitorModeSetFromHandle(hMonitorSourceModeSet);
    if (ModeSet == NULL)
        return STATUS_INVALID_PARAMETER;

    *(SIZE_T*)pNumModes = ModeSet->NumModes;
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
    _In_  HANDLE                                       hAdapter,
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

    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;

    if (VideoPresentTargetId >= VidPn->NumTargets ||
        VidPn->MonitorModeSets[VideoPresentTargetId] == NULL)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_TARGET;
    }

    *phMonitorSourceModeSet = (D3DKMDT_HMONITORSOURCEMODESET)VidPn->MonitorModeSets[VideoPresentTargetId];
    *ppMonitorSourceModeSetInterface = &g_MonitorSourceModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY
Monitor_ReleaseMonitorSourceModeSet(
    _In_ HANDLE                                        hAdapter,
    _In_ D3DKMDT_HMONITORSOURCEMODESET                 hMonitorSourceModeSet)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(hMonitorSourceModeSet);
    return STATUS_SUCCESS;
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

    Resource = DxgkVidMmCreateResourceWrapper(Adapter, NULL, MiniportResourceHandle, 0, TRUE, NULL, 0, ResourcePrivateData, ResourcePrivateDataSize);
    if (Resource == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = DxgkVidMmAttachAllocationToResource(Resource, Allocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
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

    DXGKRNL_TRACE("DxgkpEnsureSharedPrimary: Adapter=%p Committed=%ux%u "
                  "ExistingAlloc=%p VidPnSourceId=%u\n",
                  Adapter,
                  Adapter->CommittedWidth, Adapter->CommittedHeight,
                  Adapter->SharedPrimaryAllocationHandle,
                  VidPnSourceId);

    if (Adapter->CommittedWidth == 0 || Adapter->CommittedHeight == 0)
    {
        Status = DxgkDisplayCommitVidPn(Adapter);
        if (!NT_SUCCESS(Status))
            return Status;
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
        return STATUS_INVALID_HANDLE;
    }

    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
    {
        DXGKRNL_WARN("DxgkSetDisplayMode: no VidPN on adapter\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

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
        return STATUS_INVALID_HANDLE;
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
        return STATUS_INVALID_HANDLE;

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
        return STATUS_INVALID_HANDLE;

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
        return STATUS_INVALID_HANDLE;

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
    PDXGKP_VIDPN VidPn;
    PDXGKP_VIDPN_SOURCE_MODESET SrcSet;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT NumModes, i;

    PAGED_CODE();

    if (pGetDisplayModeList == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pGetDisplayModeList->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (pGetDisplayModeList->VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if (Adapter->CommittedWidth != 0 && Adapter->CommittedHeight != 0)
    {
        Status = DxgkpReturnDefaultDisplayModeList(pGetDisplayModeList);
        goto Cleanup;
    }

    VidPn = (PDXGKP_VIDPN)Adapter->VidPn;
    if (VidPn == NULL || VidPn->Signature != DXGKP_VIDPN_SIGNATURE)
    {
        Status = DxgkpReturnDefaultDisplayModeList(pGetDisplayModeList);
        goto Cleanup;
    }

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
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
NTAPI
DxgkpSetVidPnSourceOwnerWithAccessMode(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner,
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
    if (pSetVidPnSourceOwner->VidPnSourceCount == 0 && (pSetVidPnSourceOwner->pType != NULL || pSetVidPnSourceOwner->pVidPnSourceId != NULL))
        return STATUS_INVALID_PARAMETER;

    Device = DxgkLookupDeviceByHandle(pSetVidPnSourceOwner->hDevice, &Adapter);
    if (Device == NULL)
        return STATUS_INVALID_HANDLE;
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
                Status = STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
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
            }
        }
    }
    else
    {
        for (i = 0; i < pSetVidPnSourceOwner->VidPnSourceCount; ++i)
        {
            Status = DxgkpApplySourceOwnerOperation(StagedOwners, Device, SourceIds[i], Types[i]);
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
        ExFreePoolWithTag(StateToFree, TAG_DXGK_SOURCE_OWNER);

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
DxgkSetVidPnSourceOwner(
    _In_ D3DKMT_SETVIDPNSOURCEOWNER *pSetVidPnSourceOwner)
{
    return DxgkpSetVidPnSourceOwnerWithAccessMode(pSetVidPnSourceOwner, KernelMode);
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
        return STATUS_INVALID_HANDLE;
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
