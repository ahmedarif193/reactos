/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ID3D11DeviceContext (immediate context) implementation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

#define SET_STATE(slot, object) d3d11_set_unknown((IUnknown **)(slot), (IUnknown *)(object))
#define GET_STATE(object, out) d3d11_get_unknown((IUnknown *)(object), (IUnknown **)(out))

static UINT min_uint(UINT a, UINT b)
{
    return a < b ? a : b;
}

static BYTE float_to_unorm8(FLOAT value)
{
    if (value <= 0.0f)
        return 0;
    if (value >= 1.0f)
        return 255;
    return (BYTE)(value * 255.0f + 0.5f);
}

static void get_state_array(IUnknown **objects, UINT limit, UINT start, UINT count,
        IUnknown **out)
{
    UINT i;

    if (!out)
        return;

    for (i = 0; i < count; ++i)
    {
        out[i] = NULL;
        if (start + i < limit)
            d3d11_get_unknown(objects[start + i], &out[i]);
    }
}

static void clear_state_array(IUnknown **out, UINT count)
{
    UINT i;

    if (!out)
        return;

    for (i = 0; i < count; ++i)
        out[i] = NULL;
}

static void clear_shader_state(IUnknown **shader, IUnknown **instances, UINT *count)
{
    UINT i, instance_count = count ? *count : 0;

    if (shader)
        *shader = NULL;
    if (instances)
    {
        for (i = 0; i < instance_count; ++i)
            instances[i] = NULL;
    }
    if (count)
        *count = 0;
}

static void d3d11_context_release_state(struct d3d11_immediate_context *context)
{
    UINT i;

    SET_STATE(&context->input_layout, NULL);
    for (i = 0; i < ARRAY_SIZE(context->vertex_buffers); ++i)
        SET_STATE(&context->vertex_buffers[i], NULL);
    SET_STATE(&context->index_buffer, NULL);

    SET_STATE(&context->vs, NULL);
    for (i = 0; i < ARRAY_SIZE(context->vs_cbs); ++i)
        SET_STATE(&context->vs_cbs[i], NULL);
    for (i = 0; i < ARRAY_SIZE(context->vs_srvs); ++i)
        SET_STATE(&context->vs_srvs[i], NULL);

    SET_STATE(&context->ps, NULL);
    for (i = 0; i < ARRAY_SIZE(context->ps_cbs); ++i)
        SET_STATE(&context->ps_cbs[i], NULL);
    for (i = 0; i < ARRAY_SIZE(context->ps_srvs); ++i)
        SET_STATE(&context->ps_srvs[i], NULL);
    for (i = 0; i < ARRAY_SIZE(context->ps_samplers); ++i)
        SET_STATE(&context->ps_samplers[i], NULL);

    SET_STATE(&context->gs, NULL);
    SET_STATE(&context->hs, NULL);
    SET_STATE(&context->ds, NULL);
    SET_STATE(&context->cs, NULL);
    SET_STATE(&context->rasterizer_state, NULL);

    for (i = 0; i < ARRAY_SIZE(context->render_targets); ++i)
        SET_STATE(&context->render_targets[i], NULL);
    SET_STATE(&context->depth_stencil, NULL);
    SET_STATE(&context->blend_state, NULL);
    SET_STATE(&context->depth_stencil_state, NULL);
}

static void d3d11_context_reset_state(struct d3d11_immediate_context *context)
{
    d3d11_context_release_state(context);

    context->topology = 0;
    memset(context->vertex_strides, 0, sizeof(context->vertex_strides));
    memset(context->vertex_offsets, 0, sizeof(context->vertex_offsets));
    context->index_format = DXGI_FORMAT_UNKNOWN;
    context->index_offset = 0;
    context->viewport_count = 0;
    memset(context->viewports, 0, sizeof(context->viewports));
    context->scissor_count = 0;
    memset(context->scissor_rects, 0, sizeof(context->scissor_rects));
    memset(context->blend_factor, 0, sizeof(context->blend_factor));
    context->sample_mask = D3D11_DEFAULT_SAMPLE_MASK;
    context->stencil_ref = 0;
}

