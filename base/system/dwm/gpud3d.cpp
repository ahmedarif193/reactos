/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 shared-surface compositor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#include "gpud3d.h"
#include "gpud3dshader.h"
#include "gpugeometry.h"
#include "gpublurdamage.h"
#include "gpuscenecache.h"

extern "C" {
#include "presenttrace.h"
}

extern "C" DWORD_PTR NTAPI NtUserCallOneParam(DWORD_PTR Param, DWORD Routine);

/* Shared surfaces stay on the adapter that owns the output. GDI sections
 * supply newly painted pixels when the driver has no shareable GDI surface;
 * all composition, filtering and presentation still execute on that adapter. */
namespace
{
template<class T> void Release(T *&Object)
{
    if (Object != NULL)
    {
        Object->Release();
        Object = NULL;
    }
}

struct Texture
{
    ID3D11Texture2D *Resource;
    ID3D11ShaderResourceView *View;
    ID3D11RenderTargetView *Target;
    LONG Width, Height;
    DXGI_FORMAT Format;

    void Reset()
    {
        Release(Target);
        Release(View);
        Release(Resource);
        Width = Height = 0;
        Format = DXGI_FORMAT_UNKNOWN;
    }
};

struct Surface
{
    Texture Image;
    ID3D11Texture2D *SharedSource;
    ULONG SurfaceId, Share, Generation, LastFrame;
    ULONGLONG UpdateId;
    BOOL Client;

    void Reset()
    {
        Image.Reset();
        Release(SharedSource);
    }
};

struct ClientSource
{
    ID3D11Texture2D *Resource;
    ULONG SurfaceId, Share, LastFrame;

    void Reset()
    {
        Release(Resource);
        ZeroMemory(this, sizeof(*this));
    }
};

struct BlurTarget
{
    Texture Capture, Horizontal, Result;
    DWM_WIN Owner;
    RECT Bounds;
    ULONG Radius, Call, Frame;
    ULONGLONG LastUse;
    BOOL Valid;

    void Reset()
    {
        Capture.Reset();
        Horizontal.Reset();
        Result.Reset();
        Valid = FALSE;
    }
};

struct Constants
{
    FLOAT Rectangle[4], TargetSize[4], SourceSize[4], ClientRect[4], CaptureRect[4];
    FLOAT Brush[4], Colorization[4], ColorKey[4], Flags[4], Extra[4], Shadow[4], Filter[4];
    FLOAT Taps[33][4];
};

struct ConstantBuffer
{
    ID3D11Buffer *Resource;
    ConstantBuffer *Next;
};

enum Shader { Solid, Copy, Window, Filter, Shadow, ShaderCount };

struct Compositor
{
    HMODULE Runtime, Dxgi, Compiler, TraceProvider;
    HWND Window;
    BOOL Registered, Active, FrameValid, WorkPending;
    LONG Width, Height;
    char Renderer[160];
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    IDXGISwapChain1 *SwapChain;
    ID3D11VertexShader *VertexShader;
    ID3D11PixelShader *PixelShaders[ShaderCount];
    ConstantBuffer *ConstantsHead, *ConstantsTail, *NextConstants;
    ID3D11SamplerState *Sampler;
    ID3D11BlendState *Blend, *PremultipliedBlend;
    ID3D11RasterizerState *Rasterizer;
    ID3D11Query *Completion, *ClientCopies;
    /* ClientCopies fences the copies Begin makes of new client publications.
     * A copy made after that fence cannot be released early. */
    BOOL CopyingClients, ClientCopiesFenced, ClientCopiesUnfenced;
    Texture Canvas, Backdrop;
    Surface Surfaces[DWM_MAX_WINDOWS * 2];
    ClientSource ClientSources[DWM_MAX_WINDOWS * 2];
    BlurTarget Blurs[4];
    DWM_GPU_SCENE_CACHE Scene;
    BOOL LowerUnchanged[DWM_MAX_WINDOWS];
    DWM_WIN BlurOwner;
    BOOL BlurOwnerValid, BlurLowerUnchanged;
    ULONG BlurCall, Frame, PresentedBuffers;
    RECT Draw, PreviousCopy;
    ULONGLONG BlurUse, Filtered, Reused;
    /* An opaque client layer hides everything the frame draws beneath it.
     * Until that layer is drawn, canvas draws skip its rectangle. */
    RECT Occluder;
    ULONG OccluderIndex, WindowIndex;
    BOOL OccluderActive, SceneCurrent;
};

Compositor State;

BOOL Result(HRESULT Status, const char *Operation)
{
    if (SUCCEEDED(Status))
        return TRUE;
    char Message[192];
    _snprintf(Message, sizeof(Message), "DWM: Direct3D %s failed 0x%08lx\n", Operation, (ULONG)Status);
    Message[sizeof(Message) - 1] = 0;
    OutputDebugStringA(Message);
    return FALSE;
}

void UnbindTextures()
{
    ID3D11ShaderResourceView *Empty[2] = {NULL, NULL};
    State.Context->PSSetShaderResources(0, ARRAYSIZE(Empty), Empty);
    State.Context->OMSetRenderTargets(0, NULL, NULL);
}

BOOL CreateTexture(Texture &Image, LONG Width, LONG Height, BOOL RenderTarget,
                    const BYTE *Pixels = NULL, ULONG Pitch = 0,
                    DXGI_FORMAT Format = DXGI_FORMAT_B8G8R8A8_UNORM)
{
    Image.Reset();
    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = Desc.ArraySize = Desc.SampleDesc.Count = 1;
    Desc.Format = Format;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (RenderTarget ? D3D11_BIND_RENDER_TARGET : 0);
    D3D11_SUBRESOURCE_DATA Initial = {Pixels, Pitch, 0};
    if (!Result(State.Device->CreateTexture2D(&Desc, Pixels ? &Initial : NULL, &Image.Resource), "CreateTexture2D") ||
        !Result(State.Device->CreateShaderResourceView(Image.Resource, NULL, &Image.View), "CreateShaderResourceView") ||
        (RenderTarget && !Result(State.Device->CreateRenderTargetView(Image.Resource, NULL, &Image.Target), "CreateRenderTargetView")))
    {
        Image.Reset();
        return FALSE;
    }
    Image.Width = Width;
    Image.Height = Height;
    Image.Format = Format;
    return TRUE;
}

BOOL EnsureTexture(Texture &Image, LONG Width, LONG Height, BOOL RenderTarget,
                     DXGI_FORMAT Format = DXGI_FORMAT_B8G8R8A8_UNORM)
{
    if (Image.Resource != NULL && Image.Width == Width && Image.Height == Height && Image.Format == Format)
        return TRUE;
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_BO_CREATE);
    BOOL Created = CreateTexture(Image, Width, Height, RenderTarget, NULL, 0, Format);
    DptEnd(&g_DwmPresentTrace, Trace, Created, Created ? (ULONGLONG)Width * Height * 4 : 0);
    return Created;
}

/* Returns the parts of Clip outside the active occluder, at most four. */
ULONG SplitAroundOccluder(const Texture &Target, const RECT &Clip, RECT Parts[4])
{
    RECT Hidden;
    ULONG Count = 0;

    if (!State.OccluderActive || &Target != &State.Canvas ||
        !IntersectRect(&Hidden, &Clip, &State.Occluder))
    {
        Parts[0] = Clip;
        return 1;
    }
    if (Hidden.top > Clip.top)
        SetRect(&Parts[Count++], Clip.left, Clip.top, Clip.right, Hidden.top);
    if (Hidden.bottom < Clip.bottom)
        SetRect(&Parts[Count++], Clip.left, Hidden.bottom, Clip.right, Clip.bottom);
    if (Hidden.left > Clip.left)
        SetRect(&Parts[Count++], Clip.left, Hidden.top, Hidden.left, Hidden.bottom);
    if (Hidden.right < Clip.right)
        SetRect(&Parts[Count++], Hidden.right, Hidden.top, Clip.right, Hidden.bottom);
    return Count;
}

BOOL Draw(Texture &Target,
          Shader Program,
          const RECT &Clip,
          Constants &Data,
          ID3D11ShaderResourceView *Source = NULL,
          ID3D11ShaderResourceView *Backdrop = NULL,
          BOOL Blend = FALSE,
          BOOL Premultiplied = FALSE)
{
    if (Clip.left >= Clip.right || Clip.top >= Clip.bottom)
        return TRUE;
    RECT Parts[4];
    ULONG PartCount = SplitAroundOccluder(Target, Clip, Parts);
    if (PartCount == 0)
        return TRUE;
    Data.TargetSize[0] = (FLOAT)Target.Width;
    Data.TargetSize[1] = (FLOAT)Target.Height;
    ConstantBuffer *Upload = State.NextConstants;
    if (Upload != NULL)
    {
        State.NextConstants = Upload->Next;
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_CPU_UPLOAD);
        State.Context->UpdateSubresource(Upload->Resource, 0, NULL, &Data, 0, 0);
        DptEnd(&g_DwmPresentTrace, Trace, TRUE, sizeof(Data));
    }
    else
    {
        Upload = (ConstantBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Upload));
        if (Upload == NULL)
        {
            return FALSE;
        }
        D3D11_BUFFER_DESC Desc = {};
        Desc.ByteWidth = sizeof(Data);
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA Initial = {&Data, 0, 0};
        if (!Result(State.Device->CreateBuffer(&Desc, &Initial, &Upload->Resource), "CreateBuffer"))
        {
            HeapFree(GetProcessHeap(), 0, Upload);
            return FALSE;
        }
        if (State.ConstantsTail != NULL)
            State.ConstantsTail->Next = Upload;
        else
            State.ConstantsHead = Upload;
        State.ConstantsTail = Upload;
    }
    DPT_SCOPE DrawTrace = DptBegin(&g_DwmPresentTrace, DPT_SHARED_COMPOSE);
    D3D11_VIEWPORT Viewport = {0, 0, (FLOAT)Target.Width, (FLOAT)Target.Height, 0, 1};
    State.Context->RSSetViewports(1, &Viewport);
    State.Context->RSSetState(State.Rasterizer);
    State.Context->OMSetRenderTargets(1, &Target.Target, NULL);
    State.Context->OMSetBlendState(Premultiplied ? State.PremultipliedBlend : Blend ? State.Blend : NULL, NULL, ~0u);
    State.Context->IASetInputLayout(NULL);
    State.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    State.Context->VSSetShader(State.VertexShader, NULL, 0);
    State.Context->PSSetShader(State.PixelShaders[Program], NULL, 0);
    State.Context->VSSetConstantBuffers(0, 1, &Upload->Resource);
    State.Context->PSSetConstantBuffers(0, 1, &Upload->Resource);
    State.Context->PSSetSamplers(0, 1, &State.Sampler);
    ID3D11ShaderResourceView *Views[2] = {Source, Backdrop};
    State.Context->PSSetShaderResources(0, ARRAYSIZE(Views), Views);
    for (ULONG Index = 0; Index < PartCount; ++Index)
    {
        State.Context->RSSetScissorRects(1, &Parts[Index]);
        State.Context->Draw(4, 0);
    }
    State.WorkPending = TRUE;
    UnbindTextures();
    DptEnd(&g_DwmPresentTrace, DrawTrace, TRUE, 0);
    return TRUE;
}

