/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Check volume copies and generated mipmaps through the public D3D10 API.
 * Every slice must survive GPU-to-staging transfers, including padded rows. */
#include <apitest.h>
#include <d3d10_1.h>

static unsigned int mip_size(unsigned int size, unsigned int level)
{
    size >>= level;
    return size ? size : 1;
}

static BYTE pixel_byte(unsigned int level, unsigned int x, unsigned int y,
        unsigned int z, unsigned int channel, bool uniform)
{
    if (uniform)
        return channel == 3 ? 255 : 17 + 32 * level + 19 * channel;
    return (BYTE)(1 + 41 * level + 3 * x + 13 * y + 31 * z + 53 * channel);
}

static void check_copy(ID3D10Device1 *device, ID3D10Texture3D *source,
        ID3D10Texture3D *staging, const D3D10_TEXTURE3D_DESC *desc,
        unsigned int bytes_per_pixel, bool uniform, unsigned int generated_base)
{
    device->CopyResource(staging, source);
    for (unsigned int level = 0; level < desc->MipLevels; ++level)
    {
        D3D10_MAPPED_TEXTURE3D mapped = {};
        unsigned int width = mip_size(desc->Width, level);
        unsigned int height = mip_size(desc->Height, level);
        unsigned int depth = mip_size(desc->Depth, level);
        unsigned int expected_level = level;
        unsigned int bad = 0, first_x = 0, first_y = 0, first_z = 0;
        BYTE first_value = 0, first_expected = 0;
        if (uniform && level >= generated_base)
            expected_level = generated_base;

        HRESULT hr = staging->Map(level, D3D10_MAP_READ, 0, &mapped);
        ok(hr == S_OK, "Map format %#x size %ux%ux%u level %u: %#lx\n",
                desc->Format, desc->Width, desc->Height, desc->Depth, level, hr);
        if (FAILED(hr)) continue;
        bool valid_pitch = mapped.RowPitch >= width * bytes_per_pixel
                && mapped.DepthPitch >= mapped.RowPitch * height;
        ok(valid_pitch, "Invalid pitches %u/%u for %ux%ux%u, %u bytes/pixel\n",
                mapped.RowPitch, mapped.DepthPitch, width, height, depth, bytes_per_pixel);
        if (valid_pitch)
        {
            for (unsigned int z = 0; z < depth; ++z)
                for (unsigned int y = 0; y < height; ++y)
                    for (unsigned int x = 0; x < width; ++x)
                        for (unsigned int c = 0; c < bytes_per_pixel; ++c)
                        {
                            BYTE value = *((const BYTE *)mapped.pData
                                    + z * mapped.DepthPitch + y * mapped.RowPitch + x * bytes_per_pixel + c);
                            BYTE expected = pixel_byte(expected_level, x, y, z, c, uniform);
                            if (value != expected)
                            {
                                if (!bad)
                                {
                                    first_x = x; first_y = y; first_z = z;
                                    first_value = value; first_expected = expected;
                                }
                                ++bad;
                            }
                        }
            ok(!bad, "Format %#x size %ux%ux%u level %u uniform %u base %u: "
                    "%u wrong bytes, first (%u,%u,%u) %#x expected %#x\n",
                    desc->Format, desc->Width, desc->Height, desc->Depth, level,
                    uniform, generated_base, bad, first_x, first_y, first_z, first_value, first_expected);
        }
        staging->Unmap(level);
    }
}

