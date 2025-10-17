/*
 * PROJECT:         ReactOS system libraries
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * PURPOSE:         Vista resource enumeration APIs
 * COPYRIGHT:       2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

#ifndef RESOURCE_ENUM_LN
#define RESOURCE_ENUM_LN          0x0001
#endif

#ifndef RESOURCE_ENUM_MUI
#define RESOURCE_ENUM_MUI         0x0002
#endif

#ifndef RESOURCE_ENUM_MUI_SYSTEM
#define RESOURCE_ENUM_MUI_SYSTEM  0x0004
#endif

#ifndef RESOURCE_ENUM_VALIDATE
#define RESOURCE_ENUM_VALIDATE    0x0008
#endif

static VOID
K32VistaWarnUnsupportedResourceFlags(DWORD Flags,
                                     LPCSTR Function)
{
    DWORD Unsupported = Flags & (RESOURCE_ENUM_MUI | RESOURCE_ENUM_MUI_SYSTEM | RESOURCE_ENUM_VALIDATE);

    if (Unsupported != 0)
    {
        DPRINT1("%s: ignoring unsupported flags 0x%lx\n", Function, Unsupported);
    }
}

BOOL
WINAPI
EnumResourceNamesExA(HMODULE hModule,
                     LPCSTR lpType,
                     ENUMRESNAMEPROCA lpEnumFunc,
                     LONG_PTR lParam,
                     DWORD dwFlags,
                     LANGID LangId)
{
    K32VistaWarnUnsupportedResourceFlags(dwFlags, __FUNCTION__);

    if (!(dwFlags & RESOURCE_ENUM_LN) && (dwFlags != 0))
        return TRUE;

    if (!hModule)
        hModule = GetModuleHandleA(NULL);

    UNREFERENCED_PARAMETER(LangId);
    return EnumResourceNamesA(hModule, lpType, lpEnumFunc, lParam);
}

BOOL
WINAPI
EnumResourceNamesExW(HMODULE hModule,
                     LPCWSTR lpType,
                     ENUMRESNAMEPROCW lpEnumFunc,
                     LONG_PTR lParam,
                     DWORD dwFlags,
                     LANGID LangId)
{
    K32VistaWarnUnsupportedResourceFlags(dwFlags, __FUNCTION__);

    if (!(dwFlags & RESOURCE_ENUM_LN) && (dwFlags != 0))
        return TRUE;

    if (!hModule)
        hModule = GetModuleHandleW(NULL);

    UNREFERENCED_PARAMETER(LangId);
    return EnumResourceNamesW(hModule, lpType, lpEnumFunc, lParam);
}

BOOL
WINAPI
EnumResourceTypesExA(HMODULE hModule,
                     ENUMRESTYPEPROCA lpEnumFunc,
                     LONG_PTR lParam,
                     DWORD dwFlags,
                     LANGID LangId)
{
    K32VistaWarnUnsupportedResourceFlags(dwFlags, __FUNCTION__);

    if (!(dwFlags & RESOURCE_ENUM_LN) && (dwFlags != 0))
        return TRUE;

    if (!hModule)
        hModule = GetModuleHandleA(NULL);

    UNREFERENCED_PARAMETER(LangId);
    return EnumResourceTypesA(hModule, lpEnumFunc, lParam);
}

BOOL
WINAPI
EnumResourceTypesExW(HMODULE hModule,
                     ENUMRESTYPEPROCW lpEnumFunc,
                     LONG_PTR lParam,
                     DWORD dwFlags,
                     LANGID LangId)
{
    K32VistaWarnUnsupportedResourceFlags(dwFlags, __FUNCTION__);

    if (!(dwFlags & RESOURCE_ENUM_LN) && (dwFlags != 0))
        return TRUE;

    if (!hModule)
        hModule = GetModuleHandleW(NULL);

    UNREFERENCED_PARAMETER(LangId);
    return EnumResourceTypesW(hModule, lpEnumFunc, lParam);
}

BOOL
WINAPI
EnumResourceLanguagesExA(HMODULE hModule,
                         LPCSTR lpType,
                         LPCSTR lpName,
                         ENUMRESLANGPROCA lpEnumFunc,
                         LONG_PTR lParam,
                         DWORD dwFlags,
                         LANGID LangId)
{
    K32VistaWarnUnsupportedResourceFlags(dwFlags, __FUNCTION__);

    if (!(dwFlags & RESOURCE_ENUM_LN) && (dwFlags != 0))
        return TRUE;

    if (!hModule)
        hModule = GetModuleHandleA(NULL);

    UNREFERENCED_PARAMETER(LangId);
    return EnumResourceLanguagesA(hModule, lpType, lpName, lpEnumFunc, lParam);
}

BOOL
WINAPI
EnumResourceLanguagesExW(HMODULE hModule,
                         LPCWSTR lpType,
                         LPCWSTR lpName,
                         ENUMRESLANGPROCW lpEnumFunc,
                         LONG_PTR lParam,
                         DWORD dwFlags,
                         LANGID LangId)
{
    K32VistaWarnUnsupportedResourceFlags(dwFlags, __FUNCTION__);

    if (!(dwFlags & RESOURCE_ENUM_LN) && (dwFlags != 0))
        return TRUE;

    if (!hModule)
        hModule = GetModuleHandleW(NULL);

    UNREFERENCED_PARAMETER(LangId);
    return EnumResourceLanguagesW(hModule, lpType, lpName, lpEnumFunc, lParam);
}
