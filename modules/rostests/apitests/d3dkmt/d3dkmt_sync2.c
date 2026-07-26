/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM 2.0 synchronization surface tests
 *              (CreateSynchronizationObject2 monitored fences, From-CPU/GPU ops)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * WDDM 2.0 introduced the monitored fence: a 64-bit GPU/CPU-visible counter
 * (D3DDDI_MONITORED_FENCE) created via D3DKMTCreateSynchronizationObject2, which
 * can be waited on and signalled directly from the CPU (WaitFor/Signal...FromCpu)
 * and from the GPU (Signal...FromGpu). They are the backbone of WDDM 2.x
 * scheduling and paging-queue completion.
 *
 * Reference: Microsoft "D3DKMTCreateSynchronizationObject2",
 *            "D3DKMTWaitForSynchronizationObjectFromCpu",
 *            "D3DKMTSignalSynchronizationObjectFromCpu/FromGpu", "Monitored fences".
 *
 * Creating a monitored fence needs a render device, so the positive path is
 * exercised on a render-capable adapter and skipped otherwise; the portable
 * contract (NULL refused) is always validated.
 */

#include "precomp.h"

/* ---- NULL-argument contract across the WDDM2 sync surface ---- */
static void Test_CreateSyncObject2_NullArg(void)
{
    LOADFN(PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2, p, "D3DKMTCreateSynchronizationObject2");
    EXPECT_NULL_REJECTED(p, "D3DKMTCreateSynchronizationObject2");
}

static void Test_WaitForSyncObject2_NullArg(void)
{
    LOADFN(PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECT2, p, "D3DKMTWaitForSynchronizationObject2");
    EXPECT_NULL_REJECTED(p, "D3DKMTWaitForSynchronizationObject2");
}

static void Test_SignalSyncObject2_NullArg(void)
{
    LOADFN(PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECT2, p, "D3DKMTSignalSynchronizationObject2");
    EXPECT_NULL_REJECTED(p, "D3DKMTSignalSynchronizationObject2");
}

static void Test_WaitForSyncObjectFromCpu_NullArg(void)
{
    LOADFN(PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU, p,
           "D3DKMTWaitForSynchronizationObjectFromCpu");
    EXPECT_NULL_REJECTED(p, "D3DKMTWaitForSynchronizationObjectFromCpu");
}

static void Test_SignalSyncObjectFromCpu_NullArg(void)
{
    LOADFN(PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU, p,
           "D3DKMTSignalSynchronizationObjectFromCpu");
    EXPECT_NULL_REJECTED(p, "D3DKMTSignalSynchronizationObjectFromCpu");
}

static void Test_SignalSyncObjectFromGpu_NullArg(void)
{
    LOADFN(PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU, p,
           "D3DKMTSignalSynchronizationObjectFromGpu");
    EXPECT_NULL_REJECTED(p, "D3DKMTSignalSynchronizationObjectFromGpu");
}

/* ---- Monitored fence create on a real device (skip on display-only) ---- */
static void Test_MonitoredFence_Lifecycle(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cso;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2, pCreate,
           "D3DKMTCreateSynchronizationObject2");
    LOAD_D3DKMT(D3DKMTDestroySynchronizationObject);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_MONITORED_FENCE;
    cso.Info.MonitoredFence.InitialFenceValue = 0;

    Status = pCreate(&cso);
    if (!NT_SUCCESS(Status))
    {
        skip("Monitored fence create not supported on this device (0x%08lX)\n",
             (long)Status);
        goto cleanup;
    }

    ok(cso.hSyncObject != 0, "Monitored fence handle should be non-zero\n");

    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
        memset(&dso, 0, sizeof(dso));
        dso.hSyncObject = cso.hSyncObject;
        Status = pfnD3DKMTDestroySynchronizationObject(&dso);
        ok(NT_SUCCESS(Status),
           "DestroySynchronizationObject(monitored fence) failed 0x%08lX\n", (long)Status);
    }

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/*
 * The documented WDDM2 monitored-fence contract: creation returns a
 * read-only CPU mapping of the 64-bit fence value seeded with
 * InitialFenceValue, and CPU-side signals update it.  These assertions
 * hold identically on Win11.
 */
