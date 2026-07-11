/*
 * D3D10 core surface scaffolding.
 */

#include "config.h"

#include "wine/debug.h"
#include "../dx_surface_stub.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d10core);

HRESULT WINAPI D3D10CoreRegisterLayers(void)
{
    FIXME("stub.\n");
    return S_OK;
}

HRESULT WINAPI D3D10CoreCreateDevice(void *factory, void *adapter,
        UINT flags, UINT sdk_version, void **device)
{
    FIXME("factory %p, adapter %p, flags %#x, sdk %#x, device %p: backend unavailable.\n",
            factory, adapter, flags, sdk_version, device);
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
        ERR("d3d10core.dll loaded into %s\n", debugstr_w(exe));
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
