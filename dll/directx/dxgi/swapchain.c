/*
 * PROJECT:     ReactOS DXGI Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     IDXGISwapChain implementation
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "dxgi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

#define DXGI_SWAPCHAIN_MAX_SYNC_INTERVAL 4
#define DXGI_PRESENT_KNOWN_FLAGS (DXGI_PRESENT_TEST | \
                                  DXGI_PRESENT_DO_NOT_SEQUENCE | \
                                  DXGI_PRESENT_RESTART | \
                                  DXGI_PRESENT_DO_NOT_WAIT | \
                                  DXGI_PRESENT_STEREO_PREFER_RIGHT | \
                                  DXGI_PRESENT_STEREO_TEMPORARY_MONO | \
                                  DXGI_PRESENT_RESTRICT_TO_OUTPUT | \
                                  DXGI_PRESENT_USE_DURATION | \
                                  DXGI_PRESENT_ALLOW_TEARING)
#define DXGI_PRESENT_IMPLEMENTED_FLAGS 0
#define DXGI_SWAPCHAIN_SUPPORTED_FLAGS 0
#define DXGI_SWAPCHAIN_SUPPORTED_USAGE (DXGI_USAGE_SHADER_INPUT | \
                                        DXGI_USAGE_RENDER_TARGET_OUTPUT | \
                                        DXGI_USAGE_BACK_BUFFER | \
                                        DXGI_USAGE_DISCARD_ON_PRESENT)

static BOOL DxgiSwapChain_IsSupportedFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return TRUE;
        default:
            return FALSE;
    }
}

static HRESULT DxgiSwapChain_ValidateDesc(const DXGI_SWAP_CHAIN_DESC *desc)
{
    if (!desc)
        return DXGI_ERROR_INVALID_CALL;

    if (!desc->OutputWindow || !IsWindow(desc->OutputWindow))
        return DXGI_ERROR_INVALID_CALL;

    if (!desc->Windowed)
        return DXGI_ERROR_UNSUPPORTED;

    if (desc->BufferCount != 1)
        return DXGI_ERROR_UNSUPPORTED;

    if (!desc->BufferDesc.Width || !desc->BufferDesc.Height ||
        desc->BufferDesc.Format == DXGI_FORMAT_UNKNOWN)
        return DXGI_ERROR_INVALID_CALL;

    if (!DxgiSwapChain_IsSupportedFormat(desc->BufferDesc.Format))
        return DXGI_ERROR_UNSUPPORTED;

    if (desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality != 0)
        return DXGI_ERROR_UNSUPPORTED;

    if (!desc->BufferUsage || (desc->BufferUsage & ~DXGI_SWAPCHAIN_SUPPORTED_USAGE))
        return DXGI_ERROR_UNSUPPORTED;

    if (desc->SwapEffect != DXGI_SWAP_EFFECT_DISCARD &&
        desc->SwapEffect != DXGI_SWAP_EFFECT_SEQUENTIAL)
        return DXGI_ERROR_UNSUPPORTED;

    if (desc->Flags & ~DXGI_SWAPCHAIN_SUPPORTED_FLAGS)
        return DXGI_ERROR_UNSUPPORTED;

    return S_OK;
}

static HRESULT DxgiSwapChain_CreateBackBuffer(DxgiSwapChain *This,
                                               const DXGI_SWAP_CHAIN_DESC *desc,
                                               IDXGISurface **back_buffer)
{
    IDXGIDevice *dxgi_device = NULL;
    IDXGISurface *surface = NULL;
    DXGI_SURFACE_DESC surface_desc;
    HRESULT hr;

    if (!back_buffer)
        return DXGI_ERROR_INVALID_CALL;

    *back_buffer = NULL;

    hr = DxgiSwapChain_ValidateDesc(desc);
    if (FAILED(hr))
        return hr;

    if (!This->pDevice)
        return DXGI_ERROR_DEVICE_REMOVED;

    hr = IUnknown_QueryInterface(This->pDevice, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (FAILED(hr))
        return DXGI_ERROR_INVALID_CALL;

    memset(&surface_desc, 0, sizeof(surface_desc));
    surface_desc.Width = desc->BufferDesc.Width;
    surface_desc.Height = desc->BufferDesc.Height;
    surface_desc.Format = desc->BufferDesc.Format;
    surface_desc.SampleDesc = desc->SampleDesc;

    hr = IDXGIDevice_CreateSurface(dxgi_device, &surface_desc, 1,
                                   desc->BufferUsage, NULL, &surface);
    IDXGIDevice_Release(dxgi_device);
    if (FAILED(hr))
        return hr;

    *back_buffer = surface;
    return S_OK;
}

static void DxgiSwapChain_ReplaceBackBuffer(DxgiSwapChain *This,
                                             IDXGISurface *back_buffer)
{
    if (This->pBackBuffer)
        IDXGISurface_Release(This->pBackBuffer);

    This->pBackBuffer = back_buffer;
}

/*
 * ========================================================================
 *  IUnknown
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_QueryInterface(
    IDXGISwapChain *iface, REFIID riid, void **ppvObject)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%s, %p)\n", This, wine_dbgstr_guid(riid), ppvObject);

    if (!ppvObject)
        return E_POINTER;

    *ppvObject = NULL;

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IDXGIObject) ||
        IsEqualGUID(riid, &IID_IDXGIDeviceSubObject) ||
        IsEqualGUID(riid, &IID_IDXGISwapChain))
    {
        *ppvObject = &This->IDXGISwapChain_iface;
        IDXGISwapChain_AddRef(&This->IDXGISwapChain_iface);
        return S_OK;
    }

    WINE_WARN("(%p) unknown interface %s\n", This, wine_dbgstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DxgiSwapChain_AddRef(IDXGISwapChain *iface)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);
    ULONG ref = InterlockedIncrement(&This->RefCount);
    WINE_TRACE("(%p) ref=%lu\n", This, ref);
    return ref;
}

static ULONG STDMETHODCALLTYPE DxgiSwapChain_Release(IDXGISwapChain *iface)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);
    ULONG ref = InterlockedDecrement(&This->RefCount);

    WINE_TRACE("(%p) ref=%lu\n", This, ref);

    if (ref == 0)
    {
        if (This->pBackBuffer)
            IDXGISurface_Release(This->pBackBuffer);
        if (This->pFullscreenOutput)
            IDXGIOutput_Release(This->pFullscreenOutput);
        if (This->pDevice)
            IUnknown_Release(This->pDevice);
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

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_SetPrivateData(
    IDXGISwapChain *iface, REFGUID Name, UINT DataSize, const void *pData)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);
    return DxgiObject_SetPrivateData(&This->Base, Name, DataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_SetPrivateDataInterface(
    IDXGISwapChain *iface, REFGUID Name, const IUnknown *pUnknown)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);
    return DxgiObject_SetPrivateDataInterface(&This->Base, Name, pUnknown);
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetPrivateData(
    IDXGISwapChain *iface, REFGUID Name, UINT *pDataSize, void *pData)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);
    return DxgiObject_GetPrivateData(&This->Base, Name, pDataSize, pData);
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetParent(
    IDXGISwapChain *iface, REFIID riid, void **ppParent)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

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
 *  IDXGIDeviceSubObject
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetDevice(
    IDXGISwapChain *iface, REFIID riid, void **ppDevice)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%s, %p)\n", This, wine_dbgstr_guid(riid), ppDevice);

    if (!ppDevice)
        return E_POINTER;

    *ppDevice = NULL;

    if (!This->pDevice)
        return DXGI_ERROR_DEVICE_REMOVED;

    return IUnknown_QueryInterface(This->pDevice, riid, ppDevice);
}

/*
 * ========================================================================
 *  IDXGISwapChain
 * ========================================================================
 */

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_Present(
    IDXGISwapChain *iface, UINT SyncInterval, UINT Flags)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(sync=%u, flags=0x%x)\n", This, SyncInterval, Flags);

    if (SyncInterval > DXGI_SWAPCHAIN_MAX_SYNC_INTERVAL)
        return DXGI_ERROR_INVALID_CALL;

    if (Flags & ~DXGI_PRESENT_KNOWN_FLAGS)
        return DXGI_ERROR_INVALID_CALL;

    if (Flags & ~DXGI_PRESENT_IMPLEMENTED_FLAGS)
        return DXGI_ERROR_UNSUPPORTED;

    if (!This->pBackBuffer)
        return DXGI_ERROR_INVALID_CALL;

    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetBuffer(
    IDXGISwapChain *iface, UINT Buffer, REFIID riid, void **ppSurface)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%u, %s, %p)\n", This, Buffer,
               wine_dbgstr_guid(riid), ppSurface);

    if (!ppSurface)
        return E_POINTER;

    *ppSurface = NULL;

    if (!riid)
        return DXGI_ERROR_INVALID_CALL;

    if (Buffer >= This->Desc.BufferCount)
        return DXGI_ERROR_INVALID_CALL;

    if (Buffer != 0 || !This->pBackBuffer)
        return DXGI_ERROR_INVALID_CALL;

    return IDXGISurface_QueryInterface(This->pBackBuffer, riid, ppSurface);
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_SetFullscreenState(
    IDXGISwapChain *iface, BOOL Fullscreen, IDXGIOutput *pTarget)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%d, %p)\n", This, Fullscreen, pTarget);

    if (Fullscreen)
        return DXGI_ERROR_UNSUPPORTED;

    This->Fullscreen = FALSE;
    This->Desc.Windowed = TRUE;

    if (This->pFullscreenOutput)
    {
        IDXGIOutput_Release(This->pFullscreenOutput);
        This->pFullscreenOutput = NULL;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetFullscreenState(
    IDXGISwapChain *iface, BOOL *pFullscreen, IDXGIOutput **ppTarget)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%p, %p)\n", This, pFullscreen, ppTarget);

    if (!pFullscreen)
        return DXGI_ERROR_INVALID_CALL;

    *pFullscreen = This->Fullscreen;

    if (ppTarget)
    {
        *ppTarget = NULL;
        if (This->Fullscreen && This->pFullscreenOutput)
        {
            *ppTarget = This->pFullscreenOutput;
            IDXGIOutput_AddRef(This->pFullscreenOutput);
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetDesc(
    IDXGISwapChain *iface, DXGI_SWAP_CHAIN_DESC *pDesc)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pDesc);

    if (!pDesc)
        return DXGI_ERROR_INVALID_CALL;

    *pDesc = This->Desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_ResizeBuffers(
    IDXGISwapChain *iface, UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);
    DXGI_SWAP_CHAIN_DESC new_desc;
    IDXGISurface *back_buffer = NULL;
    HRESULT hr;

    WINE_TRACE("(%p)->(%u, %u, %u, %u, 0x%x)\n",
               This, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (BufferCount > 1)
        return DXGI_ERROR_UNSUPPORTED;

    new_desc = This->Desc;

    if (BufferCount)
        new_desc.BufferCount = BufferCount;
    if (Width)
        new_desc.BufferDesc.Width = Width;
    if (Height)
        new_desc.BufferDesc.Height = Height;
    if (NewFormat != DXGI_FORMAT_UNKNOWN)
        new_desc.BufferDesc.Format = NewFormat;
    new_desc.Flags = SwapChainFlags;

    hr = DxgiSwapChain_CreateBackBuffer(This, &new_desc, &back_buffer);
    if (FAILED(hr))
        return hr;

    DxgiSwapChain_ReplaceBackBuffer(This, back_buffer);
    This->Desc = new_desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_ResizeTarget(
    IDXGISwapChain *iface, const DXGI_MODE_DESC *pNewTargetParameters)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pNewTargetParameters);

    if (!pNewTargetParameters)
        return DXGI_ERROR_INVALID_CALL;

    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetContainingOutput(
    IDXGISwapChain *iface, IDXGIOutput **ppOutput)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%p)\n", This, ppOutput);

    if (!ppOutput)
        return DXGI_ERROR_INVALID_CALL;

    *ppOutput = NULL;

    if (This->pFullscreenOutput)
    {
        *ppOutput = This->pFullscreenOutput;
        IDXGIOutput_AddRef(*ppOutput);
        return S_OK;
    }

    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetFrameStatistics(
    IDXGISwapChain *iface, DXGI_FRAME_STATISTICS *pStats)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pStats);

    if (!pStats)
        return DXGI_ERROR_INVALID_CALL;

    memset(pStats, 0, sizeof(*pStats));
    pStats->PresentCount = This->PresentCount;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DxgiSwapChain_GetLastPresentCount(
    IDXGISwapChain *iface, UINT *pLastPresentCount)
{
    DxgiSwapChain *This = impl_from_IDXGISwapChain(iface);

    WINE_TRACE("(%p)->(%p)\n", This, pLastPresentCount);

    if (!pLastPresentCount)
        return DXGI_ERROR_INVALID_CALL;

    *pLastPresentCount = This->PresentCount;
    return S_OK;
}

