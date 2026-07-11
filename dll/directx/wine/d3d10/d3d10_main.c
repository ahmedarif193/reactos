/*
 * D3D10 surface scaffolding.
 */

#include "config.h"

#include "wine/debug.h"
#include "../dx_surface_stub.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d10);

HRESULT WINAPI D3D10CreateDevice(void *adapter, UINT driver_type, HMODULE swrast,
        UINT flags, UINT sdk_version, void **device)
{
    FIXME("adapter %p, driver_type %u, swrast %p, flags %#x, sdk %#x, device %p: backend unavailable.\n",
            adapter, driver_type, swrast, flags, sdk_version, device);
    dx_zero_pointer(device);
    return dx_surface_unsupported();
}

HRESULT WINAPI D3D10CreateDeviceAndSwapChain(void *adapter, UINT driver_type, HMODULE swrast,
        UINT flags, UINT sdk_version, const void *swapchain_desc, void **swapchain,
        void **device)
{
    FIXME("adapter %p, driver_type %u, swrast %p, flags %#x, sdk %#x, desc %p: backend unavailable.\n",
            adapter, driver_type, swrast, flags, sdk_version, swapchain_desc);
    dx_zero_pointer(swapchain);
    dx_zero_pointer(device);
    return dx_surface_unsupported();
}

HRESULT WINAPI D3D10CompileEffectFromMemory(void *data, SIZE_T data_size, const char *filename,
        const void *defines, void *include, UINT hlsl_flags, UINT fx_flags, void **effect,
        void **errors)
{
    FIXME("data %p, size %lu, file %s, defines %p, include %p, hlsl_flags %#x, fx_flags %#x: stub.\n",
            data, (ULONG)data_size, debugstr_a(filename), defines, include, hlsl_flags, fx_flags);
    dx_zero_pointer(effect);
    dx_zero_pointer(errors);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CompileShader(const char *data, SIZE_T data_size, const char *filename,
        const void *defines, void *include, const char *entrypoint, const char *profile,
        UINT flags, void **shader, void **errors)
{
    FIXME("data %p, size %lu, file %s, entry %s, profile %s, flags %#x: stub.\n",
            data, (ULONG)data_size, debugstr_a(filename), debugstr_a(entrypoint), debugstr_a(profile), flags);
    dx_zero_pointer(shader);
    dx_zero_pointer(errors);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateEffectFromMemory(void *data, SIZE_T data_size, UINT fx_flags,
        void *device, void *effect_pool, void **effect)
{
    FIXME("data %p, size %lu, flags %#x, device %p, pool %p, effect %p: stub.\n",
            data, (ULONG)data_size, fx_flags, device, effect_pool, effect);
    dx_zero_pointer(effect);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateEffectPoolFromMemory(void *data, SIZE_T data_size, UINT fx_flags,
        void *device, void **effect_pool)
{
    FIXME("data %p, size %lu, flags %#x, device %p, pool %p: stub.\n",
            data, (ULONG)data_size, fx_flags, device, effect_pool);
    dx_zero_pointer(effect_pool);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateStateBlock(void *device, void *state_block_mask, void **state_block)
{
    FIXME("device %p, mask %p, state_block %p: stub.\n", device, state_block_mask, state_block);
    dx_zero_pointer(state_block);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10DisassembleShader(const void *data, SIZE_T data_size,
        BOOL color_code, const char *comments, void **disassembly)
{
    FIXME("data %p, size %lu, color %u, comments %s, disassembly %p: stub.\n",
            data, (ULONG)data_size, color_code, debugstr_a(comments), disassembly);
    dx_zero_pointer(disassembly);
    return E_NOTIMPL;
}

const char *WINAPI D3D10GetGeometryShaderProfile(void *device)
{
    FIXME("device %p: stub.\n", device);
    return NULL;
}

const char *WINAPI D3D10GetPixelShaderProfile(void *device)
{
    FIXME("device %p: stub.\n", device);
    return NULL;
}

const char *WINAPI D3D10GetVertexShaderProfile(void *device)
{
    FIXME("device %p: stub.\n", device);
    return NULL;
}

HRESULT WINAPI D3D10ReflectShader(const void *data, SIZE_T data_size, void **reflector)
{
    FIXME("data %p, size %lu, reflector %p: stub.\n", data, (ULONG)data_size, reflector);
    dx_zero_pointer(reflector);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskDifference(void *mask_x, void *mask_y, void *result)
{
    FIXME("mask_x %p, mask_y %p, result %p: stub.\n", mask_x, mask_y, result);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskDisableAll(void *mask)
{
    FIXME("mask %p: stub.\n", mask);
    if (!mask)
        return E_INVALIDARG;
    ZeroMemory(mask, 256);
    return S_OK;
}

HRESULT WINAPI D3D10StateBlockMaskDisableCapture(void *mask, UINT state_type, UINT range_start, UINT range_length)
{
    FIXME("mask %p, state_type %u, start %u, length %u: stub.\n", mask, state_type, range_start, range_length);
    return mask ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI D3D10StateBlockMaskEnableAll(void *mask)
{
    FIXME("mask %p: stub.\n", mask);
    if (!mask)
        return E_INVALIDARG;
    FillMemory(mask, 256, 0xff);
    return S_OK;
}

HRESULT WINAPI D3D10StateBlockMaskEnableCapture(void *mask, UINT state_type, UINT range_start, UINT range_length)
{
    FIXME("mask %p, state_type %u, start %u, length %u: stub.\n", mask, state_type, range_start, range_length);
    return mask ? S_OK : E_INVALIDARG;
}

BOOL WINAPI D3D10StateBlockMaskGetSetting(void *mask, UINT state_type, UINT entry)
{
    FIXME("mask %p, state_type %u, entry %u: stub.\n", mask, state_type, entry);
    return FALSE;
}

HRESULT WINAPI D3D10StateBlockMaskIntersect(void *mask_x, void *mask_y, void *result)
{
    FIXME("mask_x %p, mask_y %p, result %p: stub.\n", mask_x, mask_y, result);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskUnion(void *mask_x, void *mask_y, void *result)
{
    FIXME("mask_x %p, mask_y %p, result %p: stub.\n", mask_x, mask_y, result);
    return E_NOTIMPL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR exe[260];
        exe[0] = 0;
        GetModuleFileNameW(NULL, exe, 260);
        ERR("d3d10.dll loaded into %s\n", debugstr_w(exe));
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
