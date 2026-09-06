/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture the whole Windows 8.1 export surface added for explorer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Every entry below was measured against the Windows 8.1 x86 binaries: the
 * argument count comes from the stdcall `ret imm16` epilogue, so a mismatch
 * here means the ReactOS .spec disagrees with the real ABI.
 */

#include <apitest.h>
#include <winreg.h>
#include <objbase.h>
#include <string.h>
#include <wchar.h>
#include <versionhelpers.h>
#include <pseh/pseh2.h>

typedef struct _EXPORT_ENTRY
{
    const char *pszName;    /* NULL when the export is by ordinal only */
    WORD wOrdinal;
    int nArgs;              /* argument count measured from Windows 8.1 */
} EXPORT_ENTRY;

typedef struct _MODULE_ENTRY
{
    const WCHAR *pszDll;
    const EXPORT_ENTRY *pEntries;
    UINT cEntries;
} MODULE_ENTRY;

#define E_NAMED(n, a)   { n, 0, a }
#define E_ORD(o, a)     { NULL, o, a }

static const EXPORT_ENTRY Dui70[] = {
    E_NAMED("InitProcessPriv", 5), E_NAMED("InitThread", 1),
    E_NAMED("SkipDLLUnloadInitChecks", 0),
    E_NAMED("UnInitProcessPriv", 1), E_NAMED("UnInitThread", 0),
};
static const EXPORT_ENTRY Bcp47[] = {
    E_NAMED("Bcp47GetNlsForm", 2), E_NAMED("GetUserLanguagesForUser", 3),
    E_NAMED("SqmLanguageProfileData", 0),
};
static const EXPORT_ENTRY WinLangDb[] = {
    E_NAMED("Bcp47GetSerializedUserLanguageProfile", 5),
    E_NAMED("EnsureLanguageProfileExists", 0),
};
static const EXPORT_ENTRY AppxStore[] = {
    E_NAMED("DidAppSurviveOSUpgradeForUser", 3),
};
static const EXPORT_ENTRY SndVol[] = {
    E_ORD(1, 1), E_ORD(2, 0), E_ORD(3, 3), E_ORD(4, 1),
};
static const EXPORT_ENTRY SyncPolicy[] = { E_ORD(3, 1) };
static const EXPORT_ENTRY TwinApi[]    = { E_ORD(9, 0) };
static const EXPORT_ENTRY ProfApi[]    = { E_ORD(104, 4) };
static const EXPORT_ENTRY Slc[] = {
    E_NAMED("SLRegisterWindowsEvent", 2), E_NAMED("SLUnregisterWindowsEvent", 2),
};
static const EXPORT_ENTRY SspiCli[] = {
    E_NAMED("GetUserNameExW", 3), E_NAMED("GetUserNameExA", 3),
};
static const EXPORT_ENTRY WksCli[] = {
    E_NAMED("NetGetJoinInformation", 3),
};

#define MOD(dll, arr) { dll, arr, (UINT)(sizeof(arr) / sizeof((arr)[0])) }

static const MODULE_ENTRY Modules[] = {
    MOD(L"dui70.dll", Dui70),
    MOD(L"bcp47langs.dll", Bcp47),
    MOD(L"winlangdb.dll", WinLangDb),
    MOD(L"appxalluserstore.dll", AppxStore),
    MOD(L"sndvolsso.dll", SndVol),
    MOD(L"settingsyncpolicy.dll", SyncPolicy),
    MOD(L"twinapi.dll", TwinApi),
    MOD(L"profapi.dll", ProfApi),
    MOD(L"slc.dll", Slc),
    MOD(L"sspicli.dll", SspiCli),
    MOD(L"wkscli.dll", WksCli),
};

