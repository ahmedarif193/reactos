/*
 * PROJECT:     ReactOS softgpu user-mode display driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Linear 2D user-mode half of the softgpu WDDM driver
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

/*
 * Include order matters: d3dkmthk.h pulls in d3dukmdt.h, which owns the shared
 * D3DDDI_* types the UMD DDI builds on.
 */
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>
#include <reactos/drivers/directx/softgpu_2d_shared.h>

#define SOFTGPUUM_ADAPTER_MAGIC  0x53475541u /* 'SGUA' */
#define SOFTGPUUM_DEVICE_MAGIC   0x53475544u /* 'SGUD' */
#define SOFTGPUUM_RESOURCE_MAGIC 0x53475552u /* 'SGUR' */

#define SOFTGPUUM_MAX_SUBRESOURCES 32u

/* Linear 2D resources only. Texture means a blit-addressable linear surface,
 * not texture sampling; no 3D state or draw entry is exposed below. */
#define SOFTGPUUM_RESOURCE_FLAGS_ALLOWED \
    (0x00000001u | /* RenderTarget */ \
     0x00000004u | /* Dynamic */ \
     0x00000040u | /* WriteOnly */ \
     0x00000080u | /* NotLockable */ \
     0x00008000u | /* Primary */ \
     0x00010000u | /* Texture */ \
     0x01000000u)  /* CpuOptimized */

#define SOFTGPUUM_BLT_FLAGS_ALLOWED       0x00000700u
#define SOFTGPUUM_COLORFILL_FLAGS_ALLOWED 0x00000001u

typedef struct _SOFTGPUUM_ADAPTER SOFTGPUUM_ADAPTER;
typedef struct _SOFTGPUUM_DEVICE SOFTGPUUM_DEVICE;
typedef struct _SOFTGPUUM_RESOURCE SOFTGPUUM_RESOURCE;

typedef SOFTGPUUM_ADAPTER *PSOFTGPUUM_ADAPTER;
typedef SOFTGPUUM_DEVICE *PSOFTGPUUM_DEVICE;
typedef SOFTGPUUM_RESOURCE *PSOFTGPUUM_RESOURCE;

typedef struct _SOFTGPUUM_SUBRESOURCE
{
    D3DKMT_HANDLE hAllocation;
    UINT Width;
    UINT Height;
    UINT Pitch;
    SIZE_T Size;
    BOOL Locked;
    VOID *LockBase;
} SOFTGPUUM_SUBRESOURCE, *PSOFTGPUUM_SUBRESOURCE;

struct _SOFTGPUUM_ADAPTER
{
    ULONG Magic;
    HANDLE hRuntimeAdapter;
    UINT Interface;
    UINT Version;
    D3DDDI_ADAPTERCALLBACKS Callbacks;
    volatile LONG DeviceCount;
    PSOFTGPUUM_ADAPTER Next;
};

struct _SOFTGPUUM_DEVICE
{
    ULONG Magic;
    PSOFTGPUUM_ADAPTER Adapter;
    HANDLE hRuntimeDevice;
    D3DDDI_DEVICECALLBACKS Callbacks;

    HANDLE hContext;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    UINT AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT PatchLocationListSize;

    CRITICAL_SECTION Lock;
    PSOFTGPUUM_RESOURCE Resources;
    PSOFTGPUUM_DEVICE Next;
};

struct _SOFTGPUUM_RESOURCE
{
    ULONG Magic;
    PSOFTGPUUM_DEVICE Device;
    HANDLE hRuntimeResource;
    D3DKMT_HANDLE hKMResource;
    D3DDDIFORMAT Format;
    D3DDDI_RESOURCEFLAGS Flags;
    UINT SubresourceCount;
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
    HANDLE hSyncToken;
#endif
    PSOFTGPUUM_RESOURCE Next;
    SOFTGPUUM_SUBRESOURCE Subresources[1];
};

static CRITICAL_SECTION SoftGpuUmObjectLock;
static BOOL SoftGpuUmObjectLockReady;
static PSOFTGPUUM_ADAPTER SoftGpuUmAdapters;
static PSOFTGPUUM_DEVICE SoftGpuUmDevices;

static PSOFTGPUUM_ADAPTER
SoftGpuUmAdapter(
    HANDLE hAdapter)
{
    PSOFTGPUUM_ADAPTER Adapter;

    if (hAdapter == NULL || !SoftGpuUmObjectLockReady)
        return NULL;

    EnterCriticalSection(&SoftGpuUmObjectLock);
    for (Adapter = SoftGpuUmAdapters;
         Adapter != NULL;
         Adapter = Adapter->Next)
    {
        if ((HANDLE)Adapter == hAdapter)
            break;
    }
    LeaveCriticalSection(&SoftGpuUmObjectLock);

    return Adapter != NULL &&
           Adapter->Magic == SOFTGPUUM_ADAPTER_MAGIC
               ? Adapter
               : NULL;
}

static PSOFTGPUUM_DEVICE
SoftGpuUmDevice(
    HANDLE hDevice)
{
    PSOFTGPUUM_DEVICE Device;

    if (hDevice == NULL || !SoftGpuUmObjectLockReady)
        return NULL;

    EnterCriticalSection(&SoftGpuUmObjectLock);
    for (Device = SoftGpuUmDevices;
         Device != NULL;
         Device = Device->Next)
    {
        if ((HANDLE)Device == hDevice)
            break;
    }
    LeaveCriticalSection(&SoftGpuUmObjectLock);

    return Device != NULL &&
           Device->Magic == SOFTGPUUM_DEVICE_MAGIC
               ? Device
               : NULL;
}

/* Device->Lock must be held. Comparing list members before dereferencing the
 * candidate keeps an arbitrary runtime handle from becoming an arbitrary read. */
static PSOFTGPUUM_RESOURCE
SoftGpuUmResourceLocked(
    PSOFTGPUUM_DEVICE Device,
    HANDLE hResource)
{
    PSOFTGPUUM_RESOURCE Resource;

    if (Device == NULL || hResource == NULL)
        return NULL;

    for (Resource = Device->Resources;
         Resource != NULL;
         Resource = Resource->Next)
    {
        if ((HANDLE)Resource == hResource)
            break;
    }
    return Resource != NULL &&
           Resource->Magic == SOFTGPUUM_RESOURCE_MAGIC &&
           Resource->Device == Device
               ? Resource
               : NULL;
}

