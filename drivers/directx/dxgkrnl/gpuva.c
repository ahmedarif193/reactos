/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU Virtual Address Space Management (WDDM 2.0)
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * This file implements the software bookkeeping half of per-process WDDM 2.0
 * GPU virtual address management.  Reservations and update-operation planning
 * are deterministic, but MAP/UPDATE/virtual-submit success is deliberately
 * blocked until a real paging-buffer path has built and submitted the PTEs.
 *
 * Architecture:
 *
 *   - Each DXGKRNL_PROCESS tracks a sorted list of GPU VA ranges
 *     (DXGKRNL_GPUVA_RANGE), either reserved or mapped to allocations.
 *
 *   - DxgkDdiCreateProcess is called once per process when it first
 *     uses the GPU.  The miniport creates per-process GPU state.
 *
 *   - DxgkDdiSetRootPageTable is called only after RootPageTableProgrammed
 *     proves that a real page-table build/submit path completed.
 *
 *   - GPU VA UPDATE operations are applied only to a temporary cloned list
 *     for validation.  The authoritative list is unchanged while hardware
 *     page-table programming is unavailable.
 *
 *   - CPU host aperture mapping allows CPU access to GPU-local memory
 *     through a CPU-visible aperture segment.
 *
 * Pool tags:
 *   'GVxD' — GPU VA range objects  (TAG_DXGK_GPUVA)
 */

/* INCLUDES *******************************************************************/

#include "dxgkrnl_private.h"
#include "gpuva_core.h"
#include "vidmm.h"
#include <ndk/psfuncs.h>

#define NDEBUG
#include <debug.h>

/* MACROS / CONSTANTS *********************************************************/

/*
 * Default GPU VA space size per process: 256 TB (48-bit VA space).
 * Most WDDM 2.0 GPUs support at least 48-bit virtual addressing.
 */
#define GPUVA_DEFAULT_SPACE_SIZE    (256ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL)

/*
 * Minimum alignment for GPU VA allocations: 4 KB (GPU page size).
 */
#define GPUVA_PAGE_SIZE             (4096ULL)
#define GPUVA_PAGE_MASK             (GPUVA_PAGE_SIZE - 1ULL)
#define GPUVA_RESERVATION_ALIGNMENT (64ULL * 1024ULL)
#define GPUVA_RESERVATION_MASK      (GPUVA_RESERVATION_ALIGNMENT - 1ULL)

/* A fixed per-process node cap bounds user-controlled NonPagedPool growth.
 * Validation is serialized by GpuVaLock; clone/replacement amplification is
 * separately bounded to at most three capped lists plus two split nodes. */
#define GPUVA_MAX_PROCESS_RANGES    1024UL
#define GPUVA_MAX_TRANSIENT_RANGES  (GPUVA_MAX_PROCESS_RANGES * 3UL + 2UL)

/* Residency references, paging packets, and allocation destruction now share
 * one completion-driven state machine (paging.c). */
#define GPUVA_RESIDENCY_END_TO_END  1

/*
 * GPU VA space starts at 64 KB (skip the first 16 pages as a guard region
 * to catch NULL pointer dereferences on the GPU).
 */
#define GPUVA_START_ADDRESS         (64ULL * 1024ULL)

/* HELPERS ********************************************************************/

/*
 * GpuVaAlignUp
 * Rounds Value up to the next multiple of Alignment (power-of-2).
 */
FORCEINLINE
ULONGLONG
GpuVaAlignUp(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment)
{
    ASSERT(Alignment != 0);
    ASSERT((Alignment & (Alignment - 1)) == 0);
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

/*
 * GpuVaRangesOverlap
 * Returns TRUE if [StartA, StartA+SizeA) overlaps [StartB, StartB+SizeB).
 */
FORCEINLINE
BOOLEAN
GpuVaRangesOverlap(
    _In_ ULONGLONG StartA, _In_ ULONGLONG SizeA,
    _In_ ULONGLONG StartB, _In_ ULONGLONG SizeB)
{
    return DxgkGpuVaCoreRangesOverlap(StartA, SizeA, StartB, SizeB);
}

/*
 * GpuVaAllocRange
 * Allocate and initialize a DXGKRNL_GPUVA_RANGE from NonPagedPool.
 */
static PDXGKRNL_GPUVA_RANGE
GpuVaAllocRange(VOID)
{
    PDXGKRNL_GPUVA_RANGE Range;

    Range = (PDXGKRNL_GPUVA_RANGE)ExAllocatePoolWithTag(
                NonPagedPool,
                sizeof(DXGKRNL_GPUVA_RANGE),
                TAG_DXGK_GPUVA);

    if (Range != NULL)
    {
        RtlZeroMemory(Range, sizeof(DXGKRNL_GPUVA_RANGE));
        InitializeListHead(&Range->RangeListEntry);
    }

    return Range;
}

static NTSTATUS
GpuVaCreateBinding(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DKMT_HANDLE Handle,
    _Out_ PDXGKRNL_GPUVA_BINDING *OutBinding)
{
    PDXGKRNL_GPUVA_BINDING Binding;
    PDXGKVMM_ALLOCATION LogicalAllocation;
    PDXGKVMM_ALLOCATION BackingAllocation;
    NTSTATUS Status;

    if (OutBinding == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutBinding = NULL;
    Status = DxgkVidMmAcquireGpuVaBindingReferences((HANDLE)(ULONG_PTR)Handle, Adapter, Process, &LogicalAllocation, &BackingAllocation);
    if (!NT_SUCCESS(Status))
        return Status;
    Binding = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Binding), TAG_DXGK_GPUVA);
    if (Binding == NULL)
    {
        DxgkVidMmDereferenceAllocation(BackingAllocation);
        DxgkVidMmDereferenceLogicalAllocation(LogicalAllocation);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Binding->ReferenceCount = 1;
    Binding->Handle = Handle;
    Binding->LogicalAllocation = LogicalAllocation;
    Binding->BackingAllocation = BackingAllocation;
    *OutBinding = Binding;
    return STATUS_SUCCESS;
}

static BOOLEAN
GpuVaReferenceBinding(
    _In_ PDXGKRNL_GPUVA_BINDING Binding)
{
    LONG References;

    if (Binding == NULL)
        return FALSE;
    do
    {
        References = InterlockedCompareExchange(&Binding->ReferenceCount, 0, 0);
        if (References <= 0)
            return FALSE;
    } while (InterlockedCompareExchange(&Binding->ReferenceCount, References + 1, References) != References);
    return TRUE;
}

static VOID
GpuVaDereferenceBinding(
    _In_ PDXGKRNL_GPUVA_BINDING Binding)
{
    PDXGKVMM_ALLOCATION LogicalAllocation;
    PDXGKVMM_ALLOCATION BackingAllocation;
    LONG References;

    ASSERT(Binding != NULL);
    References = InterlockedDecrement(&Binding->ReferenceCount);
    ASSERT(References >= 0);
    if (References != 0)
        return;
    LogicalAllocation = Binding->LogicalAllocation;
    BackingAllocation = Binding->BackingAllocation;
    ExFreePoolWithTag(Binding, TAG_DXGK_GPUVA);
    DxgkVidMmDereferenceAllocation(BackingAllocation);
    DxgkVidMmDereferenceLogicalAllocation(LogicalAllocation);
}

/*
 * GpuVaFreeRange
 * Free a DXGKRNL_GPUVA_RANGE.
 */
static VOID
GpuVaFreeRange(
    _In_ PDXGKRNL_GPUVA_RANGE Range)
{
    if (Range->Binding != NULL)
        GpuVaDereferenceBinding(Range->Binding);
    ExFreePoolWithTag(Range, TAG_DXGK_GPUVA);
}

/*
 * GpuVaFindOverlapping
 *
 * Search Process->GpuVaRangeList for any range that overlaps
 * [Address, Address + Size).  Returns the first overlapping range
 * or NULL.
 *
 * Caller MUST hold Process->GpuVaLock.
 */
static PDXGKRNL_GPUVA_RANGE
GpuVaFindOverlapping(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG              Size)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        /* Ranges are sorted by VA; if this range starts past our end, stop. */
        if (Range->GpuVirtualAddress >= Address + Size)
            break;

        if (GpuVaRangesOverlap(Range->GpuVirtualAddress, Range->SizeInBytes,
                                Address, Size))
        {
            return Range;
        }
    }

    return NULL;
}

/*
 * GpuVaFindFreeRegion
 *
 * Find a free GPU VA region of at least Size bytes within [MinAddress, MaxAddress).
 * Returns the base address on success, 0 on failure.
 *
 * Uses a first-fit algorithm scanning the sorted range list for gaps.
 *
 * Caller MUST hold Process->GpuVaLock.
 */
static D3DGPU_VIRTUAL_ADDRESS
GpuVaFindFreeRegion(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS MinAddress,
    _In_ D3DGPU_VIRTUAL_ADDRESS MaxAddress,
    _In_ ULONGLONG              Size)
{
    D3DGPU_VIRTUAL_ADDRESS Candidate;
    PLIST_ENTRY Entry;

    if (MinAddress < GPUVA_START_ADDRESS)
        MinAddress = GPUVA_START_ADDRESS;

    Candidate = GpuVaAlignUp(MinAddress, GPUVA_RESERVATION_ALIGNMENT);

    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;

        /* If this range is entirely before our candidate, skip. */
        if (Range->GpuVirtualAddress > MAXULONGLONG - Range->SizeInBytes)
            return 0;
        RangeEnd = Range->GpuVirtualAddress + Range->SizeInBytes;
        if (RangeEnd <= Candidate)
            continue;

        /* Check if the gap before this range fits. */
        if (Candidate <= Range->GpuVirtualAddress && Size <= Range->GpuVirtualAddress - Candidate && Candidate <= MaxAddress && Size <= MaxAddress - Candidate)
            return Candidate;

        /* Advance candidate past this range. */
        if (RangeEnd > MAXULONGLONG - GPUVA_RESERVATION_MASK)
            return 0;
        Candidate = GpuVaAlignUp(RangeEnd, GPUVA_RESERVATION_ALIGNMENT);

        if (Candidate >= MaxAddress || Size > MaxAddress - Candidate)
            return 0;
    }

    /* Check trailing gap after last range. */
    if (Candidate <= MaxAddress && Size <= MaxAddress - Candidate)
        return Candidate;

    return 0;
}

/*
 * GpuVaInsertRange
 *
 * Insert a range into the sorted list, maintaining ascending VA order.
 *
 * Caller MUST hold Process->GpuVaLock.
 */
static VOID
GpuVaInsertRange(
    _In_ PDXGKRNL_PROCESS    Process,
    _In_ PDXGKRNL_GPUVA_RANGE Range)
{
    PLIST_ENTRY Entry;

    /* Find insertion point (first range with VA > new range's VA). */
    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Existing =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        if (Existing->GpuVirtualAddress > Range->GpuVirtualAddress)
        {
            /* Insert before this entry. */
            InsertTailList(Entry, &Range->RangeListEntry);
            return;
        }
    }

    /* All existing entries have VA <= new range's VA; append at end. */
    InsertTailList(&Process->GpuVaRangeList, &Range->RangeListEntry);
}

static BOOLEAN
GpuVaGetRangeEnd(
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *EndAddress)
{
    return DxgkGpuVaCoreRangeEnd(Address, Size, EndAddress);
}

static VOID
GpuVaFreeList(
    _Inout_ PLIST_ENTRY ListHead)
{
    while (!IsListEmpty(ListHead))
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(RemoveHeadList(ListHead), DXGKRNL_GPUVA_RANGE, RangeListEntry);

        GpuVaFreeRange(Range);
    }
}

static VOID
GpuVaMoveList(
    _Inout_ PLIST_ENTRY DestinationHead,
    _Inout_ PLIST_ENTRY SourceHead)
{
    ASSERT(IsListEmpty(DestinationHead));
    while (!IsListEmpty(SourceHead))
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(RemoveHeadList(SourceHead), DXGKRNL_GPUVA_RANGE, RangeListEntry);

        InitializeListHead(&Range->RangeListEntry);
        InsertTailList(DestinationHead, &Range->RangeListEntry);
    }
}

static PDXGKRNL_GPUVA_RANGE
GpuVaCloneRange(
    _In_ CONST DXGKRNL_GPUVA_RANGE *Source)
{
    PDXGKRNL_GPUVA_RANGE Clone = GpuVaAllocRange();

    if (Clone != NULL)
    {
        Clone->GpuVirtualAddress = Source->GpuVirtualAddress;
        Clone->SizeInBytes = Source->SizeInBytes;
        Clone->State = Source->State;
        Clone->hAllocation = Source->hAllocation;
        if (Source->Binding != NULL && !GpuVaReferenceBinding(Source->Binding))
        {
            GpuVaFreeRange(Clone);
            return NULL;
        }
        Clone->Binding = Source->Binding;
        Clone->AllocationOffset = Source->AllocationOffset;
        Clone->Protection = Source->Protection;
        Clone->DriverProtection = Source->DriverProtection;
        Clone->ReservationBase = Source->ReservationBase;
        Clone->ReservationSize = Source->ReservationSize;
    }

    return Clone;
}

static NTSTATUS
GpuVaCloneList(
    _In_ PLIST_ENTRY SourceHead,
    _Out_ PLIST_ENTRY CloneHead)
{
    PLIST_ENTRY Entry;

    InitializeListHead(CloneHead);
    for (Entry = SourceHead->Flink; Entry != SourceHead; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Source = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        PDXGKRNL_GPUVA_RANGE Clone = GpuVaCloneRange(Source);

        if (Clone == NULL)
        {
            GpuVaFreeList(CloneHead);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        InsertTailList(CloneHead, &Clone->RangeListEntry);
    }

    return STATUS_SUCCESS;
}

static BOOLEAN
GpuVaCountList(
    _In_ PLIST_ENTRY ListHead,
    _In_ ULONG Limit,
    _Out_ ULONG *RangeCount)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    if (RangeCount == NULL)
        return FALSE;
    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        if (Count == Limit)
            return FALSE;
        Count++;
    }
    *RangeCount = Count;
    return TRUE;
}

static BOOLEAN
GpuVaListCoversRange(
    _In_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _In_ DXGKRNL_GPUVA_STATE RequiredState)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    D3DGPU_VIRTUAL_ADDRESS Cursor;
    PLIST_ENTRY Entry;

    if (!GpuVaGetRangeEnd(Address, Size, &EndAddress))
        return FALSE;

    Cursor = Address;
    for (Entry = ListHead->Flink; Entry != ListHead && Cursor < EndAddress; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;

        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
            return FALSE;
        if (RangeEnd <= Cursor)
            continue;
        if (Range->GpuVirtualAddress > Cursor || Range->State != RequiredState)
            return FALSE;
        if (RequiredState == GpuVaStateMapped && !Range->Protection.SystemUseOnly && (Range->Binding == NULL || Range->hAllocation != (HANDLE)(ULONG_PTR)Range->Binding->Handle || InterlockedCompareExchange(&Range->Binding->LogicalAllocation->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Range->Binding->BackingAllocation->ReferenceCount, 0, 0) <= 0))
            return FALSE;
        Cursor = min(RangeEnd, EndAddress);
    }

    return Cursor == EndAddress;
}

/*
 * Command buffers must come from executable mappings when the miniport
 * advertises no-execute PTE support.  This is checked under the same lock as
 * the submission pin so a MAP_PROTECT cannot race the decision.
 */
