/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     CDD redirection surface synchronization
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "dxgkrnl_private.h"
#include "vidmm.h"
#include <reactos/dwmframe.h>

NTKERNELAPI VOID FASTCALL
ExfAcquirePushLockExclusive(_Inout_ PEX_PUSH_LOCK PushLock);
NTKERNELAPI VOID FASTCALL
ExfAcquirePushLockShared(_Inout_ PEX_PUSH_LOCK PushLock);
NTKERNELAPI VOID FASTCALL
ExfReleasePushLockExclusive(_Inout_ PEX_PUSH_LOCK PushLock);
NTKERNELAPI VOID FASTCALL
ExfReleasePushLockShared(_Inout_ PEX_PUSH_LOCK PushLock);

#define DXGKP_REDIRECTION_SYNC_MAX_SURFACES 1024u
#define DXGKP_REDIRECTION_SYNC_TIMEOUT_MS 2000u

typedef struct _DXGKP_REDIRECTION_SYNC_SNAPSHOT
{
    PDXGKVMM_ALLOCATION Allocation;
    ULONG Fences[DXGK_MAX_TRACKED_NODES];
    ULONGLONG CacheOffset;
    ULONGLONG CacheLength;
} DXGKP_REDIRECTION_SYNC_SNAPSHOT,
 *PDXGKP_REDIRECTION_SYNC_SNAPSHOT;

static EX_PUSH_LOCK DxgkpWin32kCddInterfaceLock;
static DXGKRNL_WIN32K_CDD_INTERFACE DxgkpWin32kCddInterface;

static BOOLEAN
DxgkpRedirectionFenceReached(
    _In_ ULONG CompletedFenceId,
    _In_ ULONG SubmissionFenceId)
{
    return (LONG)(CompletedFenceId - SubmissionFenceId) >= 0;
}

VOID
DxgkRedirectionInitialize(VOID)
{
    RtlZeroMemory(&DxgkpWin32kCddInterfaceLock,
                  sizeof(DxgkpWin32kCddInterfaceLock));
    RtlZeroMemory(&DxgkpWin32kCddInterface,
                  sizeof(DxgkpWin32kCddInterface));
}

NTSTATUS
DxgkRegisterWin32kCddInterface(
    _In_ const DXGKRNL_WIN32K_CDD_INTERFACE *Interface)
{
    ULONG RequiredSize;

    if (Interface == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Interface->Version == DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_1)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    AdmitRedirectedBltPresent);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_2)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    CancelRedirectedBltPresent);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_3)
    {
        RequiredSize = FIELD_OFFSET(DXGKRNL_WIN32K_CDD_INTERFACE,
                                    DispatchNtGdi);
    }
    else if (Interface->Version ==
             DXGKRNL_WIN32K_CDD_INTERFACE_VERSION_4)
    {
        RequiredSize = sizeof(*Interface);
    }
    else
    {
        return STATUS_REVISION_MISMATCH;
    }
    if (Interface->Size != RequiredSize)
        return STATUS_INFO_LENGTH_MISMATCH;

    KeEnterCriticalRegion();
    ExfAcquirePushLockExclusive(&DxgkpWin32kCddInterfaceLock);
    RtlZeroMemory(&DxgkpWin32kCddInterface,
                  sizeof(DxgkpWin32kCddInterface));
    RtlCopyMemory(&DxgkpWin32kCddInterface, Interface, RequiredSize);
    ExfReleasePushLockExclusive(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

ULONG_PTR
DxgkDispatchWin32kNtGdi(
    _In_ ULONG NativeOrdinal,
    _In_ ULONG_PTR Argument0,
    _In_ ULONG_PTR Argument1,
    _In_ ULONG_PTR Argument2,
    _In_ ULONG_PTR Argument3,
    _In_ ULONG_PTR Argument4)
{
    PDXGKENG_DISPATCH_NTGDI Callback;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.DispatchNtGdi;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();

    if (Callback == NULL)
        return (ULONG_PTR)STATUS_DEVICE_NOT_READY;
    return Callback(NativeOrdinal, Argument0, Argument1, Argument2, Argument3, Argument4);
}

NTSTATUS
DxgkAssociateRedirectionSurface(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ const DXGK_REDIRECTION_SURFACE_ASSOCIATE *Associate)
{
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_RESOURCE Resource = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Associate == NULL ||
        Associate->StructSize != sizeof(*Associate) ||
        Associate->Flags != 0 || Associate->AllocationHandle == 0 ||
        Associate->ResourceHandle == 0 || Associate->GlobalShare == 0 ||
        Associate->SurfaceHandle == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkVidMmReferenceResource(
                 Associate->ResourceHandle, FALSE, NULL, &Resource);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmReferenceAllocation(
                 (HANDLE)(ULONG_PTR)Associate->AllocationHandle,
                 Adapter, NULL, &Allocation);
    if (!NT_SUCCESS(Status))
    {
        DxgkVidMmDereferenceResource(Resource);
        return STATUS_INVALID_PARAMETER;
    }

    if (Resource->Adapter != Adapter ||
        Resource->GlobalShareHandle != Associate->GlobalShare ||
        Resource->AllocationCount != 1 || Allocation->Resource != Resource)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                    Executive, KernelMode, FALSE, NULL);
        if (Allocation->RedirectionSurfaceHandle != 0 &&
            Allocation->RedirectionSurfaceHandle !=
                (ULONG_PTR)Associate->SurfaceHandle)
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
        }
        else
        {
            InterlockedExchangePointer(
                (PVOID volatile *)&Allocation->RedirectionSurfaceHandle,
                (PVOID)(ULONG_PTR)Associate->SurfaceHandle);
            Status = STATUS_SUCCESS;
        }
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
    }

    DxgkVidMmDereferenceAllocation(Allocation);
    DxgkVidMmDereferenceResource(Resource);
    return Status;
}