static BOOL
SoftGpuUmSurfaceGeometry(
    CONST D3DDDI_SURFACEINFO *Surface,
    UINT *Pitch,
    SIZE_T *Size)
{
    ULONGLONG SurfaceSize;

    if (Surface == NULL || Pitch == NULL || Size == NULL ||
        Surface->Width == 0 || Surface->Height == 0 ||
        Surface->Depth > 1 ||
        Surface->Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        Surface->Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Surface->Width >
            MAXUINT / SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Surface->pSysMem != NULL ||
        Surface->SysMemPitch != 0 ||
        Surface->SysMemSlicePitch != 0)
    {
        return FALSE;
    }

    *Pitch = Surface->Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    SurfaceSize = (ULONGLONG)*Pitch * Surface->Height;
    if (SurfaceSize == 0 ||
        SurfaceSize > (ULONGLONG)SOFTGPU_MAX_SURFACE_SIZE ||
        SurfaceSize > (ULONGLONG)MAXULONG_PTR)
    {
        return FALSE;
    }

    *Size = (SIZE_T)SurfaceSize;
    return TRUE;
}

static BOOL
SoftGpuUmRectValid(
    CONST RECT *Rect,
    CONST SOFTGPUUM_SUBRESOURCE *Surface)
{
    return Rect != NULL && Surface != NULL &&
           Rect->left >= 0 && Rect->top >= 0 &&
           Rect->left < Rect->right &&
           Rect->top < Rect->bottom &&
           Rect->right <= (LONG)Surface->Width &&
           Rect->bottom <= (LONG)Surface->Height;
}

/*
 * Submit one fixed-size software-engine command. Device->Lock serializes use
 * of the runtime-owned command/allocation/patch ring and resource destruction.
 */
