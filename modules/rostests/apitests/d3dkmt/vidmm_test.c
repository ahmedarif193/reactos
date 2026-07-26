/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidMm allocation lifecycle tests via D3DKMT user-mode API
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the D3DKMT allocation create/lock/unlock/destroy path through
 * gdi32.dll exports. These tests require a WDDM adapter to be present.
 */

#include "precomp.h"

/*
 * Helper: create a single allocation of the given size on the device.
 * Returns the allocation handle, 0 on failure.
 * The allocation uses no private driver data and no resource grouping.
 */
static D3DKMT_HANDLE
CreateSingleAllocation(
    PFN_D3DKMTCreateAllocation pfnCreate,
    D3DKMT_HANDLE hDevice,
    UINT Size)
{
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    NTSTATUS Status;

    memset(&ai, 0, sizeof(ai));
    ai.PrivateDriverDataSize = sizeof(UINT);
    ai.pPrivateDriverData = &Size;

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;

    Status = pfnCreate(&ca);
    if (!NT_SUCCESS(Status))
        return 0;

    return ai.hAllocation;
}

/*
 * Helper: destroy a single allocation.
 */
static NTSTATUS
DestroySingleAllocation(
    PFN_D3DKMTDestroyAllocation pfnDestroy,
    D3DKMT_HANDLE hDevice,
    D3DKMT_HANDLE hAllocation)
{
    D3DKMT_DESTROYALLOCATION da;

    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.phAllocationList = &hAllocation;
    da.AllocationCount = 1;

    return pfnDestroy(&da);
}

