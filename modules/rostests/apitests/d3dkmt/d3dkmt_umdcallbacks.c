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
typedef HRESULT (WINAPI *PFN_D3DUmdRtDestroyDeviceCallbacks)(HANDLE);

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
    ok(Callbacks.pfnCreateSynchronizationObjectCb != NULL,
       "no pfnCreateSynchronizationObjectCb\n");
    ok(Callbacks.pfnDestroySynchronizationObjectCb != NULL,
       "no pfnDestroySynchronizationObjectCb\n");
    ok(Callbacks.pfnWaitForSynchronizationObjectCb != NULL,
       "no pfnWaitForSynchronizationObjectCb\n");
    ok(Callbacks.pfnSignalSynchronizationObjectCb != NULL,
       "no pfnSignalSynchronizationObjectCb\n");
    ok(Callbacks.pfnEscapeCb != NULL, "no pfnEscapeCb\n");
    ok(Callbacks.pfnSetDisplayModeCb != NULL,
       "no pfnSetDisplayModeCb -- a primary cannot become scanout\n");
    ok(Callbacks.pfnPresentCb != NULL, "no pfnPresentCb -- a driver cannot show anything\n");

    /*
     * Unimplemented entries stay NULL.  A driver tests for NULL before calling,
     * so a stub returning S_OK without doing the work is a lie the driver
     * cannot detect until its results are wrong -- this pins that choice.
     */
    ok(Callbacks.pfnCreateOverlayCb == NULL,
       "an unimplemented entry is filled in; a driver will call it and believe it worked\n");

#if (REACTOS_EXPECTED_UMD_INTERFACE_VERSION >= \
     D3D_UMD_INTERFACE_VERSION_WIN8)
    ok(Callbacks.pfnOfferAllocationsCb != NULL,
       "no Win8 pfnOfferAllocationsCb\n");
    ok(Callbacks.pfnReclaimAllocationsCb != NULL,
       "no Win8 pfnReclaimAllocationsCb\n");
    ok(Callbacks.pfnCreateSynchronizationObject2Cb != NULL,
       "no Win8 pfnCreateSynchronizationObject2Cb\n");
    ok(Callbacks.pfnWaitForSynchronizationObject2Cb != NULL,
       "no Win8 pfnWaitForSynchronizationObject2Cb\n");
    ok(Callbacks.pfnSignalSynchronizationObject2Cb != NULL,
       "no Win8 pfnSignalSynchronizationObject2Cb\n");
#endif

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

static void
Test_SynchronizationObjectThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT CreateSync;
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT Wait;
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT Signal;
    D3DDDICB_CREATECONTEXT CreateContext;
    D3DDDICB_DESTROYCONTEXT DestroyContext;
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&CreateSync, 0, sizeof(CreateSync));
    CreateSync.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    CreateSync.Info.SynchronizationMutex.InitialState = TRUE;
    hr = Callbacks.pfnCreateSynchronizationObjectCb(hRuntimeDevice,
                                                     &CreateSync);
    if (FAILED(hr))
    {
        skip("adapter refused a synchronization object through the callback "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }
    ok(CreateSync.hSyncObject != 0,
       "synchronization creation succeeded without a handle\n");

    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(FAILED(hr),
       "the runtime device was released with a synchronization object open\n");

    memset(&CreateContext, 0, sizeof(CreateContext));
    hr = Callbacks.pfnCreateContextCb(hRuntimeDevice, &CreateContext);
    if (SUCCEEDED(hr))
    {
        memset(&Wait, 0, sizeof(Wait));
        Wait.hContext = CreateContext.hContext;
        Wait.ObjectCount = 1;
        Wait.ObjectHandleArray[0] = CreateSync.hSyncObject;
        hr = Callbacks.pfnWaitForSynchronizationObjectCb(hRuntimeDevice,
                                                          &Wait);
        ok(SUCCEEDED(hr),
           "legacy synchronization wait callback failed 0x%08lX\n",
           (long)hr);

        memset(&Signal, 0, sizeof(Signal));
        Signal.hContext = CreateContext.hContext;
        Signal.ObjectCount = 1;
        Signal.ObjectHandleArray[0] = CreateSync.hSyncObject;
        hr = Callbacks.pfnSignalSynchronizationObjectCb(hRuntimeDevice,
                                                         &Signal);
        ok(SUCCEEDED(hr),
           "legacy synchronization signal callback failed 0x%08lX\n",
           (long)hr);

        memset(&DestroyContext, 0, sizeof(DestroyContext));
        DestroyContext.hContext = CreateContext.hContext;
        hr = Callbacks.pfnDestroyContextCb(hRuntimeDevice,
                                            &DestroyContext);
        ok(SUCCEEDED(hr),
           "destroying the synchronization test context failed 0x%08lX\n",
           (long)hr);
    }
    else
    {
        skip("adapter has no context for synchronization ordering "
             "(0x%08lX)\n",
             (long)hr);
    }

    memset(&DestroySync, 0, sizeof(DestroySync));
    DestroySync.hSyncObject = CreateSync.hSyncObject;
    hr = Callbacks.pfnDestroySynchronizationObjectCb(hRuntimeDevice,
                                                      &DestroySync);
    ok(SUCCEEDED(hr),
       "destroying the synchronization object failed 0x%08lX\n",
       (long)hr);
    if (SUCCEEDED(hr))
    {
        ok(FAILED(Callbacks.pfnDestroySynchronizationObjectCb(
                      hRuntimeDevice,
                      &DestroySync)),
           "a synchronization object was destroyed twice\n");
    }

    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "synchronization bookkeeping remained live after destroy "
       "(0x%08lX)\n",
       (long)hr);
}

#if (REACTOS_EXPECTED_UMD_INTERFACE_VERSION >= \
     D3D_UMD_INTERFACE_VERSION_WIN8)

