/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DXGI/D3D11 temporary surface stub contract tests.
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <winuser.h>
#include <guiddef.h>

#define D3D_DRIVER_TYPE_HARDWARE 1
#define D3D11_SDK_VERSION 7
#define D3D_FEATURE_LEVEL_11_0 0x0000b000
#define D3D_FEATURE_LEVEL_10_1 0x0000a100
#define D3D_FEATURE_LEVEL_10_0 0x0000a000
#define DXGI_FORMAT_R8G8B8A8_UNORM 28
#define DXGI_USAGE_RENDER_TARGET_OUTPUT 0x20
#define DXGI_SWAP_EFFECT_DISCARD 0

typedef struct test_dxgi_rational
{
    UINT Numerator;
    UINT Denominator;
} test_dxgi_rational;

typedef struct test_dxgi_mode_desc
{
    UINT Width;
    UINT Height;
    test_dxgi_rational RefreshRate;
    UINT Format;
    UINT ScanlineOrdering;
    UINT Scaling;
} test_dxgi_mode_desc;

typedef struct test_dxgi_sample_desc
{
    UINT Count;
    UINT Quality;
} test_dxgi_sample_desc;

typedef struct test_dxgi_swap_chain_desc
{
    test_dxgi_mode_desc BufferDesc;
    test_dxgi_sample_desc SampleDesc;
    UINT BufferUsage;
    UINT BufferCount;
    HWND OutputWindow;
    BOOL Windowed;
    UINT SwapEffect;
    UINT Flags;
} test_dxgi_swap_chain_desc;

typedef HRESULT (WINAPI *PFN_CreateDXGIFactory)(REFIID riid, void **factory);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(REFIID riid, void **factory);
typedef HRESULT (WINAPI *PFN_CreateDXGIFactory2)(UINT flags, REFIID riid,
                                                 void **factory);
typedef HRESULT (WINAPI *PFN_D3D11CreateDevice)(void *adapter, UINT driver_type,
                                                HMODULE swrast, UINT flags,
                                                const void *feature_levels,
                                                UINT level_count,
                                                UINT sdk_version,
                                                void **device,
                                                UINT *obtained_feature_level,
                                                void **immediate_context);
typedef HRESULT (WINAPI *PFN_D3D11CreateDeviceAndSwapChain)(
                                                void *adapter,
                                                UINT driver_type,
                                                HMODULE swrast,
                                                UINT flags,
                                                const UINT *feature_levels,
                                                UINT level_count,
                                                UINT sdk_version,
                                                const test_dxgi_swap_chain_desc *swapchain_desc,
                                                void **swapchain,
                                                void **device,
                                                UINT *obtained_feature_level,
                                                void **immediate_context);
typedef ULONG (WINAPI *PFN_ComRelease)(void *self);
typedef HRESULT (WINAPI *PFN_IDXGISwapChain_Present)(void *self,
                                                     UINT sync_interval,
                                                     UINT flags);
typedef HRESULT (WINAPI *PFN_IDXGISwapChain_GetBuffer)(void *self,
                                                       UINT buffer,
                                                       REFIID riid,
                                                       void **surface);
typedef HRESULT (WINAPI *PFN_ID3D11Device_CreateRenderTargetView)(
                                                     void *self,
                                                     void *resource,
                                                     const void *desc,
                                                     void **render_target_view);
typedef void (WINAPI *PFN_ID3D11DeviceContext_ClearRenderTargetView)(
                                                     void *self,
                                                     void *render_target_view,
                                                     const float color[4]);

static const GUID test_iid_factory =
    { 0x7b7166ec, 0x21c7, 0x44ae,
      { 0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69 } };
