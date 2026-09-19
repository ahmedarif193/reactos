/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Race process attachment against thread creation and exit.
 */
#include <kmt_test.h>

#define ATTACH_WORKERS 3
#define EXIT_BATCH 8

typedef struct
{
    KEVENT Start;
    PEPROCESS Process;
    volatile LONG Stop;
    volatile LONG Attachments;
} STACK_STRESS;

static VOID NTAPI AttachWorker(PVOID Parameter)
{
    STACK_STRESS *Stress = Parameter;
    KAPC_STATE State;
    ULONG Iteration;

    KeWaitForSingleObject(&Stress->Start, Executive, KernelMode, FALSE, NULL);
    for (Iteration = 0; Iteration < 2000000 && !Stress->Stop; ++Iteration)
    {
        /* Exercise both public attachment forms, with no I/O while attached. */
        if (Iteration & 1)
        {
            KeStackAttachProcess((PKPROCESS)Stress->Process, &State);
            KeStallExecutionProcessor(1);
            KeUnstackDetachProcess(&State);
        }
        else
        {
            KeAttachProcess((PKPROCESS)Stress->Process);
            KeStallExecutionProcessor(1);
            KeDetachProcess();
        }
        InterlockedIncrement(&Stress->Attachments);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI ExitWorker(PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID JoinThread(HANDLE Thread)
{
    NTSTATUS Status = ZwWaitForSingleObject(Thread, FALSE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(Thread);
}

START_TEST(KeProcessStackCount)
{
    STACK_STRESS Stress = {0};
    OBJECT_ATTRIBUTES Attributes;
    HANDLE ProcessHandle = NULL, Attach[ATTACH_WORKERS] = {0}, Batch[EXIT_BATCH];
    ULONG i, Round, Count, Baseline;
    NTSTATUS Status;
    LARGE_INTEGER Delay;
    PKPROCESS Process;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "Requires multiple processors\n");
        return;
    }
    Stress.Process = PsGetCurrentProcess();
    if (Stress.Process == PsInitialSystemProcess)
    {
        skip(FALSE, "Run in the requesting test process\n");
        return;
    }
    Process = (PKPROCESS)Stress.Process;
    Baseline = Process->StackCount;
    KeInitializeEvent(&Stress.Start, NotificationEvent, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ObOpenObjectByPointer(Stress.Process, OBJ_KERNEL_HANDLE, NULL,
                                  PROCESS_CREATE_THREAD, *PsProcessType,
                                  KernelMode, &ProcessHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    for (i = 0; i < ATTACH_WORKERS; ++i)
    {
        Status = PsCreateSystemThread(&Attach[i], THREAD_ALL_ACCESS, &Attributes,
                                      NULL, NULL, AttachWorker, &Stress);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) break;
    }
    KeSetEvent(&Stress.Start, IO_NO_INCREMENT, FALSE);
    for (Round = 0; Round < 128; ++Round)
    {
        Count = 0;
        for (i = 0; i < EXIT_BATCH; ++i)
        {
            Status = PsCreateSystemThread(&Batch[Count], THREAD_ALL_ACCESS,
                                          &Attributes, ProcessHandle, NULL,
                                          ExitWorker, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (!NT_SUCCESS(Status)) break;
            ++Count;
        }
        for (i = 0; i < Count; ++i) JoinThread(Batch[i]);
        if (Count != EXIT_BATCH) break;
    }
    InterlockedExchange(&Stress.Stop, 1);
    for (i = 0; i < ATTACH_WORKERS; ++i)
        if (Attach[i]) JoinThread(Attach[i]);

    /* A thread is signalled before its final stack reference is released. */
    Delay.QuadPart = -100000;
    for (i = 0; i < 100 && Process->StackCount != Baseline; ++i)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok_eq_ulong(Process->StackCount, Baseline);
    ok(Stress.Attachments > 0, "No concurrent process attachments\n");
    trace("%ld attach/detach pairs, %lu thread batches, stack count %lu -> %lu\n",
          Stress.Attachments, Round, Baseline, Process->StackCount);
    ZwClose(ProcessHandle);
}
