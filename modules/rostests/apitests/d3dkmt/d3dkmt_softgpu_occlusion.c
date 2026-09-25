/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Window present occlusion must not fault the rendering device.
 *
 * Use the software miniport's allocation ABI to exercise the real KMT,
 * win32k visibility, and dxgkrnl present paths. A pixel copy after every
 * rejected present checks continued execution, not just a reported state.
 */
#include "precomp.h"
#include <winuser.h>
#include <drivers/directx/softgpu_2d_shared.h>

static PFND3DKMT_CREATEALLOCATION CreateAllocation;
static PFND3DKMT_DESTROYALLOCATION DestroyAllocation;
static PFND3DKMT_PRESENT Present;
static PFND3DKMT_GETDEVICESTATE GetDeviceState;
static PFND3DKMT_LOCK2 Lock;
static PFND3DKMT_UNLOCK2 Unlock;
static PFN_D3DKMTWaitForIdle WaitForIdle;

#define CHECK(call) do { \
    status = (call); \
    ok(NT_SUCCESS(status), "%s: %#lx\n", #call, (long)status); \
    if (!NT_SUCCESS(status)) goto cleanup; \
} while (0)

static D3DKMT_HANDLE create_surface(D3DKMT_HANDLE device)
{
    SOFTGPU_ALLOCATION_PRIVATE_DATA data = {0};
    D3DDDI_ALLOCATIONINFO info = {0};
    D3DKMT_CREATEALLOCATION create = {0};
    NTSTATUS status;

    data.Width = data.Height = data.StorageHeight = 16;
    data.BitsPerPixel = 32;
    data.Magic = SOFTGPU_ALLOCATION_PRIVATE_MAGIC;
    data.Version = SOFTGPU_ALLOCATION_PRIVATE_VERSION;
    data.Pitch = data.PlanePitches[0] = 64;
    data.Format = D3DDDIFMT_X8R8G8B8;
    data.PlaneCount = 1;
    info.pPrivateDriverData = &data;
    info.PrivateDriverDataSize = sizeof(data);
    create.hDevice = device;
    create.NumAllocations = 1;
    create.pAllocationInfo = &info;
    status = CreateAllocation(&create);
    ok(NT_SUCCESS(status), "Create surface: %#lx\n", (long)status);
    return NT_SUCCESS(status) ? info.hAllocation : 0;
}

static void check_copy(D3DKMT_HANDLE device, D3DKMT_HANDLE context,
        D3DKMT_HANDLE source, D3DKMT_HANDLE destination, unsigned round)
{
    D3DKMT_LOCK2 lock = {0};
    D3DKMT_UNLOCK2 unlock = {0};
    D3DKMT_PRESENT present = {0};
    D3DKMT_WAITFORIDLE wait = {0};
    D3DKMT_GETDEVICESTATE state = {0};
    NTSTATUS status;
    unsigned i, bad = 0;
    DWORD *pixels;

    lock.hDevice = unlock.hDevice = device;
    lock.hAllocation = unlock.hAllocation = source;
    CHECK(Lock(&lock));
    pixels = lock.pData;
    for (i = 0; i < 256; ++i)
        pixels[i] = 0x00135700 ^ (round << 12) ^ i;
    CHECK(Unlock(&unlock));
    lock.hAllocation = unlock.hAllocation = destination;
    CHECK(Lock(&lock));
    memset(lock.pData, 0, 1024);
    CHECK(Unlock(&unlock));

    present.hContext = context;
    present.hSource = source;
    present.hDestination = destination;
    present.Flags.Blt = 1;
    present.Flags.SrcRectValid = present.Flags.DstRectValid = 1;
    present.SrcRect.right = present.SrcRect.bottom = 16;
    present.DstRect = present.SrcRect;
    CHECK(Present(&present));
    wait.hDevice = device;
    CHECK(WaitForIdle(&wait));
    CHECK(Lock(&lock));
    pixels = lock.pData;
    for (i = 0; i < 256; ++i)
        if (pixels[i] != (0x00135700 ^ (round << 12) ^ i))
            ++bad;
    ok(!bad, "Round %u: %u incorrect destination pixels\n", round, bad);
    CHECK(Unlock(&unlock));

cleanup:
    state.hDevice = device;
    state.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    status = GetDeviceState(&state);
    ok(NT_SUCCESS(status), "Round %u: query state %#lx\n", round, (long)status);
    if (NT_SUCCESS(status))
        ok(state.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE,
                "Round %u: execution state %u\n", round, state.ExecutionState);
}

