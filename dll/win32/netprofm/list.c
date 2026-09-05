/*
 * Copyright 2014 Hans Leidekker for CodeWeavers
 * Copyright 2015 Michael Müller
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
#include "windef.h"
#include "winbase.h"
#include "winsock2.h"
#include "ws2ipdef.h"
#include "iphlpapi.h"
#include "ifdef.h"
#include "ipifcons.h"
#include "netioapi.h"
#include "initguid.h"
#include "objbase.h"
#include "ocidl.h"
#include "netlistmgr.h"
#include "olectl.h"
#include "winreg.h"

#include "wine/debug.h"
#include "wine/list.h"
#include "netprofm_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(netprofm);

struct network
{
    INetwork             INetwork_iface;
    LONG                 refs;
    struct list          entry;
    GUID                 id;
    INetworkListManager *mgr;
    VARIANT_BOOL         connected_to_internet_v4;
    VARIANT_BOOL         connected_to_internet_v6;
    VARIANT_BOOL         connected_v4;
    VARIANT_BOOL         connected_v6;
};

struct connection
{
    INetworkConnection     INetworkConnection_iface;
    INetworkConnectionCost INetworkConnectionCost_iface;
    LONG                   refs;
    struct list            entry;
    GUID                   id;
    GUID                   adapter_id;
    INetworkListManager   *mgr;
    INetwork              *network;
    VARIANT_BOOL           connected_to_internet_v4;
    VARIANT_BOOL           connected_to_internet_v6;
    VARIANT_BOOL           connected_v4;
    VARIANT_BOOL           connected_v6;
};

struct connection_point
{
    IConnectionPoint IConnectionPoint_iface;
    IConnectionPointContainer *container;
    IID iid;
    struct list sinks;
    DWORD cookie;
};

struct list_manager
{
    INetworkListManager INetworkListManager_iface;
    INetworkCostManager INetworkCostManager_iface;
    IConnectionPointContainer IConnectionPointContainer_iface;
    LONG                refs;
    CRITICAL_SECTION    cs;
    HANDLE              iface_change;
    HANDLE              addr_change;
    HANDLE              route_change;
    NLM_CONNECTIVITY    connectivity;
    struct list         networks;
    struct list         connections;
    struct connection_point list_mgr_cp;
    struct connection_point cost_mgr_cp;
    struct connection_point conn_mgr_cp;
    struct connection_point events_cp;
};

struct sink_entry
{
    struct list entry;
    DWORD cookie;
    IUnknown *unk;
};

static ULONG network_release_internal( struct network *network );
static ULONG connection_release_internal( struct connection *connection );

static NLM_CONNECTIVITY get_connectivity( VARIANT_BOOL connected_v4, VARIANT_BOOL internet_v4,
                                          VARIANT_BOOL connected_v6, VARIANT_BOOL internet_v6 )
{
    NLM_CONNECTIVITY ret = NLM_CONNECTIVITY_DISCONNECTED;

    if (internet_v4) ret |= NLM_CONNECTIVITY_IPV4_INTERNET;
    else if (connected_v4) ret |= NLM_CONNECTIVITY_IPV4_LOCALNETWORK;

    if (internet_v6) ret |= NLM_CONNECTIVITY_IPV6_INTERNET;
    else if (connected_v6) ret |= NLM_CONNECTIVITY_IPV6_LOCALNETWORK;

    return ret;
}

static NLM_CONNECTIVITY network_connectivity( const struct network *network )
{
    return get_connectivity( network->connected_v4, network->connected_to_internet_v4,
                             network->connected_v6, network->connected_to_internet_v6 );
}

static inline struct list_manager *impl_from_IConnectionPointContainer(IConnectionPointContainer *iface)
{
    return CONTAINING_RECORD(iface, struct list_manager, IConnectionPointContainer_iface);
}

static inline struct list_manager *impl_from_INetworkCostManager(
    INetworkCostManager *iface )
{
    return CONTAINING_RECORD( iface, struct list_manager, INetworkCostManager_iface );
}

static inline struct connection_point *impl_from_IConnectionPoint(
    IConnectionPoint *iface )
{
    return CONTAINING_RECORD( iface, struct connection_point, IConnectionPoint_iface );
}

static HRESULT WINAPI connection_point_QueryInterface(
    IConnectionPoint *iface,
    REFIID riid,
    void **obj )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %s, %p\n", cp, debugstr_guid(riid), obj );

    if (IsEqualGUID( riid, &IID_IConnectionPoint ) ||
        IsEqualGUID( riid, &IID_IUnknown ))
    {
        *obj = iface;
    }
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
    IConnectionPoint_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI connection_point_AddRef(
    IConnectionPoint *iface )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    return IConnectionPointContainer_AddRef( cp->container );
}

static ULONG WINAPI connection_point_Release(
    IConnectionPoint *iface )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    return IConnectionPointContainer_Release( cp->container );
}

static HRESULT WINAPI connection_point_GetConnectionInterface(
    IConnectionPoint *iface,
    IID *iid )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %p\n", cp, iid );

    if (!iid)
        return E_POINTER;

    memcpy( iid, &cp->iid, sizeof(*iid) );
    return S_OK;
}

static HRESULT WINAPI connection_point_GetConnectionPointContainer(
    IConnectionPoint *iface,
    IConnectionPointContainer **container )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %p\n", cp, container );

    if (!container)
        return E_POINTER;

    IConnectionPointContainer_AddRef( cp->container );
    *container = cp->container;
    return S_OK;
}

static HRESULT WINAPI connection_point_Advise(
    IConnectionPoint *iface,
    IUnknown *sink,
    DWORD *cookie )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    struct list_manager *mgr = impl_from_IConnectionPointContainer( cp->container );
    struct sink_entry *sink_entry;
    IUnknown *unk;
    HRESULT hr;

    TRACE( "%p, %p, %p\n", cp, sink, cookie );

    if (!sink || !cookie)
        return E_POINTER;

    hr = IUnknown_QueryInterface( sink, &cp->iid, (void**)&unk );
    if (FAILED(hr))
    {
        WARN( "iface %s not implemented by sink\n", debugstr_guid(&cp->iid) );
        return CONNECT_E_CANNOTCONNECT;
    }

    sink_entry = malloc( sizeof(*sink_entry) );
    if (!sink_entry)
    {
        IUnknown_Release( unk );
        return E_OUTOFMEMORY;
    }

    EnterCriticalSection( &mgr->cs );
    sink_entry->unk = unk;
    *cookie = sink_entry->cookie = ++cp->cookie;
    list_add_tail( &cp->sinks, &sink_entry->entry );
    LeaveCriticalSection( &mgr->cs );
    return S_OK;
}

static void sink_entry_release( struct sink_entry *entry )
{
    list_remove( &entry->entry );
    IUnknown_Release( entry->unk );
    free( entry );
}

static HRESULT WINAPI connection_point_Unadvise(
    IConnectionPoint *iface,
    DWORD cookie )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    struct list_manager *mgr = impl_from_IConnectionPointContainer( cp->container );
    struct sink_entry *iter;

    TRACE( "%p, %ld\n", cp, cookie );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( iter, &cp->sinks, struct sink_entry, entry )
    {
        if (iter->cookie != cookie) continue;
        sink_entry_release( iter );
        LeaveCriticalSection( &mgr->cs );
        return S_OK;
    }
    LeaveCriticalSection( &mgr->cs );

    WARN( "invalid cookie\n" );
    return CONNECT_E_NOCONNECTION;
}

static HRESULT WINAPI connection_point_EnumConnections(
    IConnectionPoint *iface,
    IEnumConnections **connections )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    FIXME( "%p, %p - stub\n", cp, connections );

    return E_NOTIMPL;
}

static const IConnectionPointVtbl connection_point_vtbl =
{
    connection_point_QueryInterface,
    connection_point_AddRef,
    connection_point_Release,
    connection_point_GetConnectionInterface,
    connection_point_GetConnectionPointContainer,
    connection_point_Advise,
    connection_point_Unadvise,
    connection_point_EnumConnections
};

static void connection_point_init(
    struct connection_point *cp,
    REFIID riid,
    IConnectionPointContainer *container )
{
    cp->IConnectionPoint_iface.lpVtbl = &connection_point_vtbl;
    cp->container = container;
    cp->cookie = 0;
    cp->iid = *riid;
    list_init( &cp->sinks );
}

static void connection_point_release( struct connection_point *cp )
{
    while (!list_empty( &cp->sinks ))
        sink_entry_release( LIST_ENTRY( list_head( &cp->sinks ), struct sink_entry, entry ) );
}

static inline struct network *impl_from_INetwork(
    INetwork *iface )
{
    return CONTAINING_RECORD( iface, struct network, INetwork_iface );
}

static HRESULT WINAPI network_QueryInterface(
    INetwork *iface, REFIID riid, void **obj )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %s, %p\n", network, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_INetwork ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
        INetwork_AddRef( iface );
        return S_OK;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI network_AddRef(
    INetwork *iface )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p\n", network );
    INetworkListManager_AddRef( network->mgr );
    return InterlockedIncrement( &network->refs );
}

static ULONG network_release_internal( struct network *network )
{
    LONG refs;

    if (!(refs = InterlockedDecrement( &network->refs )))
    {
        list_remove( &network->entry );
        free( network );
    }
    return refs;
}

static ULONG WINAPI network_Release(
    INetwork *iface )
{
    struct network *network = impl_from_INetwork( iface );
    INetworkListManager *mgr = network->mgr;
    ULONG refs;

    TRACE( "%p\n", network );
    refs = network_release_internal( network );
    INetworkListManager_Release( mgr );
    return refs;
}

static HRESULT WINAPI network_GetTypeInfoCount(
    INetwork *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI network_GetTypeInfo(
    INetwork *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI network_GetIDsOfNames(
    INetwork *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI network_Invoke(
    INetwork *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static const WCHAR profiles_keyW[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\Profiles";

static LONG open_profile_key( const GUID *id, REGSAM access, BOOL create, HKEY *key )
{
    WCHAR path[sizeof(profiles_keyW) / sizeof(WCHAR) + 40];
    swprintf( path, sizeof(path) / sizeof(path[0]), L"%s\\{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", profiles_keyW,
              id->Data1, id->Data2, id->Data3, id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
              id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7] );
    if (create)
        return RegCreateKeyExW( HKEY_LOCAL_MACHINE, path, 0, NULL, 0, access, NULL, key, NULL );
    return RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0, access, key );
}

static HRESULT read_profile_string( const GUID *id, const WCHAR *value, const WCHAR *fallback, BSTR *ret )
{
    WCHAR buf[256];
    DWORD type, size = sizeof(buf);
    HKEY key;

    buf[0] = 0;
    if (!open_profile_key( id, KEY_QUERY_VALUE, FALSE, &key ))
    {
        if (RegQueryValueExW( key, value, NULL, &type, (BYTE *)buf, &size ) || type != REG_SZ) buf[0] = 0;
        RegCloseKey( key );
    }
    buf[sizeof(buf) / sizeof(buf[0]) - 1] = 0;
    if (!buf[0] && fallback) lstrcpynW( buf, fallback, sizeof(buf) / sizeof(buf[0]) );
    if (!(*ret = SysAllocString( buf ))) return E_OUTOFMEMORY;
    return S_OK;
}

static HRESULT write_profile_string( const GUID *id, const WCHAR *value, const WCHAR *str )
{
    HKEY key;
    LONG ret;

    if (!str) return E_POINTER;
    if ((ret = open_profile_key( id, KEY_SET_VALUE, TRUE, &key ))) return HRESULT_FROM_WIN32( ret );
    ret = RegSetValueExW( key, value, 0, REG_SZ, (const BYTE *)str, (lstrlenW( str ) + 1) * sizeof(WCHAR) );
    RegCloseKey( key );
    return ret ? HRESULT_FROM_WIN32( ret ) : S_OK;
}

static HRESULT WINAPI network_GetName(
    INetwork *iface,
    BSTR *pszNetworkName )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %p\n", iface, pszNetworkName );

    if (!pszNetworkName) return E_POINTER;
    return read_profile_string( &network->id, L"ProfileName", L"Network", pszNetworkName );
}

static HRESULT WINAPI network_SetName(
    INetwork *iface,
    BSTR szNetworkNewName )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %s\n", iface, debugstr_w(szNetworkNewName) );

    if (!szNetworkNewName || !szNetworkNewName[0]) return E_INVALIDARG;
    return write_profile_string( &network->id, L"ProfileName", szNetworkNewName );
}

static HRESULT WINAPI network_GetDescription(
    INetwork *iface,
    BSTR *pszDescription )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %p\n", iface, pszDescription );

    if (!pszDescription) return E_POINTER;
    return read_profile_string( &network->id, L"Description", L"", pszDescription );
}

static HRESULT WINAPI network_SetDescription(
    INetwork *iface,
    BSTR szDescription )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %s\n", iface, debugstr_w(szDescription) );

    return write_profile_string( &network->id, L"Description", szDescription ? szDescription : L"" );
}

static HRESULT WINAPI network_GetNetworkId(
    INetwork *iface,
    GUID *pgdGuidNetworkId )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %p\n", iface, pgdGuidNetworkId );

    *pgdGuidNetworkId = network->id;
    return S_OK;
}

static HRESULT WINAPI network_GetDomainType(
    INetwork *iface,
    NLM_DOMAIN_TYPE *pDomainType )
{
    FIXME( "%p, %p\n", iface, pDomainType );

    *pDomainType = NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK;
    return S_OK;
}

static inline struct list_manager *impl_from_INetworkListManager(
    INetworkListManager *iface )
{
    return CONTAINING_RECORD( iface, struct list_manager, INetworkListManager_iface );
}

static HRESULT create_connections_enum(
    struct list_manager *, IEnumNetworkConnections** );

static HRESULT WINAPI network_GetNetworkConnections(
    INetwork *iface,
    IEnumNetworkConnections **ppEnum )
{
    struct network *network = impl_from_INetwork( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( network->mgr );

    TRACE( "%p, %p\n", iface, ppEnum );
    return create_connections_enum( mgr, ppEnum );
}

static HRESULT WINAPI network_GetTimeCreatedAndConnected(
    INetwork *iface,
    DWORD *pdwLowDateTimeCreated,
    DWORD *pdwHighDateTimeCreated,
    DWORD *pdwLowDateTimeConnected,
    DWORD *pdwHighDateTimeConnected )
{
    FIXME( "%p, %p, %p, %p, %p\n", iface, pdwLowDateTimeCreated, pdwHighDateTimeCreated,
        pdwLowDateTimeConnected, pdwHighDateTimeConnected );
    return E_NOTIMPL;
}

static HRESULT WINAPI network_get_IsConnectedToInternet(
    INetwork *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    *pbIsConnected = network->connected_to_internet_v4 | network->connected_to_internet_v6;
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI network_get_IsConnected(
    INetwork *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    *pbIsConnected = network->connected_v4 | network->connected_v6;
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI network_GetConnectivity(
    INetwork *iface,
    NLM_CONNECTIVITY *pConnectivity )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %p\n", iface, pConnectivity );

    *pConnectivity = network_connectivity( network );

    TRACE( "<- %#x\n", *pConnectivity );
    return S_OK;
}

static HRESULT WINAPI network_GetCategory(
    INetwork *iface,
    NLM_NETWORK_CATEGORY *pCategory )
{
    struct network *network = impl_from_INetwork( iface );
    DWORD type, value = NLM_NETWORK_CATEGORY_PUBLIC, size = sizeof(value);
    HKEY key;

    TRACE( "%p, %p\n", iface, pCategory );

    if (!pCategory) return E_POINTER;
    if (!open_profile_key( &network->id, KEY_QUERY_VALUE, FALSE, &key ))
    {
        if (RegQueryValueExW( key, L"Category", NULL, &type, (BYTE *)&value, &size ) || type != REG_DWORD ||
            value > NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED)
            value = NLM_NETWORK_CATEGORY_PUBLIC;
        RegCloseKey( key );
    }
    *pCategory = value;
    return S_OK;
}

static HRESULT WINAPI network_SetCategory(
    INetwork *iface,
    NLM_NETWORK_CATEGORY NewCategory )
{
    struct network *network = impl_from_INetwork( iface );
    DWORD value = NewCategory;
    HKEY key;
    LONG ret;

    TRACE( "%p, %u\n", iface, NewCategory );

    if (NewCategory > NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED) return E_INVALIDARG;
    if ((ret = open_profile_key( &network->id, KEY_SET_VALUE, TRUE, &key ))) return HRESULT_FROM_WIN32( ret );
    ret = RegSetValueExW( key, L"Category", 0, REG_DWORD, (const BYTE *)&value, sizeof(value) );
    RegCloseKey( key );
    return ret ? HRESULT_FROM_WIN32( ret ) : S_OK;
}

static const struct INetworkVtbl network_vtbl =
{
    network_QueryInterface,
    network_AddRef,
    network_Release,
    network_GetTypeInfoCount,
    network_GetTypeInfo,
    network_GetIDsOfNames,
    network_Invoke,
    network_GetName,
    network_SetName,
    network_GetDescription,
    network_SetDescription,
    network_GetNetworkId,
    network_GetDomainType,
    network_GetNetworkConnections,
    network_GetTimeCreatedAndConnected,
    network_get_IsConnectedToInternet,
    network_get_IsConnected,
    network_GetConnectivity,
    network_GetCategory,
    network_SetCategory
};

static struct network *create_network( struct list_manager *mgr, const GUID *id )
{
    struct network *ret;

    if (!(ret = calloc( 1, sizeof(*ret) ))) return NULL;

    ret->INetwork_iface.lpVtbl = &network_vtbl;
    ret->refs = 1;
    ret->id   = *id;
    ret->mgr  = &mgr->INetworkListManager_iface;
    list_init( &ret->entry );

    return ret;
}

static HRESULT WINAPI cost_manager_QueryInterface(
    INetworkCostManager *iface,
    REFIID riid,
    void **obj )
{
    struct list_manager *mgr = impl_from_INetworkCostManager( iface );
    return INetworkListManager_QueryInterface( &mgr->INetworkListManager_iface, riid, obj );
}

static ULONG WINAPI cost_manager_AddRef(
    INetworkCostManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkCostManager( iface );
    return INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
}

static ULONG WINAPI cost_manager_Release(
    INetworkCostManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkCostManager( iface );
    return INetworkListManager_Release( &mgr->INetworkListManager_iface );
}

static HRESULT WINAPI cost_manager_GetCost(
    INetworkCostManager *iface, DWORD *pCost, NLM_SOCKADDR *pDestIPAddr)
{
    FIXME( "%p, %p, %p\n", iface, pCost, pDestIPAddr );

    if (!pCost) return E_POINTER;

    *pCost = NLM_CONNECTION_COST_UNRESTRICTED;
    return S_OK;
}

static BOOL map_address_6to4( const SOCKADDR_IN6 *addr6, SOCKADDR_IN *addr4 )
{
    ULONG i;

    if (addr6->sin6_family != AF_INET6) return FALSE;

    for (i = 0; i < 5; i++)
        if (addr6->sin6_addr.u.Word[i]) return FALSE;

    if (addr6->sin6_addr.u.Word[5] != 0xffff) return FALSE;

    addr4->sin_family = AF_INET;
    addr4->sin_port   = addr6->sin6_port;
    addr4->sin_addr.S_un.S_addr = addr6->sin6_addr.u.Word[6] << 16 | addr6->sin6_addr.u.Word[7];
    memset( &addr4->sin_zero, 0, sizeof(addr4->sin_zero) );

    return TRUE;
}

static HRESULT WINAPI cost_manager_GetDataPlanStatus(
    INetworkCostManager *iface, NLM_DATAPLAN_STATUS *pDataPlanStatus,
    NLM_SOCKADDR *pDestIPAddr)
{
    DWORD ret, index;
    NET_LUID luid;
    SOCKADDR *dst = (SOCKADDR *)pDestIPAddr;
    SOCKADDR_IN addr4, *dst4;

    FIXME( "%p, %p, %p\n", iface, pDataPlanStatus, pDestIPAddr );

    if (!pDataPlanStatus) return E_POINTER;

    if (dst && ((dst->sa_family == AF_INET && (dst4 = (SOCKADDR_IN *)dst)) ||
               ((dst->sa_family == AF_INET6 && map_address_6to4( (const SOCKADDR_IN6 *)dst, &addr4 )
                && (dst4 = &addr4)))))
    {
        if ((ret = GetBestInterface( dst4->sin_addr.S_un.S_addr, &index )))
            return HRESULT_FROM_WIN32( ret );

        if ((ret = ConvertInterfaceIndexToLuid( index, &luid )))
            return HRESULT_FROM_WIN32( ret );

        if ((ret = ConvertInterfaceLuidToGuid( &luid, &pDataPlanStatus->InterfaceGuid )))
            return HRESULT_FROM_WIN32( ret );
    }
    else
    {
        FIXME( "interface guid not found\n" );
        memset( &pDataPlanStatus->InterfaceGuid, 0, sizeof(pDataPlanStatus->InterfaceGuid) );
    }

    pDataPlanStatus->UsageData.UsageInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->UsageData.LastSyncTime, 0, sizeof(pDataPlanStatus->UsageData.LastSyncTime) );
    pDataPlanStatus->DataLimitInMegabytes       = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->InboundBandwidthInKbps     = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->OutboundBandwidthInKbps    = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->NextBillingCycle, 0, sizeof(pDataPlanStatus->NextBillingCycle) );
    pDataPlanStatus->MaxTransferSizeInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->Reserved                   = 0;

    return S_OK;
}

static HRESULT WINAPI cost_manager_SetDestinationAddresses(
    INetworkCostManager *iface, UINT32 length, NLM_SOCKADDR *pDestIPAddrList,
    VARIANT_BOOL bAppend)
{
    FIXME( "%p, %u, %p, %x\n", iface, length, pDestIPAddrList, bAppend );
    return E_NOTIMPL;
}

static const INetworkCostManagerVtbl cost_manager_vtbl =
{
    cost_manager_QueryInterface,
    cost_manager_AddRef,
    cost_manager_Release,
    cost_manager_GetCost,
    cost_manager_GetDataPlanStatus,
    cost_manager_SetDestinationAddresses
};

struct networks_enum
{
    IEnumNetworks        IEnumNetworks_iface;
    LONG                 refs;
    struct list_manager *mgr;
    INetwork           **networks;
    ULONG                count;
    ULONG                pos;
    NLM_ENUM_NETWORK     flags;
};

static inline struct networks_enum *impl_from_IEnumNetworks(
    IEnumNetworks *iface )
{
    return CONTAINING_RECORD( iface, struct networks_enum, IEnumNetworks_iface );
}

static HRESULT WINAPI networks_enum_QueryInterface(
    IEnumNetworks *iface, REFIID riid, void **obj )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p, %s, %p\n", iter, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_IEnumNetworks ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
        IEnumNetworks_AddRef( iface );
        return S_OK;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI networks_enum_AddRef(
    IEnumNetworks *iface )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p\n", iter );
    return InterlockedIncrement( &iter->refs );
}

static ULONG WINAPI networks_enum_Release(
    IEnumNetworks *iface )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );
    LONG refs;

    TRACE( "%p\n", iter );

    if (!(refs = InterlockedDecrement( &iter->refs )))
    {
        ULONG i;

        for (i = 0; i < iter->count; i++) INetwork_Release( iter->networks[i] );
        free( iter->networks );
        INetworkListManager_Release( &iter->mgr->INetworkListManager_iface );
        free( iter );
    }
    return refs;
}

static HRESULT WINAPI networks_enum_GetTypeInfoCount(
    IEnumNetworks *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_GetTypeInfo(
    IEnumNetworks *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_GetIDsOfNames(
    IEnumNetworks *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_Invoke(
    IEnumNetworks *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_get__NewEnum(
    IEnumNetworks *iface, IEnumVARIANT **ppEnumVar )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static BOOL match_enum_network_flags( NLM_ENUM_NETWORK flags, struct network *network )
{
    if (flags == NLM_ENUM_NETWORK_ALL) return TRUE;
    if (network->connected_v4 || network->connected_v6)
    {
        if (flags & NLM_ENUM_NETWORK_CONNECTED) return TRUE;
    }
    else if (flags & NLM_ENUM_NETWORK_DISCONNECTED) return TRUE;
    return FALSE;
}

static HRESULT WINAPI networks_enum_Next(
    IEnumNetworks *iface, ULONG count, INetwork **ret, ULONG *fetched )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );
    ULONG i = 0;

    TRACE( "%p, %lu %p %p\n", iter, count, ret, fetched );

    if (!ret) return E_POINTER;
    *ret = NULL;
    if (fetched) *fetched = 0;
    if (!count) return S_OK;

    while (iter->pos < iter->count && i < count)
    {
        ret[i] = iter->networks[iter->pos++];
        INetwork_AddRef( ret[i] );
        i++;
    }
    if (fetched) *fetched = i;

    return i < count ? S_FALSE : S_OK;
}

static HRESULT WINAPI networks_enum_Skip(
    IEnumNetworks *iface, ULONG count )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p, %lu\n", iter, count);

    if (!count) return S_OK;
    if (iter->pos >= iter->count) return S_FALSE;

    if (iter->count - iter->pos < count)
    {
        iter->pos = iter->count;
        return S_FALSE;
    }
    iter->pos += count;
    return S_OK;
}

static HRESULT WINAPI networks_enum_Reset(
    IEnumNetworks *iface )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p\n", iter );

    iter->pos = 0;
    return S_OK;
}

static HRESULT create_networks_enum(
    struct list_manager *, NLM_ENUM_NETWORK, IEnumNetworks ** );

static HRESULT WINAPI networks_enum_Clone(
    IEnumNetworks *iface, IEnumNetworks **ret )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p, %p\n", iter, ret );
    return create_networks_enum( iter->mgr, iter->flags, ret );
}

static const IEnumNetworksVtbl networks_enum_vtbl =
{
    networks_enum_QueryInterface,
    networks_enum_AddRef,
    networks_enum_Release,
    networks_enum_GetTypeInfoCount,
    networks_enum_GetTypeInfo,
    networks_enum_GetIDsOfNames,
    networks_enum_Invoke,
    networks_enum_get__NewEnum,
    networks_enum_Next,
    networks_enum_Skip,
    networks_enum_Reset,
    networks_enum_Clone
};

static HRESULT create_networks_enum(
    struct list_manager *mgr, NLM_ENUM_NETWORK flags, IEnumNetworks **ret )
{
    struct networks_enum *iter;
    struct network *network;
    ULONG total = 0;

    *ret = NULL;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (match_enum_network_flags( flags, network )) total++;
    }
    if (total && !(iter->networks = calloc( total, sizeof(*iter->networks) )))
    {
        LeaveCriticalSection( &mgr->cs );
        free( iter );
        return E_OUTOFMEMORY;
    }
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (!match_enum_network_flags( flags, network )) continue;
        iter->networks[iter->count] = &network->INetwork_iface;
        INetwork_AddRef( iter->networks[iter->count++] );
    }
    LeaveCriticalSection( &mgr->cs );

    iter->IEnumNetworks_iface.lpVtbl = &networks_enum_vtbl;
    iter->mgr    = mgr;
    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    iter->flags  = flags;
    iter->refs   = 1;

    *ret = &iter->IEnumNetworks_iface;
    return S_OK;
}

struct connections_enum
{
    IEnumNetworkConnections IEnumNetworkConnections_iface;
    LONG                    refs;
    struct list_manager    *mgr;
    INetworkConnection    **connections;
    ULONG                   count;
    ULONG                   pos;
};

static inline struct connections_enum *impl_from_IEnumNetworkConnections(
    IEnumNetworkConnections *iface )
{
    return CONTAINING_RECORD( iface, struct connections_enum, IEnumNetworkConnections_iface );
}

static HRESULT WINAPI connections_enum_QueryInterface(
    IEnumNetworkConnections *iface, REFIID riid, void **obj )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p, %s, %p\n", iter, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_IEnumNetworkConnections ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
        IEnumNetworkConnections_AddRef( iface );
        return S_OK;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI connections_enum_AddRef(
    IEnumNetworkConnections *iface )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p\n", iter );
    return InterlockedIncrement( &iter->refs );
}

static ULONG WINAPI connections_enum_Release(
    IEnumNetworkConnections *iface )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );
    LONG refs;

    TRACE( "%p\n", iter );

    if (!(refs = InterlockedDecrement( &iter->refs )))
    {
        ULONG i;

        for (i = 0; i < iter->count; i++) INetworkConnection_Release( iter->connections[i] );
        free( iter->connections );
        INetworkListManager_Release( &iter->mgr->INetworkListManager_iface );
        free( iter );
    }
    return refs;
}

static HRESULT WINAPI connections_enum_GetTypeInfoCount(
    IEnumNetworkConnections *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_GetTypeInfo(
    IEnumNetworkConnections *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_GetIDsOfNames(
    IEnumNetworkConnections *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_Invoke(
    IEnumNetworkConnections *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_get__NewEnum(
    IEnumNetworkConnections *iface, IEnumVARIANT **ppEnumVar )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_Next(
    IEnumNetworkConnections *iface, ULONG count, INetworkConnection **ret, ULONG *fetched )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );
    ULONG i = 0;

    TRACE( "%p, %lu %p %p\n", iter, count, ret, fetched );

    if (!ret) return E_POINTER;
    *ret = NULL;
    if (fetched) *fetched = 0;
    if (!count) return S_OK;

    while (iter->pos < iter->count && i < count)
    {
        ret[i] = iter->connections[iter->pos++];
        INetworkConnection_AddRef( ret[i] );
        i++;
    }
    if (fetched) *fetched = i;

    return i < count ? S_FALSE : S_OK;
}

static HRESULT WINAPI connections_enum_Skip(
    IEnumNetworkConnections *iface, ULONG count )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p, %lu\n", iter, count);

    if (!count) return S_OK;
    if (iter->pos >= iter->count) return S_FALSE;

    if (iter->count - iter->pos < count)
    {
        iter->pos = iter->count;
        return S_FALSE;
    }
    iter->pos += count;
    return S_OK;
}

static HRESULT WINAPI connections_enum_Reset(
    IEnumNetworkConnections *iface )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p\n", iter );

    iter->pos = 0;
    return S_OK;
}

static HRESULT WINAPI connections_enum_Clone(
    IEnumNetworkConnections *iface, IEnumNetworkConnections **ret )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p, %p\n", iter, ret );
    return create_connections_enum( iter->mgr, ret );
}

static const IEnumNetworkConnectionsVtbl connections_enum_vtbl =
{
    connections_enum_QueryInterface,
    connections_enum_AddRef,
    connections_enum_Release,
    connections_enum_GetTypeInfoCount,
    connections_enum_GetTypeInfo,
    connections_enum_GetIDsOfNames,
    connections_enum_Invoke,
    connections_enum_get__NewEnum,
    connections_enum_Next,
    connections_enum_Skip,
    connections_enum_Reset,
    connections_enum_Clone
};

static HRESULT create_connections_enum(
    struct list_manager *mgr, IEnumNetworkConnections **ret )
{
    struct connections_enum *iter;
    struct connection *connection;
    ULONG total = 0;

    *ret = NULL;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( connection, &mgr->connections, struct connection, entry ) total++;
    if (total && !(iter->connections = calloc( total, sizeof(*iter->connections) )))
    {
        LeaveCriticalSection( &mgr->cs );
        free( iter );
        return E_OUTOFMEMORY;
    }
    LIST_FOR_EACH_ENTRY( connection, &mgr->connections, struct connection, entry )
    {
        iter->connections[iter->count] = &connection->INetworkConnection_iface;
        INetworkConnection_AddRef( iter->connections[iter->count++] );
    }
    LeaveCriticalSection( &mgr->cs );

    iter->IEnumNetworkConnections_iface.lpVtbl = &connections_enum_vtbl;
    iter->mgr         = mgr;
    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    iter->refs        = 1;

    *ret = &iter->IEnumNetworkConnections_iface;
    return S_OK;
}

static ULONG WINAPI list_manager_AddRef(
    INetworkListManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    return InterlockedIncrement( &mgr->refs );
}

static ULONG WINAPI list_manager_Release(
    INetworkListManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    LONG refs = InterlockedDecrement( &mgr->refs );
    if (!refs)
    {
        struct network *network, *next_network;
        struct connection *connection, *next_connection;

        TRACE( "destroying %p\n", mgr );

        if (mgr->iface_change) CancelMibChangeNotify2( mgr->iface_change );
        if (mgr->addr_change) CancelMibChangeNotify2( mgr->addr_change );
        if (mgr->route_change) CancelMibChangeNotify2( mgr->route_change );

        connection_point_release( &mgr->events_cp );
        connection_point_release( &mgr->conn_mgr_cp );
        connection_point_release( &mgr->cost_mgr_cp );
        connection_point_release( &mgr->list_mgr_cp );
        LIST_FOR_EACH_ENTRY_SAFE( connection, next_connection, &mgr->connections, struct connection, entry )
        {
            connection_release_internal( connection );
        }
        LIST_FOR_EACH_ENTRY_SAFE( network, next_network, &mgr->networks, struct network, entry )
        {
            network_release_internal( network );
        }
        DeleteCriticalSection( &mgr->cs );
        free( mgr );
    }
    return refs;
}

static HRESULT WINAPI list_manager_QueryInterface(
    INetworkListManager *iface,
    REFIID riid,
    void **obj )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );

    TRACE( "%p, %s, %p\n", mgr, debugstr_guid(riid), obj );

    if (IsEqualGUID( riid, &IID_INetworkListManager ) ||
        IsEqualGUID( riid, &IID_IDispatch ) ||
        IsEqualGUID( riid, &IID_IUnknown ))
    {
        *obj = iface;
    }
    else if (IsEqualGUID( riid, &IID_INetworkCostManager ))
    {
        *obj = &mgr->INetworkCostManager_iface;
    }
    else if (IsEqualGUID( riid, &IID_IConnectionPointContainer ))
    {
        *obj = &mgr->IConnectionPointContainer_iface;
    }
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
    INetworkListManager_AddRef( iface );
    return S_OK;
}

static HRESULT WINAPI list_manager_GetTypeInfoCount(
    INetworkListManager *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_GetTypeInfo(
    INetworkListManager *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_GetIDsOfNames(
    INetworkListManager *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_Invoke(
    INetworkListManager *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_GetNetworks(
    INetworkListManager *iface,
    NLM_ENUM_NETWORK Flags,
    IEnumNetworks **ppEnumNetwork )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );

    TRACE( "%p, %x, %p\n", iface, Flags, ppEnumNetwork );

    return create_networks_enum( mgr, Flags, ppEnumNetwork );
}

static HRESULT WINAPI list_manager_GetNetwork(
    INetworkListManager *iface,
    GUID gdNetworkId,
    INetwork **ppNetwork )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;

    TRACE( "%p, %s, %p\n", iface, debugstr_guid(&gdNetworkId), ppNetwork );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (IsEqualGUID( &network->id, &gdNetworkId ))
        {
            *ppNetwork = &network->INetwork_iface;
            INetwork_AddRef( *ppNetwork );
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &mgr->cs );

    return S_FALSE;
}

static HRESULT WINAPI list_manager_GetNetworkConnections(
    INetworkListManager *iface,
    IEnumNetworkConnections **ppEnum )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );

    TRACE( "%p, %p\n", iface, ppEnum );
    return create_connections_enum( mgr, ppEnum );
}

static HRESULT WINAPI list_manager_GetNetworkConnection(
    INetworkListManager *iface,
    GUID gdNetworkConnectionId,
    INetworkConnection **ppNetworkConnection )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct connection *connection;

    TRACE( "%p, %s, %p\n", iface, debugstr_guid(&gdNetworkConnectionId),
            ppNetworkConnection );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( connection, &mgr->connections, struct connection, entry )
    {
        if (IsEqualGUID( &connection->id, &gdNetworkConnectionId ))
        {
            *ppNetworkConnection = &connection->INetworkConnection_iface;
            INetworkConnection_AddRef( *ppNetworkConnection );
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &mgr->cs );

    return S_FALSE;
}

static HRESULT WINAPI list_manager_IsConnectedToInternet(
    INetworkListManager *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (network->connected_to_internet_v4 || network->connected_to_internet_v6)
        {
            *pbIsConnected = VARIANT_TRUE;
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &mgr->cs );

    *pbIsConnected = VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI list_manager_IsConnected(
    INetworkListManager *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (network->connected_v4 || network->connected_v6)
        {
            *pbIsConnected = VARIANT_TRUE;
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &mgr->cs );

    *pbIsConnected = VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI list_manager_GetConnectivity(
    INetworkListManager *iface,
    NLM_CONNECTIVITY *pConnectivity )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;

    TRACE( "%p, %p\n", iface, pConnectivity );

    *pConnectivity = NLM_CONNECTIVITY_DISCONNECTED;

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        *pConnectivity |= network_connectivity( network );
    }
    LeaveCriticalSection( &mgr->cs );

    TRACE( "<- %#x\n", *pConnectivity );
    return S_OK;
}

static const INetworkListManagerVtbl list_manager_vtbl =
{
    list_manager_QueryInterface,
    list_manager_AddRef,
    list_manager_Release,
    list_manager_GetTypeInfoCount,
    list_manager_GetTypeInfo,
    list_manager_GetIDsOfNames,
    list_manager_Invoke,
    list_manager_GetNetworks,
    list_manager_GetNetwork,
    list_manager_GetNetworkConnections,
    list_manager_GetNetworkConnection,
    list_manager_IsConnectedToInternet,
    list_manager_IsConnected,
    list_manager_GetConnectivity
};

static HRESULT WINAPI ConnectionPointContainer_QueryInterface(IConnectionPointContainer *iface,
                                                              REFIID riid, void **ppv)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    return INetworkListManager_QueryInterface(&This->INetworkListManager_iface, riid, ppv);
}

static ULONG WINAPI ConnectionPointContainer_AddRef(IConnectionPointContainer *iface)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    return INetworkListManager_AddRef(&This->INetworkListManager_iface);
}

static ULONG WINAPI ConnectionPointContainer_Release(IConnectionPointContainer *iface)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    return INetworkListManager_Release(&This->INetworkListManager_iface);
}

static HRESULT WINAPI ConnectionPointContainer_EnumConnectionPoints(IConnectionPointContainer *iface,
        IEnumConnectionPoints **ppEnum)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    FIXME("(%p)->(%p): stub\n", This, ppEnum);
    return E_NOTIMPL;
}

static HRESULT WINAPI ConnectionPointContainer_FindConnectionPoint(IConnectionPointContainer *iface,
        REFIID riid, IConnectionPoint **cp)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    struct connection_point *ret;

    TRACE( "%p, %s, %p\n", This, debugstr_guid(riid), cp );

    if (!riid || !cp)
        return E_POINTER;

    if (IsEqualGUID( riid, &IID_INetworkListManagerEvents ))
        ret = &This->list_mgr_cp;
    else if (IsEqualGUID( riid, &IID_INetworkCostManagerEvents ))
        ret = &This->cost_mgr_cp;
    else if (IsEqualGUID( riid, &IID_INetworkConnectionEvents ))
        ret = &This->conn_mgr_cp;
    else if (IsEqualGUID( riid, &IID_INetworkEvents))
        ret = &This->events_cp;
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        *cp = NULL;
        return E_NOINTERFACE;
    }

    IConnectionPoint_AddRef( *cp = &ret->IConnectionPoint_iface );
    return S_OK;
}

static const struct IConnectionPointContainerVtbl cpc_vtbl =
{
    ConnectionPointContainer_QueryInterface,
    ConnectionPointContainer_AddRef,
    ConnectionPointContainer_Release,
    ConnectionPointContainer_EnumConnectionPoints,
    ConnectionPointContainer_FindConnectionPoint
};

static inline struct connection *impl_from_INetworkConnection(
    INetworkConnection *iface )
{
    return CONTAINING_RECORD( iface, struct connection, INetworkConnection_iface );
}

static HRESULT WINAPI connection_QueryInterface(
    INetworkConnection *iface, REFIID riid, void **obj )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %s, %p\n", connection, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_INetworkConnection ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
    }
    else if (IsEqualIID( riid, &IID_INetworkConnectionCost ))
    {
        *obj = &connection->INetworkConnectionCost_iface;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
    INetworkConnection_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI connection_AddRef(
    INetworkConnection  *iface )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p\n", connection );
    INetworkListManager_AddRef( connection->mgr );
    return InterlockedIncrement( &connection->refs );
}

static ULONG connection_release_internal( struct connection *connection )
{
    LONG refs;

    if (!(refs = InterlockedDecrement( &connection->refs )))
    {
        list_remove( &connection->entry );
        network_release_internal( impl_from_INetwork( connection->network ) );
        free( connection );
    }
    return refs;
}

static ULONG WINAPI connection_Release(
    INetworkConnection  *iface )
{
    struct connection *connection = impl_from_INetworkConnection( iface );
    INetworkListManager *mgr = connection->mgr;
    ULONG refs;

    TRACE( "%p\n", connection );
    refs = connection_release_internal( connection );
    INetworkListManager_Release( mgr );
    return refs;
}

static HRESULT WINAPI connection_GetTypeInfoCount(
    INetworkConnection *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_GetTypeInfo(
    INetworkConnection *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_GetIDsOfNames(
    INetworkConnection *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_Invoke(
    INetworkConnection *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_GetNetwork(
    INetworkConnection *iface,
    INetwork **ppNetwork )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, ppNetwork );

    *ppNetwork = connection->network;
    INetwork_AddRef( *ppNetwork );
    return S_OK;
}

static HRESULT WINAPI connection_get_IsConnectedToInternet(
    INetworkConnection *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    *pbIsConnected = connection->connected_to_internet_v4 | connection->connected_to_internet_v6;
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI connection_get_IsConnected(
    INetworkConnection *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    *pbIsConnected = connection->connected_v4 | connection->connected_v6;
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI connection_GetConnectivity(
    INetworkConnection *iface,
    NLM_CONNECTIVITY *pConnectivity )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, pConnectivity );

    *pConnectivity = get_connectivity( connection->connected_v4, connection->connected_to_internet_v4,
                                       connection->connected_v6, connection->connected_to_internet_v6 );

    TRACE( "<- %#x\n", *pConnectivity );
    return S_OK;
}

static HRESULT WINAPI connection_GetConnectionId(
    INetworkConnection *iface,
    GUID *pgdConnectionId )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, pgdConnectionId );

    *pgdConnectionId = connection->id;
    return S_OK;
}

static HRESULT WINAPI connection_GetAdapterId(
    INetworkConnection *iface,
    GUID *pgdAdapterId )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, pgdAdapterId );

    if (!pgdAdapterId) return E_POINTER;

    *pgdAdapterId = connection->adapter_id;
    return S_OK;
}

static HRESULT WINAPI connection_GetDomainType(
    INetworkConnection *iface,
    NLM_DOMAIN_TYPE *pDomainType )
{
    FIXME( "%p, %p\n", iface, pDomainType );

    *pDomainType = NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK;
    return S_OK;
}

static const struct INetworkConnectionVtbl connection_vtbl =
{
    connection_QueryInterface,
    connection_AddRef,
    connection_Release,
    connection_GetTypeInfoCount,
    connection_GetTypeInfo,
    connection_GetIDsOfNames,
    connection_Invoke,
    connection_GetNetwork,
    connection_get_IsConnectedToInternet,
    connection_get_IsConnected,
    connection_GetConnectivity,
    connection_GetConnectionId,
    connection_GetAdapterId,
    connection_GetDomainType
};

static inline struct connection *impl_from_INetworkConnectionCost(
    INetworkConnectionCost *iface )
{
    return CONTAINING_RECORD( iface, struct connection, INetworkConnectionCost_iface );
}

static HRESULT WINAPI connection_cost_QueryInterface(
    INetworkConnectionCost *iface,
    REFIID riid,
    void **obj )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );
    return INetworkConnection_QueryInterface( &conn->INetworkConnection_iface, riid, obj );
}

static ULONG WINAPI connection_cost_AddRef(
    INetworkConnectionCost *iface )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );
    return INetworkConnection_AddRef( &conn->INetworkConnection_iface );
}

static ULONG WINAPI connection_cost_Release(
    INetworkConnectionCost *iface )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );
    return INetworkConnection_Release( &conn->INetworkConnection_iface );
}

static HRESULT WINAPI connection_cost_GetCost(
    INetworkConnectionCost *iface, DWORD *pCost )
{
    FIXME( "%p, %p\n", iface, pCost );

    if (!pCost) return E_POINTER;

    *pCost = NLM_CONNECTION_COST_UNRESTRICTED;
    return S_OK;
}

static HRESULT WINAPI connection_cost_GetDataPlanStatus(
    INetworkConnectionCost *iface, NLM_DATAPLAN_STATUS *pDataPlanStatus )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );

    FIXME( "%p, %p\n", iface, pDataPlanStatus );

    if (!pDataPlanStatus) return E_POINTER;

    memcpy( &pDataPlanStatus->InterfaceGuid, &conn->id, sizeof(conn->id) );
    pDataPlanStatus->UsageData.UsageInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->UsageData.LastSyncTime, 0, sizeof(pDataPlanStatus->UsageData.LastSyncTime) );
    pDataPlanStatus->DataLimitInMegabytes       = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->InboundBandwidthInKbps     = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->OutboundBandwidthInKbps    = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->NextBillingCycle, 0, sizeof(pDataPlanStatus->NextBillingCycle) );
    pDataPlanStatus->MaxTransferSizeInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->Reserved                   = 0;

    return S_OK;
}

static const INetworkConnectionCostVtbl connection_cost_vtbl =
{
    connection_cost_QueryInterface,
    connection_cost_AddRef,
    connection_cost_Release,
    connection_cost_GetCost,
    connection_cost_GetDataPlanStatus
};

static struct connection *create_connection( struct list_manager *mgr, const GUID *id,
                                             const GUID *adapter_id )
{
    struct connection *ret;

    if (!(ret = calloc( 1, sizeof(*ret) ))) return NULL;

    ret->INetworkConnection_iface.lpVtbl     = &connection_vtbl;
    ret->INetworkConnectionCost_iface.lpVtbl = &connection_cost_vtbl;
    ret->refs = 1;
    ret->id   = *id;
    ret->adapter_id = *adapter_id;
    ret->mgr = &mgr->INetworkListManager_iface;
    list_init( &ret->entry );

    return ret;
}

static IP_ADAPTER_ADDRESSES *get_network_adapters(void)
{
    ULONG err, size = 4096, flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                    GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    IP_ADAPTER_ADDRESSES *tmp, *ret;

    if (!(ret = malloc( size ))) return NULL;
    err = GetAdaptersAddresses( AF_UNSPEC, flags, NULL, ret, &size );
    while (err == ERROR_BUFFER_OVERFLOW)
    {
        if (!(tmp = realloc( ret, size ))) break;
        ret = tmp;
        err = GetAdaptersAddresses( AF_UNSPEC, flags, NULL, ret, &size );
    }
    if (err == ERROR_SUCCESS) return ret;
    free( ret );
    return NULL;
}

static void has_ipv6_address( const IP_ADAPTER_ADDRESSES *aa, BOOL *has_local, BOOL *has_global )
{
    const IP_ADAPTER_UNICAST_ADDRESS *addr = aa->FirstUnicastAddress;
    const struct in6_addr *sa6;

    *has_local = *has_global = FALSE;
    for (addr = aa->FirstUnicastAddress; addr; addr = addr->Next)
    {
        if (addr->Address.lpSockaddr->sa_family != AF_INET6) continue;
        sa6 = &((struct sockaddr_in6 *)addr->Address.lpSockaddr)->sin6_addr;
        if (IN6_IS_ADDR_LINKLOCAL(sa6) || IN6_IS_ADDR_SITELOCAL(sa6))
            *has_local = TRUE;
        else if (!IN6_IS_ADDR_UNSPECIFIED(sa6) && !IN6_IS_ADDR_MULTICAST(sa6) && !IN6_IS_ADDR_LOOPBACK(sa6))
            *has_global = TRUE;
    }
}

static BOOL has_ipv4_address( const IP_ADAPTER_ADDRESSES *aa )
{
    const IP_ADAPTER_UNICAST_ADDRESS *addr = aa->FirstUnicastAddress;
    while (addr)
    {
        if (addr->Address.lpSockaddr->sa_family == AF_INET)
            return TRUE;
        addr = addr->Next;
    }
    return FALSE;
}

static BOOL has_ipv4_gateway_address( const IP_ADAPTER_ADDRESSES *aa )
{
    const IP_ADAPTER_GATEWAY_ADDRESS *addr = aa->FirstGatewayAddress;
    while (addr)
    {
        if (addr->Address.lpSockaddr->sa_family == AF_INET)
            return TRUE;
        addr = addr->Next;
    }
    return FALSE;
}

struct adapter_state
{
    VARIANT_BOOL connected_to_internet_v4;
    VARIANT_BOOL connected_to_internet_v6;
    VARIANT_BOOL connected_v4;
    VARIANT_BOOL connected_v6;
};

static void get_adapter_state( const IP_ADAPTER_ADDRESSES *aa, struct adapter_state *state )
{
    BOOL has_local, has_global;

    memset( state, 0, sizeof(*state) );

    has_ipv6_address( aa, &has_local, &has_global );
    if (has_local || has_global) state->connected_v6 = VARIANT_TRUE;
    if (has_global) state->connected_to_internet_v6 = VARIANT_TRUE;
    if (has_ipv4_address( aa )) state->connected_v4 = VARIANT_TRUE;
    if (has_ipv4_gateway_address( aa )) state->connected_to_internet_v4 = VARIANT_TRUE;
}

static BOOL get_adapter_id( const IP_ADAPTER_ADDRESSES *aa, GUID *id )
{
    NET_LUID luid;

    if (aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK) return FALSE;
    if (!aa->FriendlyName || !wcscmp( aa->FriendlyName, L"lo" )) return FALSE;
    if (ConvertInterfaceIndexToLuid( aa->IfIndex, &luid )) return FALSE;
    if (ConvertInterfaceLuidToGuid( &luid, id )) return FALSE;
    return TRUE;
}

static BOOL adapter_present( const IP_ADAPTER_ADDRESSES *buf, const GUID *id )
{
    const IP_ADAPTER_ADDRESSES *aa;
    GUID adapter;

    for (aa = buf; aa; aa = aa->Next)
    {
        if (get_adapter_id( aa, &adapter ) && IsEqualGUID( &adapter, id )) return TRUE;
    }
    return FALSE;
}

static struct network *find_network( struct list_manager *mgr, const GUID *id )
{
    struct network *network;

    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (IsEqualGUID( &network->id, id )) return network;
    }
    return NULL;
}

static struct connection *find_connection( struct list_manager *mgr, const GUID *id )
{
    struct connection *connection;

    LIST_FOR_EACH_ENTRY( connection, &mgr->connections, struct connection, entry )
    {
        if (IsEqualGUID( &connection->id, id )) return connection;
    }
    return NULL;
}

static IUnknown **snapshot_sinks( struct list_manager *mgr, struct connection_point *cp,
                                  ULONG *count )
{
    struct sink_entry *sink;
    IUnknown **ret;
    ULONG i = 0;

    *count = 0;
    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( sink, &cp->sinks, struct sink_entry, entry ) (*count)++;
    if (!*count || !(ret = calloc( *count, sizeof(*ret) )))
    {
        *count = 0;
        LeaveCriticalSection( &mgr->cs );
        return NULL;
    }
    LIST_FOR_EACH_ENTRY( sink, &cp->sinks, struct sink_entry, entry )
    {
        ret[i] = sink->unk;
        IUnknown_AddRef( ret[i] );
        i++;
    }
    LeaveCriticalSection( &mgr->cs );
    return ret;
}

static void release_sinks( IUnknown **sinks, ULONG count )
{
    ULONG i;

    for (i = 0; i < count; i++) IUnknown_Release( sinks[i] );
    free( sinks );
}

enum event_type
{
    EVENT_NETWORK_ADDED,
    EVENT_NETWORK_DELETED,
    EVENT_NETWORK_CONNECTIVITY,
    EVENT_CONNECTION_CONNECTIVITY,
    EVENT_CONNECTIVITY
};

struct nlm_event
{
    struct list entry;
    enum event_type type;
    GUID id;
    NLM_CONNECTIVITY connectivity;
};

static void queue_event( struct list *events, enum event_type type, const GUID *id,
                         NLM_CONNECTIVITY connectivity )
{
    struct nlm_event *event;

    if (!(event = malloc( sizeof(*event) ))) return;
    event->type = type;
    if (id) event->id = *id;
    event->connectivity = connectivity;
    list_add_tail( events, &event->entry );
}

static void dispatch_event( struct list_manager *mgr, const struct nlm_event *event )
{
    struct connection_point *cp;
    IUnknown **sinks;
    ULONG count, i;

    switch (event->type)
    {
    case EVENT_NETWORK_ADDED:
    case EVENT_NETWORK_DELETED:
    case EVENT_NETWORK_CONNECTIVITY:
        cp = &mgr->events_cp;
        break;
    case EVENT_CONNECTION_CONNECTIVITY:
        cp = &mgr->conn_mgr_cp;
        break;
    case EVENT_CONNECTIVITY:
        cp = &mgr->list_mgr_cp;
        break;
    default:
        return;
    }

    if (!(sinks = snapshot_sinks( mgr, cp, &count ))) return;
    for (i = 0; i < count; i++)
    {
        switch (event->type)
        {
        case EVENT_NETWORK_ADDED:
            INetworkEvents_NetworkAdded( (INetworkEvents *)sinks[i], event->id );
            break;
        case EVENT_NETWORK_DELETED:
            INetworkEvents_NetworkDeleted( (INetworkEvents *)sinks[i], event->id );
            break;
        case EVENT_NETWORK_CONNECTIVITY:
            INetworkEvents_NetworkConnectivityChanged( (INetworkEvents *)sinks[i], event->id,
                                                        event->connectivity );
            break;
        case EVENT_CONNECTION_CONNECTIVITY:
            INetworkConnectionEvents_NetworkConnectionConnectivityChanged(
                (INetworkConnectionEvents *)sinks[i], event->id, event->connectivity );
            break;
        case EVENT_CONNECTIVITY:
            INetworkListManagerEvents_ConnectivityChanged(
                (INetworkListManagerEvents *)sinks[i], event->connectivity );
            break;
        }
    }
    release_sinks( sinks, count );
}

static void dispatch_events( struct list_manager *mgr, struct list *events )
{
    struct nlm_event *event;

    while (!list_empty( events ))
    {
        event = LIST_ENTRY( list_head( events ), struct nlm_event, entry );
        dispatch_event( mgr, event );
        list_remove( &event->entry );
        free( event );
    }
}

static void update_networks( struct list_manager *mgr, BOOL notify )
{
    NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED, value;
    struct network *network, *next_network;
    struct connection *connection;
    IP_ADAPTER_ADDRESSES *buf, *aa;
    struct adapter_state state;
    struct list events;
    GUID id;

    if (!(buf = get_network_adapters())) return;
    list_init( &events );

    EnterCriticalSection( &mgr->cs );

    for (aa = buf; aa; aa = aa->Next)
    {
        if (!get_adapter_id( aa, &id )) continue;
        get_adapter_state( aa, &state );

        if ((network = find_network( mgr, &id )))
        {
            connection = find_connection( mgr, &id );
            if (network->connected_v4 == state.connected_v4 &&
                network->connected_v6 == state.connected_v6 &&
                network->connected_to_internet_v4 == state.connected_to_internet_v4 &&
                network->connected_to_internet_v6 == state.connected_to_internet_v6)
                continue;
        }
        else
        {
            /* assume a one-to-one mapping between networks and connections */
            if (!(network = create_network( mgr, &id ))) continue;
            if (!(connection = create_connection( mgr, &id, &id )))
            {
                network_release_internal( network );
                continue;
            }

            connection->network = &network->INetwork_iface;
            InterlockedIncrement( &network->refs );

            list_add_tail( &mgr->networks, &network->entry );
            list_add_tail( &mgr->connections, &connection->entry );

            if (notify) queue_event( &events, EVENT_NETWORK_ADDED, &id, 0 );
        }

        network->connected_v4 = state.connected_v4;
        network->connected_v6 = state.connected_v6;
        network->connected_to_internet_v4 = state.connected_to_internet_v4;
        network->connected_to_internet_v6 = state.connected_to_internet_v6;

        if (connection)
        {
            connection->connected_v4 = state.connected_v4;
            connection->connected_v6 = state.connected_v6;
            connection->connected_to_internet_v4 = state.connected_to_internet_v4;
            connection->connected_to_internet_v6 = state.connected_to_internet_v6;
        }

        if (notify)
        {
            value = network_connectivity( network );
            queue_event( &events, EVENT_NETWORK_CONNECTIVITY, &id, value );
            if (connection)
                queue_event( &events, EVENT_CONNECTION_CONNECTIVITY, &connection->id, value );
        }
    }

    LIST_FOR_EACH_ENTRY_SAFE( network, next_network, &mgr->networks, struct network, entry )
    {
        if (adapter_present( buf, &network->id )) continue;

        if (notify) queue_event( &events, EVENT_NETWORK_DELETED, &network->id, 0 );

        if ((connection = find_connection( mgr, &network->id )))
        {
            list_remove( &connection->entry );
            list_init( &connection->entry );
            connection_release_internal( connection );
        }
        list_remove( &network->entry );
        list_init( &network->entry );
        network_release_internal( network );
    }

    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
        connectivity |= network_connectivity( network );

    if (connectivity != mgr->connectivity)
    {
        mgr->connectivity = connectivity;
        if (notify) queue_event( &events, EVENT_CONNECTIVITY, NULL, connectivity );
    }

    LeaveCriticalSection( &mgr->cs );
    free( buf );
    dispatch_events( mgr, &events );
}

