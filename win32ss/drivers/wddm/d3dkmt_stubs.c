/*
 * PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DKMT kernel-mode entry points (IOCTL bridge to dxgkrnl)
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 *
 * OVERVIEW
 * --------
 * This file provides the win32k-side D3DKMT* functions that marshal
 * parameters into METHOD_BUFFERED IOCTLs and send them to dxgkrnl via
 * WddmBridgeSendIoctl.
 *
 * The call chain is:
 *
 *   User-mode D3D/DXGI runtime
 *       |  NtGdiDdDDI* syscall
 *       v
 *   win32ss/gdi/ntgdi/d3dkmt.c   (dispatch via callback table)
 *       |  D3DKMT* call (this file)
 *       v
 *   WddmBridgeSendIoctl  ->  \Device\DxgKrnl  (dxgkrnl.sys)
 */

#include <ntifs.h>
#include <windef.h>
#include <pseh/pseh2.h>
#include "wddm_bridge.h"
#include <d3dkmthk.h>
#include <reactos/rddm/rxgkinterface.h>
#define NDEBUG
#include <debug.h>

/* Import the IOCTL code definitions from dxgkrnl */
#define DXGKRNL_DEVICE_TYPE     0x23

#define IOCTL_D3DKMT_OPENADAPTERFROMHDC \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x110, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x111, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x112, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CLOSEADAPTER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYADAPTERINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x104, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x120, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x121, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEALLOCATION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x130, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYALLOCATION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x131, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_LOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x132, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UNLOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x133, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYRESOURCEINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x134, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENRESOURCE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x135, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x136, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RENDER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x140, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_PRESENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x141, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x150, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x151, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETDISPLAYMODELIST \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x105, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETDISPLAYMODE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x160, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_D3DKMT_CREATECONTEXT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x122, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYCONTEXT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x123, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x152, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x153, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ESCAPE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x170, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETVIDPNSOURCEOWNER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x161, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETDEVICESTATE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x124, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TAG_WDDM_BRIDGE             'BmdW'
#define D3DKMT_BRIDGE_MAX_ALLOCATIONS 4096U
#define D3DKMT_BRIDGE_MAX_PRIVATE_BYTES (1024U * 1024U)

