/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM GPU paging engine — internal header for paging.c
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * Overview
 * ---------
 * The paging engine is responsible for moving GPU allocations between
 * video memory (VRAM segments) and system memory when VRAM pressure rises.
 * It works by constructing "paging DMA buffers" — streams of GPU commands
 * that instruct the GPU's copy/blitter engine to transfer data between
 * memory locations.
 *
 * The workflow for a page-out (VRAM → system RAM):
 *   1. dxgkrnl calls DxgkMmsBuildPagingBuffer(TRANSFER, srcSeg→sysRAM).
 *   2. dxgmms1 allocates a DXGMMS_PAGING_BUFFER (16 KB DMA buffer in
 *      contiguous physical memory).
 *   3. dxgmms1 fills DXGKARG_BUILDPAGINGBUFFER and calls the miniport's
 *      DxgkDdiBuildPagingBuffer.  The miniport emits GPU copy commands.
 *   4. dxgkrnl calls DxgkMmsSubmitPagingBuffer.
 *   5. dxgmms1 submits the filled DMA buffer to the copy engine (node 0)
 *      at HIGH priority via DxgkMmsScheduleCommand.
 *   6. The GPU executes the copy.  Completion is signalled via the normal
 *      fence/interrupt path (DxgkMmsCommandCompleted).
 *
 * PageIn / PageOut boundary
 * -------------------------
 * The current PageIn / PageOut entry points do not carry the transfer size or
 * system-memory MDL required by DXGK_OPERATION_TRANSFER. They therefore return
 * STATUS_NOT_SUPPORTED instead of manufacturing an invalid zero-byte request.
 * Active VidMm paging builds the complete miniport operation directly.
 *
 * Aperture operations (MAP / UNMAP) do not require a DMA transfer;
 * they only build and submit a paging buffer that programs the GPU's
 * IOMMU / aperture tables.  They follow the same build-then-submit path
 * but are dispatched synchronously without a separate work item because
 * they are always called at PASSIVE_LEVEL.
 *
 * Pool tag: 'PgmG'  (displayed as 'GmgP' in poolmon — little-endian)
 *
 * Lock / IRQL ordering
 * ---------------------
 *   DxgkMmsInitializePagingBuffer   PASSIVE_LEVEL
 *   DxgkMmsFreePagingBuffer         PASSIVE_LEVEL
 *   DxgkMmsBuildPagingBuffer        PASSIVE_LEVEL
 *   DxgkMmsSubmitPagingBuffer       PASSIVE_LEVEL
 *   DxgkMmsPageIn                   PASSIVE_LEVEL
 *   DxgkMmsPageOut                  PASSIVE_LEVEL
 *   DxgkMmsMapAperture               PASSIVE_LEVEL
 *   DxgkMmsUnmapAperture             PASSIVE_LEVEL
 */

#ifndef _DXGMMS1_PAGING_H_
#define _DXGMMS1_PAGING_H_

#include "dxgmms1_private.h"

/* =========================================================================
 * DXGK paging DDI types
 *
 * These types are defined in the Windows DDK's dispmprt.h / d3dkmddi.h but
 * are not present in the ReactOS SDK headers at this stage.  We define a
 * self-contained subset here so that paging.c can call the miniport DDI
 * without depending on external SDK headers.
 *
 * Naming and layout match the Windows 10 WDK definitions exactly so that
 * these declarations can be removed once the ReactOS SDK is updated.
 * ========================================================================= */

/*
 * DXGK_OPERATION_* — paging DMA buffer operation codes.
 *
 * Passed as DXGKARG_BUILDPAGINGBUFFER.Operation.
 * Values are stable across Windows WDDM versions (Vista+).
 */
#ifndef DXGK_OPERATION_TRANSFER
#define DXGK_OPERATION_TRANSFER                 0
#define DXGK_OPERATION_FILL                     1
#define DXGK_OPERATION_DISCARD_CONTENT          2
#define DXGK_OPERATION_READ_PHYSICAL            3
#define DXGK_OPERATION_WRITE_PHYSICAL           4
#define DXGK_OPERATION_MAP_APERTURE_SEGMENT     5
#define DXGK_OPERATION_UNMAP_APERTURE_SEGMENT   6
#define DXGK_OPERATION_SPECIAL_LOCK_TRANSFER    7
#endif /* DXGK_OPERATION_TRANSFER */