static BOOLEAN
GpuVaListAllowsExecute(
    _In_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    D3DGPU_VIRTUAL_ADDRESS Cursor;
    PLIST_ENTRY Entry;

    if (!GpuVaGetRangeEnd(Address, Size, &EndAddress))
        return FALSE;

    Cursor = Address;
    for (Entry = ListHead->Flink;
         Entry != ListHead && Cursor < EndAddress;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range =
            CONTAINING_RECORD(Entry,
                              DXGKRNL_GPUVA_RANGE,
                              RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;

        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress,
                              Range->SizeInBytes,
                              &RangeEnd))
        {
            return FALSE;
        }
        if (RangeEnd <= Cursor)
            continue;
        if (Range->GpuVirtualAddress > Cursor ||
            Range->State != GpuVaStateMapped ||
            !Range->Protection.Execute ||
            Range->Protection.Zero ||
            Range->Protection.NoAccess)
        {
            return FALSE;
        }
        Cursor = min(RangeEnd, EndAddress);
    }

    return Cursor == EndAddress;
}

/*
 * GpuVaListAdjustPin — add Delta to SubmissionPinCount on every range that
 * covers [Address, Address+Size).  Caller holds GpuVaLock and has already
 * verified coverage, so every step of the walk must land on a covering range.
 */
static VOID
GpuVaListAdjustPin(
    _In_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _In_ LONG Delta)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    D3DGPU_VIRTUAL_ADDRESS Cursor;
    PLIST_ENTRY Entry;

    if (!GpuVaGetRangeEnd(Address, Size, &EndAddress))
        return;

    Cursor = Address;
    for (Entry = ListHead->Flink; Entry != ListHead && Cursor < EndAddress; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;

        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
            return;
        if (RangeEnd <= Cursor)
            continue;
        if (Range->GpuVirtualAddress > Cursor)
            return;
        if (Delta > 0)
        {
            Range->SubmissionPinCount += (ULONG)Delta;
        }
        else
        {
            ASSERT(Range->SubmissionPinCount >= (ULONG)(-Delta));
            if (Range->SubmissionPinCount >= (ULONG)(-Delta))
                Range->SubmissionPinCount -= (ULONG)(-Delta);
        }
        Cursor = min(RangeEnd, EndAddress);
    }
}

/* TRUE if any range covering [Address, Address+Size) has live submissions. */
static BOOLEAN
GpuVaListRangeIsPinned(
    _In_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    PLIST_ENTRY Entry;

    if (!GpuVaGetRangeEnd(Address, Size, &EndAddress))
        return FALSE;
    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;

        if (Range->SubmissionPinCount == 0)
            continue;
        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
            continue;
        if (Range->GpuVirtualAddress < EndAddress && Address < RangeEnd)
            return TRUE;
    }
    return FALSE;
}

static BOOLEAN
GpuVaGetReservationForRange(
    _In_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *ReservationBase,
    _Out_ ULONGLONG *ReservationSize)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    D3DGPU_VIRTUAL_ADDRESS Cursor;
    D3DGPU_VIRTUAL_ADDRESS ExpectedBase = 0;
    ULONGLONG ExpectedSize = 0;
    PLIST_ENTRY Entry;

    if (ReservationBase == NULL || ReservationSize == NULL || !GpuVaGetRangeEnd(Address, Size, &EndAddress))
        return FALSE;

    Cursor = Address;
    for (Entry = ListHead->Flink; Entry != ListHead && Cursor < EndAddress; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;

        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
            return FALSE;
        if (RangeEnd <= Cursor)
            continue;
        if (Range->GpuVirtualAddress > Cursor || Range->ReservationSize == 0)
            return FALSE;
        if (ExpectedSize == 0)
        {
            ExpectedBase = Range->ReservationBase;
            ExpectedSize = Range->ReservationSize;
        }
        else if (Range->ReservationBase != ExpectedBase || Range->ReservationSize != ExpectedSize)
            return FALSE;
        Cursor = min(RangeEnd, EndAddress);
    }

    if (Cursor != EndAddress)
        return FALSE;
    *ReservationBase = ExpectedBase;
    *ReservationSize = ExpectedSize;
    return TRUE;
}

static NTSTATUS
GpuVaSplitRangeAt(
    _Inout_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address)
{
    PLIST_ENTRY Entry;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;
        PDXGKRNL_GPUVA_RANGE Right;

        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
            return STATUS_INTEGER_OVERFLOW;
        if (Address <= Range->GpuVirtualAddress)
            return STATUS_SUCCESS;
        if (Address >= RangeEnd)
            continue;

        Right = GpuVaCloneRange(Range);
        if (Right == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Right->GpuVirtualAddress = Address;
        Right->SizeInBytes = RangeEnd - Address;
        if (Right->State == GpuVaStateMapped)
            Right->AllocationOffset += Address - Range->GpuVirtualAddress;
        Range->SizeInBytes = Address - Range->GpuVirtualAddress;
        InsertHeadList(&Range->RangeListEntry, &Right->RangeListEntry);
        return STATUS_SUCCESS;
    }

    return STATUS_SUCCESS;
}

static VOID
GpuVaRemoveRangeSpan(
    _Inout_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress = Address + Size;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Next)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        Next = Entry->Flink;
        if (Range->GpuVirtualAddress < Address)
            continue;
        if (Range->GpuVirtualAddress >= EndAddress)
            break;
        RemoveEntryList(&Range->RangeListEntry);
        GpuVaFreeRange(Range);
    }
}

static VOID
GpuVaInsertRangeInList(
    _Inout_ PLIST_ENTRY ListHead,
    _In_ PDXGKRNL_GPUVA_RANGE Range)
{
    PLIST_ENTRY Entry;

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Existing = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        if (Existing->GpuVirtualAddress > Range->GpuVirtualAddress)
        {
            InsertTailList(Entry, &Range->RangeListEntry);
            return;
        }
    }

    InsertTailList(ListHead, &Range->RangeListEntry);
}

static NTSTATUS
GpuVaReplaceSpan(
    _Inout_ PLIST_ENTRY ListHead,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _Inout_ PLIST_ENTRY ReplacementHead)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    NTSTATUS Status;

    if (!GpuVaGetRangeEnd(Address, Size, &EndAddress))
        return STATUS_INVALID_PARAMETER;

    Status = GpuVaSplitRangeAt(ListHead, Address);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = GpuVaSplitRangeAt(ListHead, EndAddress);
    if (!NT_SUCCESS(Status))
        return Status;

    GpuVaRemoveRangeSpan(ListHead, Address, Size);
    while (!IsListEmpty(ReplacementHead))
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(RemoveHeadList(ReplacementHead), DXGKRNL_GPUVA_RANGE, RangeListEntry);

        InitializeListHead(&Range->RangeListEntry);
        GpuVaInsertRangeInList(ListHead, Range);
    }

    return STATUS_SUCCESS;
}

static BOOLEAN
GpuVaProtectionValid(
    _In_ D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection,
    _In_ BOOLEAN UnmapProtection)
{
    if (Protection.SystemUseOnly || Protection.Reserved != 0)
        return FALSE;
    if (Protection.Zero || Protection.NoAccess)
    {
        if (Protection.Write || Protection.Execute || Protection.Zero == Protection.NoAccess)
            return FALSE;
        return TRUE;
    }
    if (UnmapProtection)
        return FALSE;
    return TRUE;
}