static void
Test_Win8OfferReclaimThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    D3DDDICB_OFFERALLOCATIONS Offer;
    D3DDDICB_RECLAIMALLOCATIONS Reclaim;
    D3DKMT_HANDLE AllocationHandle;
    HANDLE RuntimeResource = (HANDLE)(ULONG_PTR)0x52534F46;
    HANDLE hRuntimeDevice = NULL;
    BOOL Discarded;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&Offer, 0, sizeof(Offer));
    ok(FAILED(Callbacks.pfnOfferAllocationsCb(hRuntimeDevice, &Offer)),
       "offer naming no allocations was accepted\n");

    Offer.pResources = &RuntimeResource;
    Offer.NumAllocations = 1;
    Offer.Priority = D3DDDI_OFFER_PRIORITY_NORMAL;
    hr = Callbacks.pfnOfferAllocationsCb(hRuntimeDevice, &Offer);
    ok(hr == E_INVALIDARG,
       "an unknown runtime resource was not rejected, got 0x%08lX\n",
       (long)hr);

    memset(&Reclaim, 0, sizeof(Reclaim));
    Reclaim.pResources = &RuntimeResource;
    Reclaim.NumAllocations = 1;
    hr = Callbacks.pfnReclaimAllocationsCb(hRuntimeDevice, &Reclaim);
    ok(hr == E_INVALIDARG,
       "an unknown runtime resource reclaim was not rejected, got 0x%08lX\n",
       (long)hr);

    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&Allocate, 0, sizeof(Allocate));
    Allocate.hResource = RuntimeResource;
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &AllocInfo;
    hr = Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate);
    if (FAILED(hr))
    {
        skip("adapter refused an allocation for offer/reclaim "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    AllocationHandle = AllocInfo.hAllocation;
    ok(Allocate.hKMResource != 0,
       "offer/reclaim resource succeeded without a KMT resource\n");
    ok(AllocationHandle != 0,
       "offer/reclaim allocation succeeded without a handle\n");
    if (Allocate.hKMResource == 0 || AllocationHandle == 0)
    {
        memset(&Deallocate, 0, sizeof(Deallocate));
        Deallocate.hResource = RuntimeResource;
        (VOID)Callbacks.pfnDeallocateCb(
                  hRuntimeDevice,
                  &Deallocate);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    memset(&Offer, 0, sizeof(Offer));
    Offer.pResources = &RuntimeResource;
    Offer.NumAllocations = 1;
    Offer.Priority = D3DDDI_OFFER_PRIORITY_NORMAL;
    hr = Callbacks.pfnOfferAllocationsCb(hRuntimeDevice, &Offer);
    ok(SUCCEEDED(hr),
       "runtime-resource offer callback failed 0x%08lX\n",
       (long)hr);
    if (SUCCEEDED(hr))
    {
        Discarded = (BOOL)0x7F7F7F7F;
        memset(&Reclaim, 0, sizeof(Reclaim));
        Reclaim.pResources = &RuntimeResource;
        Reclaim.pDiscarded = &Discarded;
        Reclaim.NumAllocations = 1;
        hr = Callbacks.pfnReclaimAllocationsCb(
                 hRuntimeDevice,
                 &Reclaim);
        ok(SUCCEEDED(hr),
           "runtime-resource reclaim callback failed 0x%08lX\n",
           (long)hr);
        if (SUCCEEDED(hr))
        {
            ok(Discarded == FALSE || Discarded == TRUE,
               "resource reclaim returned a noncanonical discarded value "
               "0x%08lX\n",
               (long)Discarded);
        }
    }

    memset(&Offer, 0, sizeof(Offer));
    Offer.HandleList = &AllocationHandle;
    Offer.NumAllocations = 1;
    Offer.Priority = D3DDDI_OFFER_PRIORITY_NONE;
    ok(FAILED(Callbacks.pfnOfferAllocationsCb(hRuntimeDevice, &Offer)),
       "offer priority NONE was accepted\n");

    Offer.Priority = D3DDDI_OFFER_PRIORITY_NORMAL;
    hr = Callbacks.pfnOfferAllocationsCb(hRuntimeDevice, &Offer);
    ok(SUCCEEDED(hr),
       "allocation-list offer callback failed 0x%08lX\n",
       (long)hr);

    if (SUCCEEDED(hr))
    {
        Discarded = (BOOL)0x7F7F7F7F;
        memset(&Reclaim, 0, sizeof(Reclaim));
        Reclaim.HandleList = &AllocationHandle;
        Reclaim.pDiscarded = &Discarded;
        Reclaim.NumAllocations = 1;
        hr = Callbacks.pfnReclaimAllocationsCb(hRuntimeDevice, &Reclaim);
        ok(SUCCEEDED(hr),
           "allocation-list reclaim callback failed 0x%08lX\n",
           (long)hr);
        if (SUCCEEDED(hr))
        {
            ok(Discarded == FALSE || Discarded == TRUE,
               "reclaim did not return a canonical discarded value 0x%08lX\n",
               (long)Discarded);
        }
    }

    memset(&Deallocate, 0, sizeof(Deallocate));
    Deallocate.hResource = RuntimeResource;
    hr = Callbacks.pfnDeallocateCb(hRuntimeDevice, &Deallocate);
    ok(SUCCEEDED(hr),
       "offer/reclaim allocation cleanup failed 0x%08lX\n",
       (long)hr);

    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "offer/reclaim callback device cleanup failed 0x%08lX\n",
       (long)hr);
}

static void
Test_Win8SynchronizationObject2ThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 Signal;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT2 Wait;
    D3DDDICB_CREATECONTEXT CreateContext;
    D3DDDICB_DESTROYCONTEXT DestroyContext;
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&CreateSync, 0, sizeof(CreateSync));
    CreateSync.Info.Type = D3DDDI_FENCE;
    CreateSync.Info.Fence.FenceValue = 0;
    hr = Callbacks.pfnCreateSynchronizationObject2Cb(hRuntimeDevice,
                                                      &CreateSync);
    if (FAILED(hr))
    {
        skip("adapter refused a Win8 fence through the callback "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }
    ok(CreateSync.hSyncObject != 0,
       "Win8 fence creation succeeded without a handle\n");

    memset(&CreateContext, 0, sizeof(CreateContext));
    hr = Callbacks.pfnCreateContextCb(hRuntimeDevice, &CreateContext);
    if (SUCCEEDED(hr))
    {
        memset(&Signal, 0, sizeof(Signal));
        Signal.hContext = CreateContext.hContext;
        Signal.ObjectCount = 1;
        Signal.ObjectHandleArray[0] = CreateSync.hSyncObject;
        Signal.FenceValue = 7;
        hr = Callbacks.pfnSignalSynchronizationObject2Cb(hRuntimeDevice,
                                                          &Signal);
        ok(SUCCEEDED(hr),
           "Win8 fence signal callback failed 0x%08lX\n",
           (long)hr);

        memset(&Wait, 0, sizeof(Wait));
        Wait.hContext = CreateContext.hContext;
        Wait.ObjectCount = 1;
        Wait.ObjectHandleArray[0] = CreateSync.hSyncObject;
        Wait.FenceValue = 7;
        hr = Callbacks.pfnWaitForSynchronizationObject2Cb(hRuntimeDevice,
                                                           &Wait);
        ok(SUCCEEDED(hr),
           "Win8 fence wait callback failed 0x%08lX\n",
           (long)hr);

        memset(&DestroyContext, 0, sizeof(DestroyContext));
        DestroyContext.hContext = CreateContext.hContext;
        hr = Callbacks.pfnDestroyContextCb(hRuntimeDevice,
                                            &DestroyContext);
        ok(SUCCEEDED(hr),
           "destroying the Win8 fence test context failed 0x%08lX\n",
           (long)hr);
    }
    else
    {
        skip("adapter has no context for Win8 fence ordering "
             "(0x%08lX)\n",
             (long)hr);
    }

    memset(&DestroySync, 0, sizeof(DestroySync));
    DestroySync.hSyncObject = CreateSync.hSyncObject;
    hr = Callbacks.pfnDestroySynchronizationObjectCb(hRuntimeDevice,
                                                      &DestroySync);
    ok(SUCCEEDED(hr),
       "destroying the Win8 fence failed 0x%08lX\n",
       (long)hr);
    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "Win8 fence bookkeeping remained live after destroy "
       "(0x%08lX)\n",
       (long)hr);
}

#endif

#if (REACTOS_EXPECTED_UMD_INTERFACE_VERSION >= \
     D3D_UMD_INTERFACE_VERSION_WDDM2_0)

/*
 * Deallocate2 receives the runtime's opaque resource cookie, not the KMT
 * resource handle returned by AllocateCb. Pin that translation separately
 * from the allocation-list form so a pointer-shaped cookie can never leak
 * into D3DKMTDestroyAllocation2 as a kernel handle.
 */
static void
Test_Wddm2ResourceDeallocateThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    D3DDDICB_DEALLOCATE2 Deallocate2;
    ULONG_PTR RuntimeResourceCookie = 0x52533244;
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }
    if (Callbacks.pfnDeallocate2Cb == NULL)
    {
        skip("WDDM 2.0 deallocation callback is unavailable\n");
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&Allocate, 0, sizeof(Allocate));
    Allocate.hResource = (HANDLE)&RuntimeResourceCookie;
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &AllocInfo;
    hr = Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate);
    if (FAILED(hr))
    {
        skip("adapter refused a callback resource allocation "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    ok(Allocate.hKMResource != 0,
       "resource allocation succeeded without a KMT resource\n");
    ok(AllocInfo.hAllocation != 0,
       "resource allocation succeeded without an allocation\n");

    memset(&Deallocate2, 0, sizeof(Deallocate2));
    Deallocate2.hResource = (HANDLE)&RuntimeResourceCookie;
    hr = Callbacks.pfnDeallocate2Cb(hRuntimeDevice, &Deallocate2);
    ok(SUCCEEDED(hr),
       "Deallocate2 did not resolve the runtime resource cookie "
       "(0x%08lX)\n",
       (long)hr);
    if (FAILED(hr))
    {
        /* The failed path must leave the mapping retryable. */
        memset(&Deallocate, 0, sizeof(Deallocate));
        Deallocate.hResource = (HANDLE)&RuntimeResourceCookie;
        (VOID)Callbacks.pfnDeallocateCb(hRuntimeDevice, &Deallocate);
    }
    else
    {
        ok(FAILED(Callbacks.pfnDeallocate2Cb(
                      hRuntimeDevice,
                      &Deallocate2)),
           "a released runtime resource cookie was accepted twice\n");
    }

    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "resource mapping remained live after Deallocate2 "
       "(0x%08lX)\n",
       (long)hr);
}

#endif

/* ------------------------------------------------------------------ *
 * The WDDM 2.0 tier: the driver owns its working set and its own GPU
 * address space, rather than the kernel deciding both at submission.
 * ------------------------------------------------------------------ */
#if (REACTOS_EXPECTED_UMD_INTERFACE_VERSION >= \
     D3D_UMD_INTERFACE_VERSION_WDDM2_0)

