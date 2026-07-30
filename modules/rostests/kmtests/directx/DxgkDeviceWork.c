/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Per-device accepted-work ledger contract tests
 */

#include <kmt_test.h>
#include "device_work_core.h"

#define DXGK_DEVICE_WORK_TEST_TIMEOUT_SECONDS 10

typedef enum _DXGK_DEVICE_WORK_TEST_EXECUTION_STATE
{
    DxgkDeviceWorkTestActive = 0,
    DxgkDeviceWorkTestStopped = 1,
    DxgkDeviceWorkTestReset = 2
} DXGK_DEVICE_WORK_TEST_EXECUTION_STATE;

typedef struct _DXGK_DEVICE_WORK_TEST_OWNER
{
    volatile LONG Destroying;
    volatile LONG ExecutionState;
    volatile LONG CallbackTerminal;
    NTSTATUS CallbackStatus;
    DXGK_DEVICE_WORK_LEDGER Ledger;
} DXGK_DEVICE_WORK_TEST_OWNER, *PDXGK_DEVICE_WORK_TEST_OWNER;

typedef struct _DXGK_DEVICE_WORK_WAIT_CONTEXT
{
    PDXGK_DEVICE_WORK_LEDGER Ledger;
    BOOLEAN UseSnapshot;
    DXGK_DEVICE_WORK_SNAPSHOT Snapshot;
    KEVENT ArmedEvent;
    KEVENT DoneEvent;
    volatile LONG Status;
} DXGK_DEVICE_WORK_WAIT_CONTEXT, *PDXGK_DEVICE_WORK_WAIT_CONTEXT;

typedef enum _DXGK_DEVICE_WORK_TERMINAL_CASE
{
    DxgkDeviceWorkTerminalStopped,
    DxgkDeviceWorkTerminalReset,
    DxgkDeviceWorkTerminalDestroy,
    DxgkDeviceWorkTerminalCallback
} DXGK_DEVICE_WORK_TERMINAL_CASE;

static NTSTATUS NTAPI DxgkDeviceWorkTestQueryTerminal(_In_opt_ PVOID Context)
{
    PDXGK_DEVICE_WORK_TEST_OWNER Owner = Context;

    if (Owner != NULL && InterlockedCompareExchange(&Owner->CallbackTerminal, 0, 0) != 0)
        return Owner->CallbackStatus;
    return STATUS_SUCCESS;
}

static VOID DxgkDeviceWorkTestInitializeOwner(_Out_ PDXGK_DEVICE_WORK_TEST_OWNER Owner)
{
    DXGK_DEVICE_WORK_TERMINAL_STATE Terminal;

    RtlZeroMemory(Owner, sizeof(*Owner));
    Owner->ExecutionState = DxgkDeviceWorkTestActive;
    Owner->CallbackStatus = STATUS_CANCELLED;
    RtlZeroMemory(&Terminal, sizeof(Terminal));
    Terminal.Destroying = &Owner->Destroying;
    Terminal.ExecutionState = &Owner->ExecutionState;
    Terminal.ActiveExecutionState = DxgkDeviceWorkTestActive;
    Terminal.TerminalStatus = STATUS_DEVICE_REMOVED;
    Terminal.Query = DxgkDeviceWorkTestQueryTerminal;
    Terminal.QueryContext = Owner;
    DxgkDeviceWorkCoreInitializeLedger(&Owner->Ledger, &Terminal);
}

static NTSTATUS DxgkDeviceWorkTestWaitForEvent(_In_ PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * 1000LL * DXGK_DEVICE_WORK_TEST_TIMEOUT_SECONDS;
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
}

static NTSTATUS DxgkDeviceWorkTestWaitForThread(_In_ HANDLE Thread)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * 1000LL * DXGK_DEVICE_WORK_TEST_TIMEOUT_SECONDS;
    return ZwWaitForSingleObject(Thread, FALSE, &Timeout);
}