static HRESULT d3d11_context_clear_rtv(struct d3d11_immediate_context *context,
        ID3D11RenderTargetView *view, const FLOAT color[4])
{
    ID3D11Resource *resource = NULL;
    ID3D11Texture2D *texture = NULL;
    D3D11_TEXTURE2D_DESC desc;
    UINT pixel, *pixels;
    SIZE_T pixel_count;
    HRESULT hr;

    (void)context;

    if (!view || !color)
        return E_INVALIDARG;

    view->lpVtbl->GetResource(view, &resource);
    if (!resource)
        return E_INVALIDARG;

    hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture2D, (void **)&texture);
    if (FAILED(hr))
    {
        ID3D11Resource_Release(resource);
        return hr;
    }

    ID3D11Texture2D_GetDesc(texture, &desc);
    ID3D11Texture2D_Release(texture);

    if (!(desc.BindFlags & D3D11_BIND_RENDER_TARGET) || desc.SampleDesc.Count != 1)
    {
        ID3D11Resource_Release(resource);
        return DXGI_ERROR_INVALID_CALL;
    }

    if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
    {
        pixel = float_to_unorm8(color[0])
                | (float_to_unorm8(color[1]) << 8)
                | (float_to_unorm8(color[2]) << 16)
                | (float_to_unorm8(color[3]) << 24);
    }
    else if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
    {
        pixel = float_to_unorm8(color[2])
                | (float_to_unorm8(color[1]) << 8)
                | (float_to_unorm8(color[0]) << 16)
                | (float_to_unorm8(color[3]) << 24);
    }
    else
    {
        ID3D11Resource_Release(resource);
        return E_NOTIMPL;
    }

    if (!desc.Width || !desc.Height || desc.Height > ~(SIZE_T)0 / desc.Width)
    {
        ID3D11Resource_Release(resource);
        return E_OUTOFMEMORY;
    }
    pixel_count = (SIZE_T)desc.Width * desc.Height;
    if (pixel_count > ~(SIZE_T)0 / sizeof(*pixels))
    {
        ID3D11Resource_Release(resource);
        return E_OUTOFMEMORY;
    }

    pixels = HeapAlloc(GetProcessHeap(), 0, pixel_count * sizeof(*pixels));
    if (!pixels)
    {
        ID3D11Resource_Release(resource);
        return E_OUTOFMEMORY;
    }

    for (pixel_count = (SIZE_T)desc.Width * desc.Height; pixel_count; --pixel_count)
        pixels[pixel_count - 1] = pixel;

    hr = d3d11_texture_resource_update(resource, 0, NULL, pixels, desc.Width * sizeof(*pixels), 0);
    HeapFree(GetProcessHeap(), 0, pixels);
    ID3D11Resource_Release(resource);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_context_QueryInterface(ID3D11DeviceContext *iface, REFIID riid, void **out)
{
    TRACE("iface %p, riid %s, out %p.\n", iface, debugstr_guid(riid), out);

    if (!out)
        return E_POINTER;

    *out = NULL;

    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11DeviceContext))
    {
        *out = iface;
        ID3D11DeviceContext_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_context_AddRef(ID3D11DeviceContext *iface)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    ULONG refcount = InterlockedIncrement(&context->refcount);
    ID3D11Device_AddRef(&context->device->ID3D11Device_iface);
    TRACE("%p increasing refcount to %lu.\n", context, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_context_Release(ID3D11DeviceContext *iface)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    struct d3d11_device *device = context->device;
    ULONG refcount = InterlockedDecrement(&context->refcount);
    TRACE("%p decreasing refcount to %lu.\n", context, refcount);
    if (!refcount)
        d3d11_context_release_state(context);
    ID3D11Device_Release(&device->ID3D11Device_iface);
    return refcount;
}

static void STDMETHODCALLTYPE d3d11_context_GetDevice(ID3D11DeviceContext *iface, ID3D11Device **device)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    d3d11_device_child_get_device(context->device, device);
}

static HRESULT STDMETHODCALLTYPE d3d11_context_GetPrivateData(ID3D11DeviceContext *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("stub!\n");
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_context_SetPrivateData(ID3D11DeviceContext *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("stub!\n");
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d11_context_SetPrivateDataInterface(ID3D11DeviceContext *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("stub!\n");
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d11_context_VSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer *const *buffers)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, start %u, count %u, buffers %p.\n", iface, start, count, buffers);
    if (start >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT_)
        return;
    count = min_uint(count, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT_ - start);
    for (i = 0; i < count; i++)
        SET_STATE(&context->vs_cbs[start + i], buffers ? buffers[i] : NULL);
}

static void STDMETHODCALLTYPE d3d11_context_PSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView *const *views)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, start %u, count %u, views %p.\n", iface, start, count, views);
    if (start >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT_)
        return;
    count = min_uint(count, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT_ - start);
    for (i = 0; i < count; i++)
        SET_STATE(&context->ps_srvs[start + i], views ? views[i] : NULL);
}

static void STDMETHODCALLTYPE d3d11_context_PSSetShader(ID3D11DeviceContext *iface,
        ID3D11PixelShader *shader, ID3D11ClassInstance *const *instances, UINT count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, shader %p.\n", iface, shader);
    SET_STATE(&context->ps, shader);
}

static void STDMETHODCALLTYPE d3d11_context_PSSetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState *const *samplers)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, start %u, count %u, samplers %p.\n", iface, start, count, samplers);
    if (start >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT_)
        return;
    count = min_uint(count, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT_ - start);
    for (i = 0; i < count; i++)
        SET_STATE(&context->ps_samplers[start + i], samplers ? samplers[i] : NULL);
}

static void STDMETHODCALLTYPE d3d11_context_VSSetShader(ID3D11DeviceContext *iface,
        ID3D11VertexShader *shader, ID3D11ClassInstance *const *instances, UINT count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, shader %p.\n", iface, shader);
    SET_STATE(&context->vs, shader);
}

static void STDMETHODCALLTYPE d3d11_context_DrawIndexed(ID3D11DeviceContext *iface,
        UINT index_count, UINT start_index, INT base_vertex)
{
    WARN("iface %p, index_count %u, start_index %u, base_vertex %d ignored; no D3D11 rasterizer backend.\n",
            iface, index_count, start_index, base_vertex);
}

