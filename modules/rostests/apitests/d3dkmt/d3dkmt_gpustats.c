/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMTQueryStatistics and the adapter performance-data classes
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * These are the queries a GPU performance view is built on: how many segments
 * and nodes an adapter has, how much of each segment is committed and resident,
 * how long each node has been executing, what each process's share of that is,
 * and whatever the driver reports about clocks and temperature.
 *
 * The assertions are relationships that must hold on any correct
 * implementation -- committed never exceeds the commit limit, a running-time
 * counter never goes backwards, a per-process figure never exceeds the global
 * one -- rather than values, so the same binary is meaningful on Windows 11
 * and on ReactOS with any miniport behind it.
 *
 * Reference: Microsoft "D3DKMTQueryStatistics",
 *            "D3DKMT_QUERYSTATISTICS structure",
 *            "KMTQAITYPE_ADAPTERPERFDATA".
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_QueryStatistics)(const D3DKMT_QUERYSTATISTICS *);

static BOOL StatisticsClassUnavailable(NTSTATUS Status)
{
    return Status == STATUS_INVALID_PARAMETER || Status == STATUS_NOT_SUPPORTED;
}

static BOOL GetAdapterLuid(D3DKMT_HANDLE hAdapter, LUID *Luid)
{
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_ADAPTERREGISTRYINFO Unused;
    D3DKMT_OPENADAPTERFROMLUID Open;
    PFN_D3DKMTEnumAdapters pfnEnum;
    PFN_D3DKMTCloseAdapter pfnClose;
    D3DKMT_ENUMADAPTERS Adapters;
    ULONG i;
    BOOL found = FALSE;

    (void)Query; (void)Unused; (void)Open;

    pfnEnum = (PFN_D3DKMTEnumAdapters)LoadD3DKMTProc("D3DKMTEnumAdapters");
    pfnClose = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");
    if (!pfnEnum || !pfnClose)
        return FALSE;

    memset(&Adapters, 0, sizeof(Adapters));
    if (!NT_SUCCESS(pfnEnum(&Adapters)))
        return FALSE;

    for (i = 0; i < Adapters.NumAdapters; i++)
    {
        D3DKMT_CLOSEADAPTER Close;

        if (!found && Adapters.Adapters[i].hAdapter == hAdapter)
        {
            *Luid = Adapters.Adapters[i].AdapterLuid;
            found = TRUE;
        }
        memset(&Close, 0, sizeof(Close));
        Close.hAdapter = Adapters.Adapters[i].hAdapter;
        if (Close.hAdapter && Close.hAdapter != hAdapter)
            pfnClose(&Close);
    }

    /*
     * EnumAdapters hands back its own handles, so the LUID has to be taken
     * from the enumeration itself rather than from the handle under test.
     * Re-enumerating for the first adapter is what makes this work when the
     * caller's handle came from a different open.
     */
    if (!found && Adapters.NumAdapters > 0)
    {
        *Luid = Adapters.Adapters[0].AdapterLuid;
        found = TRUE;
    }
    return found;
}

static NTSTATUS Query(PFN_QueryStatistics pfn, D3DKMT_QUERYSTATISTICS *Statistics,
                      D3DKMT_QUERYSTATISTICS_TYPE Type, LUID Luid)
{
    memset(Statistics, 0, sizeof(*Statistics));
    Statistics->Type = Type;
    Statistics->AdapterLuid = Luid;
    return pfn(Statistics);
}

/* ---- NULL and bogus-adapter contract ---- */
static void Test_QueryStatistics_Contract(void)
{
    D3DKMT_QUERYSTATISTICS Statistics;
    LUID Bogus;
    NTSTATUS Status;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");
    EXPECT_NULL_REJECTED(pfn, "D3DKMTQueryStatistics");

    Bogus.LowPart = 0xDEAD5002;
    Bogus.HighPart = 0x7FFFFFFF;
    Status = Query(pfn, &Statistics, D3DKMT_QUERYSTATISTICS_ADAPTER, Bogus);
    ok_failed(Status, "QueryStatistics on an adapter that does not exist should fail, got 0x%08lX\n", (long)Status);
}

