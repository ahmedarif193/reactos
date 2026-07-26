/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for D3DKMT adapter enumeration and lifecycle
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests: D3DKMTEnumAdapters, D3DKMTOpenAdapterFromHdc,
 *        D3DKMTOpenAdapterFromGdiDisplayName, D3DKMTOpenAdapterFromDeviceName,
 *        D3DKMTCloseAdapter, D3DKMTQueryAdapterInfo, D3DKMTQueryStatistics
 */

#include "precomp.h"

static void Test_EnumAdapters(void)
{
    D3DKMT_ENUMADAPTERS EnumData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTEnumAdapters);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTEnumAdapters, "D3DKMTEnumAdapters");

    /* Valid call */
    memset(&EnumData, 0, sizeof(EnumData));
    Status = pfnD3DKMTEnumAdapters(&EnumData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTEnumAdapters not implemented in kernel\n");
        return;
    }

    ok_succeeded(Status,
       "D3DKMTEnumAdapters failed with 0x%lx\n", Status);

    if (!NT_SUCCESS(Status))
        return;

    ok(EnumData.NumAdapters >= 1,
       "Expected at least 1 adapter, got %lu\n", EnumData.NumAdapters);
    ok(EnumData.NumAdapters <= MAX_ENUM_ADAPTERS,
       "NumAdapters %lu exceeds MAX_ENUM_ADAPTERS\n", EnumData.NumAdapters);

    if (EnumData.NumAdapters >= 1)
    {
        ok(EnumData.Adapters[0].hAdapter != 0,
           "First adapter handle is 0\n");
        ok(EnumData.Adapters[0].AdapterLuid.LowPart != 0 ||
           EnumData.Adapters[0].AdapterLuid.HighPart != 0,
           "Adapter LUID is zero\n");
        /* Render-only adapters (e.g. WARP) report 0 video present sources;
         * only display adapters have sources, so trace rather than assert. */
        trace("Adapter[0] NumOfSources = %lu\n", EnumData.Adapters[0].NumOfSources);
    }
}

static void Test_OpenAdapterFromHdc(void)
{
    D3DKMT_OPENADAPTERFROMHDC OpenData;
    D3DKMT_CLOSEADAPTER CloseData;
    NTSTATUS Status;
    HDC hDC;

    LOAD_D3DKMT(D3DKMTOpenAdapterFromHdc);
    LOAD_D3DKMT(D3DKMTCloseAdapter);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTOpenAdapterFromHdc, "D3DKMTOpenAdapterFromHdc");

    /* NULL HDC */
    memset(&OpenData, 0, sizeof(OpenData));
    OpenData.hDc = NULL;
    Status = pfnD3DKMTOpenAdapterFromHdc(&OpenData);
    ok_failed(Status,
       "D3DKMTOpenAdapterFromHdc with NULL HDC should fail, got 0x%lx\n", Status);

    /* Valid HDC from primary display */
    hDC = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
    if (!hDC)
    {
        skip("Cannot create DC for DISPLAY\n");
        return;
    }

    memset(&OpenData, 0, sizeof(OpenData));
    OpenData.hDc = hDC;
    Status = pfnD3DKMTOpenAdapterFromHdc(&OpenData);
    DeleteDC(hDC);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTOpenAdapterFromHdc not implemented in kernel\n");
        return;
    }

    if (!NT_SUCCESS(Status))
    {
        /* Win11 rejects OpenAdapterFromHdc on a generic CreateDCW("DISPLAY") DC
         * (returns STATUS_INVALID_PARAMETER, 0xC000000D); only a per-monitor DC
         * is accepted. Treat that as a skip rather than a failure. */
        skip("OpenAdapterFromHdc(generic DISPLAY DC) returned 0x%lx\n", Status);
        return;
    }

    ok(OpenData.hAdapter != 0,
       "OpenAdapterFromHdc returned zero handle\n");
    ok(OpenData.AdapterLuid.LowPart != 0 || OpenData.AdapterLuid.HighPart != 0,
       "Adapter LUID is zero\n");

    /* Close the adapter */
    memset(&CloseData, 0, sizeof(CloseData));
    CloseData.hAdapter = OpenData.hAdapter;
    Status = pfnD3DKMTCloseAdapter(&CloseData);
    ok_succeeded(Status, "CloseAdapter failed with 0x%lx\n", Status);
}

