/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Event test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>

/* The test driver's target version predates the Win8 KWAIT_BLOCK layout. */
typedef struct
{
    LIST_ENTRY WaitListEntry;
    UCHAR WaitType;
    UCHAR BlockState;
    USHORT WaitKey;
#ifdef _WIN64
    LONG SpareLong;
#endif
    PKTHREAD Thread;
} WAIT_BLOCK_WIN8;

static
PKTHREAD
GetWaitBlockThread(_In_ PLIST_ENTRY Entry)
{
    if (GetNTVersion() >= _WIN32_WINNT_WIN8)
        return CONTAINING_RECORD(Entry, WAIT_BLOCK_WIN8, WaitListEntry)->Thread;
    return CONTAINING_RECORD(Entry, KWAIT_BLOCK, WaitListEntry)->Thread;
}

#define CheckEvent(Event, ExpectedType, State, ExpectedWaitNext,                \
                            Irql, ThreadList, ThreadCount) do                   \
{                                                                               \
    INT TheIndex;                                                               \
    PLIST_ENTRY TheEntry;                                                       \
    PKTHREAD TheThread;                                                         \
    ok_eq_uint((Event)->Header.Type, ExpectedType);                             \
    ok_eq_uint((Event)->Header.Hand, sizeof *(Event) / sizeof(ULONG));          \
    /* NT 6.1+ KeInitializeEvent zeroes the Hand byte (bits 8-15) of the       \
     * dispatcher Lock dword, leaving only the top 0x55 from the pre-init      \
     * memset. NT 5.x doesn't touch that byte, so 0x55 stays in bits 8-15      \
     * too. Branch the expected pattern on the running kernel version. */     \
    ok_eq_hex((Event)->Header.Lock & 0xFF00FF00L,                               \
              GetNTVersion() >= _WIN32_WINNT_WIN7 ? 0x55000000L : 0x55005500L); \
    ok_eq_long((Event)->Header.SignalState, State);                             \
    TheEntry = (Event)->Header.WaitListHead.Flink;                              \
    for (TheIndex = 0; TheIndex < (ThreadCount); ++TheIndex)                    \
    {                                                                           \
        if (TheEntry == &(Event)->Header.WaitListHead)                         \
            break;                                                             \
        TheThread = GetWaitBlockThread(TheEntry);                              \
        ok_eq_pointer(TheThread, (ThreadList)[TheIndex]);                       \
        ok_eq_pointer(TheEntry->Flink->Blink, TheEntry);                        \
        TheEntry = TheEntry->Flink;                                             \
    }                                                                           \
    ok_eq_int(TheIndex, ThreadCount);                                          \
    ok_eq_pointer(TheEntry, &(Event)->Header.WaitListHead);                     \
    ok_eq_pointer(TheEntry->Flink->Blink, TheEntry);                            \
    ok_eq_long(KeReadStateEvent(Event), State);                                 \
    /* Thread->WaitNext can stay set on NT 6+ after KeWaitForMultipleObjects     \
     * satisfies the wait via the fast-path and doesn't reach KiSwapThread.      \
     * Pre-Vista always cleared it. Just don't assert the exact value on         \
     * Vista+; the test's normal flow either sets it explicitly to FALSE         \
     * before the next iteration or doesn't depend on it. */                    \
    if (GetNTVersion() < _WIN32_WINNT_VISTA)                                    \
        ok_eq_bool(Thread->WaitNext, ExpectedWaitNext);                         \
    ok_irql(Irql);                                                              \
} while (0)

