/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise cube faces and volume slices through the native D3D11 runtime. */
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <d3dcompiler.h>

static void TestSample(ID3D11Device *device, ID3D11DeviceContext *context,
        ID3D11ShaderResourceView *view, const char *source, UINT expected,
        UINT samples = 1, UINT sample_mask = ~0u, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, UINT width = 1, UINT height = 1,
        DXGI_FORMAT vertex_format = DXGI_FORMAT_UNKNOWN, UINT vertex_mode = 0,
        ID3D11Texture2D **output = NULL)
{
    if (output) *output = NULL;
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    typedef HRESULT (WINAPI *COMPILE)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
            ID3DInclude *, const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);
    COMPILE compile = compiler ? reinterpret_cast<COMPILE>(GetProcAddress(compiler, "D3DCompile")) : NULL;
    if (!compile) { skip("Shader compiler unavailable\n"); return; }
    const char *vs_source = "float4 main(uint id : SV_VertexID) : SV_Position {"
            "return float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0, 0.0, 1.0);}";
    if (vertex_format != DXGI_FORMAT_UNKNOWN)
        vs_source = "struct O {float4 p : SV_Position; float4 c : COLOR;};"
                "O main(uint id : SV_VertexID, float4 color : COLOR) { O o;"
                "o.p=float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0,0,1); o.c=color; return o;}";
    if (vertex_mode == 1)
        vs_source = "struct O {float4 p : SV_Position; float4 c : COLOR;};"
                "O main(uint id : SV_VertexID, uint instance : SV_InstanceID, float4 color : COLOR) { O o;"
                "o.p=float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0,0,1);"
                "o.c=instance == 0 ? color : float4(1,0,0,1); return o;}";
    if (vertex_mode == 2)
        vs_source = "struct O {float4 p : SV_Position; float4 extra : TEXCOORD; float4 c : COLOR;};"
                "O main(float2 position : POSITION, float4 color : COLOR) { O o;"
                "o.p=float4(position,0,1); o.extra=o.p; o.c=color; return o;}";
    ID3DBlob *vs_code = NULL, *ps_code = NULL, *errors = NULL;
    HRESULT hr = compile(vs_source, strlen(vs_source), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vs_code, &errors);
    ok(hr == S_OK, "Compile texture vertex shader: %#lx %s\n", hr, errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
    if (errors) { errors->Release(); errors = NULL; }
    hr = compile(source, strlen(source), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &ps_code, &errors);
    ok(hr == S_OK, "Compile texture pixel shader: %#lx %s\n", hr, errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
    if (errors) errors->Release();
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11Texture2D *target = NULL, *staging = NULL, *resolved = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11SamplerState *sampler = NULL;
    ID3D11Buffer *vertices = NULL, *positions = NULL;
    ID3D11InputLayout *layout = NULL;
    UINT vertex_stride = 0;
    if (vs_code && ps_code)
    {
        hr = device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), NULL, &vs);
        ok(hr == S_OK, "Create texture vertex shader: %#lx\n", hr);
        hr = device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), NULL, &ps);
        ok(hr == S_OK, "Create texture pixel shader: %#lx\n", hr);
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
        desc.SampleDesc.Count = samples;
        desc.Format = format;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (output) desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        hr = device->CreateTexture2D(&desc, NULL, &target);
        ok(hr == S_OK, "Texture sample output: %#lx\n", hr);
        if (target) hr = device->CreateRenderTargetView(target, NULL, &rtv);
        ok(hr == S_OK && rtv, "Texture sample output view: %#lx\n", hr);
        desc.BindFlags = 0;
        desc.SampleDesc.Count = 1;
        if (samples > 1)
        {
            hr = device->CreateTexture2D(&desc, NULL, &resolved);
            ok(hr == S_OK && resolved, "Resolve destination: %#lx\n", hr);
        }
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateTexture2D(&desc, NULL, &staging);
        ok(hr == S_OK, "Texture sample readback: %#lx\n", hr);
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&sd, &sampler);
        ok(hr == S_OK, "Texture sample sampler: %#lx\n", hr);
        if (vertex_format != DXGI_FORMAT_UNKNOWN)
        {
            const UINT unorm[] = {0xff00ff00, 0xff00ff00, 0xff00ff00};
            const FLOAT floats[] = {0,1,0,1, 0,1,0,1, 0,1,0,1};
            const USHORT halves[] = {0,0x3c00,0,0x3c00, 0,0x3c00,0,0x3c00, 0,0x3c00,0,0x3c00};
            vertex_stride = vertex_format == DXGI_FORMAT_R8G8B8A8_UNORM ? 4
                    : vertex_format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 16;
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = 3 * vertex_stride; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data = {};
            data.pSysMem = vertex_stride == 4 ? static_cast<const void *>(unorm)
                    : vertex_stride == 8 ? static_cast<const void *>(halves) : static_cast<const void *>(floats);
            hr = device->CreateBuffer(&bd, &data, &vertices);
            ok(hr == S_OK, "Color vertex buffer: %#lx\n", hr);
            D3D11_INPUT_ELEMENT_DESC elements[] = {
                {"COLOR", 0, vertex_format, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            hr = device->CreateInputLayout(elements, vertex_mode == 2 ? 2 : 1,
                    vs_code->GetBufferPointer(), vs_code->GetBufferSize(), &layout);
            ok(hr == S_OK, "Color input layout format %u: %#lx\n", vertex_format, hr);
            if (vertex_mode == 2)
            {
                const FLOAT xy[] = {-1,1, 3,1, -1,-3};
                bd.ByteWidth = sizeof(xy);
                data.pSysMem = xy;
                hr = device->CreateBuffer(&bd, &data, &positions);
                ok(hr == S_OK, "Position vertex buffer: %#lx\n", hr);
            }
        }
        if (vs && ps && rtv && staging && sampler && (samples == 1 || resolved))
        {
            context->ClearState();
            if (vertices && layout)
            {
                UINT offset = 0;
                context->IASetInputLayout(layout);
                context->IASetVertexBuffers(0, 1, &vertices, &vertex_stride, &offset);
                if (positions)
                {
                    UINT position_stride = 2 * sizeof(FLOAT);
                    context->IASetVertexBuffers(1, 1, &positions, &position_stride, &offset);
                }
            }
            const FLOAT black[4] = {0, 0, 0, 0};
            context->ClearRenderTargetView(rtv, black);
            context->OMSetRenderTargets(1, &rtv, NULL);
            context->OMSetBlendState(NULL, NULL, sample_mask);
            context->VSSetShader(vs, NULL, 0);
            context->PSSetShader(ps, NULL, 0);
            context->PSSetShaderResources(0, 1, &view);
            context->PSSetSamplers(0, 1, &sampler);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            D3D11_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(width), static_cast<FLOAT>(height), 0, 1};
            context->RSSetViewports(1, &viewport);
            if (vertex_mode == 1) context->DrawInstanced(3, 1, 0, 0);
            else context->Draw(3, 0);
            context->ClearState();
            if (resolved) context->ResolveSubresource(resolved, 0, target, 0, format);
            context->CopyResource(staging, resolved ? resolved : target);
            D3D11_MAPPED_SUBRESOURCE map = {};
            hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &map);
            ok(hr == S_OK, "Map sampled pixel: %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                UINT actual = *static_cast<UINT *>(map.pData);
                bool matches = actual == expected;
                if (samples > 1)
                {
                    // UNORM resolve rounding can differ by one least-significant bit.
                    matches = true;
                    for (UINT channel = 0; channel < 4; ++channel)
                    {
                        int delta = int((actual >> (channel * 8)) & 255) - int((expected >> (channel * 8)) & 255);
                        if (delta < -1 || delta > 1) matches = false;
                    }
                }
                ok(matches, "Sampled texture pixel %#x, expected %#x (resolve tolerance %u)\n", actual, expected, samples > 1 ? 1 : 0);
                if (width > 1 || height > 1)
                {
                    UINT incorrect = 0, first_x = 0, first_y = 0, first_value = 0;
                    for (UINT y = 0; y < height; ++y)
                        for (UINT x = 0; x < width; ++x)
                        {
                            UINT pixel = reinterpret_cast<const UINT *>(static_cast<const BYTE *>(map.pData) + y * map.RowPitch)[x];
                            if (pixel != expected)
                            {
                                if (!incorrect) { first_x = x; first_y = y; first_value = pixel; }
                                ++incorrect;
                            }
                        }
                    ok(!incorrect, "Raster coverage: %u bad pixels; first (%u,%u)=%#x expected %#x\n",
                       incorrect, first_x, first_y, first_value, expected);
                }
                context->Unmap(staging, 0);
            }
        }
    }
    if (positions) positions->Release();
    if (vertices) vertices->Release();
    if (layout) layout->Release();
    if (sampler) sampler->Release();
    if (rtv) rtv->Release();
    if (target && output) { *output = target; target = NULL; }
    if (target) target->Release();
    if (resolved) resolved->Release();
    if (staging) staging->Release();
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (vs_code) vs_code->Release();
    if (ps_code) ps_code->Release();
    FreeLibrary(compiler);
}