/*
 * DXGKARG_BUILDPAGINGBUFFER
 *
 * Argument structure passed by dxgmms1 to the miniport's
 * DxgkDdiBuildPagingBuffer DDI.  The miniport writes GPU commands into
 * pDmaBuffer and advances DmaBufferWriteOffset to indicate how many bytes
 * it consumed.
 *
 * Layout matches the Windows 10 WDK DXGKARG_BUILDPAGINGBUFFER exactly
 * (64-bit build; verified against the WDK 10.0.26100 headers).
 */
#ifndef _DXGKARG_BUILDPAGINGBUFFER_DEFINED_
#define _DXGKARG_BUILDPAGINGBUFFER_DEFINED_

typedef struct _DXGKARG_BUILDPAGINGBUFFER
{
    /*
     * Kernel VA of the DMA command buffer.
     * Filled by dxgmms1 before calling the miniport.
     */
    PVOID   pDmaBuffer;

    /*
     * Total size of the DMA buffer in bytes.
     */
    ULONG   DmaSize;

    /*
     * On entry: byte offset of the first writable slot in pDmaBuffer.
     * On return: updated by the miniport to reflect how many bytes it wrote.
     */
    ULONG   DmaBufferWriteOffset;

    /*
     * Private data area for the miniport (opaque to dxgmms1).
     * NULL when not used.
     */
    PVOID   pDmaBufferPrivateData;

    /*
     * Size of the private data area in bytes.
     */
    ULONG   DmaBufferPrivateDataSize;

    /*
     * Write offset into pDmaBufferPrivateData (mirrors DmaBufferWriteOffset).
     */
    ULONG   DmaBufferPrivateDataWriteOffset;

    /*
     * Paging operation: DXGK_OPERATION_*.
     */
    ULONG   Operation;

    /*
     * Operation-specific parameters.
     */
    union
    {
        /*
         * DXGK_OPERATION_TRANSFER and DXGK_OPERATION_SPECIAL_LOCK_TRANSFER.
         */
        struct
        {
            HANDLE  hAllocation;        /* Allocation handle                 */
            ULONG   TransferOffset;     /* Byte offset within the allocation */
            SIZE_T  TransferSize;       /* Number of bytes to transfer       */

            struct
            {
                ULONG           SegmentId;      /* Source segment (0=sysmem)  */
                ULONGLONG       SegmentAddress; /* Byte offset in segment     */
                PMDL            pMdl;           /* MDL (when SegmentId == 0)  */
            } Source;

            struct
            {
                ULONG           SegmentId;      /* Dest segment (0=sysmem)    */
                ULONGLONG       SegmentAddress; /* Byte offset in segment     */
                PMDL            pMdl;           /* MDL (when SegmentId == 0)  */
            } Destination;
        } Transfer;

        /*
         * DXGK_OPERATION_FILL.
         */
        struct
        {
            ULONG       SegmentId;      /* Segment to fill                   */
            SIZE_T      FillSize;       /* Number of bytes to fill           */
            UINT        FillPattern;    /* 32-bit pattern to repeat          */
            ULONGLONG   DestinationSegmentAddress; /* Byte offset in segment */
        } Fill;

        /*
         * DXGK_OPERATION_DISCARD_CONTENT.
         */
        struct
        {
            HANDLE    hAllocation;
            ULONG     SegmentId;
            SIZE_T    AllocationSize;
            ULONGLONG SegmentAddress;
        } DiscardContent;

        /*
         * DXGK_OPERATION_MAP_APERTURE_SEGMENT.
         */
        struct
        {
            ULONG   SegmentId;          /* 1-based aperture segment          */
            SIZE_T  OffsetInPages;      /* First page slot to map            */
            SIZE_T  NumberOfPages;      /* Number of pages                   */
            PMDL    pMdl;               /* System-memory MDL to map          */
        } MapApertureSegment;

        /*
         * DXGK_OPERATION_UNMAP_APERTURE_SEGMENT.
         */
        struct
        {
            ULONG   SegmentId;
            SIZE_T  OffsetInPages;
            SIZE_T  NumberOfPages;
        } UnmapApertureSegment;

    }; /* anonymous union */

} DXGKARG_BUILDPAGINGBUFFER, *PDXGKARG_BUILDPAGINGBUFFER;

#endif /* _DXGKARG_BUILDPAGINGBUFFER_DEFINED_ */

/*
 * DXGKDDI_BUILDPAGINGBUFFER
 *
 * Miniport DDI entry point for building paging DMA buffers.
 * The miniport fills pDmaBuffer with GPU commands that implement the
 * requested paging operation.
 *
 * IRQL: PASSIVE_LEVEL
 */
