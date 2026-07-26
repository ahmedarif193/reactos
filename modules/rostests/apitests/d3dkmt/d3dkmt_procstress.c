/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Process-exit and multi-process stress (roadmap gate 1.4)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * `teardown` covers what happens when a process tidies up in an unusual order.
 * This covers what happens when it does not tidy up at all -- which is the
 * common case, because an application that crashes, or is killed, or simply
 * calls ExitProcess with a device still open, never runs its cleanup path.
 *
 * The kernel's process-exit handler is then the only thing standing between a
 * live adapter and a permanent leak. Nothing in this suite reached that handler
 * before: every other subtest destroys what it creates.
 *
 * The failure being hunted is not a crash in the child. It is the adapter still
 * being usable, and no worse off, after many children have died on top of it.
 * A reference leaked per exit does not fail here -- it fails a thousand
 * launches later, on someone else's machine.
 */

#include "precomp.h"

#define PROCSTRESS_CHILDREN   6
#define PROCSTRESS_ROUNDS     3
#define PROCSTRESS_CHILD_ARG  "procstress_child"

static D3DKMT_HANDLE ProcStressOpenAdapter(void)
{
    return OpenAdapterFromDisplay1();
}

/*
 * The child half: build up real GPU state and then leave without releasing any
 * of it.  Deliberately no cleanup -- that is the entire point.
 */
static int ProcStressChild(void)
{
    PFND3DKMT_CREATEDEVICE pCreateDevice;
    PFND3DKMT_CREATECONTEXT pCreateContext;
    PFND3DKMT_CREATEALLOCATION pCreateAlloc;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_CREATEDEVICE cd;
    ULONG i;

    pCreateDevice = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pCreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    pCreateAlloc = (PFND3DKMT_CREATEALLOCATION)LoadD3DKMTProc("D3DKMTCreateAllocation");
    if (!pCreateDevice)
        return 2;

    hAdapter = ProcStressOpenAdapter();
    if (!hAdapter)
        return 3;

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    if (!NT_SUCCESS(pCreateDevice(&cd)))
        return 0;   /* nothing to leak; not a failure of the parent's check */

    /* A context holds a command-buffer mapping in this process's address
     * space, which is the reference most likely to outlive the process. */
    if (pCreateContext)
    {
        D3DKMT_CREATECONTEXT cc;

        memset(&cc, 0, sizeof(cc));
        cc.hDevice = cd.hDevice;
        cc.NodeOrdinal = 0;
        pCreateContext(&cc);
    }
    if (pCreateAlloc)
    {
        for (i = 0; i < 4; ++i)
        {
            D3DKMT_CREATEALLOCATION ca;
            D3DDDI_ALLOCATIONINFO ai;

            memset(&ca, 0, sizeof(ca));
            memset(&ai, 0, sizeof(ai));
            ca.hDevice = cd.hDevice;
            ca.NumAllocations = 1;
            ca.pAllocationInfo = &ai;
            if (!NT_SUCCESS(pCreateAlloc(&ca)))
                break;
        }
    }

    /* Leave with the adapter open, a device open, a context mapped and
     * allocations live.  ExitProcess, not return: no atexit, no unwinding. */
    ExitProcess(0);
    return 0;
}

static BOOL ProcStressSpawn(HANDLE *OutProcess)
{
    WCHAR Path[MAX_PATH];
    WCHAR Command[MAX_PATH + 64];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    *OutProcess = NULL;
    if (!GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path)))
        return FALSE;
    _snwprintf(Command, ARRAYSIZE(Command), L"\"%s\" %S", Path, PROCSTRESS_CHILD_ARG);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, Command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return FALSE;
    CloseHandle(pi.hThread);
    *OutProcess = pi.hProcess;
    return TRUE;
}

/* ------------------------------------------------------------------ *
 * Processes that exit holding live GPU objects.  The adapter must be
 * exactly as usable afterwards as before -- measured, not assumed, by
 * driving it to the same depth on both sides of the storm.
 * ------------------------------------------------------------------ */