static void TestCube(ID3D11Device *device, ID3D11DeviceContext *context, DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = 4;
    desc.MipLevels = 1;
    desc.ArraySize = 12;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    UINT pixels[12][16];
    D3D11_SUBRESOURCE_DATA initial[12] = {};
    for (UINT face = 0; face < 12; ++face)
    {
        for (UINT i = 0; i < 16; ++i) pixels[face][i] = 0xff000000 | (face + 1);
        initial[face].pSysMem = pixels[face];
        initial[face].SysMemPitch = 16;
    }
    ID3D11Texture2D *cube = NULL, *staging = NULL;
    desc.ArraySize = 7;
    HRESULT hr = device->CreateTexture2D(&desc, NULL, &cube);
    ok(hr == E_INVALIDARG && !cube, "Incomplete cube: %#lx %p\n", hr, cube);
    desc.ArraySize = 12;
    desc.Height = 2;
    hr = device->CreateTexture2D(&desc, NULL, &cube);
    ok(hr == E_INVALIDARG && !cube, "Nonsquare cube: %#lx %p\n", hr, cube);
    desc.Height = 4;
    hr = device->CreateTexture2D(&desc, initial, &cube);
    ok(hr == S_OK && cube, "Create cube array: %#lx %p\n", hr, cube);
    if (!cube) return;
    ID3D11ShaderResourceView *srv = NULL;
    hr = device->CreateShaderResourceView(cube, NULL, &srv);
    ok(hr == S_OK && srv, "Default cube array view: %#lx\n", hr);
    if (srv)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC actual;
        srv->GetDesc(&actual);
        ok(actual.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY
                && actual.TextureCubeArray.NumCubes == 2, "Default cube view: %u, cubes %u\n",
                actual.ViewDimension, actual.TextureCubeArray.NumCubes);
        srv->Release(); srv = NULL;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
    view.Format = desc.Format;
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    view.TextureCubeArray.First2DArrayFace = 6;
    view.TextureCubeArray.NumCubes = 1;
    view.TextureCubeArray.MipLevels = ~0u;
    hr = device->CreateShaderResourceView(cube, &view, &srv);
    ok(hr == S_OK && srv, "Second cube view: %#lx\n", hr);
    if (srv) { srv->Release(); srv = NULL; }
    view.TextureCubeArray.NumCubes = 2;
    hr = device->CreateShaderResourceView(cube, &view, &srv);
    ok(hr == E_INVALIDARG && !srv, "Cube view outside array: %#lx\n", hr);
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    view.TextureCube.MipLevels = 1;
    hr = device->CreateShaderResourceView(cube, &view, &srv);
    ok(hr == S_OK && srv, "Single cube view: %#lx\n", hr);
    if (srv)
    {
        TestSample(device, context, srv, "TextureCube t : register(t0); SamplerState s : register(s0);"
                "float4 main(float4 p : SV_Position) : SV_Target { return t.SampleLevel(s, float3(1,0,0), 0); }", format == DXGI_FORMAT_B8G8R8A8_UNORM ? 0xff010000 : pixels[0][0]);
        srv->Release();
    }
    D3D11_RENDER_TARGET_VIEW_DESC rt = {};
    rt.Format = desc.Format;
    rt.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    rt.Texture2DArray.FirstArraySlice = 8;
    rt.Texture2DArray.ArraySize = 1;
    ID3D11RenderTargetView *rtv = NULL;
    hr = device->CreateRenderTargetView(cube, &rt, &rtv);
    ok(hr == S_OK && rtv, "Cube face render target: %#lx\n", hr);
    if (rtv)
    {
        const FLOAT red[4] = {1, 0, 0, 1};
        context->ClearRenderTargetView(rtv, red);
        for (UINT i = 0; i < 16; ++i) pixels[8][i] = format == DXGI_FORMAT_B8G8R8A8_UNORM ? 0xffff0000 : 0xff0000ff;
        rtv->Release();
    }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = device->CreateTexture2D(&desc, NULL, &staging);
    ok(hr == S_OK && staging, "Cube readback array: %#lx\n", hr);
    if (staging)
    {
        // Copy faces individually: a staging array has no cube misc flag.
        for (UINT face = 0; face < 12; ++face)
        {
            context->CopySubresourceRegion(staging, face, 0, 0, 0, cube, face, NULL);
            D3D11_MAPPED_SUBRESOURCE map = {};
            hr = context->Map(staging, face, D3D11_MAP_READ, 0, &map);
            ok(hr == S_OK, "Map cube face %u: %#lx\n", face, hr);
            if (SUCCEEDED(hr))
            {
                UINT mismatches = 0;
                for (UINT y = 0; y < 4; ++y)
                    for (UINT x = 0; x < 4; ++x)
                        mismatches += reinterpret_cast<UINT *>(static_cast<BYTE *>(map.pData) + y * map.RowPitch)[x] != pixels[face][y * 4 + x];
                ok(!mismatches, "Cube face %u: %u mismatched pixels\n", face, mismatches);
                context->Unmap(staging, face);
            }
        }
        staging->Release();
    }
    cube->Release();
}

