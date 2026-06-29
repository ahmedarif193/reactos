/*
 * PROJECT:     ReactOS Direct3D 9 WDDM Bridge
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     WDDM UMD loading, DDI exchange, D3DKMT present path
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 *
 * This module implements the WDDM (Windows Display Driver Model) code path
 * for d3d9.dll. On Windows Vista+, d3d9 loads a User-Mode Driver (UMD)
 * dynamically and communicates with the kernel through D3DKMT thunks
 * exported by gdi32.dll.
 *
 * Flow:
 *   1. D3DKMTOpenAdapterFromHdc -> get adapter handle
 *   2. D3DKMTQueryAdapterInfo(UMDRIVERNAME) -> get UMD DLL path
 *   3. LoadLibrary(UMD) -> GetProcAddress("OpenAdapter")
 *   4. UMD->OpenAdapter -> get adapter DDI table
 *   5. D3DKMTCreateDevice -> kernel device object
 *   6. D3DKMTCreateContext -> kernel GPU context
 *   7. UMD->CreateDevice -> get device DDI table, passing callbacks
 *
 * The runtime callback table (AllocateCb, RenderCb, PresentCb, etc.)
 * wraps D3DKMT* calls so the UMD doesn't need to know about kernel thunks.
 */

#include "config.h"
#include "d3d9_private.h"
#include "d3d9_wddm.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d9);

/* ========================================================================
 * D3DKMT function pointers - resolved from gdi32.dll at runtime
 *
 * d3d9.dll on Windows imports these from gdi32.dll. We resolve them
 * dynamically to avoid hard link dependency in the Wine build.
 * ====================================================================== */

typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromHdc)(D3DKMT_OPENADAPTERFROMHDC *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCloseAdapter)(const D3DKMT_CLOSEADAPTER *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryAdapterInfo)(const D3DKMT_QUERYADAPTERINFO *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCreateDevice)(D3DKMT_CREATEDEVICE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTDestroyDevice)(const D3DKMT_DESTROYDEVICE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCreateContext)(D3DKMT_CREATECONTEXT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTDestroyContext)(const D3DKMT_DESTROYCONTEXT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTPresent)(D3DKMT_PRESENT *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCreateAllocation)(D3DKMT_CREATEALLOCATION *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTDestroyAllocation)(const D3DKMT_DESTROYALLOCATION *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTRender)(D3DKMT_RENDER *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTLock)(D3DKMT_LOCK *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTUnlock)(const D3DKMT_UNLOCK *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTEscape)(const D3DKMT_ESCAPE *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromGdiDisplayName)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *);

static struct
{
    BOOL                                    Initialized;
    BOOL                                    Available;
    HMODULE                                 hGdi32;
    PFN_D3DKMTOpenAdapterFromHdc            pfnOpenAdapterFromHdc;
    PFN_D3DKMTCloseAdapter                  pfnCloseAdapter;
    PFN_D3DKMTQueryAdapterInfo              pfnQueryAdapterInfo;
    PFN_D3DKMTCreateDevice                  pfnCreateDevice;
    PFN_D3DKMTDestroyDevice                 pfnDestroyDevice;
    PFN_D3DKMTCreateContext                 pfnCreateContext;
    PFN_D3DKMTDestroyContext                pfnDestroyContext;
    PFN_D3DKMTPresent                       pfnPresent;
    PFN_D3DKMTCreateAllocation              pfnCreateAllocation;
    PFN_D3DKMTDestroyAllocation             pfnDestroyAllocation;
    PFN_D3DKMTRender                        pfnRender;
    PFN_D3DKMTLock                          pfnLock;
    PFN_D3DKMTUnlock                        pfnUnlock;
    PFN_D3DKMTEscape                        pfnEscape;
    PFN_D3DKMTOpenAdapterFromGdiDisplayName pfnOpenAdapterFromGdiDisplayName;
} d3dkmt_thunks;

static D3D9_WDDM_DEVICE *d3d9_wddm_validate_device_handle(HANDLE hDevice)
{
    D3D9_WDDM_DEVICE *device = (D3D9_WDDM_DEVICE *)hDevice;

    if (!device || !device->pAdapter || !device->hDevice)
        return NULL;

    return device;
}

static BOOL d3d9_wddm_resolve_thunks(void)
{
    if (d3dkmt_thunks.Initialized)
        return d3dkmt_thunks.Available;

    d3dkmt_thunks.Initialized = TRUE;
    d3dkmt_thunks.Available = FALSE;

    d3dkmt_thunks.hGdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!d3dkmt_thunks.hGdi32)
    {
        WARN("gdi32.dll not loaded, WDDM path unavailable\n");
        return FALSE;
    }

