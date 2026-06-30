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

    /* Source and destination rectangles. */
    RECT                            SrcRect;
    RECT                            DstRect;

    /* Colour value for colour-fill presents (ARGB 32-bit). */
    UINT                            Color;

    /* Flip interval — how many VSyncs to wait before presenting.
     * D3DDDI_FLIPINTERVAL_IMMEDIATE = present immediately (no VSync wait).
     * D3DDDI_FLIPINTERVAL_ONE       = present at the next VSync.
     */
    D3DDDI_FLIPINTERVAL_TYPE        FlipInterval;

    /* Back-pointer to the device that submitted this present. */
    D3DKMT_HANDLE                   hDevice;

} DXGKRNL_PRESENT_ENTRY, *PDXGKRNL_PRESENT_ENTRY;

/* ========================================================================
 * DXGKRNL_PRESENT_QUEUE — Per-VidPn-source present FIFO
 *
 * One instance per VidPn source (display output).  Stored in the
 * PresentQueues array inside DXGKRNL_ADAPTER.  The queue is a fixed-size
 * circular buffer protected by a KSPIN_LOCK so that the enqueue path
 * (PASSIVE_LEVEL) and the VSync/timer dequeue path (DISPATCH_LEVEL DPC)
 * can safely coordinate.
 *
 * VSync tracking:
 *   VBlankCount is incremented by the VSync interrupt handler
 *   (DxgkCbNotifyInterrupt with DXGK_INTERRUPT_CRTC_VSYNC).  The
 *   dequeue path compares the entry's target VBlank against VBlankCount
 *   to decide when to execute the present.
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

    /*
     * VSync tracking.
     *   VBlankCount    — total vertical blanks since adapter start.
     *   LastPresentVBlank — VBlankCount at which the last present executed.
     */
    volatile LONG64                 VBlankCount;
    LONG64                          LastPresentVBlank;

    /* Back-pointer to the owning adapter. */
    struct _DXGKRNL_ADAPTER        *Adapter;

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
    _In_  PDXGKRNL_PRESENT_ENTRY    Entry,
    _Out_ ULONG64                  *OutPresentId);

/*
 * DxgkpProcessPresentQueue
 *
 * Dequeues and executes the head present from the specified VidPn source
 * queue.  Called from the VSync path (DPC context) or from the periodic
 * present timer work item for display-only adapters.
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
 * Called from DxgkCbNotifyInterrupt (CRTC_VSYNC) to bump the VBlank
 * counter on the specified VidPn source queue.  This is the trigger
 * for VSync-synchronized flip presents.
 *
 * IRQL: DISPATCH_LEVEL (ISR DPC context)
 */
VOID
DxgkpNotifyVSync(
    _In_ struct _DXGKRNL_ADAPTER          *Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId);

/* EOF */
