/*
 * PROJECT:     ReactOS user32.dll
 * LICENSE:     GPL-2.0-or-later (see COPYING in the top level directory)
 * PURPOSE:     Exported wvsprintfA/W implementations
 */

#include <user32.h>
#include <stdio.h>
#include <stdarg.h>

static BOOL
User32ValidateFormatBuffer(const void *Buffer, const void *Format)
{
    if (!Buffer || !Format)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return TRUE;
}

INT
WINAPI
wvsprintfA(
    _Out_ LPSTR Buffer,
    _In_ _Printf_format_string_ LPCSTR Format,
    _In_ va_list ArgList)
{
    if (!User32ValidateFormatBuffer(Buffer, Format))
        return 0;

    return vsprintf(Buffer, Format, ArgList);
}

INT
WINAPI
wvsprintfW(
    _Out_ LPWSTR Buffer,
    _In_ _Printf_format_string_ LPCWSTR Format,
    _In_ va_list ArgList)
{
    if (!User32ValidateFormatBuffer(Buffer, Format))
        return 0;

    return _vswprintf(Buffer, Format, ArgList);
}
