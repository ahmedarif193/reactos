/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Swap chain / present infrastructure — private declarations
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * Overview
 * --------
 * Declares the per-VidPn-source present queue and the internal helper
 * functions that dma.c (DxgkPresent) and display.c (shadow FB timer)
 * call into.
 *
 * Windows WDDM present model
 * --------------------------
 *   1. User calls D3DKMTPresent (arrives in dma.c as DxgkPresent).
 *   2. dxgkrnl queues the present into a per-VidPnSource FIFO.
 *   3. On the next VSync (or immediately for display-only adapters),
 *      the head of the queue is dequeued and executed:
 *        - DOD adapters: DxgkDdiPresentDisplayOnly (shadow FB copy).
 *        - Full WDDM: DxgkDdiPresent (blit/flip DMA packet).
 *   4. A monotonic PresentId is returned to the caller for tracking.
 */

#pragma once

#include "present_contract_core.h"
#include "present_dma_core.h"

/* ========================================================================
 * Constants
 * ====================================================================== */

/*
 * Maximum number of presents that can be queued per VidPn source before
 * new submissions block or fail.  This is intentionally small for the
 * initial implementation; Windows uses dynamic ring buffers.
 */
#define DXGKRNL_PRESENT_QUEUE_DEPTH     16

/* Pool tag for present-related allocations. */
#define TAG_DXGK_PRESENT    'TxgD'   /* DXGT */

/* ========================================================================
 * Present operation types
 * ====================================================================== */
typedef enum _DXGKRNL_PRESENT_TYPE
{
    /*
     * Blit present: CPU or GPU copy from source allocation to destination.
     * The most common path for windowed applications and display-only
     * drivers.  For DOD adapters this triggers DxgkDdiPresentDisplayOnly;
     * for full WDDM adapters this triggers DxgkDdiPresent with Blt flag.
     */
    DxgkPresentTypeBlt = 0,

    /*
     * Flip present: hardware page flip — GPU atomically switches the
     * scanout address to a new buffer.  Requires full WDDM adapter with
     * hardware flip support.  Triggered via DxgkDdiPresent with Flip flag.
     */
    DxgkPresentTypeFlip,

    /*
     * Colour fill: fill destination region with a solid colour.
     * Less common; used by some GDI-accelerated paths.
     */
    DxgkPresentTypeColorFill,

} DXGKRNL_PRESENT_TYPE;

/* One coherent SharedPrimary/SharedShadow generation.  The snapshot owns the
 * allocation references and the shared-surface rundown reference until it is
 * released or transferred to a queued present entry. */
typedef struct _DXGKRNL_SHARED_SURFACE_SNAPSHOT
{
    struct _DXGKRNL_ADAPTER        *Adapter;
    ULONG64                         Generation;
    HANDLE                          PrimaryHandle;
    HANDLE                          ShadowHandle;
    PDXGKVMM_ALLOCATION             PrimaryAllocation;
    PDXGKVMM_ALLOCATION             ShadowAllocation;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    ULONG                           PrimaryWidth;
    ULONG                           PrimaryHeight;
    D3DDDIFORMAT                    PrimaryFormat;
    ULONG                           ShadowWidth;
    ULONG                           ShadowHeight;
    ULONG                           ShadowPitch;
    D3DDDIFORMAT                    ShadowFormat;
    PVOID                           ShadowFb;
    ULONG                           ShadowFbPitch;
    ULONG                           ShadowFbSize;
    BOOLEAN                         ShadowFbPoolOwned;
    ULONG                           CommittedWidth;
    ULONG                           CommittedHeight;
    PVOID                           PostDisplayVirtualAddress;
    SIZE_T                          PostDisplayMappingSize;
    ULONG                           PostDisplayPitch;
    ULONG                           PostDisplayHeight;
    BOOLEAN                         VidPnCommitted;
    BOOLEAN                         RundownHeld;
} DXGKRNL_SHARED_SURFACE_SNAPSHOT, *PDXGKRNL_SHARED_SURFACE_SNAPSHOT;

/* ========================================================================
 * Present operation descriptor
 *
 * One instance per queued present.  Stored in the circular FIFO inside
 * DXGKRNL_PRESENT_QUEUE.  Lifetime: enqueue → dequeue+execute.
 * ====================================================================== */