/* A device with a completed DMA fault must keep reporting that terminal
 * failure even if the destination window later becomes occluded. */
static void check_failed_device(D3DKMT_HANDLE adapter, D3DKMT_HANDLE device,
        D3DKMT_HANDLE present_context, D3DKMT_HANDLE source)
{
    D3DKMT_CREATECONTEXTVIRTUAL context = {0};
    D3DKMT_CREATEPAGINGQUEUE queue = {0};
    D3DDDI_ALLOCATIONINFO info = {0};
    D3DKMT_CREATEALLOCATION create = {0};
    D3DDDI_MAPGPUVIRTUALADDRESS map = {0};
    D3DKMT_LOCK2 lock = {0};
    D3DKMT_UNLOCK2 unlock = {0};
    D3DKMT_SUBMITCOMMAND submit = {0};
    D3DKMT_WAITFORIDLE wait = {0};
    D3DKMT_GETDEVICESTATE state = {0};
    D3DKMT_PRESENT present = {0};
    SOFTGPU_CMD *command;
    HWND window = NULL;
    NTSTATUS status;
    DWORD began;

    LOADFN(PFND3DKMT_CREATECONTEXTVIRTUAL, CreateVirtualContext, "D3DKMTCreateContextVirtual");
    LOADFN(PFND3DKMT_DESTROYCONTEXT, DestroyContext, "D3DKMTDestroyContext");
    LOADFN(PFND3DKMT_CREATEPAGINGQUEUE, CreateQueue, "D3DKMTCreatePagingQueue");
    LOADFN(PFND3DKMT_DESTROYPAGINGQUEUE, DestroyQueue, "D3DKMTDestroyPagingQueue");
    LOADFN(PFND3DKMT_MAPGPUVIRTUALADDRESS, Map, "D3DKMTMapGpuVirtualAddress");
    LOADFN(PFND3DKMT_FREEGPUVIRTUALADDRESS, FreeMap, "D3DKMTFreeGpuVirtualAddress");
    LOADFN(PFND3DKMT_SUBMITCOMMAND, Submit, "D3DKMTSubmitCommand");

    trace("Terminal device failure must take precedence over occlusion\n");
    window = CreateWindowExW(0, L"STATIC", L"Failed KMT device", WS_POPUP,
            0, 0, 16, 16, NULL, NULL, NULL, NULL);
    ok(window != NULL, "Create hidden window: %lu\n", GetLastError());
    if (!window) goto cleanup;
    context.hDevice = device;
    context.EngineAffinity = 1;
    CHECK(CreateVirtualContext(&context));
    queue.hDevice = device;
    queue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    CHECK(CreateQueue(&queue));
    create.hDevice = device;
    create.NumAllocations = 1;
    create.pAllocationInfo = &info;
    CHECK(CreateAllocation(&create));
    map.hPagingQueue = queue.hPagingQueue;
    map.hAllocation = info.hAllocation;
    map.SizeInPages = 1;
    map.Protection.Write = map.Protection.Execute = 1;
    CHECK(Map(&map));
    lock.hDevice = unlock.hDevice = device;
    lock.hAllocation = unlock.hAllocation = info.hAllocation;
    CHECK(Lock(&lock));
    command = lock.pData;
    memset(command, 0, sizeof(*command));
    command->Magic = SOFTGPU_CMD_MAGIC;
    command->Size = sizeof(*command);
    command->Op = SOFTGPU_CMD_OP_NOP;
    CHECK(Unlock(&unlock));
    submit.Commands = map.VirtualAddress;
    submit.CommandLength = sizeof(*command);
    submit.BroadcastContextCount = 1;
    submit.BroadcastContext[0] = context.hContext;
    /* First prove this context and mapped command buffer execute normally. */
    CHECK(Submit(&submit));
    wait.hDevice = device;
    CHECK(WaitForIdle(&wait));
    state.hDevice = device;
    state.StateType = D3DKMT_DEVICESTATE_EXECUTION;
    CHECK(GetDeviceState(&state));
    ok(state.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE,
            "NOP control execution state %u\n", state.ExecutionState);
    if (state.ExecutionState != D3DKMT_DEVICEEXECUTION_ACTIVE) goto cleanup;

    CHECK(Lock(&lock));
    command = lock.pData;
    command->Op = 0xffffffff; /* Stock SoftGPU reports an invalid instruction. */
    CHECK(Unlock(&unlock));
    CHECK(Submit(&submit));
    began = GetTickCount();
    do
    {
        status = GetDeviceState(&state);
        if (!NT_SUCCESS(status) || state.ExecutionState != D3DKMT_DEVICEEXECUTION_ACTIVE)
            break;
        Sleep(10);
    } while (GetTickCount() - began < 5000);
    ok(NT_SUCCESS(status), "Query faulted device: %#lx\n", (long)status);
    ok(state.ExecutionState != D3DKMT_DEVICEEXECUTION_ACTIVE,
            "Invalid instruction did not fault the device\n");
    if (!NT_SUCCESS(status) || state.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE)
        goto cleanup;
    trace("Faulted execution state %u\n", state.ExecutionState);
    present.hContext = present_context;
    present.hSource = source;
    present.hWindow = window;
    present.Flags.Blt = 1;
    present.Flags.SrcRectValid = present.Flags.DstRectValid = 1;
    present.SrcRect.right = present.SrcRect.bottom = 16;
    present.DstRect = present.SrcRect;
    status = Present(&present);
    ok(status == STATUS_DEVICE_REMOVED,
            "Hidden present must preserve device failure, got %#lx\n", (long)status);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    status = Present(&present);
    ok(status == STATUS_DEVICE_REMOVED,
            "Visible present must preserve device failure, got %#lx\n", (long)status);

cleanup:
    if (window) DestroyWindow(window);
    if (context.hContext)
    {
        D3DKMT_DESTROYCONTEXT destroy = {0};
        destroy.hContext = context.hContext;
        status = DestroyContext(&destroy);
        ok(NT_SUCCESS(status), "Destroy fault context: %#lx\n", (long)status);
    }
    if (map.VirtualAddress)
    {
        D3DKMT_FREEGPUVIRTUALADDRESS unmap = {0};
        unmap.hAdapter = adapter;
        unmap.BaseAddress = map.VirtualAddress;
        unmap.Size = 4096;
        status = FreeMap(&unmap);
        ok(NT_SUCCESS(status), "Free command GPUVA: %#lx\n", (long)status);
    }
    if (info.hAllocation)
    {
        D3DKMT_DESTROYALLOCATION destroy = {0};
        destroy.hDevice = device;
        destroy.phAllocationList = &info.hAllocation;
        destroy.AllocationCount = 1;
        status = DestroyAllocation(&destroy);
        ok(NT_SUCCESS(status), "Destroy command allocation: %#lx\n", (long)status);
    }
    if (queue.hPagingQueue)
    {
        D3DDDI_DESTROYPAGINGQUEUE destroy = {0};
        destroy.hPagingQueue = queue.hPagingQueue;
        status = DestroyQueue(&destroy);
        ok(NT_SUCCESS(status), "Destroy paging queue: %#lx\n", (long)status);
    }
}