static void TestVolume(ID3D11Device *device, ID3D11DeviceContext *context, DXGI_FORMAT format)
{
    D3D11_TEXTURE3D_DESC desc = {};
    desc.Width = desc.Height = desc.Depth = 4;
    desc.MipLevels = 0;
    desc.Format = format;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    UINT pixels[3][64] = {};
    D3D11_SUBRESOURCE_DATA initial[3] = {};
    for (UINT mip = 0; mip < 3; ++mip)
    {
        UINT side = 4 >> mip;
        for (UINT i = 0; i < side * side * side; ++i) pixels[mip][i] = 0xff000000 | (mip << 16) | i;
        initial[mip].pSysMem = pixels[mip];
        initial[mip].SysMemPitch = side * 4;
        initial[mip].SysMemSlicePitch = side * side * 4;
    }
    ID3D11Texture3D *volume = NULL, *staging = NULL;
    desc.Depth = 0;
    HRESULT hr = device->CreateTexture3D(&desc, NULL, &volume);
    ok(hr == E_INVALIDARG && !volume, "Zero depth: %#lx\n", hr);
    desc.Depth = 4;
    hr = device->CreateTexture3D(&desc, initial, &volume);
    ok(hr == S_OK && volume, "Create volume: %#lx %p\n", hr, volume);
    if (!volume) return;
    D3D11_TEXTURE3D_DESC actual;
    volume->GetDesc(&actual);
    ok(actual.MipLevels == 3 && actual.Depth == 4, "Volume descriptor: mips %u depth %u\n", actual.MipLevels, actual.Depth);
    ID3D11Resource *resource = NULL;
    hr = volume->QueryInterface(IID_ID3D11Resource, reinterpret_cast<void **>(&resource));
    ok(hr == S_OK && resource, "Volume resource interface: %#lx\n", hr);
    if (resource) resource->Release();
    ID3D11ShaderResourceView *srv = NULL;
    hr = device->CreateShaderResourceView(volume, NULL, &srv);
    ok(hr == S_OK && srv, "Volume shader view: %#lx\n", hr);
    if (srv)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC view;
        srv->GetDesc(&view);
        ok(view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE3D && view.Texture3D.MipLevels == 3,
                "Volume shader view descriptor: %u %u\n", view.ViewDimension, view.Texture3D.MipLevels);
        TestSample(device, context, srv, "Texture3D t : register(t0);"
                "float4 main(float4 p : SV_Position) : SV_Target { return t.Load(int4(0,0,0,1)); }", format == DXGI_FORMAT_B8G8R8A8_UNORM ? 0xff000001 : pixels[1][0]);
        srv->Release();
    }
    D3D11_RENDER_TARGET_VIEW_DESC rt = {};
    rt.Format = desc.Format;
    rt.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
    rt.Texture3D.FirstWSlice = 2;
    rt.Texture3D.WSize = 1;
    ID3D11RenderTargetView *rtv = NULL;
    hr = device->CreateRenderTargetView(volume, &rt, &rtv);
    ok(hr == S_OK && rtv, "Volume slice render target: %#lx\n", hr);
    if (rtv)
    {
        const FLOAT green[4] = {0, 1, 0, 1};
        context->ClearRenderTargetView(rtv, green);
        for (UINT i = 32; i < 48; ++i) pixels[0][i] = 0xff00ff00;
        rtv->Release(); rtv = NULL;
    }
    rt.Texture3D.FirstWSlice = 4;
    hr = device->CreateRenderTargetView(volume, &rt, &rtv);
    ok(hr == E_INVALIDARG && !rtv, "Out of bounds volume slice: %#lx\n", hr);
    D3D11_BOX box = {1, 1, 3, 3, 3, 4};
    const UINT update[4] = {0xff654321, 0xff654321, 0xff654321, 0xff654321};
    context->UpdateSubresource(volume, 0, &box, update, 8, 16);
    for (UINT y = 1; y < 3; ++y)
        for (UINT x = 1; x < 3; ++x) pixels[0][3 * 16 + y * 4 + x] = update[0];
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = device->CreateTexture3D(&desc, NULL, &staging);
    ok(hr == S_OK && staging, "Volume staging texture: %#lx\n", hr);
    if (staging)
    {
        context->CopyResource(staging, volume);
        for (UINT mip = 0; mip < 3; ++mip)
        {
            D3D11_MAPPED_SUBRESOURCE map = {};
            hr = context->Map(staging, mip, D3D11_MAP_READ, 0, &map);
            ok(hr == S_OK, "Map volume mip %u: %#lx\n", mip, hr);
            if (SUCCEEDED(hr))
            {
                UINT mismatches = 0, side = 4 >> mip;
                for (UINT z = 0; z < side; ++z)
                    for (UINT y = 0; y < side; ++y)
                        for (UINT x = 0; x < side; ++x)
                            mismatches += reinterpret_cast<UINT *>(static_cast<BYTE *>(map.pData)
                                    + z * map.DepthPitch + y * map.RowPitch)[x] != pixels[mip][z * side * side + y * side + x];
                ok(!mismatches, "Volume mip %u: %u mismatched pixels\n", mip, mismatches);
                context->Unmap(staging, mip);
            }
        }
        staging->Release();
    }
    volume->Release();
}

