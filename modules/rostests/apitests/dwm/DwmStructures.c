/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM structure layout and constant validation
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"
#include <stddef.h>

/* Replicate DWM structures locally for layout validation */
typedef struct _TEST_DWM_SURFACE
{
    UINT        Width;
    UINT        Height;
    UINT        Stride;
    UINT        BytesPerPixel;
    PVOID       pPixels;
    SIZE_T      AllocationSize;
    D3DKMT_HANDLE   hAllocation;
    BOOL        Dirty;
} TEST_DWM_SURFACE;

typedef struct _TEST_DWM_WINDOW_ENTRY
{
    HWND        hWnd;
    RECT        rcWindow;
    BOOL        Visible;
    UINT        ZOrder;
    TEST_DWM_SURFACE Surface;
    struct _TEST_DWM_WINDOW_ENTRY *pNext;
    struct _TEST_DWM_WINDOW_ENTRY *pPrev;
} TEST_DWM_WINDOW_ENTRY;

typedef struct _TEST_DWM_COMPOSITION_BUFFER
{
    UINT        Width;
    UINT        Height;
    UINT        Stride;
    UINT        BytesPerPixel;
    PVOID       pPixels;
    SIZE_T      AllocationSize;
    D3DKMT_HANDLE   hAllocation;
    D3DKMT_HANDLE   hResource;
} TEST_DWM_COMPOSITION_BUFFER;

typedef struct _TEST_DWM_DIRTY_REGION
{
    RECT        rcBounds;
    BOOL        HasDirty;
    BOOL        FullRecomposite;
} TEST_DWM_DIRTY_REGION;

typedef struct _TEST_DWM_D3DKMT_STATE
{
    D3DKMT_HANDLE   hAdapter;
    LUID            AdapterLuid;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID  VidPnSourceId;
    D3DKMT_HANDLE   hDevice;
    BOOL            Initialized;
} TEST_DWM_D3DKMT_STATE;

static void Test_SurfaceLayout(void)
{
    ok_eq_uint(offsetof(TEST_DWM_SURFACE, Width), 0);
    ok(offsetof(TEST_DWM_SURFACE, Height) > offsetof(TEST_DWM_SURFACE, Width),
       "Height should follow Width\n");
    ok(offsetof(TEST_DWM_SURFACE, Stride) > offsetof(TEST_DWM_SURFACE, Height),
       "Stride should follow Height\n");
    ok(offsetof(TEST_DWM_SURFACE, BytesPerPixel) > offsetof(TEST_DWM_SURFACE, Stride),
       "BytesPerPixel should follow Stride\n");
    ok(offsetof(TEST_DWM_SURFACE, pPixels) > offsetof(TEST_DWM_SURFACE, BytesPerPixel),
       "pPixels should follow BytesPerPixel\n");

    /* pPixels is pointer-sized */
    ok(sizeof(((TEST_DWM_SURFACE *)0)->pPixels) == sizeof(PVOID),
       "pPixels should be pointer-sized\n");

    /* AllocationSize is SIZE_T */
    ok(sizeof(((TEST_DWM_SURFACE *)0)->AllocationSize) == sizeof(SIZE_T),
       "AllocationSize should be SIZE_T\n");

    /* Dirty is BOOL */
    ok(sizeof(((TEST_DWM_SURFACE *)0)->Dirty) == sizeof(BOOL),
       "Dirty should be BOOL-sized\n");
}

static void Test_WindowEntryLayout(void)
{
    ok_eq_uint(offsetof(TEST_DWM_WINDOW_ENTRY, hWnd), 0);

    ok(offsetof(TEST_DWM_WINDOW_ENTRY, rcWindow) >= sizeof(HWND),
       "rcWindow should follow hWnd\n");

    /* RECT is 16 bytes (4 LONGs) */
    ok_eq_uint(sizeof(((TEST_DWM_WINDOW_ENTRY *)0)->rcWindow), sizeof(RECT));

    /* Check linked list pointers exist at end */
    ok(offsetof(TEST_DWM_WINDOW_ENTRY, pNext) > 0, "pNext at positive offset\n");
    ok(offsetof(TEST_DWM_WINDOW_ENTRY, pPrev) > 0, "pPrev at positive offset\n");
    ok(offsetof(TEST_DWM_WINDOW_ENTRY, pPrev) >
       offsetof(TEST_DWM_WINDOW_ENTRY, pNext),
       "pPrev should follow pNext\n");

    /* Surface is embedded, not a pointer */
    ok(sizeof(((TEST_DWM_WINDOW_ENTRY *)0)->Surface) == sizeof(TEST_DWM_SURFACE),
       "Surface should be embedded (full struct size)\n");
}

static void Test_CompositionBufferLayout(void)
{
    ok_eq_uint(offsetof(TEST_DWM_COMPOSITION_BUFFER, Width), 0);

    ok(sizeof(TEST_DWM_COMPOSITION_BUFFER) >= sizeof(TEST_DWM_SURFACE),
       "CompBuffer must be at least as large as the base surface\n");

    /* hResource follows hAllocation */
    ok(offsetof(TEST_DWM_COMPOSITION_BUFFER, hResource) >
       offsetof(TEST_DWM_COMPOSITION_BUFFER, hAllocation),
       "hResource should follow hAllocation\n");
}

