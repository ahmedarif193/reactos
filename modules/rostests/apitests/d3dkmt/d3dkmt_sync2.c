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

START_TEST(sync2)
{
    Test_CreateSyncObject2_NullArg();
    Test_WaitForSyncObject2_NullArg();
    Test_SignalSyncObject2_NullArg();
    Test_WaitForSyncObjectFromCpu_NullArg();
    Test_SignalSyncObjectFromCpu_NullArg();
    Test_SignalSyncObjectFromGpu_NullArg();
    Test_MonitoredFence_Lifecycle();
    Test_MonitoredFence_CpuValuePage();
}