BOOL FinishGpuReadsImpl(DWM_GPU_WAIT_TIMING *Timing)
{
    if (!State.WorkPending)
        return TRUE;

    ULONGLONG Tick = Timing ? DptNow() : 0;
    State.Context->End(State.Completion);
    if (Timing)
    {
        Timing->Ticks[DWM_WAIT_END] += DptNow() - Tick;
        Tick = DptNow();
    }
    State.Context->Flush();
    if (Timing)
        Timing->Ticks[DWM_WAIT_FLUSH] += DptNow() - Tick;
    DWORD Start = GetTickCount();
    BOOL Reported = FALSE;
    for (;;)
    {
        BOOL Complete = FALSE;
        if (Timing) Tick = DptNow();
        HRESULT Status = State.Context->GetData(State.Completion, &Complete, sizeof(Complete),
                                                D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (Timing)
        {
            Timing->Ticks[DWM_WAIT_QUERY] += DptNow() - Tick;
            ++Timing->Polls;
            Tick = DptNow();
        }
        HRESULT Removed = State.Device->GetDeviceRemovedReason();
        if (Timing)
            Timing->Ticks[DWM_WAIT_REMOVED] += DptNow() - Tick;
        if (FAILED(Removed))
        {
            State.WorkPending = FALSE;
            return Result(Removed, "GPU read retirement");
        }
        if (FAILED(Status))
        {
            State.WorkPending = FALSE;
            return Result(Status, "GPU completion query");
        }
        if (Status == S_OK && Complete)
        {
            State.WorkPending = FALSE;
            return TRUE;
        }
        if (!Reported && GetTickCount() - Start >= 5000)
        {
            OutputDebugStringA("DWM: retaining window publications until Direct3D GPU reads complete\n");
            Reported = TRUE;
        }
        /* A healthy device must complete the EVENT before its producers can
         * reuse published surfaces. A removed device has stopped executing
         * this command stream, so its resources are released and recreated. */
        if (Timing) Tick = DptNow();
        Sleep(1);
        if (Timing)
        {
            ULONGLONG Elapsed = DptNow() - Tick;
            Timing->Ticks[DWM_WAIT_SLEEP] += Elapsed;
            Timing->MaxSleep = max(Timing->MaxSleep, Elapsed);
            ++Timing->Sleeps;
        }
    }
}

BOOL FinishGpuReads()
{
    if (!State.WorkPending)
        return TRUE;
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_FENCE_WAIT);
    DWM_GPU_WAIT_TIMING Timing = {};
    BOOL Complete = FinishGpuReadsImpl(Trace.Epoch ? &Timing : NULL);
    DwmPresentTraceGpuWait(Trace, &Timing, Complete);
    DptEnd(&g_DwmPresentTrace, Trace, Complete, 0);
    return Complete;
}

void SetRectangle(Constants &Data, LONGLONG Left, LONGLONG Top, LONGLONG Right, LONGLONG Bottom)
{
    Data.Rectangle[0] = (FLOAT)Left;
    Data.Rectangle[1] = (FLOAT)Top;
    Data.Rectangle[2] = (FLOAT)Right;
    Data.Rectangle[3] = (FLOAT)Bottom;
}

void SetColor(FLOAT *Value, COLORREF Color, FLOAT Alpha = 1.0f)
{
    Value[0] = GetRValue(Color) / 255.0f;
    Value[1] = GetGValue(Color) / 255.0f;
    Value[2] = GetBValue(Color) / 255.0f;
    Value[3] = Alpha;
}

RECT ClipDraw(const RECT &Bounds)
{
    RECT Clip = {max(Bounds.left, State.Draw.left), max(Bounds.top, State.Draw.top),
                 min(Bounds.right, State.Draw.right), min(Bounds.bottom, State.Draw.bottom)};
    return Clip;
}

BOOL CreateShaders()
{
    typedef HRESULT (WINAPI *CompileProc)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
        ID3DInclude *, const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);
    CompileProc Compile = (CompileProc)GetProcAddress(State.Compiler, "D3DCompile");
    if (Compile == NULL)
        return FALSE;
    const char *Entries[] = {"QuadVS", "SolidPS", "CopyPS", "WindowPS", "FilterPS", "ShadowPS"};
    for (ULONG Index = 0; Index < ARRAYSIZE(Entries); ++Index)
    {
        ID3DBlob *Bytecode = NULL, *Errors = NULL;
        HRESULT Status = Compile(DwmD3dShaderSource, sizeof(DwmD3dShaderSource) - 1,
            "DwmCompositor", NULL, NULL, Entries[Index], Index == 0 ? "vs_4_0" : "ps_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &Bytecode, &Errors);
        if (FAILED(Status) && Errors != NULL)
            OutputDebugStringA((const char *)Errors->GetBufferPointer());
        Release(Errors);
        if (SUCCEEDED(Status))
        {
            if (Index == 0)
                Status = State.Device->CreateVertexShader(Bytecode->GetBufferPointer(), Bytecode->GetBufferSize(), NULL, &State.VertexShader);
            else
                Status = State.Device->CreatePixelShader(Bytecode->GetBufferPointer(), Bytecode->GetBufferSize(), NULL, &State.PixelShaders[Index - 1]);
        }
        Release(Bytecode);
        if (!Result(Status, Entries[Index]))
            return FALSE;
    }
    D3D11_SAMPLER_DESC Sampler = {};
    Sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    Sampler.MaxLOD = D3D11_FLOAT32_MAX;
    Sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (!Result(State.Device->CreateSamplerState(&Sampler, &State.Sampler), "CreateSamplerState"))
        return FALSE;
    D3D11_BLEND_DESC Blend = {};
    Blend.RenderTarget[0].BlendEnable = TRUE;
    Blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    Blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    Blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    Blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    Blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    Blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    Blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (!Result(State.Device->CreateBlendState(&Blend, &State.Blend), "CreateBlendState"))
        return FALSE;
    Blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    if (!Result(State.Device->CreateBlendState(&Blend, &State.PremultipliedBlend), "CreateBlendState(premultiplied)"))
        return FALSE;
    D3D11_RASTERIZER_DESC Rasterizer = {};
    Rasterizer.FillMode = D3D11_FILL_SOLID;
    Rasterizer.CullMode = D3D11_CULL_NONE;
    Rasterizer.DepthClipEnable = TRUE;
    Rasterizer.ScissorEnable = TRUE;
    if (!Result(State.Device->CreateRasterizerState(&Rasterizer, &State.Rasterizer), "CreateRasterizerState"))
        return FALSE;
    D3D11_QUERY_DESC Query = {D3D11_QUERY_EVENT, 0};
    return Result(State.Device->CreateQuery(&Query, &State.Completion), "CreateQuery") &&
           Result(State.Device->CreateQuery(&Query, &State.ClientCopies), "CreateQuery client copies");
}

BOOL HasNativeDriver(const LUID &Luid)
{
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    Open.AdapterLuid = Luid;
    if (D3DKMTOpenAdapterFromLuid(&Open) < 0)
        return FALSE;
    D3DKMT_UMDFILENAMEINFO Name = {};
    Name.Version = KMTUMDVERSION_DX11;
    D3DKMT_QUERYADAPTERINFO Query = {};
    Query.hAdapter = Open.hAdapter;
    Query.Type = KMTQAITYPE_UMDRIVERNAME;
    Query.pPrivateDriverData = &Name;
    Query.PrivateDriverDataSize = sizeof(Name);
    NTSTATUS Status = D3DKMTQueryAdapterInfo(&Query);
    D3DKMT_CLOSEADAPTER Close = {};
    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    return Status >= 0 && Name.UmdFileName[0] != 0;
}

