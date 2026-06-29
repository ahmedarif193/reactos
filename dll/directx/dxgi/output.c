/*
 * PROJECT:     ReactOS DXGI Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     IDXGIOutput implementation
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "dxgi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

/*
 * ========================================================================
 *  Output description query
 * ========================================================================
 */
static void Output_FillDesc(DxgiOutput *output)
{
    MONITORINFOEXW monInfo;
    HMONITOR hMon;
    POINT pt;

    if (output->DescValid)
        return;

    memset(&output->Desc, 0, sizeof(output->Desc));

    lstrcpynW(output->Desc.DeviceName,
              output->DeviceName[0] ? output->DeviceName : output->pAdapter->DeviceName,
              sizeof(output->Desc.DeviceName) / sizeof(WCHAR));

    /*
     * Find the monitor associated with this output.  We use the display
     * name's primary point to locate the monitor handle.
     */
    pt.x = 0;
    pt.y = 0;
    hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    if (hMon)
    {
        output->Desc.Monitor = hMon;
        memset(&monInfo, 0, sizeof(monInfo));
        monInfo.cbSize = sizeof(monInfo);
        if (GetMonitorInfoW(hMon, (MONITORINFO *)&monInfo))
        {
            output->Desc.DesktopCoordinates = monInfo.rcMonitor;
            output->Desc.AttachedToDesktop = TRUE;
        }
    }

    output->Desc.Rotation = DXGI_MODE_ROTATION_IDENTITY;
    output->DescValid = TRUE;
}