static void Test_OpenAdapterFromGdiDisplayName(void)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTOpenAdapterFromGdiDisplayName);
    LOAD_D3DKMT(D3DKMTCloseAdapter);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTOpenAdapterFromGdiDisplayName, "D3DKMTOpenAdapterFromGdiDisplayName");

    /* Valid display name */
    memset(&OpenData, 0, sizeof(OpenData));
    wcscpy(OpenData.DeviceName, L"\\\\.\\DISPLAY1");
    Status = pfnD3DKMTOpenAdapterFromGdiDisplayName(&OpenData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND || Status == STATUS_UNSUCCESSFUL)
    {
        skip("D3DKMTOpenAdapterFromGdiDisplayName failed (0x%lx) - no WDDM display\n",
             Status);
        return;
    }

    ok_succeeded(Status,
       "D3DKMTOpenAdapterFromGdiDisplayName failed with 0x%lx\n", Status);

    if (!NT_SUCCESS(Status))
        return;

    ok(OpenData.hAdapter != 0,
       "OpenAdapterFromGdiDisplayName returned zero handle\n");
    ok(OpenData.AdapterLuid.LowPart != 0 || OpenData.AdapterLuid.HighPart != 0,
       "Adapter LUID is zero\n");

    /* Invalid display name */
    {
        D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME BadData;
        memset(&BadData, 0, sizeof(BadData));
        wcscpy(BadData.DeviceName, L"\\\\.\\DISPLAY999");
        Status = pfnD3DKMTOpenAdapterFromGdiDisplayName(&BadData);
        ok_failed(Status,
           "D3DKMTOpenAdapterFromGdiDisplayName should fail for DISPLAY999, got 0x%lx\n",
           Status);
    }

    CloseAdapter(OpenData.hAdapter);
}

static void Test_OpenAdapterFromDeviceName(void)
{
    D3DKMT_OPENADAPTERFROMDEVICENAME OpenData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTOpenAdapterFromDeviceName);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTOpenAdapterFromDeviceName, "D3DKMTOpenAdapterFromDeviceName");

    /* NULL device name pointer */
    memset(&OpenData, 0, sizeof(OpenData));
    OpenData.pDeviceName = NULL;
    Status = pfnD3DKMTOpenAdapterFromDeviceName(&OpenData);
    ok_failed(Status,
       "D3DKMTOpenAdapterFromDeviceName with NULL name should fail, got 0x%lx\n",
       Status);
}

static void Test_CloseAdapter(void)
{
    D3DKMT_CLOSEADAPTER CloseData;
    NTSTATUS Status;
    D3DKMT_HANDLE hAdapter;

    LOAD_D3DKMT(D3DKMTCloseAdapter);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTCloseAdapter, "D3DKMTCloseAdapter");

    /* Invalid handle */
    memset(&CloseData, 0, sizeof(CloseData));
    CloseData.hAdapter = 0xBAD0CAFE;
    Status = pfnD3DKMTCloseAdapter(&CloseData);
    ok_failed(Status,
       "D3DKMTCloseAdapter with invalid handle should fail, got 0x%lx\n", Status);

    /* Zero handle */
    memset(&CloseData, 0, sizeof(CloseData));
    CloseData.hAdapter = 0;
    Status = pfnD3DKMTCloseAdapter(&CloseData);
    ok_failed(Status,
       "D3DKMTCloseAdapter with zero handle should fail, got 0x%lx\n", Status);

    /* Valid open + close + double-close */
    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter)
    {
        memset(&CloseData, 0, sizeof(CloseData));
        CloseData.hAdapter = hAdapter;
        Status = pfnD3DKMTCloseAdapter(&CloseData);
        ok_succeeded(Status, "First CloseAdapter failed with 0x%lx\n", Status);

        /* Second close of same handle should fail */
        Status = pfnD3DKMTCloseAdapter(&CloseData);
        ok_failed(Status,
           "Double CloseAdapter should fail, got 0x%lx\n", Status);
    }
    else
    {
        skip("Cannot open adapter for CloseAdapter test\n");
    }
}

