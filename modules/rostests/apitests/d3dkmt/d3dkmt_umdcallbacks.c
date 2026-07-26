/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The runtime's D3DDDI_DEVICECALLBACKS, driven as a driver drives them
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * `umdload` proves a user-mode driver can be found, loaded and handed its
 * function table.  It deliberately stops there, because a driver that has been
 * loaded still cannot do anything: it does not call D3DKMT, it calls back
 * through D3DDDI_DEVICECALLBACKS, which the runtime implements and hands over at
 * pfnCreateDevice.  A driver given a table of NULLs is loaded and useless.
 *
 * These tests are the other direction.  They call the callbacks exactly as a
 * driver would -- with the runtime device handle, not a D3DKMT handle -- and
 * check that each one reaches the kernel and reports back honestly.
 *
 * The handle distinction is deliberate and is asserted below: the runtime hands
 * the driver a handle of its own, so a driver cannot quietly use it to call
 * D3DKMT directly and bypass the interface it is supposed to be written
 * against.  A driver that did would work here and nowhere else.
 */

#include "precomp.h"
#include <d3dumddi.h>

typedef HRESULT (WINAPI *PFN_D3DUmdRtCreateDeviceCallbacks)(D3DKMT_HANDLE, D3DKMT_HANDLE,
                                                            D3DDDI_DEVICECALLBACKS *, HANDLE *);
typedef VOID (WINAPI *PFN_D3DUmdRtDestroyDeviceCallbacks)(HANDLE);

static PFN_D3DUmdRtCreateDeviceCallbacks pfnCreateCallbacks;
static PFN_D3DUmdRtDestroyDeviceCallbacks pfnDestroyCallbacks;

/* ------------------------------------------------------------------ *
 * The table must exist and name real entries.
 * ------------------------------------------------------------------ */
static void Test_TableIsPopulated(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    memset(&Callbacks, 0xCC, sizeof(Callbacks));
    hr = pfnCreateCallbacks(hAdapter, hDevice, &Callbacks, &hRuntimeDevice);
    ok(hr == S_OK, "building the callback table failed 0x%08lX\n", (long)hr);
    if (hr != S_OK)
        return;

    ok(hRuntimeDevice != NULL, "no runtime device handle\n");
    /*
     * The handle the driver gets back must not be the D3DKMT device handle.
     * If it were, a driver could pass it straight to D3DKMT and work only on an
     * implementation that happened to make them the same value.
     */
    ok(hRuntimeDevice != (HANDLE)(ULONG_PTR)hDevice,
       "the runtime handed the driver its own D3DKMT device handle\n");

    /* The entries a driver needs to get from loaded to submitting. */
    ok(Callbacks.pfnAllocateCb != NULL, "no pfnAllocateCb -- a driver cannot allocate\n");
    ok(Callbacks.pfnDeallocateCb != NULL, "no pfnDeallocateCb\n");
    ok(Callbacks.pfnLockCb != NULL, "no pfnLockCb -- a driver cannot map anything\n");
    ok(Callbacks.pfnUnlockCb != NULL, "no pfnUnlockCb\n");
    ok(Callbacks.pfnRenderCb != NULL, "no pfnRenderCb -- a driver cannot submit\n");
    ok(Callbacks.pfnCreateContextCb != NULL, "no pfnCreateContextCb\n");
    ok(Callbacks.pfnDestroyContextCb != NULL, "no pfnDestroyContextCb\n");
    ok(Callbacks.pfnEscapeCb != NULL, "no pfnEscapeCb\n");

    /*
     * Unimplemented entries stay NULL.  A driver tests for NULL before calling,
     * so a stub returning S_OK without doing the work is a lie the driver
     * cannot detect until its results are wrong -- this pins that choice.
     */
    ok(Callbacks.pfnCreateOverlayCb == NULL,
       "an unimplemented entry is filled in; a driver will call it and believe it worked\n");

    pfnDestroyCallbacks(hRuntimeDevice);
}