START_TEST(texture_dimensions)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE runtime = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE create = runtime ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(runtime, "D3D11CreateDevice")) : NULL;
    if (!create) { skip("D3D11 unavailable\n"); return; }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(hr)) { skip("Hardware D3D11 unavailable: %#lx\n", hr); FreeLibrary(runtime); return; }
    trace("Hardware D3D11 feature level %#x\n", level);
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    for (UINT i = 0; i < ARRAYSIZE(formats); ++i)
    {
        UINT support = 0;
        hr = device->CheckFormatSupport(formats[i], &support);
        ok(hr == S_OK && (support & (D3D11_FORMAT_SUPPORT_TEXTURECUBE | D3D11_FORMAT_SUPPORT_TEXTURE3D))
                == (D3D11_FORMAT_SUPPORT_TEXTURECUBE | D3D11_FORMAT_SUPPORT_TEXTURE3D),
                "Texture dimension caps format %u: %#lx %#x\n", formats[i], hr, support);
        trace("Testing texture format %u\n", formats[i]);
        TestCube(device, context, formats[i]);
        TestVolume(device, context, formats[i]);
    }
    context->ClearState();
    context->Flush();
    context->Release();
    device->Release();
    FreeLibrary(runtime);
}

START_TEST(multisample)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE runtime = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE create = runtime ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(runtime, "D3D11CreateDevice")) : NULL;
    if (!create) { skip("D3D11 unavailable\n"); return; }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    HRESULT hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr)) { skip("Hardware D3D11 unavailable: %#lx\n", hr); FreeLibrary(runtime); return; }
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    for (UINT i = 0; i < ARRAYSIZE(formats); ++i)
    {
        UINT quality = 0, support = 0;
        hr = device->CheckMultisampleQualityLevels(formats[i], 1, &quality);
        ok(hr == S_OK && quality > 0, "Single sample format %u: %#lx quality %u\n", formats[i], hr, quality);
        hr = device->CheckMultisampleQualityLevels(formats[i], 4, &quality);
        ok(hr == S_OK, "Four samples format %u: %#lx\n", formats[i], hr);
        if (!quality) { skip("Format %u has no four-sample support\n", formats[i]); continue; }
        hr = device->CheckFormatSupport(formats[i], &support);
        ok(hr == S_OK && (support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET),
                "Multisample render-target caps %u: %#lx %#x\n", formats[i], hr, support);
        ok(hr == S_OK && (support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD),
                "Multisample shader-load caps %u: %#lx %#x\n", formats[i], hr, support);
        // Two of four samples receive green; the rest retain transparent black.
        // Readback must contain their average, not a copy of one sample.
        ID3D11Texture2D *texture = NULL;
        TestSample(device, context, NULL, "float4 main(float4 p : SV_Position) : SV_Target { return float4(0,1,0,1); }",
                0x80008000, 4, 0x5, formats[i], 1, 1, DXGI_FORMAT_UNKNOWN, 0, &texture);
        if (texture)
        {
            ID3D11ShaderResourceView *view = NULL;
            hr = device->CreateShaderResourceView(texture, NULL, &view);
            ok(hr == S_OK && view, "Multisample shader view format %u: %#lx\n", formats[i], hr);
            if (view)
            {
                // Shader model 4.0 requires literal sample indices. Read each
                // sample to detect translators that silently use sample 0.
                for (UINT sample = 0; sample < 4; ++sample)
                {
                    char source[256];
                    sprintf(source, "Texture2DMS<float4,4> t : register(t0);"
                            "float4 main(float4 p : SV_Position) : SV_Target {"
                            "return t.Load(int2(0,0), %u); }", sample);
                    trace("Loading sample %u of format %u\n", sample, formats[i]);
                    TestSample(device, context, view, source, sample & 1 ? 0 : 0xff00ff00);
                }
                view->Release();
            }
            texture->Release();
        }
    }
    context->ClearState();
    context->Flush();
    context->Release();
    device->Release();
    FreeLibrary(runtime);
}

