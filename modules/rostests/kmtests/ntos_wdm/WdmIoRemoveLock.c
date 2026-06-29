/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     WDM remove-lock behavior tests
 */

#include <kmt_test.h>

#define TAG_REMOVE_LOCK 'LRmK'

static
VOID
TestRemoveLockLifecycle(VOID)
{
    IO_REMOVE_LOCK Lock;
    NTSTATUS Status;
    PVOID Tag1 = (PVOID)(ULONG_PTR)0x11111111;
    PVOID Tag2 = (PVOID)(ULONG_PTR)0x22222222;

    RtlFillMemory(&Lock, sizeof(Lock), 0x55);
    IoInitializeRemoveLock(&Lock, TAG_REMOVE_LOCK, 0, 0);

    ok_bool_false(Lock.Common.Removed, "Remove lock started as removed");
    ok_eq_long(Lock.Common.IoCount, 1);

    Status = IoAcquireRemoveLock(&Lock, Tag1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Lock.Common.IoCount, 2);

    IoReleaseRemoveLock(&Lock, Tag1);
    ok_bool_false(Lock.Common.Removed, "Remove lock became removed after release");
    ok_eq_long(Lock.Common.IoCount, 1);

    Status = IoAcquireRemoveLock(&Lock, Tag1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = IoAcquireRemoveLock(&Lock, Tag2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Lock.Common.IoCount, 3);

    IoReleaseRemoveLock(&Lock, Tag1);
    ok_eq_long(Lock.Common.IoCount, 2);

    IoReleaseRemoveLockAndWait(&Lock, Tag2);
    ok_bool_true(Lock.Common.Removed, "Remove lock was not marked removed");

    Status = IoAcquireRemoveLock(&Lock, Tag1);
    ok_eq_hex(Status, STATUS_DELETE_PENDING);
}

START_TEST(WdmIoRemoveLock)
{
    TestRemoveLockLifecycle();
}
