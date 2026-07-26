/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgkrnl stable circular present-queue removal tests
 */

#include <kmt_test.h>
#include "present_queue_core.h"

#define DXGK_PRESENT_QUEUE_TEST_CAPACITY 5

typedef struct _DXGK_PRESENT_QUEUE_TEST_ENTRY
{
    ULONG Id;
    PVOID Device;
    PVOID Context;
} DXGK_PRESENT_QUEUE_TEST_ENTRY, *PDXGK_PRESENT_QUEUE_TEST_ENTRY;

typedef struct _DXGK_PRESENT_QUEUE_TEST_QUEUE
{
    KSPIN_LOCK Lock;
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entries[DXGK_PRESENT_QUEUE_TEST_CAPACITY];
    ULONG Head;
    ULONG Tail;
    ULONG Count;
    KIRQL BaseIrql;
    volatile LONG MatchOutsideLock;
    volatile LONG ReleaseInsideLock;
    ULONG ReleasedIds[DXGK_PRESENT_QUEUE_TEST_CAPACITY];
    ULONG ReleaseCount;
} DXGK_PRESENT_QUEUE_TEST_QUEUE, *PDXGK_PRESENT_QUEUE_TEST_QUEUE;

typedef struct _DXGK_PRESENT_QUEUE_TEST_MATCH
{
    PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue;
    PVOID Device;
    PVOID Context;
} DXGK_PRESENT_QUEUE_TEST_MATCH, *PDXGK_PRESENT_QUEUE_TEST_MATCH;

static BOOLEAN NTAPI DxgkPresentQueueTestMatch(_In_ const VOID *OpaqueEntry, _In_opt_ PVOID OpaqueContext)
{
    const DXGK_PRESENT_QUEUE_TEST_ENTRY *Entry = OpaqueEntry;
    PDXGK_PRESENT_QUEUE_TEST_MATCH Match = OpaqueContext;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        InterlockedIncrement(&Match->Queue->MatchOutsideLock);
    return !((Match->Device != NULL && Entry->Device != Match->Device) || (Match->Context != NULL && Entry->Context != Match->Context));
}

static DXGK_PRESENT_QUEUE_TEST_ENTRY DxgkPresentQueueTestEntry(_In_ ULONG Id, _In_ PVOID Device, _In_ PVOID Context)
{
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entry;

    RtlZeroMemory(&Entry, sizeof(Entry));
    Entry.Id = Id;
    Entry.Device = Device;
    Entry.Context = Context;
    return Entry;
}

static VOID DxgkPresentQueueTestInitialize(_Out_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue)
{
    RtlZeroMemory(Queue, sizeof(*Queue));
    KeInitializeSpinLock(&Queue->Lock);
    Queue->BaseIrql = KeGetCurrentIrql();
}

static VOID DxgkPresentQueueTestEnqueue(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue, _In_ const DXGK_PRESENT_QUEUE_TEST_ENTRY *Entry)
{
    Queue->Entries[Queue->Tail] = *Entry;
    Queue->Tail = (Queue->Tail + 1) % DXGK_PRESENT_QUEUE_TEST_CAPACITY;
    Queue->Count++;
}

static DXGK_PRESENT_QUEUE_TEST_ENTRY DxgkPresentQueueTestPopFront(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue)
{
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entry = Queue->Entries[Queue->Head];

    RtlZeroMemory(&Queue->Entries[Queue->Head], sizeof(Queue->Entries[Queue->Head]));
    Queue->Head = (Queue->Head + 1) % DXGK_PRESENT_QUEUE_TEST_CAPACITY;
    Queue->Count--;
    return Entry;
}

static VOID DxgkPresentQueueTestRelease(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue, _In_ const DXGK_PRESENT_QUEUE_TEST_ENTRY *Entry)
{
    if (KeGetCurrentIrql() != Queue->BaseIrql)
        InterlockedIncrement(&Queue->ReleaseInsideLock);
    Queue->ReleasedIds[Queue->ReleaseCount] = Entry->Id;
    Queue->ReleaseCount++;
}

