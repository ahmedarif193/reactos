/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Native Direct3D 11 rendering through the selected WDDM renderer
 */
#define COBJMACROS
#include "precomp.h"
#include <initguid.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

struct native_lock_test
{
    ID3D11Multithread *multithread;
    HANDLE ready;
};

static DWORD WINAPI NativeLockWorker(void *arg)
{
    struct native_lock_test *test = arg;
    SetEvent(test->ready);
    ID3D11Multithread_Enter(test->multithread);
    ID3D11Multithread_Leave(test->multithread);
    ID3D11Multithread_Release(test->multithread);
    return 0;
}

static ULONG NativeRefcount(IUnknown *object)
{
    IUnknown_AddRef(object);
    return IUnknown_Release(object);
}

static void TestImmediateContext(ID3D11Device *device, ID3D11DeviceContext **context)
{
    ID3D11DeviceContext *previous = *context, *second;
    ID3D11Multithread *device_mt = NULL, *context_mt = NULL;
    ID3D11CommandList *commands;
    struct native_lock_test *lock_test;
    ID3D11Device *owner;
    IUnknown *identity = NULL;
    HANDLE thread;
    ULONG count;
    DWORD wait;
    HRESULT hr;

    count = ID3D11DeviceContext_Release(*context);
    ok(!count, "Initial context has %lu references left\n", count);
    count = NativeRefcount((IUnknown *)device);
    ok(count == 1, "Device without external context has %lu references\n", count);
    ID3D11Device_GetImmediateContext(device, context);
    ok(*context == previous, "Immediate context identity changed\n");
    count = NativeRefcount((IUnknown *)device);
    ok(count == 2, "Context should retain one device reference, got %lu\n", count);
    ID3D11Device_GetImmediateContext(device, &second);
    ok(second == *context, "Repeated GetImmediateContext changed identity\n");
    count = NativeRefcount((IUnknown *)device);
    ok(count == 2, "Repeated GetImmediateContext added a device reference: %lu\n", count);
    count = ID3D11DeviceContext_Release(second);
    ok(count == 1, "Context has %lu references, expected one\n", count);

    count = ID3D11Device_Release(device);
    ok(count == 1, "Context-only device has %lu references\n", count);
    ID3D11DeviceContext_GetDevice(*context, &owner);
    ok(owner == device, "Context did not preserve its device\n");
    /* GetDevice restores the caller's device reference. */

    commands = (ID3D11CommandList *)0xdeadbeef;
    hr = ID3D11DeviceContext_FinishCommandList(*context, FALSE, &commands);
    ok(hr == DXGI_ERROR_INVALID_CALL && !commands,
            "Immediate FinishCommandList returned %#lx, %p\n", hr, commands);
    hr = ID3D11Device_QueryInterface(device, &IID_ID3D11Multithread, (void **)&device_mt);
    ok(hr == S_OK, "Device ID3D11Multithread returned %#lx\n", hr);
    hr = ID3D11DeviceContext_QueryInterface(*context, &IID_ID3D11Multithread, (void **)&context_mt);
    ok(hr == S_OK, "Context ID3D11Multithread returned %#lx\n", hr);
    if (!device_mt || !context_mt) goto done;
    hr = ID3D11Multithread_QueryInterface(device_mt, &IID_IUnknown, (void **)&identity);
    ok(hr == S_OK && identity == (IUnknown *)device, "Device threading identity %#lx, %p\n", hr, identity);
    if (identity) IUnknown_Release(identity);
    identity = NULL;
    hr = ID3D11Multithread_QueryInterface(context_mt, &IID_IUnknown, (void **)&identity);
    ok(hr == S_OK && identity == (IUnknown *)*context, "Context threading identity %#lx, %p\n", hr, identity);
    if (identity) IUnknown_Release(identity);
    ok(!ID3D11Multithread_GetMultithreadProtected(context_mt), "Protection should default to off\n");
    ok(!ID3D11Multithread_SetMultithreadProtected(device_mt, TRUE), "Previous protection should be off\n");
    ok(ID3D11Multithread_GetMultithreadProtected(context_mt), "Context did not observe enabled protection\n");

    lock_test = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*lock_test));
    ok(lock_test != NULL, "Cannot allocate threading test\n");
    if (!lock_test) goto restore;
    lock_test->multithread = context_mt;
    lock_test->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(lock_test->ready != NULL, "Cannot create threading event\n");
    if (!lock_test->ready) { HeapFree(GetProcessHeap(), 0, lock_test); goto restore; }
    ID3D11Multithread_AddRef(context_mt);
    ID3D11Multithread_Enter(device_mt);
    ID3D11Multithread_Enter(context_mt);
    thread = CreateThread(NULL, 0, NativeLockWorker, lock_test, 0, NULL);
    ok(thread != NULL, "Cannot create threading worker\n");
    if (thread)
    {
        wait = WaitForSingleObject(lock_test->ready, 5000);
        ok(wait == WAIT_OBJECT_0, "Worker did not start: %lu\n", wait);
        wait = WaitForSingleObject(thread, 100);
        ok(wait == WAIT_TIMEOUT, "Worker bypassed nested device/context lock: %lu\n", wait);
    }
    ID3D11Multithread_Leave(context_mt);
    if (thread)
    {
        wait = WaitForSingleObject(thread, 100);
        ok(wait == WAIT_TIMEOUT, "Worker bypassed remaining device lock: %lu\n", wait);
    }
    ID3D11Multithread_Leave(device_mt);
    if (thread)
    {
        wait = WaitForSingleObject(thread, 5000);
        ok(wait == WAIT_OBJECT_0, "Worker did not finish after Leave: %lu\n", wait);
        CloseHandle(thread);
        /* Keep worker-owned data alive if a broken lock prevents completion. */
        if (wait != WAIT_OBJECT_0) goto restore;
    }
    else ID3D11Multithread_Release(context_mt);
    CloseHandle(lock_test->ready);
    HeapFree(GetProcessHeap(), 0, lock_test);
restore:
    ok(ID3D11Multithread_SetMultithreadProtected(context_mt, FALSE), "Previous protection should be on\n");
    ok(!ID3D11Multithread_GetMultithreadProtected(device_mt), "Device did not observe disabled protection\n");
done:
    if (context_mt) ID3D11Multithread_Release(context_mt);
    if (device_mt) ID3D11Multithread_Release(device_mt);
}

static void TestInputFormatSupport(ID3D11Device *device)
{
    static const DXGI_FORMAT vertex_formats[] =
    {
        DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT,
        DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
        DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R8G8B8A8_UNORM,
    };
    static const DXGI_FORMAT non_vertex_formats[] =
    {
        DXGI_FORMAT_R32G32B32_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_BC1_UNORM,
    };
    static const DXGI_FORMAT index_formats[] = {DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R32_UINT};
    UINT i, support;
    HRESULT hr;

    for (i = 0; i < ARRAYSIZE(vertex_formats); ++i)
    {
        support = 0;
        hr = ID3D11Device_CheckFormatSupport(device, vertex_formats[i], &support);
        ok(hr == S_OK && (support & D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER),
                "Required vertex format %#x returned %#lx, support %#x\n", vertex_formats[i], hr, support);
    }
    for (i = 0; i < ARRAYSIZE(non_vertex_formats); ++i)
    {
        support = ~0u;
        hr = ID3D11Device_CheckFormatSupport(device, non_vertex_formats[i], &support);
        ok(hr == S_OK && !(support & D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER),
                "Non-vertex format %#x returned %#lx, support %#x\n", non_vertex_formats[i], hr, support);
    }
    for (i = 0; i < ARRAYSIZE(index_formats); ++i)
    {
        support = 0;
        hr = ID3D11Device_CheckFormatSupport(device, index_formats[i], &support);
        ok(hr == S_OK && (support & D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER),
                "Required index format %#x returned %#lx, support %#x\n", index_formats[i], hr, support);
    }
}

