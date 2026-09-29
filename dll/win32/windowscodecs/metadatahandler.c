/*
 * Copyright 2012 Vincent Povirk for CodeWeavers
 * Copyright 2012 Dmitry Timoshkov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "objbase.h"
#include "ole2.h"
#include "propvarutil.h"

#include "wincodecs_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wincodecs);

static inline MetadataHandler *impl_from_IWICMetadataWriter(IWICMetadataWriter *iface)
{
    return CONTAINING_RECORD(iface, MetadataHandler, IWICMetadataWriter_iface);
}

static inline MetadataHandler *impl_from_IWICPersistStream(IWICPersistStream *iface)
{
    return CONTAINING_RECORD(iface, MetadataHandler, IWICPersistStream_iface);
}

static inline MetadataHandler *impl_from_IWICStreamProvider(IWICStreamProvider *iface)
{
    return CONTAINING_RECORD(iface, MetadataHandler, IWICStreamProvider_iface);
}

void clear_metadata_item(MetadataItem *item)
{
    PropVariantClear(&item->schema);
    PropVariantClear(&item->id);
    PropVariantClear(&item->value);
}

void MetadataHandler_FreeItems(MetadataHandler *This)
{
    DWORD i;

    for (i=0; i<This->item_count; i++)
        clear_metadata_item(&This->items[i]);

    free(This->items);
    This->items = NULL;
    This->item_count = 0;
}

static HRESULT MetadataHandlerEnum_Create(MetadataHandler *parent, DWORD index,
    IWICEnumMetadataItem **ppIEnumMetadataItem);

static HRESULT WINAPI MetadataHandler_QueryInterface(IWICMetadataWriter *iface, REFIID iid,
    void **ppv)
{
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);
    TRACE("(%p,%s,%p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) ||
        IsEqualIID(&IID_IWICMetadataReader, iid) ||
        (IsEqualIID(&IID_IWICMetadataWriter, iid) && This->vtable->flags & METADATAHANDLER_IS_WRITER))
    {
        *ppv = &This->IWICMetadataWriter_iface;
    }
    else if (IsEqualIID(&IID_IPersist, iid) ||
             IsEqualIID(&IID_IPersistStream, iid) ||
             IsEqualIID(&IID_IWICPersistStream, iid))
    {
        *ppv = &This->IWICPersistStream_iface;
    }
    else if (IsEqualIID(&IID_IWICStreamProvider, iid))
    {
        *ppv = &This->IWICStreamProvider_iface;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI MetadataHandler_AddRef(IWICMetadataWriter *iface)
{
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI MetadataHandler_Release(IWICMetadataWriter *iface)
{
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    if (ref == 0)
    {
        if (This->stream)
            IStream_Release(This->stream);
        MetadataHandler_FreeItems(This);
        This->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->lock);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI MetadataHandler_GetMetadataHandlerInfo(IWICMetadataWriter *iface,
    IWICMetadataHandlerInfo **ppIHandler)
{
    HRESULT hr;
    IWICComponentInfo *component_info;
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);

    TRACE("%p,%p\n", iface, ppIHandler);

#ifdef __REACTOS__
    hr = GetCachedComponentInfo(This->vtable->clsid, &component_info);
#else
    hr = CreateComponentInfo(This->vtable->clsid, &component_info);
#endif
    if (FAILED(hr)) return hr;

    hr = IWICComponentInfo_QueryInterface(component_info, &IID_IWICMetadataHandlerInfo,
        (void **)ppIHandler);

    IWICComponentInfo_Release(component_info);
    return hr;
}

static HRESULT WINAPI MetadataHandler_GetMetadataFormat(IWICMetadataWriter *iface,
    GUID *pguidMetadataFormat)
{
    HRESULT hr;
    IWICMetadataHandlerInfo *metadata_info;

    TRACE("%p,%p\n", iface, pguidMetadataFormat);

    if (!pguidMetadataFormat) return E_INVALIDARG;

#ifdef __REACTOS__
    {
        IWICComponentInfo *component_info;
        MetadataHandler *This = impl_from_IWICMetadataWriter(iface);

        hr = CreateComponentInfo(This->vtable->clsid, &component_info);
        if (FAILED(hr)) return hr;
        hr = IWICComponentInfo_QueryInterface(component_info, &IID_IWICMetadataHandlerInfo, (void **)&metadata_info);
        IWICComponentInfo_Release(component_info);
    }
#else
    hr = MetadataHandler_GetMetadataHandlerInfo(iface, &metadata_info);
#endif
    if (FAILED(hr)) return hr;

    hr = IWICMetadataHandlerInfo_GetMetadataFormat(metadata_info, pguidMetadataFormat);
    IWICMetadataHandlerInfo_Release(metadata_info);

    return hr;
}

static HRESULT WINAPI MetadataHandler_GetCount(IWICMetadataWriter *iface,
    UINT *pcCount)
{
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);

    TRACE("%p,%p\n", iface, pcCount);

    if (!pcCount) return E_INVALIDARG;

    EnterCriticalSection(&This->lock);
    *pcCount = This->item_count;
    LeaveCriticalSection(&This->lock);
    return S_OK;
}

static HRESULT WINAPI MetadataHandler_GetValueByIndex(IWICMetadataWriter *iface,
    UINT index, PROPVARIANT *schema, PROPVARIANT *id, PROPVARIANT *value)
{
    HRESULT hr = S_OK;
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);

    TRACE("%p,%u,%p,%p,%p\n", iface, index, schema, id, value);

    EnterCriticalSection(&This->lock);

    if (index >= This->item_count)
    {
        LeaveCriticalSection(&This->lock);
#ifdef __REACTOS__
        return This->vtable->invalid_index_error ? This->vtable->invalid_index_error : E_INVALIDARG;
#else
        return E_INVALIDARG;
#endif
    }

    if (schema)
        hr = PropVariantCopy(schema, &This->items[index].schema);

    if (SUCCEEDED(hr) && id)
        hr = PropVariantCopy(id, &This->items[index].id);

    if (SUCCEEDED(hr) && value)
        hr = PropVariantCopy(value, &This->items[index].value);

    LeaveCriticalSection(&This->lock);
    return hr;
}

static MetadataItem *metadatahandler_get_item(MetadataHandler *handler, const PROPVARIANT *schema,
        const PROPVARIANT *id, unsigned int *item_index)
{
    PROPVARIANT index;
    GUID format;
    HRESULT hr;
    UINT i;

    PROPVAR_COMPARE_FLAGS compare_flags = handler->vtable->flags & METADATAHANDLER_CASE_SENSITIVE ? 0 : PVCF_USESTRCMPI;

    if (item_index) *item_index = 0;
    PropVariantInit(&index);
    if (id->vt == VT_CLSID && SUCCEEDED(PropVariantChangeType(&index, schema, 0, VT_UI4)))
    {
        for (i = 0; i < handler->item_count; i++)
        {
            PROPVARIANT *value = &handler->items[i].value;
            IWICMetadataReader *reader;

            if (value->vt != VT_UNKNOWN) continue;

            if (SUCCEEDED(IUnknown_QueryInterface(value->punkVal, &IID_IWICMetadataReader, (void **)&reader)))
            {
                hr = IWICMetadataReader_GetMetadataFormat(reader, &format);
                IWICMetadataReader_Release(reader);

                if (SUCCEEDED(hr))
                {
                    if (IsEqualGUID(&format, id->puuid))
                    {
                        if (!index.ulVal)
                        {
                            if (item_index) *item_index = i;
                            return &handler->items[i];
                        }
                        --index.ulVal;
                    }
                }
            }
        }
    }

    for (i = 0; i < handler->item_count; i++)
    {
        if (schema && handler->items[i].schema.vt != VT_EMPTY &&
            (!(handler->vtable->flags & METADATAHANDLER_CASE_SENSITIVE) || schema->vt != VT_EMPTY))
        {
            if (PropVariantCompareEx(schema, &handler->items[i].schema, 0, compare_flags) != 0) continue;
        }

        if (PropVariantCompareEx(id, &handler->items[i].id, 0, compare_flags) != 0) continue;

        if (item_index) *item_index = i;
        return &handler->items[i];
    }

    return NULL;
}

static void metadata_handler_remove_item(MetadataHandler *handler, unsigned int index)
{
    clear_metadata_item(&handler->items[index]);
    ++handler->modification;
    handler->item_count--;
    if (index != handler->item_count)
        memmove(&handler->items[index], &handler->items[index + 1],
                (handler->item_count - index) * sizeof(*handler->items));
}

static HRESULT WINAPI MetadataHandler_GetValue(IWICMetadataWriter *iface,
    const PROPVARIANT *schema, const PROPVARIANT *id, PROPVARIANT *value)
{
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);
    HRESULT hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    MetadataItem *item;

    TRACE("(%p,%s,%s,%p)\n", iface, wine_dbgstr_variant((const VARIANT *)schema), wine_dbgstr_variant((const VARIANT *)id), value);

    if (!id) return E_INVALIDARG;

    if (This->vtable->flags & METADATAHANDLER_NAMED_ITEMS &&
        id->vt != VT_LPSTR && id->vt != VT_LPWSTR && id->vt != VT_BSTR)
        return E_INVALIDARG;

    EnterCriticalSection(&This->lock);

#ifdef __REACTOS__
    if (!This->item_count && This->vtable->empty_value_error)
        hr = This->vtable->empty_value_error;

#endif
    if ((item = metadatahandler_get_item(This, schema, id, NULL)))
    {
        hr = value ? PropVariantCopy(value, &item->value) : S_OK;
    }

    LeaveCriticalSection(&This->lock);
    return hr;
}

static HRESULT WINAPI MetadataHandler_GetEnumerator(IWICMetadataWriter *iface,
    IWICEnumMetadataItem **ppIEnumMetadata)
{
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);
    TRACE("(%p,%p)\n", iface, ppIEnumMetadata);
    return MetadataHandlerEnum_Create(This, 0, ppIEnumMetadata);
}

static HRESULT WINAPI MetadataHandler_SetValue(IWICMetadataWriter *iface,
        const PROPVARIANT *schema, const PROPVARIANT *id, const PROPVARIANT *value)
{
    MetadataHandler *This = impl_from_IWICMetadataWriter(iface);
    MetadataItem *item, *new_items;
    PROPVARIANT normalized_id;
    HRESULT hr;

    TRACE("(%p,%p,%p,%p)\n", iface, schema, id, value);

    if (!id || !value)
        return E_INVALIDARG;

    if (This->vtable->flags & METADATAHANDLER_NAMED_ITEMS &&
        id->vt != VT_LPSTR && id->vt != VT_LPWSTR && id->vt != VT_BSTR)
        return E_INVALIDARG;

    PropVariantInit(&normalized_id);
    if (This->vtable->fnPrepareValue)
    {
        hr = This->vtable->fnPrepareValue(id, value, &normalized_id);
        if (FAILED(hr))
        {
            PropVariantClear(&normalized_id);
            return hr;
        }
        id = &normalized_id;
    }

    /* Replace value of an existing item, or append a new one. */

    EnterCriticalSection(&This->lock);

    if ((item = metadatahandler_get_item(This, schema, id, NULL)))
    {
        ++This->modification;
        PropVariantClear(&item->value);
        hr = PropVariantCopy(&item->value, value);
    }
    else
    {
        new_items = realloc(This->items, (This->item_count + 1) * sizeof(*new_items));
        if (new_items)
        {
            This->items = new_items;

            item = &This->items[This->item_count];

            PropVariantInit(&item->schema);
            PropVariantInit(&item->id);
            PropVariantInit(&item->value);

            /* Skip setting the schema value, it's probably format-dependent. */
            hr = PropVariantCopy(&item->id, id);
            if (SUCCEEDED(hr))
                hr = PropVariantCopy(&item->value, value);

            if (SUCCEEDED(hr))
            {
                ++This->item_count;
                ++This->modification;
            }
            else
                clear_metadata_item(item);
        }
        else
        {
            hr = E_OUTOFMEMORY;
        }
    }

    LeaveCriticalSection(&This->lock);

    PropVariantClear(&normalized_id);
    return hr;
}