#define RESOLVE(name) \
    d3dkmt_thunks.pfn##name = (PFN_D3DKMT##name)GetProcAddress(d3dkmt_thunks.hGdi32, "D3DKMT" #name); \
    if (!d3dkmt_thunks.pfn##name) \
    { \
        WARN("D3DKMT" #name " not found in gdi32.dll\n"); \
        return FALSE; \
    }

    RESOLVE(OpenAdapterFromHdc);
    RESOLVE(CloseAdapter);
    RESOLVE(QueryAdapterInfo);
    RESOLVE(CreateDevice);
    RESOLVE(DestroyDevice);
    RESOLVE(CreateContext);
    RESOLVE(DestroyContext);
    RESOLVE(Present);
    RESOLVE(CreateAllocation);
    RESOLVE(DestroyAllocation);
    RESOLVE(Render);
    RESOLVE(Lock);
    RESOLVE(Unlock);
    RESOLVE(Escape);

#undef RESOLVE

    /* OpenAdapterFromGdiDisplayName is optional (used by open_adapter_by_name) */
    d3dkmt_thunks.pfnOpenAdapterFromGdiDisplayName =
        (PFN_D3DKMTOpenAdapterFromGdiDisplayName)
        GetProcAddress(d3dkmt_thunks.hGdi32, "D3DKMTOpenAdapterFromGdiDisplayName");

    d3dkmt_thunks.Available = TRUE;
    TRACE("WDDM D3DKMT thunks resolved from gdi32.dll\n");
    return TRUE;
}

/* ========================================================================
 * WDDM Availability Check
 * ====================================================================== */

BOOL d3d9_wddm_is_available(void)
{
    return FALSE;
}

/* ========================================================================
 * UMD Discovery and Loading
 *
 * The runtime discovers the UMD DLL path through:
 *   D3DKMTQueryAdapterInfo(KMTQAITYPE_UMDRIVERNAME)
 * The kernel reads this from the driver's registry key
 * "UserModeDriverName" (multi-sz, one entry per D3D version).
 * ====================================================================== */

static HRESULT d3d9_wddm_load_umd(D3D9_WDDM_ADAPTER *adapter)
{
    D3DKMT_QUERYADAPTERINFO queryInfo;
    D3DKMT_UMDFILENAMEINFO driverName;
    NTSTATUS status;

    if (!adapter || !adapter->hAdapter)
        return D3DERR_INVALIDCALL;

    memset(&driverName, 0, sizeof(driverName));
    driverName.Version = KMTUMDVERSION_DX9;

    memset(&queryInfo, 0, sizeof(queryInfo));
    queryInfo.hAdapter = adapter->hAdapter;
    queryInfo.Type = KMTQAITYPE_UMDRIVERNAME;
    queryInfo.pPrivateDriverData = &driverName;
    queryInfo.PrivateDriverDataSize = sizeof(driverName);

    status = d3dkmt_thunks.pfnQueryAdapterInfo(&queryInfo);
    if (status != 0)
    {
        WARN("D3DKMTQueryAdapterInfo(UMDRIVERNAME) failed: 0x%08lx\n",
             status);
        return D3DERR_NOTAVAILABLE;
    }

    TRACE("UMD DLL path: %s\n", debugstr_w(driverName.UmdFileName));

    if (!driverName.UmdFileName[0])
    {
        WARN("Empty UMD driver name returned\n");
        return D3DERR_NOTAVAILABLE;
    }

    /* Load the UMD DLL */
    adapter->hUmdModule = LoadLibraryW(driverName.UmdFileName);
    if (!adapter->hUmdModule)
    {
        WARN("Could not load user mode driver DLL: %s (error %lu)\n",
             debugstr_w(driverName.UmdFileName), GetLastError());
        return D3DERR_NOTAVAILABLE;
    }

    /* Get the OpenAdapter entry point */
    adapter->pfnOpenAdapter = (PFND3DDDI_OPENADAPTER)
        GetProcAddress(adapter->hUmdModule, "OpenAdapter");
    if (!adapter->pfnOpenAdapter)
    {
        WARN("Could not find function OpenAdapter in user mode driver DLL\n");
        FreeLibrary(adapter->hUmdModule);
        adapter->hUmdModule = NULL;
        return D3DERR_NOTAVAILABLE;
    }

    TRACE("UMD loaded successfully, OpenAdapter at %p\n", adapter->pfnOpenAdapter);
    return S_OK;
}

/* ========================================================================
 * OpenAdapter - Call UMD's OpenAdapter to get adapter DDI
 * ====================================================================== */

static HRESULT d3d9_wddm_call_open_adapter(D3D9_WDDM_ADAPTER *adapter)
{
    if (!adapter || !adapter->hAdapter || !adapter->pfnOpenAdapter)
        return D3DERR_INVALIDCALL;

    /*
     * The local d3d9 UMD DDI declarations are intentionally incomplete.
     * Calling a real driver's OpenAdapter with a guessed ABI table can corrupt
     * the handoff, so fail closed until this path uses authoritative headers.
     */
    WARN("D3D9 WDDM UMD OpenAdapter is not supported by this runtime ABI yet\n");
    return D3DERR_NOTAVAILABLE;
}

/* ========================================================================
 * Open Adapter (public API)
 * ====================================================================== */

HRESULT d3d9_wddm_open_adapter(HDC hdc, D3D9_WDDM_ADAPTER *adapter)
{
    D3DKMT_OPENADAPTERFROMHDC openData;
    NTSTATUS status;
    HRESULT hr;

    if (!adapter || !hdc)
        return D3DERR_INVALIDCALL;

    if (!d3d9_wddm_resolve_thunks())
        return D3DERR_NOTAVAILABLE;

    memset(adapter, 0, sizeof(*adapter));

    /* Step 1: Open adapter via HDC */
    memset(&openData, 0, sizeof(openData));
    openData.hDc = hdc;

    status = d3dkmt_thunks.pfnOpenAdapterFromHdc(&openData);
    if (status != 0)
    {
        WARN("D3DKMTOpenAdapterFromHdc failed: 0x%08lx\n", status);
        return D3DERR_NOTAVAILABLE;
    }

    adapter->hAdapter = openData.hAdapter;
    adapter->AdapterLuid = openData.AdapterLuid;
    adapter->VidPnSourceId = openData.VidPnSourceId;

    TRACE("Adapter opened: handle=0x%x, LUID=%08lx:%08lx, VidPnSource=%u\n",
          adapter->hAdapter,
          adapter->AdapterLuid.HighPart, adapter->AdapterLuid.LowPart,
          adapter->VidPnSourceId);

    /* Step 2: Discover and load UMD */
    hr = d3d9_wddm_load_umd(adapter);
    if (FAILED(hr))
    {
        d3d9_wddm_close_adapter(adapter);
        return hr;
    }

    /* Step 3: Call UMD's OpenAdapter */
    hr = d3d9_wddm_call_open_adapter(adapter);
    if (FAILED(hr))
    {
        d3d9_wddm_close_adapter(adapter);
        return hr;
    }

    return S_OK;
}

HRESULT d3d9_wddm_open_adapter_by_name(const WCHAR *device_name,
                                        D3D9_WDDM_ADAPTER *adapter)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openData;
    NTSTATUS status;
    HRESULT hr;

    if (!adapter || !device_name || !device_name[0])
        return D3DERR_INVALIDCALL;

    if (!d3d9_wddm_resolve_thunks())
        return D3DERR_NOTAVAILABLE;

    if (!d3dkmt_thunks.pfnOpenAdapterFromGdiDisplayName)
        return D3DERR_NOTAVAILABLE;

    memset(adapter, 0, sizeof(*adapter));

    memset(&openData, 0, sizeof(openData));
    lstrcpynW(openData.DeviceName, device_name,
              sizeof(openData.DeviceName) / sizeof(WCHAR));

    status = d3dkmt_thunks.pfnOpenAdapterFromGdiDisplayName(&openData);
    if (status != 0)
    {
        WARN("D3DKMTOpenAdapterFromGdiDisplayName(%s) failed: 0x%08lx\n",
             debugstr_w(device_name), status);
        return D3DERR_NOTAVAILABLE;
    }

    adapter->hAdapter = openData.hAdapter;
    adapter->AdapterLuid = openData.AdapterLuid;
    adapter->VidPnSourceId = openData.VidPnSourceId;

    TRACE("Adapter opened by name: handle=0x%x\n", adapter->hAdapter);

    hr = d3d9_wddm_load_umd(adapter);
    if (FAILED(hr))
    {
        d3d9_wddm_close_adapter(adapter);
        return hr;
    }

    hr = d3d9_wddm_call_open_adapter(adapter);
    if (FAILED(hr))
    {
        d3d9_wddm_close_adapter(adapter);
        return hr;
    }

    return S_OK;
}

