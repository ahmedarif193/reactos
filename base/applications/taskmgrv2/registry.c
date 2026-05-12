/*
 * registry.c — Thin helpers over RegOpenKeyExW / RegQuery/SetValueExW.
 */
#include "precomp.h"

BOOL Reg_ReadDword(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, DWORD *out)
{
    HKEY  hKey;
    DWORD cbData = sizeof(DWORD);
    LONG  rc;

    if (RegOpenKeyExW(hRoot, subkey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return FALSE;

    rc = RegQueryValueExW(hKey, value, NULL, NULL, (LPBYTE)out, &cbData);
    RegCloseKey(hKey);
    return (rc == ERROR_SUCCESS);
}

BOOL Reg_WriteDword(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, DWORD val)
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyExW(hRoot, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS)
        return FALSE;

    rc = RegSetValueExW(hKey, value, 0, REG_DWORD, (const BYTE *)&val, sizeof(DWORD));
    RegCloseKey(hKey);
    return (rc == ERROR_SUCCESS);
}

BOOL Reg_ReadString(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, LPWSTR out, DWORD ccOut)
{
    HKEY  hKey;
    DWORD cbData;
    LONG  rc;

    if (RegOpenKeyExW(hRoot, subkey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return FALSE;

    cbData = ccOut * sizeof(WCHAR);
    rc = RegQueryValueExW(hKey, value, NULL, NULL, (LPBYTE)out, &cbData);
    RegCloseKey(hKey);
    return (rc == ERROR_SUCCESS);
}

BOOL Reg_WriteString(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, LPCWSTR val)
{
    HKEY hKey;
    LONG rc;

    rc = RegCreateKeyExW(hRoot, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS)
        return FALSE;

    rc = RegSetValueExW(hKey, value, 0, REG_SZ,
                        (const BYTE *)val,
                        (DWORD)((lstrlenW(val) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
    return (rc == ERROR_SUCCESS);
}
