/*
 * PROJECT:     ReactOS DXGI Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     IDXGIAdapter / IDXGIAdapter1 implementation
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "dxgi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

/*
 * ========================================================================
 *  Adapter description query
 * ========================================================================
 *
 * We fill DXGI_ADAPTER_DESC1 from two sources:
 * 1. D3DKMTQueryAdapterInfo (KMTQAITYPE_UMDRIVERPRIVATE or DRIVERNAME)
 *    -- provides vendor/device IDs and memory sizes when WDDM is running.
 * 2. Fallback: use EnumDisplayDevicesW to get the device description string,
 *    and fill in safe defaults for IDs and memory.
 */
static void Adapter_FillDesc(DxgiAdapter *adapter)
{
    DISPLAY_DEVICEW dd;

    if (adapter->DescValid)
        return;

    memset(&adapter->Desc, 0, sizeof(adapter->Desc));

    /* Try to get a description string from GDI */
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(adapter->DeviceName, 0, &dd, 0))
    {
        lstrcpynW(adapter->Desc.Description, dd.DeviceString,
                   sizeof(adapter->Desc.Description) / sizeof(WCHAR));
    }
    else
    {
        lstrcpyW(adapter->Desc.Description, L"ReactOS Display Adapter");
    }

    /*
     * If we have a valid D3DKMT adapter handle, try to query more
     * detailed information.
     */
    if (adapter->hAdapter)
    {
        D3DKMT_QUERYADAPTERINFO queryInfo;
        D3DKMT_SEGMENTSIZEINFO segInfo;
        NTSTATUS status;

        memset(&queryInfo, 0, sizeof(queryInfo));
        queryInfo.hAdapter = adapter->hAdapter;
        queryInfo.Type = KMTQAITYPE_GETSEGMENTSIZE;
        queryInfo.pPrivateDriverData = &segInfo;
        queryInfo.PrivateDriverDataSize = sizeof(segInfo);
        memset(&segInfo, 0, sizeof(segInfo));

        status = D3DKMTQueryAdapterInfo(&queryInfo);
        if (status == 0) /* STATUS_SUCCESS */
        {
            adapter->Desc.DedicatedVideoMemory = (SIZE_T)segInfo.DedicatedVideoMemorySize;
            adapter->Desc.DedicatedSystemMemory = (SIZE_T)segInfo.DedicatedSystemMemorySize;
            adapter->Desc.SharedSystemMemory = (SIZE_T)segInfo.SharedSystemMemorySize;
        }
        else
        {
            adapter->Desc.DedicatedVideoMemory = 0;
            adapter->Desc.DedicatedSystemMemory = 0;
            adapter->Desc.SharedSystemMemory = 0;
        }
    }
    else
    {
        /* Software adapter fallback */
        adapter->Desc.DedicatedVideoMemory = 0;
        adapter->Desc.DedicatedSystemMemory = 0;
        adapter->Desc.SharedSystemMemory = 256 * 1024 * 1024;
        adapter->Desc.Flags = DXGI_ADAPTER_FLAG_SOFTWARE;
    }

    adapter->Desc.AdapterLuid = adapter->AdapterLuid;
    adapter->Desc.VendorId = 0;
    adapter->Desc.DeviceId = 0;
    adapter->Desc.SubSysId = 0;
    adapter->Desc.Revision = 0;

    adapter->DescValid = TRUE;
}

