/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Metric-aware recording analysis
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_analysis.h"

#include <stdlib.h>

#define RPERF_NO_INDEX ((SIZE_T)-1)

typedef struct _RPERF_BUILD_INDEXES
{
    SIZE_T *Functions;
    SIZE_T FunctionSlots;
    SIZE_T *TopDown;
    SIZE_T TopDownSlots;
    SIZE_T *BottomUp;
    SIZE_T BottomUpSlots;
    SIZE_T *Edges;
    SIZE_T EdgeSlots;
} RPERF_BUILD_INDEXES;

static ULONGLONG
RperfMix64(ULONGLONG Value)
{
    Value ^= Value >> 30;
    Value *= 0xbf58476d1ce4e5b9ULL;
    Value ^= Value >> 27;
    Value *= 0x94d049bb133111ebULL;
    Value ^= Value >> 31;
    return Value;
}

static SIZE_T
RperfKeyHash(const RPERF_FUNCTION_KEY *Key)
{
    return (SIZE_T)RperfMix64(Key->ModuleId ^ RperfMix64(Key->Address));
}

BOOL
RperfFunctionKeyEqual(const RPERF_FUNCTION_KEY *Left,
                      const RPERF_FUNCTION_KEY *Right)
{
    return Left->ModuleId == Right->ModuleId &&
           Left->Address == Right->Address;
}

static BOOL
RperfCancelled(HANDLE Event)
{
    return Event != NULL && WaitForSingleObject(Event, 0) == WAIT_OBJECT_0;
}

static BOOL
RperfGrow(PVOID *Buffer,
          SIZE_T ElementSize,
          SIZE_T *Capacity,
          SIZE_T Required,
          SIZE_T Maximum)
{
    SIZE_T NewCapacity;
    PVOID NewBuffer;

    if (Required <= *Capacity)
        return TRUE;
    if (Required > Maximum || Required > ((SIZE_T)-1) / ElementSize)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    NewCapacity = *Capacity != 0 ? *Capacity : 256;
    while (NewCapacity < Required && NewCapacity <= Maximum / 2)
        NewCapacity *= 2;
    if (NewCapacity < Required)
        NewCapacity = Required;
    if (NewCapacity > Maximum)
        NewCapacity = Maximum;
    if (*Buffer != NULL)
        NewBuffer = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                *Buffer, NewCapacity * ElementSize);
    else
        NewBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              NewCapacity * ElementSize);
    if (NewBuffer == NULL)
        return FALSE;
    *Buffer = NewBuffer;
    *Capacity = NewCapacity;
    return TRUE;
}

static BOOL
RperfBuildHash(SIZE_T **Table,
               SIZE_T *Slots,
               SIZE_T Count,
               SIZE_T Minimum)
{
    SIZE_T NewSlots = 256;

    while (NewSlots < Minimum || NewSlots < Count * 2)
    {
        if (NewSlots > ((SIZE_T)-1) / 2)
            return FALSE;
        NewSlots *= 2;
    }
    if (*Table != NULL)
        HeapFree(GetProcessHeap(), 0, *Table);
    *Table = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                       NewSlots * sizeof(**Table));
    if (*Table == NULL)
        return FALSE;
    *Slots = NewSlots;
    return TRUE;
}

static RPERF_FUNCTION_KEY
RperfFrameKey(const RPERF_RECORDING *Recording,
              const RPERF_FRAME *Frame,
              const RPERF_MODEL_SYMBOL **Symbol)
{
    RPERF_FUNCTION_KEY Key;
    const RPERF_MODEL_SYMBOL *Found;

    Found = RperfRecordingFindSymbol(Recording, Frame->Address);
    if (Symbol != NULL)
        *Symbol = Found;
    Key.ModuleId = Frame->ModuleId;
    if (Found != NULL)
    {
        Key.ModuleId = Found->ModuleId;
        Key.Address = Found->FunctionAddress != 0 ?
                      Found->FunctionAddress : Found->Address;
    }
    else
    {
        Key.Address = Frame->FunctionAddress != 0 ?
                      Frame->FunctionAddress : Frame->Address;
    }
    return Key;
}