void SetNativeTraceProvider(IDXGIAdapter1 *Adapter)
{
    DXGI_ADAPTER_DESC1 Desc = {};
    if (FAILED(Adapter->GetDesc1(&Desc)))
        return;
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    Open.AdapterLuid = Desc.AdapterLuid;
    if (D3DKMTOpenAdapterFromLuid(&Open) < 0)
        return;
    D3DKMT_UMDFILENAMEINFO Name = {};
    Name.Version = KMTUMDVERSION_DX11;
    D3DKMT_QUERYADAPTERINFO Query = {};
    Query.hAdapter = Open.hAdapter;
    Query.Type = KMTQAITYPE_UMDRIVERNAME;
    Query.pPrivateDriverData = &Name;
    Query.PrivateDriverDataSize = sizeof(Name);
    NTSTATUS Status = D3DKMTQueryAdapterInfo(&Query);
    D3DKMT_CLOSEADAPTER Close = {};
    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    if (Status < 0 || Name.UmdFileName[0] == 0 || Name.UmdFileName[ARRAYSIZE(Name.UmdFileName) - 1] != 0)
        return;
    HMODULE Provider = NULL;
    if (!GetModuleHandleExW(0, Name.UmdFileName, &Provider))
        return;
    PFNWGLCONTROLPRESENTATIONTRACEROS Control = (PFNWGLCONTROLPRESENTATIONTRACEROS)
        GetProcAddress(Provider, "MesaControlPresentationTraceROS");
    if (Control == NULL)
    {
        FreeLibrary(Provider);
        return;
    }
    State.TraceProvider = Provider;
    DwmPresentTraceSetIcd(Control);
}

BOOL AnyAdapterHasNativeDriver()
{
    D3DKMT_ENUMADAPTERS2 Enumeration = {};
    if (D3DKMTEnumAdapters2(&Enumeration) < 0 || Enumeration.NumAdapters == 0)
        return FALSE;
    const ULONG Capacity = Enumeration.NumAdapters;
    D3DKMT_ADAPTERINFO *Adapters = static_cast<D3DKMT_ADAPTERINFO *>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Capacity * sizeof(*Adapters)));
    if (Adapters == NULL)
        return FALSE;
    Enumeration.pAdapters = Adapters;
    BOOL Found = FALSE;
    if (D3DKMTEnumAdapters2(&Enumeration) >= 0)
    {
        for (ULONG Index = 0; Index < Enumeration.NumAdapters && Index < Capacity; ++Index)
        {
            if (Adapters[Index].hAdapter == 0)
                continue;
            if (!Found && HasNativeDriver(Adapters[Index].AdapterLuid))
                Found = TRUE;
            D3DKMT_CLOSEADAPTER Close = {};
            Close.hAdapter = Adapters[Index].hAdapter;
            D3DKMTCloseAdapter(&Close);
        }
    }
    HeapFree(GetProcessHeap(), 0, Adapters);
    return Found;
}

BOOL CheckSharedSurfaceSupport()
{
    /* An adapter can have a native UMD registered even when D3D11CreateDevice
     * falls back to a runtime that cannot import window publications. Check
     * the complete legacy shared-handle path before enabling composition;
     * otherwise every frame fails and recreates the same unusable device. */
    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Desc.Height = 1;
    Desc.MipLevels = Desc.ArraySize = Desc.SampleDesc.Count = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    ID3D11Texture2D *Texture = NULL, *Opened = NULL;
    IDXGIResource *Resource = NULL;
    HANDLE Share = NULL;
    HRESULT Status = State.Device->CreateTexture2D(&Desc, NULL, &Texture);
    if (SUCCEEDED(Status))
        Status = Texture->QueryInterface(IID_IDXGIResource, (void **)&Resource);
    if (SUCCEEDED(Status))
        Status = Resource->GetSharedHandle(&Share);
    if (SUCCEEDED(Status))
        Status = Share ? State.Device->OpenSharedResource(Share, IID_ID3D11Texture2D,
            (void **)&Opened) : E_FAIL;
    if (SUCCEEDED(Status))
    {
        D3D11_TEXTURE2D_DESC Actual = {};
        Opened->GetDesc(&Actual);
        if (Actual.Width != Desc.Width || Actual.Height != Desc.Height ||
            Actual.MipLevels != 1 || Actual.ArraySize != 1 ||
            Actual.SampleDesc.Count != 1 || Actual.Format != Desc.Format ||
            !(Actual.BindFlags & D3D11_BIND_SHADER_RESOURCE))
            Status = DXGI_ERROR_UNSUPPORTED;
    }
    /* A legacy DXGI shared handle belongs to the resource, not CloseHandle. */
    Release(Opened);
    Release(Resource);
    Release(Texture);
    return Result(Status, "shared-surface capability check");
}

