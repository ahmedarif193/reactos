/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidMm allocation lifecycle stress tests via D3DKMT IOCTLs
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests CreateAllocation / DestroyAllocation / Lock / Unlock /
 * SetAllocationPriority / QueryAllocationResidency through the
 * dxgkrnl IOCTL interface.
 */

#include "precomp.h"

static HANDLE hDxgKrnl = INVALID_HANDLE_VALUE;
static D3DKMT_HANDLE hAdapter = 0;
static D3DKMT_HANDLE hDevice = 0;

/* ---- Setup / teardown helpers ---- */

static BOOL SetupAdapterAndDevice(void)
{
    D3DKMT_ENUMADAPTERS ea;
    D3DKMT_CREATEDEVICE cd;
    DWORD br;

    hDxgKrnl = OpenDxgKrnl();
    if (hDxgKrnl == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open \\Device\\DxgKrnl\n");
        return FALSE;
    }

    memset(&ea, 0, sizeof(ea));
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                      &ea, sizeof(ea), &ea, sizeof(ea), &br) ||
        ea.NumAdapters == 0)
    {
        skip("No adapter found\n");
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    hAdapter = ea.Adapters[0].hAdapter;

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                      &cd, sizeof(cd), &cd, sizeof(cd), &br))
    {
        skip("CreateDevice failed (%lu)\n", GetLastError());
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    hDevice = cd.hDevice;
    return TRUE;
}

static void Teardown(void)
{
    DWORD br;
    if (hDevice)
    {
        D3DKMT_DESTROYDEVICE dd;
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = hDevice;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
        hDevice = 0;
    }
    if (hDxgKrnl != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
    }
    hAdapter = 0;
}

/* ---- Allocation helpers ---- */

/*
 * CreateTestAllocation: creates a single allocation of the given size.
 * The miniport receives a simple private-data blob containing the
 * requested size (the miniport uses this to size the allocation).
 * Returns the allocation handle via *pOutHandle, or 0 on failure.
 */
static BOOL CreateTestAllocation(SIZE_T Size, D3DKMT_HANDLE *pOutHandle)
{
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    UINT PrivateData;
    DWORD br;
    BOOL r;

    *pOutHandle = 0;

    PrivateData = (UINT)Size;

    memset(&ai, 0, sizeof(ai));
    ai.pPrivateDriverData    = &PrivateData;
    ai.PrivateDriverDataSize = sizeof(PrivateData);

    memset(&ca, 0, sizeof(ca));
    ca.hDevice          = hDevice;
    ca.NumAllocations   = 1;
    ca.pAllocationInfo  = &ai;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEALLOCATION,
                     &ca, sizeof(ca), &ca, sizeof(ca), &br);
    if (r)
        *pOutHandle = ai.hAllocation;
    return r;
}

static BOOL DestroyTestAllocation(D3DKMT_HANDLE hAlloc)
{
    D3DKMT_DESTROYALLOCATION da;
    DWORD br;

    memset(&da, 0, sizeof(da));
    da.hDevice          = hDevice;
    da.phAllocationList  = &hAlloc;
    da.AllocationCount   = 1;

    return SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                        &da, sizeof(da), NULL, 0, &br);
}

static BOOL LockAllocation(D3DKMT_HANDLE hAlloc, VOID **ppData)
{
    D3DKMT_LOCK lk;
    DWORD br;
    BOOL r;

    memset(&lk, 0, sizeof(lk));
    lk.hDevice     = hDevice;
    lk.hAllocation = hAlloc;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                     &lk, sizeof(lk), &lk, sizeof(lk), &br);
    if (r && ppData)
        *ppData = lk.pData;
    return r;
}

static BOOL UnlockAllocation(D3DKMT_HANDLE hAlloc)
{
    D3DKMT_UNLOCK ul;
    DWORD br;

    memset(&ul, 0, sizeof(ul));
    ul.hDevice         = hDevice;
    ul.NumAllocations  = 1;
    ul.phAllocations   = &hAlloc;

    return SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_UNLOCK,
                        &ul, sizeof(ul), NULL, 0, &br);
}

