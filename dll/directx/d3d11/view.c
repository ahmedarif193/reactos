/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shader resource, render target, depth-stencil, and UAV views
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

/* ========================================================================= */
/* Common view helpers                                                       */
/* ========================================================================= */

static HRESULT view_get_resource_info(ID3D11Resource *resource, D3D11_RESOURCE_DIMENSION *dimension,
        DXGI_FORMAT *format, UINT *bind_flags, UINT *mip_levels, UINT *array_size)
{
    ID3D11Texture1D *texture1d;
    ID3D11Texture2D *texture2d;
    ID3D11Texture3D *texture3d;
    ID3D11Buffer *buffer;
    HRESULT hr;

    if (!resource || !dimension || !format || !bind_flags || !mip_levels || !array_size)
        return E_INVALIDARG;

    resource->lpVtbl->GetType(resource, dimension);
    *format = DXGI_FORMAT_UNKNOWN;
    *bind_flags = 0;
    *mip_levels = 1;
    *array_size = 1;

    switch (*dimension)
    {
        case D3D11_RESOURCE_DIMENSION_BUFFER:
            hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Buffer, (void **)&buffer);
            if (SUCCEEDED(hr))
            {
                D3D11_BUFFER_DESC desc;

                ID3D11Buffer_GetDesc(buffer, &desc);
                *bind_flags = desc.BindFlags;
                ID3D11Buffer_Release(buffer);
            }
            return hr;

        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture1D, (void **)&texture1d);
            if (SUCCEEDED(hr))
            {
                D3D11_TEXTURE1D_DESC desc;

                ID3D11Texture1D_GetDesc(texture1d, &desc);
                *format = desc.Format;
                *bind_flags = desc.BindFlags;
                *mip_levels = desc.MipLevels;
                *array_size = desc.ArraySize;
                ID3D11Texture1D_Release(texture1d);
            }
            return hr;

        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture2D, (void **)&texture2d);
            if (SUCCEEDED(hr))
            {
                D3D11_TEXTURE2D_DESC desc;

                ID3D11Texture2D_GetDesc(texture2d, &desc);
                *format = desc.Format;
                *bind_flags = desc.BindFlags;
                *mip_levels = desc.MipLevels;
                *array_size = desc.ArraySize;
                ID3D11Texture2D_Release(texture2d);
            }
            return hr;

        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture3D, (void **)&texture3d);
            if (SUCCEEDED(hr))
            {
                D3D11_TEXTURE3D_DESC desc;

                ID3D11Texture3D_GetDesc(texture3d, &desc);
                *format = desc.Format;
                *bind_flags = desc.BindFlags;
                *mip_levels = desc.MipLevels;
                *array_size = 1;
                ID3D11Texture3D_Release(texture3d);
            }
            return hr;

        default:
            return E_INVALIDARG;
    }
}

static HRESULT view_check_bind(ID3D11Resource *resource, UINT required_bind)
{
    D3D11_RESOURCE_DIMENSION dimension;
    DXGI_FORMAT format;
    UINT bind_flags, mip_levels, array_size;
    HRESULT hr;

    hr = view_get_resource_info(resource, &dimension, &format, &bind_flags, &mip_levels, &array_size);
    if (FAILED(hr))
        return hr;

    return (bind_flags & required_bind) ? S_OK : DXGI_ERROR_INVALID_CALL;
}

static HRESULT view_init_srv_desc(ID3D11Resource *resource,
        const D3D11_SHADER_RESOURCE_VIEW_DESC *desc, D3D11_SHADER_RESOURCE_VIEW_DESC *out)
{
    D3D11_RESOURCE_DIMENSION dimension;
    DXGI_FORMAT format;
    UINT bind_flags, mip_levels, array_size;
    HRESULT hr;

    hr = view_get_resource_info(resource, &dimension, &format, &bind_flags, &mip_levels, &array_size);
    if (FAILED(hr))
        return hr;
    if (!(bind_flags & D3D11_BIND_SHADER_RESOURCE))
        return DXGI_ERROR_INVALID_CALL;

    if (desc)
    {
        *out = *desc;
        return S_OK;
    }

    memset(out, 0, sizeof(*out));
    out->Format = format;
    switch (dimension)
    {
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            out->ViewDimension = array_size > 1 ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY
                    : D3D11_SRV_DIMENSION_TEXTURE1D;
            out->Texture1D.MipLevels = mip_levels;
            out->Texture1DArray.ArraySize = array_size;
            return S_OK;

        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            out->ViewDimension = array_size > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY
                    : D3D11_SRV_DIMENSION_TEXTURE2D;
            out->Texture2D.MipLevels = mip_levels;
            out->Texture2DArray.ArraySize = array_size;
            return S_OK;

        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            out->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
            out->Texture3D.MipLevels = mip_levels;
            return S_OK;

        default:
            return E_NOTIMPL;
    }
}

