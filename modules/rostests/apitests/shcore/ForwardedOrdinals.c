/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for the shlwapi helpers re-exported by shcore ordinals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string.h>
#include <wchar.h>
#include <pseh/pseh2.h>

#define ORD_SHLoadRegUIStringW        126
#define ORD_SHGlobalCounterGetValue   130
#define ORD_IUnknown_GetClassID       142
#define ORD_StrRetToStrW              143
#define ORD_StrRetToBufW              145
#define ORD_SHQueueUserWorkItem       162
#define ORD_PathIsNetworkPathW        170
#define ORD_SHLockSharedEx            183
#define ORD_SHLockShared              184
#define ORD_SHUnlockShared            186
#define ORD_SHFreeShared              187
#define ORD_SHWindowsPolicyGetValue   191
#define ORD_SHGetObjectCompatFlags    193
#define ORD_GUIDFromStringW           200

#define SHLWAPI_ORD_SHAllocShared     7
#define SHLWAPI_ORD_SHLockShared      8
#define SHLWAPI_ORD_SHUnlockShared    9
#define SHLWAPI_ORD_SHFreeShared      10

typedef HRESULT (WINAPI *FN_StrRetToStrW)(STRRET *, LPCITEMIDLIST, LPWSTR *);
typedef HRESULT (WINAPI *FN_StrRetToBufW)(STRRET *, LPCITEMIDLIST, LPWSTR, UINT);
typedef BOOL (WINAPI *FN_PathIsNetworkPathW)(LPCWSTR);
typedef BOOL (WINAPI *FN_GUIDFromStringW)(LPCWSTR, GUID *);
typedef HANDLE (WINAPI *FN_SHAllocShared)(LPCVOID, DWORD, DWORD);
typedef LPVOID (WINAPI *FN_SHLockShared)(HANDLE, DWORD);
typedef LPVOID (WINAPI *FN_SHLockSharedEx)(HANDLE, DWORD, BOOL);
typedef BOOL (WINAPI *FN_SHUnlockShared)(LPVOID);
typedef BOOL (WINAPI *FN_SHFreeShared)(HANDLE, DWORD);
typedef HRESULT (WINAPI *FN_IUnknown_GetClassID)(IUnknown *, CLSID *);

static HMODULE hShcore;
static HMODULE hShlwapi;

static FARPROC
GetOrdinal(_In_ HMODULE hMod, _In_ WORD wOrdinal, _In_ const char *pszName)
{
    FARPROC fp = GetProcAddress(hMod, (LPCSTR)(ULONG_PTR)wOrdinal);
    ok(fp != NULL, "#%u (%s) is missing\n", wOrdinal, pszName);
    return fp;
}

static void
Test_AllResolve(void)
{
    static const struct { WORD wOrd; const char *pszName; } Ordinals[] =
    {
        { ORD_SHLoadRegUIStringW,      "SHLoadRegUIStringW" },
        { ORD_SHGlobalCounterGetValue, "SHGlobalCounterGetValue" },
        { ORD_IUnknown_GetClassID,     "IUnknown_GetClassID" },
        { ORD_StrRetToStrW,            "StrRetToStrW" },
        { ORD_StrRetToBufW,            "StrRetToBufW" },
        { ORD_SHQueueUserWorkItem,     "SHQueueUserWorkItem" },
        { ORD_PathIsNetworkPathW,      "PathIsNetworkPathW" },
        { ORD_SHLockSharedEx,          "SHLockSharedEx" },
        { ORD_SHLockShared,            "SHLockShared" },
        { ORD_SHUnlockShared,          "SHUnlockShared" },
        { ORD_SHFreeShared,            "SHFreeShared" },
        { ORD_SHWindowsPolicyGetValue, "SHWindowsPolicyGetValue" },
        { ORD_SHGetObjectCompatFlags,  "SHGetObjectCompatFlags" },
        { ORD_GUIDFromStringW,         "GUIDFromStringW" },
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Ordinals); i++)
        GetOrdinal(hShcore, Ordinals[i].wOrd, Ordinals[i].pszName);
}

