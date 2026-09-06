/*
 * PROJECT:     ReactOS winlangdb.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Language profile database
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <windef.h>
#include <winbase.h>
#include <winerror.h>

#define NDEBUG
#include <debug.h>

BOOL
WINAPI
DllMain(HINSTANCE hinstDLL, DWORD dwReason, LPVOID lpvReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
    }
    return TRUE;
}

HRESULT
WINAPI
Bcp47GetSerializedUserLanguageProfile(PVOID Arg1, PVOID Arg2, PVOID Arg3, PVOID Arg4, PVOID Arg5)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);

    DPRINT1("Bcp47GetSerializedUserLanguageProfile: stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
EnsureLanguageProfileExists(VOID)
{

    DPRINT1("EnsureLanguageProfileExists: stub\n");
    return E_NOTIMPL;
}