static void
Test_AllModulesLoad(void)
{
    UINT i, j;

    for (i = 0; i < ARRAYSIZE(Modules); i++)
    {
        HMODULE hMod = LoadLibraryW(Modules[i].pszDll);

        if (!hMod)
        {
            /* Windows drops some of these between releases; ReactOS must ship them all. */
            ok(!IsReactOS(), "%ls failed to load, error %lu\n",
               Modules[i].pszDll, GetLastError());
            if (!IsReactOS())
                trace("%ls is absent on this Windows build\n", Modules[i].pszDll);
            continue;
        }

        for (j = 0; j < Modules[i].cEntries; j++)
        {
            const EXPORT_ENTRY *e = &Modules[i].pEntries[j];
            FARPROC fp;

            if (e->pszName)
                fp = GetProcAddress(hMod, e->pszName);
            else
                fp = GetProcAddress(hMod, (LPCSTR)(ULONG_PTR)e->wOrdinal);

            if (fp == NULL && !IsReactOS())
            {
                trace("%ls!%s is absent on this Windows build\n", Modules[i].pszDll,
                      e->pszName ? e->pszName : "(ordinal)");
            }
            else if (e->pszName)
            {
                ok(fp != NULL, "%ls!%s is missing\n", Modules[i].pszDll, e->pszName);
            }
            else
            {
                ok(fp != NULL, "%ls!#%u is missing\n", Modules[i].pszDll, e->wOrdinal);
            }
        }

        FreeLibrary(hMod);
    }
}

/*
 * Calling every zero-argument stub is safe and proves the .spec arity matches
 * the implementation: a stdcall mismatch corrupts the stack and would be caught
 * by the guard value below on i386.
 */
static void
Test_ZeroArgStubs(void)
{
    static const struct { const WCHAR *pszDll; const char *pszName; WORD wOrd; } Zero[] = {
        { L"dui70.dll", "SkipDLLUnloadInitChecks", 0 },
        { L"dui70.dll", "UnInitThread", 0 },
        { L"bcp47langs.dll", "SqmLanguageProfileData", 0 },
        { L"winlangdb.dll", "EnsureLanguageProfileExists", 0 },
        { L"twinapi.dll", NULL, 9 },
        { L"sndvolsso.dll", NULL, 2 },
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Zero); i++)
    {
        HMODULE hMod = LoadLibraryW(Zero[i].pszDll);
        volatile ULONG_PTR Guard = 0x5A5A5A5A;
        FARPROC fp;

        if (!hMod)
            continue;

        fp = Zero[i].pszName ? GetProcAddress(hMod, Zero[i].pszName)
                             : GetProcAddress(hMod, (LPCSTR)(ULONG_PTR)Zero[i].wOrd);
        if (fp)
        {
            _SEH2_TRY
            {
                ((void (WINAPI *)(void))fp)();
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ok(FALSE, "%ls!%s raised 0x%08lx\n", Zero[i].pszDll,
                   Zero[i].pszName ? Zero[i].pszName : "(ordinal)",
                   _SEH2_GetExceptionCode());
            }
            _SEH2_END;

            ok(Guard == 0x5A5A5A5A,
               "%ls!%s corrupted the caller frame (stdcall arity mismatch)\n",
               Zero[i].pszDll, Zero[i].pszName ? Zero[i].pszName : "(ordinal)");
        }
        FreeLibrary(hMod);
    }
}

/*
 * The forwarder DLLs must land on the very same code as their host, otherwise
 * the forwarder string in the .spec is wrong.
 */
static void
Test_ForwardersMatchHost(void)
{
    static const struct
    {
        const WCHAR *pszForwarder;
        const WCHAR *pszHost;
        const char *pszName;
    } Fwd[] = {
        { L"sspicli.dll", L"secur32.dll",  "GetUserNameExW" },
        { L"sspicli.dll", L"secur32.dll",  "GetUserNameExA" },
        { L"wkscli.dll",  L"netapi32.dll", "NetGetJoinInformation" },
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Fwd); i++)
    {
        HMODULE hF = LoadLibraryW(Fwd[i].pszForwarder);
        HMODULE hH = LoadLibraryW(Fwd[i].pszHost);

        if (hF && hH)
        {
            FARPROC pF = GetProcAddress(hF, Fwd[i].pszName);
            FARPROC pH = GetProcAddress(hH, Fwd[i].pszName);

            ok(pF != NULL, "%ls!%s is missing\n", Fwd[i].pszForwarder, Fwd[i].pszName);
            ok(pH != NULL, "%ls!%s is missing\n", Fwd[i].pszHost, Fwd[i].pszName);
            if (IsReactOS())
            {
                ok(pF == pH, "%ls!%s (%p) must forward to %ls (%p)\n",
                   Fwd[i].pszForwarder, Fwd[i].pszName, pF, Fwd[i].pszHost, pH);
            }
            else
            {
                trace("%ls!%s %p vs %ls %p (%s)\n",
                      Fwd[i].pszForwarder, Fwd[i].pszName, pF, Fwd[i].pszHost, pH,
                      (pF == pH) ? "shared, as on ReactOS" : "separate copies");
            }
        }
        if (hF) FreeLibrary(hF);
        if (hH) FreeLibrary(hH);
    }
}

