/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Video Memory Manager (VidMm) private types and prototypes
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * This header is included only by vidmm.c and adapter.c (which calls
 * DxgkVidMmInitializeAdapter / DxgkVidMmTeardownAdapter at adapter lifecycle
 * events).  All other callers use the prototypes declared in dxgkrnl_private.h.
 */

#pragma once

/* =========================================================================
 * Pool tags (local to vidmm.c)
 * ========================================================================= */

/*
 * TAG_VIDMM_SEGMENT — segment descriptor array and DXGKRNL_SEGMENT array.
 * Displayed as 'DxVS' in pool listings.
 */
#define TAG_VIDMM_SEGMENT   'SVxD'

/* Bound on how long submission admission waits for an allocation's paging
 * packet to retire before reporting the allocation busy. */
#define DXGKP_VIDMM_PAGING_ADMISSION_TIMEOUT_MS  2000

/*
 * TAG_VIDMM_ALLOC — per-allocation DXGKVMM_ALLOCATION objects.
 * Displayed as 'DxVA' in pool listings.
 */
#define TAG_VIDMM_ALLOC     'AVxD'

/*
 * TAG_VIDMM_RESOURCE — per-resource DXGKVMM_RESOURCE objects.
 * Displayed as 'DxVR' in pool listings.
 */
#define TAG_VIDMM_RESOURCE  'RVxD'

/* =========================================================================
 * DXGKRNL_SEGMENT
 *
 * Runtime representation of a single GPU memory segment.  One instance per
 * segment ID, stored in Adapter->Segments (typed PVOID in the adapter struct
 * to avoid a circular header dependency).
 * ========================================================================= */
typedef struct _DXGKRNL_SEGMENT
{
    /* 1-based segment ID, matching the WDDM convention. */
    ULONG               SegmentId;

    /* Total size of the segment in bytes. */
    ULONGLONG           Size;

    /* Maximum bytes the miniport permits dxgkrnl to commit to the segment. */
    ULONGLONG           CommitLimit;

    /*
     * Committed bytes and the virgin-space cursor are dxgmms2's; read them
     * through DXGMMS2_VIDMM_INTERFACE_V1::QuerySegmentStatus rather than
     * keeping a second copy that could disagree with the owner.
     */

    /* GPU logical base address of the segment. */
    PHYSICAL_ADDRESS    BaseAddress;

    /* CPU physical base for a non-aperture CPU-visible memory segment. */
    PHYSICAL_ADDRESS    CpuTranslatedAddress;

    /*
     * Kernel-mode virtual address base established by MmMapIoSpace.
     * NULL until the first CPU-visible allocation is mapped; released
     * at adapter teardown.  Meaningful only for non-aperture CPU-visible
     * segments.
     */
    PVOID               CpuBase;

    /* Capability flags (DXGK_SEGMENTFLAG_* bitmask from d3dkmddi.h). */
    DXGK_SEGMENTFLAGS   Flags;

    /*
     * Index from a placement back to its DXGKVMM_ALLOCATION, kept in
     * ascending SegmentOffset order.  This is a lookup aid, not the
     * allocator: placement decisions belong to dxgmms2.
     * Protected by Lock.
     */
    LIST_ENTRY          AllocationList;

    /*
     * FAST_MUTEX serialising all segment-level operations.
     * Allows PASSIVE_LEVEL mutual exclusion without raising IRQL, which
     * keeps multi-segment allocation sweeps cheap.
     */
    FAST_MUTEX          Lock;

    /*
     * Adapter-global paging-buffer parameters, cached here for
     * convenience (only meaningful on segment[0]).
     */
    ULONG               PagingBufferSegmentId;
    ULONG               PagingBufferSize;

    /* Page to which UNMAP_APERTURE_SEGMENT redirects a retired aperture. */
    PVOID               DummyPageVa;
    PHYSICAL_ADDRESS    DummyPage;

} DXGKRNL_SEGMENT, *PDXGKRNL_SEGMENT;