/*
 * ========================================================================
 *  VTable
 * ========================================================================
 */

static const IDXGISwapChainVtbl DxgiSwapChain_Vtbl =
{
    /* IUnknown */
    DxgiSwapChain_QueryInterface,
    DxgiSwapChain_AddRef,
    DxgiSwapChain_Release,
    /* IDXGIObject */
    DxgiSwapChain_SetPrivateData,
    DxgiSwapChain_SetPrivateDataInterface,
    DxgiSwapChain_GetPrivateData,
    DxgiSwapChain_GetParent,
    /* IDXGIDeviceSubObject */
    DxgiSwapChain_GetDevice,
    /* IDXGISwapChain */
    DxgiSwapChain_Present,
    DxgiSwapChain_GetBuffer,
    DxgiSwapChain_SetFullscreenState,
    DxgiSwapChain_GetFullscreenState,
    DxgiSwapChain_GetDesc,
    DxgiSwapChain_ResizeBuffers,
    DxgiSwapChain_ResizeTarget,
    DxgiSwapChain_GetContainingOutput,
    DxgiSwapChain_GetFrameStatistics,
    DxgiSwapChain_GetLastPresentCount,
};

/*
 * ========================================================================
 *  Swap chain creation
 * ========================================================================
 */

HRESULT DxgiSwapChain_Create(DxgiFactory *pFactory, IUnknown *pDevice,
                              DXGI_SWAP_CHAIN_DESC *pDesc,
                              IDXGISwapChain **ppSwapChain)
{
    DxgiSwapChain *sc;
    IDXGISurface *back_buffer = NULL;
    HRESULT hr;

    if (!ppSwapChain || !pDesc || !pDevice)
        return DXGI_ERROR_INVALID_CALL;

    *ppSwapChain = NULL;

    hr = DxgiSwapChain_ValidateDesc(pDesc);
    if (FAILED(hr))
        return hr;

    sc = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sc));
    if (!sc)
        return E_OUTOFMEMORY;

    sc->IDXGISwapChain_iface.lpVtbl = &DxgiSwapChain_Vtbl;
    sc->RefCount = 1;
    DxgiObject_Init(&sc->Base);
    sc->Desc = *pDesc;
    sc->Fullscreen = FALSE;

    sc->pFactory = pFactory;
    if (pFactory)
        IDXGIFactory1_AddRef(&pFactory->IDXGIFactory1_iface);

    sc->pDevice = pDevice;
    if (pDevice)
        IUnknown_AddRef(pDevice);

    hr = DxgiSwapChain_CreateBackBuffer(sc, &sc->Desc, &back_buffer);
    if (FAILED(hr))
    {
        IDXGISwapChain_Release(&sc->IDXGISwapChain_iface);
        return hr;
    }

    sc->pBackBuffer = back_buffer;
    *ppSwapChain = &sc->IDXGISwapChain_iface;
    return S_OK;
}
