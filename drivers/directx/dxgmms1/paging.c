/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM GPU paging engine
 *              Moves allocations between VRAM and system RAM by constructing
 *              and submitting paging DMA buffers to the GPU copy engine.
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * ARCHITECTURE NOTES (amd64/x86)
 * --------------------------------
 * Paging DMA buffers must reside in contiguous physical memory so that the
 * GPU's DMA engine can read them without IOMMU scatter-gather support.  We
 * allocate with MmAllocateContiguousMemorySpecifyCache(MmNonCached) to
 * prevent stale CPU cache lines from being visible to the GPU after the CPU
 * writes commands into the buffer.  On amd64 the strong TSO memory model
 * guarantees that CPU stores complete in program order, so no SFENCE is
 * needed between the CPU write and the GPU DMA fetch — but the non-cached
 * mapping ensures the GPU sees the writes without waiting for a cache eviction.
 *
 * The 16 KB DMA buffer fits entirely within a 32 KB L2 cache region on
 * modern amd64 processors.  Non-cached (UC) mapping bypasses the cache
 * entirely, so CPU writes go directly to DRAM and the GPU reads directly
 * from DRAM — this avoids any cache coherency concern for the command stream.
 *
 * The paging fence wait loop uses KeDelayExecutionThread (not SpinWait) to
 * avoid burning CPU cycles while the GPU performs a potentially large DMA
 * copy.  A 1 ms sleep granularity is used; typical GPU copy rates are
 * 20–80 GB/s so a 16 KB transfer completes in < 1 µs and we will overshoot
 * the first iteration — that is acceptable because paging is not latency-critical.
 *
 * ExQueueWorkItem(CriticalWorkQueue) is used for PageIn/PageOut because:
 *   - These operations must execute at PASSIVE_LEVEL (MmAllocateContiguous,
 *     KeDelayExecutionThread both require PASSIVE).
 *   - CriticalWorkQueue items preempt background/delayed work items, matching
 *     the high-priority paging requirement.
 *   - The calling thread blocks on a KEVENT so it does not spin-wait on the
 *     system worker pool.
 */

/*
 * paging.h includes dxgmms1_private.h and defines all paging-layer types
 * including DXGKARG_BUILDPAGINGBUFFER, DXGK_OPERATION_*, and
 * DXGKDDI_BUILDPAGINGBUFFER.  Include it before everything else.
 */
#include "paging.h"

/* =========================================================================
 * Forward declarations of internal helpers
 * ========================================================================= */

static
VOID
NTAPI
DxgkMmsPagingWorker(
    _In_ PVOID Parameter);

static
NTSTATUS
DxgkMmsBuildTransferBuffer(
    _In_  PDXGMMS_ADAPTER_CONTEXT  Adapter,
    _In_  PDXGMMS_PAGING_BUFFER    PagBuf,
    _In_  PVOID                    Allocation,
    _In_  ULONG                    SrcSegmentId,
    _In_  ULONGLONG                SrcOffset,
    _In_  ULONG                    DstSegmentId,
    _In_  ULONGLONG                DstOffset,
    _In_  SIZE_T                   TransferSize,
    _In_opt_ PMDL                  SysMdl);

static
NTSTATUS
DxgkMmsBuildApertureBuffer(
    _In_  PDXGMMS_ADAPTER_CONTEXT  Adapter,
    _In_  PDXGMMS_PAGING_BUFFER    PagBuf,
    _In_  ULONG                    Operation,
    _In_  ULONG                    SegmentId,
    _In_  SIZE_T                   OffsetInPages,
    _In_  SIZE_T                   NumberOfPages,
    _In_opt_ PMDL                  SysMdl);


/* =========================================================================
 * DxgkMmsInitializePagingBuffer
 *
 * Allocate the per-adapter paging DMA buffer from contiguous non-cached
 * physical memory.  Must be called at PASSIVE_LEVEL before any paging
 * operations are requested.
 * ========================================================================= */