static HRESULT view_init_rtv_desc(ID3D11Resource *resource,
        const D3D11_RENDER_TARGET_VIEW_DESC *desc, D3D11_RENDER_TARGET_VIEW_DESC *out)
{
    D3D11_RESOURCE_DIMENSION dimension;
    DXGI_FORMAT format;
    UINT bind_flags, mip_levels, array_size;
    HRESULT hr;

    hr = view_get_resource_info(resource, &dimension, &format, &bind_flags, &mip_levels, &array_size);
    if (FAILED(hr))
        return hr;
    if (!(bind_flags & D3D11_BIND_RENDER_TARGET))
        return DXGI_ERROR_INVALID_CALL;

    if (desc)
    {
        *out = *desc;
        return S_OK;
    }

    memset(out, 0, sizeof(*out));
    out->Format = format;
    switch (dimension)
    {
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            out->ViewDimension = array_size > 1 ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY
                    : D3D11_RTV_DIMENSION_TEXTURE1D;
            out->Texture1DArray.ArraySize = array_size;
            return S_OK;

        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            out->ViewDimension = array_size > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY
                    : D3D11_RTV_DIMENSION_TEXTURE2D;
            out->Texture2DArray.ArraySize = array_size;
            return S_OK;

        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            out->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
            return S_OK;

        default:
            return E_NOTIMPL;
    }
}

static HRESULT view_init_dsv_desc(ID3D11Resource *resource,
        const D3D11_DEPTH_STENCIL_VIEW_DESC *desc, D3D11_DEPTH_STENCIL_VIEW_DESC *out)
{
    D3D11_RESOURCE_DIMENSION dimension;
    DXGI_FORMAT format;
    UINT bind_flags, mip_levels, array_size;
    HRESULT hr;

    hr = view_get_resource_info(resource, &dimension, &format, &bind_flags, &mip_levels, &array_size);
    if (FAILED(hr))
        return hr;
    if (!(bind_flags & D3D11_BIND_DEPTH_STENCIL))
        return DXGI_ERROR_INVALID_CALL;

    if (desc)
    {
        *out = *desc;
        return S_OK;
    }

    memset(out, 0, sizeof(*out));
    out->Format = format;
    switch (dimension)
    {
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            out->ViewDimension = array_size > 1 ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY
                    : D3D11_DSV_DIMENSION_TEXTURE1D;
            out->Texture1DArray.ArraySize = array_size;
            return S_OK;

        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            out->ViewDimension = array_size > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY
                    : D3D11_DSV_DIMENSION_TEXTURE2D;
            out->Texture2DArray.ArraySize = array_size;
            return S_OK;

        default:
            return E_NOTIMPL;
    }
}

static HRESULT view_init_uav_desc(ID3D11Resource *resource,
        const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc, D3D11_UNORDERED_ACCESS_VIEW_DESC *out)
{
    D3D11_RESOURCE_DIMENSION dimension;
    DXGI_FORMAT format;
    UINT bind_flags, mip_levels, array_size;
    HRESULT hr;

    hr = view_get_resource_info(resource, &dimension, &format, &bind_flags, &mip_levels, &array_size);
    if (FAILED(hr))
        return hr;
    if (!(bind_flags & D3D11_BIND_UNORDERED_ACCESS))
        return DXGI_ERROR_INVALID_CALL;

    if (desc)
    {
        *out = *desc;
        return S_OK;
    }

    memset(out, 0, sizeof(*out));
    out->Format = format;
    switch (dimension)
    {
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            out->ViewDimension = array_size > 1 ? D3D11_UAV_DIMENSION_TEXTURE1DARRAY
                    : D3D11_UAV_DIMENSION_TEXTURE1D;
            out->Texture1DArray.ArraySize = array_size;
            return S_OK;

        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
            out->ViewDimension = array_size > 1 ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY
                    : D3D11_UAV_DIMENSION_TEXTURE2D;
            out->Texture2DArray.ArraySize = array_size;
            return S_OK;

        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            out->ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
            return S_OK;

        default:
            return E_NOTIMPL;
    }
}