/*
 * ========================================================================
 *  IUnknown
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiOutput_QueryInterface(
    IDXGIOutput *iface, REFIID riid, void **ppvObject)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);

    WINE_TRACE("(%p)->(%s, %p)\n", This, wine_dbgstr_guid(riid), ppvObject);

    if (!ppvObject)
        return E_POINTER;

    *ppvObject = NULL;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IDXGIObject) ||
        IsEqualGUID(riid, &IID_IDXGIOutput))
    {
        *ppvObject = &This->IDXGIOutput_iface;
        IDXGIOutput_AddRef(&This->IDXGIOutput_iface);
        return S_OK;
    }

    WINE_WARN("(%p) unknown interface %s\n", This, wine_dbgstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DxgiOutput_AddRef(IDXGIOutput *iface)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);
    ULONG ref = InterlockedIncrement(&This->RefCount);
    WINE_TRACE("(%p) ref=%lu\n", This, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE DxgiOutput_Release(IDXGIOutput *iface)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);
    ULONG ref = InterlockedDecrement(&This->RefCount);

    WINE_TRACE("(%p) ref=%lu\n", This, ref);

    if (ref == 0)
    {
        if (This->pAdapter)
            IDXGIAdapter1_Release(&This->pAdapter->IDXGIAdapter1_iface);
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

static HRESULT STDMETHODCALLTYPE DxgiOutput_SetPrivateData(
    IDXGIOutput *iface, REFGUID Name, UINT DataSize, const void *pData)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);
    return DxgiObject_SetPrivateData(&This->Base, Name, DataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_SetPrivateDataInterface(
    IDXGIOutput *iface, REFGUID Name, const IUnknown *pUnknown)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);
    return DxgiObject_SetPrivateDataInterface(&This->Base, Name, pUnknown);
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetPrivateData(
    IDXGIOutput *iface, REFGUID Name, UINT *pDataSize, void *pData)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);
    return DxgiObject_GetPrivateData(&This->Base, Name, pDataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetParent(
    IDXGIOutput *iface, REFIID riid, void **ppParent)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);

    WINE_TRACE("(%p)->(%s, %p)\n", This, wine_dbgstr_guid(riid), ppParent);

    if (!ppParent)
        return E_POINTER;

    *ppParent = NULL;

    if (!This->pAdapter)
        return E_NOINTERFACE;

    return IDXGIAdapter1_QueryInterface(&This->pAdapter->IDXGIAdapter1_iface,
                                         riid, ppParent);
}

/*
 * ========================================================================
 *  IDXGIOutput
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetDesc(
    IDXGIOutput *iface, DXGI_OUTPUT_DESC *pDesc)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pDesc);

    if (!pDesc)
        return DXGI_ERROR_INVALID_CALL;

    Output_FillDesc(This);
    *pDesc = This->Desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetDisplayModeList(
    IDXGIOutput *iface, DXGI_FORMAT EnumFormat, UINT Flags,
    UINT *pNumModes, DXGI_MODE_DESC *pDesc)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);
    DEVMODEW dm;
    UINT count = 0;
    UINT idx = 0;

    WINE_TRACE("(%p)->(fmt=%u, flags=0x%x, %p, %p)\n",
               This, EnumFormat, Flags, pNumModes, pDesc);

    if (!pNumModes)
        return DXGI_ERROR_INVALID_CALL;

    /*
     * Enumerate display modes using EnumDisplaySettingsW.
     * This is the GDI fallback path.  When WDDM is fully implemented,
     * this should use D3DKMTGetDisplayModeList instead.
     */
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);

    /* First pass: count modes */
    while (EnumDisplaySettingsW(This->DeviceName, idx, &dm))
    {
        idx++;

        /* Skip interlaced modes unless requested */
        if (!(Flags & DXGI_ENUM_MODES_INTERLACED) &&
            (dm.dmDisplayFlags & DM_INTERLACED))
            continue;

        count++;
    }

    if (count == 0)
    {
        *pNumModes = 0;
        return DXGI_ERROR_NOT_FOUND;
    }

    if (!pDesc)
    {
        *pNumModes = count;
        return S_OK;
    }

    if (*pNumModes < count)
    {
        *pNumModes = count;
        return DXGI_ERROR_MORE_DATA;
    }

    /* Second pass: fill mode descriptors */
    idx = 0;
    count = 0;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);

    while (EnumDisplaySettingsW(This->DeviceName, idx, &dm))
    {
        idx++;

        if (!(Flags & DXGI_ENUM_MODES_INTERLACED) &&
            (dm.dmDisplayFlags & DM_INTERLACED))
            continue;

        if (count < *pNumModes)
        {
            pDesc[count].Width = dm.dmPelsWidth;
            pDesc[count].Height = dm.dmPelsHeight;
            pDesc[count].RefreshRate.Numerator = dm.dmDisplayFrequency;
            pDesc[count].RefreshRate.Denominator = 1;
            pDesc[count].Format = EnumFormat;
            pDesc[count].ScanlineOrdering =
                (dm.dmDisplayFlags & DM_INTERLACED)
                    ? DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST
                    : DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
            pDesc[count].Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        }
        count++;
    }

    *pNumModes = count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_FindClosestMatchingMode(
    IDXGIOutput *iface, const DXGI_MODE_DESC *pModeToMatch,
    DXGI_MODE_DESC *pClosestMatch, IUnknown *pConcernedDevice)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);
    DXGI_MODE_DESC *modes = NULL;
    UINT numModes = 0;
    UINT i;
    UINT bestIdx = 0;
    UINT bestDiff = UINT_MAX;
    HRESULT hr;

    WINE_TRACE("(%p)->(%p, %p, %p)\n", This, pModeToMatch, pClosestMatch, pConcernedDevice);

    if (!pModeToMatch || !pClosestMatch)
        return DXGI_ERROR_INVALID_CALL;

    memset(pClosestMatch, 0, sizeof(*pClosestMatch));

    /* Get the number of modes */
    hr = DxgiOutput_GetDisplayModeList(iface,
        pModeToMatch->Format ? pModeToMatch->Format : DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_ENUM_MODES_INTERLACED, &numModes, NULL);
    if (FAILED(hr) || numModes == 0)
    {
        return hr;
    }

    modes = HeapAlloc(GetProcessHeap(), 0, numModes * sizeof(DXGI_MODE_DESC));
    if (!modes)
        return E_OUTOFMEMORY;

    hr = DxgiOutput_GetDisplayModeList(iface,
        pModeToMatch->Format ? pModeToMatch->Format : DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_ENUM_MODES_INTERLACED, &numModes, modes);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, modes);
        return hr;
    }

    /*
     * Simple closest-match: minimize the absolute difference in resolution
     * and refresh rate.
     */
    for (i = 0; i < numModes; i++)
    {
        UINT wDiff = 0, hDiff = 0, rDiff = 0, totalDiff;

        if (pModeToMatch->Width)
            wDiff = (modes[i].Width > pModeToMatch->Width)
                ? (modes[i].Width - pModeToMatch->Width)
                : (pModeToMatch->Width - modes[i].Width);

        if (pModeToMatch->Height)
            hDiff = (modes[i].Height > pModeToMatch->Height)
                ? (modes[i].Height - pModeToMatch->Height)
                : (pModeToMatch->Height - modes[i].Height);

        if (pModeToMatch->RefreshRate.Numerator)
        {
            UINT reqHz = pModeToMatch->RefreshRate.Numerator;
            UINT modHz = modes[i].RefreshRate.Numerator;
            if (pModeToMatch->RefreshRate.Denominator)
                reqHz /= pModeToMatch->RefreshRate.Denominator;
            if (modes[i].RefreshRate.Denominator)
                modHz /= modes[i].RefreshRate.Denominator;
            rDiff = (modHz > reqHz) ? (modHz - reqHz) : (reqHz - modHz);
        }

        totalDiff = wDiff + hDiff + rDiff;
        if (totalDiff < bestDiff)
        {
            bestDiff = totalDiff;
            bestIdx = i;
        }
    }

    *pClosestMatch = modes[bestIdx];
    HeapFree(GetProcessHeap(), 0, modes);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_WaitForVBlank(IDXGIOutput *iface)
{
    DxgiOutput *This = impl_from_IDXGIOutput(iface);

    WINE_TRACE("(%p)\n", This);

    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_TakeOwnership(
    IDXGIOutput *iface, IUnknown *pDevice, BOOL Exclusive)
{
    WINE_TRACE("(%p)->(%p, %d)\n", iface, pDevice, Exclusive);
    return DXGI_ERROR_UNSUPPORTED;
}

static void STDMETHODCALLTYPE DxgiOutput_ReleaseOwnership(IDXGIOutput *iface)
{
    WINE_TRACE("(%p)\n", iface);
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetGammaControlCapabilities(
    IDXGIOutput *iface, DXGI_GAMMA_CONTROL_CAPABILITIES *pGammaCaps)
{
    WINE_TRACE("(%p)->(%p)\n", iface, pGammaCaps);

    if (!pGammaCaps)
        return DXGI_ERROR_INVALID_CALL;

    memset(pGammaCaps, 0, sizeof(*pGammaCaps));
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_SetGammaControl(
    IDXGIOutput *iface, const DXGI_GAMMA_CONTROL *pArray)
{
    WINE_FIXME("(%p)->(%p) stub!\n", iface, pArray);
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetGammaControl(
    IDXGIOutput *iface, DXGI_GAMMA_CONTROL *pArray)
{
    WINE_FIXME("(%p)->(%p) stub!\n", iface, pArray);

    if (!pArray)
        return DXGI_ERROR_INVALID_CALL;

    memset(pArray, 0, sizeof(*pArray));
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_SetDisplaySurface(
    IDXGIOutput *iface, IDXGISurface *pScanoutSurface)
{
    WINE_FIXME("(%p)->(%p) stub!\n", iface, pScanoutSurface);
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetDisplaySurfaceData(
    IDXGIOutput *iface, IDXGISurface *pDestination)
{
    WINE_FIXME("(%p)->(%p) stub!\n", iface, pDestination);
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiOutput_GetFrameStatistics(
    IDXGIOutput *iface, DXGI_FRAME_STATISTICS *pStats)
{
    WINE_TRACE("(%p)->(%p)\n", iface, pStats);

    if (!pStats)
        return DXGI_ERROR_INVALID_CALL;

    memset(pStats, 0, sizeof(*pStats));
    return DXGI_ERROR_UNSUPPORTED;
}

/*
 * ========================================================================
 *  VTable
 * ========================================================================
 */

static const IDXGIOutputVtbl DxgiOutput_Vtbl =
{
    /* IUnknown */
    DxgiOutput_QueryInterface,
    DxgiOutput_AddRef,
    DxgiOutput_Release,
    /* IDXGIObject */
    DxgiOutput_SetPrivateData,
    DxgiOutput_SetPrivateDataInterface,
    DxgiOutput_GetPrivateData,
    DxgiOutput_GetParent,
    /* IDXGIOutput */
    DxgiOutput_GetDesc,
    DxgiOutput_GetDisplayModeList,
    DxgiOutput_FindClosestMatchingMode,
    DxgiOutput_WaitForVBlank,
    DxgiOutput_TakeOwnership,
    DxgiOutput_ReleaseOwnership,
    DxgiOutput_GetGammaControlCapabilities,
    DxgiOutput_SetGammaControl,
    DxgiOutput_GetGammaControl,
    DxgiOutput_SetDisplaySurface,
    DxgiOutput_GetDisplaySurfaceData,
    DxgiOutput_GetFrameStatistics,
};

/*
 * ========================================================================
 *  Output creation
 * ========================================================================
 */

HRESULT DxgiOutput_Create(DxgiAdapter *pAdapter, UINT ordinal,
                           D3DKMT_HANDLE hAdapter,
                           D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
                           const WCHAR *DeviceName,
                           IDXGIOutput **ppOutput)
{
    DxgiOutput *output;

    if (!ppOutput)
        return E_POINTER;

    *ppOutput = NULL;

    output = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*output));
    if (!output)
        return E_OUTOFMEMORY;

    output->IDXGIOutput_iface.lpVtbl = &DxgiOutput_Vtbl;
    output->RefCount = 1;
    DxgiObject_Init(&output->Base);
    output->pAdapter = pAdapter;
    output->OutputOrdinal = ordinal;
    output->hAdapter = hAdapter;
    output->VidPnSourceId = VidPnSourceId;
    if (DeviceName)
        lstrcpynW(output->DeviceName, DeviceName,
                  sizeof(output->DeviceName) / sizeof(WCHAR));

    /* Take a reference on the parent adapter */
    if (pAdapter)
        IDXGIAdapter1_AddRef(&pAdapter->IDXGIAdapter1_iface);

    *ppOutput = &output->IDXGIOutput_iface;
    return S_OK;
}
