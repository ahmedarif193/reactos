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
    Status = pfnD3DKMTEnumAdapters(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTEnumAdapters(NULL) returned 0x%lx, expected STATUS_INVALID_PARAMETER\n",
       Status);

    /* Valid call */
    memset(&EnumData, 0, sizeof(EnumData));
    Status = pfnD3DKMTEnumAdapters(&EnumData);

    if (Status == STATUS_PROCEDURE_NOT_FOUND)
    {
        skip("D3DKMTEnumAdapters not implemented in kernel\n");
        return;
    }

    ok(NT_SUCCESS(Status),
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
        ok(EnumData.Adapters[0].NumOfSources >= 1,
           "Expected NumOfSources >= 1, got %lu\n",
           EnumData.Adapters[0].NumOfSources);
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
    Status = pfnD3DKMTOpenAdapterFromHdc(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTOpenAdapterFromHdc(NULL) returned 0x%lx\n", Status);

    /* NULL HDC */
    memset(&OpenData, 0, sizeof(OpenData));
    OpenData.hDc = NULL;
    Status = pfnD3DKMTOpenAdapterFromHdc(&OpenData);
    ok(!NT_SUCCESS(Status),
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

    ok(NT_SUCCESS(Status),
       "D3DKMTOpenAdapterFromHdc failed with 0x%lx\n", Status);

    if (!NT_SUCCESS(Status))
        return;

    ok(OpenData.hAdapter != 0,
       "OpenAdapterFromHdc returned zero handle\n");
    ok(OpenData.AdapterLuid.LowPart != 0 || OpenData.AdapterLuid.HighPart != 0,
       "Adapter LUID is zero\n");

    /* Close the adapter */
    memset(&CloseData, 0, sizeof(CloseData));
    CloseData.hAdapter = OpenData.hAdapter;
    Status = pfnD3DKMTCloseAdapter(&CloseData);
    ok(NT_SUCCESS(Status), "CloseAdapter failed with 0x%lx\n", Status);
}

static void Test_OpenAdapterFromGdiDisplayName(void)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenData;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTOpenAdapterFromGdiDisplayName);
    LOAD_D3DKMT(D3DKMTCloseAdapter);

    /* NULL parameter */
    Status = pfnD3DKMTOpenAdapterFromGdiDisplayName(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTOpenAdapterFromGdiDisplayName(NULL) returned 0x%lx\n", Status);

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

    ok(NT_SUCCESS(Status),
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
        ok(!NT_SUCCESS(Status),
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
    Status = pfnD3DKMTOpenAdapterFromDeviceName(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTOpenAdapterFromDeviceName(NULL) returned 0x%lx\n", Status);

    /* NULL device name pointer */
    memset(&OpenData, 0, sizeof(OpenData));
    OpenData.pDeviceName = NULL;
    Status = pfnD3DKMTOpenAdapterFromDeviceName(&OpenData);
    ok(!NT_SUCCESS(Status),
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
    Status = pfnD3DKMTCloseAdapter(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTCloseAdapter(NULL) returned 0x%lx\n", Status);

    /* Invalid handle */
    memset(&CloseData, 0, sizeof(CloseData));
    CloseData.hAdapter = 0xBAD0CAFE;
    Status = pfnD3DKMTCloseAdapter(&CloseData);
    ok(!NT_SUCCESS(Status),
       "D3DKMTCloseAdapter with invalid handle should fail, got 0x%lx\n", Status);

    /* Zero handle */
    memset(&CloseData, 0, sizeof(CloseData));
    CloseData.hAdapter = 0;
    Status = pfnD3DKMTCloseAdapter(&CloseData);
    ok(!NT_SUCCESS(Status),
       "D3DKMTCloseAdapter with zero handle should fail, got 0x%lx\n", Status);

    /* Valid open + close + double-close */
    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter)
    {
        memset(&CloseData, 0, sizeof(CloseData));
        CloseData.hAdapter = hAdapter;
        Status = pfnD3DKMTCloseAdapter(&CloseData);
        ok(NT_SUCCESS(Status), "First CloseAdapter failed with 0x%lx\n", Status);

        /* Second close of same handle should fail */
        Status = pfnD3DKMTCloseAdapter(&CloseData);
        ok(!NT_SUCCESS(Status),
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
    Status = pfnD3DKMTQueryAdapterInfo(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTQueryAdapterInfo(NULL) returned 0x%lx\n", Status);

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

    ok(NT_SUCCESS(Status),
       "D3DKMTQueryAdapterInfo(GETSEGMENTSIZE) failed with 0x%lx\n", Status);

    if (NT_SUCCESS(Status))
    {
        ok(SegInfo.SharedSystemMemorySize > 0 ||
           SegInfo.DedicatedVideoMemorySize > 0 ||
           SegInfo.DedicatedSystemMemorySize > 0,
           "All segment sizes are zero\n");
    }

    /* Query with invalid adapter handle */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = 0xBAD0CAFE;
    QueryInfo.Type = 3;
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(SegInfo);
    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
    ok(!NT_SUCCESS(Status),
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
    ok(!NT_SUCCESS(Status),
       "D3DKMTQueryAdapterInfo with unsupported type should fail, got 0x%lx\n",
       Status);

    /* Query with NULL private data */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = 3;
    QueryInfo.pPrivateDriverData = NULL;
    QueryInfo.PrivateDriverDataSize = 0;
    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
    ok(!NT_SUCCESS(Status),
       "D3DKMTQueryAdapterInfo with NULL data should fail, got 0x%lx\n",
       Status);

    /* Query with too-small buffer */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    QueryInfo.hAdapter = hAdapter;
    QueryInfo.Type = 3;
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = 1;
    Status = pfnD3DKMTQueryAdapterInfo(&QueryInfo);
    ok(!NT_SUCCESS(Status),
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

    Status = pfnQueryStats(NULL);
    ok(Status == STATUS_INVALID_PARAMETER,
       "D3DKMTQueryStatistics(NULL) returned 0x%lx, expected STATUS_INVALID_PARAMETER\n",
       Status);
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