static HRESULT
SoftGpuUmSubmitLocked(
    PSOFTGPUUM_DEVICE Device,
    CONST SOFTGPU_CMD *Command,
    CONST D3DDDI_ALLOCATIONLIST *Allocations,
    UINT AllocationCount,
    CONST D3DDDI_PATCHLOCATIONLIST *Patches,
    UINT PatchCount)
{
    D3DDDICB_RENDER Render;
    HRESULT Result;

    if (Device == NULL || Command == NULL ||
        Device->hContext == NULL ||
        Device->Callbacks.pfnRenderCb == NULL)
    {
        return E_NOTIMPL;
    }
    if (Device->pCommandBuffer == NULL ||
        Device->CommandBufferSize < sizeof(*Command) ||
        Device->pAllocationList == NULL ||
        Device->AllocationListSize < AllocationCount ||
        Device->pPatchLocationList == NULL ||
        Device->PatchLocationListSize < PatchCount ||
        AllocationCount == 0 || PatchCount == 0)
    {
        return E_FAIL;
    }

    ZeroMemory(Device->pCommandBuffer, sizeof(*Command));
    CopyMemory(Device->pCommandBuffer, Command, sizeof(*Command));
    ZeroMemory(Device->pAllocationList,
               AllocationCount * sizeof(*Device->pAllocationList));
    CopyMemory(Device->pAllocationList,
               Allocations,
               AllocationCount * sizeof(*Allocations));
    ZeroMemory(Device->pPatchLocationList,
               PatchCount * sizeof(*Device->pPatchLocationList));
    CopyMemory(Device->pPatchLocationList,
               Patches,
               PatchCount * sizeof(*Patches));

    ZeroMemory(&Render, sizeof(Render));
    Render.CommandLength = sizeof(*Command);
    Render.CommandOffset = 0;
    Render.NumAllocations = AllocationCount;
    Render.NumPatchLocations = PatchCount;
    Render.NewCommandBufferSize = Device->CommandBufferSize;
    Render.NewAllocationListSize = Device->AllocationListSize;
    Render.NewPatchLocationListSize = Device->PatchLocationListSize;
    Render.hContext = Device->hContext;

    Result = Device->Callbacks.pfnRenderCb(
                 Device->hRuntimeDevice,
                 &Render);
    if (FAILED(Result))
        return Result;

    if (Render.pNewCommandBuffer == NULL ||
        Render.NewCommandBufferSize < sizeof(*Command) ||
        Render.pNewAllocationList == NULL ||
        Render.NewAllocationListSize < AllocationCount ||
        Render.pNewPatchLocationList == NULL ||
        Render.NewPatchLocationListSize < PatchCount)
    {
        return E_FAIL;
    }

    Device->pCommandBuffer = Render.pNewCommandBuffer;
    Device->CommandBufferSize = Render.NewCommandBufferSize;
    Device->pAllocationList = Render.pNewAllocationList;
    Device->AllocationListSize = Render.NewAllocationListSize;
    Device->pPatchLocationList = Render.pNewPatchLocationList;
    Device->PatchLocationListSize =
        Render.NewPatchLocationListSize;
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * Device resource entries
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY
SoftGpuUmCreateResource(
    HANDLE hDevice,
    D3DDDIARG_CREATERESOURCE *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource = NULL;
    D3DDDI_ALLOCATIONINFO *AllocationInfo = NULL;
    SOFTGPU_ALLOCATION_PRIVATE_DATA *PrivateData = NULL;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    SIZE_T ResourceBytes;
    UINT Index;
    HRESULT Result = E_INVALIDARG;

    if (Device == NULL || pData == NULL ||
        pData->pSurfList == NULL ||
        pData->hResource == NULL ||
        pData->SurfCount == 0 ||
        pData->SurfCount > SOFTGPUUM_MAX_SUBRESOURCES)
    {
        return E_INVALIDARG;
    }
    if ((pData->Format != D3DDDIFMT_X8R8G8B8 &&
         pData->Format != D3DDDIFMT_A8R8G8B8) ||
        (pData->Pool != D3DDDIPOOL_VIDEOMEMORY &&
         pData->Pool != D3DDDIPOOL_LOCALVIDMEM) ||
        pData->MultisampleType != D3DDDIMULTISAMPLE_NONE ||
        pData->MultisampleQuality != 0 ||
        pData->MipLevels == 0 ||
        pData->MipLevels > pData->SurfCount ||
        pData->Fvf != 0 ||
        (pData->Rotation != 0 &&
         pData->Rotation != D3DDDI_ROTATION_IDENTITY) ||
        (pData->Flags.Value & ~SOFTGPUUM_RESOURCE_FLAGS_ALLOWED) != 0 ||
        (pData->Flags.Primary &&
         (pData->SurfCount != 1 || pData->VidPnSourceId != 0)))
    {
        return E_NOTIMPL;
    }

    ResourceBytes = FIELD_OFFSET(SOFTGPUUM_RESOURCE, Subresources);
    if (pData->SurfCount >
        (MAXULONG_PTR - ResourceBytes) / sizeof(Resource->Subresources[0]))
    {
        return E_OUTOFMEMORY;
    }
    ResourceBytes +=
        (SIZE_T)pData->SurfCount * sizeof(Resource->Subresources[0]);

    Resource = (PSOFTGPUUM_RESOURCE)HeapAlloc(
                   GetProcessHeap(),
                   HEAP_ZERO_MEMORY,
                   ResourceBytes);
    AllocationInfo = (D3DDDI_ALLOCATIONINFO *)HeapAlloc(
                         GetProcessHeap(),
                         HEAP_ZERO_MEMORY,
                         (SIZE_T)pData->SurfCount *
                             sizeof(*AllocationInfo));
    PrivateData = (SOFTGPU_ALLOCATION_PRIVATE_DATA *)HeapAlloc(
                      GetProcessHeap(),
                      HEAP_ZERO_MEMORY,
                      (SIZE_T)pData->SurfCount *
                          sizeof(*PrivateData));
    if (Resource == NULL || AllocationInfo == NULL ||
        PrivateData == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }

    Resource->Magic = SOFTGPUUM_RESOURCE_MAGIC;
    Resource->Device = Device;
    Resource->hRuntimeResource = pData->hResource;
    Resource->Format = pData->Format;
    Resource->Flags = pData->Flags;
    Resource->SubresourceCount = pData->SurfCount;

    for (Index = 0; Index < pData->SurfCount; ++Index)
    {
        UINT Pitch;
        SIZE_T Size;

        if (!SoftGpuUmSurfaceGeometry(
                 &pData->pSurfList[Index],
                 &Pitch,
                 &Size))
        {
            Result = E_NOTIMPL;
            goto Cleanup;
        }

        Resource->Subresources[Index].Width =
            pData->pSurfList[Index].Width;
        Resource->Subresources[Index].Height =
            pData->pSurfList[Index].Height;
        Resource->Subresources[Index].Pitch = Pitch;
        Resource->Subresources[Index].Size = Size;

        PrivateData[Index].Width =
            Resource->Subresources[Index].Width;
        PrivateData[Index].Height =
            Resource->Subresources[Index].Height;
        PrivateData[Index].BitsPerPixel =
            SOFTGPU_DISPLAY_BITS_PER_PIXEL;
        PrivateData[Index].Magic =
            SOFTGPU_ALLOCATION_PRIVATE_MAGIC;
        PrivateData[Index].Version =
            SOFTGPU_ALLOCATION_PRIVATE_VERSION;
        PrivateData[Index].Pitch = Pitch;
        PrivateData[Index].Format = pData->Format;

        AllocationInfo[Index].pPrivateDriverData =
            &PrivateData[Index];
        AllocationInfo[Index].PrivateDriverDataSize =
            sizeof(PrivateData[Index]);
        AllocationInfo[Index].VidPnSourceId =
            pData->VidPnSourceId;
        AllocationInfo[Index].Flags.Primary =
            pData->Flags.Primary ? 1 : 0;
    }

    if (Device->hContext == NULL ||
        Device->Callbacks.pfnAllocateCb == NULL ||
        Device->Callbacks.pfnDeallocateCb == NULL)
    {
        Result = E_NOTIMPL;
        goto Cleanup;
    }

    EnterCriticalSection(&Device->Lock);
    ZeroMemory(&Allocate, sizeof(Allocate));
    Allocate.hResource = pData->hResource;
    Allocate.NumAllocations = pData->SurfCount;
    Allocate.pAllocationInfo = AllocationInfo;
    Result = Device->Callbacks.pfnAllocateCb(
                 Device->hRuntimeDevice,
                 &Allocate);
    if (FAILED(Result))
    {
        LeaveCriticalSection(&Device->Lock);
        goto Cleanup;
    }
    Resource->hKMResource = Allocate.hKMResource;

    if (Resource->hKMResource == 0)
    {
        Result = E_FAIL;
        goto AllocationFailure;
    }
    for (Index = 0; Index < pData->SurfCount; ++Index)
    {
        if (AllocationInfo[Index].hAllocation == 0)
        {
            Result = E_FAIL;
            goto AllocationFailure;
        }
        Resource->Subresources[Index].hAllocation =
            AllocationInfo[Index].hAllocation;
    }

    Resource->Next = Device->Resources;
    Device->Resources = Resource;
    pData->hResource = (HANDLE)Resource;
    Resource = NULL;
    Result = S_OK;
    LeaveCriticalSection(&Device->Lock);
    goto Cleanup;

AllocationFailure:
    ZeroMemory(&Deallocate, sizeof(Deallocate));
    Deallocate.hResource = Resource->hRuntimeResource;
    (VOID)Device->Callbacks.pfnDeallocateCb(
              Device->hRuntimeDevice,
              &Deallocate);
    LeaveCriticalSection(&Device->Lock);

Cleanup:
    if (PrivateData != NULL)
        HeapFree(GetProcessHeap(), 0, PrivateData);
    if (AllocationInfo != NULL)
        HeapFree(GetProcessHeap(), 0, AllocationInfo);
    if (Resource != NULL)
    {
        Resource->Magic = 0;
        HeapFree(GetProcessHeap(), 0, Resource);
    }
    return Result;
}

static HRESULT APIENTRY
SoftGpuUmDestroyResource(
    HANDLE hDevice,
    HANDLE hResource)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource;
    PSOFTGPUUM_RESOURCE *Link;
    D3DDDICB_DEALLOCATE Deallocate;
    UINT Index;
    HRESULT Result;

    if (Device == NULL || hResource == NULL)
        return E_INVALIDARG;

    EnterCriticalSection(&Device->Lock);
    Resource = SoftGpuUmResourceLocked(Device, hResource);
    if (Resource == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    for (Index = 0; Index < Resource->SubresourceCount; ++Index)
    {
        if (Resource->Subresources[Index].Locked)
        {
            LeaveCriticalSection(&Device->Lock);
            return E_FAIL;
        }
    }
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
    if (Resource->hSyncToken != NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_FAIL;
    }
#endif
    if (Device->Callbacks.pfnDeallocateCb == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_NOTIMPL;
    }

    ZeroMemory(&Deallocate, sizeof(Deallocate));
    Deallocate.hResource = Resource->hRuntimeResource;
    Result = Device->Callbacks.pfnDeallocateCb(
                 Device->hRuntimeDevice,
                 &Deallocate);
    if (FAILED(Result))
    {
        LeaveCriticalSection(&Device->Lock);
        return Result;
    }

    for (Link = &Device->Resources;
         *Link != NULL;
         Link = &(*Link)->Next)
    {
        if (*Link == Resource)
        {
            *Link = Resource->Next;
            break;
        }
    }
    Resource->Magic = 0;
    Resource->Device = NULL;
    LeaveCriticalSection(&Device->Lock);
    HeapFree(GetProcessHeap(), 0, Resource);
    return S_OK;
}

static HRESULT APIENTRY
SoftGpuUmLock(
    HANDLE hDevice,
    D3DDDIARG_LOCK *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource;
    PSOFTGPUUM_SUBRESOURCE Surface;
    D3DDDICB_LOCK Lock;
    ULONGLONG Offset = 0;
    ULONGLONG End;
    UINT RegionFlags;
    HRESULT Result;

    if (Device == NULL || pData == NULL)
        return E_INVALIDARG;

    pData->pSurfData = NULL;
    pData->Pitch = 0;
    pData->SlicePitch = 0;

    RegionFlags = pData->Flags.Value & (0x10u | 0x20u | 0x40u);
    if (RegionFlags != 0 &&
        RegionFlags != 0x10u &&
        RegionFlags != 0x20u)
    {
        return E_NOTIMPL;
    }
    if ((pData->Flags.Value & ~0x00000337u) != 0 ||
        pData->Flags.NotifyOnly ||
        pData->Flags.Discard ||
        pData->Flags.BoxValid ||
        pData->Flags.MightDrawFromLocked ||
        (pData->Flags.ReadOnly && pData->Flags.WriteOnly))
    {
        return E_NOTIMPL;
    }

    EnterCriticalSection(&Device->Lock);
    Resource = SoftGpuUmResourceLocked(Device, pData->hResource);
    if (Resource == NULL ||
        pData->SubResourceIndex >= Resource->SubresourceCount)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    Surface = &Resource->Subresources[pData->SubResourceIndex];
    if (Resource->Flags.NotLockable || Surface->Locked)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_FAIL;
    }
    if (Resource->Flags.WriteOnly && pData->Flags.ReadOnly)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    if (pData->Flags.AreaValid)
    {
        if (!SoftGpuUmRectValid(&pData->Area, Surface))
        {
            LeaveCriticalSection(&Device->Lock);
            return E_INVALIDARG;
        }
        Offset = (ULONGLONG)(UINT)pData->Area.top * Surface->Pitch +
                 (ULONGLONG)(UINT)pData->Area.left *
                     SOFTGPU_DISPLAY_BYTES_PER_PIXEL;
    }
    else if (pData->Flags.RangeValid)
    {
        End = (ULONGLONG)pData->Range.Offset + pData->Range.Size;
        if (pData->Range.Size == 0 ||
            End > (ULONGLONG)Surface->Size)
        {
            LeaveCriticalSection(&Device->Lock);
            return E_INVALIDARG;
        }
        Offset = pData->Range.Offset;
    }

    if (Device->Callbacks.pfnLockCb == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_NOTIMPL;
    }

    ZeroMemory(&Lock, sizeof(Lock));
    Lock.hAllocation = Surface->hAllocation;
    Lock.Flags.ReadOnly = pData->Flags.ReadOnly;
    Lock.Flags.WriteOnly = pData->Flags.WriteOnly;
    Lock.Flags.DonotWait = pData->Flags.DoNotWait;
    Lock.Flags.LockEntire = 1;
    Result = Device->Callbacks.pfnLockCb(
                 Device->hRuntimeDevice,
                 &Lock);
    if (FAILED(Result))
    {
        LeaveCriticalSection(&Device->Lock);
        return Result;
    }
    if (Lock.pData == NULL ||
        Offset >= (ULONGLONG)Surface->Size)
    {
        D3DDDICB_UNLOCK Unlock;
        D3DKMT_HANDLE Allocation = Surface->hAllocation;

        ZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Allocation;
        if (Device->Callbacks.pfnUnlockCb != NULL)
        {
            (VOID)Device->Callbacks.pfnUnlockCb(
                      Device->hRuntimeDevice,
                      &Unlock);
        }
        LeaveCriticalSection(&Device->Lock);
        return E_FAIL;
    }

    Surface->Locked = TRUE;
    Surface->LockBase = Lock.pData;
    pData->pSurfData = (BYTE *)Lock.pData + (SIZE_T)Offset;
    pData->Pitch = Surface->Pitch;
    pData->SlicePitch = (UINT)Surface->Size;
    LeaveCriticalSection(&Device->Lock);
    return S_OK;
}

