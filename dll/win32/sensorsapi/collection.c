/*
 * Windows Sensor API collection
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

struct sensor_collection
{
    ISensorCollection ISensorCollection_iface;
    LONG refs;
    CRITICAL_SECTION critical_section;
    ISensor **items;
    ULONG count;
    ULONG capacity;
};

static inline struct sensor_collection *collection_from_ISensorCollection(ISensorCollection *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_collection, ISensorCollection_iface);
}

static HRESULT WINAPI sensor_collection_QueryInterface(ISensorCollection *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ISensorCollection))
        return E_NOINTERFACE;
    *out = iface;
    ISensorCollection_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI sensor_collection_AddRef(ISensorCollection *iface)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);

    return InterlockedIncrement(&collection->refs);
}

static ULONG WINAPI sensor_collection_Release(ISensorCollection *iface)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);
    ISensor **items;
    ULONG count;
    ULONG index;
    ULONG refs;

    refs = InterlockedDecrement(&collection->refs);
    if (!refs)
    {
        EnterCriticalSection(&collection->critical_section);
        items = collection->items;
        count = collection->count;
        collection->items = NULL;
        collection->count = 0;
        collection->capacity = 0;
        LeaveCriticalSection(&collection->critical_section);
        for (index = 0; index < count; index++)
            ISensor_Release(items[index]);
        HeapFree(GetProcessHeap(), 0, items);
        DeleteCriticalSection(&collection->critical_section);
        HeapFree(GetProcessHeap(), 0, collection);
    }
    return refs;
}

static HRESULT WINAPI sensor_collection_GetAt(ISensorCollection *iface, ULONG index, ISensor **out)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);

    if (!out)
        return E_POINTER;
    *out = NULL;
    EnterCriticalSection(&collection->critical_section);
    if (index >= collection->count)
    {
        LeaveCriticalSection(&collection->critical_section);
        return E_INVALIDARG;
    }
    *out = collection->items[index];
    ISensor_AddRef(*out);
    LeaveCriticalSection(&collection->critical_section);
    return S_OK;
}

static HRESULT WINAPI sensor_collection_GetCount(ISensorCollection *iface, ULONG *count)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);

    if (!count)
        return E_POINTER;
    EnterCriticalSection(&collection->critical_section);
    *count = collection->count;
    LeaveCriticalSection(&collection->critical_section);
    return S_OK;
}

static HRESULT WINAPI sensor_collection_Add(ISensorCollection *iface, ISensor *sensor)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);
    ISensor **items;
    ULONG capacity;
    ULONG index;

    if (!sensor)
        return E_INVALIDARG;
    EnterCriticalSection(&collection->critical_section);
    for (index = 0; index < collection->count; index++)
    {
        if (collection->items[index] == sensor)
        {
            LeaveCriticalSection(&collection->critical_section);
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        }
    }
    if (collection->count == collection->capacity)
    {
        if (collection->capacity > (~(ULONG)0) / 2)
        {
            LeaveCriticalSection(&collection->critical_section);
            return E_OUTOFMEMORY;
        }
        capacity = collection->capacity ? collection->capacity * 2 : 4;
        items = sensor_heap_realloc(collection->items, capacity * sizeof(*items));
        if (!items)
        {
            LeaveCriticalSection(&collection->critical_section);
            return E_OUTOFMEMORY;
        }
        collection->items = items;
        collection->capacity = capacity;
    }
    collection->items[collection->count++] = sensor;
    ISensor_AddRef(sensor);
    LeaveCriticalSection(&collection->critical_section);
    return S_OK;
}

static HRESULT WINAPI sensor_collection_Remove(ISensorCollection *iface, ISensor *sensor)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);
    ULONG index;

    if (!sensor)
        return E_INVALIDARG;
    EnterCriticalSection(&collection->critical_section);
    for (index = 0; index < collection->count; index++)
    {
        if (collection->items[index] != sensor)
            continue;
        memmove(&collection->items[index], &collection->items[index + 1], (collection->count - index - 1) * sizeof(*collection->items));
        collection->count--;
        LeaveCriticalSection(&collection->critical_section);
        ISensor_Release(sensor);
        return S_OK;
    }
    LeaveCriticalSection(&collection->critical_section);
    return E_INVALIDARG;
}

static HRESULT WINAPI sensor_collection_RemoveByID(ISensorCollection *iface, REFSENSOR_ID sensor_id)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);
    SENSOR_ID current_id;
    ISensor *sensor;
    HRESULT hr;
    ULONG index;

    if (!sensor_id)
        return E_INVALIDARG;
    EnterCriticalSection(&collection->critical_section);
    for (index = 0; index < collection->count; index++)
    {
        sensor = collection->items[index];
        hr = ISensor_GetID(sensor, &current_id);
        if (FAILED(hr) || !IsEqualGUID(&current_id, sensor_id))
            continue;
        memmove(&collection->items[index], &collection->items[index + 1], (collection->count - index - 1) * sizeof(*collection->items));
        collection->count--;
        LeaveCriticalSection(&collection->critical_section);
        ISensor_Release(sensor);
        return S_OK;
    }
    LeaveCriticalSection(&collection->critical_section);
    return E_INVALIDARG;
}

static HRESULT WINAPI sensor_collection_Clear(ISensorCollection *iface)
{
    struct sensor_collection *collection = collection_from_ISensorCollection(iface);
    ISensor **items;
    ULONG count;
    ULONG index;

    EnterCriticalSection(&collection->critical_section);
    items = collection->items;
    count = collection->count;
    collection->items = NULL;
    collection->count = 0;
    collection->capacity = 0;
    LeaveCriticalSection(&collection->critical_section);
    for (index = 0; index < count; index++)
        ISensor_Release(items[index]);
    HeapFree(GetProcessHeap(), 0, items);
    return S_OK;
}

static const ISensorCollectionVtbl sensor_collection_vtbl =
{
    sensor_collection_QueryInterface,
    sensor_collection_AddRef,
    sensor_collection_Release,
    sensor_collection_GetAt,
    sensor_collection_GetCount,
    sensor_collection_Add,
    sensor_collection_Remove,
    sensor_collection_RemoveByID,
    sensor_collection_Clear,
};

HRESULT sensor_collection_create(REFIID iid, void **out)
{
    struct sensor_collection *collection;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    collection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*collection));
    if (!collection)
        return E_OUTOFMEMORY;
    collection->ISensorCollection_iface.lpVtbl = &sensor_collection_vtbl;
    collection->refs = 1;
    InitializeCriticalSection(&collection->critical_section);
    hr = ISensorCollection_QueryInterface(&collection->ISensorCollection_iface, iid, out);
    ISensorCollection_Release(&collection->ISensorCollection_iface);
    return hr;
}