static HRESULT WINAPI MetadataHandler_SetValueByIndex(IWICMetadataWriter *iface,
    UINT nIndex, const PROPVARIANT *pvarSchema, const PROPVARIANT *pvarId, const PROPVARIANT *pvarValue)
{
    TRACE("(%p,%u,%p,%p,%p)\n", iface, nIndex, pvarSchema, pvarId, pvarValue);
    return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

static HRESULT WINAPI MetadataHandler_RemoveValue(IWICMetadataWriter *iface,
        const PROPVARIANT *schema, const PROPVARIANT *id)
{
    MetadataHandler *handler = impl_from_IWICMetadataWriter(iface);
    unsigned int index;
    HRESULT hr = S_OK;

    TRACE("(%p,%p,%p)\n", iface, schema, id);

    if (handler->vtable->flags & METADATAHANDLER_FIXED_ITEMS)
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;

    EnterCriticalSection(&handler->lock);

    if (metadatahandler_get_item(handler, schema, id, &index))
    {
        metadata_handler_remove_item(handler, index);
    }
    else
    {
        hr = WINCODEC_ERR_PROPERTYNOTFOUND;
    }

    LeaveCriticalSection(&handler->lock);

    return hr;
}

static HRESULT WINAPI MetadataHandler_RemoveValueByIndex(IWICMetadataWriter *iface, UINT index)
{
    MetadataHandler *handler = impl_from_IWICMetadataWriter(iface);
    HRESULT hr = S_OK;

    TRACE("(%p,%u)\n", iface, index);

    EnterCriticalSection(&handler->lock);

    if (index >= handler->item_count)
        hr = E_INVALIDARG;
    else if (handler->vtable->flags & METADATAHANDLER_FIXED_ITEMS)
        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    else
        metadata_handler_remove_item(handler, index);

    LeaveCriticalSection(&handler->lock);

    return hr;
}

static const IWICMetadataWriterVtbl MetadataHandler_Vtbl = {
    MetadataHandler_QueryInterface,
    MetadataHandler_AddRef,
    MetadataHandler_Release,
    MetadataHandler_GetMetadataFormat,
    MetadataHandler_GetMetadataHandlerInfo,
    MetadataHandler_GetCount,
    MetadataHandler_GetValueByIndex,
    MetadataHandler_GetValue,
    MetadataHandler_GetEnumerator,
    MetadataHandler_SetValue,
    MetadataHandler_SetValueByIndex,
    MetadataHandler_RemoveValue,
    MetadataHandler_RemoveValueByIndex
};

static HRESULT WINAPI MetadataHandler_PersistStream_QueryInterface(IWICPersistStream *iface,
    REFIID iid, void **ppv)
{
    MetadataHandler *This = impl_from_IWICPersistStream(iface);
    return IWICMetadataWriter_QueryInterface(&This->IWICMetadataWriter_iface, iid, ppv);
}

static ULONG WINAPI MetadataHandler_PersistStream_AddRef(IWICPersistStream *iface)
{
    MetadataHandler *This = impl_from_IWICPersistStream(iface);
    return IWICMetadataWriter_AddRef(&This->IWICMetadataWriter_iface);
}

static ULONG WINAPI MetadataHandler_PersistStream_Release(IWICPersistStream *iface)
{
    MetadataHandler *This = impl_from_IWICPersistStream(iface);
    return IWICMetadataWriter_Release(&This->IWICMetadataWriter_iface);
}

static HRESULT WINAPI MetadataHandler_GetClassID(IWICPersistStream *iface, CLSID *clsid)
{
    MetadataHandler *handler = impl_from_IWICPersistStream(iface);

    TRACE("(%p,%p)\n", iface, clsid);

    if (!clsid)
        return E_INVALIDARG;

    *clsid = *handler->vtable->clsid;

    return S_OK;
}

struct metadata_snapshot
{
    MetadataItem *items;
    DWORD count;
    ULONGLONG modification, saved_modification;
};

static void free_metadata_snapshot(struct metadata_snapshot *snapshot)
{
    DWORD i;

    for (i = 0; i < snapshot->count; ++i) clear_metadata_item(&snapshot->items[i]);
    free(snapshot->items);
}

static HRESULT get_metadata_snapshot(MetadataHandler *handler, struct metadata_snapshot *snapshot,
        IStream *destination, BOOL clear_dirty)
{
    DWORD i;
    HRESULT hr = S_OK;

    memset(snapshot, 0, sizeof(*snapshot));
    EnterCriticalSection(&handler->lock);
    if (destination && destination == handler->stream)
        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    else if (!(snapshot->items = calloc(handler->item_count ? handler->item_count : 1, sizeof(*snapshot->items))))
        hr = E_OUTOFMEMORY;
    else
    {
        snapshot->count = handler->item_count;
        for (i = 0; i < snapshot->count; ++i)
        {
            hr = PropVariantCopy(&snapshot->items[i].schema, &handler->items[i].schema);
            if (SUCCEEDED(hr)) hr = PropVariantCopy(&snapshot->items[i].id, &handler->items[i].id);
            if (SUCCEEDED(hr)) hr = PropVariantCopy(&snapshot->items[i].value, &handler->items[i].value);
            if (FAILED(hr)) break;
        }
        if (SUCCEEDED(hr) && clear_dirty && (handler->vtable->flags & METADATAHANDLER_NESTED_ITEMS))
            ++handler->modification;
        snapshot->modification = handler->modification;
        snapshot->saved_modification = handler->saved_modification;
    }
    LeaveCriticalSection(&handler->lock);
    if (FAILED(hr)) free_metadata_snapshot(snapshot);
    return hr;
}

static HRESULT WINAPI MetadataHandler_IsDirty(IWICPersistStream *iface)
{
    MetadataHandler *handler = impl_from_IWICPersistStream(iface);
    struct metadata_snapshot snapshot;
    IWICPersistStream *child;
    DWORD i;
    HRESULT hr;

    if (!handler->vtable->fnSerialize) return E_NOTIMPL;
    if (!(handler->vtable->flags & METADATAHANDLER_NESTED_ITEMS))
    {
        EnterCriticalSection(&handler->lock);
        hr = handler->modification != handler->saved_modification ? S_OK : S_FALSE;
        LeaveCriticalSection(&handler->lock);
        return hr;
    }
    hr = get_metadata_snapshot(handler, &snapshot, NULL, FALSE);
    if (FAILED(hr)) return hr;
    hr = snapshot.modification != snapshot.saved_modification ? S_OK : S_FALSE;
    if (hr == S_FALSE && (handler->vtable->flags & METADATAHANDLER_NESTED_ITEMS))
    {
        for (i = 0; i < snapshot.count; ++i)
        {
            if (snapshot.items[i].value.vt != VT_UNKNOWN || !snapshot.items[i].value.punkVal) continue;
            hr = IUnknown_QueryInterface(snapshot.items[i].value.punkVal, &IID_IWICPersistStream, (void **)&child);
            if (SUCCEEDED(hr))
            {
                hr = IWICPersistStream_IsDirty(child);
                IWICPersistStream_Release(child);
            }
            if (hr != S_FALSE) break;
        }
    }
    free_metadata_snapshot(&snapshot);
    return hr;
}

static HRESULT WINAPI MetadataHandler_Load(IWICPersistStream *iface,
    IStream *pStm)
{
    MetadataHandler *This = impl_from_IWICPersistStream(iface);
    TRACE("(%p,%p)\n", iface, pStm);
    return IWICPersistStream_LoadEx(&This->IWICPersistStream_iface, pStm, NULL, WICPersistOptionDefault);
}

static HRESULT WINAPI MetadataHandler_Save(IWICPersistStream *iface,
    IStream *pStm, BOOL fClearDirty)
{
    return IWICPersistStream_SaveEx(iface, pStm, WICPersistOptionDefault, fClearDirty);
}

static HRESULT WINAPI MetadataHandler_GetSizeMax(IWICPersistStream *iface,
    ULARGE_INTEGER *pcbSize)
{
    MetadataHandler *handler = impl_from_IWICPersistStream(iface);
    struct metadata_snapshot snapshot;
    BYTE *data = NULL;
    ULONG size;
    DWORD options;
    HRESULT hr;

    if (!pcbSize) return E_INVALIDARG;
    if (!handler->vtable->fnSerialize) return E_NOTIMPL;
    EnterCriticalSection(&handler->lock);
    options = handler->persist_options;
    LeaveCriticalSection(&handler->lock);
    hr = get_metadata_snapshot(handler, &snapshot, NULL, FALSE);
    if (FAILED(hr)) return hr;
    hr = handler->vtable->fnSerialize(snapshot.items, snapshot.count, options, 0, FALSE, &data, &size);
    free_metadata_snapshot(&snapshot);
    if (SUCCEEDED(hr)) pcbSize->QuadPart = size;
    free(data);
    return hr;
}

static HRESULT metadata_handler_load_detached(MetadataHandler *handler, IStream *stream,
        const GUID *vendor, DWORD options)
{
    MetadataHandler staged = {0}, old = {0};
    LARGE_INTEGER move = {0};
    ULARGE_INTEGER origin = {0};
    IStream *old_stream = NULL;
    HRESULT hr = S_OK;

    IWICMetadataWriter_AddRef(&handler->IWICMetadataWriter_iface);
    staged.vtable = handler->vtable;
    if (stream)
    {
        IStream_AddRef(stream);
        hr = IStream_Seek(stream, move, STREAM_SEEK_CUR, &origin);
        if (SUCCEEDED(hr)) hr = handler->vtable->fnLoad(&staged, stream, vendor, options);
    }
    if (SUCCEEDED(hr))
    {
        EnterCriticalSection(&handler->lock);
        old.items = handler->items;
        old.item_count = handler->item_count;
        old_stream = handler->stream;
        handler->items = staged.items;
        handler->item_count = staged.item_count;
        staged.items = NULL;
        staged.item_count = 0;
        handler->stream = options & WICPersistOptionNoCacheStream ? NULL : stream;
        if (handler->stream) stream = NULL;
        handler->origin = origin;
        handler->persist_options = options & WICPersistOptionMask;
        handler->saved_modification = ++handler->modification;
        LeaveCriticalSection(&handler->lock);
    }
    if (old_stream) IStream_Release(old_stream);
    if (stream) IStream_Release(stream);
    MetadataHandler_FreeItems(&old);
    MetadataHandler_FreeItems(&staged);
    IWICMetadataWriter_Release(&handler->IWICMetadataWriter_iface);
    return hr;
}

static HRESULT WINAPI MetadataHandler_LoadEx(IWICPersistStream *iface,
    IStream *stream, const GUID *pguidPreferredVendor, DWORD dwPersistOptions)
{
    MetadataHandler *This = impl_from_IWICPersistStream(iface);
    HRESULT hr = S_OK;
    LARGE_INTEGER move;

    TRACE("(%p,%p,%s,%lx)\n", iface, stream, debugstr_guid(pguidPreferredVendor), dwPersistOptions);

    if (This->vtable->flags & METADATAHANDLER_DETACHED_LOAD)
        return metadata_handler_load_detached(This, stream, pguidPreferredVendor, dwPersistOptions);

    EnterCriticalSection(&This->lock);

    This->origin.QuadPart = 0;
    if (stream)
    {
        move.QuadPart = 0;
        hr = IStream_Seek(stream, move, STREAM_SEEK_CUR, &This->origin);
        if (SUCCEEDED(hr))
            hr = This->vtable->fnLoad(This, stream, pguidPreferredVendor, dwPersistOptions);
    }

    if (This->stream)
        IStream_Release(This->stream);
    This->stream = NULL;

    if (!(dwPersistOptions & WICPersistOptionNoCacheStream))
    {
        This->stream = stream;
        if (This->stream)
            IStream_AddRef(This->stream);
    }
    This->persist_options = dwPersistOptions & WICPersistOptionMask;
    if (SUCCEEDED(hr)) This->saved_modification = ++This->modification;

    LeaveCriticalSection(&This->lock);

    return hr;
}

static HRESULT WINAPI MetadataHandler_SaveEx(IWICPersistStream *iface,
    IStream *pIStream, DWORD dwPersistOptions, BOOL fClearDirty)
{
    MetadataHandler *handler = impl_from_IWICPersistStream(iface);
    struct metadata_snapshot snapshot;
    BYTE *data = NULL;
    ULONG size, written;
    ULARGE_INTEGER offset = {0};
    LARGE_INTEGER move = {0};
    HRESULT hr;

    if (!(handler->vtable->flags & METADATAHANDLER_IS_WRITER))
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    if (!pIStream) return E_INVALIDARG;
    if (!handler->vtable->fnSerialize) return E_NOTIMPL;
    if (handler->vtable->flags & METADATAHANDLER_ABSOLUTE_OFFSETS)
    {
        hr = IStream_Seek(pIStream, move, STREAM_SEEK_CUR, &offset);
        if (FAILED(hr)) return hr;
    }
    hr = get_metadata_snapshot(handler, &snapshot, pIStream, fClearDirty);
    if (FAILED(hr)) return hr;
    hr = handler->vtable->fnSerialize(snapshot.items, snapshot.count, dwPersistOptions,
            offset.QuadPart, fClearDirty, &data, &size);
    if (SUCCEEDED(hr))
    {
        hr = IStream_Write(pIStream, data, size, &written);
        if (SUCCEEDED(hr) && written != size) hr = WINCODEC_ERR_STREAMWRITE;
        if (SUCCEEDED(hr) && fClearDirty)
        {
            EnterCriticalSection(&handler->lock);
            if (handler->modification == snapshot.modification)
                handler->saved_modification = snapshot.modification;
            LeaveCriticalSection(&handler->lock);
        }
    }
    free_metadata_snapshot(&snapshot);
    free(data);
    return hr;
}

static const IWICPersistStreamVtbl MetadataHandler_PersistStream_Vtbl = {
    MetadataHandler_PersistStream_QueryInterface,
    MetadataHandler_PersistStream_AddRef,
    MetadataHandler_PersistStream_Release,
    MetadataHandler_GetClassID,
    MetadataHandler_IsDirty,
    MetadataHandler_Load,
    MetadataHandler_Save,
    MetadataHandler_GetSizeMax,
    MetadataHandler_LoadEx,
    MetadataHandler_SaveEx
};

static HRESULT WINAPI metadatahandler_stream_provider_QueryInterface(IWICStreamProvider *iface, REFIID iid, void **ppv)
{
    MetadataHandler *handler = impl_from_IWICStreamProvider(iface);
    return IWICMetadataWriter_QueryInterface(&handler->IWICMetadataWriter_iface, iid, ppv);
}

static ULONG WINAPI metadatahandler_stream_provider_AddRef(IWICStreamProvider *iface)
{
    MetadataHandler *handler = impl_from_IWICStreamProvider(iface);
    return IWICMetadataWriter_AddRef(&handler->IWICMetadataWriter_iface);
}

static ULONG WINAPI metadatahandler_stream_provider_Release(IWICStreamProvider *iface)
{
    MetadataHandler *handler = impl_from_IWICStreamProvider(iface);
    return IWICMetadataWriter_Release(&handler->IWICMetadataWriter_iface);
}

static HRESULT WINAPI metadatahandler_stream_provider_GetStream(IWICStreamProvider *iface, IStream **stream)
{
    MetadataHandler *handler = impl_from_IWICStreamProvider(iface);
    HRESULT hr = S_OK;

    TRACE("%p, %p.\n", iface, stream);

    if (!stream)
        return E_INVALIDARG;

    EnterCriticalSection(&handler->lock);

    if (handler->stream)
    {
        if (SUCCEEDED(hr = IStream_Seek(handler->stream, *(LARGE_INTEGER *)&handler->origin, STREAM_SEEK_SET, NULL)))
        {
            *stream = handler->stream;
            IStream_AddRef(*stream);
        }
    }
    else
    {
        hr = WINCODEC_ERR_STREAMNOTAVAILABLE;
    }

    LeaveCriticalSection(&handler->lock);

    return hr;
}

static HRESULT WINAPI metadatahandler_stream_provider_GetPersistOptions(IWICStreamProvider *iface, DWORD *options)
{
    MetadataHandler *handler = impl_from_IWICStreamProvider(iface);

    TRACE("%p, %p.\n", iface, options);

    if (!options)
        return E_INVALIDARG;

    *options = handler->persist_options;

    return S_OK;
}

static HRESULT WINAPI metadatahandler_stream_provider_GetPreferredVendorGUID(IWICStreamProvider *iface, GUID *guid)
{
    TRACE("%p, %p.\n", iface, guid);

    if (!guid)
        return E_INVALIDARG;

    memcpy(guid, &GUID_VendorMicrosoft, sizeof(*guid));
    return S_OK;
}

static HRESULT WINAPI metadatahandler_stream_provider_RefreshStream(IWICStreamProvider *iface)
{
    FIXME("%p stub\n", iface);

    return E_NOTIMPL;
}

static const IWICStreamProviderVtbl MetadataHandler_StreamProvider_Vtbl =
{
    metadatahandler_stream_provider_QueryInterface,
    metadatahandler_stream_provider_AddRef,
    metadatahandler_stream_provider_Release,
    metadatahandler_stream_provider_GetStream,
    metadatahandler_stream_provider_GetPersistOptions,
    metadatahandler_stream_provider_GetPreferredVendorGUID,
    metadatahandler_stream_provider_RefreshStream,
};

HRESULT MetadataReader_Create(const MetadataHandlerVtbl *vtable, REFIID iid, void** ppv)
{
    MetadataHandler *This;
    HRESULT hr = S_OK;

    TRACE("%s\n", debugstr_guid(vtable->clsid));

    *ppv = NULL;

    This = calloc(1, sizeof(*This));
    if (!This) return E_OUTOFMEMORY;

    This->IWICMetadataWriter_iface.lpVtbl = &MetadataHandler_Vtbl;
    This->IWICPersistStream_iface.lpVtbl = &MetadataHandler_PersistStream_Vtbl;
    This->IWICStreamProvider_iface.lpVtbl = &MetadataHandler_StreamProvider_Vtbl;
    This->ref = 1;
    This->vtable = vtable;

    InitializeCriticalSectionEx(&This->lock, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    This->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": MetadataHandler.lock");

    if (This->vtable->fnCreate)
        hr = This->vtable->fnCreate(This);

    if (SUCCEEDED(hr))
        hr = IWICMetadataWriter_QueryInterface(&This->IWICMetadataWriter_iface, iid, ppv);

    IWICMetadataWriter_Release(&This->IWICMetadataWriter_iface);

    return hr;
}

typedef struct MetadataHandlerEnum {
    IWICEnumMetadataItem IWICEnumMetadataItem_iface;
    LONG ref;
    MetadataHandler *parent;
    DWORD index;
} MetadataHandlerEnum;

static inline MetadataHandlerEnum *impl_from_IWICEnumMetadataItem(IWICEnumMetadataItem *iface)
{
    return CONTAINING_RECORD(iface, MetadataHandlerEnum, IWICEnumMetadataItem_iface);
}

static HRESULT WINAPI MetadataHandlerEnum_QueryInterface(IWICEnumMetadataItem *iface, REFIID iid,
    void **ppv)
{
    MetadataHandlerEnum *This = impl_from_IWICEnumMetadataItem(iface);
    TRACE("(%p,%s,%p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) ||
        IsEqualIID(&IID_IWICEnumMetadataItem, iid))
    {
        *ppv = &This->IWICEnumMetadataItem_iface;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI MetadataHandlerEnum_AddRef(IWICEnumMetadataItem *iface)
{
    MetadataHandlerEnum *This = impl_from_IWICEnumMetadataItem(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI MetadataHandlerEnum_Release(IWICEnumMetadataItem *iface)
{
    MetadataHandlerEnum *This = impl_from_IWICEnumMetadataItem(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%lu\n", iface, ref);

    if (ref == 0)
    {
        IWICMetadataWriter_Release(&This->parent->IWICMetadataWriter_iface);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI MetadataHandlerEnum_Next(IWICEnumMetadataItem *iface,
    ULONG celt, PROPVARIANT *rgeltSchema, PROPVARIANT *rgeltId,
    PROPVARIANT *rgeltValue, ULONG *pceltFetched)
{
    MetadataHandlerEnum *This = impl_from_IWICEnumMetadataItem(iface);
    ULONG new_index;
    HRESULT hr=S_FALSE;
    ULONG i;
    ULONG fetched;

    TRACE("%p, %lu, %p, %p, %p, %p.\n", iface, celt, rgeltSchema, rgeltId, rgeltValue, pceltFetched);

    if (!pceltFetched)
        pceltFetched = &fetched;

    EnterCriticalSection(&This->parent->lock);

    if (This->index >= This->parent->item_count)
    {
        *pceltFetched = 0;
        LeaveCriticalSection(&This->parent->lock);
        return S_FALSE;
    }

    new_index = min(This->parent->item_count, This->index + celt);
    *pceltFetched = new_index - This->index;

    if (rgeltSchema)
    {
        for (i=0; SUCCEEDED(hr) && i < *pceltFetched; i++)
            hr = PropVariantCopy(&rgeltSchema[i], &This->parent->items[i+This->index].schema);
    }

    for (i=0; SUCCEEDED(hr) && i < *pceltFetched; i++)
        hr = PropVariantCopy(&rgeltId[i], &This->parent->items[i+This->index].id);

    if (rgeltValue)
    {
        for (i=0; SUCCEEDED(hr) && i < *pceltFetched; i++)
            hr = PropVariantCopy(&rgeltValue[i], &This->parent->items[i+This->index].value);
    }

    if (SUCCEEDED(hr))
    {
        This->index = new_index;
    }

    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static HRESULT WINAPI MetadataHandlerEnum_Skip(IWICEnumMetadataItem *iface,
    ULONG celt)
{
    MetadataHandlerEnum *This = impl_from_IWICEnumMetadataItem(iface);

    TRACE("%p, %lu.\n", iface, celt);

    EnterCriticalSection(&This->parent->lock);

    This->index += celt;

    LeaveCriticalSection(&This->parent->lock);

    return S_OK;
}

static HRESULT WINAPI MetadataHandlerEnum_Reset(IWICEnumMetadataItem *iface)
{
    MetadataHandlerEnum *This = impl_from_IWICEnumMetadataItem(iface);

    TRACE("%p.\n", iface);

    EnterCriticalSection(&This->parent->lock);

    This->index = 0;

    LeaveCriticalSection(&This->parent->lock);

    return S_OK;
}

static HRESULT WINAPI MetadataHandlerEnum_Clone(IWICEnumMetadataItem *iface,
    IWICEnumMetadataItem **ppIEnumMetadataItem)
{
    MetadataHandlerEnum *This = impl_from_IWICEnumMetadataItem(iface);
    HRESULT hr;

    TRACE("%p, %p.\n", iface, ppIEnumMetadataItem);

    EnterCriticalSection(&This->parent->lock);

    hr = MetadataHandlerEnum_Create(This->parent, This->index, ppIEnumMetadataItem);

    LeaveCriticalSection(&This->parent->lock);

    return hr;
}

static const IWICEnumMetadataItemVtbl MetadataHandlerEnum_Vtbl = {
    MetadataHandlerEnum_QueryInterface,
    MetadataHandlerEnum_AddRef,
    MetadataHandlerEnum_Release,
    MetadataHandlerEnum_Next,
    MetadataHandlerEnum_Skip,
    MetadataHandlerEnum_Reset,
    MetadataHandlerEnum_Clone
};

static HRESULT MetadataHandlerEnum_Create(MetadataHandler *parent, DWORD index,
    IWICEnumMetadataItem **ppIEnumMetadataItem)
{
    MetadataHandlerEnum *This;

    if (!ppIEnumMetadataItem) return E_INVALIDARG;

    *ppIEnumMetadataItem = NULL;

    This = malloc(sizeof(MetadataHandlerEnum));
    if (!This) return E_OUTOFMEMORY;

    IWICMetadataWriter_AddRef(&parent->IWICMetadataWriter_iface);

    This->IWICEnumMetadataItem_iface.lpVtbl = &MetadataHandlerEnum_Vtbl;
    This->ref = 1;
    This->parent = parent;
    This->index = index;

    *ppIEnumMetadataItem = &This->IWICEnumMetadataItem_iface;

    return S_OK;
}

static HRESULT LoadUnknownMetadata(MetadataHandler *handler, IStream *input, const GUID *preferred_vendor,
        DWORD persist_options)
{
    HRESULT hr;
    MetadataItem *result;
    STATSTG stat;
    BYTE *data;
    ULONG bytesread;

    TRACE("\n");

    hr = IStream_Stat(input, &stat, STATFLAG_NONAME);
    if (FAILED(hr))
        return hr;

    data = CoTaskMemAlloc(stat.cbSize.QuadPart);
    if (!data) return E_OUTOFMEMORY;

    hr = IStream_Read(input, data, stat.cbSize.QuadPart, &bytesread);
    if (bytesread != stat.cbSize.QuadPart) hr = E_FAIL;
    if (hr != S_OK)
    {
        CoTaskMemFree(data);
        return hr;
    }

    result = calloc(1, sizeof(MetadataItem));
    if (!result)
    {
        CoTaskMemFree(data);
        return E_OUTOFMEMORY;
    }

    PropVariantInit(&result[0].schema);
    PropVariantInit(&result[0].id);
    PropVariantInit(&result[0].value);

    result[0].value.vt = VT_BLOB;
    result[0].value.blob.cbSize = bytesread;
    result[0].value.blob.pBlobData = data;

    MetadataHandler_FreeItems(handler);
    handler->items = result;
    handler->item_count = 1;

    return S_OK;
}

static HRESULT CreateUnknownHandler(MetadataHandler *handler)
{
    MetadataItem *item;

    if (!(item = calloc(1, sizeof(*item))))
        return E_OUTOFMEMORY;

    PropVariantInit(&item->schema);
    PropVariantInit(&item->id);
    PropVariantInit(&item->value);
    item->value.vt = VT_BLOB;

    handler->items = item;
    handler->item_count = 1;

    return S_OK;
}

static const MetadataHandlerVtbl UnknownMetadataReader_Vtbl =
{
    .clsid = &CLSID_WICUnknownMetadataReader,
    .fnLoad = LoadUnknownMetadata,
    .fnCreate = CreateUnknownHandler,
};

HRESULT UnknownMetadataReader_CreateInstance(REFIID iid, void** ppv)
{
    return MetadataReader_Create(&UnknownMetadataReader_Vtbl, iid, ppv);
}

static HRESULT PrepareUnknownValue(const PROPVARIANT *id, const PROPVARIANT *value, PROPVARIANT *normalized_id)
{
    if (id->vt != VT_EMPTY || value->vt != VT_BLOB)
        return E_INVALIDARG;
    return S_OK;
}

static const MetadataHandlerVtbl UnknownMetadataWriter_Vtbl =
{
    .flags = METADATAHANDLER_IS_WRITER | METADATAHANDLER_FIXED_ITEMS,
    .clsid = &CLSID_WICUnknownMetadataWriter,
    .fnLoad = LoadUnknownMetadata,
    .fnCreate = CreateUnknownHandler,
    .fnPrepareValue = PrepareUnknownValue,
};

HRESULT UnknownMetadataWriter_CreateInstance(REFIID iid, void** ppv)
{
    return MetadataReader_Create(&UnknownMetadataWriter_Vtbl, iid, ppv);
}

#ifdef __REACTOS__
static HRESULT LoadJpegQuantizationTable(MetadataHandler *handler, IStream *input,
        const GUID *vendor, DWORD options)
{
    static const WCHAR name[] = L"TableEntry";
    USHORT table[64];
    MetadataItem *item;
    ULONG size;
    HRESULT hr;

    hr = IStream_Read(input, table, sizeof(table), &size);
    if (FAILED(hr))
        return hr;

    MetadataHandler_FreeItems(handler);
    if (size != sizeof(table))
        return S_OK;

    if (!(item = calloc(1, sizeof(*item))))
        return E_OUTOFMEMORY;

    item->id.vt = VT_LPWSTR;
    item->id.pwszVal = CoTaskMemAlloc(sizeof(name));
    item->value.vt = VT_VECTOR | VT_UI2;
    item->value.caui.cElems = ARRAY_SIZE(table);
    item->value.caui.pElems = CoTaskMemAlloc(sizeof(table));
    if (!item->id.pwszVal || !item->value.caui.pElems)
    {
        clear_metadata_item(item);
        free(item);
        return E_OUTOFMEMORY;
    }

    memcpy(item->id.pwszVal, name, sizeof(name));
    memcpy(item->value.caui.pElems, table, sizeof(table));
    handler->items = item;
    handler->item_count = 1;
    return S_OK;
}

static const MetadataHandlerVtbl JpegLuminanceReader_Vtbl =
{
    .clsid = &CLSID_WICJpegLuminanceMetadataReader,
    .fnLoad = LoadJpegQuantizationTable,
    .empty_value_error = WINCODEC_ERR_STREAMREAD,
    .invalid_index_error = WINCODEC_ERR_PROPERTYNOTFOUND,
};

HRESULT JpegLuminanceReader_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&JpegLuminanceReader_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl JpegChrominanceReader_Vtbl =
{
    .clsid = &CLSID_WICJpegChrominanceMetadataReader,
    .fnLoad = LoadJpegQuantizationTable,
    .empty_value_error = WINCODEC_ERR_STREAMREAD,
    .invalid_index_error = WINCODEC_ERR_PROPERTYNOTFOUND,
};

HRESULT JpegChrominanceReader_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&JpegChrominanceReader_Vtbl, iid, ppv);
}

#endif
#define SWAP_USHORT(x) do { if (!native_byte_order) (x) = RtlUshortByteSwap(x); } while(0)
#define SWAP_ULONG(x) do { if (!native_byte_order) (x) = RtlUlongByteSwap(x); } while(0)
#define SWAP_ULONGLONG(x) do { if (!native_byte_order) (x) = RtlUlonglongByteSwap(x); } while(0)

struct IFD_entry
{
    SHORT id;
    SHORT type;
    ULONG count;
    LONG  value;
};

#define IFD_BYTE 1
#define IFD_ASCII 2
#define IFD_SHORT 3
#define IFD_LONG 4
#define IFD_RATIONAL 5
#define IFD_SBYTE 6
#define IFD_UNDEFINED 7
#define IFD_SSHORT 8
#define IFD_SLONG 9
#define IFD_SRATIONAL 10
#define IFD_FLOAT 11
#define IFD_DOUBLE 12
#define IFD_IFD 13

enum ifd_tags
{
    IFD_EXIF_TAG = 0x8769,
    IFD_GPS_TAG = 0x8825,
};

static int tag_to_vt(SHORT tag)
{
    static const int tag2vt[] =
    {
        VT_EMPTY, /* 0 */
        VT_UI1,   /* IFD_BYTE 1 */
        VT_LPSTR, /* IFD_ASCII 2 */
        VT_UI2,   /* IFD_SHORT 3 */
        VT_UI4,   /* IFD_LONG 4 */
        VT_UI8,   /* IFD_RATIONAL 5 */
        VT_I1,    /* IFD_SBYTE 6 */
        VT_BLOB,  /* IFD_UNDEFINED 7 */
        VT_I2,    /* IFD_SSHORT 8 */
        VT_I4,    /* IFD_SLONG 9 */
        VT_I8,    /* IFD_SRATIONAL 10 */
        VT_R4,    /* IFD_FLOAT 11 */
        VT_R8,    /* IFD_DOUBLE 12 */
        VT_BLOB,  /* IFD_IFD 13 */
    };
    return (tag > 0 && tag <= 13) ? tag2vt[tag] : VT_BLOB;
}

HRESULT create_stream_wrapper(IStream *input, ULONG offset, IStream **wrapper)
{
    ULARGE_INTEGER start, maxsize;
    IWICStream *wic_stream = NULL;
    HRESULT hr;

    *wrapper = NULL;

    start.QuadPart = offset;
    maxsize.QuadPart = ~0u;

    hr = StreamImpl_Create(&wic_stream);
    if (SUCCEEDED(hr))
        hr = IWICStream_InitializeFromIStreamRegion(wic_stream, input, start, maxsize);

    if (SUCCEEDED(hr))
        hr = IWICStream_QueryInterface(wic_stream, &IID_IStream, (void **)wrapper);
    if (wic_stream)
        IWICStream_Release(wic_stream);

    return hr;
}

static HRESULT create_metadata_handler(IStream *stream, const GUID *format, const GUID *vendor,
        DWORD options, bool is_writer, IWICMetadataReader **handler)
{
    IWICPersistStream *persist_stream = NULL;
    IWICMetadataReader *reader = NULL;
    HRESULT hr;

    if (is_writer)
        hr = create_metadata_writer(format, vendor, options | WICMetadataCreationFailUnknown,
                (IWICMetadataWriter **)&reader);
    else
        hr = create_metadata_reader(format, vendor, options | WICMetadataCreationFailUnknown,
                  NULL, &reader);

    if (SUCCEEDED(hr))
        hr = IWICMetadataReader_QueryInterface(reader, &IID_IWICPersistStream, (void **)&persist_stream);

    if (SUCCEEDED(hr))
        hr = IWICPersistStream_LoadEx(persist_stream, stream, vendor, options);

    if (persist_stream)
        IWICPersistStream_Release(persist_stream);

    if (SUCCEEDED(hr))
    {
        *handler = reader;
        IWICMetadataReader_AddRef(*handler);
    }

    if (reader)
        IWICMetadataReader_Release(reader);

    return hr;
}

static HRESULT load_IFD_entry(IStream *input, const GUID *vendor, DWORD options, const struct IFD_entry *entry,
        MetadataItem *item, bool resolve_pointer_tags, bool is_writer)
{
    BOOL native_byte_order = !(options & WICPersistOptionBigEndian);
    ULONG count, value, i, bytesread;
    IStream *sub_stream;
    SHORT type;
    LARGE_INTEGER pos;
    HRESULT hr = S_OK;

    item->schema.vt = VT_EMPTY;
    item->id.vt = VT_UI2;
    item->id.uiVal = entry->id;
    SWAP_USHORT(item->id.uiVal);

    count = entry->count;
    SWAP_ULONG(count);
    type = entry->type;
    SWAP_USHORT(type);
    item->value.vt = tag_to_vt(type);
    value = entry->value;
    SWAP_ULONG(value);

    switch (type)
    {
     case IFD_BYTE:
     case IFD_SBYTE:
        if (!count) count = 1;

        if (count <= 4)
        {
            const BYTE *data = (const BYTE *)&entry->value;

            if (count == 1)
                item->value.bVal = data[0];
            else
            {
                item->value.vt |= VT_VECTOR;
                item->value.caub.cElems = count;
                item->value.caub.pElems = CoTaskMemAlloc(count);
                memcpy(item->value.caub.pElems, data, count);
            }
            break;
        }

        item->value.vt |= VT_VECTOR;
        item->value.caub.cElems = count;
        item->value.caub.pElems = CoTaskMemAlloc(count);
        if (!item->value.caub.pElems) return E_OUTOFMEMORY;

        pos.QuadPart = value;
        hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
        {
            CoTaskMemFree(item->value.caub.pElems);
            return hr;
        }
        hr = IStream_Read(input, item->value.caub.pElems, count, &bytesread);
        if (bytesread != count) hr = E_FAIL;
        if (hr != S_OK)
        {
            CoTaskMemFree(item->value.caub.pElems);
            return hr;
        }
        break;
    case IFD_SHORT:
    case IFD_SSHORT:
        if (!count) count = 1;

        if (count <= 2)
        {
            const SHORT *data = (const SHORT *)&entry->value;

            if (count == 1)
            {
                item->value.uiVal = data[0];
                SWAP_USHORT(item->value.uiVal);
            }
            else
            {
                item->value.vt |= VT_VECTOR;
                item->value.caui.cElems = count;
                item->value.caui.pElems = CoTaskMemAlloc(count * 2);
                memcpy(item->value.caui.pElems, data, count * 2);
                for (i = 0; i < count; i++)
                    SWAP_USHORT(item->value.caui.pElems[i]);
            }
            break;
        }

        item->value.vt |= VT_VECTOR;
        item->value.caui.cElems = count;
        item->value.caui.pElems = CoTaskMemAlloc(count * 2);
        if (!item->value.caui.pElems) return E_OUTOFMEMORY;

        pos.QuadPart = value;
        hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
        {
            CoTaskMemFree(item->value.caui.pElems);
            return hr;
        }
        hr = IStream_Read(input, item->value.caui.pElems, count * 2, &bytesread);
        if (bytesread != count * 2) hr = E_FAIL;
        if (hr != S_OK)
        {
            CoTaskMemFree(item->value.caui.pElems);
            return hr;
        }
        for (i = 0; i < count; i++)
            SWAP_USHORT(item->value.caui.pElems[i]);
        break;
    case IFD_LONG:
    case IFD_SLONG:
    case IFD_FLOAT:
        if (!count) count = 1;

        if (count == 1)
        {
            item->value.ulVal = value;
            break;
        }

        item->value.vt |= VT_VECTOR;
        item->value.caul.cElems = count;
        item->value.caul.pElems = CoTaskMemAlloc(count * 4);
        if (!item->value.caul.pElems) return E_OUTOFMEMORY;

        pos.QuadPart = value;
        hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
        {
            CoTaskMemFree(item->value.caul.pElems);
            return hr;
        }
        hr = IStream_Read(input, item->value.caul.pElems, count * 4, &bytesread);
        if (bytesread != count * 4) hr = E_FAIL;
        if (hr != S_OK)
        {
            CoTaskMemFree(item->value.caul.pElems);
            return hr;
        }
        for (i = 0; i < count; i++)
            SWAP_ULONG(item->value.caul.pElems[i]);
        break;
    case IFD_RATIONAL:
    case IFD_SRATIONAL:
    case IFD_DOUBLE:
        if (!count)
        {
            FIXME("IFD field type %d, count 0\n", type);
            item->value.vt = VT_EMPTY;
            break;
        }

        if (count == 1)
        {
            ULONGLONG ull;

            pos.QuadPart = value;
            hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
            if (FAILED(hr)) return hr;

            hr = IStream_Read(input, &ull, sizeof(ull), &bytesread);
            if (bytesread != sizeof(ull)) hr = E_FAIL;
            if (hr != S_OK) return hr;

            item->value.uhVal.QuadPart = ull;

            if (type == IFD_DOUBLE)
                SWAP_ULONGLONG(item->value.uhVal.QuadPart);
            else
            {
                SWAP_ULONG(item->value.uhVal.LowPart);
                SWAP_ULONG(item->value.uhVal.HighPart);
            }
            break;
        }
        else
        {
            item->value.vt |= VT_VECTOR;
            item->value.cauh.cElems = count;
            item->value.cauh.pElems = CoTaskMemAlloc(count * 8);
            if (!item->value.cauh.pElems) return E_OUTOFMEMORY;

            pos.QuadPart = value;
            hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
            if (FAILED(hr))
            {
                CoTaskMemFree(item->value.cauh.pElems);
                return hr;
            }
            hr = IStream_Read(input, item->value.cauh.pElems, count * 8, &bytesread);
            if (bytesread != count * 8) hr = E_FAIL;
            if (hr != S_OK)
            {
                CoTaskMemFree(item->value.cauh.pElems);
                return hr;
            }
            for (i = 0; i < count; i++)
            {
                if (type == IFD_DOUBLE)
                    SWAP_ULONGLONG(item->value.cauh.pElems[i].QuadPart);
                else
                {
                    SWAP_ULONG(item->value.cauh.pElems[i].LowPart);
                    SWAP_ULONG(item->value.cauh.pElems[i].HighPart);
                }
            }
        }
        break;
    case IFD_ASCII:
        item->value.pszVal = CoTaskMemAlloc(count + 1);
        if (!item->value.pszVal) return E_OUTOFMEMORY;

        if (count <= 4)
        {
            const char *data = (const char *)&entry->value;
            memcpy(item->value.pszVal, data, count);
            item->value.pszVal[count] = 0;
            break;
        }

        pos.QuadPart = value;
        hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
        {
            CoTaskMemFree(item->value.pszVal);
            return hr;
        }
        hr = IStream_Read(input, item->value.pszVal, count, &bytesread);
        if (bytesread != count) hr = E_FAIL;
        if (hr != S_OK)
        {
            CoTaskMemFree(item->value.pszVal);
            return hr;
        }
        item->value.pszVal[count] = 0;
        break;
    case IFD_UNDEFINED:
        if (!count)
        {
            FIXME("IFD field type %d, count 0\n", type);
            item->value.vt = VT_EMPTY;
            break;
        }

        item->value.blob.pBlobData = CoTaskMemAlloc(count);
        if (!item->value.blob.pBlobData) return E_OUTOFMEMORY;

        item->value.blob.cbSize = count;

        if (count <= 4)
        {
            const char *data = (const char *)&entry->value;
            memcpy(item->value.blob.pBlobData, data, count);
            break;
        }

        pos.QuadPart = value;
        hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
        {
            CoTaskMemFree(item->value.blob.pBlobData);
            return hr;
        }
        hr = IStream_Read(input, item->value.blob.pBlobData, count, &bytesread);
        if (bytesread != count) hr = E_FAIL;
        if (hr != S_OK)
        {
            CoTaskMemFree(item->value.blob.pBlobData);
            return hr;
        }
        break;
    default:
        FIXME("loading field of type %d, count %lu is not implemented\n", type, count);
        break;
    }

    switch (item->id.uiVal)
    {
        case IFD_EXIF_TAG:
        case IFD_GPS_TAG:
        {
           IWICMetadataReader *sub_reader = NULL;

           if (!resolve_pointer_tags)
               break;

           if (item->value.vt != VT_UI4)
               break;

           hr = create_stream_wrapper(input, 0, &sub_stream);

           pos.QuadPart = item->value.ulVal;
           if (SUCCEEDED(hr))
               hr = IStream_Seek(sub_stream, pos, STREAM_SEEK_SET, NULL);

           if (SUCCEEDED(hr))
           {
               const GUID *format = item->id.uiVal == IFD_EXIF_TAG ? &GUID_MetadataFormatExif : &GUID_MetadataFormatGps;

               hr = create_metadata_handler(sub_stream, format, vendor, options | WICMetadataCreationFailUnknown,
                       is_writer, &sub_reader);
           }

           if (SUCCEEDED(hr))
           {
               item->value.vt = VT_UNKNOWN;
               item->value.punkVal = (IUnknown *)sub_reader;
           }

           if (sub_stream)
               IStream_Release(sub_stream);

           break;
        }
        default:
           break;
    }

    return hr;
}

static HRESULT load_ifd_metadata_internal(MetadataHandler *handler, IStream *input, const GUID *vendor,
    DWORD persist_options, bool resolve_pointer_tags, bool is_writer)
{
    HRESULT hr;
    MetadataItem *result;
    USHORT count, i;
    struct IFD_entry *entry;
    BOOL native_byte_order;
    ULONG bytesread;

    TRACE("\n");

    native_byte_order = !(persist_options & WICPersistOptionBigEndian);

    hr = IStream_Read(input, &count, sizeof(count), &bytesread);
    if (bytesread != sizeof(count)) hr = E_FAIL;
    if (hr != S_OK) return hr;

    SWAP_USHORT(count);

    entry = malloc(count * sizeof(*entry));
    if (!entry) return E_OUTOFMEMORY;

    hr = IStream_Read(input, entry, count * sizeof(*entry), &bytesread);
    if (bytesread != count * sizeof(*entry)) hr = E_FAIL;
    if (hr != S_OK)
    {
        free(entry);
        return hr;
    }

    /* limit number of IFDs to 4096 to avoid infinite loop */
    for (i = 0; i < 4096; i++)
    {
        ULONG next_ifd_offset;
        LARGE_INTEGER pos;
        USHORT next_ifd_count;

        hr = IStream_Read(input, &next_ifd_offset, sizeof(next_ifd_offset), &bytesread);
        if (bytesread != sizeof(next_ifd_offset)) hr = E_FAIL;
        if (hr != S_OK) break;

        SWAP_ULONG(next_ifd_offset);
        if (!next_ifd_offset) break;

        pos.QuadPart = next_ifd_offset;
        hr = IStream_Seek(input, pos, STREAM_SEEK_SET, NULL);
        if (FAILED(hr)) break;

        hr = IStream_Read(input, &next_ifd_count, sizeof(next_ifd_count), &bytesread);
        if (bytesread != sizeof(next_ifd_count)) hr = E_FAIL;
        if (hr != S_OK) break;

        SWAP_USHORT(next_ifd_count);

        pos.QuadPart = next_ifd_count * sizeof(*entry);
        hr = IStream_Seek(input, pos, STREAM_SEEK_CUR, NULL);
        if (FAILED(hr)) break;
    }

    if (hr != S_OK || i == 4096)
    {
        free(entry);
        return WINCODEC_ERR_BADMETADATAHEADER;
    }

    result = calloc(count, sizeof(*result));
    if (!result)
    {
        free(entry);
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < count; i++)
    {
        hr = load_IFD_entry(input, vendor, persist_options, &entry[i], &result[i], resolve_pointer_tags, is_writer);
        if (FAILED(hr))
        {
            free(entry);
            free(result);
            return hr;
        }
    }

    free(entry);

    MetadataHandler_FreeItems(handler);
    handler->items = result;
    handler->item_count = count;

    return S_OK;
}

struct serialized_ifd_entry
{
    USHORT tag, type;
    ULONG count, width;
    const BYTE *value;
};

static void store_ifd_integer(BYTE *dest, ULONGLONG value, unsigned int width, BOOL big_endian)
{
    unsigned int i;

    for (i = 0; i < width; ++i)
        dest[big_endian ? width - i - 1 : i] = value >> (i * 8);
}

static HRESULT prepare_ifd_entry(const MetadataItem *item, struct serialized_ifd_entry *entry)
{
    const PROPVARIANT *value = &item->value;
    PROPVARIANT id;
    BOOL vector = !!(value->vt & VT_VECTOR);
    HRESULT hr;

    PropVariantInit(&id);
    hr = PropVariantChangeType(&id, &item->id, 0, VT_UI2);
    if (FAILED(hr)) return WINCODEC_ERR_UNEXPECTEDMETADATATYPE;
    entry->tag = id.uiVal;
    PropVariantClear(&id);
    entry->count = 1;
    switch (value->vt & ~VT_VECTOR)
    {
#define IFD_VALUE(vt, field, array, format, bytes) \
    case vt: \
        entry->type = format; \
        entry->width = bytes; \
        if (vector) { entry->count = value->array.cElems; entry->value = (const BYTE *)value->array.pElems; } \
        else entry->value = (const BYTE *)&value->field; \
        break
        IFD_VALUE(VT_UI1, bVal, caub, IFD_BYTE, 1);
        IFD_VALUE(VT_I1, cVal, cac, IFD_SBYTE, 1);
        IFD_VALUE(VT_UI2, uiVal, caui, IFD_SHORT, 2);
        IFD_VALUE(VT_I2, iVal, cai, IFD_SSHORT, 2);
        IFD_VALUE(VT_UI4, ulVal, caul, IFD_LONG, 4);
        IFD_VALUE(VT_I4, lVal, cal, IFD_SLONG, 4);
        IFD_VALUE(VT_R4, fltVal, caflt, IFD_FLOAT, 4);
        IFD_VALUE(VT_R8, dblVal, cadbl, IFD_DOUBLE, 8);
        IFD_VALUE(VT_UI8, uhVal, cauh, IFD_RATIONAL, 8);
        IFD_VALUE(VT_I8, hVal, cah, IFD_SRATIONAL, 8);
#undef IFD_VALUE
    case VT_LPSTR:
        if (vector || !value->pszVal) return WINCODEC_ERR_UNEXPECTEDMETADATATYPE;
        if (strlen(value->pszVal) >= MAXDWORD) return WINCODEC_ERR_TOOMUCHMETADATA;
        entry->type = IFD_ASCII;
        entry->width = 1;
        entry->count = strlen(value->pszVal) + 1;
        entry->value = (const BYTE *)value->pszVal;
        break;
    case VT_BLOB:
        if (vector) return WINCODEC_ERR_UNEXPECTEDMETADATATYPE;
        entry->type = IFD_UNDEFINED;
        entry->width = 1;
        entry->count = value->blob.cbSize;
        entry->value = value->blob.pBlobData;
        break;
    default:
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    if (entry->count && !entry->value) return E_INVALIDARG;
    if (entry->count > MAXDWORD / entry->width) return WINCODEC_ERR_TOOMUCHMETADATA;
    return S_OK;
}

static int compare_ifd_entries(const void *left, const void *right)
{
    const struct serialized_ifd_entry *a = left, *b = right;
    return (a->tag > b->tag) - (a->tag < b->tag);
}

static HRESULT SerializeIfdMetadata(const MetadataItem *items, DWORD count, DWORD options, ULONGLONG offset,
        BOOL clear_dirty, BYTE **data, ULONG *size)
{
    struct serialized_ifd_entry *entries;
    BOOL big_endian = !!(options & WICPersistOptionBigEndian);
    ULONGLONG total;
    ULONG i, j, cursor, length;
    BYTE *buffer, *dest, *record;
    HRESULT hr = S_OK;

    if (count > 0xffff || offset > MAXDWORD)
        return WINCODEC_ERR_TOOMUCHMETADATA;
    entries = calloc(count ? count : 1, sizeof(*entries));
    if (!entries) return E_OUTOFMEMORY;
    total = 6 + 12 * count;
    for (i = 0; i < count; ++i)
    {
        hr = prepare_ifd_entry(&items[i], &entries[i]);
        if (FAILED(hr)) break;
        length = entries[i].count * entries[i].width;
        if (length > 4) total += (ULONGLONG)length + (length & 1);
        if (total + offset > MAXDWORD)
        {
            hr = WINCODEC_ERR_TOOMUCHMETADATA;
            break;
        }
    }
    if (SUCCEEDED(hr) && total + offset > MAXDWORD) hr = WINCODEC_ERR_TOOMUCHMETADATA;
    if (FAILED(hr))
    {
        free(entries);
        return hr;
    }
    qsort(entries, count, sizeof(*entries), compare_ifd_entries);
    for (i = 1; i < count; ++i)
    {
        if (entries[i - 1].tag == entries[i].tag)
        {
            free(entries);
            return WINCODEC_ERR_BADMETADATAHEADER;
        }
    }
    if (!(buffer = calloc(1, total)))
    {
        free(entries);
        return E_OUTOFMEMORY;
    }
    store_ifd_integer(buffer, count, 2, big_endian);
    cursor = 6 + 12 * count;
    for (i = 0; i < count; ++i)
    {
        record = buffer + 2 + 12 * i;
        store_ifd_integer(record, entries[i].tag, 2, big_endian);
        store_ifd_integer(record + 2, entries[i].type, 2, big_endian);
        store_ifd_integer(record + 4, entries[i].count, 4, big_endian);
        length = entries[i].count * entries[i].width;
        dest = record + 8;
        if (length > 4)
        {
            store_ifd_integer(dest, offset + cursor, 4, big_endian);
            dest = buffer + cursor;
            cursor += length + (length & 1);
        }
        for (j = 0; j < entries[i].count; ++j)
        {
            const BYTE *source = entries[i].value + j * entries[i].width;
            USHORT short_value;
            ULONG long_value;
            ULONGLONG longlong_value;

            switch (entries[i].width)
            {
            case 1:
                *dest = *source;
                break;
            case 2:
                memcpy(&short_value, source, 2);
                store_ifd_integer(dest, short_value, 2, big_endian);
                break;
            case 4:
                memcpy(&long_value, source, 4);
                store_ifd_integer(dest, long_value, 4, big_endian);
                break;
            case 8:
                memcpy(&longlong_value, source, 8);
                if (entries[i].type == IFD_DOUBLE)
                    store_ifd_integer(dest, longlong_value, 8, big_endian);
                else
                {
                    store_ifd_integer(dest, (ULONG)longlong_value, 4, big_endian);
                    store_ifd_integer(dest + 4, longlong_value >> 32, 4, big_endian);
                }
                break;
            }
            dest += entries[i].width;
        }
    }
    free(entries);
    *data = buffer;
    *size = total;
    return S_OK;
}

static HRESULT LoadIfdMetadataReader(MetadataHandler *handler, IStream *input, const GUID *vendor,
        DWORD options)
{
    TRACE("%p, %#lx.\n", input, options);
    return load_ifd_metadata_internal(handler, input, vendor, options, true, false);
}

static HRESULT LoadIfdMetadataWriter(MetadataHandler *handler, IStream *input, const GUID *vendor,
        DWORD options)
{
    TRACE("%p, %#lx.\n", input, options);
    return load_ifd_metadata_internal(handler, input, vendor, options, true, true);
}

static HRESULT LoadExifMetadataReader(MetadataHandler *handler, IStream *input, const GUID *vendor,
        DWORD options)
{
    TRACE("%p, %#lx.\n", input, options);

    return load_ifd_metadata_internal(handler, input, vendor, options, false, false);
}

static HRESULT LoadExifMetadataWriter(MetadataHandler *handler, IStream *input, const GUID *vendor,
        DWORD options)
{
    TRACE("%p, %#lx.\n", input, options);

    return load_ifd_metadata_internal(handler, input, vendor, options, false, true);
}

static HRESULT LoadGpsMetadataReader(MetadataHandler *handler, IStream *input, const GUID *vendor,
        DWORD options)
{
    TRACE("%p, %#lx.\n", input, options);

    return load_ifd_metadata_internal(handler, input, vendor, options, false, false);
}

static HRESULT LoadGpsMetadataWriter(MetadataHandler *handler, IStream *input, const GUID *vendor,
        DWORD options)
{
    TRACE("%p, %#lx.\n", input, options);

    return load_ifd_metadata_internal(handler, input, vendor, options, false, true);
}

static HRESULT load_app1_metadata_internal(MetadataHandler *handler, IStream *input, const GUID *vendor, DWORD options,
        bool is_writer)
{
    static const char exif_header[] = {'E','x','i','f',0,0};
    IWICMetadataReader *ifd_reader = NULL;
    BOOL native_byte_order;
    MetadataItem *result;
    LARGE_INTEGER move;

#pragma pack(push,2)
    struct app1_header
    {
        BYTE exif_header[6];
        BYTE bom[2];
        USHORT marker;
        ULONG ifd0_offset;
    } header;
#pragma pack(pop)

    IStream *ifd_stream;
    ULONG length;
    HRESULT hr;

    if (FAILED(hr = IStream_Read(input, &header, sizeof(header), &length)))
        return hr;
    if (length != sizeof(header))
        return WINCODEC_ERR_BADMETADATAHEADER;

    if (memcmp(header.exif_header, exif_header, sizeof(exif_header)))
        return WINCODEC_ERR_BADMETADATAHEADER;

    options &= ~(WICPersistOptionLittleEndian | WICPersistOptionBigEndian);
    options |= WICMetadataCreationFailUnknown;
    if (!memcmp(header.bom, "II", 2))
        options |= WICPersistOptionLittleEndian;
    else if (!memcmp(header.bom, "MM", 2))
        options |= WICPersistOptionBigEndian;
    else
    {
        WARN("Unrecognized bom marker %#x%#x.\n", header.bom[0], header.bom[1]);
        return WINCODEC_ERR_BADMETADATAHEADER;
    }
    native_byte_order = !(options & WICPersistOptionBigEndian);

    SWAP_USHORT(header.marker);
    SWAP_ULONG(header.ifd0_offset);

    if (header.marker != 0x002a)
    {
        WARN("Unrecognized marker %#x.\n", header.marker);
        return WINCODEC_ERR_BADMETADATAHEADER;
    }

    if (FAILED(hr = create_stream_wrapper(input, sizeof(exif_header), &ifd_stream)))
        return hr;
    move.QuadPart = header.ifd0_offset;
    if (FAILED(hr = IStream_Seek(ifd_stream, move, STREAM_SEEK_SET, NULL)))
    {
        IStream_Release(ifd_stream);
        return hr;
    }

    if (SUCCEEDED(hr))
        hr = create_metadata_handler(ifd_stream, &GUID_MetadataFormatIfd, vendor, options, is_writer, &ifd_reader);

    IStream_Release(ifd_stream);

    if (FAILED(hr))
    {
        WARN("Failed to create IFD0 reader.\n");
        return hr;
    }

    if (!(result = calloc(1, sizeof(*result))))
    {
        IWICMetadataReader_Release(ifd_reader);
        return E_OUTOFMEMORY;
    }

    result->id.vt = VT_UI2;
    result->id.uiVal = 0;
    result->value.vt = VT_UNKNOWN;
    result->value.punkVal = (IUnknown *)ifd_reader;

    MetadataHandler_FreeItems(handler);
    handler->items = result;
    handler->item_count = 1;

    return S_OK;
}

static HRESULT LoadApp1MetadataReader(MetadataHandler *handler, IStream *input, const GUID *vendor, DWORD options)
{
    return load_app1_metadata_internal(handler, input, vendor, options, false);
}

static HRESULT LoadApp1MetadataWriter(MetadataHandler *handler, IStream *input, const GUID *vendor, DWORD options)
{
    return load_app1_metadata_internal(handler, input, vendor, options, true);
}

static const MetadataHandlerVtbl IfdMetadataReader_Vtbl = {
    0,
    &CLSID_WICIfdMetadataReader,
    LoadIfdMetadataReader
};

HRESULT IfdMetadataReader_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&IfdMetadataReader_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl IfdMetadataWriter_Vtbl =
{
    .flags = METADATAHANDLER_IS_WRITER | METADATAHANDLER_ABSOLUTE_OFFSETS,
    &CLSID_WICIfdMetadataWriter,
    LoadIfdMetadataWriter,
    .fnSerialize = SerializeIfdMetadata
};

HRESULT IfdMetadataWriter_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&IfdMetadataWriter_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl GpsMetadataReader_Vtbl =
{
    0,
    &CLSID_WICGpsMetadataReader,
    LoadGpsMetadataReader
};

HRESULT GpsMetadataReader_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&GpsMetadataReader_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl GpsMetadataWriter_Vtbl =
{
    .flags = METADATAHANDLER_IS_WRITER | METADATAHANDLER_ABSOLUTE_OFFSETS,
    &CLSID_WICGpsMetadataWriter,
    LoadGpsMetadataWriter,
    .fnSerialize = SerializeIfdMetadata
};

HRESULT GpsMetadataWriter_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&GpsMetadataWriter_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl ExifMetadataReader_Vtbl =
{
    0,
    &CLSID_WICExifMetadataReader,
    LoadExifMetadataReader
};

HRESULT ExifMetadataReader_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&ExifMetadataReader_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl ExifMetadataWriter_Vtbl =
{
    .flags = METADATAHANDLER_IS_WRITER | METADATAHANDLER_ABSOLUTE_OFFSETS,
    &CLSID_WICExifMetadataWriter,
    LoadExifMetadataWriter,
    .fnSerialize = SerializeIfdMetadata
};

HRESULT ExifMetadataWriter_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&ExifMetadataWriter_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl App1MetadataReader_Vtbl =
{
    0,
    &CLSID_WICApp1MetadataReader,
    LoadApp1MetadataReader
};

static HRESULT LoadApp0Metadata(MetadataHandler *handler, IStream *stream,
        const GUID *vendor, DWORD options)
{
    static const VARTYPE types[] = {VT_UI2, VT_UI1, VT_UI2, VT_UI2, VT_UI1, VT_UI1, VT_BLOB};
    BYTE data[14] = {0};
    MetadataItem *items;
    ULONG read, size;
    unsigned int i;
    HRESULT hr;

    if (stream)
    {
        hr = IStream_Read(stream, data, sizeof(data), &read);
        if (FAILED(hr)) return hr;
        if (read != sizeof(data)) return WINCODEC_ERR_STREAMREAD;
        if (memcmp(data, "JFIF", 5)) return WINCODEC_ERR_BADMETADATAHEADER;
    }

    if (!(items = calloc(ARRAY_SIZE(types), sizeof(*items)))) return E_OUTOFMEMORY;
    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        items[i].id.vt = VT_UI2;
        items[i].id.uiVal = i;
        items[i].value.vt = types[i];
    }
    items[0].value.uiVal = (data[5] << 8) | data[6];
    items[1].value.bVal = data[7];
    items[2].value.uiVal = (data[8] << 8) | data[9];
    items[3].value.uiVal = (data[10] << 8) | data[11];
    items[4].value.bVal = data[12];
    items[5].value.bVal = data[13];
    size = data[12] * data[13] * 3;
    if (size)
    {
        items[6].value.blob.cbSize = size;
        items[6].value.blob.pBlobData = CoTaskMemAlloc(size);
        if (!items[6].value.blob.pBlobData)
            hr = E_OUTOFMEMORY;
        else
        {
            hr = IStream_Read(stream, items[6].value.blob.pBlobData, size, &read);
            if (SUCCEEDED(hr) && read != size) hr = WINCODEC_ERR_STREAMREAD;
        }
        if (FAILED(hr))
        {
            for (i = 0; i < ARRAY_SIZE(types); ++i) clear_metadata_item(&items[i]);
            free(items);
            return hr;
        }
    }

    MetadataHandler_FreeItems(handler);
    handler->items = items;
    handler->item_count = ARRAY_SIZE(types);
    return S_OK;
}

static HRESULT CreateApp0Handler(MetadataHandler *handler)
{
    return LoadApp0Metadata(handler, NULL, NULL, 0);
}

static HRESULT SerializeApp0Metadata(const MetadataItem *items, DWORD count, DWORD options, ULONGLONG offset,
        BOOL clear_dirty, BYTE **data, ULONG *size)
{
    static const VARTYPE types[] = {VT_UI2, VT_UI1, VT_UI2, VT_UI2, VT_UI1, VT_UI1, VT_BLOB};
    const PROPVARIANT *values[ARRAY_SIZE(types)];
    const MetadataItem *item;
    PROPVARIANT id;
    ULONG thumbnail_size;
    unsigned int i, j;
    BYTE *buffer;

    if (count != ARRAY_SIZE(types)) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    id.vt = VT_UI2;
    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        id.uiVal = i;
        item = NULL;
        for (j = 0; j < count; ++j)
            if (!PropVariantCompareEx(&items[j].id, &id, 0, PVCF_USESTRCMPI))
            {
                item = &items[j];
                break;
            }
        if (!item) return WINCODEC_ERR_PROPERTYNOTFOUND;
        if (item->value.vt != types[i]) return WINCODEC_ERR_UNEXPECTEDMETADATATYPE;
        values[i] = &item->value;
    }
    thumbnail_size = values[4]->bVal * values[5]->bVal * 3;
    if (thumbnail_size != values[6]->blob.cbSize ||
        (thumbnail_size && !values[6]->blob.pBlobData))
        return WINCODEC_ERR_BADMETADATAHEADER;
    if (thumbnail_size > 0xffff - 16) return WINCODEC_ERR_TOOMUCHMETADATA;
    if (!(buffer = malloc(14 + thumbnail_size))) return E_OUTOFMEMORY;
    memcpy(buffer, "JFIF", 5);
    buffer[5] = values[0]->uiVal >> 8;
    buffer[6] = values[0]->uiVal;
    buffer[7] = values[1]->bVal;
    buffer[8] = values[2]->uiVal >> 8;
    buffer[9] = values[2]->uiVal;
    buffer[10] = values[3]->uiVal >> 8;
    buffer[11] = values[3]->uiVal;
    buffer[12] = values[4]->bVal;
    buffer[13] = values[5]->bVal;
    if (thumbnail_size) memcpy(buffer + 14, values[6]->blob.pBlobData, thumbnail_size);
    *data = buffer;
    *size = 14 + thumbnail_size;
    return S_OK;
}

static const MetadataHandlerVtbl App0MetadataReader_Vtbl =
{
    .clsid = &CLSID_WICApp0MetadataReader,
    .fnLoad = LoadApp0Metadata,
    .fnCreate = CreateApp0Handler,
};

HRESULT App0MetadataReader_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&App0MetadataReader_Vtbl, iid, ppv);
}

static const MetadataHandlerVtbl App0MetadataWriter_Vtbl =
{
    .flags = METADATAHANDLER_IS_WRITER | METADATAHANDLER_FIXED_ITEMS,
    .clsid = &CLSID_WICApp0MetadataWriter,
    .fnLoad = LoadApp0Metadata,
    .fnCreate = CreateApp0Handler,
    .fnSerialize = SerializeApp0Metadata,
};

HRESULT App0MetadataWriter_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&App0MetadataWriter_Vtbl, iid, ppv);
}

HRESULT App1MetadataReader_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&App1MetadataReader_Vtbl, iid, ppv);
}