typedef struct _DXGKVMM_RESOURCE
{
    /* Handle-table ownership plus transient users. */
    volatile LONG      ReferenceCount;
    volatile LONG      Destroying;
    volatile LONG      FinalizeQueued;
    KEVENT             ReferencesDrainedEvent;
    WORK_QUEUE_ITEM    FinalizeWorkItem;

    /* Owning logical device. */
    PDXGKRNL_DEVICE    Device;

    /* Owning adapter, retained even for internal resources without a device. */
    PDXGKRNL_ADAPTER   Adapter;

    /* Source resource retained by a per-device OpenResource alias. */
    struct _DXGKVMM_RESOURCE *BackingResource;

    /* Opaque miniport-side resource handle. */
    HANDLE             MiniportHandle;
    KMUTEX             MiniportResourceLock;
    KMUTEX             ResourceOperationLock;
    volatile LONG      CloseUncertain;
    volatile LONG      DestroyFailureUncertain;

    /* 32-bit user-visible D3DKMT resource handle. */
    D3DKMT_HANDLE      Handle;

    /* Global-share handle used by OpenResource / GetSharedPrimaryHandle. */
    D3DKMT_HANDLE      GlobalShareHandle;

    /* TRUE only when the creator explicitly exported this resource. */
    BOOLEAN            Shareable;

    /* Runtime-private data copied from D3DKMT_CREATEALLOCATION. */
    PVOID              PrivateRuntimeData;
    UINT               PrivateRuntimeDataSize;

    /* Resource-private driver data used by QueryResourceInfo/OpenResource. */
    PVOID              ResourcePrivateDriverData;
    UINT               ResourcePrivateDriverDataSize;

    /* Ordered live allocation membership for Query/OpenResource. */
    ULONG              AllocationCount;
    LIST_ENTRY         AllocationList;

    /* Per-open arrays used to close all surviving bindings in one DDI call. */
    struct _DXGKVMM_ALLOCATION **OpenAllocations;
    PHANDLE             OpenBindingScratch;
    UINT                OpenAllocationCapacity;

    /* Linkage in the VidMm global resource list. */
    LIST_ENTRY         GlobalResourceEntry;
} DXGKVMM_RESOURCE, *PDXGKVMM_RESOURCE;

/* =========================================================================
 * DXGKVMM_ALLOCATION
 *
 * Kernel-mode tracking object for a single GPU allocation.  One instance
 * per DxgkVidMmCreateAllocation call.  Identified by its Handle field
 * (pointer-as-ULONG_PTR).
 * ========================================================================= */

/*
 * Magic value stored in DXGKVMM_ALLOCATION.Magic in debug builds.
 * Checked on every access to detect use-after-free.
 */
#define DXGKVMM_ALLOCATION_MAGIC  0xD3DAAC10UL

