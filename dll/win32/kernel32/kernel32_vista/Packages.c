/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Packaged application queries
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/version.c
 */

#include "k32_vista.h"

LONG
WINAPI
GetPackagesByPackageFamily(
    _In_ PCWSTR packageFamilyName,
    _Inout_ UINT32 *count,
    _Out_writes_opt_(*count) PWSTR *packageFullNames,
    _Inout_ UINT32 *bufferLength,
    _Out_writes_opt_(*bufferLength) WCHAR *buffer)
{
    UNREFERENCED_PARAMETER(packageFamilyName);
    UNREFERENCED_PARAMETER(packageFullNames);
    UNREFERENCED_PARAMETER(buffer);

    if (!count || !bufferLength)
        return ERROR_INVALID_PARAMETER;

    *count = 0;
    *bufferLength = 0;
    return ERROR_SUCCESS;
}

LONG
WINAPI
GetPackagePathByFullName(
    _In_ PCWSTR packageFullName,
    _Inout_ UINT32 *pathLength,
    _Out_writes_opt_(*pathLength) PWSTR path)
{
    UNREFERENCED_PARAMETER(path);

    if (!packageFullName || !pathLength)
        return ERROR_INVALID_PARAMETER;

    return APPMODEL_ERROR_NO_PACKAGE;
}