/*
 * ========================================================================
 *  IUnknown
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiAdapter_QueryInterface(
    IDXGIAdapter1 *iface, REFIID riid, void **ppvObject)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);

    WINE_TRACE("(%p)->(%s, %p)\n", This, wine_dbgstr_guid(riid), ppvObject);

    if (!ppvObject)
        return E_POINTER;

    *ppvObject = NULL;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IDXGIObject) ||
        IsEqualGUID(riid, &IID_IDXGIAdapter) ||
        IsEqualGUID(riid, &IID_IDXGIAdapter1))
    {
        *ppvObject = &This->IDXGIAdapter1_iface;
        IDXGIAdapter1_AddRef(&This->IDXGIAdapter1_iface);
        return S_OK;
    }

    WINE_WARN("(%p) unknown interface %s\n", This, wine_dbgstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DxgiAdapter_AddRef(IDXGIAdapter1 *iface)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);
    ULONG ref = InterlockedIncrement(&This->RefCount);
    WINE_TRACE("(%p) ref=%lu\n", This, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE DxgiAdapter_Release(IDXGIAdapter1 *iface)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);
    ULONG ref = InterlockedDecrement(&This->RefCount);

    WINE_TRACE("(%p) ref=%lu\n", This, ref);

    if (ref == 0)
    {
        if (This->pFactory)
            IDXGIFactory1_Release(&This->pFactory->IDXGIFactory1_iface);
        DxgiObject_Destroy(&This->Base);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

/*
 * ========================================================================
 *  IDXGIObject
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiAdapter_SetPrivateData(
    IDXGIAdapter1 *iface, REFGUID Name, UINT DataSize, const void *pData)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);
    return DxgiObject_SetPrivateData(&This->Base, Name, DataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiAdapter_SetPrivateDataInterface(
    IDXGIAdapter1 *iface, REFGUID Name, const IUnknown *pUnknown)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);
    return DxgiObject_SetPrivateDataInterface(&This->Base, Name, pUnknown);
}

static HRESULT STDMETHODCALLTYPE DxgiAdapter_GetPrivateData(
    IDXGIAdapter1 *iface, REFGUID Name, UINT *pDataSize, void *pData)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);
    return DxgiObject_GetPrivateData(&This->Base, Name, pDataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiAdapter_GetParent(
    IDXGIAdapter1 *iface, REFIID riid, void **ppParent)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);

    WINE_TRACE("(%p)->(%s, %p)\n", This, wine_dbgstr_guid(riid), ppParent);

    if (!ppParent)
        return E_POINTER;

    *ppParent = NULL;

    if (!This->pFactory)
        return E_NOINTERFACE;

    return IDXGIFactory1_QueryInterface(&This->pFactory->IDXGIFactory1_iface,
                                         riid, ppParent);
}

/*
 * ========================================================================
 *  IDXGIAdapter
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiAdapter_EnumOutputs(
    IDXGIAdapter1 *iface, UINT Output, IDXGIOutput **ppOutput)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);
    const WCHAR *deviceName;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID sourceId;

    WINE_TRACE("(%p)->(%u, %p)\n", This, Output, ppOutput);

    if (!ppOutput)
        return DXGI_ERROR_INVALID_CALL;

    *ppOutput = NULL;

    if (!This->pFactory ||
        This->AdapterOrdinal >= This->pFactory->AdapterCount ||
        !This->pFactory->AdapterCache[This->AdapterOrdinal].Valid ||
        Output >= This->pFactory->AdapterCache[This->AdapterOrdinal].OutputCount)
    {
        return DXGI_ERROR_NOT_FOUND;
    }

    deviceName = This->pFactory->AdapterCache[This->AdapterOrdinal]
                    .Outputs[Output].DeviceName;
    sourceId = This->pFactory->AdapterCache[This->AdapterOrdinal]
                  .Outputs[Output].VidPnSourceId;

    if (!deviceName[0])
        return DXGI_ERROR_NOT_FOUND;

    return DxgiOutput_Create(This, Output, This->hAdapter, sourceId,
                             deviceName, ppOutput);
}

static HRESULT STDMETHODCALLTYPE DxgiAdapter_GetDesc(
    IDXGIAdapter1 *iface, DXGI_ADAPTER_DESC *pDesc)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pDesc);

    if (!pDesc)
        return DXGI_ERROR_INVALID_CALL;

    Adapter_FillDesc(This);

    /* DXGI_ADAPTER_DESC is the first portion of DXGI_ADAPTER_DESC1 */
    memcpy(pDesc->Description, This->Desc.Description, sizeof(pDesc->Description));
    pDesc->VendorId = This->Desc.VendorId;
    pDesc->DeviceId = This->Desc.DeviceId;
    pDesc->SubSysId = This->Desc.SubSysId;
    pDesc->Revision = This->Desc.Revision;
    pDesc->DedicatedVideoMemory = This->Desc.DedicatedVideoMemory;
    pDesc->DedicatedSystemMemory = This->Desc.DedicatedSystemMemory;
    pDesc->SharedSystemMemory = This->Desc.SharedSystemMemory;
    pDesc->AdapterLuid = This->Desc.AdapterLuid;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiAdapter_CheckInterfaceSupport(
    IDXGIAdapter1 *iface, REFGUID InterfaceName, LARGE_INTEGER *pUMDVersion)
{
    WINE_TRACE("(%p)->(%s, %p)\n", iface, wine_dbgstr_guid(InterfaceName), pUMDVersion);

    if (!InterfaceName)
        return DXGI_ERROR_INVALID_CALL;

    /*
     * Windows only supports checking for ID3D10Device and ID3D11Device
     * through this method.  We report no support, as we have no
     * D3D10/D3D11 device implementation yet.
     */
    if (pUMDVersion)
    {
        pUMDVersion->QuadPart = 0;
    }

    return DXGI_ERROR_UNSUPPORTED;
}