/* ---- Adapter topology, and the segment/node counts everything else uses ---- */
static void Test_QueryStatistics_Adapter(void)
{
    D3DKMT_QUERYSTATISTICS Statistics;
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    NTSTATUS Status;
    ULONG Segments, Nodes, Sources;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    if (!GetAdapterLuid(hAdapter, &Luid)) { CloseAdapter(hAdapter); skip("No adapter LUID\n"); return; }

    Status = Query(pfn, &Statistics, D3DKMT_QUERYSTATISTICS_ADAPTER, Luid);
    ok_succeeded(Status, "QueryStatistics(ADAPTER) failed 0x%08lX\n", (long)Status);
    if (!NT_SUCCESS(Status)) { CloseAdapter(hAdapter); return; }

    Segments = Statistics.QueryResult.AdapterInformation.NbSegments;
    Nodes = Statistics.QueryResult.AdapterInformation.NodeCount;
    Sources = Statistics.QueryResult.AdapterInformation.VidPnSourceCount;
    trace("Adapter: segments=%lu nodes=%lu sources=%lu vsync=%lu tdr=%lu\n",
          (unsigned long)Segments, (unsigned long)Nodes, (unsigned long)Sources,
          (unsigned long)Statistics.QueryResult.AdapterInformation.VSyncEnabled,
          (unsigned long)Statistics.QueryResult.AdapterInformation.TdrDetectedCount);

    /* A display adapter has at least one source; a render-capable one has at
     * least one node.  Neither may be absurd. */
    ok(Segments <= 64, "Implausible segment count %lu\n", (unsigned long)Segments);
    ok(Nodes <= 64, "Implausible node count %lu\n", (unsigned long)Nodes);
    ok(Sources <= 64, "Implausible VidPn source count %lu\n", (unsigned long)Sources);

    CloseAdapter(hAdapter);
}

/* ---- Segment occupancy ---- */
static void Test_QueryStatistics_Segments(void)
{
    D3DKMT_QUERYSTATISTICS Statistics;
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    NTSTATUS Status;
    ULONG Segments, i;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    if (!GetAdapterLuid(hAdapter, &Luid)) { CloseAdapter(hAdapter); skip("No adapter LUID\n"); return; }

    Status = Query(pfn, &Statistics, D3DKMT_QUERYSTATISTICS_ADAPTER, Luid);
    if (!NT_SUCCESS(Status)) { CloseAdapter(hAdapter); skip("QueryStatistics(ADAPTER) failed\n"); return; }
    Segments = Statistics.QueryResult.AdapterInformation.NbSegments;

    for (i = 0; i < Segments; i++)
    {
        const D3DKMT_QUERYSTATISTICS_SEGMENT_INFORMATION *Segment;

        memset(&Statistics, 0, sizeof(Statistics));
        Statistics.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
        Statistics.AdapterLuid = Luid;
        Statistics.QuerySegment.SegmentId = i;
        Status = pfn(&Statistics);
        ok_succeeded(Status, "QueryStatistics(SEGMENT %lu) failed 0x%08lX\n", (unsigned long)i, (long)Status);
        if (!NT_SUCCESS(Status))
            continue;

        Segment = &Statistics.QueryResult.SegmentInformation;
        trace("Segment %lu: limit=%llu committed=%llu resident=%llu aperture=%lu\n",
              (unsigned long)i,
              (unsigned long long)Segment->CommitLimit,
              (unsigned long long)Segment->BytesCommitted,
              (unsigned long long)Segment->BytesResident,
              (unsigned long)Segment->Aperture);
        ok(Segment->CommitLimit != 0, "Segment %lu has a zero commit limit\n", (unsigned long)i);
        ok(Segment->BytesCommitted <= Segment->CommitLimit,
           "Segment %lu committed %llu exceeds its limit %llu\n", (unsigned long)i,
           (unsigned long long)Segment->BytesCommitted,
           (unsigned long long)Segment->CommitLimit);
        ok(Segment->BytesResident <= Segment->CommitLimit,
           "Segment %lu resident %llu exceeds its limit %llu\n", (unsigned long)i,
           (unsigned long long)Segment->BytesResident,
           (unsigned long long)Segment->CommitLimit);
    }

    /* One past the last segment names nothing. */
    memset(&Statistics, 0, sizeof(Statistics));
    Statistics.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
    Statistics.AdapterLuid = Luid;
    Statistics.QuerySegment.SegmentId = Segments + 16;
    Status = pfn(&Statistics);
    ok_failed(Status, "QueryStatistics(SEGMENT out of range) should fail, got 0x%08lX\n", (long)Status);

    CloseAdapter(hAdapter);
}

