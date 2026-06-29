/*
 * PROJECT:     ReactOS Direct3D 10 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     D3D10 API surface — device creation, shader compiler stubs, effects stubs
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * d3d10.dll is the public D3D10 API surface.  Device creation delegates to
 * d3d10core.dll.  The HLSL compiler and effects framework are stubbed for now.
 */

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#define COBJMACROS

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <objbase.h>

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d10);

#define D3D10_SDK_VERSION 29

/* -----------------------------------------------------------------------
 * D3D10CoreCreateDevice — resolved dynamically from d3d10core.dll
 * ----------------------------------------------------------------------- */
typedef HRESULT (WINAPI *PFN_D3D10CoreCreateDevice)(
    void *, void *, UINT, void *, UINT, UINT, void **);
typedef HRESULT (WINAPI *PFN_D3D10CoreRegisterLayers)(void);

static HMODULE hD3D10Core;
static PFN_D3D10CoreCreateDevice pfnCoreCreateDevice;
static PFN_D3D10CoreRegisterLayers pfnCoreRegisterLayers;

static BOOL LoadD3D10Core(void)
{
    if (hD3D10Core)
        return (pfnCoreCreateDevice != NULL);

    hD3D10Core = LoadLibraryW(L"d3d10core.dll");
    if (!hD3D10Core)
    {
        ERR("Failed to load d3d10core.dll\n");
        return FALSE;
    }

    pfnCoreCreateDevice = (PFN_D3D10CoreCreateDevice)
        GetProcAddress(hD3D10Core, "D3D10CoreCreateDevice");

    if (!pfnCoreCreateDevice)
    {
        ERR("d3d10core.dll lacks D3D10CoreCreateDevice\n");
        FreeLibrary(hD3D10Core);
        hD3D10Core = NULL;
        return FALSE;
    }

    pfnCoreRegisterLayers = (PFN_D3D10CoreRegisterLayers)
        GetProcAddress(hD3D10Core, "D3D10CoreRegisterLayers");

    return TRUE;
}

/* -----------------------------------------------------------------------
 * Device creation
 * ----------------------------------------------------------------------- */

/* Forward declaration */
HRESULT WINAPI D3D10CreateDeviceAndSwapChain(
    void *pAdapter, UINT DriverType, HMODULE Software, UINT Flags,
    UINT SDKVersion, void *pSwapChainDesc, void **ppSwapChain, void **ppDevice);

HRESULT WINAPI D3D10CreateDevice(
    void *pAdapter,
    UINT DriverType,
    HMODULE Software,
    UINT Flags,
    UINT SDKVersion,
    void **ppDevice)
{
    TRACE("(%p, %u, %p, %#x, %u, %p)\n",
          pAdapter, DriverType, Software, Flags, SDKVersion, ppDevice);

    return D3D10CreateDeviceAndSwapChain(pAdapter, DriverType, Software,
                                         Flags, SDKVersion, NULL, NULL, ppDevice);
}

HRESULT WINAPI D3D10CreateDeviceAndSwapChain(
    void *pAdapter,
    UINT DriverType,
    HMODULE Software,
    UINT Flags,
    UINT SDKVersion,
    void *pSwapChainDesc,
    void **ppSwapChain,
    void **ppDevice)
{
    HRESULT hr;

    TRACE("(%p, %u, %p, %#x, %u, %p, %p, %p)\n",
          pAdapter, DriverType, Software, Flags, SDKVersion,
          pSwapChainDesc, ppSwapChain, ppDevice);

    if (ppSwapChain) *ppSwapChain = NULL;
    if (ppDevice)    *ppDevice = NULL;

    if (!ppDevice)
        return E_INVALIDARG;

    if (pSwapChainDesc || ppSwapChain)
        return E_NOTIMPL;

    if (SDKVersion != D3D10_SDK_VERSION)
    {
        ERR("SDK version mismatch: app compiled with %u, runtime is %u\n",
            SDKVersion, D3D10_SDK_VERSION);
        return E_INVALIDARG;
    }

    /* Validate driver type */
    switch (DriverType)
    {
    case 0: /* D3D10_DRIVER_TYPE_HARDWARE */
    case 1: /* D3D10_DRIVER_TYPE_REFERENCE */
    case 2: /* D3D10_DRIVER_TYPE_NULL */
    case 3: /* D3D10_DRIVER_TYPE_SOFTWARE */
    case 5: /* D3D10_DRIVER_TYPE_WARP */
        break;
    default:
        ERR("Invalid driver type %u\n", DriverType);
        return E_INVALIDARG;
    }

    if (pAdapter && DriverType != 0)
        return E_INVALIDARG;

    if ((DriverType == 3 && !Software) || (DriverType != 3 && Software))
        return E_INVALIDARG;

    if (!LoadD3D10Core())
        return E_FAIL;

    hr = pfnCoreCreateDevice(NULL, pAdapter, Flags, NULL, SDKVersion, 0, ppDevice);
    if (FAILED(hr))
    {
        WARN("D3D10CoreCreateDevice failed: %#lx\n", hr);
        return hr;
    }

    return hr;
}