START_TEST(softgpu_occlusion)
{
    D3DKMT_HANDLE adapter = 0, device = 0, source = 0, destination = 0;
    D3DKMT_CREATECONTEXT context = {0};
    D3DKMT_PRESENT present = {0};
    D3DKMT_UMDFILENAMEINFO filename = {0};
    D3DKMT_QUERYADAPTERINFO query = {0};
    PFND3DKMT_QUERYADAPTERINFO QueryAdapter;
    PFND3DKMT_CREATECONTEXT CreateContext;
    PFND3DKMT_DESTROYCONTEXT DestroyContext;
    HWND window = NULL, child = NULL;
    WCHAR *base, *cursor;
    NTSTATUS status;
    unsigned round;

#define LOAD(var, type, name) do { \
    var = (type)LoadD3DKMTProc(name); \
    if (!var) { skip("Missing %s\n", name); return; } \
} while (0)
    LOAD(QueryAdapter, PFND3DKMT_QUERYADAPTERINFO, "D3DKMTQueryAdapterInfo");
    LOAD(CreateAllocation, PFND3DKMT_CREATEALLOCATION, "D3DKMTCreateAllocation");
    LOAD(DestroyAllocation, PFND3DKMT_DESTROYALLOCATION, "D3DKMTDestroyAllocation");
    LOAD(CreateContext, PFND3DKMT_CREATECONTEXT, "D3DKMTCreateContext");
    LOAD(DestroyContext, PFND3DKMT_DESTROYCONTEXT, "D3DKMTDestroyContext");
    LOAD(Present, PFND3DKMT_PRESENT, "D3DKMTPresent");
    LOAD(GetDeviceState, PFND3DKMT_GETDEVICESTATE, "D3DKMTGetDeviceState");
    LOAD(Lock, PFND3DKMT_LOCK2, "D3DKMTLock2");
    LOAD(Unlock, PFND3DKMT_UNLOCK2, "D3DKMTUnlock2");
    LOAD(WaitForIdle, PFN_D3DKMTWaitForIdle, "D3DKMTWaitForIdle");
