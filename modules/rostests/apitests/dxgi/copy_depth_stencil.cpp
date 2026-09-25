/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Depth/stencil copy destinations require feature level 10_1 in D3D10
 * and D3D11. Verify both API interfaces and separately created devices. */
#include <apitest.h>
#include <initguid.h>
#include <d3d10_1.h>
#include <d3d11.h>

static void test_copy(ID3D10Device1 *device, ID3D11DeviceContext *context,
        bool typeless, bool region, bool permitted)
{
    static const WORD pixels[] = {0x1234, 0x5678, 0x9abc, 0xffff};
    static const WORD initial[] = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd};
    D3D10_TEXTURE2D_DESC desc = {};
    D3D10_SUBRESOURCE_DATA data = {pixels, 2 * sizeof(WORD), 0};
    D3D10_MAPPED_TEXTURE2D mapped;
    ID3D10Texture2D *src = NULL, *dst = NULL, *readback = NULL;
    ID3D11Resource *src11 = NULL, *dst11 = NULL;
    HRESULT hr;

    desc.Width = desc.Height = 2;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = typeless ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R16_UNORM;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&desc, &data, &src);
    ok(hr == S_OK, "Create source: %#lx\n", hr);
    if (FAILED(hr)) goto done;

    desc.Format = typeless ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_D16_UNORM;
    desc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
    data.pSysMem = initial;
    hr = device->CreateTexture2D(&desc, &data, &dst);
    ok(hr == S_OK, "Create destination: %#lx\n", hr);
    if (FAILED(hr)) goto done;

    desc.Format = DXGI_FORMAT_R16_UNORM;
    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    hr = device->CreateTexture2D(&desc, NULL, &readback);
    ok(hr == S_OK, "Create readback: %#lx\n", hr);
    if (FAILED(hr)) goto done;

    if (context)
    {
        hr = src->QueryInterface(IID_ID3D11Resource, (void **)&src11);
        ok(hr == S_OK, "Query source D3D11 resource: %#lx\n", hr);
        if (FAILED(hr)) goto done;
        hr = dst->QueryInterface(IID_ID3D11Resource, (void **)&dst11);
        ok(hr == S_OK, "Query destination D3D11 resource: %#lx\n", hr);
        if (FAILED(hr)) goto done;
        if (region)
            context->CopySubresourceRegion(dst11, 0, 0, 0, 0, src11, 0, NULL);
        else
            context->CopyResource(dst11, src11);
    }
    else if (region)
        device->CopySubresourceRegion(dst, 0, 0, 0, 0, src, 0, NULL);
    else
        device->CopyResource(dst, src);

    device->CopyResource(readback, dst);
    hr = readback->Map(0, D3D10_MAP_READ, 0, &mapped);
    ok(hr == S_OK, "Map readback: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        for (unsigned int y = 0; y < 2; ++y)
        {
            const WORD *row = (const WORD *)((const BYTE *)mapped.pData + y * mapped.RowPitch);
            for (unsigned int x = 0; x < 2; ++x)
            {
                WORD expected = permitted ? pixels[2 * y + x] : initial[2 * y + x];
                ok(row[x] == expected, "API %u level %#x typeless %u region %u (%u,%u): %#x, expected %#x\n",
                        context ? 11 : 10, device->GetFeatureLevel(), typeless, region, x, y, row[x], expected);
            }
        }
        readback->Unmap(0);
    }

done:
    if (src11) src11->Release();
    if (dst11) dst11->Release();
    if (readback) readback->Release();
    if (dst) dst->Release();
    if (src) src->Release();
}

static void test_d3d11_copy(ID3D11Device *device, ID3D11DeviceContext *context,
        bool typeless, bool region, bool permitted)
{
    static const WORD pixels[] = {0x1234, 0x5678, 0x9abc, 0xffff};
    static const WORD initial[] = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd};
    D3D11_TEXTURE2D_DESC desc = {};
    D3D11_SUBRESOURCE_DATA data = {pixels, 2 * sizeof(WORD), 0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11Texture2D *src = NULL, *dst = NULL, *readback = NULL;
    HRESULT hr;

    desc.Width = desc.Height = 2;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = typeless ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R16_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&desc, &data, &src);
    ok(hr == S_OK, "Create D3D11 source: %#lx\n", hr);
    if (FAILED(hr)) goto done;

    desc.Format = typeless ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_D16_UNORM;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    data.pSysMem = initial;
    hr = device->CreateTexture2D(&desc, &data, &dst);
    ok(hr == S_OK, "Create D3D11 destination: %#lx\n", hr);
    if (FAILED(hr)) goto done;

    desc.Format = DXGI_FORMAT_R16_UNORM;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = device->CreateTexture2D(&desc, NULL, &readback);
    ok(hr == S_OK, "Create D3D11 readback: %#lx\n", hr);
    if (FAILED(hr)) goto done;

    if (region)
        context->CopySubresourceRegion(dst, 0, 0, 0, 0, src, 0, NULL);
    else
        context->CopyResource(dst, src);

    context->CopyResource(readback, dst);
    hr = context->Map(readback, 0, D3D11_MAP_READ, 0, &mapped);
    ok(hr == S_OK, "Map D3D11 readback: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        for (unsigned int y = 0; y < 2; ++y)
        {
            const WORD *row = (const WORD *)((const BYTE *)mapped.pData + y * mapped.RowPitch);
            for (unsigned int x = 0; x < 2; ++x)
            {
                WORD expected = permitted ? pixels[2 * y + x] : initial[2 * y + x];
                ok(row[x] == expected, "D3D11 level %#x typeless %u region %u (%u,%u): %#x, expected %#x\n",
                        device->GetFeatureLevel(), typeless, region, x, y, row[x], expected);
            }
        }
        context->Unmap(readback, 0);
    }

