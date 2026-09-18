/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     Pixel verification of the native DWM draw and copy path.
 */

#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdarg.h>
#include <reactos/dwmprof.h>
#include <reactos/dwmpresenttracenames.h>
#include "gpud3dshader.h"

namespace
{
const UINT Width = 800, Height = 600;
const WCHAR WindowClassName[] = L"DwmPixelRegression";
const DWORD Background = 0xff204060, Foreground = 0xff40c080;
const DWORD Colors[] = {0xffe02040, 0xff20c060, 0xff4060e0, 0xffe0c020};

struct Constants
{
    FLOAT Rectangle[4], TargetSize[4], SourceSize[4], ClientRect[4], CaptureRect[4];
    FLOAT Brush[4], Colorization[4], ColorKey[4], Flags[4], Extra[4], Shadow[4], Filter[4];
    FLOAT Taps[33][4];
};

struct TestState
{
    HMODULE Runtime, Compiler, Profiler;
    HWND Window;
    ATOM WindowClass;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    IDXGISwapChain *SwapChain;
    ID3D11Texture2D *Canvas, *Staging, *Source;
    ID3D11Texture2D *Depth;
    ID3D11DepthStencilView *DepthView;
    ID3D11RenderTargetView *Target;
    ID3D11ShaderResourceView *SourceView;
    ID3D11VertexShader *Vertex;
    ID3D11PixelShader *Solid, *Copy;
    ID3D11Buffer *Buffer;
    ID3D11RasterizerState *Rasterizer;
    ID3D11SamplerState *Sampler;
    ID3D11Query *Completion;
    HRESULT (WINAPI *StopProfile)(ULONG, DPT_SNAPSHOT *, ULONG);
    ULONG Session;
    UINT Failures, Checks, Presents;
};

template<class T> void Release(T *&Object)
{
    if (Object) Object->Release();
    Object = NULL;
}

void Log(const char *Format, ...)
{
    char Buffer[768];
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Args);
    va_end(Args);
    Buffer[sizeof(Buffer) - 1] = 0;
    trace("%s", Buffer);
    OutputDebugStringA(Buffer);
}

bool Check(TestState &State, HRESULT Status, const char *Stage)
{
    ok(SUCCEEDED(Status), "%s failed: %#lx\n", Stage, Status);
    if (SUCCEEDED(Status)) return true;
    ++State.Failures;
    Log("DWM_RENDER_ERROR stage=%s status=%08lx\n", Stage, Status);
    return false;
}

bool WaitForGpu(TestState &State)
{
    State.Context->End(State.Completion);
    State.Context->Flush();
    DWORD Start = GetTickCount();
    do
    {
        BOOL Complete = FALSE;
        HRESULT Status = State.Context->GetData(State.Completion, &Complete, sizeof(Complete),
                                                D3D11_ASYNC_GETDATA_DONOTFLUSH);
        HRESULT DeviceStatus = State.Device->GetDeviceRemovedReason();
        if (FAILED(DeviceStatus)) return Check(State, DeviceStatus, "device_state");
        if (FAILED(Status)) return Check(State, Status, "gpu_completion");
        if (Status == S_OK && Complete) return true;
        Sleep(1);
    } while (GetTickCount() - Start < 5000);
    return Check(State, HRESULT_FROM_WIN32(ERROR_TIMEOUT), "gpu_completion_timeout");
}

void Color(FLOAT *Output, DWORD Pixel)
{
    Output[0] = ((Pixel >> 16) & 255) / 255.0f;
    Output[1] = ((Pixel >> 8) & 255) / 255.0f;
    Output[2] = (Pixel & 255) / 255.0f;
    Output[3] = 1.0f;
}

DWORD Expected(UINT Mode, UINT X, UINT Y)
{
    if (Mode == 0) return Background;
    if (Mode == 1) return Foreground;
    if (Mode == 2)
        return X >= 37 && X < Width - 53 && Y >= 29 && Y < Height - 41 ? Foreground : Background;
    return Colors[(X >= Width / 2 ? 1 : 0) + (Y >= Height / 2 ? 2 : 0)];
}

