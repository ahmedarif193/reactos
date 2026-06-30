/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU video memory (segment) allocator for dxgmms1.sys
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * This file implements the heap within each GPU memory segment:
 *
 *   DxgkMmsInitializeSegment        — record segment geometry; build initial
 *                                     free block; allocate aperture PFN array
 *   DxgkMmsAllocateFromSegment      — first-fit + alignment; O(n) scan
 *   DxgkMmsFreeFromSegment          — sorted insert + coalesce; O(n) walk
 *   DxgkMmsGetSegmentInfo           — atomic snapshot of (Allocated,Total)
 *   DxgkMmsEvictLeastRecentlyUsed   — LRU victim selection + eviction
 *   DxgkMmsGetApertureSegmentPageArray — aperture PFN slice pointer
 *
 * Locking
 * -------
 * The free list and AllocationList for each segment are protected by
 * DXGMMS_SEGMENT::Lock, a KSPIN_LOCK acquired at DISPATCH_LEVEL.
 * This allows DxgkMmsFreeFromSegment to be called from a DPC (e.g. from
 * the GPU completion interrupt handler at DISPATCH_LEVEL).
 *
 * Because ExAllocatePoolWithTag cannot be called while a KSPIN_LOCK is
 * held at DISPATCH_LEVEL in a context that might be paged (the calling
 * thread may be paged), all pool allocations that are needed inside the
 * spinlock region are pre-allocated BEFORE the lock is acquired and
 * freed (if unused) AFTER the lock is released.
 *
 * Algorithm
 * ---------
 * The free list is a singly-linked LIST_ENTRY chain stored in ascending
 * order of offset.  Allocation uses first-fit: the scanner stops at the
 * first free block that can satisfy (size + alignment padding).
 *
 * On allocation:
 *   aligned_start = ALIGN_UP(block->Offset, Alignment)
 *   prefix        = aligned_start - block->Offset   (wasted leading bytes)
 *   suffix        = block->Size - prefix - Size      (trailing free bytes)
 *
 *   Three cases:
 *     a) prefix == 0 && suffix == 0: remove block entirely.
 *     b) prefix == 0 && suffix > 0: reuse block in-place (shift Offset, shrink Size).
 *     c) prefix > 0:                shrink block to [Offset, prefix);
 *                                   if suffix > 0, insert new block for suffix.
 *
 *   A SuffixBlock is pre-allocated before the spinlock for case (c).
 *
 * On free:
 *   Walk the sorted list to find the insertion point.
 *   Coalesce with predecessor if adjacent.
 *   Coalesce with successor if adjacent.
 *   A NewBlock is pre-allocated before the spinlock in case no coalescing
 *   with the predecessor occurs and a new node must be inserted.
 *
 * x86/amd64 notes
 * ---------------
 * On x86-64 (TSO memory model), plain ULONG64 stores/loads to naturally
 * aligned locations are effectively acquire/release.  We still use
 * InterlockedAdd64 for AllocatedSize updates to guarantee atomicity
 * on non-TSO paths and to serve as a compiler barrier.
 */

#include "dxgmms1_private.h"
#include "vidmm.h"

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/*
 * DxgkMmspAlignUp
 *
 * Round Value up to the next multiple of Alignment.
 * Alignment MUST be a power of two (enforced by callers).
 */
FORCEINLINE ULONGLONG
DxgkMmspAlignUp(
    _In_ ULONGLONG Value,
    _In_ SIZE_T    Alignment)
{
    ULONGLONG Mask = (ULONGLONG)(Alignment) - 1ULL;
    return (Value + Mask) & ~Mask;
}

FORCEINLINE BOOLEAN
DxgkMmspIsPowerOfTwo(
    _In_ SIZE_T Value)
{
    return (Value != 0) && ((Value & (Value - 1)) == 0);
}

/*
 * DxgkMmspFindSegment
 *
 * Look up the DXGMMS_SEGMENT for (Adapter, SegmentId).
 * Returns NULL and logs a warning if SegmentId is out of range.
 *
 * Called at IRQL <= DISPATCH_LEVEL; no locks held on entry.
 */
