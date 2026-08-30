/*
 * PROJECT:     ReactOS KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Package-family enumeration compatibility
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <appmodel.h>

static BOOL
IsPackageNameCharacter(
    _In_ WCHAR Character)
{
    return (Character >= L'0' && Character <= L'9') ||
           (Character >= L'A' && Character <= L'Z') ||
           (Character >= L'a' && Character <= L'z') ||
           Character == L'.' || Character == L'-';
}

static BOOL
IsPublisherIdCharacter(
    _In_ WCHAR Character)
{
    static const WCHAR PublisherIdAlphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    const WCHAR *Current;

    for (Current = PublisherIdAlphabet; *Current; Current++)
    {
        if (*Current == Character)
            return TRUE;
    }

    return FALSE;
}

static BOOL
IsValidPackageFamilyName(
    _In_ PCWSTR PackageFamilyName)
{
    const WCHAR *Current;
    const WCHAR *Separator = NULL;
    SIZE_T NameLength;
    SIZE_T PublisherIdLength;

    for (Current = PackageFamilyName; *Current; Current++)
    {
        if (*Current == L'_')
        {
            if (Separator)
                return FALSE;
            Separator = Current;
        }
        else if (!Separator && !IsPackageNameCharacter(*Current))
        {
            return FALSE;
        }
        else if (Separator && !IsPublisherIdCharacter(*Current))
        {
            return FALSE;
        }
    }

    if (!Separator)
        return FALSE;

    NameLength = Separator - PackageFamilyName;
    PublisherIdLength = Current - Separator - 1;
    if (NameLength < 3 || NameLength > 50 || PublisherIdLength != 13)
        return FALSE;

    return Separator[-1] != L'.';
}

LONG
WINAPI
FindPackagesByPackageFamily(
    _In_ PCWSTR PackageFamilyName,
    _In_ UINT32 PackageFilters,
    _Inout_ UINT32 *Count,
    _Out_writes_opt_(*Count) PWSTR *PackageFullNames,
    _Inout_ UINT32 *BufferLength,
    _Out_writes_opt_(*BufferLength) WCHAR *Buffer,
    _Out_writes_opt_(*Count) UINT32 *PackageProperties)
{
    UNREFERENCED_PARAMETER(PackageFilters);
    UNREFERENCED_PARAMETER(PackageProperties);

    if (!PackageFamilyName || !Count || !BufferLength ||
        (!PackageFullNames && *Count) || (!Buffer && *BufferLength) ||
        !IsValidPackageFamilyName(PackageFamilyName))
    {
        return ERROR_INVALID_PARAMETER;
    }

    *Count = 0;
    *BufferLength = 0;
    return ERROR_SUCCESS;
}