bool Verify(TestState &State, ID3D11Texture2D *Texture, UINT Mode, const char *Stage)
{
    State.Context->OMSetRenderTargets(0, NULL, NULL);
    State.Context->CopyResource(State.Staging, Texture);
    if (!WaitForGpu(State)) return false;
    D3D11_MAPPED_SUBRESOURCE Map = {};
    if (!Check(State, State.Context->Map(State.Staging, 0, D3D11_MAP_READ,
                                        D3D11_MAP_FLAG_DO_NOT_WAIT, &Map), "readback_map")) return false;
    UINT Mismatches = 0, FirstX = 0, FirstY = 0;
    DWORD FirstActual = 0, FirstExpected = 0, Hash = 2166136261u;
    bool Valid = Map.pData && Map.RowPitch >= Width * sizeof(DWORD);
    if (Valid)
    {
        for (UINT Y = 0; Y < Height; ++Y)
        {
            const DWORD *Row = reinterpret_cast<const DWORD *>(
                static_cast<const BYTE *>(Map.pData) + Y * Map.RowPitch);
            for (UINT X = 0; X < Width; ++X)
            {
                DWORD Actual = Row[X], Wanted = Expected(Mode, X, Y);
                Hash = (Hash ^ Actual) * 16777619u;
                bool Match = true;
                for (UINT Shift = 0; Shift < 32; Shift += 8)
                {
                    int Difference = int((Actual >> Shift) & 255) - int((Wanted >> Shift) & 255);
                    if (Difference < -1 || Difference > 1) Match = false;
                }
                if (!Match && ++Mismatches == 1)
                {
                    FirstX = X; FirstY = Y; FirstActual = Actual; FirstExpected = Wanted;
                }
            }
        }
    }
    State.Context->Unmap(State.Staging, 0);
    ++State.Checks;
    bool Passed = Valid && !Mismatches;
    ok(Passed, "%s: %u wrong pixels; first %u,%u actual=%08lx expected=%08lx, pitch=%u\n",
       Stage, Mismatches, FirstX, FirstY, FirstActual, FirstExpected, Map.RowPitch);
    if (!Passed) ++State.Failures;
    Log("DWM_RENDER_PIXELS stage=%s pass=%u mismatch=%u first=%u,%u actual=%08lx expected=%08lx hash=%08lx pitch=%u\n",
        Stage, Passed, Mismatches, FirstX, FirstY, FirstActual, FirstExpected, Hash, Map.RowPitch);
    return Passed;
}

void Draw(TestState &State, UINT Mode, ID3D11DepthStencilView *Depth = NULL)
{
    Constants Data = {};
    Data.Rectangle[2] = Data.TargetSize[0] = Width;
    Data.Rectangle[3] = Data.TargetSize[1] = Height;
    Data.SourceSize[2] = 1;
    Color(Data.Brush, Foreground);
    State.Context->UpdateSubresource(State.Buffer, 0, NULL, &Data, 0, 0);
    D3D11_VIEWPORT Viewport = {0, 0, FLOAT(Width), FLOAT(Height), 0, 1};
    RECT Clip = {0, 0, Width, Height};
    if (Mode == 2) SetRect(&Clip, 37, 29, Width - 53, Height - 41);
    State.Context->RSSetViewports(1, &Viewport);
    State.Context->RSSetScissorRects(1, &Clip);
    State.Context->RSSetState(State.Rasterizer);
    State.Context->OMSetRenderTargets(1, &State.Target, Depth);
    State.Context->OMSetBlendState(NULL, NULL, ~0u);
    State.Context->IASetInputLayout(NULL);
    State.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    State.Context->VSSetShader(State.Vertex, NULL, 0);
    State.Context->PSSetShader(Mode == 3 ? State.Copy : State.Solid, NULL, 0);
    State.Context->VSSetConstantBuffers(0, 1, &State.Buffer);
    State.Context->PSSetConstantBuffers(0, 1, &State.Buffer);
    State.Context->PSSetSamplers(0, 1, &State.Sampler);
    ID3D11ShaderResourceView *Views[2] = {Mode == 3 ? State.SourceView : NULL, NULL};
    State.Context->PSSetShaderResources(0, 2, Views);
    State.Context->Draw(4, 0);
    Views[0] = NULL;
    State.Context->PSSetShaderResources(0, 2, Views);
}

