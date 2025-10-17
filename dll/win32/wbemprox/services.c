/*
 * Copyright 2012 Hans Leidekker for CodeWeavers
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

#define COBJMACROS

#include <stdarg.h>
#include <string.h>
#ifdef __REACTOS__
#include <wchar.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "wbemcli.h"
#include "rpcdce.h"
#include "sspi.h"

#include "wine/debug.h"
#include "wbemprox_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(wbemprox);

static inline struct client_security *impl_from_IClientSecurity( IClientSecurity *iface )
{
    return CONTAINING_RECORD( iface, struct client_security, IClientSecurity_iface );
}

static WCHAR *duplicate_server_principal_name( const WCHAR *src )
{
    size_t len;
    WCHAR *dst;

    if (!src) return NULL;
    len = lstrlenW( src );
    dst = CoTaskMemAlloc( (len + 1) * sizeof(WCHAR) );
    if (!dst) return NULL;
    memcpy( dst, src, (len + 1) * sizeof(WCHAR) );
    return dst;
}

static void free_auth_identity( void *identity )
{
    ULONG flags;

    if (!identity) return;

    flags = ((SEC_WINNT_AUTH_IDENTITY_W *)identity)->Flags;
    if (flags == SEC_WINNT_AUTH_IDENTITY_UNICODE)
    {
        SEC_WINNT_AUTH_IDENTITY_W *id = identity;

        if (id->User) CoTaskMemFree( id->User );
        if (id->Domain) CoTaskMemFree( id->Domain );
        if (id->Password) CoTaskMemFree( id->Password );
        CoTaskMemFree( id );
    }
    else if (flags == SEC_WINNT_AUTH_IDENTITY_ANSI)
    {
        SEC_WINNT_AUTH_IDENTITY_A *id = identity;

        if (id->User) CoTaskMemFree( id->User );
        if (id->Domain) CoTaskMemFree( id->Domain );
        if (id->Password) CoTaskMemFree( id->Password );
        CoTaskMemFree( id );
    }
    else
    {
        CoTaskMemFree( identity );
    }
}

static HRESULT copy_auth_identity( void **dst, const void *src )
{
    ULONG flags;

    *dst = NULL;
    if (!src) return S_OK;

    flags = ((const SEC_WINNT_AUTH_IDENTITY_W *)src)->Flags;
    if (flags == SEC_WINNT_AUTH_IDENTITY_UNICODE)
    {
        const SEC_WINNT_AUTH_IDENTITY_W *srcW = src;
        SEC_WINNT_AUTH_IDENTITY_W *dstW;

        dstW = CoTaskMemAlloc( sizeof(*dstW) );
        if (!dstW) return E_OUTOFMEMORY;
        memset( dstW, 0, sizeof(*dstW) );
        dstW->Flags = srcW->Flags;

        if (srcW->User && srcW->UserLength)
        {
            dstW->User = CoTaskMemAlloc( srcW->UserLength * sizeof(WCHAR) );
            if (!dstW->User)
            {
                free_auth_identity( dstW );
                return E_OUTOFMEMORY;
            }
            memcpy( dstW->User, srcW->User, srcW->UserLength * sizeof(WCHAR) );
        }
        dstW->UserLength = srcW->UserLength;

        if (srcW->Domain && srcW->DomainLength)
        {
            dstW->Domain = CoTaskMemAlloc( srcW->DomainLength * sizeof(WCHAR) );
            if (!dstW->Domain)
            {
                free_auth_identity( dstW );
                return E_OUTOFMEMORY;
            }
            memcpy( dstW->Domain, srcW->Domain, srcW->DomainLength * sizeof(WCHAR) );
        }
        dstW->DomainLength = srcW->DomainLength;

        if (srcW->Password && srcW->PasswordLength)
        {
            dstW->Password = CoTaskMemAlloc( srcW->PasswordLength * sizeof(WCHAR) );
            if (!dstW->Password)
            {
                free_auth_identity( dstW );
                return E_OUTOFMEMORY;
            }
            memcpy( dstW->Password, srcW->Password, srcW->PasswordLength * sizeof(WCHAR) );
        }
        dstW->PasswordLength = srcW->PasswordLength;

        *dst = dstW;
        return S_OK;
    }
    else if (flags == SEC_WINNT_AUTH_IDENTITY_ANSI)
    {
        const SEC_WINNT_AUTH_IDENTITY_A *srcA = src;
        SEC_WINNT_AUTH_IDENTITY_A *dstA;

        dstA = CoTaskMemAlloc( sizeof(*dstA) );
        if (!dstA) return E_OUTOFMEMORY;
        memset( dstA, 0, sizeof(*dstA) );
        dstA->Flags = srcA->Flags;

        if (srcA->User && srcA->UserLength)
        {
            dstA->User = CoTaskMemAlloc( srcA->UserLength * sizeof(CHAR) );
            if (!dstA->User)
            {
                free_auth_identity( dstA );
                return E_OUTOFMEMORY;
            }
            memcpy( dstA->User, srcA->User, srcA->UserLength * sizeof(CHAR) );
        }
        dstA->UserLength = srcA->UserLength;

        if (srcA->Domain && srcA->DomainLength)
        {
            dstA->Domain = CoTaskMemAlloc( srcA->DomainLength * sizeof(CHAR) );
            if (!dstA->Domain)
            {
                free_auth_identity( dstA );
                return E_OUTOFMEMORY;
            }
            memcpy( dstA->Domain, srcA->Domain, srcA->DomainLength * sizeof(CHAR) );
        }
        dstA->DomainLength = srcA->DomainLength;

        if (srcA->Password && srcA->PasswordLength)
        {
            dstA->Password = CoTaskMemAlloc( srcA->PasswordLength * sizeof(CHAR) );
            if (!dstA->Password)
            {
                free_auth_identity( dstA );
                return E_OUTOFMEMORY;
            }
            memcpy( dstA->Password, srcA->Password, srcA->PasswordLength * sizeof(CHAR) );
        }
        dstA->PasswordLength = srcA->PasswordLength;

        *dst = dstA;
        return S_OK;
    }

    WARN( "unsupported auth identity flags %#lx\n", flags );
    return E_INVALIDARG;
}

static const IClientSecurityVtbl client_security_vtbl;

HRESULT client_security_init( struct client_security *security, IUnknown *outer,
                              const struct client_security *template_security )
{
    HRESULT hr = S_OK;

    security->IClientSecurity_iface.lpVtbl = &client_security_vtbl;
    security->outer = outer;
    security->server_princ_name = NULL;
    security->auth_identity = NULL;

    InitializeCriticalSection( &security->cs );
    security->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": wbemprox_client_security.cs");

    if (template_security)
    {
        struct client_security *parent = (struct client_security *)template_security;
        WCHAR *princ_copy = NULL;
        void *identity_copy = NULL;

        EnterCriticalSection( &parent->cs );
        security->authn_svc = parent->authn_svc;
        security->authz_svc = parent->authz_svc;
        security->authn_level = parent->authn_level;
        security->imp_level = parent->imp_level;
        security->capabilities = parent->capabilities;

        if (parent->server_princ_name)
        {
            princ_copy = duplicate_server_principal_name( parent->server_princ_name );
            if (!princ_copy)
            {
                LeaveCriticalSection( &parent->cs );
                hr = E_OUTOFMEMORY;
                goto fail;
            }
        }

        hr = copy_auth_identity( &identity_copy, parent->auth_identity );
        LeaveCriticalSection( &parent->cs );
        if (FAILED( hr ))
        {
            if (princ_copy) CoTaskMemFree( princ_copy );
            goto fail;
        }

        security->server_princ_name = princ_copy;
        security->auth_identity = identity_copy;
    }
    else
    {
        security->authn_svc = RPC_C_AUTHN_DEFAULT;
        security->authz_svc = RPC_C_AUTHZ_DEFAULT;
        security->authn_level = RPC_C_AUTHN_LEVEL_DEFAULT;
        security->imp_level = RPC_C_IMP_LEVEL_IMPERSONATE;
        security->capabilities = EOAC_NONE;
    }

    return hr;

fail:
    client_security_cleanup( security );
    return hr;
}

void client_security_cleanup( struct client_security *security )
{
    if (!security) return;

    if (security->server_princ_name)
    {
        CoTaskMemFree( security->server_princ_name );
        security->server_princ_name = NULL;
    }
    if (security->auth_identity)
    {
        free_auth_identity( security->auth_identity );
        security->auth_identity = NULL;
    }
    DeleteCriticalSection( &security->cs );
    security->outer = NULL;
}

static HRESULT WINAPI client_security_QueryInterface(
    IClientSecurity *iface,
    REFIID riid,
    void **ppvObject )
{
    struct client_security *cs = impl_from_IClientSecurity( iface );

    TRACE("%p %s %p\n", cs, debugstr_guid( riid ), ppvObject );

    if (IsEqualGUID( riid, &IID_IClientSecurity ) ||
        IsEqualGUID( riid, &IID_IUnknown ))
    {
        *ppvObject = &cs->IClientSecurity_iface;
        IClientSecurity_AddRef( iface );
        return S_OK;
    }

    WARN("interface %s not supported\n", debugstr_guid( riid ));
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI client_security_AddRef( IClientSecurity *iface )
{
    struct client_security *cs = impl_from_IClientSecurity( iface );
    return IUnknown_AddRef( cs->outer );
}

static ULONG WINAPI client_security_Release( IClientSecurity *iface )
{
    struct client_security *cs = impl_from_IClientSecurity( iface );
    return IUnknown_Release( cs->outer );
}

static HRESULT WINAPI client_security_QueryBlanket(
    IClientSecurity *iface,
    IUnknown *pProxy,
    DWORD *pAuthnSvc,
    DWORD *pAuthzSvc,
    OLECHAR **pServerPrincName,
    DWORD *pAuthnLevel,
    DWORD *pImpLevel,
    void **pAuthInfo,
    DWORD *pCapabilities )
{
    struct client_security *cs = impl_from_IClientSecurity( iface );
    WCHAR *name_copy = NULL;
    void *identity_copy = NULL;
    HRESULT hr = S_OK;

    EnterCriticalSection( &cs->cs );

    if (pAuthnSvc) *pAuthnSvc = cs->authn_svc;
    if (pAuthzSvc) *pAuthzSvc = cs->authz_svc;
    if (pAuthnLevel) *pAuthnLevel = cs->authn_level;
    if (pImpLevel) *pImpLevel = cs->imp_level;
    if (pCapabilities) *pCapabilities = cs->capabilities;

    if (pServerPrincName && cs->server_princ_name)
    {
        name_copy = duplicate_server_principal_name( cs->server_princ_name );
        if (!name_copy)
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
    }

    if (pAuthInfo)
    {
        hr = copy_auth_identity( &identity_copy, cs->auth_identity );
        if (FAILED( hr ))
            goto done;
    }

done:
    LeaveCriticalSection( &cs->cs );

    if (FAILED( hr ))
    {
        if (name_copy) CoTaskMemFree( name_copy );
        if (identity_copy) free_auth_identity( identity_copy );
        return hr;
    }

    if (pServerPrincName)
        *pServerPrincName = name_copy;
    else if (name_copy)
        CoTaskMemFree( name_copy );

    if (pAuthInfo)
        *pAuthInfo = identity_copy;
    else if (identity_copy)
        free_auth_identity( identity_copy );

    return WBEM_NO_ERROR;
}

static HRESULT WINAPI client_security_SetBlanket(
    IClientSecurity *iface,
    IUnknown *pProxy,
    DWORD AuthnSvc,
    DWORD AuthzSvc,
    OLECHAR *pServerPrincName,
    DWORD AuthnLevel,
    DWORD ImpLevel,
    void *pAuthInfo,
    DWORD Capabilities )
{
    struct client_security *cs = impl_from_IClientSecurity( iface );
    WCHAR *name_copy = NULL;
    void *identity_copy = NULL;
    HRESULT hr;

    if (pServerPrincName && pServerPrincName != COLE_DEFAULT_PRINCIPAL)
    {
        name_copy = duplicate_server_principal_name( pServerPrincName );
        if (!name_copy) return E_OUTOFMEMORY;
    }

    hr = copy_auth_identity( &identity_copy, pAuthInfo );
    if (FAILED( hr ))
    {
        if (name_copy) CoTaskMemFree( name_copy );
        return hr;
    }

    EnterCriticalSection( &cs->cs );

    if (cs->server_princ_name)
        CoTaskMemFree( cs->server_princ_name );
    if (cs->auth_identity)
        free_auth_identity( cs->auth_identity );

    cs->server_princ_name = (pServerPrincName == COLE_DEFAULT_PRINCIPAL) ? NULL : name_copy;
    if (cs->server_princ_name) name_copy = NULL;

    cs->auth_identity = identity_copy;
    identity_copy = NULL;

    cs->authn_svc = AuthnSvc;
    cs->authz_svc = AuthzSvc;
    cs->authn_level = AuthnLevel;
    cs->imp_level = ImpLevel;
    cs->capabilities = Capabilities;

    LeaveCriticalSection( &cs->cs );

    if (name_copy) CoTaskMemFree( name_copy );
    if (identity_copy) free_auth_identity( identity_copy );

    return WBEM_NO_ERROR;
}

static HRESULT WINAPI client_security_CopyProxy(
    IClientSecurity *iface,
    IUnknown *pProxy,
    IUnknown **ppCopy )
{
    if (!ppCopy) return E_POINTER;
    if (!pProxy) return E_INVALIDARG;

    *ppCopy = pProxy;
    IUnknown_AddRef( pProxy );
    return S_OK;
}

static const IClientSecurityVtbl client_security_vtbl =
{
    client_security_QueryInterface,
    client_security_AddRef,
    client_security_Release,
    client_security_QueryBlanket,
    client_security_SetBlanket,
    client_security_CopyProxy
};

struct async_header
{
    IWbemObjectSink *sink;
    void (*proc)( struct async_header * );
    HANDLE cancel;
    HANDLE wait;
};

struct async_query
{
    struct async_header hdr;
    WCHAR *str;
    struct client_security *security;
};

static void free_async( struct async_header *async )
{
    if (async->sink) IWbemObjectSink_Release( async->sink );
    CloseHandle( async->cancel );
    CloseHandle( async->wait );
    heap_free( async );
}

static BOOL init_async( struct async_header *async, IWbemObjectSink *sink,
                        void (*proc)(struct async_header *) )
{
    if (!(async->wait = CreateEventW( NULL, FALSE, FALSE, NULL ))) return FALSE;
    if (!(async->cancel = CreateEventW( NULL, FALSE, FALSE, NULL )))
    {
        CloseHandle( async->wait );
        return FALSE;
    }
    async->proc = proc;
    async->sink = sink;
    IWbemObjectSink_AddRef( sink );
    return TRUE;
}

static DWORD CALLBACK async_proc( LPVOID param )
{
    struct async_header *async = param;
    HANDLE wait = async->wait;

    async->proc( async );

    WaitForSingleObject( async->cancel, INFINITE );
    SetEvent( wait );
    return ERROR_SUCCESS;
}

static HRESULT queue_async( struct async_header *async )
{
    if (QueueUserWorkItem( async_proc, async, WT_EXECUTELONGFUNCTION )) return S_OK;
    return HRESULT_FROM_WIN32( GetLastError() );
}

struct wbem_services
{
    IWbemServices IWbemServices_iface;
    LONG refs;
    CRITICAL_SECTION cs;
    WCHAR *namespace;
    struct async_header *async;
    struct client_security security;
};

static inline struct wbem_services *impl_from_IWbemServices( IWbemServices *iface )
{
    return CONTAINING_RECORD( iface, struct wbem_services, IWbemServices_iface );
}

static ULONG WINAPI wbem_services_AddRef(
    IWbemServices *iface )
{
    struct wbem_services *ws = impl_from_IWbemServices( iface );
    return InterlockedIncrement( &ws->refs );
}

static ULONG WINAPI wbem_services_Release(
    IWbemServices *iface )
{
    struct wbem_services *ws = impl_from_IWbemServices( iface );
    LONG refs = InterlockedDecrement( &ws->refs );
    if (!refs)
    {
        TRACE("destroying %p\n", ws);

        EnterCriticalSection( &ws->cs );
        if (ws->async) SetEvent( ws->async->cancel );
        LeaveCriticalSection( &ws->cs );
        if (ws->async)
        {
            WaitForSingleObject( ws->async->wait, INFINITE );
            free_async( ws->async );
        }
        ws->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &ws->cs );
        client_security_cleanup( &ws->security );
        heap_free( ws->namespace );
        heap_free( ws );
    }
    return refs;
}

static HRESULT WINAPI wbem_services_QueryInterface(
    IWbemServices *iface,
    REFIID riid,
    void **ppvObject )
{
    struct wbem_services *ws = impl_from_IWbemServices( iface );

    TRACE("%p %s %p\n", ws, debugstr_guid( riid ), ppvObject );

    if ( IsEqualGUID( riid, &IID_IWbemServices ) ||
         IsEqualGUID( riid, &IID_IUnknown ) )
    {
        *ppvObject = ws;
        IWbemServices_AddRef( iface );
        return S_OK;
    }
    if ( IsEqualGUID( riid, &IID_IClientSecurity ) )
    {
        *ppvObject = &ws->security.IClientSecurity_iface;
        IClientSecurity_AddRef( &ws->security.IClientSecurity_iface );
        return S_OK;
    }

    FIXME("interface %s not implemented\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static HRESULT WINAPI wbem_services_OpenNamespace(
    IWbemServices *iface,
    const BSTR strNamespace,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemServices **ppWorkingNamespace,
    IWbemCallResult **ppResult )
{
    static const WCHAR cimv2W[] = {'c','i','m','v','2',0};
    static const WCHAR defaultW[] = {'d','e','f','a','u','l','t',0};
    struct wbem_services *ws = impl_from_IWbemServices( iface );

    TRACE("%p, %s, 0x%08x, %p, %p, %p\n", iface, debugstr_w(strNamespace), lFlags,
          pCtx, ppWorkingNamespace, ppResult);

    if ((wcsicmp( strNamespace, cimv2W ) && wcsicmp( strNamespace, defaultW )) || ws->namespace)
        return WBEM_E_INVALID_NAMESPACE;

    return WbemServices_create( cimv2W, (void **)ppWorkingNamespace );
}

static HRESULT WINAPI wbem_services_CancelAsyncCall(
    IWbemServices *iface,
    IWbemObjectSink *pSink )
{
    struct wbem_services *services = impl_from_IWbemServices( iface );
    struct async_header *async;

    TRACE("%p, %p\n", iface, pSink);

    if (!pSink) return WBEM_E_INVALID_PARAMETER;

    EnterCriticalSection( &services->cs );

    if (!(async = services->async))
    {
        LeaveCriticalSection( &services->cs );
        return WBEM_E_INVALID_PARAMETER;
    }
    services->async = NULL;
    SetEvent( async->cancel );

    LeaveCriticalSection( &services->cs );

    WaitForSingleObject( async->wait, INFINITE );
    free_async( async );
    return S_OK;
}

static HRESULT WINAPI wbem_services_QueryObjectSink(
    IWbemServices *iface,
    LONG lFlags,
    IWbemObjectSink **ppResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

HRESULT parse_path( const WCHAR *str, struct path **ret )
{
    struct path *path;
    const WCHAR *p = str, *q;
    UINT len;

    if (!(path = heap_alloc_zero( sizeof(*path) ))) return E_OUTOFMEMORY;

    if (*p == '\\')
    {
        static const WCHAR cimv2W[] = {'R','O','O','T','\\','C','I','M','V','2',0};
        WCHAR server[MAX_COMPUTERNAME_LENGTH+1];
        DWORD server_len = ARRAY_SIZE(server);

        p++;
        if (*p != '\\')
        {
            heap_free( path );
            return WBEM_E_INVALID_OBJECT_PATH;
        }
        p++;

        q = p;
        while (*p && *p != '\\') p++;
        if (!*p)
        {
            heap_free( path );
            return WBEM_E_INVALID_OBJECT_PATH;
        }

        len = p - q;
        if (!GetComputerNameW( server, &server_len ) || server_len != len || _wcsnicmp( q, server, server_len ))
        {
            heap_free( path );
            return WBEM_E_NOT_SUPPORTED;
        }

        q = ++p;
        while (*p && *p != ':') p++;
        if (!*p)
        {
            heap_free( path );
            return WBEM_E_INVALID_OBJECT_PATH;
        }

        len = p - q;
        if (len != ARRAY_SIZE(cimv2W) - 1 || _wcsnicmp( q, cimv2W, ARRAY_SIZE(cimv2W) - 1 ))
        {
            heap_free( path );
            return WBEM_E_INVALID_NAMESPACE;
        }
        p++;
    }

    q = p;
    while (*p && *p != '.') p++;

    len = p - q;
    if (!(path->class = heap_alloc( (len + 1) * sizeof(WCHAR) )))
    {
        heap_free( path );
        return E_OUTOFMEMORY;
    }
    memcpy( path->class, q, len * sizeof(WCHAR) );
    path->class[len] = 0;
    path->class_len = len;

    if (p[0] == '.' && p[1])
    {
        q = ++p;
        while (*q) q++;

        len = q - p;
        if (!(path->filter = heap_alloc( (len + 1) * sizeof(WCHAR) )))
        {
            heap_free( path->class );
            heap_free( path );
            return E_OUTOFMEMORY;
        }
        memcpy( path->filter, p, len * sizeof(WCHAR) );
        path->filter[len] = 0;
        path->filter_len = len;
    }
    *ret = path;
    return S_OK;
}

void free_path( struct path *path )
{
    if (!path) return;
    heap_free( path->class );
    heap_free( path->filter );
    heap_free( path );
}

WCHAR *query_from_path( const struct path *path )
{
    static const WCHAR selectW[] =
        {'S','E','L','E','C','T',' ','*',' ','F','R','O','M',' ','%','s',' ',
         'W','H','E','R','E',' ','%','s',0};
    static const WCHAR select_allW[] =
        {'S','E','L','E','C','T',' ','*',' ','F','R','O','M',' ',0};
    WCHAR *query;
    UINT len;

    if (path->filter)
    {
        len = path->class_len + path->filter_len + ARRAY_SIZE(selectW);
        if (!(query = heap_alloc( len * sizeof(WCHAR) ))) return NULL;
        swprintf( query, selectW, path->class, path->filter );
    }
    else
    {
        len = path->class_len + ARRAY_SIZE(select_allW);
        if (!(query = heap_alloc( len * sizeof(WCHAR) ))) return NULL;
        lstrcpyW( query, select_allW );
        lstrcatW( query, path->class );
    }
    return query;
}

static HRESULT create_instance_enum( const struct path *path, struct client_security *parent_security,
                                     IEnumWbemClassObject **iter )
{
    WCHAR *query;
    HRESULT hr;

    if (!(query = query_from_path( path ))) return E_OUTOFMEMORY;
    hr = exec_query( query, parent_security, iter );
    heap_free( query );
    return hr;
}

HRESULT get_object( const WCHAR *object_path, struct client_security *parent_security,
                    IWbemClassObject **obj )
{
    IEnumWbemClassObject *iter;
    struct path *path;
    HRESULT hr;

    hr = parse_path( object_path, &path );
    if (hr != S_OK) return hr;

    hr = create_instance_enum( path, parent_security, &iter );
    if (hr != S_OK)
    {
        free_path( path );
        return hr;
    }
    hr = create_class_object( path->class, iter, 0, NULL, parent_security, obj );
    IEnumWbemClassObject_Release( iter );
    free_path( path );
    return hr;
}

static HRESULT WINAPI wbem_services_GetObject(
    IWbemServices *iface,
    const BSTR strObjectPath,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemClassObject **ppObject,
    IWbemCallResult **ppCallResult )
{
    struct wbem_services *services = impl_from_IWbemServices( iface );

    TRACE("%p, %s, 0x%08x, %p, %p, %p\n", iface, debugstr_w(strObjectPath), lFlags,
          pCtx, ppObject, ppCallResult);

    if (lFlags) FIXME("unsupported flags 0x%08x\n", lFlags);

    if (!strObjectPath || !strObjectPath[0])
        return create_class_object( NULL, NULL, 0, NULL, &services->security, ppObject );

    return get_object( strObjectPath, &services->security, ppObject );
}

static HRESULT WINAPI wbem_services_GetObjectAsync(
    IWbemServices *iface,
    const BSTR strObjectPath,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_PutClass(
    IWbemServices *iface,
    IWbemClassObject *pObject,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemCallResult **ppCallResult )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_PutClassAsync(
    IWbemServices *iface,
    IWbemClassObject *pObject,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_DeleteClass(
    IWbemServices *iface,
    const BSTR strClass,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemCallResult **ppCallResult )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_DeleteClassAsync(
    IWbemServices *iface,
    const BSTR strClass,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_CreateClassEnum(
    IWbemServices *iface,
    const BSTR strSuperclass,
    LONG lFlags,
    IWbemContext *pCtx,
    IEnumWbemClassObject **ppEnum )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_CreateClassEnumAsync(
    IWbemServices *iface,
    const BSTR strSuperclass,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_PutInstance(
    IWbemServices *iface,
    IWbemClassObject *pInst,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemCallResult **ppCallResult )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_PutInstanceAsync(
    IWbemServices *iface,
    IWbemClassObject *pInst,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_DeleteInstance(
    IWbemServices *iface,
    const BSTR strObjectPath,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemCallResult **ppCallResult )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_DeleteInstanceAsync(
    IWbemServices *iface,
    const BSTR strObjectPath,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_CreateInstanceEnum(
    IWbemServices *iface,
    const BSTR strClass,
    LONG lFlags,
    IWbemContext *pCtx,
    IEnumWbemClassObject **ppEnum )
{
    struct wbem_services *services = impl_from_IWbemServices( iface );
    struct path *path;
    HRESULT hr;

    TRACE("%p, %s, 0x%08x, %p, %p\n", iface, debugstr_w(strClass), lFlags, pCtx, ppEnum);

    if (lFlags) FIXME("unsupported flags 0x%08x\n", lFlags);

    hr = parse_path( strClass, &path );
    if (hr != S_OK) return hr;

    hr = create_instance_enum( path, &services->security, ppEnum );
    free_path( path );
    return hr;
}

static HRESULT WINAPI wbem_services_CreateInstanceEnumAsync(
    IWbemServices *iface,
    const BSTR strFilter,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_ExecQuery(
    IWbemServices *iface,
    const BSTR strQueryLanguage,
    const BSTR strQuery,
    LONG lFlags,
    IWbemContext *pCtx,
    IEnumWbemClassObject **ppEnum )
{
    static const WCHAR wqlW[] = {'W','Q','L',0};
    struct wbem_services *ws = impl_from_IWbemServices( iface );

    TRACE("%p, %s, %s, 0x%08x, %p, %p\n", iface, debugstr_w(strQueryLanguage),
          debugstr_w(strQuery), lFlags, pCtx, ppEnum);

    if (!strQueryLanguage || !strQuery || !strQuery[0]) return WBEM_E_INVALID_PARAMETER;
    if (wcsicmp( strQueryLanguage, wqlW )) return WBEM_E_INVALID_QUERY_TYPE;
    return exec_query( strQuery, &ws->security, ppEnum );
}

static void async_exec_query( struct async_header *hdr )
{
    struct async_query *query = (struct async_query *)hdr;
    IEnumWbemClassObject *result;
    IWbemClassObject *obj;
    ULONG count;
    HRESULT hr;

    hr = exec_query( query->str, query->security, &result );
    if (hr == S_OK)
    {
        for (;;)
        {
            IEnumWbemClassObject_Next( result, WBEM_INFINITE, 1, &obj, &count );
            if (!count) break;
            IWbemObjectSink_Indicate( query->hdr.sink, 1, &obj );
            IWbemClassObject_Release( obj );
        }
        IEnumWbemClassObject_Release( result );
    }
    IWbemObjectSink_SetStatus( query->hdr.sink, WBEM_STATUS_COMPLETE, hr, NULL, NULL );
    heap_free( query->str );
}

static HRESULT WINAPI wbem_services_ExecQueryAsync(
    IWbemServices *iface,
    const BSTR strQueryLanguage,
    const BSTR strQuery,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    struct wbem_services *services = impl_from_IWbemServices( iface );
    IWbemObjectSink *sink;
    HRESULT hr = E_OUTOFMEMORY;
    struct async_header *async;
    struct async_query *query;

    TRACE("%p, %s, %s, 0x%08x, %p, %p\n", iface, debugstr_w(strQueryLanguage), debugstr_w(strQuery),
          lFlags, pCtx, pResponseHandler);

    if (!pResponseHandler) return WBEM_E_INVALID_PARAMETER;

    hr = IWbemObjectSink_QueryInterface( pResponseHandler, &IID_IWbemObjectSink, (void **)&sink );
    if (FAILED(hr)) return hr;

    EnterCriticalSection( &services->cs );

    if (services->async)
    {
        FIXME("handle more than one pending async\n");
        hr = WBEM_E_FAILED;
        goto done;
    }
    if (!(query = heap_alloc_zero( sizeof(*query) ))) goto done;
    async = (struct async_header *)query;
    query->security = &services->security;

    if (!(init_async( async, sink, async_exec_query )))
    {
        free_async( async );
        goto done;
    }
    if (!(query->str = heap_strdupW( strQuery )))
    {
        free_async( async );
        goto done;
    }
    hr = queue_async( async );
    if (hr == S_OK) services->async = async;
    else
    {
        heap_free( query->str );
        free_async( async );
    }

done:
    LeaveCriticalSection( &services->cs );
    IWbemObjectSink_Release( sink );
    return hr;
}

static HRESULT WINAPI wbem_services_ExecNotificationQuery(
    IWbemServices *iface,
    const BSTR strQueryLanguage,
    const BSTR strQuery,
    LONG lFlags,
    IWbemContext *pCtx,
    IEnumWbemClassObject **ppEnum )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static HRESULT WINAPI wbem_services_ExecNotificationQueryAsync(
    IWbemServices *iface,
    const BSTR strQueryLanguage,
    const BSTR strQuery,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemObjectSink *pResponseHandler )
{
    struct wbem_services *services = impl_from_IWbemServices( iface );
    IWbemObjectSink *sink;
    HRESULT hr = E_OUTOFMEMORY;
    struct async_header *async;
    struct async_query *query;

    TRACE("%p, %s, %s, 0x%08x, %p, %p\n", iface, debugstr_w(strQueryLanguage), debugstr_w(strQuery),
          lFlags, pCtx, pResponseHandler);

    if (!pResponseHandler) return WBEM_E_INVALID_PARAMETER;

    hr = IWbemObjectSink_QueryInterface( pResponseHandler, &IID_IWbemObjectSink, (void **)&sink );
    if (FAILED(hr)) return hr;

    EnterCriticalSection( &services->cs );

    if (services->async)
    {
        FIXME("handle more than one pending async\n");
        hr = WBEM_E_FAILED;
        goto done;
    }
    if (!(query = heap_alloc_zero( sizeof(*query) ))) goto done;
    async = (struct async_header *)query;

    if (!(init_async( async, sink, async_exec_query )))
    {
        free_async( async );
        goto done;
    }
    if (!(query->str = heap_strdupW( strQuery )))
    {
        free_async( async );
        goto done;
    }
    hr = queue_async( async );
    if (hr == S_OK) services->async = async;
    else
    {
        heap_free( query->str );
        free_async( async );
    }

done:
    LeaveCriticalSection( &services->cs );
    IWbemObjectSink_Release( sink );
    return hr;
}

static HRESULT WINAPI wbem_services_ExecMethod(
    IWbemServices *iface,
    const BSTR strObjectPath,
    const BSTR strMethodName,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemClassObject *pInParams,
    IWbemClassObject **ppOutParams,
    IWbemCallResult **ppCallResult )
{
    IEnumWbemClassObject *result = NULL;
    IWbemClassObject *obj = NULL;
    struct query *query = NULL;
    struct path *path;
    WCHAR *str;
    class_method *func;
    struct table *table;
    HRESULT hr;
    struct wbem_services *services = impl_from_IWbemServices( iface );

    TRACE("%p, %s, %s, %08x, %p, %p, %p, %p\n", iface, debugstr_w(strObjectPath),
          debugstr_w(strMethodName), lFlags, pCtx, pInParams, ppOutParams, ppCallResult);

    if (lFlags) FIXME("flags %08x not supported\n", lFlags);

    if ((hr = parse_path( strObjectPath, &path )) != S_OK) return hr;
    if (!(str = query_from_path( path )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (!(query = create_query()))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    hr = parse_query( str, &query->view, &query->mem );
    if (hr != S_OK) goto done;

    hr = execute_view( query->view );
    if (hr != S_OK) goto done;

    hr = EnumWbemClassObject_create( query, &services->security, (void **)&result );
    if (hr != S_OK) goto done;

    table = get_view_table( query->view, 0 );
    hr = create_class_object( table->name, result, 0, NULL, &services->security, &obj );
    if (hr != S_OK) goto done;

    hr = get_method( table, strMethodName, &func );
    if (hr != S_OK) goto done;

    hr = func( obj, pInParams, ppOutParams );

done:
    if (result) IEnumWbemClassObject_Release( result );
    if (obj) IWbemClassObject_Release( obj );
    free_query( query );
    free_path( path );
    heap_free( str );
    return hr;
}

static HRESULT WINAPI wbem_services_ExecMethodAsync(
    IWbemServices *iface,
    const BSTR strObjectPath,
    const BSTR strMethodName,
    LONG lFlags,
    IWbemContext *pCtx,
    IWbemClassObject *pInParams,
    IWbemObjectSink *pResponseHandler )
{
    FIXME("\n");
    return WBEM_E_FAILED;
}

static const IWbemServicesVtbl wbem_services_vtbl =
{
    wbem_services_QueryInterface,
    wbem_services_AddRef,
    wbem_services_Release,
    wbem_services_OpenNamespace,
    wbem_services_CancelAsyncCall,
    wbem_services_QueryObjectSink,
    wbem_services_GetObject,
    wbem_services_GetObjectAsync,
    wbem_services_PutClass,
    wbem_services_PutClassAsync,
    wbem_services_DeleteClass,
    wbem_services_DeleteClassAsync,
    wbem_services_CreateClassEnum,
    wbem_services_CreateClassEnumAsync,
    wbem_services_PutInstance,
    wbem_services_PutInstanceAsync,
    wbem_services_DeleteInstance,
    wbem_services_DeleteInstanceAsync,
    wbem_services_CreateInstanceEnum,
    wbem_services_CreateInstanceEnumAsync,
    wbem_services_ExecQuery,
    wbem_services_ExecQueryAsync,
    wbem_services_ExecNotificationQuery,
    wbem_services_ExecNotificationQueryAsync,
    wbem_services_ExecMethod,
    wbem_services_ExecMethodAsync
};

HRESULT WbemServices_create( const WCHAR *namespace, LPVOID *ppObj )
{
    struct wbem_services *ws;
    HRESULT hr;

    TRACE("(%p)\n", ppObj);

    ws = heap_alloc( sizeof(*ws) );
    if (!ws) return E_OUTOFMEMORY;

    ws->IWbemServices_iface.lpVtbl = &wbem_services_vtbl;
    ws->refs      = 1;
    ws->namespace = heap_strdupW( namespace );
    if (!ws->namespace)
    {
        heap_free( ws );
        return E_OUTOFMEMORY;
    }
    ws->async     = NULL;
    InitializeCriticalSection( &ws->cs );
    ws->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": wbemprox_services.cs");

    hr = client_security_init( &ws->security, (IUnknown *)&ws->IWbemServices_iface, NULL );
    if (FAILED( hr ))
    {
        ws->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &ws->cs );
        heap_free( ws->namespace );
        heap_free( ws );
        return hr;
    }

    *ppObj = &ws->IWbemServices_iface;

    TRACE("returning iface %p\n", *ppObj);
    return S_OK;
}