#ifndef DXGKDDI_BUILDPAGINGBUFFER_DEFINED
#define DXGKDDI_BUILDPAGINGBUFFER_DEFINED
typedef NTSTATUS
(NTAPI *DXGKDDI_BUILDPAGINGBUFFER)(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER  pBuildPagingBuffer);
#endif /* DXGKDDI_BUILDPAGINGBUFFER_DEFINED */

/* =========================================================================
 * Constants
 * ========================================================================= */

/*
 * DXGMMS_PAGING_BUFFER_SIZE
 *
 * Size in bytes of the contiguous DMA buffer reserved for paging commands.
 * 16 KB provides enough room for the miniport to encode a full-page
 * scatter-gather transfer (typically < 4 KB of GPU commands for a 4 KB page
 * transfer, but we allocate generously to handle large multi-page bursts).
 *
 * Must be a multiple of PAGE_SIZE so MmAllocateContiguousMemory can satisfy
 * the request from the contiguous pool without wasting PFN entries.
 */
#define DXGMMS_PAGING_BUFFER_SIZE       (16 * 1024)   /* 16 KB */

/*
 * DXGMMS_PAGING_BUFFER_ALIGN
 *
 * Physical alignment constraint for the paging DMA buffer.
 * PAGE_SIZE (4 KB) alignment satisfies all known GPU DMA engines.
 * We use a 4 KB-aligned allocation so that the GPU engine's
 * command-fetch logic does not need to handle sub-page splits.
 */
#define DXGMMS_PAGING_BUFFER_ALIGN      PAGE_SIZE

/*
 * DXGMMS_PAGING_NODE_ORDINAL
 *
 * The GPU engine node (zero-based) used for paging DMA operations.
 * Conventionally node 0 is the 3D / graphics engine, which all WDDM
 * miniports are required to support.  Some hardware exposes a dedicated
 * copy engine at node 2; we default to node 0 for maximum compatibility.
 */
#define DXGMMS_PAGING_NODE_ORDINAL      0

/*
 * Pool tag for all paging-layer allocations.
 * Stored in memory as 'G','m','g','P' (little-endian); tools display 'PgmG'.
 */
#define TAG_DXGMMS_PAGING               'GmgP'


/* =========================================================================
 * DXGMMS_PAGING_BUFFER
 *
 * Describes one in-flight paging DMA buffer.  The Buffer / PhysicalAddress
 * pair refers to a contiguous region of non-cached physical memory that the
 * GPU DMA engine can read from (the miniport fills it with GPU commands
 * during BuildPagingBuffer).
 *
 * Lifecycle:
 *   Allocated  : DxgkMmsInitializePagingBuffer (once per adapter, at start).
 *   Filled     : DxgkMmsBuildPagingBuffer (one operation per call).
 *   Submitted  : DxgkMmsSubmitPagingBuffer (queued to copy engine).
 *   Freed      : DxgkMmsFreePagingBuffer (at adapter-remove time).
 *
 * Only one paging buffer exists per adapter. Callers must serialize its use
 * and wait for the submitted fence before building the next operation.
 * ========================================================================= */
typedef struct _DXGMMS_PAGING_BUFFER
{
    /*
     * Kernel virtual address of the DMA buffer region.
     * Written to DXGKARG_BUILDPAGINGBUFFER.pDmaBuffer.
     * Allocated with MmAllocateContiguousMemorySpecifyCache (NonCached).
     */
    PVOID               Buffer;

    /*
     * Total allocated size in bytes (== DXGMMS_PAGING_BUFFER_SIZE).
     */
    SIZE_T              BufferSize;

    /*
     * Number of bytes the miniport has written into Buffer so far during
     * the current BuildPagingBuffer call.  Updated by the miniport via
     * DXGKARG_BUILDPAGINGBUFFER.DmaBufferWriteOffset on return; reset to
     * zero at the start of each new BuildPagingBuffer sequence.
     */
    SIZE_T              UsedSize;

    /*
     * Physical address of Buffer[0].  Passed to the GPU submit path so
     * that the DMA engine can locate the command stream in physical memory.
     * Set once by DxgkMmsInitializePagingBuffer; read-only thereafter.
     */
    PHYSICAL_ADDRESS    PhysicalAddress;

    /*
     * Fence identifier of the most recently submitted paging operation on
     * this buffer.  Compared against Node->LastCompletedFenceId in
     * DxgkMmsWaitForPagingFence to determine when the GPU has finished
     * reading the command stream before we overwrite Buffer.
     */
    ULONG64             SubmitFenceId;

    /*
     * Intrusive list entry.  Currently unused (only one paging buffer
     * per adapter exists), but retained so that a future multi-buffer
     * pool can chain them without changing the structure layout.
     */
    LIST_ENTRY          Entry;

} DXGMMS_PAGING_BUFFER, *PDXGMMS_PAGING_BUFFER;


