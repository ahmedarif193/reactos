/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for the shcore registry/policy helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Expectations are taken from the Windows 8.1 SHCore.dll ordinal table and the
 * documented shlwapi.h prototypes:
 *   #120 SHRegGetCLSIDKey              6 args
 *   #121 SHRegSetValue                 7 args
 *   #122 SHRegGetValueFromHKCUHKLM     6 args, LSTATUS, consumed as a BOOL
 *   #123 SHRegGetBoolValueFromHKCUHKLM 3 args, BOOL
 *   #141 IUnknown_RemoveBackReferences 1 arg
 *   #190 SHWindowsPolicy               1 arg, BOOL
 */

#include <apitest.h>
#include <winreg.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string.h>
#include <wchar.h>
#include <versionhelpers.h>
#include <pseh/pseh2.h>

#define ORD_SHRegGetCLSIDKey              120
#define ORD_SHRegSetValue                 121
#define ORD_SHRegGetValueFromHKCUHKLM     122
#define ORD_SHRegGetBoolValueFromHKCUHKLM 123
#define ORD_IUnknown_RemoveBackReferences 141
#define ORD_SHWindowsPolicy               190

typedef LSTATUS (WINAPI *FN_SHRegGetValueFromHKCUHKLM)(PCWSTR, PCWSTR, SRRF, DWORD *, void *, DWORD *);
typedef BOOL (WINAPI *FN_SHRegGetBoolValueFromHKCUHKLM)(PCWSTR, PCWSTR, BOOL);
typedef HRESULT (WINAPI *FN_SHRegGetCLSIDKey)(REFGUID, PCWSTR, BOOL, BOOL, HKEY *);
typedef void (WINAPI *FN_IUnknown_RemoveBackReferences)(IUnknown *);
typedef BOOL (WINAPI *FN_SHWindowsPolicy)(int);

static HMODULE hShcore;
static FN_SHRegGetValueFromHKCUHKLM pSHRegGetValueFromHKCUHKLM;
static FN_SHRegGetBoolValueFromHKCUHKLM pSHRegGetBoolValueFromHKCUHKLM;
static FN_SHRegGetCLSIDKey pSHRegGetCLSIDKey;
static FN_IUnknown_RemoveBackReferences pIUnknown_RemoveBackReferences;
static FN_SHWindowsPolicy pSHWindowsPolicy;

static FARPROC
Resolve(_In_opt_ PCSTR pszName, _In_ WORD wOrdinal, _In_ PCSTR pszWhat)
{
    FARPROC fp = NULL;

    if (pszName)
        fp = GetProcAddress(hShcore, pszName);
    if (!fp)
        fp = GetProcAddress(hShcore, (LPCSTR)(ULONG_PTR)wOrdinal);

    ok(fp != NULL, "shcore!%s (#%u) is missing\n", pszWhat, wOrdinal);
    return fp;
}

static const WCHAR szTestKey[] = L"Software\\ReactOS\\shcore_apitest";
static const WCHAR szValue[] = L"TortureValue";

