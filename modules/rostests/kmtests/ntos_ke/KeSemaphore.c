/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite KSEMAPHORE API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestSemaphoreBasic(VOID)
{
    KSEMAPHORE Semaphore;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    LONG State;

    Timeout.QuadPart = 0;

    KeInitializeSemaphore(&Semaphore, 0, 5);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 0L);
    ok_eq_uint(Semaphore.Header.Type, SemaphoreObject);
    ok_eq_long(Semaphore.Limit, 5L);

    Status = KeWaitForSingleObject(&Semaphore, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_TIMEOUT);

    State = KeReleaseSemaphore(&Semaphore, IO_NO_INCREMENT, 1, FALSE);
    ok_eq_long(State, 0L);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 1L);

    State = KeReleaseSemaphore(&Semaphore, IO_NO_INCREMENT, 2, FALSE);
    ok_eq_long(State, 1L);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 3L);

    Status = KeWaitForSingleObject(&Semaphore, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 2L);

    Status = KeWaitForSingleObject(&Semaphore, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = KeWaitForSingleObject(&Semaphore, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 0L);

    Status = KeWaitForSingleObject(&Semaphore, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_TIMEOUT);
}

static
VOID
TestSemaphoreLimit(VOID)
{
    KSEMAPHORE Semaphore;
    NTSTATUS RaisedStatus = STATUS_SUCCESS;
    LONG State = -1;

    KeInitializeSemaphore(&Semaphore, 2, 2);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 2L);

    _SEH2_TRY
    {
        State = KeReleaseSemaphore(&Semaphore, IO_NO_INCREMENT, 1, FALSE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        RaisedStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ok_eq_hex(RaisedStatus, STATUS_SEMAPHORE_LIMIT_EXCEEDED);
    ok_eq_long(State, -1L);
    ok_eq_long(KeReadStateSemaphore(&Semaphore), 2L);
}

static KSEMAPHORE WaiterSemaphore;
static KEVENT WaiterDone;
static volatile LONG WaiterStatus;

static
VOID
NTAPI
SemaphoreWaiterThread(
    _In_ PVOID Context)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Context);
    Status = KeWaitForSingleObject(&WaiterSemaphore, Executive, KernelMode, FALSE, NULL);
    InterlockedExchange(&WaiterStatus, (LONG)Status);
    KeSetEvent(&WaiterDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
TestSemaphoreWake(VOID)
{
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    KeInitializeSemaphore(&WaiterSemaphore, 0, 1);
    KeInitializeEvent(&WaiterDone, NotificationEvent, FALSE);
    WaiterStatus = -1;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, SemaphoreWaiterThread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObCloseHandle(ThreadHandle, KernelMode);

    KeReleaseSemaphore(&WaiterSemaphore, IO_NO_INCREMENT, 1, FALSE);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&WaiterDone, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(WaiterStatus, (LONG)STATUS_SUCCESS);
    ok_eq_long(KeReadStateSemaphore(&WaiterSemaphore), 0L);

    if (NT_SUCCESS(Status))
    {
        Status = KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    ObDereferenceObject(ThreadObject);
}

START_TEST(KeSemaphore)
{
    TestSemaphoreBasic();
    TestSemaphoreLimit();
    TestSemaphoreWake();
}