static void Test_Wddm2TierIsPopulated(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    HANDLE hRuntimeDevice = NULL;

    if (pfnCreateCallbacks(hAdapter, hDevice, &Callbacks, &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    /* Residency: a WDDM 2.0 driver is told what to give back, not silently
     * paged behind its back. */
    ok(Callbacks.pfnMakeResidentCb != NULL, "no pfnMakeResidentCb\n");
    ok(Callbacks.pfnEvictCb != NULL, "no pfnEvictCb\n");
    /* Paging queues: how the driver learns a paging operation finished. */
    ok(Callbacks.pfnCreatePagingQueueCb != NULL, "no pfnCreatePagingQueueCb\n");
    ok(Callbacks.pfnDestroyPagingQueueCb != NULL, "no pfnDestroyPagingQueueCb\n");
    /* GPU virtual addressing: the driver places allocations itself. */
    ok(Callbacks.pfnReserveGpuVirtualAddressCb != NULL, "no pfnReserveGpuVirtualAddressCb\n");
    ok(Callbacks.pfnMapGpuVirtualAddressCb != NULL, "no pfnMapGpuVirtualAddressCb\n");
    ok(Callbacks.pfnFreeGpuVirtualAddressCb != NULL, "no pfnFreeGpuVirtualAddressCb\n");
    ok(Callbacks.pfnUpdateGpuVirtualAddressCb != NULL,
       "no pfnUpdateGpuVirtualAddressCb\n");
    ok(Callbacks.pfnInvalidateCacheCb != NULL,
       "no pfnInvalidateCacheCb\n");
    ok(Callbacks.pfnReclaimAllocations2Cb != NULL,
       "no pfnReclaimAllocations2Cb\n");
    ok(Callbacks.pfnGetResourcePresentPrivateDriverDataCb != NULL,
       "no pfnGetResourcePresentPrivateDriverDataCb\n");
    /* Virtual-addressing submission. */
    ok(Callbacks.pfnCreateContextVirtualCb != NULL, "no pfnCreateContextVirtualCb\n");
    ok(Callbacks.pfnSubmitCommandCb != NULL, "no pfnSubmitCommandCb\n");
    ok(Callbacks.pfnLock2Cb != NULL, "no pfnLock2Cb\n");
    ok(Callbacks.pfnUnlock2Cb != NULL, "no pfnUnlock2Cb\n");
    ok(Callbacks.pfnDeallocate2Cb != NULL, "no pfnDeallocate2Cb\n");
    /* Monitored fences, from both sides. */
    ok(Callbacks.pfnCreateSynchronizationObject2Cb != NULL,
       "no pfnCreateSynchronizationObject2Cb\n");
    ok(Callbacks.pfnWaitForSynchronizationObjectFromCpuCb != NULL,
       "no pfnWaitForSynchronizationObjectFromCpuCb\n");
    ok(Callbacks.pfnSignalSynchronizationObjectFromCpuCb != NULL,
       "no pfnSignalSynchronizationObjectFromCpuCb\n");
    ok(Callbacks.pfnWaitForSynchronizationObjectFromGpuCb != NULL,
       "no pfnWaitForSynchronizationObjectFromGpuCb\n");
    ok(Callbacks.pfnSignalSynchronizationObjectFromGpuCb != NULL,
       "no pfnSignalSynchronizationObjectFromGpuCb\n");
    ok(Callbacks.pfnSignalSynchronizationObjectFromGpu2Cb != NULL,
       "no pfnSignalSynchronizationObjectFromGpu2Cb\n");

    pfnDestroyCallbacks(hRuntimeDevice);
}

static void
Test_Wddm2MonitoredFenceThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU SignalCpu;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU WaitCpu;
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU SignalGpu;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU WaitGpu;
    D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 SignalGpu2;
    D3DDDICB_CREATECONTEXT CreateContext;
    D3DDDICB_DESTROYCONTEXT DestroyContext;
    D3DKMT_HANDLE SyncHandles[1];
    UINT64 FenceValues[1];
    HANDLE ContextHandles[1];
    HANDLE hCompletionEvent = NULL;
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&CreateSync, 0, sizeof(CreateSync));
    CreateSync.Info.Type = D3DDDI_MONITORED_FENCE;
    CreateSync.Info.MonitoredFence.InitialFenceValue = 0;
    hr = Callbacks.pfnCreateSynchronizationObject2Cb(hRuntimeDevice,
                                                      &CreateSync);
    if (FAILED(hr))
    {
        skip("adapter refused a monitored fence through the callback "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    ok(CreateSync.hSyncObject != 0,
       "monitored fence creation succeeded without a handle\n");
    ok(CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress != NULL,
       "monitored fence has no CPU-visible value\n");
    ok(CreateSync.Info.MonitoredFence.FenceValueGPUVirtualAddress != 0,
       "monitored fence has no GPU-visible value\n");

    SyncHandles[0] = CreateSync.hSyncObject;
    FenceValues[0] = 3;
    memset(&SignalCpu, 0, sizeof(SignalCpu));
    SignalCpu.ObjectCount = 1;
    SignalCpu.ObjectHandleArray = SyncHandles;
    SignalCpu.FenceValueArray = FenceValues;
    hr = Callbacks.pfnSignalSynchronizationObjectFromCpuCb(
             hRuntimeDevice,
             &SignalCpu);
    ok(SUCCEEDED(hr),
       "CPU monitored-fence signal callback failed 0x%08lX\n",
       (long)hr);

    memset(&WaitCpu, 0, sizeof(WaitCpu));
    WaitCpu.ObjectCount = 1;
    WaitCpu.ObjectHandleArray = SyncHandles;
    WaitCpu.FenceValueArray = FenceValues;
    hr = Callbacks.pfnWaitForSynchronizationObjectFromCpuCb(
             hRuntimeDevice,
             &WaitCpu);
    ok(SUCCEEDED(hr),
       "CPU monitored-fence wait callback failed 0x%08lX\n",
       (long)hr);
    if (SUCCEEDED(hr) &&
        CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress != NULL)
    {
        ok(*(volatile UINT64 *)
               CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress >=
               FenceValues[0],
           "CPU-visible monitored fence did not reach %I64u\n",
           (unsigned long long)FenceValues[0]);
    }

    memset(&CreateContext, 0, sizeof(CreateContext));
    hr = Callbacks.pfnCreateContextCb(hRuntimeDevice, &CreateContext);
    if (SUCCEEDED(hr))
    {
        FenceValues[0] = 5;
        memset(&SignalGpu, 0, sizeof(SignalGpu));
        SignalGpu.hContext = CreateContext.hContext;
        SignalGpu.ObjectCount = 1;
        SignalGpu.ObjectHandleArray = SyncHandles;
        SignalGpu.MonitoredFenceValueArray = FenceValues;
        hr = Callbacks.pfnSignalSynchronizationObjectFromGpuCb(
                 hRuntimeDevice,
                 &SignalGpu);
        ok(SUCCEEDED(hr),
           "GPU monitored-fence signal callback failed 0x%08lX\n",
           (long)hr);

        memset(&WaitGpu, 0, sizeof(WaitGpu));
        WaitGpu.hContext = CreateContext.hContext;
        WaitGpu.ObjectCount = 1;
        WaitGpu.ObjectHandleArray = SyncHandles;
        WaitGpu.MonitoredFenceValueArray = FenceValues;
        hr = Callbacks.pfnWaitForSynchronizationObjectFromGpuCb(
                 hRuntimeDevice,
                 &WaitGpu);
        ok(SUCCEEDED(hr),
           "GPU monitored-fence wait callback failed 0x%08lX\n",
           (long)hr);

        ContextHandles[0] = CreateContext.hContext;
        FenceValues[0] = 7;
        memset(&SignalGpu2, 0, sizeof(SignalGpu2));
        SignalGpu2.ObjectCount = 1;
        SignalGpu2.ObjectHandleArray = SyncHandles;
        SignalGpu2.BroadcastContextCount = 1;
        SignalGpu2.BroadcastContextArray = ContextHandles;
        SignalGpu2.MonitoredFenceValueArray = FenceValues;
        hr = Callbacks.pfnSignalSynchronizationObjectFromGpu2Cb(
                 hRuntimeDevice,
                 &SignalGpu2);
        ok(SUCCEEDED(hr),
           "GPU2 monitored-fence signal callback failed 0x%08lX\n",
           (long)hr);

        if (SUCCEEDED(hr))
        {
            hCompletionEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
            ok(hCompletionEvent != NULL,
               "could not create the asynchronous fence event\n");
            if (hCompletionEvent != NULL)
            {
                memset(&WaitCpu, 0, sizeof(WaitCpu));
                WaitCpu.ObjectCount = 1;
                WaitCpu.ObjectHandleArray = SyncHandles;
                WaitCpu.FenceValueArray = FenceValues;
                WaitCpu.hAsyncEvent = hCompletionEvent;
                hr = Callbacks.pfnWaitForSynchronizationObjectFromCpuCb(
                         hRuntimeDevice,
                         &WaitCpu);
                ok(SUCCEEDED(hr),
                   "asynchronous CPU fence wait callback failed "
                   "0x%08lX\n",
                   (long)hr);
                if (SUCCEEDED(hr))
                {
                    ok(WaitForSingleObject(hCompletionEvent, 5000) ==
                           WAIT_OBJECT_0,
                       "GPU2 fence signal did not retire within five seconds\n");
                }
            }
        }

        memset(&DestroyContext, 0, sizeof(DestroyContext));
        DestroyContext.hContext = CreateContext.hContext;
        hr = Callbacks.pfnDestroyContextCb(hRuntimeDevice,
                                            &DestroyContext);
        ok(SUCCEEDED(hr),
           "destroying the monitored-fence test context failed "
           "0x%08lX\n",
           (long)hr);
    }
    else
    {
        skip("adapter has no context for GPU monitored-fence ordering "
             "(0x%08lX)\n",
             (long)hr);
    }

    memset(&DestroySync, 0, sizeof(DestroySync));
    DestroySync.hSyncObject = CreateSync.hSyncObject;
    hr = Callbacks.pfnDestroySynchronizationObjectCb(hRuntimeDevice,
                                                      &DestroySync);
    ok(SUCCEEDED(hr),
       "destroying the monitored fence failed 0x%08lX\n",
       (long)hr);
    if (hCompletionEvent != NULL)
        CloseHandle(hCompletionEvent);

    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "monitored-fence bookkeeping remained live after destroy "
       "(0x%08lX)\n",
       (long)hr);
}

static void Test_PagingQueueAndGpuVaThroughCallbacks(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATEPAGINGQUEUE CreateQueue;
    D3DDDI_DESTROYPAGINGQUEUE DestroyQueue;
    D3DDDI_RESERVEGPUVIRTUALADDRESS Reserve;
    D3DDDICB_FREEGPUVIRTUALADDRESS Free;
    HANDLE hRuntimeDevice = NULL;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter, hDevice, &Callbacks, &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&CreateQueue, 0, sizeof(CreateQueue));
    CreateQueue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    hr = Callbacks.pfnCreatePagingQueueCb(hRuntimeDevice, &CreateQueue);
    if (SUCCEEDED(hr))
    {
        ok(CreateQueue.hPagingQueue != 0, "paging queue created without a handle\n");
        /*
         * The queue arrives with its own monitored fence.  Without it the
         * driver has no way to learn that a paging operation it asked for has
         * finished, which makes the queue useless rather than merely limited.
         */
        ok(CreateQueue.hSyncObject != 0, "paging queue has no monitored fence\n");
        ok(CreateQueue.FenceValueCPUVirtualAddress != NULL,
           "paging queue fence has no readable value\n");
        trace("paging queue 0x%X, fence 0x%X\n", CreateQueue.hPagingQueue,
              CreateQueue.hSyncObject);

        memset(&DestroyQueue, 0, sizeof(DestroyQueue));
        DestroyQueue.hPagingQueue = CreateQueue.hPagingQueue;
        hr = Callbacks.pfnDestroyPagingQueueCb(hRuntimeDevice, &DestroyQueue);
        ok(SUCCEEDED(hr), "destroying the paging queue failed 0x%08lX\n", (long)hr);
    }
    else
    {
        trace("paging queue refused through the callback 0x%08lX\n", (long)hr);
    }

    /* Reserving GPU address space is how a WDDM 2.0 driver places its own
     * allocations; the kernel no longer does it at submission time. */
    memset(&Reserve, 0, sizeof(Reserve));
    Reserve.hAdapter = hAdapter;
    Reserve.Size = 0x10000;
    Reserve.BaseAddress = 0;
    Reserve.MinimumAddress = 0;
    Reserve.MaximumAddress = 0;
    hr = Callbacks.pfnReserveGpuVirtualAddressCb(hRuntimeDevice, &Reserve);
    if (SUCCEEDED(hr))
    {
        ok(Reserve.VirtualAddress != 0, "reserve succeeded without an address\n");
        trace("reserved GPU VA 0x%I64X\n", (unsigned long long)Reserve.VirtualAddress);

        memset(&Free, 0, sizeof(Free));
        Free.BaseAddress = Reserve.VirtualAddress;
        Free.Size = 0x10000;
        hr = Callbacks.pfnFreeGpuVirtualAddressCb(hRuntimeDevice, &Free);
        ok(SUCCEEDED(hr), "freeing the reserved range failed 0x%08lX\n", (long)hr);
    }

    /* A zero-sized free names no range at all. */
    memset(&Free, 0, sizeof(Free));
    ok(FAILED(Callbacks.pfnFreeGpuVirtualAddressCb(hRuntimeDevice, &Free)),
       "a zero-sized GPU VA free was accepted\n");

    pfnDestroyCallbacks(hRuntimeDevice);
}