FORCEINLINE PDXGMMS_SEGMENT
DxgkMmspFindSegment(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   SegmentId)
{
    if (SegmentId >= Adapter->NumSegments)
    {
        DXGMMS_WARN("DxgkMmspFindSegment: SegmentId %u out of range "
                    "(NumSegments=%u)\n", SegmentId, Adapter->NumSegments);
        return NULL;
    }
    return &Adapter->Segments[SegmentId];
}

/* =========================================================================
 * DxgkMmsInitializeSegment
 *
 * Record the geometry of a GPU memory segment and build the initial
 * single free block that covers the entire segment.  For aperture
 * segments the PFN array is also allocated here.
 *
 * Called by dxgkrnl at adapter start time.
 * IRQL: PASSIVE_LEVEL
 * ========================================================================= */
NTSTATUS
NTAPI
DxgkMmsInitializeSegment(
    _In_ PVOID            AdapterContext,
    _In_ ULONG            SegmentId,
    _In_ ULONGLONG        Size,
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONG            Flags)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;
    PDXGMMS_SEGMENT         Seg;
    PDXGMMS_FREE_BLOCK      InitialBlock;
    PPFN_NUMBER             AperturePages = NULL;
    ULONG                   NumAperturePages = 0;

    PAGED_CODE();

    if (Adapter == NULL)
    {
        DXGMMS_ERR("DxgkMmsInitializeSegment: NULL adapter context\n");
        return STATUS_INVALID_PARAMETER;
    }

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Size == 0)
    {
        DXGMMS_ERR("DxgkMmsInitializeSegment: zero-size segment\n");
        return STATUS_INVALID_PARAMETER;
    }

    Seg = DxgkMmspFindSegment(Adapter, SegmentId);
    if (Seg == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Seg->TotalSize != 0 || !IsListEmpty(&Seg->FreeListHead))
    {
        DXGMMS_WARN("DxgkMmsInitializeSegment: segment %u already "
                    "initialised\n", SegmentId);
        return STATUS_INVALID_DEVICE_STATE;
    }

    DXGMMS_TRACE("DxgkMmsInitializeSegment: seg=%u size=0x%I64x "
                 "base=0x%I64x flags=0x%08x\n",
                 SegmentId, Size, BaseAddress.QuadPart, Flags);

    /*
     * For aperture segments, allocate the PFN array up front before we
     * modify any segment state.  This way a failure is clean: the segment
     * remains uninitialised.
     *
     * Size is required to be a multiple of PAGE_SIZE for aperture segments.
     */
    if (Flags & DXGMMS_SEGMENT_FLAG_APERTURE)
    {
        if ((Size & (PAGE_SIZE - 1)) != 0)
        {
            DXGMMS_ERR("DxgkMmsInitializeSegment: aperture segment "
                       "size 0x%I64x is not page-aligned\n", Size);
            return STATUS_INVALID_PARAMETER;
        }

        if ((Size / PAGE_SIZE) > MAXULONG ||
            (Size / PAGE_SIZE) > ((SIZE_T)-1 / sizeof(PFN_NUMBER)))
        {
            DXGMMS_ERR("DxgkMmsInitializeSegment: aperture segment "
                       "page count overflow for size 0x%I64x\n", Size);
            return STATUS_INVALID_PARAMETER;
        }

        NumAperturePages = (ULONG)(Size / PAGE_SIZE);
        if (NumAperturePages == 0)
        {
            DXGMMS_ERR("DxgkMmsInitializeSegment: aperture segment "
                       "size 0x%I64x smaller than one page\n", Size);
            return STATUS_INVALID_PARAMETER;
        }

        AperturePages = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(PFN_NUMBER) * NumAperturePages,
                                              DXGMMS1_POOL_TAG);
        if (AperturePages == NULL)
        {
            DXGMMS_ERR("DxgkMmsInitializeSegment: failed to allocate "
                       "aperture PFN array (%u pages)\n", NumAperturePages);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(AperturePages, sizeof(PFN_NUMBER) * NumAperturePages);

        DXGMMS_TRACE("DxgkMmsInitializeSegment: aperture PFN array "
                     "at %p (%u pages)\n", AperturePages, NumAperturePages);
    }

    /*
     * Allocate the single initial free block that covers the whole segment.
     * Allocation cannot fail at PASSIVE_LEVEL for NonPagedPool of this size;
     * treat failure as a hard error and release the aperture array.
     */
    InitialBlock = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(DXGMMS_FREE_BLOCK),
                                         DXGMMS1_POOL_TAG);
    if (InitialBlock == NULL)
    {
        DXGMMS_ERR("DxgkMmsInitializeSegment: failed to allocate "
                   "initial free block for segment %u\n", SegmentId);
        if (AperturePages != NULL)
            ExFreePoolWithTag(AperturePages, DXGMMS1_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    InitialBlock->Offset = 0;
    InitialBlock->Size   = Size;

    /*
     * Populate the segment descriptor.  The spinlock and list heads were
     * already initialised in DxgkMmsAddAdapter; we only fill in the
     * geometry fields here.
     *
     * We do NOT hold Seg->Lock while writing these fields because
     * DxgkMmsInitializeSegment is called exactly once per segment at
     * PASSIVE_LEVEL before any allocations can occur.
     */
    Seg->BaseAddress      = BaseAddress;
    Seg->TotalSize        = Size;
    Seg->AllocatedSize    = 0;
    Seg->Flags            = Flags;
    Seg->AperturePages    = AperturePages;
    Seg->NumAperturePages = NumAperturePages;

    /*
     * Insert the initial free block.  The list was initialised to empty
     * by DxgkMmsAddAdapter; InsertTailList makes it the sole entry.
     */
    InsertTailList(&Seg->FreeListHead, &InitialBlock->ListEntry);

    DXGMMS_TRACE("DxgkMmsInitializeSegment: segment %u ready "
                 "(total=0x%I64x, aperture=%s)\n",
                 SegmentId, Size,
                 (Flags & DXGMMS_SEGMENT_FLAG_APERTURE) ? "yes" : "no");

    return STATUS_SUCCESS;
}

/* =========================================================================
 * DxgkMmsAllocateFromSegment
 *
 * First-fit scan of the free list with alignment support.
 *
 * Pre-allocates a SuffixBlock from NonPagedPool BEFORE acquiring the
 * segment spinlock so that the allocation path is safe at DISPATCH_LEVEL.
 * If the SuffixBlock turns out not to be needed it is freed after the
 * spinlock is released.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ========================================================================= */
NTSTATUS
NTAPI
DxgkMmsAllocateFromSegment(
    _In_  PVOID      AdapterContext,
    _In_  ULONG      SegmentId,
    _In_  SIZE_T     Size,
    _In_  SIZE_T     Alignment,
    _Out_ PULONGLONG OutOffset)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;
    PDXGMMS_SEGMENT         Seg;
    PDXGMMS_FREE_BLOCK      SuffixBlock;
    PLIST_ENTRY             Entry;
    PDXGMMS_FREE_BLOCK      Block;
    ULONGLONG               AlignedStart;
    ULONGLONG               Prefix;
    ULONGLONG               Suffix;
    BOOLEAN                 Found = FALSE;
    ULONGLONG               AllocOffset = 0;
    KIRQL                   OldIrql;

    if (Adapter == NULL || OutOffset == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutOffset = 0;

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Adapter->Removed)
        return STATUS_DEVICE_REMOVED;

    if (Size == 0)
        return STATUS_INVALID_PARAMETER;

    /* Clamp Alignment to at least 1 and force power-of-two. */
    if (Alignment == 0)
        Alignment = 1;

    if (!DxgkMmspIsPowerOfTwo(Alignment))
        return STATUS_INVALID_PARAMETER;

    Seg = DxgkMmspFindSegment(Adapter, SegmentId);
    if (Seg == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Seg->TotalSize == 0)
        return STATUS_INVALID_DEVICE_STATE;

    /*
     * Pre-allocate a free block that may be needed to represent the
     * trailing suffix after the allocation.  Allocated BEFORE the spinlock
     * so that ExAllocatePoolWithTag is not called while the lock is held.
     * If unused, freed below.
     */
    SuffixBlock = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(DXGMMS_FREE_BLOCK),
                                        DXGMMS1_POOL_TAG);
    if (SuffixBlock == NULL)
    {
        DXGMMS_WARN("DxgkMmsAllocateFromSegment: failed to pre-allocate "
                    "suffix block for seg=%u\n", SegmentId);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeAcquireSpinLock(&Seg->Lock, &OldIrql);

    for (Entry = Seg->FreeListHead.Flink;
         Entry != &Seg->FreeListHead;
         Entry = Entry->Flink)
    {
        Block = CONTAINING_RECORD(Entry, DXGMMS_FREE_BLOCK, ListEntry);

        /* Compute the aligned start address within this block. */
        if (Block->Offset > ((ULONGLONG)-1) - ((ULONGLONG)Alignment - 1ULL))
            continue;

        AlignedStart = DxgkMmspAlignUp(Block->Offset, Alignment);
        Prefix       = AlignedStart - Block->Offset;

        /* Does the block have enough room after alignment padding? */
        if (Prefix > Block->Size ||
            (ULONGLONG)Size > Block->Size - Prefix)
            continue;

        Suffix = Block->Size - Prefix - (ULONGLONG)Size;

        AllocOffset = AlignedStart;
        Found       = TRUE;

        if (Prefix == 0)
        {
            /*
             * Case (b)/(a): no leading waste.
             *   - If Suffix > 0: reuse the block in-place by advancing
             *     its Offset and shrinking its Size.
             *   - If Suffix == 0: the block is consumed entirely; remove it.
             */
            if (Suffix > 0)
            {
                Block->Offset += (ULONGLONG)Size;
                Block->Size    = Suffix;
            }
            else
            {
                RemoveEntryList(&Block->ListEntry);
                ExFreePoolWithTag(Block, DXGMMS1_POOL_TAG);
                Block = NULL;
            }
            /* SuffixBlock was not needed in either sub-case. */
        }
        else
        {
            /*
             * Case (c): leading prefix waste.
             * Shrink the existing block to [Block->Offset, Prefix).
             */
            Block->Size = Prefix;

            if (Suffix > 0)
            {
                /*
                 * There is also trailing waste.  Insert the pre-allocated
                 * SuffixBlock immediately after Block in the sorted list.
                 */
                SuffixBlock->Offset = AlignedStart + (ULONGLONG)Size;
                SuffixBlock->Size   = Suffix;
                InsertHeadList(Entry, &SuffixBlock->ListEntry);
                SuffixBlock = NULL; /* Ownership transferred; do not free. */
            }
        }

        /*
         * Update the segment's accounting.  On amd64 a naturally aligned
         * ULONG64 write is atomic, but we use InterlockedAdd64 as a
         * compiler barrier and for correctness on all targets.
         */
        InterlockedAdd64((volatile LONG64 *)&Seg->AllocatedSize, (LONG64)Size);

        break;
    }

    KeReleaseSpinLock(&Seg->Lock, OldIrql);

    /* Release the pre-allocated suffix block if it was not consumed. */
    if (SuffixBlock != NULL)
        ExFreePoolWithTag(SuffixBlock, DXGMMS1_POOL_TAG);

    if (!Found)
    {
        DXGMMS_WARN("DxgkMmsAllocateFromSegment: seg=%u no free block "
                    "for size=0x%Ix align=0x%Ix\n", SegmentId, Size, Alignment);
        return STATUS_NO_MEMORY;
    }

    DXGMMS_TRACE("DxgkMmsAllocateFromSegment: seg=%u offset=0x%I64x "
                 "size=0x%Ix align=0x%Ix\n",
                 SegmentId, AllocOffset, Size, Alignment);

    *OutOffset = AllocOffset;
    return STATUS_SUCCESS;
}

