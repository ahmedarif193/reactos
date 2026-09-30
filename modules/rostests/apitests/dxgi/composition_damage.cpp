/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that partial flip-model presents keep undamaged pixels on screen
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <dcomp.h>

#define WIDTH 128
#define HEIGHT 96

struct DAMAGE_FRAME
{
    RECT Rect;
    DWORD Pixel;
    COLORREF Screen;
};

static const DAMAGE_FRAME Frames[] =
{
    {{0, 0, WIDTH, HEIGHT}, 0xffff0000, RGB(255, 0, 0)},
    {{16, 12, 48, 40}, 0xff00ff00, RGB(0, 255, 0)},
    {{72, 48, 112, 84}, 0xff0000ff, RGB(0, 0, 255)},
    {{80, 8, 120, 28}, 0xffffffff, RGB(255, 255, 255)},
    {{24, 56, 56, 88}, 0xffffff00, RGB(255, 255, 0)},
};

static const POINT Background[] = {{4, 4}, {64, 44}, {120, 90}, {4, 90}};

static BOOL
ScreenColorNear(HDC Screen, POINT Origin, LONG X, LONG Y, COLORREF Expected, COLORREF *Actual)
{
    COLORREF Pixel = GetPixel(Screen, Origin.x + X, Origin.y + Y);
    *Actual = Pixel;
    return Pixel != CLR_INVALID &&
           abs((int)GetRValue(Pixel) - (int)GetRValue(Expected)) <= 8 &&
           abs((int)GetGValue(Pixel) - (int)GetGValue(Expected)) <= 8 &&
           abs((int)GetBValue(Pixel) - (int)GetBValue(Expected)) <= 8;
}

static BOOL
WaitForScreenColor(HDC Screen, POINT Origin, LONG X, LONG Y, COLORREF Expected)
{
    DWORD Start = GetTickCount();
    COLORREF Actual;
    do
    {
        if (ScreenColorNear(Screen, Origin, X, Y, Expected, &Actual))
            return TRUE;
        Sleep(10);
    } while (GetTickCount() - Start < 2000);
    return FALSE;
}

static void
CheckFrame(HDC Screen, POINT Origin, UINT Last)
{
    COLORREF Actual;

    for (UINT i = 1; i <= Last; ++i)
    {
        const RECT &Rect = Frames[i].Rect;
        LONG X = (Rect.left + Rect.right) / 2, Y = (Rect.top + Rect.bottom) / 2;
        ok(ScreenColorNear(Screen, Origin, X, Y, Frames[i].Screen, &Actual),
           "Frame %u: damage of frame %u at %ld,%ld shows %06lx, expected %06lx\n",
           Last, i, X, Y, Actual, Frames[i].Screen);
    }
    for (UINT i = 0; i < ARRAYSIZE(Background); ++i)
    {
        ok(ScreenColorNear(Screen, Origin, Background[i].x, Background[i].y, Frames[0].Screen, &Actual),
           "Frame %u: undamaged pixel %ld,%ld shows %06lx, expected %06lx\n",
           Last, Background[i].x, Background[i].y, Actual, Frames[0].Screen);
    }
}

