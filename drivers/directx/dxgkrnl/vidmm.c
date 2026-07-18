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
#include "vidmm.h"
#include "debug.h"

#define NDEBUG
#include <debug.h>

/* MACROS / CONSTANTS *********************************************************/

/*
 * VIDMM_MAX_SEGMENTS: maximum number of memory segments we support.
 * The WDDM spec allows up to 16 segments (IDs 1..16).  We use this as
 * the sanity cap when validating the miniport's NbSegments return value.
 */
#define VIDMM_MAX_SEGMENTS  16

/* Forward declaration — defined later in this file. */
static VOID DxgkpVidMmReleaseApertureMapping(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmReleaseSegmentPlacement(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmFinalizeAllocation(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmFinalizeResource(_In_ PDXGKVMM_RESOURCE Resource);
static VOID DxgkpVidMmDropAllocationHandleReference(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmDropLogicalHandleReference(_In_ PDXGKVMM_ALLOCATION Allocation);
static VOID DxgkpVidMmCloseAllocationBinding(_In_ PDXGKVMM_ALLOCATION Allocation);

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
static LIST_ENTRY DxgkVidMmAllocationListHead;
static LIST_ENTRY DxgkVidMmResourceListHead;
static ULONG      DxgkVidMmAllocationHandleCookie = 0x4D4D414C; /* "LAMM" */
static ULONG      DxgkVidMmResourceHandleCookie   = 0x4D4D4552; /* "REMM" */
static ULONG      DxgkVidMmGlobalShareHandleCookie = 0x4D4D4753; /* "SGMM" */
static volatile LONG DxgkVidMmNextAllocationHandle = 0;
static volatile LONG DxgkVidMmNextResourceHandle   = 0;
static volatile LONG DxgkVidMmNextGlobalShareHandle = 0;

/* HELPERS ********************************************************************/

/* Paging content transfer (defined after the CPU-mapping helpers). */
static VOID
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
    return (BOOLEAN)Segment->Flags.Aperture;
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
ULONGLONG
VidMmAlignUp(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment)
{
    ASSERT(Alignment != 0);
    ASSERT((Alignment & (Alignment - 1)) == 0);
    return (Value + Alignment - 1) & ~(Alignment - 1);
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
        InitializeListHead(&DxgkVidMmAllocationListHead);
        InitializeListHead(&DxgkVidMmResourceListHead);
        InterlockedExchange(&DxgkVidMmGlobalsState, 2);
        return;
    }

    while (InterlockedCompareExchange(&DxgkVidMmGlobalsState, 2, 2) != 2)
        YieldProcessor();
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
    KeInitializeEvent(&Allocation->ReferencesDrainedEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Allocation->LogicalReferencesDrainedEvent, NotificationEvent, FALSE);
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

static NTSTATUS
DxgkpVidMmCreateSystemAllocation(
    _In_      PDXGKRNL_ADAPTER     Adapter,
    _In_opt_  PDXGKRNL_DEVICE      Device,
    _In_      DXGK_ALLOCATIONINFO *AllocInfo,
    _Out_     PHANDLE              OutHandle)
{
    PDXGKVMM_ALLOCATION Alloc;
    SIZE_T AllocSize;

    if (Adapter == NULL || AllocInfo == NULL || OutHandle == NULL ||
        AllocInfo->Size == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *OutHandle = NULL;

    DxgkpVidMmEnsureGlobalsInitialized();

    AllocSize = (AllocInfo->Size + PAGE_SIZE - 1) & ~(SIZE_T)(PAGE_SIZE - 1);
    Alloc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Alloc), TAG_VIDMM_ALLOC);
    if (Alloc == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Alloc, sizeof(*Alloc));
    DxgkpVidMmInitializeAllocationLifetime(Alloc);

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
    Alloc->Size = AllocInfo->Size;
    Alloc->Alignment = AllocInfo->Alignment ? AllocInfo->Alignment : PAGE_SIZE;
    Alloc->AllocationPriority = AllocInfo->AllocationPriority ?
                                AllocInfo->AllocationPriority :
                                VIDMM_PRIORITY_NORMAL;
    Alloc->CpuVisible = TRUE;
    Alloc->Resident = FALSE;
    Alloc->PhysicalAddress = MmGetPhysicalAddress(Alloc->SystemMemory);
    Alloc->CpuAddress = Alloc->SystemMemory;
    ExInitializeFastMutex(&Alloc->UserModeLock);

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
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    AllocInfo->hAllocation = NULL;
    *OutHandle = (HANDLE)(ULONG_PTR)Alloc->Handle;
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
    NTSTATUS Status = STATUS_INVALID_HANDLE;

    if (OutAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocation = NULL;
    if (Handle == NULL)
        return STATUS_INVALID_HANDLE;

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
    NTSTATUS Status = STATUS_INVALID_HANDLE;

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
    NTSTATUS Status = STATUS_INVALID_HANDLE;

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
        KeSetEvent(&Allocation->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
        if (InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0 && InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) != 2)
            DxgkpVidMmScheduleAllocationFinalizer(Allocation);
    }
}

NTSTATUS
DxgkVidMmReferenceResource(
    _In_ D3DKMT_HANDLE Handle,
    _In_ BOOLEAN GlobalShareHandle,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _Out_ PDXGKVMM_RESOURCE *OutResource)
{
    PDXGKVMM_RESOURCE Resource;
    NTSTATUS Status = STATUS_INVALID_HANDLE;

    if (OutResource == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutResource = NULL;
    if (Handle == 0)
        return STATUS_INVALID_HANDLE;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    Resource = GlobalShareHandle ? DxgkpVidMmLookupGlobalShareLocked(Handle) : DxgkpVidMmLookupResourceLocked(Handle);
    if (Resource != NULL && InterlockedCompareExchange(&Resource->Destroying, 0, 0) == 0 && (ExpectedDevice == NULL || Resource->Device == ExpectedDevice))
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
    NTSTATUS Status;

    PAGED_CODE();
    if (Resource == NULL || ExpectedAdapter == NULL || OutAllocations == NULL || OutAllocationCount == NULL || OutTotalPrivateDriverDataSize == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocations = NULL;
    *OutAllocationCount = 0;
    *OutTotalPrivateDriverDataSize = 0;

    for (;;)
    {
        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        if (Resource->Adapter != ExpectedAdapter || Resource->BackingResource != NULL || InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0 || Resource->AllocationCount == 0)
        {
            ExReleaseFastMutex(&DxgkVidMmResourceListLock);
            return STATUS_INVALID_HANDLE;
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
        if (Resource->AllocationCount != Capacity || InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
            Status = STATUS_RETRY;
        else
        {
            for (Entry = Resource->AllocationList.Flink; Entry != &Resource->AllocationList; Entry = Entry->Flink)
            {
                PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, ResourceEntry);

                if (Count >= Capacity || Allocation->Adapter != ExpectedAdapter || Allocation->Resource != Resource || Allocation->Initializing || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
                {
                    Status = STATUS_RETRY;
                    break;
                }
                if (TotalSize > MAXULONG - Allocation->PrivateDriverDataSize)
                {
                    Status = STATUS_INTEGER_OVERFLOW;
                    break;
                }
                InterlockedIncrement(&Allocation->ReferenceCount);
                Allocations[Count++] = Allocation;
                TotalSize += Allocation->PrivateDriverDataSize;
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
    DXGK_OPENALLOCATIONINFO *OpenInfo = NULL;
    DXGKARG_OPENALLOCATION OpenArgs;
    DXGKARG_CLOSEALLOCATION CloseArgs;
    UINT Offset = 0;
    UINT Index;
    UINT CloseCount = 0;
    BOOLEAN AllocationsPublished = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Device == NULL || Device->Adapter == NULL || BackingResource == NULL || BackingAllocations == NULL || AllocationCount == 0 || OutResource == NULL || OutAllocationHandles == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutResource = NULL;
    for (Index = 0; Index < AllocationCount; ++Index)
        OutAllocationHandles[Index] = NULL;
    Adapter = Device->Adapter;
    if (BackingResource->Adapter != Adapter || BackingResource->BackingResource != NULL || DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL)
        return STATUS_NOT_SUPPORTED;
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*OpenInfo) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Resource->OpenAllocations) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Resource->OpenBindingScratch))
        return STATUS_INTEGER_OVERFLOW;

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
    Resource->Device = Device;
    Resource->Adapter = Adapter;
    Resource->BackingResource = BackingResource;
    Resource->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextResourceHandle, DxgkVidMmResourceHandleCookie);
    Resource->OpenAllocationCapacity = AllocationCount;
    Resource->OpenAllocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Resource->OpenAllocations), TAG_VIDMM_RESOURCE);
    Resource->OpenBindingScratch = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocationCount * sizeof(*Resource->OpenBindingScratch), TAG_VIDMM_RESOURCE);
    if (Resource->OpenAllocations == NULL || Resource->OpenBindingScratch == NULL)
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
        Allocation->Magic = DXGKVMM_ALLOCATION_MAGIC;
        Allocation->Adapter = Adapter;
        Allocation->Device = Device;
        Allocation->MiniportDeviceHandle = Device->hMiniportDevice;
        Allocation->BackingAllocation = BackingAllocation;
        Allocation->Resource = Resource;
        Allocation->OpenBindingIndex = Index;
        Allocation->Initializing = TRUE;
        Allocation->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle, DxgkVidMmAllocationHandleCookie);
        ExInitializeFastMutex(&Allocation->UserModeLock);
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
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (OpenInfo[Index].hDeviceSpecificAllocation == NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        Resource->OpenBindingScratch[CloseCount++] = OpenInfo[Index].hDeviceSpecificAllocation;
    }
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
        Resource->OpenAllocations[Index]->OpenBindingHandle = OpenInfo[Index].hDeviceSpecificAllocation;
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

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
    ExFreePoolWithTag(OpenInfo, TAG_VIDMM_RESOURCE);
    return STATUS_SUCCESS;

Cleanup:
    if (AllocationsPublished && Resource != NULL && Resource->OpenAllocations != NULL)
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
    if (Resource != NULL && OpenInfo != NULL)
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
    if (Resource != NULL && Resource->OpenAllocations != NULL)
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
            if (Resource != NULL && InterlockedCompareExchange(&Resource->Destroying, 0, 0) == 0)
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
        Status = STATUS_INVALID_HANDLE;
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
        KeSetEvent(&Allocation->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
        if (InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) != 2)
            DxgkpVidMmScheduleAllocationFinalizer(Allocation);
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

static VOID
DxgkpVidMmCloseAllocationBinding(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter;
    DXGKARG_CLOSEALLOCATION CloseArgs;
    HANDLE OpenHandle;

    PAGED_CODE();
    ASSERT(Allocation != NULL);
    DxgkpVidMmDropLogicalHandleReference(Allocation);
    KeWaitForSingleObject(&Allocation->LogicalReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    OpenHandle = Allocation->OpenBindingHandle;
    Allocation->OpenBindingHandle = NULL;
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    Adapter = Allocation->Adapter;
    if (OpenHandle == NULL || Adapter == NULL || DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL || !DxgkAcquireMiniportCallback(Adapter))
        return;
    RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
    CloseArgs.NumAllocations = 1;
    CloseArgs.pOpenHandleList = &OpenHandle;
    DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(Allocation->MiniportDeviceHandle, &CloseArgs);
    DxgkReleaseMiniportCallback(Adapter);
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
        if (Allocation->SystemMemory != NULL)
            ExFreePoolWithTag(Allocation->SystemMemory, TAG_VIDMM_ALLOC);
        Allocation->SystemMemory = NULL;
        if (Allocation->PrivateDriverData != NULL)
            ExFreePoolWithTag(Allocation->PrivateDriverData, TAG_VIDMM_ALLOC);
        Allocation->PrivateDriverData = NULL;
        Allocation->PrivateDriverDataSize = 0;
        if (Allocation->MiniportHandle != NULL && Adapter != NULL && DxgkAcquireMiniportCallback(Adapter))
        {
            DXGKARG_DESTROYALLOCATION DestroyArgs;

            RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
            DestroyArgs.NumAllocations = 1;
            DestroyArgs.phAllocation = &Allocation->MiniportHandle;
            if (Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation != NULL)
                Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation(Adapter->MiniportDeviceContext, &DestroyArgs);
            DxgkReleaseMiniportCallback(Adapter);
        }
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
    ASSERT(IsListEmpty(&Resource->AllocationList));

    if (Resource->BackingResource != NULL)
    {
        DxgkVidMmDereferenceResource(Resource->BackingResource);
        Resource->BackingResource = NULL;
    }
    else if (Resource->MiniportHandle != NULL && Adapter != NULL && DxgkAcquireMiniportCallback(Adapter))
    {
        DXGKARG_DESTROYALLOCATION DestroyArgs;

        RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
        DestroyArgs.hResource = Resource->MiniportHandle;
        DestroyArgs.Flags.DestroyResource = 1;
        if (Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation != NULL)
            Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation(Adapter->MiniportDeviceContext, &DestroyArgs);
        DxgkReleaseMiniportCallback(Adapter);
    }
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

static VOID
NTAPI
DxgkpVidMmCloseOpenResourceWorker(
    _In_ PVOID Context)
{
    PDXGKVMM_RESOURCE Resource = Context;
    PDXGKRNL_ADAPTER Adapter = Resource->Adapter;
    DXGKARG_CLOSEALLOCATION CloseArgs;
    HANDLE MiniportDeviceHandle = NULL;
    UINT CloseCount = 0;
    UINT Index;

    PAGED_CODE();
    for (Index = 0; Index < Resource->OpenAllocationCapacity; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation;

        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        Allocation = Resource->OpenAllocations[Index];
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (Allocation != NULL && InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) == 2)
        {
            KeWaitForSingleObject(&Allocation->LogicalReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
            KeWaitForSingleObject(&Allocation->ReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
        }
    }

    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    for (Index = 0; Index < Resource->OpenAllocationCapacity; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation = Resource->OpenAllocations[Index];

        if (Allocation == NULL || InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) != 2)
            continue;
        if (Allocation->OpenBindingHandle != NULL)
            Resource->OpenBindingScratch[CloseCount++] = Allocation->OpenBindingHandle;
        if (MiniportDeviceHandle == NULL)
            MiniportDeviceHandle = Allocation->MiniportDeviceHandle;
    }
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    if (CloseCount != 0 && Adapter != NULL && DxgkAcquireMiniportCallback(Adapter))
    {
        RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
        CloseArgs.NumAllocations = CloseCount;
        CloseArgs.pOpenHandleList = Resource->OpenBindingScratch;
        if (DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) != NULL)
            DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(MiniportDeviceHandle, &CloseArgs);
        DxgkReleaseMiniportCallback(Adapter);
    }
    for (Index = 0; Index < Resource->OpenAllocationCapacity; ++Index)
    {
        PDXGKVMM_ALLOCATION Allocation;

        ExAcquireFastMutex(&DxgkVidMmResourceListLock);
        Allocation = Resource->OpenAllocations[Index];
        if (Allocation != NULL && InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) == 2)
            Allocation->OpenBindingHandle = NULL;
        else
            Allocation = NULL;
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (Allocation != NULL)
        {
            InterlockedExchange(&Allocation->FinalizeQueued, 1);
            DxgkpVidMmFinalizeAllocation(Allocation);
        }
    }
    DxgkVidMmDereferenceResource(Resource);
}

NTSTATUS
DxgkpVidMmDestroyResourceWrapper(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_RESOURCE Resource)
{
    PDXGKVMM_ALLOCATION *SourceAllocations = NULL;
    PHANDLE SourceOpenHandles = NULL;
    HANDLE SourceMiniportDeviceHandle = NULL;
    PLIST_ENTRY Entry;
    UINT BatchCount = 0;
    UINT SourceCount = 0;
    UINT SourceCloseCount = 0;
    UINT SourceCapacity = 0;
    UINT Index;
    BOOLEAN SourceResource;

    if (Adapter == NULL || Resource == NULL || Resource->Adapter != Adapter)
        return STATUS_INVALID_PARAMETER;
    DxgkpVidMmEnsureGlobalsInitialized();

RetrySnapshot:
    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        return STATUS_INVALID_HANDLE;
    }
    for (Entry = DxgkVidMmResourceListHead.Flink; Entry != &DxgkVidMmResourceListHead && CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry) != Resource; Entry = Entry->Flink)
        NOTHING;
    if (Entry == &DxgkVidMmResourceListHead)
    {
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        return STATUS_INVALID_HANDLE;
    }
    InterlockedIncrement(&Resource->ReferenceCount);
    SourceResource = Resource->BackingResource == NULL;
    SourceCapacity = SourceResource ? Resource->AllocationCount : 0;
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);

    if (SourceCapacity != 0)
    {
        if ((SIZE_T)SourceCapacity > MAXULONG_PTR / sizeof(*SourceAllocations) || (SIZE_T)SourceCapacity > MAXULONG_PTR / sizeof(*SourceOpenHandles))
        {
            DxgkVidMmDereferenceResource(Resource);
            return STATUS_INTEGER_OVERFLOW;
        }
        SourceAllocations = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)SourceCapacity * sizeof(*SourceAllocations), TAG_VIDMM_RESOURCE);
        SourceOpenHandles = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)SourceCapacity * sizeof(*SourceOpenHandles), TAG_VIDMM_RESOURCE);
        if (SourceAllocations == NULL || SourceOpenHandles == NULL)
        {
            if (SourceOpenHandles != NULL)
                ExFreePoolWithTag(SourceOpenHandles, TAG_VIDMM_RESOURCE);
            if (SourceAllocations != NULL)
                ExFreePoolWithTag(SourceAllocations, TAG_VIDMM_RESOURCE);
            DxgkVidMmDereferenceResource(Resource);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(SourceAllocations, (SIZE_T)SourceCapacity * sizeof(*SourceAllocations));
        RtlZeroMemory(SourceOpenHandles, (SIZE_T)SourceCapacity * sizeof(*SourceOpenHandles));
    }

    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    if (InterlockedCompareExchange(&Resource->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (SourceOpenHandles != NULL)
            ExFreePoolWithTag(SourceOpenHandles, TAG_VIDMM_RESOURCE);
        if (SourceAllocations != NULL)
            ExFreePoolWithTag(SourceAllocations, TAG_VIDMM_RESOURCE);
        DxgkVidMmDereferenceResource(Resource);
        return STATUS_INVALID_HANDLE;
    }
    for (Entry = DxgkVidMmResourceListHead.Flink; Entry != &DxgkVidMmResourceListHead && CONTAINING_RECORD(Entry, DXGKVMM_RESOURCE, GlobalResourceEntry) != Resource; Entry = Entry->Flink)
        NOTHING;
    if (Entry == &DxgkVidMmResourceListHead)
    {
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (SourceOpenHandles != NULL)
            ExFreePoolWithTag(SourceOpenHandles, TAG_VIDMM_RESOURCE);
        if (SourceAllocations != NULL)
            ExFreePoolWithTag(SourceAllocations, TAG_VIDMM_RESOURCE);
        DxgkVidMmDereferenceResource(Resource);
        return STATUS_INVALID_HANDLE;
    }
    if (SourceResource && Resource->AllocationCount > SourceCapacity)
    {
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        ExReleaseFastMutex(&DxgkVidMmResourceListLock);
        if (SourceOpenHandles != NULL)
            ExFreePoolWithTag(SourceOpenHandles, TAG_VIDMM_RESOURCE);
        if (SourceAllocations != NULL)
            ExFreePoolWithTag(SourceAllocations, TAG_VIDMM_RESOURCE);
        SourceOpenHandles = NULL;
        SourceAllocations = NULL;
        DxgkVidMmDereferenceResource(Resource);
        goto RetrySnapshot;
    }

    InterlockedExchange(&Resource->Destroying, 1);
    RemoveEntryList(&Resource->GlobalResourceEntry);
    InitializeListHead(&Resource->GlobalResourceEntry);
    if (SourceResource)
    {
        for (Entry = Resource->AllocationList.Flink; Entry != &Resource->AllocationList; Entry = Entry->Flink)
        {
            PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, ResourceEntry);

            if (InterlockedCompareExchange(&Allocation->Destroying, 1, 0) != 0)
                continue;
            ASSERT(SourceCount < SourceCapacity);
            SourceAllocations[SourceCount++] = Allocation;
            if (!IsListEmpty(&Allocation->GlobalAllocationEntry))
            {
                RemoveEntryList(&Allocation->GlobalAllocationEntry);
                InitializeListHead(&Allocation->GlobalAllocationEntry);
            }
            if (Resource->AllocationCount != 0)
                Resource->AllocationCount--;
            if (Allocation->OpenBindingHandle != NULL)
            {
                SourceOpenHandles[SourceCloseCount++] = Allocation->OpenBindingHandle;
                Allocation->OpenBindingHandle = NULL;
            }
            if (SourceMiniportDeviceHandle == NULL)
                SourceMiniportDeviceHandle = Allocation->MiniportDeviceHandle;
        }
    }
    else
    {
        for (Index = 0; Index < Resource->OpenAllocationCapacity; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Resource->OpenAllocations[Index];

            if (Allocation == NULL)
                continue;
            if (InterlockedCompareExchange(&Allocation->Destroying, 1, 0) == 0)
            {
                if (!IsListEmpty(&Allocation->GlobalAllocationEntry))
                {
                    RemoveEntryList(&Allocation->GlobalAllocationEntry);
                    InitializeListHead(&Allocation->GlobalAllocationEntry);
                }
                if (Resource->AllocationCount != 0)
                    Resource->AllocationCount--;
            }
            if (InterlockedCompareExchange(&Allocation->FinalizeQueued, 2, 0) == 0)
                BatchCount++;
        }
        if (BatchCount != 0)
            InterlockedIncrement(&Resource->ReferenceCount);
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);

    if (SourceResource)
    {
        DXGKARG_CLOSEALLOCATION CloseArgs;

        for (Index = 0; Index < SourceCount; ++Index)
        {
            DxgkpVidMmDropLogicalHandleReference(SourceAllocations[Index]);
            KeWaitForSingleObject(&SourceAllocations[Index]->LogicalReferencesDrainedEvent, Executive, KernelMode, FALSE, NULL);
        }
        if (SourceCloseCount != 0 && DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) != NULL && DxgkAcquireMiniportCallback(Adapter))
        {
            RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
            CloseArgs.NumAllocations = SourceCloseCount;
            CloseArgs.pOpenHandleList = SourceOpenHandles;
            DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(SourceMiniportDeviceHandle, &CloseArgs);
            DxgkReleaseMiniportCallback(Adapter);
        }
        for (Index = 0; Index < SourceCount; ++Index)
            DxgkpVidMmDropAllocationHandleReference(SourceAllocations[Index]);
        if (SourceOpenHandles != NULL)
            ExFreePoolWithTag(SourceOpenHandles, TAG_VIDMM_RESOURCE);
        if (SourceAllocations != NULL)
            ExFreePoolWithTag(SourceAllocations, TAG_VIDMM_RESOURCE);
    }
    else
    {
        for (Index = 0; Index < Resource->OpenAllocationCapacity; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Resource->OpenAllocations[Index];

            if (Allocation != NULL && InterlockedCompareExchange(&Allocation->FinalizeQueued, 0, 0) == 2)
            {
                DxgkpVidMmDropLogicalHandleReference(Allocation);
                DxgkpVidMmDropAllocationHandleReference(Allocation);
            }
        }
    }
    DxgkVidMmDereferenceResource(Resource);
    DxgkVidMmDereferenceResource(Resource);
    if (BatchCount != 0)
        DxgkpVidMmCloseOpenResourceWorker(Resource);
    return STATUS_SUCCESS;
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
    DXGK_QUERYSEGMENTIN4            QueryIn4;
    PUCHAR                          DescBuffer;
    SIZE_T                          DescStride;
    SIZE_T                          Seg4Stride = sizeof(DXGK_SEGMENTDESCRIPTOR4);
    BOOLEAN                         UsingSeg3;
    BOOLEAN                         UsingSeg4;
    ULONG                           PagingBufferSegmentId;
    ULONG                           PagingBufferSize;
    PDXGKRNL_SEGMENT                Segments;
    ULONG                           SegmentCount;
    ULONG                           i;

    DPRINT("DxgkVidMmInitializeAdapter: Adapter %p\n", Adapter);

    ASSERT(Adapter != NULL);
    ASSERT(Adapter->MiniportDeviceContext != NULL);
    ASSERT(Adapter->MiniportContext != NULL);
    InterlockedExchange(&Adapter->VidMmBackingCount, 0);
    KeInitializeEvent(&Adapter->VidMmBackingsDrainedEvent, NotificationEvent, TRUE);

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
           Adapter->MiniportContext->InitData.s.DxgkDdiCreateAllocation,
           (int)Adapter->MiniportContext->IsDisplayOnlyDriver);

    if (Adapter->MiniportContext->IsDisplayOnlyDriver ||
        Adapter->MiniportContext->InitData.s.DxgkDdiCreateAllocation == NULL)
    {
        DPRINT("DxgkVidMmInitializeAdapter: DOD driver — skipping segment query\n");
        return STATUS_SUCCESS;
    }

    /* -----------------------------------------------------------------------
     * Step 1: Query segment count (NbSegment discovery pass).
     *
     * WDDM 2.0+ drivers (version >= 0x4000) implement QUERYSEGMENT3 (with
     * the distinct DXGK_QUERYSEGMENTOUT3/DXGK_SEGMENTDESCRIPTOR3 layouts);
     * legacy drivers implement the original QUERYSEGMENT.  Try QUERYSEGMENT3
     * first for modern drivers and fall back.  Neither takes input data;
     * pSegmentDescriptor == NULL tells the miniport to write only NbSegment.
     * ----------------------------------------------------------------------- */
    UsingSeg4 = (Adapter->MiniportContext->InitData.s.Version >=
                 DXGKDDI_INTERFACE_VERSION_WDDM2_0);
    UsingSeg3 = (!UsingSeg4 &&
                 Adapter->MiniportContext->InitData.s.Version >= 0x4000);

    RtlZeroMemory(&SegOut, sizeof(SegOut));
    RtlZeroMemory(&SegOut3, sizeof(SegOut3));
    RtlZeroMemory(&SegOut4, sizeof(SegOut4));
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
            QueryInfo.pOutputData    = &SegOut3;
            QueryInfo.OutputDataSize = sizeof(SegOut3);
        }
        else
        {
            QueryInfo.Type           = DXGKQAITYPE_QUERYSEGMENT;
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

    DescBuffer = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                               SegmentCount * DescStride,
                                               TAG_VIDMM_SEGMENT);
    if (DescBuffer == NULL)
    {
        DPRINT1("DxgkVidMmInitializeAdapter: cannot allocate descriptor "
                "array for %lu segments\n", SegmentCount);
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(DescBuffer, SegmentCount * DescStride);

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

    /* -----------------------------------------------------------------------
     * Step 5: Populate each DXGKRNL_SEGMENT from the miniport descriptor
     *         (the two descriptor flavours carry the same placement facts).
     * ----------------------------------------------------------------------- */
/* The three descriptor flavours carry identical placement fields. */
#define VIDMM_READ_SEGMENT_DESC(DescType)                                   \
        do {                                                                \
            DescType *Desc = (DescType *)(DescBuffer + i * DescStride);     \
            Seg->Size        = (ULONGLONG)Desc->Size;                       \
            Seg->BaseAddress = Desc->BaseAddress;                           \
            Seg->Flags       = Desc->Flags;                                 \
        } while (0)

    for (i = 0; i < SegmentCount; i++)
    {
        PDXGKRNL_SEGMENT        Seg  = &Segments[i];

        /* Segment IDs are 1-based per WDDM convention. */
        Seg->SegmentId    = i + 1;
        Seg->UsedSize     = 0;
        Seg->CpuBase      = NULL;   /* mapped lazily on first CPU access */

        if (UsingSeg4)
            VIDMM_READ_SEGMENT_DESC(DXGK_SEGMENTDESCRIPTOR4);
        else if (UsingSeg3)
            VIDMM_READ_SEGMENT_DESC(DXGK_SEGMENTDESCRIPTOR3);
        else
            VIDMM_READ_SEGMENT_DESC(DXGK_SEGMENTDESCRIPTOR);

        InitializeListHead(&Seg->AllocationList);
        ExInitializeFastMutex(&Seg->Lock);

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
}


/*
 * DxgkVidMmTeardownAdapter
 *
 * Releases all VidMm state associated with an adapter.  Called from
 * DxgkAdapterStop after all allocations should have been destroyed.
 */
VOID
DxgkVidMmTeardownAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_SEGMENT    Segments;
    ULONG               i;

    DPRINT("DxgkVidMmTeardownAdapter: Adapter %p\n", Adapter);

    DxgkVidMmCleanupAdapterAllocations(Adapter);
    if (InterlockedCompareExchange(&Adapter->VidMmBackingCount, 0, 0) != 0)
        KeWaitForSingleObject(&Adapter->VidMmBackingsDrainedEvent, Executive, KernelMode, FALSE, NULL);

    if (Adapter->Segments == NULL)
        return;

    Segments = ADAPTER_SEGMENTS(Adapter);

    for (i = 0; i < Adapter->SegmentCount; i++)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[i];

        if (!IsListEmpty(&Seg->AllocationList))
        {
            DPRINT1("DxgkVidMmTeardownAdapter: segment %lu still has "
                    "allocations at teardown!\n", Seg->SegmentId);
        }

        if (Seg->CpuBase != NULL)
        {
            MmUnmapIoSpace(Seg->CpuBase, (SIZE_T)Seg->Size);
            Seg->CpuBase = NULL;
            DPRINT("DxgkVidMmTeardownAdapter: unmapped CPU VA for segment %lu\n",
                   Seg->SegmentId);
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
    NTSTATUS                    Status;
    DXGKARG_CREATEALLOCATION    CreateArgs;
    PDXGKVMM_ALLOCATION         Alloc;
    ULONG                       i;
    BOOLEAN                     Placed;

    DPRINT("DxgkVidMmCreateAllocation: Adapter=%p Size=%Iu Align=%u\n",
           Adapter,
           AllocInfo ? AllocInfo->Size : 0,
           AllocInfo ? AllocInfo->Alignment : 0);

    if (Adapter == NULL || AllocInfo == NULL || OutHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutHandle = NULL;
    if (OutResourceHandle != NULL)
        *OutResourceHandle = NULL;

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

    if (Adapter->MiniportContext->InitData.s.DxgkDdiCreateAllocation != NULL)
    {
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiCreateAllocation(
                     Adapter->MiniportDeviceContext,
                     &CreateArgs);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkVidMmCreateAllocation: DxgkDdiCreateAllocation "
                    "failed: 0x%08lx\n", Status);
            return Status;
        }
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
        return STATUS_INVALID_PARAMETER;
    }

    if (OutResourceHandle != NULL)
        *OutResourceHandle = CreateArgs.hResource;

    /* -----------------------------------------------------------------------
     * Step 2: Allocate DXGKVMM_ALLOCATION tracking object.
     * Pool tag 'AlxD' — displayed as 'DxlA' in pool-tag dumps.
     * ----------------------------------------------------------------------- */
    Alloc = (PDXGKVMM_ALLOCATION)ExAllocatePoolWithTag(
                NonPagedPool,
                sizeof(DXGKVMM_ALLOCATION),
                TAG_VIDMM_ALLOC);

    if (Alloc == NULL)
    {
        DPRINT1("DxgkVidMmCreateAllocation: cannot allocate tracking "
                "object\n");

        /* Clean up miniport state. */
        if (Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation != NULL &&
            AllocInfo->hAllocation != NULL)
        {
            DXGKARG_DESTROYALLOCATION DestroyArgs;
            RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
            DestroyArgs.NumAllocations             = 1;
            DestroyArgs.phAllocation               = &AllocInfo->hAllocation;
            DestroyArgs.hResource                  = CreateArgs.hResource;
            DestroyArgs.Flags.DestroyResource      = CreateFlags.Resource;

            Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation(
                Adapter->MiniportDeviceContext,
                &DestroyArgs);
        }

        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(Alloc, sizeof(DXGKVMM_ALLOCATION));
    DxgkpVidMmInitializeAllocationLifetime(Alloc);

    Alloc->Size               = AllocInfo->Size;
    Alloc->Alignment          = (AllocInfo->Alignment != 0)
                                ? (SIZE_T)AllocInfo->Alignment
                                : PAGE_SIZE;
    Alloc->AllocationPriority = AllocInfo->AllocationPriority
                                ? AllocInfo->AllocationPriority
                                : VIDMM_PRIORITY_NORMAL;
    Alloc->CpuVisible         = (AllocInfo->Flags.CpuVisible != 0);
    Alloc->MiniportHandle     = AllocInfo->hAllocation;
    Alloc->Resident           = FALSE;
    Alloc->Adapter            = Adapter;
    Alloc->Device             = Device;
    Alloc->MiniportDeviceHandle = Device != NULL ? Device->hMiniportDevice : NULL;
    Alloc->Resource           = NULL;
    ExInitializeFastMutex(&Alloc->UserModeLock);

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

    InitializeListHead(&Alloc->SegmentEntry);
    InitializeListHead(&Alloc->DeviceEntry);
    InitializeListHead(&Alloc->GlobalAllocationEntry);
    InitializeListHead(&Alloc->ResourceEntry);

    DxgkpVidMmEnsureGlobalsInitialized();
    Alloc->Handle = DxgkpVidMmAllocateHandle(&DxgkVidMmNextAllocationHandle,
                                             DxgkVidMmAllocationHandleCookie);
    Alloc->Magic  = DXGKVMM_ALLOCATION_MAGIC;

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
        SIZE_T AllocSize = (Alloc->Size + PAGE_SIZE - 1) & ~(SIZE_T)(PAGE_SIZE - 1);

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

            DxgkpVidMmReleaseSegmentPlacement(Alloc);

            if (Alloc->PrivateDriverData != NULL)
                ExFreePoolWithTag(Alloc->PrivateDriverData, TAG_VIDMM_ALLOC);

            /* Notify miniport of cancellation. */
            if (Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation != NULL &&
                AllocInfo->hAllocation != NULL)
            {
                DXGKARG_DESTROYALLOCATION DestroyArgs;
                RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
                DestroyArgs.NumAllocations        = 1;
                DestroyArgs.phAllocation          = &AllocInfo->hAllocation;
                DestroyArgs.hResource             = CreateArgs.hResource;
                DestroyArgs.Flags.DestroyResource = CreateFlags.Resource;

                Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation(
                    Adapter->MiniportDeviceContext,
                    &DestroyArgs);
            }

            Alloc->Magic = 0;
            ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);
            return STATUS_NO_MEMORY;
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

    DxgkpVidMmTrackBacking(Adapter);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    InsertTailList(&DxgkVidMmAllocationListHead, &Alloc->GlobalAllocationEntry);
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    *OutHandle = (HANDLE)(ULONG_PTR)Alloc->Handle;

    DPRINT("DxgkVidMmCreateAllocation: handle=%p size=%Iu resident=%d\n",
           *OutHandle, Alloc->Size, Alloc->Resident);

    return STATUS_SUCCESS;

FailTrackedAllocation:
    if (Alloc->PrivateDriverData != NULL)
        ExFreePoolWithTag(Alloc->PrivateDriverData, TAG_VIDMM_ALLOC);

    Alloc->Magic = 0;
    ExFreePoolWithTag(Alloc, TAG_VIDMM_ALLOC);

    if (Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation != NULL &&
        AllocInfo->hAllocation != NULL)
    {
        DXGKARG_DESTROYALLOCATION DestroyArgs;
        RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
        DestroyArgs.NumAllocations        = 1;
        DestroyArgs.phAllocation          = &AllocInfo->hAllocation;
        DestroyArgs.hResource             = CreateArgs.hResource;
        DestroyArgs.Flags.DestroyResource = CreateFlags.Resource;

        Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation(
            Adapter->MiniportDeviceContext,
            &DestroyArgs);
    }

    return Status;
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
    ExInitializeFastMutex(&Alloc->UserModeLock);

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
DxgkpVidMmDetachAllocation(
    _In_opt_ PDXGKRNL_ADAPTER ExpectedAdapter,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _In_opt_ PDXGKVMM_RESOURCE ExpectedResource,
    _In_ HANDLE AllocationHandle)
{
    PDXGKVMM_ALLOCATION Allocation;
    PDXGKVMM_RESOURCE Resource;
    NTSTATUS Status = STATUS_SUCCESS;

    if (AllocationHandle == NULL)
        return STATUS_INVALID_HANDLE;

    DxgkpVidMmEnsureGlobalsInitialized();
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    Allocation = DxgkpVidMmLookupAllocationLocked((D3DKMT_HANDLE)(ULONG_PTR)AllocationHandle);
    if (Allocation == NULL || (ExpectedAdapter != NULL && Allocation->Adapter != ExpectedAdapter) || (ExpectedDevice != NULL && Allocation->Device != ExpectedDevice) || (ExpectedResource == NULL && Allocation->Resource != NULL) || (ExpectedResource != NULL && Allocation->Resource != ExpectedResource) || InterlockedCompareExchange(&Allocation->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
        return STATUS_INVALID_HANDLE;
    }
    InterlockedIncrement(&Allocation->ReferenceCount);
    Resource = Allocation->Resource;
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);

    ExAcquireFastMutex(&DxgkVidMmResourceListLock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    if (DxgkpVidMmLookupAllocationLocked((D3DKMT_HANDLE)(ULONG_PTR)AllocationHandle) != Allocation || (ExpectedResource == NULL && Allocation->Resource != NULL) || (ExpectedResource != NULL && Allocation->Resource != ExpectedResource) || InterlockedCompareExchange(&Allocation->Destroying, 1, 0) != 0)
        Status = STATUS_INVALID_HANDLE;
    else
    {
        RemoveEntryList(&Allocation->GlobalAllocationEntry);
        InitializeListHead(&Allocation->GlobalAllocationEntry);
        if (Resource != NULL && Resource->AllocationCount != 0)
            Resource->AllocationCount--;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    ExReleaseFastMutex(&DxgkVidMmResourceListLock);
    if (NT_SUCCESS(Status))
    {
        DxgkpVidMmCloseAllocationBinding(Allocation);
        DxgkpVidMmDropAllocationHandleReference(Allocation);
    }
    DxgkVidMmDereferenceAllocation(Allocation);
    return Status;
}

static NTSTATUS
DxgkpVidMmDestroyAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE ExpectedDevice,
    _In_opt_ PDXGKVMM_RESOURCE ExpectedResource,
    _In_ HANDLE AllocationHandle)
{
    NTSTATUS Status;

    DPRINT("DxgkVidMmDestroyAllocation: Adapter=%p Handle=%p\n", Adapter, AllocationHandle);
    Status = DxgkpVidMmDetachAllocation(Adapter, ExpectedDevice, ExpectedResource, AllocationHandle);
    if (!NT_SUCCESS(Status))
        DPRINT1("DxgkVidMmDestroyAllocation: invalid handle %p\n", AllocationHandle);
    return Status;
}

NTSTATUS
DxgkVidMmDestroyAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE AllocationHandle)
{
    return DxgkpVidMmDestroyAllocation(Adapter, NULL, NULL, AllocationHandle);
}

static NTSTATUS
DxgkpVidMmOpenCreatorAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_CREATEALLOCATION *CreateAllocation)
{
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    DXGK_OPENALLOCATIONINFO *OpenInfo = NULL;
    PHANDLE CloseHandles = NULL;
    PVOID ResourcePrivateData = NULL;
    DXGKARG_OPENALLOCATION OpenArgs;
    DXGKARG_CLOSEALLOCATION CloseArgs;
    UINT AllocationCount;
    UINT CloseCount = 0;
    UINT Index;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Adapter == NULL || Device == NULL || CreateAllocation == NULL)
        return STATUS_INVALID_PARAMETER;
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
    if (Allocations == NULL || OpenInfo == NULL || CloseHandles == NULL)
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
        D3DDDI_ALLOCATIONINFO *AllocationInfo = &CreateAllocation->pAllocationInfo[Index];
        HANDLE AllocationHandle = (HANDLE)(ULONG_PTR)AllocationInfo->hAllocation;

        Status = DxgkVidMmReferenceAllocation(AllocationHandle, Adapter, Device, &Allocations[Index]);
        if (!NT_SUCCESS(Status) || Allocations[Index]->BackingAllocation != NULL || Allocations[Index]->PrivateDriverDataSize != AllocationInfo->PrivateDriverDataSize)
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        OpenInfo[Index].hAllocation = AllocationInfo->hAllocation;
        OpenInfo[Index].pPrivateDriverData = AllocationInfo->pPrivateDriverData;
        OpenInfo[Index].PrivateDriverDataSize = AllocationInfo->PrivateDriverDataSize;
    }

    RtlZeroMemory(&OpenArgs, sizeof(OpenArgs));
    OpenArgs.NumAllocations = AllocationCount;
    OpenArgs.pOpenAllocation = OpenInfo;
    OpenArgs.pPrivateDriverData = ResourcePrivateData;
    OpenArgs.PrivateDriverSize = CreateAllocation->PrivateDriverDataSize;
    OpenArgs.Flags.Create = 1;
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation)(Device->hMiniportDevice, &OpenArgs);
    DxgkReleaseMiniportCallback(Adapter);
    if (!NT_SUCCESS(Status))
        goto RollbackOpen;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (OpenInfo[Index].hDeviceSpecificAllocation == NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            goto RollbackOpen;
        }
    }

    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (DxgkpVidMmLookupAllocationLocked(OpenInfo[Index].hAllocation) != Allocations[Index] || InterlockedCompareExchange(&Allocations[Index]->Destroying, 0, 0) != 0 || Allocations[Index]->OpenBindingHandle != NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            break;
        }
    }
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < AllocationCount; ++Index)
            Allocations[Index]->OpenBindingHandle = OpenInfo[Index].hDeviceSpecificAllocation;
    }
    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    if (!NT_SUCCESS(Status))
        goto RollbackOpen;

    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (Allocations[Index]->PrivateDriverDataSize != 0)
            RtlCopyMemory(Allocations[Index]->PrivateDriverData, OpenInfo[Index].pPrivateDriverData, Allocations[Index]->PrivateDriverDataSize);
    }
    goto Cleanup;