/* =========================================================================
 * DxgkMmsFreeFromSegment
 *
 * Insert a free block in sorted order and coalesce with neighbours.
 *
 * Like the allocator, we pre-allocate a NewBlock from NonPagedPool BEFORE
 * acquiring the spinlock.  If coalescing with the predecessor node absorbs
 * the freed range, NewBlock is freed after the spinlock is released.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ========================================================================= */
VOID
NTAPI
DxgkMmsFreeFromSegment(
    _In_ PVOID     AdapterContext,
    _In_ ULONG     SegmentId,
    _In_ ULONGLONG Offset,
    _In_ SIZE_T    Size)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;
    PDXGMMS_SEGMENT         Seg;
    PDXGMMS_FREE_BLOCK      NewBlock;
    PLIST_ENTRY             Entry;
    PDXGMMS_FREE_BLOCK      Cursor;
    PDXGMMS_FREE_BLOCK      Predecessor = NULL;
    PDXGMMS_FREE_BLOCK      Successor   = NULL;
    PLIST_ENTRY             InsertBefore;
    KIRQL                   OldIrql;
    ULONGLONG               EndOffset;

    if (Adapter == NULL)
        return;

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (Size == 0)
        return;

    Seg = DxgkMmspFindSegment(Adapter, SegmentId);
    if (Seg == NULL)
        return;

    if (Seg->TotalSize == 0 ||
        Offset > ((ULONGLONG)-1) - (ULONGLONG)Size ||
        Offset >= Seg->TotalSize ||
        (ULONGLONG)Size > Seg->TotalSize - Offset)
    {
        DXGMMS_WARN("DxgkMmsFreeFromSegment: invalid range seg=%u "
                    "offset=0x%I64x size=0x%Ix total=0x%I64x\n",
                    SegmentId, Offset, Size, Seg->TotalSize);
        return;
    }

    EndOffset = Offset + (ULONGLONG)Size;

    DXGMMS_TRACE("DxgkMmsFreeFromSegment: seg=%u offset=0x%I64x size=0x%Ix\n",
                 SegmentId, Offset, Size);

    /*
     * Pre-allocate a new free block node in case we cannot coalesce with
     * an existing predecessor block.
     */
    NewBlock = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(DXGMMS_FREE_BLOCK),
                                     DXGMMS1_POOL_TAG);
    if (NewBlock == NULL)
    {
        /*
         * Allocation failure on free is a non-fatal leak: the memory is
         * simply lost until the segment is torn down.  Log a warning and
         * update accounting so pressure is recorded correctly.
         */
        DXGMMS_WARN("DxgkMmsFreeFromSegment: NewBlock allocation failed "
                    "seg=%u offset=0x%I64x size=0x%Ix — memory leaked\n",
                    SegmentId, Offset, Size);
        InterlockedAdd64((volatile LONG64 *)&Seg->AllocatedSize, -(LONG64)Size);
        return;
    }

    KeAcquireSpinLock(&Seg->Lock, &OldIrql);

    if (Seg->AllocatedSize < (ULONGLONG)Size)
    {
        KeReleaseSpinLock(&Seg->Lock, OldIrql);
        DXGMMS_WARN("DxgkMmsFreeFromSegment: accounting underflow seg=%u "
                    "allocated=0x%I64x free=0x%Ix\n",
                    SegmentId, Seg->AllocatedSize, Size);
        ExFreePoolWithTag(NewBlock, DXGMMS1_POOL_TAG);
        return;
    }

    /*
     * Update accounting before the sorted insert so that any concurrent
     * reader of AllocatedSize sees a coherent (never over-counted) value
     * even if we release the lock before completing the list manipulation.
     */
    InterlockedAdd64((volatile LONG64 *)&Seg->AllocatedSize, -(LONG64)Size);

    /*
     * Walk the sorted free list to find the insertion point.
     * We want InsertBefore to point to the first block with Offset > freed.
     * Predecessor is the last block with Offset < freed.
     */
    InsertBefore = &Seg->FreeListHead; /* default: insert at tail */

    for (Entry = Seg->FreeListHead.Flink;
         Entry != &Seg->FreeListHead;
         Entry = Entry->Flink)
    {
        Cursor = CONTAINING_RECORD(Entry, DXGMMS_FREE_BLOCK, ListEntry);

        if (Offset < Cursor->Offset + Cursor->Size &&
            EndOffset > Cursor->Offset)
        {
            KeReleaseSpinLock(&Seg->Lock, OldIrql);
            DXGMMS_WARN("DxgkMmsFreeFromSegment: overlapping free seg=%u "
                        "offset=0x%I64x size=0x%Ix with block "
                        "[0x%I64x, 0x%I64x)\n",
                        SegmentId, Offset, Size, Cursor->Offset,
                        Cursor->Offset + Cursor->Size);
            ExFreePoolWithTag(NewBlock, DXGMMS1_POOL_TAG);
            return;
        }

        if (Cursor->Offset > Offset)
        {
            /*
             * Cursor is the first block that starts after the freed range.
             * Predecessor (if any) is the block just before this one.
             */
            InsertBefore = Entry;

            if (Entry->Blink != &Seg->FreeListHead)
                Predecessor = CONTAINING_RECORD(Entry->Blink,
                                                DXGMMS_FREE_BLOCK,
                                                ListEntry);
            Successor = Cursor;
            break;
        }
        else
        {
            /* Still before or at the freed range. */
            Predecessor = Cursor;
        }
    }

    /*
     * Check whether we can coalesce with the predecessor.
     *
     * Adjacent means: Predecessor->Offset + Predecessor->Size == Offset.
     * If so, extend the predecessor to swallow the freed range and
     * potentially the successor too.  NewBlock is then not needed.
     */
    if (Predecessor != NULL &&
        (Predecessor->Offset + Predecessor->Size == Offset))
    {
        /* Extend predecessor. */
        Predecessor->Size += (ULONGLONG)Size;

        /*
         * Check whether the extended predecessor is now adjacent to
         * the successor.
         */
        if (Successor != NULL &&
            (Predecessor->Offset + Predecessor->Size == Successor->Offset))
        {
            Predecessor->Size += Successor->Size;
            RemoveEntryList(&Successor->ListEntry);
            ExFreePoolWithTag(Successor, DXGMMS1_POOL_TAG);
        }

        /* NewBlock was not used. */
    }
    else
    {
        /*
         * Cannot coalesce with predecessor.  Use NewBlock to insert the
         * freed range into the sorted list.
         */
        NewBlock->Offset = Offset;
        NewBlock->Size   = (ULONGLONG)Size;
        InsertTailList(InsertBefore, &NewBlock->ListEntry);
        NewBlock = NULL; /* Ownership transferred; do not free. */

        /*
         * Check whether the new block is adjacent to the successor.
         */
        if (Successor != NULL)
        {
            PDXGMMS_FREE_BLOCK Inserted =
                CONTAINING_RECORD(InsertBefore->Blink,
                                  DXGMMS_FREE_BLOCK,
                                  ListEntry);
            if (Inserted->Offset + Inserted->Size == Successor->Offset)
            {
                Inserted->Size += Successor->Size;
                RemoveEntryList(&Successor->ListEntry);
                ExFreePoolWithTag(Successor, DXGMMS1_POOL_TAG);
            }
        }
    }

    KeReleaseSpinLock(&Seg->Lock, OldIrql);

    /* Release the pre-allocated block if it was not consumed. */
    if (NewBlock != NULL)
        ExFreePoolWithTag(NewBlock, DXGMMS1_POOL_TAG);
}