static const GUID test_iid_d3d11_texture2d =
    { 0x6f15aaf2, 0xd208, 0x4e89,
      { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

static void **
com_vtbl(void *Object)
{
    if (Object == NULL)
        return NULL;

    return *(void ***)Object;
}

static void
release_object(void *Object)
{
    void **Vtbl = com_vtbl(Object);

    if (Vtbl != NULL && Vtbl[2] != NULL)
        ((PFN_ComRelease)Vtbl[2])(Object);
}

static LRESULT CALLBACK
probe_wndproc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static HWND
create_probe_window(void)
{
    static const WCHAR ClassName[] = L"ReactOSD3D11ProbeWindow";
    WNDCLASSW Class;
    HINSTANCE Instance = GetModuleHandleW(NULL);

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = probe_wndproc;
    Class.hInstance = Instance;
    Class.lpszClassName = ClassName;

    if (!RegisterClassW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    return CreateWindowExW(0, ClassName, L"D3D11 Probe", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, NULL, NULL,
                           Instance, NULL);
}

static void
expect_factory_unsupported(const char *Name, HRESULT hr, void *Factory)
{
    ok(hr == DXGI_ERROR_UNSUPPORTED,
       "%s returned 0x%08lx, expected DXGI_ERROR_UNSUPPORTED\n",
       Name, hr);
    ok(Factory == NULL, "%s left factory output %p\n", Name, Factory);
}

START_TEST(stub_surface)
{
    HMODULE Dxgi, D3d11;
    PFN_CreateDXGIFactory pCreateDXGIFactory;
    PFN_CreateDXGIFactory1 pCreateDXGIFactory1;
    PFN_CreateDXGIFactory2 pCreateDXGIFactory2;
    PFN_D3D11CreateDevice pD3D11CreateDevice;
    HRESULT hr;
    void *Object;
    void *Device;
    void *Context;
    UINT FeatureLevel;

    Dxgi = LoadLibraryW(L"dxgi.dll");
    ok(Dxgi != NULL, "LoadLibraryW(dxgi.dll) failed, error %lu\n",
       GetLastError());
    if (Dxgi == NULL)
        return;

    D3d11 = LoadLibraryW(L"d3d11.dll");
    ok(D3d11 != NULL, "LoadLibraryW(d3d11.dll) failed, error %lu\n",
       GetLastError());
    if (D3d11 == NULL)
    {
        FreeLibrary(Dxgi);
        return;
    }

    pCreateDXGIFactory = (PFN_CreateDXGIFactory)
        GetProcAddress(Dxgi, "CreateDXGIFactory");
    pCreateDXGIFactory1 = (PFN_CreateDXGIFactory1)
        GetProcAddress(Dxgi, "CreateDXGIFactory1");
    pCreateDXGIFactory2 = (PFN_CreateDXGIFactory2)
        GetProcAddress(Dxgi, "CreateDXGIFactory2");
    pD3D11CreateDevice = (PFN_D3D11CreateDevice)
        GetProcAddress(D3d11, "D3D11CreateDevice");

    ok(pCreateDXGIFactory != NULL, "CreateDXGIFactory export missing\n");
    ok(pCreateDXGIFactory1 != NULL, "CreateDXGIFactory1 export missing\n");
    ok(pCreateDXGIFactory2 != NULL, "CreateDXGIFactory2 export missing\n");
    ok(pD3D11CreateDevice != NULL, "D3D11CreateDevice export missing\n");
    if (!pCreateDXGIFactory || !pCreateDXGIFactory1 ||
        !pCreateDXGIFactory2 || !pD3D11CreateDevice)
    {
        FreeLibrary(D3d11);
        FreeLibrary(Dxgi);
        return;
    }

    Object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = pCreateDXGIFactory(&test_iid_factory, &Object);
    expect_factory_unsupported("CreateDXGIFactory", hr, Object);

    Object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = pCreateDXGIFactory1(&test_iid_factory, &Object);
    expect_factory_unsupported("CreateDXGIFactory1", hr, Object);

    Object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = pCreateDXGIFactory2(0, &test_iid_factory, &Object);
    expect_factory_unsupported("CreateDXGIFactory2", hr, Object);

    Device = (void *)(ULONG_PTR)0xdeadbeef;
    Context = (void *)(ULONG_PTR)0xfeedface;
    FeatureLevel = 0x11111111;
    hr = pD3D11CreateDevice(NULL, 1, NULL, 0, NULL, 0, 7,
                            &Device, &FeatureLevel, &Context);
    ok(hr == DXGI_ERROR_UNSUPPORTED,
       "D3D11CreateDevice returned 0x%08lx, expected DXGI_ERROR_UNSUPPORTED\n",
       hr);
    ok(Device == NULL, "D3D11CreateDevice left device output %p\n", Device);
    ok(Context == NULL, "D3D11CreateDevice left context output %p\n", Context);
    ok(FeatureLevel == 0, "D3D11CreateDevice left feature level %#x\n",
       FeatureLevel);

    FreeLibrary(D3d11);
    FreeLibrary(Dxgi);
}

START_TEST(d3d11_render_probe)
{
    static const UINT FeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    const float ClearColor[4] = { 0.10f, 0.20f, 0.40f, 1.00f };
    HMODULE D3d11;
    PFN_D3D11CreateDeviceAndSwapChain pD3D11CreateDeviceAndSwapChain;
    test_dxgi_swap_chain_desc Desc;
    HWND Window = NULL;
    void *SwapChain = NULL;
    void *Device = NULL;
    void *Context = NULL;
    void *BackBuffer = NULL;
    void *RenderTargetView = NULL;
    UINT FeatureLevel = 0;
    HRESULT hr;
    void **Vtbl;

    D3d11 = LoadLibraryW(L"d3d11.dll");
    ok(D3d11 != NULL, "LoadLibraryW(d3d11.dll) failed, error %lu\n",
       GetLastError());
    if (D3d11 == NULL)
        return;

    pD3D11CreateDeviceAndSwapChain = (PFN_D3D11CreateDeviceAndSwapChain)
        GetProcAddress(D3d11, "D3D11CreateDeviceAndSwapChain");
    ok(pD3D11CreateDeviceAndSwapChain != NULL,
       "D3D11CreateDeviceAndSwapChain export missing\n");
    if (pD3D11CreateDeviceAndSwapChain == NULL)
        goto done;

    Window = create_probe_window();
    ok(Window != NULL, "failed to create D3D11 probe window, error %lu\n",
       GetLastError());
    if (Window == NULL)
        goto done;

    ZeroMemory(&Desc, sizeof(Desc));
    Desc.BufferDesc.Width = 64;
    Desc.BufferDesc.Height = 64;
    Desc.BufferDesc.RefreshRate.Numerator = 60;
    Desc.BufferDesc.RefreshRate.Denominator = 1;
    Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 1;
    Desc.OutputWindow = Window;
    Desc.Windowed = TRUE;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = pD3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                        0, FeatureLevels,
                                        sizeof(FeatureLevels) / sizeof(FeatureLevels[0]),
                                        D3D11_SDK_VERSION, &Desc, &SwapChain,
                                        &Device, &FeatureLevel, &Context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
    {
        trace("D3D11: backend unavailable hr=0x%08lx (stub path)\n",
              (ULONG)hr);
        ok(SwapChain == NULL, "unsupported create left swapchain %p\n",
           SwapChain);
        ok(Device == NULL, "unsupported create left device %p\n", Device);
        ok(Context == NULL, "unsupported create left context %p\n", Context);
        ok(FeatureLevel == 0, "unsupported create left feature level %#x\n",
           FeatureLevel);
        skip("D3D11 render probe needs a real d3d11/dxgi backend\n");
        goto done;
    }

    ok(hr == S_OK, "D3D11CreateDeviceAndSwapChain returned 0x%08lx\n",
       (ULONG)hr);
    ok(SwapChain != NULL, "D3D11CreateDeviceAndSwapChain returned NULL swapchain\n");
    ok(Device != NULL, "D3D11CreateDeviceAndSwapChain returned NULL device\n");
    ok(Context != NULL, "D3D11CreateDeviceAndSwapChain returned NULL context\n");
    if (hr != S_OK || SwapChain == NULL || Device == NULL || Context == NULL)
        goto done;

    trace("D3D11: backend active feature_level=0x%08x swapchain=%p device=%p context=%p\n",
          FeatureLevel, SwapChain, Device, Context);

    Vtbl = com_vtbl(SwapChain);
    ok(Vtbl != NULL && Vtbl[8] != NULL && Vtbl[9] != NULL,
       "swapchain vtable is incomplete\n");
    if (Vtbl == NULL || Vtbl[8] == NULL || Vtbl[9] == NULL)
        goto done;

    hr = ((PFN_IDXGISwapChain_GetBuffer)Vtbl[9])(SwapChain, 0,
                                                 &test_iid_d3d11_texture2d,
                                                 &BackBuffer);
    ok(hr == S_OK, "IDXGISwapChain::GetBuffer returned 0x%08lx\n",
       (ULONG)hr);
    ok(BackBuffer != NULL, "IDXGISwapChain::GetBuffer returned NULL backbuffer\n");
    if (hr != S_OK || BackBuffer == NULL)
        goto done;

    Vtbl = com_vtbl(Device);
    ok(Vtbl != NULL && Vtbl[9] != NULL, "device vtable is incomplete\n");
    if (Vtbl == NULL || Vtbl[9] == NULL)
        goto done;

    hr = ((PFN_ID3D11Device_CreateRenderTargetView)Vtbl[9])(
        Device, BackBuffer, NULL, &RenderTargetView);
    ok(hr == S_OK, "ID3D11Device::CreateRenderTargetView returned 0x%08lx\n",
       (ULONG)hr);
    ok(RenderTargetView != NULL,
       "ID3D11Device::CreateRenderTargetView returned NULL view\n");
    if (hr != S_OK || RenderTargetView == NULL)
        goto done;

    Vtbl = com_vtbl(Context);
    ok(Vtbl != NULL && Vtbl[50] != NULL, "context vtable is incomplete\n");
    if (Vtbl == NULL || Vtbl[50] == NULL)
        goto done;

    ((PFN_ID3D11DeviceContext_ClearRenderTargetView)Vtbl[50])(
        Context, RenderTargetView, ClearColor);
    trace("D3D11: clear executed\n");

    Vtbl = com_vtbl(SwapChain);
    hr = ((PFN_IDXGISwapChain_Present)Vtbl[8])(SwapChain, 0, 0);
    ok(hr == S_OK, "IDXGISwapChain::Present returned 0x%08lx\n",
       (ULONG)hr);
    if (hr == S_OK)
    {
        trace("D3D11: Present backend active feature_level=0x%08x\n",
              FeatureLevel);
    }

done:
    release_object(RenderTargetView);
    release_object(BackBuffer);
    release_object(Context);
    release_object(Device);
    release_object(SwapChain);
    if (Window != NULL)
        DestroyWindow(Window);
    FreeLibrary(D3d11);
}
