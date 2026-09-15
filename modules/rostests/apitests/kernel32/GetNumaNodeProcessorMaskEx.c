#include "precomp.h"

BOOL WINAPI GetNumaHighestNodeNumber(PULONG HighestNodeNumber);
BOOL WINAPI GetNumaNodeProcessorMask(UCHAR Node, PULONGLONG ProcessorMask);

static
void
Test_MatchesLegacyMask(ULONG HighestNode, KAFFINITY *UnionMask)
{
    USHORT Node;
    GROUP_AFFINITY Affinity;
    ULONGLONG LegacyMask;
    BOOL Result;
    ULONG i;

    *UnionMask = 0;

    for (Node = 0; Node <= HighestNode; Node++)
    {
        FillMemory(&Affinity, sizeof(Affinity), 0xCC);
        SetLastError(0xDEADBEEF);
        Result = GetNumaNodeProcessorMaskEx(Node, &Affinity);
        ok(Result, "Node %u: GetNumaNodeProcessorMaskEx failed: %lu\n", Node, GetLastError());
        if (!Result)
            continue;

        ok(Affinity.Group == 0, "Node %u: Group = %u, expected 0\n", Node, Affinity.Group);
        for (i = 0; i < ARRAYSIZE(Affinity.Reserved); i++)
        {
            ok(Affinity.Reserved[i] == 0, "Node %u: Reserved[%lu] = %u, expected 0\n",
               Node, i, Affinity.Reserved[i]);
        }

        LegacyMask = 0;
        Result = GetNumaNodeProcessorMask((UCHAR)Node, &LegacyMask);
        ok(Result, "Node %u: GetNumaNodeProcessorMask failed: %lu\n", Node, GetLastError());
        if (Result)
        {
            ok(Affinity.Mask == (KAFFINITY)LegacyMask,
               "Node %u: Mask = %Ix, legacy = %I64x\n", Node, Affinity.Mask, LegacyMask);
        }

        if (Node == 0)
            ok(Affinity.Mask != 0, "Node 0 has an empty processor mask\n");

        ok((*UnionMask & Affinity.Mask) == 0,
           "Node %u: Mask %Ix overlaps earlier nodes %Ix\n", Node, Affinity.Mask, *UnionMask);
        *UnionMask |= Affinity.Mask;
    }
}

static
void
Test_MatchesNtQuery(ULONG HighestNode)
{
    SYSTEM_NUMA_INFORMATION NumaInfo;
    GROUP_AFFINITY Affinity;
    NTSTATUS Status;
    ULONG Length;
    USHORT Node;

    Status = NtQuerySystemInformation(SystemNumaProcessorMap, &NumaInfo, sizeof(NumaInfo), &Length);
    ok(NT_SUCCESS(Status), "NtQuerySystemInformation(SystemNumaProcessorMap) = %lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;

    ok(NumaInfo.HighestNodeNumber == HighestNode,
       "HighestNodeNumber = %lu, GetNumaHighestNodeNumber = %lu\n",
       NumaInfo.HighestNodeNumber, HighestNode);

    for (Node = 0; Node <= HighestNode && Node < MAXIMUM_NUMA_NODES; Node++)
    {
        if (!GetNumaNodeProcessorMaskEx(Node, &Affinity))
            continue;
        ok(Affinity.Mask == (KAFFINITY)NumaInfo.ActiveProcessorsAffinityMask[Node],
           "Node %u: Mask = %Ix, kernel map = %I64x\n",
           Node, Affinity.Mask, NumaInfo.ActiveProcessorsAffinityMask[Node]);
    }
}

static
void
Test_UnionCoversSystemAffinity(KAFFINITY UnionMask)
{
    DWORD_PTR ProcessMask, SystemMask;
    BOOL Result;

    Result = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Result, "GetProcessAffinityMask failed: %lu\n", GetLastError());
    if (!Result)
        return;

    ok(UnionMask == (KAFFINITY)SystemMask,
       "Union of node masks %Ix != system affinity %Ix\n", UnionMask, (KAFFINITY)SystemMask);
}

static
void
Test_InvalidNode(ULONG HighestNode)
{
    GROUP_AFFINITY Affinity;
    BOOL Result;
    USHORT BadNodes[3];
    ULONG i;

    BadNodes[0] = (USHORT)(HighestNode + 1);
    BadNodes[1] = MAXIMUM_NUMA_NODES;
    BadNodes[2] = 0xFFFF;

    for (i = 0; i < ARRAYSIZE(BadNodes); i++)
    {
        SetLastError(0xDEADBEEF);
        Result = GetNumaNodeProcessorMaskEx(BadNodes[i], &Affinity);
        ok(!Result, "Node %u: expected failure\n", BadNodes[i]);
        ok(GetLastError() == ERROR_INVALID_PARAMETER,
           "Node %u: LastError = %lu, expected ERROR_INVALID_PARAMETER\n",
           BadNodes[i], GetLastError());
    }
}

START_TEST(GetNumaNodeProcessorMaskEx)
{
    ULONG HighestNode;
    KAFFINITY UnionMask;
    BOOL Result;

    Result = GetNumaHighestNodeNumber(&HighestNode);
    ok(Result, "GetNumaHighestNodeNumber failed: %lu\n", GetLastError());
    if (!Result)
        return;

    ok(HighestNode < MAXIMUM_NUMA_NODES, "HighestNode = %lu\n", HighestNode);

    Test_MatchesLegacyMask(HighestNode, &UnionMask);
    Test_MatchesNtQuery(HighestNode);
    Test_UnionCoversSystemAffinity(UnionMask);
    Test_InvalidNode(HighestNode);
}
