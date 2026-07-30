/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video Memory Manager (VidMm) implementation
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * This file implements the Video Memory Manager subsystem of dxgkrnl.sys.
 * VidMm is responsible for:
 *
 *   1. Querying the miniport for memory segment descriptors at adapter-start.
 *   2. Tracking all active GPU memory allocations.
 *   3. Placing allocations in the best-fitting memory segment (VRAM or
 *      aperture) using a first-fit allocator.
 *   4. Evicting resident allocations to system memory when VRAM is full.
 *   5. Making system-memory-backed allocations resident again when needed.
 *   6. Providing CPU-visible mappings for allocations flagged CPU_VISIBLE.
 *
 * Architecture notes (amd64/x86):
 *
 *   - x86 has a Total Store Ordering (TSO) memory model: stores are visible
 *     to other processors in program order, and loads are not reordered past
 *     stores.  This means we can read volatile fields (e.g. UsedSize) without
 *     an LFENCE for correctness — however MSVC may still reorder instructions
 *     at the compiler level, so we use InterlockedAdd64 for the UsedSize
 *     counter which is updated from multiple paths.
 *
 *   - FAST_MUTEX (ExAcquireFastMutex / ExReleaseFastMutex) is used for
 *     segment locks because:
 *       a) All segment operations happen at PASSIVE_LEVEL.
 *       b) FAST_MUTEX is cheaper than a kernel mutex (no wait queuing
 *          overhead) and correct for the <=APC_LEVEL constraint.
 *       c) Unlike KSPIN_LOCK it does not raise IRQL to DISPATCH, keeping
 *          latency low for multi-segment allocation sweeps.
 *
 *   - Physical address arithmetic uses PHYSICAL_ADDRESS (LARGE_INTEGER)
 *     correctly: QuadPart is used for 64-bit PA values, which avoids
 *     truncation on systems with >4 GB of GPU VRAM.
 *
 *   - MmMapIoSpace uses WriteCombined caching for VRAM — this matches
 *     Windows behaviour for CPU-visible VRAM segments.  Write-combining
 *     batches store operations into large PCIe write bursts, which is
 *     optimal for framebuffer access patterns.  MmCached would cause
 *     coherency issues; MmNonCached incurs unnecessary performance cost.
 *
 * Pool tags:
 *   'VmxD' — segment array and related allocations  (TAG_VIDMM_SEGMENT)
 *   'AlxD' — per-allocation objects                 (TAG_VIDMM_ALLOC)
 */

/* INCLUDES *******************************************************************/

#include "dxgkrnl_private.h"
#include "handles.h"
#include "residency_core.h"
#include "vidmm.h"
#include "vidmm_worker_drain_core.h"
#include "vidsch.h"
#include "debug.h"
#include <ndk/psfuncs.h>

#define NDEBUG
#include <debug.h>

/* MACROS / CONSTANTS *********************************************************/

/*
 * VIDMM_MAX_SEGMENTS: maximum number of memory segments we support.
 * The WDDM spec allows up to 16 segments (IDs 1..16).  We use this as
 * the sanity cap when validating the miniport's NbSegments return value.
 */
#define VIDMM_MAX_SEGMENTS  16
#define VIDMM_MAX_SEGMENT_DESCRIPTOR_STRIDE 4096

/* Forward declaration — defined later in this file. */
static NTSTATUS DxgkpVidMmReleaseApertureMapping(_In_ PDXGKVMM_ALLOCATION Allocation, _In_ BOOLEAN ForceInvalidate);
static NTSTATUS DxgkpVidMmPrepareAllocationApertureMappingOwned(_In_ PDXGKVMM_ALLOCATION Allocation);
static NTSTATUS DxgkpVidMmFillAperturePagingOperation(_In_ PDXGKVMM_ALLOCATION Allocation, _In_ BOOLEAN Map, _Out_ PDXGKRNL_PAGING_OP Op);
static NTSTATUS DxgkpVidMmLockResidencyForExternalOperation(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmReleaseSegmentPlacement(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmFinalizeAllocation(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmFinalizeResource(_In_ PDXGKVMM_RESOURCE Resource);
static NTSTATUS DxgkpVidMmReferenceInitializingAllocation(_In_ HANDLE Handle, _In_ PDXGKRNL_ADAPTER Adapter, _In_ PDXGKRNL_DEVICE Device, _Out_ PDXGKVMM_ALLOCATION *OutAllocation);
static VOID DxgkpVidMmDropAllocationHandleReference(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmDropLogicalHandleReference(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmNotifyDestroyBatchAllocationDrained(_In_ struct _DXGKVMM_DESTROY_BATCH *Batch);
static VOID NTAPI DxgkpVidMmDestroyBatchWorker(_In_ PVOID Context);
static ULONG DxgkpVidMmForceQuarantinedDestroyBatches(_In_ PDXGKRNL_ADAPTER Adapter);
static ULONG DxgkpVidMmForceLocalAdapterBackings(_In_ PDXGKRNL_ADAPTER Adapter);
static NTSTATUS DxgkpVidMmDestroyAllocation(_In_ PDXGKRNL_ADAPTER Adapter, _In_opt_ PDXGKRNL_DEVICE ExpectedDevice, _In_opt_ PDXGKVMM_RESOURCE ExpectedResource, _In_ HANDLE AllocationHandle);
static NTSTATUS DxgkpVidMmDestroyAllocationList(_In_ PDXGKRNL_ADAPTER Adapter, _In_opt_ PDXGKRNL_DEVICE Device, _In_reads_(AllocationCount) CONST D3DKMT_HANDLE *AllocationHandles, _In_ UINT AllocationCount, _In_ BOOLEAN ResourceOperationLockHeld);
static struct _DXGKVMM_DESTROY_BATCH *DxgkpVidMmAllocateDestroyBatch(_In_ PDXGKRNL_ADAPTER Adapter, _In_ UINT AllocationCount);
static NTSTATUS DxgkpVidMmActivateUnpublishedDestroyBatch(_In_ struct _DXGKVMM_DESTROY_BATCH *Batch, _In_reads_(Batch->AllocationCount) PDXGKVMM_ALLOCATION *Allocations, _In_opt_ PDXGKVMM_RESOURCE Resource, _In_reads_opt_(Batch->AllocationCount) PHANDLE OpenHandles, _In_opt_ struct _DXGKVMM_OPEN_BINDING_GROUP *OpenBindingGroup, _In_ NTSTATUS FailureStatus, _In_ BOOLEAN TrackUnpublishedObjects, _In_ BOOLEAN AwaitingStopBoundary);
static VOID DxgkpVidMmFreeDestroyBatch(_In_ struct _DXGKVMM_DESTROY_BATCH *Batch);
static VOID DxgkpVidMmQuarantineDestroyBatch(_In_ struct _DXGKVMM_DESTROY_BATCH *Batch);
static BOOLEAN DxgkpVidMmTryCommitDestroyBatch(_In_ struct _DXGKVMM_DESTROY_BATCH *Batch);
static NTSTATUS DxgkpVidMmCloseReadyBindingGroup(_In_ PDXGKRNL_ADAPTER Adapter, _In_ struct _DXGKVMM_OPEN_BINDING_GROUP *Group);

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
typedef struct _DXGKVMM_PROCESS_BUDGET
{
    LIST_ENTRY Link;
    PEPROCESS Process;
    PDXGKRNL_ADAPTER Adapter;
    DXGK_RESIDENCY_BUDGET Groups[2];
    ULONG ChargeEntryCount;
    ULONG ActiveTransactions;
    BOOLEAN ProcessExiting;
} DXGKVMM_PROCESS_BUDGET, *PDXGKVMM_PROCESS_BUDGET;

typedef struct _DXGKVMM_PROCESS_BUDGET_CHARGE
{
    LIST_ENTRY Link;
    PDXGKVMM_PROCESS_BUDGET Record;
    D3DKMT_MEMORY_SEGMENT_GROUP Group;
    ULONGLONG Bytes;
    DXGK_RESIDENCY_PROCESS_CHARGE_STATE State;
} DXGKVMM_PROCESS_BUDGET_CHARGE, *PDXGKVMM_PROCESS_BUDGET_CHARGE;

typedef struct _DXGKVMM_PROCESS_BUDGET_GATE
{
    LIST_ENTRY Link;
    PEPROCESS Process;
    DXGK_RESIDENCY_PROCESS_ADMISSION Admission;
} DXGKVMM_PROCESS_BUDGET_GATE, *PDXGKVMM_PROCESS_BUDGET_GATE;

static ULONG
DxgkpVidMmBudgetGroupIndex(
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP Group);

static PDXGKVMM_PROCESS_BUDGET
DxgkpVidMmAcquireProcessBudget(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ NTSTATUS *OutStatus);

static VOID
DxgkpVidMmReleaseProcessBudget(
    _In_ PDXGKVMM_PROCESS_BUDGET Record);

static NTSTATUS
DxgkpVidMmChargeInitialPlacement(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Inout_ PDXGKVMM_ALLOCATION Allocation);

static VOID
DxgkpVidMmReleaseAllAllocationBudgetCharges(
    _Inout_ PDXGKVMM_ALLOCATION Allocation);

static NTSTATUS
DxgkpVidMmReleaseProcessBudgetReferences(
    _Inout_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PEPROCESS Process,
    _In_ ULONG ReferenceCount);

static VOID
DxgkpVidMmReleaseAdapterBudgets(
    _In_ PDXGKRNL_ADAPTER Adapter);
#endif

/*
 * VIDMM_PAGING_BUFFER_SIZE_DEFAULT: fallback when the miniport does not
 * specify a paging buffer size.  64 KB handles Vista-era paging operations.
 */
#define VIDMM_PAGING_BUFFER_SIZE_DEFAULT    (64 * 1024)

/*
 * VIDMM_PRIORITY_NORMAL: WDDM normal allocation priority.
 * Matches D3DDDI_ALLOCATIONPRIORITY_NORMAL = 0x78000000.
 */
#define VIDMM_PRIORITY_NORMAL   0x78000000UL

/*
 * Convenience cast: retrieve the PDXGKRNL_SEGMENT array from an adapter,
 * which is stored as PVOID in the private header.
 */
#define ADAPTER_SEGMENTS(Adapter) \
    ((PDXGKRNL_SEGMENT)(Adapter)->Segments)

static LONG       DxgkVidMmGlobalsState = 0;
static FAST_MUTEX DxgkVidMmAllocationListLock;
static FAST_MUTEX DxgkVidMmResourceListLock;
static FAST_MUTEX DxgkVidMmDestroyBatchListLock;
static FAST_MUTEX DxgkVidMmPolicyLock;
static LIST_ENTRY DxgkVidMmAllocationListHead;
static LIST_ENTRY DxgkVidMmResourceListHead;
static LIST_ENTRY DxgkVidMmDestroyBatchListHead;
static ULONG      DxgkVidMmAllocationHandleCookie = 0x4D4D414C; /* "LAMM" */
static ULONG      DxgkVidMmResourceHandleCookie   = 0x4D4D4552; /* "REMM" */
static ULONG      DxgkVidMmGlobalShareHandleCookie = 0x4D4D4753; /* "SGMM" */
static volatile LONG DxgkVidMmNextAllocationHandle = 0;
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static LIST_ENTRY DxgkVidMmProcessBudgetListHead;
static LIST_ENTRY DxgkVidMmProcessBudgetGateListHead;
static FAST_MUTEX DxgkVidMmProcessBudgetLock;
#endif

static BOOLEAN
DxgkpVidMmRoundUpPageSize(
    _In_ SIZE_T Size,
    _Out_ SIZE_T *RoundedSize)
{
    if (RoundedSize == NULL || Size == 0 || Size > MAXULONG_PTR - (PAGE_SIZE - 1))
        return FALSE;
    *RoundedSize = (Size + PAGE_SIZE - 1) & ~(SIZE_T)(PAGE_SIZE - 1);
    return TRUE;
}
static volatile LONG DxgkVidMmNextResourceHandle   = 0;
static volatile LONG DxgkVidMmNextGlobalShareHandle = 0;

typedef struct _DXGKVMM_OPEN_BINDING_GROUP
{
    volatile LONG ReferenceCount;
    UINT AllocationCount;
    UINT DestroyingCount;
    volatile LONG CloseState;
    NTSTATUS CloseStatus;
    HANDLE MiniportDeviceHandle;
    KMUTEX OperationLock;
    PDXGKVMM_ALLOCATION *Allocations;
    PHANDLE OpenHandles;
    struct _DXGKVMM_DESTROY_BATCH **DestroyBatches;
    UINT DestroyBatchCount;
} DXGKVMM_OPEN_BINDING_GROUP, *PDXGKVMM_OPEN_BINDING_GROUP;

typedef struct _DXGKVMM_DESTROY_BATCH
{
    WORK_QUEUE_ITEM WorkItem;
    KEVENT WorkerIdleEvent;
    KEVENT CompletionEvent;
    LIST_ENTRY QuarantineEntry;
    PDXGKRNL_ADAPTER Adapter;
    PDXGKVMM_RESOURCE Resource;
    HANDLE MiniportDeviceHandle;
    PDXGKVMM_ALLOCATION *Allocations;
    PHANDLE OpenBindingHandles;
    PHANDLE MiniportHandles;
    UINT AllocationCount;
    UINT MiniportHandleCount;
    volatile LONG PendingAllocationCount;
    volatile LONG WorkQueued;
    volatile LONG WorkerCounted;
    volatile LONG Listed;
    volatile LONG LifetimeReferenceCount;
    volatile LONG ActiveReferenceHeld;
    volatile LONG Quarantined;
    volatile LONG AdmissionDeferred;
    volatile LONG KmdAdmissionReserved;
    volatile LONG CompletionWaiter;
    volatile LONG DestroyCommitted;
    volatile LONG ResourcePoisonHeld;
    volatile LONG ResourceHandleReferenceOwned;
    NTSTATUS CompletionStatus;
    HANDLE MiniportResourceHandle;
    BOOLEAN ForceLocalRelease;
    BOOLEAN AwaitingStopBoundary;
    BOOLEAN DestroyResource;
    BOOLEAN DestroyResourceWrapper;
} DXGKVMM_DESTROY_BATCH, *PDXGKVMM_DESTROY_BATCH;

typedef struct _DXGKVMM_ALLOCATION_SNAPSHOT_ENTRY
{
    PDXGKVMM_ALLOCATION Allocation;
    BOOLEAN ReferenceHeld;
} DXGKVMM_ALLOCATION_SNAPSHOT_ENTRY, *PDXGKVMM_ALLOCATION_SNAPSHOT_ENTRY;

typedef struct _DXGKVMM_ALLOCATION_SNAPSHOT
{
    PDXGKVMM_ALLOCATION_SNAPSHOT_ENTRY Entries;
    SIZE_T EntryCount;
    PDXGKVMM_DESTROY_BATCH *Batches;
    SIZE_T BatchCount;
} DXGKVMM_ALLOCATION_SNAPSHOT, *PDXGKVMM_ALLOCATION_SNAPSHOT;

/* HELPERS ********************************************************************/

/* Paging content transfer (defined after the CPU-mapping helpers). */
static NTSTATUS
DxgkpVidMmTransferAllocationContent(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN ToSegment);

/*
 * VidMmSegmentIsAperture
 * Returns TRUE if this is a GPU-visible system memory aperture segment.
 */
FORCEINLINE
BOOLEAN
VidMmSegmentIsAperture(
    _In_ CONST PDXGKRNL_SEGMENT Segment)
{
    return (BOOLEAN)(Segment->Flags.Aperture || Segment->Flags.Agp);
}

/*
 * VidMmSegmentIsCpuVisible
 * Returns TRUE if the segment can be mapped into CPU address space.
 */
FORCEINLINE
BOOLEAN
VidMmSegmentIsCpuVisible(
    _In_ CONST PDXGKRNL_SEGMENT Segment)
{
    return (BOOLEAN)Segment->Flags.CpuVisible;
}

/*
 * VidMmAlignUp
 * Rounds Value up to the next multiple of Alignment (power-of-2).
 */
FORCEINLINE
BOOLEAN
VidMmAlignUp(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment,
    _Out_ ULONGLONG *AlignedValue)
{
    if (AlignedValue == NULL || Alignment == 0 || (Alignment & (Alignment - 1)) != 0 || Value > MAXULONGLONG - (Alignment - 1))
        return FALSE;
    *AlignedValue = (Value + Alignment - 1) & ~(Alignment - 1);
    return TRUE;
}

/* The video-memory owner.  NULL means dxgmms2 has not published the contract
 * for this adapter, in which case there is no legal placement to make. */
FORCEINLINE
PDXGMMS2_VIDMM_INTERFACE_V1
DxgkpVidMmOwner(_In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->Mms2VidMmValid, 0, 0) == 0)
        return NULL;
    return &Adapter->Mms2VidMmInterface;
}

FORCEINLINE
ULONGLONG
VidMmSegmentPlacementLimit(
    _In_ CONST PDXGKRNL_SEGMENT Segment)
{
    if (VidMmSegmentIsAperture(Segment))
        return min(Segment->CommitLimit, Segment->Size);
    return Segment->Size;
}

FORCEINLINE
BOOLEAN
VidMmAllocationSupportsSegment(_In_ CONST PDXGKVMM_ALLOCATION Allocation, _In_ ULONG SegmentId)
{
    return SegmentId >= 1 && SegmentId <= 32 && (Allocation->SupportedWriteSegmentSet & (1UL << (SegmentId - 1))) != 0;
}

FORCEINLINE
ULONGLONG
VidMmAllocationSizeForSegment(_In_ CONST PDXGKVMM_ALLOCATION Allocation, _In_ CONST PDXGKRNL_SEGMENT Segment)
{
    return Segment->Flags.PitchAlignment && Allocation->PitchAlignedSize != 0 ? (ULONGLONG)Allocation->PitchAlignedSize : (ULONGLONG)Allocation->Size;
}

typedef enum _VIDMM_BUDGET_GROUP
{
    VidMmBudgetGroupNone = 0,
    VidMmBudgetGroupLocal,
    VidMmBudgetGroupNonLocal
} VIDMM_BUDGET_GROUP;

FORCEINLINE
VIDMM_BUDGET_GROUP
VidMmSegmentBudgetGroup(_In_ PDXGKRNL_ADAPTER Adapter, _In_ CONST PDXGKRNL_SEGMENT Segment)
{
    if (DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
    {
        if (Segment->Flags.LocalBudgetGroup)
            return VidMmBudgetGroupLocal;
        if (Segment->Flags.NonLocalBudgetGroup)
            return VidMmBudgetGroupNonLocal;
        return VidMmBudgetGroupNone;
    }
    return VidMmSegmentIsAperture(Segment) ? VidMmBudgetGroupNonLocal : VidMmBudgetGroupLocal;
}

static BOOLEAN DxgkpVidMmValidateAllocationSegmentSets(_In_ PDXGKRNL_ADAPTER Adapter, _In_ CONST DXGK_ALLOCATIONINFO *AllocInfo)
{
    ULONG PreferredIds[5];
    ULONG ValidMask;
    ULONG Index;
    BOOLEAN SupportsPitchAlignedSegment = FALSE;

    if (Adapter == NULL || AllocInfo == NULL || Adapter->SegmentCount > 32)
        return FALSE;
    ValidMask = Adapter->SegmentCount == 32 ? MAXULONG : ((1UL << Adapter->SegmentCount) - 1);
    if (((AllocInfo->SupportedWriteSegmentSet | AllocInfo->EvictionSegmentSet) & ~ValidMask) != 0)
        return FALSE;
    for (Index = 0; Index < Adapter->SegmentCount; ++Index)
    {
        PDXGKRNL_SEGMENT Segment = &ADAPTER_SEGMENTS(Adapter)[Index];

        if ((AllocInfo->EvictionSegmentSet & (1UL << Index)) != 0 && (!VidMmSegmentIsAperture(Segment) || Segment->Flags.PitchAlignment))
            return FALSE;
        if ((AllocInfo->SupportedWriteSegmentSet & (1UL << Index)) != 0 && Segment->Flags.PitchAlignment)
            SupportsPitchAlignedSegment = TRUE;
    }
    if (SupportsPitchAlignedSegment != (AllocInfo->PitchAlignedSize != 0) || (AllocInfo->PitchAlignedSize != 0 && AllocInfo->PitchAlignedSize < AllocInfo->Size))
        return FALSE;
    PreferredIds[0] = AllocInfo->PreferredSegment.SegmentId0;
    PreferredIds[1] = AllocInfo->PreferredSegment.SegmentId1;
    PreferredIds[2] = AllocInfo->PreferredSegment.SegmentId2;
    PreferredIds[3] = AllocInfo->PreferredSegment.SegmentId3;
    PreferredIds[4] = AllocInfo->PreferredSegment.SegmentId4;
    for (Index = 0; Index < RTL_NUMBER_OF(PreferredIds); ++Index)
    {
        if (PreferredIds[Index] != 0 && (PreferredIds[Index] > Adapter->SegmentCount || (AllocInfo->SupportedWriteSegmentSet & (1UL << (PreferredIds[Index] - 1))) == 0))
            return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpVidMmEnsureGlobalsInitialized(VOID)
{
    LONG State;

    State = InterlockedCompareExchange(&DxgkVidMmGlobalsState, 1, 0);
    if (State == 0)
    {
        ExInitializeFastMutex(&DxgkVidMmAllocationListLock);
        ExInitializeFastMutex(&DxgkVidMmResourceListLock);
        ExInitializeFastMutex(&DxgkVidMmDestroyBatchListLock);
        ExInitializeFastMutex(&DxgkVidMmPolicyLock);
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        ExInitializeFastMutex(&DxgkVidMmProcessBudgetLock);
#endif
        InitializeListHead(&DxgkVidMmAllocationListHead);
        InitializeListHead(&DxgkVidMmResourceListHead);
        InitializeListHead(&DxgkVidMmDestroyBatchListHead);
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        InitializeListHead(&DxgkVidMmProcessBudgetListHead);
        InitializeListHead(&DxgkVidMmProcessBudgetGateListHead);
#endif
        InterlockedExchange(&DxgkVidMmGlobalsState, 2);
        return;
    }

    while (InterlockedCompareExchange(&DxgkVidMmGlobalsState, 2, 2) != 2)
        YieldProcessor();
}

static BOOLEAN
DxgkpVidMmSnapshotContains(
    _In_ PDXGKVMM_ALLOCATION_SNAPSHOT Snapshot,
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    SIZE_T Index;

    for (Index = 0; Index < Snapshot->EntryCount; ++Index)
    {
        if (Snapshot->Entries[Index].Allocation == Allocation)
            return TRUE;
    }
    return FALSE;
}

static VOID
DxgkpVidMmReleaseAllocationSnapshot(
    _Inout_ PDXGKVMM_ALLOCATION_SNAPSHOT Snapshot)
{
    SIZE_T Index;

    if (Snapshot == NULL)
        return;
    for (Index = 0; Index < Snapshot->EntryCount; ++Index)
    {
        if (Snapshot->Entries[Index].ReferenceHeld)
            DxgkVidMmDereferenceAllocation(Snapshot->Entries[Index].Allocation);
    }
    for (Index = 0; Index < Snapshot->BatchCount; ++Index)
        DxgkpVidMmFreeDestroyBatch(Snapshot->Batches[Index]);
    if (Snapshot->Batches != NULL)
        ExFreePoolWithTag(Snapshot->Batches, TAG_VIDMM_ALLOC);
    if (Snapshot->Entries != NULL)
        ExFreePoolWithTag(Snapshot->Entries, TAG_VIDMM_ALLOC);
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
}

static NTSTATUS
DxgkpVidMmCaptureAdapterAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN IncludeDestroyBatches,
    _Out_ PDXGKVMM_ALLOCATION_SNAPSHOT Snapshot)
{
    PLIST_ENTRY Entry;
    SIZE_T EntryCapacity = 0;
    SIZE_T BatchCapacity = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Adapter == NULL || Snapshot == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    DxgkpVidMmEnsureGlobalsInitialized();

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Entry = DxgkVidMmAllocationListHead.Flink; Entry != &DxgkVidMmAllocationListHead; Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);

        if (Allocation->Adapter == Adapter && Allocation->BackingAllocation == NULL)
            EntryCapacity++;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    if (IncludeDestroyBatches)
    {
        ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
        for (Entry = DxgkVidMmDestroyBatchListHead.Flink; Entry != &DxgkVidMmDestroyBatchListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);
            UINT Index;

            if (Batch->Adapter != Adapter || InterlockedCompareExchange(&Batch->Listed, 0, 0) == 0)
                continue;
            BatchCapacity++;
            for (Index = 0; Index < Batch->AllocationCount; ++Index)
            {
                if (Batch->Allocations[Index] != NULL && Batch->Allocations[Index]->BackingAllocation == NULL)
                    EntryCapacity++;
            }
        }
        ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
    }

    if (EntryCapacity > MAXULONG_PTR / sizeof(*Snapshot->Entries) || BatchCapacity > MAXULONG_PTR / sizeof(*Snapshot->Batches))
        return STATUS_INTEGER_OVERFLOW;
    if (EntryCapacity != 0)
    {
        Snapshot->Entries = ExAllocatePoolWithTag(NonPagedPool, EntryCapacity * sizeof(*Snapshot->Entries), TAG_VIDMM_ALLOC);
        if (Snapshot->Entries == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Snapshot->Entries, EntryCapacity * sizeof(*Snapshot->Entries));
    }
    if (BatchCapacity != 0)
    {
        Snapshot->Batches = ExAllocatePoolWithTag(NonPagedPool, BatchCapacity * sizeof(*Snapshot->Batches), TAG_VIDMM_ALLOC);
        if (Snapshot->Batches == NULL)
        {
            DxgkpVidMmReleaseAllocationSnapshot(Snapshot);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Snapshot->Batches, BatchCapacity * sizeof(*Snapshot->Batches));
    }

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Entry = DxgkVidMmAllocationListHead.Flink; Entry != &DxgkVidMmAllocationListHead; Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);

        if (Allocation->Adapter != Adapter || Allocation->BackingAllocation != NULL || InterlockedCompareExchange(&Allocation->ReferenceCount, 0, 0) <= 0)
            continue;
        if (Snapshot->EntryCount == EntryCapacity)
        {
            Status = STATUS_RETRY;
            break;
        }
        InterlockedIncrement(&Allocation->ReferenceCount);
        Snapshot->Entries[Snapshot->EntryCount].Allocation = Allocation;
        Snapshot->Entries[Snapshot->EntryCount].ReferenceHeld = TRUE;
        Snapshot->EntryCount++;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    if (!NT_SUCCESS(Status))
    {
        DxgkpVidMmReleaseAllocationSnapshot(Snapshot);
        return Status;
    }

    if (IncludeDestroyBatches)
    {
        ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
        for (Entry = DxgkVidMmDestroyBatchListHead.Flink; Entry != &DxgkVidMmDestroyBatchListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);
            UINT Index;

            if (Batch->Adapter != Adapter || InterlockedCompareExchange(&Batch->Listed, 0, 0) == 0)
                continue;
            if (Snapshot->BatchCount == BatchCapacity)
            {
                Status = STATUS_RETRY;
                break;
            }
            InterlockedIncrement(&Batch->LifetimeReferenceCount);
            Snapshot->Batches[Snapshot->BatchCount++] = Batch;
            for (Index = 0; Index < Batch->AllocationCount; ++Index)
            {
                PDXGKVMM_ALLOCATION Allocation = Batch->Allocations[Index];

                if (Allocation == NULL || Allocation->BackingAllocation != NULL || DxgkpVidMmSnapshotContains(Snapshot, Allocation))
                    continue;
                if (Snapshot->EntryCount == EntryCapacity)
                {
                    Status = STATUS_RETRY;
                    break;
                }
                Snapshot->Entries[Snapshot->EntryCount].Allocation = Allocation;
                Snapshot->Entries[Snapshot->EntryCount].ReferenceHeld = FALSE;
                Snapshot->EntryCount++;
            }
            if (!NT_SUCCESS(Status))
                break;
        }
        ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
    }
    if (!NT_SUCCESS(Status))
        DxgkpVidMmReleaseAllocationSnapshot(Snapshot);
    return Status;
}

static VOID
DxgkpVidMmInitializeAllocationLifetime(
    _Inout_ PDXGKVMM_ALLOCATION Allocation)
{
    Allocation->ReferenceCount = 1;
    Allocation->Destroying = 0;
    Allocation->FinalizeQueued = 0;
    Allocation->LogicalReferenceCount = 1;
    Allocation->LogicalHandleReferenceDropped = 0;
    /* Native WDDM 2 allocations begin life resident with one implicit
     * residency reference owned by the creating device; that device's first
     * Evict removes it.  It is tracked as a flag rather than a list entry
     * because the owning device is assigned after this initializer runs. */
    Allocation->ResidencyReferenceCount = 1;
    Allocation->ImplicitResidencyReference = TRUE;
    InitializeListHead(&Allocation->ResidencyReferenceList);
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    InitializeListHead(&Allocation->ResidencyBudgetChargeList);
#endif
    Allocation->SubmissionResidencyPinCount = 0;
    Allocation->ResidencyTransactionOwner = NULL;
    KeInitializeEvent(&Allocation->ReferencesDrainedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Allocation->LogicalReferencesDrainedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Allocation->ResidencyTransactionEvent, NotificationEvent, TRUE);
}

static VOID
DxgkpVidMmInitializeResourceLifetime(
    _Inout_ PDXGKVMM_RESOURCE Resource)
{
    Resource->ReferenceCount = 1;
    Resource->Destroying = 0;
    Resource->FinalizeQueued = 0;
    KeInitializeEvent(&Resource->ReferencesDrainedEvent, NotificationEvent, FALSE);
}

static VOID
NTAPI
DxgkpVidMmFinalizeAllocationWorker(
    _In_ PVOID Context)
{
    DxgkpVidMmFinalizeAllocation((PDXGKVMM_ALLOCATION)Context);
}

static VOID
NTAPI
DxgkpVidMmFinalizeResourceWorker(
    _In_ PVOID Context)
{
    DxgkpVidMmFinalizeResource((PDXGKVMM_RESOURCE)Context);
}

static VOID
DxgkpVidMmScheduleAllocationFinalizer(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    if (InterlockedCompareExchange(&Allocation->FinalizeQueued, 1, 0) != 0)
        return;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        DxgkpVidMmFinalizeAllocation(Allocation);
        return;
    }
    ExInitializeWorkItem(&Allocation->FinalizeWorkItem, DxgkpVidMmFinalizeAllocationWorker, Allocation);
    ExQueueWorkItem(&Allocation->FinalizeWorkItem, DelayedWorkQueue);
}

static VOID
DxgkpVidMmScheduleResourceFinalizer(
    _In_ PDXGKVMM_RESOURCE Resource)
{
    if (InterlockedCompareExchange(&Resource->FinalizeQueued, 1, 0) != 0)
        return;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        DxgkpVidMmFinalizeResource(Resource);
        return;
    }
    ExInitializeWorkItem(&Resource->FinalizeWorkItem, DxgkpVidMmFinalizeResourceWorker, Resource);
    ExQueueWorkItem(&Resource->FinalizeWorkItem, DelayedWorkQueue);
}

static VOID
DxgkpVidMmNotifyDestroyBatchAllocationDrained(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    PDXGKRNL_ADAPTER Adapter = Batch->Adapter;
    LONG PendingAllocations;
    BOOLEAN QueueWorker = FALSE;
    BOOLEAN ResetDrainedEvent = FALSE;
    BOOLEAN RunWorkerInline = FALSE;
    BOOLEAN WorkerAdmitted;
    BOOLEAN KmdAdmissionReserved = FALSE;

    ASSERT(Batch != NULL);
    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    PendingAllocations = InterlockedDecrement(&Batch->PendingAllocationCount);
    ASSERT(PendingAllocations >= 0);
    if (PendingAllocations == 0 && InterlockedCompareExchange(&Batch->WorkQueued, 0, 0) == 0)
    {
        if (Batch->AwaitingStopBoundary)
            InterlockedExchange(&Batch->Quarantined, 1);
        else
        {
            RunWorkerInline = InterlockedCompareExchange(&Batch->CompletionWaiter, 0, 0) != 0 && Adapter->KmdTransactionOwnerThread == PsGetCurrentThread();
            if (!RunWorkerInline)
                KmdAdmissionReserved = DxgkAcquireKmdCall(Adapter);
            WorkerAdmitted = (RunWorkerInline || KmdAdmissionReserved) && DxgkVidMmWorkerDrainCoreTryAdmitLocked(&Adapter->VidMmDestroyWorkerCount, InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0, &ResetDrainedEvent);
            if (WorkerAdmitted)
            {
                InterlockedExchange(&Batch->AdmissionDeferred, 0);
                InterlockedExchange(&Batch->WorkQueued, 1);
                if (ResetDrainedEvent)
                    KeResetEvent(&Adapter->VidMmDestroyWorkersDrainedEvent);
                InterlockedExchange(&Batch->WorkerCounted, 1);
                if (KmdAdmissionReserved)
                    InterlockedExchange(&Batch->KmdAdmissionReserved, 1);
                if (!RunWorkerInline)
                    QueueWorker = TRUE;
            }
            else
            {
                if (KmdAdmissionReserved)
                    DxgkReleaseKmdCall(Adapter);
                if (InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0 || !RunWorkerInline)
                    InterlockedExchange(&Batch->AdmissionDeferred, 1);
                InterlockedExchange(&Batch->Quarantined, 1);
                if (RunWorkerInline)
                {
                    Batch->CompletionStatus = STATUS_DEVICE_NOT_READY;
                    KeSetEvent(&Batch->CompletionEvent, IO_NO_INCREMENT, FALSE);
                    RunWorkerInline = FALSE;
                }
            }
        }
    }
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
    if (RunWorkerInline)
        DxgkpVidMmDestroyBatchWorker(Batch);
    else if (QueueWorker)
        ExQueueWorkItem(&Batch->WorkItem, DelayedWorkQueue);
}

static VOID
DxgkpVidMmTrackBacking(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedIncrement(&Adapter->VidMmBackingCount) == 1)
        KeResetEvent(&Adapter->VidMmBackingsDrainedEvent);
}

static VOID
DxgkpVidMmReleaseBacking(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG Count = InterlockedDecrement(&Adapter->VidMmBackingCount);

    ASSERT(Count >= 0);
    if (Count == 0)
        KeSetEvent(&Adapter->VidMmBackingsDrainedEvent, IO_NO_INCREMENT, FALSE);
}

static VOID
DxgkpVidMmSaturatingAdd(
    _Inout_ UINT64 *Total,
    _In_ UINT64 Value)
{
    *Total = (~(UINT64)0 - *Total < Value) ? ~(UINT64)0 : *Total + Value;
}

static NTSTATUS
DxgkpVidMmQuerySystemMemoryAvailableForGraphics(
    _Out_ UINT64 *AvailableForGraphics)
{
    PPHYSICAL_MEMORY_RANGE Ranges;
    UINT64 TotalSystemMemory = 0;
    UINT64 HalfSystemMemory;
    UINT64 EightyPercentSystemMemory;
    UINT64 AboveSixteenGigabytes;
    ULONG Index;

    if (AvailableForGraphics == NULL)
        return STATUS_INVALID_PARAMETER;
    *AvailableForGraphics = 0;
    Ranges = MmGetPhysicalMemoryRanges();
    if (Ranges == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Ranges[Index].BaseAddress.QuadPart != 0 || Ranges[Index].NumberOfBytes.QuadPart != 0; ++Index)
    {
        if (Ranges[Index].NumberOfBytes.QuadPart < 0)
        {
            ExFreePool(Ranges);
            return STATUS_DATA_ERROR;
        }
        DxgkpVidMmSaturatingAdd(&TotalSystemMemory, (UINT64)Ranges[Index].NumberOfBytes.QuadPart);
    }
    ExFreePool(Ranges);
    HalfSystemMemory = TotalSystemMemory / 2;
    EightyPercentSystemMemory = TotalSystemMemory - TotalSystemMemory / 5;
    AboveSixteenGigabytes = TotalSystemMemory > 16ULL * 1024ULL * 1024ULL * 1024ULL ? TotalSystemMemory - 16ULL * 1024ULL * 1024ULL * 1024ULL : 0;
    *AvailableForGraphics = min(EightyPercentSystemMemory, max(HalfSystemMemory, AboveSixteenGigabytes));
    return STATUS_SUCCESS;
}

static D3DKMT_HANDLE
DxgkpVidMmAllocateHandle(
    _Inout_ volatile LONG *Sequence,
    _In_ ULONG             Cookie)
{
    ULONG Value;

    do
    {
        Value = (ULONG)InterlockedIncrement(Sequence) ^ Cookie;
    } while (Value == 0);

    return (D3DKMT_HANDLE)Value;
}

static PDXGKVMM_OPEN_BINDING_GROUP
DxgkpVidMmCreateOpenBindingGroup(
    _In_ UINT AllocationCount)
{
    PDXGKVMM_OPEN_BINDING_GROUP Group;

    if (AllocationCount == 0)
        return NULL;
    Group = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Group), TAG_VIDMM_ALLOC);
    if (Group == NULL)
        return NULL;
    RtlZeroMemory(Group, sizeof(*Group));
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Group->Allocations) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Group->OpenHandles) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Group->DestroyBatches))
    {
        ExFreePoolWithTag(Group, TAG_VIDMM_ALLOC);
        return NULL;
    }
    Group->Allocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Group->Allocations), TAG_VIDMM_ALLOC);
    Group->OpenHandles = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Group->OpenHandles), TAG_VIDMM_ALLOC);
    Group->DestroyBatches = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Group->DestroyBatches), TAG_VIDMM_ALLOC);
    if (Group->Allocations == NULL || Group->OpenHandles == NULL || Group->DestroyBatches == NULL)
    {
        if (Group->DestroyBatches != NULL)
            ExFreePoolWithTag(Group->DestroyBatches, TAG_VIDMM_ALLOC);
        if (Group->OpenHandles != NULL)
            ExFreePoolWithTag(Group->OpenHandles, TAG_VIDMM_ALLOC);
        if (Group->Allocations != NULL)
            ExFreePoolWithTag(Group->Allocations, TAG_VIDMM_ALLOC);
        ExFreePoolWithTag(Group, TAG_VIDMM_ALLOC);
        return NULL;
    }
    RtlZeroMemory(Group->Allocations, (SIZE_T)AllocationCount * sizeof(*Group->Allocations));
    RtlZeroMemory(Group->OpenHandles, (SIZE_T)AllocationCount * sizeof(*Group->OpenHandles));
    RtlZeroMemory(Group->DestroyBatches, (SIZE_T)AllocationCount * sizeof(*Group->DestroyBatches));
    Group->ReferenceCount = 1;
    Group->AllocationCount = AllocationCount;
    Group->CloseStatus = STATUS_PENDING;
    KeInitializeMutex(&Group->OperationLock, 0);
    return Group;
}

static VOID
DxgkpVidMmReferenceOpenBindingGroup(
    _In_ PDXGKVMM_OPEN_BINDING_GROUP Group)
{
    LONG References;

    ASSERT(Group != NULL);
    References = InterlockedIncrement(&Group->ReferenceCount);
    ASSERT(References > 1);
}

static VOID
DxgkpVidMmDereferenceOpenBindingGroup(
    _In_ PDXGKVMM_OPEN_BINDING_GROUP Group)
{
    LONG References;

    ASSERT(Group != NULL);
    References = InterlockedDecrement(&Group->ReferenceCount);
    ASSERT(References >= 0);
    if (References == 0)
    {
        ExFreePoolWithTag(Group->DestroyBatches, TAG_VIDMM_ALLOC);
        ExFreePoolWithTag(Group->OpenHandles, TAG_VIDMM_ALLOC);
        ExFreePoolWithTag(Group->Allocations, TAG_VIDMM_ALLOC);
        ExFreePoolWithTag(Group, TAG_VIDMM_ALLOC);
    }
}

static NTSTATUS
DxgkpVidMmCreateSystemAllocation(
    _In_      PDXGKRNL_ADAPTER     Adapter,
    _In_opt_  PDXGKRNL_DEVICE      Device,
    _In_      DXGK_ALLOCATIONINFO *AllocInfo,
    _Out_     PHANDLE              OutHandle,
    _Out_     PDXGKVMM_ALLOCATION *OutAllocation)
{
    PDXGKVMM_ALLOCATION Alloc;
    SIZE_T AllocSize;
    SIZE_T PitchAlignedSize = 0;

    if (Adapter == NULL || AllocInfo == NULL || OutHandle == NULL ||
        OutAllocation == NULL ||
        AllocInfo->Size == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutHandle = NULL;
    *OutAllocation = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();

    if (!DxgkpVidMmRoundUpPageSize(AllocInfo->Size, &AllocSize))
        return STATUS_INTEGER_OVERFLOW;
    if (AllocInfo->PitchAlignedSize != 0)
    {
        if (AllocInfo->PitchAlignedSize < AllocInfo->Size)
            return STATUS_INVALID_PARAMETER;
        if (!DxgkpVidMmRoundUpPageSize(AllocInfo->PitchAlignedSize, &PitchAlignedSize))
            return STATUS_INTEGER_OVERFLOW;
    }
    if (AllocInfo->Alignment != 0 && (AllocInfo->Alignment & (AllocInfo->Alignment - 1)) != 0)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->Segments != NULL && !DxgkpVidMmValidateAllocationSegmentSets(Adapter, AllocInfo))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Alloc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Alloc), TAG_VIDMM_ALLOC);
    if (Alloc == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Alloc, sizeof(*Alloc));
    DxgkpVidMmInitializeAllocationLifetime(Alloc);
    KeInitializeMutex(&Alloc->ResidencyLock, 0);

    Alloc->SystemMemory = ExAllocatePoolWithTag(NonPagedPool,
                                                AllocSize,
                                                TAG_VIDMM_ALLOC);
    if (Alloc->SystemMemory == NULL)
    {
        ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Alloc->SystemMemory, AllocSize);

    Alloc->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle,
                                             DxgkVidMmAllocationHandleCookie);
    Alloc->Magic = DXGKVMM_ALLOCATION_MAGIC;
    Alloc->Adapter = Adapter;
    Alloc->Device = Device;
    Alloc->MiniportDeviceHandle = Device != NULL ? Device->hMiniportDevice : NULL;
    Alloc->Initializing = Device != NULL;
    Alloc->Size = AllocSize;
    Alloc->PitchAlignedSize = PitchAlignedSize;
    Alloc->Alignment = AllocInfo->Alignment ? AllocInfo->Alignment : PAGE_SIZE;
    Alloc->SupportedWriteSegmentSet = AllocInfo->SupportedWriteSegmentSet;
    Alloc->EvictionSegmentSet = AllocInfo->EvictionSegmentSet;
    Alloc->PreferredSegment.Value = AllocInfo->PreferredSegment.Value;
    Alloc->AccessedPhysically =
        AllocInfo->FlagsWddm2.AccessedPhysically != 0;
    Alloc->AllocationPriority = AllocInfo->AllocationPriority ?
                                AllocInfo->AllocationPriority :
                                VIDMM_PRIORITY_NORMAL;
    Alloc->CpuVisible = TRUE;
    Alloc->Resident = FALSE;
    Alloc->PhysicalAddress = MmGetPhysicalAddress(Alloc->SystemMemory);
    Alloc->CpuAddress = Alloc->SystemMemory;
    KeInitializeMutex(&Alloc->UserModeLock, 0);

    if (AllocInfo->PrivateDriverDataSize != 0)
    {
        if (AllocInfo->pPrivateDriverData == NULL)
        {
            ExFreePoolWithTag(Alloc->SystemMemory, TAG_VIDMM_ALLOC);
            ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);
            return STATUS_INVALID_PARAMETER;
        }

        Alloc->PrivateDriverData = ExAllocatePoolWithTag(
            NonPagedPool,
            AllocInfo->PrivateDriverDataSize,
            TAG_VIDMM_ALLOC);
        if (Alloc->PrivateDriverData == NULL)
        {
            ExFreePoolWithTag(Alloc->SystemMemory, TAG_VIDMM_ALLOC);
            ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(Alloc->PrivateDriverData,
                      AllocInfo->pPrivateDriverData,
                      AllocInfo->PrivateDriverDataSize);
        Alloc->PrivateDriverDataSize = AllocInfo->PrivateDriverDataSize;
    }

    InitializeListHead(&Alloc->SegmentEntry);
    InitializeListHead(&Alloc->DeviceEntry);
    InitializeListHead(&Alloc->GlobalAllocationEntry);
    InitializeListHead(&Alloc->ResourceEntry);

    DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    InsertTailList(&DxgkVidMmAllocationListHead, &Alloc->GlobalAllocationEntry);
    InterlockedIncrement(&Alloc->ReferenceCount);
    *OutAllocation = Alloc;
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    AllocInfo->hAllocation = NULL;
    *OutHandle = (HANDLE)(ULONG_PTR)Alloc->Handle;
    return STATUS_SUCCESS;
}

/*
 * DxgkpVidMmCreateExistingHeapAllocation
 *
 * D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP backing: the caller's own
 * page-aligned heap pages are probed, locked for write, and given a kernel
 * mapping that becomes the allocation's system backing.  The pages remain
 * caller-owned — DxgkpVidMmReleaseSystemBacking unlocks them, it never
 * pool-frees them.  Kernel-managed: no miniport placement, CPU-visible,
 * GPU access goes through GpuMmu PTEs over the locked pages.
 */
static NTSTATUS
DxgkpVidMmCreateExistingHeapAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE  Device,
    _In_ PVOID            UserSystemMem,
    _In_ SIZE_T           Size,
    _Out_ PHANDLE         OutHandle)
{
    PDXGKVMM_ALLOCATION Alloc;
    PMDL Mdl;
    PVOID SystemVa;

    if (Adapter == NULL || Device == NULL || OutHandle == NULL ||
        UserSystemMem == NULL || Size == 0 || Size > MAXULONG ||
        ((ULONG_PTR)UserSystemMem & (PAGE_SIZE - 1)) != 0 ||
        (Size & (PAGE_SIZE - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutHandle = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();

    Mdl = IoAllocateMdl(UserSystemMem, (ULONG)Size, FALSE, FALSE, NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, UserMode, IoWriteAccess);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        IoFreeMdl(Mdl);
        _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
    }
    _SEH2_END;

    SystemVa = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority | MdlMappingNoExecute);
    if (SystemVa == NULL)
    {
        MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Alloc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Alloc), TAG_VIDMM_ALLOC);
    if (Alloc == NULL)
    {
        MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Alloc, sizeof(*Alloc));
    DxgkpVidMmInitializeAllocationLifetime(Alloc);
    KeInitializeMutex(&Alloc->ResidencyLock, 0);

    Alloc->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle,
                                             DxgkVidMmAllocationHandleCookie);
    Alloc->Magic = DXGKVMM_ALLOCATION_MAGIC;
    Alloc->Adapter = Adapter;
    Alloc->Device = Device;
    Alloc->MiniportDeviceHandle = Device->hMiniportDevice;
    Alloc->Initializing = TRUE;
    Alloc->Size = Size;
    Alloc->Alignment = PAGE_SIZE;
    Alloc->AllocationPriority = VIDMM_PRIORITY_NORMAL;
    Alloc->CpuVisible = TRUE;
    Alloc->Resident = FALSE;
    Alloc->SystemMemory = SystemVa;
    Alloc->SysMemMdl = Mdl;
    Alloc->PhysicalAddress = MmGetPhysicalAddress(SystemVa);
    Alloc->CpuAddress = SystemVa;
    KeInitializeMutex(&Alloc->UserModeLock, 0);

    InitializeListHead(&Alloc->SegmentEntry);
    InitializeListHead(&Alloc->DeviceEntry);
    InitializeListHead(&Alloc->GlobalAllocationEntry);
    InitializeListHead(&Alloc->ResourceEntry);

    DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    InsertTailList(&DxgkVidMmAllocationListHead, &Alloc->GlobalAllocationEntry);
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    *OutHandle = (HANDLE)(ULONG_PTR)Alloc->Handle;
    DPRINT("DxgkpVidMmCreateExistingHeapAllocation: handle=%p user=%p size=%Iu\n",
           *OutHandle, UserSystemMem, Size);
    return STATUS_SUCCESS;
}

static PDXGKRNL_DEVICE
DxgkpVidMmFindDeviceByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;

    if (OutAdapter != NULL)
        *OutAdapter = NULL;
    if (!NT_SUCCESS(DxgkReferenceDeviceByHandle(Handle, PsGetCurrentProcess(), &Adapter, &Device)))
        return NULL;
    if (OutAdapter != NULL)
        *OutAdapter = Adapter;
    return Device;
}

static PDXGKVMM_ALLOCATION
DxgkpVidMmLookupAllocationLocked(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkVidMmAllocationListHead.Flink;
         Entry != &DxgkVidMmAllocationListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Alloc;

        Alloc = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);
        if (Alloc->Handle == Handle && Alloc->Magic == DXGKVMM_ALLOCATION_MAGIC)
            return Alloc;
    }

    return NULL;
}

static PDXGKVMM_RESOURCE
DxgkpVidMmLookupResourceLocked(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkVidMmResourceListHead.Flink;
         Entry != &DxgkVidMmResourceListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_RESOURCE Resource;

        Resource = CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry);
        if (Resource->Handle == Handle)
            return Resource;
    }

    return NULL;
}

static PDXGKVMM_RESOURCE
DxgkpVidMmLookupGlobalShareLocked(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkVidMmResourceListHead.Flink;
         Entry != &DxgkVidMmResourceListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_RESOURCE Resource;

        Resource = CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry);
        if (Resource->Shareable && Resource->GlobalShareHandle == Handle)
            return Resource;
    }

    return NULL;
}

NTSTATUS
DxgkVidMmReferenceAllocation(
    _In_opt_ HANDLE Handle,
    _In_opt_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation)
{
    PDXGKVMM_ALLOCATION Allocation;
    PDXGKVMM_ALLOCATION ReferencedAllocation;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    if (OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocation = NULL;
    if (Handle == NULL)
        return STATUS_INVALID_PARAMETER;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    Allocation = DxgkpVidMmLookupAllocationLocked((D3DKMT_HANDLE)(ULONG_PTR)Handle);
    ReferencedAllocation = Allocation != NULL && Allocation->BackingAllocation != NULL ? Allocation->BackingAllocation : Allocation;
    if (Allocation != NULL && ReferencedAllocation != NULL && !Allocation->Initializing && InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0 && InterlockedCompareExchange(&ReferencedAllocation->ReferenceCount, 0, 0) > 0 && (ExpectedAdapter == NULL || Allocation->Adapter == ExpectedAdapter) && (ExpectedDevice == NULL || Allocation->Device == ExpectedDevice))
    {
        InterlockedIncrement(&ReferencedAllocation->ReferenceCount);
        *OutAllocation = ReferencedAllocation;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    return Status;
}

NTSTATUS
DxgkVidMmReferenceProcessAllocation(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ PDXGKRNL_PROCESS ExpectedProcess,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation)
{
    PDXGKVMM_ALLOCATION Allocation;
    PDXGKVMM_ALLOCATION ReferencedAllocation;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    if (Handle == NULL || ExpectedAdapter == NULL || ExpectedProcess == NULL || ExpectedProcess->Adapter != ExpectedAdapter || OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocation = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    Allocation = DxgkpVidMmLookupAllocationLocked((D3DKMT_HANDLE)(ULONG_PTR)Handle);
    ReferencedAllocation = Allocation != NULL && Allocation->BackingAllocation != NULL ? Allocation->BackingAllocation : Allocation;
    if (Allocation != NULL && ReferencedAllocation != NULL && !Allocation->Initializing && InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0 && Allocation->Adapter == ExpectedAdapter)
    {
        if (Allocation->Device == NULL || Allocation->Device->ProcessRecord != ExpectedProcess)
            Status = STATUS_ACCESS_DENIED;
        else if (InterlockedCompareExchange(&ReferencedAllocation->ReferenceCount, 0, 0) > 0)
        {
            InterlockedIncrement(&ReferencedAllocation->ReferenceCount);
            *OutAllocation = ReferencedAllocation;
            Status = STATUS_SUCCESS;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    return Status;
}

NTSTATUS
DxgkVidMmReferenceOpenBinding(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PHANDLE OutOpenBindingHandle,
    _Out_ PDXGKVMM_ALLOCATION *OutBindingReference)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    if (Handle == NULL || ExpectedAdapter == NULL || ExpectedDevice == NULL || ExpectedDevice->Adapter != ExpectedAdapter || OutOpenBindingHandle == NULL || OutBindingReference == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutOpenBindingHandle = NULL;
    *OutBindingReference = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    Allocation = DxgkpVidMmLookupAllocationLocked((D3DKMT_HANDLE)(ULONG_PTR)Handle);
    if (Allocation != NULL && !Allocation->Initializing && InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0 && Allocation->Adapter == ExpectedAdapter && Allocation->Device == ExpectedDevice)
    {
        if (Allocation->OpenBindingHandle == NULL)
            Status = STATUS_NOT_FOUND;
        else
        {
            InterlockedIncrement(&Allocation->LogicalReferenceCount);
            *OutOpenBindingHandle = Allocation->OpenBindingHandle;
            *OutBindingReference = Allocation;
            Status = STATUS_SUCCESS;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    return Status;
}

NTSTATUS DxgkVidMmCreatePresentBinding(_In_ PDXGKRNL_DEVICE Device, _In_ PDXGKVMM_ALLOCATION BackingAllocation, _In_ BOOLEAN ReadOnly, _Out_ PHANDLE OutOpenBindingHandle, _Out_ PDXGKVMM_ALLOCATION *OutBindingReference)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKVMM_ALLOCATION Binding = NULL;
    PDXGKVMM_OPEN_BINDING_GROUP OpenBindingGroup = NULL;
    PDXGKVMM_DESTROY_BATCH RollbackBatch = NULL;
    PDXGKVMM_ALLOCATION RollbackAllocations[1];
    DXGK_OPENALLOCATIONINFO OpenInfo;
    DXGKARG_OPENALLOCATION OpenArgs;
    HANDLE OpenHandles[1];
    PVOID AllocationPrivateCopy = NULL;
    PVOID ResourcePrivateCopy = NULL;
    UINT ResourcePrivateSize = 0;
    BOOLEAN BackingReferenced = FALSE;
    BOOLEAN KmdTransactionStarted = FALSE;
    BOOLEAN OpenSucceeded = FALSE;
    BOOLEAN Published = FALSE;
    NTSTATUS ActivationStatus;
    NTSTATUS CloseStatus;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Device == NULL || Device->Adapter == NULL || BackingAllocation == NULL || OutOpenBindingHandle == NULL || OutBindingReference == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutOpenBindingHandle = NULL;
    *OutBindingReference = NULL;
    Adapter = Device->Adapter;
    if (BackingAllocation->Adapter != Adapter || DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL)
        return STATUS_NOT_SUPPORTED;

    Binding = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Binding), TAG_VIDMM_ALLOC);
    OpenBindingGroup = DxgkpVidMmCreateOpenBindingGroup(1);
    RollbackBatch = DxgkpVidMmAllocateDestroyBatch(Adapter, 1);
    if (Binding == NULL || OpenBindingGroup == NULL || RollbackBatch == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(Binding, sizeof(*Binding));
    DxgkpVidMmInitializeAllocationLifetime(Binding);
    KeInitializeMutex(&Binding->ResidencyLock, 0);
    KeInitializeMutex(&Binding->UserModeLock, 0);
    InitializeListHead(&Binding->SegmentEntry);
    InitializeListHead(&Binding->DeviceEntry);
    InitializeListHead(&Binding->GlobalAllocationEntry);
    InitializeListHead(&Binding->ResourceEntry);
    Binding->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle, DxgkVidMmAllocationHandleCookie);
    Binding->Magic = DXGKVMM_ALLOCATION_MAGIC;
    Binding->Adapter = Adapter;
    Binding->Device = Device;
    Binding->MiniportDeviceHandle = Device->hMiniportDevice;
    Binding->BackingAllocation = BackingAllocation;
    Binding->Initializing = TRUE;

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    if (InterlockedCompareExchange(&BackingAllocation->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&BackingAllocation->ReferenceCount, 0, 0) <= 0)
        Status = STATUS_DELETE_PENDING;
    else
    {
        InterlockedIncrement(&BackingAllocation->ReferenceCount);
        BackingReferenced = TRUE;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (BackingAllocation->PrivateDriverDataSize != 0)
    {
        if (BackingAllocation->PrivateDriverData == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        AllocationPrivateCopy = ExAllocatePoolWithTag(NonPagedPool, BackingAllocation->PrivateDriverDataSize, TAG_VIDMM_ALLOC);
        if (AllocationPrivateCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(AllocationPrivateCopy, BackingAllocation->PrivateDriverData, BackingAllocation->PrivateDriverDataSize);
    }
    if (BackingAllocation->Resource != NULL)
    {
        ResourcePrivateSize = BackingAllocation->Resource->ResourcePrivateDriverDataSize;
        if (ResourcePrivateSize != 0)
        {
            if (BackingAllocation->Resource->ResourcePrivateDriverData == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
            ResourcePrivateCopy = ExAllocatePoolWithTag(NonPagedPool, ResourcePrivateSize, TAG_VIDMM_ALLOC);
            if (ResourcePrivateCopy == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }
            RtlCopyMemory(ResourcePrivateCopy, BackingAllocation->Resource->ResourcePrivateDriverData, ResourcePrivateSize);
        }
    }

    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    KmdTransactionStarted = TRUE;
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE || Device->hMiniportDevice == NULL)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    InsertTailList(&DxgkVidMmAllocationListHead, &Binding->GlobalAllocationEntry);
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    Published = TRUE;

    RtlZeroMemory(&OpenInfo, sizeof(OpenInfo));
    RtlZeroMemory(&OpenArgs, sizeof(OpenArgs));
    OpenInfo.hAllocation = Binding->Handle;
    OpenInfo.pPrivateDriverData = AllocationPrivateCopy;
    OpenInfo.PrivateDriverDataSize = BackingAllocation->PrivateDriverDataSize;
    OpenArgs.NumAllocations = 1;
    OpenArgs.pOpenAllocation = &OpenInfo;
    OpenArgs.pPrivateDriverData = ResourcePrivateCopy;
    OpenArgs.PrivateDriverSize = ResourcePrivateSize;
    OpenArgs.Flags.ReadOnly = ReadOnly;
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto RollbackPublished;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation)(Device->hMiniportDevice, &OpenArgs);
    DxgkReleaseMiniportCallback(Adapter);
    OpenSucceeded = NT_SUCCESS(Status);
    if (!NT_SUCCESS(Status))
        goto RollbackPublished;
    if (OpenInfo.hDeviceSpecificAllocation == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto RollbackPublished;
    }

    DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    DxgkpVidMmReferenceOpenBindingGroup(OpenBindingGroup);
    Binding->OpenBindingHandle = OpenInfo.hDeviceSpecificAllocation;
    Binding->OpenBindingGroup = OpenBindingGroup;
    Binding->OpenBindingGroupIndex = 0;
    OpenBindingGroup->MiniportDeviceHandle = Device->hMiniportDevice;
    OpenBindingGroup->Allocations[0] = Binding;
    OpenBindingGroup->OpenHandles[0] = OpenInfo.hDeviceSpecificAllocation;
    Binding->Initializing = FALSE;
    InterlockedIncrement(&Binding->LogicalReferenceCount);
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
    OpenBindingGroup = NULL;
    *OutOpenBindingHandle = OpenInfo.hDeviceSpecificAllocation;
    *OutBindingReference = Binding;
    Binding = NULL;
    DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    RollbackBatch = NULL;
    goto Cleanup;

RollbackPublished:
    RollbackAllocations[0] = Binding;
    OpenHandles[0] = OpenInfo.hDeviceSpecificAllocation;
    ActivationStatus = DxgkpVidMmActivateUnpublishedDestroyBatch(RollbackBatch, RollbackAllocations, NULL, OpenHandles, OpenInfo.hDeviceSpecificAllocation != NULL ? OpenBindingGroup : NULL, Status, TRUE, OpenSucceeded && OpenInfo.hDeviceSpecificAllocation == NULL);
    ASSERT(NT_SUCCESS(ActivationStatus));
    if (NT_SUCCESS(ActivationStatus))
    {
        if (OpenInfo.hDeviceSpecificAllocation != NULL)
        {
            CloseStatus = DxgkpVidMmCloseReadyBindingGroup(Adapter, OpenBindingGroup);
            if (NT_SUCCESS(CloseStatus))
                DxgkpVidMmTryCommitDestroyBatch(RollbackBatch);
            else
            {
                RollbackBatch->AwaitingStopBoundary = TRUE;
                DxgkpVidMmQuarantineDestroyBatch(RollbackBatch);
            }
        }
        DxgkpVidMmFreeDestroyBatch(RollbackBatch);
        RollbackBatch = NULL;
        DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
        OpenBindingGroup = NULL;
        Binding = NULL;
        BackingReferenced = FALSE;
    }
    else
        Status = ActivationStatus;

Cleanup:
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    if (ResourcePrivateCopy != NULL)
        ExFreePoolWithTag(ResourcePrivateCopy, TAG_VIDMM_ALLOC);
    if (AllocationPrivateCopy != NULL)
        ExFreePoolWithTag(AllocationPrivateCopy, TAG_VIDMM_ALLOC);
    if (RollbackBatch != NULL)
        DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    if (OpenBindingGroup != NULL)
        DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
    if (Binding != NULL)
    {
        if (Published && !IsListEmpty(&Binding->GlobalAllocationEntry))
        {
            ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
            if (!IsListEmpty(&Binding->GlobalAllocationEntry))
            {
                RemoveEntryList(&Binding->GlobalAllocationEntry);
                InitializeListHead(&Binding->GlobalAllocationEntry);
            }
            ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        }
        if (BackingReferenced)
            DxgkVidMmDereferenceAllocation(BackingAllocation);
        ExFreePoolWithTag(Binding, TAG_VIDMM_ALLOC);
    }
    return Status;
}

NTSTATUS DxgkVidMmDestroyPresentBinding(_In_ PDXGKRNL_DEVICE Device, _In_ PDXGKVMM_ALLOCATION BindingReference)
{
    PDXGKRNL_ADAPTER Adapter;
    HANDLE BindingHandle;

    PAGED_CODE();
    if (Device == NULL || BindingReference == NULL || Device->Adapter == NULL || BindingReference->Device != Device || BindingReference->Adapter != Device->Adapter)
        return STATUS_INVALID_PARAMETER;
    Adapter = Device->Adapter;
    BindingHandle = (HANDLE)(ULONG_PTR)BindingReference->Handle;
    if (BindingHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    DxgkVidMmDereferenceLogicalAllocation(BindingReference);
    return DxgkpVidMmDestroyAllocation(Adapter, Device, NULL, BindingHandle);
}

NTSTATUS
DxgkVidMmAcquireGpuVaBindingReferences(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ PDXGKRNL_PROCESS ExpectedProcess,
    _Out_ PDXGKVMM_ALLOCATION *OutLogicalAllocation,
    _Out_ PDXGKVMM_ALLOCATION *OutBackingAllocation)
{
    PDXGKVMM_ALLOCATION LogicalAllocation;
    PDXGKVMM_ALLOCATION BackingAllocation;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    if (Handle == NULL || ExpectedAdapter == NULL || ExpectedProcess == NULL || ExpectedProcess->Adapter != ExpectedAdapter || OutLogicalAllocation == NULL || OutBackingAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutLogicalAllocation = NULL;
    *OutBackingAllocation = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    LogicalAllocation = DxgkpVidMmLookupAllocationLocked((D3DKMT_HANDLE)(ULONG_PTR)Handle);
    BackingAllocation = LogicalAllocation != NULL && LogicalAllocation->BackingAllocation != NULL ? LogicalAllocation->BackingAllocation : LogicalAllocation;
    if (LogicalAllocation != NULL && BackingAllocation != NULL && !LogicalAllocation->Initializing && InterlockedCompareExchange(&LogicalAllocation->Destroying, 0, 0) == 0 && LogicalAllocation->Adapter == ExpectedAdapter && LogicalAllocation->Device != NULL && LogicalAllocation->Device->ProcessRecord == ExpectedProcess && InterlockedCompareExchange(&LogicalAllocation->LogicalReferenceCount, 0, 0) > 0 && InterlockedCompareExchange(&BackingAllocation->ReferenceCount, 0, 0) > 0)
    {
        InterlockedIncrement(&LogicalAllocation->LogicalReferenceCount);
        InterlockedIncrement(&BackingAllocation->ReferenceCount);
        *OutLogicalAllocation = LogicalAllocation;
        *OutBackingAllocation = BackingAllocation;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    return Status;
}

BOOLEAN
DxgkVidMmDuplicateLogicalReference(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LONG References;

    if (Allocation == NULL)
        return FALSE;
    do
    {
        References = InterlockedCompareExchange(&Allocation->LogicalReferenceCount, 0, 0);
        if (References <= 0)
            return FALSE;
    } while (InterlockedCompareExchange(&Allocation->LogicalReferenceCount, References + 1, References) != References);
    return TRUE;
}

VOID
DxgkVidMmDereferenceLogicalAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LONG References;

    ASSERT(Allocation != NULL);
    References = InterlockedDecrement(&Allocation->LogicalReferenceCount);
    ASSERT(References >= 0);
    if (References == 0)
        KeSetEvent(&Allocation->LogicalReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
}

VOID
DxgkVidMmDereferenceAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LONG References;

    ASSERT(Allocation != NULL);
    References = InterlockedDecrement(&Allocation->ReferenceCount);
    ASSERT(References >= 0);
    if (References == 0)
    {
        if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0 && InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) == 2 && Allocation->DestroyBatch != NULL)
        {
            KeSetEvent(&Allocation->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
            DxgkpVidMmNotifyDestroyBatchAllocationDrained(Allocation->DestroyBatch);
        }
        else
        {
            KeSetEvent(&Allocation->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
            if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
                DxgkpVidMmScheduleAllocationFinalizer(Allocation);
        }
    }
}

BOOLEAN
DxgkVidMmDuplicateAllocationReference(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LONG References;

    if (Allocation == NULL)
        return FALSE;
    for (;;)
    {
        References = InterlockedCompareExchange(&Allocation->ReferenceCount, 0, 0);
        if (References <= 0 || References == MAXLONG)
            return FALSE;
        if (InterlockedCompareExchange(&Allocation->ReferenceCount, References + 1, References) == References)
            return TRUE;
    }
}

NTSTATUS
DxgkVidMmSetAllocationPriorities(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_reads_(AllocationCount) CONST UINT *Priorities,
    _In_ UINT AllocationCount)
{
    UINT Index;

    if (Allocations == NULL || Priorities == NULL || AllocationCount == 0)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (Allocations[Index] == NULL)
            return STATUS_INVALID_PARAMETER;
    }

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    for (Index = 0; Index < AllocationCount; ++Index)
        Allocations[Index]->AllocationPriority = Priorities[Index];
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidMmQueryAllocationPriorities(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ UINT AllocationCount,
    _Out_writes_(AllocationCount) UINT *Priorities)
{
    UINT Index;

    if (Allocations == NULL || Priorities == NULL || AllocationCount == 0)
        return STATUS_INVALID_PARAMETER;
    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (Allocations[Index] == NULL)
        {
            ExReleaseFastMutex(&DxgkVidMmPolicyLock);
            return STATUS_INVALID_PARAMETER;
        }
    }
    for (Index = 0; Index < AllocationCount; ++Index)
        Priorities[Index] = Allocations[Index]->AllocationPriority;
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidMmQueryAllocationResidencyStates(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ UINT AllocationCount,
    _Out_writes_(AllocationCount) D3DKMT_ALLOCATIONRESIDENCYSTATUS *ResidencyStates)
{
    UINT Index;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Allocations == NULL || ResidencyStates == NULL || AllocationCount == 0)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Allocations[Index];
        PDXGKRNL_ADAPTER Adapter;

        if (Allocation == NULL)
            return STATUS_INVALID_PARAMETER;
        Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
        if (!NT_SUCCESS(Status))
            break;
        Adapter = Allocation->Adapter;
        if (!Allocation->Resident)
            ResidencyStates[Index] = D3DKMT_ALLOCATIONRESIDENCYSTATUS_NOTRESIDENT;
        else if (Adapter == NULL || Adapter->Segments == NULL || Allocation->SegmentId == 0 || Allocation->SegmentId > Adapter->SegmentCount)
            Status = STATUS_INVALID_DEVICE_STATE;
        else if (VidMmSegmentIsAperture(&ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1]))
            ResidencyStates[Index] = D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINSHAREDMEMORY;
        else
            ResidencyStates[Index] = D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINGPUMEMORY;
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        if (!NT_SUCCESS(Status))
            break;
    }
    return Status;
}

NTSTATUS
DxgkVidMmReferenceResource(
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN GlobalShareHandle,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PDXGKVMM_RESOURCE *OutResource)
{
    PDXGKVMM_RESOURCE Resource;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    if (OutResource == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutResource = NULL;
    if (Handle == 0)
        return STATUS_INVALID_PARAMETER;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    Resource = GlobalShareHandle ? DxgkpVidMmLookupGlobalShareLocked(Handle) : DxgkpVidMmLookupResourceLocked(Handle);
    if (Resource != NULL && InterlockedCompareExchange(&Resource->Destroying, 0, 0) == 0 && InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) == 0 && InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) == 0 && (ExpectedDevice == NULL || Resource->Device == ExpectedDevice))
    {
        InterlockedIncrement(&Resource->ReferenceCount);
        *OutResource = Resource;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    return Status;
}

VOID
DxgkVidMmDereferenceResource(
    _In_ PDXGKVMM_RESOURCE Resource)
{
    LONG References;

    ASSERT(Resource != NULL);
    References = InterlockedDecrement(&Resource->ReferenceCount);
    ASSERT(References >= 0);
    if (References == 0)
    {
        KeSetEvent(&Resource->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
        if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
            DxgkpVidMmScheduleResourceFinalizer(Resource);
    }
}

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
NTSTATUS
DxgkVidMmCopyResourcePresentPrivateDriverData(
    _In_ D3DKMT_HANDLE ResourceHandle,
    _In_ PEPROCESS ExpectedProcess,
    _Out_writes_bytes_to_opt_(BufferCapacity, *RequiredSize) PVOID Buffer,
    _In_ UINT BufferCapacity,
    _Out_ PUINT RequiredSize)
{
    PDXGKVMM_RESOURCE Resource = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (ResourceHandle == 0 ||
        ExpectedProcess == NULL ||
        RequiredSize == NULL ||
        (BufferCapacity != 0 && Buffer == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *RequiredSize = 0;

    Status = DxgkVidMmReferenceResource(
                 ResourceHandle,
                 FALSE,
                 NULL,
                 &Resource);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_HANDLE;

    Status = KeWaitForSingleObject(
                 &Resource->ResourceOperationLock,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /*
     * A KMT resource handle is process-local.  ReferenceResource validates
     * type and lifetime, while this owner check prevents a handle guessed in
     * another process from becoming a private-data oracle.
     */
    if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 ||
        Resource->Device == NULL ||
        Resource->Device->ProcessRecord == NULL ||
        Resource->Device->ProcessRecord->Process != ExpectedProcess)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else
    {
        *RequiredSize = Resource->ResourcePrivateDriverDataSize;
        if (BufferCapacity < *RequiredSize)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            if (*RequiredSize != 0)
            {
                ASSERT(Resource->ResourcePrivateDriverData != NULL);
                RtlCopyMemory(
                    Buffer,
                    Resource->ResourcePrivateDriverData,
                    *RequiredSize);
            }
            Status = STATUS_SUCCESS;
        }
    }

    KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);

Cleanup:
    DxgkVidMmDereferenceResource(Resource);
    return Status;
}
#endif

NTSTATUS
DxgkVidMmSnapshotResourceAllocations(
    _In_ PDXGKVMM_RESOURCE Resource,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _Outptr_result_buffer_(*OutAllocationCount) PDXGKVMM_ALLOCATION **OutAllocations,
    _Out_ PUINT OutAllocationCount,
    _Out_ PUINT OutTotalPrivateDriverDataSize)
{
    PDXGKVMM_ALLOCATION *Allocations;
    PLIST_ENTRY Entry;
    UINT Capacity;
    UINT Count;
    UINT TotalSize;
    UINT Index;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Resource == NULL || ExpectedAdapter == NULL || OutAllocations == NULL || OutAllocationCount == NULL || OutTotalPrivateDriverDataSize == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocations = NULL;
    *OutAllocationCount = 0;
    *OutTotalPrivateDriverDataSize = 0;

    for (;;)
    {
        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        if (Resource->Adapter != ExpectedAdapter || InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) != 0 || InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) != 0 || Resource->AllocationCount == 0)
        {
            ExReleaseFastMutex(&DxgkVidMmResourceListLock);
            return STATUS_INVALID_PARAMETER;
        }
        Capacity = Resource->AllocationCount;
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if ((SIZE_T)Capacity > MAXULONG_PTR / sizeof(*Allocations))
            return STATUS_INTEGER_OVERFLOW;
        Allocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Capacity * sizeof(*Allocations), TAG_VIDMM_RESOURCE);
        if (Allocations == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Count = 0;
        TotalSize = 0;
        Status = STATUS_SUCCESS;
        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        if (Resource->AllocationCount != Capacity || InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) != 0 || InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) != 0)
            Status = STATUS_RETRY;
        else
        {
            for (Entry = Resource->AllocationList.Flink; Entry != &Resource->AllocationList; Entry = Entry->Flink)
            {
                PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, ResourceEntry);
                PDXGKVMM_ALLOCATION ReferencedAllocation = Allocation->BackingAllocation != NULL ? Allocation->BackingAllocation : Allocation;

                if (Count >= Capacity || Allocation->Adapter != ExpectedAdapter || Allocation->Resource != Resource || Allocation->Initializing || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0 || ReferencedAllocation == NULL || ReferencedAllocation->Adapter != ExpectedAdapter || InterlockedCompareExchange(&ReferencedAllocation->ReferenceCount, 0, 0) <= 0)
                {
                    Status = STATUS_RETRY;
                    break;
                }
                if (TotalSize > MAXULONG - ReferencedAllocation->PrivateDriverDataSize)
                {
                    Status = STATUS_INTEGER_OVERFLOW;
                    break;
                }
                InterlockedIncrement(&ReferencedAllocation->ReferenceCount);
                Allocations[Count++] = ReferencedAllocation;
                TotalSize += ReferencedAllocation->PrivateDriverDataSize;
            }
            if (NT_SUCCESS(Status) && Count != Capacity)
                Status = STATUS_RETRY;
        }
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (NT_SUCCESS(Status))
        {
            *OutAllocations = Allocations;
            *OutAllocationCount = Count;
            *OutTotalPrivateDriverDataSize = TotalSize;
            return STATUS_SUCCESS;
        }
        for (Index = 0; Index < Count; ++Index)
            DxgkVidMmDereferenceAllocation(Allocations[Index]);
        ExFreePoolWithTag(Allocations, TAG_VIDMM_RESOURCE);
        if (Status != STATUS_RETRY)
            return Status;
    }
}

VOID
DxgkVidMmReleaseAllocationSnapshot(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION *Allocations,
    _In_ UINT AllocationCount)
{
    UINT Index;

    if (Allocations == NULL)
        return;
    for (Index = 0; Index < AllocationCount; ++Index)
        DxgkVidMmDereferenceAllocation(Allocations[Index]);
    ExFreePoolWithTag(Allocations, TAG_VIDMM_RESOURCE);
}

NTSTATUS
DxgkVidMmCreateOpenResource(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ PDXGKVMM_RESOURCE BackingResource,
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *BackingAllocations,
    _In_ UINT AllocationCount,
    _In_reads_bytes_opt_(ResourcePrivateDriverDataSize) PVOID ResourcePrivateDriverData,
    _In_ UINT ResourcePrivateDriverDataSize,
    _Inout_updates_bytes_opt_(TotalPrivateDriverDataSize) PVOID TotalPrivateDriverData,
    _In_ UINT TotalPrivateDriverDataSize,
    _Out_ PDXGKVMM_RESOURCE *OutResource,
    _Out_writes_(AllocationCount) PHANDLE OutAllocationHandles)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_OPEN_BINDING_GROUP OpenBindingGroup = NULL;
    PDXGKVMM_DESTROY_BATCH RollbackBatch = NULL;
    DXGK_OPENALLOCATIONINFO *OpenInfo = NULL;
    DXGKARG_OPENALLOCATION OpenArgs;
    DXGKARG_CLOSEALLOCATION CloseArgs;
    UINT Offset = 0;
    UINT Index;
    UINT CloseCount = 0;
    BOOLEAN AllocationsPublished = FALSE;
    BOOLEAN CleanupTransferred = FALSE;
    BOOLEAN OpenSucceeded = FALSE;
    BOOLEAN KmdTransactionStarted = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Device == NULL || Device->Adapter == NULL || BackingResource == NULL || BackingAllocations == NULL || AllocationCount == 0 || OutResource == NULL || OutAllocationHandles == NULL)
        return STATUS_INVALID_PARAMETER;
    Adapter = Device->Adapter;
    if (BackingResource->Adapter != Adapter || BackingResource->BackingResource != NULL || DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL)
        return STATUS_NOT_SUPPORTED;
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*OpenInfo) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Resource->OpenAllocations) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Resource->OpenBindingScratch))
        return STATUS_INTEGER_OVERFLOW;
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DEVICE_NOT_READY;
    KmdTransactionStarted = TRUE;
    if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    *OutResource = NULL;
    for (Index = 0; Index < AllocationCount; ++Index)
        OutAllocationHandles[Index] = NULL;

    Resource = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Resource), TAG_VIDMM_RESOURCE);
    OpenInfo = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*OpenInfo), TAG_VIDMM_RESOURCE);
    if (Resource == NULL || OpenInfo == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(Resource, sizeof(*Resource));
    RtlZeroMemory(OpenInfo, (SIZE_T)AllocationCount * sizeof(*OpenInfo));
    DxgkpVidMmInitializeResourceLifetime(Resource);
    KeInitializeMutex(&Resource->MiniportResourceLock, 0);
    KeInitializeMutex(&Resource->ResourceOperationLock, 0);
    Resource->Device = Device;
    Resource->Adapter = Adapter;
    Resource->BackingResource = BackingResource;
    Resource->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextResourceHandle, DxgkVidMmResourceHandleCookie);
    Resource->OpenAllocationCapacity = AllocationCount;
    Resource->OpenAllocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Resource->OpenAllocations), TAG_VIDMM_RESOURCE);
    Resource->OpenBindingScratch = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Resource->OpenBindingScratch), TAG_VIDMM_RESOURCE);
    OpenBindingGroup = DxgkpVidMmCreateOpenBindingGroup(AllocationCount);
    if (Resource->OpenAllocations == NULL || Resource->OpenBindingScratch == NULL || OpenBindingGroup == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(Resource->OpenAllocations, (SIZE_T)AllocationCount * sizeof(*Resource->OpenAllocations));
    RtlZeroMemory(Resource->OpenBindingScratch, (SIZE_T)AllocationCount * sizeof(*Resource->OpenBindingScratch));
    InitializeListHead(&Resource->AllocationList);
    InitializeListHead(&Resource->GlobalResourceEntry);
    InterlockedIncrement(&BackingResource->ReferenceCount);

    for (Index = 0; Index < AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation;
        PDXGKVMM_ALLOCATION BackingAllocation = BackingAllocations[Index];

        if (BackingAllocation == NULL || BackingAllocation->Adapter != Adapter || BackingAllocation->BackingAllocation != NULL || Offset > TotalPrivateDriverDataSize || BackingAllocation->PrivateDriverDataSize > TotalPrivateDriverDataSize - Offset)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Allocation = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Allocation), TAG_VIDMM_ALLOC);
        if (Allocation == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlZeroMemory(Allocation, sizeof(*Allocation));
        DxgkpVidMmInitializeAllocationLifetime(Allocation);
        KeInitializeMutex(&Allocation->ResidencyLock, 0);
        Allocation->Magic = DXGKVMM_ALLOCATION_MAGIC;
        Allocation->Adapter = Adapter;
        Allocation->Device = Device;
        Allocation->MiniportDeviceHandle = Device->hMiniportDevice;
        Allocation->BackingAllocation = BackingAllocation;
        Allocation->Resource = Resource;
        Allocation->OpenBindingIndex = Index;
        Allocation->Initializing = TRUE;
        Allocation->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle, DxgkVidMmAllocationHandleCookie);
        KeInitializeMutex(&Allocation->UserModeLock, 0);
        InitializeListHead(&Allocation->SegmentEntry);
        InitializeListHead(&Allocation->DeviceEntry);
        InitializeListHead(&Allocation->GlobalAllocationEntry);
        InitializeListHead(&Allocation->ResourceEntry);
        InterlockedIncrement(&BackingAllocation->ReferenceCount);
        InterlockedIncrement(&Resource->ReferenceCount);
        InsertTailList(&Resource->AllocationList, &Allocation->ResourceEntry);
        Resource->AllocationCount++;
        Resource->OpenAllocations[Index] = Allocation;
        OpenInfo[Index].hAllocation = Allocation->Handle;
        OpenInfo[Index].pPrivateDriverData = BackingAllocation->PrivateDriverDataSize != 0 ? (PVOID)((PUCHAR)TotalPrivateDriverData + Offset) : NULL;
        OpenInfo[Index].PrivateDriverDataSize = BackingAllocation->PrivateDriverDataSize;
        Offset += BackingAllocation->PrivateDriverDataSize;
    }
    if (Offset != TotalPrivateDriverDataSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    RollbackBatch = DxgkpVidMmAllocateDestroyBatch(Adapter, AllocationCount);
    if (RollbackBatch == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
        InsertTailList(&DxgkVidMmAllocationListHead, &Resource->OpenAllocations[Index]->GlobalAllocationEntry);
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    AllocationsPublished = TRUE;

    RtlZeroMemory(&OpenArgs, sizeof(OpenArgs));
    OpenArgs.NumAllocations = AllocationCount;
    OpenArgs.pOpenAllocation = OpenInfo;
    OpenArgs.pPrivateDriverData = ResourcePrivateDriverData;
    OpenArgs.PrivateDriverSize = ResourcePrivateDriverDataSize;
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation)(Device->hMiniportDevice, &OpenArgs);
    DxgkReleaseMiniportCallback(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    OpenSucceeded = TRUE;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (OpenInfo[Index].hDeviceSpecificAllocation == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Resource->OpenBindingScratch[CloseCount++] = OpenInfo[Index].hDeviceSpecificAllocation;
    }
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    OpenBindingGroup->MiniportDeviceHandle = Device->hMiniportDevice;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        DxgkpVidMmReferenceOpenBindingGroup(OpenBindingGroup);
        Resource->OpenAllocations[Index]->OpenBindingHandle = OpenInfo[Index].hDeviceSpecificAllocation;
        Resource->OpenAllocations[Index]->OpenBindingGroupIndex = Index;
        Resource->OpenAllocations[Index]->OpenBindingGroup = OpenBindingGroup;
        OpenBindingGroup->Allocations[Index] = Resource->OpenAllocations[Index];
        OpenBindingGroup->OpenHandles[Index] = OpenInfo[Index].hDeviceSpecificAllocation;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
    OpenBindingGroup = NULL;

    DxgkpVidMmTrackBacking(Adapter);
    for (Index = 0; Index < AllocationCount; ++Index)
        DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
        Resource->OpenAllocations[Index]->Initializing = FALSE;
    InsertTailList(&DxgkVidMmResourceListHead, &Resource->GlobalResourceEntry);
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
        OutAllocationHandles[Index] = (HANDLE)(ULONG_PTR)Resource->OpenAllocations[Index]->Handle;
    *OutResource = Resource;
    DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    RollbackBatch = NULL;
    ExFreePoolWithTag(OpenInfo, TAG_VIDMM_RESOURCE);
    DxgkEndKmdTransaction(Adapter);
    return STATUS_SUCCESS;

Cleanup:
    if (AllocationsPublished && Resource != NULL && Resource->OpenAllocations != NULL && Resource->OpenBindingScratch != NULL && OpenInfo != NULL && RollbackBatch != NULL)
    {
        CloseCount = 0;
        RtlZeroMemory(Resource->OpenBindingScratch, (SIZE_T)AllocationCount * sizeof(*Resource->OpenBindingScratch));
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            Resource->OpenBindingScratch[Index] = OpenInfo[Index].hDeviceSpecificAllocation;
            if (Resource->OpenBindingScratch[Index] != NULL)
                CloseCount++;
        }
        if (CloseCount != 0 || OpenSucceeded)
        {
            NTSTATUS CloseStatus;
            BOOLEAN AwaitStopBoundary = OpenSucceeded && CloseCount == 0;

            InterlockedExchange(&RollbackBatch->CompletionWaiter, AwaitStopBoundary ? 0 : 1);
            CloseStatus = DxgkpVidMmActivateUnpublishedDestroyBatch(RollbackBatch, Resource->OpenAllocations, Resource, Resource->OpenBindingScratch, CloseCount != 0 ? OpenBindingGroup : NULL, Status, TRUE, AwaitStopBoundary);
            ASSERT(NT_SUCCESS(CloseStatus));
            if (NT_SUCCESS(CloseStatus))
            {
                if (CloseCount != 0)
                    CloseStatus = DxgkpVidMmCloseReadyBindingGroup(Adapter, OpenBindingGroup);
                if (CloseStatus == STATUS_SUCCESS && !AwaitStopBoundary)
                {
                    DxgkpVidMmTryCommitDestroyBatch(RollbackBatch);
                    KeWaitForSingleObject(&RollbackBatch->CompletionEvent, Executive, KernelMode, FALSE, NULL);
                    InterlockedExchange(&RollbackBatch->CompletionWaiter, 0);
                }
                else if (CloseStatus != STATUS_SUCCESS)
                {
                    RollbackBatch->AwaitingStopBoundary = TRUE;
                    InterlockedExchange(&RollbackBatch->CompletionWaiter, 0);
                    DxgkpVidMmQuarantineDestroyBatch(RollbackBatch);
                }
                DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
                OpenBindingGroup = NULL;
                DxgkpVidMmFreeDestroyBatch(RollbackBatch);
                RollbackBatch = NULL;
                CleanupTransferred = TRUE;
                Resource = NULL;
            }
        }
    }
    if (OpenBindingGroup != NULL)
        DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
    if (!CleanupTransferred && AllocationsPublished && Resource != NULL && Resource->OpenAllocations != NULL)
    {
        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Resource->OpenAllocations[Index];

            if (Allocation != NULL && !IsListEmpty(&Allocation->GlobalAllocationEntry))
            {
                InterlockedExchange(&Allocation->Destroying, 1);
                InterlockedExchange(&Allocation->FinalizeQueued, 2);
                RemoveEntryList(&Allocation->GlobalAllocationEntry);
                InitializeListHead(&Allocation->GlobalAllocationEntry);
            }
        }
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Resource->OpenAllocations[Index];

            if (Allocation != NULL && InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) == 2)
            {
                DxgkpVidMmDropLogicalHandleReference(Allocation);
                KeWaitForSingleObject(&Allocation->LogicalReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
                DxgkpVidMmDropAllocationHandleReference(Allocation);
                KeWaitForSingleObject(&Allocation->ReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
            }
        }
    }
    if (!CleanupTransferred && Resource != NULL && OpenInfo != NULL)
    {
        CloseCount = 0;
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            if (OpenInfo[Index].hDeviceSpecificAllocation != NULL && Resource->OpenBindingScratch != NULL)
                Resource->OpenBindingScratch[CloseCount++] = OpenInfo[Index].hDeviceSpecificAllocation;
        }
        if (CloseCount != 0 && Resource->OpenBindingScratch != NULL && DxgkAcquireMiniportCallback(Adapter))
        {
            RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
            CloseArgs.NumAllocations = CloseCount;
            CloseArgs.pOpenHandleList = Resource->OpenBindingScratch;
            DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(Device->hMiniportDevice, &CloseArgs);
            DxgkReleaseMiniportCallback(Adapter);
        }
    }
    if (!CleanupTransferred && Resource != NULL && Resource->OpenAllocations != NULL)
    {
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Resource->OpenAllocations[Index];

            if (Allocation == NULL)
                continue;
            if (Allocation->BackingAllocation != NULL)
                DxgkVidMmDereferenceAllocation(Allocation->BackingAllocation);
            ExFreePoolWithTag(Allocation, TAG_VIDMM_ALLOC);
        }
    }
    if (Resource != NULL && Resource->BackingResource != NULL)
        DxgkVidMmDereferenceResource(Resource->BackingResource);
    if (Resource != NULL && Resource->OpenBindingScratch != NULL)
        ExFreePoolWithTag(Resource->OpenBindingScratch, TAG_VIDMM_RESOURCE);
    if (Resource != NULL && Resource->OpenAllocations != NULL)
        ExFreePoolWithTag(Resource->OpenAllocations, TAG_VIDMM_RESOURCE);
    if (OpenInfo != NULL)
        ExFreePoolWithTag(OpenInfo, TAG_VIDMM_RESOURCE);
    if (Resource != NULL)
        ExFreePoolWithTag(Resource, TAG_VIDMM_RESOURCE);
    if (RollbackBatch != NULL)
        DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    return Status;
}

PVOID
DxgkVidMmGetHandleData(
    _In_ DXGK_HANDLE_TYPE Type,
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN DeviceSpecific)
{
    PVOID MiniportHandle = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();
    switch (Type)
    {
        case DXGK_HANDLE_ALLOCATION:
        {
            PDXGKVMM_ALLOCATION Allocation;

            ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
            Allocation = DxgkpVidMmLookupAllocationLocked(Handle);
            if (Allocation != NULL && InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0)
            {
                if (DeviceSpecific)
                    MiniportHandle = Allocation->OpenBindingHandle;
                else if (Allocation->BackingAllocation != NULL)
                    MiniportHandle = Allocation->BackingAllocation->MiniportHandle;
                else
                    MiniportHandle = Allocation->MiniportHandle;
            }
            ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
            return MiniportHandle;
        }

        case DXGK_HANDLE_RESOURCE:
        {
            PDXGKVMM_RESOURCE Resource;

            if (DeviceSpecific)
                return NULL;
            ExAcquireFastMutex(&DxgkVidMmResourceListLock);
            Resource = DxgkpVidMmLookupResourceLocked(Handle);
            if (Resource != NULL && InterlockedCompareExchange(&Resource->Destroying, 0, 0) == 0 && InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) == 0 && InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) == 0)
                MiniportHandle = Resource->BackingResource != NULL ? Resource->BackingResource->MiniportHandle : Resource->MiniportHandle;
            ExReleaseFastMutex(&DxgkVidMmResourceListLock);
            return MiniportHandle;
        }

        default:
            DPRINT1("DxgkVidMmGetHandleData: unsupported type %u handle 0x%08X\n",
                    Type,
                    Handle);
            return NULL;
    }
}

PVOID
DxgkVidMmAcquireHandleData(
    _In_ DXGK_HANDLE_TYPE Type,
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN DeviceSpecific,
    _Out_ PDXGKARG_RELEASE_HANDLE ReleaseHandle)
{
    PVOID MiniportHandle = NULL;

    if (ReleaseHandle == NULL)
        return NULL;
    *ReleaseHandle = NULL;
    DxgkpVidMmEnsureGlobalsInitialized();
    switch (Type)
    {
        case DXGK_HANDLE_ALLOCATION:
        {
            PDXGKVMM_ALLOCATION Allocation;
            PDXGKVMM_ALLOCATION BackingAllocation;

            ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
            Allocation = DxgkpVidMmLookupAllocationLocked(Handle);
            if (Allocation != NULL && InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0)
            {
                BackingAllocation = Allocation->BackingAllocation != NULL ? Allocation->BackingAllocation : Allocation;
                MiniportHandle = DeviceSpecific ? Allocation->OpenBindingHandle : BackingAllocation->MiniportHandle;
                if (MiniportHandle != NULL && InterlockedCompareExchange(&Allocation->LogicalReferenceCount, 0, 0) > 0)
                {
                    InterlockedIncrement(&Allocation->LogicalReferenceCount);
                    *ReleaseHandle = Allocation;
                }
                else
                {
                    MiniportHandle = NULL;
                }
            }
            ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
            return MiniportHandle;
        }

        case DXGK_HANDLE_RESOURCE:
        {
            PDXGKVMM_RESOURCE Resource;
            PDXGKVMM_RESOURCE ReferencedResource;

            if (DeviceSpecific)
                return NULL;
            ExAcquireFastMutex(&DxgkVidMmResourceListLock);
            Resource = DxgkpVidMmLookupResourceLocked(Handle);
            if (Resource != NULL && InterlockedCompareExchange(&Resource->Destroying, 0, 0) == 0)
            {
                ReferencedResource = Resource->BackingResource != NULL ? Resource->BackingResource : Resource;
                MiniportHandle = ReferencedResource->MiniportHandle;
                if (MiniportHandle != NULL && InterlockedCompareExchange(&ReferencedResource->ReferenceCount, 0, 0) > 0)
                {
                    InterlockedIncrement(&ReferencedResource->ReferenceCount);
                    *ReleaseHandle = ReferencedResource;
                }
                else
                {
                    MiniportHandle = NULL;
                }
            }
            ExReleaseFastMutex(&DxgkVidMmResourceListLock);
            return MiniportHandle;
        }

        default:
            DPRINT1("DxgkVidMmAcquireHandleData: unsupported type %u handle 0x%08X\n", Type, Handle);
            return NULL;
    }
}

VOID
DxgkVidMmReleaseHandleData(
    _In_ DXGK_HANDLE_TYPE Type,
    _In_ DXGKARG_RELEASE_HANDLE ReleaseHandle)
{
    if (ReleaseHandle == NULL)
        return;

    switch (Type)
    {
        case DXGK_HANDLE_ALLOCATION:
            DxgkVidMmDereferenceLogicalAllocation((PDXGKVMM_ALLOCATION)ReleaseHandle);
            break;

        case DXGK_HANDLE_RESOURCE:
            DxgkVidMmDereferenceResource((PDXGKVMM_RESOURCE)ReleaseHandle);
            break;

        default:
            DPRINT1("DxgkVidMmReleaseHandleData: unsupported type %u\n", Type);
            break;
    }
}

D3DKMT_HANDLE
DxgkVidMmGetHandleParent(
    _In_ D3DKMT_HANDLE AllocationHandle)
{
    PDXGKVMM_ALLOCATION Allocation;
    D3DKMT_HANDLE ParentHandle = 0;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    Allocation = DxgkpVidMmLookupAllocationLocked(AllocationHandle);
    if (Allocation != NULL &&
        InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0 &&
        Allocation->Resource != NULL &&
        InterlockedCompareExchange(&Allocation->Resource->Destroying, 0, 0) == 0)
    {
        ParentHandle = Allocation->Resource->Handle;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    return ParentHandle;
}

D3DKMT_HANDLE
DxgkVidMmEnumHandleChildren(
    _In_ D3DKMT_HANDLE ResourceHandle,
    _In_ UINT Index)
{
    PLIST_ENTRY Entry;
    PDXGKVMM_RESOURCE Resource = NULL;
    D3DKMT_HANDLE ChildHandle = 0;
    UINT CurrentIndex = 0;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);

    for (Entry = DxgkVidMmAllocationListHead.Flink;
         Entry != &DxgkVidMmAllocationListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Allocation =
            CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);

        if (Allocation->Resource != NULL &&
            Allocation->Resource->Handle == ResourceHandle &&
            InterlockedCompareExchange(&Allocation->Resource->Destroying, 0, 0) == 0)
        {
            Resource = Allocation->Resource;
            break;
        }
    }

    if (Resource != NULL)
    {
        for (Entry = Resource->AllocationList.Flink;
             Entry != &Resource->AllocationList;
             Entry = Entry->Flink)
        {
            PDXGKVMM_ALLOCATION Allocation =
                CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, ResourceEntry);

            if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
                continue;
            if (CurrentIndex++ == Index)
            {
                ChildHandle = Allocation->Handle;
                break;
            }
        }
    }

    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    return ChildHandle;
}

NTSTATUS
DxgkVidMmGetCaptureAddress(
    INOUT_PDXGKARGCB_GETCAPTUREADDRESS GetCaptureAddress)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    if (GetCaptureAddress == NULL || GetCaptureAddress->hAllocation == 0)
        return STATUS_INVALID_PARAMETER;

    GetCaptureAddress->SegmentId = 0;
    GetCaptureAddress->PhysicalAddress.QuadPart = 0;

    Status = DxgkVidMmReferenceAllocation(
        (HANDLE)(ULONG_PTR)GetCaptureAddress->hAllocation,
        NULL,
        NULL,
        &Allocation);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    (VOID)KeWaitForSingleObject(
        &Allocation->ResidencyLock,
        Executive,
        KernelMode,
        FALSE,
        NULL);
    if (!Allocation->Resident || Allocation->PendingPlacement)
    {
        Status = STATUS_GRAPHICS_ALLOCATION_BUSY;
    }
    else
    {
        GetCaptureAddress->SegmentId = Allocation->SegmentId;
        GetCaptureAddress->PhysicalAddress = Allocation->PhysicalAddress;
        Status = STATUS_SUCCESS;
    }
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    DxgkVidMmDereferenceAllocation(Allocation);

    return Status;
}

PDXGKVMM_RESOURCE
DxgkVidMmCreateResourceWrapper(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ HANDLE          MiniportHandle,
    _In_ D3DKMT_HANDLE       GlobalShareHandle,
    _In_ BOOLEAN              Shareable,
    _In_reads_bytes_opt_(PrivateRuntimeDataSize) CONST VOID *PrivateRuntimeData,
    _In_ UINT PrivateRuntimeDataSize,
    _In_reads_bytes_opt_(ResourcePrivateDriverDataSize)
            CONST VOID      *ResourcePrivateDriverData,
    _In_    UINT             ResourcePrivateDriverDataSize)
{
    PDXGKVMM_RESOURCE Resource;

    if (Adapter == NULL || (Device != NULL && Device->Adapter != Adapter) || (PrivateRuntimeDataSize != 0 && PrivateRuntimeData == NULL) || (ResourcePrivateDriverDataSize != 0 && ResourcePrivateDriverData == NULL))
        return NULL;
    DxgkpVidMmEnsureGlobalsInitialized();

    Resource = (PDXGKVMM_RESOURCE)ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGKVMM_RESOURCE), TAG_VIDMM_RESOURCE);
    if (Resource == NULL)
        return NULL;

    RtlZeroMemory(Resource, sizeof(*Resource));
    DxgkpVidMmInitializeResourceLifetime(Resource);
    KeInitializeMutex(&Resource->MiniportResourceLock, 0);
    KeInitializeMutex(&Resource->ResourceOperationLock, 0);
    Resource->Device         = Device;
    Resource->Adapter        = Adapter;
    Resource->MiniportHandle = MiniportHandle;
    Resource->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextResourceHandle, DxgkVidMmResourceHandleCookie);
    Resource->Shareable = Shareable;
    if (Shareable)
        Resource->GlobalShareHandle = GlobalShareHandle ? GlobalShareHandle : DxgkpVidMmAllocateHandle(&DxgkVidMmNextGlobalShareHandle, DxgkVidMmGlobalShareHandleCookie);
    if (PrivateRuntimeDataSize != 0)
    {
        Resource->PrivateRuntimeData = ExAllocatePoolWithTag(NonPagedPool, PrivateRuntimeDataSize, TAG_VIDMM_RESOURCE);
        if (Resource->PrivateRuntimeData == NULL)
            goto Fail;
        RtlCopyMemory(Resource->PrivateRuntimeData, PrivateRuntimeData, PrivateRuntimeDataSize);
        Resource->PrivateRuntimeDataSize = PrivateRuntimeDataSize;
    }
    if (ResourcePrivateDriverDataSize != 0)
    {
        ASSERT(ResourcePrivateDriverData != NULL);

        Resource->ResourcePrivateDriverData = ExAllocatePoolWithTag(NonPagedPool, ResourcePrivateDriverDataSize, TAG_VIDMM_RESOURCE);
        if (Resource->ResourcePrivateDriverData == NULL)
        {
            goto Fail;
        }

        RtlCopyMemory(Resource->ResourcePrivateDriverData, ResourcePrivateDriverData, ResourcePrivateDriverDataSize);
        Resource->ResourcePrivateDriverDataSize = ResourcePrivateDriverDataSize;
    }
    InitializeListHead(&Resource->AllocationList);
    InitializeListHead(&Resource->GlobalResourceEntry);

    DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    InsertTailList(&DxgkVidMmResourceListHead, &Resource->GlobalResourceEntry);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);

    return Resource;

Fail:
    if (Resource->ResourcePrivateDriverData != NULL)
        ExFreePoolWithTag(Resource->ResourcePrivateDriverData, TAG_VIDMM_RESOURCE);
    if (Resource->PrivateRuntimeData != NULL)
        ExFreePoolWithTag(Resource->PrivateRuntimeData, TAG_VIDMM_RESOURCE);
    ExFreePoolWithTag(Resource, TAG_VIDMM_RESOURCE);
    return NULL;
}

NTSTATUS
DxgkVidMmAttachAllocationToResource(
    _In_ PDXGKVMM_RESOURCE Resource,
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    if (Resource->Adapter == NULL || Allocation->Adapter != Resource->Adapter || Allocation->Resource != NULL || InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
        Status = STATUS_INVALID_PARAMETER;
    else
    {
        InterlockedIncrement(&Resource->ReferenceCount);
        Allocation->Resource = Resource;
        InsertTailList(&Resource->AllocationList, &Allocation->ResourceEntry);
        Resource->AllocationCount++;
    }
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    return Status;
}

static VOID
DxgkpVidMmDropAllocationHandleReference(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LONG References;

    if (InterlockedCompareExchange(&Allocation->HandleReferenceDropped, 1, 0) != 0)
        return;
    References = InterlockedDecrement(&Allocation->ReferenceCount);
    ASSERT(References >= 0);
    if (References == 0)
    {
        if (InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) == 2 && Allocation->DestroyBatch != NULL)
        {
            KeSetEvent(&Allocation->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
            DxgkpVidMmNotifyDestroyBatchAllocationDrained(Allocation->DestroyBatch);
        }
        else
        {
            KeSetEvent(&Allocation->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
            DxgkpVidMmScheduleAllocationFinalizer(Allocation);
        }
    }
}

static VOID
DxgkpVidMmDropLogicalHandleReference(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LONG References;

    if (InterlockedCompareExchange(&Allocation->LogicalHandleReferenceDropped, 1, 0) != 0)
        return;
    References = InterlockedDecrement(&Allocation->LogicalReferenceCount);
    ASSERT(References >= 0);
    if (References == 0)
        KeSetEvent(&Allocation->LogicalReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
}


/*
 * DxgkpVidMmReleaseSystemBacking
 *
 * Releases the system backing by kind: existing-heap backing unlocks and
 * frees the caller-page MDL (never pool-frees caller memory); pool-owned
 * backing is pool-freed.
 */
static VOID
DxgkpVidMmReleaseSystemBacking(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation->SysMemMdl != NULL)
    {
        MmUnlockPages(Allocation->SysMemMdl);
        IoFreeMdl(Allocation->SysMemMdl);
        Allocation->SysMemMdl = NULL;
        Allocation->SystemMemory = NULL;
        return;
    }
    if (Allocation->SystemMemory != NULL)
    {
        ExFreePoolWithTag(Allocation->SystemMemory, TAG_VIDMM_ALLOC);
        Allocation->SystemMemory = NULL;
    }
}

static VOID
DxgkpVidMmDrainResidencyReferences(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return;
    while (!IsListEmpty(&Allocation->ResidencyReferenceList))
    {
        PDXGKVMM_RESIDENCY_REF Ref;

        Entry = RemoveHeadList(&Allocation->ResidencyReferenceList);
        Ref = CONTAINING_RECORD(Entry, DXGKVMM_RESIDENCY_REF, Entry);
        ExFreePoolWithTag(Ref, TAG_VIDMM_ALLOC);
    }
    Allocation->ImplicitResidencyReference = FALSE;
    InterlockedExchange(&Allocation->ResidencyReferenceCount, 0);
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /*
     * Finalization is the last-resort close path for unpublished/quarantined
     * physical backings.  Device cleanup normally releases each process
     * charge with its exact reference count first; draining the charge list
     * here guarantees that no process ledger survives a backing whose
     * residency-reference storage is being destroyed.
     */
    DxgkpVidMmReleaseAllAllocationBudgetCharges(Allocation);
#endif
}

static VOID
DxgkpVidMmFinalizeAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter = Allocation->Adapter;
    PDXGKVMM_RESOURCE Resource = Allocation->Resource;

    PAGED_CODE();
    ASSERT(InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0);
    ASSERT(InterlockedCompareExchange(&Allocation->ReferenceCount, 0, 0) == 0);
    ASSERT(InterlockedCompareExchange(&Allocation->LogicalReferenceCount, 0, 0) == 0);
    ASSERT(!DxgkSubmissionResidencyPinIsHeld(&Allocation->SubmissionResidencyPinCount));
    ASSERT(InterlockedCompareExchangePointer(
               &Allocation->ResidencyTransactionOwner,
               NULL,
               NULL) == NULL);

    /*
     * Destroy is legal even if user mode did not balance every MakeResident
     * with Evict.  Residency entries contain raw device identities, so neither
     * their storage nor those identities may survive allocation finalization.
     */
    DxgkpVidMmDrainResidencyReferences(Allocation);

    if (Allocation->BackingAllocation != NULL)
    {
        ASSERT(Allocation->OpenBindingHandle == NULL);
        DxgkVidMmDereferenceAllocation(Allocation->BackingAllocation);
        Allocation->BackingAllocation = NULL;
    }
    else
    {
        if (Allocation->ApertureMdl != NULL)
        {
            IoFreeMdl(Allocation->ApertureMdl);
            Allocation->ApertureMdl = NULL;
        }
        Allocation->ApertureMapped = FALSE;
        if (Allocation->UserModeAddress != NULL)
            DxgkVidMmUnmapAllocationUser(Allocation);
        if (Allocation->CpuAddress != NULL)
            DxgkVidMmUnmapAllocationCpu(Allocation);
        if (Allocation->Resident)
            DxgkpVidMmReleaseSegmentPlacement(Allocation);
        DxgkpVidMmReleaseSystemBacking(Allocation);
        if (Allocation->PrivateDriverData != NULL)
            ExFreePoolWithTag(Allocation->PrivateDriverData, TAG_VIDMM_ALLOC);
        Allocation->PrivateDriverData = NULL;
        Allocation->PrivateDriverDataSize = 0;
        ASSERT(Allocation->MiniportHandle == NULL || Adapter == NULL || InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0);
    }

    if (Allocation->OpenBindingGroup != NULL)
    {
        PDXGKVMM_OPEN_BINDING_GROUP Group = Allocation->OpenBindingGroup;

        ASSERT(Allocation->OpenBindingHandle == NULL);
        (VOID)KeWaitForSingleObject(&Group->OperationLock, Executive, KernelMode, FALSE, NULL);
        if (Allocation->OpenBindingGroupIndex < Group->AllocationCount && Group->Allocations[Allocation->OpenBindingGroupIndex] == Allocation)
            Group->Allocations[Allocation->OpenBindingGroupIndex] = NULL;
        KeReleaseMutex(&Group->OperationLock, FALSE);
        DxgkpVidMmDereferenceOpenBindingGroup(Group);
        Allocation->OpenBindingGroup = NULL;
    }

    if (Resource != NULL)
    {
        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        if (!IsListEmpty(&Allocation->ResourceEntry))
        {
            RemoveEntryList(&Allocation->ResourceEntry);
            InitializeListHead(&Allocation->ResourceEntry);
        }
        if (Resource->OpenAllocations != NULL && Allocation->OpenBindingIndex < Resource->OpenAllocationCapacity && Resource->OpenAllocations[Allocation->OpenBindingIndex] == Allocation)
            Resource->OpenAllocations[Allocation->OpenBindingIndex] = NULL;
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        Allocation->Resource = NULL;
        DxgkVidMmDereferenceResource(Resource);
    }

    Allocation->Magic = 0;
    DxgkpVidMmReleaseBacking(Adapter);
    ExFreePoolWithTag(Allocation, TAG_VIDMM_ALLOC);
}

static VOID
DxgkpVidMmFinalizeResource(
    _In_ PDXGKVMM_RESOURCE Resource)
{
    PDXGKRNL_ADAPTER Adapter = Resource->Adapter;

    PAGED_CODE();
    ASSERT(InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0);
    ASSERT(InterlockedCompareExchange(&Resource->ReferenceCount, 0, 0) == 0);
    ASSERT(InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) == 0);
    ASSERT(InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) == 0);
    ASSERT(IsListEmpty(&Resource->AllocationList));

    if (Resource->BackingResource != NULL)
    {
        DxgkVidMmDereferenceResource(Resource->BackingResource);
        Resource->BackingResource = NULL;
    }
    else
        ASSERT(Resource->MiniportHandle == NULL || Adapter == NULL || InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0);
    if (Resource->OpenBindingScratch != NULL)
        ExFreePoolWithTag(Resource->OpenBindingScratch, TAG_VIDMM_RESOURCE);
    if (Resource->OpenAllocations != NULL)
        ExFreePoolWithTag(Resource->OpenAllocations, TAG_VIDMM_RESOURCE);
    if (Resource->ResourcePrivateDriverData != NULL)
        ExFreePoolWithTag(Resource->ResourcePrivateDriverData, TAG_VIDMM_RESOURCE);
    if (Resource->PrivateRuntimeData != NULL)
        ExFreePoolWithTag(Resource->PrivateRuntimeData, TAG_VIDMM_RESOURCE);
    DxgkpVidMmReleaseBacking(Adapter);
    ExFreePoolWithTag(Resource, TAG_VIDMM_RESOURCE);
}


NTSTATUS
DxgkpVidMmDestroyResourceWrapper(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_RESOURCE Resource)
{
    PLIST_ENTRY Entry;
    DXGKARG_DESTROYALLOCATION DestroyArgs;
    BOOLEAN ResourcePinned = FALSE;
    BOOLEAN ResourceOperationLockHeld = FALSE;
    BOOLEAN ResourceHandleDelegated = FALSE;
    BOOLEAN KmdTransactionStarted = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Adapter == NULL || Resource == NULL || Resource->Adapter != Adapter)
        return STATUS_INVALID_PARAMETER;
    if (!Adapter->MiniportDeviceStopped && InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) != 0)
    {
        if (!DxgkBeginKmdTransaction(Adapter))
            return STATUS_DEVICE_NOT_READY;
        KmdTransactionStarted = TRUE;
    }
    DxgkpVidMmEnsureGlobalsInitialized();

    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
        Status = STATUS_INVALID_PARAMETER;
    else
    {
        for (Entry = DxgkVidMmResourceListHead.Flink; Entry != &DxgkVidMmResourceListHead && CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry) != Resource; Entry = Entry->Flink)
            NOTHING;
        if (Entry == &DxgkVidMmResourceListHead)
            Status = STATUS_INVALID_PARAMETER;
        else
        {
            InterlockedIncrement(&Resource->ReferenceCount);
            ResourcePinned = TRUE;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    if (!NT_SUCCESS(Status))
    {
        if (KmdTransactionStarted)
            DxgkEndKmdTransaction(Adapter);
        return Status;
    }

    (VOID)KeWaitForSingleObject(&Resource->ResourceOperationLock, Executive, KernelMode, FALSE, NULL);
    ResourceOperationLockHeld = TRUE;
    if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) != 0 || (InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) != 0 && InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) == 0))
    {
        Status = STATUS_DEVICE_BUSY;
        goto Cleanup;
    }

    for (;;)
    {
        D3DKMT_HANDLE *AllocationHandles = NULL;
        SIZE_T AllocationHandleBytes;
        UINT AllocationCount;
        UINT CapturedCount = 0;

        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
            Status = STATUS_INVALID_PARAMETER;
        else
            Status = STATUS_SUCCESS;
        AllocationCount = NT_SUCCESS(Status) ? Resource->AllocationCount : 0;
        if (NT_SUCCESS(Status) && AllocationCount == 0 && !IsListEmpty(&Resource->AllocationList))
            Status = STATUS_DEVICE_BUSY;
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (AllocationCount == 0)
            break;
        if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*AllocationHandles))
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Cleanup;
        }
        AllocationHandleBytes = (SIZE_T)AllocationCount * sizeof(*AllocationHandles);
        AllocationHandles = ExAllocatePoolWithTag(NonPagedPool, AllocationHandleBytes, TAG_VIDMM_RESOURCE);
        if (AllocationHandles == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || Resource->AllocationCount > AllocationCount)
            Status = STATUS_RETRY;
        else
        {
            Status = STATUS_SUCCESS;
            for (Entry = Resource->AllocationList.Flink; Entry != &Resource->AllocationList; Entry = Entry->Flink)
            {
                PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, ResourceEntry);

                if (CapturedCount >= AllocationCount || Allocation->Adapter != Adapter || Allocation->Resource != Resource || Allocation->Initializing || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0 || Allocation->DestroyBatch != NULL || IsListEmpty(&Allocation->GlobalAllocationEntry))
                {
                    Status = STATUS_DEVICE_BUSY;
                    break;
                }
                AllocationHandles[CapturedCount++] = Allocation->Handle;
            }
            if (NT_SUCCESS(Status) && CapturedCount != Resource->AllocationCount)
                Status = STATUS_RETRY;
        }
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (Status == STATUS_RETRY)
        {
            ExFreePoolWithTag(AllocationHandles, TAG_VIDMM_RESOURCE);
            continue;
        }
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(AllocationHandles, TAG_VIDMM_RESOURCE);
            goto Cleanup;
        }
        Status = DxgkpVidMmDestroyAllocationList(Adapter, NULL, AllocationHandles, CapturedCount, TRUE);
        ExFreePoolWithTag(AllocationHandles, TAG_VIDMM_RESOURCE);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
        {
            ResourceHandleDelegated = TRUE;
            goto ResourceTombstoned;
        }
    }

    (VOID)KeWaitForSingleObject(&Resource->MiniportResourceLock, Executive, KernelMode, FALSE, NULL);
    if (Resource->BackingResource == NULL && Resource->MiniportHandle != NULL)
    {
        if (InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0)
        {
            if (Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
                Status = STATUS_SUCCESS;
            else
                Status = STATUS_DEVICE_NOT_READY;
        }
        else if (DXGK_CB_FULL(Adapter, DxgkDdiDestroyAllocation) == NULL)
            Status = STATUS_NOT_SUPPORTED;
        else if (!DxgkAcquireMiniportCallback(Adapter))
            Status = STATUS_DEVICE_NOT_READY;
        else
        {
            RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
            DestroyArgs.hResource = Resource->MiniportHandle;
            DestroyArgs.Flags.DestroyResource = 1;
            Status = DXGK_CB_FULL(Adapter, DxgkDdiDestroyAllocation)(Adapter->MiniportDeviceContext, &DestroyArgs);
            DxgkReleaseMiniportCallback(Adapter);
        }
        if (!NT_SUCCESS(Status))
        {
            InterlockedExchange(&Resource->DestroyFailureUncertain, 1);
            KeReleaseMutex(&Resource->MiniportResourceLock, FALSE);
            goto Cleanup;
        }
        Resource->MiniportHandle = NULL;
    }
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || Resource->AllocationCount != 0 || !IsListEmpty(&Resource->AllocationList) || IsListEmpty(&Resource->GlobalResourceEntry))
        Status = STATUS_DEVICE_BUSY;
    else
    {
        ASSERT(InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) == 0);
        InterlockedExchange(&Resource->DestroyFailureUncertain, 0);
        InterlockedExchange(&Resource->Destroying, 1);
        RemoveEntryList(&Resource->GlobalResourceEntry);
        InitializeListHead(&Resource->GlobalResourceEntry);
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    KeReleaseMutex(&Resource->MiniportResourceLock, FALSE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

ResourceTombstoned:
    KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
    ResourceOperationLockHeld = FALSE;
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    DxgkVidMmDereferenceResource(Resource);
    ResourcePinned = FALSE;
    if (!ResourceHandleDelegated)
        DxgkVidMmDereferenceResource(Resource);
    return STATUS_SUCCESS;

Cleanup:
    if (ResourceOperationLockHeld)
        KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    if (ResourcePinned)
        DxgkVidMmDereferenceResource(Resource);
    return Status;
}


/* SEGMENT INITIALISATION *****************************************************/

/*
 * DxgkVidMmInitializeAdapter
 *
 * Queries the miniport for segment descriptors and builds the in-memory
 * segment table stored at Adapter->Segments (typed PVOID, cast to
 * PDXGKRNL_SEGMENT by all vidmm.c callers).
 *
 * Called once per adapter from DxgkAdapterStart.
 *
 * Algorithm:
 *   1. Issue DXGKQAITYPE_QUERYSEGMENT with NbSegments=0 to discover count.
 *   2. Allocate descriptor array.
 *   3. Re-issue with NbSegments=N to fill descriptors.
 *   4. Allocate DXGKRNL_SEGMENT[N] array.
 *   5. Populate each DXGKRNL_SEGMENT from the descriptor.
 *   6. Publish to Adapter->Segments / Adapter->SegmentCount.
 */
NTSTATUS
DxgkVidMmInitializeAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    NTSTATUS                        Status;
    DXGKARG_QUERYADAPTERINFO        QueryInfo;
    DXGK_QUERYSEGMENTOUT            SegOut;
    DXGK_QUERYSEGMENTOUT3           SegOut3;
    DXGK_QUERYSEGMENTOUT4           SegOut4;
    DXGK_QUERYSEGMENTIN             QueryIn;
    DXGK_QUERYSEGMENTIN4            QueryIn4;
    PUCHAR                          DescBuffer;
    SIZE_T                          DescBufferSize;
    SIZE_T                          DescStride;
    SIZE_T                          Seg4Stride = sizeof(DXGK_SEGMENTDESCRIPTOR4);
    BOOLEAN                         UsingSeg3;
    BOOLEAN                         UsingSeg4;
    ULONG                           PagingBufferSegmentId;
    ULONG                           PagingBufferSize;
    PDXGKRNL_SEGMENT                Segments;
    PHYSICAL_ADDRESS                LowestAddress;
    PHYSICAL_ADDRESS                HighestAddress;
    PHYSICAL_ADDRESS                BoundaryAddress;
    ULONG                           SegmentCount;
    ULONG                           i;

    DPRINT("DxgkVidMmInitializeAdapter: Adapter %p\n", Adapter);

    ASSERT(Adapter != NULL);
    ASSERT(Adapter->MiniportDeviceContext != NULL);
    ASSERT(Adapter->MiniportContext != NULL);
    InterlockedExchange(&Adapter->VidMmBackingCount, 0);
    KeInitializeEvent(&Adapter->VidMmBackingsDrainedEvent, NotificationEvent, TRUE);
    InterlockedExchange(&Adapter->VidMmDestroyWorkerCount, 0);
    InterlockedExchange(&Adapter->VidMmDestroyQueuesBlocked, 0);
    KeInitializeEvent(&Adapter->VidMmDestroyWorkersDrainedEvent, NotificationEvent, TRUE);

    /*
     * Display-Only Drivers (DOD) do not implement DxgkDdiQueryAdapterInfo
     * for DXGKQAITYPE_QUERYSEGMENT — they have no GPU memory segments, so
     * skip VidMm initialization for them.  The reliable indicator is
     * IsDisplayOnlyDriver (set when the miniport registers through
     * DxgkInitializeDisplayOnlyDriver).  The "DxgkDdiCreateAllocation == NULL"
     * heuristic is NOT sufficient: the smaller KMDDOD_INITIALIZATION_DATA
     * overlaps that slot, so a genuine DOD (e.g. the Red Hat virtio-gpu
     * display-only driver viogpudo.sys) can present a non-NULL value there
     * and would otherwise fall through to a QUERYSEGMENT it can't service
     * (STATUS_NOT_SUPPORTED -> adapter start fails 0xC00000BB).
     */
    DPRINT("DxgkVidMmInitializeAdapter: DxgkDdiCreateAllocation=%p DOD=%d\n",
           DXGK_CB_FULL(Adapter, DxgkDdiCreateAllocation),
           (int)Adapter->MiniportContext->IsDisplayOnlyDriver);

    if (Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiCreateAllocation) == NULL)
    {
        DPRINT("DxgkVidMmInitializeAdapter: DOD driver — skipping segment query\n");
        return STATUS_SUCCESS;
    }

    /* -----------------------------------------------------------------------
     * Step 1: Query segment count (NbSegment discovery pass).
     *
     * WDDM 2.0+ drivers implement QUERYSEGMENT4.  Windows 8/WDDM 1.2+
     * drivers below WDDM 2.0 implement QUERYSEGMENT3 (with the distinct
     * DXGK_QUERYSEGMENTOUT3/DXGK_SEGMENTDESCRIPTOR3 layouts); older drivers
     * implement the original QUERYSEGMENT.  Legacy and v3 receive the
     * zeroed AGP aperture input because the AGP/GART service is unavailable;
     * pSegmentDescriptor == NULL tells the miniport to write only NbSegment.
     * ----------------------------------------------------------------------- */
    UsingSeg4 = DxgkCapsCoreInterfaceVersionAtLeast(
        Adapter->MiniportContext->InitData.s.Version,
        DXGK_CAPS_CORE_LEVEL_WDDM_2_0);
    UsingSeg3 = !UsingSeg4 &&
                DxgkCapsCoreInterfaceVersionAtLeast(
                    Adapter->MiniportContext->InitData.s.Version,
                    DXGK_CAPS_CORE_LEVEL_WDDM_1_2);

    RtlZeroMemory(&SegOut, sizeof(SegOut));
    RtlZeroMemory(&SegOut3, sizeof(SegOut3));
    RtlZeroMemory(&SegOut4, sizeof(SegOut4));
    RtlZeroMemory(&QueryIn, sizeof(QueryIn));
    RtlZeroMemory(&QueryIn4, sizeof(QueryIn4));
    SegOut4.SegmentDescriptorStride = Seg4Stride;

    for (;;)
    {
        RtlZeroMemory(&QueryInfo, sizeof(QueryInfo));
        if (UsingSeg4)
        {
            QueryInfo.Type           = DXGKQAITYPE_QUERYSEGMENT4;
            QueryInfo.pInputData     = &QueryIn4;
            QueryInfo.InputDataSize  = sizeof(QueryIn4);
            QueryInfo.pOutputData    = &SegOut4;
            QueryInfo.OutputDataSize = sizeof(SegOut4);
        }
        else if (UsingSeg3)
        {
            QueryInfo.Type           = DXGKQAITYPE_QUERYSEGMENT3;
            QueryInfo.pInputData     = &QueryIn;
            QueryInfo.InputDataSize  = sizeof(QueryIn);
            QueryInfo.pOutputData    = &SegOut3;
            QueryInfo.OutputDataSize = sizeof(SegOut3);
        }
        else
        {
            QueryInfo.Type           = DXGKQAITYPE_QUERYSEGMENT;
            QueryInfo.pInputData     = &QueryIn;
            QueryInfo.InputDataSize  = sizeof(QueryIn);
            QueryInfo.pOutputData    = &SegOut;
            QueryInfo.OutputDataSize = sizeof(SegOut);
        }

        DPRINT("DxgkVidMmInitializeAdapter: querying segments type=%d "
               "version=0x%X\n",
               QueryInfo.Type, Adapter->MiniportContext->InitData.s.Version);

        Status = Adapter->MiniportContext->InitData.s.DxgkDdiQueryAdapterInfo(
                     Adapter->MiniportDeviceContext,
                     &QueryInfo);
        if (NT_SUCCESS(Status))
            break;

        /* Fall back through the query flavours: 4 -> 3 -> legacy. */
        if (UsingSeg4)
        {
            UsingSeg4 = FALSE;
            UsingSeg3 = TRUE;
            continue;
        }
        if (UsingSeg3)
        {
            UsingSeg3 = FALSE;
            continue;
        }

        if (Status == STATUS_NOT_SUPPORTED)
        {
            DPRINT("DxgkVidMmInitializeAdapter: QUERYSEGMENT not supported\n");
            return STATUS_NOT_SUPPORTED;
        }
        DPRINT1("DxgkVidMmInitializeAdapter: QUERYSEGMENT failed: 0x%08lx\n",
                Status);
        return Status;
    }

    if (UsingSeg4 && SegOut4.SegmentDescriptorStride > Seg4Stride)
        Seg4Stride = SegOut4.SegmentDescriptorStride;
    if (UsingSeg4 && (Seg4Stride < sizeof(DXGK_SEGMENTDESCRIPTOR4) || Seg4Stride > VIDMM_MAX_SEGMENT_DESCRIPTOR_STRIDE))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    SegmentCount = UsingSeg4 ? SegOut4.NbSegment
                             : (UsingSeg3 ? SegOut3.NbSegment : SegOut.NbSegment);

    if (SegmentCount == 0 || SegmentCount > VIDMM_MAX_SEGMENTS)
    {
        DPRINT1("DxgkVidMmInitializeAdapter: miniport returned invalid "
                "segment count %lu (max %d)\n", SegmentCount, VIDMM_MAX_SEGMENTS);
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT("DxgkVidMmInitializeAdapter: miniport reports %lu segments\n",
           SegmentCount);

    /* -----------------------------------------------------------------------
     * Step 2: Allocate the miniport descriptor array for the fill pass
     *         (stride-matched to the query flavour in use).
     * ----------------------------------------------------------------------- */
    DescStride = UsingSeg4 ? Seg4Stride
                           : (UsingSeg3 ? sizeof(DXGK_SEGMENTDESCRIPTOR3)
                                        : sizeof(DXGK_SEGMENTDESCRIPTOR));
    if (DescStride == 0 || SegmentCount > MAXULONG_PTR / DescStride)
        return STATUS_INTEGER_OVERFLOW;
    DescBufferSize = (SIZE_T)SegmentCount * DescStride;

    DescBuffer = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                               DescBufferSize,
                                               TAG_VIDMM_SEGMENT);
    if (DescBuffer == NULL)
    {
        DPRINT1("DxgkVidMmInitializeAdapter: cannot allocate descriptor "
                "array for %lu segments\n", SegmentCount);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(DescBuffer, DescBufferSize);

    /* -----------------------------------------------------------------------
     * Step 3: Fill pass — miniport writes segment descriptors.
     * ----------------------------------------------------------------------- */
    if (UsingSeg4)
    {
        SegOut4.NbSegment               = SegmentCount;
        SegOut4.pSegmentDescriptor      = DescBuffer;
        SegOut4.SegmentDescriptorStride = Seg4Stride;
    }
    else if (UsingSeg3)
    {
        SegOut3.NbSegment          = SegmentCount;
        SegOut3.pSegmentDescriptor = (PDXGK_SEGMENTDESCRIPTOR3)DescBuffer;
    }
    else
    {
        SegOut.NbSegment          = SegmentCount;
        SegOut.pSegmentDescriptor = (PDXGK_SEGMENTDESCRIPTOR)DescBuffer;
    }

    Status = Adapter->MiniportContext->InitData.s.DxgkDdiQueryAdapterInfo(
                 Adapter->MiniportDeviceContext,
                 &QueryInfo);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxgkVidMmInitializeAdapter: QUERYSEGMENT fill-pass "
                "failed: 0x%08lx\n", Status);
        ExFreePoolWithTag(DescBuffer, TAG_VIDMM_SEGMENT);
        return Status;
    }

    if (UsingSeg4)
    {
        PagingBufferSegmentId = SegOut4.PagingBufferSegmentId;
        PagingBufferSize      = SegOut4.PagingBufferSize;
    }
    else if (UsingSeg3)
    {
        PagingBufferSegmentId = SegOut3.PagingBufferSegmentId;
        PagingBufferSize      = SegOut3.PagingBufferSize;
    }
    else
    {
        PagingBufferSegmentId = SegOut.PagingBufferSegmentId;
        PagingBufferSize      = SegOut.PagingBufferSize;
    }

    /* -----------------------------------------------------------------------
     * Step 4: Allocate the runtime DXGKRNL_SEGMENT array.
     * ----------------------------------------------------------------------- */
    Segments = (PDXGKRNL_SEGMENT)ExAllocatePoolWithTag(
                   NonPagedPool,
                   SegmentCount * sizeof(DXGKRNL_SEGMENT),
                   TAG_VIDMM_SEGMENT);

    if (Segments == NULL)
    {
        DPRINT1("DxgkVidMmInitializeAdapter: cannot allocate segment "
                "array (%lu entries)\n", SegmentCount);
        ExFreePoolWithTag(DescBuffer, TAG_VIDMM_SEGMENT);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Segments, SegmentCount * sizeof(DXGKRNL_SEGMENT));
    LowestAddress.QuadPart = 0;
    HighestAddress = Adapter->HighestAcceptableAddress;
    if (HighestAddress.QuadPart == 0)
        HighestAddress.QuadPart = (LONGLONG)-1;
    BoundaryAddress.QuadPart = 0;

    /* -----------------------------------------------------------------------
     * Step 5: Populate each DXGKRNL_SEGMENT from the miniport descriptor
     *         (the two descriptor flavours carry the same placement facts).
     * ----------------------------------------------------------------------- */
/* The three descriptor flavours carry identical placement fields. */
#define VIDMM_READ_SEGMENT_DESC(DescType)                                   \
        do {                                                                \
            DescType *Desc = (DescType *)(DescBuffer + i * DescStride);     \
            Seg->Size        = (ULONGLONG)Desc->Size;                       \
            Seg->CommitLimit = (ULONGLONG)Desc->CommitLimit;                \
            Seg->BaseAddress = Desc->BaseAddress;                           \
            Seg->CpuTranslatedAddress = Desc->CpuTranslatedAddress;         \
            Seg->Flags       = Desc->Flags;                                 \
        } while (0)

    for (i = 0; i < SegmentCount; i++)
    {
        PDXGKRNL_SEGMENT        Seg  = &Segments[i];

        /* Segment IDs are 1-based per WDDM convention. */
        Seg->SegmentId    = i + 1;
        Seg->CpuBase      = NULL;   /* mapped lazily on first CPU access */

        if (UsingSeg4)
            VIDMM_READ_SEGMENT_DESC(DXGK_SEGMENTDESCRIPTOR4);
        else if (UsingSeg3)
            VIDMM_READ_SEGMENT_DESC(DXGK_SEGMENTDESCRIPTOR3);
        else
            VIDMM_READ_SEGMENT_DESC(DXGK_SEGMENTDESCRIPTOR);

        if (Seg->Size == 0 || (VidMmSegmentIsAperture(Seg) ? (Seg->CommitLimit > Seg->Size || Seg->Flags.PopulatedFromSystemMemory) : Seg->CommitLimit != Seg->Size) || (Seg->Flags.LocalBudgetGroup && Seg->Flags.NonLocalBudgetGroup) || (Seg->Flags.Agp && Seg->Flags.Value != (1UL << 1)) || (DxgkCapsCoreInterfaceVersionInRange(Adapter->MiniportContext->InitData.s.Version, DXGK_CAPS_CORE_LEVEL_WDDM_1_0, DXGK_CAPS_CORE_LEVEL_WDDM_2_0) && Seg->Flags.Reserved != 0))
        {
            DPRINT1("DxgkVidMmInitializeAdapter: invalid segment %lu size=0x%I64x commit=0x%I64x flags=0x%lx\n", i, Seg->Size, Seg->CommitLimit, Seg->Flags.Value);
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto FailSegmentInitialization;
        }
        if (Seg->Flags.Agp)
        {
            DPRINT1("DxgkVidMmInitializeAdapter: AGP segment %lu requires an unavailable GART service\n", i);
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            goto FailSegmentInitialization;
        }
        if (UsingSeg4 && (Seg->Flags.SupportsCpuHostAperture || Seg->Flags.SupportsCachedCpuHostAperture))
        {
            DPRINT1("DxgkVidMmInitializeAdapter: CPU host aperture segment %lu is not implemented\n", i);
            Status = STATUS_NOT_SUPPORTED;
            goto FailSegmentInitialization;
        }

        InitializeListHead(&Seg->AllocationList);
        ExInitializeFastMutex(&Seg->Lock);

        if (VidMmSegmentIsAperture(Seg))
        {
            Seg->DummyPageVa = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, LowestAddress, HighestAddress, BoundaryAddress, MmCached);
            if (Seg->DummyPageVa == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto FailSegmentInitialization;
            }
            RtlZeroMemory(Seg->DummyPageVa, PAGE_SIZE);
            Seg->DummyPage = MmGetPhysicalAddress(Seg->DummyPageVa);
        }

        /*
         * Cache paging-buffer parameters.  The paging buffer segment ID
         * and size are adapter-global (the same for all segments); store
         * them on segment[0] where DxgkVidMmEvict can find them.
         */
        if (i == 0)
        {
            Seg->PagingBufferSegmentId = PagingBufferSegmentId;
            Seg->PagingBufferSize      = (PagingBufferSize != 0)
                                         ? PagingBufferSize
                                         : VIDMM_PAGING_BUFFER_SIZE_DEFAULT;
        }

        DPRINT("DxgkVidMmInitializeAdapter: seg[%lu] id=%lu "
               "size=0x%I64x base=0x%I64x flags=0x%lx %s\n",
               i, Seg->SegmentId,
               Seg->Size,
               Seg->BaseAddress.QuadPart,
               Seg->Flags,
               VidMmSegmentIsAperture(Seg) ? "APERTURE" : "VRAM");
    }
#undef VIDMM_READ_SEGMENT_DESC

    /* -----------------------------------------------------------------------
     * Step 6: Publish the segment table to the adapter.
     *
     * On x86-64 (TSO) a store followed by KeMemoryBarrier() (MFENCE) ensures
     * the write to Adapter->Segments is visible to all CPUs before any
     * subsequent code that reads Adapter->SegmentCount can observe the new
     * Segments pointer.
     * ----------------------------------------------------------------------- */
    Adapter->Segments      = (PVOID)Segments;
    Adapter->SegmentCount  = SegmentCount;
    KeMemoryBarrier();

    ExFreePoolWithTag(DescBuffer, TAG_VIDMM_SEGMENT);

    DPRINT("DxgkVidMmInitializeAdapter: %lu segments initialised for "
           "adapter %p\n", SegmentCount, Adapter);

    return STATUS_SUCCESS;

FailSegmentInitialization:
    while (i != 0)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[--i];

        if (Seg->DummyPageVa != NULL)
            MmFreeContiguousMemory(Seg->DummyPageVa);
    }
    ExFreePoolWithTag(Segments, TAG_VIDMM_SEGMENT);
    ExFreePoolWithTag(DescBuffer, TAG_VIDMM_SEGMENT);
    return Status;
}


/*
 * DxgkVidMmTeardownAdapter
 *
 * Releases all VidMm state associated with an adapter.  Called from
 * DxgkAdapterStop after all allocations should have been destroyed.
 */
VOID
DxgkVidMmQuiesceAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    DxgkpVidMmEnsureGlobalsInitialized();
    for (;;)
    {
        BOOLEAN Drained;
        BOOLEAN EventSignaled;

        ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
        InterlockedExchange(&Adapter->VidMmDestroyQueuesBlocked, 1);
        Drained = DxgkVidMmWorkerDrainCoreIsDrainedLocked(&Adapter->VidMmDestroyWorkerCount);
        EventSignaled = KeReadStateEvent(&Adapter->VidMmDestroyWorkersDrainedEvent) != 0;
        if (EventSignaled != Drained)
        {
            if (Drained)
                KeSetEvent(&Adapter->VidMmDestroyWorkersDrainedEvent, IO_NO_INCREMENT, FALSE);
            else
                KeResetEvent(&Adapter->VidMmDestroyWorkersDrainedEvent);
        }
        ASSERT((KeReadStateEvent(&Adapter->VidMmDestroyWorkersDrainedEvent) != 0) == Drained);
        ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
        if (Drained)
            return;
        KeWaitForSingleObject(&Adapter->VidMmDestroyWorkersDrainedEvent, Executive, KernelMode, FALSE, NULL);
    }
}

VOID
DxgkVidMmResumeAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    InterlockedExchange(&Adapter->VidMmDestroyQueuesBlocked, 0);
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);

    DxgkVidMmKickDeferredDestroyBatches(Adapter);
}

VOID
DxgkVidMmKickDeferredDestroyBatches(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    DxgkpVidMmEnsureGlobalsInitialized();
    if (InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0)
        return;

    for (;;)
    {
        PDXGKVMM_DESTROY_BATCH Batch = NULL;
        PLIST_ENTRY Entry;
        BOOLEAN AdmissionBlocked;
        BOOLEAN ResetDrainedEvent = FALSE;
        BOOLEAN KmdAdmissionReserved = FALSE;

        ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
        AdmissionBlocked = InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->KmdCallsBlocked, 0, 0) != 0;
        if (AdmissionBlocked)
        {
            ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
            break;
        }
        for (Entry = DxgkVidMmDestroyBatchListHead.Flink; Entry != &DxgkVidMmDestroyBatchListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_DESTROY_BATCH Candidate = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);

            if (Candidate->Adapter != Adapter || InterlockedCompareExchange(&Candidate->Listed, 0, 0) == 0 || InterlockedCompareExchange(&Candidate->Quarantined, 0, 0) == 0 || InterlockedCompareExchange(&Candidate->AdmissionDeferred, 0, 0) == 0 || Candidate->AwaitingStopBoundary || InterlockedCompareExchange(&Candidate->DestroyCommitted, 0, 0) == 0 || InterlockedCompareExchange(&Candidate->PendingAllocationCount, 0, 0) != 0 || InterlockedCompareExchange(&Candidate->WorkQueued, 0, 0) != 0 || InterlockedCompareExchange(&Candidate->WorkerCounted, 0, 0) != 0)
                continue;
            KmdAdmissionReserved = DxgkAcquireKmdCall(Adapter);
            if (!KmdAdmissionReserved)
                break;
            if (!DxgkVidMmWorkerDrainCoreTryAdmitLocked(&Adapter->VidMmDestroyWorkerCount, AdmissionBlocked, &ResetDrainedEvent))
            {
                DxgkReleaseKmdCall(Adapter);
                break;
            }
            InterlockedExchange(&Candidate->AdmissionDeferred, 0);
            InterlockedExchange(&Candidate->Quarantined, 0);
            InterlockedExchange(&Candidate->WorkQueued, 1);
            InterlockedExchange(&Candidate->WorkerCounted, 1);
            InterlockedExchange(&Candidate->KmdAdmissionReserved, 1);
            if (ResetDrainedEvent)
                KeResetEvent(&Adapter->VidMmDestroyWorkersDrainedEvent);
            Batch = Candidate;
            break;
        }
        ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
        if (Batch == NULL)
            break;
        ExQueueWorkItem(&Batch->WorkItem, DelayedWorkQueue);
    }
}

static ULONG
DxgkpVidMmForceLocalUnbatchedAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG ProcessedCount = 0;

    for (;;)
    {
        PDXGKVMM_ALLOCATION Allocation = NULL;
        PDXGKVMM_RESOURCE Resource;
        PLIST_ENTRY Entry;

        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        for (Entry = DxgkVidMmAllocationListHead.Flink; Entry != &DxgkVidMmAllocationListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_ALLOCATION Candidate = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);

            if (Candidate->Adapter != Adapter || Candidate->DestroyBatch != NULL)
                continue;
            Allocation = Candidate;
            break;
        }
        if (Allocation != NULL)
        {
            Resource = Allocation->Resource;
            InterlockedExchange(&Allocation->Destroying, 1);
            Allocation->Initializing = FALSE;
            Allocation->Device = NULL;
            Allocation->MiniportDeviceHandle = NULL;
            Allocation->OpenBindingHandle = NULL;
            if (Allocation->BackingAllocation == NULL)
            {
                Allocation->MiniportHandle = NULL;
                Allocation->MiniportResourceHandle = NULL;
                Allocation->DestroyMiniportResource = FALSE;
            }
            RemoveEntryList(&Allocation->GlobalAllocationEntry);
            InitializeListHead(&Allocation->GlobalAllocationEntry);
            if (Resource != NULL && !IsListEmpty(&Allocation->ResourceEntry))
            {
                ASSERT(Resource->AllocationCount != 0);
                RemoveEntryList(&Allocation->ResourceEntry);
                InitializeListHead(&Allocation->ResourceEntry);
                Resource->AllocationCount--;
            }
        }
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (Allocation == NULL)
            break;
        DxgkpVidMmDropLogicalHandleReference(Allocation);
        DxgkpVidMmDropAllocationHandleReference(Allocation);
        ProcessedCount++;
    }
    return ProcessedCount;
}

static BOOLEAN
DxgkpVidMmAdapterHasDestroyBatches(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PLIST_ENTRY Entry;
    BOOLEAN HasBatches = FALSE;

    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    for (Entry = DxgkVidMmDestroyBatchListHead.Flink; Entry != &DxgkVidMmDestroyBatchListHead; Entry = Entry->Flink)
    {
        PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);

        if (Batch->Adapter == Adapter)
        {
            HasBatches = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
    return HasBatches;
}

static ULONG
DxgkpVidMmForceLocalResources(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG ProcessedCount = 0;

    if (DxgkpVidMmAdapterHasDestroyBatches(Adapter))
        return 0;
    for (;;)
    {
        PDXGKVMM_RESOURCE Resource = NULL;
        PLIST_ENTRY Entry;

        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        for (Entry = DxgkVidMmResourceListHead.Flink; Entry != &DxgkVidMmResourceListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_RESOURCE Candidate = CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry);

            if (Candidate->Adapter != Adapter || Candidate->AllocationCount != 0 || !IsListEmpty(&Candidate->AllocationList))
                continue;
            Resource = Candidate;
            InterlockedExchange(&Resource->Destroying, 1);
            InterlockedExchange(&Resource->CloseUncertain, 0);
            InterlockedExchange(&Resource->DestroyFailureUncertain, 0);
            Resource->Device = NULL;
            Resource->MiniportHandle = NULL;
            RemoveEntryList(&Resource->GlobalResourceEntry);
            InitializeListHead(&Resource->GlobalResourceEntry);
            break;
        }
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (Resource == NULL)
            break;
        DxgkVidMmDereferenceResource(Resource);
        ProcessedCount++;
    }
    return ProcessedCount;
}

static ULONG
DxgkpVidMmForceLocalAdapterBackings(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG ProcessedCount;

    ASSERT(Adapter->MiniportRemoveDeviceComplete);
    ASSERT(Adapter->MiniportDeviceStopped);
    ASSERT(InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0);
    ASSERT(InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0);
    ProcessedCount = DxgkpVidMmForceLocalUnbatchedAllocations(Adapter);
    while (DxgkpVidMmForceQuarantinedDestroyBatches(Adapter) != 0)
        ProcessedCount++;
    ProcessedCount += DxgkpVidMmForceLocalResources(Adapter);
    return ProcessedCount;
}

static VOID
DxgkpVidMmRetireOwnerLedger(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGMMS2_VIDMM_INTERFACE_V1 VidMm = DxgkpVidMmOwner(Adapter);

    if (VidMm == NULL)
        return;
    /* Clear validity first so nothing can place into a ledger being retired. */
    InterlockedExchange(&Adapter->Mms2VidMmValid, 0);
    VidMm->Stop(VidMm->VidMmHandle);
}

VOID
DxgkVidMmTeardownAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_SEGMENT    Segments;
    ULONG               i;
    ULONG               StallCount = 0;

    DPRINT("DxgkVidMmTeardownAdapter: Adapter %p\n", Adapter);

    DxgkVidMmQuiesceAdapter(Adapter);
    while (InterlockedCompareExchange(&Adapter->VidMmBackingCount, 0, 0) != 0)
    {
        LONG BeforeCount = InterlockedCompareExchange(&Adapter->VidMmBackingCount, 0, 0);
        ULONG ForcedCount;

        if (Adapter->MiniportRemoveDeviceComplete)
            ForcedCount = DxgkpVidMmForceLocalAdapterBackings(Adapter);
        else
        {
            DxgkVidMmCleanupAdapterAllocations(Adapter);
            ForcedCount = DxgkpVidMmForceQuarantinedDestroyBatches(Adapter);
        }
        if (InterlockedCompareExchange(&Adapter->VidMmBackingCount, 0, 0) < BeforeCount || ForcedCount != 0)
            StallCount = 0;
        else
        {
            LARGE_INTEGER Delay;

            StallCount++;
            if ((StallCount & 0x3FF) == 1)
                DPRINT1("DxgkVidMmTeardownAdapter: waiting for %ld retained backings at fixed point\n", InterlockedCompareExchange(&Adapter->VidMmBackingCount, 0, 0));
            Delay.QuadPart = -10 * 1000;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        }
    }

    if (Adapter->Segments == NULL)
    {
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        DxgkpVidMmReleaseAdapterBudgets(Adapter);
#endif
        DxgkpVidMmRetireOwnerLedger(Adapter);
        return;
    }

    Segments = ADAPTER_SEGMENTS(Adapter);

    /*
     * Release anything still placed before the ledger is retired.  Callers
     * are expected to have destroyed every allocation by now, but a teardown
     * step earlier in the sequence can fail (a closed KMD transaction will
     * fail the shared-primary destroy, for instance), and the retire must be
     * correct regardless of who got there first.
     */
    for (i = 0; i < Adapter->SegmentCount; i++)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[i];

        for (;;)
        {
            PDXGKVMM_ALLOCATION Straggler = NULL;

            ExAcquireFastMutex(&Seg->Lock);
            if (!IsListEmpty(&Seg->AllocationList))
                Straggler = CONTAINING_RECORD(Seg->AllocationList.Flink, DXGKVMM_ALLOCATION, SegmentEntry);
            ExReleaseFastMutex(&Seg->Lock);
            if (Straggler == NULL)
                break;
            DPRINT1("DxgkVidMmTeardownAdapter: releasing allocation %p still placed in segment %lu\n", Straggler, Seg->SegmentId);
            DxgkpVidMmReleaseSegmentPlacement(Straggler);
        }
    }

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    DxgkpVidMmReleaseAdapterBudgets(Adapter);
#endif
    DxgkpVidMmRetireOwnerLedger(Adapter);

    for (i = 0; i < Adapter->SegmentCount; i++)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[i];

        if (Seg->CpuBase != NULL)
        {
            MmUnmapIoSpace(Seg->CpuBase, (SIZE_T)Seg->Size);
            Seg->CpuBase = NULL;
            DPRINT("DxgkVidMmTeardownAdapter: unmapped CPU VA for segment %lu\n",
                   Seg->SegmentId);
        }
        if (Seg->DummyPageVa != NULL)
        {
            MmFreeContiguousMemory(Seg->DummyPageVa);
            Seg->DummyPageVa = NULL;
            Seg->DummyPage.QuadPart = 0;
        }
    }

    ExFreePoolWithTag(Adapter->Segments, TAG_VIDMM_SEGMENT);
    Adapter->Segments     = NULL;
    Adapter->SegmentCount = 0;

    DPRINT("DxgkVidMmTeardownAdapter: done\n");
}


/* ALLOCATION MANAGEMENT ******************************************************/

/*
 * DxgkVidMmCreateAllocation
 *
 * Creates a GPU memory allocation:
 *   1. Calls the miniport's DxgkDdiCreateAllocation for private data,
 *      size, alignment, segment preferences, and miniport handle.
 *   2. Allocates a DXGKVMM_ALLOCATION tracking object.
 *   3. Tries to place in a VRAM segment (preferred segments first, then all).
 *   4. Falls back to NonPagedPool system memory if VRAM is unavailable.
 *
 * DeviceCtx is reserved (pass NULL); it may be used in future to look up
 * the DXGKRNL_DEVICE for device-scoped allocation lists.
 */
static NTSTATUS
DxgkpVidMmRollbackMiniportAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PHANDLE MiniportAllocationHandle,
    _In_opt_ HANDLE MiniportResourceHandle,
    _In_ BOOLEAN DestroyResource,
    _Out_opt_ PHANDLE OutResourceHandle)
{
    DXGKARG_DESTROYALLOCATION DestroyArgs;
    NTSTATUS Status;

    if (Adapter == NULL || MiniportAllocationHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    if (DXGK_CB_FULL(Adapter, DxgkDdiDestroyAllocation) == NULL)
        return (*MiniportAllocationHandle == NULL && !DestroyResource) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
    DestroyArgs.NumAllocations = *MiniportAllocationHandle != NULL ? 1 : 0;
    DestroyArgs.phAllocation = *MiniportAllocationHandle != NULL ? MiniportAllocationHandle : NULL;
    DestroyArgs.hResource = MiniportResourceHandle;
    DestroyArgs.Flags.DestroyResource = DestroyResource;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiDestroyAllocation)(Adapter->MiniportDeviceContext, &DestroyArgs);
    DxgkReleaseMiniportCallback(Adapter);
    if (NT_SUCCESS(Status))
    {
        *MiniportAllocationHandle = NULL;
        if (OutResourceHandle != NULL)
            *OutResourceHandle = DestroyArgs.hResource;
    }
    return Status;
}

static NTSTATUS
DxgkpVidMmCreateAllocationTracked(
    _In_      PDXGKRNL_ADAPTER         Adapter,
    _In_opt_  PDXGKRNL_DEVICE          Device,
    _In_      DXGK_ALLOCATIONINFO     *AllocInfo,
    _In_opt_  CONST VOID              *CreatePrivateDriverData,
    _In_      UINT                     CreatePrivateDriverDataSize,
    _In_opt_  HANDLE                   ResourceHandle,
    _In_      DXGK_CREATEALLOCATIONFLAGS CreateFlags,
    _Out_     PHANDLE                  OutHandle,
    _Out_opt_ PHANDLE                  OutResourceHandle,
    _Out_opt_ PDXGKVMM_ALLOCATION     *OutAllocation)
{
    NTSTATUS                    Status;
    DXGKARG_CREATEALLOCATION    CreateArgs;
    PDXGKVMM_ALLOCATION         Alloc = NULL;
    PDXGKVMM_ALLOCATION         RollbackAllocations[1];
    PDXGKVMM_DESTROY_BATCH      RollbackBatch = NULL;
    ULONG                       i;
    BOOLEAN                     Placed;
    BOOLEAN                     MiniportAllocationCreated = FALSE;
    BOOLEAN                     KmdTransactionStarted = FALSE;
    SIZE_T                      RoundedAllocationSize;
    SIZE_T                      RoundedPitchAlignedSize = 0;

    DPRINT("DxgkVidMmCreateAllocation: Adapter=%p Size=%Iu Align=%u\n",
           Adapter,
           AllocInfo ? AllocInfo->Size : 0,
           AllocInfo ? AllocInfo->Alignment : 0);

    if (OutAllocation != NULL)
        *OutAllocation = NULL;
    if (Adapter == NULL || AllocInfo == NULL || OutHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutHandle = NULL;
    if (OutResourceHandle != NULL)
        *OutResourceHandle = ResourceHandle;
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->UseDodLayout || Adapter->MiniportContext->IsDisplayOnlyDriver)
        return STATUS_NOT_SUPPORTED;

    DxgkpVidMmEnsureGlobalsInitialized();
    Alloc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Alloc), TAG_VIDMM_ALLOC);
    RollbackBatch = DxgkpVidMmAllocateDestroyBatch(Adapter, 1);
    if (Alloc == NULL || RollbackBatch == NULL)
    {
        if (RollbackBatch != NULL)
            DxgkpVidMmFreeDestroyBatch(RollbackBatch);
        if (Alloc != NULL)
            ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Alloc, sizeof(*Alloc));
    DxgkpVidMmInitializeAllocationLifetime(Alloc);
    KeInitializeMutex(&Alloc->ResidencyLock, 0);
    Alloc->Adapter = Adapter;
    Alloc->Device = Device;
    Alloc->MiniportDeviceHandle = Device != NULL ? Device->hMiniportDevice : NULL;
    Alloc->Initializing = Device != NULL;
    Alloc->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle, DxgkVidMmAllocationHandleCookie);
    Alloc->Magic = DXGKVMM_ALLOCATION_MAGIC;
    KeInitializeMutex(&Alloc->UserModeLock, 0);
    InitializeListHead(&Alloc->SegmentEntry);
    InitializeListHead(&Alloc->DeviceEntry);
    InitializeListHead(&Alloc->GlobalAllocationEntry);
    InitializeListHead(&Alloc->ResourceEntry);

    /* -----------------------------------------------------------------------
     * Step 1: Notify miniport via DxgkDdiCreateAllocation.
     *
     * We build a single-entry wrapper around the caller's AllocInfo.
     * On return the miniport will have updated:
     *   AllocInfo->hAllocation       — miniport private handle
     *   AllocInfo->Size              — may be rounded up
     *   AllocInfo->Alignment         — may be increased
     *   AllocInfo->Flags             — capability flags
     *   AllocInfo->AllocationPriority
     * ----------------------------------------------------------------------- */
    RtlZeroMemory(&CreateArgs, sizeof(CreateArgs));
    CreateArgs.pAllocationInfo       = AllocInfo;
    CreateArgs.NumAllocations        = 1;
    CreateArgs.hResource             = ResourceHandle;
    CreateArgs.Flags                 = CreateFlags;
    CreateArgs.pPrivateDriverData    = (PVOID)CreatePrivateDriverData;
    CreateArgs.PrivateDriverDataSize = CreatePrivateDriverDataSize;

    if (DXGK_CB_FULL(Adapter, DxgkDdiCreateAllocation) != NULL)
    {
        if (!DxgkBeginKmdTransaction(Adapter))
        {
            Status = STATUS_DEVICE_NOT_READY;
            goto FailTrackedAllocation;
        }
        KmdTransactionStarted = TRUE;
        Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateAllocation)(Adapter->MiniportDeviceContext, &CreateArgs);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkVidMmCreateAllocation: DxgkDdiCreateAllocation "
                    "failed: 0x%08lx\n", Status);
            if (AllocInfo->hAllocation != NULL || (CreateFlags.Resource && ResourceHandle == NULL && CreateArgs.hResource != NULL))
            {
                MiniportAllocationCreated = TRUE;
                Alloc->MiniportHandle = AllocInfo->hAllocation;
                Alloc->MiniportResourceHandle = CreateArgs.hResource;
                Alloc->DestroyMiniportResource = CreateFlags.Resource && ResourceHandle == NULL;
                goto FailMiniportAllocation;
            }
            DxgkEndKmdTransaction(Adapter);
            DxgkpVidMmFreeDestroyBatch(RollbackBatch);
            Alloc->Magic = 0;
            ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);
            return Status;
        }
        if (OutResourceHandle != NULL)
            *OutResourceHandle = CreateArgs.hResource;
        MiniportAllocationCreated = TRUE;
    }
    Alloc->MiniportHandle = AllocInfo->hAllocation;
    if (MiniportAllocationCreated && CreateFlags.Resource)
    {
        Alloc->MiniportResourceHandle = CreateArgs.hResource;
        Alloc->DestroyMiniportResource = ResourceHandle == NULL;
    }

    if (AllocInfo->Size == 0 &&
        AllocInfo->pPrivateDriverData != NULL &&
        AllocInfo->PrivateDriverDataSize >= sizeof(UINT))
    {
        UINT RequestedSize = *(const UINT *)AllocInfo->pPrivateDriverData;
        if (RequestedSize != 0)
            AllocInfo->Size = RequestedSize;
    }

    /*
     * The incoming size can legitimately be zero.  Full WDDM miniports such as
     * viogpu3d derive the final allocation size from their private creation
     * data inside DxgkDdiCreateAllocation and write it back to AllocInfo->Size.
     * Reject only if the size is still zero after the miniport had a chance to
     * populate it.
     */
    if (AllocInfo->Size == 0)
    {
        DPRINT1("DxgkVidMmCreateAllocation: zero-byte allocation remained after DxgkDdiCreateAllocation\n");
        Status = STATUS_INVALID_PARAMETER;
        goto FailMiniportAllocation;
    }
    if (!DxgkpVidMmRoundUpPageSize(AllocInfo->Size, &RoundedAllocationSize))
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto FailMiniportAllocation;
    }
    if (AllocInfo->PitchAlignedSize != 0)
    {
        if (AllocInfo->PitchAlignedSize < AllocInfo->Size)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto FailMiniportAllocation;
        }
        if (!DxgkpVidMmRoundUpPageSize(AllocInfo->PitchAlignedSize, &RoundedPitchAlignedSize))
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto FailMiniportAllocation;
        }
    }
    if (AllocInfo->Alignment != 0 && (AllocInfo->Alignment & (AllocInfo->Alignment - 1)) != 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto FailMiniportAllocation;
    }
    if (Adapter->Segments != NULL && !DxgkpVidMmValidateAllocationSegmentSets(Adapter, AllocInfo))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto FailMiniportAllocation;
    }

    /* -----------------------------------------------------------------------
     * Step 2: Allocate DXGKVMM_ALLOCATION tracking object.
     * Pool tag 'AlxD' — displayed as 'DxlA' in pool-tag dumps.
     * ----------------------------------------------------------------------- */
    Alloc->Size               = RoundedAllocationSize;
    Alloc->PitchAlignedSize   = RoundedPitchAlignedSize;
    Alloc->Alignment          = (AllocInfo->Alignment != 0)
                                ? (SIZE_T)AllocInfo->Alignment
                                : PAGE_SIZE;
    Alloc->SupportedWriteSegmentSet = AllocInfo->SupportedWriteSegmentSet;
    Alloc->EvictionSegmentSet = AllocInfo->EvictionSegmentSet;
    Alloc->PreferredSegment.Value = AllocInfo->PreferredSegment.Value;
    Alloc->AccessedPhysically =
        AllocInfo->FlagsWddm2.AccessedPhysically != 0;
    Alloc->AllocationPriority = AllocInfo->AllocationPriority
                                ? AllocInfo->AllocationPriority
                                : VIDMM_PRIORITY_NORMAL;
    Alloc->CpuVisible         = (AllocInfo->Flags.CpuVisible != 0);
    Alloc->Resident           = FALSE;
    Alloc->Resource           = NULL;

    if (AllocInfo->PrivateDriverDataSize != 0)
    {
        if (AllocInfo->pPrivateDriverData == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto FailTrackedAllocation;
        }

        Alloc->PrivateDriverData = ExAllocatePoolWithTag(NonPagedPool,
                                                         AllocInfo->PrivateDriverDataSize,
                                                         TAG_VIDMM_ALLOC);
        if (Alloc->PrivateDriverData == NULL)
        {
            Status = STATUS_NO_MEMORY;
            goto FailTrackedAllocation;
        }

        RtlCopyMemory(Alloc->PrivateDriverData,
                      AllocInfo->pPrivateDriverData,
                      AllocInfo->PrivateDriverDataSize);
        Alloc->PrivateDriverDataSize = AllocInfo->PrivateDriverDataSize;
    }

    /* -----------------------------------------------------------------------
     * Step 3: Place in VRAM segment (preferred segments first, then all).
     * ----------------------------------------------------------------------- */
    Placed = FALSE;

    if (Adapter->Segments != NULL)
    {
        PDXGKRNL_SEGMENT Segments = ADAPTER_SEGMENTS(Adapter);
        ULONG PreferredIds[5];
        ULONG NumPreferred = 0;

        /* Extract up to 5 preferred segment IDs from AllocInfo. */
        if (AllocInfo->PreferredSegment.SegmentId0 != 0)
            PreferredIds[NumPreferred++] = AllocInfo->PreferredSegment.SegmentId0;
        if (AllocInfo->PreferredSegment.SegmentId1 != 0)
            PreferredIds[NumPreferred++] = AllocInfo->PreferredSegment.SegmentId1;
        if (AllocInfo->PreferredSegment.SegmentId2 != 0)
            PreferredIds[NumPreferred++] = AllocInfo->PreferredSegment.SegmentId2;
        if (AllocInfo->PreferredSegment.SegmentId3 != 0)
            PreferredIds[NumPreferred++] = AllocInfo->PreferredSegment.SegmentId3;
        if (AllocInfo->PreferredSegment.SegmentId4 != 0)
            PreferredIds[NumPreferred++] = AllocInfo->PreferredSegment.SegmentId4;

        /* Try preferred segments first. */
        for (i = 0; i < NumPreferred && !Placed; i++)
        {
            ULONG SegId = PreferredIds[i];

            if (SegId < 1 || SegId > Adapter->SegmentCount)
                continue;
            if (!VidMmAllocationSupportsSegment(Alloc, SegId))
                continue;

            Status = DxgkVidMmTryPlaceInSegment(
                         &Segments[SegId - 1], Alloc);

            if (NT_SUCCESS(Status))
            {
                Placed = TRUE;
                DPRINT("DxgkVidMmCreateAllocation: placed in preferred "
                       "segment %lu at offset 0x%I64x\n",
                       SegId, Alloc->SegmentOffset);
            }
        }

        /* Fallback: scan all segments, preferring dedicated VRAM first. */
        for (i = 0; i < Adapter->SegmentCount && !Placed; i++)
        {
            PDXGKRNL_SEGMENT Seg = &Segments[i];
            ULONG j;
            BOOLEAN AlreadyTried = FALSE;

            /* Skip segments already tried via preferred list. */
            for (j = 0; j < NumPreferred; j++)
            {
                if (PreferredIds[j] == Seg->SegmentId)
                {
                    AlreadyTried = TRUE;
                    break;
                }
            }
            if (AlreadyTried)
                continue;
            if (!VidMmAllocationSupportsSegment(Alloc, Seg->SegmentId))
                continue;

            Status = DxgkVidMmTryPlaceInSegment(Seg, Alloc);

            if (NT_SUCCESS(Status))
            {
                Placed = TRUE;
                DPRINT("DxgkVidMmCreateAllocation: placed in segment %lu "
                       "at offset 0x%I64x\n",
                       Seg->SegmentId, Alloc->SegmentOffset);
            }
        }
    }

    /* -----------------------------------------------------------------------
     * Step 4: Allocate system backing if segment placement was impossible,
     * or if the resident segment is an aperture that needs RAM pages to
     * attach behind it.
     * ----------------------------------------------------------------------- */
    if (!Placed ||
        (Alloc->Resident &&
         Alloc->SegmentId <= Adapter->SegmentCount &&
         VidMmSegmentIsAperture(&ADAPTER_SEGMENTS(Adapter)[Alloc->SegmentId - 1]) &&
         Alloc->SystemMemory == NULL))
    {
        SIZE_T AllocSize;

        if (!DxgkpVidMmRoundUpPageSize(Alloc->Size, &AllocSize))
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto FailTrackedAllocation;
        }

        DPRINT("DxgkVidMmCreateAllocation: allocating system backing "
               "(placed=%d size=%Iu bytes seg=%lu)\n",
               Placed, AllocSize, Alloc->SegmentId);

        Alloc->SystemMemory = ExAllocatePoolWithTag(
                                  NonPagedPool,
                                  AllocSize,
                                  TAG_VIDMM_ALLOC);

        if (Alloc->SystemMemory == NULL)
        {
            DPRINT1("DxgkVidMmCreateAllocation: system memory fallback "
                    "failed for %Iu bytes\n", AllocSize);

            Status = STATUS_NO_MEMORY;
            goto FailTrackedAllocation;
        }

        /* Zero the backing memory so GPU sees a clean allocation. */
        RtlZeroMemory(Alloc->SystemMemory, AllocSize);

        if (!Placed)
        {
            Alloc->PhysicalAddress = MmGetPhysicalAddress(Alloc->SystemMemory);
        }

        DPRINT("DxgkVidMmCreateAllocation: system backing at VA=%p "
               "PA=0x%I64x resident=%d\n",
               Alloc->SystemMemory,
               Alloc->PhysicalAddress.QuadPart,
               Alloc->Resident);

        /* For CPU-visible allocations the system VA is immediately usable. */
        if (Alloc->CpuVisible)
            Alloc->CpuAddress = Alloc->SystemMemory;
    }

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (Placed && Device != NULL && Device->OwnerProcess != NULL)
    {
        Status = DxgkpVidMmChargeInitialPlacement(
                     Adapter,
                     Device->OwnerProcess,
                     Alloc);
        if (!NT_SUCCESS(Status))
            goto FailMiniportAllocation;
    }
#endif

    DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    InsertTailList(&DxgkVidMmAllocationListHead, &Alloc->GlobalAllocationEntry);
    if (OutAllocation != NULL)
    {
        InterlockedIncrement(&Alloc->ReferenceCount);
        *OutAllocation = Alloc;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    *OutHandle = (HANDLE)(ULONG_PTR)Alloc->Handle;
    DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);

    DPRINT("DxgkVidMmCreateAllocation: handle=%p size=%Iu resident=%d\n",
           *OutHandle, Alloc->Size, Alloc->Resident);

    return STATUS_SUCCESS;

FailTrackedAllocation:
FailMiniportAllocation:
    if (MiniportAllocationCreated)
    {
        NTSTATUS RollbackStatus = DxgkpVidMmRollbackMiniportAllocation(Adapter, &AllocInfo->hAllocation, CreateArgs.hResource, CreateFlags.Resource && ResourceHandle == NULL, OutResourceHandle);

        if (!NT_SUCCESS(RollbackStatus))
        {
            NTSTATUS ActivationStatus;

            Alloc->MiniportHandle = AllocInfo->hAllocation;
            RollbackBatch->MiniportResourceHandle = CreateArgs.hResource;
            RollbackBatch->DestroyResource = CreateFlags.Resource && ResourceHandle == NULL;
            RollbackAllocations[0] = Alloc;
            ActivationStatus = DxgkpVidMmActivateUnpublishedDestroyBatch(RollbackBatch, RollbackAllocations, NULL, NULL, NULL, RollbackStatus, TRUE, TRUE);
            ASSERT(NT_SUCCESS(ActivationStatus));
            if (!NT_SUCCESS(ActivationStatus))
            {
                DPRINT1("DxgkVidMmCreateAllocation: could not quarantine uncertain rollback 0x%08lx\n", ActivationStatus);
                if (KmdTransactionStarted)
                    DxgkEndKmdTransaction(Adapter);
                return ActivationStatus;
            }
            DxgkpVidMmFreeDestroyBatch(RollbackBatch);
            if (KmdTransactionStarted)
                DxgkEndKmdTransaction(Adapter);
            return Status;
        }
        Alloc->MiniportHandle = NULL;
    }
    if (Alloc->Resident)
        DxgkpVidMmReleaseSegmentPlacement(Alloc);
    if (Alloc->SystemMemory != NULL)
        ExFreePoolWithTag(Alloc->SystemMemory, TAG_VIDMM_ALLOC);
    if (Alloc->PrivateDriverData != NULL)
        ExFreePoolWithTag(Alloc->PrivateDriverData, TAG_VIDMM_ALLOC);
    DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    Alloc->Magic = 0;
    ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    return Status;
}

NTSTATUS
DxgkVidMmCreateAllocation(
    _In_      PDXGKRNL_ADAPTER         Adapter,
    _In_opt_  PDXGKRNL_DEVICE          Device,
    _In_      DXGK_ALLOCATIONINFO     *AllocInfo,
    _In_opt_  CONST VOID              *CreatePrivateDriverData,
    _In_      UINT                     CreatePrivateDriverDataSize,
    _In_opt_  HANDLE                   ResourceHandle,
    _In_      DXGK_CREATEALLOCATIONFLAGS CreateFlags,
    _Out_     PHANDLE                  OutHandle,
    _Out_opt_ PHANDLE                  OutResourceHandle)
{
    return DxgkpVidMmCreateAllocationTracked(Adapter,
                                             Device,
                                             AllocInfo,
                                             CreatePrivateDriverData,
                                             CreatePrivateDriverDataSize,
                                             ResourceHandle,
                                             CreateFlags,
                                             OutHandle,
                                             OutResourceHandle,
                                             NULL);
}


/*
 * DxgkVidMmCreatePreMappedAllocation
 *
 * Creates a lightweight allocation backed by a pre-existing physical/virtual
 * mapping (e.g., the GOP framebuffer).  No miniport DxgkDdiCreateAllocation
 * is called — the allocation is CPU-visible from creation.
 */
NTSTATUS
DxgkVidMmCreatePreMappedAllocation(
    _In_  PDXGKRNL_ADAPTER   Adapter,
    _In_  PHYSICAL_ADDRESS    PhysicalAddress,
    _In_  PVOID               VirtualAddress,
    _In_  SIZE_T              Size,
    _Out_ HANDLE             *OutHandle)
{
    PDXGKVMM_ALLOCATION Alloc;

    if (Adapter == NULL || VirtualAddress == NULL ||
        Size == 0 || OutHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutHandle = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();

    Alloc = (PDXGKVMM_ALLOCATION)ExAllocatePoolWithTag(
                NonPagedPool, sizeof(*Alloc), TAG_VIDMM_ALLOC);
    if (Alloc == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Alloc, sizeof(*Alloc));
    DxgkpVidMmInitializeAllocationLifetime(Alloc);
    KeInitializeMutex(&Alloc->ResidencyLock, 0);

    Alloc->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle,
                                              DxgkVidMmAllocationHandleCookie);
    Alloc->Magic           = DXGKVMM_ALLOCATION_MAGIC;
    Alloc->Adapter         = Adapter;
    Alloc->Device          = NULL;
    Alloc->Resource        = NULL;
    Alloc->Size            = Size;
    Alloc->Alignment       = PAGE_SIZE;
    Alloc->CpuVisible      = TRUE;
    Alloc->Resident        = FALSE;
    Alloc->PhysicalAddress = PhysicalAddress;
    Alloc->SegmentId       = 0;
    Alloc->SegmentOffset   = 0;
    Alloc->SystemMemory    = NULL;  /* NOT pool-allocated; don't free */
    Alloc->CpuAddress      = VirtualAddress;
    Alloc->MiniportHandle  = NULL;
    Alloc->ApertureMdl     = NULL;
    Alloc->ApertureMapped  = FALSE;
    KeInitializeMutex(&Alloc->UserModeLock, 0);

    InitializeListHead(&Alloc->SegmentEntry);
    InitializeListHead(&Alloc->DeviceEntry);
    InitializeListHead(&Alloc->GlobalAllocationEntry);
    InitializeListHead(&Alloc->ResourceEntry);

    DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    InsertTailList(&DxgkVidMmAllocationListHead, &Alloc->GlobalAllocationEntry);
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    *OutHandle = (HANDLE)(ULONG_PTR)Alloc->Handle;

    DPRINT("DxgkVidMmCreatePreMappedAllocation: handle=%p VA=%p PA=0x%I64x "
           "Size=%Iu\n",
           *OutHandle, VirtualAddress, PhysicalAddress.QuadPart, Size);

    return STATUS_SUCCESS;
}


/*
 * DxgkVidMmDestroyAllocation
 *
 * Validates the handle, evicts the allocation if resident, frees system
 * backing memory, notifies the miniport, and frees the tracking object.
 */

static NTSTATUS
DxgkpVidMmDestroyAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _In_opt_ PDXGKVMM_RESOURCE ExpectedResource,
    _In_ HANDLE AllocationHandle)
{
    PDXGKVMM_ALLOCATION Allocation;
    PDXGKRNL_DEVICE Device = NULL;
    D3DKMT_HANDLE Handle = (D3DKMT_HANDLE)(ULONG_PTR)AllocationHandle;
    NTSTATUS Status;

    DPRINT("DxgkVidMmDestroyAllocation: Adapter=%p Handle=%p\n", Adapter, AllocationHandle);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    Allocation = DxgkpVidMmLookupAllocationLocked(Handle);
    if (Allocation != NULL && Allocation->Adapter == Adapter && (ExpectedDevice == NULL || Allocation->Device == ExpectedDevice) && (ExpectedResource == NULL ? Allocation->Resource == NULL : Allocation->Resource == ExpectedResource))
        Device = ExpectedDevice != NULL ? Allocation->Device : NULL;
    else
        Allocation = NULL;
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    Status = Allocation != NULL ? DxgkpVidMmDestroyAllocationList(Adapter, Device, &Handle, 1, FALSE) : STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(Status))
        DPRINT1("DxgkVidMmDestroyAllocation: invalid handle %p\n", AllocationHandle);
    return Status;
}

static VOID
DxgkpVidMmFreeDestroyBatch(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    LONG References = InterlockedDecrement(&Batch->LifetimeReferenceCount);

    ASSERT(References >= 0);
    if (References != 0)
        return;
    ASSERT(InterlockedCompareExchange(&Batch->Listed, 0, 0) == 0);
    ASSERT(InterlockedCompareExchange(&Batch->ActiveReferenceHeld, 0, 0) == 0);
    if (Batch->MiniportHandles != NULL)
        ExFreePoolWithTag(Batch->MiniportHandles, TAG_VIDMM_ALLOC);
    if (Batch->OpenBindingHandles != NULL)
        ExFreePoolWithTag(Batch->OpenBindingHandles, TAG_VIDMM_ALLOC);
    if (Batch->Allocations != NULL)
        ExFreePoolWithTag(Batch->Allocations, TAG_VIDMM_ALLOC);
    ExFreePoolWithTag(Batch, TAG_VIDMM_ALLOC);
}

static PDXGKVMM_DESTROY_BATCH
DxgkpVidMmAllocateDestroyBatch(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ UINT AllocationCount)
{
    PDXGKVMM_DESTROY_BATCH Batch;

    if (Adapter == NULL || AllocationCount == 0 || AllocationCount > MAXLONG)
        return NULL;
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Batch->Allocations) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Batch->OpenBindingHandles) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Batch->MiniportHandles))
        return NULL;
    Batch = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Batch), TAG_VIDMM_ALLOC);
    if (Batch == NULL)
        return NULL;
    RtlZeroMemory(Batch, sizeof(*Batch));
    Batch->LifetimeReferenceCount = 1;
    Batch->Allocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Batch->Allocations), TAG_VIDMM_ALLOC);
    Batch->OpenBindingHandles = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Batch->OpenBindingHandles), TAG_VIDMM_ALLOC);
    Batch->MiniportHandles = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Batch->MiniportHandles), TAG_VIDMM_ALLOC);
    if (Batch->Allocations == NULL || Batch->OpenBindingHandles == NULL || Batch->MiniportHandles == NULL)
    {
        DxgkpVidMmFreeDestroyBatch(Batch);
        return NULL;
    }
    RtlZeroMemory(Batch->Allocations, (SIZE_T)AllocationCount * sizeof(*Batch->Allocations));
    RtlZeroMemory(Batch->OpenBindingHandles, (SIZE_T)AllocationCount * sizeof(*Batch->OpenBindingHandles));
    RtlZeroMemory(Batch->MiniportHandles, (SIZE_T)AllocationCount * sizeof(*Batch->MiniportHandles));
    Batch->Adapter = Adapter;
    Batch->AllocationCount = AllocationCount;
    Batch->PendingAllocationCount = (LONG)AllocationCount;
    Batch->CompletionStatus = STATUS_PENDING;
    KeInitializeEvent(&Batch->WorkerIdleEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Batch->CompletionEvent, NotificationEvent, FALSE);
    InitializeListHead(&Batch->QuarantineEntry);
    ExInitializeWorkItem(&Batch->WorkItem, DxgkpVidMmDestroyBatchWorker, Batch);
    return Batch;
}

static VOID
DxgkpVidMmPoisonDestroyBatchResource(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    if (Batch->Resource != NULL && InterlockedCompareExchange(&Batch->ResourcePoisonHeld, 1, 0) == 0)
        InterlockedIncrement(&Batch->Resource->CloseUncertain);
}

static VOID
DxgkpVidMmReleaseDestroyBatchResourcePoison(
    _In_ PDXGKVMM_DESTROY_BATCH Batch,
    _In_ PDXGKVMM_RESOURCE Resource)
{
    LONG PoisonCount;

    if (Resource == NULL || InterlockedCompareExchange(&Batch->ResourcePoisonHeld, 0, 1) != 1)
        return;
    PoisonCount = InterlockedDecrement(&Resource->CloseUncertain);
    ASSERT(PoisonCount >= 0);
}

static VOID
DxgkpVidMmRegisterDestroyBatch(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    if (InterlockedCompareExchange(&Batch->Listed, 1, 0) == 0)
    {
        InterlockedIncrement(&Batch->LifetimeReferenceCount);
        InterlockedExchange(&Batch->ActiveReferenceHeld, 1);
        InsertTailList(&DxgkVidMmDestroyBatchListHead, &Batch->QuarantineEntry);
    }
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
}

static VOID
DxgkpVidMmRetireDestroyWorkerCount(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    BOOLEAN Retired;
    BOOLEAN SetDrainedEvent;

    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    Retired = DxgkVidMmWorkerDrainCoreRetireLocked(&Adapter->VidMmDestroyWorkerCount, &SetDrainedEvent);
    ASSERT(Retired);
    if (Retired && SetDrainedEvent)
        KeSetEvent(&Adapter->VidMmDestroyWorkersDrainedEvent, IO_NO_INCREMENT, FALSE);
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
}

static VOID
DxgkpVidMmRetireFailedDestroyBatchWorker(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    PDXGKRNL_ADAPTER Adapter = Batch->Adapter;
    BOOLEAN Retired;
    BOOLEAN SetDrainedEvent;
    BOOLEAN WorkerCounted;

    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    WorkerCounted = InterlockedExchange(&Batch->WorkerCounted, 0) != 0;
    if (WorkerCounted)
    {
        Retired = DxgkVidMmWorkerDrainCoreRetireLocked(&Adapter->VidMmDestroyWorkerCount, &SetDrainedEvent);
        ASSERT(Retired);
        if (Retired && SetDrainedEvent)
            KeSetEvent(&Adapter->VidMmDestroyWorkersDrainedEvent, IO_NO_INCREMENT, FALSE);
    }
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
}

static BOOLEAN
DxgkpVidMmReleaseActiveDestroyBatch(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    BOOLEAN ReleaseReference;
    BOOLEAN WorkerCounted;

    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    if (InterlockedCompareExchange(&Batch->Listed, 0, 1) == 1)
    {
        RemoveEntryList(&Batch->QuarantineEntry);
        InitializeListHead(&Batch->QuarantineEntry);
    }
    ReleaseReference = InterlockedExchange(&Batch->ActiveReferenceHeld, 0) != 0;
    WorkerCounted = InterlockedExchange(&Batch->WorkerCounted, 0) != 0;
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
    if (ReleaseReference)
        DxgkpVidMmFreeDestroyBatch(Batch);
    return WorkerCounted;
}

static VOID
DxgkpVidMmQuarantineDestroyBatch(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    DxgkpVidMmEnsureGlobalsInitialized();
    InterlockedExchange(&Batch->Quarantined, 1);
}
static BOOLEAN
DxgkpVidMmTryCommitDestroyBatch(
    _In_ PDXGKVMM_DESTROY_BATCH Batch)
{
    PDXGKVMM_RESOURCE Resource = Batch->Resource;
    UINT Index;
    BOOLEAN Ready = TRUE;
    BOOLEAN CommitOwner = FALSE;

    if (InterlockedCompareExchange(&Batch->DestroyCommitted, 0, 0) != 0)
        return TRUE;
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < Batch->AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Batch->Allocations[Index];

        if (Allocation->DestroyBatch != Batch || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0 || Allocation->OpenBindingHandle != NULL)
        {
            Ready = FALSE;
            break;
        }
    }
    if (Ready && InterlockedCompareExchange(&Batch->DestroyCommitted, 1, 0) == 0)
    {
        CommitOwner = TRUE;
        RtlZeroMemory(Batch->MiniportHandles, (SIZE_T)Batch->AllocationCount * sizeof(*Batch->MiniportHandles));
        for (Index = 0; Index < Batch->AllocationCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Batch->Allocations[Index];

            if (!IsListEmpty(&Allocation->GlobalAllocationEntry))
            {
                RemoveEntryList(&Allocation->GlobalAllocationEntry);
                InitializeListHead(&Allocation->GlobalAllocationEntry);
            }
            if (Resource != NULL && !IsListEmpty(&Allocation->ResourceEntry))
            {
                ASSERT(Resource->AllocationCount != 0);
                RemoveEntryList(&Allocation->ResourceEntry);
                InitializeListHead(&Allocation->ResourceEntry);
                Resource->AllocationCount--;
            }
            if (Allocation->BackingAllocation == NULL && Allocation->MiniportHandle != NULL)
                Batch->MiniportHandles[Batch->MiniportHandleCount++] = Allocation->MiniportHandle;
            if (Resource == NULL && Allocation->MiniportResourceHandle != NULL)
            {
                ASSERT(Batch->MiniportResourceHandle == NULL || Batch->MiniportResourceHandle == Allocation->MiniportResourceHandle);
                Batch->MiniportResourceHandle = Allocation->MiniportResourceHandle;
                Batch->DestroyResource = Batch->DestroyResource || Allocation->DestroyMiniportResource;
            }
        }
        if (Batch->DestroyResourceWrapper)
        {
            ASSERT(Resource != NULL);
            ASSERT(Resource->AllocationCount == 0);
            ASSERT(IsListEmpty(&Resource->AllocationList));
            ASSERT(!IsListEmpty(&Resource->GlobalResourceEntry));
            InterlockedExchange(&Resource->Destroying, 1);
            RemoveEntryList(&Resource->GlobalResourceEntry);
            InitializeListHead(&Resource->GlobalResourceEntry);
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    if (CommitOwner)
    {
        for (Index = 0; Index < Batch->AllocationCount; ++Index)
            DxgkpVidMmDropAllocationHandleReference(Batch->Allocations[Index]);
    }
    return Ready;
}

static NTSTATUS
DxgkpVidMmCloseReadyBindingGroup(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_OPEN_BINDING_GROUP Group)
{
    DXGKARG_CLOSEALLOCATION CloseArgs;
    UINT Index;
    UINT OtherIndex;
    NTSTATUS Status = STATUS_PENDING;

    (VOID)KeWaitForSingleObject(&Group->OperationLock, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Group->CloseState, 0, 0) == 2)
    {
        KeReleaseMutex(&Group->OperationLock, FALSE);
        return STATUS_SUCCESS;
    }
    if (InterlockedCompareExchange(&Group->CloseState, 0, 0) == 3)
    {
        Status = Group->CloseStatus;
        KeReleaseMutex(&Group->OperationLock, FALSE);
        return Status;
    }
    Group->DestroyingCount = 0;
    for (Index = 0; Index < Group->AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Group->Allocations[Index];

        if (Allocation == NULL || Allocation->OpenBindingGroup != Group || Group->OpenHandles[Index] == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
            Group->DestroyingCount++;
    }
    if (!NT_SUCCESS(Status) || Group->DestroyingCount != Group->AllocationCount)
    {
        KeReleaseMutex(&Group->OperationLock, FALSE);
        return Status;
    }
    for (Index = 0; Index < Group->AllocationCount; ++Index)
        KeWaitForSingleObject(&Group->Allocations[Index]->LogicalReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0)
        Status = STATUS_DEVICE_NOT_READY;
    else if (DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL)
        Status = STATUS_NOT_SUPPORTED;
    else if (!DxgkAcquireMiniportCallback(Adapter))
        Status = STATUS_DEVICE_NOT_READY;
    else
    {
        RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
        CloseArgs.NumAllocations = Group->AllocationCount;
        CloseArgs.pOpenHandleList = Group->OpenHandles;
        Status = DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(Group->MiniportDeviceHandle, &CloseArgs);
        DxgkReleaseMiniportCallback(Adapter);
    }
    Group->CloseStatus = Status;
    if (NT_SUCCESS(Status))
    {
        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        for (Index = 0; Index < Group->AllocationCount; ++Index)
            Group->Allocations[Index]->OpenBindingHandle = NULL;
        Group->DestroyBatchCount = 0;
        for (Index = 0; Index < Group->AllocationCount; ++Index)
        {
            PDXGKVMM_DESTROY_BATCH Batch = Group->Allocations[Index]->DestroyBatch;

            if (Batch == NULL)
                continue;
            for (OtherIndex = 0; OtherIndex < Group->DestroyBatchCount && Group->DestroyBatches[OtherIndex] != Batch; ++OtherIndex)
                NOTHING;
            if (OtherIndex == Group->DestroyBatchCount)
            {
                LONG References = InterlockedIncrement(&Batch->LifetimeReferenceCount);

                ASSERT(References > 1);
                Group->DestroyBatches[Group->DestroyBatchCount++] = Batch;
            }
        }
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        InterlockedExchange(&Group->CloseState, 2);
        DxgkpVidMmReferenceOpenBindingGroup(Group);
    }
    else
        InterlockedExchange(&Group->CloseState, 3);
    KeReleaseMutex(&Group->OperationLock, FALSE);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Group->DestroyBatchCount; ++Index)
        {
            PDXGKVMM_DESTROY_BATCH Batch = Group->DestroyBatches[Index];

            DxgkpVidMmTryCommitDestroyBatch(Batch);
            Group->DestroyBatches[Index] = NULL;
            DxgkpVidMmFreeDestroyBatch(Batch);
        }
        Group->DestroyBatchCount = 0;
        DxgkpVidMmDereferenceOpenBindingGroup(Group);
    }
    return Status;
}
static VOID
NTAPI
DxgkpVidMmDestroyBatchWorker(
    _In_ PVOID Context)
{
    PDXGKVMM_DESTROY_BATCH Batch = Context;
    PDXGKRNL_ADAPTER Adapter = Batch->Adapter;
    PDXGKVMM_RESOURCE Resource = Batch->Resource;
    DXGKARG_DESTROYALLOCATION DestroyArgs;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT Index;
    BOOLEAN WorkerCounted;
    BOOLEAN CompletionWaiter;
    BOOLEAN AdmissionRejected = FALSE;
    BOOLEAN KmdAdmissionReserved;

    PAGED_CODE();
    KmdAdmissionReserved = InterlockedExchange(&Batch->KmdAdmissionReserved, 0) != 0;
    ASSERT(InterlockedCompareExchange(&Batch->PendingAllocationCount, 0, 0) == 0);
    KeResetEvent(&Batch->WorkerIdleEvent);
    if (Resource != NULL)
        (VOID)KeWaitForSingleObject(&Resource->MiniportResourceLock, Executive, KernelMode, FALSE, NULL);
    if (!Batch->ForceLocalRelease && (Batch->MiniportHandleCount != 0 || Batch->DestroyResource))
    {
        if (DXGK_CB_FULL(Adapter, DxgkDdiDestroyAllocation) == NULL)
        {
            Status = STATUS_NOT_SUPPORTED;
            if (KmdAdmissionReserved)
            {
                DxgkReleaseKmdCall(Adapter);
                KmdAdmissionReserved = FALSE;
            }
        }
        else if (!(KmdAdmissionReserved ? DxgkAcquireMiniportCallbackFromReservedKmdCall(Adapter) : DxgkAcquireMiniportCallback(Adapter)))
        {
            Status = STATUS_DEVICE_NOT_READY;
            AdmissionRejected = TRUE;
            KmdAdmissionReserved = FALSE;
        }
        else
        {
            KmdAdmissionReserved = FALSE;
            RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
            DestroyArgs.NumAllocations = Batch->MiniportHandleCount;
            DestroyArgs.pAllocationList = Batch->MiniportHandleCount != 0 ? Batch->MiniportHandles : NULL;
            DestroyArgs.hResource = Resource != NULL ? Resource->MiniportHandle : Batch->MiniportResourceHandle;
            DestroyArgs.Flags.DestroyResource = Batch->DestroyResource;
            Status = DXGK_CB_FULL(Adapter, DxgkDdiDestroyAllocation)(Adapter->MiniportDeviceContext, &DestroyArgs);
            if (NT_SUCCESS(Status) && Resource != NULL)
                Resource->MiniportHandle = Batch->DestroyResource ? NULL : DestroyArgs.hResource;
            else if (NT_SUCCESS(Status))
                Batch->MiniportResourceHandle = DestroyArgs.hResource;
            DxgkReleaseMiniportCallback(Adapter);
        }
    }
    else if (KmdAdmissionReserved)
        DxgkReleaseKmdCall(Adapter);
    if (Resource != NULL)
        KeReleaseMutex(&Resource->MiniportResourceLock, FALSE);
    if (!NT_SUCCESS(Status))
    {
        if (AdmissionRejected)
        {
            BOOLEAN RetryAdmission = Adapter->State == DxgkAdapterStateStarted && InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) != 0 && InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) == 0 && InterlockedCompareExchange(&Adapter->RemoveRundownStarted, 0, 0) == 0;

            DPRINT("DxgkDestroyAllocation: DestroyAllocation batch admission deferred with 0x%08lx\n", Status);
            Batch->CompletionStatus = Status;
            InterlockedExchange(&Batch->WorkQueued, 0);
            InterlockedExchange(&Batch->AdmissionDeferred, RetryAdmission ? 1 : 0);
            if (!RetryAdmission)
                Batch->AwaitingStopBoundary = TRUE;
            DxgkpVidMmQuarantineDestroyBatch(Batch);
            KeSetEvent(&Batch->WorkerIdleEvent, IO_NO_INCREMENT, FALSE);
            CompletionWaiter = InterlockedCompareExchange(&Batch->CompletionWaiter, 0, 0) != 0;
            if (CompletionWaiter)
                KeSetEvent(&Batch->CompletionEvent, IO_NO_INCREMENT, FALSE);
            DxgkpVidMmRetireFailedDestroyBatchWorker(Batch);
            return;
        }
        DPRINT1("DxgkDestroyAllocation: DestroyAllocation batch failed with 0x%08lx; retaining tombstones\n", Status);
        DxgkpVidMmPoisonDestroyBatchResource(Batch);
        Batch->CompletionStatus = Status;
        InterlockedExchange(&Batch->WorkQueued, 0);
        DxgkpVidMmQuarantineDestroyBatch(Batch);
        KeSetEvent(&Batch->WorkerIdleEvent, IO_NO_INCREMENT, FALSE);
        CompletionWaiter = InterlockedCompareExchange(&Batch->CompletionWaiter, 0, 0) != 0;
        if (CompletionWaiter)
            KeSetEvent(&Batch->CompletionEvent, IO_NO_INCREMENT, FALSE);
        DxgkpVidMmRetireFailedDestroyBatchWorker(Batch);
        return;
    }
    for (Index = 0; Index < Batch->AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Batch->Allocations[Index];

        ASSERT(InterlockedCompareExchange(&Allocation->ReferenceCount, 0, 0) == 0);
        ASSERT(InterlockedCompareExchange(&Allocation->LogicalReferenceCount, 0, 0) == 0);
        if (Allocation->BackingAllocation == NULL)
        {
            Allocation->MiniportHandle = NULL;
            Allocation->MiniportResourceHandle = NULL;
            Allocation->DestroyMiniportResource = FALSE;
        }
        Allocation->DestroyBatch = NULL;
        InterlockedExchange(&Allocation->FinalizeQueued, 1);
        DxgkpVidMmFinalizeAllocation(Allocation);
    }
    if (Resource != NULL)
    {
        DxgkpVidMmReleaseDestroyBatchResourcePoison(Batch, Resource);
        if (InterlockedExchange(&Batch->ResourceHandleReferenceOwned, 0) != 0)
            DxgkVidMmDereferenceResource(Resource);
        DxgkVidMmDereferenceResource(Resource);
        Batch->Resource = NULL;
    }
    Batch->CompletionStatus = STATUS_SUCCESS;
    CompletionWaiter = InterlockedCompareExchange(&Batch->CompletionWaiter, 0, 0) != 0;
    if (CompletionWaiter)
        KeSetEvent(&Batch->CompletionEvent, IO_NO_INCREMENT, FALSE);
    WorkerCounted = DxgkpVidMmReleaseActiveDestroyBatch(Batch);
    if (WorkerCounted)
        DxgkpVidMmRetireDestroyWorkerCount(Adapter);
}

static ULONG
DxgkpVidMmForceQuarantinedDestroyBatches(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY ForcedBatches;
    PLIST_ENTRY Entry;
    ULONG ProcessedCount = 0;
    BOOLEAN MadeProgress;

    InitializeListHead(&ForcedBatches);
    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    Entry = DxgkVidMmDestroyBatchListHead.Flink;
    while (Entry != &DxgkVidMmDestroyBatchListHead)
    {
        PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);

        Entry = Entry->Flink;
        if (Batch->Adapter == Adapter && InterlockedCompareExchange(&Batch->CompletionWaiter, 0, 0) == 0 && InterlockedCompareExchange(&Batch->WorkerCounted, 0, 0) == 0 && InterlockedCompareExchange(&Batch->WorkQueued, 0, 0) == 0)
        {
            RemoveEntryList(&Batch->QuarantineEntry);
            InsertTailList(&ForcedBatches, &Batch->QuarantineEntry);
            InterlockedExchange(&Batch->Listed, 0);
            InterlockedExchange(&Batch->Quarantined, 0);
        }
    }
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);

    for (Entry = ForcedBatches.Flink; Entry != &ForcedBatches; Entry = Entry->Flink)
    {
        PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);
        UINT Index;

        for (Index = 0; Index < Batch->AllocationCount; ++Index)
        {
            PDXGKVMM_OPEN_BINDING_GROUP Group = Batch->Allocations[Index]->OpenBindingGroup;
            UINT GroupIndex;
            BOOLEAN AllDestroying = TRUE;

            if (Group == NULL)
                continue;
            (VOID)KeWaitForSingleObject(&Group->OperationLock, Executive, KernelMode, FALSE, NULL);
            for (GroupIndex = 0; GroupIndex < Group->AllocationCount; ++GroupIndex)
            {
                if (Group->Allocations[GroupIndex] != NULL && InterlockedCompareExchange(&Group->Allocations[GroupIndex]->Destroying, 0, 0) == 0)
                {
                    AllDestroying = FALSE;
                    break;
                }
            }
            if (AllDestroying)
            {
                for (GroupIndex = 0; GroupIndex < Group->AllocationCount; ++GroupIndex)
                {
                    if (Group->Allocations[GroupIndex] != NULL)
                        Group->Allocations[GroupIndex]->OpenBindingHandle = NULL;
                }
                Group->CloseStatus = STATUS_SUCCESS;
                InterlockedExchange(&Group->CloseState, 2);
            }
            KeReleaseMutex(&Group->OperationLock, FALSE);
        }
    }

    for (Entry = ForcedBatches.Flink; Entry != &ForcedBatches; Entry = Entry->Flink)
    {
        PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);
        UINT Index;

        /*
         * A GPUVA flush failure deliberately retains the logical handle
         * references so its backing cannot be recycled while stale hardware
         * translations may exist.  This force path runs at the adapter-stop
         * boundary; dropping the references here is therefore the first safe
         * point at which those tombstones may proceed to local finalization.
         * The drop is idempotent for all other quarantined batches.
         */
        for (Index = 0; Index < Batch->AllocationCount; ++Index)
            DxgkpVidMmDropLogicalHandleReference(Batch->Allocations[Index]);
        for (Index = 0; Index < Batch->AllocationCount; ++Index)
            KeWaitForSingleObject(&Batch->Allocations[Index]->LogicalReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
        if (!DxgkpVidMmTryCommitDestroyBatch(Batch))
            continue;
        Batch->ForceLocalRelease = TRUE;
        Batch->AwaitingStopBoundary = FALSE;
        for (Index = 0; Index < Batch->AllocationCount; ++Index)
        {
            if (Batch->Allocations[Index]->BackingAllocation == NULL)
                Batch->Allocations[Index]->MiniportHandle = NULL;
        }
    }

    do
    {
        MadeProgress = FALSE;
        for (Entry = ForcedBatches.Flink; Entry != &ForcedBatches; Entry = Entry->Flink)
        {
            PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);
            UINT Index;
            BOOLEAN Ready = InterlockedCompareExchange(&Batch->DestroyCommitted, 0, 0) != 0 && InterlockedCompareExchange(&Batch->PendingAllocationCount, 0, 0) == 0;

            for (Index = 0; Ready && Index < Batch->AllocationCount; ++Index)
                Ready = InterlockedCompareExchange(&Batch->Allocations[Index]->ReferenceCount, 0, 0) == 0;
            if (!Ready)
                continue;
            RemoveEntryList(&Batch->QuarantineEntry);
            InitializeListHead(&Batch->QuarantineEntry);
            InterlockedExchange(&Batch->WorkQueued, 1);
            InterlockedExchange(&Batch->CompletionWaiter, 0);
            DxgkpVidMmDestroyBatchWorker(Batch);
            ProcessedCount++;
            MadeProgress = TRUE;
            break;
        }
    } while (MadeProgress);

    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    while (!IsListEmpty(&ForcedBatches))
    {
        PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(RemoveHeadList(&ForcedBatches), DXGKVMM_DESTROY_BATCH, QuarantineEntry);

        InsertTailList(&DxgkVidMmDestroyBatchListHead, &Batch->QuarantineEntry);
        InterlockedExchange(&Batch->Listed, 1);
        InterlockedExchange(&Batch->Quarantined, 1);
    }
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
    return ProcessedCount;
}

static NTSTATUS
DxgkpVidMmDestroyAllocationList(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_reads_(AllocationCount) CONST D3DKMT_HANDLE *AllocationHandles,
    _In_ UINT AllocationCount,
    _In_ BOOLEAN ResourceOperationLockHeld)
{
    PDXGKVMM_DESTROY_BATCH Batch = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    UINT Index;
    UINT OtherIndex;
    BOOLEAN ResourceOperationLockAcquired = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    DxgkpVidMmEnsureGlobalsInitialized();
    if (Adapter == NULL || (Device != NULL && Device->Adapter != Adapter) || AllocationHandles == NULL || AllocationCount == 0 || AllocationCount > MAXLONG)
        return STATUS_INVALID_PARAMETER;
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Batch->Allocations) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Batch->OpenBindingHandles) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Batch->MiniportHandles))
        return STATUS_INTEGER_OVERFLOW;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (AllocationHandles[Index] == 0)
            return STATUS_INVALID_PARAMETER;
        for (OtherIndex = 0; OtherIndex < Index; ++OtherIndex)
        {
            if (AllocationHandles[OtherIndex] == AllocationHandles[Index])
                return STATUS_INVALID_PARAMETER;
        }
    }

    Batch = DxgkpVidMmAllocateDestroyBatch(Adapter, AllocationCount);
    if (Batch == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = DxgkpVidMmLookupAllocationLocked(AllocationHandles[Index]);

        if (Allocation == NULL || Allocation->Adapter != Adapter || (Device != NULL && Allocation->Device != Device) || Allocation->Initializing || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) != 0 || InterlockedCompareExchange(&Allocation->HandleReferenceDropped, 0, 0) != 0 || InterlockedCompareExchange(&Allocation->LogicalHandleReferenceDropped, 0, 0) != 0 || Allocation->DestroyBatch != NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (Index == 0)
            Resource = Allocation->Resource;
        else if (Allocation->Resource != Resource)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        Batch->Allocations[Index] = Allocation;
        if (Batch->MiniportDeviceHandle == NULL)
            Batch->MiniportDeviceHandle = Allocation->MiniportDeviceHandle;
    }
    if (NT_SUCCESS(Status) && Resource != NULL && (Resource->Adapter != Adapter || (Device != NULL && Resource->Device != Device) || InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || Resource->AllocationCount < AllocationCount))
        Status = STATUS_INVALID_PARAMETER;
    for (Index = 0; NT_SUCCESS(Status) && Index < AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Batch->Allocations[Index];
        PDXGKVMM_OPEN_BINDING_GROUP Group = Allocation->OpenBindingGroup;

        if ((Allocation->OpenBindingHandle == NULL) != (Group == NULL))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (Group == NULL)
            continue;
        if (Group->AllocationCount == 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
    }
    if (NT_SUCCESS(Status) && Resource != NULL)
    {
        InterlockedIncrement(&Resource->ReferenceCount);
        Batch->Resource = Resource;
        if (ResourceOperationLockHeld)
        {
            Batch->DestroyResourceWrapper = TRUE;
            Batch->DestroyResource = Resource->BackingResource == NULL;
            InterlockedExchange(&Batch->ResourceHandleReferenceOwned, 1);
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    if (!NT_SUCCESS(Status))
    {
        DxgkpVidMmFreeDestroyBatch(Batch);
        return Status;
    }

    if (Resource != NULL && !ResourceOperationLockHeld)
    {
        (VOID)KeWaitForSingleObject(&Resource->ResourceOperationLock, Executive, KernelMode, FALSE, NULL);
        ResourceOperationLockAcquired = TRUE;
    }
    if (Resource != NULL && (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) != 0 || InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) != 0))
    {
        Status = STATUS_DEVICE_BUSY;
        if (ResourceOperationLockAcquired)
            KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
        if (Batch->Resource != NULL)
            DxgkVidMmDereferenceResource(Batch->Resource);
        DxgkpVidMmFreeDestroyBatch(Batch);
        return Status;
    }
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = DxgkpVidMmLookupAllocationLocked(AllocationHandles[Index]);

        if (Allocation != Batch->Allocations[Index] || Allocation->Adapter != Adapter || (Device != NULL && Allocation->Device != Device) || Allocation->Resource != Resource || Allocation->Initializing || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) != 0 || InterlockedCompareExchange(&Allocation->HandleReferenceDropped, 0, 0) != 0 || InterlockedCompareExchange(&Allocation->LogicalHandleReferenceDropped, 0, 0) != 0 || Allocation->DestroyBatch != NULL || IsListEmpty(&Allocation->GlobalAllocationEntry) || (Resource != NULL && IsListEmpty(&Allocation->ResourceEntry)))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
    }
    if (NT_SUCCESS(Status) && Resource != NULL && (Resource->Adapter != Adapter || (Device != NULL && Resource->Device != Device) || InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || Resource->AllocationCount < AllocationCount))
        Status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(Status))
    {
        DxgkpVidMmRegisterDestroyBatch(Batch);
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Batch->Allocations[Index];

            Allocation->DestroyBatch = Batch;
            InterlockedExchange(&Allocation->FinalizeQueued, 2);
            InterlockedExchange(&Allocation->Destroying, 1);
            Batch->OpenBindingHandles[Index] = Allocation->OpenBindingHandle;

            /* Unpublish now: a destroyed handle must stop being visible even
             * while its miniport close/free defers until the whole open
             * binding group can close.  Snapshot/query would otherwise retry
             * against the pending member forever.  The commit-time removal
             * in TryCommitDestroyBatch is already idempotent. */
            if (!IsListEmpty(&Allocation->GlobalAllocationEntry))
            {
                RemoveEntryList(&Allocation->GlobalAllocationEntry);
                InitializeListHead(&Allocation->GlobalAllocationEntry);
            }
            if (Resource != NULL && !IsListEmpty(&Allocation->ResourceEntry))
            {
                ASSERT(Resource->AllocationCount != 0);
                RemoveEntryList(&Allocation->ResourceEntry);
                InitializeListHead(&Allocation->ResourceEntry);
                Resource->AllocationCount--;
            }
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    if (!NT_SUCCESS(Status))
    {
        if (ResourceOperationLockAcquired)
            KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
        if (Batch->Resource != NULL)
            DxgkVidMmDereferenceResource(Batch->Resource);
        DxgkpVidMmFreeDestroyBatch(Batch);
        return Status;
    }

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /*
     * Clear every GPUVA binding before dropping the logical handle references.
     * GpuVaInvalidateAllocation batches page-table notifications while it owns
     * GpuVaLock; flush each distinct process after the full batch is invalidated
     * and before any allocation backing can reach finalization.  A failed
     * hardware invalidation leaves the batch as registered tombstones with its
     * logical handle references intact.  Only the adapter-stop force path may
     * release those references, after hardware access has ceased.
     */
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        PDXGKRNL_PROCESS Process = Batch->Allocations[Index]->Device != NULL ? Batch->Allocations[Index]->Device->ProcessRecord : NULL;

        if (Process != NULL)
            DxgkGpuVaInvalidateAllocation(Adapter, Process, Batch->Allocations[Index]);
    }
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        PDXGKRNL_PROCESS Process = Batch->Allocations[Index]->Device != NULL ? Batch->Allocations[Index]->Device->ProcessRecord : NULL;

        if (Process != NULL)
        {
            for (OtherIndex = 0; OtherIndex < Index; ++OtherIndex)
            {
                PDXGKRNL_DEVICE OtherDevice = Batch->Allocations[OtherIndex]->Device;

                if (OtherDevice != NULL && OtherDevice->ProcessRecord == Process)
                    break;
            }
            if (OtherIndex == Index)
            {
                Status = DxgkGpuVaFlushPageTableUpdates(Process);
                if (!NT_SUCCESS(Status))
                    break;
            }
        }
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxgkDestroyAllocation: GPUVA invalidation failed with 0x%08lx; retaining backing until adapter stop\n", Status);
        Batch->CompletionStatus = Status;
        DxgkpVidMmPoisonDestroyBatchResource(Batch);
        InterlockedExchange(&Batch->CompletionWaiter, 0);
        Batch->AwaitingStopBoundary = TRUE;
        DxgkpVidMmQuarantineDestroyBatch(Batch);
        if (ResourceOperationLockAcquired)
        {
            KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
            ResourceOperationLockAcquired = FALSE;
        }
        DxgkpVidMmFreeDestroyBatch(Batch);
        return Status;
    }
#endif
    for (Index = 0; Index < AllocationCount; ++Index)
        DxgkpVidMmDropLogicalHandleReference(Batch->Allocations[Index]);
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        KeWaitForSingleObject(&Batch->Allocations[Index]->LogicalReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
    }
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        PDXGKVMM_OPEN_BINDING_GROUP Group = Batch->Allocations[Index]->OpenBindingGroup;

        if (Group == NULL)
            continue;
        for (OtherIndex = 0; OtherIndex < Index && Batch->Allocations[OtherIndex]->OpenBindingGroup != Group; ++OtherIndex)
            NOTHING;
        if (OtherIndex != Index)
            continue;
        Status = DxgkpVidMmCloseReadyBindingGroup(Adapter, Group);
        if (Status == STATUS_PENDING)
        {
            Status = STATUS_SUCCESS;
            continue;
        }
        if (!NT_SUCCESS(Status))
            break;
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxgkDestroyAllocation: group CloseAllocation failed with 0x%08lx; quarantining uncertain bindings\n", Status);
        DxgkpVidMmPoisonDestroyBatchResource(Batch);
        InterlockedExchange(&Batch->CompletionWaiter, 0);
        DxgkpVidMmQuarantineDestroyBatch(Batch);
        if (ResourceOperationLockAcquired)
            KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
        DxgkpVidMmFreeDestroyBatch(Batch);
        return Status;
    }

    DxgkpVidMmTryCommitDestroyBatch(Batch);
    if (ResourceOperationLockAcquired)
    {
        KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
        ResourceOperationLockAcquired = FALSE;
    }
    if (InterlockedCompareExchange(&Batch->DestroyCommitted, 0, 0) == 0)
    {
        if (ResourceOperationLockHeld)
            DxgkpVidMmPoisonDestroyBatchResource(Batch);
        InterlockedExchange(&Batch->CompletionWaiter, 0);
        DxgkpVidMmFreeDestroyBatch(Batch);
        return ResourceOperationLockHeld ? STATUS_DEVICE_BUSY : STATUS_SUCCESS;
    }
    if (ResourceOperationLockHeld)
    {
        DxgkpVidMmFreeDestroyBatch(Batch);
        return STATUS_SUCCESS;
    }
    DxgkpVidMmFreeDestroyBatch(Batch);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpVidMmActivateUnpublishedDestroyBatch(
    _In_ PDXGKVMM_DESTROY_BATCH Batch,
    _In_reads_(Batch->AllocationCount) PDXGKVMM_ALLOCATION *Allocations,
    _In_opt_ PDXGKVMM_RESOURCE Resource,
    _In_reads_opt_(Batch->AllocationCount) PHANDLE OpenHandles,
    _In_opt_ PDXGKVMM_OPEN_BINDING_GROUP OpenBindingGroup,
    _In_ NTSTATUS FailureStatus,
    _In_ BOOLEAN TrackUnpublishedObjects,
    _In_ BOOLEAN AwaitingStopBoundary)
{
    UINT OpenCount = 0;
    UINT Index;
    UINT OpenIndex;

    if (Batch == NULL || Batch->Adapter == NULL || Allocations == NULL || Batch->AllocationCount == 0 || InterlockedCompareExchange(&Batch->Listed, 0, 0) != 0 || InterlockedCompareExchange(&Batch->ActiveReferenceHeld, 0, 0) != 0)
        return STATUS_INVALID_PARAMETER;
    if (OpenHandles != NULL)
    {
        for (Index = 0; Index < Batch->AllocationCount; ++Index)
        {
            if (OpenHandles[Index] != NULL)
                OpenCount++;
        }
    }
    if ((OpenCount != 0 && (OpenBindingGroup == NULL || OpenBindingGroup->AllocationCount < OpenCount)) || (OpenCount == 0 && OpenBindingGroup != NULL && OpenBindingGroup->ReferenceCount != 1))
        return STATUS_INVALID_PARAMETER;
    if (OpenCount != 0)
    {
        OpenBindingGroup->AllocationCount = OpenCount;
        OpenBindingGroup->MiniportDeviceHandle = Allocations[0]->MiniportDeviceHandle;
        OpenBindingGroup->CloseStatus = STATUS_PENDING;
        InterlockedExchange(&OpenBindingGroup->CloseState, 0);
    }
    Batch->Resource = Resource;
    Batch->MiniportDeviceHandle = Allocations[0]->MiniportDeviceHandle;
    Batch->CompletionStatus = FailureStatus;
    Batch->AwaitingStopBoundary = AwaitingStopBoundary;

    if (TrackUnpublishedObjects)
    {
        if (Resource != NULL)
            DxgkpVidMmTrackBacking(Batch->Adapter);
        for (Index = 0; Index < Batch->AllocationCount; ++Index)
            DxgkpVidMmTrackBacking(Batch->Adapter);
    }
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    DxgkpVidMmRegisterDestroyBatch(Batch);
    OpenIndex = 0;
    for (Index = 0; Index < Batch->AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Allocations[Index];

        ASSERT(Allocation != NULL);
        Batch->Allocations[Index] = Allocation;
        Batch->OpenBindingHandles[Index] = OpenHandles != NULL ? OpenHandles[Index] : NULL;
        Allocation->DestroyBatch = Batch;
        Allocation->Initializing = FALSE;
        InterlockedExchange(&Allocation->FinalizeQueued, 2);
        InterlockedExchange(&Allocation->Destroying, 1);
        if (OpenBindingGroup != NULL && OpenHandles[Index] != NULL)
        {
            DxgkpVidMmReferenceOpenBindingGroup(OpenBindingGroup);
            Allocation->OpenBindingHandle = OpenHandles[Index];
            Allocation->OpenBindingGroupIndex = OpenIndex;
            Allocation->OpenBindingGroup = OpenBindingGroup;
            OpenBindingGroup->Allocations[OpenIndex] = Allocation;
            OpenBindingGroup->OpenHandles[OpenIndex] = OpenHandles[Index];
            OpenIndex++;
        }
    }
    if (Resource != NULL)
    {
        InterlockedIncrement(&Resource->ReferenceCount);
        if (TrackUnpublishedObjects)
            InterlockedExchange(&Resource->Destroying, 1);
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    DxgkpVidMmPoisonDestroyBatchResource(Batch);
    for (Index = 0; Index < Batch->AllocationCount; ++Index)
        DxgkpVidMmDropLogicalHandleReference(Allocations[Index]);
    if (TrackUnpublishedObjects && Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    if (OpenCount == 0)
        DxgkpVidMmTryCommitDestroyBatch(Batch);
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidMmDestroyAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE AllocationHandle)
{
    NTSTATUS Status;

    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DEVICE_NOT_READY;
    Status = DxgkpVidMmDestroyAllocation(Adapter, NULL, NULL, AllocationHandle);
    DxgkEndKmdTransaction(Adapter);
    return Status;
}

typedef enum _DXGKP_ALLOCATION_INFO_VERSION
{
    DxgkpAllocationInfoVersion1 = 1,
    DxgkpAllocationInfoVersion2 = 2
} DXGKP_ALLOCATION_INFO_VERSION;

typedef struct _DXGKP_ALLOCATION_INFO_VIEW
{
    D3DKMT_HANDLE hAllocation;
    PVOID pSystemMem;
    PVOID pPrivateDriverData;
    UINT PrivateDriverDataSize;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT Flags;
    D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
} DXGKP_ALLOCATION_INFO_VIEW, *PDXGKP_ALLOCATION_INFO_VIEW;

/* Decode the raw transport word because this tree deliberately carries newer
 * WDDM bits even when the compilation target exposes them as Reserved. */
C_ASSERT(sizeof(D3DKMT_CREATEALLOCATIONFLAGS) == sizeof(UINT));
#define DXGKP_CA_FLAG_CREATE_RESOURCE       0x00000001U
#define DXGKP_CA_FLAG_CREATE_SHARED         0x00000002U
#define DXGKP_CA_FLAG_EXISTING_SYSMEM       0x00000020U
#define DXGKP_CA_FLAG_CROSS_ADAPTER         0x00000800U
#define DXGKP_CA_FLAG_STANDARD_ALLOCATION   0x00010000U
#define DXGKP_CA_FLAG_EXISTING_SECTION      0x00020000U
#define DXGKP_CA_KNOWN_FLAGS_WDDM_1_0       0x0000003FU
#define DXGKP_CA_KNOWN_FLAGS_WDDM_1_2       0x000007FFU
#define DXGKP_CA_KNOWN_FLAGS_WDDM_1_3       0x0000FFFFU
#define DXGKP_CA_KNOWN_FLAGS_WDDM_2_3       0x0003FFFFU
#define DXGKP_CA_KNOWN_FLAGS_WDDM_2_6       0x0007FFFFU
#define DXGKP_CA_KNOWN_FLAGS_WDDM_2_7       0x001FFFFFU
#define DXGKP_CA_KNOWN_FLAGS_WDDM_3_0       0x003FFFFFU
#define DXGKP_CA_KNOWN_FLAGS_WDDM_3_2       0x007FFFFFU
/* Keep documented-but-unimplemented inputs out of this mask so they reach the
 * STATUS_NOT_SUPPORTED gate instead of being misclassified as malformed. */
#define DXGKP_CA_INVALID_INPUT_FLAGS_MASK   0x00001308U
#define DXGKP_CA_SUPPORTED_BASE_FLAGS_MASK  (DXGKP_CA_FLAG_CREATE_RESOURCE | DXGKP_CA_FLAG_CREATE_SHARED)
#define DXGKP_CA_STANDARD_REQUIRED_MASK     (DXGKP_CA_FLAG_CREATE_SHARED | DXGKP_CA_FLAG_CROSS_ADAPTER | DXGKP_CA_FLAG_STANDARD_ALLOCATION)
#define DXGKP_CA_STANDARD_SOURCE_MASK       (DXGKP_CA_FLAG_EXISTING_SYSMEM | DXGKP_CA_FLAG_EXISTING_SECTION)

#ifndef REACTOS_WDDM_TARGET_LEVEL
#define REACTOS_WDDM_TARGET_LEVEL DXGK_CAPS_CORE_LEVEL_WDDM_2_0
#endif

static UINT
DxgkpCreateAllocationKnownFlagsMask(
    _In_ ULONG WddmLevel)
{
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_3_2)
        return DXGKP_CA_KNOWN_FLAGS_WDDM_3_2;
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_3_0)
        return DXGKP_CA_KNOWN_FLAGS_WDDM_3_0;
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_2_7)
        return DXGKP_CA_KNOWN_FLAGS_WDDM_2_7;
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_2_6)
        return DXGKP_CA_KNOWN_FLAGS_WDDM_2_6;
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_2_3)
        return DXGKP_CA_KNOWN_FLAGS_WDDM_2_3;
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_1_3)
        return DXGKP_CA_KNOWN_FLAGS_WDDM_1_3;
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_1_2)
        return DXGKP_CA_KNOWN_FLAGS_WDDM_1_2;
    return DXGKP_CA_KNOWN_FLAGS_WDDM_1_0;
}

static ULONG
DxgkpVidMmEffectiveWddmLevel(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG MiniportLevel;

    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return 0;
    MiniportLevel = DxgkCapsCoreInterfaceVersionToLevel(
        Adapter->MiniportContext->InitData.s.Version);
    if (MiniportLevel == 0)
        return 0;
    return min(MiniportLevel, (ULONG)REACTOS_WDDM_TARGET_LEVEL);
}

static NTSTATUS
DxgkpValidateCreateAllocationFlags(
    _In_ CONST D3DKMT_CREATEALLOCATIONFLAGS *Flags,
    _In_ ULONG WddmLevel,
    _In_ BOOLEAN CheckImplemented,
    _Out_ PBOOLEAN StandardAllocation)
{
    UINT RawFlags;
    UINT KnownFlags;
    UINT StandardSources;

    RtlCopyMemory(&RawFlags, Flags, sizeof(RawFlags));
    KnownFlags = DxgkpCreateAllocationKnownFlagsMask(WddmLevel);
    *StandardAllocation = (RawFlags & DXGKP_CA_FLAG_STANDARD_ALLOCATION) != 0;
    if ((RawFlags & ~KnownFlags) != 0 || (RawFlags & DXGKP_CA_INVALID_INPUT_FLAGS_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if ((RawFlags & DXGKP_CA_FLAG_CREATE_SHARED) != 0 && (RawFlags & DXGKP_CA_FLAG_CREATE_RESOURCE) == 0)
        return STATUS_INVALID_PARAMETER;
    if (*StandardAllocation)
    {
        StandardSources = RawFlags & DXGKP_CA_STANDARD_SOURCE_MASK;
        if ((RawFlags & DXGKP_CA_STANDARD_REQUIRED_MASK) != DXGKP_CA_STANDARD_REQUIRED_MASK || StandardSources == 0 || StandardSources == DXGKP_CA_STANDARD_SOURCE_MASK)
            return STATUS_INVALID_PARAMETER;
        if (!CheckImplemented)
            return STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }
    if ((RawFlags & DXGKP_CA_STANDARD_SOURCE_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if (!CheckImplemented)
        return STATUS_SUCCESS;
    if ((RawFlags & ~DXGKP_CA_SUPPORTED_BASE_FLAGS_MASK) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static VOID
DxgkpReadAllocationInfo(
    _In_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion,
    _In_ UINT Index,
    _Out_ PDXGKP_ALLOCATION_INFO_VIEW View)
{
    RtlZeroMemory(View, sizeof(*View));
    if (InfoVersion == DxgkpAllocationInfoVersion2)
    {
        D3DDDI_ALLOCATIONINFO2 *Info = &CreateAllocation->pAllocationInfo2[Index];

        View->hAllocation = Info->hAllocation;
        View->pSystemMem = (PVOID)Info->pSystemMem;
        View->pPrivateDriverData = Info->pPrivateDriverData;
        View->PrivateDriverDataSize = Info->PrivateDriverDataSize;
        View->VidPnSourceId = Info->VidPnSourceId;
        View->Flags = Info->Flags.Value;
        View->GpuVirtualAddress = Info->GpuVirtualAddress;
    }
    else
    {
        D3DDDI_ALLOCATIONINFO *Info = &CreateAllocation->pAllocationInfo[Index];

        View->hAllocation = Info->hAllocation;
        View->pSystemMem = (PVOID)Info->pSystemMem;
        View->pPrivateDriverData = Info->pPrivateDriverData;
        View->PrivateDriverDataSize = Info->PrivateDriverDataSize;
        View->VidPnSourceId = Info->VidPnSourceId;
        View->Flags = Info->Flags.Value;
    }
}

static VOID
DxgkpSetAllocationHandle(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion,
    _In_ UINT Index,
    _In_ D3DKMT_HANDLE Handle)
{
    if (InfoVersion == DxgkpAllocationInfoVersion2)
        CreateAllocation->pAllocationInfo2[Index].hAllocation = Handle;
    else
        CreateAllocation->pAllocationInfo[Index].hAllocation = Handle;
}

static VOID
DxgkpSetAllocationPrivateData(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion,
    _In_ UINT Index,
    _In_opt_ PVOID PrivateDriverData)
{
    if (InfoVersion == DxgkpAllocationInfoVersion2)
        CreateAllocation->pAllocationInfo2[Index].pPrivateDriverData = PrivateDriverData;
    else
        CreateAllocation->pAllocationInfo[Index].pPrivateDriverData = PrivateDriverData;
}

static NTSTATUS
DxgkpValidateAllocationInfo(
    _In_ CONST D3DDDI_ALLOCATIONINFO *Info,
    _In_ ULONG WddmLevel,
    _In_ BOOLEAN CheckImplemented)
{
    UINT KnownFlags = WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_1_2 ? 0x3U : 0x1U;

    if ((Info->Flags.Value & ~KnownFlags) != 0 ||
        ((Info->Flags.Value & 0x2U) != 0 && (Info->Flags.Value & 0x1U) == 0))
        return STATUS_INVALID_PARAMETER;
    if (CheckImplemented && (Info->Flags.Value & KnownFlags) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpValidateAllocationInfo2(
    _Inout_ D3DDDI_ALLOCATIONINFO2 *Info,
    _In_ ULONG WddmLevel,
    _In_ BOOLEAN CheckImplemented)
{
    CONST ULONG_PTR *Tail = (CONST ULONG_PTR *)((CONST UCHAR *)Info + sizeof(*Info) - (6 * sizeof(ULONG_PTR)));
    UINT KnownFlags = 0x1U;
    UINT Index;

    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_1_2)
        KnownFlags |= 0x2U;
    if (WddmLevel >= DXGK_CAPS_CORE_LEVEL_WDDM_2_2)
        KnownFlags |= 0x4U;
    if ((Info->Flags.Value & ~KnownFlags) != 0)
        return STATUS_INVALID_PARAMETER;
    for (Index = 1; Index < 6; ++Index)
    {
        if (Tail[Index] != 0)
            return STATUS_INVALID_PARAMETER;
    }
    if ((WddmLevel < DXGK_CAPS_CORE_LEVEL_WDDM_2_2 ||
         (Info->Flags.Value & 0x4U) == 0) &&
        Tail[0] != 0)
        return STATUS_INVALID_PARAMETER;
    if ((Info->Flags.Value & 0x2U) != 0 && (Info->Flags.Value & 0x1U) == 0)
        return STATUS_INVALID_PARAMETER;
    if (CheckImplemented && (Info->Flags.Value & KnownFlags) != 0)
        return STATUS_NOT_SUPPORTED;
    /* CreateAllocation2 is also valid for physical-addressing adapters. Zero
     * records that common-prefix result; it is not a process GPUVA mapping. */
    Info->GpuVirtualAddress = 0;
    return STATUS_SUCCESS;
}

static VOID
DxgkpScrubCreateAllocationOutputs(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _Inout_updates_bytes_(AllocationCount * AllocationInfoStride) PVOID UserAllocationInfo,
    _In_ UINT AllocationCount,
    _In_ SIZE_T AllocationInfoStride,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode,
    _In_ D3DKMT_HANDLE SafeResourceHandle)
{
    D3DKMT_HANDLE ZeroHandle = 0;
    D3DGPU_VIRTUAL_ADDRESS ZeroGpuVirtualAddress = 0;
    UINT Index;

    for (Index = 0; Index < AllocationCount; ++Index)
    {
        (VOID)DxgkpCopyToUserBuffer((PUCHAR)UserAllocationInfo + ((SIZE_T)Index * AllocationInfoStride), &ZeroHandle, sizeof(ZeroHandle), EmbeddedBufferMode);
        if (InfoVersion == DxgkpAllocationInfoVersion2)
            (VOID)DxgkpCopyToUserBuffer((PUCHAR)UserAllocationInfo + ((SIZE_T)Index * AllocationInfoStride) + FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, GpuVirtualAddress), &ZeroGpuVirtualAddress, sizeof(ZeroGpuVirtualAddress), EmbeddedBufferMode);
    }
    CreateAllocation->hResource = SafeResourceHandle;
    CreateAllocation->hGlobalShare = 0;
}

static NTSTATUS
DxgkpVidMmOpenCreatorAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion,
    _Out_ PBOOLEAN RollbackOwned)
{
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    PDXGKVMM_OPEN_BINDING_GROUP OpenBindingGroup = NULL;
    PDXGKVMM_DESTROY_BATCH RollbackBatch = NULL;
    DXGK_OPENALLOCATIONINFO *OpenInfo = NULL;
    PHANDLE CloseHandles = NULL;
    PVOID ResourcePrivateData = NULL;
    DXGKARG_OPENALLOCATION OpenArgs;
    UINT AllocationCount;
    UINT CloseCount = 0;
    UINT Index;
    BOOLEAN OpenSucceeded = FALSE;
    BOOLEAN KmdTransactionStarted = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Adapter == NULL || Device == NULL || CreateAllocation == NULL || RollbackOwned == NULL)
        return STATUS_INVALID_PARAMETER;
    *RollbackOwned = FALSE;
    if (DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation) == NULL)
        return STATUS_SUCCESS;
    if (DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL)
        return STATUS_NOT_SUPPORTED;
    AllocationCount = CreateAllocation->NumAllocations;
    if (AllocationCount == 0 || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Allocations) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*OpenInfo) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*CloseHandles))
        return STATUS_INTEGER_OVERFLOW;

    Allocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Allocations), TAG_VIDMM_ALLOC);
    OpenInfo = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*OpenInfo), TAG_VIDMM_ALLOC);
    CloseHandles = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*CloseHandles), TAG_VIDMM_ALLOC);
    OpenBindingGroup = DxgkpVidMmCreateOpenBindingGroup(AllocationCount);
    RollbackBatch = DxgkpVidMmAllocateDestroyBatch(Adapter, AllocationCount);
    if (Allocations == NULL || OpenInfo == NULL || CloseHandles == NULL || OpenBindingGroup == NULL || RollbackBatch == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(Allocations, (SIZE_T)AllocationCount * sizeof(*Allocations));
    RtlZeroMemory(OpenInfo, (SIZE_T)AllocationCount * sizeof(*OpenInfo));
    RtlZeroMemory(CloseHandles, (SIZE_T)AllocationCount * sizeof(*CloseHandles));
    if (CreateAllocation->PrivateDriverDataSize != 0)
    {
        if (CreateAllocation->pPrivateDriverData == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        ResourcePrivateData = ExAllocatePoolWithTag(NonPagedPool, CreateAllocation->PrivateDriverDataSize, TAG_VIDMM_ALLOC);
        if (ResourcePrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(ResourcePrivateData, CreateAllocation->pPrivateDriverData, CreateAllocation->PrivateDriverDataSize);
    }

    for (Index = 0; Index < AllocationCount; ++Index)
    {
        DXGKP_ALLOCATION_INFO_VIEW AllocationInfo;
        HANDLE AllocationHandle;

        DxgkpReadAllocationInfo(CreateAllocation, InfoVersion, Index, &AllocationInfo);
        AllocationHandle = (HANDLE)(ULONG_PTR)AllocationInfo.hAllocation;

        Status = DxgkpVidMmReferenceInitializingAllocation(AllocationHandle, Adapter, Device, &Allocations[Index]);
        if (!NT_SUCCESS(Status) || Allocations[Index]->BackingAllocation != NULL || Allocations[Index]->PrivateDriverDataSize != AllocationInfo.PrivateDriverDataSize)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        OpenInfo[Index].hAllocation = AllocationInfo.hAllocation;
        OpenInfo[Index].pPrivateDriverData = AllocationInfo.pPrivateDriverData;
        OpenInfo[Index].PrivateDriverDataSize = AllocationInfo.PrivateDriverDataSize;
    }

    RtlZeroMemory(&OpenArgs, sizeof(OpenArgs));
    OpenArgs.NumAllocations = AllocationCount;
    OpenArgs.pOpenAllocation = OpenInfo;
    OpenArgs.pPrivateDriverData = ResourcePrivateData;
    OpenArgs.PrivateDriverSize = CreateAllocation->PrivateDriverDataSize;
    OpenArgs.Flags.Create = 1;
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    KmdTransactionStarted = TRUE;
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation)(Device->hMiniportDevice, &OpenArgs);
    DxgkReleaseMiniportCallback(Adapter);
    if (!NT_SUCCESS(Status))
        goto RollbackOpen;
    OpenSucceeded = TRUE;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (OpenInfo[Index].hDeviceSpecificAllocation == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto RollbackOpen;
        }
    }

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (DxgkpVidMmLookupAllocationLocked(OpenInfo[Index].hAllocation) != Allocations[Index] || InterlockedCompareExchange(&Allocations[Index]->Destroying, 0, 0) != 0 || Allocations[Index]->OpenBindingHandle != NULL || Allocations[Index]->OpenBindingGroup != NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
    }
    if (NT_SUCCESS(Status))
    {
        OpenBindingGroup->MiniportDeviceHandle = Device->hMiniportDevice;
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            DxgkpVidMmReferenceOpenBindingGroup(OpenBindingGroup);
            Allocations[Index]->OpenBindingHandle = OpenInfo[Index].hDeviceSpecificAllocation;
            Allocations[Index]->OpenBindingGroupIndex = Index;
            Allocations[Index]->OpenBindingGroup = OpenBindingGroup;
            OpenBindingGroup->Allocations[Index] = Allocations[Index];
            OpenBindingGroup->OpenHandles[Index] = OpenInfo[Index].hDeviceSpecificAllocation;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    if (!NT_SUCCESS(Status))
        goto RollbackOpen;
    DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
    OpenBindingGroup = NULL;
    DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    RollbackBatch = NULL;

    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (Allocations[Index]->PrivateDriverDataSize != 0)
            RtlCopyMemory(Allocations[Index]->PrivateDriverData, OpenInfo[Index].pPrivateDriverData, Allocations[Index]->PrivateDriverDataSize);
    }
    goto Cleanup;

RollbackOpen:
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        CloseHandles[Index] = OpenInfo[Index].hDeviceSpecificAllocation;
        if (OpenInfo[Index].hDeviceSpecificAllocation != NULL)
            CloseCount++;
    }
    {
        PDXGKVMM_RESOURCE RollbackResource = Allocations[0]->Resource;
        NTSTATUS CloseStatus;
        BOOLEAN AwaitStopBoundary = OpenSucceeded && CloseCount == 0;

        InterlockedExchange(&RollbackBatch->CompletionWaiter, AwaitStopBoundary ? 0 : 1);
        CloseStatus = DxgkpVidMmActivateUnpublishedDestroyBatch(RollbackBatch, Allocations, RollbackResource, CloseHandles, CloseCount != 0 ? OpenBindingGroup : NULL, Status, FALSE, AwaitStopBoundary);
        ASSERT(NT_SUCCESS(CloseStatus));
        if (NT_SUCCESS(CloseStatus))
        {
            *RollbackOwned = TRUE;
            if (CloseCount != 0)
                CloseStatus = DxgkpVidMmCloseReadyBindingGroup(Adapter, OpenBindingGroup);
            else
                CloseStatus = STATUS_SUCCESS;
            if (CloseStatus == STATUS_SUCCESS && !AwaitStopBoundary)
                DxgkpVidMmTryCommitDestroyBatch(RollbackBatch);
            else
            {
                if (CloseStatus != STATUS_SUCCESS)
                {
                    RollbackBatch->AwaitingStopBoundary = TRUE;
                    InterlockedExchange(&RollbackBatch->CompletionWaiter, 0);
                    DxgkpVidMmQuarantineDestroyBatch(RollbackBatch);
                }
            }
            DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
            OpenBindingGroup = NULL;
            for (Index = 0; Index < AllocationCount; ++Index)
            {
                DxgkVidMmDereferenceAllocation(Allocations[Index]);
                Allocations[Index] = NULL;
            }
            if (CloseStatus == STATUS_SUCCESS && !AwaitStopBoundary)
            {
                KeWaitForSingleObject(&RollbackBatch->CompletionEvent, Executive, KernelMode, FALSE, NULL);
                InterlockedExchange(&RollbackBatch->CompletionWaiter, 0);
            }
            DxgkpVidMmFreeDestroyBatch(RollbackBatch);
            RollbackBatch = NULL;
        }
    }

Cleanup:
    if (OpenBindingGroup != NULL)
        DxgkpVidMmDereferenceOpenBindingGroup(OpenBindingGroup);
    if (Allocations != NULL)
    {
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            if (Allocations[Index] != NULL)
                DxgkVidMmDereferenceAllocation(Allocations[Index]);
        }
        ExFreePoolWithTag(Allocations, TAG_VIDMM_ALLOC);
    }
    if (OpenInfo != NULL)
        ExFreePoolWithTag(OpenInfo, TAG_VIDMM_ALLOC);
    if (CloseHandles != NULL)
        ExFreePoolWithTag(CloseHandles, TAG_VIDMM_ALLOC);
    if (ResourcePrivateData != NULL)
        ExFreePoolWithTag(ResourcePrivateData, TAG_VIDMM_ALLOC);
    if (RollbackBatch != NULL)
        DxgkpVidMmFreeDestroyBatch(RollbackBatch);
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    return Status;
}

static NTSTATUS
DxgkpVidMmReferenceInitializingAllocation(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    if (Handle == NULL || Adapter == NULL || Device == NULL || Device->Adapter != Adapter || OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocation = NULL;
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    Allocation = DxgkpVidMmLookupAllocationLocked((D3DKMT_HANDLE)(ULONG_PTR)Handle);
    if (Allocation != NULL && Allocation->Adapter == Adapter && Allocation->Device == Device && Allocation->BackingAllocation == NULL && Allocation->Initializing && InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0 && InterlockedCompareExchange(&Allocation->ReferenceCount, 0, 0) > 0)
    {
        InterlockedIncrement(&Allocation->ReferenceCount);
        *OutAllocation = Allocation;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    return Status;
}

static NTSTATUS
DxgkpVidMmCommitInitializingAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion)
{
    PDXGKVMM_ALLOCATION Allocation;
    UINT Index;
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < CreateAllocation->NumAllocations; ++Index)
    {
        DXGKP_ALLOCATION_INFO_VIEW AllocationInfo;

        DxgkpReadAllocationInfo(CreateAllocation, InfoVersion, Index, &AllocationInfo);
        Allocation = DxgkpVidMmLookupAllocationLocked(AllocationInfo.hAllocation);
        if (Allocation == NULL || Allocation->Adapter != Adapter || Allocation->Device != Device || !Allocation->Initializing || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
    }
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < CreateAllocation->NumAllocations; ++Index)
        {
            DXGKP_ALLOCATION_INFO_VIEW AllocationInfo;

            DxgkpReadAllocationInfo(CreateAllocation, InfoVersion, Index, &AllocationInfo);
            Allocation = DxgkpVidMmLookupAllocationLocked(AllocationInfo.hAllocation);
            Allocation->Initializing = FALSE;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    return Status;
}

static NTSTATUS
DxgkpCreateAllocationCaptured(
    _Inout_ D3DKMT_CREATEALLOCATION *pCreateAllocation,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKVMM_RESOURCE Resource = NULL;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    D3DKMT_HANDLE InputResourceHandle;
    BOOLEAN CreatedResource = FALSE;
    BOOLEAN ResourceLockHeld = FALSE;
    BOOLEAN ResourceReferenced = FALSE;
    BOOLEAN OpenRollbackOwned = FALSE;
    BOOLEAN KmdTransactionStarted = FALSE;
    PDXGKVMM_ALLOCATION *CreateRollbackAllocations = NULL;
    PDXGKVMM_DESTROY_BATCH CreateRollbackBatch = NULL;
    UINT CreatedAllocationCount = 0;
    BOOLEAN StandardAllocation;
    ULONG EffectiveWddmLevel;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT i;

    PAGED_CODE();

    if (pCreateAllocation == NULL ||
        (InfoVersion != DxgkpAllocationInfoVersion1 && InfoVersion != DxgkpAllocationInfoVersion2) ||
        pCreateAllocation->NumAllocations == 0 ||
        pCreateAllocation->pAllocationInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (InfoVersion == DxgkpAllocationInfoVersion2 &&
        REACTOS_WDDM_TARGET_LEVEL < DXGK_CAPS_CORE_LEVEL_WDDM_1_1)
    {
        return STATUS_NOT_SUPPORTED;
    }

    InputResourceHandle = pCreateAllocation->hResource;
    if (pCreateAllocation->Flags.CreateResource && InputResourceHandle != 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateCreateAllocationFlags(
        &pCreateAllocation->Flags,
        REACTOS_WDDM_TARGET_LEVEL,
        FALSE,
        &StandardAllocation);
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < pCreateAllocation->NumAllocations; ++i)
    {
        Status = InfoVersion == DxgkpAllocationInfoVersion2 ?
            DxgkpValidateAllocationInfo2(
                &pCreateAllocation->pAllocationInfo2[i],
                REACTOS_WDDM_TARGET_LEVEL,
                FALSE) :
            DxgkpValidateAllocationInfo(
                &pCreateAllocation->pAllocationInfo[i],
                REACTOS_WDDM_TARGET_LEVEL,
                FALSE);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (StandardAllocation)
    {
        D3DKMT_CREATESTANDARDALLOCATION *StdAlloc = pCreateAllocation->pStandardAllocation;
        D3DKMT_STANDARDALLOCATIONTYPE StdType = D3DKMT_STANDARDALLOCATIONTYPE_MAX;
        UINT StdFlags = MAXUINT;

        if (StdAlloc == NULL)
            return STATUS_INVALID_PARAMETER;

        _SEH2_TRY
        {
            StdType = StdAlloc->Type;
            StdFlags = StdAlloc->Flags.Value;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
        }
        _SEH2_END;

        if (StdFlags != 0 || StdType < D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP || StdType >= D3DKMT_STANDARDALLOCATIONTYPE_MAX || pCreateAllocation->NumAllocations != 1 || pCreateAllocation->hResource != 0 || pCreateAllocation->PrivateDriverDataSize != 0)
            return STATUS_INVALID_PARAMETER;
        return STATUS_NOT_SUPPORTED;
    }

    Device = DxgkpVidMmFindDeviceByHandle(pCreateAllocation->hDevice, &Adapter);
    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    EffectiveWddmLevel = DxgkpVidMmEffectiveWddmLevel(Adapter);
    if (InfoVersion == DxgkpAllocationInfoVersion2 &&
        EffectiveWddmLevel < DXGK_CAPS_CORE_LEVEL_WDDM_1_1)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }
    Status = DxgkpValidateCreateAllocationFlags(
        &pCreateAllocation->Flags,
        EffectiveWddmLevel,
        TRUE,
        &StandardAllocation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    for (i = 0; i < pCreateAllocation->NumAllocations; ++i)
    {
        Status = InfoVersion == DxgkpAllocationInfoVersion2 ?
            DxgkpValidateAllocationInfo2(
                &pCreateAllocation->pAllocationInfo2[i],
                EffectiveWddmLevel,
                TRUE) :
            DxgkpValidateAllocationInfo(
                &pCreateAllocation->pAllocationInfo[i],
                EffectiveWddmLevel,
                TRUE);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    CreateRollbackAllocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)pCreateAllocation->NumAllocations * sizeof(*CreateRollbackAllocations), TAG_VIDMM_ALLOC);
    CreateRollbackBatch = DxgkpVidMmAllocateDestroyBatch(Adapter, pCreateAllocation->NumAllocations);
    if (CreateRollbackAllocations == NULL || CreateRollbackBatch == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(CreateRollbackAllocations, (SIZE_T)pCreateAllocation->NumAllocations * sizeof(*CreateRollbackAllocations));
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    KmdTransactionStarted = TRUE;
    if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    if (pCreateAllocation->hResource != 0)
    {
        Status = DxgkVidMmReferenceResource(pCreateAllocation->hResource, FALSE, Device, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        ResourceReferenced = TRUE;
        if (Resource->BackingResource != NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        (VOID)KeWaitForSingleObject(&Resource->ResourceOperationLock, Executive, KernelMode, FALSE, NULL);
        ResourceLockHeld = TRUE;
        if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Resource->CloseUncertain, 0, 0) != 0 || InterlockedCompareExchange(&Resource->DestroyFailureUncertain, 0, 0) != 0)
        {
            Status = STATUS_DEVICE_BUSY;
            goto Cleanup;
        }
    }

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = (pCreateAllocation->Flags.CreateResource || Resource != NULL) ? 1 : 0;

    pCreateAllocation->hGlobalShare = 0;
    pCreateAllocation->hResource    = InputResourceHandle;

    for (i = 0; i < pCreateAllocation->NumAllocations; ++i)
    {
        DXGKP_ALLOCATION_INFO_VIEW AllocationInfo;
        DXGK_ALLOCATIONINFO DxgkAllocInfo;
        HANDLE AllocationHandle = NULL;
        HANDLE MiniportResourceHandle = NULL;
        PDXGKVMM_ALLOCATION TrackedAlloc = NULL;
        BOOLEAN UseSystemAllocation = FALSE;

        RtlZeroMemory(&DxgkAllocInfo, sizeof(DxgkAllocInfo));
        DxgkpReadAllocationInfo(pCreateAllocation, InfoVersion, i, &AllocationInfo);

        DxgkAllocInfo.pPrivateDriverData = AllocationInfo.pPrivateDriverData;
        DxgkAllocInfo.PrivateDriverDataSize = AllocationInfo.PrivateDriverDataSize;
        DxgkAllocInfo.Flags.ExistingSysMem = (pCreateAllocation->Flags.ExistingSysMem || AllocationInfo.pSystemMem != NULL) ? 1 : 0;

        Status = STATUS_SUCCESS;
        if (DxgkAllocInfo.PrivateDriverDataSize == 0)
        {
            DxgkAllocInfo.Size = PAGE_SIZE;
            UseSystemAllocation = TRUE;
        }
        else if (DxgkAllocInfo.PrivateDriverDataSize >= sizeof(UINT) &&
                 DxgkAllocInfo.PrivateDriverDataSize < 32 &&
                 DxgkAllocInfo.pPrivateDriverData != NULL)
        {
            if (DxgkAllocInfo.PrivateDriverDataSize >= (3 * sizeof(UINT)))
            {
                UINT Dimensions[3];

                _SEH2_TRY
                {
                    RtlCopyMemory(Dimensions,
                                  DxgkAllocInfo.pPrivateDriverData,
                                  sizeof(Dimensions));
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                    break;
                }
                _SEH2_END;

                if (Dimensions[0] != 0 && Dimensions[1] != 0 && Dimensions[2] != 0)
                {
                    SIZE_T BytesPerPixel = ((SIZE_T)Dimensions[2] + 7) / 8;
                    SIZE_T PixelCount;

                    if ((SIZE_T)Dimensions[0] > MAXULONG_PTR / (SIZE_T)Dimensions[1])
                        Status = STATUS_INTEGER_OVERFLOW;
                    else
                    {
                        PixelCount = (SIZE_T)Dimensions[0] * (SIZE_T)Dimensions[1];
                        if (BytesPerPixel == 0 || PixelCount > MAXULONG_PTR / BytesPerPixel)
                            Status = STATUS_INTEGER_OVERFLOW;
                        else
                        {
                            DxgkAllocInfo.Size = PixelCount * BytesPerPixel;
                            UseSystemAllocation = TRUE;
                        }
                    }
                }
            }
            else if (DxgkAllocInfo.PrivateDriverDataSize >= sizeof(UINT))
            {
                UINT Size;

                _SEH2_TRY
                {
                    Size = *(const UINT *)DxgkAllocInfo.pPrivateDriverData;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                    break;
                }
                _SEH2_END;

                DxgkAllocInfo.Size = Size;
                if (Size != 0)
                    UseSystemAllocation = TRUE;
            }
        }

        if (!NT_SUCCESS(Status))
            break;

        /*
         * The private-data-size heuristic above infers a size but must NOT force
         * a pool-backed "system" allocation when the adapter has a real miniport:
         * a VRAM-backed miniport (e.g. rpi5vc4) has to go through
         * DxgkVidMmCreateAllocation so the allocation is placed in a device
         * segment and gets a slab-relative physical address the GPU can reach
         * (the parsed DxgkAllocInfo.Size is carried into DxgkDdiCreateAllocation).
         * Pool RAM (MmGetPhysicalAddress) lies outside the slab window and is
         * rejected by the UMD at D3DKMTMapGpuVirtualAddress time.  Only adapters
         * with no CreateAllocation DDI fall back to the synthetic system path.
         */
        if (UseSystemAllocation &&
            (Adapter->MiniportContext == NULL ||
             DXGK_CB_FULL(Adapter, DxgkDdiCreateAllocation) == NULL))
        {
            DxgkAllocInfo.Flags.CpuVisible = 1;
            Status = DxgkpVidMmCreateSystemAllocation(Adapter,
                                                      Device,
                                                      &DxgkAllocInfo,
                                                      &AllocationHandle,
                                                      &TrackedAlloc);
            MiniportResourceHandle = NULL;
        }
        else
        {
            Status = STATUS_NOT_SUPPORTED;
        }

        if (Status == STATUS_NOT_SUPPORTED)
        {
            if (Resource != NULL)
            {
                (VOID)KeWaitForSingleObject(&Resource->MiniportResourceLock, Executive, KernelMode, FALSE, NULL);
                Status = DxgkpVidMmCreateAllocationTracked(Adapter,
                                                           Device,
                                                           &DxgkAllocInfo,
                                                           pCreateAllocation->pPrivateDriverData,
                                                           pCreateAllocation->PrivateDriverDataSize,
                                                           Resource->MiniportHandle,
                                                           CreateFlags,
                                                           &AllocationHandle,
                                                           &MiniportResourceHandle,
                                                           &TrackedAlloc);
                Resource->MiniportHandle = MiniportResourceHandle;
                KeReleaseMutex(&Resource->MiniportResourceLock, FALSE);
            }
            else
                Status = DxgkpVidMmCreateAllocationTracked(Adapter,
                                                           Device,
                                                           &DxgkAllocInfo,
                                                           pCreateAllocation->pPrivateDriverData,
                                                           pCreateAllocation->PrivateDriverDataSize,
                                                           NULL,
                                                           CreateFlags,
                                                           &AllocationHandle,
                                                           &MiniportResourceHandle,
                                                           &TrackedAlloc);
        }
        if (!NT_SUCCESS(Status))
            break;

        /*
         * The create helpers return the initializing object with a transient
         * reference in the same transaction that publishes its handle.  This
         * makes rollback ownership unconditional: there is no second lookup
         * whose unexpected failure could strand an initializing allocation
         * that normal public destroy intentionally refuses to touch.
         */
        ASSERT(TrackedAlloc != NULL);
        if (TrackedAlloc == NULL)
        {
            Status = STATUS_INTERNAL_ERROR;
            break;
        }
        CreateRollbackAllocations[CreatedAllocationCount++] = TrackedAlloc;

        if (CreateFlags.Resource && Resource == NULL)
        {
            Resource = DxgkVidMmCreateResourceWrapper(Adapter, Device, MiniportResourceHandle, 0, pCreateAllocation->Flags.CreateShared != 0, pCreateAllocation->pPrivateRuntimeData, pCreateAllocation->PrivateRuntimeDataSize, pCreateAllocation->pPrivateDriverData, pCreateAllocation->PrivateDriverDataSize);
            if (Resource == NULL)
            {
                DxgkVidMmDereferenceAllocation(TrackedAlloc);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            TrackedAlloc->DestroyMiniportResource = FALSE;

            CreatedResource = TRUE;
            (VOID)KeWaitForSingleObject(&Resource->ResourceOperationLock, Executive, KernelMode, FALSE, NULL);
            ResourceLockHeld = TRUE;

            pCreateAllocation->hResource = Resource->Handle;
            if (pCreateAllocation->Flags.CreateShared)
            {
                pCreateAllocation->hGlobalShare = Resource->GlobalShareHandle;
                ASSERT(pCreateAllocation->hGlobalShare != 0);
            }
        }

        if (Resource != NULL)
        {
            Status = DxgkVidMmAttachAllocationToResource(Resource, TrackedAlloc);
            if (!NT_SUCCESS(Status))
            {
                DxgkVidMmDereferenceAllocation(TrackedAlloc);
                break;
            }
            TrackedAlloc->MiniportResourceHandle = NULL;
            TrackedAlloc->DestroyMiniportResource = FALSE;
        }

        DxgkpSetAllocationHandle(pCreateAllocation, InfoVersion, i, (D3DKMT_HANDLE)(ULONG_PTR)AllocationHandle);

        DxgkVidMmDereferenceAllocation(TrackedAlloc);

        if (!NT_SUCCESS(Status))
        {
            if (ResourceLockHeld)
            {
                KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
                ResourceLockHeld = FALSE;
            }
            break;
        }
    }

    if (NT_SUCCESS(Status) && i == pCreateAllocation->NumAllocations)
        Status = DxgkpVidMmOpenCreatorAllocations(Adapter, Device, pCreateAllocation, InfoVersion, &OpenRollbackOwned);
    if (NT_SUCCESS(Status) && i == pCreateAllocation->NumAllocations)
        Status = DxgkpVidMmCommitInitializingAllocations(Adapter, Device, pCreateAllocation, InfoVersion);

    if (!NT_SUCCESS(Status))
    {
        if (ResourceLockHeld)
        {
            KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
            ResourceLockHeld = FALSE;
        }
        if (!OpenRollbackOwned && CreatedAllocationCount != 0)
        {
            PDXGKVMM_RESOURCE RollbackResource = CreateRollbackAllocations[0]->Resource;
            BOOLEAN HasOpenBindings = CreateRollbackAllocations[0]->OpenBindingGroup != NULL;
            NTSTATUS RollbackStatus;
            UINT RollbackIndex;
            UINT RollbackGroupCount = 0;
            UINT OtherRollbackIndex;

            CreateRollbackBatch->AllocationCount = CreatedAllocationCount;
            CreateRollbackBatch->PendingAllocationCount = (LONG)CreatedAllocationCount;
            if (HasOpenBindings)
            {
                for (RollbackIndex = 0; RollbackIndex < CreatedAllocationCount; ++RollbackIndex)
                {
                    PDXGKVMM_OPEN_BINDING_GROUP Group = CreateRollbackAllocations[RollbackIndex]->OpenBindingGroup;

                    if (Group == NULL)
                        continue;
                    for (OtherRollbackIndex = 0; OtherRollbackIndex < RollbackGroupCount && (PDXGKVMM_OPEN_BINDING_GROUP)CreateRollbackBatch->MiniportHandles[OtherRollbackIndex] != Group; ++OtherRollbackIndex)
                        NOTHING;
                    if (OtherRollbackIndex == RollbackGroupCount)
                        CreateRollbackBatch->MiniportHandles[RollbackGroupCount++] = (HANDLE)Group;
                }
            }
            InterlockedExchange(&CreateRollbackBatch->CompletionWaiter, 1);
            RollbackStatus = DxgkpVidMmActivateUnpublishedDestroyBatch(CreateRollbackBatch, CreateRollbackAllocations, RollbackResource, NULL, NULL, Status, FALSE, FALSE);
            ASSERT(NT_SUCCESS(RollbackStatus));
            if (NT_SUCCESS(RollbackStatus))
            {
                if (HasOpenBindings)
                {
                    for (RollbackIndex = 0; RollbackIndex < RollbackGroupCount; ++RollbackIndex)
                    {
                        PDXGKVMM_OPEN_BINDING_GROUP Group = (PDXGKVMM_OPEN_BINDING_GROUP)CreateRollbackBatch->MiniportHandles[RollbackIndex];

                        RollbackStatus = DxgkpVidMmCloseReadyBindingGroup(Adapter, Group);
                        if (RollbackStatus != STATUS_SUCCESS)
                            break;
                    }
                    if (RollbackStatus == STATUS_SUCCESS)
                        DxgkpVidMmTryCommitDestroyBatch(CreateRollbackBatch);
                }
                if (RollbackStatus == STATUS_SUCCESS)
                {
                    KeWaitForSingleObject(&CreateRollbackBatch->CompletionEvent, Executive, KernelMode, FALSE, NULL);
                    InterlockedExchange(&CreateRollbackBatch->CompletionWaiter, 0);
                }
                else
                {
                    CreateRollbackBatch->AwaitingStopBoundary = TRUE;
                    InterlockedExchange(&CreateRollbackBatch->CompletionWaiter, 0);
                    DxgkpVidMmQuarantineDestroyBatch(CreateRollbackBatch);
                }
                DxgkpVidMmFreeDestroyBatch(CreateRollbackBatch);
                CreateRollbackBatch = NULL;
            }
        }
        while (i-- > 0)
        {
            HANDLE AllocationHandle;
            DXGKP_ALLOCATION_INFO_VIEW AllocationInfo;

            DxgkpReadAllocationInfo(pCreateAllocation, InfoVersion, i, &AllocationInfo);
            AllocationHandle = (HANDLE)(ULONG_PTR)AllocationInfo.hAllocation;
            if (AllocationHandle != NULL)
                DxgkpSetAllocationHandle(pCreateAllocation, InfoVersion, i, 0);
        }

        if (CreatedResource && Resource != NULL && Resource->AllocationCount == 0)
            DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);

        pCreateAllocation->hGlobalShare = 0;
        pCreateAllocation->hResource = CreatedResource ? 0 : InputResourceHandle;
    }

    if (ResourceLockHeld)
    {
        KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
        ResourceLockHeld = FALSE;
    }

Cleanup:
    if (ResourceLockHeld)
        KeReleaseMutex(&Resource->ResourceOperationLock, FALSE);
    if (ResourceReferenced)
        DxgkVidMmDereferenceResource(Resource);
    if (CreateRollbackBatch != NULL)
        DxgkpVidMmFreeDestroyBatch(CreateRollbackBatch);
    if (CreateRollbackAllocations != NULL)
        ExFreePoolWithTag(CreateRollbackAllocations, TAG_VIDMM_ALLOC);
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    DxgkDereferenceDevice(Device);
    return Status;
}

typedef struct _DXGKP_CREATEALLOCATION_PRIVATE_CAPTURE
{
    PVOID UserBuffer;
    PVOID CapturedBuffer;
    PVOID OriginalBuffer;
    UINT Size;
} DXGKP_CREATEALLOCATION_PRIVATE_CAPTURE, *PDXGKP_CREATEALLOCATION_PRIVATE_CAPTURE;

static NTSTATUS
DxgkpCreateAllocationWithAccessModeVariant(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode,
    _In_ DXGKP_ALLOCATION_INFO_VERSION InfoVersion)
{
    D3DKMT_CREATEALLOCATION Captured;
    D3DKMT_DESTROYALLOCATION DestroyAllocation;
    PVOID CapturedAllocationInfo = NULL;
    PVOID UserAllocationInfo;
    PDXGKP_CREATEALLOCATION_PRIVATE_CAPTURE PrivateCaptures = NULL;
    D3DKMT_HANDLE *CreatedHandles = NULL;
    PVOID CapturedPrivateDriverData = NULL;
    PVOID CapturedPrivateRuntimeData = NULL;
    SIZE_T AllocationInfoSize;
    SIZE_T PrivateCaptureSize;
    SIZE_T CreatedHandlesSize;
    SIZE_T AllocationInfoStride;
    SIZE_T TotalPrivateSize = 0;
    D3DKMT_HANDLE InputDevice;
    D3DKMT_HANDLE InputResource;
    UINT InputAllocationCount;
    UINT RollbackCount;
    UINT i;
    UINT j;
    BOOLEAN CoreSucceeded = FALSE;
    BOOLEAN InputCreatesResource;
    BOOLEAN StandardAllocation;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    PAGED_CODE();

    if (CreateAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    Captured = *CreateAllocation;
    if (InfoVersion != DxgkpAllocationInfoVersion1 && InfoVersion != DxgkpAllocationInfoVersion2)
        return STATUS_INVALID_PARAMETER;
    if (InfoVersion == DxgkpAllocationInfoVersion2 &&
        REACTOS_WDDM_TARGET_LEVEL < DXGK_CAPS_CORE_LEVEL_WDDM_1_1)
    {
        return STATUS_NOT_SUPPORTED;
    }
    Status = DxgkpValidateCreateAllocationFlags(
        &Captured.Flags,
        REACTOS_WDDM_TARGET_LEVEL,
        FALSE,
        &StandardAllocation);
    if (!NT_SUCCESS(Status))
        return Status;
    UserAllocationInfo = InfoVersion == DxgkpAllocationInfoVersion2 ? (PVOID)Captured.pAllocationInfo2 : (PVOID)Captured.pAllocationInfo;
    InputDevice = Captured.hDevice;
    InputResource = Captured.hResource;
    InputAllocationCount = Captured.NumAllocations;
    InputCreatesResource = Captured.Flags.CreateResource != 0;

    if (InputAllocationCount == 0 || InputAllocationCount > DXGKP_MAX_CAPTURE_ALLOCATIONS || UserAllocationInfo == NULL || (InputCreatesResource && InputResource != 0))
        return STATUS_INVALID_PARAMETER;
    AllocationInfoStride = InfoVersion == DxgkpAllocationInfoVersion2 ? sizeof(D3DDDI_ALLOCATIONINFO2) : sizeof(D3DDDI_ALLOCATIONINFO);
    if ((SIZE_T)InputAllocationCount > MAXULONG_PTR / AllocationInfoStride || (SIZE_T)InputAllocationCount > MAXULONG_PTR / sizeof(*PrivateCaptures) || (SIZE_T)InputAllocationCount > MAXULONG_PTR / sizeof(*CreatedHandles))
        return STATUS_INTEGER_OVERFLOW;

    AllocationInfoSize = (SIZE_T)InputAllocationCount * AllocationInfoStride;
    PrivateCaptureSize = (SIZE_T)InputAllocationCount * sizeof(*PrivateCaptures);
    CreatedHandlesSize = (SIZE_T)InputAllocationCount * sizeof(*CreatedHandles);
    Status = DxgkpCaptureUserBuffer(UserAllocationInfo, AllocationInfoSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, (PVOID *)&CapturedAllocationInfo);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkpProbeOutputBuffer(UserAllocationInfo, AllocationInfoSize, EmbeddedBufferMode);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    PrivateCaptures = ExAllocatePoolWithTag(NonPagedPool, PrivateCaptureSize, TAG_DXGK_CAPTURE);
    CreatedHandles = ExAllocatePoolWithTag(NonPagedPool, CreatedHandlesSize, TAG_DXGK_CAPTURE);
    if (PrivateCaptures == NULL || CreatedHandles == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(PrivateCaptures, PrivateCaptureSize);
    RtlZeroMemory(CreatedHandles, CreatedHandlesSize);

    Captured.hResource = InputResource;
    Captured.hGlobalShare = 0;
    if (InfoVersion == DxgkpAllocationInfoVersion2)
        Captured.pAllocationInfo2 = (D3DDDI_ALLOCATIONINFO2 *)CapturedAllocationInfo;
    else
        Captured.pAllocationInfo = (D3DDDI_ALLOCATIONINFO *)CapturedAllocationInfo;
    for (i = 0; i < InputAllocationCount; ++i)
    {
        Status = InfoVersion == DxgkpAllocationInfoVersion2 ?
            DxgkpValidateAllocationInfo2(
                &Captured.pAllocationInfo2[i],
                REACTOS_WDDM_TARGET_LEVEL,
                FALSE) :
            DxgkpValidateAllocationInfo(
                &Captured.pAllocationInfo[i],
                REACTOS_WDDM_TARGET_LEVEL,
                FALSE);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    if (StandardAllocation)
    {
        if (Captured.pStandardAllocation == NULL || Captured.PrivateDriverDataSize != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCaptureUserBuffer(Captured.pStandardAllocation, sizeof(D3DKMT_CREATESTANDARDALLOCATION), EmbeddedBufferMode, TAG_DXGK_CAPTURE, &CapturedPrivateDriverData);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (((D3DKMT_CREATESTANDARDALLOCATION *)CapturedPrivateDriverData)->Flags.Value != 0 ||
            ((D3DKMT_CREATESTANDARDALLOCATION *)CapturedPrivateDriverData)->Type < D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP ||
            ((D3DKMT_CREATESTANDARDALLOCATION *)CapturedPrivateDriverData)->Type >= D3DKMT_STANDARDALLOCATIONTYPE_MAX ||
            Captured.NumAllocations != 1 || Captured.hResource != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }
    else if (Captured.PrivateDriverDataSize != 0)
    {
        if (Captured.pPrivateDriverData == NULL || Captured.PrivateDriverDataSize > DXGKP_MAX_USER_PRIVATE_DATA)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCaptureUserBuffer(Captured.pPrivateDriverData, Captured.PrivateDriverDataSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, &CapturedPrivateDriverData);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Captured.pPrivateDriverData = CapturedPrivateDriverData;
        TotalPrivateSize = Captured.PrivateDriverDataSize;
    }
    else
    {
        Captured.pPrivateDriverData = NULL;
    }

    if (Captured.PrivateRuntimeDataSize != 0)
    {
        if (Captured.pPrivateRuntimeData == NULL || Captured.PrivateRuntimeDataSize > DXGKP_MAX_USER_PRIVATE_DATA || TotalPrivateSize > DXGKP_MAX_USER_PRIVATE_DATA - Captured.PrivateRuntimeDataSize)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCaptureUserBuffer(Captured.pPrivateRuntimeData, Captured.PrivateRuntimeDataSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, &CapturedPrivateRuntimeData);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Captured.pPrivateRuntimeData = CapturedPrivateRuntimeData;
        TotalPrivateSize += Captured.PrivateRuntimeDataSize;
    }
    else
    {
        Captured.pPrivateRuntimeData = NULL;
    }

    for (i = 0; i < InputAllocationCount; ++i)
    {
        DXGKP_ALLOCATION_INFO_VIEW AllocationInfo;

        DxgkpReadAllocationInfo(&Captured, InfoVersion, i, &AllocationInfo);
        PrivateCaptures[i].UserBuffer = AllocationInfo.pPrivateDriverData;
        PrivateCaptures[i].Size = AllocationInfo.PrivateDriverDataSize;
        DxgkpSetAllocationHandle(&Captured, InfoVersion, i, 0);
        if (EmbeddedBufferMode != KernelMode && AllocationInfo.pSystemMem != NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        if (PrivateCaptures[i].Size == 0)
        {
            DxgkpSetAllocationPrivateData(&Captured, InfoVersion, i, NULL);
            continue;
        }
        if (PrivateCaptures[i].UserBuffer == NULL || PrivateCaptures[i].Size > DXGKP_MAX_USER_PRIVATE_DATA || TotalPrivateSize > DXGKP_MAX_USER_PRIVATE_DATA - PrivateCaptures[i].Size)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCaptureUserBuffer(PrivateCaptures[i].UserBuffer, PrivateCaptures[i].Size, EmbeddedBufferMode, TAG_DXGK_CAPTURE, &PrivateCaptures[i].CapturedBuffer);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        PrivateCaptures[i].OriginalBuffer = ExAllocatePoolWithTag(NonPagedPool, PrivateCaptures[i].Size, TAG_DXGK_CAPTURE);
        if (PrivateCaptures[i].OriginalBuffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(PrivateCaptures[i].OriginalBuffer, PrivateCaptures[i].CapturedBuffer, PrivateCaptures[i].Size);
        Status = DxgkpProbeOutputBuffer(PrivateCaptures[i].UserBuffer, PrivateCaptures[i].Size, EmbeddedBufferMode);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        DxgkpSetAllocationPrivateData(&Captured, InfoVersion, i, PrivateCaptures[i].CapturedBuffer);
        TotalPrivateSize += PrivateCaptures[i].Size;
    }

    Status = DxgkpCreateAllocationCaptured(&Captured, InfoVersion);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    CoreSucceeded = TRUE;

    for (i = 0; i < InputAllocationCount; ++i)
    {
        DXGKP_ALLOCATION_INFO_VIEW AllocationInfo;

        DxgkpReadAllocationInfo(&Captured, InfoVersion, i, &AllocationInfo);
        CreatedHandles[i] = AllocationInfo.hAllocation;
    }
    if (Captured.hDevice != InputDevice || Captured.NumAllocations != InputAllocationCount || (InputCreatesResource && Captured.hResource == 0) || (!InputCreatesResource && Captured.hResource != InputResource))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Rollback;
    }
    for (i = 0; i < InputAllocationCount; ++i)
    {
        if (CreatedHandles[i] == 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Rollback;
        }
        for (j = 0; j < i; ++j)
        {
            if (CreatedHandles[j] == CreatedHandles[i])
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Rollback;
            }
        }
    }

    for (i = 0; i < InputAllocationCount; ++i)
    {
        if (PrivateCaptures[i].Size != 0)
        {
            Status = DxgkpCopyToUserBuffer(PrivateCaptures[i].UserBuffer, PrivateCaptures[i].CapturedBuffer, PrivateCaptures[i].Size, EmbeddedBufferMode);
            if (!NT_SUCCESS(Status))
                goto Rollback;
        }
    }
    for (i = 0; i < InputAllocationCount; ++i)
    {
        Status = DxgkpCopyToUserBuffer((PUCHAR)UserAllocationInfo + ((SIZE_T)i * AllocationInfoStride), &CreatedHandles[i], sizeof(CreatedHandles[i]), EmbeddedBufferMode);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }
    if (InfoVersion == DxgkpAllocationInfoVersion2)
    {
        for (i = 0; i < InputAllocationCount; ++i)
        {
            D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress = Captured.pAllocationInfo2[i].GpuVirtualAddress;

            Status = DxgkpCopyToUserBuffer((PUCHAR)UserAllocationInfo + ((SIZE_T)i * AllocationInfoStride) + FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, GpuVirtualAddress), &GpuVirtualAddress, sizeof(GpuVirtualAddress), EmbeddedBufferMode);
            if (!NT_SUCCESS(Status))
                goto Rollback;
        }
    }

    CreateAllocation->hResource = Captured.hResource;
    CreateAllocation->hGlobalShare = Captured.hGlobalShare;
    goto Cleanup;

Rollback:
    if (CoreSucceeded)
    {
        RtlZeroMemory(&DestroyAllocation, sizeof(DestroyAllocation));
        DestroyAllocation.hDevice = InputDevice;
        if (InputCreatesResource && Captured.hResource != 0)
        {
            DestroyAllocation.hResource = Captured.hResource;
            CleanupStatus = DxgkDestroyAllocation(&DestroyAllocation);
        }
        else
        {
            RollbackCount = 0;
            for (i = 0; i < InputAllocationCount; ++i)
            {
                if (CreatedHandles[i] == 0)
                    continue;
                for (j = 0; j < RollbackCount && CreatedHandles[j] != CreatedHandles[i]; ++j)
                    NOTHING;
                if (j == RollbackCount)
                    CreatedHandles[RollbackCount++] = CreatedHandles[i];
            }
            DestroyAllocation.phAllocationList = CreatedHandles;
            DestroyAllocation.AllocationCount = RollbackCount;
            CleanupStatus = RollbackCount == 0 ? STATUS_SUCCESS : DxgkDestroyAllocation(&DestroyAllocation);
        }
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("DxgkCreateAllocation: publication rollback failed with 0x%08lX\n", CleanupStatus);
        for (i = 0; i < InputAllocationCount; ++i)
        {
            if (PrivateCaptures[i].OriginalBuffer != NULL)
                (VOID)DxgkpCopyToUserBuffer(PrivateCaptures[i].UserBuffer, PrivateCaptures[i].OriginalBuffer, PrivateCaptures[i].Size, EmbeddedBufferMode);
        }
        DxgkpScrubCreateAllocationOutputs(CreateAllocation, UserAllocationInfo, InputAllocationCount, AllocationInfoStride, InfoVersion, EmbeddedBufferMode, InputCreatesResource ? 0 : InputResource);
    }

Cleanup:
    if (PrivateCaptures != NULL)
    {
        for (i = 0; i < InputAllocationCount; ++i)
        {
            if (PrivateCaptures[i].OriginalBuffer != NULL)
                ExFreePoolWithTag(PrivateCaptures[i].OriginalBuffer, TAG_DXGK_CAPTURE);
            if (PrivateCaptures[i].CapturedBuffer != NULL)
                ExFreePoolWithTag(PrivateCaptures[i].CapturedBuffer, TAG_DXGK_CAPTURE);
        }
        ExFreePoolWithTag(PrivateCaptures, TAG_DXGK_CAPTURE);
    }
    if (CapturedPrivateRuntimeData != NULL)
        ExFreePoolWithTag(CapturedPrivateRuntimeData, TAG_DXGK_CAPTURE);
    if (CapturedPrivateDriverData != NULL)
        ExFreePoolWithTag(CapturedPrivateDriverData, TAG_DXGK_CAPTURE);
    if (CapturedAllocationInfo != NULL)
        ExFreePoolWithTag(CapturedAllocationInfo, TAG_DXGK_CAPTURE);
    if (CreatedHandles != NULL)
        ExFreePoolWithTag(CreatedHandles, TAG_DXGK_CAPTURE);
    return Status;
}

NTSTATUS
NTAPI
DxgkpCreateAllocationWithAccessMode(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    return DxgkpCreateAllocationWithAccessModeVariant(CreateAllocation, EmbeddedBufferMode, DxgkpAllocationInfoVersion1);
}

NTSTATUS
NTAPI
DxgkpCreateAllocation2WithAccessMode(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    return DxgkpCreateAllocationWithAccessModeVariant(CreateAllocation, EmbeddedBufferMode, DxgkpAllocationInfoVersion2);
}

NTSTATUS
DxgkCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation)
{
    return DxgkpCreateAllocationWithAccessMode(CreateAllocation, KernelMode);
}

NTSTATUS
DxgkCreateAllocation2(
    _Inout_ D3DKMT_CREATEALLOCATION *CreateAllocation)
{
    return DxgkpCreateAllocation2WithAccessMode(CreateAllocation, KernelMode);
}

NTSTATUS
DxgkDestroyAllocation(
    _In_ CONST D3DKMT_DESTROYALLOCATION *pDestroyAllocation)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    D3DKMT_HANDLE *AllocationHandles = NULL;
    SIZE_T AllocationListSize = 0;
    BOOLEAN KmdTransactionStarted = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pDestroyAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pDestroyAllocation->hResource == 0 && (pDestroyAllocation->AllocationCount == 0 || pDestroyAllocation->phAllocationList == NULL))
        return STATUS_INVALID_PARAMETER;
    if (pDestroyAllocation->hResource == 0)
    {
        if ((SIZE_T)pDestroyAllocation->AllocationCount > MAXULONG_PTR / sizeof(*AllocationHandles))
            return STATUS_INTEGER_OVERFLOW;
        AllocationListSize = (SIZE_T)pDestroyAllocation->AllocationCount * sizeof(*AllocationHandles);
        AllocationHandles = ExAllocatePoolWithTag(NonPagedPool, AllocationListSize, TAG_VIDMM_ALLOC);
        if (AllocationHandles == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        _SEH2_TRY
        {
            RtlCopyMemory(AllocationHandles, pDestroyAllocation->phAllocationList, AllocationListSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Device = DxgkpVidMmFindDeviceByHandle(pDestroyAllocation->hDevice, &Adapter);
    if (Device == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    KmdTransactionStarted = TRUE;

    if (pDestroyAllocation->hResource != 0)
    {
        Status = DxgkVidMmReferenceResource(pDestroyAllocation->hResource, FALSE, Device, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
    }

    if (Resource != NULL)
    {
        Status = DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);
        DxgkVidMmDereferenceResource(Resource);
        Resource = NULL;
        goto Cleanup;
    }

    Status = DxgkpVidMmDestroyAllocationList(Adapter, Device, AllocationHandles, pDestroyAllocation->AllocationCount, FALSE);

Cleanup:
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    if (AllocationHandles != NULL)
        ExFreePoolWithTag(AllocationHandles, TAG_VIDMM_ALLOC);
    if (KmdTransactionStarted)
        DxgkEndKmdTransaction(Adapter);
    if (Device != NULL)
        DxgkDereferenceDevice(Device);
    return Status;
}


/* SEGMENT PLACEMENT **********************************************************/

/*
 * DxgkVidMmTryPlaceInSegment
 *
 * Asks dxgmms2, which owns segment space, for an offset for this allocation
 * and turns the answer into a physical address.  The placement policy and
 * the commit ledger live in the owner; dxgkrnl keeps only the index from an
 * offset back to the allocation object.
 */
NTSTATUS
DxgkVidMmTryPlaceInSegment(
    _In_ PDXGKRNL_SEGMENT       Segment,
    _In_ PDXGKVMM_ALLOCATION    Allocation)
{
    PDXGMMS2_VIDMM_INTERFACE_V1 VidMm;
    DXGMMS2_VIDMM_RESERVE_INFO_V1 Info;
    ULONGLONG Offset = 0;
    ULONGLONG Limit;
    NTSTATUS Status;

    ASSERT(Segment != NULL);
    ASSERT(Allocation != NULL);
    ASSERT(Allocation->Size != 0);
    ASSERT(Allocation->Alignment != 0);

    VidMm = DxgkpVidMmOwner(Allocation->Adapter);
    if (VidMm == NULL)
        return STATUS_DEVICE_NOT_READY;

    Limit = VidMmSegmentPlacementLimit(Segment);
    if ((ULONGLONG)Segment->BaseAddress.QuadPart > MAXULONGLONG - Limit)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    /*
     * dxgmms2 decides where this lands.  dxgkrnl supplies the request and
     * turns the answer into an address; it keeps no second copy of segment
     * occupancy, so there is exactly one ledger.
     */
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = VidMmAllocationSizeForSegment(Allocation, Segment);
    Info.Alignment = (ULONGLONG)Allocation->Alignment;
    Info.OwnerCookie = (ULONGLONG)(ULONG_PTR)Allocation;
    Info.Priority = (LONG)Allocation->AllocationPriority;
    if (Allocation->Offered)
        Info.Flags |= DXGMMS2_VIDMM_RANGE_OFFERED;
    if (DxgkSubmissionResidencyPinIsHeld(&Allocation->SubmissionResidencyPinCount))
        Info.Flags |= DXGMMS2_VIDMM_RANGE_PINNED;
    if (InterlockedCompareExchange(&Allocation->ResidencyReferenceCount, 0, 0) != 0)
        Info.Flags |= DXGMMS2_VIDMM_RANGE_RESIDENCY_REFERENCED;

    Status = VidMm->ReservePlacement(VidMm->VidMmHandle, Segment->SegmentId - 1, &Info, &Offset);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ULONGLONG)Segment->BaseAddress.QuadPart > MAXULONGLONG - Offset)
    {
        (VOID)VidMm->ReleasePlacement(VidMm->VidMmHandle, Segment->SegmentId - 1, Info.OwnerCookie);
        return STATUS_INTEGER_OVERFLOW;
    }

    Allocation->PhysicalAddress.QuadPart = (LONGLONG)((ULONGLONG)Segment->BaseAddress.QuadPart + Offset);
    Allocation->SegmentId = Segment->SegmentId;
    Allocation->SegmentOffset = Offset;
    Allocation->Resident = TRUE;

    /*
     * The segment list stays as dxgkrnl's index from an offset back to the
     * allocation object; it is not the allocator and does not gate placement.
     */
    ExAcquireFastMutex(&Segment->Lock);
    {
        PLIST_ENTRY Entry;
        PLIST_ENTRY InsertBefore = &Segment->AllocationList;

        for (Entry = Segment->AllocationList.Blink;
             Entry != &Segment->AllocationList;
             Entry = Entry->Blink)
        {
            PDXGKVMM_ALLOCATION Existing =
                CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, SegmentEntry);

            if (Existing->SegmentOffset <= Offset)
            {
                InsertBefore = Entry->Flink;
                break;
            }
        }
        InsertTailList(InsertBefore, &Allocation->SegmentEntry);
    }
    ExReleaseFastMutex(&Segment->Lock);

    DPRINT("DxgkVidMmTryPlaceInSegment: alloc %p placed in seg %lu offset=0x%I64x\n",
           Allocation, Segment->SegmentId, Offset);
    return STATUS_SUCCESS;
}


static VOID
DxgkpVidMmReleaseSegmentPlacement(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment;
    PDXGMMS2_VIDMM_INTERFACE_V1 VidMm;

    if (Allocation == NULL || !Allocation->Resident)
        return;

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    DxgkpVidMmReleaseAllAllocationBudgetCharges(Allocation);
#endif

    Adapter = Allocation->Adapter;
    if (Adapter == NULL ||
        Adapter->Segments == NULL ||
        Allocation->SegmentId == 0 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        Allocation->Resident = FALSE;
        Allocation->SegmentId = 0;
        Allocation->SegmentOffset = 0;
        Allocation->PhysicalAddress.QuadPart = 0;
        return;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];

    ExAcquireFastMutex(&Segment->Lock);
    RemoveEntryList(&Allocation->SegmentEntry);
    InitializeListHead(&Allocation->SegmentEntry);
    ExReleaseFastMutex(&Segment->Lock);

    /* dxgmms2 reclaims the space; dxgkrnl only reports that it is done. */
    VidMm = DxgkpVidMmOwner(Adapter);
    if (VidMm != NULL)
        (VOID)VidMm->ReleasePlacement(VidMm->VidMmHandle, Allocation->SegmentId - 1, (ULONGLONG)(ULONG_PTR)Allocation);

    Allocation->Resident = FALSE;
    Allocation->SegmentId = 0;
    Allocation->SegmentOffset = 0;
    Allocation->PhysicalAddress.QuadPart = 0;
}


/* EVICTION AND RESIDENCY *****************************************************/

/*
 * DxgkVidMmEvict
 *
 * Moves an allocation from its current GPU segment to system memory.
 *
 * This is a "soft eviction": we allocate a NonPagedPool backing store and
 * remove the allocation from the segment list.  A full implementation would
 * submit a DxgkDdiBuildPagingBuffer(DXGK_OPERATION_TRANSFER) to the GPU
 * via dxgmms1 to copy VRAM→system before releasing the VRAM range.
 *
 * Soft eviction is safe when the GPU has already finished using the
 * allocation (guaranteed by the caller through fence/sync completion).
 */
static NTSTATUS
DxgkpVidMmPrepareEvictionOwned(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PBOOLEAN OutApertureUnmapRequired)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment;
    SIZE_T BackingSize;
    NTSTATUS Status;

    *OutApertureUnmapRequired = FALSE;
    DPRINT("DxgkVidMmEvict: Alloc=%p seg=%lu offset=0x%I64x\n", Allocation, Allocation->SegmentId, Allocation->SegmentOffset);

    if (!Allocation->Resident)
    {
        DPRINT1("DxgkVidMmEvict: allocation %p is not resident\n", Allocation);
        return STATUS_INVALID_PARAMETER;
    }
    if (DxgkSubmissionResidencyPinIsHeld(&Allocation->SubmissionResidencyPinCount))
        return STATUS_DEVICE_BUSY;

    Adapter = Allocation->Adapter;
    if (Adapter == NULL || Adapter->Segments == NULL)
    {
        DPRINT1("DxgkVidMmEvict: null adapter or segment table for allocation %p\n", Allocation);
        return STATUS_INVALID_PARAMETER;
    }
    if (Allocation->SegmentId < 1 || Allocation->SegmentId > Adapter->SegmentCount)
    {
        DPRINT1("DxgkVidMmEvict: SegmentId %lu out of range for allocation %p\n", Allocation->SegmentId, Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (Segment->Flags.PitchAlignment)
        return STATUS_NOT_SUPPORTED;
    if (!VidMmSegmentIsAperture(Segment) && Allocation->UserModeAddress != NULL)
    {
        DPRINT1("DxgkVidMmEvict: refusing user-mapped alloc %p (seg %lu off 0x%I64x)\n", Allocation, Allocation->SegmentId, Allocation->SegmentOffset);
        return STATUS_DEVICE_BUSY;
    }

    if (VidMmSegmentIsAperture(Segment))
    {
        if (Allocation->SystemMemory == NULL)
            return STATUS_INVALID_DEVICE_STATE;
        *OutApertureUnmapRequired = Allocation->ApertureMapped;
        return STATUS_SUCCESS;
    }

    if (Allocation->SystemMemory == NULL)
    {
        if (!DxgkpVidMmRoundUpPageSize(Allocation->Size, &BackingSize))
            return STATUS_INTEGER_OVERFLOW;
        Allocation->SystemMemory = ExAllocatePoolWithTag(NonPagedPool, BackingSize, TAG_VIDMM_ALLOC);
        if (Allocation->SystemMemory == NULL)
        {
            DPRINT1("DxgkVidMmEvict: cannot allocate backing store for allocation %p (%Iu bytes)\n", Allocation, BackingSize);
            return STATUS_NO_MEMORY;
        }
        RtlZeroMemory(Allocation->SystemMemory, BackingSize);
    }

    Status = DxgkpVidMmTransferAllocationContent(Adapter, Allocation, FALSE);
    return Status;
}

static VOID
DxgkpVidMmCommitPreparedEvictionOwned(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation->ApertureMdl != NULL)
    {
        IoFreeMdl(Allocation->ApertureMdl);
        Allocation->ApertureMdl = NULL;
    }
    Allocation->ApertureMapped = FALSE;
    if (Allocation->CpuAddress != NULL &&
        Allocation->CpuAddress != Allocation->SystemMemory)
    {
        DxgkVidMmUnmapAllocationCpu(Allocation);
    }

    DxgkpVidMmReleaseSegmentPlacement(Allocation);
    Allocation->PhysicalAddress = MmGetPhysicalAddress(Allocation->SystemMemory);
    Allocation->ContentLost = FALSE;
    DPRINT("DxgkVidMmEvict: alloc %p evicted to VA=%p PA=0x%I64x\n", Allocation, Allocation->SystemMemory, Allocation->PhysicalAddress.QuadPart);
}

static NTSTATUS
DxgkpVidMmEvictOwned(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    DXGKRNL_PAGING_OP Op;
    ULONG PagingFenceId = 0;
    BOOLEAN ApertureUnmapRequired;
    BOOLEAN Queued = FALSE;
    NTSTATUS Status;

    Status = DxgkpVidMmPrepareEvictionOwned(
                 Allocation,
                 &ApertureUnmapRequired);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ApertureUnmapRequired)
    {
        Status = DxgkpVidMmFillAperturePagingOperation(Allocation,
                                                      FALSE,
                                                      &Op);
        if (NT_SUCCESS(Status))
        {
            Status = DxgkPagingExecuteBatch(Allocation->Adapter,
                                            Allocation->Device,
                                            &Op,
                                            1,
                                            &Allocation,
                                            1,
                                            0,
                                            0,
                                            &PagingFenceId,
                                            &Queued);
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }

    DxgkpVidMmCommitPreparedEvictionOwned(Allocation);
    /*
     * Admission is the transaction's commit point.  The tracked packet owns a
     * lifetime-only allocation reference, so the miniport handle survives
     * until terminal cleanup.  Keep the newest paging fence on the allocation
     * as compensating state instead of returning a timeout after an unmap that
     * can no longer be withdrawn.
     */
    if (ApertureUnmapRequired && Queued)
        DxgkPagingBeginPlacement(Allocation, 0, 0, PagingFenceId);
    return STATUS_SUCCESS;
}

/*
 * Complete the semantic half of evicting an offered allocation.
 *
 * A normal offer may lose its contents but keeps a valid backing store.
 * WDDM 2.1 AllowDecommit additionally permits VidMm to release pool-owned
 * backing. Existing-heap pages and live user mappings remain committed: they
 * are owned or mapped outside VidMm and cannot safely be discarded here.
 */
static VOID
DxgkpVidMmCompleteOfferedEvictionOwned(
    _Inout_ PDXGKVMM_ALLOCATION Allocation)
{
    BOOLEAN AllowDecommit = FALSE;

    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    if (Allocation->Offered)
    {
        Allocation->OfferDiscarded = TRUE;
        AllowDecommit = Allocation->OfferAllowDecommit;
    }
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);

    if (!AllowDecommit ||
        Allocation->Resident ||
        Allocation->SysMemMdl != NULL ||
        Allocation->SystemMemory == NULL ||
        Allocation->UserModeAddress != NULL ||
        (Allocation->CpuAddress != NULL &&
         Allocation->CpuAddress != Allocation->SystemMemory))
    {
        return;
    }

    if (Allocation->CpuAddress == Allocation->SystemMemory)
        Allocation->CpuAddress = NULL;
    DxgkpVidMmReleaseSystemBacking(Allocation);
    Allocation->PhysicalAddress.QuadPart = 0;

    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    if (Allocation->Offered)
        Allocation->OfferDecommitted = TRUE;
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
}

/* ========================================================================
 * Per-device residency references
 *
 * MakeResident charges the calling device; Evict may only release what that
 * device holds.  Short mutations own Allocation->ResidencyLock.  Operations
 * that must call the miniport or wait for paging instead publish a stable
 * transaction owner, release the lock, and clear/signal the owner at their
 * single terminal edge.
 * ====================================================================== */

static NTSTATUS
DxgkpVidMmBeginResidencyTransaction(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PVOID Owner)
{
    NTSTATUS Status;

    if (Allocation == NULL || Owner == NULL)
        return STATUS_INVALID_PARAMETER;

    for (;;)
    {
        Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Allocation->ResidencyTransactionOwner == NULL)
        {
            BOOLEAN Acquired;

            if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
            {
                KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
                return STATUS_DELETE_PENDING;
            }
            Acquired = DxgkResidencyCoreTransactionTryAcquire(
                           &Allocation->ResidencyTransactionOwner,
                           Owner);
            ASSERT(Acquired);
            if (!Acquired)
            {
                KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
                continue;
            }
            KeClearEvent(&Allocation->ResidencyTransactionEvent);
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            return STATUS_SUCCESS;
        }
        if (Allocation->ResidencyTransactionOwner == Owner)
        {
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            return STATUS_SUCCESS;
        }
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);

        Status = KeWaitForSingleObject(&Allocation->ResidencyTransactionEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }
}

/*
 * Victim selection is opportunistic.  Never wait for a victim transaction
 * while this MakeResident transaction owns another allocation: a concurrent
 * sorted batch can own the victim and be waiting for ours.  Failing the
 * zero-time claim simply makes this candidate unavailable for this pass.
 */
static NTSTATUS
DxgkpVidMmTryBeginResidencyTransaction(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PVOID Owner)
{
    LARGE_INTEGER ZeroTimeout;
    BOOLEAN Acquired;
    NTSTATUS Status;

    if (Allocation == NULL || Owner == NULL)
        return STATUS_INVALID_PARAMETER;

    ZeroTimeout.QuadPart = 0;
    Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   &ZeroTimeout);
    if (Status == STATUS_TIMEOUT)
        return STATUS_DEVICE_BUSY;
    if (!NT_SUCCESS(Status))
        return Status;
    if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
    {
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        return STATUS_DELETE_PENDING;
    }
    if (Allocation->ResidencyTransactionOwner != NULL)
    {
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        return STATUS_DEVICE_BUSY;
    }

    Acquired = DxgkResidencyCoreTransactionTryAcquire(
                   &Allocation->ResidencyTransactionOwner,
                   Owner);
    ASSERT(Acquired);
    if (!Acquired)
    {
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        return STATUS_DEVICE_BUSY;
    }
    KeClearEvent(&Allocation->ResidencyTransactionEvent);
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return STATUS_SUCCESS;
}

static VOID
DxgkpVidMmEndResidencyTransaction(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PVOID Owner)
{
    NTSTATUS Status;

    ASSERT(Allocation != NULL);
    ASSERT(Owner != NULL);
    Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    ASSERT(NT_SUCCESS(Status));
    if (!NT_SUCCESS(Status))
        return;
    ASSERT(Allocation->ResidencyTransactionOwner == Owner);
    if (Allocation->ResidencyTransactionOwner == Owner)
    {
        BOOLEAN Released =
            DxgkResidencyCoreTransactionRelease(
                &Allocation->ResidencyTransactionOwner,
                Owner);

        ASSERT(Released);
        if (Released)
        {
            KeSetEvent(&Allocation->ResidencyTransactionEvent,
                       IO_NO_INCREMENT,
                       FALSE);
        }
    }
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
}

/*
 * Return with ResidencyLock owned at a point where no callback-spanning
 * transaction is active.  The event is notification-style, and it is cleared
 * while ResidencyLock is held, so a waiter cannot miss the terminal signal.
 */
static NTSTATUS
DxgkpVidMmLockResidencyForExternalOperation(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    NTSTATUS Status;

    for (;;)
    {
        Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Allocation->ResidencyTransactionOwner == NULL)
            return STATUS_SUCCESS;
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);

        Status = KeWaitForSingleObject(&Allocation->ResidencyTransactionEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }
}

static PDXGKVMM_RESIDENCY_REF
DxgkpVidMmFindResidencyReferenceLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    PLIST_ENTRY Entry;

    if (Allocation->ResidencyReferenceList.Flink == NULL)
        return NULL;
    for (Entry = Allocation->ResidencyReferenceList.Flink;
         Entry != &Allocation->ResidencyReferenceList;
         Entry = Entry->Flink)
    {
        PDXGKVMM_RESIDENCY_REF Ref = CONTAINING_RECORD(Entry, DXGKVMM_RESIDENCY_REF, Entry);

        if (Ref->Device == Device)
            return Ref;
    }
    return NULL;
}

static NTSTATUS
DxgkpVidMmAcquireDeviceResidencyReferenceLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    PDXGKVMM_RESIDENCY_REF Ref;
    LONG TotalReferences;
    BOOLEAN CreatedRef = FALSE;

    if (Allocation->ResidencyReferenceList.Flink == NULL)
        InitializeListHead(&Allocation->ResidencyReferenceList);
    Ref = DxgkpVidMmFindResidencyReferenceLocked(Allocation, Device);
    if (Ref == NULL)
    {
        Ref = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Ref), TAG_VIDMM_ALLOC);
        if (Ref == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Ref, sizeof(*Ref));
        Ref->Device = Device;
        InsertTailList(&Allocation->ResidencyReferenceList, &Ref->Entry);
        CreatedRef = TRUE;
    }
    TotalReferences = InterlockedCompareExchange(&Allocation->ResidencyReferenceCount, 0, 0);
    if (Ref->Count == MAXLONG || TotalReferences < 0 || TotalReferences == MAXLONG)
    {
        if (CreatedRef)
        {
            RemoveEntryList(&Ref->Entry);
            ExFreePoolWithTag(Ref, TAG_VIDMM_ALLOC);
        }
        return STATUS_INTEGER_OVERFLOW;
    }
    Ref->Count++;
    InterlockedIncrement(&Allocation->ResidencyReferenceCount);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpVidMmAcquireDeviceResidencyReferencesLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_ ULONG Count)
{
    PDXGKVMM_RESIDENCY_REF Ref;
    LONG TotalReferences;
    BOOLEAN CreatedRef = FALSE;

    if (Count == 0 || Count > MAXLONG)
        return STATUS_INVALID_PARAMETER;
    if (Allocation->ResidencyReferenceList.Flink == NULL)
        InitializeListHead(&Allocation->ResidencyReferenceList);
    Ref = DxgkpVidMmFindResidencyReferenceLocked(Allocation, Device);
    if (Ref == NULL)
    {
        Ref = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Ref), TAG_VIDMM_ALLOC);
        if (Ref == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Ref, sizeof(*Ref));
        Ref->Device = Device;
        InsertTailList(&Allocation->ResidencyReferenceList, &Ref->Entry);
        CreatedRef = TRUE;
    }

    TotalReferences = InterlockedCompareExchange(&Allocation->ResidencyReferenceCount, 0, 0);
    if (Ref->Count < 0 ||
        TotalReferences < 0 ||
        Count > (ULONG)(MAXLONG - Ref->Count) ||
        Count > (ULONG)(MAXLONG - TotalReferences))
    {
        if (CreatedRef)
        {
            RemoveEntryList(&Ref->Entry);
            ExFreePoolWithTag(Ref, TAG_VIDMM_ALLOC);
        }
        return STATUS_INTEGER_OVERFLOW;
    }

    Ref->Count += (LONG)Count;
    InterlockedExchangeAdd(&Allocation->ResidencyReferenceCount, (LONG)Count);
    return STATUS_SUCCESS;
}

static LONG
DxgkpVidMmQueryDeviceResidencyReferenceCountLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    PDXGKVMM_RESIDENCY_REF Ref;
    LONG Count;

    Ref = DxgkpVidMmFindResidencyReferenceLocked(Allocation, Device);
    Count = Ref != NULL ? Ref->Count : 0;
    if (Allocation->ImplicitResidencyReference && Device == Allocation->Device)
        Count++;
    return Count;
}

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static ULONG
DxgkpVidMmQueryProcessResidencyReferenceCountLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    if (Allocation->ResidencyReferenceList.Flink != NULL)
    {
        for (Entry = Allocation->ResidencyReferenceList.Flink;
             Entry != &Allocation->ResidencyReferenceList;
             Entry = Entry->Flink)
        {
            PDXGKVMM_RESIDENCY_REF Ref =
                CONTAINING_RECORD(Entry, DXGKVMM_RESIDENCY_REF, Entry);

            if (Ref->Device == NULL ||
                Ref->Device->OwnerProcess != Process)
            {
                continue;
            }
            ASSERT(Ref->Count >= 0);
            ASSERT((ULONG)Ref->Count <= MAXULONG - Count);
            if (Ref->Count > 0 && (ULONG)Ref->Count <= MAXULONG - Count)
                Count += (ULONG)Ref->Count;
        }
    }
    if (Allocation->ImplicitResidencyReference &&
        Allocation->Device != NULL &&
        Allocation->Device->OwnerProcess == Process)
    {
        ASSERT(Count != MAXULONG);
        if (Count != MAXULONG)
            Count++;
    }
    return Count;
}
#endif

static BOOLEAN
DxgkpVidMmReleaseExplicitResidencyReferencesLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_ ULONG Count,
    _Out_opt_ PBOOLEAN OutReachedZero)
{
    PDXGKVMM_RESIDENCY_REF Ref;
    LONG PreviousTotal;

    if (OutReachedZero != NULL)
        *OutReachedZero = FALSE;
    if (Count == 0 || Count > MAXLONG)
        return FALSE;
    Ref = DxgkpVidMmFindResidencyReferenceLocked(Allocation, Device);
    if (Ref == NULL || Ref->Count < (LONG)Count)
        return FALSE;

    Ref->Count -= (LONG)Count;
    if (Ref->Count == 0)
    {
        RemoveEntryList(&Ref->Entry);
        ExFreePoolWithTag(Ref, TAG_VIDMM_ALLOC);
    }
    PreviousTotal = InterlockedExchangeAdd(&Allocation->ResidencyReferenceCount,
                                           -(LONG)Count);
    ASSERT(PreviousTotal >= (LONG)Count);
    if (OutReachedZero != NULL && PreviousTotal == (LONG)Count)
        *OutReachedZero = TRUE;
    return TRUE;
}

NTSTATUS
DxgkVidMmAcquireDeviceResidencyReference(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    NTSTATUS Status;

    if (Allocation == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkpVidMmAcquireDeviceResidencyReferenceLocked(Allocation, Device);
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return Status;
}

LONG
DxgkVidMmQueryDeviceResidencyReferenceCount(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    LONG Count;
    NTSTATUS Status;

    if (Allocation == NULL)
        return 0;
    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return 0;
    Count = DxgkpVidMmQueryDeviceResidencyReferenceCountLocked(Allocation, Device);
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return Count;
}

BOOLEAN
DxgkVidMmDeviceHoldsResidencyReference(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    return DxgkVidMmQueryDeviceResidencyReferenceCount(Allocation, Device) > 0;
}

static BOOLEAN
DxgkpVidMmReleaseDeviceResidencyReferenceLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _Out_ PBOOLEAN OutReachedZero)
{
    PDXGKVMM_RESIDENCY_REF Ref;

    if (OutReachedZero != NULL)
        *OutReachedZero = FALSE;
    Ref = DxgkpVidMmFindResidencyReferenceLocked(Allocation, Device);
    if (Ref == NULL || Ref->Count == 0)
    {
        /* Fall back to the created-resident reference the owning device
         * still holds. */
        if (!Allocation->ImplicitResidencyReference || Device != Allocation->Device)
            return FALSE;
        Allocation->ImplicitResidencyReference = FALSE;
        if (InterlockedDecrement(&Allocation->ResidencyReferenceCount) == 0 && OutReachedZero != NULL)
            *OutReachedZero = TRUE;
        return TRUE;
    }
    Ref->Count--;
    if (Ref->Count == 0)
    {
        RemoveEntryList(&Ref->Entry);
        ExFreePoolWithTag(Ref, TAG_VIDMM_ALLOC);
    }
    if (InterlockedDecrement(&Allocation->ResidencyReferenceCount) == 0 && OutReachedZero != NULL)
        *OutReachedZero = TRUE;
    return TRUE;
}

BOOLEAN
DxgkVidMmReleaseDeviceResidencyReference(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _Out_ PBOOLEAN OutReachedZero)
{
    BOOLEAN Released;
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    NTSTATUS BudgetStatus;
#endif

    if (OutReachedZero != NULL)
        *OutReachedZero = FALSE;
    if (Allocation == NULL)
        return FALSE;
    if (!NT_SUCCESS(DxgkpVidMmLockResidencyForExternalOperation(Allocation)))
        return FALSE;
    Released = DxgkpVidMmReleaseDeviceResidencyReferenceLocked(Allocation, Device, OutReachedZero);
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (Released && Device != NULL && Device->OwnerProcess != NULL)
    {
        BudgetStatus = DxgkpVidMmReleaseProcessBudgetReferences(
                           Allocation,
                           Device->OwnerProcess,
                           1);
        ASSERT(NT_SUCCESS(BudgetStatus) ||
               BudgetStatus == STATUS_NOT_FOUND);
    }
#endif
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return Released;
}

VOID
DxgkVidMmReleaseAllDeviceResidencyReferences(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    PDXGKVMM_RESIDENCY_REF Ref;
    ULONG ReleasedReferences = 0;
    NTSTATUS Status;

    if (Allocation == NULL)
        return;
    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return;
    Ref = DxgkpVidMmFindResidencyReferenceLocked(Allocation, Device);
    if (Ref != NULL)
    {
        LONG Count = Ref->Count;
        LONG PreviousCount;

        RemoveEntryList(&Ref->Entry);
        ExFreePoolWithTag(Ref, TAG_VIDMM_ALLOC);
        PreviousCount = InterlockedExchangeAdd(&Allocation->ResidencyReferenceCount, -Count);
        ASSERT(Count >= 0 && PreviousCount >= Count);
        if (Count > 0)
            ReleasedReferences += (ULONG)Count;
    }
    if (Allocation->ImplicitResidencyReference && Device == Allocation->Device)
    {
        LONG References;

        Allocation->ImplicitResidencyReference = FALSE;
        References = InterlockedDecrement(&Allocation->ResidencyReferenceCount);
        ASSERT(References >= 0);
        ReleasedReferences++;
    }
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (ReleasedReferences != 0 &&
        Device != NULL &&
        Device->OwnerProcess != NULL)
    {
        Status = DxgkpVidMmReleaseProcessBudgetReferences(
                     Allocation,
                     Device->OwnerProcess,
                     ReleasedReferences);
        ASSERT(NT_SUCCESS(Status) || Status == STATUS_NOT_FOUND);
    }
#endif
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
}

NTSTATUS
DxgkVidMmEvict(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    ULONG OwnerToken;
    NTSTATUS Status;

    if (Allocation == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpVidMmBeginResidencyTransaction(Allocation, &OwnerToken);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkpVidMmEvictOwned(Allocation);
    if (NT_SUCCESS(Status))
        DxgkpVidMmCompleteOfferedEvictionOwned(Allocation);
    DxgkpVidMmEndResidencyTransaction(Allocation, &OwnerToken);
    return Status;
}

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
/*
 * Flush at most this much with one temporary MDL.  The public range is
 * SIZE_T-wide, but an MDL's ByteCount and IoAllocateMdl length are ULONG.
 * Chunking therefore supports the full allocation range without allocating
 * an unbounded PFN array.
 */
#define DXGKP_VIDMM_CACHE_FLUSH_CHUNK_MAX (16U * 1024U * 1024U)

NTSTATUS
DxgkVidMmInvalidateAllocationCache(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE Handle,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length)
{
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKRNL_SEGMENT Segment = NULL;
    ULONGLONG CurrentOffset;
    ULONGLONG Remaining;
    BOOLEAN CachedBacking = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Device == NULL || Handle == 0 || Length == 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceAllocation(
                 (HANDLE)(ULONG_PTR)Handle,
                 Adapter,
                 Device,
                 &Allocation);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_HANDLE;

    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        goto CleanupReference;

    /*
     * A queued paging transfer leaves the old placement authoritative until
     * its fence retires.  Do not clean one backing while the GPU is switching
     * to another one.
     */
    if (!DxgkPagingSyncPlacement(Allocation))
    {
        Status = STATUS_GRAPHICS_ALLOCATION_BUSY;
        goto CleanupLock;
    }
    if (Allocation->ContentLost)
    {
        Status = STATUS_GRAPHICS_ALLOCATION_CONTENT_LOST;
        goto CleanupLock;
    }
    if (Offset > (ULONGLONG)Allocation->Size ||
        Length > (ULONGLONG)Allocation->Size - Offset)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto CleanupLock;
    }

    if (!Allocation->Resident)
    {
        CachedBacking = Allocation->SystemMemory != NULL;
    }
    else
    {
        if (Adapter->Segments == NULL ||
            Allocation->SegmentId == 0 ||
            Allocation->SegmentId > Adapter->SegmentCount)
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto CleanupLock;
        }

        Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
        CachedBacking =
            VidMmSegmentIsAperture(Segment) &&
            Allocation->SystemMemory != NULL;
    }

    /*
     * Non-aperture GPU memory is exposed through MmWriteCombined mappings,
     * never through the CPU cache.  It needs ordering, but has no dirty cache
     * lines to write back or stale cache lines to invalidate.
     */
    if (!CachedBacking)
    {
        if (Allocation->Resident && Segment != NULL &&
            !VidMmSegmentIsAperture(Segment))
        {
            KeMemoryBarrier();
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_INVALID_DEVICE_STATE;
        }
        goto CleanupLock;
    }

    CurrentOffset = Offset;
    Remaining = Length;
    while (Remaining != 0)
    {
        PVOID MdlAddress;
        PMDL Mdl;
        ULONG ChunkLength =
            Remaining > DXGKP_VIDMM_CACHE_FLUSH_CHUNK_MAX
                ? DXGKP_VIDMM_CACHE_FLUSH_CHUNK_MAX
                : (ULONG)Remaining;

        if (CurrentOffset > MAXULONG_PTR ||
            (ULONG_PTR)Allocation->SystemMemory >
                MAXULONG_PTR - (ULONG_PTR)CurrentOffset)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto CleanupLock;
        }

        if (Allocation->SysMemMdl != NULL)
        {
            PVOID SourceAddress = MmGetMdlVirtualAddress(
                                      Allocation->SysMemMdl);

            if (SourceAddress == NULL ||
                CurrentOffset > MmGetMdlByteCount(Allocation->SysMemMdl) ||
                ChunkLength >
                    MmGetMdlByteCount(Allocation->SysMemMdl) -
                        (ULONG)CurrentOffset ||
                (ULONG_PTR)SourceAddress >
                    MAXULONG_PTR - (ULONG_PTR)CurrentOffset)
            {
                Status = STATUS_INVALID_DEVICE_STATE;
                goto CleanupLock;
            }

            MdlAddress = (PVOID)((ULONG_PTR)SourceAddress +
                                 (ULONG_PTR)CurrentOffset);
            Mdl = IoAllocateMdl(
                      MdlAddress,
                      ChunkLength,
                      FALSE,
                      FALSE,
                      NULL);
            if (Mdl != NULL)
            {
                IoBuildPartialMdl(
                    Allocation->SysMemMdl,
                    Mdl,
                    MdlAddress,
                    ChunkLength);
            }
        }
        else
        {
            MdlAddress =
                (PVOID)((ULONG_PTR)Allocation->SystemMemory +
                        (ULONG_PTR)CurrentOffset);
            Mdl = IoAllocateMdl(
                      MdlAddress,
                      ChunkLength,
                      FALSE,
                      FALSE,
                      NULL);
            if (Mdl != NULL)
                MmBuildMdlForNonPagedPool(Mdl);
        }

        if (Mdl == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto CleanupLock;
        }

        /*
         * ReadOperation=TRUE performs clean+invalidate on non-coherent ARM64
         * and is a coherent no-op on x86/amd64.  The final barrier also orders
         * write-combined/coherent machines where KeFlushIoBuffers is a macro.
         */
        KeFlushIoBuffers(Mdl, TRUE, TRUE);
        IoFreeMdl(Mdl);

        CurrentOffset += ChunkLength;
        Remaining -= ChunkLength;
    }

    KeMemoryBarrier();
    Status = STATUS_SUCCESS;

CleanupLock:
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
CleanupReference:
    DxgkVidMmDereferenceAllocation(Allocation);
    return Status;
}
#endif

/*
 * DxgkVidMmOfferAllocation / DxgkVidMmReclaimAllocation
 *
 * D3DKMTOfferAllocations marks the backing as reclaimable by the memory
 * manager (a preferred eviction victim); ReclaimAllocations clears the
 * offer and reports whether the content was discarded in between.  A
 * discarded pool-owned backing is replaced with zeroed pages at reclaim.
 */
NTSTATUS
DxgkVidMmOfferReferencedAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG Priority,
    _In_ ULONG Flags)
{
    NTSTATUS Status;

    if (Allocation == NULL || (Flags & ~0x3UL) != 0)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return Status;
    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    if (Allocation->Offered)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else
    {
        Allocation->Offered = TRUE;
        Allocation->OfferDiscarded = FALSE;
        Allocation->OfferAllowDecommit = (Flags & 0x2UL) != 0;
        Allocation->OfferDecommitted = FALSE;
        Allocation->OfferPriority = Priority;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return Status;
}

NTSTATUS
DxgkVidMmOfferAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE  Device,
    _In_ D3DKMT_HANDLE    Handle,
    _In_ ULONG            Priority,
    _In_ ULONG            Flags)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Handle, Adapter, Device, &Allocation);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmOfferReferencedAllocation(
                 Allocation,
                 Priority,
                 Flags);
    DxgkVidMmDereferenceAllocation(Allocation);
    return Status;
}

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2100)
NTSTATUS
DxgkVidMmReclaimReferencedAllocation3(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PULONG Result)
{
    BOOLEAN Offered;
    BOOLEAN OfferDiscarded;
    BOOLEAN OfferDecommitted;
    NTSTATUS Status;

    if (Allocation == NULL || Result == NULL)
        return STATUS_INVALID_PARAMETER;
    *Result = D3DDDI_RECLAIM_RESULT_OK;
    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return Status;
    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    Offered = Allocation->Offered;
    OfferDiscarded = Allocation->OfferDiscarded;
    OfferDecommitted = Allocation->OfferDecommitted;
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    if (!Offered)
    {
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (OfferDecommitted)
        *Result = D3DDDI_RECLAIM_RESULT_NOT_COMMITTED;
    else if (OfferDiscarded)
        *Result = D3DDDI_RECLAIM_RESULT_DISCARDED;

    if ((OfferDiscarded || OfferDecommitted) &&
        Allocation->SystemMemory == NULL &&
        Allocation->SysMemMdl == NULL)
    {
        SIZE_T AllocSize;

        if (!DxgkpVidMmRoundUpPageSize(Allocation->Size, &AllocSize))
        {
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            return STATUS_INTEGER_OVERFLOW;
        }

        Allocation->SystemMemory = ExAllocatePoolWithTag(NonPagedPool, AllocSize, TAG_VIDMM_ALLOC);
        if (Allocation->SystemMemory == NULL)
        {
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Allocation->SystemMemory, AllocSize);
        if (!Allocation->Resident)
            Allocation->PhysicalAddress = MmGetPhysicalAddress(Allocation->SystemMemory);
        if (Allocation->CpuVisible && Allocation->CpuAddress == NULL)
            Allocation->CpuAddress = Allocation->SystemMemory;
    }
    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    Allocation->Offered = FALSE;
    Allocation->OfferDiscarded = FALSE;
    Allocation->OfferAllowDecommit = FALSE;
    Allocation->OfferDecommitted = FALSE;
    Allocation->OfferPriority = 0;
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return STATUS_SUCCESS;
}
#endif

NTSTATUS
DxgkVidMmReclaimReferencedAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PBOOLEAN Discarded)
{
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2100)
    ULONG Result;
    NTSTATUS Status;

    if (Discarded == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkVidMmReclaimReferencedAllocation3(Allocation, &Result);
    if (NT_SUCCESS(Status))
        *Discarded = Result != D3DDDI_RECLAIM_RESULT_OK;
    else
        *Discarded = FALSE;
    return Status;
#else
    BOOLEAN Offered;
    BOOLEAN OfferDiscarded;
    NTSTATUS Status;

    if (Allocation == NULL || Discarded == NULL)
        return STATUS_INVALID_PARAMETER;
    *Discarded = FALSE;
    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return Status;
    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    Offered = Allocation->Offered;
    OfferDiscarded = Allocation->OfferDiscarded;
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    if (!Offered)
    {
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        return STATUS_INVALID_DEVICE_STATE;
    }
    *Discarded = OfferDiscarded;
    if (OfferDiscarded &&
        Allocation->SystemMemory == NULL &&
        Allocation->SysMemMdl == NULL)
    {
        SIZE_T AllocSize;

        if (!DxgkpVidMmRoundUpPageSize(Allocation->Size, &AllocSize))
        {
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            return STATUS_INTEGER_OVERFLOW;
        }

        Allocation->SystemMemory = ExAllocatePoolWithTag(
                                       NonPagedPool,
                                       AllocSize,
                                       TAG_VIDMM_ALLOC);
        if (Allocation->SystemMemory == NULL)
        {
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Allocation->SystemMemory, AllocSize);
        if (!Allocation->Resident)
            Allocation->PhysicalAddress =
                MmGetPhysicalAddress(Allocation->SystemMemory);
        if (Allocation->CpuVisible && Allocation->CpuAddress == NULL)
            Allocation->CpuAddress = Allocation->SystemMemory;
    }
    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    Allocation->Offered = FALSE;
    Allocation->OfferDiscarded = FALSE;
    Allocation->OfferAllowDecommit = FALSE;
    Allocation->OfferDecommitted = FALSE;
    Allocation->OfferPriority = 0;
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return STATUS_SUCCESS;
#endif
}

NTSTATUS
DxgkVidMmReclaimAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE  Device,
    _In_ D3DKMT_HANDLE    Handle,
    _Out_ PBOOLEAN        Discarded)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    if (Discarded == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Handle, Adapter, Device, &Allocation);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReclaimReferencedAllocation(Allocation, Discarded);
    DxgkVidMmDereferenceAllocation(Allocation);
    return Status;
}

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2100)
#define DXGKP_UPDATE_PROPERTY_MASK_WDDM_2_1   0x00000007UL
#define DXGKP_UPDATE_PROPERTY_FLAGS_WDDM_2_1  0x00000001UL

NTSTATUS
DxgkVidMmUpdateAllocationProperty(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE Handle,
    _In_ ULONG SupportedSegmentSet,
    _In_ ULONG PreferredSegmentValue,
    _In_ ULONG PropertyFlagsValue,
    _In_ ULONG PropertyMaskValue)
{
    PDXGKVMM_ALLOCATION Binding = NULL;
    PDXGKVMM_ALLOCATION Allocation;
    HANDLE MiniportAllocation = NULL;
    D3DDDI_SEGMENTPREFERENCE PreferredSegment;
    DXGK_ALLOCATIONINFO ValidationInfo;
    DXGKARG_VALIDATEUPDATEALLOCPROPERTY ValidationArgs;
    ULONG NewSupportedSegmentSet;
    ULONG NewPreferredSegmentValue;
    BOOLEAN NewAccessedPhysically;
    BOOLEAN Offered;
    BOOLEAN EvictionRequired;
    ULONG OwnerToken;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Device == NULL || Handle == 0 ||
        Device->Adapter != Adapter ||
        (PropertyMaskValue & ~DXGKP_UPDATE_PROPERTY_MASK_WDDM_2_1) != 0 ||
        (((PropertyMaskValue & 0x1UL) != 0) &&
         (PropertyFlagsValue &
          ~DXGKP_UPDATE_PROPERTY_FLAGS_WDDM_2_1) != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->UseDodLayout ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
             Adapter->MiniportContext->InitData.s.Version,
             DXGK_CAPS_CORE_LEVEL_WDDM_2_1) ||
        DXGK_CB_FULL(Adapter,
                     DxgkDdiValidateUpdateAllocationProperty) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    PreferredSegment.Value = PreferredSegmentValue;
    if ((PropertyMaskValue & 0x4UL) != 0 &&
        PreferredSegment.Reserved != 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceOpenBinding(
                 (HANDLE)(ULONG_PTR)Handle,
                 Adapter,
                 Device,
                 &MiniportAllocation,
                 &Binding);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_HANDLE;

    Allocation = Binding->BackingAllocation != NULL
                     ? Binding->BackingAllocation
                     : Binding;
    Status = DxgkpVidMmBeginResidencyTransaction(
                 Allocation,
                 &OwnerToken);
    if (!NT_SUCCESS(Status))
        goto CleanupBinding;

    if (Allocation->PendingPlacement &&
        !DxgkPagingFenceCompleted(
             Adapter,
             Allocation->PagingFenceId))
    {
        Status = DxgkPagingWaitForFence(
                     Adapter,
                     Allocation->PagingFenceId,
                     DXGKP_VIDMM_PAGING_ADMISSION_TIMEOUT_MS);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_TIMEOUT)
                Status = STATUS_GRAPHICS_ALLOCATION_BUSY;
            goto CleanupTransaction;
        }
    }
    if (!DxgkPagingSyncPlacement(Allocation))
    {
        Status = STATUS_GRAPHICS_ALLOCATION_BUSY;
        goto CleanupTransaction;
    }

    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    Offered = Allocation->Offered;
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);
    if (Offered)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto CleanupTransaction;
    }

    NewSupportedSegmentSet =
        (PropertyMaskValue & 0x2UL) != 0
            ? SupportedSegmentSet
            : Allocation->SupportedWriteSegmentSet;
    NewPreferredSegmentValue =
        (PropertyMaskValue & 0x4UL) != 0
            ? PreferredSegmentValue
            : Allocation->PreferredSegment.Value;
    NewAccessedPhysically =
        (PropertyMaskValue & 0x1UL) != 0
            ? (PropertyFlagsValue & 0x1UL) != 0
            : Allocation->AccessedPhysically;

    if ((PropertyMaskValue & 0x2UL) != 0 &&
        NewSupportedSegmentSet == 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto CleanupTransaction;
    }

    RtlZeroMemory(&ValidationInfo, sizeof(ValidationInfo));
    ValidationInfo.Size = Allocation->Size;
    ValidationInfo.PitchAlignedSize = Allocation->PitchAlignedSize;
    ValidationInfo.Alignment = (UINT)Allocation->Alignment;
    ValidationInfo.SupportedWriteSegmentSet = NewSupportedSegmentSet;
    ValidationInfo.EvictionSegmentSet = Allocation->EvictionSegmentSet;
    ValidationInfo.PreferredSegment.Value = NewPreferredSegmentValue;
    if (Adapter->Segments != NULL &&
        !DxgkpVidMmValidateAllocationSegmentSets(
             Adapter,
             &ValidationInfo))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto CleanupTransaction;
    }

    RtlZeroMemory(&ValidationArgs, sizeof(ValidationArgs));
    ValidationArgs.hAllocation = MiniportAllocation;
    ValidationArgs.SupportedSegmentSet = NewSupportedSegmentSet;
    ValidationArgs.PreferredSegment.Value =
        NewPreferredSegmentValue;
    ValidationArgs.Flags.Value =
        NewAccessedPhysically ? 0x1UL : 0;
    ValidationArgs.PropertyMaskValue = PropertyMaskValue;

    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto CleanupTransaction;
    }
    Status = DXGK_CB_FULL(
                 Adapter,
                 DxgkDdiValidateUpdateAllocationProperty)(
                     Adapter->MiniportDeviceContext,
                     &ValidationArgs);
    DxgkReleaseMiniportCallback(Adapter);
    if (!NT_SUCCESS(Status))
        goto CleanupTransaction;

    if (Allocation->Resident &&
        Allocation->SegmentId >= 1 &&
        Allocation->SegmentId <= 32)
    {
        EvictionRequired =
            (NewSupportedSegmentSet &
             (1UL << (Allocation->SegmentId - 1))) == 0;
    }
    else
    {
        EvictionRequired = FALSE;
    }

    if (EvictionRequired)
    {
        Status = DxgkpVidMmEvictOwned(Allocation);
        if (!NT_SUCCESS(Status))
            goto CleanupTransaction;
        if (Allocation->PendingPlacement &&
            !DxgkPagingFenceCompleted(
                 Adapter,
                 Allocation->PagingFenceId))
        {
            Status = DxgkPagingWaitForFence(
                         Adapter,
                         Allocation->PagingFenceId,
                         DXGKP_VIDMM_PAGING_ADMISSION_TIMEOUT_MS);
            if (!NT_SUCCESS(Status))
            {
                if (Status == STATUS_TIMEOUT)
                    Status = STATUS_GRAPHICS_ALLOCATION_BUSY;
                goto CleanupTransaction;
            }
        }
        if (!DxgkPagingSyncPlacement(Allocation))
        {
            Status = STATUS_GRAPHICS_ALLOCATION_BUSY;
            goto CleanupTransaction;
        }
    }

    Status = KeWaitForSingleObject(
                 &Allocation->ResidencyLock,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL);
    if (!NT_SUCCESS(Status))
        goto CleanupTransaction;
    ASSERT(Allocation->ResidencyTransactionOwner == &OwnerToken);
    Allocation->SupportedWriteSegmentSet =
        NewSupportedSegmentSet;
    Allocation->PreferredSegment.Value =
        NewPreferredSegmentValue;
    Allocation->AccessedPhysically =
        NewAccessedPhysically;
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    Status = STATUS_SUCCESS;

CleanupTransaction:
    DxgkpVidMmEndResidencyTransaction(
        Allocation,
        &OwnerToken);
CleanupBinding:
    DxgkVidMmDereferenceLogicalAllocation(Binding);
    return Status;
}
#endif


/*
 * VidMmFindLowestPriorityResident
 *
 * Refreshes each placement's state in the owner, then lets the owner rank
 * them.  dxgkrnl reports facts — pinned, residency-referenced, offered, and
 * the effective priority — while which of those disqualifies a victim for
 * this segment type is the caller's, and the ranking among the survivors is
 * dxgmms2's.
 *
 * Must be called with Segment->Lock held.
 */
static
PDXGKVMM_ALLOCATION
VidMmFindLowestPriorityResident(
    _In_ PDXGKRNL_SEGMENT Segment)
{
    PDXGMMS2_VIDMM_INTERFACE_V1 VidMm;
    PDXGKVMM_ALLOCATION Victim = NULL;
    PLIST_ENTRY         Entry;
    ULONGLONG           VictimCookie = 0;
    ULONG               ExcludeFlags;

    if (IsListEmpty(&Segment->AllocationList))
        return NULL;
    Entry = Segment->AllocationList.Flink;
    VidMm = DxgkpVidMmOwner(CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, SegmentEntry)->Adapter);
    if (VidMm == NULL)
        return NULL;

    ExAcquireFastMutex(&DxgkVidMmPolicyLock);
    for (Entry = Segment->AllocationList.Flink;
         Entry != &Segment->AllocationList;
         Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Candidate =
            CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, SegmentEntry);
        ULONG Flags = 0;

        if (DxgkSubmissionResidencyPinIsHeld(&Candidate->SubmissionResidencyPinCount) ||
            InterlockedCompareExchangePointer(
                &Candidate->ResidencyTransactionOwner,
                NULL,
                NULL) != NULL)
            Flags |= DXGMMS2_VIDMM_RANGE_PINNED;
        if (InterlockedCompareExchange(&Candidate->ResidencyReferenceCount, 0, 0) != 0)
            Flags |= DXGMMS2_VIDMM_RANGE_RESIDENCY_REFERENCED;
        if (Candidate->Offered)
            Flags |= DXGMMS2_VIDMM_RANGE_OFFERED;
        (VOID)VidMm->SetPlacementState(VidMm->VidMmHandle,
                                       Segment->SegmentId - 1,
                                       (ULONGLONG)(ULONG_PTR)Candidate,
                                       Candidate->Offered ? (LONG)Candidate->OfferPriority : (LONG)Candidate->AllocationPriority,
                                       Flags);
    }
    ExReleaseFastMutex(&DxgkVidMmPolicyLock);

    /*
     * Active submissions pin placement in every segment.  A user-mode
     * residency reference additionally pins local placement because demand
     * repaging does not exist yet; aperture content survives ordinary
     * priority-pressure eviction in system memory.
     */
    ExcludeFlags = DXGMMS2_VIDMM_RANGE_PINNED;
    if (!VidMmSegmentIsAperture(Segment))
        ExcludeFlags |= DXGMMS2_VIDMM_RANGE_RESIDENCY_REFERENCED;

    if (!VidMm->FindEvictionCandidate(VidMm->VidMmHandle, Segment->SegmentId - 1, ExcludeFlags, &VictimCookie) || VictimCookie == 0)
        return NULL;

    /* Resolve the cookie against this segment's index so a stale answer can
     * never be dereferenced as an allocation. */
    for (Entry = Segment->AllocationList.Flink;
         Entry != &Segment->AllocationList;
         Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Candidate =
            CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, SegmentEntry);

        if ((ULONGLONG)(ULONG_PTR)Candidate == VictimCookie)
        {
            Victim = Candidate;
            break;
        }
    }
    return Victim;
}



/*
 * DxgkVidMmMakeResident
 *
 * Brings an evicted allocation back into a GPU segment.
 *
 * Strategy:
 *   Pass 1: try all non-aperture segments without evicting anything.
 *   Pass 2: for each full segment, evict the lowest-priority resident
 *           (if its priority is lower than ours) and retry placement.
 */
static NTSTATUS
DxgkpVidMmCompleteResidencyOwned(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN DeferAperturePaging,
    _Out_opt_ PBOOLEAN OutAperturePagingRequired)
{
    PDXGKRNL_SEGMENT Segment;
    NTSTATUS Status = STATUS_SUCCESS;

    if (OutAperturePagingRequired != NULL)
        *OutAperturePagingRequired = FALSE;
    if (!Allocation->Resident || Allocation->SegmentId < 1 || Allocation->SegmentId > Adapter->SegmentCount)
        return STATUS_INVALID_DEVICE_STATE;
    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (VidMmSegmentIsAperture(Segment))
    {
        Status = DxgkpVidMmPrepareAllocationApertureMappingOwned(Allocation);
        if (NT_SUCCESS(Status) && DeferAperturePaging)
        {
            if (OutAperturePagingRequired != NULL)
                *OutAperturePagingRequired = !Allocation->ApertureMapped;
        }
        else if (NT_SUCCESS(Status) && !Allocation->ApertureMapped)
        {
            DXGKRNL_PAGING_OP Op;

            Status = DxgkpVidMmFillAperturePagingOperation(Allocation,
                                                          TRUE,
                                                          &Op);
            if (NT_SUCCESS(Status))
                Status = DxgkPagingExecuteSynchronous(Adapter,
                                                      Allocation->Device,
                                                      &Op);
            if (NT_SUCCESS(Status))
                Allocation->ApertureMapped = TRUE;
        }
    }
    else if (Allocation->SystemMemory != NULL)
        Status = DxgkpVidMmTransferAllocationContent(Adapter, Allocation, TRUE);
    if (NT_SUCCESS(Status) && !VidMmSegmentIsAperture(Segment) && Allocation->CpuAddress == Allocation->SystemMemory)
        Allocation->CpuAddress = NULL;
    if (!NT_SUCCESS(Status))
    {
        if (!Allocation->ApertureMapped && Allocation->ApertureMdl != NULL)
        {
            IoFreeMdl(Allocation->ApertureMdl);
            Allocation->ApertureMdl = NULL;
        }
        DxgkpVidMmReleaseSegmentPlacement(Allocation);
        if (Allocation->SystemMemory != NULL)
            Allocation->PhysicalAddress = MmGetPhysicalAddress(Allocation->SystemMemory);
    }
    return Status;
}

static BOOLEAN
DxgkpVidMmTryReferencePhysicalAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LONG References;

    if (Allocation == NULL || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
        return FALSE;
    for (;;)
    {
        References = InterlockedCompareExchange(&Allocation->ReferenceCount, 0, 0);
        if (References <= 0 || References == MAXLONG)
            return FALSE;
        if (InterlockedCompareExchange(&Allocation->ReferenceCount, References + 1, References) == References)
            break;
    }
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
    {
        DxgkVidMmDereferenceAllocation(Allocation);
        return FALSE;
    }
    return TRUE;
}

static NTSTATUS
DxgkpVidMmMakeResidentOwned(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN DeferAperturePaging,
    _Out_opt_ PBOOLEAN OutAperturePagingRequired)
{
    PDXGKRNL_SEGMENT Segments;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;
    BOOLEAN Placed = FALSE;

    if (Allocation == NULL || Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (OutAperturePagingRequired != NULL)
        *OutAperturePagingRequired = FALSE;
    DPRINT("DxgkVidMmMakeResident: Alloc=%p size=%Iu\n", Allocation, Allocation->Size);
    if (Allocation->Resident)
    {
        DPRINT1("DxgkVidMmMakeResident: alloc %p is already resident\n", Allocation);
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (Allocation->ContentLost)
    {
        Status = STATUS_GRAPHICS_ALLOCATION_CONTENT_LOST;
        goto Cleanup;
    }
    if (Allocation->Adapter != Adapter)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (Adapter->Segments == NULL)
    {
        DPRINT1("DxgkVidMmMakeResident: adapter %p has no segment table\n", Adapter);
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }

    Segments = ADAPTER_SEGMENTS(Adapter);
    for (i = 0; i < Adapter->SegmentCount && !Placed; i++)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[i];

        if (!VidMmAllocationSupportsSegment(Allocation, Seg->SegmentId))
            continue;
        if (VidMmSegmentIsAperture(Seg) && Allocation->SystemMemory == NULL)
            continue;
        Status = DxgkVidMmTryPlaceInSegment(Seg, Allocation);
        if (NT_SUCCESS(Status))
        {
            Placed = TRUE;
            DPRINT("DxgkVidMmMakeResident: placed in seg %lu offset=0x%I64x (no eviction needed)\n", Seg->SegmentId, Allocation->SegmentOffset);
        }
    }

    if (Placed)
    {
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        if (!DeferAperturePaging &&
            Allocation->Device != NULL &&
            Allocation->Device->OwnerProcess != NULL)
        {
            Status = DxgkpVidMmChargeInitialPlacement(
                         Adapter,
                         Allocation->Device->OwnerProcess,
                         Allocation);
            if (!NT_SUCCESS(Status))
            {
                DxgkpVidMmReleaseSegmentPlacement(Allocation);
                if (Allocation->SystemMemory != NULL)
                {
                    Allocation->PhysicalAddress =
                        MmGetPhysicalAddress(Allocation->SystemMemory);
                }
                goto Cleanup;
            }
        }
#endif
        Status = DxgkpVidMmCompleteResidencyOwned(Allocation,
                                                 Adapter,
                                                 DeferAperturePaging,
                                                 OutAperturePagingRequired);
        goto Cleanup;
    }

    for (i = 0; i < Adapter->SegmentCount && !Placed; i++)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[i];
        PDXGKVMM_ALLOCATION Victim;
        BOOLEAN VictimOffered;
        ULONG VictimPriority;
        ULONG AllocationPriority;
        ULONG VictimOwnerToken;

        if (!VidMmAllocationSupportsSegment(Allocation, Seg->SegmentId))
            continue;
        if (VidMmSegmentIsAperture(Seg))
            continue;
        ExAcquireFastMutex(&Seg->Lock);
        Victim = VidMmFindLowestPriorityResident(Seg);
        if (Victim != NULL && !DxgkpVidMmTryReferencePhysicalAllocation(Victim))
            Victim = NULL;
        ExReleaseFastMutex(&Seg->Lock);
        if (Victim == NULL)
            continue;
        ExAcquireFastMutex(&DxgkVidMmPolicyLock);
        VictimOffered = Victim->Offered;
        VictimPriority = Victim->AllocationPriority;
        AllocationPriority = Allocation->AllocationPriority;
        ExReleaseFastMutex(&DxgkVidMmPolicyLock);
        if (!VictimOffered && VictimPriority >= AllocationPriority)
        {
            DPRINT("DxgkVidMmMakeResident: victim %p priority %lu >= our priority %lu, skipping seg %lu\n", Victim, VictimPriority, AllocationPriority, Seg->SegmentId);
            DxgkVidMmDereferenceAllocation(Victim);
            continue;
        }
        DPRINT("DxgkVidMmMakeResident: evicting victim %p (priority %lu) from seg %lu to make room\n", Victim, VictimPriority, Seg->SegmentId);
        Status = DxgkpVidMmTryBeginResidencyTransaction(
                     Victim,
                     &VictimOwnerToken);
        if (NT_SUCCESS(Status))
        {
            Status = DxgkpVidMmEvictOwned(Victim);
            if (NT_SUCCESS(Status))
                DxgkpVidMmCompleteOfferedEvictionOwned(Victim);
            DxgkpVidMmEndResidencyTransaction(Victim,
                                             &VictimOwnerToken);
        }
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkVidMmMakeResident: eviction of victim %p failed 0x%08lx\n", Victim, Status);
            DxgkVidMmDereferenceAllocation(Victim);
            continue;
        }
        DxgkVidMmDereferenceAllocation(Victim);
        Status = DxgkVidMmTryPlaceInSegment(Seg, Allocation);
        if (NT_SUCCESS(Status))
        {
            Placed = TRUE;
            DPRINT("DxgkVidMmMakeResident: placed after eviction in seg %lu offset=0x%I64x\n", Seg->SegmentId, Allocation->SegmentOffset);
        }
    }

    if (!Placed)
    {
        DPRINT1("DxgkVidMmMakeResident: cannot make alloc %p resident — all segments full\n", Allocation);
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (!DeferAperturePaging &&
        Allocation->Device != NULL &&
        Allocation->Device->OwnerProcess != NULL)
    {
        Status = DxgkpVidMmChargeInitialPlacement(
                     Adapter,
                     Allocation->Device->OwnerProcess,
                     Allocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkpVidMmReleaseSegmentPlacement(Allocation);
            if (Allocation->SystemMemory != NULL)
            {
                Allocation->PhysicalAddress =
                    MmGetPhysicalAddress(Allocation->SystemMemory);
            }
            goto Cleanup;
        }
    }
#endif
    Status = DxgkpVidMmCompleteResidencyOwned(Allocation,
                                             Adapter,
                                             DeferAperturePaging,
                                             OutAperturePagingRequired);

Cleanup:
    return Status;
}

NTSTATUS
DxgkVidMmMakeResident(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG OwnerToken;
    NTSTATUS Status;

    if (Allocation == NULL || Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpVidMmBeginResidencyTransaction(Allocation, &OwnerToken);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkpVidMmMakeResidentOwned(Allocation,
                                        Adapter,
                                        FALSE,
                                        NULL);
    DxgkpVidMmEndResidencyTransaction(Allocation, &OwnerToken);
    return Status;
}

typedef struct _DXGKP_VIDMM_BATCH_ENTRY
{
    PDXGKVMM_ALLOCATION Allocation;
    ULONG RequestReferenceCount;
    BOOLEAN TransactionOwned;
    BOOLEAN ReferencesAcquired;
    BOOLEAN WasResident;
    BOOLEAN PlacementOwned;
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    ULONG ProcessReferenceCount;
    PDXGKVMM_PROCESS_BUDGET_CHARGE BudgetChargeCandidate;
    BOOLEAN BudgetReferencesAcquired;
#endif
    BOOLEAN AperturePagingRequired;
    BOOLEAN EvictionRequired;
    BOOLEAN ApertureUnmapRequired;
} DXGKP_VIDMM_BATCH_ENTRY, *PDXGKP_VIDMM_BATCH_ENTRY;

static ULONG
DxgkpVidMmBuildSortedBatchEntries(
    _Out_writes_(AllocationCount) PDXGKP_VIDMM_BATCH_ENTRY Entries,
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ ULONG AllocationCount)
{
    ULONG Gap;
    ULONG Index;
    ULONG UniqueCount = 0;

    RtlZeroMemory(Entries,
                  (SIZE_T)AllocationCount * sizeof(*Entries));
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        Entries[Index].Allocation = Allocations[Index];
        Entries[Index].RequestReferenceCount = 1;
    }

    /* A deterministic pointer order prevents two overlapping batches from
     * claiming A/B and B/A and then waiting forever on each other. */
    for (Gap = AllocationCount / 2; Gap != 0; Gap /= 2)
    {
        for (Index = Gap; Index < AllocationCount; ++Index)
        {
            DXGKP_VIDMM_BATCH_ENTRY Value = Entries[Index];
            ULONG Position = Index;

            while (Position >= Gap &&
                   (ULONG_PTR)Entries[Position - Gap].Allocation >
                       (ULONG_PTR)Value.Allocation)
            {
                Entries[Position] = Entries[Position - Gap];
                Position -= Gap;
            }
            Entries[Position] = Value;
        }
    }

    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (UniqueCount != 0 &&
            Entries[UniqueCount - 1].Allocation == Entries[Index].Allocation)
        {
            Entries[UniqueCount - 1].RequestReferenceCount++;
        }
        else
        {
            if (UniqueCount != Index)
                Entries[UniqueCount] = Entries[Index];
            Entries[UniqueCount].RequestReferenceCount = 1;
            UniqueCount++;
        }
    }
    return UniqueCount;
}

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static PDXGKVMM_PROCESS_BUDGET_CHARGE
DxgkpVidMmFindAllocationBudgetChargeLocked(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKVMM_PROCESS_BUDGET Record)
{
    PLIST_ENTRY Entry;

    for (Entry = Allocation->ResidencyBudgetChargeList.Flink;
         Entry != &Allocation->ResidencyBudgetChargeList;
         Entry = Entry->Flink)
    {
        PDXGKVMM_PROCESS_BUDGET_CHARGE Charge =
            CONTAINING_RECORD(
                Entry,
                DXGKVMM_PROCESS_BUDGET_CHARGE,
                Link);

        if (Charge->Record == Record)
            return Charge;
    }
    return NULL;
}

static NTSTATUS
DxgkpVidMmChargeMakeResidentBatch(
    _In_ PDXGKVMM_PROCESS_BUDGET Record,
    _Inout_updates_(EntryCount) PDXGKP_VIDMM_BATCH_ENTRY Entries,
    _In_ ULONG EntryCount,
    _In_ BOOLEAN CantTrimFurther,
    _Out_ PULONGLONG OutNumBytesToTrim)
{
    ULONGLONG Bytes[2] = { 0, 0 };
    ULONGLONG Trim[2] = { 0, 0 };
    NTSTATUS PlanStatus[2] = { STATUS_SUCCESS, STATUS_SUCCESS };
    BOOLEAN Committed[2] = { FALSE, FALSE };
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    if (Record->ProcessExiting)
    {
        Status = STATUS_DELETE_PENDING;
        goto Exit;
    }

    for (Index = 0; Index < EntryCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[Index].Allocation;
        PDXGKRNL_SEGMENT Segment;
        VIDMM_BUDGET_GROUP VidMmGroup;
        ULONGLONG AllocationBytes;
        PDXGKVMM_PROCESS_BUDGET_CHARGE Charge;
        BOOLEAN RequiresBudgetCharge;
        ULONG AcquireReferenceCount;
        ULONG GroupIndex;

        /*
         * Deliberately include allocations that were already resident before
         * this request.  An opened/shared handle resolves to the resident
         * physical backing, but this can still be the calling process's first
         * reference and therefore its first budget charge.
         */
        if (!Allocation->Resident ||
            Allocation->SegmentId == 0 ||
            Allocation->SegmentId > Record->Adapter->SegmentCount ||
            Entries[Index].ProcessReferenceCount <
                Entries[Index].RequestReferenceCount)
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto Exit;
        }
        Segment =
            &ADAPTER_SEGMENTS(Record->Adapter)[Allocation->SegmentId - 1];
        VidMmGroup = VidMmSegmentBudgetGroup(Record->Adapter, Segment);
        if (VidMmGroup == VidMmBudgetGroupNone)
            continue;
        GroupIndex = (VidMmGroup == VidMmBudgetGroupLocal) ? 0 : 1;
        AllocationBytes =
            VidMmAllocationSizeForSegment(Allocation, Segment);

        Charge = DxgkpVidMmFindAllocationBudgetChargeLocked(
                     Allocation,
                     Record);
        if (Charge == NULL)
        {
            Charge = ExAllocatePoolWithTag(
                         NonPagedPool,
                         sizeof(*Charge),
                         TAG_VIDMM_ALLOC);
            if (Charge == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Exit;
            }
            RtlZeroMemory(Charge, sizeof(*Charge));
            Charge->Record = Record;
            Charge->Group =
                (VidMmGroup == VidMmBudgetGroupLocal)
                    ? D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL
                    : D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL;
            Charge->Bytes = AllocationBytes;
            DxgkResidencyCoreProcessChargeInitialize(&Charge->State);
            Entries[Index].BudgetChargeCandidate = Charge;
            AcquireReferenceCount =
                Entries[Index].ProcessReferenceCount;
        }
        else
        {
            if (Charge->Group !=
                    ((VidMmGroup == VidMmBudgetGroupLocal)
                         ? D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL
                         : D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL) ||
                Charge->Bytes != AllocationBytes ||
                Charge->State.ReferenceCount >
                    Entries[Index].ProcessReferenceCount ||
                Entries[Index].ProcessReferenceCount -
                        Charge->State.ReferenceCount !=
                    Entries[Index].RequestReferenceCount)
            {
                Status = STATUS_INVALID_DEVICE_STATE;
                goto Exit;
            }
            AcquireReferenceCount =
                Entries[Index].RequestReferenceCount;
        }

        Status = DxgkResidencyCoreProcessChargePlanAcquire(
                     &Charge->State,
                     AcquireReferenceCount,
                     &RequiresBudgetCharge);
        if (!NT_SUCCESS(Status))
            goto Exit;
        if (RequiresBudgetCharge !=
                (Entries[Index].BudgetChargeCandidate != NULL))
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto Exit;
        }
        if (!RequiresBudgetCharge)
            continue;
        if (Bytes[GroupIndex] > MAXULONGLONG - AllocationBytes)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto Exit;
        }
        Bytes[GroupIndex] += AllocationBytes;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Bytes); ++Index)
    {
        if (Bytes[Index] == 0)
            continue;
        PlanStatus[Index] = DxgkResidencyCoreBudgetPlanCharge(
                                &Record->Groups[Index],
                                Bytes[Index],
                                CantTrimFurther,
                                &Trim[Index]);
        if (!NT_SUCCESS(PlanStatus[Index]) &&
            PlanStatus[Index] != STATUS_GRAPHICS_NO_VIDEO_MEMORY)
        {
            Status = PlanStatus[Index];
            goto Exit;
        }
    }
    if (PlanStatus[0] == STATUS_GRAPHICS_NO_VIDEO_MEMORY ||
        PlanStatus[1] == STATUS_GRAPHICS_NO_VIDEO_MEMORY)
    {
        DxgkpVidMmSaturatingAdd(OutNumBytesToTrim, Trim[0]);
        DxgkpVidMmSaturatingAdd(OutNumBytesToTrim, Trim[1]);
        Status = STATUS_GRAPHICS_NO_VIDEO_MEMORY;
        goto Exit;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Bytes); ++Index)
    {
        ULONGLONG IgnoredTrim;

        if (Bytes[Index] == 0)
            continue;
        Status = DxgkResidencyCoreBudgetTryCharge(
                     &Record->Groups[Index],
                     Bytes[Index],
                     CantTrimFurther,
                     &IgnoredTrim);
        ASSERT(NT_SUCCESS(Status));
        if (!NT_SUCCESS(Status))
        {
            ULONG RollbackIndex;

            for (RollbackIndex = 0;
                 RollbackIndex < RTL_NUMBER_OF(Committed);
                 ++RollbackIndex)
            {
                if (Committed[RollbackIndex])
                {
                    (VOID)DxgkResidencyCoreBudgetRelease(
                        &Record->Groups[RollbackIndex],
                        Bytes[RollbackIndex]);
                }
            }
            goto Exit;
        }
        Committed[Index] = TRUE;
    }

    for (Index = 0; Index < EntryCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[Index].Allocation;
        PDXGKVMM_PROCESS_BUDGET_CHARGE Charge;
        ULONG AcquireReferenceCount;

        Charge = Entries[Index].BudgetChargeCandidate;
        if (Charge == NULL)
            Charge = DxgkpVidMmFindAllocationBudgetChargeLocked(
                         Allocation,
                         Record);
        if (Charge == NULL)
            continue;
        AcquireReferenceCount =
            Entries[Index].BudgetChargeCandidate != NULL
                ? Entries[Index].ProcessReferenceCount
                : Entries[Index].RequestReferenceCount;
        Status = DxgkResidencyCoreProcessChargeCommitAcquire(
                     &Charge->State,
                     AcquireReferenceCount,
                     Entries[Index].BudgetChargeCandidate != NULL);
        ASSERT(NT_SUCCESS(Status));
        if (!NT_SUCCESS(Status))
            goto Exit;
        if (Entries[Index].BudgetChargeCandidate != NULL)
        {
            InsertTailList(
                &Allocation->ResidencyBudgetChargeList,
                &Charge->Link);
            Entries[Index].BudgetChargeCandidate = NULL;
            Record->ChargeEntryCount++;
        }
        Entries[Index].BudgetReferencesAcquired = TRUE;
    }

Exit:
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    for (Index = 0; Index < EntryCount; ++Index)
    {
        if (Entries[Index].BudgetChargeCandidate != NULL)
        {
            ExFreePoolWithTag(
                Entries[Index].BudgetChargeCandidate,
                TAG_VIDMM_ALLOC);
            Entries[Index].BudgetChargeCandidate = NULL;
        }
    }
    return Status;
}

static VOID
DxgkpVidMmRollbackMakeResidentBudget(
    _In_ PDXGKVMM_PROCESS_BUDGET Record,
    _Inout_updates_(EntryCount) PDXGKP_VIDMM_BATCH_ENTRY Entries,
    _In_ ULONG EntryCount)
{
    LIST_ENTRY RemovedCharges;
    ULONG Index;

    InitializeListHead(&RemovedCharges);
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    for (Index = 0; Index < EntryCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[Index].Allocation;
        PDXGKVMM_PROCESS_BUDGET_CHARGE Charge;
        BOOLEAN ReleaseBudgetCharge;
        NTSTATUS Status;

        if (!Entries[Index].BudgetReferencesAcquired)
            continue;
        Charge = DxgkpVidMmFindAllocationBudgetChargeLocked(
                     Allocation,
                     Record);
        ASSERT(Charge != NULL);
        if (Charge == NULL)
            continue;
        Status = DxgkResidencyCoreProcessChargeRelease(
                     &Charge->State,
                     Entries[Index].RequestReferenceCount,
                     &ReleaseBudgetCharge);
        ASSERT(NT_SUCCESS(Status));
        if (!NT_SUCCESS(Status))
            continue;
        if (ReleaseBudgetCharge)
        {
            Status = DxgkResidencyCoreBudgetRelease(
                         &Record->Groups[
                             DxgkpVidMmBudgetGroupIndex(Charge->Group)],
                         Charge->Bytes);
            ASSERT(NT_SUCCESS(Status));
            ASSERT(Record->ChargeEntryCount != 0);
            if (Record->ChargeEntryCount != 0)
                Record->ChargeEntryCount--;
            RemoveEntryList(&Charge->Link);
            InsertTailList(&RemovedCharges, &Charge->Link);
        }
        Entries[Index].BudgetReferencesAcquired = FALSE;
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    while (!IsListEmpty(&RemovedCharges))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&RemovedCharges);

        ExFreePoolWithTag(
            CONTAINING_RECORD(
                Entry,
                DXGKVMM_PROCESS_BUDGET_CHARGE,
                Link),
            TAG_VIDMM_ALLOC);
    }
}
#endif

static VOID
DxgkpVidMmRollbackMakeResidentBatchEntry(
    _Inout_ PDXGKP_VIDMM_BATCH_ENTRY Entry,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_ PVOID Owner)
{
    PDXGKVMM_ALLOCATION Allocation = Entry->Allocation;
    BOOLEAN ReachedZero = FALSE;
    NTSTATUS Status;

    if (!Entry->ReferencesAcquired)
        return;
    Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    ASSERT(NT_SUCCESS(Status));
    if (!NT_SUCCESS(Status))
        return;
    ASSERT(Allocation->ResidencyTransactionOwner == Owner);
    {
        BOOLEAN Released =
            DxgkpVidMmReleaseExplicitResidencyReferencesLocked(
                Allocation,
                Device,
                Entry->RequestReferenceCount,
                &ReachedZero);

        ASSERT(Released);
    }
    Entry->ReferencesAcquired = FALSE;
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);

    /*
     * Only this request's zero-to-one placement may be undone, and only if
     * releasing this request's references left no pre-existing owner.  No
     * render packet can have used it while the transaction owner was
     * published, so the original system backing remains authoritative and no
     * transfer-back callback is needed.
     */
    if (DxgkResidencyCoreShouldRollbackPlacement(
            Entry->PlacementOwned,
            ReachedZero))
    {
        if (!Allocation->ApertureMapped && Allocation->ApertureMdl != NULL)
        {
            IoFreeMdl(Allocation->ApertureMdl);
            Allocation->ApertureMdl = NULL;
        }
        if (Allocation->CpuAddress != NULL &&
            Allocation->CpuAddress != Allocation->SystemMemory)
        {
            Allocation->CpuAddress = NULL;
        }
        DxgkpVidMmReleaseSegmentPlacement(Allocation);
        if (Allocation->SystemMemory != NULL)
            Allocation->PhysicalAddress =
                MmGetPhysicalAddress(Allocation->SystemMemory);
    }
}

NTSTATUS
DxgkVidMmMakeResidentBatch(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PROCESS Process,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ ULONG AllocationCount,
    _In_ D3DKMT_HANDLE hPagingSyncObject,
    _Inout_opt_ volatile LONG64 *PagingFenceCounter,
    _In_ BOOLEAN CantTrimFurther,
    _In_ BOOLEAN MustSucceed,
    _Out_ PULONGLONG OutNumBytesToTrim,
    _Out_ PULONGLONG OutPagingFenceValue,
    _Out_ PBOOLEAN OutPagingQueued)
{
    DXGKP_VIDMM_BATCH_ENTRY *Entries = NULL;
    DXGKRNL_PAGING_OP *PagingOperations = NULL;
    PDXGKVMM_ALLOCATION *PagingAllocations = NULL;
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    PDXGKVMM_PROCESS_BUDGET BudgetRecord = NULL;
#endif
    ULONG OwnerToken;
    ULONG UniqueCount = 0;
    ULONG EntryIndex;
    ULONG PagingOperationCount = 0;
    ULONG PagingFenceId = 0;
    ULONG64 PagingFenceValue = 0;
    BOOLEAN PagingQueued = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (OutNumBytesToTrim != NULL)
        *OutNumBytesToTrim = 0;
    if (OutPagingFenceValue != NULL)
        *OutPagingFenceValue = 0;
    if (OutPagingQueued != NULL)
        *OutPagingQueued = FALSE;
    if (Adapter == NULL ||
        Process == NULL ||
        Process->Process == NULL ||
        Process->Adapter != Adapter ||
        (Device != NULL && Device->ProcessRecord != Process) ||
        Allocations == NULL ||
        AllocationCount == 0 ||
        AllocationCount > MAXLONG ||
        (MustSucceed && !CantTrimFurther) ||
        OutNumBytesToTrim == NULL ||
        OutPagingFenceValue == NULL ||
        OutPagingQueued == NULL ||
        (SIZE_T)AllocationCount >
            MAXULONG_PTR / sizeof(*Entries))
    {
        return STATUS_INVALID_PARAMETER;
    }

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (DxgkpVidMmEffectiveWddmLevel(Adapter) <
        DXGK_CAPS_CORE_LEVEL_WDDM_2_0)
    {
        return STATUS_NOT_SUPPORTED;
    }
    BudgetRecord = DxgkpVidMmAcquireProcessBudget(
                       Adapter,
                       Process->Process,
                       &Status);
    if (BudgetRecord == NULL)
        return Status;
#else
    UNREFERENCED_PARAMETER(CantTrimFurther);
    UNREFERENCED_PARAMETER(MustSucceed);
    return STATUS_NOT_SUPPORTED;
#endif

    Entries = ExAllocatePoolWithTag(PagedPool,
                                    (SIZE_T)AllocationCount *
                                        sizeof(*Entries),
                                    TAG_VIDMM_ALLOC);
    if (Entries == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    UniqueCount = DxgkpVidMmBuildSortedBatchEntries(Entries,
                                                   Allocations,
                                                   AllocationCount);

    for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[EntryIndex].Allocation;

        if (Allocation == NULL || Allocation->Adapter != Adapter)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Rollback;
        }
        Status = DxgkpVidMmBeginResidencyTransaction(Allocation,
                                                    &OwnerToken);
        if (!NT_SUCCESS(Status))
            goto Rollback;
        Entries[EntryIndex].TransactionOwned = TRUE;
    }

    for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[EntryIndex].Allocation;

        Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
            goto Rollback;
        ASSERT(Allocation->ResidencyTransactionOwner == &OwnerToken);
        Entries[EntryIndex].WasResident = Allocation->Resident;
        Status = DxgkpVidMmAcquireDeviceResidencyReferencesLocked(
                     Allocation,
                     Device,
                     Entries[EntryIndex].RequestReferenceCount);
        if (NT_SUCCESS(Status))
        {
            Entries[EntryIndex].ReferencesAcquired = TRUE;
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            Entries[EntryIndex].ProcessReferenceCount =
                DxgkpVidMmQueryProcessResidencyReferenceCountLocked(
                    Allocation,
                    BudgetRecord->Process);
            if (Entries[EntryIndex].ProcessReferenceCount <
                Entries[EntryIndex].RequestReferenceCount)
            {
                Status = STATUS_INVALID_DEVICE_STATE;
            }
#endif
        }
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }

    for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
    {
        if (Entries[EntryIndex].WasResident)
            continue;
        Status = DxgkpVidMmMakeResidentOwned(
                     Entries[EntryIndex].Allocation,
                     Adapter,
                     TRUE,
                     &Entries[EntryIndex].AperturePagingRequired);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_NO_MEMORY ||
                Status == STATUS_GRAPHICS_NO_VIDEO_MEMORY)
            {
                DxgkpVidMmSaturatingAdd(
                    OutNumBytesToTrim,
                    Entries[EntryIndex].Allocation->Size);
            }
            goto Rollback;
        }
        Entries[EntryIndex].PlacementOwned = TRUE;
        if (Entries[EntryIndex].AperturePagingRequired)
            PagingOperationCount++;
    }

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    Status = DxgkpVidMmChargeMakeResidentBatch(
                 BudgetRecord,
                 Entries,
                 UniqueCount,
                 CantTrimFurther,
                 OutNumBytesToTrim);
    if (!NT_SUCCESS(Status))
        goto Rollback;
#endif

    if (PagingOperationCount != 0)
    {
        ULONG OperationIndex = 0;

        if (Device == NULL ||
            hPagingSyncObject == 0 ||
            PagingFenceCounter == NULL ||
            Device->hMiniportDevice == NULL ||
            (SIZE_T)PagingOperationCount >
                MAXULONG_PTR / sizeof(*PagingOperations))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Rollback;
        }
        PagingOperations = ExAllocatePoolWithTag(
                               PagedPool,
                               (SIZE_T)PagingOperationCount *
                                   sizeof(*PagingOperations),
                               TAG_VIDMM_ALLOC);
        if (PagingOperations == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }
        PagingAllocations = ExAllocatePoolWithTag(
                                PagedPool,
                                (SIZE_T)PagingOperationCount *
                                    sizeof(*PagingAllocations),
                                TAG_VIDMM_ALLOC);
        if (PagingAllocations == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }

        for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
        {
            if (!Entries[EntryIndex].AperturePagingRequired)
                continue;
            Status = DxgkpVidMmFillAperturePagingOperation(
                         Entries[EntryIndex].Allocation,
                         TRUE,
                         &PagingOperations[OperationIndex]);
            if (!NT_SUCCESS(Status))
                goto Rollback;
            PagingOperations[OperationIndex].hMiniportDevice =
                Device->hMiniportDevice;
            PagingAllocations[OperationIndex] =
                Entries[EntryIndex].Allocation;
            OperationIndex++;
        }

        PagingFenceValue =
            (ULONG64)InterlockedIncrement64(PagingFenceCounter);
        Status = DxgkPagingExecuteBatch(Adapter,
                                        Device,
                                        PagingOperations,
                                        PagingOperationCount,
                                        PagingAllocations,
                                        PagingOperationCount,
                                        hPagingSyncObject,
                                        PagingFenceValue,
                                        &PagingFenceId,
                                        &PagingQueued);
        if (!NT_SUCCESS(Status))
            goto Rollback;

        for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
        {
            PDXGKVMM_ALLOCATION Allocation =
                Entries[EntryIndex].Allocation;

            if (!Entries[EntryIndex].AperturePagingRequired)
                continue;
            Allocation->ApertureMapped = TRUE;
            if (PagingQueued)
            {
                DxgkPagingBeginPlacement(Allocation,
                                         Allocation->SegmentId,
                                         Allocation->SegmentOffset,
                                         PagingFenceId);
            }
        }
        *OutPagingFenceValue = PagingFenceValue;
        *OutPagingQueued = PagingQueued;
    }

    goto Cleanup;

Rollback:
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (BudgetRecord != NULL && Entries != NULL)
    {
        DxgkpVidMmRollbackMakeResidentBudget(
            BudgetRecord,
            Entries,
            UniqueCount);
    }
#endif
    for (EntryIndex = UniqueCount; EntryIndex != 0; --EntryIndex)
    {
        DxgkpVidMmRollbackMakeResidentBatchEntry(
            &Entries[EntryIndex - 1],
            Device,
            &OwnerToken);
    }

Cleanup:
    if (Entries != NULL)
    {
        for (EntryIndex = UniqueCount; EntryIndex != 0; --EntryIndex)
        {
            if (Entries[EntryIndex - 1].TransactionOwned)
            {
                DxgkpVidMmEndResidencyTransaction(
                    Entries[EntryIndex - 1].Allocation,
                    &OwnerToken);
            }
        }
    }
    if (PagingOperations != NULL)
        ExFreePoolWithTag(PagingOperations, TAG_VIDMM_ALLOC);
    if (PagingAllocations != NULL)
        ExFreePoolWithTag(PagingAllocations, TAG_VIDMM_ALLOC);
    if (Entries != NULL)
        ExFreePoolWithTag(Entries, TAG_VIDMM_ALLOC);
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (BudgetRecord != NULL)
        DxgkpVidMmReleaseProcessBudget(BudgetRecord);
#endif
    if (!NT_SUCCESS(Status) &&
        MustSucceed &&
        CantTrimFurther &&
        Device != NULL &&
        (Status == STATUS_GRAPHICS_NO_VIDEO_MEMORY ||
         Status == STATUS_NO_MEMORY))
    {
        DxgkDeviceSetExecutionState(
            Device,
            D3DKMT_DEVICEEXECUTION_ERROR_OUTOFMEMORY);
    }
    return Status;
}

NTSTATUS
DxgkVidMmEvictBatch(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ ULONG AllocationCount,
    _In_ BOOLEAN EvictOnlyIfNecessary,
    _Out_ PULONGLONG OutNumBytesToTrim)
{
    DXGKP_VIDMM_BATCH_ENTRY *Entries = NULL;
    DXGKRNL_PAGING_OP *PagingOperations = NULL;
    PDXGKVMM_ALLOCATION *PagingAllocations = NULL;
    ULONG OwnerToken;
    ULONG UniqueCount;
    ULONG EntryIndex;
    ULONG PagingOperationCount = 0;
    ULONG PagingFenceId = 0;
    BOOLEAN PagingQueued = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (OutNumBytesToTrim != NULL)
        *OutNumBytesToTrim = 0;
    if (Adapter == NULL ||
        Allocations == NULL ||
        AllocationCount == 0 ||
        AllocationCount > MAXLONG ||
        OutNumBytesToTrim == NULL ||
        (SIZE_T)AllocationCount >
            MAXULONG_PTR / sizeof(*Entries))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Entries = ExAllocatePoolWithTag(PagedPool,
                                    (SIZE_T)AllocationCount *
                                        sizeof(*Entries),
                                    TAG_VIDMM_ALLOC);
    if (Entries == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    UniqueCount = DxgkpVidMmBuildSortedBatchEntries(Entries,
                                                   Allocations,
                                                   AllocationCount);

    for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[EntryIndex].Allocation;

        if (Allocation == NULL || Allocation->Adapter != Adapter)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpVidMmBeginResidencyTransaction(Allocation,
                                                    &OwnerToken);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Entries[EntryIndex].TransactionOwned = TRUE;
    }

    /*
     * Validate every duplicate count while all unique physical allocations
     * are transaction-owned.  No concurrent MakeResident, Evict, or device
     * teardown can consume a reference between this pass and the commit.
     */
    for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[EntryIndex].Allocation;
        LONG DeviceReferences;
        LONG TotalReferences;

        Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ASSERT(Allocation->ResidencyTransactionOwner == &OwnerToken);
        DeviceReferences =
            DxgkpVidMmQueryDeviceResidencyReferenceCountLocked(Allocation,
                                                               Device);
        TotalReferences = InterlockedCompareExchange(
                              &Allocation->ResidencyReferenceCount,
                              0,
                              0);
        {
            BOOLEAN TrimCandidate;

            /* The core decision consumes the exact duplicate-collapsed
             * request count while this allocation is transaction-owned. */
            Status = DxgkResidencyCorePlanEvict(
                         DeviceReferences,
                         TotalReferences,
                         Entries[EntryIndex].RequestReferenceCount,
                         Allocation->Resident,
                         EvictOnlyIfNecessary,
                         &Entries[EntryIndex].EvictionRequired,
                         &TrimCandidate);
            if (NT_SUCCESS(Status) && TrimCandidate)
                DxgkpVidMmSaturatingAdd(OutNumBytesToTrim,
                                        Allocation->Size);
        }
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    /*
     * Prepare every fallible physical operation before changing any
     * residency reference or placement.  Local-memory downloads may update
     * the allocation's existing system cache, but remain invisible while the
     * committed placement stays resident.
     */
    for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
    {
        if (!Entries[EntryIndex].EvictionRequired)
            continue;
        Status = DxgkpVidMmPrepareEvictionOwned(
                     Entries[EntryIndex].Allocation,
                     &Entries[EntryIndex].ApertureUnmapRequired);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (Entries[EntryIndex].ApertureUnmapRequired)
            PagingOperationCount++;
    }

    if (PagingOperationCount != 0)
    {
        ULONG OperationIndex = 0;

        if (Device == NULL ||
            Device->hMiniportDevice == NULL ||
            (SIZE_T)PagingOperationCount >
                MAXULONG_PTR / sizeof(*PagingOperations))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        PagingOperations = ExAllocatePoolWithTag(
                               PagedPool,
                               (SIZE_T)PagingOperationCount *
                                   sizeof(*PagingOperations),
                               TAG_VIDMM_ALLOC);
        if (PagingOperations == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        PagingAllocations = ExAllocatePoolWithTag(
                                PagedPool,
                                (SIZE_T)PagingOperationCount *
                                    sizeof(*PagingAllocations),
                                TAG_VIDMM_ALLOC);
        if (PagingAllocations == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
        {
            if (!Entries[EntryIndex].ApertureUnmapRequired)
                continue;
            Status = DxgkpVidMmFillAperturePagingOperation(
                         Entries[EntryIndex].Allocation,
                         FALSE,
                         &PagingOperations[OperationIndex]);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
            PagingOperations[OperationIndex].hMiniportDevice =
                Device->hMiniportDevice;
            PagingAllocations[OperationIndex] =
                Entries[EntryIndex].Allocation;
            OperationIndex++;
        }
        Status = DxgkPagingExecuteBatch(Adapter,
                                        Device,
                                        PagingOperations,
                                        PagingOperationCount,
                                        PagingAllocations,
                                        PagingOperationCount,
                                        0,
                                        0,
                                        &PagingFenceId,
                                        &PagingQueued);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    /* All remaining work is infallible bookkeeping. */
    for (EntryIndex = 0; EntryIndex < UniqueCount; ++EntryIndex)
    {
        PDXGKVMM_ALLOCATION Allocation = Entries[EntryIndex].Allocation;
        ULONG ReleaseIndex;
        BOOLEAN ReachedZero = FALSE;

        Status = KeWaitForSingleObject(&Allocation->ResidencyLock,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
        ASSERT(NT_SUCCESS(Status));
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ASSERT(Allocation->ResidencyTransactionOwner == &OwnerToken);
        for (ReleaseIndex = 0;
             ReleaseIndex < Entries[EntryIndex].RequestReferenceCount;
             ++ReleaseIndex)
        {
            BOOLEAN Released =
                DxgkpVidMmReleaseDeviceResidencyReferenceLocked(
                    Allocation,
                    Device,
                    &ReachedZero);

            ASSERT(Released);
        }
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        if (Device != NULL && Device->OwnerProcess != NULL)
        {
            NTSTATUS BudgetStatus =
                DxgkpVidMmReleaseProcessBudgetReferences(
                    Allocation,
                    Device->OwnerProcess,
                    Entries[EntryIndex].RequestReferenceCount);

            ASSERT(NT_SUCCESS(BudgetStatus) ||
                   BudgetStatus == STATUS_NOT_FOUND);
        }
#endif
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);

        if (Entries[EntryIndex].EvictionRequired)
        {
            ASSERT(ReachedZero);
            DxgkpVidMmCommitPreparedEvictionOwned(Allocation);
            if (Entries[EntryIndex].ApertureUnmapRequired &&
                PagingQueued)
            {
                DxgkPagingBeginPlacement(Allocation,
                                         0,
                                         0,
                                         PagingFenceId);
            }
            DxgkpVidMmCompleteOfferedEvictionOwned(Allocation);
        }
    }
    Status = STATUS_SUCCESS;

Cleanup:
    if (!NT_SUCCESS(Status))
        *OutNumBytesToTrim = 0;
    if (Entries != NULL)
    {
        for (EntryIndex = UniqueCount; EntryIndex != 0; --EntryIndex)
        {
            if (Entries[EntryIndex - 1].TransactionOwned)
            {
                DxgkpVidMmEndResidencyTransaction(
                    Entries[EntryIndex - 1].Allocation,
                    &OwnerToken);
            }
        }
    }
    if (PagingOperations != NULL)
        ExFreePoolWithTag(PagingOperations, TAG_VIDMM_ALLOC);
    if (PagingAllocations != NULL)
        ExFreePoolWithTag(PagingAllocations, TAG_VIDMM_ALLOC);
    if (Entries != NULL)
        ExFreePoolWithTag(Entries, TAG_VIDMM_ALLOC);
    return Status;
}


/* CPU MAPPING ****************************************************************/

/*
 * VidMmMapSegmentCpu
 *
 * Lazily establishes a kernel-mode CPU mapping for a VRAM segment.
 * Uses MmWriteCombined caching which is optimal for GPU framebuffer access
 * on x86: write-combining coalesces multiple CPU stores into single large
 * PCIe write transactions, avoiding the per-store penalty of uncached mode.
 *
 * Called only for non-aperture, CPU-visible segments.
 */
static NTSTATUS
VidMmMapSegmentCpu(
    _In_ PDXGKRNL_SEGMENT Segment)
{
    ASSERT(Segment != NULL);
    ASSERT(!VidMmSegmentIsAperture(Segment));
    ASSERT(VidMmSegmentIsCpuVisible(Segment));
    ASSERT(Segment->Size > 0);

    ExAcquireFastMutex(&Segment->Lock);
    if (Segment->CpuBase != NULL)
    {
        ExReleaseFastMutex(&Segment->Lock);
        return STATUS_SUCCESS;
    }

    Segment->CpuBase = MmMapIoSpace(Segment->CpuTranslatedAddress, (SIZE_T)Segment->Size, MmWriteCombined);

    if (Segment->CpuBase == NULL)
    {
        ExReleaseFastMutex(&Segment->Lock);
        DPRINT1("VidMmMapSegmentCpu: MmMapIoSpace failed for seg %lu "
                "(PA=0x%I64x size=0x%I64x)\n",
                Segment->SegmentId,
                Segment->CpuTranslatedAddress.QuadPart,
                Segment->Size);
        return STATUS_NO_MEMORY;
    }
    ExReleaseFastMutex(&Segment->Lock);

    DPRINT("VidMmMapSegmentCpu: seg %lu CPU VA=%p "
           "PA=0x%I64x size=0x%I64x\n",
           Segment->SegmentId, Segment->CpuBase,
           Segment->CpuTranslatedAddress.QuadPart, Segment->Size);

    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpVidMmReleaseApertureMapping(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN ForceInvalidate)
{
    DXGKARG_BUILDPAGINGBUFFER BuildArgs;
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Allocation == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!Allocation->ApertureMapped && Allocation->ApertureMdl == NULL)
        return STATUS_SUCCESS;

    Adapter = Allocation->Adapter;
    if (Adapter != NULL && Adapter->Segments != NULL && Allocation->SegmentId >= 1 && Allocation->SegmentId <= Adapter->SegmentCount)
        Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (Allocation->ApertureMapped)
    {
        if (Segment == NULL || !VidMmSegmentIsAperture(Segment) || Segment->DummyPageVa == NULL || Allocation->SystemMemory == NULL)
            Status = STATUS_INVALID_DEVICE_STATE;
        else if (Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
            Status = STATUS_DEVICE_NOT_READY;
        else if (DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer) == NULL)
            Status = STATUS_NOT_SUPPORTED;
        else if (!DxgkAcquireMiniportCallback(Adapter))
            Status = STATUS_DEVICE_NOT_READY;
        else
        {
            RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
            BuildArgs.Operation = DXGK_OPERATION_UNMAP_APERTURE_SEGMENT;
            BuildArgs.UnmapApertureSegment.hDevice = Allocation->MiniportDeviceHandle;
            BuildArgs.UnmapApertureSegment.hAllocation = Allocation->MiniportHandle;
            BuildArgs.UnmapApertureSegment.SegmentId = Allocation->SegmentId;
            BuildArgs.UnmapApertureSegment.OffsetInPages = (SIZE_T)(Allocation->SegmentOffset / PAGE_SIZE);
            BuildArgs.UnmapApertureSegment.NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Allocation->SystemMemory, Allocation->Size);
            BuildArgs.UnmapApertureSegment.DummyPage = Segment->DummyPage;
            Status = DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer)(Adapter->MiniportDeviceContext, &BuildArgs);
            DxgkReleaseMiniportCallback(Adapter);
        }
    }

    if (NT_SUCCESS(Status) || ForceInvalidate)
    {
        if (Allocation->ApertureMdl != NULL)
        {
            IoFreeMdl(Allocation->ApertureMdl);
            Allocation->ApertureMdl = NULL;
        }
        Allocation->ApertureMapped = FALSE;
    }
    return Status;
}

static NTSTATUS
DxgkpVidMmPrepareAllocationApertureMappingOwned(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment;
    PMDL Mdl;

    if (Allocation == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Allocation->ContentLost)
        return STATUS_GRAPHICS_ALLOCATION_CONTENT_LOST;
    if (!Allocation->Resident)
        return STATUS_SUCCESS;

    Adapter = Allocation->Adapter;
    if (Adapter == NULL ||
        Adapter->Segments == NULL ||
        Allocation->SegmentId < 1 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (!VidMmSegmentIsAperture(Segment))
        return STATUS_SUCCESS;
    if (Segment->Flags.PitchAlignment)
        return STATUS_NOT_SUPPORTED;
    if (Allocation->ApertureMapped || Allocation->ApertureMdl != NULL)
        return STATUS_SUCCESS;
    if (Allocation->SystemMemory == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Allocation->Size > MAXULONG)
        return STATUS_INVALID_BUFFER_SIZE;

    Mdl = IoAllocateMdl(Allocation->SystemMemory,
                        (ULONG)Allocation->Size,
                        FALSE,
                        FALSE,
                        NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Mdl);
    Allocation->ApertureMdl = Mdl;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpVidMmFillAperturePagingOperation(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN Map,
    _Out_ PDXGKRNL_PAGING_OP Op)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment;

    if (Allocation == NULL || Op == NULL)
        return STATUS_INVALID_PARAMETER;
    Adapter = Allocation->Adapter;
    if (Adapter == NULL ||
        Adapter->Segments == NULL ||
        Allocation->SegmentId < 1 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (!VidMmSegmentIsAperture(Segment) ||
        Allocation->SystemMemory == NULL ||
        (Map && Allocation->ApertureMdl == NULL))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    RtlZeroMemory(Op, sizeof(*Op));
    Op->Type = Map ? DxgkPagingOpMapAperture
                   : DxgkPagingOpUnmapAperture;
    Op->hMiniportDevice = Allocation->MiniportDeviceHandle;
    Op->hMiniportAllocation = Allocation->MiniportHandle;
    Op->DestinationSegmentId = Allocation->SegmentId;
    Op->OffsetInPages = (SIZE_T)(Allocation->SegmentOffset / PAGE_SIZE);
    Op->NumberOfPages =
        ADDRESS_AND_SIZE_TO_SPAN_PAGES(Allocation->SystemMemory,
                                       Allocation->Size);
    if (Map)
        Op->SourceMdl = Allocation->ApertureMdl;
    else
        Op->DummyPage = Segment->DummyPage;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidMmEnsureAllocationApertureMapped(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    ULONG OwnerToken;
    DXGKRNL_PAGING_OP Op;
    NTSTATUS Status;

    if (Allocation == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpVidMmBeginResidencyTransaction(Allocation, &OwnerToken);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DxgkpVidMmPrepareAllocationApertureMappingOwned(Allocation);
    if (NT_SUCCESS(Status) &&
        Allocation->Resident &&
        !Allocation->ApertureMapped &&
        Allocation->ApertureMdl != NULL)
    {
        Status = DxgkpVidMmFillAperturePagingOperation(Allocation, TRUE, &Op);
        if (NT_SUCCESS(Status))
            Status = DxgkPagingExecuteSynchronous(Allocation->Adapter,
                                                  Allocation->Device,
                                                  &Op);
        if (NT_SUCCESS(Status))
            Allocation->ApertureMapped = TRUE;
    }
    DxgkpVidMmEndResidencyTransaction(Allocation, &OwnerToken);
    return Status;
}

LARGE_INTEGER
DxgkVidMmGetAllocationPrimaryAddress(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    LARGE_INTEGER Address;

    Address.QuadPart = 0;
    if (Allocation != NULL)
        Address = Allocation->PhysicalAddress;

    return Address;
}

/*
 * DxgkpVidMmTransferAllocationContent
 *
 * Moves an allocation's content between its system-memory backing store
 * and its segment placement (ToSegment=TRUE uploads backing->segment on
 * MakeResident; FALSE downloads segment->backing on Evict), so paging
 * preserves content the way Windows VidMm does.  CPU-visible segments can
 * be copied directly through their segment mapping.  A non-CPU-visible
 * segment requires a real paging DMA submission and fence wait; merely
 * calling BuildPagingBuffer without a DMA buffer does not execute a transfer.
 *
 * Must be called while the allocation's SegmentId/SegmentOffset placement
 * is still valid.
 */
/*
 * DxgkpVidMmSubmitTransferPagingPacket
 *
 * Describes the segment<->backing move as a DXGK_OPERATION_TRANSFER paging
 * packet and runs it through the paging state machine.  The backing side is
 * named by an MDL (SegmentId 0) exactly as the DDI requires; the segment side
 * carries the placement offset.  Returns STATUS_NOT_SUPPORTED when the
 * miniport cannot describe the transfer, so the caller can fall back.
 */
static NTSTATUS
DxgkpVidMmSubmitTransferPagingPacket(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_SEGMENT Segment,
    _In_ BOOLEAN ToSegment)
{
    DXGKRNL_PAGING_OP Op;
    PMDL Mdl;
    NTSTATUS Status;

    if (!DxgkPagingOperationSupported(Adapter, DxgkPagingOpTransfer))
        return STATUS_NOT_SUPPORTED;
    if (Allocation->SystemMemory == NULL || Allocation->Size == 0 || Allocation->Size > MAXULONG)
        return STATUS_NOT_SUPPORTED;
    /* Idle-prepare, quiesce, and TDR recovery run inside the KMD exclusive
     * boundary with submission already closing; those callers take the CPU
     * path instead of queueing a packet that can never retire. */
    if (Adapter->KmdExclusiveOwnerThread == PsGetCurrentThread())
        return STATUS_NOT_SUPPORTED;
    if (InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
        return STATUS_NOT_SUPPORTED;
    if (Allocation->SegmentOffset + Allocation->Size > Segment->Size)
        return STATUS_INVALID_PARAMETER;

    Mdl = IoAllocateMdl(Allocation->SystemMemory, (ULONG)Allocation->Size, FALSE, FALSE, NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Mdl);

    RtlZeroMemory(&Op, sizeof(Op));
    Op.Type = DxgkPagingOpTransfer;
    Op.hMiniportDevice = Allocation->MiniportDeviceHandle;
    Op.hMiniportAllocation = Allocation->MiniportHandle;
    Op.TransferOffset = 0;
    Op.TransferSize = Allocation->Size;
    if (ToSegment)
    {
        Op.SourceSegmentId = 0;
        Op.SourceMdl = Mdl;
        Op.DestinationSegmentId = Allocation->SegmentId;
        Op.DestinationSegmentAddress.QuadPart = (LONGLONG)Allocation->SegmentOffset;
    }
    else
    {
        Op.SourceSegmentId = Allocation->SegmentId;
        Op.SourceSegmentAddress.QuadPart = (LONGLONG)Allocation->SegmentOffset;
        Op.DestinationSegmentId = 0;
        Op.DestinationMdl = Mdl;
    }

    Status = DxgkPagingExecuteSynchronous(Adapter, Allocation->Device, &Op);
    IoFreeMdl(Mdl);
    return Status;
}

static NTSTATUS
DxgkpVidMmTransferAllocationContent(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN ToSegment)
{
    PDXGKRNL_SEGMENT Segment;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    if (Adapter == NULL || Allocation == NULL ||
        Allocation->SystemMemory == NULL ||
        Allocation->Size == 0 ||
        Adapter->Segments == NULL ||
        Allocation->SegmentId < 1 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (Segment->Flags.PitchAlignment)
        return STATUS_NOT_SUPPORTED;
    if (VidMmSegmentIsAperture(Segment))
        return STATUS_SUCCESS;

    /*
     * Preferred path: a real TRANSFER paging packet built by the miniport and
     * executed by the GPU engine.  The CPU copy below stays as the fallback
     * for miniports whose BuildPagingBuffer does not describe transfers.
     */
    Status = DxgkpVidMmSubmitTransferPagingPacket(Adapter, Allocation, Segment, ToSegment);
    if (NT_SUCCESS(Status))
        return Status;
    if (Status != STATUS_NOT_SUPPORTED)
    {
        DPRINT1("DxgkpVidMmTransferAllocationContent: paging transfer failed 0x%08lx alloc=%p\n", Status, Allocation);
        return Status;
    }
    Status = STATUS_NOT_SUPPORTED;

    if (VidMmSegmentIsCpuVisible(Segment))
    {
        /* CPU copy through the segment's write-combined mapping. */
        if (NT_SUCCESS(VidMmMapSegmentCpu(Segment)) &&
            Allocation->SegmentOffset + Allocation->Size <= Segment->Size)
        {
            PUCHAR SegmentVa = (PUCHAR)Segment->CpuBase + Allocation->SegmentOffset;

            if (ToSegment)
            {
                RtlCopyMemory(SegmentVa, Allocation->SystemMemory, Allocation->Size);
            }
            else
            {
                RtlCopyMemory(Allocation->SystemMemory, SegmentVa, Allocation->Size);
            }
            Status = STATUS_SUCCESS;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxgkpVidMmTransferAllocationContent: %s failed 0x%08lx "
                "alloc=%p\n",
                ToSegment ? "upload" : "download", Status, Allocation);
    }
    return Status;
}

static NTSTATUS
DxgkpVidMmNotifyLostMemoryPlacement(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter = Allocation->Adapter;
    DXGKARG_BUILDPAGINGBUFFER BuildArgs;
    NTSTATUS Status;

    if (Adapter == NULL || Adapter->MiniportContext == NULL || Adapter->MiniportDeviceContext == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer) == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireMiniportCallback(Adapter))
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
    BuildArgs.Operation = DXGK_OPERATION_DISCARD_CONTENT;
    BuildArgs.DiscardContent.hAllocation = Allocation->MiniportHandle;
    BuildArgs.DiscardContent.Flags.AllocationIsIdle = 1;
    BuildArgs.DiscardContent.SegmentId = Allocation->SegmentId;
    BuildArgs.DiscardContent.SegmentAddress.QuadPart = (LONGLONG)Allocation->SegmentOffset;
    Status = DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer)(Adapter->MiniportDeviceContext, &BuildArgs);
    DxgkReleaseMiniportCallback(Adapter);
    return Status;
}

static NTSTATUS
DxgkpVidMmForceUnmapUserMapping(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    BOOLEAN MappingPresent;
    BOOLEAN MappingComplete;

    (VOID)KeWaitForSingleObject(&Allocation->UserModeLock, Executive, KernelMode, FALSE, NULL);
    MappingPresent = Allocation->UserModeMapBase != NULL || Allocation->UserModeAddress != NULL || Allocation->UserModeMdl != NULL || Allocation->UserModeProcess != NULL;
    MappingComplete = Allocation->UserModeMapBase != NULL && Allocation->UserModeAddress != NULL && Allocation->UserModeMdl != NULL;
    if (MappingComplete)
        Allocation->UserModeLockCount = 1;
    KeReleaseMutex(&Allocation->UserModeLock, FALSE);
    if (MappingComplete)
        DxgkVidMmUnmapAllocationUser(Allocation);
    return !MappingPresent || MappingComplete ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
}

static NTSTATUS
DxgkpVidMmRecoverAllocationOwned(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter = Allocation->Adapter;
    PDXGKRNL_SEGMENT Segment;
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS CleanupStatus;

    if (!Allocation->Resident)
    {
        if (Allocation->ApertureMapped || Allocation->ApertureMdl != NULL)
            return DxgkpVidMmReleaseApertureMapping(Allocation, TRUE);
        return STATUS_SUCCESS;
    }
    if (Adapter == NULL || Adapter->Segments == NULL || Allocation->SegmentId < 1 || Allocation->SegmentId > Adapter->SegmentCount)
    {
        if (Allocation->ApertureMapped || Allocation->ApertureMdl != NULL)
            Status = DxgkpVidMmReleaseApertureMapping(Allocation, TRUE);
        DxgkpVidMmReleaseSegmentPlacement(Allocation);
        return NT_SUCCESS(Status) ? STATUS_INVALID_DEVICE_STATE : Status;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (VidMmSegmentIsAperture(Segment))
    {
        Status = DxgkpVidMmReleaseApertureMapping(Allocation, TRUE);
        DxgkpVidMmReleaseSegmentPlacement(Allocation);
        Allocation->ContentLost = FALSE;
        if (Allocation->SystemMemory != NULL)
            Allocation->PhysicalAddress = MmGetPhysicalAddress(Allocation->SystemMemory);
        return Status;
    }

    if (Allocation->ApertureMapped || Allocation->ApertureMdl != NULL)
        Status = DxgkpVidMmReleaseApertureMapping(Allocation, TRUE);
    CleanupStatus = DxgkpVidMmNotifyLostMemoryPlacement(Allocation);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(CleanupStatus))
        Status = CleanupStatus;
    CleanupStatus = DxgkpVidMmForceUnmapUserMapping(Allocation);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(CleanupStatus))
        Status = CleanupStatus;
    if (Allocation->CpuAddress != NULL && Allocation->CpuAddress != Allocation->SystemMemory)
        DxgkVidMmUnmapAllocationCpu(Allocation);
    Allocation->ContentLost = TRUE;
    DxgkpVidMmReleaseSegmentPlacement(Allocation);
    return Status;
}

NTSTATUS
DxgkVidMmPrepareForIdle(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGKVMM_ALLOCATION_SNAPSHOT Snapshot;
    NTSTATUS Status;
    NTSTATUS FirstFailure = STATUS_SUCCESS;
    SIZE_T Index;

    PAGED_CODE();
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) == 0 || Adapter->KmdExclusiveOwnerThread != PsGetCurrentThread())
        return STATUS_INVALID_DEVICE_STATE;
    Status = DxgkpVidMmCaptureAdapterAllocations(Adapter, TRUE, &Snapshot);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Snapshot.EntryCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Snapshot.Entries[Index].Allocation;

        Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
        if (!NT_SUCCESS(Status))
        {
            FirstFailure = Status;
            break;
        }
        if (DxgkSubmissionResidencyPinIsHeld(&Allocation->SubmissionResidencyPinCount))
            FirstFailure = STATUS_DEVICE_BUSY;
        else if (Allocation->Resident && Adapter->Segments != NULL && Allocation->SegmentId >= 1 && Allocation->SegmentId <= Adapter->SegmentCount && !VidMmSegmentIsAperture(&ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1]) && Allocation->UserModeAddress != NULL)
            FirstFailure = STATUS_DEVICE_BUSY;
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        if (!NT_SUCCESS(FirstFailure))
            break;
    }

    if (NT_SUCCESS(FirstFailure))
    {
        for (Index = 0; Index < Snapshot.EntryCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Snapshot.Entries[Index].Allocation;
            BOOLEAN Resident;
            BOOLEAN ReleaseAperture;
            ULONG OwnerToken;

            Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
            if (!NT_SUCCESS(Status))
            {
                if (NT_SUCCESS(FirstFailure))
                    FirstFailure = Status;
                continue;
            }
            Resident = Allocation->Resident;
            ReleaseAperture =
                Allocation->ApertureMapped ||
                Allocation->ApertureMdl != NULL;
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            if (Resident)
            {
                Status = DxgkVidMmEvict(Allocation);
            }
            else if (ReleaseAperture)
            {
                Status = DxgkpVidMmBeginResidencyTransaction(
                             Allocation,
                             &OwnerToken);
                if (NT_SUCCESS(Status))
                {
                    Status = DxgkpVidMmReleaseApertureMapping(Allocation,
                                                             FALSE);
                    DxgkpVidMmEndResidencyTransaction(Allocation,
                                                     &OwnerToken);
                }
            }
            else
            {
                Status = STATUS_SUCCESS;
            }
            if (NT_SUCCESS(FirstFailure) && !NT_SUCCESS(Status))
                FirstFailure = Status;
        }
    }

    DxgkpVidMmReleaseAllocationSnapshot(&Snapshot);
    return FirstFailure;
}

NTSTATUS
DxgkVidMmRecoverFromTimeout(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    DXGKVMM_ALLOCATION_SNAPSHOT Snapshot;
    NTSTATUS Status;
    NTSTATUS FirstFailure = STATUS_SUCCESS;
    SIZE_T Index;

    PAGED_CODE();
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) == 0 || Adapter->KmdExclusiveOwnerThread != PsGetCurrentThread())
        return STATUS_INVALID_DEVICE_STATE;
    Status = DxgkpVidMmCaptureAdapterAllocations(Adapter, TRUE, &Snapshot);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Snapshot.EntryCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Snapshot.Entries[Index].Allocation;
        ULONG OwnerToken;

        Status = DxgkpVidMmBeginResidencyTransaction(Allocation,
                                                    &OwnerToken);
        if (NT_SUCCESS(Status))
        {
            Status = DxgkpVidMmRecoverAllocationOwned(Allocation);
            DxgkpVidMmEndResidencyTransaction(Allocation, &OwnerToken);
        }
        if (NT_SUCCESS(FirstFailure) && !NT_SUCCESS(Status))
            FirstFailure = Status;
    }

    DxgkpVidMmReleaseAllocationSnapshot(&Snapshot);
    return FirstFailure;
}

VOID
DxgkVidMmFillAllocationListEntry(
    _In_ D3DKMT_HANDLE AllocationHandle,
    _Inout_ DXGK_ALLOCATIONLIST *ListEntry)
{
    PDXGKVMM_ALLOCATION Allocation;

    if (ListEntry == NULL || AllocationHandle == 0)
        return;

    if (!NT_SUCCESS(DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)AllocationHandle, NULL, NULL, &Allocation)))
        return;

    /* SegmentId is a 5-bit field in DXGK_ALLOCATIONLIST. */
    ListEntry->SegmentId = (Allocation->SegmentId <= 31) ? Allocation->SegmentId : 0;
    ListEntry->PhysicalAddress = Allocation->PhysicalAddress;
    DxgkVidMmDereferenceAllocation(Allocation);
}

NTSTATUS
DxgkVidMmAcquireSubmissionResidencyPin(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _Out_opt_ DXGK_ALLOCATIONLIST *ListEntry)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Allocation == NULL || ExpectedAdapter == NULL || Allocation->Adapter != ExpectedAdapter)
        return STATUS_INVALID_PARAMETER;
    /* Scheduler admission depends on the paging fence: an allocation whose
     * paging packet has not retired does not yet have the placement this
     * submission would patch into its DMA buffer. */
    if (KeGetCurrentIrql() == PASSIVE_LEVEL && !DxgkPagingAllocationReadyForSubmission(Allocation))
    {
        Status = DxgkPagingWaitForFence(ExpectedAdapter, Allocation->PagingFenceId, DXGKP_VIDMM_PAGING_ADMISSION_TIMEOUT_MS);
        if (!NT_SUCCESS(Status))
            return Status == STATUS_TIMEOUT ? STATUS_GRAPHICS_ALLOCATION_BUSY : Status;
    }
    Status = DxgkpVidMmLockResidencyForExternalOperation(Allocation);
    if (!NT_SUCCESS(Status))
        return Status;
    (VOID)DxgkPagingSyncPlacement(Allocation);
    if (!DxgkPagingAllocationReadyForSubmission(Allocation))
        Status = STATUS_GRAPHICS_ALLOCATION_BUSY;
    else if (!Allocation->Resident || ExpectedAdapter->Segments == NULL || Allocation->SegmentId == 0 || Allocation->SegmentId > ExpectedAdapter->SegmentCount || Allocation->SegmentId > 31)
        Status = STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    else if (!DxgkSubmissionResidencyPinTryAcquire(&Allocation->SubmissionResidencyPinCount))
        Status = STATUS_INTEGER_OVERFLOW;
    else
    {
        if (ListEntry != NULL)
        {
            ListEntry->Value = 0;
            ListEntry->SegmentId = Allocation->SegmentId;
            ListEntry->PhysicalAddress = Allocation->PhysicalAddress;
        }
    }
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    return Status;
}

VOID
DxgkVidMmReleaseSubmissionResidencyPin(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    BOOLEAN Released;

    ASSERT(Allocation != NULL);
    Released = DxgkSubmissionResidencyPinRelease(&Allocation->SubmissionResidencyPinCount);
    ASSERT(Released);
}

static NTSTATUS
DxgkpVidMmBuildAllocationUserMdl(
    _In_  PDXGKVMM_ALLOCATION  Allocation,
    _Out_ PMDL                *OutMdl,
    _Out_ ULONG_PTR           *OutUserOffset,
    _Out_ MEMORY_CACHING_TYPE *OutCacheType)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment;
    PHYSICAL_ADDRESS PhysicalAddress;
    PFN_NUMBER *Pages;
    PFN_NUMBER FirstPfn;
    ULONG Offset;
    ULONG PageCount;
    ULONG MappingSize;
    PMDL Mdl;
    ULONG i;
    NTSTATUS Status;

    ASSERT(Allocation != NULL);
    ASSERT(OutMdl != NULL);
    ASSERT(OutUserOffset != NULL);
    ASSERT(OutCacheType != NULL);

    *OutMdl = NULL;
    *OutUserOffset = 0;
    *OutCacheType = MmCached;

    if (Allocation->ContentLost)
        return STATUS_GRAPHICS_ALLOCATION_CONTENT_LOST;

    if (Allocation->Size == 0 || Allocation->Size > MAXULONG)
        return STATUS_INVALID_PARAMETER;

    /*
     * Pure system-memory allocations can be directly mapped into user mode
     * via an MDL built over the nonpaged pool backing store.
     *
     * Resident aperture allocations also have SystemMemory backing, but they
     * must establish MAP_APERTURE_SEGMENT first so the miniport can attach
     * that backing to the GPU resource before user mode starts accessing it.
     */
    /* GPU-visible mappings must target the FINAL placement: a user map
     * built over the pool backing goes permanently stale the moment the
     * allocation is placed in a segment (the CPU then writes pages the
     * GPU never reads).  Place first; pool mapping only if that fails. */
    if (!Allocation->Resident && Allocation->Adapter != NULL &&
        Allocation->Adapter->Segments != NULL)
    {
        (VOID)DxgkVidMmMakeResident(Allocation, Allocation->Adapter);
    }

    if (!Allocation->Resident)
    {
        if (Allocation->SystemMemory != NULL)
        {
            Mdl = IoAllocateMdl(Allocation->SystemMemory,
                                (ULONG)Allocation->Size,
                                FALSE,
                                FALSE,
                                NULL);
            if (Mdl == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            MmBuildMdlForNonPagedPool(Mdl);
            *OutMdl = Mdl;
            *OutCacheType = MmCached;
            return STATUS_SUCCESS;
        }

        if (Allocation->PhysicalAddress.QuadPart == 0)
            return STATUS_INVALID_PARAMETER;

        /* Fall through to physical mapping for pre-mapped allocations. */
    }

    if (Allocation->Resident)
    {
        Adapter = Allocation->Adapter;
        if (Adapter == NULL || Adapter->Segments == NULL)
            return STATUS_INVALID_PARAMETER;

        if (Allocation->SegmentId < 1 || Allocation->SegmentId > Adapter->SegmentCount)
            return STATUS_INVALID_PARAMETER;

        Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
        if (Segment->Flags.PitchAlignment)
            return STATUS_NOT_SUPPORTED;
        if (VidMmSegmentIsAperture(Segment))
        {
            if (Allocation->SystemMemory == NULL)
                return STATUS_INVALID_PARAMETER;

            Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
            if (!NT_SUCCESS(Status))
                return Status;

            ASSERT(Allocation->ApertureMapped);

            Mdl = IoAllocateMdl(Allocation->SystemMemory,
                                (ULONG)Allocation->Size,
                                FALSE,
                                FALSE,
                                NULL);
            if (Mdl == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            MmBuildMdlForNonPagedPool(Mdl);
            *OutMdl = Mdl;
            *OutCacheType = MmCached;
            return STATUS_SUCCESS;
        }

        if (!VidMmSegmentIsCpuVisible(Segment))
            return STATUS_INVALID_PARAMETER;
    }

    PhysicalAddress = Allocation->PhysicalAddress;
    if (PhysicalAddress.QuadPart == 0)
        return STATUS_INVALID_PARAMETER;

    Offset = (ULONG)(PhysicalAddress.QuadPart & (PAGE_SIZE - 1));
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Offset, Allocation->Size);
    if (PageCount == 0 || PageCount > (MAXULONG >> PAGE_SHIFT))
        return STATUS_INVALID_PARAMETER;

    MappingSize = PageCount << PAGE_SHIFT;
    Mdl = IoAllocateMdl(NULL, MappingSize, FALSE, FALSE, NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Pages = MmGetMdlPfnArray(Mdl);
    FirstPfn = (PFN_NUMBER)(PhysicalAddress.QuadPart >> PAGE_SHIFT);
    for (i = 0; i < PageCount; ++i)
        Pages[i] = FirstPfn + i;

    Mdl->MdlFlags |= MDL_PAGES_LOCKED | MDL_IO_SPACE;

    *OutMdl = Mdl;
    *OutUserOffset = Offset;
    *OutCacheType = MmWriteCombined;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidMmMapAllocationUser(
    _In_  PDXGKVMM_ALLOCATION   Allocation,
    _Out_ PVOID                *OutVa)
{
    PMDL Mdl = NULL;
    MEMORY_CACHING_TYPE CacheType;
    ULONG_PTR UserOffset;
    PVOID UserMapBase;
    PVOID UserVa;
    NTSTATUS Status;
    PEPROCESS Process;

    ASSERT(Allocation != NULL);
    ASSERT(OutVa != NULL);

    *OutVa = NULL;
    Process = PsGetCurrentProcess();

    (VOID)KeWaitForSingleObject(&Allocation->UserModeLock, Executive, KernelMode, FALSE, NULL);

    if (Allocation->UserModeAddress != NULL)
    {
        if (Allocation->UserModeProcess != Process)
        {
            DPRINT1("DxgkVidMmMapAllocationUser: alloc %p already mapped in process %p (current %p)\n",
                    Allocation, Allocation->UserModeProcess, Process);
            KeReleaseMutex(&Allocation->UserModeLock, FALSE);
            return STATUS_DEVICE_BUSY;
        }

        Allocation->UserModeLockCount++;
        *OutVa = Allocation->UserModeAddress;
        KeReleaseMutex(&Allocation->UserModeLock, FALSE);
        return STATUS_SUCCESS;
    }

    Status = DxgkpVidMmBuildAllocationUserMdl(Allocation,
                                              &Mdl,
                                              &UserOffset,
                                              &CacheType);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutex(&Allocation->UserModeLock, FALSE);
        return Status;
    }

    UserMapBase = NULL;
    _SEH2_TRY
    {
        UserMapBase = MmMapLockedPagesSpecifyCache(Mdl,
                                                   UserMode,
                                                   CacheType,
                                                   NULL,
                                                   FALSE,
                                                   NormalPagePriority);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status) || UserMapBase == NULL)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_INSUFFICIENT_RESOURCES;

        IoFreeMdl(Mdl);
        KeReleaseMutex(&Allocation->UserModeLock, FALSE);
        return Status;
    }

    UserVa = (PVOID)((ULONG_PTR)UserMapBase + UserOffset);
    ASSERT((ULONG_PTR)UserVa < (ULONG_PTR)MmSystemRangeStart);

    ObReferenceObject(Process);
    Allocation->UserModeMapBase = UserMapBase;
    Allocation->UserModeAddress = UserVa;
    Allocation->UserModeMdl = Mdl;
    Allocation->UserModeProcess = Process;
    Allocation->UserModeLockCount = 1;

    *OutVa = UserVa;

    KeReleaseMutex(&Allocation->UserModeLock, FALSE);

    DXGKRNL_VERBOSE("DxgkVidMmMapAllocationUser: alloc=%p handle=0x%X map=%p user=%p mdl=%p start=%p byteOffset=0x%lx byteCount=0x%lx flags=0x%lx sysmem=%p resident=%u segment=%u pa=0x%I64x process=%p\n",
                  Allocation,
                  Allocation->Handle,
                  UserMapBase,
                  UserVa,
                  Mdl,
                  MmGetMdlVirtualAddress(Mdl),
                  MmGetMdlByteOffset(Mdl),
                  MmGetMdlByteCount(Mdl),
                  Mdl->MdlFlags,
                  Allocation->SystemMemory,
                  Allocation->Resident,
                  Allocation->SegmentId,
                  Allocation->PhysicalAddress.QuadPart,
                  Process);

    return STATUS_SUCCESS;
}


/*
 * DxgkVidMmMapAllocationCpu
 *
 * Maps the allocation's backing storage into kernel-mode VA.
 * Sets Allocation->CpuAddress on success; *OutVa receives the address.
 *
 * Three cases:
 *   A) Resident in CPU-visible VRAM segment → map segment, return offset ptr.
 *   B) Resident in aperture segment → use system memory backing if present.
 *   C) Not resident (system memory) → return SystemMemory pointer.
 */
NTSTATUS
DxgkVidMmMapAllocationCpu(
    _In_  PDXGKVMM_ALLOCATION   Allocation,
    _Out_ PVOID                *OutVa)
{
    PDXGKRNL_ADAPTER    Adapter;
    PDXGKRNL_SEGMENT    Segment;
    NTSTATUS            Status;

    ASSERT(Allocation != NULL);
    ASSERT(OutVa != NULL);

    *OutVa = NULL;

    if (Allocation->ContentLost)
        return STATUS_GRAPHICS_ALLOCATION_CONTENT_LOST;

    if (!Allocation->CpuVisible)
    {
        DPRINT1("DxgkVidMmMapAllocationCpu: alloc %p is not CPU-visible\n",
                Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    /* Pre-mapped allocation (e.g., GOP framebuffer): CpuAddress is already set. */
    if (Allocation->CpuAddress != NULL && !Allocation->Resident)
    {
        *OutVa = Allocation->CpuAddress;
        return STATUS_SUCCESS;
    }

    /* Case C: not resident — return system memory pointer. */
    if (!Allocation->Resident)
    {
        if (Allocation->SystemMemory == NULL)
        {
            DPRINT1("DxgkVidMmMapAllocationCpu: alloc %p not resident "
                    "and no system memory\n", Allocation);
            return STATUS_INVALID_PARAMETER;
        }

        Allocation->CpuAddress = Allocation->SystemMemory;
        *OutVa                 = Allocation->CpuAddress;
        return STATUS_SUCCESS;
    }

    /* Allocation is resident in a GPU segment. */
    Adapter = Allocation->Adapter;

    if (Adapter == NULL || Adapter->Segments == NULL)
    {
        DPRINT1("DxgkVidMmMapAllocationCpu: null adapter/segments for "
                "alloc %p\n", Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    if (Allocation->SegmentId < 1 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        DPRINT1("DxgkVidMmMapAllocationCpu: invalid SegmentId %lu\n",
                Allocation->SegmentId);
        return STATUS_INVALID_PARAMETER;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (Segment->Flags.PitchAlignment)
        return STATUS_NOT_SUPPORTED;

    /* Case B: aperture segment → use system backing. */
    if (VidMmSegmentIsAperture(Segment))
    {
        if (Allocation->SystemMemory != NULL)
        {
            Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
            if (!NT_SUCCESS(Status))
                return Status;

            ASSERT(Allocation->ApertureMapped);

            Allocation->CpuAddress = Allocation->SystemMemory;
            *OutVa                 = Allocation->CpuAddress;
            return STATUS_SUCCESS;
        }
        DPRINT1("DxgkVidMmMapAllocationCpu: aperture alloc %p has no "
                "system memory\n", Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    /* Case A: VRAM, CPU-visible via PCI BAR. */
    if (!VidMmSegmentIsCpuVisible(Segment))
    {
        DPRINT1("DxgkVidMmMapAllocationCpu: segment %lu is not "
                "CPU-visible\n", Segment->SegmentId);
        return STATUS_INVALID_PARAMETER;
    }

    /* Establish segment CPU mapping lazily. */
    Status = VidMmMapSegmentCpu(Segment);

    if (!NT_SUCCESS(Status))
        return Status;

    ASSERT(Segment->CpuBase != NULL);
    ASSERT(Allocation->SegmentOffset < Segment->Size);

    /*
     * Return a pointer into the segment mapping at the allocation's offset.
     *
     * Cast through ULONG_PTR (not ULONG) to guarantee 64-bit pointer
     * arithmetic on amd64: (PVOID + ULONGLONG) is not directly portable,
     * but (ULONG_PTR) + (ULONG_PTR) is well-defined.
     */
    Allocation->CpuAddress = (PVOID)((ULONG_PTR)Segment->CpuBase
                                     + (ULONG_PTR)Allocation->SegmentOffset);

    *OutVa = Allocation->CpuAddress;

    DPRINT("DxgkVidMmMapAllocationCpu: alloc %p VA=%p "
           "(seg %lu offset=0x%I64x)\n",
           Allocation, *OutVa,
           Segment->SegmentId, Allocation->SegmentOffset);

    return STATUS_SUCCESS;
}


/*
 * DxgkVidMmUnmapAllocationCpu
 *
 * Clears the per-allocation CPU mapping pointer.  The segment-level
 * MmMapIoSpace mapping is shared and is released only at adapter teardown
 * via DxgkVidMmTeardownAdapter.
 *
 * For system-memory backed allocations CpuAddress points into SystemMemory;
 * we just clear the pointer — the memory itself survives until
 * DxgkVidMmDestroyAllocation frees it.
 */
VOID
DxgkVidMmUnmapAllocationCpu(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    DPRINT("DxgkVidMmUnmapAllocationCpu: Alloc=%p CpuAddr=%p\n",
           Allocation, Allocation->CpuAddress);

    Allocation->CpuAddress = NULL;
}

VOID
DxgkVidMmUnmapAllocationUser(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PMDL Mdl;
    PVOID UserMapBase;
    PVOID UserVa;
    PEPROCESS Process;
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;

    ASSERT(Allocation != NULL);

    (VOID)KeWaitForSingleObject(&Allocation->UserModeLock, Executive, KernelMode, FALSE, NULL);

    if (Allocation->UserModeMapBase == NULL ||
        Allocation->UserModeAddress == NULL ||
        Allocation->UserModeMdl == NULL)
    {
        KeReleaseMutex(&Allocation->UserModeLock, FALSE);
        return;
    }

    if (Allocation->UserModeLockCount > 1)
    {
        DXGKRNL_VERBOSE("DxgkVidMmUnmapAllocationUser: alloc=%p handle=0x%X deferred lockCount=%lu\n",
                      Allocation,
                      Allocation->Handle,
                      Allocation->UserModeLockCount);
        Allocation->UserModeLockCount--;
        KeReleaseMutex(&Allocation->UserModeLock, FALSE);
        return;
    }

    UserMapBase = Allocation->UserModeMapBase;
    UserVa = Allocation->UserModeAddress;
    Mdl = Allocation->UserModeMdl;
    Process = Allocation->UserModeProcess;
    (VOID)UserVa;

    Allocation->UserModeMapBase = NULL;
    Allocation->UserModeAddress = NULL;
    Allocation->UserModeMdl = NULL;
    Allocation->UserModeProcess = NULL;
    Allocation->UserModeLockCount = 0;

    KeReleaseMutex(&Allocation->UserModeLock, FALSE);

    if (Process != NULL && Process != PsGetCurrentProcess())
    {
        KeStackAttachProcess((PKPROCESS)Process, &ApcState);
        Attached = TRUE;
    }

    MmUnmapLockedPages(UserMapBase, Mdl);

    if (Attached)
        KeUnstackDetachProcess(&ApcState);

    IoFreeMdl(Mdl);

    if (Process != NULL)
        ObDereferenceObject(Process);

    DXGKRNL_VERBOSE("DxgkVidMmUnmapAllocationUser: alloc=%p handle=0x%X map=%p user=%p mdl=%p start=%p byteOffset=0x%lx byteCount=0x%lx flags=0x%lx process=%p\n",
                  Allocation,
                  Allocation->Handle,
                  UserMapBase,
                  UserVa,
                  Mdl,
                  MmGetMdlVirtualAddress(Mdl),
                  MmGetMdlByteOffset(Mdl),
                  MmGetMdlByteCount(Mdl),
                  Mdl->MdlFlags,
                  Process);
}

VOID
DxgkVidMmProcessCleanup(
    _In_ PEPROCESS Process)
{
    D3DKMT_HANDLE *HandleArray = NULL;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Index = 0;

    PAGED_CODE();
    ASSERT(Process != NULL);

    DxgkpVidMmEnsureGlobalsInitialized();

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Entry = DxgkVidMmAllocationListHead.Flink;
         Entry != &DxgkVidMmAllocationListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Alloc;

        Alloc = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);
        if (Alloc->UserModeProcess == Process &&
            Alloc->UserModeMapBase != NULL &&
            Alloc->UserModeAddress != NULL &&
            Alloc->UserModeMdl != NULL)
        {
            Count++;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    if (Count == 0)
        return;

    HandleArray = ExAllocatePoolWithTag(NonPagedPool,
                                        Count * sizeof(*HandleArray),
                                        TAG_VIDMM_ALLOC);
    if (HandleArray == NULL)
    {
        DPRINT1("DxgkVidMmProcessCleanup: unable to allocate handle array for %lu user mappings\n",
                Count);
        return;
    }

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Entry = DxgkVidMmAllocationListHead.Flink;
         Entry != &DxgkVidMmAllocationListHead && Index < Count;
         Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Alloc;

        Alloc = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);
        if (Alloc->UserModeProcess == Process &&
            Alloc->UserModeMapBase != NULL &&
            Alloc->UserModeAddress != NULL &&
            Alloc->UserModeMdl != NULL)
        {
            HandleArray[Index++] = Alloc->Handle;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    while (Index != 0)
    {
        PDXGKVMM_ALLOCATION Alloc;

        if (!NT_SUCCESS(DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)HandleArray[--Index], NULL, NULL, &Alloc)))
            continue;

        (VOID)KeWaitForSingleObject(&Alloc->UserModeLock, Executive, KernelMode, FALSE, NULL);
        if (Alloc->UserModeProcess == Process &&
            Alloc->UserModeMapBase != NULL &&
            Alloc->UserModeAddress != NULL &&
            Alloc->UserModeMdl != NULL)
        {
            if (Alloc->UserModeLockCount > 1)
            {
                DXGKRNL_WARN("DxgkVidMmProcessCleanup: forcing unlock of alloc=%p handle=0x%X lockCount=%lu for exiting process %p\n",
                             Alloc,
                             Alloc->Handle,
                             Alloc->UserModeLockCount,
                             Process);
            }

            /*
             * Process teardown must drop the mapping regardless of how many
             * D3DKMT locks the runtime leaked. There will be no matching
             * unlocks after the address space is gone.
             */
            Alloc->UserModeLockCount = 1;
        }
        KeReleaseMutex(&Alloc->UserModeLock, FALSE);

        DxgkVidMmUnmapAllocationUser(Alloc);
        DxgkVidMmDereferenceAllocation(Alloc);
    }

    ExFreePoolWithTag(HandleArray, TAG_VIDMM_ALLOC);
}

static VOID
DxgkpVidMmCleanupAllocations(
    _In_opt_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    PDXGKRNL_ADAPTER OwningAdapter = Device != NULL ? Device->Adapter : Adapter;

    ASSERT(OwningAdapter != NULL);
    for (;;)
    {
        PDXGKVMM_ALLOCATION Allocation;
        PLIST_ENTRY Entry;
        HANDLE AllocationHandle = NULL;
        NTSTATUS Status;

        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        for (Entry = DxgkVidMmAllocationListHead.Flink; Entry != &DxgkVidMmAllocationListHead; Entry = Entry->Flink)
        {
            Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);
            if (((Device != NULL && Allocation->Device == Device) || (Device == NULL && Allocation->Adapter == Adapter)) && Allocation->Resource == NULL && InterlockedCompareExchange(&Allocation->Destroying, 0, 0) == 0)
            {
                AllocationHandle = (HANDLE)(ULONG_PTR)Allocation->Handle;
                break;
            }
        }
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        if (AllocationHandle == NULL)
            break;
        Status = DxgkpVidMmDestroyAllocation(OwningAdapter, Device, NULL, AllocationHandle);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkpVidMmCleanupAllocations: failed to destroy allocation %p\n", AllocationHandle);
            break;
        }
    }

    for (;;)
    {
        PDXGKVMM_RESOURCE Resource = NULL;
        PLIST_ENTRY Entry;
        NTSTATUS Status;

        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        for (Entry = DxgkVidMmResourceListHead.Flink; Entry != &DxgkVidMmResourceListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_RESOURCE Candidate = CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry);

            if ((Device != NULL && Candidate->Device == Device) || (Device == NULL && Candidate->Adapter == Adapter))
            {
                Resource = Candidate;
                break;
            }
        }
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (Resource == NULL)
            break;
        Status = DxgkpVidMmDestroyResourceWrapper(OwningAdapter, Resource);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkpVidMmCleanupAllocations: failed to destroy resource %p status 0x%08lX\n", Resource, Status);
            break;
        }
    }
}

static NTSTATUS
DxgkpVidMmReleaseDeviceResidencyReferencesForCleanup(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device)
{
    DXGKVMM_ALLOCATION_SNAPSHOT Snapshot;
    NTSTATUS Status;
    SIZE_T Index;

    /*
     * Explicit residency is charged to the physical backing, including when
     * the caller used an opened/shared allocation.  Device teardown removes
     * all of that device's handles before freeing the device object, so purge
     * its charges from every still-published physical backing first.
     *
     * Do not include destroy-batch entries here.  They are no longer
     * published to callers and their finalizers drain residency state; unlike
     * global entries, a batch lifetime reference does not itself hold each
     * allocation object alive while a worker finalizes the batch.
     */
    Status = DxgkpVidMmCaptureAdapterAllocations(Adapter, FALSE, &Snapshot);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Snapshot.EntryCount; ++Index)
        DxgkVidMmReleaseAllDeviceResidencyReferences(Snapshot.Entries[Index].Allocation, Device);

    DxgkpVidMmReleaseAllocationSnapshot(&Snapshot);
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkVidMmCleanupDeviceAllocations(
    _In_ PDXGKRNL_DEVICE Device)
{
    PDXGKRNL_ADAPTER Adapter;
    HANDLE MiniportDeviceHandle;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    BOOLEAN RetainedObjects = FALSE;

    PAGED_CODE();
    if (Device == NULL || Device->Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    Adapter = Device->Adapter;
    MiniportDeviceHandle = Device->hMiniportDevice;
    if (Adapter->MiniportRemoveDeviceComplete)
    {
        if (!Adapter->MiniportDeviceStopped || InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) == 0)
            return STATUS_INVALID_DEVICE_STATE;
    }

    Status = DxgkpVidMmReleaseDeviceResidencyReferencesForCleanup(Adapter, Device);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Adapter->MiniportRemoveDeviceComplete)
    {
        ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
        for (Entry = DxgkVidMmDestroyBatchListHead.Flink; Entry != &DxgkVidMmDestroyBatchListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);
            UINT Index;

            if (Batch->Adapter != Adapter)
                continue;
            if (Batch->MiniportDeviceHandle == MiniportDeviceHandle)
                Batch->MiniportDeviceHandle = NULL;
            if (Batch->Resource != NULL && Batch->Resource->Device == Device)
                Batch->Resource->Device = NULL;
            for (Index = 0; Index < Batch->AllocationCount; ++Index)
            {
                if (Batch->Allocations[Index] != NULL && Batch->Allocations[Index]->Device == Device)
                {
                    Batch->Allocations[Index]->Device = NULL;
                    Batch->Allocations[Index]->MiniportDeviceHandle = NULL;
                }
            }
        }
        ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);
        ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
        for (Entry = DxgkVidMmAllocationListHead.Flink; Entry != &DxgkVidMmAllocationListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);

            if (Allocation->Device == Device)
            {
                Allocation->Device = NULL;
                Allocation->MiniportDeviceHandle = NULL;
            }
        }
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        for (Entry = DxgkVidMmResourceListHead.Flink; Entry != &DxgkVidMmResourceListHead; Entry = Entry->Flink)
        {
            PDXGKVMM_RESOURCE Resource = CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry);

            if (Resource->Device == Device)
                Resource->Device = NULL;
        }
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        return STATUS_SUCCESS;
    }
    DxgkpVidMmCleanupAllocations(NULL, Device);
    if (InterlockedCompareExchange(&Adapter->VidMmDestroyQueuesBlocked, 0, 0) != 0)
    {
        while (DxgkpVidMmForceQuarantinedDestroyBatches(Adapter) != 0)
            NOTHING;
        DxgkpVidMmCleanupAllocations(NULL, Device);
    }

    ExAcquireFastMutex(&DxgkVidMmDestroyBatchListLock);
    for (Entry = DxgkVidMmDestroyBatchListHead.Flink; Entry != &DxgkVidMmDestroyBatchListHead; Entry = Entry->Flink)
    {
        PDXGKVMM_DESTROY_BATCH Batch = CONTAINING_RECORD(Entry, DXGKVMM_DESTROY_BATCH, QuarantineEntry);
        if (Batch->Adapter != Adapter || Batch->MiniportDeviceHandle != MiniportDeviceHandle)
            continue;
        RetainedObjects = TRUE;
    }
    ExReleaseFastMutex(&DxgkVidMmDestroyBatchListLock);

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Entry = DxgkVidMmAllocationListHead.Flink; Entry != &DxgkVidMmAllocationListHead; Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);

        if (Allocation->Device == Device)
            RetainedObjects = TRUE;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    for (Entry = DxgkVidMmResourceListHead.Flink; Entry != &DxgkVidMmResourceListHead; Entry = Entry->Flink)
    {
        PDXGKVMM_RESOURCE Resource = CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry);

        if (Resource->Device == Device)
            RetainedObjects = TRUE;
    }
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    return RetainedObjects ? STATUS_DEVICE_BUSY : STATUS_SUCCESS;
}

VOID
DxgkVidMmCleanupAdapterAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    if (Adapter != NULL)
        DxgkpVidMmCleanupAllocations(Adapter, NULL);
}

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static ULONG
DxgkpVidMmBudgetGroupIndex(
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP Group)
{
    return (Group == D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL) ? 0 : 1;
}

static PDXGKVMM_PROCESS_BUDGET
DxgkpVidMmFindProcessBudgetLocked(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkVidMmProcessBudgetListHead.Flink;
         Entry != &DxgkVidMmProcessBudgetListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_PROCESS_BUDGET Record =
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET, Link);

        if (Record->Adapter == Adapter && Record->Process == Process)
            return Record;
    }
    return NULL;
}

static PDXGKVMM_PROCESS_BUDGET_GATE
DxgkpVidMmFindProcessBudgetGateLocked(
    _In_ PEPROCESS Process)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkVidMmProcessBudgetGateListHead.Flink;
         Entry != &DxgkVidMmProcessBudgetGateListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_PROCESS_BUDGET_GATE Gate =
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET_GATE, Link);

        if (Gate->Process == Process)
            return Gate;
    }
    return NULL;
}

static PDXGKVMM_PROCESS_BUDGET_GATE
DxgkpVidMmEnterProcessBudgetGate(
    _In_ PEPROCESS Process,
    _Out_ NTSTATUS *OutStatus)
{
    PDXGKVMM_PROCESS_BUDGET_GATE Gate;
    PDXGKVMM_PROCESS_BUDGET_GATE Candidate;
    PDXGKVMM_PROCESS_BUDGET_GATE RetiredGate = NULL;
    BOOLEAN ProcessAlreadyExiting;

    Candidate = ExAllocatePoolWithTag(
                    NonPagedPool,
                    sizeof(*Candidate),
                    TAG_VIDMM_ALLOC);
    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    Gate = DxgkpVidMmFindProcessBudgetGateLocked(Process);
    if (Gate == NULL && Candidate != NULL)
    {
        RtlZeroMemory(Candidate, sizeof(*Candidate));
        Candidate->Process = Process;
        DxgkResidencyCoreProcessAdmissionInitialize(&Candidate->Admission);
        Gate = Candidate;
        Candidate = NULL;
        InsertTailList(&DxgkVidMmProcessBudgetGateListHead, &Gate->Link);
    }

    ProcessAlreadyExiting =
        PsGetProcessExitStatus(Process) != STATUS_PENDING;
    if (Gate == NULL)
    {
        *OutStatus = ProcessAlreadyExiting
                         ? STATUS_PROCESS_IS_TERMINATING
                         : STATUS_INSUFFICIENT_RESOURCES;
    }
    else if (!DxgkResidencyCoreProcessAdmissionTryEnter(
                  &Gate->Admission,
                  ProcessAlreadyExiting))
    {
        ProcessAlreadyExiting = Gate->Admission.Exiting;
        if (Gate->Admission.ActiveEntrants == 0)
        {
            RemoveEntryList(&Gate->Link);
            RetiredGate = Gate;
        }
        Gate = NULL;
        *OutStatus = ProcessAlreadyExiting
                         ? STATUS_PROCESS_IS_TERMINATING
                         : STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        *OutStatus = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);

    if (Candidate != NULL)
        ExFreePoolWithTag(Candidate, TAG_VIDMM_ALLOC);
    if (RetiredGate != NULL)
        ExFreePoolWithTag(RetiredGate, TAG_VIDMM_ALLOC);
    return Gate;
}

static VOID
DxgkpVidMmLeaveProcessBudgetGate(
    _In_ PDXGKVMM_PROCESS_BUDGET_GATE Gate)
{
    BOOLEAN Retire;

    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    ASSERT(Gate->Admission.ActiveEntrants != 0);
    Retire = DxgkResidencyCoreProcessAdmissionLeave(&Gate->Admission);
    if (Retire)
    {
        ASSERT(Gate->Admission.ActiveEntrants == 0);
        RemoveEntryList(&Gate->Link);
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    if (Retire)
        ExFreePoolWithTag(Gate, TAG_VIDMM_ALLOC);
}

static BOOLEAN
DxgkpVidMmProcessBudgetCanRetireLocked(
    _In_ PDXGKVMM_PROCESS_BUDGET Record)
{
    return Record->ProcessExiting &&
           Record->ActiveTransactions == 0 &&
           Record->ChargeEntryCount == 0 &&
           Record->Groups[0].Reservation == 0 &&
           Record->Groups[1].Reservation == 0;
}

static VOID
DxgkpVidMmFreeProcessBudget(
    _In_ PDXGKVMM_PROCESS_BUDGET Record)
{
    ObDereferenceObject(Record->Process);
    ExFreePoolWithTag(Record, TAG_VIDMM_ALLOC);
}

static NTSTATUS
DxgkpVidMmQueryGroupMaximum(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP Group,
    _Out_ PULONGLONG Maximum)
{
    PDXGKRNL_SEGMENT Segments;
    D3DKMT_SEGMENTGROUPSIZEINFO SegmentInfo;
    VIDMM_BUDGET_GROUP RequestedGroup;
    ULONGLONG ApertureBytes = 0;
    ULONGLONG MemoryBytes = 0;
    ULONG Index;
    NTSTATUS Status;

    if (Maximum == NULL)
        return STATUS_INVALID_PARAMETER;
    *Maximum = 0;
    if (Adapter == NULL ||
        (Group != D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL &&
         Group != D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (DxgkpVidMmEffectiveWddmLevel(Adapter) <
        DXGK_CAPS_CORE_LEVEL_WDDM_2_0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&SegmentInfo, sizeof(SegmentInfo));
    Status = DxgkVidMmQuerySegmentSizes(Adapter, &SegmentInfo);
    if (!NT_SUCCESS(Status))
        return Status;
    Segments = ADAPTER_SEGMENTS(Adapter);
    if (Adapter->SegmentCount != 0 && Segments == NULL)
        return STATUS_DEVICE_NOT_READY;
    RequestedGroup =
        (Group == D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL)
            ? VidMmBudgetGroupLocal
            : VidMmBudgetGroupNonLocal;

    for (Index = 0; Index < Adapter->SegmentCount; ++Index)
    {
        ULONGLONG PlacementLimit;

        ExAcquireFastMutex(&Segments[Index].Lock);
        if (VidMmSegmentBudgetGroup(Adapter, &Segments[Index]) ==
            RequestedGroup)
        {
            PlacementLimit = VidMmSegmentPlacementLimit(&Segments[Index]);
            if (VidMmSegmentIsAperture(&Segments[Index]))
                DxgkpVidMmSaturatingAdd(&ApertureBytes, PlacementLimit);
            else
                DxgkpVidMmSaturatingAdd(&MemoryBytes, PlacementLimit);
        }
        ExReleaseFastMutex(&Segments[Index].Lock);
    }

    *Maximum = MemoryBytes;
    DxgkpVidMmSaturatingAdd(
        Maximum,
        min(ApertureBytes, SegmentInfo.LegacyInfo.SharedSystemMemorySize));
    return STATUS_SUCCESS;
}

static PDXGKVMM_PROCESS_BUDGET
DxgkpVidMmAcquireProcessBudget(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ NTSTATUS *OutStatus)
{
    PDXGKVMM_PROCESS_BUDGET Record = NULL;
    PDXGKVMM_PROCESS_BUDGET Candidate = NULL;
    PDXGKVMM_PROCESS_BUDGET_GATE Gate;
    ULONGLONG Maximum[2];
    NTSTATUS Status;

    if (OutStatus == NULL)
        return NULL;
    *OutStatus = STATUS_INVALID_PARAMETER;
    if (Adapter == NULL || Process == NULL)
        return NULL;

    Gate = DxgkpVidMmEnterProcessBudgetGate(Process, &Status);
    if (Gate == NULL)
    {
        *OutStatus = Status;
        return NULL;
    }

    Status = DxgkpVidMmQueryGroupMaximum(
                 Adapter,
                 D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL,
                 &Maximum[0]);
    if (!NT_SUCCESS(Status))
        goto Exit;
    Status = DxgkpVidMmQueryGroupMaximum(
                 Adapter,
                 D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                 &Maximum[1]);
    if (!NT_SUCCESS(Status))
        goto Exit;

    Candidate = ExAllocatePoolWithTag(
                    NonPagedPool,
                    sizeof(*Candidate),
                    TAG_VIDMM_ALLOC);
    if (Candidate == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }
    RtlZeroMemory(Candidate, sizeof(*Candidate));
    Candidate->Process = Process;
    Candidate->Adapter = Adapter;
    ObReferenceObject(Process);
    /*
     * Segment geometry supplies the immutable hard maximum.  Until a memory
     * pressure policy publishes a smaller working-set budget, current budget
     * begins at that maximum; keeping the two fields distinct is what lets a
     * later policy lower Budget without weakening CantTrimFurther's hard cap.
     */
    Status = DxgkResidencyCoreBudgetInitialize(
                 &Candidate->Groups[0],
                 Maximum[0],
                 Maximum[0],
                 0);
    if (NT_SUCCESS(Status))
    {
        Status = DxgkResidencyCoreBudgetInitialize(
                     &Candidate->Groups[1],
                     Maximum[1],
                     Maximum[1],
                     0);
    }
    if (!NT_SUCCESS(Status))
    {
        goto Exit;
    }

    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    Record = DxgkpVidMmFindProcessBudgetLocked(Adapter, Process);
    if (!DxgkResidencyCoreProcessAdmissionCanPublish(&Gate->Admission) ||
        (Record != NULL && Record->ProcessExiting))
    {
        Record = NULL;
        Status = STATUS_PROCESS_IS_TERMINATING;
    }
    else
    {
        if (Record == NULL)
        {
            Record = Candidate;
            Candidate = NULL;
            InsertTailList(&DxgkVidMmProcessBudgetListHead, &Record->Link);
        }
        Record->ActiveTransactions++;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);

Exit:
    if (Candidate != NULL)
        DxgkpVidMmFreeProcessBudget(Candidate);
    DxgkpVidMmLeaveProcessBudgetGate(Gate);
    *OutStatus = Status;
    return NT_SUCCESS(Status) ? Record : NULL;
}

static VOID
DxgkpVidMmReleaseProcessBudget(
    _In_ PDXGKVMM_PROCESS_BUDGET Record)
{
    BOOLEAN Retire = FALSE;

    if (Record == NULL)
        return;
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    ASSERT(Record->ActiveTransactions != 0);
    if (Record->ActiveTransactions != 0)
        Record->ActiveTransactions--;
    if (DxgkpVidMmProcessBudgetCanRetireLocked(Record))
    {
        RemoveEntryList(&Record->Link);
        Retire = TRUE;
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    if (Retire)
        DxgkpVidMmFreeProcessBudget(Record);
}

static NTSTATUS
DxgkpVidMmChargeInitialPlacement(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Inout_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKVMM_PROCESS_BUDGET Record;
    PDXGKVMM_PROCESS_BUDGET_CHARGE Charge;
    D3DKMT_MEMORY_SEGMENT_GROUP Group;
    PDXGKRNL_SEGMENT Segment;
    ULONGLONG Bytes;
    ULONGLONG NumBytesToTrim;
    ULONG ReferenceCount;
    BOOLEAN RequiresBudgetCharge;
    NTSTATUS Status;

    if (DxgkpVidMmEffectiveWddmLevel(Adapter) <
        DXGK_CAPS_CORE_LEVEL_WDDM_2_0)
    {
        return STATUS_SUCCESS;
    }
    if (Allocation == NULL ||
        !Allocation->Resident ||
        Allocation->SegmentId == 0 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (VidMmSegmentBudgetGroup(Adapter, Segment) == VidMmBudgetGroupLocal)
        Group = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;
    else if (VidMmSegmentBudgetGroup(Adapter, Segment) ==
             VidMmBudgetGroupNonLocal)
        Group = D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL;
    else
        return STATUS_SUCCESS;
    Bytes = VidMmAllocationSizeForSegment(Allocation, Segment);
    ReferenceCount =
        DxgkpVidMmQueryProcessResidencyReferenceCountLocked(
            Allocation,
            Process);
    if (ReferenceCount == 0)
        return STATUS_INVALID_DEVICE_STATE;

    Charge = ExAllocatePoolWithTag(
                 NonPagedPool,
                 sizeof(*Charge),
                 TAG_VIDMM_ALLOC);
    if (Charge == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Charge, sizeof(*Charge));
    Charge->Group = Group;
    Charge->Bytes = Bytes;
    DxgkResidencyCoreProcessChargeInitialize(&Charge->State);

    Record = DxgkpVidMmAcquireProcessBudget(Adapter, Process, &Status);
    if (Record == NULL)
    {
        ExFreePoolWithTag(Charge, TAG_VIDMM_ALLOC);
        return Status;
    }
    Charge->Record = Record;
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    if (Record->ProcessExiting)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else if (DxgkpVidMmFindAllocationBudgetChargeLocked(
                 Allocation,
                 Record) != NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else
    {
        Status = DxgkResidencyCoreProcessChargePlanAcquire(
                     &Charge->State,
                     ReferenceCount,
                     &RequiresBudgetCharge);
        ASSERT(!NT_SUCCESS(Status) || RequiresBudgetCharge);
        if (NT_SUCCESS(Status))
        {
            Status = DxgkResidencyCoreBudgetTryCharge(
                         &Record->Groups[
                             DxgkpVidMmBudgetGroupIndex(Group)],
                         Bytes,
                         TRUE,
                         &NumBytesToTrim);
        }
        if (NT_SUCCESS(Status))
        {
            Status = DxgkResidencyCoreProcessChargeCommitAcquire(
                         &Charge->State,
                         ReferenceCount,
                         TRUE);
            ASSERT(NT_SUCCESS(Status));
        }
        if (NT_SUCCESS(Status))
        {
            InsertTailList(
                &Allocation->ResidencyBudgetChargeList,
                &Charge->Link);
            Record->ChargeEntryCount++;
            Charge = NULL;
        }
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    DxgkpVidMmReleaseProcessBudget(Record);
    if (Charge != NULL)
        ExFreePoolWithTag(Charge, TAG_VIDMM_ALLOC);
    return Status;
}

static VOID
DxgkpVidMmReleaseAllAllocationBudgetCharges(
    _Inout_ PDXGKVMM_ALLOCATION Allocation)
{
    LIST_ENTRY RemovedCharges;
    LIST_ENTRY RetiredRecords;
    PLIST_ENTRY Entry;

    if (Allocation->ResidencyBudgetChargeList.Flink == NULL ||
        IsListEmpty(&Allocation->ResidencyBudgetChargeList))
    {
        return;
    }
    DxgkpVidMmEnsureGlobalsInitialized();
    InitializeListHead(&RemovedCharges);
    InitializeListHead(&RetiredRecords);
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    while (!IsListEmpty(&Allocation->ResidencyBudgetChargeList))
    {
        PDXGKVMM_PROCESS_BUDGET_CHARGE Charge;
        PDXGKVMM_PROCESS_BUDGET Record;
        NTSTATUS Status;

        Entry = RemoveHeadList(&Allocation->ResidencyBudgetChargeList);
        Charge = CONTAINING_RECORD(
                     Entry,
                     DXGKVMM_PROCESS_BUDGET_CHARGE,
                     Link);
        Record = Charge->Record;
        ASSERT(Record != NULL);
        ASSERT(Charge->State.BudgetCharged);
        Status = DxgkResidencyCoreBudgetRelease(
                     &Record->Groups[
                         DxgkpVidMmBudgetGroupIndex(Charge->Group)],
                     Charge->Bytes);
        ASSERT(NT_SUCCESS(Status));
        ASSERT(Record->ChargeEntryCount != 0);
        if (Record->ChargeEntryCount != 0)
            Record->ChargeEntryCount--;
        InsertTailList(&RemovedCharges, &Charge->Link);
        if (DxgkpVidMmProcessBudgetCanRetireLocked(Record))
        {
            RemoveEntryList(&Record->Link);
            InsertTailList(&RetiredRecords, &Record->Link);
        }
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);

    while (!IsListEmpty(&RemovedCharges))
    {
        Entry = RemoveHeadList(&RemovedCharges);
        ExFreePoolWithTag(
            CONTAINING_RECORD(
                Entry,
                DXGKVMM_PROCESS_BUDGET_CHARGE,
                Link),
            TAG_VIDMM_ALLOC);
    }
    while (!IsListEmpty(&RetiredRecords))
    {
        Entry = RemoveHeadList(&RetiredRecords);
        DxgkpVidMmFreeProcessBudget(
            CONTAINING_RECORD(
                Entry,
                DXGKVMM_PROCESS_BUDGET,
                Link));
    }
}

static NTSTATUS
DxgkpVidMmReleaseProcessBudgetReferences(
    _Inout_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PEPROCESS Process,
    _In_ ULONG ReferenceCount)
{
    PDXGKVMM_PROCESS_BUDGET_CHARGE Charge;
    PDXGKVMM_PROCESS_BUDGET Record;
    PDXGKVMM_PROCESS_BUDGET RetiredRecord = NULL;
    BOOLEAN ReleaseBudgetCharge;
    NTSTATUS Status;

    if (Allocation == NULL || Process == NULL || ReferenceCount == 0)
        return STATUS_INVALID_PARAMETER;
    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    Record = DxgkpVidMmFindProcessBudgetLocked(
                 Allocation->Adapter,
                 Process);
    Charge = Record != NULL
                 ? DxgkpVidMmFindAllocationBudgetChargeLocked(
                       Allocation,
                       Record)
                 : NULL;
    if (Charge == NULL)
    {
        ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
        return STATUS_NOT_FOUND;
    }

    Status = DxgkResidencyCoreProcessChargeRelease(
                 &Charge->State,
                 ReferenceCount,
                 &ReleaseBudgetCharge);
    if (NT_SUCCESS(Status) && ReleaseBudgetCharge)
    {
        Status = DxgkResidencyCoreBudgetRelease(
                     &Record->Groups[
                         DxgkpVidMmBudgetGroupIndex(Charge->Group)],
                     Charge->Bytes);
        ASSERT(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            ASSERT(Record->ChargeEntryCount != 0);
            if (Record->ChargeEntryCount != 0)
                Record->ChargeEntryCount--;
            RemoveEntryList(&Charge->Link);
            if (DxgkpVidMmProcessBudgetCanRetireLocked(Record))
            {
                RemoveEntryList(&Record->Link);
                RetiredRecord = Record;
            }
        }
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);

    if (NT_SUCCESS(Status) && ReleaseBudgetCharge)
        ExFreePoolWithTag(Charge, TAG_VIDMM_ALLOC);
    if (RetiredRecord != NULL)
        DxgkpVidMmFreeProcessBudget(RetiredRecord);
    return Status;
}

static VOID
DxgkpVidMmReleaseAdapterBudgets(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY Removed;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    DxgkpVidMmEnsureGlobalsInitialized();
    InitializeListHead(&Removed);
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    for (Entry = DxgkVidMmProcessBudgetListHead.Flink;
         Entry != &DxgkVidMmProcessBudgetListHead;
         Entry = Next)
    {
        PDXGKVMM_PROCESS_BUDGET Record =
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET, Link);

        Next = Entry->Flink;
        if (Record->Adapter != Adapter)
            continue;
        ASSERT(Record->ActiveTransactions == 0);
        ASSERT(Record->ChargeEntryCount == 0);
        ASSERT(Record->Groups[0].CurrentUsage == 0);
        ASSERT(Record->Groups[1].CurrentUsage == 0);
        RemoveEntryList(&Record->Link);
        InsertTailList(&Removed, &Record->Link);
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);

    while (!IsListEmpty(&Removed))
    {
        Entry = RemoveHeadList(&Removed);
        DxgkpVidMmFreeProcessBudget(
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET, Link));
    }
}
#endif

NTSTATUS
DxgkVidMmSetProcessReservation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP Group,
    _In_ UINT64 Reservation)
{
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    PDXGKVMM_PROCESS_BUDGET Record;
    PLIST_ENTRY Entry;
    ULONGLONG OtherReservations = 0;
    ULONGLONG Available;
    ULONG GroupIndex;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || Process == NULL ||
        (Group != D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL &&
         Group != D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Record = DxgkpVidMmAcquireProcessBudget(Adapter, Process, &Status);
    if (Record == NULL)
        return Status;
    GroupIndex = DxgkpVidMmBudgetGroupIndex(Group);
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    for (Entry = DxgkVidMmProcessBudgetListHead.Flink;
         Entry != &DxgkVidMmProcessBudgetListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_PROCESS_BUDGET Other =
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET, Link);

        if (Other != Record && Other->Adapter == Adapter)
        {
            DxgkpVidMmSaturatingAdd(
                &OtherReservations,
                Other->Groups[GroupIndex].Reservation);
        }
    }
    Available = (OtherReservations >= Record->Groups[GroupIndex].Maximum)
                    ? 0
                    : Record->Groups[GroupIndex].Maximum - OtherReservations;
    if (Record->ProcessExiting)
        Status = STATUS_DELETE_PENDING;
    else if (Reservation > Available)
        Status = STATUS_INVALID_PARAMETER;
    else
        Status = DxgkResidencyCoreBudgetSetReservation(
                     &Record->Groups[GroupIndex],
                     Reservation);
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    DxgkpVidMmReleaseProcessBudget(Record);
    return Status;
#else
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Group);
    UNREFERENCED_PARAMETER(Reservation);
    return STATUS_NOT_SUPPORTED;
#endif
}

VOID
DxgkVidMmReleaseProcessReservations(
    _In_ PEPROCESS Process)
{
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    PDXGKVMM_PROCESS_BUDGET_GATE Gate;
    LIST_ENTRY Removed;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    if (Process == NULL ||
        InterlockedCompareExchange(&DxgkVidMmGlobalsState, 0, 0) == 0)
    {
        return;
    }
    DxgkpVidMmEnsureGlobalsInitialized();
    InitializeListHead(&Removed);
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    Gate = DxgkpVidMmFindProcessBudgetGateLocked(Process);
    if (Gate != NULL)
    {
        /*
         * This tombstone is independent of ledger publication.  An entrant
         * that passed PsGetProcessExitStatus before this callback cannot
         * publish a new ledger after cleanup reaches this point.
         */
        DxgkResidencyCoreProcessAdmissionMarkExiting(&Gate->Admission);
    }
    for (Entry = DxgkVidMmProcessBudgetListHead.Flink;
         Entry != &DxgkVidMmProcessBudgetListHead;
         Entry = Next)
    {
        PDXGKVMM_PROCESS_BUDGET Record =
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET, Link);

        Next = Entry->Flink;
        if (Record->Process != Process)
            continue;
        Record->ProcessExiting = TRUE;
        (VOID)DxgkResidencyCoreBudgetSetReservation(&Record->Groups[0], 0);
        (VOID)DxgkResidencyCoreBudgetSetReservation(&Record->Groups[1], 0);
        if (DxgkpVidMmProcessBudgetCanRetireLocked(Record))
        {
            RemoveEntryList(&Record->Link);
            InsertTailList(&Removed, &Record->Link);
        }
    }
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    while (!IsListEmpty(&Removed))
    {
        Entry = RemoveHeadList(&Removed);
        DxgkpVidMmFreeProcessBudget(
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET, Link));
    }
#else
    UNREFERENCED_PARAMETER(Process);
#endif
}

NTSTATUS
DxgkVidMmQueryProcessBudget(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP MemorySegmentGroup,
    _Out_ UINT64 *Budget,
    _Out_ UINT64 *CurrentUsage,
    _Out_ UINT64 *CurrentReservation,
    _Out_ UINT64 *AvailableForReservation)
{
#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    PDXGKVMM_PROCESS_BUDGET Record;
    PLIST_ENTRY Entry;
    ULONGLONG OtherReservations = 0;
    ULONG GroupIndex;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || Process == NULL || Budget == NULL ||
        CurrentUsage == NULL || CurrentReservation == NULL ||
        AvailableForReservation == NULL ||
        (MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL &&
         MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *Budget = 0;
    *CurrentUsage = 0;
    *CurrentReservation = 0;
    *AvailableForReservation = 0;

    Record = DxgkpVidMmAcquireProcessBudget(Adapter, Process, &Status);
    if (Record == NULL)
        return Status;
    GroupIndex = DxgkpVidMmBudgetGroupIndex(MemorySegmentGroup);
    ExAcquireFastMutex(&DxgkVidMmProcessBudgetLock);
    *Budget = Record->Groups[GroupIndex].Budget;
    *CurrentUsage = Record->Groups[GroupIndex].CurrentUsage;
    *CurrentReservation = Record->Groups[GroupIndex].Reservation;
    for (Entry = DxgkVidMmProcessBudgetListHead.Flink;
         Entry != &DxgkVidMmProcessBudgetListHead;
         Entry = Entry->Flink)
    {
        PDXGKVMM_PROCESS_BUDGET Other =
            CONTAINING_RECORD(Entry, DXGKVMM_PROCESS_BUDGET, Link);

        if (Other != Record && Other->Adapter == Adapter)
        {
            DxgkpVidMmSaturatingAdd(
                &OtherReservations,
                Other->Groups[GroupIndex].Reservation);
        }
    }
    *AvailableForReservation =
        (OtherReservations >= Record->Groups[GroupIndex].Maximum)
            ? 0
            : Record->Groups[GroupIndex].Maximum - OtherReservations;
    ExReleaseFastMutex(&DxgkVidMmProcessBudgetLock);
    DxgkpVidMmReleaseProcessBudget(Record);
    return STATUS_SUCCESS;
#else
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(MemorySegmentGroup);
    UNREFERENCED_PARAMETER(Budget);
    UNREFERENCED_PARAMETER(CurrentUsage);
    UNREFERENCED_PARAMETER(CurrentReservation);
    UNREFERENCED_PARAMETER(AvailableForReservation);
    return STATUS_NOT_SUPPORTED;
#endif
}

/*
 * DxgkVidMmSubmitAperturePagingPacket
 *
 * Builds a MAP/UNMAP_APERTURE_SEGMENT paging buffer through the miniport and
 * submits it as a tracked packet on node 0.  The packet signals the caller's
 * monitored fence (the device paging queue) with SignalFenceValue when the
 * scheduler retires it, which is the WDDM 2.0 paging-completion contract.
 */
NTSTATUS
DxgkVidMmSubmitAperturePagingPacket(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN Map,
    _In_ D3DKMT_HANDLE hSignalSyncObject,
    _In_ ULONG64 SignalFenceValue)
{
    PDXGKRNL_DMA_BUFFER DmaBuffer = NULL;
    DXGKARG_BUILDPAGINGBUFFER BuildArgs;
    DXGKRNL_TRACK_DMA_ARGS TrackArgs;
    SIZE_T NumberOfPages;
    ULONG DmaBytesUsed = 0;
    ULONG VidSchFence = 0;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || Device == NULL || Allocation == NULL || hSignalSyncObject == 0)
        return STATUS_INVALID_PARAMETER;
    if (DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer) == NULL)
        return STATUS_NOT_SUPPORTED;

    Status = DxgkAllocateDmaBuffer(Adapter, PAGE_SIZE, &DmaBuffer);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!DxgkpVidMmRoundUpPageSize(Allocation->Size, &NumberOfPages))
    {
        DxgkFreeDmaBuffer(DmaBuffer);
        return STATUS_INTEGER_OVERFLOW;
    }
    NumberOfPages /= PAGE_SIZE;
    RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
    BuildArgs.pDmaBuffer = DmaBuffer->VirtualAddress;
    BuildArgs.DmaSize = (UINT)DmaBuffer->Capacity;
    if (Map)
    {
        BuildArgs.Operation = DXGK_OPERATION_MAP_APERTURE_SEGMENT;
        BuildArgs.MapApertureSegment.hDevice = Device->hMiniportDevice;
        BuildArgs.MapApertureSegment.hAllocation = Allocation->MiniportHandle;
        BuildArgs.MapApertureSegment.SegmentId = Allocation->SegmentId;
        BuildArgs.MapApertureSegment.OffsetInPages = (SIZE_T)(Allocation->SegmentOffset / PAGE_SIZE);
        BuildArgs.MapApertureSegment.NumberOfPages = NumberOfPages;
        BuildArgs.MapApertureSegment.pMdl = Allocation->ApertureMdl;
    }
    else
    {
        BuildArgs.Operation = DXGK_OPERATION_UNMAP_APERTURE_SEGMENT;
        BuildArgs.UnmapApertureSegment.hDevice = Device->hMiniportDevice;
        BuildArgs.UnmapApertureSegment.hAllocation = Allocation->MiniportHandle;
        BuildArgs.UnmapApertureSegment.SegmentId = Allocation->SegmentId;
        BuildArgs.UnmapApertureSegment.OffsetInPages = (SIZE_T)(Allocation->SegmentOffset / PAGE_SIZE);
        BuildArgs.UnmapApertureSegment.NumberOfPages = NumberOfPages;
    }

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer)(Adapter->MiniportDeviceContext, &BuildArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (BuildArgs.pDmaBuffer == NULL || (PUCHAR)BuildArgs.pDmaBuffer < (PUCHAR)DmaBuffer->VirtualAddress || (PUCHAR)BuildArgs.pDmaBuffer > (PUCHAR)DmaBuffer->VirtualAddress + DmaBuffer->Capacity)
    {
        Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        goto Cleanup;
    }
    DmaBytesUsed = (ULONG)((PUCHAR)BuildArgs.pDmaBuffer - (PUCHAR)DmaBuffer->VirtualAddress);
    if (DmaBytesUsed == 0)
    {
        /* No paging commands means there is nothing for hardware to retire. */
        Status = DxgkSyncObjectGpuRetireSignal(hSignalSyncObject, SignalFenceValue);
        goto Cleanup;
    }
    if (DmaBytesUsed > DmaBuffer->Capacity)
    {
        Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        goto Cleanup;
    }
    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = DmaBytesUsed;

    RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
    TrackArgs.hSignalSyncObject = hSignalSyncObject;
    TrackArgs.SignalFenceValue = SignalFenceValue;
    TrackArgs.Device = Device;

    Status = VidSchSubmitCommandTracked(Adapter, 0, 0, DmaBuffer, NULL, 0, NULL, 0, NULL, 0, Device->hMiniportDevice, NULL, 0, &TrackArgs, VIDSCH_SUBMITFLAG_PAGING, 0, &VidSchFence);
    if (NT_SUCCESS(Status))
        DmaBuffer = NULL;

Cleanup:
    if (DmaBuffer != NULL)
        DxgkFreeDmaBuffer(DmaBuffer);
    return Status;
}

NTSTATUS
DxgkVidMmQuerySegmentSizes(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ D3DKMT_SEGMENTGROUPSIZEINFO *Info)
{
    PDXGKRNL_SEGMENT Segments;
    ULONG SegmentCount;
    ULONG Index;
    UINT32 PhysicalAdapterIndex;
    ULONGLONG GlobalApertureCommitLimit = MAXULONGLONG;
    ULONGLONG ApertureCommitLimitSum = 0;
    ULONGLONG SystemMemoryAvailableForGraphics;
    ULONGLONG MaximumSharedSystemMemory;
    NTSTATUS Status;
    UCHAR CapsBuffer[DXGKP_DRIVERCAPS_QUERY_SIZE];

    PAGED_CODE();
    if (Adapter == NULL || Info == NULL)
        return STATUS_INVALID_PARAMETER;
    PhysicalAdapterIndex = Info->PhysicalAdapterIndex;
    if (PhysicalAdapterIndex != 0)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Info, sizeof(*Info));
    Info->PhysicalAdapterIndex = PhysicalAdapterIndex;
    Status = DxgkpVidMmQuerySystemMemoryAvailableForGraphics(&SystemMemoryAvailableForGraphics);
    if (!NT_SUCCESS(Status))
        return Status;
    if (NT_SUCCESS(DxgkpQueryDriverCaps(Adapter, (PDXGK_DRIVERCAPS)CapsBuffer)) && ((PDXGK_DRIVERCAPS)CapsBuffer)->ApertureSegmentCommitLimit != 0)
        GlobalApertureCommitLimit = ((PDXGK_DRIVERCAPS)CapsBuffer)->ApertureSegmentCommitLimit;
    Segments = ADAPTER_SEGMENTS(Adapter);
    SegmentCount = Adapter->SegmentCount;
    if (SegmentCount != 0 && Segments == NULL)
        return STATUS_DEVICE_NOT_READY;
    for (Index = 0; Index < SegmentCount; ++Index)
    {
        ULONGLONG SegmentSize;
        ULONGLONG SegmentCommitLimit;

        ExAcquireFastMutex(&Segments[Index].Lock);
        SegmentSize = Segments[Index].Size;
        SegmentCommitLimit = VidMmSegmentPlacementLimit(&Segments[Index]);
        switch (VidMmSegmentBudgetGroup(Adapter, &Segments[Index]))
        {
            case VidMmBudgetGroupLocal:
                DxgkpVidMmSaturatingAdd(&Info->LocalMemory, SegmentSize);
                break;
            case VidMmBudgetGroupNonLocal:
                DxgkpVidMmSaturatingAdd(&Info->NonLocalMemory, SegmentSize);
                break;
            default:
                DxgkpVidMmSaturatingAdd(&Info->NonBudgetMemory, SegmentSize);
                break;
        }
        if (VidMmSegmentIsAperture(&Segments[Index]))
            DxgkpVidMmSaturatingAdd(&ApertureCommitLimitSum, SegmentCommitLimit);
        else if (Segments[Index].Flags.PopulatedFromSystemMemory)
            DxgkpVidMmSaturatingAdd(&Info->LegacyInfo.DedicatedSystemMemorySize, SegmentSize);
        else
            DxgkpVidMmSaturatingAdd(&Info->LegacyInfo.DedicatedVideoMemorySize, SegmentSize);
        ExReleaseFastMutex(&Segments[Index].Lock);
    }
    Info->LegacyInfo.DedicatedSystemMemorySize = min(Info->LegacyInfo.DedicatedSystemMemorySize, SystemMemoryAvailableForGraphics);
    MaximumSharedSystemMemory = SystemMemoryAvailableForGraphics - Info->LegacyInfo.DedicatedSystemMemorySize;
    Info->LegacyInfo.SharedSystemMemorySize = min(min(ApertureCommitLimitSum, GlobalApertureCommitLimit), MaximumSharedSystemMemory);
    return STATUS_SUCCESS;
}

/*
 * DxgkVidMmPublishSegments
 *
 * Hands every segment's geometry to dxgmms2, which owns the space.  Called
 * once the provider has published the video-memory contract, which is after
 * segment discovery: the owner needs the sizes discovery produced.
 */
NTSTATUS
DxgkVidMmPublishSegments(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGMMS2_VIDMM_INTERFACE_V1 VidMm;
    PDXGKRNL_SEGMENT Segments;
    ULONG i;

    PAGED_CODE();
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    VidMm = DxgkpVidMmOwner(Adapter);
    if (VidMm == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (Adapter->Segments == NULL || Adapter->SegmentCount == 0)
        return STATUS_SUCCESS;

    Segments = ADAPTER_SEGMENTS(Adapter);
    for (i = 0; i < Adapter->SegmentCount; i++)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[i];
        DXGMMS2_VIDMM_SEGMENT_DESC_V1 Desc;
        NTSTATUS Status;

        RtlZeroMemory(&Desc, sizeof(Desc));
        Desc.Size = Seg->Size;
        Desc.CommitLimit = VidMmSegmentPlacementLimit(Seg);
        if (VidMmSegmentIsAperture(Seg))
            Desc.Flags |= DXGMMS2_VIDMM_SEGMENT_APERTURE;
        if (Seg->Flags.CpuVisible)
            Desc.Flags |= DXGMMS2_VIDMM_SEGMENT_CPU_VISIBLE;
        Status = VidMm->SetSegment(VidMm->VidMmHandle, i, &Desc);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkVidMmPublishSegments: segment %lu rejected 0x%08lX\n", i, Status);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

/* EOF */