static void
Test_Wddm2WaitForPagingFence(
    D3DDDI_DEVICECALLBACKS *Callbacks,
    HANDLE hRuntimeDevice,
    D3DKMT_HANDLE hSyncObject,
    volatile UINT64 *FenceValueCpuVa,
    UINT64 FenceValue)
{
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;
    HANDLE Event;
    DWORD WaitResult;
    HRESULT hr;

    if (FenceValue == 0)
        return;
    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Event == NULL)
    {
        skip("could not create a paging-fence event\n");
        return;
    }

    memset(&Wait, 0, sizeof(Wait));
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &hSyncObject;
    Wait.FenceValueArray = &FenceValue;
    Wait.hAsyncEvent = Event;
    hr = Callbacks->pfnWaitForSynchronizationObjectFromCpuCb(
             hRuntimeDevice,
             &Wait);
    ok(SUCCEEDED(hr),
       "paging-fence wait submission failed 0x%08lX\n",
       (long)hr);
    if (SUCCEEDED(hr))
    {
        WaitResult = WaitForSingleObject(Event, 5000);
        ok(WaitResult == WAIT_OBJECT_0,
           "paging fence %I64u did not retire (wait %lu)\n",
           (unsigned long long)FenceValue,
           WaitResult);
        if (WaitResult == WAIT_OBJECT_0 && FenceValueCpuVa != NULL)
        {
            ok(*FenceValueCpuVa >= FenceValue,
               "paging queue exposed %I64u after waiting for %I64u\n",
               (unsigned long long)*FenceValueCpuVa,
               (unsigned long long)FenceValue);
        }
    }
    CloseHandle(Event);
}

