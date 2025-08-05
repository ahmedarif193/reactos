/*
 * Copyright 2007 Jacek Caban for CodeWeavers
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
#include "config.h"
#include "ws2tcpip.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "rpcproxy.h"
#include "httprequest.h"
#include "winhttp.h"

#include "wine/debug.h"
#include "winhttp_private.h"

HINSTANCE winhttp_instance;

WINE_DEFAULT_DEBUG_CHANNEL(winhttp);

/******************************************************************
 *              DllMain (winhttp.@)
 */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpv)
{
    switch(fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        winhttp_instance = hInstDLL;
        DisableThreadLibraryCalls(hInstDLL);
        break;
    case DLL_PROCESS_DETACH:
        if (lpv) break;
        netconn_unload();
        release_typelib();
        break;
    }
    return TRUE;
}

typedef HRESULT (*fnCreateInstance)( void **obj );

struct winhttp_cf
{
    IClassFactory IClassFactory_iface;
    fnCreateInstance pfnCreateInstance;
};

static inline struct winhttp_cf *impl_from_IClassFactory( IClassFactory *iface )
{
    return CONTAINING_RECORD( iface, struct winhttp_cf, IClassFactory_iface );
}

static HRESULT WINAPI requestcf_QueryInterface(
    IClassFactory *iface,
    REFIID riid,
    void **obj )
{
    if (IsEqualGUID( riid, &IID_IUnknown ) ||
        IsEqualGUID( riid, &IID_IClassFactory ))
    {
        IClassFactory_AddRef( iface );
        *obj = iface;
        return S_OK;
    }
    FIXME("interface %s not implemented\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI requestcf_AddRef(
    IClassFactory *iface )
{
    return 2;
}

static ULONG WINAPI requestcf_Release(
    IClassFactory *iface )
{
    return 1;
}

static HRESULT WINAPI requestcf_CreateInstance(
    IClassFactory *iface,
    LPUNKNOWN outer,
    REFIID riid,
    void **obj )
{
    struct winhttp_cf *cf = impl_from_IClassFactory( iface );
    IUnknown *unknown;
    HRESULT hr;

    TRACE("%p, %s, %p\n", outer, debugstr_guid(riid), obj);

    *obj = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    hr = cf->pfnCreateInstance( (void **)&unknown );
    if (FAILED(hr))
        return hr;

    hr = IUnknown_QueryInterface( unknown, riid, obj );
    IUnknown_Release( unknown );
    return hr;
}

static HRESULT WINAPI requestcf_LockServer(
    IClassFactory *iface,
    BOOL dolock )
{
    FIXME("%p, %d\n", iface, dolock);
    return S_OK;
}

static const struct IClassFactoryVtbl winhttp_cf_vtbl =
{
    requestcf_QueryInterface,
    requestcf_AddRef,
    requestcf_Release,
    requestcf_CreateInstance,
    requestcf_LockServer
};

static struct winhttp_cf request_cf = { { &winhttp_cf_vtbl }, WinHttpRequest_create };

/******************************************************************
 *		DllGetClassObject (winhttp.@)
 */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    IClassFactory *cf = NULL;

    TRACE("%s, %s, %p\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv);

    if (IsEqualGUID( rclsid, &CLSID_WinHttpRequest ))
    {
       cf = &request_cf.IClassFactory_iface;
    }
    if (!cf) return CLASS_E_CLASSNOTAVAILABLE;
    return IClassFactory_QueryInterface( cf, riid, ppv );
}

/******************************************************************
 *              DllCanUnloadNow (winhttp.@)
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

/***********************************************************************
 *          DllRegisterServer (winhttp.@)
 */
HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources( winhttp_instance );
}

/***********************************************************************
 *          DllUnregisterServer (winhttp.@)
 */
HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources( winhttp_instance );
}

/* Windows 10 WinHTTP API Stubs */