static void TestTexture1D(ID3D11Device *device, ID3D11DeviceContext *context)
{
    ID3D11Texture1D *source = NULL, *texture = NULL, *staging = NULL;
    ID3D11ShaderResourceView *srv = NULL, *returned_srv = NULL, *srv_identity;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11Resource *retained = NULL;
    IDXGISurface *surface = NULL;
    IUnknown *identity = NULL;
    D3D11_TEXTURE1D_DESC desc = {16, 3, 2, DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA initial[6];
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    DXGI_SURFACE_DESC surface_desc;
    DXGI_MAPPED_RECT rect;
    DWORD pixels[6][16], expected;
    const FLOAT green[4] = {0, 1, 0, 1};
    UINT i, j, phase, bad;
    ULONG count, device_references = NativeRefcount((IUnknown *)device);
    HRESULT hr;

    memset(initial, 0, sizeof(initial));
    for (i = 0; i < ARRAYSIZE(initial); ++i)
    {
        for (j = 0; j < 16; ++j) pixels[i][j] = 0x12340000 + i * 256 + j;
        initial[i].pSysMem = pixels[i]; /* 1D API pitches are ignored. */
    }
    hr = ID3D11Device_CreateTexture1D(device, &desc, initial, &source);
    ok(hr == S_OK, "Create initialized 1D array returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture1D(device, &desc, NULL, &texture);
    ok(hr == S_OK, "Create writable 1D array returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)texture, (ID3D11Resource *)source);
    hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)texture, NULL, &srv);
    ok(hr == S_OK, "Create 1D array SRV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11ShaderResourceView_GetDesc(srv, &srv_desc);
    ok(srv_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE1DARRAY && srv_desc.Texture1DArray.MipLevels == 3
            && srv_desc.Texture1DArray.ArraySize == 2, "Wrong 1D SRV dimension/mips/slices\n");
    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.Format = desc.Format;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
    rtv_desc.Texture1DArray.MipSlice = rtv_desc.Texture1DArray.FirstArraySlice = rtv_desc.Texture1DArray.ArraySize = 1;
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)texture, &rtv_desc, &rtv);
    ok(hr == S_OK, "Create 1D mip/slice RTV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    count = ID3D11Texture1D_Release(texture);
    texture = NULL;
    ok(!count, "Views should hold private texture references, got %lu public references\n", count);
    ID3D11ShaderResourceView_GetResource(srv, &retained);
    ok(retained != NULL, "View lost texture after final application release\n");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture1D(device, &desc, NULL, &staging);
    ok(hr == S_OK, "Create 1D staging array returned %#lx\n", hr);
    if (FAILED(hr) || !retained) goto done;
    for (phase = 0; phase < 2; ++phase)
    {
        if (phase) ID3D11DeviceContext_ClearRenderTargetView(context, rtv, green);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, retained);
        for (i = 0; i < ARRAYSIZE(initial); ++i)
        {
            hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, i, D3D11_MAP_READ, 0, &mapped);
            ok(hr == S_OK, "Map 1D phase %u subresource %u returned %#lx\n", phase, i, hr);
            if (FAILED(hr)) continue;
            bad = 0;
            for (j = 0; j < (16u >> (i % 3)); ++j)
            {
                expected = phase && i == 4 ? 0xff00ff00 : pixels[i][j];
                if (!mapped.pData || ((DWORD *)mapped.pData)[j] != expected) ++bad;
            }
            ok(!bad, "1D phase %u mip %u slice %u has %u incorrect pixels\n", phase, i % 3, i / 3, bad);
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, i);
        }
    }
    /* Bound views survive with no public references and can be reacquired. */
    ID3D11DeviceContext_PSSetShaderResources(context, 0, 1, &srv);
    srv_identity = srv;
    count = ID3D11ShaderResourceView_Release(srv);
    srv = NULL;
    ok(!count, "Bound SRV has %lu public references\n", count);
    ID3D11DeviceContext_PSGetShaderResources(context, 0, 1, &returned_srv);
    ok(returned_srv == srv_identity, "Bound SRV identity changed\n");
    ID3D11DeviceContext_ClearState(context);
    ID3D11Texture1D_Release(staging);
    staging = NULL;

    desc.MipLevels = desc.ArraySize = 1;
    hr = ID3D11Device_CreateTexture1D(device, &desc, initial, &staging);
    ok(hr == S_OK, "Create mappable 1D surface returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Texture1D_QueryInterface(staging, &IID_IDXGISurface, (void **)&surface);
    ok(hr == S_OK, "Query 1D DXGI surface returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IDXGISurface_QueryInterface(surface, &IID_IUnknown, (void **)&identity);
    ok(hr == S_OK && identity == (IUnknown *)staging, "Surface identity %#lx, %p\n", hr, identity);
    hr = IDXGISurface_GetDesc(surface, &surface_desc);
    ok(hr == S_OK && surface_desc.Width == 16 && surface_desc.Height == 1 && surface_desc.SampleDesc.Count == 1,
            "Unexpected 1D surface description, hr %#lx\n", hr);
    hr = IDXGISurface_Map(surface, &rect, DXGI_MAP_READ);
    ok(hr == S_OK, "Map DXGI 1D surface returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(rect.pBits && !memcmp(rect.pBits, pixels[0], sizeof(pixels[0])), "DXGI surface data differs\n");
        hr = IDXGISurface_Unmap(surface);
        ok(hr == S_OK, "Unmap DXGI 1D surface returned %#lx\n", hr);
    }
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    desc.CPUAccessFlags = 0;
    hr = ID3D11Device_CreateTexture1D(device, &desc, NULL, &texture);
    ok(hr == S_OK, "Create 1D depth texture returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)texture, NULL, &dsv);
    ok(hr == S_OK, "Create 1D DSV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11DeviceContext_ClearDepthStencilView(context, dsv, D3D11_CLEAR_DEPTH, 0.25f, 0);
    ID3D11Texture1D_Release(source);
    source = NULL;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture1D(device, &desc, NULL, &source);
    ok(hr == S_OK, "Create 1D depth readback returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)source, (ID3D11Resource *)texture);
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)source, 0, D3D11_MAP_READ, 0, &mapped);
    ok(hr == S_OK, "Map 1D depth readback returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        bad = 0;
        for (j = 0; j < 16; ++j) if (!mapped.pData || ((FLOAT *)mapped.pData)[j] != 0.25f) ++bad;
        ok(!bad, "1D depth clear has %u incorrect values\n", bad);
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)source, 0);
    }
done:
    ID3D11DeviceContext_ClearState(context);
    if (identity) IUnknown_Release(identity);
    if (surface) IDXGISurface_Release(surface);
    if (dsv) ID3D11DepthStencilView_Release(dsv);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (srv) ID3D11ShaderResourceView_Release(srv);
    if (returned_srv) ID3D11ShaderResourceView_Release(returned_srv);
    if (retained) ID3D11Resource_Release(retained);
    if (staging) ID3D11Texture1D_Release(staging);
    if (texture) ID3D11Texture1D_Release(texture);
    if (source) ID3D11Texture1D_Release(source);
    count = NativeRefcount((IUnknown *)device);
    ok(count == device_references, "Texture/view teardown leaked device references: %lu, expected %lu\n", count, device_references);
}

static void TestDepthArrayViews(ID3D11Device *device, ID3D11DeviceContext *context)
{
    D3D11_TEXTURE2D_DESC desc = {8, 4, 2, 3, DXGI_FORMAT_D32_FLOAT, {1, 0},
            D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0};
    D3D11_SUBRESOURCE_DATA initial[6];
    D3D11_DEPTH_STENCIL_VIEW_DESC view_desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11Texture2D *texture = NULL, *staging = NULL;
    ID3D11DepthStencilView *view = NULL, *ms_view = NULL;
    FLOAT pixels[6][32], expected;
    UINT i, x, y, phase, bad;
    HRESULT hr;

    for (i = 0; i < ARRAYSIZE(initial); ++i)
    {
        for (x = 0; x < ARRAYSIZE(pixels[i]); ++x) pixels[i][x] = 0.75f;
        initial[i].pSysMem = pixels[i];
        initial[i].SysMemPitch = (8u >> (i % 2)) * sizeof(FLOAT);
        initial[i].SysMemSlicePitch = initial[i].SysMemPitch * (4u >> (i % 2));
    }
    hr = ID3D11Device_CreateTexture2D(device, &desc, initial, &texture);
    ok(hr == S_OK, "Create depth array returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    ok(hr == S_OK, "Create depth staging array returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    view_desc.Texture2DArray.MipSlice = 1;
    view_desc.Texture2DArray.FirstArraySlice = 1;
    view_desc.Texture2DArray.ArraySize = ~0u;
    hr = ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)texture, &view_desc, &view);
    ok(hr == S_OK, "Create remaining-slice DSV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11DepthStencilView_GetDesc(view, &view_desc);
    ok(view_desc.Format == desc.Format && view_desc.Texture2DArray.MipSlice == 1
            && view_desc.Texture2DArray.FirstArraySlice == 1 && view_desc.Texture2DArray.ArraySize == 2,
            "Incorrect normalized depth array view\n");

    memset(&view_desc, 0, sizeof(view_desc));
    view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
    view_desc.Texture2DMSArray.FirstArraySlice = 2;
    view_desc.Texture2DMSArray.ArraySize = ~0u;
    hr = ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)texture, &view_desc, &ms_view);
    ok(hr == S_OK, "Create MS-form DSV on single-sample array returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11DepthStencilView_GetDesc(ms_view, &view_desc);
    ok(view_desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY
            && view_desc.Texture2DMSArray.FirstArraySlice == 2 && view_desc.Texture2DMSArray.ArraySize == 1,
            "Incorrect normalized MS depth array view\n");
    for (phase = 0; phase < 3; ++phase)
    {
        if (phase == 1) ID3D11DeviceContext_ClearDepthStencilView(context, view, D3D11_CLEAR_DEPTH, 0.25f, 0);
        if (phase == 2) ID3D11DeviceContext_ClearDepthStencilView(context, ms_view, D3D11_CLEAR_DEPTH, 0.5f, 0);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
        for (i = 0; i < ARRAYSIZE(initial); ++i)
        {
            hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, i, D3D11_MAP_READ, 0, &mapped);
            ok(hr == S_OK, "Map depth array phase %u subresource %u returned %#lx\n", phase, i, hr);
            if (FAILED(hr)) continue;
            expected = phase && (i == 3 || i == 5) ? 0.25f : phase == 2 && i == 4 ? 0.5f : 0.75f;
            bad = 0;
            for (y = 0; y < (4u >> (i % 2)); ++y)
                for (x = 0; x < (8u >> (i % 2)); ++x)
                    if (!mapped.pData || ((FLOAT *)((BYTE *)mapped.pData + y * mapped.RowPitch))[x] != expected) ++bad;
            ok(!bad, "Depth array phase %u mip %u slice %u has %u incorrect values\n", phase, i % 2, i / 2, bad);
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, i);
        }
    }