#undef LOAD

    adapter = OpenAdapterFromDisplay1();
    if (!adapter) { skip("No display adapter\n"); return; }
    /* Never pass software miniport private data to a vendor driver. */
    filename.Version = KMTUMDVERSION_DX9;
    query.hAdapter = adapter;
    query.Type = KMTQAITYPE_UMDRIVERNAME;
    query.pPrivateDriverData = &filename;
    query.PrivateDriverDataSize = sizeof(filename);
    status = QueryAdapter(&query);
    filename.UmdFileName[ARRAY_SIZE(filename.UmdFileName) - 1] = 0;
    base = filename.UmdFileName;
    for (cursor = base; *cursor; ++cursor)
        if (*cursor == L'\\' || *cursor == L'/') base = cursor + 1;
    if (!NT_SUCCESS(status) || lstrcmpiW(base, L"softgpuum.dll"))
    {
        skip("Requires the ReactOS software GPU\n");
        goto cleanup;
    }
    device = CreateTestDevice(adapter);
    ok(device != 0, "Create device\n");
    if (!device) goto cleanup;
    context.hDevice = device;
    context.EngineAffinity = 1;
    CHECK(CreateContext(&context));
    source = create_surface(device);
    destination = create_surface(device);
    if (!source || !destination) goto cleanup;
    window = CreateWindowExW(0, L"STATIC", L"KMT occlusion", WS_POPUP,
            0, 0, 32, 32, NULL, NULL, NULL, NULL);
    child = CreateWindowExW(0, L"STATIC", L"child", WS_CHILD | WS_VISIBLE,
            0, 0, 16, 16, window, NULL, NULL, NULL);
    ok(window && child, "Create windows: %lu\n", GetLastError());
    if (!window || !child) goto cleanup;

    check_copy(device, context.hContext, source, destination, 0);
    present.hContext = context.hContext;
    present.hSource = source;
    present.Flags.Blt = 1;
    present.Flags.SrcRectValid = present.Flags.DstRectValid = 1;
    present.SrcRect.right = present.SrcRect.bottom = 16;
    present.DstRect = present.SrcRect;
    /* Hidden window, hidden parent, minimized window, minimized parent,
     * and zero-sized visible client area must all reject display work. */
    for (round = 0; round < 5; ++round)
    {
        trace("Occlusion round %u\n", round);
        if (round == 2) ShowWindow(window, SW_SHOWMINNOACTIVE);
        if (round == 4)
        {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER);
        }
        present.hWindow = (round == 1 || round == 3) ? child : window;
        status = Present(&present);
        ok(status == STATUS_GRAPHICS_PRESENT_OCCLUDED,
                "Round %u: expected occlusion, got %#lx\n", round, (long)status);
        check_copy(device, context.hContext, source, destination, round + 1);
    }

    /* A bad allocation or window must not be hidden by occlusion. */
    present.hSource = 0xdeadbeef;
    status = Present(&present);
    ok(!NT_SUCCESS(status) && status != STATUS_GRAPHICS_PRESENT_OCCLUDED,
            "Invalid source: %#lx\n", (long)status);
    present.hSource = source;
    DestroyWindow(child);
    child = NULL;
    DestroyWindow(window);
    present.hWindow = window;
    window = NULL;
    status = Present(&present);
    ok(status == STATUS_INVALID_HANDLE, "Destroyed window: %#lx\n", (long)status);
    check_copy(device, context.hContext, source, destination, 6);
    check_failed_device(adapter, device, context.hContext, source);

cleanup:
    if (child) DestroyWindow(child);
    if (window) DestroyWindow(window);
    if (context.hContext)
    {
        D3DKMT_DESTROYCONTEXT destroy = {0};
        destroy.hContext = context.hContext;
        status = DestroyContext(&destroy);
        ok(NT_SUCCESS(status), "Destroy context: %#lx\n", (long)status);
    }
    if (source || destination)
    {
        D3DKMT_HANDLE allocations[2];
        D3DKMT_DESTROYALLOCATION destroy = {0};
        if (source) allocations[destroy.AllocationCount++] = source;
        if (destination) allocations[destroy.AllocationCount++] = destination;
        destroy.hDevice = device;
        destroy.phAllocationList = allocations;
        status = DestroyAllocation(&destroy);
        ok(NT_SUCCESS(status), "Destroy allocations: %#lx\n", (long)status);
    }
    if (device) DestroyTestDevice(device);
    if (adapter) CloseAdapter(adapter);
}
