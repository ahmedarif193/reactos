/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite system thread API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT ThreadRunning;
static KEVENT ThreadContinue;
static volatile LONG InsideThread;
static PETHREAD ObservedThread;
static BOOLEAN ObservedIsSystem;

static
VOID
NTAPI
SystemThreadRoutine(
    _In_ PVOID Context)
{
    ok_eq_pointer(Context, (PVOID)(ULONG_PTR)0x1234);
    InterlockedExchange(&InsideThread, 1);
    ObservedThread = PsGetCurrentThread();
    ObservedIsSystem = PsIsSystemThread(PsGetCurrentThread());
    KeSetEvent(&ThreadRunning, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&ThreadContinue, Executive, KernelMode, FALSE, NULL);
    PsTerminateSystemThread(STATUS_PIPE_BROKEN);
}

static
VOID
TestSystemThread(VOID)
{
    HANDLE ThreadHandle;
    PETHREAD Thread;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;
    OBJECT_ATTRIBUTES ObjectAttributes;

    KeInitializeEvent(&ThreadRunning, NotificationEvent, FALSE);
    KeInitializeEvent(&ThreadContinue, NotificationEvent, FALSE);
    InsideThread = 0;
    ObservedThread = NULL;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, SystemThreadRoutine, (PVOID)(ULONG_PTR)0x1234);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID *)&Thread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&ThreadRunning, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(InsideThread, 1L);
    ok_eq_pointer(ObservedThread, Thread);
    ok_bool_true(ObservedIsSystem, "PsIsSystemThread in system thread");

    KeSetEvent(&ThreadContinue, IO_NO_INCREMENT, FALSE);
    Status = KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ObDereferenceObject(Thread);
    ObCloseHandle(ThreadHandle, KernelMode);
}

static
VOID
TestCurrentIdentity(VOID)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();

    ok(Process != NULL, "PsGetCurrentProcess NULL\n");
    ok(Thread != NULL, "PsGetCurrentThread NULL\n");
    ok_eq_pointer(Process, IoGetCurrentProcess());
    ok_eq_pointer((PVOID)Thread, (PVOID)KeGetCurrentThread());

    ok_eq_pointer(PsGetThreadId(Thread), PsGetCurrentThreadId());
    ok_eq_pointer(PsGetProcessId(Process), PsGetCurrentProcessId());
    ok_eq_pointer(PsGetThreadProcessId(Thread), PsGetCurrentProcessId());
    ok_eq_pointer(PsGetThreadProcess(Thread), Process);

    ok(PsInitialSystemProcess != NULL, "PsInitialSystemProcess NULL\n");
    ok(PsGetProcessId(PsInitialSystemProcess) == (HANDLE)(ULONG_PTR)4, "system pid %p\n", PsGetProcessId(PsInitialSystemProcess));
}

static
VOID
TestLookup(VOID)
{
    PEPROCESS Process = NULL;
    PETHREAD Thread = NULL;
    NTSTATUS Status;

    Status = PsLookupProcessByProcessId(PsGetCurrentProcessId(), &Process);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Process, PsGetCurrentProcess());
        ObDereferenceObject(Process);
    }

    Status = PsLookupThreadByThreadId(PsGetCurrentThreadId(), &Thread);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_pointer(Thread, PsGetCurrentThread());
        ObDereferenceObject(Thread);
    }

    Process = (PEPROCESS)(ULONG_PTR)1;
    Status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)0xFFFFF, &Process);
    ok_eq_hex(Status, STATUS_INVALID_CID);
    ok_eq_pointer(Process, (PEPROCESS)(ULONG_PTR)1);
}

START_TEST(PsSystemThread)
{
    TestSystemThread();
    TestCurrentIdentity();
    TestLookup();
}
