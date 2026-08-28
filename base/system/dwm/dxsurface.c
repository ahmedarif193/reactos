/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT shared-surface consumer for desktop composition
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif

#include <windows.h>
#include <d3dkmthk.h>
#include <reactos/dwmframe.h>

#include "dxsurface.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR Param, DWORD Routine);

#define DWM_DX_MAX_DEVICES 8
#define DWM_DX_MAX_VIEWS   (DWM_MAX_WINDOWS * 2)
#define DWM_DX_MAX_PRIVATE (1024u * 1024u)

typedef struct _DWM_DX_DEVICE
{
    LUID Luid;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
} DWM_DX_DEVICE;

typedef struct _DWM_D3DKMT_INVALIDATECACHE
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    D3DKMT_ALIGN64 D3DKMT_SIZE_T Offset;
    D3DKMT_ALIGN64 D3DKMT_SIZE_T Length;
} DWM_D3DKMT_INVALIDATECACHE;

typedef NTSTATUS (APIENTRY *PFN_DWM_D3DKMT_INVALIDATECACHE)(
    const DWM_D3DKMT_INVALIDATECACHE *Invalidate);

typedef struct _DWM_DX_VIEW
{
    ULONG GlobalShare;
    ULONG Generation;
    ULONG DeviceIndex;
    ULONG LastSeenFrame;
    D3DKMT_HANDLE hResource;
    D3DKMT_HANDLE hAllocation;
    BYTE *Mapping;
    BYTE *Snapshot;
    ULONG Bytes;
    ULONGLONG LastUpdateId;
} DWM_DX_VIEW;

static DWM_DX_DEVICE g_Devices[DWM_DX_MAX_DEVICES];
static DWM_DX_VIEW g_Views[DWM_DX_MAX_VIEWS];
static ULONG g_CurrentFrame;
static PFN_DWM_D3DKMT_INVALIDATECACHE g_InvalidateCache;
static BOOL g_InvalidateCacheResolved;

static BOOL
DwmDxLuidEqual(const LUID *Left, const LUID *Right)
{
    return Left->LowPart == Right->LowPart &&
           Left->HighPart == Right->HighPart;
}

static NTSTATUS
DwmDxGetDevice(const LUID *Luid, ULONG *DeviceIndex)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_CREATEDEVICE CreateDevice;
    ULONG Index, FreeIndex = DWM_DX_MAX_DEVICES;
    NTSTATUS Status;

    for (Index = 0; Index < DWM_DX_MAX_DEVICES; ++Index)
    {
        if (g_Devices[Index].hDevice != 0 &&
            DwmDxLuidEqual(&g_Devices[Index].Luid, Luid))
        {
            *DeviceIndex = Index;
            return STATUS_SUCCESS;
        }
        if (g_Devices[Index].hDevice == 0 && FreeIndex == DWM_DX_MAX_DEVICES)
            FreeIndex = Index;
    }
    if (FreeIndex == DWM_DX_MAX_DEVICES)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    lstrcpynW(OpenAdapter.DeviceName, L"\\\\.\\DISPLAY1",
              ARRAYSIZE(OpenAdapter.DeviceName));
    Status = D3DKMTOpenAdapterFromGdiDisplayName(&OpenAdapter);
    if (!NT_SUCCESS(Status) || OpenAdapter.hAdapter == 0)
        return NT_SUCCESS(Status) ? STATUS_NOT_FOUND : Status;
    if (!DwmDxLuidEqual(&OpenAdapter.AdapterLuid, Luid))
    {
        D3DKMT_CLOSEADAPTER CloseAdapter;

        RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
        CloseAdapter.hAdapter = OpenAdapter.hAdapter;
        (void)D3DKMTCloseAdapter(&CloseAdapter);
        return STATUS_NOT_FOUND;
    }

    RtlZeroMemory(&CreateDevice, sizeof(CreateDevice));
    CreateDevice.hAdapter = OpenAdapter.hAdapter;
    Status = D3DKMTCreateDevice(&CreateDevice);
    if (!NT_SUCCESS(Status) || CreateDevice.hDevice == 0)
    {
        D3DKMT_CLOSEADAPTER CloseAdapter;

        RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
        CloseAdapter.hAdapter = OpenAdapter.hAdapter;
        (void)D3DKMTCloseAdapter(&CloseAdapter);
        return NT_SUCCESS(Status) ? STATUS_INVALID_DEVICE_STATE : Status;
    }

    g_Devices[FreeIndex].Luid = *Luid;
    g_Devices[FreeIndex].hAdapter = OpenAdapter.hAdapter;
    g_Devices[FreeIndex].hDevice = CreateDevice.hDevice;
    *DeviceIndex = FreeIndex;
    return STATUS_SUCCESS;
}