static NTSTATUS
GpuVaApplyMapOperation(
    _Inout_ PLIST_ENTRY ListHead,
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operation,
    _In_ BOOLEAN Protect)
{
    D3DGPU_VIRTUAL_ADDRESS Address;
    D3DGPU_VIRTUAL_ADDRESS ReservationBase;
    ULONGLONG Size;
    ULONGLONG ReservationSize;
    D3DKMT_HANDLE AllocationHandle;
    ULONGLONG AllocationOffset;
    ULONGLONG AllocationSize;
    ULONGLONG RangeOffset;
    D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection;
    PDXGKRNL_GPUVA_BINDING Binding = NULL;
    UINT64 DriverProtection;
    LIST_ENTRY ReplacementHead;
    NTSTATUS Status;

    RtlZeroMemory(&Protection, sizeof(Protection));
    DriverProtection = 0;
    if (Protect)
    {
        Address = Operation->MapProtect.BaseAddress;
        Size = Operation->MapProtect.SizeInBytes;
        AllocationHandle = Operation->MapProtect.hAllocation;
        AllocationOffset = Operation->MapProtect.AllocationOffsetInBytes;
        AllocationSize = Operation->MapProtect.AllocationSizeInBytes;
        Protection = Operation->MapProtect.Protection;
        DriverProtection = Operation->MapProtect.DriverProtection;
    }
    else
    {
        Address = Operation->Map.BaseAddress;
        Size = Operation->Map.SizeInBytes;
        AllocationHandle = Operation->Map.hAllocation;
        AllocationOffset = Operation->Map.AllocationOffsetInBytes;
        AllocationSize = Operation->Map.AllocationSizeInBytes;
        /*
         * The legacy MAP operation has no protection payload. Its documented
         * compatibility protection is read/write/execute; MAP_PROTECT is the
         * operation that can deliberately remove write or execute access.
         */
        Protection.Write = 1;
        Protection.Execute = 1;
    }

    if (Size == 0 || (Address & GPUVA_PAGE_MASK) != 0 || (Size & GPUVA_PAGE_MASK) != 0 || (AllocationOffset & GPUVA_PAGE_MASK) != 0 || (AllocationSize & GPUVA_PAGE_MASK) != 0 || !GpuVaProtectionValid(Protection, FALSE))
        return STATUS_INVALID_PARAMETER;
    if (!GpuVaGetReservationForRange(ListHead, Address, Size, &ReservationBase, &ReservationSize))
        return STATUS_CONFLICTING_ADDRESSES;

    InitializeListHead(&ReplacementHead);
    if (Protection.Zero || Protection.NoAccess)
    {
        PDXGKRNL_GPUVA_RANGE Replacement;

        if (!Protect || AllocationHandle != 0 || AllocationOffset != 0 || AllocationSize != 0)
            return STATUS_INVALID_PARAMETER;
        Replacement = GpuVaAllocRange();
        if (Replacement == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        Replacement->GpuVirtualAddress = Address;
        Replacement->SizeInBytes = Size;
        Replacement->State = GpuVaStateReserved;
        Replacement->Protection = Protection;
        Replacement->DriverProtection = DriverProtection;
        Replacement->ReservationBase = ReservationBase;
        Replacement->ReservationSize = ReservationSize;
        InsertTailList(&ReplacementHead, &Replacement->RangeListEntry);
    }
    else
    {
        if (AllocationSize == 0)
            AllocationSize = Size;
        if (AllocationSize > Size || Size % AllocationSize != 0)
            return STATUS_INVALID_PARAMETER;
        if (Size / AllocationSize > GPUVA_MAX_PROCESS_RANGES)
            return STATUS_QUOTA_EXCEEDED;
        if (AllocationHandle == 0 || AllocationOffset > MAXULONGLONG - AllocationSize)
            return STATUS_INVALID_PARAMETER;
        Status = GpuVaCreateBinding(Adapter, Process, AllocationHandle, &Binding);
        if (!NT_SUCCESS(Status))
            return Status;
        if (AllocationOffset > Binding->BackingAllocation->Size || AllocationSize > Binding->BackingAllocation->Size - AllocationOffset)
        {
            GpuVaDereferenceBinding(Binding);
            return STATUS_INVALID_PARAMETER;
        }

        for (RangeOffset = 0; RangeOffset < Size; RangeOffset += AllocationSize)
        {
            PDXGKRNL_GPUVA_RANGE Replacement = GpuVaAllocRange();

            if (Replacement == NULL)
            {
                GpuVaFreeList(&ReplacementHead);
                GpuVaDereferenceBinding(Binding);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            if (!GpuVaReferenceBinding(Binding))
            {
                GpuVaFreeRange(Replacement);
                GpuVaFreeList(&ReplacementHead);
                GpuVaDereferenceBinding(Binding);
                return STATUS_DELETE_PENDING;
            }
            Replacement->GpuVirtualAddress = Address + RangeOffset;
            Replacement->SizeInBytes = AllocationSize;
            Replacement->State = GpuVaStateMapped;
            Replacement->hAllocation = (HANDLE)(ULONG_PTR)AllocationHandle;
            Replacement->Binding = Binding;
            Replacement->AllocationOffset = AllocationOffset;
            Replacement->Protection = Protection;
            Replacement->DriverProtection = DriverProtection;
            Replacement->ReservationBase = ReservationBase;
            Replacement->ReservationSize = ReservationSize;
            InsertTailList(&ReplacementHead, &Replacement->RangeListEntry);
        }
    }

    Status = GpuVaReplaceSpan(ListHead, Address, Size, &ReplacementHead);
    GpuVaFreeList(&ReplacementHead);
    if (Binding != NULL)
        GpuVaDereferenceBinding(Binding);
    return Status;
}

static NTSTATUS
GpuVaApplyUnmapOperation(
    _Inout_ PLIST_ENTRY ListHead,
    _In_ CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operation)
{
    D3DGPU_VIRTUAL_ADDRESS ReservationBase;
    ULONGLONG ReservationSize;
    PDXGKRNL_GPUVA_RANGE Replacement;
    LIST_ENTRY ReplacementHead;
    NTSTATUS Status;

    if (Operation->Unmap.SizeInBytes == 0 || (Operation->Unmap.BaseAddress & GPUVA_PAGE_MASK) != 0 || (Operation->Unmap.SizeInBytes & GPUVA_PAGE_MASK) != 0 || !GpuVaProtectionValid(Operation->Unmap.Protection, TRUE))
        return STATUS_INVALID_PARAMETER;
    if (!GpuVaGetReservationForRange(ListHead, Operation->Unmap.BaseAddress, Operation->Unmap.SizeInBytes, &ReservationBase, &ReservationSize))
        return STATUS_CONFLICTING_ADDRESSES;

    Replacement = GpuVaAllocRange();
    if (Replacement == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Replacement->GpuVirtualAddress = Operation->Unmap.BaseAddress;
    Replacement->SizeInBytes = Operation->Unmap.SizeInBytes;
    Replacement->State = GpuVaStateReserved;
    Replacement->Protection = Operation->Unmap.Protection;
    Replacement->ReservationBase = ReservationBase;
    Replacement->ReservationSize = ReservationSize;
    InitializeListHead(&ReplacementHead);
    InsertTailList(&ReplacementHead, &Replacement->RangeListEntry);
    Status = GpuVaReplaceSpan(ListHead, Replacement->GpuVirtualAddress, Replacement->SizeInBytes, &ReplacementHead);
    GpuVaFreeList(&ReplacementHead);
    return Status;
}

static NTSTATUS
GpuVaApplyCopyOperation(
    _Inout_ PLIST_ENTRY ListHead,
    _In_ CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operation)
{
    D3DGPU_VIRTUAL_ADDRESS SourceEnd;
    D3DGPU_VIRTUAL_ADDRESS SourceReservationBase;
    D3DGPU_VIRTUAL_ADDRESS DestReservationBase;
    ULONGLONG SourceReservationSize;
    ULONGLONG DestReservationSize;
    LIST_ENTRY ReplacementHead;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    if (Operation->Copy.SizeInBytes == 0 || (Operation->Copy.SourceAddress & GPUVA_PAGE_MASK) != 0 || (Operation->Copy.DestAddress & GPUVA_PAGE_MASK) != 0 || (Operation->Copy.SizeInBytes & GPUVA_PAGE_MASK) != 0 || !GpuVaGetRangeEnd(Operation->Copy.SourceAddress, Operation->Copy.SizeInBytes, &SourceEnd))
        return STATUS_INVALID_PARAMETER;
    if (!GpuVaGetReservationForRange(ListHead, Operation->Copy.SourceAddress, Operation->Copy.SizeInBytes, &SourceReservationBase, &SourceReservationSize) || !GpuVaGetReservationForRange(ListHead, Operation->Copy.DestAddress, Operation->Copy.SizeInBytes, &DestReservationBase, &DestReservationSize))
        return STATUS_CONFLICTING_ADDRESSES;

    InitializeListHead(&ReplacementHead);
    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Source = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS SourceRangeEnd;
        D3DGPU_VIRTUAL_ADDRESS ChunkStart;
        D3DGPU_VIRTUAL_ADDRESS ChunkEnd;
        PDXGKRNL_GPUVA_RANGE Replacement;

        if (!GpuVaGetRangeEnd(Source->GpuVirtualAddress, Source->SizeInBytes, &SourceRangeEnd))
        {
            GpuVaFreeList(&ReplacementHead);
            return STATUS_INTEGER_OVERFLOW;
        }
        if (SourceRangeEnd <= Operation->Copy.SourceAddress)
            continue;
        if (Source->GpuVirtualAddress >= SourceEnd)
            break;
        ChunkStart = max(Source->GpuVirtualAddress, Operation->Copy.SourceAddress);
        ChunkEnd = min(SourceRangeEnd, SourceEnd);
        Replacement = GpuVaCloneRange(Source);
        if (Replacement == NULL)
        {
            GpuVaFreeList(&ReplacementHead);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Replacement->GpuVirtualAddress = Operation->Copy.DestAddress + (ChunkStart - Operation->Copy.SourceAddress);
        Replacement->SizeInBytes = ChunkEnd - ChunkStart;
        if (Replacement->State == GpuVaStateMapped)
            Replacement->AllocationOffset += ChunkStart - Source->GpuVirtualAddress;
        else
            Replacement->AllocationOffset = 0;
        Replacement->ReservationBase = DestReservationBase;
        Replacement->ReservationSize = DestReservationSize;
        InsertTailList(&ReplacementHead, &Replacement->RangeListEntry);
    }

    Status = GpuVaReplaceSpan(ListHead, Operation->Copy.DestAddress, Operation->Copy.SizeInBytes, &ReplacementHead);
    GpuVaFreeList(&ReplacementHead);
    return Status;
}


/* SOFTWARE PAGE TABLES (CPU_VIRTUAL GpuMmu) **********************************/

/*
 * Radix page tables whose shape comes from the miniport:
 * DXGKQAITYPE_PAGETABLELEVELDESC supplies each level's index bit count, table
 * size, and alignment, and DXGKQAITYPE_GPUMMUCAPS supplies the level count and
 * VA width.  Level 0 is the leaf.  DXGK_PTE is only the generic update
 * descriptor the DDI defines; every structural change is described to the
 * miniport through DXGK_OPERATION_UPDATE_PAGE_TABLE so the KMD, not dxgkrnl,
 * decides the hardware entry format.
 */
C_ASSERT(sizeof(DXGK_PTE) == 16);

/* Bounds user-controlled NonPagedPool growth. */
#define GPUVA_MAX_PROCESS_PAGE_TABLES 1024UL

FORCEINLINE
CONST DXGK_PAGE_TABLE_LEVEL_DESC *
GpuVaLevelDesc(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Level)
{
    return &Adapter->PageTableLevels[Level];
}

FORCEINLINE
ULONG
GpuVaLevelCount(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter->GpuMmuCaps.PageTableLevelCount;
}

FORCEINLINE
ULONGLONG
GpuVaEntriesPerTable(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Level)
{
    return 1ULL << Adapter->PageTableLevels[Level].PageTableIndexBitCount;
}

FORCEINLINE
ULONG
GpuVaTableBytes(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Level)
{
    return Adapter->PageTableLevels[Level].PageTableSizeInBytes;
}

/* Sum of the index bits below Level, i.e. the VA shift that level indexes at. */
FORCEINLINE
ULONG
GpuVaLevelShift(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Level)
{
    ULONG Shift = 12;
    ULONG Index;

    for (Index = 0; Index < Level; ++Index)
        Shift += Adapter->PageTableLevels[Index].PageTableIndexBitCount;
    return Shift;
}

FORCEINLINE
ULONG
GpuVaPteIndexFor(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONGLONG Va,
    _In_ ULONG Level)
{
    return (ULONG)((Va >> GpuVaLevelShift(Adapter, Level)) & (GpuVaEntriesPerTable(Adapter, Level) - 1ULL));
}

/*
 * GpuVaAllocPageTable
 * Allocate one software page table plus its tracking object and link it
 * on the process page-table list.  Caller MUST hold GpuVaLock.
 */
static PDXGKRNL_GPUVA_PAGE_TABLE
GpuVaAllocPageTable(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ ULONG Level,
    _In_ ULONGLONG CoverageBase)
{
    PDXGKRNL_GPUVA_PAGE_TABLE Table;
    PHYSICAL_ADDRESS LowestAddress;
    PHYSICAL_ADDRESS HighestAddress;
    PHYSICAL_ADDRESS BoundaryAddress;
    MEMORY_CACHING_TYPE CacheType;
    PDXGKRNL_ADAPTER Adapter = Process->Adapter;
    ULONG TableBytes;
    ULONG EntryCount;

    if (Process->GpuVaPageTableCount >= GPUVA_MAX_PROCESS_PAGE_TABLES)
        return NULL;
    if (Adapter == NULL || !Adapter->PageTableLevelsValid || Level >= GpuVaLevelCount(Adapter))
        return NULL;

    TableBytes = GpuVaTableBytes(Adapter, Level);
    EntryCount = (ULONG)GpuVaEntriesPerTable(Adapter, Level);

    Table = (PDXGKRNL_GPUVA_PAGE_TABLE)ExAllocatePoolWithTag(
                NonPagedPool, sizeof(*Table), TAG_DXGK_GPUVA_PT);
    if (Table == NULL)
        return NULL;
    RtlZeroMemory(Table, sizeof(*Table));

    LowestAddress.QuadPart = 0;
    HighestAddress.QuadPart = Adapter->HighestAcceptableAddress.QuadPart != 0 ? Adapter->HighestAcceptableAddress.QuadPart : (LONGLONG)-1;
    /* The miniport's declared alignment is expressed as a boundary the table
     * must not cross, which is exactly the contiguous-allocator contract. */
    BoundaryAddress.QuadPart = GpuVaLevelDesc(Adapter, Level)->PageTableAlignmentInBytes > TableBytes
                                   ? (LONGLONG)GpuVaLevelDesc(Adapter, Level)->PageTableAlignmentInBytes
                                   : 0;
    CacheType = Adapter->GpuMmuCaps.CacheCoherentMemorySupported ? MmCached : MmNonCached;
    Table->KernelVa = MmAllocateContiguousMemorySpecifyCache(TableBytes, LowestAddress, HighestAddress, BoundaryAddress, CacheType);
    if (Table->KernelVa == NULL)
    {
        ExFreePoolWithTag(Table, TAG_DXGK_GPUVA_PT);
        return NULL;
    }
    RtlZeroMemory(Table->KernelVa, TableBytes);

    if (Level > 0)
    {
        Table->Children = (PDXGKRNL_GPUVA_PAGE_TABLE *)ExAllocatePoolWithTag(
                    NonPagedPool,
                    (SIZE_T)EntryCount * sizeof(PDXGKRNL_GPUVA_PAGE_TABLE),
                    TAG_DXGK_GPUVA_PT);
        if (Table->Children == NULL)
        {
            MmFreeContiguousMemorySpecifyCache(Table->KernelVa, TableBytes, CacheType);
            ExFreePoolWithTag(Table, TAG_DXGK_GPUVA_PT);
            return NULL;
        }
        RtlZeroMemory(Table->Children,
                      (SIZE_T)EntryCount * sizeof(PDXGKRNL_GPUVA_PAGE_TABLE));
    }

    Table->Bytes = TableBytes;
    Table->EntryCount = EntryCount;
    Table->Level = Level;
    Table->CoverageBase = CoverageBase;
    Table->CacheType = CacheType;
    Table->Physical = MmGetPhysicalAddress(Table->KernelVa);

    InsertTailList(&Process->GpuVaPageTableList, &Table->PageTableListEntry);
    Process->GpuVaPageTableCount++;
    return Table;
}

/*
 * GpuVaFreePageTables
 * Free every page table of the process and reset the root state.
 * Caller MUST hold GpuVaLock (or own the process exclusively at teardown).
 */
static VOID
GpuVaFreePageTables(
    _In_ PDXGKRNL_PROCESS Process)
{
    while (!IsListEmpty(&Process->GpuVaPageTableList))
    {
        PDXGKRNL_GPUVA_PAGE_TABLE Table =
            CONTAINING_RECORD(RemoveHeadList(&Process->GpuVaPageTableList),
                              DXGKRNL_GPUVA_PAGE_TABLE, PageTableListEntry);

        if (Table->Children != NULL)
            ExFreePoolWithTag(Table->Children, TAG_DXGK_GPUVA_PT);
        MmFreeContiguousMemorySpecifyCache(Table->KernelVa, Table->Bytes, Table->CacheType);
        ExFreePoolWithTag(Table, TAG_DXGK_GPUVA_PT);
    }
    Process->GpuVaPageTableCount = 0;
    Process->hRootPageTable = NULL;
    Process->RootPageTableEntries = 0;
    Process->RootPageTableProgrammed = FALSE;
    RtlZeroMemory(&Process->RootPageTableAddress,
                  sizeof(Process->RootPageTableAddress));
}

/*
 * GpuVaEnsureRootPageTable
 * Allocate the root table on first use and publish it as the process root.
 * The CPU-visible tables ARE the authoritative page tables in CPU_VIRTUAL
 * mode, so the root counts as programmed from birth.
 * Caller MUST hold GpuVaLock.
 */
static NTSTATUS
GpuVaEnsureRootPageTable(
    _In_ PDXGKRNL_PROCESS Process)
{
    PDXGKRNL_GPUVA_PAGE_TABLE Root;
    PDXGKRNL_ADAPTER Adapter = Process->Adapter;
    ULONG RootLevel;
    ULONG RootEntries;
    SIZE_T ReportedSize;

    if (Process->hRootPageTable != NULL)
        return STATUS_SUCCESS;

    if (Adapter == NULL || !Adapter->GpuMmuCapsValid || !Adapter->PageTableLevelsValid)
        return STATUS_NOT_SUPPORTED;
    if (Adapter->GpuMmuCaps.PageTableUpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
        return STATUS_NOT_SUPPORTED;

    RootLevel = GpuVaLevelCount(Adapter) - 1;
    RootEntries = (ULONG)GpuVaEntriesPerTable(Adapter, RootLevel);
    ReportedSize = DxgkGpuVaGetRootPageTableSize(Adapter, RootEntries, 0);
    if (ReportedSize != GpuVaTableBytes(Adapter, RootLevel))
    {
        DPRINT1("DxgkGpuVa: root table size %Iu disagrees with level descriptor %u\n", ReportedSize, GpuVaTableBytes(Adapter, RootLevel));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Root = GpuVaAllocPageTable(Process, RootLevel, 0);
    if (Root == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Process->hRootPageTable = (HANDLE)Root;
    Process->RootPageTableAddress.SegmentId = 0;
    Process->RootPageTableAddress.SegmentOffset = (UINT64)Root->Physical.QuadPart;
    Process->RootPageTableEntries = RootEntries;
    Process->RootPageTableProgrammed = TRUE;
    return STATUS_SUCCESS;
}

/*
 * GpuVaNotifyPageTableUpdate
 *
 * Describes a written span of one table to the miniport through
 * DXGK_OPERATION_UPDATE_PAGE_TABLE and then invalidates the covering TLB
 * range.  dxgkrnl writes only the generic DXGK_PTE update descriptors; the
 * miniport owns the hardware entry format and the invalidation.
 * Caller MUST hold GpuVaLock.
 */
/*
 * GpuVaNotifyPageTableUpdate
 *
 * Records that a span of the process's page tables changed.  The description
 * to the miniport is a paging submission and therefore PASSIVE_LEVEL work, so
 * it is deferred to DxgkGpuVaFlushPageTableUpdates once GpuVaLock is dropped;
 * until then dxgkrnl's CPU-written tables remain authoritative, which is what
 * DXGK_PAGETABLEUPDATE_CPU_VIRTUAL means.
 *
 * Caller MUST hold GpuVaLock.
 */
static NTSTATUS
GpuVaNotifyPageTableUpdate(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ PDXGKRNL_GPUVA_PAGE_TABLE Table,
    _In_ ULONG StartIndex,
    _In_ ULONG Count,
    _In_ ULONGLONG FirstVirtualAddress,
    _In_ BOOLEAN InitialUpdate)
{
    PDXGKRNL_ADAPTER Adapter = Process->Adapter;
    ULONGLONG Coverage;

    UNREFERENCED_PARAMETER(InitialUpdate);
    if (Adapter == NULL || Count == 0 || Table == NULL)
        return STATUS_INVALID_PARAMETER;
    if (StartIndex >= Table->EntryCount || Count > Table->EntryCount - StartIndex)
        return STATUS_INVALID_PARAMETER;
    if (!DxgkPagingOperationSupported(Adapter, DxgkPagingOpUpdatePageTable))
        return STATUS_SUCCESS;

    Coverage = (ULONGLONG)Count << GpuVaLevelShift(Adapter, Table->Level);
    if (Coverage == 0 || FirstVirtualAddress > MAXULONGLONG - Coverage)
        return STATUS_INTEGER_OVERFLOW;
    if (!Process->PageTableUpdatePending)
    {
        Process->PageTableUpdatePending = TRUE;
        Process->PageTableUpdateStart = FirstVirtualAddress;
        Process->PageTableUpdateEnd = FirstVirtualAddress + Coverage;
    }
    else
    {
        if (FirstVirtualAddress < Process->PageTableUpdateStart)
            Process->PageTableUpdateStart = FirstVirtualAddress;
        if (FirstVirtualAddress + Coverage > Process->PageTableUpdateEnd)
            Process->PageTableUpdateEnd = FirstVirtualAddress + Coverage;
    }
    return STATUS_SUCCESS;
}

static VOID
GpuVaRequeuePageTableUpdate(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Start,
    _In_ D3DGPU_VIRTUAL_ADDRESS End)
{
    ExAcquireFastMutex(&Process->GpuVaLock);
    if (!Process->PageTableUpdatePending)
    {
        Process->PageTableUpdatePending = TRUE;
        Process->PageTableUpdateStart = Start;
        Process->PageTableUpdateEnd = End;
    }
    else
    {
        if (Start < Process->PageTableUpdateStart)
            Process->PageTableUpdateStart = Start;
        if (End > Process->PageTableUpdateEnd)
            Process->PageTableUpdateEnd = End;
    }
    ExReleaseFastMutex(&Process->GpuVaLock);
}

/*
 * DxgkGpuVaFlushPageTableUpdates
 *
 * Describes the page-table span accumulated under GpuVaLock to the miniport
 * and invalidates the covering translations.  PageTableFlushMutex covers the
 * entire snapshot/submit/retire transaction, so a second caller cannot observe
 * an empty pending span and release backing while the first invalidation is
 * still in flight.  A rejected transaction is merged back with any newer
 * updates before the mutex is released.
 *
 * The caller holds a process reference for the duration.
 *
 * IRQL: PASSIVE_LEVEL, GpuVaLock and PageTableFlushMutex NOT held.
 */
NTSTATUS
DxgkGpuVaFlushPageTableUpdates(
    _In_ PDXGKRNL_PROCESS Process)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE PagingDevice = NULL;
    DXGKRNL_PAGING_OP Op;
    HANDLE PagingMiniportDevice = NULL;
    D3DGPU_VIRTUAL_ADDRESS Start;
    D3DGPU_VIRTUAL_ADDRESS End;
    NTSTATUS Status;

    PAGED_CODE();
    if (Process == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = KeWaitForSingleObject(&Process->PageTableFlushMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    ExAcquireFastMutex(&Process->GpuVaLock);
    if (!Process->PageTableUpdatePending)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        KeReleaseMutex(&Process->PageTableFlushMutex, FALSE);
        return STATUS_SUCCESS;
    }
    Start = Process->PageTableUpdateStart;
    End = Process->PageTableUpdateEnd;
    Process->PageTableUpdatePending = FALSE;
    Process->PageTableUpdateStart = 0;
    Process->PageTableUpdateEnd = 0;
    ExReleaseFastMutex(&Process->GpuVaLock);

    Adapter = Process->Adapter;
    if (End <= Start)
    {
        Status = STATUS_DATA_ERROR;
        goto Complete;
    }
    if (Adapter == NULL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Requeue;
    }
    /* The tables the CPU already wrote are authoritative in CPU_VIRTUAL mode;
     * this tells the miniport which translations changed so it can drop any
     * it had cached. */
    if (!DxgkPagingOperationSupported(Adapter, DxgkPagingOpFlushTlb))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Requeue;
    }
    RtlZeroMemory(&Op, sizeof(Op));
    Op.Type = DxgkPagingOpFlushTlb;
    Status = DxgkReferenceProcessPagingDevice(Process,
                                              &PagingDevice,
                                              &PagingMiniportDevice);
    if (!NT_SUCCESS(Status))
    {
        goto Requeue;
    }
    Op.hMiniportDevice = PagingMiniportDevice;
    Op.hMiniportProcess = Process->hMiniportProcess;
    Op.RootPageTableAddress = Process->RootPageTableAddress;
    Op.StartVirtualAddress = Start;
    Op.EndVirtualAddress = End;
    /*
     * Passing PagingDevice makes every queued paging packet take its own device
     * reference.  The explicit reference acquired above covers the build and
     * no-packet paths; a queued packet remains safe even if this bounded wait
     * times out before retirement.
     */
    Status = DxgkPagingExecuteSynchronous(Adapter, PagingDevice, &Op);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxgkGpuVa: TLB invalidation rejected 0x%08lX over [0x%I64x,0x%I64x)\n", Status, Start, End);
        goto Requeue;
    }
    goto Complete;

Requeue:
    GpuVaRequeuePageTableUpdate(Process, Start, End);

Complete:
    if (PagingDevice != NULL)
        DxgkDereferenceDevice(PagingDevice);
    KeReleaseMutex(&Process->PageTableFlushMutex, FALSE);
    return Status;
}

/*
 * GpuVaGetLeafTable
 * Walk the radix from the root to the leaf covering Va, lazily allocating
 * missing levels when Allocate is TRUE.  Parent PTEs are linked as valid
 * table pointers.  Caller MUST hold GpuVaLock.
 */
static PDXGKRNL_GPUVA_PAGE_TABLE
GpuVaGetLeafTable(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ ULONGLONG Va,
    _In_ BOOLEAN Allocate)
{
    PDXGKRNL_GPUVA_PAGE_TABLE Table =
        (PDXGKRNL_GPUVA_PAGE_TABLE)Process->hRootPageTable;
    PDXGKRNL_ADAPTER Adapter = Process->Adapter;
    ULONG Level;

    if (Table == NULL || Adapter == NULL || !Adapter->PageTableLevelsValid)
        return NULL;

    for (Level = GpuVaLevelCount(Adapter) - 1; Level > 0; Level--)
    {
        ULONG Index = GpuVaPteIndexFor(Adapter, Va, Level);
        PDXGKRNL_GPUVA_PAGE_TABLE Child;

        if (Index >= Table->EntryCount)
            return NULL;
        Child = Table->Children[Index];
        if (Child == NULL)
        {
            DXGK_PTE *Entries;
            ULONGLONG CoverageBase;

            if (!Allocate)
                return NULL;
            CoverageBase = Va & ~((1ULL << GpuVaLevelShift(Adapter, Level)) - 1ULL);
            Child = GpuVaAllocPageTable(Process, Level - 1, CoverageBase);
            if (Child == NULL)
                return NULL;
            Table->Children[Index] = Child;
            Entries = (DXGK_PTE *)Table->KernelVa;
            Entries[Index].Flags = 0;
            Entries[Index].Valid = 1;
            Entries[Index].PageTableAddress =
                (ULONGLONG)Child->Physical.QuadPart & ~GPUVA_PAGE_MASK;
            if (!NT_SUCCESS(GpuVaNotifyPageTableUpdate(Process, Table, Index, 1, CoverageBase, TRUE)))
            {
                Entries[Index].Flags = 0;
                Entries[Index].PageTableAddress = 0;
                Table->Children[Index] = NULL;
                return NULL;
            }
        }
        Table = Child;
    }
    return Table;
}

/*
 * GpuVaAllocationPageAddress
 *
 * Physical address of the page at Offset in an allocation's current
 * placement.  A segment placement is physically contiguous from
 * PhysicalAddress; a system-memory backing is walked page by page.  Returns
 * zero when the allocation has no usable placement.
 */
static PHYSICAL_ADDRESS
GpuVaAllocationPageAddress(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONGLONG Offset)
{
    PHYSICAL_ADDRESS Physical;

    Physical.QuadPart = 0;
    if (Offset >= Allocation->Size)
        return Physical;
    if (Allocation->SystemMemory != NULL)
        return MmGetPhysicalAddress((PUCHAR)Allocation->SystemMemory + Offset);
    if (Allocation->Resident && Allocation->PhysicalAddress.QuadPart != 0)
        Physical.QuadPart = Allocation->PhysicalAddress.QuadPart + (LONGLONG)Offset;
    return Physical;
}

/*
 * GpuVaClearPteSpan
 * Zero the leaf PTEs over [Address, Address + SizeInBytes).  Never
 * allocates; untouched (never-mapped) pages are skipped.
 * Caller MUST hold GpuVaLock.
 */
static VOID
GpuVaClearPteSpan(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG SizeInBytes)
{
    ULONGLONG Offset;

    for (Offset = 0; Offset < SizeInBytes; Offset += GPUVA_PAGE_SIZE)
    {
        PDXGKRNL_GPUVA_PAGE_TABLE Leaf =
            GpuVaGetLeafTable(Process, Address + Offset, FALSE);
        DXGK_PTE *Entries;
        ULONG Index;

        if (Leaf == NULL)
            continue;
        Index = GpuVaPteIndexFor(Process->Adapter, Address + Offset, 0);
        Entries = (DXGK_PTE *)Leaf->KernelVa;
        Entries[Index].Flags = 0;
        Entries[Index].PageAddress = 0;
        (VOID)GpuVaNotifyPageTableUpdate(Process, Leaf, Index, 1, Address + Offset, FALSE);
    }
}

/*
 * GpuVaWritePteSpan
 * Write leaf PTEs for [Address, Address + SizeInBytes).  With an allocation
 * the PTEs become valid pointers at the backing system pages honouring the
 * protection; with Protection.Zero they become Zero PTEs; with NoAccess the
 * span is cleared.  Rolls the span back on mid-walk allocation failure.
 * Caller MUST hold GpuVaLock.
 */
static NTSTATUS
GpuVaWritePteSpan(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG SizeInBytes,
    _In_opt_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONGLONG AllocationOffset,
    _In_ D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection)
{
    ULONGLONG Offset;

    if (Allocation == NULL && Protection.NoAccess)
    {
        GpuVaClearPteSpan(Process, Address, SizeInBytes);
        return STATUS_SUCCESS;
    }

    for (Offset = 0; Offset < SizeInBytes; Offset += GPUVA_PAGE_SIZE)
    {
        PDXGKRNL_GPUVA_PAGE_TABLE Leaf =
            GpuVaGetLeafTable(Process, Address + Offset, TRUE);
        DXGK_PTE *Entries;
        DXGK_PTE Pte;

        if (Leaf == NULL)
        {
            GpuVaClearPteSpan(Process, Address, Offset);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(&Pte, sizeof(Pte));
        if (Allocation != NULL)
        {
            PHYSICAL_ADDRESS Physical = GpuVaAllocationPageAddress(Allocation, AllocationOffset + Offset);

            if (Physical.QuadPart == 0)
            {
                GpuVaClearPteSpan(Process, Address, Offset);
                return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
            }
            Pte.Valid = 1;
            Pte.CacheCoherent = 1;
            Pte.ReadOnly = Protection.Write ? 0 : 1;
            Pte.NoExecute = Protection.Execute ? 0 : 1;
            Pte.PageAddress = (ULONGLONG)Physical.QuadPart & ~GPUVA_PAGE_MASK;
        }
        else
        {
            Pte.Zero = 1;
        }

        {
            ULONG Index = GpuVaPteIndexFor(Process->Adapter, Address + Offset, 0);

            Entries = (DXGK_PTE *)Leaf->KernelVa;
            Entries[Index] = Pte;
            if (!NT_SUCCESS(GpuVaNotifyPageTableUpdate(Process, Leaf, Index, 1, Address + Offset, FALSE)))
            {
                Entries[Index].Flags = 0;
                Entries[Index].PageAddress = 0;
                GpuVaClearPteSpan(Process, Address, Offset);
                return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
            }
        }
    }
    return STATUS_SUCCESS;
}

/*
 * GpuVaRewriteSpanPtes
 * Re-derive the leaf PTEs for [Address, Address + SizeInBytes) from the
 * authoritative range list: mapped ranges point at their allocation's
 * backing pages, Zero-protected reservations become Zero PTEs, everything
 * else (NoAccess, plain reservations, gaps) is cleared.  A subrange whose
 * allocation vanished or whose tables cannot grow is cleared and the
 * failure reported, so the GPU faults instead of reading stale pages.
 * Caller MUST hold GpuVaLock.
 */
static NTSTATUS
GpuVaRewriteSpanPtes(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG SizeInBytes)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    D3DGPU_VIRTUAL_ADDRESS Cursor;
    NTSTATUS FinalStatus = STATUS_SUCCESS;
    PLIST_ENTRY Entry;

    if (!GpuVaGetRangeEnd(Address, SizeInBytes, &EndAddress))
        return STATUS_INVALID_PARAMETER;

    Cursor = Address;
    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList && Cursor < EndAddress;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;
        D3DGPU_VIRTUAL_ADDRESS OverlapEnd;
        ULONGLONG OverlapSize;
        NTSTATUS Status;

        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
            continue;
        if (RangeEnd <= Cursor)
            continue;
        if (Range->GpuVirtualAddress >= EndAddress)
            break;
        if (Range->GpuVirtualAddress > Cursor)
        {
            GpuVaClearPteSpan(Process, Cursor, Range->GpuVirtualAddress - Cursor);
            Cursor = Range->GpuVirtualAddress;
        }
        OverlapEnd = min(RangeEnd, EndAddress);
        OverlapSize = OverlapEnd - Cursor;

        if (Range->State == GpuVaStateMapped)
        {
            if (Range->Binding != NULL && Range->Binding->BackingAllocation != NULL && Range->Binding->BackingAllocation->SystemMemory != NULL)
                Status = GpuVaWritePteSpan(Process, Cursor, OverlapSize, Range->Binding->BackingAllocation, Range->AllocationOffset + (Cursor - Range->GpuVirtualAddress), Range->Protection);
            else
                Status = STATUS_NOT_SUPPORTED;
            if (!NT_SUCCESS(Status))
            {
                GpuVaClearPteSpan(Process, Cursor, OverlapSize);
                FinalStatus = Status;
            }
        }
        else if (Range->Protection.Zero)
        {
            Status = GpuVaWritePteSpan(Process, Cursor, OverlapSize, NULL, 0, Range->Protection);
            if (!NT_SUCCESS(Status))
            {
                GpuVaClearPteSpan(Process, Cursor, OverlapSize);
                FinalStatus = Status;
            }
        }
        else
        {
            GpuVaClearPteSpan(Process, Cursor, OverlapSize);
        }
        Cursor = OverlapEnd;
    }
    if (Cursor < EndAddress)
        GpuVaClearPteSpan(Process, Cursor, EndAddress - Cursor);
    return FinalStatus;
}

/*
 * GpuVaOperationSpan
 * Target VA span of an update operation (destination span for COPY).
 */
static BOOLEAN
GpuVaOperationSpan(
    _In_ CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operation,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *Address,
    _Out_ ULONGLONG *Size)
{
    switch (Operation->OperationType)
    {
        case D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP:
            *Address = Operation->Map.BaseAddress;
            *Size = Operation->Map.SizeInBytes;
            return TRUE;
        case D3DDDI_UPDATEGPUVIRTUALADDRESS_UNMAP:
            *Address = Operation->Unmap.BaseAddress;
            *Size = Operation->Unmap.SizeInBytes;
            return TRUE;
        case D3DDDI_UPDATEGPUVIRTUALADDRESS_COPY:
            *Address = Operation->Copy.DestAddress;
            *Size = Operation->Copy.SizeInBytes;
            return TRUE;
        case D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP_PROTECT:
            *Address = Operation->MapProtect.BaseAddress;
            *Size = Operation->MapProtect.SizeInBytes;
            return TRUE;
        default:
            *Address = 0;
            *Size = 0;
            return FALSE;
    }
}


/* PROCESS LIFECYCLE **********************************************************/

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static BOOLEAN
DxgkpGpuVaProcessDdiTableAvailable(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter != NULL &&
           Adapter->MiniportContext != NULL &&
           !Adapter->MiniportContext->UseDodLayout &&
           DxgkCapsCoreInterfaceVersionAtLeast(
               Adapter->MiniportContext->InitData.s.Version,
               DXGK_CAPS_CORE_LEVEL_WDDM_2_0);
}
#endif

/*
 * DxgkGpuVaCreateProcess
 *
 * Called when a process first uses the GPU.  Initializes the per-process
 * GPU VA space and calls DxgkDdiCreateProcess on the miniport.
 */
NTSTATUS
DxgkGpuVaCreateProcess(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    DPRINT("DxgkGpuVaCreateProcess: Adapter=%p Process=%p\n",
           Adapter, Process);

    ASSERT(Adapter != NULL);
    ASSERT(Process != NULL);

    /* Adapter/process ownership exists at every selected driver-model level. */
    Process->Adapter = Adapter;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /* Initialize the WDDM 2.0 GPU VA space. */
    InitializeListHead(&Process->GpuVaRangeList);
    ExInitializeFastMutex(&Process->GpuVaLock);
    KeInitializeMutex(&Process->PageTableFlushMutex, 0);
    Process->PageTableUpdatePending = FALSE;
    Process->PageTableUpdateStart = 0;
    Process->PageTableUpdateEnd = 0;
    Process->GpuVaTotalReserved = 0;
    Process->GpuVaTotalMapped   = 0;
    Process->GpuVaRangeCount    = 0;
    Process->ResidencyBudget    = 0;  /* 0 = no limit */
    Process->ResidentBytes      = 0;

    KeInitializeEvent(&Process->ResidencyBudgetEvent,
                      NotificationEvent, FALSE);

    Process->hMiniportProcess   = NULL;
    Process->MiniportProcessCreated = FALSE;
    Process->hRootPageTable     = NULL;
    RtlZeroMemory(&Process->RootPageTableAddress,
                  sizeof(Process->RootPageTableAddress));
    Process->RootPageTableEntries = 0;
    Process->RootPageTableProgrammed = FALSE;
    InitializeListHead(&Process->GpuVaPageTableList);
    Process->GpuVaPageTableCount = 0;

    /* Call the miniport's DxgkDdiCreateProcess if this table exposes it. */
    if (DxgkpGpuVaProcessDdiTableAvailable(Adapter) &&
        DXGK_CB_FULL(Adapter, DxgkDdiCreateProcess) != NULL)
    {
        DXGKARG_CREATEPROCESS CreateArgs;

        if (DXGK_CB_FULL(Adapter, DxgkDdiDestroyProcess) == NULL)
            return STATUS_NOT_SUPPORTED;

        RtlZeroMemory(&CreateArgs, sizeof(CreateArgs));
        CreateArgs.hDxgkProcess = (HANDLE)Process;
        CreateArgs.Flags.Value  = 0;
        CreateArgs.Flags.SystemProcess =
            PsIsSystemProcess(Process->Process) ? 1 : 0;
        CreateArgs.NumPasid     = 0;
        CreateArgs.pPasid       = NULL;

        if (!DxgkAcquireKmdCall(Adapter))
            return STATUS_DELETE_PENDING;
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateProcess)(
                         Adapter->MiniportDeviceContext,
                         &CreateArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);

        if (NT_SUCCESS(Status))
        {
            Process->hMiniportProcess = CreateArgs.hKmdProcess;
            Process->MiniportProcessCreated = TRUE;
            DPRINT("DxgkGpuVaCreateProcess: miniport process=%p\n",
                   Process->hMiniportProcess);
        }
        else
        {
            DPRINT1("DxgkGpuVaCreateProcess: DxgkDdiCreateProcess failed "
                    "0x%08lx\n", Status);
            return Status;
        }
    }
#endif

    DPRINT("DxgkGpuVaCreateProcess: success\n");
    return Status;
}


/*
 * DxgkGpuVaDestroyProcess
 *
 * Tears down the per-process GPU VA space.  Frees all VA ranges and
 * calls DxgkDdiDestroyProcess on the miniport.
 */
VOID
DxgkGpuVaDestroyProcess(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaDestroyProcess: Adapter=%p Process=%p\n",
           Adapter, Process);

    if (Process == NULL)
        return;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /* Free all GPU VA ranges. */
    ExAcquireFastMutex(&Process->GpuVaLock);
    {
        PLIST_ENTRY Entry = Process->GpuVaRangeList.Flink;
        while (Entry != &Process->GpuVaRangeList)
        {
            PLIST_ENTRY Next = Entry->Flink;
            PDXGKRNL_GPUVA_RANGE Range =
                CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

            RemoveEntryList(Entry);
            GpuVaFreeRange(Range);
            Entry = Next;
        }
    }
    ExReleaseFastMutex(&Process->GpuVaLock);

    /* Free the software page tables (root included). */
    ExAcquireFastMutex(&Process->GpuVaLock);
    GpuVaFreePageTables(Process);
    ExReleaseFastMutex(&Process->GpuVaLock);

    /*
     * A successful CreateProcess owns one DestroyProcess callback even when
     * the miniport selected NULL as its opaque process context.
     */
    if (Process->MiniportProcessCreated)
    {
        if (DxgkpGpuVaProcessDdiTableAvailable(Adapter) &&
            DXGK_CB_FULL(Adapter, DxgkDdiDestroyProcess) != NULL &&
            DxgkAcquireMiniportCallback(Adapter))
        {
            NTSTATUS Status;

            _SEH2_TRY
            {
                Status = DXGK_CB_FULL(Adapter, DxgkDdiDestroyProcess)(
                             Adapter->MiniportDeviceContext,
                             Process->hMiniportProcess);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            DxgkReleaseMiniportCallback(Adapter);

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("DxgkGpuVaDestroyProcess: "
                        "DxgkDdiDestroyProcess failed 0x%08lx\n",
                        Status);
            }
        }

        Process->MiniportProcessCreated = FALSE;
        Process->hMiniportProcess = NULL;
    }

    Process->GpuVaRangeCount = 0;
#endif

    DPRINT("DxgkGpuVaDestroyProcess: done\n");
}


/* GPU VA RANGE MANAGEMENT ****************************************************/

/*
 * Plan a reservation without changing the process GPUVA state.  The win32k
 * bridge publishes the selected address first and then commits the same fixed
 * address through DxgkGpuVaReserve.  A failed user copy therefore cannot leave
 * an unreachable reservation behind.
 */
NTSTATUS
DxgkGpuVaPlanReserve(_In_ PDXGKRNL_PROCESS Process, _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress, _In_ D3DGPU_VIRTUAL_ADDRESS MinAddress, _In_ D3DGPU_VIRTUAL_ADDRESS MaxAddress, _In_ ULONGLONG SizeInBytes, _In_ D3DDDIGPUVIRTUALADDRESS_RESERVATION_TYPE ReservationType, _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress)
{
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;

    PAGED_CODE();

    if (Process == NULL || OutAddress == NULL || SizeInBytes == 0 || (SizeInBytes & GPUVA_RESERVATION_MASK) != 0 || ReservationType > D3DDDIGPUVIRTUALADDRESS_RESERVE_ZERO)
        return STATUS_INVALID_PARAMETER;

    *OutAddress = 0;
    if (BaseAddress != 0)
    {
        if ((BaseAddress & GPUVA_RESERVATION_MASK) != 0 || BaseAddress < GPUVA_START_ADDRESS || BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE || SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        if ((MinAddress != 0 && (MinAddress & GPUVA_RESERVATION_MASK) != 0) || (MaxAddress != 0 && (MaxAddress & GPUVA_RESERVATION_MASK) != 0))
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress == 0 || MaxAddress > GPUVA_DEFAULT_SPACE_SIZE)
            MaxAddress = GPUVA_DEFAULT_SPACE_SIZE;
        MinAddress = max(MinAddress, GPUVA_START_ADDRESS);
        if (MinAddress >= MaxAddress || SizeInBytes > MaxAddress - MinAddress)
            return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&Process->GpuVaLock);
    if (Process->GpuVaRangeCount >= GPUVA_MAX_PROCESS_RANGES)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_QUOTA_EXCEEDED;
    }

    if (BaseAddress != 0)
    {
        ActualAddress = BaseAddress;
        if (GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes) != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else
    {
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_NO_MEMORY;
        }
    }

    ExReleaseFastMutex(&Process->GpuVaLock);
    *OutAddress = ActualAddress;
    return STATUS_SUCCESS;
}