static
VOID
TestEventFunctional(
    IN PKEVENT Event,
    IN EVENT_TYPE Type,
    IN KIRQL OriginalIrql)
{
    LONG State;
    PKTHREAD Thread = KeGetCurrentThread();

    memset(Event, 0x55, sizeof *Event);
    KeInitializeEvent(Event, Type, FALSE);
    CheckEvent(Event, Type, 0L, FALSE, OriginalIrql, (PVOID *)NULL, 0);

    memset(Event, 0x55, sizeof *Event);
    KeInitializeEvent(Event, Type, TRUE);
    CheckEvent(Event, Type, 1L, FALSE, OriginalIrql, (PVOID *)NULL, 0);

    Event->Header.SignalState = 0x12345678L;
    CheckEvent(Event, Type, 0x12345678L, FALSE, OriginalIrql, (PVOID *)NULL, 0);

    State = KePulseEvent(Event, 0, FALSE);
    CheckEvent(Event, Type, 0L, FALSE, OriginalIrql, (PVOID *)NULL, 0);
    ok_eq_long(State, 0x12345678L);

    Event->Header.SignalState = 0x12345678L;
    KeClearEvent(Event);
    CheckEvent(Event, Type, 0L, FALSE, OriginalIrql, (PVOID *)NULL, 0);

    State = KeSetEvent(Event, 0, FALSE);
    CheckEvent(Event, Type, 1L, FALSE, OriginalIrql, (PVOID *)NULL, 0);
    ok_eq_long(State, 0L);

    State = KeResetEvent(Event);
    CheckEvent(Event, Type, 0L, FALSE, OriginalIrql, (PVOID *)NULL, 0);
    ok_eq_long(State, 1L);

    Event->Header.SignalState = 0x23456789L;
    State = KeSetEvent(Event, 0, FALSE);
    CheckEvent(Event, Type, 1L, FALSE, OriginalIrql, (PVOID *)NULL, 0);
    ok_eq_long(State, 0x23456789L);

    Event->Header.SignalState = 0x3456789AL;
    State = KeResetEvent(Event);
    CheckEvent(Event, Type, 0L, FALSE, OriginalIrql, (PVOID *)NULL, 0);
    ok_eq_long(State, 0x3456789AL);

    /* Irql is raised to DISPATCH_LEVEL here, which kills checked build,
     * a spinlock is acquired and never released, which kills MP build */
    if ((OriginalIrql <= DISPATCH_LEVEL || !KmtIsCheckedBuild) &&
        !KmtIsMultiProcessorBuild)
    {
        Event->Header.SignalState = 0x456789ABL;
        State = KeSetEvent(Event, 0, TRUE);
        CheckEvent(Event, Type, 1L, TRUE, DISPATCH_LEVEL, (PVOID *)NULL, 0);
        ok_eq_long(State, 0x456789ABL);
        ok_eq_uint(Thread->WaitIrql, OriginalIrql);
        /* repair the "damage" */
        Thread->WaitNext = FALSE;
        KmtSetIrql(OriginalIrql);

        Event->Header.SignalState = 0x56789ABCL;
        State = KePulseEvent(Event, 0, TRUE);
        CheckEvent(Event, Type, 0L, TRUE, DISPATCH_LEVEL, (PVOID *)NULL, 0);
        ok_eq_long(State, 0x56789ABCL);
        ok_eq_uint(Thread->WaitIrql, OriginalIrql);
        /* repair the "damage" */
        Thread->WaitNext = FALSE;
        KmtSetIrql(OriginalIrql);
    }

    ok_irql(OriginalIrql);
    KmtSetIrql(OriginalIrql);
}

typedef struct
{
    HANDLE Handle;
    PKTHREAD Thread;
    PKEVENT Event;
    KEVENT Ready;
} THREAD_DATA, *PTHREAD_DATA;

static
VOID
NTAPI
WaitForEventThread(
    IN OUT PVOID Context)
{
    NTSTATUS Status;
    PTHREAD_DATA ThreadData = Context;
    KAFFINITY OldAffinity;
    KIRQL OldIrql;

    ok_irql(PASSIVE_LEVEL);
    OldAffinity = KeSetSystemAffinityThreadEx(1);
    /* All participants use one processor. Wait=TRUE prevents the controller
     * from running between this signal and insertion into the event wait list.
     * APC_LEVEL also prevents an APC from temporarily removing this waiter. */
    KeRaiseIrql(APC_LEVEL, &OldIrql);
    KeSetEvent(&ThreadData->Ready, IO_NO_INCREMENT, TRUE);
    Status = KeWaitForSingleObject(ThreadData->Event, Executive, KernelMode, FALSE, NULL);
    KeLowerIrql(OldIrql);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_irql(PASSIVE_LEVEL);
    KeRevertToUserAffinityThreadEx(OldAffinity);
}

