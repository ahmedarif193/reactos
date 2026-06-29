/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ID3D11Device and IDXGIDevice implementation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

/* ========================================================================= */
/* IDXGIDevice implementation (on d3d11_device)                              */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE dxgi_device_QueryInterface(IDXGIDevice *iface, REFIID riid, void **out)
{
    struct d3d11_device *device = impl_from_IDXGIDevice(iface);
    return ID3D11Device_QueryInterface(&device->ID3D11Device_iface, riid, out);
}

static ULONG STDMETHODCALLTYPE dxgi_device_AddRef(IDXGIDevice *iface)
{
    struct d3d11_device *device = impl_from_IDXGIDevice(iface);
    return ID3D11Device_AddRef(&device->ID3D11Device_iface);
}

static ULONG STDMETHODCALLTYPE dxgi_device_Release(IDXGIDevice *iface)
{
    struct d3d11_device *device = impl_from_IDXGIDevice(iface);
    return ID3D11Device_Release(&device->ID3D11Device_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_device_SetPrivateData(IDXGIDevice *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_SetPrivateDataInterface(IDXGIDevice *iface,
        REFGUID guid, const IUnknown *object)
{
    FIXME("iface %p, guid %s, object %p stub!\n", iface, debugstr_guid(guid), object);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_GetPrivateData(IDXGIDevice *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_GetParent(IDXGIDevice *iface,
        REFIID riid, void **parent)
{
    struct d3d11_device *device = impl_from_IDXGIDevice(iface);

    TRACE("iface %p, riid %s, parent %p.\n", iface, debugstr_guid(riid), parent);

    if (!parent)
        return E_POINTER;
    *parent = NULL;

    if (device->adapter)
        return IDXGIAdapter_QueryInterface(device->adapter, riid, parent);

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_GetAdapter(IDXGIDevice *iface,
        IDXGIAdapter **adapter)
{
    struct d3d11_device *device = impl_from_IDXGIDevice(iface);

    TRACE("iface %p, adapter %p.\n", iface, adapter);

    if (!adapter)
        return E_INVALIDARG;

    if (device->adapter)
    {
        *adapter = device->adapter;
        IDXGIAdapter_AddRef(*adapter);
        return S_OK;
    }

    *adapter = NULL;
    return DXGI_ERROR_DEVICE_REMOVED;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_CreateSurface(IDXGIDevice *iface,
        const DXGI_SURFACE_DESC *desc, UINT num_surfaces, DXGI_USAGE usage,
        const DXGI_SHARED_RESOURCE *shared_resource, IDXGISurface **surface)
{
    struct d3d11_device *device = impl_from_IDXGIDevice(iface);
    D3D11_TEXTURE2D_DESC texture_desc;
    struct d3d11_texture2d *texture;
    HRESULT hr;

    TRACE("iface %p, desc %p, num_surfaces %u, usage %#x, shared_resource %p, surface %p.\n",
            iface, desc, num_surfaces, usage, shared_resource, surface);

    if (!surface)
        return E_INVALIDARG;

    *surface = NULL;

    if (!desc || !desc->Width || !desc->Height || !desc->SampleDesc.Count
            || num_surfaces != 1 || shared_resource)
        return E_INVALIDARG;

    memset(&texture_desc, 0, sizeof(texture_desc));
    texture_desc.Width = desc->Width;
    texture_desc.Height = desc->Height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = desc->Format;
    texture_desc.SampleDesc = desc->SampleDesc;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = 0;
    if (usage & DXGI_USAGE_RENDER_TARGET_OUTPUT)
        texture_desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (usage & DXGI_USAGE_SHADER_INPUT)
        texture_desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    hr = d3d11_texture2d_create(device, &texture_desc, NULL, &texture);
    if (FAILED(hr))
        return hr;

    hr = ID3D11Texture2D_QueryInterface(&texture->ID3D11Texture2D_iface,
                                        &IID_IDXGISurface, (void **)surface);
    ID3D11Texture2D_Release(&texture->ID3D11Texture2D_iface);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_QueryResourceResidency(IDXGIDevice *iface,
        IUnknown *const *resources, DXGI_RESIDENCY *residency_status, UINT num_resources)
{
    UINT i;

    TRACE("iface %p, resources %p, residency_status %p, num_resources %u.\n",
            iface, resources, residency_status, num_resources);

    if (num_resources && (!resources || !residency_status))
        return E_INVALIDARG;

    for (i = 0; i < num_resources; ++i)
        residency_status[i] = DXGI_RESIDENCY_FULLY_RESIDENT;

    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_SetGPUThreadPriority(IDXGIDevice *iface, INT priority)
{
    TRACE("iface %p, priority %d.\n", iface, priority);

    if (priority < -7 || priority > 7)
        return E_INVALIDARG;

    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE dxgi_device_GetGPUThreadPriority(IDXGIDevice *iface, INT *priority)
{
    TRACE("iface %p, priority %p.\n", iface, priority);

    if (!priority)
        return E_INVALIDARG;

    *priority = 0;
    return DXGI_ERROR_UNSUPPORTED;
}

static const struct IDXGIDeviceVtbl dxgi_device_vtbl =
{
    dxgi_device_QueryInterface,
    dxgi_device_AddRef,
    dxgi_device_Release,
    dxgi_device_SetPrivateData,
    dxgi_device_SetPrivateDataInterface,
    dxgi_device_GetPrivateData,
    dxgi_device_GetParent,
    dxgi_device_GetAdapter,
    dxgi_device_CreateSurface,
    dxgi_device_QueryResourceResidency,
    dxgi_device_SetGPUThreadPriority,
    dxgi_device_GetGPUThreadPriority,
};

/* ========================================================================= */
/* ID3D11Device implementation                                               */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE d3d11_device_QueryInterface(ID3D11Device *iface, REFIID riid, void **out)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);

    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    if (!out)
        return E_POINTER;

    *out = NULL;

    if (IsEqualGUID(riid, &IID_IUnknown)
            || IsEqualGUID(riid, &IID_ID3D11Device))
    {
        *out = &device->ID3D11Device_iface;
        ID3D11Device_AddRef(iface);
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IDXGIObject) || IsEqualGUID(riid, &IID_IDXGIDevice))
    {
        *out = &device->IDXGIDevice_iface;
        ID3D11Device_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_device_AddRef(ID3D11Device *iface)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    ULONG refcount = InterlockedIncrement(&device->refcount);

    TRACE("%p increasing refcount to %lu.\n", device, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_device_Release(ID3D11Device *iface)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    ULONG refcount = InterlockedDecrement(&device->refcount);

    TRACE("%p decreasing refcount to %lu.\n", device, refcount);

    if (!refcount)
    {
        if (device->adapter)
            IDXGIAdapter_Release(device->adapter);
        if (device->factory)
            IDXGIFactory_Release(device->factory);
        HeapFree(GetProcessHeap(), 0, device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateBuffer(ID3D11Device *iface,
        const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *data, ID3D11Buffer **buffer)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_buffer *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, buffer %p.\n", iface, desc, data, buffer);

    if (!desc)
        return E_INVALIDARG;

    hr = d3d11_buffer_create(device, desc, data, &object);
    if (FAILED(hr))
        return hr;

    if (buffer)
    {
        *buffer = &object->ID3D11Buffer_iface;
    }
    else
    {
        ID3D11Buffer_Release(&object->ID3D11Buffer_iface);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateTexture1D(ID3D11Device *iface,
        const D3D11_TEXTURE1D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
        ID3D11Texture1D **texture)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_texture1d *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, texture %p.\n", iface, desc, data, texture);

    hr = d3d11_texture1d_create(device, desc, data, &object);
    if (FAILED(hr))
        return hr;

    if (texture)
        *texture = &object->ID3D11Texture1D_iface;
    else
        ID3D11Texture1D_Release(&object->ID3D11Texture1D_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateTexture2D(ID3D11Device *iface,
        const D3D11_TEXTURE2D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
        ID3D11Texture2D **texture)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_texture2d *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, texture %p.\n", iface, desc, data, texture);

    hr = d3d11_texture2d_create(device, desc, data, &object);
    if (FAILED(hr))
        return hr;

    if (texture)
        *texture = &object->ID3D11Texture2D_iface;
    else
        ID3D11Texture2D_Release(&object->ID3D11Texture2D_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateTexture3D(ID3D11Device *iface,
        const D3D11_TEXTURE3D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
        ID3D11Texture3D **texture)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_texture3d *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, texture %p.\n", iface, desc, data, texture);

    hr = d3d11_texture3d_create(device, desc, data, &object);
    if (FAILED(hr))
        return hr;

    if (texture)
        *texture = &object->ID3D11Texture3D_iface;
    else
        ID3D11Texture3D_Release(&object->ID3D11Texture3D_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateShaderResourceView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *desc,
        ID3D11ShaderResourceView **view)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_shader_resource_view *object;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    hr = d3d11_shader_resource_view_create(device, resource, desc, &object);
    if (FAILED(hr))
        return hr;

    if (view)
        *view = &object->ID3D11ShaderResourceView_iface;
    else
        ID3D11ShaderResourceView_Release(&object->ID3D11ShaderResourceView_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateUnorderedAccessView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc,
        ID3D11UnorderedAccessView **view)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_unordered_access_view *object;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    hr = d3d11_unordered_access_view_create(device, resource, desc, &object);
    if (FAILED(hr))
        return hr;

    if (view)
        *view = &object->ID3D11UnorderedAccessView_iface;
    else
        ID3D11UnorderedAccessView_Release(&object->ID3D11UnorderedAccessView_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateRenderTargetView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_RENDER_TARGET_VIEW_DESC *desc,
        ID3D11RenderTargetView **view)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_render_target_view *object;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    hr = d3d11_render_target_view_create(device, resource, desc, &object);
    if (FAILED(hr))
        return hr;

    if (view)
        *view = &object->ID3D11RenderTargetView_iface;
    else
        ID3D11RenderTargetView_Release(&object->ID3D11RenderTargetView_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDepthStencilView(ID3D11Device *iface,
        ID3D11Resource *resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *desc,
        ID3D11DepthStencilView **view)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_depth_stencil_view *object;
    HRESULT hr;

    TRACE("iface %p, resource %p, desc %p, view %p.\n", iface, resource, desc, view);

    hr = d3d11_depth_stencil_view_create(device, resource, desc, &object);
    if (FAILED(hr))
        return hr;

    if (view)
        *view = &object->ID3D11DepthStencilView_iface;
    else
        ID3D11DepthStencilView_Release(&object->ID3D11DepthStencilView_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateInputLayout(ID3D11Device *iface,
        const D3D11_INPUT_ELEMENT_DESC *descs, UINT count, const void *bytecode,
        SIZE_T bytecode_length, ID3D11InputLayout **layout)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_input_layout *object;
    HRESULT hr;

    TRACE("iface %p, descs %p, count %u, bytecode %p, length %Iu, layout %p.\n",
            iface, descs, count, bytecode, bytecode_length, layout);

    hr = d3d11_input_layout_create(device, descs, count, bytecode, bytecode_length, &object);
    if (FAILED(hr))
        return hr;

    if (layout)
        *layout = &object->ID3D11InputLayout_iface;
    else
        ID3D11InputLayout_Release(&object->ID3D11InputLayout_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateVertexShader(ID3D11Device *iface,
        const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11VertexShader **shader)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_vertex_shader *object;
    HRESULT hr;

    TRACE("iface %p, bytecode %p, length %Iu, linkage %p, shader %p.\n",
            iface, bytecode, length, linkage, shader);

    hr = d3d11_vertex_shader_create(device, bytecode, length, &object);
    if (FAILED(hr))
        return hr;

    if (shader)
        *shader = &object->ID3D11VertexShader_iface;
    else
        ID3D11VertexShader_Release(&object->ID3D11VertexShader_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateGeometryShader(ID3D11Device *iface,
        const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11GeometryShader **shader)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_geometry_shader *object;
    HRESULT hr;

    TRACE("iface %p, bytecode %p, length %Iu, linkage %p, shader %p.\n",
            iface, bytecode, length, linkage, shader);

    hr = d3d11_geometry_shader_create(device, bytecode, length, &object);
    if (FAILED(hr))
        return hr;

    if (shader)
        *shader = &object->ID3D11GeometryShader_iface;
    else
        ID3D11GeometryShader_Release(&object->ID3D11GeometryShader_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateGeometryShaderWithStreamOutput(
        ID3D11Device *iface, const void *bytecode, SIZE_T length,
        const D3D11_SO_DECLARATION_ENTRY *so_entries, UINT entry_count,
        const UINT *strides, UINT stride_count, UINT rasterized_stream,
        ID3D11ClassLinkage *linkage, ID3D11GeometryShader **shader)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreatePixelShader(ID3D11Device *iface,
        const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11PixelShader **shader)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_pixel_shader *object;
    HRESULT hr;

    TRACE("iface %p, bytecode %p, length %Iu, linkage %p, shader %p.\n",
            iface, bytecode, length, linkage, shader);

    hr = d3d11_pixel_shader_create(device, bytecode, length, &object);
    if (FAILED(hr))
        return hr;

    if (shader)
        *shader = &object->ID3D11PixelShader_iface;
    else
        ID3D11PixelShader_Release(&object->ID3D11PixelShader_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateHullShader(ID3D11Device *iface,
        const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11HullShader **shader)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_hull_shader *object;
    HRESULT hr;

    TRACE("iface %p, bytecode %p, length %Iu, shader %p.\n", iface, bytecode, length, shader);

    hr = d3d11_hull_shader_create(device, bytecode, length, &object);
    if (FAILED(hr))
        return hr;

    if (shader)
        *shader = &object->ID3D11HullShader_iface;
    else
        ID3D11HullShader_Release(&object->ID3D11HullShader_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDomainShader(ID3D11Device *iface,
        const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11DomainShader **shader)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_domain_shader *object;
    HRESULT hr;

    TRACE("iface %p, bytecode %p, length %Iu, shader %p.\n", iface, bytecode, length, shader);

    hr = d3d11_domain_shader_create(device, bytecode, length, &object);
    if (FAILED(hr))
        return hr;

    if (shader)
        *shader = &object->ID3D11DomainShader_iface;
    else
        ID3D11DomainShader_Release(&object->ID3D11DomainShader_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateComputeShader(ID3D11Device *iface,
        const void *bytecode, SIZE_T length, ID3D11ClassLinkage *linkage, ID3D11ComputeShader **shader)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_compute_shader *object;
    HRESULT hr;

    TRACE("iface %p, bytecode %p, length %Iu, shader %p.\n", iface, bytecode, length, shader);

    hr = d3d11_compute_shader_create(device, bytecode, length, &object);
    if (FAILED(hr))
        return hr;

    if (shader)
        *shader = &object->ID3D11ComputeShader_iface;
    else
        ID3D11ComputeShader_Release(&object->ID3D11ComputeShader_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateClassLinkage(ID3D11Device *iface,
        ID3D11ClassLinkage **linkage)
{
    FIXME("iface %p, linkage %p stub!\n", iface, linkage);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateBlendState(ID3D11Device *iface,
        const D3D11_BLEND_DESC *desc, ID3D11BlendState **state)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_blend_state *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, state %p.\n", iface, desc, state);

    hr = d3d11_blend_state_create(device, desc, &object);
    if (FAILED(hr))
        return hr;

    if (state)
        *state = &object->ID3D11BlendState_iface;
    else
        ID3D11BlendState_Release(&object->ID3D11BlendState_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDepthStencilState(ID3D11Device *iface,
        const D3D11_DEPTH_STENCIL_DESC *desc, ID3D11DepthStencilState **state)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_depth_stencil_state *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, state %p.\n", iface, desc, state);

    hr = d3d11_depth_stencil_state_create(device, desc, &object);
    if (FAILED(hr))
        return hr;

    if (state)
        *state = &object->ID3D11DepthStencilState_iface;
    else
        ID3D11DepthStencilState_Release(&object->ID3D11DepthStencilState_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateRasterizerState(ID3D11Device *iface,
        const D3D11_RASTERIZER_DESC *desc, ID3D11RasterizerState **state)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_rasterizer_state *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, state %p.\n", iface, desc, state);

    hr = d3d11_rasterizer_state_create(device, desc, &object);
    if (FAILED(hr))
        return hr;

    if (state)
        *state = &object->ID3D11RasterizerState_iface;
    else
        ID3D11RasterizerState_Release(&object->ID3D11RasterizerState_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateSamplerState(ID3D11Device *iface,
        const D3D11_SAMPLER_DESC *desc, ID3D11SamplerState **state)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_sampler_state *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, state %p.\n", iface, desc, state);

    hr = d3d11_sampler_state_create(device, desc, &object);
    if (FAILED(hr))
        return hr;

    if (state)
        *state = &object->ID3D11SamplerState_iface;
    else
        ID3D11SamplerState_Release(&object->ID3D11SamplerState_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateQuery(ID3D11Device *iface,
        const D3D11_QUERY_DESC *desc, ID3D11Query **query)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_query *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, query %p.\n", iface, desc, query);

    hr = d3d11_query_create(device, desc, FALSE, &object);
    if (FAILED(hr))
        return hr;

    if (query)
        *query = &object->ID3D11Query_iface;
    else
        ID3D11Query_Release(&object->ID3D11Query_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreatePredicate(ID3D11Device *iface,
        const D3D11_QUERY_DESC *desc, ID3D11Predicate **predicate)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);
    struct d3d11_query *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, predicate %p.\n", iface, desc, predicate);

    hr = d3d11_query_create(device, desc, TRUE, &object);
    if (FAILED(hr))
        return hr;

    if (predicate)
        *predicate = (ID3D11Predicate *)&object->ID3D11Query_iface;
    else
        ID3D11Query_Release(&object->ID3D11Query_iface);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateCounter(ID3D11Device *iface,
        const D3D11_COUNTER_DESC *desc, ID3D11Counter **counter)
{
    FIXME("iface %p, desc %p, counter %p stub!\n", iface, desc, counter);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CreateDeferredContext(ID3D11Device *iface,
        UINT flags, ID3D11DeviceContext **context)
{
    FIXME("iface %p, flags %#x, context %p stub!\n", iface, flags, context);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_OpenSharedResource(ID3D11Device *iface,
        HANDLE resource, REFIID riid, void **out)
{
    FIXME("iface %p, resource %p, riid %s, out %p stub!\n", iface, resource, debugstr_guid(riid), out);
    if (out)
        *out = NULL;
    return E_NOTIMPL;
}

static UINT d3d11_device_format_support(DXGI_FORMAT format)
{
    UINT support = D3D11_FORMAT_SUPPORT_TEXTURE1D | D3D11_FORMAT_SUPPORT_TEXTURE2D
            | D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_SHADER_LOAD
            | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE;

    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_B5G6R5_UNORM:
        case DXGI_FORMAT_B5G5R5A1_UNORM:
            return support | D3D11_FORMAT_SUPPORT_RENDER_TARGET
                    | D3D11_FORMAT_SUPPORT_BLENDABLE;

        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
            return D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DEPTH_STENCIL
                    | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE;

        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            return support;

        default:
            return 0;
    }
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckFormatSupport(ID3D11Device *iface,
        DXGI_FORMAT format, UINT *support)
{
    TRACE("iface %p, format %u, support %p.\n", iface, format, support);

    if (!support)
        return E_INVALIDARG;

    *support = d3d11_device_format_support(format);
    return *support ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckMultisampleQualityLevels(ID3D11Device *iface,
        DXGI_FORMAT format, UINT sample_count, UINT *quality_levels)
{
    TRACE("iface %p, format %u, sample_count %u, quality_levels %p.\n",
            iface, format, sample_count, quality_levels);

    if (!quality_levels)
        return E_INVALIDARG;

    *quality_levels = 0;
    if (!sample_count)
        return E_INVALIDARG;
    if (!d3d11_device_format_support(format))
        return E_FAIL;

    *quality_levels = (sample_count == 1) ? 1 : 0;
    return S_OK;
}

static void STDMETHODCALLTYPE d3d11_device_CheckCounterInfo(ID3D11Device *iface,
        D3D11_COUNTER_INFO *info)
{
    TRACE("iface %p, info %p.\n", iface, info);

    if (info)
    {
        info->LastDeviceDependentCounter = D3D11_COUNTER_DEVICE_DEPENDENT_0;
        info->NumSimultaneousCounters = 0;
        info->NumDetectableParallelUnits = 0;
    }
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckCounter(ID3D11Device *iface,
        const D3D11_COUNTER_DESC *desc, D3D11_COUNTER_TYPE *type, UINT *active_counters,
        LPSTR name, UINT *name_length, LPSTR units, UINT *units_length,
        LPSTR description, UINT *description_length)
{
    FIXME("iface %p stub!\n", iface);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_CheckFeatureSupport(ID3D11Device *iface,
        D3D11_FEATURE feature, void *data, UINT data_size)
{
    TRACE("iface %p, feature %u, data %p, data_size %u.\n", iface, feature, data, data_size);

    if (!data)
        return E_INVALIDARG;

    switch (feature)
    {
        case D3D11_FEATURE_THREADING:
        {
            D3D11_FEATURE_DATA_THREADING *threading = data;
            if (data_size != sizeof(*threading))
                return E_INVALIDARG;
            threading->DriverConcurrentCreates = FALSE;
            threading->DriverCommandLists = FALSE;
            return S_OK;
        }
        case D3D11_FEATURE_DOUBLES:
        {
            D3D11_FEATURE_DATA_DOUBLES *doubles = data;
            if (data_size != sizeof(*doubles))
                return E_INVALIDARG;
            doubles->DoublePrecisionFloatShaderOps = FALSE;
            return S_OK;
        }
        case D3D11_FEATURE_FORMAT_SUPPORT:
        {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT *format_support = data;
            if (data_size != sizeof(*format_support))
                return E_INVALIDARG;
            format_support->OutFormatSupport = d3d11_device_format_support(format_support->InFormat);
            return format_support->OutFormatSupport ? S_OK : E_FAIL;
        }
        default:
            FIXME("Unhandled feature %u.\n", feature);
            return E_NOTIMPL;
    }
}

static HRESULT STDMETHODCALLTYPE d3d11_device_GetPrivateData(ID3D11Device *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_SetPrivateData(ID3D11Device *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n", iface, debugstr_guid(guid), data_size, data);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_SetPrivateDataInterface(ID3D11Device *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);
    return E_NOTIMPL;
}

static D3D_FEATURE_LEVEL STDMETHODCALLTYPE d3d11_device_GetFeatureLevel(ID3D11Device *iface)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);

    TRACE("iface %p.\n", iface);

    return device->feature_level;
}

static UINT STDMETHODCALLTYPE d3d11_device_GetCreationFlags(ID3D11Device *iface)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);

    TRACE("iface %p.\n", iface);

    return device->creation_flags;
}

static HRESULT STDMETHODCALLTYPE d3d11_device_GetDeviceRemovedReason(ID3D11Device *iface)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);

    TRACE("iface %p.\n", iface);

    return device->device_removed_reason;
}

static void STDMETHODCALLTYPE d3d11_device_GetImmediateContext(ID3D11Device *iface,
        ID3D11DeviceContext **context)
{
    struct d3d11_device *device = impl_from_ID3D11Device(iface);

    TRACE("iface %p, context %p.\n", iface, context);

    if (!context)
        return;

    *context = &device->immediate_context.ID3D11DeviceContext_iface;
    ID3D11DeviceContext_AddRef(*context);
}

static HRESULT STDMETHODCALLTYPE d3d11_device_SetExceptionMode(ID3D11Device *iface, UINT flags)
{
    TRACE("iface %p, flags %#x.\n", iface, flags);
    if (flags)
        return DXGI_ERROR_UNSUPPORTED;
    return S_OK;
}

static UINT STDMETHODCALLTYPE d3d11_device_GetExceptionMode(ID3D11Device *iface)
{
    TRACE("iface %p.\n", iface);
    return 0;
}

static const struct ID3D11DeviceVtbl d3d11_device_vtbl =
{
    d3d11_device_QueryInterface,
    d3d11_device_AddRef,
    d3d11_device_Release,
    d3d11_device_CreateBuffer,
    d3d11_device_CreateTexture1D,
    d3d11_device_CreateTexture2D,
    d3d11_device_CreateTexture3D,
    d3d11_device_CreateShaderResourceView,
    d3d11_device_CreateUnorderedAccessView,
    d3d11_device_CreateRenderTargetView,
    d3d11_device_CreateDepthStencilView,
    d3d11_device_CreateInputLayout,
    d3d11_device_CreateVertexShader,
    d3d11_device_CreateGeometryShader,
    d3d11_device_CreateGeometryShaderWithStreamOutput,
    d3d11_device_CreatePixelShader,
    d3d11_device_CreateHullShader,
    d3d11_device_CreateDomainShader,
    d3d11_device_CreateComputeShader,
    d3d11_device_CreateClassLinkage,
    d3d11_device_CreateBlendState,
    d3d11_device_CreateDepthStencilState,
    d3d11_device_CreateRasterizerState,
    d3d11_device_CreateSamplerState,
    d3d11_device_CreateQuery,
    d3d11_device_CreatePredicate,
    d3d11_device_CreateCounter,
    d3d11_device_CreateDeferredContext,
    d3d11_device_OpenSharedResource,
    d3d11_device_CheckFormatSupport,
    d3d11_device_CheckMultisampleQualityLevels,
    d3d11_device_CheckCounterInfo,
    d3d11_device_CheckCounter,
    d3d11_device_CheckFeatureSupport,
    d3d11_device_GetPrivateData,
    d3d11_device_SetPrivateData,
    d3d11_device_SetPrivateDataInterface,
    d3d11_device_GetFeatureLevel,
    d3d11_device_GetCreationFlags,
    d3d11_device_GetDeviceRemovedReason,
    d3d11_device_GetImmediateContext,
    d3d11_device_SetExceptionMode,
    d3d11_device_GetExceptionMode,
};

HRESULT d3d11_device_create(IDXGIAdapter *adapter, D3D_DRIVER_TYPE driver_type,
        UINT flags, const D3D_FEATURE_LEVEL *feature_levels, UINT levels,
        struct d3d11_device **out)
{
    UINT i;

    TRACE("adapter %p, driver_type %d, flags %#x, feature_levels %p, levels %u.\n",
            adapter, driver_type, flags, feature_levels, levels);

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!feature_levels || !levels)
        return E_INVALIDARG;

    if (adapter && driver_type != D3D_DRIVER_TYPE_UNKNOWN)
        return E_INVALIDARG;
    if (!adapter && driver_type == D3D_DRIVER_TYPE_UNKNOWN)
        return E_INVALIDARG;

    for (i = 0; i < levels; ++i)
    {
        switch (feature_levels[i])
        {
            case D3D_FEATURE_LEVEL_11_0:
            case D3D_FEATURE_LEVEL_10_1:
            case D3D_FEATURE_LEVEL_10_0:
            case D3D_FEATURE_LEVEL_9_3:
            case D3D_FEATURE_LEVEL_9_2:
            case D3D_FEATURE_LEVEL_9_1:
                break;
            default:
                WARN("Unsupported D3D11 feature level %#x.\n", feature_levels[i]);
                return E_INVALIDARG;
        }
    }

    switch (driver_type)
    {
        case D3D_DRIVER_TYPE_NULL:
        case D3D_DRIVER_TYPE_UNKNOWN:
        case D3D_DRIVER_TYPE_HARDWARE:
        case D3D_DRIVER_TYPE_REFERENCE:
        case D3D_DRIVER_TYPE_SOFTWARE:
        case D3D_DRIVER_TYPE_WARP:
            WARN("D3D11 driver type %u is not backed by a real runtime yet.\n",
                 driver_type);
            return DXGI_ERROR_UNSUPPORTED;
        default:
            return E_INVALIDARG;
    }
}
