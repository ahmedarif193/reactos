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

/* Opaque AcquireHandleData references returned to a display miniport. */
#define TAG_VIDMM_HANDLE_REF 'HVxD'

/* A GPUVA write dependency has a stable identity even when VidSch rebinds
 * its hardware fence at dispatch. The packet owns this record and a backing
 * allocation reference until terminal cleanup. */
typedef struct _DXGKVMM_TRACKED_SUBMISSION
{
    LIST_ENTRY Entry;
    PDXGKVMM_ALLOCATION Allocation;
    ULONGLONG Sequence;
    BOOLEAN Pending;
} DXGKVMM_TRACKED_SUBMISSION, *PDXGKVMM_TRACKED_SUBMISSION;

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
    ULONG               PagingBufferPrivateDataSize;

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

    /* TRUE when sharing uses a secured NT object instead of hGlobalShare. */
    BOOLEAN            NtSecuritySharing;

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

typedef struct _DXGKVMM_USER_MAPPING
{
    LIST_ENTRY          Entry;
    PVOID               MapBase;
    PVOID               Address;
    PMDL                Mdl;
    PEPROCESS           Process;
    ULONG               LockCount;

    /*
     * Placement identity captured when MapBase was established.  A balanced
     * D3DKMTUnlock releases the residency pin and makes Address unusable by
     * the caller, but VidMm may retain the process mapping for a later lock.
     * A changed identity makes that dormant mapping stale and forces a
     * rebuild before it is returned again.
     */
    BOOLEAN             Resident;
    ULONG               SegmentId;
    ULONGLONG           SegmentOffset;
    PHYSICAL_ADDRESS    PhysicalAddress;
    PVOID               SystemMemory;
} DXGKVMM_USER_MAPPING, *PDXGKVMM_USER_MAPPING;

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

    /*
     * Highest submission fence, per node, of any command that referenced this
     * allocation.  Destroy must wait for these to retire.  Waiting only on the
     * destroying device's own queued work is not enough: an allocation shared
     * with another device is referenced by that device's submissions too, and
     * clearing its PTEs while such a command is still in flight makes the GPU
     * read an unmapped page.
     */
    volatile ULONG      LastRefFenceId[DXGK_MAX_TRACKED_NODES];

    /* Fence identities are reset as a unit and the adapter bumps
     * SubmittedFenceIdentityEpoch when that happens.  References stamped in an
     * older epoch name fence ids that no longer exist, so they must be treated
     * as retired rather than waited on - otherwise every destroy after a reset
     * would block for the whole timeout. */
    volatile LONG       LastRefEpoch;

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

    /* Immutable on the backing allocation; UNINITIALIZED means non-primary
     * (NOTAPPLICABLE is zero and would collide with source 0). Open aliases
     * resolve here. Creation does not grant VidPn source ownership. */
    D3DDDI_VIDEO_PRESENT_SOURCE_ID PrimaryVidPnSourceId;

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

    /* CDD redirection identity and the newest GPU write on each scheduler
     * node. ResidencyLock serializes association and fence snapshots. */
    ULONG_PTR           RedirectionSurfaceHandle;
    ULONG               RedirectionSubmittedFenceId[DXGK_MAX_TRACKED_NODES];

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
    D3DDDI_SEGMENTPREFERENCE PreferredSegment;

    /* Mutable WDDM 2.1 allocation properties. */
    BOOLEAN             AccessedPhysically;
    BOOLEAN             Unmoveable;

    /* WDDM allocation priority (D3DDDI_ALLOCATIONPRIORITY_NORMAL default). */
    ULONG               AllocationPriority;

    /* TRUE if the allocation can be mapped into CPU virtual address space. */
    BOOLEAN             CpuVisible;
    BOOLEAN             Cached;
    BOOLEAN             ExplicitResidencyNotification;

    /* Miniport-declared capture buffer; required by GetCaptureAddress. */
    BOOLEAN             Capture;

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

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /*
     * One charge entry per process that currently references this physical
     * placement. Open/shared aliases resolve here, so every process receives
     * its own usage charge without multiplying usage for extra aliases or
     * devices in that same process. Protected by the VidMm budget lock.
     */
    LIST_ENTRY          ResidencyBudgetChargeList;