/*
 * ========================================================================
 *  IDXGIAdapter1
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiAdapter_GetDesc1(
    IDXGIAdapter1 *iface, DXGI_ADAPTER_DESC1 *pDesc)
{
    DxgiAdapter *This = impl_from_IDXGIAdapter1(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pDesc);

    if (!pDesc)
        return DXGI_ERROR_INVALID_CALL;

    Adapter_FillDesc(This);
    *pDesc = This->Desc;

    return S_OK;
}

/*
 * ========================================================================
 *  VTable
 * ========================================================================
 */

static const IDXGIAdapter1Vtbl DxgiAdapter_Vtbl =
{
    /* IUnknown */
    DxgiAdapter_QueryInterface,
    DxgiAdapter_AddRef,
    DxgiAdapter_Release,
    /* IDXGIObject */
    DxgiAdapter_SetPrivateData,
    DxgiAdapter_SetPrivateDataInterface,
    DxgiAdapter_GetPrivateData,
    DxgiAdapter_GetParent,
    /* IDXGIAdapter */
    DxgiAdapter_EnumOutputs,
    DxgiAdapter_GetDesc,
    DxgiAdapter_CheckInterfaceSupport,
    /* IDXGIAdapter1 */
    DxgiAdapter_GetDesc1,
};

/*
 * ========================================================================
 *  Adapter creation
 * ========================================================================
 */

HRESULT DxgiAdapter_Create(DxgiFactory *pFactory, UINT ordinal,
                            D3DKMT_HANDLE hAdapter, const WCHAR *DeviceName,
                            LUID AdapterLuid, IDXGIAdapter1 **ppAdapter)
{
    DxgiAdapter *adapter;

    if (!ppAdapter)
        return E_POINTER;

    *ppAdapter = NULL;

    adapter = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
    if (!adapter)
        return E_OUTOFMEMORY;

    adapter->IDXGIAdapter1_iface.lpVtbl = &DxgiAdapter_Vtbl;
    adapter->RefCount = 1;
    DxgiObject_Init(&adapter->Base);
    adapter->pFactory = pFactory;
    adapter->AdapterOrdinal = ordinal;
    adapter->hAdapter = hAdapter;
    adapter->AdapterLuid = AdapterLuid;
    if (DeviceName)
        lstrcpynW(adapter->DeviceName, DeviceName,
                   sizeof(adapter->DeviceName) / sizeof(WCHAR));

    /* Take a reference on the parent factory */
    if (pFactory)
        IDXGIFactory1_AddRef(&pFactory->IDXGIFactory1_iface);

    *ppAdapter = &adapter->IDXGIAdapter1_iface;
    return S_OK;
}