static void Test_RefusesMalformedConstruction(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    HANDLE hRuntimeDevice = NULL;

    ok(FAILED(pfnCreateCallbacks(hAdapter, hDevice, NULL, &hRuntimeDevice)),
       "NULL callback table accepted\n");
    ok(FAILED(pfnCreateCallbacks(hAdapter, hDevice, &Callbacks, NULL)),
       "NULL handle output accepted\n");
    /* Every callback is serviced against these two handles; without them there
     * is nothing to service against. */
    ok(FAILED(pfnCreateCallbacks(0, hDevice, &Callbacks, &hRuntimeDevice)),
       "zero adapter handle accepted\n");
    ok(FAILED(pfnCreateCallbacks(hAdapter, 0, &Callbacks, &hRuntimeDevice)),
       "zero device handle accepted\n");

    /* Releasing something that is not a runtime device must not crash. */
    pfnDestroyCallbacks(NULL);
    pfnDestroyCallbacks((HANDLE)(ULONG_PTR)0xBAD0CAFE);
}

/* ------------------------------------------------------------------ *
 * Driving the callbacks the way a driver does.
 * ------------------------------------------------------------------ */
static void Test_AllocateLockUnlockDeallocate(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    D3DDDICB_LOCK Lock;
    D3DDDICB_UNLOCK Unlock;
    D3DKMT_HANDLE Handles[1];
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter, hDevice, &Callbacks, &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    /* A driver checks the handle it was given, not one it invented. */
    memset(&Allocate, 0, sizeof(Allocate));
    ok(FAILED(Callbacks.pfnAllocateCb((HANDLE)(ULONG_PTR)0xBAD0CAFE, &Allocate)),
       "allocate accepted a handle the runtime never issued\n");
    ok(FAILED(Callbacks.pfnAllocateCb(hRuntimeDevice, NULL)),
       "allocate accepted a NULL request\n");
    /* Zero allocations names nothing to create. */
    ok(FAILED(Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate)),
       "allocate accepted a request for zero allocations\n");

    memset(&AllocInfo, 0, sizeof(AllocInfo));
    AllocInfo.pSystemMem = NULL;
    memset(&Allocate, 0, sizeof(Allocate));
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &AllocInfo;

    hr = Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate);
    if (FAILED(hr))
    {
        /* Refusing is a legitimate answer from a driver-less adapter; what is
         * not legitimate is claiming success without producing a handle. */
        trace("allocate through the callback refused 0x%08lX\n", (long)hr);
        skip("adapter will not allocate through the callback\n");
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    ok(AllocInfo.hAllocation != 0,
       "allocate reported success without producing an allocation handle\n");
    if (AllocInfo.hAllocation == 0)
    {
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    /* Lock, which is how a driver gets a CPU pointer to what it just made. */
    memset(&Lock, 0, sizeof(Lock));
    Lock.hAllocation = AllocInfo.hAllocation;
    hr = Callbacks.pfnLockCb(hRuntimeDevice, &Lock);
    if (SUCCEEDED(hr))
    {
        ok(Lock.pData != NULL, "lock succeeded but produced no pointer\n");
        memset(&Unlock, 0, sizeof(Unlock));
        Handles[0] = AllocInfo.hAllocation;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = Handles;
        hr = Callbacks.pfnUnlockCb(hRuntimeDevice, &Unlock);
        ok(SUCCEEDED(hr), "unlock of a locked allocation failed 0x%08lX\n", (long)hr);
    }

    /* And free it through the same table. */
    Handles[0] = AllocInfo.hAllocation;
    memset(&Deallocate, 0, sizeof(Deallocate));
    Deallocate.NumAllocations = 1;
    Deallocate.HandleList = Handles;
    hr = Callbacks.pfnDeallocateCb(hRuntimeDevice, &Deallocate);
    ok(SUCCEEDED(hr), "deallocate through the callback failed 0x%08lX\n", (long)hr);

    /* Naming nothing at all is malformed: with no resource and no list there is
     * nothing to free, and succeeding would tell the driver its memory was
     * released when it was not. */
    memset(&Deallocate, 0, sizeof(Deallocate));
    ok(FAILED(Callbacks.pfnDeallocateCb(hRuntimeDevice, &Deallocate)),
       "deallocate naming neither a resource nor a handle list accepted\n");

    pfnDestroyCallbacks(hRuntimeDevice);
}

static void Test_ContextLifetimeThroughCallbacks(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATECONTEXT Create;
    D3DDDICB_DESTROYCONTEXT Destroy;
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter, hDevice, &Callbacks, &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&Create, 0, sizeof(Create));
    Create.NodeOrdinal = 0;
    Create.EngineAffinity = 0;
    hr = Callbacks.pfnCreateContextCb(hRuntimeDevice, &Create);
    if (FAILED(hr))
    {
        skip("adapter has no render node reachable through the callback (0x%08lX)\n", (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    ok(Create.hContext != NULL, "context creation succeeded without a handle\n");
    /*
     * All three of these come back together and are useless apart: a command
     * buffer with no allocation list cannot name what it touches, so a driver
     * given one without the other cannot build a submission at all.
     */
    ok(Create.pCommandBuffer != NULL, "no command buffer handed to the driver\n");
    ok(Create.pAllocationList != NULL, "no allocation list handed to the driver\n");
    ok(Create.pPatchLocationList != NULL, "no patch location list handed to the driver\n");
    ok(Create.CommandBufferSize != 0, "a command buffer of zero bytes\n");
    trace("context: buffer %u bytes, %u allocations, %u patches\n",
          Create.CommandBufferSize, Create.AllocationListSize, Create.PatchLocationListSize);

    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hContext = Create.hContext;
    hr = Callbacks.pfnDestroyContextCb(hRuntimeDevice, &Destroy);
    ok(SUCCEEDED(hr), "destroying the context through the callback failed 0x%08lX\n", (long)hr);

    pfnDestroyCallbacks(hRuntimeDevice);
}

START_TEST(umdcallbacks)
{
    HMODULE Runtime;
    D3DKMT_HANDLE hAdapter, hDevice;

    Runtime = LoadLibraryW(L"d3dumdrt.dll");
    if (Runtime == NULL)
    {
        skip("d3dumdrt.dll not present (error %lu)\n", GetLastError());
        return;
    }
    pfnCreateCallbacks = (PFN_D3DUmdRtCreateDeviceCallbacks)
        GetProcAddress(Runtime, "D3DUmdRtCreateDeviceCallbacks");
    pfnDestroyCallbacks = (PFN_D3DUmdRtDestroyDeviceCallbacks)
        GetProcAddress(Runtime, "D3DUmdRtDestroyDeviceCallbacks");
    ok(pfnCreateCallbacks != NULL, "d3dumdrt.dll exports no D3DUmdRtCreateDeviceCallbacks\n");
    ok(pfnDestroyCallbacks != NULL, "d3dumdrt.dll exports no D3DUmdRtDestroyDeviceCallbacks\n");
    if (pfnCreateCallbacks == NULL || pfnDestroyCallbacks == NULL)
    {
        FreeLibrary(Runtime);
        return;
    }

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter)
    {
        skip("No adapter on \\\\.\\DISPLAY1\n");
        FreeLibrary(Runtime);
        return;
    }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice)
    {
        skip("No device\n");
        CloseAdapter(hAdapter);
        FreeLibrary(Runtime);
        return;
    }

    Test_TableIsPopulated(hAdapter, hDevice);
    Test_RefusesMalformedConstruction(hAdapter, hDevice);
    Test_AllocateLockUnlockDeallocate(hAdapter, hDevice);
    Test_ContextLifetimeThroughCallbacks(hAdapter, hDevice);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
    FreeLibrary(Runtime);
}

/* EOF */
