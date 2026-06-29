/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU Virtual Address IOCTL tests (WDDM 2.0)
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Tests the WDDM 2.0 GPU virtual addressing IOCTLs:
 *   - IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS
 *   - IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS
 *   - IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS
 *   - IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS
 *   - IOCTL_D3DKMT_MAKERESIDENT
 *   - IOCTL_D3DKMT_EVICT
 *
 * These tests verify that the kernel driver accepts the IOCTLs and returns
 * expected status codes.  They do not test full GPU VA space functionality
 * (which requires a running miniport) but verify the dispatch path.
 */

#include "precomp.h"

/*
 * Test: MapGpuVirtualAddress IOCTL acceptance
 *
 * Verify that the kernel accepts a MapGpuVirtualAddress IOCTL with a
 * well-formed buffer and returns a VA in the output field.
 */
static void
Test_MapGpuVirtualAddress_Basic(void)
{
    HANDLE hDevice;
    TEST_MAPGPUVA MapVa;
    DWORD BytesReturned;
    BOOL Result;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    /* Test 1: Map with explicit base address */
    memset(&MapVa, 0, sizeof(MapVa));
    MapVa.BaseAddress = 0x100000ULL;    /* 1 MB */
    MapVa.SizeInPages = 1;

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS,
                          &MapVa, sizeof(MapVa),
                          &MapVa, sizeof(MapVa),
                          &BytesReturned);
    ok(Result, "MapGpuVirtualAddress with base address should succeed\n");
    ok(MapVa.VirtualAddress == 0x100000ULL,
       "VirtualAddress should match BaseAddress, got 0x%I64x\n",
       MapVa.VirtualAddress);

    /* Test 2: Map with minimum address (auto-assign) */
    memset(&MapVa, 0, sizeof(MapVa));
    MapVa.MinimumAddress = 0x200000ULL; /* 2 MB */
    MapVa.SizeInPages = 16;

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS,
                          &MapVa, sizeof(MapVa),
                          &MapVa, sizeof(MapVa),
                          &BytesReturned);
    ok(Result, "MapGpuVirtualAddress with MinimumAddress should succeed\n");
    ok(MapVa.VirtualAddress >= 0x200000ULL,
       "VirtualAddress should be >= MinimumAddress, got 0x%I64x\n",
       MapVa.VirtualAddress);

    /* Test 3: Map with no address hints (default) */
    memset(&MapVa, 0, sizeof(MapVa));
    MapVa.SizeInPages = 1;

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS,
                          &MapVa, sizeof(MapVa),
                          &MapVa, sizeof(MapVa),
                          &BytesReturned);
    ok(Result, "MapGpuVirtualAddress with default address should succeed\n");
    ok(MapVa.VirtualAddress != 0,
       "VirtualAddress should be non-zero, got 0x%I64x\n",
       MapVa.VirtualAddress);

    /* Test 4: Buffer too small should fail */
    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS,
                          &MapVa, 4,  /* too small */
                          &MapVa, sizeof(MapVa),
                          &BytesReturned);
    ok(!Result, "MapGpuVirtualAddress with small buffer should fail\n");

    CloseHandle(hDevice);
}

/*
 * Test: ReserveGpuVirtualAddress IOCTL acceptance
 */
static void
Test_ReserveGpuVirtualAddress_Basic(void)
{
    HANDLE hDevice;
    TEST_RESERVEGPUVA Reserve;
    DWORD BytesReturned;
    BOOL Result;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    /* Test 1: Reserve with explicit base */
    memset(&Reserve, 0, sizeof(Reserve));
    Reserve.BaseAddress = 0x400000ULL;  /* 4 MB */
    Reserve.Size = 0x10000ULL;          /* 64 KB */

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS,
                          &Reserve, sizeof(Reserve),
                          &Reserve, sizeof(Reserve),
                          &BytesReturned);
    ok(Result, "ReserveGpuVirtualAddress should succeed\n");
    ok(Reserve.VirtualAddress == 0x400000ULL,
       "VirtualAddress should match BaseAddress, got 0x%I64x\n",
       Reserve.VirtualAddress);

    /* Test 2: Reserve with auto-assignment */
    memset(&Reserve, 0, sizeof(Reserve));
    Reserve.Size = 0x100000ULL; /* 1 MB */

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS,
                          &Reserve, sizeof(Reserve),
                          &Reserve, sizeof(Reserve),
                          &BytesReturned);
    ok(Result, "ReserveGpuVirtualAddress auto-assign should succeed\n");
    ok(Reserve.VirtualAddress != 0,
       "VirtualAddress should be non-zero\n");

    CloseHandle(hDevice);
}

/*
 * Test: FreeGpuVirtualAddress IOCTL acceptance
 */
