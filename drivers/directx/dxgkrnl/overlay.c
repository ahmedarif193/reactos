/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Vista hardware-overlay object and DDI lifetime management
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "dxgkrnl_private.h"
#include "vidmm.h"

static BOOLEAN
DxgkpReferenceOverlay(
    _In_ PVOID Object)
{
    PDXGKRNL_OVERLAY Overlay = Object;

    if (Overlay == NULL ||
        InterlockedCompareExchange(&Overlay->Destroying, 0, 0) != 0)
    {
        return FALSE;
    }

    InterlockedIncrement(&Overlay->ReferenceCount);
    if (InterlockedCompareExchange(&Overlay->Destroying, 0, 0) != 0)
    {
        LONG References = InterlockedDecrement(&Overlay->ReferenceCount);

        if (References <= 1)
            KeSetEvent(&Overlay->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
        return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpDereferenceOverlay(
    _In_ PDXGKRNL_OVERLAY Overlay)
{
    LONG References;

    ASSERT(Overlay != NULL);
    References = InterlockedDecrement(&Overlay->ReferenceCount);
    ASSERT(References >= 1);
    if (References == 1 &&
        InterlockedCompareExchange(&Overlay->Destroying, 0, 0) != 0)
    {
        KeSetEvent(&Overlay->ReferencesDrainedEvent, IO_NO_INCREMENT, FALSE);
    }
}

static VOID
DxgkpWaitForOverlayReferences(
    _In_ PDXGKRNL_OVERLAY Overlay)
{
    while (InterlockedCompareExchange(&Overlay->ReferenceCount, 0, 0) != 1)
    {
        (VOID)KeWaitForSingleObject(&Overlay->ReferencesDrainedEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
    }
}

static VOID
DxgkpReleaseOverlayAllocation(
    _In_opt_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation == NULL)
        return;

    DxgkVidMmReleaseSubmissionResidencyPin(Allocation);
    DxgkVidMmDereferenceAllocation(Allocation);
}

static VOID
DxgkpFreeOverlay(
    _In_ PDXGKRNL_OVERLAY Overlay)
{
    PDXGKRNL_DEVICE Device;

    ASSERT(Overlay != NULL);
    ASSERT(InterlockedCompareExchange(&Overlay->ReferenceCount, 0, 0) == 1);
    ASSERT(IsListEmpty(&Overlay->DeviceOverlayListEntry));

    Device = Overlay->Device;
    DxgkpReleaseOverlayAllocation(Overlay->Allocation);
    Overlay->Allocation = NULL;
    Overlay->hMiniportOverlay = NULL;
    InterlockedExchange(&Overlay->ReferenceCount, 0);
    ExFreePoolWithTag(Overlay, TAG_DXGK_OVERLAY);
    DxgkDereferenceDevice(Device);
}

static BOOLEAN
DxgkpValidOverlayRect(
    _In_ CONST D3DDDIRECT *Rect)
{
    return Rect != NULL && Rect->right > Rect->left &&
           Rect->bottom > Rect->top;
}

static VOID
DxgkpCopyOverlayRect(
    _Out_ RECT *Destination,
    _In_ CONST D3DDDIRECT *Source)
{
    Destination->left = Source->left;
    Destination->top = Source->top;
    Destination->right = Source->right;
    Destination->bottom = Source->bottom;
}

static NTSTATUS
DxgkpCaptureOverlayPrivateData(
    _In_reads_bytes_opt_(Size) CONST VOID *Data,
    _In_ UINT Size,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_result_bytebuffer_maybenull_(Size) PVOID *CapturedData)
{
    if (CapturedData == NULL)
        return STATUS_INVALID_PARAMETER;
    *CapturedData = NULL;
    if (Size == 0)
        return Data == NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    if (Data == NULL || Size > DXGKP_MAX_USER_PRIVATE_DATA)
        return STATUS_INVALID_PARAMETER;

    return DxgkpCaptureUserBuffer(Data,
                                  Size,
                                  AccessMode,
                                  TAG_DXGK_CAPTURE,
                                  CapturedData);
}

static NTSTATUS
DxgkpReferenceAndPinOverlayAllocation(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE Handle,
    _Out_ PDXGKVMM_ALLOCATION *OutAllocation,
    _Out_ DXGK_ALLOCATIONLIST *Placement)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    *OutAllocation = NULL;
    RtlZeroMemory(Placement, sizeof(*Placement));
    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Handle,
                                          Device->Adapter,
                                          Device,
                                          &Allocation);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Residency is the generic dxgkrnl responsibility. Cache ownership is
     * transferred by the producer/miniport path; a blind CPU clean here could
     * overwrite firmware-produced video frames on non-coherent systems. */
    Status = DxgkVidMmAcquireSubmissionResidencyPinEx(Allocation,
                                                       Device->Adapter,
                                                       Placement,
                                                       FALSE);
    if (!NT_SUCCESS(Status))
    {
        DxgkVidMmDereferenceAllocation(Allocation);
        return Status;
    }

    *OutAllocation = Allocation;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpInvokeCreateOverlay(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PDXGKARG_CREATEOVERLAY Args)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (DXGK_CB_FULL(Adapter, DxgkDdiCreateOverlay) == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DEVICE_NOT_READY;

    (VOID)KeWaitForSingleObject(&Adapter->OverlayMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else
    {
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiCreateOverlay)(
                         Adapter->MiniportDeviceContext,
                         Args);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseMiniportCallback(Adapter);
    }
    KeReleaseMutex(&Adapter->OverlayMutex, FALSE);
    DxgkEndKmdTransaction(Adapter);
    return Status;
}

static NTSTATUS
DxgkpInvokeUpdateOverlay(
    _In_ PDXGKRNL_OVERLAY Overlay,
    _In_ CONST DXGKARG_UPDATEOVERLAY *Args)
{
    PDXGKRNL_ADAPTER Adapter = Overlay->Device->Adapter;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (DXGK_CB_FULL(Adapter, DxgkDdiUpdateOverlay) == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DEVICE_NOT_READY;

    (VOID)KeWaitForSingleObject(&Adapter->OverlayMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else
    {
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiUpdateOverlay)(
                         Overlay->hMiniportOverlay,
                         Args);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseMiniportCallback(Adapter);
    }
    KeReleaseMutex(&Adapter->OverlayMutex, FALSE);
    DxgkEndKmdTransaction(Adapter);
    return Status;
}

static NTSTATUS
DxgkpInvokeFlipOverlay(
    _In_ PDXGKRNL_OVERLAY Overlay,
    _In_ CONST DXGKARG_FLIPOVERLAY *Args)
{
    PDXGKRNL_ADAPTER Adapter = Overlay->Device->Adapter;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (DXGK_CB_FULL(Adapter, DxgkDdiFlipOverlay) == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DEVICE_NOT_READY;

    (VOID)KeWaitForSingleObject(&Adapter->OverlayMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else
    {
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiFlipOverlay)(
                         Overlay->hMiniportOverlay,
                         Args);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseMiniportCallback(Adapter);
    }
    KeReleaseMutex(&Adapter->OverlayMutex, FALSE);
    DxgkEndKmdTransaction(Adapter);
    return Status;
}

static NTSTATUS
DxgkpInvokeDestroyOverlay(
    _In_ PDXGKRNL_OVERLAY Overlay)
{
    PDXGKRNL_ADAPTER Adapter = Overlay->Device->Adapter;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    if (Overlay->hMiniportOverlay == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiDestroyOverlay) == NULL ||
        Adapter->MiniportDeviceStopped ||
        InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) == 0)
    {
        return STATUS_SUCCESS;
    }
    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DEVICE_NOT_READY;

    (VOID)KeWaitForSingleObject(&Adapter->OverlayMutex,
                                Executive,
                                KernelMode,
                                FALSE,
                                NULL);
    if (!DxgkAcquireMiniportCallback(Adapter))
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else
    {
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiDestroyOverlay)(
                         Overlay->hMiniportOverlay);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseMiniportCallback(Adapter);
    }
    KeReleaseMutex(&Adapter->OverlayMutex, FALSE);
    DxgkEndKmdTransaction(Adapter);
    return Status;
}

static VOID
DxgkpRetainFailedOverlay(
    _In_ PDXGKRNL_OVERLAY Overlay)
{
    PDXGKRNL_DEVICE Device = Overlay->Device;

    InterlockedExchange(&Overlay->MiniportDestroyPending, 1);
    ExAcquireFastMutex(&Device->DeviceMutex);
    InterlockedExchange(&Overlay->TeardownClaimed, 0);
    if (IsListEmpty(&Overlay->DeviceOverlayListEntry))
    {
        InsertTailList(&Device->OverlayListHead,
                       &Overlay->DeviceOverlayListEntry);
    }
    ExReleaseFastMutex(&Device->DeviceMutex);
    InterlockedExchange(&Device->MiniportDestroyPending, 1);
}

NTSTATUS
DxgkCreateOverlayWithAccessMode(
    _Inout_ D3DKMT_CREATEOVERLAY *Data,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_OVERLAY Overlay = NULL;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    DXGK_ALLOCATIONLIST Placement;
    DXGKARG_CREATEOVERLAY Args;
    PVOID CapturedPrivateData = NULL;
    NTSTATUS DestroyStatus;
    NTSTATUS Status;

    PAGED_CODE();
    if (Data == NULL)
        return STATUS_INVALID_PARAMETER;
    Data->hOverlay = 0;
    if (Data->OverlayInfo.hAllocation == 0 ||
        !DxgkpValidOverlayRect(&Data->OverlayInfo.SrcRect) ||
        !DxgkpValidOverlayRect(&Data->OverlayInfo.DstRect))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkReferenceOwnedDeviceByHandle(Data->hDevice,
                                               PsGetCurrentProcess(),
                                               &Adapter,
                                               &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Data->VidPnSourceId >= Adapter->NumberOfVideoPresentSources ||
        DXGK_CB_FULL(Adapter, DxgkDdiCreateOverlay) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiUpdateOverlay) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiFlipOverlay) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiDestroyOverlay) == NULL)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkpCaptureOverlayPrivateData(
                 Data->OverlayInfo.pPrivateDriverData,
                 Data->OverlayInfo.PrivateDriverDataSize,
                 EmbeddedBufferMode,
                 &CapturedPrivateData);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = DxgkpReferenceAndPinOverlayAllocation(
                 Device,
                 Data->OverlayInfo.hAllocation,
                 &Allocation,
                 &Placement);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Overlay = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Overlay),
                                    TAG_DXGK_OVERLAY);
    if (Overlay == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    RtlZeroMemory(Overlay, sizeof(*Overlay));
    Overlay->Device = Device;
    Overlay->OwnerProcess = PsGetCurrentProcess();
    Overlay->Allocation = Allocation;
    Overlay->ReferenceCount = 1;
    KeInitializeMutex(&Overlay->OperationMutex, 0);
    InitializeListHead(&Overlay->DeviceOverlayListEntry);
    KeInitializeEvent(&Overlay->ReferencesDrainedEvent,
                      NotificationEvent,
                      FALSE);

    RtlZeroMemory(&Args, sizeof(Args));
    Args.VidPnSourceId = Data->VidPnSourceId;
    Args.OverlayInfo.hAllocation = Allocation->MiniportHandle;
    Args.OverlayInfo.PhysicalAddress = Placement.PhysicalAddress;
    Args.OverlayInfo.SegmentId = Placement.SegmentId;
    DxgkpCopyOverlayRect(&Args.OverlayInfo.DstRect,
                         &Data->OverlayInfo.DstRect);
    DxgkpCopyOverlayRect(&Args.OverlayInfo.SrcRect,
                         &Data->OverlayInfo.SrcRect);
    Args.OverlayInfo.pPrivateDriverData = CapturedPrivateData;
    Args.OverlayInfo.PrivateDriverDataSize =
        Data->OverlayInfo.PrivateDriverDataSize;
    Status = DxgkpInvokeCreateOverlay(Adapter, &Args);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (Args.hOverlay == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto RollbackMiniport;
    }
    Overlay->hMiniportOverlay = Args.hOverlay;

    Status = DxgkCreateOwnedHandle(DxgkHandleTypeOverlay,
                                    Overlay,
                                    Adapter,
                                    Overlay->OwnerProcess,
                                    &Overlay->Destroying,
                                    &Overlay->TeardownClaimed,
                                    &Overlay->Handle);
    if (!NT_SUCCESS(Status))
        goto RollbackMiniport;

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 ||
        InterlockedCompareExchange(&Device->ExecutionState, 0, 0) !=
            D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else
    {
        InsertTailList(&Device->OverlayListHead,
                       &Overlay->DeviceOverlayListEntry);
    }
    ExReleaseFastMutex(&Device->DeviceMutex);
    if (!NT_SUCCESS(Status))
    {
        DxgkRemoveOwnedHandleObject(DxgkHandleTypeOverlay, Overlay);
        goto RollbackMiniport;
    }

    Data->hOverlay = Overlay->Handle;
    if (CapturedPrivateData != NULL)
        ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
    return STATUS_SUCCESS;

RollbackMiniport:
    InterlockedExchange(&Overlay->Destroying, 1);
    InterlockedExchange(&Overlay->TeardownClaimed, 1);
    DestroyStatus = DxgkpInvokeDestroyOverlay(Overlay);
    if (!NT_SUCCESS(DestroyStatus))
    {
        DxgkpRetainFailedOverlay(Overlay);
        if (CapturedPrivateData != NULL)
            ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
        return Status;
    }

Failure:
    if (CapturedPrivateData != NULL)
        ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
    if (Overlay != NULL)
    {
        DxgkpFreeOverlay(Overlay);
    }
    else
    {
        DxgkpReleaseOverlayAllocation(Allocation);
        DxgkDereferenceDevice(Device);
    }
    return Status;
}

