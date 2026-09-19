/* SPDX-License-Identifier: GPL-2.0-or-later
 * Cancel a CPU-event broadcast behind unresolved work without a GPU hang. */
#include "precomp.h"

START_TEST(eventcancel)
{
    PFND3DKMT_CREATECONTEXTVIRTUAL CreateContext = (PFND3DKMT_CREATECONTEXTVIRTUAL)LoadD3DKMTProc("D3DKMTCreateContextVirtual");
    PFND3DKMT_DESTROYCONTEXT DestroyContext = (PFND3DKMT_DESTROYCONTEXT)LoadD3DKMTProc("D3DKMTDestroyContext");
    PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSync = (PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTCreateSynchronizationObject2");
    PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync = (PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT)LoadD3DKMTProc("D3DKMTDestroySynchronizationObject");
    PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU WaitGpu = (PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU)LoadD3DKMTProc("D3DKMTWaitForSynchronizationObjectFromGpu");
    PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 Signal = (PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECT2)LoadD3DKMTProc("D3DKMTSignalSynchronizationObject2");
    D3DKMT_HANDLE Adapter, Device, Contexts[2], Fence;
    ULONG Manual, Index;
    UINT64 One = 1;
    NTSTATUS Status;

    if (!CreateContext || !DestroyContext || !CreateSync || !DestroySync || !WaitGpu || !Signal)
    {
        skip("WDDM virtual contexts and synchronization are required\n");
        return;
    }
    Adapter = OpenAdapterFromDisplay1();
    if (!Adapter) { skip("No display adapter\n"); return; }
    Device = CreateTestDevice(Adapter);
    if (!Device) { CloseAdapter(Adapter); return; }
    for (Manual = 0; Manual != 2; ++Manual)
    {
        HANDLE Event = CreateEventW(NULL, Manual, FALSE, NULL);
        D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Sync = {0};
        D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait = {0};
        D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 Notify = {0};
        BOOL Admitted = FALSE;
        memset(Contexts, 0, sizeof(Contexts));
        Fence = 0;
        ok(Event != NULL, "Create event: %lu\n", GetLastError());
        if (!Event) break;
        for (Index = 0; Index < RTL_NUMBER_OF(Contexts); ++Index)
        {
            D3DKMT_CREATECONTEXTVIRTUAL Context = {0};
            Context.hDevice = Device;
            Context.EngineAffinity = 1;
            Context.Flags.NullRendering = 1;
            Status = CreateContext(&Context);
            ok_succeeded(Status, "Create context %lu: %#lx\n", Index, Status);
            if (!NT_SUCCESS(Status)) goto Cleanup;
            Contexts[Index] = Context.hContext;
        }
        Sync.hDevice = Device;
        Sync.Info.Type = D3DDDI_MONITORED_FENCE;
        Sync.Info.MonitoredFence.EngineAffinity = 1;
        Status = CreateSync(&Sync);
        ok_succeeded(Status, "Create gate: %#lx\n", Status);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Fence = Sync.hSyncObject;
        Wait.hContext = Contexts[0];
        Wait.ObjectCount = 1;
        Wait.ObjectHandleArray = &Fence;
        Wait.MonitoredFenceValueArray = &One;
        Status = WaitGpu(&Wait);
        ok_succeeded(Status, "Queue unresolved gate: %#lx\n", Status);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Notify.hContext = Contexts[0];
        Notify.BroadcastContextCount = 1;
        Notify.BroadcastContext[0] = Contexts[1];
        Notify.Flags.EnqueueCpuEvent = 1;
        Notify.CpuEventHandle = Event;
        Status = Signal(&Notify);
        ok_succeeded(Status, "Queue broadcast notification: %#lx\n", Status);
        Admitted = NT_SUCCESS(Status);
        if (Admitted)
            ok(WaitForSingleObject(Event, 0) == WAIT_TIMEOUT,
               "Notification passed an unresolved dependency\n");
Cleanup:
        for (Index = 0; Index < RTL_NUMBER_OF(Contexts); ++Index)
        {
            D3DKMT_DESTROYCONTEXT Destroy = {Contexts[Index]};
            if (Contexts[Index])
                ok_succeeded(DestroyContext(&Destroy), "Cancel context %lu\n", Index);
        }
        if (Admitted)
        {
            ok(WaitForSingleObject(Event, 2000) == WAIT_OBJECT_0,
               "Cancelled broadcast did not release notification\n");
            ok(WaitForSingleObject(Event, 0) == (Manual ? WAIT_OBJECT_0 : WAIT_TIMEOUT),
               "Cancelled broadcast changed event reset semantics\n");
            ok(Sync.Info.MonitoredFence.FenceValueCPUVirtualAddress &&
               *(volatile UINT64 *)Sync.Info.MonitoredFence.FenceValueCPUVirtualAddress == 0,
               "Cancellation incorrectly signalled the unresolved fence\n");
        }
        if (Fence)
        {
            D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {Fence};
            ok_succeeded(DestroySync(&Destroy), "Destroy gate\n");
        }
        CloseHandle(Event);
    }
    DestroyTestDevice(Device);
    CloseAdapter(Adapter);
}