BOOL CreateDevice(IDXGIAdapter1 **Selected)
{
    typedef HRESULT (WINAPI *CreateFactoryProc)(REFIID, void **);
    typedef HRESULT (WINAPI *CreateDeviceProc)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    CreateFactoryProc CreateFactory = (CreateFactoryProc)GetProcAddress(State.Dxgi, "CreateDXGIFactory1");
    CreateDeviceProc Create = (CreateDeviceProc)GetProcAddress(State.Runtime, "D3D11CreateDevice");
    if (CreateFactory == NULL || Create == NULL)
        return FALSE;
    IDXGIFactory1 *Factory = NULL;
    if (!Result(CreateFactory(IID_IDXGIFactory1, (void **)&Factory), "CreateDXGIFactory1"))
        return FALSE;
    const HMONITOR Monitor = MonitorFromWindow(State.Window, MONITOR_DEFAULTTOPRIMARY);
    BOOL Success = FALSE;
    for (ULONG Index = 0; !Success; ++Index)
    {
        IDXGIAdapter1 *Adapter = NULL;
        HRESULT Status = Factory->EnumAdapters1(Index, &Adapter);
        if (Status == DXGI_ERROR_NOT_FOUND)
            break;
        if (!Result(Status, "EnumAdapters1"))
            break;
        DXGI_ADAPTER_DESC1 Desc = {};
        Status = Adapter->GetDesc1(&Desc);
        BOOL OwnsOutput = FALSE;
        if (SUCCEEDED(Status) && Desc.VendorId != 0 && Desc.VendorId != 0x1414 &&
            !(Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && HasNativeDriver(Desc.AdapterLuid))
        {
            for (ULONG OutputIndex = 0; ; ++OutputIndex)
            {
                IDXGIOutput *Output = NULL;
                if (Adapter->EnumOutputs(OutputIndex, &Output) != S_OK)
                    break;
                DXGI_OUTPUT_DESC OutputDesc = {};
                if (SUCCEEDED(Output->GetDesc(&OutputDesc)) && OutputDesc.AttachedToDesktop && OutputDesc.Monitor == Monitor)
                    OwnsOutput = TRUE;
                Release(Output);
                if (OwnsOutput)
                    break;
            }
        }
        if (OwnsOutput)
        {
            const D3D_FEATURE_LEVEL Levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
            D3D_FEATURE_LEVEL Obtained;
            Status = Create(Adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                Levels, ARRAYSIZE(Levels), D3D11_SDK_VERSION, &State.Device, &Obtained, &State.Context);
            Success = Result(Status, "D3D11CreateDevice");
            if (Success)
            {
                IDXGIDevice *DxgiDevice = NULL;
                IDXGIAdapter *ActualAdapter = NULL;
                DXGI_ADAPTER_DESC Actual = {};
                Success = Result(State.Device->QueryInterface(IID_IDXGIDevice, (void **)&DxgiDevice), "Get DXGI device") &&
                    Result(DxgiDevice->GetAdapter(&ActualAdapter), "Get device adapter") &&
                    Result(ActualAdapter->GetDesc(&Actual), "Get device adapter description") &&
                    Actual.VendorId == Desc.VendorId && Actual.DeviceId == Desc.DeviceId &&
                    Actual.AdapterLuid.HighPart == Desc.AdapterLuid.HighPart &&
                    Actual.AdapterLuid.LowPart == Desc.AdapterLuid.LowPart;
                Release(ActualAdapter);
                Release(DxgiDevice);
                if (Success)
                    Success = CheckSharedSurfaceSupport();
                if (!Success)
                {
                    Release(State.Context);
                    Release(State.Device);
                }
            }
            if (Success)
            {
                *Selected = Adapter;
                WideCharToMultiByte(CP_UTF8, 0, Desc.Description, -1, State.Renderer, sizeof(State.Renderer), NULL, NULL);
                State.Renderer[sizeof(State.Renderer) - 1] = 0;
                IDXGIDevice1 *DxgiDevice = NULL;
                if (SUCCEEDED(State.Device->QueryInterface(IID_IDXGIDevice1, (void **)&DxgiDevice)))
                {
                    DxgiDevice->SetMaximumFrameLatency(1);
                    Release(DxgiDevice);
                }
            }
        }
        if (!Success)
            Release(Adapter);
    }
    Release(Factory);
    return Success;
}

BOOL CreateSwapChain(IDXGIAdapter1 *Adapter)
{
    IDXGIFactory2 *Factory = NULL;
    if (!Result(Adapter->GetParent(IID_IDXGIFactory2, (void **)&Factory), "GetFactory2"))
        return FALSE;
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    Desc.Width = State.Width;
    Desc.Height = State.Height;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.Scaling = DXGI_SCALING_STRETCH;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    Desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT Status = Factory->CreateSwapChainForHwnd(State.Device, State.Window, &Desc, NULL, NULL, &State.SwapChain);
    if (SUCCEEDED(Status))
        Factory->MakeWindowAssociation(State.Window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    Release(Factory);
    return Result(Status, "CreateSwapChainForHwnd");
}

Surface *FindSurface(const DWM_WIN *Window, BOOL Client)
{
    Surface *Available = NULL, *Oldest = &State.Surfaces[0];
    for (ULONG Index = 0; Index < ARRAYSIZE(State.Surfaces); ++Index)
    {
        Surface *Slot = &State.Surfaces[Index];
        if (Slot->Image.Resource != NULL && Slot->SurfaceId == Window->SurfaceId && Slot->Client == Client)
            return Slot;
        if (Slot->Image.Resource == NULL && Available == NULL)
            Available = Slot;
        if (Slot->LastFrame < Oldest->LastFrame)
            Oldest = Slot;
    }
    if (Available != NULL)
        return Available;
    Oldest->Reset();
    ZeroMemory(Oldest, sizeof(*Oldest));
    return Oldest;
}

BOOL SurfaceCurrent(const Surface *Slot, const DWM_WIN *Window)
{
    return Slot->Image.Resource != NULL && Slot->Share == 0 &&
           Slot->Generation == Window->Generation &&
           Slot->Image.Width == Window->cx && Slot->Image.Height == Window->cy;
}

Texture *UploadGdi(const DWM_WIN *Window, const BYTE *Pixels)
{
    if (Window->Generation == 0 || Window->BaseUpdateId == 0 ||
        Window->cx <= 0 || Window->cy <= 0 ||
        Window->BaseWidth != (ULONG)Window->cx || Window->BaseHeight != (ULONG)Window->cy ||
        Window->BaseFormat != DWM_DX_FORMAT_B8G8R8A8_UNORM ||
        Window->Stride != Window->BasePitch || Window->Stride > MAXLONG ||
        (ULONGLONG)Window->cx * sizeof(ULONG) > Window->Stride ||
        (ULONGLONG)Window->Stride * Window->cy > MAXULONG_PTR)
        return NULL;

    Surface *Slot = FindSurface(Window, FALSE);
    BOOL Current = SurfaceCurrent(Slot, Window);
    if (Current && Slot->UpdateId == Window->BaseUpdateId)
    {
        Slot->LastFrame = State.Frame;
        return &Slot->Image;
    }
    if (Pixels == NULL)
        return NULL;

    RECT Dirty = {0, 0, Window->cx, Window->cy};
    if (Current && Slot->UpdateId == Window->BasePreviousUpdateId)
    {
        const RECTL *Bounds = &Window->BaseDirtyRect;
        if (Bounds->left < 0 || Bounds->top < 0 || Bounds->right > Window->cx ||
            Bounds->bottom > Window->cy || Bounds->left >= Bounds->right || Bounds->top >= Bounds->bottom)
            return NULL;
        SetRect(&Dirty, Bounds->left, Bounds->top, Bounds->right, Bounds->bottom);
    }
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_CPU_UPLOAD);
    BOOL Success;
    if (!Current)
    {
        Success = CreateTexture(Slot->Image, Window->cx, Window->cy, FALSE, Pixels, Window->Stride);
    }
    else
    {
        D3D11_BOX Box = {(UINT)Dirty.left, (UINT)Dirty.top, 0, (UINT)Dirty.right, (UINT)Dirty.bottom, 1};
        const BYTE *Source = Pixels + (SIZE_T)Dirty.top * Window->Stride + (SIZE_T)Dirty.left * sizeof(ULONG);
        State.Context->UpdateSubresource(Slot->Image.Resource, 0, &Box, Source, Window->Stride, 0);
        Success = Result(State.Device->GetDeviceRemovedReason(), "GDI texture update");
    }
    DptEnd(&g_DwmPresentTrace, Trace, Success,
        Success ? (ULONGLONG)(Dirty.right - Dirty.left) * (Dirty.bottom - Dirty.top) * sizeof(ULONG) : 0);
    if (!Success)
        return NULL;
    Slot->SurfaceId = Window->SurfaceId;
    Slot->Client = FALSE;
    Slot->Share = 0;
    Slot->Generation = Window->Generation;
    Slot->UpdateId = Window->BaseUpdateId;
    Slot->LastFrame = State.Frame;
    return &Slot->Image;
}

ClientSource *ImportClientSource(const DWM_WIN *Window)
{
    if (Window->Generation == 0)
        return NULL;
    ClientSource *Found = NULL, *Available = NULL, *Oldest = NULL, *OwnerOldest = NULL;
    ULONG OwnerCount = 0;
    for (ULONG Index = 0; Index < ARRAYSIZE(State.ClientSources); ++Index)
    {
        ClientSource *Source = &State.ClientSources[Index];
        if (Source->Resource == NULL)
        {
            if (Available == NULL)
                Available = Source;
        }
        else
        {
            if (Source->SurfaceId == Window->SurfaceId)
            {
                ++OwnerCount;
                if (Source->Share == Window->DxGlobalShare)
                {
                    Found = Source;
                    break;
                }
                if (Source->LastFrame != State.Frame &&
                    (OwnerOldest == NULL || State.Frame - Source->LastFrame > State.Frame - OwnerOldest->LastFrame))
                    OwnerOldest = Source;
            }
            if (Source->LastFrame != State.Frame &&
                (Oldest == NULL || State.Frame - Source->LastFrame > State.Frame - Oldest->LastFrame))
                Oldest = Source;
        }
    }
    /* DXGI has at most 16 swapchain buffers. Also bound same-size replacement
     * history when ResizeBuffers leaves the window's GDI generation intact. */
    ClientSource *Destination = Found ? Found : OwnerCount >= 16 ? OwnerOldest : Available ? Available : Oldest;
    if (Destination == NULL)
        return NULL;

    ID3D11Texture2D *Resource = Found ? Found->Resource : NULL;
    if (Resource == NULL && !Result(State.Device->OpenSharedResource((HANDLE)(ULONG_PTR)Window->DxGlobalShare,
        IID_ID3D11Texture2D, (void **)&Resource), "OpenSharedResource client"))
        return NULL;
    D3D11_TEXTURE2D_DESC Desc;
    Resource->GetDesc(&Desc);
    if (Desc.Width != Window->DxWidth || Desc.Height != Window->DxHeight ||
        Desc.MipLevels != 1 || Desc.ArraySize != 1 || Desc.SampleDesc.Count != 1 ||
        !(Desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || Desc.Format != (DXGI_FORMAT)Window->DxFormat)
    {
        if (Found == NULL)
            Release(Resource);
        return NULL;
    }
    if (Found == NULL)
    {
        /* Holding an opened resource retains its shared handle and backing
         * allocation. DxGeneration identifies a publication registration and
         * changes when a swapchain selects another buffer; it is checked by
         * Import before copying, rather than reopening this same allocation.
         * Never evict an import read by the current frame before its EVENT. */
        Destination->Reset();
        Destination->Resource = Resource;
        Destination->SurfaceId = Window->SurfaceId;
        Destination->Share = Window->DxGlobalShare;
    }
    Destination->LastFrame = State.Frame;
    return Destination;
}

void PruneClientSources(const DWM_WIN *Windows, ULONG Count)
{
    if (State.WorkPending)
        return;
    for (ULONG Index = 0; Index < ARRAYSIZE(State.ClientSources); ++Index)
    {
        ClientSource *Source = &State.ClientSources[Index];
        if (Source->Resource == NULL)
            continue;
        D3D11_TEXTURE2D_DESC Desc;
        Source->Resource->GetDesc(&Desc);
        BOOL Present = FALSE;
        for (ULONG WindowIndex = 0; WindowIndex < Count; ++WindowIndex)
        {
            const DWM_WIN *Window = &Windows[WindowIndex];
            /* A GDI backing resize does not retire the client publication. */
            if (Window->SurfaceId == Source->SurfaceId &&
                Window->DxGlobalShare != 0 && Window->DxGeneration != 0 && Window->DxUpdateId != 0 &&
                Desc.Width == Window->DxWidth && Desc.Height == Window->DxHeight &&
                Desc.Format == (DXGI_FORMAT)Window->DxFormat)
            {
                Present = TRUE;
                break;
            }
        }
        if (!Present)
            Source->Reset();
    }
}

Texture *Import(const DWM_WIN *Window, BOOL Client)
{
    ULONG Share = Client ? Window->DxGlobalShare : Window->BaseGlobalShare;
    ULONG Generation = Client ? Window->DxGeneration : Window->BaseGeneration;
    ULONG Width = Client ? Window->DxWidth : Window->BaseWidth;
    ULONG Height = Client ? Window->DxHeight : Window->BaseHeight;
    ULONG Format = Client ? Window->DxFormat : Window->BaseFormat;
    if (Share == 0 || Generation == 0 || Width == 0 || Height == 0 ||
        Width > MAXLONG || Height > MAXLONG ||
        (Format != DWM_DX_FORMAT_B8G8R8A8_UNORM && (!Client || Format != DWM_DX_FORMAT_R8G8B8A8_UNORM)))
        return NULL;
    if (!Client && (Width != (ULONG)Window->cx || Height != (ULONG)Window->cy))
        return NULL;
    Surface *Slot = FindSurface(Window, Client);
    if (Client)
    {
        if (Window->DxUpdateId == 0)
            return NULL;
        ClientSource *Source = ImportClientSource(Window);
        if (Source == NULL)
            return NULL;
        if (Slot->Share != Share || Slot->Generation != Generation || Slot->UpdateId != Window->DxUpdateId)
        {
            if (!EnsureTexture(Slot->Image, Width, Height, FALSE, (DXGI_FORMAT)Format))
                return NULL;
            /* A consumed producer may immediately render into this buffer
             * again. Retain a GPU-owned snapshot so unrelated desktop damage
             * keeps drawing the last published pixels until its next update.
             * GDI front/back generation changes do not publish client pixels. */
            UnbindTextures();
            State.Context->CopyResource(Slot->Image.Resource, Source->Resource);
            State.WorkPending = TRUE;
            State.ClientCopiesUnfenced |= !State.CopyingClients;
            if (!Result(State.Device->GetDeviceRemovedReason(), "Client texture snapshot"))
                return NULL;
            Slot->Share = Share;
            Slot->Generation = Generation;
            Slot->SurfaceId = Window->SurfaceId;
            Slot->Client = TRUE;
            Slot->UpdateId = Window->DxUpdateId;
        }
        Slot->LastFrame = State.Frame;
        return &Slot->Image;
    }
    if (Window->BaseUpdateId == 0)
        return NULL;
    if (Slot->SharedSource == NULL || Slot->Share != Share || Slot->Generation != Generation ||
        Slot->Image.Width != (LONG)Width || Slot->Image.Height != (LONG)Height)
    {
        Slot->Reset();
        if (!Result(State.Device->OpenSharedResource((HANDLE)(ULONG_PTR)Share,
            IID_ID3D11Texture2D, (void **)&Slot->SharedSource), "OpenSharedResource"))
            return NULL;
        D3D11_TEXTURE2D_DESC Desc;
        Slot->SharedSource->GetDesc(&Desc);
        if (Desc.Width != Width || Desc.Height != Height || Desc.MipLevels != 1 || Desc.ArraySize != 1 ||
            Desc.SampleDesc.Count != 1 || !(Desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) ||
            (Desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && Desc.Format != DXGI_FORMAT_B8G8R8X8_UNORM))
        {
            Slot->Reset();
            return NULL;
        }
        if (!CreateTexture(Slot->Image, Width, Height, FALSE, NULL, 0, Desc.Format))
        {
            Slot->Reset();
            return NULL;
        }
        Slot->SurfaceId = Window->SurfaceId;
        Slot->Client = Client;
        Slot->Share = Share;
        Slot->Generation = Generation;
        Slot->UpdateId = 0;
    }
    if (Slot->UpdateId != Window->BaseUpdateId)
    {
        /* Keep unchanged GDI publications in a GPU-owned texture. */
        UnbindTextures();
        State.Context->CopyResource(Slot->Image.Resource, Slot->SharedSource);
        State.WorkPending = TRUE;
        if (!Result(State.Device->GetDeviceRemovedReason(), "GDI texture snapshot"))
            return NULL;
        Slot->UpdateId = Window->BaseUpdateId;
    }
    Slot->LastFrame = State.Frame;
    return &Slot->Image;
}

void BuildWeights(ULONG Radius, Constants &Data)
{
    FLOAT Weights[65];
    Radius = min(Radius, 64u);
    const double Sigma = max(Radius / 3.0, 0.5);
    double Sum = 0;
    for (ULONG Index = 0; Index <= Radius; ++Index)
    {
        Weights[Index] = (FLOAT)exp(-(double)Index * Index / (2 * Sigma * Sigma));
        Sum += Weights[Index] * (Index ? 2 : 1);
    }
    Data.Taps[0][0] = (FLOAT)(Weights[0] / Sum);
    ULONG Count = 1;
    for (ULONG Index = 1; Index <= Radius; Index += 2)
    {
        FLOAT Weight = Weights[Index] + (Index < Radius ? Weights[Index + 1] : 0);
        Data.Taps[Count][0] = (FLOAT)(Weight / Sum);
        Data.Taps[Count][1] = Index + (Index < Radius ? Weights[Index + 1] / Weight : 0);
        ++Count;
    }
    Data.Filter[2] = (FLOAT)Count;
}

BlurTarget *FilterCaptureImpl(const RECT &Bounds, ULONG Radius)
{
    ULONG Call = State.BlurCall++;
    BlurTarget *Oldest = &State.Blurs[0], *Reusable = NULL;
    for (ULONG Index = 0; Index < ARRAYSIZE(State.Blurs); ++Index)
    {
        BlurTarget *Target = &State.Blurs[Index];
        if (State.BlurOwnerValid && State.BlurLowerUnchanged && Target->Valid &&
            Target->Frame + 1 == State.Frame && Target->Call == Call && Target->Radius == Radius &&
            EqualRect(&Target->Bounds, &Bounds) && memcmp(&Target->Owner, &State.BlurOwner, sizeof(Target->Owner)) == 0)
        {
            Target->Frame = State.Frame;
            Target->LastUse = ++State.BlurUse;
            ++State.Reused;
            return Target;
        }
        if (Target->Frame != State.Frame && Target->Call == Call &&
            Target->Owner.SurfaceId == State.BlurOwner.SurfaceId &&
            (Reusable == NULL || Target->LastUse > Reusable->LastUse))
            Reusable = Target;
        if (Target->LastUse < Oldest->LastUse)
            Oldest = Target;
    }
    /* Invalid pixels can still use the owner's previous texture storage. */
    BlurTarget *Target = Reusable ? Reusable : Oldest;
    Target->Valid = FALSE;
    Target->Owner = State.BlurOwner;
    LONG Width = Bounds.right - Bounds.left, Height = Bounds.bottom - Bounds.top;
    ULONG Scale = Radius >= 12 ? 2 : 1;
    LONG FilterWidth = (Width + Scale - 1) / Scale, FilterHeight = (Height + Scale - 1) / Scale;
    if (!EnsureTexture(Target->Capture, Width, Height, FALSE) ||
        !EnsureTexture(Target->Horizontal, FilterWidth, FilterHeight, TRUE) ||
        !EnsureTexture(Target->Result, FilterWidth, FilterHeight, TRUE))
        return NULL;
    UnbindTextures();
    D3D11_BOX Box = {(UINT)Bounds.left, (UINT)Bounds.top, 0, (UINT)Bounds.right, (UINT)Bounds.bottom, 1};
    DPT_SCOPE CaptureTrace = DptBegin(&g_DwmPresentTrace, DPT_BLIT);
    State.Context->CopySubresourceRegion(Target->Capture.Resource, 0, 0, 0, 0, State.Canvas.Resource, 0, &Box);
    DptEnd(&g_DwmPresentTrace, CaptureTrace, TRUE, 0);
    Constants Data = {};
    SetRectangle(Data, 0, 0, FilterWidth, FilterHeight);
    RECT Clip = {0, 0, FilterWidth, FilterHeight};
    BuildWeights((Radius + Scale - 1) / Scale, Data);
    Data.Filter[0] = 1.0f / FilterWidth;
    if (!Draw(Target->Horizontal, Filter, Clip, Data, Target->Capture.View))
        return NULL;
    Data.Filter[0] = 0;
    Data.Filter[1] = 1.0f / FilterHeight;
    if (!Draw(Target->Result, Filter, Clip, Data, Target->Horizontal.View))
        return NULL;
    Target->Bounds = Bounds;
    Target->Radius = Radius;
    Target->Call = Call;
    Target->Frame = State.Frame;
    Target->LastUse = ++State.BlurUse;
    Target->Valid = State.BlurOwnerValid;
    ++State.Filtered;
    return Target;
}

BlurTarget *FilterCapture(const RECT &Bounds, ULONG Radius)
{
    DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_BLUR_FILTER);
    BlurTarget *Target = FilterCaptureImpl(Bounds, Radius);
    DptEnd(&g_DwmPresentTrace, Trace, Target != NULL, 0);
    return Target;
}

BOOL DrawLayer(const DWM_WIN *Window, const BYTE *Pixels, BOOL Client, LONG OriginX, LONG OriginY,
                 Texture *Prepared = NULL)
{
    DWM_GPU_WINDOW_GEOMETRY Geometry;
    if (!DwmGpuWindowGeometry(Window, OriginX, OriginY, &Geometry))
        return TRUE;
    if (Client)
    {
        DWM_GPU_WINDOW_GEOMETRY Owner = Geometry;
        if (!DwmGpuClientGeometry(Window, &Owner, &Geometry))
            return TRUE;
    }
    RECT Bounds;
    if (!DwmGpuDamageBounds(&Bounds, State.Width, State.Height, Geometry.Left, Geometry.Top,
        Geometry.Left + Geometry.Width, Geometry.Top + Geometry.Height) ||
        !DwmGpuDamageIntersects(&Bounds, &State.Draw))
        return TRUE;
    FLOAT Alpha = (Window->LayerFlags & DWM_LWA_ALPHA) ? min(Window->Alpha, 255u) / 255.0f : 1.0f;
    if (Alpha == 0)
        return TRUE;
    Texture *Image = Prepared;
    if (Image == NULL)
    {
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_TEXTURE);
        Image = !Client && Window->BaseGlobalShare == 0 ? UploadGdi(Window, Pixels) : Import(Window, Client);
        DptEnd(&g_DwmPresentTrace, Trace, Image != NULL, 0);
    }
    if (Image == NULL)
        return FALSE;
    BOOL Glass = Window->BackdropType == DWM_BACKDROP_TRANSIENT && Window->BackdropRegion != 0 &&
        Window->BackdropOpacity < 255 && (!Client || Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW);
    RECT Capture = {};
    BlurTarget *Blur = NULL;
    if (Glass)
    {
        ULONG Radius = DwmGpuMaterialBlurRadius(Window);
        if (!DwmGpuDamageBounds(&Capture, State.Width, State.Height, Geometry.Left - Radius, Geometry.Top - Radius,
            Geometry.Left + Geometry.Width + Radius, Geometry.Top + Geometry.Height + Radius) ||
            (Blur = FilterCapture(Capture, Radius)) == NULL)
            return FALSE;
    }
    Constants Data = {};
    SetRectangle(Data, Geometry.Left, Geometry.Top, Geometry.Left + Geometry.Width, Geometry.Top + Geometry.Height);
    Data.SourceSize[0] = Client ? Window->DxWidth : Window->cx;
    Data.SourceSize[1] = Client ? Window->DxHeight : Window->cy;
    Data.SourceSize[2] = Alpha;
    Data.SourceSize[3] = Client ? 0 : min(Window->CornerRadius, (ULONG)min(Window->cx / 2, Window->cy / 2));
    Data.ClientRect[0] = (FLOAT)min((LONGLONG)Window->ClientX + Window->BackdropNcExtendLeft, (LONGLONG)Window->ClientX + Window->ClientWidth);
    Data.ClientRect[1] = (FLOAT)min((LONGLONG)Window->ClientY + Window->BackdropNcExtend, (LONGLONG)Window->ClientY + Window->ClientHeight);
    Data.ClientRect[2] = (FLOAT)((LONGLONG)Window->ClientX + Window->ClientWidth);
    Data.ClientRect[3] = (FLOAT)((LONGLONG)Window->ClientY + Window->ClientHeight);
    Data.CaptureRect[0] = (FLOAT)Capture.left;
    Data.CaptureRect[1] = (FLOAT)Capture.top;
    Data.CaptureRect[2] = (FLOAT)(Capture.right - Capture.left);
    Data.CaptureRect[3] = (FLOAT)(Capture.bottom - Capture.top);
    SetColor(Data.Brush, Window->BackdropColor);
    SetColor(Data.Colorization, Window->BackdropColorization);
    SetColor(Data.ColorKey, Window->ColorKey);
    Data.Flags[0] = (FLOAT)Glass;
    Data.Flags[1] = Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW;
    BOOL Premultiplied = !!(Window->LayerFlags & DWM_WINDOW_PREMULTIPLIED_ALPHA) ||
        (Client && !!(Window->LayerFlags & DWM_WINDOW_DX_PREMULTIPLIED_ALPHA));
    Data.Flags[2] = !!((Window->BlurFlags & DWM_BLUR_ENABLE) || Premultiplied);
    Data.Flags[3] = !!(Window->LayerFlags & DWM_LWA_COLORKEY);
    Data.Extra[0] = min(Window->BackdropOpacity, 255u) / 255.0f;
    Data.Extra[1] = (100.0f + DWM_MATERIAL_SATURATION) / 100.0f;
    Data.Extra[2] = DWM_MATERIAL_REFLECT_STRENGTH / 255.0f;
    Data.Extra[3] = (FLOAT)Premultiplied;
    BOOL Blend = Alpha < 1.0f || Data.SourceSize[3] != 0 || Data.Flags[2] != 0 || Premultiplied;
    return Draw(State.Canvas,
                Shader::Window,
                ClipDraw(Bounds),
                Data,
                Image->View,
                Blur ? Blur->Result.View : NULL,
                Blend,
                Premultiplied);
}
} // namespace

