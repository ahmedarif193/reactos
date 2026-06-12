/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ObGetObjectSecurity API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(ObSecurityDescKM)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    BOOLEAN MemoryAllocated = FALSE;
    NTSTATUS Status;
    BOOLEAN Present, Defaulted;
    PACL Dacl;
    PSID Owner;

    Status = ObGetObjectSecurity(PsGetCurrentProcess(), &Sd, &MemoryAllocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(Sd != NULL, "no security descriptor\n");

    if (Sd != NULL)
    {
        ok_bool_true(RtlValidSecurityDescriptor(Sd), "SD valid");

        Dacl = NULL;
        Present = FALSE;
        Status = RtlGetDaclSecurityDescriptor(Sd, &Present, &Dacl, &Defaulted);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_bool_true(Present, "process dacl present");

        Owner = NULL;
        Status = RtlGetOwnerSecurityDescriptor(Sd, &Owner, &Defaulted);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Owner != NULL, "process owner NULL\n");
        if (Owner != NULL)
            ok_bool_true(RtlValidSid(Owner), "owner sid valid");

        ObReleaseObjectSecurity(Sd, MemoryAllocated);
    }
}
