/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */

/* GPU engine counters through the same public KMT queries used by Task Manager. */
#pragma once

static INT
RunGpuStatsProbe(VOID)
{
    typedef LONG (APIENTRY *OPEN)(D3DKMT_OPENADAPTERFROMHDC *);
    typedef LONG (APIENTRY *CLOSE)(const D3DKMT_CLOSEADAPTER *);
    typedef LONG (APIENTRY *QUERY)(D3DKMT_QUERYSTATISTICS *);
    typedef LONG (APIENTRY *INFO)(const D3DKMT_QUERYADAPTERINFO *);
    HMODULE Gdi = GetModuleHandleW(L"gdi32.dll");
    OPEN Open = (OPEN)GetProcAddress(Gdi, "D3DKMTOpenAdapterFromHdc");
    CLOSE Close = (CLOSE)GetProcAddress(Gdi, "D3DKMTCloseAdapter");
    QUERY Query = (QUERY)GetProcAddress(Gdi, "D3DKMTQueryStatistics");
    INFO Info = (INFO)GetProcAddress(Gdi, "D3DKMTQueryAdapterInfo");
    D3DKMT_OPENADAPTERFROMHDC Adapter = {0};
    D3DKMT_CLOSEADAPTER Closing = {0};
    D3DKMT_QUERYSTATISTICS Stats = {0};
    D3DKMT_QUERYADAPTERINFO Information = {0};
    D3DKMT_DRIVERVERSION Version = 0;
    ULONGLONG Previous[32] = {0};
    LARGE_INTEGER SampleTime[32] = {{0}};
    ULONG Nodes = 0, Segments = 0, Node, Segment, Sample, Failures = 0;
    LONG Status;

    if (!Open || !Close || !Query || !Info)
        return 1;
    Adapter.hDc = GetDC(NULL);
    if (!Adapter.hDc)
        return 1;
    Status = Open(&Adapter);
    if (Status < 0 || !Adapter.hAdapter)
    {
        TestPrint("GPU_STATS_ERROR open=0x%08lx\n", Status);
        ReleaseDC(NULL, Adapter.hDc);
        return 1;
    }

    Information.hAdapter = Adapter.hAdapter;
    Information.Type = KMTQAITYPE_DRIVERVERSION;
    Information.pPrivateDriverData = &Version;
    Information.PrivateDriverDataSize = sizeof(Version);
    Status = Info(&Information);
    TestPrint("GPU_STATS_VERSION status=0x%08lx version=%u\n", Status, Version);
    if (Status < 0) ++Failures;
    Stats.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    Stats.AdapterLuid = Adapter.AdapterLuid;
    Status = Query(&Stats);
    if (Status >= 0)
    {
        Nodes = Stats.QueryResult.AdapterInformation.NodeCount;
        Segments = Stats.QueryResult.AdapterInformation.NbSegments;
    }
    TestPrint("GPU_STATS_BEGIN status=0x%08lx nodes=%lu\n", Status, Nodes);
    if (Status < 0 || Nodes == 0 || Nodes > ARRAYSIZE(Previous))
    {
        ++Failures;
        goto Cleanup;
    }

    for (Node = 0; Node < Nodes; ++Node)
    {
        D3DKMT_NODEMETADATA Metadata = {0};
        Metadata.NodeOrdinalAndAdapterIndex = Node;
        Information.Type = KMTQAITYPE_NODEMETADATA;
        Information.pPrivateDriverData = &Metadata;
        Information.PrivateDriverDataSize = sizeof(Metadata);
        Status = Info(&Information);
        TestPrint("GPU_STATS_ENGINE node=%lu status=0x%08lx type=%u\n", Node, Status, Metadata.NodeData.EngineType);
        if (Status < 0) ++Failures;
    }

    for (Sample = 0; Sample <= 10; ++Sample)
    {
        if (Sample) Sleep(1000);
        for (Node = 0; Node < Nodes; ++Node)
        {
            LARGE_INTEGER Now;
            ULONGLONG Running = 0, Elapsed = 0, Busy = 0;
            ZeroMemory(&Stats, sizeof(Stats));
            Stats.Type = D3DKMT_QUERYSTATISTICS_NODE;
            Stats.AdapterLuid = Adapter.AdapterLuid;
            Stats.QueryNode.NodeId = Node;
            Status = Query(&Stats);
            QueryPerformanceCounter(&Now);
            if (Status >= 0)
            {
                Running = Stats.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
                if (SampleTime[Node].QuadPart)
                {
                    Elapsed = ElapsedMicroseconds(SampleTime[Node], Now);
                    if (Running >= Previous[Node])
                        Busy = (Running - Previous[Node]) / 10;
                    else
                        ++Failures;
                }
                Previous[Node] = Running;
                SampleTime[Node] = Now;
            }
            else
            {
                SampleTime[Node].QuadPart = 0;
                ++Failures;
            }
            TestPrint("GPU_STATS_SAMPLE sample=%lu node=%lu status=0x%08lx running_100ns=%llu elapsed_us=%llu busy_us=%llu usage_milli_pct=%llu\n", Sample, Node, Status, Running, Elapsed, Busy, Elapsed ? Busy * 100000 / Elapsed : 0);
        }
        if (Sample == 0 || Sample == 10)
        {
            for (Segment = 0; Segment < Segments; ++Segment)
            {
                ZeroMemory(&Stats, sizeof(Stats));
                Stats.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
                Stats.AdapterLuid = Adapter.AdapterLuid;
                Stats.QuerySegment.SegmentId = Segment;
                Status = Query(&Stats);
                if (Status < 0) ++Failures;
                TestPrint("GPU_STATS_MEMORY sample=%lu segment=%lu status=0x%08lx limit=%llu committed=%llu resident=%llu allocations=%lu\n", Sample, Segment, Status, Stats.QueryResult.SegmentInformation.CommitLimit, Stats.QueryResult.SegmentInformation.BytesCommitted, Stats.QueryResult.SegmentInformation.BytesResident, Stats.QueryResult.SegmentInformation.Memory.AllocsResident);
            }
        }
    }

    ZeroMemory(&Stats, sizeof(Stats));
    Stats.Type = D3DKMT_QUERYSTATISTICS_NODE;
    Stats.AdapterLuid = Adapter.AdapterLuid;
    Stats.QueryNode.NodeId = Nodes;
    Status = Query(&Stats);
    TestPrint("GPU_STATS_INVALID_NODE status=0x%08lx\n", Status);
    if (Status != (LONG)0xc000000d) ++Failures;

Cleanup:
    Closing.hAdapter = Adapter.hAdapter;
    Close(&Closing);
    ReleaseDC(NULL, Adapter.hDc);
    TestPrint("GPU_STATS_RESULT failures=%lu\n", Failures);
    return Failures ? 1 : 0;
}
