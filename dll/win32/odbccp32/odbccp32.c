/*
 * Implementation of the ODBC driver installer
 *
 * Copyright 2005 Mike McCormack for CodeWeavers
 * Copyright 2005 Hans Leidekker
 * Copyright 2007 Bill Medland
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

#include <assert.h>
#include <stdarg.h>
#ifdef __REACTOS__
#include <stdlib.h>
#endif

#define COBJMACROS

#ifdef __REACTOS__
#include <limits.h>
#endif
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winnls.h"
#include "sqlext.h"
#ifdef __REACTOS__
#undef TRACE_ON
#endif
#include "wine/debug.h"

#include "odbcinst.h"

WINE_DEFAULT_DEBUG_CHANNEL(odbc);

/* This config mode is known to be process-wide.
 * MSDN documentation suggests that the value is hidden somewhere in the registry but I haven't found it yet.
 * Although both the registry and the ODBC.ini files appear to be maintained together they are not maintained automatically through the registry's IniFileMapping.
 */
static UWORD config_mode = ODBC_BOTH_DSN;

/* MSDN documentation suggests that the error subsystem handles errors 1 to 8
 * only and experimentation (Windows 2000) shows that the errors are process-
 * wide so go for the simple solution; static arrays.
 */
static int num_errors;
static int error_code[8];
static const WCHAR *error_msg[8];

static BOOL (WINAPI *pConfigDSN)(HWND hwnd, WORD request, const char *driver, const char *attr);
static BOOL (WINAPI *pConfigDSNW)(HWND hwnd, WORD request, const WCHAR *driver, const WCHAR *attr);

/* Push an error onto the error stack, taking care of ranges etc. */
static void push_error(int code, LPCWSTR msg)
{
    if (num_errors < ARRAY_SIZE(error_code))
    {
        error_code[num_errors] = code;
        error_msg[num_errors] = msg;
        num_errors++;
    }
}

/* Clear the error stack */
static void clear_errors(void)
{
    num_errors = 0;
}

static inline WCHAR *strdupAtoW(const char *str)
{
    LPWSTR ret = NULL;

    if(str) {
#ifdef __REACTOS__
        int len;
#else
        DWORD len;
#endif

        len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
#ifdef __REACTOS__
        if (!len || (size_t)len > (size_t)-1 / sizeof(WCHAR)) return NULL;
        ret = malloc((size_t)len * sizeof(WCHAR));
        if (ret && !MultiByteToWideChar(CP_ACP, 0, str, -1, ret, len))
        {
            free(ret);
            ret = NULL;
        }
#else
        ret = malloc(len * sizeof(WCHAR));
        if(ret)
            MultiByteToWideChar(CP_ACP, 0, str, -1, ret, len);
#endif
    }

    return ret;
}


BOOL WINAPI ODBCCPlApplet( LONG i, LONG j, LONG * p1, LONG * p2)
{
    clear_errors();
    FIXME( "( %ld %ld %p %p) : stub!\n", i, j, p1, p2);
    return FALSE;
}

static LPWSTR SQLInstall_strdup_multi(LPCSTR str)
{
    LPCSTR p;
    LPWSTR ret = NULL;
#ifdef __REACTOS__
    size_t count = 0, size;
    int len;
#else
    DWORD len;
#endif

    if (!str)
        return ret;

#ifdef __REACTOS__
    for (p = str; *p; p += size)
    {
        size = strlen(p) + 1;
        if (size > INT_MAX - count) return NULL;
        count += size;
    }
#else
    for (p = str; *p; p += lstrlenA(p) + 1)
        ;
#endif

#ifdef __REACTOS__
    len = count ? MultiByteToWideChar(CP_ACP, 0, str, count, NULL, 0) : 0;
    if ((count && !len) || (size_t)len >= (size_t)-1 / sizeof(WCHAR)) return NULL;
    ret = malloc(((size_t)len + 1) * sizeof(WCHAR));
    if (!ret) return NULL;
    if (count && !MultiByteToWideChar(CP_ACP, 0, str, count, ret, len))
    {
        free(ret);
        return NULL;
    }
#else
    len = MultiByteToWideChar(CP_ACP, 0, str, p - str, NULL, 0 );
    ret = malloc((len + 1) * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, str, p - str, ret, len );
#endif
    ret[len] = 0;

    return ret;
}

static LPSTR SQLInstall_strdup_multiWtoA(LPCWSTR str)
{
    LPCWSTR p;
    LPSTR ret = NULL;
#ifdef __REACTOS__
    size_t count = 0, size;
    int len;
#else
    DWORD len;
#endif

    if (!str)
        return ret;

#ifdef __REACTOS__
    for (p = str; *p; p += size)
    {
        size = wcslen(p) + 1;
        if (size > INT_MAX - count) return NULL;
        count += size;
    }
#else
    for (p = str; *p; p += lstrlenW(p) + 1)
        ;
#endif

#ifdef __REACTOS__
    len = count ? WideCharToMultiByte(CP_ACP, 0, str, count, NULL, 0, NULL, NULL) : 0;
    if (count && !len) return NULL;
    ret = malloc((size_t)len + 1);
    if (!ret) return NULL;
    if (count && !WideCharToMultiByte(CP_ACP, 0, str, count, ret, len, NULL, NULL))
    {
        free(ret);
        return NULL;
    }
#else
    len = WideCharToMultiByte(CP_ACP, 0, str,   p - str, NULL, 0, NULL, NULL );
    ret = malloc((len + 1));
    WideCharToMultiByte(CP_ACP, 0, str, p - str, ret, len, NULL, NULL );
#endif
    ret[len] = 0;

    return ret;
}

static inline char *strdupWtoA( const WCHAR *str )
{
    char *ret = NULL;
    if (str)
    {
#ifdef __REACTOS__
        int len = WideCharToMultiByte( CP_ACP, 0, str, -1, NULL, 0, NULL, NULL );
        if (!len) return NULL;
        ret = malloc( len );
        if (ret && !WideCharToMultiByte( CP_ACP, 0, str, -1, ret, len, NULL, NULL ))
        {
            free( ret );
            ret = NULL;
        }
#else
        DWORD len = WideCharToMultiByte( CP_ACP, 0, str, -1, NULL, 0, NULL, NULL );
        if ((ret = malloc( len )))
            WideCharToMultiByte( CP_ACP, 0, str, -1, ret, len, NULL, NULL );
#endif
    }
    return ret;
}

static LPWSTR SQLInstall_strdup(LPCSTR str)
{
#ifdef __REACTOS__
    return strdupAtoW(str);
#else
    DWORD len;
    LPWSTR ret = NULL;

    if (!str)
        return ret;

    len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0 );
    ret = malloc(len * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, str, -1, ret, len );

    return ret;
#endif
}

/* Convert the wide string or zero-length-terminated list of wide strings to a
 * narrow string or zero-length-terminated list of narrow strings.
 * Do not try to emulate windows undocumented excesses (e.g. adding a third \0
 * to a list)
 * Arguments
 *   mode Indicates the sort of string.
 *     1 denotes that the buffers contain strings terminated by a single nul
 *       character
 *     2 denotes that the buffers contain zero-length-terminated lists
 *       (frequently erroneously referred to as double-null-terminated)
 *   buffer The narrow-character buffer into which to place the result.  This
 *          must be a non-null pointer to the first element of a buffer whose
 *          length is passed in buffer_length.
 *   str The wide-character buffer containing the string or list of strings to
 *       be converted.  str_length defines how many wide characters in the
 *       buffer are to be converted, including all desired terminating nul
 *       characters.
 *   str_length Effective length of str
 *   buffer_length Length of buffer
 *   returned_length A pointer to a variable that will receive the number of
 *                   narrow characters placed into the buffer.  This pointer
 *                   may be NULL.
 */
static BOOL SQLInstall_narrow(int mode, LPSTR buffer, LPCWSTR str, WORD str_length, WORD buffer_length, WORD *returned_length)
{
    LPSTR pbuf; /* allows us to allocate a temporary buffer only if needed */
    int len; /* Length of the converted list */
    BOOL success = FALSE;
    assert(mode == 1 || mode == 2);
    assert(buffer_length);
    len = WideCharToMultiByte(CP_ACP, 0, str, str_length, 0, 0, NULL, NULL);
    if (len > 0)
    {
        if (len > buffer_length)
        {
            pbuf = malloc(len);
#ifdef __REACTOS__
            if (!pbuf) return FALSE;
#endif
        }
        else
        {
            pbuf = buffer;
        }
        len = WideCharToMultiByte(CP_ACP, 0, str, str_length, pbuf, len, NULL, NULL);
        if (len > 0)
        {
            if (pbuf != buffer)
            {
                if (buffer_length > (mode - 1))
                {
                    memcpy (buffer, pbuf, buffer_length-mode);
                    *(buffer+buffer_length-mode) = '\0';
                }
                *(buffer+buffer_length-1) = '\0';
            }
            if (returned_length)
            {
                *returned_length = pbuf == buffer ? len : buffer_length;
            }
            success = TRUE;
        }
        else
        {
            ERR("transferring wide to narrow\n");
        }
        if (pbuf != buffer)
        {
            free(pbuf);
        }
    }
    else
    {
        ERR("measuring wide to narrow\n");
    }
    return success;
}

