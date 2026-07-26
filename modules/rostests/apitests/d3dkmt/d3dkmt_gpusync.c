/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU-side monitored fence wait/signal (roadmap 1.1 sync rows)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * The CPU-side halves of the monitored-fence contract have worked for a while:
 * a thread can wait for or signal a fence from the CPU.  The GPU-side halves
 * are how one engine's work is ordered against another's *without* a round trip
 * through the CPU, and they were the missing half:
 * D3DKMTSignalSynchronizationObjectFromGpu was exported but returned
 * STATUS_NOT_IMPLEMENTED, and WaitForSynchronizationObjectFromGpu and
 * SignalSynchronizationObjectFromGpu2 were not exported at all.
 *
 * What makes these different from the ...Object2 forms -- and the reason a
 * monitored fence needs them -- is that every object carries its *own* fence
 * value.  One call waits for fence A to reach 7 while fence B reaches 12.  The
 * Object2 forms apply a single value to the whole batch, so they cannot express
 * that at all; a caller forced through them either issues one call per fence,
 * losing the atomicity that made it a batch, or waits on the wrong value.
 *
 * That per-object array is what these tests are really pinning.
 */

#include "precomp.h"

static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU pfnWaitFromGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU pfnSignalFromGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 pfnSignalFromGpu2;

/* ------------------------------------------------------------------ *
 * The exports must exist at all -- this is the first build in which
 * two of the three do.
 * ------------------------------------------------------------------ */
static void Test_ExportsExist(void)
{
    ok(pfnWaitFromGpu != NULL,
       "D3DKMTWaitForSynchronizationObjectFromGpu is not exported -- a context can signal a "
       "fence from the GPU but never wait on one\n");
    ok(pfnSignalFromGpu != NULL, "D3DKMTSignalSynchronizationObjectFromGpu is not exported\n");
    ok(pfnSignalFromGpu2 != NULL, "D3DKMTSignalSynchronizationObjectFromGpu2 is not exported\n");
}

/* ------------------------------------------------------------------ *
 * Argument refusal.  Every one of these is a request the kernel must
 * not act on, and each has a distinct reason.
 * ------------------------------------------------------------------ */
static void Test_WaitRefusesMalformed(D3DKMT_HANDLE hContext)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait;
    D3DKMT_HANDLE Objects[2];
    UINT64 Values[2];

    if (pfnWaitFromGpu == NULL)
    {
        skip("no GPU-side wait to exercise\n");
        return;
    }

    Objects[0] = 0x1234;
    Objects[1] = 0x5678;
    Values[0] = 7;
    Values[1] = 12;

    ok_failed(pfnWaitFromGpu(NULL), "NULL request accepted\n");

    /* A zero count names nothing to wait for; acting on it would let a caller
     * believe it had ordered work behind a fence when it had not. */
    memset(&Wait, 0, sizeof(Wait));
    Wait.hContext = hContext;
    Wait.ObjectCount = 0;
    Wait.ObjectHandleArray = Objects;
    Wait.MonitoredFenceValueArray = Values;
    ok_failed(pfnWaitFromGpu(&Wait), "zero object count accepted\n");

    /* Past the batch limit the kernel has no room to capture the arrays. */
    memset(&Wait, 0, sizeof(Wait));
    Wait.hContext = hContext;
    Wait.ObjectCount = D3DDDI_MAX_OBJECT_WAITED_ON + 1;
    Wait.ObjectHandleArray = Objects;
    Wait.MonitoredFenceValueArray = Values;
    ok_failed(pfnWaitFromGpu(&Wait), "over-long object count accepted\n");

    /* The value array is not optional for these entry points: it is the whole
     * reason they exist, and a NULL one would be read as address zero. */
    memset(&Wait, 0, sizeof(Wait));
    Wait.hContext = hContext;
    Wait.ObjectCount = 2;
    Wait.ObjectHandleArray = Objects;
    Wait.MonitoredFenceValueArray = NULL;
    ok_failed(pfnWaitFromGpu(&Wait), "NULL fence value array accepted\n");

    memset(&Wait, 0, sizeof(Wait));
    Wait.hContext = hContext;
    Wait.ObjectCount = 2;
    Wait.ObjectHandleArray = NULL;
    Wait.MonitoredFenceValueArray = Values;
    ok_failed(pfnWaitFromGpu(&Wait), "NULL object array accepted\n");

    /* A handle that is not a context cannot be scheduled on. */
    memset(&Wait, 0, sizeof(Wait));
    Wait.hContext = 0xBAD0CAFE;
    Wait.ObjectCount = 2;
    Wait.ObjectHandleArray = Objects;
    Wait.MonitoredFenceValueArray = Values;
    ok_failed(pfnWaitFromGpu(&Wait), "wait on a bogus context accepted\n");
}