static HRESULT APIENTRY
SoftGpuUmUnlock(
    HANDLE hDevice,
    CONST D3DDDIARG_UNLOCK *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource;
    PSOFTGPUUM_SUBRESOURCE Surface;
    D3DDDICB_UNLOCK Unlock;
    D3DKMT_HANDLE Allocation;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        pData->Flags.Value != 0)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&Device->Lock);
    Resource = SoftGpuUmResourceLocked(Device, pData->hResource);
    if (Resource == NULL ||
        pData->SubResourceIndex >= Resource->SubresourceCount)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    Surface = &Resource->Subresources[pData->SubResourceIndex];
    if (!Surface->Locked)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    if (Device->Callbacks.pfnUnlockCb == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_NOTIMPL;
    }

    Allocation = Surface->hAllocation;
    ZeroMemory(&Unlock, sizeof(Unlock));
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &Allocation;
    Result = Device->Callbacks.pfnUnlockCb(
                 Device->hRuntimeDevice,
                 &Unlock);
    if (SUCCEEDED(Result))
    {
        Surface->Locked = FALSE;
        Surface->LockBase = NULL;
    }
    LeaveCriticalSection(&Device->Lock);
    return Result;
}

static HRESULT APIENTRY
SoftGpuUmBlt(
    HANDLE hDevice,
    CONST D3DDDIARG_BLT *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Source;
    PSOFTGPUUM_RESOURCE Destination;
    PSOFTGPUUM_SUBRESOURCE SourceSurface;
    PSOFTGPUUM_SUBRESOURCE DestinationSurface;
    SOFTGPU_CMD Command;
    D3DDDI_ALLOCATIONLIST Allocations[2];
    D3DDDI_PATCHLOCATIONLIST Patches[2];
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        (pData->Flags.Value & ~SOFTGPUUM_BLT_FLAGS_ALLOWED) != 0)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&Device->Lock);
    Source = SoftGpuUmResourceLocked(Device, pData->hSrcResource);
    Destination =
        SoftGpuUmResourceLocked(Device, pData->hDstResource);
    if (Source == NULL || Destination == NULL ||
        pData->SrcSubResourceIndex >= Source->SubresourceCount ||
        pData->DstSubResourceIndex >=
            Destination->SubresourceCount ||
        Source->Format != Destination->Format)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }

    SourceSurface =
        &Source->Subresources[pData->SrcSubResourceIndex];
    DestinationSurface =
        &Destination->Subresources[pData->DstSubResourceIndex];
    if (SourceSurface->Locked || DestinationSurface->Locked ||
        !SoftGpuUmRectValid(&pData->SrcRect, SourceSurface) ||
        !SoftGpuUmRectValid(&pData->DstRect, DestinationSurface) ||
        pData->SrcRect.right - pData->SrcRect.left !=
            pData->DstRect.right - pData->DstRect.left ||
        pData->SrcRect.bottom - pData->SrcRect.top !=
            pData->DstRect.bottom - pData->DstRect.top)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_NOTIMPL;
    }

    ZeroMemory(&Command, sizeof(Command));
    Command.Magic = SOFTGPU_CMD_MAGIC;
    Command.Op = SOFTGPU_CMD_OP_BLT;
    Command.Size = sizeof(Command);
    Command.SrcRect = pData->SrcRect;
    Command.DstRect = pData->DstRect;
    Command.SrcPitch = SourceSurface->Pitch;
    Command.DstPitch = DestinationSurface->Pitch;

    ZeroMemory(Allocations, sizeof(Allocations));
    Allocations[0].hAllocation = SourceSurface->hAllocation;
    Allocations[1].hAllocation =
        DestinationSurface->hAllocation;
    Allocations[1].WriteOperation = 1;

    ZeroMemory(Patches, sizeof(Patches));
    Patches[0].AllocationIndex = 0;
    Patches[0].PatchOffset =
        FIELD_OFFSET(SOFTGPU_CMD, SrcAddress);
    Patches[1].AllocationIndex = 1;
    Patches[1].PatchOffset =
        FIELD_OFFSET(SOFTGPU_CMD, DstAddress);

    Result = SoftGpuUmSubmitLocked(
                 Device,
                 &Command,
                 Allocations,
                 ARRAYSIZE(Allocations),
                 Patches,
                 ARRAYSIZE(Patches));
    LeaveCriticalSection(&Device->Lock);
    return Result;
}

