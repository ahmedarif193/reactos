/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite KQUEUE API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef struct _QUEUE_DIRECT_HANDOFF_CONTEXT
{
    KQUEUE Queue;
    LIST_ENTRY Entries[2];
    KEVENT Completed[2];
    PLIST_ENTRY Results[2];
    ULONG ActiveCounts[2];
} QUEUE_DIRECT_HANDOFF_CONTEXT, *PQUEUE_DIRECT_HANDOFF_CONTEXT;

static
BOOLEAN
WaitForQueueWaiter(
    _In_ PKQUEUE Queue)
{
    LARGE_INTEGER Delay;
    ULONG Attempt;

    Delay.QuadPart = -10 * 1000;
    for (Attempt = 0; Attempt < 5000; Attempt++)
    {
        KeMemoryBarrier();
        if (Queue->Header.WaitListHead.Flink != &Queue->Header.WaitListHead)
            return TRUE;

        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    return FALSE;
}

static
VOID
NTAPI
QueueDirectHandoffThread(
    _In_ PVOID Parameter)
{
    PQUEUE_DIRECT_HANDOFF_CONTEXT Context = Parameter;
    LARGE_INTEGER Timeout;
    ULONG Index;

    Timeout.QuadPart = -5LL * 10 * 1000 * 1000;
    for (Index = 0; Index < RTL_NUMBER_OF(Context->Entries); Index++)
    {
        Context->Results[Index] = KeRemoveQueue(&Context->Queue, KernelMode, &Timeout);
        Context->ActiveCounts[Index] = Context->Queue.CurrentCount;
        KeSetEvent(&Context->Completed[Index], IO_NO_INCREMENT, FALSE);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
TestQueueOrder(VOID)
{
    KQUEUE Queue;
    LIST_ENTRY Entries[4];
    PLIST_ENTRY Entry;
    LARGE_INTEGER Timeout;
    LONG State;
    ULONG i;

    KeInitializeQueue(&Queue, 0);
    ok_eq_long(KeReadStateQueue(&Queue), 0L);

    for (i = 0; i < 4; i++)
    {
        State = KeInsertQueue(&Queue, &Entries[i]);
        ok_eq_long(State, (LONG)i);
    }
    ok_eq_long(KeReadStateQueue(&Queue), 4L);

    for (i = 0; i < 4; i++)
    {
        Entry = KeRemoveQueue(&Queue, KernelMode, NULL);
        ok_eq_pointer(Entry, &Entries[i]);
    }
    ok_eq_long(KeReadStateQueue(&Queue), 0L);

    Timeout.QuadPart = 0;
    Entry = KeRemoveQueue(&Queue, KernelMode, &Timeout);
    ok_eq_pointer(Entry, (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT);

    KeRundownQueue(&Queue);
}

static
VOID
TestQueueHeadInsert(VOID)
{
    KQUEUE Queue;
    LIST_ENTRY Entries[3];
    PLIST_ENTRY Entry;
    LONG State;

    KeInitializeQueue(&Queue, 0);
    State = KeInsertQueue(&Queue, &Entries[0]);
    ok_eq_long(State, 0L);
    State = KeInsertHeadQueue(&Queue, &Entries[1]);
    ok_eq_long(State, 1L);
    State = KeInsertQueue(&Queue, &Entries[2]);
    ok_eq_long(State, 2L);

    Entry = KeRemoveQueue(&Queue, KernelMode, NULL);
    ok_eq_pointer(Entry, &Entries[1]);
    Entry = KeRemoveQueue(&Queue, KernelMode, NULL);
    ok_eq_pointer(Entry, &Entries[0]);
    Entry = KeRemoveQueue(&Queue, KernelMode, NULL);
    ok_eq_pointer(Entry, &Entries[2]);

    KeRundownQueue(&Queue);
}

static
VOID
TestQueueRundown(VOID)
{
    KQUEUE Queue;
    LIST_ENTRY Entries[2];
    PLIST_ENTRY First;

    KeInitializeQueue(&Queue, 0);
    KeInsertQueue(&Queue, &Entries[0]);
    KeInsertQueue(&Queue, &Entries[1]);

    First = KeRundownQueue(&Queue);
    ok(First != NULL, "KeRundownQueue returned NULL for non-empty queue\n");
    if (First != NULL)
    {
        ok_eq_pointer(First, &Entries[0]);
        ok_eq_pointer(First->Flink, &Entries[1]);
        ok_eq_pointer(Entries[1].Flink, First);
    }

    KeInitializeQueue(&Queue, 0);
    First = KeRundownQueue(&Queue);
    ok_eq_pointer(First, NULL);
}

static
VOID
TestQueueDirectHandoffAccounting(VOID)
{
    QUEUE_DIRECT_HANDOFF_CONTEXT Context;
    LARGE_INTEGER Timeout;
    HANDLE ThreadHandle;
    PVOID ThreadObject = NULL;
    NTSTATUS Status;
    BOOLEAN WaiterVisible;
    ULONG Index;

    RtlZeroMemory(&Context, sizeof(Context));
    KeInitializeQueue(&Context.Queue, 1);
    for (Index = 0; Index < RTL_NUMBER_OF(Context.Completed); Index++)
        KeInitializeEvent(&Context.Completed[Index], NotificationEvent, FALSE);

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, QueueDirectHandoffThread, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObCloseHandle(ThreadHandle, KernelMode);

    Timeout.QuadPart = -10LL * 10 * 1000 * 1000;
    for (Index = 0; Index < RTL_NUMBER_OF(Context.Entries); Index++)
    {
        WaiterVisible = WaitForQueueWaiter(&Context.Queue);
        ok(WaiterVisible, "Queue waiter %lu was not published\n", Index);

        KeInsertQueue(&Context.Queue, &Context.Entries[Index]);
        Status = KeWaitForSingleObject(&Context.Completed[Index], Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
            break;

        ok_eq_pointer(Context.Results[Index], &Context.Entries[Index]);
        ok_eq_ulong(Context.ActiveCounts[Index], 1UL);
    }

    if (ThreadObject != NULL)
    {
        Status = KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ObDereferenceObject(ThreadObject);
    }

    KeRundownQueue(&Context.Queue);
}

START_TEST(KeQueue)
{
    TestQueueOrder();
    TestQueueHeadInsert();
    TestQueueRundown();
    TestQueueDirectHandoffAccounting();
}
