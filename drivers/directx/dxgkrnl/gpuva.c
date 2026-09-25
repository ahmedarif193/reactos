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
 *   - DxgkDdiSetRootPageTable is called only for a context whose node reports
 *     DXGK_NODEMETADATA.GpuMmuSupported — the OS owns the page tables only
 *     there — and only after RootPageTableProgrammed proves that a real
 *     page-table build/submit path completed and the root owns a placement.
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

/* DXGK_PTE stores address bits 63:12; root SegmentOffset remains in bytes. */
static __inline ULONGLONG
GpuVaPteAddress(
    _In_ ULONGLONG Address)
{
    return Address >> PAGE_SHIFT;
}

/* A fixed per-process node cap bounds user-controlled NonPagedPool growth.
 * Validation is serialized by GpuVaLock; clone/replacement amplification is
 * separately bounded to at most three capped lists plus two split nodes. */
#define GPUVA_MAX_PROCESS_RANGES    16384UL
#define GPUVA_MAX_TRANSIENT_RANGES  (GPUVA_MAX_PROCESS_RANGES * 3UL + 2UL)

/* Residency references, paging packets, and allocation destruction now share
 * one completion-driven state machine (paging.c). */
#define GPUVA_RESIDENCY_END_TO_END  1

/*
 * GPU VA space starts at 64 KB (skip the first 16 pages as a guard region
 * to catch NULL pointer dereferences on the GPU).
 */
#define GPUVA_START_ADDRESS         (64ULL * 1024ULL)

/* Snapshot of a mapped range taken under GpuVaLock for leaf page-table
 * updates that must name their allocation to the miniport. */
typedef struct _DXGKP_GPUVA_MAP_SPAN
{
    D3DGPU_VIRTUAL_ADDRESS      Start;
    D3DGPU_VIRTUAL_ADDRESS      End;
    HANDLE                      MiniportHandle;
    ULONGLONG                   AllocationOffset;
    UINT64                      DriverProtection;
    PDXGKRNL_GPUVA_BINDING      Binding;
} DXGKP_GPUVA_MAP_SPAN, *PDXGKP_GPUVA_MAP_SPAN;
C_ASSERT(FIELD_OFFSET(DXGKP_GPUVA_MAP_SPAN, Start) == FIELD_OFFSET(DXGK_GPUVA_CORE_SPAN, Start));
C_ASSERT(FIELD_OFFSET(DXGKP_GPUVA_MAP_SPAN, End) == FIELD_OFFSET(DXGK_GPUVA_CORE_SPAN, End));

/* Portable descriptors copied with the dirty range under GpuVaLock. */
typedef struct _DXGKP_GPUVA_TABLE_SNAPSHOT
{
    PDXGKRNL_GPUVA_PAGE_TABLE Table;
    DXGK_PTE                  *Entries;
    ULONG                     StartIndex;
    ULONG                     EndIndex;
    BOOLEAN                   InitialUpdatePending;
} DXGKP_GPUVA_TABLE_SNAPSHOT, *PDXGKP_GPUVA_TABLE_SNAPSHOT;

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
        if (References <= 0 || References == MAXLONG)
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
    _In_ ULONGLONG              Size,
    _In_ ULONGLONG              Alignment)
{
    D3DGPU_VIRTUAL_ADDRESS Candidate;
    PLIST_ENTRY Entry;

    if (MinAddress < GPUVA_START_ADDRESS)
        MinAddress = GPUVA_START_ADDRESS;

    Candidate = GpuVaAlignUp(MinAddress, Alignment);

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
        if (RangeEnd > MAXULONGLONG - (Alignment - 1))
            return 0;
        Candidate = GpuVaAlignUp(RangeEnd, Alignment);

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
 * GPU VA SUBMISSION PINS *****************************************************
 *
 * A pin records that a submitted, unretired DMA packet executes out of a GPU
 * virtual span.  It is kept off DXGKRNL_GPUVA_RANGE on purpose: every mapping
 * transaction replaces the whole range list with clones, so a count stored on
 * a range object is reset by the next unrelated map -- which both drops the
 * protection and makes the matching unpin underflow.
 *
 * All three helpers require GpuVaLock.
 */

/* Add one reference to the pin on [Address, Address+Size). */
static BOOLEAN
GpuVaPinAcquire(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    PLIST_ENTRY Entry;
    PDXGKRNL_GPUVA_PIN Pin;

    for (Entry = Process->GpuVaPinList.Flink;
         Entry != &Process->GpuVaPinList;
         Entry = Entry->Flink)
    {
        Pin = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_PIN, PinListEntry);

        if (Pin->GpuVirtualAddress == Address && Pin->SizeInBytes == Size)
        {
            if (Pin->Count == MAXULONG)
                return FALSE;
            Pin->Count++;
            return TRUE;
        }
    }

    Pin = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Pin), TAG_DXGK_GPUVA_PIN);
    if (Pin == NULL)
        return FALSE;
    RtlZeroMemory(Pin, sizeof(*Pin));
    Pin->GpuVirtualAddress = Address;
    Pin->SizeInBytes = Size;
    Pin->Count = 1;
    InsertTailList(&Process->GpuVaPinList, &Pin->PinListEntry);
    return TRUE;
}

/* Drop one reference from the pin on [Address, Address+Size). */
static VOID
GpuVaPinRelease(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->GpuVaPinList.Flink;
         Entry != &Process->GpuVaPinList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_PIN Pin =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_PIN, PinListEntry);

        if (Pin->GpuVirtualAddress != Address || Pin->SizeInBytes != Size)
            continue;

        ASSERT(Pin->Count != 0);
        Pin->Count--;
        if (Pin->Count == 0)
        {
            RemoveEntryList(&Pin->PinListEntry);
            ExFreePoolWithTag(Pin, TAG_DXGK_GPUVA_PIN);
        }
        return;
    }

    /* Every unpin is issued by the packet that took the pin, so the entry is
     * normally still here.  Process teardown frees the pin list while the
     * scheduler may still be retiring that process's packets, so report the
     * miss instead of asserting the machine into the debugger. */
    DPRINT1("DxgkGpuVaUnpinRange: no pin for [0x%I64x+0x%I64x) on process %p\n",
            Address, Size, Process);
}

/* Free every pin of the process.  Teardown only. */
static VOID
GpuVaPinFreeAll(
    _In_ PDXGKRNL_PROCESS Process)
{
    while (!IsListEmpty(&Process->GpuVaPinList))
    {
        PDXGKRNL_GPUVA_PIN Pin =
            CONTAINING_RECORD(RemoveHeadList(&Process->GpuVaPinList),
                              DXGKRNL_GPUVA_PIN, PinListEntry);

        ExFreePoolWithTag(Pin, TAG_DXGK_GPUVA_PIN);
    }
}

