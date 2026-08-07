/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ResolveLocaleName
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/locale.c
 */

#include "k32_vista.h"

INT
WINAPI
ResolveLocaleName(
    _In_opt_ LPCWSTR NameToResolve,
    _Out_writes_opt_(cchLocaleName) LPWSTR LocaleName,
    _In_ INT cchLocaleName)
{
    static const WCHAR ValidChars[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    WCHAR Tmp[LOCALE_NAME_MAX_LENGTH];
    LCID Lcid = 0;
    LPWSTR p;

    if (cchLocaleName < 0 || (cchLocaleName > 0 && !LocaleName))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!NameToResolve)
    {
        if (!GetUserDefaultLocaleName(Tmp, LOCALE_NAME_MAX_LENGTH))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        Lcid = LocaleNameToLCID(Tmp, 0);
    }
    else
    {
        if (wcsspn(NameToResolve, ValidChars) < wcslen(NameToResolve))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }

        lstrcpynW(Tmp, NameToResolve, LOCALE_NAME_MAX_LENGTH);
        for (;;)
        {
            Lcid = LocaleNameToLCID(Tmp, 0);
            if (Lcid)
                break;
            p = Tmp + wcslen(Tmp);
            while (p != Tmp)
            {
                p--;
                if (*p == L'-' || *p == L'_')
                    break;
            }
            if (p == Tmp)
                break;
            *p = UNICODE_NULL;
        }
    }

    if (Lcid)
        return LCIDToLocaleName(Lcid, LocaleName, cchLocaleName, 0);

    if (cchLocaleName > 0)
        LocaleName[0] = UNICODE_NULL;
    return 1;
}