done:
    if (ms_view) ID3D11DepthStencilView_Release(ms_view);
    if (view) ID3D11DepthStencilView_Release(view);
    if (staging) ID3D11Texture2D_Release(staging);
    if (texture) ID3D11Texture2D_Release(texture);
}

static void TestContextState(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11VertexShader *shader)
{
    ID3D11Device1 *device1 = NULL;
    ID3D11DeviceContext1 *context1 = NULL;
    ID3DDeviceContextState *state = NULL, *previous = NULL, *returned = NULL, *identity;
    ID3D11VertexShader *bound;
    ID3D11Device *owner;
    D3D_FEATURE_LEVEL level = ID3D11Device_GetFeatureLevel(device), chosen;
    enum D3D_PRIMITIVE_TOPOLOGY topology;
    DWORD marker = 0x10293847, value = 0;
    UINT size;
    ULONG count;
    HRESULT hr;

    hr = ID3D11Device_QueryInterface(device, &IID_ID3D11Device1, (void **)&device1);
    ok(hr == S_OK, "Get state-capable device returned %#lx\n", hr);
    hr = ID3D11DeviceContext_QueryInterface(context, &IID_ID3D11DeviceContext1, (void **)&context1);
    ok(hr == S_OK, "Get state-capable context returned %#lx\n", hr);
    if (!device1 || !context1) goto done;
    chosen = 0;
    hr = ID3D11Device1_CreateDeviceContextState(device1, 0, &level, 1, D3D11_SDK_VERSION,
            &IID_ID3D11Device1, &chosen, NULL);
    ok(hr == S_FALSE && chosen == level, "Validate state returned %#lx, level %#x\n", hr, chosen);
    chosen = level;
    state = (ID3DDeviceContextState *)0xdeadbeef;
    hr = ID3D11Device1_CreateDeviceContextState(device1, 0, &level, 0, D3D11_SDK_VERSION,
            &IID_ID3D11Device1, &chosen, &state);
    ok(hr == E_INVALIDARG && !chosen && !state,
            "Invalid state creation returned %#lx, level %#x, state %p\n", hr, chosen, state);
    state = NULL;
    hr = ID3D11Device1_CreateDeviceContextState(device1, 0, &level, 1, D3D11_SDK_VERSION,
            &IID_ID3D11Device1, &chosen, &state);
    ok(hr == S_OK && state && chosen == level, "Create state returned %#lx, %p, level %#x\n", hr, state, chosen);
    if (FAILED(hr) || !state) goto done;
    identity = state;
    ID3DDeviceContextState_GetDevice(state, &owner);
    ok(owner == device, "State has unexpected owner %p\n", owner);
    ID3D11Device_Release(owner);
    hr = ID3DDeviceContextState_SetPrivateData(state, &IID_ID3DDeviceContextState, sizeof(marker), &marker);
    ok(hr == S_OK, "Set state private data returned %#lx\n", hr);

    previous = (ID3DDeviceContextState *)0xdeadbeef;
    ID3D11DeviceContext1_SwapDeviceContextState(context1, NULL, &previous);
    ok(!previous, "NULL swap returned state %p\n", previous);
    previous = NULL;
    ID3D11DeviceContext1_SwapDeviceContextState(context1, state, &previous);
    ok(previous != NULL, "Swap did not expose the initial pipeline state\n");
    if (!previous) goto done;
    bound = (ID3D11VertexShader *)0xdeadbeef;
    ID3D11DeviceContext_VSGetShader(context, &bound, NULL, NULL);
    ok(!bound, "New state kept old shader %p\n", bound);
    if (bound && bound != (ID3D11VertexShader *)0xdeadbeef) ID3D11VertexShader_Release(bound);
    /* Modify the new state, then reactivate it. A same-object swap must not
     * restore an obsolete snapshot or drop the current pipeline bindings. */
    ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ID3D11DeviceContext1_SwapDeviceContextState(context1, state, &returned);
    ok(returned == state, "Same-state swap changed identity: %p, %p\n", returned, state);
    if (returned) { ID3DDeviceContextState_Release(returned); returned = NULL; }
    ID3D11DeviceContext_IAGetPrimitiveTopology(context, &topology);
    ok(topology == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, "Same-state swap reset topology %#x\n", topology);
    count = ID3DDeviceContextState_Release(state);
    state = NULL;
    ok(!count, "Active state has %lu public references left\n", count);

    ID3D11DeviceContext1_SwapDeviceContextState(context1, previous, &returned);
    ok(returned == identity, "State identity was lost after final public release: %p, %p\n", returned, identity);
    if (returned)
    {
        size = sizeof(value);
        hr = ID3DDeviceContextState_GetPrivateData(returned, &IID_ID3DDeviceContextState, &size, &value);
        ok(hr == S_OK && size == sizeof(value) && value == marker,
                "Reactivated state lost private data: %#lx, size %u, value %#lx\n", hr, size, value);
    }
    bound = NULL;
    ID3D11DeviceContext_VSGetShader(context, &bound, NULL, NULL);
    ok(bound == shader, "Swap did not restore the draw's vertex shader: %p, %p\n", bound, shader);
    if (bound) ID3D11VertexShader_Release(bound);
    ID3D11DeviceContext_IAGetPrimitiveTopology(context, &topology);
    ok(topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "Swap did not restore draw topology %#x\n", topology);
    count = ID3DDeviceContextState_Release(previous);
    previous = NULL;
    ok(!count, "Restored state has %lu public references left\n", count);
done:
    if (returned) ID3DDeviceContextState_Release(returned);
    if (previous && previous != (ID3DDeviceContextState *)0xdeadbeef) ID3DDeviceContextState_Release(previous);
    if (state) ID3DDeviceContextState_Release(state);
    if (context1) ID3D11DeviceContext1_Release(context1);
    if (device1) ID3D11Device1_Release(device1);
}

