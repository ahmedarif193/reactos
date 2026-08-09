/*
 * Portable Device collection implementations
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(portabledev);

struct key_collection
{
    IPortableDeviceKeyCollection IPortableDeviceKeyCollection_iface;
    LONG refcount;
    CRITICAL_SECTION cs;
    PROPERTYKEY *items;
    DWORD count;
    DWORD capacity;
};

struct propvariant_collection
{
    IPortableDevicePropVariantCollection IPortableDevicePropVariantCollection_iface;
    LONG refcount;
    CRITICAL_SECTION cs;
    PROPVARIANT *items;
    DWORD count;
    DWORD capacity;
};

struct values_collection
{
    IPortableDeviceValuesCollection IPortableDeviceValuesCollection_iface;
    LONG refcount;
    CRITICAL_SECTION cs;
    IPortableDeviceValues **items;
    DWORD count;
    DWORD capacity;
};

static HRESULT collection_reserve(void **items, DWORD *capacity, DWORD count, SIZE_T element_size)
{
    DWORD new_capacity;
    void *new_items;

    if (count <= *capacity)
        return S_OK;
    if (count > ~(DWORD)0 / element_size)
        return E_OUTOFMEMORY;

    new_capacity = *capacity ? *capacity * 2 : 8;
    if (new_capacity < count || new_capacity < *capacity)
        new_capacity = count;
    new_items = heap_realloc(*items, (SIZE_T)new_capacity * element_size);
    if (!new_items)
        return E_OUTOFMEMORY;

    *items = new_items;
    *capacity = new_capacity;
    return S_OK;
}

static HRESULT propvariant_collection_copy(PROPVARIANT *destination, const PROPVARIANT *source)
{
    if (source->vt != (VT_VECTOR | VT_UI1) || source->caub.cElems)
        return portable_propvariant_copy(destination, source);
    if (!source->caub.pElems)
        return E_INVALIDARG;

    PropVariantInit(destination);
    return S_OK;
}

static inline struct key_collection *impl_from_IPortableDeviceKeyCollection(IPortableDeviceKeyCollection *iface)
{
    return CONTAINING_RECORD(iface, struct key_collection, IPortableDeviceKeyCollection_iface);
}

static HRESULT WINAPI key_collection_QueryInterface(IPortableDeviceKeyCollection *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IPortableDeviceKeyCollection))
        return E_NOINTERFACE;

    *out = iface;
    IPortableDeviceKeyCollection_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI key_collection_AddRef(IPortableDeviceKeyCollection *iface)
{
    struct key_collection *collection = impl_from_IPortableDeviceKeyCollection(iface);
    return InterlockedIncrement(&collection->refcount);
}

static HRESULT WINAPI key_collection_Clear(IPortableDeviceKeyCollection *iface);

static ULONG WINAPI key_collection_Release(IPortableDeviceKeyCollection *iface)
{
    struct key_collection *collection = impl_from_IPortableDeviceKeyCollection(iface);
    ULONG refcount = InterlockedDecrement(&collection->refcount);

    if (!refcount)
    {
        key_collection_Clear(iface);
        DeleteCriticalSection(&collection->cs);
        HeapFree(GetProcessHeap(), 0, collection);
    }
    return refcount;
}

static HRESULT WINAPI key_collection_GetCount(IPortableDeviceKeyCollection *iface, DWORD *count)
{
    struct key_collection *collection = impl_from_IPortableDeviceKeyCollection(iface);

    if (!count)
        return E_POINTER;
    EnterCriticalSection(&collection->cs);
    *count = collection->count;
    LeaveCriticalSection(&collection->cs);
    return S_OK;
}

static HRESULT WINAPI key_collection_GetAt(IPortableDeviceKeyCollection *iface, DWORD index, PROPERTYKEY *key)
{
    struct key_collection *collection = impl_from_IPortableDeviceKeyCollection(iface);
    HRESULT hr = S_OK;

    if (!key)
        return E_POINTER;
    EnterCriticalSection(&collection->cs);
    if (index >= collection->count)
        hr = E_INVALIDARG;
    else
        *key = collection->items[index];
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static HRESULT WINAPI key_collection_Add(IPortableDeviceKeyCollection *iface, REFPROPERTYKEY key)
{
    struct key_collection *collection = impl_from_IPortableDeviceKeyCollection(iface);
    HRESULT hr;

    if (!key)
        return E_POINTER;
    EnterCriticalSection(&collection->cs);
    hr = collection_reserve((void **)&collection->items, &collection->capacity, collection->count + 1, sizeof(*collection->items));
    if (SUCCEEDED(hr))
        collection->items[collection->count++] = *key;
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static HRESULT WINAPI key_collection_Clear(IPortableDeviceKeyCollection *iface)
{
    struct key_collection *collection = impl_from_IPortableDeviceKeyCollection(iface);

    EnterCriticalSection(&collection->cs);
    HeapFree(GetProcessHeap(), 0, collection->items);
    collection->items = NULL;
    collection->count = 0;
    collection->capacity = 0;
    LeaveCriticalSection(&collection->cs);
    return S_OK;
}

static HRESULT WINAPI key_collection_RemoveAt(IPortableDeviceKeyCollection *iface, DWORD index)
{
    struct key_collection *collection = impl_from_IPortableDeviceKeyCollection(iface);
    HRESULT hr = S_OK;

    EnterCriticalSection(&collection->cs);
    if (index >= collection->count)
        hr = E_INVALIDARG;
    else
    {
        if (index + 1 < collection->count)
            memmove(&collection->items[index], &collection->items[index + 1], (collection->count - index - 1) * sizeof(*collection->items));
        --collection->count;
        memset(&collection->items[collection->count], 0, sizeof(*collection->items));
    }
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static const IPortableDeviceKeyCollectionVtbl key_collection_vtbl =
{
    key_collection_QueryInterface,
    key_collection_AddRef,
    key_collection_Release,
    key_collection_GetCount,
    key_collection_GetAt,
    key_collection_Add,
    key_collection_Clear,
    key_collection_RemoveAt,
};

HRESULT portable_device_key_collection_create(REFIID iid, void **out)
{
    struct key_collection *collection;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    collection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*collection));
    if (!collection)
        return E_OUTOFMEMORY;

    collection->IPortableDeviceKeyCollection_iface.lpVtbl = &key_collection_vtbl;
    collection->refcount = 1;
    InitializeCriticalSection(&collection->cs);
    hr = IPortableDeviceKeyCollection_QueryInterface(&collection->IPortableDeviceKeyCollection_iface, iid, out);
    IPortableDeviceKeyCollection_Release(&collection->IPortableDeviceKeyCollection_iface);
    return hr;
}

static inline struct propvariant_collection *impl_from_IPortableDevicePropVariantCollection(IPortableDevicePropVariantCollection *iface)
{
    return CONTAINING_RECORD(iface, struct propvariant_collection, IPortableDevicePropVariantCollection_iface);
}

static HRESULT WINAPI propvariant_collection_QueryInterface(IPortableDevicePropVariantCollection *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IPortableDevicePropVariantCollection))
        return E_NOINTERFACE;

    *out = iface;
    IPortableDevicePropVariantCollection_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI propvariant_collection_AddRef(IPortableDevicePropVariantCollection *iface)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    return InterlockedIncrement(&collection->refcount);
}

static HRESULT WINAPI propvariant_collection_Clear(IPortableDevicePropVariantCollection *iface);

static ULONG WINAPI propvariant_collection_Release(IPortableDevicePropVariantCollection *iface)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    ULONG refcount = InterlockedDecrement(&collection->refcount);

    if (!refcount)
    {
        propvariant_collection_Clear(iface);
        DeleteCriticalSection(&collection->cs);
        HeapFree(GetProcessHeap(), 0, collection);
    }
    return refcount;
}

static HRESULT WINAPI propvariant_collection_GetCount(IPortableDevicePropVariantCollection *iface, DWORD *count)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);

    if (!count)
        return E_POINTER;
    EnterCriticalSection(&collection->cs);
    *count = collection->count;
    LeaveCriticalSection(&collection->cs);
    return S_OK;
}

static HRESULT WINAPI propvariant_collection_GetAt(IPortableDevicePropVariantCollection *iface, DWORD index, PROPVARIANT *value)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    HRESULT hr;

    if (!value)
        return E_POINTER;
    PropVariantInit(value);
    EnterCriticalSection(&collection->cs);
    if (index >= collection->count)
        hr = E_INVALIDARG;
    else
        hr = portable_propvariant_copy(value, &collection->items[index]);
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static HRESULT WINAPI propvariant_collection_Add(IPortableDevicePropVariantCollection *iface, const PROPVARIANT *value)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    PROPVARIANT copy;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    PropVariantInit(&copy);

    EnterCriticalSection(&collection->cs);
    if (!collection->count || value->vt == collection->items[0].vt)
        hr = propvariant_collection_copy(&copy, value);
    else
        hr = portable_propvariant_change_type(&copy, value, collection->items[0].vt);

    if (SUCCEEDED(hr))
        hr = collection_reserve((void **)&collection->items, &collection->capacity, collection->count + 1, sizeof(*collection->items));
    if (SUCCEEDED(hr))
        collection->items[collection->count++] = copy;
    LeaveCriticalSection(&collection->cs);

    if (FAILED(hr))
        PropVariantClear(&copy);
    return hr;
}

static HRESULT WINAPI propvariant_collection_GetType(IPortableDevicePropVariantCollection *iface, VARTYPE *type)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    HRESULT hr;

    if (!type)
        return E_POINTER;
    EnterCriticalSection(&collection->cs);
    if (!collection->count)
        hr = E_UNEXPECTED;
    else
    {
        *type = collection->items[0].vt;
        hr = S_OK;
    }
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static HRESULT WINAPI propvariant_collection_ChangeType(IPortableDevicePropVariantCollection *iface, VARTYPE type)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    HRESULT hr = S_OK;
    DWORD i;

    EnterCriticalSection(&collection->cs);
    if (!collection->count)
    {
        LeaveCriticalSection(&collection->cs);
        return S_OK;
    }
    if (type != collection->items[0].vt)
    {
        for (i = 0; i < collection->count; ++i)
        {
            PROPVARIANT converted;

            PropVariantInit(&converted);
            hr = portable_propvariant_change_type(&converted, &collection->items[i], type);
            PropVariantClear(&collection->items[i]);
            if (FAILED(hr))
            {
                PropVariantClear(&converted);
                break;
            }
            collection->items[i] = converted;
        }
    }
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static HRESULT WINAPI propvariant_collection_Clear(IPortableDevicePropVariantCollection *iface)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    DWORD i;

    EnterCriticalSection(&collection->cs);
    for (i = 0; i < collection->count; ++i)
        PropVariantClear(&collection->items[i]);
    HeapFree(GetProcessHeap(), 0, collection->items);
    collection->items = NULL;
    collection->count = 0;
    collection->capacity = 0;
    LeaveCriticalSection(&collection->cs);
    return S_OK;
}

static HRESULT WINAPI propvariant_collection_RemoveAt(IPortableDevicePropVariantCollection *iface, DWORD index)
{
    struct propvariant_collection *collection = impl_from_IPortableDevicePropVariantCollection(iface);
    HRESULT hr = S_OK;

    EnterCriticalSection(&collection->cs);
    if (index >= collection->count)
        hr = E_INVALIDARG;
    else
    {
        PropVariantClear(&collection->items[index]);
        if (index + 1 < collection->count)
            memmove(&collection->items[index], &collection->items[index + 1], (collection->count - index - 1) * sizeof(*collection->items));
        --collection->count;
        PropVariantInit(&collection->items[collection->count]);
    }
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static const IPortableDevicePropVariantCollectionVtbl propvariant_collection_vtbl =
{
    propvariant_collection_QueryInterface,
    propvariant_collection_AddRef,
    propvariant_collection_Release,
    propvariant_collection_GetCount,
    propvariant_collection_GetAt,
    propvariant_collection_Add,
    propvariant_collection_GetType,
    propvariant_collection_ChangeType,
    propvariant_collection_Clear,
    propvariant_collection_RemoveAt,
};

HRESULT portable_device_propvariant_collection_create(REFIID iid, void **out)
{
    struct propvariant_collection *collection;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    collection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*collection));
    if (!collection)
        return E_OUTOFMEMORY;

    collection->IPortableDevicePropVariantCollection_iface.lpVtbl = &propvariant_collection_vtbl;
    collection->refcount = 1;
    InitializeCriticalSection(&collection->cs);
    hr = IPortableDevicePropVariantCollection_QueryInterface(&collection->IPortableDevicePropVariantCollection_iface, iid, out);
    IPortableDevicePropVariantCollection_Release(&collection->IPortableDevicePropVariantCollection_iface);
    return hr;
}

static inline struct values_collection *impl_from_IPortableDeviceValuesCollection(IPortableDeviceValuesCollection *iface)
{
    return CONTAINING_RECORD(iface, struct values_collection, IPortableDeviceValuesCollection_iface);
}

static HRESULT WINAPI values_collection_QueryInterface(IPortableDeviceValuesCollection *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IPortableDeviceValuesCollection))
        return E_NOINTERFACE;

    *out = iface;
    IPortableDeviceValuesCollection_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI values_collection_AddRef(IPortableDeviceValuesCollection *iface)
{
    struct values_collection *collection = impl_from_IPortableDeviceValuesCollection(iface);
    return InterlockedIncrement(&collection->refcount);
}

static HRESULT WINAPI values_collection_Clear(IPortableDeviceValuesCollection *iface);

static ULONG WINAPI values_collection_Release(IPortableDeviceValuesCollection *iface)
{
    struct values_collection *collection = impl_from_IPortableDeviceValuesCollection(iface);
    ULONG refcount = InterlockedDecrement(&collection->refcount);

    if (!refcount)
    {
        values_collection_Clear(iface);
        DeleteCriticalSection(&collection->cs);
        HeapFree(GetProcessHeap(), 0, collection);
    }
    return refcount;
}

static HRESULT WINAPI values_collection_GetCount(IPortableDeviceValuesCollection *iface, DWORD *count)
{
    struct values_collection *collection = impl_from_IPortableDeviceValuesCollection(iface);

    if (!count)
        return E_POINTER;
    EnterCriticalSection(&collection->cs);
    *count = collection->count;
    LeaveCriticalSection(&collection->cs);
    return S_OK;
}

static HRESULT WINAPI values_collection_GetAt(IPortableDeviceValuesCollection *iface, DWORD index, IPortableDeviceValues **value)
{
    struct values_collection *collection = impl_from_IPortableDeviceValuesCollection(iface);
    HRESULT hr = S_OK;

    if (!value)
        return E_POINTER;
    *value = NULL;
    EnterCriticalSection(&collection->cs);
    if (index >= collection->count)
        hr = E_INVALIDARG;
    else
    {
        *value = collection->items[index];
        IPortableDeviceValues_AddRef(*value);
    }
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static HRESULT WINAPI values_collection_Add(IPortableDeviceValuesCollection *iface, IPortableDeviceValues *value)
{
    struct values_collection *collection = impl_from_IPortableDeviceValuesCollection(iface);
    HRESULT hr;

    if (!value)
        return E_POINTER;
    EnterCriticalSection(&collection->cs);
    hr = collection_reserve((void **)&collection->items, &collection->capacity, collection->count + 1, sizeof(*collection->items));
    if (SUCCEEDED(hr))
    {
        IPortableDeviceValues_AddRef(value);
        collection->items[collection->count++] = value;
    }
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static HRESULT WINAPI values_collection_Clear(IPortableDeviceValuesCollection *iface)
{
    struct values_collection *collection = impl_from_IPortableDeviceValuesCollection(iface);
    DWORD i;

    EnterCriticalSection(&collection->cs);
    for (i = 0; i < collection->count; ++i)
        IPortableDeviceValues_Release(collection->items[i]);
    HeapFree(GetProcessHeap(), 0, collection->items);
    collection->items = NULL;
    collection->count = 0;
    collection->capacity = 0;
    LeaveCriticalSection(&collection->cs);
    return S_OK;
}

static HRESULT WINAPI values_collection_RemoveAt(IPortableDeviceValuesCollection *iface, DWORD index)
{
    struct values_collection *collection = impl_from_IPortableDeviceValuesCollection(iface);
    HRESULT hr = S_OK;

    EnterCriticalSection(&collection->cs);
    if (index >= collection->count)
        hr = E_INVALIDARG;
    else
    {
        IPortableDeviceValues_Release(collection->items[index]);
        if (index + 1 < collection->count)
            memmove(&collection->items[index], &collection->items[index + 1], (collection->count - index - 1) * sizeof(*collection->items));
        --collection->count;
        collection->items[collection->count] = NULL;
    }
    LeaveCriticalSection(&collection->cs);
    return hr;
}

static const IPortableDeviceValuesCollectionVtbl values_collection_vtbl =
{
    values_collection_QueryInterface,
    values_collection_AddRef,
    values_collection_Release,
    values_collection_GetCount,
    values_collection_GetAt,
    values_collection_Add,
    values_collection_Clear,
    values_collection_RemoveAt,
};

HRESULT portable_device_values_collection_create(REFIID iid, void **out)
{
    struct values_collection *collection;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    collection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*collection));
    if (!collection)
        return E_OUTOFMEMORY;

    collection->IPortableDeviceValuesCollection_iface.lpVtbl = &values_collection_vtbl;
    collection->refcount = 1;
    InitializeCriticalSection(&collection->cs);
    hr = IPortableDeviceValuesCollection_QueryInterface(&collection->IPortableDeviceValuesCollection_iface, iid, out);
    IPortableDeviceValuesCollection_Release(&collection->IPortableDeviceValuesCollection_iface);
    return hr;
}