START_TEST(composition_damage)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    typedef HRESULT (WINAPI *CREATE_COMPOSITION)(IDXGIDevice *, REFIID, void **);
    HMODULE Runtime = LoadLibraryW(L"d3d11.dll"), Dcomp = LoadLibraryW(L"dcomp.dll");
    if (!Runtime || !Dcomp) { skip("D3D11/DirectComposition unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Runtime, "D3D11CreateDevice"));
    CREATE_COMPOSITION CreateComposition = reinterpret_cast<CREATE_COMPOSITION>(GetProcAddress(Dcomp, "DCompositionCreateDevice"));
    if (!Create || !CreateComposition) { skip("Creation exports unavailable\n"); return; }

    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                        NULL, 0, D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); return; }

    IDXGIDevice *Dxgi = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIFactory2 *Factory = NULL;
    Hr = Device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void **>(&Dxgi));
    ok(Hr == S_OK, "IDXGIDevice: %#lx\n", Hr);
    if (Dxgi) Hr = Dxgi->GetAdapter(&Adapter);
    if (Adapter) Hr = Adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void **>(&Factory));
    ok(Hr == S_OK && Factory, "IDXGIFactory2: %#lx\n", Hr);

    POINT Origin = {200, 200};
    HWND Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"STATIC", L"composition damage",
                                  WS_POPUP | WS_VISIBLE, Origin.x, Origin.y, WIDTH, HEIGHT,
                                  NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow: %lu\n", GetLastError());
    HDC Screen = GetDC(NULL);

    IDXGISwapChain1 *Chain = NULL;
    IDCompositionDevice *Composition = NULL;
    IDCompositionTarget *Target = NULL;
    IDCompositionVisual *Visual = NULL;
    if (Factory && Window && Screen)
    {
        UpdateWindow(Window);
        DXGI_SWAP_CHAIN_DESC1 Desc = {};
        Desc.Width = WIDTH;
        Desc.Height = HEIGHT;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 2;
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        Desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        Hr = Factory->CreateSwapChainForComposition(Device, &Desc, NULL, &Chain);
        ok(Hr == S_OK && Chain, "CreateSwapChainForComposition: %#lx\n", Hr);
        Hr = CreateComposition(Dxgi, IID_IDCompositionDevice, reinterpret_cast<void **>(&Composition));
        ok(Hr == S_OK && Composition, "DCompositionCreateDevice: %#lx\n", Hr);
        if (Composition) Hr = Composition->CreateTargetForHwnd(Window, TRUE, &Target);
        ok(Hr == S_OK && Target, "CreateTargetForHwnd: %#lx\n", Hr);
        if (Composition) Hr = Composition->CreateVisual(&Visual);
        ok(Hr == S_OK && Visual, "CreateVisual: %#lx\n", Hr);
    }
    if (Chain && Visual && Target)
    {
        Hr = Target->SetRoot(Visual);
        ok(Hr == S_OK, "SetRoot: %#lx\n", Hr);
        Hr = Visual->SetContent(Chain);
        ok(Hr == S_OK, "SetContent: %#lx\n", Hr);
        Hr = Composition->Commit();
        ok(Hr == S_OK, "Commit: %#lx\n", Hr);

        static DWORD Pixels[WIDTH * HEIGHT];
        for (UINT Frame = 0; Frame < ARRAYSIZE(Frames); ++Frame)
        {
            ID3D11Texture2D *Buffer = NULL;
            Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
            ok(Hr == S_OK && Buffer, "Frame %u GetBuffer: %#lx\n", Frame, Hr);
            if (!Buffer) break;
            RECT Damage = Frames[Frame].Rect;
            for (UINT i = 0; i < ARRAYSIZE(Pixels); ++i) Pixels[i] = Frames[Frame].Pixel;
            D3D11_BOX Box = {static_cast<UINT>(Damage.left), static_cast<UINT>(Damage.top), 0,
                            static_cast<UINT>(Damage.right), static_cast<UINT>(Damage.bottom), 1};
            Context->UpdateSubresource(Buffer, 0, &Box, Pixels, WIDTH * sizeof(DWORD), 0);
            Buffer->Release();
            DXGI_PRESENT_PARAMETERS Params = {1, &Damage, NULL, NULL};
            Hr = Chain->Present1(0, 0, &Params);
            ok(Hr == S_OK, "Frame %u Present1: %#lx\n", Frame, Hr);
            if (FAILED(Hr)) break;
            LONG X = (Damage.left + Damage.right) / 2, Y = (Damage.top + Damage.bottom) / 2;
            BOOL Shown = WaitForScreenColor(Screen, Origin, X, Y, Frames[Frame].Screen);
            ok(Shown, "Frame %u never reached the screen\n", Frame);
            if (!Shown) break;
            CheckFrame(Screen, Origin, Frame);
        }
        Visual->SetContent(NULL);
        Target->SetRoot(NULL);
        Composition->Commit();
    }

    if (Visual) Visual->Release();
    if (Target) Target->Release();
    if (Composition) Composition->Release();
    if (Chain) Chain->Release();
    if (Screen) ReleaseDC(NULL, Screen);
    if (Window) DestroyWindow(Window);
    if (Factory) Factory->Release();
    if (Adapter) Adapter->Release();
    if (Dxgi) Dxgi->Release();
    Context->ClearState();
    Context->Release();
    Device->Release();
    FreeLibrary(Dcomp);
    FreeLibrary(Runtime);
}