static HMODULE load_config_driver(const WCHAR *driver)
{
    long ret;
    HMODULE hmod;
    WCHAR *filename = NULL;
    DWORD size = 0, type;
    HKEY hkey;

    if ((ret = RegOpenKeyW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI\\", &hkey)) == ERROR_SUCCESS)
    {
        HKEY hkeydriver;

        if ((ret = RegOpenKeyW(hkey, driver, &hkeydriver)) == ERROR_SUCCESS)
        {
            ret = RegGetValueW(hkeydriver, NULL, L"Setup", RRF_RT_REG_SZ, &type, NULL, &size);
            if(ret != ERROR_SUCCESS || type != REG_SZ)
            {
                RegCloseKey(hkeydriver);
                RegCloseKey(hkey);
                push_error(ODBC_ERROR_INVALID_DSN, L"Invalid DSN");

                return NULL;
            }

            filename = malloc(size);
            if(!filename)
            {
                RegCloseKey(hkeydriver);
                RegCloseKey(hkey);
                push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");

                return NULL;
            }
            ret = RegGetValueW(hkeydriver, NULL, L"Setup", RRF_RT_REG_SZ, &type, filename, &size);

            RegCloseKey(hkeydriver);
        }

        RegCloseKey(hkey);
    }

    if(ret != ERROR_SUCCESS)
    {
        free(filename);
        push_error(ODBC_ERROR_COMPONENT_NOT_FOUND, L"Component not found");
        return NULL;
    }

    hmod = LoadLibraryExW(filename, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    free(filename);

    if(!hmod)
        push_error(ODBC_ERROR_LOAD_LIB_FAILED, L"Load Library Failed");

    return hmod;
}

static BOOL write_config_value(const WCHAR *driver, const WCHAR *args)
{
    long ret;
    HKEY hkey, hkeydriver;
    WCHAR *name = NULL;

    if(!args)
        return FALSE;

    if((ret = RegOpenKeyW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI\\", &hkey)) == ERROR_SUCCESS)
    {
        if((ret = RegOpenKeyW(hkey, driver, &hkeydriver)) == ERROR_SUCCESS)
        {
            WCHAR *divider, *value;

            name = malloc((wcslen(args) + 1) * sizeof(WCHAR));
            if(!name)
            {
                push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
                goto fail;
            }
            lstrcpyW(name, args);

            divider = wcschr(name,'=');
            if(!divider)
            {
                push_error(ODBC_ERROR_INVALID_KEYWORD_VALUE, L"Invalid keyword value");
                goto fail;
            }

            value = divider + 1;
            *divider = '\0';

            TRACE("Write pair: %s = %s\n", debugstr_w(name), debugstr_w(value));
            if(RegSetValueExW(hkeydriver, name, 0, REG_SZ, (BYTE*)value,
                               (lstrlenW(value)+1) * sizeof(WCHAR)) != ERROR_SUCCESS)
                ERR("Failed to write registry installed key\n");
            free(name);

            RegCloseKey(hkeydriver);
        }

        RegCloseKey(hkey);
    }

    if(ret != ERROR_SUCCESS)
        push_error(ODBC_ERROR_COMPONENT_NOT_FOUND, L"Component not found");

    return ret == ERROR_SUCCESS;

fail:
    RegCloseKey(hkeydriver);
    RegCloseKey(hkey);
    free(name);

    return FALSE;
}

static WORD map_request(WORD request)
{
    switch (request)
    {
    case ODBC_ADD_DSN:
    case ODBC_ADD_SYS_DSN:
        return ODBC_ADD_DSN;

    case ODBC_CONFIG_DSN:
    case ODBC_CONFIG_SYS_DSN:
        return ODBC_CONFIG_DSN;

    case ODBC_REMOVE_DSN:
    case ODBC_REMOVE_SYS_DSN:
        return ODBC_REMOVE_DSN;

    default:
        FIXME("unhandled request %u\n", request);
        return 0;
    }
}

static UWORD get_config_mode(WORD request)
{
    if (request == ODBC_ADD_DSN || request == ODBC_CONFIG_DSN || request == ODBC_REMOVE_DSN) return ODBC_USER_DSN;
    return ODBC_SYSTEM_DSN;
}

BOOL WINAPI SQLConfigDataSourceW(HWND hwnd, WORD request, LPCWSTR driver, LPCWSTR attributes)
{
    HMODULE mod;
    BOOL ret = FALSE;
    UWORD config_mode_prev = config_mode;
    WORD mapped_request;

    TRACE("%p, %d, %s, %s\n", hwnd, request, debugstr_w(driver), debugstr_w(attributes));
    if (TRACE_ON(odbc) && attributes)
    {
        const WCHAR *p;
        for (p = attributes; *p; p += lstrlenW(p) + 1)
            TRACE("%s\n", debugstr_w(p));
    }

    clear_errors();

    mapped_request = map_request(request);
    if (!mapped_request)
        return FALSE;

    mod = load_config_driver(driver);
    if (!mod)
        return FALSE;

    config_mode = get_config_mode(request);

    pConfigDSNW = (void*)GetProcAddress(mod, "ConfigDSNW");
    if(pConfigDSNW)
        ret = pConfigDSNW(hwnd, mapped_request, driver, attributes);
    else
    {
        pConfigDSN = (void*)GetProcAddress(mod, "ConfigDSN");
        if (pConfigDSN)
        {
            LPSTR attr = SQLInstall_strdup_multiWtoA(attributes);
            char *driverA = strdupWtoA(driver);
            TRACE("Calling ConfigDSN\n");

#ifdef __REACTOS__
            if (!driverA || (attributes && !attr))
                push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
            else
                ret = pConfigDSN(hwnd, mapped_request, driverA, attr);
#else
            ret = pConfigDSN(hwnd, mapped_request, driverA, attr);
#endif
            free(attr);
            free(driverA);
        }
        else
            ERR("Failed to find ConfigDSN/W\n");
    }

    config_mode = config_mode_prev;

    if (!ret)
        push_error(ODBC_ERROR_REQUEST_FAILED, L"Request Failed");

    FreeLibrary(mod);

    return ret;
}

BOOL WINAPI SQLConfigDataSource(HWND hwnd, WORD request, LPCSTR driver, LPCSTR attributes)
{
    HMODULE mod;
    BOOL ret = FALSE;
    WCHAR *driverW;
    UWORD config_mode_prev = config_mode;
    WORD mapped_request;

    TRACE("%p, %d, %s, %s\n", hwnd, request, debugstr_a(driver), debugstr_a(attributes));

    if (TRACE_ON(odbc) && attributes)
    {
        const char *p;
        for (p = attributes; *p; p += lstrlenA(p) + 1)
            TRACE("%s\n", debugstr_a(p));
    }

    clear_errors();

    mapped_request = map_request(request);
    if (!mapped_request)
        return FALSE;

    driverW = strdupAtoW(driver);
    if (!driverW)
    {
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        return FALSE;
    }

    mod = load_config_driver(driverW);
    if (!mod)
    {
        free(driverW);
        return FALSE;
    }

    config_mode = get_config_mode(request);

    pConfigDSN = (void*)GetProcAddress(mod, "ConfigDSN");
    if (pConfigDSN)
    {
        TRACE("Calling ConfigDSN\n");
        ret = pConfigDSN(hwnd, mapped_request, driver, attributes);
    }
    else
    {
        pConfigDSNW = (void*)GetProcAddress(mod, "ConfigDSNW");
        if (pConfigDSNW)
        {
            WCHAR *attr = NULL;
            TRACE("Calling ConfigDSNW\n");

            attr = SQLInstall_strdup_multi(attributes);
#ifdef __REACTOS__
            if (!attributes || attr)
#else
            if(attr)
#endif
                ret = pConfigDSNW(hwnd, mapped_request, driverW, attr);
#ifdef __REACTOS__
            else
                push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
#endif
            free(attr);
        }
    }

    config_mode = config_mode_prev;

    if (!ret)
        push_error(ODBC_ERROR_REQUEST_FAILED, L"Request Failed");

    free(driverW);
    FreeLibrary(mod);

    return ret;
}

BOOL WINAPI SQLConfigDriverW(HWND hwnd, WORD request, LPCWSTR driver,
               LPCWSTR args, LPWSTR msg, WORD msgmax, WORD *msgout)
{
    BOOL (WINAPI *pConfigDriverW)(HWND hwnd, WORD request, const WCHAR *driver, const WCHAR *args, const WCHAR *msg, WORD msgmax, WORD *msgout);
    HMODULE hmod;
    BOOL funcret = FALSE;

    clear_errors();
    TRACE("(%p %d %s %s %p %d %p)\n", hwnd, request, debugstr_w(driver),
          debugstr_w(args), msg, msgmax, msgout);

    if(request == ODBC_CONFIG_DRIVER)
    {
        return write_config_value(driver, args);
    }

    hmod = load_config_driver(driver);
    if(!hmod)
        return FALSE;

    pConfigDriverW = (void*)GetProcAddress(hmod, "ConfigDriverW");
    if(pConfigDriverW)
        funcret = pConfigDriverW(hwnd, request, driver, args, msg, msgmax, msgout);

    if(!funcret)
        push_error(ODBC_ERROR_REQUEST_FAILED, L"Request Failed");

    FreeLibrary(hmod);

    return funcret;
}

BOOL WINAPI SQLConfigDriver(HWND hwnd, WORD request, LPCSTR driver,
               LPCSTR args, LPSTR msg, WORD msgmax, WORD *msgout)
{
    BOOL (WINAPI *pConfigDriverA)(HWND hwnd, WORD request, const char *driver, const char *args, const char *msg, WORD msgmax, WORD *msgout);
    HMODULE hmod;
    WCHAR *driverW;
    BOOL funcret = FALSE;

    clear_errors();
    TRACE("(%p %d %s %s %p %d %p)\n", hwnd, request, debugstr_a(driver),
          debugstr_a(args), msg, msgmax, msgout);

    driverW = strdupAtoW(driver);
    if(!driverW)
    {
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        return FALSE;
    }
    if(request == ODBC_CONFIG_DRIVER)
    {
        BOOL ret = FALSE;
        WCHAR *argsW = strdupAtoW(args);
        if(argsW)
        {
            ret = write_config_value(driverW, argsW);
            free(argsW);
        }
        else
        {
            push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        }

        free(driverW);

        return ret;
    }

    hmod = load_config_driver(driverW);
    free(driverW);
    if(!hmod)
        return FALSE;

    pConfigDriverA = (void*)GetProcAddress(hmod, "ConfigDriver");
    if(pConfigDriverA)
        funcret = pConfigDriverA(hwnd, request, driver, args, msg, msgmax, msgout);

    if(!funcret)
        push_error(ODBC_ERROR_REQUEST_FAILED, L"Request Failed");

    FreeLibrary(hmod);

    return funcret;
}

BOOL WINAPI SQLCreateDataSourceW(HWND hwnd, LPCWSTR lpszDS)
{
    clear_errors();
    FIXME("%p %s\n", hwnd, debugstr_w(lpszDS));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLCreateDataSource(HWND hwnd, LPCSTR lpszDS)
{
    clear_errors();
    FIXME("%p %s\n", hwnd, debugstr_a(lpszDS));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLGetAvailableDriversW(LPCWSTR lpszInfFile, LPWSTR lpszBuf,
               WORD cbBufMax, WORD *pcbBufOut)
{
    clear_errors();
    FIXME("%s %p %d %p\n", debugstr_w(lpszInfFile), lpszBuf, cbBufMax, pcbBufOut);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLGetAvailableDrivers(LPCSTR lpszInfFile, LPSTR lpszBuf,
               WORD cbBufMax, WORD *pcbBufOut)
{
    clear_errors();
    FIXME("%s %p %d %p\n", debugstr_a(lpszInfFile), lpszBuf, cbBufMax, pcbBufOut);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLGetConfigMode(UWORD *pwConfigMode)
{
    clear_errors();
    TRACE("%p\n", pwConfigMode);
    if (pwConfigMode)
        *pwConfigMode = config_mode;
    return TRUE;
}

BOOL WINAPI SQLGetInstalledDriversW(WCHAR *buf, WORD size, WORD *sizeout)
{
    WORD written = 0;
    DWORD index = 0;
    BOOL ret = TRUE;
    DWORD valuelen;
    WCHAR *value;
    HKEY drivers;
    DWORD len;
    LONG res;

    clear_errors();

    TRACE("%p %d %p\n", buf, size, sizeout);

    if (!buf || !size)
    {
        push_error(ODBC_ERROR_INVALID_BUFF_LEN, L"Invalid buffer length");
        return FALSE;
    }

    res = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI\\ODBC Drivers", 0,
                        KEY_QUERY_VALUE, &drivers);
    if (res)
    {
        push_error(ODBC_ERROR_COMPONENT_NOT_FOUND, L"Component not found");
        return FALSE;
    }

    valuelen = 256;
    value = malloc(valuelen * sizeof(WCHAR));
#ifdef __REACTOS__
    if (!value) goto out_of_memory;
#endif

    size--;

    while (1)
    {
        len = valuelen;
        res = RegEnumValueW(drivers, index, value, &len, NULL, NULL, NULL, NULL);
        while (res == ERROR_MORE_DATA)
        {
#ifdef __REACTOS__
            WCHAR *new_value;

            if (valuelen > MAXDWORD / 2 || (SIZE_T)valuelen > ~(SIZE_T)0 / sizeof(WCHAR) / 2)
                goto out_of_memory;
            valuelen *= 2;
            new_value = realloc(value, valuelen * sizeof(WCHAR));
            if (!new_value) goto out_of_memory;
            value = new_value;
            len = valuelen;
#else
            value = realloc(value, ++len * sizeof(WCHAR));
#endif
            res = RegEnumValueW(drivers, index, value, &len, NULL, NULL, NULL, NULL);
        }
        if (res == ERROR_SUCCESS)
        {
            lstrcpynW(buf + written, value, size - written);
            written += min(len + 1, size - written);
        }
        else if (res == ERROR_NO_MORE_ITEMS)
            break;
        else
        {
            push_error(ODBC_ERROR_GENERAL_ERR, L"General error");
            ret = FALSE;
            break;
        }
        index++;
    }

#ifdef __REACTOS__
done:
#endif
    buf[written++] = 0;

    free(value);
    RegCloseKey(drivers);
    if (sizeout)
        *sizeout = written;
    return ret;
#ifdef __REACTOS__

out_of_memory:
    push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
    ret = FALSE;
    goto done;
#endif
}

BOOL WINAPI SQLGetInstalledDrivers(char *buf, WORD size, WORD *sizeout)
{
    WORD written;
    WCHAR *wbuf;
    BOOL ret;

    TRACE("%p %d %p\n", buf, size, sizeout);

    if (!buf || !size)
    {
        push_error(ODBC_ERROR_INVALID_BUFF_LEN, L"Invalid buffer length");
        return FALSE;
    }

    wbuf = malloc(size * sizeof(WCHAR));
    if (!wbuf)
    {
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        return FALSE;
    }

    ret = SQLGetInstalledDriversW(wbuf, size, &written);
    if (!ret)
    {
        free(wbuf);
        return FALSE;
    }

    if (sizeout)
        *sizeout = WideCharToMultiByte(CP_ACP, 0, wbuf, written, NULL, 0, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, wbuf, written, buf, size, NULL, NULL);

    free(wbuf);
    return TRUE;
}

static HKEY get_privateprofile_sectionkey(HKEY root, const WCHAR *section, const WCHAR *filename)
{
    HKEY hkey, hkeyfilename, hkeysection;
    LONG ret;

    if (RegOpenKeyW(root, L"Software\\ODBC", &hkey))
        return NULL;

    ret = RegOpenKeyW(hkey, filename, &hkeyfilename);
    RegCloseKey(hkey);
    if (ret)
        return NULL;

    ret = RegOpenKeyW(hkeyfilename, section, &hkeysection);
    RegCloseKey(hkeyfilename);

    return ret ? NULL : hkeysection;
}

int WINAPI SQLGetPrivateProfileStringW(LPCWSTR section, LPCWSTR entry,
    LPCWSTR defvalue, LPWSTR buff, int buff_len, LPCWSTR filename)
{
    BOOL usedefault = TRUE;
    HKEY sectionkey;
    LONG ret = 0;

    TRACE("%s %s %s %p %d %s\n", debugstr_w(section), debugstr_w(entry),
               debugstr_w(defvalue), buff, buff_len, debugstr_w(filename));

    clear_errors();

    if (buff_len <= 0 || !section)
        return 0;

    if(buff)
        buff[0] = 0;

    if (!defvalue || !buff)
        return 0;

    /* odbcinit.ini is only for drivers, so default to local Machine */
    if (!wcsicmp(filename, L"ODBCINST.INI") || config_mode == ODBC_SYSTEM_DSN)
        sectionkey = get_privateprofile_sectionkey(HKEY_LOCAL_MACHINE, section, filename);
    else if (config_mode == ODBC_USER_DSN)
        sectionkey = get_privateprofile_sectionkey(HKEY_CURRENT_USER, section, filename);
    else
    {
        sectionkey = get_privateprofile_sectionkey(HKEY_CURRENT_USER, section, filename);
        if (!sectionkey) sectionkey = get_privateprofile_sectionkey(HKEY_LOCAL_MACHINE, section, filename);
    }

    if (sectionkey)
    {
        DWORD type, size;

        if (entry)
        {
            size = buff_len * sizeof(*buff);
            if (RegGetValueW(sectionkey, NULL, entry, RRF_RT_REG_SZ, &type, buff, &size) == ERROR_SUCCESS)
            {
                usedefault = FALSE;
                ret = (size / sizeof(*buff)) - 1;
            }
        }
        else
        {
            WCHAR name[MAX_PATH];
            DWORD index = 0;
            DWORD namelen;

            usedefault = FALSE;

            memset(buff, 0, buff_len);

            namelen = sizeof(name);
            while (RegEnumValueW(sectionkey, index, name, &namelen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
            {
                if ((ret +  namelen+1) > buff_len)
                    break;

                lstrcpyW(buff+ret, name);
                ret += namelen+1;
                namelen = sizeof(name);
                index++;
            }
        }

        RegCloseKey(sectionkey);
    }
    else
        usedefault = entry != NULL;

    if (usedefault)
    {
        lstrcpynW(buff, defvalue, buff_len);
        ret = lstrlenW(buff);
    }

    return ret;
}

int WINAPI SQLGetPrivateProfileString(LPCSTR section, LPCSTR entry,
    LPCSTR defvalue, LPSTR buff, int buff_len, LPCSTR filename)
{
    WCHAR *sectionW, *filenameW;
    BOOL usedefault = TRUE;
    HKEY sectionkey;
    LONG ret = 0;

    TRACE("%s %s %s %p %d %s\n", debugstr_a(section), debugstr_a(entry),
               debugstr_a(defvalue), buff, buff_len, debugstr_a(filename));

    clear_errors();

    if (buff_len <= 0)
        return 0;

    if (buff)
        buff[0] = 0;

    if (!section || !defvalue || !buff)
        return 0;

    if (!(sectionW = strdupAtoW(section))) return 0;
    if (!(filenameW = strdupAtoW(filename)))
    {
        free(sectionW);
        return 0;
    }

    if (config_mode == ODBC_USER_DSN)
        sectionkey = get_privateprofile_sectionkey(HKEY_CURRENT_USER, sectionW, filenameW);
    else if (config_mode == ODBC_SYSTEM_DSN)
        sectionkey = get_privateprofile_sectionkey(HKEY_LOCAL_MACHINE, sectionW, filenameW);
    else
    {
        sectionkey = get_privateprofile_sectionkey(HKEY_CURRENT_USER, sectionW, filenameW);
        if (!sectionkey) sectionkey = get_privateprofile_sectionkey(HKEY_LOCAL_MACHINE, sectionW, filenameW);
    }

    free(sectionW);
    free(filenameW);

    if (sectionkey)
    {
        DWORD type, size;

        if (entry)
        {
            size = buff_len * sizeof(*buff);
            if (RegGetValueA(sectionkey, NULL, entry, RRF_RT_REG_SZ, &type, buff, &size) == ERROR_SUCCESS)
            {
                usedefault = FALSE;
                ret = (size / sizeof(*buff)) - 1;
            }
        }
        else
        {
            char name[MAX_PATH] = {0};
            DWORD index = 0;
            DWORD namelen;

            usedefault = FALSE;

            memset(buff, 0, buff_len);

            namelen = sizeof(name);
            while (RegEnumValueA(sectionkey, index, name, &namelen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
            {
                if ((ret +  namelen+1) > buff_len)
                    break;

                lstrcpyA(buff+ret, name);

                ret += namelen+1;
                namelen = sizeof(name);
                index++;
            }
        }

        RegCloseKey(sectionkey);
    }
    else
        usedefault = entry != NULL;

    if (usedefault)
    {
        lstrcpynA(buff, defvalue, buff_len);
        ret = strlen(buff);
    }

    return ret;
}

BOOL WINAPI SQLGetTranslatorW(HWND hwndParent, LPWSTR lpszName, WORD cbNameMax,
               WORD *pcbNameOut, LPWSTR lpszPath, WORD cbPathMax,
               WORD *pcbPathOut, DWORD *pvOption)
{
    clear_errors();
    FIXME("%p %s %d %p %p %d %p %p\n", hwndParent, debugstr_w(lpszName), cbNameMax,
               pcbNameOut, lpszPath, cbPathMax, pcbPathOut, pvOption);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLGetTranslator(HWND hwndParent, LPSTR lpszName, WORD cbNameMax,
               WORD *pcbNameOut, LPSTR lpszPath, WORD cbPathMax,
               WORD *pcbPathOut, DWORD *pvOption)
{
    clear_errors();
    FIXME("%p %s %d %p %p %d %p %p\n", hwndParent, debugstr_a(lpszName), cbNameMax,
               pcbNameOut, lpszPath, cbPathMax, pcbPathOut, pvOption);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLInstallDriverW(LPCWSTR lpszInfFile, LPCWSTR lpszDriver,
               LPWSTR lpszPath, WORD cbPathMax, WORD * pcbPathOut)
{
    DWORD usage;

    clear_errors();
    TRACE("%s %s %p %d %p\n", debugstr_w(lpszInfFile),
          debugstr_w(lpszDriver), lpszPath, cbPathMax, pcbPathOut);

    if (lpszInfFile)
        return FALSE;

    return SQLInstallDriverExW(lpszDriver, NULL, lpszPath, cbPathMax,
                               pcbPathOut, ODBC_INSTALL_COMPLETE, &usage);
}

BOOL WINAPI SQLInstallDriver(LPCSTR lpszInfFile, LPCSTR lpszDriver,
               LPSTR lpszPath, WORD cbPathMax, WORD * pcbPathOut)
{
    DWORD usage;

    clear_errors();
    TRACE("%s %s %p %d %p\n", debugstr_a(lpszInfFile),
          debugstr_a(lpszDriver), lpszPath, cbPathMax, pcbPathOut);

    if (lpszInfFile)
        return FALSE;

    return SQLInstallDriverEx(lpszDriver, NULL, lpszPath, cbPathMax,
                              pcbPathOut, ODBC_INSTALL_COMPLETE, &usage);
}

#ifdef __REACTOS__
static BOOL get_install_path(const WCHAR *driver, const WCHAR *file_key, const WCHAR *path_in,
                             WCHAR *path, DWORD *usage_count, BOOL *installed)
{
    HKEY root, key = NULL;
    WCHAR *filename = NULL, *separator, *slash;
    DWORD error = ODBC_ERROR_GENERAL_ERR;
    DWORD size, capacity;
    size_t len;
    LONG status;
    BOOL ret = FALSE;

    *usage_count = 0;
    *installed = FALSE;
    if (!driver || !*driver)
    {
        error = ODBC_ERROR_INVALID_PARAM_SEQUENCE;
        goto done;
    }

    status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI", 0, KEY_READ, &root);
    if (status == ERROR_FILE_NOT_FOUND) goto default_path;
    if (status != ERROR_SUCCESS) goto done;
    status = RegOpenKeyExW(root, driver, 0, KEY_READ, &key);
    RegCloseKey(root);
    if (status != ERROR_SUCCESS)
    {
        key = NULL;
        if (status == ERROR_FILE_NOT_FOUND) goto default_path;
        goto done;
    }

    size = sizeof(*usage_count);
    status = RegGetValueW(key, NULL, L"UsageCount", RRF_RT_DWORD, NULL, usage_count, &size);
    if (status == ERROR_FILE_NOT_FOUND)
        *usage_count = 1;
    else if (status != ERROR_SUCCESS || size != sizeof(*usage_count))
    {
        error = ODBC_ERROR_USAGE_UPDATE_FAILED;
        goto done;
    }

    size = 0;
    status = RegGetValueW(key, NULL, file_key, RRF_RT_REG_SZ, NULL, NULL, &size);
    if (status == ERROR_FILE_NOT_FOUND) goto default_path;
    if (status != ERROR_SUCCESS) goto done;
    if (!size || size % sizeof(WCHAR))
    {
        error = ODBC_ERROR_INVALID_PATH;
        goto done;
    }
    if (size > MAXDWORD - sizeof(WCHAR))
    {
        error = ODBC_ERROR_OUT_OF_MEM;
        goto done;
    }
    capacity = size + sizeof(WCHAR);
    filename = malloc(capacity);
    if (!filename)
    {
        error = ODBC_ERROR_OUT_OF_MEM;
        goto done;
    }
    status = RegGetValueW(key, NULL, file_key, RRF_RT_REG_SZ, NULL, filename, &capacity);
    if (status != ERROR_SUCCESS || !capacity || capacity % sizeof(WCHAR) || capacity > size + sizeof(WCHAR))
        goto done;
    filename[capacity / sizeof(WCHAR) - 1] = 0;
    separator = wcsrchr(filename, '\\');
    slash = wcsrchr(filename, '/');
    if (slash && (!separator || slash > separator)) separator = slash;
    if (!separator || !separator[1])
    {
        error = ODBC_ERROR_INVALID_PATH;
        goto done;
    }
    len = separator - filename;
    if (len && separator[-1] == ':') len++;
    if (!len || len >= MAX_PATH)
    {
        error = ODBC_ERROR_INVALID_PATH;
        goto done;
    }
    memcpy(path, filename, len * sizeof(WCHAR));
    path[len] = 0;
    *installed = TRUE;
    ret = TRUE;
    goto done;

default_path:
    if (path_in)
    {
        len = wcslen(path_in);
        if (!len || len >= MAX_PATH)
        {
            error = ODBC_ERROR_INVALID_PATH;
            goto done;
        }
        memcpy(path, path_in, (len + 1) * sizeof(WCHAR));
    }
    else
    {
        len = GetSystemDirectoryW(path, MAX_PATH);
        if (!len || len >= MAX_PATH) goto done;
    }
    ret = TRUE;
done:
    free(filename);
    if (key) RegCloseKey(key);
    if (!ret) push_error(error, L"Failed to read installation path");
    return ret;
}

static BOOL copy_install_path(const WCHAR *path, void *buffer, WORD capacity, WORD *length,
                              BOOL unicode, BOOL inquiry)
{
    size_t len;
    int size;
    char *converted;

    if (unicode)
    {
        len = wcslen(path);
        if (len > USHRT_MAX) goto invalid_buffer;
        if (length) *length = len;
        if (!buffer || !capacity)
        {
            if (inquiry) return TRUE;
            goto invalid_buffer;
        }
        memcpy(buffer, path, min(len, capacity - 1) * sizeof(WCHAR));
        ((WCHAR *)buffer)[min(len, capacity - 1)] = 0;
        if (len >= capacity) goto invalid_buffer;
    }
    else
    {
        size = WideCharToMultiByte(CP_ACP, 0, path, -1, NULL, 0, NULL, NULL);
        if (!size) goto conversion_failed;
        if (size - 1 > USHRT_MAX) goto invalid_buffer;
        if (length) *length = size - 1;
        if (!buffer || !capacity)
        {
            if (inquiry) return TRUE;
            goto invalid_buffer;
        }
        if (size <= capacity)
        {
            if (!WideCharToMultiByte(CP_ACP, 0, path, -1, buffer, capacity, NULL, NULL))
                goto conversion_failed;
        }
        else
        {
            if (!(converted = malloc(size)))
            {
                push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
                return FALSE;
            }
            if (!WideCharToMultiByte(CP_ACP, 0, path, -1, converted, size, NULL, NULL))
            {
                free(converted);
                goto conversion_failed;
            }
            memcpy(buffer, converted, capacity - 1);
            ((char *)buffer)[capacity - 1] = 0;
            free(converted);
            goto invalid_buffer;
        }
    }
    return TRUE;

invalid_buffer:
    push_error(ODBC_ERROR_INVALID_BUFF_LEN, L"Invalid buffer length");
    return FALSE;
conversion_failed:
    push_error(ODBC_ERROR_GENERAL_ERR, L"Failed to convert installation path");
    return FALSE;
}

static BOOL write_registry_values(const WCHAR *regkey, const WCHAR *driver, const WCHAR *path,
                                  BOOL installed, DWORD *usage_count)
#else
static void write_registry_values(const WCHAR *regkey, const WCHAR *driver, const  WCHAR *path_in, WCHAR *path,
                                  DWORD *usage_count)
#endif
{
    HKEY hkey, hkeydriver;
#ifdef __REACTOS__
    DWORD error = ODBC_ERROR_GENERAL_ERR, disposition;
    size_t pathlen = wcslen(path);
    LONG status;
    BOOL ret = FALSE;
#endif

    if (RegCreateKeyW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI\\", &hkey) == ERROR_SUCCESS)
    {
        if (RegCreateKeyW(hkey, regkey, &hkeydriver) == ERROR_SUCCESS)
        {
#ifdef __REACTOS__
            status = RegSetValueExW(hkeydriver, driver, 0, REG_SZ, (BYTE*)L"Installed", sizeof(L"Installed"));
            if (status != ERROR_SUCCESS)
#else
            if(RegSetValueExW(hkeydriver, driver, 0, REG_SZ, (BYTE*)L"Installed", sizeof(L"Installed")) != ERROR_SUCCESS)
#endif
                ERR("Failed to write registry installed key\n");

            RegCloseKey(hkeydriver);
#ifdef __REACTOS__
            if (status != ERROR_SUCCESS) goto done;
#endif
        }
#ifdef __REACTOS__
        else goto done;
#endif

#ifdef __REACTOS__
        if (RegCreateKeyExW(hkey, driver, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                            NULL, &hkeydriver, &disposition) == ERROR_SUCCESS)
#else
        if (RegCreateKeyW(hkey, driver, &hkeydriver) == ERROR_SUCCESS)
#endif
        {
            WCHAR entry[1024];
            const WCHAR *p;
            DWORD usagecount = 0;
            DWORD type, size;

            /* Skip name entry */
            p = driver;
#ifdef __REACTOS__
            p += wcslen(p) + 1;
#else
            p += lstrlenW(p) + 1;

            if (!path_in)
                GetSystemDirectoryW(path, MAX_PATH);
            else
                lstrcpyW(path, path_in);
#endif

            /* Store Usage */
            size = sizeof(usagecount);
#ifdef __REACTOS__
            status = RegGetValueA(hkeydriver, NULL, "UsageCount", RRF_RT_DWORD, &type, &usagecount, &size);
            if (status == ERROR_FILE_NOT_FOUND)
                usagecount = disposition == REG_OPENED_EXISTING_KEY ? 1 : 0;
            else if (status != ERROR_SUCCESS || size != sizeof(usagecount) || usagecount == MAXDWORD)
            {
                error = ODBC_ERROR_USAGE_UPDATE_FAILED;
                goto driver_done;
            }
#else
            RegGetValueA(hkeydriver, NULL, "UsageCount", RRF_RT_DWORD, &type, &usagecount, &size);
#endif
            TRACE("Usage count %ld\n", usagecount);

#ifdef __REACTOS__
            for (; *p && (!installed || disposition == REG_CREATED_NEW_KEY); p += wcslen(p) + 1)
#else
            for (; *p; p += lstrlenW(p) + 1)
#endif
            {
                WCHAR *divider = wcschr(p,'=');

                if (divider)
                {
                    WCHAR *value;
#ifdef __REACTOS__
                    size_t len;

                    if (divider == p || divider - p >= ARRAY_SIZE(entry))
                    {
                        error = ODBC_ERROR_INVALID_KEYWORD_VALUE;
                        goto driver_done;
                    }
#else
                    int len;
#endif

                    /* Write pair values to the registry. */
                    lstrcpynW(entry, p, divider - p + 1);

                    divider++;
                    TRACE("Writing pair %s,%s\n", debugstr_w(entry), debugstr_w(divider));
#ifdef __REACTOS__
                    len = wcslen(divider);
                    if (len >= MAXDWORD / sizeof(WCHAR))
                    {
                        error = ODBC_ERROR_OUT_OF_MEM;
                        goto driver_done;
                    }
                    len++;
#endif

                    /* Driver, Setup, Translator entries use the system path unless a path is specified. */
                    if(lstrcmpiW(L"Driver", entry) == 0 || lstrcmpiW(L"Setup", entry) == 0 ||
                       lstrcmpiW(L"Translator", entry) == 0)
                    {
                        if(GetFileAttributesW(divider) == INVALID_FILE_ATTRIBUTES)
                        {
#ifdef __REACTOS__
                            if (len > MAXDWORD / sizeof(WCHAR) - pathlen - 1)
                            {
                                error = ODBC_ERROR_OUT_OF_MEM;
                                goto driver_done;
                            }
                            len += pathlen + 1;
#else
                            int pathlen = lstrlenW(path);
                            len = pathlen + 1 + lstrlenW(divider) + 1;
#endif
                            value = malloc(len * sizeof(WCHAR));
                            if(!value)
                            {
#ifndef __REACTOS__
                                RegCloseKey(hkeydriver);
#endif
                                ERR("Out of memory\n");
#ifdef __REACTOS__
                                error = ODBC_ERROR_OUT_OF_MEM;
                                goto driver_done;
#else
                                return;
#endif
                            }

                            lstrcpyW(value, path);
                            if (pathlen && path[pathlen - 1] != '\\')
                                lstrcatW(value, L"\\");
                        }
                        else
                        {
#ifdef __REACTOS__
                            value = calloc(1, len * sizeof(WCHAR));
#else
                            value = calloc(1, (lstrlenW(divider)+1) * sizeof(WCHAR));
#endif
                            if(!value)
                            {
#ifndef __REACTOS__
                                RegCloseKey(hkeydriver);
#endif
                                ERR("Out of memory\n");
#ifdef __REACTOS__
                                error = ODBC_ERROR_OUT_OF_MEM;
                                goto driver_done;
#else
                                return;
#endif
                            }
                        }
                        lstrcatW(value, divider);
                    }
                    else
                    {
#ifndef __REACTOS__
                        len = lstrlenW(divider) + 1;
#endif
                        value = malloc(len * sizeof(WCHAR));
#ifdef __REACTOS__
                        if (!value)
                        {
                            error = ODBC_ERROR_OUT_OF_MEM;
                            goto driver_done;
                        }
#endif
                        lstrcpyW(value, divider);
                    }

#ifdef __REACTOS__
                    status = RegSetValueExW(hkeydriver, entry, 0, REG_SZ, (BYTE*)value,
                                           (wcslen(value) + 1) * sizeof(WCHAR));
                    if (status != ERROR_SUCCESS)
#else
                    if (RegSetValueExW(hkeydriver, entry, 0, REG_SZ, (BYTE*)value,
                                    (lstrlenW(value)+1)*sizeof(WCHAR)) != ERROR_SUCCESS)
#endif
                        ERR("Failed to write registry data %s %s\n", debugstr_w(entry), debugstr_w(value));
                    free(value);
#ifdef __REACTOS__
                    if (status != ERROR_SUCCESS) goto driver_done;
#endif
                }
                else
                {
                    ERR("No pair found. %s\n", debugstr_w(p));
#ifdef __REACTOS__
                    error = ODBC_ERROR_INVALID_KEYWORD_VALUE;
                    goto driver_done;
#else
                    break;
#endif
                }
            }

            /* Set Usage Count */
            usagecount++;
            if (RegSetValueExA(hkeydriver, "UsageCount", 0, REG_DWORD, (BYTE*)&usagecount, sizeof(usagecount)) != ERROR_SUCCESS)
#ifdef __REACTOS__
            {
#endif
                ERR("Failed to write registry UsageCount key\n");
#ifdef __REACTOS__
                error = ODBC_ERROR_USAGE_UPDATE_FAILED;
                goto driver_done;
            }
#endif

            if (usage_count)
                *usage_count = usagecount;
#ifdef __REACTOS__
            ret = TRUE;
#endif

#ifdef __REACTOS__
driver_done:
#endif
            RegCloseKey(hkeydriver);
        }

#ifdef __REACTOS__
done:
#endif
        RegCloseKey(hkey);
    }
#ifdef __REACTOS__
    if (!ret) push_error(error, L"Driver or translator installation failed");
    return ret;
}

static BOOL install_component(const WCHAR *regkey, const WCHAR *file_key, const WCHAR *driver,
                               const WCHAR *path_in, void *path_out, WORD capacity, WORD *length,
                               WORD request, DWORD *usage_count, BOOL unicode)
{
    WCHAR path[MAX_PATH];
    const WCHAR *p, *divider;
    DWORD usage;
    WORD required;
    BOOL installed;

    if (request != ODBC_INSTALL_INQUIRY && request != ODBC_INSTALL_COMPLETE)
    {
        push_error(ODBC_ERROR_INVALID_REQUEST_TYPE, L"Invalid request type");
        return FALSE;
    }
    if (request == ODBC_INSTALL_COMPLETE && (!path_out || !capacity))
    {
        push_error(ODBC_ERROR_INVALID_BUFF_LEN, L"Invalid buffer length");
        return FALSE;
    }
    if (!driver || !*driver || !*(p = driver + wcslen(driver) + 1))
    {
        push_error(ODBC_ERROR_INVALID_PARAM_SEQUENCE, L"Invalid parameter sequence");
        return FALSE;
    }
    for (; *p; p += wcslen(p) + 1)
    {
        divider = wcschr(p, '=');
        if (!divider || divider == p)
        {
            push_error(ODBC_ERROR_INVALID_KEYWORD_VALUE, L"Invalid keyword-value pair");
            return FALSE;
        }
    }
    if (!get_install_path(driver, file_key, path_in, path, &usage, &installed)) return FALSE;
    if (request == ODBC_INSTALL_INQUIRY)
    {
        if (!copy_install_path(path, path_out, capacity, length, unicode, TRUE)) return FALSE;
        if (usage_count) *usage_count = usage;
        return TRUE;
    }
    if (!copy_install_path(path, NULL, 0, &required, unicode, TRUE)) return FALSE;
    if (capacity <= required)
        return copy_install_path(path, path_out, capacity, length, unicode, FALSE);
    if (!write_registry_values(regkey, driver, path, installed, usage_count)) return FALSE;
    return copy_install_path(path, path_out, capacity, length, unicode, FALSE);
#endif
}

BOOL WINAPI SQLInstallDriverExW(LPCWSTR lpszDriver, LPCWSTR lpszPathIn,
               LPWSTR lpszPathOut, WORD cbPathOutMax, WORD *pcbPathOut,
               WORD fRequest, LPDWORD lpdwUsageCount)
{
#ifndef __REACTOS__
    UINT len;
    WCHAR path[MAX_PATH];

#endif
    clear_errors();
    TRACE("%s %s %p %d %p %d %p\n", debugstr_w(lpszDriver),
          debugstr_w(lpszPathIn), lpszPathOut, cbPathOutMax, pcbPathOut,
          fRequest, lpdwUsageCount);

#ifdef __REACTOS__
    return install_component(L"ODBC Drivers", L"Driver", lpszDriver, lpszPathIn, lpszPathOut,
                             cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount, TRUE);
#else
    write_registry_values(L"ODBC Drivers", lpszDriver, lpszPathIn, path, lpdwUsageCount);

    len = lstrlenW(path);

    if (pcbPathOut)
        *pcbPathOut = len;

    if (lpszPathOut && cbPathOutMax > len)
    {
        lstrcpyW(lpszPathOut, path);
        return TRUE;
    }
    return FALSE;
#endif
}

BOOL WINAPI SQLInstallDriverEx(LPCSTR lpszDriver, LPCSTR lpszPathIn,
               LPSTR lpszPathOut, WORD cbPathOutMax, WORD *pcbPathOut,
               WORD fRequest, LPDWORD lpdwUsageCount)
{
    LPWSTR driver, pathin;
#ifdef __REACTOS__
    BOOL ret = FALSE;
#else
    WCHAR pathout[MAX_PATH];
    BOOL ret;
    WORD cbOut = 0;
#endif

    clear_errors();
    TRACE("%s %s %p %d %p %d %p\n", debugstr_a(lpszDriver),
          debugstr_a(lpszPathIn), lpszPathOut, cbPathOutMax, pcbPathOut,
          fRequest, lpdwUsageCount);

    driver = SQLInstall_strdup_multi(lpszDriver);
    pathin = SQLInstall_strdup(lpszPathIn);
#ifdef __REACTOS__
    if ((lpszDriver && !driver) || (lpszPathIn && !pathin))
#else

    ret = SQLInstallDriverExW(driver, pathin, pathout, MAX_PATH, &cbOut,
                              fRequest, lpdwUsageCount);
    if (ret)
#endif
    {
#ifdef __REACTOS__
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        goto out;
#else
        int len =  WideCharToMultiByte(CP_ACP, 0, pathout, -1, lpszPathOut,
                                       0, NULL, NULL);
        if (len)
        {
            if (pcbPathOut)
                *pcbPathOut = len - 1;

            if (!lpszPathOut || cbPathOutMax < len)
            {
                ret = FALSE;
                goto out;
            }
            len =  WideCharToMultiByte(CP_ACP, 0, pathout, -1, lpszPathOut,
                                       cbPathOutMax, NULL, NULL);
        }
#endif
    }

#ifdef __REACTOS__
    ret = install_component(L"ODBC Drivers", L"Driver", driver, pathin, lpszPathOut,
                            cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount, FALSE);

#endif
out:
    free(driver);
    free(pathin);
    return ret;
}

BOOL WINAPI SQLInstallDriverManagerW(LPWSTR lpszPath, WORD cbPathMax,
               WORD *pcbPathOut)
{
    UINT len;
    WCHAR path[MAX_PATH];

    TRACE("(%p %d %p)\n", lpszPath, cbPathMax, pcbPathOut);

#ifndef __REACTOS__
    if (cbPathMax < MAX_PATH)
        return FALSE;

#endif
    clear_errors();
#ifdef __REACTOS__
    if (!lpszPath || cbPathMax < MAX_PATH)
    {
        push_error(ODBC_ERROR_INVALID_BUFF_LEN, L"Invalid buffer length");
        return FALSE;
    }
#endif

    len = GetSystemDirectoryW(path, MAX_PATH);
#ifdef __REACTOS__
    if (!len || len >= ARRAY_SIZE(path))
#else

    if (pcbPathOut)
        *pcbPathOut = len;

    if (lpszPath && cbPathMax > len)
#endif
    {
#ifdef __REACTOS__
        push_error(ODBC_ERROR_GENERAL_ERR, L"Failed to read system directory");
        return FALSE;
#else
    	lstrcpyW(lpszPath, path);
    	return TRUE;
#endif
    }
#ifdef __REACTOS__
    return copy_install_path(path, lpszPath, cbPathMax, pcbPathOut, TRUE, FALSE);
#else
    return FALSE;
#endif
}

BOOL WINAPI SQLInstallDriverManager(LPSTR lpszPath, WORD cbPathMax,
               WORD *pcbPathOut)
{
#ifdef __REACTOS__
    UINT len;
#else
    BOOL ret;
    WORD len, cbOut = 0;
#endif
    WCHAR path[MAX_PATH];

    TRACE("(%p %d %p)\n", lpszPath, cbPathMax, pcbPathOut);

#ifndef __REACTOS__
    if (cbPathMax < MAX_PATH)
        return FALSE;

#endif
    clear_errors();
#ifdef __REACTOS__
    if (!lpszPath || cbPathMax < MAX_PATH)
#else

    ret = SQLInstallDriverManagerW(path, MAX_PATH, &cbOut);
    if (ret)
#endif
    {
#ifdef __REACTOS__
        push_error(ODBC_ERROR_INVALID_BUFF_LEN, L"Invalid buffer length");
        return FALSE;
    }
#else
        len =  WideCharToMultiByte(CP_ACP, 0, path, -1, lpszPath, 0,
                                   NULL, NULL);
        if (len)
        {
            if (pcbPathOut)
                *pcbPathOut = len - 1;

            if (!lpszPath || cbPathMax < len)
                return FALSE;
#endif

#ifdef __REACTOS__
    len = GetSystemDirectoryW(path, MAX_PATH);
    if (!len || len >= ARRAY_SIZE(path))
    {
        push_error(ODBC_ERROR_GENERAL_ERR, L"Failed to read system directory");
        return FALSE;
#else
            len =  WideCharToMultiByte(CP_ACP, 0, path, -1, lpszPath,
                                       cbPathMax, NULL, NULL);
        }
#endif
    }
#ifdef __REACTOS__
    return copy_install_path(path, lpszPath, cbPathMax, pcbPathOut, FALSE, FALSE);
#else
    return ret;
#endif
}

BOOL WINAPI SQLInstallODBCW(HWND hwndParent, LPCWSTR lpszInfFile,
               LPCWSTR lpszSrcPath, LPCWSTR lpszDrivers)
{
    clear_errors();
    FIXME("%p %s %s %s\n", hwndParent, debugstr_w(lpszInfFile),
               debugstr_w(lpszSrcPath), debugstr_w(lpszDrivers));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLInstallODBC(HWND hwndParent, LPCSTR lpszInfFile,
               LPCSTR lpszSrcPath, LPCSTR lpszDrivers)
{
    clear_errors();
    FIXME("%p %s %s %s\n", hwndParent, debugstr_a(lpszInfFile),
               debugstr_a(lpszSrcPath), debugstr_a(lpszDrivers));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

SQLRETURN WINAPI SQLInstallerErrorW(WORD iError, DWORD *pfErrorCode,
               LPWSTR lpszErrorMsg, WORD cbErrorMsgMax, WORD *pcbErrorMsg)
{
    TRACE("%d %p %p %d %p\n", iError, pfErrorCode, lpszErrorMsg,
          cbErrorMsgMax, pcbErrorMsg);

    if (iError == 0)
    {
        return SQL_ERROR;
    }
    else if (iError <= num_errors)
    {
        BOOL truncated = FALSE;
        WORD len;
        LPCWSTR msg;
        iError--;
        if (pfErrorCode)
            *pfErrorCode = error_code[iError];
        msg = error_msg[iError];
        len = msg ? lstrlenW(msg) : 0;
        if (pcbErrorMsg)
            *pcbErrorMsg = len;
        len++;
        if (cbErrorMsgMax < len)
        {
            len = cbErrorMsgMax;
            truncated = TRUE;
        }
        if (lpszErrorMsg && len)
        {
            if (msg)
            {
                memcpy (lpszErrorMsg, msg, len * sizeof(WCHAR));
            }
            else
            {
                assert(len==1);
                *lpszErrorMsg = 0;
            }
        }
        else
        {
            /* Yes.  If you pass a null pointer and a large length it is not an error! */
            truncated = TRUE;
        }

        return truncated ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
    }

    /* At least on Windows 2000 , the buffers are not altered in this case.  However that is a little too dangerous a test for just now */
    if (pcbErrorMsg)
        *pcbErrorMsg = 0;

    if (lpszErrorMsg && cbErrorMsgMax > 0)
        *lpszErrorMsg = '\0';

    return SQL_NO_DATA;
}

SQLRETURN WINAPI SQLInstallerError(WORD iError, DWORD *pfErrorCode,
               LPSTR lpszErrorMsg, WORD cbErrorMsgMax, WORD *pcbErrorMsg)
{
    SQLRETURN ret;
    LPWSTR wbuf;
    WORD cbwbuf;
    TRACE("%d %p %p %d %p\n", iError, pfErrorCode, lpszErrorMsg,
          cbErrorMsgMax, pcbErrorMsg);

    wbuf = 0;
    if (lpszErrorMsg && cbErrorMsgMax)
    {
        wbuf = malloc(cbErrorMsgMax * sizeof(WCHAR));
        if (!wbuf)
            return SQL_ERROR;
    }
    ret = SQLInstallerErrorW(iError, pfErrorCode, wbuf, cbErrorMsgMax, &cbwbuf);
#ifdef __REACTOS__
    if (wbuf && ret != SQL_ERROR)
#else
    if (wbuf)
#endif
    {
        WORD cbBuf = 0;
#ifdef __REACTOS__
        if (!SQLInstall_narrow(1, lpszErrorMsg, wbuf, cbwbuf+1, cbErrorMsgMax, &cbBuf))
            ret = SQL_ERROR;
        else if (pcbErrorMsg)
#else
        SQLInstall_narrow(1, lpszErrorMsg, wbuf, cbwbuf+1, cbErrorMsgMax, &cbBuf);
        free(wbuf);
        if (pcbErrorMsg)
#endif
            *pcbErrorMsg = cbBuf-1;
    }
#ifdef __REACTOS__
    free(wbuf);
#endif
    return ret;
}

BOOL WINAPI SQLInstallTranslatorExW(LPCWSTR lpszTranslator, LPCWSTR lpszPathIn,
               LPWSTR lpszPathOut, WORD cbPathOutMax, WORD *pcbPathOut,
               WORD fRequest, LPDWORD lpdwUsageCount)
{
#ifndef __REACTOS__
    UINT len;
    WCHAR path[MAX_PATH];

#endif
    clear_errors();
    TRACE("%s %s %p %d %p %d %p\n", debugstr_w(lpszTranslator),
          debugstr_w(lpszPathIn), lpszPathOut, cbPathOutMax, pcbPathOut,
          fRequest, lpdwUsageCount);

#ifdef __REACTOS__
    return install_component(L"ODBC Translators", L"Translator", lpszTranslator, lpszPathIn,
                             lpszPathOut, cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount, TRUE);
#else
    write_registry_values(L"ODBC Translators", lpszTranslator, lpszPathIn, path, lpdwUsageCount);

    len = lstrlenW(path);

    if (pcbPathOut)
        *pcbPathOut = len;

    if (lpszPathOut && cbPathOutMax > len)
    {
        lstrcpyW(lpszPathOut, path);
        return TRUE;
    }
    return FALSE;
#endif
}

BOOL WINAPI SQLInstallTranslatorEx(LPCSTR lpszTranslator, LPCSTR lpszPathIn,
               LPSTR lpszPathOut, WORD cbPathOutMax, WORD *pcbPathOut,
               WORD fRequest, LPDWORD lpdwUsageCount)
{
    LPCSTR p;
    LPWSTR translator, pathin;
#ifdef __REACTOS__
    BOOL ret = FALSE;
#else
    WCHAR pathout[MAX_PATH];
    BOOL ret;
    WORD cbOut = 0;
#endif

    clear_errors();
    TRACE("%s %s %p %d %p %d %p\n", debugstr_a(lpszTranslator),
          debugstr_a(lpszPathIn), lpszPathOut, cbPathOutMax, pcbPathOut,
          fRequest, lpdwUsageCount);

#ifdef __REACTOS__
    for (p = lpszTranslator; p && *p; p += strlen(p) + 1)
#else
    for (p = lpszTranslator; *p; p += lstrlenA(p) + 1)
#endif
        TRACE("%s\n", debugstr_a(p));

    translator = SQLInstall_strdup_multi(lpszTranslator);
    pathin = SQLInstall_strdup(lpszPathIn);
#ifdef __REACTOS__
    if ((lpszTranslator && !translator) || (lpszPathIn && !pathin))
#else

    ret = SQLInstallTranslatorExW(translator, pathin, pathout, MAX_PATH,
                                  &cbOut, fRequest, lpdwUsageCount);
    if (ret)
#endif
    {
#ifdef __REACTOS__
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        goto out;
#else
        int len =  WideCharToMultiByte(CP_ACP, 0, pathout, -1, lpszPathOut,
                                       0, NULL, NULL);
        if (len)
        {
            if (pcbPathOut)
                *pcbPathOut = len - 1;

            if (!lpszPathOut || cbPathOutMax < len)
            {
                ret = FALSE;
                goto out;
            }
            len =  WideCharToMultiByte(CP_ACP, 0, pathout, -1, lpszPathOut,
                                       cbPathOutMax, NULL, NULL);
        }
#endif
    }

#ifdef __REACTOS__
    ret = install_component(L"ODBC Translators", L"Translator", translator, pathin, lpszPathOut,
                            cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount, FALSE);

#endif
out:
    free(translator);
    free(pathin);
    return ret;
}

BOOL WINAPI SQLInstallTranslator(LPCSTR lpszInfFile, LPCSTR lpszTranslator,
               LPCSTR lpszPathIn, LPSTR lpszPathOut, WORD cbPathOutMax,
               WORD *pcbPathOut, WORD fRequest, LPDWORD lpdwUsageCount)
{
    clear_errors();
    TRACE("%s %s %s %p %d %p %d %p\n", debugstr_a(lpszInfFile),
          debugstr_a(lpszTranslator), debugstr_a(lpszPathIn), lpszPathOut,
          cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount);

    if (lpszInfFile)
        return FALSE;

    return SQLInstallTranslatorEx(lpszTranslator, lpszPathIn, lpszPathOut,
                       cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount);
}

BOOL WINAPI SQLInstallTranslatorW(LPCWSTR lpszInfFile, LPCWSTR lpszTranslator,
              LPCWSTR lpszPathIn, LPWSTR lpszPathOut, WORD cbPathOutMax,
              WORD *pcbPathOut, WORD fRequest, LPDWORD lpdwUsageCount)
{
    clear_errors();
    TRACE("%s %s %s %p %d %p %d %p\n", debugstr_w(lpszInfFile),
          debugstr_w(lpszTranslator), debugstr_w(lpszPathIn), lpszPathOut,
          cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount);

    if (lpszInfFile)
        return FALSE;

    return SQLInstallTranslatorExW(lpszTranslator, lpszPathIn, lpszPathOut,
                        cbPathOutMax, pcbPathOut, fRequest, lpdwUsageCount);
}

BOOL WINAPI SQLManageDataSources(HWND hwnd)
{
    clear_errors();
    FIXME("%p\n", hwnd);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

SQLRETURN WINAPI SQLPostInstallerErrorW(DWORD fErrorCode, LPCWSTR szErrorMsg)
{
    FIXME("%lu %s\n", fErrorCode, debugstr_w(szErrorMsg));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

SQLRETURN WINAPI SQLPostInstallerError(DWORD fErrorCode, LPCSTR szErrorMsg)
{
    FIXME("%lu %s\n", fErrorCode, debugstr_a(szErrorMsg));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLReadFileDSNW(LPCWSTR lpszFileName, LPCWSTR lpszAppName,
               LPCWSTR lpszKeyName, LPWSTR lpszString, WORD cbString,
               WORD *pcbString)
{
    clear_errors();
    FIXME("%s %s %s %s %d %p\n", debugstr_w(lpszFileName), debugstr_w(lpszAppName),
               debugstr_w(lpszKeyName), debugstr_w(lpszString), cbString, pcbString);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLReadFileDSN(LPCSTR lpszFileName, LPCSTR lpszAppName,
               LPCSTR lpszKeyName, LPSTR lpszString, WORD cbString,
               WORD *pcbString)
{
    clear_errors();
    FIXME("%s %s %s %s %d %p\n", debugstr_a(lpszFileName), debugstr_a(lpszAppName),
               debugstr_a(lpszKeyName), debugstr_a(lpszString), cbString, pcbString);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLRemoveDefaultDataSource(void)
{
    clear_errors();
    FIXME("\n");
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLRemoveDriverW(LPCWSTR drivername, BOOL remove_dsn, LPDWORD usage_count)
{
    HKEY hkey;
    DWORD usagecount = 1;

    clear_errors();
    TRACE("%s %d %p\n", debugstr_w(drivername), remove_dsn, usage_count);

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI\\", &hkey) == ERROR_SUCCESS)
    {
        HKEY hkeydriver;

        if (RegOpenKeyW(hkey, drivername, &hkeydriver) == ERROR_SUCCESS)
        {
            DWORD size, type;
            DWORD count;

            size = sizeof(usagecount);
            RegGetValueA(hkeydriver, NULL, "UsageCount", RRF_RT_DWORD, &type, &usagecount, &size);
            TRACE("Usage count %ld\n", usagecount);
            count = usagecount - 1;
            if (count)
            {
                 if (RegSetValueExA(hkeydriver, "UsageCount", 0, REG_DWORD, (BYTE*)&count, sizeof(count)) != ERROR_SUCCESS)
                    ERR("Failed to write registry UsageCount key\n");
            }

            RegCloseKey(hkeydriver);
        }

        if (usagecount)
            usagecount--;

        if (!usagecount)
        {
            if (RegDeleteKeyW(hkey, drivername) != ERROR_SUCCESS)
                ERR("Failed to delete registry key: %s\n", debugstr_w(drivername));

            if (RegOpenKeyW(hkey, L"ODBC Drivers", &hkeydriver) == ERROR_SUCCESS)
            {
                if(RegDeleteValueW(hkeydriver, drivername) != ERROR_SUCCESS)
                    ERR("Failed to delete registry value: %s\n", debugstr_w(drivername));
                RegCloseKey(hkeydriver);
            }
        }

        RegCloseKey(hkey);
    }

    if (usage_count)
        *usage_count = usagecount;

    return TRUE;
}

BOOL WINAPI SQLRemoveDriver(LPCSTR lpszDriver, BOOL fRemoveDSN,
               LPDWORD lpdwUsageCount)
{
    WCHAR *driver;
    BOOL ret;

    clear_errors();
    TRACE("%s %d %p\n", debugstr_a(lpszDriver), fRemoveDSN, lpdwUsageCount);

    driver = SQLInstall_strdup(lpszDriver);
#ifdef __REACTOS__
    if (lpszDriver && !driver)
    {
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        return FALSE;
    }
#endif

    ret =  SQLRemoveDriverW(driver, fRemoveDSN, lpdwUsageCount);

    free(driver);
    return ret;
}

BOOL WINAPI SQLRemoveDriverManager(LPDWORD pdwUsageCount)
{
    clear_errors();
    FIXME("%p\n", pdwUsageCount);
    if (pdwUsageCount) *pdwUsageCount = 1;
    return TRUE;
}

BOOL WINAPI SQLRemoveDSNFromIniW(LPCWSTR lpszDSN)
{
    HKEY hkey, hkeyroot = HKEY_CURRENT_USER;

    TRACE("%s\n", debugstr_w(lpszDSN));

    if (!SQLValidDSNW(lpszDSN))
    {
        push_error(ODBC_ERROR_INVALID_DSN, L"Invalid DSN");
        return FALSE;
    }

    clear_errors();

    if (config_mode == ODBC_SYSTEM_DSN)
        hkeyroot = HKEY_LOCAL_MACHINE;
    else if (config_mode == ODBC_BOTH_DSN)
    {
        WCHAR *regpath = malloc( (wcslen(L"Software\\ODBC\\ODBC.INI\\") + wcslen(lpszDSN) + 1) * sizeof(WCHAR) );
        if (!regpath)
        {
            push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
            return FALSE;
        }
        wcscpy(regpath, L"Software\\ODBC\\ODBC.INI\\");
        wcscat(regpath, lpszDSN);

        /* ONLY removes one DSN, USER or SYSTEM */
        if (RegOpenKeyW(HKEY_CURRENT_USER, regpath, &hkey) == ERROR_SUCCESS)
            hkeyroot = HKEY_CURRENT_USER;
        else
            hkeyroot = HKEY_LOCAL_MACHINE;

        RegCloseKey(hkey);
        free(regpath);
    }

    if (RegOpenKeyW(hkeyroot, L"Software\\ODBC\\ODBC.INI\\ODBC Data Sources", &hkey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hkey, lpszDSN);
        RegCloseKey(hkey);
    }

    if (RegOpenKeyW(hkeyroot, L"Software\\ODBC\\ODBC.INI", &hkey) == ERROR_SUCCESS)
    {
        RegDeleteTreeW(hkey, lpszDSN);
        RegCloseKey(hkey);
    }

    return TRUE;
}

BOOL WINAPI SQLRemoveDSNFromIni(LPCSTR lpszDSN)
{
    BOOL ret = FALSE;
    WCHAR *dsn;

    TRACE("%s\n", debugstr_a(lpszDSN));

    clear_errors();

    dsn = SQLInstall_strdup(lpszDSN);
    if (dsn)
        ret = SQLRemoveDSNFromIniW(dsn);
    else
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");

    free(dsn);

    return ret;
}

BOOL WINAPI SQLRemoveTranslatorW(const WCHAR *translator, DWORD *usage_count)
{
    HKEY hkey;
    DWORD usagecount = 1;
    BOOL ret = TRUE;

    clear_errors();
    TRACE("%s %p\n", debugstr_w(translator), usage_count);

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI\\", &hkey) == ERROR_SUCCESS)
    {
        HKEY hkeydriver;

        if (RegOpenKeyW(hkey, translator, &hkeydriver) == ERROR_SUCCESS)
        {
            DWORD size, type;
            DWORD count;

            size = sizeof(usagecount);
            RegGetValueA(hkeydriver, NULL, "UsageCount", RRF_RT_DWORD, &type, &usagecount, &size);
            TRACE("Usage count %ld\n", usagecount);
            count = usagecount - 1;
            if (count)
            {
                 if (RegSetValueExA(hkeydriver, "UsageCount", 0, REG_DWORD, (BYTE*)&count, sizeof(count)) != ERROR_SUCCESS)
                    ERR("Failed to write registry UsageCount key\n");
            }

            RegCloseKey(hkeydriver);
        }

        if (usagecount)
            usagecount--;

        if (!usagecount)
        {
            if(RegDeleteKeyW(hkey, translator) != ERROR_SUCCESS)
            {
                push_error(ODBC_ERROR_COMPONENT_NOT_FOUND, L"Component not found");
                WARN("Failed to delete registry key: %s\n", debugstr_w(translator));
                ret = FALSE;
            }

            if (ret && RegOpenKeyW(hkey, L"ODBC Translators", &hkeydriver) == ERROR_SUCCESS)
            {
                if(RegDeleteValueW(hkeydriver, translator) != ERROR_SUCCESS)
                {
                    push_error(ODBC_ERROR_COMPONENT_NOT_FOUND, L"Component not found");
                    WARN("Failed to delete registry key: %s\n", debugstr_w(translator));
                    ret = FALSE;
                }

                RegCloseKey(hkeydriver);
            }
        }

        RegCloseKey(hkey);
    }

    if (ret && usage_count)
        *usage_count = usagecount;

    return ret;
}

BOOL WINAPI SQLRemoveTranslator(LPCSTR lpszTranslator, LPDWORD lpdwUsageCount)
{
    WCHAR *translator;
    BOOL ret;

    clear_errors();
    TRACE("%s %p\n", debugstr_a(lpszTranslator), lpdwUsageCount);

    translator = SQLInstall_strdup(lpszTranslator);
#ifdef __REACTOS__
    if (lpszTranslator && !translator)
    {
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        return FALSE;
    }
#endif
    ret =  SQLRemoveTranslatorW(translator, lpdwUsageCount);

    free(translator);
    return ret;
}

BOOL WINAPI SQLSetConfigMode(UWORD wConfigMode)
{
    clear_errors();
    TRACE("%u\n", wConfigMode);

    if (wConfigMode > ODBC_SYSTEM_DSN)
    {
        push_error(ODBC_ERROR_INVALID_PARAM_SEQUENCE, L"Invalid parameter sequence");
        return FALSE;
    }
    else
    {
        config_mode = wConfigMode;
        return TRUE;
    }
}

BOOL WINAPI SQLValidDSNW(LPCWSTR lpszDSN)
{
    clear_errors();
    TRACE("%s\n", debugstr_w(lpszDSN));

    if (!lpszDSN || !*lpszDSN || lstrlenW(lpszDSN) > SQL_MAX_DSN_LENGTH || wcspbrk(lpszDSN, L"[]{}(),;?*=!@\\"))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL WINAPI SQLValidDSN(LPCSTR lpszDSN)
{
    static const char *invalid = "[]{}(),;?*=!@\\";
    clear_errors();
    TRACE("%s\n", debugstr_a(lpszDSN));

    if (!lpszDSN || !*lpszDSN || strlen(lpszDSN) > SQL_MAX_DSN_LENGTH || strpbrk(lpszDSN, invalid))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL WINAPI SQLWriteDSNToIniW(LPCWSTR lpszDSN, LPCWSTR lpszDriver)
{
    DWORD ret;
    HKEY hkey, hkeydriver, hkeyroot = HKEY_CURRENT_USER;
    WCHAR filename[MAX_PATH];
#ifdef __REACTOS__
    size_t driverlen;
#endif

    TRACE("%s %s\n", debugstr_w(lpszDSN), debugstr_w(lpszDriver));

    clear_errors();

    if (!SQLValidDSNW(lpszDSN))
    {
        push_error(ODBC_ERROR_INVALID_DSN, L"Invalid DSN");
        return FALSE;
    }
#ifdef __REACTOS__
    if (!lpszDriver || !*lpszDriver)
    {
        push_error(ODBC_ERROR_INVALID_NAME, L"Invalid driver name");
        return FALSE;
    }
    driverlen = wcslen(lpszDriver);
    if (driverlen >= MAXDWORD / sizeof(WCHAR))
    {
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        return FALSE;
    }
#endif

    /* It doesn't matter if we cannot find the driver, windows just writes a blank value. */
    filename[0] = 0;
    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, L"Software\\ODBC\\ODBCINST.INI\\", &hkey) == ERROR_SUCCESS)
    {
        HKEY hkeydriver;

        if (RegOpenKeyW(hkey, lpszDriver, &hkeydriver) == ERROR_SUCCESS)
        {
            DWORD size = MAX_PATH * sizeof(WCHAR);
#ifdef __REACTOS__
            if (RegGetValueW(hkeydriver, NULL, L"driver", RRF_RT_REG_SZ, NULL, filename, &size) != ERROR_SUCCESS)
                filename[0] = 0;
#else
            RegGetValueW(hkeydriver, NULL, L"driver", RRF_RT_REG_SZ, NULL, filename, &size);
#endif
            RegCloseKey(hkeydriver);
        }
        RegCloseKey(hkey);
    }

    if (config_mode == ODBC_SYSTEM_DSN)
        hkeyroot = HKEY_LOCAL_MACHINE;
    else if (config_mode == ODBC_BOTH_DSN)
    {
        WCHAR *regpath = malloc( (wcslen(L"Software\\ODBC\\ODBC.INI\\") + wcslen(lpszDSN) + 1) * sizeof(WCHAR) );
        if (!regpath)
        {
            push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
            return FALSE;
        }
        wcscpy(regpath, L"Software\\ODBC\\ODBC.INI\\");
        wcscat(regpath, lpszDSN);

        /* Check for existing entry */
        if (RegOpenKeyW(HKEY_LOCAL_MACHINE, regpath, &hkey) == ERROR_SUCCESS)
#ifdef __REACTOS__
        {
#endif
            hkeyroot = HKEY_LOCAL_MACHINE;
#ifdef __REACTOS__
            RegCloseKey(hkey);
        }
#endif
        else
            hkeyroot = HKEY_CURRENT_USER;

#ifndef __REACTOS__
        RegCloseKey(hkey);
#endif
        free(regpath);
    }

    if ((ret = RegCreateKeyW(hkeyroot, L"SOFTWARE\\ODBC\\ODBC.INI", &hkey)) == ERROR_SUCCESS)
    {
        HKEY sources;

        if ((ret = RegCreateKeyW(hkey, L"ODBC Data Sources", &sources)) == ERROR_SUCCESS)
        {
#ifdef __REACTOS__
            ret = RegSetValueExW(sources, lpszDSN, 0, REG_SZ, (BYTE*)lpszDriver,
                                 (driverlen + 1) * sizeof(WCHAR));
#else
            RegSetValueExW(sources, lpszDSN, 0, REG_SZ, (BYTE*)lpszDriver, (lstrlenW(lpszDriver)+1)*sizeof(WCHAR));
#endif
            RegCloseKey(sources);

#ifdef __REACTOS__
            if (ret == ERROR_SUCCESS)
#else
            RegDeleteTreeW(hkey, lpszDSN);
            if ((ret = RegCreateKeyW(hkey, lpszDSN, &hkeydriver)) == ERROR_SUCCESS)
#endif
            {
#ifdef __REACTOS__
                ret = RegDeleteTreeW(hkey, lpszDSN);
                if (ret == ERROR_FILE_NOT_FOUND) ret = ERROR_SUCCESS;
            }
            if (ret == ERROR_SUCCESS &&
                (ret = RegCreateKeyW(hkey, lpszDSN, &hkeydriver)) == ERROR_SUCCESS)
            {
                ret = RegSetValueExW(hkeydriver, L"driver", 0, REG_SZ, (BYTE*)filename,
                                     (wcslen(filename) + 1) * sizeof(WCHAR));
#else
                RegSetValueExW(sources, L"driver", 0, REG_SZ, (BYTE*)filename, (lstrlenW(filename)+1)*sizeof(WCHAR));
#endif
                RegCloseKey(hkeydriver);
            }
        }
        RegCloseKey(hkey);
    }

    if (ret != ERROR_SUCCESS)
        push_error(ODBC_ERROR_REQUEST_FAILED, L"Request Failed");

    return ret == ERROR_SUCCESS;
}

BOOL WINAPI SQLWriteDSNToIni(LPCSTR lpszDSN, LPCSTR lpszDriver)
{
    BOOL ret = FALSE;
    WCHAR *dsn, *driver;

    TRACE("%s %s\n", debugstr_a(lpszDSN), debugstr_a(lpszDriver));

#ifdef __REACTOS__
    clear_errors();
#endif
    dsn = SQLInstall_strdup(lpszDSN);
    driver = SQLInstall_strdup(lpszDriver);
    if (dsn && driver)
        ret = SQLWriteDSNToIniW(dsn, driver);
    else
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");

    free(dsn);
    free(driver);

    return ret;
}

BOOL WINAPI SQLWriteFileDSNW(LPCWSTR lpszFileName, LPCWSTR lpszAppName,
               LPCWSTR lpszKeyName, LPCWSTR lpszString)
{
    clear_errors();
    FIXME("%s %s %s %s\n", debugstr_w(lpszFileName), debugstr_w(lpszAppName),
                 debugstr_w(lpszKeyName), debugstr_w(lpszString));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLWriteFileDSN(LPCSTR lpszFileName, LPCSTR lpszAppName,
               LPCSTR lpszKeyName, LPCSTR lpszString)
{
    clear_errors();
    FIXME("%s %s %s %s\n", debugstr_a(lpszFileName), debugstr_a(lpszAppName),
                 debugstr_a(lpszKeyName), debugstr_a(lpszString));
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI SQLWritePrivateProfileStringW(LPCWSTR lpszSection, LPCWSTR lpszEntry,
               LPCWSTR lpszString, LPCWSTR lpszFilename)
{
    LONG ret;
#ifdef __REACTOS__
    HKEY hkey, root = HKEY_CURRENT_USER;
#else
    HKEY hkey;
#endif
    WCHAR *regpath;
#ifdef __REACTOS__
    size_t filename_len, section_len, value_len = 0;
    size_t prefix_len = wcslen(L"Software\\ODBC\\");
    BOOL write = lpszEntry && lpszString;
#endif

    clear_errors();
    TRACE("%s %s %s %s\n", debugstr_w(lpszSection), debugstr_w(lpszEntry),
                debugstr_w(lpszString), debugstr_w(lpszFilename));

#ifdef __REACTOS__
    if(!lpszFilename || !*lpszFilename || !lpszSection || !*lpszSection)
#else
    if(!lpszFilename || !*lpszFilename)
#endif
    {
        push_error(ODBC_ERROR_INVALID_STR, L"Invalid parameter string");
        return FALSE;
    }

#ifdef __REACTOS__
    filename_len = wcslen(lpszFilename);
    section_len = wcslen(lpszSection);
    if (filename_len > (size_t)-1 / sizeof(WCHAR) - prefix_len - 2 ||
        section_len > (size_t)-1 / sizeof(WCHAR) - prefix_len - 2 - filename_len)
        goto no_memory;
    if (write)
#else
    regpath = malloc ( (wcslen(L"Software\\ODBC\\") + wcslen(lpszFilename) + wcslen(L"\\")
                            + wcslen(lpszSection) + 1) * sizeof(WCHAR));
    if (!regpath)
#endif
    {
#ifdef __REACTOS__
        value_len = wcslen(lpszString);
        if (value_len >= MAXDWORD / sizeof(WCHAR)) goto no_memory;
#else
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
        return FALSE;
#endif
    }
#ifdef __REACTOS__
    regpath = malloc((prefix_len + filename_len + section_len + 2) * sizeof(WCHAR));
    if (!regpath) goto no_memory;
#endif
    wcscpy(regpath, L"Software\\ODBC\\");
    wcscat(regpath, lpszFilename);
    wcscat(regpath, L"\\");
    wcscat(regpath, lpszSection);

    /* odbcinit.ini is only for drivers, so default to local Machine */
    if (!wcsicmp(lpszFilename, L"ODBCINST.INI") || config_mode == ODBC_SYSTEM_DSN)
#ifdef __REACTOS__
    {
        root = HKEY_LOCAL_MACHINE;
        ret = write ? RegCreateKeyW(root, regpath, &hkey) : RegOpenKeyW(root, regpath, &hkey);
    }
#else
        ret = RegCreateKeyW(HKEY_LOCAL_MACHINE, regpath, &hkey);
#endif
    else if (config_mode == ODBC_USER_DSN)
#ifdef __REACTOS__
        ret = write ? RegCreateKeyW(root, regpath, &hkey) : RegOpenKeyW(root, regpath, &hkey);
#else
        ret = RegCreateKeyW(HKEY_CURRENT_USER, regpath, &hkey);
#endif
    else
    {
        /* Check existing keys first */
#ifdef __REACTOS__
        if ((ret = RegOpenKeyW(root, regpath, &hkey)) != ERROR_SUCCESS)
        {
            root = HKEY_LOCAL_MACHINE;
            ret = RegOpenKeyW(root, regpath, &hkey);
        }
#else
        if ((ret = RegOpenKeyW(HKEY_CURRENT_USER, regpath, &hkey)) != ERROR_SUCCESS)
            ret = RegOpenKeyW(HKEY_LOCAL_MACHINE, regpath, &hkey);
#endif

#ifdef __REACTOS__
        if (ret != ERROR_SUCCESS && write)
        {
            root = HKEY_CURRENT_USER;
            ret = RegCreateKeyW(root, regpath, &hkey);
        }
#else
        if (ret != ERROR_SUCCESS)
            ret = RegCreateKeyW(HKEY_CURRENT_USER, regpath, &hkey);
#endif
    }

#ifndef __REACTOS__
    free(regpath);

#endif
    if (ret == ERROR_SUCCESS)
    {
#ifdef __REACTOS__
        if (!lpszEntry)
            ret = RegDeleteTreeW(root, regpath);
        else if (!lpszString)
            ret = RegDeleteValueW(hkey, lpszEntry);
#else
        if(lpszString)
            ret = RegSetValueExW(hkey, lpszEntry, 0, REG_SZ, (BYTE*)lpszString, (lstrlenW(lpszString)+1)*sizeof(WCHAR));
#endif
        else
#ifdef __REACTOS__
            ret = RegSetValueExW(hkey, lpszEntry, 0, REG_SZ, (BYTE*)lpszString,
                                 (value_len + 1) * sizeof(WCHAR));
        RegCloseKey(hkey);
#else
            ret = RegSetValueExW(hkey, lpszEntry, 0, REG_SZ, (BYTE*)L"", sizeof(L""));
         RegCloseKey(hkey);
#endif
    }
#ifdef __REACTOS__
    free(regpath);
#endif

#ifdef __REACTOS__
    if (ret != ERROR_SUCCESS) push_error(ODBC_ERROR_REQUEST_FAILED, L"Request failed");
#endif
    return ret == ERROR_SUCCESS;
#ifdef __REACTOS__

no_memory:
    push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
    return FALSE;
#endif
}

BOOL WINAPI SQLWritePrivateProfileString(LPCSTR lpszSection, LPCSTR lpszEntry,
               LPCSTR lpszString, LPCSTR lpszFilename)
{
#ifdef __REACTOS__
    BOOL ret = FALSE;
#else
    BOOL ret;
#endif
    WCHAR *sect, *entry, *string, *file;
    clear_errors();
    TRACE("%s %s %s %s\n", lpszSection, lpszEntry, lpszString, lpszFilename);

    sect = strdupAtoW(lpszSection);
    entry = strdupAtoW(lpszEntry);
#ifdef __REACTOS__
    string = lpszEntry ? strdupAtoW(lpszString) : NULL;
#else
    string = strdupAtoW(lpszString);
#endif
    file = strdupAtoW(lpszFilename);

#ifdef __REACTOS__
    if ((lpszSection && !sect) || (lpszEntry && !entry) ||
        (lpszEntry && lpszString && !string) || (lpszFilename && !file))
        push_error(ODBC_ERROR_OUT_OF_MEM, L"Out of memory");
    else
        ret = SQLWritePrivateProfileStringW(sect, entry, string, file);
#else
    ret = SQLWritePrivateProfileStringW(sect, entry, string, file);
#endif

    free(sect);
    free(entry);
    free(string);
    free(file);

    return ret;
}
