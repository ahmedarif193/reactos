/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest7.c
 * PURPOSE:         ReactX Direct3D 7 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <d3d.h>

#define D3D7_CHECK(stage, expression) \
    do \
    { \
        hr = (expression); \
        if (FAILED(hr)) \
        { \
            D3DTestTraceFailure(7, stage, hr); \
            goto cleanup; \
        } \
    } while (0)

BOOL D3D7Test(GUID *lpDevice, HWND hWnd, BOOL HardwareOnly)
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

    D3D7_CHECK("create-ddraw", DirectDrawCreateEx(lpDevice, (void **)&DirectDraw, &IID_IDirectDraw7, NULL));
    D3D7_CHECK("set-cooperative-level", IDirectDraw7_SetCooperativeLevel(DirectDraw, hWnd, DDSCL_NORMAL));
    D3D7_CHECK("query-d3d7", IDirectDraw7_QueryInterface(DirectDraw, &IID_IDirect3D7, (void **)&Direct3D));

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    SurfaceDesc.dwFlags = DDSD_CAPS;
    SurfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    D3D7_CHECK("create-primary", IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Primary, NULL));

    D3D7_CHECK("create-clipper", IDirectDraw7_CreateClipper(DirectDraw, 0, &Clipper, NULL));
    D3D7_CHECK("set-clipper-window", IDirectDrawClipper_SetHWnd(Clipper, 0, hWnd));
    D3D7_CHECK("set-primary-clipper", IDirectDrawSurface7_SetClipper(Primary, Clipper));

    GetClientRect(hWnd, &ClientRect);
    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    SurfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    Width = ClientRect.right - ClientRect.left;
    Height = ClientRect.bottom - ClientRect.top;
    SurfaceDesc.dwWidth = Width;
    SurfaceDesc.dwHeight = Height;
    SurfaceDesc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
    D3D7_CHECK("create-target", IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Target, NULL));

    hr = IDirect3D7_CreateDevice(Direct3D, &IID_IDirect3DHALDevice, Target, &Device);
    if (FAILED(hr) && !HardwareOnly)
        hr = IDirect3D7_CreateDevice(Direct3D, &IID_IDirect3DRGBDevice, Target, &Device);
    if (FAILED(hr))
    {
        D3DTestTraceFailure(7, "create-device", hr);
        goto cleanup;
    }

    ZeroMemory(&Viewport, sizeof(Viewport));
    Viewport.dwWidth = SurfaceDesc.dwWidth;
    Viewport.dwHeight = SurfaceDesc.dwHeight;
    Viewport.dvMaxZ = 1.0f;
    D3D7_CHECK("set-viewport", IDirect3DDevice7_SetViewport(Device, &Viewport));
    D3D7_CHECK("disable-lighting", IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_LIGHTING, FALSE));
    D3D7_CHECK("disable-z", IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_ZENABLE, FALSE));
    D3D7_CHECK("disable-culling", IDirect3DDevice7_SetRenderState(Device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE));

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
    D3D7_CHECK("create-texture", IDirectDraw7_CreateSurface(DirectDraw, &SurfaceDesc, &Texture, NULL));

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));
    SurfaceDesc.dwSize = sizeof(SurfaceDesc);
    D3D7_CHECK("lock-texture", IDirectDrawSurface7_Lock(Texture, NULL, &SurfaceDesc, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL));
    if (!D3DTestLoadTexture(SurfaceDesc.lpSurface, SurfaceDesc.lPitch))
    {
        IDirectDrawSurface7_Unlock(Texture, NULL);
        D3DTestTraceFailure(7, "load-texture", E_FAIL);
        goto cleanup;
    }
    D3D7_CHECK("unlock-texture", IDirectDrawSurface7_Unlock(Texture, NULL));

    D3D7_CHECK("set-texture", IDirect3DDevice7_SetTexture(Device, 0, Texture));
    D3D7_CHECK("set-color-op", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE));
    D3D7_CHECK("set-color-arg1", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE));
    D3D7_CHECK("set-color-arg2", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE));
    D3D7_CHECK("set-address-u", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP));
    D3D7_CHECK("set-address-v", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP));
    D3D7_CHECK("set-mag-filter", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_MAGFILTER, D3DTFG_LINEAR));
    D3D7_CHECK("set-min-filter", IDirect3DDevice7_SetTextureStageState(Device, 0, D3DTSS_MINFILTER, D3DTFN_LINEAR));
    D3D7_CHECK("disable-stage-1", IDirect3DDevice7_SetTextureStageState(Device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE));

    Start = GetTickCount();
    do
    {
        D3DTestBuildCube(Vertices, GetTickCount() - Start, Width, Height);
        D3D7_CHECK("clear", IDirect3DDevice7_Clear(Device, 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0));
        D3D7_CHECK("begin-scene", IDirect3DDevice7_BeginScene(Device));
        hr = IDirect3DDevice7_DrawPrimitive(Device, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, Vertices, D3DTEST_VERTEX_COUNT, 0);
        if (FAILED(hr))
        {
            D3DTestTraceFailure(7, "draw", hr);
            IDirect3DDevice7_EndScene(Device);
            goto cleanup;
        }
        D3D7_CHECK("end-scene", IDirect3DDevice7_EndScene(Device));

        GetClientRect(hWnd, &ScreenRect);
        MapWindowPoints(hWnd, NULL, (POINT *)&ScreenRect, 2);
        D3D7_CHECK("present", IDirectDrawSurface7_Blt(Primary, &ScreenRect, Target, NULL, DDBLT_WAIT, NULL));
        if (!D3DTestPumpMessages())
        {
            D3DTestTraceFailure(7, "message-pump", E_ABORT);
            goto cleanup;
        }
        Sleep(16);
    } while (GetTickCount() - Start < D3DTEST_DURATION_MS);

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

#undef D3D7_CHECK
