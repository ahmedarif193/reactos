/*
 * PROJECT:     ReactOS DXGI Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     IDXGIFactory / IDXGIFactory1 implementation
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "dxgi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

static BOOL Factory_LuidEqual(const LUID *a, const LUID *b)
{
    return a->LowPart == b->LowPart && a->HighPart == b->HighPart;
}

static BOOL Factory_DisplayHasModes(const WCHAR *deviceName)
{
    DEVMODEW dm;

    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    return EnumDisplaySettingsW(deviceName, 0, &dm);
}

static void Factory_AddOutput(DxgiFactory *factory, UINT adapterIdx,
                              const WCHAR *deviceName,
                              D3DDDI_VIDEO_PRESENT_SOURCE_ID sourceId)
{
    UINT outputIdx;

    if (adapterIdx >= DXGI_MAX_ADAPTERS)
        return;

    outputIdx = factory->AdapterCache[adapterIdx].OutputCount;
    if (outputIdx >= DXGI_MAX_OUTPUTS)
        return;

    lstrcpynW(factory->AdapterCache[adapterIdx].Outputs[outputIdx].DeviceName,
              deviceName,
              sizeof(factory->AdapterCache[adapterIdx].Outputs[outputIdx].DeviceName) / sizeof(WCHAR));
    factory->AdapterCache[adapterIdx].Outputs[outputIdx].VidPnSourceId = sourceId;
    factory->AdapterCache[adapterIdx].OutputCount++;
}

/*
 * ========================================================================
 *  Adapter enumeration via D3DKMT
 * ========================================================================
 *
 * On Windows, DXGI discovers adapters through D3DKMTEnumAdapters or by
 * iterating GDI display names (\\.\DISPLAY1, \\.\DISPLAY2, ...) and
 * calling D3DKMTOpenAdapterFromGdiDisplayName for each.
 *
 * We use the GDI display name approach because it works on the existing
 * ReactOS D3DKMT thunk layer without requiring D3DKMTEnumAdapters.
 */
static void Factory_EnumerateAdapters(DxgiFactory *factory)
{
    DISPLAY_DEVICEW dd;
    UINT idx = 0;
    UINT adapterIdx = 0;

    if (factory->Enumerated)
        return;

    factory->Enumerated = TRUE;
    factory->AdapterCount = 0;
    memset(factory->AdapterCache, 0, sizeof(factory->AdapterCache));

    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);

    /*
     * Walk all display devices.  Each unique adapter LUID maps to one
     * DXGI adapter.  In simple configurations there is one adapter with
     * one or more outputs.
     */
    while (EnumDisplayDevicesW(NULL, idx, &dd, 0))
    {
        D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openAdapter;
        NTSTATUS status;
        BOOL found = FALSE;
        UINT i;

        idx++;

        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;

        if (!Factory_DisplayHasModes(dd.DeviceName))
            continue;

        memset(&openAdapter, 0, sizeof(openAdapter));
        lstrcpynW(openAdapter.DeviceName, dd.DeviceName,
                   sizeof(openAdapter.DeviceName) / sizeof(WCHAR));

        status = D3DKMTOpenAdapterFromGdiDisplayName(&openAdapter);
        if (status != 0) /* STATUS_SUCCESS */
        {
            WINE_TRACE("D3DKMTOpenAdapterFromGdiDisplayName(%S) failed: 0x%08lx\n",
                       dd.DeviceName, status);
            continue;
        }

        /* Check if we already have an entry for this adapter LUID. */
        for (i = 0; i < adapterIdx; i++)
        {
            if (Factory_LuidEqual(&factory->AdapterCache[i].AdapterLuid,
                                  &openAdapter.AdapterLuid))
            {
                Factory_AddOutput(factory, i, dd.DeviceName, openAdapter.VidPnSourceId);
                found = TRUE;
                break;
            }
        }

        if (found)
        {
            /* Close the duplicate handle */
            D3DKMT_CLOSEADAPTER closeAdapter;
            closeAdapter.hAdapter = openAdapter.hAdapter;
            D3DKMTCloseAdapter(&closeAdapter);
            continue;
        }

        if (adapterIdx >= DXGI_MAX_ADAPTERS)
        {
            D3DKMT_CLOSEADAPTER closeAdapter;
            closeAdapter.hAdapter = openAdapter.hAdapter;
            D3DKMTCloseAdapter(&closeAdapter);
            break;
        }

        factory->AdapterCache[adapterIdx].hAdapter = openAdapter.hAdapter;
        lstrcpynW(factory->AdapterCache[adapterIdx].DeviceName,
                   dd.DeviceName,
                   sizeof(factory->AdapterCache[adapterIdx].DeviceName) / sizeof(WCHAR));
        factory->AdapterCache[adapterIdx].AdapterLuid = openAdapter.AdapterLuid;
        Factory_AddOutput(factory, adapterIdx, dd.DeviceName, openAdapter.VidPnSourceId);
        factory->AdapterCache[adapterIdx].Valid = TRUE;
        adapterIdx++;
    }

    factory->AdapterCount = adapterIdx;
}

