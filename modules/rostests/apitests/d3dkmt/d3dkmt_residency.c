/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 residency management tests
 *              (MakeResident / Evict / OfferAllocations / ReclaimAllocations)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * WDDM 2.0 moved residency under explicit application control: the runtime
 * makes allocations resident (paging them into a memory segment) before the
 * GPU references them, evicts them when done, and may offer idle allocations
 * back to the OS for reuse, reclaiming them later.
 *
 * Reference: Microsoft "D3DKMTMakeResident", "D3DKMTEvict",
 *            "D3DKMTOfferAllocations", "D3DKMTReclaimAllocations".
 *
 * These calls require a render-capable device plus a paging queue and real
 * allocations to exercise positively, which a generic user-mode test cannot
 * construct on an arbitrary KMD. We therefore validate the portable contract:
 * NULL is refused, and bogus handles fail rather than succeed.
 */

#include "precomp.h"

/* ---- NULL-argument contract ---- */
static void Test_MakeResident_NullArg(void)
{
    LOADFN(PFND3DKMT_MAKERESIDENT, p, "D3DKMTMakeResident");
    EXPECT_NULL_REJECTED(p, "D3DKMTMakeResident");
}

static void Test_Evict_NullArg(void)
{
    LOADFN(PFND3DKMT_EVICT, p, "D3DKMTEvict");
    EXPECT_NULL_REJECTED(p, "D3DKMTEvict");
}

static void Test_OfferAllocations_NullArg(void)
{
    LOADFN(PFND3DKMT_OFFERALLOCATIONS, p, "D3DKMTOfferAllocations");
    EXPECT_NULL_REJECTED(p, "D3DKMTOfferAllocations");
}

static void Test_ReclaimAllocations_NullArg(void)
{
    LOADFN(PFND3DKMT_RECLAIMALLOCATIONS, p, "D3DKMTReclaimAllocations");
    EXPECT_NULL_REJECTED(p, "D3DKMTReclaimAllocations");
}

/* ---- Bogus handles must fail, not succeed ---- */
static void Test_MakeResident_BadHandle(void)
{
    D3DDDI_MAKERESIDENT mr;
    D3DKMT_HANDLE bogusAlloc = (D3DKMT_HANDLE)0xDEAD2001;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_MAKERESIDENT, p, "D3DKMTMakeResident");

    memset(&mr, 0, sizeof(mr));
    mr.hPagingQueue = (D3DKMT_HANDLE)0xDEAD2002;
    mr.NumAllocations = 1;
    mr.AllocationList = &bogusAlloc;

    Status = p(&mr);
    ok(!NT_SUCCESS(Status),
       "MakeResident with a bogus paging queue should fail, got 0x%08lX\n",
       (long)Status);
}

static void Test_MakeResident_MustSucceedRequiresCantTrimFurther(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEPAGINGQUEUE cpq;
    D3DDDI_DESTROYPAGINGQUEUE dpq;
    D3DDDI_MAKERESIDENT mr;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, pCreate, "D3DKMTCreatePagingQueue");
    LOADFN(PFND3DKMT_DESTROYPAGINGQUEUE, pDestroy, "D3DKMTDestroyPagingQueue");
    LOADFN(PFND3DKMT_MAKERESIDENT, pMakeResident, "D3DKMTMakeResident");

    hAdapter = OpenRenderAdapter();
    if (!hAdapter) { skip("No render-capable adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&cpq, 0, sizeof(cpq));
    cpq.hDevice = hDevice;
    cpq.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    Status = pCreate(&cpq);
    if (!NT_SUCCESS(Status) || cpq.hPagingQueue == 0) { skip("CreatePagingQueue not supported on this adapter (0x%08lX)\n", (long)Status); goto cleanup_device; }

    memset(&mr, 0, sizeof(mr));
    mr.hPagingQueue = cpq.hPagingQueue;
    Status = pMakeResident(&mr);
    if (!NT_SUCCESS(Status)) { skip("Empty MakeResident baseline not supported (0x%08lX)\n", (long)Status); goto cleanup_queue; }

    memset(&mr, 0, sizeof(mr));
    mr.hPagingQueue = cpq.hPagingQueue;
    mr.Flags.MustSucceed = 1;
    Status = pMakeResident(&mr);
    ok(Status == STATUS_INVALID_PARAMETER, "MakeResident MustSucceed without CantTrimFurther returned 0x%08lX, expected STATUS_INVALID_PARAMETER\n", (long)Status);

cleanup_queue:
    memset(&dpq, 0, sizeof(dpq));
    dpq.hPagingQueue = cpq.hPagingQueue;
    Status = pDestroy(&dpq);
    ok(NT_SUCCESS(Status), "DestroyPagingQueue failed 0x%08lX\n", (long)Status);
cleanup_device:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static void Test_Evict_BadHandle(void)
{
    D3DKMT_EVICT ev;
    D3DKMT_HANDLE bogusAlloc = (D3DKMT_HANDLE)0xDEAD2003;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_EVICT, p, "D3DKMTEvict");

    memset(&ev, 0, sizeof(ev));
    ev.hDevice = (D3DKMT_HANDLE)0xDEAD2004;
    ev.NumAllocations = 1;
    ev.AllocationList = &bogusAlloc;

    Status = p(&ev);
    ok(!NT_SUCCESS(Status),
       "Evict with a bogus device should fail, got 0x%08lX\n", (long)Status);
}

START_TEST(residency)
{
    Test_MakeResident_NullArg();
    Test_Evict_NullArg();
    Test_OfferAllocations_NullArg();
    Test_ReclaimAllocations_NullArg();
    Test_MakeResident_BadHandle();
    Test_MakeResident_MustSucceedRequiresCantTrimFurther();
    Test_Evict_BadHandle();
}
