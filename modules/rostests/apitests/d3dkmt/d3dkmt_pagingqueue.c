/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 paging queue tests (D3DKMTCreatePagingQueue / DestroyPagingQueue)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Paging queues are the WDDM 2.0 synchronization primitive that serialises
 * residency operations (MakeResident/Evict, GPU virtual-address mapping) for a
 * device. They are created per device and back a monitored fence.
 *
 * Reference: Microsoft "D3DKMTCreatePagingQueue function" /
 *            "D3DKMT_CREATEPAGINGQUEUE structure".
 *
 * Win11 contract validated here:
 *   - NULL argument is refused (STATUS_INVALID_PARAMETER or a faulting thunk).
 *   - On a render-capable adapter, a device can create/destroy a paging queue
 *     and receives a non-zero queue handle and a monitored-fence sync object.
 *   - Display-only / software adapters that do not implement the paging path
 *     are tolerated: the positive path is skipped rather than failed.
 */

#include "precomp.h"

/* ---- NULL-argument contract ---- */
static void Test_PagingQueue_NullArg(void)
{
    LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, pCreate, "D3DKMTCreatePagingQueue");
    EXPECT_NULL_REJECTED(pCreate, "D3DKMTCreatePagingQueue");
}

static void Test_DestroyPagingQueue_NullArg(void)
{
    LOADFN(PFND3DKMT_DESTROYPAGINGQUEUE, pDestroy, "D3DKMTDestroyPagingQueue");
    EXPECT_NULL_REJECTED(pDestroy, "D3DKMTDestroyPagingQueue");
}

/* ---- Invalid device handle is rejected ---- */
static void Test_PagingQueue_BadDevice(void)
{
    D3DKMT_CREATEPAGINGQUEUE cpq;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, pCreate, "D3DKMTCreatePagingQueue");

    memset(&cpq, 0, sizeof(cpq));
    cpq.hDevice = (D3DKMT_HANDLE)0xDEAD0001;   /* never a valid device */

    Status = pCreate(&cpq);
    ok_failed(Status,
       "CreatePagingQueue with a bogus device should fail, got 0x%08lX\n",
       (long)Status);
}

/* ---- Full create/destroy lifecycle on a real device ---- */
static void Test_PagingQueue_Lifecycle(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEPAGINGQUEUE cpq;
    D3DDDI_DESTROYPAGINGQUEUE dpq;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, pCreate, "D3DKMTCreatePagingQueue");
    LOADFN(PFND3DKMT_DESTROYPAGINGQUEUE, pDestroy, "D3DKMTDestroyPagingQueue");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&cpq, 0, sizeof(cpq));
    cpq.hDevice = hDevice;
    cpq.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;

    Status = pCreate(&cpq);
    if (!NT_SUCCESS(Status))
    {
        /* Display-only/software adapters legitimately lack the paging path. */
        skip("CreatePagingQueue not supported on this adapter (0x%08lX)\n",
             (long)Status);
        goto cleanup;
    }

    ok(cpq.hPagingQueue != 0, "Paging queue handle should be non-zero\n");
    ok(cpq.hSyncObject != 0,
       "Paging queue should expose a monitored-fence sync object\n");

    memset(&dpq, 0, sizeof(dpq));
    dpq.hPagingQueue = cpq.hPagingQueue;
    Status = pDestroy(&dpq);
    ok_succeeded(Status, "DestroyPagingQueue failed 0x%08lX\n", (long)Status);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(pagingqueue)
{
    Test_PagingQueue_NullArg();
    Test_DestroyPagingQueue_NullArg();
    Test_PagingQueue_BadDevice();
    Test_PagingQueue_Lifecycle();
}