static VOID NTAPI DxgkDeviceWorkTestWaitThread(_In_ PVOID Parameter)
{
    PDXGK_DEVICE_WORK_WAIT_CONTEXT Context = Parameter;
    NTSTATUS Status;

    if (Context->UseSnapshot)
        Status = DxgkDeviceWorkCoreWaitForSnapshot(Context->Ledger, &Context->Snapshot, &Context->ArmedEvent);
    else
        Status = DxgkDeviceWorkCoreWaitForIdle(Context->Ledger, &Context->ArmedEvent);
    InterlockedExchange(&Context->Status, Status);
    KeSetEvent(&Context->DoneEvent, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static NTSTATUS DxgkDeviceWorkTestStartWait(_Out_ PDXGK_DEVICE_WORK_WAIT_CONTEXT Context, _In_ PDXGK_DEVICE_WORK_LEDGER Ledger, _In_opt_ const DXGK_DEVICE_WORK_SNAPSHOT *Snapshot, _Out_ PHANDLE Thread)
{
    OBJECT_ATTRIBUTES Attributes;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Ledger = Ledger;
    Context->UseSnapshot = Snapshot != NULL;
    if (Snapshot != NULL)
        Context->Snapshot = *Snapshot;
    Context->Status = STATUS_PENDING;
    KeInitializeEvent(&Context->ArmedEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context->DoneEvent, NotificationEvent, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    *Thread = NULL;
    return PsCreateSystemThread(Thread, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, DxgkDeviceWorkTestWaitThread, Context);
}

static VOID DxgkDeviceWorkTestJoin(_Inout_ PDXGK_DEVICE_WORK_WAIT_CONTEXT Context, _Inout_ PHANDLE Thread)
{
    NTSTATUS Status;

    if (*Thread == NULL)
        return;
    Status = DxgkDeviceWorkTestWaitForEvent(&Context->DoneEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
    {
        DxgkDeviceWorkCoreTransitionTerminal(Context->Ledger, Context->Ledger->Terminal.Destroying, 1);
        Status = DxgkDeviceWorkTestWaitForEvent(&Context->DoneEvent);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Status != STATUS_SUCCESS)
        {
            Status = KeWaitForSingleObject(&Context->DoneEvent, Executive, KernelMode, FALSE, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }
    }
    Status = DxgkDeviceWorkTestWaitForThread(*Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS)
    {
        Status = ZwWaitForSingleObject(*Thread, FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (Status == STATUS_SUCCESS)
    {
        Status = ZwClose(*Thread);
        ok_eq_hex(Status, STATUS_SUCCESS);
        *Thread = NULL;
    }
}

static VOID DxgkDeviceWorkTestAbortWait(_Inout_ PDXGK_DEVICE_WORK_TEST_OWNER Owner)
{
    DxgkDeviceWorkCoreTransitionTerminal(&Owner->Ledger, &Owner->Destroying, 1);
}

static VOID DxgkDeviceWorkTestDormantIdempotentOverflow(VOID)
{
    DXGK_DEVICE_WORK_TEST_OWNER Owner;
    DXGK_DEVICE_WORK_TEST_OWNER OverflowOwner;
    DXGK_DEVICE_WORK_TEST_OWNER TerminalOwner;
    DXGK_DEVICE_WORK_ITEM Work;
    DXGK_DEVICE_WORK_ITEM DormantWork;
    DXGK_DEVICE_WORK_ITEM OverflowWork;
    DXGK_DEVICE_WORK_ITEM TerminalWork;
    DXGK_DEVICE_WORK_SNAPSHOT Snapshot;
    ULONGLONG Sequence;
    NTSTATUS Status;

    DxgkDeviceWorkTestInitializeOwner(&Owner);
    DxgkDeviceWorkCoreInitializeItem(&Work, &Owner.Ledger);
    ok_eq_long(InterlockedCompareExchange(&Work.State, 0, 0), DxgkDeviceWorkItemDormant);
    ok(Work.Sequence == 0, "dormant work has sequence %I64u\n", Work.Sequence);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "new ledger empty");
    Status = DxgkDeviceWorkCoreActivate(&Work);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Sequence = Work.Sequence;
    ok(Sequence == 1, "first work has sequence %I64u\n", Sequence);
    Status = DxgkDeviceWorkCoreActivate(&Work);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Work.Sequence == Sequence, "idempotent activation changed sequence to %I64u\n", Work.Sequence);
    ok(Owner.Ledger.LastAssignedSequence == Sequence, "idempotent activation advanced ledger to %I64u\n", Owner.Ledger.LastAssignedSequence);
    Status = DxgkDeviceWorkCoreCaptureSnapshot(&Owner.Ledger, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Snapshot.Ledger, &Owner.Ledger);
    ok(Snapshot.TargetSequence == Sequence, "snapshot target is %I64u\n", Snapshot.TargetSequence);
    DxgkDeviceWorkCoreComplete(&Work);
    DxgkDeviceWorkCoreComplete(&Work);
    ok_eq_long(InterlockedCompareExchange(&Work.State, 0, 0), DxgkDeviceWorkItemCompleted);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "completed ledger empty");
    Status = DxgkDeviceWorkCoreActivate(&Work);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);

    DxgkDeviceWorkCoreInitializeItem(&DormantWork, &Owner.Ledger);
    DxgkDeviceWorkCoreComplete(&DormantWork);
    DxgkDeviceWorkCoreComplete(&DormantWork);
    ok_eq_long(InterlockedCompareExchange(&DormantWork.State, 0, 0), DxgkDeviceWorkItemCompleted);
    ok(DormantWork.Sequence == 0, "cancelled dormant work has sequence %I64u\n", DormantWork.Sequence);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "dormant completion leaves ledger empty");

    DxgkDeviceWorkTestInitializeOwner(&OverflowOwner);
    OverflowOwner.Ledger.LastAssignedSequence = MAXULONGLONG;
    DxgkDeviceWorkCoreInitializeItem(&OverflowWork, &OverflowOwner.Ledger);
    Status = DxgkDeviceWorkCoreActivate(&OverflowWork);
    ok_eq_hex(Status, STATUS_INTEGER_OVERFLOW);
    ok_eq_long(InterlockedCompareExchange(&OverflowWork.State, 0, 0), DxgkDeviceWorkItemDormant);
    ok(OverflowWork.Sequence == 0, "overflow work has sequence %I64u\n", OverflowWork.Sequence);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&OverflowOwner.Ledger), "overflow ledger empty");

    DxgkDeviceWorkTestInitializeOwner(&TerminalOwner);
    DxgkDeviceWorkCoreInitializeItem(&TerminalWork, &TerminalOwner.Ledger);
    DxgkDeviceWorkCoreTransitionTerminal(&TerminalOwner.Ledger, &TerminalOwner.ExecutionState, DxgkDeviceWorkTestStopped);
    Status = DxgkDeviceWorkCoreActivate(&TerminalWork);
    ok_eq_hex(Status, STATUS_DEVICE_REMOVED);
    ok_eq_long(InterlockedCompareExchange(&TerminalWork.State, 0, 0), DxgkDeviceWorkItemDormant);
    ok(TerminalWork.Sequence == 0, "terminal-rejected work has sequence %I64u\n", TerminalWork.Sequence);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&TerminalOwner.Ledger), "terminal-rejected ledger empty");
}