bool VerifyDepthBinding(TestState &State)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Width; Desc.Height = Height;
    Desc.MipLevels = Desc.ArraySize = Desc.SampleDesc.Count = 1;
    Desc.Format = DXGI_FORMAT_D32_FLOAT;
    Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (!Check(State, State.Device->CreateTexture2D(&Desc, NULL, &State.Depth), "depth_texture") ||
        !Check(State, State.Device->CreateDepthStencilView(State.Depth, NULL, &State.DepthView), "depth_view")) return false;

    FLOAT Clear[4];
    Color(Clear, Background);
    State.Context->OMSetDepthStencilState(NULL, 0);
    State.Context->ClearRenderTargetView(State.Target, Clear);
    State.Context->ClearDepthStencilView(State.DepthView, D3D11_CLEAR_DEPTH, 0, 0);
    Draw(State, 1, State.DepthView);
    if (!Verify(State, State.Canvas, 0, "depth_bound_reject")) return false;
    State.Context->ClearDepthStencilView(State.DepthView, D3D11_CLEAR_DEPTH, 1, 0);
    Draw(State, 1, State.DepthView);
    if (!Verify(State, State.Canvas, 1, "depth_bound_accept")) return false;
    State.Context->ClearRenderTargetView(State.Target, Clear);
    Draw(State, 1);
    return Verify(State, State.Canvas, 1, "depth_detached");
}

bool CreateResources(TestState &State)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Width; Desc.Height = Height;
    Desc.MipLevels = Desc.ArraySize = Desc.SampleDesc.Count = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (!Check(State, State.Device->CreateTexture2D(&Desc, NULL, &State.Canvas), "canvas") ||
        !Check(State, State.Device->CreateRenderTargetView(State.Canvas, NULL, &State.Target), "target")) return false;
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.BindFlags = 0;
    if (!Check(State, State.Device->CreateTexture2D(&Desc, NULL, &State.Staging), "staging")) return false;
    DWORD *Pixels = static_cast<DWORD *>(HeapAlloc(GetProcessHeap(), 0, Width * Height * sizeof(DWORD)));
    if (!Pixels) return Check(State, E_OUTOFMEMORY, "source_pixels");
    for (UINT Y = 0; Y < Height; ++Y)
        for (UINT X = 0; X < Width; ++X) Pixels[Y * Width + X] = Expected(3, X, Y);
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.CPUAccessFlags = 0;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA Initial = {Pixels, Width * sizeof(DWORD), 0};
    HRESULT Status = State.Device->CreateTexture2D(&Desc, &Initial, &State.Source);
    HeapFree(GetProcessHeap(), 0, Pixels);
    if (!Check(State, Status, "source_upload") ||
        !Check(State, State.Device->CreateShaderResourceView(State.Source, NULL, &State.SourceView), "source_view")) return false;
    D3D11_BUFFER_DESC Buffer = {};
    Buffer.ByteWidth = sizeof(Constants);
    Buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_RASTERIZER_DESC Rasterizer = {};
    Rasterizer.FillMode = D3D11_FILL_SOLID;
    Rasterizer.CullMode = D3D11_CULL_NONE;
    Rasterizer.DepthClipEnable = Rasterizer.ScissorEnable = TRUE;
    D3D11_SAMPLER_DESC Sampler = {};
    Sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    Sampler.MaxLOD = D3D11_FLOAT32_MAX;
    Sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    D3D11_QUERY_DESC Query = {D3D11_QUERY_EVENT, 0};
    return Check(State, State.Device->CreateBuffer(&Buffer, NULL, &State.Buffer), "constant_buffer") &&
        Check(State, State.Device->CreateRasterizerState(&Rasterizer, &State.Rasterizer), "rasterizer") &&
        Check(State, State.Device->CreateSamplerState(&Sampler, &State.Sampler), "sampler") &&
        Check(State, State.Device->CreateQuery(&Query, &State.Completion), "completion_query");
}

