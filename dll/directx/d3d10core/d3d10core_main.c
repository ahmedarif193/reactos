/*
 * PROJECT:     ReactOS Direct3D 10 Core Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3D10 Core Runtime — device creation, DDI layer, OpenAdapter
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * d3d10core.dll sits between the D3D10 API (d3d10.dll) and the WDDM
 * User-Mode Driver (UMD).  It registers itself with DXGI as a "layer",
 * negotiates DDI versions via OpenAdapter10/OpenAdapter10_2, and creates
 * the device object that wraps the UMD DDI function table.
 */

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <objbase.h>

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d10core);

/* Packed 64-bit version constants returned by D3D10CoreGetSupportedVersions.
 *   Bits 48-55: D3D major (10)
 *   Bits 32-47: Runtime sub-version (1=10.0, 9=10.1)
 *   Bits 16-31: DDI revision
 *   Bits 0-15:  Build number (6000 = 0x1770)
 */
#define D3D10_0_VERSION_PACKED  0x0A000100041770ULL   /* D3D10.0, DDI rev 4, build 6000 */
#define D3D10_1_VERSION_PACKED  0x0A000900001770ULL   /* D3D10.1, DDI rev 0, build 6000 */

#define D3DKMT_STATUS_INVALID_PARAMETER ((LONG)0xC000000DL)
#define D3DKMT_STATUS_NOT_IMPLEMENTED   ((LONG)0xC0000002L)

typedef LONG (WINAPI *PFN_D3DKMT_THUNK)(void *);

static HMODULE hGdi32;

static LONG
ForwardD3dkmtThunk(const char *Name, void *pData)
{
    PFN_D3DKMT_THUNK pfn;

    if (!pData)
        return D3DKMT_STATUS_INVALID_PARAMETER;

    if (!hGdi32)
        hGdi32 = LoadLibraryA("gdi32.dll");
    if (!hGdi32)
        return D3DKMT_STATUS_NOT_IMPLEMENTED;

    pfn = (PFN_D3DKMT_THUNK)GetProcAddress(hGdi32, Name);
    if (!pfn)
        return D3DKMT_STATUS_NOT_IMPLEMENTED;

    return pfn(pData);
}