/*
 * Plan a map without allocating ranges or writing PTEs.  A later fixed-base
 * DxgkGpuVaMap call performs the atomic commit after win32k has published the
 * selected address.  If another thread wins the address in between, commit
 * fails and no hidden mapping is created.
 */
NTSTATUS
DxgkGpuVaPlanMap(_In_ PDXGKRNL_ADAPTER Adapter, _In_ PDXGKRNL_PROCESS Process, _In_opt_ PDXGKVMM_ALLOCATION Allocation, _In_ ULONGLONG AllocationOffset, _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress, _In_ D3DGPU_VIRTUAL_ADDRESS MinAddress, _In_ D3DGPU_VIRTUAL_ADDRESS MaxAddress, _In_ ULONGLONG SizeInBytes, _In_ D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection, _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress)
{
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;
    D3DGPU_VIRTUAL_ADDRESS ReservationBase;
    ULONGLONG ReservationSize;
    ULONG AuthoritativeCount;
    BOOLEAN InReservation;

    PAGED_CODE();

    if (Process == NULL || OutAddress == NULL || SizeInBytes == 0 || (SizeInBytes & GPUVA_PAGE_MASK) != 0 || (BaseAddress & GPUVA_PAGE_MASK) != 0 || (AllocationOffset & GPUVA_PAGE_MASK) != 0 || !GpuVaProtectionValid(Protection, FALSE))
        return STATUS_INVALID_PARAMETER;
    if ((Protection.Zero || Protection.NoAccess) != (Allocation == NULL))
        return STATUS_INVALID_PARAMETER;
    if (Adapter == NULL || !Adapter->GpuMmuCapsValid || Adapter->GpuMmuCaps.PageTableUpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
        return STATUS_NOT_SUPPORTED;
    if (Allocation != NULL && Allocation->SystemMemory == NULL && !Allocation->Resident)
        return STATUS_NOT_SUPPORTED;

    *OutAddress = 0;
    if (BaseAddress != 0)
    {
        if (BaseAddress < GPUVA_START_ADDRESS || BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE || SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        if ((MinAddress & GPUVA_PAGE_MASK) != 0 || (MaxAddress & GPUVA_PAGE_MASK) != 0)
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress == 0 || MaxAddress > GPUVA_DEFAULT_SPACE_SIZE)
            MaxAddress = GPUVA_DEFAULT_SPACE_SIZE;
        MinAddress = max(MinAddress, GPUVA_START_ADDRESS);
        if (MinAddress >= MaxAddress || SizeInBytes > MaxAddress - MinAddress)
            return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&Process->GpuVaLock);
    if (!GpuVaCountList(&Process->GpuVaRangeList, GPUVA_MAX_PROCESS_RANGES, &AuthoritativeCount) || AuthoritativeCount != Process->GpuVaRangeCount)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_INTERNAL_ERROR;
    }
    if ((ULONGLONG)AuthoritativeCount * 2ULL + 3ULL > GPUVA_MAX_TRANSIENT_RANGES)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_QUOTA_EXCEEDED;
    }

    if (BaseAddress != 0)
    {
        ActualAddress = BaseAddress;
        InReservation = GpuVaGetReservationForRange(&Process->GpuVaRangeList, ActualAddress, SizeInBytes, &ReservationBase, &ReservationSize);
        if (!InReservation && GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes) != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_CONFLICTING_ADDRESSES;
        }
        if (!InReservation && Process->GpuVaRangeCount >= GPUVA_MAX_PROCESS_RANGES)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_QUOTA_EXCEEDED;
        }
    }
    else
    {
        if (Process->GpuVaRangeCount >= GPUVA_MAX_PROCESS_RANGES)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_QUOTA_EXCEEDED;
        }
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_NO_MEMORY;
        }
    }

    ExReleaseFastMutex(&Process->GpuVaLock);
    *OutAddress = ActualAddress;
    return STATUS_SUCCESS;
}

