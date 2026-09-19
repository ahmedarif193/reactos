/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#include "dxgkrnl_private.h"
#include "presenttrace.h"
#include <stdlib.h>
DPT_BANK g_DxgPresentTrace;

#define DXGK_TRACE_PACKET_CAPACITY 8192
static struct
{
    ULONGLONG Admit, Dispatch, Retire;
    ULONGLONG Visit, Claim, RootTicks;
    ULONG Node, Fence;
    BOOLEAN Complete;
} PacketSamples[DXGK_TRACE_PACKET_CAPACITY];
static ULONGLONG PacketScratch[DXGK_TRACE_PACKET_CAPACITY];
static volatile LONG PacketCount, PacketReadyEpoch, PacketControl;
static ULONG PacketPid;
#define DXGK_TRACE_PRODUCER_CAPACITY 64
static KSPIN_LOCK ProducerLock;
static ULONG ProducerCount, ProducerOverflow;
static struct
{
    ULONG Pid, Context, Node;
    DPT_COUNTER Counter[DxgTraceProducerStageCount];
} ProducerTimings[DXGK_TRACE_PRODUCER_CAPACITY];

/* Context handles include their generation. Keep only scalar identities, not
 * object pointers, so teardown cannot invalidate an active/frozen capture.
 * No allocation or printing occurs on the submission path. */
DPT_SCOPE DxgPresentTraceProducerBegin(PDXGKRNL_CONTEXT Context, ULONG Stage)
{
    DPT_SCOPE Scope = {0};
    ULONG Pid, Handle, Node, Index;
    LONG Epoch = DPT_READ(&PacketReadyEpoch);
    KIRQL Irql;

    if (!Epoch || DPT_READ(&g_DxgPresentTrace.Epoch) != Epoch ||
        Stage >= DxgTraceProducerStageCount)
        return Scope;
    Pid = Context && Context->Device && Context->Device->OwnerProcess ?
        HandleToUlong(PsGetProcessId(Context->Device->OwnerProcess)) :
        HandleToUlong(PsGetCurrentProcessId());
    Handle = Context ? Context->Handle : 0;
    Node = Context ? Context->NodeOrdinal : MAXULONG;
    InterlockedIncrement(&g_DxgPresentTrace.Writers);
    if (DPT_READ(&g_DxgPresentTrace.Epoch) == Epoch &&
        DPT_READ(&PacketReadyEpoch) == Epoch)
    {
        KeAcquireSpinLock(&ProducerLock, &Irql);
        for (Index = 0; Index < ProducerCount; ++Index)
            if (ProducerTimings[Index].Pid == Pid &&
                ProducerTimings[Index].Context == Handle &&
                ProducerTimings[Index].Node == Node)
                break;
        if (Index < DXGK_TRACE_PRODUCER_CAPACITY)
        {
            if (Index == ProducerCount)
            {
                ProducerTimings[Index].Pid = Pid;
                ProducerTimings[Index].Context = Handle;
                ProducerTimings[Index].Node = Node;
                ++ProducerCount;
            }
            ++ProducerTimings[Index].Counter[Stage].Entered;
            Scope.Epoch = Epoch;
            Scope.Metric = Index * DxgTraceProducerStageCount + Stage;
            Scope.Start = DptNow();
        }
        else
            ++ProducerOverflow;
        KeReleaseSpinLock(&ProducerLock, Irql);
    }
    InterlockedDecrement(&g_DxgPresentTrace.Writers);
    return Scope;
}