static VOID
DwmDxDropView(DWM_DX_VIEW *View)
{
    if (View->Mapping != NULL &&
        View->DeviceIndex < DWM_DX_MAX_DEVICES &&
        g_Devices[View->DeviceIndex].hDevice != 0)
    {
        D3DKMT_UNLOCK Unlock;

        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = g_Devices[View->DeviceIndex].hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &View->hAllocation;
        (void)D3DKMTUnlock(&Unlock);
    }
    if (View->hResource != 0 &&
        View->DeviceIndex < DWM_DX_MAX_DEVICES &&
        g_Devices[View->DeviceIndex].hDevice != 0)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_Devices[View->DeviceIndex].hDevice;
        Destroy.hResource = View->hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
    }
    if (View->Snapshot != NULL)
        HeapFree(GetProcessHeap(), 0, View->Snapshot);
    RtlZeroMemory(View, sizeof(*View));
}

static BOOL
DwmDxOpenView(const DWM_WIN *Window, DWM_DX_VIEW *View)
{
    DWM_DX_SHARED_SURFACE_INFO RuntimeInfo;
    D3DKMT_QUERYRESOURCEINFO Query;
    D3DKMT_OPENRESOURCE Open;
    D3DDDI_OPENALLOCATIONINFO *Allocations = NULL;
    D3DKMT_LOCK Lock;
    PVOID ResourcePrivate = NULL, TotalPrivate = NULL;
    ULONG DeviceIndex;
    ULONGLONG Bytes;
    NTSTATUS Status;

    Status = DwmDxGetDevice(&Window->DxAdapterLuid, &DeviceIndex);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlZeroMemory(&RuntimeInfo, sizeof(RuntimeInfo));
    RtlZeroMemory(&Query, sizeof(Query));
    Query.hDevice = g_Devices[DeviceIndex].hDevice;
    Query.hGlobalShare = Window->DxGlobalShare;
    Query.pPrivateRuntimeData = &RuntimeInfo;
    Query.PrivateRuntimeDataSize = sizeof(RuntimeInfo);
    Status = D3DKMTQueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status) || Query.NumAllocations != 1 ||
        Query.PrivateRuntimeDataSize != sizeof(RuntimeInfo) ||
        Query.ResourcePrivateDriverDataSize > DWM_DX_MAX_PRIVATE ||
        Query.TotalPrivateDriverDataSize > DWM_DX_MAX_PRIVATE ||
        RuntimeInfo.Magic != DWM_DX_SURFACE_INFO_MAGIC ||
        RuntimeInfo.Version != DWM_DX_SURFACE_INFO_VERSION ||
        RuntimeInfo.Width != Window->DxWidth ||
        RuntimeInfo.Height != Window->DxHeight ||
        RuntimeInfo.Pitch != Window->DxPitch ||
        RuntimeInfo.Format != Window->DxFormat)
    {
        return FALSE;
    }

    Bytes = (ULONGLONG)RuntimeInfo.Pitch * RuntimeInfo.Height;
    if (Bytes == 0 || Bytes > ~(ULONG)0)
        return FALSE;

    Allocations = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            sizeof(*Allocations));
    if (Allocations == NULL)
        goto Failure;
    if (Query.ResourcePrivateDriverDataSize != 0)
        ResourcePrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    Query.ResourcePrivateDriverDataSize);
    if (Query.TotalPrivateDriverDataSize != 0)
        TotalPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                 Query.TotalPrivateDriverDataSize);
    if ((Query.ResourcePrivateDriverDataSize != 0 && ResourcePrivate == NULL) ||
        (Query.TotalPrivateDriverDataSize != 0 && TotalPrivate == NULL))
    {
        goto Failure;
    }

    RtlZeroMemory(&Open, sizeof(Open));
    Open.hDevice = g_Devices[DeviceIndex].hDevice;
    Open.hGlobalShare = Window->DxGlobalShare;
    Open.NumAllocations = 1;
    Open.pOpenAllocationInfo = Allocations;
    Open.pPrivateRuntimeData = &RuntimeInfo;
    Open.PrivateRuntimeDataSize = sizeof(RuntimeInfo);
    Open.pResourcePrivateDriverData = ResourcePrivate;
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = TotalPrivate;
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = D3DKMTOpenResource(&Open);
    if (!NT_SUCCESS(Status) || Open.hResource == 0 ||
        Allocations[0].hAllocation == 0)
    {
        goto Failure;
    }

    RtlZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice = g_Devices[DeviceIndex].hDevice;
    Lock.hAllocation = Allocations[0].hAllocation;
    Status = D3DKMTLock(&Lock);
    if (!NT_SUCCESS(Status) || Lock.pData == NULL)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_Devices[DeviceIndex].hDevice;
        Destroy.hResource = Open.hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        goto Failure;
    }

    View->Snapshot = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Bytes);
    if (View->Snapshot == NULL)
    {
        D3DKMT_UNLOCK Unlock;
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = g_Devices[DeviceIndex].hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Allocations[0].hAllocation;
        (void)D3DKMTUnlock(&Unlock);
        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_Devices[DeviceIndex].hDevice;
        Destroy.hResource = Open.hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        goto Failure;
    }

    View->GlobalShare = Window->DxGlobalShare;
    View->Generation = Window->DxGeneration;
    View->DeviceIndex = DeviceIndex;
    View->hResource = Open.hResource;
    View->hAllocation = Allocations[0].hAllocation;
    View->Mapping = Lock.pData;
    View->Bytes = (ULONG)Bytes;

    if (TotalPrivate != NULL)
        HeapFree(GetProcessHeap(), 0, TotalPrivate);
    if (ResourcePrivate != NULL)
        HeapFree(GetProcessHeap(), 0, ResourcePrivate);
    HeapFree(GetProcessHeap(), 0, Allocations);
    return TRUE;

