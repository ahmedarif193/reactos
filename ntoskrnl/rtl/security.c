/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL ACL and package-identity services
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
RtlGetAcesBufferSize(
    _In_ PACL Acl,
    _Out_ PULONG AcesBufferSize)
{
    ULONG Index;
    ULONG Size = 0;
    PACE_HEADER Ace;
    NTSTATUS Status;

    if (AcesBufferSize == NULL)
        return STATUS_INVALID_PARAMETER;
    *AcesBufferSize = 0;
    if (!RtlValidAcl(Acl))
        return STATUS_INVALID_ACL;

    for (Index = 0; Index < Acl->AceCount; ++Index)
    {
        Status = RtlGetAce(Acl, Index, (PVOID *)&Ace);
        if (!NT_SUCCESS(Status))
            return Status;
        Size += Ace->AceSize;
    }

    *AcesBufferSize = Size;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlQueryPackageIdentity(
    _In_opt_ PVOID TokenObject,
    _Out_writes_bytes_to_opt_(*PackageSize, *PackageSize) PWSTR PackageFullName,
    _Inout_ PSIZE_T PackageSize,
    _Out_writes_bytes_to_opt_(*AppIdSize, *AppIdSize) PWSTR AppId,
    _Inout_opt_ PSIZE_T AppIdSize,
    _Out_opt_ PBOOLEAN Packaged)
{
    UNREFERENCED_PARAMETER(TokenObject);
    UNREFERENCED_PARAMETER(PackageFullName);
    UNREFERENCED_PARAMETER(AppId);
    UNREFERENCED_PARAMETER(AppIdSize);
    UNREFERENCED_PARAMETER(Packaged);

    if (PackageSize == NULL)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_FOUND;
}
