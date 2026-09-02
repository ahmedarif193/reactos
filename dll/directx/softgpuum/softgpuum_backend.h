/*
 * PROJECT:     ReactOS user-mode display drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Optional video backend contract for the linear WDDM UMD core
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <reactos/drivers/directx/softgpu_2d_shared.h>

typedef struct _SOFTGPUUM_SURFACE_LAYOUT
{
    UINT Pitch;
    UINT StorageHeight;
    SIZE_T Size;
    ULONG BitsPerPixel;
    UINT PlaneCount;
    UINT PlaneOffsets[SOFTGPU_ALLOCATION_MAX_PLANES];
    UINT PlanePitches[SOFTGPU_ALLOCATION_MAX_PLANES];
} SOFTGPUUM_SURFACE_LAYOUT, *PSOFTGPUUM_SURFACE_LAYOUT;

typedef struct _SOFTGPUUM_RESOURCE_MAPPING
{
    VOID *Data;
    SIZE_T Size;
    UINT Pitch;
    UINT Width;
    UINT Height;
    UINT StorageHeight;
    UINT PlaneCount;
    UINT PlaneOffsets[SOFTGPU_ALLOCATION_MAX_PLANES];
    UINT PlanePitches[SOFTGPU_ALLOCATION_MAX_PLANES];
    D3DDDIFORMAT Format;
    HANDLE Resource;
    UINT SubResourceIndex;
} SOFTGPUUM_RESOURCE_MAPPING, *PSOFTGPUUM_RESOURCE_MAPPING;

typedef struct _SOFTGPUUM_BACKEND
{
    HRESULT (APIENTRY *GetCaps)(CONST D3DDDIARG_GETCAPS *Data);
    HRESULT (APIENTRY *CreateDevice)(HANDLE Device,
                                     D3DDDI_DEVICEFUNCS *Functions,
                                     VOID **Context);
    HRESULT (APIENTRY *CanDestroyDevice)(HANDLE Device, VOID *Context);
    VOID (APIENTRY *DestroyDevice)(HANDLE Device, VOID *Context);
    HRESULT (APIENTRY *GetSurfaceLayout)(
        CONST D3DDDIARG_CREATERESOURCE *Resource,
        CONST D3DDDI_SURFACEINFO *Surface,
        SOFTGPUUM_SURFACE_LAYOUT *Layout);
    HRESULT (APIENTRY *WaitResource)(HANDLE Device,
                                     VOID *Context,
                                     HANDLE Resource,
                                     UINT SubResourceIndex,
                                     BOOL WriteAccess,
                                     BOOL DoNotWait);
    HRESULT (APIENTRY *ReleaseResource)(HANDLE Device,
                                        VOID *Context,
                                        HANDLE Resource);
} SOFTGPUUM_BACKEND, *PSOFTGPUUM_BACKEND;

CONST SOFTGPUUM_BACKEND *APIENTRY SoftGpuUmGetBackend(VOID);

VOID *APIENTRY SoftGpuUmGetBackendContext(HANDLE Device);

HRESULT APIENTRY
SoftGpuUmMapResource(HANDLE Device,
                     HANDLE Resource,
                     UINT SubResourceIndex,
                     BOOL WriteOnly,
                     SOFTGPUUM_RESOURCE_MAPPING *Mapping);

HRESULT APIENTRY
SoftGpuUmUnmapResource(HANDLE Device,
                       SOFTGPUUM_RESOURCE_MAPPING *Mapping);
