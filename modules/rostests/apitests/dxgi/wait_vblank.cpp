/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that IDXGIOutput::WaitForVBlank blocks until the output's vertical blank
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <initguid.h>
#include <dxgi.h>

START_TEST(wait_vblank)
{
    typedef HRESULT (WINAPI *CREATE_FACTORY)(REFIID, void **);
    IDXGIFactory1 *Factory = NULL;
    IDXGIAdapter1 *Adapter = NULL;
    IDXGIOutput *Output = NULL;
    LARGE_INTEGER Frequency, Start, End;
    HRESULT Hr;
    double Interval;
    UINT Index, Failures = 0;
    HMODULE Module = LoadLibraryW(L"dxgi.dll");

    if (!Module) { skip("dxgi.dll is unavailable\n"); return; }
    CREATE_FACTORY Create = reinterpret_cast<CREATE_FACTORY>(GetProcAddress(Module, "CreateDXGIFactory1"));
    if (!Create) { skip("CreateDXGIFactory1 is unavailable\n"); FreeLibrary(Module); return; }

    Hr = Create(IID_IDXGIFactory1, reinterpret_cast<void **>(&Factory));
    ok(SUCCEEDED(Hr), "CreateDXGIFactory1: %#lx\n", Hr);
    if (Factory && Factory->EnumAdapters1(0, &Adapter) == S_OK &&
        Adapter->EnumOutputs(0, &Output) == S_OK)
    {
        Hr = Output->WaitForVBlank();
        ok(Hr == S_OK, "WaitForVBlank: %#lx\n", Hr);

        QueryPerformanceFrequency(&Frequency);
        QueryPerformanceCounter(&Start);
        for (Index = 0; Index < 30; ++Index)
        {
            if (Output->WaitForVBlank() != S_OK)
                ++Failures;
        }
        QueryPerformanceCounter(&End);

        Interval = (End.QuadPart - Start.QuadPart) * 1000.0 / Frequency.QuadPart / 30;
        ok(Failures == 0, "%u of 30 WaitForVBlank calls failed\n", Failures);
        ok(Interval >= 4.0 && Interval <= 42.0,
           "Average vertical blank interval is %.2f ms\n", Interval);
    }
    else
    {
        skip("No DXGI output\n");
    }

    if (Output) Output->Release();
    if (Adapter) Adapter->Release();
    if (Factory) Factory->Release();
    FreeLibrary(Module);
}
