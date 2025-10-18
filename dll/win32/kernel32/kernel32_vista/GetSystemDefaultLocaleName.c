/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of GetSystemDefaultLocaleName
 * COPYRIGHT:   Copyright 2025 ReactOS contributors
 */

#include "k32_vista.h"

INT
WINAPI
GetSystemDefaultLocaleName(
    _Out_writes_(cchLocaleName) LPWSTR lpLocaleName,
    _In_ INT cchLocaleName)
{
    UNICODE_STRING LocaleNameString;
    NTSTATUS Status;

    if (!lpLocaleName || cchLocaleName <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    cchLocaleName = min(cchLocaleName, LOCALE_NAME_MAX_LENGTH);

    LocaleNameString.Buffer = lpLocaleName;
    LocaleNameString.Length = 0;
    LocaleNameString.MaximumLength = (USHORT)(cchLocaleName * sizeof(WCHAR));

    Status = RtlLcidToLocaleName(LOCALE_SYSTEM_DEFAULT,
                                 &LocaleNameString,
                                 0,
                                 FALSE);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return 0;
    }

    lpLocaleName[LocaleNameString.Length / sizeof(WCHAR)] = L'\0';
    return LocaleNameString.Length / sizeof(WCHAR);
}
