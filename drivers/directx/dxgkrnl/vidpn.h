/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN (Video Present Network) manager private declarations
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * This header declares the internal data structures used by the VidPN
 * manager (vidpn.c).  These are NOT part of the public WDDM DDI; they are
 * internal to dxgkrnl.
 *
 * The VidPN manager implements the following WDDM callback interfaces:
 *   - DxgkCbQueryVidPnInterface   (DXGK_INTERFACE offset 0x90)
 *   - DxgkCbQueryMonitorInterface (DXGK_INTERFACE offset 0x98)
 *
 * These return pointers to static interface tables that the miniport driver
 * uses during IsSupportedVidPn, EnumVidPnCofuncModality, and CommitVidPn.
 *
 * Multi-monitor support
 * ---------------------
 * The VidPN object supports up to DXGKP_MAX_SOURCES VidPN sources (display
 * outputs) and DXGKP_MAX_TARGETS targets (physical connectors).  Each
 * source has its own source mode set and each target has its own target
 * mode set and monitor source mode set.  The topology maps sources to
 * targets via paths; one source can drive multiple targets (clone mode).
 */

#pragma once

/* ========================================================================
 * Pool tags for VidPN allocations
 * ====================================================================== */
#define TAG_DXGK_VIDPN      'NVgD'   /* DXGVN - VidPN object             */
#define TAG_DXGK_MODESET    'MSgD'   /* DXGMS - mode set                 */
#define TAG_DXGK_MODE       'OMgD'   /* DXGMO - individual mode entry    */
#define TAG_DXGK_PATH       'HPgD'   /* DXGPH - path info                */

/* ========================================================================
 * Limits
 * ====================================================================== */

/* Maximum modes per mode set. */
#define DXGKP_MAX_MODES     64

/* Maximum VidPN sources (display heads). */
#define DXGKP_MAX_SOURCES   16

/* Maximum VidPN targets (physical connectors). */
#define DXGKP_MAX_TARGETS   16

/* Maximum paths in the topology. */
#define DXGKP_MAX_PATHS     16

/* ========================================================================
 * DXGKP_VIDPN_SOURCE_MODESET - Internal source mode set object
 *
 * Represents the set of source modes available for a given VidPN source.
 * Modes are stored in a fixed-size inline array for simplicity.
 * ====================================================================== */
typedef struct _DXGKP_VIDPN_SOURCE_MODESET
{
    /* Number of modes currently in the set. */
    SIZE_T                          NumModes;

    /* Mode entries.  Entries [0..NumModes-1] are valid. */
    D3DKMDT_VIDPN_SOURCE_MODE       Modes[DXGKP_MAX_MODES];

    /* ID of the pinned mode, or (UINT)-1 if no mode is pinned. */
    D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID PinnedModeId;

    /* Back-pointer to the owning VidPN. */
    struct _DXGKP_VIDPN            *Owner;

    /* Source ID this mode set is associated with. */
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  SourceId;

    /* Next available mode ID for CreateNewModeInfo. */
    UINT                            NextModeId;
} DXGKP_VIDPN_SOURCE_MODESET, *PDXGKP_VIDPN_SOURCE_MODESET;

/* ========================================================================
 * DXGKP_VIDPN_TARGET_MODESET - Internal target mode set object
 * ====================================================================== */
typedef struct _DXGKP_VIDPN_TARGET_MODESET
{
    SIZE_T                          NumModes;
    D3DKMDT_VIDPN_TARGET_MODE       Modes[DXGKP_MAX_MODES];
    D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID PinnedModeId;
    struct _DXGKP_VIDPN            *Owner;
    D3DDDI_VIDEO_PRESENT_TARGET_ID  TargetId;
    UINT                            NextModeId;
} DXGKP_VIDPN_TARGET_MODESET, *PDXGKP_VIDPN_TARGET_MODESET;

/* ========================================================================
 * DXGKP_MONITOR_SOURCE_MODESET - Internal monitor source mode set object
 * ====================================================================== */