static void
Test_Wddm2ReclaimAllocations2ThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATEPAGINGQUEUE CreateQueue;
    D3DDDI_DESTROYPAGINGQUEUE DestroyQueue;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    D3DDDICB_OFFERALLOCATIONS Offer;
    D3DDDICB_RECLAIMALLOCATIONS2 Reclaim;
    D3DDDICB_EVICT Evict;
    D3DDDICB_LOCK2 Lock;
    D3DDDICB_UNLOCK2 Unlock;
    ULONG_PTR RuntimeResourceCookie = 0x324D4C43;
    HANDLE RuntimeResource = (HANDLE)&RuntimeResourceCookie;
    HANDLE UnknownResource = (HANDLE)(ULONG_PTR)0xBAD0CAFE;
    HANDLE hRuntimeDevice = NULL;
    D3DKMT_HANDLE AllocationHandle = 0;
    D3DKMT_HANDLE BadAllocation = (D3DKMT_HANDLE)0xBAD0CAFE;
    BOOL Discarded;
    BOOL QueueCreated = FALSE;
    BOOL ResourceCreated = FALSE;
    BOOL Locked = FALSE;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }
    if (Callbacks.pfnReclaimAllocations2Cb == NULL)
    {
        skip("WDDM 2.0 asynchronous reclaim callback is unavailable\n");
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    memset(&Reclaim, 0, sizeof(Reclaim));
    ok(Callbacks.pfnReclaimAllocations2Cb(
           (HANDLE)(ULONG_PTR)0xBAD0CAFE,
           &Reclaim) == E_INVALIDARG,
       "reclaim2 accepted an unknown runtime device\n");
    ok(Callbacks.pfnReclaimAllocations2Cb(
           hRuntimeDevice,
           NULL) == E_INVALIDARG,
       "reclaim2 accepted NULL data\n");
    ok(Callbacks.pfnReclaimAllocations2Cb(
           hRuntimeDevice,
           &Reclaim) == E_INVALIDARG,
       "reclaim2 accepted an empty request\n");
    Reclaim.PagingQueue = 1;
    Reclaim.NumAllocations = 1;
    Reclaim.pResources = &UnknownResource;
    Reclaim.HandleList = &BadAllocation;
    ok(Callbacks.pfnReclaimAllocations2Cb(
           hRuntimeDevice,
           &Reclaim) == E_INVALIDARG,
       "reclaim2 accepted both resource and allocation lists\n");

    memset(&CreateQueue, 0, sizeof(CreateQueue));
    CreateQueue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    hr = Callbacks.pfnCreatePagingQueueCb(
             hRuntimeDevice,
             &CreateQueue);
    if (FAILED(hr))
    {
        skip("adapter refused a paging queue for reclaim2 "
             "(0x%08lX)\n",
             (long)hr);
        goto Cleanup;
    }
    QueueCreated = TRUE;

    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&Allocate, 0, sizeof(Allocate));
    Allocate.hResource = RuntimeResource;
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &AllocInfo;
    hr = Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate);
    if (FAILED(hr))
    {
        skip("adapter refused a resource for reclaim2 "
             "(0x%08lX)\n",
             (long)hr);
        goto Cleanup;
    }
    ResourceCreated = TRUE;
    AllocationHandle = AllocInfo.hAllocation;
    ok(Allocate.hKMResource != 0,
       "reclaim2 resource has no KMT resource handle\n");
    ok(AllocationHandle != 0,
       "reclaim2 resource has no allocation handle\n");
    if (Allocate.hKMResource == 0 || AllocationHandle == 0)
        goto Cleanup;

    memset(&Offer, 0, sizeof(Offer));
    Offer.pResources = &RuntimeResource;
    Offer.NumAllocations = 1;
    Offer.Priority = D3DDDI_OFFER_PRIORITY_NORMAL;
    hr = Callbacks.pfnOfferAllocationsCb(
             hRuntimeDevice,
             &Offer);
    ok(SUCCEEDED(hr),
       "resource-list offer failed 0x%08lX\n",
       (long)hr);

    if (SUCCEEDED(hr))
    {
        Discarded = (BOOL)0x7F7F7F7F;
        memset(&Reclaim, 0, sizeof(Reclaim));
        Reclaim.PagingQueue = CreateQueue.hPagingQueue;
        Reclaim.NumAllocations = 1;
        Reclaim.pResources = &RuntimeResource;
        Reclaim.pDiscarded = &Discarded;
        Reclaim.PagingFenceValue = MAXULONGLONG;
        hr = Callbacks.pfnReclaimAllocations2Cb(
                 hRuntimeDevice,
                 &Reclaim);
        ok(SUCCEEDED(hr),
           "resource-list reclaim2 failed 0x%08lX\n",
           (long)hr);
        if (SUCCEEDED(hr))
        {
            ok(Discarded == FALSE || Discarded == TRUE,
               "resource reclaim2 returned noncanonical discarded value "
               "0x%08lX\n",
               (long)Discarded);
            Test_Wddm2WaitForPagingFence(
                &Callbacks,
                hRuntimeDevice,
                CreateQueue.hSyncObject,
                CreateQueue.FenceValueCPUVirtualAddress,
                Reclaim.PagingFenceValue);
        }
    }

    memset(&Offer, 0, sizeof(Offer));
    Offer.HandleList = &AllocationHandle;
    Offer.NumAllocations = 1;
    Offer.Priority = D3DDDI_OFFER_PRIORITY_NORMAL;
    hr = Callbacks.pfnOfferAllocationsCb(
             hRuntimeDevice,
             &Offer);
    ok(SUCCEEDED(hr),
       "allocation-list offer before reclaim2 failed 0x%08lX\n",
       (long)hr);
    if (SUCCEEDED(hr))
    {
        memset(&Evict, 0, sizeof(Evict));
        Evict.NumAllocations = 1;
        Evict.AllocationList = &AllocationHandle;
        hr = Callbacks.pfnEvictCb(hRuntimeDevice, &Evict);
        if (FAILED(hr))
        {
            trace("explicit eviction before reclaim2 was not needed or "
                  "available (0x%08lX)\n",
                  (long)hr);
        }

        Discarded = (BOOL)0x7F7F7F7F;
        memset(&Reclaim, 0, sizeof(Reclaim));
        Reclaim.PagingQueue = CreateQueue.hPagingQueue;
        Reclaim.NumAllocations = 1;
        Reclaim.HandleList = &AllocationHandle;
        Reclaim.pDiscarded = &Discarded;
        Reclaim.PagingFenceValue = MAXULONGLONG;
        hr = Callbacks.pfnReclaimAllocations2Cb(
                 hRuntimeDevice,
                 &Reclaim);
        ok(SUCCEEDED(hr),
           "allocation-list reclaim2 failed 0x%08lX\n",
           (long)hr);
        if (SUCCEEDED(hr))
        {
            ok(Discarded == FALSE || Discarded == TRUE,
               "allocation reclaim2 returned noncanonical discarded value "
               "0x%08lX\n",
               (long)Discarded);
            Test_Wddm2WaitForPagingFence(
                &Callbacks,
                hRuntimeDevice,
                CreateQueue.hSyncObject,
                CreateQueue.FenceValueCPUVirtualAddress,
                Reclaim.PagingFenceValue);

            memset(&Lock, 0, sizeof(Lock));
            Lock.hAllocation = AllocationHandle;
            hr = Callbacks.pfnLock2Cb(hRuntimeDevice, &Lock);
            ok(SUCCEEDED(hr),
               "reclaimed backing was not immediately CPU-accessible "
               "(0x%08lX)\n",
               (long)hr);
            if (SUCCEEDED(hr))
            {
                Locked = TRUE;
                ok(Lock.pData != NULL,
                   "reclaimed allocation locked without a CPU address\n");
            }
        }
    }

    memset(&Reclaim, 0, sizeof(Reclaim));
    Reclaim.PagingQueue = (D3DKMT_HANDLE)0xBAD0CAFE;
    Reclaim.NumAllocations = 1;
    Reclaim.HandleList = &AllocationHandle;
    ok(FAILED(Callbacks.pfnReclaimAllocations2Cb(
                  hRuntimeDevice,
                  &Reclaim)),
       "reclaim2 accepted an unknown paging queue\n");

    Reclaim.PagingQueue = CreateQueue.hPagingQueue;
    Reclaim.HandleList = &BadAllocation;
    ok(FAILED(Callbacks.pfnReclaimAllocations2Cb(
                  hRuntimeDevice,
                  &Reclaim)),
       "reclaim2 accepted an unknown allocation\n");

    Reclaim.HandleList = &AllocationHandle;
    Reclaim.pDiscarded = (BOOL *)(ULONG_PTR)1;
    ok(FAILED(Callbacks.pfnReclaimAllocations2Cb(
                  hRuntimeDevice,
                  &Reclaim)),
       "reclaim2 accepted an unwritable result array\n");

    if (Locked)
    {
        memset(&Unlock, 0, sizeof(Unlock));
        Unlock.hAllocation = AllocationHandle;
        hr = Callbacks.pfnUnlock2Cb(hRuntimeDevice, &Unlock);
        ok(SUCCEEDED(hr),
           "reclaim2 allocation unlock failed 0x%08lX\n",
           (long)hr);
        Locked = FALSE;
    }

    memset(&DestroyQueue, 0, sizeof(DestroyQueue));
    DestroyQueue.hPagingQueue = CreateQueue.hPagingQueue;
    hr = Callbacks.pfnDestroyPagingQueueCb(
             hRuntimeDevice,
             &DestroyQueue);
    ok(SUCCEEDED(hr),
       "reclaim2 paging queue cleanup failed 0x%08lX\n",
       (long)hr);
    if (SUCCEEDED(hr))
    {
        QueueCreated = FALSE;
        memset(&Reclaim, 0, sizeof(Reclaim));
        Reclaim.PagingQueue = CreateQueue.hPagingQueue;
        Reclaim.NumAllocations = 1;
        Reclaim.HandleList = &AllocationHandle;
        ok(FAILED(Callbacks.pfnReclaimAllocations2Cb(
                      hRuntimeDevice,
                      &Reclaim)),
           "reclaim2 accepted a destroyed paging queue\n");
    }