bool CreateShaders(TestState &State)
{
    typedef HRESULT (WINAPI *CompileProc)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
        ID3DInclude *, const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);
    CompileProc Compile = reinterpret_cast<CompileProc>(GetProcAddress(State.Compiler, "D3DCompile"));
    if (!Compile) return Check(State, E_NOINTERFACE, "compiler_export");
    const char *Entries[] = {"QuadVS", "SolidPS", "CopyPS"};
    for (UINT Index = 0; Index < ARRAYSIZE(Entries); ++Index)
    {
        ID3DBlob *Code = NULL, *Errors = NULL;
        HRESULT Status = Compile(DwmD3dShaderSource, sizeof(DwmD3dShaderSource) - 1, "DwmCompositor", NULL, NULL,
            Entries[Index], Index ? "ps_4_0" : "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &Code, &Errors);
        if (FAILED(Status) && Errors) Log("DWM_RENDER_SHADER_ERROR %s\n", static_cast<const char *>(Errors->GetBufferPointer()));
        Release(Errors);
        if (SUCCEEDED(Status))
        {
            if (!Index) Status = State.Device->CreateVertexShader(Code->GetBufferPointer(), Code->GetBufferSize(), NULL, &State.Vertex);
            else Status = State.Device->CreatePixelShader(Code->GetBufferPointer(), Code->GetBufferSize(), NULL, Index == 1 ? &State.Solid : &State.Copy);
        }
        Release(Code);
        if (!Check(State, Status, Entries[Index])) return false;
    }
    return true;
}

void Profile(TestState &State, bool Start)
{
    if (Start)
    {
        State.Profiler = LoadLibraryW(L"dwmprof.dll");
        if (!State.Profiler) { Log("DWM_RENDER_PROFILE unavailable=%lu\n", GetLastError()); return; }
        typedef HRESULT (WINAPI *StartProc)(ULONG *);
        StartProc Begin = reinterpret_cast<StartProc>(GetProcAddress(State.Profiler, "DwmProfileStartCapture"));
        State.StopProfile = reinterpret_cast<decltype(State.StopProfile)>(GetProcAddress(State.Profiler, "DwmProfileStopCapture"));
        if (Begin && State.StopProfile)
        {
            HRESULT Status = Begin(&State.Session);
            Log("DWM_RENDER_PROFILE start=%08lx session=%lu\n", Status, State.Session);
            if (FAILED(Status)) State.Session = 0;
        }
        return;
    }
    if (!State.Session) return;
    DPT_SNAPSHOT Snapshot = {};
    HRESULT Status = State.StopProfile(State.Session, &Snapshot, sizeof(Snapshot));
    State.Session = 0;
    Log("DWM_RENDER_PROFILE stop=%08lx available=%lx\n", Status, Snapshot.Available);
    if (FAILED(Status)) return;
    for (ULONG Domain = 0; Domain < 3; ++Domain)
    {
        Log("DWM_RENDER_DOMAIN id=%lu status=%08lx pid=%lu frequency=%I64u\n",
            Domain, Snapshot.Status[Domain], Snapshot.Domain[Domain].ProcessId, Snapshot.Domain[Domain].Frequency);
        if (!(Snapshot.Available & (1 << Domain))) continue;
        for (ULONG Metric = 0; Metric < DPT_METRIC_COUNT; ++Metric)
        {
            const DPT_COUNTER &Counter = Snapshot.Domain[Domain].Counter[Metric];
            if (!Counter.Entered) continue;
            Log("DWM_RENDER_COUNTER domain=%lu stage=%s entered=%I64u completed=%I64u failed=%I64u ticks=%I64u max=%I64u\n",
                Domain, DptMetricName(Metric), Counter.Entered, Counter.Completed, Counter.Failed, Counter.Ticks, Counter.MaxTicks);
        }
    }
}

