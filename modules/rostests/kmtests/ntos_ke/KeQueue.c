/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite KQUEUE API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

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

START_TEST(KeQueue)
{
    TestQueueOrder();
    TestQueueHeadInsert();
    TestQueueRundown();
}