static BOOL
RperfReindexFunctions(RPERF_ANALYSIS *Analysis,
                      RPERF_BUILD_INDEXES *Indexes)
{
    SIZE_T Index;

    if (!RperfBuildHash(&Indexes->Functions,
                        &Indexes->FunctionSlots,
                        Analysis->FunctionCount,
                        256))
        return FALSE;
    for (Index = 0; Index < Analysis->FunctionCount; ++Index)
    {
        SIZE_T Slot = RperfKeyHash(&Analysis->Functions[Index].Key) &
                      (Indexes->FunctionSlots - 1);
        while (Indexes->Functions[Slot] != 0)
            Slot = (Slot + 1) & (Indexes->FunctionSlots - 1);
        Indexes->Functions[Slot] = Index + 1;
    }
    return TRUE;
}

static SIZE_T
RperfFindOrAddFunction(RPERF_ANALYSIS *Analysis,
                       RPERF_BUILD_INDEXES *Indexes,
                       const RPERF_FUNCTION_KEY *Key,
                       const RPERF_MODEL_SYMBOL *Symbol)
{
    SIZE_T Slot, Index;

    if (Indexes->FunctionSlots == 0 ||
        Analysis->FunctionCount * 3 >= Indexes->FunctionSlots * 2)
    {
        if (!RperfReindexFunctions(Analysis, Indexes))
            return RPERF_NO_INDEX;
    }
    Slot = RperfKeyHash(Key) & (Indexes->FunctionSlots - 1);
    while (Indexes->Functions[Slot] != 0)
    {
        Index = Indexes->Functions[Slot] - 1;
        if (RperfFunctionKeyEqual(&Analysis->Functions[Index].Key, Key))
            return Index;
        Slot = (Slot + 1) & (Indexes->FunctionSlots - 1);
    }
    if (!RperfGrow((PVOID *)&Analysis->Functions,
                   sizeof(*Analysis->Functions),
                   &Analysis->FunctionCapacity,
                   Analysis->FunctionCount + 1,
                   (SIZE_T)Analysis->Recording->Limits.MaxSymbols))
        return RPERF_NO_INDEX;
    Index = Analysis->FunctionCount++;
    ZeroMemory(&Analysis->Functions[Index], sizeof(Analysis->Functions[Index]));
    Analysis->Functions[Index].Key = *Key;
    Analysis->Functions[Index].Symbol = Symbol;
    Indexes->Functions[Slot] = Index + 1;
    return Index;
}

static SIZE_T
RperfNodeHash(SIZE_T Parent,
              const RPERF_FUNCTION_KEY *Key)
{
    return RperfKeyHash(Key) ^ (SIZE_T)RperfMix64((ULONGLONG)Parent);
}

static BOOL
RperfReindexNodes(RPERF_CALL_NODE *Nodes,
                  SIZE_T Count,
                  SIZE_T **Table,
                  SIZE_T *Slots)
{
    SIZE_T Index;

    if (!RperfBuildHash(Table, Slots, Count, 256))
        return FALSE;
    for (Index = 1; Index < Count; ++Index)
    {
        SIZE_T Slot = RperfNodeHash(Nodes[Index].Parent,
                                    &Nodes[Index].Key) & (*Slots - 1);
        while ((*Table)[Slot] != 0)
            Slot = (Slot + 1) & (*Slots - 1);
        (*Table)[Slot] = Index + 1;
    }
    return TRUE;
}

static SIZE_T
RperfFindOrAddNode(RPERF_CALL_NODE **Nodes,
                   SIZE_T *Count,
                   SIZE_T *Capacity,
                   SIZE_T **Table,
                   SIZE_T *Slots,
                   SIZE_T Parent,
                   const RPERF_FUNCTION_KEY *Key,
                   SIZE_T Maximum)
{
    SIZE_T Slot, Index;
    RPERF_CALL_NODE *Node;

    if (*Slots == 0 || *Count * 3 >= *Slots * 2)
    {
        if (!RperfReindexNodes(*Nodes, *Count, Table, Slots))
            return RPERF_NO_INDEX;
    }
    Slot = RperfNodeHash(Parent, Key) & (*Slots - 1);
    while ((*Table)[Slot] != 0)
    {
        Index = (*Table)[Slot] - 1;
        if ((*Nodes)[Index].Parent == Parent &&
            RperfFunctionKeyEqual(&(*Nodes)[Index].Key, Key))
            return Index;
        Slot = (Slot + 1) & (*Slots - 1);
    }
    if (!RperfGrow((PVOID *)Nodes,
                   sizeof(**Nodes),
                   Capacity,
                   *Count + 1,
                   Maximum))
        return RPERF_NO_INDEX;
    Index = (*Count)++;
    Node = &(*Nodes)[Index];
    ZeroMemory(Node, sizeof(*Node));
    Node->Key = *Key;
    Node->Parent = Parent;
    Node->FirstChild = RPERF_NO_INDEX;
    Node->NextSibling = (*Nodes)[Parent].FirstChild;
    Node->Depth = (*Nodes)[Parent].Depth + 1;
    (*Nodes)[Parent].FirstChild = Index;
    (*Table)[Slot] = Index + 1;
    return Index;
}