/* TRUE if any live submission overlaps [Address, Address+Size). */
static BOOLEAN
GpuVaRangeIsPinned(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    D3DGPU_VIRTUAL_ADDRESS EndAddress;
    PLIST_ENTRY Entry;

    if (!GpuVaGetRangeEnd(Address, Size, &EndAddress))
        return FALSE;
    for (Entry = Process->GpuVaPinList.Flink;
         Entry != &Process->GpuVaPinList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_PIN Pin =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_PIN, PinListEntry);
        D3DGPU_VIRTUAL_ADDRESS PinEnd;

        if (Pin->Count == 0)
            continue;
        if (!GpuVaGetRangeEnd(Pin->GpuVirtualAddress, Pin->SizeInBytes, &PinEnd))
            continue;
        if (Pin->GpuVirtualAddress < EndAddress && Address < PinEnd)
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

/* Bound resident bookkeeping by memory, not by an address-space limit.
 * 1024 tables cannot describe even one unaligned 2 GiB zero reservation with
 * 4 KiB pages once its directory tables and other mappings are included.
 * Each process gets 1/64 of installed RAM, between 16 and 256 MiB, for tables
 * and their software entries. Paging snapshots require additional transient
 * memory; allocation failure continues to be propagated to the caller. */
#define GPUVA_MIN_PAGE_TABLE_BUDGET (16ULL * 1024 * 1024)
#define GPUVA_MAX_PAGE_TABLE_BUDGET (256ULL * 1024 * 1024)
#define GPUVA_PAGING_SYNC_TIMEOUT_MS  2000UL

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
    _In_ ULONGLONG CoverageBase,
    _In_opt_ PDXGKRNL_GPUVA_PAGE_TABLE Parent,
    _In_ ULONG ParentIndex)
{
    PDXGKRNL_GPUVA_PAGE_TABLE Table;
    PHYSICAL_ADDRESS LowestAddress;
    PHYSICAL_ADDRESS HighestAddress;
    PHYSICAL_ADDRESS BoundaryAddress;
    MEMORY_CACHING_TYPE CacheType;
    PDXGKRNL_ADAPTER Adapter = Process->Adapter;
    ULONG TableBytes;
    ULONG EntryCount;
    ULONG SegmentId;
    ULONGLONG Charge;

    if (Process->GpuVaPageTableCount == MAXULONG)
        return NULL;
    if (Adapter == NULL || !Adapter->PageTableLevelsValid || Level >= GpuVaLevelCount(Adapter))
        return NULL;

    SegmentId = GpuVaLevelDesc(Adapter, Level)->PageTableSegmentId;
    if (SegmentId != 0)
    {
        PDXGKRNL_SEGMENT Segment;

        if (Adapter->Segments == NULL || SegmentId > Adapter->SegmentCount)
            return NULL;
        Segment = &((PDXGKRNL_SEGMENT)Adapter->Segments)[SegmentId - 1];
        /* Native VidMm can place its implicit page-table allocation in the
         * miniport's aperture segment because that allocation has the full
         * VidMm open/close and residency lifecycle.  Our table is direct
         * system backing without such an allocation.  Publishing an aperture
         * offset for it leaves the hardware window on the scratch page, so
         * describe the backing through segment zero instead. */
        if (Segment->Flags.Aperture || Segment->Flags.Agp)
            SegmentId = 0;
    }

    TableBytes = GpuVaTableBytes(Adapter, Level);
    EntryCount = (ULONG)GpuVaEntriesPerTable(Adapter, Level);
    if ((SIZE_T)EntryCount > MAXULONG_PTR / sizeof(*Table->Entries))
        return NULL;
    Charge = sizeof(*Table) + (ULONGLONG)TableBytes +
             (ULONGLONG)EntryCount * sizeof(*Table->Entries);
    if (Level > 0)
        Charge += (ULONGLONG)EntryCount * sizeof(*Table->Children);
    if (Charge > Process->GpuVaPageTableBudget ||
        Process->GpuVaPageTableBytes > Process->GpuVaPageTableBudget - Charge)
        return NULL;

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
    /* Page-table cacheability is independent of ordinary allocation cache
     * coherency.  CachedPageTables is the KMD's explicit promise that its
     * walker observes cached CPU_VIRTUAL updates. */
    CacheType = Adapter->GpuMmuCaps.CachedPageTables ? MmCached : MmNonCached;
    Table->KernelVa = MmAllocateContiguousMemorySpecifyCache(TableBytes, LowestAddress, HighestAddress, BoundaryAddress, CacheType);
    if (Table->KernelVa == NULL)
    {
        ExFreePoolWithTag(Table, TAG_DXGK_GPUVA_PT);
        return NULL;
    }
    RtlZeroMemory(Table->KernelVa, TableBytes);

    Table->Entries = (DXGK_PTE *)ExAllocatePoolWithTag(
                         NonPagedPool,
                         (SIZE_T)EntryCount * sizeof(*Table->Entries),
                         TAG_DXGK_GPUVA_PT);
    if (Table->Entries == NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Table->KernelVa,
                                            TableBytes,
                                            CacheType);
        ExFreePoolWithTag(Table, TAG_DXGK_GPUVA_PT);
        return NULL;
    }
    RtlZeroMemory(Table->Entries,
                  (SIZE_T)EntryCount * sizeof(*Table->Entries));

    if (Level > 0)
    {
        Table->Children = (PDXGKRNL_GPUVA_PAGE_TABLE *)ExAllocatePoolWithTag(
                    NonPagedPool,
                    (SIZE_T)EntryCount * sizeof(PDXGKRNL_GPUVA_PAGE_TABLE),
                    TAG_DXGK_GPUVA_PT);
        if (Table->Children == NULL)
        {
            ExFreePoolWithTag(Table->Entries, TAG_DXGK_GPUVA_PT);
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
    Table->InitialUpdatePending = TRUE;
    Table->Parent = Parent;
    Table->ParentIndex = ParentIndex;
    /* Place the implicit allocation in the miniport's declared segment once
     * GpuVaLock is released. The root DDI needs that physical placement. */
    Table->SegmentId = SegmentId;
    Table->SegmentOffset = 0;
    Table->SegmentMdl = NULL;
    Table->MiniportDeviceHandle = NULL;
    Table->PlacementPending = Table->SegmentId != 0;

    InsertTailList(&Process->GpuVaPageTableList, &Table->PageTableListEntry);
    Process->GpuVaPageTableCount++;
    Process->GpuVaPageTableBytes += Charge;
    return Table;
}

/*
 * GpuVaTableSegmentOffset
 * Address of the table relative to the segment it was placed in.
 *
 * Windows' SetPageTableInPde and GetPageDirectoryData pair the declared
 * segment ID with the VIDMM_PHYSICAL_ALLOC address. The SDK defines the
 * nonzero-segment DXGK_PTE and D3DGPU_PHYSICAL_ADDRESS value as an offset from
 * that segment; segment zero instead carries a system physical address.
 */
static ULONGLONG
GpuVaTableSegmentOffset(
    _In_ PDXGKRNL_GPUVA_PAGE_TABLE Table)
{
    if (Table->SegmentId == 0)
        return (ULONGLONG)Table->Physical.QuadPart;
    return Table->PlacementPending ? 0ULL : Table->SegmentOffset;
}

/*
 * GpuVaLinkChildEntry
 * Write the parent entry that points at Child.  Caller MUST hold GpuVaLock.
 */
static VOID
GpuVaLinkChildEntry(
    _In_ PDXGKRNL_GPUVA_PAGE_TABLE Parent,
    _In_ ULONG Index,
    _In_ PDXGKRNL_GPUVA_PAGE_TABLE Child)
{
    ULONGLONG Address = GpuVaTableSegmentOffset(Child);

    /* Windows preserves the miniport-declared page-table segment in the PDE
     * and writes the child allocation's offset within that segment. */
    Parent->Entries[Index].Flags = 0;
    Parent->Entries[Index].Valid =
        (Child->SegmentId != 0 && Child->PlacementPending) ? 0 : 1;
    Parent->Entries[Index].Segment = Child->SegmentId;
    Parent->Entries[Index].PageTableAddress =
        GpuVaPteAddress(Address);
}

/*
 * GpuVaPublishRootAddress
 * Republish the root address handed to DxgkDdiSetRootPageTable.
 * Caller MUST hold GpuVaLock.
 */
static VOID
GpuVaPublishRootAddress(
    _In_ PDXGKRNL_PROCESS Process)
{
    PDXGKRNL_GPUVA_PAGE_TABLE Root =
        (PDXGKRNL_GPUVA_PAGE_TABLE)Process->hRootPageTable;

    if (Root == NULL)
        return;
    Process->RootPageTableAddress.SegmentId = Root->SegmentId;
    Process->RootPageTableAddress.SegmentOffset = GpuVaTableSegmentOffset(Root);
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

        if (Table->SegmentId != 0 && !Table->PlacementPending)
        {
            DxgkVidMmUnmapPageTableSegment(Process->Adapter,
                                           Table->MiniportDeviceHandle,
                                           Table->SegmentId,
                                           Table->SegmentOffset,
                                           Table->Bytes,
                                           (ULONGLONG)(ULONG_PTR)Table,
                                           Table->SegmentMdl);
        }
        if (Table->Children != NULL)
            ExFreePoolWithTag(Table->Children, TAG_DXGK_GPUVA_PT);
        ExFreePoolWithTag(Table->Entries, TAG_DXGK_GPUVA_PT);
        MmFreeContiguousMemorySpecifyCache(Table->KernelVa, Table->Bytes, Table->CacheType);
        ExFreePoolWithTag(Table, TAG_DXGK_GPUVA_PT);
    }
    Process->GpuVaPageTableCount = 0;
    Process->GpuVaPageTableBytes = 0;
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
    ReportedSize = GpuVaTableBytes(Adapter, RootLevel);
    /* Only a two-level translation scheme has a resizable root. */
    if (GpuVaLevelCount(Adapter) == 2)
        ReportedSize = DxgkGpuVaGetRootPageTableSize(Adapter, RootEntries, 0);
    if (ReportedSize != GpuVaTableBytes(Adapter, RootLevel))
    {
        DPRINT1("DxgkGpuVa: root table size %Iu disagrees with level descriptor %u\n", ReportedSize, GpuVaTableBytes(Adapter, RootLevel));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Root = GpuVaAllocPageTable(Process, RootLevel, 0, NULL, 0);
    if (Root == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Process->hRootPageTable = (HANDLE)Root;
    GpuVaPublishRootAddress(Process);
    Process->RootPageTableEntries = RootEntries;
    Process->RootPageTableProgrammed = TRUE;
    return STATUS_SUCCESS;
}

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
    _In_ ULONGLONG FirstVirtualAddress)
{
    PDXGKRNL_ADAPTER Adapter = Process->Adapter;
    ULONGLONG Coverage;

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

static NTSTATUS
GpuVaAppendPagingOperation(
    _Inout_ PDXGKRNL_PAGING_OP *Operations,
    _Inout_ PULONG OperationCount,
    _Inout_ PULONG OperationCapacity,
    _In_ CONST DXGKRNL_PAGING_OP *Operation)
{
    PDXGKRNL_PAGING_OP NewOperations;
    ULONG NewCapacity;

    if (Operations == NULL || OperationCount == NULL ||
        OperationCapacity == NULL || Operation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (*OperationCount == *OperationCapacity)
    {
        if (*OperationCapacity == 0)
            NewCapacity = 16;
        else if (*OperationCapacity > MAXULONG / 2)
            return STATUS_INTEGER_OVERFLOW;
        else
            NewCapacity = *OperationCapacity * 2;
        if ((SIZE_T)NewCapacity > MAXULONG_PTR / sizeof(**Operations))
            return STATUS_INTEGER_OVERFLOW;
        NewOperations = ExAllocatePoolWithTag(
                            NonPagedPool,
                            (SIZE_T)NewCapacity * sizeof(*NewOperations),
                            TAG_DXGK_GPUVA_PT);
        if (NewOperations == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (*OperationCount != 0)
        {
            RtlCopyMemory(NewOperations,
                          *Operations,
                          (SIZE_T)*OperationCount * sizeof(*NewOperations));
        }
        if (*Operations != NULL)
            ExFreePoolWithTag(*Operations, TAG_DXGK_GPUVA_PT);
        *Operations = NewOperations;
        *OperationCapacity = NewCapacity;
    }
    (*Operations)[(*OperationCount)++] = *Operation;
    return STATUS_SUCCESS;
}

static BOOLEAN
GpuVaWaitForKmdResetBoundary(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE SubmissionDevice)
{
    PVOID CurrentThread;
    NTSTATUS WaitStatus;
    BOOLEAN AdmissionReopened;

    PAGED_CODE();

    if (Adapter == NULL ||
        Adapter->State != DxgkAdapterStateStarted ||
        InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0 ||
        InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) == 0 ||
        (SubmissionDevice != NULL &&
         InterlockedCompareExchange(&SubmissionDevice->Destroying, 0, 0) != 0))
    {
        return FALSE;
    }

    CurrentThread = PsGetCurrentThread();
    if (Adapter->KmdExclusiveOwnerThread == CurrentThread ||
        Adapter->KmdTransactionOwnerThread == CurrentThread)
    {
        return FALSE;
    }

    /*
     * Native GPU-VA entry points wait at the adapter shared-access boundary
     * while reset owns it.  KmdCallsBlocked is our equivalent admission edge:
     * wait for its exclusive owner, then retry only if that owner reopened the
     * started adapter.  A stop or failed reset releases the mutex without
     * reopening admission and therefore remains a terminal failure.
     */
    DXGKRNL_WARN("DxgkGpuVa: waiting for KMD reset boundary before retrying "
                 "page-table work\n");
    WaitStatus = KeWaitForSingleObject(&Adapter->KmdExclusiveMutex,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    if (!NT_SUCCESS(WaitStatus))
        return FALSE;

    AdmissionReopened =
        Adapter->State == DxgkAdapterStateStarted &&
        InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) != 0 &&
        InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) == 0 &&
        (SubmissionDevice == NULL ||
         InterlockedCompareExchange(&SubmissionDevice->Destroying, 0, 0) == 0);
    KeReleaseMutex(&Adapter->KmdExclusiveMutex, FALSE);
    DXGKRNL_WARN("DxgkGpuVa: KMD reset boundary completed; admission %s\n",
                 AdmissionReopened ? "reopened" : "closed");
    return AdmissionReopened;
}

static NTSTATUS
GpuVaExecutePageTableBatch(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_reads_(OperationCount) CONST DXGKRNL_PAGING_OP *Operations,
    _In_ ULONG OperationCount,
    _Inout_ PBOOLEAN TransactionHeld)
{
    ULONG FenceId = 0;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(*TransactionHeld);
    ASSERT(Adapter->KmdTransactionOwnerThread == PsGetCurrentThread());

    /* Page-table maintenance belongs to VidMm, not the client work ledger.
     * A client fault must reject its rendering while still allowing the OS
     * to invalidate its mappings before releasing their backing pages. The
     * caller retains the process, tables and bindings until the paging fence
     * retires; the packet uses the adapter's paging system context. */
    Status = DxgkPagingExecuteBatch(Adapter,
                                    NULL,
                                    Operations,
                                    OperationCount,
                                    NULL,
                                    0,
                                    0,
                                    0,
                                    &FenceId,
                                    NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    /* No more miniport calls until PageTableFlushMutex is released. A caller
     * waiting for this flush may already own the KMD transaction. Drop our
     * admission reference before the fence wait so reset can recover a stalled
     * packet when there is no enclosing miniport callback. */
    DxgkEndKmdTransaction(Adapter);
    *TransactionHeld = FALSE;
    return DxgkPagingWaitForFence(Adapter, FenceId, GPUVA_PAGING_SYNC_TIMEOUT_MS);
}

/*
 * DxgkGpuVaPlacePendingPageTables
 *
 * Give every page table allocated since the last call its placement in the
 * segment the miniport declared for that level.  A page table is an implicit
 * VidMm allocation: the GPU walker reaches it through the declared segment,
 * so until the placement exists the parent entry has no address to point at
 * and the root has none to publish.
 *
 * Placing a table is a paging submission, so it cannot happen where tables
 * are allocated (under GpuVaLock, at APC_LEVEL).  Page tables are only
 * destroyed at process teardown and the caller owns the process, so dropping
 * the lock around each placement cannot free the table underneath it.
 *
 * IRQL: PASSIVE_LEVEL, GpuVaLock NOT held.
 */
NTSTATUS
DxgkGpuVaPlacePendingPageTables(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ HANDLE MiniportDeviceHandle)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Adapter == NULL || Process == NULL || MiniportDeviceHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    for (;;)
    {
        PDXGKRNL_GPUVA_PAGE_TABLE Table = NULL;
        PLIST_ENTRY Entry;
        ULONG SegmentId;
        ULONG Bytes;
        ULONG Alignment;
        PVOID KernelVa;
        ULONGLONG SegmentOffset = 0;
        PMDL Mdl = NULL;

        ExAcquireFastMutex(&Process->GpuVaLock);
        for (Entry = Process->GpuVaPageTableList.Flink;
             Entry != &Process->GpuVaPageTableList;
             Entry = Entry->Flink)
        {
            PDXGKRNL_GPUVA_PAGE_TABLE Candidate =
                CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_PAGE_TABLE, PageTableListEntry);

            if (Candidate->PlacementPending)
            {
                Table = Candidate;
                break;
            }
        }
        if (Table == NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            break;
        }
        SegmentId = Table->SegmentId;
        Bytes = Table->Bytes;
        KernelVa = Table->KernelVa;
        Alignment = GpuVaLevelDesc(Adapter, Table->Level)->PageTableAlignmentInBytes;
        ExReleaseFastMutex(&Process->GpuVaLock);

        Status = DxgkVidMmMapPageTableSegment(Adapter,
                                              MiniportDeviceHandle,
                                              SegmentId,
                                              KernelVa,
                                              Bytes,
                                              Alignment,
                                              (ULONGLONG)(ULONG_PTR)Table,
                                              &SegmentOffset,
                                              &Mdl);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkGpuVa: page table level %lu placement in segment %lu failed 0x%08lX\n",
                        Table->Level, SegmentId, Status);
            return Status;
        }
        ExAcquireFastMutex(&Process->GpuVaLock);
        Table->SegmentOffset = SegmentOffset;
        Table->SegmentMdl = Mdl;
        Table->MiniportDeviceHandle = MiniportDeviceHandle;
        Table->PlacementPending = FALSE;
        if (Table->Parent != NULL)
        {
            GpuVaLinkChildEntry(Table->Parent, Table->ParentIndex, Table);
            (VOID)GpuVaNotifyPageTableUpdate(Process,
                                             Table->Parent,
                                             Table->ParentIndex,
                                             1,
                                             Table->CoverageBase);
        }
        else if ((HANDLE)Table == Process->hRootPageTable)
        {
            GpuVaPublishRootAddress(Process);
        }
        ExReleaseFastMutex(&Process->GpuVaLock);
    }

    return Status;
}

/*
 * DxgkGpuVaFlushPageTableUpdates
 *
 * Describes the page-table span accumulated under GpuVaLock to the miniport
 * and invalidates translations when the recorded transitions require it.
 * PageTableFlushMutex covers the entire snapshot/submit/retire transaction,
 * so a second caller cannot observe an empty pending span and release backing
 * while the first transaction is in flight. A rejected transaction is merged
 * back with any newer updates before the mutex is released.
 *
 * The caller holds a process reference for the duration.
 *
 * IRQL: PASSIVE_LEVEL, GpuVaLock and PageTableFlushMutex NOT held.
 */
