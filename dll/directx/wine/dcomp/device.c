/*
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Wine ships this as an E_NOTIMPL stub; ReactOS returns a real
 * IDCompositionDevice so the Win11 composited-present path (mesa/WSI and
 * DXGI composition swapchains) has a live device.  Commit/state calls
 * succeed; the visual-tree and surface factories return E_NOTIMPL until they
 * are materialised into the WDDM compositor (roadmap).
 */

#include "dcomp_native.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

static const GUID IID_IUnknown_local            = {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID IID_IDCompositionDevice_local = {0xc37ea93a,0xe7aa,0x450d,{0xb1,0x6f,0x97,0x46,0xcb,0x04,0x07,0xf3}};

typedef struct dcomp_device {
    IDCompositionDevice iface;
    LONG refcount;
} dcomp_device;

static inline dcomp_device *device_from(IDCompositionDevice *iface)
{
    return CONTAINING_RECORD(iface, dcomp_device, iface);
}

static HRESULT STDMETHODCALLTYPE device_QueryInterface(IDCompositionDevice *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown_local) || IsEqualGUID(riid, &IID_IDCompositionDevice_local))
    {
        iface->lpVtbl->AddRef(iface);
        *ppv = iface;
        return S_OK;
    }
    WARN("dcomp device: no interface %s\n", debugstr_guid(riid));
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE device_AddRef(IDCompositionDevice *iface)
{
    return InterlockedIncrement(&device_from(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE device_Release(IDCompositionDevice *iface)
{
    dcomp_device *d = device_from(iface);
    ULONG rc = InterlockedDecrement(&d->refcount);
    if (!rc) HeapFree(GetProcessHeap(), 0, d);
    return rc;
}

/* Frame/commit bookkeeping succeeds: callers gate on these HRESULTs. */
static HRESULT STDMETHODCALLTYPE device_Commit(IDCompositionDevice *iface) { return S_OK; }
static HRESULT STDMETHODCALLTYPE device_WaitForCommitCompletion(IDCompositionDevice *iface) { return S_OK; }
static HRESULT STDMETHODCALLTYPE device_GetFrameStatistics(IDCompositionDevice *iface, void *stats) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CheckDeviceState(IDCompositionDevice *iface, BOOL *valid)
{ if (valid) *valid = TRUE; return S_OK; }

/* Visual-tree / surface / transform factories — materialised with the WDDM
 * compositor present path (roadmap). */
static HRESULT STDMETHODCALLTYPE device_CreateTargetForHwnd(IDCompositionDevice *iface, HWND h, BOOL t, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateVisual(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateSurface(IDCompositionDevice *iface, UINT w, UINT h, UINT f, UINT a, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateVirtualSurface(IDCompositionDevice *iface, UINT w, UINT h, UINT f, UINT a, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateSurfaceFromHandle(IDCompositionDevice *iface, HANDLE h, IUnknown **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateSurfaceFromHwnd(IDCompositionDevice *iface, HWND h, IUnknown **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateTranslateTransform(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateScaleTransform(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateRotateTransform(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateSkewTransform(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateMatrixTransform(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateTransformGroup(IDCompositionDevice *iface, void **t, UINT c, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateTranslateTransform3D(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateScaleTransform3D(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateRotateTransform3D(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateMatrixTransform3D(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateTransform3DGroup(IDCompositionDevice *iface, void **t, UINT c, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateEffectGroup(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateRectangleClip(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE device_CreateAnimation(IDCompositionDevice *iface, void **o) { if (o) *o = NULL; return E_NOTIMPL; }

static const IDCompositionDeviceVtbl device_vtbl = {
    device_QueryInterface, device_AddRef, device_Release,
    device_Commit, device_WaitForCommitCompletion, device_GetFrameStatistics,
    device_CreateTargetForHwnd, device_CreateVisual, device_CreateSurface, device_CreateVirtualSurface,
    device_CreateSurfaceFromHandle, device_CreateSurfaceFromHwnd,
    device_CreateTranslateTransform, device_CreateScaleTransform, device_CreateRotateTransform,
    device_CreateSkewTransform, device_CreateMatrixTransform, device_CreateTransformGroup,
    device_CreateTranslateTransform3D, device_CreateScaleTransform3D, device_CreateRotateTransform3D,
    device_CreateMatrixTransform3D, device_CreateTransform3DGroup, device_CreateEffectGroup,
    device_CreateRectangleClip, device_CreateAnimation, device_CheckDeviceState,
};

static HRESULT dcomp_create_device(REFIID iid, void **out)
{
    dcomp_device *d;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;

    d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) return E_OUTOFMEMORY;
    d->iface.lpVtbl = &device_vtbl;
    d->refcount = 1;

    hr = device_QueryInterface(&d->iface, iid, out);
    device_Release(&d->iface);
    return hr;
}

HRESULT WINAPI DCompositionCreateDevice(void *dxgi_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", dxgi_device, debugstr_guid(iid), device);
    return dcomp_create_device(iid, device);
}

HRESULT WINAPI DCompositionCreateDevice2(void *rendering_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    return dcomp_create_device(iid, device);
}

HRESULT WINAPI DCompositionCreateDevice3(void *rendering_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    return dcomp_create_device(iid, device);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR exe[260];
        exe[0] = 0;
        GetModuleFileNameW(NULL, exe, 260);
        ERR("dcomp.dll loaded into %s\n", debugstr_w(exe));
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
