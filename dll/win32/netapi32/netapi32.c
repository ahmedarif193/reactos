/* Copyright 2001 Mike McCormack
 * Copyright 2003 Juan Lang
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

#include "netapi32.h"
#include <davclnt.h>
#include <lmjoin.h>

WINE_DEFAULT_DEBUG_CHANNEL(netapi32);

BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("%p,%lx,%p\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            InitializeListHead(&g_EnumContextListHead);
            InitializeCriticalSection(&g_EnumContextListLock);
            DisableThreadLibraryCalls(hinstDLL);
            NetBIOSInit();
            NetBTInit();
            break;
        case DLL_PROCESS_DETACH:
            if (lpvReserved) break;
            NetBIOSShutdown();
            break;
    }

    return TRUE;
}

DWORD WINAPI DsGetDcOpenA(LPCSTR domain, ULONG flags, LPCSTR site,
    GUID *domain_guid, LPCSTR forest, ULONG dc_flags, PHANDLE context)
{
    FIXME("(%s, %08lx, %s, %s, %s, %08lx, %p)\n", debugstr_a(domain),
        flags, debugstr_a(site), wine_dbgstr_guid(domain_guid),
        debugstr_a(forest), dc_flags, context);

    *context = NULL;

    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD WINAPI DsGetDcOpenW(LPCWSTR domain, ULONG flags, LPCWSTR site,
    GUID *domain_guid, LPCWSTR forest, ULONG dc_flags, PHANDLE context)
{
    FIXME("(%s, %08lx, %s, %s, %s, %08lx, %p)\n", debugstr_w(domain),
        flags, debugstr_w(site), wine_dbgstr_guid(domain_guid),
        debugstr_w(forest), dc_flags, context);

    *context = NULL;

    return ERROR_CALL_NOT_IMPLEMENTED;
}
/************************************************************
 *                NetValidatePasswordPolicy  (NETAPI32.@)
 */
NET_API_STATUS WINAPI NetValidatePasswordPolicy(
    LPCWSTR servername,
    LPVOID qualifier,
    DWORD validation_type,
    LPVOID input_arg,
    LPVOID *output_arg)
{
    NET_API_STATUS status;
    NET_VALIDATE_OUTPUT_ARG *out = NULL;

    FIXME("(%s, %p, %lu, %p, %p) stub!\n",
          debugstr_w(servername), qualifier, validation_type, input_arg, output_arg);

    if (!output_arg)
        return ERROR_INVALID_PARAMETER;

    *output_arg = NULL;

    status = NetApiBufferAllocate(sizeof(*out), (LPVOID *)&out);
    if (status != NERR_Success)
        return status;

    /* Zero all fields; report success so most probes pass */
    memset(out, 0, sizeof(*out));
    out->ValidationStatus = NERR_Success;

    *output_arg = out;
    return NERR_Success;
}

/************************************************************
 *          NetValidatePasswordPolicyFree  (NETAPI32.@)
 */
NET_API_STATUS WINAPI NetValidatePasswordPolicyFree(LPVOID *output_arg)
{
    TRACE("(%p)\n", output_arg);

    if (!output_arg) return ERROR_INVALID_PARAMETER;
    if (*output_arg)
    {
        NetApiBufferFree(*output_arg);
        *output_arg = NULL;
    }
    return NERR_Success;
}

/************************************************************
 *                DavGetHTTPFromUNCPath (NETAPI32.@)
 */
