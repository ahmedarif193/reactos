/*
 * PROJECT:     ReactOS sndvolsso.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Volume control shell service object
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
SndVolSSOOrdinal1(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    DPRINT1("SndVolSSOOrdinal1: stub\n");
    return E_NOTIMPL;
}

VOID
WINAPI
SndVolSSOOrdinal2(VOID)
{

    DPRINT1("SndVolSSOOrdinal2: stub\n");
}

HRESULT
WINAPI
SndVolSSOOrdinal3(PVOID Arg1, PVOID Arg2, PVOID Arg3)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);

    DPRINT1("SndVolSSOOrdinal3: stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SndVolSSOOrdinal4(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    DPRINT1("SndVolSSOOrdinal4: stub\n");
    return E_NOTIMPL;
}