typedef struct _DXGKP_MONITOR_SOURCE_MODESET
{
    SIZE_T                          NumModes;
    D3DKMDT_MONITOR_SOURCE_MODE     Modes[DXGKP_MAX_MODES];
    UINT                            NextModeId;
    D3DDDI_VIDEO_PRESENT_TARGET_ID  TargetId;
    struct _DXGKP_VIDPN            *Owner;
} DXGKP_MONITOR_SOURCE_MODESET, *PDXGKP_MONITOR_SOURCE_MODESET;

/* ========================================================================
 * DXGKP_CHILD_MONITOR - Per-target child/monitor state for HPD
 * ====================================================================== */
typedef struct _DXGKP_CHILD_MONITOR
{
    /* TRUE if a monitor is currently connected to this target. */
    BOOLEAN                         Connected;

    /* EDID data (128 or 256 bytes) if available, NULL otherwise. */
    PUCHAR                          EdidData;
    ULONG                           EdidSize;
} DXGKP_CHILD_MONITOR, *PDXGKP_CHILD_MONITOR;

/* ========================================================================
 * DXGKP_VIDPN_SOURCE_OWNER - one (adapter, source) ownership tuple
 *
 * SHARED is one yieldable owner, not evidence that Windows permits an
 * arbitrary collection of simultaneous non-GDI owners.  EXCLUSIVEGDI and
 * EMULATED remain distinct because their primary-present rights differ.
 * ====================================================================== */
typedef struct _DXGKP_VIDPN_SOURCE_OWNER
{
    /* Stable device identity, or NULL if unowned; cleanup precedes final free. */
    PDXGKRNL_DEVICE                 OwnerDevice;

    /* Exact D3DKMT_VIDPNSOURCEOWNER_* value; never collapse enum members. */
    D3DKMT_VIDPNSOURCEOWNER_TYPE    OwnerType;

    /* Validated Version1 flags for the current owner. */
    UINT                            OwnerFlags;
} DXGKP_VIDPN_SOURCE_OWNER, *PDXGKP_VIDPN_SOURCE_OWNER;

/*
 * DxgkpDeviceOwnsVidPnSource
 *
 * TRUE if hDevice has SHARED, EXCLUSIVE, or EXCLUSIVEGDI ownership of
 * VidPnSourceId.  EMULATED ownership deliberately returns FALSE because it
 * grants gamma control without real primary ownership.
 */
BOOLEAN
DxgkpDeviceOwnsVidPnSource(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId);

VOID
DxgkVidPnCleanupDeviceOwners(
    _In_ PDXGKRNL_DEVICE Device);

NTSTATUS
DxgkVidPnReleaseProcessOwners(
    _In_ PEPROCESS Process);

NTSTATUS NTAPI DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(_In_ D3DKMT_SETVIDPNSOURCEOWNER *SetVidPnSourceOwner, _In_ UINT OwnerFlags, _In_ KPROCESSOR_MODE EmbeddedBufferMode);

/* ========================================================================
 * DXGKP_VIDPN - Internal VidPN object
 *
 * Represents a complete Video Present Network:
 *   - A topology (set of source -> target paths)
 *   - Per-source mode sets
 *   - Per-target mode sets
 *   - Per-target monitor source mode sets
 *
 * Supports up to DXGKP_MAX_SOURCES sources, DXGKP_MAX_TARGETS targets,
 * and DXGKP_MAX_PATHS paths for multi-monitor configurations.
 *
 * The DXGKP_VIDPN pointer is cast to D3DKMDT_HVIDPN for handle passing.
 * ====================================================================== */
#define DXGKP_VIDPN_SIGNATURE  'NpDV'   /* "VDpN" */

