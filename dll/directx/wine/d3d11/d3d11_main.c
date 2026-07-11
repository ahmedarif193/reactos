/*
 * D3D11 surface scaffolding.
 */

#include "config.h"

#include "wine/debug.h"
#include "../dx_surface_stub.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

HRESULT WINAPI D3D11CoreRegisterLayers(void)
{
    FIXME("stub.\n");
    return S_OK;
}

HRESULT WINAPI D3D11CoreCreateDevice(void *factory, void *adapter, UINT flags,
        const void *feature_levels, UINT level_count, void **device)
{
    FIXME("factory %p, adapter %p, flags %#x, levels %p, level_count %u, device %p: backend unavailable.\n",
            factory, adapter, flags, feature_levels, level_count, device);
    dx_zero_pointer(device);
    return dx_surface_unsupported();
}

HRESULT WINAPI D3D11CreateDevice(void *adapter, UINT driver_type, HMODULE swrast,
        UINT flags, const void *feature_levels, UINT level_count, UINT sdk_version,
        void **device, UINT *obtained_feature_level, void **immediate_context)
{
    FIXME("adapter %p, driver_type %u, swrast %p, flags %#x, levels %p, level_count %u, sdk %#x: backend unavailable.\n",
            adapter, driver_type, swrast, flags, feature_levels, level_count, sdk_version);
    dx_zero_pointer(device);
    dx_zero_pointer(immediate_context);
    if (obtained_feature_level)
        *obtained_feature_level = 0;
    return dx_surface_unsupported();
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(void *adapter, UINT driver_type, HMODULE swrast,
        UINT flags, const void *feature_levels, UINT level_count, UINT sdk_version,
        const void *swapchain_desc, void **swapchain, void **device,
        UINT *obtained_feature_level, void **immediate_context)
{
    FIXME("adapter %p, driver_type %u, swrast %p, flags %#x, levels %p, level_count %u, sdk %#x, desc %p: backend unavailable.\n",
            adapter, driver_type, swrast, flags, feature_levels, level_count, sdk_version, swapchain_desc);
    dx_zero_pointer(swapchain);
    dx_zero_pointer(device);
    dx_zero_pointer(immediate_context);
    if (obtained_feature_level)
        *obtained_feature_level = 0;
    return dx_surface_unsupported();
}

HRESULT WINAPI D3D11On12CreateDevice(void *d3d12_device, UINT flags,
        const void *feature_levels, UINT feature_level_count, void *const *queues,
        UINT queue_count, UINT node_mask, void **device,
        void **immediate_context, UINT *obtained_feature_level)
{
    FIXME("device %p, flags %#x, levels %p, level_count %u, queues %p, queue_count %u, node_mask %#x: unsupported.\n",
            d3d12_device, flags, feature_levels, feature_level_count, queues, queue_count, node_mask);
    dx_zero_pointer(device);
    dx_zero_pointer(immediate_context);
    if (obtained_feature_level)
        *obtained_feature_level = 0;
    return dx_surface_unsupported();
}


BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR exe[260];
        exe[0] = 0;
        GetModuleFileNameW(NULL, exe, 260);
        ERR("d3d11.dll loaded into %s\n", debugstr_w(exe));
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