typedef LONG (NTAPI *PSET_EVENT_FUNCTION)(PRKEVENT, KPRIORITY, BOOLEAN);

static
VOID
TestEventConcurrent(
    IN PKEVENT Event,
    IN EVENT_TYPE Type,
    IN KIRQL OriginalIrql,
    PSET_EVENT_FUNCTION SetEvent,
    KPRIORITY PriorityIncrement,
    LONG ExpectedState,
    BOOLEAN SatisfiesAll)
{
    NTSTATUS Status;
    THREAD_DATA Threads[5];
    const INT ThreadCount = sizeof Threads / sizeof Threads[0];
    KPRIORITY Priority;
    LARGE_INTEGER LongTimeout, ShortTimeout;
    INT i;
    KWAIT_BLOCK WaitBlock[RTL_NUMBER_OF(Threads)];
    PVOID ThreadObjects[RTL_NUMBER_OF(Threads)];
    LONG State;
    PKTHREAD Thread = KeGetCurrentThread();
    OBJECT_ATTRIBUTES ObjectAttributes;
    KIRQL OldIrql;
    INT Created = 0;

    LongTimeout.QuadPart = -10 * SECOND;
    ShortTimeout.QuadPart = -1 * MILLISECOND;

    RtlZeroMemory(Threads, sizeof(Threads));
    memset(Event, 0x55, sizeof(*Event));
    KeInitializeEvent(Event, Type, FALSE);

    for (i = 0; i < ThreadCount; ++i)
    {
        Threads[i].Event = Event;
        KeInitializeEvent(&Threads[i].Ready, SynchronizationEvent, FALSE);
        InitializeObjectAttributes(&ObjectAttributes,
                                   NULL,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);
        Status = PsCreateSystemThread(&Threads[i].Handle, GENERIC_ALL, &ObjectAttributes, NULL, NULL, WaitForEventThread, &Threads[i]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ++Created;
        Status = ObReferenceObjectByHandle(Threads[i].Handle, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Threads[i].Thread, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ThreadObjects[i] = Threads[i].Thread;
        Status = KeWaitForSingleObject(&Threads[i].Ready, Executive, KernelMode, FALSE, &LongTimeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
            goto Cleanup;
        /* Startup can boost the worker; establish the baseline for the event boost. */
        KeSetPriorityThread(Threads[i].Thread, 8);
        Priority = KeQueryPriorityThread(Threads[i].Thread);
        ok_eq_long(Priority, 8L);
        CheckEvent(Event, Type, 0L, FALSE, PASSIVE_LEVEL, ThreadObjects, i + 1);
    }

    KeRaiseIrql(OriginalIrql, &OldIrql);
    /* the threads shouldn't wake up on their own */
    Status = KeDelayExecutionThread(KernelMode, FALSE, &ShortTimeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    for (i = 0; i < ThreadCount; ++i)
    {
        CheckEvent(Event, Type, 0L, FALSE, OriginalIrql, ThreadObjects + i, ThreadCount - i);
        State = SetEvent(Event, PriorityIncrement + i, FALSE);

        ok_eq_long(State, 0L);
        CheckEvent(Event, Type, ExpectedState, FALSE, OriginalIrql, ThreadObjects + i + 1, SatisfiesAll ? 0 : ThreadCount - i - 1);

        /* The controller runs above the variable-priority range, so these
         * ready threads cannot execute and decay their boosts before inspection. */
        if (SatisfiesAll)
        {
            INT j;
            for (j = 0; j < ThreadCount; ++j)
            {
                Priority = KeQueryPriorityThread(Threads[j].Thread);
                ok_eq_long(Priority, max(min(8L + PriorityIncrement, 15L), 8L));
            }
        }
        else
        {
            Priority = KeQueryPriorityThread(Threads[i].Thread);
            ok_eq_long(Priority, max(min(8L + PriorityIncrement + i, 15L), 8L));
        }
        Status = KeWaitForMultipleObjects(ThreadCount, ThreadObjects, SatisfiesAll ? WaitAll : WaitAny, Executive, KernelMode, FALSE, &LongTimeout, WaitBlock);
        ok_eq_hex(Status, STATUS_WAIT_0 + i);
        if (Status != STATUS_WAIT_0 + i)
        {
            KeLowerIrql(OldIrql);
            goto Cleanup;
        }
        if (SatisfiesAll)
            break;
        /* replace the thread with the current thread - which will never signal */
        ThreadObjects[i] = Thread;
        Status = KeWaitForMultipleObjects(ThreadCount, ThreadObjects, WaitAny, Executive, KernelMode, FALSE, &ShortTimeout, WaitBlock);
        ok_eq_hex(Status, STATUS_TIMEOUT);
    }
    KeLowerIrql(OldIrql);

Cleanup:
    for (i = 0; i < Created; ++i)
    {
        /* Keep the stack context alive until every created worker has exited. */
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
        Status = ZwWaitForSingleObject(Threads[i].Handle, FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Threads[i].Thread)
            ObDereferenceObject(Threads[i].Thread);
        Status = ZwClose(Threads[i].Handle);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
}

#define NUM_SCHED_TESTS 1000

typedef struct
{
    KEVENT Event;
    KEVENT WaitEvent;
    ULONG Counter;
    KPRIORITY PriorityIncrement;
    ULONG CounterValues[NUM_SCHED_TESTS];
} COUNT_THREAD_DATA, *PCOUNT_THREAD_DATA;

static
VOID
NTAPI
CountThread(
    IN OUT PVOID Context)
{
    PCOUNT_THREAD_DATA ThreadData = Context;
    PKEVENT Event = &ThreadData->Event;
    volatile ULONG *Counter = &ThreadData->Counter;
    ULONG *CounterValue = ThreadData->CounterValues;
    KPRIORITY Priority;

    Priority = KeQueryPriorityThread(KeGetCurrentThread());
    ok_eq_long(Priority, 8L);

    while (CounterValue < &ThreadData->CounterValues[NUM_SCHED_TESTS])
    {
        KeSetEvent(&ThreadData->WaitEvent, IO_NO_INCREMENT, TRUE);
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        *CounterValue++ = *Counter;
    }

    Priority = KeQueryPriorityThread(KeGetCurrentThread());
    if (GetNTVersion() >= _WIN32_WINNT_WIN10)
        ok(Priority >= 8L && Priority <= 8L + min(ThreadData->PriorityIncrement, 7),
           "Priority = %lu, expected 8..%lu\n", Priority, 8L + min(ThreadData->PriorityIncrement, 7));
    else
        ok_eq_long(Priority, 8L + min(ThreadData->PriorityIncrement, 7));
}

static
VOID
NTAPI
TestEventScheduling(
    _In_ PVOID Context)
{
    PCOUNT_THREAD_DATA ThreadData;
    PKTHREAD Thread;
    NTSTATUS Status;
    LONG PreviousState;
    ULONG i;
    volatile ULONG *Counter;
    KPRIORITY PriorityIncrement;
    KPRIORITY Priority;

    UNREFERENCED_PARAMETER(Context);

    ThreadData = ExAllocatePoolWithTag(PagedPool, sizeof(*ThreadData), 'CEmK');
    if (skip(ThreadData != NULL, "Out of memory\n"))
    {
        return;
    }
    KeInitializeEvent(&ThreadData->Event, SynchronizationEvent, FALSE);
    KeInitializeEvent(&ThreadData->WaitEvent, SynchronizationEvent, FALSE);
    Counter = &ThreadData->Counter;

    for (PriorityIncrement = 0; PriorityIncrement <= 8; PriorityIncrement++)
    {
        ThreadData->PriorityIncrement = PriorityIncrement;
        ThreadData->Counter = 0;
        RtlFillMemory(ThreadData->CounterValues,
                      sizeof(ThreadData->CounterValues),
                      0xFE);
        Thread = KmtStartThread(CountThread, ThreadData);
        Priority = KeQueryPriorityThread(KeGetCurrentThread());
        if (GetNTVersion() >= _WIN32_WINNT_WIN10)
            ok(Priority >= 8 && Priority <= 15, "[%lu] Priority = %lu\n", PriorityIncrement, Priority);
        else
            ok(Priority == 8, "[%lu] Priority = %lu\n", PriorityIncrement, Priority);
        for (i = 1; i <= NUM_SCHED_TESTS; i++)
        {
            Status = KeWaitForSingleObject(&ThreadData->WaitEvent, Executive, KernelMode, FALSE, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
            PreviousState = KeSetEvent(&ThreadData->Event, PriorityIncrement, FALSE);
            *Counter = i;
            ok_eq_long(PreviousState, 0L);
        }
        Priority = KeQueryPriorityThread(KeGetCurrentThread());
        if (GetNTVersion() >= _WIN32_WINNT_WIN10)
            ok(Priority >= 8 && Priority <= min(8 + (LONG)PriorityIncrement, 15), "[%lu] Priority = %lu\n", PriorityIncrement, Priority);
        else
            ok(Priority == 8, "[%lu] Priority = %lu\n", PriorityIncrement, Priority);
        KmtFinishThread(Thread, NULL);

        for (i = 0; i < NUM_SCHED_TESTS; i++)
        {
            /* The waiter may observe the producer's counter immediately
             * before or after the producer stores the next value. */
            ok(ThreadData->CounterValues[i] == i ||
               ThreadData->CounterValues[i] == i + 1,
               "[%lu] Counter %lu = %lu, expected %lu or %lu\n",
               PriorityIncrement, i,
               ThreadData->CounterValues[i], i, i + 1);
        }
    }

    ExFreePoolWithTag(ThreadData, 'CEmK');
}

static
VOID
NTAPI
TestConcurrentEvents(_In_opt_ PVOID Context)
{
    KEVENT Event;
    KIRQL Irqls[] = { PASSIVE_LEVEL, APC_LEVEL };
    ULONG i;
    KPRIORITY PriorityIncrement;
    KPRIORITY OldPriority;
    KAFFINITY OldAffinity;

    UNREFERENCED_PARAMETER(Context);
    OldAffinity = KeSetSystemAffinityThreadEx(1);
    OldPriority = KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    for (i = 0; i < RTL_NUMBER_OF(Irqls); ++i)
    {
        trace("IRQL: %u\n", Irqls[i]);
        for (PriorityIncrement = -1; PriorityIncrement <= 8; ++PriorityIncrement)
        {
            if (PriorityIncrement < 0 && KmtIsCheckedBuild)
                continue;
            trace("PriorityIncrement: %ld\n", PriorityIncrement);
            trace("-> Checking KeSetEvent, NotificationEvent\n");
            TestEventConcurrent(&Event, NotificationEvent, Irqls[i], KeSetEvent, PriorityIncrement, 1, TRUE);
            trace("-> Checking KeSetEvent, SynchronizationEvent\n");
            TestEventConcurrent(&Event, SynchronizationEvent, Irqls[i], KeSetEvent, PriorityIncrement, 0, FALSE);
            trace("-> Checking KePulseEvent, NotificationEvent\n");
            TestEventConcurrent(&Event, NotificationEvent, Irqls[i], KePulseEvent, PriorityIncrement, 0, TRUE);
            trace("-> Checking KePulseEvent, SynchronizationEvent\n");
            TestEventConcurrent(&Event, SynchronizationEvent, Irqls[i], KePulseEvent, PriorityIncrement, 0, FALSE);
        }
    }
    KeSetPriorityThread(KeGetCurrentThread(), OldPriority);
    KeRevertToUserAffinityThreadEx(OldAffinity);
}

START_TEST(KeEvent)
{
    PKTHREAD Thread;
    KEVENT Event;
    KIRQL Irql;
    KIRQL Irqls[] = { PASSIVE_LEVEL, APC_LEVEL, DISPATCH_LEVEL };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Irqls); ++i)
    {
        KeRaiseIrql(Irqls[i], &Irql);
        TestEventFunctional(&Event, NotificationEvent, Irqls[i]);
        TestEventFunctional(&Event, SynchronizationEvent, Irqls[i]);
        KeLowerIrql(Irql);
    }

    Thread = KmtStartThread(TestConcurrentEvents, NULL);
    KmtFinishThread(Thread, NULL);

    ok_irql(PASSIVE_LEVEL);
    KmtSetIrql(PASSIVE_LEVEL);

    Thread = KmtStartThread(TestEventScheduling, NULL);
    KmtFinishThread(Thread, NULL);
}
