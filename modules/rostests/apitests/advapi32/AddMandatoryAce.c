/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests AddMandatoryAce behavior and validation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_ACL_SIZE 128
#define TEST_LAST_ERROR 0xdeadbeef

typedef BOOL
(WINAPI *PADD_MANDATORY_ACE)(
    _Inout_ PACL Acl,
    _In_ DWORD Revision,
    _In_ DWORD Flags,
    _In_ DWORD MandatoryFlags,
    _In_ PSID LabelSid);

static VOID
TestAddMandatoryAceCase(
    _In_ PADD_MANDATORY_ACE AddMandatoryAce,
    _In_ PCSTR Name,
    _In_ DWORD Revision,
    _In_ DWORD Flags,
    _In_ DWORD MandatoryFlags,
    _In_ PSID LabelSid,
    _In_ USHORT AclSize,
    _In_ UCHAR InitialAclRevision,
    _In_ BOOL ExpectedSuccess,
    _In_ DWORD ExpectedError,
    _In_ UCHAR ExpectedAclRevision)
{
    BYTE Buffer[TEST_ACL_SIZE], Before[TEST_ACL_SIZE];
    PSYSTEM_MANDATORY_LABEL_ACE Ace;
    PACL Acl = (PACL)Buffer;
    ULONG AceSize, UsedSize;
    DWORD Error;
    BOOL Success;

    memset(Buffer, 0xcc, sizeof(Buffer));
    Success = InitializeAcl(Acl, sizeof(Buffer), ACL_REVISION);
    ok(Success, "%s: InitializeAcl failed with error %lu\n", Name, GetLastError());
    if (!Success) return;

    Acl->AclSize = AclSize;
    Acl->AclRevision = InitialAclRevision;
    memcpy(Before, Buffer, sizeof(Buffer));

    SetLastError(TEST_LAST_ERROR);
    Success = AddMandatoryAce(Acl, Revision, Flags, MandatoryFlags, LabelSid);
    Error = GetLastError();
    ok(Success == ExpectedSuccess, "%s: got success %d, expected %d, error %lu\n", Name, Success, ExpectedSuccess, Error);

    if (!ExpectedSuccess)
    {
        ok(Error == ExpectedError, "%s: got error %lu, expected %lu\n", Name, Error, ExpectedError);
        ok(!memcmp(Buffer, Before, sizeof(Buffer)), "%s: failure modified the ACL buffer\n", Name);
        return;
    }
    if (!Success) return;

    AceSize = FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE, SidStart) + RtlLengthSid(LabelSid);
    UsedSize = sizeof(ACL) + AceSize;
    Ace = (PSYSTEM_MANDATORY_LABEL_ACE)(Acl + 1);

    ok(Acl->AclSize == AclSize, "%s: got ACL size %u, expected %u\n", Name, Acl->AclSize, AclSize);
    ok(Acl->AclRevision == ExpectedAclRevision, "%s: got ACL revision %u, expected %u\n", Name, Acl->AclRevision, ExpectedAclRevision);
    ok(Acl->AceCount == 1, "%s: got ACE count %u, expected 1\n", Name, Acl->AceCount);
    ok(Ace->Header.AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE, "%s: got ACE type %#x\n", Name, Ace->Header.AceType);
    ok(Ace->Header.AceFlags == (BYTE)Flags, "%s: got ACE flags %#x, expected %#lx\n", Name, Ace->Header.AceFlags, Flags);
    ok(Ace->Header.AceSize == AceSize, "%s: got ACE size %u, expected %lu\n", Name, Ace->Header.AceSize, AceSize);
    ok(Ace->Mask == MandatoryFlags, "%s: got mandatory mask %#lx, expected %#lx\n", Name, Ace->Mask, MandatoryFlags);
    ok(RtlEqualSid(&Ace->SidStart, LabelSid), "%s: stored the wrong SID\n", Name);
    ok(UsedSize <= sizeof(Buffer), "%s: computed used size %lu exceeds the test buffer\n", Name, UsedSize);
    if (UsedSize <= sizeof(Buffer)) ok(!memcmp(Buffer + UsedSize, Before + UsedSize, sizeof(Buffer) - UsedSize), "%s: success modified bytes beyond the ACE\n", Name);
}

