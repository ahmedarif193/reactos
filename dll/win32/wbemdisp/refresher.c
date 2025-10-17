/*
 * WMI refresher stub implementations
 *
 * Copyright 2025 ReactOS
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
#define CINTERFACE

#include "windef.h"
#include "winbase.h"
#include "oleauto.h"

#include "wbemcli.h"

#include "wine/debug.h"
#include "wine/heap.h"

#include "wbemdisp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(wbemdisp);

#ifndef __IWbemRefresher_FWD_DEFINED__
typedef interface IWbemRefresher IWbemRefresher;
#endif
#ifndef __IWbemConfigureRefresher_FWD_DEFINED__
typedef interface IWbemConfigureRefresher IWbemConfigureRefresher;
#endif
#ifndef __IWbemHiPerfEnum_FWD_DEFINED__
typedef interface IWbemHiPerfEnum IWbemHiPerfEnum;
#endif
#ifndef __IWbemObjectAccess_FWD_DEFINED__
typedef interface IWbemObjectAccess IWbemObjectAccess;
#endif
#ifndef __ISWbemServices_FWD_DEFINED__
typedef interface ISWbemServices ISWbemServices;
#endif
#ifndef __ISWbemNamedValueSet_FWD_DEFINED__
typedef interface ISWbemNamedValueSet ISWbemNamedValueSet;
#endif
#ifndef __ISWbemObject_FWD_DEFINED__
typedef interface ISWbemObject ISWbemObject;
#endif
#ifndef __ISWbemObjectSet_FWD_DEFINED__
typedef interface ISWbemObjectSet ISWbemObjectSet;
#endif

EXTERN_C const IID IID_IWbemRefresher;
EXTERN_C const IID IID_IWbemConfigureRefresher;
EXTERN_C const IID IID_ISWbemRefresher;
EXTERN_C const IID IID_ISWbemRefreshableItem;
EXTERN_C const CLSID CLSID_WbemRefresher;

#ifndef __IWbemRefresher_INTERFACE_DEFINED__
#define __IWbemRefresher_INTERFACE_DEFINED__
typedef struct IWbemRefresherVtbl IWbemRefresherVtbl;
struct IWbemRefresher
{
    const IWbemRefresherVtbl *lpVtbl;
};
struct IWbemRefresherVtbl
{
    HRESULT (WINAPI *QueryInterface)( IWbemRefresher *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( IWbemRefresher *This );
    ULONG (WINAPI *Release)( IWbemRefresher *This );
    HRESULT (WINAPI *Refresh)( IWbemRefresher *This, LONG lFlags );
};
#endif

#ifndef __IWbemConfigureRefresher_INTERFACE_DEFINED__
#define __IWbemConfigureRefresher_INTERFACE_DEFINED__
typedef struct IWbemConfigureRefresherVtbl IWbemConfigureRefresherVtbl;
struct IWbemConfigureRefresher
{
    const IWbemConfigureRefresherVtbl *lpVtbl;
};
struct IWbemConfigureRefresherVtbl
{
    HRESULT (WINAPI *QueryInterface)( IWbemConfigureRefresher *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( IWbemConfigureRefresher *This );
    ULONG (WINAPI *Release)( IWbemConfigureRefresher *This );
    HRESULT (WINAPI *AddObjectByPath)( IWbemConfigureRefresher *This, IWbemServices *pNamespace,
                                       LPCWSTR wszPath, LONG lFlags, IWbemContext *pContext,
                                       IWbemClassObject **ppRefreshable, LONG *plId );
    HRESULT (WINAPI *AddObjectByTemplate)( IWbemConfigureRefresher *This, IWbemServices *pNamespace,
                                           IWbemClassObject *pTemplate, LONG lFlags, IWbemContext *pContext,
                                           IWbemClassObject **ppRefreshable, LONG *plId );
    HRESULT (WINAPI *AddRefresher)( IWbemConfigureRefresher *This, IWbemRefresher *pRefresher,
                                    LONG lFlags, LONG *plId );
    HRESULT (WINAPI *Remove)( IWbemConfigureRefresher *This, LONG lId, LONG lFlags );
    HRESULT (WINAPI *AddEnum)( IWbemConfigureRefresher *This, IWbemServices *pNamespace,
                               LPCWSTR wszClassName, LONG lFlags, IWbemContext *pContext,
                               IWbemHiPerfEnum **ppEnum, LONG *plId );
};
#endif

#ifndef __ISWbemRefresher_INTERFACE_DEFINED__
#define __ISWbemRefresher_INTERFACE_DEFINED__
typedef struct ISWbemRefresher ISWbemRefresher;
typedef struct ISWbemRefreshableItem ISWbemRefreshableItem;
typedef struct ISWbemRefresherVtbl ISWbemRefresherVtbl;
struct ISWbemRefresher
{
    const ISWbemRefresherVtbl *lpVtbl;
};
struct ISWbemRefresherVtbl
{
    HRESULT (WINAPI *QueryInterface)( ISWbemRefresher *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ISWbemRefresher *This );
    ULONG (WINAPI *Release)( ISWbemRefresher *This );
    HRESULT (WINAPI *GetTypeInfoCount)( ISWbemRefresher *This, UINT *pctinfo );
    HRESULT (WINAPI *GetTypeInfo)( ISWbemRefresher *This, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo );
    HRESULT (WINAPI *GetIDsOfNames)( ISWbemRefresher *This, REFIID riid, LPOLESTR *rgszNames,
                                     UINT cNames, LCID lcid, DISPID *rgDispId );
    HRESULT (WINAPI *Invoke)( ISWbemRefresher *This, DISPID dispIdMember, REFIID riid, LCID lcid,
                              WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult,
                              EXCEPINFO *pExcepInfo, UINT *puArgErr );
    HRESULT (WINAPI *get__NewEnum)( ISWbemRefresher *This, IUnknown **pUnk );
    HRESULT (WINAPI *Item)( ISWbemRefresher *This, LONG iIndex, ISWbemRefreshableItem **objWbemRefreshableItem );
    HRESULT (WINAPI *get_Count)( ISWbemRefresher *This, LONG *iCount );
    HRESULT (WINAPI *Add)( ISWbemRefresher *This, ISWbemServices *objWbemServices, BSTR bsInstancePath,
                           LONG iFlags, IDispatch *objWbemNamedValueSet, ISWbemRefreshableItem **objWbemRefreshableItem );
    HRESULT (WINAPI *AddEnum)( ISWbemRefresher *This, ISWbemServices *objWbemServices, BSTR bsClassName,
                               LONG iFlags, IDispatch *objWbemNamedValueSet, ISWbemRefreshableItem **objWbemRefreshableItem );
    HRESULT (WINAPI *Remove)( ISWbemRefresher *This, LONG iIndex, LONG iFlags );
    HRESULT (WINAPI *Refresh)( ISWbemRefresher *This, LONG iFlags );
    HRESULT (WINAPI *get_AutoReconnect)( ISWbemRefresher *This, VARIANT_BOOL *bCount );
    HRESULT (WINAPI *put_AutoReconnect)( ISWbemRefresher *This, VARIANT_BOOL bCount );
    HRESULT (WINAPI *DeleteAll)( ISWbemRefresher *This );
};
#endif

#ifndef __ISWbemRefreshableItem_INTERFACE_DEFINED__
#define __ISWbemRefreshableItem_INTERFACE_DEFINED__
typedef struct ISWbemRefreshableItemVtbl ISWbemRefreshableItemVtbl;
struct ISWbemRefreshableItem
{
    const ISWbemRefreshableItemVtbl *lpVtbl;
};
struct ISWbemRefreshableItemVtbl
{
    HRESULT (WINAPI *QueryInterface)( ISWbemRefreshableItem *This, REFIID riid, void **ppvObject );
    ULONG (WINAPI *AddRef)( ISWbemRefreshableItem *This );
    ULONG (WINAPI *Release)( ISWbemRefreshableItem *This );
    HRESULT (WINAPI *GetTypeInfoCount)( ISWbemRefreshableItem *This, UINT *pctinfo );
    HRESULT (WINAPI *GetTypeInfo)( ISWbemRefreshableItem *This, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo );
    HRESULT (WINAPI *GetIDsOfNames)( ISWbemRefreshableItem *This, REFIID riid, LPOLESTR *rgszNames,
                                     UINT cNames, LCID lcid, DISPID *rgDispId );
    HRESULT (WINAPI *Invoke)( ISWbemRefreshableItem *This, DISPID dispIdMember, REFIID riid, LCID lcid,
                              WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult,
                              EXCEPINFO *pExcepInfo, UINT *puArgErr );
    HRESULT (WINAPI *get_Index)( ISWbemRefreshableItem *This, LONG *iIndex );
    HRESULT (WINAPI *get_Refresher)( ISWbemRefreshableItem *This, ISWbemRefresher **objWbemRefresher );
    HRESULT (WINAPI *get_IsSet)( ISWbemRefreshableItem *This, VARIANT_BOOL *bIsSet );
    HRESULT (WINAPI *get_Object)( ISWbemRefreshableItem *This, ISWbemObject **objWbemObject );
    HRESULT (WINAPI *get_ObjectSet)( ISWbemRefreshableItem *This, ISWbemObjectSet **objWbemObjectSet );
    HRESULT (WINAPI *Remove)( ISWbemRefreshableItem *This, LONG iFlags );
};
#endif


struct wbem_refresher
{
    IWbemRefresher IWbemRefresher_iface;
    IWbemConfigureRefresher IWbemConfigureRefresher_iface;
    LONG refs;
};

struct swbem_refresher
{
    ISWbemRefresher ISWbemRefresher_iface;
    LONG refs;
    VARIANT_BOOL auto_reconnect;
};

struct swbem_refreshable_item
{
    ISWbemRefreshableItem ISWbemRefreshableItem_iface;
    LONG refs;
};

static inline struct wbem_refresher *impl_from_IWbemRefresher( IWbemRefresher *iface )
{
    return CONTAINING_RECORD( iface, struct wbem_refresher, IWbemRefresher_iface );
}

static inline struct wbem_refresher *impl_from_IWbemConfigureRefresher( IWbemConfigureRefresher *iface )
{
    return CONTAINING_RECORD( iface, struct wbem_refresher, IWbemConfigureRefresher_iface );
}

static inline struct swbem_refresher *impl_from_ISWbemRefresher( ISWbemRefresher *iface )
{
    return CONTAINING_RECORD( iface, struct swbem_refresher, ISWbemRefresher_iface );
}

static inline struct swbem_refreshable_item *impl_from_ISWbemRefreshableItem( ISWbemRefreshableItem *iface )
{
    return CONTAINING_RECORD( iface, struct swbem_refreshable_item, ISWbemRefreshableItem_iface );
}

/* IWbemRefresher */