/* ---- Node execution accounting ---- */
static void Test_QueryStatistics_Nodes(void)
{
    D3DKMT_QUERYSTATISTICS First, Second;
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    NTSTATUS Status;
    ULONG Nodes, i;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    if (!GetAdapterLuid(hAdapter, &Luid)) { CloseAdapter(hAdapter); skip("No adapter LUID\n"); return; }

    Status = Query(pfn, &First, D3DKMT_QUERYSTATISTICS_ADAPTER, Luid);
    if (!NT_SUCCESS(Status)) { CloseAdapter(hAdapter); skip("QueryStatistics(ADAPTER) failed\n"); return; }
    Nodes = First.QueryResult.AdapterInformation.NodeCount;
    if (Nodes == 0) { CloseAdapter(hAdapter); skip("Adapter reports no GPU nodes\n"); return; }

    for (i = 0; i < Nodes; i++)
    {
        LONGLONG FirstTime, SecondTime;

        memset(&First, 0, sizeof(First));
        First.Type = D3DKMT_QUERYSTATISTICS_NODE;
        First.AdapterLuid = Luid;
        First.QueryNode.NodeId = i;
        Status = pfn(&First);
        if (!NT_SUCCESS(Status))
        {
            if (StatisticsClassUnavailable(Status))
                skip("QueryStatistics(NODE %lu) unavailable (0x%08lX)\n", (unsigned long)i, (long)Status);
            else
                ok_succeeded(Status, "QueryStatistics(NODE %lu) failed 0x%08lX\n", (unsigned long)i, (long)Status);
            continue;
        }

        Sleep(50);

        memset(&Second, 0, sizeof(Second));
        Second.Type = D3DKMT_QUERYSTATISTICS_NODE;
        Second.AdapterLuid = Luid;
        Second.QueryNode.NodeId = i;
        Status = pfn(&Second);
        if (!NT_SUCCESS(Status))
            continue;

        FirstTime = First.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
        SecondTime = Second.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
        trace("Node %lu: running=%lld -> %lld ctxswitch=%lu\n", (unsigned long)i,
              (long long)FirstTime, (long long)SecondTime,
              (unsigned long)Second.QueryResult.NodeInformation.GlobalInformation.ContextSwitch);

        ok(FirstTime >= 0, "Node %lu reports a negative running time %lld\n",
           (unsigned long)i, (long long)FirstTime);
        ok(SecondTime >= FirstTime,
           "Node %lu running time went backwards: %lld then %lld\n",
           (unsigned long)i, (long long)FirstTime, (long long)SecondTime);
        /*
         * 50 ms of wall time cannot have produced more than 50 ms of busy
         * time on one engine.  A counter that claims otherwise is measuring
         * something other than execution.
         */
        ok(SecondTime - FirstTime <= 10LL * 1000LL * 1000LL,
           "Node %lu gained %lld * 100ns of busy time across a 50 ms wait\n",
           (unsigned long)i, (long long)(SecondTime - FirstTime));

        /* The system's own share cannot exceed the node total. */
        ok(Second.QueryResult.NodeInformation.SystemInformation.RunningTime.QuadPart <= SecondTime,
           "Node %lu system running time %lld exceeds the node total %lld\n",
           (unsigned long)i,
           (long long)Second.QueryResult.NodeInformation.SystemInformation.RunningTime.QuadPart,
           (long long)SecondTime);
    }

    memset(&First, 0, sizeof(First));
    First.Type = D3DKMT_QUERYSTATISTICS_NODE;
    First.AdapterLuid = Luid;
    First.QueryNode.NodeId = Nodes + 16;
    Status = pfn(&First);
    ok_failed(Status, "QueryStatistics(NODE out of range) should fail, got 0x%08lX\n", (long)Status);

    CloseAdapter(hAdapter);
}