static void STDMETHODCALLTYPE d3d11_context_Draw(ID3D11DeviceContext *iface,
        UINT vertex_count, UINT start_vertex)
{
    WARN("iface %p, vertex_count %u, start_vertex %u ignored; no D3D11 rasterizer backend.\n",
            iface, vertex_count, start_vertex);
}

static HRESULT STDMETHODCALLTYPE d3d11_context_Map(ID3D11DeviceContext *iface,
        ID3D11Resource *resource, UINT subresource, D3D11_MAP type, UINT flags,
        D3D11_MAPPED_SUBRESOURCE *mapped)
{
    TRACE("iface %p, resource %p, subresource %u, type %u, flags %#x, mapped %p.\n",
            iface, resource, subresource, type, flags, mapped);
    return d3d11_texture_resource_map(resource, subresource, type, flags, mapped);
}

static void STDMETHODCALLTYPE d3d11_context_Unmap(ID3D11DeviceContext *iface,
        ID3D11Resource *resource, UINT subresource)
{
    TRACE("iface %p, resource %p, subresource %u.\n", iface, resource, subresource);
    d3d11_texture_resource_unmap(resource, subresource);
}

static void STDMETHODCALLTYPE d3d11_context_PSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer *const *buffers)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, start %u, count %u, buffers %p.\n", iface, start, count, buffers);
    if (start >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT_)
        return;
    count = min_uint(count, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT_ - start);
    for (i = 0; i < count; i++)
        SET_STATE(&context->ps_cbs[start + i], buffers ? buffers[i] : NULL);
}

static void STDMETHODCALLTYPE d3d11_context_IASetInputLayout(ID3D11DeviceContext *iface,
        ID3D11InputLayout *layout)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, layout %p.\n", iface, layout);
    SET_STATE(&context->input_layout, layout);
}

static void STDMETHODCALLTYPE d3d11_context_IASetVertexBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer *const *buffers, const UINT *strides, const UINT *offsets)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, start %u, count %u.\n", iface, start, count);
    if (start >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT_)
        return;
    count = min_uint(count, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT_ - start);
    for (i = 0; i < count; i++)
    {
        SET_STATE(&context->vertex_buffers[start + i], buffers ? buffers[i] : NULL);
        context->vertex_strides[start + i] = strides ? strides[i] : 0;
        context->vertex_offsets[start + i] = offsets ? offsets[i] : 0;
    }
}

static void STDMETHODCALLTYPE d3d11_context_IASetIndexBuffer(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, DXGI_FORMAT format, UINT offset)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, buffer %p, format %u, offset %u.\n", iface, buffer, format, offset);
    SET_STATE(&context->index_buffer, buffer);
    context->index_format = format;
    context->index_offset = offset;
}

static void STDMETHODCALLTYPE d3d11_context_DrawIndexedInstanced(ID3D11DeviceContext *iface,
        UINT index_count_per_instance, UINT instance_count, UINT start_index,
        INT base_vertex, UINT start_instance)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_DrawInstanced(ID3D11DeviceContext *iface,
        UINT vertex_count_per_instance, UINT instance_count, UINT start_vertex, UINT start_instance)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_GSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer *const *buffers)
{
    TRACE("iface %p, start %u, count %u, buffers %p.\n", iface, start, count, buffers);
}

static void STDMETHODCALLTYPE d3d11_context_GSSetShader(ID3D11DeviceContext *iface,
        ID3D11GeometryShader *shader, ID3D11ClassInstance *const *instances, UINT count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, shader %p.\n", iface, shader);
    SET_STATE(&context->gs, shader);
}

static void STDMETHODCALLTYPE d3d11_context_IASetPrimitiveTopology(ID3D11DeviceContext *iface,
        D3D11_PRIMITIVE_TOPOLOGY topology)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, topology %u.\n", iface, topology);
    context->topology = topology;
}

static void STDMETHODCALLTYPE d3d11_context_VSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView *const *views)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, start %u, count %u, views %p.\n", iface, start, count, views);
    if (start >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT_)
        return;
    count = min_uint(count, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT_ - start);
    for (i = 0; i < count; i++)
        SET_STATE(&context->vs_srvs[start + i], views ? views[i] : NULL);
}

static void STDMETHODCALLTYPE d3d11_context_VSSetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState *const *samplers)
{
    TRACE("iface %p, start %u, count %u.\n", iface, start, count);
}

static void STDMETHODCALLTYPE d3d11_context_Begin(ID3D11DeviceContext *iface, ID3D11Asynchronous *async)
{
    FIXME("iface %p, async %p stub!\n", iface, async);
}

static void STDMETHODCALLTYPE d3d11_context_End(ID3D11DeviceContext *iface, ID3D11Asynchronous *async)
{
    FIXME("iface %p, async %p stub!\n", iface, async);
}

static HRESULT STDMETHODCALLTYPE d3d11_context_GetData(ID3D11DeviceContext *iface,
        ID3D11Asynchronous *async, void *data, UINT data_size, UINT flags)
{
    FIXME("stub!\n");
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d11_context_SetPredication(ID3D11DeviceContext *iface,
        ID3D11Predicate *predicate, BOOL value)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_GSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView *const *views)
{
    TRACE("iface %p, start %u, count %u.\n", iface, start, count);
}

