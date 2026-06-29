/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ID3D11InputLayout implementation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

static HRESULT STDMETHODCALLTYPE il_QueryInterface(ID3D11InputLayout *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D11InputLayout))
    {
        *out = iface;
        ID3D11InputLayout_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE il_AddRef(ID3D11InputLayout *iface)
{ return InterlockedIncrement(&impl_from_ID3D11InputLayout(iface)->refcount); }

static ULONG STDMETHODCALLTYPE il_Release(ID3D11InputLayout *iface)
{
    struct d3d11_input_layout *il = impl_from_ID3D11InputLayout(iface);
    ULONG r = InterlockedDecrement(&il->refcount);
    if (!r)
    {
        UINT i;

        for (i = 0; i < il->element_count; ++i)
            HeapFree(GetProcessHeap(), 0, (void *)il->elements[i].SemanticName);
        HeapFree(GetProcessHeap(), 0, il->elements);
        d3d11_device_child_cleanup(&il->child);
        HeapFree(GetProcessHeap(), 0, il);
    }
    return r;
}

static void STDMETHODCALLTYPE il_GetDevice(ID3D11InputLayout *iface, ID3D11Device **d)
{
    d3d11_device_child_get_device(impl_from_ID3D11InputLayout(iface)->child.device, d);
}

static HRESULT STDMETHODCALLTYPE il_GetPrivateData(ID3D11InputLayout *iface, REFGUID g, UINT *sz, void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE il_SetPrivateData(ID3D11InputLayout *iface, REFGUID g, UINT sz, const void *d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE il_SetPrivateDataInterface(ID3D11InputLayout *iface, REFGUID g, const IUnknown *d) { return E_NOTIMPL; }

static const struct ID3D11InputLayoutVtbl il_vtbl = {
    il_QueryInterface, il_AddRef, il_Release,
    il_GetDevice, il_GetPrivateData, il_SetPrivateData, il_SetPrivateDataInterface,
};

static BOOL input_layout_desc_is_valid(const D3D11_INPUT_ELEMENT_DESC *desc)
{
    if (!desc->SemanticName || desc->Format == DXGI_FORMAT_UNKNOWN)
        return FALSE;
    if (desc->InputSlot >= D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT_)
        return FALSE;
    if (desc->InputSlotClass != D3D11_INPUT_PER_VERTEX_DATA
            && desc->InputSlotClass != D3D11_INPUT_PER_INSTANCE_DATA)
        return FALSE;
    if (desc->InputSlotClass == D3D11_INPUT_PER_VERTEX_DATA && desc->InstanceDataStepRate)
        return FALSE;
    if (desc->InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA && !desc->InstanceDataStepRate)
        return FALSE;

    return TRUE;
}

static char *input_layout_strdup(const char *src)
{
    char *dst;
    SIZE_T len;

    len = strlen(src) + 1;
    dst = HeapAlloc(GetProcessHeap(), 0, len);
    if (dst)
        memcpy(dst, src, len);
    return dst;
}

HRESULT d3d11_input_layout_create(struct d3d11_device *device,
        const D3D11_INPUT_ELEMENT_DESC *descs, UINT count,
        const void *bytecode, SIZE_T bytecode_length,
        struct d3d11_input_layout **out)
{
    struct d3d11_input_layout *il;
    SIZE_T elements_size;
    HRESULT hr;
    UINT i;

    if (!out)
        return E_INVALIDARG;
    *out = NULL;

    if (!device || !descs || !count || count > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT
            || !bytecode || !bytecode_length)
        return E_INVALIDARG;
    if (!d3d11_checked_mul_size(count, sizeof(*il->elements), &elements_size))
        return E_INVALIDARG;
    for (i = 0; i < count; ++i)
    {
        if (!input_layout_desc_is_valid(&descs[i]))
            return E_INVALIDARG;
    }

    il = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*il));
    if (!il) return E_OUTOFMEMORY;

    il->elements = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, elements_size);
    if (!il->elements)
    {
        HeapFree(GetProcessHeap(), 0, il);
        return E_OUTOFMEMORY;
    }

    il->ID3D11InputLayout_iface.lpVtbl = &il_vtbl;
    il->refcount = 1;
    il->element_count = count;
    for (i = 0; i < count; ++i)
    {
        il->elements[i] = descs[i];
        il->elements[i].SemanticName = input_layout_strdup(descs[i].SemanticName);
        if (!il->elements[i].SemanticName)
        {
            ID3D11InputLayout_Release(&il->ID3D11InputLayout_iface);
            return E_OUTOFMEMORY;
        }
    }

    hr = d3d11_device_child_init(&il->child, device);
    if (FAILED(hr))
    {
        ID3D11InputLayout_Release(&il->ID3D11InputLayout_iface);
        return hr;
    }

    *out = il;
    return S_OK;
}
