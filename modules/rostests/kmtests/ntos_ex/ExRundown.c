/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite rundown protection API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestRundownBasic(VOID)
{
    EX_RUNDOWN_REF Ref;
    BOOLEAN Acquired;

    ExInitializeRundownProtection(&Ref);

    Acquired = ExAcquireRundownProtection(&Ref);
    ok_bool_true(Acquired, "acquire on fresh ref");
    ExReleaseRundownProtection(&Ref);

    Acquired = ExAcquireRundownProtectionEx(&Ref, 3);
    ok_bool_true(Acquired, "acquire ex count 3");
    ExReleaseRundownProtectionEx(&Ref, 2);
    ExReleaseRundownProtection(&Ref);

    ExWaitForRundownProtectionRelease(&Ref);

    Acquired = ExAcquireRundownProtection(&Ref);
    ok_bool_false(Acquired, "acquire after rundown");

    ExReInitializeRundownProtection(&Ref);
    Acquired = ExAcquireRundownProtection(&Ref);
    ok_bool_true(Acquired, "acquire after reinit");
    ExReleaseRundownProtection(&Ref);

    ExRundownCompleted(&Ref);
    Acquired = ExAcquireRundownProtection(&Ref);
    ok_bool_false(Acquired, "acquire after completed");
}

static EX_RUNDOWN_REF SharedRef;
static KEVENT RundownFinished;
static volatile LONG RundownDone;

static
VOID
NTAPI
RundownWaiterThread(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    ExWaitForRundownProtectionRelease(&SharedRef);
    InterlockedExchange(&RundownDone, 1);
    KeSetEvent(&RundownFinished, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
TestRundownBlocking(VOID)
{
    HANDLE ThreadHandle;
    PVOID Thread;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;
    BOOLEAN Acquired;

    ExInitializeRundownProtection(&SharedRef);
    KeInitializeEvent(&RundownFinished, NotificationEvent, FALSE);
    RundownDone = 0;

    Acquired = ExAcquireRundownProtection(&SharedRef);
    ok_bool_true(Acquired, "holder acquire");

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, RundownWaiterThread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseRundownProtection(&SharedRef);
        return;
    }
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &Thread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObCloseHandle(ThreadHandle, KernelMode);

    Timeout.QuadPart = -10 * 1000 * 200;
    Status = KeWaitForSingleObject(&RundownFinished, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_TIMEOUT);
    ok_eq_long(RundownDone, 0L);

    ExReleaseRundownProtection(&SharedRef);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&RundownFinished, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(RundownDone, 1L);

    Status = KeWaitForSingleObject(Thread, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObDereferenceObject(Thread);
}

START_TEST(ExRundown)
{
    TestRundownBasic();
    TestRundownBlocking();
}