#endif

    /* Separate from user MakeResident/Evict references: GPU and overlay work
     * pins this exact placement until its terminal cleanup.  The tracked
     * subset lets D3DKMTLock wait only for submitted GPU work, not a
     * long-lived overlay placement pin. */
    KSPIN_LOCK          TrackedSubmissionLock;
    volatile LONG       SubmissionResidencyPinCount;
    LIST_ENTRY          TrackedSubmissions;
    ULONGLONG           TrackedSubmissionSequence;
    ULONGLONG           TrackedSubmissionFailureSequence;
    NTSTATUS            TrackedSubmissionFailure;
    KEVENT              TrackedSubmissionsChangedEvent;

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

    /* One cached D3DKMTLock mapping per process, shared by nested locks. */
    LIST_ENTRY          UserModeMappingList;
    volatile LONG       UserModeMappingCount;
    KMUTEX              UserModeLock;
    /* Remains discoverable after handle retirement, until mappings are gone. */
    LIST_ENTRY          UserModeMappingGlobalEntry;
    volatile LONG       UserModeMappingRegistered;

    /* Miniport-side allocation handle (from DxgkDdiCreateAllocation). */
    HANDLE              MiniportHandle;
    BOOLEAN             ContextAllocation;
    PVOID               ContextAllocationHandle;
    BOOLEAN             SysMemContiguousWc;
    PMDL                SysMemPagesMdl;

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
    BOOLEAN             OfferAllowDecommit;
    BOOLEAN             OfferDecommitted;
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
DxgkVidMmPrepareForStop(
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

VOID
DxgkpVidMmFlushCpuCache(
    _In_reads_bytes_(Size) PVOID Address,
    _In_ SIZE_T Size);

VOID DxgkVidMmDumpUserMappings(_In_ PDXGKVMM_ALLOCATION Allocation);

NTSTATUS
DxgkVidMmCreateContextAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ HANDLE DriverAllocation,
    _In_ SIZE_T Size,
    _In_ UINT Alignment,
    _In_ UINT SupportedSegmentSet,
    _In_ UINT EvictionSegmentSet,
    _In_ DXGK_SEGMENTPREFERENCE PreferredSegment,
    _In_ DXGK_SEGMENTBANKPREFERENCE HintedBank,
    _In_ DXGK_ALLOCATIONINFOFLAGS Flags,
    _In_ BOOLEAN MapGpuVirtualAddress,
    _Out_ PHANDLE OutAllocation);

NTSTATUS
DxgkVidMmUpdateContextAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE ContextAllocationHandle,
    _In_reads_bytes_opt_(PrivateDriverDataSize) PVOID PrivateDriverData,
    _In_ ULONG PrivateDriverDataSize);

VOID DxgkVidMmDumpSegments(_In_ PDXGKRNL_ADAPTER Adapter);
VOID DxgkVidMmDumpContextAllocations(_In_ PDXGKRNL_ADAPTER Adapter);

NTSTATUS
DxgkVidMmMapContextAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE ContextAllocationHandle,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ D3DGPU_VIRTUAL_ADDRESS MinimumAddress,
    _In_ D3DGPU_VIRTUAL_ADDRESS MaximumAddress,
    _In_ ULONGLONG OffsetInPages,
    _In_ ULONGLONG SizeInPages,
    _In_ D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection,
    _In_ UINT64 DriverProtection,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress);

NTSTATUS
DxgkVidMmDestroyContextAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE AllocationHandle);

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
    _In_ ULONG            Priority,
    _In_ ULONG            Flags);

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

BOOLEAN
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