VOID
DxgkPublishRedirectionPresent(
    _Inout_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    if (Allocation == NULL || SubmissionFenceId == 0 ||
        NodeOrdinal >= DXGK_MAX_TRACKED_NODES ||
        InterlockedCompareExchangePointer(
            (PVOID volatile *)&Allocation->RedirectionSurfaceHandle,
            NULL, NULL) == NULL)
    {
        return;
    }

    (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                Executive, KernelMode, FALSE, NULL);
    if (Allocation->RedirectionSurfaceHandle != 0)
        Allocation->RedirectionSubmittedFenceId[NodeOrdinal] =
            SubmissionFenceId;
    KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
}

NTSTATUS
DxgkAdmitRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    PDXGKENG_ADMIT_REDIRECTED_BLT_PRESENT Callback;
    NTSTATUS Status;

    if (Present == NULL || Present->Size != sizeof(*Present) ||
        Present->Flags != 0 || Present->SurfaceHandle == 0 ||
        Present->WindowHandle == 0 || Present->OwnerProcess == NULL ||
        Present->GlobalShare == 0 || Present->Reserved != 0 ||
        Present->UpdateId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.AdmitRedirectedBltPresent;
    Status = Callback != NULL ? Callback(Present) : STATUS_NOT_SUPPORTED;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
    return Status;
}

VOID
DxgkCancelRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present)
{
    PDXGKENG_CANCEL_REDIRECTED_BLT_PRESENT Callback;

    if (Present == NULL)
        return;

    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.CancelRedirectedBltPresent;
    if (Callback != NULL)
        (VOID)Callback(Present);
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
}