static void Test_ProcessExitWithLiveObjects(void)
{
    PFND3DKMT_CREATEDEVICE pCreate;
    PFND3DKMT_DESTROYDEVICE pDestroy;
    D3DKMT_HANDLE hAdapter;
    ULONG Before = 0, After = 0;
    ULONG round, i;

    pCreate = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pDestroy = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    if (!pCreate || !pDestroy)
    {
        skip("D3DKMTCreateDevice/DestroyDevice not exported\n");
        return;
    }
    hAdapter = ProcStressOpenAdapter();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }

    /* Depth the adapter reaches before anything has leaked on it. */
    {
        D3DKMT_HANDLE d[64];
        for (i = 0; i < ARRAYSIZE(d); ++i)
        {
            D3DKMT_CREATEDEVICE cd;
            memset(&cd, 0, sizeof(cd));
            cd.hAdapter = hAdapter;
            if (!NT_SUCCESS(pCreate(&cd))) break;
            d[Before++] = cd.hDevice;
        }
        for (i = 0; i < Before; ++i)
        {
            D3DKMT_DESTROYDEVICE dd;
            memset(&dd, 0, sizeof(dd));
            dd.hDevice = d[i];
            pDestroy(&dd);
        }
    }
    ok(Before > 0, "adapter accepted no devices before the stress\n");
    trace("baseline device depth: %lu\n", Before);

    for (round = 0; round < PROCSTRESS_ROUNDS; ++round)
    {
        HANDLE kids[PROCSTRESS_CHILDREN];
        ULONG started = 0;

        for (i = 0; i < PROCSTRESS_CHILDREN; ++i)
            if (ProcStressSpawn(&kids[started])) started++;
        if (started == 0)
        {
            skip("could not spawn a child process\n");
            CloseAdapter(hAdapter);
            return;
        }
        for (i = 0; i < started; ++i)
        {
            WaitForSingleObject(kids[i], 30000);
            CloseHandle(kids[i]);
        }
    }
    trace("%u processes exited holding live GPU objects\n",
          PROCSTRESS_ROUNDS * PROCSTRESS_CHILDREN);

    /* Same measurement again.  A reference leaked per exit shows up here as a
     * shallower adapter, and nowhere else. */
    {
        D3DKMT_HANDLE d[64];
        for (i = 0; i < ARRAYSIZE(d); ++i)
        {
            D3DKMT_CREATEDEVICE cd;
            memset(&cd, 0, sizeof(cd));
            cd.hAdapter = hAdapter;
            if (!NT_SUCCESS(pCreate(&cd))) break;
            d[After++] = cd.hDevice;
        }
        for (i = 0; i < After; ++i)
        {
            D3DKMT_DESTROYDEVICE dd;
            memset(&dd, 0, sizeof(dd));
            dd.hDevice = d[i];
            pDestroy(&dd);
        }
    }
    trace("device depth after the stress: %lu\n", After);
    ok(After >= Before,
       "adapter shallower after %u abrupt exits (%lu -> %lu): the exit path leaks\n",
       PROCSTRESS_ROUNDS * PROCSTRESS_CHILDREN, Before, After);

    CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ *
 * Several processes on one adapter at once.  Distinct from the
 * multi-threaded case: these have separate handle namespaces and
 * separate process records, so they exercise the per-process bookkeeping
 * rather than the per-thread locking.
 * ------------------------------------------------------------------ */