Failure:
    if (TotalPrivate != NULL)
        HeapFree(GetProcessHeap(), 0, TotalPrivate);
    if (ResourcePrivate != NULL)
        HeapFree(GetProcessHeap(), 0, ResourcePrivate);
    if (Allocations != NULL)
        HeapFree(GetProcessHeap(), 0, Allocations);
    return FALSE;
}

const BYTE *
DwmDxGetSurfaceSnapshot(const DWM_WIN *Window)
{
    DWM_DX_VIEW *View = NULL, *FreeView = NULL, *Oldest = NULL;
    ULONG Index;

    if (Window == NULL || Window->DxGlobalShare == 0 ||
        Window->DxGeneration == 0 || Window->DxWidth == 0 ||
        Window->DxHeight == 0)
    {
        return NULL;
    }

    for (Index = 0; Index < DWM_DX_MAX_VIEWS; ++Index)
    {
        if (g_Views[Index].GlobalShare == Window->DxGlobalShare &&
            g_Views[Index].Generation == Window->DxGeneration)
        {
            View = &g_Views[Index];
            break;
        }
        if (g_Views[Index].GlobalShare == 0 && FreeView == NULL)
            FreeView = &g_Views[Index];
        if (g_Views[Index].GlobalShare != 0 &&
            (Oldest == NULL ||
             g_Views[Index].LastSeenFrame < Oldest->LastSeenFrame))
        {
            Oldest = &g_Views[Index];
        }
    }

    if (View == NULL)
    {
        View = FreeView != NULL ? FreeView : Oldest;
        if (View == NULL)
            return NULL;
        if (View->GlobalShare != 0)
            DwmDxDropView(View);
        if (!DwmDxOpenView(Window, View))
            return NULL;
    }
    View->LastSeenFrame = g_CurrentFrame;

    if (Window->DxUpdateId != 0 &&
        Window->DxUpdateId != View->LastUpdateId)
    {
        DWM_D3DKMT_INVALIDATECACHE Invalidate;

        if (!g_InvalidateCacheResolved)
        {
            HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");

            if (Gdi32 != NULL)
            {
                g_InvalidateCache =
                    (PFN_DWM_D3DKMT_INVALIDATECACHE)GetProcAddress(
                        Gdi32, "D3DKMTInvalidateCache");
            }
            g_InvalidateCacheResolved = TRUE;
        }

        RtlZeroMemory(&Invalidate, sizeof(Invalidate));
        Invalidate.hDevice = g_Devices[View->DeviceIndex].hDevice;
        Invalidate.hAllocation = View->hAllocation;
        Invalidate.Length = View->Bytes;
        if (g_InvalidateCache != NULL &&
            NT_SUCCESS(g_InvalidateCache(&Invalidate)))
        {
            RtlCopyMemory(View->Snapshot, View->Mapping, View->Bytes);
            View->LastUpdateId = Window->DxUpdateId;
        }
    }

    return View->LastUpdateId != 0 ? View->Snapshot : NULL;
}

void
DwmDxAcknowledgeSurface(const DWM_WIN *Window)
{
    DWM_DX_SURFACE_EXCHANGE Exchange;

    if (Window == NULL || Window->DxGlobalShare == 0 ||
        Window->DxGeneration == 0 || Window->DxUpdateId == 0)
    {
        return;
    }

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_CONSUMED;
    Exchange.SurfaceId = Window->SurfaceId;
    Exchange.Generation = Window->DxGeneration;
    Exchange.UpdateId = Window->DxUpdateId;
    (void)NtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
}

void
DwmDxSweepSurfaces(ULONG FrameSequence)
{
    ULONG Index;

    g_CurrentFrame = FrameSequence;
    for (Index = 0; Index < DWM_DX_MAX_VIEWS; ++Index)
    {
        if (g_Views[Index].GlobalShare != 0 &&
            FrameSequence - g_Views[Index].LastSeenFrame > 256)
        {
            DwmDxDropView(&g_Views[Index]);
        }
    }
}