static HRESULT APIENTRY
SoftGpuUmColorFill(
    HANDLE hDevice,
    CONST D3DDDIARG_COLORFILL *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource;
    PSOFTGPUUM_SUBRESOURCE Surface;
    SOFTGPU_CMD Command;
    D3DDDI_ALLOCATIONLIST Allocation;
    D3DDDI_PATCHLOCATIONLIST Patch;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        (pData->Flags.Value &
         ~SOFTGPUUM_COLORFILL_FLAGS_ALLOWED) != 0)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&Device->Lock);
    Resource = SoftGpuUmResourceLocked(Device, pData->hResource);
    if (Resource == NULL ||
        pData->SubResourceIndex >= Resource->SubresourceCount)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    Surface = &Resource->Subresources[pData->SubResourceIndex];
    if (Surface->Locked ||
        !SoftGpuUmRectValid(&pData->DstRect, Surface))
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }

    ZeroMemory(&Command, sizeof(Command));
    Command.Magic = SOFTGPU_CMD_MAGIC;
    Command.Op = SOFTGPU_CMD_OP_FILL;
    Command.Size = sizeof(Command);
    Command.Color = pData->Color;
    Command.DstRect = pData->DstRect;
    Command.DstPitch = Surface->Pitch;

    ZeroMemory(&Allocation, sizeof(Allocation));
    Allocation.hAllocation = Surface->hAllocation;
    Allocation.WriteOperation = 1;

    ZeroMemory(&Patch, sizeof(Patch));
    Patch.AllocationIndex = 0;
    Patch.PatchOffset =
        FIELD_OFFSET(SOFTGPU_CMD, DstAddress);

    Result = SoftGpuUmSubmitLocked(
                 Device,
                 &Command,
                 &Allocation,
                 1,
                 &Patch,
                 1);
    LeaveCriticalSection(&Device->Lock);
    return Result;
}

static HRESULT APIENTRY
SoftGpuUmSetDisplayMode(
    HANDLE hDevice,
    CONST D3DDDIARG_SETDISPLAYMODE *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource;
    D3DDDICB_SETDISPLAYMODE SetMode;
    HRESULT Result;

    if (Device == NULL || pData == NULL)
        return E_INVALIDARG;

    EnterCriticalSection(&Device->Lock);
    Resource = SoftGpuUmResourceLocked(Device, pData->hResource);
    if (Resource == NULL ||
        pData->SubResourceIndex >= Resource->SubresourceCount ||
        !Resource->Flags.Primary)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    if (Device->Callbacks.pfnSetDisplayModeCb == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_NOTIMPL;
    }

    ZeroMemory(&SetMode, sizeof(SetMode));
    SetMode.hPrimaryAllocation =
        Resource->Subresources[pData->SubResourceIndex].hAllocation;
    Result = Device->Callbacks.pfnSetDisplayModeCb(
                 Device->hRuntimeDevice,
                 &SetMode);
    LeaveCriticalSection(&Device->Lock);
    return Result;
}

