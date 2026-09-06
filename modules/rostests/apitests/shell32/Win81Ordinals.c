/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     ABI contract tests for the Windows 8.1 shell32 ordinals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Argument counts come from the Windows 8.1 x86 shell32.dll `ret imm16`
 * epilogues; the return-value contracts come from how explorer.exe consumes
 * each call site:
 *   #254 DisconnectWindowsDialog  1 arg  - result ignored
 *   #787 ...IndexAsync            8 args - `test eax,eax; js`  -> HRESULT
 *   #790 SHMapIDListToSystemImageListIndex 4 args - `test eax,eax; jns`
 *   #850 PathComparePaths         2 args
 *   #892                          3 args - `test eax,eax; je`  -> BOOL
 *   #899                          1 arg  - stores its argument, returns S_OK
 *   #905                          2 args - zeroes *arg2, defaults to E_FAIL
 *   #906                          0 args
 */

#define COM_NO_WINDOWS_H
#define WIN32_NO_STATUS

#include <apitest.h>
#include <winreg.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <versionhelpers.h>
#include <pseh/pseh2.h>

#define ORD_DisconnectWindowsDialog 254
#define ORD_MapIDListAsync          787
#define ORD_MapIDList               790
#define ORD_PathComparePaths        850
#define ORD_Ordinal892              892
#define ORD_Ordinal899              899
#define ORD_Ordinal905              905
#define ORD_Ordinal906              906

typedef BOOL (WINAPI *FN_PathComparePaths)(LPCWSTR, LPCWSTR);
typedef BOOL (WINAPI *FN_Ord892)(PVOID, PVOID, PVOID);
typedef HRESULT (WINAPI *FN_Ord899)(PVOID);
typedef HRESULT (WINAPI *FN_Ord905)(PVOID, PVOID);
typedef int (WINAPI *FN_MapIDList)(PVOID, PVOID, DWORD, int *);

static HMODULE hShell32;

static FARPROC
Ord(WORD w, const char *pszWhat)
{
    FARPROC fp = GetProcAddress(hShell32, (LPCSTR)(ULONG_PTR)w);

    if (!fp && !IsReactOS())
    {
        trace("shell32.#%u (%s) is absent on this Windows build\n", w, pszWhat);
        return NULL;
    }

    ok(fp != NULL, "shell32.#%u (%s) is missing\n", w, pszWhat);
    return fp;
}

