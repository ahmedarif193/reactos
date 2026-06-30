/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video memory manager (segment allocator) — internal header
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * This header is included by vidmm.c and by any other source that needs
 * to manipulate DXGMMS_ALLOCATION_ENTRY objects directly.  It must be
 * included AFTER dxgmms1_private.h because it references DXGMMS_SEGMENT
 * and DXGMMS_ADAPTER_CONTEXT.
 */

#pragma once

#ifndef _DXGMMS1_VIDMM_H_
#define _DXGMMS1_VIDMM_H_

/* =========================================================================
 * PDXGMMS_EVICT_CALLBACK
 *
 * Type of the callback stored in DXGMMS_ADAPTER_CONTEXT::EvictCallback.
 * Called by DxgkMmsEvictLeastRecentlyUsed to ask dxgkrnl to build and
 * submit a paging DMA buffer that moves an allocation out of GPU memory.
 *
 * Parameters:
 *   DxgkrnlContext   — opaque context registered via DxgkMmsAddAdapter
 *   AllocationHandle — dxgkrnl allocation handle (from DXGMMS_ALLOCATION_ENTRY)
 *
 * Return value:
 *   STATUS_SUCCESS if the eviction was submitted successfully.
 *   Any failure status leaves the allocation in place; the caller will
 *   report STATUS_NO_MEMORY to the segment allocation path.
 *
 * IRQL: called at PASSIVE_LEVEL (DxgkMmsEvictLeastRecentlyUsed always runs
 *       at PASSIVE_LEVEL so the paging engine can block on I/O).
 * ========================================================================= */
typedef NTSTATUS
(NTAPI *PDXGMMS_EVICT_CALLBACK)(
    _In_ PVOID DxgkrnlContext,
    _In_ PVOID AllocationHandle);

/* =========================================================================
 * DXGMMS_ALLOCATION_ENTRY
 *
 * Tracks one allocation that is currently resident in a GPU memory segment.
 *
 * An entry is created by DxgkMmsAllocateFromSegment (or by the paging engine
 * when it moves an allocation into a segment) and destroyed when the
 * allocation is evicted or freed via DxgkMmsFreeFromSegment.
 *
 * Locking discipline:
 *   SegmentEntry   — protected by the owning DXGMMS_SEGMENT::Lock
 *   GlobalEntry    — protected by DXGMMS_ADAPTER_CONTEXT::AdapterLock
 *   All read-only fields after construction (SegmentId, SegmentOffset,
 *   AllocationSize, AllocationHandle) need no further locking once written.
 *   Priority and LastUseTimestamp are written under DXGMMS_SEGMENT::Lock.
 * ========================================================================= */
typedef struct _DXGMMS_ALLOCATION_ENTRY
{
    /*
     * SegmentEntry: links this entry into DXGMMS_SEGMENT::AllocationList.
     * Protected by DXGMMS_SEGMENT::Lock.
     */
    LIST_ENTRY  SegmentEntry;

    /*
     * GlobalEntry: links this entry into
     * DXGMMS_ADAPTER_CONTEXT::GlobalAllocationList.
     * Protected by DXGMMS_ADAPTER_CONTEXT::AdapterLock.
     */
    LIST_ENTRY  GlobalEntry;

    /*
     * AllocationHandle: opaque handle passed to us by dxgkrnl that
     * uniquely identifies this allocation within the dxgkrnl allocation
     * subsystem.  Passed back in EvictCallback.
     */
    PVOID       AllocationHandle;

    /* Index of the segment in which this allocation resides. */
    ULONG       SegmentId;

    /* Byte offset from the segment base of the allocation start. */
    ULONGLONG   SegmentOffset;

    /* Byte size of the allocation (page-aligned). */
    SIZE_T      AllocationSize;

    /*
     * Priority: scheduling priority at which this allocation was made.
     * Lower numeric value = higher eviction priority (evict low-priority
     * work first).  Maps to the DXGMMS_PRIORITY_* constants defined in
     * dxgmms1_private.h.
     */
    ULONG       Priority;

    /*
     * LastUseTimestamp: value of KeQueryInterruptTime() at the moment
     * this allocation was last accessed (submitted in a command buffer).
     * Used by LRU eviction when priorities are equal: older (smaller
     * timestamp) allocations are evicted first.
     */
    ULONGLONG   LastUseTimestamp;

} DXGMMS_ALLOCATION_ENTRY, *PDXGMMS_ALLOCATION_ENTRY;

/* =========================================================================
 * Public API — implemented in vidmm.c
 *
 * The three NTAPI functions (InitializeSegment, AllocateFromSegment,
 * FreeFromSegment) are also declared in dxgmms1_private.h because they
 * are registered in the DXGMMS_INTERFACE dispatch table.  The three
 * helper functions below are only called from within dxgmms1.sys itself.
 * ========================================================================= */

/*
 * DxgkMmsGetSegmentInfo
 *
 * Return a consistent snapshot of (AllocatedSize, TotalSize) for the
 * given segment.  Acquires Segment->Lock briefly to avoid torn reads.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
VOID
DxgkMmsGetSegmentInfo(
    _In_  PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_  ULONG                   SegmentId,
    _Out_ PULONGLONG               OutAllocated,
    _Out_ PULONGLONG               OutTotal);

/*
 * DxgkMmsEvictLeastRecentlyUsed
 *
 * Select the allocation from Segment->AllocationList with the lowest
 * Priority (and, as a tiebreaker, the smallest LastUseTimestamp), remove
 * it from the segment and from the adapter-wide GlobalAllocationList, then
 * invoke the EvictCallback (if set) or perform a soft eviction.
 *
 * Returns STATUS_SUCCESS if a victim was found and evicted.
 * Returns STATUS_NOT_FOUND if the segment has no resident allocations.
 *
 * IRQL: PASSIVE_LEVEL (the EvictCallback may build/submit a paging buffer)
 */
NTSTATUS
DxgkMmsEvictLeastRecentlyUsed(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   SegmentId);

/*
 * DxgkMmsGetApertureSegmentPageArray
 *
 * Return a pointer into the AperturePages PFN array for the specified
 * aperture segment, starting at the page that covers byte offset Offset.
 *
 * On success *OutPageArray points to &Seg->AperturePages[Offset/PAGE_SIZE]
 * and *OutNumPages is filled with the number of pages from that point to
 * the end of the segment.
 *
 * Returns STATUS_INVALID_PARAMETER if the segment is not an aperture segment,
 * or if Offset is not page-aligned, or if Offset >= TotalSize.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
NTSTATUS
DxgkMmsGetApertureSegmentPageArray(
    _In_  PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_  ULONG                   SegmentId,
    _In_  ULONGLONG               Offset,
    _Out_ PPFN_NUMBER            *OutPageArray,
    _Out_ PULONG                  OutNumPages);

#endif /* _DXGMMS1_VIDMM_H_ */
