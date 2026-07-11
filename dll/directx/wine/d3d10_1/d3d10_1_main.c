/*
 * D3D10.1 surface scaffolding.
 */

#include "config.h"

#include "wine/debug.h"
#include "../dx_surface_stub.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d10);

HRESULT WINAPI D3D10CreateDevice1(void *adapter, UINT driver_type, HMODULE swrast,
        UINT flags, UINT feature_level, UINT sdk_version, void **device)
{
    FIXME("adapter %p, driver_type %u, swrast %p, flags %#x, feature_level %#x, sdk %#x, device %p: backend unavailable.\n",
            adapter, driver_type, swrast, flags, feature_level, sdk_version, device);
    dx_zero_pointer(device);
    return dx_surface_unsupported();
}

HRESULT WINAPI D3D10CreateDeviceAndSwapChain1(void *adapter, UINT driver_type, HMODULE swrast,
        UINT flags, UINT feature_level, UINT sdk_version, const void *swapchain_desc,
        void **swapchain, void **device)
{
    FIXME("adapter %p, driver_type %u, swrast %p, flags %#x, feature_level %#x, sdk %#x, desc %p: backend unavailable.\n",
            adapter, driver_type, swrast, flags, feature_level, sdk_version, swapchain_desc);
    dx_zero_pointer(swapchain);
    dx_zero_pointer(device);
    return dx_surface_unsupported();
}


BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR exe[260];
        exe[0] = 0;
        GetModuleFileNameW(NULL, exe, 260);
        ERR("d3d10_1.dll loaded into %s\n", debugstr_w(exe));
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
