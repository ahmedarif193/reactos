/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for LoadIconWithScaleDown frame selection and downscaling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <commctrl.h>

#include <pshpack2.h>
typedef struct
{
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    DWORD dwImageOffset;
} TEST_ICONDIRENTRY;

typedef struct
{
    WORD idReserved;
    WORD idType;
    WORD idCount;
    TEST_ICONDIRENTRY idEntries[2];
} TEST_ICONDIR;
#include <poppack.h>

#define SMALL_FRAME 16
#define LARGE_FRAME 32
#define SMALL_COLOR 0xffff0000
#define LARGE_DARK  0xff000000
#define LARGE_LIGHT 0xffffffff

typedef HRESULT (WINAPI *PLOADICONWITHSCALEDOWN)(HINSTANCE, PCWSTR, int, int, HICON *);
typedef HRESULT (WINAPI *PLOADICONMETRIC)(HINSTANCE, PCWSTR, int, HICON *);

static PLOADICONWITHSCALEDOWN pLoadIconWithScaleDown;
static PLOADICONMETRIC pLoadIconMetric;

static DWORD
frame_bytes(int size)
{
    return sizeof(BITMAPINFOHEADER) + size * size * sizeof(DWORD) + ((size + 31) / 32) * 4 * size;
}

static void
write_frame(BYTE *dest, int size, BOOL stripes)
{
    BITMAPINFOHEADER *header = (BITMAPINFOHEADER *)dest;
    DWORD *pixels = (DWORD *)(dest + sizeof(*header));
    int x, y;

    ZeroMemory(dest, frame_bytes(size));
    header->biSize = sizeof(*header);
    header->biWidth = size;
    header->biHeight = size * 2;
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biSizeImage = frame_bytes(size) - sizeof(*header);
    for (y = 0; y < size; y++)
    {
        for (x = 0; x < size; x++)
            pixels[y * size + x] = stripes ? ((x & 1) ? LARGE_LIGHT : LARGE_DARK) : SMALL_COLOR;
    }
}

static BOOL
write_icon_file(WCHAR *path)
{
    WCHAR tempPath[MAX_PATH];
    TEST_ICONDIR dir;
    BYTE *buffer;
    DWORD size, written = 0;
    HANDLE file;
    BOOL result;

    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"SCD", 0, path);

    size = sizeof(dir) + frame_bytes(SMALL_FRAME) + frame_bytes(LARGE_FRAME);
    buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!buffer)
        return FALSE;

    ZeroMemory(&dir, sizeof(dir));
    dir.idType = 1;
    dir.idCount = 2;
    dir.idEntries[0].bWidth = SMALL_FRAME;
    dir.idEntries[0].bHeight = SMALL_FRAME;
    dir.idEntries[0].wPlanes = 1;
    dir.idEntries[0].wBitCount = 32;
    dir.idEntries[0].dwBytesInRes = frame_bytes(SMALL_FRAME);
    dir.idEntries[0].dwImageOffset = sizeof(dir);
    dir.idEntries[1].bWidth = LARGE_FRAME;
    dir.idEntries[1].bHeight = LARGE_FRAME;
    dir.idEntries[1].wPlanes = 1;
    dir.idEntries[1].wBitCount = 32;
    dir.idEntries[1].dwBytesInRes = frame_bytes(LARGE_FRAME);
    dir.idEntries[1].dwImageOffset = sizeof(dir) + frame_bytes(SMALL_FRAME);

    CopyMemory(buffer, &dir, sizeof(dir));
    write_frame(buffer + dir.idEntries[0].dwImageOffset, SMALL_FRAME, FALSE);
    write_frame(buffer + dir.idEntries[1].dwImageOffset, LARGE_FRAME, TRUE);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    result = file != INVALID_HANDLE_VALUE && WriteFile(file, buffer, size, &written, NULL) && written == size;
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    HeapFree(GetProcessHeap(), 0, buffer);
    return result;
}

static DWORD *
read_icon_pixels(HICON icon, int *width, int *height)
{
    ICONINFO info;
    BITMAP bm;
    BITMAPINFO bmi;
    DWORD *pixels = NULL;
    HDC hdc;

    *width = *height = 0;
    if (!GetIconInfo(icon, &info))
        return NULL;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm))
    {
        *width = bm.bmWidth;
        *height = bm.bmHeight;
        pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bm.bmWidth * bm.bmHeight * sizeof(DWORD));
        hdc = CreateCompatibleDC(NULL);
        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bm.bmWidth;
        bmi.bmiHeader.biHeight = -bm.bmHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        if (pixels && hdc && GetDIBits(hdc, info.hbmColor, 0, bm.bmHeight, pixels, &bmi, DIB_RGB_COLORS) != bm.bmHeight)
        {
            HeapFree(GetProcessHeap(), 0, pixels);
            pixels = NULL;
        }
        if (hdc)
            DeleteDC(hdc);
    }
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    return pixels;
}