static void Test_MonitoredFence_CpuValuePage(void)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cso;
    D3DKMT_HANDLE hAdapter, hDevice;
    NTSTATUS Status;
    volatile const UINT64 *FenceVa;
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreate;
    PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU pSignal;
    PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU pWait;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy;

    pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)
              LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pSignal = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU)
              LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromCpu");
    pWait   = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU)
              LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromCpu");
    pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)
               LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    if (!pCreate || !pSignal || !pWait || !pDestroy)
    {
        skip("monitored-fence entry points not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_MONITORED_FENCE;
    cso.Info.MonitoredFence.InitialFenceValue = 7;

    Status = pCreate(&cso);
    if (!NT_SUCCESS(Status))
    {
        skip("Monitored fence create not supported (0x%08lX)\n", (long)Status);
        goto cleanup;
    }

    FenceVa = (volatile const UINT64 *)
              cso.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    ok(FenceVa != NULL, "FenceValueCPUVirtualAddress should be non-NULL\n");

    if (FenceVa != NULL)
    {
        ok(*FenceVa == 7,
           "mapped fence value should equal InitialFenceValue (7), got %I64u\n",
           *FenceVa);

        {
            D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU sig;
            D3DKMT_HANDLE Handles[1];
            UINT64 Values[1];

            memset(&sig, 0, sizeof(sig));
            Handles[0] = cso.hSyncObject;
            Values[0] = 42;
            sig.hDevice = hDevice;
            sig.ObjectCount = 1;
            sig.ObjectHandleArray = Handles;
            sig.FenceValueArray = Values;

            Status = pSignal(&sig);
            ok(NT_SUCCESS(Status),
               "SignalSynchronizationObjectFromCpu failed 0x%08lX\n",
               (long)Status);

            if (NT_SUCCESS(Status))
            {
                ok(*FenceVa == 42,
                   "mapped fence value should be 42 after CPU signal, "
                   "got %I64u\n", *FenceVa);
            }

            /* A satisfied wait must complete immediately. */
            {
                D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait;

                memset(&wait, 0, sizeof(wait));
                wait.hDevice = hDevice;
                wait.ObjectCount = 1;
                wait.ObjectHandleArray = Handles;
                wait.FenceValueArray = Values;

                Status = pWait(&wait);
                ok(NT_SUCCESS(Status),
                   "WaitForSynchronizationObjectFromCpu(42) failed "
                   "0x%08lX\n", (long)Status);
            }
        }
    }

    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
        memset(&dso, 0, sizeof(dso));
        dso.hSyncObject = cso.hSyncObject;
        Status = pDestroy(&dso);
        ok(NT_SUCCESS(Status),
           "DestroySynchronizationObject failed 0x%08lX\n", (long)Status);
    }

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

static NTSTATUS D3dkmtTestSignalFromCpu(_In_ PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal, _In_ D3DKMT_HANDLE Device, _In_reads_(Count) const D3DKMT_HANDLE *Handles, _In_reads_(Count) const UINT64 *FenceValues, _In_ UINT Count, _In_ BOOL AllowFenceRewind)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Arguments;

    memset(&Arguments, 0, sizeof(Arguments));
    Arguments.hDevice = Device;
    Arguments.ObjectCount = Count;
    Arguments.ObjectHandleArray = Handles;
    Arguments.FenceValueArray = FenceValues;
    Arguments.Flags.AllowFenceRewind = AllowFenceRewind != FALSE;
    return Signal(&Arguments);
}

static NTSTATUS D3dkmtTestWaitFromCpu(_In_ PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait, _In_ D3DKMT_HANDLE Device, _In_reads_(Count) const D3DKMT_HANDLE *Handles, _In_reads_(Count) const UINT64 *FenceValues, _In_ UINT Count, _In_opt_ HANDLE Event, _In_ BOOL WaitAny)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Arguments;

    memset(&Arguments, 0, sizeof(Arguments));
    Arguments.hDevice = Device;
    Arguments.ObjectCount = Count;
    Arguments.ObjectHandleArray = Handles;
    Arguments.FenceValueArray = FenceValues;
    Arguments.hAsyncEvent = Event;
    Arguments.Flags.WaitAny = WaitAny != FALSE;
    return Wait(&Arguments);
}