NTSTATUS
DxgkCompleteRedirectedBltPresent(
    _In_ const DXGKRNL_REDIRECTED_BLT_PRESENT *Present,
    _In_reads_(DirtyRectCount) const RECT *DirtyRects,
    _In_ UINT DirtyRectCount,
    _In_opt_ PDXGKRNL_CONTEXT Context)
{
    PDXGKENG_COMPLETE_REDIRECTED_BLT_PRESENT Callback;
    HANDLE ContextHandle;
    NTSTATUS Status;

    if (Present == NULL ||
        (DirtyRectCount != 0 && DirtyRects == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    ContextHandle = Context != NULL
                        ? (HANDLE)(ULONG_PTR)Context->Handle
                        : NULL;
    KeEnterCriticalRegion();
    ExfAcquirePushLockShared(&DxgkpWin32kCddInterfaceLock);
    Callback = DxgkpWin32kCddInterface.CompleteRedirectedBltPresent;
    Status = Callback != NULL
                 ? Callback(Present,
                            DirtyRects,
                            DirtyRectCount,
                            ContextHandle != NULL ? &ContextHandle : NULL,
                            ContextHandle != NULL ? 1 : 0)
                 : STATUS_NOT_SUPPORTED;
    ExfReleasePushLockShared(&DxgkpWin32kCddInterfaceLock);
    KeLeaveCriticalRegion();
    return Status;
}

static NTSTATUS
DxgkpWaitForRedirectionFence(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG SubmissionFenceId)
{
    LARGE_INTEGER Interval;
    ULONG Waited;

    if (SubmissionFenceId == 0)
        return STATUS_SUCCESS;
    if (Adapter == NULL || NodeOrdinal >= DXGK_MAX_TRACKED_NODES)
        return STATUS_INVALID_PARAMETER;

    Interval.QuadPart = -10 * 1000;
    for (Waited = 0; Waited <= DXGKP_REDIRECTION_SYNC_TIMEOUT_MS; ++Waited)
    {
        ULONG CompletedFenceId;

        DxgkRetireCompletedDmaBuffers(Adapter);
        CompletedFenceId = (ULONG)InterlockedCompareExchange(
            (volatile LONG *)&Adapter->NodeLastCompletedFenceId[NodeOrdinal],
            0, 0);
        if (CompletedFenceId != 0 &&
            DxgkpRedirectionFenceReached(CompletedFenceId,
                                         SubmissionFenceId))
        {
            return STATUS_SUCCESS;
        }
        if (InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
            return STATUS_DEVICE_REMOVED;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    return STATUS_TIMEOUT;
}

NTSTATUS
DxgkSynchronizeRedirectionSurfaces(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ DXGK_REDIRECTION_SURFACES_SYNC *Sync,
    _In_ ULONG BufferLength)
{
    PDXGKP_REDIRECTION_SYNC_SNAPSHOT Snapshots = NULL;
    ULONG TargetFences[DXGK_MAX_TRACKED_NODES] = {0};
    SIZE_T HeaderSize;
    SIZE_T RequiredSize;
    ULONG Index;
    ULONG Node;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    HeaderSize = FIELD_OFFSET(DXGK_REDIRECTION_SURFACES_SYNC, Surfaces);
    if (Adapter == NULL || Sync == NULL || BufferLength < HeaderSize ||
        Sync->Flags != 0 || Sync->Reserved != 0 || Sync->SurfaceCount == 0 ||
        Sync->SurfaceCount > DXGKP_REDIRECTION_SYNC_MAX_SURFACES ||
        Sync->SurfaceCount > (MAXULONG - HeaderSize) /
                             sizeof(Sync->Surfaces[0]))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RequiredSize = HeaderSize +
                   (SIZE_T)Sync->SurfaceCount * sizeof(Sync->Surfaces[0]);
    if (Sync->StructSize != RequiredSize || BufferLength != RequiredSize)
        return STATUS_INFO_LENGTH_MISMATCH;
    Sync->FenceId = 0;

    Snapshots = ExAllocatePoolZero(
                    PagedPool,
                    (SIZE_T)Sync->SurfaceCount * sizeof(*Snapshots),
                    TAG_DXGK_DISPLAY);
    if (Snapshots == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Sync->SurfaceCount; ++Index)
    {
        const DXGK_REDIRECTION_SURFACE_SYNC_ENTRY *Entry =
            &Sync->Surfaces[Index];
        PDXGKVMM_RESOURCE Resource = NULL;
        PDXGKVMM_ALLOCATION Allocation = NULL;
        ULONGLONG BottomOffset;
        ULONGLONG TopOffset;

        if (Entry->AllocationHandle == 0 || Entry->ResourceHandle == 0 ||
            Entry->GlobalShare == 0 || Entry->Pitch == 0 ||
            Entry->DirtyRect.left < 0 || Entry->DirtyRect.top < 0 ||
            Entry->DirtyRect.right <= Entry->DirtyRect.left ||
            Entry->DirtyRect.bottom <= Entry->DirtyRect.top ||
            (ULONG)Entry->DirtyRect.right > Entry->Pitch / sizeof(ULONG))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Status = DxgkVidMmReferenceResource(
                     Entry->ResourceHandle, FALSE, NULL, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        Status = DxgkVidMmReferenceAllocation(
                     (HANDLE)(ULONG_PTR)Entry->AllocationHandle,
                     Adapter, NULL, &Allocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkVidMmDereferenceResource(Resource);
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        TopOffset = (ULONGLONG)(ULONG)Entry->DirtyRect.top * Entry->Pitch;
        BottomOffset =
            (ULONGLONG)(ULONG)Entry->DirtyRect.bottom * Entry->Pitch;
        if (Resource->Adapter != Adapter ||
            Resource->GlobalShareHandle != Entry->GlobalShare ||
            Resource->AllocationCount != 1 || Allocation->Resource != Resource ||
            BottomOffset <= TopOffset || BottomOffset > Allocation->Size)
        {
            DxgkVidMmDereferenceAllocation(Allocation);
            DxgkVidMmDereferenceResource(Resource);
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        DxgkVidMmDereferenceResource(Resource);

        (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                    Executive, KernelMode, FALSE, NULL);
        if (Allocation->RedirectionSurfaceHandle == 0)
        {
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
            DxgkVidMmDereferenceAllocation(Allocation);
            Status = STATUS_INVALID_DEVICE_STATE;
            break;
        }
        for (Node = 0; Node < DXGK_MAX_TRACKED_NODES; ++Node)
        {
            ULONG Fence = Allocation->RedirectionSubmittedFenceId[Node];

            Snapshots[Index].Fences[Node] = Fence;
            if (Fence != 0 &&
                (TargetFences[Node] == 0 ||
                 !DxgkpRedirectionFenceReached(TargetFences[Node], Fence)))
            {
                TargetFences[Node] = Fence;
            }
        }
        KeReleaseMutex(&Allocation->ResidencyLock, FALSE);

        Snapshots[Index].Allocation = Allocation;
        Snapshots[Index].CacheOffset = TopOffset;
        Snapshots[Index].CacheLength = BottomOffset - TopOffset;
    }

    if (NT_SUCCESS(Status))
    {
        for (Node = 0; Node < DXGK_MAX_TRACKED_NODES; ++Node)
        {
            Status = DxgkpWaitForRedirectionFence(
                         Adapter, Node, TargetFences[Node]);
            if (!NT_SUCCESS(Status))
                break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Sync->SurfaceCount; ++Index)
        {
            Status = DxgkVidMmInvalidateReferencedAllocationCache(
                         Snapshots[Index].Allocation,
                         Snapshots[Index].CacheOffset,
                         Snapshots[Index].CacheLength);
            if (!NT_SUCCESS(Status))
                break;
        }
    }

    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Sync->SurfaceCount; ++Index)
        {
            PDXGKVMM_ALLOCATION Allocation = Snapshots[Index].Allocation;

            (VOID)KeWaitForSingleObject(&Allocation->ResidencyLock,
                                        Executive, KernelMode, FALSE, NULL);
            for (Node = 0; Node < DXGK_MAX_TRACKED_NODES; ++Node)
            {
                if (Allocation->RedirectionSubmittedFenceId[Node] ==
                    Snapshots[Index].Fences[Node])
                {
                    Allocation->RedirectionSubmittedFenceId[Node] = 0;
                }
            }
            KeReleaseMutex(&Allocation->ResidencyLock, FALSE);
        }

        Sync->FenceId = (ULONGLONG)InterlockedIncrement64(
            &Adapter->NextRedirectionFenceId);
        if (Sync->FenceId == 0)
        {
            Sync->FenceId = (ULONGLONG)InterlockedIncrement64(
                &Adapter->NextRedirectionFenceId);
        }
    }

    for (Index = 0; Index < Sync->SurfaceCount; ++Index)
    {
        if (Snapshots[Index].Allocation != NULL)
            DxgkVidMmDereferenceAllocation(Snapshots[Index].Allocation);
    }
    ExFreePoolWithTag(Snapshots, TAG_DXGK_DISPLAY);
    return Status;
}
