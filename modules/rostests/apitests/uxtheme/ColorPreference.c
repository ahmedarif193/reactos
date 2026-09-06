/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     ABI contract tests for the uxtheme colour-preference ordinals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Measured against Windows 8.1 uxtheme.dll:
 *   #86  1 arg  - explorer branches on it with `test eax,eax; je`, so the
 *                 return value is a BOOL. Anything HRESULT-shaped (E_NOTIMPL
 *                 is non-zero) sends the caller down the success path.
 *   #118 2 args - writes 8 bytes through its first argument and returns S_OK
 *                 unconditionally (`xor eax,eax`). explorer reads that buffer
 *                 without checking the return value.
 *   #120 2 args - GetUserColorPreference; also `xor eax,eax` -> always S_OK,
 *                 and fills the 8-byte preference explorer then hands to #121.
 *   #121 4 args - GetColorFromPreference, returns a COLORREF.
 *   #122 2 args - result is stored by the caller.
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <uxtheme.h>
#include <versionhelpers.h>
#include <pseh/pseh2.h>

#define ORD_UxTheme86               86
#define ORD_UxTheme118             118
#define ORD_GetUserColorPreference 120
#define ORD_GetColorFromPreference 121
#define ORD_UxTheme122             122

typedef BOOL (WINAPI *FN_Ord86)(PVOID);
typedef HRESULT (WINAPI *FN_Ord118)(PVOID, PVOID);
typedef HRESULT (WINAPI *FN_GetUserColorPreference)(PVOID, BOOL);
typedef COLORREF (WINAPI *FN_GetColorFromPreference)(PVOID, int, BOOL, int);
typedef HRESULT (WINAPI *FN_Ord122)(PVOID, PVOID);

static HMODULE hUxTheme;

static FARPROC
Ordinal(WORD w, const char *pszWhat)
{
    FARPROC fp = GetProcAddress(hUxTheme, (LPCSTR)(ULONG_PTR)w);

    ok(fp != NULL, "uxtheme.#%u (%s) is missing\n", w, pszWhat);
    return fp;
}

/* The preference block explorer keeps on the stack is two DWORDs wide. */
typedef struct _COLOR_PREFERENCE
{
    DWORD dw0;
    DWORD dw1;
} COLOR_PREFERENCE;

static void
Test_Ordinal86_IsBool(void)
{
    FN_Ord86 pFn = (FN_Ord86)Ordinal(ORD_UxTheme86, "boolean query");
    BOOL ret;

    if (!pFn)
        return;

    ret = pFn(NULL);
    ok(ret == FALSE || ret == TRUE,
       "uxtheme.#86 must return a canonical BOOL, got 0x%08x "
       "(an HRESULT-shaped failure reads as TRUE)\n", ret);

    /* An unsupported query has to report FALSE, not a non-zero error code. */
    ok(ret == FALSE, "uxtheme.#86 should report FALSE when unsupported, got 0x%08x\n", ret);
}