typedef struct _DXGKRNL_PRESENT_ENTRY
{
    /* Monotonically increasing present identifier (per VidPn source). */
    ULONG64                         PresentId;

    /* Type of present operation (blt / flip / colour fill). */
    DXGKRNL_PRESENT_TYPE            Type;

    /* VidPn source targeted by this present. */
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;

    /* Source and destination allocation handles (D3DKMT_HANDLE). */
    D3DKMT_HANDLE                   hSource;
    D3DKMT_HANDLE                   hDestination;

    /* Owned VidMm references acquired when the present is admitted.  These
     * remain valid through a queued VSync wait and execution; no execution
     * path is allowed to resolve hSource/hDestination again.  Aliased source
     * and destination handles deliberately own two balanced references. */
    PDXGKVMM_ALLOCATION             SourceAllocation;
    PDXGKVMM_ALLOCATION             DestinationAllocation;

    /* An OpenResource alias owns a persistent per-device miniport binding.
     * Keep the logical alias referenced separately from its physical backing. */
    HANDLE                          SourceOpenBindingHandle;
    HANDLE                          DestinationOpenBindingHandle;
    PDXGKVMM_ALLOCATION             SourceOpenBindingReference;
    PDXGKVMM_ALLOCATION             DestinationOpenBindingReference;

    /* Shared-surface identity is captured at admission, never inferred later
     * by comparing a queued handle with mutable adapter fields. */
    DXGKRNL_SHARED_SURFACE_SNAPSHOT SharedSurface;
    BOOLEAN                         SourceIsSharedPrimary;
    BOOLEAN                         SourceIsSharedShadow;
    BOOLEAN                         DestinationIsSharedPrimary;
    BOOLEAN                         DestinationIsSharedShadow;

    /* Source and destination rectangles. */
    RECT                            SrcRect;
    RECT                            DstRect;

    /*
     * Destination-space dirty rectangles owned by this entry.  KMT supplies
     * source-space rectangles; admission maps and snapshots them before a
     * queued present can outlive the caller's buffer.
     */
    RECT                           *DstSubRects;
    UINT                            DstSubRectCount;

    /* Colour value for colour-fill presents (ARGB 32-bit). */
    UINT                            Color;

    /* Flip interval — how many VSyncs to wait before presenting.
     * D3DDDI_FLIPINTERVAL_IMMEDIATE = present immediately (no VSync wait).
     * D3DDDI_FLIPINTERVAL_ONE       = present at the next VSync.
     */
    D3DDDI_FLIPINTERVAL_TYPE        FlipInterval;

    /* Owned references to the submitting context, when the caller supplied
     * one, and its device.  The DMA tracker takes independent references. */
    struct _DXGKRNL_CONTEXT        *Context;
    struct _DXGKRNL_DEVICE         *Device;
    struct _DXGKRNL_DEVICE_WORK    *DeviceWork;
    BOOLEAN                         PresentLimitReservationOwned;

} DXGKRNL_PRESENT_ENTRY, *PDXGKRNL_PRESENT_ENTRY;

NTSTATUS DxgkPresentSetQueuedLimit(_In_ struct _DXGKRNL_DEVICE *Device, _In_ ULONG RequestedLimit);
NTSTATUS DxgkPresentGetQueuedLimit(_In_ struct _DXGKRNL_DEVICE *Device, _Out_ PUINT QueuedPresentLimit);
NTSTATUS DxgkPresentGetPendingFlipLimit(_In_ struct _DXGKRNL_DEVICE *Device, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _Out_ PUINT QueuedPendingFlipLimit);
NTSTATUS DxgkPresentGetQueueLimitState(_In_ struct _DXGKRNL_DEVICE *Device, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _Out_ PBOOLEAN LimitReached);

/* ========================================================================
 * DXGKRNL_PRESENT_QUEUE — Per-VidPn-source present FIFO
 *
 * One instance per VidPn source (display output).  Stored in the
 * PresentQueues array inside DXGKRNL_ADAPTER.  The queue is a fixed-size
 * circular buffer protected by a KSPIN_LOCK so that enqueue and the
 * PASSIVE_LEVEL VSync worker can safely coordinate.
 *
 * VSync tracking:
 *   DxgkpNotifyVSync records each CRTC_VSYNC pulse.  The PASSIVE worker
 *   compares the entry's target VBlank against VBlankCount to decide when to
 *   execute the present.
 * ====================================================================== */
typedef struct _DXGKRNL_PRESENT_QUEUE
{
    /* VidPn source index this queue serves. */
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;

    /* Circular FIFO of present entries. */
    DXGKRNL_PRESENT_ENTRY           Entries[DXGKRNL_PRESENT_QUEUE_DEPTH];

    /*
     * Head = next entry to dequeue (execute).
     * Tail = next slot to enqueue into.
     * Count = number of entries currently queued.
     */
    ULONG                           Head;
    ULONG                           Tail;
    ULONG                           Count;

    /* Spinlock protecting Head/Tail/Count and Entries. */
    KSPIN_LOCK                      QueueLock;

    /* Monotonically increasing present ID counter for this source. */
    volatile LONG64                 NextPresentId;

    /* Frames actually presented on this source (blt and flip), reported as
     * D3DKMT_QUERYSTATISTICS_VIDPNSOURCE.GlobalInformation.Frame. */
    volatile LONG                   PresentedFrameCount;

    /*
     * VSync tracking.
     *   VBlankCount    — total vertical blanks since adapter start.
     *   LastPresentVBlank — VBlankCount at which the last present executed.
     */
    volatile LONG64                 VBlankCount;
    LONG64                          LastPresentVBlank;
    KSPIN_LOCK                      VBlankWaitLock;
    LIST_ENTRY                      VBlankWaiterList;
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
    /*
     * Kernel-owned synchronization event exposed to DWM through a caller-owned
     * handle.  Target fields are protected by VBlankWaitLock.
     */
    PKEVENT                         DwmVBlankEvent;
    ULONG                           DwmVBlankTarget;
    BOOLEAN                         DwmVBlankTargetArmed;
#endif

    /* Back-pointer to the owning adapter. */
    struct _DXGKRNL_ADAPTER        *Adapter;

    /* A DPC records pending pulses and queues a PASSIVE_LEVEL worker.  A
     * queued worker owns one PresentQueueActiveCalls reference until exit. */
    volatile LONG                  VSyncWorkQueued;
    volatile LONG                  PendingVBlanks;

} DXGKRNL_PRESENT_QUEUE, *PDXGKRNL_PRESENT_QUEUE;