static void Test_SignalRefusesMalformed(D3DKMT_HANDLE hContext)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU Signal;
    D3DKMT_HANDLE Objects[2];
    UINT64 Values[2];

    if (pfnSignalFromGpu == NULL)
    {
        skip("no GPU-side signal to exercise\n");
        return;
    }

    Objects[0] = 0x1234;
    Objects[1] = 0x5678;
    Values[0] = 7;
    Values[1] = 12;

    ok_failed(pfnSignalFromGpu(NULL), "NULL request accepted\n");

    memset(&Signal, 0, sizeof(Signal));
    Signal.hContext = hContext;
    Signal.ObjectCount = 0;
    Signal.ObjectHandleArray = Objects;
    Signal.MonitoredFenceValueArray = Values;
    ok_failed(pfnSignalFromGpu(&Signal), "zero object count accepted\n");

    memset(&Signal, 0, sizeof(Signal));
    Signal.hContext = hContext;
    Signal.ObjectCount = 2;
    Signal.ObjectHandleArray = Objects;
    Signal.MonitoredFenceValueArray = NULL;
    ok_failed(pfnSignalFromGpu(&Signal), "NULL fence value array accepted\n");

    memset(&Signal, 0, sizeof(Signal));
    Signal.hContext = 0xBAD0CAFE;
    Signal.ObjectCount = 2;
    Signal.ObjectHandleArray = Objects;
    Signal.MonitoredFenceValueArray = Values;
    ok_failed(pfnSignalFromGpu(&Signal), "signal on a bogus context accepted\n");
}

/*
 * The ...Gpu2 form carries no hContext of its own: the contexts it signals on
 * are exactly the broadcast array.  That makes an empty array a malformed
 * request rather than a harmless no-op, which is the interesting case here.
 */
static void Test_Signal2BroadcastContract(D3DKMT_HANDLE hContext)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 Signal;
    D3DKMT_HANDLE Objects[2];
    D3DKMT_HANDLE Contexts[1];
    UINT64 Values[2];

    if (pfnSignalFromGpu2 == NULL)
    {
        skip("no GPU-side signal2 to exercise\n");
        return;
    }

    Objects[0] = 0x1234;
    Objects[1] = 0x5678;
    Values[0] = 7;
    Values[1] = 12;
    Contexts[0] = hContext;

    ok_failed(pfnSignalFromGpu2(NULL), "NULL request accepted\n");

    memset(&Signal, 0, sizeof(Signal));
    Signal.ObjectCount = 2;
    Signal.ObjectHandleArray = Objects;
    Signal.MonitoredFenceValueArray = Values;
    Signal.BroadcastContextCount = 0;
    Signal.BroadcastContextArray = Contexts;
    ok_failed(pfnSignalFromGpu2(&Signal),
              "empty broadcast array accepted -- this form has no other context to signal on\n");

    memset(&Signal, 0, sizeof(Signal));
    Signal.ObjectCount = 2;
    Signal.ObjectHandleArray = Objects;
    Signal.MonitoredFenceValueArray = Values;
    Signal.BroadcastContextCount = 1;
    Signal.BroadcastContextArray = NULL;
    ok_failed(pfnSignalFromGpu2(&Signal), "NULL broadcast array accepted\n");

    memset(&Signal, 0, sizeof(Signal));
    Signal.ObjectCount = 2;
    Signal.ObjectHandleArray = Objects;
    Signal.MonitoredFenceValueArray = Values;
    Signal.BroadcastContextCount = D3DDDI_MAX_BROADCAST_CONTEXT + 1;
    Signal.BroadcastContextArray = Contexts;
    ok_failed(pfnSignalFromGpu2(&Signal), "over-long broadcast count accepted\n");

    /*
     * Signalling a CPU event and signalling fences are alternatives -- the value
     * union holds the event handle in that case, so it cannot also hold fence
     * values.  Carrying objects as well is contradictory, and half-honouring it
     * would signal the event and silently drop the fences.
     */
    memset(&Signal, 0, sizeof(Signal));
    Signal.Flags.EnqueueCpuEvent = 1;
    Signal.ObjectCount = 2;
    Signal.ObjectHandleArray = Objects;
    Signal.BroadcastContextCount = 1;
    Signal.BroadcastContextArray = Contexts;
    ok_failed(pfnSignalFromGpu2(&Signal),
              "EnqueueCpuEvent accepted together with fence objects\n");
}

/* ------------------------------------------------------------------ *
 * The point of these entry points: distinct values per object.
 * ------------------------------------------------------------------ */