done:
    if (readback) readback->Release();
    if (dst) dst->Release();
    if (src) src->Release();
}

START_TEST(copy_depth_stencil)
{
    typedef HRESULT (WINAPI *create_device_fn)(IDXGIAdapter *, D3D10_DRIVER_TYPE,
            HMODULE, UINT, D3D10_FEATURE_LEVEL1, UINT, ID3D10Device1 **);
    HMODULE module = LoadLibraryW(L"d3d10_1.dll");
    if (!module) { skip("D3D10.1 unavailable\n"); return; }
    create_device_fn create_device = (create_device_fn)GetProcAddress(module, "D3D10CreateDevice1");
    if (!create_device) { skip("D3D10CreateDevice1 unavailable\n"); FreeLibrary(module); return; }

    static const D3D10_FEATURE_LEVEL1 levels[] = {D3D10_FEATURE_LEVEL_10_0, D3D10_FEATURE_LEVEL_10_1};
    for (unsigned int i = 0; i < ARRAY_SIZE(levels); ++i)
    {
        ID3D10Device1 *device = NULL;
        HRESULT hr = create_device(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                levels[i], D3D10_1_SDK_VERSION, &device);
        if (FAILED(hr))
            hr = create_device(NULL, D3D10_DRIVER_TYPE_WARP, NULL, 0, levels[i], D3D10_1_SDK_VERSION, &device);
        ok(hr == S_OK, "Create feature level %#x: %#lx\n", levels[i], hr);
        if (FAILED(hr)) continue;
        trace("Testing feature level %#x\n", device->GetFeatureLevel());

        for (unsigned int typeless = 0; typeless < 2; ++typeless)
            for (unsigned int region = 0; region < 2; ++region)
                test_copy(device, NULL, typeless, region, levels[i] >= D3D10_FEATURE_LEVEL_10_1);

        if (levels[i] == D3D10_FEATURE_LEVEL_10_0)
        {
            ID3D11Device *device11 = NULL;
            hr = device->QueryInterface(IID_ID3D11Device, (void **)&device11);
            ok(hr == S_OK, "Query D3D11 at feature level 10_0: %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                ID3D11DeviceContext *context = NULL;
                device11->GetImmediateContext(&context);
                ok(context != NULL, "No immediate context\n");
                if (context)
                {
                    for (unsigned int typeless = 0; typeless < 2; ++typeless)
                        for (unsigned int region = 0; region < 2; ++region)
                            test_copy(device, context, typeless, region, false);
                    context->Release();
                }
                device11->Release();
            }
        }
        ULONG refs = device->Release();
        ok(!refs, "Device has %lu references\n", refs);
    }
    FreeLibrary(module);

    /* Verify the feature-level boundary through separately created D3D11 devices. */
    module = LoadLibraryW(L"d3d11.dll");
    if (!module) { skip("D3D11 unavailable\n"); return; }
    typedef HRESULT (WINAPI *create_device11_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
            D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    create_device11_fn create_device11 = (create_device11_fn)GetProcAddress(module, "D3D11CreateDevice");
    if (!create_device11) { skip("D3D11CreateDevice unavailable\n"); FreeLibrary(module); return; }
    static const D3D_FEATURE_LEVEL levels11[] = {D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_10_1};
    for (unsigned int i = 0; i < ARRAY_SIZE(levels11); ++i)
    {
        const D3D_FEATURE_LEVEL level = levels11[i];
        ID3D11Device *device11 = NULL;
        ID3D11DeviceContext *context = NULL;
        HRESULT hr = create_device11(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &level, 1,
                D3D11_SDK_VERSION, &device11, NULL, &context);
        if (FAILED(hr))
            hr = create_device11(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, &level, 1,
                    D3D11_SDK_VERSION, &device11, NULL, &context);
        ok(hr == S_OK, "Create D3D11 feature level %#x: %#lx\n", level, hr);
        if (SUCCEEDED(hr))
        {
            trace("Testing separately created D3D11 device at level %#x\n", device11->GetFeatureLevel());
            for (unsigned int typeless = 0; typeless < 2; ++typeless)
                for (unsigned int region = 0; region < 2; ++region)
                    test_d3d11_copy(device11, context, typeless, region, level >= D3D_FEATURE_LEVEL_10_1);
            context->Release();
            ULONG refs = device11->Release();
            ok(!refs, "D3D11 device has %lu references\n", refs);
        }
    }
    FreeLibrary(module);
}
