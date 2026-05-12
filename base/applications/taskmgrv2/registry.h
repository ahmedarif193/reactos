#pragma once

/* -----------------------------------------------------------------------
 * registry.h — Thin helpers over RegOpenKeyExW / RegQuery/SetValueExW.
 * All keys are under HKCU\Software\ReactOS\TaskMgrV2\ and sub-keys.
 * ----------------------------------------------------------------------- */

BOOL Reg_ReadDword (HKEY hRoot, LPCWSTR subkey, LPCWSTR value, DWORD *out);
BOOL Reg_WriteDword(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, DWORD val);
BOOL Reg_ReadString(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, LPWSTR out, DWORD ccOut);
BOOL Reg_WriteString(HKEY hRoot, LPCWSTR subkey, LPCWSTR value, LPCWSTR val);
