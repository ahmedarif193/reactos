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

} DXGKRNL_SEGMENT, *PDXGKRNL_SEGMENT;

typedef struct _DXGKVMM_RESOURCE
{
    /* Owning logical device. */
    PDXGKRNL_DEVICE    Device;

    /* Opaque miniport-side resource handle. */
    HANDLE             MiniportHandle;

    /* 32-bit user-visible D3DKMT resource handle. */
    D3DKMT_HANDLE      Handle;

    /* Global-share handle used by OpenResource / GetSharedPrimaryHandle. */
    D3DKMT_HANDLE      GlobalShareHandle;

    /* Primary allocation exported by this resource, when any. */
    HANDLE             AllocationHandle;

    /* Resource-private driver data used by QueryResourceInfo/OpenResource. */
    PVOID              ResourcePrivateDriverData;
    UINT               ResourcePrivateDriverDataSize;

    /* Number of tracked allocations referencing this resource. */
    ULONG              AllocationCount;

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

    /* Back-pointer to the owning adapter. */
    PDXGKRNL_ADAPTER    Adapter;

    /* Owning logical device for user-mode allocations, NULL for internal use. */
    PDXGKRNL_DEVICE     Device;

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
    FAST_MUTEX          UserModeLock;

    /* Miniport-side allocation handle (from DxgkDdiCreateAllocation). */
    HANDLE              MiniportHandle;

    /* Allocation-private driver data used by QueryResourceInfo/OpenResource. */
    PVOID               PrivateDriverData;
    UINT                PrivateDriverDataSize;

    /* MDL backing an aperture attachment, when active. */
    PMDL                ApertureMdl;

    /* TRUE once BuildPagingBuffer(MAP_APERTURE_SEGMENT) succeeded. */
    BOOLEAN             ApertureMapped;

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

/*
 * DxgkVidMmHandleToAllocation
 *
 * Validates and dereferences a D3DKMT allocation handle.
 */
PDXGKVMM_ALLOCATION
DxgkVidMmHandleToAllocation(
    _In_opt_ HANDLE Handle);

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

PDXGKVMM_RESOURCE
DxgkVidMmHandleToResource(
    _In_ D3DKMT_HANDLE Handle);

PDXGKVMM_RESOURCE
DxgkVidMmHandleToGlobalShare(
    _In_ D3DKMT_HANDLE Handle);

PDXGKVMM_RESOURCE
DxgkVidMmCreateResourceWrapper(
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ HANDLE          MiniportHandle,
    _In_ D3DKMT_HANDLE       GlobalShareHandle,
    _In_opt_ HANDLE          AllocationHandle,
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
    _In_ D3DKMT_HANDLE Handle);
