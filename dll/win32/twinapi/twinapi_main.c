/*
 * PROJECT:     ReactOS twinapi.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Immersive shell support
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
TwinApiOrdinal9(VOID)
{

    DPRINT1("TwinApiOrdinal9: stub\n");
    return E_NOTIMPL;
}