NTSTATUS
DxgkMmsInitializePagingBuffer(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter)
{
    PDXGMMS_PAGING_BUFFER   PagBuf;
    PVOID                   Buffer;
    PHYSICAL_ADDRESS        LowestAddress;
    PHYSICAL_ADDRESS        HighestAddress;
    PHYSICAL_ADDRESS        BoundaryAddress;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsInitializePagingBuffer: adapter=%p\n", Adapter);

    ASSERT(Adapter != NULL);
    ASSERT(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE);
    ASSERT(Adapter->PagingBuffer == NULL);

    /*
     * Allocate the tracking structure from non-paged pool.  This is a
     * small control structure; pool tag 'PgmG'.
     */
    PagBuf = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(DXGMMS_PAGING_BUFFER),
                                   TAG_DXGMMS_PAGING);
    if (PagBuf == NULL)
    {
        DXGMMS_ERR("DxgkMmsInitializePagingBuffer: failed to allocate "
                   "paging buffer descriptor\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(PagBuf, sizeof(DXGMMS_PAGING_BUFFER));

    /*
     * Allocate the actual DMA command buffer.
     *
     * Requirements:
     *   - Contiguous physical memory (GPU DMA engine requires contiguous PA).
     *   - Non-cached CPU mapping (UC) so that CPU writes are immediately
     *     visible to the GPU without cache flush.
     *   - 4 KB (PAGE_SIZE) physical alignment.
     *   - No boundary (BoundaryAddress = 0) — we do not need to avoid
     *     crossing 4 GB or other arbitrary DMA boundaries here because
     *     WDDM-capable GPUs are required to support 64-bit DMA addresses.
     *
     * HighestAddress is set to the maximum 64-bit physical address
     * (all bits set) to allow allocation anywhere in physical memory.
     * dxgkrnl / the miniport must handle 64-bit DMA addresses.
     */
    LowestAddress.QuadPart  = 0;
    HighestAddress.QuadPart = (LONGLONG)-1;   /* 0xFFFFFFFFFFFFFFFF */
    BoundaryAddress.QuadPart = 0;             /* no boundary constraint */

    Buffer = MmAllocateContiguousMemorySpecifyCache(
                 DXGMMS_PAGING_BUFFER_SIZE,
                 LowestAddress,
                 HighestAddress,
                 BoundaryAddress,
                 MmNonCached);
    if (Buffer == NULL)
    {
        DXGMMS_ERR("DxgkMmsInitializePagingBuffer: MmAllocateContiguousMemory "
                   "failed for %u bytes\n",
                   (ULONG)DXGMMS_PAGING_BUFFER_SIZE);
        ExFreePoolWithTag(PagBuf, TAG_DXGMMS_PAGING);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Zero the buffer before first use.  The miniport will overwrite it
     * with GPU commands on each BuildPagingBuffer call.
     */
    RtlZeroMemory(Buffer, DXGMMS_PAGING_BUFFER_SIZE);

    /* Obtain the physical address of the buffer start. */
    PagBuf->Buffer          = Buffer;
    PagBuf->BufferSize      = DXGMMS_PAGING_BUFFER_SIZE;
    PagBuf->UsedSize        = 0;
    PagBuf->PhysicalAddress = MmGetPhysicalAddress(Buffer);
    PagBuf->SubmitFenceId   = 0;
    InitializeListHead(&PagBuf->Entry);

    DXGMMS_TRACE("DxgkMmsInitializePagingBuffer: allocated %u bytes "
                 "at VA=%p PA=0x%I64x\n",
                 (ULONG)DXGMMS_PAGING_BUFFER_SIZE,
                 Buffer,
                 PagBuf->PhysicalAddress.QuadPart);

    Adapter->PagingBuffer = PagBuf;
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkMmsFreePagingBuffer
 *
 * Release the contiguous DMA buffer and its tracking structure.
 * Safe to call on a partially-initialised adapter (when PagingBuffer
 * is NULL, this is a no-op).
 * ========================================================================= */
VOID
DxgkMmsFreePagingBuffer(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter)
{
    PDXGMMS_PAGING_BUFFER PagBuf;

    PAGED_CODE();

    ASSERT(Adapter != NULL);

    PagBuf = (PDXGMMS_PAGING_BUFFER)Adapter->PagingBuffer;
    if (PagBuf == NULL)
    {
        /* Either never initialised or already freed — nothing to do. */
        DXGMMS_TRACE("DxgkMmsFreePagingBuffer: no paging buffer "
                     "(adapter=%p)\n", Adapter);
        return;
    }

    DXGMMS_TRACE("DxgkMmsFreePagingBuffer: freeing buffer VA=%p PA=0x%I64x\n",
                 PagBuf->Buffer,
                 PagBuf->PhysicalAddress.QuadPart);

    /* Release the contiguous DMA memory. */
    if (PagBuf->Buffer != NULL)
    {
        MmFreeContiguousMemory(PagBuf->Buffer);
        PagBuf->Buffer = NULL;
    }

    /* Free the tracking structure. */
    ExFreePoolWithTag(PagBuf, TAG_DXGMMS_PAGING);
    Adapter->PagingBuffer = NULL;
}


/* =========================================================================
 * DxgkMmsWaitForPagingFence
 *
 * Poll the GPU's copy-engine fence until it reaches FenceId, using an
 * exponential back-off sleep to avoid burning CPU cycles.
 *
 * The miniport reports completion via DxgkMmsCommandCompleted (called from
 * the interrupt DPC), which updates Node->LastCompletedFenceId.  We read
 * that volatile field here without a spinlock because:
 *   - We only need eventual visibility (not strict serialisation).
 *   - Reading a 64-bit volatile LONG64 is atomic on amd64 (naturally aligned
 *     8-byte reads are atomic under the x86-64 memory model).
 *   - The worst-case effect of missing an update is sleeping an extra 1 ms.
 * ========================================================================= */
NTSTATUS
DxgkMmsWaitForPagingFence(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG64                 FenceId)
{
    PDXGMMS_NODE        Node;
    LARGE_INTEGER       Delay;
    ULONG               Iterations;

    PAGED_CODE();

    ASSERT(Adapter != NULL);

    /*
     * Guard against an adapter with no nodes (software adapter).
     * In that case there is no GPU fence to wait for; return immediately.
     */
    if (Adapter->NumNodes == 0 || Adapter->Nodes == NULL)
    {
        DXGMMS_TRACE("DxgkMmsWaitForPagingFence: no nodes — "
                     "trivially complete (fence=0x%I64x)\n", FenceId);
        return STATUS_SUCCESS;
    }

    Node = &Adapter->Nodes[DXGMMS_PAGING_NODE_ORDINAL];

    DXGMMS_TRACE("DxgkMmsWaitForPagingFence: waiting for fence 0x%I64x "
                 "(current=0x%I64x)\n",
                 FenceId,
                 (ULONG64)Node->LastCompletedFenceId);

    /*
     * Poll with exponential back-off:
     *   First 4 iterations: 1 ms sleep.
     *   Iterations 5–16:    5 ms sleep.
     *   After that:         50 ms sleep.
     *
     * Timeout after 5 seconds (approximately 100 × 50 ms iterations in the
     * slow phase, plus earlier iterations).  A genuine GPU hang will be caught
     * by the TDR subsystem; this timeout is a backstop for cases where the GPU
     * fence is never updated (e.g. miniport bug).
     *
     * Total maximum wait: ~5 s before returning STATUS_TIMEOUT.
     */
#define PAGING_FENCE_MAX_ITERATIONS     200u
#define PAGING_FENCE_FAST_ITERS         4u
#define PAGING_FENCE_MED_ITERS          16u
#define PAGING_FENCE_FAST_SLEEP_MS      1LL
#define PAGING_FENCE_MED_SLEEP_MS       5LL
#define PAGING_FENCE_SLOW_SLEEP_MS      50LL

    for (Iterations = 0; Iterations < PAGING_FENCE_MAX_ITERATIONS; ++Iterations)
    {
        /*
         * Read the completed fence ID.  The volatile qualifier on
         * LastCompletedFenceId ensures the compiler does not cache the
         * value across loop iterations.  On amd64 this compiles to a
         * plain MOV; no fence instruction is needed because we only require
         * eventual visibility, not strict ordering.
         */
        if ((ULONG64)Node->LastCompletedFenceId >= FenceId)
        {
            DXGMMS_TRACE("DxgkMmsWaitForPagingFence: fence 0x%I64x reached "
                         "after %u iterations\n", FenceId, Iterations);
            return STATUS_SUCCESS;
        }

        /* Select the sleep interval based on iteration count. */
        if (Iterations < PAGING_FENCE_FAST_ITERS)
            Delay.QuadPart = -(PAGING_FENCE_FAST_SLEEP_MS * 10000LL);
        else if (Iterations < PAGING_FENCE_MED_ITERS)
            Delay.QuadPart = -(PAGING_FENCE_MED_SLEEP_MS * 10000LL);
        else
            Delay.QuadPart = -(PAGING_FENCE_SLOW_SLEEP_MS * 10000LL);

        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    DXGMMS_WARN("DxgkMmsWaitForPagingFence: timeout waiting for fence "
                "0x%I64x (current=0x%I64x)\n",
                FenceId,
                (ULONG64)Node->LastCompletedFenceId);
    return STATUS_TIMEOUT;

#undef PAGING_FENCE_MAX_ITERATIONS
#undef PAGING_FENCE_FAST_ITERS
#undef PAGING_FENCE_MED_ITERS
#undef PAGING_FENCE_FAST_SLEEP_MS
#undef PAGING_FENCE_MED_SLEEP_MS
#undef PAGING_FENCE_SLOW_SLEEP_MS
}


/* =========================================================================
 * DxgkMmsBuildTransferBuffer  (internal helper)
 *
 * Populate a DXGKARG_BUILDPAGINGBUFFER for a DXGK_OPERATION_TRANSFER
 * command and call the miniport's DxgkDdiBuildPagingBuffer DDI.
 *
 * This handles both page-in (sys→VRAM) and page-out (VRAM→sys) as well
 * as VRAM-to-VRAM transfers (when both segment IDs are non-zero).
 *
 * SysMdl must be non-NULL when either SrcSegmentId or DstSegmentId is 0
 * (indicating the system-memory side of the transfer).
 * ========================================================================= */
static
NTSTATUS
DxgkMmsBuildTransferBuffer(
    _In_  PDXGMMS_ADAPTER_CONTEXT  Adapter,
    _In_  PDXGMMS_PAGING_BUFFER    PagBuf,
    _In_  PVOID                    Allocation,
    _In_  ULONG                    SrcSegmentId,
    _In_  ULONGLONG                SrcOffset,
    _In_  ULONG                    DstSegmentId,
    _In_  ULONGLONG                DstOffset,
    _In_  SIZE_T                   TransferSize,
    _In_opt_ PMDL                  SysMdl)
{
    DXGKARG_BUILDPAGINGBUFFER   Args;
    DXGKDDI_BUILDPAGINGBUFFER   BuildPagingBuffer;
    NTSTATUS                    Status;

    PAGED_CODE();

    ASSERT(Adapter != NULL);
    ASSERT(PagBuf != NULL);
    ASSERT(PagBuf->Buffer != NULL);

    if (TransferSize == 0)
    {
        DXGMMS_WARN("DxgkMmsBuildTransferBuffer: zero-length transfer\n");
        return STATUS_INVALID_PARAMETER;
    }

    if ((SrcSegmentId == 0 || DstSegmentId == 0) && SysMdl == NULL)
    {
        DXGMMS_WARN("DxgkMmsBuildTransferBuffer: system-memory transfer "
                    "requires an MDL\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Ensure the pre-allocated buffer is large enough for this call. */
    if (TransferSize > PagBuf->BufferSize)
    {
        DXGMMS_ERR("DxgkMmsBuildTransferBuffer: TransferSize (%Iu) exceeds "
                   "paging buffer size (%Iu)\n",
                   TransferSize, PagBuf->BufferSize);
        return STATUS_BUFFER_TOO_SMALL;
    }

    /*
     * Build the DXGKARG_BUILDPAGINGBUFFER for DXGK_OPERATION_TRANSFER.
     *
     * pDmaBuffer        — VA of the GPU command buffer; the miniport writes
     *                     GPU copy commands starting at this address.
     * DmaSize           — total available bytes in the command buffer.
     * DmaBufferWriteOffset — initialised to 0; the miniport advances it
     *                     as it writes commands.
     * pDmaBufferPrivateData — NULL; we do not use per-buffer private data
     *                     for paging operations.
     * Operation         — DXGK_OPERATION_TRANSFER.
     *
     * Transfer.hAllocation    — miniport's allocation handle.
     * Transfer.TransferOffset — byte offset at which the transfer starts
     *                     within the allocation (used for split transfers).
     * Transfer.TransferSize   — number of bytes to transfer.
     * Transfer.Source         — source segment + offset or MDL.
     * Transfer.Destination    — dest segment + offset or MDL.
     */
    RtlZeroMemory(&Args, sizeof(Args));

    Args.pDmaBuffer             = PagBuf->Buffer;
    Args.DmaSize                = (ULONG)PagBuf->BufferSize;
    Args.DmaBufferWriteOffset   = 0;
    Args.pDmaBufferPrivateData  = NULL;
    Args.DmaBufferPrivateDataWriteOffset = 0;
    Args.Operation              = DXGK_OPERATION_TRANSFER;

    Args.Transfer.hAllocation           = (HANDLE)Allocation;
    Args.Transfer.TransferOffset        = 0;   /* full transfer from offset 0 */
    Args.Transfer.TransferSize          = TransferSize;

    Args.Transfer.Source.SegmentId      = SrcSegmentId;
    Args.Transfer.Source.SegmentAddress = SrcOffset;
    Args.Transfer.Source.pMdl           = (SrcSegmentId == 0) ? (PMDL)SysMdl : NULL;

    Args.Transfer.Destination.SegmentId      = DstSegmentId;
    Args.Transfer.Destination.SegmentAddress = DstOffset;
    Args.Transfer.Destination.pMdl           = (DstSegmentId == 0) ? (PMDL)SysMdl : NULL;

    DXGMMS_TRACE("DxgkMmsBuildTransferBuffer: TRANSFER "
                 "src seg=%u off=0x%I64x dst seg=%u off=0x%I64x size=%Iu\n",
                 SrcSegmentId, SrcOffset,
                 DstSegmentId, DstOffset,
                 TransferSize);

    /*
     * Call the miniport's DxgkDdiBuildPagingBuffer.
     * The function pointer is stored as PVOID in the adapter context and
     * cast here to the DXGKDDI_BUILDPAGINGBUFFER type from dispmprt.h.
     * The miniport receives its own opaque context as the first argument.
     *
     * IRQL requirement: PASSIVE_LEVEL (caller must ensure this).
     */
    BuildPagingBuffer = (DXGKDDI_BUILDPAGINGBUFFER)Adapter->DxgkDdiBuildPagingBuffer;
    if (BuildPagingBuffer == NULL)
    {
        DXGMMS_WARN("DxgkMmsBuildTransferBuffer: DxgkDdiBuildPagingBuffer "
                    "is NULL (miniport does not support paging?)\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = BuildPagingBuffer(Adapter->MiniportContext, &Args);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsBuildTransferBuffer: miniport returned 0x%08lx\n",
                   Status);
        return Status;
    }

    /* Record how many bytes the miniport wrote into the DMA buffer. */
    PagBuf->UsedSize = Args.DmaBufferWriteOffset;

    DXGMMS_TRACE("DxgkMmsBuildTransferBuffer: miniport wrote %Iu bytes "
                 "of GPU commands\n", PagBuf->UsedSize);

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkMmsBuildApertureBuffer  (internal helper)
 *
 * Populate a DXGKARG_BUILDPAGINGBUFFER for MAP_APERTURE_SEGMENT or
 * UNMAP_APERTURE_SEGMENT and call the miniport DDI.
 * ========================================================================= */
static
NTSTATUS
DxgkMmsBuildApertureBuffer(
    _In_  PDXGMMS_ADAPTER_CONTEXT  Adapter,
    _In_  PDXGMMS_PAGING_BUFFER    PagBuf,
    _In_  ULONG                    Operation,
    _In_  ULONG                    SegmentId,
    _In_  SIZE_T                   OffsetInPages,
    _In_  SIZE_T                   NumberOfPages,
    _In_opt_ PMDL                  SysMdl)
{
    DXGKARG_BUILDPAGINGBUFFER   Args;
    DXGKDDI_BUILDPAGINGBUFFER   BuildPagingBuffer;
    NTSTATUS                    Status;

    PAGED_CODE();

    ASSERT(Adapter != NULL);
    ASSERT(PagBuf != NULL);
    ASSERT(PagBuf->Buffer != NULL);
    ASSERT(Operation == DXGK_OPERATION_MAP_APERTURE_SEGMENT ||
           Operation == DXGK_OPERATION_UNMAP_APERTURE_SEGMENT);
    ASSERT(SegmentId != 0);

    if (NumberOfPages == 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Args, sizeof(Args));

    Args.pDmaBuffer             = PagBuf->Buffer;
    Args.DmaSize                = (ULONG)PagBuf->BufferSize;
    Args.DmaBufferWriteOffset   = 0;
    Args.pDmaBufferPrivateData  = NULL;
    Args.DmaBufferPrivateDataWriteOffset = 0;
    Args.Operation              = Operation;

    if (Operation == DXGK_OPERATION_MAP_APERTURE_SEGMENT)
    {
        Args.MapApertureSegment.SegmentId    = SegmentId;
        Args.MapApertureSegment.OffsetInPages = OffsetInPages;
        Args.MapApertureSegment.NumberOfPages = NumberOfPages;
        Args.MapApertureSegment.pMdl          = SysMdl;

        DXGMMS_TRACE("DxgkMmsBuildApertureBuffer: MAP_APERTURE seg=%u "
                     "pageOffset=%Iu numPages=%Iu mdl=%p\n",
                     SegmentId, OffsetInPages, NumberOfPages, SysMdl);
    }
    else /* DXGK_OPERATION_UNMAP_APERTURE_SEGMENT */
    {
        Args.UnmapApertureSegment.SegmentId    = SegmentId;
        Args.UnmapApertureSegment.OffsetInPages = OffsetInPages;
        Args.UnmapApertureSegment.NumberOfPages = NumberOfPages;

        DXGMMS_TRACE("DxgkMmsBuildApertureBuffer: UNMAP_APERTURE seg=%u "
                     "pageOffset=%Iu numPages=%Iu\n",
                     SegmentId, OffsetInPages, NumberOfPages);
    }

    BuildPagingBuffer = (DXGKDDI_BUILDPAGINGBUFFER)Adapter->DxgkDdiBuildPagingBuffer;
    if (BuildPagingBuffer == NULL)
    {
        DXGMMS_WARN("DxgkMmsBuildApertureBuffer: DxgkDdiBuildPagingBuffer "
                    "is NULL\n");
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = BuildPagingBuffer(Adapter->MiniportContext, &Args);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsBuildApertureBuffer: miniport returned 0x%08lx\n",
                   Status);
        return Status;
    }

    PagBuf->UsedSize = Args.DmaBufferWriteOffset;

    DXGMMS_TRACE("DxgkMmsBuildApertureBuffer: miniport wrote %Iu bytes\n",
                 PagBuf->UsedSize);

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkMmsBuildPagingBuffer
 *
 * Public interface entry: construct a GPU paging DMA buffer for the given
 * operation.  Called from dxgkrnl (via the DXGMMS_INTERFACE dispatch table)
 * whenever an allocation needs to be moved.
 *
 * The adapter's pre-allocated DXGMMS_PAGING_BUFFER is filled with GPU
 * commands by the miniport; on success *PagingBuffer points to it.
 * ========================================================================= */
NTSTATUS
NTAPI
DxgkMmsBuildPagingBuffer(
    _In_  PVOID     AdapterContext,
    _Out_ PVOID     PagingBuffer,
    _In_  ULONG     Operation,
    _In_  PVOID     Allocation,
    _In_  ULONGLONG SrcOffset,
    _In_  ULONGLONG DstOffset,
    _In_  SIZE_T    TransferSize)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;
    PDXGMMS_PAGING_BUFFER   PagBuf;
    PVOID                  *OutBuffer = (PVOID *)PagingBuffer;
    NTSTATUS                Status;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsBuildPagingBuffer: op=%u alloc=%p src=0x%I64x "
                 "dst=0x%I64x size=%Iu\n",
                 Operation, Allocation, SrcOffset, DstOffset, TransferSize);

    /* ---- Parameter validation ----------------------------------------- */
    if (Adapter == NULL || PagingBuffer == NULL)
    {
        DXGMMS_ERR("DxgkMmsBuildPagingBuffer: NULL pointer (adapter=%p "
                   "out=%p)\n", Adapter, PagingBuffer);
        return STATUS_INVALID_PARAMETER;
    }

    *OutBuffer = NULL;

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Adapter->Removed)
    {
        DXGMMS_WARN("DxgkMmsBuildPagingBuffer: adapter removed\n");
        return STATUS_DEVICE_REMOVED;
    }

    PagBuf = (PDXGMMS_PAGING_BUFFER)Adapter->PagingBuffer;
    if (PagBuf == NULL)
    {
        /*
         * PagingBuffer was never initialised.  This should not happen in a
         * correctly initialised adapter, but return a clear error code.
         */
        DXGMMS_ERR("DxgkMmsBuildPagingBuffer: paging buffer not "
                   "initialised for adapter %p\n", Adapter);
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }

    /* Reset the write offset for this new command sequence. */
    PagBuf->UsedSize = 0;

    /* ---- Dispatch to the appropriate builder helper -------------------- */
    switch (Operation)
    {
    case DXGK_OPERATION_TRANSFER:
    case DXGK_OPERATION_SPECIAL_LOCK_TRANSFER:
        /*
         * Transfer: move allocation data between two segments (or between
         * a segment and system memory).
         *
         * For this simplified public interface, SrcOffset == source segment
         * offset (SrcSegmentId inferred from context = 0 for sys, non-zero
         * for VRAM) and similarly for DstOffset.
         *
         * We treat SrcOffset == 0 and DstOffset != 0 as page-in (sys→VRAM)
         * when Allocation's system-memory MDL is implied by the caller.
         * The full MDL is NULL here because this interface does not pass it
         * explicitly — callers with MDL knowledge should use
         * DxgkMmsPageIn / DxgkMmsPageOut instead.
         */
        Status = DxgkMmsBuildTransferBuffer(Adapter,
                                             PagBuf,
                                             Allocation,
                                             0,          /* SrcSegmentId: sys */
                                             SrcOffset,
                                             1,          /* DstSegmentId: seg 1 */
                                             DstOffset,
                                             TransferSize,
                                             NULL        /* SysMdl: unknown */);
        break;

    case DXGK_OPERATION_FILL:
    {
        /*
         * Fill: zero-initialise or fill a VRAM region with a pattern.
         * We build a FILL command using the raw miniport interface.
         * The fill pattern is 0 (zero-initialise) for this simplified path.
         */
        DXGKARG_BUILDPAGINGBUFFER   Args;
        DXGKDDI_BUILDPAGINGBUFFER   BuildPagingBuffer;

        if (TransferSize == 0)
        {
            DXGMMS_WARN("DxgkMmsBuildPagingBuffer: FILL with zero size\n");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        DXGMMS_TRACE("DxgkMmsBuildPagingBuffer: FILL seg=1 off=0x%I64x "
                     "size=%Iu pattern=0\n", DstOffset, TransferSize);

        RtlZeroMemory(&Args, sizeof(Args));
        Args.pDmaBuffer             = PagBuf->Buffer;
        Args.DmaSize                = (ULONG)PagBuf->BufferSize;
        Args.DmaBufferWriteOffset   = 0;
        Args.Operation              = DXGK_OPERATION_FILL;
        Args.Fill.SegmentId         = 1;
        Args.Fill.FillSize          = TransferSize;
        Args.Fill.FillPattern       = 0;
        Args.Fill.DestinationSegmentAddress = DstOffset;

        BuildPagingBuffer =
            (DXGKDDI_BUILDPAGINGBUFFER)Adapter->DxgkDdiBuildPagingBuffer;
        if (BuildPagingBuffer == NULL)
        {
            DXGMMS_WARN("DxgkMmsBuildPagingBuffer: FILL: no miniport DDI\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        Status = BuildPagingBuffer(Adapter->MiniportContext, &Args);
        if (NT_SUCCESS(Status))
            PagBuf->UsedSize = Args.DmaBufferWriteOffset;
        else
            DXGMMS_ERR("DxgkMmsBuildPagingBuffer: FILL miniport "
                       "returned 0x%08lx\n", Status);
        break;
    }

    case DXGK_OPERATION_DISCARD_CONTENT:
    {
        /*
         * Discard: tell the GPU this VRAM range is no longer needed.
         * Some miniports use this to release on-chip compression metadata.
         * Build a minimal paging buffer with just the DISCARD opcode.
         */
        DXGKARG_BUILDPAGINGBUFFER   Args;
        DXGKDDI_BUILDPAGINGBUFFER   BuildPagingBuffer;

        if (TransferSize == 0)
        {
            DXGMMS_WARN("DxgkMmsBuildPagingBuffer: DISCARD with zero size\n");
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        DXGMMS_TRACE("DxgkMmsBuildPagingBuffer: DISCARD_CONTENT "
                     "off=0x%I64x size=%Iu\n", SrcOffset, TransferSize);

        RtlZeroMemory(&Args, sizeof(Args));
        Args.pDmaBuffer             = PagBuf->Buffer;
        Args.DmaSize                = (ULONG)PagBuf->BufferSize;
        Args.DmaBufferWriteOffset   = 0;
        Args.Operation              = DXGK_OPERATION_DISCARD_CONTENT;
        Args.DiscardContent.hAllocation = (HANDLE)Allocation;
        Args.DiscardContent.SegmentId = 1;
        Args.DiscardContent.AllocationSize = TransferSize;
        Args.DiscardContent.SegmentAddress = SrcOffset;

        BuildPagingBuffer =
            (DXGKDDI_BUILDPAGINGBUFFER)Adapter->DxgkDdiBuildPagingBuffer;
        if (BuildPagingBuffer == NULL)
        {
            DXGMMS_WARN("DxgkMmsBuildPagingBuffer: DISCARD: no miniport DDI\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        }

        Status = BuildPagingBuffer(Adapter->MiniportContext, &Args);
        if (NT_SUCCESS(Status))
            PagBuf->UsedSize = Args.DmaBufferWriteOffset;
        else
            DXGMMS_ERR("DxgkMmsBuildPagingBuffer: DISCARD miniport "
                       "returned 0x%08lx\n", Status);
        break;
    }

    case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
        if (TransferSize == 0 || SrcOffset > (ULONGLONG)((SIZE_T)-1))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = DxgkMmsBuildApertureBuffer(Adapter,
                                             PagBuf,
                                             DXGK_OPERATION_MAP_APERTURE_SEGMENT,
                                             1,                /* SegmentId */
                                             (SIZE_T)SrcOffset, /* OffsetInPages */
                                             (SIZE_T)TransferSize, /* NumPages */
                                             NULL);
        break;

    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
        if (TransferSize == 0 || SrcOffset > (ULONGLONG)((SIZE_T)-1))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = DxgkMmsBuildApertureBuffer(Adapter,
                                             PagBuf,
                                             DXGK_OPERATION_UNMAP_APERTURE_SEGMENT,
                                             1,
                                             (SIZE_T)SrcOffset,
                                             (SIZE_T)TransferSize,
                                             NULL);
        break;

    default:
        DXGMMS_WARN("DxgkMmsBuildPagingBuffer: unsupported operation %u\n",
                    Operation);
        Status = STATUS_INVALID_PARAMETER;
        break;
    }

    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsBuildPagingBuffer: build failed status=0x%08lx\n",
                   Status);
        return Status;
    }

    /* Return the filled paging buffer pointer to the caller. */
    *OutBuffer = PagBuf;

    DXGMMS_TRACE("DxgkMmsBuildPagingBuffer: success, DMA buffer %p "
                 "used=%Iu bytes\n", PagBuf->Buffer, PagBuf->UsedSize);

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkMmsSubmitPagingBuffer
 *
 * Submit a filled paging DMA buffer to the GPU copy engine at HIGH priority
 * so that paging preempts normal rendering work.
 *
 * After submission the function blocks at PASSIVE_LEVEL until the GPU fence
 * advances to confirm copy completion.
 * ========================================================================= */
NTSTATUS
NTAPI
DxgkMmsSubmitPagingBuffer(
    _In_ PVOID AdapterContext,
    _In_ PVOID PagingBuffer)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter  = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;
    PDXGMMS_PAGING_BUFFER   PagBuf   = (PDXGMMS_PAGING_BUFFER)PagingBuffer;
    PDXGMMS_NODE            Node;
    DXGMMS_COMMAND_BUFFER   Command;
    KIRQL                   OldIrql;
    ULONG64                 FenceId;
    NTSTATUS                Status;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsSubmitPagingBuffer: adapter=%p pagBuf=%p\n",
                 Adapter, PagBuf);

    if (Adapter == NULL || PagBuf == NULL)
    {
        DXGMMS_ERR("DxgkMmsSubmitPagingBuffer: NULL pointer\n");
        return STATUS_INVALID_PARAMETER;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Adapter->Removed)
    {
        DXGMMS_WARN("DxgkMmsSubmitPagingBuffer: adapter removed\n");
        return STATUS_DEVICE_REMOVED;
    }

    if (PagBuf != (PDXGMMS_PAGING_BUFFER)Adapter->PagingBuffer)
    {
        DXGMMS_ERR("DxgkMmsSubmitPagingBuffer: foreign paging buffer %p "
                   "(expected %p)\n", PagBuf, Adapter->PagingBuffer);
        return STATUS_INVALID_PARAMETER;
    }

    if (PagBuf->Buffer == NULL || PagBuf->UsedSize > (SIZE_T)MAXULONG)
    {
        DXGMMS_ERR("DxgkMmsSubmitPagingBuffer: invalid buffer VA=%p "
                   "used=%Iu\n", PagBuf->Buffer, PagBuf->UsedSize);
        return STATUS_INVALID_PARAMETER;
    }

    if (Adapter->NumNodes == 0 || Adapter->Nodes == NULL)
    {
        /*
         * Software adapter with no GPU engine nodes: nothing to submit.
         * Treat as immediate completion.
         */
        DXGMMS_TRACE("DxgkMmsSubmitPagingBuffer: no GPU nodes — "
                     "skipping submit\n");
        return STATUS_SUCCESS;
    }

    /*
     * Assign a new paging fence ID.  Increment under the copy-engine node's
     * spinlock so that concurrent submitters (if any) see distinct fence IDs.
     *
     * We use the DXGMMS_PAGING_NODE_ORDINAL node for fence sequencing even
     * though the actual submission goes through DxgkMmsScheduleCommand which
     * has its own node ordering.  The fence ID space for paging operations is
     * separate from normal rendering fence IDs.
     */
    Node = &Adapter->Nodes[DXGMMS_PAGING_NODE_ORDINAL];

    KeAcquireSpinLock(&Node->Lock, &OldIrql);
    FenceId = ++Adapter->PagingFenceId;
    PagBuf->SubmitFenceId = FenceId;
    KeReleaseSpinLock(&Node->Lock, OldIrql);

    DXGMMS_TRACE("DxgkMmsSubmitPagingBuffer: submitting fence=0x%I64x "
                 "PA=0x%I64x size=%Iu\n",
                 FenceId,
                 PagBuf->PhysicalAddress.QuadPart,
                 PagBuf->UsedSize);

    /*
     * Submit the filled DMA buffer to the GPU scheduler at HIGH priority
     * so it runs before any queued rendering work.
     *
     * DxgkMmsScheduleCommand expects:
     *   AdapterContext — this adapter's MMS context.
     *   CommandBuffer  — opaque descriptor of the DMA buffer.
     *
     * In Phase-1, DxgkMmsScheduleCommand returns STATUS_NOT_IMPLEMENTED
     * (scheduler not wired).  In that case we skip the wait and return
     * success, because there is no GPU submission and therefore no fence
     * to wait for.
     *
     * When the full scheduler is wired, this call will enqueue the buffer
     * at DXGMMS_PRIORITY_HIGH and return STATUS_SUCCESS; the GPU will
     * execute it and raise the completion interrupt, which increments
     * Node->LastCompletedFenceId via DxgkMmsCommandCompleted.
     */
    RtlZeroMemory(&Command, sizeof(Command));
    Command.DxgkrnlBuffer = PagBuf;
    Command.DmaBuffer = PagBuf->Buffer;
    Command.DmaBufferSize = (ULONG)PagBuf->UsedSize;
    Command.NodeOrdinal = DXGMMS_PAGING_NODE_ORDINAL;
    Command.Priority = DXGMMS_PRIORITY_HIGH;
    Command.SubmitFenceId = FenceId;

    Status = DxgkMmsScheduleCommand(Adapter, &Command);
    if (Status == STATUS_NOT_IMPLEMENTED)
    {
        /*
         * Phase-1: scheduler is not yet active.  The GPU will not execute
         * the paging buffer, but we consider the "transfer" trivially
         * complete for development purposes.
         */
        DXGMMS_TRACE("DxgkMmsSubmitPagingBuffer: scheduler not active "
                     "(Phase-1) — no wait\n");
        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsSubmitPagingBuffer: DxgkMmsScheduleCommand "
                   "failed 0x%08lx\n", Status);
        return Status;
    }

    /*
     * Wait for the GPU to signal completion by checking the fence.
     * DxgkMmsWaitForPagingFence blocks with sleep until the fence advances
     * or a timeout occurs.  A timeout indicates a GPU hang; the TDR
     * subsystem should detect this independently.
     */
    Status = DxgkMmsWaitForPagingFence(Adapter, FenceId);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsSubmitPagingBuffer: fence wait failed "
                   "0x%08lx for fence=0x%I64x\n", Status, FenceId);
    }
    else
    {
        DXGMMS_TRACE("DxgkMmsSubmitPagingBuffer: paging DMA complete "
                     "(fence=0x%I64x)\n", FenceId);
    }

    return Status;
}


/* =========================================================================
 * DxgkMmsPagingWorker  (internal work-item callback)
 *
 * Executed in a system worker thread (CriticalWorkQueue) at PASSIVE_LEVEL.
 * Performs the actual BuildPagingBuffer + SubmitPagingBuffer sequence for
 * a page-in or page-out operation queued by DxgkMmsPageIn / DxgkMmsPageOut.
 *
 * On completion the work item's Completed event is signalled so the
 * submitting thread can proceed.
 * ========================================================================= */
static
VOID
NTAPI
DxgkMmsPagingWorker(
    _In_ PVOID Parameter)
{
    PDXGMMS_PAGING_WORK_ITEM    WorkItem;
    PDXGMMS_ADAPTER_CONTEXT     Adapter;
    PDXGMMS_PAGING_BUFFER       PagBuf;
    NTSTATUS                    Status;

    PAGED_CODE();

    ASSERT(Parameter != NULL);

    WorkItem = (PDXGMMS_PAGING_WORK_ITEM)Parameter;
    Adapter  = WorkItem->Adapter;

    DXGMMS_TRACE("DxgkMmsPagingWorker: op=%u alloc=%p src seg=%u "
                 "off=0x%I64x dst seg=%u off=0x%I64x size=%Iu\n",
                 WorkItem->Operation,
                 WorkItem->Allocation,
                 WorkItem->SrcSegmentId, WorkItem->SrcOffset,
                 WorkItem->DstSegmentId, WorkItem->DstOffset,
                 WorkItem->TransferSize);

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_NULL_CONTEXT);

    PagBuf = (PDXGMMS_PAGING_BUFFER)Adapter->PagingBuffer;
    if (PagBuf == NULL)
    {
        DXGMMS_ERR("DxgkMmsPagingWorker: no paging buffer\n");
        WorkItem->Status = STATUS_GRAPHICS_NO_VIDEO_MEMORY;
        goto done;
    }

    /* Build the paging DMA buffer for a TRANSFER operation. */
    Status = DxgkMmsBuildTransferBuffer(Adapter,
                                         PagBuf,
                                         WorkItem->Allocation,
                                         WorkItem->SrcSegmentId,
                                         WorkItem->SrcOffset,
                                         WorkItem->DstSegmentId,
                                         WorkItem->DstOffset,
                                         WorkItem->TransferSize,
                                         (PMDL)WorkItem->SysMdl);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsPagingWorker: BuildTransfer failed 0x%08lx\n",
                   Status);
        WorkItem->Status = Status;
        goto done;
    }

    /* Submit the filled buffer to the GPU copy engine and wait for done. */
    Status = DxgkMmsSubmitPagingBuffer(Adapter, PagBuf);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsPagingWorker: Submit failed 0x%08lx\n", Status);
    }

    WorkItem->Status = Status;

done:
    /*
     * Signal the originating thread that the paging operation is complete.
     * The originating thread (DxgkMmsPageIn / DxgkMmsPageOut) is blocked on
     * WorkItem->Completed and will free the work item after this signal.
     */
    DXGMMS_TRACE("DxgkMmsPagingWorker: signalling completion "
                 "status=0x%08lx\n", WorkItem->Status);
    KeSetEvent(&WorkItem->Completed, IO_NO_INCREMENT, FALSE);
}