/* ========================================================================
 * Function prototypes — present.c
 * ====================================================================== */

/*
 * DxgkPresentInit
 *
 * Initialises the present queue array for an adapter.  Called from
 * DxgkAdapterStart after NumberOfVideoPresentSources is known.
 * Allocates PresentQueues[0..N-1] from NonPagedPool.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkPresentInit(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

/*
 * DxgkPresentTeardown
 *
 * Frees the present queue array.  Called from DxgkAdapterStop.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkPresentTeardown(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

BOOLEAN
DxgkPresentTryBeginStop(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
DxgkPresentBeginStop(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
DxgkPresentResume(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
DxgkPresentBeginReset(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
DxgkPresentCompleteReset(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
DxgkPresentNotifyDeviceRemoved(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
DxgkPresentCancelDevice(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ struct _DXGKRNL_DEVICE *Device);

VOID
DxgkPresentCancelContext(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ struct _DXGKRNL_CONTEXT *Context);

VOID
DxgkPresentCancelAllStopped(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

NTSTATUS
DxgkpWaitForVerticalBlank(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_opt_ struct _DXGKRNL_DEVICE *Device,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ UINT NumObjects,
    _In_reads_opt_(NumObjects) CONST D3DKMT_PTR_TYPE *ObjectHandleArray);

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
NTSTATUS
DxgkPresentOpenDwmVBlankEvent(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ PHANDLE EventHandle);

NTSTATUS
DxgkPresentSetSyncRefreshCountWaitTarget(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ ULONG TargetSyncRefreshCount);
#endif

/*
 * DxgkpQueuePresent
 *
 * Enqueues a present operation into the per-VidPnSource FIFO.
 * Returns the assigned PresentId in *OutPresentId.
 *
 * For display-only adapters with FlipInterval == IMMEDIATE, the present
 * is executed inline (not queued) and the shadow FB is pushed to the GPU
 * synchronously before returning.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkpQueuePresent(
    _In_  struct _DXGKRNL_ADAPTER  *Adapter,
    _Inout_ PDXGKRNL_PRESENT_ENTRY  Entry,
    _Out_ ULONG64                  *OutPresentId);

/* Releases every owned reference in an entry and clears the fields. */
VOID
DxgkpReleasePresentEntry(
    _Inout_ PDXGKRNL_PRESENT_ENTRY Entry);

NTSTATUS
DxgkpAcquireSharedSurfaceSnapshot(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _Out_ PDXGKRNL_SHARED_SURFACE_SNAPSHOT Snapshot);

VOID
DxgkpReleaseSharedSurfaceSnapshot(
    _Inout_ PDXGKRNL_SHARED_SURFACE_SNAPSHOT Snapshot);

VOID
DxgkpBeginSharedSurfaceMutationLocked(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
DxgkpEndSharedSurfaceMutationLocked(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

/*
 * DxgkpProcessPresentQueue
 *
 * Dequeues and executes the head present from the specified VidPn source
 * queue.  Called from the VSync work item or directly for an immediate
 * present.
 *
 * Returns STATUS_SUCCESS if a present was executed, STATUS_NO_MORE_ENTRIES
 * if the queue was empty.
 *
 * IRQL: PASSIVE_LEVEL (called from work-item context)
 */
NTSTATUS
DxgkpProcessPresentQueue(
    _In_ struct _DXGKRNL_ADAPTER          *Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId);

/*
 * DxgkpNotifyVSync
 *
 * Called from the adapter DPC for a recorded CRTC_VSYNC source.  It records
 * the pulse and queues a PASSIVE_LEVEL worker; it never calls a miniport DDI.
 *
 * IRQL: DISPATCH_LEVEL (ISR DPC context)
 */
VOID
DxgkpNotifyVSync(
    _In_ struct _DXGKRNL_ADAPTER          *Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId);

/* EOF */