BOOL
DwmD3dInitialize(LONG Width, LONG Height)
{
    if (State.Active)
        return TRUE;
    if (Width <= 0 || Height <= 0)
        return FALSE;
    if (!AnyAdapterHasNativeDriver())
        return FALSE;
    State.Width = Width;
    State.Height = Height;
    WNDCLASSEXW Class = {};
    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"DwmD3dCompositor";
    if (!RegisterClassExW(&Class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    State.Window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        Class.lpszClassName, L"", WS_POPUP, 0, 0, Width, Height, NULL, NULL, Class.hInstance, NULL);
    if (State.Window == NULL)
        return FALSE;
    DWM_GPU_OUTPUT Output = {};
    Output.StructSize = sizeof(Output);
    Output.Window = (ULONGLONG)(ULONG_PTR)State.Window;
    Output.Width = Width;
    Output.Height = Height;
    if (!SetPropW(State.Window, DWM_PROP_GPU_OUTPUT, (HANDLE)1) ||
        (LONG)NtUserCallOneParam((DWORD_PTR)&Output, DWM_ROUTINE_SETGPUOUTPUT) < 0)
    {
        DwmD3dShutdown();
        return FALSE;
    }
    State.Registered = TRUE;
    if (!SetWindowPos(State.Window, HWND_TOPMOST, 0, 0, Width, Height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW))
    {
        DwmD3dShutdown();
        return FALSE;
    }
    if (!State.Runtime) State.Runtime = LoadLibraryW(L"d3d11.dll");
    if (!State.Dxgi) State.Dxgi = LoadLibraryW(L"dxgi.dll");
    if (!State.Compiler) State.Compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    IDXGIAdapter1 *Adapter = NULL;
    BOOL Success = State.Runtime && State.Dxgi && State.Compiler && CreateDevice(&Adapter) &&
        CreateSwapChain(Adapter) && CreateShaders() && CreateTexture(State.Canvas, Width, Height, TRUE);
    if (Success)
        SetNativeTraceProvider(Adapter);
    Release(Adapter);
    if (!Success)
    {
        DwmD3dShutdown();
        return FALSE;
    }
    State.Active = TRUE;
    char Message[224];
    _snprintf(Message, sizeof(Message), "DWM: Direct3D compositor initialized on %s, %ldx%ld\n", State.Renderer, Width, Height);
    Message[sizeof(Message) - 1] = 0;
    OutputDebugStringA(Message);
    return TRUE;
}

BOOL DwmD3dIsActive(void) { return State.Active; }
const char *DwmD3dRendererName(void) { return State.Active ? State.Renderer : NULL; }

void
DwmD3dShutdown(void)
{
    if (State.TraceProvider)
        DwmPresentTraceSetIcd(NULL);
    if (State.Context != NULL)
    {
        FinishGpuReads();
        State.Context->ClearState();
        State.Context->Flush();
    }
    if (State.Registered)
    {
        DWM_GPU_OUTPUT Output = {};
        Output.StructSize = sizeof(Output);
        NtUserCallOneParam((DWORD_PTR)&Output, DWM_ROUTINE_SETGPUOUTPUT);
    }
    for (ULONG Index = 0; Index < ARRAYSIZE(State.Surfaces); ++Index)
        State.Surfaces[Index].Reset();
    for (ULONG Index = 0; Index < ARRAYSIZE(State.ClientSources); ++Index)
        State.ClientSources[Index].Reset();
    for (ULONG Index = 0; Index < ARRAYSIZE(State.Blurs); ++Index)
        State.Blurs[Index].Reset();
    State.Backdrop.Reset();
    State.Canvas.Reset();
    Release(State.ClientCopies);
    Release(State.Completion);
    Release(State.Rasterizer);
    Release(State.PremultipliedBlend);
    Release(State.Blend);
    Release(State.Sampler);
    while (State.ConstantsHead != NULL)
    {
        ConstantBuffer *Upload = State.ConstantsHead;
        State.ConstantsHead = Upload->Next;
        Release(Upload->Resource);
        HeapFree(GetProcessHeap(), 0, Upload);
    }
    for (ULONG Index = 0; Index < ARRAYSIZE(State.PixelShaders); ++Index)
        Release(State.PixelShaders[Index]);
    Release(State.VertexShader);
    Release(State.SwapChain);
    Release(State.Context);
    Release(State.Device);
    if (State.Window != NULL)
    {
        RemovePropW(State.Window, DWM_PROP_GPU_OUTPUT);
        DestroyWindow(State.Window);
    }
    if (State.TraceProvider) FreeLibrary(State.TraceProvider);
    /* Runtime modules live for the compositor process, across resizes and
     * device resets. Releasing every GPU interface above is sufficient for
     * recovery; unloading DXGI also discards its adapter enumeration cache
     * and needlessly probes the reset OpenGL ICD again on reinitialization. */
    HMODULE Runtime = State.Runtime, Dxgi = State.Dxgi, Compiler = State.Compiler;
    ZeroMemory(&State, sizeof(State));
    State.Runtime = Runtime;
    State.Dxgi = Dxgi;
    State.Compiler = Compiler;
}

void
DwmD3dScene(const DWM_WIN *Windows, ULONG Count, const RECTL *BlurRects,
             ULONG BlurRectCount, LONG OriginX, LONG OriginY,
             BOOL RefreshBackdrop, ULONG BlurRadius, const RECT *ShadowMargins)
{
    State.SceneCurrent = FALSE;
    if (Count > DWM_MAX_WINDOWS || BlurRectCount > DWM_MAX_BLUR_RECTS)
    {
        State.Scene.Valid = FALSE;
        return;
    }
    DWM_GPU_SCENE_SPACE Space = {OriginX, OriginY, State.Width, State.Height, BlurRadius, *ShadowMargins};
    DwmGpuCacheScene(&State.Scene, Windows, Count, BlurRects, BlurRectCount, &Space, RefreshBackdrop, State.LowerUnchanged);
    State.SceneCurrent = TRUE;
    PruneClientSources(Windows, Count);
    for (ULONG Index = 0; Index < ARRAYSIZE(State.Surfaces); ++Index)
    {
        Surface *Slot = &State.Surfaces[Index];
        if (Slot->Image.Resource != NULL &&
            !DwmGpuSceneHasSurface(Windows, Count, Slot->SurfaceId, Slot->Client))
        {
            Slot->Reset();
            ZeroMemory(Slot, sizeof(*Slot));
        }
    }
    for (ULONG Index = 0; Index < ARRAYSIZE(State.Blurs); ++Index)
    {
        BlurTarget *Target = &State.Blurs[Index];
        if (!DwmGpuSceneHasSurface(Windows, Count, Target->Owner.SurfaceId, FALSE))
        {
            Target->Reset();
            ZeroMemory(Target, sizeof(*Target));
        }
    }
}

void
DwmD3dPrepareWindow(const DWM_WIN *Window, ULONG Index)
{
    State.WindowIndex = Index;
    if (Index > State.OccluderIndex)
        State.OccluderActive = FALSE;
    State.BlurOwner = DwmGpuCacheBlurOwner(Window);
    State.BlurOwnerValid = Window->AnimFlags == 0;
    State.BlurLowerUnchanged = Index < DWM_MAX_WINDOWS && State.LowerUnchanged[Index];
    State.BlurCall = 0;
}

BOOL
DwmD3dNeedsSurfacePixels(const DWM_WIN *Window)
{
    if (!State.Active || Window == NULL || Window->BaseGlobalShare != 0 || Window->BaseUpdateId == 0)
        return FALSE;
    Surface *Slot = FindSurface(Window, FALSE);
    return !SurfaceCurrent(Slot, Window) || Slot->UpdateId != Window->BaseUpdateId;
}

/* A published client drawn without blending, glass, colour key or animation
 * overwrites its whole client rectangle. */
static BOOL
OpaqueClientRect(const DWM_WIN *Window, RECT *Rect)
{
    DWM_GPU_WINDOW_GEOMETRY Owner, Client;

    if (Window->DxGlobalShare == 0 || Window->DxUpdateId == 0 || Window->AnimFlags != 0 ||
        ((Window->LayerFlags & DWM_LWA_ALPHA) && Window->Alpha < 255) ||
        (Window->LayerFlags & (DWM_LWA_COLORKEY | DWM_WINDOW_PREMULTIPLIED_ALPHA |
                               DWM_WINDOW_DX_PREMULTIPLIED_ALPHA)) ||
        (Window->BlurFlags & DWM_BLUR_ENABLE) ||
        (Window->BackdropType == DWM_BACKDROP_TRANSIENT && Window->BackdropOpacity < 255 &&
         Window->BackdropRegion == DWM_BACKDROP_REGION_WINDOW) ||
        !DwmGpuWindowGeometry(Window, State.Scene.Space.OriginX, State.Scene.Space.OriginY, &Owner) ||
        !DwmGpuClientGeometry(Window, &Owner, &Client))
    {
        return FALSE;
    }
    return DwmGpuDamageBounds(Rect, State.Width, State.Height, Client.Left, Client.Top,
                              Client.Left + Client.Width, Client.Top + Client.Height);
}

/* Pick the largest opaque client inside this frame's damage. Layers drawn
 * beneath it within its rectangle would be overwritten, so they are skipped.
 * Those pixels stay stale on the canvas, so no capture drawn before the
 * client may read them: a lower window's capture must miss the rectangle,
 * and the window's own material blur stays outside it by its radius. */
static void
SelectOccluder(void)
{
    LONGLONG Best = 0;

    State.OccluderActive = FALSE;
    State.OccluderIndex = ~0ul;
    if (!State.SceneCurrent)
        return;
    for (ULONG Index = 0; Index < State.Scene.Count; ++Index)
    {
        const DWM_WIN *Window = &State.Scene.Windows[Index];
        RECT Rect, Visible, Capture;
        BOOL Clear = TRUE;

        if (!OpaqueClientRect(Window, &Rect))
            continue;
        if (DwmGpuSceneWindowBounds(Window, &State.Scene.Space, TRUE, &Capture))
        {
            LONG Radius = (LONG)DwmGpuMaterialBlurRadius(Window) + 2;

            InflateRect(&Rect, -Radius, -Radius);
        }
        if (!IntersectRect(&Visible, &Rect, &State.Draw))
            continue;
        for (ULONG Lower = 0; Clear && Lower < Index; ++Lower)
        {
            if (DwmGpuSceneWindowBounds(&State.Scene.Windows[Lower], &State.Scene.Space, TRUE, &Capture) &&
                DwmGpuDamageIntersects(&Capture, &Visible))
                Clear = FALSE;
        }
        LONGLONG Area = (LONGLONG)(Visible.right - Visible.left) * (Visible.bottom - Visible.top);
        if (Clear && Area > Best)
        {
            Best = Area;
            State.Occluder = Visible;
            State.OccluderIndex = Index;
        }
    }
    State.OccluderActive = Best != 0;
}

BOOL
DwmD3dBegin(ULONG BackdropColor, const BYTE *BackdropPixels, BOOL RefreshBackdrop, const RECT *Damage)
{
    if (!State.Active || !FinishGpuReads())
        return FALSE;
    /* Each draw retains its constants until the frame's GPU reads retire. */
    State.NextConstants = State.ConstantsHead;
    ++State.Frame;
    State.BlurOwnerValid = FALSE;
    /* Copy new client publications before the canvas has pending draws, so
     * the fence ending here flushes only these copies. Their producers can
     * then reuse the shared buffers while the frame renders and waits for
     * scanout. A failed copy is retried, unfenced, when its window draws. */
    State.ClientCopiesFenced = State.ClientCopiesUnfenced = FALSE;
    State.CopyingClients = TRUE;
    for (ULONG Index = 0; State.SceneCurrent && Index < State.Scene.Count; ++Index)
    {
        const DWM_WIN *Window = &State.Scene.Windows[Index];
        if (Window->DxGlobalShare != 0 && Window->DxUpdateId != 0)
        {
            DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_TEXTURE);
            Texture *Client = Import(Window, TRUE);
            DptEnd(&g_DwmPresentTrace, Trace, Client != NULL, 0);
        }
    }
    State.CopyingClients = FALSE;
    if (State.WorkPending)
    {
        State.Context->End(State.ClientCopies);
        State.Context->Flush();
        State.ClientCopiesFenced = TRUE;
    }
    RECT Full = {0, 0, State.Width, State.Height};
    State.Draw = State.FrameValid && !RefreshBackdrop && Damage ? *Damage : Full;
    DwmGpuDamageUnion(&State.Draw, &State.Scene.AnimationDamage);
    if (!DwmGpuDamageBounds(&State.Draw, State.Width, State.Height, State.Draw.left, State.Draw.top, State.Draw.right, State.Draw.bottom))
        State.Draw = Full;
    DwmGpuDamageExpandBlur(&State.Draw, State.Width, State.Height, State.Scene.Windows, State.Scene.Count,
        State.Scene.Space.OriginX, State.Scene.Space.OriginY, State.Scene.Space.BlurRadius, NULL);
    SelectOccluder();
    Constants Data = {};
    SetRectangle(Data, 0, 0, State.Width, State.Height);
    SetColor(Data.Brush, BackdropColor);
    if (!Draw(State.Canvas, Solid, State.Draw, Data))
        return FALSE;
    if (BackdropPixels == NULL)
        return TRUE;
    if (State.Backdrop.Resource == NULL)
    {
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_CPU_UPLOAD);
        BOOL Success = CreateTexture(State.Backdrop, State.Width, State.Height, FALSE, BackdropPixels, State.Width * 4);
        DptEnd(&g_DwmPresentTrace, Trace, Success, Success ? (ULONGLONG)State.Width * State.Height * sizeof(ULONG) : 0);
        if (!Success)
            return FALSE;
    }
    else if (RefreshBackdrop)
    {
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_CPU_UPLOAD);
        State.Context->UpdateSubresource(State.Backdrop.Resource, 0, NULL, BackdropPixels, State.Width * 4, 0);
        BOOL Success = Result(State.Device->GetDeviceRemovedReason(), "Wallpaper update");
        DptEnd(&g_DwmPresentTrace, Trace, Success, Success ? (ULONGLONG)State.Width * State.Height * sizeof(ULONG) : 0);
        if (!Success)
            return FALSE;
    }
    Data.SourceSize[2] = 1;
    return Draw(State.Canvas, Copy, State.Draw, Data, State.Backdrop.View);
}

