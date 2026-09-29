/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Package identity of an AppContainer token
 */

#include <windows.h>
#include <appmodel.h>
#include <winternl.h>
#include <strsafe.h>

#define APPCONTAINER_MAPPINGS_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings"

static
LONG
BasepGetTokenMoniker(
    _In_ HANDLE Token,
    _Out_writes_(Length) PWSTR Moniker,
    _In_ DWORD Length)
{
    UCHAR Buffer[sizeof(TOKEN_APPCONTAINER_INFORMATION) + SECURITY_MAX_SID_SIZE];
    PTOKEN_APPCONTAINER_INFORMATION Package = (PTOKEN_APPCONTAINER_INFORMATION)Buffer;
    WCHAR Path[512];
    UNICODE_STRING SidString;
    NTSTATUS Status;
    DWORD Returned, Type, Size;
    HKEY Key;
    LONG Error;

    if (!GetTokenInformation(Token, TokenAppContainerSid, Buffer, sizeof(Buffer), &Returned) ||
        !Package->TokenAppContainer)
    {
        return APPMODEL_ERROR_NO_PACKAGE;
    }

    Status = RtlConvertSidToUnicodeString(&SidString, Package->TokenAppContainer, TRUE);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    Error = ERROR_SUCCESS;
    if (FAILED(StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%.*s", APPCONTAINER_MAPPINGS_KEY,
                                (int)(SidString.Length / sizeof(WCHAR)), SidString.Buffer)))
        Error = ERROR_INVALID_PARAMETER;
    RtlFreeUnicodeString(&SidString);
    if (Error != ERROR_SUCCESS)
        return Error;

    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_QUERY_VALUE, &Key);
    if (Error != ERROR_SUCCESS)
        return APPMODEL_ERROR_NO_PACKAGE;

    Size = Length * sizeof(WCHAR);
    Error = RegQueryValueExW(Key, L"Moniker", NULL, &Type, (LPBYTE)Moniker, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS)
        return Error == ERROR_FILE_NOT_FOUND ? APPMODEL_ERROR_NO_PACKAGE : Error;
    if (Type != REG_SZ)
        return APPMODEL_ERROR_NO_PACKAGE;

    Moniker[Length - 1] = UNICODE_NULL;
    return ERROR_SUCCESS;
}

LONG
WINAPI
GetPackageFamilyNameFromToken(
    _In_ HANDLE token,
    _Inout_ UINT32 *packageFamilyNameLength,
    _Out_writes_opt_(*packageFamilyNameLength) PWSTR packageFamilyName)
{
    UNREFERENCED_PARAMETER(token);
    UNREFERENCED_PARAMETER(packageFamilyName);

    if (!packageFamilyNameLength)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG
WINAPI
GetPackageFullNameFromToken(
    _In_ HANDLE token,
    _Inout_ UINT32 *packageFullNameLength,
    _Out_writes_opt_(*packageFullNameLength) PWSTR packageFullName)
{
    UNREFERENCED_PARAMETER(token);
    UNREFERENCED_PARAMETER(packageFullName);

    if (!packageFullNameLength)
        return ERROR_INVALID_PARAMETER;
    return APPMODEL_ERROR_NO_PACKAGE;
}

LONG
WINAPI
GetApplicationUserModelIdFromToken(
    _In_ HANDLE token,
    _Inout_ UINT32 *applicationUserModelIdLength,
    _Out_writes_opt_(*applicationUserModelIdLength) PWSTR applicationUserModelId)
{
    WCHAR Moniker[256];
    LONG Error;

    if (!applicationUserModelIdLength)
        return ERROR_INVALID_PARAMETER;

    UNREFERENCED_PARAMETER(applicationUserModelId);

    Error = BasepGetTokenMoniker(token, Moniker, ARRAYSIZE(Moniker));
    if (Error != ERROR_SUCCESS)
        return Error;

    return APPMODEL_ERROR_NO_APPLICATION;
}