static void
Test_FreeGpuVirtualAddress_Basic(void)
{
    HANDLE hDevice;
    TEST_RESERVEGPUVA Reserve;
    TEST_FREEGPUVA FreeVa;
    DWORD BytesReturned;
    BOOL Result;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    memset(&Reserve, 0, sizeof(Reserve));
    Reserve.BaseAddress = 0x400000ULL;
    Reserve.Size = 0x10000ULL;

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS,
                          &Reserve, sizeof(Reserve),
                          &Reserve, sizeof(Reserve),
                          &BytesReturned);
    ok(Result, "Reserve before FreeGpuVirtualAddress should succeed\n");
    if (!Result)
    {
        CloseHandle(hDevice);
        return;
    }

    memset(&FreeVa, 0, sizeof(FreeVa));
    FreeVa.BaseAddress = Reserve.VirtualAddress;
    FreeVa.Size = Reserve.Size;

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS,
                          &FreeVa, sizeof(FreeVa),
                          NULL, 0,
                          &BytesReturned);
    ok(Result, "FreeGpuVirtualAddress should succeed\n");

    CloseHandle(hDevice);
}

/*
 * Test: MakeResident / Evict IOCTL acceptance
 */
static void
Test_MakeResident_Evict_Basic(void)
{
    HANDLE hDevice;
    TEST_MAKERESIDENT MakeRes;
    TEST_EVICT Evict;
    DWORD BytesReturned;
    BOOL Result;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    /* Test MakeResident */
    memset(&MakeRes, 0, sizeof(MakeRes));
    MakeRes.NumAllocations = 0;

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_MAKERESIDENT,
                          &MakeRes, sizeof(MakeRes),
                          &MakeRes, sizeof(MakeRes),
                          &BytesReturned);
    ok(Result, "MakeResident with 0 allocations should succeed\n");
    ok(MakeRes.PagingFenceValue == 0,
       "PagingFenceValue should be 0, got %I64u\n", MakeRes.PagingFenceValue);

    /* Test Evict */
    memset(&Evict, 0, sizeof(Evict));
    Evict.NumAllocations = 0;

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_EVICT,
                          &Evict, sizeof(Evict),
                          &Evict, sizeof(Evict),
                          &BytesReturned);
    ok(Result, "Evict with 0 allocations should succeed\n");
    ok(Evict.NumBytesToTrim == 0,
       "NumBytesToTrim should be 0, got %I64u\n", Evict.NumBytesToTrim);

    /* Test buffer too small */
    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_MAKERESIDENT,
                          &MakeRes, 4,
                          &MakeRes, sizeof(MakeRes),
                          &BytesReturned);
    ok(!Result, "MakeResident with small buffer should fail\n");

    BytesReturned = 0;
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_EVICT,
                          &Evict, 4,
                          &Evict, sizeof(Evict),
                          &BytesReturned);
    ok(!Result, "Evict with small buffer should fail\n");

    CloseHandle(hDevice);
}

/*
 * Test: GPU VA address range isolation
 *
 * Verifies that successive reserve/map operations return non-overlapping
 * VA ranges from the kernel.
 */
static void
Test_GpuVa_RangeIsolation(void)
{
    HANDLE hDevice;
    TEST_RESERVEGPUVA Reserve1, Reserve2;
    DWORD BytesReturned;
    BOOL Result;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    /* Reserve two distinct regions */
    memset(&Reserve1, 0, sizeof(Reserve1));
    Reserve1.BaseAddress = 0x100000ULL;
    Reserve1.Size = 0x10000ULL;

    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS,
                          &Reserve1, sizeof(Reserve1),
                          &Reserve1, sizeof(Reserve1),
                          &BytesReturned);
    ok(Result, "First reserve should succeed\n");

    memset(&Reserve2, 0, sizeof(Reserve2));
    Reserve2.BaseAddress = 0x200000ULL;
    Reserve2.Size = 0x10000ULL;

    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS,
                          &Reserve2, sizeof(Reserve2),
                          &Reserve2, sizeof(Reserve2),
                          &BytesReturned);
    ok(Result, "Second reserve should succeed\n");

    /* Verify they don't overlap */
    ok(Reserve1.VirtualAddress != Reserve2.VirtualAddress,
       "Two reserves should produce different VAs: 0x%I64x vs 0x%I64x\n",
       Reserve1.VirtualAddress, Reserve2.VirtualAddress);

    if (Reserve1.VirtualAddress < Reserve2.VirtualAddress)
    {
        ok(Reserve1.VirtualAddress + 0x10000ULL <= Reserve2.VirtualAddress,
           "Reserve1 end 0x%I64x should not overlap Reserve2 start 0x%I64x\n",
           Reserve1.VirtualAddress + 0x10000ULL, Reserve2.VirtualAddress);
    }

    CloseHandle(hDevice);
}

START_TEST(GpuVirtualAddress)
{
    Test_MapGpuVirtualAddress_Basic();
    Test_ReserveGpuVirtualAddress_Basic();
    Test_FreeGpuVirtualAddress_Basic();
    Test_MakeResident_Evict_Basic();
    Test_GpuVa_RangeIsolation();
}