static VOID DxgkDeviceWorkTestOutOfOrderCompletion(VOID)
{
    DXGK_DEVICE_WORK_TEST_OWNER Owner;
    DXGK_DEVICE_WORK_ITEM First;
    DXGK_DEVICE_WORK_ITEM Second;
    DXGK_DEVICE_WORK_ITEM Third;
    DXGK_DEVICE_WORK_WAIT_CONTEXT Wait;
    HANDLE Thread = NULL;
    NTSTATUS Status;

    DxgkDeviceWorkTestInitializeOwner(&Owner);
    DxgkDeviceWorkCoreInitializeItem(&First, &Owner.Ledger);
    DxgkDeviceWorkCoreInitializeItem(&Second, &Owner.Ledger);
    DxgkDeviceWorkCoreInitializeItem(&Third, &Owner.Ledger);
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&First); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&Second); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&Third); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok(First.Sequence == 1 && Second.Sequence == 2 && Third.Sequence == 3, "work order is %I64u/%I64u/%I64u\n", First.Sequence, Second.Sequence, Third.Sequence);
    Status = DxgkDeviceWorkTestStartWait(&Wait, &Owner.Ledger, NULL, &Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkDeviceWorkTestWaitForEvent(&Wait.ArmedEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DxgkDeviceWorkCoreComplete(&Second);
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&Wait.ArmedEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_long(KeReadStateEvent(&Wait.DoneEvent), 0);
    DxgkDeviceWorkCoreComplete(&Third);
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&Wait.ArmedEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_long(KeReadStateEvent(&Wait.DoneEvent), 0);
    DxgkDeviceWorkCoreComplete(&First);
    Status = DxgkDeviceWorkTestWaitForEvent(&Wait.DoneEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex((NTSTATUS)InterlockedCompareExchange(&Wait.Status, 0, 0), STATUS_SUCCESS);

Cleanup:
    DxgkDeviceWorkCoreComplete(&First);
    DxgkDeviceWorkCoreComplete(&Second);
    DxgkDeviceWorkCoreComplete(&Third);
    if (Thread != NULL && KeReadStateEvent(&Wait.DoneEvent) == 0)
        DxgkDeviceWorkTestAbortWait(&Owner);
    DxgkDeviceWorkTestJoin(&Wait, &Thread);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "out-of-order ledger empty");
}

static VOID DxgkDeviceWorkTestLaterSubmitExclusion(VOID)
{
    DXGK_DEVICE_WORK_TEST_OWNER Owner;
    DXGK_DEVICE_WORK_ITEM Initial;
    DXGK_DEVICE_WORK_ITEM Later;
    DXGK_DEVICE_WORK_WAIT_CONTEXT Wait;
    HANDLE Thread = NULL;
    NTSTATUS Status;

    DxgkDeviceWorkTestInitializeOwner(&Owner);
    DxgkDeviceWorkCoreInitializeItem(&Initial, &Owner.Ledger);
    DxgkDeviceWorkCoreInitializeItem(&Later, &Owner.Ledger);
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&Initial); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Status = DxgkDeviceWorkTestStartWait(&Wait, &Owner.Ledger, NULL, &Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkDeviceWorkTestWaitForEvent(&Wait.ArmedEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&Later); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok(Initial.Sequence == 1 && Later.Sequence == 2, "later-submit sequences are %I64u/%I64u\n", Initial.Sequence, Later.Sequence);
    DxgkDeviceWorkCoreComplete(&Initial);
    Status = DxgkDeviceWorkTestWaitForEvent(&Wait.DoneEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex((NTSTATUS)InterlockedCompareExchange(&Wait.Status, 0, 0), STATUS_SUCCESS);
    ok_eq_long(InterlockedCompareExchange(&Later.State, 0, 0), DxgkDeviceWorkItemActive);
    ok_bool_false(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "later work remains active");

Cleanup:
    DxgkDeviceWorkCoreComplete(&Initial);
    DxgkDeviceWorkCoreComplete(&Later);
    if (Thread != NULL && KeReadStateEvent(&Wait.DoneEvent) == 0)
        DxgkDeviceWorkTestAbortWait(&Owner);
    DxgkDeviceWorkTestJoin(&Wait, &Thread);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "later-submit ledger empty");
}

static VOID DxgkDeviceWorkTestConcurrentWaiters(VOID)
{
    DXGK_DEVICE_WORK_TEST_OWNER Owner;
    DXGK_DEVICE_WORK_ITEM Work;
    DXGK_DEVICE_WORK_WAIT_CONTEXT FirstWait;
    DXGK_DEVICE_WORK_WAIT_CONTEXT SecondWait;
    HANDLE FirstThread = NULL;
    HANDLE SecondThread = NULL;
    NTSTATUS Status;

    DxgkDeviceWorkTestInitializeOwner(&Owner);
    DxgkDeviceWorkCoreInitializeItem(&Work, &Owner.Ledger);
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&Work); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Status = DxgkDeviceWorkTestStartWait(&FirstWait, &Owner.Ledger, NULL, &FirstThread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkDeviceWorkTestStartWait(&SecondWait, &Owner.Ledger, NULL, &SecondThread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&FirstWait.ArmedEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&SecondWait.ArmedEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_long(KeReadStateEvent(&FirstWait.DoneEvent), 0);
    ok_eq_long(KeReadStateEvent(&SecondWait.DoneEvent), 0);
    DxgkDeviceWorkCoreComplete(&Work);
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&FirstWait.DoneEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&SecondWait.DoneEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_hex((NTSTATUS)InterlockedCompareExchange(&FirstWait.Status, 0, 0), STATUS_SUCCESS);
    ok_eq_hex((NTSTATUS)InterlockedCompareExchange(&SecondWait.Status, 0, 0), STATUS_SUCCESS);

Cleanup:
    DxgkDeviceWorkCoreComplete(&Work);
    if ((FirstThread != NULL && KeReadStateEvent(&FirstWait.DoneEvent) == 0) || (SecondThread != NULL && KeReadStateEvent(&SecondWait.DoneEvent) == 0))
        DxgkDeviceWorkTestAbortWait(&Owner);
    DxgkDeviceWorkTestJoin(&FirstWait, &FirstThread);
    DxgkDeviceWorkTestJoin(&SecondWait, &SecondThread);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "concurrent-wait ledger empty");
}

static VOID DxgkDeviceWorkTestTerminalWake(_In_ DXGK_DEVICE_WORK_TERMINAL_CASE TerminalCase)
{
    DXGK_DEVICE_WORK_TEST_OWNER Owner;
    DXGK_DEVICE_WORK_ITEM Work;
    DXGK_DEVICE_WORK_WAIT_CONTEXT Wait;
    HANDLE Thread = NULL;
    NTSTATUS ExpectedStatus = STATUS_DEVICE_REMOVED;
    NTSTATUS Status;

    DxgkDeviceWorkTestInitializeOwner(&Owner);
    DxgkDeviceWorkCoreInitializeItem(&Work, &Owner.Ledger);
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&Work); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Status = DxgkDeviceWorkTestStartWait(&Wait, &Owner.Ledger, NULL, &Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&Wait.ArmedEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    if (TerminalCase == DxgkDeviceWorkTerminalStopped)
        DxgkDeviceWorkCoreTransitionTerminal(&Owner.Ledger, &Owner.ExecutionState, DxgkDeviceWorkTestStopped);
    else if (TerminalCase == DxgkDeviceWorkTerminalReset)
        DxgkDeviceWorkCoreTransitionTerminal(&Owner.Ledger, &Owner.ExecutionState, DxgkDeviceWorkTestReset);
    else if (TerminalCase == DxgkDeviceWorkTerminalDestroy)
        DxgkDeviceWorkCoreTransitionTerminal(&Owner.Ledger, &Owner.Destroying, 1);
    else
    {
        ExpectedStatus = Owner.CallbackStatus;
        InterlockedExchange(&Owner.CallbackTerminal, 1);
    }
    if (TerminalCase == DxgkDeviceWorkTerminalCallback)
        DxgkDeviceWorkCoreNotifyStateChange(&Owner.Ledger);
    Status = DxgkDeviceWorkTestWaitForEvent(&Wait.DoneEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex((NTSTATUS)InterlockedCompareExchange(&Wait.Status, 0, 0), ExpectedStatus);

Cleanup:
    DxgkDeviceWorkCoreComplete(&Work);
    if (Thread != NULL && KeReadStateEvent(&Wait.DoneEvent) == 0)
        DxgkDeviceWorkTestAbortWait(&Owner);
    DxgkDeviceWorkTestJoin(&Wait, &Thread);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&Owner.Ledger), "terminal-wake ledger empty");
}