static HRESULT WINAPI wbem_refresher_QueryInterface( IWbemRefresher *iface, REFIID riid, void **ppv )
{
    struct wbem_refresher *refresher = impl_from_IWbemRefresher( iface );

    TRACE( "%p %s %p\n", refresher, debugstr_guid(riid), ppv );

    if (!ppv) return E_POINTER;

    if (IsEqualGUID( riid, &IID_IUnknown ) || IsEqualGUID( riid, &IID_IWbemRefresher ))
        *ppv = &refresher->IWbemRefresher_iface;
    else if (IsEqualGUID( riid, &IID_IWbemConfigureRefresher ))
        *ppv = &refresher->IWbemConfigureRefresher_iface;
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown *)*ppv );
    return S_OK;
}

static ULONG WINAPI wbem_refresher_AddRef( IWbemRefresher *iface )
{
    struct wbem_refresher *refresher = impl_from_IWbemRefresher( iface );
    return InterlockedIncrement( &refresher->refs );
}

static ULONG WINAPI wbem_refresher_Release( IWbemRefresher *iface )
{
    struct wbem_refresher *refresher = impl_from_IWbemRefresher( iface );
    LONG refs = InterlockedDecrement( &refresher->refs );

    if (!refs)
    {
        TRACE( "destroying %p\n", refresher );
        heap_free( refresher );
    }

    return refs;
}

