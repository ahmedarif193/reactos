/*
 * PROJECT:     ReactOS Win32 Advanced API Library
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Transacted registry API forwarders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <advapi32.h>

LONG
WINAPI
RegCreateKeyTransactedA(
    _In_ HKEY hKey,
    _In_ LPCSTR lpSubKey,
    _In_ DWORD Reserved,
    _In_opt_ LPSTR lpClass,
    _In_ DWORD dwOptions,
    _In_ REGSAM samDesired,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _Out_ PHKEY phkResult,
    _Out_opt_ LPDWORD lpdwDisposition,
    _In_ HANDLE hTransaction,
    _Reserved_ PVOID pExtendedParameter)
{
    if (Reserved != 0)
        return ERROR_INVALID_PARAMETER;

    if (pExtendedParameter != NULL)
        return ERROR_INVALID_PARAMETER;

    if (hTransaction != NULL && hTransaction != INVALID_HANDLE_VALUE)
        return ERROR_NOT_SUPPORTED;

    return RegCreateKeyExA(hKey,
                           lpSubKey,
                           Reserved,
                           lpClass,
                           dwOptions,
                           samDesired,
                           lpSecurityAttributes,
                           phkResult,
                           lpdwDisposition);
}

LONG
WINAPI
RegCreateKeyTransactedW(
    _In_ HKEY hKey,
    _In_ LPCWSTR lpSubKey,
    _In_ DWORD Reserved,
    _In_opt_ LPWSTR lpClass,
    _In_ DWORD dwOptions,
    _In_ REGSAM samDesired,
    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    _Out_ PHKEY phkResult,
    _Out_opt_ LPDWORD lpdwDisposition,
    _In_ HANDLE hTransaction,
    _Reserved_ PVOID pExtendedParameter)
{
    if (Reserved != 0)
        return ERROR_INVALID_PARAMETER;

    if (pExtendedParameter != NULL)
        return ERROR_INVALID_PARAMETER;

    if (hTransaction != NULL && hTransaction != INVALID_HANDLE_VALUE)
        return ERROR_NOT_SUPPORTED;

    return RegCreateKeyExW(hKey,
                           lpSubKey,
                           Reserved,
                           lpClass,
                           dwOptions,
                           samDesired,
                           lpSecurityAttributes,
                           phkResult,
                           lpdwDisposition);
}
