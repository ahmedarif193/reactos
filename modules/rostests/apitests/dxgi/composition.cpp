/* SPDX-License-Identifier: GPL-3.0-or-later
 * Composition swap chains used by CPU-upload and GPU-rendered clients. */
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

START_TEST(composition)
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
    ok(Hr == S_OK && Adapter, "GetAdapter: %#lx\n", Hr);
    if (Adapter) Hr = Adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void **>(&Factory));
    ok(Hr == S_OK && Factory, "GetParent: %#lx\n", Hr);
    HWND Window = CreateWindowExW(0, L"static", L"DXGI composition regression", WS_POPUP | WS_VISIBLE,
                                  30, 30, 128, 96, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow: %lu\n", GetLastError());
    if (Factory && Window)
    {
        DXGI_SWAP_CHAIN_DESC1 Desc = {};
        Desc.Width = 128; Desc.Height = 96;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BufferCount = 2;
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        Desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        IDXGISwapChain1 *Chain = NULL;
        Desc.Width = 0;
        Hr = Factory->CreateSwapChainForComposition(Device, &Desc, NULL, &Chain);
        ok(Hr == DXGI_ERROR_INVALID_CALL && !Chain, "Zero width: %#lx, %p\n", Hr, Chain);
        Desc.Width = 128;
        Desc.Scaling = DXGI_SCALING_NONE;
        Hr = Factory->CreateSwapChainForComposition(Device, &Desc, NULL, &Chain);
        ok(Hr == DXGI_ERROR_INVALID_CALL && !Chain, "Invalid scaling: %#lx, %p\n", Hr, Chain);
        Desc.Scaling = DXGI_SCALING_STRETCH;
        Hr = Factory->CreateSwapChainForComposition(Device, &Desc, NULL, &Chain);
        ok(Hr == S_OK && Chain, "Zero-usage premultiplied composition: %#lx, %p\n", Hr, Chain);
        if (Chain)
        {
            DXGI_SWAP_CHAIN_DESC1 Actual = {};
            Hr = Chain->GetDesc1(&Actual);
            ok(Hr == S_OK && Actual.BufferUsage == 0 && Actual.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED,
               "Descriptor: %#lx usage=%#x alpha=%u\n", Hr, Actual.BufferUsage, Actual.AlphaMode);
            HWND InvalidWindow = NULL;
            Hr = Chain->GetHwnd(&InvalidWindow);
            ok(Hr == DXGI_ERROR_INVALID_CALL, "Composition GetHwnd: %#lx\n", Hr);
            Hr = Chain->SetFullscreenState(FALSE, NULL);
            ok(Hr == DXGI_ERROR_INVALID_CALL, "Composition SetFullscreenState: %#lx\n", Hr);
            Hr = Chain->ResizeBuffers(0, 0, 96, DXGI_FORMAT_UNKNOWN, 0);
            ok(Hr == DXGI_ERROR_INVALID_CALL, "Composition resize zero width: %#lx\n", Hr);
            ID3D11Texture2D *Initial = NULL;
            Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Initial));
            ok(Hr == S_OK && Initial, "Unbound GetBuffer: %#lx\n", Hr);
            if (Initial)
            {
                DWORD Pixels[128 * 96];
                for (UINT i = 0; i < ARRAYSIZE(Pixels); ++i) Pixels[i] = 0xff204060;
                Context->UpdateSubresource(Initial, 0, NULL, Pixels, 128 * sizeof(DWORD), 0);
                Initial->Release();
            }
            Hr = Chain->Present(0, 0);
            ok(Hr == S_OK, "Present before attaching a visual: %#lx\n", Hr);
            IDCompositionDevice *Composition = NULL;
            IDCompositionTarget *Target = NULL;
            IDCompositionVisual *Visual = NULL;
            Hr = CreateComposition(Dxgi, IID_IDCompositionDevice, reinterpret_cast<void **>(&Composition));
            ok(Hr == S_OK && Composition, "DCompositionCreateDevice: %#lx\n", Hr);
            if (Composition) Hr = Composition->CreateTargetForHwnd(Window, TRUE, &Target);
            ok(Hr == S_OK && Target, "CreateTargetForHwnd: %#lx\n", Hr);
            if (Composition) Hr = Composition->CreateVisual(&Visual);
            ok(Hr == S_OK && Visual, "CreateVisual: %#lx\n", Hr);
            if (Visual && Target)
            {
                Hr = Target->SetRoot(Visual);
                ok(Hr == S_OK, "SetRoot: %#lx\n", Hr);
                Hr = Visual->SetContent(Chain);
                ok(Hr == S_OK, "SetContent: %#lx\n", Hr);
                Hr = Composition->Commit();
                ok(Hr == S_OK, "Commit target binding: %#lx\n", Hr);
                for (UINT Frame = 0; Frame != 4; ++Frame)
                {
                    ID3D11Texture2D *Buffer = NULL;
                    Hr = Chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void **>(&Buffer));
                    ok(Hr == S_OK && Buffer, "Frame %u GetBuffer: %#lx\n", Frame, Hr);
                    if (!Buffer) break;
                    D3D11_TEXTURE2D_DESC TextureDesc = {};
                    Buffer->GetDesc(&TextureDesc);
                    ok(TextureDesc.BindFlags == 0, "Zero usage must not grant render-target binding: %#x\n", TextureDesc.BindFlags);
                    DWORD Pixels[128 * 96];
                    for (UINT i = 0; i < ARRAYSIZE(Pixels); ++i) Pixels[i] = Frame & 1 ? 0x80004000 : 0xff204060;
                    RECT Damage = Frame ? RECT{16, 12, 64, 48} : RECT{0, 0, 128, 96};
                    D3D11_BOX Box = {static_cast<UINT>(Damage.left), static_cast<UINT>(Damage.top), 0,
                                    static_cast<UINT>(Damage.right), static_cast<UINT>(Damage.bottom), 1};
                    Context->UpdateSubresource(Buffer, 0, &Box, Pixels, 128 * sizeof(DWORD), 0);
                    Buffer->Release();
                    DXGI_PRESENT_PARAMETERS Params = {1, &Damage, NULL, NULL};
                    Hr = Chain->Present1(0, 0, &Params);
                    ok(Hr == S_OK, "Frame %u full/partial Present1: %#lx\n", Frame, Hr);
                    if (FAILED(Hr)) break;
                    UINT Count = 0;
                    Hr = Chain->GetLastPresentCount(&Count);
                    ok(Hr == S_OK && Count == Frame + 2, "Present count: %#lx %u\n", Hr, Count);
                    Sleep(50);
                }
                RECT BadRect = {-1, 0, 16, 16};
                DXGI_PRESENT_PARAMETERS Bad = {1, &BadRect, NULL, NULL};
                Hr = Chain->Present1(0, 0, &Bad);
                ok(Hr == DXGI_ERROR_INVALID_CALL, "Negative damage: %#lx\n", Hr);
                Hr = Chain->ResizeBuffers(2, 128, 96, DXGI_FORMAT_UNKNOWN, 0);
                ok(Hr == S_OK, "Resize after releasing all back buffers: %#lx\n", Hr);
                Visual->SetContent(NULL);
                Target->SetRoot(NULL);
                Composition->Commit();
            }
            if (Visual) Visual->Release();
            if (Target) Target->Release();
            if (Composition) Composition->Release();
            Chain->Release();
        }
    }
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
