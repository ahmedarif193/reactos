/*
 * PROJECT:     ReactOS dui70.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DirectUI runtime entry points
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
InitProcessPriv(PVOID Arg1, PVOID Arg2, PVOID Arg3, PVOID Arg4, PVOID Arg5)
{
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    UNREFERENCED_PARAMETER(Arg3);
    UNREFERENCED_PARAMETER(Arg4);
    UNREFERENCED_PARAMETER(Arg5);

    DPRINT1("InitProcessPriv: stub\n");
    return E_NOTIMPL;
}

HRESULT
WINAPI
InitThread(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    DPRINT1("InitThread: stub\n");
    return E_NOTIMPL;
}

VOID
WINAPI
SkipDLLUnloadInitChecks(VOID)
{

    DPRINT1("SkipDLLUnloadInitChecks: stub\n");
}

HRESULT
WINAPI
UnInitProcessPriv(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    DPRINT1("UnInitProcessPriv: stub\n");
    return E_NOTIMPL;
}

VOID
WINAPI
UnInitThread(VOID)
{

    DPRINT1("UnInitThread: stub\n");
}
