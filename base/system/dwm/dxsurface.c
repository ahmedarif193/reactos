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

typedef struct _DWM_D3DKMT_GETSHAREDRESOURCEADAPTERLUID
{
    D3DKMT_HANDLE hGlobalShare;
    HANDLE hNtHandle;
    LUID AdapterLuid;
} DWM_D3DKMT_GETSHAREDRESOURCEADAPTERLUID;

typedef NTSTATUS (APIENTRY *PFN_DWM_D3DKMT_GETSHAREDRESOURCEADAPTERLUID)(
    DWM_D3DKMT_GETSHAREDRESOURCEADAPTERLUID *Query);

/* OpenAdapterFromLuid is a public Win8 D3DKMT thunk.  Keep this Win10/11 DWM
 * consumer buildable at the Intel KMD's older DXGKDDI header level by using
 * the fixed public thunk layout instead of raising the miniport ABI view. */
typedef struct _DWM_D3DKMT_OPENADAPTERFROMLUID
{
    LUID AdapterLuid;
    D3DKMT_HANDLE hAdapter;
} DWM_D3DKMT_OPENADAPTERFROMLUID;

typedef NTSTATUS (APIENTRY *PFN_DWM_D3DKMT_OPENADAPTERFROMLUID)(
    DWM_D3DKMT_OPENADAPTERFROMLUID *OpenAdapter);

typedef struct _DWM_DX_VIEW
{
    ULONG GlobalShare;
    ULONG Generation;
    ULONG SurfaceId;
    BOOL Redirection;
    ULONG DeviceIndex;
    ULONG LastSeenFrame;
    D3DKMT_HANDLE hResource;
    D3DKMT_HANDLE hAllocation;
    BYTE *Snapshot;
    ULONG Bytes;
    ULONGLONG LastUpdateId;
} DWM_DX_VIEW;

typedef struct _DWM_DX_SOURCE
{
    ULONG GlobalShare;
    ULONG Generation;
    ULONG SurfaceId;
    BOOL Redirection;
    const LUID *AdapterLuid;
    ULONGLONG UpdateId;
    ULONG Width;
    ULONG Height;
    ULONG Pitch;
    ULONG Format;
} DWM_DX_SOURCE;

static DWM_DX_DEVICE g_Devices[DWM_DX_MAX_DEVICES];
static DWM_DX_VIEW g_Views[DWM_DX_MAX_VIEWS];
static ULONG g_CurrentFrame;
static PFN_DWM_D3DKMT_INVALIDATECACHE g_InvalidateCache;
static BOOL g_InvalidateCacheResolved;
static PFN_DWM_D3DKMT_GETSHAREDRESOURCEADAPTERLUID g_GetSharedResourceAdapterLuid;
static BOOL g_GetSharedResourceAdapterLuidResolved;
static PFN_DWM_D3DKMT_OPENADAPTERFROMLUID g_OpenAdapterFromLuid;
static BOOL g_OpenAdapterFromLuidResolved;

static BOOL
DwmDxLuidEqual(const LUID *Left, const LUID *Right)
{
    return Left->LowPart == Right->LowPart &&
           Left->HighPart == Right->HighPart;
}