static BOOL
is_red(DWORD pixel)
{
    return ((pixel >> 16) & 0xff) > 0x80 && ((pixel >> 8) & 0xff) < 0x40 && (pixel & 0xff) < 0x40;
}

static BOOL
is_blended_gray(DWORD pixel)
{
    BYTE r = (pixel >> 16) & 0xff, g = (pixel >> 8) & 0xff, b = pixel & 0xff;
    return r == g && g == b && r > 0x20 && r < 0xe0;
}

static void
check_icon(const WCHAR *path, int request, BOOL expectRed, BOOL expectBlend)
{
    HICON icon = NULL;
    HRESULT hr;
    DWORD *pixels;
    int width, height, i, red = 0, blended = 0, total;

    hr = pLoadIconWithScaleDown(NULL, path, request, request, &icon);
    ok(hr == S_OK, "request %d: hr %#lx\n", request, hr);
    if (hr != S_OK)
        return;

    pixels = read_icon_pixels(icon, &width, &height);
    ok(width == request && height == request, "request %d: icon is %dx%d\n", request, width, height);
    ok(pixels != NULL, "request %d: cannot read icon pixels\n", request);
    if (pixels)
    {
        total = width * height;
        for (i = 0; i < total; i++)
        {
            red += is_red(pixels[i]);
            blended += is_blended_gray(pixels[i]);
        }
        trace("request %d: %d red, %d blended of %d pixels, first row %08lx %08lx %08lx %08lx\n",
              request, red, blended, total, pixels[0], pixels[1], pixels[2], pixels[3]);
        if (expectRed)
            ok(red == total, "request %d: expected the exact %dpx frame, got %d/%d red pixels\n", request, request, red, total);
        else
            ok(red == 0, "request %d: expected the larger frame, got %d red pixels\n", request, red);
        if (expectBlend)
            ok(blended > 0, "request %d: downscale is not smoothed, no blended pixels\n", request);
        HeapFree(GetProcessHeap(), 0, pixels);
    }
    DestroyIcon(icon);
}

START_TEST(LoadIconWithScaleDown)
{
    WCHAR path[MAX_PATH];
    HMODULE comctl32;
    HICON icon = NULL;
    HRESULT hr;
    DWORD *pixels;
    int width, height;

    comctl32 = LoadLibraryW(L"comctl32.dll");
    pLoadIconWithScaleDown = (PLOADICONWITHSCALEDOWN)GetProcAddress(comctl32, "LoadIconWithScaleDown");
    pLoadIconMetric = (PLOADICONMETRIC)GetProcAddress(comctl32, "LoadIconMetric");
    if (!pLoadIconWithScaleDown || !pLoadIconMetric)
    {
        skip("LoadIconWithScaleDown is not exported\n");
        return;
    }

    if (!write_icon_file(path))
    {
        skip("cannot write the test icon file\n");
        return;
    }

    check_icon(path, SMALL_FRAME, TRUE, FALSE);
    check_icon(path, 20, FALSE, TRUE);
    check_icon(path, 24, FALSE, TRUE);
    check_icon(path, LARGE_FRAME, FALSE, FALSE);
    check_icon(path, 48, FALSE, FALSE);

    hr = pLoadIconMetric(NULL, path, LIM_SMALL, &icon);
    ok(hr == S_OK, "LoadIconMetric hr %#lx\n", hr);
    if (hr == S_OK)
    {
        pixels = read_icon_pixels(icon, &width, &height);
        ok(width == GetSystemMetrics(SM_CXSMICON) && height == GetSystemMetrics(SM_CYSMICON),
           "LIM_SMALL icon is %dx%d\n", width, height);
        if (pixels)
            HeapFree(GetProcessHeap(), 0, pixels);
        DestroyIcon(icon);
    }

    hr = pLoadIconWithScaleDown(NULL, (PCWSTR)IDI_APPLICATION, 42, 42, &icon);
    ok(hr == S_OK, "IDI_APPLICATION hr %#lx\n", hr);
    if (hr == S_OK)
    {
        pixels = read_icon_pixels(icon, &width, &height);
        ok(width == 42 && height == 42, "IDI_APPLICATION icon is %dx%d\n", width, height);
        if (pixels)
            HeapFree(GetProcessHeap(), 0, pixels);
        DestroyIcon(icon);
    }

    DeleteFileW(path);
}
