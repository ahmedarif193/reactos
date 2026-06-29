/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ID3D11Texture1D/2D/3D implementation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

static BOOL checked_mul_size(SIZE_T a, SIZE_T b, SIZE_T *out)
{
    if (a && b > ~(SIZE_T)0 / a)
        return FALSE;

    *out = a * b;
    return TRUE;
}

static HRESULT texture_bytes_per_pixel(DXGI_FORMAT format, UINT *bytes)
{
    UINT bpp;

    if (format == DXGI_FORMAT_UNKNOWN)
        return E_INVALIDARG;

    bpp = d3d11_dxgi_format_bpp(format);
    if (!bpp || bpp % 8)
        return E_INVALIDARG;

    *bytes = bpp / 8;
    return S_OK;
}

static HRESULT texture_check_basic_layout(UINT mip_levels, UINT array_size)
{
    if (!mip_levels || !array_size)
        return E_INVALIDARG;

    if (mip_levels != 1 || array_size != 1)
        return E_NOTIMPL;

    return S_OK;
}

static HRESULT texture_alloc_sysmem(void **memory, SIZE_T size)
{
    *memory = NULL;

    if (!size)
        return S_OK;

    *memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!*memory)
        return E_OUTOFMEMORY;

    return S_OK;
}

static HRESULT texture_copy_rows(void *dst, UINT dst_pitch, UINT row_count, UINT row_size,
        const void *src, UINT src_pitch)
{
    BYTE *dst_row = dst;
    const BYTE *src_row = src;
    UINT y;

    if (!src)
        return S_OK;

    if (!src_pitch)
        src_pitch = row_size;
    if (src_pitch < row_size || dst_pitch < row_size)
        return E_INVALIDARG;

    for (y = 0; y < row_count; ++y)
    {
        memcpy(dst_row, src_row, row_size);
        dst_row += dst_pitch;
        src_row += src_pitch;
    }

    return S_OK;
}

static HRESULT texture_check_map(UINT usage, UINT cpu_access_flags, D3D11_MAP type, UINT flags)
{
    if (flags & ~D3D11_MAP_FLAG_DO_NOT_WAIT)
        return E_INVALIDARG;

    switch (type)
    {
        case D3D11_MAP_READ:
            if (!(cpu_access_flags & D3D11_CPU_ACCESS_READ))
                return DXGI_ERROR_INVALID_CALL;
            break;

        case D3D11_MAP_WRITE:
        case D3D11_MAP_WRITE_DISCARD:
            if (!(cpu_access_flags & D3D11_CPU_ACCESS_WRITE))
                return DXGI_ERROR_INVALID_CALL;
            break;

        case D3D11_MAP_READ_WRITE:
            if ((cpu_access_flags & (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE))
                    != (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE))
                return DXGI_ERROR_INVALID_CALL;
            break;

        case D3D11_MAP_WRITE_NO_OVERWRITE:
            return DXGI_ERROR_INVALID_CALL;

        default:
            return E_INVALIDARG;
    }

    if (usage == D3D11_USAGE_DEFAULT || usage == D3D11_USAGE_IMMUTABLE)
        return DXGI_ERROR_INVALID_CALL;

    return S_OK;
}

static HRESULT texture1d_update(struct d3d11_texture1d *texture, const D3D11_BOX *box,
        const void *data, UINT row_pitch)
{
    UINT bytes;
    UINT left = 0, right = texture->desc.Width;
    HRESULT hr;

    if (!data)
        return E_INVALIDARG;
    if (texture->desc.Usage == D3D11_USAGE_IMMUTABLE)
        return DXGI_ERROR_INVALID_CALL;
    if (box)
    {
        if (box->top || box->front || box->bottom > 1 || box->back > 1
                || box->left >= box->right || box->right > texture->desc.Width)
            return E_INVALIDARG;
        left = box->left;
        right = box->right;
    }

    hr = texture_bytes_per_pixel(texture->desc.Format, &bytes);
    if (FAILED(hr))
        return hr;

    return texture_copy_rows((BYTE *)texture->sysmem + left * bytes, texture->row_pitch,
            1, (right - left) * bytes, data, row_pitch);
}

