/*
 * PROJECT:     ReactOS KernelBase
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AppContainer SID-to-moniker registration helpers
 */

#include <windows.h>
#include <sddl.h>
#include <strsafe.h>

#define APPCONTAINER_MAPPINGS_KEY \
    L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings"

static HRESULT
BasepFormatAppContainerMappingPath(
    _In_ PSID AppContainerSid,
    _Out_writes_(PathCount) PWSTR Path,
    _In_ SIZE_T PathCount)
{
    LPWSTR SidString = NULL;
    HRESULT Result;

    if (!AppContainerSid || !IsValidSid(AppContainerSid))
        return E_INVALIDARG;
    if (!ConvertSidToStringSidW(AppContainerSid, &SidString))
        return HRESULT_FROM_WIN32(GetLastError());

    Result = StringCchPrintfW(Path,
                              PathCount,
                              L"%s\\%s",
                              APPCONTAINER_MAPPINGS_KEY,
                              SidString);
    LocalFree(SidString);
    return Result;
}

HRESULT
WINAPI
AppContainerRegisterSid(
    _In_ PSID AppContainerSid,
    _In_ PCWSTR Moniker,
    _In_ PCWSTR DisplayName)
{
    WCHAR Path[512];
    HKEY Key;
    DWORD Disposition;
    LONG Error;
    HRESULT Result;

    if (!Moniker || !DisplayName)
        return E_INVALIDARG;

    Result = BasepFormatAppContainerMappingPath(AppContainerSid,
                                                Path,
                                                ARRAYSIZE(Path));
    if (FAILED(Result))
        return Result;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER,
                            Path,
                            0,
                            NULL,
                            0,
                            KEY_READ | KEY_WRITE,
                            NULL,
                            &Key,
                            &Disposition);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);
    if (Disposition != REG_CREATED_NEW_KEY)
    {
        RegCloseKey(Key);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    Error = RegSetValueExW(Key,
                           L"Moniker",
                           0,
                           REG_SZ,
                           (const BYTE *)Moniker,
                           (DWORD)((lstrlenW(Moniker) + 1) * sizeof(WCHAR)));
    if (Error == ERROR_SUCCESS)
    {
        Error = RegSetValueExW(Key,
                               L"DisplayName",
                               0,
                               REG_SZ,
                               (const BYTE *)DisplayName,
                               (DWORD)((lstrlenW(DisplayName) + 1) * sizeof(WCHAR)));
    }
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
    {
        RegDeleteKeyW(HKEY_CURRENT_USER, Path);
        return HRESULT_FROM_WIN32(Error);
    }
    return S_OK;
}

HRESULT
WINAPI
AppContainerUnregisterSid(
    _In_ PSID AppContainerSid)
{
    WCHAR Path[512];
    LONG Error;
    HRESULT Result;

    Result = BasepFormatAppContainerMappingPath(AppContainerSid,
                                                Path,
                                                ARRAYSIZE(Path));
    if (FAILED(Result))
        return Result;

    Error = RegDeleteKeyW(HKEY_CURRENT_USER, Path);
    return Error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(Error);
}

HRESULT
WINAPI
AppContainerLookupMoniker(
    _In_ PSID AppContainerSid,
    _Outptr_ LPWSTR *Moniker)
{
    WCHAR Path[512];
    DWORD Type, Size = 0;
    LPWSTR Buffer;
    HKEY Key;
    LONG Error;
    HRESULT Result;

    if (!Moniker)
        return E_INVALIDARG;

    Result = BasepFormatAppContainerMappingPath(AppContainerSid,
                                                Path,
                                                ARRAYSIZE(Path));
    if (FAILED(Result))
        return Result;

    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(Error);

    Error = RegQueryValueExW(Key, L"Moniker", NULL, &Type, NULL, &Size);
    if (Error != ERROR_SUCCESS || Type != REG_SZ || Size < sizeof(WCHAR))
    {
        RegCloseKey(Key);
        return Error == ERROR_SUCCESS ? E_INVALIDARG : HRESULT_FROM_WIN32(Error);
    }

    Buffer = LocalAlloc(LMEM_FIXED, Size);
    if (!Buffer)
    {
        RegCloseKey(Key);
        return E_OUTOFMEMORY;
    }

    Error = RegQueryValueExW(Key,
                             L"Moniker",
                             NULL,
                             &Type,
                             (LPBYTE)Buffer,
                             &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_SZ)
    {
        LocalFree(Buffer);
        return Error == ERROR_SUCCESS ? E_INVALIDARG : HRESULT_FROM_WIN32(Error);
    }

    Buffer[Size / sizeof(WCHAR) - 1] = UNICODE_NULL;
    *Moniker = Buffer;
    return S_OK;
}

VOID
WINAPI
AppContainerFreeMemory(
    _In_opt_ PVOID Memory)
{
    LocalFree(Memory);
}