static BOOLEAN DxgkPresentQueueTestRemove(_Inout_ PDXGK_PRESENT_QUEUE_TEST_QUEUE Queue, _In_opt_ PVOID Device, _In_opt_ PVOID Context)
{
    DXGK_PRESENT_QUEUE_TEST_ENTRY RemovedEntry;
    DXGK_PRESENT_QUEUE_TEST_MATCH Match;
    BOOLEAN Removed;

    RtlZeroMemory(&RemovedEntry, sizeof(RemovedEntry));
    Match.Queue = Queue;
    Match.Device = Device;
    Match.Context = Context;
    Removed = DxgkPresentQueueCoreRemove(&Queue->Lock, Queue->Entries, sizeof(Queue->Entries[0]), DXGK_PRESENT_QUEUE_TEST_CAPACITY, &Queue->Head, &Queue->Tail, &Queue->Count, DxgkPresentQueueTestMatch, &Match, &RemovedEntry);
    if (Removed)
        DxgkPresentQueueTestRelease(Queue, &RemovedEntry);
    return Removed;
}

static ULONG DxgkPresentQueueTestLogicalId(_In_ const DXGK_PRESENT_QUEUE_TEST_QUEUE *Queue, _In_ ULONG Offset)
{
    return Queue->Entries[(Queue->Head + Offset) % DXGK_PRESENT_QUEUE_TEST_CAPACITY].Id;
}

static VOID DxgkPresentQueueTestLimits(VOID)
{
    DXGK_PRESENT_LIMIT_CORE State;

    DxgkPresentLimitCoreInitialize(&State, 3);
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 3);
    ok_eq_ulong(DxgkPresentLimitCoreGetReserved(&State), 0);
    ok_bool_false(DxgkPresentLimitCoreIsReached(&State), "empty default limit is not reached");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "first reservation succeeds");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "second reservation succeeds");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "third reservation succeeds");
    ok_bool_true(DxgkPresentLimitCoreIsReached(&State), "third reservation reaches default limit");
    ok_bool_false(DxgkPresentLimitCoreTryReserve(&State), "fourth reservation is rejected");
    ok_eq_ulong(DxgkPresentLimitCoreGetReserved(&State), 3);
    { NTSTATUS Observed = DxgkPresentLimitCoreSet(&State, 2, 3, 16); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 2);
    ok_bool_true(DxgkPresentLimitCoreIsReached(&State), "lowering below current reservations remains reached");
    DxgkPresentLimitCoreRelease(&State);
    ok_bool_true(DxgkPresentLimitCoreIsReached(&State), "count equal to lowered limit remains reached");
    DxgkPresentLimitCoreRelease(&State);
    ok_bool_false(DxgkPresentLimitCoreIsReached(&State), "release below limit reopens admission");
    ok_bool_true(DxgkPresentLimitCoreTryReserve(&State), "admission resumes below the lowered limit");
    { NTSTATUS Observed = DxgkPresentLimitCoreSet(&State, 0, 3, 16); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 3);
    { NTSTATUS Observed = DxgkPresentLimitCoreSet(&State, 17, 3, 16); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulong(DxgkPresentLimitCoreGetLimit(&State), 3);
    DxgkPresentLimitCoreRelease(&State);
    DxgkPresentLimitCoreRelease(&State);
    ok_eq_ulong(DxgkPresentLimitCoreGetReserved(&State), 0);
}