static HRESULT texture2d_update(struct d3d11_texture2d *texture, const D3D11_BOX *box,
        const void *data, UINT row_pitch)
{
    UINT bytes;
    UINT left = 0, top = 0, right = texture->desc.Width, bottom = texture->desc.Height;
    HRESULT hr;

    if (!data)
        return E_INVALIDARG;
    if (texture->desc.Usage == D3D11_USAGE_IMMUTABLE)
        return DXGI_ERROR_INVALID_CALL;
    if (box)
    {
        if (box->front || box->back > 1 || box->left >= box->right || box->top >= box->bottom
                || box->right > texture->desc.Width || box->bottom > texture->desc.Height)
            return E_INVALIDARG;
        left = box->left;
        top = box->top;
        right = box->right;
        bottom = box->bottom;
    }

    hr = texture_bytes_per_pixel(texture->desc.Format, &bytes);
    if (FAILED(hr))
        return hr;

    return texture_copy_rows((BYTE *)texture->sysmem + top * texture->row_pitch + left * bytes,
            texture->row_pitch, bottom - top, (right - left) * bytes, data, row_pitch);
}

static HRESULT texture3d_update(struct d3d11_texture3d *texture, const D3D11_BOX *box,
        const void *data, UINT row_pitch, UINT depth_pitch)
{
    const BYTE *src_slice = data;
    BYTE *dst_slice;
    UINT bytes;
    UINT left = 0, top = 0, front = 0;
    UINT right = texture->desc.Width, bottom = texture->desc.Height, back = texture->desc.Depth;
    UINT row_size, rows, slices, z;
    HRESULT hr;

    if (!data)
        return E_INVALIDARG;
    if (texture->desc.Usage == D3D11_USAGE_IMMUTABLE)
        return DXGI_ERROR_INVALID_CALL;
    if (box)
    {
        if (box->left >= box->right || box->top >= box->bottom || box->front >= box->back
                || box->right > texture->desc.Width || box->bottom > texture->desc.Height
                || box->back > texture->desc.Depth)
            return E_INVALIDARG;
        left = box->left;
        top = box->top;
        front = box->front;
        right = box->right;
        bottom = box->bottom;
        back = box->back;
    }

    hr = texture_bytes_per_pixel(texture->desc.Format, &bytes);
    if (FAILED(hr))
        return hr;

    row_size = (right - left) * bytes;
    rows = bottom - top;
    slices = back - front;
    if (!row_pitch)
        row_pitch = row_size;
    if (!depth_pitch)
        depth_pitch = row_pitch * rows;
    if (row_pitch < row_size || depth_pitch < row_pitch * rows)
        return E_INVALIDARG;

    dst_slice = (BYTE *)texture->sysmem + front * texture->slice_pitch
            + top * texture->row_pitch + left * bytes;

    for (z = 0; z < slices; ++z)
    {
        hr = texture_copy_rows(dst_slice, texture->row_pitch, rows, row_size,
                src_slice, row_pitch);
        if (FAILED(hr))
            return hr;
        dst_slice += texture->slice_pitch;
        src_slice += depth_pitch;
    }

    return S_OK;
}

static ULONG texture_release_device(struct d3d11_device *device)
{
    if (!device)
        return 0;

    return ID3D11Device_Release(&device->ID3D11Device_iface);
}