/* -----------------------------------------------------------------------
 * Version / Layer registration
 * ----------------------------------------------------------------------- */

UINT WINAPI D3D10GetVersion(void)
{
    TRACE("()\n");
    return D3D10_SDK_VERSION;
}

HRESULT WINAPI D3D10RegisterLayers(void)
{
    TRACE("()\n");

    if (!LoadD3D10Core())
        return E_FAIL;

    if (!pfnCoreRegisterLayers)
        return E_NOTIMPL;

    return pfnCoreRegisterLayers();
}

/* -----------------------------------------------------------------------
 * Shader profile queries — hardcoded for SM 4.0
 * ----------------------------------------------------------------------- */

LPCSTR WINAPI D3D10GetPixelShaderProfile(void *pDevice)
{
    TRACE("(%p)\n", pDevice);
    return pDevice ? "ps_4_0" : NULL;
}

LPCSTR WINAPI D3D10GetVertexShaderProfile(void *pDevice)
{
    TRACE("(%p)\n", pDevice);
    return pDevice ? "vs_4_0" : NULL;
}

LPCSTR WINAPI D3D10GetGeometryShaderProfile(void *pDevice)
{
    TRACE("(%p)\n", pDevice);
    return pDevice ? "gs_4_0" : NULL;
}

/* -----------------------------------------------------------------------
 * Blob management
 * ----------------------------------------------------------------------- */

typedef struct D3D10Blob
{
    const void *lpVtbl;
    LONG        refcount;
    SIZE_T      size;
    void       *data;
} D3D10Blob;

static ULONG STDMETHODCALLTYPE Blob_AddRef(D3D10Blob *This);

static HRESULT STDMETHODCALLTYPE Blob_QueryInterface(D3D10Blob *This, REFIID riid, void **ppv)
{
    static const GUID IID_ID3D10Blob_Local =
        {0x8ba5fb08, 0x5195, 0x40e2, {0xac,0x58,0x0d,0x98,0x9c,0x3a,0x01,0x02}};
    static const GUID IID_IUnknown_Local =
        {0x00000000, 0x0000, 0x0000, {0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

    if (!ppv)
        return E_POINTER;

    *ppv = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown_Local) ||
        IsEqualGUID(riid, &IID_ID3D10Blob_Local))
    {
        Blob_AddRef(This);
        *ppv = This;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Blob_AddRef(D3D10Blob *This)
{
    return InterlockedIncrement(&This->refcount);
}

static ULONG STDMETHODCALLTYPE Blob_Release(D3D10Blob *This)
{
    ULONG ref = InterlockedDecrement(&This->refcount);
    if (ref == 0)
    {
        if (This->data) HeapFree(GetProcessHeap(), 0, This->data);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return ref;
}

static void * STDMETHODCALLTYPE Blob_GetBufferPointer(D3D10Blob *This)
{
    return This->data;
}

static SIZE_T STDMETHODCALLTYPE Blob_GetBufferSize(D3D10Blob *This)
{
    return This->size;
}

static const void *BlobVtbl[] = {
    Blob_QueryInterface,
    Blob_AddRef,
    Blob_Release,
    Blob_GetBufferPointer,
    Blob_GetBufferSize,
};

HRESULT WINAPI D3D10CreateBlob(SIZE_T DataSize, void **ppBlob)
{
    D3D10Blob *blob;

    TRACE("(%lu, %p)\n", (ULONG)DataSize, ppBlob);

    if (!ppBlob)
        return E_INVALIDARG;

    *ppBlob = NULL;

    blob = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*blob));
    if (!blob)
        return E_OUTOFMEMORY;

    blob->lpVtbl = BlobVtbl;
    blob->refcount = 1;
    blob->size = DataSize;

    if (DataSize > 0)
    {
        blob->data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DataSize);
        if (!blob->data)
        {
            HeapFree(GetProcessHeap(), 0, blob);
            return E_OUTOFMEMORY;
        }
    }

    *ppBlob = blob;
    return S_OK;
}