static void STDMETHODCALLTYPE d3d11_context_GSSetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState *const *samplers)
{
    TRACE("iface %p, start %u, count %u.\n", iface, start, count);
}

static void STDMETHODCALLTYPE d3d11_context_OMSetRenderTargets(ID3D11DeviceContext *iface,
        UINT count, ID3D11RenderTargetView *const *rtvs, ID3D11DepthStencilView *dsv)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, count %u, rtvs %p, dsv %p.\n", iface, count, rtvs, dsv);
    count = min_uint(count, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
    for (i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
        SET_STATE(&context->render_targets[i], (i < count && rtvs) ? rtvs[i] : NULL);
    SET_STATE(&context->depth_stencil, dsv);
}

static void STDMETHODCALLTYPE d3d11_context_OMSetRenderTargetsAndUnorderedAccessViews(
        ID3D11DeviceContext *iface, UINT rtv_count, ID3D11RenderTargetView *const *rtvs,
        ID3D11DepthStencilView *dsv, UINT uav_start, UINT uav_count,
        ID3D11UnorderedAccessView *const *uavs, const UINT *initial_counts)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_OMSetBlendState(ID3D11DeviceContext *iface,
        ID3D11BlendState *state, const FLOAT blend_factor[4], UINT sample_mask)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, state %p, sample_mask %#x.\n", iface, state, sample_mask);
    SET_STATE(&context->blend_state, state);
    if (blend_factor)
    {
        context->blend_factor[0] = blend_factor[0];
        context->blend_factor[1] = blend_factor[1];
        context->blend_factor[2] = blend_factor[2];
        context->blend_factor[3] = blend_factor[3];
    }
    context->sample_mask = sample_mask;
}

static void STDMETHODCALLTYPE d3d11_context_OMSetDepthStencilState(ID3D11DeviceContext *iface,
        ID3D11DepthStencilState *state, UINT stencil_ref)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, state %p, stencil_ref %u.\n", iface, state, stencil_ref);
    SET_STATE(&context->depth_stencil_state, state);
    context->stencil_ref = stencil_ref;
}

static void STDMETHODCALLTYPE d3d11_context_SOSetTargets(ID3D11DeviceContext *iface,
        UINT count, ID3D11Buffer *const *buffers, const UINT *offsets)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_DrawAuto(ID3D11DeviceContext *iface)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_DrawIndexedInstancedIndirect(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, UINT offset)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_DrawInstancedIndirect(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, UINT offset)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_Dispatch(ID3D11DeviceContext *iface, UINT x, UINT y, UINT z)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_DispatchIndirect(ID3D11DeviceContext *iface,
        ID3D11Buffer *buffer, UINT offset)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_RSSetState(ID3D11DeviceContext *iface,
        ID3D11RasterizerState *state)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, state %p.\n", iface, state);
    SET_STATE(&context->rasterizer_state, state);
}

static void STDMETHODCALLTYPE d3d11_context_RSSetViewports(ID3D11DeviceContext *iface,
        UINT count, const D3D11_VIEWPORT *viewports)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, count %u, viewports %p.\n", iface, count, viewports);
    if (count && !viewports)
    {
        context->viewport_count = 0;
        return;
    }
    context->viewport_count = count < D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE_
            ? count : D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE_;
    for (i = 0; i < context->viewport_count; i++)
        context->viewports[i] = viewports[i];
}

static void STDMETHODCALLTYPE d3d11_context_RSSetScissorRects(ID3D11DeviceContext *iface,
        UINT count, const D3D11_RECT *rects)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;
    TRACE("iface %p, count %u, rects %p.\n", iface, count, rects);
    if (count && !rects)
    {
        context->scissor_count = 0;
        return;
    }
    context->scissor_count = count < D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE_
            ? count : D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE_;
    for (i = 0; i < context->scissor_count; i++)
        context->scissor_rects[i] = rects[i];
}

static void STDMETHODCALLTYPE d3d11_context_CopySubresourceRegion(ID3D11DeviceContext *iface,
        ID3D11Resource *dst, UINT dst_subresource, UINT dst_x, UINT dst_y, UINT dst_z,
        ID3D11Resource *src, UINT src_subresource, const D3D11_BOX *src_box)
{
    FIXME("iface %p, dst %p, dst_subresource %u, dst (%u,%u,%u), src %p, "
            "src_subresource %u, src_box %p stub!\n",
            iface, dst, dst_subresource, dst_x, dst_y, dst_z, src, src_subresource, src_box);
}

static void STDMETHODCALLTYPE d3d11_context_CopyResource(ID3D11DeviceContext *iface,
        ID3D11Resource *dst, ID3D11Resource *src)
{
    FIXME("iface %p, dst %p, src %p stub!\n", iface, dst, src);
}

