/*
 * Copyright 2026 ReactOS contributors
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define COBJMACROS
#include "windows.h"
#include "wincodec.h"
#include "wine/test.h"

static void test_filtered_scaling(IWICImagingFactory *factory)
{
    static const BYTE half_cubic[16] = {128,128,128,128,128,128,128,126,156,135,127,128,128,128,128,128};
    static const BYTE double_cubic[64] =
    {
        128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
        128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,127,
        124,143,184,184,143,124,127,128,128,128,128,128,128,128,128,128,
        128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128
    };
    static const struct {UINT size, mode;} cases[] = {{1,3}, {16,3}, {16,4}, {64,4}};
    DWORD source[32 * 32], output[64 * 64], row[64], partial[17 * 5];
    IWICBitmap *bitmap;
    IWICBitmapScaler *scaler;
    WICPixelFormatGUID format;
    HRESULT hr;
    UINT x, y, t;

    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            source[y * 32 + x] = 0xff000000 | ((128 + (x == 17 ? 64 : 0)) * 0x010101);
    hr = IWICImagingFactory_CreateBitmapFromMemory(factory, 32, 32, &GUID_WICPixelFormat32bppBGRA, 32 * 4, sizeof(source), (BYTE *)source, &bitmap);
    ok(hr == S_OK, "CreateBitmapFromMemory failed %#lx\n", hr);
    if (FAILED(hr)) return;
    for (t = 0; t < ARRAY_SIZE(cases); ++t)
    {
        UINT size = cases[t].size, mode = cases[t].mode;
        hr = IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
        ok(hr == S_OK, "CreateBitmapScaler failed %#lx\n", hr);
        if (FAILED(hr)) continue;
        hr = IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)bitmap, size, size, mode);
        ok(hr == S_OK, "Initialize failed %#lx\n", hr);
        hr = IWICBitmapScaler_GetPixelFormat(scaler, &format);
        ok(hr == S_OK && IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA), "Unexpected pixel format\n");
        hr = IWICBitmapScaler_CopyPixels(scaler, NULL, size * 4, size * size * 4, (BYTE *)output);
        ok(hr == S_OK, "CopyPixels failed %#lx\n", hr);
        for (y = 0; y < size; ++y)
        {
            WICRect rect = {0, y, size, 1};
            hr = IWICBitmapScaler_CopyPixels(scaler, &rect, size * 4, size * 4, (BYTE *)row);
            ok(hr == S_OK, "Row CopyPixels failed %#lx\n", hr);
            ok(!memcmp(row, output + y * size, size * 4), "Whole/row result mismatch\n");
            for (x = 0; x < size; ++x)
            {
                BYTE expected = mode == 3 ? (size == 1 ? 130 : x == 8 ? 160 : 128) : size == 16 ? half_cubic[x] : double_cubic[x];
                DWORD pixel = 0xff000000 | (expected * 0x010101);
                ok(output[y * size + x] == pixel, "Mode %u, size %u (%u,%u): %#lx expected %#lx\n", mode, size, x, y, output[y * size + x], pixel);
            }
        }
        if (size >= 16)
        {
            WICRect rect = {3, 4, 9, 5};
            memset(partial, 0xa5, sizeof(partial));
            hr = IWICBitmapScaler_CopyPixels(scaler, &rect, 17 * 4, sizeof(partial), (BYTE *)partial);
            ok(hr == S_OK, "Partial CopyPixels failed %#lx\n", hr);
            for (y = 0; y < 5; ++y)
            {
                ok(!memcmp(partial + y * 17, output + (y + 4) * size + 3, 9 * 4), "Partial result mismatch\n");
                for (x = 9; x < 17; ++x)
                    ok(partial[y * 17 + x] == 0xa5a5a5a5, "Stride padding overwritten\n");
            }
            hr = IWICBitmapScaler_CopyPixels(scaler, &rect, 9 * 4 - 1, sizeof(partial), (BYTE *)partial);
            ok(hr == E_INVALIDARG, "Small stride returned %#lx\n", hr);
        }
        IWICBitmapScaler_Release(scaler);
    }
    IWICBitmap_Release(bitmap);

    /* Transparent red must not bleed into opaque blue while filtering. */
    for (x = 0; x < ARRAY_SIZE(source); ++x)
        source[x] = x % 2 ? 0xff0000ff : 0x00ff0000;
    hr = IWICImagingFactory_CreateBitmapFromMemory(factory, 32, 32, &GUID_WICPixelFormat32bppBGRA, 32 * 4, sizeof(source), (BYTE *)source, &bitmap);
    ok(hr == S_OK, "CreateBitmapFromMemory failed %#lx\n", hr);
    if (FAILED(hr)) return;
    for (t = 3; t <= 4; ++t)
    {
        static const BYTE alpha[16] = {102,130,128,128,128,128,128,128,128,128,128,128,128,128,125,153};
        hr = IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
        ok(hr == S_OK, "CreateBitmapScaler failed %#lx\n", hr);
        if (FAILED(hr)) continue;
        hr = IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)bitmap, 16, 16, t);
        ok(hr == S_OK, "Initialize failed %#lx\n", hr);
        hr = IWICBitmapScaler_CopyPixels(scaler, NULL, 16 * 4, 16 * 16 * 4, (BYTE *)output);
        ok(hr == S_OK, "CopyPixels failed %#lx\n", hr);
        for (y = 0; y < 16; ++y)
            for (x = 0; x < 16; ++x)
            {
                DWORD pixel = output[y * 16 + x];
                ok((pixel >> 24) == (t == 3 ? 128 : alpha[x]), "Incorrect alpha %#lx at (%u,%u), mode %u\n", pixel, x, y, t);
                ok(!(pixel & 0x00ffff00) && (pixel & 255) >= 254, "Transparent color leaked: %#lx\n", pixel);
            }
        IWICBitmapScaler_Release(scaler);
    }
    IWICBitmap_Release(bitmap);
}

START_TEST(scaler)
{
    IWICImagingFactory *factory;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx failed %#lx\n", hr);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    ok(hr == S_OK, "CoCreateInstance failed %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        test_filtered_scaling(factory);
        IWICImagingFactory_Release(factory);
    }
    CoUninitialize();
}
