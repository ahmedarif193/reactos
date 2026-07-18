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
#include "vidmm.h"

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
    return (StartA < StartB + SizeB) && (StartB < StartA + SizeA);
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

/*
 * GpuVaFreeRange
 * Free a DXGKRNL_GPUVA_RANGE.
 */
static VOID
GpuVaFreeRange(
    _In_ PDXGKRNL_GPUVA_RANGE Range)
{
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
    if (Size == 0 || Address > MAXULONGLONG - Size)
        return FALSE;

    *EndAddress = Address + Size;
    return TRUE;
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
        Cursor = min(RangeEnd, EndAddress);
    }

    return Cursor == EndAddress;
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
GpuVaValidateAllocationMapping(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DKMT_HANDLE AllocationHandle,
    _In_ ULONGLONG AllocationOffset,
    _In_ ULONGLONG AllocationSize)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    if (AllocationHandle == 0 || AllocationSize == 0 || AllocationOffset > MAXULONGLONG - AllocationSize)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceProcessAllocation((HANDLE)(ULONG_PTR)AllocationHandle, Adapter, Process, &Allocation);
    if (!NT_SUCCESS(Status))
        return Status;
    if (AllocationOffset > Allocation->Size || AllocationSize > Allocation->Size - AllocationOffset)
        Status = STATUS_INVALID_PARAMETER;
    else
        Status = STATUS_SUCCESS;
    DxgkVidMmDereferenceAllocation(Allocation);
    return Status;
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
        Protection.Write = 1;
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
        Status = GpuVaValidateAllocationMapping(Adapter, Process, AllocationHandle, AllocationOffset, AllocationSize);
        if (!NT_SUCCESS(Status))
            return Status;

        for (RangeOffset = 0; RangeOffset < Size; RangeOffset += AllocationSize)
        {
            PDXGKRNL_GPUVA_RANGE Replacement = GpuVaAllocRange();

            if (Replacement == NULL)
            {
                GpuVaFreeList(&ReplacementHead);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            Replacement->GpuVirtualAddress = Address + RangeOffset;
            Replacement->SizeInBytes = AllocationSize;
            Replacement->State = GpuVaStateMapped;
            Replacement->hAllocation = (HANDLE)(ULONG_PTR)AllocationHandle;
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


/* PROCESS LIFECYCLE **********************************************************/

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

    /* Initialize GPU VA space. */
    Process->Adapter = Adapter;
    InitializeListHead(&Process->GpuVaRangeList);
    ExInitializeFastMutex(&Process->GpuVaLock);
    Process->GpuVaTotalReserved = 0;
    Process->GpuVaTotalMapped   = 0;
    Process->GpuVaRangeCount    = 0;
    Process->ResidencyBudget    = 0;  /* 0 = no limit */
    Process->ResidentBytes      = 0;

    KeInitializeEvent(&Process->ResidencyBudgetEvent,
                      NotificationEvent, FALSE);

    Process->hMiniportProcess   = NULL;
    Process->hRootPageTable     = NULL;
    RtlZeroMemory(&Process->RootPageTableAddress,
                  sizeof(Process->RootPageTableAddress));
    Process->RootPageTableEntries = 0;
    Process->RootPageTableProgrammed = FALSE;

    /* Call the miniport's DxgkDdiCreateProcess if available. */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter->MiniportContext->InitData.s.DxgkDdiCreateProcess != NULL)
    {
        DXGKARG_CREATEPROCESS CreateArgs;

        RtlZeroMemory(&CreateArgs, sizeof(CreateArgs));
        CreateArgs.hDxgkProcess = (HANDLE)Process;
        CreateArgs.Flags.Value  = 0;
        CreateArgs.NumPasid     = 0;
        CreateArgs.pPasid       = NULL;

        Status = Adapter->MiniportContext->InitData.s.DxgkDdiCreateProcess(
                     Adapter->MiniportDeviceContext,
                     &CreateArgs);

        if (NT_SUCCESS(Status))
        {
            Process->hMiniportProcess = CreateArgs.hKmdProcess;
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

    /* Destroy root page table allocation if any. */
    if (Process->hRootPageTable != NULL && Adapter != NULL)
    {
        DxgkVidMmDestroyAllocation(Adapter, Process->hRootPageTable);
        Process->hRootPageTable = NULL;
    }

    /* Call the miniport's DxgkDdiDestroyProcess. */
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Process->hMiniportProcess != NULL &&
        Adapter != NULL &&
        Adapter->MiniportContext->InitData.s.DxgkDdiDestroyProcess != NULL &&
        DxgkAcquireMiniportCallback(Adapter))
    {
        NTSTATUS Status;

        Status = Adapter->MiniportContext->InitData.s.DxgkDdiDestroyProcess(Adapter->MiniportDeviceContext, Process->hMiniportProcess);
        DxgkReleaseMiniportCallback(Adapter);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkGpuVaDestroyProcess: DxgkDdiDestroyProcess failed "
                    "0x%08lx\n", Status);
        }

        Process->hMiniportProcess = NULL;
    }
