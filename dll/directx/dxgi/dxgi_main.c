/*
 * PROJECT:     ReactOS DXGI Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DLL entry point, factory creation, D3DKMT thunks, D3D10 device layer
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "dxgi_private.h"
#include <d3d10umddi.h>

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        WINE_TRACE("dxgi.dll loaded (hInst=%p)\n", hInstDLL);
        DisableThreadLibraryCalls(hInstDLL);
        break;
    case DLL_PROCESS_DETACH:
        WINE_TRACE("dxgi.dll unloaded\n");
        break;
    }

    return TRUE;
}

/*
 * ========================================================================
 *  Factory Creation Exports
 * ========================================================================
 */

HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **ppFactory)
{
    WINE_TRACE("(%s, %p)\n", wine_dbgstr_guid(riid), ppFactory);
    return DxgiFactory_Create(riid, ppFactory);
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **ppFactory)
{
    WINE_TRACE("(%s, %p)\n", wine_dbgstr_guid(riid), ppFactory);
    return DxgiFactory_Create(riid, ppFactory);
}

HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void **ppFactory)
{
    WINE_TRACE("(flags=0x%x, %s, %p)\n", Flags, wine_dbgstr_guid(riid), ppFactory);
    return DxgiFactory_Create(riid, ppFactory);
}

HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void **ppDebug)
{
    WINE_FIXME("(flags=0x%x, %s, %p) stub!\n", Flags, wine_dbgstr_guid(riid), ppDebug);

    if (ppDebug)
        *ppDebug = NULL;

    return DXGI_ERROR_NOT_FOUND;
}

/*
 * ========================================================================
 *  D3DKMT Kernel Thunk Exports
 *
 *  DXGI exports the D3DKMT names for import compatibility, but it must not
 *  pretend that kernel objects were created locally.  Forward to gdi32's real
 *  D3DKMT thunks when present; otherwise fail explicitly.
 * ========================================================================
 */

typedef NTSTATUS (WINAPI *PFN_DXGI_D3DKMT_THUNK)(void *);

static NTSTATUS
dxgi_forward_d3dkmt(const char *name, void *data)
{
    HMODULE gdi32;
    PFN_DXGI_D3DKMT_THUNK thunk;

    WINE_TRACE("%s(%p)\n", name, data);

    gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!gdi32)
        gdi32 = LoadLibraryW(L"gdi32.dll");
    if (!gdi32)
        return STATUS_DLL_NOT_FOUND;

    thunk = (PFN_DXGI_D3DKMT_THUNK)GetProcAddress(gdi32, name);
    if (!thunk)
        return STATUS_NOT_IMPLEMENTED;

    return thunk(data);
}

#define DXGI_FORWARD_D3DKMT(Name, Type) \
    NTSTATUS APIENTRY Name(Type *pData) { return dxgi_forward_d3dkmt(#Name, pData); }

#define DXGI_FORWARD_D3DKMT_CONST(Name, Type) \
    NTSTATUS APIENTRY Name(const Type *pData) { return dxgi_forward_d3dkmt(#Name, (void *)pData); }

DXGI_FORWARD_D3DKMT_CONST(D3DKMTCloseAdapter, D3DKMT_CLOSEADAPTER)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTDestroyAllocation, D3DKMT_DESTROYALLOCATION)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTDestroyContext, D3DKMT_DESTROYCONTEXT)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTDestroyDevice, D3DKMT_DESTROYDEVICE)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTDestroySynchronizationObject, D3DKMT_DESTROYSYNCHRONIZATIONOBJECT)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTSetDisplayPrivateDriverFormat, D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTSignalSynchronizationObject, D3DKMT_SIGNALSYNCHRONIZATIONOBJECT)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTUnlock, D3DKMT_UNLOCK)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTWaitForSynchronizationObject, D3DKMT_WAITFORSYNCHRONIZATIONOBJECT)
DXGI_FORWARD_D3DKMT(D3DKMTCreateAllocation, D3DKMT_CREATEALLOCATION)
DXGI_FORWARD_D3DKMT(D3DKMTCreateContext, D3DKMT_CREATECONTEXT)
DXGI_FORWARD_D3DKMT(D3DKMTCreateDevice, D3DKMT_CREATEDEVICE)
DXGI_FORWARD_D3DKMT(D3DKMTCreateSynchronizationObject, D3DKMT_CREATESYNCHRONIZATIONOBJECT)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTEscape, D3DKMT_ESCAPE)
DXGI_FORWARD_D3DKMT(D3DKMTGetContextSchedulingPriority, D3DKMT_GETCONTEXTSCHEDULINGPRIORITY)
DXGI_FORWARD_D3DKMT(D3DKMTGetDeviceState, D3DKMT_GETDEVICESTATE)
DXGI_FORWARD_D3DKMT(D3DKMTGetDisplayModeList, D3DKMT_GETDISPLAYMODELIST)
DXGI_FORWARD_D3DKMT(D3DKMTGetMultisampleMethodList, D3DKMT_GETMULTISAMPLEMETHODLIST)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTGetRuntimeData, D3DKMT_GETRUNTIMEDATA)
DXGI_FORWARD_D3DKMT(D3DKMTGetSharedPrimaryHandle, D3DKMT_GETSHAREDPRIMARYHANDLE)
DXGI_FORWARD_D3DKMT(D3DKMTLock, D3DKMT_LOCK)
DXGI_FORWARD_D3DKMT(D3DKMTOpenAdapterFromHdc, D3DKMT_OPENADAPTERFROMHDC)
DXGI_FORWARD_D3DKMT(D3DKMTOpenResource, D3DKMT_OPENRESOURCE)
DXGI_FORWARD_D3DKMT(D3DKMTPresent, D3DKMT_PRESENT)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTQueryAdapterInfo, D3DKMT_QUERYADAPTERINFO)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTQueryAllocationResidency, D3DKMT_QUERYALLOCATIONRESIDENCY)
DXGI_FORWARD_D3DKMT(D3DKMTQueryResourceInfo, D3DKMT_QUERYRESOURCEINFO)
DXGI_FORWARD_D3DKMT(D3DKMTRender, D3DKMT_RENDER)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTSetAllocationPriority, D3DKMT_SETALLOCATIONPRIORITY)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTSetContextSchedulingPriority, D3DKMT_SETCONTEXTSCHEDULINGPRIORITY)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTSetDisplayMode, D3DKMT_SETDISPLAYMODE)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTSetGammaRamp, D3DKMT_SETGAMMARAMP)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTSetVidPnSourceOwner, D3DKMT_SETVIDPNSOURCEOWNER)
DXGI_FORWARD_D3DKMT_CONST(D3DKMTWaitForVerticalBlankEvent, D3DKMT_WAITFORVERTICALBLANKEVENT)

