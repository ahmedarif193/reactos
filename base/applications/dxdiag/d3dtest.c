/*
 * PROJECT:         ReactX Diagnosis Application
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            base/applications/dxdiag/d3dtest.c
 * PURPOSE:         ReactX Direct3D 7, 8 and 9 tests
 * PROGRAMMERS:     Gregor Gullwi <gbrunmar (dot) ros (at) gmail (dot) com>
 */

#include "precomp.h"

#include <math.h>
#include <png.h>
#include <setjmp.h>
#include <stdio.h>

#define WIDTH   800
#define HEIGHT  600

BOOL D3D7Test(GUID *lpDevice, HWND hWnd);
BOOL D3D8Test(GUID *lpDevice, HWND hWnd);
BOOL D3D9Test(GUID *lpDevice, HWND hWnd);

typedef struct
{
    FLOAT x;
    FLOAT y;
    FLOAT z;
} D3DTEST_POINT;

typedef struct
{
    UINT Index;
    FLOAT Depth;
} D3DTEST_FACE_ORDER;

VOID D3DTestBuildCube(PD3DTEST_VERTEX Vertices, DWORD Elapsed, UINT Width, UINT Height)
{
    static const D3DTEST_POINT Cube[8] =
    {
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}
    };
    static const UINT Faces[6][4] =
    {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
        {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}
    };
    static const DWORD Shades[6] =
    {
        0xffffffff, 0xffe8e8e8, 0xffd8d8d8,
        0xfff0f0f0, 0xffc8c8c8, 0xffe0e0e0
    };
    static const UINT Triangles[6] = {0, 1, 2, 0, 2, 3};
    static const FLOAT TexCoords[4][2] =
    {
        {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
    };
    D3DTEST_POINT Rotated[8];
    D3DTEST_FACE_ORDER Order[6];
    FLOAT ProjectedX[8];
    FLOAT ProjectedY[8];
    FLOAT ProjectedZ[8];
    FLOAT angle, cx, cy, focal, cos_x, sin_x, cos_y, sin_y;
    UINT i, j, k, out = 0;

    angle = (FLOAT)Elapsed * 0.0015f;
    cos_x = (FLOAT)cos(angle * 0.7f);
    sin_x = (FLOAT)sin(angle * 0.7f);
    cos_y = (FLOAT)cos(angle);
    sin_y = (FLOAT)sin(angle);
    cx = (FLOAT)Width * 0.5f;
    cy = (FLOAT)Height * 0.5f;
    focal = (FLOAT)(Width < Height ? Width : Height) * 1.2f;

    for (i = 0; i < 8; ++i)
    {
        FLOAT x = Cube[i].x * cos_y + Cube[i].z * sin_y;
        FLOAT z = -Cube[i].x * sin_y + Cube[i].z * cos_y;
        FLOAT y = Cube[i].y * cos_x - z * sin_x;

        Rotated[i].x = x;
        Rotated[i].y = y;
        Rotated[i].z = Cube[i].y * sin_x + z * cos_x + 4.0f;
        ProjectedX[i] = cx + x * focal / Rotated[i].z;
        ProjectedY[i] = cy - y * focal / Rotated[i].z;
        ProjectedZ[i] = (Rotated[i].z - 2.0f) * 0.25f;
    }

    for (i = 0; i < 6; ++i)
    {
        Order[i].Index = i;
        Order[i].Depth = 0.0f;
        for (j = 0; j < 4; ++j)
            Order[i].Depth += Rotated[Faces[i][j]].z;
    }

    for (i = 0; i < 5; ++i)
    {
        UINT farthest = i;

        for (j = i + 1; j < 6; ++j)
        {
            if (Order[j].Depth > Order[farthest].Depth)
                farthest = j;
        }

        if (farthest != i)
        {
            D3DTEST_FACE_ORDER tmp = Order[i];
            Order[i] = Order[farthest];
            Order[farthest] = tmp;
        }
    }

    for (i = 0; i < 6; ++i)
    {
        UINT face = Order[i].Index;

        for (j = 0; j < 6; ++j)
        {
            UINT corner = Triangles[j];

            k = Faces[face][corner];
            Vertices[out].x = ProjectedX[k];
            Vertices[out].y = ProjectedY[k];
            Vertices[out].z = ProjectedZ[k];
            Vertices[out].rhw = 1.0f / Rotated[k].z;
            Vertices[out].color = Shades[face];
            Vertices[out].u = TexCoords[corner][0];
            Vertices[out].v = TexCoords[corner][1];
            ++out;
        }
    }
}