/* ---- Test cases ---- */

static void Test_BasicAllocFree(void)
{
    D3DKMT_HANDLE hAlloc = 0;
    BOOL r;

    if (!SetupAdapterAndDevice()) return;

    r = CreateTestAllocation(4096, &hAlloc);
    if (!r)
    {
        skip("CreateAllocation(4KB) failed (%lu)\n", GetLastError());
        Teardown();
        return;
    }
    ok(hAlloc != 0, "Allocation handle should be non-zero\n");

    r = DestroyTestAllocation(hAlloc);
    ok(r, "DestroyAllocation failed (%lu)\n", GetLastError());

    Teardown();
}

static void Test_VariousSizes(void)
{
    static const SIZE_T Sizes[] = { 4096, 65536, 1024 * 1024, 16 * 1024 * 1024 };
    static const char *Names[]  = { "4KB", "64KB", "1MB", "16MB" };
    D3DKMT_HANDLE hAllocs[4] = {0};
    ULONG i;

    if (!SetupAdapterAndDevice()) return;

    for (i = 0; i < 4; ++i)
    {
        BOOL r = CreateTestAllocation(Sizes[i], &hAllocs[i]);
        ok(r, "%s allocation failed (%lu)\n", Names[i], GetLastError());
        if (r)
            ok(hAllocs[i] != 0, "%s handle should be non-zero\n", Names[i]);
    }

    /* Clean up in reverse order */
    for (i = 4; i-- > 0; )
    {
        if (hAllocs[i])
            DestroyTestAllocation(hAllocs[i]);
    }

    Teardown();
}

static void Test_LockUnlockData(void)
{
    D3DKMT_HANDLE hAlloc = 0;
    VOID *pData = NULL;
    BOOL r;

    if (!SetupAdapterAndDevice()) return;

    r = CreateTestAllocation(4096, &hAlloc);
    if (!r)
    {
        skip("CreateAllocation for lock test failed\n");
        Teardown();
        return;
    }

    /* First lock — write pattern */
    r = LockAllocation(hAlloc, &pData);
    if (!r)
    {
        skip("Lock failed (%lu)\n", GetLastError());
        DestroyTestAllocation(hAlloc);
        Teardown();
        return;
    }
    ok(pData != NULL, "Lock should return non-NULL pData\n");

    if (pData)
    {
        UINT *pWords = (UINT *)pData;
        ULONG count = 4096 / sizeof(UINT);
        ULONG j;

        /* Write 0xDEADBEEF pattern */
        for (j = 0; j < count; ++j)
            pWords[j] = 0xDEADBEEF;
    }

    r = UnlockAllocation(hAlloc);
    ok(r, "Unlock after write failed (%lu)\n", GetLastError());

    /* Second lock — read back and verify */
    pData = NULL;
    r = LockAllocation(hAlloc, &pData);
    if (!r)
    {
        skip("Second lock failed (%lu)\n", GetLastError());
        DestroyTestAllocation(hAlloc);
        Teardown();
        return;
    }
    ok(pData != NULL, "Second lock should return non-NULL pData\n");

    if (pData)
    {
        UINT *pWords = (UINT *)pData;
        ULONG count = 4096 / sizeof(UINT);
        ULONG mismatches = 0;
        ULONG j;

        for (j = 0; j < count; ++j)
        {
            if (pWords[j] != 0xDEADBEEF)
                ++mismatches;
        }
        ok(mismatches == 0,
           "Data readback: %lu of %lu words mismatched\n", mismatches, count);
    }

    r = UnlockAllocation(hAlloc);
    ok(r, "Unlock after read failed (%lu)\n", GetLastError());

    DestroyTestAllocation(hAlloc);
    Teardown();
}

