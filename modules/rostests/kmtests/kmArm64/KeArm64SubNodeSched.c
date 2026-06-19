/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 scheduler subnode topology under churn
 */

#include <kmt_test.h>

VOID Test_KeArm64SubNodeSched(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

#define CHURN_THREADS 16
#define SAMPLE_ROUNDS 8
#define ROUND_MS      250

static volatile LONG SubNodeChurnRun;

static VOID NTAPI SubNodeChurnRoutine(IN PVOID Context)
{
    ULONG Index = (ULONG)(ULONG_PTR)Context;
    ULONG Processors = KeNumberProcessors;

    while (SubNodeChurnRun)
    {
        LARGE_INTEGER Delay;

        KeSetSystemAffinityThread((KAFFINITY)1 << (Index % Processors));
        Delay.QuadPart = -((LONGLONG)((Index & 7) + 1) * 10000LL);
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        KeRevertToUserAffinityThread();
        Delay.QuadPart = -10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

static VOID Arm64SubNodeCheckCpu(PKSCHEDULER_SUBNODE Expected)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKSCHEDULER_SUBNODE SubNode;
    ULONG MyNumber = KeGetCurrentProcessorNumber();
    KAFFINITY Active = KeQueryActiveProcessors();

    ok(Prcb != NULL, "Prcb NULL\n");
    if (!Prcb)
        return;

    SubNode = (PKSCHEDULER_SUBNODE)Prcb->SchedulerSubNode;
    ok(SubNode != NULL, "cpu %lu SchedulerSubNode NULL\n", MyNumber);
    if (!SubNode)
        return;

    ok((ULONG_PTR)SubNode >= 0xFFFF000000000000ULL,
       "cpu %lu SchedulerSubNode=%p not a kernel address\n", MyNumber, SubNode);
    ok(SubNode == Expected,
       "cpu %lu subnode=%p != shared %p\n", MyNumber, SubNode, Expected);

    ok((SubNode->Affinity & ((KAFFINITY)1 << MyNumber)) != 0,
       "cpu %lu not in subnode Affinity=0x%I64x\n",
       MyNumber, (ULONGLONG)SubNode->Affinity);
    ok((SubNode->Affinity & Active) == Active,
       "subnode Affinity=0x%I64x missing active=0x%I64x\n",
       (ULONGLONG)SubNode->Affinity, (ULONGLONG)Active);
    ok_eq_uint(SubNode->SubNodeNumber, 0);
    ok_eq_uint(SubNode->ParentNodeNumber, 0);
    ok_eq_ulong(SubNode->Lowest, 0u);
    ok(SubNode->Highest < (ULONG)KeNumberProcessors,
       "Highest=%lu >= KeNumberProcessors=%lu\n",
       SubNode->Highest, (ULONG)KeNumberProcessors);

    ok((SubNode->IdleCpuSet & ~SubNode->Affinity) == 0,
       "IdleCpuSet=0x%I64x has bits outside Affinity=0x%I64x\n",
       (ULONGLONG)SubNode->IdleCpuSet, (ULONGLONG)SubNode->Affinity);
    ok(SubNode->IdleSmtSet == SubNode->IdleCpuSet,
       "IdleSmtSet=0x%I64x != IdleCpuSet=0x%I64x\n",
       (ULONGLONG)SubNode->IdleSmtSet, (ULONGLONG)SubNode->IdleCpuSet);
    ok((SubNode->SiblingMask & ~SubNode->Affinity) == 0,
       "SiblingMask=0x%I64x outside Affinity=0x%I64x\n",
       (ULONGLONG)SubNode->SiblingMask, (ULONGLONG)SubNode->Affinity);
}

static VOID Arm64SubNodeSched(VOID)
{
    HANDLE Handles[CHURN_THREADS];
    PVOID Objects[CHURN_THREADS];
    ULONG Created = 0;
    ULONG i;
    ULONG Cpu;
    ULONG Round;
    ULONG Processors = KeNumberProcessors;
    PKSCHEDULER_SUBNODE Shared;
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    Shared = (PKSCHEDULER_SUBNODE)KeGetCurrentPrcb()->SchedulerSubNode;
    ok(Shared != NULL, "SchedulerSubNode NULL on the boot CPU\n");
    if (!Shared)
    {
        skip(FALSE, "Scheduler subnode topology not populated\n");
        return;
    }

    if (Processors < 2)
    {
        Arm64SubNodeCheckCpu(Shared);
        skip(FALSE, "Uniprocessor -- no cross-CPU churn\n");
        return;
    }

    InterlockedExchange(&SubNodeChurnRun, 1);

    for (i = 0; i < CHURN_THREADS; i++)
    {
        Status = PsCreateSystemThread(&Handles[i], THREAD_ALL_ACCESS, NULL,
                                      NULL, NULL, SubNodeChurnRoutine,
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
        ObReferenceObjectByHandle(Handles[i], SYNCHRONIZE, NULL, KernelMode,
                                  &Objects[i], NULL);

    for (Round = 0; Round < SAMPLE_ROUNDS; Round++)
    {
        for (Cpu = 0; Cpu < Processors; Cpu++)
        {
            KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
            Arm64SubNodeCheckCpu(Shared);
        }
        KeRevertToUserAffinityThread();

        Delay.QuadPart = -((LONGLONG)ROUND_MS * 10000LL);
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    InterlockedExchange(&SubNodeChurnRun, 0);

    for (i = 0; i < Created; i++)
    {
        if (Objects[i] != NULL)
        {
            KeWaitForSingleObject(Objects[i], Executive, KernelMode, FALSE, NULL);
            ObDereferenceObject(Objects[i]);
        }
        ZwClose(Handles[i]);
    }

    dump_trace("[arm64][KeArm64SubNodeSched] subnode=%p Affinity=0x%I64x IdleCpuSet=0x%I64x validated on %lu cpus under churn\n",
               Shared, (ULONGLONG)Shared->Affinity,
               (ULONGLONG)Shared->IdleCpuSet, Processors);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64SubNodeSched)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64SubNodeSched is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64SubNodeSched] enter\n");
    Arm64SubNodeSched();
#endif
}