static void test_volume(ID3D10Device1 *device, unsigned int width, unsigned int height,
        unsigned int depth, unsigned int levels, DXGI_FORMAT format, unsigned int bytes_per_pixel,
        bool generate, unsigned int base)
{
    ID3D10Texture3D *source = NULL, *staging = NULL;
    ID3D10ShaderResourceView *view = NULL;
    D3D10_TEXTURE3D_DESC desc = {};
    D3D10_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    BYTE pixels[8 * 8 * 8 * 4];
    HRESULT hr;

    desc.Width = width; desc.Height = height; desc.Depth = depth;
    desc.MipLevels = levels;
    desc.Format = format;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE | (generate ? D3D10_BIND_RENDER_TARGET : 0);
    desc.MiscFlags = generate ? D3D10_RESOURCE_MISC_GENERATE_MIPS : 0;
    hr = device->CreateTexture3D(&desc, NULL, &source);
    ok(hr == S_OK, "Create volume format %#x size %ux%ux%u: %#lx\n", format, width, height, depth, hr);
    if (FAILED(hr)) goto done;

    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    hr = device->CreateTexture3D(&desc, NULL, &staging);
    ok(hr == S_OK, "Create staging volume: %#lx\n", hr);
    if (FAILED(hr)) goto done;

    for (unsigned int level = 0; level < levels; ++level)
    {
        unsigned int w = mip_size(width, level), h = mip_size(height, level), d = mip_size(depth, level);
        for (unsigned int z = 0; z < d; ++z)
            for (unsigned int y = 0; y < h; ++y)
                for (unsigned int x = 0; x < w; ++x)
                    for (unsigned int c = 0; c < bytes_per_pixel; ++c)
                        pixels[((z * h + y) * w + x) * bytes_per_pixel + c]
                                = pixel_byte(level, x, y, z, c, generate);
        device->UpdateSubresource(source, level, NULL, pixels, w * bytes_per_pixel, w * h * bytes_per_pixel);
    }
    check_copy(device, source, staging, &desc, bytes_per_pixel, generate, levels);
    if (!generate) goto done;

    view_desc.Format = format;
    view_desc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE3D;
    view_desc.Texture3D.MostDetailedMip = base;
    view_desc.Texture3D.MipLevels = levels - base;
    hr = device->CreateShaderResourceView(source, &view_desc, &view);
    ok(hr == S_OK, "Create mipmap view base %u: %#lx\n", base, hr);
    if (FAILED(hr)) goto done;
    device->GenerateMips(view);
    check_copy(device, source, staging, &desc, bytes_per_pixel, true, base);

done:
    if (view) view->Release();
    if (staging) staging->Release();
    if (source) source->Release();
}

START_TEST(volume_readback)
{
    typedef HRESULT (WINAPI *create_device_fn)(IDXGIAdapter *, D3D10_DRIVER_TYPE,
            HMODULE, UINT, D3D10_FEATURE_LEVEL1, UINT, ID3D10Device1 **);
    static const struct { unsigned int width, height, depth, levels; } sizes[] =
    {
        {8, 8, 8, 4}, {7, 5, 3, 3}, {3, 2, 4, 3}, {8, 4, 1, 4},
    };
    HMODULE module = LoadLibraryW(L"d3d10_1.dll");
    if (!module) { skip("D3D10.1 unavailable\n"); return; }
    create_device_fn create_device = (create_device_fn)GetProcAddress(module, "D3D10CreateDevice1");
    if (!create_device) { skip("D3D10CreateDevice1 unavailable\n"); FreeLibrary(module); return; }

    ID3D10Device1 *device = NULL;
    HRESULT hr = create_device(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, &device);
    if (FAILED(hr))
    {
        trace("Hardware device unavailable (%#lx), using WARP\n", hr);
        hr = create_device(NULL, D3D10_DRIVER_TYPE_WARP, NULL, 0,
                D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, &device);
    }
    ok(hr == S_OK, "Create D3D10.1 device: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        for (unsigned int i = 0; i < ARRAY_SIZE(sizes); ++i)
        {
            test_volume(device, sizes[i].width, sizes[i].height, sizes[i].depth,
                    sizes[i].levels, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, 0);
            test_volume(device, sizes[i].width, sizes[i].height, sizes[i].depth,
                    sizes[i].levels, DXGI_FORMAT_R8_UNORM, 1, false, 0);
        }
        for (unsigned int base = 0; base < 2; ++base)
            test_volume(device, 8, 8, 8, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 4, true, base);
        ULONG refs = device->Release();
        ok(!refs, "Device has %lu references\n", refs);
    }
    FreeLibrary(module);
}
