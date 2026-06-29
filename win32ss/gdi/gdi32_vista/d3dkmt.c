/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DKMT user-mode thunks with wrapper logic
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2025 ReactOS Contributors
 */

#include <gdi32_vista.h>
#include <d3dkmthk.h>

static NTSTATUS
D3dkmtValidateHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    return (Handle != 0) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}

static NTSTATUS
D3dkmtValidateSyncObjectArray(
    _In_reads_(Count) const D3DKMT_HANDLE *Handles,
    _In_ UINT Count,
    _In_ UINT Limit)
{
    UINT Index;

    if (Count == 0 || Count > Limit || Handles == NULL)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < Count; ++Index)
    {
        if (Handles[Index] == 0)
            return STATUS_INVALID_HANDLE;
    }

    return STATUS_SUCCESS;
}

static BOOL
D3dkmtIsNullTerminatedName(
    _In_reads_(Count) const WCHAR *Name,
    _In_ SIZE_T Count)
{
    SIZE_T Index;

    for (Index = 0; Index < Count; ++Index)
    {
        if (Name[Index] == UNICODE_NULL)
            return TRUE;
    }

    return FALSE;
}

/*
 * D3DKMTOpenAdapterFromGdiDisplayName
 *
 * This is NOT a simple syscall stub. It converts a GDI display name
 * (e.g., L"\\\\.\\DISPLAY1") to an HDC, calls OpenAdapterFromHdc,
 * copies the adapter handle / LUID / VidPnSourceId back, then
 * releases the temporary DC.
 */