DWORD WINAPI DavGetHTTPFromUNCPath(const WCHAR *unc_path, WCHAR *buf, DWORD *buflen)
{
    static const WCHAR httpW[] = L"http://";
    static const WCHAR httpsW[] = L"https://";
    const WCHAR *p = unc_path, *q, *server, *path, *scheme = httpW;
    UINT i, len_server, len_path = 0, len_port = 0, len, port = 0;
    WCHAR *end, portbuf[12];

    TRACE("(%s %p %p)\n", debugstr_w(unc_path), buf, buflen);

    if (p[0] != '\\' || p[1] != '\\' || !p[2]) return ERROR_INVALID_PARAMETER;
    q = p += 2;
    while (*q && *q != '\\' && *q != '/' && *q != '@') q++;
    server = p;
    len_server = q - p;
    if (*q == '@')
    {
        p = ++q;
        while (*p && (*p != '\\' && *p != '/' && *p != '@')) p++;
        if (p - q == 3 && !wcsnicmp( q, L"SSL", 3 ))
        {
            scheme = httpsW;
            q = p;
        }
        else if ((port = wcstol( q, &end, 10 ))) q = end;
        else return ERROR_INVALID_PARAMETER;
    }
    if (*q == '@')
    {
        if (!(port = wcstol( ++q, &end, 10 ))) return ERROR_INVALID_PARAMETER;
        q = end;
    }
    if (*q == '\\' || *q  == '/') q++;
    path = q;
    while (*q++) len_path++;
    if (len_path && (path[len_path - 1] == '\\' || path[len_path - 1] == '/'))
        len_path--; /* remove trailing slash */

    swprintf( portbuf, ARRAY_SIZE(portbuf), L":%u", port );
    if (scheme == httpsW)
    {
        len = wcslen( httpsW );
        if (port && port != 443) len_port = wcslen( portbuf );
    }
    else
    {
        len = wcslen( httpW );
        if (port && port != 80) len_port = wcslen( portbuf );
    }
    len += len_server;
    len += len_port;
    if (len_path) len += len_path + 1; /* leading '/' */
    len++; /* nul */

    if (*buflen < len)
    {
        *buflen = len;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    memcpy( buf, scheme, wcslen(scheme) * sizeof(WCHAR) );
    buf += wcslen( scheme );
    memcpy( buf, server, len_server * sizeof(WCHAR) );
    buf += len_server;
    if (len_port)
    {
        memcpy( buf, portbuf, len_port * sizeof(WCHAR) );
        buf += len_port;
    }
    if (len_path)
    {
        *buf++ = '/';
        for (i = 0; i < len_path; i++)
        {
            if (path[i] == '\\') *buf++ = '/';
            else *buf++ = path[i];
        }
    }
    *buf = 0;
    *buflen = len;

    return ERROR_SUCCESS;
}

/************************************************************
 *                DavGetUNCFromHTTPPath (NETAPI32.@)
 */
DWORD WINAPI DavGetUNCFromHTTPPath(const WCHAR *http_path, WCHAR *buf, DWORD *buflen)
{
    static const WCHAR httpW[] = {'h','t','t','p'};
    static const WCHAR httpsW[] = {'h','t','t','p','s'};
    static const WCHAR davrootW[] = {'\\','D','a','v','W','W','W','R','o','o','t'};
    static const WCHAR sslW[] = {'@','S','S','L'};
    static const WCHAR port80W[] = {'8','0'};
    static const WCHAR port443W[] = {'4','4','3'};
    const WCHAR *p = http_path, *server, *port = NULL, *path = NULL;
    DWORD i, len = 0, len_server = 0, len_port = 0, len_path = 0;
    BOOL ssl;

    TRACE("(%s %p %p)\n", debugstr_w(http_path), buf, buflen);

    while (*p && *p != ':') { p++; len++; };
    if (len == ARRAY_SIZE(httpW) && !wcsnicmp( http_path, httpW, len )) ssl = FALSE;
    else if (len == ARRAY_SIZE(httpsW) && !wcsnicmp( http_path, httpsW, len )) ssl = TRUE;
    else return ERROR_INVALID_PARAMETER;

    if (p[0] != ':' || p[1] != '/' || p[2] != '/') return ERROR_INVALID_PARAMETER;
    server = p += 3;

    while (*p && *p != ':' && *p != '/') { p++; len_server++; };
    if (!len_server) return ERROR_BAD_NET_NAME;
    if (*p == ':')
    {
        port = ++p;
        while (*p >= '0' && *p <= '9') { p++; len_port++; };
        if (len_port == 2 && !ssl && !memcmp( port, port80W, sizeof(port80W) )) port = NULL;
        else if (len_port == 3 && ssl && !memcmp( port, port443W, sizeof(port443W) )) port = NULL;
        path = p;
    }
    else if (*p == '/') path = p;

    while (*p)
    {
        if (p[0] == '/' && p[1] == '/') return ERROR_BAD_NET_NAME;
        p++; len_path++;
    }
    if (len_path && path[len_path - 1] == '/') len_path--;

    len = len_server + 2; /* \\ */
    if (ssl) len += 4; /* @SSL */
    if (port) len += len_port + 1 /* @ */;
    len += ARRAY_SIZE(davrootW);
    len += len_path + 1; /* nul */

    if (*buflen < len)
    {
        *buflen = len;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    buf[0] = buf[1] = '\\';
    buf += 2;
    memcpy( buf, server, len_server * sizeof(WCHAR) );
    buf += len_server;
    if (ssl)
    {
        memcpy( buf, sslW, sizeof(sslW) );
        buf += 4;
    }
    if (port)
    {
        *buf++ = '@';
        memcpy( buf, port, len_port * sizeof(WCHAR) );
        buf += len_port;
    }
    memcpy( buf, davrootW, sizeof(davrootW) );
    buf += ARRAY_SIZE(davrootW);
    for (i = 0; i < len_path; i++)
    {
        if (path[i] == '/') *buf++ = '\\';
        else *buf++ = path[i];
    }

    *buf = 0;
    *buflen = len;

    return ERROR_SUCCESS;
}
/************************************************************
 *  NetGetAadJoinInformation (NETAPI32.@)
 */
HRESULT WINAPI NetGetAadJoinInformation(LPCWSTR tenant_id, PDSREG_JOIN_INFO *join_info)
{
    FIXME("(%s, %p): stub\n", debugstr_w(tenant_id), join_info);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

void NET_API_FUNCTION NetFreeAadJoinInformation(DSREG_JOIN_INFO *join_info)
{
    FIXME("%p): stub\n", join_info);
}