static void D3dkmtTestDestroySyncObject(_In_ PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy, _In_ D3DKMT_HANDLE Handle)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Arguments;
    NTSTATUS Status;

    if (Handle == 0)
        return;
    memset(&Arguments, 0, sizeof(Arguments));
    Arguments.hSyncObject = Handle;
    Status = Destroy(&Arguments);
    ok(NT_SUCCESS(Status), "DestroySynchronizationObject(0x%08lX) failed 0x%08lX\n", (unsigned long)Handle, (long)Status);
}

static void Test_CreateSyncObject2_NormalizedState(void)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    HANDLE Event = NULL;
    HANDLE RestrictedEvent = NULL;
    NTSTATUS Status;

    if (pCreate == NULL || pDestroy == NULL)
    {
        skip("CreateSynchronizationObject2 lifecycle entry points unavailable\n");
        return;
    }
    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        skip("CreateDevice failed\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_FENCE;
    Create.Info.Fence.FenceValue = 0x100000002ULL;
    Status = pCreate(&Create);
    ok(NT_SUCCESS(Status), "CreateSynchronizationObject2(fence) failed 0x%08lX\n", (long)Status);
    D3dkmtTestDestroySyncObject(pDestroy, Create.hSyncObject);

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_SEMAPHORE;
    Create.Info.Semaphore.MaxCount = 0;
    Status = pCreate(&Create);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Create.Info.Semaphore.MaxCount = 2;
    Create.Info.Semaphore.InitialCount = 3;
    Status = pCreate(&Create);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_CPU_NOTIFICATION;
    Status = pCreate(&Create);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed %lu\n", GetLastError());
    if (Event != NULL)
    {
        memset(&Create, 0, sizeof(Create));
        Create.hDevice = hDevice;
        Create.Info.Type = D3DDDI_CPU_NOTIFICATION;
        Create.Info.CPUNotification.Event = Event;
        Status = pCreate(&Create);
        ok(NT_SUCCESS(Status), "CreateSynchronizationObject2(CPU_NOTIFICATION) failed 0x%08lX\n", (long)Status);
        CloseHandle(Event);
        Event = NULL;
        D3dkmtTestDestroySyncObject(pDestroy, Create.hSyncObject);
    }

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Event != NULL && DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(), &RestrictedEvent, SYNCHRONIZE, FALSE, 0))
    {
        memset(&Create, 0, sizeof(Create));
        Create.hDevice = hDevice;
        Create.Info.Type = D3DDDI_CPU_NOTIFICATION;
        Create.Info.CPUNotification.Event = RestrictedEvent;
        Status = pCreate(&Create);
        ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    }
    if (RestrictedEvent != NULL)
        CloseHandle(RestrictedEvent);
    if (Event != NULL)
        CloseHandle(Event);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
#else
    skip("CreateSynchronizationObject2 needs WDDM 2.0 headers\n");
#endif
}

