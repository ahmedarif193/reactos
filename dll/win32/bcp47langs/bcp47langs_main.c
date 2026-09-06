/*
 * PROJECT:     ReactOS bcp47langs.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     BCP-47 language tag services
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
Bcp47GetNlsForm(PVOID Arg1, PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    DPRINT1("Bcp47GetNlsForm: stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
GetUserLanguagesForUser(PVOID Arg1, PVOID Arg2, PVOID Arg3)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);

    DPRINT1("GetUserLanguagesForUser: stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SqmLanguageProfileData(VOID)
{

    DPRINT1("SqmLanguageProfileData: stub\n");
    return E_NOTIMPL;
}