NTSTATUS
WINAPI
D3DKMTOpenAdapterFromGdiDisplayName(
    _Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *pData)
{
    D3DKMT_OPENADAPTERFROMHDC OpenFromHdc;
    HDC hDC;
    NTSTATUS Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    if (!D3dkmtIsNullTerminatedName(pData->DeviceName,
                                    RTL_NUMBER_OF(pData->DeviceName)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    hDC = CreateDCW(pData->DeviceName, NULL, NULL, NULL);
    if (!hDC)
        return STATUS_UNSUCCESSFUL;

    RtlZeroMemory(&OpenFromHdc, sizeof(OpenFromHdc));
    OpenFromHdc.hDc = hDC;

    Status = NtGdiDdDDIOpenAdapterFromHdc(&OpenFromHdc);
    if (NT_SUCCESS(Status))
    {
        pData->hAdapter = OpenFromHdc.hAdapter;
        pData->AdapterLuid = OpenFromHdc.AdapterLuid;
        pData->VidPnSourceId = OpenFromHdc.VidPnSourceId;
    }

    DeleteDC(hDC);
    return Status;
}

/*
 * D3DKMTCreateAllocation
 *
 * Backward-compatibility wrapper: converts v1 D3DDDI_ALLOCATIONINFO
 * descriptors (per-allocation) to v2 D3DDDI_ALLOCATIONINFO2 format,
 * calls D3DKMTCreateAllocation2 (the native kernel interface), then
 * copies results back to the caller's v1 descriptors.
 */
NTSTATUS
WINAPI
D3DKMTCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *pData)
{
    NTSTATUS Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = D3dkmtValidateHandle(pData->hDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->NumAllocations != 0 && pData->pAllocationInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pData->PrivateDriverDataSize != 0 && pData->pPrivateDriverData == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * On Win8.1, this wrapper converts v1 D3DDDI_ALLOCATIONINFO to v2
     * and calls CreateAllocation2.  On ReactOS, both v1 and v2 are
     * handled identically by the same dxgkrnl IOCTL, so pass through.
     */
    return NtGdiDdDDICreateAllocation(pData);
}

/*
 * D3DKMTOpenResource
 *
 * Converts v1 D3DDDI_OPENALLOCATIONINFO descriptors to v2
 * D3DDDI_OPENALLOCATIONINFO2 format and calls OpenResource2.
 */
NTSTATUS
WINAPI
D3DKMTOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pData)
{
    NTSTATUS Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = D3dkmtValidateHandle(pData->hDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = D3dkmtValidateHandle(pData->hGlobalShare);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->NumAllocations != 0 && pData->pOpenAllocationInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * On Win8.1, this wrapper converts v1 to v2 open-allocation-info
     * and calls OpenResource2.  On ReactOS, both are the same IOCTL.
     */
    return NtGdiDdDDIOpenResource(pData);
}

/*
 * D3DKMTCreateSynchronizationObject
 *
 * Converts v1 D3DKMT_CREATESYNCHRONIZATIONOBJECT to v2 format
 * and calls CreateSynchronizationObject2.
 */
NTSTATUS
WINAPI
D3DKMTCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 V2Data;
    NTSTATUS Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = D3dkmtValidateHandle(pData->hDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->Info.Type <= 0 ||
        pData->Info.Type >= D3DDDI_SYNCHRONIZATION_TYPE_LIMIT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&V2Data, sizeof(V2Data));
    V2Data.hDevice = pData->hDevice;

    /*
     * Copy v1 synchronization object info into v2.
     * v1 Info is D3DDDI_SYNCHRONIZATIONOBJECTINFO (Type + anonymous union).
     * v2 Info is D3DDDI_SYNCHRONIZATIONOBJECTINFO2 (Type + Flags + anonymous union).
     * Copy Type, then the per-type data via the union members directly.
     */
    V2Data.Info.Type = pData->Info.Type;
    switch (pData->Info.Type)
    {
    case D3DDDI_SYNCHRONIZATION_MUTEX:
        V2Data.Info.SynchronizationMutex.InitialState =
            pData->Info.SynchronizationMutex.InitialState;
        break;
    case D3DDDI_SEMAPHORE:
        V2Data.Info.Semaphore.MaxCount = pData->Info.Semaphore.MaxCount;
        V2Data.Info.Semaphore.InitialCount = pData->Info.Semaphore.InitialCount;
        break;
    default:
        RtlCopyMemory(&V2Data.Info.Reserved, &pData->Info.Reserved,
                       sizeof(pData->Info.Reserved));
        break;
    }

    Status = NtGdiDdDDICreateSynchronizationObject2(&V2Data);

    if (NT_SUCCESS(Status))
    {
        pData->hSyncObject = V2Data.hSyncObject;
    }

    return Status;
}

/*
 * D3DKMTSignalSynchronizationObject
 *
 * Converts v1 to v2 signal format and calls SignalSynchronizationObject2.
 */
NTSTATUS
WINAPI
D3DKMTSignalSynchronizationObject(
    _In_ const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 V2Data;
    NTSTATUS Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = D3dkmtValidateHandle(pData->hContext);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = D3dkmtValidateSyncObjectArray(pData->ObjectHandleArray,
                                           pData->ObjectCount,
                                           D3DDDI_MAX_OBJECT_SIGNALED);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&V2Data, sizeof(V2Data));
    V2Data.hContext = pData->hContext;
    V2Data.ObjectCount = pData->ObjectCount;
    RtlCopyMemory(V2Data.ObjectHandleArray,
                   pData->ObjectHandleArray,
                   sizeof(pData->ObjectHandleArray));
    V2Data.Flags = pData->Flags;

    return NtGdiDdDDISignalSynchronizationObject2(&V2Data);
}

/*
 * D3DKMTWaitForSynchronizationObject
 *
 * Converts v1 to v2 wait format and calls WaitForSynchronizationObject2.
 */
NTSTATUS
WINAPI
D3DKMTWaitForSynchronizationObject(
    _In_ const D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 V2Data;
    NTSTATUS Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = D3dkmtValidateHandle(pData->hContext);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = D3dkmtValidateSyncObjectArray(pData->ObjectHandleArray,
                                           pData->ObjectCount,
                                           D3DDDI_MAX_OBJECT_WAITED_ON);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&V2Data, sizeof(V2Data));
    V2Data.hContext = pData->hContext;
    V2Data.ObjectCount = pData->ObjectCount;
    RtlCopyMemory(V2Data.ObjectHandleArray,
                   pData->ObjectHandleArray,
                   sizeof(pData->ObjectHandleArray));

    return NtGdiDdDDIWaitForSynchronizationObject2(&V2Data);
}