static void Test_MonitoredFence_AccessFlags(void)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreate;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy;
    PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU pSignal;
    PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU pWait;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE Handle;
    volatile const UINT64 *FencePage;
    HANDLE Event;
    UINT64 Value;
    NTSTATUS Status;

    pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    pSignal = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU)LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromCpu");
    pWait = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU)LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromCpu");
    if (!pCreate || !pDestroy || !pSignal || !pWait)
    {
        skip("monitored-fence access-flag entry points not exported\n");
        return;
    }
    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = 1;
    Create.Info.MonitoredFence.InitialFenceValue = 11;
    Create.Info.MonitoredFence.EngineAffinity = 1;
    Create.Info.SharedHandle = (D3DKMT_HANDLE)0xBAD0F003;
    Status = pCreate(&Create);
    if (!NT_SUCCESS(Status))
    {
        skip("NoGPUAccess monitored fence create refused 0x%08lX\n", (long)Status);
        goto Cleanup;
    }
    Handle = Create.hSyncObject;
    FencePage = (volatile const UINT64 *)Create.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    ok(FencePage != NULL, "NoGPUAccess fence must retain its CPU mapping\n");
    ok(Create.Info.MonitoredFence.FenceValueGPUVirtualAddress == 0, "NoGPUAccess fence received GPU VA %I64x\n", Create.Info.MonitoredFence.FenceValueGPUVirtualAddress);
    ok(Create.Info.SharedHandle == 0, "unshared monitored fence echoed SharedHandle 0x%08lX\n", (unsigned long)Create.Info.SharedHandle);
    if (FencePage != NULL)
        ok(*FencePage == 11, "NoGPUAccess initial fence is %I64u, expected 11\n", *FencePage);
    Value = 12;
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handle, &Value, 1, FALSE);
    ok(NT_SUCCESS(Status), "NoGPUAccess CPU signal failed 0x%08lX\n", (long)Status);
    Status = D3dkmtTestWaitFromCpu(pWait, hDevice, &Handle, &Value, 1, NULL, FALSE);
    ok(NT_SUCCESS(Status), "NoGPUAccess CPU wait failed 0x%08lX\n", (long)Status);
    D3dkmtTestDestroySyncObject(pDestroy, Handle);

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = 1;
    Create.Info.MonitoredFence.EngineAffinity = 2;
    Status = pCreate(&Create);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = 1;
    Create.Info.MonitoredFence.Padding = 1;
    Status = pCreate(&Create);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = 1;
    Create.Info.Flags.NoSignal = 1;
    Create.Info.MonitoredFence.InitialFenceValue = 3;
    Status = pCreate(&Create);
    ok(NT_SUCCESS(Status), "NoSignal monitored fence create failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        Handle = Create.hSyncObject;
        FencePage = (volatile const UINT64 *)Create.Info.MonitoredFence.FenceValueCPUVirtualAddress;
        Value = 4;
        Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handle, &Value, 1, FALSE);
        ok_eq_hex(Status, STATUS_ACCESS_DENIED);
        if (FencePage != NULL)
            ok(*FencePage == 3, "NoSignal denial changed fence to %I64u\n", *FencePage);
        D3dkmtTestDestroySyncObject(pDestroy, Handle);
    }

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = 1;
    Create.Info.Flags.NoWait = 1;
    Create.Info.MonitoredFence.InitialFenceValue = 5;
    Status = pCreate(&Create);
    ok(NT_SUCCESS(Status), "NoWait monitored fence create failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        Handle = Create.hSyncObject;
        Value = 6;
        Event = CreateEventW(NULL, FALSE, FALSE, NULL);
        ok(Event != NULL, "CreateEvent for NoWait test failed %lu\n", GetLastError());
        if (Event != NULL)
        {
            Status = D3dkmtTestWaitFromCpu(pWait, hDevice, &Handle, &Value, 1, Event, FALSE);
            ok_eq_hex(Status, STATUS_ACCESS_DENIED);
            { DWORD Waited = WaitForSingleObject(Event, 0); ok_eq_ulong(Waited, (DWORD)WAIT_TIMEOUT); }
            CloseHandle(Event);
        }
        D3dkmtTestDestroySyncObject(pDestroy, Handle);
    }

    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = 1;
    Create.Info.Flags.NoSignal = 1;
    Create.Info.Flags.NoWait = 1;
    Status = pCreate(&Create);
    ok(!NT_SUCCESS(Status), "NoSignal+NoWait monitored fence create succeeded\n");

    /* A shared monitored fence publishes a global share handle that another
     * device on the same adapter can open into its own namespace. */
    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.Shared = 1;
    Create.Info.Flags.NoGPUAccess = 1;
    Status = pCreate(&Create);
    ok(NT_SUCCESS(Status), "Shared monitored fence create failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        PFND3DKMT_OPENSYNCHRONIZATIONOBJECT pOpen =
            (PFND3DKMT_OPENSYNCHRONIZATIONOBJECT)GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTOpenSynchronizationObject");

        ok(Create.Info.SharedHandle != 0, "Shared monitored fence published a zero share handle\n");
        if (pOpen != NULL && Create.Info.SharedHandle != 0)
        {
            D3DKMT_OPENSYNCHRONIZATIONOBJECT Open;

            memset(&Open, 0, sizeof(Open));
            Open.hSharedHandle = Create.Info.SharedHandle;
            Status = pOpen(&Open);
            ok(NT_SUCCESS(Status), "Open of a shared monitored fence failed 0x%08lX\n", (long)Status);
            if (NT_SUCCESS(Status))
            {
                ok(Open.hSyncObject != 0, "Open returned a zero sync handle\n");
                ok(Open.hSyncObject != Create.hSyncObject, "Open returned the creator's own handle\n");
                D3dkmtTestDestroySyncObject(pDestroy, Open.hSyncObject);
            }

            memset(&Open, 0, sizeof(Open));
            Open.hSharedHandle = Create.Info.SharedHandle + 0x1000;
            Status = pOpen(&Open);
            ok(!NT_SUCCESS(Status), "Open of an unknown share handle succeeded\n");
        }
        D3dkmtTestDestroySyncObject(pDestroy, Create.hSyncObject);
    }

    /* Legacy object types share through the same namespace. */
    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    Create.Info.Flags.Shared = 1;
    Create.Info.SynchronizationMutex.InitialState = FALSE;
    Status = pCreate(&Create);
    ok(NT_SUCCESS(Status), "Shared mutex create failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        ok(Create.Info.SharedHandle != 0, "Shared mutex published a zero share handle\n");
        D3dkmtTestDestroySyncObject(pDestroy, Create.hSyncObject);
    }

    /* A periodic monitored fence advances on its own vertical-blank cadence. */
    memset(&Create, 0, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_PERIODIC_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = 1;
    Status = pCreate(&Create);
    ok(NT_SUCCESS(Status), "Periodic monitored fence create failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        volatile UINT64 *Value = (volatile UINT64 *)Create.Info.PeriodicMonitoredFence.FenceValueCPUVirtualAddress;

        ok(Value != NULL, "Periodic fence exposed no CPU value mapping\n");
        if (Value != NULL)
        {
            UINT64 First = *Value;
            UINT Spin;

            for (Spin = 0; Spin < 200 && *Value == First; ++Spin)
                Sleep(10);
            ok(*Value > First, "Periodic fence did not advance (stuck at %I64u)\n", First);
        }
        D3dkmtTestDestroySyncObject(pDestroy, Create.hSyncObject);
    }

Cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
#else
    skip("Monitored fences need WDDM 2.0 headers\n");
#endif
}

