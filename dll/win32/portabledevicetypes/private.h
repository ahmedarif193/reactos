/*
 * Portable Device Types private declarations
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdarg.h>
#include <string.h>

#define COBJMACROS

#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <propsys.h>
#include <propvarutil.h>
#include <portabledevicetypes.h>

#include <wine/debug.h>

HRESULT portable_device_values_create(REFIID iid, void **out);
HRESULT portable_device_key_collection_create(REFIID iid, void **out);
HRESULT portable_device_propvariant_collection_create(REFIID iid, void **out);
HRESULT portable_device_values_collection_create(REFIID iid, void **out);

static inline BOOL property_key_equal(REFPROPERTYKEY left, REFPROPERTYKEY right)
{
    return left->pid == right->pid && IsEqualGUID(&left->fmtid, &right->fmtid);
}

static inline void *heap_realloc(void *memory, SIZE_T size)
{
    if (memory)
        return HeapReAlloc(GetProcessHeap(), 0, memory, size);
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static inline HRESULT portable_propvariant_copy(PROPVARIANT *destination, const PROPVARIANT *source)
{
    BYTE *buffer;

    if (source->vt != (VT_VECTOR | VT_UI1))
        return PropVariantCopy(destination, source);
    if (!source->caub.pElems)
        return E_INVALIDARG;
    if (source->caub.cElems)
        return PropVariantCopy(destination, source);

    buffer = CoTaskMemAlloc(1);
    if (!buffer)
        return E_OUTOFMEMORY;
    destination->vt = VT_VECTOR | VT_UI1;
    destination->caub.cElems = 0;
    destination->caub.pElems = buffer;
    return S_OK;
}

static inline HRESULT portable_propvariant_change_type(PROPVARIANT *destination, const PROPVARIANT *source, VARTYPE type)
{
    HRESULT hr = PropVariantChangeType(destination, source, 0, type);

    if (hr == DISP_E_TYPEMISMATCH)
        hr = TYPE_E_TYPEMISMATCH;
    return hr;
}