static void Test_DoubleLock(void)
{
    D3DKMT_HANDLE hAlloc = 0;
    VOID *pData1 = NULL, *pData2 = NULL;
    BOOL r;

    if (!SetupAdapterAndDevice()) return;

    r = CreateTestAllocation(4096, &hAlloc);
    if (!r) { skip("CreateAllocation failed\n"); Teardown(); return; }

    r = LockAllocation(hAlloc, &pData1);
    if (!r) { skip("First lock failed\n"); DestroyTestAllocation(hAlloc); Teardown(); return; }

    /* Second lock on the same allocation while still locked */
    r = LockAllocation(hAlloc, &pData2);
    if (r)
    {
        /* If double lock succeeds, the VA should match */
        ok(pData2 == pData1,
           "Double lock returned different VA: %p vs %p\n", pData2, pData1);
        UnlockAllocation(hAlloc);
    }
    /* If it fails, that's also acceptable behavior */

    UnlockAllocation(hAlloc);
    DestroyTestAllocation(hAlloc);
    Teardown();
}

static void Test_UnlockWithoutLock(void)
{
    D3DKMT_HANDLE hAlloc = 0;
    BOOL r;

    if (!SetupAdapterAndDevice()) return;

    r = CreateTestAllocation(4096, &hAlloc);
    if (!r) { skip("CreateAllocation failed\n"); Teardown(); return; }

    /* Unlock an allocation that was never locked — should fail gracefully */
    r = UnlockAllocation(hAlloc);
    ok(!r, "Unlock without prior lock should fail\n");

    DestroyTestAllocation(hAlloc);
    Teardown();
}

static void Test_DestroyWhileLocked(void)
{
    D3DKMT_HANDLE hAlloc = 0;
    VOID *pData = NULL;
    BOOL r;

    if (!SetupAdapterAndDevice()) return;

    r = CreateTestAllocation(4096, &hAlloc);
    if (!r) { skip("CreateAllocation failed\n"); Teardown(); return; }

    r = LockAllocation(hAlloc, &pData);
    if (!r) { skip("Lock failed\n"); DestroyTestAllocation(hAlloc); Teardown(); return; }

    /* Destroy while still locked — must not crash */
    r = DestroyTestAllocation(hAlloc);
    ok(r, "DestroyAllocation while locked should succeed (or at least not crash)\n");

    Teardown();
}

static void Test_ResourceMultipleAllocs(void)
{
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai[3];
    UINT PrivateData[3];
    D3DKMT_DESTROYALLOCATION da;
    DWORD br;
    BOOL r;
    ULONG i;

    if (!SetupAdapterAndDevice()) return;

    /* Set up 3 allocations under one resource */
    PrivateData[0] = 4096;
    PrivateData[1] = 8192;
    PrivateData[2] = 16384;

    for (i = 0; i < 3; ++i)
    {
        memset(&ai[i], 0, sizeof(ai[i]));
        ai[i].pPrivateDriverData    = &PrivateData[i];
        ai[i].PrivateDriverDataSize = sizeof(UINT);
    }

    memset(&ca, 0, sizeof(ca));
    ca.hDevice        = hDevice;
    ca.NumAllocations = 3;
    ca.pAllocationInfo = ai;
    ca.Flags.CreateResource = 1;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEALLOCATION,
                     &ca, sizeof(ca), &ca, sizeof(ca), &br);
    if (!r)
    {
        skip("CreateAllocation with resource failed (%lu)\n", GetLastError());
        Teardown();
        return;
    }

    ok(ca.hResource != 0, "Resource handle should be non-zero\n");
    for (i = 0; i < 3; ++i)
        ok(ai[i].hAllocation != 0, "Alloc[%lu] handle should be non-zero\n", i);

    /* Destroy via resource handle */
    memset(&da, 0, sizeof(da));
    da.hDevice   = hDevice;
    da.hResource = ca.hResource;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                     &da, sizeof(da), NULL, 0, &br);
    ok(r, "DestroyAllocation via resource failed (%lu)\n", GetLastError());

    Teardown();
}

