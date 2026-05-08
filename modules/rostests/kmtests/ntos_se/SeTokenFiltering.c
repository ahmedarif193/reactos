/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel mode tests for token filtering implementation
 * COPYRIGHT:   Copyright 2021 George Bișoc <george.bisoc@reactos.org>
 */

#include <kmt_test.h>
#include <ntifs.h>

static
VOID
FilterToken(VOID)
{
    NTSTATUS Status;
    PSECURITY_SUBJECT_CONTEXT SubjectContext;
    PACCESS_TOKEN Token, FilteredToken = NULL;
    TOKEN_GROUPS SidsToDisable, RestrictedGroups;
    TOKEN_PRIVILEGES Privilege;

    /* Capture the subject context and token for tests */
    SubjectContext = ExAllocatePool(PagedPool, sizeof(SECURITY_SUBJECT_CONTEXT));
    if (SubjectContext == NULL)
    {
        trace("Failed to allocate memory pool for the subject context!\n");
        return;
    }

    SeCaptureSubjectContext(SubjectContext);
    SeLockSubjectContext(SubjectContext);
    Token = SeQuerySubjectContextToken(SubjectContext);
    ok(Token != NULL, "Token mustn't be NULL...\n");

    /* Delete a privilege */
    Privilege.PrivilegeCount = 1;
    Privilege.Privileges[0].Attributes = 0;
    Privilege.Privileges[0].Luid = SeExports->SeSystemEnvironmentPrivilege;

    Status = SeFilterToken(Token,
                           0,
                           NULL,
                           &Privilege,
                           NULL,
                           &FilteredToken);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(FilteredToken);

    /* Disable all the privileges */
    FilteredToken = NULL;
    Status = SeFilterToken(Token,
                           DISABLE_MAX_PRIVILEGE,
                           NULL,
                           NULL,
                           NULL,
                           &FilteredToken);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(FilteredToken);

    /* Disable a SID */
    SidsToDisable.GroupCount = 1;
    SidsToDisable.Groups[0].Attributes = 0;
    SidsToDisable.Groups[0].Sid = SeExports->SeWorldSid;

    FilteredToken = NULL;
    Status = SeFilterToken(Token,
                           0,
                           &SidsToDisable,
                           NULL,
                           NULL,
                           &FilteredToken);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(FilteredToken);

    /*
     * Add a restricted SID but we're going to fail...
     * Because no attributes must be within restricted
     * SIDs.
     */
    RestrictedGroups.GroupCount = 1;
    RestrictedGroups.Groups[0].Attributes = SE_GROUP_ENABLED;
    RestrictedGroups.Groups[0].Sid = SeExports->SeDialupSid;

    FilteredToken = NULL;
    Status = SeFilterToken(Token,
                           0,
                           NULL,
                           NULL,
                           &RestrictedGroups,
                           &FilteredToken);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    if (skip(GetNTVersion() >= _WIN32_WINNT_VISTA,
             "Restricted SID SeFilterToken probe poisons subsequent process creation on NT 5.x\n"))
        goto done;

    /* Add a restricted SID now */
    RestrictedGroups.GroupCount = 1;
    RestrictedGroups.Groups[0].Attributes = 0;
    RestrictedGroups.Groups[0].Sid = SeExports->SeDialupSid;

    FilteredToken = NULL;
    Status = SeFilterToken(Token,
                           0,
                           NULL,
                           NULL,
                           &RestrictedGroups,
                           &FilteredToken);
    ok_irql(PASSIVE_LEVEL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(FilteredToken);

done:
    /* We're done */
    SeUnlockSubjectContext(SubjectContext);
    SeReleaseSubjectContext(SubjectContext);
    if (SubjectContext)
        ExFreePool(SubjectContext);
}

START_TEST(SeTokenFiltering)
{
    FilterToken();
}
