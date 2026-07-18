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

    /*
     * Bytes currently committed to resident allocations.  Updated with
     * InterlockedAdd64 under Segment->Lock for atomic RMW semantics.
     * On x86-64 (TSO) this is sufficient for correct visibility without
     * an explicit fence — LOCK ADD is fully ordered.
     */
    LONGLONG            UsedSize;

    /* Monotone high-water placement cursor: virgin-VA-first allocation
     * (GPU VA recycling wedges the V3D vertex pipe — see rpi5vc4 P0.3);
     * first-fit is the fallback once the segment tail is exhausted. */
    ULONGLONG           BumpOffset;

    /* Physical base address of the segment (VRAM or aperture window). */
    PHYSICAL_ADDRESS    BaseAddress;

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
     * Ordered list of resident DXGKVMM_ALLOCATION objects.
     * Sorted by ascending SegmentOffset for the first-fit allocator.
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

    /*
     * Required base alignment in bytes (power of two).
     * Default is PAGE_SIZE when the miniport reports 0.
     */
    SIZE_T              Alignment;

    /* WDDM allocation priority (D3DDDI_ALLOCATIONPRIORITY_NORMAL default). */
    ULONG               AllocationPriority;

    /* TRUE if the allocation can be mapped into CPU virtual address space. */
    BOOLEAN             CpuVisible;

    /* TRUE when the allocation is resident in a GPU segment. */
    BOOLEAN             Resident;

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
     * System-memory backing store.
     * - Populated on first eviction or system-memory fallback placement.
     * - NULL when the allocation is only in a VRAM segment with no eviction.
     * - Freed at DxgkVidMmDestroyAllocation time.
     */
    PVOID               SystemMemory;


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
DxgkVidMmQueryProcessBudget(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS Process,
    _In_ D3DKMT_MEMORY_SEGMENT_GROUP MemorySegmentGroup,
    _Out_ UINT64 *Budget,
    _Out_ UINT64 *CurrentUsage,
    _Out_ UINT64 *CurrentReservation,
    _Out_ UINT64 *AvailableForReservation);

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
DxgkVidMmReferenceOpenBinding(
    _In_ HANDLE Handle,
    _In_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PHANDLE OutOpenBindingHandle,
    _Out_ PDXGKVMM_ALLOCATION *OutBindingReference);

BOOLEAN
DxgkVidMmDuplicateLogicalReference(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmDereferenceLogicalAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation);

VOID
DxgkVidMmDereferenceAllocation(
    _In_ PDXGKVMM_ALLOCATION Allocation);

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