static NTSTATUS
DwmDxGetDevice(const LUID *Luid, ULONG *DeviceIndex)
{
    DWM_D3DKMT_OPENADAPTERFROMLUID OpenAdapter;
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

    if (!g_OpenAdapterFromLuidResolved)
    {
        HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");

        if (Gdi32 != NULL)
        {
            g_OpenAdapterFromLuid =
                (PFN_DWM_D3DKMT_OPENADAPTERFROMLUID)
                    GetProcAddress(Gdi32, "D3DKMTOpenAdapterFromLuid");
        }
        g_OpenAdapterFromLuidResolved = TRUE;
    }
    if (g_OpenAdapterFromLuid == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    OpenAdapter.AdapterLuid = *Luid;
    Status = g_OpenAdapterFromLuid(&OpenAdapter);
    if (!NT_SUCCESS(Status) || OpenAdapter.hAdapter == 0)
        return NT_SUCCESS(Status) ? STATUS_NOT_FOUND : Status;

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

static BOOL
DwmDxDropView(DWM_DX_VIEW *View)
{
    if (View->hResource != 0 &&
        View->DeviceIndex < DWM_DX_MAX_DEVICES &&
        g_Devices[View->DeviceIndex].hDevice != 0)
    {
        D3DKMT_DESTROYALLOCATION Destroy;
        NTSTATUS Status;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_Devices[View->DeviceIndex].hDevice;
        Destroy.hResource = View->hResource;
        Status = D3DKMTDestroyAllocation(&Destroy);
        if (!NT_SUCCESS(Status) &&
            Status != STATUS_INVALID_PARAMETER &&
            Status != STATUS_INVALID_HANDLE &&
            Status != STATUS_DEVICE_REMOVED &&
            Status != STATUS_GRAPHICS_ALLOCATION_CLOSED &&
            Status != STATUS_GRAPHICS_INVALID_ALLOCATION_HANDLE)
        {
            OutputDebugStringA(
                "DWM: cached shared surface close failed; retrying\n");
            return FALSE;
        }
    }
    if (View->Snapshot != NULL)
        HeapFree(GetProcessHeap(), 0, View->Snapshot);
    RtlZeroMemory(View, sizeof(*View));
    return TRUE;
}

static BOOL
DwmDxOpenView(const DWM_DX_SOURCE *Source, DWM_DX_VIEW *View)
{
    DWM_DX_SHARED_SURFACE_INFO RuntimeInfo;
    D3DKMT_QUERYRESOURCEINFO Query;
    D3DKMT_OPENRESOURCE Open;
    D3DDDI_OPENALLOCATIONINFO *Allocations = NULL;
    PVOID ResourcePrivate = NULL, TotalPrivate = NULL;
    ULONG DeviceIndex;
    ULONGLONG Bytes;
    LUID AdapterLuid;
    NTSTATUS Status;

    if (Source->AdapterLuid != NULL)
    {
        AdapterLuid = *Source->AdapterLuid;
    }
    else
    {
        DWM_D3DKMT_GETSHAREDRESOURCEADAPTERLUID QueryLuid;

        if (!g_GetSharedResourceAdapterLuidResolved)
        {
            HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");

            if (Gdi32 != NULL)
            {
                g_GetSharedResourceAdapterLuid =
                    (PFN_DWM_D3DKMT_GETSHAREDRESOURCEADAPTERLUID)
                        GetProcAddress(Gdi32,
                                       "D3DKMTGetSharedResourceAdapterLuid");
            }
            g_GetSharedResourceAdapterLuidResolved = TRUE;
        }
        if (g_GetSharedResourceAdapterLuid == NULL)
            return FALSE;

        RtlZeroMemory(&QueryLuid, sizeof(QueryLuid));
        QueryLuid.hGlobalShare = Source->GlobalShare;
        Status = g_GetSharedResourceAdapterLuid(&QueryLuid);
        if (!NT_SUCCESS(Status))
            return FALSE;
        AdapterLuid = QueryLuid.AdapterLuid;
    }

    Status = DwmDxGetDevice(&AdapterLuid, &DeviceIndex);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlZeroMemory(&RuntimeInfo, sizeof(RuntimeInfo));
    RtlZeroMemory(&Query, sizeof(Query));
    Query.hDevice = g_Devices[DeviceIndex].hDevice;
    Query.hGlobalShare = Source->GlobalShare;
    Query.pPrivateRuntimeData = &RuntimeInfo;
    Query.PrivateRuntimeDataSize = sizeof(RuntimeInfo);
    Status = D3DKMTQueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status) || Query.NumAllocations != 1 ||
        Query.PrivateRuntimeDataSize != sizeof(RuntimeInfo) ||
        Query.ResourcePrivateDriverDataSize > DWM_DX_MAX_PRIVATE ||
        Query.TotalPrivateDriverDataSize > DWM_DX_MAX_PRIVATE ||
        RuntimeInfo.Magic != DWM_DX_SURFACE_INFO_MAGIC ||
        RuntimeInfo.Version != DWM_DX_SURFACE_INFO_VERSION ||
        RuntimeInfo.Width != Source->Width ||
        RuntimeInfo.Height != Source->Height ||
        RuntimeInfo.Pitch != Source->Pitch ||
        RuntimeInfo.Format != Source->Format)
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
    Open.hGlobalShare = Source->GlobalShare;
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

    View->Snapshot = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)Bytes);
    if (View->Snapshot == NULL)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = g_Devices[DeviceIndex].hDevice;
        Destroy.hResource = Open.hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        goto Failure;
    }

    View->GlobalShare = Source->GlobalShare;
    View->Generation = Source->Generation;
    View->SurfaceId = Source->SurfaceId;
    View->Redirection = Source->Redirection;
    View->DeviceIndex = DeviceIndex;
    View->hResource = Open.hResource;
    View->hAllocation = Allocations[0].hAllocation;
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