static HRESULT APIENTRY
SoftGpuUmPresent(
    HANDLE hDevice,
    CONST D3DDDIARG_PRESENT *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Source;
    PSOFTGPUUM_RESOURCE Destination = NULL;
    PSOFTGPUUM_SUBRESOURCE SourceSurface;
    PSOFTGPUUM_SUBRESOURCE DestinationSurface = NULL;
    D3DDDICB_PRESENT Present;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        pData->hSrcResource == NULL ||
        pData->FlipInterval != D3DDDI_FLIPINTERVAL_IMMEDIATE ||
        (pData->Flags.Value & ~0x00000007u) != 0 ||
        pData->Flags.ColorFill)
    {
        return E_NOTIMPL;
    }
    if ((pData->Flags.Flip &&
         (pData->Flags.Blt || pData->hDstResource != NULL)) ||
        (!pData->Flags.Flip &&
         pData->hDstResource == NULL))
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&Device->Lock);
    Source = SoftGpuUmResourceLocked(Device, pData->hSrcResource);
    if (Source == NULL ||
        pData->SrcSubResourceIndex >= Source->SubresourceCount)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_INVALIDARG;
    }
    SourceSurface =
        &Source->Subresources[pData->SrcSubResourceIndex];
    if (SourceSurface->Locked)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_FAIL;
    }

    if (!pData->Flags.Flip)
    {
        Destination =
            SoftGpuUmResourceLocked(Device, pData->hDstResource);
        if (Destination == NULL ||
            pData->DstSubResourceIndex >=
                Destination->SubresourceCount)
        {
            LeaveCriticalSection(&Device->Lock);
            return E_INVALIDARG;
        }
        DestinationSurface =
            &Destination->Subresources[pData->DstSubResourceIndex];
        if (DestinationSurface->Locked ||
            !Destination->Flags.Primary ||
            Source->Format != Destination->Format ||
            SourceSurface->Width != DestinationSurface->Width ||
            SourceSurface->Height != DestinationSurface->Height)
        {
            LeaveCriticalSection(&Device->Lock);
            return E_NOTIMPL;
        }
    }
    else if (!Source->Flags.Primary)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_NOTIMPL;
    }

    if (Device->hContext == NULL ||
        Device->Callbacks.pfnPresentCb == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_NOTIMPL;
    }

    ZeroMemory(&Present, sizeof(Present));
    Present.hSrcAllocation = SourceSurface->hAllocation;
    if (DestinationSurface != NULL)
    {
        Present.hDstAllocation =
            DestinationSurface->hAllocation;
    }
    Present.hContext = Device->hContext;
    Result = Device->Callbacks.pfnPresentCb(
                 Device->hRuntimeDevice,
                 &Present);
    LeaveCriticalSection(&Device->Lock);
    return Result;
}

static HRESULT APIENTRY
SoftGpuUmFlush(
    HANDLE hDevice)
{
    /* Blt/fill/present submit synchronously through their runtime callback.
     * There is no driver-private batch left to flush. */
    return SoftGpuUmDevice(hDevice) != NULL ? S_OK : E_INVALIDARG;
}

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
static VOID APIENTRY
SoftGpuUmAcquireResource(
    HANDLE hDevice,
    CONST D3DDDIARG_SYNCTOKEN *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource;
    D3DDDICB_SYNCTOKEN Token;
    HANDLE Context;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        pData->hResource == NULL ||
        pData->hSyncToken == NULL)
    {
        return;
    }

    EnterCriticalSection(&Device->Lock);
    Resource = SoftGpuUmResourceLocked(
                   Device,
                   pData->hResource);
    if (Resource == NULL ||
        Resource->hSyncToken != NULL ||
        Device->hContext == NULL ||
        Device->Callbacks.pfnAcquireResourceCb == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return;
    }

    Context = Device->hContext;
    ZeroMemory(&Token, sizeof(Token));
    Token.hSyncToken = pData->hSyncToken;
    Token.BroadcastContextCount = 1;
    Token.BroadcastContextArray = &Context;
    Result = Device->Callbacks.pfnAcquireResourceCb(
                 Device->hRuntimeDevice,
                 &Token);
    if (SUCCEEDED(Result))
        Resource->hSyncToken = pData->hSyncToken;
    LeaveCriticalSection(&Device->Lock);
}

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_3)
static VOID APIENTRY
SoftGpuUmReleaseResource(
    HANDLE hDevice,
    CONST D3DDDIARG_SYNCTOKEN *pData)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_RESOURCE Resource;
    D3DDDICB_SYNCTOKEN Token;
    HANDLE Context;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        pData->hResource == NULL ||
        pData->hSyncToken == NULL)
    {
        return;
    }

    EnterCriticalSection(&Device->Lock);
    Resource = SoftGpuUmResourceLocked(
                   Device,
                   pData->hResource);
    if (Resource == NULL ||
        Resource->hSyncToken != pData->hSyncToken ||
        Device->hContext == NULL ||
        Device->Callbacks.pfnReleaseResourceCb == NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return;
    }

    Context = Device->hContext;
    ZeroMemory(&Token, sizeof(Token));
    Token.hSyncToken = pData->hSyncToken;
    Token.BroadcastContextCount = 1;
    Token.BroadcastContextArray = &Context;
    Result = Device->Callbacks.pfnReleaseResourceCb(
                 Device->hRuntimeDevice,
                 &Token);
    if (SUCCEEDED(Result))
        Resource->hSyncToken = NULL;
    LeaveCriticalSection(&Device->Lock);
}
#endif
#endif

static HRESULT APIENTRY
SoftGpuUmDestroyDevice(
    HANDLE hDevice)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);
    PSOFTGPUUM_DEVICE *Link;
    D3DDDICB_DESTROYCONTEXT DestroyContext;
    HRESULT Result;

    if (Device == NULL)
        return E_INVALIDARG;

    EnterCriticalSection(&Device->Lock);
    if (Device->Resources != NULL)
    {
        LeaveCriticalSection(&Device->Lock);
        return E_FAIL;
    }
    if (Device->hContext != NULL)
    {
        if (Device->Callbacks.pfnDestroyContextCb == NULL)
        {
            LeaveCriticalSection(&Device->Lock);
            return E_FAIL;
        }
        ZeroMemory(&DestroyContext, sizeof(DestroyContext));
        DestroyContext.hContext = Device->hContext;
        Result = Device->Callbacks.pfnDestroyContextCb(
                     Device->hRuntimeDevice,
                     &DestroyContext);
        if (FAILED(Result))
        {
            LeaveCriticalSection(&Device->Lock);
            return Result;
        }
        Device->hContext = NULL;
    }
    LeaveCriticalSection(&Device->Lock);

    EnterCriticalSection(&SoftGpuUmObjectLock);
    for (Link = &SoftGpuUmDevices;
         *Link != NULL;
         Link = &(*Link)->Next)
    {
        if (*Link == Device)
        {
            *Link = Device->Next;
            break;
        }
    }
    LeaveCriticalSection(&SoftGpuUmObjectLock);

    InterlockedDecrement(&Device->Adapter->DeviceCount);
    Device->Magic = 0;
    DeleteCriticalSection(&Device->Lock);
    HeapFree(GetProcessHeap(), 0, Device);
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * Adapter entries
 * ------------------------------------------------------------------------ */

