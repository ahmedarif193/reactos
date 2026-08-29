/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Remaining NT 10+ RTL compatibility services under validation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <apisets.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
RtlIsApiSetImplemented(
    _In_ PCSTR ApiSetName)
{
    ANSI_STRING AnsiName;
    UNICODE_STRING UnicodeName;
    UNICODE_STRING HostName;
    BOOLEAN Resolved = FALSE;
    WCHAR NameBuffer[128];
    NTSTATUS Status;

    if (ApiSetName == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlInitAnsiString(&AnsiName, ApiSetName);
    if ((AnsiName.Length >= sizeof(".dll") - 1) && !_stricmp(ApiSetName + AnsiName.Length - (sizeof(".dll") - 1), ".dll"))
        return STATUS_INVALID_PARAMETER;

    UnicodeName.Buffer = NameBuffer;
    UnicodeName.Length = 0;
    UnicodeName.MaximumLength = sizeof(NameBuffer);
    Status = RtlAnsiStringToUnicodeString(&UnicodeName, &AnsiName, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ApiSetResolveToHost(APISET_WIN10, &UnicodeName, &Resolved, &HostName);
    if (!NT_SUCCESS(Status))
        return Status;
    return (Resolved && HostName.Length != 0) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

VOID
NTAPI
RtlLogUnexpectedCodepath(VOID)
{
}

NTSTATUS
NTAPI
RtlCapabilityCheck(
    _In_opt_ PVOID TokenObject,
    _In_ PCUNICODE_STRING CapabilityName,
    _Out_ PBOOLEAN HasCapability)
{
    UNREFERENCED_PARAMETER(TokenObject);

    if ((CapabilityName == NULL) || (HasCapability == NULL))
        return STATUS_INVALID_PARAMETER;
    *HasCapability = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlCapabilityCheckForSingleSessionSku(
    _In_opt_ PVOID TokenObject,
    _In_ PCUNICODE_STRING CapabilityName,
    _Out_ PBOOLEAN HasCapability)
{
    return RtlCapabilityCheck(TokenObject, CapabilityName, HasCapability);
}

NTSTATUS
NTAPI
RtlGetAppContainerSidType(
    _In_ PSID Sid,
    _Out_ PULONG SidType)
{
    SID_IDENTIFIER_AUTHORITY AppPackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    UCHAR Count;

    if (SidType == NULL)
        return STATUS_INVALID_PARAMETER;
    *SidType = 0;
    if (!RtlValidSid(Sid) || (RtlCompareMemory(RtlIdentifierAuthoritySid(Sid), &AppPackageAuthority, sizeof(AppPackageAuthority)) != sizeof(AppPackageAuthority)) || (*RtlSubAuthoritySid(Sid, 0) != SECURITY_APP_PACKAGE_BASE_RID))
        return (NTSTATUS)0xC000A200;

    Count = *RtlSubAuthorityCountSid(Sid);
    *SidType = Count == 12 ? 1 : Count == 8 ? 2 : 3;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlQueryPackageClaims(
    _In_opt_ PVOID TokenObject,
    _Out_writes_bytes_to_opt_(*PackageSize, *PackageSize) PWSTR PackageFullName,
    _Inout_opt_ PSIZE_T PackageSize,
    _Out_writes_bytes_to_opt_(*AppIdSize, *AppIdSize) PWSTR AppId,
    _Inout_opt_ PSIZE_T AppIdSize,
    _Out_opt_ PGUID DynamicId,
    _Out_opt_ PVOID PackageClaim)
{
    UNREFERENCED_PARAMETER(TokenObject);
    UNREFERENCED_PARAMETER(PackageFullName);
    UNREFERENCED_PARAMETER(AppId);
    UNREFERENCED_PARAMETER(PackageClaim);

    if (PackageSize != NULL)
        *PackageSize = 0;
    if (AppIdSize != NULL)
        *AppIdSize = 0;
    if (DynamicId != NULL)
        RtlZeroMemory(DynamicId, sizeof(*DynamicId));
    return STATUS_NOT_FOUND;
}