static NTSTATUS
DxgkpGpuVaFlushPageTableUpdatesOnce(
    _In_ PDXGKRNL_PROCESS Process,
    _In_opt_ PDXGKRNL_DEVICE OwnedDevice)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE PagingDevice = NULL;
    PDXGKP_GPUVA_TABLE_SNAPSHOT Tables = NULL;
    PDXGKP_GPUVA_MAP_SPAN Spans = NULL;
    PDXGKRNL_PAGING_OP Operations = NULL;
    ULONG SpanCount = 0;
    DXGKRNL_PAGING_OP Op;
    DXGK_PTE ZeroPte;
    HANDLE PagingMiniportDevice = NULL;
    D3DGPU_VIRTUAL_ADDRESS Start = 0;
    D3DGPU_VIRTUAL_ADDRESS End = 0;
    ULONG TableCapacity;
    ULONG TableCount = 0;
    ULONG TableIndex;
    ULONG ReleaseIndex;
    ULONG OperationCount = 0;
    ULONG OperationCapacity = 0;
    NTSTATUS Status;
    BOOLEAN TransactionHeld = FALSE;

    PAGED_CODE();
    if (Process == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = Process->Adapter;
    if (Adapter == NULL)
        return STATUS_DEVICE_NOT_READY;

    /* Miniport reverse callbacks may flush while already owning the adapter
     * transaction. Taking the process mutex first deadlocks fence cleanup
     * against context/allocation destruction: each waits for the other's lock.
     * Always admit the KMD transaction before taking the process flush mutex. */
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DELETE_PENDING;
    TransactionHeld = TRUE;

    Status = KeWaitForSingleObject(&Process->PageTableFlushMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
    {
        DxgkEndKmdTransaction(Adapter);
        return Status;
    }

    if (!DxgkPagingOperationSupported(Adapter,
                                      DxgkPagingOpUpdatePageTable) ||
        !DxgkPagingOperationSupported(Adapter, DxgkPagingOpFlushTlb))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Complete;
    }

    if (OwnedDevice != NULL)
    {
        /* Device destruction has already closed admission and unlinked this
         * device from Process->DeviceListHead.  The teardown owner nevertheless
         * owns the object and hMiniportDevice until this synchronous operation
         * retires. */
        if (OwnedDevice->ProcessRecord != Process ||
            OwnedDevice->Adapter != Adapter ||
            OwnedDevice->hMiniportDevice == NULL)
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto Complete;
        }
        PagingMiniportDevice = OwnedDevice->hMiniportDevice;
    }
    else
    {
        Status = DxgkReferenceProcessPagingDevice(Process,
                                                  &PagingDevice,
                                                  &PagingMiniportDevice);
        if (Status == STATUS_DEVICE_NOT_READY)
        {
            /* The process has no live device: its last one is mid-teardown
             * and already unlinked from DeviceListHead.  Its page tables
             * still have to be updated (allocation destruction clears PTEs,
             * the fence-page unmap in sync.c, and finally the tables
             * themselves), and native VidMm performs that work on its own
             * paging device, never on the client's.  Use the adapter's
             * paging system device the same way; refusing here quarantined
             * every allocation of a closing device until adapter stop. */
            PDXGKRNL_DEVICE SystemDevice = Adapter->PagingSystemDevice;

            if (SystemDevice != NULL &&
                SystemDevice->hMiniportDevice != NULL &&
                DxgkReferenceDevice(SystemDevice))
            {
                PagingDevice = SystemDevice;
                PagingMiniportDevice = SystemDevice->hMiniportDevice;
                Status = STATUS_SUCCESS;
            }
        }
        if (!NT_SUCCESS(Status))
            goto Complete;
    }

    /* Anything allocated since the last flush is still unreachable by the GPU
     * until it has a placement in the miniport's page-table segment.  Native
     * VidMm maps it with the process device and implicit allocation identity,
     * so acquire that identity before publishing any parent or root address. */
    Status = DxgkGpuVaPlacePendingPageTables(Adapter,
                                             Process,
                                             PagingMiniportDevice);
    if (!NT_SUCCESS(Status))
        goto Complete;

    for (;;)
    {
        PLIST_ENTRY Entry;

        ExAcquireFastMutex(&Process->GpuVaLock);
        if (!Process->PageTableUpdatePending)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            Status = STATUS_SUCCESS;
            goto Complete;
        }
        TableCapacity = Process->GpuVaPageTableCount;
        ExReleaseFastMutex(&Process->GpuVaLock);

        if (TableCapacity == 0 ||
            (SIZE_T)TableCapacity > MAXULONG_PTR / sizeof(*Tables))
        {
            Status = STATUS_DATA_ERROR;
            goto Complete;
        }
        Tables = ExAllocatePoolWithTag(
            NonPagedPool,
            (SIZE_T)TableCapacity * sizeof(*Tables),
            TAG_DXGK_GPUVA_PT);
        if (Tables == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Complete;
        }
        RtlZeroMemory(Tables, (SIZE_T)TableCapacity * sizeof(*Tables));

        ExAcquireFastMutex(&Process->GpuVaLock);
        if (Process->GpuVaPageTableCount > TableCapacity)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            ExFreePoolWithTag(Tables, TAG_DXGK_GPUVA_PT);
            Tables = NULL;
            continue;
        }

        Start = Process->PageTableUpdateStart;
        End = Process->PageTableUpdateEnd;
        if (End <= Start)
        {
            Process->PageTableUpdatePending = FALSE;
            Process->PageTableUpdateStart = 0;
            Process->PageTableUpdateEnd = 0;
            ExReleaseFastMutex(&Process->GpuVaLock);
            Status = STATUS_DATA_ERROR;
            goto Complete;
        }
        TableCount = 0;
        for (Entry = Process->GpuVaPageTableList.Flink;
             Entry != &Process->GpuVaPageTableList;
             Entry = Entry->Flink)
        {
            PDXGKRNL_GPUVA_PAGE_TABLE Table;
            ULONGLONG EntryCoverage;
            ULONGLONG TableCoverage;
            ULONGLONG TableEnd;
            ULONGLONG OverlapStart;
            ULONGLONG OverlapEnd;
            ULONG StartIndex;
            ULONG EndIndex;
            ULONG EntryCount;

            if (TableCount >= TableCapacity)
                break;
            Table = CONTAINING_RECORD(
                Entry,
                DXGKRNL_GPUVA_PAGE_TABLE,
                PageTableListEntry);
            EntryCoverage = 1ULL << GpuVaLevelShift(Adapter, Table->Level);
            if (Table->EntryCount > MAXULONGLONG / EntryCoverage ||
                Table->CoverageBase > MAXULONGLONG -
                                      Table->EntryCount * EntryCoverage)
            {
                ExReleaseFastMutex(&Process->GpuVaLock);
                Status = STATUS_INTEGER_OVERFLOW;
                goto Requeue;
            }
            TableCoverage = Table->EntryCount * EntryCoverage;
            TableEnd = Table->CoverageBase + TableCoverage;
            if (Table->InitialUpdatePending)
            {
                OverlapStart = Table->CoverageBase;
                OverlapEnd = TableEnd;
            }
            else
            {
                OverlapStart = max(Start, Table->CoverageBase);
                OverlapEnd = min(End, TableEnd);
            }
            if (OverlapStart >= OverlapEnd)
                continue;

            StartIndex = (ULONG)((OverlapStart - Table->CoverageBase) /
                                 EntryCoverage);
            EndIndex = (ULONG)(((OverlapEnd - Table->CoverageBase) +
                                EntryCoverage - 1) / EntryCoverage);
            if (EndIndex > Table->EntryCount)
                EndIndex = Table->EntryCount;
            if (StartIndex >= EndIndex)
                continue;

            EntryCount = EndIndex - StartIndex;
            if ((SIZE_T)EntryCount > MAXULONG_PTR / sizeof(DXGK_PTE))
            {
                ExReleaseFastMutex(&Process->GpuVaLock);
                Status = STATUS_INTEGER_OVERFLOW;
                goto Requeue;
            }
            Tables[TableCount].Entries = ExAllocatePoolWithTag(
                NonPagedPool,
                (SIZE_T)EntryCount * sizeof(DXGK_PTE),
                TAG_DXGK_GPUVA_PT);
            if (Tables[TableCount].Entries == NULL)
            {
                ExReleaseFastMutex(&Process->GpuVaLock);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Requeue;
            }
            RtlCopyMemory(Tables[TableCount].Entries,
                          Table->Entries + StartIndex,
                          (SIZE_T)EntryCount * sizeof(DXGK_PTE));
            Tables[TableCount].Table = Table;
            Tables[TableCount].StartIndex = StartIndex;
            Tables[TableCount].EndIndex = EndIndex;
            Tables[TableCount].InitialUpdatePending = Table->InitialUpdatePending;
            TableCount++;
        }
        /* Preserve protection for reservations too: zero PTEs have no backing
         * allocation, but their UMD-supplied encoding is still significant. */
        SpanCount = 0;
        {
            ULONG MappedCount = 0;

            for (Entry = Process->GpuVaRangeList.Flink; Entry != &Process->GpuVaRangeList; Entry = Entry->Flink)
            {
                PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);

                if ((Range->State == GpuVaStateMapped && Range->Binding != NULL) ||
                    Range->State == GpuVaStateReserved)
                    MappedCount++;
            }
            if (MappedCount != 0)
            {
                if ((SIZE_T)MappedCount > MAXULONG_PTR / sizeof(*Spans))
                {
                    ExReleaseFastMutex(&Process->GpuVaLock);
                    Status = STATUS_INTEGER_OVERFLOW;
                    goto Requeue;
                }
                Spans = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)MappedCount * sizeof(*Spans), TAG_DXGK_GPUVA_PT);
                if (Spans == NULL)
                {
                    ExReleaseFastMutex(&Process->GpuVaLock);
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Requeue;
                }
                for (Entry = Process->GpuVaRangeList.Flink; Entry != &Process->GpuVaRangeList && SpanCount < MappedCount; Entry = Entry->Flink)
                {
                    PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
                    PDXGKVMM_ALLOCATION MapAllocation = NULL;

                    if (Range->State != GpuVaStateReserved &&
                        (Range->State != GpuVaStateMapped || Range->Binding == NULL))
                        continue;
                    if (Range->Binding != NULL && !GpuVaReferenceBinding(Range->Binding))
                    {
                        ExReleaseFastMutex(&Process->GpuVaLock);
                        Status = STATUS_DELETE_PENDING;
                        goto Requeue;
                    }
                    Spans[SpanCount].Binding = Range->Binding;
                    if (Range->Binding != NULL)
                        MapAllocation = Range->Binding->BackingAllocation != NULL ? Range->Binding->BackingAllocation : Range->Binding->LogicalAllocation;
                    Spans[SpanCount].Start = Range->GpuVirtualAddress;
                    Spans[SpanCount].End = Range->GpuVirtualAddress + Range->SizeInBytes;
                    Spans[SpanCount].MiniportHandle = MapAllocation != NULL ? MapAllocation->MiniportHandle : NULL;
                    Spans[SpanCount].AllocationOffset = Range->AllocationOffset;
                    Spans[SpanCount].DriverProtection = Range->DriverProtection;
                    SpanCount++;
                }
            }
        }
        Process->PageTableUpdatePending = FALSE;
        Process->PageTableUpdateStart = 0;
        Process->PageTableUpdateEnd = 0;
        ExReleaseFastMutex(&Process->GpuVaLock);
        break;
    }

    /* Build one ordered paging transaction, matching native VidMm's shared
     * paging-buffer path. CPU_VIRTUAL describes how dxgkrnl reaches its
     * tables; the miniport still owns conversion to its native PTE encoding.
     * A table is never removed before process teardown, and the caller holds
     * the process throughout this transaction. */
    RtlZeroMemory(&ZeroPte, sizeof(ZeroPte));
    for (TableIndex = 0; TableIndex < TableCount; ++TableIndex)
    {
        PDXGKP_GPUVA_TABLE_SNAPSHOT Snapshot = &Tables[TableIndex];
        PDXGKRNL_GPUVA_PAGE_TABLE Table = Snapshot->Table;
        ULONGLONG EntryCoverage;

        EntryCoverage = 1ULL << GpuVaLevelShift(Adapter, Table->Level);

        RtlZeroMemory(&Op, sizeof(Op));
        Op.Type = DxgkPagingOpUpdatePageTable;
        Op.hMiniportDevice = PagingMiniportDevice;
        Op.hMiniportProcess = Process->hMiniportProcess;
        Op.PageTableLevel = Table->Level;
        Op.PageTableAddress.CpuVirtual = Table->KernelVa;
        Op.UpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        if (Snapshot->InitialUpdatePending)
        {
            /* InitialUpdate describes the whole implicit table and cannot
             * attribute valid entries from several allocations at once. */
            Op.PageTableEntries = &ZeroPte;
            Op.StartIndex = 0;
            Op.NumPageTableEntries = Table->EntryCount;
            Op.Repeat = TRUE;
            Op.InitialUpdate = TRUE;
            Op.StartVirtualAddress = Table->CoverageBase;
            Status = GpuVaAppendPagingOperation(&Operations,
                                                 &OperationCount,
                                                 &OperationCapacity,
                                                 &Op);
            if (!NT_SUCCESS(Status))
                goto Requeue;
        }

        Op.Repeat = FALSE;
        Op.InitialUpdate = FALSE;
        if (Table->Level != 0 || SpanCount == 0)
        {
            /* Directory entries and unowned leaf entries do not name an
             * allocation, but still come from the coherent snapshot. */
            Op.PageTableEntries = Snapshot->Entries;
            Op.StartIndex = Snapshot->StartIndex;
            Op.NumPageTableEntries = Snapshot->EndIndex - Snapshot->StartIndex;
            Op.StartVirtualAddress = Table->CoverageBase +
                                     (ULONGLONG)Snapshot->StartIndex * EntryCoverage;
            Status = GpuVaAppendPagingOperation(&Operations,
                                                 &OperationCount,
                                                 &OperationCapacity,
                                                 &Op);
            if (!NT_SUCCESS(Status))
                goto Requeue;
        }
        if (Table->Level == 0 && SpanCount != 0)
        {
            /* Split leaf updates at allocation boundaries. */
            ULONGLONG Cursor = Table->CoverageBase + (ULONGLONG)Snapshot->StartIndex * EntryCoverage;
            ULONGLONG SpanLimit = Table->CoverageBase + (ULONGLONG)Snapshot->EndIndex * EntryCoverage;
            ULONG SpanIndex = 0;

            while (Cursor < SpanLimit)
            {
                ULONGLONG PieceEnd;
                PDXGKP_GPUVA_MAP_SPAN Span = NULL;
                ULONG Covering;
                ULONG PieceStartIndex;
                ULONG PieceEndIndex;

                if (!DxgkGpuVaCoreNextMapPiece(Spans, SpanCount, sizeof(*Spans), Cursor, SpanLimit, &SpanIndex, &PieceEnd, &Covering))
                {
                    Status = STATUS_DATA_ERROR;
                    goto Requeue;
                }
                if (Covering != MAXULONG)
                    Span = &Spans[Covering];
                PieceStartIndex = (ULONG)((Cursor - Table->CoverageBase) / EntryCoverage);
                PieceEndIndex = (ULONG)(((PieceEnd - Table->CoverageBase) + EntryCoverage - 1) / EntryCoverage);
                if (PieceEndIndex > Table->EntryCount)
                    PieceEndIndex = Table->EntryCount;
                if (PieceStartIndex < PieceEndIndex)
                {
                    Op.PageTableEntries = Snapshot->Entries +
                                          (PieceStartIndex - Snapshot->StartIndex);
                    Op.StartIndex = PieceStartIndex;
                    Op.NumPageTableEntries = PieceEndIndex - PieceStartIndex;
                    Op.StartVirtualAddress = Cursor;
                    Op.hMiniportAllocation = Span != NULL ? Span->MiniportHandle : NULL;
                    Op.AllocationOffsetInBytes = Span != NULL && Span->MiniportHandle != NULL ? Span->AllocationOffset + (Cursor - Span->Start) : 0;
                    Op.DriverProtection = Span != NULL ? Span->DriverProtection : 0;
                    Status = GpuVaAppendPagingOperation(&Operations,
                                                         &OperationCount,
                                                         &OperationCapacity,
                                                         &Op);
                    if (!NT_SUCCESS(Status))
                        goto Requeue;
                }
                Cursor = PieceEnd;
            }
        }
    }

    /* UpdateGpuVirtualAddressSystemCommand always calls FlushGpuVaTlb for the
     * changed process range. MustFlushTlbOnValidTransition belongs to the
     * separate scratch/paging helpers and must not suppress this operation. */
    RtlZeroMemory(&Op, sizeof(Op));
    Op.Type = DxgkPagingOpFlushTlb;
    Op.hMiniportDevice = PagingMiniportDevice;
    Op.hMiniportProcess = Process->hMiniportProcess;
    Op.RootPageTableAddress = Process->RootPageTableAddress;
    Op.StartVirtualAddress = Start;
    Op.EndVirtualAddress = End;
    if (!Adapter->GpuMmuCaps.InvalidTlbEntriesNotCached)
    {
        /* A zero range asks the miniport to invalidate the entire address space. */
        Op.StartVirtualAddress = 0;
        Op.EndVirtualAddress = 0;
    }
    Status = GpuVaAppendPagingOperation(&Operations,
                                         &OperationCount,
                                         &OperationCapacity,
                                         &Op);
    if (!NT_SUCCESS(Status))
        goto Requeue;
    if (OperationCount == 0)
    {
        Status = STATUS_DATA_ERROR;
        goto Requeue;
    }

    /* Build every operation before admitting the single combined packet. This
     * preserves update order, avoids one DMA allocation and fence wait per PTE
     * span, and ensures a BuildPagingBuffer failure submits none of the batch. */
    Status = GpuVaExecutePageTableBatch(Adapter,
                                          Operations,
                                          OperationCount,
                                          &TransactionHeld);
    if (!NT_SUCCESS(Status))
    {
        /* Busy admission is retried after dropping both locks below. */
        if (Status == STATUS_DEVICE_BUSY)
            goto Requeue;
        if (Status == STATUS_DELETE_PENDING ||
            Status == STATUS_DEVICE_REMOVED ||
            Status == STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE ||
            Status == STATUS_IO_TIMEOUT)
        {
            DXGKRNL_WARN("DxgkGpuVa: page-table transaction deferred "
                         "0x%08lX over [0x%I64x,0x%I64x)\n",
                         Status,
                         Start,
                         End);
        }
        else
        {
            DXGKRNL_ERR("DxgkGpuVa: page-table transaction rejected "
                               "0x%08lX over [0x%I64x,0x%I64x)\n",
                               Status,
                               Start,
                               End);
        }
        goto Requeue;
    }

    ExAcquireFastMutex(&Process->GpuVaLock);
    for (TableIndex = 0; TableIndex < TableCount; ++TableIndex)
    {
        if (Tables[TableIndex].InitialUpdatePending)
            Tables[TableIndex].Table->InitialUpdatePending = FALSE;
    }
    ExReleaseFastMutex(&Process->GpuVaLock);
    goto Complete;

