/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN topology, mode sets and the flip queue
 *
 * The display half of the GPU path.  A topology that lets two sources drive
 * one target, or a flip queue that scans out a buffer still being written,
 * shows up as corruption rather than as an error return.  No dxgkrnl types.
 */

#ifndef _DXGK_DISPLAY_CORE_H_
#define _DXGK_DISPLAY_CORE_H_

#include <ntddk.h>

/* --- VidPN topology --------------------------------------------------- */

#define DXGK_VIDPN_CORE_MAX_PATHS    16
#define DXGK_VIDPN_CORE_MAX_MODES    32

typedef struct _DXGK_VIDPN_PATH
{
    ULONG SourceId;
    ULONG TargetId;
    BOOLEAN InUse;
} DXGK_VIDPN_PATH, *PDXGK_VIDPN_PATH;

typedef struct _DXGK_VIDPN_TOPOLOGY
{
    DXGK_VIDPN_PATH Paths[DXGK_VIDPN_CORE_MAX_PATHS];
    ULONG PathCount;
} DXGK_VIDPN_TOPOLOGY, *PDXGK_VIDPN_TOPOLOGY;

VOID DxgkVidPnCoreTopologyInitialize(_Out_ PDXGK_VIDPN_TOPOLOGY Topology);
/* A target is driven by at most one source; a source may clone to several. */
NTSTATUS DxgkVidPnCoreAddPath(_Inout_ PDXGK_VIDPN_TOPOLOGY Topology, _In_ ULONG SourceId, _In_ ULONG TargetId);
NTSTATUS DxgkVidPnCoreRemovePath(_Inout_ PDXGK_VIDPN_TOPOLOGY Topology, _In_ ULONG SourceId, _In_ ULONG TargetId);
BOOLEAN DxgkVidPnCoreFindPath(_In_ const DXGK_VIDPN_TOPOLOGY *Topology, _In_ ULONG SourceId, _In_ ULONG TargetId);
ULONG DxgkVidPnCoreCountTargetsForSource(_In_ const DXGK_VIDPN_TOPOLOGY *Topology, _In_ ULONG SourceId);
BOOLEAN DxgkVidPnCoreTargetIsDriven(_In_ const DXGK_VIDPN_TOPOLOGY *Topology, _In_ ULONG TargetId, _Out_ PULONG SourceId);
/* Every path must reference a source and target the adapter actually has. */
NTSTATUS DxgkVidPnCoreValidateTopology(_In_ const DXGK_VIDPN_TOPOLOGY *Topology, _In_ ULONG SourceCount, _In_ ULONG TargetCount);

/* --- mode sets -------------------------------------------------------- */

typedef struct _DXGK_VIDPN_MODE
{
    ULONG Width;
    ULONG Height;
    ULONG RefreshRateNumerator;
    ULONG RefreshRateDenominator;
    ULONG BitsPerPixel;
} DXGK_VIDPN_MODE, *PDXGK_VIDPN_MODE;

typedef struct _DXGK_VIDPN_MODESET
{
    DXGK_VIDPN_MODE Modes[DXGK_VIDPN_CORE_MAX_MODES];
    ULONG ModeCount;
    ULONG PinnedIndex;      /* DXGK_VIDPN_CORE_NO_PIN when nothing is pinned */
} DXGK_VIDPN_MODESET, *PDXGK_VIDPN_MODESET;

#define DXGK_VIDPN_CORE_NO_PIN  0xFFFFFFFFUL

VOID DxgkVidPnCoreModeSetInitialize(_Out_ PDXGK_VIDPN_MODESET ModeSet);
NTSTATUS DxgkVidPnCoreModeValid(_In_ const DXGK_VIDPN_MODE *Mode);
NTSTATUS DxgkVidPnCoreAddMode(_Inout_ PDXGK_VIDPN_MODESET ModeSet, _In_ const DXGK_VIDPN_MODE *Mode);
NTSTATUS DxgkVidPnCorePinMode(_Inout_ PDXGK_VIDPN_MODESET ModeSet, _In_ ULONG ModeIndex);
NTSTATUS DxgkVidPnCoreUnpinMode(_Inout_ PDXGK_VIDPN_MODESET ModeSet);
BOOLEAN DxgkVidPnCoreGetPinnedMode(_In_ const DXGK_VIDPN_MODESET *ModeSet, _Out_ PDXGK_VIDPN_MODE Mode);

/* --- flip queue ------------------------------------------------------- */

#define DXGK_FLIP_CORE_MAX_QUEUED   4

typedef enum _DXGK_FLIP_INTERVAL
{
    DxgkFlipIntervalImmediate = 0,
    DxgkFlipIntervalOne       = 1,
    DxgkFlipIntervalTwo       = 2,
    DxgkFlipIntervalThree     = 3,
    DxgkFlipIntervalFour      = 4,
    DxgkFlipIntervalMax
} DXGK_FLIP_INTERVAL;

typedef struct _DXGK_FLIP_ENTRY
{
    ULONGLONG AllocationCookie;
    DXGK_FLIP_INTERVAL Interval;
    ULONG VSyncsRemaining;
} DXGK_FLIP_ENTRY, *PDXGK_FLIP_ENTRY;

typedef struct _DXGK_FLIP_QUEUE
{
    DXGK_FLIP_ENTRY Entries[DXGK_FLIP_CORE_MAX_QUEUED];
    ULONG Count;
    ULONG HeadIndex;
    ULONGLONG ScannedOutCookie;   /* what the display is showing now */
    ULONG SourceId;
    BOOLEAN OwnsSource;
} DXGK_FLIP_QUEUE, *PDXGK_FLIP_QUEUE;

VOID DxgkFlipCoreInitialize(_Out_ PDXGK_FLIP_QUEUE Queue, _In_ ULONG SourceId);
VOID DxgkFlipCoreSetSourceOwnership(_Inout_ PDXGK_FLIP_QUEUE Queue, _In_ BOOLEAN Owned);
/* A flip is refused unless the caller owns the VidPN source: otherwise one
 * process could scan out over another's display. */
NTSTATUS DxgkFlipCoreQueue(_Inout_ PDXGK_FLIP_QUEUE Queue, _In_ ULONGLONG AllocationCookie, _In_ DXGK_FLIP_INTERVAL Interval);
/* Reports one vsync; returns TRUE and the new scanned-out buffer if a flip
 * became visible on this vsync. */
BOOLEAN DxgkFlipCoreNotifyVSync(_Inout_ PDXGK_FLIP_QUEUE Queue, _Out_ PULONGLONG PresentedCookie);
ULONG DxgkFlipCoreQueuedCount(_In_ const DXGK_FLIP_QUEUE *Queue);

#endif /* _DXGK_DISPLAY_CORE_H_ */