BOOL
DwmD3dWindow(const DWM_WIN *Window, const BYTE *Pixels, LONG OriginX, LONG OriginY)
{
    if (!State.Active || Window == NULL || Window->cx <= 0 || Window->cy <= 0)
        return FALSE;
    Texture *Client = NULL;
    if (Window->DxGlobalShare != 0 && Window->DxUpdateId != 0)
    {
        /* Even a clipped client needs its new publication copied before the
         * frame acknowledgement lets the producer reuse the shared buffer. */
        DPT_SCOPE Trace = DptBegin(&g_DwmPresentTrace, DPT_TEXTURE);
        Client = Import(Window, TRUE);
        DptEnd(&g_DwmPresentTrace, Trace, Client != NULL, 0);
        if (Client == NULL)
            return FALSE;
    }
    if (Window->BaseUpdateId != 0 && !DrawLayer(Window, Pixels, FALSE, OriginX, OriginY))
        return FALSE;
    /* A client capture also sees its owner's newly drawn base pixels. */
    State.BlurOwnerValid = FALSE;
    /* The occluding client itself, and everything above it, draws whole. */
    if (State.WindowIndex >= State.OccluderIndex)
        State.OccluderActive = FALSE;
    return Client == NULL || DrawLayer(Window, NULL, TRUE, OriginX, OriginY, Client);
}