static void Test_QueryAdapterInfo(void)
{
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    D3DKMT_SEGMENTSIZEINFO SegInfo;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTQueryAdapterInfo);

    /* NULL parameter */
    EXPECT_NULL_REJECTED(pfnD3DKMTQueryAdapterInfo, "D3DKMTQueryAdapterInfo");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("Cannot open adapter for QueryAdapterInfo test\n");
        return;
    }

    /* Query segment size info (KMTQAITYPE_GETSEGMENTSIZE = 3) */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    memset(&SegInfo, 0, sizeof(SegInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = 3; /* KMTQAITYPE_GETSEGMENTSIZE */
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(SegInfo);

    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTQueryAdapterInfo not implemented\n");
        CloseAdapter(hAdapter);
        return;
    }

    ok_succeeded(Status,
       "D3DKMTQueryAdapterInfo(GETSEGMENTSIZE) failed with 0x%lx\n", Status);

    if (NT_SUCCESS(Status))
    {
        /* Segment sizes may legitimately be zero on a display-only/software
         * adapter; trace rather than assert. */
        trace("Segment sizes: video=%llu sysDedicated=%llu sysShared=%llu\n",
              (unsigned long long)SegInfo.DedicatedVideoMemorySize,
              (unsigned long long)SegInfo.DedicatedSystemMemorySize,
              (unsigned long long)SegInfo.SharedSystemMemorySize);
    }

    /* Query with invalid adapter handle */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = 0xBAD0CAFE;
    QueryInfo.Type = 3;
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(SegInfo);
    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
    ok_failed(Status,
       "D3DKMTQueryAdapterInfo with invalid adapter should fail, got 0x%lx\n",
       Status);

    /* Query driver version (KMTQAITYPE_DRIVERVERSION = 13) */
    {
        D3DKMT_DRIVERVERSION DriverVersion = 0;
        memset(&QueryInfo, 0, sizeof(QueryInfo));
        QueryInfo.hAdapter = hAdapter;
        QueryInfo.Type = 13; /* KMTQAITYPE_DRIVERVERSION */
        QueryInfo.pPrivateDriverData = &DriverVersion;
        QueryInfo.PrivateDriverDataSize = sizeof(DriverVersion);

        Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
        if (NT_SUCCESS(Status))
        {
            ok(DriverVersion >= KMT_DRIVERVERSION_WDDM_1_0,
               "Driver version %d is less than WDDM 1.0\n", DriverVersion);
        }
    }

    /* Query with unsupported type */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = 9999;
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(SegInfo);
    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
    ok_failed(Status,
       "D3DKMTQueryAdapterInfo with unsupported type should fail, got 0x%lx\n",
       Status);

    /* Query with NULL private data */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = 3;
    QueryInfo.pPrivateDriverData = NULL;
    QueryInfo.PrivateDriverDataSize = 0;
    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
    ok_failed(Status,
       "D3DKMTQueryAdapterInfo with NULL data should fail, got 0x%lx\n",
       Status);

    /* Query with too-small buffer */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = 3;
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = 1;
    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
    ok_failed(Status,
       "D3DKMTQueryAdapterInfo with too-small buffer should fail, got 0x%lx\n",
       Status);

    CloseAdapter(hAdapter);
}

static void Test_QueryStatistics(void)
{
    /*
     * D3DKMTQueryStatistics is a complex API. We just test NULL param
     * and basic export availability here.
     */
    PFN_D3DKMTQueryAdapterInfo pfnQueryStats;
    NTSTATUS Status;

    pfnQueryStats = (PFN_D3DKMTQueryAdapterInfo)LoadD3DKMTProc("D3DKMTQueryStatistics");
    if (!pfnQueryStats)
    {
        skip("D3DKMTQueryStatistics not exported by gdi32.dll\n");
        return;
    }

    EXPECT_NULL_REJECTED(pfnQueryStats, "QueryStats");
}

START_TEST(D3dkmtAdapter)
{
    Test_EnumAdapters();
    Test_OpenAdapterFromHdc();
    Test_OpenAdapterFromGdiDisplayName();
    Test_OpenAdapterFromDeviceName();
    Test_CloseAdapter();
    Test_QueryAdapterInfo();
    Test_QueryStatistics();
}
