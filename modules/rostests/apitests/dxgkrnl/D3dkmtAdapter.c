/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT adapter enumeration and lifecycle tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the adapter enumeration, open, query, and close path through
 * \Device\DxgKrnl IOCTLs.  These tests require a WDDM adapter to be
 * present in the system (i.e., dxgkrnl.sys loaded with a miniport).
 */

#include "precomp.h"

static HANDLE hDxgKrnl = INVALID_HANDLE_VALUE;

static BOOL OpenDxgDevice(void)
{
    hDxgKrnl = OpenDxgKrnl();
    if (hDxgKrnl == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open \\Device\\DxgKrnl — WDDM driver not loaded\n");
        return FALSE;
    }
    return TRUE;
}

static void CloseDxgDevice(void)
{
    if (hDxgKrnl != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
    }
}

static void Test_EnumAdapters(void)
{
    D3DKMT_ENUMADAPTERS EnumAdapters;
    DWORD BytesReturned = 0;
    BOOL Result;

    if (!OpenDxgDevice())
        return;

    /* Zero-init the structure */
    memset(&EnumAdapters, 0, sizeof(EnumAdapters));

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                          &EnumAdapters, sizeof(EnumAdapters),
                          &EnumAdapters, sizeof(EnumAdapters),
                          &BytesReturned);

    if (!Result)
    {
        skip("EnumAdapters IOCTL failed (error %lu) — no adapter present\n",
             GetLastError());
        CloseDxgDevice();
        return;
    }

    /* Must return at least 1 adapter */
    ok(EnumAdapters.NumAdapters >= 1,
       "Expected at least 1 adapter, got %lu\n", EnumAdapters.NumAdapters);

    /* Must not exceed MAX_ENUM_ADAPTERS */
    ok(EnumAdapters.NumAdapters <= MAX_ENUM_ADAPTERS,
       "NumAdapters %lu exceeds MAX_ENUM_ADAPTERS %u\n",
       EnumAdapters.NumAdapters, MAX_ENUM_ADAPTERS);

    /* First adapter handle must be non-zero */
    if (EnumAdapters.NumAdapters >= 1)
    {
        ok(EnumAdapters.Adapters[0].hAdapter != 0,
           "First adapter handle is 0\n");

        /* LUID should be non-trivial (at least LowPart != 0 or HighPart != 0) */
        ok(EnumAdapters.Adapters[0].AdapterLuid.LowPart != 0 ||
           EnumAdapters.Adapters[0].AdapterLuid.HighPart != 0,
           "Adapter LUID is zero — adapter should have a valid LUID\n");

        /* NumOfSources should be at least 1 for a display adapter */
        ok(EnumAdapters.Adapters[0].NumOfSources >= 1,
           "Expected NumOfSources >= 1, got %lu\n",
           EnumAdapters.Adapters[0].NumOfSources);
    }

    /* All adapter handles must be unique */
    if (EnumAdapters.NumAdapters >= 2)
    {
        ULONG i, j;
        for (i = 0; i < EnumAdapters.NumAdapters; ++i)
        {
            for (j = i + 1; j < EnumAdapters.NumAdapters; ++j)
            {
                ok(EnumAdapters.Adapters[i].hAdapter !=
                   EnumAdapters.Adapters[j].hAdapter,
                   "Adapters %lu and %lu have same handle 0x%08X\n",
                   i, j, EnumAdapters.Adapters[i].hAdapter);
            }
        }
    }

    CloseDxgDevice();
}

static void Test_EnumAdapters2(void)
{
    D3DKMT_ENUMADAPTERS2 EnumAdapters2;
    D3DKMT_ADAPTERINFO Buffer[MAX_ENUM_ADAPTERS];
    DWORD BytesReturned = 0;
    BOOL Result;

    if (!OpenDxgDevice())
        return;

    memset(&EnumAdapters2, 0, sizeof(EnumAdapters2));
    memset(Buffer, 0, sizeof(Buffer));
    EnumAdapters2.NumAdapters = MAX_ENUM_ADAPTERS;
    EnumAdapters2.pAdapters = Buffer;

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS2,
                          &EnumAdapters2, sizeof(EnumAdapters2),
                          &EnumAdapters2, sizeof(EnumAdapters2),
                          &BytesReturned);

    if (!Result)
    {
        skip("EnumAdapters2 IOCTL failed (error %lu)\n", GetLastError());
        CloseDxgDevice();
        return;
    }

    ok(EnumAdapters2.NumAdapters >= 1,
       "EnumAdapters2: Expected at least 1 adapter, got %lu\n",
       EnumAdapters2.NumAdapters);

    if (EnumAdapters2.NumAdapters >= 1)
    {
        ok(Buffer[0].hAdapter != 0,
           "EnumAdapters2: First adapter handle is 0\n");
    }

    CloseDxgDevice();
}