static HRESULT WINAPI wbem_refresher_Refresh( IWbemRefresher *iface, LONG flags )
{
    TRACE( "%p %ld\n", iface, flags );
    return WBEM_E_NOT_SUPPORTED;
}

static const IWbemRefresherVtbl wbem_refresher_vtbl =
{
    wbem_refresher_QueryInterface,
    wbem_refresher_AddRef,
    wbem_refresher_Release,
    wbem_refresher_Refresh
};

/* IWbemConfigureRefresher */

static HRESULT WINAPI wbem_config_QueryInterface( IWbemConfigureRefresher *iface, REFIID riid, void **ppv )
{
    struct wbem_refresher *refresher = impl_from_IWbemConfigureRefresher( iface );
    return wbem_refresher_QueryInterface( &refresher->IWbemRefresher_iface, riid, ppv );
}

static ULONG WINAPI wbem_config_AddRef( IWbemConfigureRefresher *iface )
{
    struct wbem_refresher *refresher = impl_from_IWbemConfigureRefresher( iface );
    return wbem_refresher_AddRef( &refresher->IWbemRefresher_iface );
}

static ULONG WINAPI wbem_config_Release( IWbemConfigureRefresher *iface )
{
    struct wbem_refresher *refresher = impl_from_IWbemConfigureRefresher( iface );
    return wbem_refresher_Release( &refresher->IWbemRefresher_iface );
}