static void Factory_CloseAdapters(DxgiFactory *factory)
{
    UINT i;
    for (i = 0; i < factory->AdapterCount; i++)
    {
        if (factory->AdapterCache[i].Valid && factory->AdapterCache[i].hAdapter)
        {
            D3DKMT_CLOSEADAPTER closeAdapter;
            closeAdapter.hAdapter = factory->AdapterCache[i].hAdapter;
            D3DKMTCloseAdapter(&closeAdapter);
            factory->AdapterCache[i].Valid = FALSE;
        }
    }
    factory->AdapterCount = 0;
    factory->Enumerated = FALSE;
}

/*
 * ========================================================================
 *  IDXGIFactory1 / IDXGIFactory / IDXGIObject / IUnknown vtable
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiFactory_QueryInterface(
    IDXGIFactory1 *iface, REFIID riid, void **ppvObject)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);

    WINE_TRACE("(%p)->(%s, %p)\n", This, wine_dbgstr_guid(riid), ppvObject);

    if (!ppvObject)
        return E_POINTER;

    *ppvObject = NULL;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IDXGIObject) ||
        IsEqualGUID(riid, &IID_IDXGIFactory) ||
        IsEqualGUID(riid, &IID_IDXGIFactory1))
    {
        *ppvObject = &This->IDXGIFactory1_iface;
        IDXGIFactory1_AddRef(&This->IDXGIFactory1_iface);
        return S_OK;
    }

    WINE_WARN("(%p) unknown interface %s\n", This, wine_dbgstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DxgiFactory_AddRef(IDXGIFactory1 *iface)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);
    ULONG ref = InterlockedIncrement(&This->RefCount);
    WINE_TRACE("(%p) ref=%lu\n", This, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE DxgiFactory_Release(IDXGIFactory1 *iface)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);
    ULONG ref = InterlockedDecrement(&This->RefCount);

    WINE_TRACE("(%p) ref=%lu\n", This, ref);

    if (ref == 0)
    {
        Factory_CloseAdapters(This);
        DxgiObject_Destroy(&This->Base);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

/* IDXGIObject methods */

static HRESULT STDMETHODCALLTYPE DxgiFactory_SetPrivateData(
    IDXGIFactory1 *iface, REFGUID Name, UINT DataSize, const void *pData)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);
    return DxgiObject_SetPrivateData(&This->Base, Name, DataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiFactory_SetPrivateDataInterface(
    IDXGIFactory1 *iface, REFGUID Name, const IUnknown *pUnknown)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);
    return DxgiObject_SetPrivateDataInterface(&This->Base, Name, pUnknown);
}

static HRESULT STDMETHODCALLTYPE DxgiFactory_GetPrivateData(
    IDXGIFactory1 *iface, REFGUID Name, UINT *pDataSize, void *pData)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);
    return DxgiObject_GetPrivateData(&This->Base, Name, pDataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiFactory_GetParent(
    IDXGIFactory1 *iface, REFIID riid, void **ppParent)
{
    WINE_TRACE("(%p)->(%s, %p)\n", iface, wine_dbgstr_guid(riid), ppParent);

    /* The factory has no parent -- it is the root of the DXGI object hierarchy */
    if (ppParent)
        *ppParent = NULL;
    return E_NOINTERFACE;
}

/* IDXGIFactory methods */

static HRESULT STDMETHODCALLTYPE DxgiFactory_EnumAdapters(
    IDXGIFactory1 *iface, UINT Adapter, IDXGIAdapter **ppAdapter)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);
    IDXGIAdapter1 *adapter1 = NULL;
    HRESULT hr;

    WINE_TRACE("(%p)->(%u, %p)\n", This, Adapter, ppAdapter);

    if (!ppAdapter)
        return DXGI_ERROR_INVALID_CALL;

    *ppAdapter = NULL;

    Factory_EnumerateAdapters(This);

    if (Adapter >= This->AdapterCount)
        return DXGI_ERROR_NOT_FOUND;

    hr = DxgiAdapter_Create(This, Adapter,
                             This->AdapterCache[Adapter].hAdapter,
                             This->AdapterCache[Adapter].DeviceName,
                             This->AdapterCache[Adapter].AdapterLuid,
                             &adapter1);
    if (SUCCEEDED(hr))
    {
        *ppAdapter = (IDXGIAdapter *)adapter1;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE DxgiFactory_MakeWindowAssociation(
    IDXGIFactory1 *iface, HWND WindowHandle, UINT Flags)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);

    WINE_TRACE("(%p)->(%p, 0x%x)\n", This, WindowHandle, Flags);

    This->AssociatedWindow = WindowHandle;
    This->AssociationFlags = Flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiFactory_GetWindowAssociation(
    IDXGIFactory1 *iface, HWND *pWindowHandle)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pWindowHandle);

    if (!pWindowHandle)
        return DXGI_ERROR_INVALID_CALL;

    *pWindowHandle = This->AssociatedWindow;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiFactory_CreateSwapChain(
    IDXGIFactory1 *iface, IUnknown *pDevice,
    DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);

    WINE_TRACE("(%p)->(%p, %p, %p)\n", This, pDevice, pDesc, ppSwapChain);

    if (!pDevice || !pDesc || !ppSwapChain)
        return DXGI_ERROR_INVALID_CALL;

    *ppSwapChain = NULL;

    return DxgiSwapChain_Create(This, pDevice, pDesc, ppSwapChain);
}

