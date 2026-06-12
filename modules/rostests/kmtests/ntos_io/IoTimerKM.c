/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite IoInitializeTimer API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT TimerTicked;
static volatile LONG TimerTicks;
static KIRQL TimerIrql;

static
VOID
NTAPI
IoTimerRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    ok(DeviceObject != NULL, "DeviceObject NULL in timer\n");
    ok_eq_pointer(Context, (PVOID)(ULONG_PTR)0xCAFE);
    TimerIrql = KeGetCurrentIrql();
    if (InterlockedIncrement(&TimerTicks) >= 2)
        KeSetEvent(&TimerTicked, IO_NO_INCREMENT, FALSE);
}

START_TEST(IoTimerKM)
{
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;

    Status = IoCreateDevice(KmtDriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    KeInitializeEvent(&TimerTicked, NotificationEvent, FALSE);
    TimerTicks = 0;

    Status = IoInitializeTimer(DeviceObject, IoTimerRoutine, (PVOID)(ULONG_PTR)0xCAFE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        IoStartTimer(DeviceObject);

        Timeout.QuadPart = -10 * 1000 * 1000 * 10;
        Status = KeWaitForSingleObject(&TimerTicked, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(TimerTicks >= 2, "timer ticked %ld times\n", TimerTicks);
        ok_eq_uint(TimerIrql, DISPATCH_LEVEL);

        IoStopTimer(DeviceObject);
        Timeout.QuadPart = -10 * 1000 * 1000 * 2;
        KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
        TimerTicks = 0;
        Timeout.QuadPart = -10 * 1000 * 1000 * 2;
        KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
        ok(TimerTicks <= 1, "timer still ticking after stop: %ld\n", TimerTicks);
    }

    IoDeleteDevice(DeviceObject);
}