static void
Test_StrRet(void)
{
    FN_StrRetToStrW pStrRetToStrW =
        (FN_StrRetToStrW)GetOrdinal(hShcore, ORD_StrRetToStrW, "StrRetToStrW");
    FN_StrRetToBufW pStrRetToBufW =
        (FN_StrRetToBufW)GetOrdinal(hShcore, ORD_StrRetToBufW, "StrRetToBufW");
    static const WCHAR szSource[] = L"shcore-forward";
    WCHAR szBuffer[32];
    STRRET sr;
    LPWSTR pszOut;
    HRESULT hr;
    UINT i;

    if (!pStrRetToStrW || !pStrRetToBufW)
        return;

    for (i = 0; i < 500; i++)
    {
        sr.uType = STRRET_WSTR;
        sr.pOleStr = CoTaskMemAlloc(sizeof(szSource));
        if (!sr.pOleStr)
        {
            ok(FALSE, "CoTaskMemAlloc failed at %u\n", i);
            return;
        }
        memcpy(sr.pOleStr, szSource, sizeof(szSource));

        pszOut = NULL;
        hr = pStrRetToStrW(&sr, NULL, &pszOut);
        if (hr != S_OK || !pszOut || wcscmp(pszOut, szSource) != 0)
        {
            ok(FALSE, "iteration %u: hr 0x%08lx out %p\n", i, hr, pszOut);
            CoTaskMemFree(pszOut);
            return;
        }
        CoTaskMemFree(pszOut);
    }
    ok(TRUE, "500 STRRET_WSTR conversions round-tripped\n");

    sr.uType = STRRET_CSTR;
    strcpy(sr.cStr, "ansi-forward");
    szBuffer[0] = L'\0';
    hr = pStrRetToBufW(&sr, NULL, szBuffer, ARRAYSIZE(szBuffer));
    ok(hr == S_OK, "STRRET_CSTR returned 0x%08lx\n", hr);
    ok(wcscmp(szBuffer, L"ansi-forward") == 0, "got %ls\n", szBuffer);

    sr.uType = STRRET_CSTR;
    strcpy(sr.cStr, "truncate-me-please");
    szBuffer[0] = L'\0';
    hr = pStrRetToBufW(&sr, NULL, szBuffer, 5);
    trace("truncating StrRetToBufW -> 0x%08lx %ls\n", hr, szBuffer);
    ok(wcslen(szBuffer) < 5, "the output must fit the buffer, got %ls\n", szBuffer);

    sr.uType = STRRET_CSTR;
    strcpy(sr.cStr, "zero");
    hr = pStrRetToBufW(&sr, NULL, szBuffer, 0);
    trace("StrRetToBufW with cch 0 -> 0x%08lx\n", hr);

    sr.uType = 0xBEEF;
    hr = pStrRetToBufW(&sr, NULL, szBuffer, ARRAYSIZE(szBuffer));
    ok(FAILED(hr), "an unknown STRRET type must fail, got 0x%08lx\n", hr);

    hr = E_NOTIMPL;
    _SEH2_TRY
    {
        hr = pStrRetToBufW(NULL, NULL, szBuffer, ARRAYSIZE(szBuffer));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("NULL STRRET raised 0x%08lx\n", _SEH2_GetExceptionCode());
        hr = E_FAIL;
    }
    _SEH2_END;
    ok(FAILED(hr), "a NULL STRRET must not succeed, got 0x%08lx\n", hr);
}