typedef struct
{
    const BYTE *Data;
    SIZE_T Size;
    SIZE_T Offset;
} D3DTEST_PNG_READER, *PD3DTEST_PNG_READER;

static VOID PNGAPI D3DTestReadPng(png_structp Png, png_bytep Output, png_size_t Size)
{
    PD3DTEST_PNG_READER Reader = png_get_io_ptr(Png);

    if (!Reader || Reader->Offset > Reader->Size || Size > Reader->Size - Reader->Offset)
        png_error(Png, "Unexpected end of DxDiag texture data");

    CopyMemory(Output, Reader->Data + Reader->Offset, Size);
    Reader->Offset += Size;
}

BOOL D3DTestLoadTexture(PVOID Bits, LONG Pitch)
{
    HINSTANCE Instance = GetModuleHandleW(NULL);
    HRSRC ResourceInfo;
    HGLOBAL Resource;
    const BYTE *ResourceData;
    DWORD ResourceSize;
    D3DTEST_PNG_READER Reader;
    png_structp Png = NULL;
    png_infop Info = NULL;
    png_bytep ImageBits = NULL;
    png_bytepp Rows = NULL;
    png_uint_32 Width, Height;
    png_size_t RowBytes;
    int BitDepth, ColorType, InterlaceMethod;
    BYTE *Destination = Bits;
    UINT x, y;
    BOOL Result = FALSE;

    if (!Bits || !Pitch)
        return FALSE;

    if (!(ResourceInfo = FindResourceW(Instance, MAKEINTRESOURCEW(IDR_D3DTEST_TEXTURE), RT_RCDATA)))
        goto cleanup;
    if (!(Resource = LoadResource(Instance, ResourceInfo)))
        goto cleanup;
    ResourceData = LockResource(Resource);
    ResourceSize = SizeofResource(Instance, ResourceInfo);
    if (!ResourceData || ResourceSize < 8 || png_sig_cmp(ResourceData, 0, 8))
        goto cleanup;

    if (!(Png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)))
        goto cleanup;
    if (!(Info = png_create_info_struct(Png)))
        goto cleanup;
    if (setjmp(png_jmpbuf(Png)))
        goto cleanup;

    Reader.Data = ResourceData;
    Reader.Size = ResourceSize;
    Reader.Offset = 0;
    png_set_read_fn(Png, &Reader, D3DTestReadPng);
    png_read_info(Png, Info);
    if (!png_get_IHDR(Png, Info, &Width, &Height, &BitDepth, &ColorType, &InterlaceMethod, NULL, NULL))
        goto cleanup;
    if (Width != D3DTEST_TEXTURE_SIZE || Height != D3DTEST_TEXTURE_SIZE || BitDepth != 8 || ColorType != PNG_COLOR_TYPE_RGBA || InterlaceMethod != PNG_INTERLACE_NONE)
        goto cleanup;
    RowBytes = png_get_rowbytes(Png, Info);
    if (RowBytes != Width * 4)
        goto cleanup;

    if (!(ImageBits = HeapAlloc(GetProcessHeap(), 0, RowBytes * Height)))
        goto cleanup;
    if (!(Rows = HeapAlloc(GetProcessHeap(), 0, Height * sizeof(*Rows))))
        goto cleanup;
    for (y = 0; y < Height; ++y)
        Rows[y] = ImageBits + y * RowBytes;
    png_read_image(Png, Rows);
    png_read_end(Png, Info);

    if (Pitch < 0)
        Destination += (D3DTEST_TEXTURE_SIZE - 1) * (size_t)-Pitch;
    for (y = 0; y < D3DTEST_TEXTURE_SIZE; ++y)
    {
        const BYTE *SourceRow = ImageBits + y * RowBytes;
        DWORD *DestinationRow = (DWORD *)(Destination + (LONG_PTR)y * Pitch);

        for (x = 0; x < D3DTEST_TEXTURE_SIZE; ++x)
        {
            const BYTE *Source = SourceRow + x * 4;
            DWORD Alpha = Source[3];

            DestinationRow[x] = 0xff000000 | (((Source[0] * Alpha + 127) / 255) << 16) | (((Source[1] * Alpha + 127) / 255) << 8) | ((Source[2] * Alpha + 127) / 255);
        }
    }
    Result = TRUE;