VOID DxgPresentTraceProducerEnd(DPT_SCOPE Scope, NTSTATUS Status)
{
    DPT_COUNTER *Counter;
    ULONGLONG Ticks;
    KIRQL Irql;

    if (!Scope.Epoch || DPT_READ(&g_DxgPresentTrace.Epoch) != Scope.Epoch)
        return;
    Ticks = DptNow() - Scope.Start;
    InterlockedIncrement(&g_DxgPresentTrace.Writers);
    if (DPT_READ(&g_DxgPresentTrace.Epoch) == Scope.Epoch &&
        DPT_READ(&PacketReadyEpoch) == Scope.Epoch)
    {
        KeAcquireSpinLock(&ProducerLock, &Irql);
        Counter = &ProducerTimings[Scope.Metric / DxgTraceProducerStageCount]
            .Counter[Scope.Metric % DxgTraceProducerStageCount];
        ++Counter->Completed;
        Counter->Failed += !NT_SUCCESS(Status);
        Counter->Ticks += Ticks;
        Counter->MaxTicks = max(Counter->MaxTicks, Ticks);
        KeReleaseSpinLock(&ProducerLock, Irql);
    }
    InterlockedDecrement(&g_DxgPresentTrace.Writers);
}

static VOID DxgDumpProducers(ULONGLONG Frequency)
{
    static const char *Names[] = {"command_admission", "context_room", "transaction_reacquire"};
    ULONG Index, Stage;

    if (!Frequency)
        return;
    DbgPrint("DXGK_PRODUCER_COUNTS contexts=%lu overflow=%lu\n", ProducerCount, ProducerOverflow);
    for (Index = 0; Index < ProducerCount; ++Index)
        for (Stage = 0; Stage < DxgTraceProducerStageCount; ++Stage)
        {
            DPT_COUNTER *Counter = &ProducerTimings[Index].Counter[Stage];
            if (!Counter->Entered)
                continue;
            DbgPrint("DXGK_PRODUCER_TIMING pid=%lu context=%08lx node=%lu phase=%s entered=%llu completed=%llu unfinished=%llu failed=%llu total_us=%llu avg_us=%llu max_us=%llu\n",
                     ProducerTimings[Index].Pid, ProducerTimings[Index].Context,
                     ProducerTimings[Index].Node, Names[Stage], Counter->Entered,
                     Counter->Completed, Counter->Entered - Counter->Completed,
                     Counter->Failed, Counter->Ticks * 1000000 / Frequency,
                     Counter->Completed ? Counter->Ticks * 1000000 / Frequency / Counter->Completed : 0,
                     Counter->MaxTicks * 1000000 / Frequency);
        }
}
static struct
{
    volatile LONG64 Total, Maximum;
    volatile LONG Count;
} ContextTimings[32][3];

DPT_SCOPE DxgPresentTraceContextBegin(ULONG Pid)
{
    DPT_SCOPE Scope = {0};
    LONG Epoch = DPT_READ(&PacketReadyEpoch);
    if (Epoch && DPT_READ(&g_DxgPresentTrace.Epoch) == Epoch && Pid == PacketPid)
    {
        Scope.Epoch = Epoch;
        Scope.Start = DptNow();
    }
    return Scope;
}

VOID DxgPresentTraceContextEnd(DPT_SCOPE Scope, ULONG Node, ULONG Kind)
{
    LONG64 Ticks, Maximum;
    if (!Scope.Epoch || Node >= 32 || Kind >= 3 ||
        DPT_READ(&g_DxgPresentTrace.Epoch) != Scope.Epoch)
        return;
    Ticks = DptNow() - Scope.Start;
    InterlockedIncrement(&g_DxgPresentTrace.Writers);
    if (DPT_READ(&g_DxgPresentTrace.Epoch) == Scope.Epoch &&
        DPT_READ(&PacketReadyEpoch) == Scope.Epoch)
    {
        InterlockedExchangeAdd64(&ContextTimings[Node][Kind].Total, Ticks);
        InterlockedIncrement(&ContextTimings[Node][Kind].Count);
        Maximum = DPT_READ64(&ContextTimings[Node][Kind].Maximum);
        while (Ticks > Maximum)
        {
            LONG64 Previous = InterlockedCompareExchange64(&ContextTimings[Node][Kind].Maximum, Ticks, Maximum);
            if (Previous == Maximum)
                break;
            Maximum = Previous;
        }
    }
    InterlockedDecrement(&g_DxgPresentTrace.Writers);
}

