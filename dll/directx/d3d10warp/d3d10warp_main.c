/*
 * d3d10warp.dll - WARP (Windows Advanced Rasterization Platform) stub
 *
 * Exports the WARP adapter entry points but fails closed until a real
 * software rasterizer is wired behind them.
 */

#include <windef.h>
#include <winbase.h>
#include <d3d10umddi.h>
#include <dxgiddi.h>

/* No WARP rasterizer is implemented yet, so the exports fail closed. */

/* ------------------------------------------------------------------ */
/*  Exported entry points                                              */
/* ------------------------------------------------------------------ */

HRESULT APIENTRY OpenAdapter10(D3D10DDIARG_OPENADAPTER *pOpenData)
{
    if (!pOpenData)
        return E_INVALIDARG;

    pOpenData->hAdapter.pDrvPrivate = NULL;
    pOpenData->pAdapterFuncs = NULL;
    return E_NOTIMPL;
}

HRESULT APIENTRY OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *pOpenData)
{
    if (!pOpenData)
        return E_INVALIDARG;

    pOpenData->hAdapter.pDrvPrivate = NULL;
    pOpenData->pAdapterFuncs = NULL;
    pOpenData->pAdapterFuncs_2 = NULL;
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ */
/*  DllMain                                                            */
/* ------------------------------------------------------------------ */

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hInstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return TRUE;
}