static const BYTE *
DwmDxGetSnapshot(const DWM_DX_SOURCE *Source)
{
    DWM_DX_VIEW *View = NULL, *FreeView = NULL;
    DWM_DX_VIEW *SameSurface = NULL, *Oldest = NULL;
    ULONG Index;

    if (Source == NULL || Source->GlobalShare == 0 ||
        Source->Generation == 0 || Source->UpdateId == 0 ||
        Source->Width == 0 || Source->Height == 0 ||
        Source->Pitch == 0 || Source->Format == 0)
    {
        return NULL;
    }

    for (Index = 0; Index < DWM_DX_MAX_VIEWS; ++Index)
    {
        if (g_Views[Index].GlobalShare == Source->GlobalShare &&
            g_Views[Index].Generation == Source->Generation &&
            g_Views[Index].Redirection == Source->Redirection)
        {
            View = &g_Views[Index];
            break;
        }
        if (g_Views[Index].GlobalShare == 0 && FreeView == NULL)
            FreeView = &g_Views[Index];
        if (g_Views[Index].GlobalShare != 0 &&
            g_Views[Index].SurfaceId == Source->SurfaceId &&
            g_Views[Index].Redirection == Source->Redirection &&
            (SameSurface == NULL ||
             g_Views[Index].LastSeenFrame < SameSurface->LastSeenFrame))
        {
            SameSurface = &g_Views[Index];
        }
        if (g_Views[Index].GlobalShare != 0 &&
            (Oldest == NULL ||
             g_Views[Index].LastSeenFrame < Oldest->LastSeenFrame))
        {
            Oldest = &g_Views[Index];
        }
    }

    if (View == NULL)
    {
        View = SameSurface != NULL ? SameSurface :
               (FreeView != NULL ? FreeView : Oldest);
        if (View == NULL)
            return NULL;
        if (View->GlobalShare != 0 && !DwmDxDropView(View))
            return NULL;
        if (!DwmDxOpenView(Source, View))
            return NULL;
    }
    View->LastSeenFrame = g_CurrentFrame;

    if (Source->UpdateId != View->LastUpdateId)
    {
        DWM_D3DKMT_INVALIDATECACHE Invalidate;
        D3DKMT_LOCK Lock;
        D3DKMT_UNLOCK Unlock;
        NTSTATUS Status;

        RtlZeroMemory(&Lock, sizeof(Lock));
        Lock.hDevice = g_Devices[View->DeviceIndex].hDevice;
        Lock.hAllocation = View->hAllocation;
        Lock.Flags.ReadOnly = 1;
        Lock.Flags.LockEntire = 1;
        Status = D3DKMTLock(&Lock);
        if (!NT_SUCCESS(Status) || Lock.pData == NULL)
            return View->LastUpdateId != 0 ? View->Snapshot : NULL;

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
        Status = STATUS_NOT_SUPPORTED;
        if (g_InvalidateCache != NULL)
            Status = g_InvalidateCache(&Invalidate);
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(View->Snapshot, Lock.pData, View->Bytes);
            View->LastUpdateId = Source->UpdateId;
        }

        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = g_Devices[View->DeviceIndex].hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &View->hAllocation;
        (void)D3DKMTUnlock(&Unlock);
    }

    return View->LastUpdateId != 0 ? View->Snapshot : NULL;
}

