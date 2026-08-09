/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest7.c
 * PURPOSE:         ReactX Direct3D 7 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <d3d.h>

BOOL D3D7Test(GUID *lpDevice, HWND hWnd)
{
    D3DTEST_VERTEX Vertices[D3DTEST_VERTEX_COUNT];
    IDirect3DDevice7 *Device = NULL;
    IDirectDrawSurface7 *Primary = NULL;
    IDirectDrawSurface7 *Target = NULL;
    IDirectDrawSurface7 *Texture = NULL;
    IDirectDrawClipper *Clipper = NULL;
    IDirectDraw7 *DirectDraw = NULL;
    IDirect3D7 *Direct3D = NULL;
    DDSURFACEDESC2 SurfaceDesc;
    D3DVIEWPORT7 Viewport;
    RECT ClientRect;
    RECT ScreenRect;
    DWORD Start;
    UINT Width, Height;
    BOOL Result = FALSE;
    HRESULT hr;

    hr = DirectDrawCreateEx(lpDevice, (void **)&DirectDraw, &IID_IDirectDraw7, NULL);
    if (FAILED(hr))
        goto cleanup;

    if (FAILED(IDirectDraw7_SetCooperativeLevel(DirectDraw, hWnd, DDSCL_NORMAL)))
        goto cleanup;
    if (FAILED(IDirectDraw7_QueryInterface(DirectDraw, &IID_IDirect3D7, (void **)&Direct3D)))
        goto cleanup;

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    SurfaceDesc.dwFlags = DDSD_CAPS;
    SurfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (FAILED(IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Primary, NULL)))
        goto cleanup;

    if (FAILED(IDirectDraw7_CreateClipper(DirectDraw, 0, &Clipper, NULL)))
        goto cleanup;
    if (FAILED(IDirectDrawClipper_SetHWnd(Clipper, 0, hWnd)))
        goto cleanup;
    if (FAILED(IDirectDrawSurface7_SetClipper(Primary, Clipper)))
        goto cleanup;

    GetClientRect(hWnd, &ClientRect);
    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    SurfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    Width = ClientRect.right - ClientRect.left;
    Height = ClientRect.bottom - ClientRect.top;
    SurfaceDesc.dwWidth = Width;
    SurfaceDesc.dwHeight = Height;
    SurfaceDesc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
    if (FAILED(IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Target, NULL)))
        goto cleanup;

    hr = IDirect3D7_CreateDevice(Direct3D, &IID_IDirect3DHALDevice, Target, &Device);
    if (FAILED(hr))
        hr = IDirect3D7_CreateDevice(Direct3D, &IID_IDirect3DRGBDevice, Target, &Device);
    if (FAILED(hr))
        goto cleanup;

    ZeroMemory(&Viewport, sizeof(Viewport));
    Viewport.dwWidth = SurfaceDesc.dwWidth;
    Viewport.dwHeight = SurfaceDesc.dwHeight;
    Viewport.dvMaxZ = 1.0f;
    if (FAILED(IDirect3DDevice7_SetViewport(Device, &Viewport)))
        goto cleanup;
    IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_LIGHTING, FALSE);
    IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_ZENABLE, FALSE);
    IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    SurfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    SurfaceDesc.dwWidth = D3DTEST_TEXTURE_SIZE;
    SurfaceDesc.dwHeight = D3DTEST_TEXTURE_SIZE;
    SurfaceDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
    SurfaceDesc.ddpfPixelFormat.dwSize = sizeof(SurfaceDesc.ddpfPixelFormat);
    SurfaceDesc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    SurfaceDesc.ddpfPixelFormat.dwRGBBitCount = 32;
    SurfaceDesc.ddpfPixelFormat.dwRBitMask = 0x00ff0000;
    SurfaceDesc.ddpfPixelFormat.dwGBitMask = 0x0000ff00;
    SurfaceDesc.ddpfPixelFormat.dwBBitMask = 0x000000ff;
    SurfaceDesc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000;
    if (FAILED(IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Texture, NULL)))
        goto cleanup;

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    if (FAILED(IDirectDrawSurface7_Lock(Texture, NULL, &SurfaceDesc, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL)))
        goto cleanup;
    if (!D3DTestLoadTexture(SurfaceDesc.lpSurface, SurfaceDesc.lPitch))
    {
        IDirectDrawSurface7_Unlock(Texture, NULL);
        goto cleanup;
    }
    if (FAILED(IDirectDrawSurface7_Unlock(Texture, NULL)))
        goto cleanup;

    if (FAILED(IDirect3DDevice7_SetTexture(Device, 0, Texture)))
        goto cleanup;
    IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_MAGFILTER, D3DTFG_LINEAR);
    IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_MINFILTER, D3DTFN_LINEAR);
    IDirect3DDevice7_SetTextureStageState(Device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    Start = GetTickCount();
    do
    {
        D3DTestBuildCube(Vertices, GetTickCount() - Start, Width, Height);
        if (FAILED(IDirect3DDevice7_Clear(Device, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0)))
            goto cleanup;
        if (FAILED(IDirect3DDevice7_BeginScene(Device)))
            goto cleanup;
        hr = IDirect3DDevice7_DrawPrimitive(Device, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, Vertices, D3DTEST_VERTEX_COUNT, 0);
        IDirect3DDevice7_EndScene(Device);
        if (FAILED(hr))
            goto cleanup;

        GetClientRect(hWnd, &ScreenRect);
        MapWindowPoints(hWnd, NULL, (POINT *)&ScreenRect, 2);
        if (FAILED(IDirectDrawSurface7_Blt(Primary, &ScreenRect, Target, NULL, DDBLT_WAIT, NULL)))
            goto cleanup;
        Sleep(16);
    } while (GetTickCount() - Start < D3DTEST_DURATION_MS && D3DTestPumpMessages());

    Result = TRUE;

cleanup:
    if (Texture)
        IDirectDrawSurface7_Release(Texture);
    if (Device)
        IDirect3DDevice7_Release(Device);
    if (Target)
        IDirectDrawSurface7_Release(Target);
    if (Primary)
        IDirectDrawSurface7_Release(Primary);
    if (Clipper)
        IDirectDrawClipper_Release(Clipper);
    if (Direct3D)
        IDirect3D7_Release(Direct3D);
    if (DirectDraw)
        IDirectDraw7_Release(DirectDraw);
    return Result;
}