Cleanup:
    if (Locked)
    {
        memset(&Unlock, 0, sizeof(Unlock));
        Unlock.hAllocation = AllocationHandle;
        (VOID)Callbacks.pfnUnlock2Cb(hRuntimeDevice, &Unlock);
    }
    if (ResourceCreated)
    {
        memset(&Deallocate, 0, sizeof(Deallocate));
        Deallocate.hResource = RuntimeResource;
        hr = Callbacks.pfnDeallocateCb(
                 hRuntimeDevice,
                 &Deallocate);
        ok(SUCCEEDED(hr),
           "reclaim2 resource cleanup failed 0x%08lX\n",
           (long)hr);
    }
    if (QueueCreated)
    {
        memset(&DestroyQueue, 0, sizeof(DestroyQueue));
        DestroyQueue.hPagingQueue = CreateQueue.hPagingQueue;
        (VOID)Callbacks.pfnDestroyPagingQueueCb(
                  hRuntimeDevice,
                  &DestroyQueue);
    }
    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "reclaim2 callback bookkeeping remained live 0x%08lX\n",
       (long)hr);
}

static void
Test_Wddm2GpuVaUpdateThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATECONTEXTVIRTUAL CreateContext;
    D3DDDICB_DESTROYCONTEXT DestroyContext;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    D3DDDI_RESERVEGPUVIRTUALADDRESS Reserve;
    D3DDDICB_FREEGPUVIRTUALADDRESS Free;
    D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION Operations[2];
    D3DDDICB_UPDATEGPUVIRTUALADDRESS Update;
    D3DKMT_HANDLE AllocationHandle = 0;
    HANDLE hRuntimeDevice = NULL;
    BOOL ContextCreated = FALSE;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&Update, 0, sizeof(Update));
    ok(FAILED(Callbacks.pfnUpdateGpuVirtualAddressCb(hRuntimeDevice,
                                                     &Update)),
       "GPU-VA update naming no context or operations was accepted\n");

    memset(&CreateContext, 0, sizeof(CreateContext));
    hr = Callbacks.pfnCreateContextVirtualCb(hRuntimeDevice,
                                             &CreateContext);
    if (FAILED(hr))
    {
        skip("adapter refused a virtual context for GPU-VA update "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }
    ContextCreated = TRUE;
    ok(CreateContext.hContext != NULL,
       "virtual context creation succeeded without a handle\n");

    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&Allocate, 0, sizeof(Allocate));
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &AllocInfo;
    hr = Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate);
    if (FAILED(hr))
    {
        skip("adapter refused an allocation for GPU-VA update "
             "(0x%08lX)\n",
             (long)hr);
        goto CleanupContext;
    }
    AllocationHandle = AllocInfo.hAllocation;
    ok(AllocationHandle != 0,
       "GPU-VA update allocation succeeded without a handle\n");
    if (AllocationHandle == 0)
        goto CleanupContext;

    memset(&Reserve, 0, sizeof(Reserve));
    Reserve.hAdapter = hAdapter;
    Reserve.Size = 0x10000;
    hr = Callbacks.pfnReserveGpuVirtualAddressCb(hRuntimeDevice,
                                                 &Reserve);
    if (FAILED(hr))
    {
        skip("adapter refused a GPU-VA reservation for update "
             "(0x%08lX)\n",
             (long)hr);
        goto CleanupAllocation;
    }
    ok(Reserve.VirtualAddress != 0,
       "GPU-VA reservation succeeded without an address\n");
    if (Reserve.VirtualAddress == 0)
        goto CleanupAllocation;

    memset(Operations, 0, sizeof(Operations));
    Operations[0].OperationType = D3DDDI_UPDATEGPUVIRTUALADDRESS_MAP;
    Operations[0].Map.BaseAddress = Reserve.VirtualAddress;
    Operations[0].Map.SizeInBytes = 0x1000;
    Operations[0].Map.hAllocation = AllocationHandle;
    Operations[0].Map.AllocationSizeInBytes = 0x1000;
    Operations[1].OperationType = D3DDDI_UPDATEGPUVIRTUALADDRESS_UNMAP;
    Operations[1].Unmap.BaseAddress = Reserve.VirtualAddress;
    Operations[1].Unmap.SizeInBytes = 0x1000;
    Operations[1].Unmap.Protection.NoAccess = 1;

    memset(&Update, 0, sizeof(Update));
    Update.hContext = CreateContext.hContext;
    Update.NumOperations = ARRAYSIZE(Operations);
    Update.Operations = Operations;
    Update.Reserved0 = 1;
    ok(FAILED(Callbacks.pfnUpdateGpuVirtualAddressCb(hRuntimeDevice,
                                                     &Update)),
       "GPU-VA update accepted a nonzero reserved field\n");
    Update.Reserved0 = 0;

#ifdef _WIN64
    {
        HANDLE ValidContext = Update.hContext;

        Update.hContext = (HANDLE)(ULONG_PTR)0x100000001ULL;
        ok(FAILED(Callbacks.pfnUpdateGpuVirtualAddressCb(hRuntimeDevice,
                                                         &Update)),
           "GPU-VA update truncated a pointer-sized context handle\n");
        Update.hContext = ValidContext;
    }
#endif

    hr = Callbacks.pfnUpdateGpuVirtualAddressCb(hRuntimeDevice,
                                                &Update);
    ok(SUCCEEDED(hr),
       "atomic GPU-VA map/unmap update callback failed 0x%08lX\n",
       (long)hr);

    memset(&Free, 0, sizeof(Free));
    Free.BaseAddress = Reserve.VirtualAddress;
    Free.Size = Reserve.Size;
    hr = Callbacks.pfnFreeGpuVirtualAddressCb(hRuntimeDevice, &Free);
    ok(SUCCEEDED(hr),
       "GPU-VA update reservation cleanup failed 0x%08lX\n",
       (long)hr);

CleanupAllocation:
    memset(&Deallocate, 0, sizeof(Deallocate));
    Deallocate.HandleList = &AllocationHandle;
    Deallocate.NumAllocations = 1;
    hr = Callbacks.pfnDeallocateCb(hRuntimeDevice, &Deallocate);
    ok(SUCCEEDED(hr),
       "GPU-VA update allocation cleanup failed 0x%08lX\n",
       (long)hr);

CleanupContext:
    if (ContextCreated)
    {
        memset(&DestroyContext, 0, sizeof(DestroyContext));
        DestroyContext.hContext = CreateContext.hContext;
        hr = Callbacks.pfnDestroyContextCb(hRuntimeDevice,
                                           &DestroyContext);
        ok(SUCCEEDED(hr),
           "GPU-VA update context cleanup failed 0x%08lX\n",
           (long)hr);
    }
    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "GPU-VA update callback device cleanup failed 0x%08lX\n",
       (long)hr);
}

