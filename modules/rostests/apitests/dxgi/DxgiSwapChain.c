/*
 * PROJECT:     ReactOS DXGI API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test IDXGISwapChain
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "precomp.h"

/*
 * Helper: create a simple IUnknown-like device stub so that
 * CreateSwapChain has something to bind to.  In a full test we would
 * use a real D3D device, but for interface-level testing a stub suffices.
 */
typedef struct _DummyDevice
{
    IUnknown IUnknown_iface;
    LONG RefCount;
} DummyDevice;

static inline DummyDevice *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, DummyDevice, IUnknown_iface);
}

static HRESULT STDMETHODCALLTYPE Dummy_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown))
    {
        *ppv = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Dummy_AddRef(IUnknown *iface)
{
    DummyDevice *d = impl_from_IUnknown(iface);
    return InterlockedIncrement(&d->RefCount);
}

static ULONG STDMETHODCALLTYPE Dummy_Release(IUnknown *iface)
{
    DummyDevice *d = impl_from_IUnknown(iface);
    return InterlockedDecrement(&d->RefCount);
}

static const IUnknownVtbl DummyDevice_Vtbl = {
    Dummy_QueryInterface,
    Dummy_AddRef,
    Dummy_Release,
};

static void test_CreateSwapChain(void)
{
    IDXGIFactory *factory = NULL;
    IDXGISwapChain *swapchain = NULL;
    DXGI_SWAP_CHAIN_DESC desc;
    DummyDevice device;
    HWND hwnd;
    HRESULT hr;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    /* Create a test window */
    hwnd = CreateWindowW(L"STATIC", L"dxgi_sc_test", WS_OVERLAPPEDWINDOW,
                          0, 0, 640, 480, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindow failed\n");
    if (!hwnd)
    {
        IDXGIFactory_Release(factory);
        return;
    }

    /* Set up dummy device */
    device.IUnknown_iface.lpVtbl = &DummyDevice_Vtbl;
    device.RefCount = 1;

    /* Set up swap chain description */
    memset(&desc, 0, sizeof(desc));
    desc.BufferDesc.Width = 640;
    desc.BufferDesc.Height = 480;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = IDXGIFactory_CreateSwapChain(factory, &device.IUnknown_iface, &desc, &swapchain);
    ok(hr == S_OK || hr == DXGI_ERROR_UNSUPPORTED || hr == DXGI_ERROR_INVALID_CALL,
       "CreateSwapChain returned unexpected status: 0x%08lx\n", hr);

    if (SUCCEEDED(hr) && swapchain)
    {
        DXGI_SWAP_CHAIN_DESC scDesc;
        UINT presentCount = 0;

        /* Test GetDesc */
        hr = IDXGISwapChain_GetDesc(swapchain, &scDesc);
        ok(hr == S_OK, "GetDesc failed: 0x%08lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(scDesc.BufferDesc.Width == 640, "Width=%u\n", scDesc.BufferDesc.Width);
            ok(scDesc.BufferDesc.Height == 480, "Height=%u\n", scDesc.BufferDesc.Height);
            ok(scDesc.OutputWindow == hwnd, "OutputWindow mismatch\n");
            ok(scDesc.Windowed == TRUE, "Windowed=%d\n", scDesc.Windowed);
        }

        /* Test Present */
        hr = IDXGISwapChain_Present(swapchain, 0, 0);
        ok(hr == S_OK, "Present failed: 0x%08lx\n", hr);

        /* Test Present with DXGI_PRESENT_TEST */
        hr = IDXGISwapChain_Present(swapchain, 0, DXGI_PRESENT_TEST);
        ok(hr == S_OK, "Present(TEST) failed: 0x%08lx\n", hr);

        /* Test GetLastPresentCount */
        hr = IDXGISwapChain_GetLastPresentCount(swapchain, &presentCount);
        ok(hr == S_OK, "GetLastPresentCount failed: 0x%08lx\n", hr);
        ok(presentCount >= 1, "Expected presentCount >= 1, got %u\n", presentCount);

        /* Test fullscreen state */
        {
            BOOL fullscreen = TRUE;
            IDXGIOutput *fsOutput = NULL;

            hr = IDXGISwapChain_GetFullscreenState(swapchain, &fullscreen, &fsOutput);
            ok(hr == S_OK, "GetFullscreenState failed: 0x%08lx\n", hr);
            ok(fullscreen == FALSE, "Should be windowed, got fullscreen=%d\n", fullscreen);
            ok(fsOutput == NULL, "fsOutput should be NULL in windowed mode\n");
        }

        /* Test ResizeBuffers */
        hr = IDXGISwapChain_ResizeBuffers(swapchain, 2, 800, 600,
                                           DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        ok(hr == S_OK, "ResizeBuffers failed: 0x%08lx\n", hr);

        /* Verify the desc was updated */
        hr = IDXGISwapChain_GetDesc(swapchain, &scDesc);
        ok(hr == S_OK, "GetDesc after resize failed: 0x%08lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(scDesc.BufferDesc.Width == 800, "Width=%u after resize\n", scDesc.BufferDesc.Width);
            ok(scDesc.BufferDesc.Height == 600, "Height=%u after resize\n", scDesc.BufferDesc.Height);
            ok(scDesc.BufferCount == 2, "BufferCount=%u after resize\n", scDesc.BufferCount);
        }

        IDXGISwapChain_Release(swapchain);
    }

    /* Test CreateSwapChain with NULL device */
    hr = IDXGIFactory_CreateSwapChain(factory, NULL, &desc, &swapchain);
    ok(FAILED(hr), "CreateSwapChain(NULL device) should fail\n");

    /* Test CreateSwapChain with NULL desc */
    hr = IDXGIFactory_CreateSwapChain(factory, &device.IUnknown_iface, NULL, &swapchain);
    ok(FAILED(hr), "CreateSwapChain(NULL desc) should fail\n");

    DestroyWindow(hwnd);
    IDXGIFactory_Release(factory);
}

START_TEST(DxgiSwapChain)
{
    CoInitialize(NULL);

    test_CreateSwapChain();

    CoUninitialize();
}