static void Test_OpenAdapterFromLuid(void)
{
    D3DKMT_ENUMADAPTERS EnumAdapters;
    D3DKMT_OPENADAPTERFROMLUID OpenAdapter;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    DWORD BytesReturned = 0;
    BOOL Result;

    if (!OpenDxgDevice())
        return;

    /* First enumerate to get a valid LUID */
    memset(&EnumAdapters, 0, sizeof(EnumAdapters));
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                          &EnumAdapters, sizeof(EnumAdapters),
                          &EnumAdapters, sizeof(EnumAdapters),
                          &BytesReturned);

    if (!Result || EnumAdapters.NumAdapters == 0)
    {
        skip("Cannot enumerate adapters for OpenAdapterFromLuid test\n");
        CloseDxgDevice();
        return;
    }

    /* Open using the first adapter's LUID */
    memset(&OpenAdapter, 0, sizeof(OpenAdapter));
    OpenAdapter.AdapterLuid = EnumAdapters.Adapters[0].AdapterLuid;

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                          &OpenAdapter, sizeof(OpenAdapter),
                          &OpenAdapter, sizeof(OpenAdapter),
                          &BytesReturned);

    if (!Result)
    {
        skip("OpenAdapterFromLuid IOCTL failed (error %lu)\n", GetLastError());
        CloseDxgDevice();
        return;
    }

    ok(OpenAdapter.hAdapter != 0,
       "OpenAdapterFromLuid returned zero handle\n");

    ok(OpenAdapter.AdapterLuid.LowPart == EnumAdapters.Adapters[0].AdapterLuid.LowPart &&
       OpenAdapter.AdapterLuid.HighPart == EnumAdapters.Adapters[0].AdapterLuid.HighPart,
       "OpenAdapterFromLuid returned LUID {%ld,%lu}, expected {%ld,%lu}\n",
       OpenAdapter.AdapterLuid.HighPart,
       OpenAdapter.AdapterLuid.LowPart,
       EnumAdapters.Adapters[0].AdapterLuid.HighPart,
       EnumAdapters.Adapters[0].AdapterLuid.LowPart);

    /* Close the adapter */
    memset(&CloseAdapter, 0, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = OpenAdapter.hAdapter;

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                          &CloseAdapter, sizeof(CloseAdapter),
                          NULL, 0,
                          &BytesReturned);

    ok(Result, "CloseAdapter IOCTL failed (error %lu)\n", GetLastError());

    CloseDxgDevice();
}

static void Test_OpenAdapterInvalidLuid(void)
{
    D3DKMT_OPENADAPTERFROMLUID OpenAdapter;
    DWORD BytesReturned = 0;
    BOOL Result;

    if (!OpenDxgDevice())
        return;

    /* Try to open with an invalid LUID — should fail gracefully */
    memset(&OpenAdapter, 0, sizeof(OpenAdapter));
    OpenAdapter.AdapterLuid.LowPart = 0xDEADBEEF;
    OpenAdapter.AdapterLuid.HighPart = 0x7FFFFFFF;

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                          &OpenAdapter, sizeof(OpenAdapter),
                          &OpenAdapter, sizeof(OpenAdapter),
                          &BytesReturned);

    ok(!Result, "OpenAdapterFromLuid should fail with invalid LUID\n");

    CloseDxgDevice();
}

static void Test_CloseAdapterInvalidHandle(void)
{
    D3DKMT_CLOSEADAPTER CloseAdapter;
    DWORD BytesReturned = 0;
    BOOL Result;

    if (!OpenDxgDevice())
        return;

    /* Close with an invalid handle — should fail gracefully */
    memset(&CloseAdapter, 0, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = 0xBAD0CAFE;

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                          &CloseAdapter, sizeof(CloseAdapter),
                          NULL, 0,
                          &BytesReturned);

    ok(!Result, "CloseAdapter should fail with invalid handle 0xBAD0CAFE\n");

    /* Close with zero handle */
    CloseAdapter.hAdapter = 0;
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                          &CloseAdapter, sizeof(CloseAdapter),
                          NULL, 0,
                          &BytesReturned);

    ok(!Result, "CloseAdapter should fail with zero handle\n");

    CloseDxgDevice();
}

static void Test_QueryAdapterInfo(void)
{
    D3DKMT_ENUMADAPTERS EnumAdapters;
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    D3DKMT_SEGMENTSIZEINFO SegInfo;
    DWORD BytesReturned = 0;
    BOOL Result;

    if (!OpenDxgDevice())
        return;

    /* Enumerate to get a valid adapter handle */
    memset(&EnumAdapters, 0, sizeof(EnumAdapters));
    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                          &EnumAdapters, sizeof(EnumAdapters),
                          &EnumAdapters, sizeof(EnumAdapters),
                          &BytesReturned);

    if (!Result || EnumAdapters.NumAdapters == 0)
    {
        skip("Cannot enumerate adapters for QueryAdapterInfo test\n");
        CloseDxgDevice();
        return;
    }

    /* Query segment size info (KMTQAITYPE_GETSEGMENTSIZE = 3) */
    memset(&QueryInfo, 0, sizeof(QueryInfo));
    memset(&SegInfo, 0, sizeof(SegInfo));
    QueryInfo.hAdapter = EnumAdapters.Adapters[0].hAdapter;
    QueryInfo.Type = 3; /* KMTQAITYPE_GETSEGMENTSIZE */
    QueryInfo.pPrivateDriverData = &SegInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(SegInfo);

    Result = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                          &QueryInfo, sizeof(QueryInfo),
                          &QueryInfo, sizeof(QueryInfo),
                          &BytesReturned);

    if (!Result)
    {
        skip("QueryAdapterInfo IOCTL failed (error %lu)\n", GetLastError());
        CloseDxgDevice();
        return;
    }

    /* At minimum, shared system memory should be > 0 */
    ok(SegInfo.SharedSystemMemorySize > 0 ||
       SegInfo.DedicatedVideoMemorySize > 0 ||
       SegInfo.DedicatedSystemMemorySize > 0,
       "All segment sizes are zero — at least one should be non-zero\n");

    CloseDxgDevice();
}

START_TEST(D3dkmtAdapter)
{
    Test_EnumAdapters();
    Test_EnumAdapters2();
    Test_OpenAdapterFromLuid();
    Test_OpenAdapterInvalidLuid();
    Test_CloseAdapterInvalidHandle();
    Test_QueryAdapterInfo();
}
