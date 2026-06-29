/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Pipeline state objects (blend, depth-stencil, rasterizer, sampler)
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

/* ========================================================================= */
/* ID3D11BlendState                                                          */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE blend_QueryInterface(ID3D11BlendState *iface, REFIID riid, void **out)
{
    TRACE("iface %p, riid %s.\n", iface, debugstr_guid(riid));
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11BlendState))
    {
        *out = iface;
        ID3D11BlendState_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE blend_AddRef(ID3D11BlendState *iface)
{
    struct d3d11_blend_state *state = impl_from_ID3D11BlendState(iface);
    return InterlockedIncrement(&state->refcount);
}

static ULONG STDMETHODCALLTYPE blend_Release(ID3D11BlendState *iface)
{
    struct d3d11_blend_state *state = impl_from_ID3D11BlendState(iface);
    ULONG refcount = InterlockedDecrement(&state->refcount);
    if (!refcount)
    {
        d3d11_device_child_cleanup(&state->child);
        HeapFree(GetProcessHeap(), 0, state);
    }
    return refcount;
}

static void STDMETHODCALLTYPE blend_GetDevice(ID3D11BlendState *iface, ID3D11Device **device)
{
    d3d11_device_child_get_device(impl_from_ID3D11BlendState(iface)->child.device, device);
}

static HRESULT STDMETHODCALLTYPE blend_GetPrivateData(ID3D11BlendState *iface, REFGUID guid, UINT *sz, void *data) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE blend_SetPrivateData(ID3D11BlendState *iface, REFGUID guid, UINT sz, const void *data) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE blend_SetPrivateDataInterface(ID3D11BlendState *iface, REFGUID guid, const IUnknown *data) { return E_NOTIMPL; }

static void STDMETHODCALLTYPE blend_GetDesc(ID3D11BlendState *iface, D3D11_BLEND_DESC *desc)
{
    if (desc)
        *desc = impl_from_ID3D11BlendState(iface)->desc;
}

static const struct ID3D11BlendStateVtbl blend_vtbl = {
    blend_QueryInterface, blend_AddRef, blend_Release,
    blend_GetDevice, blend_GetPrivateData, blend_SetPrivateData, blend_SetPrivateDataInterface,
    blend_GetDesc,
};

HRESULT d3d11_blend_state_create(struct d3d11_device *device,
        const D3D11_BLEND_DESC *desc, struct d3d11_blend_state **out)
{
    struct d3d11_blend_state *state;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc) return E_INVALIDARG;
    state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    if (!state) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&state->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, state);
        return hr;
    }
    state->ID3D11BlendState_iface.lpVtbl = &blend_vtbl;
    state->refcount = 1;
    state->desc = *desc;
    *out = state;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11DepthStencilState                                                   */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE dss_QueryInterface(ID3D11DepthStencilState *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11DepthStencilState))
    {
        *out = iface;
        ID3D11DepthStencilState_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dss_AddRef(ID3D11DepthStencilState *iface)
{ return InterlockedIncrement(&impl_from_ID3D11DepthStencilState(iface)->refcount); }

static ULONG STDMETHODCALLTYPE dss_Release(ID3D11DepthStencilState *iface)
{
    struct d3d11_depth_stencil_state *s = impl_from_ID3D11DepthStencilState(iface);
    ULONG r = InterlockedDecrement(&s->refcount);
    if (!r)
    {
        d3d11_device_child_cleanup(&s->child);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return r;
}

static void STDMETHODCALLTYPE dss_GetDevice(ID3D11DepthStencilState *iface, ID3D11Device **d)
{ d3d11_device_child_get_device(impl_from_ID3D11DepthStencilState(iface)->child.device, d); }
static HRESULT STDMETHODCALLTYPE dss_GetPrivateData(ID3D11DepthStencilState *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE dss_SetPrivateData(ID3D11DepthStencilState *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE dss_SetPrivateDataInterface(ID3D11DepthStencilState *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; }

static void STDMETHODCALLTYPE dss_GetDesc(ID3D11DepthStencilState *iface, D3D11_DEPTH_STENCIL_DESC *desc)
{ if (desc) *desc = impl_from_ID3D11DepthStencilState(iface)->desc; }

static const struct ID3D11DepthStencilStateVtbl dss_vtbl = {
    dss_QueryInterface, dss_AddRef, dss_Release,
    dss_GetDevice, dss_GetPrivateData, dss_SetPrivateData, dss_SetPrivateDataInterface,
    dss_GetDesc,
};

HRESULT d3d11_depth_stencil_state_create(struct d3d11_device *device,
        const D3D11_DEPTH_STENCIL_DESC *desc, struct d3d11_depth_stencil_state **out)
{
    struct d3d11_depth_stencil_state *s;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc) return E_INVALIDARG;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&s->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    s->ID3D11DepthStencilState_iface.lpVtbl = &dss_vtbl;
    s->refcount = 1; s->desc = *desc;
    *out = s;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11RasterizerState                                                     */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE rs_QueryInterface(ID3D11RasterizerState *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11RasterizerState))
    {
        *out = iface;
        ID3D11RasterizerState_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE rs_AddRef(ID3D11RasterizerState *iface)
{ return InterlockedIncrement(&impl_from_ID3D11RasterizerState(iface)->refcount); }

static ULONG STDMETHODCALLTYPE rs_Release(ID3D11RasterizerState *iface)
{
    struct d3d11_rasterizer_state *s = impl_from_ID3D11RasterizerState(iface);
    ULONG r = InterlockedDecrement(&s->refcount);
    if (!r)
    {
        d3d11_device_child_cleanup(&s->child);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return r;
}

static void STDMETHODCALLTYPE rs_GetDevice(ID3D11RasterizerState *iface, ID3D11Device **d)
{ d3d11_device_child_get_device(impl_from_ID3D11RasterizerState(iface)->child.device, d); }
static HRESULT STDMETHODCALLTYPE rs_GetPrivateData(ID3D11RasterizerState *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE rs_SetPrivateData(ID3D11RasterizerState *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE rs_SetPrivateDataInterface(ID3D11RasterizerState *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; }

static void STDMETHODCALLTYPE rs_GetDesc(ID3D11RasterizerState *iface, D3D11_RASTERIZER_DESC *desc)
{ if (desc) *desc = impl_from_ID3D11RasterizerState(iface)->desc; }

static const struct ID3D11RasterizerStateVtbl rs_vtbl = {
    rs_QueryInterface, rs_AddRef, rs_Release,
    rs_GetDevice, rs_GetPrivateData, rs_SetPrivateData, rs_SetPrivateDataInterface,
    rs_GetDesc,
};

HRESULT d3d11_rasterizer_state_create(struct d3d11_device *device,
        const D3D11_RASTERIZER_DESC *desc, struct d3d11_rasterizer_state **out)
{
    struct d3d11_rasterizer_state *s;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc) return E_INVALIDARG;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&s->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    s->ID3D11RasterizerState_iface.lpVtbl = &rs_vtbl;
    s->refcount = 1; s->desc = *desc;
    *out = s;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11SamplerState                                                        */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE ss_QueryInterface(ID3D11SamplerState *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11SamplerState))
    {
        *out = iface;
        ID3D11SamplerState_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ss_AddRef(ID3D11SamplerState *iface)
{ return InterlockedIncrement(&impl_from_ID3D11SamplerState(iface)->refcount); }

static ULONG STDMETHODCALLTYPE ss_Release(ID3D11SamplerState *iface)
{
    struct d3d11_sampler_state *s = impl_from_ID3D11SamplerState(iface);
    ULONG r = InterlockedDecrement(&s->refcount);
    if (!r)
    {
        d3d11_device_child_cleanup(&s->child);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return r;
}

static void STDMETHODCALLTYPE ss_GetDevice(ID3D11SamplerState *iface, ID3D11Device **d)
{ d3d11_device_child_get_device(impl_from_ID3D11SamplerState(iface)->child.device, d); }
static HRESULT STDMETHODCALLTYPE ss_GetPrivateData(ID3D11SamplerState *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE ss_SetPrivateData(ID3D11SamplerState *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE ss_SetPrivateDataInterface(ID3D11SamplerState *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; }

static void STDMETHODCALLTYPE ss_GetDesc(ID3D11SamplerState *iface, D3D11_SAMPLER_DESC *desc)
{ if (desc) *desc = impl_from_ID3D11SamplerState(iface)->desc; }

static const struct ID3D11SamplerStateVtbl ss_vtbl = {
    ss_QueryInterface, ss_AddRef, ss_Release,
    ss_GetDevice, ss_GetPrivateData, ss_SetPrivateData, ss_SetPrivateDataInterface,
    ss_GetDesc,
};

HRESULT d3d11_sampler_state_create(struct d3d11_device *device,
        const D3D11_SAMPLER_DESC *desc, struct d3d11_sampler_state **out)
{
    struct d3d11_sampler_state *s;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc) return E_INVALIDARG;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&s->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, s);
        return hr;
    }
    s->ID3D11SamplerState_iface.lpVtbl = &ss_vtbl;
    s->refcount = 1; s->desc = *desc;
    *out = s;
    return S_OK;
}