static void STDMETHODCALLTYPE d3d11_context_UpdateSubresource(ID3D11DeviceContext *iface,
        ID3D11Resource *resource, UINT subresource, const D3D11_BOX *box,
        const void *data, UINT row_pitch, UINT depth_pitch)
{
    HRESULT hr;

    TRACE("iface %p, resource %p, subresource %u, box %p, data %p, row_pitch %u, depth_pitch %u.\n",
            iface, resource, subresource, box, data, row_pitch, depth_pitch);

    hr = d3d11_texture_resource_update(resource, subresource, box, data, row_pitch, depth_pitch);
    if (FAILED(hr))
        WARN("UpdateSubresource ignored, hr %#lx.\n", hr);
}

static void STDMETHODCALLTYPE d3d11_context_CopyStructureCount(ID3D11DeviceContext *iface,
        ID3D11Buffer *dst, UINT dst_offset, ID3D11UnorderedAccessView *src)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_ClearRenderTargetView(ID3D11DeviceContext *iface,
        ID3D11RenderTargetView *view, const FLOAT color[4])
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    HRESULT hr;

    TRACE("iface %p, view %p, color %p.\n", iface, view, color);

    hr = d3d11_context_clear_rtv(context, view, color);
    if (FAILED(hr))
        WARN("ClearRenderTargetView ignored, hr %#lx.\n", hr);
}

static void STDMETHODCALLTYPE d3d11_context_ClearUnorderedAccessViewUint(ID3D11DeviceContext *iface,
        ID3D11UnorderedAccessView *view, const UINT values[4])
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_ClearUnorderedAccessViewFloat(ID3D11DeviceContext *iface,
        ID3D11UnorderedAccessView *view, const FLOAT values[4])
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_ClearDepthStencilView(ID3D11DeviceContext *iface,
        ID3D11DepthStencilView *view, UINT flags, FLOAT depth, UINT8 stencil)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_GenerateMips(ID3D11DeviceContext *iface,
        ID3D11ShaderResourceView *view)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_SetResourceMinLOD(ID3D11DeviceContext *iface,
        ID3D11Resource *resource, FLOAT min_lod)
{
    FIXME("stub!\n");
}

static FLOAT STDMETHODCALLTYPE d3d11_context_GetResourceMinLOD(ID3D11DeviceContext *iface,
        ID3D11Resource *resource)
{
    FIXME("stub!\n");
    return 0.0f;
}

static void STDMETHODCALLTYPE d3d11_context_ResolveSubresource(ID3D11DeviceContext *iface,
        ID3D11Resource *dst, UINT dst_subresource, ID3D11Resource *src,
        UINT src_subresource, DXGI_FORMAT format)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_ExecuteCommandList(ID3D11DeviceContext *iface,
        ID3D11CommandList *list, BOOL restore)
{
    FIXME("stub!\n");
}

static void STDMETHODCALLTYPE d3d11_context_HSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView *const *views) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_HSSetShader(ID3D11DeviceContext *iface,
        ID3D11HullShader *shader, ID3D11ClassInstance *const *instances, UINT count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    SET_STATE(&context->hs, shader);
}
static void STDMETHODCALLTYPE d3d11_context_HSSetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState *const *samplers) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_HSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer *const *buffers) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_DSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView *const *views) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_DSSetShader(ID3D11DeviceContext *iface,
        ID3D11DomainShader *shader, ID3D11ClassInstance *const *instances, UINT count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    SET_STATE(&context->ds, shader);
}
static void STDMETHODCALLTYPE d3d11_context_DSSetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState *const *samplers) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_DSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer *const *buffers) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_CSSetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView *const *views) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_CSSetUnorderedAccessViews(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11UnorderedAccessView *const *views, const UINT *initial_counts) { FIXME("stub!\n"); }
static void STDMETHODCALLTYPE d3d11_context_CSSetShader(ID3D11DeviceContext *iface,
        ID3D11ComputeShader *shader, ID3D11ClassInstance *const *instances, UINT count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    SET_STATE(&context->cs, shader);
}
static void STDMETHODCALLTYPE d3d11_context_CSSetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState *const *samplers) { TRACE("stub.\n"); }
static void STDMETHODCALLTYPE d3d11_context_CSSetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer *const *buffers) { TRACE("stub.\n"); }