/* =========================================================================
 * DxgkMmsGetSegmentInfo
 *
 * Return a consistent (AllocatedSize, TotalSize) snapshot.
 * Acquires Seg->Lock briefly so the two values are read atomically
 * with respect to concurrent AllocateFromSegment / FreeFromSegment calls.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ========================================================================= */
VOID
DxgkMmsGetSegmentInfo(
    _In_  PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_  ULONG                   SegmentId,
    _Out_ PULONGLONG               OutAllocated,
    _Out_ PULONGLONG               OutTotal)
{
    PDXGMMS_SEGMENT Seg;
    KIRQL           OldIrql;

    ASSERT(Adapter != NULL && OutAllocated != NULL && OutTotal != NULL);

    *OutAllocated = 0;
    *OutTotal     = 0;

    Seg = DxgkMmspFindSegment(Adapter, SegmentId);
    if (Seg == NULL)
        return;

    KeAcquireSpinLock(&Seg->Lock, &OldIrql);
    *OutAllocated = Seg->AllocatedSize;
    *OutTotal     = Seg->TotalSize;
    KeReleaseSpinLock(&Seg->Lock, OldIrql);
}

/* =========================================================================
 * DxgkMmsEvictLeastRecentlyUsed
 *
 * Scan Segment->AllocationList under Seg->Lock to find the victim with
 * the lowest Priority.  When multiple candidates share the same priority,
 * the one with the smallest LastUseTimestamp (oldest) is chosen.
 *
 * After selecting the victim:
 *   1. Remove it from Seg->AllocationList (Seg->Lock held).
 *   2. Remove it from Adapter->GlobalAllocationList (AdapterLock).
 *   3. Release both locks.
 *   4. Call Adapter->EvictCallback or perform a soft eviction.
 *   5. Free the DXGMMS_ALLOCATION_ENTRY pool allocation.
 *
 * IRQL: PASSIVE_LEVEL
 * ========================================================================= */