/*
 * D3DKMTQueryStatistics support.  SegmentIndex is the zero-based index the
 * public query uses, not the one-based WDDM segment id a placement records.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkVidMmQuerySegmentStatistics(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG SegmentIndex,
    _Out_ D3DKMT_QUERYSTATISTICS_SEGMENT_INFORMATION *Information);

NTSTATUS
DxgkVidMmQueryProcessSegmentStatistics(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _In_ ULONG SegmentIndex,
    _Out_ D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT_INFORMATION *Information);

NTSTATUS
DxgkVidMmQuerySegmentUsage(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG SegmentIndex,
    _Out_ D3DKMT_QUERYSTATISTICS_MEMORY_USAGE *Usage);

NTSTATUS
DxgkVidMmQuerySegmentGroupUsage(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP Group,
    _Out_ D3DKMT_QUERYSTATISTICS_MEMORY_USAGE *Usage);

NTSTATUS
DxgkVidMmQueryProcessMemoryStatistics(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _Out_ D3DKMT_QUERYSTATISTICS_SYSTEM_MEMORY *SystemMemory);

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

VOID
NTAPI
DxgkUnreferenceDxgAllocation(
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

NTSTATUS
DxgkVidMmAcquireSubmissionResidencyPinEx(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _Out_opt_ DXGK_ALLOCATIONLIST *ListEntry,
    _In_ BOOLEAN CpuDirty);

NTSTATUS
DxgkVidMmAcquireTrackedSubmissionResidencyPin(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ BOOLEAN CpuDirty,
    _Out_ PDXGKVMM_TRACKED_SUBMISSION Submission);

VOID
DxgkVidMmReleaseSubmissionResidencyPin(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmReleaseTrackedSubmissionResidencyPin(
    _Inout_ PDXGKVMM_TRACKED_SUBMISSION Submission);

VOID
DxgkVidMmCompleteTrackedSubmission(
    _Inout_ PDXGKVMM_TRACKED_SUBMISSION Submission,
    _In_ NTSTATUS Status);

ULONGLONG
DxgkVidMmSnapshotTrackedSubmissions(
    _In_ PDXGKVMM_ALLOCATION Allocation);

NTSTATUS
DxgkVidMmWaitForSubmissionSequence(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONGLONG Sequence,
    _In_ BOOLEAN DoNotWait);

NTSTATUS
DxgkVidMmWaitForTrackedSubmissions(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN DoNotWait);

NTSTATUS
DxgkVidMmReferenceResource(
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN GlobalShareHandle,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PDXGKVMM_RESOURCE *OutResource);

NTSTATUS
DxgkVidMmValidateSharedResourceOwner(
    _In_ ULONG GlobalShare,
    _In_ const LUID *AdapterLuid,
    _In_ PEPROCESS ExpectedProcess);

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

VOID
NTAPI
DxgkUnreferenceDxgResource(
    _In_ PDXGKVMM_RESOURCE Resource);

NTSTATUS
DxgkVidMmInvalidateReferencedAllocationCache(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length);

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2000)
NTSTATUS
DxgkVidMmCopyResourcePresentPrivateDriverData(
    _In_ D3DKMT_HANDLE ResourceHandle,
    _In_ PEPROCESS ExpectedProcess,
    _Out_writes_bytes_to_opt_(BufferCapacity, *RequiredSize) PVOID Buffer,
    _In_ UINT BufferCapacity,
    _Out_ PUINT RequiredSize);

NTSTATUS
DxgkVidMmInvalidateAllocationCache(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE Handle,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length);
#endif

NTSTATUS
DxgkVidMmOfferReferencedAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG Priority,
    _In_ ULONG Flags);

NTSTATUS
DxgkVidMmReclaimReferencedAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PBOOLEAN Discarded);

#if defined(REACTOS_WDDM_TARGET_LEVEL) && (REACTOS_WDDM_TARGET_LEVEL >= 2100)
NTSTATUS
DxgkVidMmReclaimReferencedAllocation3(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Out_ PULONG Result);

NTSTATUS
DxgkVidMmUpdateAllocationProperty(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE Handle,
    _In_ ULONG SupportedSegmentSet,
    _In_ ULONG PreferredSegmentValue,
    _In_ ULONG PropertyFlagsValue,
    _In_ ULONG PropertyMaskValue);
#endif

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

NTSTATUS
DxgkVidMmCreateVirtualDmaBufferBacking(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ ULONG Size,
    _In_ ULONG SegmentSet,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation,
    _Out_ struct _DXGKVMM_VIRTUAL_DMA_BACKING **OutBacking,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress);

VOID
DxgkVidMmFreeVirtualDmaBufferBacking(
    _In_ struct _DXGKVMM_VIRTUAL_DMA_BACKING *Backing);

NTSTATUS
DxgkVidMmResetVirtualDmaBufferMappings(
    _In_ struct _DXGKVMM_VIRTUAL_DMA_BACKING *Backing);

VOID
DxgkVidMmUnpinVirtualDmaBufferMappings(
    _In_ struct _DXGKVMM_VIRTUAL_DMA_BACKING *Backing);

NTSTATUS
DxgkVidMmMapVirtualPresentAllocation(
    _In_ struct _DXGKVMM_VIRTUAL_DMA_BACKING *Backing,
    _In_ PDXGKVMM_ALLOCATION Binding,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN Write,
    _Out_ D3DGPU_VIRTUAL_ADDRESS *OutAddress);

NTSTATUS
DxgkVidMmCreateDmaBufferBacking(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG Size,
    _In_ ULONG SegmentSet,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation);

NTSTATUS
DxgkVidMmMapPageTableSegment(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE MiniportDeviceHandle,
    _In_ ULONG SegmentId,
    _In_ PVOID KernelVa,
    _In_ ULONG Size,
    _In_ ULONG Alignment,
    _In_ ULONGLONG OwnerCookie,
    _Out_ PULONGLONG OutSegmentOffset,
    _Out_ PMDL *OutMdl);

VOID
DxgkVidMmUnmapPageTableSegment(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE MiniportDeviceHandle,
    _In_ ULONG SegmentId,
    _In_ ULONGLONG SegmentOffset,
    _In_ ULONG Size,
    _In_ ULONGLONG OwnerCookie,
    _In_opt_ PMDL Mdl);

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

D3DKMT_HANDLE
DxgkVidMmGetHandleParent(
    _In_ D3DKMT_HANDLE AllocationHandle);

D3DKMT_HANDLE
DxgkVidMmEnumHandleChildren(
    _In_ D3DKMT_HANDLE ResourceHandle,
    _In_ UINT Index);

NTSTATUS
DxgkVidMmGetCaptureAddress(
    INOUT_PDXGKARGCB_GETCAPTUREADDRESS GetCaptureAddress);

NTSTATUS DxgkVidMmPublishSegments(_In_ PDXGKRNL_ADAPTER Adapter);