START_TEST(raster_coverage)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE runtime = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE create = runtime ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(runtime, "D3D11CreateDevice")) : NULL;
    if (!create) { skip("D3D11 unavailable\n"); return; }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    HRESULT hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr)) { skip("Hardware D3D11 unavailable: %#lx\n", hr); FreeLibrary(runtime); return; }
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    for (UINT i = 0; i < ARRAYSIZE(formats); ++i)
    {
        TestSample(device, context, NULL, "float4 main(float4 p : SV_Position) : SV_Target { return float4(0,1,0,1); }",
                0xff00ff00, 1, ~0u, formats[i], 992, 509);
    }
    const DXGI_FORMAT vertex_formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
    /* Exercise vertex attributes after the vertex/instance ID prefix, and
     * an attributes-only shader with a sparse, noperspective color varying.
     */
    for (UINT mode = 0; mode < 3; ++mode)
        for (UINT i = 0; i < ARRAYSIZE(vertex_formats); ++i)
        {
            trace("Color vertex format %u, mode %u\n", vertex_formats[i], mode);
            const char *ps = mode == 2
                    ? "float4 main(float4 p : SV_Position, float4 extra : TEXCOORD, noperspective float4 color : COLOR) : SV_Target { return color; }"
                    : "float4 main(float4 p : SV_Position, float4 color : COLOR) : SV_Target { return color; }";
            TestSample(device, context, NULL, ps,
                    0xff00ff00, 1, ~0u, DXGI_FORMAT_R8G8B8A8_UNORM, 992, 509, vertex_formats[i], mode);
        }
    context->ClearState();
    context->Flush();
    context->Release();
    device->Release();
    FreeLibrary(runtime);
}


