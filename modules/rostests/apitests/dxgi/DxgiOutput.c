/*
 * PROJECT:     ReactOS DXGI API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test IDXGIOutput
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "precomp.h"

static IDXGIOutput *get_first_output(void)
{
    IDXGIFactory *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIOutput *output = NULL;
    HRESULT hr;

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    if (FAILED(hr))
        return NULL;

    hr = IDXGIFactory_EnumAdapters(factory, 0, &adapter);
    if (FAILED(hr))
    {
        IDXGIFactory_Release(factory);
        return NULL;
    }

    hr = IDXGIAdapter_EnumOutputs(adapter, 0, &output);
    IDXGIAdapter_Release(adapter);
    IDXGIFactory_Release(factory);

    if (FAILED(hr))
        return NULL;

    return output;
}

static BOOL is_display_device_name(const WCHAR *name)
{
    static const WCHAR prefix[] = L"\\\\.\\DISPLAY";

    return wcsncmp(name, prefix, sizeof(prefix) / sizeof(prefix[0]) - 1) == 0;
}

static void test_GetDisplayModeList(void)
{
    IDXGIOutput *output;
    UINT numModes = 0;
    DXGI_MODE_DESC *modes;
    HRESULT hr;
    UINT i;

    output = get_first_output();
    if (!output)
    {
        skip("No output available\n");
        return;
    }

    /* Query the number of modes */
    hr = IDXGIOutput_GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, NULL);
    ok(hr == S_OK, "GetDisplayModeList count query failed: 0x%08lx\n", hr);
    trace("Display modes for R8G8B8A8_UNORM: %u\n", numModes);

    if (numModes > 0)
    {
        modes = HeapAlloc(GetProcessHeap(), 0, numModes * sizeof(DXGI_MODE_DESC));
        ok(modes != NULL, "Failed to allocate mode list\n");
        if (modes)
        {
            hr = IDXGIOutput_GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, modes);
            ok(hr == S_OK, "GetDisplayModeList fill failed: 0x%08lx\n", hr);
            if (SUCCEEDED(hr))
            {
                for (i = 0; i < numModes && i < 5; i++)
                {
                    trace("  Mode %u: %ux%u @ %u/%u Hz\n",
                          i, modes[i].Width, modes[i].Height,
                          modes[i].RefreshRate.Numerator,
                          modes[i].RefreshRate.Denominator);
                    ok(modes[i].Width > 0, "Mode %u has zero width\n", i);
                    ok(modes[i].Height > 0, "Mode %u has zero height\n", i);
                }
            }
            HeapFree(GetProcessHeap(), 0, modes);
        }
    }

    /* Test with NULL pNumModes */
    hr = IDXGIOutput_GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, NULL, NULL);
    ok(FAILED(hr), "GetDisplayModeList(NULL numModes) should fail\n");

    /* Test buffer too small */
    if (numModes > 1)
    {
        UINT smallCount = 1;
        modes = HeapAlloc(GetProcessHeap(), 0, sizeof(DXGI_MODE_DESC));
        if (modes)
        {
            hr = IDXGIOutput_GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &smallCount, modes);
            ok(hr == DXGI_ERROR_MORE_DATA, "Expected MORE_DATA, got 0x%08lx\n", hr);
            HeapFree(GetProcessHeap(), 0, modes);
        }
    }

    IDXGIOutput_Release(output);
}

static void test_FindClosestMatchingMode(void)
{
    IDXGIOutput *output;
    DXGI_MODE_DESC requested, result;
    HRESULT hr;

    output = get_first_output();
    if (!output)
    {
        skip("No output available\n");
        return;
    }

    memset(&requested, 0, sizeof(requested));
    requested.Width = 1024;
    requested.Height = 768;
    requested.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    hr = IDXGIOutput_FindClosestMatchingMode(output, &requested, &result, NULL);
    ok(hr == S_OK, "FindClosestMatchingMode failed: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        trace("Closest match for 1024x768: %ux%u @ %u/%u Hz\n",
              result.Width, result.Height,
              result.RefreshRate.Numerator, result.RefreshRate.Denominator);
        ok(result.Width > 0, "Result width is 0\n");
        ok(result.Height > 0, "Result height is 0\n");
    }

    /* Test with NULL parameters */
    hr = IDXGIOutput_FindClosestMatchingMode(output, NULL, &result, NULL);
    ok(FAILED(hr), "FindClosestMatchingMode(NULL mode) should fail\n");

    hr = IDXGIOutput_FindClosestMatchingMode(output, &requested, NULL, NULL);
    ok(FAILED(hr), "FindClosestMatchingMode(NULL result) should fail\n");

    IDXGIOutput_Release(output);
}

static void test_GetDesc(void)
{
    IDXGIOutput *output;
    DXGI_OUTPUT_DESC desc;
    HRESULT hr;

    output = get_first_output();
    if (!output)
    {
        skip("No output available\n");
        return;
    }

    hr = IDXGIOutput_GetDesc(output, &desc);
    ok(hr == S_OK, "GetDesc failed: 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        trace("Output: %S\n", desc.DeviceName);
        trace("  Desktop: (%ld,%ld)-(%ld,%ld)\n",
              desc.DesktopCoordinates.left, desc.DesktopCoordinates.top,
              desc.DesktopCoordinates.right, desc.DesktopCoordinates.bottom);
        trace("  Attached: %d\n", desc.AttachedToDesktop);
        trace("  Monitor: %p\n", desc.Monitor);
        ok(desc.DeviceName[0] != 0, "Output DeviceName is empty\n");
        ok(is_display_device_name(desc.DeviceName),
           "Output DeviceName should use \\\\.\\DISPLAY*, got %S\n",
           desc.DeviceName);
        ok(desc.AttachedToDesktop,
           "Output should be attached to the desktop\n");
        ok(desc.DesktopCoordinates.right > desc.DesktopCoordinates.left,
           "Output desktop rectangle has no width\n");
        ok(desc.DesktopCoordinates.bottom > desc.DesktopCoordinates.top,
           "Output desktop rectangle has no height\n");
    }

    /* Test with NULL */
    hr = IDXGIOutput_GetDesc(output, NULL);
    ok(FAILED(hr), "GetDesc(NULL) should fail\n");

    IDXGIOutput_Release(output);
}

START_TEST(DxgiOutput)
{
    CoInitialize(NULL);

    test_GetDisplayModeList();
    test_FindClosestMatchingMode();
    test_GetDesc();

    CoUninitialize();
}