static NTSTATUS
WddmBridgeSafeCopyFrom(
    _Out_writes_bytes_(Size) PVOID Destination,
    _In_reads_bytes_(Size) CONST VOID *Source,
    _In_ SIZE_T Size)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Size == 0)
        return STATUS_SUCCESS;

    if (Destination == NULL || Source == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        RtlCopyMemory(Destination, Source, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static NTSTATUS
WddmBridgeSizeForCount(
    _In_ UINT Count,
    _In_ SIZE_T ElementSize,
    _Out_ SIZE_T *Size)
{
    if (Size == NULL || ElementSize == 0)
        return STATUS_INVALID_PARAMETER;

    if ((SIZE_T)Count > (((SIZE_T)-1) / ElementSize))
        return STATUS_INVALID_PARAMETER;

    *Size = (SIZE_T)Count * ElementSize;
    return STATUS_SUCCESS;
}

/* ---- Adapter management -------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromHdc(
    _Inout_ D3DKMT_OPENADAPTERFROMHDC *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_OPENADAPTERFROMHDC,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromGdiDisplayName(
    _Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromDeviceName(
    _Inout_ D3DKMT_OPENADAPTERFROMDEVICENAME *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTCloseAdapter(
    _In_ CONST D3DKMT_CLOSEADAPTER *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTQueryAdapterInfo(
    _In_ CONST D3DKMT_QUERYADAPTERINFO *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_QUERYADAPTERINFO,
                               (PVOID)pData, sizeof(*pData),
                               (PVOID)pData, sizeof(*pData));
}

/* ---- Device management --------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateDevice(
    _Inout_ D3DKMT_CREATEDEVICE *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CREATEDEVICE,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTDestroyDevice(
    _In_ CONST D3DKMT_DESTROYDEVICE *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYDEVICE,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- Allocation management ----------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *pData)
{
    D3DKMT_CREATEALLOCATION Captured;
    D3DDDI_ALLOCATIONINFO *AllocationInfo = NULL;
    PVOID *AllocationPrivateBuffers = NULL;
    PVOID PrivateDriverData = NULL;
    PVOID PrivateRuntimeData = NULL;
    SIZE_T AllocationInfoSize;
    SIZE_T PointerArraySize;
    SIZE_T TotalPrivateSize = 0;
    NTSTATUS Status;
    UINT i;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    if (Captured.NumAllocations == 0 ||
        Captured.NumAllocations > D3DKMT_BRIDGE_MAX_ALLOCATIONS ||
        Captured.pAllocationInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WddmBridgeSizeForCount(Captured.NumAllocations,
                                    sizeof(*AllocationInfo),
                                    &AllocationInfoSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSizeForCount(Captured.NumAllocations,
                                    sizeof(*AllocationPrivateBuffers),
                                    &PointerArraySize);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationInfo = ExAllocatePoolWithTag(NonPagedPool,
                                           AllocationInfoSize,
                                           TAG_WDDM_BRIDGE);
    if (AllocationInfo == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    AllocationPrivateBuffers = ExAllocatePoolWithTag(NonPagedPool,
                                                     PointerArraySize,
                                                     TAG_WDDM_BRIDGE);
    if (AllocationPrivateBuffers == NULL)
    {
        ExFreePoolWithTag(AllocationInfo, TAG_WDDM_BRIDGE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(AllocationPrivateBuffers, PointerArraySize);

    Status = WddmBridgeSafeCopyFrom(AllocationInfo,
                                    Captured.pAllocationInfo,
                                    AllocationInfoSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (Captured.PrivateDriverDataSize != 0)
    {
        if (Captured.pPrivateDriverData == NULL ||
            Captured.PrivateDriverDataSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        PrivateDriverData = ExAllocatePoolWithTag(NonPagedPool,
                                                  Captured.PrivateDriverDataSize,
                                                  TAG_WDDM_BRIDGE);
        if (PrivateDriverData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        Status = WddmBridgeSafeCopyFrom(PrivateDriverData,
                                        Captured.pPrivateDriverData,
                                        Captured.PrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Captured.pPrivateDriverData = PrivateDriverData;
        TotalPrivateSize += Captured.PrivateDriverDataSize;
    }

    if (Captured.PrivateRuntimeDataSize != 0)
    {
        if (Captured.pPrivateRuntimeData == NULL ||
            Captured.PrivateRuntimeDataSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES ||
            TotalPrivateSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES - Captured.PrivateRuntimeDataSize)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        PrivateRuntimeData = ExAllocatePoolWithTag(NonPagedPool,
                                                   Captured.PrivateRuntimeDataSize,
                                                   TAG_WDDM_BRIDGE);
        if (PrivateRuntimeData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        Status = WddmBridgeSafeCopyFrom(PrivateRuntimeData,
                                        Captured.pPrivateRuntimeData,
                                        Captured.PrivateRuntimeDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Captured.pPrivateRuntimeData = PrivateRuntimeData;
        TotalPrivateSize += Captured.PrivateRuntimeDataSize;
    }

    for (i = 0; i < Captured.NumAllocations; ++i)
    {
        if (AllocationInfo[i].PrivateDriverDataSize == 0)
            continue;

        if (AllocationInfo[i].pPrivateDriverData == NULL ||
            AllocationInfo[i].PrivateDriverDataSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES ||
            TotalPrivateSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES - AllocationInfo[i].PrivateDriverDataSize)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        AllocationPrivateBuffers[i] = ExAllocatePoolWithTag(
                                          NonPagedPool,
                                          AllocationInfo[i].PrivateDriverDataSize,
                                          TAG_WDDM_BRIDGE);
        if (AllocationPrivateBuffers[i] == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        Status = WddmBridgeSafeCopyFrom(AllocationPrivateBuffers[i],
                                        AllocationInfo[i].pPrivateDriverData,
                                        AllocationInfo[i].PrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        AllocationInfo[i].pPrivateDriverData = AllocationPrivateBuffers[i];
        TotalPrivateSize += AllocationInfo[i].PrivateDriverDataSize;
    }

    Captured.pAllocationInfo = AllocationInfo;

    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_CREATEALLOCATION,
                                 &Captured, sizeof(Captured),
                                 &Captured, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    _SEH2_TRY
    {
        pData->hResource = Captured.hResource;
        pData->hGlobalShare = Captured.hGlobalShare;

        for (i = 0; i < Captured.NumAllocations; ++i)
        {
            pData->pAllocationInfo[i].hAllocation =
                AllocationInfo[i].hAllocation;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

Cleanup:
    if (AllocationPrivateBuffers != NULL)
    {
        for (i = 0; i < Captured.NumAllocations; ++i)
        {
            if (AllocationPrivateBuffers[i] != NULL)
                ExFreePoolWithTag(AllocationPrivateBuffers[i], TAG_WDDM_BRIDGE);
        }

        ExFreePoolWithTag(AllocationPrivateBuffers, TAG_WDDM_BRIDGE);
    }

    if (PrivateRuntimeData != NULL)
        ExFreePoolWithTag(PrivateRuntimeData, TAG_WDDM_BRIDGE);

    if (PrivateDriverData != NULL)
        ExFreePoolWithTag(PrivateDriverData, TAG_WDDM_BRIDGE);

    if (AllocationInfo != NULL)
        ExFreePoolWithTag(AllocationInfo, TAG_WDDM_BRIDGE);

    return Status;
}

NTSTATUS
APIENTRY
D3DKMTDestroyAllocation(
    _In_ CONST D3DKMT_DESTROYALLOCATION *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYALLOCATION,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTQueryResourceInfo(
    _Inout_ D3DKMT_QUERYRESOURCEINFO *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_QUERYRESOURCEINFO,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_OPENRESOURCE,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTGetSharedPrimaryHandle(
    _Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

/* ---- Rendering ----------------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTRender(
    _Inout_ D3DKMT_RENDER *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_RENDER,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTPresent(
    _Inout_ D3DKMT_PRESENT *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_PRESENT,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

/* ---- Memory locking ------------------------------------------------------ */

NTSTATUS
APIENTRY
D3DKMTLock(
    _Inout_ D3DKMT_LOCK *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_LOCK,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTUnlock(
    _In_ CONST D3DKMT_UNLOCK *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_UNLOCK,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- Synchronisation objects --------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObject(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObject(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- Display mode management --------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTGetDisplayModeList(
    _Inout_ D3DKMT_GETDISPLAYMODELIST *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETDISPLAYMODELIST,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTSetDisplayMode(
    _In_ CONST D3DKMT_SETDISPLAYMODE *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETDISPLAYMODE,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- Context management -------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateContext(
    _Inout_ D3DKMT_CREATECONTEXT *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CREATECONTEXT,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTDestroyContext(
    _In_ CONST D3DKMT_DESTROYCONTEXT *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYCONTEXT,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- Synchronisation object management ----------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTDestroySynchronizationObject(
    _In_ CONST D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- Escape -------------------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTEscape(
    _In_ CONST D3DKMT_ESCAPE *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_ESCAPE,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- VidPn source ownership ---------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner(
    _In_ CONST D3DKMT_SETVIDPNSOURCEOWNER *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETVIDPNSOURCEOWNER,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

/* ---- Device state -------------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTGetDeviceState(
    _Inout_ D3DKMT_GETDEVICESTATE *pData)
{
    if (!pData)
        return STATUS_INVALID_PARAMETER;

    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETDEVICESTATE,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

/* ---- Additional D3DKMT operations ---------------------------------------- */
/* These cover the remaining SSDT entries not yet bridged above.              */

/* WDDM 1.2 additions */
#define IOCTL_D3DKMT_ENUMADAPTERS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ENUMADAPTERS2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENADAPTERFROMLUID \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x102, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OFFERALLOCATIONS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RECLAIMALLOCATIONS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETVIDPNSOURCEOWNER1 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B6, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* WDDM 2.0 residency / paging queue / video memory */
#define IOCTL_D3DKMT_MAKERESIDENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x180, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_EVICT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x181, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEPAGINGQUEUE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x186, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYPAGINGQUEUE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x187, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYVIDEOMEMORYINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x188, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* WDDM 2.0 GPU virtual addressing */
#define IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x182, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x183, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x184, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x185, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x189, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x18A, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_D3DKMT_CHECKMONITORPOWERSTATE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x168, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x165, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CHECKOCCLUSION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x169, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_FLIPOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UPDATEOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x126, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x125, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETMULTISAMPLEMETHODLIST \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETPRESENTHISTORY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x142, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETRUNTIMEDATA \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x171, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETSCANLINE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x167, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_INVALIDATEACTIVEVIDPN \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x178, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_POLLDISPLAYCHILDREN \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x179, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x163, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYSTATISTICS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x172, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RELEASEPROCESSVIDPNSOURCEOWNERS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETALLOCATIONPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x162, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x177, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETGAMMARAMP \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETQUEUEDLIMIT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORIDLE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x166, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS
APIENTRY
D3DKMTCheckMonitorPowerState(
    _In_ CONST D3DKMT_CHECKMONITORPOWERSTATE *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CHECKMONITORPOWERSTATE,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTCheckOcclusion(
    _In_ CONST D3DKMT_CHECKOCCLUSION *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CHECKOCCLUSION,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTCreateOverlay(
    _Inout_ D3DKMT_CREATEOVERLAY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CREATEOVERLAY,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTDestroyOverlay(
    _In_ CONST D3DKMT_DESTROYOVERLAY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYOVERLAY,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTFlipOverlay(
    _In_ CONST D3DKMT_FLIPOVERLAY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_FLIPOVERLAY,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTUpdateOverlay(
    _In_ CONST D3DKMT_UPDATEOVERLAY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_UPDATEOVERLAY,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTGetContextSchedulingPriority(
    _Inout_ D3DKMT_GETCONTEXTSCHEDULINGPRIORITY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTSetContextSchedulingPriority(
    _In_ CONST D3DKMT_SETCONTEXTSCHEDULINGPRIORITY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTGetMultisampleMethodList(
    _Inout_ D3DKMT_GETMULTISAMPLEMETHODLIST *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETMULTISAMPLEMETHODLIST,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTGetPresentHistory(
    _Inout_ D3DKMT_GETPRESENTHISTORY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETPRESENTHISTORY,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTGetRuntimeData(
    _In_ CONST D3DKMT_GETRUNTIMEDATA *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETRUNTIMEDATA,
                               (PVOID)pData, sizeof(*pData),
                               (PVOID)pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTGetScanLine(
    _Inout_ D3DKMT_GETSCANLINE *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_GETSCANLINE,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTInvalidateActiveVidPn(
    _In_ CONST D3DKMT_INVALIDATEACTIVEVIDPN *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_INVALIDATEACTIVEVIDPN,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTPollDisplayChildren(
    _In_ CONST D3DKMT_POLLDISPLAYCHILDREN *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_POLLDISPLAYCHILDREN,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTQueryAllocationResidency(
    _In_ CONST D3DKMT_QUERYALLOCATIONRESIDENCY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY,
                               (PVOID)pData, sizeof(*pData),
                               (PVOID)pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTQueryStatistics(
    _In_ CONST D3DKMT_QUERYSTATISTICS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_QUERYSTATISTICS,
                               (PVOID)pData, sizeof(*pData),
                               (PVOID)pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTReleaseProcessVidPnSourceOwners(
    _In_ HANDLE hProcess)
{
    if (!hProcess) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_RELEASEPROCESSVIDPNSOURCEOWNERS,
                               &hProcess, sizeof(hProcess), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTSetAllocationPriority(
    _In_ CONST D3DKMT_SETALLOCATIONPRIORITY *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETALLOCATIONPRIORITY,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTSetDisplayPrivateDriverFormat(
    _In_ CONST D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTSetGammaRamp(
    _In_ CONST D3DKMT_SETGAMMARAMP *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETGAMMARAMP,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTSetQueuedLimit(
    _In_ CONST D3DKMT_SETQUEUEDLIMIT *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETQUEUEDLIMIT,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTWaitForIdle(
    _In_ CONST D3DKMT_WAITFORIDLE *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_WAITFORIDLE,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTWaitForVerticalBlankEvent(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTCheckVidPnExclusiveOwnership(
    _In_ CONST D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

/* ---- WDDM 1.2 additions ------------------------------------------------ */

NTSTATUS
APIENTRY
D3DKMTEnumAdapters(
    _Inout_ D3DKMT_ENUMADAPTERS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_ENUMADAPTERS,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromLuid(
    _Inout_ D3DKMT_OPENADAPTERFROMLUID *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

/*
 * D3DKMTEnumAdapters2 — two-pass enumeration into a caller-provided array.
 *
 * Unlike EnumAdapters (which carries an inline Adapters[] array marshalled by
 * METHOD_BUFFERED), EnumAdapters2 uses an external pAdapters pointer.  Marshal
 * it through a nonpaged kernel buffer so dxgkrnl writes to a kernel address,
 * then copy the result back into the caller's array under SEH.
 */
NTSTATUS
APIENTRY
D3DKMTEnumAdapters2(
    _Inout_ D3DKMT_ENUMADAPTERS2 *pData)
{
    D3DKMT_ENUMADAPTERS2 Captured;
    D3DKMT_ADAPTERINFO  *KernelAdapters = NULL;
    SIZE_T               BufSize;
    NTSTATUS             Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    /* Pass 1: count query (no output array). */
    if (Captured.pAdapters == NULL || Captured.NumAdapters == 0)
    {
        Captured.pAdapters = NULL;
        Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_ENUMADAPTERS2,
                                     &Captured, sizeof(Captured),
                                     &Captured, sizeof(Captured));
        _SEH2_TRY
        {
            pData->NumAdapters = Captured.NumAdapters;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        return Status;
    }

    if (Captured.NumAdapters > 256)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSizeForCount(Captured.NumAdapters,
                                    sizeof(D3DKMT_ADAPTERINFO),
                                    &BufSize);
    if (!NT_SUCCESS(Status))
        return Status;

    KernelAdapters = ExAllocatePoolWithTag(NonPagedPool, BufSize, TAG_WDDM_BRIDGE);
    if (KernelAdapters == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(KernelAdapters, BufSize);
    Captured.pAdapters = KernelAdapters;

    /* Pass 2: fill the (kernel) buffer, then copy back to the user array. */
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_ENUMADAPTERS2,
                                 &Captured, sizeof(Captured),
                                 &Captured, sizeof(Captured));
    if (NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            RtlCopyMemory(pData->pAdapters, KernelAdapters,
                          (SIZE_T)Captured.NumAdapters * sizeof(D3DKMT_ADAPTERINFO));
            pData->NumAdapters = Captured.NumAdapters;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    ExFreePoolWithTag(KernelAdapters, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTOfferAllocations(
    _In_ CONST D3DKMT_OFFERALLOCATIONS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_OFFERALLOCATIONS,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTReclaimAllocations(
    _Inout_ D3DKMT_RECLAIMALLOCATIONS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_RECLAIMALLOCATIONS,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner1(
    _In_ CONST D3DKMT_SETVIDPNSOURCEOWNER1 *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SETVIDPNSOURCEOWNER1,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTWaitForVerticalBlankEvent2(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT2 *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT2,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTCreateSynchronizationObject2(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT2,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObject2(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObject2(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2,
                               (PVOID)pData, sizeof(*pData), NULL, 0);
}

/* ---- WDDM 2.0 additions ------------------------------------------------ */

NTSTATUS
APIENTRY
D3DKMTMakeResident(
    _Inout_ D3DDDI_MAKERESIDENT *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_MAKERESIDENT,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTEvict(
    _Inout_ D3DKMT_EVICT *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_EVICT,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTQueryVideoMemoryInfo(
    _Inout_ D3DKMT_QUERYVIDEOMEMORYINFO *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_QUERYVIDEOMEMORYINFO,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTCreatePagingQueue(
    _Inout_ D3DKMT_CREATEPAGINGQUEUE *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_CREATEPAGINGQUEUE,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTDestroyPagingQueue(
    _Inout_ D3DDDI_DESTROYPAGINGQUEUE *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYPAGINGQUEUE,
                               pData, sizeof(*pData),
                               NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTReserveGpuVirtualAddress(
    _Inout_ D3DDDI_RESERVEGPUVIRTUALADDRESS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTMapGpuVirtualAddress(
    _Inout_ D3DDDI_MAPGPUVIRTUALADDRESS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS,
                               pData, sizeof(*pData),
                               pData, sizeof(*pData));
}

NTSTATUS
APIENTRY
D3DKMTFreeGpuVirtualAddress(
    _In_ CONST D3DKMT_FREEGPUVIRTUALADDRESS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTUpdateGpuVirtualAddress(
    _In_ CONST D3DKMT_UPDATEGPUVIRTUALADDRESS *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    if (!pData) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU,
                               (PVOID)pData, sizeof(*pData),
                               NULL, 0);
}
