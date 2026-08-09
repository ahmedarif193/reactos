/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest8.c
 * PURPOSE:         ReactX Direct3D 8 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <d3d8.h>

BOOL D3D8Test(GUID *lpDevice, HWND hWnd)
{
    D3DPRESENT_PARAMETERS PresentParameters;
    D3DTEST_VERTEX Vertices[D3DTEST_VERTEX_COUNT];
    IDirect3DTexture8 *Texture = NULL;
    IDirect3DDevice8 *Device = NULL;
    IDirect3D8 *Direct3D = NULL;
    RECT ClientRect;
    DWORD Start;
    BOOL Result = FALSE;
    D3DLOCKED_RECT LockedRect;
    HRESULT hr;

    UNREFERENCED_PARAMETER(lpDevice);

    if (!(Direct3D = Direct3DCreate8(D3D_SDK_VERSION)))
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
    PresentParameters.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    hr = IDirect3D8_CreateDevice(Direct3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &PresentParameters, &Device);
    if (FAILED(hr))
        goto cleanup;

    IDirect3DDevice8_SetRenderState(Device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(Device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice8_SetRenderState(Device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetVertexShader(Device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    if (FAILED(IDirect3DDevice8_CreateTexture(Device, D3DTEST_TEXTURE_SIZE, D3DTEST_TEXTURE_SIZE, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &Texture)))
        goto cleanup;
    if (FAILED(IDirect3DTexture8_LockRect(Texture, 0, &LockedRect, NULL, 0)))
        goto cleanup;
    if (!D3DTestLoadTexture(LockedRect.pBits, LockedRect.Pitch))
    {
        IDirect3DTexture8_UnlockRect(Texture, 0);
        goto cleanup;
    }
    if (FAILED(IDirect3DTexture8_UnlockRect(Texture, 0)))
        goto cleanup;
    if (FAILED(IDirect3DDevice8_SetTexture(Device, 0, (IDirect3DBaseTexture8 *)Texture)))
        goto cleanup;
    IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(Device, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(Device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    Start = GetTickCount();
    do
    {
        D3DTestBuildCube(Vertices, GetTickCount() - Start, PresentParameters.BackBufferWidth, PresentParameters.BackBufferHeight);
        if (FAILED(IDirect3DDevice8_Clear(Device, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0)))
            goto cleanup;
        if (FAILED(IDirect3DDevice8_BeginScene(Device)))
            goto cleanup;
        hr = IDirect3DDevice8_DrawPrimitiveUP(Device, D3DPT_TRIANGLELIST, D3DTEST_VERTEX_COUNT / 3, Vertices, sizeof(Vertices[0]));
        IDirect3DDevice8_EndScene(Device);
        if (FAILED(hr) || FAILED(IDirect3DDevice8_Present(Device, NULL, NULL, NULL, NULL)))
            goto cleanup;
        Sleep(16);
    } while (GetTickCount() - Start < D3DTEST_DURATION_MS && D3DTestPumpMessages());

    Result = TRUE;

cleanup:
    if (Texture)
        IDirect3DTexture8_Release(Texture);
    if (Device)
        IDirect3DDevice8_Release(Device);
    if (Direct3D)
        IDirect3D8_Release(Direct3D);
    return Result;
}