NTSTATUS
NTAPI
DxgkCreateOverlay(
    _Inout_ D3DKMT_CREATEOVERLAY *Data)
{
    return DxgkCreateOverlayWithAccessMode(Data, KernelMode);
}

NTSTATUS
DxgkUpdateOverlayWithAccessMode(
    _In_ CONST D3DKMT_UPDATEOVERLAY *Data,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_OVERLAY Overlay;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_ALLOCATION OldAllocation;
    DXGK_ALLOCATIONLIST Placement;
    DXGKARG_UPDATEOVERLAY Args;
    PVOID CapturedPrivateData = NULL;
    PVOID Object;
    NTSTATUS Status;

    PAGED_CODE();
    if (Data == NULL || Data->hOverlay == 0 ||
        Data->OverlayInfo.hAllocation == 0 ||
        !DxgkpValidOverlayRect(&Data->OverlayInfo.SrcRect) ||
        !DxgkpValidOverlayRect(&Data->OverlayInfo.DstRect))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkReferenceOwnedDeviceByHandle(Data->hDevice,
                                               PsGetCurrentProcess(),
                                               &Adapter,
                                               &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkReferenceOwnedHandle(Data->hOverlay,
                                       DxgkHandleTypeOverlay,
                                       PsGetCurrentProcess(),
                                       DxgkpReferenceOverlay,
                                       &Object);
    if (!NT_SUCCESS(Status))
        goto CleanupDevice;
    Overlay = Object;
    if (Overlay->Device != Device)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto CleanupOverlay;
    }
    Status = DxgkpCaptureOverlayPrivateData(
                 Data->OverlayInfo.pPrivateDriverData,
                 Data->OverlayInfo.PrivateDriverDataSize,
                 EmbeddedBufferMode,
                 &CapturedPrivateData);
    if (!NT_SUCCESS(Status))
        goto CleanupOverlay;
    Status = DxgkpReferenceAndPinOverlayAllocation(
                 Device,
                 Data->OverlayInfo.hAllocation,
                 &Allocation,
                 &Placement);
    if (!NT_SUCCESS(Status))
        goto CleanupOverlay;

    Status = KeWaitForSingleObject(&Overlay->OperationMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        goto CleanupOverlay;

    RtlZeroMemory(&Args, sizeof(Args));
    Args.OverlayInfo.hAllocation = Allocation->MiniportHandle;
    Args.OverlayInfo.PhysicalAddress = Placement.PhysicalAddress;
    Args.OverlayInfo.SegmentId = Placement.SegmentId;
    DxgkpCopyOverlayRect(&Args.OverlayInfo.DstRect,
                         &Data->OverlayInfo.DstRect);
    DxgkpCopyOverlayRect(&Args.OverlayInfo.SrcRect,
                         &Data->OverlayInfo.SrcRect);
    Args.OverlayInfo.pPrivateDriverData = CapturedPrivateData;
    Args.OverlayInfo.PrivateDriverDataSize =
        Data->OverlayInfo.PrivateDriverDataSize;
    Status = DxgkpInvokeUpdateOverlay(Overlay, &Args);
    if (NT_SUCCESS(Status))
    {
        OldAllocation = Overlay->Allocation;
        Overlay->Allocation = Allocation;
        Allocation = NULL;
        DxgkpReleaseOverlayAllocation(OldAllocation);
    }
    KeReleaseMutex(&Overlay->OperationMutex, FALSE);

CleanupOverlay:
    DxgkpReleaseOverlayAllocation(Allocation);
    if (CapturedPrivateData != NULL)
        ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
    DxgkpDereferenceOverlay(Overlay);
CleanupDevice:
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkUpdateOverlay(
    _In_ CONST D3DKMT_UPDATEOVERLAY *Data)
{
    return DxgkUpdateOverlayWithAccessMode(Data, KernelMode);
}

NTSTATUS
DxgkFlipOverlayWithAccessMode(
    _In_ CONST D3DKMT_FLIPOVERLAY *Data,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_OVERLAY Overlay;
    PDXGKVMM_ALLOCATION Allocation = NULL;
    PDXGKVMM_ALLOCATION OldAllocation;
    DXGK_ALLOCATIONLIST Placement;
    DXGKARG_FLIPOVERLAY Args;
    PVOID CapturedPrivateData = NULL;
    PVOID Object;
    NTSTATUS Status;

    PAGED_CODE();
    if (Data == NULL || Data->hOverlay == 0 || Data->hSource == 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(Data->hDevice,
                                               PsGetCurrentProcess(),
                                               &Adapter,
                                               &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkReferenceOwnedHandle(Data->hOverlay,
                                       DxgkHandleTypeOverlay,
                                       PsGetCurrentProcess(),
                                       DxgkpReferenceOverlay,
                                       &Object);
    if (!NT_SUCCESS(Status))
        goto CleanupDevice;
    Overlay = Object;
    if (Overlay->Device != Device)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto CleanupOverlay;
    }
    Status = DxgkpCaptureOverlayPrivateData(Data->pPrivateDriverData,
                                             Data->PrivateDriverDataSize,
                                             EmbeddedBufferMode,
                                             &CapturedPrivateData);
    if (!NT_SUCCESS(Status))
        goto CleanupOverlay;
    Status = DxgkpReferenceAndPinOverlayAllocation(Device,
                                                    Data->hSource,
                                                    &Allocation,
                                                    &Placement);
    if (!NT_SUCCESS(Status))
        goto CleanupOverlay;

    Status = KeWaitForSingleObject(&Overlay->OperationMutex,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        goto CleanupOverlay;

    RtlZeroMemory(&Args, sizeof(Args));
    Args.hSource = Allocation->MiniportHandle;
    Args.SrcPhysicalAddress = Placement.PhysicalAddress;
    Args.SrcSegmentId = Placement.SegmentId;
    Args.pPrivateDriverData = CapturedPrivateData;
    Args.PrivateDriverDataSize = Data->PrivateDriverDataSize;
    Status = DxgkpInvokeFlipOverlay(Overlay, &Args);
    if (NT_SUCCESS(Status))
    {
        OldAllocation = Overlay->Allocation;
        Overlay->Allocation = Allocation;
        Allocation = NULL;
        DxgkpReleaseOverlayAllocation(OldAllocation);
    }
    KeReleaseMutex(&Overlay->OperationMutex, FALSE);

CleanupOverlay:
    DxgkpReleaseOverlayAllocation(Allocation);
    if (CapturedPrivateData != NULL)
        ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
    DxgkpDereferenceOverlay(Overlay);
CleanupDevice:
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
NTAPI
DxgkFlipOverlay(
    _In_ CONST D3DKMT_FLIPOVERLAY *Data)
{
    return DxgkFlipOverlayWithAccessMode(Data, KernelMode);
}

NTSTATUS
NTAPI
DxgkDestroyOverlay(
    _In_ CONST D3DKMT_DESTROYOVERLAY *Data)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_OVERLAY Overlay;
    PVOID ReferencedObject;
    PVOID DetachedObject;
    NTSTATUS Status;

    PAGED_CODE();
    if (Data == NULL || Data->hOverlay == 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(Data->hDevice,
                                               PsGetCurrentProcess(),
                                               &Adapter,
                                               &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkReferenceOwnedHandle(Data->hOverlay,
                                       DxgkHandleTypeOverlay,
                                       PsGetCurrentProcess(),
                                       DxgkpReferenceOverlay,
                                       &ReferencedObject);
    if (!NT_SUCCESS(Status))
        goto CleanupDevice;
    Overlay = ReferencedObject;
    if (Overlay->Device != Device)
    {
        Status = STATUS_INVALID_PARAMETER;
        DxgkpDereferenceOverlay(Overlay);
        goto CleanupDevice;
    }

    Status = DxgkDetachOwnedHandle(Data->hOverlay,
                                    DxgkHandleTypeOverlay,
                                    PsGetCurrentProcess(),
                                    &DetachedObject);
    if (!NT_SUCCESS(Status))
    {
        DxgkpDereferenceOverlay(Overlay);
        goto CleanupDevice;
    }
    ASSERT(DetachedObject == Overlay);

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (!IsListEmpty(&Overlay->DeviceOverlayListEntry))
    {
        RemoveEntryList(&Overlay->DeviceOverlayListEntry);
        InitializeListHead(&Overlay->DeviceOverlayListEntry);
    }
    ExReleaseFastMutex(&Device->DeviceMutex);
    DxgkpDereferenceOverlay(Overlay);
    DxgkpWaitForOverlayReferences(Overlay);

    Status = DxgkpInvokeDestroyOverlay(Overlay);
    if (!NT_SUCCESS(Status))
    {
        DxgkpRetainFailedOverlay(Overlay);
        goto CleanupDevice;
    }

    InterlockedExchange(&Overlay->MiniportDestroyPending, 0);
    DxgkpFreeOverlay(Overlay);

CleanupDevice:
    DxgkDereferenceDevice(Device);
    return Status;
}

NTSTATUS
DxgkOverlayCleanupDevice(
    _In_ PDXGKRNL_DEVICE Device)
{
    NTSTATUS Status;

    PAGED_CODE();
    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;

    for (;;)
    {
        PDXGKRNL_OVERLAY Overlay;
        PLIST_ENTRY Entry;
        BOOLEAN OwnsTeardown;

        ExAcquireFastMutex(&Device->DeviceMutex);
        if (IsListEmpty(&Device->OverlayListHead))
        {
            ExReleaseFastMutex(&Device->DeviceMutex);
            return STATUS_SUCCESS;
        }

        Entry = Device->OverlayListHead.Flink;
        Overlay = CONTAINING_RECORD(Entry,
                                    DXGKRNL_OVERLAY,
                                    DeviceOverlayListEntry);
        OwnsTeardown = DxgkTryClaimTeardown(&Overlay->TeardownClaimed);
        InterlockedExchange(&Overlay->Destroying, 1);
        RemoveEntryList(Entry);
        InitializeListHead(Entry);
        ExReleaseFastMutex(&Device->DeviceMutex);

        if (!OwnsTeardown)
        {
            /* The direct destroy owner retains the device until it finishes. */
            continue;
        }

        DxgkRemoveOwnedHandleObject(DxgkHandleTypeOverlay, Overlay);
        DxgkpWaitForOverlayReferences(Overlay);
        Status = DxgkpInvokeDestroyOverlay(Overlay);
        if (!NT_SUCCESS(Status))
        {
            DxgkpRetainFailedOverlay(Overlay);
            return Status;
        }

        InterlockedExchange(&Overlay->MiniportDestroyPending, 0);
        DxgkpFreeOverlay(Overlay);
    }
}