const BYTE *
DwmDxGetSurfaceSnapshot(const DWM_WIN *Window)
{
    DWM_DX_SOURCE Source;

    if (Window == NULL)
        return NULL;

    RtlZeroMemory(&Source, sizeof(Source));
    Source.GlobalShare = Window->DxGlobalShare;
    Source.Generation = Window->DxGeneration;
    Source.SurfaceId = Window->SurfaceId;
    Source.AdapterLuid = &Window->DxAdapterLuid;
    Source.UpdateId = Window->DxUpdateId;
    Source.Width = Window->DxWidth;
    Source.Height = Window->DxHeight;
    Source.Pitch = Window->DxPitch;
    Source.Format = Window->DxFormat;
    return DwmDxGetSnapshot(&Source);
}

const BYTE *
DwmDxGetRedirectionSnapshot(const DWM_WIN *Window)
{
    DWM_DX_SOURCE Source;

    if (Window == NULL)
        return NULL;

    RtlZeroMemory(&Source, sizeof(Source));
    Source.GlobalShare = Window->BaseGlobalShare;
    Source.Generation = Window->BaseGeneration;
    Source.SurfaceId = Window->SurfaceId;
    Source.Redirection = TRUE;
    Source.UpdateId = Window->BaseUpdateId;
    Source.Width = Window->BaseWidth;
    Source.Height = Window->BaseHeight;
    Source.Pitch = Window->BasePitch;
    Source.Format = Window->BaseFormat;
    return DwmDxGetSnapshot(&Source);
}

void
DwmDxAcknowledgeSurface(const DWM_WIN *Window)
{
    static struct
    {
        ULONG SurfaceId, Generation, Share, DxGeneration;
        ULONGLONG UpdateId;
    } Acknowledged[DWM_MAX_WINDOWS];
    DWM_DX_SURFACE_EXCHANGE Exchange;
    ULONG Index;

    if (Window == NULL || Window->DxGlobalShare == 0 ||
        Window->DxGeneration == 0 || Window->DxUpdateId == 0)
    {
        return;
    }

    Index = Window->SurfaceId % ARRAYSIZE(Acknowledged);
    if (Acknowledged[Index].SurfaceId == Window->SurfaceId &&
        Acknowledged[Index].Generation == Window->Generation &&
        Acknowledged[Index].Share == Window->DxGlobalShare &&
        Acknowledged[Index].DxGeneration == Window->DxGeneration &&
        Acknowledged[Index].UpdateId == Window->DxUpdateId)
    {
        return;
    }

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_CONSUMED;
    Exchange.SurfaceId = Window->SurfaceId;
    Exchange.Generation = Window->DxGeneration;
    Exchange.UpdateId = Window->DxUpdateId;
    /* Failed acknowledgements must be retried; a reused surface slot or a
     * replaced shared resource must not inherit another publication's ack. */
    if ((LONG)NtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE) >= 0)
    {
        Acknowledged[Index].SurfaceId = Window->SurfaceId;
        Acknowledged[Index].Generation = Window->Generation;
        Acknowledged[Index].Share = Window->DxGlobalShare;
        Acknowledged[Index].DxGeneration = Window->DxGeneration;
        Acknowledged[Index].UpdateId = Window->DxUpdateId;
    }
}

typedef struct _DWM_DX_HELD_FRAME
{
    ULONG SurfaceId;
    ULONG Generation;
    ULONGLONG UpdateId;
} DWM_DX_HELD_FRAME;

/* Indexed by SurfaceId. A slot left by a torn-down window keeps a tuple that
 * win32k already released; releasing it again when the slot is reused is a
 * harmless STATUS_NOT_FOUND, since update IDs are never reused. */
static DWM_DX_HELD_FRAME g_HeldFrames[DWM_MAX_SURFACES];

static void
DwmDxReleaseFrame(const DWM_DX_HELD_FRAME *Held)
{
    DWM_DX_SURFACE_EXCHANGE Exchange;

    RtlZeroMemory(&Exchange, sizeof(Exchange));
    Exchange.StructSize = sizeof(Exchange);
    Exchange.Action = DWM_DX_SURFACE_RELEASE;
    Exchange.SurfaceId = Held->SurfaceId;
    Exchange.Generation = Held->Generation;
    Exchange.UpdateId = Held->UpdateId;
    /* Window teardown may already have released it. */
    (void)NtUserCallOneParam((DWORD_PTR)&Exchange, DWM_ROUTINE_DXSURFACE);
}