/* -----------------------------------------------------------------------
 * Shader compilation stubs
 * ----------------------------------------------------------------------- */

HRESULT WINAPI D3D10CompileShader(
    const void *pSrcData, SIZE_T SrcDataSize, const char *pFileName,
    const void *pDefines, void *pInclude,
    const char *pFunctionName, const char *pProfile,
    UINT Flags, void **ppShader, void **ppErrorMsgs)
{
    TRACE("(%p, %lu, %s, %p, %p, %s, %s, %#x, %p, %p)\n",
          pSrcData, (ULONG)SrcDataSize, debugstr_a(pFileName),
          pDefines, pInclude, debugstr_a(pFunctionName),
          debugstr_a(pProfile), Flags, ppShader, ppErrorMsgs);

    if (ppShader) *ppShader = NULL;
    if (ppErrorMsgs) *ppErrorMsgs = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10PreprocessShader(
    const void *pSrcData, SIZE_T SrcDataSize, const char *pFileName,
    const void *pDefines, void *pInclude,
    void **ppShaderText, void **ppErrorMsgs)
{
    TRACE("(%p, %lu, %s, %p, %p, %p, %p)\n",
          pSrcData, (ULONG)SrcDataSize, debugstr_a(pFileName),
          pDefines, pInclude, ppShaderText, ppErrorMsgs);

    if (ppShaderText) *ppShaderText = NULL;
    if (ppErrorMsgs) *ppErrorMsgs = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CompileEffectFromMemory(
    void *pData, SIZE_T DataLength, const char *pSrcFileName,
    const void *pDefines, void *pInclude,
    UINT HLSLFlags, UINT FXFlags, void **ppCompiledEffect, void **ppErrors)
{
    TRACE("(%p, %lu, %s, %p, %p, %#x, %#x, %p, %p)\n",
          pData, (ULONG)DataLength, debugstr_a(pSrcFileName),
          pDefines, pInclude, HLSLFlags, FXFlags, ppCompiledEffect, ppErrors);

    if (ppCompiledEffect) *ppCompiledEffect = NULL;
    if (ppErrors) *ppErrors = NULL;
    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * Shader bytecode utilities stubs
 * ----------------------------------------------------------------------- */

HRESULT WINAPI D3D10GetShaderDebugInfo(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    TRACE("(%p, %lu, %p)\n", pData, (ULONG)DataSize, ppBlob);
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10GetInputSignatureBlob(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    TRACE("(%p, %lu, %p)\n", pData, (ULONG)DataSize, ppBlob);
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10GetOutputSignatureBlob(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    TRACE("(%p, %lu, %p)\n", pData, (ULONG)DataSize, ppBlob);
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10GetInputAndOutputSignatureBlob(const void *pData, SIZE_T DataSize, void **ppBlob)
{
    TRACE("(%p, %lu, %p)\n", pData, (ULONG)DataSize, ppBlob);
    if (ppBlob) *ppBlob = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10ReflectShader(const void *pData, SIZE_T DataSize, void **ppReflector)
{
    TRACE("(%p, %lu, %p)\n", pData, (ULONG)DataSize, ppReflector);
    if (ppReflector) *ppReflector = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10DisassembleShader(const void *pData, SIZE_T DataSize, BOOL EnableColorCode,
                                       const char *pComments, void **ppDisassembly)
{
    TRACE("(%p, %lu, %d, %s, %p)\n",
          pData, (ULONG)DataSize, EnableColorCode,
          debugstr_a(pComments), ppDisassembly);
    if (ppDisassembly) *ppDisassembly = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10DisassembleEffect(void *pEffect, BOOL EnableColorCode, void **ppDisassembly)
{
    TRACE("(%p, %d, %p)\n", pEffect, EnableColorCode, ppDisassembly);
    if (ppDisassembly) *ppDisassembly = NULL;
    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * Effects framework stubs
 * ----------------------------------------------------------------------- */

HRESULT WINAPI D3D10CreateEffectFromMemory(
    void *pData, SIZE_T DataLength, UINT FXFlags,
    void *pDevice, void *pEffectPool, void **ppEffect)
{
    TRACE("(%p, %lu, %#x, %p, %p, %p)\n",
          pData, (ULONG)DataLength, FXFlags, pDevice, pEffectPool, ppEffect);
    if (ppEffect) *ppEffect = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateEffectPoolFromMemory(
    void *pData, SIZE_T DataLength, UINT FXFlags,
    void *pDevice, void **ppEffectPool)
{
    TRACE("(%p, %lu, %#x, %p, %p)\n",
          pData, (ULONG)DataLength, FXFlags, pDevice, ppEffectPool);
    if (ppEffectPool) *ppEffectPool = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10CreateStateBlock(void *pDevice, void *pStateBlockMask, void **ppStateBlock)
{
    TRACE("(%p, %p, %p)\n", pDevice, pStateBlockMask, ppStateBlock);
    if (ppStateBlock) *ppStateBlock = NULL;
    return E_NOTIMPL;
}

/* -----------------------------------------------------------------------
 * State Block Mask utilities
 *
 * State block masks are 76-byte bitmasks (0x4C bytes).
 * ----------------------------------------------------------------------- */

#define STATE_BLOCK_MASK_SIZE 76

HRESULT WINAPI D3D10StateBlockMaskUnion(const void *pA, const void *pB, void *pResult)
{
    UINT i;
    const BYTE *a = pA, *b = pB;
    BYTE *r = pResult;

    if (!pA || !pB || !pResult)
        return E_INVALIDARG;

    for (i = 0; i < STATE_BLOCK_MASK_SIZE; i++)
        r[i] = a[i] | b[i];

    return S_OK;
}

HRESULT WINAPI D3D10StateBlockMaskIntersect(const void *pA, const void *pB, void *pResult)
{
    UINT i;
    const BYTE *a = pA, *b = pB;
    BYTE *r = pResult;

    if (!pA || !pB || !pResult)
        return E_INVALIDARG;

    for (i = 0; i < STATE_BLOCK_MASK_SIZE; i++)
        r[i] = a[i] & b[i];

    return S_OK;
}

HRESULT WINAPI D3D10StateBlockMaskDifference(const void *pA, const void *pB, void *pResult)
{
    UINT i;
    const BYTE *a = pA, *b = pB;
    BYTE *r = pResult;

    if (!pA || !pB || !pResult)
        return E_INVALIDARG;

    for (i = 0; i < STATE_BLOCK_MASK_SIZE; i++)
        r[i] = a[i] & ~b[i];

    return S_OK;
}

HRESULT WINAPI D3D10StateBlockMaskEnableAll(void *pMask)
{
    if (!pMask)
        return E_INVALIDARG;

    memset(pMask, 0xFF, STATE_BLOCK_MASK_SIZE);
    return S_OK;
}

HRESULT WINAPI D3D10StateBlockMaskDisableAll(void *pMask)
{
    if (!pMask)
        return E_INVALIDARG;

    memset(pMask, 0, STATE_BLOCK_MASK_SIZE);
    return S_OK;
}

HRESULT WINAPI D3D10StateBlockMaskEnableCapture(void *pMask, UINT StateType, UINT Start, UINT Count)
{
    TRACE("(%p, %u, %u, %u)\n", pMask, StateType, Start, Count);
    if (!pMask || !Count)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

HRESULT WINAPI D3D10StateBlockMaskDisableCapture(void *pMask, UINT StateType, UINT Start, UINT Count)
{
    TRACE("(%p, %u, %u, %u)\n", pMask, StateType, Start, Count);
    if (!pMask || !Count)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

BOOL WINAPI D3D10StateBlockMaskGetSetting(const void *pMask, UINT StateType, UINT Index)
{
    TRACE("(%p, %u, %u)\n", pMask, StateType, Index);
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
    case DLL_PROCESS_DETACH:
        if (hD3D10Core)
        {
            FreeLibrary(hD3D10Core);
            hD3D10Core = NULL;
            pfnCoreCreateDevice = NULL;
            pfnCoreRegisterLayers = NULL;
        }
        break;
    }
    return TRUE;
}