#define IMPL_VIEW(prefix, type, iid, desc_type, get_desc_name) \
static HRESULT STDMETHODCALLTYPE prefix##_QueryInterface(type *iface, REFIID riid, void **out) \
{ \
    if (!out) \
        return E_POINTER; \
    *out = NULL; \
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) \
            || IsEqualGUID(riid, &IID_ID3D11View) || IsEqualGUID(riid, &iid)) \
    { *out = iface; type##_AddRef(iface); return S_OK; } \
    return E_NOINTERFACE; \
} \
static ULONG STDMETHODCALLTYPE prefix##_AddRef(type *iface) \
{ return InterlockedIncrement(&impl_from_##type(iface)->refcount); } \
static ULONG STDMETHODCALLTYPE prefix##_Release(type *iface) \
{ \
    struct d3d11_##prefix *v = impl_from_##type(iface); \
    ULONG r = InterlockedDecrement(&v->refcount); \
    if (!r) { if (v->resource) ID3D11Resource_Release(v->resource); d3d11_device_child_cleanup(&v->child); HeapFree(GetProcessHeap(), 0, v); } \
    return r; \
} \
static void STDMETHODCALLTYPE prefix##_GetDevice(type *iface, ID3D11Device **d) \
{ d3d11_device_child_get_device(impl_from_##type(iface)->child.device, d); } \
static HRESULT STDMETHODCALLTYPE prefix##_GetPrivateData(type *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; } \
static HRESULT STDMETHODCALLTYPE prefix##_SetPrivateData(type *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; } \
static HRESULT STDMETHODCALLTYPE prefix##_SetPrivateDataInterface(type *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; } \
static void STDMETHODCALLTYPE prefix##_GetResource(type *iface, ID3D11Resource **r) \
{ struct d3d11_##prefix *v = impl_from_##type(iface); if (!r) return; *r = v->resource; if (*r) ID3D11Resource_AddRef(*r); } \
static void STDMETHODCALLTYPE prefix##_GetDesc(type *iface, desc_type *desc) \
{ if (desc) *desc = impl_from_##type(iface)->desc; }

/* ========================================================================= */
/* ID3D11ShaderResourceView                                                  */
/* ========================================================================= */

IMPL_VIEW(shader_resource_view, ID3D11ShaderResourceView, IID_ID3D11ShaderResourceView,
        D3D11_SHADER_RESOURCE_VIEW_DESC, GetDesc)

static const struct ID3D11ShaderResourceViewVtbl srv_vtbl = {
    shader_resource_view_QueryInterface, shader_resource_view_AddRef, shader_resource_view_Release,
    shader_resource_view_GetDevice, shader_resource_view_GetPrivateData,
    shader_resource_view_SetPrivateData, shader_resource_view_SetPrivateDataInterface,
    shader_resource_view_GetResource, shader_resource_view_GetDesc,
};

HRESULT d3d11_shader_resource_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *desc,
        struct d3d11_shader_resource_view **out)
{
    struct d3d11_shader_resource_view *v;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !resource)
        return E_INVALIDARG;

    hr = view_check_bind(resource, D3D11_BIND_SHADER_RESOURCE);
    if (FAILED(hr))
        return hr;

    v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&v->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    hr = view_init_srv_desc(resource, desc, &v->desc);
    if (FAILED(hr))
    {
        d3d11_device_child_cleanup(&v->child);
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    v->ID3D11ShaderResourceView_iface.lpVtbl = &srv_vtbl;
    v->refcount = 1;
    v->resource = resource; ID3D11Resource_AddRef(resource);
    *out = v;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11RenderTargetView                                                    */
/* ========================================================================= */

IMPL_VIEW(render_target_view, ID3D11RenderTargetView, IID_ID3D11RenderTargetView,
        D3D11_RENDER_TARGET_VIEW_DESC, GetDesc)

static const struct ID3D11RenderTargetViewVtbl rtv_vtbl = {
    render_target_view_QueryInterface, render_target_view_AddRef, render_target_view_Release,
    render_target_view_GetDevice, render_target_view_GetPrivateData,
    render_target_view_SetPrivateData, render_target_view_SetPrivateDataInterface,
    render_target_view_GetResource, render_target_view_GetDesc,
};

HRESULT d3d11_render_target_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_RENDER_TARGET_VIEW_DESC *desc,
        struct d3d11_render_target_view **out)
{
    struct d3d11_render_target_view *v;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !resource)
        return E_INVALIDARG;

    v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&v->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    hr = view_init_rtv_desc(resource, desc, &v->desc);
    if (FAILED(hr))
    {
        d3d11_device_child_cleanup(&v->child);
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    v->ID3D11RenderTargetView_iface.lpVtbl = &rtv_vtbl;
    v->refcount = 1;
    v->resource = resource; ID3D11Resource_AddRef(resource);
    *out = v;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11DepthStencilView                                                    */
/* ========================================================================= */

IMPL_VIEW(depth_stencil_view, ID3D11DepthStencilView, IID_ID3D11DepthStencilView,
        D3D11_DEPTH_STENCIL_VIEW_DESC, GetDesc)

static const struct ID3D11DepthStencilViewVtbl dsv_vtbl = {
    depth_stencil_view_QueryInterface, depth_stencil_view_AddRef, depth_stencil_view_Release,
    depth_stencil_view_GetDevice, depth_stencil_view_GetPrivateData,
    depth_stencil_view_SetPrivateData, depth_stencil_view_SetPrivateDataInterface,
    depth_stencil_view_GetResource, depth_stencil_view_GetDesc,
};

HRESULT d3d11_depth_stencil_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *desc,
        struct d3d11_depth_stencil_view **out)
{
    struct d3d11_depth_stencil_view *v;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !resource)
        return E_INVALIDARG;

    v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&v->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    hr = view_init_dsv_desc(resource, desc, &v->desc);
    if (FAILED(hr))
    {
        d3d11_device_child_cleanup(&v->child);
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    v->ID3D11DepthStencilView_iface.lpVtbl = &dsv_vtbl;
    v->refcount = 1;
    v->resource = resource; ID3D11Resource_AddRef(resource);
    *out = v;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11UnorderedAccessView                                                 */
/* ========================================================================= */

IMPL_VIEW(unordered_access_view, ID3D11UnorderedAccessView, IID_ID3D11UnorderedAccessView,
        D3D11_UNORDERED_ACCESS_VIEW_DESC, GetDesc)

static const struct ID3D11UnorderedAccessViewVtbl uav_vtbl = {
    unordered_access_view_QueryInterface, unordered_access_view_AddRef, unordered_access_view_Release,
    unordered_access_view_GetDevice, unordered_access_view_GetPrivateData,
    unordered_access_view_SetPrivateData, unordered_access_view_SetPrivateDataInterface,
    unordered_access_view_GetResource, unordered_access_view_GetDesc,
};

HRESULT d3d11_unordered_access_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc,
        struct d3d11_unordered_access_view **out)
{
    struct d3d11_unordered_access_view *v;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !resource)
        return E_INVALIDARG;

    v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v) return E_OUTOFMEMORY;
    hr = d3d11_device_child_init(&v->child, device);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    hr = view_init_uav_desc(resource, desc, &v->desc);
    if (FAILED(hr))
    {
        d3d11_device_child_cleanup(&v->child);
        HeapFree(GetProcessHeap(), 0, v);
        return hr;
    }
    v->ID3D11UnorderedAccessView_iface.lpVtbl = &uav_vtbl;
    v->refcount = 1;
    v->resource = resource; ID3D11Resource_AddRef(resource);
    *out = v;
    return S_OK;
}
