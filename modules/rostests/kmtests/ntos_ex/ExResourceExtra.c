/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended ERESOURCE coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestRecursiveExclusive(VOID)
{
    ERESOURCE Res;
    NTSTATUS Status;
    ULONG i;

    Status = ExInitializeResourceLite(&Res);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeEnterCriticalRegion();
    for (i = 0; i < 8; i++)
    {
        ok_bool_true(ExAcquireResourceExclusiveLite(&Res, TRUE), "recursive exclusive acquire");
        ok_bool_true(ExIsResourceAcquiredExclusiveLite(&Res), "held exclusive");
        ok_eq_ulong(ExGetExclusiveWaiterCount(&Res), 0LU);
    }
    ok_eq_ulong(ExIsResourceAcquiredSharedLite(&Res), 8LU);
    for (i = 0; i < 8; i++)
        ExReleaseResourceLite(&Res);
    ok_bool_false(ExIsResourceAcquiredExclusiveLite(&Res), "released exclusive");
    KeLeaveCriticalRegion();

    Status = ExDeleteResourceLite(&Res);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
VOID
TestRecursiveShared(VOID)
{
    ERESOURCE Res;
    NTSTATUS Status;
    ULONG i;

    Status = ExInitializeResourceLite(&Res);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeEnterCriticalRegion();
    for (i = 0; i < 16; i++)
        ok_bool_true(ExAcquireResourceSharedLite(&Res, TRUE), "recursive shared acquire");
    ok_eq_ulong(ExIsResourceAcquiredSharedLite(&Res), 16LU);
    ok_bool_false(ExIsResourceAcquiredExclusiveLite(&Res), "shared is not exclusive");
    ok_eq_ulong(ExGetSharedWaiterCount(&Res), 0LU);
    for (i = 0; i < 16; i++)
        ExReleaseResourceLite(&Res);
    ok_eq_ulong(ExIsResourceAcquiredSharedLite(&Res), 0LU);
    KeLeaveCriticalRegion();

    ExDeleteResourceLite(&Res);
}

static
VOID
TestSharedToExclusiveStarve(VOID)
{
    ERESOURCE Res;
    NTSTATUS Status;

    Status = ExInitializeResourceLite(&Res);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeEnterCriticalRegion();
    ok_bool_true(ExAcquireResourceSharedLite(&Res, TRUE), "shared acquire");
    ok_bool_true(ExAcquireSharedStarveExclusive(&Res, TRUE), "starve-exclusive shared acquire");
    ok_eq_ulong(ExIsResourceAcquiredSharedLite(&Res), 2LU);
    ExReleaseResourceLite(&Res);
    ExReleaseResourceLite(&Res);
    KeLeaveCriticalRegion();

    ExDeleteResourceLite(&Res);
}

static
VOID
TestReinitIntegrity(VOID)
{
    ERESOURCE Res;
    NTSTATUS Status;
    ULONG i;

    Status = ExInitializeResourceLite(&Res);
    ok_eq_hex(Status, STATUS_SUCCESS);

    for (i = 0; i < 4; i++)
    {
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&Res, TRUE);
        ok_eq_ulong(ExGetExclusiveWaiterCount(&Res), 0LU);
        ok_eq_ulong(ExGetSharedWaiterCount(&Res), 0LU);
        ExReleaseResourceLite(&Res);
        KeLeaveCriticalRegion();

        Status = ExReinitializeResourceLite(&Res);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_bool_false(ExIsResourceAcquiredExclusiveLite(&Res), "clean after reinit");
        ok_eq_ulong(ExIsResourceAcquiredSharedLite(&Res), 0LU);
    }

    ExDeleteResourceLite(&Res);
}

START_TEST(ExResourceExtra)
{
    TestRecursiveExclusive();
    TestRecursiveShared();
    TestSharedToExclusiveStarve();
    TestReinitIntegrity();
}