static HRESULT PrepareApp1Value(const PROPVARIANT *id, const PROPVARIANT *value, PROPVARIANT *normalized_id)
{
    IWICMetadataReader *reader;
    GUID format;
    HRESULT hr;

    if (id->vt == VT_LPSTR || id->vt == VT_LPWSTR || id->vt == VT_BSTR)
        return WINCODEC_ERR_PROPERTYNOTFOUND;
    if (id->vt == VT_R4 || id->vt == VT_R8)
    {
        normalized_id->vt = VT_UI2;
        hr = id->vt == VT_R4 ? VarUI2FromR4(id->fltVal, &normalized_id->uiVal) :
                VarUI2FromR8(id->dblVal, &normalized_id->uiVal);
    }
    else
        hr = PropVariantChangeType(normalized_id, id, 0, VT_UI2);
    if (FAILED(hr))
        return WINCODEC_ERR_UNEXPECTEDMETADATATYPE;
    if (normalized_id->uiVal)
        return (id->vt == VT_R4 || id->vt == VT_R8) ? WINCODEC_ERR_UNEXPECTEDMETADATATYPE : E_INVALIDARG;
    if (value->vt != VT_UNKNOWN || !value->punkVal)
        return E_INVALIDARG;
    hr = IUnknown_QueryInterface(value->punkVal, &IID_IWICMetadataReader, (void **)&reader);
    if (FAILED(hr)) return E_INVALIDARG;
    hr = IWICMetadataReader_GetMetadataFormat(reader, &format);
    IWICMetadataReader_Release(reader);
    if (FAILED(hr)) return hr;
    return IsEqualGUID(&format, &GUID_MetadataFormatIfd) ? S_OK : E_INVALIDARG;
}

