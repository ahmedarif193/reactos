/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest9.c
 * PURPOSE:         ReactX Direct3D 9 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <d3d9.h>

BOOL D3D9Test(GUID *lpDevice, HWND hWnd)
{
    D3DPRESENT_PARAMETERS PresentParameters;
    D3DTEST_VERTEX Vertices[D3DTEST_VERTEX_COUNT];
    IDirect3DTexture9 *Texture = NULL;
    IDirect3DDevice9 *Device = NULL;
    IDirect3D9 *Direct3D = NULL;
    RECT ClientRect;
    DWORD Start;
    BOOL Result = FALSE;
    D3DLOCKED_RECT LockedRect;
    HRESULT hr;

    UNREFERENCED_PARAMETER(lpDevice);

    if (!(Direct3D = Direct3DCreate9(D3D_SDK_VERSION)))
        goto cleanup;

    ZeroMemory(&PresentParameters, sizeof(PresentParameters));
    GetClientRect(hWnd, &ClientRect);
    PresentParameters.BackBufferWidth = ClientRect.right - ClientRect.left;
    PresentParameters.BackBufferHeight = ClientRect.bottom - ClientRect.top;
    PresentParameters.BackBufferFormat = D3DFMT_UNKNOWN;
    PresentParameters.BackBufferCount = 1;
    PresentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    PresentParameters.hDeviceWindow = hWnd;
    PresentParameters.Windowed = TRUE;
    PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    hr = IDirect3D9_CreateDevice(Direct3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &PresentParameters, &Device);
    if (FAILED(hr))
        goto cleanup;

    IDirect3DDevice9_SetRenderState(Device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(Device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetFVF(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    if (FAILED(IDirect3DDevice9_CreateTexture(Device, D3DTEST_TEXTURE_SIZE, D3DTEST_TEXTURE_SIZE, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &Texture, NULL)))
        goto cleanup;
    if (FAILED(IDirect3DTexture9_LockRect(Texture, 0, &LockedRect, NULL, 0)))
        goto cleanup;
    if (!D3DTestLoadTexture(LockedRect.pBits, LockedRect.Pitch))
    {
        IDirect3DTexture9_UnlockRect(Texture, 0);
        goto cleanup;
    }
    if (FAILED(IDirect3DTexture9_UnlockRect(Texture, 0)))
        goto cleanup;
    if (FAILED(IDirect3DDevice9_SetTexture(Device, 0, (IDirect3DBaseTexture9 *)Texture)))
        goto cleanup;
    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(Device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(Device, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetTextureStageState(Device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    Start = GetTickCount();
    do
    {
        D3DTestBuildCube(Vertices, GetTickCount() - Start, PresentParameters.BackBufferWidth, PresentParameters.BackBufferHeight);
        if (FAILED(IDirect3DDevice9_Clear(Device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 32), 1.0f, 0)))
            goto cleanup;
        if (FAILED(IDirect3DDevice9_BeginScene(Device)))
            goto cleanup;
        hr = IDirect3DDevice9_DrawPrimitiveUP(Device, D3DPT_TRIANGLELIST, D3DTEST_VERTEX_COUNT / 3, Vertices, sizeof(Vertices[0]));
        IDirect3DDevice9_EndScene(Device);
        if (FAILED(hr) || FAILED(IDirect3DDevice9_Present(Device, NULL, NULL, NULL, NULL)))
            goto cleanup;
        Sleep(16);
    } while (GetTickCount() - Start < D3DTEST_DURATION_MS && D3DTestPumpMessages());

    Result = TRUE;

cleanup:
    if (Texture)
        IDirect3DTexture9_Release(Texture);
    if (Device)
        IDirect3DDevice9_Release(Device);
    if (Direct3D)
        IDirect3D9_Release(Direct3D);
    return Result;
}
