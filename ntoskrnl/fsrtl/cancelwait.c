/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Cancellable waits for file systems and filters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
VOID
NTAPI
FsRtlpCancelWaitRoutine(IN PDEVICE_OBJECT DeviceObject,
                        IN PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    KeAlertThread(&Irp->Tail.Overlay.Thread->Tcb, KernelMode);
    IoReleaseCancelSpinLock(Irp->CancelIrql);
}

NTSTATUS
NTAPI
FsRtlCancellableWaitForMultipleObjects(IN ULONG Count,
                                       IN PVOID ObjectArray[],
                                       IN WAIT_TYPE WaitType,
                                       IN PLARGE_INTEGER Timeout OPTIONAL,
                                       IN PKWAIT_BLOCK WaitBlockArray OPTIONAL,
                                       IN PIRP Irp OPTIONAL)
{
    PETHREAD Thread = PsGetCurrentThread();
    LARGE_INTEGER Remaining, *WaitTimeout = Timeout;
    ULONGLONG Start = KeQueryInterruptTime();
    BOOLEAN Armed = FALSE, Alertable;
    NTSTATUS Status;
    KIRQL OldIrql;

    if (Irp != NULL && Irp->Tail.Overlay.Thread == Thread)
    {
        IoAcquireCancelSpinLock(&OldIrql);
        if (Irp->Cancel)
        {
            IoReleaseCancelSpinLock(OldIrql);
            return STATUS_CANCELLED;
        }
        IoSetCancelRoutine(Irp, FsRtlpCancelWaitRoutine);
        IoReleaseCancelSpinLock(OldIrql);
        Armed = TRUE;
    }
    else if (Irp != NULL && Irp->Cancel)
    {
        return STATUS_CANCELLED;
    }

    Alertable = Armed;

    for (;;)
    {
        Status = KeWaitForMultipleObjects(Count,
                                          ObjectArray,
                                          WaitType,
                                          Executive,
                                          UserMode,
                                          Alertable,
                                          WaitTimeout,
                                          WaitBlockArray);
        if (Status != STATUS_ALERTED && Status != STATUS_USER_APC)
        {
            break;
        }

        if (PsIsThreadTerminating(Thread))
        {
            Status = STATUS_THREAD_IS_TERMINATING;
            break;
        }
        if (Irp != NULL && Irp->Cancel)
        {
            Status = STATUS_CANCELLED;
            break;
        }
        if (Status == STATUS_USER_APC)
        {
            Alertable = FALSE;
        }

        if (Timeout != NULL && Timeout->QuadPart < 0)
        {
            Remaining.QuadPart = Timeout->QuadPart + (LONGLONG)(KeQueryInterruptTime() - Start);
            if (Remaining.QuadPart >= 0)
            {
                Status = STATUS_TIMEOUT;
                break;
            }
            WaitTimeout = &Remaining;
        }
    }

    if (Armed)
    {
        if (IoSetCancelRoutine(Irp, NULL) == NULL)
        {
            IoAcquireCancelSpinLock(&OldIrql);
            IoReleaseCancelSpinLock(OldIrql);
            KeTestAlertThread(KernelMode);
        }
    }

    return Status;
}

NTSTATUS
NTAPI
FsRtlCancellableWaitForSingleObject(IN PVOID Object,
                                    IN PLARGE_INTEGER Timeout OPTIONAL,
                                    IN PIRP Irp OPTIONAL)
{
    return FsRtlCancellableWaitForMultipleObjects(1, &Object, WaitAny, Timeout, NULL, Irp);
}
