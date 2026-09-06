/*
 * PROJECT:     ReactOS settingsyncpolicy.dll
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Setting synchronisation group policy
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

BOOL
WINAPI
SettingSync_IsAllowedByGroupPolicy(PVOID Arg1)
{
    UNREFERENCED_PARAMETER(Arg1);

    DPRINT1("SettingSync_IsAllowedByGroupPolicy: stub\n");
    return FALSE;
}