static void TestBufferViews(ID3D11Device *device, ID3D11DeviceContext *context,
        ID3D11VertexShader *vs, ID3D11RasterizerState *rasterizer, ID3D11Texture2D *texture,
        ID3D11RenderTargetView *target, ID3D11Texture2D *staging, pD3DCompile compile)
{
    static const char *sources[] =
    {
        "Buffer<float4> v : register(t0); float4 main(float4 p : SV_Position) : SV_Target {"
        "return v.Load((uint)p.x & 7); }",
        "struct E {float4 color; float4 padding;}; StructuredBuffer<E> v : register(t0);"
        "float4 main(float4 p : SV_Position) : SV_Target {return v[(uint)p.x & 7].color;}",
        "ByteAddressBuffer v : register(t0); float4 main(float4 p : SV_Position) : SV_Target {"
        "return asfloat(v.Load4(((uint)p.x & 7) * 16));}"
    };
    static const FLOAT colors[3][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
    static const FLOAT original[4] = {0.25f, 0.5f, 0.75f, 1};
    static const BYTE expected[3][4] = {{0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}};
    const FLOAT clear[4] = {0, 0, 0, 0};
    D3D11_VIEWPORT viewport = {0, 0, 16, 16, 0, 1};
    D3D11_BUFFER_DESC desc;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D11_SUBRESOURCE_DATA initial;
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11Buffer *buffer = NULL, *readback = NULL;
    ID3D11ShaderResourceView *srv = NULL, *invalid = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3DBlob *code = NULL, *errors = NULL;
    FLOAT data[128], value;
    UINT phase, stride, i, j, x, y, bad;
    HRESULT hr;

    for (phase = 0; phase < 3; ++phase)
    {
        stride = phase == 1 ? 8 : 4;
        for (i = 0; i < 16; ++i)
            for (j = 0; j < stride; ++j)
                data[i * stride + j] = phase && i >= 4 && i < 12 && j < 4 ? colors[phase][j] : original[j % 4];
        memset(&desc, 0, sizeof(desc));
        desc.ByteWidth = 16 * stride * sizeof(FLOAT);
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (!phase ? D3D11_BIND_RENDER_TARGET : 0);
        desc.MiscFlags = phase == 1 ? D3D11_RESOURCE_MISC_BUFFER_STRUCTURED
                : phase == 2 ? D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS : 0;
        desc.StructureByteStride = phase == 1 ? stride * sizeof(FLOAT) : 0;
        memset(&initial, 0, sizeof(initial));
        initial.pSysMem = data;
        hr = ID3D11Device_CreateBuffer(device, &desc, &initial, &buffer);
        ok(hr == S_OK, "Create buffer view phase %u returned %#lx\n", phase, hr);
        if (FAILED(hr)) goto done;
        if (!phase)
        {
            memset(&rtv_desc, 0, sizeof(rtv_desc));
            rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_BUFFER;
            rtv_desc.Buffer.FirstElement = 4;
            rtv_desc.Buffer.NumElements = 8;
            hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)buffer, &rtv_desc, &rtv);
            ok(hr == S_OK, "Create buffer RTV returned %#lx\n", hr);
            if (FAILED(hr)) goto done;
            ID3D11DeviceContext_ClearRenderTargetView(context, rtv, colors[0]);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            hr = ID3D11Device_CreateBuffer(device, &desc, NULL, &readback);
            ok(hr == S_OK, "Create buffer readback returned %#lx\n", hr);
            if (FAILED(hr)) goto done;
            ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)readback, (ID3D11Resource *)buffer);
            hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)readback, 0, D3D11_MAP_READ, 0, &mapped);
            ok(hr == S_OK, "Map buffer RTV clear returned %#lx\n", hr);
            if (FAILED(hr)) goto done;
            bad = 0;
            for (i = 0; i < 16; ++i)
                for (j = 0; j < 4; ++j)
                {
                    value = i >= 4 && i < 12 ? colors[0][j] : original[j];
                    if (!mapped.pData || ((FLOAT *)mapped.pData)[i * 4 + j] != value) ++bad;
                }
            ok(!bad, "Buffer RTV clear changed %u incorrect components\n", bad);
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)readback, 0);
        }
        if (phase == 1)
        {
            hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)buffer, NULL, &srv);
            ok(hr == S_OK, "Create default structured SRV returned %#lx\n", hr);
            if (FAILED(hr)) goto done;
            ID3D11ShaderResourceView_GetDesc(srv, &srv_desc);
            ok(srv_desc.Format == DXGI_FORMAT_UNKNOWN && srv_desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFER
                    && !srv_desc.Buffer.FirstElement && srv_desc.Buffer.NumElements == 16,
                    "Default structured view did not use the buffer stride\n");
            ID3D11ShaderResourceView_Release(srv);
            srv = NULL;
        }
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = !phase ? DXGI_FORMAT_R32G32B32A32_FLOAT : phase == 1 ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
        srv_desc.ViewDimension = phase == 2 ? D3D11_SRV_DIMENSION_BUFFEREX : D3D11_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = ~0u;
        srv_desc.Buffer.NumElements = phase == 2 ? 32 : 8;
        if (phase == 2) srv_desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        invalid = (ID3D11ShaderResourceView *)0xdeadbeef;
        hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)buffer, &srv_desc, &invalid);
        ok(hr == E_INVALIDARG && !invalid, "Overflowing buffer view phase %u returned %#lx, %p\n", phase, hr, invalid);
        if (SUCCEEDED(hr) && invalid) ID3D11ShaderResourceView_Release(invalid);
        srv_desc.Buffer.FirstElement = phase == 2 ? 16 : 4;
        hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)buffer, &srv_desc, &srv);
        ok(hr == S_OK, "Create buffer SRV phase %u returned %#lx\n", phase, hr);
        if (FAILED(hr)) goto done;
        hr = compile(sources[phase], strlen(sources[phase]), "native11-buffer", NULL, NULL, "main",
                phase ? "ps_5_0" : "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        ok(hr == S_OK, "Compile buffer PS phase %u returned %#lx: %s\n", phase, hr,
                errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "");
        if (errors) { ID3D10Blob_Release(errors); errors = NULL; }
        if (FAILED(hr)) goto done;
        hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &ps);
        ID3D10Blob_Release(code);
        code = NULL;
        ok(hr == S_OK, "Create buffer PS phase %u returned %#lx\n", phase, hr);
        if (FAILED(hr)) goto done;
        ID3D11DeviceContext_ClearState(context);
        ID3D11DeviceContext_ClearRenderTargetView(context, target, clear);
        ID3D11DeviceContext_OMSetRenderTargets(context, 1, &target, NULL);
        ID3D11DeviceContext_RSSetState(context, rasterizer);
        ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
        ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(context, 0, 1, &srv);
        ID3D11DeviceContext_Draw(context, 3, 0);
        ID3D11DeviceContext_OMSetRenderTargets(context, 0, NULL, NULL);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
        ok(hr == S_OK, "Read back buffer shader phase %u returned %#lx\n", phase, hr);
        if (FAILED(hr)) goto done;
        bad = 0;
        for (y = 0; y < 16; ++y)
            for (x = 0; x < 16; ++x)
                if (!mapped.pData || memcmp((BYTE *)mapped.pData + y * mapped.RowPitch + x * 4, expected[phase], 4)) ++bad;
        ok(!bad, "Buffer shader phase %u has %u incorrect pixels\n", phase, bad);
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
        ID3D11DeviceContext_ClearState(context);
        ID3D11PixelShader_Release(ps); ps = NULL;
        ID3D11ShaderResourceView_Release(srv); srv = NULL;
        if (rtv) { ID3D11RenderTargetView_Release(rtv); rtv = NULL; }
        if (readback) { ID3D11Buffer_Release(readback); readback = NULL; }
        ID3D11Buffer_Release(buffer); buffer = NULL;
    }
done:
    ID3D11DeviceContext_ClearState(context);
    if (errors) ID3D10Blob_Release(errors);
    if (code) ID3D10Blob_Release(code);
    if (ps) ID3D11PixelShader_Release(ps);
    if (srv) ID3D11ShaderResourceView_Release(srv);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (readback) ID3D11Buffer_Release(readback);
    if (buffer) ID3D11Buffer_Release(buffer);
}

static HRESULT CreateCacheState(ID3D11Device *device, UINT type, const void *desc, IUnknown **out)
{
    switch (type)
    {
        case 0: return ID3D11Device_CreateSamplerState(device, desc, (ID3D11SamplerState **)out);
        case 1: return ID3D11Device_CreateBlendState(device, desc, (ID3D11BlendState **)out);
        case 2: return ID3D11Device_CreateDepthStencilState(device, desc, (ID3D11DepthStencilState **)out);
        default: return ID3D11Device_CreateRasterizerState(device, desc, (ID3D11RasterizerState **)out);
    }
}

