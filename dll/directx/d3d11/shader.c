/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shader object implementations (VS, PS, GS, HS, DS, CS)
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

/* Common shader implementation macro */
#define IMPL_SHADER(prefix, type, iid) \
static HRESULT STDMETHODCALLTYPE prefix##_QueryInterface(type *iface, REFIID riid, void **out) \
{ \
    if (!out) \
        return E_POINTER; \
    *out = NULL; \
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) \
            || IsEqualGUID(riid, &iid)) \
    { *out = iface; type##_AddRef(iface); return S_OK; } \
    return E_NOINTERFACE; \
} \
static ULONG STDMETHODCALLTYPE prefix##_AddRef(type *iface) \
{ return InterlockedIncrement(&impl_from_##type(iface)->refcount); } \
static ULONG STDMETHODCALLTYPE prefix##_Release(type *iface) \
{ \
    struct d3d11_##prefix *s = impl_from_##type(iface); \
    ULONG r = InterlockedDecrement(&s->refcount); \
    if (!r) { HeapFree(GetProcessHeap(), 0, s->bytecode); d3d11_device_child_cleanup(&s->child); HeapFree(GetProcessHeap(), 0, s); } \
    return r; \
} \
static void STDMETHODCALLTYPE prefix##_GetDevice(type *iface, ID3D11Device **d) \
{ d3d11_device_child_get_device(impl_from_##type(iface)->child.device, d); } \
static HRESULT STDMETHODCALLTYPE prefix##_GetPrivateData(type *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; } \
static HRESULT STDMETHODCALLTYPE prefix##_SetPrivateData(type *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; } \
static HRESULT STDMETHODCALLTYPE prefix##_SetPrivateDataInterface(type *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; }

IMPL_SHADER(vertex_shader, ID3D11VertexShader, IID_ID3D11VertexShader)
static const struct ID3D11VertexShaderVtbl vs_vtbl = {
    vertex_shader_QueryInterface, vertex_shader_AddRef, vertex_shader_Release,
    vertex_shader_GetDevice, vertex_shader_GetPrivateData,
    vertex_shader_SetPrivateData, vertex_shader_SetPrivateDataInterface,
};

IMPL_SHADER(pixel_shader, ID3D11PixelShader, IID_ID3D11PixelShader)
static const struct ID3D11PixelShaderVtbl ps_vtbl = {
    pixel_shader_QueryInterface, pixel_shader_AddRef, pixel_shader_Release,
    pixel_shader_GetDevice, pixel_shader_GetPrivateData,
    pixel_shader_SetPrivateData, pixel_shader_SetPrivateDataInterface,
};

IMPL_SHADER(geometry_shader, ID3D11GeometryShader, IID_ID3D11GeometryShader)
static const struct ID3D11GeometryShaderVtbl gs_vtbl = {
    geometry_shader_QueryInterface, geometry_shader_AddRef, geometry_shader_Release,
    geometry_shader_GetDevice, geometry_shader_GetPrivateData,
    geometry_shader_SetPrivateData, geometry_shader_SetPrivateDataInterface,
};

IMPL_SHADER(hull_shader, ID3D11HullShader, IID_ID3D11HullShader)
static const struct ID3D11HullShaderVtbl hs_vtbl = {
    hull_shader_QueryInterface, hull_shader_AddRef, hull_shader_Release,
    hull_shader_GetDevice, hull_shader_GetPrivateData,
    hull_shader_SetPrivateData, hull_shader_SetPrivateDataInterface,
};

IMPL_SHADER(domain_shader, ID3D11DomainShader, IID_ID3D11DomainShader)
static const struct ID3D11DomainShaderVtbl ds_vtbl = {
    domain_shader_QueryInterface, domain_shader_AddRef, domain_shader_Release,
    domain_shader_GetDevice, domain_shader_GetPrivateData,
    domain_shader_SetPrivateData, domain_shader_SetPrivateDataInterface,
};

IMPL_SHADER(compute_shader, ID3D11ComputeShader, IID_ID3D11ComputeShader)
static const struct ID3D11ComputeShaderVtbl cs_vtbl = {
    compute_shader_QueryInterface, compute_shader_AddRef, compute_shader_Release,
    compute_shader_GetDevice, compute_shader_GetPrivateData,
    compute_shader_SetPrivateData, compute_shader_SetPrivateDataInterface,
};

/* Common create helper */
static HRESULT shader_create_common(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, void **bytecode_out,
        SIZE_T *length_out, d3d11_device_child_info *child)
{
    HRESULT hr;

    if (!bytecode || !length)
        return E_INVALIDARG;
    if (!bytecode_out || !length_out || !child)
        return E_INVALIDARG;

    *bytecode_out = NULL;
    *length_out = 0;

    hr = d3d11_device_child_init(child, device);
    if (FAILED(hr))
        return hr;

    *bytecode_out = HeapAlloc(GetProcessHeap(), 0, length);
    if (!*bytecode_out)
    {
        d3d11_device_child_cleanup(child);
        return E_OUTOFMEMORY;
    }

    memcpy(*bytecode_out, bytecode, length);
    *length_out = length;
    return S_OK;
}

HRESULT d3d11_vertex_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_vertex_shader **out)
{
    struct d3d11_vertex_shader *s;
    HRESULT hr;
    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = shader_create_common(device, bytecode, length, &s->bytecode, &s->bytecode_length, &s->child);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, s); return hr; }
    s->ID3D11VertexShader_iface.lpVtbl = &vs_vtbl;
    s->refcount = 1;
    *out = s;
    return S_OK;
}

HRESULT d3d11_pixel_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_pixel_shader **out)
{
    struct d3d11_pixel_shader *s;
    HRESULT hr;
    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = shader_create_common(device, bytecode, length, &s->bytecode, &s->bytecode_length, &s->child);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, s); return hr; }
    s->ID3D11PixelShader_iface.lpVtbl = &ps_vtbl;
    s->refcount = 1;
    *out = s;
    return S_OK;
}

