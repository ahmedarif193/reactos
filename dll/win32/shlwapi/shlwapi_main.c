/*
 * SHLWAPI initialisation
 *
 *  Copyright 1998 Marcus Meissner
 *  Copyright 1998 Juergen Schmied (jsch)
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

#include "windef.h"
#include "winbase.h"
#define NO_SHLWAPI_REG
#define NO_SHLWAPI_STREAM
#include "shlwapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);
#ifndef __REACTOS__
WINE_DECLARE_DEBUG_CHANNEL(string);
#endif

#ifdef __REACTOS__
DECLSPEC_HIDDEN HINSTANCE shlwapi_hInstance = 0;
DECLSPEC_HIDDEN DWORD SHLWAPI_ThreadRef_index = TLS_OUT_OF_INDEXES;
extern CRITICAL_SECTION g_csZoneMgrLock;
extern CRITICAL_SECTION g_csBagCacheLock;
VOID FreeViewStatePropertyBagCache(VOID);
VOID SHLWAPI_DeleteCachedZonesManager(VOID);
VOID SHPolicyCache_DllProcessAttach(VOID);
VOID SHPolicyCache_DllProcessDetach(VOID);
#else
HINSTANCE shlwapi_hInstance = 0;

static int (CDECL *ntdll__vsnprintf)( char *str, size_t len, const char *format, va_list args );
static int (CDECL *ntdll__vsnwprintf)( WCHAR *str, size_t len, const WCHAR *format, va_list args );


/***********************************************************************
 *           wvnsprintfA   (SHLWAPI.@)
 *
 * Print formatted output to a string, up to a maximum number of chars.
 *
 * PARAMS
 * buffer [O] Destination for output string
 * maxlen [I] Maximum number of characters to write
 * spec   [I] Format string
 *
 * RETURNS
 *  Success: The number of characters written.
 *  Failure: -1.
 */
INT WINAPI wvnsprintfA( LPSTR buffer, INT maxlen, LPCSTR spec, va_list args )
{
    INT ret;

    TRACE_(string)( "%p %u %s\n", buffer, maxlen, debugstr_a(spec) );

    if (!maxlen) return -1;
    if (maxlen < 0)
    {
        buffer[0] = 0;
        return -1;
    }
    if ((ret = ntdll__vsnprintf( buffer, maxlen, spec, args )) == -1)
        buffer[maxlen - 1] = 0;
    return ret;
}

/***********************************************************************
 *           wvnsprintfW   (SHLWAPI.@)
 *
 * See wvnsprintfA.
 */
INT WINAPI wvnsprintfW( LPWSTR buffer, INT maxlen, LPCWSTR spec, va_list args )
{
    INT ret;

    TRACE_(string)( "%p %u %s\n", buffer, maxlen, debugstr_w(spec) );

    if (!maxlen) return -1;
    if (maxlen < 0)
    {
        buffer[0] = 0;
        return -1;
    }
    if ((ret = ntdll__vsnwprintf( buffer, maxlen, spec, args )) == -1)
        buffer[maxlen - 1] = 0;
    return ret;
}

/*************************************************************************
 *           wnsprintfA   (SHLWAPI.@)
 *
 * Print formatted output to a string, up to a maximum number of chars.
 *
 * PARAMS
 * lpOut      [O] Destination for output string
 * cchLimitIn [I] Maximum number of characters to write
 * lpFmt      [I] Format string
 *
 * RETURNS
 *  Success: The number of characters written.
 *  Failure: -1.
 */
int WINAPIV wnsprintfA(LPSTR lpOut, int cchLimitIn, LPCSTR lpFmt, ...)
{
    va_list valist;
    INT res;

    va_start( valist, lpFmt );
    res = wvnsprintfA( lpOut, cchLimitIn, lpFmt, valist );
    va_end( valist );
    return res;
}

/*************************************************************************
 *           wnsprintfW   (SHLWAPI.@)
 *
 * See wnsprintfA.
 */
int WINAPIV wnsprintfW(LPWSTR lpOut, int cchLimitIn, LPCWSTR lpFmt, ...)
{
    va_list valist;
    INT res;

    va_start( valist, lpFmt );
    res = wvnsprintfW( lpOut, cchLimitIn, lpFmt, valist );
    va_end( valist );
    return res;
}
#endif

/*************************************************************************
 * SHLWAPI {SHLWAPI}
 *
 * The Shell Light-Weight API dll provides a large number of utility functions
 * which are commonly required by Win32 programs. Originally distributed with
 * Internet Explorer as a free download, it became a core part of Windows when
 * Internet Explorer was 'integrated' into the O/S with the release of Win98.
 *
 * All functions exported by ordinal are undocumented by MS. The vast majority
 * of these are wrappers for Unicode functions that may not exist on early 16
 * bit platforms. The remainder perform various small tasks and presumably were
 * added to facilitate code reuse amongst the MS developers.
 */