static void TestStateCache(ID3D11Device *device, ID3D11DeviceContext *context)
{
    D3D11_SAMPLER_DESC sampler;
    D3D11_BLEND_DESC blend;
    D3D11_DEPTH_STENCIL_DESC depth;
    D3D11_RASTERIZER_DESC rasterizer;
    const void *descs[4] = {&sampler, &blend, &depth, &rasterizer};
    IUnknown *first = NULL, *second = NULL, *again = NULL;
    ID3D11SamplerState *sampler_binding;
    UINT type;
    ULONG refs;
    HRESULT hr;

    memset(&sampler, 0, sizeof(sampler));
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = 16;
    memset(&blend, 0, sizeof(blend));
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    memset(&depth, 0, sizeof(depth));
    memset(&rasterizer, 0, sizeof(rasterizer));
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    /* Keep this lifetime fixture distinct from TestShaderDraw's live state. */
    rasterizer.DepthBias = 17;
    for (type = 0; type < 4; ++type)
    {
        ID3D11DeviceContext_ClearState(context);
        hr = CreateCacheState(device, type, descs[type], &first);
        ok(hr == S_OK, "Create cached state %u returned %#lx\n", type, hr);
        if (FAILED(hr)) goto done;
        /* These fields do not change the effective state. The cache key
         * must use the normalized descriptor, not application padding/data. */
        if (!type) { sampler.MaxAnisotropy = 16; sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS; sampler.BorderColor[0] = 1; }
        if (type == 1) { blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; blend.RenderTarget[7].BlendEnable = TRUE; }
        if (type == 2) { depth.DepthFunc = D3D11_COMPARISON_NEVER; depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_ZERO; }
        hr = CreateCacheState(device, type, descs[type], &second);
        ok(hr == S_OK && first == second, "State %u did not share normalized identity: %#lx, %p %p\n", type, hr, first, second);
        if (FAILED(hr)) goto done;
        switch (type)
        {
            case 0:
                sampler_binding = (ID3D11SamplerState *)first;
                ID3D11DeviceContext_PSSetSamplers(context, 0, 1, &sampler_binding);
                break;
            case 1: ID3D11DeviceContext_OMSetBlendState(context, (ID3D11BlendState *)first, NULL, ~0u); break;
            case 2: ID3D11DeviceContext_OMSetDepthStencilState(context, (ID3D11DepthStencilState *)first, 0); break;
            case 3: ID3D11DeviceContext_RSSetState(context, (ID3D11RasterizerState *)first); break;
        }
        refs = IUnknown_Release(first);
        ok(refs == 1, "Cached state %u first release returned %lu\n", type, refs);
        refs = IUnknown_Release(second); second = NULL;
        ok(!refs, "Cached state %u last public release returned %lu\n", type, refs);
        hr = CreateCacheState(device, type, descs[type], &again);
        ok(hr == S_OK && again == first, "Bound state %u lost identity at public refcount zero: %#lx, %p %p\n",
                type, hr, first, again);
        first = NULL;
        if (FAILED(hr)) goto done;
        ID3D11DeviceContext_ClearState(context);
        refs = IUnknown_Release(again); again = NULL;
        ok(!refs, "Unbound cached state %u release returned %lu\n", type, refs);
        hr = CreateCacheState(device, type, descs[type], &again);
        ok(hr == S_OK, "Recreate destroyed state %u returned %#lx\n", type, hr);
        if (FAILED(hr)) goto done;
        refs = IUnknown_Release(again); again = NULL;
        ok(!refs, "Recreated state %u release returned %lu\n", type, refs);
    }
done:
    ID3D11DeviceContext_ClearState(context);
    if (first) IUnknown_Release(first);
    if (second) IUnknown_Release(second);
    if (again) IUnknown_Release(again);
}