START_TEST(DxgkPresentQueue)
{
    PVOID DeviceA = (PVOID)(ULONG_PTR)0x1000;
    PVOID DeviceB = (PVOID)(ULONG_PTR)0x2000;
    PVOID DeviceC = (PVOID)(ULONG_PTR)0x3000;
    PVOID DeviceD = (PVOID)(ULONG_PTR)0x4000;
    PVOID ContextX = (PVOID)(ULONG_PTR)0x5000;
    PVOID ContextY = (PVOID)(ULONG_PTR)0x6000;
    PVOID ContextZ = (PVOID)(ULONG_PTR)0x7000;
    DXGK_PRESENT_QUEUE_TEST_QUEUE Queue;
    DXGK_PRESENT_QUEUE_TEST_ENTRY Entry;
    ULONG Index;

    DxgkPresentQueueTestLimits();

    DxgkPresentQueueTestInitialize(&Queue);
    Entry = DxgkPresentQueueTestEntry(1, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(2, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(3, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(4, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 1);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 2);
    Entry = DxgkPresentQueueTestEntry(5, DeviceC, ContextZ);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(6, DeviceD, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);

    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 1);
    ok_eq_ulong(Queue.Count, 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 0), 3);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 1), 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 2), 5);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 3), 6);

    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, DeviceC, NULL), "remove wrapped middle entry by device");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 0);
    ok_eq_ulong(Queue.Count, 3);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 0), 3);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 1), 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 2), 6);
    ok_eq_ulong(Queue.ReleasedIds[0], 5);

    ok_bool_false(DxgkPresentQueueTestRemove(&Queue, DeviceA, ContextY), "combined device/context mismatch must not remove");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 0);
    ok_eq_ulong(Queue.Count, 3);

    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, ContextX), "remove first matching context while preserving order");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 4);
    ok_eq_ulong(Queue.Count, 2);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 0), 4);
    ok_eq_ulong(DxgkPresentQueueTestLogicalId(&Queue, 1), 6);
    ok_eq_ulong(Queue.ReleasedIds[1], 3);

    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, DeviceB, ContextY), "remove exact device/context match");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "remove final remaining entry");
    ok_bool_false(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "empty queue removal must fail");
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 2);
    ok_eq_ulong(Queue.Count, 0);
    ok_eq_ulong(Queue.ReleaseCount, 4);
    ok_eq_ulong(Queue.ReleasedIds[0], 5);
    ok_eq_ulong(Queue.ReleasedIds[1], 3);
    ok_eq_ulong(Queue.ReleasedIds[2], 4);
    ok_eq_ulong(Queue.ReleasedIds[3], 6);
    ok_eq_long(InterlockedCompareExchange(&Queue.MatchOutsideLock, 0, 0), 0);
    ok_eq_long(InterlockedCompareExchange(&Queue.ReleaseInsideLock, 0, 0), 0);
    for (Index = 0; Index < DXGK_PRESENT_QUEUE_TEST_CAPACITY; ++Index)
    {
        ok_eq_ulong(Queue.Entries[Index].Id, 0);
        ok_eq_pointer(Queue.Entries[Index].Device, NULL);
        ok_eq_pointer(Queue.Entries[Index].Context, NULL);
    }

    DxgkPresentQueueTestInitialize(&Queue);
    Entry = DxgkPresentQueueTestEntry(11, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(12, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(13, DeviceA, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(14, DeviceB, ContextY);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 11);
    Entry = DxgkPresentQueueTestPopFront(&Queue);
    ok_eq_ulong(Entry.Id, 12);
    Entry = DxgkPresentQueueTestEntry(15, DeviceC, ContextZ);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    Entry = DxgkPresentQueueTestEntry(16, DeviceD, ContextX);
    DxgkPresentQueueTestEnqueue(&Queue, &Entry);
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 1);
    ok_eq_ulong(Queue.Count, 4);
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 13");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 14");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 15");
    ok_bool_true(DxgkPresentQueueTestRemove(&Queue, NULL, NULL), "cancel-all removes wrapped head 16");
    ok_eq_ulong(Queue.ReleaseCount, 4);
    ok_eq_ulong(Queue.ReleasedIds[0], 13);
    ok_eq_ulong(Queue.ReleasedIds[1], 14);
    ok_eq_ulong(Queue.ReleasedIds[2], 15);
    ok_eq_ulong(Queue.ReleasedIds[3], 16);
    ok_eq_ulong(Queue.Head, 2);
    ok_eq_ulong(Queue.Tail, 2);
    ok_eq_ulong(Queue.Count, 0);
    ok_eq_long(InterlockedCompareExchange(&Queue.MatchOutsideLock, 0, 0), 0);
    ok_eq_long(InterlockedCompareExchange(&Queue.ReleaseInsideLock, 0, 0), 0);
}