static SIZE_T
RperfEdgeHash(const RPERF_FUNCTION_KEY *Caller,
              const RPERF_FUNCTION_KEY *Callee)
{
    return RperfKeyHash(Caller) ^ (RperfKeyHash(Callee) << 1);
}

static BOOL
RperfReindexEdges(RPERF_ANALYSIS *Analysis,
                  RPERF_BUILD_INDEXES *Indexes)
{
    SIZE_T Index;

    if (!RperfBuildHash(&Indexes->Edges,
                        &Indexes->EdgeSlots,
                        Analysis->EdgeCount,
                        256))
        return FALSE;
    for (Index = 0; Index < Analysis->EdgeCount; ++Index)
    {
        SIZE_T Slot = RperfEdgeHash(&Analysis->Edges[Index].Caller,
                                    &Analysis->Edges[Index].Callee) &
                      (Indexes->EdgeSlots - 1);
        while (Indexes->Edges[Slot] != 0)
            Slot = (Slot + 1) & (Indexes->EdgeSlots - 1);
        Indexes->Edges[Slot] = Index + 1;
    }
    return TRUE;
}

static SIZE_T
RperfFindOrAddEdge(RPERF_ANALYSIS *Analysis,
                   RPERF_BUILD_INDEXES *Indexes,
                   const RPERF_FUNCTION_KEY *Caller,
                   const RPERF_FUNCTION_KEY *Callee)
{
    SIZE_T Slot, Index;

    if (Indexes->EdgeSlots == 0 ||
        Analysis->EdgeCount * 3 >= Indexes->EdgeSlots * 2)
    {
        if (!RperfReindexEdges(Analysis, Indexes))
            return RPERF_NO_INDEX;
    }
    Slot = RperfEdgeHash(Caller, Callee) & (Indexes->EdgeSlots - 1);
    while (Indexes->Edges[Slot] != 0)
    {
        Index = Indexes->Edges[Slot] - 1;
        if (RperfFunctionKeyEqual(&Analysis->Edges[Index].Caller, Caller) &&
            RperfFunctionKeyEqual(&Analysis->Edges[Index].Callee, Callee))
            return Index;
        Slot = (Slot + 1) & (Indexes->EdgeSlots - 1);
    }
    if (!RperfGrow((PVOID *)&Analysis->Edges,
                   sizeof(*Analysis->Edges),
                   &Analysis->EdgeCapacity,
                   Analysis->EdgeCount + 1,
                   Analysis->Recording->Limits.MaxRecords))
        return RPERF_NO_INDEX;
    Index = Analysis->EdgeCount++;
    ZeroMemory(&Analysis->Edges[Index], sizeof(Analysis->Edges[Index]));
    Analysis->Edges[Index].Caller = *Caller;
    Analysis->Edges[Index].Callee = *Callee;
    Indexes->Edges[Slot] = Index + 1;
    return Index;
}

static ULONGLONG
RperfSampleWeight(const RPERF_ANALYSIS *Analysis,
                  const RPERF_SAMPLE_RECORD *Sample)
{
    switch (Analysis->Metric)
    {
        case RperfMetricEventWeight:
        case RperfMetricWallTime:
        case RperfMetricOffCpuTime:
            return Sample->Weight;
        case RperfMetricCpuSamples:
        case RperfMetricSnapshotPopulation:
        default:
            return 1;
    }
}

static BOOL
RperfInitializeRoot(RPERF_CALL_NODE **Nodes,
                    SIZE_T *Count,
                    SIZE_T *Capacity,
                    SIZE_T Maximum)
{
    if (!RperfGrow((PVOID *)Nodes, sizeof(**Nodes), Capacity, 1, Maximum))
        return FALSE;
    ZeroMemory(&(*Nodes)[0], sizeof(**Nodes));
    (*Nodes)[0].Parent = RPERF_NO_INDEX;
    (*Nodes)[0].FirstChild = RPERF_NO_INDEX;
    (*Nodes)[0].NextSibling = RPERF_NO_INDEX;
    *Count = 1;
    return TRUE;
}