typedef struct _DXGKVMM_ALLOCATION
{
    /* Magic word for use-after-free detection (debug builds). */
    ULONG               Magic;

    /* Handle-table ownership plus transient users. */
    volatile LONG       ReferenceCount;
    volatile LONG       Destroying;
    volatile LONG       FinalizeQueued;
    volatile LONG       HandleReferenceDropped;
    KEVENT              ReferencesDrainedEvent;
    WORK_QUEUE_ITEM     FinalizeWorkItem;
    KMUTEX              ResidencyLock;
    /*
     * A residency transaction may release ResidencyLock while it builds or
     * waits for paging work.  The stable owner token keeps every competing
     * reference/placement mutation out until that transaction either commits
     * or rolls back.  ResidencyTransactionEvent is signaled exactly when the
     * owner is NULL.
     */
    PVOID volatile      ResidencyTransactionOwner;
    KEVENT              ResidencyTransactionEvent;

    /* Back-pointer to the owning adapter. */
    PDXGKRNL_ADAPTER    Adapter;

    /* Owning logical device for user-mode allocations, NULL for internal use. */
    PDXGKRNL_DEVICE     Device;

    /* Stable miniport-device handle retained after logical device teardown. */
    HANDLE              MiniportDeviceHandle;

    /* Creation-time resource ownership retained until wrapper attachment. */
    HANDLE              MiniportResourceHandle;
    BOOLEAN             DestroyMiniportResource;

    /* Source allocation retained by a per-device OpenResource alias. */
    struct _DXGKVMM_ALLOCATION *BackingAllocation;

    /* Miniport binding created by DxgkDdiOpenAllocation for an open alias. */
    HANDLE               OpenBindingHandle;
    UINT                 OpenBindingIndex;
    UINT                 OpenBindingGroupIndex;
    struct _DXGKVMM_OPEN_BINDING_GROUP *OpenBindingGroup;
    struct _DXGKVMM_DESTROY_BATCH *DestroyBatch;
    volatile LONG        LogicalReferenceCount;
    volatile LONG        LogicalHandleReferenceDropped;
    KEVENT               LogicalReferencesDrainedEvent;
    BOOLEAN              Initializing;

    /* Optional parent resource wrapper. */
    PDXGKVMM_RESOURCE   Resource;

    /* Allocation size in bytes (may be rounded up by the miniport). */
    SIZE_T              Size;

    /* Segment resource size when DXGK_SEGMENTFLAGS.PitchAlignment is set. */
    SIZE_T              PitchAlignedSize;

    /*
     * Required base alignment in bytes (power of two).
     * Default is PAGE_SIZE when the miniport reports 0.
     */
    SIZE_T              Alignment;

    /* Miniport-declared placement sets; bit 0 names segment 1. */
    UINT                SupportedWriteSegmentSet;
    UINT                EvictionSegmentSet;

    /* WDDM allocation priority (D3DDDI_ALLOCATIONPRIORITY_NORMAL default). */
    ULONG               AllocationPriority;

    /* TRUE if the allocation can be mapped into CPU virtual address space. */
    BOOLEAN             CpuVisible;

    /* TRUE when the allocation is resident in a GPU segment. */
    BOOLEAN             Resident;

    /*
     * WDDM 2.0 residency references (D3DKMTMakeResident/D3DKMTEvict).
     * Each MakeResident list entry adds one; each Evict list entry removes
     * one.  A referenced allocation is pinned against pressure trimming; at
     * zero references it is a trim candidate, and an Evict that drops the
     * count to zero releases the placement unless EvictOnlyIfNecessary.
     */
    volatile LONG       ResidencyReferenceCount;

    /*
     * Per-device breakdown of the count above.  MakeResident charges the
     * calling device and Evict may only release what that same device holds,
     * so one device cannot evict another device's residency, and device
     * teardown releases exactly its own share.  Protected by ResidencyLock.
     */
    LIST_ENTRY          ResidencyReferenceList;

    /* The created-resident reference, owned by Device.  Held separately from
     * the list because the owner is assigned after lifetime initialization. */
    BOOLEAN             ImplicitResidencyReference;

    /* Separate from user MakeResident/Evict references: each admitted GPU
     * submission pins this exact placement until tracked terminal cleanup. */
    volatile LONG       SubmissionResidencyPinCount;

    /*
     * Physical base address of the allocation.
     * Segment->BaseAddress.QuadPart + SegmentOffset when resident;
     * MmGetPhysicalAddress(SystemMemory) when in system memory.
     */
    PHYSICAL_ADDRESS    PhysicalAddress;

    /*
     * Placement within the segment when Resident == TRUE.
     * SegmentId  : 1-based segment index (matches DXGKRNL_SEGMENT.SegmentId).
     * SegmentOffset : byte offset from Segment->BaseAddress.
     */
    ULONG               SegmentId;
    ULONGLONG           SegmentOffset;

    /*
     * Newest admitted paging transition.  This covers both a placement being
     * established and an eviction whose aperture unmap is still retiring.
     * The committed residency fields above stay authoritative; scheduler
     * admission consumes the same watermark before patching (paging.c).
     */
    BOOLEAN             PendingPlacement;
    ULONG               PendingSegmentId;
    ULONGLONG           PendingSegmentOffset;
    ULONG               PagingFenceId;

    /*
     * System-memory backing store.
     * - Populated on first eviction or system-memory fallback placement.
     * - NULL when the allocation is only in a VRAM segment with no eviction.
     * - Freed at DxgkVidMmDestroyAllocation time.
     */
    PVOID               SystemMemory;

    /*
     * Existing-heap backing (D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP):
     * caller-owned user pages locked for the allocation's lifetime.  When
     * non-NULL, SystemMemory is the MDL system mapping and is released by
     * unlock/free of the MDL — never by pool free.
     */
    PMDL                SysMemMdl;


    /*
     * Kernel-mode CPU virtual address for CPU-visible allocations.
     * Points into the segment's MmMapIoSpace mapping (VRAM) or into
     * SystemMemory (system memory / aperture).  NULL when not mapped.
     */
    PVOID               CpuAddress;

    /*
     * User-mode mapping state for D3DKMTLock/D3DKMTUnlock.
     * UserModeMapBase is the exact base returned by MmMapLockedPagesSpecifyCache.
     * UserModeAddress is the caller-visible VA after applying any allocation
     * offset within the first mapped page.
     */
    PVOID               UserModeMapBase;
    PVOID               UserModeAddress;
    PMDL                UserModeMdl;
    PEPROCESS           UserModeProcess;
    ULONG               UserModeLockCount;
    KMUTEX              UserModeLock;

    /* Miniport-side allocation handle (from DxgkDdiCreateAllocation). */
    HANDLE              MiniportHandle;

    /* Allocation-private driver data used by QueryResourceInfo/OpenResource. */
    PVOID               PrivateDriverData;
    UINT                PrivateDriverDataSize;

    /* MDL backing an aperture attachment, when active. */
    PMDL                ApertureMdl;

    /* TRUE once BuildPagingBuffer(MAP_APERTURE_SEGMENT) succeeded. */
    BOOLEAN             ApertureMapped;

    /* TRUE when reset purged a memory-segment placement without a transfer. */
    BOOLEAN             ContentLost;

    /*
     * D3DKMTOfferAllocations state.  An offered allocation is a preferred
     * eviction victim and its content may be discarded; Reclaim reports and
     * clears the discard.  Protected by ResidencyLock.
     */
    BOOLEAN             Offered;
    BOOLEAN             OfferDiscarded;
    ULONG               OfferPriority;

    /*
     * 32-bit user-visible D3DKMT allocation handle.
     */
    D3DKMT_HANDLE       Handle;

    /*
     * Linkage in DXGKRNL_SEGMENT.AllocationList (sorted by SegmentOffset).
     * Head == ListHead when not resident in any segment.
     */
    LIST_ENTRY          SegmentEntry;

    /*
     * Linkage in DXGKRNL_DEVICE.AllocationListHead (or similar).
     * Not currently used; reserved for future per-device tracking.
     */
    LIST_ENTRY          DeviceEntry;

    /*
     * Linkage in the VidMm global allocation list.
     */
    LIST_ENTRY          GlobalAllocationEntry;

    /* Ordered membership in DXGKVMM_RESOURCE.AllocationList. */
    LIST_ENTRY          ResourceEntry;

} DXGKVMM_ALLOCATION, *PDXGKVMM_ALLOCATION;