BOOL
DwmD3dBlurRect(const RECT *Rect, ULONG Radius)
{
    if (!State.Active || Rect == NULL || Radius > 64)
        return FALSE;
    RECT Capture;
    if (!DwmGpuDamageBounds(&Capture, State.Width, State.Height, Rect->left, Rect->top, Rect->right, Rect->bottom) ||
        !DwmGpuDamageIntersects(&Capture, &State.Draw))
        return TRUE;
    BlurTarget *Blur = FilterCapture(Capture, Radius);
    if (Blur == NULL)
        return FALSE;
    Constants Data = {};
    SetRectangle(Data, Capture.left, Capture.top, Capture.right, Capture.bottom);
    Data.SourceSize[2] = 1;
    return Draw(State.Canvas, Copy, ClipDraw(Capture), Data, Blur->Result.View);
}

static BOOL
WindowBlurRect(const DWM_WIN *Window, const RECTL *Region, LONG OriginX, LONG OriginY, RECT &Rect)
{
    DWM_GPU_WINDOW_GEOMETRY Geometry;
    if (!DwmGpuWindowGeometry(Window, OriginX, OriginY, &Geometry))
        return FALSE;
    return DwmGpuDamageBounds(&Rect, State.Width, State.Height,
        Geometry.Left + DwmGpuScaleWindowEdge(max(Region->left, 0), Window->cx, Geometry.Width),
        Geometry.Top + DwmGpuScaleWindowEdge(max(Region->top, 0), Window->cy, Geometry.Height),
        Geometry.Left + DwmGpuScaleWindowEdge(min(Region->right, Window->cx), Window->cx, Geometry.Width),
        Geometry.Top + DwmGpuScaleWindowEdge(min(Region->bottom, Window->cy), Window->cy, Geometry.Height));
}

