/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Threaded DPC test
 *
 * Threaded DPCs run inside a system worker thread at PASSIVE_LEVEL. The
 * distinguishing contract is the dispatch context, not merely that the
 * initializer is exported.
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

/* DpcType values from ntddk's KOBJECTS enum:
 *   DpcObject         = 19
 *   ThreadedDpcObject = 24
 * The KDPC.Type byte is initialized by KeInitializeThreadedDpc to
 * ThreadedDpcObject so the dispatcher routes it to the worker thread. */
#define KMT_THREADED_DPC_TYPE 24
#define KMT_DPC_OBJECT_TYPE   19

typedef VOID (NTAPI *PKE_INIT_THREADED_DPC)(
    PRKDPC Dpc,
    PKDEFERRED_ROUTINE DeferredRoutine,
    PVOID DeferredContext);

static volatile LONG ThreadedDpcRanCount;
static volatile KIRQL ThreadedDpcIrql;
static volatile PKTHREAD ThreadedDpcThread;
static KEVENT ThreadedDpcDoneEvent;

static KDEFERRED_ROUTINE ThreadedDpcRoutine;

static
VOID
NTAPI
ThreadedDpcRoutine(
    IN PRKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);

    /* Capture the dispatch context before waking the waiting test thread. */
    ThreadedDpcIrql = KeGetCurrentIrql();
    ThreadedDpcThread = KeGetCurrentThread();
    InterlockedIncrement(&ThreadedDpcRanCount);

    ok_eq_pointer(SystemArgument1, (PVOID)0xCAFE1);
    ok_eq_pointer(SystemArgument2, (PVOID)0xCAFE2);

    KeSetEvent(&ThreadedDpcDoneEvent, IO_NO_INCREMENT, FALSE);
}

START_TEST(KeThreadedDpc)
{
    PKE_INIT_THREADED_DPC pKeInitializeThreadedDpc;
    KDPC Dpc;
    BOOLEAN Inserted;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    pKeInitializeThreadedDpc =
        (PKE_INIT_THREADED_DPC)KmtGetSystemRoutineAddress(L"KeInitializeThreadedDpc");

    if (skip(GetNTVersion() >= _WIN32_WINNT_VISTA,
             "Threaded DPCs require NT 6+\n"))
    {
        return;
    }

    if (skip(pKeInitializeThreadedDpc != NULL,
             "KeInitializeThreadedDpc not exported (pre-Vista kernel)\n"))
    {
        return;
    }

    trace("KeInitializeThreadedDpc = %p, NT version = 0x%04x\n",
          pKeInitializeThreadedDpc, GetNTVersion());

    ThreadedDpcRanCount = 0;
    ThreadedDpcIrql = 0xFF;
    ThreadedDpcThread = NULL;
    KeInitializeEvent(&ThreadedDpcDoneEvent, NotificationEvent, FALSE);

    /* Pre-fill so we can confirm the init writes the right Type byte. */
    memset(&Dpc, 0xAA, sizeof(Dpc));
    pKeInitializeThreadedDpc(&Dpc, ThreadedDpcRoutine, NULL);

    ok_eq_uint(Dpc.Type, KMT_THREADED_DPC_TYPE);
    ok_eq_pointer(Dpc.DeferredRoutine, ThreadedDpcRoutine);
    ok_eq_pointer(Dpc.DeferredContext, NULL);

    /* Queue from PASSIVE_LEVEL. */
    ok_irql(PASSIVE_LEVEL);
    Inserted = KeInsertQueueDpc(&Dpc, (PVOID)0xCAFE1, (PVOID)0xCAFE2);
    ok_bool_true(Inserted, "KeInsertQueueDpc on threaded DPC");

    /* Wait up to 5 seconds for the worker thread to dispatch us. */
    Timeout.QuadPart = -5LL * SECOND;
    Status = KeWaitForSingleObject(&ThreadedDpcDoneEvent,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (skip(Status == STATUS_SUCCESS, "Threaded DPC never ran\n"))
        return;

    ok_eq_long(ThreadedDpcRanCount, 1L);

    ok(ThreadedDpcIrql == PASSIVE_LEVEL,
       "Threaded DPC ran at IRQL %u, expected PASSIVE_LEVEL (0)\n",
       ThreadedDpcIrql);
    ok(ThreadedDpcThread != KeGetCurrentThread(),
       "Threaded DPC ran on the queueing thread %p (should be a worker)\n",
       ThreadedDpcThread);

    trace("Threaded DPC ran on thread %p at IRQL %u (queueing thread = %p)\n",
          ThreadedDpcThread, ThreadedDpcIrql, KeGetCurrentThread());
}