/* ========================================================================
 * Close Adapter
 * ====================================================================== */

void d3d9_wddm_close_adapter(D3D9_WDDM_ADAPTER *adapter)
{
    if (!adapter)
        return;

    /* Close UMD adapter */
    if (adapter->hUmdAdapter && adapter->AdapterFuncs.pfnCloseAdapter)
    {
        adapter->AdapterFuncs.pfnCloseAdapter(adapter->hUmdAdapter);
        adapter->hUmdAdapter = NULL;
    }

    /* Unload UMD DLL */
    if (adapter->hUmdModule)
    {
        FreeLibrary(adapter->hUmdModule);
        adapter->hUmdModule = NULL;
    }

    /* Close kernel adapter */
    if (adapter->hAdapter)
    {
        D3DKMT_CLOSEADAPTER closeData;
        memset(&closeData, 0, sizeof(closeData));
        closeData.hAdapter = adapter->hAdapter;
        d3dkmt_thunks.pfnCloseAdapter(&closeData);
        adapter->hAdapter = 0;
    }
}

/* ========================================================================
 * Device-Level Runtime Callbacks (provided TO the UMD)
 *
 * These are the "CB" callbacks. The UMD calls these when it needs
 * kernel services. We forward to D3DKMT*.
 * ====================================================================== */