NTSTATUS
DxgkMmsEvictLeastRecentlyUsed(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   SegmentId)
{
    PDXGMMS_SEGMENT         Seg;
    PDXGMMS_ALLOCATION_ENTRY Victim = NULL;
    PLIST_ENTRY             Entry;
    PDXGMMS_ALLOCATION_ENTRY Candidate;
    KIRQL                   OldIrql;
    KIRQL                   AdapterIrql;
    NTSTATUS                Status;

    PAGED_CODE();

    ASSERT(Adapter != NULL);

    Seg = DxgkMmspFindSegment(Adapter, SegmentId);
    if (Seg == NULL)
        return STATUS_INVALID_PARAMETER;

    /* --- Phase 1: select victim under Seg->Lock --- */
    KeAcquireSpinLock(&Seg->Lock, &OldIrql);

    for (Entry = Seg->AllocationList.Flink;
         Entry != &Seg->AllocationList;
         Entry = Entry->Flink)
    {
        Candidate = CONTAINING_RECORD(Entry, DXGMMS_ALLOCATION_ENTRY, SegmentEntry);

        if (Victim == NULL)
        {
            Victim = Candidate;
            continue;
        }

        /*
         * Lower Priority value means lower priority (higher eviction
         * preference).  Break ties with LRU (smaller timestamp = older).
         */
        if (Candidate->Priority < Victim->Priority ||
            (Candidate->Priority == Victim->Priority &&
             Candidate->LastUseTimestamp < Victim->LastUseTimestamp))
        {
            Victim = Candidate;
        }
    }

    if (Victim == NULL)
    {
        KeReleaseSpinLock(&Seg->Lock, OldIrql);
        DXGMMS_WARN("DxgkMmsEvictLeastRecentlyUsed: seg=%u is empty\n",
                    SegmentId);
        return STATUS_NOT_FOUND;
    }

    /* Remove from per-segment list. */
    RemoveEntryList(&Victim->SegmentEntry);

    KeReleaseSpinLock(&Seg->Lock, OldIrql);

    /* --- Phase 2: remove from global list under AdapterLock --- */
    KeAcquireSpinLock(&Adapter->AdapterLock, &AdapterIrql);
    RemoveEntryList(&Victim->GlobalEntry);
    KeReleaseSpinLock(&Adapter->AdapterLock, AdapterIrql);

    DXGMMS_TRACE("DxgkMmsEvictLeastRecentlyUsed: evicting seg=%u "
                 "offset=0x%I64x size=0x%Ix priority=%u ts=0x%I64x\n",
                 SegmentId, Victim->SegmentOffset, Victim->AllocationSize,
                 Victim->Priority, Victim->LastUseTimestamp);

    /* --- Phase 3: perform the eviction --- */
    if (Adapter->EvictCallback != NULL)
    {
        PDXGMMS_EVICT_CALLBACK Cb = (PDXGMMS_EVICT_CALLBACK)Adapter->EvictCallback;

        Status = Cb(Adapter->DxgkrnlContext, Victim->AllocationHandle);
        if (!NT_SUCCESS(Status))
        {
            DXGMMS_WARN("DxgkMmsEvictLeastRecentlyUsed: EvictCallback "
                        "failed 0x%08lx for handle=%p\n",
                        Status, Victim->AllocationHandle);
            /*
             * Even if the eviction callback fails, we already removed the
             * entry from our lists.  Do not re-insert it; the caller will
             * propagate the error and dxgkrnl is expected to clean up.
             */
        }
    }
    else
    {
        /*
         * No eviction callback: perform a soft eviction by simply
         * releasing the segment address range back to the free list.
         * This is used in Phase-1 where dxgkrnl has not yet wired up
         * the paging engine.
         */
        DXGMMS_TRACE("DxgkMmsEvictLeastRecentlyUsed: no EvictCallback — "
                     "soft-evicting seg=%u offset=0x%I64x size=0x%Ix\n",
                     SegmentId, Victim->SegmentOffset, Victim->AllocationSize);

        DxgkMmsFreeFromSegment(Adapter, SegmentId,
                                Victim->SegmentOffset, Victim->AllocationSize);
        Status = STATUS_SUCCESS;
    }

    ExFreePoolWithTag(Victim, DXGMMS1_POOL_TAG);

    return Status;
}