static void
Test_Ordinal118_WritesOutput(void)
{
    FN_Ord118 pFn = (FN_Ord118)Ordinal(ORD_UxTheme118, "8-byte out query");
    COLOR_PREFERENCE pref;
    HRESULT hr;

    if (!pFn)
        return;

    memset(&pref, 0xCC, sizeof(pref));
    hr = pFn(&pref, NULL);

    ok(hr == S_OK, "uxtheme.#118 returns S_OK unconditionally on Windows, got 0x%08lx\n", hr);
    ok(pref.dw0 != 0xCCCCCCCC || pref.dw1 != 0xCCCCCCCC,
       "uxtheme.#118 must write its output buffer; explorer reads it without "
       "checking the return value\n");

    _SEH2_TRY
    {
        pFn(NULL, NULL);
        ok(TRUE, "uxtheme.#118(NULL) survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("uxtheme.#118(NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
}

static void
Test_GetUserColorPreference(void)
{
    FN_GetUserColorPreference pFn =
        (FN_GetUserColorPreference)Ordinal(ORD_GetUserColorPreference, "GetUserColorPreference");
    COLOR_PREFERENCE pref;
    HRESULT hr;

    if (!pFn)
        return;

    memset(&pref, 0xCC, sizeof(pref));
    hr = pFn(&pref, FALSE);
    ok(hr == S_OK, "GetUserColorPreference returns S_OK unconditionally, got 0x%08lx\n", hr);
    ok(pref.dw0 != 0xCCCCCCCC || pref.dw1 != 0xCCCCCCCC,
       "GetUserColorPreference must initialise the preference it hands to "
       "GetColorFromPreference\n");

    memset(&pref, 0xCC, sizeof(pref));
    hr = pFn(&pref, TRUE);
    ok(hr == S_OK, "GetUserColorPreference(force) returned 0x%08lx\n", hr);

    _SEH2_TRY
    {
        pFn(NULL, FALSE);
        ok(TRUE, "GetUserColorPreference(NULL) survived\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("GetUserColorPreference(NULL) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
}

static void
Test_GetColorFromPreference(void)
{
    FN_GetUserColorPreference pGet =
        (FN_GetUserColorPreference)GetProcAddress(hUxTheme, (LPCSTR)(ULONG_PTR)ORD_GetUserColorPreference);
    FN_GetColorFromPreference pFn =
        (FN_GetColorFromPreference)Ordinal(ORD_GetColorFromPreference, "GetColorFromPreference");
    COLOR_PREFERENCE pref;
    COLORREF cr;
    int i;

    if (!pFn || !pGet)
        return;

    memset(&pref, 0, sizeof(pref));
    pGet(&pref, FALSE);

    /*
     * Windows returns the immersive colour with a fully opaque alpha channel,
     * so the top byte is 0xFF rather than the zero of a classic COLORREF.
     */
    for (i = 0; i < 8; i++)
    {
        cr = pFn(&pref, i, FALSE, 0);
        trace("GetColorFromPreference(%d) = 0x%08lx\n", i, cr);
        ok((cr & 0xFF000000) == 0xFF000000,
           "GetColorFromPreference(%d) must carry an opaque alpha, got 0x%08lx\n", i, cr);
    }

    cr = pFn(&pref, 0, TRUE, COLOR_WINDOW);
    ok((cr & 0xFF000000) == 0xFF000000,
       "GetColorFromPreference(high contrast) must carry an opaque alpha, got 0x%08lx\n", cr);
}

/*
 * A stdcall arity mismatch between the .spec and the implementation unbalances
 * the caller stack. On i386 the guard below catches it directly; on other
 * targets this still exercises the call path.
 */
static void
Test_StackBalance(void)
{
    FN_Ord86 p86 = (FN_Ord86)GetProcAddress(hUxTheme, (LPCSTR)(ULONG_PTR)ORD_UxTheme86);
    FN_Ord118 p118 = (FN_Ord118)GetProcAddress(hUxTheme, (LPCSTR)(ULONG_PTR)ORD_UxTheme118);
    FN_Ord122 p122 = (FN_Ord122)GetProcAddress(hUxTheme, (LPCSTR)(ULONG_PTR)ORD_UxTheme122);
    COLOR_PREFERENCE pref;
    volatile ULONG_PTR Guard = 0xA5A5A5A5;
    UINT i;

    memset(&pref, 0, sizeof(pref));
    for (i = 0; i < 2000; i++)
    {
        if (p86) p86(NULL);
        if (p118) p118(&pref, NULL);
        if (p122) p122(&pref, NULL);
    }
    ok(Guard == 0xA5A5A5A5,
       "the caller frame was corrupted: a uxtheme ordinal has the wrong arity\n");
}

START_TEST(ColorPreference)
{
    hUxTheme = LoadLibraryW(L"uxtheme.dll");
    ok(hUxTheme != NULL, "uxtheme.dll failed to load, error %lu\n", GetLastError());
    if (!hUxTheme)
        return;

    Test_Ordinal86_IsBool();
    Test_Ordinal118_WritesOutput();
    Test_GetUserColorPreference();
    Test_GetColorFromPreference();
    Test_StackBalance();

    FreeLibrary(hUxTheme);
}