typedef struct _DXGKP_VIDPN
{
    /* Signature for handle validation. */
    ULONG                           Signature;

    /* Reference count for clone tracking.  Zero means owned exclusively. */
    volatile LONG                   RefCount;

    /* Back-pointer to the adapter that owns this VidPN. */
    struct _DXGKRNL_ADAPTER        *Adapter;

    /* Number of sources and targets this VidPN is configured for. */
    ULONG                           NumSources;
    ULONG                           NumTargets;

    /* ---- Topology ---- */

    /* Array of present paths.  Entries [0..NumPaths-1] are valid. */
    D3DKMDT_VIDPN_PRESENT_PATH      Paths[DXGKP_MAX_PATHS];
    SIZE_T                          NumPaths;

    /* ---- Per-source mode sets ---- */
    /* Pointer array: SourceModeSets[i] is heap-allocated for each active source.
     * NULL if source i does not have a mode set yet. */
    PDXGKP_VIDPN_SOURCE_MODESET     SourceModeSets[DXGKP_MAX_SOURCES];

    /* ---- Per-target mode sets ---- */
    PDXGKP_VIDPN_TARGET_MODESET     TargetModeSets[DXGKP_MAX_TARGETS];

    /* ---- Per-target monitor source mode sets ---- */
    PDXGKP_MONITOR_SOURCE_MODESET   MonitorModeSets[DXGKP_MAX_TARGETS];

    /* ---- Scratch storage for newly created objects ---- */

    D3DKMDT_VIDPN_PRESENT_PATH      NewPath;
    BOOLEAN                         NewPathValid;

    D3DKMDT_VIDPN_SOURCE_MODE       NewSourceMode;
    BOOLEAN                         NewSourceModeValid;

    D3DKMDT_VIDPN_TARGET_MODE       NewTargetMode;
    BOOLEAN                         NewTargetModeValid;

    D3DKMDT_MONITOR_SOURCE_MODE     NewMonitorMode;
    BOOLEAN                         NewMonitorModeValid;

    /* ---- Compatibility shim for single-source code paths ---- */
    /* These pointers reference SourceModeSets[0], TargetModeSets[0], and
     * MonitorModeSets[0] respectively.  They exist purely so that code that
     * previously accessed VidPn->SourceModeSet by value can instead use
     * VidPn->SourceModeSet-> with a pointer dereference.  New code should
     * use the arrays directly. */
} DXGKP_VIDPN, *PDXGKP_VIDPN;

/* ========================================================================
 * VidPN manager API (internal to dxgkrnl)
 * ====================================================================== */

/*
 * DxgkVidPnCreateForAdapter
 *
 * Creates a VidPN object populated with default mode sets for the given
 * adapter.  The number of sources and targets is taken from the adapter's
 * NumberOfVideoPresentSources and NumberOfChildren respectively.
 */
NTSTATUS
DxgkVidPnCreateForAdapter(
    _In_  struct _DXGKRNL_ADAPTER  *Adapter,
    _Out_ D3DKMDT_HVIDPN          *phVidPn);

/*
 * DxgkVidPnClone
 *
 * Creates a deep copy of an existing VidPN.  The clone is independent
 * of the original; modifications to one do not affect the other.
 */
NTSTATUS
DxgkVidPnClone(
    _In_  D3DKMDT_HVIDPN  hSourceVidPn,
    _Out_ D3DKMDT_HVIDPN *phClonedVidPn);

/*
 * DxgkVidPnDestroy
 *
 * Frees a VidPN object created by DxgkVidPnCreateForAdapter or
 * DxgkVidPnClone.
 */
VOID
DxgkVidPnDestroy(
    _In_ D3DKMDT_HVIDPN hVidPn);

/*
 * DxgkVidPnRebuildForHotPlug
 *
 * Called when a child device connects or disconnects.  Rebuilds the
 * topology based on current child status and calls the miniport's
 * DxgkDdiRecommendFunctionalVidPn if available.
 */
NTSTATUS
DxgkVidPnRebuildForHotPlug(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

/*
 * DxgkCbQueryVidPnInterface
 *
 * The DXGK_INTERFACE callback at offset 0x90.  Returns a pointer to
 * the static DXGK_VIDPN_INTERFACE table.
 */
NTSTATUS
APIENTRY
DxgkCbQueryVidPnInterface(
    _In_  D3DKMDT_HVIDPN                       hVidPn,
    _In_  DXGK_VIDPN_INTERFACE_VERSION         VidPnInterfaceVersion,
    _Out_ CONST DXGK_VIDPN_INTERFACE**         ppVidPnInterface);

/*
 * DxgkCbQueryMonitorInterface
 *
 * The DXGK_INTERFACE callback at offset 0x98.  Returns a pointer to
 * a static monitor interface.
 */
NTSTATUS
APIENTRY
DxgkCbQueryMonitorInterface(
    _In_  HANDLE                               hAdapter,
    _In_  UINT                                 MonitorInterfaceVersion,
    _Out_ PVOID*                               ppMonitorInterface);
