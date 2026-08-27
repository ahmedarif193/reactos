/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         PsTls native-parity tests
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef struct _TEST_TLS_WORKER_CONTEXT
{
    KEVENT ReadyEvent;
    KEVENT ReleaseEvent;
    ULONG TlsIndex;
    PVOID Value;
    NTSTATUS SetStatus;
    BOOLEAN WaitForRelease;
} TEST_TLS_WORKER_CONTEXT, *PTEST_TLS_WORKER_CONTEXT;

NTSTATUS
NTAPI
PsTlsAlloc(
    _In_opt_ PVOID Callback,
    _In_ ULONG Flags,
    _Out_ PULONG TlsIndex);

VOID
NTAPI
PsTlsFree(
    _In_ ULONG TlsIndex);

NTSTATUS
NTAPI
PsTlsGetValue(
    _In_ ULONG TlsIndex,
    _Out_ PVOID *Value);

NTSTATUS
NTAPI
PsTlsSetValue(
    _In_ ULONG TlsIndex,
    _In_opt_ PVOID Value);

static volatile LONG TestTlsCallbackCount;
static PVOID TestTlsCallbackValues[4];
static KIRQL TestTlsCallbackIrql;

static
VOID
NTAPI
TestTlsCallback(
    _In_opt_ PVOID Value)
{
    LONG Index;

    Index = InterlockedIncrement(&TestTlsCallbackCount) - 1;
    if ((ULONG)Index < RTL_NUMBER_OF(TestTlsCallbackValues))
        TestTlsCallbackValues[Index] = Value;
    TestTlsCallbackIrql = KeGetCurrentIrql();
}

static
VOID
NTAPI
TestTlsWorker(
    _In_ PVOID Parameter)
{
    PTEST_TLS_WORKER_CONTEXT Context = Parameter;

    Context->SetStatus = PsTlsSetValue(Context->TlsIndex, Context->Value);
    KeSetEvent(&Context->ReadyEvent, IO_NO_INCREMENT, FALSE);
    if (Context->WaitForRelease)
        KeWaitForSingleObject(&Context->ReleaseEvent, Executive, KernelMode, FALSE, NULL);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
BOOLEAN
TestTlsSawCallbackValue(
    _In_ PVOID Value)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(TestTlsCallbackValues); Index++)
    {
        if (TestTlsCallbackValues[Index] == Value)
            return TRUE;
    }

    return FALSE;
}