static HRESULT APIENTRY d3d9_wddm_allocate_cb(
    HANDLE hDevice, D3DDDICB_ALLOCATE *pData)
{
    D3DKMT_CREATEALLOCATION createAlloc;
    D3D9_WDDM_DEVICE *device = d3d9_wddm_validate_device_handle(hDevice);
    NTSTATUS status;

    TRACE("AllocateCb: device=%p, NumAllocations=%u\n",
          hDevice, pData ? pData->NumAllocations : 0);

    if (!device || !pData)
        return E_INVALIDARG;
    if (!pData->NumAllocations || !pData->pAllocationInfo)
        return E_INVALIDARG;
    if (pData->PrivateDriverDataSize && !pData->pPrivateDriverData)
        return E_INVALIDARG;

    /*
     * Forward to D3DKMTCreateAllocation.
     * The UMD passes private driver data that the KMD understands.
     */
    memset(&createAlloc, 0, sizeof(createAlloc));
    createAlloc.hDevice = device->hDevice;
    createAlloc.hResource = (D3DKMT_HANDLE)(ULONG_PTR)pData->hResource;
    createAlloc.pPrivateRuntimeData = NULL;
    createAlloc.PrivateRuntimeDataSize = 0;
    createAlloc.pPrivateDriverData = pData->pPrivateDriverData;
    createAlloc.PrivateDriverDataSize = pData->PrivateDriverDataSize;
    createAlloc.NumAllocations = pData->NumAllocations;
    createAlloc.pAllocationInfo = pData->pAllocationInfo;

    status = d3dkmt_thunks.pfnCreateAllocation(&createAlloc);
    if (status != 0)
    {
        WARN("D3DKMTCreateAllocation failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    pData->hKMResource = createAlloc.hResource;
    return S_OK;
}

static HRESULT APIENTRY d3d9_wddm_deallocate_cb(
    HANDLE hDevice, const D3DDDICB_DEALLOCATE *pData)
{
    D3D9_WDDM_DEVICE *device = d3d9_wddm_validate_device_handle(hDevice);
    D3DKMT_DESTROYALLOCATION destroyAlloc;
    NTSTATUS status;

    TRACE("DeallocateCb: device=%p\n", hDevice);

    if (!device || !pData)
        return E_INVALIDARG;
    if (!pData->hResource && (!pData->NumAllocations || !pData->HandleList))
        return E_INVALIDARG;

    memset(&destroyAlloc, 0, sizeof(destroyAlloc));
    destroyAlloc.hDevice = device->hDevice;
    destroyAlloc.hResource = (D3DKMT_HANDLE)(ULONG_PTR)pData->hResource;
    destroyAlloc.AllocationCount = pData->NumAllocations;
    destroyAlloc.phAllocationList = pData->HandleList;

    status = d3dkmt_thunks.pfnDestroyAllocation(&destroyAlloc);
    if (status != 0)
    {
        WARN("D3DKMTDestroyAllocation failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT APIENTRY d3d9_wddm_lock_cb(
    HANDLE hDevice, D3DDDICB_LOCK *pData)
{
    D3D9_WDDM_DEVICE *device = d3d9_wddm_validate_device_handle(hDevice);
    D3DKMT_LOCK lockData;
    NTSTATUS status;

    TRACE("LockCb: device=%p, alloc=0x%x\n",
          hDevice, pData ? pData->hAllocation : 0);

    if (!device || !pData || !pData->hAllocation)
        return E_INVALIDARG;

    memset(&lockData, 0, sizeof(lockData));
    lockData.hDevice = device->hDevice;
    lockData.hAllocation = pData->hAllocation;
    lockData.PrivateDriverData = pData->PrivateDriverData;
    lockData.NumPages = pData->NumPages;
    lockData.pPages = pData->pPages;
    lockData.Flags = pData->Flags;

    status = d3dkmt_thunks.pfnLock(&lockData);
    if (status != 0)
    {
        WARN("D3DKMTLock failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    pData->pData = lockData.pData;
    return S_OK;
}

static HRESULT APIENTRY d3d9_wddm_unlock_cb(
    HANDLE hDevice, const D3DDDICB_UNLOCK *pData)
{
    D3D9_WDDM_DEVICE *device = d3d9_wddm_validate_device_handle(hDevice);
    D3DKMT_UNLOCK unlockData;
    NTSTATUS status;

    TRACE("UnlockCb: device=%p\n", hDevice);

    if (!device || !pData)
        return E_INVALIDARG;
    if (!pData->NumAllocations || !pData->phAllocations)
        return E_INVALIDARG;

    memset(&unlockData, 0, sizeof(unlockData));
    unlockData.hDevice = device->hDevice;
    unlockData.NumAllocations = pData->NumAllocations;
    unlockData.phAllocations = pData->phAllocations;

    status = d3dkmt_thunks.pfnUnlock(&unlockData);
    if (status != 0)
    {
        WARN("D3DKMTUnlock failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT APIENTRY d3d9_wddm_render_cb(
    HANDLE hDevice, D3DDDICB_RENDER *pData)
{
    D3D9_WDDM_DEVICE *device = d3d9_wddm_validate_device_handle(hDevice);
    D3DKMT_RENDER renderData;
    NTSTATUS status;

    TRACE("RenderCb: device=%p, cmdLen=%u\n",
          hDevice, pData ? pData->CommandLength : 0);

    if (!device || !pData)
        return E_INVALIDARG;
    if (pData->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
        return E_INVALIDARG;
    if (pData->PrivateDriverDataSize && !pData->pPrivateDriverData)
        return E_INVALIDARG;

    memset(&renderData, 0, sizeof(renderData));
    renderData.hContext = pData->hContext ?
        (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext : device->hContext;
    renderData.CommandLength = pData->CommandLength;
    renderData.CommandOffset = pData->CommandOffset;
    renderData.AllocationCount = pData->NumAllocations;
    renderData.PatchLocationCount = pData->NumPatchLocations;
    renderData.Flags = pData->Flags;
    renderData.BroadcastContextCount = pData->BroadcastContextCount;
    renderData.pPrivateDriverData = pData->pPrivateDriverData;
    renderData.PrivateDriverDataSize = pData->PrivateDriverDataSize;
    if (pData->BroadcastContextCount)
    {
        UINT i;
        for (i = 0; i < pData->BroadcastContextCount; ++i)
            renderData.BroadcastContext[i] =
                (D3DKMT_HANDLE)(ULONG_PTR)pData->BroadcastContext[i];
    }

    status = d3dkmt_thunks.pfnRender(&renderData);
    if (status != 0)
    {
        WARN("D3DKMTRender failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    /* Return new command buffer to UMD for next batch */
    pData->pNewCommandBuffer = renderData.pNewCommandBuffer;
    pData->NewCommandBufferSize = renderData.NewCommandBufferSize;
    pData->pNewAllocationList = renderData.pNewAllocationList;
    pData->NewAllocationListSize = renderData.NewAllocationListSize;
    pData->pNewPatchLocationList = renderData.pNewPatchLocationList;
    pData->NewPatchLocationListSize = renderData.NewPatchLocationListSize;
    pData->QueuedBufferCount = renderData.QueuedBufferCount;
    pData->NewCommandBuffer = renderData.NewCommandBuffer;

    return S_OK;
}

static HRESULT APIENTRY d3d9_wddm_present_cb(
    HANDLE hDevice, const D3DDDICB_PRESENT *pData)
{
    D3D9_WDDM_DEVICE *device = d3d9_wddm_validate_device_handle(hDevice);
    D3DKMT_PRESENT presentData;
    NTSTATUS status;

    TRACE("PresentCb: device=%p, src=0x%x, dst=0x%x\n",
          hDevice,
          pData ? pData->hSrcAllocation : 0,
          pData ? pData->hDstAllocation : 0);

    if (!device || !pData)
        return E_INVALIDARG;
    if (!pData->hSrcAllocation || pData->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
        return E_INVALIDARG;

    memset(&presentData, 0, sizeof(presentData));
    /* hDevice and hContext are a union; use hContext for present */
    presentData.hContext = device->hContext;
    presentData.hSource = pData->hSrcAllocation;
    presentData.hDestination = pData->hDstAllocation;
    presentData.BroadcastContextCount = pData->BroadcastContextCount;

    if (pData->BroadcastContextCount > 0)
    {
        UINT i;
        for (i = 0; i < pData->BroadcastContextCount; i++)
            presentData.BroadcastContext[i] = (D3DKMT_HANDLE)(ULONG_PTR)pData->BroadcastContext[i];
    }

    /* Set Blt flag for basic present */
    presentData.Flags.Blt = 1;

    status = d3dkmt_thunks.pfnPresent(&presentData);
    if (status != 0)
    {
        WARN("D3DKMTPresent failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT APIENTRY d3d9_wddm_escape_cb(
    HANDLE hDevice, const D3DDDICB_ESCAPE *pData)
{
    D3D9_WDDM_DEVICE *device = pData ?
        d3d9_wddm_validate_device_handle(pData->hDevice) : NULL;
    D3DKMT_ESCAPE escapeData;
    NTSTATUS status;

    TRACE("EscapeCb: adapter=%p device=%p\n", hDevice, pData ? pData->hDevice : NULL);

    if (!hDevice || !pData)
        return E_INVALIDARG;
    if (pData->hDevice && !device)
        return E_INVALIDARG;
    if (pData->PrivateDriverDataSize && !pData->pPrivateDriverData)
        return E_INVALIDARG;

    memset(&escapeData, 0, sizeof(escapeData));
    escapeData.hAdapter = (D3DKMT_HANDLE)(ULONG_PTR)hDevice;
    escapeData.hDevice = device ? device->hDevice : 0;
    escapeData.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escapeData.Flags = pData->Flags;
    escapeData.pPrivateDriverData = pData->pPrivateDriverData;
    escapeData.PrivateDriverDataSize = pData->PrivateDriverDataSize;
    escapeData.hContext = pData->hContext ?
        (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext :
        (device ? device->hContext : 0);

    status = d3dkmt_thunks.pfnEscape(&escapeData);
    if (status != 0)
    {
        WARN("D3DKMTEscape failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    return S_OK;
}

/* ========================================================================
 * Create Device
 *
 * Creates a WDDM device: kernel device + kernel context + UMD device.
 * ====================================================================== */

HRESULT d3d9_wddm_create_device(D3D9_WDDM_ADAPTER *adapter,
                                 D3D9_WDDM_DEVICE *device)
{
    D3DKMT_CREATEDEVICE createDevice;
    D3DKMT_CREATECONTEXT createContext;
    D3DDDIARG_CREATEDEVICE createUmdDevice;
    NTSTATUS status;
    HRESULT hr;

    if (!adapter || !device || !adapter->hAdapter || !adapter->hUmdAdapter)
        return D3DERR_INVALIDCALL;

    memset(device, 0, sizeof(*device));
    device->pAdapter = adapter;

    /* Step 1: D3DKMTCreateDevice */
    memset(&createDevice, 0, sizeof(createDevice));
    createDevice.hAdapter = adapter->hAdapter;
    /* LegacyMode = 0 for WDDM path */

    status = d3dkmt_thunks.pfnCreateDevice(&createDevice);
    if (status != 0)
    {
        WARN("D3DKMTCreateDevice failed: 0x%08lx\n", status);
        return D3DERR_NOTAVAILABLE;
    }

    device->hDevice = createDevice.hDevice;
    if (!device->hDevice)
    {
        WARN("D3DKMTCreateDevice returned a zero device handle\n");
        d3d9_wddm_destroy_device(device);
        return D3DERR_NOTAVAILABLE;
    }

    device->pCommandBuffer = createDevice.pCommandBuffer;
    device->CommandBufferSize = createDevice.CommandBufferSize;

    TRACE("Kernel device created: handle=0x%x\n", device->hDevice);

    /* Step 2: D3DKMTCreateContext */
    memset(&createContext, 0, sizeof(createContext));
    createContext.hDevice = device->hDevice;
    createContext.NodeOrdinal = 0;
    createContext.EngineAffinity = 1; /* Primary engine */
    createContext.ClientHint = D3DKMT_CLIENTHINT_DX9;

    status = d3dkmt_thunks.pfnCreateContext(&createContext);
    if (status != 0)
    {
        WARN("D3DKMTCreateContext failed: 0x%08lx\n", status);
        d3d9_wddm_destroy_device(device);
        return D3DERR_NOTAVAILABLE;
    }

    device->hContext = createContext.hContext;
    if (!device->hContext)
    {
        WARN("D3DKMTCreateContext returned a zero context handle\n");
        d3d9_wddm_destroy_device(device);
        return D3DERR_NOTAVAILABLE;
    }

    /* Update command buffer from context creation */
    if (createContext.pCommandBuffer)
        device->pCommandBuffer = createContext.pCommandBuffer;
    if (createContext.CommandBufferSize)
        device->CommandBufferSize = createContext.CommandBufferSize;
    device->CommandBuffer = createContext.CommandBuffer;
    device->pAllocationList = createContext.pAllocationList;
    device->AllocationListSize = createContext.AllocationListSize;
    device->pPatchLocationList = createContext.pPatchLocationList;
    device->PatchLocationListSize = createContext.PatchLocationListSize;

    TRACE("Kernel context created: handle=0x%x\n", device->hContext);

    /* Step 3: Set up runtime callback table */
    device->DeviceCallbacks.pfnAllocateCb = d3d9_wddm_allocate_cb;
    device->DeviceCallbacks.pfnDeallocateCb = d3d9_wddm_deallocate_cb;
    device->DeviceCallbacks.pfnPresentCb = d3d9_wddm_present_cb;
    device->DeviceCallbacks.pfnRenderCb = d3d9_wddm_render_cb;
    device->DeviceCallbacks.pfnLockCb = d3d9_wddm_lock_cb;
    device->DeviceCallbacks.pfnUnlockCb = d3d9_wddm_unlock_cb;
    device->DeviceCallbacks.pfnEscapeCb = d3d9_wddm_escape_cb;

    /* Step 4: Call UMD's CreateDevice */
    if (!adapter->AdapterFuncs.pfnCreateDevice)
    {
        WARN("UMD did not provide pfnCreateDevice\n");
        d3d9_wddm_destroy_device(device);
        return D3DERR_NOTAVAILABLE;
    }

    memset(&createUmdDevice, 0, sizeof(createUmdDevice));
    createUmdDevice.hDevice = (HANDLE)device;  /* RT device handle = our device struct */
    createUmdDevice.Interface = D3D_UMD_INTERFACE_VERSION_VISTA;
    createUmdDevice.Version = 0;
    createUmdDevice.pCallbacks = &device->DeviceCallbacks;
    createUmdDevice.pCommandBuffer = device->pCommandBuffer;
    createUmdDevice.CommandBufferSize = device->CommandBufferSize;
    createUmdDevice.pAllocationList = device->pAllocationList;
    createUmdDevice.AllocationListSize = device->AllocationListSize;
    createUmdDevice.pPatchLocationList = device->pPatchLocationList;
    createUmdDevice.PatchLocationListSize = device->PatchLocationListSize;

    hr = adapter->AdapterFuncs.pfnCreateDevice(adapter->hUmdAdapter,
                                                &createUmdDevice);
    if (FAILED(hr))
    {
        WARN("UMD CreateDevice failed: 0x%08lx\n", hr);
        d3d9_wddm_destroy_device(device);
        return hr;
    }

    if (!createUmdDevice.pDeviceFuncs)
    {
        WARN("UMD CreateDevice did not return a device function table\n");
        d3d9_wddm_destroy_device(device);
        return D3DERR_NOTAVAILABLE;
    }

    device->hUmdDevice = createUmdDevice.hDevice;
    if (!device->hUmdDevice)
    {
        WARN("UMD CreateDevice returned a zero device handle\n");
        d3d9_wddm_destroy_device(device);
        return D3DERR_NOTAVAILABLE;
    }

    device->DeviceFuncs = *createUmdDevice.pDeviceFuncs;
    if (createUmdDevice.pCommandBuffer)
        device->pCommandBuffer = createUmdDevice.pCommandBuffer;
    if (createUmdDevice.CommandBufferSize)
        device->CommandBufferSize = createUmdDevice.CommandBufferSize;
    device->pAllocationList = createUmdDevice.pAllocationList;
    device->AllocationListSize = createUmdDevice.AllocationListSize;
    device->pPatchLocationList = createUmdDevice.pPatchLocationList;
    device->PatchLocationListSize = createUmdDevice.PatchLocationListSize;

    TRACE("UMD device created successfully\n");
    return S_OK;
}

/* ========================================================================
 * Destroy Device
 * ====================================================================== */

void d3d9_wddm_destroy_device(D3D9_WDDM_DEVICE *device)
{
    if (!device)
        return;

    /* Destroy UMD device */
    if (device->hUmdDevice && device->DeviceFuncs.pfnDestroyDevice)
    {
        device->DeviceFuncs.pfnDestroyDevice(device->hUmdDevice);
        device->hUmdDevice = NULL;
    }

    /* Destroy kernel context */
    if (device->hContext)
    {
        D3DKMT_DESTROYCONTEXT destroyCtx;
        memset(&destroyCtx, 0, sizeof(destroyCtx));
        destroyCtx.hContext = device->hContext;
        d3dkmt_thunks.pfnDestroyContext(&destroyCtx);
        device->hContext = 0;
    }

    /* Destroy kernel device */
    if (device->hDevice)
    {
        D3DKMT_DESTROYDEVICE destroyDev;
        memset(&destroyDev, 0, sizeof(destroyDev));
        destroyDev.hDevice = device->hDevice;
        d3dkmt_thunks.pfnDestroyDevice(&destroyDev);
        device->hDevice = 0;
    }

    memset(device, 0, sizeof(*device));
}

/* ========================================================================
 * Present (public API)
 *
 * Direct D3DKMT present path for the runtime to call.
 * ====================================================================== */

HRESULT d3d9_wddm_present(D3D9_WDDM_DEVICE *device,
                           D3DKMT_HANDLE hSrcAlloc,
                           D3DKMT_HANDLE hDstAlloc)
{
    D3DKMT_PRESENT presentData;
    NTSTATUS status;

    if (!device || !device->hContext || !hSrcAlloc)
        return E_INVALIDARG;

    memset(&presentData, 0, sizeof(presentData));
    /* hDevice and hContext are a union; use hContext */
    presentData.hContext = device->hContext;
    presentData.hSource = hSrcAlloc;
    presentData.hDestination = hDstAlloc;
    presentData.Flags.Blt = 1;

    status = d3dkmt_thunks.pfnPresent(&presentData);
    if (status != 0)
    {
        WARN("D3DKMTPresent failed: 0x%08lx\n", status);
        return E_FAIL;
    }

    return S_OK;
}

/* ========================================================================
 * DWM Composition Support
 *
 * Resolve the 7 undocumented dwmapi ordinal imports used by d3d9.dll
 * for windowed present via DWM composition.
 * ====================================================================== */

BOOL d3d9_wddm_resolve_dwm(D3D9_DWM_FUNCS *dwm)
{
    if (!dwm)
        return FALSE;

    if (dwm->Resolved)
        return (dwm->hDwmapi != NULL);

    dwm->Resolved = TRUE;

    dwm->hDwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!dwm->hDwmapi)
    {
        WARN("Could not load dwmapi.dll, DWM composition unavailable\n");
        return FALSE;
    }

    /* Resolve by ordinal */
    dwm->pfnGetWindowSharedSurface =
        (PFN_DwmpDxGetWindowSharedSurface)
        GetProcAddress(dwm->hDwmapi, MAKEINTRESOURCEA(100));

    dwm->pfnUpdateWindowSharedSurface =
        (PFN_DwmpDxUpdateWindowSharedSurface)
        GetProcAddress(dwm->hDwmapi, MAKEINTRESOURCEA(101));

    dwm->pfnBindSwapChain =
        (PFN_DwmpDxBindSwapChain)
        GetProcAddress(dwm->hDwmapi, MAKEINTRESOURCEA(125));

    dwm->pfnUnbindSwapChain =
        (PFN_DwmpDxUnbindSwapChain)
        GetProcAddress(dwm->hDwmapi, MAKEINTRESOURCEA(126));

    dwm->pfnIsThreadDesktopComposited =
        (PFN_DwmpDxgiIsThreadDesktopComposited)
        GetProcAddress(dwm->hDwmapi, MAKEINTRESOURCEA(128));

    dwm->pfnDisableRedirection =
        (PFN_DwmpDxgiDisableRedirection)
        GetProcAddress(dwm->hDwmapi, MAKEINTRESOURCEA(129));

    dwm->pfnEnableRedirection =
        (PFN_DwmpDxgiEnableRedirection)
        GetProcAddress(dwm->hDwmapi, MAKEINTRESOURCEA(130));

    /* The critical ones for windowed present are 100 and 101 */
    if (!dwm->pfnGetWindowSharedSurface || !dwm->pfnUpdateWindowSharedSurface)
    {
        WARN("Missing critical dwmapi ordinals (100, 101)\n");
        FreeLibrary(dwm->hDwmapi);
        memset(dwm, 0, sizeof(*dwm));
        dwm->Resolved = TRUE;
        return FALSE;
    }

    TRACE("DWM ordinals resolved: 100=%p, 101=%p, 125=%p, 126=%p, "
          "128=%p, 129=%p, 130=%p\n",
          dwm->pfnGetWindowSharedSurface,
          dwm->pfnUpdateWindowSharedSurface,
          dwm->pfnBindSwapChain,
          dwm->pfnUnbindSwapChain,
          dwm->pfnIsThreadDesktopComposited,
          dwm->pfnDisableRedirection,
          dwm->pfnEnableRedirection);
    return TRUE;
}
