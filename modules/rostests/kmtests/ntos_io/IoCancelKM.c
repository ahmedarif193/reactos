/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite IRP cancel API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static volatile LONG CancelCalls;
static PIRP CancelledIrp;

static
VOID
NTAPI
TestCancelRoutine(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    ok(DeviceObject == NULL, "unexpected device %p\n", DeviceObject);
    CancelledIrp = Irp;
    InterlockedIncrement(&CancelCalls);
    IoReleaseCancelSpinLock(Irp->CancelIrql);
}

static
VOID
TestIrpAllocation(VOID)
{
    PIRP Irp;

    Irp = IoAllocateIrp(2, FALSE);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (Irp == NULL) return;

    ok_eq_uint(Irp->StackCount, 2);
    ok_eq_uint(Irp->CurrentLocation, 3);
    ok_eq_uint(Irp->Type, IO_TYPE_IRP);
    ok_bool_false(Irp->Cancel, "fresh irp cancel flag");
    ok_eq_pointer(Irp->CancelRoutine, NULL);

    IoFreeIrp(Irp);
}

static
VOID
TestCancel(VOID)
{
    PIRP Irp;
    PDRIVER_CANCEL OldRoutine;
    BOOLEAN Result;

    Irp = IoAllocateIrp(1, FALSE);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (Irp == NULL) return;

    CancelCalls = 0;
    CancelledIrp = NULL;

    OldRoutine = IoSetCancelRoutine(Irp, TestCancelRoutine);
    ok_eq_pointer(OldRoutine, NULL);

    Result = IoCancelIrp(Irp);
    ok_bool_true(Result, "IoCancelIrp with routine");
    ok_eq_long(CancelCalls, 1L);
    ok_eq_pointer(CancelledIrp, Irp);
    ok_bool_true(Irp->Cancel, "cancel flag set");
    ok_eq_pointer(Irp->CancelRoutine, NULL);

    Result = IoCancelIrp(Irp);
    ok_bool_false(Result, "IoCancelIrp without routine");
    ok_eq_long(CancelCalls, 1L);

    IoFreeIrp(Irp);
}

static
VOID
TestCancelNoRoutine(VOID)
{
    PIRP Irp;
    BOOLEAN Result;

    Irp = IoAllocateIrp(1, FALSE);
    ok(Irp != NULL, "IoAllocateIrp failed\n");
    if (Irp == NULL) return;

    Result = IoCancelIrp(Irp);
    ok_bool_false(Result, "IoCancelIrp on fresh irp");
    ok_bool_true(Irp->Cancel, "cancel flag still set");

    IoFreeIrp(Irp);
}

START_TEST(IoCancelKM)
{
    TestIrpAllocation();
    TestCancel();
    TestCancelNoRoutine();
}
