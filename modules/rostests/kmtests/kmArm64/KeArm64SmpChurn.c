/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 SMP scheduler churn stress
 *
 * Spawns worker threads that block, wake and migrate across processors to
 * stress the context-switch and ready-queue paths. A scheduler race shows up
 * as a bugcheck or a corrupted dispatcher object during the run.
 */

#include <kmt_test.h>

VOID Test_KeArm64SmpChurn(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

#define CHURN_THREADS 16
#define CHURN_MS      2000

static volatile LONG ChurnRun;
static volatile LONG ChurnIterations;

static VOID NTAPI ChurnRoutine(IN PVOID Context)
{
    ULONG Index = (ULONG)(ULONG_PTR)Context;
    ULONG Processors = KeNumberProcessors;

    while (ChurnRun)
    {
        LARGE_INTEGER Delay;

        KeSetSystemAffinityThread((KAFFINITY)1 << (Index % Processors));
        Delay.QuadPart = -((LONGLONG)((Index & 7) + 1) * 10000LL);
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        KeRevertToUserAffinityThread();
        Delay.QuadPart = -10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        InterlockedIncrement(&ChurnIterations);
    }
}

static VOID Arm64SmpChurn(VOID)
{
    HANDLE Handles[CHURN_THREADS];
    PVOID Objects[CHURN_THREADS];
    ULONG Created = 0;
    ULONG i;
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "Uniprocessor -- nothing to churn\n");
        return;
    }

    InterlockedExchange(&ChurnRun, 1);
    InterlockedExchange(&ChurnIterations, 0);

    for (i = 0; i < CHURN_THREADS; i++)
    {
        Status = PsCreateSystemThread(&Handles[i], THREAD_ALL_ACCESS, NULL,
                                      NULL, NULL, ChurnRoutine,
                                      (PVOID)(ULONG_PTR)i);
        if (!NT_SUCCESS(Status))
            break;
        Created++;
    }

    ok(Created == CHURN_THREADS, "Created %lu/%u churn threads\n",
       Created, CHURN_THREADS);

    for (i = 0; i < Created; i++)
        Objects[i] = NULL;
    for (i = 0; i < Created; i++)
    {
        ObReferenceObjectByHandle(Handles[i], SYNCHRONIZE, NULL, KernelMode,
                                  &Objects[i], NULL);
    }

    Delay.QuadPart = -((LONGLONG)CHURN_MS * 10000LL);
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    InterlockedExchange(&ChurnRun, 0);

    for (i = 0; i < Created; i++)
    {
        if (Objects[i] != NULL)
        {
            KeWaitForSingleObject(Objects[i], Executive, KernelMode, FALSE, NULL);
            ObDereferenceObject(Objects[i]);
        }
        ZwClose(Handles[i]);
    }

    ok(ChurnIterations > 0, "Churn made no progress\n");
    dump_trace("[arm64][KeArm64SmpChurn] survived %ld iterations on %lu cpus\n",
               (LONG)ChurnIterations, KeNumberProcessors);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64SmpChurn)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64SmpChurn is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64SmpChurn] enter\n");
    Arm64SmpChurn();
#endif
}
