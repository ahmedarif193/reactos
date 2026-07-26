/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Teardown, exhaustion and contention (roadmap gate 1.4)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Every other subtest exercises objects that are created and destroyed in the
 * order the API documents.  Nothing exercises the orders a real application
 * produces: a device destroyed with allocations still on it, a handle used
 * after the object behind it is gone, a handle table run to its limit, several
 * threads racing on one adapter.  Those are where a reference count goes wrong,
 * and a reference count that goes wrong does not fail here -- it fails later,
 * somewhere else, as a bugcheck.
 *
 * Win11-green rules: nothing asserts that an operation must succeed on a
 * display-only adapter.  What is asserted is that a refusal is a refusal and
 * not a crash, that a stale handle is never honoured, and that the system is
 * still usable afterwards.
 */

#include "precomp.h"

#define TEARDOWN_THREADS 4
#define TEARDOWN_ROUNDS  64
#define TEARDOWN_MAX_DEVICES 512

static PFN_D3DKMTCloseAdapter pfnClose;

/* ------------------------------------------------------------------ *
 * A destroyed object's handle must never be honoured again.  The
 * dangerous outcome is not the second destroy failing, it is the second
 * destroy *succeeding* against a recycled object somebody else owns.
 * ------------------------------------------------------------------ */
static void Test_StaleHandlesAreRefused(void)
{
    PFND3DKMT_CREATEDEVICE pCreate;
    PFND3DKMT_DESTROYDEVICE pDestroy;
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_DESTROYDEVICE dd;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;

    pCreate = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pDestroy = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    if (!pCreate || !pDestroy)
    {
        skip("D3DKMTCreateDevice/DestroyDevice not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    Status = pCreate(&cd);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateDevice refused (0x%08lX)\n", (long)Status);
        CloseAdapter(hAdapter);
        return;
    }
    hDevice = cd.hDevice;

    memset(&dd, 0, sizeof(dd));
    dd.hDevice = hDevice;
    Status = pDestroy(&dd);
    ok_succeeded(Status, "DestroyDevice failed 0x%08lX\n", (long)Status);

    /* The same handle again: the object is gone, so this must be refused
     * however many times it is offered. */
    {
        int i;
        for (i = 0; i < 8; ++i)
        {
            memset(&dd, 0, sizeof(dd));
            dd.hDevice = hDevice;
            Status = pDestroy(&dd);
            ok_failed(Status, "DestroyDevice honoured a stale handle on pass %d (0x%08lX)\n",
                      i, (long)Status);
        }
    }

    /* The adapter is still usable: a refused stale destroy must not have
     * damaged anything around it. */
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    Status = pCreate(&cd);
    ok_succeeded(Status, "adapter unusable after stale-handle rejection 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = cd.hDevice;
        pDestroy(&dd);
    }
    CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ *
 * Destroying a device while its allocations are still alive.  An
 * application that exits without tidying up produces exactly this, and
 * the allocations must go with the device rather than outliving it.
 * ------------------------------------------------------------------ */
static void Test_DestroyDeviceWithLiveAllocations(void)
{
    PFND3DKMT_CREATEDEVICE pCreate;
    PFND3DKMT_DESTROYDEVICE pDestroy;
    PFND3DKMT_CREATEALLOCATION pAlloc;
    PFND3DKMT_DESTROYALLOCATION pFree;
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_DESTROYDEVICE dd;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE Allocations[8];
    ULONG Created = 0;
    ULONG i;
    NTSTATUS Status;

    pCreate = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pDestroy = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    pAlloc = (PFND3DKMT_CREATEALLOCATION)LoadD3DKMTProc("D3DKMTCreateAllocation");
    pFree = (PFND3DKMT_DESTROYALLOCATION)LoadD3DKMTProc("D3DKMTDestroyAllocation");
    if (!pCreate || !pDestroy || !pAlloc)
    {
        skip("device/allocation entry points not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    Status = pCreate(&cd);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateDevice refused (0x%08lX)\n", (long)Status);
        CloseAdapter(hAdapter);
        return;
    }

    memset(Allocations, 0, sizeof(Allocations));
    for (i = 0; i < ARRAYSIZE(Allocations); ++i)
    {
        D3DKMT_CREATEALLOCATION ca;
        D3DDDI_ALLOCATIONINFO ai;

        memset(&ca, 0, sizeof(ca));
        memset(&ai, 0, sizeof(ai));
        ca.hDevice = cd.hDevice;
        ca.NumAllocations = 1;
        ca.pAllocationInfo = &ai;
        if (!NT_SUCCESS(pAlloc(&ca)))
            break;
        Allocations[Created++] = ai.hAllocation;
    }
    trace("created %lu allocations before destroying their device\n", Created);

    memset(&dd, 0, sizeof(dd));
    dd.hDevice = cd.hDevice;
    Status = pDestroy(&dd);
    ok_succeeded(Status, "DestroyDevice with live allocations failed 0x%08lX\n", (long)Status);

    /* The allocations went with the device.  Freeing them now names a device
     * that no longer exists, which must be refused rather than followed. */
    if (pFree != NULL)
    {
        for (i = 0; i < Created; ++i)
        {
            D3DKMT_DESTROYALLOCATION da;

            memset(&da, 0, sizeof(da));
            da.hDevice = cd.hDevice;
            da.AllocationCount = 1;
            da.phAllocationList = &Allocations[i];
            Status = pFree(&da);
            ok_failed(Status, "DestroyAllocation honoured an allocation whose device is gone (0x%08lX)\n",
                      (long)Status);
        }
    }
    CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ *
 * Run the handle table to its limit.  What matters is not the limit but
 * what happens at it: a clean refusal, and full recovery once the
 * handles are given back.  A table that leaks under refusal would make
 * the second pass smaller than the first.
 * ------------------------------------------------------------------ */
static void Test_HandleTableExhaustion(void)
{
    PFND3DKMT_CREATEDEVICE pCreate;
    PFND3DKMT_DESTROYDEVICE pDestroy;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE *Devices;
    ULONG FirstPass = 0, SecondPass = 0;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;

    pCreate = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pDestroy = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    if (!pCreate || !pDestroy)
    {
        skip("D3DKMTCreateDevice/DestroyDevice not exported\n");
        return;
    }
    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    Devices = (D3DKMT_HANDLE *)calloc(TEARDOWN_MAX_DEVICES, sizeof(*Devices));
    if (Devices == NULL)
    {
        skip("out of memory\n");
        CloseAdapter(hAdapter);
        return;
    }

    for (i = 0; i < TEARDOWN_MAX_DEVICES; ++i)
    {
        D3DKMT_CREATEDEVICE cd;

        memset(&cd, 0, sizeof(cd));
        cd.hAdapter = hAdapter;
        Status = pCreate(&cd);
        if (!NT_SUCCESS(Status))
            break;
        Devices[FirstPass++] = cd.hDevice;
    }
    trace("handle table accepted %lu devices, then 0x%08lX\n", FirstPass, (long)Status);
    ok(FirstPass > 0, "could not create a single device\n");

    for (i = 0; i < FirstPass; ++i)
    {
        D3DKMT_DESTROYDEVICE dd;

        memset(&dd, 0, sizeof(dd));
        dd.hDevice = Devices[i];
        Status = pDestroy(&dd);
        ok_succeeded(Status, "DestroyDevice %lu of %lu failed 0x%08lX\n", i, FirstPass, (long)Status);
    }

    /* Everything was handed back, so the second pass must reach the same
     * depth.  A smaller number means the refusal path leaked. */
    for (i = 0; i < FirstPass; ++i)
    {
        D3DKMT_CREATEDEVICE cd;

        memset(&cd, 0, sizeof(cd));
        cd.hAdapter = hAdapter;
        if (!NT_SUCCESS(pCreate(&cd)))
            break;
        Devices[SecondPass++] = cd.hDevice;
    }
    ok(SecondPass >= FirstPass, "second pass reached %lu of %lu -- the table leaked\n",
       SecondPass, FirstPass);
    for (i = 0; i < SecondPass; ++i)
    {
        D3DKMT_DESTROYDEVICE dd;

        memset(&dd, 0, sizeof(dd));
        dd.hDevice = Devices[i];
        pDestroy(&dd);
    }

    free(Devices);
    CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ *
 * Several threads creating and destroying on one adapter.  The handle
 * table and the adapter's device list are shared, and their locking is
 * only exercised when more than one thread is inside them at once.
 * ------------------------------------------------------------------ */
typedef struct _TEARDOWN_WORKER
{
    D3DKMT_HANDLE hAdapter;
    PFND3DKMT_CREATEDEVICE pCreate;
    PFND3DKMT_DESTROYDEVICE pDestroy;
    HANDLE StartGate;
    volatile LONG Created;
    volatile LONG Destroyed;
    volatile LONG Mismatched;
} TEARDOWN_WORKER;

static DWORD WINAPI TeardownWorker(LPVOID Parameter)
{
    TEARDOWN_WORKER *Work = (TEARDOWN_WORKER *)Parameter;
    ULONG Round;

    WaitForSingleObject(Work->StartGate, INFINITE);
    for (Round = 0; Round < TEARDOWN_ROUNDS; ++Round)
    {
        D3DKMT_CREATEDEVICE cd;
        D3DKMT_DESTROYDEVICE dd;

        memset(&cd, 0, sizeof(cd));
        cd.hAdapter = Work->hAdapter;
        if (!NT_SUCCESS(Work->pCreate(&cd)))
            continue;
        InterlockedIncrement(&Work->Created);
        if (cd.hDevice == 0)
        {
            InterlockedIncrement(&Work->Mismatched);
            continue;
        }
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = cd.hDevice;
        if (NT_SUCCESS(Work->pDestroy(&dd)))
            InterlockedIncrement(&Work->Destroyed);
        else
            InterlockedIncrement(&Work->Mismatched);
    }
    return 0;
}

static void Test_MultiThreadContention(void)
{
    TEARDOWN_WORKER Work;
    HANDLE Threads[TEARDOWN_THREADS];
    ULONG Started = 0;
    ULONG i;

    memset(&Work, 0, sizeof(Work));
    Work.pCreate = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    Work.pDestroy = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    if (!Work.pCreate || !Work.pDestroy)
    {
        skip("D3DKMTCreateDevice/DestroyDevice not exported\n");
        return;
    }
    Work.hAdapter = OpenAdapterFromDisplay1();
    if (!Work.hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    Work.StartGate = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (Work.StartGate == NULL)
    {
        skip("CreateEvent failed %lu\n", GetLastError());
        CloseAdapter(Work.hAdapter);
        return;
    }

    for (i = 0; i < TEARDOWN_THREADS; ++i)
    {
        Threads[i] = CreateThread(NULL, 0, TeardownWorker, &Work, 0, NULL);
        if (Threads[i] == NULL)
            break;
        Started++;
    }
    ok(Started != 0, "no worker threads started\n");
    SetEvent(Work.StartGate);
    for (i = 0; i < Started; ++i)
    {
        WaitForSingleObject(Threads[i], 60000);
        CloseHandle(Threads[i]);
    }
    CloseHandle(Work.StartGate);

    trace("contention: %ld created, %ld destroyed across %lu threads\n",
          Work.Created, Work.Destroyed, Started);
    /* Every device that was created was destroyed by the thread that made it.
     * A zero handle or a refused destroy means two threads collided inside the
     * handle table. */
    ok_eq_long(Work.Mismatched, 0L);
    ok_eq_long(Work.Destroyed, Work.Created);

    CloseAdapter(Work.hAdapter);
}

/* ------------------------------------------------------------------ *
 * Closing an adapter whose devices are still open.
 *
 * The obvious expectation -- that the devices go with it -- is wrong, and
 * measuring both kernels is what showed it: on Windows 11 the device
 * handles stay valid and stay destroyable after D3DKMTCloseAdapter, and
 * ReactOS agrees exactly.  CloseAdapter closes the *handle*; the devices
 * hold their own reference to the adapter object underneath it.  So what
 * is asserted here is that both survive, because an application that
 * destroys its devices after closing the adapter works on Windows and
 * must keep working here.
 * ------------------------------------------------------------------ */
static void Test_CloseAdapterWithLiveDevices(void)
{
    PFND3DKMT_CREATEDEVICE pCreate;
    PFND3DKMT_DESTROYDEVICE pDestroy;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE Devices[4];
    ULONG Created = 0;
    ULONG i;
    NTSTATUS Status;

    pCreate = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pDestroy = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    if (!pCreate || !pDestroy)
    {
        skip("D3DKMTCreateDevice/DestroyDevice not exported\n");
        return;
    }
    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    memset(Devices, 0, sizeof(Devices));
    for (i = 0; i < ARRAYSIZE(Devices); ++i)
    {
        D3DKMT_CREATEDEVICE cd;

        memset(&cd, 0, sizeof(cd));
        cd.hAdapter = hAdapter;
        if (!NT_SUCCESS(pCreate(&cd)))
            break;
        Devices[Created++] = cd.hDevice;
    }
    if (Created == 0)
    {
        skip("CreateDevice refused on this adapter\n");
        CloseAdapter(hAdapter);
        return;
    }

    CloseAdapter(hAdapter);

    for (i = 0; i < Created; ++i)
    {
        D3DKMT_DESTROYDEVICE dd;

        memset(&dd, 0, sizeof(dd));
        dd.hDevice = Devices[i];
        Status = pDestroy(&dd);
        ok_succeeded(Status, "device %lu became undestroyable once its adapter handle closed (0x%08lX)\n",
                     i, (long)Status);
    }

    /* And the whole path still works afterwards. */
    hAdapter = OpenAdapterFromDisplay1();
    ok(hAdapter != 0, "adapter could not be reopened after teardown\n");
    if (hAdapter)
        CloseAdapter(hAdapter);
}

START_TEST(teardown)
{
    pfnClose = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");

    Test_StaleHandlesAreRefused();
    Test_DestroyDeviceWithLiveAllocations();
    Test_HandleTableExhaustion();
    Test_MultiThreadContention();
    Test_CloseAdapterWithLiveDevices();
}

/* EOF */