START_TEST(PsTls)
{
    const PVOID TestValue = (PVOID)(ULONG_PTR)0x12345678;
    const PVOID MainValue = (PVOID)(ULONG_PTR)0x11111111;
    const PVOID WorkerValue = (PVOID)(ULONG_PTR)0x22222222;
    TEST_TLS_WORKER_CONTEXT WorkerContext;
    HANDLE ThreadHandle;
    PVOID Value;
    ULONG CallbackTlsIndex;
    ULONG TlsIndex;
    NTSTATUS Status;

    Value = TestValue;
    Status = PsTlsGetValue(0, &Value);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_pointer(Value, TestValue);
    Status = PsTlsSetValue(MAXULONG, TestValue);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    TlsIndex = MAXULONG;
    Status = PsTlsAlloc(NULL, 1, &TlsIndex);
    trace("PsTlsAlloc(flags 1) returned 0x%08lx, index %lu\n", Status, TlsIndex);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_ulong(TlsIndex, MAXULONG);

    TlsIndex = MAXULONG;
    Status = PsTlsAlloc(NULL, 0, &TlsIndex);
    trace("PsTlsAlloc returned 0x%08lx, index %lu\n", Status, TlsIndex);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Value = TestValue;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(initial) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, NULL);

    Status = PsTlsSetValue(TlsIndex, TestValue);
    trace("PsTlsSetValue returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Value = NULL;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(stored) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, TestValue);

    PsTlsFree(TlsIndex);

    Value = TestValue;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(freed) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, NULL);

    Status = PsTlsSetValue(TlsIndex, TestValue);
    trace("PsTlsSetValue(freed) returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Value = NULL;
    Status = PsTlsGetValue(TlsIndex, &Value);
    trace("PsTlsGetValue(unallocated stored) returned 0x%08lx, value %p\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, TestValue);

    Status = PsTlsSetValue(TlsIndex, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(TestTlsCallbackValues, sizeof(TestTlsCallbackValues));
    TestTlsCallbackCount = 0;
    TestTlsCallbackIrql = HIGH_LEVEL;
    CallbackTlsIndex = MAXULONG;
    Status = PsTlsAlloc(TestTlsCallback, 0, &CallbackTlsIndex);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = PsTlsSetValue(CallbackTlsIndex, (PVOID)MainValue);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(&WorkerContext, sizeof(WorkerContext));
    KeInitializeEvent(&WorkerContext.ReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&WorkerContext.ReleaseEvent, NotificationEvent, FALSE);
    WorkerContext.TlsIndex = CallbackTlsIndex;
    WorkerContext.Value = (PVOID)WorkerValue;
    WorkerContext.SetStatus = STATUS_UNSUCCESSFUL;
    WorkerContext.WaitForRelease = TRUE;
    ThreadHandle = NULL;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, TestTlsWorker, &WorkerContext);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        PsTlsFree(CallbackTlsIndex);
        return;
    }

    Status = KeWaitForSingleObject(&WorkerContext.ReadyEvent, Executive, KernelMode, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(WorkerContext.SetStatus, STATUS_SUCCESS);
    Value = NULL;
    Status = PsTlsGetValue(CallbackTlsIndex, &Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, MainValue);

    PsTlsFree(CallbackTlsIndex);
    ok_eq_long(TestTlsCallbackCount, 2);
    ok_bool_true(TestTlsSawCallbackValue((PVOID)MainValue), "main-thread TLS callback value");
    ok_bool_true(TestTlsSawCallbackValue((PVOID)WorkerValue), "worker-thread TLS callback value");
    ok(TestTlsCallbackIrql <= APC_LEVEL, "TLS callback ran at IRQL %u\n", TestTlsCallbackIrql);
    Value = TestValue;
    Status = PsTlsGetValue(CallbackTlsIndex, &Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Value, NULL);

    KeSetEvent(&WorkerContext.ReleaseEvent, IO_NO_INCREMENT, FALSE);
    Status = ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);
    ok_eq_long(TestTlsCallbackCount, 2);

    RtlZeroMemory(TestTlsCallbackValues, sizeof(TestTlsCallbackValues));
    TestTlsCallbackCount = 0;
    TestTlsCallbackIrql = HIGH_LEVEL;
    CallbackTlsIndex = MAXULONG;
    Status = PsTlsAlloc(TestTlsCallback, 0, &CallbackTlsIndex);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(&WorkerContext, sizeof(WorkerContext));
    KeInitializeEvent(&WorkerContext.ReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&WorkerContext.ReleaseEvent, NotificationEvent, FALSE);
    WorkerContext.TlsIndex = CallbackTlsIndex;
    WorkerContext.Value = (PVOID)WorkerValue;
    WorkerContext.SetStatus = STATUS_UNSUCCESSFUL;
    ThreadHandle = NULL;
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, TestTlsWorker, &WorkerContext);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        PsTlsFree(CallbackTlsIndex);
        return;
    }

    Status = ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(ThreadHandle);
    ok_eq_hex(WorkerContext.SetStatus, STATUS_SUCCESS);
    ok_eq_long(TestTlsCallbackCount, 1);
    ok_eq_pointer(TestTlsCallbackValues[0], WorkerValue);
    ok(TestTlsCallbackIrql <= APC_LEVEL, "thread-exit TLS callback ran at IRQL %u\n", TestTlsCallbackIrql);
    PsTlsFree(CallbackTlsIndex);
    ok_eq_long(TestTlsCallbackCount, 1);
}