BOOL
DwmD3dBlurWindow(const DWM_WIN *Window, const RECTL *Rectangles, LONG OriginX, LONG OriginY, ULONG Radius)
{
    if (!(Window->BlurFlags & DWM_BLUR_ENABLE))
        return TRUE;
    if (Radius > 64 || (Window->BlurRectCount != 0 && Rectangles == NULL))
        return FALSE;
    FLOAT Alpha = (Window->LayerFlags & DWM_LWA_ALPHA) ? min(Window->Alpha, 255u) / 255.0f : 1.0f;
    if (Alpha == 0)
        return TRUE;
    RECTL Whole = {0, 0, Window->cx, Window->cy};
    ULONG Count = Window->BlurRectCount;
    if (Window->BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW)
    {
        Rectangles = &Whole;
        Count = 1;
    }
    RECT Capture = {};
    for (ULONG Index = 0; Index < Count; ++Index)
    {
        RECT Part;
        if (WindowBlurRect(Window, &Rectangles[Index], OriginX, OriginY, Part))
            DwmGpuDamageUnion(&Capture, &Part);
    }
    if (!DwmGpuDamageBounds(&Capture, State.Width, State.Height, (LONGLONG)Capture.left - Radius,
        (LONGLONG)Capture.top - Radius, (LONGLONG)Capture.right + Radius, (LONGLONG)Capture.bottom + Radius) ||
        !DwmGpuDamageIntersects(&Capture, &State.Draw) || Count == 0)
        return TRUE;
    BlurTarget *Blur = FilterCapture(Capture, Radius);
    if (Blur == NULL)
        return FALSE;
    Constants Data = {};
    SetRectangle(Data, Capture.left, Capture.top, Capture.right, Capture.bottom);
    Data.SourceSize[2] = Alpha;
    for (ULONG Index = 0; Index < Count; ++Index)
    {
        RECT Part;
        if (WindowBlurRect(Window, &Rectangles[Index], OriginX, OriginY, Part) &&
            !Draw(State.Canvas, Copy, ClipDraw(Part), Data, Blur->Result.View, NULL, Alpha < 1))
            return FALSE;
    }
    return TRUE;
}

BOOL
DwmD3dShadow(const RECT *Bounds, LONGLONG X, LONGLONG Y, LONG Width, LONG Height,
              LONG Offset, LONG WideExtent, BOOL Active, ULONG WideOpacity,
              ULONG TightOpacity, ULONG WindowAlpha)
{
    if (!State.Active || Bounds == NULL || Width <= 0 || Height <= 0 || WideExtent <= 0)
        return FALSE;
    if (WindowAlpha == 0 || !DwmGpuDamageIntersects(Bounds, &State.Draw))
        return TRUE;
    Constants Data = {};
    SetRectangle(Data, Bounds->left, Bounds->top, Bounds->right, Bounds->bottom);
    Data.SourceSize[0] = (FLOAT)Width;
    Data.SourceSize[1] = (FLOAT)Height;
    Data.CaptureRect[0] = (FLOAT)X;
    Data.CaptureRect[1] = (FLOAT)Y;
    Data.Shadow[0] = WideExtent / 3.0f;
    Data.Shadow[1] = WideExtent * (Active ? 2.0f : 1.0f) / 9.0f;
    Data.Shadow[2] = (FLOAT)WideOpacity;
    Data.Shadow[3] = (FLOAT)TightOpacity;
    Data.Extra[1] = (FLOAT)Offset;
    Data.Extra[2] = (FLOAT)WindowAlpha;
    LONG L = (LONG)max(Bounds->left, min(Bounds->right, X));
    LONG T = (LONG)max(Bounds->top, min(Bounds->bottom, Y));
    LONG R = (LONG)max(Bounds->left, min(Bounds->right, X + Width));
    LONG B = (LONG)max(Bounds->top, min(Bounds->bottom, Y + Height));
    RECT Parts[] = {{Bounds->left, Bounds->top, Bounds->right, T},
        {Bounds->left, B, Bounds->right, Bounds->bottom}, {Bounds->left, T, L, B}, {R, T, Bounds->right, B}};
    for (ULONG Index = 0; Index < ARRAYSIZE(Parts); ++Index)
        if (!Draw(State.Canvas, Shadow, ClipDraw(Parts[Index]), Data, NULL, NULL, TRUE))
            return FALSE;
    return TRUE;
}

void
DwmD3dBlurStats(ULONGLONG *Filtered, ULONGLONG *Reused)
{
    *Filtered = State.Filtered;
    *Reused = State.Reused;
    State.Filtered = State.Reused = 0;
}

BOOL
DwmD3dClientCopiesRetired(void)
{
    if (!State.Active || State.ClientCopiesUnfenced)
        return FALSE;
    if (!State.ClientCopiesFenced)
        return TRUE;
    /* The copies were submitted at Begin and are usually done by now. Do not
     * delay the present for a slow one; it is acknowledged after the present. */
    LARGE_INTEGER Frequency, Start, Now;
    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    for (;;)
    {
        BOOL Complete = FALSE;
        HRESULT Status = State.Context->GetData(State.ClientCopies, &Complete, sizeof(Complete),
                                                D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(Status))
            return FALSE;
        if (Status == S_OK && Complete)
        {
            State.ClientCopiesFenced = FALSE;
            return TRUE;
        }
        QueryPerformanceCounter(&Now);
        if ((Now.QuadPart - Start.QuadPart) * 250 >= Frequency.QuadPart)
            return FALSE;
        SwitchToThread();
    }
}

DWM_GPU_RESULT
DwmD3dEnd(void)
{
    if (!State.Active)
        return DWM_GPU_FAILED;
    ID3D11Texture2D *BackBuffer = NULL;
    if (!Result(State.SwapChain->GetBuffer(0, IID_ID3D11Texture2D, (void **)&BackBuffer), "GetBuffer"))
        return DWM_GPU_FAILED;
    /* The canvas always contains the last completed composition. A two-buffer
     * flip chain needs both this frame's repair and the previous frame's repair
     * before the current back buffer is complete. Initialize each buffer once. */
    RECT Copy = State.Draw;
    if (State.PresentedBuffers < 2)
        SetRect(&Copy, 0, 0, State.Width, State.Height);
    else
        DwmGpuDamageUnion(&Copy, &State.PreviousCopy);
    D3D11_BOX Box = {(UINT)Copy.left, (UINT)Copy.top, 0, (UINT)Copy.right, (UINT)Copy.bottom, 1};
    UnbindTextures();
    DPT_SCOPE CopyTrace = DptBegin(&g_DwmPresentTrace, DPT_FLUSH);
    State.Context->CopySubresourceRegion(BackBuffer, 0, Copy.left, Copy.top, 0, State.Canvas.Resource, 0, &Box);
    DptEnd(&g_DwmPresentTrace, CopyTrace, TRUE, 0);
    State.WorkPending = TRUE;
    Release(BackBuffer);
    DXGI_PRESENT_PARAMETERS Present = {};
    Present.DirtyRectsCount = 1;
    Present.pDirtyRects = &Copy;
    DPT_SCOPE PresentTrace = DptBegin(&g_DwmPresentTrace, DPT_KMT_PRESENT);
    HRESULT Status = State.SwapChain->Present1(1, 0, &Present);
    DptEnd(&g_DwmPresentTrace, PresentTrace, SUCCEEDED(Status), 0);
    BOOL Finished = FinishGpuReads();
    if (!Finished || !Result(Status, "Present1"))
        return DWM_GPU_FAILED;
    if (Status == DXGI_STATUS_OCCLUDED)
    {
        /* The private compositor output can yield to an exclusive owner.
         * Keep the device and imported copies, but repair both flip buffers
         * before trusting incremental damage after output becomes available. */
        State.FrameValid = FALSE;
        State.Scene.Valid = FALSE;
        State.PresentedBuffers = 0;
        return DWM_GPU_DEFERRED;
    }
    State.PreviousCopy = State.Draw;
    State.PresentedBuffers = min(State.PresentedBuffers + 1, 2u);
    State.FrameValid = TRUE;
    State.Scene.Valid = TRUE;
    return DWM_GPU_COMPLETE;
}

DWM_GPU_RESULT
DwmD3dCheckOutput(void)
{
    if (!State.Active)
        return DWM_GPU_FAILED;
    HRESULT Status = State.SwapChain->Present1(0, DXGI_PRESENT_TEST, NULL);
    if (Status == DXGI_STATUS_OCCLUDED)
        return DWM_GPU_DEFERRED;
    return Result(Status, "Present1(TEST)") ? DWM_GPU_COMPLETE : DWM_GPU_FAILED;
}
