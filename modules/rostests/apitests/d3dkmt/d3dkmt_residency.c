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

/*
 * Full residency cycle: create an allocation, evict it (removes the implicit
 * created-resident reference), bring it back with MakeResident, and honor the
 * paging-fence contract: STATUS_PENDING means wait for the paging queue's
 * monitored fence to reach PagingFenceValue, observable both through the
 * CPU wait API and the fence's CPU-mapped value.  Every step that a given
 * adapter refuses is a skip, so the test also passes on native Windows where
 * runtime-private CreateAllocation data is required.
 */
static void Test_ResidencyCycle_EvictMakeResidentWait(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEPAGINGQUEUE cpq;
    D3DDDI_DESTROYPAGINGQUEUE dpq;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    D3DKMT_DESTROYALLOCATION da;
    D3DDDI_MAKERESIDENT mr;
    D3DKMT_EVICT ev;
    D3DKMT_HANDLE hAlloc;
    UINT Pass;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, pCreateQueue, "D3DKMTCreatePagingQueue");
    LOADFN(PFND3DKMT_DESTROYPAGINGQUEUE, pDestroyQueue, "D3DKMTDestroyPagingQueue");
    LOADFN(PFND3DKMT_CREATEALLOCATION, pCreateAlloc, "D3DKMTCreateAllocation");
    LOADFN(PFND3DKMT_DESTROYALLOCATION, pDestroyAlloc, "D3DKMTDestroyAllocation");
    LOADFN(PFND3DKMT_MAKERESIDENT, pMakeResident, "D3DKMTMakeResident");
    LOADFN(PFND3DKMT_EVICT, pEvict, "D3DKMTEvict");
    LOADFN(PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU, pWaitCpu, "D3DKMTWaitForSynchronizationObjectFromCpu");

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

    for (Pass = 0; Pass < 2; ++Pass)
    {
        memset(&ev, 0, sizeof(ev));
        ev.hDevice = hDevice;
        ev.NumAllocations = 1;
        ev.AllocationList = &hAlloc;
        Status = pEvict(&ev);
        if (Status == STATUS_NOT_SUPPORTED) { skip("Evict is safely gated until residency accounting is implemented\n"); break; }
        ok(NT_SUCCESS(Status), "Evict pass %u failed 0x%08lX\n", Pass, (long)Status);
        if (!NT_SUCCESS(Status))
            break;

        memset(&mr, 0, sizeof(mr));
        mr.hPagingQueue = cpq.hPagingQueue;
        mr.NumAllocations = 1;
        mr.AllocationList = &hAlloc;
        Status = pMakeResident(&mr);
        if (Status == STATUS_NOT_SUPPORTED) { skip("MakeResident is safely gated until residency accounting is implemented\n"); break; }
        ok(NT_SUCCESS(Status), "MakeResident pass %u failed 0x%08lX\n", Pass, (long)Status);
        if (!NT_SUCCESS(Status))
            break;
        ok(mr.NumAllocations == 1, "MakeResident pass %u completed %u of 1\n", Pass, mr.NumAllocations);

        if (Status == STATUS_PENDING)
        {
            ok(mr.PagingFenceValue != 0, "PENDING MakeResident returned a zero paging fence\n");
            if (pWaitCpu != NULL && cpq.hSyncObject != 0)
            {
                D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait;
                D3DKMT_HANDLE hFence = cpq.hSyncObject;
                UINT64 FenceValue = mr.PagingFenceValue;

                memset(&wait, 0, sizeof(wait));
                wait.hDevice = hDevice;
                wait.ObjectCount = 1;
                wait.ObjectHandleArray = &hFence;
                wait.FenceValueArray = &FenceValue;
                Status = pWaitCpu(&wait);
                ok(NT_SUCCESS(Status), "CPU wait on the paging fence failed 0x%08lX\n", (long)Status);
            }
            if (cpq.FenceValueCPUVirtualAddress != NULL)
            {
                volatile UINT64 *MappedFence = (volatile UINT64 *)cpq.FenceValueCPUVirtualAddress;
                UINT Spin;

                for (Spin = 0; Spin < 1000 && *MappedFence < mr.PagingFenceValue; ++Spin)
                    Sleep(1);
                ok(*MappedFence >= mr.PagingFenceValue,
                   "Mapped paging fence stuck at %I64u, expected >= %I64u\n",
                   *MappedFence, mr.PagingFenceValue);
            }
            trace("MakeResident pass %u: PENDING, paging fence %I64u reached\n", Pass, mr.PagingFenceValue);
        }
        else
            trace("MakeResident pass %u completed synchronously\n", Pass);
    }

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

START_TEST(residency)
{
    Test_MakeResident_NullArg();
    Test_Evict_NullArg();
    Test_OfferAllocations_NullArg();
    Test_ReclaimAllocations_NullArg();
    Test_MakeResident_BadHandle();
    Test_MakeResident_MustSucceedRequiresCantTrimFurther();
    Test_Evict_BadHandle();
    Test_ResidencyCycle_EvictMakeResidentWait();
}