static CONST FORMATOP SoftGpuUmFormats[] =
{
    {
        D3DDDIFMT_X8R8G8B8,
        FORMATOP_DISPLAYMODE |
            FORMATOP_PIXELSIZE |
            FORMATOP_OFFSCREENPLAIN,
        1,
        1,
        SOFTGPU_DISPLAY_BITS_PER_PIXEL
    },
    {
        D3DDDIFMT_A8R8G8B8,
        FORMATOP_DISPLAYMODE |
            FORMATOP_PIXELSIZE |
            FORMATOP_OFFSCREENPLAIN,
        1,
        1,
        SOFTGPU_DISPLAY_BITS_PER_PIXEL
    }
};

static HRESULT APIENTRY
SoftGpuUmGetCaps(
    HANDLE hAdapter,
    CONST D3DDDIARG_GETCAPS *pData)
{
    PSOFTGPUUM_ADAPTER Adapter = SoftGpuUmAdapter(hAdapter);

    if (Adapter == NULL || pData == NULL || pData->pData == NULL)
    {
        return E_INVALIDARG;
    }

    switch (pData->Type)
    {
        case D3DDDICAPS_DDRAW:
            if (pData->DataSize < sizeof(DDRAW_CAPS))
                return E_INVALIDARG;
            ZeroMemory(pData->pData, pData->DataSize);
            return S_OK;

        case D3DDDICAPS_DDRAW_MODE_SPECIFIC:
            if (pData->DataSize <
                sizeof(DDRAW_MODE_SPECIFIC_CAPS))
            {
                return E_INVALIDARG;
            }
            ZeroMemory(pData->pData, pData->DataSize);
            return S_OK;

        case D3DDDICAPS_GETFORMATCOUNT:
            if (pData->DataSize < sizeof(UINT))
                return E_INVALIDARG;
            *(UINT *)pData->pData = ARRAYSIZE(SoftGpuUmFormats);
            return S_OK;

        case D3DDDICAPS_GETFORMATDATA:
            if (pData->DataSize < sizeof(SoftGpuUmFormats))
                return E_INVALIDARG;
            ZeroMemory(pData->pData, pData->DataSize);
            CopyMemory(pData->pData,
                       SoftGpuUmFormats,
                       sizeof(SoftGpuUmFormats));
            return S_OK;

        case D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS:
        {
            DDIMULTISAMPLEQUALITYLEVELSDATA *Quality;

            if (pData->DataSize < sizeof(*Quality))
                return E_INVALIDARG;
            Quality =
                (DDIMULTISAMPLEQUALITYLEVELSDATA *)pData->pData;
            Quality->QualityLevels =
                Quality->MsType == D3DDDIMULTISAMPLE_NONE &&
                (Quality->Format == D3DDDIFMT_X8R8G8B8 ||
                 Quality->Format == D3DDDIFMT_A8R8G8B8)
                    ? 1
                    : 0;
            return S_OK;
        }

        case D3DDDICAPS_GETD3DQUERYCOUNT:
            if (pData->DataSize < sizeof(UINT))
                return E_INVALIDARG;
            *(UINT *)pData->pData = 0;
            return S_OK;

        case D3DDDICAPS_GETD3D3CAPS:
        case D3DDDICAPS_GETD3D5CAPS:
        case D3DDDICAPS_GETD3D6CAPS:
        case D3DDDICAPS_GETD3D7CAPS:
        case D3DDDICAPS_GETD3D8CAPS:
        case D3DDDICAPS_GETD3D9CAPS:
            if (pData->DataSize == 0)
                return E_INVALIDARG;
            ZeroMemory(pData->pData, pData->DataSize);
            return S_OK;

        default:
            return E_NOTIMPL;
    }
}