static void Test_MultiProcessContention(void)
{
    HANDLE kids[PROCSTRESS_CHILDREN];
    D3DKMT_HANDLE hAdapter;
    ULONG started = 0, i;
    DWORD code;
    ULONG failed = 0;

    hAdapter = ProcStressOpenAdapter();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }

    /* All at once, and the parent keeps its own adapter open throughout. */
    for (i = 0; i < PROCSTRESS_CHILDREN; ++i)
        if (ProcStressSpawn(&kids[started])) started++;
    ok(started != 0, "could not spawn any child process\n");

    for (i = 0; i < started; ++i)
    {
        if (WaitForSingleObject(kids[i], 30000) != WAIT_OBJECT_0)
            failed++;
        else if (GetExitCodeProcess(kids[i], &code) && code > 1)
            failed++;
        CloseHandle(kids[i]);
    }
    ok_eq_ulong(failed, 0UL);
    trace("%lu concurrent processes on one adapter, %lu did not finish cleanly\n",
          started, failed);

    /* The parent's own handle must have survived all of it. */
    {
        PFND3DKMT_CREATEDEVICE pCreate =
            (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
        if (pCreate)
        {
            D3DKMT_CREATEDEVICE cd;
            NTSTATUS Status;

            memset(&cd, 0, sizeof(cd));
            cd.hAdapter = hAdapter;
            Status = pCreate(&cd);
            ok_succeeded(Status, "parent adapter unusable after concurrent processes (0x%08lX)\n",
                         (long)Status);
            if (NT_SUCCESS(Status))
            {
                PFND3DKMT_DESTROYDEVICE pDestroy =
                    (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
                if (pDestroy)
                {
                    D3DKMT_DESTROYDEVICE dd;
                    memset(&dd, 0, sizeof(dd));
                    dd.hDevice = cd.hDevice;
                    pDestroy(&dd);
                }
            }
        }
    }
    CloseAdapter(hAdapter);
}

/* ------------------------------------------------------------------ *
 * Allocate until the kernel says no, then check it still works.  What
 * matters is not the limit but that the refusal path is clean: a failed
 * allocation that half-registered would make the next one fail too.
 * ------------------------------------------------------------------ */
static void Test_AllocationPressureRecovery(void)
{
    PFND3DKMT_CREATEDEVICE pCreate;
    PFND3DKMT_DESTROYDEVICE pDestroy;
    PFND3DKMT_CREATEALLOCATION pAlloc;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_CREATEDEVICE cd;
    ULONG accepted = 0;
    NTSTATUS Status;

    pCreate = (PFND3DKMT_CREATEDEVICE)LoadD3DKMTProc("D3DKMTCreateDevice");
    pDestroy = (PFND3DKMT_DESTROYDEVICE)LoadD3DKMTProc("D3DKMTDestroyDevice");
    pAlloc = (PFND3DKMT_CREATEALLOCATION)LoadD3DKMTProc("D3DKMTCreateAllocation");
    if (!pCreate || !pAlloc)
    {
        skip("device/allocation entry points not exported\n");
        return;
    }
    hAdapter = ProcStressOpenAdapter();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    if (!NT_SUCCESS(pCreate(&cd)))
    {
        skip("CreateDevice refused\n");
        CloseAdapter(hAdapter);
        return;
    }

    while (accepted < 4096)
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
        accepted++;
    }
    trace("allocations accepted before refusal: %lu\n", accepted);

    /* Whatever the limit was, the device must still answer afterwards --
     * a refusal that corrupted state would take the device with it. */
    {
        D3DKMT_CREATEALLOCATION ca;
        D3DDDI_ALLOCATIONINFO ai;

        memset(&ca, 0, sizeof(ca));
        memset(&ai, 0, sizeof(ai));
        ca.hDevice = cd.hDevice;
        ca.NumAllocations = 0;          /* trivially valid request */
        ca.pAllocationInfo = &ai;
        Status = pAlloc(&ca);
        ok(Status != STATUS_DEVICE_REMOVED && Status != STATUS_DELETE_PENDING,
           "device lost after allocation pressure (0x%08lX)\n", (long)Status);
    }

    if (pDestroy)
    {
        D3DKMT_DESTROYDEVICE dd;
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = cd.hDevice;
        Status = pDestroy(&dd);
        ok_succeeded(Status, "device undestroyable after allocation pressure (0x%08lX)\n",
                     (long)Status);
    }
    CloseAdapter(hAdapter);
}

START_TEST(procstress)
{
    int argc;
    char **argv;

    /* Re-invoked as its own child?  Then be the child. */
    argc = winetest_get_mainargs(&argv);
    if (argc > 2 && strcmp(argv[2], PROCSTRESS_CHILD_ARG) == 0)
    {
        ProcStressChild();
        return;
    }

    Test_ProcessExitWithLiveObjects();
    Test_MultiProcessContention();
    Test_AllocationPressureRecovery();
}

/* EOF */