static HRESULT STDMETHODCALLTYPE DxgiFactory_CreateSoftwareAdapter(
    IDXGIFactory1 *iface, HMODULE Module, IDXGIAdapter **ppAdapter)
{
    WINE_FIXME("(%p)->(%p, %p) stub!\n", iface, Module, ppAdapter);

    if (ppAdapter)
        *ppAdapter = NULL;

    return DXGI_ERROR_UNSUPPORTED;
}

/* IDXGIFactory1 methods */

static HRESULT STDMETHODCALLTYPE DxgiFactory_EnumAdapters1(
    IDXGIFactory1 *iface, UINT Adapter, IDXGIAdapter1 **ppAdapter)
{
    DxgiFactory *This = impl_from_IDXGIFactory1(iface);
    HRESULT hr;

    WINE_TRACE("(%p)->(%u, %p)\n", This, Adapter, ppAdapter);

    if (!ppAdapter)
        return DXGI_ERROR_INVALID_CALL;

    *ppAdapter = NULL;

    Factory_EnumerateAdapters(This);

    if (Adapter >= This->AdapterCount)
        return DXGI_ERROR_NOT_FOUND;

    hr = DxgiAdapter_Create(This, Adapter,
                             This->AdapterCache[Adapter].hAdapter,
                             This->AdapterCache[Adapter].DeviceName,
                             This->AdapterCache[Adapter].AdapterLuid,
                             ppAdapter);
    return hr;
}

static BOOL STDMETHODCALLTYPE DxgiFactory_IsCurrent(IDXGIFactory1 *iface)
{
    WINE_TRACE("(%p)\n", iface);
    /*
     * We do not track hardware changes at runtime, so always report
     * that the factory is current.
     */
    return TRUE;
}

/*
 * ========================================================================
 *  VTable
 * ========================================================================
 */

static const IDXGIFactory1Vtbl DxgiFactory_Vtbl =
{
    /* IUnknown */
    DxgiFactory_QueryInterface,
    DxgiFactory_AddRef,
    DxgiFactory_Release,
    /* IDXGIObject */
    DxgiFactory_SetPrivateData,
    DxgiFactory_SetPrivateDataInterface,
    DxgiFactory_GetPrivateData,
    DxgiFactory_GetParent,
    /* IDXGIFactory */
    DxgiFactory_EnumAdapters,
    DxgiFactory_MakeWindowAssociation,
    DxgiFactory_GetWindowAssociation,
    DxgiFactory_CreateSwapChain,
    DxgiFactory_CreateSoftwareAdapter,
    /* IDXGIFactory1 */
    DxgiFactory_EnumAdapters1,
    DxgiFactory_IsCurrent,
};

/*
 * ========================================================================
 *  Factory creation
 * ========================================================================
 */

HRESULT DxgiFactory_Create(REFIID riid, void **ppFactory)
{
    DxgiFactory *factory;
    HRESULT hr;

    if (!ppFactory)
        return DXGI_ERROR_INVALID_CALL;

    *ppFactory = NULL;

    factory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;

    factory->IDXGIFactory1_iface.lpVtbl = &DxgiFactory_Vtbl;
    factory->RefCount = 1;
    DxgiObject_Init(&factory->Base);

    hr = IDXGIFactory1_QueryInterface(&factory->IDXGIFactory1_iface, riid, ppFactory);
    IDXGIFactory1_Release(&factory->IDXGIFactory1_iface);

    return hr;
}