static BOOL
WriteTestValue(_In_ HKEY hRoot, _In_ DWORD dwData)
{
    HKEY hKey;
    LSTATUS err;

    err = RegCreateKeyExW(hRoot, szTestKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (err != ERROR_SUCCESS)
        return FALSE;

    err = RegSetValueExW(hKey, szValue, 0, REG_DWORD, (const BYTE *)&dwData, sizeof(dwData));
    RegCloseKey(hKey);
    return err == ERROR_SUCCESS;
}

static void
DeleteTestValue(_In_ HKEY hRoot)
{
    HKEY hKey;

    if (RegOpenKeyExW(hRoot, szTestKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, szValue);
        RegCloseKey(hKey);
    }
    RegDeleteKeyW(hRoot, szTestKey);
}

static void
Test_GetValueFromHKCUHKLM(void)
{
    DWORD dwType, dwData, cbData;
    LSTATUS err;

    if (!pSHRegGetValueFromHKCUHKLM)
        return;

    /* Absent everywhere */
    DeleteTestValue(HKEY_CURRENT_USER);
    cbData = sizeof(dwData);
    err = pSHRegGetValueFromHKCUHKLM(szTestKey, szValue, SRRF_RT_DWORD, &dwType, &dwData, &cbData);
    ok(err != ERROR_SUCCESS, "an absent value must not succeed, got %ld\n", err);

    /* HKCU wins */
    if (WriteTestValue(HKEY_CURRENT_USER, 0x11223344))
    {
        dwType = 0;
        dwData = 0;
        cbData = sizeof(dwData);
        err = pSHRegGetValueFromHKCUHKLM(szTestKey, szValue, SRRF_RT_DWORD,
                                         &dwType, &dwData, &cbData);
        ok(err == ERROR_SUCCESS, "HKCU lookup failed with %ld\n", err);
        ok(dwType == REG_DWORD, "expected REG_DWORD, got %lu\n", dwType);
        ok(dwData == 0x11223344, "expected 0x11223344, got 0x%08lx\n", dwData);
        ok(cbData == sizeof(dwData), "expected cbData %u, got %lu\n",
           (UINT)sizeof(dwData), cbData);

        /* Type filtering must be honoured */
        cbData = sizeof(dwData);
        err = pSHRegGetValueFromHKCUHKLM(szTestKey, szValue, SRRF_RT_REG_SZ,
                                         &dwType, &dwData, &cbData);
        ok(err != ERROR_SUCCESS, "SRRF_RT_REG_SZ must reject a REG_DWORD, got %ld\n", err);

        /* Size query */
        cbData = 0;
        err = pSHRegGetValueFromHKCUHKLM(szTestKey, szValue, SRRF_RT_DWORD,
                                         NULL, NULL, &cbData);
        ok(err == ERROR_SUCCESS, "size query failed with %ld\n", err);
        ok(cbData == sizeof(DWORD), "size query returned %lu\n", cbData);

        /* Too-small buffer */
        cbData = 1;
        err = pSHRegGetValueFromHKCUHKLM(szTestKey, szValue, SRRF_RT_DWORD,
                                         &dwType, &dwData, &cbData);
        ok(err == ERROR_MORE_DATA, "expected ERROR_MORE_DATA, got %ld\n", err);

        DeleteTestValue(HKEY_CURRENT_USER);
    }
    else
    {
        skip("could not create the HKCU test value\n");
    }

    /* A missing key must not fault */
    cbData = sizeof(dwData);
    err = pSHRegGetValueFromHKCUHKLM(L"Software\\ReactOS\\NoSuchKeyAtAll", szValue,
                                     SRRF_RT_DWORD, &dwType, &dwData, &cbData);
    ok(err != ERROR_SUCCESS, "a missing key must fail, got %ld\n", err);
}

static void
Test_GetBoolValueFromHKCUHKLM(void)
{
    BOOL ret;

    if (!pSHRegGetBoolValueFromHKCUHKLM)
        return;

    DeleteTestValue(HKEY_CURRENT_USER);

    ret = pSHRegGetBoolValueFromHKCUHKLM(szTestKey, szValue, TRUE);
    ok(ret == TRUE, "the default must be returned when absent, got %d\n", ret);
    ret = pSHRegGetBoolValueFromHKCUHKLM(szTestKey, szValue, FALSE);
    ok(ret == FALSE, "the default must be returned when absent, got %d\n", ret);

    if (WriteTestValue(HKEY_CURRENT_USER, 0))
    {
        ok(pSHRegGetBoolValueFromHKCUHKLM(szTestKey, szValue, TRUE) == FALSE,
           "a zero DWORD must read as FALSE\n");
    }
    if (WriteTestValue(HKEY_CURRENT_USER, 1))
    {
        ok(pSHRegGetBoolValueFromHKCUHKLM(szTestKey, szValue, FALSE) == TRUE,
           "a non-zero DWORD must read as TRUE\n");
    }
    if (WriteTestValue(HKEY_CURRENT_USER, 0xDEADBEEF))
    {
        ok(pSHRegGetBoolValueFromHKCUHKLM(szTestKey, szValue, FALSE) == TRUE,
           "any non-zero DWORD must read as TRUE\n");
    }
    DeleteTestValue(HKEY_CURRENT_USER);

    /* The result must be a strict 0/1 BOOL: explorer branches on it with test+je */
    ret = pSHRegGetBoolValueFromHKCUHKLM(szTestKey, szValue, TRUE);
    ok(ret == 0 || ret == 1, "the return value must be a canonical BOOL, got %d\n", ret);
}

static void
Test_ReturnTypeContracts(void)
{
    HKEY hKey;
    HRESULT hr;

    /*
     * Windows 8.1 explorer branches on SHWindowsPolicy with test eax,eax + jne,
     * so the value has to be a real BOOL. An HRESULT-shaped failure such as
     * E_NOTIMPL would read as TRUE and send the caller down the wrong path.
     */
    if (pSHWindowsPolicy)
    {
        BOOL ret = pSHWindowsPolicy(0);

        ok(ret == 0 || ret == 1,
           "SHWindowsPolicy must return a canonical BOOL, got 0x%08x\n", ret);
    }

    if (pSHRegGetCLSIDKey)
    {
        hKey = (HKEY)(ULONG_PTR)0xdeadbeef;
        hr = pSHRegGetCLSIDKey(&GUID_NULL, NULL, FALSE, FALSE, &hKey);
        ok(FAILED(hr) || hKey != (HKEY)(ULONG_PTR)0xdeadbeef,
           "SHRegGetCLSIDKey must either fail or write the output, hr 0x%08lx\n", hr);
        if (SUCCEEDED(hr) && hKey && hKey != (HKEY)(ULONG_PTR)0xdeadbeef)
            RegCloseKey(hKey);
    }

    if (pIUnknown_RemoveBackReferences)
    {
        _SEH2_TRY
        {
            pIUnknown_RemoveBackReferences(NULL);
            ok(TRUE, "IUnknown_RemoveBackReferences(NULL) survived\n");
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ok(FALSE, "IUnknown_RemoveBackReferences(NULL) raised 0x%08lx\n",
               _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
}

static void
Test_NullTorture(void)
{
    DWORD dwType, dwData, cbData;

    if (!pSHRegGetValueFromHKCUHKLM)
        return;

    cbData = sizeof(dwData);
    _SEH2_TRY
    {
        pSHRegGetValueFromHKCUHKLM(NULL, NULL, SRRF_RT_DWORD, &dwType, &dwData, &cbData);
        ok(TRUE, "NULL key/value survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("NULL key/value raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    _SEH2_TRY
    {
        pSHRegGetValueFromHKCUHKLM(szTestKey, szValue, SRRF_RT_DWORD, NULL, NULL, NULL);
        ok(TRUE, "all-NULL outputs survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("all-NULL outputs raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (pSHRegGetBoolValueFromHKCUHKLM)
    {
        _SEH2_TRY
        {
            pSHRegGetBoolValueFromHKCUHKLM(NULL, NULL, TRUE);
            ok(TRUE, "SHRegGetBoolValueFromHKCUHKLM(NULL) survived\n");
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            trace("SHRegGetBoolValueFromHKCUHKLM(NULL) raised 0x%08lx\n",
                  _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
}

static void
Test_Churn(void)
{
    DWORD dwType, dwData, cbData;
    UINT i;
    LONG Bad = 0;

    if (!pSHRegGetValueFromHKCUHKLM || !WriteTestValue(HKEY_CURRENT_USER, 0x5A5A5A5A))
    {
        skip("cannot set up the churn test\n");
        return;
    }

    for (i = 0; i < 5000; i++)
    {
        dwData = 0;
        cbData = sizeof(dwData);
        if (pSHRegGetValueFromHKCUHKLM(szTestKey, szValue, SRRF_RT_DWORD,
                                       &dwType, &dwData, &cbData) != ERROR_SUCCESS ||
            dwData != 0x5A5A5A5A)
        {
            Bad++;
            break;
        }
    }
    ok(Bad == 0, "%ld of 5000 lookups misbehaved\n", Bad);

    DeleteTestValue(HKEY_CURRENT_USER);
}

START_TEST(RegistryHelpers)
{
    hShcore = LoadLibraryW(L"shcore.dll");
    ok(hShcore != NULL, "shcore.dll failed to load, error %lu\n", GetLastError());
    if (!hShcore)
        return;

    /*
     * Only #122 carries a name in the Windows export table, so resolve by name
     * first and fall back to the ordinal. The ordinal meanings drift between
     * Windows releases, which is why the behavioural checks below only run
     * where the mapping is known to hold.
     */
    pSHRegGetValueFromHKCUHKLM = (FN_SHRegGetValueFromHKCUHKLM)
        Resolve("SHRegGetValueFromHKCUHKLM", ORD_SHRegGetValueFromHKCUHKLM,
                "SHRegGetValueFromHKCUHKLM");
    pSHRegGetBoolValueFromHKCUHKLM = (FN_SHRegGetBoolValueFromHKCUHKLM)
        Resolve("SHRegGetBoolValueFromHKCUHKLM", ORD_SHRegGetBoolValueFromHKCUHKLM,
                "SHRegGetBoolValueFromHKCUHKLM");
    pSHRegGetCLSIDKey = (FN_SHRegGetCLSIDKey)
        Resolve(NULL, ORD_SHRegGetCLSIDKey, "SHRegGetCLSIDKey");
    pIUnknown_RemoveBackReferences = (FN_IUnknown_RemoveBackReferences)
        Resolve(NULL, ORD_IUnknown_RemoveBackReferences, "IUnknown_RemoveBackReferences");
    pSHWindowsPolicy = (FN_SHWindowsPolicy)
        Resolve(NULL, ORD_SHWindowsPolicy, "SHWindowsPolicy");

    if (!IsReactOS())
    {
        /*
         * On Windows these ordinals are unnamed and their meaning is release
         * specific, so calling them blind can corrupt the caller stack. Proving
         * they resolve is all this test can safely claim there.
         */
        trace("running on Windows: ordinal semantics are not asserted\n");
        FreeLibrary(hShcore);
        return;
    }

    Test_GetValueFromHKCUHKLM();
    Test_GetBoolValueFromHKCUHKLM();
    Test_ReturnTypeContracts();
    Test_NullTorture();
    Test_Churn();

    FreeLibrary(hShcore);
}
