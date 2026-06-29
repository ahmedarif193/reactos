/*
 * PROJECT:     ReactOS DXGI API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test IDXGIFactory / IDXGIFactory1 creation and basic operations
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "precomp.h"

static void test_CreateDXGIFactory(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIFactory1 *factory1 = NULL;
    HRESULT hr;
    ULONG ref;

    /* Test CreateDXGIFactory with IID_IDXGIFactory */
    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(factory != NULL, "factory is NULL\n");

        /* Test QueryInterface for IDXGIFactory1 */
        hr = IDXGIFactory_QueryInterface(factory, &IID_IDXGIFactory1, (void **)&factory1);
        ok(hr == S_OK, "QI for IDXGIFactory1 failed: 0x%08lx\n", hr);
        if (SUCCEEDED(hr))
        {
            IDXGIFactory1_Release(factory1);
        }

        ref = IDXGIFactory_Release(factory);
        ok(ref == 0, "Expected ref 0, got %lu\n", ref);
    }

    /* Test CreateDXGIFactory1 */
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory1);
    ok(hr == S_OK, "CreateDXGIFactory1 failed: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(factory1 != NULL, "factory1 is NULL\n");
        ref = IDXGIFactory1_Release(factory1);
        ok(ref == 0, "Expected ref 0, got %lu\n", ref);
    }

    /* Test with NULL output pointer */
    hr = CreateDXGIFactory(&IID_IDXGIFactory, NULL);
    ok(hr != S_OK, "CreateDXGIFactory with NULL should fail\n");

    /* Test with unknown IID */
    hr = CreateDXGIFactory(&IID_IUnknown, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory with IID_IUnknown should succeed: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
        IDXGIFactory_Release(factory);
}

static void test_EnumAdapters(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    HRESULT hr;
    UINT i;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    /* Enumerate adapters -- there should be at least one */
    hr = IDXGIFactory_EnumAdapters(factory, 0, &adapter);
    ok(hr == S_OK || hr == DXGI_ERROR_NOT_FOUND,
       "EnumAdapters(0) returned unexpected hr: 0x%08lx\n", hr);

    if (hr == S_OK)
    {
        DXGI_ADAPTER_DESC desc;

        ok(adapter != NULL, "adapter is NULL\n");

        /* Get adapter description */
        hr = IDXGIAdapter_GetDesc(adapter, &desc);
        ok(hr == S_OK, "GetDesc failed: 0x%08lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(desc.Description[0] != 0, "Description is empty\n");
            trace("Adapter 0: %S\n", desc.Description);
            trace("  VendorId=0x%04x DeviceId=0x%04x\n", desc.VendorId, desc.DeviceId);
            trace("  DedicatedVideoMemory=%Iu\n", desc.DedicatedVideoMemory);
            trace("  SharedSystemMemory=%Iu\n", desc.SharedSystemMemory);
        }

        IDXGIAdapter_Release(adapter);
    }

    /* Test that a high index returns DXGI_ERROR_NOT_FOUND */
    hr = IDXGIFactory_EnumAdapters(factory, 999, &adapter);
    ok(hr == DXGI_ERROR_NOT_FOUND, "EnumAdapters(999) expected NOT_FOUND, got 0x%08lx\n", hr);

    /* Enumerate all adapters until NOT_FOUND */
    for (i = 0; i < 16; i++)
    {
        hr = IDXGIFactory_EnumAdapters(factory, i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        ok(hr == S_OK, "EnumAdapters(%u) failed: 0x%08lx\n", i, hr);
        if (SUCCEEDED(hr))
            IDXGIAdapter_Release(adapter);
    }
    trace("Total adapters found: %u\n", i);

    IDXGIFactory_Release(factory);
}

static void test_MakeWindowAssociation(void)
{
    IDXGIFactory *factory = NULL;
    HWND hwnd;
    HWND hwndAssoc = NULL;
    HRESULT hr;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    hwnd = CreateWindowW(L"STATIC", L"dxgi_test", WS_OVERLAPPEDWINDOW,
                          0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ok(hwnd != NULL, "CreateWindow failed\n");

    hr = IDXGIFactory_MakeWindowAssociation(factory, hwnd, 0);
    ok(hr == S_OK, "MakeWindowAssociation failed: 0x%08lx\n", hr);

    hr = IDXGIFactory_GetWindowAssociation(factory, &hwndAssoc);
    ok(hr == S_OK, "GetWindowAssociation failed: 0x%08lx\n", hr);
    ok(hwndAssoc == hwnd, "Expected hwnd=%p, got %p\n", hwnd, hwndAssoc);

    /* Break association */
    hr = IDXGIFactory_MakeWindowAssociation(factory, NULL, 0);
    ok(hr == S_OK, "MakeWindowAssociation(NULL) failed: 0x%08lx\n", hr);

    if (hwnd)
        DestroyWindow(hwnd);

    IDXGIFactory_Release(factory);
}

static void test_RefCounting(void)
{
    IDXGIFactory *factory = NULL;
    HRESULT hr;
    ULONG ref;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    ref = IDXGIFactory_AddRef(factory);
    ok(ref == 2, "Expected ref 2, got %lu\n", ref);

    ref = IDXGIFactory_AddRef(factory);
    ok(ref == 3, "Expected ref 3, got %lu\n", ref);

    ref = IDXGIFactory_Release(factory);
    ok(ref == 2, "Expected ref 2, got %lu\n", ref);

    ref = IDXGIFactory_Release(factory);
    ok(ref == 1, "Expected ref 1, got %lu\n", ref);

    ref = IDXGIFactory_Release(factory);
    ok(ref == 0, "Expected ref 0, got %lu\n", ref);
}

START_TEST(DxgiFactory)
{
    CoInitialize(NULL);

    test_CreateDXGIFactory();
    test_EnumAdapters();
    test_MakeWindowAssociation();
    test_RefCounting();

    CoUninitialize();
}
