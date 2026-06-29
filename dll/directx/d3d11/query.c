/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ID3D11Query / ID3D11Predicate implementation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

static HRESULT STDMETHODCALLTYPE query_QueryInterface(ID3D11Query *iface, REFIID riid, void **out)
{
    struct d3d11_query *q = impl_from_ID3D11Query(iface);

    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11Asynchronous) || IsEqualGUID(riid, &IID_ID3D11Query))
    {
        *out = iface;
        ID3D11Query_AddRef(iface);
        return S_OK;
    }

    if (q->is_predicate && IsEqualGUID(riid, &IID_ID3D11Predicate))
    {
        *out = iface;
        ID3D11Query_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE query_AddRef(ID3D11Query *iface)
{ return InterlockedIncrement(&impl_from_ID3D11Query(iface)->refcount); }

static ULONG STDMETHODCALLTYPE query_Release(ID3D11Query *iface)
{
    struct d3d11_query *q = impl_from_ID3D11Query(iface);
    ULONG r = InterlockedDecrement(&q->refcount);
    if (!r)
    {
        d3d11_device_child_cleanup(&q->child);
        HeapFree(GetProcessHeap(), 0, q);
    }
    return r;
}

static void STDMETHODCALLTYPE query_GetDevice(ID3D11Query *iface, ID3D11Device **d)
{
    d3d11_device_child_get_device(impl_from_ID3D11Query(iface)->child.device, d);
}

static HRESULT STDMETHODCALLTYPE query_GetPrivateData(ID3D11Query *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE query_SetPrivateData(ID3D11Query *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE query_SetPrivateDataInterface(ID3D11Query *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; }

static UINT STDMETHODCALLTYPE query_GetDataSize(ID3D11Query *iface)
{
    struct d3d11_query *q = impl_from_ID3D11Query(iface);
    switch (q->desc.Query)
    {
        case D3D11_QUERY_EVENT: return sizeof(BOOL);
        case D3D11_QUERY_OCCLUSION: return sizeof(UINT64);
        case D3D11_QUERY_TIMESTAMP: return sizeof(UINT64);
        case D3D11_QUERY_OCCLUSION_PREDICATE: return sizeof(BOOL);
        default: return 0;
    }
}

static void STDMETHODCALLTYPE query_GetDesc(ID3D11Query *iface, D3D11_QUERY_DESC *desc)
{
    if (desc)
        *desc = impl_from_ID3D11Query(iface)->desc;
}

static const struct ID3D11QueryVtbl query_vtbl = {
    query_QueryInterface, query_AddRef, query_Release,
    query_GetDevice, query_GetPrivateData, query_SetPrivateData, query_SetPrivateDataInterface,
    query_GetDataSize, query_GetDesc,
};

static BOOL d3d11_query_is_predicate_type(D3D11_QUERY query)
{
    return query == D3D11_QUERY_OCCLUSION_PREDICATE
            || query == D3D11_QUERY_SO_OVERFLOW_PREDICATE
            || query == D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0
            || query == D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1
            || query == D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2
            || query == D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3;
}

static BOOL d3d11_query_is_supported_type(D3D11_QUERY query)
{
    return query >= D3D11_QUERY_EVENT && query <= D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3;
}

HRESULT d3d11_query_create(struct d3d11_device *device,
        const D3D11_QUERY_DESC *desc, BOOL is_predicate, struct d3d11_query **out)
{
    struct d3d11_query *q;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc || !d3d11_query_is_supported_type(desc->Query))
        return E_INVALIDARG;
    if (desc->MiscFlags & ~D3D11_QUERY_MISC_PREDICATEHINT)
        return E_INVALIDARG;
    if (is_predicate != d3d11_query_is_predicate_type(desc->Query))
        return E_INVALIDARG;

    q = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*q));
    if (!q) return E_OUTOFMEMORY;

    hr = d3d11_device_child_init(&q->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, q);
        return hr;
    }

    q->ID3D11Query_iface.lpVtbl = &query_vtbl;
    q->refcount = 1;
    q->desc = *desc;
    q->is_predicate = is_predicate;

    *out = q;
    return S_OK;
}