/* Get methods - return currently bound state */
static void STDMETHODCALLTYPE d3d11_context_VSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer **buffers)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    get_state_array((IUnknown **)context->vs_cbs, ARRAY_SIZE(context->vs_cbs), start, count,
            (IUnknown **)buffers);
}
static void STDMETHODCALLTYPE d3d11_context_PSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView **views)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    get_state_array((IUnknown **)context->ps_srvs, ARRAY_SIZE(context->ps_srvs), start, count,
            (IUnknown **)views);
}
static void STDMETHODCALLTYPE d3d11_context_PSGetShader(ID3D11DeviceContext *iface,
        ID3D11PixelShader **shader, ID3D11ClassInstance **instances, UINT *count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    GET_STATE(context->ps, shader);
    if (count)
        *count = 0;
}
static void STDMETHODCALLTYPE d3d11_context_PSGetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState **samplers)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    get_state_array((IUnknown **)context->ps_samplers, ARRAY_SIZE(context->ps_samplers), start, count,
            (IUnknown **)samplers);
}
static void STDMETHODCALLTYPE d3d11_context_VSGetShader(ID3D11DeviceContext *iface,
        ID3D11VertexShader **shader, ID3D11ClassInstance **instances, UINT *count)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    GET_STATE(context->vs, shader);
    if (count)
        *count = 0;
}
static void STDMETHODCALLTYPE d3d11_context_PSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer **buffers)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    get_state_array((IUnknown **)context->ps_cbs, ARRAY_SIZE(context->ps_cbs), start, count,
            (IUnknown **)buffers);
}
static void STDMETHODCALLTYPE d3d11_context_IAGetInputLayout(ID3D11DeviceContext *iface,
        ID3D11InputLayout **layout)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    GET_STATE(context->input_layout, layout);
}
static void STDMETHODCALLTYPE d3d11_context_IAGetVertexBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer **buffers, UINT *strides, UINT *offsets)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    UINT i;

    for (i = 0; i < count; ++i)
    {
        if (buffers)
        {
            buffers[i] = NULL;
            if (start + i < ARRAY_SIZE(context->vertex_buffers))
                GET_STATE(context->vertex_buffers[start + i], &buffers[i]);
        }
        if (strides)
            strides[i] = (start + i < ARRAY_SIZE(context->vertex_strides))
                    ? context->vertex_strides[start + i] : 0;
        if (offsets)
            offsets[i] = (start + i < ARRAY_SIZE(context->vertex_offsets))
                    ? context->vertex_offsets[start + i] : 0;
    }
}
static void STDMETHODCALLTYPE d3d11_context_IAGetIndexBuffer(ID3D11DeviceContext *iface,
        ID3D11Buffer **buffer, DXGI_FORMAT *format, UINT *offset)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    GET_STATE(context->index_buffer, buffer);
    if (format)
        *format = context->index_format;
    if (offset)
        *offset = context->index_offset;
}
static void STDMETHODCALLTYPE d3d11_context_GSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer **buffers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)buffers, count);
}
static void STDMETHODCALLTYPE d3d11_context_GSGetShader(ID3D11DeviceContext *iface,
        ID3D11GeometryShader **shader, ID3D11ClassInstance **instances, UINT *count)
{
    FIXME("stub!\n");
    clear_shader_state((IUnknown **)shader, (IUnknown **)instances, count);
}

static void STDMETHODCALLTYPE d3d11_context_IAGetPrimitiveTopology(ID3D11DeviceContext *iface,
        D3D11_PRIMITIVE_TOPOLOGY *topology)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, topology %p.\n", iface, topology);
    if (topology)
        *topology = context->topology;
}

static void STDMETHODCALLTYPE d3d11_context_VSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView **views)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    get_state_array((IUnknown **)context->vs_srvs, ARRAY_SIZE(context->vs_srvs), start, count,
            (IUnknown **)views);
}
static void STDMETHODCALLTYPE d3d11_context_VSGetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState **samplers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)samplers, count);
}
static void STDMETHODCALLTYPE d3d11_context_GetPredication(ID3D11DeviceContext *iface,
        ID3D11Predicate **predicate, BOOL *value)
{
    FIXME("stub!\n");
    if (predicate)
        *predicate = NULL;
    if (value)
        *value = FALSE;
}
static void STDMETHODCALLTYPE d3d11_context_GSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView **views)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)views, count);
}
static void STDMETHODCALLTYPE d3d11_context_GSGetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState **samplers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)samplers, count);
}
static void STDMETHODCALLTYPE d3d11_context_OMGetRenderTargets(ID3D11DeviceContext *iface,
        UINT count, ID3D11RenderTargetView **rtvs, ID3D11DepthStencilView **dsv)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    get_state_array((IUnknown **)context->render_targets, ARRAY_SIZE(context->render_targets), 0, count,
            (IUnknown **)rtvs);
    GET_STATE(context->depth_stencil, dsv);
}
static void STDMETHODCALLTYPE d3d11_context_OMGetRenderTargetsAndUnorderedAccessViews(
        ID3D11DeviceContext *iface, UINT rtv_count, ID3D11RenderTargetView **rtvs,
        ID3D11DepthStencilView **dsv, UINT uav_start, UINT uav_count,
        ID3D11UnorderedAccessView **uavs)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)rtvs, rtv_count);
    if (dsv)
        *dsv = NULL;
    clear_state_array((IUnknown **)uavs, uav_count);
}
static void STDMETHODCALLTYPE d3d11_context_OMGetBlendState(ID3D11DeviceContext *iface,
        ID3D11BlendState **state, FLOAT blend_factor[4], UINT *sample_mask)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    GET_STATE(context->blend_state, state);
    if (blend_factor)
        memcpy(blend_factor, context->blend_factor, sizeof(context->blend_factor));
    if (sample_mask)
        *sample_mask = context->sample_mask;
}
static void STDMETHODCALLTYPE d3d11_context_OMGetDepthStencilState(ID3D11DeviceContext *iface,
        ID3D11DepthStencilState **state, UINT *stencil_ref)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    GET_STATE(context->depth_stencil_state, state);
    if (stencil_ref)
        *stencil_ref = context->stencil_ref;
}
static void STDMETHODCALLTYPE d3d11_context_SOGetTargets(ID3D11DeviceContext *iface,
        UINT count, ID3D11Buffer **buffers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)buffers, count);
}
static void STDMETHODCALLTYPE d3d11_context_RSGetState(ID3D11DeviceContext *iface,
        ID3D11RasterizerState **state)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    GET_STATE(context->rasterizer_state, state);
}

