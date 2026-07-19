/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 GPU virtual addressing tests
 *              (Reserve / Map / Free / Update GpuVirtualAddress)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * GPU virtual addressing is the defining WDDM 2.0 feature: each device owns a
 * per-process GPU virtual address space, into which allocations are mapped at
 * explicit virtual addresses (rather than being patched at submit time as in
 * WDDM 1.x). Address ranges are reserved, allocations mapped/unmapped, and
 * ranges freed, all synchronized through a paging queue.
 *
 * Reference: Microsoft "D3DKMTReserveGpuVirtualAddress",
 *            "D3DKMTMapGpuVirtualAddress", "D3DKMTFreeGpuVirtualAddress",
 *            "D3DKMTUpdateGpuVirtualAddress", "GPU virtual addressing (WDDMv2)".
 *
 * Positively exercising the mapping path needs a render-capable device, a
 * paging queue and real allocations; here we validate the portable contract
 * (NULL refused, bogus handles fail).
 */

#include "precomp.h"

/* ---- NULL-argument contract ---- */
static void Test_ReserveGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_RESERVEGPUVIRTUALADDRESS, p, "D3DKMTReserveGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTReserveGpuVirtualAddress");
}

static void Test_MapGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_MAPGPUVIRTUALADDRESS, p, "D3DKMTMapGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTMapGpuVirtualAddress");
}

static void Test_FreeGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_FREEGPUVIRTUALADDRESS, p, "D3DKMTFreeGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTFreeGpuVirtualAddress");
}

static void Test_UpdateGpuVa_NullArg(void)
{
    LOADFN(PFND3DKMT_UPDATEGPUVIRTUALADDRESS, p, "D3DKMTUpdateGpuVirtualAddress");
    EXPECT_NULL_REJECTED(p, "D3DKMTUpdateGpuVirtualAddress");
}

/* ---- Reserve with a bogus adapter handle must fail ---- */
static void Test_ReserveGpuVa_BadHandle(void)
{
    D3DDDI_RESERVEGPUVIRTUALADDRESS rsv;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_RESERVEGPUVIRTUALADDRESS, p, "D3DKMTReserveGpuVirtualAddress");

    memset(&rsv, 0, sizeof(rsv));
    rsv.hAdapter = (D3DKMT_HANDLE)0xDEAD3001;   /* M2 form: reserve against an adapter */
    rsv.Size = 0x10000;                         /* 64 KiB */

    Status = p(&rsv);
    ok(!NT_SUCCESS(Status),
       "ReserveGpuVirtualAddress with a bogus adapter should fail, got 0x%08lX\n",
       (long)Status);
}

/* ---- End-to-end GpuMmu cycle: reserve, map into the reservation, map at
 * a kernel-chosen address, free both.  Skips wherever a stage is not
 * supported (physical-mode adapters, native drivers without runtime-private
 * allocations), so the test stays portable. ---- */
