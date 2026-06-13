/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite kernel APC injection API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT ApcFiredEvent;
static KEVENT ThreadReadyEvent;
static KEVENT ThreadGoEvent;
static volatile LONG NormalApcRuns;
static volatile LONG RundownApcRuns;
static PKTHREAD ApcTargetThread;
static KIRQL ApcFireIrql;

static
VOID
NTAPI
NormalApcRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ExFreePoolWithTag(Apc, 'cApK');
}

static
VOID
NTAPI
NormalRoutine(
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ok_eq_pointer(NormalContext, (PVOID)(ULONG_PTR)0xA9C0);
    ok_eq_pointer(SystemArgument1, (PVOID)(ULONG_PTR)0x1111);
    ok_eq_pointer(SystemArgument2, (PVOID)(ULONG_PTR)0x2222);
    ApcFireIrql = KeGetCurrentIrql();
    ok_eq_pointer(KeGetCurrentThread(), ApcTargetThread);
    InterlockedIncrement(&NormalApcRuns);
    KeSetEvent(&ApcFiredEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
ApcTargetThreadRoutine(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    ApcTargetThread = KeGetCurrentThread();
    KeSetEvent(&ThreadReadyEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&ThreadGoEvent, Executive, KernelMode, TRUE, NULL);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
TestNormalApcInjection(VOID)
{
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    PKAPC Apc;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;
    BOOLEAN Inserted;

    KeInitializeEvent(&ApcFiredEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&ThreadReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&ThreadGoEvent, NotificationEvent, FALSE);
    NormalApcRuns = 0;
    ApcTargetThread = NULL;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, ApcTargetThreadRoutine, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ObCloseHandle(ThreadHandle, KernelMode);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&ThreadReadyEvent, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(ApcTargetThread != NULL, "target thread not set\n");

    Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Apc), 'cApK');
    ok(Apc != NULL, "no pool for APC\n");
    if (Apc != NULL && ApcTargetThread != NULL)
    {
        KeInitializeApc(Apc, ApcTargetThread, OriginalApcEnvironment, NormalApcRoutine, NULL, NormalRoutine, KernelMode, (PVOID)(ULONG_PTR)0xA9C0);
        Inserted = KeInsertQueueApc(Apc, (PVOID)(ULONG_PTR)0x1111, (PVOID)(ULONG_PTR)0x2222, IO_NO_INCREMENT);
        ok_bool_true(Inserted, "KeInsertQueueApc");

        Status = KeWaitForSingleObject(&ApcFiredEvent, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(NormalApcRuns, 1L);
        ok_eq_uint(ApcFireIrql, PASSIVE_LEVEL);
    }

    KeSetEvent(&ThreadGoEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);
    ObDereferenceObject(ThreadObject);
}

static
VOID
NTAPI
RundownApcRoutine(
    _In_ PKAPC Apc)
{
    InterlockedIncrement(&RundownApcRuns);
    ExFreePoolWithTag(Apc, 'cApK');
}

static
VOID
NTAPI
NoopNormalRoutine(
    _In_opt_ PVOID NormalContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

static
VOID
TestApcRundown(VOID)
{
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    PKAPC Apc;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    KeInitializeEvent(&ThreadReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&ThreadGoEvent, NotificationEvent, FALSE);
    RundownApcRuns = 0;
    ApcTargetThread = NULL;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, ApcTargetThreadRoutine, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ObCloseHandle(ThreadHandle, KernelMode);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    KeWaitForSingleObject(&ThreadReadyEvent, Executive, KernelMode, FALSE, &Timeout);

    KeSetEvent(&ThreadGoEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(ThreadObject, Executive, KernelMode, FALSE, &Timeout);

    Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Apc), 'cApK');
    if (Apc != NULL && ApcTargetThread != NULL)
    {
        KeInitializeApc(Apc, ApcTargetThread, OriginalApcEnvironment, RundownApcRoutine, NULL, NoopNormalRoutine, KernelMode, NULL);
        if (!KeInsertQueueApc(Apc, NULL, NULL, IO_NO_INCREMENT))
            ExFreePoolWithTag(Apc, 'cApK');
    }

    ObDereferenceObject(ThreadObject);
}

START_TEST(KeApcInject)
{
    TestNormalApcInjection();
    TestApcRundown();
}