#endif

    Process->Adapter = NULL;
    Process->GpuVaRangeCount = 0;

    DPRINT("DxgkGpuVaDestroyProcess: done\n");
}


/* GPU VA RANGE MANAGEMENT ****************************************************/

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
        if (Range->State == GpuVaStateMapped)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            return STATUS_NOT_SUPPORTED;
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

    ExReleaseFastMutex(&Process->GpuVaLock);
    DPRINT("DxgkGpuVaFree: freed at 0x%I64x\n", BaseAddress);
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkGpuVaPageTableReady(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process)
{
    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || Process->hMiniportProcess == NULL || Process->hRootPageTable == NULL || Process->RootPageTableEntries == 0 || !Process->RootPageTableProgrammed)
        return FALSE;
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->InitData.s.Version < DXGKDDI_INTERFACE_VERSION_WDDM2_0)
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

NTSTATUS
DxgkGpuVaValidateUpdate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_reads_(NumOperations) CONST D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operations,
    _In_ UINT NumOperations)
{
    LIST_ENTRY WorkingHead;
    D3DGPU_VIRTUAL_ADDRESS ExpectedReservationBase = 0;
    D3DGPU_VIRTUAL_ADDRESS ExpectedSourceReservationBase = 0;
    ULONGLONG ExpectedReservationSize = 0;
    ULONGLONG ExpectedSourceReservationSize = 0;
    NTSTATUS Status;
    UINT Index;
    ULONG AuthoritativeCount;
    ULONG WorkingCount;

    PAGED_CODE();

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || Operations == NULL || NumOperations == 0)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&Process->GpuVaLock);
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
    GpuVaFreeList(&WorkingHead);
    ExReleaseFastMutex(&Process->GpuVaLock);
    return Status;
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (!DxgkGpuVaPageTableReady(Adapter, Process))
        return STATUS_NOT_SUPPORTED;

    {
        DXGKARG_SETROOTPAGETABLE SetArgs;

        RtlZeroMemory(&SetArgs, sizeof(SetArgs));
        SetArgs.hContext = Context->hMiniportContext;
        SetArgs.Address = Process->RootPageTableAddress;
        SetArgs.NumEntries = Process->RootPageTableEntries;
        DXGK_CB_FULL(Adapter, DxgkDdiSetRootPageTable)(Adapter->MiniportDeviceContext, &SetArgs);
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter != NULL &&
        Adapter->MiniportContext->InitData.s.DxgkDdiGetRootPageTableSize != NULL)
    {
        DXGKARG_GETROOTPAGETABLESIZE Args;
        SIZE_T Size;

        Args.NumberOfPte       = NumberOfPte;
        Args.PhysicalAdapterIndex = PhysicalAdapterIndex;

        Size = Adapter->MiniportContext->InitData.s.DxgkDdiGetRootPageTableSize(
                   Adapter->MiniportDeviceContext,
                   &Args);

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
 * Validate a WDDM 2.0 MakeResident request.  Only the idempotent case is
 * currently observable; a real residency transition needs paging execution.
 */
NTSTATUS
DxgkGpuVaMakeResident(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_reads_(NumAllocations) CONST D3DKMT_HANDLE *AllocationList,
    _In_    ULONG              NumAllocations,
    _Out_   ULONG             *OutCompleted,
    _Out_   ULONGLONG         *OutNumBytesToTrim)
{
    ULONG i;

    PAGED_CODE();

    DPRINT("DxgkGpuVaMakeResident: %lu allocations\n", NumAllocations);

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || AllocationList == NULL || NumAllocations == 0 || OutCompleted == NULL || OutNumBytesToTrim == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutCompleted = 0;
    *OutNumBytesToTrim = 0;

    for (i = 0; i < NumAllocations; i++)
    {
        PDXGKVMM_ALLOCATION Alloc;
        NTSTATUS Status = DxgkVidMmReferenceProcessAllocation((HANDLE)(ULONG_PTR)AllocationList[i], Adapter, Process, &Alloc);

        if (!NT_SUCCESS(Status))
            return Status;
        if (!Alloc->Resident)
        {
            DxgkVidMmDereferenceAllocation(Alloc);
            return STATUS_NOT_SUPPORTED;
        }
        DxgkVidMmDereferenceAllocation(Alloc);
    }

    /* Idempotent success is truthful.  A state transition is blocked above
     * until paging-buffer transfer and scheduler completion are implemented. */
    *OutCompleted = NumAllocations;
    return STATUS_SUCCESS;
}


/*
 * DxgkGpuVaEvict
 *
 * Validate a WDDM 2.0 Evict request.  Only the idempotent case is currently
 * observable; a real residency transition needs paging execution.
 */
NTSTATUS
DxgkGpuVaEvict(
    _In_    PDXGKRNL_ADAPTER   Adapter,
    _In_    PDXGKRNL_PROCESS   Process,
    _In_reads_(NumAllocations) CONST D3DKMT_HANDLE *AllocationList,
    _In_    ULONG              NumAllocations,
    _Out_   ULONGLONG         *OutNumBytesToTrim)
{
    ULONG i;

    PAGED_CODE();

    DPRINT("DxgkGpuVaEvict: %lu allocations\n", NumAllocations);

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter || AllocationList == NULL || NumAllocations == 0 || OutNumBytesToTrim == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutNumBytesToTrim = 0;

    for (i = 0; i < NumAllocations; i++)
    {
        PDXGKVMM_ALLOCATION Alloc;
        NTSTATUS Status = DxgkVidMmReferenceProcessAllocation((HANDLE)(ULONG_PTR)AllocationList[i], Adapter, Process, &Alloc);

        if (!NT_SUCCESS(Status))
            return Status;
        if (Alloc->Resident)
        {
            DxgkVidMmDereferenceAllocation(Alloc);
            return STATUS_NOT_SUPPORTED;
        }
        DxgkVidMmDereferenceAllocation(Alloc);
    }

    /* Idempotent success is truthful.  A state transition is blocked above
     * until paging-buffer transfer and scheduler completion are implemented. */
    return STATUS_SUCCESS;
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter->MiniportContext->InitData.s.DxgkDdiMapCpuHostAperture != NULL)
    {
        DXGKARG_MAPCPUHOSTAPERTURE Args;

        RtlZeroMemory(&Args, sizeof(Args));
        Args.hAllocation           = hAllocation;
        Args.SegmentId             = SegmentId;
        Args.PhysicalAdapterIndex  = 0;
        Args.NumberOfPages         = NumberOfPages;
        Args.pCpuHostAperturePages = pCpuHostAperturePages;
        Args.pMemorySegmentPages   = pMemorySegmentPages;

        return Adapter->MiniportContext->InitData.s.DxgkDdiMapCpuHostAperture(
                   Adapter->MiniportDeviceContext,
                   &Args);
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

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    if (Adapter->MiniportContext->InitData.s.DxgkDdiUnmapCpuHostAperture != NULL)
    {
        DXGKARG_UNMAPCPUHOSTAPERTURE Args;

        RtlZeroMemory(&Args, sizeof(Args));
        Args.NumberOfPages         = NumberOfPages;
        Args.pCpuHostAperturePages = pCpuHostAperturePages;
        Args.SegmentId             = SegmentId;
        Args.PhysicalAdapterIndex  = 0;

        return Adapter->MiniportContext->InitData.s.DxgkDdiUnmapCpuHostAperture(
                   Adapter->MiniportDeviceContext,
                   &Args);
    }
#else
    UNREFERENCED_PARAMETER(NumberOfPages);
    UNREFERENCED_PARAMETER(pCpuHostAperturePages);
    UNREFERENCED_PARAMETER(SegmentId);
#endif

    return STATUS_NOT_SUPPORTED;
}

/* EOF */