/* ========================================================================= */
/* ID3D11Texture1D                                                           */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE tex1d_QueryInterface(ID3D11Texture1D *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11Resource) || IsEqualGUID(riid, &IID_ID3D11Texture1D))
    {
        *out = iface;
        ID3D11Texture1D_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE tex1d_AddRef(ID3D11Texture1D *iface)
{
    return InterlockedIncrement(&impl_from_ID3D11Texture1D(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE tex1d_Release(ID3D11Texture1D *iface)
{
    struct d3d11_texture1d *texture = impl_from_ID3D11Texture1D(iface);
    ULONG refcount = InterlockedDecrement(&texture->refcount);

    if (!refcount)
    {
        struct d3d11_device *device = texture->child.device;

        HeapFree(GetProcessHeap(), 0, texture->sysmem);
        HeapFree(GetProcessHeap(), 0, texture);
        texture_release_device(device);
    }

    return refcount;
}

static void STDMETHODCALLTYPE tex1d_GetDevice(ID3D11Texture1D *iface, ID3D11Device **device)
{
    d3d11_device_child_get_device(impl_from_ID3D11Texture1D(iface)->child.device, device);
}

static HRESULT STDMETHODCALLTYPE tex1d_GetPrivateData(ID3D11Texture1D *iface, REFGUID guid,
        UINT *data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex1d_SetPrivateData(ID3D11Texture1D *iface, REFGUID guid,
        UINT data_size, const void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex1d_SetPrivateDataInterface(ID3D11Texture1D *iface,
        REFGUID guid, const IUnknown *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE tex1d_GetType(ID3D11Texture1D *iface, D3D11_RESOURCE_DIMENSION *dimension)
{
    if (dimension)
        *dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
}

static void STDMETHODCALLTYPE tex1d_SetEvictionPriority(ID3D11Texture1D *iface, UINT priority)
{
    impl_from_ID3D11Texture1D(iface)->eviction_priority = priority;
}

static UINT STDMETHODCALLTYPE tex1d_GetEvictionPriority(ID3D11Texture1D *iface)
{
    return impl_from_ID3D11Texture1D(iface)->eviction_priority;
}

static void STDMETHODCALLTYPE tex1d_GetDesc(ID3D11Texture1D *iface, D3D11_TEXTURE1D_DESC *desc)
{
    if (desc)
        *desc = impl_from_ID3D11Texture1D(iface)->desc;
}

static const struct ID3D11Texture1DVtbl tex1d_vtbl =
{
    tex1d_QueryInterface,
    tex1d_AddRef,
    tex1d_Release,
    tex1d_GetDevice,
    tex1d_GetPrivateData,
    tex1d_SetPrivateData,
    tex1d_SetPrivateDataInterface,
    tex1d_GetType,
    tex1d_SetEvictionPriority,
    tex1d_GetEvictionPriority,
    tex1d_GetDesc,
};

HRESULT d3d11_texture1d_create(struct d3d11_device *device, const D3D11_TEXTURE1D_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *data, struct d3d11_texture1d **out)
{
    struct d3d11_texture1d *texture;
    SIZE_T row_size;
    UINT bytes;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc || !desc->Width || desc->Width > D3D11_REQ_TEXTURE1D_U_DIMENSION)
        return E_INVALIDARG;

    hr = texture_check_basic_layout(desc->MipLevels, desc->ArraySize);
    if (FAILED(hr))
        return hr;

    hr = texture_bytes_per_pixel(desc->Format, &bytes);
    if (FAILED(hr))
        return hr;

    if (!checked_mul_size(desc->Width, bytes, &row_size) || row_size > ~(UINT)0)
        return E_INVALIDARG;

    texture = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*texture));
    if (!texture)
        return E_OUTOFMEMORY;

    hr = texture_alloc_sysmem(&texture->sysmem, row_size);
    if (SUCCEEDED(hr) && data && data->pSysMem)
        hr = texture_copy_rows(texture->sysmem, (UINT)row_size, 1, (UINT)row_size,
                data->pSysMem, data->SysMemPitch);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, texture->sysmem);
        HeapFree(GetProcessHeap(), 0, texture);
        return hr;
    }

    texture->ID3D11Texture1D_iface.lpVtbl = &tex1d_vtbl;
    texture->refcount = 1;
    texture->child.device = device;
    texture->desc = *desc;
    texture->sysmem_size = row_size;
    texture->row_pitch = (UINT)row_size;
    ID3D11Device_AddRef(&device->ID3D11Device_iface);

    *out = texture;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11Texture2D with IDXGISurface                                         */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE tex2d_QueryInterface(ID3D11Texture2D *iface, REFIID riid, void **out)
{
    struct d3d11_texture2d *texture = impl_from_ID3D11Texture2D(iface);

    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11Resource) || IsEqualGUID(riid, &IID_ID3D11Texture2D))
    {
        *out = iface;
        ID3D11Texture2D_AddRef(iface);
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_IDXGIObject) || IsEqualGUID(riid, &IID_IDXGIDeviceSubObject)
            || IsEqualGUID(riid, &IID_IDXGISurface))
    {
        *out = &texture->IDXGISurface_iface;
        ID3D11Texture2D_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE tex2d_AddRef(ID3D11Texture2D *iface)
{
    return InterlockedIncrement(&impl_from_ID3D11Texture2D(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE tex2d_Release(ID3D11Texture2D *iface)
{
    struct d3d11_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    ULONG refcount = InterlockedDecrement(&texture->refcount);

    if (!refcount)
    {
        struct d3d11_device *device = texture->child.device;

        HeapFree(GetProcessHeap(), 0, texture->sysmem);
        HeapFree(GetProcessHeap(), 0, texture);
        texture_release_device(device);
    }

    return refcount;
}

static void STDMETHODCALLTYPE tex2d_GetDevice(ID3D11Texture2D *iface, ID3D11Device **device)
{
    d3d11_device_child_get_device(impl_from_ID3D11Texture2D(iface)->child.device, device);
}

static HRESULT STDMETHODCALLTYPE tex2d_GetPrivateData(ID3D11Texture2D *iface, REFGUID guid,
        UINT *data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex2d_SetPrivateData(ID3D11Texture2D *iface, REFGUID guid,
        UINT data_size, const void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex2d_SetPrivateDataInterface(ID3D11Texture2D *iface,
        REFGUID guid, const IUnknown *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE tex2d_GetType(ID3D11Texture2D *iface, D3D11_RESOURCE_DIMENSION *dimension)
{
    if (dimension)
        *dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
}

static void STDMETHODCALLTYPE tex2d_SetEvictionPriority(ID3D11Texture2D *iface, UINT priority)
{
    impl_from_ID3D11Texture2D(iface)->eviction_priority = priority;
}

static UINT STDMETHODCALLTYPE tex2d_GetEvictionPriority(ID3D11Texture2D *iface)
{
    return impl_from_ID3D11Texture2D(iface)->eviction_priority;
}

static void STDMETHODCALLTYPE tex2d_GetDesc(ID3D11Texture2D *iface, D3D11_TEXTURE2D_DESC *desc)
{
    if (desc)
        *desc = impl_from_ID3D11Texture2D(iface)->desc;
}

static const struct ID3D11Texture2DVtbl tex2d_vtbl =
{
    tex2d_QueryInterface,
    tex2d_AddRef,
    tex2d_Release,
    tex2d_GetDevice,
    tex2d_GetPrivateData,
    tex2d_SetPrivateData,
    tex2d_SetPrivateDataInterface,
    tex2d_GetType,
    tex2d_SetEvictionPriority,
    tex2d_GetEvictionPriority,
    tex2d_GetDesc,
};

static HRESULT STDMETHODCALLTYPE tex2d_surface_QueryInterface(IDXGISurface *iface, REFIID riid,
        void **out)
{
    struct d3d11_texture2d *texture = impl_from_IDXGISurface(iface);

    return tex2d_QueryInterface(&texture->ID3D11Texture2D_iface, riid, out);
}

static ULONG STDMETHODCALLTYPE tex2d_surface_AddRef(IDXGISurface *iface)
{
    struct d3d11_texture2d *texture = impl_from_IDXGISurface(iface);

    return tex2d_AddRef(&texture->ID3D11Texture2D_iface);
}

static ULONG STDMETHODCALLTYPE tex2d_surface_Release(IDXGISurface *iface)
{
    struct d3d11_texture2d *texture = impl_from_IDXGISurface(iface);

    return tex2d_Release(&texture->ID3D11Texture2D_iface);
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_SetPrivateData(IDXGISurface *iface, REFGUID guid,
        UINT data_size, const void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_SetPrivateDataInterface(IDXGISurface *iface,
        REFGUID guid, const IUnknown *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_GetPrivateData(IDXGISurface *iface, REFGUID guid,
        UINT *data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_GetParent(IDXGISurface *iface, REFIID riid, void **parent)
{
    if (!parent)
        return E_POINTER;

    *parent = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_GetDevice(IDXGISurface *iface, REFIID riid, void **device)
{
    struct d3d11_texture2d *texture = impl_from_IDXGISurface(iface);

    if (!device)
        return E_POINTER;

    *device = NULL;
    return ID3D11Device_QueryInterface(&texture->child.device->ID3D11Device_iface, riid, device);
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_GetDesc(IDXGISurface *iface, DXGI_SURFACE_DESC *desc)
{
    struct d3d11_texture2d *texture = impl_from_IDXGISurface(iface);

    if (!desc)
        return E_INVALIDARG;

    desc->Width = texture->desc.Width;
    desc->Height = texture->desc.Height;
    desc->Format = texture->desc.Format;
    desc->SampleDesc = texture->desc.SampleDesc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_Map(IDXGISurface *iface, DXGI_MAPPED_RECT *mapped,
        UINT flags)
{
    struct d3d11_texture2d *texture = impl_from_IDXGISurface(iface);
    D3D11_MAP map_type;
    HRESULT hr;

    if (!mapped)
        return E_INVALIDARG;
    mapped->pBits = NULL;
    mapped->Pitch = 0;

    if (flags & ~(DXGI_MAP_READ | DXGI_MAP_WRITE | DXGI_MAP_DISCARD))
        return E_INVALIDARG;
    if ((flags & DXGI_MAP_DISCARD) && !(flags & DXGI_MAP_WRITE))
        return E_INVALIDARG;

    if ((flags & DXGI_MAP_READ) && (flags & DXGI_MAP_WRITE))
        map_type = D3D11_MAP_READ_WRITE;
    else if (flags & DXGI_MAP_READ)
        map_type = D3D11_MAP_READ;
    else if (flags & DXGI_MAP_DISCARD)
        map_type = D3D11_MAP_WRITE_DISCARD;
    else if (flags & DXGI_MAP_WRITE)
        map_type = D3D11_MAP_WRITE;
    else
        return E_INVALIDARG;

    hr = texture_check_map(texture->desc.Usage, texture->desc.CPUAccessFlags, map_type, 0);
    if (FAILED(hr))
        return hr;

    mapped->pBits = texture->sysmem;
    mapped->Pitch = texture->row_pitch;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tex2d_surface_Unmap(IDXGISurface *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static const struct IDXGISurfaceVtbl tex2d_surface_vtbl =
{
    tex2d_surface_QueryInterface,
    tex2d_surface_AddRef,
    tex2d_surface_Release,
    tex2d_surface_SetPrivateData,
    tex2d_surface_SetPrivateDataInterface,
    tex2d_surface_GetPrivateData,
    tex2d_surface_GetParent,
    tex2d_surface_GetDevice,
    tex2d_surface_GetDesc,
    tex2d_surface_Map,
    tex2d_surface_Unmap,
};

HRESULT d3d11_texture2d_create(struct d3d11_device *device, const D3D11_TEXTURE2D_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *data, struct d3d11_texture2d **out)
{
    struct d3d11_texture2d *texture;
    SIZE_T row_size, total_size;
    UINT bytes;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc || !desc->Width || !desc->Height
            || desc->Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || desc->Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || !desc->SampleDesc.Count)
        return E_INVALIDARG;

    hr = texture_check_basic_layout(desc->MipLevels, desc->ArraySize);
    if (FAILED(hr))
        return hr;

    if (desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality)
        return E_NOTIMPL;

    hr = texture_bytes_per_pixel(desc->Format, &bytes);
    if (FAILED(hr))
        return hr;

    if (!checked_mul_size(desc->Width, bytes, &row_size) || row_size > ~(UINT)0)
        return E_INVALIDARG;
    if (!checked_mul_size(row_size, desc->Height, &total_size))
        return E_INVALIDARG;

    texture = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*texture));
    if (!texture)
        return E_OUTOFMEMORY;

    hr = texture_alloc_sysmem(&texture->sysmem, total_size);
    if (SUCCEEDED(hr) && data && data->pSysMem)
        hr = texture_copy_rows(texture->sysmem, (UINT)row_size, desc->Height, (UINT)row_size,
                data->pSysMem, data->SysMemPitch);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, texture->sysmem);
        HeapFree(GetProcessHeap(), 0, texture);
        return hr;
    }

    texture->ID3D11Texture2D_iface.lpVtbl = &tex2d_vtbl;
    texture->IDXGISurface_iface.lpVtbl = &tex2d_surface_vtbl;
    texture->refcount = 1;
    texture->child.device = device;
    texture->desc = *desc;
    texture->sysmem_size = total_size;
    texture->row_pitch = (UINT)row_size;
    ID3D11Device_AddRef(&device->ID3D11Device_iface);

    *out = texture;
    return S_OK;
}

/* ========================================================================= */
/* ID3D11Texture3D                                                           */
/* ========================================================================= */

static HRESULT STDMETHODCALLTYPE tex3d_QueryInterface(ID3D11Texture3D *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11Resource) || IsEqualGUID(riid, &IID_ID3D11Texture3D))
    {
        *out = iface;
        ID3D11Texture3D_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE tex3d_AddRef(ID3D11Texture3D *iface)
{
    return InterlockedIncrement(&impl_from_ID3D11Texture3D(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE tex3d_Release(ID3D11Texture3D *iface)
{
    struct d3d11_texture3d *texture = impl_from_ID3D11Texture3D(iface);
    ULONG refcount = InterlockedDecrement(&texture->refcount);

    if (!refcount)
    {
        struct d3d11_device *device = texture->child.device;

        HeapFree(GetProcessHeap(), 0, texture->sysmem);
        HeapFree(GetProcessHeap(), 0, texture);
        texture_release_device(device);
    }

    return refcount;
}

static void STDMETHODCALLTYPE tex3d_GetDevice(ID3D11Texture3D *iface, ID3D11Device **device)
{
    d3d11_device_child_get_device(impl_from_ID3D11Texture3D(iface)->child.device, device);
}

static HRESULT STDMETHODCALLTYPE tex3d_GetPrivateData(ID3D11Texture3D *iface, REFGUID guid,
        UINT *data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex3d_SetPrivateData(ID3D11Texture3D *iface, REFGUID guid,
        UINT data_size, const void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE tex3d_SetPrivateDataInterface(ID3D11Texture3D *iface,
        REFGUID guid, const IUnknown *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE tex3d_GetType(ID3D11Texture3D *iface, D3D11_RESOURCE_DIMENSION *dimension)
{
    if (dimension)
        *dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
}

static void STDMETHODCALLTYPE tex3d_SetEvictionPriority(ID3D11Texture3D *iface, UINT priority)
{
    impl_from_ID3D11Texture3D(iface)->eviction_priority = priority;
}

static UINT STDMETHODCALLTYPE tex3d_GetEvictionPriority(ID3D11Texture3D *iface)
{
    return impl_from_ID3D11Texture3D(iface)->eviction_priority;
}

static void STDMETHODCALLTYPE tex3d_GetDesc(ID3D11Texture3D *iface, D3D11_TEXTURE3D_DESC *desc)
{
    if (desc)
        *desc = impl_from_ID3D11Texture3D(iface)->desc;
}

static const struct ID3D11Texture3DVtbl tex3d_vtbl =
{
    tex3d_QueryInterface,
    tex3d_AddRef,
    tex3d_Release,
    tex3d_GetDevice,
    tex3d_GetPrivateData,
    tex3d_SetPrivateData,
    tex3d_SetPrivateDataInterface,
    tex3d_GetType,
    tex3d_SetEvictionPriority,
    tex3d_GetEvictionPriority,
    tex3d_GetDesc,
};

HRESULT d3d11_texture3d_create(struct d3d11_device *device, const D3D11_TEXTURE3D_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *data, struct d3d11_texture3d **out)
{
    struct d3d11_texture3d *texture;
    SIZE_T row_size, slice_size, total_size;
    UINT bytes;
    HRESULT hr;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !desc || !desc->Width || !desc->Height || !desc->Depth
            || desc->Width > D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
            || desc->Height > D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
            || desc->Depth > D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION)
        return E_INVALIDARG;

    hr = texture_check_basic_layout(desc->MipLevels, 1);
    if (FAILED(hr))
        return hr;

    hr = texture_bytes_per_pixel(desc->Format, &bytes);
    if (FAILED(hr))
        return hr;

    if (!checked_mul_size(desc->Width, bytes, &row_size) || row_size > ~(UINT)0)
        return E_INVALIDARG;
    if (!checked_mul_size(row_size, desc->Height, &slice_size) || slice_size > ~(UINT)0)
        return E_INVALIDARG;
    if (!checked_mul_size(slice_size, desc->Depth, &total_size))
        return E_INVALIDARG;

    texture = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*texture));
    if (!texture)
        return E_OUTOFMEMORY;

    hr = texture_alloc_sysmem(&texture->sysmem, total_size);
    texture->desc = *desc;
    texture->sysmem_size = total_size;
    texture->row_pitch = (UINT)row_size;
    texture->slice_pitch = (UINT)slice_size;
    if (SUCCEEDED(hr) && data && data->pSysMem)
        hr = texture3d_update(texture, NULL, data->pSysMem,
                data->SysMemPitch, data->SysMemSlicePitch);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, texture->sysmem);
        HeapFree(GetProcessHeap(), 0, texture);
        return hr;
    }

    texture->ID3D11Texture3D_iface.lpVtbl = &tex3d_vtbl;
    texture->refcount = 1;
    texture->child.device = device;
    ID3D11Device_AddRef(&device->ID3D11Device_iface);

    *out = texture;
    return S_OK;
}

HRESULT d3d11_texture_resource_map(ID3D11Resource *resource, UINT subresource,
        D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    ID3D11Texture1D *texture1d;
    ID3D11Texture2D *texture2d;
    ID3D11Texture3D *texture3d;
    HRESULT hr;

    if (!mapped)
        return E_INVALIDARG;
    mapped->pData = NULL;
    mapped->RowPitch = 0;
    mapped->DepthPitch = 0;

    if (!resource)
        return E_INVALIDARG;
    if (subresource)
        return E_INVALIDARG;

    hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture1D, (void **)&texture1d);
    if (SUCCEEDED(hr))
    {
        struct d3d11_texture1d *texture = impl_from_ID3D11Texture1D(texture1d);

        hr = texture_check_map(texture->desc.Usage, texture->desc.CPUAccessFlags, type, flags);
        if (SUCCEEDED(hr))
        {
            mapped->pData = texture->sysmem;
            mapped->RowPitch = texture->row_pitch;
            mapped->DepthPitch = texture->row_pitch;
        }
        ID3D11Texture1D_Release(texture1d);
        return hr;
    }

    hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture2D, (void **)&texture2d);
    if (SUCCEEDED(hr))
    {
        struct d3d11_texture2d *texture = impl_from_ID3D11Texture2D(texture2d);

        hr = texture_check_map(texture->desc.Usage, texture->desc.CPUAccessFlags, type, flags);
        if (SUCCEEDED(hr))
        {
            mapped->pData = texture->sysmem;
            mapped->RowPitch = texture->row_pitch;
            mapped->DepthPitch = texture->row_pitch * texture->desc.Height;
        }
        ID3D11Texture2D_Release(texture2d);
        return hr;
    }

    hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture3D, (void **)&texture3d);
    if (SUCCEEDED(hr))
    {
        struct d3d11_texture3d *texture = impl_from_ID3D11Texture3D(texture3d);

        hr = texture_check_map(texture->desc.Usage, texture->desc.CPUAccessFlags, type, flags);
        if (SUCCEEDED(hr))
        {
            mapped->pData = texture->sysmem;
            mapped->RowPitch = texture->row_pitch;
            mapped->DepthPitch = texture->slice_pitch;
        }
        ID3D11Texture3D_Release(texture3d);
        return hr;
    }

    return E_NOTIMPL;
}

void d3d11_texture_resource_unmap(ID3D11Resource *resource, UINT subresource)
{
    TRACE("resource %p, subresource %u.\n", resource, subresource);
}

HRESULT d3d11_texture_resource_update(ID3D11Resource *resource, UINT subresource,
        const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch)
{
    ID3D11Texture1D *texture1d;
    ID3D11Texture2D *texture2d;
    ID3D11Texture3D *texture3d;
    HRESULT hr;

    if (!resource || !data)
        return E_INVALIDARG;
    if (subresource)
        return E_INVALIDARG;

    hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture1D, (void **)&texture1d);
    if (SUCCEEDED(hr))
    {
        hr = texture1d_update(impl_from_ID3D11Texture1D(texture1d), box, data, row_pitch);
        ID3D11Texture1D_Release(texture1d);
        return hr;
    }

    hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture2D, (void **)&texture2d);
    if (SUCCEEDED(hr))
    {
        hr = texture2d_update(impl_from_ID3D11Texture2D(texture2d), box, data, row_pitch);
        ID3D11Texture2D_Release(texture2d);
        return hr;
    }

    hr = ID3D11Resource_QueryInterface(resource, &IID_ID3D11Texture3D, (void **)&texture3d);
    if (SUCCEEDED(hr))
    {
        hr = texture3d_update(impl_from_ID3D11Texture3D(texture3d), box,
                data, row_pitch, depth_pitch);
        ID3D11Texture3D_Release(texture3d);
        return hr;
    }

    return E_NOTIMPL;
}
