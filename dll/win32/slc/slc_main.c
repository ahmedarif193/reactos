/*
 * PROJECT:     ReactOS slc.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Software licensing client
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
SLRegisterWindowsEvent(PVOID Arg1, PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    DPRINT1("SLRegisterWindowsEvent: stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
SLUnregisterWindowsEvent(PVOID Arg1, PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    DPRINT1("SLUnregisterWindowsEvent: stub\n");
    return E_NOTIMPL;
}