BOOL WINAPI WinHttpAddCookies(HINTERNET hRequest, LPCWSTR lpszCookies, DWORD dwFlags)
{
    FIXME("(%p, %s, %lu): stub\n", hRequest, debugstr_w(lpszCookies), dwFlags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpAddRequestHeadersEx(HINTERNET hRequest, DWORD dwModifiers, ULONGLONG ullTotal, ULONGLONG ullLength, LPCWSTR lpvOptional)
{
    FIXME("(%p, %lu, %llu, %llu, %s): stub\n", hRequest, dwModifiers, ullTotal, ullLength, debugstr_w(lpvOptional));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD WINAPI WinHttpCreateProxyResolver(HINTERNET hSession, HINTERNET* phResolver)
{
    FIXME("(%p, %p): stub\n", hSession, phResolver);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

BOOL WINAPI WinHttpGetCookieJar(HINTERNET hSession, PVOID* ppvCookieJar, PDWORD pdwSize)
{
    FIXME("(%p, %p, %p): stub\n", hSession, ppvCookieJar, pdwSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD WINAPI WinHttpGetProxyForUrlEx(HINTERNET hResolver, LPCWSTR pwszUrl, WINHTTP_AUTOPROXY_OPTIONS* pAutoProxyOptions, DWORD_PTR pContext)
{
    FIXME("(%p, %s, %p, %p): stub\n", hResolver, debugstr_w(pwszUrl), pAutoProxyOptions, (void*)pContext);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD WINAPI WinHttpGetProxyForUrlEx2(HINTERNET hResolver, LPCWSTR pwszUrl, WINHTTP_AUTOPROXY_OPTIONS* pAutoProxyOptions, DWORD cbInterfaceSelectionContext, PVOID pInterfaceSelectionContext, DWORD_PTR pContext)
{
    FIXME("(%p, %s, %p, %lu, %p, %p): stub\n", hResolver, debugstr_w(pwszUrl), pAutoProxyOptions, cbInterfaceSelectionContext, pInterfaceSelectionContext, (void*)pContext);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD WINAPI WinHttpGetProxyResult(HINTERNET hResolver, WINHTTP_PROXY_RESULT* pProxyResult)
{
    FIXME("(%p, %p): stub\n", hResolver, pProxyResult);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD WINAPI WinHttpGetProxyResultEx(HINTERNET hResolver, WINHTTP_PROXY_RESULT_EX* pProxyResultEx)
{
    FIXME("(%p, %p): stub\n", hResolver, pProxyResultEx);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

BOOL WINAPI WinHttpGetTracing(HINTERNET hInternet, PDWORD pdwFlags, PVOID* ppvData, PDWORD pdwDataSize)
{
    FIXME("(%p, %p, %p, %p): stub\n", hInternet, pdwFlags, ppvData, pdwDataSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HANDLE WINAPI WinHttpGetTunnelSocket(HINTERNET hRequest)
{
    FIXME("(%p): stub\n", hRequest);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return INVALID_HANDLE_VALUE;
}

BOOL WINAPI WinHttpQueryAddressFamily(HINTERNET hInternet, PDWORD pdwAddressFamily)
{
    FIXME("(%p, %p): stub\n", hInternet, pdwAddressFamily);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryClientCertificate(HINTERNET hRequest, PVOID* ppvCertificate, PDWORD pdwCertificateSize)
{
    FIXME("(%p, %p, %p): stub\n", hRequest, ppvCertificate, pdwCertificateSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryCompressionLevel(HINTERNET hInternet, PDWORD pdwLevel)
{
    FIXME("(%p, %p): stub\n", hInternet, pdwLevel);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryConnectionGroup(HINTERNET hInternet, PVOID* ppvGroup, PDWORD pdwGroupSize)
{
    FIXME("(%p, %p, %p): stub\n", hInternet, ppvGroup, pdwGroupSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryConnectionTiming(HINTERNET hInternet, PVOID pTiming, PDWORD pdwTimingSize)
{
    FIXME("(%p, %p, %p): stub\n", hInternet, pTiming, pdwTimingSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryContentEncoding(HINTERNET hInternet, PVOID pEncoding, PDWORD pdwEncodingSize)
{
    FIXME("(%p, %p, %p): stub\n", hInternet, pEncoding, pdwEncodingSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryCookies(HINTERNET hInternet, LPCWSTR lpszUrl, PVOID pCookies, PDWORD pdwCookiesSize, PDWORD pdwFlags)
{
    FIXME("(%p, %s, %p, %p, %p): stub\n", hInternet, debugstr_w(lpszUrl), pCookies, pdwCookiesSize, pdwFlags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryExtendedError(HINTERNET hInternet, PDWORD pdwError, PVOID pErrorInfo, PDWORD pdwErrorInfoSize)
{
    FIXME("(%p, %p, %p, %p): stub\n", hInternet, pdwError, pErrorInfo, pdwErrorInfoSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryHeadersEx(HINTERNET hRequest, DWORD dwInfoLevel, LPCWSTR pwszName, PVOID pBuffer, PDWORD pdwBufferLength, PDWORD pdwIndex, ULONGLONG* pullFlags)
{
    FIXME("(%p, %lu, %s, %p, %p, %p, %p): stub\n", hRequest, dwInfoLevel, debugstr_w(pwszName), pBuffer, pdwBufferLength, pdwIndex, pullFlags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryNetworkInterface(HINTERNET hInternet, PVOID pInterface, PDWORD pdwInterfaceSize)
{
    FIXME("(%p, %p, %p): stub\n", hInternet, pInterface, pdwInterfaceSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryPerformanceCounter(HINTERNET hInternet, PULONGLONG pCounter)
{
    FIXME("(%p, %p): stub\n", hInternet, pCounter);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryRequestProperty(HINTERNET hRequest, DWORD dwProperty, PVOID pBuffer, PDWORD pdwBufferLength)
{
    FIXME("(%p, %lu, %p, %p): stub\n", hRequest, dwProperty, pBuffer, pdwBufferLength);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryRequestTiming(HINTERNET hRequest, PVOID pTiming, PDWORD pdwTimingSize)
{
    FIXME("(%p, %p, %p): stub\n", hRequest, pTiming, pdwTimingSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQuerySecurityInfo(HINTERNET hInternet, DWORD dwInfoLevel, PVOID pBuffer, PDWORD pdwBufferLength)
{
    FIXME("(%p, %lu, %p, %p): stub\n", hInternet, dwInfoLevel, pBuffer, pdwBufferLength);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQuerySessionProperty(HINTERNET hSession, DWORD dwProperty, PVOID pBuffer, PDWORD pdwBufferLength)
{
    FIXME("(%p, %lu, %p, %p): stub\n", hSession, dwProperty, pBuffer, pdwBufferLength);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpQueryStatistics(HINTERNET hInternet, DWORD dwFlags, PVOID pStatistics, PDWORD pdwStatisticsSize)
{
    FIXME("(%p, %lu, %p, %p): stub\n", hInternet, dwFlags, pStatistics, pdwStatisticsSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpReadDataEx(HINTERNET hRequest, PVOID pBuffer, DWORD dwNumberOfBytesToRead, PDWORD pdwNumberOfBytesRead, ULONGLONG ullFlags, PVOID pReserved1, PVOID pReserved2)
{
    FIXME("(%p, %p, %lu, %p, %llu, %p, %p): stub\n", hRequest, pBuffer, dwNumberOfBytesToRead, pdwNumberOfBytesRead, ullFlags, pReserved1, pReserved2);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD WINAPI WinHttpResetAutoProxy(HINTERNET hSession, DWORD dwFlags)
{
    FIXME("(%p, %lu): stub\n", hSession, dwFlags);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

BOOL WINAPI WinHttpSetAddressFamily(HINTERNET hInternet, DWORD dwAddressFamily)
{
    FIXME("(%p, %lu): stub\n", hInternet, dwAddressFamily);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetClientCertificate(HINTERNET hRequest, PVOID pCertificate, DWORD dwCertificateSize)
{
    FIXME("(%p, %p, %lu): stub\n", hRequest, pCertificate, dwCertificateSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetConnectionGroup(HINTERNET hInternet, PVOID pGroup)
{
    FIXME("(%p, %p): stub\n", hInternet, pGroup);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetContentEncoding(HINTERNET hInternet, LPCWSTR pwszEncoding)
{
    FIXME("(%p, %s): stub\n", hInternet, debugstr_w(pwszEncoding));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetCookieJar(HINTERNET hSession, PVOID pCookieJar)
{
    FIXME("(%p, %p): stub\n", hSession, pCookieJar);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetDecompressionLevel(HINTERNET hInternet, DWORD dwLevel)
{
    FIXME("(%p, %lu): stub\n", hInternet, dwLevel);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetExtendedErrorCallback(HINTERNET hInternet, PVOID pCallback, DWORD dwFlags)
{
    FIXME("(%p, %p, %lu): stub\n", hInternet, pCallback, dwFlags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetNetworkInterface(HINTERNET hInternet, PVOID pInterface)
{
    FIXME("(%p, %p): stub\n", hInternet, pInterface);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetRequestProperty(HINTERNET hRequest, DWORD dwProperty, PVOID pBuffer, DWORD dwBufferLength)
{
    FIXME("(%p, %lu, %p, %lu): stub\n", hRequest, dwProperty, pBuffer, dwBufferLength);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetSecurityInfo(HINTERNET hInternet, DWORD dwInfoLevel, PVOID pBuffer, DWORD dwBufferLength)
{
    FIXME("(%p, %lu, %p, %lu): stub\n", hInternet, dwInfoLevel, pBuffer, dwBufferLength);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetSessionProperty(HINTERNET hSession, DWORD dwProperty, PVOID pBuffer, DWORD dwBufferLength)
{
    FIXME("(%p, %lu, %p, %lu): stub\n", hSession, dwProperty, pBuffer, dwBufferLength);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpSetTracing(HINTERNET hInternet, DWORD dwFlags, PVOID pData, DWORD dwDataSize)
{
    FIXME("(%p, %lu, %p, %lu): stub\n", hInternet, dwFlags, pData, dwDataSize);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpStreamedDownload(HINTERNET hInternet, PVOID pCallback, PVOID pContext)
{
    FIXME("(%p, %p, %p): stub\n", hInternet, pCallback, pContext);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI WinHttpStreamedUpload(HINTERNET hInternet, PVOID pCallback, PVOID pContext)
{
    FIXME("(%p, %p, %p): stub\n", hInternet, pCallback, pContext);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

DWORD WINAPI WinHttpWebSocketClose(HINTERNET hWebSocket, USHORT usStatus, PVOID pvReason, DWORD dwReasonLength)
{
    FIXME("(%p, %u, %p, %lu): stub\n", hWebSocket, usStatus, pvReason, dwReasonLength);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

HINTERNET WINAPI WinHttpWebSocketCompleteUpgrade(HINTERNET hRequest, DWORD_PTR pContext)
{
    FIXME("(%p, %p): stub\n", hRequest, (void*)pContext);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

DWORD WINAPI WinHttpWebSocketQueryCloseStatus(HINTERNET hWebSocket, PUSHORT pusStatus, PVOID pvReason, DWORD dwReasonLength, PDWORD pdwReasonLengthConsumed)
{
    FIXME("(%p, %p, %p, %lu, %p): stub\n", hWebSocket, pusStatus, pvReason, dwReasonLength, pdwReasonLengthConsumed);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD WINAPI WinHttpWebSocketReceive(HINTERNET hWebSocket, PVOID pvBuffer, DWORD dwBufferLength, PDWORD pdwBytesRead, WINHTTP_WEB_SOCKET_BUFFER_TYPE *peBufferType)
{
    FIXME("(%p, %p, %lu, %p, %p): stub\n", hWebSocket, pvBuffer, dwBufferLength, pdwBytesRead, peBufferType);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD WINAPI WinHttpWebSocketSend(HINTERNET hWebSocket, WINHTTP_WEB_SOCKET_BUFFER_TYPE eBufferType, PVOID pvBuffer, DWORD dwBufferLength)
{
    FIXME("(%p, %d, %p, %lu): stub\n", hWebSocket, eBufferType, pvBuffer, dwBufferLength);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD WINAPI WinHttpWebSocketShutdown(HINTERNET hWebSocket, USHORT usStatus, PVOID pvReason, DWORD dwReasonLength)
{
    FIXME("(%p, %u, %p, %lu): stub\n", hWebSocket, usStatus, pvReason, dwReasonLength);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

BOOL WINAPI WinHttpWriteDataEx(HINTERNET hRequest, PVOID pBuffer, DWORD dwNumberOfBytesToWrite, PDWORD pdwNumberOfBytesWritten, ULONGLONG ullFlags, PVOID pReserved1, PVOID pReserved2)
{
    FIXME("(%p, %p, %lu, %p, %llu, %p, %p): stub\n", hRequest, pBuffer, dwNumberOfBytesToWrite, pdwNumberOfBytesWritten, ullFlags, pReserved1, pReserved2);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