HRESULT d3d11_geometry_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_geometry_shader **out)
{
    struct d3d11_geometry_shader *s;
    HRESULT hr;
    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = shader_create_common(device, bytecode, length, &s->bytecode, &s->bytecode_length, &s->child);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, s); return hr; }
    s->ID3D11GeometryShader_iface.lpVtbl = &gs_vtbl;
    s->refcount = 1;
    *out = s;
    return S_OK;
}

HRESULT d3d11_hull_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_hull_shader **out)
{
    struct d3d11_hull_shader *s;
    HRESULT hr;
    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = shader_create_common(device, bytecode, length, &s->bytecode, &s->bytecode_length, &s->child);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, s); return hr; }
    s->ID3D11HullShader_iface.lpVtbl = &hs_vtbl;
    s->refcount = 1;
    *out = s;
    return S_OK;
}

HRESULT d3d11_domain_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_domain_shader **out)
{
    struct d3d11_domain_shader *s;
    HRESULT hr;
    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = shader_create_common(device, bytecode, length, &s->bytecode, &s->bytecode_length, &s->child);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, s); return hr; }
    s->ID3D11DomainShader_iface.lpVtbl = &ds_vtbl;
    s->refcount = 1;
    *out = s;
    return S_OK;
}

HRESULT d3d11_compute_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_compute_shader **out)
{
    struct d3d11_compute_shader *s;
    HRESULT hr;
    if (!out)
        return E_INVALIDARG;
    *out = NULL;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return E_OUTOFMEMORY;
    hr = shader_create_common(device, bytecode, length, &s->bytecode, &s->bytecode_length, &s->child);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(), 0, s); return hr; }
    s->ID3D11ComputeShader_iface.lpVtbl = &cs_vtbl;
    s->refcount = 1;
    *out = s;
    return S_OK;
}