static HRESULT APIENTRY
SoftGpuUmCreateDevice(
    HANDLE hAdapter,
    D3DDDIARG_CREATEDEVICE *pData)
{
    PSOFTGPUUM_ADAPTER Adapter = SoftGpuUmAdapter(hAdapter);
    PSOFTGPUUM_DEVICE Device;
    D3DDDI_DEVICEFUNCS *Funcs;
    D3DDDICB_CREATECONTEXT CreateContext;
    D3DDDICB_DESTROYCONTEXT DestroyContext;
    HRESULT Result;

    if (Adapter == NULL || pData == NULL ||
        pData->pDeviceFuncs == NULL || pData->pCallbacks == NULL)
    {
        return E_INVALIDARG;
    }
    if ((pData->Interface != 0 &&
         pData->Interface != D3D_UMD_INTERFACE_VERSION) ||
        (pData->Version != 0 &&
         pData->Version != D3D_UMD_INTERFACE_VERSION))
    {
        return E_NOTIMPL;
    }

    Device = (PSOFTGPUUM_DEVICE)HeapAlloc(
                 GetProcessHeap(),
                 HEAP_ZERO_MEMORY,
                 sizeof(*Device));
    if (Device == NULL)
        return E_OUTOFMEMORY;

    Device->Magic = SOFTGPUUM_DEVICE_MAGIC;
    Device->Adapter = Adapter;
    Device->hRuntimeDevice = pData->hDevice;
    Device->Callbacks = *pData->pCallbacks;
    InitializeCriticalSection(&Device->Lock);

    /*
     * A zero callback table is accepted for lifecycle-only loader probes.
     * A real runtime supplies CreateContextCb; once present it is mandatory
     * that context creation return the complete render-ring tuple.
     */
    if (Device->Callbacks.pfnCreateContextCb != NULL)
    {
        if (Device->Callbacks.pfnDestroyContextCb == NULL)
        {
            DeleteCriticalSection(&Device->Lock);
            Device->Magic = 0;
            HeapFree(GetProcessHeap(), 0, Device);
            return E_INVALIDARG;
        }

        ZeroMemory(&CreateContext, sizeof(CreateContext));
        CreateContext.NodeOrdinal = 0;
        CreateContext.EngineAffinity = 0;
        Result = Device->Callbacks.pfnCreateContextCb(
                     Device->hRuntimeDevice,
                     &CreateContext);
        if (FAILED(Result))
        {
            DeleteCriticalSection(&Device->Lock);
            Device->Magic = 0;
            HeapFree(GetProcessHeap(), 0, Device);
            return Result;
        }
        if (CreateContext.hContext == NULL ||
            CreateContext.pCommandBuffer == NULL ||
            CreateContext.CommandBufferSize < sizeof(SOFTGPU_CMD) ||
            CreateContext.pAllocationList == NULL ||
            CreateContext.AllocationListSize < 2 ||
            CreateContext.pPatchLocationList == NULL ||
            CreateContext.PatchLocationListSize < 2)
        {
            if (CreateContext.hContext != NULL &&
                Device->Callbacks.pfnDestroyContextCb != NULL)
            {
                ZeroMemory(&DestroyContext, sizeof(DestroyContext));
                DestroyContext.hContext = CreateContext.hContext;
                (VOID)Device->Callbacks.pfnDestroyContextCb(
                          Device->hRuntimeDevice,
                          &DestroyContext);
            }
            DeleteCriticalSection(&Device->Lock);
            Device->Magic = 0;
            HeapFree(GetProcessHeap(), 0, Device);
            return E_FAIL;
        }

        Device->hContext = CreateContext.hContext;
        Device->pCommandBuffer = CreateContext.pCommandBuffer;
        Device->CommandBufferSize = CreateContext.CommandBufferSize;
        Device->pAllocationList = CreateContext.pAllocationList;
        Device->AllocationListSize =
            CreateContext.AllocationListSize;
        Device->pPatchLocationList =
            CreateContext.pPatchLocationList;
        Device->PatchLocationListSize =
            CreateContext.PatchLocationListSize;
    }

    Funcs = pData->pDeviceFuncs;
    ZeroMemory(Funcs, sizeof(*Funcs));
    Funcs->pfnLock = SoftGpuUmLock;
    Funcs->pfnUnlock = SoftGpuUmUnlock;
    Funcs->pfnCreateResource = SoftGpuUmCreateResource;
    Funcs->pfnDestroyResource = SoftGpuUmDestroyResource;
    Funcs->pfnSetDisplayMode = SoftGpuUmSetDisplayMode;
    Funcs->pfnPresent = SoftGpuUmPresent;
    Funcs->pfnFlush = SoftGpuUmFlush;
    Funcs->pfnBlt = SoftGpuUmBlt;
    Funcs->pfnColorFill = SoftGpuUmColorFill;
    Funcs->pfnDestroyDevice = SoftGpuUmDestroyDevice;
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
    Funcs->pfnAcquireResource = SoftGpuUmAcquireResource;
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_3)
    Funcs->pfnReleaseResource = SoftGpuUmReleaseResource;
#endif

    EnterCriticalSection(&SoftGpuUmObjectLock);
    Device->Next = SoftGpuUmDevices;
    SoftGpuUmDevices = Device;
    LeaveCriticalSection(&SoftGpuUmObjectLock);

    pData->hDevice = (HANDLE)Device;
    InterlockedIncrement(&Adapter->DeviceCount);
    return S_OK;
}

static HRESULT APIENTRY
SoftGpuUmCloseAdapter(
    HANDLE hAdapter)
{
    PSOFTGPUUM_ADAPTER Adapter = SoftGpuUmAdapter(hAdapter);
    PSOFTGPUUM_ADAPTER *Link;

    if (Adapter == NULL)
        return E_INVALIDARG;
    if (InterlockedCompareExchange(&Adapter->DeviceCount, 0, 0) != 0)
        return E_FAIL;

    EnterCriticalSection(&SoftGpuUmObjectLock);
    for (Link = &SoftGpuUmAdapters;
         *Link != NULL;
         Link = &(*Link)->Next)
    {
        if (*Link == Adapter)
        {
            *Link = Adapter->Next;
            break;
        }
    }
    LeaveCriticalSection(&SoftGpuUmObjectLock);

    Adapter->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Adapter);
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * The one export the runtime resolves by name
 * ------------------------------------------------------------------------ */

HRESULT APIENTRY
OpenAdapter10_2(
    D3DDDIARG_OPENADAPTER *pOpenData)
{
    PSOFTGPUUM_ADAPTER Adapter;

    if (pOpenData == NULL || pOpenData->pAdapterFuncs == NULL)
        return E_INVALIDARG;
    if ((pOpenData->Interface != 0 &&
         pOpenData->Interface != D3D_UMD_INTERFACE_VERSION) ||
        (pOpenData->Version != 0 &&
         pOpenData->Version != D3D_UMD_INTERFACE_VERSION))
    {
        return E_NOTIMPL;
    }

    Adapter = (PSOFTGPUUM_ADAPTER)HeapAlloc(
                  GetProcessHeap(),
                  HEAP_ZERO_MEMORY,
                  sizeof(*Adapter));
    if (Adapter == NULL)
        return E_OUTOFMEMORY;

    Adapter->Magic = SOFTGPUUM_ADAPTER_MAGIC;
    Adapter->hRuntimeAdapter = pOpenData->hAdapter;
    Adapter->Interface = pOpenData->Interface;
    Adapter->Version = pOpenData->Version;
    if (pOpenData->pAdapterCallbacks != NULL)
        Adapter->Callbacks = *pOpenData->pAdapterCallbacks;

    EnterCriticalSection(&SoftGpuUmObjectLock);
    Adapter->Next = SoftGpuUmAdapters;
    SoftGpuUmAdapters = Adapter;
    LeaveCriticalSection(&SoftGpuUmObjectLock);

    ZeroMemory(pOpenData->pAdapterFuncs,
               sizeof(*pOpenData->pAdapterFuncs));
    pOpenData->pAdapterFuncs->pfnGetCaps = SoftGpuUmGetCaps;
    pOpenData->pAdapterFuncs->pfnCreateDevice =
        SoftGpuUmCreateDevice;
    pOpenData->pAdapterFuncs->pfnCloseAdapter =
        SoftGpuUmCloseAdapter;

    pOpenData->hAdapter = (HANDLE)Adapter;
    pOpenData->DriverVersion = D3D_UMD_INTERFACE_VERSION;
    return S_OK;
}

BOOL WINAPI
DllMain(
    HINSTANCE hInstance,
    DWORD Reason,
    LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hInstance);
        InitializeCriticalSection(&SoftGpuUmObjectLock);
        SoftGpuUmObjectLockReady = TRUE;
    }
    return TRUE;
}

/* EOF */