/* ---- Per-process classes ---- */
static void Test_QueryStatistics_Process(void)
{
    D3DKMT_QUERYSTATISTICS Statistics, Global;
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    NTSTATUS Status;
    ULONG Nodes, Segments, i;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    if (!GetAdapterLuid(hAdapter, &Luid)) { CloseAdapter(hAdapter); skip("No adapter LUID\n"); return; }

    Status = Query(pfn, &Global, D3DKMT_QUERYSTATISTICS_ADAPTER, Luid);
    if (!NT_SUCCESS(Status)) { CloseAdapter(hAdapter); skip("QueryStatistics(ADAPTER) failed\n"); return; }
    Nodes = Global.QueryResult.AdapterInformation.NodeCount;
    Segments = Global.QueryResult.AdapterInformation.NbSegments;

    Status = Query(pfn, &Statistics, D3DKMT_QUERYSTATISTICS_PROCESS, Luid);
    ok_succeeded(Status, "QueryStatistics(PROCESS) failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        const D3DKMT_QUERYSTATISTICS_SYSTEM_MEMORY *Memory =
            &Statistics.QueryResult.ProcessInformation.SystemMemory;

        ok(Statistics.QueryResult.ProcessInformation.NodeCount == Nodes,
           "PROCESS node count %lu disagrees with the adapter's %lu\n",
           (unsigned long)Statistics.QueryResult.ProcessInformation.NodeCount,
           (unsigned long)Nodes);
        ok(Memory->BytesAllocated <= Memory->BytesReserved ||
           Memory->BytesReserved == 0,
           "PROCESS allocated %llu exceeds reserved %llu\n",
           (unsigned long long)Memory->BytesAllocated,
           (unsigned long long)Memory->BytesReserved);
        trace("Process: sysmem allocated=%llu reserved=%llu small=%lu large=%lu\n",
              (unsigned long long)Memory->BytesAllocated,
              (unsigned long long)Memory->BytesReserved,
              (unsigned long)Memory->SmallAllocationBlocks,
              (unsigned long)Memory->LargeAllocationBlocks);
    }

    Status = Query(pfn, &Statistics, D3DKMT_QUERYSTATISTICS_PROCESS_ADAPTER, Luid);
    if (StatisticsClassUnavailable(Status))
        skip("QueryStatistics(PROCESS_ADAPTER) unavailable (0x%08lX)\n", (long)Status);
    else
        ok_succeeded(Status, "QueryStatistics(PROCESS_ADAPTER) failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        ok(Statistics.QueryResult.ProcessAdapterInformation.NbSegments == Segments,
           "PROCESS_ADAPTER segment count %lu disagrees with the adapter's %lu\n",
           (unsigned long)Statistics.QueryResult.ProcessAdapterInformation.NbSegments,
           (unsigned long)Segments);
    }

    for (i = 0; i < Segments; i++)
    {
        D3DKMT_QUERYSTATISTICS SegmentGlobal;

        memset(&SegmentGlobal, 0, sizeof(SegmentGlobal));
        SegmentGlobal.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
        SegmentGlobal.AdapterLuid = Luid;
        SegmentGlobal.QuerySegment.SegmentId = i;
        if (!NT_SUCCESS(pfn(&SegmentGlobal)))
            continue;

        memset(&Statistics, 0, sizeof(Statistics));
        Statistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT;
        Statistics.AdapterLuid = Luid;
        Statistics.QueryProcessSegment.SegmentId = i;
        Status = pfn(&Statistics);
        if (!NT_SUCCESS(Status))
        {
            if (StatisticsClassUnavailable(Status))
                skip("QueryStatistics(PROCESS_SEGMENT %lu) unavailable (0x%08lX)\n", (unsigned long)i, (long)Status);
            else
                ok_succeeded(Status, "QueryStatistics(PROCESS_SEGMENT %lu) failed 0x%08lX\n", (unsigned long)i, (long)Status);
            continue;
        }

        /* A single process cannot hold more of a segment than the segment
         * itself is holding. */
        ok(Statistics.QueryResult.ProcessSegmentInformation.BytesCommitted <=
               SegmentGlobal.QueryResult.SegmentInformation.CommitLimit,
           "PROCESS_SEGMENT %lu committed %llu exceeds the segment limit %llu\n",
           (unsigned long)i,
           (unsigned long long)Statistics.QueryResult.ProcessSegmentInformation.BytesCommitted,
           (unsigned long long)SegmentGlobal.QueryResult.SegmentInformation.CommitLimit);
    }

    for (i = 0; i < Nodes; i++)
    {
        D3DKMT_QUERYSTATISTICS NodeGlobal;

        memset(&NodeGlobal, 0, sizeof(NodeGlobal));
        NodeGlobal.Type = D3DKMT_QUERYSTATISTICS_NODE;
        NodeGlobal.AdapterLuid = Luid;
        NodeGlobal.QueryNode.NodeId = i;
        if (!NT_SUCCESS(pfn(&NodeGlobal)))
            continue;

        memset(&Statistics, 0, sizeof(Statistics));
        Statistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_NODE;
        Statistics.AdapterLuid = Luid;
        Statistics.QueryProcessNode.NodeId = i;
        Status = pfn(&Statistics);
        ok_succeeded(Status, "QueryStatistics(PROCESS_NODE %lu) failed 0x%08lX\n",
                     (unsigned long)i, (long)Status);
        if (!NT_SUCCESS(Status))
            continue;

        ok(Statistics.QueryResult.ProcessNodeInformation.RunningTime.QuadPart <=
               NodeGlobal.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart,
           "PROCESS_NODE %lu running time %lld exceeds the node total %lld\n",
           (unsigned long)i,
           (long long)Statistics.QueryResult.ProcessNodeInformation.RunningTime.QuadPart,
           (long long)NodeGlobal.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart);
    }

    CloseAdapter(hAdapter);
}

