/*
 * PROJECT:     ReactOS NT User-Mode DLL
 * PURPOSE:     Capability SID support
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 */

#include <ntdll.h>
#include <symcrypt.h>

SYMCRYPT_CPU_FEATURES SYMCRYPT_CALL
SymCryptCpuFeaturesNeverPresent(VOID)
{
    return 0;
}

VOID SYMCRYPT_CALL
SymCryptFatal(UINT32 FatalCode)
{
    UNREFERENCED_PARAMETER(FatalCode);
}

VOID SYMCRYPT_CALL
SymCryptInjectError(PBYTE Buffer, SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
}

#if SYMCRYPT_CPU_X86 | SYMCRYPT_CPU_AMD64
SYMCRYPT_ERROR SYMCRYPT_CALL
SymCryptSaveXmm(PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
    return SYMCRYPT_NO_ERROR;
}

VOID SYMCRYPT_CALL
SymCryptRestoreXmm(PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
}
#endif

NTSTATUS
NTAPI
RtlDeriveCapabilitySidsFromName(
    _In_ PUNICODE_STRING CapabilityName,
    _Out_ PSID CapabilityGroupSid,
    _Out_ PSID CapabilitySid)
{
    static const SID_IDENTIFIER_AUTHORITY AppAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    static const SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    UNICODE_STRING UppercaseName;
    NTSTATUS Status;
    ULONG Hash[8];
    PISID Sid;

    Status = RtlUpcaseUnicodeString(&UppercaseName, CapabilityName, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    SymCryptSha256((PBYTE)UppercaseName.Buffer, UppercaseName.Length, (PBYTE)Hash);
    RtlFreeUnicodeString(&UppercaseName);

    Sid = CapabilitySid;
    Sid->Revision = SID_REVISION;
    Sid->IdentifierAuthority = AppAuthority;
    Sid->SubAuthorityCount = 2 + RTL_NUMBER_OF(Hash);
    Sid->SubAuthority[0] = SECURITY_BATCH_RID;
    Sid->SubAuthority[1] = SECURITY_CAPABILITY_APP_RID;
    RtlCopyMemory(Sid->SubAuthority + 2, Hash, sizeof(Hash));

    Sid = CapabilityGroupSid;
    Sid->Revision = SID_REVISION;
    Sid->IdentifierAuthority = NtAuthority;
    Sid->SubAuthorityCount = 1 + RTL_NUMBER_OF(Hash);
    Sid->SubAuthority[0] = SECURITY_BUILTIN_DOMAIN_RID;
    RtlCopyMemory(Sid->SubAuthority + 1, Hash, sizeof(Hash));

    return STATUS_SUCCESS;
}