/*
 * DxgkGpuVaReserve
 *
 * Reserve a region of GPU VA space without mapping it to any allocation.
 * If BaseAddress is 0, a free region is found automatically.
 * On success, *OutAddress receives the actual VA.
 */
NTSTATUS
DxgkGpuVaReserve(
    _In_  PDXGKRNL_PROCESS         Process,
    _In_  D3DGPU_VIRTUAL_ADDRESS   BaseAddress,
    _In_  D3DGPU_VIRTUAL_ADDRESS   MinAddress,
    _In_  D3DGPU_VIRTUAL_ADDRESS   MaxAddress,
    _In_  ULONGLONG                SizeInBytes,
    _In_  D3DDDIGPUVIRTUALADDRESS_RESERVATION_TYPE ReservationType,
    _In_  UINT64                   DriverProtection,
    _Out_ D3DGPU_VIRTUAL_ADDRESS  *OutAddress)
{
    PDXGKRNL_GPUVA_RANGE Range;
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;

    PAGED_CODE();

    DPRINT("DxgkGpuVaReserve: Process=%p Base=0x%I64x Size=0x%I64x\n",
           Process, BaseAddress, SizeInBytes);

    if (Process == NULL || OutAddress == NULL || SizeInBytes == 0 || (SizeInBytes & GPUVA_RESERVATION_MASK) != 0 || ReservationType > D3DDDIGPUVIRTUALADDRESS_RESERVE_ZERO)
        return STATUS_INVALID_PARAMETER;

    *OutAddress = 0;
    if (BaseAddress != 0)
    {
        if ((BaseAddress & GPUVA_RESERVATION_MASK) != 0 || BaseAddress < GPUVA_START_ADDRESS || BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE || SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        if ((MinAddress != 0 && (MinAddress & GPUVA_RESERVATION_MASK) != 0) || (MaxAddress != 0 && (MaxAddress & GPUVA_RESERVATION_MASK) != 0))
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress == 0 || MaxAddress > GPUVA_DEFAULT_SPACE_SIZE)
            MaxAddress = GPUVA_DEFAULT_SPACE_SIZE;
        MinAddress = max(MinAddress, GPUVA_START_ADDRESS);
        if (MinAddress >= MaxAddress || SizeInBytes > MaxAddress - MinAddress)
            return STATUS_INVALID_PARAMETER;
    }

    Range = GpuVaAllocRange();
    if (Range == NULL)
        return STATUS_NO_MEMORY;

    ExAcquireFastMutex(&Process->GpuVaLock);

    if (Process->GpuVaRangeCount >= GPUVA_MAX_PROCESS_RANGES)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        GpuVaFreeRange(Range);
        return STATUS_QUOTA_EXCEEDED;
    }

    if (BaseAddress != 0)
    {
        ActualAddress = BaseAddress;

        /* Check for conflicts. */
        if (GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes) != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            DPRINT1("DxgkGpuVaReserve: conflict at 0x%I64x\n", ActualAddress);
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else
    {
        /* Find a free region. */
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            DPRINT1("DxgkGpuVaReserve: no free VA region for %I64u bytes\n",
                    SizeInBytes);
            return STATUS_NO_MEMORY;
        }
    }

    /* Populate the range. */
    Range->GpuVirtualAddress = ActualAddress;
    Range->SizeInBytes       = SizeInBytes;
    Range->State             = GpuVaStateReserved;
    Range->hAllocation       = NULL;
    Range->AllocationOffset  = 0;
    Range->Protection.NoAccess = ReservationType == D3DDDIGPUVIRTUALADDRESS_RESERVE_NO_ACCESS;
    Range->Protection.Zero = ReservationType == D3DDDIGPUVIRTUALADDRESS_RESERVE_ZERO;
    Range->DriverProtection = DriverProtection;
    Range->ReservationBase   = ActualAddress;
    Range->ReservationSize   = SizeInBytes;

    GpuVaInsertRange(Process, Range);
    Process->GpuVaRangeCount++;
    Process->GpuVaTotalReserved += SizeInBytes;

    ExReleaseFastMutex(&Process->GpuVaLock);

    *OutAddress = ActualAddress;

    DPRINT("DxgkGpuVaReserve: reserved at 0x%I64x size=0x%I64x\n",
           ActualAddress, SizeInBytes);

    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaFree
 *
 * Free a previously reserved or mapped GPU VA region.
 */