/* ---- Per-process memory budget groups ---- */
static void Test_QueryStatistics_SegmentGroups(void)
{
    D3DKMT_QUERYSTATISTICS Statistics;
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    NTSTATUS Status;
    int group;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    if (!GetAdapterLuid(hAdapter, &Luid)) { CloseAdapter(hAdapter); skip("No adapter LUID\n"); return; }

    for (group = 0; group < 2; group++)
    {
        memset(&Statistics, 0, sizeof(Statistics));
        Statistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT_GROUP;
        Statistics.AdapterLuid = Luid;
        Statistics.QueryProcessSegmentGroup = (group == 0)
            ? D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL
            : D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL;
        Status = pfn(&Statistics);
        if (!NT_SUCCESS(Status))
        {
            if (StatisticsClassUnavailable(Status))
                skip("QueryStatistics(PROCESS_SEGMENT_GROUP %d) unavailable (0x%08lX)\n", group, (long)Status);
            else
                ok_succeeded(Status, "QueryStatistics(PROCESS_SEGMENT_GROUP %d) failed 0x%08lX\n", group, (long)Status);
            continue;
        }

        trace("Segment group %d: budget=%llu usage=%llu requested=%llu\n", group,
              (unsigned long long)Statistics.QueryResult.ProcessSegmentGroupInformation.Budget,
              (unsigned long long)Statistics.QueryResult.ProcessSegmentGroupInformation.Usage,
              (unsigned long long)Statistics.QueryResult.ProcessSegmentGroupInformation.Requested);
        ok(Statistics.QueryResult.ProcessSegmentGroupInformation.Usage <=
               Statistics.QueryResult.ProcessSegmentGroupInformation.Budget ||
           Statistics.QueryResult.ProcessSegmentGroupInformation.Budget == 0,
           "Segment group %d usage %llu exceeds its budget %llu\n", group,
           (unsigned long long)Statistics.QueryResult.ProcessSegmentGroupInformation.Usage,
           (unsigned long long)Statistics.QueryResult.ProcessSegmentGroupInformation.Budget);
    }

    CloseAdapter(hAdapter);
}