VOID DxgPresentTracePacket(ULONG Pid, ULONG Node, ULONG Fence,
                          DPT_SCOPE Queue, DPT_SCOPE Dispatch, BOOLEAN Complete,
                          ULONGLONG Visit, ULONGLONG Claim, ULONGLONG RootTicks)
{
    ULONGLONG End;
    LONG Slot;
    if (!Dispatch.Epoch || DPT_READ(&PacketReadyEpoch) != Dispatch.Epoch ||
        DPT_READ(&g_DxgPresentTrace.Epoch) != Dispatch.Epoch)
        return;
    End = DptNow();
    InterlockedIncrement(&g_DxgPresentTrace.Writers);
    if (DPT_READ(&g_DxgPresentTrace.Epoch) == Dispatch.Epoch &&
        DPT_READ(&PacketReadyEpoch) == Dispatch.Epoch && Pid == PacketPid &&
        Queue.Start >= g_DxgPresentTrace.Data.Start && Dispatch.Start >= Queue.Start)
    {
        Slot = InterlockedIncrement(&PacketCount) - 1;
        if ((ULONG)Slot < DXGK_TRACE_PACKET_CAPACITY)
        {
            PacketSamples[Slot].Admit = Queue.Start;
            PacketSamples[Slot].Dispatch = Dispatch.Start;
            PacketSamples[Slot].Retire = End;
            PacketSamples[Slot].Visit = Visit;
            PacketSamples[Slot].Claim = Claim;
            PacketSamples[Slot].RootTicks = RootTicks;
            PacketSamples[Slot].Node = Node;
            PacketSamples[Slot].Fence = Fence;
            PacketSamples[Slot].Complete = Complete;
        }
    }
    InterlockedDecrement(&g_DxgPresentTrace.Writers);
}

static int __cdecl DxgComparePacketTicks(const void *Left, const void *Right)
{
    ULONGLONG A = *(const ULONGLONG *)Left, B = *(const ULONGLONG *)Right;
    return (A > B) - (A < B);
}