/*
 * DWM owns a retained client frame from the GETFRAME that reports it until
 * a later GETFRAME reports a newer frame of that surface. DWM fetches a frame
 * only after the GPU finished reading the previous one, so the older frame
 * is released here, whether or not any compositor path sampled it. A window
 * that is merely absent from this frame keeps its frame: it can reappear
 * without a new present, and window teardown releases it in win32k.
 */
void
DwmDxHoldFrames(const DWM_WIN *Windows, ULONG Count)
{
    ULONG Index;

    if ((Count != 0 && Windows == NULL) || Count > DWM_MAX_WINDOWS)
        return;

    for (Index = 0; Index < Count; ++Index)
    {
        const DWM_WIN *Window = &Windows[Index];
        DWM_DX_HELD_FRAME *Held;

        if (!(Window->LayerFlags & DWM_WINDOW_DX_RETAINED) || Window->DxUpdateId == 0 ||
            Window->SurfaceId >= ARRAYSIZE(g_HeldFrames))
            continue;
        Held = &g_HeldFrames[Window->SurfaceId];
        if (Held->Generation == Window->DxGeneration && Held->UpdateId == Window->DxUpdateId)
            continue;
        if (Held->UpdateId != 0)
            DwmDxReleaseFrame(Held);
        Held->SurfaceId = Window->SurfaceId;
        Held->Generation = Window->DxGeneration;
        Held->UpdateId = Window->DxUpdateId;
    }
}

void
DwmDxSweepSurfaces(const DWM_WIN *Windows, ULONG Count)
{
    ULONG Index, WindowIndex;

    if ((Count != 0 && Windows == NULL) || Count > DWM_MAX_WINDOWS)
        return;

    ++g_CurrentFrame;
    for (Index = 0; Index < DWM_DX_MAX_VIEWS; ++Index)
    {
        DWM_DX_VIEW *View = &g_Views[Index];
        BOOL Present = FALSE;

        if (View->GlobalShare == 0)
            continue;

        for (WindowIndex = 0; WindowIndex < Count; ++WindowIndex)
        {
            const DWM_WIN *Window = &Windows[WindowIndex];

            if (Window->SurfaceId != View->SurfaceId)
                continue;

            if (View->Redirection)
            {
                Present =
                    Window->BaseGlobalShare == View->GlobalShare &&
                    Window->BaseGeneration == View->Generation;
            }
            else
            {
                Present =
                    Window->DxGlobalShare == View->GlobalShare &&
                    Window->DxGeneration == View->Generation;
            }
            if (Present)
                break;
        }

        if (!Present)
            DwmDxDropView(View);
    }
}

void
DwmDxCleanupSurfaces(void)
{
    ULONG Index;

    /* Release locked mappings before destroying their owning devices. */
    for (Index = 0; Index < DWM_DX_MAX_VIEWS; ++Index)
        DwmDxDropView(&g_Views[Index]);

    /* Composition has stopped, so no retained frame is read any more. */
    for (Index = 0; Index < ARRAYSIZE(g_HeldFrames); ++Index)
    {
        if (g_HeldFrames[Index].UpdateId != 0)
            DwmDxReleaseFrame(&g_HeldFrames[Index]);
    }
    RtlZeroMemory(g_HeldFrames, sizeof(g_HeldFrames));

    for (Index = 0; Index < DWM_DX_MAX_DEVICES; ++Index)
    {
        DWM_DX_DEVICE *Device = &g_Devices[Index];

        if (Device->hDevice != 0)
        {
            D3DKMT_DESTROYDEVICE DestroyDevice;

            RtlZeroMemory(&DestroyDevice, sizeof(DestroyDevice));
            DestroyDevice.hDevice = Device->hDevice;
            (void)D3DKMTDestroyDevice(&DestroyDevice);
        }
        if (Device->hAdapter != 0)
        {
            D3DKMT_CLOSEADAPTER CloseAdapter;

            RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
            CloseAdapter.hAdapter = Device->hAdapter;
            (void)D3DKMTCloseAdapter(&CloseAdapter);
        }
        RtlZeroMemory(Device, sizeof(*Device));
    }
    g_CurrentFrame = 0;
}