static void Test_MonitoredFence_CpuBatchSemantics(void)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreate;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy;
    PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU pSignal;
    PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU pWait;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 FenceCreate[2];
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 AccessCreate;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 LegacyCreate;
    D3DKMT_HANDLE hAdapter = 0;
    D3DKMT_HANDLE hDevice = 0;
    D3DKMT_HANDLE Handles[2];
    UINT64 Values[2];
    volatile const UINT64 *FencePages[2];
    HANDLE Event = NULL;
    HANDLE DuplicateEvent = NULL;
    NTSTATUS Status;

    pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    pSignal = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU)LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromCpu");
    pWait = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU)LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromCpu");
    if (!pCreate || !pDestroy || !pSignal || !pWait)
    {
        skip("monitored-fence batch entry points not exported\n");
        return;
    }
    hAdapter = OpenAdapterFromDisplay1();
    if (hAdapter == 0)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    if (hDevice == 0)
    {
        skip("CreateDevice failed\n");
        CloseAdapter(hAdapter);
        return;
    }

    memset(FenceCreate, 0, sizeof(FenceCreate));
    FenceCreate[0].hDevice = hDevice;
    FenceCreate[0].Info.Type = D3DDDI_MONITORED_FENCE;
    FenceCreate[0].Info.MonitoredFence.InitialFenceValue = 0;
    Status = pCreate(&FenceCreate[0]);
    if (!NT_SUCCESS(Status))
    {
        skip("first monitored fence create refused 0x%08lX\n", (long)Status);
        goto Cleanup;
    }
    FenceCreate[1].hDevice = hDevice;
    FenceCreate[1].Info.Type = D3DDDI_MONITORED_FENCE;
    FenceCreate[1].Info.MonitoredFence.InitialFenceValue = 0;
    Status = pCreate(&FenceCreate[1]);
    if (!NT_SUCCESS(Status))
    {
        skip("second monitored fence create refused 0x%08lX\n", (long)Status);
        goto Cleanup;
    }
    Handles[0] = FenceCreate[0].hSyncObject;
    Handles[1] = FenceCreate[1].hSyncObject;
    FencePages[0] = (volatile const UINT64 *)FenceCreate[0].Info.MonitoredFence.FenceValueCPUVirtualAddress;
    FencePages[1] = (volatile const UINT64 *)FenceCreate[1].Info.MonitoredFence.FenceValueCPUVirtualAddress;
    ok(FencePages[0] != NULL && FencePages[1] != NULL, "monitored fence pages must be mapped\n");
    if (FencePages[0] == NULL || FencePages[1] == NULL)
        goto Cleanup;

    /* Validation is transactional: a bad later handle cannot mutate the first fence. */
    Handles[1] = (D3DKMT_HANDLE)0xBAD0F002;
    Values[0] = 1;
    Values[1] = 1;
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, Handles, Values, ARRAYSIZE(Handles), FALSE);
    ok(!NT_SUCCESS(Status), "signal batch with a bad later handle succeeded\n");
    ok(*FencePages[0] == 0, "failed signal batch mutated the first fence to %I64u\n", *FencePages[0]);
    Handles[1] = FenceCreate[1].hSyncObject;

    /* WaitAll remains pending after the first condition and completes after both. */
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEvent for WaitAll failed %lu\n", GetLastError());
    if (Event == NULL)
        goto Cleanup;
    Status = D3dkmtTestWaitFromCpu(pWait, hDevice, Handles, Values, ARRAYSIZE(Handles), Event, FALSE);
    ok(NT_SUCCESS(Status), "register WaitAll failed 0x%08lX\n", (long)Status);
    { DWORD Waited = WaitForSingleObject(Event, 0); ok_eq_ulong(Waited, (DWORD)WAIT_TIMEOUT); }
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handles[0], &Values[0], 1, FALSE);
    ok(NT_SUCCESS(Status), "signal first WaitAll fence failed 0x%08lX\n", (long)Status);
    { DWORD Waited = WaitForSingleObject(Event, 0); ok_eq_ulong(Waited, (DWORD)WAIT_TIMEOUT); }
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handles[1], &Values[1], 1, FALSE);
    ok(NT_SUCCESS(Status), "signal second WaitAll fence failed 0x%08lX\n", (long)Status);
    { DWORD Waited = WaitForSingleObject(Event, 2000); ok_eq_ulong(Waited, (DWORD)WAIT_OBJECT_0); }
    CloseHandle(Event);
    Event = NULL;

    /* WaitAny completes when either requested fence reaches its target. */
    Values[0] = 2;
    Values[1] = 2;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEvent for WaitAny failed %lu\n", GetLastError());
    if (Event == NULL)
        goto Cleanup;
    Status = D3dkmtTestWaitFromCpu(pWait, hDevice, Handles, Values, ARRAYSIZE(Handles), Event, TRUE);
    ok(NT_SUCCESS(Status), "register WaitAny failed 0x%08lX\n", (long)Status);
    { DWORD Waited = WaitForSingleObject(Event, 0); ok_eq_ulong(Waited, (DWORD)WAIT_TIMEOUT); }
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handles[0], &Values[0], 1, FALSE);
    ok(NT_SUCCESS(Status), "signal WaitAny fence failed 0x%08lX\n", (long)Status);
    { DWORD Waited = WaitForSingleObject(Event, 2000); ok_eq_ulong(Waited, (DWORD)WAIT_OBJECT_0); }
    CloseHandle(Event);
    Event = NULL;

    /* The kernel references hAsyncEvent, so closing its user handle is safe. */
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEvent for close-handle test failed %lu\n", GetLastError());
    if (Event == NULL)
        goto Cleanup;
    ok(DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(), &DuplicateEvent, SYNCHRONIZE, FALSE, 0), "DuplicateHandle failed %lu\n", GetLastError());
    if (DuplicateEvent == NULL)
        goto Cleanup;
    Values[1] = 2;
    Status = D3dkmtTestWaitFromCpu(pWait, hDevice, &Handles[1], &Values[1], 1, Event, FALSE);
    ok(NT_SUCCESS(Status), "register close-handle wait failed 0x%08lX\n", (long)Status);
    CloseHandle(Event);
    Event = NULL;
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handles[1], &Values[1], 1, FALSE);
    ok(NT_SUCCESS(Status), "signal close-handle wait failed 0x%08lX\n", (long)Status);
    { DWORD Waited = WaitForSingleObject(DuplicateEvent, 2000); ok_eq_ulong(Waited, (DWORD)WAIT_OBJECT_0); }
    CloseHandle(DuplicateEvent);
    DuplicateEvent = NULL;

    /* Default publication is monotonic; AllowFenceRewind explicitly lowers it. */
    Values[0] = 10;
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handles[0], &Values[0], 1, FALSE);
    ok(NT_SUCCESS(Status), "signal fence to 10 failed 0x%08lX\n", (long)Status);
    ok(*FencePages[0] == 10, "fence page should be 10, got %I64u\n", *FencePages[0]);
    Values[0] = 5;
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handles[0], &Values[0], 1, FALSE);
    ok(NT_SUCCESS(Status), "default lower signal failed 0x%08lX\n", (long)Status);
    ok(*FencePages[0] == 10, "default lower signal rewound fence to %I64u\n", *FencePages[0]);
    Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &Handles[0], &Values[0], 1, TRUE);
    ok(NT_SUCCESS(Status), "AllowFenceRewind signal failed 0x%08lX\n", (long)Status);
    ok(*FencePages[0] == 5, "AllowFenceRewind should lower fence to 5, got %I64u\n", *FencePages[0]);

    /* A denied second object rejects the whole signal batch before the first
     * object can be mutated. */
    memset(&AccessCreate, 0, sizeof(AccessCreate));
    AccessCreate.hDevice = hDevice;
    AccessCreate.Info.Type = D3DDDI_MONITORED_FENCE;
    AccessCreate.Info.Flags.NoGPUAccess = 1;
    AccessCreate.Info.Flags.NoSignal = 1;
    Status = pCreate(&AccessCreate);
    ok(NT_SUCCESS(Status), "NoSignal batch fence create failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        Handles[0] = FenceCreate[0].hSyncObject;
        Handles[1] = AccessCreate.hSyncObject;
        Values[0] = 6;
        Values[1] = 1;
        Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, Handles, Values, ARRAYSIZE(Handles), FALSE);
        ok_eq_hex(Status, STATUS_ACCESS_DENIED);
        ok(*FencePages[0] == 5, "denied second signal object mutated the first fence to %I64u\n", *FencePages[0]);
        D3dkmtTestDestroySyncObject(pDestroy, AccessCreate.hSyncObject);
        AccessCreate.hSyncObject = 0;
    }

    /* NoWait is likewise request-atomic and cannot register the event after
     * validating an otherwise eligible first object. */
    memset(&AccessCreate, 0, sizeof(AccessCreate));
    AccessCreate.hDevice = hDevice;
    AccessCreate.Info.Type = D3DDDI_MONITORED_FENCE;
    AccessCreate.Info.Flags.NoGPUAccess = 1;
    AccessCreate.Info.Flags.NoWait = 1;
    Status = pCreate(&AccessCreate);
    ok(NT_SUCCESS(Status), "NoWait batch fence create failed 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        Handles[0] = FenceCreate[0].hSyncObject;
        Handles[1] = AccessCreate.hSyncObject;
        Values[0] = 6;
        Values[1] = 1;
        Event = CreateEventW(NULL, FALSE, FALSE, NULL);
        ok(Event != NULL, "CreateEvent for NoWait batch failed %lu\n", GetLastError());
        if (Event != NULL)
        {
            Status = D3dkmtTestWaitFromCpu(pWait, hDevice, Handles, Values, ARRAYSIZE(Handles), Event, FALSE);
            ok_eq_hex(Status, STATUS_ACCESS_DENIED);
            { DWORD Waited = WaitForSingleObject(Event, 0); ok_eq_ulong(Waited, (DWORD)WAIT_TIMEOUT); }
            CloseHandle(Event);
            Event = NULL;
        }
        D3dkmtTestDestroySyncObject(pDestroy, AccessCreate.hSyncObject);
        AccessCreate.hSyncObject = 0;
    }

    /* Registering an asynchronous wait requires event-modify access, not
     * merely the ability to wait on the event. */
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEvent for access test failed %lu\n", GetLastError());
    if (Event != NULL)
    {
        ok(DuplicateHandle(GetCurrentProcess(), Event, GetCurrentProcess(), &DuplicateEvent, SYNCHRONIZE, FALSE, 0), "DuplicateHandle for access test failed %lu\n", GetLastError());
        if (DuplicateEvent != NULL)
        {
            Handles[0] = FenceCreate[0].hSyncObject;
            Values[0] = 6;
            Status = D3dkmtTestWaitFromCpu(pWait, hDevice, Handles, Values, 1, DuplicateEvent, FALSE);
            ok_eq_hex(Status, STATUS_ACCESS_DENIED);
            { DWORD Waited = WaitForSingleObject(Event, 0); ok_eq_ulong(Waited, (DWORD)WAIT_TIMEOUT); }
            CloseHandle(DuplicateEvent);
            DuplicateEvent = NULL;
        }
        CloseHandle(Event);
        Event = NULL;
    }

    /* Legacy synchronization objects cannot be passed to FromCpu operations. */
    memset(&LegacyCreate, 0, sizeof(LegacyCreate));
    LegacyCreate.hDevice = hDevice;
    LegacyCreate.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    LegacyCreate.Info.SynchronizationMutex.InitialState = FALSE;
    Status = pCreate(&LegacyCreate);
    if (NT_SUCCESS(Status))
    {
        D3DKMT_HANDLE LegacyHandle = LegacyCreate.hSyncObject;
        UINT64 LegacyValue = 1;

        Status = D3dkmtTestSignalFromCpu(pSignal, hDevice, &LegacyHandle, &LegacyValue, 1, FALSE);
        ok(!NT_SUCCESS(Status), "SignalSynchronizationObjectFromCpu accepted a legacy mutex\n");
        D3dkmtTestDestroySyncObject(pDestroy, LegacyHandle);
    }
    else
    {
        skip("legacy mutex create refused; wrong-type FromCpu case skipped\n");
    }

Cleanup:
    if (DuplicateEvent != NULL)
        CloseHandle(DuplicateEvent);
    if (Event != NULL)
        CloseHandle(Event);
    D3dkmtTestDestroySyncObject(pDestroy, FenceCreate[1].hSyncObject);
    D3dkmtTestDestroySyncObject(pDestroy, FenceCreate[0].hSyncObject);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
#else
    skip("Monitored fences need WDDM 2.0 headers\n");
#endif
}

START_TEST(sync2)
{
    Test_CreateSyncObject2_NullArg();
    Test_WaitForSyncObject2_NullArg();
    Test_SignalSyncObject2_NullArg();
    Test_WaitForSyncObjectFromCpu_NullArg();
    Test_SignalSyncObjectFromCpu_NullArg();
    Test_SignalSyncObjectFromGpu_NullArg();
    Test_CreateSyncObject2_NormalizedState();
    Test_MonitoredFence_Lifecycle();
    Test_MonitoredFence_CpuValuePage();
    Test_MonitoredFence_AccessFlags();
    Test_MonitoredFence_CpuBatchSemantics();
}