static void STDMETHODCALLTYPE d3d11_context_RSGetViewports(ID3D11DeviceContext *iface,
        UINT *count, D3D11_VIEWPORT *viewports)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p, count %p, viewports %p.\n", iface, count, viewports);
    if (!count)
        return;
    if (!viewports)
    {
        *count = context->viewport_count;
        return;
    }
    if (*count > context->viewport_count)
        *count = context->viewport_count;
    if (*count)
        memcpy(viewports, context->viewports, *count * sizeof(*viewports));
}

static void STDMETHODCALLTYPE d3d11_context_RSGetScissorRects(ID3D11DeviceContext *iface,
        UINT *count, D3D11_RECT *rects)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);

    if (!count)
        return;
    if (!rects)
    {
        *count = context->scissor_count;
        return;
    }
    if (*count > context->scissor_count)
        *count = context->scissor_count;
    if (*count)
        memcpy(rects, context->scissor_rects, *count * sizeof(*rects));
}
static void STDMETHODCALLTYPE d3d11_context_HSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView **views)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)views, count);
}
static void STDMETHODCALLTYPE d3d11_context_HSGetShader(ID3D11DeviceContext *iface,
        ID3D11HullShader **shader, ID3D11ClassInstance **instances, UINT *count)
{
    FIXME("stub!\n");
    clear_shader_state((IUnknown **)shader, (IUnknown **)instances, count);
}
static void STDMETHODCALLTYPE d3d11_context_HSGetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState **samplers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)samplers, count);
}
static void STDMETHODCALLTYPE d3d11_context_HSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer **buffers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)buffers, count);
}
static void STDMETHODCALLTYPE d3d11_context_DSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView **views)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)views, count);
}
static void STDMETHODCALLTYPE d3d11_context_DSGetShader(ID3D11DeviceContext *iface,
        ID3D11DomainShader **shader, ID3D11ClassInstance **instances, UINT *count)
{
    FIXME("stub!\n");
    clear_shader_state((IUnknown **)shader, (IUnknown **)instances, count);
}
static void STDMETHODCALLTYPE d3d11_context_DSGetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState **samplers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)samplers, count);
}
static void STDMETHODCALLTYPE d3d11_context_DSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer **buffers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)buffers, count);
}
static void STDMETHODCALLTYPE d3d11_context_CSGetShaderResources(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11ShaderResourceView **views)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)views, count);
}
static void STDMETHODCALLTYPE d3d11_context_CSGetUnorderedAccessViews(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11UnorderedAccessView **views)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)views, count);
}
static void STDMETHODCALLTYPE d3d11_context_CSGetShader(ID3D11DeviceContext *iface,
        ID3D11ComputeShader **shader, ID3D11ClassInstance **instances, UINT *count)
{
    FIXME("stub!\n");
    clear_shader_state((IUnknown **)shader, (IUnknown **)instances, count);
}
static void STDMETHODCALLTYPE d3d11_context_CSGetSamplers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11SamplerState **samplers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)samplers, count);
}
static void STDMETHODCALLTYPE d3d11_context_CSGetConstantBuffers(ID3D11DeviceContext *iface,
        UINT start, UINT count, ID3D11Buffer **buffers)
{
    FIXME("stub!\n");
    clear_state_array((IUnknown **)buffers, count);
}

static void STDMETHODCALLTYPE d3d11_context_ClearState(ID3D11DeviceContext *iface)
{
    struct d3d11_immediate_context *context = impl_from_ID3D11DeviceContext(iface);
    TRACE("iface %p.\n", iface);
    d3d11_context_reset_state(context);
}