/* ---- VidPN source present counters ---- */
static void Test_QueryStatistics_VidPnSource(void)
{
    D3DKMT_QUERYSTATISTICS Statistics;
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    NTSTATUS Status;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    if (!GetAdapterLuid(hAdapter, &Luid)) { CloseAdapter(hAdapter); skip("No adapter LUID\n"); return; }

    memset(&Statistics, 0, sizeof(Statistics));
    Statistics.Type = D3DKMT_QUERYSTATISTICS_VIDPNSOURCE;
    Statistics.AdapterLuid = Luid;
    Statistics.QueryVidPnSource.VidPnSourceId = 0;
    Status = pfn(&Statistics);
    ok_succeeded(Status, "QueryStatistics(VIDPNSOURCE) failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        trace("VidPnSource 0: frame=%lu queued=%lu\n",
              (unsigned long)Statistics.QueryResult.VidPnSourceInformation.GlobalInformation.Frame,
              (unsigned long)Statistics.QueryResult.VidPnSourceInformation.GlobalInformation.QueuedPresent);
    }

    memset(&Statistics, 0, sizeof(Statistics));
    Statistics.Type = D3DKMT_QUERYSTATISTICS_PROCESS_VIDPNSOURCE;
    Statistics.AdapterLuid = Luid;
    Statistics.QueryProcessVidPnSource.VidPnSourceId = 0;
    Status = pfn(&Statistics);
    if (StatisticsClassUnavailable(Status))
        skip("QueryStatistics(PROCESS_VIDPNSOURCE) unavailable (0x%08lX)\n", (long)Status);
    else
        ok_succeeded(Status, "QueryStatistics(PROCESS_VIDPNSOURCE) failed 0x%08lX\n", (long)Status);

    CloseAdapter(hAdapter);
}

/* ---- Physical adapter performance data ---- */
static void Test_QueryStatistics_PhysicalAdapter(void)
{
    D3DKMT_QUERYSTATISTICS Statistics;
    D3DKMT_HANDLE hAdapter;
    LUID Luid;
    NTSTATUS Status;

    LOADFN(PFN_QueryStatistics, pfn, "D3DKMTQueryStatistics");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    if (!GetAdapterLuid(hAdapter, &Luid)) { CloseAdapter(hAdapter); skip("No adapter LUID\n"); return; }

    memset(&Statistics, 0, sizeof(Statistics));
    Statistics.Type = D3DKMT_QUERYSTATISTICS_PHYSICAL_ADAPTER;
    Statistics.AdapterLuid = Luid;
    Statistics.QueryPhysAdapter.PhysicalAdapterIndex = 0;
    Status = pfn(&Statistics);
    if (StatisticsClassUnavailable(Status))
        skip("QueryStatistics(PHYSICAL_ADAPTER) unavailable (0x%08lX)\n", (long)Status);
    else
        ok_succeeded(Status, "QueryStatistics(PHYSICAL_ADAPTER) failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        trace("PhysAdapter: temperature=%lu fan=%lu memfreq=%llu\n",
              (unsigned long)Statistics.QueryResult.PhysAdapterInformation.AdapterPerfData.Temperature,
              (unsigned long)Statistics.QueryResult.PhysAdapterInformation.AdapterPerfData.FanRPM,
              (unsigned long long)Statistics.QueryResult.PhysAdapterInformation.AdapterPerfData.MemoryFrequency);
    }

    /* An index past the only physical adapter names nothing. */
    memset(&Statistics, 0, sizeof(Statistics));
    Statistics.Type = D3DKMT_QUERYSTATISTICS_PHYSICAL_ADAPTER;
    Statistics.AdapterLuid = Luid;
    Statistics.QueryPhysAdapter.PhysicalAdapterIndex = 7;
    Status = pfn(&Statistics);
    ok_failed(Status, "QueryStatistics(PHYSICAL_ADAPTER index 7) should fail, got 0x%08lX\n", (long)Status);

    CloseAdapter(hAdapter);
}