/* =========================================================================
 * DxgkMmsGetApertureSegmentPageArray
 *
 * Return a pointer into the aperture PFN array at the page corresponding
 * to byte offset Offset.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ========================================================================= */
NTSTATUS
DxgkMmsGetApertureSegmentPageArray(
    _In_  PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_  ULONG                   SegmentId,
    _In_  ULONGLONG               Offset,
    _Out_ PPFN_NUMBER            *OutPageArray,
    _Out_ PULONG                  OutNumPages)
{
    PDXGMMS_SEGMENT Seg;
    ULONG           FirstPageIndex;

    ASSERT(Adapter != NULL && OutPageArray != NULL && OutNumPages != NULL);

    *OutPageArray = NULL;
    *OutNumPages  = 0;

    Seg = DxgkMmspFindSegment(Adapter, SegmentId);
    if (Seg == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!(Seg->Flags & DXGMMS_SEGMENT_FLAG_APERTURE))
    {
        DXGMMS_WARN("DxgkMmsGetApertureSegmentPageArray: seg=%u is not "
                    "an aperture segment\n", SegmentId);
        return STATUS_INVALID_PARAMETER;
    }

    /* Offset must be page-aligned. */
    if ((Offset & (PAGE_SIZE - 1)) != 0)
    {
        DXGMMS_WARN("DxgkMmsGetApertureSegmentPageArray: seg=%u offset "
                    "0x%I64x is not page-aligned\n", SegmentId, Offset);
        return STATUS_INVALID_PARAMETER;
    }

    if (Offset >= Seg->TotalSize)
    {
        DXGMMS_WARN("DxgkMmsGetApertureSegmentPageArray: seg=%u offset "
                    "0x%I64x >= TotalSize 0x%I64x\n",
                    SegmentId, Offset, Seg->TotalSize);
        return STATUS_INVALID_PARAMETER;
    }

    FirstPageIndex  = (ULONG)(Offset / PAGE_SIZE);
    *OutPageArray   = &Seg->AperturePages[FirstPageIndex];
    *OutNumPages    = Seg->NumAperturePages - FirstPageIndex;

    return STATUS_SUCCESS;
}