bool Run(TestState &State)
{
    State.Runtime = LoadLibraryW(L"d3d11.dll");
    if (!State.Runtime) return Check(State, HRESULT_FROM_WIN32(GetLastError()), "runtime_load");
    State.Compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!State.Compiler) return Check(State, HRESULT_FROM_WIN32(GetLastError()), "compiler_load");
    typedef HRESULT (WINAPI *CreateProc)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
        ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    CreateProc Create = reinterpret_cast<CreateProc>(GetProcAddress(State.Runtime, "D3D11CreateDeviceAndSwapChain"));
    if (!Create) return Check(State, E_NOINTERFACE, "create_export");
    WNDCLASSW WindowClass = {};
    WindowClass.lpfnWndProc = DefWindowProcW;
    WindowClass.hInstance = GetModuleHandleW(NULL);
    WindowClass.lpszClassName = WindowClassName;
    State.WindowClass = RegisterClassW(&WindowClass);
    if (!State.WindowClass) return Check(State, HRESULT_FROM_WIN32(GetLastError()), "window_class");
    RECT Bounds = {0, 0, Width, Height};
    AdjustWindowRect(&Bounds, WS_OVERLAPPEDWINDOW, FALSE);
    State.Window = CreateWindowW(WindowClassName, L"DWM pixel regression", WS_OVERLAPPEDWINDOW,
        16, 16, Bounds.right - Bounds.left, Bounds.bottom - Bounds.top, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!State.Window) return Check(State, HRESULT_FROM_WIN32(GetLastError()), "window");
    DXGI_SWAP_CHAIN_DESC Desc = {};
    Desc.BufferDesc.Width = Width; Desc.BufferDesc.Height = Height;
    Desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.BufferDesc.RefreshRate.Numerator = 60; Desc.BufferDesc.RefreshRate.Denominator = 1;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.OutputWindow = State.Window;
    Desc.Windowed = TRUE;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    const D3D_FEATURE_LEVEL Levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL Level;
    if (!Check(State, Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        Levels, ARRAYSIZE(Levels), D3D11_SDK_VERSION, &Desc, &State.SwapChain, &State.Device,
        &Level, &State.Context), "hardware_device")) return false;
    Log("DWM_RENDER_DEVICE feature=%x desktop=%dx%d client=%ux%u\n", Level,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), Width, Height);
    if (!CreateResources(State) || !CreateShaders(State)) return false;
    Profile(State, true);
    ShowWindow(State.Window, SW_SHOWNOACTIVATE);
    UpdateWindow(State.Window);
    const char *Stages[] = {"clear", "solid_shader", "scissor", "wallpaper_sample"};
    for (UINT Mode = 0; Mode < ARRAYSIZE(Stages); ++Mode)
    {
        FLOAT Clear[4];
        Color(Clear, Background);
        State.Context->ClearRenderTargetView(State.Target, Clear);
        if (Mode) Draw(State, Mode);
        if (!Verify(State, State.Canvas, Mode, Stages[Mode])) return false;
        for (UINT Frame = 0; Frame < 3; ++Frame)
        {
            ID3D11Texture2D *BackBuffer = NULL;
            if (!Check(State, State.SwapChain->GetBuffer(0, IID_ID3D11Texture2D,
                reinterpret_cast<void **>(&BackBuffer)), "backbuffer")) return false;
            State.Context->CopyResource(BackBuffer, State.Canvas);
            bool Good = Verify(State, BackBuffer, Mode, "swapchain_copy");
            Release(BackBuffer);
            if (!Good) return false;
            HRESULT Status = State.SwapChain->Present(1, 0);
            ok(Status == S_OK, "Present returned %#lx\n", Status);
            if (Status != S_OK)
            {
                ++State.Failures;
                Log("DWM_RENDER_ERROR stage=present status=%08lx\n", Status);
                return false;
            }
            ++State.Presents;
            DWORD Start = GetTickCount();
            do
            {
                MSG Message;
                while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&Message);
                    DispatchMessageW(&Message);
                }
                Sleep(10);
            } while (GetTickCount() - Start < 200);
        }
    }
    return VerifyDepthBinding(State);
}
}

START_TEST(dwm_render)
{
    TestState State = {};
    Log("DWM_RENDER_BEGIN physical_display=unverified\n");
    bool Complete = Run(State);
    Profile(State, false);
    if (State.Context) { State.Context->ClearState(); State.Context->Flush(); }
    Release(State.Completion); Release(State.Sampler); Release(State.Rasterizer);
    Release(State.Buffer); Release(State.Copy); Release(State.Solid); Release(State.Vertex);
    Release(State.SourceView); Release(State.Target);
    Release(State.DepthView); Release(State.Depth);
    Release(State.Source); Release(State.Staging); Release(State.Canvas);
    Release(State.SwapChain); Release(State.Context); Release(State.Device);
    if (State.Window) DestroyWindow(State.Window);
    if (State.WindowClass) UnregisterClassW(WindowClassName, GetModuleHandleW(NULL));
    if (State.Profiler) FreeLibrary(State.Profiler);
    if (State.Compiler) FreeLibrary(State.Compiler);
    if (State.Runtime) FreeLibrary(State.Runtime);
    Log("DWM_RENDER_END complete=%u failures=%u pixel_checks=%u presents=%u physical_display=unverified\n",
        Complete, State.Failures, State.Checks, State.Presents);
}
