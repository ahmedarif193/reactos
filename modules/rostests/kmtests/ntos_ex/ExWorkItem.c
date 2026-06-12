/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite work item API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT WorkDone;
static volatile LONG WorkRuns;
static PETHREAD WorkThread;
static KIRQL WorkIrql;

static WORK_QUEUE_ITEM ExItem;

static
VOID
NTAPI
ExWorkRoutine(
    _In_ PVOID Parameter)
{
    ok_eq_pointer(Parameter, (PVOID)(ULONG_PTR)0xBEEF);
    WorkIrql = KeGetCurrentIrql();
    WorkThread = PsGetCurrentThread();
    InterlockedIncrement(&WorkRuns);
    KeSetEvent(&WorkDone, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestExQueueWorkItem(VOID)
{
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    KeInitializeEvent(&WorkDone, NotificationEvent, FALSE);
    WorkRuns = 0;
    WorkThread = NULL;

    ExInitializeWorkItem(&ExItem, ExWorkRoutine, (PVOID)(ULONG_PTR)0xBEEF);
    ExQueueWorkItem(&ExItem, DelayedWorkQueue);

    Timeout.QuadPart = -10 * 1000 * 1000 * 10;
    Status = KeWaitForSingleObject(&WorkDone, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(WorkRuns, 1L);
    ok_eq_uint(WorkIrql, PASSIVE_LEVEL);
    ok(WorkThread != PsGetCurrentThread(), "work ran on caller thread\n");
    ok_bool_true(PsIsSystemThread(WorkThread), "worker is system thread");
}

static PIO_WORKITEM IoItem;

static
VOID
NTAPI
IoWorkRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    ok(DeviceObject != NULL, "DeviceObject NULL in work routine\n");
    ok_eq_pointer(Context, (PVOID)(ULONG_PTR)0xF00D);
    WorkIrql = KeGetCurrentIrql();
    InterlockedIncrement(&WorkRuns);
    KeSetEvent(&WorkDone, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestIoWorkItem(VOID)
{
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    IoItem = IoAllocateWorkItem(DeviceObject);
    ok(IoItem != NULL, "IoAllocateWorkItem failed\n");
    if (IoItem != NULL)
    {
        KeInitializeEvent(&WorkDone, NotificationEvent, FALSE);
        WorkRuns = 0;

        IoQueueWorkItem(IoItem, IoWorkRoutine, CriticalWorkQueue, (PVOID)(ULONG_PTR)0xF00D);

        Timeout.QuadPart = -10 * 1000 * 1000 * 10;
        Status = KeWaitForSingleObject(&WorkDone, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_long(WorkRuns, 1L);
        ok_eq_uint(WorkIrql, PASSIVE_LEVEL);

        IoFreeWorkItem(IoItem);
    }

    IoDeleteDevice(DeviceObject);
}

START_TEST(ExWorkItem)
{
    TestExQueueWorkItem();
    TestIoWorkItem();
}