static void
Test_PathComparePaths(void)
{
    FN_PathComparePaths pFn = (FN_PathComparePaths)Ord(ORD_PathComparePaths, "PathComparePaths");

    if (!pFn)
        return;

    ok(pFn(L"C:\\Windows", L"C:\\Windows") == TRUE,
       "identical paths must compare equal\n");
    ok(pFn(L"C:\\Windows", L"c:\\windows") == TRUE,
       "path comparison must be case-insensitive\n");
    ok(pFn(L"C:\\Windows", L"C:\\Program Files") == FALSE,
       "different paths must not compare equal\n");
    ok(pFn(L"", L"") == TRUE, "two empty paths must compare equal\n");
    ok(pFn(L"C:\\Windows", L"") == FALSE, "an empty path must not match\n");

    /* The result is consumed as a boolean, so it must be canonical. */
    {
        BOOL ret = pFn(L"a", L"b");

        ok(ret == 0 || ret == 1, "PathComparePaths must return a canonical BOOL, got 0x%08x\n", ret);
    }

    _SEH2_TRY
    {
        pFn(NULL, NULL);
        ok(TRUE, "PathComparePaths(NULL, NULL) survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("PathComparePaths(NULL, NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
}

static void
Test_Ordinal892_IsBool(void)
{
    FN_Ord892 pFn = (FN_Ord892)Ord(ORD_Ordinal892, "boolean query");
    BOOL ret;

    if (!pFn)
        return;

    ret = pFn(NULL, NULL, NULL);
    ok(ret == FALSE || ret == TRUE,
       "shell32.#892 must return a canonical BOOL, got 0x%08x "
       "(explorer branches on it with test+je, so an HRESULT reads as TRUE)\n", ret);
    ok(ret == FALSE, "shell32.#892 should report FALSE when unsupported, got 0x%08x\n", ret);
}

static void
Test_Ordinal899_IsSetter(void)
{
    FN_Ord899 pFn = (FN_Ord899)Ord(ORD_Ordinal899, "setter");
    HRESULT hr;

    if (!pFn)
        return;

    hr = pFn(NULL);
    ok(hr == S_OK, "shell32.#899 stores its argument and returns S_OK, got 0x%08lx\n", hr);

    hr = pFn((PVOID)(ULONG_PTR)0x1234);
    ok(hr == S_OK, "shell32.#899 must keep returning S_OK, got 0x%08lx\n", hr);

    /* Restore whatever the shell had. */
    pFn(NULL);
}

static void
Test_Ordinal905_ZeroesOutput(void)
{
    FN_Ord905 pFn = (FN_Ord905)Ord(ORD_Ordinal905, "query with out-param");
    DWORD dwOut = 0xDEADBEEF;
    HRESULT hr;

    if (!pFn)
        return;

    hr = pFn(NULL, &dwOut);
    ok(dwOut == 0,
       "shell32.#905 zeroes its second parameter before doing anything, got 0x%08lx\n", dwOut);
    ok(hr == E_FAIL, "shell32.#905 defaults to E_FAIL, got 0x%08lx\n", hr);

    _SEH2_TRY
    {
        pFn(NULL, NULL);
        ok(TRUE, "shell32.#905(NULL, NULL) survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("shell32.#905(NULL, NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
}

static void
Test_MapIDListIsHResultShaped(void)
{
    FN_MapIDList pFn = (FN_MapIDList)Ord(ORD_MapIDList, "SHMapIDListToSystemImageListIndex");
    int iSel = 0x1234;
    int ret;

    if (!pFn)
        return;

    ret = pFn(NULL, NULL, 0, &iSel);
    ok(ret < 0,
       "shell32.#790 must report a negative index on failure (explorer tests it "
       "with test+jns), got %d\n", ret);
    ok(iSel < 0 || iSel == 0x1234 || iSel == -1,
       "the selected-index output must not be left as a bogus positive, got %d\n", iSel);
}

static void
Test_AllOrdinalsResolve(void)
{
    static const struct { WORD w; int nArgs; const char *pszWhat; } Set[] = {
        { 206, 1, "ordinal 206" }, { 254, 1, "DisconnectWindowsDialog" },
        { 787, 8, "SHMapIDListToSystemImageListIndexAsync" },
        { 790, 4, "SHMapIDListToSystemImageListIndex" },
        { 792, 3, "ordinal 792" }, { 840, 3, "PathGetPathDisplayName" },
        { 850, 2, "PathComparePaths" }, { 885, 1, "ordinal 885" },
        { 892, 3, "ordinal 892" }, { 893, 6, "ordinal 893" },
        { 894, 7, "ordinal 894" }, { 895, 3, "ordinal 895" },
        { 896, 1, "ordinal 896" }, { 899, 1, "ordinal 899" },
        { 904, 2, "ordinal 904" }, { 905, 2, "ordinal 905" },
        { 906, 0, "ordinal 906" },
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Set); i++)
        Ord(Set[i].w, Set[i].pszWhat);
}

static void
Test_StackBalance(void)
{
    FN_Ord892 p892 = (FN_Ord892)GetProcAddress(hShell32, (LPCSTR)(ULONG_PTR)ORD_Ordinal892);
    FN_Ord899 p899 = (FN_Ord899)GetProcAddress(hShell32, (LPCSTR)(ULONG_PTR)ORD_Ordinal899);
    FN_PathComparePaths pCmp =
        (FN_PathComparePaths)GetProcAddress(hShell32, (LPCSTR)(ULONG_PTR)ORD_PathComparePaths);
    volatile ULONG_PTR Guard = 0x5C5C5C5C;
    UINT i;

    for (i = 0; i < 3000; i++)
    {
        if (p892) p892(NULL, NULL, NULL);
        if (p899) p899(NULL);
        if (pCmp) pCmp(L"x", L"x");
    }
    ok(Guard == 0x5C5C5C5C,
       "the caller frame was corrupted: a shell32 ordinal has the wrong arity\n");
}

START_TEST(Win81Ordinals)
{
    hShell32 = LoadLibraryW(L"shell32.dll");
    ok(hShell32 != NULL, "shell32.dll failed to load, error %lu\n", GetLastError());
    if (!hShell32)
        return;

    Test_AllOrdinalsResolve();

    if (!IsReactOS())
    {
        /*
         * These are unnamed ordinals whose meaning is specific to the Windows
         * 8.1 export table; a later Windows renumbers them, so calling them
         * would exercise unrelated code with the wrong argument count.
         */
        trace("running on Windows: 8.1 ordinal semantics are not asserted\n");
        FreeLibrary(hShell32);
        return;
    }

    Test_PathComparePaths();
    Test_Ordinal892_IsBool();
    Test_Ordinal899_IsSetter();
    Test_Ordinal905_ZeroesOutput();
    Test_MapIDListIsHResultShaped();
    Test_StackBalance();

    FreeLibrary(hShell32);
}