Requeue:
    GpuVaRequeuePageTableUpdate(Process, Start, End);

Complete:
    /* Binding/device dereferences can reenter the miniport. Release the flush
     * mutex first, including after a fence wait that dropped the transaction. */
    KeReleaseMutex(&Process->PageTableFlushMutex, FALSE);
    if (TransactionHeld)
        DxgkEndKmdTransaction(Adapter);
    if (Operations != NULL)
        ExFreePoolWithTag(Operations, TAG_DXGK_GPUVA_PT);
    if (Tables != NULL)
    {
        for (ReleaseIndex = 0; ReleaseIndex < TableCount; ++ReleaseIndex)
            ExFreePoolWithTag(Tables[ReleaseIndex].Entries, TAG_DXGK_GPUVA_PT);
        ExFreePoolWithTag(Tables, TAG_DXGK_GPUVA_PT);
    }
    if (Spans != NULL)
    {
        for (ReleaseIndex = 0; ReleaseIndex < SpanCount; ++ReleaseIndex)
            if (Spans[ReleaseIndex].Binding != NULL)
                GpuVaDereferenceBinding(Spans[ReleaseIndex].Binding);
        ExFreePoolWithTag(Spans, TAG_DXGK_GPUVA_PT);
    }
    if (PagingDevice != NULL)
        DxgkDereferenceDevice(PagingDevice);
    return Status;
}

static NTSTATUS
DxgkpGpuVaFlushPageTableUpdates(
    _In_ PDXGKRNL_PROCESS Process,
    _In_opt_ PDXGKRNL_DEVICE OwnedDevice)
{
    ULONG Retry;
    NTSTATUS Status;

    for (Retry = 0;; ++Retry)
    {
        LARGE_INTEGER Interval;

        Status = DxgkpGpuVaFlushPageTableUpdatesOnce(Process, OwnedDevice);
        if (Retry >= 999 || Process == NULL || Process->Adapter == NULL)
            return Status;
        if (Status == STATUS_DELETE_PENDING &&
            GpuVaWaitForKmdResetBoundary(Process->Adapter, OwnedDevice))
            continue;
        if (Status != STATUS_DEVICE_BUSY)
            return Status;

        /* Retain dirty PTEs, but never wait for another admission while holding
         * PageTableFlushMutex. Destruction callbacks need the same mutex. */
        Interval.QuadPart = -10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }
}

NTSTATUS
DxgkGpuVaFlushPageTableUpdates(
    _In_ PDXGKRNL_PROCESS Process)
{
    return DxgkpGpuVaFlushPageTableUpdates(Process, NULL);
}