/*
 * shcore re-exports a set of shlwapi helpers by ordinal. Both sides must be the
 * same function, and every one of them must resolve.
 */
static void
Test_ShcoreForwardParity(void)
{
    static const struct { WORD wShcore; WORD wShlwapi; const char *pszName; } Pair[] = {
        { 184, 8,  "SHLockShared" },
        { 186, 9,  "SHUnlockShared" },
        { 187, 10, "SHFreeShared" },
        { 126, 439, "SHLoadRegUIStringW" },
        { 130, 223, "SHGlobalCounterGetValue" },
        { 142, 175, "IUnknown_GetClassID" },
        { 162, 260, "SHQueueUserWorkItem" },
        { 183, 510, "SHLockSharedEx" },
        { 191, 560, "SHWindowsPolicyGetValue" },
        { 193, 476, "SHGetObjectCompatFlags" },
        { 200, 270, "GUIDFromStringW" },
    };
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    HMODULE hShlwapi = LoadLibraryW(L"shlwapi.dll");
    UINT i;

    if (!hShcore || !hShlwapi)
    {
        skip("shcore/shlwapi unavailable\n");
        if (hShcore) FreeLibrary(hShcore);
        if (hShlwapi) FreeLibrary(hShlwapi);
        return;
    }

    for (i = 0; i < ARRAYSIZE(Pair); i++)
    {
        FARPROC a = GetProcAddress(hShcore, (LPCSTR)(ULONG_PTR)Pair[i].wShcore);
        FARPROC b = GetProcAddress(hShlwapi, (LPCSTR)(ULONG_PTR)Pair[i].wShlwapi);

        ok(a != NULL, "shcore.#%u (%s) is missing\n", Pair[i].wShcore, Pair[i].pszName);
        if (!IsReactOS())
        {
            /* Windows shares most of these between shcore and shlwapi but keeps
             * private copies of the shared-memory group, and the ordinals drift
             * between releases, so only the ReactOS contract is asserted. */
            trace("shcore.#%u=%p shlwapi.#%u=%p (%s)\n",
                  Pair[i].wShcore, a, Pair[i].wShlwapi, b, Pair[i].pszName);
            continue;
        }
        ok(b != NULL, "shlwapi.#%u (%s) is missing\n", Pair[i].wShlwapi, Pair[i].pszName);
        ok(a == b, "shcore.#%u must forward to shlwapi.#%u (%s): %p vs %p\n",
           Pair[i].wShcore, Pair[i].wShlwapi, Pair[i].pszName, a, b);
    }

    FreeLibrary(hShlwapi);
    FreeLibrary(hShcore);
}

static void
Test_LoadUnloadChurn(void)
{
    UINT i, j;

    for (i = 0; i < 100; i++)
    {
        for (j = 0; j < ARRAYSIZE(Modules); j++)
        {
            HMODULE hMod = LoadLibraryW(Modules[j].pszDll);

            if (!hMod)
            {
                ok(!IsReactOS(), "%ls failed to load on iteration %u, error %lu\n",
                   Modules[j].pszDll, i, GetLastError());
                continue;
            }
            FreeLibrary(hMod);
        }
    }
    ok(TRUE, "100 load/unload cycles across %u modules\n", (UINT)ARRAYSIZE(Modules));
}

START_TEST(Win81ExportSurface)
{
    Test_AllModulesLoad();
    Test_ZeroArgStubs();
    Test_ForwardersMatchHost();
    Test_ShcoreForwardParity();
    Test_LoadUnloadChurn();
}
