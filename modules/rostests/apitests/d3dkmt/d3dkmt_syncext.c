/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Extended D3DKMT synchronization-object matrix and keyed-mutex
 *              lifecycle tests (goes deeper/wider than d3dkmt_sync.c / sync2.c /
 *              surface.c, which only cover the basic NULL contracts).
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Two groups:
 *
 *   START_TEST(syncext)
 *     - D3DKMTCreateSynchronizationObject2 for EVERY D3DDDI_SYNCHRONIZATIONOBJECT_TYPE
 *       the header defines at the compiled DXGKDDI_INTERFACE_VERSION (0x5023 =
 *       WDDM 2.0): SynchronizationMutex, Semaphore, Fence, CPU_NOTIFICATION (needs
 *       a real event), MonitoredFence.  PeriodicMonitoredFence is WDDM 2.2 (0x700A)
 *       and is compiled out at 0x5023, so it is preprocessor-guarded and skipped.
 *     - Monitored-fence CPU signal/wait round trip (SignalSynchronizationObjectFromCpu
 *       then WaitForSynchronizationObjectFromCpu via a bounded async event).
 *     - D3DKMTOpenSynchronizationObject: NULL contract + skip-guarded positive
 *       (create a Shared sync object, open it by its global shared handle).
 *     - D3DKMTWaitForIdle skip-guarded positive on a real device.
 *     - Legacy v1 D3DKMTCreateSynchronizationObject mutex+semaphore lifecycle as a
 *       Win11-green parity baseline (skip-guarded, never asserts create success).
 *
 *   START_TEST(keyedmutexlife)
 *     - NULL contract for the keyed-mutex entry points NOT already covered by
 *       d3dkmt_surface.c's keyedmutex group (Destroy / Acquire / Acquire2 /
 *       Release / Release2), plus bogus-handle rejection.
 *     - Full skip-guarded lifecycle: CreateKeyedMutex -> Acquire/Release round trip
 *       -> OpenKeyedMutex by shared handle -> DestroyKeyedMutex (+ double-destroy).
 *     - Same for the v2 family (CreateKeyedMutex2 / AcquireKeyedMutex2 /
 *       ReleaseKeyedMutex2).
 *
 * Win11-green rules followed throughout: NULL/param checks assert refusal only;
 * positive lifecycle paths attempt the operation on a real device and skip() on
 * failure (a display-only / QEMU adapter may legitimately refuse render-only
 * object types) rather than ever asserting success.  AV-prone calls are guarded
 * with SEH; values are trace()d and ok() is reserved for invariants.
 *
 * Reference: Microsoft "D3DKMTCreateSynchronizationObject2", "Monitored fences",
 *            "D3DKMTSignalSynchronizationObjectFromCpu",
 *            "D3DKMTWaitForSynchronizationObjectFromCpu",
 *            "D3DKMTOpenSynchronizationObject", "D3DKMTCreateKeyedMutex(2)",
 *            "D3DKMTAcquireKeyedMutex(2)", "D3DKMTReleaseKeyedMutex(2)".
 */

#include "precomp.h"

/*
 * Run a single D3DKMT statement under SEH so a thunk that dereferences before it
 * validates cannot abort the whole subtest.  FaultFlag (volatile BOOL) is set to
 * TRUE if the call raised.  Stmt must contain no top-level comma.
 */
#define SYNCEXT_TRY(FaultFlag, Stmt)                    \
    do {                                                \
        (FaultFlag) = FALSE;                            \
        _SEH2_TRY { Stmt; }                             \
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {       \
            (FaultFlag) = TRUE;                         \
        } _SEH2_END;                                    \
    } while (0)

/* ===================================================================== */
/* ===================  Group 1: syncext  ============================== */
/* ===================================================================== */

/*
 * Create one synchronization object of the type already filled into *pInfo on a
 * real device, assert the handle invariant on success and destroy it, or skip
 * when the device refuses that object type.  Never asserts create success.
 */
static void
TrySyncObjectType(D3DKMT_HANDLE hDevice,
                  const char *Label,
                  const D3DDDI_SYNCHRONIZATIONOBJECTINFO2 *pInfo)
{
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreate;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cso;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
    volatile NTSTATUS Status = STATUS_SUCCESS;
    volatile BOOL faulted = FALSE;

    pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)
              LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)
               LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    if (!pCreate || !pDestroy)
    {
        skip("CreateSynchronizationObject2/Destroy not exported by gdi32.dll\n");
        return;
    }

    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info = *pInfo;

    SYNCEXT_TRY(faulted, Status = pCreate(&cso));
    if (faulted)
    {
        skip("CreateSynchronizationObject2(%s) raised an exception\n", Label);
        return;
    }
    if (!NT_SUCCESS(Status))
    {
        skip("CreateSynchronizationObject2(%s) refused on this device (0x%08lX)\n",
             Label, (long)Status);
        return;
    }

    trace("CreateSynchronizationObject2(%s): hSyncObject=0x%08lX\n",
          Label, (unsigned long)cso.hSyncObject);
    ok(cso.hSyncObject != 0,
       "CreateSynchronizationObject2(%s) returned a zero handle\n", Label);

    memset(&dso, 0, sizeof(dso));
    dso.hSyncObject = cso.hSyncObject;
    Status = pDestroy(&dso);
    ok(NT_SUCCESS(Status),
       "DestroySynchronizationObject(%s) failed (0x%08lX)\n", Label, (long)Status);
}