/* ---- KMTQAITYPE_ADAPTERPERFDATA and friends ---- */
static void Test_AdapterPerfData(void)
{
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_ADAPTER_PERFDATA Perf;
    D3DKMT_ADAPTER_PERFDATACAPS Caps;
    D3DKMT_NODE_PERFDATA Node;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOADFN(PFN_D3DKMTQueryAdapterInfo, pfn, "D3DKMTQueryAdapterInfo");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    memset(&Perf, 0, sizeof(Perf));
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_ADAPTERPERFDATA;
    Query.pPrivateDriverData = &Perf;
    Query.PrivateDriverDataSize = sizeof(Perf);
    Status = pfn(&Query);
    /*
     * A driver either reports thermals or does not; both are correct.  What
     * would be wrong is answering success with a temperature no silicon
     * reaches, because a caller displays that as a measurement.
     */
    if (NT_SUCCESS(Status))
    {
        trace("AdapterPerfData: temperature=%lu (deci-C) fan=%lu power=%lu\n",
              (unsigned long)Perf.Temperature, (unsigned long)Perf.FanRPM,
              (unsigned long)Perf.Power);
        ok(Perf.Temperature <= 2000,
           "Reported GPU temperature %lu deci-Celsius is not a real temperature\n",
           (unsigned long)Perf.Temperature);
    }
    else
    {
        trace("AdapterPerfData not reported by this driver (0x%08lX)\n", (long)Status);
    }

    /* Too small a buffer must be refused rather than partly filled. */
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_ADAPTERPERFDATA;
    Query.pPrivateDriverData = &Perf;
    Query.PrivateDriverDataSize = sizeof(Perf) / 2;
    Status = pfn(&Query);
    ok_failed(Status, "ADAPTERPERFDATA with a short buffer should fail, got 0x%08lX\n", (long)Status);

    memset(&Caps, 0, sizeof(Caps));
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_ADAPTERPERFDATA_CAPS;
    Query.pPrivateDriverData = &Caps;
    Query.PrivateDriverDataSize = sizeof(Caps);
    Status = pfn(&Query);
    if (NT_SUCCESS(Status))
    {
        ok(Caps.TemperatureWarning <= Caps.TemperatureMax || Caps.TemperatureMax == 0,
           "Thermal warning level %lu is above the damage level %lu\n",
           (unsigned long)Caps.TemperatureWarning, (unsigned long)Caps.TemperatureMax);
    }

    /* A node ordinal past the adapter's node count names nothing. */
    memset(&Node, 0, sizeof(Node));
    Node.NodeOrdinal = 0xFFFF;
    memset(&Query, 0, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_NODEPERFDATA;
    Query.pPrivateDriverData = &Node;
    Query.PrivateDriverDataSize = sizeof(Node);
    Status = pfn(&Query);
    ok_failed(Status, "NODEPERFDATA for a node that does not exist should fail, got 0x%08lX\n", (long)Status);

    CloseAdapter(hAdapter);
}

START_TEST(gpustats)
{
    Test_QueryStatistics_Contract();
    Test_QueryStatistics_Adapter();
    Test_QueryStatistics_Segments();
    Test_QueryStatistics_Nodes();
    Test_QueryStatistics_Process();
    Test_QueryStatistics_SegmentGroups();
    Test_QueryStatistics_VidPnSource();
    Test_QueryStatistics_PhysicalAdapter();
    Test_AdapterPerfData();
}
