/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 SMT (logical-processor) topology
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

VOID Test_KeArm64Smt(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static ULONG Arm64PopCount(KAFFINITY Mask)
{
    ULONG Count = 0;
    while (Mask)
    {
        Count += (ULONG)(Mask & 1);
        Mask >>= 1;
    }
    return Count;
}

static VOID Arm64SmtCheckCpu(KAFFINITY *SiblingOut)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG MyNumber = KeGetCurrentProcessorNumber();
    KAFFINITY Active = KeQueryActiveProcessors();
    KAFFINITY Siblings;

    *SiblingOut = 0;

    ok(Prcb != NULL, "Prcb NULL\n");
    if (!Prcb)
        return;

    ok(Prcb->CoresPerPhysicalProcessor >= 1u,
       "cpu %lu CoresPerPhysicalProcessor=%u < 1\n",
       MyNumber, Prcb->CoresPerPhysicalProcessor);
    ok(Prcb->LogicalProcessorsPerCore >= 1u,
       "cpu %lu LogicalProcessorsPerCore=%u < 1\n",
       MyNumber, Prcb->LogicalProcessorsPerCore);

    Siblings = Prcb->MultiThreadProcessorSet;
    *SiblingOut = Siblings;

    ok((Siblings & ((KAFFINITY)1 << MyNumber)) != 0,
       "cpu %lu absent from its own MultiThreadProcessorSet=0x%I64x\n",
       MyNumber, (ULONGLONG)Siblings);
    ok((Siblings & ~Active) == 0,
       "cpu %lu MultiThreadProcessorSet=0x%I64x has offline bits (active=0x%I64x)\n",
       MyNumber, (ULONGLONG)Siblings, (ULONGLONG)Active);
    ok(Arm64PopCount(Siblings) == Prcb->LogicalProcessorsPerCore,
       "cpu %lu sibling-count=%lu != LogicalProcessorsPerCore=%u\n",
       MyNumber, Arm64PopCount(Siblings), Prcb->LogicalProcessorsPerCore);

    if (Prcb->SchedulerSubNode != NULL)
    {
        PKSCHEDULER_SUBNODE SubNode = (PKSCHEDULER_SUBNODE)Prcb->SchedulerSubNode;
        ok((SubNode->IdleSmtSet & ~SubNode->IdleCpuSet) == 0,
           "cpu %lu IdleSmtSet=0x%I64x has bits outside IdleCpuSet=0x%I64x\n",
           MyNumber, (ULONGLONG)SubNode->IdleSmtSet, (ULONGLONG)SubNode->IdleCpuSet);
    }
}

static VOID Arm64Smt(VOID)
{
    KAFFINITY Sibling[MAXIMUM_PROCESSORS];
    ULONG Processors = KeNumberProcessors;
    ULONG Cpu;
    ULONG i;

    if (Processors > MAXIMUM_PROCESSORS)
        Processors = MAXIMUM_PROCESSORS;

    for (Cpu = 0; Cpu < Processors; Cpu++)
    {
        KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
        Arm64SmtCheckCpu(&Sibling[Cpu]);
    }
    KeRevertToUserAffinityThread();

    for (Cpu = 0; Cpu < Processors; Cpu++)
    {
        for (i = 0; i < Processors; i++)
        {
            if (Sibling[Cpu] & ((KAFFINITY)1 << i))
                ok(Sibling[i] == Sibling[Cpu],
                   "siblings not reciprocal: cpu %lu set=0x%I64x lists cpu %lu whose set=0x%I64x\n",
                   Cpu, (ULONGLONG)Sibling[Cpu], i, (ULONGLONG)Sibling[i]);
        }
    }

    dump_trace("[arm64][KeArm64Smt] %lu cpus, logical/core=%u sibling0=0x%I64x\n",
               Processors, KeGetCurrentPrcb()->LogicalProcessorsPerCore,
               (ULONGLONG)Sibling[0]);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64Smt)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Smt is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64Smt] enter\n");
    Arm64Smt();
#endif
}