static void Test_GpuVa_MapCycle(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEPAGINGQUEUE cpq;
    D3DDDI_DESTROYPAGINGQUEUE dpq;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    D3DKMT_DESTROYALLOCATION da;
    D3DDDI_RESERVEGPUVIRTUALADDRESS rsv;
    D3DDDI_MAPGPUVIRTUALADDRESS map;
    D3DKMT_FREEGPUVIRTUALADDRESS fre;
    D3DKMT_HANDLE hAlloc;
    D3DGPU_VIRTUAL_ADDRESS FreshVa = 0;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, pCreateQueue, "D3DKMTCreatePagingQueue");
    LOADFN(PFND3DKMT_DESTROYPAGINGQUEUE, pDestroyQueue, "D3DKMTDestroyPagingQueue");
    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreateAlloc, "D3DKMTCreateAllocation");
    LOADFN(PFND3DKMT_DESTROYALLOCATION, pDestroyAlloc, "D3DKMTDestroyAllocation");
    LOADFN(PFND3DKMT_RESERVEGPUVIRTUALADDRESS, pReserve, "D3DKMTReserveGpuVirtualAddress");
    LOADFN(PFND3DKMT_MAPGPUVIRTUALADDRESS, pMap, "D3DKMTMapGpuVirtualAddress");
    LOADFN(PFND3DKMT_FREEGPUVIRTUALADDRESS, pFree, "D3DKMTFreeGpuVirtualAddress");

    hAdapter = OpenRenderAdapter();
    if (!hAdapter) { skip("No render-capable adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&cpq, 0, sizeof(cpq));
    cpq.hDevice = hDevice;
    cpq.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    Status = pCreateQueue(&cpq);
    if (!NT_SUCCESS(Status) || cpq.hPagingQueue == 0) { skip("CreatePagingQueue not supported (0x%08lX)\n", (long)Status); goto cleanup_device; }

    memset(&ai, 0, sizeof(ai));
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;
    Status = pCreateAlloc(&ca);
    if (!NT_SUCCESS(Status) || ai.hAllocation == 0) { skip("Runtime-private CreateAllocation refused (0x%08lX)\n", (long)Status); goto cleanup_queue; }
    hAlloc = ai.hAllocation;

    memset(&rsv, 0, sizeof(rsv));
    rsv.hPagingQueue = cpq.hPagingQueue;
    rsv.Size = 0x10000;
    Status = pReserve(&rsv);
    if (!NT_SUCCESS(Status)) { skip("ReserveGpuVirtualAddress not supported (0x%08lX)\n", (long)Status); goto cleanup_alloc; }
    ok(rsv.VirtualAddress != 0, "Reserve returned a zero GPU VA\n");

    memset(&map, 0, sizeof(map));
    map.hPagingQueue = cpq.hPagingQueue;
    map.BaseAddress = rsv.VirtualAddress;
    map.hAllocation = hAlloc;
    map.OffsetInPages = 0;
    map.SizeInPages = 1;
    map.Protection.Write = 1;
    Status = pMap(&map);
    if (Status == STATUS_NOT_SUPPORTED)
    {
        skip("MapGpuVirtualAddress not supported on this adapter\n");
        goto cleanup_reservation;
    }
    ok(NT_SUCCESS(Status), "Map into the reservation failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        ok(map.VirtualAddress == rsv.VirtualAddress,
           "Map honored base 0x%I64x but returned 0x%I64x\n",
           rsv.VirtualAddress, map.VirtualAddress);
        trace("mapped into reservation at 0x%I64x (paging fence %I64u)\n",
              map.VirtualAddress, map.PagingFenceValue);
    }

    memset(&map, 0, sizeof(map));
    map.hPagingQueue = cpq.hPagingQueue;
    map.hAllocation = hAlloc;
    map.OffsetInPages = 0;
    map.SizeInPages = 1;
    map.Protection.Write = 1;
    Status = pMap(&map);
    ok(NT_SUCCESS(Status), "Map at a kernel-chosen VA failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        FreshVa = map.VirtualAddress;
        ok(FreshVa != 0, "Fresh map returned a zero GPU VA\n");
        trace("mapped fresh at 0x%I64x\n", FreshVa);
    }

    if (FreshVa != 0)
    {
        memset(&fre, 0, sizeof(fre));
        fre.hAdapter = hAdapter;
        fre.BaseAddress = FreshVa;
        fre.Size = 0x1000;
        Status = pFree(&fre);
        ok(NT_SUCCESS(Status), "Free of the fresh mapping failed 0x%08lX\n", (long)Status);
    }

cleanup_reservation:
    memset(&fre, 0, sizeof(fre));
    fre.hAdapter = hAdapter;
    fre.BaseAddress = rsv.VirtualAddress;
    fre.Size = 0x10000;
    Status = pFree(&fre);
    ok(NT_SUCCESS(Status), "Free of the reservation failed 0x%08lX\n", (long)Status);

cleanup_alloc:
    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.phAllocationList = &hAlloc;
    da.AllocationCount = 1;
    Status = pDestroyAlloc(&da);
    ok(NT_SUCCESS(Status), "DestroyAllocation failed 0x%08lX\n", (long)Status);
cleanup_queue:
    memset(&dpq, 0, sizeof(dpq));
    dpq.hPagingQueue = cpq.hPagingQueue;
    Status = pDestroyQueue(&dpq);
    ok(NT_SUCCESS(Status), "DestroyPagingQueue failed 0x%08lX\n", (long)Status);
cleanup_device:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(gpuva)
{
    Test_ReserveGpuVa_NullArg();
    Test_MapGpuVa_NullArg();
    Test_FreeGpuVa_NullArg();
    Test_UpdateGpuVa_NullArg();
    Test_ReserveGpuVa_BadHandle();
    Test_GpuVa_MapCycle();
}