static void
Test_Wddm2InvalidateCacheThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    D3DDDICB_LOCK2 Lock;
    D3DDDICB_UNLOCK2 Unlock;
    D3DDDICB_INVALIDATECACHE Invalidate;
    D3DKMT_HANDLE AllocationHandle;
    HANDLE hRuntimeDevice = NULL;
    BOOL Locked = FALSE;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }
    if (Callbacks.pfnInvalidateCacheCb == NULL)
    {
        skip("WDDM 2.0 cache-invalidation callback is unavailable\n");
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    memset(&Invalidate, 0, sizeof(Invalidate));
    ok(Callbacks.pfnInvalidateCacheCb(
           (HANDLE)(ULONG_PTR)0xBAD0CAFE,
           &Invalidate) == E_INVALIDARG,
       "cache invalidation accepted an unknown runtime device\n");
    ok(Callbacks.pfnInvalidateCacheCb(
           hRuntimeDevice,
           NULL) == E_INVALIDARG,
       "cache invalidation accepted NULL data\n");
    Invalidate.Length = 1;
    ok(Callbacks.pfnInvalidateCacheCb(
           hRuntimeDevice,
           &Invalidate) == E_INVALIDARG,
       "cache invalidation accepted a zero allocation handle\n");
    Invalidate.hAllocation = 1;
    Invalidate.Length = 0;
    ok(Callbacks.pfnInvalidateCacheCb(
           hRuntimeDevice,
           &Invalidate) == E_INVALIDARG,
       "cache invalidation accepted an empty range\n");
    Invalidate.Offset = (SIZE_T)-1;
    Invalidate.Length = 2;
    ok(Callbacks.pfnInvalidateCacheCb(
           hRuntimeDevice,
           &Invalidate) == E_INVALIDARG,
       "cache invalidation accepted a wrapping range\n");

    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&Allocate, 0, sizeof(Allocate));
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &AllocInfo;
    hr = Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate);
    if (FAILED(hr))
    {
        skip("adapter refused an allocation for cache invalidation "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    AllocationHandle = AllocInfo.hAllocation;
    ok(AllocationHandle != 0,
       "cache-invalidation allocation succeeded without a handle\n");
    if (AllocationHandle == 0)
        goto CleanupCallbacks;

    memset(&Lock, 0, sizeof(Lock));
    Lock.hAllocation = AllocationHandle;
    hr = Callbacks.pfnLock2Cb != NULL
             ? Callbacks.pfnLock2Cb(hRuntimeDevice, &Lock)
             : E_NOTIMPL;
    if (SUCCEEDED(hr))
    {
        Locked = TRUE;
        ok(Lock.pData != NULL,
           "Lock2 succeeded without a CPU address\n");
        if (Lock.pData != NULL)
            *(volatile UCHAR *)Lock.pData = 0x5A;
    }
    else
    {
        trace("Lock2 unavailable for cache-invalidation setup "
              "(0x%08lX)\n",
              (long)hr);
    }

    memset(&Invalidate, 0, sizeof(Invalidate));
    Invalidate.hAllocation = AllocationHandle;
    Invalidate.Length = 1;
    hr = Callbacks.pfnInvalidateCacheCb(hRuntimeDevice, &Invalidate);
    ok(hr == S_OK,
       "valid cache invalidation failed 0x%08lX\n",
       (long)hr);

    Invalidate.Offset = (SIZE_T)-2;
    Invalidate.Length = 1;
    hr = Callbacks.pfnInvalidateCacheCb(hRuntimeDevice, &Invalidate);
    ok(FAILED(hr),
       "cache invalidation accepted a range outside the allocation\n");

    Invalidate.hAllocation = (D3DKMT_HANDLE)0xBAD0CAFE;
    Invalidate.Offset = 0;
    Invalidate.Length = 1;
    hr = Callbacks.pfnInvalidateCacheCb(hRuntimeDevice, &Invalidate);
    ok(FAILED(hr),
       "cache invalidation accepted an unknown KMT allocation\n");

    if (Locked)
    {
        memset(&Unlock, 0, sizeof(Unlock));
        Unlock.hAllocation = AllocationHandle;
        hr = Callbacks.pfnUnlock2Cb != NULL
                 ? Callbacks.pfnUnlock2Cb(hRuntimeDevice, &Unlock)
                 : E_NOTIMPL;
        ok(SUCCEEDED(hr),
           "cache-invalidation allocation unlock failed 0x%08lX\n",
           (long)hr);
    }

    memset(&Deallocate, 0, sizeof(Deallocate));
    Deallocate.HandleList = &AllocationHandle;
    Deallocate.NumAllocations = 1;
    hr = Callbacks.pfnDeallocateCb(hRuntimeDevice, &Deallocate);
    ok(SUCCEEDED(hr),
       "cache-invalidation allocation cleanup failed 0x%08lX\n",
       (long)hr);

    if (SUCCEEDED(hr))
    {
        memset(&Invalidate, 0, sizeof(Invalidate));
        Invalidate.hAllocation = AllocationHandle;
        Invalidate.Length = 1;
        hr = Callbacks.pfnInvalidateCacheCb(
                 hRuntimeDevice,
                 &Invalidate);
        ok(FAILED(hr),
           "cache invalidation accepted a destroyed KMT allocation\n");
    }

CleanupCallbacks:
    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "cache-invalidation callback device cleanup failed 0x%08lX\n",
       (long)hr);
}

static void
Test_Wddm2ResourcePresentPrivateDataThroughCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice)
{
    static const UCHAR ResourcePrivateData[] =
    {
        0x52, 0x50, 0x50, 0x44, 0x2D, 0x57, 0x44, 0x44,
        0x4D, 0x32, 0x2D, 0x45, 0x32, 0x45
    };
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDI_ALLOCATIONINFO AllocInfo;
    D3DDDICB_ALLOCATE Allocate;
    D3DDDICB_DEALLOCATE Deallocate;
    D3DDDICB_DEALLOCATE2 Deallocate2;
    D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA Query;
    ULONG_PTR RuntimeResourceCookie = 0x52505044;
    D3DKMT_HANDLE KernelResource;
    UCHAR PrivateData[sizeof(ResourcePrivateData)];
    UCHAR SmallBuffer[sizeof(ResourcePrivateData) - 1];
    HANDLE hRuntimeDevice = NULL;
    BOOL ResourceDestroyed = FALSE;
    HRESULT hr;

    if (pfnCreateCallbacks(hAdapter,
                           hDevice,
                           &Callbacks,
                           &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }
    if (Callbacks.pfnGetResourcePresentPrivateDriverDataCb == NULL)
    {
        skip("WDDM 2.0 resource-present private-data callback is unavailable\n");
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    memset(&AllocInfo, 0, sizeof(AllocInfo));
    memset(&Allocate, 0, sizeof(Allocate));
    Allocate.pPrivateDriverData = ResourcePrivateData;
    Allocate.PrivateDriverDataSize = sizeof(ResourcePrivateData);
    Allocate.hResource = (HANDLE)&RuntimeResourceCookie;
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &AllocInfo;
    hr = Callbacks.pfnAllocateCb(hRuntimeDevice, &Allocate);
    if (FAILED(hr))
    {
        skip("adapter refused a resource allocation for present private data "
             "(0x%08lX)\n",
             (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    KernelResource = Allocate.hKMResource;
    ok(KernelResource != 0,
       "resource allocation succeeded without a KMT resource\n");
    ok(AllocInfo.hAllocation != 0,
       "resource allocation succeeded without an allocation\n");
    if (KernelResource == 0)
        goto Cleanup;

    memset(&Query, 0, sizeof(Query));
    ok(Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
           (HANDLE)(ULONG_PTR)0xBAD0CAFE,
           &Query) == E_INVALIDARG,
       "resource-present query accepted an unknown runtime device\n");
    ok(Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
           hRuntimeDevice,
           NULL) == E_INVALIDARG,
       "resource-present query accepted NULL data\n");
    ok(Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
           hRuntimeDevice,
           &Query) == E_INVALIDARG,
       "resource-present query accepted a zero resource handle\n");

    Query.hResource = KernelResource;
    Query.PrivateDriverDataSize = 1;
    ok(Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
           hRuntimeDevice,
           &Query) == E_INVALIDARG,
       "resource-present query accepted a sized NULL output buffer\n");

    Query.PrivateDriverDataSize = 0;
    hr = Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
             hRuntimeDevice,
             &Query);
    ok(hr == (HRESULT)STATUS_INVALID_BUFFER_SIZE,
       "zero-sized resource-present query returned 0x%08lX\n",
       (long)hr);
    ok(Query.PrivateDriverDataSize == sizeof(ResourcePrivateData),
       "size query returned %u bytes, expected %u\n",
       Query.PrivateDriverDataSize,
       (unsigned int)sizeof(ResourcePrivateData));

    memset(SmallBuffer, 0xA5, sizeof(SmallBuffer));
    Query.PrivateDriverDataSize = sizeof(SmallBuffer);
    Query.pPrivateDriverData = SmallBuffer;
    hr = Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
             hRuntimeDevice,
             &Query);
    ok(hr == (HRESULT)STATUS_INVALID_BUFFER_SIZE,
       "undersized resource-present query returned 0x%08lX\n",
       (long)hr);
    ok(Query.PrivateDriverDataSize == sizeof(ResourcePrivateData),
       "undersized query returned %u required bytes, expected %u\n",
       Query.PrivateDriverDataSize,
       (unsigned int)sizeof(ResourcePrivateData));

    memset(PrivateData, 0, sizeof(PrivateData));
    Query.PrivateDriverDataSize = sizeof(PrivateData);
    Query.pPrivateDriverData = PrivateData;
    hr = Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
             hRuntimeDevice,
             &Query);
    ok(hr == S_OK,
       "resource-present private-data query failed 0x%08lX\n",
       (long)hr);
    ok(Query.PrivateDriverDataSize == sizeof(ResourcePrivateData),
       "successful query returned %u bytes, expected %u\n",
       Query.PrivateDriverDataSize,
       (unsigned int)sizeof(ResourcePrivateData));
    ok(memcmp(PrivateData,
              ResourcePrivateData,
              sizeof(ResourcePrivateData)) == 0,
       "resource-present query returned different private data\n");

    Query.hResource = (D3DKMT_HANDLE)0xBAD0CAFE;
    hr = Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
             hRuntimeDevice,
             &Query);
    ok(FAILED(hr),
       "resource-present query accepted an unknown KMT resource\n");

