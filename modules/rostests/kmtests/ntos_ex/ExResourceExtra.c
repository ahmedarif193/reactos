/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended ERESOURCE coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

/* Windows 11 26100 WDK layout, corroborated by the ARM64 kernel symbols. */
#ifdef _WIN64
C_ASSERT(sizeof(ERESOURCE) == 0x68);
C_ASSERT(sizeof(OWNER_ENTRY) == 0x10);
C_ASSERT(FIELD_OFFSET(ERESOURCE, Flag) == 0x1a);
C_ASSERT(FIELD_OFFSET(ERESOURCE, OwnerEntry) == 0x30);
C_ASSERT(FIELD_OFFSET(ERESOURCE, MiscFlags) == 0x50);
C_ASSERT(FIELD_OFFSET(ERESOURCE, ResourceTimeoutCount) == 0x54);
C_ASSERT(FIELD_OFFSET(ERESOURCE, SpinLock) == 0x60);
#else
C_ASSERT(sizeof(ERESOURCE) == 0x38);
C_ASSERT(sizeof(OWNER_ENTRY) == 0x8);
C_ASSERT(FIELD_OFFSET(ERESOURCE, Flag) == 0xe);
C_ASSERT(FIELD_OFFSET(ERESOURCE, OwnerEntry) == 0x18);
C_ASSERT(FIELD_OFFSET(ERESOURCE, SpinLock) == 0x34);
#endif

static
VOID
TestOwnerEntryLayout(VOID)
{
    OWNER_ENTRY Owner = {0};
    ERESOURCE Res;
    RTL_OSVERSIONINFOW Version = {sizeof(Version)};
    NTSTATUS Status;

    Owner.IoPriorityBoosted = 1;
    ok_eq_ulong(Owner.TableSize, 1UL);
    Owner.OwnerReferenced = 1;
    ok_eq_ulong(Owner.TableSize, 3UL);
    Owner.IoQoSPriorityBoosted = 1;
    ok_eq_ulong(Owner.TableSize, 7UL);
    Owner.OwnerCount = 1;
    ok_eq_ulong(Owner.TableSize, 15UL);
    Owner.OwnerCount = 0x1fffffff;
    ok_eq_ulong(Owner.TableSize, MAXULONG);

    /* Older Windows kernels use a different owner-count bit position. */
    Status = RtlGetVersion(&Version);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || Version.dwBuildNumber < 26100)
    {
        skip(FALSE, "Live OWNER_ENTRY layout requires Windows build 26100 or later\n");
        return;
    }

    Status = ExInitializeResourceLite(&Res);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    KeEnterCriticalRegion();
    if (ExAcquireResourceExclusiveLite(&Res, FALSE))
    {
        ok_eq_ulong(Res.OwnerEntry.TableSize, 1UL << 3);
        ok_eq_ulong(Res.OwnerEntry.OwnerCount, 1UL);
        ok_bool_true(ExIsResourceAcquiredExclusiveLite(&Res), "held exclusive");
        ExReleaseResourceLite(&Res);
        ok_eq_ulong(Res.OwnerEntry.TableSize, 0UL);
    }
    else
    {
        ok(FALSE, "Cannot acquire a newly initialized resource\n");
    }
    KeLeaveCriticalRegion();

    Status = ExDeleteResourceLite(&Res);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

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
    TestOwnerEntryLayout();
    TestRecursiveExclusive();
    TestRecursiveShared();
    TestSharedToExclusiveStarve();
    TestReinitIntegrity();
}