/* =========================================================================
 * Internal VidMm function prototypes
 * ========================================================================= */

/*
 * DxgkVidMmInitializeAdapter
 * DxgkVidMmTeardownAdapter
 *
 * Adapter-level init/teardown.  Called from DxgkAdapterStart / DxgkAdapterStop
 * in adapter.c.
 */
NTSTATUS
DxgkVidMmInitializeAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkVidMmQuiesceAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkVidMmResumeAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkVidMmKickDeferredDestroyBatches(
    _In_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
DxgkVidMmPrepareForIdle(
    _In_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
DxgkVidMmRecoverFromTimeout(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkVidMmTeardownAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

/*
 * DxgkVidMmTryPlaceInSegment
 *
 * First-fit allocator for a single segment.  Called internally.
 */
NTSTATUS
DxgkVidMmTryPlaceInSegment(
    _In_ PDXGKRNL_SEGMENT       Segment,
    _In_ PDXGKVMM_ALLOCATION    Allocation);

/*
 * DxgkVidMmEvict
 * DxgkVidMmMakeResident
 *
 * Residency management.
 */
NTSTATUS
DxgkVidMmEvict(
    _In_ PDXGKVMM_ALLOCATION Allocation);

NTSTATUS
DxgkVidMmMakeResident(
    _In_ PDXGKVMM_ALLOCATION    Allocation,
    _In_ PDXGKRNL_ADAPTER       Adapter);

NTSTATUS
DxgkVidMmMakeResidentBatch(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ ULONG AllocationCount,
    _In_ D3DKMT_HANDLE hPagingSyncObject,
    _Inout_opt_ volatile LONG64 *PagingFenceCounter,
    _Out_ PULONGLONG OutNumBytesToTrim,
    _Out_ PULONGLONG OutPagingFenceValue,
    _Out_ PBOOLEAN OutPagingQueued);

NTSTATUS
DxgkVidMmEvictBatch(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ ULONG AllocationCount,
    _In_ BOOLEAN EvictOnlyIfNecessary,
    _Out_ PULONGLONG OutNumBytesToTrim);

typedef struct _DXGKVMM_RESIDENCY_REF
{
    LIST_ENTRY          Entry;
    PDXGKRNL_DEVICE     Device;
    LONG                Count;
} DXGKVMM_RESIDENCY_REF, *PDXGKVMM_RESIDENCY_REF;

NTSTATUS
DxgkVidMmAcquireDeviceResidencyReference(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device);

BOOLEAN
DxgkVidMmReleaseDeviceResidencyReference(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _Out_ PBOOLEAN OutReachedZero);

BOOLEAN
DxgkVidMmDeviceHoldsResidencyReference(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device);

LONG
DxgkVidMmQueryDeviceResidencyReferenceCount(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device);

VOID
DxgkVidMmReleaseAllDeviceResidencyReferences(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_opt_ PDXGKRNL_DEVICE Device);

NTSTATUS
DxgkVidMmOfferAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE  Device,
    _In_ D3DKMT_HANDLE    Handle,
    _In_ ULONG            Priority);

NTSTATUS
DxgkVidMmReclaimAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE  Device,
    _In_ D3DKMT_HANDLE    Handle,
    _Out_ PBOOLEAN        Discarded);

/*
 * DxgkVidMmMapAllocationCpu
 * DxgkVidMmUnmapAllocationCpu
 * DxgkVidMmMapAllocationUser
 * DxgkVidMmUnmapAllocationUser
 *
 * CPU-accessible VA establishment/release.
 */
NTSTATUS
DxgkVidMmMapAllocationCpu(
    _In_  PDXGKVMM_ALLOCATION   Allocation,
    _Out_ PVOID                *OutVa);

NTSTATUS
DxgkVidMmMapAllocationUser(
    _In_  PDXGKVMM_ALLOCATION   Allocation,
    _Out_ PVOID                *OutVa);

VOID
DxgkVidMmUnmapAllocationCpu(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmUnmapAllocationUser(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmProcessCleanup(
    _In_ PEPROCESS Process);

NTSTATUS
DxgkVidMmCleanupDeviceAllocations(
    _In_ PDXGKRNL_DEVICE Device);

VOID
DxgkVidMmCleanupAdapterAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
DxgkVidMmSetProcessReservation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP Group,
    _In_ UINT64 Reservation);

VOID
DxgkVidMmReleaseProcessReservations(
    _In_ PEPROCESS Process);

NTSTATUS
DxgkVidMmQueryProcessBudget(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP MemorySegmentGroup,
    _Out_ UINT64 *Budget,
    _Out_ UINT64 *CurrentUsage,
    _Out_ UINT64 *CurrentReservation,
    _Out_ UINT64 *AvailableForReservation);

NTSTATUS
DxgkVidMmQuerySegmentSizes(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ D3DKMT_SEGMENTGROUPSIZEINFO *Info);

NTSTATUS
DxgkVidMmSubmitAperturePagingPacket(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN Map,
    _In_ D3DKMT_HANDLE hSignalSyncObject,
    _In_ ULONG64 SignalFenceValue);

NTSTATUS
DxgkVidMmReferenceAllocation(
    _In_opt_ HANDLE Handle,
    _In_opt_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation);

NTSTATUS
DxgkVidMmReferenceProcessAllocation(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ PDXGKRNL_PROCESS ExpectedProcess,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation);

NTSTATUS
DxgkVidMmAcquireGpuVaBindingReferences(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ PDXGKRNL_PROCESS ExpectedProcess,
    _Out_ PDXGKVMM_ALLOCATION *OutLogicalAllocation,
    _Out_ PDXGKVMM_ALLOCATION *OutBackingAllocation);

NTSTATUS
DxgkVidMmReferenceOpenBinding(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PHANDLE OutOpenBindingHandle,
    _Out_ PDXGKVMM_ALLOCATION *OutBindingReference);

NTSTATUS DxgkVidMmCreatePresentBinding(_In_ PDXGKRNL_DEVICE Device, _In_ PDXGKVMM_ALLOCATION BackingAllocation, _In_ BOOLEAN ReadOnly, _Out_ PHANDLE OutOpenBindingHandle, _Out_ PDXGKVMM_ALLOCATION *OutBindingReference);

/* Device must be held by a live DxgkReferenceDevice ownership reference. */
NTSTATUS DxgkVidMmDestroyPresentBinding(_In_ PDXGKRNL_DEVICE Device, _In_ PDXGKVMM_ALLOCATION BindingReference);

BOOLEAN
DxgkVidMmDuplicateLogicalReference(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmDereferenceLogicalAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmDereferenceAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation);

/* Duplicate a lifetime reference already owned by the caller.  This remains
 * valid after handle tombstoning, but never resurrects a zero reference. */
BOOLEAN
DxgkVidMmDuplicateAllocationReference(
    _In_ PDXGKVMM_ALLOCATION Allocation);

NTSTATUS
DxgkVidMmSetAllocationPriorities(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_reads_(AllocationCount) CONST UINT *Priorities,
    _In_ UINT AllocationCount);

NTSTATUS
DxgkVidMmQueryAllocationPriorities(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ UINT AllocationCount,
    _Out_writes_(AllocationCount) UINT *Priorities);

NTSTATUS
DxgkVidMmQueryAllocationResidencyStates(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION const *Allocations,
    _In_ UINT AllocationCount,
    _Out_writes_(AllocationCount) D3DKMT_ALLOCATIONRESIDENCYSTATUS *ResidencyStates);

/*
 * DxgkVidMmFillAllocationListEntry
 *
 * Fills a DXGK_ALLOCATIONLIST entry's SegmentId/PhysicalAddress from the
 * allocation's current placement so DxgkDdiPatch can relocate DMA buffer
 * references the documented way.
 */
VOID
DxgkVidMmFillAllocationListEntry(
    _In_ D3DKMT_HANDLE AllocationHandle,
    _Inout_ DXGK_ALLOCATIONLIST *ListEntry);

/* The caller owns a lifetime reference.  Acquisition and the optional
 * placement snapshot are atomic with normal residency transitions. */
NTSTATUS
DxgkVidMmAcquireSubmissionResidencyPin(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _Out_opt_ DXGK_ALLOCATIONLIST *ListEntry);

VOID
DxgkVidMmReleaseSubmissionResidencyPin(
    _In_ PDXGKVMM_ALLOCATION Allocation);

NTSTATUS
DxgkVidMmReferenceResource(
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN GlobalShareHandle,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PDXGKVMM_RESOURCE *OutResource);

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
    _Out_writes_(AllocationCount) PHANDLE OutAllocationHandles);

NTSTATUS
DxgkVidMmSnapshotResourceAllocations(
    _In_ PDXGKVMM_RESOURCE Resource,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _Outptr_result_buffer_(*OutAllocationCount) PDXGKVMM_ALLOCATION **OutAllocations,
    _Out_ PUINT OutAllocationCount,
    _Out_ PUINT OutTotalPrivateDriverDataSize);

VOID
DxgkVidMmReleaseAllocationSnapshot(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION *Allocations,
    _In_ UINT AllocationCount);

NTSTATUS
DxgkVidMmAttachAllocationToResource(
    _In_ PDXGKVMM_RESOURCE Resource,
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmDereferenceResource(
    _In_ PDXGKVMM_RESOURCE Resource);

PDXGKVMM_RESOURCE
DxgkVidMmCreateResourceWrapper(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ HANDLE          MiniportHandle,
    _In_ D3DKMT_HANDLE       GlobalShareHandle,
    _In_ BOOLEAN              Shareable,
    _In_reads_bytes_opt_(PrivateRuntimeDataSize)
            CONST VOID      *PrivateRuntimeData,
    _In_    UINT             PrivateRuntimeDataSize,
    _In_reads_bytes_opt_(ResourcePrivateDriverDataSize)
            CONST VOID      *ResourcePrivateDriverData,
    _In_    UINT             ResourcePrivateDriverDataSize);

NTSTATUS
DxgkpVidMmDestroyResourceWrapper(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_RESOURCE Resource);

NTSTATUS
DxgkVidMmEnsureAllocationApertureMapped(
    _In_ PDXGKVMM_ALLOCATION Allocation);

/*
 * DxgkVidMmCreatePreMappedAllocation
 *
 * Creates an allocation backed by a pre-existing physical/virtual mapping
 * (e.g., the GOP framebuffer).  No miniport DxgkDdiCreateAllocation call.
 */
NTSTATUS
DxgkVidMmCreatePreMappedAllocation(
    _In_  PDXGKRNL_ADAPTER   Adapter,
    _In_  PHYSICAL_ADDRESS    PhysicalAddress,
    _In_  PVOID               VirtualAddress,
    _In_  SIZE_T              Size,
    _Out_ HANDLE             *OutHandle);

LARGE_INTEGER
DxgkVidMmGetAllocationPrimaryAddress(
    _In_ PDXGKVMM_ALLOCATION Allocation);

PVOID
DxgkVidMmGetHandleData(
    _In_ DXGK_HANDLE_TYPE Type,
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN DeviceSpecific);

PVOID
DxgkVidMmAcquireHandleData(
    _In_ DXGK_HANDLE_TYPE Type,
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN DeviceSpecific,
    _Out_ PDXGKARG_RELEASE_HANDLE ReleaseHandle);

VOID
DxgkVidMmReleaseHandleData(
    _In_ DXGK_HANDLE_TYPE Type,
    _In_ DXGKARG_RELEASE_HANDLE ReleaseHandle);

NTSTATUS DxgkVidMmPublishSegments(_In_ PDXGKRNL_ADAPTER Adapter);
