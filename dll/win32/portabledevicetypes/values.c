/*
 * IPortableDeviceValues implementation
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(portabledev);

struct value_entry
{
    PROPERTYKEY key;
    PROPVARIANT value;
};

struct portable_device_values
{
    IPortableDeviceValues IPortableDeviceValues_iface;
    LONG refcount;
    CRITICAL_SECTION cs;
    struct value_entry *entries;
    DWORD count;
    DWORD capacity;
};

struct property_key_holder
{
    IUnknown IUnknown_iface;
    LONG refcount;
    PROPERTYKEY key;
};

static const IUnknownVtbl property_key_holder_vtbl;

static inline struct portable_device_values *impl_from_IPortableDeviceValues(IPortableDeviceValues *iface)
{
    return CONTAINING_RECORD(iface, struct portable_device_values, IPortableDeviceValues_iface);
}

static inline struct property_key_holder *impl_from_key_holder(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct property_key_holder, IUnknown_iface);
}

static HRESULT WINAPI key_holder_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;

    if (!IsEqualIID(iid, &IID_IUnknown))
        return E_NOINTERFACE;

    *out = iface;
    IUnknown_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI key_holder_AddRef(IUnknown *iface)
{
    struct property_key_holder *holder = impl_from_key_holder(iface);
    return InterlockedIncrement(&holder->refcount);
}

static ULONG WINAPI key_holder_Release(IUnknown *iface)
{
    struct property_key_holder *holder = impl_from_key_holder(iface);
    ULONG refcount = InterlockedDecrement(&holder->refcount);

    if (!refcount)
        HeapFree(GetProcessHeap(), 0, holder);
    return refcount;
}

static const IUnknownVtbl property_key_holder_vtbl =
{
    key_holder_QueryInterface,
    key_holder_AddRef,
    key_holder_Release,
};

static HRESULT values_reserve(struct portable_device_values *values, DWORD capacity)
{
    struct value_entry *entries;
    DWORD new_capacity;

    if (capacity <= values->capacity)
        return S_OK;
    if (capacity > ~(DWORD)0 / sizeof(*entries))
        return E_OUTOFMEMORY;

    new_capacity = values->capacity ? values->capacity * 2 : 8;
    if (new_capacity < capacity || new_capacity < values->capacity)
        new_capacity = capacity;

    entries = heap_realloc(values->entries, (SIZE_T)new_capacity * sizeof(*entries));
    if (!entries)
        return E_OUTOFMEMORY;

    values->entries = entries;
    values->capacity = new_capacity;
    return S_OK;
}

static LONG values_find(struct portable_device_values *values, REFPROPERTYKEY key)
{
    DWORD i;

    for (i = 0; i < values->count; ++i)
    {
        if (property_key_equal(&values->entries[i].key, key))
            return i;
    }
    return -1;
}

static HRESULT WINAPI values_QueryInterface(IPortableDeviceValues *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;

    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IPortableDeviceValues))
        return E_NOINTERFACE;

    *out = iface;
    IPortableDeviceValues_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI values_AddRef(IPortableDeviceValues *iface)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);
    return InterlockedIncrement(&values->refcount);
}

static HRESULT WINAPI values_Clear(IPortableDeviceValues *iface);

static ULONG WINAPI values_Release(IPortableDeviceValues *iface)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);
    ULONG refcount = InterlockedDecrement(&values->refcount);

    if (!refcount)
    {
        values_Clear(iface);
        DeleteCriticalSection(&values->cs);
        HeapFree(GetProcessHeap(), 0, values);
    }
    return refcount;
}

static HRESULT WINAPI values_GetCount(IPortableDeviceValues *iface, DWORD *count)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);

    if (!count)
        return E_POINTER;

    EnterCriticalSection(&values->cs);
    *count = values->count;
    LeaveCriticalSection(&values->cs);
    return S_OK;
}

static HRESULT WINAPI values_GetAt(IPortableDeviceValues *iface, DWORD index, PROPERTYKEY *key, PROPVARIANT *value)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);
    HRESULT hr = S_OK;

    if (!key && !value)
        return E_INVALIDARG;
    if (value)
        PropVariantInit(value);

    EnterCriticalSection(&values->cs);
    if (index >= values->count)
        hr = E_INVALIDARG;
    else
    {
        if (key)
            *key = values->entries[index].key;
        if (value)
            hr = portable_propvariant_copy(value, &values->entries[index].value);
    }
    LeaveCriticalSection(&values->cs);
    return hr;
}

static HRESULT values_set_value(IPortableDeviceValues *iface, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);
    PROPVARIANT copy;
    HRESULT hr;
    LONG index;

    if (!key || !value)
        return E_POINTER;

    PropVariantInit(&copy);
    EnterCriticalSection(&values->cs);
    index = values_find(values, key);
    if (index >= 0)
    {
        PropVariantClear(&values->entries[index].value);
        hr = portable_propvariant_copy(&values->entries[index].value, value);
        if (FAILED(hr))
        {
            PropVariantClear(&values->entries[index].value);
            PropVariantInit(&values->entries[index].value);
        }
    }
    else if (SUCCEEDED(hr = portable_propvariant_copy(&copy, value)) && SUCCEEDED(hr = values_reserve(values, values->count + 1)))
    {
        values->entries[values->count].key = *key;
        values->entries[values->count].value = copy;
        ++values->count;
    }
    LeaveCriticalSection(&values->cs);

    if (FAILED(hr))
        PropVariantClear(&copy);
    return hr;
}

static HRESULT WINAPI values_SetValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    return values_set_value(iface, key, value);
}

static HRESULT WINAPI values_GetValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);
    HRESULT hr;
    LONG index;

    if (!key || !value)
        return E_POINTER;
    PropVariantInit(value);

    EnterCriticalSection(&values->cs);
    index = values_find(values, key);
    if (index < 0)
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    else
        hr = portable_propvariant_copy(value, &values->entries[index].value);
    LeaveCriticalSection(&values->cs);
    return hr;
}

static HRESULT values_get_typed(IPortableDeviceValues *iface, REFPROPERTYKEY key, VARTYPE type, PROPVARIANT *value, BOOL *conversion_attempted)
{
    PROPVARIANT converted;
    HRESULT hr = values_GetValue(iface, key, value);

    if (conversion_attempted)
        *conversion_attempted = FALSE;
    if (SUCCEEDED(hr) && value->vt != type)
    {
        if (conversion_attempted)
            *conversion_attempted = TRUE;
        PropVariantInit(&converted);
        hr = portable_propvariant_change_type(&converted, value, type);
        PropVariantClear(value);
        if (SUCCEEDED(hr))
            *value = converted;
        else
            PropVariantClear(&converted);
    }
    return hr;
}

static HRESULT WINAPI values_SetStringValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, LPCWSTR value)
{
    PROPVARIANT variant;

    if (!value)
        return E_POINTER;
    PropVariantInit(&variant);
    variant.vt = VT_LPWSTR;
    variant.pwszVal = (WCHAR *)value;
    return values_SetValue(iface, key, &variant);
}

static HRESULT WINAPI values_GetStringValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, LPWSTR *value)
{
    PROPVARIANT variant;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    *value = NULL;
    hr = values_get_typed(iface, key, VT_LPWSTR, &variant, NULL);
    if (SUCCEEDED(hr))
    {
        *value = variant.pwszVal;
        variant.vt = VT_EMPTY;
    }
    PropVariantClear(&variant);
    return hr;
}

#define DEFINE_SCALAR_ACCESSORS(name, ctype, vartype, member) \
static HRESULT WINAPI values_Set##name(IPortableDeviceValues *iface, REFPROPERTYKEY key, ctype value) \
{ \
    PROPVARIANT variant; \
    PropVariantInit(&variant); \
    variant.vt = vartype; \
    variant.member = value; \
    return values_SetValue(iface, key, &variant); \
} \
static HRESULT WINAPI values_Get##name(IPortableDeviceValues *iface, REFPROPERTYKEY key, ctype *value) \
{ \
    PROPVARIANT variant; \
    BOOL conversion_attempted; \
    HRESULT hr; \
    if (!value) \
        return E_POINTER; \
    hr = values_get_typed(iface, key, vartype, &variant, &conversion_attempted); \
    if (SUCCEEDED(hr)) \
        *value = variant.member; \
    else if (conversion_attempted) \
        *value = 0; \
    PropVariantClear(&variant); \
    return hr; \
}

DEFINE_SCALAR_ACCESSORS(UnsignedIntegerValue, ULONG, VT_UI4, ulVal)
DEFINE_SCALAR_ACCESSORS(SignedIntegerValue, LONG, VT_I4, lVal)
DEFINE_SCALAR_ACCESSORS(UnsignedLargeIntegerValue, ULONGLONG, VT_UI8, uhVal.QuadPart)
DEFINE_SCALAR_ACCESSORS(SignedLargeIntegerValue, LONGLONG, VT_I8, hVal.QuadPart)
DEFINE_SCALAR_ACCESSORS(FloatValue, FLOAT, VT_R4, fltVal)
DEFINE_SCALAR_ACCESSORS(ErrorValue, HRESULT, VT_ERROR, scode)

static HRESULT WINAPI values_SetKeyValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, REFPROPERTYKEY value)
{
    struct property_key_holder *holder;
    PROPVARIANT variant;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    holder = HeapAlloc(GetProcessHeap(), 0, sizeof(*holder));
    if (!holder)
        return E_OUTOFMEMORY;

    holder->IUnknown_iface.lpVtbl = &property_key_holder_vtbl;
    holder->refcount = 1;
    holder->key = *value;
    PropVariantInit(&variant);
    variant.vt = VT_UNKNOWN;
    variant.punkVal = &holder->IUnknown_iface;
    hr = values_SetValue(iface, key, &variant);
    IUnknown_Release(&holder->IUnknown_iface);
    return hr;
}

static HRESULT WINAPI values_GetKeyValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, PROPERTYKEY *value)
{
    struct property_key_holder *holder;
    PROPVARIANT variant;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    hr = values_get_typed(iface, key, VT_UNKNOWN, &variant, NULL);
    if (FAILED(hr))
        return hr;

    if (!variant.punkVal || variant.punkVal->lpVtbl != &property_key_holder_vtbl)
        hr = DISP_E_TYPEMISMATCH;
    else
    {
        holder = impl_from_key_holder(variant.punkVal);
        *value = holder->key;
    }
    PropVariantClear(&variant);
    return hr;
}

static HRESULT WINAPI values_SetBoolValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, BOOL value)
{
    PROPVARIANT variant;

    PropVariantInit(&variant);
    variant.vt = VT_BOOL;
    variant.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return values_SetValue(iface, key, &variant);
}

static HRESULT WINAPI values_GetBoolValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, BOOL *value)
{
    PROPVARIANT variant;
    BOOL conversion_attempted;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    hr = values_get_typed(iface, key, VT_BOOL, &variant, &conversion_attempted);
    if (SUCCEEDED(hr))
        *value = variant.boolVal != VARIANT_FALSE;
    else if (conversion_attempted)
        *value = FALSE;
    PropVariantClear(&variant);
    return hr;
}

static HRESULT WINAPI values_SetIUnknownValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, IUnknown *value)
{
    PROPVARIANT variant;

    if (!value)
        return E_POINTER;
    PropVariantInit(&variant);
    variant.vt = VT_UNKNOWN;
    variant.punkVal = value;
    return values_SetValue(iface, key, &variant);
}

static HRESULT WINAPI values_GetIUnknownValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, IUnknown **value)
{
    PROPVARIANT variant;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    *value = NULL;
    hr = values_get_typed(iface, key, VT_UNKNOWN, &variant, NULL);
    if (SUCCEEDED(hr))
    {
        *value = variant.punkVal;
        variant.vt = VT_EMPTY;
    }
    PropVariantClear(&variant);
    return hr;
}

static HRESULT WINAPI values_SetGuidValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, REFGUID value)
{
    PROPVARIANT variant;

    if (!value)
        return E_POINTER;
    PropVariantInit(&variant);
    variant.vt = VT_CLSID;
    variant.puuid = (GUID *)value;
    return values_SetValue(iface, key, &variant);
}

static HRESULT WINAPI values_GetGuidValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, GUID *value)
{
    PROPVARIANT variant;
    BOOL conversion_attempted;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    hr = values_get_typed(iface, key, VT_CLSID, &variant, &conversion_attempted);
    if (SUCCEEDED(hr))
        *value = *variant.puuid;
    else if (conversion_attempted)
        memset(value, 0, sizeof(*value));
    PropVariantClear(&variant);
    return hr;
}

static HRESULT WINAPI values_SetBufferValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, BYTE *value, DWORD size)
{
    PROPVARIANT variant;

    if (!value)
        return E_POINTER;
    PropVariantInit(&variant);
    variant.vt = VT_VECTOR | VT_UI1;
    variant.caub.cElems = size;
    variant.caub.pElems = value;
    return values_set_value(iface, key, &variant);
}

static HRESULT WINAPI values_GetBufferValue(IPortableDeviceValues *iface, REFPROPERTYKEY key, BYTE **value, DWORD *size)
{
    PROPVARIANT variant;
    HRESULT hr;

    if (!value || !size)
        return E_POINTER;
    hr = values_get_typed(iface, key, VT_VECTOR | VT_UI1, &variant, NULL);
    if (SUCCEEDED(hr))
    {
        *value = variant.caub.pElems;
        *size = variant.caub.cElems;
        variant.vt = VT_EMPTY;
    }
    PropVariantClear(&variant);
    return hr;
}

static HRESULT values_get_interface(IPortableDeviceValues *iface, REFPROPERTYKEY key, REFIID iid, void **out)
{
    PROPVARIANT variant;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;
    hr = values_get_typed(iface, key, VT_UNKNOWN, &variant, NULL);
    if (FAILED(hr))
        return hr;

    if (!variant.punkVal || FAILED(IUnknown_QueryInterface(variant.punkVal, iid, out)))
        hr = DISP_E_TYPEMISMATCH;
    PropVariantClear(&variant);
    return hr;
}

#define DEFINE_INTERFACE_ACCESSORS(name, itype, iid) \
static HRESULT WINAPI values_Set##name(IPortableDeviceValues *iface, REFPROPERTYKEY key, itype *value) \
{ \
    return values_SetIUnknownValue(iface, key, (IUnknown *)value); \
} \
static HRESULT WINAPI values_Get##name(IPortableDeviceValues *iface, REFPROPERTYKEY key, itype **value) \
{ \
    return values_get_interface(iface, key, iid, (void **)value); \
}

DEFINE_INTERFACE_ACCESSORS(IPortableDeviceValuesValue, IPortableDeviceValues, &IID_IPortableDeviceValues)
DEFINE_INTERFACE_ACCESSORS(IPortableDevicePropVariantCollectionValue, IPortableDevicePropVariantCollection, &IID_IPortableDevicePropVariantCollection)
DEFINE_INTERFACE_ACCESSORS(IPortableDeviceKeyCollectionValue, IPortableDeviceKeyCollection, &IID_IPortableDeviceKeyCollection)
DEFINE_INTERFACE_ACCESSORS(IPortableDeviceValuesCollectionValue, IPortableDeviceValuesCollection, &IID_IPortableDeviceValuesCollection)

static HRESULT WINAPI values_RemoveValue(IPortableDeviceValues *iface, REFPROPERTYKEY key)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);
    HRESULT hr = S_OK;
    LONG index;

    if (!key)
        return E_POINTER;

    EnterCriticalSection(&values->cs);
    index = values_find(values, key);
    if (index < 0)
        hr = S_OK;
    else
    {
        PropVariantClear(&values->entries[index].value);
        if ((DWORD)index + 1 < values->count)
            memmove(&values->entries[index], &values->entries[index + 1], (values->count - index - 1) * sizeof(*values->entries));
        --values->count;
        memset(&values->entries[values->count], 0, sizeof(*values->entries));
    }
    LeaveCriticalSection(&values->cs);
    return hr;
}

static HRESULT WINAPI values_CopyValuesFromPropertyStore(IPortableDeviceValues *iface, IPropertyStore *store)
{
    PROPVARIANT value;
    PROPERTYKEY key;
    DWORD count, i;
    HRESULT hr;

    if (!store)
        return E_POINTER;
    if (FAILED(hr = IPropertyStore_GetCount(store, &count)))
        return hr;

    for (i = 0; i < count; ++i)
    {
        if (FAILED(hr = IPropertyStore_GetAt(store, i, &key)))
            return hr;
        PropVariantInit(&value);
        hr = IPropertyStore_GetValue(store, &key, &value);
        if (SUCCEEDED(hr))
            hr = values_SetValue(iface, &key, &value);
        PropVariantClear(&value);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT WINAPI values_CopyValuesToPropertyStore(IPortableDeviceValues *iface, IPropertyStore *store)
{
    PROPVARIANT value;
    PROPERTYKEY key;
    DWORD count, i;
    HRESULT hr;

    if (!store)
        return E_POINTER;
    if (FAILED(hr = values_GetCount(iface, &count)))
        return hr;

    for (i = 0; i < count; ++i)
    {
        PropVariantInit(&value);
        hr = values_GetAt(iface, i, &key, &value);
        if (SUCCEEDED(hr))
            hr = IPropertyStore_SetValue(store, &key, &value);
        PropVariantClear(&value);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static HRESULT WINAPI values_Clear(IPortableDeviceValues *iface)
{
    struct portable_device_values *values = impl_from_IPortableDeviceValues(iface);
    DWORD i;

    EnterCriticalSection(&values->cs);
    for (i = 0; i < values->count; ++i)
        PropVariantClear(&values->entries[i].value);
    HeapFree(GetProcessHeap(), 0, values->entries);
    values->entries = NULL;
    values->count = 0;
    values->capacity = 0;
    LeaveCriticalSection(&values->cs);
    return S_OK;
}

static const IPortableDeviceValuesVtbl values_vtbl =
{
    values_QueryInterface,
    values_AddRef,
    values_Release,
    values_GetCount,
    values_GetAt,
    values_SetValue,
    values_GetValue,
    values_SetStringValue,
    values_GetStringValue,
    values_SetUnsignedIntegerValue,
    values_GetUnsignedIntegerValue,
    values_SetSignedIntegerValue,
    values_GetSignedIntegerValue,
    values_SetUnsignedLargeIntegerValue,
    values_GetUnsignedLargeIntegerValue,
    values_SetSignedLargeIntegerValue,
    values_GetSignedLargeIntegerValue,
    values_SetFloatValue,
    values_GetFloatValue,
    values_SetErrorValue,
    values_GetErrorValue,
    values_SetKeyValue,
    values_GetKeyValue,
    values_SetBoolValue,
    values_GetBoolValue,
    values_SetIUnknownValue,
    values_GetIUnknownValue,
    values_SetGuidValue,
    values_GetGuidValue,
    values_SetBufferValue,
    values_GetBufferValue,
    values_SetIPortableDeviceValuesValue,
    values_GetIPortableDeviceValuesValue,
    values_SetIPortableDevicePropVariantCollectionValue,
    values_GetIPortableDevicePropVariantCollectionValue,
    values_SetIPortableDeviceKeyCollectionValue,
    values_GetIPortableDeviceKeyCollectionValue,
    values_SetIPortableDeviceValuesCollectionValue,
    values_GetIPortableDeviceValuesCollectionValue,
    values_RemoveValue,
    values_CopyValuesFromPropertyStore,
    values_CopyValuesToPropertyStore,
    values_Clear,
};

HRESULT portable_device_values_create(REFIID iid, void **out)
{
    struct portable_device_values *values;
    HRESULT hr;

    if (!out)
        return E_POINTER;
    *out = NULL;

    values = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*values));
    if (!values)
        return E_OUTOFMEMORY;
    values->IPortableDeviceValues_iface.lpVtbl = &values_vtbl;
    values->refcount = 1;
    InitializeCriticalSection(&values->cs);

    hr = IPortableDeviceValues_QueryInterface(&values->IPortableDeviceValues_iface, iid, out);
    IPortableDeviceValues_Release(&values->IPortableDeviceValues_iface);
    return hr;
}