/* CreateSynchronizationObject2 across the whole type matrix. */
static void Test_Sync2_TypeMatrix(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DDDI_SYNCHRONIZATIONOBJECTINFO2 info;
    HANDLE hEvent;

    if (!LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2"))
    {
        skip("D3DKMTCreateSynchronizationObject2 not exported by gdi32.dll\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("CreateDevice failed; render-only sync types need a device\n");
        CloseAdapter(hAdapter);
        return;
    }

    /* 1. Synchronization mutex (WDDM 1.0) */
    memset(&info, 0, sizeof(info));
    info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    info.SynchronizationMutex.InitialState = TRUE;
    TrySyncObjectType(hDevice, "SynchronizationMutex", &info);

    /* 2. Semaphore (WDDM 1.0) */
    memset(&info, 0, sizeof(info));
    info.Type = D3DDDI_SEMAPHORE;
    info.Semaphore.MaxCount = 8;
    info.Semaphore.InitialCount = 4;
    TrySyncObjectType(hDevice, "Semaphore", &info);

    /* 3. Fence (WDDM 1.1) */
    memset(&info, 0, sizeof(info));
    info.Type = D3DDDI_FENCE;
    info.Fence.FenceValue = 0;
    TrySyncObjectType(hDevice, "Fence", &info);

    /* 4. CPU notification (WDDM 1.1) -- requires a real event handle */
    hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (hEvent)
    {
        memset(&info, 0, sizeof(info));
        info.Type = D3DDDI_CPU_NOTIFICATION;
        info.CPUNotification.Event = hEvent;
        TrySyncObjectType(hDevice, "CpuNotification", &info);
        CloseHandle(hEvent);
    }
    else
    {
        skip("CreateEvent failed for the CPU_NOTIFICATION test\n");
    }

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    /* 5. Monitored fence (WDDM 2.0) -- the WDDM 2.x scheduling primitive */
    memset(&info, 0, sizeof(info));
    info.Type = D3DDDI_MONITORED_FENCE;
    info.MonitoredFence.InitialFenceValue = 0;
    TrySyncObjectType(hDevice, "MonitoredFence", &info);
#else
    skip("D3DDDI_MONITORED_FENCE not defined at this DXGKDDI_INTERFACE_VERSION\n");
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
    /* 6. Periodic monitored fence (WDDM 2.2) -- bound to a VidPn target */
    memset(&info, 0, sizeof(info));
    info.Type = D3DDDI_PERIODIC_MONITORED_FENCE;
    info.PeriodicMonitoredFence.hAdapter = hAdapter;
    info.PeriodicMonitoredFence.VidPnTargetId = 0;
    info.PeriodicMonitoredFence.Time = 0;
    TrySyncObjectType(hDevice, "PeriodicMonitoredFence", &info);
#else
    skip("D3DDDI_PERIODIC_MONITORED_FENCE needs WDDM 2.2 (0x700A); skipped at 0x%X\n",
         (unsigned int)DXGKDDI_INTERFACE_VERSION);
#endif

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/*
 * Monitored fence: create one, signal it to a value from the CPU, then wait for
 * that value from the CPU using an async event with a bounded host wait so the
 * test can never hang.  Entirely skip-guarded (needs a WDDM 2.0 render device).
 */
static void Test_MonitoredFence_CpuSignalWait(void)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreate;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy;
    PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU pSignal;
    PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU pWait;
    D3DKMT_HANDLE hAdapter, hDevice, hSync;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cso;
    D3DKMT_HANDLE objs[1];
    UINT64 fenceVals[1];
    volatile NTSTATUS Status = STATUS_SUCCESS;
    volatile BOOL faulted = FALSE;

    pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)
              LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)
               LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    pSignal = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU)
              LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromCpu");
    pWait = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU)
            LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromCpu");
    if (!pCreate || !pDestroy || !pSignal || !pWait)
    {
        skip("Monitored-fence CPU signal/wait entry points not all exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_MONITORED_FENCE;
    cso.Info.MonitoredFence.InitialFenceValue = 0;

    SYNCEXT_TRY(faulted, Status = pCreate(&cso));
    if (faulted || !NT_SUCCESS(Status))
    {
        skip("Monitored fence create not supported here (0x%08lX%s)\n",
             (long)Status, faulted ? ", faulted" : "");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    hSync = cso.hSyncObject;
    ok(hSync != 0, "Monitored fence handle should be non-zero\n");
    trace("Monitored fence: hSyncObject=0x%08lX FenceValueCPUVirtualAddress=%p\n",
          (unsigned long)hSync, cso.Info.MonitoredFence.FenceValueCPUVirtualAddress);

    /* Signal the fence to 1 from the CPU. */
    objs[0] = hSync;
    fenceVals[0] = 1;
    {
        D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU sig;
        memset(&sig, 0, sizeof(sig));
        sig.hDevice = hDevice;
        sig.ObjectCount = 1;
        sig.ObjectHandleArray = objs;
        sig.FenceValueArray = fenceVals;
        SYNCEXT_TRY(faulted, Status = pSignal(&sig));
        trace("SignalSynchronizationObjectFromCpu(->1) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");
    }

    /* Wait for value 1 from the CPU through a bounded async event. */
    if (!faulted && NT_SUCCESS(Status))
    {
        HANDLE hWaitEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait;

        memset(&wait, 0, sizeof(wait));
        wait.hDevice = hDevice;
        wait.ObjectCount = 1;
        wait.ObjectHandleArray = objs;
        fenceVals[0] = 1;
        wait.FenceValueArray = fenceVals;
        wait.hAsyncEvent = hWaitEvent;     /* non-blocking: returns immediately */

        SYNCEXT_TRY(faulted, Status = pWait(&wait));
        trace("WaitForSynchronizationObjectFromCpu(==1) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");

        if (!faulted && NT_SUCCESS(Status) && hWaitEvent)
        {
            DWORD wr = WaitForSingleObject(hWaitEvent, 1000);
            trace("  async wait event -> %lu (0=signalled, 258=timeout)\n",
                  (unsigned long)wr);
        }
        if (hWaitEvent)
            CloseHandle(hWaitEvent);
    }

    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
        memset(&dso, 0, sizeof(dso));
        dso.hSyncObject = hSync;
        Status = pDestroy(&dso);
        ok(NT_SUCCESS(Status),
           "DestroySynchronizationObject(monitored fence) failed (0x%08lX)\n",
           (long)Status);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
#else
    skip("Monitored fences need WDDM 2.0 headers; skipped\n");
#endif
}

/* D3DKMTOpenSynchronizationObject: create a Shared object, reopen by handle. */
static void Test_OpenSyncObject_Positive(void)
{
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pCreate;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy;
    PFND3DKMT_OPENSYNCHRONIZATIONOBJECT pOpen;
    D3DKMT_HANDLE hAdapter, hDevice, hSync, hShared;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cso;
    volatile NTSTATUS Status = STATUS_SUCCESS;
    volatile BOOL faulted = FALSE;

    pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)
              LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)
               LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    pOpen = (PFND3DKMT_OPENSYNCHRONIZATIONOBJECT)
            LoadD3DKMTProc("D3DKMTOpenSynchronizationObject");
    if (!pCreate || !pDestroy || !pOpen)
    {
        skip("Create/Open/DestroySynchronizationObject not all exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    /* Create a legacy-shareable mutex (Shared bit -> global SharedHandle). */
    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    cso.Info.Flags.Shared = 1;
    cso.Info.SynchronizationMutex.InitialState = FALSE;

    SYNCEXT_TRY(faulted, Status = pCreate(&cso));
    if (faulted || !NT_SUCCESS(Status))
    {
        skip("Shared sync object create not supported here (0x%08lX%s)\n",
             (long)Status, faulted ? ", faulted" : "");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    hSync = cso.hSyncObject;
    hShared = cso.Info.SharedHandle;
    ok(hSync != 0, "Shared sync object returned a zero local handle\n");
    trace("Shared sync object: hSyncObject=0x%08lX SharedHandle=0x%08lX\n",
          (unsigned long)hSync, (unsigned long)hShared);

    if (hShared)
    {
        D3DKMT_OPENSYNCHRONIZATIONOBJECT open;
        memset(&open, 0, sizeof(open));
        open.hSharedHandle = hShared;
        SYNCEXT_TRY(faulted, Status = pOpen(&open));
        if (!faulted && NT_SUCCESS(Status))
        {
            trace("OpenSynchronizationObject: hSyncObject=0x%08lX\n",
                  (unsigned long)open.hSyncObject);
            ok(open.hSyncObject != 0,
               "OpenSynchronizationObject returned a zero handle\n");
            if (open.hSyncObject)
            {
                D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
                memset(&dso, 0, sizeof(dso));
                dso.hSyncObject = open.hSyncObject;
                pDestroy(&dso);
            }
        }
        else
        {
            skip("OpenSynchronizationObject refused (0x%08lX%s)\n",
                 (long)Status, faulted ? ", faulted" : "");
        }
    }
    else
    {
        skip("No global shared handle returned; cannot exercise Open\n");
    }

    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
        memset(&dso, 0, sizeof(dso));
        dso.hSyncObject = hSync;
        Status = pDestroy(&dso);
        ok(NT_SUCCESS(Status),
           "DestroySynchronizationObject(shared) failed (0x%08lX)\n", (long)Status);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* D3DKMTWaitForIdle skip-guarded positive on a real device. */
static void Test_WaitForIdle_Positive(void)
{
    PFND3DKMT_WAITFORIDLE pWait;
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_WAITFORIDLE wfi;
    volatile NTSTATUS Status = STATUS_SUCCESS;
    volatile BOOL faulted = FALSE;

    pWait = (PFND3DKMT_WAITFORIDLE)LoadD3DKMTProc("D3DKMTWaitForIdle");
    if (!pWait) { skip("D3DKMTWaitForIdle not exported by gdi32.dll\n"); return; }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&wfi, 0, sizeof(wfi));
    wfi.hDevice = hDevice;
    SYNCEXT_TRY(faulted, Status = pWait(&wfi));
    trace("WaitForIdle(valid device) -> 0x%08lX%s\n",
          (long)Status, faulted ? " (faulted)" : "");
    if (!faulted && NT_SUCCESS(Status))
        ok(TRUE, "WaitForIdle succeeded on a valid device\n");
    else
        skip("WaitForIdle not supported on this device\n");

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* Legacy v1 CreateSynchronizationObject mutex + semaphore (parity baseline). */
static void Test_LegacySync_V1_Lifecycle(void)
{
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT pCreate;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pDestroy;
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT cso;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
    NTSTATUS Status;

    pCreate = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT)
              LoadD3DKMTProc("D3DKMTCreateSynchronizationObject");
    pDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)
               LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    if (!pCreate || !pDestroy)
    {
        skip("D3DKMTCreateSynchronizationObject/Destroy not exported\n");
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    /* v1 mutex */
    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    cso.Info.SynchronizationMutex.InitialState = TRUE;
    Status = pCreate(&cso);
    if (NT_SUCCESS(Status))
    {
        trace("v1 CreateSynchronizationObject(Mutex): 0x%08lX\n",
              (unsigned long)cso.hSyncObject);
        ok(cso.hSyncObject != 0, "v1 mutex returned a zero handle\n");
        memset(&dso, 0, sizeof(dso));
        dso.hSyncObject = cso.hSyncObject;
        Status = pDestroy(&dso);
        ok(NT_SUCCESS(Status), "v1 mutex destroy failed (0x%08lX)\n", (long)Status);
    }
    else
    {
        skip("v1 CreateSynchronizationObject(Mutex) refused (0x%08lX)\n", (long)Status);
    }

    /* v1 semaphore */
    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_SEMAPHORE;
    cso.Info.Semaphore.MaxCount = 4;
    cso.Info.Semaphore.InitialCount = 2;
    Status = pCreate(&cso);
    if (NT_SUCCESS(Status))
    {
        trace("v1 CreateSynchronizationObject(Semaphore): 0x%08lX\n",
              (unsigned long)cso.hSyncObject);
        ok(cso.hSyncObject != 0, "v1 semaphore returned a zero handle\n");
        memset(&dso, 0, sizeof(dso));
        dso.hSyncObject = cso.hSyncObject;
        Status = pDestroy(&dso);
        ok(NT_SUCCESS(Status), "v1 semaphore destroy failed (0x%08lX)\n", (long)Status);
    }
    else
    {
        skip("v1 CreateSynchronizationObject(Semaphore) refused (0x%08lX)\n", (long)Status);
    }

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

typedef struct _SYNC_DEVICE_TEARDOWN_RACE
{
    HANDLE StartEvent;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hSyncObject;
    PFN_D3DKMTDestroyDevice DestroyDevice;
    PFN_D3DKMTDestroySynchronizationObject DestroySyncObject;
    volatile NTSTATUS DeviceStatus;
    volatile NTSTATUS SyncStatus;
} SYNC_DEVICE_TEARDOWN_RACE;

static DWORD WINAPI SyncDeviceRaceDestroySync(LPVOID Parameter)
{
    SYNC_DEVICE_TEARDOWN_RACE *Race = Parameter;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;

    WaitForSingleObject(Race->StartEvent, INFINITE);
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hSyncObject = Race->hSyncObject;
    Race->SyncStatus = Race->DestroySyncObject(&Destroy);
    return 0;
}

static DWORD WINAPI SyncDeviceRaceDestroyDevice(LPVOID Parameter)
{
    SYNC_DEVICE_TEARDOWN_RACE *Race = Parameter;
    D3DKMT_DESTROYDEVICE Destroy;

    WaitForSingleObject(Race->StartEvent, INFINITE);
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hDevice = Race->hDevice;
    Race->DeviceStatus = Race->DestroyDevice(&Destroy);
    return 0;
}

/* Regression for direct sync-object destruction racing the device's bulk cleanup. */
static void Test_SyncDeviceConcurrentTeardown(void)
{
    PFN_D3DKMTCreateSynchronizationObject CreateSyncObject;
    PFN_D3DKMTDestroySynchronizationObject DestroySyncObject;
    PFN_D3DKMTDestroyDevice DestroyDevice;
    D3DKMT_HANDLE hAdapter;
    ULONG Iteration;

    CreateSyncObject = (PFN_D3DKMTCreateSynchronizationObject)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject");
    DestroySyncObject = (PFN_D3DKMTDestroySynchronizationObject)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    DestroyDevice = (PFN_D3DKMTDestroyDevice)LoadD3DKMTProc("D3DKMTDestroyDevice");
    if (!CreateSyncObject || !DestroySyncObject || !DestroyDevice)
    {
        skip("sync/device teardown race entry points not all exported\n");
        return;
    }

    hAdapter = OpenRenderAdapterEx(NULL, NULL);
    if (!hAdapter)
        hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter for sync/device teardown race\n");
        return;
    }

    for (Iteration = 0; Iteration < 32; ++Iteration)
    {
        SYNC_DEVICE_TEARDOWN_RACE Race;
        D3DKMT_CREATESYNCHRONIZATIONOBJECT Create;
        HANDLE Threads[2];
        DWORD WaitStatus;

        memset(&Race, 0, sizeof(Race));
        Race.hDevice = CreateTestDevice(hAdapter);
        if (!Race.hDevice)
        {
            skip("CreateDevice failed at teardown-race iteration %lu\n", Iteration);
            break;
        }

        memset(&Create, 0, sizeof(Create));
        Create.hDevice = Race.hDevice;
        Create.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
        if (!NT_SUCCESS(CreateSyncObject(&Create)))
        {
            skip("CreateSynchronizationObject failed at teardown-race iteration %lu\n", Iteration);
            DestroyTestDevice(Race.hDevice);
            break;
        }

        Race.hSyncObject = Create.hSyncObject;
        Race.DestroyDevice = DestroyDevice;
        Race.DestroySyncObject = DestroySyncObject;
        Race.DeviceStatus = STATUS_UNSUCCESSFUL;
        Race.SyncStatus = STATUS_UNSUCCESSFUL;
        Race.StartEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        Threads[0] = Race.StartEvent ? CreateThread(NULL, 0, SyncDeviceRaceDestroySync, &Race, 0, NULL) : NULL;
        Threads[1] = Race.StartEvent ? CreateThread(NULL, 0, SyncDeviceRaceDestroyDevice, &Race, 0, NULL) : NULL;
        if (!Race.StartEvent || !Threads[0] || !Threads[1])
        {
            if (Race.StartEvent)
                SetEvent(Race.StartEvent);
            if (Threads[0])
                WaitForSingleObject(Threads[0], INFINITE);
            if (Threads[1])
                WaitForSingleObject(Threads[1], INFINITE);
            if (Threads[0])
                CloseHandle(Threads[0]);
            if (Threads[1])
                CloseHandle(Threads[1]);
            if (Race.StartEvent)
                CloseHandle(Race.StartEvent);
            if (!Threads[1])
                DestroyTestDevice(Race.hDevice);
            skip("Unable to create teardown-race synchronization objects\n");
            break;
        }

        SetEvent(Race.StartEvent);
        WaitStatus = WaitForMultipleObjects(ARRAYSIZE(Threads), Threads, TRUE, 10000);
        ok(WaitStatus == WAIT_OBJECT_0, "sync/device teardown race timed out at iteration %lu (wait=%lu)\n", Iteration, WaitStatus);
        if (WaitStatus != WAIT_OBJECT_0)
            WaitForMultipleObjects(ARRAYSIZE(Threads), Threads, TRUE, INFINITE);
        ok(NT_SUCCESS(Race.DeviceStatus), "DestroyDevice failed at teardown-race iteration %lu with 0x%08lX (sync=0x%08lX)\n", Iteration, (long)Race.DeviceStatus, (long)Race.SyncStatus);
        CloseHandle(Threads[0]);
        CloseHandle(Threads[1]);
        CloseHandle(Race.StartEvent);
    }

    CloseAdapter(hAdapter);
}

START_TEST(syncext)
{
    /* NULL contract not already covered by sync.c / sync2.c. */
    CHECK_NULL_REJECTED(PFND3DKMT_OPENSYNCHRONIZATIONOBJECT, "D3DKMTOpenSynchronizationObject");

    Test_Sync2_TypeMatrix();
    Test_MonitoredFence_CpuSignalWait();
    Test_OpenSyncObject_Positive();
    Test_WaitForIdle_Positive();
    Test_LegacySync_V1_Lifecycle();
    Test_SyncDeviceConcurrentTeardown();
}

/* ===================================================================== */
/* ================  Group 2: keyedmutexlife  ========================== */
/* ===================================================================== */

/*
 * NULL + bogus-handle contracts for the keyed-mutex entry points NOT covered by
 * d3dkmt_surface.c's keyedmutex group (which does Create/Create2/Open/Open2).
 */
static void Test_KeyedMutex_NullContracts(void)
{
    CHECK_NULL_REJECTED(PFND3DKMT_DESTROYKEYEDMUTEX, "D3DKMTDestroyKeyedMutex");
    CHECK_NULL_REJECTED(PFND3DKMT_ACQUIREKEYEDMUTEX, "D3DKMTAcquireKeyedMutex");
    CHECK_NULL_REJECTED(PFND3DKMT_ACQUIREKEYEDMUTEX2, "D3DKMTAcquireKeyedMutex2");
    CHECK_NULL_REJECTED(PFND3DKMT_RELEASEKEYEDMUTEX, "D3DKMTReleaseKeyedMutex");
    CHECK_NULL_REJECTED(PFND3DKMT_RELEASEKEYEDMUTEX2, "D3DKMTReleaseKeyedMutex2");
}

static void Test_KeyedMutex_BogusHandles(void)
{
    PFND3DKMT_DESTROYKEYEDMUTEX pDestroy;
    PFND3DKMT_ACQUIREKEYEDMUTEX pAcquire;
    PFND3DKMT_RELEASEKEYEDMUTEX pRelease;
    LARGE_INTEGER timeout;
    volatile NTSTATUS Status = STATUS_SUCCESS;
    volatile BOOL faulted = FALSE;

    pDestroy = (PFND3DKMT_DESTROYKEYEDMUTEX)LoadD3DKMTProc("D3DKMTDestroyKeyedMutex");
    pAcquire = (PFND3DKMT_ACQUIREKEYEDMUTEX)LoadD3DKMTProc("D3DKMTAcquireKeyedMutex");
    pRelease = (PFND3DKMT_RELEASEKEYEDMUTEX)LoadD3DKMTProc("D3DKMTReleaseKeyedMutex");

    /* relative 100 ms, so a bogus acquire can never block the test */
    timeout.QuadPart = -1000000LL;

    if (pDestroy)
    {
        D3DKMT_DESTROYKEYEDMUTEX d;
        memset(&d, 0, sizeof(d));
        d.hKeyedMutex = (D3DKMT_HANDLE)0xBADBABE1;
        SYNCEXT_TRY(faulted, Status = pDestroy(&d));
        ok(faulted || !NT_SUCCESS(Status),
           "DestroyKeyedMutex(bogus) should fail, got 0x%08lX\n", (long)Status);
    }
    if (pAcquire)
    {
        D3DKMT_ACQUIREKEYEDMUTEX a;
        memset(&a, 0, sizeof(a));
        a.hKeyedMutex = (D3DKMT_HANDLE)0xBADBABE2;
        a.Key = 0;
        a.pTimeout = &timeout;
        SYNCEXT_TRY(faulted, Status = pAcquire(&a));
        ok(faulted || !NT_SUCCESS(Status),
           "AcquireKeyedMutex(bogus) should fail, got 0x%08lX\n", (long)Status);
    }
    if (pRelease)
    {
        D3DKMT_RELEASEKEYEDMUTEX r;
        memset(&r, 0, sizeof(r));
        r.hKeyedMutex = (D3DKMT_HANDLE)0xBADBABE3;
        r.Key = 0;
        SYNCEXT_TRY(faulted, Status = pRelease(&r));
        ok(faulted || !NT_SUCCESS(Status),
           "ReleaseKeyedMutex(bogus) should fail, got 0x%08lX\n", (long)Status);
    }
}

/* Full v1 keyed-mutex lifecycle (skip-guarded). */
static void Test_KeyedMutex_V1_Lifecycle(void)
{
    PFND3DKMT_CREATEKEYEDMUTEX  pCreate;
    PFND3DKMT_DESTROYKEYEDMUTEX pDestroy;
    PFND3DKMT_ACQUIREKEYEDMUTEX pAcquire;
    PFND3DKMT_RELEASEKEYEDMUTEX pRelease;
    PFND3DKMT_OPENKEYEDMUTEX    pOpen;
    D3DKMT_CREATEKEYEDMUTEX  create;
    D3DKMT_DESTROYKEYEDMUTEX destroy;
    D3DKMT_ACQUIREKEYEDMUTEX acq;
    D3DKMT_RELEASEKEYEDMUTEX rel;
    LARGE_INTEGER timeout;
    D3DKMT_HANDLE hKM, hShared;
    volatile NTSTATUS Status = STATUS_SUCCESS;
    volatile BOOL faulted = FALSE;

    pCreate  = (PFND3DKMT_CREATEKEYEDMUTEX) LoadD3DKMTProc("D3DKMTCreateKeyedMutex");
    pDestroy = (PFND3DKMT_DESTROYKEYEDMUTEX)LoadD3DKMTProc("D3DKMTDestroyKeyedMutex");
    pAcquire = (PFND3DKMT_ACQUIREKEYEDMUTEX)LoadD3DKMTProc("D3DKMTAcquireKeyedMutex");
    pRelease = (PFND3DKMT_RELEASEKEYEDMUTEX)LoadD3DKMTProc("D3DKMTReleaseKeyedMutex");
    pOpen    = (PFND3DKMT_OPENKEYEDMUTEX)   LoadD3DKMTProc("D3DKMTOpenKeyedMutex");
    if (!pCreate || !pDestroy)
    {
        skip("D3DKMTCreateKeyedMutex/DestroyKeyedMutex not exported\n");
        return;
    }

    timeout.QuadPart = -1000000LL;  /* relative 100 ms safety bound */

    memset(&create, 0, sizeof(create));
    create.InitialValue = 0;
    SYNCEXT_TRY(faulted, Status = pCreate(&create));
    if (faulted || !NT_SUCCESS(Status))
    {
        skip("D3DKMTCreateKeyedMutex refused (0x%08lX%s)\n",
             (long)Status, faulted ? ", faulted" : "");
        return;
    }

    hKM = create.hKeyedMutex;
    hShared = create.hSharedHandle;
    ok(hKM != 0, "CreateKeyedMutex returned a zero handle\n");
    trace("CreateKeyedMutex: hKeyedMutex=0x%08lX hSharedHandle=0x%08lX\n",
          (unsigned long)hKM, (unsigned long)hShared);

    /* Acquire(0) -> Release(1) -> Acquire(1) -> Release(0).  Each immediately
     * satisfiable; the bounded timeout guards against any quirk. */
    if (pAcquire)
    {
        memset(&acq, 0, sizeof(acq));
        acq.hKeyedMutex = hKM;
        acq.Key = 0;
        acq.pTimeout = &timeout;
        SYNCEXT_TRY(faulted, Status = pAcquire(&acq));
        trace("AcquireKeyedMutex(Key=0) -> 0x%08lX%s (fence=%lu)\n",
              (long)Status, faulted ? " (faulted)" : "",
              (unsigned long)acq.FenceValue);
    }
    if (pRelease)
    {
        memset(&rel, 0, sizeof(rel));
        rel.hKeyedMutex = hKM;
        rel.Key = 1;
        SYNCEXT_TRY(faulted, Status = pRelease(&rel));
        trace("ReleaseKeyedMutex(Key=1) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");
    }
    if (pAcquire)
    {
        memset(&acq, 0, sizeof(acq));
        acq.hKeyedMutex = hKM;
        acq.Key = 1;
        acq.pTimeout = &timeout;
        SYNCEXT_TRY(faulted, Status = pAcquire(&acq));
        trace("AcquireKeyedMutex(Key=1) -> 0x%08lX%s (fence=%lu)\n",
              (long)Status, faulted ? " (faulted)" : "",
              (unsigned long)acq.FenceValue);
    }
    if (pRelease)
    {
        memset(&rel, 0, sizeof(rel));
        rel.hKeyedMutex = hKM;
        rel.Key = 0;
        SYNCEXT_TRY(faulted, Status = pRelease(&rel));
        trace("ReleaseKeyedMutex(Key=0) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");
    }

    /* Open the same mutex through its global shared handle. */
    if (pOpen && hShared)
    {
        D3DKMT_OPENKEYEDMUTEX open;
        memset(&open, 0, sizeof(open));
        open.hSharedHandle = hShared;
        SYNCEXT_TRY(faulted, Status = pOpen(&open));
        if (!faulted && NT_SUCCESS(Status))
        {
            trace("OpenKeyedMutex: hKeyedMutex=0x%08lX\n",
                  (unsigned long)open.hKeyedMutex);
            ok(open.hKeyedMutex != 0, "OpenKeyedMutex returned a zero handle\n");
            if (open.hKeyedMutex)
            {
                memset(&destroy, 0, sizeof(destroy));
                destroy.hKeyedMutex = open.hKeyedMutex;
                pDestroy(&destroy);
            }
        }
        else
        {
            trace("OpenKeyedMutex refused (0x%08lX%s)\n",
                  (long)Status, faulted ? ", faulted" : "");
        }
    }

    memset(&destroy, 0, sizeof(destroy));
    destroy.hKeyedMutex = hKM;
    Status = pDestroy(&destroy);
    ok(NT_SUCCESS(Status), "DestroyKeyedMutex failed (0x%08lX)\n", (long)Status);

    /* Double destroy of the same handle must fail. */
    SYNCEXT_TRY(faulted, Status = pDestroy(&destroy));
    ok(faulted || !NT_SUCCESS(Status),
       "Double DestroyKeyedMutex should fail, got 0x%08lX\n", (long)Status);
}

/* Full v2 keyed-mutex lifecycle (skip-guarded). */
static void Test_KeyedMutex_V2_Lifecycle(void)
{
    PFND3DKMT_CREATEKEYEDMUTEX2  pCreate;
    PFND3DKMT_DESTROYKEYEDMUTEX  pDestroy;
    PFND3DKMT_ACQUIREKEYEDMUTEX2 pAcquire;
    PFND3DKMT_RELEASEKEYEDMUTEX2 pRelease;
    D3DKMT_CREATEKEYEDMUTEX2  create;
    D3DKMT_DESTROYKEYEDMUTEX  destroy;
    D3DKMT_ACQUIREKEYEDMUTEX2 acq;
    D3DKMT_RELEASEKEYEDMUTEX2 rel;
    LARGE_INTEGER timeout;
    D3DKMT_HANDLE hKM;
    volatile NTSTATUS Status = STATUS_SUCCESS;
    volatile BOOL faulted = FALSE;

    pCreate  = (PFND3DKMT_CREATEKEYEDMUTEX2) LoadD3DKMTProc("D3DKMTCreateKeyedMutex2");
    pDestroy = (PFND3DKMT_DESTROYKEYEDMUTEX) LoadD3DKMTProc("D3DKMTDestroyKeyedMutex");
    pAcquire = (PFND3DKMT_ACQUIREKEYEDMUTEX2)LoadD3DKMTProc("D3DKMTAcquireKeyedMutex2");
    pRelease = (PFND3DKMT_RELEASEKEYEDMUTEX2)LoadD3DKMTProc("D3DKMTReleaseKeyedMutex2");
    if (!pCreate || !pDestroy)
    {
        skip("D3DKMTCreateKeyedMutex2/DestroyKeyedMutex not exported\n");
        return;
    }

    timeout.QuadPart = -1000000LL;

    memset(&create, 0, sizeof(create));
    create.InitialValue = 0;
    /* Flags.Value left 0 (legacy global sharing), no private runtime data. */
    SYNCEXT_TRY(faulted, Status = pCreate(&create));
    if (faulted || !NT_SUCCESS(Status))
    {
        skip("D3DKMTCreateKeyedMutex2 refused (0x%08lX%s)\n",
             (long)Status, faulted ? ", faulted" : "");
        return;
    }

    hKM = create.hKeyedMutex;
    ok(hKM != 0, "CreateKeyedMutex2 returned a zero handle\n");
    trace("CreateKeyedMutex2: hKeyedMutex=0x%08lX hSharedHandle=0x%08lX\n",
          (unsigned long)hKM, (unsigned long)create.hSharedHandle);

    if (pAcquire)
    {
        memset(&acq, 0, sizeof(acq));
        acq.hKeyedMutex = hKM;
        acq.Key = 0;
        acq.pTimeout = &timeout;
        SYNCEXT_TRY(faulted, Status = pAcquire(&acq));
        trace("AcquireKeyedMutex2(Key=0) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");
    }
    if (pRelease)
    {
        memset(&rel, 0, sizeof(rel));
        rel.hKeyedMutex = hKM;
        rel.Key = 1;
        SYNCEXT_TRY(faulted, Status = pRelease(&rel));
        trace("ReleaseKeyedMutex2(Key=1) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");
    }
    if (pAcquire)
    {
        memset(&acq, 0, sizeof(acq));
        acq.hKeyedMutex = hKM;
        acq.Key = 1;
        acq.pTimeout = &timeout;
        SYNCEXT_TRY(faulted, Status = pAcquire(&acq));
        trace("AcquireKeyedMutex2(Key=1) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");
    }
    if (pRelease)
    {
        memset(&rel, 0, sizeof(rel));
        rel.hKeyedMutex = hKM;
        rel.Key = 0;
        SYNCEXT_TRY(faulted, Status = pRelease(&rel));
        trace("ReleaseKeyedMutex2(Key=0) -> 0x%08lX%s\n",
              (long)Status, faulted ? " (faulted)" : "");
    }

    memset(&destroy, 0, sizeof(destroy));
    destroy.hKeyedMutex = hKM;
    Status = pDestroy(&destroy);
    ok(NT_SUCCESS(Status), "DestroyKeyedMutex (v2 object) failed (0x%08lX)\n",
       (long)Status);

    SYNCEXT_TRY(faulted, Status = pDestroy(&destroy));
    ok(faulted || !NT_SUCCESS(Status),
       "Double DestroyKeyedMutex (v2) should fail, got 0x%08lX\n", (long)Status);
}

START_TEST(keyedmutexlife)
{
    Test_KeyedMutex_NullContracts();
    Test_KeyedMutex_BogusHandles();
    Test_KeyedMutex_V1_Lifecycle();
    Test_KeyedMutex_V2_Lifecycle();
}