#define D3DKMT_FORWARD_THUNK(Name) \
    LONG WINAPI Name(void *pData) { return ForwardD3dkmtThunk(#Name, pData); }

/* -----------------------------------------------------------------------
 * OpenAdapter10
 *
 * Initial handshake with the UMD for D3D10.0 devices.
 * Populates 3 adapter function pointers and sets the DDI version.
 *
 * Layout of D3D10DDIARG_OPENADAPTER (offsets from pOpenData):
 *   +0x08  hRTAdapter   — runtime adapter handle (set to DDI version)
 *   +0x20  pAdapterFuncs — pointer to output function table
 * ----------------------------------------------------------------------- */
HRESULT WINAPI OpenAdapter10(void *pOpenData)
{
    TRACE("(%p)\n", pOpenData);

    if (!pOpenData)
        return E_INVALIDARG;

    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * OpenAdapter10_2
 *
 * Extended handshake for D3D10.1 devices. Superset of OpenAdapter10 —
 * adds GetSupportedVersions and GetNotImplemented callbacks.
 * ----------------------------------------------------------------------- */
HRESULT WINAPI OpenAdapter10_2(void *pOpenData)
{
    TRACE("(%p)\n", pOpenData);

    if (!pOpenData)
        return E_INVALIDARG;

    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * D3D10CoreCreateDevice
 *
 * Main device creation entry point called by d3d10.dll (via DXGI).
 * For now this is a stub that returns E_NOTIMPL — the full implementation
 * will load the UMD, negotiate DDI, and create the device object.
 * ----------------------------------------------------------------------- */
HRESULT WINAPI D3D10CoreCreateDevice(
    void *pFactory,     /* IDXGIFactory* */
    void *pAdapter,     /* IDXGIAdapter* */
    UINT Flags,
    void *pUnknown,
    UINT SDKVersion,
    UINT FeatureLevel,
    void **ppDevice)
{
    TRACE("(%p, %p, %#x, %p, %u, %u, %p)\n",
          pFactory, pAdapter, Flags, pUnknown, SDKVersion, FeatureLevel, ppDevice);

    if (!ppDevice)
        return E_INVALIDARG;

    *ppDevice = NULL;

    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * D3D10CoreGetSupportedVersions
 *
 * Reports which DDI versions this runtime supports.
 *   type == 2 (D3D10.1): returns 2 versions (10.1 + 10.0)
 *   otherwise (D3D10.0): returns 1 version (10.0)
 * ----------------------------------------------------------------------- */
HRESULT WINAPI D3D10CoreGetSupportedVersions(
    UINT type,
    UINT *pCount,
    UINT64 *pVersions)
{
    UINT needed;

    TRACE("(%u, %p, %p)\n", type, pCount, pVersions);

    if (!pCount)
        return E_INVALIDARG;

    needed = (type == 2) ? 2 : 1;

    if (!pVersions)
    {
        *pCount = needed;
        return S_OK;
    }

    if (*pCount < needed)
    {
        *pCount = needed;
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    if (type == 2)
    {
        pVersions[0] = D3D10_1_VERSION_PACKED;
        pVersions[1] = D3D10_0_VERSION_PACKED;
        *pCount = 2;
    }
    else
    {
        pVersions[0] = D3D10_0_VERSION_PACKED;
        *pCount = 1;
    }

    return S_OK;
}

/* -----------------------------------------------------------------------
 * D3D10CoreGetVersion
 * ----------------------------------------------------------------------- */
UINT64 WINAPI D3D10CoreGetVersion(void)
{
    TRACE("()\n");
    return D3D10_0_VERSION_PACKED;
}

/* -----------------------------------------------------------------------
 * D3D10CoreRegisterLayers
 *
 * Registers the D3D10 device layer with DXGI's layer system.
 * Full implementation will call DXGID3D10RegisterLayers.
 * ----------------------------------------------------------------------- */
HRESULT WINAPI D3D10CoreRegisterLayers(void)
{
    TRACE("()\n");
    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * D3DKMT thunk exports.
 *
 * Windows exposes these ordinals from d3d10core, but the actual work belongs
 * in the D3DKMT runtime path. Forward to gdi32 when available and fail closed
 * when it is not, instead of reporting success without touching the kernel.
 * ----------------------------------------------------------------------- */

D3DKMT_FORWARD_THUNK(D3DKMTCloseAdapter)
D3DKMT_FORWARD_THUNK(D3DKMTDestroyAllocation)
D3DKMT_FORWARD_THUNK(D3DKMTDestroyContext)
D3DKMT_FORWARD_THUNK(D3DKMTDestroyDevice)
D3DKMT_FORWARD_THUNK(D3DKMTDestroySynchronizationObject)
D3DKMT_FORWARD_THUNK(D3DKMTSetDisplayPrivateDriverFormat)
D3DKMT_FORWARD_THUNK(D3DKMTSignalSynchronizationObject)
D3DKMT_FORWARD_THUNK(D3DKMTUnlock)
D3DKMT_FORWARD_THUNK(D3DKMTWaitForSynchronizationObject)
D3DKMT_FORWARD_THUNK(D3DKMTCreateAllocation)
D3DKMT_FORWARD_THUNK(D3DKMTCreateContext)
D3DKMT_FORWARD_THUNK(D3DKMTCreateDevice)
D3DKMT_FORWARD_THUNK(D3DKMTCreateSynchronizationObject)
D3DKMT_FORWARD_THUNK(D3DKMTEscape)
D3DKMT_FORWARD_THUNK(D3DKMTGetContextSchedulingPriority)
D3DKMT_FORWARD_THUNK(D3DKMTGetDeviceState)
D3DKMT_FORWARD_THUNK(D3DKMTGetDisplayModeList)
D3DKMT_FORWARD_THUNK(D3DKMTGetMultisampleMethodList)
D3DKMT_FORWARD_THUNK(D3DKMTGetRuntimeData)
D3DKMT_FORWARD_THUNK(D3DKMTGetSharedPrimaryHandle)
D3DKMT_FORWARD_THUNK(D3DKMTLock)
D3DKMT_FORWARD_THUNK(D3DKMTOpenAdapterFromHdc)
D3DKMT_FORWARD_THUNK(D3DKMTOpenResource)
D3DKMT_FORWARD_THUNK(D3DKMTPresent)
D3DKMT_FORWARD_THUNK(D3DKMTQueryAdapterInfo)
D3DKMT_FORWARD_THUNK(D3DKMTQueryAllocationResidency)
D3DKMT_FORWARD_THUNK(D3DKMTQueryResourceInfo)
D3DKMT_FORWARD_THUNK(D3DKMTRender)
D3DKMT_FORWARD_THUNK(D3DKMTSetAllocationPriority)
D3DKMT_FORWARD_THUNK(D3DKMTSetContextSchedulingPriority)
D3DKMT_FORWARD_THUNK(D3DKMTSetDisplayMode)
D3DKMT_FORWARD_THUNK(D3DKMTSetGammaRamp)
D3DKMT_FORWARD_THUNK(D3DKMTSetVidPnSourceOwner)
D3DKMT_FORWARD_THUNK(D3DKMTWaitForVerticalBlankEvent)

/* -----------------------------------------------------------------------
 * DllMain
 * ----------------------------------------------------------------------- */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstDLL);
        break;
    case DLL_PROCESS_DETACH:
        if (hGdi32)
        {
            FreeLibrary(hGdi32);
            hGdi32 = NULL;
        }
        break;
    }
    return TRUE;
}