static void TestPredication(ID3D11Device *device, ID3D11DeviceContext *context,
        ID3D11VertexShader *vs, ID3D11PixelShader *ps, ID3D11RasterizerState *rasterizer,
        ID3D11Texture2D *texture, ID3D11RenderTargetView *target, ID3D11Texture2D *staging)
{
    static const FLOAT clear[4] = {0, 0, 0, 0};
    static const FLOAT color[4] = {0.25f, 0.5f, 0.75f, 1};
    static const BYTE rendered[4] = {191, 128, 64, 255};
    D3D11_VIEWPORT viewport = {0, 0, 16, 16, 0, 1};
    D3D11_QUERY_DESC desc = {D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11Predicate *predicates[2] = {NULL, NULL}, *bound = NULL;
    ID3D11DeviceContext *deferred = NULL, *commands;
    ID3D11CommandList *list = NULL;
    ID3D11Query *query = NULL;
    D3D11_QUERY_DATA_SO_STATISTICS stats;
    BOOL value, draw;
    UINT mode, result, expected, operation, x, y, component, bad, type;
    DWORD start;
    HRESULT hr;

    hr = ID3D11Device_CreateDeferredContext(device, 0, &deferred);
    ok(hr == S_OK, "Create predicate deferred context returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11DeviceContext_ClearState(context);
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &target, NULL);
    ID3D11DeviceContext_RSSetState(context, rasterizer);
    ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
    ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
    for (result = 0; result < 2; ++result)
    {
        hr = ID3D11Device_CreatePredicate(device, &desc, &predicates[result]);
        ok(hr == S_OK, "Create occlusion predicate %u returned %#lx\n", result, hr);
        if (FAILED(hr)) goto done;
        ID3D11DeviceContext_Begin(context, (ID3D11Asynchronous *)predicates[result]);
        if (result) ID3D11DeviceContext_Draw(context, 3, 0);
        ID3D11DeviceContext_End(context, (ID3D11Asynchronous *)predicates[result]);
    }
    /* Do not poll the predicates before use: the UMD/GPU must resolve their
     * dependency. Exercise both Boolean outcomes and draw/clear predication. */
    for (mode = 0; mode < 2; ++mode)
        for (result = 0; result < 2; ++result)
            for (expected = 0; expected < 2; ++expected)
                for (operation = 0; operation < 2; ++operation)
                {
                    commands = mode ? deferred : context;
                    ID3D11DeviceContext_ClearState(commands);
                    ID3D11DeviceContext_ClearRenderTargetView(commands, target, clear);
                    ID3D11DeviceContext_OMSetRenderTargets(commands, 1, &target, NULL);
                    ID3D11DeviceContext_RSSetState(commands, rasterizer);
                    ID3D11DeviceContext_RSSetViewports(commands, 1, &viewport);
                    ID3D11DeviceContext_IASetPrimitiveTopology(commands, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    ID3D11DeviceContext_VSSetShader(commands, vs, NULL, 0);
                    ID3D11DeviceContext_PSSetShader(commands, ps, NULL, 0);
                    ID3D11DeviceContext_SetPredication(commands, predicates[result], expected);
                    value = !expected;
                    ID3D11DeviceContext_GetPredication(commands, &bound, &value);
                    ok(bound == predicates[result] && value == expected,
                            "Predicate state mode %u result %u value %u: %p, %d\n", mode, result, expected, bound, value);
                    if (bound) { ID3D11Predicate_Release(bound); bound = NULL; }
                    if (operation) ID3D11DeviceContext_ClearRenderTargetView(commands, target, color);
                    else ID3D11DeviceContext_Draw(commands, 3, 0);
                    ID3D11DeviceContext_SetPredication(commands, NULL, FALSE);
                    ID3D11DeviceContext_OMSetRenderTargets(commands, 0, NULL, NULL);
                    if (mode)
                    {
                        hr = ID3D11DeviceContext_FinishCommandList(deferred, FALSE, &list);
                        ok(hr == S_OK, "Finish predicate list returned %#lx\n", hr);
                        if (FAILED(hr)) goto done;
                        ID3D11DeviceContext_SetPredication(context, predicates[1], TRUE);
                        ID3D11DeviceContext_ExecuteCommandList(context, list, TRUE);
                        value = FALSE;
                        ID3D11DeviceContext_GetPredication(context, &bound, &value);
                        ok(bound == predicates[1] && value, "Execute did not restore predicate state\n");
                        if (bound) { ID3D11Predicate_Release(bound); bound = NULL; }
                        ID3D11DeviceContext_SetPredication(context, NULL, FALSE);
                        ID3D11CommandList_Release(list); list = NULL;
                    }
                    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
                    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
                    ok(hr == S_OK, "Map predicated mode %u result %u value %u operation %u returned %#lx\n",
                            mode, result, expected, operation, hr);
                    if (FAILED(hr)) goto done;
                    draw = result != expected;
                    bad = 0;
                    for (y = 0; y < 16; ++y)
                        for (x = 0; x < 16; ++x)
                            for (component = 0; component < 4; ++component)
                                if (!mapped.pData || abs(((BYTE *)mapped.pData)[y * mapped.RowPitch + x * 4 + component]
                                        - (draw ? rendered[component] : 0)) > 1) ++bad;
                    ok(!bad, "Predicated mode %u result %u value %u operation %u has %u incorrect components\n",
                            mode, result, expected, operation, bad);
                    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
                }
    for (result = 0; result < 2; ++result)
    {
        value = !result;
        hr = ID3D11DeviceContext_GetData(context, (ID3D11Asynchronous *)predicates[result], &value, sizeof(value), 0);
        ok(hr == S_OK && value == result, "Occlusion predicate %u data returned %#lx, %d\n", result, hr, value);
    }
    ID3D11DeviceContext_SetPredication(context, NULL, TRUE);
    value = FALSE;
    ID3D11DeviceContext_GetPredication(context, &bound, &value);
    ok(!bound && value, "NULL predicate did not preserve the supplied Boolean\n");
    ID3D11DeviceContext_ClearState(context);
    ID3D11DeviceContext_GetPredication(context, &bound, &value);
    ok(!bound && !value, "ClearState did not reset predication\n");

    /* Check all stream mappings and Boolean/statistics result sizes without
     * inventing stream-output work before that pipeline stage is implemented. */
    for (type = D3D11_QUERY_SO_STATISTICS; type <= D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3; ++type)
    {
        desc.Query = type;
        hr = ID3D11Device_CreateQuery(device, &desc, &query);
        ok(hr == S_OK, "Create stream query %u returned %#lx\n", type, hr);
        if (FAILED(hr)) goto done;
        ID3D11DeviceContext_Begin(context, (ID3D11Asynchronous *)query);
        ID3D11DeviceContext_End(context, (ID3D11Asynchronous *)query);
        start = GetTickCount();
        memset(&stats, 0xff, sizeof(stats));
        do
        {
            hr = ID3D11DeviceContext_GetData(context, (ID3D11Asynchronous *)query, &stats,
                    ID3D11Query_GetDataSize(query), 0);
            if (hr != S_FALSE) break;
            Sleep(1);
        } while (GetTickCount() - start < 5000);
        ok(hr == S_OK, "Stream query %u completion returned %#lx\n", type, hr);
        if (type & 1) ok(!*(BOOL *)&stats, "Empty stream overflow query %u is nonzero\n", type);
        else ok(!stats.NumPrimitivesWritten && !stats.PrimitivesStorageNeeded,
                "Empty stream statistics query %u is nonzero\n", type);
        ID3D11Query_Release(query); query = NULL;
    }
done:
    ID3D11DeviceContext_ClearState(context);
    if (deferred) { ID3D11DeviceContext_ClearState(deferred); ID3D11DeviceContext_Release(deferred); }
    if (list) ID3D11CommandList_Release(list);
    if (query) ID3D11Query_Release(query);
    for (result = 0; result < 2; ++result)
        if (predicates[result]) ID3D11Predicate_Release(predicates[result]);
}

static void TestComputeDispatch(ID3D11Device *device, ID3D11DeviceContext *context, pD3DCompile compile)
{
    static const char source[] =
            "StructuredBuffer<uint> src : register(t2);"
            "RWStructuredBuffer<uint> dst : register(u1);"
            "cbuffer Params : register(b3) { uint add; };"
            "[numthreads(4,1,1)] void main(uint3 id : SV_DispatchThreadID)"
            "{ dst[id.x] = src[id.x] * 3 + add; }";
    ID3D11Buffer *input = NULL, *output = NULL, *staging = NULL, *constants[2] = {NULL, NULL};
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11UnorderedAccessView *uav = NULL, *observed_uav = NULL;
    ID3D11ComputeShader *cs = NULL, *observed_cs = NULL;
    ID3D11DeviceContext *deferred = NULL;
    ID3D11CommandList *list = NULL;
    ID3DBlob *code = NULL, *errors = NULL;
    D3D11_BUFFER_DESC desc;
    D3D11_SUBRESOURCE_DATA initial = {0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    const UINT clear[4] = {0xdeadbeef, 0x12345678, 0, 0};
    UINT values[16], cb[4] = {0}, i, phase, bad;
    HRESULT hr;

    trace("Native11 compute: deferred dispatch and restored state readback\n");
    ID3D11DeviceContext_ClearState(context);
    hr = compile(source, sizeof(source) - 1, "native11-compute", NULL, NULL, "main", "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    ok(hr == S_OK, "Compile CS returned %#lx: %s\n", hr,
            errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "");
    if (errors) { ID3D10Blob_Release(errors); errors = NULL; }
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code), NULL, &cs);
    ok(hr == S_OK, "Create compute shader returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    for (i = 0; i < ARRAYSIZE(values); ++i) values[i] = i + 7;
    memset(&desc, 0, sizeof(desc));
    desc.ByteWidth = sizeof(values);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(UINT);
    initial.pSysMem = values;
    hr = ID3D11Device_CreateBuffer(device, &desc, &initial, &input);
    ok(hr == S_OK, "Create compute input returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)input, NULL, &srv);
    ok(hr == S_OK, "Create compute SRV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    hr = ID3D11Device_CreateBuffer(device, &desc, NULL, &output);
    ok(hr == S_OK, "Create compute output returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)output, NULL, &uav);
    ok(hr == S_OK, "Create compute UAV returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateBuffer(device, &desc, NULL, &staging);
    ok(hr == S_OK, "Create compute staging returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    memset(&desc, 0, sizeof(desc));
    desc.ByteWidth = sizeof(cb);
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    for (i = 0; i < 2; ++i)
    {
        cb[0] = i ? 29 : 17;
        initial.pSysMem = cb;
        hr = ID3D11Device_CreateBuffer(device, &desc, &initial, &constants[i]);
        ok(hr == S_OK, "Create compute constants %u returned %#lx\n", i, hr);
        if (FAILED(hr)) goto done;
    }
    ID3D11DeviceContext_CSSetShader(context, cs, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(context, 2, 1, &srv);
    ID3D11DeviceContext_CSSetConstantBuffers(context, 3, 1, &constants[0]);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 1, 1, &uav, NULL);
    hr = ID3D11Device_CreateDeferredContext(device, 0, &deferred);
    ok(hr == S_OK, "Create compute deferred context returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ID3D11DeviceContext_CSSetShader(deferred, cs, NULL, 0);
    ID3D11DeviceContext_CSSetShaderResources(deferred, 2, 1, &srv);
    ID3D11DeviceContext_CSSetConstantBuffers(deferred, 3, 1, &constants[1]);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(deferred, 1, 1, &uav, NULL);
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(deferred, uav, clear);
    ID3D11DeviceContext_Dispatch(deferred, 4, 1, 1);
    hr = ID3D11DeviceContext_FinishCommandList(deferred, FALSE, &list);
    ok(hr == S_OK, "Finish compute command list returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    for (phase = 0; phase < 3; ++phase)
    {
        if (phase == 0 || phase == 2) ID3D11DeviceContext_ExecuteCommandList(context, list, TRUE);
        else
        {
            /* The previous staging copy/map may have borrowed the CS stage.
             * Dispatch without rebinding any state, so empty restore callbacks
             * cannot accidentally make this test pass. */
            ID3D11DeviceContext_ClearUnorderedAccessViewUint(context, uav, clear);
            ID3D11DeviceContext_Dispatch(context, 4, 1, 1);
        }
        ID3D11DeviceContext_CSGetShader(context, &observed_cs, NULL, NULL);
        ok(observed_cs == cs, "Phase %u restored compute shader %p, expected %p\n", phase, observed_cs, cs);
        if (observed_cs) ID3D11ComputeShader_Release(observed_cs);
        ID3D11DeviceContext_CSGetUnorderedAccessViews(context, 1, 1, &observed_uav);
        ok(observed_uav == uav, "Phase %u restored UAV %p, expected %p\n", phase, observed_uav, uav);
        if (observed_uav) ID3D11UnorderedAccessView_Release(observed_uav);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)output);
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
        ok(hr == S_OK, "Phase %u compute readback returned %#lx\n", phase, hr);
        if (SUCCEEDED(hr))
        {
            bad = 0;
            for (i = 0; i < ARRAYSIZE(values); ++i)
                if (((UINT *)mapped.pData)[i] != values[i] * 3 + (phase == 1 ? 17 : 29)) ++bad;
            ok(!bad, "Phase %u has %u incorrect GPU-written values; first %#x\n", phase, bad, ((UINT *)mapped.pData)[0]);
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
        }
    }
done:
    ID3D11DeviceContext_ClearState(context);
    if (deferred) { ID3D11DeviceContext_ClearState(deferred); ID3D11DeviceContext_Release(deferred); }
    if (list) ID3D11CommandList_Release(list);
    for (i = 0; i < 2; ++i) if (constants[i]) ID3D11Buffer_Release(constants[i]);
    if (cs) ID3D11ComputeShader_Release(cs);
    if (uav) ID3D11UnorderedAccessView_Release(uav);
    if (srv) ID3D11ShaderResourceView_Release(srv);
    if (input) ID3D11Buffer_Release(input);
    if (output) ID3D11Buffer_Release(output);
    if (staging) ID3D11Buffer_Release(staging);
    if (code) ID3D10Blob_Release(code);
}

static void TestShaderDraw(ID3D11Device *device, ID3D11DeviceContext *context,
        ID3D11Texture2D *texture, ID3D11RenderTargetView *target, ID3D11Texture2D *staging)
{
    static const char source[] =
            "float4 vs_main(uint id : SV_VertexID) : SV_Position {"
            "float2 p = float2((id << 1) & 2, id & 2);"
            "return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }"
            "float4 ps_main() : SV_Target { return float4(0.25, 0.5, 0.75, 1); }";
    const FLOAT clear[4] = {0, 0, 0, 0};
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    pD3DCompile compile;
    ID3DBlob *code = NULL, *errors = NULL;
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11RasterizerState *rasterizer = NULL;
    ID3D11Query *query = NULL;
    D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT, 0};
    D3D11_RASTERIZER_DESC rasterizer_desc;
    D3D11_VIEWPORT viewport = {0, 0, 16, 16, 0, 1};
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    DWORD start;
    BOOL complete;
    UINT phase, x, y, bad;

    ok(compiler != NULL, "Shader compiler unavailable, error %lu\n", GetLastError());
    if (!compiler) return;
    compile = (pD3DCompile)GetProcAddress(compiler, "D3DCompile");
    ok(compile != NULL, "D3DCompile entry point unavailable\n");
    if (!compile) goto done;
    for (phase = 0; phase < 2; ++phase)
    {
        hr = compile(source, sizeof(source) - 1, "native11", NULL, NULL,
                phase ? "ps_main" : "vs_main", phase ? "ps_4_0" : "vs_4_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        ok(hr == S_OK, "Compile %s returned %#lx: %s\n", phase ? "PS" : "VS", hr,
                errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "");
        if (errors) { ID3D10Blob_Release(errors); errors = NULL; }
        if (FAILED(hr)) goto done;
        if (phase)
            hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(code),
                    ID3D10Blob_GetBufferSize(code), NULL, &ps);
        else
            hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(code),
                    ID3D10Blob_GetBufferSize(code), NULL, &vs);
        ID3D10Blob_Release(code);
        code = NULL;
        ok(hr == S_OK, "Create %s returned %#lx\n", phase ? "PS" : "VS", hr);
        if (FAILED(hr)) goto done;
    }
    memset(&rasterizer_desc, 0, sizeof(rasterizer_desc));
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    hr = ID3D11Device_CreateRasterizerState(device, &rasterizer_desc, &rasterizer);
    ok(hr == S_OK, "Create rasterizer returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateQuery(device, &query_desc, &query);
    ok(hr == S_OK, "Create completion query returned %#lx\n", hr);
    if (FAILED(hr)) goto done;

    /* Separate event-query completion from shader execution. Both must work
     * without a swapchain or any relationship to the desktop adapter. */
    for (phase = 0; phase < 4; ++phase)
    {
        const char *label = !phase ? "empty" : phase == 1 ? "draw"
                : phase == 2 ? "default-state draw" : "context-state draw";
        if (phase >= 2)
        {
            ID3D11BlendState *bound_blend = NULL;
            ID3D11DepthStencilState *bound_depth = NULL;
            ID3D11RasterizerState *bound_rasterizer = NULL;
            FLOAT factors[4];
            UINT mask, reference;

            ID3D11DeviceContext_ClearState(context);
            ID3D11DeviceContext_OMGetBlendState(context, &bound_blend, factors, &mask);
            ID3D11DeviceContext_OMGetDepthStencilState(context, &bound_depth, &reference);
            ID3D11DeviceContext_RSGetState(context, &bound_rasterizer);
            ok(!bound_blend && !bound_depth && !bound_rasterizer,
                    "ClearState exposed internal defaults: %p %p %p\n", bound_blend, bound_depth, bound_rasterizer);
            ok(mask == ~0u && !reference && factors[0] == 1 && factors[1] == 1 && factors[2] == 1 && factors[3] == 1,
                    "ClearState default mask/reference/factors incorrect\n");
            if (bound_blend) ID3D11BlendState_Release(bound_blend);
            if (bound_depth) ID3D11DepthStencilState_Release(bound_depth);
            if (bound_rasterizer) ID3D11RasterizerState_Release(bound_rasterizer);
        }
        if (phase)
        {
            ID3D11DeviceContext_ClearRenderTargetView(context, target, clear);
            ID3D11DeviceContext_OMSetRenderTargets(context, 1, &target, NULL);
            ID3D11DeviceContext_OMSetBlendState(context, NULL, NULL, ~0u);
            ID3D11DeviceContext_RSSetState(context, phase >= 2 ? NULL : rasterizer);
            ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
            ID3D11DeviceContext_IASetInputLayout(context, NULL);
            ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
            ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
            if (phase == 3) TestContextState(device, context, vs);
            trace("Native11 shader draw begin\n");
            ID3D11DeviceContext_Draw(context, 3, 0);
            trace("Native11 shader draw returned\n");
        }
        trace("Native11 %s event end/flush begin\n", label);
        start = GetTickCount();
        ID3D11DeviceContext_End(context, (ID3D11Asynchronous *)query);
        ID3D11DeviceContext_Flush(context);
        trace("Native11 %s event flush returned after %lu ms\n", label, GetTickCount() - start);
        complete = FALSE;
        do
        {
            hr = ID3D11DeviceContext_GetData(context, (ID3D11Asynchronous *)query, &complete,
                    sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr != S_FALSE) break;
            Sleep(1);
        } while (GetTickCount() - start < 5000);
        ok(hr == S_OK && complete, "%s completion returned %#lx, done %d after %lu ms\n",
                label, hr, complete, GetTickCount() - start);
        if (hr != S_OK || !complete) goto done;
        if (!phase) continue;
        ID3D11DeviceContext_OMSetRenderTargets(context, 0, NULL, NULL);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
        memset(&mapped, 0, sizeof(mapped));
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
        ok(hr == S_OK, "Read back shader draw returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(mapped.pData && mapped.RowPitch >= 16 * 4, "Invalid drawn surface %p, pitch %u\n",
                    mapped.pData, mapped.RowPitch);
            if (mapped.pData && mapped.RowPitch >= 16 * 4)
            {
                bad = 0;
                for (y = 0; y < 16; ++y)
                    for (x = 0; x < 16; ++x)
                    {
                        const BYTE *pixel = (const BYTE *)mapped.pData + y * mapped.RowPitch + x * 4;
                        if (abs(pixel[0] - 191) > 1 || abs(pixel[1] - 128) > 1 ||
                                abs(pixel[2] - 64) > 1 || pixel[3] != 255) ++bad;
                    }
                if (bad)
                {
                    const BYTE *pixel = mapped.pData;
                    trace("Native11 shader readback first BGRA %u,%u,%u,%u\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                }
                ok(!bad, "%u pixels did not contain the shader output\n", bad);
            }
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
        }
    }
    TestStateCache(device, context);
    TestPredication(device, context, vs, ps, rasterizer, texture, target, staging);
    TestBufferViews(device, context, vs, rasterizer, texture, target, staging, compile);
    TestComputeDispatch(device, context, compile);
done:
    ID3D11DeviceContext_ClearState(context);
    if (query) ID3D11Query_Release(query);
    if (rasterizer) ID3D11RasterizerState_Release(rasterizer);
    if (ps) ID3D11PixelShader_Release(ps);
    if (vs) ID3D11VertexShader_Release(vs);
    if (code) ID3D10Blob_Release(code);
    FreeLibrary(compiler);
}

static void TestOutputOwnership(IDXGIAdapter1 *adapter, const LUID *luid)
{
    PFN_D3DKMTOpenAdapterFromGdiDisplayName open_adapter =
            (PFN_D3DKMTOpenAdapterFromGdiDisplayName)LoadD3DKMTProc("D3DKMTOpenAdapterFromGdiDisplayName");
    IDXGIOutput *output;
    DXGI_OUTPUT_DESC desc;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open;
    UINT i;
    HRESULT hr;
    NTSTATUS status;

    if (!open_adapter) { skip("No GDI adapter ownership query\n"); return; }
    for (i = 0; ; ++i)
    {
        output = NULL;
        hr = IDXGIAdapter1_EnumOutputs(adapter, i, &output);
        if (FAILED(hr))
        {
            ok(hr == DXGI_ERROR_NOT_FOUND && !output, "EnumOutputs(%u) returned %#lx, %p\n", i, hr, output);
            break;
        }
        hr = IDXGIOutput_GetDesc(output, &desc);
        ok(hr == S_OK, "Output %u description returned %#lx\n", i, hr);
        if (SUCCEEDED(hr))
        {
            memset(&open, 0, sizeof(open));
            lstrcpynW(open.DeviceName, desc.DeviceName, ARRAYSIZE(open.DeviceName));
            status = open_adapter(&open);
            ok(NT_SUCCESS(status), "Open output %S returned %#lx\n", desc.DeviceName, status);
            if (NT_SUCCESS(status))
            {
                ok(open.AdapterLuid.LowPart == luid->LowPart && open.AdapterLuid.HighPart == luid->HighPart,
                        "Output %S belongs to %lx:%lx, enumerated under %lx:%lx\n", desc.DeviceName,
                        open.AdapterLuid.HighPart, open.AdapterLuid.LowPart, luid->HighPart, luid->LowPart);
                CloseAdapter(open.hAdapter);
            }
        }
        IDXGIOutput_Release(output);
    }
    trace("Native11 renderer owns %u DXGI outputs\n", i);
}

START_TEST(native11)
{
    typedef HRESULT (WINAPI *PFN_CREATE_FACTORY)(REFIID, void **);
    typedef HRESULT (WINAPI *PFN_CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
            const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE dxgi = NULL, d3d11 = NULL;
    PFN_CREATE_FACTORY create_factory;
    PFN_CREATE_DEVICE create_device;
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *texture = NULL, *opened = NULL, *staging = NULL;
    ID3D11RenderTargetView *target = NULL;
    IDXGIResource *resource = NULL;
    D3DKMT_HANDLE km_adapter;
    LUID luid;
    DXGI_ADAPTER_DESC1 adapter_desc;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D_FEATURE_LEVEL obtained;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const FLOAT colors[][4] = {{0.25f, 0.5f, 0.75f, 1.0f}, {0.75f, 0.25f, 0.5f, 1.0f}};
    const BYTE expected[][4] = {{191, 128, 64, 255}, {128, 64, 191, 255}};
    HANDLE share = NULL;
    HRESULT hr;
    UINT i, x, y, phase, bad;

    km_adapter = OpenRenderAdapterEx(&luid, NULL);
    if (!km_adapter) { skip("No WDDM render adapter\n"); return; }
    CloseAdapter(km_adapter);
    dxgi = LoadLibraryW(L"dxgi.dll");
    d3d11 = LoadLibraryW(L"d3d11.dll");
    ok(dxgi && d3d11, "Direct3D runtime modules unavailable\n");
    if (!dxgi || !d3d11) goto done;
    create_factory = (PFN_CREATE_FACTORY)GetProcAddress(dxgi, "CreateDXGIFactory1");
    create_device = (PFN_CREATE_DEVICE)GetProcAddress(d3d11, "D3D11CreateDevice");
    ok(create_factory && create_device, "Direct3D runtime entry points unavailable\n");
    if (!create_factory || !create_device) goto done;
    hr = create_factory(&IID_IDXGIFactory1, (void **)&factory);
    ok(hr == S_OK, "CreateDXGIFactory1 returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    for (i = 0; ; ++i)
    {
        hr = IDXGIFactory1_EnumAdapters1(factory, i, &adapter);
        if (FAILED(hr)) break;
        hr = IDXGIAdapter1_GetDesc1(adapter, &adapter_desc);
        if (SUCCEEDED(hr) && adapter_desc.AdapterLuid.LowPart == luid.LowPart &&
                adapter_desc.AdapterLuid.HighPart == luid.HighPart) break;
        IDXGIAdapter1_Release(adapter);
        adapter = NULL;
    }
    ok(adapter != NULL, "DXGI did not enumerate the selected WDDM renderer\n");
    if (!adapter) goto done;
    trace("Native11 renderer %S, PCI %04x:%04x\n", adapter_desc.Description,
            adapter_desc.VendorId, adapter_desc.DeviceId);
    TestOutputOwnership(adapter, &adapter_desc.AdapterLuid);
    hr = create_device((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, &obtained, &context);
    ok(hr == S_OK, "D3D11CreateDevice returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    trace("Native11 feature level %#x\n", obtained);
    TestImmediateContext(device, &context);
    TestInputFormatSupport(device);
    TestTexture1D(device, context);
    TestDepthArrayViews(device, context);
    memset(&desc, 0, sizeof(desc));
    desc.Width = desc.Height = 16;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &texture);
    ok(hr == S_OK, "Create shared texture returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Texture2D_QueryInterface(texture, &IID_IDXGIResource, (void **)&resource);
    ok(hr == S_OK, "Query IDXGIResource returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IDXGIResource_GetSharedHandle(resource, &share);
    ok(hr == S_OK && share != NULL, "GetSharedHandle returned %#lx, handle %p\n", hr, share);
    if (FAILED(hr) || !share) goto done;
    hr = ID3D11Device_OpenSharedResource(device, share, &IID_ID3D11Texture2D, (void **)&opened);
    ok(hr == S_OK, "OpenSharedResource returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)texture, NULL, &target);
    ok(hr == S_OK, "CreateRenderTargetView returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    ok(hr == S_OK, "Create staging texture returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    for (phase = 0; phase < 2; ++phase)
    {
        const char *label = phase ? "shared" : "direct";
        ID3D11DeviceContext_ClearRenderTargetView(context, target, colors[phase]);
        /* Publish writes before using an independently opened shared resource.
         * The direct case deliberately relies on Map's implicit synchronization. */
        if (phase) ID3D11DeviceContext_Flush(context);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging,
                (ID3D11Resource *)(phase ? opened : texture));
        memset(&mapped, 0, sizeof(mapped));
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
        ok(hr == S_OK, "Read back %s clear returned %#lx\n", label, hr);
        if (FAILED(hr)) continue;
        ok(mapped.pData && mapped.RowPitch >= 16 * 4, "Invalid %s surface %p, pitch %u\n",
                label, mapped.pData, mapped.RowPitch);
        if (mapped.pData && mapped.RowPitch >= 16 * 4)
        {
            bad = 0;
            for (y = 0; y < 16; ++y)
                for (x = 0; x < 16; ++x)
                {
                    const BYTE *pixel = (const BYTE *)mapped.pData + y * mapped.RowPitch + x * 4;
                    if (abs(pixel[0] - expected[phase][0]) > 1 || abs(pixel[1] - expected[phase][1]) > 1 ||
                            abs(pixel[2] - expected[phase][2]) > 1 || pixel[3] != expected[phase][3]) ++bad;
                }
            if (bad)
            {
                const BYTE *pixel = mapped.pData;
                trace("Native11 %s readback pitch %u, first BGRA %u,%u,%u,%u; expected %u,%u,%u,%u\n",
                        label, mapped.RowPitch, pixel[0], pixel[1], pixel[2], pixel[3],
                        expected[phase][0], expected[phase][1], expected[phase][2], expected[phase][3]);
            }
            ok(!bad, "%u %s pixels did not contain the GPU clear color\n", bad, label);
        }
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    }
    TestShaderDraw(device, context, texture, target, staging);
    /* Destruction may leave UMD work pending after the COM object is freed.
     * Repeated creation must not confuse a reused object address with the
     * runtime cookie of an earlier resource still awaiting deallocation. */
    ID3D11Texture2D_GetDesc(texture, &desc);
    for (i = 0; i < 32; ++i)
    {
        ID3D11Texture2D *temporary = NULL;
        ID3D11RenderTargetView *temporary_target = NULL;
        hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &temporary);
        ok(hr == S_OK, "Deferred lifetime texture %u returned %#lx\n", i, hr);
        if (FAILED(hr)) break;
        hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)temporary, NULL, &temporary_target);
        ok(hr == S_OK, "Deferred lifetime target %u returned %#lx\n", i, hr);
        if (SUCCEEDED(hr))
        {
            ID3D11DeviceContext_ClearRenderTargetView(context, temporary_target, colors[i % 2]);
            ID3D11RenderTargetView_Release(temporary_target);
        }
        ID3D11Texture2D_Release(temporary);
        if (FAILED(hr)) break;
    }
    ID3D11DeviceContext_Flush(context);
    /* An opened shared resource owns its backing independently of the
     * creator. Closing the original must not mark the surviving alias dead. */
    ID3D11RenderTargetView_Release(target);
    target = NULL;
    IDXGIResource_Release(resource);
    resource = NULL;
    ID3D11Texture2D_Release(texture);
    texture = NULL;
    ID3D11DeviceContext_Flush(context);
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)opened, NULL, &target);
    ok(hr == S_OK, "Surviving shared texture target returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ID3D11DeviceContext_ClearRenderTargetView(context, target, colors[0]);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)opened);
        memset(&mapped, 0, sizeof(mapped));
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
        ok(hr == S_OK, "Read back surviving shared texture returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(mapped.pData && mapped.RowPitch >= 16 * 4, "Invalid surviving shared texture %p, pitch %u\n",
                    mapped.pData, mapped.RowPitch);
            if (mapped.pData && mapped.RowPitch >= 16 * 4)
            {
                bad = 0;
                for (y = 0; y < 16; ++y)
                    for (x = 0; x < 16; ++x)
                    {
                        const BYTE *pixel = (const BYTE *)mapped.pData + y * mapped.RowPitch + x * 4;
                        if (abs(pixel[0] - expected[0][0]) > 1 || abs(pixel[1] - expected[0][1]) > 1 ||
                                abs(pixel[2] - expected[0][2]) > 1 || pixel[3] != expected[0][3]) ++bad;
                    }
                ok(!bad, "%u pixels incorrect after closing shared texture creator\n", bad);
            }
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
        }
    }
    hr = ID3D11Device_GetDeviceRemovedReason(device);
    ok(hr == S_OK, "Device removed reason %#lx\n", hr);
done:
    if (context) ID3D11DeviceContext_ClearState(context);
    if (target) ID3D11RenderTargetView_Release(target);
    if (staging) ID3D11Texture2D_Release(staging);
    if (opened) ID3D11Texture2D_Release(opened);
    if (resource) IDXGIResource_Release(resource);
    if (texture) ID3D11Texture2D_Release(texture);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    if (adapter) IDXGIAdapter1_Release(adapter);
    if (factory) IDXGIFactory1_Release(factory);
    if (d3d11) FreeLibrary(d3d11);
    if (dxgi) FreeLibrary(dxgi);
}
