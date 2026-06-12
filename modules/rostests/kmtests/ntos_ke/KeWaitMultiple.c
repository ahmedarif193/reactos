/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite KeWaitForMultipleObjects API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestWaitAny(VOID)
{
    KEVENT Events[3];
    PVOID Objects[3];
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG i;

    for (i = 0; i < 3; i++)
    {
        KeInitializeEvent(&Events[i], SynchronizationEvent, FALSE);
        Objects[i] = &Events[i];
    }

    Timeout.QuadPart = 0;
    Status = KeWaitForMultipleObjects(3, Objects, WaitAny, Executive, KernelMode, FALSE, &Timeout, NULL);
    ok_eq_hex(Status, STATUS_TIMEOUT);

    KeSetEvent(&Events[1], IO_NO_INCREMENT, FALSE);
    Status = KeWaitForMultipleObjects(3, Objects, WaitAny, Executive, KernelMode, FALSE, &Timeout, NULL);
    ok_eq_hex(Status, STATUS_WAIT_1);
    ok_eq_long(KeReadStateEvent(&Events[1]), 0L);

    KeSetEvent(&Events[0], IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Events[2], IO_NO_INCREMENT, FALSE);
    Status = KeWaitForMultipleObjects(3, Objects, WaitAny, Executive, KernelMode, FALSE, &Timeout, NULL);
    ok_eq_hex(Status, STATUS_WAIT_0);
    ok_eq_long(KeReadStateEvent(&Events[0]), 0L);
    ok_eq_long(KeReadStateEvent(&Events[2]), 1L);
}

static
VOID
TestWaitAll(VOID)
{
    KEVENT Events[2];
    KSEMAPHORE Semaphore;
    PVOID Objects[3];
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    KeInitializeEvent(&Events[0], SynchronizationEvent, TRUE);
    KeInitializeEvent(&Events[1], NotificationEvent, FALSE);
    KeInitializeSemaphore(&Semaphore, 1, 10);
    Objects[0] = &Events[0];
    Objects[1] = &Events[1];
    Objects[2] = &Semaphore;

    Timeout.QuadPart = 0;
    Status = KeWaitForMultipleObjects(3, Objects, WaitAll, Executive, KernelMode, FALSE, &Timeout, NULL);
    ok_eq_hex(Status, STATUS_TIMEOUT);
    ok_eq_long(KeReadStateEvent(&Events[0]), 1L);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 1L);

    KeSetEvent(&Events[1], IO_NO_INCREMENT, FALSE);
    Status = KeWaitForMultipleObjects(3, Objects, WaitAll, Executive, KernelMode, FALSE, &Timeout, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(KeReadStateEvent(&Events[0]), 0L);
    ok_eq_long(KeReadStateEvent(&Events[1]), 1L);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 0L);
}

static
VOID
TestWaitArray(VOID)
{
    PKEVENT Events;
    PVOID Objects[8];
    PKWAIT_BLOCK WaitBlocks;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG i;

    Events = ExAllocatePoolWithTag(NonPagedPool, 8 * sizeof(KEVENT), 'tWmK');
    WaitBlocks = ExAllocatePoolWithTag(NonPagedPool, 8 * sizeof(KWAIT_BLOCK), 'tWmK');
    ok(Events != NULL, "no pool for events\n");
    ok(WaitBlocks != NULL, "no pool for wait blocks\n");
    if (Events == NULL || WaitBlocks == NULL) goto cleanup;

    for (i = 0; i < 8; i++)
    {
        KeInitializeEvent(&Events[i], SynchronizationEvent, TRUE);
        Objects[i] = &Events[i];
    }

    Timeout.QuadPart = 0;
    Status = KeWaitForMultipleObjects(8, Objects, WaitAll, Executive, KernelMode, FALSE, &Timeout, WaitBlocks);
    ok_eq_hex(Status, STATUS_SUCCESS);
    for (i = 0; i < 8; i++)
        ok_eq_long(KeReadStateEvent(&Events[i]), 0L);

    Status = KeWaitForMultipleObjects(8, Objects, WaitAny, Executive, KernelMode, FALSE, &Timeout, WaitBlocks);
    ok_eq_hex(Status, STATUS_TIMEOUT);

cleanup:
    if (Events) ExFreePoolWithTag(Events, 'tWmK');
    if (WaitBlocks) ExFreePoolWithTag(WaitBlocks, 'tWmK');
}

START_TEST(KeWaitMultiple)
{
    TestWaitAny();
    TestWaitAll();
    TestWaitArray();
}