/* =========================================================================
 * Public function declarations — paging.c
 * ========================================================================= */

/*
 * DxgkMmsInitializePagingBuffer
 *
 * Allocate and initialise the adapter-wide paging DMA buffer.  Must be
 * called once at adapter-start time (from DxgkMmsAddAdapter), before any
 * paging operations are requested.
 *
 * On success the paging buffer is stored at Adapter->PagingBuffer.
 *
 * Returns:
 *   STATUS_SUCCESS        on success.
 *   STATUS_NO_MEMORY      if MmAllocateContiguousMemorySpecifyCache fails.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsInitializePagingBuffer(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter);

/*
 * DxgkMmsFreePagingBuffer
 *
 * Release the contiguous DMA buffer allocated by DxgkMmsInitializePagingBuffer.
 * Must be called from DxgkMmsRemoveAdapter after all in-flight paging
 * operations have drained.
 *
 * Idempotent: safe to call even if DxgkMmsInitializePagingBuffer was not
 * called or failed (checks for NULL before freeing).
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkMmsFreePagingBuffer(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter);

/*
 * DxgkMmsBuildPagingBuffer
 *
 * Construct a GPU paging DMA buffer for a single allocation movement
 * operation.  Fills the adapter's pre-allocated DXGMMS_PAGING_BUFFER with
 * GPU commands by calling the miniport's DxgkDdiBuildPagingBuffer DDI.
 *
 * Parameters:
 *   AdapterContext  — dxgmms1 per-adapter context (PDXGMMS_ADAPTER_CONTEXT).
 *   PagingBuffer    — caller-supplied pointer that receives the address of
 *                     the filled DXGMMS_PAGING_BUFFER.  The caller passes
 *                     this to DxgkMmsSubmitPagingBuffer.
 *   Operation       — DXGK_OPERATION_* constant (TRANSFER, FILL, etc.).
 *   Allocation      — dxgkrnl allocation handle (PVOID / PHANDLE).
 *   SrcOffset       — byte offset in the source segment.
 *   DstOffset       — byte offset in the destination segment.
 *   TransferSize    — number of bytes to move.
 *
 * Returns:
 *   STATUS_SUCCESS              on success; *PagingBuffer is valid.
 *   STATUS_INVALID_PARAMETER    if AdapterContext or PagingBuffer is NULL.
 *   STATUS_GRAPHICS_NO_VIDEO_MEMORY  if the pre-allocated buffer is in use
 *                               (previous operation not yet completed).
 *   Miniport error code         if DxgkDdiBuildPagingBuffer fails.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkMmsBuildPagingBuffer(
    _In_  PVOID     AdapterContext,
    _Out_ PVOID     PagingBuffer,
    _In_  ULONG     Operation,
    _In_  PVOID     Allocation,
    _In_  ULONGLONG SrcOffset,
    _In_  ULONGLONG DstOffset,
    _In_  SIZE_T    TransferSize);

/*
 * DxgkMmsSubmitPagingBuffer
 *
 * Submit a previously built paging DMA buffer to the GPU copy engine.
 * The buffer is inserted at HIGH priority in the scheduler queue for
 * node DXGMMS_PAGING_NODE_ORDINAL so that paging work preempts normal
 * rendering.
 *
 * After submission, blocks at PASSIVE_LEVEL until the GPU fence advances
 * past PagingBuffer->SubmitFenceId to confirm the copy is complete.
 *
 * Parameters:
 *   AdapterContext — dxgmms1 per-adapter context.
 *   PagingBuffer   — pointer returned by DxgkMmsBuildPagingBuffer.
 *
 * Returns:
 *   STATUS_SUCCESS       on successful GPU execution.
 *   STATUS_TIMEOUT       if the GPU does not complete within a reasonable
 *                        time (the TDR subsystem should trigger separately).
 *   STATUS_INVALID_PARAMETER  if either pointer is NULL.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkMmsSubmitPagingBuffer(
    _In_ PVOID AdapterContext,
    _In_ PVOID PagingBuffer);

/*
 * DxgkMmsPageIn
 *
 * Reserved interface for moving an allocation from system memory into VRAM.
 * The signature lacks the nonzero transfer size and MDL required by the
 * miniport paging DDI, so the implementation returns STATUS_NOT_SUPPORTED.
 *
 * Parameters:
 *   Adapter         — dxgmms1 per-adapter context.
 *   DxgkAllocation  — dxgkrnl allocation handle.
 *   TargetSegmentId — 1-based segment ID of the VRAM destination.
 *   TargetOffset    — byte offset within TargetSegmentId.
 *
 * Returns:
 *   STATUS_NOT_SUPPORTED    transfer metadata is not present in this ABI.
 *   STATUS_INVALID_PARAMETER for invalid adapter/allocation/segment input.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsPageIn(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ PVOID                   DxgkAllocation,
    _In_ ULONG                   TargetSegmentId,
    _In_ ULONGLONG               TargetOffset);

/*
 * DxgkMmsPageOut
 *
 * Reserved interface for moving an allocation from VRAM into system memory.
 * The signature lacks the nonzero transfer size and MDL required by the
 * miniport paging DDI, so the implementation returns STATUS_NOT_SUPPORTED.
 *
 * Parameters:
 *   Adapter      — dxgmms1 per-adapter context.
 *   DxgkAllocation — dxgkrnl allocation handle.
 *   SrcSegmentId — 1-based segment ID of the VRAM source.
 *   SrcOffset    — byte offset within SrcSegmentId.
 *
 * Returns:
 *   STATUS_NOT_SUPPORTED    transfer metadata is not present in this ABI.
 *   STATUS_INVALID_PARAMETER for invalid adapter/allocation/segment input.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsPageOut(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ PVOID                   DxgkAllocation,
    _In_ ULONG                   SrcSegmentId,
    _In_ ULONGLONG               SrcOffset);

/*
 * DxgkMmsMapAperture
 *
 * Map system-memory pages into a GPU aperture segment so that the GPU can
 * access them without an explicit VRAM copy.  The GPU programs its IOMMU /
 * aperture tables as commanded by the paging DMA buffer built here.
 *
 * Parameters:
 *   Adapter       — dxgmms1 per-adapter context.
 *   SegmentId     — 1-based aperture segment ID.
 *   OffsetInPages — first aperture page slot to fill (0-based page offset).
 *   SysMdl        — MDL describing the system-memory pages to map.
 *   NumPages      — number of pages to map starting at OffsetInPages.
 *
 * Returns:
 *   STATUS_SUCCESS          on success.
 *   STATUS_INVALID_PARAMETER  if any pointer is NULL or out of range.
 *   Propagated status       from BuildPagingBuffer or SubmitPagingBuffer.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsMapAperture(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   SegmentId,
    _In_ ULONGLONG               OffsetInPages,
    _In_ PMDL                    SysMdl,
    _In_ ULONG                   NumPages);

/*
 * DxgkMmsUnmapAperture
 *
 * Reverse a previous DxgkMmsMapAperture operation.  The GPU removes the
 * aperture table entries for the specified page range.
 *
 * Parameters:
 *   Adapter       — dxgmms1 per-adapter context.
 *   SegmentId     — 1-based aperture segment ID.
 *   OffsetInPages — first aperture page slot to clear (0-based page offset).
 *   NumPages      — number of pages to unmap.
 *
 * Returns:
 *   STATUS_SUCCESS          on success.
 *   STATUS_INVALID_PARAMETER  if any pointer is NULL or out of range.
 *   Propagated status       from BuildPagingBuffer or SubmitPagingBuffer.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsUnmapAperture(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   SegmentId,
    _In_ ULONGLONG               OffsetInPages,
    _In_ ULONG                   NumPages);

/*
 * DxgkMmsWaitForPagingFence
 *
 * Block the calling thread until the GPU's copy engine has advanced its
 * completed-fence counter to at least FenceId.  Used internally by
 * DxgkMmsSubmitPagingBuffer.
 *
 * Polls with a short exponential back-off to avoid flooding the query path.
 * Falls back to KeDelayExecutionThread for the long-sleep phase.
 *
 * Returns:
 *   STATUS_SUCCESS   when the fence has been reached.
 *   STATUS_TIMEOUT   if the fence did not advance within the deadline
 *                    (currently 5 seconds; the TDR subsystem will handle
 *                    a genuine GPU hang separately).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsWaitForPagingFence(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG64                 FenceId);

#endif /* _DXGMMS1_PAGING_H_ */