static HRESULT SerializeApp1Metadata(const MetadataItem *items, DWORD count, DWORD options,
        ULONGLONG offset, BOOL clear_dirty, BYTE **data, ULONG *size)
{
    IWICPersistStream *persist;
    IStream *stream;
    BYTE header[8], *buffer = NULL;
    BOOL big_endian = !!(options & WICPersistOptionBigEndian);
    LARGE_INTEGER move = {{0}};
    ULARGE_INTEGER end;
    PROPVARIANT id;
    ULONG written, read;
    HRESULT hr;

    if (count != 1) return WINCODEC_ERR_BADMETADATAHEADER;
    PropVariantInit(&id);
    hr = PropVariantChangeType(&id, &items[0].id, 0, VT_UI2);
    if (FAILED(hr)) return WINCODEC_ERR_UNEXPECTEDMETADATATYPE;
    if (id.uiVal || items[0].value.vt != VT_UNKNOWN || !items[0].value.punkVal)
        return WINCODEC_ERR_BADMETADATAHEADER;
    hr = IUnknown_QueryInterface(items[0].value.punkVal, &IID_IWICPersistStream, (void **)&persist);
    if (FAILED(hr)) return hr;
    hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (SUCCEEDED(hr))
    {
        header[0] = header[1] = big_endian ? 'M' : 'I';
        store_ifd_integer(header + 2, 42, 2, big_endian);
        store_ifd_integer(header + 4, 8, 4, big_endian);
        hr = IStream_Write(stream, header, sizeof(header), &written);
        if (SUCCEEDED(hr) && written != sizeof(header)) hr = WINCODEC_ERR_STREAMWRITE;
        if (SUCCEEDED(hr)) hr = IWICPersistStream_SaveEx(persist, stream, options, clear_dirty);
        if (SUCCEEDED(hr)) hr = IStream_Seek(stream, move, STREAM_SEEK_END, &end);
        if (SUCCEEDED(hr) && end.QuadPart > 0xffff - 8) hr = WINCODEC_ERR_TOOMUCHMETADATA;
        if (SUCCEEDED(hr) && !(buffer = malloc(6 + end.QuadPart))) hr = E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
        {
            memcpy(buffer, "Exif\0", 6);
            hr = IStream_Seek(stream, move, STREAM_SEEK_SET, NULL);
            if (SUCCEEDED(hr)) hr = IStream_Read(stream, buffer + 6, end.QuadPart, &read);
            if (SUCCEEDED(hr) && read != end.QuadPart) hr = WINCODEC_ERR_STREAMREAD;
            if (SUCCEEDED(hr))
            {
                *data = buffer;
                *size = 6 + end.QuadPart;
            }
        }
        IStream_Release(stream);
    }
    IWICPersistStream_Release(persist);
    if (FAILED(hr)) free(buffer);
    return hr;
}

static const MetadataHandlerVtbl App1MetadataWriter_Vtbl =
{
    .flags = METADATAHANDLER_IS_WRITER | METADATAHANDLER_NESTED_ITEMS,
    &CLSID_WICApp1MetadataWriter,
    LoadApp1MetadataWriter,
    .fnPrepareValue = PrepareApp1Value,
    .fnSerialize = SerializeApp1Metadata,
};

HRESULT App1MetadataWriter_CreateInstance(REFIID iid, void **ppv)
{
    return MetadataReader_Create(&App1MetadataWriter_Vtbl, iid, ppv);
}