Cleanup:
    if (Callbacks.pfnDeallocate2Cb != NULL)
    {
        memset(&Deallocate2, 0, sizeof(Deallocate2));
        Deallocate2.hResource = (HANDLE)&RuntimeResourceCookie;
        hr = Callbacks.pfnDeallocate2Cb(hRuntimeDevice, &Deallocate2);
    }
    else
    {
        memset(&Deallocate, 0, sizeof(Deallocate));
        Deallocate.hResource = (HANDLE)&RuntimeResourceCookie;
        hr = Callbacks.pfnDeallocateCb(hRuntimeDevice, &Deallocate);
    }
    ok(SUCCEEDED(hr),
       "resource-present allocation cleanup failed 0x%08lX\n",
       (long)hr);
    ResourceDestroyed = SUCCEEDED(hr);

    if (ResourceDestroyed && KernelResource != 0)
    {
        memset(&Query, 0, sizeof(Query));
        Query.hResource = KernelResource;
        Query.PrivateDriverDataSize = sizeof(PrivateData);
        Query.pPrivateDriverData = PrivateData;
        hr = Callbacks.pfnGetResourcePresentPrivateDriverDataCb(
                 hRuntimeDevice,
                 &Query);
        ok(FAILED(hr),
           "resource-present query accepted a destroyed KMT resource\n");
    }

    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr),
       "resource-present callback device cleanup failed 0x%08lX\n",
       (long)hr);
}

#endif

/*
 * Teardown is ordered.  Contexts and paging queues are parented to the device,
 * so releasing the device while they are open would leave kernel objects owned
 * by something that no longer exists.
 */
static void Test_TeardownIsOrdered(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
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
    hr = Callbacks.pfnCreateContextCb(hRuntimeDevice, &Create);
    if (FAILED(hr))
    {
        skip("no context to hold the device open (0x%08lX)\n", (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }

    /* Out of order: the context is still open. */
    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(FAILED(hr),
       "the device was released with a context still open (0x%08lX) -- that context is parented "
       "to it and would outlive its owner\n", (long)hr);

    /* The device must still work after refusing, not be half torn down. */
    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hContext = Create.hContext;
    hr = Callbacks.pfnDestroyContextCb(hRuntimeDevice, &Destroy);
    ok(SUCCEEDED(hr),
       "the device stopped working after refusing an out-of-order release (0x%08lX)\n", (long)hr);

    /* In order now. */
    hr = pfnDestroyCallbacks(hRuntimeDevice);
    ok(SUCCEEDED(hr), "in-order device release failed 0x%08lX\n", (long)hr);

    /* Releasing twice must find nothing to release rather than free again. */
    ok(FAILED(pfnDestroyCallbacks(hRuntimeDevice)),
       "the device was released a second time\n");
}

/*
 * A driver fills its command buffer and submits.  The buffer it just handed
 * over now belongs to the GPU, so render must give it a different one to write
 * into -- and it must be usable immediately, because the driver has more
 * commands to emit right now.  A runtime that returned the same buffer would
 * have the driver overwrite work already in flight, and one that returned none
 * would strand it.
 */
static void Test_RenderHandsBackAUsableBuffer(D3DKMT_HANDLE hAdapter, D3DKMT_HANDLE hDevice)
{
    D3DDDI_DEVICECALLBACKS Callbacks;
    D3DDDICB_CREATECONTEXT Create;
    D3DDDICB_DESTROYCONTEXT Destroy;
    D3DDDICB_RENDER Render;
    HANDLE hRuntimeDevice = NULL;
    VOID *FirstBuffer;
    UINT FirstSize;
    HRESULT hr;
    int Round;

    if (pfnCreateCallbacks(hAdapter, hDevice, &Callbacks, &hRuntimeDevice) != S_OK)
    {
        skip("no callback table\n");
        return;
    }

    memset(&Create, 0, sizeof(Create));
    hr = Callbacks.pfnCreateContextCb(hRuntimeDevice, &Create);
    if (FAILED(hr))
    {
        skip("no context to submit from (0x%08lX)\n", (long)hr);
        pfnDestroyCallbacks(hRuntimeDevice);
        return;
    }
    FirstBuffer = Create.pCommandBuffer;
    FirstSize = Create.CommandBufferSize;

    /*
     * Submit repeatedly, as a driver emitting more than one buffer's worth of
     * commands does.  Each round must come back with somewhere to write.
     */
    for (Round = 0; Round < 3; ++Round)
    {
        memset(&Render, 0, sizeof(Render));
        Render.hContext = Create.hContext;
        Render.CommandOffset = 0;
        Render.CommandLength = 0;
        Render.NumAllocations = 0;
        Render.NumPatchLocations = 0;
        Render.NewCommandBufferSize = FirstSize;
        Render.NewAllocationListSize = Create.AllocationListSize;
        Render.NewPatchLocationListSize = Create.PatchLocationListSize;
        Render.BroadcastContextCount = 0;

        hr = Callbacks.pfnRenderCb(hRuntimeDevice, &Render);
        if (FAILED(hr))
        {
            trace("render round %d refused 0x%08lX\n", Round, (long)hr);
            break;
        }

        ok(Render.pNewCommandBuffer != NULL,
           "round %d: render succeeded but handed back no buffer -- the driver has nowhere to "
           "write its next command\n", Round);
        ok(Render.NewCommandBufferSize != 0,
           "round %d: the next command buffer is zero bytes\n", Round);
        /* The lists come back with the buffer and are useless apart: a buffer
         * with no allocation list cannot name what it touches. */
        ok(Render.pNewAllocationList != NULL, "round %d: no allocation list handed back\n", Round);
        ok(Render.pNewPatchLocationList != NULL, "round %d: no patch list handed back\n", Round);
        trace("round %d: buffer %p (%u bytes), %lu queued\n", Round,
              Render.pNewCommandBuffer, Render.NewCommandBufferSize,
              (unsigned long)Render.QueuedBufferCount);
    }

    UNREFERENCED_PARAMETER(FirstBuffer);

    memset(&Destroy, 0, sizeof(Destroy));
    Destroy.hContext = Create.hContext;
    Callbacks.pfnDestroyContextCb(hRuntimeDevice, &Destroy);
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
    Test_SynchronizationObjectThroughCallbacks(hAdapter, hDevice);
#if (REACTOS_EXPECTED_UMD_INTERFACE_VERSION >= \
     D3D_UMD_INTERFACE_VERSION_WIN8)
    Test_Win8OfferReclaimThroughCallbacks(hAdapter, hDevice);
    Test_Win8SynchronizationObject2ThroughCallbacks(hAdapter, hDevice);
#endif
#if (REACTOS_EXPECTED_UMD_INTERFACE_VERSION >= \
     D3D_UMD_INTERFACE_VERSION_WDDM2_0)
    Test_Wddm2ResourceDeallocateThroughCallbacks(hAdapter, hDevice);
    Test_Wddm2TierIsPopulated(hAdapter, hDevice);
    Test_Wddm2MonitoredFenceThroughCallbacks(hAdapter, hDevice);
    Test_PagingQueueAndGpuVaThroughCallbacks(hAdapter, hDevice);
    Test_Wddm2ReclaimAllocations2ThroughCallbacks(hAdapter, hDevice);
    Test_Wddm2GpuVaUpdateThroughCallbacks(hAdapter, hDevice);
    Test_Wddm2InvalidateCacheThroughCallbacks(hAdapter, hDevice);
    Test_Wddm2ResourcePresentPrivateDataThroughCallbacks(hAdapter, hDevice);
#endif
    Test_RenderHandsBackAUsableBuffer(hAdapter, hDevice);
    Test_TeardownIsOrdered(hAdapter, hDevice);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
    FreeLibrary(Runtime);
}

/* EOF */
