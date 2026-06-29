/*
 * PROJECT:     ReactOS DXGI API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test IDXGIAdapter / IDXGIAdapter1
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "precomp.h"

static void test_GetDesc(void)
{
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    DXGI_ADAPTER_DESC desc;
    DXGI_ADAPTER_DESC1 desc1;
    HRESULT hr;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory1 failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IDXGIFactory1_EnumAdapters1(factory, 0, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
    {
        skip("No adapters found\n");
        IDXGIFactory1_Release(factory);
        return;
    }
    ok(hr == S_OK, "EnumAdapters1(0) failed: 0x%08lx\n", hr);

    /* Test GetDesc (IDXGIAdapter method) */
    hr = IDXGIAdapter1_GetDesc((IDXGIAdapter *)adapter, &desc);
    ok(hr == S_OK, "GetDesc failed: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(desc.Description[0] != 0, "Description is empty\n");
        trace("  Description: %S\n", desc.Description);
    }

    /* Test GetDesc with NULL */
    hr = IDXGIAdapter1_GetDesc((IDXGIAdapter *)adapter, NULL);
    ok(FAILED(hr), "GetDesc(NULL) should fail\n");

    /* Test GetDesc1 (IDXGIAdapter1 method) */
    hr = IDXGIAdapter1_GetDesc1(adapter, &desc1);
    ok(hr == S_OK, "GetDesc1 failed: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(desc1.Description[0] != 0, "Description1 is empty\n");
        /* The descriptions should match */
        ok(wcscmp(desc.Description, desc1.Description) == 0,
           "Descriptions differ: '%S' vs '%S'\n", desc.Description, desc1.Description);
    }

    IDXGIAdapter1_Release(adapter);
    IDXGIFactory1_Release(factory);
}

static void test_EnumOutputs(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIOutput *output = NULL;
    HRESULT hr;
    UINT i;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IDXGIFactory_EnumAdapters(factory, 0, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
    {
        skip("No adapters found\n");
        IDXGIFactory_Release(factory);
        return;
    }
    ok(hr == S_OK, "EnumAdapters(0) failed: 0x%08lx\n", hr);

    /* Enumerate outputs */
    for (i = 0; i < 16; i++)
    {
        hr = IDXGIAdapter_EnumOutputs(adapter, i, &output);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;

        ok(hr == S_OK, "EnumOutputs(%u) failed: 0x%08lx\n", i, hr);
        if (SUCCEEDED(hr))
        {
            DXGI_OUTPUT_DESC oDesc;
            hr = IDXGIOutput_GetDesc(output, &oDesc);
            ok(hr == S_OK, "Output GetDesc failed: 0x%08lx\n", hr);
            if (SUCCEEDED(hr))
            {
                trace("  Output %u: %S (%ld,%ld)-(%ld,%ld) attached=%d\n",
                      i, oDesc.DeviceName,
                      oDesc.DesktopCoordinates.left, oDesc.DesktopCoordinates.top,
                      oDesc.DesktopCoordinates.right, oDesc.DesktopCoordinates.bottom,
                      oDesc.AttachedToDesktop);
            }
            IDXGIOutput_Release(output);
        }
    }
    trace("Total outputs: %u\n", i);

    IDXGIAdapter_Release(adapter);
    IDXGIFactory_Release(factory);
}

static void test_CheckInterfaceSupport(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    LARGE_INTEGER umdVersion;
    HRESULT hr;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IDXGIFactory_EnumAdapters(factory, 0, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
    {
        skip("No adapters found\n");
        IDXGIFactory_Release(factory);
        return;
    }
    ok(hr == S_OK, "EnumAdapters(0) failed: 0x%08lx\n", hr);

    /*
     * CheckInterfaceSupport should return DXGI_ERROR_UNSUPPORTED
     * since we don't have a D3D10/D3D11 device.
     */
    hr = IDXGIAdapter_CheckInterfaceSupport(adapter, &IID_IUnknown, &umdVersion);
    ok(hr == DXGI_ERROR_UNSUPPORTED,
       "CheckInterfaceSupport expected UNSUPPORTED, got 0x%08lx\n", hr);

    IDXGIAdapter_Release(adapter);
    IDXGIFactory_Release(factory);
}

static void test_GetParent(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory *parent = NULL;
    HRESULT hr;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory failed: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IDXGIFactory_EnumAdapters(factory, 0, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
    {
        skip("No adapters found\n");
        IDXGIFactory_Release(factory);
        return;
    }
    ok(hr == S_OK, "EnumAdapters(0) failed: 0x%08lx\n", hr);

    /* Adapter's parent should be the factory */
    hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory, (void **)&parent);
    ok(hr == S_OK, "GetParent failed: 0x%08lx\n", hr);
    ok(parent != NULL, "parent is NULL\n");
    if (parent)
        IDXGIFactory_Release(parent);

    IDXGIAdapter_Release(adapter);
    IDXGIFactory_Release(factory);
}

START_TEST(DxgiAdapter)
{
    CoInitialize(NULL);

    test_GetDesc();
    test_EnumOutputs();
    test_CheckInterfaceSupport();
    test_GetParent();

    CoUninitialize();
}
