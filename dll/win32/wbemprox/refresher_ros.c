/*
 * WBEM refresher
 *
 * Copyright 2026 Ahmed Jarraya
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

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "wbemcli.h"

#include "wbemprox_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wbemprox);

struct wbem_refresher
{
    IWbemRefresher IWbemRefresher_iface;
    IWbemConfigureRefresher IWbemConfigureRefresher_iface;
    LONG refs;
};

static inline struct wbem_refresher *impl_from_IWbemRefresher(IWbemRefresher *iface)
{
    return CONTAINING_RECORD(iface, struct wbem_refresher, IWbemRefresher_iface);
}

static inline struct wbem_refresher *impl_from_IWbemConfigureRefresher(IWbemConfigureRefresher *iface)
{
    return CONTAINING_RECORD(iface, struct wbem_refresher, IWbemConfigureRefresher_iface);
}

static HRESULT wbem_refresher_query_interface(struct wbem_refresher *refresher, REFIID iid, void **obj)
{
    if (!obj) return E_INVALIDARG;

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IWbemRefresher))
        *obj = &refresher->IWbemRefresher_iface;
    else if (IsEqualGUID(iid, &IID_IWbemConfigureRefresher))
        *obj = &refresher->IWbemConfigureRefresher_iface;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }

    InterlockedIncrement(&refresher->refs);
    return S_OK;
}

static HRESULT WINAPI refresher_QueryInterface(IWbemRefresher *iface, REFIID iid, void **obj)
{
    return wbem_refresher_query_interface(impl_from_IWbemRefresher(iface), iid, obj);
}

static ULONG WINAPI refresher_AddRef(IWbemRefresher *iface)
{
    return InterlockedIncrement(&impl_from_IWbemRefresher(iface)->refs);
}

static ULONG WINAPI refresher_Release(IWbemRefresher *iface)
{
    struct wbem_refresher *refresher = impl_from_IWbemRefresher(iface);
    ULONG refs = InterlockedDecrement(&refresher->refs);

    if (!refs) free(refresher);
    return refs;
}

static HRESULT WINAPI refresher_Refresh(IWbemRefresher *iface, LONG flags)
{
    TRACE("%p, %#lx\n", iface, flags);
    return S_OK;
}

static const IWbemRefresherVtbl refresher_vtbl =
{
    refresher_QueryInterface,
    refresher_AddRef,
    refresher_Release,
    refresher_Refresh,
};

static HRESULT WINAPI configure_QueryInterface(IWbemConfigureRefresher *iface, REFIID iid, void **obj)
{
    return wbem_refresher_query_interface(impl_from_IWbemConfigureRefresher(iface), iid, obj);
}

static ULONG WINAPI configure_AddRef(IWbemConfigureRefresher *iface)
{
    return InterlockedIncrement(&impl_from_IWbemConfigureRefresher(iface)->refs);
}

static ULONG WINAPI configure_Release(IWbemConfigureRefresher *iface)
{
    struct wbem_refresher *refresher = impl_from_IWbemConfigureRefresher(iface);
    ULONG refs = InterlockedDecrement(&refresher->refs);

    if (!refs) free(refresher);
    return refs;
}

static HRESULT WINAPI configure_AddObjectByPath(IWbemConfigureRefresher *iface, IWbemServices *services,
        LPCWSTR path, LONG flags, IWbemContext *context, IWbemClassObject **refreshable, LONG *id)
{
    FIXME("%p, %p, %s, %#lx, %p, %p, %p: stub\n", iface, services, debugstr_w(path), flags, context,
          refreshable, id);
    if (refreshable) *refreshable = NULL;
    if (id) *id = 0;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI configure_AddObjectByTemplate(IWbemConfigureRefresher *iface, IWbemServices *services,
        IWbemClassObject *object, LONG flags, IWbemContext *context, IWbemClassObject **refreshable, LONG *id)
{
    FIXME("%p, %p, %p, %#lx, %p, %p, %p: stub\n", iface, services, object, flags, context, refreshable, id);
    if (refreshable) *refreshable = NULL;
    if (id) *id = 0;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI configure_AddRefresher(IWbemConfigureRefresher *iface, IWbemRefresher *nested,
        LONG flags, LONG *id)
{
    FIXME("%p, %p, %#lx, %p: stub\n", iface, nested, flags, id);
    if (id) *id = 0;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI configure_Remove(IWbemConfigureRefresher *iface, LONG id, LONG flags)
{
    FIXME("%p, %ld, %#lx: stub\n", iface, id, flags);
    return WBEM_E_NOT_FOUND;
}

static HRESULT WINAPI configure_AddEnum(IWbemConfigureRefresher *iface, IWbemServices *services,
        LPCWSTR class_name, LONG flags, IWbemContext *context, IWbemHiPerfEnum **enumerator, LONG *id)
{
    FIXME("%p, %p, %s, %#lx, %p, %p, %p: stub\n", iface, services, debugstr_w(class_name), flags,
          context, enumerator, id);
    if (enumerator) *enumerator = NULL;
    if (id) *id = 0;
    return WBEM_E_NOT_SUPPORTED;
}

static const IWbemConfigureRefresherVtbl configure_vtbl =
{
    configure_QueryInterface,
    configure_AddRef,
    configure_Release,
    configure_AddObjectByPath,
    configure_AddObjectByTemplate,
    configure_AddRefresher,
    configure_Remove,
    configure_AddEnum,
};

HRESULT WbemRefresher_create(void **obj, REFIID iid)
{
    struct wbem_refresher *refresher;
    HRESULT hr;

    if (!obj) return E_INVALIDARG;
    *obj = NULL;

    if (!(refresher = calloc(1, sizeof(*refresher)))) return E_OUTOFMEMORY;
    refresher->IWbemRefresher_iface.lpVtbl = &refresher_vtbl;
    refresher->IWbemConfigureRefresher_iface.lpVtbl = &configure_vtbl;
    refresher->refs = 1;

    hr = wbem_refresher_query_interface(refresher, iid, obj);
    refresher_Release(&refresher->IWbemRefresher_iface);
    return hr;
}