static VOID DxgkDeviceWorkTestSnapshotAndIsolation(VOID)
{
    DXGK_DEVICE_WORK_TEST_OWNER FirstOwner;
    DXGK_DEVICE_WORK_TEST_OWNER SecondOwner;
    DXGK_DEVICE_WORK_ITEM FirstWork;
    DXGK_DEVICE_WORK_ITEM LaterWork;
    DXGK_DEVICE_WORK_ITEM IsolatedWork;
    DXGK_DEVICE_WORK_SNAPSHOT Snapshot;
    DXGK_DEVICE_WORK_WAIT_CONTEXT Wait;
    HANDLE Thread = NULL;
    NTSTATUS Status;

    DxgkDeviceWorkTestInitializeOwner(&FirstOwner);
    DxgkDeviceWorkTestInitializeOwner(&SecondOwner);
    DxgkDeviceWorkCoreInitializeItem(&FirstWork, &FirstOwner.Ledger);
    DxgkDeviceWorkCoreInitializeItem(&LaterWork, &FirstOwner.Ledger);
    DxgkDeviceWorkCoreInitializeItem(&IsolatedWork, &SecondOwner.Ledger);
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&FirstWork); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&IsolatedWork); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Status = DxgkDeviceWorkCoreCaptureSnapshot(&FirstOwner.Ledger, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    { NTSTATUS Observed = DxgkDeviceWorkCoreWaitForSnapshot(&SecondOwner.Ledger, &Snapshot, NULL); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkDeviceWorkCoreActivate(&LaterWork); ok_eq_hex(Observed, STATUS_SUCCESS); }
    Status = DxgkDeviceWorkTestStartWait(&Wait, &FirstOwner.Ledger, &Snapshot, &Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&Wait.ArmedEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    DxgkDeviceWorkCoreComplete(&IsolatedWork);
    DxgkDeviceWorkCoreNotifyStateChange(&SecondOwner.Ledger);
    ok_eq_long(KeReadStateEvent(&Wait.DoneEvent), 0);
    DxgkDeviceWorkCoreComplete(&FirstWork);
    { NTSTATUS Observed = DxgkDeviceWorkTestWaitForEvent(&Wait.DoneEvent); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_hex((NTSTATUS)InterlockedCompareExchange(&Wait.Status, 0, 0), STATUS_SUCCESS);
    ok_eq_long(InterlockedCompareExchange(&LaterWork.State, 0, 0), DxgkDeviceWorkItemActive);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&SecondOwner.Ledger), "second ledger empty");
    ok_bool_false(DxgkDeviceWorkCoreIsEmpty(&FirstOwner.Ledger), "first ledger retains later work");

Cleanup:
    DxgkDeviceWorkCoreComplete(&FirstWork);
    DxgkDeviceWorkCoreComplete(&LaterWork);
    DxgkDeviceWorkCoreComplete(&IsolatedWork);
    if (Thread != NULL && KeReadStateEvent(&Wait.DoneEvent) == 0)
        DxgkDeviceWorkTestAbortWait(&FirstOwner);
    DxgkDeviceWorkTestJoin(&Wait, &Thread);
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&FirstOwner.Ledger), "first isolated ledger empty");
    ok_bool_true(DxgkDeviceWorkCoreIsEmpty(&SecondOwner.Ledger), "second isolated ledger empty");
}

