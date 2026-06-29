/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DLL entry points: D3D11CreateDevice, D3D11CreateDeviceAndSwapChain
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(inst);
            break;
    }
    return TRUE;
}

static const D3D_FEATURE_LEVEL default_feature_levels[] =
{
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1,
};

HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter *adapter, D3D_DRIVER_TYPE driver_type,
        HMODULE swrast, UINT flags, const D3D_FEATURE_LEVEL *feature_levels,
        UINT levels, UINT sdk_version, ID3D11Device **device_out,
        D3D_FEATURE_LEVEL *obtained_feature_level, ID3D11DeviceContext **context_out)
{
    struct d3d11_device *device;
    HRESULT hr;

    TRACE("adapter %p, driver_type %d, swrast %p, flags %#x, feature_levels %p, "
            "levels %u, sdk_version %u, device %p, obtained_feature_level %p, context %p.\n",
            adapter, driver_type, swrast, flags, feature_levels,
            levels, sdk_version, device_out, obtained_feature_level, context_out);

    if (!feature_levels)
    {
        feature_levels = default_feature_levels;
        levels = ARRAY_SIZE(default_feature_levels);
    }

    if (device_out)
        *device_out = NULL;
    if (context_out)
        *context_out = NULL;
    if (obtained_feature_level)
        *obtained_feature_level = 0;

    if (sdk_version != D3D11_SDK_VERSION)
        return E_INVALIDARG;

    hr = d3d11_device_create(adapter, driver_type, flags, feature_levels, levels, &device);
    if (FAILED(hr))
    {
        WARN("Failed to create device, hr %#lx.\n", hr);
        return hr;
    }

    if (obtained_feature_level)
        *obtained_feature_level = device->feature_level;

    if (context_out)
    {
        *context_out = &device->immediate_context.ID3D11DeviceContext_iface;
        ID3D11DeviceContext_AddRef(*context_out);
    }

    if (device_out)
    {
        *device_out = &device->ID3D11Device_iface;
    }
    else
    {
        /* Caller doesn't want the device, release it. */
        ID3D11Device_Release(&device->ID3D11Device_iface);
    }

    return S_OK;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(IDXGIAdapter *adapter,
        D3D_DRIVER_TYPE driver_type, HMODULE swrast, UINT flags,
        const D3D_FEATURE_LEVEL *feature_levels, UINT levels, UINT sdk_version,
        const DXGI_SWAP_CHAIN_DESC *swapchain_desc, IDXGISwapChain **swapchain_out,
        ID3D11Device **device_out, D3D_FEATURE_LEVEL *obtained_feature_level,
        ID3D11DeviceContext **context_out)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    HRESULT hr;

    TRACE("adapter %p, driver_type %d, swrast %p, flags %#x, feature_levels %p, "
            "levels %u, sdk_version %u, swapchain_desc %p, swapchain %p, "
            "device %p, obtained_feature_level %p, context %p.\n",
            adapter, driver_type, swrast, flags, feature_levels,
            levels, sdk_version, swapchain_desc, swapchain_out,
            device_out, obtained_feature_level, context_out);

    if (swapchain_out)
        *swapchain_out = NULL;

    hr = D3D11CreateDevice(adapter, driver_type, swrast, flags, feature_levels,
            levels, sdk_version, &device, obtained_feature_level, &context);
    if (FAILED(hr))
        return hr;

    if (swapchain_desc && swapchain_out)
    {
        IDXGIDevice *dxgi_device;
        IDXGIAdapter *dxgi_adapter;
        IDXGIFactory *factory;

        hr = ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
        if (SUCCEEDED(hr))
        {
            hr = IDXGIDevice_GetAdapter(dxgi_device, &dxgi_adapter);
            if (SUCCEEDED(hr))
            {
                hr = IDXGIAdapter_GetParent(dxgi_adapter, &IID_IDXGIFactory, (void **)&factory);
                if (SUCCEEDED(hr))
                {
                    hr = IDXGIFactory_CreateSwapChain(factory, (IUnknown *)device,
                            (DXGI_SWAP_CHAIN_DESC *)swapchain_desc, swapchain_out);
                    IDXGIFactory_Release(factory);
                }
                IDXGIAdapter_Release(dxgi_adapter);
            }
            IDXGIDevice_Release(dxgi_device);
        }

        if (FAILED(hr))
        {
            WARN("Failed to create swap chain, hr %#lx.\n", hr);
            ID3D11DeviceContext_Release(context);
            ID3D11Device_Release(device);
            return hr;
        }
    }

    if (device_out)
        *device_out = device;
    else
        ID3D11Device_Release(device);

    if (context_out)
        *context_out = context;
    else
        ID3D11DeviceContext_Release(context);

    return S_OK;
}