RPERF_ANALYSIS *
RperfAnalysisBuild(RPERF_RECORDING *Recording,
                   const RPERF_FILTER *Filter,
                   HANDLE CancelEvent)
{
    RPERF_ANALYSIS *Analysis;
    RPERF_BUILD_INDEXES Indexes;
    RPERF_FILTER EffectiveFilter;
    SIZE_T RecordIndex;
    SIZE_T MaximumNodes;

    if (Recording == NULL || !Recording->Frozen)
    {
        SetLastError(ERROR_INVALID_STATE);
        return NULL;
    }
    Analysis = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         sizeof(*Analysis));
    if (Analysis == NULL)
        return NULL;
    ZeroMemory(&Indexes, sizeof(Indexes));
    Analysis->References = 1;
    Analysis->Recording = Recording;
    RperfRecordingAddRef(Recording);
    Analysis->Metric = Recording->Info.Metric;
    if (Filter != NULL)
        EffectiveFilter = *Filter;
    else
        RperfInitializeFilter(&EffectiveFilter);
    Analysis->Filter = EffectiveFilter;
    MaximumNodes = (SIZE_T)(Recording->Limits.MaxRecords > (ULONGLONG)-2 ?
                           (ULONGLONG)-2 : Recording->Limits.MaxRecords);
    if (!RperfInitializeRoot(&Analysis->TopDown,
                             &Analysis->TopDownCount,
                             &Analysis->TopDownCapacity,
                             MaximumNodes) ||
        !RperfInitializeRoot(&Analysis->BottomUp,
                             &Analysis->BottomUpCount,
                             &Analysis->BottomUpCapacity,
                             MaximumNodes))
        goto Failure;

    for (RecordIndex = 0; RecordIndex < Recording->RecordCount; ++RecordIndex)
    {
        const RPERF_RECORD *Record = &Recording->Records[RecordIndex];
        RPERF_FUNCTION_KEY Keys[RPERF_MODEL_MAX_FRAMES];
        const RPERF_MODEL_SYMBOL *Symbols[RPERF_MODEL_MAX_FRAMES];
        ULONGLONG Weight;
        SIZE_T Parent;
        USHORT FrameIndex;

        if ((RecordIndex & 1023) == 0 && RperfCancelled(CancelEvent))
        {
            SetLastError(ERROR_CANCELLED);
            goto Failure;
        }
        if (!RperfRecordMatchesFilter(Recording, Record, &EffectiveFilter))
            continue;
        if (Record->Header.Kind == RperfRecordLost)
        {
            Analysis->MatchedLostRecords += Record->Data.Lost.Count;
            Analysis->MatchedLostWeight += Record->Data.Lost.Weight;
            continue;
        }
        if (Record->Header.Kind != RperfRecordSample)
            continue;

        Weight = RperfSampleWeight(Analysis, &Record->Data.Sample);
        Analysis->TotalSamples++;
        Analysis->TotalWeight += Weight;
        Analysis->TopDown[0].Samples++;
        Analysis->TopDown[0].Weight += Weight;
        Analysis->BottomUp[0].Samples++;
        Analysis->BottomUp[0].Weight += Weight;

        for (FrameIndex = 0; FrameIndex < Record->Data.Sample.Depth; ++FrameIndex)
            Keys[FrameIndex] = RperfFrameKey(Recording,
                                            &Record->Data.Sample.Frames[FrameIndex],
                                            &Symbols[FrameIndex]);

        for (FrameIndex = 0; FrameIndex < Record->Data.Sample.Depth; ++FrameIndex)
        {
            BOOL Seen = FALSE;
            USHORT Previous;
            SIZE_T FunctionIndex;

            for (Previous = 0; Previous < FrameIndex; ++Previous)
            {
                if (RperfFunctionKeyEqual(&Keys[Previous], &Keys[FrameIndex]))
                {
                    Seen = TRUE;
                    break;
                }
            }
            FunctionIndex = RperfFindOrAddFunction(Analysis,
                                                   &Indexes,
                                                   &Keys[FrameIndex],
                                                   Symbols[FrameIndex]);
            if (FunctionIndex == RPERF_NO_INDEX)
                goto Failure;
            if (!Seen)
            {
                Analysis->Functions[FunctionIndex].InclusiveSamples++;
                Analysis->Functions[FunctionIndex].InclusiveWeight += Weight;
            }
            if (FrameIndex == 0)
            {
                Analysis->Functions[FunctionIndex].SelfSamples++;
                Analysis->Functions[FunctionIndex].SelfWeight += Weight;
            }
        }

        Parent = 0;
        FrameIndex = Record->Data.Sample.Depth;
        while (FrameIndex-- != 0)
        {
            Parent = RperfFindOrAddNode(&Analysis->TopDown,
                                       &Analysis->TopDownCount,
                                       &Analysis->TopDownCapacity,
                                       &Indexes.TopDown,
                                       &Indexes.TopDownSlots,
                                       Parent,
                                       &Keys[FrameIndex],
                                       MaximumNodes);
            if (Parent == RPERF_NO_INDEX)
                goto Failure;
            Analysis->TopDown[Parent].Samples++;
            Analysis->TopDown[Parent].Weight += Weight;
        }

        Parent = 0;
        for (FrameIndex = 0; FrameIndex < Record->Data.Sample.Depth; ++FrameIndex)
        {
            Parent = RperfFindOrAddNode(&Analysis->BottomUp,
                                       &Analysis->BottomUpCount,
                                       &Analysis->BottomUpCapacity,
                                       &Indexes.BottomUp,
                                       &Indexes.BottomUpSlots,
                                       Parent,
                                       &Keys[FrameIndex],
                                       MaximumNodes);
            if (Parent == RPERF_NO_INDEX)
                goto Failure;
            Analysis->BottomUp[Parent].Samples++;
            Analysis->BottomUp[Parent].Weight += Weight;
        }

        for (FrameIndex = 1; FrameIndex < Record->Data.Sample.Depth; ++FrameIndex)
        {
            SIZE_T EdgeIndex = RperfFindOrAddEdge(Analysis,
                                                  &Indexes,
                                                  &Keys[FrameIndex],
                                                  &Keys[FrameIndex - 1]);
            if (EdgeIndex == RPERF_NO_INDEX)
                goto Failure;
            Analysis->Edges[EdgeIndex].Samples++;
            Analysis->Edges[EdgeIndex].Weight += Weight;
        }
    }

    if (Indexes.Functions != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.Functions);
    if (Indexes.TopDown != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.TopDown);
    if (Indexes.BottomUp != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.BottomUp);
    if (Indexes.Edges != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.Edges);
    return Analysis;