START_TEST(null_texture)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE runtime = LoadLibraryW(L"d3d11.dll");
    CREATE_DEVICE create = runtime ? reinterpret_cast<CREATE_DEVICE>(GetProcAddress(runtime, "D3D11CreateDevice")) : NULL;
    if (!create) { skip("D3D11 unavailable\n"); return; }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    HRESULT hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr)) { skip("Hardware D3D11 unavailable: %#lx\n", hr); FreeLibrary(runtime); return; }

    const char *sources[] = {
        "Texture2D<float4> t : register(t0); SamplerState s : register(s0); float4 main() : SV_Target {return t.SampleLevel(s,float2(0.5,0.5),0);}",
        "Texture2D<float4> t : register(t0); float4 main() : SV_Target {return t.Load(int3(0,0,0));}",
        "Texture2D<float4> t : register(t0); float4 main() : SV_Target {uint w,h,l; t.GetDimensions(0,w,h,l); return float4(w,h,l,0);}",
        "Texture2D<float4> t : register(t0); float4 main() : SV_Target {uint w,h,l; t.GetDimensions(3,w,h,l); return float4(w,h,l,0);}",
        "Texture3D<float4> t : register(t0); float4 main() : SV_Target {return t.Load(int4(0,0,0,0));}",
        "Texture3D<float4> t : register(t0); float4 main() : SV_Target {uint w,h,d,l; t.GetDimensions(0,w,h,d,l); return float4(w,h,d,l);}",
        "Texture2DArray<float4> t : register(t0); float4 main() : SV_Target {uint w,h,a,l; t.GetDimensions(0,w,h,a,l); return float4(w,h,a,l);}",
        "TextureCube<float4> t : register(t0); SamplerState s : register(s0); float4 main() : SV_Target {return t.SampleLevel(s,float3(1,0,0),0);}"
    };
    for (UINT i = 0; i < ARRAYSIZE(sources); ++i)
    {
        trace("Unbound texture case %u\n", i);
        TestSample(device, context, NULL, sources[i], 0);
    }

    /* A bound view must retain its contents after unbound-view variants. */
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const UINT green = 0xff00ff00;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = &green; initial.SysMemPitch = sizeof(green);
    ID3D11Texture2D *texture = NULL;
    ID3D11ShaderResourceView *view = NULL;
    hr = device->CreateTexture2D(&desc, &initial, &texture);
    ok(hr == S_OK, "Bound control texture: %#lx\n", hr);
    if (texture) hr = device->CreateShaderResourceView(texture, NULL, &view);
    ok(hr == S_OK && view, "Bound control view: %#lx\n", hr);
    if (view)
    {
        TestSample(device, context, view, sources[0], green);
        TestSample(device, context, NULL, sources[0], 0);
        TestSample(device, context, view, sources[0], green);
        view->Release();
    }
    if (texture) texture->Release();
    context->ClearState();
    context->Flush();
    context->Release();
    device->Release();
    FreeLibrary(runtime);
}