NTSTATUS
DxgkGpuVaFree(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ ULONGLONG              SizeInBytes)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    D3DGPU_VIRTUAL_ADDRESS ReservationBase;
    ULONGLONG ReservationSize;
    ULONGLONG MappedBytes = 0;
    LIST_ENTRY WorkingHead;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    ULONG AuthoritativeCount;
    ULONG WorkingCount;

    PAGED_CODE();

    DPRINT("DxgkGpuVaFree: Process=%p Base=0x%I64x Size=0x%I64x\n", Process, BaseAddress, SizeInBytes);

    if (Process == NULL || BaseAddress == 0 || SizeInBytes == 0 || (BaseAddress & GPUVA_PAGE_MASK) != 0 || (SizeInBytes & GPUVA_PAGE_MASK) != 0 || !GpuVaGetRangeEnd(BaseAddress, SizeInBytes, &EndAddress))
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&Process->GpuVaLock);
    if (!GpuVaCountList(&Process->GpuVaRangeList, GPUVA_MAX_PROCESS_RANGES, &AuthoritativeCount) || AuthoritativeCount != Process->GpuVaRangeCount)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_INTERNAL_ERROR;
    }
    if ((ULONGLONG)AuthoritativeCount * 2ULL + 2ULL > GPUVA_MAX_TRANSIENT_RANGES)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_QUOTA_EXCEEDED;
    }
    if (!GpuVaGetReservationForRange(&Process->GpuVaRangeList, BaseAddress, SizeInBytes, &ReservationBase, &ReservationSize))
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_INVALID_PARAMETER;
    }
    for (Entry = Process->GpuVaRangeList.Flink; Entry != &Process->GpuVaRangeList; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        D3DGPU_VIRTUAL_ADDRESS RangeEnd;

        if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_INTEGER_OVERFLOW;
        }
        if (RangeEnd <= BaseAddress)
            continue;
        if (Range->GpuVirtualAddress >= EndAddress)
            break;
        /* A system-use range (a monitored fence's value page) is owned by the
         * kernel object that established it; only that object's teardown may
         * remove it, otherwise a caller could free the mapping a fence packet
         * still resolves through. */
        if (Range->Protection.SystemUseOnly)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_INVALID_PARAMETER;
        }
        /* A submitted command buffer executes out of this address; the
         * miniport still holds it, so it cannot be taken away yet. */
        if (Range->SubmissionPinCount != 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_DEVICE_BUSY;
        }
        if (Range->State == GpuVaStateMapped)
        {
            D3DGPU_VIRTUAL_ADDRESS OverlapStart = max(Range->GpuVirtualAddress, BaseAddress);
            D3DGPU_VIRTUAL_ADDRESS OverlapEnd = min(RangeEnd, EndAddress);

            MappedBytes += OverlapEnd - OverlapStart;
        }
    }

    Status = GpuVaCloneList(&Process->GpuVaRangeList, &WorkingHead);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return Status;
    }
    Status = GpuVaSplitRangeAt(&WorkingHead, BaseAddress);
    if (NT_SUCCESS(Status))
        Status = GpuVaSplitRangeAt(&WorkingHead, EndAddress);
    if (!NT_SUCCESS(Status))
    {
        GpuVaFreeList(&WorkingHead);
        ExReleaseFastMutex(&Process->GpuVaLock);
        return Status;
    }

    GpuVaRemoveRangeSpan(&WorkingHead, BaseAddress, SizeInBytes);
    if (!GpuVaCountList(&WorkingHead, GPUVA_MAX_PROCESS_RANGES, &WorkingCount))
    {
        GpuVaFreeList(&WorkingHead);
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_QUOTA_EXCEEDED;
    }

    GpuVaFreeList(&Process->GpuVaRangeList);
    while (!IsListEmpty(&WorkingHead))
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(RemoveHeadList(&WorkingHead), DXGKRNL_GPUVA_RANGE, RangeListEntry);

        InitializeListHead(&Range->RangeListEntry);
        InsertTailList(&Process->GpuVaRangeList, &Range->RangeListEntry);
    }
    Process->GpuVaRangeCount = WorkingCount;
    Process->GpuVaTotalReserved -= SizeInBytes;
    GpuVaClearPteSpan(Process, BaseAddress, SizeInBytes);
    if (MappedBytes != 0)
        Process->GpuVaTotalMapped -= min(MappedBytes, Process->GpuVaTotalMapped);

    ExReleaseFastMutex(&Process->GpuVaLock);
    DPRINT("DxgkGpuVaFree: freed at 0x%I64x\n", BaseAddress);
    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaMap
 *
 * Map GPU VA to an allocation's backing pages (or apply a Zero/NoAccess
 * state mapping) by writing leaf PTEs with the CPU, per the adapter's
 * DXGK_PAGETABLEUPDATE_CPU_VIRTUAL declaration.  The target VA is either
 * caller-fixed (fresh space or wholly inside one existing reservation,
 * remap over mapped pages allowed) or picked from free space.
 * Synchronous: no paging packet, no paging fence.
 */
NTSTATUS
DxgkGpuVaMap(
    _In_ PDXGKRNL_ADAPTER       Adapter,
    _In_ PDXGKRNL_PROCESS       Process,
    _In_opt_ PDXGKVMM_ALLOCATION Allocation,
    _In_ D3DKMT_HANDLE          hAllocation,
    _In_ ULONGLONG              AllocationOffset,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ D3DGPU_VIRTUAL_ADDRESS MinAddress,
    _In_ D3DGPU_VIRTUAL_ADDRESS MaxAddress,
    _In_ ULONGLONG              SizeInBytes,
    _In_ D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection,
    _In_ UINT64                 DriverProtection,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress)
{
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;
    D3DGPU_VIRTUAL_ADDRESS ReservationBase;
    ULONGLONG ReservationSize;
    ULONGLONG PrevMappedBytes = 0;
    PDXGKRNL_GPUVA_BINDING Binding = NULL;
    BOOLEAN InReservation = FALSE;
    NTSTATUS Status;
    ULONG AuthoritativeCount;

    PAGED_CODE();

    DPRINT("DxgkGpuVaMap: Process=%p hAlloc=0x%x Base=0x%I64x Size=0x%I64x\n",
           Process, hAllocation, BaseAddress, SizeInBytes);

    if (Process == NULL || OutAddress == NULL || SizeInBytes == 0 ||
        (SizeInBytes & GPUVA_PAGE_MASK) != 0 ||
        (BaseAddress & GPUVA_PAGE_MASK) != 0 ||
        (AllocationOffset & GPUVA_PAGE_MASK) != 0 ||
        !GpuVaProtectionValid(Protection, FALSE))
        return STATUS_INVALID_PARAMETER;
    if ((Protection.Zero || Protection.NoAccess) != (Allocation == NULL))
        return STATUS_INVALID_PARAMETER;
    if (Adapter == NULL || !Adapter->GpuMmuCapsValid ||
        Adapter->GpuMmuCaps.PageTableUpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
        return STATUS_NOT_SUPPORTED;
    if (Allocation != NULL && Allocation->SystemMemory == NULL && !Allocation->Resident)
        return STATUS_NOT_SUPPORTED;

    *OutAddress = 0;
    if (BaseAddress != 0)
    {
        if (BaseAddress < GPUVA_START_ADDRESS ||
            BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE ||
            SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        if ((MinAddress & GPUVA_PAGE_MASK) != 0 || (MaxAddress & GPUVA_PAGE_MASK) != 0)
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress == 0 || MaxAddress > GPUVA_DEFAULT_SPACE_SIZE)
            MaxAddress = GPUVA_DEFAULT_SPACE_SIZE;
        MinAddress = max(MinAddress, GPUVA_START_ADDRESS);
        if (MinAddress >= MaxAddress || SizeInBytes > MaxAddress - MinAddress)
            return STATUS_INVALID_PARAMETER;
    }

    if (Allocation != NULL)
    {
        Status = GpuVaCreateBinding(Adapter, Process, hAllocation, &Binding);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Binding->BackingAllocation != Allocation)
        {
            GpuVaDereferenceBinding(Binding);
            return STATUS_INVALID_HANDLE;
        }
        Allocation = Binding->BackingAllocation;
    }

    ExAcquireFastMutex(&Process->GpuVaLock);

    if (Binding != NULL && InterlockedCompareExchange(&Binding->LogicalAllocation->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        GpuVaDereferenceBinding(Binding);
        return STATUS_DELETE_PENDING;
    }

    if (!GpuVaCountList(&Process->GpuVaRangeList, GPUVA_MAX_PROCESS_RANGES, &AuthoritativeCount) || AuthoritativeCount != Process->GpuVaRangeCount)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        if (Binding != NULL)
            GpuVaDereferenceBinding(Binding);
        return STATUS_INTERNAL_ERROR;
    }
    if ((ULONGLONG)AuthoritativeCount * 2ULL + 3ULL > GPUVA_MAX_TRANSIENT_RANGES)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        if (Binding != NULL)
            GpuVaDereferenceBinding(Binding);
        return STATUS_QUOTA_EXCEEDED;
    }

    Status = GpuVaEnsureRootPageTable(Process);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        if (Binding != NULL)
            GpuVaDereferenceBinding(Binding);
        return Status;
    }

    if (BaseAddress != 0)
    {
        PDXGKRNL_GPUVA_RANGE Existing;

        ActualAddress = BaseAddress;
        /* A system-use range belongs to the kernel object that established
         * it; remapping over one would retarget a live fence mapping. */
        Existing = GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes);
        if (Existing != NULL && Existing->Protection.SystemUseOnly)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_INVALID_PARAMETER;
        }
        /* Remapping over a range a submitted buffer executes from would
         * retarget the address the miniport is already running. */
        if (GpuVaListRangeIsPinned(&Process->GpuVaRangeList, ActualAddress, SizeInBytes))
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_DEVICE_BUSY;
        }
        if (GpuVaGetReservationForRange(&Process->GpuVaRangeList, ActualAddress, SizeInBytes, &ReservationBase, &ReservationSize))
        {
            InReservation = TRUE;
        }
        else if (Existing != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else
    {
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_NO_MEMORY;
        }
    }

    if (!InReservation)
    {
        PDXGKRNL_GPUVA_RANGE Range;

        if (Process->GpuVaRangeCount >= GPUVA_MAX_PROCESS_RANGES)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_QUOTA_EXCEEDED;
        }
        Range = GpuVaAllocRange();
        if (Range == NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_NO_MEMORY;
        }

        Range->Binding = Binding;
        Binding = NULL;

        Status = GpuVaWritePteSpan(Process, ActualAddress, SizeInBytes, Allocation, AllocationOffset, Protection);
        if (!NT_SUCCESS(Status))
        {
            GpuVaClearPteSpan(Process, ActualAddress, SizeInBytes);
            GpuVaFreeRange(Range);
            ExReleaseFastMutex(&Process->GpuVaLock);
            return Status;
        }

        Range->GpuVirtualAddress = ActualAddress;
        Range->SizeInBytes       = SizeInBytes;
        Range->State             = (Allocation != NULL) ? GpuVaStateMapped : GpuVaStateReserved;
        Range->hAllocation       = (Allocation != NULL) ? (HANDLE)(ULONG_PTR)hAllocation : NULL;
        Range->AllocationOffset  = (Allocation != NULL) ? AllocationOffset : 0;
        Range->Protection        = Protection;
        Range->DriverProtection  = DriverProtection;
        Range->ReservationBase   = ActualAddress;
        Range->ReservationSize   = SizeInBytes;

        GpuVaInsertRange(Process, Range);
        Process->GpuVaRangeCount++;
        Process->GpuVaTotalReserved += SizeInBytes;
        if (Allocation != NULL)
            Process->GpuVaTotalMapped += SizeInBytes;
    }
    else
    {
        LIST_ENTRY WorkingHead;
        LIST_ENTRY ReplacementHead;
        PDXGKRNL_GPUVA_RANGE Replacement;
        PLIST_ENTRY Entry;
        ULONG WorkingCount;
        D3DGPU_VIRTUAL_ADDRESS SpanEnd = ActualAddress + SizeInBytes;

        for (Entry = Process->GpuVaRangeList.Flink; Entry != &Process->GpuVaRangeList; Entry = Entry->Flink)
        {
            PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
            D3DGPU_VIRTUAL_ADDRESS RangeEnd;

            if (!GpuVaGetRangeEnd(Range->GpuVirtualAddress, Range->SizeInBytes, &RangeEnd))
                continue;
            if (RangeEnd <= ActualAddress)
                continue;
            if (Range->GpuVirtualAddress >= SpanEnd)
                break;
            if (Range->State == GpuVaStateMapped)
                PrevMappedBytes += min(RangeEnd, SpanEnd) - max(Range->GpuVirtualAddress, ActualAddress);
        }

        Replacement = GpuVaAllocRange();
        if (Replacement == NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_NO_MEMORY;
        }
        Replacement->GpuVirtualAddress = ActualAddress;
        Replacement->SizeInBytes       = SizeInBytes;
        Replacement->State             = (Allocation != NULL) ? GpuVaStateMapped : GpuVaStateReserved;
        Replacement->hAllocation       = (Allocation != NULL) ? (HANDLE)(ULONG_PTR)hAllocation : NULL;
        Replacement->Binding           = Binding;
        Binding = NULL;
        Replacement->AllocationOffset  = (Allocation != NULL) ? AllocationOffset : 0;
        Replacement->Protection        = Protection;
        Replacement->DriverProtection  = DriverProtection;
        Replacement->ReservationBase   = ReservationBase;
        Replacement->ReservationSize   = ReservationSize;

        Status = GpuVaCloneList(&Process->GpuVaRangeList, &WorkingHead);
        if (!NT_SUCCESS(Status))
        {
            GpuVaFreeRange(Replacement);
            ExReleaseFastMutex(&Process->GpuVaLock);
            return Status;
        }

        InitializeListHead(&ReplacementHead);
        InsertTailList(&ReplacementHead, &Replacement->RangeListEntry);
        Status = GpuVaReplaceSpan(&WorkingHead, ActualAddress, SizeInBytes, &ReplacementHead);
        GpuVaFreeList(&ReplacementHead);
        if (NT_SUCCESS(Status) && !GpuVaCountList(&WorkingHead, GPUVA_MAX_PROCESS_RANGES, &WorkingCount))
            Status = STATUS_QUOTA_EXCEEDED;
        if (NT_SUCCESS(Status))
        {
            Status = GpuVaWritePteSpan(Process, ActualAddress, SizeInBytes, Allocation, AllocationOffset, Protection);
            if (!NT_SUCCESS(Status))
            {
                NTSTATUS RollbackStatus = GpuVaRewriteSpanPtes(Adapter, Process, ActualAddress, SizeInBytes);

                if (!NT_SUCCESS(RollbackStatus))
                {
                    DPRINT1("DxgkGpuVaMap: PTE rollback failed 0x%08lx after map failure 0x%08lx\n", RollbackStatus, Status);
                    Status = RollbackStatus;
                }
            }
        }
        if (!NT_SUCCESS(Status))
        {
            GpuVaFreeList(&WorkingHead);
            ExReleaseFastMutex(&Process->GpuVaLock);
            return Status;
        }

        GpuVaFreeList(&Process->GpuVaRangeList);
        GpuVaMoveList(&Process->GpuVaRangeList, &WorkingHead);
        Process->GpuVaRangeCount = WorkingCount;
        Process->GpuVaTotalMapped -= min(PrevMappedBytes, Process->GpuVaTotalMapped);
        if (Allocation != NULL)
            Process->GpuVaTotalMapped += SizeInBytes;
    }

    ExReleaseFastMutex(&Process->GpuVaLock);

    *OutAddress = ActualAddress;
    DPRINT("DxgkGpuVaMap: mapped at 0x%I64x size=0x%I64x\n", ActualAddress, SizeInBytes);
    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaMapFencePage
 *
 * Map a kernel-owned nonpaged page (a monitored-fence value page) into the
 * process GPU VA space so the GPU can address the fence value.  The range
 * is system-use: mapped, writable, with no allocation handle behind it.
 */
