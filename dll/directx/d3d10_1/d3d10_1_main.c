/*
 * PROJECT:     ReactOS Direct3D 10.1 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3D10.1 coordination layer — feature-level device creation + forwarding to d3d10.dll
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * d3d10_1.dll is a thin layer that adds feature-level-aware device creation
 * (D3D10CreateDevice1 / D3D10CreateDeviceAndSwapChain1) and forwards all
 * other D3D10 utility functions to d3d10.dll via dynamic resolution.
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

WINE_DEFAULT_DEBUG_CHANNEL(d3d10_1);

#define D3D10_1_SDK_VERSION 32

/* Feature level constants */
#define D3D10_FEATURE_LEVEL_10_0  0xA000
#define D3D10_FEATURE_LEVEL_10_1  0xA100
#define D3D10_FEATURE_LEVEL_9_1   0x9100
#define D3D10_FEATURE_LEVEL_9_2   0x9200
#define D3D10_FEATURE_LEVEL_9_3   0x9300

/* -----------------------------------------------------------------------
 * Dynamic resolution of d3d10.dll functions
 * ----------------------------------------------------------------------- */
static HMODULE hD3D10;

static HMODULE EnsureD3D10(void)
{
    if (!hD3D10)
    {
        hD3D10 = GetModuleHandleA("d3d10.dll");
        if (!hD3D10)
            hD3D10 = LoadLibraryA("d3d10.dll");
    }
    return hD3D10;
}

static FARPROC ResolveD3D10(const char *name)
{
    HMODULE hMod = EnsureD3D10();
    if (!hMod) return NULL;
    return GetProcAddress(hMod, name);
}

/* -----------------------------------------------------------------------
 * D3D10.1-specific device creation
 * ----------------------------------------------------------------------- */

/* Forward declaration */
HRESULT WINAPI D3D10CreateDeviceAndSwapChain1(
    void *pAdapter, UINT DriverType, HMODULE Software, UINT Flags,
    UINT HardwareLevel, UINT SDKVersion,
    void *pSwapChainDesc, void **ppSwapChain, void **ppDevice);

HRESULT WINAPI D3D10CreateDevice1(
    void *pAdapter,
    UINT DriverType,
    HMODULE Software,
    UINT Flags,
    UINT HardwareLevel,
    UINT SDKVersion,
    void **ppDevice)
{
    TRACE("(%p, %u, %p, %#x, %#x, %u, %p)\n",
          pAdapter, DriverType, Software, Flags, HardwareLevel,
          SDKVersion, ppDevice);

    return D3D10CreateDeviceAndSwapChain1(pAdapter, DriverType, Software,
                                          Flags, HardwareLevel, SDKVersion,
                                          NULL, NULL, ppDevice);
}

