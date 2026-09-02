/*
 * PROJECT:     ReactOS DirectX video support
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Private D3D9 surface contract for asynchronous DXVA output
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <windef.h>
#include <unknwn.h>

#define REACTOS_DXVA_SURFACE_FENCE_GUID_INIT \
    {0x0f57f428, 0x6532, 0x4f35, {0xa6, 0x8d, 0x51, 0xd0, 0x97, 0xec, 0x43, 0x6f}}

#define REACTOS_DXVA_SURFACE_FENCE_DONOTWAIT 0x00000001u

#define REACTOS_DXVA_SURFACE_PRESENT_LIMITED_RGB 0x00000001u
#define REACTOS_DXVA_SURFACE_PRESENT_BT709       0x00000002u
#define REACTOS_DXVA_SURFACE_PRESENT_ALLOWED \
    (REACTOS_DXVA_SURFACE_PRESENT_LIMITED_RGB | \
     REACTOS_DXVA_SURFACE_PRESENT_BT709)

#define REACTOS_DXVA_SURFACE_MAX_PLANES 3u

typedef struct REACTOS_DXVA_SURFACE_MEMORY
{
    void *Data;
    SIZE_T Size;
    UINT Width;
    UINT Height;
    UINT Pitch;
    UINT StorageHeight;
    UINT PlaneCount;
    UINT PlaneOffsets[REACTOS_DXVA_SURFACE_MAX_PLANES];
    UINT PlanePitches[REACTOS_DXVA_SURFACE_MAX_PLANES];
    ULONG Generation;
} REACTOS_DXVA_SURFACE_MEMORY;

typedef struct IReactOSDxvaSurfaceFence IReactOSDxvaSurfaceFence;

typedef struct IReactOSDxvaSurfaceFenceVtbl
{
    HRESULT (WINAPI *QueryInterface)(IReactOSDxvaSurfaceFence *iface,
                                     REFIID iid,
                                     void **object);
    ULONG (WINAPI *AddRef)(IReactOSDxvaSurfaceFence *iface);
    ULONG (WINAPI *Release)(IReactOSDxvaSurfaceFence *iface);
    HRESULT (WINAPI *GetPresentationFlags)(IReactOSDxvaSurfaceFence *iface,
                                           DWORD *flags);
    HRESULT (WINAPI *Wait)(IReactOSDxvaSurfaceFence *iface, DWORD flags);
    HRESULT (WINAPI *GetSharedMemory)(IReactOSDxvaSurfaceFence *iface,
                                     DWORD flags,
                                     REACTOS_DXVA_SURFACE_MEMORY *memory);
    HRESULT (WINAPI *PrepareFallback)(IReactOSDxvaSurfaceFence *iface,
                                      DWORD flags);
    HRESULT (WINAPI *Present)(IReactOSDxvaSurfaceFence *iface,
                              const RECT *source,
                              const RECT *destination,
                              DWORD flags);
    HRESULT (WINAPI *Hide)(IReactOSDxvaSurfaceFence *iface);
} IReactOSDxvaSurfaceFenceVtbl;

struct IReactOSDxvaSurfaceFence
{
    const IReactOSDxvaSurfaceFenceVtbl *lpVtbl;
};

#define IReactOSDxvaSurfaceFence_AddRef(iface) \
    ((iface)->lpVtbl->AddRef((iface)))
#define IReactOSDxvaSurfaceFence_Release(iface) \
    ((iface)->lpVtbl->Release((iface)))
#define IReactOSDxvaSurfaceFence_GetPresentationFlags(iface, flags) \
    ((iface)->lpVtbl->GetPresentationFlags((iface), (flags)))
#define IReactOSDxvaSurfaceFence_Wait(iface, flags) \
    ((iface)->lpVtbl->Wait((iface), (flags)))
#define IReactOSDxvaSurfaceFence_GetSharedMemory(iface, flags, memory) \
    ((iface)->lpVtbl->GetSharedMemory((iface), (flags), (memory)))
#define IReactOSDxvaSurfaceFence_PrepareFallback(iface, flags) \
    ((iface)->lpVtbl->PrepareFallback((iface), (flags)))
#define IReactOSDxvaSurfaceFence_Present(iface, source, destination, flags) \
    ((iface)->lpVtbl->Present((iface), (source), (destination), (flags)))
#define IReactOSDxvaSurfaceFence_Hide(iface) \
    ((iface)->lpVtbl->Hide((iface)))