static void WINAPI interface_change_callback( PVOID context, PMIB_IPINTERFACE_ROW row,
                                              MIB_NOTIFICATION_TYPE type )
{
    struct list_manager *mgr = context;

    TRACE( "%p, %p, %u\n", context, row, type );
    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    update_networks( mgr, TRUE );
    INetworkListManager_Release( &mgr->INetworkListManager_iface );
}

static void WINAPI address_change_callback( PVOID context, PMIB_UNICASTIPADDRESS_ROW row,
                                            MIB_NOTIFICATION_TYPE type )
{
    struct list_manager *mgr = context;

    TRACE( "%p, %p, %u\n", context, row, type );
    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    update_networks( mgr, TRUE );
    INetworkListManager_Release( &mgr->INetworkListManager_iface );
}

static void WINAPI route_change_callback( PVOID context, PMIB_IPFORWARD_ROW2 row,
                                          MIB_NOTIFICATION_TYPE type )
{
    struct list_manager *mgr = context;

    TRACE( "%p, %p, %u\n", context, row, type );
    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    update_networks( mgr, TRUE );
    INetworkListManager_Release( &mgr->INetworkListManager_iface );
}

HRESULT list_manager_create( void **obj )
{
    struct list_manager *mgr;
    DWORD error;

    TRACE( "%p\n", obj );

    if (!(mgr = calloc( 1, sizeof(*mgr) ))) return E_OUTOFMEMORY;
    mgr->INetworkListManager_iface.lpVtbl = &list_manager_vtbl;
    mgr->INetworkCostManager_iface.lpVtbl = &cost_manager_vtbl;
    mgr->IConnectionPointContainer_iface.lpVtbl = &cpc_vtbl;
    mgr->refs = 1;
    InitializeCriticalSection( &mgr->cs );
    list_init( &mgr->networks );
    list_init( &mgr->connections );

    connection_point_init( &mgr->list_mgr_cp, &IID_INetworkListManagerEvents,
                           &mgr->IConnectionPointContainer_iface );
    connection_point_init( &mgr->cost_mgr_cp, &IID_INetworkCostManagerEvents,
                           &mgr->IConnectionPointContainer_iface);
    connection_point_init( &mgr->conn_mgr_cp, &IID_INetworkConnectionEvents,
                           &mgr->IConnectionPointContainer_iface );
    connection_point_init( &mgr->events_cp, &IID_INetworkEvents,
                           &mgr->IConnectionPointContainer_iface );

    update_networks( mgr, FALSE );

    error = NotifyIpInterfaceChange( AF_UNSPEC, interface_change_callback, mgr, FALSE,
                                     &mgr->iface_change );
    if (error) WARN( "failed to register interface notifications, error %lu\n", error );
    error = NotifyUnicastIpAddressChange( AF_UNSPEC, address_change_callback, mgr, FALSE,
                                          &mgr->addr_change );
    if (error) WARN( "failed to register address notifications, error %lu\n", error );
    error = NotifyRouteChange2( AF_UNSPEC, route_change_callback, mgr, FALSE,
                                &mgr->route_change );
    if (error) WARN( "failed to register route notifications, error %lu\n", error );

    *obj = &mgr->INetworkListManager_iface;
    TRACE( "returning iface %p\n", *obj );
    return S_OK;
}