/*************************************************************************
 * SHLWAPI DllMain
 *
 * NOTES
 *  calling oleinitialize here breaks some apps.
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID fImpLoad)
{
#ifdef __REACTOS__
	TRACE("%p 0x%x %p\n", hinstDLL, fdwReason, fImpLoad);
	switch (fdwReason)
	{
	  case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
	    shlwapi_hInstance = hinstDLL;
	    SHLWAPI_ThreadRef_index = TlsAlloc();
	    InitializeCriticalSection(&g_csZoneMgrLock);
	    InitializeCriticalSection(&g_csBagCacheLock);
	    SHPolicyCache_DllProcessAttach();
	    break;
	  case DLL_PROCESS_DETACH:
            if (fImpLoad) break;
	    FreeViewStatePropertyBagCache();
	    SHLWAPI_DeleteCachedZonesManager();
	    DeleteCriticalSection(&g_csBagCacheLock);
	    DeleteCriticalSection(&g_csZoneMgrLock);
	    SHPolicyCache_DllProcessDetach();
	    if (SHLWAPI_ThreadRef_index != TLS_OUT_OF_INDEXES) TlsFree(SHLWAPI_ThreadRef_index);
	    break;
	}
	return TRUE;
#else
    TRACE("%p 0x%lx %p\n", hinstDLL, fdwReason, fImpLoad);
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        HANDLE hntdll = GetModuleHandleW(L"ntdll.dll");

        ntdll__vsnprintf = (void *)GetProcAddress(hntdll, "_vsnprintf");
        ntdll__vsnwprintf = (void *)GetProcAddress(hntdll, "_vsnwprintf");
        DisableThreadLibraryCalls(hinstDLL);
        shlwapi_hInstance = hinstDLL;
        break;
    }
    }
    return TRUE;
#endif
}

/*************************************************************************
 *      WhichPlatform()        [SHLWAPI.276]
 */
UINT WINAPI WhichPlatform(void)
{
    static const char szIntegratedBrowser[] = "IntegratedBrowser";
    static DWORD state = PLATFORM_UNKNOWN;
    DWORD ret, data, size;
    HMODULE hshell32;
    HKEY hKey;

    if (state)
        return state;

    /* If shell32 exports DllGetVersion(), the browser is integrated */
    state = PLATFORM_BROWSERONLY;
    hshell32 = LoadLibraryA("shell32.dll");
    if (hshell32)
    {
        FARPROC pDllGetVersion;
        pDllGetVersion = GetProcAddress(hshell32, "DllGetVersion");
        state = pDllGetVersion ? PLATFORM_INTEGRATED : PLATFORM_BROWSERONLY;
        FreeLibrary(hshell32);
    }

    /* Set or delete the key accordingly */
    ret = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Internet Explorer", 0, KEY_ALL_ACCESS, &hKey);
    if (!ret)
    {
        size = sizeof(data);
        ret = RegQueryValueExA(hKey, szIntegratedBrowser, 0, 0, (BYTE *)&data, &size);
        if (!ret && state == PLATFORM_BROWSERONLY)
        {
            /* Value exists but browser is not integrated */
            RegDeleteValueA(hKey, szIntegratedBrowser);
        }
        else if (ret && state == PLATFORM_INTEGRATED)
        {
            /* Browser is integrated but value does not exist */
            data = TRUE;
            RegSetValueExA(hKey, szIntegratedBrowser, 0, REG_DWORD, (BYTE *)&data, sizeof(data));
        }
        RegCloseKey(hKey);
    }

    return state;
}

#ifndef __REACTOS__ /* See propbag.cpp */
/***********************************************************************
 *             SHGetViewStatePropertyBag [SHLWAPI.515]
 */
HRESULT WINAPI SHGetViewStatePropertyBag(PCIDLIST_ABSOLUTE pidl, PCWSTR bag_name, DWORD flags, REFIID riid, void **ppv)
{
    FIXME("%p, %s, %#lx, %s, %p stub.\n", pidl, debugstr_w(bag_name), flags, debugstr_guid(riid), ppv);

    return E_NOTIMPL;
}
#endif

/*************************************************************************
 *      SHIsLowMemoryMachine    [SHLWAPI.@]
 */
BOOL WINAPI SHIsLowMemoryMachine(DWORD type)
{
#ifdef __REACTOS__
    MEMORYSTATUS status;
    static int is_low = -1;
    TRACE("(0x%08x)\n", type);
    if (type == 0 && is_low == -1)
    {
        GlobalMemoryStatus(&status);
        is_low = (status.dwTotalPhys <= 0x1000000);
    }
    return is_low;
#else
    FIXME("%ld stub\n", type);

    return FALSE;
#endif
}
