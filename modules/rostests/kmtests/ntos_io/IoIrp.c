/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Io Regressions KM-Test (Irp)
 * PROGRAMMER:      Aleksey Bragin <aleksey@reactos.org>
 */
/* Based on code Copyright 2008 Etersoft (Alexander Morozov) */

#include <kmt_test.h>
#include <kmt_public.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestSynchronousCompletion(VOID)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    UCHAR Output[128];
    PIRP Irp;
    NTSTATUS Status;
    ULONG Index, Late = 0, BadStatus = 0, BadOutput = 0;

    /* Match a file system's synchronous buffered IOCTL, including its normal
     * APC exclusion. Special completion APCs must still finish before a
     * non-pending dispatch returns and its stack event goes out of scope. */
    KeEnterCriticalRegion();
    for (Index = 0; Index < 4096; Index++)
    {
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        IoStatus.Status = STATUS_PENDING;
        IoStatus.Information = 0;
        Output[0] = 0;
        Irp = IoBuildDeviceIoControlRequest(IOCTL_KMTEST_GET_TESTS,
                                           KmtDriverObject->DeviceObject,
                                           NULL, 0, Output, sizeof(Output),
                                           FALSE, &Event, &IoStatus);
        if (!Irp) break;
        Status = IoCallDriver(KmtDriverObject->DeviceObject, Irp);
        if (Status != STATUS_PENDING && !KeReadStateEvent(&Event)) Late++;
        /* Drain completion before reusing the event, IOSB and output buffer
         * so the regression reports a failure without corrupting the stack. */
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        if (Status != STATUS_SUCCESS || IoStatus.Status != STATUS_SUCCESS) BadStatus++;
        if (!IoStatus.Information || !Output[0]) BadOutput++;
    }
    KeLeaveCriticalRegion();
    ok_eq_ulong(Index, 4096);
    ok_eq_ulong(Late, 0);
    ok_eq_ulong(BadStatus, 0);
    ok_eq_ulong(BadOutput, 0);
}

START_TEST(IoIrp)
{
    USHORT size;
    IRP *iorp;

    TestSynchronousCompletion();

    // 1st test
    size = sizeof(IRP) + 5 * sizeof(IO_STACK_LOCATION);
    iorp = ExAllocatePool(NonPagedPool, size);

    if (NULL != iorp)
    {
        IoInitializeIrp(iorp, size, 5);

        ok(6 == iorp->Type, "Irp type should be 6, but got %d\n", iorp->Type);
        ok(iorp->Size == size, "Irp size should be %d, but got %d\n",
            iorp->Size, size);
        ok(5 == iorp->StackCount, "Irp StackCount should be 5, but got %d\n",
            iorp->StackCount);
        ok(6 == iorp->CurrentLocation, "Irp CurrentLocation should be 6, but got %d\n",
            iorp->CurrentLocation);
        ok(IsListEmpty(&iorp->ThreadListEntry), "IRP thread list is not empty\n");
        ok ((PIO_STACK_LOCATION)(iorp + 1) + 5 ==
            iorp->Tail.Overlay.CurrentStackLocation,
            "CurrentStackLocation mismatch\n");

        ExFreePool(iorp);
    }

    // 2nd test
    size = sizeof(IRP) + 2 * sizeof(IO_STACK_LOCATION);
    iorp = IoAllocateIrp(2, FALSE);

    if (NULL != iorp)
    {
        ok(6 == iorp->Type, "Irp type should be 6, but got %d\n", iorp->Type);
        ok(iorp->Size >= size,
            "Irp size should be more or equal to %d, but got %d\n",
            iorp->Size, size);
        ok(2 == iorp->StackCount, "Irp StackCount should be 2, but got %d\n",
            iorp->StackCount);
        ok(3 == iorp->CurrentLocation, "Irp CurrentLocation should be 3, but got %d\n",
            iorp->CurrentLocation);
        ok(IsListEmpty(&iorp->ThreadListEntry), "IRP thread list is not empty\n");
        ok ((PIO_STACK_LOCATION)(iorp + 1) + 2 ==
            iorp->Tail.Overlay.CurrentStackLocation,
            "CurrentStackLocation mismatch\n");
        ok((IRP_ALLOCATED_FIXED_SIZE & iorp->AllocationFlags),
            "IRP Allocation flags lack fixed size attribute\n");
        ok(!(IRP_LOOKASIDE_ALLOCATION & iorp->AllocationFlags),
            "IRP Allocation flags should not have lookaside allocation\n");

        IoFreeIrp(iorp);
    }

    // 3rd test
    size = sizeof(IRP) + 2 * sizeof(IO_STACK_LOCATION);
    iorp = IoAllocateIrp(2, TRUE);

    if (NULL != iorp)
    {
        ok(6 == iorp->Type, "Irp type should be 6, but got %d\n", iorp->Type);
        ok(iorp->Size >= size,
            "Irp size should be more or equal to %d, but got %d\n",
            iorp->Size, size);
        ok(2 == iorp->StackCount, "Irp StackCount should be 2, but got %d\n",
            iorp->StackCount);
        ok(3 == iorp->CurrentLocation, "Irp CurrentLocation should be 3, but got %d\n",
            iorp->CurrentLocation);
        ok(IsListEmpty(&iorp->ThreadListEntry), "IRP thread list is not empty\n");
        ok ((PIO_STACK_LOCATION)(iorp + 1) + 2 ==
            iorp->Tail.Overlay.CurrentStackLocation,
            "CurrentStackLocation mismatch\n");
        ok((IRP_ALLOCATED_FIXED_SIZE & iorp->AllocationFlags),
            "IRP Allocation flags lack fixed size attribute\n");
        if (GetNTVersion() < _WIN32_WINNT_VISTA)
        {
            ok((IRP_LOOKASIDE_ALLOCATION & iorp->AllocationFlags),
                "IRP Allocation flags lack lookaside allocation\n");
        }
        else if (!(IRP_LOOKASIDE_ALLOCATION & iorp->AllocationFlags))
        {
            skip(FALSE, "Vista+ may allocate charged IRPs without the lookaside flag\n");
        }

        IoFreeIrp(iorp);
    }
}