START_TEST(AddMandatoryAce)
{
    SID_IDENTIFIER_AUTHORITY MandatoryAuthority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY WorldAuthority = SECURITY_WORLD_SID_AUTHORITY;
    BYTE LowSidBuffer[SECURITY_MAX_SID_SIZE];
    BYTE WorldSidBuffer[SECURITY_MAX_SID_SIZE];
    BYTE InvalidSidBuffer[SECURITY_MAX_SID_SIZE];
    BYTE ZeroSubauthoritySidBuffer[SECURITY_MAX_SID_SIZE];
    BYTE TwoSubauthoritySidBuffer[SECURITY_MAX_SID_SIZE];
    BYTE ArbitraryRidSidBuffer[SECURITY_MAX_SID_SIZE];
    PSID LowSid = (PSID)LowSidBuffer;
    PSID WorldSid = (PSID)WorldSidBuffer;
    PSID InvalidSid = (PSID)InvalidSidBuffer;
    PSID ZeroSubauthoritySid = (PSID)ZeroSubauthoritySidBuffer;
    PSID TwoSubauthoritySid = (PSID)TwoSubauthoritySidBuffer;
    PSID ArbitraryRidSid = (PSID)ArbitraryRidSidBuffer;
    PADD_MANDATORY_ACE AddMandatoryAce;
    ULONG AceSize;
    USHORT ExactAclSize;
    HMODULE Advapi32;
    NTSTATUS Status;

    Advapi32 = GetModuleHandleW(L"advapi32.dll");
    AddMandatoryAce = (PADD_MANDATORY_ACE)GetProcAddress(Advapi32, "AddMandatoryAce");
    if (!AddMandatoryAce)
    {
        skip("AddMandatoryAce is unavailable, error %lu\n", GetLastError());
        return;
    }

    Status = RtlInitializeSid(LowSid, &MandatoryAuthority, 1);
    ok(Status == STATUS_SUCCESS, "RtlInitializeSid(low) returned %08lx\n", Status);
    *RtlSubAuthoritySid(LowSid, 0) = SECURITY_MANDATORY_LOW_RID;
    Status = RtlInitializeSid(WorldSid, &WorldAuthority, 1);
    ok(Status == STATUS_SUCCESS, "RtlInitializeSid(world) returned %08lx\n", Status);
    *RtlSubAuthoritySid(WorldSid, 0) = SECURITY_WORLD_RID;
    memcpy(InvalidSid, LowSid, RtlLengthSid(LowSid));
    ((PISID)InvalidSid)->Revision = 0;
    Status = RtlInitializeSid(ZeroSubauthoritySid, &MandatoryAuthority, 0);
    ok(Status == STATUS_SUCCESS, "RtlInitializeSid(zero-subauthority) returned %08lx\n", Status);
    Status = RtlInitializeSid(TwoSubauthoritySid, &MandatoryAuthority, 2);
    ok(Status == STATUS_SUCCESS, "RtlInitializeSid(two-subauthority) returned %08lx\n", Status);
    *RtlSubAuthoritySid(TwoSubauthoritySid, 0) = SECURITY_MANDATORY_LOW_RID;
    *RtlSubAuthoritySid(TwoSubauthoritySid, 1) = 1;
    Status = RtlInitializeSid(ArbitraryRidSid, &MandatoryAuthority, 1);
    ok(Status == STATUS_SUCCESS, "RtlInitializeSid(arbitrary-rid) returned %08lx\n", Status);
    *RtlSubAuthoritySid(ArbitraryRidSid, 0) = 0x1234;

    AceSize = FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE, SidStart) + RtlLengthSid(LowSid);
    ExactAclSize = (USHORT)(sizeof(ACL) + AceSize);

    TestAddMandatoryAceCase(AddMandatoryAce, "valid", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "combined-flags-and-mask", ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE, SYSTEM_MANDATORY_LABEL_VALID_MASK, LowSid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "wrong-authority", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, WorldSid, TEST_ACL_SIZE, ACL_REVISION, FALSE, ERROR_INVALID_PARAMETER, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "inherit-only", ACL_REVISION, INHERIT_ONLY_ACE, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "audit-flag", ACL_REVISION, SUCCESSFUL_ACCESS_ACE_FLAG, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, TEST_ACL_SIZE, ACL_REVISION, FALSE, ERROR_INVALID_PARAMETER, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "requested-revision-one", ACL_REVISION1, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "requested-revision-four", ACL_REVISION4, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION4);
    TestAddMandatoryAceCase(AddMandatoryAce, "requested-revision-five", ACL_REVISION4 + 1, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, TEST_ACL_SIZE, ACL_REVISION, FALSE, ERROR_REVISION_MISMATCH, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "invalid-mask", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_VALID_MASK + 1, LowSid, TEST_ACL_SIZE, ACL_REVISION, FALSE, ERROR_INVALID_PARAMETER, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "invalid-sid", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, InvalidSid, TEST_ACL_SIZE, ACL_REVISION, FALSE, ERROR_INVALID_SID, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "zero-subauthority", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, ZeroSubauthoritySid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "two-subauthorities", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, TwoSubauthoritySid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "arbitrary-rid", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, ArbitraryRidSid, TEST_ACL_SIZE, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "exact-space", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, ExactAclSize, ACL_REVISION, TRUE, ERROR_SUCCESS, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "short-space", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, ExactAclSize - sizeof(ULONG), ACL_REVISION, FALSE, ERROR_ALLOTTED_SPACE_EXCEEDED, ACL_REVISION);
    TestAddMandatoryAceCase(AddMandatoryAce, "invalid-acl-revision", ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowSid, TEST_ACL_SIZE, ACL_REVISION4 + 1, FALSE, ERROR_REVISION_MISMATCH, ACL_REVISION4 + 1);
}
