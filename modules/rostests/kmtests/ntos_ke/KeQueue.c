/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite KQUEUE API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#ifdef _M_ARM64
typedef ULONG (NTAPI *PKMT_KE_REMOVE_QUEUE_EX)(PKQUEUE, KPROCESSOR_MODE, BOOLEAN, PLARGE_INTEGER, PLIST_ENTRY *, ULONG);

typedef struct _QUEUE_EX_WAIT_CONTEXT
{
    PKMT_KE_REMOVE_QUEUE_EX RemoveQueueEx;
    KQUEUE Queue;
    LIST_ENTRY Entries[3];
    PLIST_ENTRY Output[3];
    KEVENT Completed;
    ULONG Removed;
    ULONG CurrentCount;
} QUEUE_EX_WAIT_CONTEXT, *PQUEUE_EX_WAIT_CONTEXT;
#endif

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

#ifdef _M_ARM64

static VOID NTAPI QueueExWaitThread(_In_ PVOID Parameter)
{
    PQUEUE_EX_WAIT_CONTEXT Context = Parameter;

    KeSetSystemAffinityThread((KAFFINITY)1 << 1);
    Context->Removed = Context->RemoveQueueEx(&Context->Queue, KernelMode, FALSE, NULL, Context->Output, RTL_NUMBER_OF(Context->Output));
    Context->CurrentCount = Context->Queue.CurrentCount;
    KeSetEvent(&Context->Completed, IO_NO_INCREMENT, FALSE);
    KeRevertToUserAffinityThread();
}

#endif

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
    ok_eq_long(KeReadStateQueue(&Queue), 0L);
    ok_bool_true(IsListEmpty(&Queue.EntryListHead), "Rundown queue head should be empty");

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

#ifdef _M_ARM64