NTSTATUS
DxgkGpuVaFlushPageTableUpdatesForDevice(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ PDXGKRNL_DEVICE Device)
{
    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkpGpuVaFlushPageTableUpdates(Process, Device);
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
            ULONGLONG CoverageBase;

            if (!Allocate)
                return NULL;
            CoverageBase = Va & ~((1ULL << GpuVaLevelShift(Adapter, Level)) - 1ULL);
            Child = GpuVaAllocPageTable(Process, Level - 1, CoverageBase, Table, Index);
            if (Child == NULL)
                return NULL;
            Table->Children[Index] = Child;
            GpuVaLinkChildEntry(Table, Index, Child);
            if (!NT_SUCCESS(GpuVaNotifyPageTableUpdate(Process,
                                                       Table,
                                                       Index,
                                                       1,
                                                       CoverageBase)))
            {
                Table->Entries[Index].Flags = 0;
                Table->Entries[Index].PageTableAddress = 0;
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
 * placement.  For a nonzero segment DXGK_PTE carries an offset from the start
 * of that segment; segment zero carries a system-memory physical address.
 * Returns FALSE when the allocation has no usable placement.
 */
static BOOLEAN
GpuVaAllocationPageAddress(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONGLONG Offset,
    _Out_ PULONG SegmentId,
    _Out_ PULONGLONG PageAddress)
{
    PHYSICAL_ADDRESS Physical;

    if (Allocation == NULL || SegmentId == NULL || PageAddress == NULL ||
        Offset >= Allocation->Size)
    {
        return FALSE;
    }

    *SegmentId = 0;
    *PageAddress = 0;
    if (Allocation->Resident && Allocation->SegmentId != 0)
    {
        PDXGKRNL_ADAPTER Adapter = Allocation->Adapter;
        PDXGKRNL_SEGMENT Segment;

        if (Allocation->SegmentId > 31 || Adapter == NULL ||
            Adapter->Segments == NULL ||
            Allocation->SegmentId > Adapter->SegmentCount ||
            Allocation->SegmentOffset > MAXULONGLONG - Offset)
        {
            return FALSE;
        }
        Segment = &((PDXGKRNL_SEGMENT)Adapter->Segments)
                       [Allocation->SegmentId - 1];
        /* GPU virtual mappings of aperture allocations name system pages,
         * including allocations also mapped for physical engine access. */
        if (Segment->Flags.Aperture || Segment->Flags.Agp)
        {
            if (Allocation->SystemMemory == NULL)
                return FALSE;
            Physical = MmGetPhysicalAddress(
                           (PUCHAR)Allocation->SystemMemory + Offset);
            if (Physical.QuadPart == 0)
                return FALSE;
            *PageAddress = GpuVaPteAddress(
                               (ULONGLONG)Physical.QuadPart);
            return TRUE;
        }
        *SegmentId = Allocation->SegmentId;
        *PageAddress = GpuVaPteAddress(
                           Allocation->SegmentOffset + Offset);
        return TRUE;
    }

    if (Allocation->SystemMemory != NULL)
        Physical = MmGetPhysicalAddress((PUCHAR)Allocation->SystemMemory + Offset);
    else if (Allocation->Resident &&
             Allocation->PhysicalAddress.QuadPart != 0)
        Physical.QuadPart = Allocation->PhysicalAddress.QuadPart + (LONGLONG)Offset;
    else
        Physical.QuadPart = 0;

    if (Physical.QuadPart == 0)
        return FALSE;
    *PageAddress = GpuVaPteAddress((ULONGLONG)Physical.QuadPart);
    return TRUE;
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
        ULONG Index;

        if (Leaf == NULL)
            continue;
        Index = GpuVaPteIndexFor(Process->Adapter, Address + Offset, 0);
        Leaf->Entries[Index].Flags = 0;
        Leaf->Entries[Index].PageAddress = 0;
        (VOID)GpuVaNotifyPageTableUpdate(Process,
                                         Leaf,
                                         Index,
                                         1,
                                         Address + Offset);
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
        DXGK_PTE Pte;

        if (Leaf == NULL)
        {
            GpuVaClearPteSpan(Process, Address, Offset);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(&Pte, sizeof(Pte));
        if (Allocation != NULL)
        {
            ULONG SegmentId;
            ULONGLONG PageAddress;

            if (!GpuVaAllocationPageAddress(Allocation,
                                            AllocationOffset + Offset,
                                            &SegmentId,
                                            &PageAddress))
            {
                GpuVaClearPteSpan(Process, Address, Offset);
                return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
            }
            Pte.Valid = 1;
            Pte.CacheCoherent =
                Process->Adapter->GpuMmuCaps.CacheCoherentMemorySupported ?
                    1 : 0;
            /* Protection bits are only valid when the miniport advertised
             * support for them in DXGK_GPUMMUCAPS.  Intel's N100 node reports
             * neither capability; emitting ReadOnly/NoExecute there creates
             * PTE encodings the hardware contract does not define. */
            Pte.ReadOnly =
                Process->Adapter->GpuMmuCaps.ReadOnlyMemorySupported &&
                !Protection.Write;
            Pte.NoExecute =
                Process->Adapter->GpuMmuCaps.NoExecuteMemorySupported &&
                !Protection.Execute;
            Pte.Segment = SegmentId;
            Pte.PageAddress = PageAddress;
        }
        else
        {
            Pte.Valid = 1;
            Pte.Zero = 1;
        }

        {
            ULONG Index = GpuVaPteIndexFor(Process->Adapter, Address + Offset, 0);

            Leaf->Entries[Index] = Pte;
            if (!NT_SUCCESS(GpuVaNotifyPageTableUpdate(Process,
                                                       Leaf,
                                                       Index,
                                                       1,
                                                       Address + Offset)))
            {
                Leaf->Entries[Index].Flags = 0;
                Leaf->Entries[Index].PageAddress = 0;
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
    InitializeListHead(&Process->GpuVaPinList);
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
    Process->GpuVaPageTableBytes = 0;
    Process->GpuVaPageTableBudget = min(GPUVA_MAX_PAGE_TABLE_BUDGET,
        max(GPUVA_MIN_PAGE_TABLE_BUDGET,
            ((ULONGLONG)SharedUserData->NumberOfPhysicalPages << PAGE_SHIFT) / 64));

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

    ExAcquireFastMutex(&Process->GpuVaLock);
    GpuVaPinFreeAll(Process);
    ExReleaseFastMutex(&Process->GpuVaLock);

    /* Free the software page tables (root included).  Releasing a table's
     * placement is a paging submission, so this runs outside GpuVaLock;
     * teardown owns the process exclusively at this point. */
    GpuVaFreePageTables(Process);

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
            NTSTATUS Status = STATUS_UNSUCCESSFUL;

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

    if (Process == NULL || OutAddress == NULL || SizeInBytes == 0 || (SizeInBytes & GPUVA_RESERVATION_MASK) != 0 || ReservationType > D3DDDIGPUVIRTUALADDRESS_RESERVE_NO_COMMIT)
        return STATUS_INVALID_PARAMETER;

    *OutAddress = 0;
    if (BaseAddress != 0)
    {
        if ((BaseAddress & GPUVA_RESERVATION_MASK) != 0 || BaseAddress < GPUVA_START_ADDRESS || BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE || SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        /*
         * MaximumAddress is an *inclusive* upper bound -- the highest usable
         * byte -- so its low bits are all ones by construction (Intel's ICD
         * passes 0xb801ffffffff).  Requiring it to be allocation-aligned
         * therefore rejected every such request with STATUS_INVALID_PARAMETER,
         * which is what made DxgkCbMapContextAllocation and the GPU VA
         * reservations fail.  Only MinimumAddress names a placement candidate
         * and has to be aligned; convert the inclusive bound to the exclusive
         * limit the search below already assumes.
         */
        if (MinAddress != 0 && (MinAddress & GPUVA_RESERVATION_MASK) != 0)
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress != 0 && MaxAddress != MAXULONGLONG)
            MaxAddress += 1;
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
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes, GPUVA_RESERVATION_ALIGNMENT);
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
    if (Allocation != NULL &&
        (AllocationOffset >= Allocation->Size ||
         (Allocation->Size & GPUVA_PAGE_MASK) != 0))
        return STATUS_INVALID_PARAMETER;
    if (Allocation != NULL &&
        (SizeInBytes - 1) / (Allocation->Size - AllocationOffset) >= GPUVA_MAX_PROCESS_RANGES)
        return STATUS_QUOTA_EXCEEDED;

    *OutAddress = 0;
    if (BaseAddress != 0)
    {
        if (BaseAddress < GPUVA_START_ADDRESS || BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE || SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        /*
         * MaximumAddress is an *inclusive* upper bound -- the highest usable
         * byte -- so its low bits are all ones by construction (Intel's ICD
         * passes 0xb801ffffffff).  Requiring it to be allocation-aligned
         * therefore rejected every such request with STATUS_INVALID_PARAMETER,
         * which is what made DxgkCbMapContextAllocation and the GPU VA
         * reservations fail.  Only MinimumAddress names a placement candidate
         * and has to be aligned; convert the inclusive bound to the exclusive
         * limit the search below already assumes.
         */
        if ((MinAddress & GPUVA_PAGE_MASK) != 0)
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress != 0 && MaxAddress != MAXULONGLONG)
            MaxAddress += 1;
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
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes, GPUVA_RESERVATION_ALIGNMENT);
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
    D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection;
    NTSTATUS Status;

    PAGED_CODE();

    DPRINT("DxgkGpuVaReserve: Process=%p Base=0x%I64x Size=0x%I64x\n",
           Process, BaseAddress, SizeInBytes);

    if (Process == NULL || OutAddress == NULL || SizeInBytes == 0 || (SizeInBytes & GPUVA_RESERVATION_MASK) != 0 || ReservationType > D3DDDIGPUVIRTUALADDRESS_RESERVE_NO_COMMIT)
        return STATUS_INVALID_PARAMETER;

    *OutAddress = 0;
    Protection.Value = 0;
    Protection.NoAccess = ReservationType == D3DDDIGPUVIRTUALADDRESS_RESERVE_NO_ACCESS;
    Protection.Zero = ReservationType == D3DDDIGPUVIRTUALADDRESS_RESERVE_ZERO;
    if (BaseAddress != 0)
    {
        if ((BaseAddress & GPUVA_RESERVATION_MASK) != 0 || BaseAddress < GPUVA_START_ADDRESS || BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE || SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        /*
         * MaximumAddress is an *inclusive* upper bound -- the highest usable
         * byte -- so its low bits are all ones by construction (Intel's ICD
         * passes 0xb801ffffffff).  Requiring it to be allocation-aligned
         * therefore rejected every such request with STATUS_INVALID_PARAMETER,
         * which is what made DxgkCbMapContextAllocation and the GPU VA
         * reservations fail.  Only MinimumAddress names a placement candidate
         * and has to be aligned; convert the inclusive bound to the exclusive
         * limit the search below already assumes.
         */
        if (MinAddress != 0 && (MinAddress & GPUVA_RESERVATION_MASK) != 0)
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress != 0 && MaxAddress != MAXULONGLONG)
            MaxAddress += 1;
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
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes, GPUVA_RESERVATION_ALIGNMENT);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            DPRINT1("DxgkGpuVaReserve: no free VA region for %I64u bytes\n",
                    SizeInBytes);
            return STATUS_NO_MEMORY;
        }
    }

    if (Protection.Zero)
    {
        Status = GpuVaEnsureRootPageTable(Process);
        if (NT_SUCCESS(Status))
            Status = GpuVaWritePteSpan(Process, ActualAddress, SizeInBytes, NULL, 0, Protection);
        if (!NT_SUCCESS(Status))
        {
            GpuVaClearPteSpan(Process, ActualAddress, SizeInBytes);
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            return Status;
        }
    }

    /* Populate the range. */
    Range->GpuVirtualAddress = ActualAddress;
    Range->SizeInBytes       = SizeInBytes;
    Range->State             = GpuVaStateReserved;
    Range->hAllocation       = NULL;
    Range->AllocationOffset  = 0;
    Range->Protection        = Protection;
    Range->DriverProtection = DriverProtection;
    Range->ReservationBase   = ActualAddress;
    Range->ReservationSize   = SizeInBytes;

    GpuVaInsertRange(Process, Range);
    Process->GpuVaRangeCount++;
    Process->GpuVaTotalReserved += SizeInBytes;

    ExReleaseFastMutex(&Process->GpuVaLock);

    *OutAddress = ActualAddress;

    DxgkGpuVaRecordEvent('R', ActualAddress, SizeInBytes, 0);
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
    /* A submitted command buffer executes out of this address; the miniport
     * still holds it, so it cannot be taken away yet. */
    if (GpuVaRangeIsPinned(Process, BaseAddress, SizeInBytes))
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return STATUS_DEVICE_BUSY;
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
    DxgkGpuVaRecordEvent('F', BaseAddress, SizeInBytes, 0);
    DPRINT("DxgkGpuVaFree: freed at 0x%I64x\n", BaseAddress);
    return STATUS_SUCCESS;
}


/* Keep each repeated allocation slice as an ordinary bounded range. Splits,
 * remaps, copies, residency snapshots and destruction then retain their usual
 * allocation-offset and binding lifetime rules. The whole virtual mapping
 * shares one reservation, so it can still be freed in a single call. */
static NTSTATUS
GpuVaCreateMapRanges(
    _Out_ PLIST_ENTRY Ranges,
    _In_opt_ PDXGKRNL_GPUVA_BINDING Binding,
    _In_ D3DKMT_HANDLE AllocationHandle,
    _In_ ULONGLONG AllocationOffset,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _In_ D3DGPU_VIRTUAL_ADDRESS ReservationBase,
    _In_ ULONGLONG ReservationSize,
    _In_ D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection,
    _In_ UINT64 DriverProtection,
    _Out_ PULONG RangeCount)
{
    ULONGLONG Offset = 0;

    InitializeListHead(Ranges);
    *RangeCount = 0;
    while (Offset < Size)
    {
        ULONGLONG SourceOffset = 0;
        ULONGLONG ChunkSize = Size;
        PDXGKRNL_GPUVA_RANGE Range;

        if (Binding != NULL &&
            !DxgkGpuVaCoreAllocationMapChunk(Binding->BackingAllocation->Size,
                                             AllocationOffset, Size, Offset,
                                             &SourceOffset, &ChunkSize))
            return STATUS_INVALID_PARAMETER;
        Range = GpuVaAllocRange();
        if (Range == NULL)
            return STATUS_NO_MEMORY;
        if (Binding != NULL && !GpuVaReferenceBinding(Binding))
        {
            GpuVaFreeRange(Range);
            return STATUS_DELETE_PENDING;
        }
        Range->Binding = Binding;
        Range->GpuVirtualAddress = Address + Offset;
        Range->SizeInBytes = ChunkSize;
        Range->State = Binding != NULL ? GpuVaStateMapped : GpuVaStateReserved;
        Range->hAllocation = Binding != NULL ? (HANDLE)(ULONG_PTR)AllocationHandle : NULL;
        Range->AllocationOffset = SourceOffset;
        Range->Protection = Protection;
        Range->DriverProtection = DriverProtection;
        Range->ReservationBase = ReservationBase;
        Range->ReservationSize = ReservationSize;
        InsertTailList(Ranges, &Range->RangeListEntry);
        ++*RangeCount;
        Offset += ChunkSize;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
GpuVaWriteMapPtes(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _In_opt_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONGLONG AllocationOffset,
    _In_ D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection)
{
    ULONGLONG Offset = 0;

    while (Offset < Size)
    {
        ULONGLONG SourceOffset = 0;
        ULONGLONG ChunkSize = Size;
        NTSTATUS Status;

        if (Allocation != NULL &&
            !DxgkGpuVaCoreAllocationMapChunk(Allocation->Size, AllocationOffset,
                                             Size, Offset, &SourceOffset, &ChunkSize))
            return STATUS_INVALID_PARAMETER;
        Status = GpuVaWritePteSpan(Process, Address + Offset, ChunkSize,
                                   Allocation, SourceOffset, Protection);
        if (!NT_SUCCESS(Status))
            return Status;
        Offset += ChunkSize;
    }
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
    _In_ D3DGPU_VIRTUAL_ADDRESS PlannedAddress,
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
    if (Allocation != NULL &&
        (AllocationOffset >= Allocation->Size ||
         (Allocation->Size & GPUVA_PAGE_MASK) != 0))
        return STATUS_INVALID_PARAMETER;
    if (Allocation != NULL &&
        (SizeInBytes - 1) / (Allocation->Size - AllocationOffset) >= GPUVA_MAX_PROCESS_RANGES)
        return STATUS_QUOTA_EXCEEDED;

    *OutAddress = 0;
    /* A bridge-selected automatic address is provisional until this lock is
     * acquired. Preserve that distinction from an explicit caller remap. */
    if (PlannedAddress != 0 &&
        (BaseAddress != 0 || (PlannedAddress & GPUVA_RESERVATION_MASK) != 0))
        return STATUS_INVALID_PARAMETER;
    if (BaseAddress != 0)
    {
        if (BaseAddress < GPUVA_START_ADDRESS ||
            BaseAddress >= GPUVA_DEFAULT_SPACE_SIZE ||
            SizeInBytes > GPUVA_DEFAULT_SPACE_SIZE - BaseAddress)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        /*
         * MaximumAddress is an *inclusive* upper bound -- the highest usable
         * byte -- so its low bits are all ones by construction (Intel's ICD
         * passes 0xb801ffffffff).  Requiring it to be allocation-aligned
         * therefore rejected every such request with STATUS_INVALID_PARAMETER,
         * which is what made DxgkCbMapContextAllocation and the GPU VA
         * reservations fail.  Only MinimumAddress names a placement candidate
         * and has to be aligned; convert the inclusive bound to the exclusive
         * limit the search below already assumes.
         */
        if ((MinAddress & GPUVA_PAGE_MASK) != 0)
            return STATUS_INVALID_PARAMETER;
        if (MinAddress == 0)
            MinAddress = GPUVA_START_ADDRESS;
        if (MaxAddress != 0 && MaxAddress != MAXULONGLONG)
            MaxAddress += 1;
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

    if (PlannedAddress != 0)
    {
        ActualAddress = PlannedAddress;
        if (ActualAddress < MinAddress || ActualAddress >= MaxAddress ||
            SizeInBytes > MaxAddress - ActualAddress)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_INVALID_PARAMETER;
        }
        if (GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes) != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            if (Binding != NULL)
                GpuVaDereferenceBinding(Binding);
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else if (BaseAddress != 0)
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
        if (GpuVaRangeIsPinned(Process, ActualAddress, SizeInBytes))
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
        ActualAddress = GpuVaFindFreeRegion(Process, MinAddress, MaxAddress, SizeInBytes, GPUVA_RESERVATION_ALIGNMENT);
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
        LIST_ENTRY Ranges;
        ULONG RangeCount;

        Status = GpuVaCreateMapRanges(&Ranges, Binding, hAllocation,
                                       AllocationOffset, ActualAddress, SizeInBytes,
                                       ActualAddress, SizeInBytes, Protection,
                                       DriverProtection, &RangeCount);
        if (NT_SUCCESS(Status) &&
            RangeCount > GPUVA_MAX_PROCESS_RANGES - Process->GpuVaRangeCount)
            Status = STATUS_QUOTA_EXCEEDED;
        if (NT_SUCCESS(Status))
        {
            Status = GpuVaWriteMapPtes(Process, ActualAddress, SizeInBytes,
                                      Allocation, AllocationOffset, Protection);
            if (!NT_SUCCESS(Status))
                GpuVaClearPteSpan(Process, ActualAddress, SizeInBytes);
        }
        if (Binding != NULL)
            GpuVaDereferenceBinding(Binding);
        if (!NT_SUCCESS(Status))
        {
            GpuVaFreeList(&Ranges);
            ExReleaseFastMutex(&Process->GpuVaLock);
            return Status;
        }
        while (!IsListEmpty(&Ranges))
        {
            PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(
                RemoveHeadList(&Ranges), DXGKRNL_GPUVA_RANGE, RangeListEntry);

            InitializeListHead(&Range->RangeListEntry);
            GpuVaInsertRange(Process, Range);
        }
        Process->GpuVaRangeCount += RangeCount;
        Process->GpuVaTotalReserved += SizeInBytes;
        if (Allocation != NULL)
            Process->GpuVaTotalMapped += SizeInBytes;
    }
    else
    {
        LIST_ENTRY WorkingHead;
        LIST_ENTRY ReplacementHead;
        ULONG ReplacementCount;
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

        Status = GpuVaCreateMapRanges(&ReplacementHead, Binding, hAllocation,
                                       AllocationOffset, ActualAddress, SizeInBytes,
                                       ReservationBase, ReservationSize, Protection,
                                       DriverProtection, &ReplacementCount);
        if (Binding != NULL)
            GpuVaDereferenceBinding(Binding);
        if (NT_SUCCESS(Status))
            Status = GpuVaCloneList(&Process->GpuVaRangeList, &WorkingHead);
        if (!NT_SUCCESS(Status))
        {
            GpuVaFreeList(&ReplacementHead);
            ExReleaseFastMutex(&Process->GpuVaLock);
            return Status;
        }

        Status = GpuVaReplaceSpan(&WorkingHead, ActualAddress, SizeInBytes, &ReplacementHead);
        GpuVaFreeList(&ReplacementHead);
        if (NT_SUCCESS(Status) && !GpuVaCountList(&WorkingHead, GPUVA_MAX_PROCESS_RANGES, &WorkingCount))
            Status = STATUS_QUOTA_EXCEEDED;
        if (NT_SUCCESS(Status))
        {
            Status = GpuVaWriteMapPtes(Process, ActualAddress, SizeInBytes, Allocation, AllocationOffset, Protection);
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
    DxgkGpuVaRecordEvent('M', ActualAddress, SizeInBytes, (ULONG)hAllocation);
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
                                        GPUVA_DEFAULT_SPACE_SIZE, GPUVA_PAGE_SIZE,
                                        GPUVA_RESERVATION_ALIGNMENT);
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
    Pte.CacheCoherent =
        Adapter->GpuMmuCaps.CacheCoherentMemorySupported ? 1 : 0;
    Pte.NoExecute =
        Adapter->GpuMmuCaps.NoExecuteMemorySupported ? 1 : 0;
    Pte.PageAddress = GpuVaPteAddress((ULONGLONG)Physical.QuadPart);
    {
        ULONG Index = GpuVaPteIndexFor(Process->Adapter, ActualAddress, 0);

        Leaf->Entries[Index] = Pte;
        if (!NT_SUCCESS(GpuVaNotifyPageTableUpdate(Process,
                                                   Leaf,
                                                   Index,
                                                   1,
                                                   ActualAddress)))
        {
            Leaf->Entries[Index].Flags = 0;
            Leaf->Entries[Index].PageAddress = 0;
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
    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter ||
        !Adapter->GpuMmuCapsValid || !Adapter->PageTableLevelsValid ||
        Process->hMiniportProcess == NULL || Process->hRootPageTable == NULL ||
        Process->RootPageTableEntries == 0 || !Process->RootPageTableProgrammed)
        return FALSE;
    if (Adapter->MiniportContext == NULL ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
        return FALSE;
    if (DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetRootPageTable) == NULL ||
        (GpuVaLevelCount(Adapter) == 2 &&
         DXGK_CB_FULL(Adapter, DxgkDdiGetRootPageTableSize) == NULL))
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
static BOOLEAN
DxgkpGpuVaPinRange(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size,
    _In_ BOOLEAN RequireExecute,
    _In_ BOOLEAN AllowUnmapped,
    _Out_opt_ PBOOLEAN Pinned)
{
    BOOLEAN Valid;

    PAGED_CODE();

    if (Pinned != NULL)
        *Pinned = FALSE;
    if (Process == NULL || Address == 0 || Size == 0)
        return FALSE;
    if (!DxgkGpuVaPageTableReady(Adapter, Process))
        return FALSE;

    ExAcquireFastMutex(&Process->GpuVaLock);
    Valid = GpuVaListCoversRange(&Process->GpuVaRangeList, Address, Size, GpuVaStateMapped);
    if (!Valid && AllowUnmapped)
    {
        ExReleaseFastMutex(&Process->GpuVaLock);
        return TRUE;
    }
    if (Valid && RequireExecute &&
        Adapter->GpuMmuCaps.NoExecuteMemorySupported &&
        !GpuVaListAllowsExecute(&Process->GpuVaRangeList,
                                Address,
                                Size))
    {
        Valid = FALSE;
    }
    if (Valid)
        Valid = GpuVaPinAcquire(Process, Address, Size);
    if (Pinned != NULL)
        *Pinned = Valid;
    ExReleaseFastMutex(&Process->GpuVaLock);
    return Valid;
}

BOOLEAN
DxgkGpuVaPinRange(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    return DxgkpGpuVaPinRange(Adapter, Process, Address, Size, TRUE, FALSE, NULL);
}

/* Some miniports interpret Commands using their private command payload.
 * Pin a VidMm mapping if one exists, while allowing that opaque address
 * contract. Lookup and pin remain atomic with respect to unmap/remap. */
BOOLEAN
DxgkGpuVaPinCommandStart(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _Out_ PBOOLEAN Pinned)
{
    return DxgkpGpuVaPinRange(Adapter, Process, Address, 1, TRUE, TRUE, Pinned);
}

BOOLEAN
DxgkGpuVaPinAllocationRange(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Address,
    _In_ ULONGLONG Size)
{
    return DxgkpGpuVaPinRange(Adapter, Process, Address, Size, FALSE, FALSE, NULL);
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
    GpuVaPinRelease(Process, Address, Size);
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
                DxgkGpuVaRecordEvent('m', Operations[Index].Map.BaseAddress, Operations[Index].Map.SizeInBytes, (ULONG)Operations[Index].Map.hAllocation);
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_UNMAP:
                Status = GpuVaApplyUnmapOperation(&WorkingHead, &Operations[Index]);
                DxgkGpuVaRecordEvent('u', Operations[Index].Unmap.BaseAddress, Operations[Index].Unmap.SizeInBytes, 0);
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_COPY:
                Status = GpuVaApplyCopyOperation(&WorkingHead, &Operations[Index]);
                DxgkGpuVaRecordEvent('c', Operations[Index].Copy.DestAddress, Operations[Index].Copy.SizeInBytes, 0);
                break;
            case D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP_PROTECT:
                Status = GpuVaApplyMapOperation(&WorkingHead, Adapter, Process, &Operations[Index], TRUE);
                DxgkGpuVaRecordEvent('p', Operations[Index].MapProtect.BaseAddress, Operations[Index].MapProtect.SizeInBytes, (ULONG)Operations[Index].MapProtect.hAllocation);
                break;
            default:
                Status = STATUS_INVALID_PARAMETER;
                break;
        }

        if (!NT_SUCCESS(Status))
        {
            /* A refused update leaves the UMD believing it owns an address the
             * GPU cannot reach, which surfaces much later as an execution
             * fault or a hang, so name the operation that was refused. */
            DXGKRNL_ERR("DxgkGpuVaApplyUpdate: operation %lu of %lu type=%u refused 0x%08lX\n",
                               Index, NumOperations, (UINT)Operations[Index].OperationType, Status);
            break;
        }
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

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
/*
 * GpuVaNodeOwnsGpuMmuPageTables
 *
 * WDDM 2.0 makes the addressing contract a property of the node, not of the
 * adapter: DXGK_NODEMETADATA reports GpuMmuSupported and IoMmuSupported per
 * node ordinal, and DXGKQAITYPE_GPUMMUCAPS only describes what GpuMmu would
 * look like on an adapter that has such a node at all.
 *
 * Only a GpuMmu node executes against a page-table hierarchy that the OS
 * builds and owns, so only a GpuMmu node has a root for dxgkrnl to publish.
 * An IoMmu node translates through the owning process's CPU page tables and a
 * node that reports neither addresses memory physically; on either one the
 * page-table tree the GPU walks belongs to the miniport, and handing it a root
 * of ours replaces an address space we do not own with one that describes only
 * the mappings we happen to know about.  A miniport without
 * DxgkDdiGetNodeMetadata has declared no GpuMmu node at all.
 *
 * IRQL: PASSIVE_LEVEL.
 */
static NTSTATUS
GpuVaQueryNodeGpuMmuSupport(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ UINT NodeOrdinal,
    _Out_ PBOOLEAN Supported)
{
    DXGKARG_GETNODEMETADATA Metadata;
    NTSTATUS Status;

    PAGED_CODE();
    *Supported = FALSE;

    if (DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata) == NULL)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Metadata, sizeof(Metadata));
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata)(
                 Adapter->MiniportDeviceContext, NodeOrdinal, &Metadata);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }


    *Supported = Metadata.GpuMmuSupported ? TRUE : FALSE;
    return STATUS_SUCCESS;
}
#endif /* REACTOS_WDDM_TARGET_LEVEL >= 2000 */

/* The caller owns the context stream. Process root placement is read under
 * GpuVaLock; roots live until process teardown. */
BOOLEAN
DxgkGpuVaRootPageTableNeedsUpdate(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ PDXGKRNL_CONTEXT Context)
{
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    BOOLEAN Changed;

    PAGED_CODE();
    if (Context->GpuMmuNodeKnown && !Context->GpuMmuNode)
        return FALSE;
    ExAcquireFastMutex(&Process->GpuVaLock);
    Changed = !Context->RootPageTablePublished ||
        Context->PublishedRootPageTableEpoch !=
            (ULONG)InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0) ||
        Context->PublishedRootPageTableAddress.SegmentId != Process->RootPageTableAddress.SegmentId ||
        Context->PublishedRootPageTableAddress.SegmentOffset != Process->RootPageTableAddress.SegmentOffset ||
        Context->PublishedRootPageTableEntries != Process->RootPageTableEntries;
    ExReleaseFastMutex(&Process->GpuVaLock);
    return Changed;
#else
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Context);
    return FALSE;
#endif
}

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
    if (!DxgkGpuVaRootPageTableNeedsUpdate(Adapter, Process, Context))
        return STATUS_SUCCESS;

    /* The node the context runs on decides who owns its page tables.  Building
     * and publishing a root for a node that is not a GpuMmu node would hand the
     * walker an address space dxgkrnl does not own. */
    if (!Context->GpuMmuNodeKnown)
    {
        NTSTATUS Status = GpuVaQueryNodeGpuMmuSupport(Adapter,
                                                     Context->NodeOrdinal,
                                                     &Context->GpuMmuNode);
        if (!NT_SUCCESS(Status))
            return Status;
        Context->GpuMmuNodeKnown = TRUE;
    }
    if (!Context->GpuMmuNode)
    {
        DPRINT("DxgkGpuVaSetRootPageTable: node %u is not a GpuMmu node; no root page table to publish\n",
               Context->NodeOrdinal);
        return STATUS_SUCCESS;
    }

    /* Page-table allocation, placement and updates precede packet admission.
     * Never queue paging work recursively from the scheduler's dispatch. */
    if (!DxgkGpuVaPageTableReady(Adapter, Process))
        return STATUS_NOT_SUPPORTED;

    {
        PDXGKRNL_GPUVA_PAGE_TABLE Root;
        BOOLEAN Placed;

        /* The published address is where the walker starts, so the root has to
         * own its placement before it is named.  A root still waiting for one
         * has no address at all, and offset zero of a segment is a legal
         * address the miniport cannot tell apart from an unplaced root. */
        ExAcquireFastMutex(&Process->GpuVaLock);
        Root = (PDXGKRNL_GPUVA_PAGE_TABLE)Process->hRootPageTable;
        Placed = (BOOLEAN)(Root != NULL && !Root->PlacementPending);
        ExReleaseFastMutex(&Process->GpuVaLock);
        if (!Placed)
        {
            DXGKRNL_ERR("DxgkGpuVa: root page table has no placement; not publishing a root for context %p\n",
                        Context);
            return STATUS_DEVICE_NOT_READY;
        }
    }

    {
        DXGKARG_SETROOTPAGETABLE SetArgs;

        RtlZeroMemory(&SetArgs, sizeof(SetArgs));
        SetArgs.hContext = Context->hMiniportContext;
        ExAcquireFastMutex(&Process->GpuVaLock);
        SetArgs.Address = Process->RootPageTableAddress;
        SetArgs.NumEntries = Process->RootPageTableEntries;
        ExReleaseFastMutex(&Process->GpuVaLock);
        if (!DxgkAcquireKmdCall(Adapter))
            return STATUS_DELETE_PENDING;
        if (InterlockedCompareExchange(&Context->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        {
            DxgkReleaseKmdCall(Adapter);
            return STATUS_DEVICE_REMOVED;
        }
        DXGK_CB_FULL(Adapter, DxgkDdiSetRootPageTable)(Adapter->MiniportDeviceContext, &SetArgs);
        DXGKRNL_VERBOSE("DxgkGpuVaSetRootPageTable: published root seg=%u off=0x%I64x entries=%u for ctx %p (kmd %p) seq=#%I64d\n",
                     SetArgs.Address.SegmentId, (ULONGLONG)SetArgs.Address.SegmentOffset, SetArgs.NumEntries, Context, SetArgs.hContext, DxgkDiagSequence());
        Context->PublishedRootPageTableAddress = SetArgs.Address;
        Context->PublishedRootPageTableEntries = SetArgs.NumEntries;
        Context->PublishedRootPageTableEpoch =
            (ULONG)InterlockedCompareExchange(&Adapter->SubmittedFenceIdentityEpoch, 0, 0);
        Context->RootPageTablePublished = TRUE;
        DxgkReleaseKmdCall(Adapter);
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

/* TDR diagnostics: recent GPU virtual address operations, in order. */
#define DXGKP_GPUVA_EVENT_RING_SIZE 512

/*
 * TDR diagnostics: hex-dump a GPU-visible buffer through the owning process's
 * GPU VA bindings, and list the process's mapped ranges.  PASSIVE/APC only.
 */
VOID
DxgkGpuVaDumpBuffer(
    _In_opt_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS GpuVa,
    _In_ ULONG Size)
{
    PDXGKRNL_GPUVA_RANGE Range;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    ULONGLONG Offset = 0;
    const ULONG *Words;
    ULONG Count, i;

    if (Process == NULL || GpuVa == 0 || Size == 0)
    {
        DXGKRNL_ERR("DxgkGpuVaDumpBuffer: nothing to dump (process=%p va=0x%I64x size=%lu)\n", Process, GpuVa, Size);
        return;
    }
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        DXGKRNL_ERR("DxgkGpuVaDumpBuffer: irql %u too high\n", KeGetCurrentIrql());
        return;
    }
    ExAcquireFastMutex(&Process->GpuVaLock);
    Range = GpuVaFindOverlapping(Process, GpuVa, 1);
    if (Range != NULL && Range->State == GpuVaStateMapped && Range->Binding != NULL)
    {
        Allocation = Range->Binding->BackingAllocation != NULL ? Range->Binding->BackingAllocation : Range->Binding->LogicalAllocation;
        Offset = (GpuVa - Range->GpuVirtualAddress) + Range->AllocationOffset;
    }
    if (Range == NULL)
    {
        DXGKRNL_ERR("DxgkGpuVaDumpBuffer: va=0x%I64x is not mapped in process %p\n", GpuVa, Process);
    }
    else
    {
        DXGKRNL_ERR("DxgkGpuVaDumpBuffer: va=0x%I64x size=%lu range=[0x%I64x+0x%I64x] state=%d hAlloc=%p alloc=%p offset=0x%I64x\n",
                    GpuVa, Size, Range->GpuVirtualAddress, Range->SizeInBytes, (int)Range->State, Range->hAllocation, Allocation, Offset);
    }
    if (Allocation != NULL)
    {
        DXGKRNL_ERR("DxgkGpuVaDumpBuffer: alloc handle=0x%x size=0x%I64x segment=%u resident=%d sysmem=%p cpu=%p phys=0x%I64x\n",
                    Allocation->Handle, (ULONGLONG)Allocation->Size, Allocation->SegmentId, (int)Allocation->Resident,
                    Allocation->SystemMemory, Allocation->CpuAddress, (ULONGLONG)Allocation->PhysicalAddress.QuadPart);
        if (Allocation->SystemMemory != NULL && Offset + Size <= Allocation->Size)
        {
            Words = (const ULONG *)((PUCHAR)Allocation->SystemMemory + Offset);
            Count = Size / sizeof(ULONG);
            for (i = 0; i < Count; i += 8)
            {
                DXGKRNL_ERR("DMA[%04lx] %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                            i * 4,
                            Words[i], i + 1 < Count ? Words[i + 1] : 0, i + 2 < Count ? Words[i + 2] : 0, i + 3 < Count ? Words[i + 3] : 0,
                            i + 4 < Count ? Words[i + 4] : 0, i + 5 < Count ? Words[i + 5] : 0, i + 6 < Count ? Words[i + 6] : 0, i + 7 < Count ? Words[i + 7] : 0);
            }
        }
    }
    ExReleaseFastMutex(&Process->GpuVaLock);
}

/* Copy Size bytes of GPU-visible process memory at Va into Buffer through
 * the process's mappings (system-memory backed allocations only). */
static BOOLEAN
DxgkpGpuVaReadProcessMemory(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Va,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    PDXGKRNL_GPUVA_RANGE Range;
    PDXGKVMM_ALLOCATION Allocation;
    ULONGLONG Offset;

    RtlZeroMemory(Buffer, Size);
    Range = GpuVaFindOverlapping(Process, Va, Size);
    if (Range == NULL || Range->State != GpuVaStateMapped || Range->Binding == NULL ||
        Va < Range->GpuVirtualAddress || Va + Size > Range->GpuVirtualAddress + Range->SizeInBytes)
    {
        return FALSE;
    }
    Allocation = Range->Binding->BackingAllocation != NULL ? Range->Binding->BackingAllocation : Range->Binding->LogicalAllocation;
    if (Allocation == NULL || Allocation->SystemMemory == NULL)
        return FALSE;
    Offset = (Va - Range->GpuVirtualAddress) + Range->AllocationOffset;
    if (Offset + Size > Allocation->Size)
        return FALSE;
    RtlCopyMemory(Buffer, (PUCHAR)Allocation->SystemMemory + Offset, Size);
    return TRUE;
}

/* Public form of the reader above for submit-time capture: PASSIVE_LEVEL,
 * GpuVaLock not held.  FALSE when [Va, Va+Size) is not one mapped,
 * system-memory-backed span. */
BOOLEAN
DxgkGpuVaCopyFromProcess(
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Va,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    BOOLEAN Copied;

    PAGED_CODE();
    if (Process == NULL || Va == 0 || Buffer == NULL || Size == 0)
        return FALSE;
    ExAcquireFastMutex(&Process->GpuVaLock);
    Copied = DxgkpGpuVaReadProcessMemory(Process, Va, Buffer, Size);
    ExReleaseFastMutex(&Process->GpuVaLock);
    return Copied;
}

/*
 * TDR diagnostics for a Gen12 render batch: find the surface-state base and
 * the last pixel-shader binding table in the batch, then print the surface
 * states the sampler would read.  Canonical (sign-extended) 48-bit addresses
 * are folded back to the GPU VA.
 */
static ULONGLONG GpuVaRawEntry( _In_ PDXGKRNL_GPUVA_PAGE_TABLE Table, _In_ ULONG Index);


VOID
DxgkGpuVaDumpProcessRanges(
    _In_opt_ PDXGKRNL_PROCESS Process)
{
    PLIST_ENTRY Entry;
    ULONG Index = 0;

    if (Process == NULL || KeGetCurrentIrql() > APC_LEVEL)
        return;
    ExAcquireFastMutex(&Process->GpuVaLock);
    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList && Index < 120;
         Entry = Entry->Flink, Index++)
    {
        PDXGKRNL_GPUVA_RANGE Range = CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        PDXGKVMM_ALLOCATION Allocation = NULL;

        if (Range->Binding != NULL)
            Allocation = Range->Binding->BackingAllocation != NULL ? Range->Binding->BackingAllocation : Range->Binding->LogicalAllocation;
        DXGKRNL_ERR("RANGE %3lu va=0x%I64x size=0x%I64x state=%d hAlloc=%p off=0x%I64x prot=0x%I64x seg=%u res=%d asize=0x%I64x\n",
                    Index, Range->GpuVirtualAddress, Range->SizeInBytes, (int)Range->State, Range->hAllocation, Range->AllocationOffset,
                    Range->Protection.Value,
                    Allocation != NULL ? Allocation->SegmentId : 0, Allocation != NULL ? (int)Allocation->Resident : -1,
                    Allocation != NULL ? (ULONGLONG)Allocation->Size : 0ULL);
    }
    ExReleaseFastMutex(&Process->GpuVaLock);
}


static ULONGLONG
GpuVaRawEntry(
    _In_ PDXGKRNL_GPUVA_PAGE_TABLE Table,
    _In_ ULONG Index)
{
    ULONG EntrySize = Table->EntryCount != 0 ? Table->Bytes / Table->EntryCount : 8;
    const UCHAR *Raw = (const UCHAR *)Table->KernelVa + (ULONGLONG)Index * EntrySize;

    if (Table->KernelVa == NULL || EntrySize == 0 || EntrySize > 16)
        return 0;
    if (EntrySize >= 8)
        return *(const volatile ULONGLONG *)Raw;
    return *(const volatile ULONG *)Raw;
}

static ULONG GpuVaVerifyAbove4G;

static VOID
GpuVaVerifyTable(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_GPUVA_PAGE_TABLE Table,
    _Inout_ PULONG Tables,
    _Inout_ PULONG Valid,
    _Inout_ PULONG Missing,
    _Inout_ PULONG InvalidNonzero,
    _Inout_ PULONG Printed)
{
    ULONG i;
    ULONGLONG Coverage = 1ULL << GpuVaLevelShift(Adapter, Table->Level);

    (*Tables)++;
    for (i = 0; i < Table->EntryCount; i++)
    {
        BOOLEAN Ours = Table->Entries[i].Valid != 0;
        ULONGLONG Raw = GpuVaRawEntry(Table, i);

        if (Ours)
        {
            (*Valid)++;
            if (Table->Level == 0 && Table->Entries[i].PageAddress >= 0x100000ULL)
                GpuVaVerifyAbove4G++;
        }
        if (Ours && Raw == 0)
            (*Missing)++;
        else if (!Ours && Raw != 0)
        {
            /* Invalid hardware entries can carry a scratch-page address.
             * Their encoding belongs to the miniport, not DXGK_PTE. */
            (*InvalidNonzero)++;
            continue;
        }
        else
            continue;
        if ((*Printed)++ < 10)
        {
            DXGKRNL_ERR("PT MISMATCH level=%lu table=%p idx=%lu va=0x%I64x ours(valid=%u seg=%u page=0x%I64x flags=0x%I64x) raw=0x%I64x\n",
                        Table->Level, Table->KernelVa, i, Table->CoverageBase + (ULONGLONG)i * Coverage,
                        (UINT)Table->Entries[i].Valid, (UINT)Table->Entries[i].Segment,
                        (ULONGLONG)Table->Entries[i].PageAddress, (ULONGLONG)Table->Entries[i].Flags, Raw);
        }
    }
    if (Table->Level > 0 && Table->Children != NULL)
    {
        for (i = 0; i < Table->EntryCount; i++)
        {
            if (Table->Children[i] != NULL)
                GpuVaVerifyTable(Adapter, Table->Children[i], Tables, Valid, Missing, InvalidNonzero, Printed);
        }
    }
}

/*
 * TDR diagnostics: compare the page-table entries this driver asked the
 * miniport to write (Table->Entries) with what the miniport actually wrote
 * into the GPU-visible tables (Table->KernelVa). Count nonzero invalid
 * encodings separately; ZeroInPteSupported does not specify their format.
 */
VOID
DxgkGpuVaVerifyProcessTables(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_PROCESS Process)
{
    ULONG Tables = 0, Valid = 0, Missing = 0, InvalidNonzero = 0, Printed = 0;
    ULONG Above4G = 0;
    PDXGKRNL_GPUVA_PAGE_TABLE Root;

    if (Adapter == NULL || Process == NULL || KeGetCurrentIrql() > APC_LEVEL || !Adapter->PageTableLevelsValid)
        return;
    ExAcquireFastMutex(&Process->GpuVaLock);
    GpuVaVerifyAbove4G = 0;
    Root = (PDXGKRNL_GPUVA_PAGE_TABLE)Process->hRootPageTable;
    if (Root != NULL)
        GpuVaVerifyTable(Adapter, Root, &Tables, &Valid, &Missing, &InvalidNonzero, &Printed);
    Above4G = GpuVaVerifyAbove4G;
    ExReleaseFastMutex(&Process->GpuVaLock);
    DXGKRNL_ERR("PT VERIFY process=%p root=%p tables=%lu valid-entries=%lu missing-in-hw=%lu invalid-nonzero=%lu leaf-pages-above-4G=%lu (highest-acceptable=0x%I64x)\n",
                Process, Root, Tables, Valid, Missing, InvalidNonzero, Above4G, (ULONGLONG)Adapter->HighestAcceptableAddress.QuadPart);
}

/*
 * Walks every mapped range of a process and reports only those whose page
 * table hierarchy cannot be walked to a leaf.  An "invalid PDE" fault names
 * no address on this hardware, so the mapping that lost its directory entry
 * has to be found by auditing what the process believes is mapped.
 */
VOID
DxgkGpuVaAuditMappings(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_PROCESS Process)
{
    PLIST_ENTRY Entry;
    ULONG Mapped = 0;
    ULONG Broken = 0;

    if (Adapter == NULL || Process == NULL ||
        KeGetCurrentIrql() > APC_LEVEL || !Adapter->PageTableLevelsValid)
    {
        return;
    }

    ExAcquireFastMutex(&Process->GpuVaLock);
    for (Entry = Process->GpuVaRangeList.Flink;
         Entry != &Process->GpuVaRangeList;
         Entry = Entry->Flink)
    {
        PDXGKRNL_GPUVA_RANGE Range =
            CONTAINING_RECORD(Entry, DXGKRNL_GPUVA_RANGE, RangeListEntry);
        PDXGKRNL_GPUVA_PAGE_TABLE Table;
        D3DGPU_VIRTUAL_ADDRESS Va;
        ULONG Level;
        ULONG BadLevel = MAXULONG;

        if (Range->State != GpuVaStateMapped)
            continue;
        Mapped++;

        Va = Range->GpuVirtualAddress;
        Table = (PDXGKRNL_GPUVA_PAGE_TABLE)Process->hRootPageTable;
        for (Level = GpuVaLevelCount(Adapter); Level-- > 0 && Table != NULL;)
        {
            ULONG Index = GpuVaPteIndexFor(Adapter, Va, Level);

            if (Index >= Table->EntryCount || !Table->Entries[Index].Valid)
            {
                BadLevel = Level;
                break;
            }
            Table = (Level > 0 && Table->Children != NULL) ? Table->Children[Index] : NULL;
            if (Level > 0 && Table == NULL)
            {
                BadLevel = Level;
                break;
            }
        }

        if (BadLevel != MAXULONG && Broken < 16)
        {
            Broken++;
            DXGKRNL_ERR("AUDIT va=0x%I64x size=0x%I64x prot=0x%I64x broken at level %lu\n",
                        Range->GpuVirtualAddress,
                        Range->SizeInBytes,
                        Range->DriverProtection,
                        BadLevel);
        }
    }
    ExReleaseFastMutex(&Process->GpuVaLock);

    DXGKRNL_ERR("AUDIT: %lu mapped range(s), %lu with an unwalkable hierarchy\n",
                Mapped, Broken);
}

VOID
DxgkGpuVaDumpTranslation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS Va)
{
    PDXGKRNL_GPUVA_PAGE_TABLE Table;
    ULONG Level;

    if (Adapter == NULL || Process == NULL || KeGetCurrentIrql() > APC_LEVEL || !Adapter->PageTableLevelsValid)
        return;
    ExAcquireFastMutex(&Process->GpuVaLock);
    Table = (PDXGKRNL_GPUVA_PAGE_TABLE)Process->hRootPageTable;
    for (Level = GpuVaLevelCount(Adapter); Level-- > 0 && Table != NULL;)
    {
        ULONG Index = GpuVaPteIndexFor(Adapter, Va, Level);

        if (Index >= Table->EntryCount)
        {
            DXGKRNL_ERR("XLATE va=0x%I64x level=%lu index %lu out of range (%lu)\n", Va, Level, Index, Table->EntryCount);
            break;
        }
        DXGKRNL_ERR("XLATE va=0x%I64x level=%lu table=%p(phys 0x%I64x) idx=%lu ours(valid=%u seg=%u page=0x%I64x flags=0x%I64x) raw=0x%I64x\n",
                    Va, Level, Table->KernelVa, (ULONGLONG)Table->Physical.QuadPart, Index,
                    (UINT)Table->Entries[Index].Valid, (UINT)Table->Entries[Index].Segment,
                    (ULONGLONG)Table->Entries[Index].PageAddress, (ULONGLONG)Table->Entries[Index].Flags,
                    GpuVaRawEntry(Table, Index));
        Table = (Level > 0 && Table->Children != NULL) ? Table->Children[Index] : NULL;
    }
    ExReleaseFastMutex(&Process->GpuVaLock);
}

typedef struct _DXGKP_GPUVA_EVENT
{
    volatile LONG64 Sequence;
    CHAR            Op;
    ULONG           Handle;
    ULONGLONG       Address;
    ULONGLONG       Size;
} DXGKP_GPUVA_EVENT;

static DXGKP_GPUVA_EVENT DxgkpGpuVaEvents[DXGKP_GPUVA_EVENT_RING_SIZE];
static volatile LONG64 DxgkpGpuVaEventNext;
volatile LONG64 DxgkDiagGlobalSequence;

VOID
DxgkGpuVaRecordEvent(
    _In_ CHAR Op,
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Size,
    _In_ ULONG Handle)
{
    LONG64 Slot = InterlockedIncrement64(&DxgkpGpuVaEventNext);
    LONG64 Sequence = DxgkDiagSequence();
    DXGKP_GPUVA_EVENT *Event = &DxgkpGpuVaEvents[(ULONG)(Slot - 1) % DXGKP_GPUVA_EVENT_RING_SIZE];

    Event->Sequence = 0;
    KeMemoryBarrier();
    Event->Op = Op;
    Event->Address = Address;
    Event->Size = Size;
    Event->Handle = Handle;
    KeMemoryBarrier();
    Event->Sequence = Sequence;
}

VOID
DxgkGpuVaDumpRecentEvents(VOID)
{
    LONG64 Next = DxgkpGpuVaEventNext;
    ULONG Index;
    const ULONG DumpCount = min(DXGKP_GPUVA_EVENT_RING_SIZE, 32u);

    /* Serial debug output is synchronous. Dumping the full ring can consume
     * the TDR DDI deadline before ResetFromTimeout is even called. Retain the
     * full ring for the debugger, but print only its most recent entries. */
    DXGKRNL_ERR("TDR last %lu GPU VA operations (oldest first; R=reserve F=free M=map D=destroy W=destroy-wait; #=global seq):\n", DumpCount);
    for (Index = DXGKP_GPUVA_EVENT_RING_SIZE - DumpCount;
         Index < DXGKP_GPUVA_EVENT_RING_SIZE; Index++)
    {
        DXGKP_GPUVA_EVENT Event = DxgkpGpuVaEvents[(ULONG)(Next + Index) % DXGKP_GPUVA_EVENT_RING_SIZE];

        if (Event.Sequence == 0)
            continue;
        DXGKRNL_ERR("TDR   #%I64d %c va=0x%I64x size=0x%I64x alloc=0x%X\n",
                    Event.Sequence, Event.Op, Event.Address, Event.Size, Event.Handle);
    }
}


static BOOLEAN
GpuVaGetDriverReservationGeometry(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ ULONGLONG *LeafEntryCoverage,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *FirstDriverAddress,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *AddressSpaceEnd)
{
    ULONG LeafEntryShift;

    if (Adapter == NULL || LeafEntryCoverage == NULL ||
        FirstDriverAddress == NULL || AddressSpaceEnd == NULL ||
        !Adapter->GpuMmuCapsValid || !Adapter->PageTableLevelsValid ||
        GpuVaLevelCount(Adapter) == 0 ||
        Adapter->GpuMmuCaps.VirtualAddressBitCount == 0 ||
        Adapter->GpuMmuCaps.VirtualAddressBitCount >= 64)
    {
        return FALSE;
    }

    LeafEntryShift = GpuVaLevelShift(Adapter, 0);
    if (LeafEntryShift >= 64)
        return FALSE;

    *LeafEntryCoverage = 1ULL << LeafEntryShift;
    *FirstDriverAddress = GPUVA_START_ADDRESS;
    *AddressSpaceEnd = 1ULL << Adapter->GpuMmuCaps.VirtualAddressBitCount;
    return TRUE;
}

/*
 * Reserve GPU virtual address space on behalf of a miniport while its
 * DxgkDdiCreateProcess callback is active.  These ranges live until process
 * teardown and remain distinguishable from reservations owned by the UMD.
 */
NTSTATUS
DxgkGpuVaReserveDriverRange(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ ULONGLONG SizeInBytes,
    _In_ ULONG Alignment,
    _In_ BOOLEAN AllowUserModeMapping,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress)
{
    PDXGKRNL_GPUVA_RANGE Range;
    D3DGPU_VIRTUAL_ADDRESS ActualAddress;
    D3DGPU_VIRTUAL_ADDRESS FirstDriverAddress;
    D3DGPU_VIRTUAL_ADDRESS AddressSpaceEnd;
    ULONGLONG LeafEntryCoverage;
    ULONGLONG EffectiveAlignment;

    PAGED_CODE();

    /*
     * DXGKARGCB_RESERVEGPUVIRTUALADDRESSRANGE.Alignment carries no documented
     * lower bound: zero means the miniport has no alignment requirement beyond
     * the one the page tables already impose.  Rejecting it made every such
     * request fail STATUS_INVALID_PARAMETER, which took DxgkDdiCreateProcess
     * down with it.  Treat zero as "leaf granularity" and note that computing
     * (Alignment - 1) on zero would underflow the mask below.
     */
    if (Process == NULL || OutAddress == NULL || SizeInBytes == 0 ||
        (Alignment != 0 && (Alignment & (Alignment - 1)) != 0))
    {
        DPRINT1("DxgkGpuVaReserveDriverRange: bad arguments process=%p out=%p "
                "base=0x%I64x size=0x%I64x align=0x%lx\n",
                Process, OutAddress, BaseAddress, SizeInBytes, Alignment);
        return STATUS_INVALID_PARAMETER;
    }
    if (Process->Adapter != Adapter)
    {
        DPRINT1("DxgkGpuVaReserveDriverRange: process %p belongs to adapter %p, "
                "reservation is for adapter %p\n",
                Process, Process->Adapter, Adapter);
        return STATUS_INVALID_PARAMETER;
    }
    if (!GpuVaGetDriverReservationGeometry(Adapter,
                                           &LeafEntryCoverage,
                                           &FirstDriverAddress,
                                           &AddressSpaceEnd))
    {
        DPRINT1("DxgkGpuVaReserveDriverRange: no reservation geometry for adapter %p "
                "(mmucaps=%u pagetablelevels=%u levels=%lu vabits=%u)\n",
                Adapter,
                Adapter != NULL ? Adapter->GpuMmuCapsValid : 0,
                Adapter != NULL ? Adapter->PageTableLevelsValid : 0,
                Adapter != NULL ? GpuVaLevelCount(Adapter) : 0,
                Adapter != NULL ? Adapter->GpuMmuCaps.VirtualAddressBitCount : 0);
        return STATUS_INVALID_PARAMETER;
    }

    *OutAddress = 0;
    EffectiveAlignment = max((ULONGLONG)Alignment, LeafEntryCoverage);

    if ((SizeInBytes & (LeafEntryCoverage - 1)) != 0 ||
        (BaseAddress != 0 &&
         ((BaseAddress & (EffectiveAlignment - 1)) != 0 ||
          BaseAddress < FirstDriverAddress ||
          BaseAddress >= AddressSpaceEnd ||
          SizeInBytes > AddressSpaceEnd - BaseAddress)) ||
        (BaseAddress == 0 && SizeInBytes > AddressSpaceEnd - FirstDriverAddress))
    {
        DPRINT1("DxgkGpuVaReserveDriverRange: rejected base=0x%I64x size=0x%I64x "
                "align=0x%lx effalign=0x%I64x leaf=0x%I64x first=0x%I64x end=0x%I64x\n",
                BaseAddress, SizeInBytes, Alignment, EffectiveAlignment,
                LeafEntryCoverage, FirstDriverAddress, AddressSpaceEnd);
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
        if (GpuVaFindOverlapping(Process, ActualAddress, SizeInBytes) != NULL)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            return STATUS_CONFLICTING_ADDRESSES;
        }
    }
    else
    {
        ActualAddress = GpuVaFindFreeRegion(Process,
                                            FirstDriverAddress,
                                            AddressSpaceEnd,
                                            SizeInBytes,
                                            EffectiveAlignment);
        if (ActualAddress == 0)
        {
            ExReleaseFastMutex(&Process->GpuVaLock);
            GpuVaFreeRange(Range);
            return STATUS_NO_MEMORY;
        }
    }

    Range->GpuVirtualAddress = ActualAddress;
    Range->SizeInBytes = SizeInBytes;
    Range->State = GpuVaStateReserved;
    Range->ReservationBase = ActualAddress;
    Range->ReservationSize = SizeInBytes;
    Range->DriverReserved = TRUE;
    Range->AllowUserModeMapping = AllowUserModeMapping;

    GpuVaInsertRange(Process, Range);
    Process->GpuVaRangeCount++;
    Process->GpuVaTotalReserved += SizeInBytes;
    ExReleaseFastMutex(&Process->GpuVaLock);

    DxgkGpuVaRecordEvent('r', ActualAddress, SizeInBytes, 0);
    *OutAddress = ActualAddress;
    return STATUS_SUCCESS;
}

/*
 * Allocate the per-process root page table before a virtual context is
 * handed to the miniport.  The context itself is required before
 * DxgkDdiSetRootPageTable can associate that allocation with the KMD object,
 * so allocation and association are deliberately separate transactions.
 */
NTSTATUS
DxgkGpuVaPreparePageTable(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ HANDLE MiniportDeviceHandle)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Process == NULL || Process->Adapter != Adapter ||
        MiniportDeviceHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Process->hMiniportProcess == NULL ||
        Adapter->MiniportContext == NULL ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0) ||
        DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiGetRootPageTableSize) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetRootPageTable) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    ExAcquireFastMutex(&Process->GpuVaLock);
    Status = GpuVaEnsureRootPageTable(Process);
    ExReleaseFastMutex(&Process->GpuVaLock);
    if (NT_SUCCESS(Status))
        Status = DxgkGpuVaPlacePendingPageTables(Adapter,
                                                 Process,
                                                 MiniportDeviceHandle);
    if (NT_SUCCESS(Status) && !DxgkGpuVaPageTableReady(Adapter, Process))
        Status = STATUS_INTERNAL_ERROR;
    return Status;
}


/* EOF */