#undef DXGI_FORWARD_D3DKMT
#undef DXGI_FORWARD_D3DKMT_CONST

/*
 * ========================================================================
 *  UMD (User-Mode Driver) Loading Exports
 *
 *  OpenAdapter10 / OpenAdapter10_2 populate the DXGI DDI function table
 *  used by D3D10/10.1 runtimes.  Version constant 0x0F07 indicates
 *  D3D10-level DDI support.
 * ========================================================================
 */

HRESULT WINAPI OpenAdapter10(D3D10DDIARG_OPENADAPTER *pArg)
{
    WINE_TRACE("(%p)\n", pArg);

    if (!pArg)
        return DXGI_ERROR_INVALID_CALL;

    pArg->hAdapter.pDrvPrivate = NULL;
    pArg->pAdapterFuncs = NULL;
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT WINAPI OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *pArg)
{
    WINE_TRACE("(%p)\n", pArg);

    if (!pArg)
        return DXGI_ERROR_INVALID_CALL;

    pArg->hAdapter.pDrvPrivate = NULL;
    pArg->pAdapterFuncs = NULL;
    pArg->pAdapterFuncs_2 = NULL;
    return DXGI_ERROR_UNSUPPORTED;
}

/*
 * ========================================================================
 *  D3D10 Device Creation / Layer Management Exports
 * ========================================================================
 */

HRESULT WINAPI DXGID3D10CreateDevice(HMODULE hModule, IDXGIFactory *pFactory,
                                      IDXGIAdapter *pAdapter, UINT Flags,
                                      void *pFeatureLevels, UINT FeatureLevels,
                                      void **ppDevice)
{
    WINE_FIXME("(%p, %p, %p, 0x%x, %p, %u, %p) stub!\n",
               hModule, pFactory, pAdapter, Flags, pFeatureLevels,
               FeatureLevels, ppDevice);

    if (ppDevice)
        *ppDevice = NULL;

    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT WINAPI DXGID3D10CreateLayeredDevice(void *pArg)
{
    WINE_FIXME("(%p) stub!\n", pArg);
    return DXGI_ERROR_UNSUPPORTED;
}

SIZE_T WINAPI DXGID3D10GetLayeredDeviceSize(void *pLayers, UINT NumLayers)
{
    WINE_FIXME("(%p, %u) stub!\n", pLayers, NumLayers);
    return 0;
}

HRESULT WINAPI DXGID3D10RegisterLayers(void *pLayers, UINT NumLayers)
{
    WINE_FIXME("(%p, %u) stub!\n", pLayers, NumLayers);
    return DXGI_ERROR_UNSUPPORTED;
}

/*
 * ========================================================================
 *  Diagnostic and Reporting Exports
 * ========================================================================
 */

HRESULT WINAPI DXGIDumpJournal(void)
{
    WINE_FIXME("stub!\n");
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT WINAPI DXGIReportAdapterConfiguration(DWORD dwFlags)
{
    WINE_FIXME("(flags=0x%lx) stub!\n", dwFlags);
    return DXGI_ERROR_UNSUPPORTED;
}