cleanup:
    if (Rows)
        HeapFree(GetProcessHeap(), 0, Rows);
    if (ImageBits)
        HeapFree(GetProcessHeap(), 0, ImageBits);
    if (Png)
        png_destroy_read_struct(&Png, Info ? &Info : NULL, NULL);
    return Result;
}

BOOL D3DTestPumpMessages(VOID)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return FALSE;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return TRUE;
}

BOOL StartD3DTest(GUID *lpDevice, HWND hWnd, HINSTANCE hInstance, WCHAR* pszCaption, INT TestNr)
{
    WCHAR szTestDescriptionRaw[256];
    WCHAR szTestDescription[256];
    WCHAR szResultRaw[256];
    WCHAR szResult[256];
    WCHAR szError[256];
    BOOL Result;

    LoadStringW(hInstance, IDS_DDTEST_ERROR, szError, sizeof(szError) / sizeof(WCHAR));
    LoadStringW(hInstance, IDS_D3DTEST_D3Dx, szTestDescriptionRaw, sizeof(szTestDescriptionRaw) / sizeof(WCHAR));
    LoadStringW(hInstance, IDS_D3DTEST_RESULT, szResultRaw, sizeof(szResultRaw) / sizeof(WCHAR));
    StringCchPrintfW(szResult, ARRAYSIZE(szResult), szResultRaw, TestNr);

    _swprintf(szTestDescription, szTestDescriptionRaw, TestNr);
    if (MessageBox(NULL, szTestDescription, pszCaption, MB_YESNO | MB_ICONQUESTION) == IDNO)
        return FALSE;

    ShowWindow(hWnd, SW_SHOW);

    switch (TestNr)
    {
        case 7:
            Result = D3D7Test(lpDevice, hWnd);
            break;

        case 8:
            Result = D3D8Test(lpDevice, hWnd);
            break;

        case 9:
            Result = D3D9Test(lpDevice, hWnd);
            break;

        default:
            Result = FALSE;
    }

    ShowWindow(hWnd, SW_HIDE);

    if (!Result)
    {
        MessageBox(NULL, szError, pszCaption, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (MessageBox(NULL, szResult, pszCaption, MB_YESNO | MB_ICONQUESTION) == IDYES)
        return TRUE;

    return FALSE;
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

VOID D3DTests(GUID *lpDevice)
{
    WNDCLASSEX winClass;
    HWND hWnd;
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WCHAR szDescription[256];
    WCHAR szCaption[256];

    winClass.cbSize = sizeof(WNDCLASSEX);
    winClass.style = CS_DBLCLKS | CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    winClass.lpfnWndProc = WindowProc;
    winClass.cbClsExtra = 0;
    winClass.cbWndExtra = 0;
    winClass.hInstance = hInstance;
    winClass.hIcon = 0;
    winClass.hCursor = 0;
    winClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    winClass.lpszMenuName = NULL;
    winClass.lpszClassName = L"d3dtest";
    winClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&winClass))
        return;

    hWnd = CreateWindowEx(
        0,
        winClass.lpszClassName,
        NULL,
        WS_POPUP,
        (GetSystemMetrics(SM_CXSCREEN) - WIDTH)/2,
        (GetSystemMetrics(SM_CYSCREEN) - HEIGHT)/2,
        WIDTH,
        HEIGHT,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hWnd)
        goto cleanup;

    LoadStringW(hInstance, IDS_D3DTEST_DESCRIPTION, szDescription, sizeof(szDescription) / sizeof(WCHAR));
    LoadStringW(hInstance, IDS_MAIN_DIALOG, szCaption, sizeof(szCaption) / sizeof(WCHAR));
    if(MessageBox(NULL, szDescription, szCaption, MB_YESNO | MB_ICONQUESTION) == IDNO)
        goto cleanup;

    StartD3DTest(lpDevice, hWnd, hInstance, szCaption, 7);
    StartD3DTest(lpDevice, hWnd, hInstance, szCaption, 8);
    StartD3DTest(lpDevice, hWnd, hInstance, szCaption, 9);

cleanup:
    DestroyWindow(hWnd);
    UnregisterClass(winClass.lpszClassName, hInstance);
}
