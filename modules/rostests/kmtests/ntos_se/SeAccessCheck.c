/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite SeAccessCheck API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'cAmK'

static
VOID
TestAccessCheck(VOID)
{
    SECURITY_DESCRIPTOR Sd;
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PACL Dacl;
    ULONG DaclSize;
    NTSTATUS Status, AccessStatus;
    BOOLEAN Granted;
    ACCESS_MASK GrantedAccess;
    GENERIC_MAPPING Mapping;
    PSID WorldSid = SeExports->SeWorldSid;
    PSID AdminsSid = SeExports->SeAliasAdminsSid;

    ok(SeExports != NULL, "SeExports NULL\n");
    ok(WorldSid != NULL, "SeWorldSid NULL\n");

    Status = RtlCreateSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);

    DaclSize = sizeof(ACL) + 2 * (sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE);
    Dacl = ExAllocatePoolWithTag(PagedPool, DaclSize, TAG_TEST);
    ok(Dacl != NULL, "no pool for dacl\n");
    if (Dacl == NULL) return;

    Status = RtlCreateAcl(Dacl, DaclSize, ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, FILE_GENERIC_READ, WorldSid);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlSetDaclSecurityDescriptor(&Sd, TRUE, Dacl, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlSetOwnerSecurityDescriptor(&Sd, AdminsSid, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlSetGroupSecurityDescriptor(&Sd, WorldSid, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Mapping.GenericRead = FILE_GENERIC_READ;
    Mapping.GenericWrite = FILE_GENERIC_WRITE;
    Mapping.GenericExecute = FILE_GENERIC_EXECUTE;
    Mapping.GenericAll = FILE_ALL_ACCESS;

    SeCaptureSubjectContext(&SubjectContext);
    SeLockSubjectContext(&SubjectContext);

    GrantedAccess = 0;
    AccessStatus = STATUS_PENDING;
    Granted = SeAccessCheck(&Sd, &SubjectContext, TRUE, FILE_READ_DATA, 0, NULL, &Mapping, KernelMode, &GrantedAccess, &AccessStatus);
    ok_bool_true(Granted, "kernel-mode read");
    ok_eq_hex(AccessStatus, STATUS_SUCCESS);
    ok_eq_hex(GrantedAccess, FILE_READ_DATA);

    Granted = SeAccessCheck(&Sd, &SubjectContext, TRUE, FILE_WRITE_DATA, 0, NULL, &Mapping, KernelMode, &GrantedAccess, &AccessStatus);
    ok_bool_true(Granted, "kernel-mode write bypasses dacl");

    Granted = SeAccessCheck(&Sd, &SubjectContext, TRUE, FILE_READ_DATA, 0, NULL, &Mapping, UserMode, &GrantedAccess, &AccessStatus);
    ok_bool_true(Granted, "user-mode read allowed by world ace");
    if (Granted)
    {
        ok_eq_hex(AccessStatus, STATUS_SUCCESS);
        ok_eq_hex(GrantedAccess, FILE_READ_DATA);
    }

    Granted = SeAccessCheck(&Sd, &SubjectContext, TRUE, FILE_WRITE_DATA, 0, NULL, &Mapping, UserMode, &GrantedAccess, &AccessStatus);
    ok_bool_false(Granted, "user-mode write denied");
    if (!Granted) ok_eq_hex(AccessStatus, STATUS_ACCESS_DENIED);

    SeUnlockSubjectContext(&SubjectContext);
    SeReleaseSubjectContext(&SubjectContext);

    ExFreePoolWithTag(Dacl, TAG_TEST);
}

static
VOID
TestPrivilegeCheck(VOID)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PRIVILEGE_SET PrivilegeSet;
    BOOLEAN Result;

    SeCaptureSubjectContext(&SubjectContext);

    PrivilegeSet.PrivilegeCount = 1;
    PrivilegeSet.Control = PRIVILEGE_SET_ALL_NECESSARY;
    PrivilegeSet.Privilege[0].Luid = SeExports->SeTcbPrivilege;
    PrivilegeSet.Privilege[0].Attributes = 0;

    Result = SePrivilegeCheck(&PrivilegeSet, &SubjectContext, KernelMode);
    ok_bool_true(Result, "tcb privilege in kernel mode");

    SeReleaseSubjectContext(&SubjectContext);
}

static
VOID
TestTokenQueries(VOID)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PACCESS_TOKEN Token;

    SeCaptureSubjectContext(&SubjectContext);
    Token = SeQuerySubjectContextToken(&SubjectContext);
    ok(Token != NULL, "no token in subject context\n");

    if (Token != NULL)
    {
        ok_bool_true(SeTokenIsAdmin(Token), "service token is admin");
    }

    SeReleaseSubjectContext(&SubjectContext);
}

START_TEST(SeAccessCheckKM)
{
    TestAccessCheck();
    TestPrivilegeCheck();
    TestTokenQueries();
}
