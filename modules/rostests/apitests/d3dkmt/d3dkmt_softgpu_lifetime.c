/*
 * PROJECT: ReactOS D3DKMT API Tests
 * LICENSE: GPL-2.0-or-later
 * PURPOSE: GPUVA allocation lifetime across devices in one process.
 *
 * Integration test using the unmodified software GPU command ABI.
 * A command allocation on device A indirectly reads a second A allocation
 * from a context on A or B, after an explicitly controlled GPU fence gate. */
#include "precomp.h"
#include <drivers/directx/softgpu_2d_shared.h>
static PFND3DKMT_QUERYADAPTERINFO QueryAdapter;
static PFND3DKMT_CREATEALLOCATION CreateAlloc;
static PFND3DKMT_DESTROYALLOCATION DestroyAlloc;
static PFND3DKMT_CREATECONTEXTVIRTUAL CreateContext;
static PFND3DKMT_DESTROYCONTEXT DestroyContext;
static PFND3DKMT_SUBMITCOMMAND Submit;
static PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
static PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU WaitCpu;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU WaitGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU SignalGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU SignalCpu;
static PFND3DKMT_GETDEVICESTATE GetState;
static PFND3DKMT_CREATEPAGINGQUEUE CreateQueue;
static PFND3DKMT_DESTROYPAGINGQUEUE DestroyQueue;
static PFND3DKMT_MAPGPUVIRTUALADDRESS Map;
static PFND3DKMT_FREEGPUVIRTUALADDRESS FreeMap;
static PFND3DKMT_LOCK2 Lock;
static PFND3DKMT_UNLOCK2 Unlock;
struct destroy_thread
{
    D3DKMT_DESTROYALLOCATION args;
    HANDLE began;
    NTSTATUS status;
};
static DWORD WINAPI destroy_thread(void *p)
{
    struct destroy_thread *d = p;
    SetEvent(d->began);
    d->status = DestroyAlloc(&d->args);
    return 0;
}
#define CHECK(call)                                                                                \
    do                                                                                             \
    {                                                                                              \
        status = (call);                                                                           \
        ok_succeeded(status, "%s: %08lx\n", #call, status);                                        \
        if (!NT_SUCCESS(status))                                                                   \
            goto cleanup;                                                                          \
    } while (0)
static void round_test(unsigned cross)
{
    D3DKMT_HANDLE adapter = 0, dev[2] = {0}, alloc[2] = {0}, context = 0, fence[2] = {0};
    D3DKMT_CREATEPAGINGQUEUE queue = {0};
    D3DGPU_VIRTUAL_ADDRESS va[2] = {0};
    D3DKMT_CREATECONTEXTVIRTUAL c = {0};
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU wg = {0};
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU sg = {0};
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU sc = {0};
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wc = {0};
    D3DKMT_SUBMITCOMMAND cmd = {0};
    D3DKMT_GETDEVICESTATE state = {0};
    struct destroy_thread d = {0};
    HANDLE thread = NULL, event = NULL;
    UINT64 one = 1;
    volatile const UINT64 *completed = NULL;
    NTSTATUS status;
    unsigned i;
    DWORD result;
    BOOL gated = FALSE;
    trace("Allocation lifetime round cross=%u begin\n", cross);
    adapter = OpenAdapterFromDisplay1();
    ok(adapter != 0, "No software adapter\n");
    if (!adapter)
        return;
    /* These are software-GPU commands, never submit them to a vendor GPU. */
    {
        D3DKMT_UMDFILENAMEINFO info = {0};
        D3DKMT_QUERYADAPTERINFO query = {0};
        const WCHAR *base, *cursor;
        info.Version = KMTUMDVERSION_DX9;
        query.hAdapter = adapter;
        query.Type = KMTQAITYPE_UMDRIVERNAME;
        query.pPrivateDriverData = &info;
        query.PrivateDriverDataSize = sizeof(info);
        status = QueryAdapter(&query);
        info.UmdFileName[ARRAY_SIZE(info.UmdFileName) - 1] = 0;
        base = info.UmdFileName;
        for (cursor = base; *cursor; ++cursor)
            if (*cursor == L'\\' || *cursor == L'/')
                base = cursor + 1;
        if (!NT_SUCCESS(status) || lstrcmpiW(base, L"softgpuum.dll"))
        {
            skip("This test requires the ReactOS software GPU\n");
            goto cleanup;
        }
    }
    for (i = 0; i < 2; i++)
    {
        dev[i] = CreateTestDevice(adapter);
        ok(dev[i] != 0, "Create device %u\n", i);
        if (!dev[i])
            goto cleanup;
    }
    queue.hDevice = dev[0];
    queue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    CHECK(CreateQueue(&queue));
    for (i = 0; i < 2; i++)
    {
        D3DDDI_ALLOCATIONINFO ai = {0};
        D3DKMT_CREATEALLOCATION ca = {0};
        D3DDDI_MAPGPUVIRTUALADDRESS map = {0};
        D3DKMT_LOCK2 lock = {0};
        D3DKMT_UNLOCK2 unlock = {0};
        ca.hDevice = dev[0];
        ca.NumAllocations = 1;
        ca.pAllocationInfo = &ai;
        CHECK(CreateAlloc(&ca));
        alloc[i] = ai.hAllocation;
        map.hPagingQueue = queue.hPagingQueue;
        map.hAllocation = alloc[i];
        map.SizeInPages = 1;
        map.Protection.Write = 1;
        map.Protection.Execute = (i == 0);
        CHECK(Map(&map));
        va[i] = map.VirtualAddress;
        lock.hDevice = dev[0];
        lock.hAllocation = alloc[i];
        CHECK(Lock(&lock));
        memset(lock.pData, 0, 4096);
        unlock.hDevice = dev[0];
        unlock.hAllocation = alloc[i];
        CHECK(Unlock(&unlock));
    }
    {
        D3DKMT_LOCK2 lock = {0};
        D3DKMT_UNLOCK2 unlock = {0};
        SOFTGPU_CMD *read;
        lock.hDevice = dev[0];
        lock.hAllocation = alloc[0];
        CHECK(Lock(&lock));
        read = lock.pData;
        read->Magic = SOFTGPU_CMD_MAGIC;
        read->Size = sizeof(*read);
        read->Op = SOFTGPU_CMD_OP_WAIT_FENCE;
        read->FenceGpuVa = va[1];
        read->FenceValue = 0;
        unlock.hDevice = dev[0];
        unlock.hAllocation = alloc[0];
        CHECK(Unlock(&unlock));
    }
    c.hDevice = dev[cross];
    c.EngineAffinity = 1;
    CHECK(CreateContext(&c));
    context = c.hContext;
    for (i = 0; i < 2; i++)
    {
        D3DKMT_CREATESYNCHRONIZATIONOBJECT2 s = {0};
        s.hDevice = dev[cross];
        s.Info.Type = D3DDDI_MONITORED_FENCE;
        s.Info.MonitoredFence.EngineAffinity = 1;
        CHECK(CreateSync(&s));
        fence[i] = s.hSyncObject;
        if (i == 1)
            completed = s.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    }
    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    d.began = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(event && d.began, "Create events\n");
    if (!event || !d.began)
        goto cleanup;
    /* A positive control actually reads the indirect allocation first. */
    cmd.Commands = va[0];
    cmd.CommandLength = sizeof(SOFTGPU_CMD);
    cmd.BroadcastContextCount = 1;
    cmd.BroadcastContext[0] = context;
    CHECK(Submit(&cmd));
    sg.hContext = context;
    sg.ObjectCount = 1;
    sg.ObjectHandleArray = &fence[1];
    sg.MonitoredFenceValueArray = &one;
    CHECK(SignalGpu(&sg));
    wc.hDevice = dev[cross];
    wc.ObjectCount = 1;
    wc.ObjectHandleArray = &fence[1];
    wc.FenceValueArray = &one;
    wc.hAsyncEvent = event;
    CHECK(WaitCpu(&wc));
    result = WaitForSingleObject(event, 3000);
    ok(result == WAIT_OBJECT_0 && completed && *completed == 1,
       "Control indirect read failed wait=%lu fence=%I64u\n", result, completed ? *completed : 0);
    if (result != WAIT_OBJECT_0 || !completed || *completed != 1)
        goto cleanup;
    ResetEvent(event);
    wg.hContext = context;
    wg.ObjectCount = 1;
    wg.ObjectHandleArray = &fence[0];
    wg.MonitoredFenceValueArray = &one;
    CHECK(WaitGpu(&wg));
    gated = TRUE;
    CHECK(Submit(&cmd));
    one = 2;
    CHECK(SignalGpu(&sg));
    CHECK(WaitCpu(&wc));
    ok(WaitForSingleObject(event, 0) == WAIT_TIMEOUT, "Gate did not hold queued work\n");
    d.args.hDevice = dev[0];
    d.args.phAllocationList = &alloc[1];
    d.args.AllocationCount = 1;
    thread = CreateThread(NULL, 0, destroy_thread, &d, 0, NULL);
    ok(thread != NULL, "Create destruction thread\n");
    if (!thread)
        goto cleanup;
    result = WaitForSingleObject(d.began, 3000);
    ok(result == WAIT_OBJECT_0, "Destroy thread not started\n");
    result = WaitForSingleObject(thread, 400);
    trace("Allocation lifetime before gate release cross=%u destroy_wait=%lu status=%08lx\n", cross,
          result, result == WAIT_OBJECT_0 ? d.status : STATUS_PENDING);
    one = 1;
    sc.hDevice = dev[cross];
    sc.ObjectCount = 1;
    sc.ObjectHandleArray = &fence[0];
    sc.FenceValueArray = &one;
    CHECK(SignalCpu(&sc));
    gated = FALSE;
    result = WaitForSingleObject(event, 3000);
    ok(result == WAIT_OBJECT_0, "Queued indirect read terminal wait=%lu\n", result);
    state.hDevice = dev[cross];
    state.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    CHECK(GetState(&state));
    ok(state.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE,
       "Queued reader faulted after destroy: state=%u\n", state.ExecutionState);
    ok(completed && *completed == 2, "Queued reader never completed: fence=%I64u\n",
       completed ? *completed : 0);
cleanup:
    if (gated)
    {
        one = 1;
        sc.hDevice = dev[cross];
        sc.ObjectCount = 1;
        sc.ObjectHandleArray = &fence[0];
        sc.FenceValueArray = &one;
        status = SignalCpu(&sc);
        ok_succeeded(status, "Release cleanup gate: %08lx\n", status);
    }
    if (thread)
    {
        result = WaitForSingleObject(thread, 10000);
        ok(result == WAIT_OBJECT_0, "Destroy did not terminate: %lu\n", result);
        if (result != WAIT_OBJECT_0)
        {
            trace("Allocation destruction has not terminated\n");
            WaitForSingleObject(thread, INFINITE);
        }
        ok_succeeded(d.status, "Destroy returned %08lx\n", d.status);
        if (NT_SUCCESS(d.status))
            alloc[1] = 0;
        CloseHandle(thread);
    }
    if (context)
    {
        D3DKMT_DESTROYCONTEXT x = {0};
        x.hContext = context;
        status = DestroyContext(&x);
        ok_succeeded(status, "Cleanup DestroyContext: %08lx\n", status);
    }
    for (i = 0; i < 2; i++)
    {
        if (fence[i])
        {
            D3DKMT_DESTROYSYNCHRONIZATIONOBJECT x = {0};
            x.hSyncObject = fence[i];
            status = DestroySync(&x);
            ok_succeeded(status, "Cleanup DestroySync: %08lx\n", status);
        }
        if (va[i])
        {
            D3DKMT_FREEGPUVIRTUALADDRESS x = {0};
            x.hAdapter = adapter;
            x.BaseAddress = va[i];
            x.Size = 4096;
            status = FreeMap(&x);
            ok_succeeded(status, "Cleanup FreeMap: %08lx\n", status);
        }
        if (alloc[i])
        {
            D3DKMT_DESTROYALLOCATION x = {0};
            x.hDevice = dev[0];
            x.phAllocationList = &alloc[i];
            x.AllocationCount = 1;
            status = DestroyAlloc(&x);
            ok_succeeded(status, "Cleanup DestroyAlloc: %08lx\n", status);
        }
    }
    if (queue.hPagingQueue)
    {
        D3DDDI_DESTROYPAGINGQUEUE x = {0};
        x.hPagingQueue = queue.hPagingQueue;
        status = DestroyQueue(&x);
        ok_succeeded(status, "Cleanup DestroyQueue: %08lx\n", status);
    }
    if (event)
        CloseHandle(event);
    if (d.began)
        CloseHandle(d.began);
    for (i = 0; i < 2; i++)
        if (dev[i])
            DestroyTestDevice(dev[i]);
    if (adapter)
        CloseAdapter(adapter);
    trace("Allocation lifetime round cross=%u end\n", cross);
}
START_TEST(softgpu_lifetime)
{
#define LOAD(var, type, name)                                                                      \
    var = (type)LoadD3DKMTProc(name);                                                              \
    if (!var)                                                                                      \
    {                                                                                              \
        ok(0, "Missing %s\n", name);                                                               \
        return;                                                                                    \
    }
    LOAD(QueryAdapter, PFND3DKMT_QUERYADAPTERINFO, "D3DKMTQueryAdapterInfo");
    LOAD(CreateAlloc, PFND3DKMT_CREATEALLOCATION, "D3DKMTCreateAllocation");
    LOAD(DestroyAlloc, PFND3DKMT_DESTROYALLOCATION, "D3DKMTDestroyAllocation");
    LOAD(CreateContext, PFND3DKMT_CREATECONTEXTVIRTUAL, "D3DKMTCreateContextVirtual");
    LOAD(DestroyContext, PFND3DKMT_DESTROYCONTEXT, "D3DKMTDestroyContext");
    LOAD(Submit, PFND3DKMT_SUBMITCOMMAND, "D3DKMTSubmitCommand");
    LOAD(CreateSync, PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2, "D3DKMTCreateSynchronizationObject2");
    LOAD(DestroySync, PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT, "D3DKMTDestroySynchronizationObject");
    LOAD(WaitCpu, PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU,
         "D3DKMTWaitForSynchronizationObjectFromCpu");
    LOAD(WaitGpu, PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU,
         "D3DKMTWaitForSynchronizationObjectFromGpu");
    LOAD(SignalGpu, PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU,
         "D3DKMTSignalSynchronizationObjectFromGpu");
    LOAD(SignalCpu, PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU,
         "D3DKMTSignalSynchronizationObjectFromCpu");
    LOAD(GetState, PFND3DKMT_GETDEVICESTATE, "D3DKMTGetDeviceState");
    LOAD(CreateQueue, PFND3DKMT_CREATEPAGINGQUEUE, "D3DKMTCreatePagingQueue");
    LOAD(DestroyQueue, PFND3DKMT_DESTROYPAGINGQUEUE, "D3DKMTDestroyPagingQueue");
    LOAD(Map, PFND3DKMT_MAPGPUVIRTUALADDRESS, "D3DKMTMapGpuVirtualAddress");
    LOAD(FreeMap, PFND3DKMT_FREEGPUVIRTUALADDRESS, "D3DKMTFreeGpuVirtualAddress");
    LOAD(Lock, PFND3DKMT_LOCK2, "D3DKMTLock2");
    LOAD(Unlock, PFND3DKMT_UNLOCK2, "D3DKMTUnlock2");
    round_test(0);
    round_test(1);
}