static VOID DxgDumpPackets(ULONGLONG Frequency, ULONGLONG Start)
{
    static const char *Names[] = {"admit_to_dispatch", "dispatch_to_retire", "admit_to_retire",
                                 "admit_to_visit", "visit_to_claim", "claim_to_dispatch", "root_publish"};
    ULONG Count = min((ULONG)PacketCount, DXGK_TRACE_PACKET_CAPACITY);
    ULONG Index, Node, Phase, MaxNode = 0;
    static const char *ContextNames[] = {"worker_ready", "gpu_wait", "gpu_signal"};
    if (!Frequency)
        return;
    for (Node = 0; Node < 32; ++Node)
        for (Phase = 0; Phase < 3; ++Phase)
            if (ContextTimings[Node][Phase].Count)
                DbgPrint("DXGK_CONTEXT_TIMING pid=%lu node=%lu phase=%s samples=%ld avg_us=%llu max_us=%llu\n",
                         PacketPid, Node, ContextNames[Phase], ContextTimings[Node][Phase].Count,
                         (ULONGLONG)ContextTimings[Node][Phase].Total * 1000000 / Frequency / ContextTimings[Node][Phase].Count,
                         (ULONGLONG)ContextTimings[Node][Phase].Maximum * 1000000 / Frequency);
    if (!Count)
        return;
    DbgPrint("DXGK_PACKET_COUNTS pid=%lu samples=%lu lost=%lu\n", PacketPid, Count, (ULONG)PacketCount - Count);
    for (Index = 0; Index < Count; ++Index)
        MaxNode = max(MaxNode, PacketSamples[Index].Node);
    for (Node = 0; Node <= MaxNode && Node < 32; ++Node)
    {
        ULONG Worst = MAXULONG;
        for (Phase = 0; Phase < ARRAYSIZE(Names); ++Phase)
        {
            ULONGLONG Sum = 0;
            ULONG Samples = 0, Failed = 0;
            for (Index = 0; Index < Count; ++Index)
            {
                ULONGLONG Ticks;
                if (PacketSamples[Index].Node != Node)
                    continue;
                if (Phase >= 3 && (!PacketSamples[Index].Visit || !PacketSamples[Index].Claim))
                    continue;
                switch (Phase)
                {
                    case 0: Ticks = PacketSamples[Index].Dispatch - PacketSamples[Index].Admit; break;
                    case 1: Ticks = PacketSamples[Index].Retire - PacketSamples[Index].Dispatch; break;
                    case 2: Ticks = PacketSamples[Index].Retire - PacketSamples[Index].Admit; break;
                    case 3: Ticks = PacketSamples[Index].Visit - PacketSamples[Index].Admit; break;
                    case 4: Ticks = PacketSamples[Index].Claim - PacketSamples[Index].Visit; break;
                    case 5: Ticks = PacketSamples[Index].Dispatch - PacketSamples[Index].Claim; break;
                    default: Ticks = PacketSamples[Index].RootTicks; break;
                }
                PacketScratch[Samples++] = Ticks;
                Sum += Ticks;
                Failed += !PacketSamples[Index].Complete;
                if (Phase == 2 && (Worst == MAXULONG || Ticks >
                    PacketSamples[Worst].Retire - PacketSamples[Worst].Admit))
                    Worst = Index;
            }
            if (!Samples)
                continue;
            qsort(PacketScratch, Samples, sizeof(PacketScratch[0]), DxgComparePacketTicks);
            DbgPrint("DXGK_PACKET_TIMING pid=%lu node=%lu phase=%s samples=%lu failed=%lu avg_us=%llu p50_us=%llu p95_us=%llu p99_us=%llu max_us=%llu\n",
                     PacketPid, Node, Names[Phase], Samples, Failed, Sum * 1000000 / Frequency / Samples,
                     PacketScratch[(Samples - 1) / 2] * 1000000 / Frequency,
                     PacketScratch[(Samples - 1) * 95 / 100] * 1000000 / Frequency,
                     PacketScratch[(Samples - 1) * 99 / 100] * 1000000 / Frequency,
                     PacketScratch[Samples - 1] * 1000000 / Frequency);
        }
        if (Worst != MAXULONG)
            DbgPrint("DXGK_PACKET_SLOW pid=%lu node=%lu fence=%lu admit_us=%llu dispatch_us=%llu retire_us=%llu\n",
                     PacketPid, Node, PacketSamples[Worst].Fence,
                     (PacketSamples[Worst].Admit - Start) * 1000000 / Frequency,
                     (PacketSamples[Worst].Dispatch - Start) * 1000000 / Frequency,
                     (PacketSamples[Worst].Retire - Start) * 1000000 / Frequency);
    }
}

LONG DxgPresentTraceControl(const DPT_REQUEST *Request, DPT_DOMAIN *Output, ULONG Bytes)
{
    LONG Result;
    if (InterlockedCompareExchange(&PacketControl, 1, 0))
        return (LONG)0x800700aa;
    Result = DptControl(&g_DxgPresentTrace, Request, Output, Bytes, 0);
    if (Result >= 0 && Request->Operation == DPT_START)
    {
        InterlockedExchange(&PacketReadyEpoch, 0);
        PacketCount = 0;
        KeInitializeSpinLock(&ProducerLock);
        ProducerCount = ProducerOverflow = 0;
        RtlZeroMemory(ProducerTimings, sizeof(ProducerTimings));
        RtlZeroMemory(ContextTimings, sizeof(ContextTimings));
        PacketPid = HandleToUlong(PsGetCurrentProcessId());
        InterlockedExchange(&PacketReadyEpoch, DPT_READ(&g_DxgPresentTrace.Epoch));
    }
    else if (Result >= 0 && Request->Operation == DPT_STOP &&
             InterlockedExchange(&PacketReadyEpoch, 0) != 0)
    {
        DxgDumpPackets(Output->Frequency, Output->Start);
        DxgDumpProducers(Output->Frequency);
    }
    InterlockedExchange(&PacketControl, 0);
    return Result;
}
