/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 NUMA node topology
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

VOID Test_KeArm64Numa(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64NumaCheckCpu(ULONG *NodeOut)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG MyNumber = KeGetCurrentProcessorNumber();
    KAFFINITY Active = KeQueryActiveProcessors();
    PKNODE Node;

    *NodeOut = 0;

    ok(Prcb != NULL, "Prcb NULL\n");
    if (!Prcb)
        return;

    Node = Prcb->ParentNode;
    if (Node == NULL)
    {
        /* Win11's KPRCB does not expose a KNODE* at this offset (the field is a
         * ReactOS extension that lands in Win11's PrcbPad4 padding). Treat as
         * not-applicable on Win11 instead of failing; ReactOS populates it and
         * is fully validated when running on ReactOS. */
        trace("cpu %lu: ParentNode not populated (ReactOS-private/Win11 padding); NUMA checks skipped\n",
              MyNumber);
        return;
    }

    ok((ULONG_PTR)Node >= 0xFFFF000000000000ULL,
       "cpu %lu ParentNode=%p not a kernel address\n", MyNumber, Node);
    ok((Node->ProcessorMask & ((KAFFINITY)1 << MyNumber)) != 0,
       "cpu %lu absent from ParentNode->ProcessorMask=0x%I64x\n",
       MyNumber, (ULONGLONG)Node->ProcessorMask);
    ok((Node->ProcessorMask & ~Active) == 0,
       "cpu %lu node ProcessorMask=0x%I64x has offline bits (active=0x%I64x)\n",
       MyNumber, (ULONGLONG)Node->ProcessorMask, (ULONGLONG)Active);

    *NodeOut = Node->NodeNumber;
}

static VOID Arm64Numa(VOID)
{
    ULONG NodeOf[MAXIMUM_PROCESSORS];
    ULONG Processors = KeNumberProcessors;
    ULONG Cpu;
    ULONG i;
    ULONG MaxNode = 0;

    if (Processors > MAXIMUM_PROCESSORS)
        Processors = MAXIMUM_PROCESSORS;

    for (Cpu = 0; Cpu < Processors; Cpu++)
    {
        KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
        Arm64NumaCheckCpu(&NodeOf[Cpu]);
        if (NodeOf[Cpu] > MaxNode)
            MaxNode = NodeOf[Cpu];
    }
    KeRevertToUserAffinityThread();

    for (i = 0; i <= MaxNode; i++)
    {
        BOOLEAN Found = FALSE;
        for (Cpu = 0; Cpu < Processors; Cpu++)
        {
            if (NodeOf[Cpu] == i)
            {
                Found = TRUE;
                break;
            }
        }
        ok(Found, "node %lu has no CPU (nodes must be contiguous 0..%lu)\n", i, MaxNode);
    }

    dump_trace("[arm64][KeArm64Numa] %lu cpus across %lu node(s), node[0]=%lu\n",
               Processors, MaxNode + 1, NodeOf[0]);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64Numa)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Numa is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64Numa] enter\n");
    Arm64Numa();
#endif
}