static void
Test_PathIsNetworkPathW(void)
{
    static const struct { const WCHAR *pszPath; BOOL bExpected; } Cases[] =
    {
        { L"\\\\server\\share",   TRUE },
        { L"\\\\?\\UNC\\a\\b",    TRUE },
        { L"C:\\Windows",         FALSE },
        { L"",                    FALSE },
        { L"\\",                  FALSE },
        { L"\\\\",                TRUE },
        { L"relative\\path",      FALSE },
        { L"\\\\.\\PhysicalDrive0", TRUE },
    };
    FN_PathIsNetworkPathW pFn =
        (FN_PathIsNetworkPathW)GetOrdinal(hShcore, ORD_PathIsNetworkPathW, "PathIsNetworkPathW");
    UINT i;

    if (!pFn)
        return;

    for (i = 0; i < ARRAYSIZE(Cases); i++)
    {
        BOOL ret = pFn(Cases[i].pszPath);

        if (ret != Cases[i].bExpected)
            trace("PathIsNetworkPathW(%ls) = %d, expected %d\n",
                  Cases[i].pszPath, ret, Cases[i].bExpected);
    }

    ok(pFn(L"\\\\server\\share") == TRUE, "a UNC path is a network path\n");
    ok(pFn(L"C:\\Windows") == FALSE, "the system drive is not a network path\n");
    ok(pFn(L"") == FALSE, "an empty path is not a network path\n");

    _SEH2_TRY
    {
        pFn(NULL);
        ok(TRUE, "PathIsNetworkPathW(NULL) survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("PathIsNetworkPathW(NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
}

static void
Test_GUIDFromStringW(void)
{
    static const GUID Expected =
        { 0x00021400, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    static const WCHAR *Bad[] =
    {
        L"", L"not-a-guid", L"{00021400-0000-0000-C000-00000000004}",
        L"00021400-0000-0000-C000-000000000046",
        L"{00021400-0000-0000-C000-000000000046",
        L"{ZZ021400-0000-0000-C000-000000000046}",
        L"{00021400-0000-0000-C000-0000000000466}",
    };
    FN_GUIDFromStringW pFn =
        (FN_GUIDFromStringW)GetOrdinal(hShcore, ORD_GUIDFromStringW, "GUIDFromStringW");
    GUID guid;
    UINT i;

    if (!pFn)
        return;

    memset(&guid, 0xcc, sizeof(guid));
    ok(pFn(L"{00021400-0000-0000-C000-000000000046}", &guid) == TRUE,
       "GUIDFromStringW failed on a well-formed GUID\n");
    ok(memcmp(&guid, &Expected, sizeof(guid)) == 0, "GUIDFromStringW produced the wrong GUID\n");

    for (i = 0; i < ARRAYSIZE(Bad); i++)
    {
        BOOL ret;

        memset(&guid, 0xcc, sizeof(guid));
        ret = pFn(Bad[i], &guid);
        if (ret)
            trace("GUIDFromStringW accepted %ls\n", Bad[i]);
        ok(!ret, "GUIDFromStringW must reject %ls\n", Bad[i]);
    }

    _SEH2_TRY
    {
        pFn(NULL, &guid);
        ok(TRUE, "GUIDFromStringW(NULL, ...) survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("GUIDFromStringW(NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
}

static void
Test_SharedMemory(void)
{
    FN_SHAllocShared pAlloc =
        (FN_SHAllocShared)GetProcAddress(hShlwapi, (LPCSTR)(ULONG_PTR)SHLWAPI_ORD_SHAllocShared);
    FN_SHLockShared pLock =
        (FN_SHLockShared)GetOrdinal(hShcore, ORD_SHLockShared, "SHLockShared");
    FN_SHLockSharedEx pLockEx =
        (FN_SHLockSharedEx)GetOrdinal(hShcore, ORD_SHLockSharedEx, "SHLockSharedEx");
    FN_SHUnlockShared pUnlock =
        (FN_SHUnlockShared)GetOrdinal(hShcore, ORD_SHUnlockShared, "SHUnlockShared");
    FN_SHFreeShared pFree =
        (FN_SHFreeShared)GetOrdinal(hShcore, ORD_SHFreeShared, "SHFreeShared");
    static const DWORD dwPayload = 0x5ACAB1E5;
    DWORD dwPid = GetCurrentProcessId();
    HANDLE hShared;
    LPVOID pView;
    UINT i;

    ok(pAlloc != NULL, "shlwapi.#7 SHAllocShared is missing\n");
    if (!pAlloc || !pLock || !pUnlock || !pFree)
        return;

    hShared = pAlloc(&dwPayload, sizeof(dwPayload), dwPid);
    ok(hShared != NULL, "SHAllocShared failed, error %lu\n", GetLastError());
    if (!hShared)
        return;

    pView = pLock(hShared, dwPid);
    ok(pView != NULL, "shcore.#184 SHLockShared failed, error %lu\n", GetLastError());
    if (pView)
    {
        ok(*(DWORD *)pView == dwPayload, "payload is 0x%08lx, expected 0x%08lx\n",
           *(DWORD *)pView, dwPayload);
        ok(pUnlock(pView), "shcore.#186 SHUnlockShared failed, error %lu\n", GetLastError());
    }

    if (pLockEx)
    {
        pView = pLockEx(hShared, dwPid, FALSE);
        ok(pView != NULL, "shcore.#183 SHLockSharedEx failed, error %lu\n", GetLastError());
        if (pView)
            pUnlock(pView);
    }

    for (i = 0; i < 2000; i++)
    {
        pView = pLock(hShared, dwPid);
        if (!pView || *(DWORD *)pView != dwPayload)
        {
            ok(FALSE, "lock churn failed at %u\n", i);
            break;
        }
        if (!pUnlock(pView))
        {
            ok(FALSE, "unlock churn failed at %u\n", i);
            break;
        }
    }
    ok(i == 2000, "2000 lock/unlock cycles completed\n");

    ok(pFree(hShared, dwPid), "shcore.#187 SHFreeShared failed, error %lu\n", GetLastError());

    _SEH2_TRY
    {
        ok(pLock(NULL, dwPid) == NULL, "SHLockShared(NULL) must fail\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("SHLockShared(NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    _SEH2_TRY
    {
        ok(pLock((HANDLE)(ULONG_PTR)0xdeadbeef, dwPid) == NULL,
           "SHLockShared on a bogus handle must fail\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("SHLockShared(bogus) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    _SEH2_TRY
    {
        ok(!pUnlock(NULL), "SHUnlockShared(NULL) must fail\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("SHUnlockShared(NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    for (i = 0; i < 1000; i++)
    {
        HANDLE h = pAlloc(&dwPayload, sizeof(dwPayload), dwPid);

        if (!h)
        {
            ok(FALSE, "SHAllocShared exhausted at %u, error %lu\n", i, GetLastError());
            break;
        }
        if (!pFree(h, dwPid))
        {
            ok(FALSE, "SHFreeShared failed at %u, error %lu\n", i, GetLastError());
            break;
        }
    }
    ok(i == 1000, "1000 alloc/free cycles completed without leaking handles\n");
}

static void
Test_IUnknown_GetClassID(void)
{
    FN_IUnknown_GetClassID pFn =
        (FN_IUnknown_GetClassID)GetOrdinal(hShcore, ORD_IUnknown_GetClassID, "IUnknown_GetClassID");
    CLSID clsid;
    HRESULT hr;

    if (!pFn)
        return;

    memset(&clsid, 0xcc, sizeof(clsid));
    hr = pFn(NULL, &clsid);
    trace("IUnknown_GetClassID(NULL) -> 0x%08lx\n", hr);
    ok(FAILED(hr) || IsEqualCLSID(&clsid, &CLSID_NULL),
       "a NULL object must not yield a real CLSID\n");
}

static DWORD WINAPI
HammerThread(LPVOID pv)
{
    LONG *pFailures = (LONG *)pv;
    FN_PathIsNetworkPathW pFn =
        (FN_PathIsNetworkPathW)GetProcAddress(hShcore, (LPCSTR)(ULONG_PTR)ORD_PathIsNetworkPathW);
    UINT i;

    if (!pFn)
    {
        InterlockedIncrement(pFailures);
        return 0;
    }

    for (i = 0; i < 20000; i++)
    {
        if (!pFn(L"\\\\server\\share") || pFn(L"C:\\Windows"))
        {
            InterlockedIncrement(pFailures);
            break;
        }
    }
    return 0;
}

static void
Test_Threaded(void)
{
    HANDLE hThreads[4];
    LONG Failures = 0;
    UINT i;

    for (i = 0; i < ARRAYSIZE(hThreads); i++)
        hThreads[i] = CreateThread(NULL, 0, HammerThread, &Failures, 0, NULL);

    WaitForMultipleObjects(ARRAYSIZE(hThreads), hThreads, TRUE, 60000);
    for (i = 0; i < ARRAYSIZE(hThreads); i++)
    {
        if (hThreads[i])
            CloseHandle(hThreads[i]);
    }
    ok(Failures == 0, "%ld threaded iterations misbehaved\n", Failures);
}

static void
Test_LoadUnloadChurn(void)
{
    UINT i;

    for (i = 0; i < 200; i++)
    {
        HMODULE hMod = LoadLibraryW(L"shcore.dll");

        if (!hMod)
        {
            ok(FALSE, "shcore.dll failed to load at %u, error %lu\n", i, GetLastError());
            return;
        }
        if (!GetProcAddress(hMod, (LPCSTR)(ULONG_PTR)ORD_SHLockShared))
        {
            ok(FALSE, "ordinal 184 vanished at %u\n", i);
            FreeLibrary(hMod);
            return;
        }
        FreeLibrary(hMod);
    }
    ok(TRUE, "200 load/unload cycles kept the forwarders resolvable\n");
}

START_TEST(ForwardedOrdinals)
{
    hShcore = LoadLibraryW(L"shcore.dll");
    ok(hShcore != NULL, "shcore.dll failed to load, error %lu\n", GetLastError());
    if (!hShcore)
        return;

    hShlwapi = LoadLibraryW(L"shlwapi.dll");
    ok(hShlwapi != NULL, "shlwapi.dll failed to load, error %lu\n", GetLastError());
    if (!hShlwapi)
    {
        FreeLibrary(hShcore);
        return;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    Test_AllResolve();
    Test_StrRet();
    Test_PathIsNetworkPathW();
    Test_GUIDFromStringW();
    Test_SharedMemory();
    Test_IUnknown_GetClassID();
    Test_Threaded();
    Test_LoadUnloadChurn();

    CoUninitialize();
    FreeLibrary(hShlwapi);
    FreeLibrary(hShcore);
}