static HRESULT WINAPI wbem_config_AddObjectByPath( IWbemConfigureRefresher *iface, IWbemServices *namespace,
                                                   LPCWSTR path, LONG flags, IWbemContext *context,
                                                   IWbemClassObject **refreshable, LONG *id )
{
    TRACE( "%p %p %s %ld %p %p %p\n", iface, namespace, debugstr_w(path), flags, context, refreshable, id );
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI wbem_config_AddObjectByTemplate( IWbemConfigureRefresher *iface, IWbemServices *namespace,
                                                       IWbemClassObject *templ, LONG flags, IWbemContext *context,
                                                       IWbemClassObject **refreshable, LONG *id )
{
    TRACE( "%p %p %p %ld %p %p %p\n", iface, namespace, templ, flags, context, refreshable, id );
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI wbem_config_AddRefresher( IWbemConfigureRefresher *iface, IWbemRefresher *refresher,
                                                LONG flags, LONG *id )
{
    TRACE( "%p %p %ld %p\n", iface, refresher, flags, id );
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI wbem_config_Remove( IWbemConfigureRefresher *iface, LONG id, LONG flags )
{
    TRACE( "%p %ld %ld\n", iface, id, flags );
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI wbem_config_AddEnum( IWbemConfigureRefresher *iface, IWbemServices *namespace,
                                           LPCWSTR class_name, LONG flags, IWbemContext *context,
                                           IWbemHiPerfEnum **hi_perf, LONG *id )
{
    TRACE( "%p %p %s %ld %p %p %p\n", iface, namespace, debugstr_w(class_name), flags, context, hi_perf, id );
    return WBEM_E_NOT_SUPPORTED;
}

static const IWbemConfigureRefresherVtbl wbem_config_vtbl =
{
    wbem_config_QueryInterface,
    wbem_config_AddRef,
    wbem_config_Release,
    wbem_config_AddObjectByPath,
    wbem_config_AddObjectByTemplate,
    wbem_config_AddRefresher,
    wbem_config_Remove,
    wbem_config_AddEnum
};

/* ISWbemRefresher */

static HRESULT WINAPI swbem_refresher_QueryInterface( ISWbemRefresher *iface, REFIID riid, void **ppv )
{
    struct swbem_refresher *refresher = impl_from_ISWbemRefresher( iface );

    TRACE( "%p %s %p\n", refresher, debugstr_guid(riid), ppv );

    if (!ppv) return E_POINTER;

    if (IsEqualGUID( riid, &IID_IUnknown ) ||
        IsEqualGUID( riid, &IID_IDispatch ) ||
        IsEqualGUID( riid, &IID_ISWbemRefresher ))
    {
        *ppv = iface;
        iface->lpVtbl->AddRef( iface );
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI swbem_refresher_AddRef( ISWbemRefresher *iface )
{
    struct swbem_refresher *refresher = impl_from_ISWbemRefresher( iface );
    return InterlockedIncrement( &refresher->refs );
}

static ULONG WINAPI swbem_refresher_Release( ISWbemRefresher *iface )
{
    struct swbem_refresher *refresher = impl_from_ISWbemRefresher( iface );
    LONG refs = InterlockedDecrement( &refresher->refs );

    if (!refs)
    {
        TRACE( "destroying %p\n", refresher );
        heap_free( refresher );
    }
    return refs;
}

static HRESULT WINAPI swbem_refresher_GetTypeInfoCount( ISWbemRefresher *iface, UINT *count )
{
    TRACE( "%p %p\n", iface, count );
    if (count) *count = 0;
    return S_OK;
}

static HRESULT WINAPI swbem_refresher_GetTypeInfo( ISWbemRefresher *iface, UINT index, LCID lcid, ITypeInfo **info )
{
    FIXME( "%p index %u lcid %u typeinfo stub\n", iface, index, (unsigned int)lcid );
    return E_NOTIMPL;
}

static HRESULT WINAPI swbem_refresher_GetIDsOfNames( ISWbemRefresher *iface, REFIID riid, LPOLESTR *names,
                                                     UINT count, LCID lcid, DISPID *dispids )
{
    FIXME( "%p riid %s name-count %u lcid %u stub\n", iface, debugstr_guid(riid), count, (unsigned int)lcid );
    return DISP_E_UNKNOWNNAME;
}

static HRESULT WINAPI swbem_refresher_Invoke( ISWbemRefresher *iface, DISPID dispid, REFIID riid, LCID lcid,
                                              WORD flags, DISPPARAMS *params, VARIANT *result,
                                              EXCEPINFO *excep, UINT *argerr )
{
    FIXME( "%p dispid %ld flags 0x%x invoke stub\n", iface, dispid, flags );
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT WINAPI swbem_refresher_get__NewEnum( ISWbemRefresher *iface, IUnknown **unk )
{
    FIXME( "%p enumeration stub\n", iface );
    if (!unk) return E_POINTER;
    *unk = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI swbem_refresher_Item( ISWbemRefresher *iface, LONG index,
                                            ISWbemRefreshableItem **item )
{
    FIXME( "%p index %ld stub\n", iface, index );
    if (item) *item = NULL;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_refresher_get_Count( ISWbemRefresher *iface, LONG *count )
{
    TRACE( "%p %p\n", iface, count );
    if (!count) return E_POINTER;
    *count = 0;
    return S_OK;
}

static HRESULT WINAPI swbem_refresher_Add( ISWbemRefresher *iface, ISWbemServices *services, BSTR path,
                                           LONG flags, IDispatch *named_values,
                                           ISWbemRefreshableItem **item )
{
    FIXME( "%p Add path %s flags 0x%lx stub\n", iface, debugstr_w(path), flags );
    if (item) *item = NULL;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_refresher_AddEnum( ISWbemRefresher *iface, ISWbemServices *services, BSTR class_name,
                                               LONG flags, IDispatch *named_values,
                                               ISWbemRefreshableItem **item )
{
    FIXME( "%p AddEnum class %s flags 0x%lx stub\n", iface, debugstr_w(class_name), flags );
    if (item) *item = NULL;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_refresher_Remove( ISWbemRefresher *iface, LONG index, LONG flags )
{
    FIXME( "%p Remove index %ld flags 0x%lx stub\n", iface, index, flags );
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_refresher_Refresh( ISWbemRefresher *iface, LONG flags )
{
    FIXME( "%p Refresh flags 0x%lx stub\n", iface, flags );
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_refresher_get_AutoReconnect( ISWbemRefresher *iface, VARIANT_BOOL *value )
{
    struct swbem_refresher *refresher = impl_from_ISWbemRefresher( iface );

    TRACE( "%p %p\n", refresher, value );
    if (!value) return E_POINTER;

    *value = refresher->auto_reconnect;
    return S_OK;
}

static HRESULT WINAPI swbem_refresher_put_AutoReconnect( ISWbemRefresher *iface, VARIANT_BOOL value )
{
    struct swbem_refresher *refresher = impl_from_ISWbemRefresher( iface );

    TRACE( "%p %d\n", refresher, value );
    refresher->auto_reconnect = value ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI swbem_refresher_DeleteAll( ISWbemRefresher *iface )
{
    FIXME( "%p DeleteAll stub\n", iface );
    return WBEM_E_NOT_SUPPORTED;
}

static const ISWbemRefresherVtbl swbem_refresher_vtbl =
{
    swbem_refresher_QueryInterface,
    swbem_refresher_AddRef,
    swbem_refresher_Release,
    swbem_refresher_GetTypeInfoCount,
    swbem_refresher_GetTypeInfo,
    swbem_refresher_GetIDsOfNames,
    swbem_refresher_Invoke,
    swbem_refresher_get__NewEnum,
    swbem_refresher_Item,
    swbem_refresher_get_Count,
    swbem_refresher_Add,
    swbem_refresher_AddEnum,
    swbem_refresher_Remove,
    swbem_refresher_Refresh,
    swbem_refresher_get_AutoReconnect,
    swbem_refresher_put_AutoReconnect,
    swbem_refresher_DeleteAll
};

/* ISWbemRefreshableItem */

static HRESULT WINAPI swbem_item_QueryInterface( ISWbemRefreshableItem *iface, REFIID riid, void **ppv )
{
    struct swbem_refreshable_item *item = impl_from_ISWbemRefreshableItem( iface );

    TRACE( "%p %s %p\n", item, debugstr_guid(riid), ppv );

    if (!ppv) return E_POINTER;

    if (IsEqualGUID( riid, &IID_IUnknown ) ||
        IsEqualGUID( riid, &IID_IDispatch ) ||
        IsEqualGUID( riid, &IID_ISWbemRefreshableItem ))
    {
        *ppv = iface;
        iface->lpVtbl->AddRef( iface );
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI swbem_item_AddRef( ISWbemRefreshableItem *iface )
{
    struct swbem_refreshable_item *item = impl_from_ISWbemRefreshableItem( iface );
    return InterlockedIncrement( &item->refs );
}

static ULONG WINAPI swbem_item_Release( ISWbemRefreshableItem *iface )
{
    struct swbem_refreshable_item *item = impl_from_ISWbemRefreshableItem( iface );
    LONG refs = InterlockedDecrement( &item->refs );

    if (!refs)
    {
        TRACE( "destroying %p\n", item );
        heap_free( item );
    }
    return refs;
}

static HRESULT WINAPI swbem_item_GetTypeInfoCount( ISWbemRefreshableItem *iface, UINT *count )
{
    TRACE( "%p %p\n", iface, count );
    if (count) *count = 0;
    return S_OK;
}

static HRESULT WINAPI swbem_item_GetTypeInfo( ISWbemRefreshableItem *iface, UINT index, LCID lcid, ITypeInfo **info )
{
    FIXME( "%p index %u lcid %u typeinfo stub\n", iface, index, (unsigned int)lcid );
    return E_NOTIMPL;
}

static HRESULT WINAPI swbem_item_GetIDsOfNames( ISWbemRefreshableItem *iface, REFIID riid, LPOLESTR *names,
                                                UINT count, LCID lcid, DISPID *dispids )
{
    FIXME( "%p riid %s name-count %u lcid %u stub\n", iface, debugstr_guid(riid), count, (unsigned int)lcid );
    return DISP_E_UNKNOWNNAME;
}

static HRESULT WINAPI swbem_item_Invoke( ISWbemRefreshableItem *iface, DISPID dispid, REFIID riid, LCID lcid,
                                         WORD flags, DISPPARAMS *params, VARIANT *result,
                                         EXCEPINFO *excep, UINT *argerr )
{
    FIXME( "%p dispid %ld flags 0x%x invoke stub\n", iface, dispid, flags );
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT WINAPI swbem_item_get_Index( ISWbemRefreshableItem *iface, LONG *index )
{
    FIXME( "%p get_Index stub\n", iface );
    if (!index) return E_POINTER;
    *index = 0;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_item_get_Refresher( ISWbemRefreshableItem *iface, ISWbemRefresher **refresher )
{
    FIXME( "%p get_Refresher stub\n", iface );
    if (!refresher) return E_POINTER;
    *refresher = NULL;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_item_get_IsSet( ISWbemRefreshableItem *iface, VARIANT_BOOL *is_set )
{
    FIXME( "%p get_IsSet stub\n", iface );
    if (!is_set) return E_POINTER;
    *is_set = VARIANT_FALSE;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_item_get_Object( ISWbemRefreshableItem *iface, ISWbemObject **object )
{
    FIXME( "%p get_Object stub\n", iface );
    if (!object) return E_POINTER;
    *object = NULL;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_item_get_ObjectSet( ISWbemRefreshableItem *iface, ISWbemObjectSet **object_set )
{
    FIXME( "%p get_ObjectSet stub\n", iface );
    if (!object_set) return E_POINTER;
    *object_set = NULL;
    return WBEM_E_NOT_SUPPORTED;
}

static HRESULT WINAPI swbem_item_Remove( ISWbemRefreshableItem *iface, LONG flags )
{
    FIXME( "%p Remove flags 0x%lx stub\n", iface, flags );
    return WBEM_E_NOT_SUPPORTED;
}

static const ISWbemRefreshableItemVtbl swbem_item_vtbl =
{
    swbem_item_QueryInterface,
    swbem_item_AddRef,
    swbem_item_Release,
    swbem_item_GetTypeInfoCount,
    swbem_item_GetTypeInfo,
    swbem_item_GetIDsOfNames,
    swbem_item_Invoke,
    swbem_item_get_Index,
    swbem_item_get_Refresher,
    swbem_item_get_IsSet,
    swbem_item_get_Object,
    swbem_item_get_ObjectSet,
    swbem_item_Remove
};

HRESULT WbemRefresher_create( LPVOID *obj )
{
    struct wbem_refresher *refresher;

    if (!obj) return E_POINTER;

    refresher = heap_alloc_zero( sizeof(*refresher) );
    if (!refresher) return E_OUTOFMEMORY;

    refresher->IWbemRefresher_iface.lpVtbl = &wbem_refresher_vtbl;
    refresher->IWbemConfigureRefresher_iface.lpVtbl = &wbem_config_vtbl;
    refresher->refs = 1;

    *obj = &refresher->IWbemRefresher_iface;
    TRACE( "created %p\n", refresher );
    return S_OK;
}

HRESULT SWbemRefresher_create( LPVOID *obj )
{
    struct swbem_refresher *refresher;

    if (!obj) return E_POINTER;

    refresher = heap_alloc_zero( sizeof(*refresher) );
    if (!refresher) return E_OUTOFMEMORY;

    refresher->ISWbemRefresher_iface.lpVtbl = &swbem_refresher_vtbl;
    refresher->refs = 1;
    refresher->auto_reconnect = VARIANT_FALSE;
    *obj = &refresher->ISWbemRefresher_iface;
    TRACE( "created %p\n", refresher );
    return S_OK;
}

HRESULT SWbemRefreshableItem_create( LPVOID *obj )
{
    struct swbem_refreshable_item *item;

    if (!obj) return E_POINTER;

    item = heap_alloc_zero( sizeof(*item) );
    if (!item) return E_OUTOFMEMORY;

    item->ISWbemRefreshableItem_iface.lpVtbl = &swbem_item_vtbl;
    item->refs = 1;

    *obj = &item->ISWbemRefreshableItem_iface;
    TRACE( "created %p\n", item );
    return S_OK;
}