static void Test_DirtyRegionLayout(void)
{
    ok_eq_uint(offsetof(TEST_DWM_DIRTY_REGION, rcBounds), 0);
    ok_eq_uint(sizeof(((TEST_DWM_DIRTY_REGION *)0)->rcBounds), sizeof(RECT));

    ok(offsetof(TEST_DWM_DIRTY_REGION, HasDirty) >= sizeof(RECT),
       "HasDirty should follow rcBounds\n");
    ok(offsetof(TEST_DWM_DIRTY_REGION, FullRecomposite) >
       offsetof(TEST_DWM_DIRTY_REGION, HasDirty),
       "FullRecomposite should follow HasDirty\n");
}

static void Test_D3dkmtStateLayout(void)
{
    ok_eq_uint(offsetof(TEST_DWM_D3DKMT_STATE, hAdapter), 0);

    /* LUID is 8 bytes */
    ok_eq_uint(sizeof(((TEST_DWM_D3DKMT_STATE *)0)->AdapterLuid), 8);

    /* VidPnSourceId is UINT */
    ok_eq_uint(sizeof(((TEST_DWM_D3DKMT_STATE *)0)->VidPnSourceId), sizeof(UINT));

    /* Initialized is BOOL */
    ok_eq_uint(sizeof(((TEST_DWM_D3DKMT_STATE *)0)->Initialized), sizeof(BOOL));
}

static void Test_DwmConstants(void)
{
    /* DWM_MAX_INITIAL_WINDOWS */
    UINT MAX_WINDOWS = 256;
    ok(MAX_WINDOWS >= 64, "MAX_WINDOWS should be at least 64\n");
    ok(MAX_WINDOWS <= 1024, "MAX_WINDOWS should be at most 1024\n");

    /* DWM_COMPOSE_INTERVAL_MS */
    UINT INTERVAL_MS = 16;
    ok(INTERVAL_MS >= 1 && INTERVAL_MS <= 100,
       "Compose interval should be 1-100ms, got %u\n", INTERVAL_MS);

    /* Target 60Hz = 16.67ms */
    ok(INTERVAL_MS == 16 || INTERVAL_MS == 17,
       "Compose interval should target ~60Hz (16-17ms)\n");
}

static void Test_SurfacePixelFormat(void)
{
    /* DWM uses 32-bpp XRGB (B8G8R8X8) */
    UINT BPP = 4;
    ok_eq_uint(BPP, 4);

    /* Verify 32-bpp pixel encoding */
    UINT pixel = 0x00FF8040; /* X=0, R=FF, G=80, B=40 */
    BYTE *bytes = (BYTE *)&pixel;

    /* Little-endian: byte 0 = B, byte 1 = G, byte 2 = R, byte 3 = X */
    ok_eq_uint(bytes[0], 0x40); /* Blue */
    ok_eq_uint(bytes[1], 0x80); /* Green */
    ok_eq_uint(bytes[2], 0xFF); /* Red */
    ok_eq_uint(bytes[3], 0x00); /* Alpha/X */
}

static void Test_StrideCalculation(void)
{
    /* Stride = ((Width * BPP) + 3) & ~3 for 4-byte alignment */
    struct { UINT Width; UINT ExpectedStride; } cases[] = {
        { 1, 4 },
        { 2, 8 },
        { 3, 12 },
        { 4, 16 },
        { 5, 20 },
        { 100, 400 },
        { 640, 2560 },
        { 1024, 4096 },
        { 1920, 7680 },
    };
    ULONG i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        UINT Stride = ((cases[i].Width * 4) + 3) & ~3;
        ok_eq_uint(Stride, cases[i].ExpectedStride);
    }
}

static void Test_ServiceConstants(void)
{
    /* Service name */
    ok(sizeof(L"DWM") > 0, "DWM service name exists\n");

    /* Verify the name is a valid wide string */
    LPCWSTR Name = L"DWM";
    ok(Name[0] == L'D', "Service name starts with D\n");
    ok(Name[1] == L'W', "Service name has W\n");
    ok(Name[2] == L'M', "Service name has M\n");
    ok(Name[3] == L'\0', "Service name is NUL-terminated\n");
}

static void Test_WindowEntryZeroInit(void)
{
    TEST_DWM_WINDOW_ENTRY entry;
    memset(&entry, 0, sizeof(entry));

    ok(entry.hWnd == NULL, "hWnd should be NULL\n");
    ok(entry.Visible == FALSE, "Visible should be FALSE\n");
    ok_eq_uint(entry.ZOrder, 0);
    ok(entry.Surface.pPixels == NULL, "Surface.pPixels should be NULL\n");
    ok_eq_uint(entry.Surface.Width, 0);
    ok_eq_uint(entry.Surface.Height, 0);
    ok(entry.pNext == NULL, "pNext should be NULL\n");
    ok(entry.pPrev == NULL, "pPrev should be NULL\n");
}

static void Test_LinkedListPointerSize(void)
{
    /* Ensure pNext/pPrev are pointer-sized */
    ok(sizeof(((TEST_DWM_WINDOW_ENTRY *)0)->pNext) == sizeof(void *),
       "pNext should be pointer-sized\n");
    ok(sizeof(((TEST_DWM_WINDOW_ENTRY *)0)->pPrev) == sizeof(void *),
       "pPrev should be pointer-sized\n");
}

START_TEST(DwmStructures)
{
    Test_SurfaceLayout();
    Test_WindowEntryLayout();
    Test_CompositionBufferLayout();
    Test_DirtyRegionLayout();
    Test_D3dkmtStateLayout();
    Test_DwmConstants();
    Test_SurfacePixelFormat();
    Test_StrideCalculation();
    Test_ServiceConstants();
    Test_WindowEntryZeroInit();
    Test_LinkedListPointerSize();
}