NTSTATUS
DxgkGpuVaMapFencePage(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process,
    _In_ PVOID             KernelVa,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress)
{
    PDXGKRNL_GPUVA_RANGE Range;
    PDXGKRNL_GPUVA_PAGE_TABLE Leaf;
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;
    PHYSICAL_ADDRESS Physical;
    DXGK_PTE *Entries;
    DXGK_PTE Pte;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Process == NULL || KernelVa == NULL || OutAddress == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAddress = 0;
    if (!Adapter->GpuMmuCapsValid ||
        Adapter->GpuMmuCaps.PageTableUpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
        return STATUS_NOT_SUPPORTED;

    Range = GpuVaAllocRange();
    if (Range == NULL)
        return STATUS_NO_MEMORY;

    ExAcquireFastMutex(&Process->GpuVaLock);

    if (Process->GpuVaRangeCount >= GPUVA_MAX_PROCESS_RANGES)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        GpuVaFreeRange(Range);
        return STATUS_QUOTA_EXCEEDED;
    }
    Status = GpuVaEnsureRootPageTable(Process);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        GpuVaFreeRange(Range);
        return Status;
    }
    ActualAddress = GpuVaFindFreeRegion(Process, GPUVA_START_ADDRESS,
                                        GPUVA_DEFAULT_SPACE_SIZE, GPUVA_PAGE_SIZE);
    if (ActualAddress == 0)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        GpuVaFreeRange(Range);
        return STATUS_NO_MEMORY;
    }
    Leaf = GpuVaGetLeafTable(Process, ActualAddress, TRUE);
    if (Leaf == NULL)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        GpuVaFreeRange(Range);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Physical = MmGetPhysicalAddress(KernelVa);
    RtlZeroMemory(&Pte, sizeof(Pte));
    Pte.Valid = 1;
    Pte.CacheCoherent = 1;
    Pte.NoExecute = 1;
    Pte.PageAddress = (ULONGLONG)Physical.QuadPart & ~GPUVA_PAGE_MASK;
    Entries = (DXGK_PTE *)Leaf->KernelVa;
    {
        ULONG Index = GpuVaPteIndexFor(Process->Adapter, ActualAddress, 0);

        Entries[Index] = Pte;
        if (!NT_SUCCESS(GpuVaNotifyPageTableUpdate(Process, Leaf, Index, 1, ActualAddress, FALSE)))
        {
            Entries[Index].Flags = 0;
            Entries[Index].PageAddress = 0;
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }
    }

    Range->GpuVirtualAddress = ActualAddress;
    Range->SizeInBytes       = GPUVA_PAGE_SIZE;
    Range->State             = GpuVaStateMapped;
    Range->hAllocation       = NULL;
    Range->AllocationOffset  = 0;
    Range->Protection.Write  = 1;
    Range->Protection.SystemUseOnly = 1;
    Range->ReservationBase   = ActualAddress;
    Range->ReservationSize   = GPUVA_PAGE_SIZE;

    GpuVaInsertRange(Process, Range);
    Process->GpuVaRangeCount++;
    Process->GpuVaTotalReserved += GPUVA_PAGE_SIZE;
    Process->GpuVaTotalMapped   += GPUVA_PAGE_SIZE;

    ExReleaseFastMutex(&Process->GpuVaLock);

    *OutAddress = ActualAddress;
    DPRINT("DxgkGpuVaMapFencePage: fence page at 0x%I64x\n", ActualAddress);
    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaUnmapFencePage
 * Remove a fence-page mapping created by DxgkGpuVaMapFencePage.  The caller
 * releases the nonpaged backing immediately afterwards, so the PTE clear must
 * reach the miniport/TLB before this function returns.  Leaving the update in
 * Process->PageTableUpdatePending would let the GPU keep a translation to a
 * page that has already gone back to the pool.
 */
NTSTATUS
DxgkGpuVaUnmapFencePage(
    _In_ PDXGKRNL_PROCESS       Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address)
{
    PLIST_ENTRY Entry;
    BOOLEAN Unmapped = FALSE;

    if (Process == NULL || Address == 0)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&Process->GpuVaLock);
    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        if (Range->GpuVirtualAddress != Address)
            continue;
        if (Range->SizeInBytes != GPUVA_PAGE_SIZE ||
            Range->State != GpuVaStateMapped ||
            Range->hAllocation != NULL ||
            !Range->Protection.SystemUseOnly)
            break;

        RemoveEntryList(&Range->RangeListEntry);
        GpuVaFreeRange(Range);
        if (Process->GpuVaRangeCount != 0)
            Process->GpuVaRangeCount--;
        Process->GpuVaTotalReserved -= min(GPUVA_PAGE_SIZE, Process->GpuVaTotalReserved);
        Process->GpuVaTotalMapped -= min(GPUVA_PAGE_SIZE, Process->GpuVaTotalMapped);
        GpuVaClearPteSpan(Process, Address, GPUVA_PAGE_SIZE);
        Unmapped = TRUE;
        break;
    }
    ExReleaseFastMutex(&Process->GpuVaLock);

    if (!Unmapped)
        return STATUS_NOT_FOUND;

    /*
     * GpuVaClearPteSpan only records the invalidation while GpuVaLock is held.
     * Flush after dropping the lock, before sync.c frees the fence page.
     */
    return DxgkGpuVaFlushPageTableUpdates(Process);
}

VOID
DxgkGpuVaInvalidateAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ PDXGKVMM_ALLOCATION LogicalAllocation)
{
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || LogicalAllocation == NULL)
        return;

    ExAcquireFastMutex(&Process->GpuVaLock);
    for (Entry = Process->GpuVaRangeList.Flink; Entry != &Process->GpuVaRangeList; Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

        if (Range->State != GpuVaStateMapped || Range->Protection.SystemUseOnly || Range->Binding == NULL || Range->Binding->LogicalAllocation != LogicalAllocation)
            continue;
        GpuVaClearPteSpan(Process, Range->GpuVirtualAddress, Range->SizeInBytes);
        Process->GpuVaTotalMapped -= min(Range->SizeInBytes, Process->GpuVaTotalMapped);
        Range->State = GpuVaStateReserved;
        Range->hAllocation = NULL;
        GpuVaDereferenceBinding(Range->Binding);
        Range->Binding = NULL;
        Range->AllocationOffset = 0;
        Range->Protection.Value = 0;
        Range->Protection.NoAccess = 1;
        Range->DriverProtection = 0;
    }
    KeMemoryBarrier();
    ExReleaseFastMutex(&Process->GpuVaLock);
}

BOOLEAN
DxgkGpuVaPageTableReady(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process)
{
    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || Process->hMiniportProcess == NULL || Process->hRootPageTable == NULL || Process->RootPageTableEntries == 0 || !Process->RootPageTableProgrammed)
        return FALSE;
    if (Adapter->MiniportContext == NULL ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
        return FALSE;
    if (DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiGetRootPageTableSize) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiSetRootPageTable) == NULL)
        return FALSE;
    return TRUE;
}

BOOLEAN
DxgkGpuVaValidateRange(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    BOOLEAN Valid;

    PAGED_CODE();

    if (!DxgkGpuVaPageTableReady(Adapter, Process))
        return FALSE;

    ExAcquireFastMutex(&Process->GpuVaLock);
    Valid = GpuVaListCoversRange(&Process->GpuVaRangeList, Address, Size, GpuVaStateMapped);
    ExReleaseFastMutex(&Process->GpuVaLock);
    return Valid;
}

/*
 * DxgkGpuVaPinRange
 *
 * Validates and pins in one step under GpuVaLock.  Validating and then
 * submitting without a pin is a time-of-check/time-of-use hole: the range
 * could be unmapped or remapped before the miniport reads the address.
 */
BOOLEAN
DxgkGpuVaPinRange(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    BOOLEAN Valid;

    PAGED_CODE();

    if (Process == NULL || Address == 0 || Size == 0)
        return FALSE;
    if (!DxgkGpuVaPageTableReady(Adapter, Process))
        return FALSE;

    ExAcquireFastMutex(&Process->GpuVaLock);
    Valid = GpuVaListCoversRange(&Process->GpuVaRangeList, Address, Size, GpuVaStateMapped);
    if (Valid &&
        Adapter->GpuMmuCaps.NoExecuteMemorySupported &&
        !GpuVaListAllowsExecute(&Process->GpuVaRangeList,
                                Address,
                                Size))
    {
        Valid = FALSE;
    }
    if (Valid)
        GpuVaListAdjustPin(&Process->GpuVaRangeList, Address, Size, 1);
    ExReleaseFastMutex(&Process->GpuVaLock);
    return Valid;
}

VOID
DxgkGpuVaUnpinRange(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    if (Process == NULL || Address == 0 || Size == 0)
        return;
    ExAcquireFastMutex(&Process->GpuVaLock);
    GpuVaListAdjustPin(&Process->GpuVaRangeList, Address, Size, -1);
    ExReleaseFastMutex(&Process->GpuVaLock);
}

static NTSTATUS
DxgkpGpuVaUpdateWorker(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_reads_(NumOperations) CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operations,
    _In_ UINT NumOperations,
    _In_ BOOLEAN Commit)
{
    LIST_ENTRY WorkingHead;
    LIST_ENTRY PreviousHead;
    D3DGPU_VIRTUAL_ADDRESS ExpectedReservationBase = 0;
    D3DGPU_VIRTUAL_ADDRESS ExpectedSourceReservationBase = 0;
    ULONGLONG ExpectedReservationSize = 0;
    ULONGLONG ExpectedSourceReservationSize = 0;
    NTSTATUS Status;
    UINT Index;
    ULONG AuthoritativeCount;
    ULONG WorkingCount;
    ULONGLONG PreviousMappedBytes;

    PAGED_CODE();

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || Operations == NULL || NumOperations == 0)
        return STATUS_INVALID_PARAMETER;
    if (Commit && (!Adapter->GpuMmuCapsValid || Adapter->GpuMmuCaps.PageTableUpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL))
        return STATUS_NOT_SUPPORTED;
    ExAcquireFastMutex(&Process->GpuVaLock);
    if (Commit)
    {
        Status = GpuVaEnsureRootPageTable(Process);
        if (!NT_SUCCESS(Status))
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return Status;
        }
    }
    if (!GpuVaCountList(&Process->GpuVaRangeList, GPUVA_MAX_PROCESS_RANGES, &AuthoritativeCount) || AuthoritativeCount != Process->GpuVaRangeCount)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_INTERNAL_ERROR;
    }
    if ((ULONGLONG)AuthoritativeCount * 3ULL + 2ULL > GPUVA_MAX_TRANSIENT_RANGES)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_QUOTA_EXCEEDED;
    }
    Status = GpuVaCloneList(&Process->GpuVaRangeList, &WorkingHead);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return Status;
    }
    if (!GpuVaCountList(&WorkingHead, GPUVA_MAX_PROCESS_RANGES, &WorkingCount) || WorkingCount != AuthoritativeCount)
    {
        GpuVaFreeList(&WorkingHead);
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_INTERNAL_ERROR;
    }

    for (Index = 0; Index < NumOperations; ++Index)
    {
        D3DGPU_VIRTUAL_ADDRESS Address;
        D3DGPU_VIRTUAL_ADDRESS ReservationBase;
        ULONGLONG Size;
        ULONGLONG ReservationSize;

        switch (Operations[Index].OperationType)
        {
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP:
                Address = Operations[Index].Map.BaseAddress;
                Size = Operations[Index].Map.SizeInBytes;
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_UNMAP:
                Address = Operations[Index].Unmap.BaseAddress;
                Size = Operations[Index].Unmap.SizeInBytes;
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_COPY:
                Address = Operations[Index].Copy.DestAddress;
                Size = Operations[Index].Copy.SizeInBytes;
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP_PROTECT:
                Address = Operations[Index].MapProtect.BaseAddress;
                Size = Operations[Index].MapProtect.SizeInBytes;
                break;
            default:
                Status = STATUS_INVALID_PARAMETER;
                goto ValidationDone;
        }

        if (Size == 0 || (Address & GPUVA_PAGE_MASK) != 0 || (Size & GPUVA_PAGE_MASK) != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto ValidationDone;
        }
        if (!GpuVaGetReservationForRange(&WorkingHead, Address, Size, &ReservationBase, &ReservationSize))
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto ValidationDone;
        }
        if (ExpectedReservationSize == 0)
        {
            ExpectedReservationBase = ReservationBase;
            ExpectedReservationSize = ReservationSize;
        }
        else if (ReservationBase != ExpectedReservationBase || ReservationSize != ExpectedReservationSize)
        {
            Status = STATUS_CONFLICTING_ADDRESSES;
            goto ValidationDone;
        }

        if (Operations[Index].OperationType == D3DDDI_UPDATEGPUVIRTUALADDRESS_COPY)
        {
            D3DGPU_VIRTUAL_ADDRESS SourceReservationBase;
            ULONGLONG SourceReservationSize;

            if (!GpuVaGetReservationForRange(&WorkingHead, Operations[Index].Copy.SourceAddress, Operations[Index].Copy.SizeInBytes, &SourceReservationBase, &SourceReservationSize))
            {
                Status = STATUS_CONFLICTING_ADDRESSES;
                goto ValidationDone;
            }
            if (ExpectedSourceReservationSize == 0)
            {
                ExpectedSourceReservationBase = SourceReservationBase;
                ExpectedSourceReservationSize = SourceReservationSize;
            }
            else if (SourceReservationBase != ExpectedSourceReservationBase || SourceReservationSize != ExpectedSourceReservationSize)
            {
                Status = STATUS_CONFLICTING_ADDRESSES;
                goto ValidationDone;
            }
        }

        switch (Operations[Index].OperationType)
        {
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP:
                Status = GpuVaApplyMapOperation(&WorkingHead, Adapter, Process, &Operations[Index], FALSE);
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_UNMAP:
                Status = GpuVaApplyUnmapOperation(&WorkingHead, &Operations[Index]);
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_COPY:
                Status = GpuVaApplyCopyOperation(&WorkingHead, &Operations[Index]);
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP_PROTECT:
                Status = GpuVaApplyMapOperation(&WorkingHead, Adapter, Process, &Operations[Index], TRUE);
                break;
            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
        }

        if (!NT_SUCCESS(Status))
            break;
        if (!GpuVaCountList(&WorkingHead, GPUVA_MAX_PROCESS_RANGES, &WorkingCount))
        {
            Status = STATUS_QUOTA_EXCEEDED;
            break;
        }
    }