static void STDMETHODCALLTYPE d3d11_context_Flush(ID3D11DeviceContext *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE d3d11_context_GetType(ID3D11DeviceContext *iface)
{
    TRACE("iface %p.\n", iface);
    return D3D11_DEVICE_CONTEXT_IMMEDIATE;
}

static UINT STDMETHODCALLTYPE d3d11_context_GetContextFlags(ID3D11DeviceContext *iface)
{
    TRACE("iface %p.\n", iface);
    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d11_context_FinishCommandList(ID3D11DeviceContext *iface,
        BOOL restore, ID3D11CommandList **list)
{
    FIXME("stub!\n");
    return DXGI_ERROR_INVALID_CALL;
}

static struct ID3D11DeviceContextVtbl d3d11_context_vtbl =
{
    d3d11_context_QueryInterface,
    d3d11_context_AddRef,
    d3d11_context_Release,
    d3d11_context_GetDevice,
    d3d11_context_GetPrivateData,
    d3d11_context_SetPrivateData,
    d3d11_context_SetPrivateDataInterface,
    d3d11_context_VSSetConstantBuffers,
    d3d11_context_PSSetShaderResources,
    d3d11_context_PSSetShader,
    d3d11_context_PSSetSamplers,
    d3d11_context_VSSetShader,
    d3d11_context_DrawIndexed,
    d3d11_context_Draw,
    d3d11_context_Map,
    d3d11_context_Unmap,
    d3d11_context_PSSetConstantBuffers,
    d3d11_context_IASetInputLayout,
    d3d11_context_IASetVertexBuffers,
    d3d11_context_IASetIndexBuffer,
    d3d11_context_DrawIndexedInstanced,
    d3d11_context_DrawInstanced,
    d3d11_context_GSSetConstantBuffers,
    d3d11_context_GSSetShader,
    d3d11_context_IASetPrimitiveTopology,
    d3d11_context_VSSetShaderResources,
    d3d11_context_VSSetSamplers,
    d3d11_context_Begin,
    d3d11_context_End,
    d3d11_context_GetData,
    d3d11_context_SetPredication,
    d3d11_context_GSSetShaderResources,
    d3d11_context_GSSetSamplers,
    d3d11_context_OMSetRenderTargets,
    d3d11_context_OMSetRenderTargetsAndUnorderedAccessViews,
    d3d11_context_OMSetBlendState,
    d3d11_context_OMSetDepthStencilState,
    d3d11_context_SOSetTargets,
    d3d11_context_DrawAuto,
    d3d11_context_DrawIndexedInstancedIndirect,
    d3d11_context_DrawInstancedIndirect,
    d3d11_context_Dispatch,
    d3d11_context_DispatchIndirect,
    d3d11_context_RSSetState,
    d3d11_context_RSSetViewports,
    d3d11_context_RSSetScissorRects,
    d3d11_context_CopySubresourceRegion,
    d3d11_context_CopyResource,
    d3d11_context_UpdateSubresource,
    d3d11_context_CopyStructureCount,
    d3d11_context_ClearRenderTargetView,
    d3d11_context_ClearUnorderedAccessViewUint,
    d3d11_context_ClearUnorderedAccessViewFloat,
    d3d11_context_ClearDepthStencilView,
    d3d11_context_GenerateMips,
    d3d11_context_SetResourceMinLOD,
    d3d11_context_GetResourceMinLOD,
    d3d11_context_ResolveSubresource,
    d3d11_context_ExecuteCommandList,
    d3d11_context_HSSetShaderResources,
    d3d11_context_HSSetShader,
    d3d11_context_HSSetSamplers,
    d3d11_context_HSSetConstantBuffers,
    d3d11_context_DSSetShaderResources,
    d3d11_context_DSSetShader,
    d3d11_context_DSSetSamplers,
    d3d11_context_DSSetConstantBuffers,
    d3d11_context_CSSetShaderResources,
    d3d11_context_CSSetUnorderedAccessViews,
    d3d11_context_CSSetShader,
    d3d11_context_CSSetSamplers,
    d3d11_context_CSSetConstantBuffers,
    d3d11_context_VSGetConstantBuffers,
    d3d11_context_PSGetShaderResources,
    d3d11_context_PSGetShader,
    d3d11_context_PSGetSamplers,
    d3d11_context_VSGetShader,
    d3d11_context_PSGetConstantBuffers,
    d3d11_context_IAGetInputLayout,
    d3d11_context_IAGetVertexBuffers,
    d3d11_context_IAGetIndexBuffer,
    d3d11_context_GSGetConstantBuffers,
    d3d11_context_GSGetShader,
    d3d11_context_IAGetPrimitiveTopology,
    d3d11_context_VSGetShaderResources,
    d3d11_context_VSGetSamplers,
    d3d11_context_GetPredication,
    d3d11_context_GSGetShaderResources,
    d3d11_context_GSGetSamplers,
    d3d11_context_OMGetRenderTargets,
    d3d11_context_OMGetRenderTargetsAndUnorderedAccessViews,
    d3d11_context_OMGetBlendState,
    d3d11_context_OMGetDepthStencilState,
    d3d11_context_SOGetTargets,
    d3d11_context_RSGetState,
    d3d11_context_RSGetViewports,
    d3d11_context_RSGetScissorRects,
    d3d11_context_HSGetShaderResources,
    d3d11_context_HSGetShader,
    d3d11_context_HSGetSamplers,
    d3d11_context_HSGetConstantBuffers,
    d3d11_context_DSGetShaderResources,
    d3d11_context_DSGetShader,
    d3d11_context_DSGetSamplers,
    d3d11_context_DSGetConstantBuffers,
    d3d11_context_CSGetShaderResources,
    d3d11_context_CSGetUnorderedAccessViews,
    d3d11_context_CSGetShader,
    d3d11_context_CSGetSamplers,
    d3d11_context_CSGetConstantBuffers,
    d3d11_context_ClearState,
    d3d11_context_Flush,
    d3d11_context_GetType,
    d3d11_context_GetContextFlags,
    d3d11_context_FinishCommandList,
};

void d3d11_immediate_context_init(struct d3d11_immediate_context *context,
        struct d3d11_device *device)
{
    context->ID3D11DeviceContext_iface.lpVtbl = &d3d11_context_vtbl;
    context->refcount = 0;
    context->device = device;
    context->index_format = DXGI_FORMAT_UNKNOWN;
    context->sample_mask = D3D11_DEFAULT_SAMPLE_MASK;
}

#undef GET_STATE
#undef SET_STATE