HRESULT WINAPI D3D10CreateDeviceAndSwapChain1(
    void *pAdapter,
    UINT DriverType,
    HMODULE Software,
    UINT Flags,
    UINT HardwareLevel,
    UINT SDKVersion,
    void *pSwapChainDesc,
    void **ppSwapChain,
    void **ppDevice)
{
    TRACE("(%p, %u, %p, %#x, %#x, %u, %p, %p, %p)\n",
          pAdapter, DriverType, Software, Flags, HardwareLevel,
          SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice);

    if (ppSwapChain) *ppSwapChain = NULL;
    if (ppDevice)    *ppDevice = NULL;

    if (!ppDevice)
        return E_INVALIDARG;

    if (SDKVersion != D3D10_1_SDK_VERSION)
    {
        ERR("SDK version mismatch: app compiled with %u, runtime is %u\n",
            SDKVersion, D3D10_1_SDK_VERSION);
        return E_INVALIDARG;
    }

    /* Validate feature level */
    switch (HardwareLevel)
    {
    case D3D10_FEATURE_LEVEL_10_0:
    case D3D10_FEATURE_LEVEL_10_1:
    case D3D10_FEATURE_LEVEL_9_1:
    case D3D10_FEATURE_LEVEL_9_2:
    case D3D10_FEATURE_LEVEL_9_3:
        break;
    default:
        WARN("Feature level %#x not supported by this D3D10.1 layer\n", HardwareLevel);
        return E_INVALIDARG;
    }

    WARN("D3D10.1 device creation needs an ID3D10Device1-capable runtime.\n");
    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * Forwarded functions — resolved dynamically from d3d10.dll
 * ----------------------------------------------------------------------- */

UINT WINAPI D3D10GetVersion(void)
{
    typedef UINT (WINAPI *PFN)(void);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetVersion");
    if (pfn)
        return pfn();
    return 0;
}

HRESULT WINAPI D3D10RegisterLayers(void)
{
    typedef HRESULT (WINAPI *PFN)(void);
    PFN pfn = (PFN)ResolveD3D10("D3D10RegisterLayers");
    if (pfn)
        return pfn();
    return E_NOTIMPL;
}

/* For functions with complex signatures, implement explicit forwarding */

LPCSTR WINAPI D3D10GetPixelShaderProfile(void *pDevice)
{
    typedef LPCSTR (WINAPI *PFN)(void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetPixelShaderProfile");
    if (pfn) return pfn(pDevice);
    return NULL;
}

LPCSTR WINAPI D3D10GetVertexShaderProfile(void *pDevice)
{
    typedef LPCSTR (WINAPI *PFN)(void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetVertexShaderProfile");
    if (pfn) return pfn(pDevice);
    return NULL;
}

LPCSTR WINAPI D3D10GetGeometryShaderProfile(void *pDevice)
{
    typedef LPCSTR (WINAPI *PFN)(void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetGeometryShaderProfile");
    if (pfn) return pfn(pDevice);
    return NULL;
}

HRESULT WINAPI D3D10CreateBlob(SIZE_T DataSize, void **ppBlob)
{
    typedef HRESULT (WINAPI *PFN)(SIZE_T, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10CreateBlob");
    if (pfn) return pfn(DataSize, ppBlob);
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CompileShader(
    const void *pSrcData, SIZE_T SrcDataSize, const char *pFileName,
    const void *pDefines, void *pInclude,
    const char *pFunctionName, const char *pProfile,
    UINT Flags, void **ppShader, void **ppErrorMsgs)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, const char *,
        const void *, void *, const char *, const char *, UINT, void **, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10CompileShader");
    if (pfn) return pfn(pSrcData, SrcDataSize, pFileName, pDefines, pInclude,
                        pFunctionName, pProfile, Flags, ppShader, ppErrorMsgs);
    FIXME("stub!\n");
    if (ppShader) *ppShader = NULL;
    if (ppErrorMsgs) *ppErrorMsgs = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10PreprocessShader(
    const void *pSrcData, SIZE_T SrcDataSize, const char *pFileName,
    const void *pDefines, void *pInclude,
    void **ppShaderText, void **ppErrorMsgs)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, const char *,
        const void *, void *, void **, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10PreprocessShader");
    if (pfn) return pfn(pSrcData, SrcDataSize, pFileName, pDefines, pInclude,
                        ppShaderText, ppErrorMsgs);
    FIXME("stub!\n");
    if (ppShaderText) *ppShaderText = NULL;
    if (ppErrorMsgs) *ppErrorMsgs = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CompileEffectFromMemory(
    void *pData, SIZE_T DataLength, const char *pSrcFileName,
    const void *pDefines, void *pInclude,
    UINT HLSLFlags, UINT FXFlags, void **ppCompiledEffect, void **ppErrors)
{
    typedef HRESULT (WINAPI *PFN)(void *, SIZE_T, const char *,
        const void *, void *, UINT, UINT, void **, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10CompileEffectFromMemory");
    if (pfn) return pfn(pData, DataLength, pSrcFileName, pDefines, pInclude,
                        HLSLFlags, FXFlags, ppCompiledEffect, ppErrors);
    FIXME("stub!\n");
    if (ppCompiledEffect) *ppCompiledEffect = NULL;
    if (ppErrors) *ppErrors = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateEffectFromMemory(
    void *pData, SIZE_T DataLength, UINT FXFlags,
    void *pDevice, void *pEffectPool, void **ppEffect)
{
    typedef HRESULT (WINAPI *PFN)(void *, SIZE_T, UINT, void *, void *, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10CreateEffectFromMemory");
    if (pfn) return pfn(pData, DataLength, FXFlags, pDevice, pEffectPool, ppEffect);
    FIXME("stub!\n");
    if (ppEffect) *ppEffect = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateEffectPoolFromMemory(
    void *pData, SIZE_T DataLength, UINT FXFlags,
    void *pDevice, void **ppEffectPool)
{
    typedef HRESULT (WINAPI *PFN)(void *, SIZE_T, UINT, void *, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10CreateEffectPoolFromMemory");
    if (pfn) return pfn(pData, DataLength, FXFlags, pDevice, ppEffectPool);
    FIXME("stub!\n");
    if (ppEffectPool) *ppEffectPool = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateStateBlock(void *pDevice, void *pMask, void **ppStateBlock)
{
    typedef HRESULT (WINAPI *PFN)(void *, void *, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10CreateStateBlock");
    if (pfn) return pfn(pDevice, pMask, ppStateBlock);
    FIXME("stub!\n");
    if (ppStateBlock) *ppStateBlock = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10GetShaderDebugInfo(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetShaderDebugInfo");
    if (pfn) return pfn(pData, DataSize, ppBlob);
    FIXME("stub!\n");
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10GetInputSignatureBlob(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetInputSignatureBlob");
    if (pfn) return pfn(pData, DataSize, ppBlob);
    FIXME("stub!\n");
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10GetOutputSignatureBlob(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetOutputSignatureBlob");
    if (pfn) return pfn(pData, DataSize, ppBlob);
    FIXME("stub!\n");
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10GetInputAndOutputSignatureBlob(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10GetInputAndOutputSignatureBlob");
    if (pfn) return pfn(pData, DataSize, ppBlob);
    FIXME("stub!\n");
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10ReflectShader(const void *pData, SIZE_T DataSize, void **ppReflector)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10ReflectShader");
    if (pfn) return pfn(pData, DataSize, ppReflector);
    FIXME("stub!\n");
    if (ppReflector) *ppReflector = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10DisassembleShader(const void *pData, SIZE_T DataSize, BOOL EnableColorCode,
                                       const char *pComments, void **ppDisassembly)
{
    typedef HRESULT (WINAPI *PFN)(const void *, SIZE_T, BOOL, const char *, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10DisassembleShader");
    if (pfn) return pfn(pData, DataSize, EnableColorCode, pComments, ppDisassembly);
    FIXME("stub!\n");
    if (ppDisassembly) *ppDisassembly = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10DisassembleEffect(void *pEffect, BOOL EnableColorCode, void **ppDisassembly)
{
    typedef HRESULT (WINAPI *PFN)(void *, BOOL, void **);
    PFN pfn = (PFN)ResolveD3D10("D3D10DisassembleEffect");
    if (pfn) return pfn(pEffect, EnableColorCode, ppDisassembly);
    FIXME("stub!\n");
    if (ppDisassembly) *ppDisassembly = NULL;
    return E_NOTIMPL;
}

/* State block mask -- forward to d3d10.dll */
HRESULT WINAPI D3D10StateBlockMaskUnion(const void *pA, const void *pB, void *pResult)
{
    typedef HRESULT (WINAPI *PFN)(const void *, const void *, void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskUnion");
    if (pfn) return pfn(pA, pB, pResult);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskIntersect(const void *pA, const void *pB, void *pResult)
{
    typedef HRESULT (WINAPI *PFN)(const void *, const void *, void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskIntersect");
    if (pfn) return pfn(pA, pB, pResult);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskDifference(const void *pA, const void *pB, void *pResult)
{
    typedef HRESULT (WINAPI *PFN)(const void *, const void *, void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskDifference");
    if (pfn) return pfn(pA, pB, pResult);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskEnableAll(void *pMask)
{
    typedef HRESULT (WINAPI *PFN)(void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskEnableAll");
    if (pfn) return pfn(pMask);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskDisableAll(void *pMask)
{
    typedef HRESULT (WINAPI *PFN)(void *);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskDisableAll");
    if (pfn) return pfn(pMask);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskEnableCapture(void *pMask, UINT StateType, UINT Start, UINT Count)
{
    typedef HRESULT (WINAPI *PFN)(void *, UINT, UINT, UINT);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskEnableCapture");
    if (pfn) return pfn(pMask, StateType, Start, Count);
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskDisableCapture(void *pMask, UINT StateType, UINT Start, UINT Count)
{
    typedef HRESULT (WINAPI *PFN)(void *, UINT, UINT, UINT);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskDisableCapture");
    if (pfn) return pfn(pMask, StateType, Start, Count);
    return E_NOTIMPL;
}

BOOL WINAPI D3D10StateBlockMaskGetSetting(const void *pMask, UINT StateType, UINT Index)
{
    typedef BOOL (WINAPI *PFN)(const void *, UINT, UINT);
    PFN pfn = (PFN)ResolveD3D10("D3D10StateBlockMaskGetSetting");
    if (pfn) return pfn(pMask, StateType, Index);
    return FALSE;
}

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
    }
    return TRUE;
}