RollbackOpen:
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        if (OpenInfo[Index].hDeviceSpecificAllocation != NULL)
            CloseHandles[CloseCount++] = OpenInfo[Index].hDeviceSpecificAllocation;
    }
    if (CloseCount != 0 && DxgkAcquireMiniportCallback(Adapter))
    {
        RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
        CloseArgs.NumAllocations = CloseCount;
        CloseArgs.pOpenHandleList = CloseHandles;
        DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(Device->hMiniportDevice, &CloseArgs);
        DxgkReleaseMiniportCallback(Adapter);
    }

Cleanup:
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
    return Status;
}

NTSTATUS
DxgkCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *pCreateAllocation)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKVMM_RESOURCE Resource = NULL;
    DXGK_CREATEALLOCATIONFLAGS CreateFlags;
    D3DKMT_HANDLE InputResourceHandle;
    BOOLEAN CreatedResource = FALSE;
    BOOLEAN ResourceReferenced = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT i;

    PAGED_CODE();

    if (pCreateAllocation == NULL ||
        pCreateAllocation->NumAllocations == 0 ||
        pCreateAllocation->pAllocationInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InputResourceHandle = pCreateAllocation->hResource;
    if (pCreateAllocation->Flags.CreateResource && InputResourceHandle != 0)
        return STATUS_INVALID_PARAMETER;

    if (pCreateAllocation->Flags.RestrictSharedAccess ||
        pCreateAllocation->Flags.CreateProtected ||
        pCreateAllocation->Flags.NonSecure)
    {
        DPRINT1("DxgkCreateAllocation: unsupported create flags 0x%08x\n",
                (pCreateAllocation->Flags.RestrictSharedAccess << 4) |
                (pCreateAllocation->Flags.CreateProtected << 3) |
                (pCreateAllocation->Flags.NonSecure << 2));
        return STATUS_NOT_SUPPORTED;
    }

    if (pCreateAllocation->Flags.CreateShared &&
        !pCreateAllocation->Flags.CreateResource)
    {
        DPRINT1("DxgkCreateAllocation: shared allocations require CreateResource\n");
        return STATUS_NOT_SUPPORTED;
    }

    /*
     * Flags the D3DKMT contract documents as "Cannot be used when allocation is
     * created from the user mode".  Every D3DKMT path in this stack originates
     * in user mode (gdi32 -> win32k -> bridge), so these are always illegal here
     * and must be refused before reaching the miniport.
     */
    if (pCreateAllocation->Flags.ExistingSysMem ||
        pCreateAllocation->Flags.CreateWriteCombined ||
        pCreateAllocation->Flags.CreateCached ||
        pCreateAllocation->Flags.OpenCrossAdapter)
    {
        DPRINT1("DxgkCreateAllocation: user-mode-forbidden flags 0x%08x\n",
                *(const UINT *)&pCreateAllocation->Flags);
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * StandardAllocation requests carry a D3DKMT_CREATESTANDARDALLOCATION via
     * the pStandardAllocation union member.  The StandardAllocation flag bit
     * (0x00010000) is only a *named* bitfield from WDDM 2.3; at the pinned
     * WDDM2_0 ABI it lives inside Reserved, so test it through the raw flags
     * word.  The descriptor must be present and carry a valid allocation type.
     * The bridge only marshals this pointer when PrivateDriverDataSize != 0, so
     * it can still be an unmarshalled user pointer — read its type under SEH.
     */
    if ((*(const UINT *)&pCreateAllocation->Flags) & 0x00010000u)
    {
        D3DKMT_CREATESTANDARDALLOCATION *StdAlloc =
            pCreateAllocation->pStandardAllocation;
        D3DKMT_STANDARDALLOCATIONTYPE StdType = D3DKMT_STANDARDALLOCATIONTYPE_MAX;

        if (StdAlloc == NULL)
            return STATUS_INVALID_PARAMETER;

        _SEH2_TRY
        {
            StdType = StdAlloc->Type;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
        }
        _SEH2_END;

        if (StdType < D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP ||
            StdType >= D3DKMT_STANDARDALLOCATIONTYPE_MAX)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    Device = DxgkpVidMmFindDeviceByHandle(pCreateAllocation->hDevice, &Adapter);
    if (Device == NULL)
        return STATUS_INVALID_HANDLE;

    if (pCreateAllocation->hResource != 0)
    {
        Status = DxgkVidMmReferenceResource(pCreateAllocation->hResource, FALSE, Device, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        ResourceReferenced = TRUE;
        if (Resource->BackingResource != NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
    }

    RtlZeroMemory(&CreateFlags, sizeof(CreateFlags));
    CreateFlags.Resource = pCreateAllocation->Flags.CreateResource ? 1 : 0;
    CreateFlags.Shared   = pCreateAllocation->Flags.CreateShared ? 1 : 0;

    pCreateAllocation->hGlobalShare = 0;
    pCreateAllocation->hResource    = InputResourceHandle;

    for (i = 0; i < pCreateAllocation->NumAllocations; ++i)
    {
        D3DDDI_ALLOCATIONINFO AllocationInfo;
        DXGK_ALLOCATIONINFO DxgkAllocInfo;
        HANDLE AllocationHandle = NULL;
        HANDLE MiniportResourceHandle = NULL;
        PDXGKVMM_ALLOCATION TrackedAlloc;
        BOOLEAN UseSystemAllocation = FALSE;

        RtlZeroMemory(&DxgkAllocInfo, sizeof(DxgkAllocInfo));
        _SEH2_TRY
        {
            AllocationInfo = pCreateAllocation->pAllocationInfo[i];
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            break;
        }
        _SEH2_END;

        DxgkAllocInfo.pPrivateDriverData    = AllocationInfo.pPrivateDriverData;
        DxgkAllocInfo.PrivateDriverDataSize = AllocationInfo.PrivateDriverDataSize;
        DxgkAllocInfo.Flags.ExistingSysMem  =
            (pCreateAllocation->Flags.ExistingSysMem ||
             AllocationInfo.pSystemMem != NULL) ? 1 : 0;

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
                    DxgkAllocInfo.Size = (SIZE_T)Dimensions[0] * Dimensions[1] * ((Dimensions[2] + 7) / 8);
                    UseSystemAllocation = TRUE;
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
             Adapter->MiniportContext->InitData.s.DxgkDdiCreateAllocation == NULL))
        {
            DxgkAllocInfo.Flags.CpuVisible = 1;
            Status = DxgkpVidMmCreateSystemAllocation(Adapter, Device, &DxgkAllocInfo, &AllocationHandle);
            MiniportResourceHandle = NULL;
        }
        else
        {
            Status = STATUS_NOT_SUPPORTED;
        }

        if (Status == STATUS_NOT_SUPPORTED)
        {
            Status = DxgkVidMmCreateAllocation(Adapter, Device, &DxgkAllocInfo, pCreateAllocation->pPrivateDriverData, pCreateAllocation->PrivateDriverDataSize, Resource ? Resource->MiniportHandle : NULL, CreateFlags, &AllocationHandle, &MiniportResourceHandle);
        }
        if (!NT_SUCCESS(Status))
            break;

        Status = DxgkVidMmReferenceAllocation(AllocationHandle, Adapter, Device, &TrackedAlloc);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INTERNAL_ERROR;
            DxgkpVidMmDestroyAllocation(Adapter, Device, NULL, AllocationHandle);
            break;
        }

        if (CreateFlags.Resource && Resource == NULL)
        {
            Resource = DxgkVidMmCreateResourceWrapper(Adapter, Device, MiniportResourceHandle, 0, pCreateAllocation->Flags.CreateShared != 0, pCreateAllocation->pPrivateRuntimeData, pCreateAllocation->PrivateRuntimeDataSize, pCreateAllocation->pPrivateDriverData, pCreateAllocation->PrivateDriverDataSize);
            if (Resource == NULL)
            {
                if (MiniportResourceHandle != NULL && DxgkAcquireMiniportCallback(Adapter))
                {
                    DXGKARG_DESTROYALLOCATION DestroyArgs;
                    RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
                    DestroyArgs.hResource             = MiniportResourceHandle;
                    DestroyArgs.Flags.DestroyResource = 1;
                    if (Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation != NULL)
                        Adapter->MiniportContext->InitData.s.DxgkDdiDestroyAllocation(Adapter->MiniportDeviceContext, &DestroyArgs);
                    DxgkReleaseMiniportCallback(Adapter);
                }
                DxgkVidMmDereferenceAllocation(TrackedAlloc);
                DxgkpVidMmDestroyAllocation(Adapter, Device, NULL, AllocationHandle);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            CreatedResource = TRUE;

            pCreateAllocation->hResource = Resource->Handle;
            if (pCreateAllocation->Flags.CreateShared)
            {
                pCreateAllocation->hGlobalShare = Resource->GlobalShareHandle;
                ASSERT(pCreateAllocation->hGlobalShare != 0);
            }
            CreateFlags.Resource = 0;
        }

        if (Resource != NULL)
        {
            Status = DxgkVidMmAttachAllocationToResource(Resource, TrackedAlloc);
            if (!NT_SUCCESS(Status))
            {
                DxgkVidMmDereferenceAllocation(TrackedAlloc);
                DxgkpVidMmDestroyAllocation(Adapter, Device, NULL, AllocationHandle);
                break;
            }
        }

        _SEH2_TRY
        {
            pCreateAllocation->pAllocationInfo[i].hAllocation =
                (D3DKMT_HANDLE)(ULONG_PTR)AllocationHandle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        DxgkVidMmDereferenceAllocation(TrackedAlloc);

        if (!NT_SUCCESS(Status))
        {
            DxgkpVidMmDestroyAllocation(Adapter, Device, Resource, AllocationHandle);
            break;
        }
    }

    if (NT_SUCCESS(Status) && i == pCreateAllocation->NumAllocations)
        Status = DxgkpVidMmOpenCreatorAllocations(Adapter, Device, pCreateAllocation);

    if (!NT_SUCCESS(Status))
    {
        while (i-- > 0)
        {
            HANDLE AllocationHandle;

            AllocationHandle = (HANDLE)(ULONG_PTR)pCreateAllocation->pAllocationInfo[i].hAllocation;
            if (AllocationHandle != NULL)
            {
                DxgkpVidMmDestroyAllocation(Adapter, Device, Resource, AllocationHandle);
                pCreateAllocation->pAllocationInfo[i].hAllocation = 0;
            }
        }

        if (CreatedResource && Resource != NULL && Resource->AllocationCount == 0)
            DxgkpVidMmDestroyResourceWrapper(Adapter, Resource);

        pCreateAllocation->hResource = 0;
    }

    if (ResourceReferenced)
        DxgkVidMmDereferenceResource(Resource);

Cleanup:
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
DxgkDestroyAllocation(
    _In_ CONST D3DKMT_DESTROYALLOCATION *pDestroyAllocation)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT i;

    PAGED_CODE();

    if (pDestroyAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pDestroyAllocation->hResource == 0 && pDestroyAllocation->AllocationCount != 0 && pDestroyAllocation->phAllocationList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Device = DxgkpVidMmFindDeviceByHandle(pDestroyAllocation->hDevice, &Adapter);
    if (Device == NULL)
        return STATUS_INVALID_HANDLE;

    if (pDestroyAllocation->hResource != 0)
    {
        Status = DxgkVidMmReferenceResource(pDestroyAllocation->hResource, FALSE, Device, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
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

    for (i = 0; i < pDestroyAllocation->AllocationCount; ++i)
    {
        HANDLE AllocationHandle;

        AllocationHandle = (HANDLE)(ULONG_PTR)pDestroyAllocation->phAllocationList[i];
        Status = DxgkpVidMmDestroyAllocation(Adapter, Device, NULL, AllocationHandle);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

Cleanup:
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}


/* SEGMENT PLACEMENT **********************************************************/

/*
 * DxgkVidMmTryPlaceInSegment
 *
 * First-fit allocator for a single segment.
 *
 * We maintain Segment->AllocationList in ascending SegmentOffset order.
 * The algorithm scans the list and checks:
 *   a) Gap before the first allocation.
 *   b) Gap between consecutive allocations.
 *   c) Gap after the last allocation (to end of segment).
 *
 * ExAcquireFastMutex disables APCs and provides mutual exclusion at
 * PASSIVE_LEVEL without raising IRQL to DISPATCH_LEVEL.
 *
 * InterlockedAdd64 updates UsedSize atomically on x86-64 without needing
 * an explicit lock (the LOCK prefix provides full sequential consistency on
 * the cache line containing UsedSize via the MESI protocol).
 */
NTSTATUS
DxgkVidMmTryPlaceInSegment(
    _In_ PDXGKRNL_SEGMENT       Segment,
    _In_ PDXGKVMM_ALLOCATION    Allocation)
{
    ULONGLONG       AlignedSize;
    ULONGLONG       CandidateOffset;
    BOOLEAN         Found;
    NTSTATUS        Status;
    PLIST_ENTRY     Entry;

    ASSERT(Segment != NULL);
    ASSERT(Allocation != NULL);
    ASSERT(Allocation->Size != 0);
    ASSERT(Allocation->Alignment != 0);

    /*
     * Round the allocation size up to its own alignment boundary.
     * This ensures that the region we reserve in the segment is aligned,
     * which keeps subsequent first-fit iterations simple.
     */
    AlignedSize = VidMmAlignUp((ULONGLONG)Allocation->Size,
                               (ULONGLONG)Allocation->Alignment);

    if (AlignedSize > Segment->Size)
        return STATUS_NO_MEMORY;

    ExAcquireFastMutex(&Segment->Lock);

    Found           = FALSE;
    CandidateOffset = 0;
    Status          = STATUS_NO_MEMORY;

    /* Quick capacity check before the O(n) list walk. */
    if ((ULONGLONG)Segment->UsedSize + AlignedSize > Segment->Size)
    {
        ExReleaseFastMutex(&Segment->Lock);
        return STATUS_NO_MEMORY;
    }

    /* Virgin-VA-first: place above the high-water mark while it lasts. */
    {
        ULONGLONG Bump = VidMmAlignUp(Segment->BumpOffset,
                                      (ULONGLONG)Allocation->Alignment);

        if (Bump + AlignedSize <= Segment->Size)
        {
            CandidateOffset = Bump;
            Segment->BumpOffset = Bump + AlignedSize;
            Found = TRUE;
        }
    }

    /*
     * First-fit search: advance CandidateOffset past each existing
     * allocation until a gap large enough for AlignedSize is found.
     */
    if (!Found)
    {
        for (Entry = Segment->AllocationList.Flink;
             Entry != &Segment->AllocationList;
             Entry = Entry->Flink)
        {
            PDXGKVMM_ALLOCATION Existing;
            ULONGLONG           ExistStart;
            ULONGLONG           AlignedCandidate;
            ULONGLONG           NeededEnd;

            Existing = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, SegmentEntry);

            ExistStart      = Existing->SegmentOffset;
            AlignedCandidate = VidMmAlignUp(CandidateOffset,
                                            (ULONGLONG)Allocation->Alignment);
            NeededEnd        = AlignedCandidate + AlignedSize;

            if (NeededEnd <= ExistStart)
            {
                CandidateOffset = AlignedCandidate;
                Found           = TRUE;
                break;
            }

            /* Advance past this allocation. */
            CandidateOffset = Existing->SegmentOffset
                              + VidMmAlignUp((ULONGLONG)Existing->Size,
                                             (ULONGLONG)Existing->Alignment);
        }
    }

    if (!Found)
    {
        /* Check trailing gap after last allocation (or entire segment if empty). */
        ULONGLONG AlignedCandidate = VidMmAlignUp(CandidateOffset,
                                                  (ULONGLONG)Allocation->Alignment);
        ULONGLONG NeededEnd        = AlignedCandidate + AlignedSize;

        if (NeededEnd <= Segment->Size)
        {
            CandidateOffset = AlignedCandidate;
            Found           = TRUE;
        }
    }

    if (!Found)
    {
        ExReleaseFastMutex(&Segment->Lock);
        return STATUS_NO_MEMORY;
    }

    /* Commit the placement. */
    Allocation->SegmentId     = Segment->SegmentId;
    Allocation->SegmentOffset = CandidateOffset;

    /*
     * Physical address of this placement.
     *
     * VRAM: BaseAddress.QuadPart + offset gives the CPU/GPU physical address.
     * Aperture: BaseAddress is typically zero; the GPU maps system memory
     *           pages here via the aperture's IOMMU.  The QuadPart value
     *           serves as a logical segment address for paging operations.
     *
     * We add with LONGLONG arithmetic to avoid unsigned overflow on
     * 64-bit physical addresses larger than 2^63 bytes.
     */
    Allocation->PhysicalAddress.QuadPart =
        Segment->BaseAddress.QuadPart + (LONGLONG)CandidateOffset;

    Allocation->Resident = TRUE;

    /*
     * Insert into the sorted allocation list, maintaining ascending
     * SegmentOffset order.  We scan from the tail (most recently appended =
     * highest offset) which is O(1) for the common sequential-append case.
     */
    {
        PLIST_ENTRY InsertBefore = &Segment->AllocationList;

        for (Entry = Segment->AllocationList.Blink;
             Entry != &Segment->AllocationList;
             Entry = Entry->Blink)
        {
            PDXGKVMM_ALLOCATION Existing =
                CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, SegmentEntry);

            if (Existing->SegmentOffset <= CandidateOffset)
            {
                InsertBefore = Entry->Flink;
                break;
            }
        }

        InsertTailList(InsertBefore, &Allocation->SegmentEntry);
    }

    /*
     * Update UsedSize with a locked add.  On x86-64 the LOCK prefix ensures
     * the RMW is atomic and fully ordered with respect to other processors,
     * without needing a separate fence instruction.
     */
    (VOID)InterlockedAdd64(&Segment->UsedSize, (LONGLONG)AlignedSize);

    Status = STATUS_SUCCESS;

    ExReleaseFastMutex(&Segment->Lock);

    DPRINT("DxgkVidMmTryPlaceInSegment: alloc %p placed in seg %lu "
           "offset=0x%I64x size=0x%I64x (aligned)\n",
           Allocation, Segment->SegmentId, CandidateOffset, AlignedSize);

    return Status;
}

static VOID
DxgkpVidMmReleaseSegmentPlacement(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment;
    ULONGLONG AlignedSize;

    if (Allocation == NULL || !Allocation->Resident)
        return;

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
    AlignedSize = VidMmAlignUp((ULONGLONG)Allocation->Size,
                               (ULONGLONG)Allocation->Alignment);

    ExAcquireFastMutex(&Segment->Lock);
    RemoveEntryList(&Allocation->SegmentEntry);
    InitializeListHead(&Allocation->SegmentEntry);
    (VOID)InterlockedAdd64(&Segment->UsedSize, -(LONGLONG)AlignedSize);
    ExReleaseFastMutex(&Segment->Lock);

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
NTSTATUS
DxgkVidMmEvict(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER    Adapter;
    PDXGKRNL_SEGMENT    Segment;
    ULONGLONG           AlignedSize;

    DPRINT("DxgkVidMmEvict: Alloc=%p seg=%lu offset=0x%I64x\n",
           Allocation, Allocation->SegmentId, Allocation->SegmentOffset);

    if (!Allocation->Resident)
    {
        DPRINT1("DxgkVidMmEvict: allocation %p is not resident\n",
                Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = Allocation->Adapter;

    if (Adapter == NULL || Adapter->Segments == NULL)
    {
        DPRINT1("DxgkVidMmEvict: null adapter or segment table for "
                "allocation %p\n", Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    if (Allocation->SegmentId < 1 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        DPRINT1("DxgkVidMmEvict: SegmentId %lu out of range for "
                "allocation %p\n", Allocation->SegmentId, Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];

    /* A user-mapped allocation cannot move: the UMD holds the pointer and
     * keeps writing through it; unmapping here leaves it dangling and the
     * GPU address it baked into command streams stale. */
    if (Allocation->UserModeAddress != NULL)
    {
        DPRINT1("DxgkVidMmEvict: refusing user-mapped alloc %p (seg %lu off 0x%I64x)\n",
                Allocation, Allocation->SegmentId, Allocation->SegmentOffset);
        return STATUS_DEVICE_BUSY;
    }

    if (VidMmSegmentIsAperture(Segment))
        DxgkpVidMmReleaseApertureMapping(Allocation);

    /* Unmap CPU access first (before we invalidate the segment offset). */
    if (Allocation->CpuAddress != NULL &&
        Allocation->CpuAddress != Allocation->SystemMemory)
    {
        DxgkVidMmUnmapAllocationCpu(Allocation);
    }

    /* Allocate system-memory backing if not already present. */
    if (Allocation->SystemMemory == NULL)
    {
        SIZE_T BackingSize = (Allocation->Size + PAGE_SIZE - 1) &
                             ~(SIZE_T)(PAGE_SIZE - 1);

        Allocation->SystemMemory = ExAllocatePoolWithTag(
                                       NonPagedPool,
                                       BackingSize,
                                       TAG_VIDMM_ALLOC);

        if (Allocation->SystemMemory == NULL)
        {
            DPRINT1("DxgkVidMmEvict: cannot allocate backing store "
                    "for allocation %p (%Iu bytes)\n",
                    Allocation, BackingSize);
            return STATUS_NO_MEMORY;
        }

        RtlZeroMemory(Allocation->SystemMemory, BackingSize);
    }

    /*
     * Preserve the allocation's content across the eviction (documented
     * VidMm behavior): download segment content into the backing store
     * while the placement is still valid.
     */
    DxgkpVidMmTransferAllocationContent(Adapter, Allocation, FALSE);

    /* Remove allocation from segment list and update accounting. */
    AlignedSize = VidMmAlignUp((ULONGLONG)Allocation->Size,
                               (ULONGLONG)Allocation->Alignment);

    ExAcquireFastMutex(&Segment->Lock);

    RemoveEntryList(&Allocation->SegmentEntry);
    InitializeListHead(&Allocation->SegmentEntry);

    (VOID)InterlockedAdd64(&Segment->UsedSize, -(LONGLONG)AlignedSize);

    ExReleaseFastMutex(&Segment->Lock);

    /* Update allocation residency state. */
    Allocation->Resident    = FALSE;
    Allocation->SegmentId   = 0;
    Allocation->SegmentOffset = 0;
    Allocation->PhysicalAddress = MmGetPhysicalAddress(Allocation->SystemMemory);

    DPRINT("DxgkVidMmEvict: alloc %p evicted to VA=%p PA=0x%I64x\n",
           Allocation, Allocation->SystemMemory,
           Allocation->PhysicalAddress.QuadPart);

    return STATUS_SUCCESS;
}


/*
 * VidMmFindLowestPriorityResident
 *
 * Scans Segment->AllocationList and returns the allocation with the lowest
 * AllocationPriority.  Returns NULL if the list is empty.
 *
 * Must be called with Segment->Lock held.
 */
static
PDXGKVMM_ALLOCATION
VidMmFindLowestPriorityResident(
    _In_ PDXGKRNL_SEGMENT Segment)
{
    PDXGKVMM_ALLOCATION Victim = NULL;
    PLIST_ENTRY         Entry;

    for (Entry = Segment->AllocationList.Flink;
         Entry != &Segment->AllocationList;
         Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Candidate =
            CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, SegmentEntry);

        if (Victim == NULL ||
            Candidate->AllocationPriority < Victim->AllocationPriority)
        {
            Victim = Candidate;
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
NTSTATUS
DxgkVidMmMakeResident(
    _In_ PDXGKVMM_ALLOCATION    Allocation,
    _In_ PDXGKRNL_ADAPTER       Adapter)
{
    PDXGKRNL_SEGMENT    Segments;
    NTSTATUS            Status;
    ULONG               i;
    BOOLEAN             Placed = FALSE;

    DPRINT("DxgkVidMmMakeResident: Alloc=%p size=%Iu\n",
           Allocation, Allocation->Size);

    if (Allocation == NULL || Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Allocation->Resident)
    {
        DPRINT1("DxgkVidMmMakeResident: alloc %p is already resident\n",
                Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    if (Adapter->Segments == NULL)
    {
        DPRINT1("DxgkVidMmMakeResident: adapter %p has no segment table\n",
                Adapter);
        return STATUS_NO_MEMORY;
    }

    Segments = ADAPTER_SEGMENTS(Adapter);

    /* Pass 1: try all non-aperture segments without eviction. */
    for (i = 0; i < Adapter->SegmentCount && !Placed; i++)
    {
        PDXGKRNL_SEGMENT Seg = &Segments[i];

        if (VidMmSegmentIsAperture(Seg))
            continue;

        Status = DxgkVidMmTryPlaceInSegment(Seg, Allocation);

        if (NT_SUCCESS(Status))
        {
            Placed = TRUE;
            DPRINT("DxgkVidMmMakeResident: placed in seg %lu offset=0x%I64x "
                   "(no eviction needed)\n",
                   Seg->SegmentId, Allocation->SegmentOffset);
        }
    }

    if (Placed)
    {
        /* Upload the preserved content into the new placement. */
        DxgkpVidMmTransferAllocationContent(Adapter, Allocation, TRUE);
        return STATUS_SUCCESS;
    }

    /* Pass 2: evict lowest-priority resident and retry. */
    for (i = 0; i < Adapter->SegmentCount && !Placed; i++)
    {
        PDXGKRNL_SEGMENT    Seg    = &Segments[i];
        PDXGKVMM_ALLOCATION Victim;

        if (VidMmSegmentIsAperture(Seg))
            continue;

        /* Find victim while holding the segment lock. */
        ExAcquireFastMutex(&Seg->Lock);
        Victim = VidMmFindLowestPriorityResident(Seg);
        ExReleaseFastMutex(&Seg->Lock);

        if (Victim == NULL)
            continue;

        if (Victim->AllocationPriority >= Allocation->AllocationPriority)
        {
            DPRINT("DxgkVidMmMakeResident: victim %p priority %lu >= "
                   "our priority %lu, skipping seg %lu\n",
                   Victim, Victim->AllocationPriority,
                   Allocation->AllocationPriority, Seg->SegmentId);
            continue;
        }

        DPRINT("DxgkVidMmMakeResident: evicting victim %p (priority %lu) "
               "from seg %lu to make room\n",
               Victim, Victim->AllocationPriority, Seg->SegmentId);

        Status = DxgkVidMmEvict(Victim);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkVidMmMakeResident: eviction of victim %p "
                    "failed 0x%08lx\n", Victim, Status);
            continue;
        }

        Status = DxgkVidMmTryPlaceInSegment(Seg, Allocation);

        if (NT_SUCCESS(Status))
        {
            Placed = TRUE;
            DPRINT("DxgkVidMmMakeResident: placed after eviction in "
                   "seg %lu offset=0x%I64x\n",
                   Seg->SegmentId, Allocation->SegmentOffset);
        }
    }

    if (!Placed)
    {
        DPRINT1("DxgkVidMmMakeResident: cannot make alloc %p resident — "
                "all segments full\n", Allocation);
        return STATUS_NO_MEMORY;
    }

    /* Upload the preserved content into the new placement. */
    DxgkpVidMmTransferAllocationContent(Adapter, Allocation, TRUE);

    return STATUS_SUCCESS;
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
    ASSERT(Segment->BaseAddress.QuadPart != 0);

    if (Segment->CpuBase != NULL)
        return STATUS_SUCCESS;  /* Already mapped (lazy init guard). */

    Segment->CpuBase = MmMapIoSpace(
                           Segment->BaseAddress,
                           (SIZE_T)Segment->Size,
                           MmWriteCombined);

    if (Segment->CpuBase == NULL)
    {
        DPRINT1("VidMmMapSegmentCpu: MmMapIoSpace failed for seg %lu "
                "(PA=0x%I64x size=0x%I64x)\n",
                Segment->SegmentId,
                Segment->BaseAddress.QuadPart,
                Segment->Size);
        return STATUS_NO_MEMORY;
    }

    DPRINT("VidMmMapSegmentCpu: seg %lu CPU VA=%p "
           "PA=0x%I64x size=0x%I64x\n",
           Segment->SegmentId, Segment->CpuBase,
           Segment->BaseAddress.QuadPart, Segment->Size);

    return STATUS_SUCCESS;
}

static VOID
DxgkpVidMmReleaseApertureMapping(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    DXGKARG_BUILDPAGINGBUFFER BuildArgs;
    PDXGKRNL_ADAPTER Adapter;

    if (Allocation == NULL)
        return;

    if (!Allocation->ApertureMapped && Allocation->ApertureMdl == NULL)
        return;

    Adapter = Allocation->Adapter;
    if (Allocation->ApertureMapped && Adapter != NULL && Allocation->SegmentId >= 1 && Allocation->SegmentId <= Adapter->SegmentCount && DxgkAcquireMiniportCallback(Adapter))
    {
        RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
        BuildArgs.Operation = DXGK_OPERATION_UNMAP_APERTURE_SEGMENT;
        BuildArgs.UnmapApertureSegment.hDevice = Allocation->MiniportDeviceHandle;
        BuildArgs.UnmapApertureSegment.hAllocation = Allocation->MiniportHandle;
        BuildArgs.UnmapApertureSegment.SegmentId = Allocation->SegmentId;
        BuildArgs.UnmapApertureSegment.OffsetInPages = (SIZE_T)(Allocation->SegmentOffset / PAGE_SIZE);
        BuildArgs.UnmapApertureSegment.NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Allocation->SystemMemory, Allocation->Size);
        if (Adapter->MiniportContext->InitData.s.DxgkDdiBuildPagingBuffer != NULL)
            Adapter->MiniportContext->InitData.s.DxgkDdiBuildPagingBuffer(Adapter->MiniportDeviceContext, &BuildArgs);
        DxgkReleaseMiniportCallback(Adapter);
    }

    if (Allocation->ApertureMdl != NULL)
    {
        IoFreeMdl(Allocation->ApertureMdl);
        Allocation->ApertureMdl = NULL;
    }

    Allocation->ApertureMapped = FALSE;
}

NTSTATUS
DxgkVidMmEnsureAllocationApertureMapped(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_SEGMENT Segment;
    DXGKARG_BUILDPAGINGBUFFER BuildArgs;
    SIZE_T NumberOfPages;
    PMDL Mdl;
    NTSTATUS Status;

    if (Allocation == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!Allocation->Resident)
        return STATUS_SUCCESS;

    Adapter = Allocation->Adapter;
    if (Adapter == NULL || Adapter->Segments == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Allocation->SegmentId < 1 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (!VidMmSegmentIsAperture(Segment))
        return STATUS_SUCCESS;

    if (Allocation->ApertureMapped)
        return STATUS_SUCCESS;

    if (Allocation->SystemMemory == NULL)
        return STATUS_INVALID_PARAMETER;

    if (((ULONG_PTR)Allocation->SystemMemory & (PAGE_SIZE - 1)) != 0)
    {
        DPRINT1("DxgkVidMmEnsureAllocationApertureMapped: non page-aligned backing %p\n",
                Allocation->SystemMemory);
    }

    NumberOfPages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(Allocation->SystemMemory, Allocation->Size);
    Mdl = IoAllocateMdl(Allocation->SystemMemory, (ULONG)ALIGN_UP_BY(Allocation->Size, PAGE_SIZE), FALSE, FALSE, NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    MmBuildMdlForNonPagedPool(Mdl);

    RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
    BuildArgs.Operation = DXGK_OPERATION_MAP_APERTURE_SEGMENT;
    BuildArgs.MapApertureSegment.hDevice = Allocation->MiniportDeviceHandle;
    BuildArgs.MapApertureSegment.hAllocation = Allocation->MiniportHandle;
    BuildArgs.MapApertureSegment.SegmentId = Allocation->SegmentId;
    BuildArgs.MapApertureSegment.OffsetInPages = (SIZE_T)(Allocation->SegmentOffset / PAGE_SIZE);
    BuildArgs.MapApertureSegment.NumberOfPages = NumberOfPages;
    BuildArgs.MapApertureSegment.pMdl = Mdl;
    BuildArgs.MapApertureSegment.MdlOffset = 0;

    DXGKRNL_VERBOSE("EnsureApertureMapped: calling BuildPagingBuffer MAP_APERTURE "
                    "hAlloc=%p SegId=%u Pages=%Iu\n",
                    Allocation->MiniportHandle, Allocation->SegmentId, NumberOfPages);

    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        IoFreeMdl(Mdl);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Adapter->MiniportContext->InitData.s.DxgkDdiBuildPagingBuffer != NULL)
        Status = Adapter->MiniportContext->InitData.s.DxgkDdiBuildPagingBuffer(Adapter->MiniportDeviceContext, &BuildArgs);
    else
        Status = STATUS_NOT_SUPPORTED;
    DxgkReleaseMiniportCallback(Adapter);

    DXGKRNL_VERBOSE("EnsureApertureMapped: BuildPagingBuffer returned 0x%08lX\n", Status);

    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        return Status;
    }

    Allocation->ApertureMdl = Mdl;
    Allocation->ApertureMapped = TRUE;
    return STATUS_SUCCESS;
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
 * preserves content the way Windows VidMm does.  Prefers the miniport's
 * documented BuildPagingBuffer(TRANSFER) operation and falls back to a
 * CPU copy through the segment's write-combined mapping.
 *
 * Must be called while the allocation's SegmentId/SegmentOffset placement
 * is still valid.
 */
static VOID
DxgkpVidMmTransferAllocationContent(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ BOOLEAN ToSegment)
{
    PDXGKRNL_SEGMENT Segment;
    DXGKARG_BUILDPAGINGBUFFER BuildArgs;
    PMDL Mdl;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    if (Adapter == NULL || Allocation == NULL ||
        Allocation->SystemMemory == NULL ||
        Allocation->Size == 0 ||
        Adapter->Segments == NULL ||
        Allocation->SegmentId < 1 ||
        Allocation->SegmentId > Adapter->SegmentCount)
    {
        return;
    }

    Segment = &ADAPTER_SEGMENTS(Adapter)[Allocation->SegmentId - 1];
    if (VidMmSegmentIsAperture(Segment))
        return; /* aperture placements alias the backing store directly */

    if (!DxgkAcquireMiniportCallback(Adapter))
        return;
    if (Adapter->MiniportContext->InitData.s.DxgkDdiBuildPagingBuffer != NULL)
    {
        Mdl = IoAllocateMdl(Allocation->SystemMemory, (ULONG)ALIGN_UP_BY(Allocation->Size, PAGE_SIZE), FALSE, FALSE, NULL);
        if (Mdl != NULL)
        {
            MmBuildMdlForNonPagedPool(Mdl);

            RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
            BuildArgs.Operation = DXGK_OPERATION_TRANSFER;
            BuildArgs.Transfer.hAllocation = Allocation->MiniportHandle;
            BuildArgs.Transfer.TransferSize = Allocation->Size;

            if (ToSegment)
            {
                BuildArgs.Transfer.Source.SegmentId = 0;
                BuildArgs.Transfer.Source.pMdl = Mdl;
                BuildArgs.Transfer.Destination.SegmentId = Allocation->SegmentId;
                BuildArgs.Transfer.Destination.SegmentAddress.QuadPart = (LONGLONG)Allocation->SegmentOffset;
            }
            else
            {
                BuildArgs.Transfer.Source.SegmentId = Allocation->SegmentId;
                BuildArgs.Transfer.Source.SegmentAddress.QuadPart = (LONGLONG)Allocation->SegmentOffset;
                BuildArgs.Transfer.Destination.SegmentId = 0;
                BuildArgs.Transfer.Destination.pMdl = Mdl;
            }

            Status = Adapter->MiniportContext->InitData.s.DxgkDdiBuildPagingBuffer(Adapter->MiniportDeviceContext, &BuildArgs);

            IoFreeMdl(Mdl);
        }
    }

    if (!NT_SUCCESS(Status) && VidMmSegmentIsCpuVisible(Segment))
    {
        /* Fallback: CPU copy through the segment's WC mapping. */
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

    DxgkReleaseMiniportCallback(Adapter);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("DxgkpVidMmTransferAllocationContent: %s failed 0x%08lx "
                "alloc=%p\n",
                ToSegment ? "upload" : "download", Status, Allocation);
    }
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

    ExAcquireFastMutex(&Allocation->UserModeLock);

    if (Allocation->UserModeAddress != NULL)
    {
        if (Allocation->UserModeProcess != Process)
        {
            DPRINT1("DxgkVidMmMapAllocationUser: alloc %p already mapped in process %p (current %p)\n",
                    Allocation, Allocation->UserModeProcess, Process);
            ExReleaseFastMutex(&Allocation->UserModeLock);
            return STATUS_DEVICE_BUSY;
        }

        Allocation->UserModeLockCount++;
        *OutVa = Allocation->UserModeAddress;
        ExReleaseFastMutex(&Allocation->UserModeLock);
        return STATUS_SUCCESS;
    }

    Status = DxgkpVidMmBuildAllocationUserMdl(Allocation,
                                              &Mdl,
                                              &UserOffset,
                                              &CacheType);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Allocation->UserModeLock);
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
        ExReleaseFastMutex(&Allocation->UserModeLock);
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

    ExReleaseFastMutex(&Allocation->UserModeLock);

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

    if (!Allocation->CpuVisible)
    {
        DPRINT1("DxgkVidMmMapAllocationCpu: alloc %p is not CPU-visible\n",
                Allocation);
        return STATUS_INVALID_PARAMETER;
    }

    /* Pre-mapped allocation (e.g., GOP framebuffer): CpuAddress is already set. */
    if (Allocation->CpuAddress != NULL)
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

    ExAcquireFastMutex(&Allocation->UserModeLock);

    if (Allocation->UserModeMapBase == NULL ||
        Allocation->UserModeAddress == NULL ||
        Allocation->UserModeMdl == NULL)
    {
        ExReleaseFastMutex(&Allocation->UserModeLock);
        return;
    }

    if (Allocation->UserModeLockCount > 1)
    {
        DXGKRNL_VERBOSE("DxgkVidMmUnmapAllocationUser: alloc=%p handle=0x%X deferred lockCount=%lu\n",
                      Allocation,
                      Allocation->Handle,
                      Allocation->UserModeLockCount);
        Allocation->UserModeLockCount--;
        ExReleaseFastMutex(&Allocation->UserModeLock);
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

    ExReleaseFastMutex(&Allocation->UserModeLock);

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

        ExAcquireFastMutex(&Alloc->UserModeLock);
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
        ExReleaseFastMutex(&Alloc->UserModeLock);

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
            if (((Device != NULL && Allocation->Device == Device) || (Device == NULL && Allocation->Adapter == Adapter)) && Allocation->Resource == NULL)
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

VOID
DxgkVidMmCleanupDeviceAllocations(
    _In_ PDXGKRNL_DEVICE Device)
{
    PAGED_CODE();
    if (Device != NULL && Device->Adapter != NULL)
        DxgkpVidMmCleanupAllocations(NULL, Device);
}

VOID
DxgkVidMmCleanupAdapterAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();
    if (Adapter != NULL)
        DxgkpVidMmCleanupAllocations(Adapter, NULL);
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
    PDXGKRNL_SEGMENT Segments;
    PLIST_ENTRY Entry;
    ULONG SegmentCount;
    ULONG Index;

    PAGED_CODE();
    if (Adapter == NULL || Process == NULL || Budget == NULL || CurrentUsage == NULL || CurrentReservation == NULL || AvailableForReservation == NULL)
        return STATUS_INVALID_PARAMETER;
    if (MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL && MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL)
        return STATUS_INVALID_PARAMETER;

    *Budget = 0;
    *CurrentUsage = 0;
    *CurrentReservation = 0;
    *AvailableForReservation = 0;
    Segments = ADAPTER_SEGMENTS(Adapter);
    SegmentCount = Adapter->SegmentCount;
    if (SegmentCount != 0 && Segments == NULL)
        return STATUS_DEVICE_NOT_READY;

    for (Index = 0; Index < SegmentCount; ++Index)
        ExAcquireFastMutex(&Segments[Index].Lock);
    ExAcquireFastMutex(&DxgkVidMmAllocationListLock);

    for (Index = 0; Index < SegmentCount; ++Index)
    {
        BOOLEAN IsNonLocal = VidMmSegmentIsAperture(&Segments[Index]);

        if ((MemorySegmentGroup == D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL) == IsNonLocal)
            DxgkpVidMmSaturatingAdd(Budget, Segments[Index].Size);
    }

    for (Entry = DxgkVidMmAllocationListHead.Flink; Entry != &DxgkVidMmAllocationListHead; Entry = Entry->Flink)
    {
        PDXGKVMM_ALLOCATION Allocation = CONTAINING_RECORD(Entry, DXGKVMM_ALLOCATION, GlobalAllocationEntry);
        BOOLEAN IsNonLocal;

        if (Allocation->Adapter != Adapter || Allocation->Device == NULL || Allocation->Device->OwnerProcess != Process)
            continue;
        if (Allocation->Resident)
        {
            if (Allocation->SegmentId == 0 || Allocation->SegmentId > SegmentCount)
                continue;
            IsNonLocal = VidMmSegmentIsAperture(&Segments[Allocation->SegmentId - 1]);
        }
        else
        {
            if (Allocation->SystemMemory == NULL)
                continue;
            IsNonLocal = TRUE;
        }
        if ((MemorySegmentGroup == D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL) == IsNonLocal)
            DxgkpVidMmSaturatingAdd(CurrentUsage, (UINT64)Allocation->Size);
    }

    ExReleaseFastMutex(&DxgkVidMmAllocationListLock);
    while (SegmentCount != 0)
        ExReleaseFastMutex(&Segments[--SegmentCount].Lock);

    /* Reservation changes are not implemented, so advertising zero is the
     * only truthful AvailableForReservation value. */
    return STATUS_SUCCESS;
}