static VOID
DxgkDeviceWorkTestConditionalTerminalTransition(VOID)
{
    DXGK_DEVICE_WORK_TEST_OWNER Owner;
    DXGK_DEVICE_WORK_ITEM Work;
    NTSTATUS Status;

    DxgkDeviceWorkTestInitializeOwner(&Owner);
    DxgkDeviceWorkCoreInitializeItem(&Work, &Owner.Ledger);

    ok_bool_true(
        DxgkDeviceWorkCoreTryTransitionTerminal(
            &Owner.Ledger,
            &Owner.ExecutionState,
            DxgkDeviceWorkTestActive,
            DxgkDeviceWorkTestReset),
        "active owner takes the first terminal transition");
    ok_eq_long(
        InterlockedCompareExchange(&Owner.ExecutionState, 0, 0),
        DxgkDeviceWorkTestReset);
    ok_bool_false(
        DxgkDeviceWorkCoreTryTransitionTerminal(
            &Owner.Ledger,
            &Owner.ExecutionState,
            DxgkDeviceWorkTestActive,
            DxgkDeviceWorkTestStopped),
        "late terminal transition cannot overwrite the winner");
    ok_eq_long(
        InterlockedCompareExchange(&Owner.ExecutionState, 0, 0),
        DxgkDeviceWorkTestReset);

    Status = DxgkDeviceWorkCoreActivate(&Work);
    ok_eq_hex(Status, STATUS_DEVICE_REMOVED);
    ok_eq_long(
        InterlockedCompareExchange(&Work.State, 0, 0),
        DxgkDeviceWorkItemDormant);
}

START_TEST(DxgkDeviceWork)
{
    DxgkDeviceWorkTestDormantIdempotentOverflow();
    DxgkDeviceWorkTestOutOfOrderCompletion();
    DxgkDeviceWorkTestLaterSubmitExclusion();
    DxgkDeviceWorkTestConcurrentWaiters();
    DxgkDeviceWorkTestTerminalWake(DxgkDeviceWorkTerminalStopped);
    DxgkDeviceWorkTestTerminalWake(DxgkDeviceWorkTerminalReset);
    DxgkDeviceWorkTestTerminalWake(DxgkDeviceWorkTerminalDestroy);
    DxgkDeviceWorkTestTerminalWake(DxgkDeviceWorkTerminalCallback);
    DxgkDeviceWorkTestSnapshotAndIsolation();
    DxgkDeviceWorkTestConditionalTerminalTransition();
}
