/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Resource LOD clamps affect immediate and deferred GPU sampling.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wine/test.h>

static void test_texture(ID3D11Device *device, ID3D11DeviceContext *context,
        pD3DCompile compile, UINT dimension)
{
    static const FLOAT lods[] = {0, 1, 1.5f, 2, 0};
    static const FLOAT expected[][4] =
    {
        {1, 0, 0, 1}, {0, 1, 0, 1}, {0, .5f, .5f, 1}, {0, 0, 1, 1}, {1, 0, 0, 1},
    };
    static const char *coords[] = {"0.5", "float2(0.5,0.5)", "float3(0.5,0.5,0.5)"};
    ID3D11Resource *texture = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11SamplerState *sampler = NULL;
    ID3D11ComputeShader *shader = NULL;
    ID3D11Buffer *output = NULL, *staging = NULL;
    ID3D11UnorderedAccessView *uav = NULL;
    ID3D11DeviceContext *deferred = NULL, *recording;
    ID3D11CommandList *list = NULL;
    ID3DBlob *code = NULL, *errors = NULL;
    D3D11_SUBRESOURCE_DATA initial[3];
    D3D11_BUFFER_DESC buffer_desc = {0};
    D3D11_SAMPLER_DESC sampler_desc = {0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    FLOAT pixels[3][64][4], actual, difference;
    char source[512];
    UINT i, j, k, phase;
    HRESULT hr;

    trace("ResourceMinLOD: Texture%uD\n", dimension);
    for (i = 0; i < 3; ++i)
    {
        for (j = 0; j < 64; ++j)
            for (k = 0; k < 4; ++k) pixels[i][j][k] = k == i || k == 3 ? 1 : 0;
        initial[i].pSysMem = pixels[i];
        initial[i].SysMemPitch = (4 >> i) * sizeof(pixels[0][0]);
        initial[i].SysMemSlicePitch = (4 >> i) * initial[i].SysMemPitch;
    }
    if (dimension == 1)
    {
        D3D11_TEXTURE1D_DESC desc = {4, 3, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
                D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_RESOURCE_CLAMP};
        hr = ID3D11Device_CreateTexture1D(device, &desc, initial, (ID3D11Texture1D **)&texture);
    }
    else if (dimension == 2)
    {
        D3D11_TEXTURE2D_DESC desc = {4, 4, 3, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0},
                D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_RESOURCE_CLAMP};
        hr = ID3D11Device_CreateTexture2D(device, &desc, initial, (ID3D11Texture2D **)&texture);
    }
    else
    {
        D3D11_TEXTURE3D_DESC desc = {4, 4, 4, 3, DXGI_FORMAT_R32G32B32A32_FLOAT,
                D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_RESOURCE_CLAMP};
        hr = ID3D11Device_CreateTexture3D(device, &desc, initial, (ID3D11Texture3D **)&texture);
    }
    ok(hr == S_OK, "Texture%uD creation returned %#lx\n", dimension, hr);
    if (FAILED(hr)) goto done;
    actual = ID3D11DeviceContext_GetResourceMinLOD(context, texture);
    ok(actual == 0, "Initial LOD %g\n", actual);
    hr = ID3D11Device_CreateShaderResourceView(device, texture, NULL, &srv);
    ok(hr == S_OK, "Create SRV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    hr = ID3D11Device_CreateSamplerState(device, &sampler_desc, &sampler);
    ok(hr == S_OK, "Create sampler returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    buffer_desc.ByteWidth = 4 * sizeof(FLOAT);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    buffer_desc.StructureByteStride = buffer_desc.ByteWidth;
    hr = ID3D11Device_CreateBuffer(device, &buffer_desc, NULL, &output);
    ok(hr == S_OK, "Create output returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)output, NULL, &uav);
    ok(hr == S_OK, "Create UAV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    buffer_desc.Usage = D3D11_USAGE_STAGING;
    buffer_desc.BindFlags = buffer_desc.MiscFlags = buffer_desc.StructureByteStride = 0;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateBuffer(device, &buffer_desc, NULL, &staging);
    ok(hr == S_OK, "Create staging returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    sprintf(source, "Texture%uD<float4> t : register(t0); SamplerState s : register(s0);"
            "RWStructuredBuffer<float4> o : register(u0); [numthreads(1,1,1)]"
            "void main() { o[0] = t.SampleLevel(s, %s, 0); }", dimension, coords[dimension - 1]);
    hr = compile(source, strlen(source), "resource-min-lod", NULL, NULL, "main", "cs_5_0", 0, 0, &code, &errors);
    ok(hr == S_OK, "Compile returned %#lx: %s\n", hr,
            errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "");
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code), NULL, &shader);
    ok(hr == S_OK, "Create shader returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateDeferredContext(device, 0, &deferred);
    ok(hr == S_OK, "Create deferred context returned %#lx\n", hr);
    if (FAILED(hr)) goto done;

    for (phase = 0; phase < 2; ++phase)
    {
        recording = phase ? deferred : context;
        for (i = 0; i < ARRAY_SIZE(lods); ++i)
        {
            ID3D11DeviceContext_SetResourceMinLOD(context, texture, lods[i]);
            if (phase)
            {
                /* SetResourceMinLOD is immediate-only. A deferred call is
                 * ignored on Windows and must not poison the command list.
                 * Deferred shader sampling still honors the resource clamp. */
                ID3D11DeviceContext_SetResourceMinLOD(deferred, texture, lods[i] ? 0 : 1);
                actual = ID3D11DeviceContext_GetResourceMinLOD(deferred, texture);
                ok(actual == 0, "Deferred GetResourceMinLOD returned %g\n", actual);
            }
            actual = ID3D11DeviceContext_GetResourceMinLOD(context, texture);
            ok(actual == lods[i],
                    "Texture%uD phase %u LOD %g before playback: %g\n", dimension, phase, lods[i], actual);
            ID3D11DeviceContext_CSSetShader(recording, shader, NULL, 0);
            ID3D11DeviceContext_CSSetShaderResources(recording, 0, 1, &srv);
            ID3D11DeviceContext_CSSetSamplers(recording, 0, 1, &sampler);
            ID3D11DeviceContext_CSSetUnorderedAccessViews(recording, 0, 1, &uav, NULL);
            ID3D11DeviceContext_Dispatch(recording, 1, 1, 1);
            if (phase)
            {
                hr = ID3D11DeviceContext_FinishCommandList(deferred, FALSE, &list);
                ok(hr == S_OK, "FinishCommandList returned %#lx\n", hr);
                if (FAILED(hr)) goto done;
                ID3D11DeviceContext_ExecuteCommandList(context, list, TRUE);
                ID3D11CommandList_Release(list);
                list = NULL;
            }
            actual = ID3D11DeviceContext_GetResourceMinLOD(context, texture);
            ok(actual == lods[i], "Texture%uD phase %u LOD %g after playback: %g\n",
                    dimension, phase, lods[i], actual);
            ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)output);
            hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
            ok(hr == S_OK, "Texture%uD phase %u LOD %g readback: %#lx\n", dimension, phase, lods[i], hr);
            if (FAILED(hr)) goto done;
            for (k = 0; k < 4; ++k)
            {
                actual = ((const FLOAT *)mapped.pData)[k];
                difference = actual - expected[i][k];
                ok(difference >= -.01f && difference <= .01f,
                        "Texture%uD phase %u LOD %g component %u: %g expected %g\n",
                        dimension, phase, lods[i], k, actual, expected[i][k]);
            }
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
            ID3D11DeviceContext_ClearState(context);
            actual = ID3D11DeviceContext_GetResourceMinLOD(context, texture);
            ok(actual == lods[i], "ClearState changed resource LOD %g to %g\n", lods[i], actual);
        }
    }
done:
    ID3D11DeviceContext_ClearState(context);
    if (deferred) { ID3D11DeviceContext_ClearState(deferred); ID3D11DeviceContext_Release(deferred); }
    if (list) ID3D11CommandList_Release(list);
    if (shader) ID3D11ComputeShader_Release(shader);
    if (code) ID3D10Blob_Release(code);
    if (errors) ID3D10Blob_Release(errors);
    if (sampler) ID3D11SamplerState_Release(sampler);
    if (srv) ID3D11ShaderResourceView_Release(srv);
    if (uav) ID3D11UnorderedAccessView_Release(uav);
    if (output) ID3D11Buffer_Release(output);
    if (staging) ID3D11Buffer_Release(staging);
    if (texture) ID3D11Resource_Release(texture);
}

START_TEST(resource_min_lod)
{
    HMODULE runtime = LoadLibraryA("d3d11.dll"), compiler = LoadLibraryA("d3dcompiler_47.dll");
    PFN_D3D11_CREATE_DEVICE create_device;
    pD3DCompile compile;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    D3D_DRIVER_TYPE type = D3D_DRIVER_TYPE_HARDWARE;
    HRESULT hr;
    UINT dimension;

    if (!runtime || !compiler) { skip("D3D11 runtime or shader compiler unavailable\n"); goto done; }
    create_device = (void *)GetProcAddress(runtime, "D3D11CreateDevice");
    compile = (void *)GetProcAddress(compiler, "D3DCompile");
    if (!create_device || !compile) { skip("D3D11/compiler exports unavailable\n"); goto done; }
    hr = create_device(NULL, type, NULL, 0, &level, 1, D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr))
    {
        trace("Hardware device unavailable: %#lx; trying WARP\n", hr);
        type = D3D_DRIVER_TYPE_WARP;
        hr = create_device(NULL, type, NULL, 0, &level, 1, D3D11_SDK_VERSION, &device, NULL, &context);
    }
    ok(hr == S_OK, "Create device returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    trace("Device type %u, feature level %#x\n", type, ID3D11Device_GetFeatureLevel(device));
    for (dimension = 1; dimension <= 3; ++dimension) test_texture(device, context, compile, dimension);
done:
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    if (compiler) FreeLibrary(compiler);
    if (runtime) FreeLibrary(runtime);
}