Failure:
    if (Indexes.Functions != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.Functions);
    if (Indexes.TopDown != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.TopDown);
    if (Indexes.BottomUp != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.BottomUp);
    if (Indexes.Edges != NULL)
        HeapFree(GetProcessHeap(), 0, Indexes.Edges);
    RperfAnalysisRelease(Analysis);
    return NULL;
}

VOID
RperfAnalysisAddRef(RPERF_ANALYSIS *Analysis)
{
    if (Analysis != NULL)
        InterlockedIncrement(&Analysis->References);
}

VOID
RperfAnalysisRelease(RPERF_ANALYSIS *Analysis)
{
    if (Analysis == NULL || InterlockedDecrement(&Analysis->References) != 0)
        return;
    if (Analysis->Recording != NULL)
        RperfRecordingRelease(Analysis->Recording);
    if (Analysis->Functions != NULL)
        HeapFree(GetProcessHeap(), 0, Analysis->Functions);
    if (Analysis->TopDown != NULL)
        HeapFree(GetProcessHeap(), 0, Analysis->TopDown);
    if (Analysis->BottomUp != NULL)
        HeapFree(GetProcessHeap(), 0, Analysis->BottomUp);
    if (Analysis->Edges != NULL)
        HeapFree(GetProcessHeap(), 0, Analysis->Edges);
    HeapFree(GetProcessHeap(), 0, Analysis);
}

const RPERF_FUNCTION_COST *
RperfAnalysisFindFunction(const RPERF_ANALYSIS *Analysis,
                          const RPERF_FUNCTION_KEY *Key)
{
    SIZE_T Index;
    if (Analysis == NULL || Key == NULL)
        return NULL;
    for (Index = 0; Index < Analysis->FunctionCount; ++Index)
    {
        if (RperfFunctionKeyEqual(&Analysis->Functions[Index].Key, Key))
            return &Analysis->Functions[Index];
    }
    return NULL;
}