ValidationDone:
    if (Commit && NT_SUCCESS(Status))
    {
        ULONGLONG TotalMapped = 0;
        PLIST_ENTRY Entry;
        NTSTATUS RollbackStatus = STATUS_SUCCESS;

        InitializeListHead(&PreviousHead);
        PreviousMappedBytes = Process->GpuVaTotalMapped;
        GpuVaMoveList(&PreviousHead, &Process->GpuVaRangeList);
        GpuVaMoveList(&Process->GpuVaRangeList, &WorkingHead);
        Process->GpuVaRangeCount = WorkingCount;

        for (Index = 0; Index < NumOperations; ++Index)
        {
            D3DGPU_VIRTUAL_ADDRESS Address;
            ULONGLONG Size;
            NTSTATUS PteStatus;

            if (!GpuVaOperationSpan(&Operations[Index], &Address, &Size))
                continue;
            PteStatus = GpuVaRewriteSpanPtes(Adapter, Process, Address, Size);
            if (!NT_SUCCESS(PteStatus))
            {
                Status = PteStatus;
                break;
            }
        }

        if (!NT_SUCCESS(Status))
        {
            GpuVaMoveList(&WorkingHead, &Process->GpuVaRangeList);
            GpuVaMoveList(&Process->GpuVaRangeList, &PreviousHead);
            Process->GpuVaRangeCount = AuthoritativeCount;
            Process->GpuVaTotalMapped = PreviousMappedBytes;
            for (Index = 0; Index < NumOperations; ++Index)
            {
                D3DGPU_VIRTUAL_ADDRESS Address;
                ULONGLONG Size;
                NTSTATUS PteStatus;

                if (!GpuVaOperationSpan(&Operations[Index], &Address, &Size))
                    continue;
                PteStatus = GpuVaRewriteSpanPtes(Adapter, Process, Address, Size);
                if (!NT_SUCCESS(PteStatus) && NT_SUCCESS(RollbackStatus))
                    RollbackStatus = PteStatus;
            }
            if (!NT_SUCCESS(RollbackStatus))
            {
                DPRINT1("DxgkGpuVaApplyUpdate: PTE rollback failed 0x%08lx after update failure 0x%08lx\n", RollbackStatus, Status);
                Status = RollbackStatus;
            }
        }
        else for (Entry = Process->GpuVaRangeList.Flink; Entry != &Process->GpuVaRangeList; Entry = Entry->Flink)
        {
            PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

            if (Range->State == GpuVaStateMapped)
                TotalMapped += Range->SizeInBytes;
        }
        if (NT_SUCCESS(Status))
        {
            Process->GpuVaTotalMapped = TotalMapped;
            GpuVaFreeList(&PreviousHead);
        }
    }
    GpuVaFreeList(&WorkingHead);
    ExReleaseFastMutex(&Process->GpuVaLock);
    return Status;
}

NTSTATUS
DxgkGpuVaValidateUpdate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_reads_(NumOperations) CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operations,
    _In_ UINT NumOperations)
{
    return DxgkpGpuVaUpdateWorker(Adapter, Process, Operations, NumOperations, FALSE);
}

/*
 * DxgkGpuVaApplyUpdate
 *
 * Validate and COMMIT a batch of UpdateGpuVirtualAddress operations: the
 * fully-updated working list becomes authoritative and the leaf PTEs of
 * every touched span are rewritten from it (CPU_VIRTUAL mode).
 */
NTSTATUS
DxgkGpuVaApplyUpdate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_reads_(NumOperations) CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operations,
    _In_ UINT NumOperations)
{
    return DxgkpGpuVaUpdateWorker(Adapter, Process, Operations, NumOperations, TRUE);
}


/* ROOT PAGE TABLE ************************************************************/

/*
 * DxgkGpuVaSetRootPageTable
 *
 * Programs the GPU MMU with the process's root page table for a context.
 * Calls DxgkDdiSetRootPageTable on the miniport.
 */
NTSTATUS
DxgkGpuVaSetRootPageTable(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ PDXGKRNL_PROCESS  Process,
    _In_ PDXGKRNL_CONTEXT  Context)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaSetRootPageTable: Adapter=%p Process=%p Context=%p\n",
           Adapter, Process, Context);

    if (Adapter == NULL || Process == NULL || Context == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Context->Device == NULL || Context->Device->Adapter != Adapter || Context->Device->ProcessRecord != Process)
        return STATUS_INVALID_HANDLE;
    if (InterlockedCompareExchange(&Context->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        return STATUS_DEVICE_REMOVED;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (Adapter->GpuMmuCapsValid &&
        Adapter->GpuMmuCaps.PageTableUpdateMode == DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
    {
        NTSTATUS Status;

        ExAcquireFastMutex(&Process->GpuVaLock);
        Status = GpuVaEnsureRootPageTable(Process);
        ExReleaseFastMutex(&Process->GpuVaLock);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (!DxgkGpuVaPageTableReady(Adapter, Process))
        return STATUS_NOT_SUPPORTED;

    {
        DXGKARG_SETROOTPAGETABLE SetArgs;

        RtlZeroMemory(&SetArgs, sizeof(SetArgs));
        SetArgs.hContext = Context->hMiniportContext;
        SetArgs.Address = Process->RootPageTableAddress;
        SetArgs.NumEntries = Process->RootPageTableEntries;
        if (!DxgkAcquireKmdCall(Adapter))
            return STATUS_DELETE_PENDING;
        if (InterlockedCompareExchange(&Context->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        {
            DxgkReleaseKmdCall(Adapter);
            return STATUS_DEVICE_REMOVED;
        }
        DXGK_CB_FULL(Adapter, DxgkDdiSetRootPageTable)(Adapter->MiniportDeviceContext, &SetArgs);
        DxgkReleaseKmdCall(Adapter);
        DPRINT("DxgkGpuVaSetRootPageTable: set for context %p addr=0x%I64x entries=%u\n", Context, SetArgs.Address.SegmentOffset, SetArgs.NumEntries);
    }
#else
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
#endif

    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaGetRootPageTableSize
 *
 * Query the miniport for the root page table allocation size
 * needed to cover NumberOfPte entries.
 */
SIZE_T
DxgkGpuVaGetRootPageTableSize(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ UINT              NumberOfPte,
    _In_ UINT              PhysicalAdapterIndex)
{
    PAGED_CODE();

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (Adapter != NULL &&
        DXGK_CB_FULL(Adapter, DxgkDdiGetRootPageTableSize) != NULL)
    {
        DXGKARG_GETROOTPAGETABLESIZE Args;
        SIZE_T Size;

        Args.NumberOfPte       = NumberOfPte;
        Args.PhysicalAdapterIndex = PhysicalAdapterIndex;

        if (!DxgkAcquireKmdCall(Adapter))
            return 0;
        Size = DXGK_CB_FULL(Adapter, DxgkDdiGetRootPageTableSize)(Adapter->MiniportDeviceContext, &Args);
        DxgkReleaseKmdCall(Adapter);

        DPRINT("DxgkGpuVaGetRootPageTableSize: NumberOfPte=%u size=%Iu\n",
               NumberOfPte, Size);
        return Size;
    }
#else
    UNREFERENCED_PARAMETER(NumberOfPte);
    UNREFERENCED_PARAMETER(PhysicalAdapterIndex);
#endif

    return 0;
}


/* RESIDENCY MANAGEMENT *******************************************************/

/*
 * DxgkGpuVaMakeResident
 *
 * Execute the WDDM 2.0 MakeResident residency-reference transaction after the
 * end-to-end residency gate is enabled.  Nonempty work remains unobservable
 * while paging execution, completion, budgets, and teardown are incomplete.
 */
NTSTATUS
DxgkGpuVaMakeResident(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_opt_ PDXGKRNL_DEVICE   Device,
    _In_    D3DKMT_HANDLE      hPagingSyncObject,
    _Inout_opt_ volatile LONG64 *PagingFenceCounter,
    _In_reads_(NumAllocations) CONST D3DKMT_HANDLE *AllocationList,
    _In_    ULONG              NumAllocations,
    _In_    BOOLEAN            CantTrimFurther,
    _In_    BOOLEAN            MustSucceed,
    _Out_   ULONG             *OutCompleted,
    _Out_   ULONGLONG         *OutNumBytesToTrim,
    _Out_   ULONGLONG         *OutPagingFenceValue)
{
    PDXGKVMM_ALLOCATION *Allocations;
    BOOLEAN PagingQueued = FALSE;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    DPRINT("DxgkGpuVaMakeResident: %lu allocations\n", NumAllocations);

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || AllocationList == NULL || NumAllocations == 0 || OutCompleted == NULL || OutNumBytesToTrim == NULL || OutPagingFenceValue == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutCompleted = 0;
    *OutNumBytesToTrim = 0;
    *OutPagingFenceValue = 0;

    if (NumAllocations > MAXLONG ||
        (SIZE_T)NumAllocations > MAXULONG_PTR / sizeof(*Allocations))
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    Allocations = ExAllocatePoolWithTag(PagedPool,
                                        (SIZE_T)NumAllocations *
                                            sizeof(*Allocations),
                                        TAG_DXGK_GPUVA);
    if (Allocations == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Allocations,
                  (SIZE_T)NumAllocations * sizeof(*Allocations));

    /*
     * Capture every physical backing before the first residency mutation.
     * Rollback therefore never re-resolves a logical handle that destroy may
     * already have unpublished.
     */
    for (i = 0; i < NumAllocations; ++i)
    {
        Status = DxgkVidMmReferenceProcessAllocation(
                     (HANDLE)(ULONG_PTR)AllocationList[i],
                     Adapter,
                     Process,
                     &Allocations[i]);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Status = DxgkVidMmMakeResidentBatch(Adapter,
                                        Process,
                                        Device,
                                        Allocations,
                                        NumAllocations,
                                        hPagingSyncObject,
                                        PagingFenceCounter,
                                        CantTrimFurther,
                                        MustSucceed,
                                        OutNumBytesToTrim,
                                        OutPagingFenceValue,
                                        &PagingQueued);
    if (NT_SUCCESS(Status))
        *OutCompleted = NumAllocations;

Cleanup:
    for (i = 0; i < NumAllocations; ++i)
    {
        if (Allocations[i] != NULL)
            DxgkVidMmDereferenceAllocation(Allocations[i]);
    }
    ExFreePoolWithTag(Allocations, TAG_DXGK_GPUVA);
    if (!NT_SUCCESS(Status))
        return Status;
    /* Queued paging work completes through the paging queue's monitored
     * fence; the caller waits *OutPagingFenceValue per the native contract. */
    return PagingQueued ? STATUS_PENDING : STATUS_SUCCESS;
}


/*
 * DxgkGpuVaEvict
 *
 * Execute the WDDM 2.0 Evict residency-reference transaction after the
 * end-to-end residency gate is enabled.  Nonempty work remains unobservable
 * while paging execution, completion, budgets, and teardown are incomplete.
 */
NTSTATUS
DxgkGpuVaEvict(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_opt_ PDXGKRNL_DEVICE   Device,
    _In_reads_(NumAllocations) CONST D3DKMT_HANDLE *AllocationList,
    _In_    ULONG              NumAllocations,
    _In_    BOOLEAN            EvictOnlyIfNecessary,
    _Out_   ULONGLONG         *OutNumBytesToTrim)
{
    PDXGKVMM_ALLOCATION *Allocations;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    DPRINT("DxgkGpuVaEvict: %lu allocations\n", NumAllocations);

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || AllocationList == NULL || NumAllocations == 0 || OutNumBytesToTrim == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutNumBytesToTrim = 0;
    if (NumAllocations > MAXLONG ||
        (SIZE_T)NumAllocations > MAXULONG_PTR / sizeof(*Allocations))
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    Allocations = ExAllocatePoolWithTag(PagedPool,
                                        (SIZE_T)NumAllocations *
                                            sizeof(*Allocations),
                                        TAG_DXGK_GPUVA);
    if (Allocations == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Allocations,
                  (SIZE_T)NumAllocations * sizeof(*Allocations));

    for (i = 0; i < NumAllocations; ++i)
    {
        Status = DxgkVidMmReferenceProcessAllocation(
                     (HANDLE)(ULONG_PTR)AllocationList[i],
                     Adapter,
                     Process,
                     &Allocations[i]);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    Status = DxgkVidMmEvictBatch(Adapter,
                                 Device,
                                 Allocations,
                                 NumAllocations,
                                 EvictOnlyIfNecessary,
                                 OutNumBytesToTrim);

Cleanup:
    for (i = 0; i < NumAllocations; ++i)
    {
        if (Allocations[i] != NULL)
            DxgkVidMmDereferenceAllocation(Allocations[i]);
    }
    ExFreePoolWithTag(Allocations, TAG_DXGK_GPUVA);
    return Status;
}


/* CPU HOST APERTURE **********************************************************/

/*
 * DxgkGpuVaMapCpuHostAperture
 *
 * Map GPU memory into a CPU-visible aperture segment via the miniport.
 */
NTSTATUS
DxgkGpuVaMapCpuHostAperture(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ HANDLE            hAllocation,
    _In_ WORD              SegmentId,
    _In_ UINT64            NumberOfPages,
    _In_ UINT32           *pCpuHostAperturePages,
    _In_ UINT64           *pMemorySegmentPages)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaMapCpuHostAperture: Adapter=%p hAlloc=%p Seg=%u "
           "Pages=%I64u\n",
           Adapter, hAllocation, SegmentId, NumberOfPages);

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (DXGK_CB_FULL(Adapter, DxgkDdiMapCpuHostAperture) != NULL)
    {
        DXGKARG_MAPCPUHOSTAPERTURE Args;
        NTSTATUS Status;

        RtlZeroMemory(&Args, sizeof(Args));
        Args.hAllocation           = hAllocation;
        Args.SegmentId             = SegmentId;
        Args.PhysicalAdapterIndex  = 0;
        Args.NumberOfPages         = NumberOfPages;
        Args.pCpuHostAperturePages = pCpuHostAperturePages;
        Args.pMemorySegmentPages   = pMemorySegmentPages;

        if (!DxgkAcquireKmdCall(Adapter))
            return STATUS_DELETE_PENDING;
        Status = DXGK_CB_FULL(Adapter, DxgkDdiMapCpuHostAperture)(Adapter->MiniportDeviceContext, &Args);
        DxgkReleaseKmdCall(Adapter);
        return Status;
    }
#else
    UNREFERENCED_PARAMETER(hAllocation);
    UNREFERENCED_PARAMETER(SegmentId);
    UNREFERENCED_PARAMETER(NumberOfPages);
    UNREFERENCED_PARAMETER(pCpuHostAperturePages);
    UNREFERENCED_PARAMETER(pMemorySegmentPages);
#endif

    return STATUS_NOT_SUPPORTED;
}


/*
 * DxgkGpuVaUnmapCpuHostAperture
 *
 * Unmap GPU memory from a CPU-visible aperture segment.
 */
NTSTATUS
DxgkGpuVaUnmapCpuHostAperture(
    _In_ PDXGKRNL_ADAPTER  Adapter,
    _In_ UINT64            NumberOfPages,
    _In_ UINT32           *pCpuHostAperturePages,
    _In_ WORD              SegmentId)
{
    PAGED_CODE();

    DPRINT("DxgkGpuVaUnmapCpuHostAperture: Adapter=%p Seg=%u Pages=%I64u\n",
           Adapter, SegmentId, NumberOfPages);

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (DXGK_CB_FULL(Adapter, DxgkDdiUnmapCpuHostAperture) != NULL)
    {
        DXGKARG_UNMAPCPUHOSTAPERTURE Args;
        NTSTATUS Status;

        RtlZeroMemory(&Args, sizeof(Args));
        Args.NumberOfPages         = NumberOfPages;
        Args.pCpuHostAperturePages = pCpuHostAperturePages;
        Args.SegmentId             = SegmentId;
        Args.PhysicalAdapterIndex  = 0;

        if (!DxgkAcquireKmdCall(Adapter))
            return STATUS_DELETE_PENDING;
        Status = DXGK_CB_FULL(Adapter, DxgkDdiUnmapCpuHostAperture)(Adapter->MiniportDeviceContext, &Args);
        DxgkReleaseKmdCall(Adapter);
        return Status;
    }
#else
    UNREFERENCED_PARAMETER(NumberOfPages);
    UNREFERENCED_PARAMETER(pCpuHostAperturePages);
    UNREFERENCED_PARAMETER(SegmentId);
#endif

    return STATUS_NOT_SUPPORTED;
}

/* EOF */