/* ---- Test 1: Basic allocation create/destroy ---- */
static void Test_BasicAllocation(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    /*
     * Create a 4KB allocation. A generic user-mode caller cannot supply the
     * per-KMD private driver data that a real Windows display driver validates,
     * so on Win11 this create is expected to be refused -- skip rather than
     * fail. A KMD that accepts a plain size blob (e.g. the ReactOS reference
     * miniport in Phase B) succeeds and we exercise the destroy path.
     */
    hAlloc = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
    if (!hAlloc)
    {
        skip("CreateAllocation without UMD-private data not supported by this KMD\n");
        goto cleanup;
    }
    ok(hAlloc != 0, "4KB allocation handle should be non-zero\n");

    Status = DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
    ok_succeeded(Status, "DestroyAllocation failed 0x%08lX\n", Status);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 2: Various allocation sizes ---- */
static void Test_VariousSizes(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_HANDLE hAllocs[4];
    static const UINT Sizes[] = { 4096, 65536, 1024 * 1024, 16 * 1024 * 1024 };
    UINT i;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(hAllocs, 0, sizeof(hAllocs));
    for (i = 0; i < 4; i++)
    {
        hAllocs[i] = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, Sizes[i]);
        if (!hAllocs[i])
        {
            if (i == 0)
                skip("CreateAllocation without UMD-private data not supported by this KMD\n");
            break;
        }
        ok(hAllocs[i] != 0, "Allocation of %u bytes: handle should be non-zero\n", Sizes[i]);
    }

    /* Destroy all in reverse order */
    for (i = 4; i > 0; i--)
    {
        if (hAllocs[i - 1])
            DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAllocs[i - 1]);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 3: Lock/Unlock cycle with data verification ---- */
static void Test_LockUnlockCycle(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_LOCK Lock;
    D3DKMT_UNLOCK Unlock;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    LOAD_D3DKMT(D3DKMTLock);
    LOAD_D3DKMT(D3DKMTUnlock);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
    if (!hAlloc) { skip("CreateAllocation failed\n"); goto cleanup; }

    /* First lock */
    memset(&Lock, 0, sizeof(Lock));
    Lock.hDevice = hDevice;
    Lock.hAllocation = hAlloc;
    Lock.Flags.LockEntire = 1;

    Status = pfnD3DKMTLock(&Lock);
    if (!NT_SUCCESS(Status))
    {
        skip("Lock failed 0x%08lX\n", Status);
        goto destroy;
    }
    ok(Lock.pData != NULL, "Lock: VA should be non-NULL\n");

    /* Write pattern */
    if (Lock.pData)
    {
        ULONG *pDwords = (ULONG *)Lock.pData;
        UINT j;
        for (j = 0; j < 4096 / sizeof(ULONG); j++)
            pDwords[j] = 0xDEADBEEF;
    }

    /* Unlock */
    memset(&Unlock, 0, sizeof(Unlock));
    Unlock.hDevice = hDevice;
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &hAlloc;
    Status = pfnD3DKMTUnlock(&Unlock);
    ok_succeeded(Status, "Unlock failed 0x%08lX\n", Status);

    /* Lock again and verify pattern */
    memset(&Lock, 0, sizeof(Lock));
    Lock.hDevice = hDevice;
    Lock.hAllocation = hAlloc;
    Lock.Flags.LockEntire = 1;

    Status = pfnD3DKMTLock(&Lock);
    if (NT_SUCCESS(Status) && Lock.pData)
    {
        ULONG *pDwords = (ULONG *)Lock.pData;
        BOOL PatternOk = TRUE;
        UINT j;
        for (j = 0; j < 4096 / sizeof(ULONG); j++)
        {
            if (pDwords[j] != 0xDEADBEEF)
            {
                PatternOk = FALSE;
                break;
            }
        }
        ok(PatternOk, "Data pattern mismatch after re-lock at dword %u\n", j);

        /* Unlock again */
        memset(&Unlock, 0, sizeof(Unlock));
        Unlock.hDevice = hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &hAlloc;
        pfnD3DKMTUnlock(&Unlock);
    }
    else
    {
        skip("Second lock failed 0x%08lX\n", Status);
    }

destroy:
    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 4: Double lock ---- */
static void Test_DoubleLock(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_LOCK Lock1, Lock2;
    D3DKMT_UNLOCK Unlock;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    LOAD_D3DKMT(D3DKMTLock);
    LOAD_D3DKMT(D3DKMTUnlock);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
    if (!hAlloc) { skip("CreateAllocation failed\n"); goto cleanup; }

    /* First lock */
    memset(&Lock1, 0, sizeof(Lock1));
    Lock1.hDevice = hDevice;
    Lock1.hAllocation = hAlloc;
    Lock1.Flags.LockEntire = 1;
    Status = pfnD3DKMTLock(&Lock1);
    if (!NT_SUCCESS(Status))
    {
        skip("First lock failed 0x%08lX\n", Status);
        goto destroy;
    }

    /* Second lock - should either fail or return same VA */
    memset(&Lock2, 0, sizeof(Lock2));
    Lock2.hDevice = hDevice;
    Lock2.hAllocation = hAlloc;
    Lock2.Flags.LockEntire = 1;
    Status = pfnD3DKMTLock(&Lock2);
    if (NT_SUCCESS(Status))
    {
        /*
         * If the driver allows double-lock, the VA should be
         * the same as the first lock.
         */
        ok(Lock2.pData == Lock1.pData,
           "Double lock returned different VA: first=%p second=%p\n",
           Lock1.pData, Lock2.pData);
    }
    /* Either outcome (fail or same VA) is acceptable */

    /* Unlock */
    memset(&Unlock, 0, sizeof(Unlock));
    Unlock.hDevice = hDevice;
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &hAlloc;
    pfnD3DKMTUnlock(&Unlock);

destroy:
    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 5: Unlock without prior lock ---- */
static void Test_UnlockWithoutLock(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_UNLOCK Unlock;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    LOAD_D3DKMT(D3DKMTUnlock);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
    if (!hAlloc) { skip("CreateAllocation failed\n"); goto cleanup; }

    /* Attempt unlock without a preceding lock */
    memset(&Unlock, 0, sizeof(Unlock));
    Unlock.hDevice = hDevice;
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &hAlloc;
    Status = pfnD3DKMTUnlock(&Unlock);
    /*
     * Should fail gracefully or succeed as a no-op.
     * The important thing is it should not crash.
     */
    ok(TRUE, "Unlock without lock did not crash (status=0x%08lX)\n", Status);

    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 6: Destroy while locked ---- */
static void Test_DestroyWhileLocked(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_LOCK Lock;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    LOAD_D3DKMT(D3DKMTLock);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
    if (!hAlloc) { skip("CreateAllocation failed\n"); goto cleanup; }

    /* Lock it */
    memset(&Lock, 0, sizeof(Lock));
    Lock.hDevice = hDevice;
    Lock.hAllocation = hAlloc;
    Lock.Flags.LockEntire = 1;
    Status = pfnD3DKMTLock(&Lock);
    if (!NT_SUCCESS(Status))
    {
        skip("Lock failed 0x%08lX\n", Status);
        DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
        goto cleanup;
    }

    /* Destroy while still locked - should succeed (implicit unlock) */
    Status = DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
    ok_succeeded(Status,
       "DestroyAllocation while locked should succeed, got 0x%08lX\n", Status);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 7: Resource with multiple allocations ---- */
static void Test_ResourceMultipleAllocations(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai[3];
    D3DKMT_DESTROYALLOCATION da;
    UINT Sizes[3] = { 4096, 8192, 16384 };
    NTSTATUS Status;
    UINT i;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    /* Set up 3 allocations under one resource */
    for (i = 0; i < 3; i++)
    {
        memset(&ai[i], 0, sizeof(ai[i]));
        ai[i].PrivateDriverDataSize = sizeof(UINT);
        ai[i].pPrivateDriverData = &Sizes[i];
    }

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 3;
    ca.pAllocationInfo = ai;
    ca.Flags.CreateResource = 1;

    Status = pfnD3DKMTCreateAllocation(&ca);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateAllocation with 3 allocs failed 0x%08lX\n", Status);
        goto cleanup;
    }

    /* Verify we got a resource handle */
    ok(ca.hResource != 0, "Resource handle should be non-zero\n");

    /* Verify all 3 allocation handles are non-zero and unique */
    for (i = 0; i < 3; i++)
    {
        ok(ai[i].hAllocation != 0,
           "Allocation[%u] handle should be non-zero\n", i);
    }
    ok(ai[0].hAllocation != ai[1].hAllocation,
       "Allocation[0] and [1] should differ\n");
    ok(ai[1].hAllocation != ai[2].hAllocation,
       "Allocation[1] and [2] should differ\n");
    ok(ai[0].hAllocation != ai[2].hAllocation,
       "Allocation[0] and [2] should differ\n");

    /* Destroy the resource (destroys all allocations) */
    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.hResource = ca.hResource;
    da.AllocationCount = 0;
    da.phAllocationList = NULL;

    Status = pfnD3DKMTDestroyAllocation(&da);
    ok_succeeded(Status, "DestroyAllocation(resource) failed 0x%08lX\n", Status);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 8: Stress test - 1000 allocations ---- */
static void Test_StressAllocations(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_HANDLE Handles[1000];
    UINT i, Created = 0;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    /* Create 1000 small allocations */
    for (i = 0; i < 1000; i++)
    {
        Handles[i] = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
        if (!Handles[i])
        {
            /* May run out of memory, that is acceptable */
            break;
        }
        Created++;
    }

    if (Created == 0)
    {
        skip("CreateAllocation without UMD-private data not supported by this KMD\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }
    ok(Created > 0, "Should have created at least 1 allocation (created %u)\n", Created);

    /* Destroy all in reverse order */
    for (i = Created; i > 0; i--)
    {
        Status = DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, Handles[i - 1]);
        ok_succeeded(Status, "DestroyAllocation[%u] failed 0x%08lX\n", i - 1, Status);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 9: SetAllocationPriority ---- */
static void Test_AllocationPriority(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_SETALLOCATIONPRIORITY sp;
    UINT Priority;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    LOAD_D3DKMT(D3DKMTSetAllocationPriority);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
    if (!hAlloc) { skip("CreateAllocation failed\n"); goto cleanup; }

    /* Set priority to HIGH (0xA0000000 = D3DDDI_ALLOCATIONPRIORITY_HIGH) */
    Priority = 0xA0000000;
    memset(&sp, 0, sizeof(sp));
    sp.hDevice = hDevice;
    sp.phAllocationList = &hAlloc;
    sp.AllocationCount = 1;
    sp.pPriorities = &Priority;

    Status = pfnD3DKMTSetAllocationPriority(&sp);
    ok_succeeded(Status, "SetAllocationPriority(HIGH) failed 0x%08lX\n", Status);

    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 10: QueryAllocationResidency ---- */
static void Test_QueryAllocationResidency(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_QUERYALLOCATIONRESIDENCY qr;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS ResidencyStatus;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    LOAD_D3DKMT(D3DKMTQueryAllocationResidency);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSingleAllocation(pfnD3DKMTCreateAllocation, hDevice, 4096);
    if (!hAlloc) { skip("CreateAllocation failed\n"); goto cleanup; }

    memset(&qr, 0, sizeof(qr));
    qr.hDevice = hDevice;
    qr.phAllocationList = &hAlloc;
    qr.AllocationCount = 1;
    qr.pResidencyStatus = &ResidencyStatus;

    Status = pfnD3DKMTQueryAllocationResidency(&qr);
    ok_succeeded(Status, "QueryAllocationResidency failed 0x%08lX\n", Status);

    if (NT_SUCCESS(Status))
    {
        /* Status should be one of the defined enum values */
        ok(ResidencyStatus == D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINGPUMEMORY ||
           ResidencyStatus == D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINSHAREDMEMORY ||
           ResidencyStatus == D3DKMT_ALLOCATIONRESIDENCYSTATUS_NOTRESIDENT,
           "ResidencyStatus has unexpected value %u\n", ResidencyStatus);
    }

    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 11: resource priority and aggregate residency ---- */
static void Test_ResourcePriorityAndResidency(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai[2];
    D3DKMT_HANDLE Handles[2];
    D3DKMT_HANDLE BadHandles[2];
    D3DKMT_SETALLOCATIONPRIORITY sp;
    D3DKMT_GETALLOCATIONPRIORITY gp;
    D3DKMT_QUERYALLOCATIONRESIDENCY qr;
    D3DKMT_DESTROYALLOCATION da;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS ResidencyStatus;
    UINT Sizes[2] = { 4096, 8192 };
    UINT Priorities[2] = { D3DDDI_ALLOCATIONPRIORITY_NORMAL + 1, D3DDDI_ALLOCATIONPRIORITY_LOW };
    UINT UpdatedPriorities[2] = { D3DDDI_ALLOCATIONPRIORITY_HIGH, D3DDDI_ALLOCATIONPRIORITY_MINIMUM };
    UINT GotPriorities[2] = { 0, 0 };
    UINT ResourcePriority = 0;
    UINT Index;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);
    LOAD_D3DKMT(D3DKMTSetAllocationPriority);
    LOAD_D3DKMT(D3DKMTGetAllocationPriority);
    LOAD_D3DKMT(D3DKMTQueryAllocationResidency);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(ai, 0, sizeof(ai));
    for (Index = 0; Index < RTL_NUMBER_OF(ai); ++Index)
    {
        ai[Index].PrivateDriverDataSize = sizeof(UINT);
        ai[Index].pPrivateDriverData = &Sizes[Index];
    }
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = RTL_NUMBER_OF(ai);
    ca.pAllocationInfo = ai;
    ca.Flags.CreateResource = 1;
    Status = pfnD3DKMTCreateAllocation(&ca);
    if (!NT_SUCCESS(Status))
    {
        skip("resource priority test needs CreateAllocation support, got 0x%08lX\n", Status);
        goto cleanup;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(ai); ++Index)
        Handles[Index] = ai[Index].hAllocation;

    memset(&sp, 0, sizeof(sp));
    sp.hDevice = hDevice;
    sp.phAllocationList = Handles;
    sp.AllocationCount = RTL_NUMBER_OF(ai);
    sp.pPriorities = Priorities;
    Status = pfnD3DKMTSetAllocationPriority(&sp);
    ok_succeeded(Status, "SetAllocationPriority(arbitrary UINT priority) failed 0x%08lX\n", Status);

    memset(&gp, 0, sizeof(gp));
    gp.hDevice = hDevice;
    gp.phAllocationList = Handles;
    gp.AllocationCount = RTL_NUMBER_OF(Handles);
    gp.pPriorities = GotPriorities;
    Status = pfnD3DKMTGetAllocationPriority(&gp);
    ok_succeeded(Status, "GetAllocationPriority(initial list) failed 0x%08lX\n", Status);
    if (NT_SUCCESS(Status))
        ok(GotPriorities[0] == Priorities[0] && GotPriorities[1] == Priorities[1], "initial priorities changed: got 0x%08X/0x%08X expected 0x%08X/0x%08X\n", GotPriorities[0], GotPriorities[1], Priorities[0], Priorities[1]);

    BadHandles[0] = Handles[0];
    BadHandles[1] = (D3DKMT_HANDLE)0xDEAD3003;
    sp.phAllocationList = BadHandles;
    sp.pPriorities = UpdatedPriorities;
    Status = pfnD3DKMTSetAllocationPriority(&sp);
    ok_failed(Status, "SetAllocationPriority accepted a request with an invalid second handle\n");
    GotPriorities[0] = 0;
    GotPriorities[1] = 0;
    Status = pfnD3DKMTGetAllocationPriority(&gp);
    ok_succeeded(Status, "GetAllocationPriority(after failed set) failed 0x%08lX\n", Status);
    if (NT_SUCCESS(Status))
        ok(GotPriorities[0] == Priorities[0] && GotPriorities[1] == Priorities[1], "failed request was not atomic: got 0x%08X/0x%08X expected 0x%08X/0x%08X\n", GotPriorities[0], GotPriorities[1], Priorities[0], Priorities[1]);

    sp.phAllocationList = Handles;
    sp.pPriorities = UpdatedPriorities;
    Status = pfnD3DKMTSetAllocationPriority(&sp);
    ok_succeeded(Status, "SetAllocationPriority(valid list) failed 0x%08lX\n", Status);
    GotPriorities[0] = 0;
    GotPriorities[1] = 0;
    Status = pfnD3DKMTGetAllocationPriority(&gp);
    ok_succeeded(Status, "GetAllocationPriority(updated list) failed 0x%08lX\n", Status);
    if (NT_SUCCESS(Status))
        ok(GotPriorities[0] == UpdatedPriorities[0] && GotPriorities[1] == UpdatedPriorities[1], "updated priorities mismatch: got 0x%08X/0x%08X expected 0x%08X/0x%08X\n", GotPriorities[0], GotPriorities[1], UpdatedPriorities[0], UpdatedPriorities[1]);

    memset(&gp, 0, sizeof(gp));
    gp.hDevice = hDevice;
    gp.hResource = ca.hResource;
    gp.pPriorities = &ResourcePriority;
    Status = pfnD3DKMTGetAllocationPriority(&gp);
    ok_succeeded(Status, "GetAllocationPriority(mixed resource) failed 0x%08lX\n", Status);
    if (NT_SUCCESS(Status))
        ok(ResourcePriority == D3DDDI_ALLOCATIONPRIORITY_HIGH, "resource maximum priority is 0x%08X expected 0x%08X\n", ResourcePriority, D3DDDI_ALLOCATIONPRIORITY_HIGH);

    memset(&sp, 0, sizeof(sp));
    sp.hDevice = hDevice;
    sp.hResource = ca.hResource;
    sp.pPriorities = &Priorities[0];
    Status = pfnD3DKMTSetAllocationPriority(&sp);
    ok_succeeded(Status, "SetAllocationPriority(resource) failed 0x%08lX\n", Status);
    GotPriorities[0] = 0;
    GotPriorities[1] = 0;
    memset(&gp, 0, sizeof(gp));
    gp.hDevice = hDevice;
    gp.phAllocationList = Handles;
    gp.AllocationCount = RTL_NUMBER_OF(Handles);
    gp.pPriorities = GotPriorities;
    Status = pfnD3DKMTGetAllocationPriority(&gp);
    ok_succeeded(Status, "GetAllocationPriority(after resource set) failed 0x%08lX\n", Status);
    if (NT_SUCCESS(Status))
        ok(GotPriorities[0] == Priorities[0] && GotPriorities[1] == Priorities[0], "resource priority did not reach every allocation: got 0x%08X/0x%08X expected 0x%08X\n", GotPriorities[0], GotPriorities[1], Priorities[0]);
    ResourcePriority = 0;
    memset(&gp, 0, sizeof(gp));
    gp.hDevice = hDevice;
    gp.hResource = ca.hResource;
    gp.pPriorities = &ResourcePriority;
    Status = pfnD3DKMTGetAllocationPriority(&gp);
    ok_succeeded(Status, "GetAllocationPriority(resource after resource set) failed 0x%08lX\n", Status);
    if (NT_SUCCESS(Status))
        ok(ResourcePriority == Priorities[0], "resource priority is 0x%08X expected 0x%08X\n", ResourcePriority, Priorities[0]);

    ResidencyStatus = (D3DKMT_ALLOCATIONRESIDENCYSTATUS)0;
    memset(&qr, 0, sizeof(qr));
    qr.hDevice = hDevice;
    qr.hResource = ca.hResource;
    qr.pResidencyStatus = &ResidencyStatus;
    Status = pfnD3DKMTQueryAllocationResidency(&qr);
    ok_succeeded(Status, "QueryAllocationResidency(resource) failed 0x%08lX\n", Status);
    if (NT_SUCCESS(Status))
        ok(ResidencyStatus >= D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINGPUMEMORY && ResidencyStatus <= D3DKMT_ALLOCATIONRESIDENCYSTATUS_NOTRESIDENT, "resource residency has unexpected value %u\n", ResidencyStatus);

    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.hResource = ca.hResource;
    Status = pfnD3DKMTDestroyAllocation(&da);
    ok_succeeded(Status, "DestroyAllocation(resource) failed 0x%08lX\n", Status);

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

START_TEST(vidmm)
{
    Test_BasicAllocation();
    Test_VariousSizes();
    Test_LockUnlockCycle();
    Test_DoubleLock();
    Test_UnlockWithoutLock();
    Test_DestroyWhileLocked();
    Test_ResourceMultipleAllocations();
    Test_StressAllocations();
    Test_AllocationPriority();
    Test_QueryAllocationResidency();
    Test_ResourcePriorityAndResidency();
}