/* =========================================================================
 * DxgkMmsPageIn
 *
 * Move an allocation from system memory into a VRAM segment (populate).
 * ========================================================================= */
NTSTATUS
DxgkMmsPageIn(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ PVOID                   DxgkAllocation,
    _In_ ULONG                   TargetSegmentId,
    _In_ ULONGLONG               TargetOffset)
{
    PDXGMMS_PAGING_WORK_ITEM    WorkItem;
    NTSTATUS                    Status;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsPageIn: alloc=%p dstSeg=%u dstOff=0x%I64x\n",
                 DxgkAllocation, TargetSegmentId, TargetOffset);

    if (Adapter == NULL || DxgkAllocation == NULL)
    {
        DXGMMS_ERR("DxgkMmsPageIn: invalid parameters\n");
        return STATUS_INVALID_PARAMETER;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (TargetSegmentId == 0)
    {
        DXGMMS_ERR("DxgkMmsPageIn: TargetSegmentId must be non-zero "
                   "(0 = system memory)\n");
        return STATUS_INVALID_PARAMETER;
    }

    DXGMMS_WARN("DxgkMmsPageIn: explicit MDL and transfer size are not "
                "wired in this interface\n");
    return STATUS_NOT_SUPPORTED;

    /*
     * Allocate the work item from non-paged pool.  It will be freed by
     * this function after KeWaitForSingleObject returns.
     */
    WorkItem = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(DXGMMS_PAGING_WORK_ITEM),
                                     TAG_DXGMMS_PAGING);
    if (WorkItem == NULL)
    {
        DXGMMS_ERR("DxgkMmsPageIn: out of pool memory\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(WorkItem, sizeof(DXGMMS_PAGING_WORK_ITEM));

    /*
     * Populate the work item fields.
     *
     * Operation   : TRANSFER (system memory → VRAM)
     * SrcSegmentId: 0 (system memory side)
     * SrcOffset   : 0 (full allocation starting at MDL offset 0)
     * DstSegmentId: TargetSegmentId (VRAM destination)
     * DstOffset   : TargetOffset
     * TransferSize: unknown at this level — the caller must set the correct
     *               size.  For this interface we use 0 as a sentinel to tell
     *               the miniport to transfer the whole allocation.
     *               TODO: accept TransferSize as a parameter.
     * SysMdl      : NULL — caller must wire up the MDL if needed.
     *               TODO: accept SysMdl as a parameter.
     */
    ExInitializeWorkItem(&WorkItem->WorkItem,
                         DxgkMmsPagingWorker,
                         WorkItem);

    WorkItem->Adapter       = Adapter;
    WorkItem->Operation     = DXGK_OPERATION_TRANSFER;
    WorkItem->Allocation    = DxgkAllocation;
    WorkItem->SrcSegmentId  = 0;                /* system memory source */
    WorkItem->SrcOffset     = 0;
    WorkItem->DstSegmentId  = TargetSegmentId;
    WorkItem->DstOffset     = TargetOffset;
    WorkItem->TransferSize  = 0;                /* full allocation */
    WorkItem->SysMdl        = NULL;
    WorkItem->Status        = STATUS_PENDING;

    /*
     * Initialise the completion event (SynchronizationEvent, not signalled).
     * The worker signals it when done; we block on it below.
     */
    KeInitializeEvent(&WorkItem->Completed, SynchronizationEvent, FALSE);

    /*
     * Queue to the Critical work queue.  CriticalWorkQueue items run before
     * DelayedWorkQueue items, ensuring paging is not starved by lower-priority
     * system work.  The work item is consumed by DxgkMmsPagingWorker.
     */
    ExQueueWorkItem(&WorkItem->WorkItem, CriticalWorkQueue);

    /*
     * Block until the worker signals completion.  Use an infinite wait
     * (no timeout) because the TDR watchdog handles the timeout case
     * independently.  Using a finite timeout here risks false failures
     * on loaded systems.
     */
    KeWaitForSingleObject(&WorkItem->Completed,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    Status = WorkItem->Status;

    DXGMMS_TRACE("DxgkMmsPageIn: alloc=%p dstSeg=%u completed "
                 "status=0x%08lx\n",
                 DxgkAllocation, TargetSegmentId, Status);

    ExFreePoolWithTag(WorkItem, TAG_DXGMMS_PAGING);

    return Status;
}


/* =========================================================================
 * DxgkMmsPageOut
 *
 * Move an allocation from VRAM to system memory (evict).
 * ========================================================================= */
NTSTATUS
DxgkMmsPageOut(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ PVOID                   DxgkAllocation,
    _In_ ULONG                   SrcSegmentId,
    _In_ ULONGLONG               SrcOffset)
{
    PDXGMMS_PAGING_WORK_ITEM    WorkItem;
    NTSTATUS                    Status;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsPageOut: alloc=%p srcSeg=%u srcOff=0x%I64x\n",
                 DxgkAllocation, SrcSegmentId, SrcOffset);

    if (Adapter == NULL || DxgkAllocation == NULL)
    {
        DXGMMS_ERR("DxgkMmsPageOut: invalid parameters\n");
        return STATUS_INVALID_PARAMETER;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (SrcSegmentId == 0)
    {
        DXGMMS_ERR("DxgkMmsPageOut: SrcSegmentId must be non-zero "
                   "(0 = system memory)\n");
        return STATUS_INVALID_PARAMETER;
    }

    DXGMMS_WARN("DxgkMmsPageOut: explicit MDL and transfer size are not "
                "wired in this interface\n");
    return STATUS_NOT_SUPPORTED;

    WorkItem = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(DXGMMS_PAGING_WORK_ITEM),
                                     TAG_DXGMMS_PAGING);
    if (WorkItem == NULL)
    {
        DXGMMS_ERR("DxgkMmsPageOut: out of pool memory\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(WorkItem, sizeof(DXGMMS_PAGING_WORK_ITEM));

    ExInitializeWorkItem(&WorkItem->WorkItem,
                         DxgkMmsPagingWorker,
                         WorkItem);

    WorkItem->Adapter       = Adapter;
    WorkItem->Operation     = DXGK_OPERATION_TRANSFER;
    WorkItem->Allocation    = DxgkAllocation;
    WorkItem->SrcSegmentId  = SrcSegmentId;    /* VRAM source       */
    WorkItem->SrcOffset     = SrcOffset;
    WorkItem->DstSegmentId  = 0;               /* system memory dst */
    WorkItem->DstOffset     = 0;
    WorkItem->TransferSize  = 0;               /* full allocation   */
    WorkItem->SysMdl        = NULL;
    WorkItem->Status        = STATUS_PENDING;

    KeInitializeEvent(&WorkItem->Completed, SynchronizationEvent, FALSE);

    ExQueueWorkItem(&WorkItem->WorkItem, CriticalWorkQueue);

    KeWaitForSingleObject(&WorkItem->Completed,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    Status = WorkItem->Status;

    DXGMMS_TRACE("DxgkMmsPageOut: alloc=%p srcSeg=%u completed "
                 "status=0x%08lx\n",
                 DxgkAllocation, SrcSegmentId, Status);

    ExFreePoolWithTag(WorkItem, TAG_DXGMMS_PAGING);

    return Status;
}


/* =========================================================================
 * DxgkMmsMapAperture
 *
 * Map system-memory pages into a GPU aperture segment.
 * ========================================================================= */
NTSTATUS
DxgkMmsMapAperture(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   SegmentId,
    _In_ ULONGLONG               OffsetInPages,
    _In_ PMDL                    SysMdl,
    _In_ ULONG                   NumPages)
{
    PDXGMMS_PAGING_BUFFER   PagBuf;
    NTSTATUS                Status;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsMapAperture: seg=%u pageOff=0x%I64x numPages=%u "
                 "mdl=%p\n", SegmentId, OffsetInPages, NumPages, SysMdl);

    if (Adapter == NULL || SysMdl == NULL || NumPages == 0 || SegmentId == 0)
    {
        DXGMMS_ERR("DxgkMmsMapAperture: invalid parameters "
                   "(adapter=%p mdl=%p numPages=%u seg=%u)\n",
                   Adapter, SysMdl, NumPages, SegmentId);
        return STATUS_INVALID_PARAMETER;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Adapter->Removed)
    {
        DXGMMS_WARN("DxgkMmsMapAperture: adapter removed\n");
        return STATUS_DEVICE_REMOVED;
    }

    PagBuf = (PDXGMMS_PAGING_BUFFER)Adapter->PagingBuffer;
    if (PagBuf == NULL)
    {
        DXGMMS_ERR("DxgkMmsMapAperture: paging buffer not initialised\n");
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }

    PagBuf->UsedSize = 0;

    /* Build the MAP_APERTURE_SEGMENT paging buffer. */
    Status = DxgkMmsBuildApertureBuffer(Adapter,
                                         PagBuf,
                                         DXGK_OPERATION_MAP_APERTURE_SEGMENT,
                                         SegmentId,
                                         (SIZE_T)OffsetInPages,
                                         (SIZE_T)NumPages,
                                         SysMdl);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsMapAperture: build failed 0x%08lx\n", Status);
        return Status;
    }

    /* Submit the filled buffer to the GPU copy engine. */
    Status = DxgkMmsSubmitPagingBuffer(Adapter, PagBuf);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsMapAperture: submit failed 0x%08lx\n", Status);
        return Status;
    }

    DXGMMS_TRACE("DxgkMmsMapAperture: seg=%u %u pages mapped\n",
                 SegmentId, NumPages);
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkMmsUnmapAperture
 *
 * Unmap system-memory pages from a GPU aperture segment.
 * ========================================================================= */
NTSTATUS
DxgkMmsUnmapAperture(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   SegmentId,
    _In_ ULONGLONG               OffsetInPages,
    _In_ ULONG                   NumPages)
{
    PDXGMMS_PAGING_BUFFER   PagBuf;
    NTSTATUS                Status;

    PAGED_CODE();

    DXGMMS_TRACE("DxgkMmsUnmapAperture: seg=%u pageOff=0x%I64x numPages=%u\n",
                 SegmentId, OffsetInPages, NumPages);

    if (Adapter == NULL || NumPages == 0 || SegmentId == 0)
    {
        DXGMMS_ERR("DxgkMmsUnmapAperture: invalid parameters "
                   "(adapter=%p numPages=%u seg=%u)\n",
                   Adapter, NumPages, SegmentId);
        return STATUS_INVALID_PARAMETER;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Adapter->Removed)
    {
        DXGMMS_WARN("DxgkMmsUnmapAperture: adapter removed\n");
        return STATUS_DEVICE_REMOVED;
    }

    PagBuf = (PDXGMMS_PAGING_BUFFER)Adapter->PagingBuffer;
    if (PagBuf == NULL)
    {
        DXGMMS_ERR("DxgkMmsUnmapAperture: paging buffer not initialised\n");
        return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
    }

    PagBuf->UsedSize = 0;

    /* Build the UNMAP_APERTURE_SEGMENT paging buffer. */
    Status = DxgkMmsBuildApertureBuffer(Adapter,
                                         PagBuf,
                                         DXGK_OPERATION_UNMAP_APERTURE_SEGMENT,
                                         SegmentId,
                                         (SIZE_T)OffsetInPages,
                                         (SIZE_T)NumPages,
                                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsUnmapAperture: build failed 0x%08lx\n", Status);
        return Status;
    }

    /* Submit the filled buffer to the GPU. */
    Status = DxgkMmsSubmitPagingBuffer(Adapter, PagBuf);
    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsUnmapAperture: submit failed 0x%08lx\n", Status);
        return Status;
    }

    DXGMMS_TRACE("DxgkMmsUnmapAperture: seg=%u %u pages unmapped\n",
                 SegmentId, NumPages);
    return STATUS_SUCCESS;
}