static void Test_PerObjectValuesReachTheKernel(D3DKMT_HANDLE hDevice, D3DKMT_HANDLE hContext)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateA, CreateB;
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU Signal;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pfnCreate2;
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pfnDestroy;
    D3DKMT_HANDLE Objects[2];
    UINT64 Values[2];
    NTSTATUS Status;

    pfnCreate2 = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    pfnDestroy = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    if (pfnCreate2 == NULL || pfnDestroy == NULL || pfnSignalFromGpu == NULL)
    {
        skip("monitored fences or the GPU-side signal are unavailable\n");
        return;
    }

    memset(&CreateA, 0, sizeof(CreateA));
    CreateA.hDevice = hDevice;
    CreateA.Info.Type = D3DDDI_MONITORED_FENCE;
    CreateA.Info.MonitoredFence.InitialFenceValue = 0;
    Status = pfnCreate2(&CreateA);
    if (!NT_SUCCESS(Status))
    {
        skip("monitored fence A not created (0x%08lX)\n", (long)Status);
        return;
    }

    memset(&CreateB, 0, sizeof(CreateB));
    CreateB.hDevice = hDevice;
    CreateB.Info.Type = D3DDDI_MONITORED_FENCE;
    CreateB.Info.MonitoredFence.InitialFenceValue = 0;
    Status = pfnCreate2(&CreateB);
    if (!NT_SUCCESS(Status))
    {
        skip("monitored fence B not created (0x%08lX)\n", (long)Status);
        memset(&Destroy, 0, sizeof(Destroy));
        Destroy.hSyncObject = CreateA.hSyncObject;
        pfnDestroy(&Destroy);
        return;
    }

    /*
     * Two fences, two different target values, one call.  This is the case the
     * ...Object2 forms cannot express, so if it is accepted here the per-object
     * array reached the scheduler intact.
     */
    Objects[0] = CreateA.hSyncObject;
    Objects[1] = CreateB.hSyncObject;
    Values[0] = 7;
    Values[1] = 12;

    memset(&Signal, 0, sizeof(Signal));
    Signal.hContext = hContext;
    Signal.ObjectCount = 2;
    Signal.ObjectHandleArray = Objects;
    Signal.MonitoredFenceValueArray = Values;
    Status = pfnSignalFromGpu(&Signal);
    ok(NT_SUCCESS(Status),
       "signalling two fences to different values in one call failed 0x%08lX -- this is exactly "
       "what the per-object value array exists for\n", (long)Status);

    /* The caller's array must not be consulted again after the call returns:
     * it is captured, so rewriting it cannot change what was queued. */
    Values[0] = 0xDEAD;
    Values[1] = 0xBEEF;

    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hSyncObject = CreateB.hSyncObject;
    pfnDestroy(&Destroy);
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hSyncObject = CreateA.hSyncObject;
    pfnDestroy(&Destroy);
}

START_TEST(gpusync)
{
    D3DKMT_HANDLE hAdapter, hDevice, hContext = 0;
    PFND3DKMT_CREATECONTEXT pfnCreateContext;
    PFND3DKMT_DESTROYCONTEXT pfnDestroyContext;
    D3DKMT_CREATECONTEXT cc;

    pfnWaitFromGpu = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU)
        LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromGpu");
    pfnSignalFromGpu = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU)
        LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromGpu");
    pfnSignalFromGpu2 = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2)
        LoadD3DKMTProc("D3DKMTSignalSynchronizationObjectFromGpu2");

    Test_ExportsExist();

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("No device\n");
        CloseAdapter(hAdapter);
        return;
    }

    pfnCreateContext = (PFND3DKMT_CREATECONTEXT)LoadD3DKMTProc("D3DKMTCreateContext");
    pfnDestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    if (pfnCreateContext != NULL)
    {
        memset(&cc, 0, sizeof(cc));
        cc.hDevice = hDevice;
        cc.NodeOrdinal = 0;
        cc.EngineAffinity = 0;
        if (NT_SUCCESS(pfnCreateContext(&cc)))
            hContext = cc.hContext;
    }

    Test_WaitRefusesMalformed(hContext);
    Test_SignalRefusesMalformed(hContext);
    Test_Signal2BroadcastContract(hContext);
    if (hContext)
        Test_PerObjectValuesReachTheKernel(hDevice, hContext);
    else
        skip("no context: the GPU-side operations are all scheduled on one\n");

    if (hContext && pfnDestroyContext != NULL)
    {
        D3DKMT_DESTROYCONTEXT dc;

        memset(&dc, 0, sizeof(dc));
        dc.hContext = hContext;
        pfnDestroyContext(&dc);
    }
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* EOF */