static VOID TestQueueRemoveEx(VOID)
{
    PKMT_KE_REMOVE_QUEUE_EX RemoveQueueEx;
    KQUEUE Queue;
    LIST_ENTRY Entries[5];
    PLIST_ENTRY Output[6];
    PLIST_ENTRY PoisonPointer = (PLIST_ENTRY)(ULONG_PTR)0x5555555555555555ULL;
    LARGE_INTEGER Timeout;
    BOOLEAN InterruptsBefore;
    BOOLEAN InterruptsAfter;
    KIRQL OldIrql;
    ULONG Removed;
    ULONG Index;

    RemoveQueueEx = (PKMT_KE_REMOVE_QUEUE_EX)KmtGetSystemRoutineAddress(L"KeRemoveQueueEx");
    ok(RemoveQueueEx != NULL, "KeRemoveQueueEx is not exported\n");
    if (!RemoveQueueEx)
        return;

    KeInitializeQueue(&Queue, 1);
    for (Index = 0; Index < RTL_NUMBER_OF(Entries); Index++)
        KeInsertQueue(&Queue, &Entries[Index]);
    for (Index = 0; Index < RTL_NUMBER_OF(Output); Index++)
        Output[Index] = PoisonPointer;

    Timeout.QuadPart = 0;
    Removed = RemoveQueueEx(&Queue, KernelMode, FALSE, &Timeout, Output, 3);
    trace("[KeQueue][Ex] first removed=%lu state=%ld current=%lu out=%p,%p,%p,%p\n", Removed, KeReadStateQueue(&Queue), Queue.CurrentCount, Output[0], Output[1], Output[2], Output[3]);
    ok_eq_ulong(Removed, 3);
    ok_eq_pointer(Output[0], &Entries[0]);
    ok_eq_pointer(Output[1], &Entries[1]);
    ok_eq_pointer(Output[2], &Entries[2]);
    ok_eq_pointer(Output[3], PoisonPointer);
    ok_eq_pointer(Entries[0].Flink, NULL);
    ok_eq_pointer(Entries[1].Flink, NULL);
    ok_eq_pointer(Entries[2].Flink, NULL);
    ok_eq_long(KeReadStateQueue(&Queue), 2);
    ok_eq_ulong(Queue.CurrentCount, 1);

    for (Index = 0; Index < RTL_NUMBER_OF(Output); Index++)
        Output[Index] = PoisonPointer;
    Removed = RemoveQueueEx(&Queue, KernelMode, TRUE, &Timeout, Output, 5);
    trace("[KeQueue][Ex] partial removed=%lu state=%ld current=%lu out=%p,%p,%p\n", Removed, KeReadStateQueue(&Queue), Queue.CurrentCount, Output[0], Output[1], Output[2]);
    ok_eq_ulong(Removed, 2);
    ok_eq_pointer(Output[0], &Entries[3]);
    ok_eq_pointer(Output[1], &Entries[4]);
    ok_eq_pointer(Output[2], PoisonPointer);
    ok_eq_pointer(Entries[3].Flink, NULL);
    ok_eq_pointer(Entries[4].Flink, NULL);
    ok_eq_long(KeReadStateQueue(&Queue), 0);
    ok_eq_ulong(Queue.CurrentCount, 1);

    for (Index = 0; Index < RTL_NUMBER_OF(Output); Index++)
        Output[Index] = PoisonPointer;
    Removed = RemoveQueueEx(&Queue, KernelMode, FALSE, &Timeout, Output, 3);
    trace("[KeQueue][Ex] empty removed=%lu state=%ld current=%lu out=%p,%p\n", Removed, KeReadStateQueue(&Queue), Queue.CurrentCount, Output[0], Output[1]);
    ok_eq_ulong(Removed, 1);
    ok_eq_pointer(Output[0], (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT);
    ok_eq_pointer(Output[1], PoisonPointer);
    ok_eq_long(KeReadStateQueue(&Queue), 0);
    ok_eq_ulong(Queue.CurrentCount, 1);
    KeRundownQueue(&Queue);

    KeInitializeQueue(&Queue, 1);
    for (Index = 0; Index < RTL_NUMBER_OF(Output); Index++)
        Output[Index] = PoisonPointer;
    Timeout.QuadPart = -10 * 1000 * 20;
    Removed = RemoveQueueEx(&Queue, KernelMode, FALSE, &Timeout, Output, 3);
    trace("[KeQueue][Ex] timed removed=%lu state=%ld current=%lu out=%p,%p\n", Removed, KeReadStateQueue(&Queue), Queue.CurrentCount, Output[0], Output[1]);
    ok_eq_ulong(Removed, 1);
    ok_eq_pointer(Output[0], (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT);
    ok_eq_pointer(Output[1], PoisonPointer);
    ok_eq_long(KeReadStateQueue(&Queue), 0);
    ok_eq_ulong(Queue.CurrentCount, 1);
    KeRundownQueue(&Queue);

    KeInitializeQueue(&Queue, 1);
    KeInsertQueue(&Queue, &Entries[0]);
    for (Index = 0; Index < RTL_NUMBER_OF(Output); Index++)
        Output[Index] = PoisonPointer;
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    InterruptsBefore = KmtAreInterruptsEnabled();
    Removed = RemoveQueueEx(&Queue, KernelMode, FALSE, &Timeout, Output, 2);
    InterruptsAfter = KmtAreInterruptsEnabled();
    KeLowerIrql(OldIrql);
    trace("[KeQueue][Ex] dispatch-entry removed=%lu state=%ld current=%lu irq=%u/%u out=%p,%p\n", Removed, KeReadStateQueue(&Queue), Queue.CurrentCount, InterruptsBefore, InterruptsAfter, Output[0], Output[1]);
    ok_eq_ulong(Removed, 1);
    ok_eq_pointer(Output[0], &Entries[0]);
    ok_eq_pointer(Output[1], PoisonPointer);
    ok_eq_uint(InterruptsBefore, InterruptsAfter);

    for (Index = 0; Index < RTL_NUMBER_OF(Output); Index++)
        Output[Index] = PoisonPointer;
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    InterruptsBefore = KmtAreInterruptsEnabled();
    Removed = RemoveQueueEx(&Queue, KernelMode, TRUE, &Timeout, Output, 2);
    InterruptsAfter = KmtAreInterruptsEnabled();
    KeLowerIrql(OldIrql);
    trace("[KeQueue][Ex] dispatch-empty removed=%lu state=%ld current=%lu irq=%u/%u out=%p,%p\n", Removed, KeReadStateQueue(&Queue), Queue.CurrentCount, InterruptsBefore, InterruptsAfter, Output[0], Output[1]);
    ok_eq_ulong(Removed, 1);
    ok_eq_pointer(Output[0], (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT);
    ok_eq_pointer(Output[1], PoisonPointer);
    ok_eq_uint(InterruptsBefore, InterruptsAfter);
    ok_eq_long(KeReadStateQueue(&Queue), 0);
    ok_eq_ulong(Queue.CurrentCount, 1);
    KeRundownQueue(&Queue);
}

static VOID TestQueueRemoveExDirectHandoff(_In_ PKMT_KE_REMOVE_QUEUE_EX RemoveQueueEx)
{
    QUEUE_EX_WAIT_CONTEXT Context;
    PKTHREAD WaitThread;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    BOOLEAN WaiterVisible;
    KIRQL OldIrql;
    ULONG Index;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "Single CPU -- skipping KeRemoveQueueEx direct handoff\n");
        return;
    }

    RtlZeroMemory(&Context, sizeof(Context));
    Context.RemoveQueueEx = RemoveQueueEx;
    KeInitializeQueue(&Context.Queue, 1);
    KeInitializeEvent(&Context.Completed, NotificationEvent, FALSE);
    KeSetSystemAffinityThread((KAFFINITY)1 << 1);
    WaitThread = KmtStartThread(QueueExWaitThread, &Context);
    if (!WaitThread)
    {
        KeRevertToUserAffinityThread();
        return;
    }

    WaiterVisible = WaitForQueueWaiter(&Context.Queue);
    ok_bool_true(WaiterVisible, "KeRemoveQueueEx waiter should be published");
    if (!WaiterVisible)
    {
        KeInsertQueue(&Context.Queue, &Context.Entries[0]);
        KmtFinishThread(WaitThread, NULL);
        KeRevertToUserAffinityThread();
        KeRundownQueue(&Context.Queue);
        return;
    }

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    for (Index = 0; Index < RTL_NUMBER_OF(Context.Entries); Index++)
        KeInsertQueue(&Context.Queue, &Context.Entries[Index]);
    KeLowerIrql(OldIrql);

    Timeout.QuadPart = -5LL * 10 * 1000 * 1000;
    Status = KeWaitForSingleObject(&Context.Completed, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    KmtFinishThread(WaitThread, NULL);
    trace("[KeQueue][Ex] handoff removed=%lu state=%ld current=%lu out=%p,%p,%p\n", Context.Removed, KeReadStateQueue(&Context.Queue), Context.CurrentCount, Context.Output[0], Context.Output[1], Context.Output[2]);
    ok_eq_ulong(Context.Removed, RTL_NUMBER_OF(Context.Entries));
    ok_eq_pointer(Context.Output[0], &Context.Entries[0]);
    ok_eq_pointer(Context.Output[1], &Context.Entries[1]);
    ok_eq_pointer(Context.Output[2], &Context.Entries[2]);
    ok_eq_pointer(Context.Entries[0].Flink, NULL);
    ok_eq_pointer(Context.Entries[1].Flink, NULL);
    ok_eq_pointer(Context.Entries[2].Flink, NULL);
    ok_eq_long(KeReadStateQueue(&Context.Queue), 0);
    ok_eq_ulong(Context.CurrentCount, 1);
    KeRevertToUserAffinityThread();
    KeRundownQueue(&Context.Queue);
}

#endif

START_TEST(KeQueue)
{
    TestQueueOrder();
    TestQueueHeadInsert();
    TestQueueRundown();
    TestQueueDirectHandoffAccounting();
#ifdef _M_ARM64
    TestQueueRemoveEx();
    {
        PKMT_KE_REMOVE_QUEUE_EX RemoveQueueEx = (PKMT_KE_REMOVE_QUEUE_EX)KmtGetSystemRoutineAddress(L"KeRemoveQueueEx");
        if (RemoveQueueEx)
            TestQueueRemoveExDirectHandoff(RemoveQueueEx);
    }
#endif
}