static void Test_Stress1000Allocs(void)
{
    #define STRESS_COUNT 1000
    D3DKMT_HANDLE *handles;
    ULONG created = 0;
    ULONG destroyed = 0;
    ULONG i;

    if (!SetupAdapterAndDevice()) return;

    handles = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        STRESS_COUNT * sizeof(D3DKMT_HANDLE));
    if (!handles)
    {
        skip("HeapAlloc for stress handles failed\n");
        Teardown();
        return;
    }

    /* Create 1000 allocations */
    for (i = 0; i < STRESS_COUNT; ++i)
    {
        if (!CreateTestAllocation(4096, &handles[i]))
        {
            skip("Stress allocation #%lu failed (%lu)\n", i, GetLastError());
            break;
        }
        ok(handles[i] != 0, "Stress alloc #%lu handle should be non-zero\n", i);
        ++created;
    }

    ok(created == STRESS_COUNT,
       "Expected %d allocations, created %lu\n", STRESS_COUNT, created);

    /* Destroy all in reverse order */
    for (i = created; i-- > 0; )
    {
        if (handles[i])
        {
            BOOL r = DestroyTestAllocation(handles[i]);
            ok(r, "Stress destroy #%lu failed (%lu)\n", i, GetLastError());
            if (r) ++destroyed;
        }
    }

    ok(destroyed == created,
       "Expected to destroy %lu allocations, destroyed %lu\n", created, destroyed);

    HeapFree(GetProcessHeap(), 0, handles);
    Teardown();
    #undef STRESS_COUNT
}

static void Test_SetAllocationPriority(void)
{
    D3DKMT_HANDLE hAlloc = 0;
    D3DKMT_SETALLOCATIONPRIORITY sap;
    UINT priority = 0x78000000; /* D3DDDI_ALLOCATIONPRIORITY_NORMAL */
    DWORD br;
    BOOL r;

    if (!SetupAdapterAndDevice()) return;

    r = CreateTestAllocation(4096, &hAlloc);
    if (!r) { skip("CreateAllocation failed\n"); Teardown(); return; }

    memset(&sap, 0, sizeof(sap));
    sap.hDevice          = hDevice;
    sap.phAllocationList = &hAlloc;
    sap.AllocationCount  = 1;
    sap.pPriorities      = &priority;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SETALLOCATIONPRIORITY,
                     &sap, sizeof(sap), NULL, 0, &br);
    ok(r, "SetAllocationPriority failed (%lu)\n", GetLastError());

    DestroyTestAllocation(hAlloc);
    Teardown();
}

static void Test_QueryAllocationResidency(void)
{
    D3DKMT_HANDLE hAlloc = 0;
    D3DKMT_QUERYALLOCATIONRESIDENCY qar;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS status = 0;
    DWORD br;
    BOOL r;

    if (!SetupAdapterAndDevice()) return;

    r = CreateTestAllocation(4096, &hAlloc);
    if (!r) { skip("CreateAllocation failed\n"); Teardown(); return; }

    memset(&qar, 0, sizeof(qar));
    qar.hDevice          = hDevice;
    qar.phAllocationList = &hAlloc;
    qar.AllocationCount  = 1;
    qar.pResidencyStatus = &status;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY,
                     &qar, sizeof(qar), &qar, sizeof(qar), &br);
    ok(r, "QueryAllocationResidency failed (%lu)\n", GetLastError());

    if (r)
    {
        ok(status >= D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINGPUMEMORY &&
           status <= D3DKMT_ALLOCATIONRESIDENCYSTATUS_NOTRESIDENT,
           "Residency status %d is outside valid range [1..3]\n", status);
    }

    DestroyTestAllocation(hAlloc);
    Teardown();
}

START_TEST(D3dkmtVidMm)
{
    Test_BasicAllocFree();
    Test_VariousSizes();
    Test_LockUnlockData();
    Test_DoubleLock();
    Test_UnlockWithoutLock();
    Test_DestroyWhileLocked();
    Test_ResourceMultipleAllocs();
    Test_Stress1000Allocs();
    Test_SetAllocationPriority();
    Test_QueryAllocationResidency();
}
