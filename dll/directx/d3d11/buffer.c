/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ID3D11Buffer implementation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

static HRESULT STDMETHODCALLTYPE buf_QueryInterface(ID3D11Buffer *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11Resource) || IsEqualGUID(riid, &IID_ID3D11Buffer))
    {
        *out = iface;
        ID3D11Buffer_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE buf_AddRef(ID3D11Buffer *iface)
{ return InterlockedIncrement(&impl_from_ID3D11Buffer(iface)->refcount); }

static ULONG STDMETHODCALLTYPE buf_Release(ID3D11Buffer *iface)
{
    struct d3d11_buffer *b = impl_from_ID3D11Buffer(iface);
    ULONG r = InterlockedDecrement(&b->refcount);
    if (!r)
    {
        HeapFree(GetProcessHeap(), 0, b->sysmem);
        d3d11_device_child_cleanup(&b->child);
        HeapFree(GetProcessHeap(), 0, b);
    }
    return r;
}

static void STDMETHODCALLTYPE buf_GetDevice(ID3D11Buffer *iface, ID3D11Device **d)
{
    d3d11_device_child_get_device(impl_from_ID3D11Buffer(iface)->child.device, d);
}

static HRESULT STDMETHODCALLTYPE buf_GetPrivateData(ID3D11Buffer *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE buf_SetPrivateData(ID3D11Buffer *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE buf_SetPrivateDataInterface(ID3D11Buffer *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; }

static void STDMETHODCALLTYPE buf_GetType(ID3D11Buffer *iface, D3D11_RESOURCE_DIMENSION *dim)
{
    if (dim)
        *dim = D3D11_RESOURCE_DIMENSION_BUFFER;
}

static void STDMETHODCALLTYPE buf_SetEvictionPriority(ID3D11Buffer *iface, UINT prio)
{
    impl_from_ID3D11Buffer(iface)->eviction_priority = prio;
}

static UINT STDMETHODCALLTYPE buf_GetEvictionPriority(ID3D11Buffer *iface)
{
    return impl_from_ID3D11Buffer(iface)->eviction_priority;
}

static void STDMETHODCALLTYPE buf_GetDesc(ID3D11Buffer *iface, D3D11_BUFFER_DESC *desc)
{
    if (desc)
        *desc = impl_from_ID3D11Buffer(iface)->desc;
}

static const struct ID3D11BufferVtbl buf_vtbl = {
    buf_QueryInterface, buf_AddRef, buf_Release,
    buf_GetDevice, buf_GetPrivateData, buf_SetPrivateData, buf_SetPrivateDataInterface,
    buf_GetType, buf_SetEvictionPriority, buf_GetEvictionPriority, buf_GetDesc,
};

HRESULT d3d11_buffer_create(struct d3d11_device *device, const D3D11_BUFFER_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *data, struct d3d11_buffer **out)
{
    struct d3d11_buffer *b;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc || !desc->ByteWidth)
        return E_INVALIDARG;
    if (desc->Usage > D3D11_USAGE_STAGING)
        return E_INVALIDARG;
    if (desc->MiscFlags & ~(D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS
            | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS
            | D3D11_RESOURCE_MISC_BUFFER_STRUCTURED))
        return E_INVALIDARG;
    if ((desc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
            && (!desc->StructureByteStride || desc->ByteWidth % desc->StructureByteStride))
        return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_IMMUTABLE && (!data || !data->pSysMem))
        return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_STAGING && desc->BindFlags)
        return E_INVALIDARG;
    if (desc->Usage == D3D11_USAGE_DYNAMIC && !(desc->CPUAccessFlags & D3D11_CPU_ACCESS_WRITE))
        return E_INVALIDARG;

    b = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b) return E_OUTOFMEMORY;

    b->sysmem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, desc->ByteWidth);
    if (!b->sysmem) { HeapFree(GetProcessHeap(), 0, b); return E_OUTOFMEMORY; }

    hr = d3d11_device_child_init(&b->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, b->sysmem);
        HeapFree(GetProcessHeap(), 0, b);
        return hr;
    }

    if (data && data->pSysMem)
        memcpy(b->sysmem, data->pSysMem, desc->ByteWidth);

    b->ID3D11Buffer_iface.lpVtbl = &buf_vtbl;
    b->refcount = 1;
    b->desc = *desc;
    b->sysmem_size = desc->ByteWidth;
    *out = b;
    return S_OK;
}
